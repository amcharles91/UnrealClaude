// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Author and edit Unreal Engine Landscapes.
 *
 * Covers landscape actor creation, introspection, sculpting/heightmap edits,
 * paint-layer management, splines, holes, and Runtime Virtual Texture (RVT)
 * assignment.
 *
 * Ported from VibeUE's ULandscapeService and URuntimeVirtualTextureService
 * (assign_rvt) into the native MCP tool pattern.
 *
 * NOTE: STUB. Schemas and op dispatch are final; Execute bodies return
 * "not implemented yet" until the Landscape/LandscapeEditor and
 * RuntimeVirtualTexture dependencies are wired into the module.
 */
class FMCPTool_Landscape : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSculpt(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetHeightmap(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpPaintLayer(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddLayer(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddSpline(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddHole(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAssignRVT(const TSharedRef<FJsonObject>& Params);
};
