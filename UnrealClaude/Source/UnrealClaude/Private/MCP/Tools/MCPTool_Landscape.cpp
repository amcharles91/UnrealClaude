// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.

#include "MCPTool_Landscape.h"
#include "UnrealClaudeModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "EditorAssetLibrary.h"

#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeEditLayer.h"

#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureEnum.h"

#if WITH_EDITOR
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"
#include "ScopedTransaction.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#endif // WITH_EDITOR

// =====================================================================
// File-local helpers (no header members — keeps ABI stable for Live Coding)
// =====================================================================
namespace
{
	/** Resolve the target ALandscape from the 'landscape' parameter (name or label). */
	ALandscape* ResolveLandscape(UWorld* World, const FString& NameOrLabel)
	{
		if (!World || NameOrLabel.IsEmpty())
		{
			return nullptr;
		}
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			ALandscape* Landscape = *It;
			if (!Landscape)
			{
				continue;
			}
			if (Landscape->GetName().Equals(NameOrLabel, ESearchCase::IgnoreCase) ||
				Landscape->GetActorLabel().Equals(NameOrLabel, ESearchCase::IgnoreCase))
			{
				return Landscape;
			}
		}
		return nullptr;
	}

#if WITH_EDITOR
	/**
	 * Resolve a valid editing-layer GUID. On freshly created/edit-layer landscapes
	 * GetEditingLayer() can be invalid because nothing called SetEditingLayer();
	 * fall back to the first available edit layer (mirrors the Landscape editor UI).
	 */
	FGuid ResolveEditLayerGuid(ALandscape* Landscape)
	{
		if (!Landscape)
		{
			return FGuid();
		}
		FGuid LayerGuid = Landscape->GetEditingLayer();
		if (!LayerGuid.IsValid())
		{
			const TArray<ULandscapeEditLayerBase*> EditLayers = Landscape->GetEditLayers();
			if (EditLayers.Num() > 0 && EditLayers[0])
			{
				LayerGuid = EditLayers[0]->GetGuid();
			}
		}
		return LayerGuid;
	}

	/** Push queued edit-layer content to the merged heightmap and rebuild collision. */
	void FinalizeLandscapeEdit(ALandscape* Landscape)
	{
		if (!Landscape)
		{
			return;
		}
		// Process queued edit-layer merges synchronously so subsequent reads (and the
		// collision rebuild below) see the just-edited heights instead of stale data.
		Landscape->ForceUpdateLayersContent();
		Landscape->RequestLayersContentUpdateForceAll();

		UWorld* World = Landscape->GetWorld();
		const FGuid LandscapeGuid = Landscape->GetLandscapeGuid();
		if (World)
		{
			for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
			{
				ALandscapeProxy* Proxy = *It;
				if (!Proxy || Proxy->GetLandscapeGuid() != LandscapeGuid)
				{
					continue;
				}
				Proxy->RecreateCollisionComponents();
			}
		}
	}

	/** Map a world-space XY (cm) to landscape vertex coordinates (quad space). */
	FVector2D WorldToVertex(ALandscape* Landscape, double WorldX, double WorldY)
	{
		// InverseTransformPosition removes the actor scale, so the resulting local
		// XY is already expressed in quads (1 unit == 1 quad before draw scale).
		const FVector Local = Landscape->GetActorTransform().InverseTransformPosition(FVector(WorldX, WorldY, 0.0));
		return FVector2D(Local.X, Local.Y);
	}

	/** Brush falloff weight in [0,1] given normalized distance (0 at center, 1 at edge). */
	float BrushFalloff(const FString& Type, float NormalizedDist)
	{
		const float D = FMath::Clamp(NormalizedDist, 0.0f, 1.0f);
		if (Type.Equals(TEXT("Smooth"), ESearchCase::IgnoreCase))
		{
			// Smoothstep-style cosine falloff.
			return 0.5f * (1.0f + FMath::Cos(PI * D));
		}
		if (Type.Equals(TEXT("Spherical"), ESearchCase::IgnoreCase))
		{
			return FMath::Sqrt(FMath::Max(0.0f, 1.0f - D * D));
		}
		if (Type.Equals(TEXT("Tip"), ESearchCase::IgnoreCase))
		{
			// Sharp peak: weight^2 of the linear falloff.
			const float Lin = 1.0f - D;
			return Lin * Lin;
		}
		// Linear (default).
		return 1.0f - D;
	}

	/** Convert a signed world-unit (cm) height delta to a uint16 height delta. */
	int32 WorldDeltaToHeightUnits(ALandscape* Landscape, double WorldDeltaZ)
	{
		// LANDSCAPE_ZSCALE maps a uint16 unit to local Z; multiply by actor Z scale for cm.
		const double CmPerUnit = (double)LANDSCAPE_ZSCALE * Landscape->GetActorScale().Z;
		if (FMath::IsNearlyZero(CmPerUnit))
		{
			return 0;
		}
		return FMath::RoundToInt(WorldDeltaZ / CmPerUnit);
	}
#endif // WITH_EDITOR
}

FMCPToolInfo FMCPTool_Landscape::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("landscape");
	Info.Description = TEXT(
		"Author and edit Unreal Engine Landscapes (create, sculpt, heightmaps, paint layers, splines, holes, RVT).\n\n"
		"Operations (set 'operation'):\n"
		"- 'create': Spawn a new landscape actor at 'location'/'rotation'/'scale' with the given component grid "
		"('sections_per_component', 'quads_per_section', 'component_count_x', 'component_count_y'); optional 'label'\n"
		"- 'get_info': Resolution, component grid, bounds, layers, and material for 'landscape'\n"
		"- 'sculpt': Raise/lower terrain at ('world_x','world_y') with 'brush_radius' and 'strength' (signed world units), "
		"optional 'falloff' (Linear, Smooth, Spherical, Tip)\n"
		"- 'set_heightmap': Import a 16-bit heightmap into 'landscape' from 'file_path' (resolution must match exactly), "
		"or set 'heights' in the region ('start_x','start_y','size_x','size_y')\n"
		"- 'paint_layer': Paint weight for 'layer' at ('world_x','world_y') with 'brush_radius' and 'strength' (0..1)\n"
		"- 'add_layer': Add a paint layer to 'landscape' from a ULandscapeLayerInfoObject at 'layer_info_path'\n"
		"- 'add_spline': Create a spline path from 'points' (array of world locations); optional 'width', "
		"'side_falloff', 'end_falloff', 'paint_layer', 'raise_terrain', 'lower_terrain', 'closed_loop'\n"
		"- 'add_hole': Punch (or fill) a visibility hole at ('world_x','world_y') with 'brush_radius'; "
		"set 'create_hole' false to fill\n"
		"- 'assign_rvt': Assign a Runtime Virtual Texture at 'rvt_path' to 'landscape' at 'slot_index'\n\n"
		"Locations/rotations/scale are nested objects ({x,y,z} / {pitch,yaw,roll}). World coordinates are in cm.\n\n"
		"Returns: operation-specific data (created actor info, landscape info, or edit confirmation)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: create, get_info, sculpt, set_heightmap, paint_layer, add_layer, add_spline, add_hole, assign_rvt"), true),
		FMCPToolParameter(TEXT("landscape"), TEXT("string"), TEXT("Target landscape actor name or label (required for all ops except 'create')"), false),
		// create
		FMCPToolParameter(TEXT("location"), TEXT("object"), TEXT("For 'create': world location {x,y,z} (default 0,0,0)"), false),
		FMCPToolParameter(TEXT("rotation"), TEXT("object"), TEXT("For 'create': world rotation {pitch,yaw,roll} (default 0,0,0)"), false),
		FMCPToolParameter(TEXT("scale"), TEXT("object"), TEXT("For 'create': scale {x,y,z} (default 100,100,100 = 1m/unit)"), false),
		FMCPToolParameter(TEXT("sections_per_component"), TEXT("number"), TEXT("For 'create': 1 or 2 (default 1)"), false),
		FMCPToolParameter(TEXT("quads_per_section"), TEXT("number"), TEXT("For 'create': 7, 15, 31, 63, 127, or 255 (default 63)"), false),
		FMCPToolParameter(TEXT("component_count_x"), TEXT("number"), TEXT("For 'create': components in X (default 8)"), false),
		FMCPToolParameter(TEXT("component_count_y"), TEXT("number"), TEXT("For 'create': components in Y (default 8)"), false),
		FMCPToolParameter(TEXT("label"), TEXT("string"), TEXT("For 'create': optional display label for the new actor"), false),
		// sculpt / paint / hole shared world-brush params
		FMCPToolParameter(TEXT("world_x"), TEXT("number"), TEXT("For sculpt/paint_layer/add_hole: world X (cm)"), false),
		FMCPToolParameter(TEXT("world_y"), TEXT("number"), TEXT("For sculpt/paint_layer/add_hole: world Y (cm)"), false),
		FMCPToolParameter(TEXT("brush_radius"), TEXT("number"), TEXT("For sculpt/paint_layer/add_hole: brush radius in world units"), false),
		FMCPToolParameter(TEXT("strength"), TEXT("number"), TEXT("For 'sculpt': signed height delta (world units). For 'paint_layer': weight 0..1"), false),
		FMCPToolParameter(TEXT("falloff"), TEXT("string"), TEXT("For 'sculpt': brush falloff (Linear, Smooth, Spherical, Tip; default Linear)"), false),
		// set_heightmap
		FMCPToolParameter(TEXT("file_path"), TEXT("string"), TEXT("For 'set_heightmap': absolute path to a 16-bit heightmap (PNG/RAW); resolution must match landscape"), false),
		FMCPToolParameter(TEXT("start_x"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: start vertex X"), false),
		FMCPToolParameter(TEXT("start_y"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: start vertex Y"), false),
		FMCPToolParameter(TEXT("size_x"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: width in vertices"), false),
		FMCPToolParameter(TEXT("size_y"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: height in vertices"), false),
		FMCPToolParameter(TEXT("heights"), TEXT("array"), TEXT("For 'set_heightmap' region: row-major float heights (size = size_x * size_y)"), false),
		// paint_layer / add_layer
		FMCPToolParameter(TEXT("layer"), TEXT("string"), TEXT("For 'paint_layer': name of the paint layer to paint"), false),
		FMCPToolParameter(TEXT("layer_info_path"), TEXT("string"), TEXT("For 'add_layer': path to the ULandscapeLayerInfoObject asset"), false),
		// add_spline
		FMCPToolParameter(TEXT("points"), TEXT("array"), TEXT("For 'add_spline': ordered array of world locations [{x,y,z},...]"), false),
		FMCPToolParameter(TEXT("width"), TEXT("number"), TEXT("For 'add_spline': control-point half-width (default 500)"), false),
		FMCPToolParameter(TEXT("side_falloff"), TEXT("number"), TEXT("For 'add_spline': side falloff (default 500)"), false),
		FMCPToolParameter(TEXT("end_falloff"), TEXT("number"), TEXT("For 'add_spline': end falloff (default 500)"), false),
		FMCPToolParameter(TEXT("paint_layer"), TEXT("string"), TEXT("For 'add_spline': layer painted under the spline (optional)"), false),
		FMCPToolParameter(TEXT("raise_terrain"), TEXT("boolean"), TEXT("For 'add_spline': raise terrain along spline (default true)"), false),
		FMCPToolParameter(TEXT("lower_terrain"), TEXT("boolean"), TEXT("For 'add_spline': lower terrain along spline (default true)"), false),
		FMCPToolParameter(TEXT("closed_loop"), TEXT("boolean"), TEXT("For 'add_spline': connect last point back to first (default false)"), false),
		// add_hole
		FMCPToolParameter(TEXT("create_hole"), TEXT("boolean"), TEXT("For 'add_hole': true punches a hole, false fills (default true)"), false),
		// assign_rvt
		FMCPToolParameter(TEXT("rvt_path"), TEXT("string"), TEXT("For 'assign_rvt': path to the RuntimeVirtualTexture asset"), false),
		FMCPToolParameter(TEXT("slot_index"), TEXT("number"), TEXT("For 'assign_rvt': slot index in the landscape's RVT array (default 0)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_Landscape::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("create"))        { return OpCreate(Params); }
	if (Operation == TEXT("get_info"))      { return OpGetInfo(Params); }
	if (Operation == TEXT("sculpt"))        { return OpSculpt(Params); }
	if (Operation == TEXT("set_heightmap")) { return OpSetHeightmap(Params); }
	if (Operation == TEXT("paint_layer"))   { return OpPaintLayer(Params); }
	if (Operation == TEXT("add_layer"))     { return OpAddLayer(Params); }
	if (Operation == TEXT("add_spline"))    { return OpAddSpline(Params); }
	if (Operation == TEXT("add_hole"))      { return OpAddHole(Params); }
	if (Operation == TEXT("assign_rvt"))    { return OpAssignRVT(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create, get_info, sculpt, set_heightmap, paint_layer, add_layer, add_spline, add_hole, assign_rvt"), *Operation));
}

FMCPToolResult FMCPTool_Landscape::OpCreate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	const FVector Location = ExtractVectorParam(Params, TEXT("location"), FVector::ZeroVector);
	const FRotator Rotation = ExtractRotatorParam(Params, TEXT("rotation"), FRotator::ZeroRotator);
	const FVector Scale = ExtractScaleParam(Params, TEXT("scale"), FVector(100.0, 100.0, 100.0));

	const int32 SectionsPerComponent = ExtractOptionalNumber<int32>(Params, TEXT("sections_per_component"), 1);
	const int32 QuadsPerSection = ExtractOptionalNumber<int32>(Params, TEXT("quads_per_section"), 63);
	const int32 ComponentCountX = ExtractOptionalNumber<int32>(Params, TEXT("component_count_x"), 8);
	const int32 ComponentCountY = ExtractOptionalNumber<int32>(Params, TEXT("component_count_y"), 8);
	const FString Label = ExtractOptionalString(Params, TEXT("label"));

	const TArray<int32> ValidQuadSizes = { 7, 15, 31, 63, 127, 255 };
	if (!ValidQuadSizes.Contains(QuadsPerSection))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("create: invalid quads_per_section %d (must be 7, 15, 31, 63, 127, or 255)"), QuadsPerSection));
	}
	if (SectionsPerComponent != 1 && SectionsPerComponent != 2)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("create: invalid sections_per_component %d (must be 1 or 2)"), SectionsPerComponent));
	}
	if (ComponentCountX < 1 || ComponentCountY < 1)
	{
		return FMCPToolResult::Error(TEXT("create: component_count_x and component_count_y must be >= 1"));
	}

	const int32 ComponentSizeQuads = QuadsPerSection * SectionsPerComponent;
	const int32 SizeX = ComponentCountX * ComponentSizeQuads + 1;
	const int32 SizeY = ComponentCountY * ComponentSizeQuads + 1;

	// Flat heightmap at mid-height (32768).
	TArray<uint16> HeightData;
	HeightData.Init(32768, SizeX * SizeY);

	// Import() looks up height data using the default/empty FGuid key, not the
	// landscape GUID; the GUID param is only stored via SetLandscapeGuid().
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	const FGuid LandscapeGuid = FGuid::NewGuid();

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "CreateLandscape", "Create Landscape"));

	FActorSpawnParameters SpawnParams;
	ALandscape* NewLandscape = World->SpawnActor<ALandscape>(Location, Rotation, SpawnParams);
	if (!NewLandscape)
	{
		return FMCPToolResult::Error(TEXT("create: failed to spawn ALandscape actor"));
	}

	NewLandscape->Modify();
	NewLandscape->SetActorScale3D(Scale);
	NewLandscape->SetLandscapeGuid(LandscapeGuid);

	TArrayView<const FLandscapeLayer> EmptyEditLayers;
	NewLandscape->Import(
		LandscapeGuid,
		0, 0,
		SizeX - 1, SizeY - 1,
		SectionsPerComponent, QuadsPerSection,
		HeightDataPerLayers,
		nullptr,
		MaterialLayerDataPerLayers,
		ELandscapeImportAlphamapType::Additive,
		EmptyEditLayers);

	if (!Label.IsEmpty())
	{
		NewLandscape->SetActorLabel(Label);
	}

	if (ULandscapeInfo* LandscapeInfo = NewLandscape->GetLandscapeInfo())
	{
		LandscapeInfo->UpdateComponentLayerAllowList();
	}

	NewLandscape->MarkPackageDirty();
	if (ULevel* Level = NewLandscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Data = BuildActorInfoWithTransformJson(NewLandscape);
	Data->SetNumberField(TEXT("resolution_x"), SizeX);
	Data->SetNumberField(TEXT("resolution_y"), SizeY);
	Data->SetNumberField(TEXT("component_count_x"), ComponentCountX);
	Data->SetNumberField(TEXT("component_count_y"), ComponentCountY);
	Data->SetNumberField(TEXT("sections_per_component"), SectionsPerComponent);
	Data->SetNumberField(TEXT("quads_per_section"), QuadsPerSection);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created landscape '%s' (%dx%d verts, %dx%d components)"),
			*NewLandscape->GetActorLabel(), SizeX, SizeY, ComponentCountX, ComponentCountY),
		Data);
#else
	return FMCPToolResult::Error(TEXT("create requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Landscape::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}

	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("get_info: landscape '%s' not found"), *Name));
	}

	TSharedPtr<FJsonObject> Data = BuildActorInfoWithTransformJson(Landscape);

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		Data->SetBoolField(TEXT("has_landscape_info"), false);
		return FMCPToolResult::Success(FString::Printf(TEXT("Landscape '%s' (no ULandscapeInfo registered)"), *Landscape->GetActorLabel()), Data);
	}

	int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	if (LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		Data->SetNumberField(TEXT("resolution_x"), MaxX - MinX + 1);
		Data->SetNumberField(TEXT("resolution_y"), MaxY - MinY + 1);
		Data->SetNumberField(TEXT("min_x"), MinX);
		Data->SetNumberField(TEXT("min_y"), MinY);
		Data->SetNumberField(TEXT("max_x"), MaxX);
		Data->SetNumberField(TEXT("max_y"), MaxY);
	}

	Data->SetNumberField(TEXT("component_size_quads"), LandscapeInfo->ComponentSizeQuads);
	Data->SetNumberField(TEXT("subsection_size_quads"), LandscapeInfo->SubsectionSizeQuads);
	Data->SetNumberField(TEXT("num_subsections"), LandscapeInfo->ComponentNumSubsections);

	FIntRect ComponentBounds;
	if (LandscapeInfo->GetLandscapeXYComponentBounds(ComponentBounds))
	{
		Data->SetNumberField(TEXT("component_count_x"), ComponentBounds.Width() + 1);
		Data->SetNumberField(TEXT("component_count_y"), ComponentBounds.Height() + 1);
	}

	// Paint layers.
	TArray<TSharedPtr<FJsonValue>> LayersJson;
#if WITH_EDITORONLY_DATA
	for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
	{
		TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
		LayerObj->SetStringField(TEXT("name"), LayerSettings.GetLayerName().ToString());
		LayerObj->SetBoolField(TEXT("has_layer_info"), LayerSettings.LayerInfoObj != nullptr);
		if (LayerSettings.LayerInfoObj)
		{
			LayerObj->SetStringField(TEXT("layer_info_path"), LayerSettings.LayerInfoObj->GetPathName());
		}
		LayersJson.Add(MakeShared<FJsonValueObject>(LayerObj));
	}
#endif
	Data->SetArrayField(TEXT("paint_layers"), LayersJson);

	if (UMaterialInterface* Material = Landscape->GetLandscapeMaterial())
	{
		Data->SetStringField(TEXT("material"), Material->GetPathName());
	}

	return FMCPToolResult::Success(FString::Printf(TEXT("Landscape '%s' info"), *Landscape->GetActorLabel()), Data);
}

FMCPToolResult FMCPTool_Landscape::OpSculpt(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}
	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("sculpt: landscape '%s' not found"), *Name));
	}
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return FMCPToolResult::Error(TEXT("sculpt: landscape has no ULandscapeInfo"));
	}

	const double WorldX = ExtractOptionalNumber<double>(Params, TEXT("world_x"), 0.0);
	const double WorldY = ExtractOptionalNumber<double>(Params, TEXT("world_y"), 0.0);
	const double BrushRadius = ExtractOptionalNumber<double>(Params, TEXT("brush_radius"), 1000.0);
	const double Strength = ExtractOptionalNumber<double>(Params, TEXT("strength"), 0.0);
	const FString Falloff = ExtractOptionalString(Params, TEXT("falloff"), TEXT("Linear"));

	if (BrushRadius <= 0.0)
	{
		return FMCPToolResult::Error(TEXT("sculpt: brush_radius must be > 0"));
	}

	// Brush radius is in world units (cm); convert to vertex/quad space via X scale.
	const double QuadCm = Landscape->GetActorScale().X;
	if (FMath::IsNearlyZero(QuadCm))
	{
		return FMCPToolResult::Error(TEXT("sculpt: landscape X scale is zero"));
	}
	const FVector2D Center = WorldToVertex(Landscape, WorldX, WorldY);
	const double RadiusVerts = BrushRadius / QuadCm;
	const int32 PeakDelta = WorldDeltaToHeightUnits(Landscape, Strength);

	int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return FMCPToolResult::Error(TEXT("sculpt: failed to query landscape extent"));
	}

	int32 X1 = FMath::Clamp(FMath::FloorToInt(Center.X - RadiusVerts), MinX, MaxX);
	int32 Y1 = FMath::Clamp(FMath::FloorToInt(Center.Y - RadiusVerts), MinY, MaxY);
	int32 X2 = FMath::Clamp(FMath::CeilToInt(Center.X + RadiusVerts), MinX, MaxX);
	int32 Y2 = FMath::Clamp(FMath::CeilToInt(Center.Y + RadiusVerts), MinY, MaxY);
	if (X2 < X1 || Y2 < Y1)
	{
		return FMCPToolResult::Error(TEXT("sculpt: brush region falls entirely outside the landscape"));
	}

	const int32 RegionW = X2 - X1 + 1;
	const int32 RegionH = Y2 - Y1 + 1;

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "SculptLandscape", "Sculpt Landscape"));
	Landscape->Modify();

	const FGuid EditLayerGuid = ResolveEditLayerGuid(Landscape);
	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo, EditLayerGuid);

	TArray<uint16> Heights;
	Heights.SetNumZeroed(RegionW * RegionH);
	LandscapeEdit.GetHeightDataFast(X1, Y1, X2, Y2, Heights.GetData(), 0);

	for (int32 Y = 0; Y < RegionH; ++Y)
	{
		for (int32 X = 0; X < RegionW; ++X)
		{
			const double VX = (double)(X1 + X);
			const double VY = (double)(Y1 + Y);
			const double Dist = FVector2D::Distance(FVector2D(VX, VY), Center);
			if (Dist > RadiusVerts)
			{
				continue;
			}
			const float Weight = BrushFalloff(Falloff, (float)(Dist / RadiusVerts));
			const int32 Idx = Y * RegionW + X;
			const int32 NewVal = (int32)Heights[Idx] + FMath::RoundToInt(PeakDelta * Weight);
			Heights[Idx] = (uint16)FMath::Clamp(NewVal, 0, 65535);
		}
	}

	LandscapeEdit.SetHeightData(X1, Y1, X2, Y2, Heights.GetData(), 0, /*InCalcNormals*/ true);
	LandscapeEdit.Flush();
	FinalizeLandscapeEdit(Landscape);

	Landscape->MarkPackageDirty();
	if (ULevel* Level = Landscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("region_x1"), X1);
	Data->SetNumberField(TEXT("region_y1"), Y1);
	Data->SetNumberField(TEXT("region_x2"), X2);
	Data->SetNumberField(TEXT("region_y2"), Y2);
	Data->SetNumberField(TEXT("peak_height_units"), PeakDelta);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Sculpted landscape '%s' (region %dx%d verts, peak delta %d units)"),
			*Landscape->GetActorLabel(), RegionW, RegionH, PeakDelta),
		Data);
#else
	return FMCPToolResult::Error(TEXT("sculpt requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Landscape::OpSetHeightmap(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}
	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("set_heightmap: landscape '%s' not found"), *Name));
	}
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return FMCPToolResult::Error(TEXT("set_heightmap: landscape has no ULandscapeInfo"));
	}

	int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return FMCPToolResult::Error(TEXT("set_heightmap: failed to query landscape extent"));
	}
	const int32 FullW = MaxX - MinX + 1;
	const int32 FullH = MaxY - MinY + 1;

	const FString FilePath = ExtractOptionalString(Params, TEXT("file_path"));

	int32 X1, Y1, X2, Y2;
	TArray<uint16> Heights;

	if (!FilePath.IsEmpty())
	{
		// --- File import path: must match the full landscape resolution. ---
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("set_heightmap: could not read file '%s'"), *FilePath));
		}

		const FString Ext = FPaths::GetExtension(FilePath).ToLower();
		if (Ext == TEXT("png"))
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
			{
				return FMCPToolResult::Error(TEXT("set_heightmap: failed to decode PNG heightmap"));
			}
			if ((int32)ImageWrapper->GetWidth() != FullW || (int32)ImageWrapper->GetHeight() != FullH)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("set_heightmap: PNG resolution %lldx%lld does not match landscape %dx%d"),
					(long long)ImageWrapper->GetWidth(), (long long)ImageWrapper->GetHeight(), FullW, FullH));
			}
			if (ImageWrapper->GetBitDepth() != 16 || ImageWrapper->GetFormat() != ERGBFormat::Gray)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("set_heightmap: PNG must be 16-bit grayscale (got bitdepth=%d, format=%d)"),
					ImageWrapper->GetBitDepth(), (int32)ImageWrapper->GetFormat()));
			}
			TArray64<uint8> Raw;
			if (!ImageWrapper->GetRaw(Raw))
			{
				return FMCPToolResult::Error(TEXT("set_heightmap: failed to decode 16-bit grayscale PNG data"));
			}
			const int64 Expected = (int64)FullW * FullH * (int64)sizeof(uint16);
			if (Raw.Num() != Expected)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("set_heightmap: decoded PNG produced %lld bytes, expected %lld"), (long long)Raw.Num(), (long long)Expected));
			}
			Heights.SetNumUninitialized(FullW * FullH);
			FMemory::Memcpy(Heights.GetData(), Raw.GetData(), Expected);
		}
		else if (Ext == TEXT("raw") || Ext == TEXT("r16"))
		{
			const int32 Expected = FullW * FullH * (int32)sizeof(uint16);
			if (FileData.Num() != Expected)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("set_heightmap: raw file is %d bytes but landscape %dx%d needs %d bytes (16-bit)"),
					FileData.Num(), FullW, FullH, Expected));
			}
			Heights.SetNumUninitialized(FullW * FullH);
			FMemory::Memcpy(Heights.GetData(), FileData.GetData(), Expected);
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("set_heightmap: unsupported file extension '.%s' (use .png or .raw/.r16)"), *Ext));
		}

		X1 = MinX; Y1 = MinY; X2 = MaxX; Y2 = MaxY;
	}
	else
	{
		// --- Region 'heights' path. ---
		const int32 StartX = ExtractOptionalNumber<int32>(Params, TEXT("start_x"), 0);
		const int32 StartY = ExtractOptionalNumber<int32>(Params, TEXT("start_y"), 0);
		const int32 SizeXp = ExtractOptionalNumber<int32>(Params, TEXT("size_x"), 0);
		const int32 SizeYp = ExtractOptionalNumber<int32>(Params, TEXT("size_y"), 0);

		const TArray<TSharedPtr<FJsonValue>>* HeightsArr = nullptr;
		if (!Params->TryGetArrayField(TEXT("heights"), HeightsArr) || !HeightsArr)
		{
			return FMCPToolResult::Error(TEXT("set_heightmap: provide either 'file_path' or 'heights' + region (start_x/start_y/size_x/size_y)"));
		}
		if (SizeXp <= 0 || SizeYp <= 0)
		{
			return FMCPToolResult::Error(TEXT("set_heightmap: size_x and size_y must be > 0 when using 'heights'"));
		}
		if (HeightsArr->Num() != SizeXp * SizeYp)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("set_heightmap: 'heights' has %d entries but size_x*size_y = %d"),
				HeightsArr->Num(), SizeXp * SizeYp));
		}

		X1 = MinX + StartX;
		Y1 = MinY + StartY;
		X2 = X1 + SizeXp - 1;
		Y2 = Y1 + SizeYp - 1;
		if (X1 < MinX || Y1 < MinY || X2 > MaxX || Y2 > MaxY)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("set_heightmap: region [%d,%d..%d,%d] exceeds landscape extent [%d,%d..%d,%d]"),
				X1, Y1, X2, Y2, MinX, MinY, MaxX, MaxY));
		}

		// Convert world-unit (cm) float heights to uint16. Height 0cm == mid (32768).
		const double CmPerUnit = (double)LANDSCAPE_ZSCALE * Landscape->GetActorScale().Z;
		if (FMath::IsNearlyZero(CmPerUnit))
		{
			return FMCPToolResult::Error(TEXT("set_heightmap: landscape Z scale is zero"));
		}
		Heights.SetNumUninitialized(SizeXp * SizeYp);
		for (int32 i = 0; i < HeightsArr->Num(); ++i)
		{
			const double HCm = (*HeightsArr)[i]->AsNumber();
			const int32 Val = 32768 + FMath::RoundToInt(HCm / CmPerUnit);
			Heights[i] = (uint16)FMath::Clamp(Val, 0, 65535);
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "SetHeightmap", "Set Landscape Heightmap"));
	Landscape->Modify();

	const FGuid EditLayerGuid = ResolveEditLayerGuid(Landscape);
	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo, EditLayerGuid);
	LandscapeEdit.SetHeightData(X1, Y1, X2, Y2, Heights.GetData(), 0, /*InCalcNormals*/ true);
	LandscapeEdit.Flush();
	FinalizeLandscapeEdit(Landscape);

	Landscape->MarkPackageDirty();
	if (ULevel* Level = Landscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("region_x1"), X1);
	Data->SetNumberField(TEXT("region_y1"), Y1);
	Data->SetNumberField(TEXT("region_x2"), X2);
	Data->SetNumberField(TEXT("region_y2"), Y2);
	Data->SetBoolField(TEXT("from_file"), !FilePath.IsEmpty());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set heightmap for '%s' over region %dx%d"),
			*Landscape->GetActorLabel(), X2 - X1 + 1, Y2 - Y1 + 1),
		Data);
#else
	return FMCPToolResult::Error(TEXT("set_heightmap requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Landscape::OpPaintLayer(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}
	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("paint_layer: landscape '%s' not found"), *Name));
	}
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return FMCPToolResult::Error(TEXT("paint_layer: landscape has no ULandscapeInfo"));
	}

	FString LayerName;
	if (!ExtractRequiredString(Params, TEXT("layer"), LayerName, Err))
	{
		return Err.GetValue();
	}
	ULandscapeLayerInfoObject* LayerInfo = LandscapeInfo->GetLayerInfoByName(FName(*LayerName));
	if (!LayerInfo)
	{
		FString Available;
		for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
		{
			if (!Available.IsEmpty()) { Available += TEXT(", "); }
			Available += LayerSettings.GetLayerName().ToString();
		}
		return FMCPToolResult::Error(FString::Printf(
			TEXT("paint_layer: layer '%s' not found on landscape. Available paint layers: [%s]. Add a new one via add_layer."),
			*LayerName, Available.IsEmpty() ? TEXT("(none)") : *Available));
	}

	const double WorldX = ExtractOptionalNumber<double>(Params, TEXT("world_x"), 0.0);
	const double WorldY = ExtractOptionalNumber<double>(Params, TEXT("world_y"), 0.0);
	const double BrushRadius = ExtractOptionalNumber<double>(Params, TEXT("brush_radius"), 1000.0);
	const double TargetWeight = FMath::Clamp(ExtractOptionalNumber<double>(Params, TEXT("strength"), 1.0), 0.0, 1.0);
	const FString Falloff = ExtractOptionalString(Params, TEXT("falloff"), TEXT("Linear"));

	if (BrushRadius <= 0.0)
	{
		return FMCPToolResult::Error(TEXT("paint_layer: brush_radius must be > 0"));
	}
	const double QuadCm = Landscape->GetActorScale().X;
	if (FMath::IsNearlyZero(QuadCm))
	{
		return FMCPToolResult::Error(TEXT("paint_layer: landscape X scale is zero"));
	}

	const FVector2D Center = WorldToVertex(Landscape, WorldX, WorldY);
	const double RadiusVerts = BrushRadius / QuadCm;

	int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return FMCPToolResult::Error(TEXT("paint_layer: failed to query landscape extent"));
	}
	int32 X1 = FMath::Clamp(FMath::FloorToInt(Center.X - RadiusVerts), MinX, MaxX);
	int32 Y1 = FMath::Clamp(FMath::FloorToInt(Center.Y - RadiusVerts), MinY, MaxY);
	int32 X2 = FMath::Clamp(FMath::CeilToInt(Center.X + RadiusVerts), MinX, MaxX);
	int32 Y2 = FMath::Clamp(FMath::CeilToInt(Center.Y + RadiusVerts), MinY, MaxY);
	if (X2 < X1 || Y2 < Y1)
	{
		return FMCPToolResult::Error(TEXT("paint_layer: brush region falls outside the landscape"));
	}
	const int32 RegionW = X2 - X1 + 1;
	const int32 RegionH = Y2 - Y1 + 1;

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "PaintLayer", "Paint Landscape Layer"));
	Landscape->Modify();

	const FGuid EditLayerGuid = ResolveEditLayerGuid(Landscape);
	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo, EditLayerGuid);

	TArray<uint8> Weights;
	Weights.SetNumZeroed(RegionW * RegionH);
	// GetWeightData rewrites its X1/Y1/X2/Y2 args by reference (to the valid-data sub-extent). Pass
	// scratch copies so the originals keep describing the full brush region — the buffer layout
	// (RegionW), the loop's vertex math, and the SetAlphaData write-back all depend on them.
	int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
	LandscapeEdit.GetWeightData(LayerInfo, GX1, GY1, GX2, GY2, Weights.GetData(), 0);

	for (int32 Y = 0; Y < RegionH; ++Y)
	{
		for (int32 X = 0; X < RegionW; ++X)
		{
			const double VX = (double)(X1 + X);
			const double VY = (double)(Y1 + Y);
			const double Dist = FVector2D::Distance(FVector2D(VX, VY), Center);
			if (Dist > RadiusVerts)
			{
				continue;
			}
			const float FalloffW = BrushFalloff(Falloff, (float)(Dist / RadiusVerts));
			const int32 Idx = Y * RegionW + X;
			const float Existing = (float)Weights[Idx] / 255.0f;
			const float Painted = FMath::Lerp(Existing, (float)TargetWeight, FalloffW);
			Weights[Idx] = (uint8)FMath::Clamp(FMath::RoundToInt(Painted * 255.0f), 0, 255);
		}
	}

	// Write the painted weights for this layer. (The bWeightAdjust/bTotalWeightAdjust overload that
	// auto-renormalized sibling layers was removed in UE5.7; this is the supported single-layer form.)
	LandscapeEdit.SetAlphaData(LayerInfo, X1, Y1, X2, Y2, Weights.GetData(), 0,
		ELandscapeLayerPaintingRestriction::None);
	LandscapeEdit.Flush();
	Landscape->RequestLayersContentUpdateForceAll();

	Landscape->MarkPackageDirty();
	if (ULevel* Level = Landscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("layer"), LayerName);
	Data->SetNumberField(TEXT("region_x1"), X1);
	Data->SetNumberField(TEXT("region_y1"), Y1);
	Data->SetNumberField(TEXT("region_x2"), X2);
	Data->SetNumberField(TEXT("region_y2"), Y2);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Painted layer '%s' on '%s' (region %dx%d)"),
			*LayerName, *Landscape->GetActorLabel(), RegionW, RegionH),
		Data);
#else
	return FMCPToolResult::Error(TEXT("paint_layer requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Landscape::OpAddLayer(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}
	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_layer: landscape '%s' not found"), *Name));
	}
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return FMCPToolResult::Error(TEXT("add_layer: landscape has no ULandscapeInfo"));
	}

	FString LayerInfoPath;
	if (!ExtractRequiredString(Params, TEXT("layer_info_path"), LayerInfoPath, Err))
	{
		return Err.GetValue();
	}
	UObject* Loaded = UEditorAssetLibrary::LoadAsset(LayerInfoPath);
	ULandscapeLayerInfoObject* LayerInfo = Cast<ULandscapeLayerInfoObject>(Loaded);
	if (!LayerInfo)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("add_layer: could not load ULandscapeLayerInfoObject at '%s'"), *LayerInfoPath));
	}

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "AddLayer", "Add Landscape Layer"));
	Landscape->Modify();
	LayerInfo->Modify();

	// Associate the layer info with the landscape's target layer of the same name.
	// CreateTargetLayerSettingsFor registers the layer in ULandscapeInfo->Layers.
	LandscapeInfo->CreateTargetLayerSettingsFor(LayerInfo);
	LandscapeInfo->UpdateLayerInfoMap();

	Landscape->MarkPackageDirty();
	if (ULevel* Level = Landscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}
	UEditorAssetLibrary::SaveAsset(LayerInfoPath, /*bOnlyIfIsDirty*/ false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("layer"), LayerInfo->GetLayerName().ToString());
	Data->SetStringField(TEXT("layer_info_path"), LayerInfo->GetPathName());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added paint layer '%s' to '%s'"),
			*LayerInfo->GetLayerName().ToString(), *Landscape->GetActorLabel()),
		Data);
#else
	return FMCPToolResult::Error(TEXT("add_layer requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Landscape::OpAddSpline(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}
	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_spline: landscape '%s' not found"), *Name));
	}

	const TArray<TSharedPtr<FJsonValue>>* PointsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("points"), PointsArr) || !PointsArr || PointsArr->Num() < 2)
	{
		return FMCPToolResult::Error(TEXT("add_spline: 'points' must be an array of at least 2 world locations"));
	}

	const double HalfWidth = ExtractOptionalNumber<double>(Params, TEXT("width"), 500.0);
	const double SideFalloff = ExtractOptionalNumber<double>(Params, TEXT("side_falloff"), 500.0);
	const double EndFalloff = ExtractOptionalNumber<double>(Params, TEXT("end_falloff"), 500.0);
	const bool bRaise = ExtractOptionalBool(Params, TEXT("raise_terrain"), true);
	const bool bLower = ExtractOptionalBool(Params, TEXT("lower_terrain"), true);
	const bool bClosed = ExtractOptionalBool(Params, TEXT("closed_loop"), false);
	const FString PaintLayerName = ExtractOptionalString(Params, TEXT("paint_layer"));

	FName PaintLayerFName = NAME_None;
	if (!PaintLayerName.IsEmpty())
	{
		PaintLayerFName = FName(*PaintLayerName);
	}

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "AddSpline", "Add Landscape Spline"));
	Landscape->Modify();

	// Ensure the landscape has a splines component.
	if (!Landscape->GetSplinesComponent())
	{
		Landscape->CreateSplineComponent();
	}
	ULandscapeSplinesComponent* Splines = Landscape->GetSplinesComponent();
	if (!Splines)
	{
		return FMCPToolResult::Error(TEXT("add_spline: failed to create ULandscapeSplinesComponent"));
	}
	Splines->Modify();

	const FTransform ActorXform = Landscape->GetActorTransform();

	// Control points store Location in landscape-local space.
	TArray<ULandscapeSplineControlPoint*> NewPoints;
	NewPoints.Reserve(PointsArr->Num());
	for (const TSharedPtr<FJsonValue>& PV : *PointsArr)
	{
		const TSharedPtr<FJsonObject>* PObj = nullptr;
		if (!PV->TryGetObject(PObj) || !PObj || !(*PObj).IsValid())
		{
			return FMCPToolResult::Error(TEXT("add_spline: each 'points' entry must be an object {x,y,z}"));
		}
		const double WX = (*PObj)->GetNumberField(TEXT("x"));
		const double WY = (*PObj)->GetNumberField(TEXT("y"));
		const double WZ = (*PObj)->HasField(TEXT("z")) ? (*PObj)->GetNumberField(TEXT("z")) : 0.0;
		const FVector LocalLoc = ActorXform.InverseTransformPosition(FVector(WX, WY, WZ));

		ULandscapeSplineControlPoint* CP = NewObject<ULandscapeSplineControlPoint>(Splines, NAME_None, RF_Transactional);
		CP->Location = LocalLoc;
		CP->Width = HalfWidth;
		CP->SideFalloff = SideFalloff;
		CP->EndFalloff = EndFalloff;
		CP->LayerName = PaintLayerFName;
		CP->bRaiseTerrain = bRaise;
		CP->bLowerTerrain = bLower;
		Splines->GetControlPoints().Add(CP);
		NewPoints.Add(CP);
	}

	// Connect consecutive control points with segments.
	auto MakeSegment = [&](ULandscapeSplineControlPoint* A, ULandscapeSplineControlPoint* B)
	{
		ULandscapeSplineSegment* Seg = NewObject<ULandscapeSplineSegment>(Splines, NAME_None, RF_Transactional);
		Seg->Connections[0].ControlPoint = A;
		Seg->Connections[1].ControlPoint = B;
		Seg->LayerName = PaintLayerFName;
		Splines->GetSegments().Add(Seg);
		A->ConnectedSegments.Add(FLandscapeSplineConnection(Seg, 0));
		B->ConnectedSegments.Add(FLandscapeSplineConnection(Seg, 1));
		Seg->AutoFlipTangents();
	};

	for (int32 i = 0; i < NewPoints.Num() - 1; ++i)
	{
		MakeSegment(NewPoints[i], NewPoints[i + 1]);
	}
	if (bClosed && NewPoints.Num() > 2)
	{
		MakeSegment(NewPoints.Last(), NewPoints[0]);
	}

	// Update control points (auto-sets tangents/sockets) then segments (deforms terrain).
	for (ULandscapeSplineControlPoint* CP : NewPoints)
	{
		CP->AutoCalcRotation(/*bAlwaysRotateForward*/ false);
		CP->UpdateSplinePoints();
	}
	for (ULandscapeSplineSegment* Seg : Splines->GetSegments())
	{
		Seg->UpdateSplinePoints();
	}

	Landscape->RequestLayersContentUpdateForceAll();
	Landscape->MarkPackageDirty();
	if (ULevel* Level = Landscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("control_points"), NewPoints.Num());
	Data->SetNumberField(TEXT("segments"), Splines->GetSegments().Num());
	Data->SetBoolField(TEXT("closed_loop"), bClosed);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added spline to '%s' (%d control points)"),
			*Landscape->GetActorLabel(), NewPoints.Num()),
		Data);
#else
	return FMCPToolResult::Error(TEXT("add_spline requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Landscape::OpAddHole(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}
	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_hole: landscape '%s' not found"), *Name));
	}
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return FMCPToolResult::Error(TEXT("add_hole: landscape has no ULandscapeInfo"));
	}

	ULandscapeLayerInfoObject* VisibilityLayer = ALandscapeProxy::VisibilityLayer;
	if (!VisibilityLayer)
	{
		return FMCPToolResult::Error(TEXT("add_hole: ALandscapeProxy::VisibilityLayer is unavailable"));
	}

	const double WorldX = ExtractOptionalNumber<double>(Params, TEXT("world_x"), 0.0);
	const double WorldY = ExtractOptionalNumber<double>(Params, TEXT("world_y"), 0.0);
	const double BrushRadius = ExtractOptionalNumber<double>(Params, TEXT("brush_radius"), 500.0);
	const bool bCreateHole = ExtractOptionalBool(Params, TEXT("create_hole"), true);

	if (BrushRadius <= 0.0)
	{
		return FMCPToolResult::Error(TEXT("add_hole: brush_radius must be > 0"));
	}
	const double QuadCm = Landscape->GetActorScale().X;
	if (FMath::IsNearlyZero(QuadCm))
	{
		return FMCPToolResult::Error(TEXT("add_hole: landscape X scale is zero"));
	}
	const FVector2D Center = WorldToVertex(Landscape, WorldX, WorldY);
	const double RadiusVerts = BrushRadius / QuadCm;

	int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return FMCPToolResult::Error(TEXT("add_hole: failed to query landscape extent"));
	}
	int32 X1 = FMath::Clamp(FMath::FloorToInt(Center.X - RadiusVerts), MinX, MaxX);
	int32 Y1 = FMath::Clamp(FMath::FloorToInt(Center.Y - RadiusVerts), MinY, MaxY);
	int32 X2 = FMath::Clamp(FMath::CeilToInt(Center.X + RadiusVerts), MinX, MaxX);
	int32 Y2 = FMath::Clamp(FMath::CeilToInt(Center.Y + RadiusVerts), MinY, MaxY);
	if (X2 < X1 || Y2 < Y1)
	{
		return FMCPToolResult::Error(TEXT("add_hole: brush region falls outside the landscape"));
	}
	const int32 RegionW = X2 - X1 + 1;
	const int32 RegionH = Y2 - Y1 + 1;

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "AddHole", "Edit Landscape Hole"));
	Landscape->Modify();

	const FGuid EditLayerGuid = ResolveEditLayerGuid(Landscape);
	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo, EditLayerGuid);

	// Visibility weightmap: 255 = hole (not visible), 0 = solid. Read-modify-write.
	// GetWeightData rewrites its X1/Y1/X2/Y2 args by reference; pass scratch copies so the originals
	// keep describing the full brush region for the buffer layout, loop, and SetAlphaData write-back.
	TArray<uint8> Vis;
	Vis.SetNumZeroed(RegionW * RegionH);
	int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
	LandscapeEdit.GetWeightData(VisibilityLayer, GX1, GY1, GX2, GY2, Vis.GetData(), 0);

	const uint8 FillValue = bCreateHole ? 255 : 0;
	for (int32 Y = 0; Y < RegionH; ++Y)
	{
		for (int32 X = 0; X < RegionW; ++X)
		{
			const double VX = (double)(X1 + X);
			const double VY = (double)(Y1 + Y);
			const double Dist = FVector2D::Distance(FVector2D(VX, VY), Center);
			if (Dist > RadiusVerts)
			{
				continue;
			}
			Vis[Y * RegionW + X] = FillValue;
		}
	}

	LandscapeEdit.SetAlphaData(VisibilityLayer, X1, Y1, X2, Y2, Vis.GetData(), 0, ELandscapeLayerPaintingRestriction::None);
	LandscapeEdit.Flush();
	FinalizeLandscapeEdit(Landscape);

	Landscape->MarkPackageDirty();
	if (ULevel* Level = Landscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("create_hole"), bCreateHole);
	Data->SetNumberField(TEXT("region_x1"), X1);
	Data->SetNumberField(TEXT("region_y1"), Y1);
	Data->SetNumberField(TEXT("region_x2"), X2);
	Data->SetNumberField(TEXT("region_y2"), Y2);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("%s hole on '%s' (region %dx%d)"),
			bCreateHole ? TEXT("Punched") : TEXT("Filled"), *Landscape->GetActorLabel(), RegionW, RegionH),
		Data);
#else
	return FMCPToolResult::Error(TEXT("add_hole requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Landscape::OpAssignRVT(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> Ctx = ValidateEditorContext(World))
	{
		return Ctx.GetValue();
	}

	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Name, Err))
	{
		return Err.GetValue();
	}
	ALandscape* Landscape = ResolveLandscape(World, Name);
	if (!Landscape)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("assign_rvt: landscape '%s' not found"), *Name));
	}

	FString RvtPath;
	if (!ExtractRequiredString(Params, TEXT("rvt_path"), RvtPath, Err))
	{
		return Err.GetValue();
	}
	UObject* Loaded = UEditorAssetLibrary::LoadAsset(RvtPath);
	URuntimeVirtualTexture* RVT = Cast<URuntimeVirtualTexture>(Loaded);
	if (!RVT)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("assign_rvt: could not load URuntimeVirtualTexture at '%s'"), *RvtPath));
	}

	const int32 SlotIndex = ExtractOptionalNumber<int32>(Params, TEXT("slot_index"), 0);
	if (SlotIndex < 0)
	{
		return FMCPToolResult::Error(TEXT("assign_rvt: slot_index must be >= 0"));
	}

	FScopedTransaction Transaction(NSLOCTEXT("UnrealClaude", "AssignRVT", "Assign Landscape RVT"));
	Landscape->Modify();

	// Grow the array to fit the requested slot.
	if (Landscape->RuntimeVirtualTextures.Num() <= SlotIndex)
	{
		Landscape->RuntimeVirtualTextures.SetNum(SlotIndex + 1);
	}
	Landscape->RuntimeVirtualTextures[SlotIndex] = RVT;

	// Match the editor's default RVT setup: write to the RVT and still draw the main pass.
	Landscape->bUseDynamicMaterialInstance = true;
	Landscape->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;

	// Apply changes via PostEditChange so the render state / material rebuilds.
	Landscape->PostEditChange();

	Landscape->MarkPackageDirty();
	if (ULevel* Level = Landscape->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("rvt_path"), RVT->GetPathName());
	Data->SetNumberField(TEXT("slot_index"), SlotIndex);
	Data->SetNumberField(TEXT("rvt_count"), Landscape->RuntimeVirtualTextures.Num());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Assigned RVT '%s' to '%s' at slot %d"),
			*RVT->GetName(), *Landscape->GetActorLabel(), SlotIndex),
		Data);
#else
	return FMCPToolResult::Error(TEXT("assign_rvt requires an editor build"));
#endif
}
