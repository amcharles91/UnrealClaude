// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_UVMapping.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UVMapSettings.h"
#endif

// Real implementation ported from VibeUE's UUVMappingService into the native MCP tool
// pattern. Static-mesh UV editing flows through FMeshDescription:
//   UStaticMesh::GetMeshDescription(LOD) -> FStaticMeshAttributes::GetVertexInstanceUVs()
// and mutations are committed via Modify() + CommitMeshDescription() + PostEditChange().
// Mutating ops are gated on WITH_EDITOR because GetMeshDescription is editor-only.
//
// Modules used (all already declared in UnrealClaude.Build.cs):
//   Engine (UStaticMesh), MeshDescription (FMeshDescription / vertex-instance UV attrs),
//   StaticMeshDescription (FStaticMeshAttributes, FStaticMeshOperations, FUVMapParameters).

namespace
{
#if WITH_EDITOR
	/** Load a StaticMesh asset by path. Returns nullptr if not found / not a StaticMesh. */
	UStaticMesh* LoadStaticMesh(const FString& MeshPath)
	{
		return LoadObject<UStaticMesh>(nullptr, *MeshPath);
	}

	/**
	 * Compute per-channel UV stats and write them into a JSON object: bounds, vertex-instance /
	 * triangle counts, and the fraction of UVs inside the [0,1] unit square. Read-only.
	 */
	void ChannelStatsToJson(const FMeshDescription& MeshDesc, int32 LODIndex, int32 ChannelIndex,
		const TSharedPtr<FJsonObject>& Out)
	{
		const FStaticMeshConstAttributes Attrs(MeshDesc);
		const TVertexInstanceAttributesConstRef<FVector2f> UVs = Attrs.GetVertexInstanceUVs();

		Out->SetNumberField(TEXT("lod_index"), LODIndex);
		Out->SetNumberField(TEXT("channel_index"), ChannelIndex);
		Out->SetNumberField(TEXT("vertex_instance_count"), MeshDesc.VertexInstances().Num());
		Out->SetNumberField(TEXT("triangle_count"), MeshDesc.Triangles().Num());
		Out->SetNumberField(TEXT("channel_count"), UVs.GetNumChannels());

		if (ChannelIndex < 0 || ChannelIndex >= UVs.GetNumChannels())
		{
			Out->SetBoolField(TEXT("channel_in_range"), false);
			return;
		}
		Out->SetBoolField(TEXT("channel_in_range"), true);

		FBox2f Bounds(ForceInit);
		int32 InUnitSquare = 0;
		int32 TotalUVs = 0;
		for (const FVertexInstanceID VI : MeshDesc.VertexInstances().GetElementIDs())
		{
			const FVector2f UV = UVs.Get(VI, ChannelIndex);
			Bounds += UV;
			++TotalUVs;
			if (UV.X >= 0.0f && UV.X <= 1.0f && UV.Y >= 0.0f && UV.Y <= 1.0f)
			{
				++InUnitSquare;
			}
		}

		if (TotalUVs > 0)
		{
			Out->SetNumberField(TEXT("min_u"), Bounds.Min.X);
			Out->SetNumberField(TEXT("min_v"), Bounds.Min.Y);
			Out->SetNumberField(TEXT("max_u"), Bounds.Max.X);
			Out->SetNumberField(TEXT("max_v"), Bounds.Max.Y);
			Out->SetNumberField(TEXT("in_unit_square_percent"),
				100.0f * static_cast<float>(InUnitSquare) / static_cast<float>(TotalUVs));
		}
	}

	/** Commit an edited mesh description back to the asset and trigger an editor rebuild/dirty. */
	void CommitMesh(UStaticMesh* Mesh, int32 LODIndex)
	{
		UStaticMesh::FCommitMeshDescriptionParams CommitParams;
		CommitParams.bMarkPackageDirty = true;
		CommitParams.bUseHashAsGuid = false;
		Mesh->CommitMeshDescription(LODIndex, CommitParams);
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();
	}
#endif // WITH_EDITOR
}

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
#if WITH_EDITOR
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath));
	}

	const int32 NumLODs = Mesh->GetNumSourceModels();
	const bool bSingleChannel = Params->HasField(TEXT("channel_index"));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("lod_count"), NumLODs);
	Data->SetNumberField(TEXT("lightmap_coordinate_index"), Mesh->GetLightMapCoordinateIndex());
	Data->SetNumberField(TEXT("lightmap_resolution"), Mesh->GetLightMapResolution());

	// Single-channel path: stats for one (lod, channel).
	if (bSingleChannel)
	{
		const int32 LODIndex = ExtractOptionalNumber<int32>(Params, TEXT("lod_index"), 0);
		const int32 ChannelIndex = ExtractOptionalNumber<int32>(Params, TEXT("channel_index"), 0);
		if (LODIndex < 0 || LODIndex >= NumLODs)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("LOD %d out of range (have %d)"), LODIndex, NumLODs));
		}
		FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIndex);
		if (!MeshDesc)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("LOD %d has no mesh description"), LODIndex));
		}
		ChannelStatsToJson(*MeshDesc, LODIndex, ChannelIndex, Data);
		return FMCPToolResult::Success(FString::Printf(
			TEXT("UV stats for '%s' LOD %d channel %d"), *MeshPath, LODIndex, ChannelIndex), Data);
	}

	// Full health report: enumerate every channel on every LOD.
	TArray<TSharedPtr<FJsonValue>> ChannelsJson;
	bool bLightmapInRange = false;
	const int32 LightmapIdx = Mesh->GetLightMapCoordinateIndex();
	for (int32 LOD = 0; LOD < NumLODs; ++LOD)
	{
		FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LOD);
		if (!MeshDesc) { continue; }

		const FStaticMeshConstAttributes Attrs(*MeshDesc);
		const int32 NumChannels = Attrs.GetVertexInstanceUVs().GetNumChannels();
		for (int32 Ch = 0; Ch < NumChannels; ++Ch)
		{
			TSharedPtr<FJsonObject> ChObj = MakeShared<FJsonObject>();
			ChannelStatsToJson(*MeshDesc, LOD, Ch, ChObj);
			ChannelsJson.Add(MakeShared<FJsonValueObject>(ChObj));
		}
		if (LOD == 0 && LightmapIdx >= 0 && LightmapIdx < NumChannels)
		{
			bLightmapInRange = true;
		}
	}
	Data->SetArrayField(TEXT("channels"), ChannelsJson);
	Data->SetBoolField(TEXT("lightmap_channel_in_range"), bLightmapInRange);
	if (!bLightmapInRange)
	{
		Data->SetStringField(TEXT("warning"), FString::Printf(
			TEXT("LightMapCoordinateIndex (%d) is out of range on LOD 0."), LightmapIdx));
	}

	return FMCPToolResult::Success(FString::Printf(
		TEXT("UV health report for '%s' (%d LODs, %d channels total)"),
		*MeshPath, NumLODs, ChannelsJson.Num()), Data);
#else
	return FMCPToolResult::Error(TEXT("get_uv_info requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UVMapping::OpTransformUV(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath)); }

	const int32 LODIndex = ExtractOptionalNumber<int32>(Params, TEXT("lod_index"), 0);
	const int32 ChannelIndex = ExtractOptionalNumber<int32>(Params, TEXT("channel_index"), 0);
	const float ScaleU = ExtractOptionalNumber<float>(Params, TEXT("scale_u"), 1.0f);
	const float ScaleV = ExtractOptionalNumber<float>(Params, TEXT("scale_v"), 1.0f);
	const float RotationDegrees = ExtractOptionalNumber<float>(Params, TEXT("rotation_degrees"), 0.0f);
	const float OffsetU = ExtractOptionalNumber<float>(Params, TEXT("offset_u"), 0.0f);
	const float OffsetV = ExtractOptionalNumber<float>(Params, TEXT("offset_v"), 0.0f);

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIndex);
	if (!MeshDesc) { return FMCPToolResult::Error(FString::Printf(TEXT("LOD %d has no mesh description"), LODIndex)); }

	FStaticMeshAttributes Attrs(*MeshDesc);
	TVertexInstanceAttributesRef<FVector2f> UVs = Attrs.GetVertexInstanceUVs();
	if (ChannelIndex < 0 || ChannelIndex >= UVs.GetNumChannels())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Channel %d out of range (have %d)"), ChannelIndex, UVs.GetNumChannels()));
	}

	const float CosR = FMath::Cos(FMath::DegreesToRadians(RotationDegrees));
	const float SinR = FMath::Sin(FMath::DegreesToRadians(RotationDegrees));

	Mesh->Modify();
	int32 Count = 0;
	for (const FVertexInstanceID VI : MeshDesc->VertexInstances().GetElementIDs())
	{
		FVector2f UV = UVs.Get(VI, ChannelIndex);
		// Scale, then rotate about the UV origin, then translate.
		UV.X *= ScaleU;
		UV.Y *= ScaleV;
		const float Rx = UV.X * CosR - UV.Y * SinR;
		const float Ry = UV.X * SinR + UV.Y * CosR;
		UVs.Set(VI, ChannelIndex, FVector2f(Rx + OffsetU, Ry + OffsetV));
		++Count;
	}

	CommitMesh(Mesh, LODIndex);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("lod_index"), LODIndex);
	Data->SetNumberField(TEXT("channel_index"), ChannelIndex);
	Data->SetNumberField(TEXT("vertex_instances_transformed"), Count);
	return FMCPToolResult::Success(FString::Printf(
		TEXT("Transformed channel %d on LOD %d (scale %.3f,%.3f rot %.1f offset %.3f,%.3f) over %d verts"),
		ChannelIndex, LODIndex, ScaleU, ScaleV, RotationDegrees, OffsetU, OffsetV, Count), Data);
#else
	return FMCPToolResult::Error(TEXT("transform_uv requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UVMapping::OpAutoUnwrap(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath)); }

	const int32 LODIndex = ExtractOptionalNumber<int32>(Params, TEXT("lod_index"), 0);
	const int32 ChannelIndex = ExtractOptionalNumber<int32>(Params, TEXT("channel_index"), 0);
	const FString ProjectionType = ExtractOptionalString(Params, TEXT("projection_type"), TEXT("Box"));
	const float HardAngleThreshold = ExtractOptionalNumber<float>(Params, TEXT("hard_angle_threshold"), 66.0f);

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIndex);
	if (!MeshDesc) { return FMCPToolResult::Error(FString::Printf(TEXT("LOD %d has no mesh description"), LODIndex)); }

	FStaticMeshAttributes Attrs(*MeshDesc);
	TVertexInstanceAttributesRef<FVector2f> UVs = Attrs.GetVertexInstanceUVs();
	if (ChannelIndex < 0 || ChannelIndex >= UVs.GetNumChannels())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Channel %d out of range (have %d). Add a channel first."), ChannelIndex, UVs.GetNumChannels()));
	}

	// Size the projection gizmo from the mesh's bounding box.
	const FBox MeshBounds = Mesh->GetBoundingBox();
	FUVMapParameters MapParams;
	MapParams.Position = MeshBounds.GetCenter();
	MapParams.Rotation = FQuat::Identity;
	MapParams.Size = MeshBounds.GetSize();
	MapParams.Scale = FVector(1.0, 1.0, 1.0);
	MapParams.UVTile = FVector2D(1.0, 1.0);

	TMap<FVertexInstanceID, FVector2D> NewUVs;
	const FString Lower = ProjectionType.ToLower();
	if (Lower == TEXT("planar"))
	{
		FStaticMeshOperations::GeneratePlanarUV(*MeshDesc, MapParams, NewUVs);
	}
	else if (Lower == TEXT("cylindrical"))
	{
		FStaticMeshOperations::GenerateCylindricalUV(*MeshDesc, MapParams, NewUVs);
	}
	else if (Lower == TEXT("box"))
	{
		FStaticMeshOperations::GenerateBoxUV(*MeshDesc, MapParams, NewUVs);
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown projection_type '%s'. Use 'Planar', 'Box', or 'Cylindrical'."), *ProjectionType));
	}

	// HardAngleThreshold accepted for parity with future seam-aware unwrap; the engine
	// generators above seam internally from mesh normals.
	(void)HardAngleThreshold;

	Mesh->Modify();
	for (const TPair<FVertexInstanceID, FVector2D>& Pair : NewUVs)
	{
		UVs.Set(Pair.Key, ChannelIndex, FVector2f(Pair.Value));
	}

	CommitMesh(Mesh, LODIndex);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("lod_index"), LODIndex);
	Data->SetNumberField(TEXT("channel_index"), ChannelIndex);
	Data->SetStringField(TEXT("projection_type"), ProjectionType);
	Data->SetNumberField(TEXT("vertex_instances_unwrapped"), NewUVs.Num());
	return FMCPToolResult::Success(FString::Printf(
		TEXT("Auto-unwrapped %d vertex instances into LOD %d channel %d using %s projection"),
		NewUVs.Num(), LODIndex, ChannelIndex, *ProjectionType), Data);
#else
	return FMCPToolResult::Error(TEXT("auto_unwrap requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UVMapping::OpGenerateLightmapUV(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath)); }
	if (Mesh->GetNumSourceModels() == 0)
	{
		return FMCPToolResult::Error(TEXT("StaticMesh has no source models"));
	}

	const int32 SourceUVIndex = ExtractOptionalNumber<int32>(Params, TEXT("source_uv_index"), 0);
	const int32 DestUVIndex = ExtractOptionalNumber<int32>(Params, TEXT("dest_uv_index"), 1);
	const float MinChartSpacingPercent = ExtractOptionalNumber<float>(Params, TEXT("min_chart_spacing_percent"), 1.0f);

	Mesh->Modify();

	// Drive the engine lightmap generator via LOD 0 build settings, then rebuild.
	FStaticMeshSourceModel& SM = Mesh->GetSourceModel(0);
	SM.BuildSettings.bGenerateLightmapUVs = true;
	SM.BuildSettings.SrcLightmapIndex = SourceUVIndex;
	SM.BuildSettings.DstLightmapIndex = DestUVIndex;
	SM.BuildSettings.MinLightmapResolution = FMath::Max(
		Mesh->GetLightMapResolution(),
		FMath::CeilToInt(100.0f / FMath::Max(MinChartSpacingPercent, 0.1f)));

	Mesh->SetLightMapCoordinateIndex(DestUVIndex);

	Mesh->Build(/*bInSilent=*/false);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("source_uv_index"), SourceUVIndex);
	Data->SetNumberField(TEXT("dest_uv_index"), DestUVIndex);
	Data->SetNumberField(TEXT("lightmap_coordinate_index"), Mesh->GetLightMapCoordinateIndex());
	return FMCPToolResult::Success(FString::Printf(
		TEXT("Generated lightmap UVs into channel %d (source %d); LightMapCoordinateIndex set to %d"),
		DestUVIndex, SourceUVIndex, DestUVIndex), Data);
#else
	return FMCPToolResult::Error(TEXT("generate_lightmap_uv requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UVMapping::OpPackUVs(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UStaticMesh* Mesh = LoadStaticMesh(MeshPath);
	if (!Mesh) { return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath)); }

	const int32 LODIndex = ExtractOptionalNumber<int32>(Params, TEXT("lod_index"), 0);
	const int32 ChannelIndex = ExtractOptionalNumber<int32>(Params, TEXT("channel_index"), 0);
	const float PaddingPercent = ExtractOptionalNumber<float>(Params, TEXT("padding_percent"), 1.0f);

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIndex);
	if (!MeshDesc) { return FMCPToolResult::Error(FString::Printf(TEXT("LOD %d has no mesh description"), LODIndex)); }

	FStaticMeshAttributes Attrs(*MeshDesc);
	TVertexInstanceAttributesRef<FVector2f> UVs = Attrs.GetVertexInstanceUVs();
	if (ChannelIndex < 0 || ChannelIndex >= UVs.GetNumChannels())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Channel %d out of range (have %d)"), ChannelIndex, UVs.GetNumChannels()));
	}

	// Compute current UV bounds.
	FBox2f Bounds(ForceInit);
	int32 Count = 0;
	for (const FVertexInstanceID VI : MeshDesc->VertexInstances().GetElementIDs())
	{
		Bounds += UVs.Get(VI, ChannelIndex);
		++Count;
	}
	if (Count == 0) { return FMCPToolResult::Error(TEXT("No vertex instances on this LOD")); }

	const FVector2f Extent = Bounds.GetSize();
	if (Extent.X < KINDA_SMALL_NUMBER || Extent.Y < KINDA_SMALL_NUMBER)
	{
		return FMCPToolResult::Error(TEXT("UV bounds are degenerate; nothing to pack"));
	}

	// Fit-to-square repack with uniform padding. (Tight repack without island detection;
	// run auto_unwrap with Box projection first for full island layout.)
	const float Pad = FMath::Clamp(PaddingPercent, 0.0f, 25.0f) / 100.0f;
	const float TargetExtent = 1.0f - 2.0f * Pad;
	const float Scale = TargetExtent / FMath::Max(Extent.X, Extent.Y);

	Mesh->Modify();
	for (const FVertexInstanceID VI : MeshDesc->VertexInstances().GetElementIDs())
	{
		const FVector2f UV = UVs.Get(VI, ChannelIndex);
		const FVector2f Packed = (UV - Bounds.Min) * Scale + FVector2f(Pad, Pad);
		UVs.Set(VI, ChannelIndex, Packed);
	}

	CommitMesh(Mesh, LODIndex);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("lod_index"), LODIndex);
	Data->SetNumberField(TEXT("channel_index"), ChannelIndex);
	Data->SetNumberField(TEXT("padding_percent"), PaddingPercent);
	Data->SetNumberField(TEXT("scale_applied"), Scale);
	return FMCPToolResult::Success(FString::Printf(
		TEXT("Packed channel %d on LOD %d into [%.3f, %.3f]^2 (scale %.4f)"),
		ChannelIndex, LODIndex, Pad, 1.0f - Pad, Scale), Data);
#else
	return FMCPToolResult::Error(TEXT("pack_uvs requires an editor build"));
#endif
}
