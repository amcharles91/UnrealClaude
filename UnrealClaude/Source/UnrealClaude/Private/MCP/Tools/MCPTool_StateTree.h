// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Author Unreal Engine StateTree assets (states, tasks, transitions,
 * conditions, and utility-AI considerations).
 *
 * Ported from VibeUE's UStateTreeService into the native MCP tool pattern. State
 * paths use "/" separators starting from the subtree root name (e.g. "Root",
 * "Root/Walking", "Root/Walking/Idle").
 *
 * NOTE: This is a STUB. The schema (operations + parameters) is final, but every
 * OpXxx() body currently returns "not implemented yet". The real implementation
 * will drive UStateTreeEditorData / UStateTree via the StateTree editor modules
 * (see the .cpp header comment for the Build.cs module + engine-plugin deps).
 *
 * Editor-only: StateTree authoring requires WITH_EDITOR and the StateTreeEditorModule.
 */
class FMCPTool_StateTree : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** Create a new StateTree asset (asset_path, optional schema_class). */
	FMCPToolResult OpCreate(const TSharedRef<FJsonObject>& Params);

	/** Get full structural info about a StateTree asset (asset_path). */
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);

	/** Add a state under a parent (asset_path, parent_path, state_name, optional state_type). */
	FMCPToolResult OpAddState(const TSharedRef<FJsonObject>& Params);

	/** Remove a state and its children by path (asset_path, state_path). */
	FMCPToolResult OpRemoveState(const TSharedRef<FJsonObject>& Params);

	/** Mark a state as the entry/selection target of its parent (asset_path, state_path, optional selection_behavior). */
	FMCPToolResult OpSetEntry(const TSharedRef<FJsonObject>& Params);

	/** Add a task to a state (asset_path, state_path, task_struct). */
	FMCPToolResult OpAddTask(const TSharedRef<FJsonObject>& Params);

	/** Add a transition to a state (asset_path, state_path, trigger, transition_type, optional target_path/priority/event_tag). */
	FMCPToolResult OpAddTransition(const TSharedRef<FJsonObject>& Params);

	/** Add a condition to a state's enter conditions or a transition (asset_path, state_path, condition_struct, optional transition_index). */
	FMCPToolResult OpAddCondition(const TSharedRef<FJsonObject>& Params);

	/** Add a utility-AI consideration to a state (asset_path, state_path, consideration_struct). */
	FMCPToolResult OpAddConsideration(const TSharedRef<FJsonObject>& Params);

	/** Get the flattened list of states in the StateTree (asset_path). */
	FMCPToolResult OpGetStates(const TSharedRef<FJsonObject>& Params);

	/** Compile the StateTree asset (asset_path). */
	FMCPToolResult OpCompile(const TSharedRef<FJsonObject>& Params);

	/** Validate the StateTree structure without persisting a compile (asset_path). */
	FMCPToolResult OpValidate(const TSharedRef<FJsonObject>& Params);
};
