// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Build and edit Landscape Materials.
 *
 * Covers auto-material generation (height/slope blended), LandscapeLayerBlend
 * node editing, ULandscapeLayerInfoObject creation, and LandscapeGrassOutput
 * wiring for procedural foliage.
 *
 * Ported from VibeUE's ULandscapeMaterialService into the native MCP tool
 * pattern.
 *
 * NOTE: STUB. Schemas and op dispatch are final; Execute bodies return
 * "not implemented yet" until the Landscape, UnrealEd, and MaterialEditor
 * dependencies are wired into the module.
 */
class FMCPTool_LandscapeMaterial : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpCreateAutoMaterial(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddLayerBlend(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetLayerInfo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddGrassOutput(const TSharedRef<FJsonObject>& Params);
};
