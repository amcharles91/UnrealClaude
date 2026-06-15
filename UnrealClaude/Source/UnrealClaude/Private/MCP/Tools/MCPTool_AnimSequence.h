// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Manage Unreal Engine Animation Sequences.
 *
 * Query (list / get_info) and editor mutations (curves, keys, notifies, frame
 * rate) against UAnimSequence assets via the IAnimationDataController data-model APIs.
 */
class FMCPTool_AnimSequence : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpList(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddCurve(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveCurve(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddKey(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddNotify(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveNotify(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetFrameRate(const TSharedRef<FJsonObject>& Params);
};
