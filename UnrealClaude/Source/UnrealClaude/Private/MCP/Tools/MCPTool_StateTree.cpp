// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_StateTree.h"

// ---------------------------------------------------------------------------
// STUB: every OpXxx() body returns "not implemented yet". Includes are kept
// minimal so this compiles before the StateTree authoring dependencies are
// wired up.
//
// For the full implementation (porting VibeUE's UStateTreeService), add to
// UnrealClaude.Build.cs PrivateDependencyModuleNames:
//
//   "StateTreeModule"          - runtime: UStateTree, FStateTreeReference, schemas
//   "StateTreeEditorModule"    - editor: UStateTreeEditorData, UStateTreeState,
//                                 FStateTreeEditorNode, compile (editor-only, guard WITH_EDITOR)
//   "GameplayStateTreeModule"  - StateTreeComponentSchema / AIComponentSchema +
//                                 the stock tasks/conditions (engine PLUGIN — see below)
//
// Engine PLUGINS that must be enabled in the .uplugin (already enabled in this
// project per the StateTree authoring requirement):
//   - "StateTree"          (plugin) -> StateTreeModule, StateTreeEditorModule
//   - "GameplayStateTree"  (plugin) -> GameplayStateTreeModule
//
// Key includes the full implementation will need (editor-guarded):
//   #include "StateTree.h"
//   #include "StateTreeEditorData.h"
//   #include "StateTreeState.h"
//   #include "StateTreeEditorNode.h"
//   #include "StateTreeCompiler.h"            // FStateTreeCompiler / compile path
//   #include "StateTreeCompilerLog.h"
//   #include "StateTreeReference.h"
//   #include "Conditions/StateTreeCondition_Common.h"
//   #include "Considerations/StateTreeConsiderationBase.h"
//   #include "AssetToolsModule.h" / "IAssetTools.h"  // create/save asset
// ---------------------------------------------------------------------------

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
		"- 'set_entry': Make 'state_path' the entry/selection target of its parent (optional 'selection_behavior')\n"
		"- 'add_task': Add task 'task_struct' to 'state_path'\n"
		"- 'add_transition': Add a transition from 'state_path' with 'trigger' and 'transition_type' (optional 'target_path', 'priority', 'event_tag')\n"
		"- 'add_condition': Add condition 'condition_struct' to 'state_path' enter conditions, or to a transition when 'transition_index' is given\n"
		"- 'add_consideration': Add utility-AI consideration 'consideration_struct' to 'state_path'\n"
		"- 'get_states': List the flattened states in 'asset_path'\n"
		"- 'compile': Compile 'asset_path' (required after structural changes)\n"
		"- 'validate': Validate the structure of 'asset_path' without persisting a compile\n\n"
		"Requires an editor build with the StateTree and GameplayStateTree engine plugins enabled.\n\n"
		"NOTE: STUB — operations are scaffolded but not yet implemented."
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
	return FMCPToolResult::Error(TEXT("create: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_info: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpAddState(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_state: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpRemoveState(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove_state: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpSetEntry(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_entry: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpAddTask(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_task: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpAddTransition(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_transition: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpAddCondition(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_condition: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpAddConsideration(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_consideration: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpGetStates(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_states: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpCompile(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("compile: not implemented yet"));
}

FMCPToolResult FMCPTool_StateTree::OpValidate(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("validate: not implemented yet"));
}
