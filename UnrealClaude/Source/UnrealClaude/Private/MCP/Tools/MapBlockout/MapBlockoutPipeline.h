// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.
//
// The MapBlockout pipeline as plain static functions (faithful to VibeUE's UMapBlockoutService statics,
// minus the UFUNCTION/UCLASS machinery). Implemented across MapBlockout_Grid.cpp / _Stages.cpp /
// _Materialize.cpp / _Render.cpp. The native MCP tool (MCPTool_MapBlockout) drives these + JSON.

#pragma once

#include "CoreMinimal.h"
#include "MapBlockoutTypes.h"

namespace MapBlockoutPipeline
{
	// ---- Stage 0: landcover grid + rivers (MapBlockout_Grid.cpp) ----
	FMapBlockoutLandcoverGrid ExportLandcoverGrid(const FString& LandscapeLabel, int32 GridN = 120, bool bSynthesizeFloodFromHeight = false, float FloodPercentile = 8.0f);
	FString WriteLandcoverGridJson(const FMapBlockoutLandcoverGrid& Grid, const FString& OutputFilePath);
	FMapBlockoutLandcoverGrid LoadLandcoverGridJson(const FString& FilePath);
	TArray<FMapBlockoutRiver> ExtractRiverCenterlines(const FMapBlockoutMask& WaterMask, float WorldLo, float WorldHi, float MinLengthCm = 50000.0f);

	// ---- Stages 1-5 + final pass (MapBlockout_Stages.cpp) ----
	FMapBlockoutRoadNetworkResult GenerateRoads(const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutConfig& Config);
	FMapBlockoutPOIResult PlacePois(const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutRoadNetworkResult& Roads, const FMapBlockoutConfig& Config);
	FMapBlockoutFieldResult PlaceFields(const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutRoadNetworkResult& Roads, const FMapBlockoutPOIResult& Pois, const FMapBlockoutConfig& Config);
	FMapBlockoutFoliageResult PlaceFoliage(const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutRoadNetworkResult& Roads, const FMapBlockoutPOIResult& Pois, const FMapBlockoutFieldResult& Fields, const FMapBlockoutConfig& Config);
	FMapBlockoutRailwayResult PlaceRailway(const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutRoadNetworkResult& Roads, const FMapBlockoutPOIResult& Pois, const FMapBlockoutFieldResult& Fields, const FMapBlockoutFoliageResult& Foliage, const FMapBlockoutConfig& Config);
	FMapBlockoutGateResult RunFinalPass(const FMapBlockoutState& State);

	// ---- Rendering (MapBlockout_Render.cpp) ----
	FString RenderStageSnapshot(int32 Stage, const FMapBlockoutState& State, const FString& OutputDir);
	TArray<FString> RenderFinalDeliverables(const FMapBlockoutState& State, const FString& OutputDir);

	// ---- Orchestrator (MapBlockout_Render.cpp) ----
	FMapBlockoutPipelineResult RunFullPipeline(const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutConfig& Config);
	FMapBlockoutPipelineResult RunFullPipelineForLandscape(const FString& LandscapeLabel, const FMapBlockoutConfig& Config);

	// ---- Materialization (MapBlockout_Materialize.cpp) ----
	FMapBlockoutMaterializeResult MaterializeRoadsAsSplines(const FMapBlockoutRoadNetworkResult& Roads, const FString& LandscapeLabel);
	FMapBlockoutMaterializeResult MaterializeFieldsAsPaint(const FMapBlockoutFieldResult& Fields, const FString& LandscapeLabel, const FString& LayerName = TEXT("Crop"));
	FMapBlockoutMaterializeResult MaterializePoisAsActors(const FMapBlockoutPOIResult& Pois, const FString& FolderPath, const FString& BuildingMeshPath = TEXT(""));
	// WorldLo/WorldHi are the play-area bounds (from the landcover grid) the masks map onto — needed so
	// foliage scatters at the correct world XY for the actual landscape size (not a hardcoded span).
	FMapBlockoutMaterializeResult MaterializeForestAsFoliage(const FMapBlockoutFoliageResult& Foliage, float WorldLo, float WorldHi, const FString& ForestFoliageTypePath, const FString& TreelineFoliageTypePath, const FString& ScrubFoliageTypePath);
	FMapBlockoutMaterializeResult MaterializeRailwayAndBridges(const FMapBlockoutRailwayResult& Railway, const FString& LandscapeLabel, const FString& BridgeMeshPath = TEXT(""));
}
