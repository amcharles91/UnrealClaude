// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_EditorTransaction.h"
#include "UnrealClaudeModule.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/TransBuffer.h"
#endif

#if WITH_EDITOR
namespace
{
	/** Cast GEditor->Trans to UTransBuffer, or nullptr if unavailable. */
	UTransBuffer* GetTransBuffer()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		return Cast<UTransBuffer>(GEditor->Trans);
	}
}
#endif

FMCPToolInfo FMCPTool_EditorTransaction::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("editor_transaction");
	Info.Description = TEXT(
		"Drive the Unreal Engine editor undo/redo (transaction) system.\n\n"
		"Operations (set 'operation'):\n"
		"- 'undo': Undo the last transaction; optional 'count' (default 1) undoes multiple\n"
		"- 'redo': Redo the last undone transaction; optional 'count' (default 1) redoes multiple\n"
		"- 'can_undo': Whether anything can be undone, plus the next-undo description\n"
		"- 'can_redo': Whether anything can be redone, plus the next-redo description\n"
		"- 'get_history': Inspect the buffer; optional 'max_entries' (default 20) for undo and redo lists\n"
		"- 'get_counts': Number of undoable and redoable transactions\n"
		"- 'reset': Clear the entire undo/redo buffer (cannot itself be undone); optional 'reason'\n\n"
		"Most native MCP tools already wrap their mutations in transactions, so 'undo'/'redo' "
		"revert or replay those operations. Each MCP call is independent, so cross-call "
		"begin/end transaction grouping is not supported.\n\n"
		"Returns: operation-specific data (descriptions, history entries, counts)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: undo, redo, can_undo, can_redo, get_history, get_counts, reset"), true),
		FMCPToolParameter(TEXT("count"), TEXT("number"), TEXT("For 'undo'/'redo': number of transactions to process (default 1)"), false),
		FMCPToolParameter(TEXT("max_entries"), TEXT("number"), TEXT("For 'get_history': max entries per list (default 20)"), false),
		FMCPToolParameter(TEXT("reason"), TEXT("string"), TEXT("For 'reset': reason logged for diagnostics (default 'Manual reset')"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Destructive();
	return Info;
}

FMCPToolResult FMCPTool_EditorTransaction::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("undo"))        { return OpUndo(Params); }
	if (Operation == TEXT("redo"))        { return OpRedo(Params); }
	if (Operation == TEXT("can_undo"))    { return OpCanUndo(Params); }
	if (Operation == TEXT("can_redo"))    { return OpCanRedo(Params); }
	if (Operation == TEXT("get_history")) { return OpGetHistory(Params); }
	if (Operation == TEXT("get_counts"))  { return OpGetCounts(Params); }
	if (Operation == TEXT("reset"))       { return OpReset(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: undo, redo, can_undo, can_redo, get_history, get_counts, reset"), *Operation));
}

FMCPToolResult FMCPTool_EditorTransaction::OpUndo(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	if (!GEditor || !GEditor->Trans)
	{
		return FMCPToolResult::Error(TEXT("No editor transaction system available"));
	}

	const int32 Count = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("count"), 1));

	TArray<FString> UndoneTitles;
	for (int32 i = 0; i < Count; ++i)
	{
		if (!GEditor->Trans->CanUndo())
		{
			break;
		}
		const FString Title = GEditor->Trans->GetUndoContext(false).Title.ToString();
		if (!GEditor->UndoTransaction())
		{
			break;
		}
		UndoneTitles.Add(Title);
		UE_LOG(LogUnrealClaude, Log, TEXT("Undid transaction: %s"), *Title);
	}

	if (UndoneTitles.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Nothing to undo"));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("undone"), StringArrayToJsonArray(UndoneTitles));
	Data->SetNumberField(TEXT("count"), UndoneTitles.Num());
	Data->SetNumberField(TEXT("requested"), Count);

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Undone %d of %d requested transaction(s)"), UndoneTitles.Num(), Count), Data);
	if (UndoneTitles.Num() < Count)
	{
		Result.Warnings.Add(TEXT("Reached the end of the undo buffer before completing all requested undos"));
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("Editor transactions require an editor build"));
#endif
}

FMCPToolResult FMCPTool_EditorTransaction::OpRedo(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	if (!GEditor || !GEditor->Trans)
	{
		return FMCPToolResult::Error(TEXT("No editor transaction system available"));
	}

	const int32 Count = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("count"), 1));

	TArray<FString> RedoneTitles;
	for (int32 i = 0; i < Count; ++i)
	{
		if (!GEditor->Trans->CanRedo())
		{
			break;
		}
		const FString Title = GEditor->Trans->GetRedoContext().Title.ToString();
		if (!GEditor->RedoTransaction())
		{
			break;
		}
		RedoneTitles.Add(Title);
		UE_LOG(LogUnrealClaude, Log, TEXT("Redid transaction: %s"), *Title);
	}

	if (RedoneTitles.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("Nothing to redo"));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("redone"), StringArrayToJsonArray(RedoneTitles));
	Data->SetNumberField(TEXT("count"), RedoneTitles.Num());
	Data->SetNumberField(TEXT("requested"), Count);

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Redone %d of %d requested transaction(s)"), RedoneTitles.Num(), Count), Data);
	if (RedoneTitles.Num() < Count)
	{
		Result.Warnings.Add(TEXT("Reached the end of the redo buffer before completing all requested redos"));
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("Editor transactions require an editor build"));
#endif
}

FMCPToolResult FMCPTool_EditorTransaction::OpCanUndo(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	const bool bCanUndo = GEditor && GEditor->Trans && GEditor->Trans->CanUndo();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("can_undo"), bCanUndo);
	Data->SetStringField(TEXT("next_undo"),
		bCanUndo ? GEditor->Trans->GetUndoContext(false).Title.ToString() : FString());

	return FMCPToolResult::Success(
		bCanUndo
			? FString::Printf(TEXT("Can undo: %s"), *GEditor->Trans->GetUndoContext(false).Title.ToString())
			: TEXT("Nothing to undo"),
		Data);
#else
	return FMCPToolResult::Error(TEXT("Editor transactions require an editor build"));
#endif
}

FMCPToolResult FMCPTool_EditorTransaction::OpCanRedo(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	const bool bCanRedo = GEditor && GEditor->Trans && GEditor->Trans->CanRedo();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("can_redo"), bCanRedo);
	Data->SetStringField(TEXT("next_redo"),
		bCanRedo ? GEditor->Trans->GetRedoContext().Title.ToString() : FString());

	return FMCPToolResult::Success(
		bCanRedo
			? FString::Printf(TEXT("Can redo: %s"), *GEditor->Trans->GetRedoContext().Title.ToString())
			: TEXT("Nothing to redo"),
		Data);
#else
	return FMCPToolResult::Error(TEXT("Editor transactions require an editor build"));
#endif
}

FMCPToolResult FMCPTool_EditorTransaction::OpGetHistory(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UTransBuffer* TransBuffer = GetTransBuffer();
	if (!TransBuffer)
	{
		return FMCPToolResult::Error(TEXT("No editor transaction buffer available"));
	}

	const int32 MaxEntries = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("max_entries"), 20));

	const int32 QueueLength = TransBuffer->GetQueueLength();
	// In UE, UTransBuffer::GetUndoCount() returns the number of transactions that have
	// been undone (i.e. available for redo). Undoable transactions are the remainder.
	const int32 RedoableCount = TransBuffer->GetUndoCount();
	const int32 UndoableCount = QueueLength - RedoableCount;

	// Undo history: walk from most-recent undoable backwards.
	// Undoable transactions occupy buffer indices [0, UndoableCount).
	TArray<TSharedPtr<FJsonValue>> UndoArr;
	const int32 UndoCount = FMath::Min(MaxEntries, UndoableCount);
	for (int32 i = 0; i < UndoCount; ++i)
	{
		const int32 BufferIndex = UndoableCount - 1 - i;
		const FTransaction* Transaction = TransBuffer->GetTransaction(BufferIndex);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetStringField(TEXT("title"),
			Transaction ? Transaction->GetContext().Title.ToString() : TEXT("[Transaction]"));
		Entry->SetBoolField(TEXT("is_current"), i == 0);
		UndoArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	// Redo history: redoable transactions occupy buffer indices [UndoableCount, QueueLength).
	TArray<TSharedPtr<FJsonValue>> RedoArr;
	const int32 RedoCount = FMath::Min(MaxEntries, RedoableCount);
	for (int32 i = 0; i < RedoCount; ++i)
	{
		const int32 BufferIndex = UndoableCount + i;
		const FTransaction* Transaction = TransBuffer->GetTransaction(BufferIndex);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetStringField(TEXT("title"),
			Transaction ? Transaction->GetContext().Title.ToString() : TEXT("[Transaction]"));
		Entry->SetBoolField(TEXT("is_current"), i == 0);
		RedoArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("undo_history"), UndoArr);
	Data->SetArrayField(TEXT("redo_history"), RedoArr);
	Data->SetNumberField(TEXT("undo_count"), UndoableCount);
	Data->SetNumberField(TEXT("redo_count"), RedoableCount);
	Data->SetNumberField(TEXT("queue_length"), QueueLength);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Transaction history: %d undoable, %d redoable"), UndoableCount, RedoableCount), Data);
#else
	return FMCPToolResult::Error(TEXT("Editor transactions require an editor build"));
#endif
}

FMCPToolResult FMCPTool_EditorTransaction::OpGetCounts(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UTransBuffer* TransBuffer = GetTransBuffer();
	if (!TransBuffer)
	{
		return FMCPToolResult::Error(TEXT("No editor transaction buffer available"));
	}

	const int32 QueueLength = TransBuffer->GetQueueLength();
	const int32 RedoableCount = TransBuffer->GetUndoCount();
	const int32 UndoableCount = QueueLength - RedoableCount;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("undo_count"), UndoableCount);
	Data->SetNumberField(TEXT("redo_count"), RedoableCount);
	Data->SetNumberField(TEXT("queue_length"), QueueLength);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("%d undoable, %d redoable transaction(s)"), UndoableCount, RedoableCount), Data);
#else
	return FMCPToolResult::Error(TEXT("Editor transactions require an editor build"));
#endif
}

FMCPToolResult FMCPTool_EditorTransaction::OpReset(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UTransBuffer* TransBuffer = GetTransBuffer();
	if (!TransBuffer)
	{
		return FMCPToolResult::Error(TEXT("No editor transaction buffer available"));
	}

	FString Reason = ExtractOptionalString(Params, TEXT("reason"), TEXT("Manual reset"));
	if (Reason.IsEmpty())
	{
		Reason = TEXT("Manual reset");
	}

	TransBuffer->Reset(FText::FromString(Reason));
	UE_LOG(LogUnrealClaude, Warning, TEXT("Reset editor transaction buffer. Reason: %s"), *Reason);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("reason"), Reason);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Reset undo/redo buffer (%s)"), *Reason), Data);
#else
	return FMCPToolResult::Error(TEXT("Editor transactions require an editor build"));
#endif
}
