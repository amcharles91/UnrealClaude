// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_UVMapping.h"

// NOTE: STUB. Includes are intentionally minimal so the tool compiles before the
// MeshDescription / StaticMeshDescription / MeshConversionEngineTypes module
// dependencies are added to Build.cs. The real implementation will add:
// #include "StaticMeshAttributes.h", "MeshDescription.h", "StaticMeshOperations.h", etc.

FMCPToolInfo FMCPTool_UVMapping::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("uv_mapping");
	Info.Description = TEXT(
		"Inspect and edit StaticMesh UV channels: channel stats, transforms, auto-unwrap, "
		"lightmap UV generation, and island packing.\n\n"
		"Operations (set 'operation'):\n"
		"- 'get_uv_info': Channel stats for 'mesh_path' (omit channel for a full health report)\n"
		"- 'transform_uv': Scale/rotate/translate UVs in ('lod_index', 'channel_index')\n"
		"- 'auto_unwrap': Projection unwrap ('projection_type' = Planar/Box/Cylindrical) into a channel\n"
		"- 'generate_lightmap_uv': Pack a lightmap channel from 'source_uv_index' into 'dest_uv_index'\n"
		"- 'pack_uvs': Repack existing islands in a channel with 'padding_percent'\n\n"
		"NOTE: This tool is a stub; operations are not implemented yet.\n\n"
		"Returns: operation-specific data (channel stats/health, or mutation result)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: get_uv_info, transform_uv, auto_unwrap, generate_lightmap_uv, pack_uvs"), true),
		FMCPToolParameter(TEXT("mesh_path"), TEXT("string"), TEXT("Full path to a StaticMesh asset (e.g. '/Game/Meshes/SM_Wall')"), true),
		FMCPToolParameter(TEXT("lod_index"), TEXT("number"), TEXT("LOD index to operate on (default 0)"), false),
		FMCPToolParameter(TEXT("channel_index"), TEXT("number"), TEXT("UV channel index for get_uv_info/transform_uv/auto_unwrap/pack_uvs"), false),
		FMCPToolParameter(TEXT("scale_u"), TEXT("number"), TEXT("For 'transform_uv': U scale factor (default 1.0)"), false),
		FMCPToolParameter(TEXT("scale_v"), TEXT("number"), TEXT("For 'transform_uv': V scale factor (default 1.0)"), false),
		FMCPToolParameter(TEXT("rotation_degrees"), TEXT("number"), TEXT("For 'transform_uv': rotation about UV origin in degrees (default 0)"), false),
		FMCPToolParameter(TEXT("offset_u"), TEXT("number"), TEXT("For 'transform_uv': U translation applied after scale/rotate (default 0)"), false),
		FMCPToolParameter(TEXT("offset_v"), TEXT("number"), TEXT("For 'transform_uv': V translation applied after scale/rotate (default 0)"), false),
		FMCPToolParameter(TEXT("projection_type"), TEXT("string"), TEXT("For 'auto_unwrap': 'Planar', 'Box', or 'Cylindrical' (default 'Box')"), false),
		FMCPToolParameter(TEXT("hard_angle_threshold"), TEXT("number"), TEXT("For 'auto_unwrap': seam angle threshold in degrees, Box mode (default 66.0)"), false),
		FMCPToolParameter(TEXT("source_uv_index"), TEXT("number"), TEXT("For 'generate_lightmap_uv': channel to read seams from (default 0)"), false),
		FMCPToolParameter(TEXT("dest_uv_index"), TEXT("number"), TEXT("For 'generate_lightmap_uv': channel to write the lightmap into (default 1)"), false),
		FMCPToolParameter(TEXT("min_chart_spacing_percent"), TEXT("number"), TEXT("For 'generate_lightmap_uv': chart padding as percent of UV space (default 1.0)"), false),
		FMCPToolParameter(TEXT("padding_percent"), TEXT("number"), TEXT("For 'pack_uvs': padding between islands as percent of UV space (default 1.0)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_UVMapping::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("get_uv_info"))          { return OpGetUVInfo(Params); }
	if (Operation == TEXT("transform_uv"))         { return OpTransformUV(Params); }
	if (Operation == TEXT("auto_unwrap"))          { return OpAutoUnwrap(Params); }
	if (Operation == TEXT("generate_lightmap_uv")) { return OpGenerateLightmapUV(Params); }
	if (Operation == TEXT("pack_uvs"))             { return OpPackUVs(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: get_uv_info, transform_uv, auto_unwrap, generate_lightmap_uv, pack_uvs"), *Operation));
}

FMCPToolResult FMCPTool_UVMapping::OpGetUVInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_uv_info: not implemented yet"));
}

FMCPToolResult FMCPTool_UVMapping::OpTransformUV(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("transform_uv: not implemented yet"));
}

FMCPToolResult FMCPTool_UVMapping::OpAutoUnwrap(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("auto_unwrap: not implemented yet"));
}

FMCPToolResult FMCPTool_UVMapping::OpGenerateLightmapUV(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("generate_lightmap_uv: not implemented yet"));
}

FMCPToolResult FMCPTool_UVMapping::OpPackUVs(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("pack_uvs: not implemented yet"));
}
