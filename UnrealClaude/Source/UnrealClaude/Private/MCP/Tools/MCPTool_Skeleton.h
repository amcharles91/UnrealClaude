// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Manage Unreal Engine Skeletons.
 *
 * Query and edit skeleton bone hierarchies, sockets, curve metadata, retargeting
 * modes, and blend profiles against USkeleton / USkeletalMesh assets.
 */
class FMCPTool_Skeleton : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpListBones(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddSocket(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveSocket(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetSockets(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddCurve(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetRetarget(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetBlendProfiles(const TSharedRef<FJsonObject>& Params);
};
