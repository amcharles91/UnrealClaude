// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Author Unreal Engine MetaSound source assets and their node graphs.
 *
 * Create MetaSound sources, inspect the graph, add DSP nodes, connect output
 * pins to input pins, set node input defaults, and add graph-level inputs and
 * outputs via the MetaSound Builder API.
 *
 * Ported from VibeUE's UMetaSoundService into the native MCP tool pattern.
 *
 * NOTE: This is a STUB. All operations currently return "not implemented yet".
 */
class FMCPTool_MetaSound : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetGraph(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddNode(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpConnectNodes(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetPin(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddGraphInput(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddGraphOutput(const TSharedRef<FJsonObject>& Params);
};
