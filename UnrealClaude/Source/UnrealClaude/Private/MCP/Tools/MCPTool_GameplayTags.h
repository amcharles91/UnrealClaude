// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Manage Unreal Engine Gameplay Tags.
 *
 * Query (list / has / get_info / get_children) via UGameplayTagsManager, and
 * editor mutations (add / remove / rename) via IGameplayTagsEditorModule, which
 * persist to DefaultGameplayTags.ini and register at runtime.
 *
 * Ported from VibeUE's UGameplayTagService into the native MCP tool pattern.
 */
class FMCPTool_GameplayTags : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpList(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpHas(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetChildren(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAdd(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemove(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRename(const TSharedRef<FJsonObject>& Params);

	/** Build a JSON object describing a tag (source, comment, explicit, child_count, redirect). */
	static TSharedPtr<FJsonObject> TagInfoJson(const FString& TagName);
};
