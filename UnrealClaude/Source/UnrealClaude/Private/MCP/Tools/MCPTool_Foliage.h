// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Manage Unreal Engine foliage (types + instanced placement).
 *
 * STUB — schemas are final but operation bodies are not implemented yet. When the
 * real implementation lands it will port VibeUE's UFoliageService: foliage type
 * assets via UFoliageType, instanced placement via AInstancedFoliageActor, and
 * layer-aware scatter against landscape paint layers.
 *
 * Build.cs dependencies required for the real implementation (NOT yet added):
 * - "Foliage"     (runtime: AInstancedFoliageActor, UFoliageType)
 * - "FoliageEdit" (editor placement helpers)
 *
 * Ported (schema only) from VibeUE's UFoliageService into the native MCP tool pattern.
 */
class FMCPTool_Foliage : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** Create a UFoliageType asset from a static mesh (CreateFoliageType). */
	FMCPToolResult OpAddType(const TSharedRef<FJsonObject>& Params);

	/** Scatter instances in a circular region with Poisson sampling (ScatterFoliage). */
	FMCPToolResult OpScatter(const TSharedRef<FJsonObject>& Params);

	/** Scatter only where a landscape paint layer is dominant (ScatterFoliageOnLayer). */
	FMCPToolResult OpPaint(const TSharedRef<FJsonObject>& Params);

	/** Remove instances in a radius or all of a type (RemoveFoliageInRadius / RemoveAllFoliageOfType). */
	FMCPToolResult OpRemove(const TSharedRef<FJsonObject>& Params);

	/** List all foliage types in the level with instance counts (ListFoliageTypes). */
	FMCPToolResult OpListTypes(const TSharedRef<FJsonObject>& Params);

	/** Query foliage instances of a type in a circular region (GetFoliageInRadius). */
	FMCPToolResult OpGetInstances(const TSharedRef<FJsonObject>& Params);
};
