// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Author Unreal Engine SoundCue assets and their node graphs.
 *
 * Create cues, inspect the node graph, add/connect sound nodes, set node
 * properties via reflection, and import SoundWaves from disk.
 *
 * Ported from VibeUE's USoundCueService into the native MCP tool pattern.
 */
class FMCPTool_SoundCue : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetGraph(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddNode(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpConnectNodes(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetNodeProperty(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpImportWave(const TSharedRef<FJsonObject>& Params);
};
