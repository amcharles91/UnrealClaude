// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_GameplayTags.h"
#include "UnrealClaudeModule.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"
#if WITH_EDITOR
#include "GameplayTagsEditorModule.h"
#endif

FMCPToolInfo FMCPTool_GameplayTags::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("gameplay_tags");
	Info.Description = TEXT(
		"Manage Unreal Engine Gameplay Tags (create, query, remove, rename, hierarchy).\n\n"
		"Operations (set 'operation'):\n"
		"- 'list': List all tags; optional 'filter' prefix (e.g., 'Ability')\n"
		"- 'has': Check whether 'tag' exists\n"
		"- 'get_info': Details for 'tag' (source, comment, is_explicit, child_count, redirected_to)\n"
		"- 'get_children': Direct children of 'parent' tag\n"
		"- 'add': Create a new 'tag' (optional 'comment', 'source'), or batch via 'tags' array\n"
		"- 'remove': Delete 'tag' from the config\n"
		"- 'rename': Rename 'tag' to 'new_tag' (registers a redirect from the old name)\n\n"
		"Tags are hierarchical via dot notation (e.g., 'Ability.Attack.Heavy') and are written to "
		"DefaultGameplayTags.ini by default.\n\n"
		"Returns: operation-specific data (tag list/info, or the set of modified tags)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: list, has, get_info, get_children, add, remove, rename"), true),
		FMCPToolParameter(TEXT("tag"), TEXT("string"), TEXT("Full tag name for has/get_info/add/remove/rename (e.g., 'Ability.Attack')"), false),
		FMCPToolParameter(TEXT("tags"), TEXT("array"), TEXT("For 'add': array of tag names to create in one call"), false),
		FMCPToolParameter(TEXT("parent"), TEXT("string"), TEXT("For 'get_children': parent tag whose direct children to return"), false),
		FMCPToolParameter(TEXT("new_tag"), TEXT("string"), TEXT("For 'rename': the new tag name"), false),
		FMCPToolParameter(TEXT("filter"), TEXT("string"), TEXT("For 'list': optional prefix filter"), false),
		FMCPToolParameter(TEXT("comment"), TEXT("string"), TEXT("For 'add': optional developer comment"), false),
		FMCPToolParameter(TEXT("source"), TEXT("string"), TEXT("For 'add': config source file (default: DefaultGameplayTags.ini)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_GameplayTags::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("list"))         { return OpList(Params); }
	if (Operation == TEXT("has"))          { return OpHas(Params); }
	if (Operation == TEXT("get_info"))     { return OpGetInfo(Params); }
	if (Operation == TEXT("get_children")) { return OpGetChildren(Params); }
	if (Operation == TEXT("add"))          { return OpAdd(Params); }
	if (Operation == TEXT("remove"))       { return OpRemove(Params); }
	if (Operation == TEXT("rename"))       { return OpRename(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: list, has, get_info, get_children, add, remove, rename"), *Operation));
}

TSharedPtr<FJsonObject> FMCPTool_GameplayTags::TagInfoJson(const FString& TagName)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("tag"), TagName);

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	const FGameplayTag Tag = Manager.RequestGameplayTag(FName(*TagName), false);
	if (Tag.IsValid())
	{
		// RequestGameplayTag follows redirects registered by renames.
		if (!Tag.GetTagName().IsEqual(FName(*TagName)))
		{
			Obj->SetStringField(TEXT("redirected_to"), Tag.GetTagName().ToString());
		}

		const TSharedPtr<FGameplayTagNode> Node = Manager.FindTagNode(Tag);
		if (Node.IsValid())
		{
			Obj->SetBoolField(TEXT("is_explicit"), Node->IsExplicitTag());
			Obj->SetNumberField(TEXT("child_count"), Node->GetChildTagNodes().Num());
			Obj->SetStringField(TEXT("source"), Node->GetFirstSourceName().ToString());
			Obj->SetStringField(TEXT("comment"), Node->GetDevComment());
		}
	}
	return Obj;
}

FMCPToolResult FMCPTool_GameplayTags::OpList(const TSharedRef<FJsonObject>& Params)
{
	const FString Filter = ExtractOptionalString(Params, TEXT("filter"));

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	FGameplayTagContainer AllTags;
	Manager.RequestAllGameplayTags(AllTags, false);

	TArray<FString> Names;
	for (const FGameplayTag& Tag : AllTags)
	{
		const FString Name = Tag.GetTagName().ToString();
		if (Filter.IsEmpty() || Name.StartsWith(Filter))
		{
			Names.Add(Name);
		}
	}
	Names.Sort();

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& Name : Names)
	{
		Arr.Add(MakeShared<FJsonValueObject>(TagInfoJson(Name)));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tags"), Arr);
	Data->SetNumberField(TEXT("count"), Names.Num());

	const FString Msg = Filter.IsEmpty()
		? FString::Printf(TEXT("Found %d gameplay tag(s)"), Names.Num())
		: FString::Printf(TEXT("Found %d gameplay tag(s) matching '%s'"), Names.Num(), *Filter);
	return FMCPToolResult::Success(Msg, Data);
}

FMCPToolResult FMCPTool_GameplayTags::OpHas(const TSharedRef<FJsonObject>& Params)
{
	FString Tag;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("tag"), Tag, Err))
	{
		return Err.GetValue();
	}

	const FGameplayTag T = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Tag), false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("tag"), Tag);
	Data->SetBoolField(TEXT("exists"), T.IsValid());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Tag '%s' %s"), *Tag, T.IsValid() ? TEXT("exists") : TEXT("does not exist")), Data);
}

FMCPToolResult FMCPTool_GameplayTags::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	FString Tag;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("tag"), Tag, Err))
	{
		return Err.GetValue();
	}

	const FGameplayTag T = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Tag), false);
	if (!T.IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tag '%s' not found"), *Tag));
	}
	return FMCPToolResult::Success(FString::Printf(TEXT("Info for tag '%s'"), *Tag), TagInfoJson(Tag));
}

FMCPToolResult FMCPTool_GameplayTags::OpGetChildren(const TSharedRef<FJsonObject>& Params)
{
	FString Parent;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("parent"), Parent, Err))
	{
		return Err.GetValue();
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	const FGameplayTag P = Manager.RequestGameplayTag(FName(*Parent), false);
	if (!P.IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Parent tag '%s' not found"), *Parent));
	}

	TArray<FString> ChildNames;
	const TSharedPtr<FGameplayTagNode> Node = Manager.FindTagNode(P);
	if (Node.IsValid())
	{
		for (const TSharedPtr<FGameplayTagNode>& Child : Node->GetChildTagNodes())
		{
			if (Child.IsValid())
			{
				ChildNames.Add(Child->GetCompleteTagName().ToString());
			}
		}
	}
	ChildNames.Sort();

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& Name : ChildNames)
	{
		Arr.Add(MakeShared<FJsonValueObject>(TagInfoJson(Name)));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("children"), Arr);
	Data->SetNumberField(TEXT("count"), ChildNames.Num());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Tag '%s' has %d direct child(ren)"), *Parent, ChildNames.Num()), Data);
}

FMCPToolResult FMCPTool_GameplayTags::OpAdd(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	const FString Comment = ExtractOptionalString(Params, TEXT("comment"));
	FString Source = ExtractOptionalString(Params, TEXT("source"), TEXT("DefaultGameplayTags.ini"));
	if (Source.IsEmpty())
	{
		Source = TEXT("DefaultGameplayTags.ini");
	}

	// Collect tag names from the 'tags' array if present, otherwise the single 'tag'.
	TArray<FString> TagNames;
	const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("tags"), TagsArr) && TagsArr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *TagsArr)
		{
			const FString Name = Value->AsString();
			if (!Name.IsEmpty())
			{
				TagNames.Add(Name);
			}
		}
	}
	else
	{
		FString Tag;
		TOptional<FMCPToolResult> Err;
		if (!ExtractRequiredString(Params, TEXT("tag"), Tag, Err))
		{
			return Err.GetValue();
		}
		TagNames.Add(Tag);
	}

	if (TagNames.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("No tags provided. Supply 'tag' (string) or 'tags' (array)."));
	}

	IGameplayTagsEditorModule& EditorModule = IGameplayTagsEditorModule::Get();
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	TArray<FString> Added, Skipped, Failed;
	for (const FString& Name : TagNames)
	{
		const FGameplayTag Existing = Manager.RequestGameplayTag(FName(*Name), false);
		if (Existing.IsValid())
		{
			Skipped.Add(Name);
			continue;
		}
		if (EditorModule.AddNewGameplayTagToINI(Name, Comment, FName(*Source)))
		{
			Added.Add(Name);
			UE_LOG(LogUnrealClaude, Log, TEXT("Added gameplay tag: %s (source: %s)"), *Name, *Source);
		}
		else
		{
			Failed.Add(Name);
			UE_LOG(LogUnrealClaude, Error, TEXT("Failed to add gameplay tag: %s"), *Name);
		}
	}

	if (Added.Num() == 0 && Failed.Num() > 0)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to add tag(s): %s"), *FString::Join(Failed, TEXT(", "))));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("added"), StringArrayToJsonArray(Added));
	Data->SetArrayField(TEXT("skipped_existing"), StringArrayToJsonArray(Skipped));
	Data->SetArrayField(TEXT("failed"), StringArrayToJsonArray(Failed));
	Data->SetStringField(TEXT("source"), Source);

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Added %d tag(s), skipped %d existing, %d failed"),
			Added.Num(), Skipped.Num(), Failed.Num()), Data);
	if (Failed.Num() > 0)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Failed to add: %s"), *FString::Join(Failed, TEXT(", "))));
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("Gameplay tag creation requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_GameplayTags::OpRemove(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Tag;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("tag"), Tag, Err))
	{
		return Err.GetValue();
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	const FGameplayTag T = Manager.RequestGameplayTag(FName(*Tag), false);
	if (!T.IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tag '%s' does not exist"), *Tag));
	}

	const TSharedPtr<FGameplayTagNode> Node = Manager.FindTagNode(T);
	if (!Node.IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tag node not found for '%s'"), *Tag));
	}

	if (!IGameplayTagsEditorModule::Get().DeleteTagFromINI(Node))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to remove tag '%s' - it may be a native tag or referenced by assets"), *Tag));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Removed gameplay tag: %s"), *Tag);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed"), Tag);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed gameplay tag '%s'"), *Tag), Data);
#else
	return FMCPToolResult::Error(TEXT("Gameplay tag removal requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_GameplayTags::OpRename(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString OldTag, NewTag;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("tag"), OldTag, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("new_tag"), NewTag, Err))
	{
		return Err.GetValue();
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	if (!Manager.RequestGameplayTag(FName(*OldTag), false).IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tag '%s' does not exist"), *OldTag));
	}
	if (Manager.RequestGameplayTag(FName(*NewTag), false).IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Target tag '%s' already exists"), *NewTag));
	}

	if (!IGameplayTagsEditorModule::Get().RenameTagInINI(OldTag, NewTag))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to rename tag '%s' to '%s'"), *OldTag, *NewTag));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Renamed gameplay tag: %s -> %s"), *OldTag, *NewTag);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("old_tag"), OldTag);
	Data->SetStringField(TEXT("new_tag"), NewTag);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Renamed '%s' -> '%s' (a redirect from the old name was registered)"), *OldTag, *NewTag), Data);
#else
	return FMCPToolResult::Error(TEXT("Gameplay tag rename requires an editor build"));
#endif
}
