// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_StateTree.h"

// ---------------------------------------------------------------------------
// StateTree authoring implementation. Ported from VibeUE's UStateTreeService
// into the native MCP tool pattern. All engine APIs verified against UE5.7
// (Engine/Plugins/Runtime/StateTree). Editor-only: every mutation is guarded
// with #if WITH_EDITOR and the editor-data accessors with WITH_EDITORONLY_DATA.
//
// Modules used (already in UnrealClaude.Build.cs): StateTreeModule,
// StateTreeEditorModule, GameplayStateTreeModule, AssetRegistry, AssetTools,
// EditorScriptingUtilities (UEditorAssetLibrary), GameplayTags.
// ---------------------------------------------------------------------------

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "Misc/PackageName.h"
#include "GameplayTagContainer.h"
#include "Logging/TokenizedMessage.h"

// StateTree core (runtime)
#include "StateTree.h"
#include "StateTreeTypes.h"
#include "StateTreeNodeBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"

#if WITH_EDITOR
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#include "StateTreeSchema.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#endif

// ---------------------------------------------------------------------------
// Internal helpers (anon namespace — mirrors MCPTool_EngineSettings.cpp style).
// ---------------------------------------------------------------------------
namespace
{
	/** Load a StateTree by content path; tries the in-memory object first, then disk. */
	UStateTree* LoadStateTree(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}

		// Newly created (unsaved) assets live in memory and are invisible to LoadAsset.
		const FString ShortName = FPackageName::GetShortName(AssetPath);
		const FString FullObjectPath = AssetPath + TEXT(".") + ShortName;
		if (UStateTree* Found = FindObject<UStateTree>(nullptr, *FullObjectPath, EFindObjectFlags::None))
		{
			return Found;
		}

		UObject* Obj = UEditorAssetLibrary::LoadAsset(AssetPath);
		return Cast<UStateTree>(Obj);
	}

#if WITH_EDITOR
	UStateTreeEditorData* GetEditorData(UStateTree* StateTree)
	{
		if (!StateTree)
		{
			return nullptr;
		}
#if WITH_EDITORONLY_DATA
		return Cast<UStateTreeEditorData>(StateTree->EditorData);
#else
		return nullptr;
#endif
	}

	/** Split "Root/Walking/Idle" into ["Root","Walking","Idle"]. */
	TArray<FString> SplitPath(const FString& Path)
	{
		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("/"), true);
		return Segments;
	}

	/** Build a slash path for a state by walking up to the subtree root. */
	FString BuildStatePath(const UStateTreeState* State)
	{
		if (!State)
		{
			return FString();
		}
		TArray<FString> Parts;
		const UStateTreeState* Current = State;
		while (Current)
		{
			Parts.Insert(Current->Name.ToString(), 0);
			Current = Current->Parent;
		}
		return FString::Join(Parts, TEXT("/"));
	}

	/** Find a state by slash path starting from the editor SubTrees. */
	UStateTreeState* FindStateByPath(UStateTreeEditorData* EditorData, const FString& StatePath)
	{
		if (!EditorData || StatePath.IsEmpty())
		{
			return nullptr;
		}

		TArray<FString> Segments = SplitPath(StatePath);
		if (Segments.IsEmpty())
		{
			return nullptr;
		}

		UStateTreeState* Current = nullptr;
		for (UStateTreeState* SubTree : EditorData->SubTrees)
		{
			if (SubTree && SubTree->Name.ToString() == Segments[0])
			{
				Current = SubTree;
				break;
			}
		}
		if (!Current)
		{
			return nullptr;
		}

		for (int32 i = 1; i < Segments.Num(); ++i)
		{
			UStateTreeState* Found = nullptr;
			for (UStateTreeState* Child : Current->Children)
			{
				if (Child && Child->Name.ToString() == Segments[i])
				{
					Found = Child;
					break;
				}
			}
			if (!Found)
			{
				return nullptr;
			}
			Current = Found;
		}
		return Current;
	}

	/** Find a UScriptStruct by name, tolerating presence/absence of the leading 'F'. */
	UScriptStruct* FindNodeStruct(const FString& StructName)
	{
		if (StructName.IsEmpty())
		{
			return nullptr;
		}

		if (UScriptStruct* Found = FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::None))
		{
			return Found;
		}
		if (!StructName.StartsWith(TEXT("F")))
		{
			if (UScriptStruct* Found = FindFirstObject<UScriptStruct>(*(TEXT("F") + StructName), EFindFirstObjectOptions::None))
			{
				return Found;
			}
		}
		if (StructName.StartsWith(TEXT("F")) && StructName.Len() > 1)
		{
			if (UScriptStruct* Found = FindFirstObject<UScriptStruct>(*StructName.RightChop(1), EFindFirstObjectOptions::None))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** Initialize an editor node from a node struct (sets up Node + Instance + runtime data). */
	bool InitEditorNodeFromStruct(FStateTreeEditorNode& OutNode, UScriptStruct* NodeStruct, UObject* Outer)
	{
		if (!NodeStruct)
		{
			return false;
		}
		if (Outer == nullptr)
		{
			Outer = GetTransientPackage();
		}

		OutNode.Reset();
		OutNode.ID = FGuid::NewGuid();
		OutNode.Node.InitializeAs(NodeStruct);

		if (const FStateTreeNodeBase* NodeBase = OutNode.Node.GetPtr<FStateTreeNodeBase>())
		{
			if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase->GetInstanceDataType()))
			{
				OutNode.Instance.InitializeAs(InstanceType);
			}
			else if (const UClass* InstanceClass = Cast<const UClass>(NodeBase->GetInstanceDataType()))
			{
				OutNode.InstanceObject = NewObject<UObject>(Outer, const_cast<UClass*>(InstanceClass));
			}

			if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
			{
				OutNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
			}
			else if (const UClass* RuntimeClass = Cast<const UClass>(NodeBase->GetExecutionRuntimeDataType()))
			{
				OutNode.ExecutionRuntimeDataObject = NewObject<UObject>(Outer, const_cast<UClass*>(RuntimeClass));
			}
		}
		return true;
	}

	/**
	 * Snapshot the StateTree + editor data into the transaction BEFORE a mutation,
	 * so undo can capture the pre-mutation state. Must be called before touching
	 * SubTrees/Children/Tasks/Transitions/etc.
	 */
	void BeginStateTreeEdit(UStateTree* StateTree)
	{
		if (!StateTree)
		{
			return;
		}

		StateTree->Modify();

#if WITH_EDITORONLY_DATA
		if (UStateTreeEditorData* EditorData = GetEditorData(StateTree))
		{
			EditorData->Modify();
		}
#endif
	}

	/** Mark the package dirty and fire PostEditChange AFTER a mutation is applied. */
	void FinalizeStateTreeEdit(UStateTree* StateTree)
	{
		if (!StateTree)
		{
			return;
		}

		StateTree->MarkPackageDirty();

#if WITH_EDITORONLY_DATA
		if (UStateTreeEditorData* EditorData = GetEditorData(StateTree))
		{
			EditorData->PostEditChange();
		}
#endif
		StateTree->PostEditChange();
	}

	/**
	 * Run the StateTree compiler and split its log into JSON error/warning arrays.
	 * Shared by compile (persists on success) and validate (never persists).
	 */
	bool CompileAndCollectLog(UStateTree& StateTree, TArray<TSharedPtr<FJsonValue>>& OutErrors, TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		const bool bSuccess = Compiler.Compile(StateTree);

		for (const TSharedRef<FTokenizedMessage>& Msg : Log.ToTokenizedMessages())
		{
			const FString MsgText = Msg->ToText().ToString();
			const EMessageSeverity::Type Sev = Msg->GetSeverity();
			if (Sev == EMessageSeverity::Error)
			{
				OutErrors.Add(MakeShared<FJsonValueString>(MsgText));
			}
			else if (Sev == EMessageSeverity::Warning || Sev == EMessageSeverity::PerformanceWarning)
			{
				OutWarnings.Add(MakeShared<FJsonValueString>(MsgText));
			}
		}
		return bSuccess;
	}

	// --- String <-> enum converters ---------------------------------------

	EStateTreeStateType StringToStateType(const FString& TypeStr)
	{
		if (TypeStr == TEXT("Group"))       return EStateTreeStateType::Group;
		if (TypeStr == TEXT("Linked"))      return EStateTreeStateType::Linked;
		if (TypeStr == TEXT("LinkedAsset")) return EStateTreeStateType::LinkedAsset;
		if (TypeStr == TEXT("Subtree"))     return EStateTreeStateType::Subtree;
		return EStateTreeStateType::State;
	}

	FString StateTypeToString(EStateTreeStateType Type)
	{
		switch (Type)
		{
		case EStateTreeStateType::Group:       return TEXT("Group");
		case EStateTreeStateType::Linked:      return TEXT("Linked");
		case EStateTreeStateType::LinkedAsset: return TEXT("LinkedAsset");
		case EStateTreeStateType::Subtree:     return TEXT("Subtree");
		default:                               return TEXT("State");
		}
	}

	EStateTreeStateSelectionBehavior StringToSelectionBehavior(const FString& Str)
	{
		if (Str == TEXT("None"))                                        return EStateTreeStateSelectionBehavior::None;
		if (Str == TEXT("TryEnterState"))                               return EStateTreeStateSelectionBehavior::TryEnterState;
		if (Str == TEXT("TrySelectChildrenInOrder"))                    return EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
		if (Str == TEXT("TrySelectChildrenAtRandom"))                   return EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
		if (Str == TEXT("TrySelectChildrenWithHighestUtility"))         return EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
		if (Str == TEXT("TrySelectChildrenAtRandomWeightedByUtility"))  return EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
		if (Str == TEXT("TryFollowTransitions"))                        return EStateTreeStateSelectionBehavior::TryFollowTransitions;
		return EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
	}

	FString SelectionBehaviorToString(EStateTreeStateSelectionBehavior Behavior)
	{
		switch (Behavior)
		{
		case EStateTreeStateSelectionBehavior::None:                                       return TEXT("None");
		case EStateTreeStateSelectionBehavior::TryEnterState:                              return TEXT("TryEnterState");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder:                   return TEXT("TrySelectChildrenInOrder");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom:                  return TEXT("TrySelectChildrenAtRandom");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility:        return TEXT("TrySelectChildrenWithHighestUtility");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility: return TEXT("TrySelectChildrenAtRandomWeightedByUtility");
		case EStateTreeStateSelectionBehavior::TryFollowTransitions:                       return TEXT("TryFollowTransitions");
		default:                                                                           return TEXT("TrySelectChildrenInOrder");
		}
	}

	EStateTreeTransitionTrigger StringToTransitionTrigger(const FString& TriggerStr)
	{
		if (TriggerStr == TEXT("OnStateSucceeded")) return EStateTreeTransitionTrigger::OnStateSucceeded;
		if (TriggerStr == TEXT("OnStateFailed"))    return EStateTreeTransitionTrigger::OnStateFailed;
		if (TriggerStr == TEXT("OnTick"))           return EStateTreeTransitionTrigger::OnTick;
		if (TriggerStr == TEXT("OnEvent"))          return EStateTreeTransitionTrigger::OnEvent;
		return EStateTreeTransitionTrigger::OnStateCompleted;
	}

	FString TransitionTriggerToString(EStateTreeTransitionTrigger Trigger)
	{
		switch (Trigger)
		{
		case EStateTreeTransitionTrigger::OnStateSucceeded: return TEXT("OnStateSucceeded");
		case EStateTreeTransitionTrigger::OnStateFailed:    return TEXT("OnStateFailed");
		case EStateTreeTransitionTrigger::OnTick:           return TEXT("OnTick");
		case EStateTreeTransitionTrigger::OnEvent:          return TEXT("OnEvent");
		default:                                            return TEXT("OnStateCompleted");
		}
	}

	EStateTreeTransitionType StringToTransitionType(const FString& TypeStr)
	{
		if (TypeStr == TEXT("Succeeded"))           return EStateTreeTransitionType::Succeeded;
		if (TypeStr == TEXT("Failed"))              return EStateTreeTransitionType::Failed;
		if (TypeStr == TEXT("NextState"))           return EStateTreeTransitionType::NextState;
		if (TypeStr == TEXT("NextSelectableState")) return EStateTreeTransitionType::NextSelectableState;
		if (TypeStr == TEXT("None"))                return EStateTreeTransitionType::None;
		return EStateTreeTransitionType::GotoState;
	}

	FString TransitionTypeToString(EStateTreeTransitionType Type)
	{
		switch (Type)
		{
		case EStateTreeTransitionType::None:                return TEXT("None");
		case EStateTreeTransitionType::Succeeded:           return TEXT("Succeeded");
		case EStateTreeTransitionType::Failed:              return TEXT("Failed");
		case EStateTreeTransitionType::NextState:           return TEXT("NextState");
		case EStateTreeTransitionType::NextSelectableState: return TEXT("NextSelectableState");
		default:                                            return TEXT("GotoState");
		}
	}

	EStateTreeTransitionPriority StringToPriority(const FString& PriorityStr)
	{
		if (PriorityStr == TEXT("Low"))      return EStateTreeTransitionPriority::Low;
		if (PriorityStr == TEXT("Medium"))   return EStateTreeTransitionPriority::Medium;
		if (PriorityStr == TEXT("High"))     return EStateTreeTransitionPriority::High;
		if (PriorityStr == TEXT("Critical")) return EStateTreeTransitionPriority::Critical;
		return EStateTreeTransitionPriority::Normal;
	}

	FString PriorityToString(EStateTreeTransitionPriority Priority)
	{
		switch (Priority)
		{
		case EStateTreeTransitionPriority::Low:      return TEXT("Low");
		case EStateTreeTransitionPriority::Medium:   return TEXT("Medium");
		case EStateTreeTransitionPriority::High:     return TEXT("High");
		case EStateTreeTransitionPriority::Critical: return TEXT("Critical");
		default:                                     return TEXT("Normal");
		}
	}

	/** Expand consideration shorthand aliases to full struct names. */
	FString ResolveConsiderationAlias(const FString& InName)
	{
		if (InName.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))   return TEXT("FStateTreeConstantConsideration");
		if (InName.Equals(TEXT("FloatInput"), ESearchCase::IgnoreCase)) return TEXT("FStateTreeFloatInputConsideration");
		if (InName.Equals(TEXT("EnumInput"), ESearchCase::IgnoreCase))  return TEXT("FStateTreeEnumInputConsideration");
		return InName;
	}

	/** Build a JSON object describing a single editor node (name + struct type). */
	TSharedPtr<FJsonObject> NodeToJson(const FStateTreeEditorNode& Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Node.GetName().ToString());
		if (Node.Node.IsValid() && Node.Node.GetScriptStruct())
		{
			Obj->SetStringField(TEXT("struct"), Node.Node.GetScriptStruct()->GetName());
		}
		return Obj;
	}

	/** Recursively serialize a state and its descendants into the OutStates array. */
	void CollectStateJson(const UStateTreeState* State, TArray<TSharedPtr<FJsonValue>>& OutStates)
	{
		if (!State)
		{
			return;
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), State->Name.ToString());
		Obj->SetStringField(TEXT("path"), BuildStatePath(State));
		Obj->SetStringField(TEXT("state_type"), StateTypeToString(State->Type));
		Obj->SetStringField(TEXT("selection_behavior"), SelectionBehaviorToString(State->SelectionBehavior));
		Obj->SetBoolField(TEXT("enabled"), State->bEnabled);
		if (!State->Description.IsEmpty())
		{
			Obj->SetStringField(TEXT("description"), State->Description);
		}

		// Tasks
		{
			TArray<TSharedPtr<FJsonValue>> Tasks;
			for (const FStateTreeEditorNode& Task : State->Tasks)
			{
				Tasks.Add(MakeShared<FJsonValueObject>(NodeToJson(Task)));
			}
			Obj->SetArrayField(TEXT("tasks"), Tasks);
		}

		// Enter conditions
		{
			TArray<TSharedPtr<FJsonValue>> Conds;
			for (const FStateTreeEditorNode& Cond : State->EnterConditions)
			{
				Conds.Add(MakeShared<FJsonValueObject>(NodeToJson(Cond)));
			}
			Obj->SetArrayField(TEXT("enter_conditions"), Conds);
		}

		// Considerations
		{
			TArray<TSharedPtr<FJsonValue>> Cons;
			for (const FStateTreeEditorNode& C : State->Considerations)
			{
				Cons.Add(MakeShared<FJsonValueObject>(NodeToJson(C)));
			}
			Obj->SetArrayField(TEXT("considerations"), Cons);
		}

		// Transitions
		{
			TArray<TSharedPtr<FJsonValue>> Transitions;
			for (int32 i = 0; i < State->Transitions.Num(); ++i)
			{
				const FStateTreeTransition& T = State->Transitions[i];
				TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
				TObj->SetNumberField(TEXT("index"), i);
				TObj->SetStringField(TEXT("trigger"), TransitionTriggerToString(T.Trigger));
				TObj->SetStringField(TEXT("priority"), PriorityToString(T.Priority));
				TObj->SetBoolField(TEXT("enabled"), T.bTransitionEnabled);
				TObj->SetStringField(TEXT("transition_type"), TransitionTypeToString(T.State.LinkType));
				if (!T.State.Name.IsNone())
				{
					TObj->SetStringField(TEXT("target"), T.State.Name.ToString());
				}
				if (T.RequiredEvent.Tag.IsValid())
				{
					TObj->SetStringField(TEXT("event_tag"), T.RequiredEvent.Tag.ToString());
				}
				TObj->SetNumberField(TEXT("condition_count"), T.Conditions.Num());
				Transitions.Add(MakeShared<FJsonValueObject>(TObj));
			}
			Obj->SetArrayField(TEXT("transitions"), Transitions);
		}

		// Child paths
		{
			TArray<TSharedPtr<FJsonValue>> ChildPaths;
			for (const UStateTreeState* Child : State->Children)
			{
				if (Child)
				{
					ChildPaths.Add(MakeShared<FJsonValueString>(BuildStatePath(Child)));
				}
			}
			Obj->SetArrayField(TEXT("children"), ChildPaths);
		}

		OutStates.Add(MakeShared<FJsonValueObject>(Obj));

		for (const UStateTreeState* Child : State->Children)
		{
			CollectStateJson(Child, OutStates);
		}
	}
#endif // WITH_EDITOR
}

FMCPToolInfo FMCPTool_StateTree::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("state_tree");
	Info.Description = TEXT(
		"Author Unreal Engine StateTree assets (states, tasks, transitions, conditions, "
		"and utility-AI considerations).\n\n"
		"State paths use '/' separators starting from the subtree root name "
		"(e.g. 'Root', 'Root/Walking', 'Root/Walking/Idle').\n\n"
		"Operations (set 'operation'):\n"
		"- 'create': Create a StateTree at 'asset_path' (optional 'schema_class', default 'StateTreeComponentSchema')\n"
		"- 'get_info': Full structural info for 'asset_path' (schema, parameters, states, tasks, transitions, compile status)\n"
		"- 'add_state': Add a state under 'parent_path' named 'state_name' (optional 'state_type': State/Group/Subtree/Linked/LinkedAsset)\n"
		"- 'remove_state': Remove 'state_path' and all its children\n"
		"- 'set_entry': Set how 'state_path' selects among its children (SelectionBehavior; optional 'selection_behavior')\n"
		"- 'add_task': Add task 'task_struct' to 'state_path'\n"
		"- 'add_transition': Add a transition from 'state_path' with 'trigger' and 'transition_type' (optional 'target_path', 'priority', 'event_tag')\n"
		"- 'add_condition': Add condition 'condition_struct' to 'state_path' enter conditions, or to a transition when 'transition_index' is given\n"
		"- 'add_consideration': Add utility-AI consideration 'consideration_struct' to 'state_path'\n"
		"- 'get_states': List the flattened states in 'asset_path'\n"
		"- 'compile': Compile 'asset_path' (required after structural changes)\n"
		"- 'validate': Validate the structure of 'asset_path' without persisting a compile\n\n"
		"Requires an editor build with the StateTree and GameplayStateTree engine plugins enabled. "
		"Structural changes are not visible at runtime until you 'compile'."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: create, get_info, add_state, remove_state, set_entry, add_task, add_transition, add_condition, add_consideration, get_states, compile, validate"), true),
		FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Content path to the StateTree asset, including name (e.g. '/Game/AI/MyStateTree')"), false),
		FMCPToolParameter(TEXT("schema_class"), TEXT("string"), TEXT("For 'create': schema class name (default 'StateTreeComponentSchema'; accepts shorthand 'Component', 'AIComponent')"), false),
		FMCPToolParameter(TEXT("parent_path"), TEXT("string"), TEXT("For 'add_state': path of the parent state (e.g. 'Root'), or empty for a new top-level subtree"), false),
		FMCPToolParameter(TEXT("state_name"), TEXT("string"), TEXT("For 'add_state': name of the new state"), false),
		FMCPToolParameter(TEXT("state_type"), TEXT("string"), TEXT("For 'add_state': 'State' (default), 'Group', 'Subtree', 'Linked', 'LinkedAsset'"), false),
		FMCPToolParameter(TEXT("state_path"), TEXT("string"), TEXT("Target state path for state-scoped ops (e.g. 'Root/Walking')"), false),
		FMCPToolParameter(TEXT("selection_behavior"), TEXT("string"), TEXT("For 'set_entry': how children are selected (e.g. 'TrySelectChildrenInOrder')"), false),
		FMCPToolParameter(TEXT("task_struct"), TEXT("string"), TEXT("For 'add_task': task type (struct name like 'FStateTreeDelayTask', or Blueprint task name/path like 'STT_Rotate_C')"), false),
		FMCPToolParameter(TEXT("trigger"), TEXT("string"), TEXT("For 'add_transition': 'OnStateCompleted', 'OnStateSucceeded', 'OnStateFailed', 'OnTick', 'OnEvent'"), false),
		FMCPToolParameter(TEXT("transition_type"), TEXT("string"), TEXT("For 'add_transition': 'GotoState', 'Succeeded', 'Failed', 'NextState', 'NextSelectableState'"), false),
		FMCPToolParameter(TEXT("target_path"), TEXT("string"), TEXT("For 'add_transition': target state path (only when transition_type is 'GotoState')"), false),
		FMCPToolParameter(TEXT("priority"), TEXT("string"), TEXT("For 'add_transition': 'Low', 'Normal' (default), 'Medium', 'High', 'Critical'"), false),
		FMCPToolParameter(TEXT("event_tag"), TEXT("string"), TEXT("For 'add_transition': gameplay tag for the 'OnEvent' trigger (e.g. 'AI.StartPatrol')"), false),
		FMCPToolParameter(TEXT("condition_struct"), TEXT("string"), TEXT("For 'add_condition': condition struct name (e.g. 'FStateTreeCompareIntCondition')"), false),
		FMCPToolParameter(TEXT("transition_index"), TEXT("number"), TEXT("For 'add_condition': zero-based transition index to attach the condition to (omit to add an enter condition)"), false),
		FMCPToolParameter(TEXT("consideration_struct"), TEXT("string"), TEXT("For 'add_consideration': 'Constant', 'FloatInput', 'EnumInput', or full struct name (e.g. 'FStateTreeConstantConsideration')"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_StateTree::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("create"))            { return OpCreate(Params); }
	if (Operation == TEXT("get_info"))          { return OpGetInfo(Params); }
	if (Operation == TEXT("add_state"))         { return OpAddState(Params); }
	if (Operation == TEXT("remove_state"))      { return OpRemoveState(Params); }
	if (Operation == TEXT("set_entry"))         { return OpSetEntry(Params); }
	if (Operation == TEXT("add_task"))          { return OpAddTask(Params); }
	if (Operation == TEXT("add_transition"))    { return OpAddTransition(Params); }
	if (Operation == TEXT("add_condition"))     { return OpAddCondition(Params); }
	if (Operation == TEXT("add_consideration")) { return OpAddConsideration(Params); }
	if (Operation == TEXT("get_states"))        { return OpGetStates(Params); }
	if (Operation == TEXT("compile"))           { return OpCompile(Params); }
	if (Operation == TEXT("validate"))          { return OpValidate(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create, get_info, add_state, remove_state, set_entry, "
			"add_task, add_transition, add_condition, add_consideration, get_states, compile, validate"),
		*Operation));
}

FMCPToolResult FMCPTool_StateTree::OpCreate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	// Reject if the asset already exists.
	if (LoadStateTree(AssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("create: StateTree already exists at '%s'"), *AssetPath));
	}

	FString PackagePath, AssetName;
	if (!AssetPath.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd) || AssetName.IsEmpty())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("create: invalid asset_path '%s' (expected e.g. '/Game/AI/MyStateTree')"), *AssetPath));
	}
	PackagePath = AssetPath.Left(AssetPath.Len() - AssetName.Len() - 1);

	// Resolve schema class, honoring shorthand (e.g. "Component" -> "StateTreeComponentSchema").
	FString SchemaName = ExtractOptionalString(Params, TEXT("schema_class"));
	if (SchemaName.IsEmpty())
	{
		SchemaName = TEXT("StateTreeComponentSchema");
	}
	else if (!SchemaName.Contains(TEXT("Schema")))
	{
		SchemaName = TEXT("StateTree") + SchemaName + TEXT("Schema");
	}

	UClass* SchemaClass = nullptr;
	TArray<FString> AvailableSchemas;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UStateTreeSchema::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			AvailableSchemas.Add(It->GetName());
			if (It->GetName() == SchemaName)
			{
				SchemaClass = *It;
			}
		}
	}

	if (!SchemaClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("create: schema class '%s' not found. Available: %s"),
			*SchemaName, *FString::Join(AvailableSchemas, TEXT(", "))));
	}

	const FString PackageName = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("create: failed to create package '%s'"), *PackageName));
	}

	UStateTree* NewStateTree = NewObject<UStateTree>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewStateTree)
	{
		return FMCPToolResult::Error(TEXT("create: failed to create UStateTree object"));
	}

#if WITH_EDITORONLY_DATA
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(NewStateTree, NAME_None, RF_Transactional);
	EditorData->Schema = NewObject<UStateTreeSchema>(EditorData, SchemaClass);
	NewStateTree->EditorData = EditorData;
#endif

	FAssetRegistryModule::AssetCreated(NewStateTree);
	Package->SetDirtyFlag(true);
	NewStateTree->Modify();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("schema_class"), SchemaClass->GetName());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created StateTree '%s' (schema: %s). Add states, then 'compile'."), *AssetPath, *SchemaClass->GetName()),
		Data);
#else
	return FMCPToolResult::Error(TEXT("create: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("get_info: StateTree not found: '%s'"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("asset_name"), StateTree->GetName());
	Data->SetBoolField(TEXT("is_compiled"), StateTree->IsReadyToRun());
	Data->SetStringField(TEXT("compile_status"), StateTree->IsReadyToRun() ? TEXT("Compiled") : TEXT("Not compiled or compile failed"));

	if (const UStateTreeSchema* Schema = StateTree->GetSchema())
	{
		Data->SetStringField(TEXT("schema_class"), Schema->GetClass()->GetName());
	}

#if WITH_EDITOR
	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (EditorData)
	{
		if (!Data->HasField(TEXT("schema_class")) && EditorData->Schema)
		{
			Data->SetStringField(TEXT("schema_class"), EditorData->Schema->GetClass()->GetName());
		}

		TArray<TSharedPtr<FJsonValue>> States;
		for (const UStateTreeState* SubTree : EditorData->SubTrees)
		{
			CollectStateJson(SubTree, States);
		}
		Data->SetArrayField(TEXT("states"), States);
		Data->SetNumberField(TEXT("state_count"), States.Num());

		TArray<TSharedPtr<FJsonValue>> Evaluators;
		for (const FStateTreeEditorNode& Eval : EditorData->Evaluators)
		{
			Evaluators.Add(MakeShared<FJsonValueObject>(NodeToJson(Eval)));
		}
		Data->SetArrayField(TEXT("evaluators"), Evaluators);

		TArray<TSharedPtr<FJsonValue>> GlobalTasks;
		for (const FStateTreeEditorNode& GT : EditorData->GlobalTasks)
		{
			GlobalTasks.Add(MakeShared<FJsonValueObject>(NodeToJson(GT)));
		}
		Data->SetArrayField(TEXT("global_tasks"), GlobalTasks);
	}
#endif

	return FMCPToolResult::Success(FString::Printf(TEXT("StateTree '%s' info"), *AssetPath), Data);
}

FMCPToolResult FMCPTool_StateTree::OpAddState(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, StateName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("state_name"), StateName, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_state: StateTree not found: '%s'"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("add_state: StateTree has no editor data"));
	}

	const FString ParentPath = ExtractOptionalString(Params, TEXT("parent_path"));
	const FString StateType = ExtractOptionalString(Params, TEXT("state_type"));
	const EStateTreeStateType ParsedType = StringToStateType(StateType);
	const FName NewStateName(*StateName);

	FString ResultPath;
	if (ParentPath.IsEmpty())
	{
		BeginStateTreeEdit(StateTree);
		UStateTreeState& SubTree = EditorData->AddSubTree(NewStateName);
		ResultPath = BuildStatePath(&SubTree);
	}
	else
	{
		UStateTreeState* ParentState = FindStateByPath(EditorData, ParentPath);
		if (!ParentState)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("add_state: parent state not found: '%s'"), *ParentPath));
		}
		BeginStateTreeEdit(StateTree);
		UStateTreeState& Child = ParentState->AddChildState(NewStateName, ParsedType);
		ResultPath = BuildStatePath(&Child);
	}

	FinalizeStateTreeEdit(StateTree);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_path"), ResultPath);
	Data->SetStringField(TEXT("state_type"), StateTypeToString(ParsedType));
	return FMCPToolResult::Success(FString::Printf(TEXT("Added state '%s'"), *ResultPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_state: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpRemoveState(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, StatePath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("state_path"), StatePath, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("remove_state: StateTree not found: '%s'"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("remove_state: StateTree has no editor data"));
	}

	UStateTreeState* State = FindStateByPath(EditorData, StatePath);
	if (!State)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("remove_state: state not found: '%s'"), *StatePath));
	}

	BeginStateTreeEdit(StateTree);

	bool bRemoved = false;
	if (UStateTreeState* Parent = State->Parent)
	{
		Parent->Modify();
		bRemoved = Parent->Children.Remove(State) > 0;
	}
	else
	{
		bRemoved = EditorData->SubTrees.Remove(State) > 0;
	}

	if (!bRemoved)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("remove_state: failed to remove '%s'"), *StatePath));
	}

	FinalizeStateTreeEdit(StateTree);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_path"), StatePath);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed state '%s' and its children"), *StatePath), Data);
#else
	return FMCPToolResult::Error(TEXT("remove_state: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpSetEntry(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, StatePath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("state_path"), StatePath, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("set_entry: StateTree not found: '%s'"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("set_entry: StateTree has no editor data"));
	}

	UStateTreeState* State = FindStateByPath(EditorData, StatePath);
	if (!State)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("set_entry: state not found: '%s'"), *StatePath));
	}

	const FString Behavior = ExtractOptionalString(Params, TEXT("selection_behavior"), TEXT("TryEnterState"));
	BeginStateTreeEdit(StateTree);
	State->Modify();
	State->SelectionBehavior = StringToSelectionBehavior(Behavior);
	FinalizeStateTreeEdit(StateTree);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_path"), StatePath);
	Data->SetStringField(TEXT("selection_behavior"), SelectionBehaviorToString(State->SelectionBehavior));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set '%s' selection behavior to '%s'"), *StatePath, *SelectionBehaviorToString(State->SelectionBehavior)),
		Data);
#else
	return FMCPToolResult::Error(TEXT("set_entry: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpAddTask(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, StatePath, TaskStructName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("state_path"), StatePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("task_struct"), TaskStructName, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_task: StateTree not found: '%s'"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("add_task: StateTree has no editor data"));
	}

	UStateTreeState* State = FindStateByPath(EditorData, StatePath);
	if (!State)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_task: state not found: '%s'"), *StatePath));
	}

	UScriptStruct* TaskStruct = FindNodeStruct(TaskStructName);
	if (!TaskStruct)
	{
		// Blueprint-task identifiers (e.g. 'STT_Rotate_C') need the BlueprintTaskWrapper
		// resolution path, which is deferred. Surface a clear error instead of guessing.
		return FMCPToolResult::Error(FString::Printf(
			TEXT("add_task: task struct '%s' not found. Blueprint-task wrappers (e.g. 'STT_Foo_C') are deferred - "
				"pass a native task struct name like 'FStateTreeDelayTask'."), *TaskStructName));
	}

	if (!TaskStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_task: struct '%s' is not a FStateTreeTaskBase"), *TaskStruct->GetName()));
	}

	BeginStateTreeEdit(StateTree);
	State->Modify();
	FStateTreeEditorNode& NewNode = State->Tasks.AddDefaulted_GetRef();
	if (!InitEditorNodeFromStruct(NewNode, TaskStruct, EditorData))
	{
		State->Tasks.RemoveAt(State->Tasks.Num() - 1);
		return FMCPToolResult::Error(FString::Printf(TEXT("add_task: failed to initialize task node for '%s'"), *TaskStruct->GetName()));
	}

	FinalizeStateTreeEdit(StateTree);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_path"), StatePath);
	Data->SetStringField(TEXT("task_struct"), TaskStruct->GetName());
	Data->SetNumberField(TEXT("task_index"), State->Tasks.Num() - 1);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added task '%s' to state '%s'"), *TaskStruct->GetName(), *StatePath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_task: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpAddTransition(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, StatePath, Trigger, TransitionType;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("state_path"), StatePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("trigger"), Trigger, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("transition_type"), TransitionType, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_transition: StateTree not found: '%s'"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("add_transition: StateTree has no editor data"));
	}

	UStateTreeState* State = FindStateByPath(EditorData, StatePath);
	if (!State)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_transition: source state not found: '%s'"), *StatePath));
	}

	const EStateTreeTransitionTrigger ParsedTrigger = StringToTransitionTrigger(Trigger);
	const EStateTreeTransitionType ParsedType = StringToTransitionType(TransitionType);
	const EStateTreeTransitionPriority ParsedPriority = StringToPriority(ExtractOptionalString(Params, TEXT("priority")));
	const FString TargetPath = ExtractOptionalString(Params, TEXT("target_path"));
	const FString EventTag = ExtractOptionalString(Params, TEXT("event_tag"));

	const UStateTreeState* TargetState = nullptr;
	if (ParsedType == EStateTreeTransitionType::GotoState)
	{
		if (TargetPath.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("add_transition: 'target_path' is required when transition_type is 'GotoState'"));
		}
		TargetState = FindStateByPath(EditorData, TargetPath);
		if (!TargetState)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("add_transition: target state not found: '%s'"), *TargetPath));
		}
	}
	else if (!TargetPath.IsEmpty())
	{
		// A target_path only takes effect via GotoState; FStateTreeTransition would
		// otherwise silently force the link type. Reject rather than echo a lie.
		return FMCPToolResult::Error(FString::Printf(
			TEXT("add_transition: 'target_path' is only valid with transition_type='GotoState' (got '%s')"),
			*TransitionType));
	}

	BeginStateTreeEdit(StateTree);
	State->Modify();
	FStateTreeTransition& NewTransition = State->AddTransition(ParsedTrigger, ParsedType, TargetState);
	NewTransition.Priority = ParsedPriority;
	if (!EventTag.IsEmpty())
	{
		NewTransition.RequiredEvent.Tag = FGameplayTag::RequestGameplayTag(FName(*EventTag), /*ErrorIfNotFound=*/false);
	}

	// Echo the actual resulting link type (AddTransition/GetLinkToState may differ from the request).
	const EStateTreeTransitionType ResultType = NewTransition.State.LinkType;

	FinalizeStateTreeEdit(StateTree);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_path"), StatePath);
	Data->SetStringField(TEXT("trigger"), TransitionTriggerToString(ParsedTrigger));
	Data->SetStringField(TEXT("transition_type"), TransitionTypeToString(ResultType));
	Data->SetStringField(TEXT("priority"), PriorityToString(ParsedPriority));
	Data->SetNumberField(TEXT("transition_index"), State->Transitions.Num() - 1);
	if (TargetState)
	{
		Data->SetStringField(TEXT("target"), TargetPath);
	}
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added %s -> %s transition to state '%s'"), *Trigger, *TransitionTypeToString(ResultType), *StatePath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_transition: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpAddCondition(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, StatePath, ConditionStructName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("state_path"), StatePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("condition_struct"), ConditionStructName, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_condition: StateTree not found: '%s'"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("add_condition: StateTree has no editor data"));
	}

	UStateTreeState* State = FindStateByPath(EditorData, StatePath);
	if (!State)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_condition: state not found: '%s'"), *StatePath));
	}

	UScriptStruct* CondStruct = FindNodeStruct(ConditionStructName);
	if (!CondStruct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_condition: condition struct '%s' not found"), *ConditionStructName));
	}
	if (!CondStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_condition: struct '%s' is not a FStateTreeConditionBase"), *CondStruct->GetName()));
	}

	// Decide target array: a transition's Conditions (when transition_index given) or the state's EnterConditions.
	double TransIdxDouble = 0.0;
	const bool bHasTransIdx = Params->TryGetNumberField(TEXT("transition_index"), TransIdxDouble);

	TArray<FStateTreeEditorNode>* TargetArray = nullptr;
	FString Where;
	if (bHasTransIdx)
	{
		const int32 TransIdx = static_cast<int32>(TransIdxDouble);
		if (!State->Transitions.IsValidIndex(TransIdx))
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("add_condition: transition_index %d out of range (state '%s' has %d transitions)"),
				TransIdx, *StatePath, State->Transitions.Num()));
		}
		TargetArray = &State->Transitions[TransIdx].Conditions;
		Where = FString::Printf(TEXT("transition %d"), TransIdx);
	}
	else
	{
		TargetArray = &State->EnterConditions;
		Where = TEXT("enter conditions");
	}

	BeginStateTreeEdit(StateTree);
	State->Modify();
	FStateTreeEditorNode& NewNode = TargetArray->AddDefaulted_GetRef();
	if (!InitEditorNodeFromStruct(NewNode, CondStruct, EditorData))
	{
		TargetArray->RemoveAt(TargetArray->Num() - 1);
		return FMCPToolResult::Error(FString::Printf(TEXT("add_condition: failed to initialize condition node for '%s'"), *CondStruct->GetName()));
	}

	// First condition copies its result; subsequent default to AND.
	NewNode.ExpressionOperand = (TargetArray->Num() == 1)
		? EStateTreeExpressionOperand::Copy
		: EStateTreeExpressionOperand::And;

	FinalizeStateTreeEdit(StateTree);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_path"), StatePath);
	Data->SetStringField(TEXT("condition_struct"), CondStruct->GetName());
	Data->SetStringField(TEXT("attached_to"), Where);
	Data->SetNumberField(TEXT("condition_index"), TargetArray->Num() - 1);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added condition '%s' to %s of state '%s'"), *CondStruct->GetName(), *Where, *StatePath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_condition: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpAddConsideration(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, StatePath, ConsiderationStructName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("state_path"), StatePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("consideration_struct"), ConsiderationStructName, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_consideration: StateTree not found: '%s'"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("add_consideration: StateTree has no editor data"));
	}

	UStateTreeState* State = FindStateByPath(EditorData, StatePath);
	if (!State)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_consideration: state not found: '%s'"), *StatePath));
	}

	const FString ResolvedName = ResolveConsiderationAlias(ConsiderationStructName);
	UScriptStruct* ConsiderationStruct = FindNodeStruct(ResolvedName);
	if (!ConsiderationStruct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_consideration: struct '%s' not found"), *ResolvedName));
	}
	if (!ConsiderationStruct->IsChildOf(FStateTreeConsiderationBase::StaticStruct()))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_consideration: struct '%s' is not a FStateTreeConsiderationBase"), *ConsiderationStruct->GetName()));
	}

	BeginStateTreeEdit(StateTree);
	State->Modify();
	FStateTreeEditorNode& NewNode = State->Considerations.AddDefaulted_GetRef();
	if (!InitEditorNodeFromStruct(NewNode, ConsiderationStruct, EditorData))
	{
		State->Considerations.RemoveAt(State->Considerations.Num() - 1);
		return FMCPToolResult::Error(FString::Printf(TEXT("add_consideration: failed to initialize node for '%s'"), *ConsiderationStruct->GetName()));
	}

	FinalizeStateTreeEdit(StateTree);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state_path"), StatePath);
	Data->SetStringField(TEXT("consideration_struct"), ConsiderationStruct->GetName());
	Data->SetNumberField(TEXT("consideration_index"), State->Considerations.Num() - 1);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added consideration '%s' to state '%s'"), *ConsiderationStruct->GetName(), *StatePath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_consideration: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpGetStates(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("get_states: StateTree not found: '%s'"), *AssetPath));
	}

#if WITH_EDITOR
	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FMCPToolResult::Error(TEXT("get_states: StateTree has no editor data"));
	}

	TArray<TSharedPtr<FJsonValue>> States;
	for (const UStateTreeState* SubTree : EditorData->SubTrees)
	{
		CollectStateJson(SubTree, States);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetArrayField(TEXT("states"), States);
	Data->SetNumberField(TEXT("state_count"), States.Num());
	return FMCPToolResult::Success(FString::Printf(TEXT("StateTree '%s' has %d states"), *AssetPath, States.Num()), Data);
#else
	return FMCPToolResult::Error(TEXT("get_states: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpCompile(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("compile: StateTree not found: '%s'"), *AssetPath));
	}

	BeginStateTreeEdit(StateTree);

	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	const bool bSuccess = CompileAndCollectLog(*StateTree, Errors, Warnings);

	if (bSuccess)
	{
		FinalizeStateTreeEdit(StateTree);
		// Persist the compiled asset to disk so the compile sticks.
		UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetBoolField(TEXT("success"), bSuccess);
	Data->SetArrayField(TEXT("errors"), Errors);
	Data->SetArrayField(TEXT("warnings"), Warnings);

	if (bSuccess)
	{
		return FMCPToolResult::Success(
			FString::Printf(TEXT("Compiled '%s' (%d warnings)"), *AssetPath, Warnings.Num()), Data);
	}
	return FMCPToolResult::Error(FString::Printf(
		TEXT("compile: failed for '%s' with %d errors. First: %s"),
		*AssetPath, Errors.Num(),
		Errors.Num() > 0 ? *Errors[0]->AsString() : TEXT("(no message)")));
#else
	return FMCPToolResult::Error(TEXT("compile: requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_StateTree::OpValidate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	UStateTree* StateTree = LoadStateTree(AssetPath);
	if (!StateTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("validate: StateTree not found: '%s'"), *AssetPath));
	}

	// Validate by running the compiler and collecting diagnostics WITHOUT saving to disk.
	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	const bool bValid = CompileAndCollectLog(*StateTree, Errors, Warnings);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetBoolField(TEXT("valid"), bValid);
	Data->SetArrayField(TEXT("errors"), Errors);
	Data->SetArrayField(TEXT("warnings"), Warnings);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Validation of '%s': %s (%d errors, %d warnings) — not persisted"),
			*AssetPath, bValid ? TEXT("valid") : TEXT("invalid"), Errors.Num(), Warnings.Num()),
		Data);
#else
	return FMCPToolResult::Error(TEXT("validate: requires an editor build"));
#endif
}
