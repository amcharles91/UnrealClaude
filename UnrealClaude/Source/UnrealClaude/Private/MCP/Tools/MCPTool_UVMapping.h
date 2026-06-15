// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Inspect and edit StaticMesh UV channels.
 *
 * STUB — schemas are final but operation bodies are not implemented yet. When the
 * real implementation lands it will port VibeUE's UUVMappingService: per-LOD UV
 * channel inspection, affine UV transforms, projection auto-unwrap, lightmap UV
 * generation, and island packing, all via FMeshDescription / FStaticMeshOperations.
 *
 * Build.cs dependencies required for the real implementation (NOT yet added):
 * - "MeshDescription"            (FMeshDescription, vertex-instance UV attributes)
 * - "StaticMeshDescription"      (UStaticMesh <-> FMeshDescription get/commit)
 * - "MeshConversionEngineTypes"  (FStaticMeshOperations::GenerateUniqueUVsForStaticMesh)
 * No engine PLUGIN dependency is required (these are engine modules, not plugins).
 *
 * Ported (schema only) from VibeUE's UUVMappingService into the native MCP tool pattern.
 */
class FMCPTool_UVMapping : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** Per-LOD channel stats / aggregate health report (ListUVChannels / GetUVChannelInfo / GetUVHealth). */
	FMCPToolResult OpGetUVInfo(const TSharedRef<FJsonObject>& Params);

	/** Scale, rotate, translate UVs in a channel (TransformUVs). */
	FMCPToolResult OpTransformUV(const TSharedRef<FJsonObject>& Params);

	/** Planar / box / cylindrical projection unwrap into a channel (AutoUnwrapUVs). */
	FMCPToolResult OpAutoUnwrap(const TSharedRef<FJsonObject>& Params);

	/** Build a packed lightmap UV channel from a source channel (GenerateLightmapUVs). */
	FMCPToolResult OpGenerateLightmapUV(const TSharedRef<FJsonObject>& Params);

	/** Repack islands in an existing channel with padding (PackUVs). */
	FMCPToolResult OpPackUVs(const TSharedRef<FJsonObject>& Params);
};
