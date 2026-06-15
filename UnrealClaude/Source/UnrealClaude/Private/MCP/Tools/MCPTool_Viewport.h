// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Control the Unreal Editor level viewport.
 *
 * Provides camera control (location/rotation/FOV), view mode (lit/unlit/wireframe),
 * exposure (fixed EV100 or auto game settings), viewport layout (single pane / quad
 * view), and Game View toggling for the active level editor viewport.
 *
 * Ported from VibeUE's UViewportService into the native MCP tool pattern.
 *
 * NOTE: This is a STUB. All operations return "not implemented yet" until the
 * underlying engine integration is wired up.
 */
class FMCPTool_Viewport : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpSetCamera(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetViewMode(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetFov(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetExposure(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetLayout(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpToggleGameView(const TSharedRef<FJsonObject>& Params);
};
