// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.
//
// Procedural FPS-map blockout types, ported from VibeUE's UMapBlockoutService.h. Kept as USTRUCTs so the
// native MCP tool can serialize config/state/results to JSON via FJsonObjectConverter. The VIBEUE_API
// UCLASS service is NOT ported here — the pipeline is plain static functions (see MapBlockoutPipeline.h).

#pragma once

#include "CoreMinimal.h"
#include "MapBlockoutTypes.generated.h"

// =========================================================================
// Stage Validation
// =========================================================================

/** Single named check inside a stage gate. */
USTRUCT(BlueprintType)
struct FMapBlockoutCheckResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Name;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bPassed = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Message;
};

/** Aggregate gate result for one stage (Stage 1..5, Stage 0, Final Pass). */
USTRUCT(BlueprintType)
struct FMapBlockoutGateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 Stage = 0;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutCheckResult> Checks;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bAllPassed = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 FailedCount = 0;
};

// =========================================================================
// Landcover Grid (Stage 0 output)
// =========================================================================

/** A single named weight layer downsampled to an NxN grid (row 0 = south). */
USTRUCT(BlueprintType)
struct FMapBlockoutLayerMap
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString LayerName;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 GridN = 0;

	/** Row-major weights in [0,1]. Length = GridN * GridN. Row 0 = south edge of map. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<float> Weights;
};

/** Result of Stage 0: a square, origin-centred sample of every paint layer + world bounds. */
USTRUCT(BlueprintType)
struct FMapBlockoutLandcoverGrid
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString LandscapeLabel;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 GridN = 120;

	/** World min (Unreal units). The world is treated as square and origin-centred. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float WorldLo = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float WorldHi = 0.0f;

	/** Heightmap downsampled to GridN x GridN, normalized 0..1. Empty if export failed. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<float> HeightNormalized;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutLayerMap> Layers;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};

// =========================================================================
// River Centerlines (optional Stage 0 input for crisp rivers)
// =========================================================================

USTRUCT(BlueprintType)
struct FMapBlockoutRiverPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FVector2D World = FVector2D::ZeroVector;

	/** Width in meters at this point (default 30). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float WidthM = 30.0f;
};

USTRUCT(BlueprintType)
struct FMapBlockoutRiver
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Name;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutRiverPoint> Points;
};

// =========================================================================
// Configuration
// =========================================================================

USTRUCT(BlueprintType)
struct FMapBlockoutRoadConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 MainVerticals = 3;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 MainHorizontals = 2;

	/** Dirt-road grid pitch (km). Smaller = denser network = more intersections / more POIs. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float DirtSpacingKm = 1.7f;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 Diagonals = 3;
};

USTRUCT(BlueprintType)
struct FMapBlockoutPOIConfig
{
	GENERATED_BODY()

	/** Target POI count for a 12 km reference map. 0 = auto-scale by area. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 TargetCount = 16;

	/** Minimum POI spacing as a fraction of map span. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float MinSpacingFrac = 0.085f;
};

/** Maps the contributor's design categories to source-landscape paint-layer names. */
USTRUCT(BlueprintType)
struct FMapBlockoutLayerKeyMap
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Crop;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Soil;

	/** Water / wet layer. May be empty — water can be synthesized from heightmap percentile. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Flood;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Forest;
};

USTRUCT(BlueprintType)
struct FMapBlockoutConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString LevelName;

	/** Empty = <Project>/Saved/MapBlockout/<LevelName>/. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString OutputDir;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutLayerKeyMap Layers;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 Seed = 7;

	/** Required field coverage band (% of play area). MapDesigner default 50..60. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FVector2D FieldCoverageBand = FVector2D(50.0f, 60.0f);

	/** Required tree coverage band (% of play area). MapDesigner default 30..40. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FVector2D TreeCoverageBand = FVector2D(30.0f, 40.0f);

	/** Min crop-layer weight for a cell to be field-eligible. Lower = more fields. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float FieldCropThreshold = 0.12f;

	/** Width of the young-growth treeline ring around each wood (cells). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 ForestFringeIters = 9;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutRoadConfig Road;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutPOIConfig Pois;

	/** Optional precise river polylines (overrides flood-layer-derived water). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutRiver> Rivers;
};

// =========================================================================
// Stage 1 — Roads
// =========================================================================

UENUM(BlueprintType)
enum class EMapBlockoutRoadType : uint8
{
	Main UMETA(DisplayName = "Main Road"),
	Dirt UMETA(DisplayName = "Dirt Road"),
	Railway UMETA(DisplayName = "Railway"),
};

USTRUCT(BlueprintType)
struct FMapBlockoutRoad
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	EMapBlockoutRoadType Type = EMapBlockoutRoadType::Dirt;

	/** Ordered world-XY points (Unreal units). Treat as a polyline. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FVector2D> Points;

	/** Road half-width (Unreal units) for materialization as a landscape spline. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float WidthCm = 500.0f;
};

USTRUCT(BlueprintType)
struct FMapBlockoutRoadNetworkResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutRoad> Roads;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutGateResult Gate;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};

// =========================================================================
// Stage 2 — POIs
// =========================================================================

UENUM(BlueprintType)
enum class EMapBlockoutPOIType : uint8
{
	Town UMETA(DisplayName = "Town"),
	Village UMETA(DisplayName = "Village"),
	Farmstead UMETA(DisplayName = "Farmstead"),
	Crossroads UMETA(DisplayName = "Crossroads Hamlet"),
	Industrial UMETA(DisplayName = "Industrial"),
	Strongpoint UMETA(DisplayName = "Forest-Surrounded Strongpoint"),
};

USTRUCT(BlueprintType)
struct FMapBlockoutBuilding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FVector2D World = FVector2D::ZeroVector;

	/** Footprint half-extents (Unreal units). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FVector2D HalfExtents = FVector2D(400.0f, 400.0f);

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float YawDegrees = 0.0f;
};

USTRUCT(BlueprintType)
struct FMapBlockoutPOI
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString Name;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	EMapBlockoutPOIType Type = EMapBlockoutPOIType::Village;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FVector2D Center = FVector2D::ZeroVector;

	/** POI zone radius (Unreal units). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float RadiusCm = 5000.0f;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutBuilding> Buildings;
};

USTRUCT(BlueprintType)
struct FMapBlockoutPOIResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutPOI> Pois;

	/** Any service roads added by this stage (appended to the Stage 1 network). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutRoad> AddedServiceRoads;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutGateResult Gate;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};

// =========================================================================
// Stage 3 — Fields
// =========================================================================

/** A binary mask sampled at MaskW x MaskH cells over the play area. Row 0 = north edge. */
USTRUCT(BlueprintType)
struct FMapBlockoutMask
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 Width = 0;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 Height = 0;

	/** Row-major, 0 = off, 1 = on. Length = Width * Height. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<uint8> Cells;
};

USTRUCT(BlueprintType)
struct FMapBlockoutFieldResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutMask FieldMask;

	/** Coverage as a fraction of total play area (0..1). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float CoverageFraction = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutGateResult Gate;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};

// =========================================================================
// Stage 4 — Foliage (Trees, Treelines, Underbrush)
// =========================================================================

USTRUCT(BlueprintType)
struct FMapBlockoutFoliageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutMask ForestMask;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutMask TreelineMask;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutMask ScrubMask;

	/** Combined tree+forest coverage as a fraction of total play area (0..1). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float TreeCoverageFraction = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutGateResult Gate;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};

// =========================================================================
// Stage 5 — Railway and Bridges
// =========================================================================

USTRUCT(BlueprintType)
struct FMapBlockoutBridge
{
	GENERATED_BODY()

	/** Bridge midpoint (Unreal units). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FVector2D World = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float LengthCm = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	float YawDegrees = 0.0f;

	/** Which infrastructure crosses the bridge. */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	EMapBlockoutRoadType Carries = EMapBlockoutRoadType::Main;
};

USTRUCT(BlueprintType)
struct FMapBlockoutRailwayResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	/** Railway polylines (Type = Railway). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutRoad> RailLines;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FMapBlockoutBridge> Bridges;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutGateResult Gate;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};

// =========================================================================
// Accumulated Blockout State + Final Pipeline Result
// =========================================================================

/** Carries every prior stage's output forward (snapshots are never overwritten). */
USTRUCT(BlueprintType)
struct FMapBlockoutState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutConfig Config;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutLandcoverGrid Grid;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutRoadNetworkResult Stage1Roads;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutPOIResult Stage2Pois;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutFieldResult Stage3Fields;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutFoliageResult Stage4Foliage;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutRailwayResult Stage5Railway;
};

USTRUCT(BlueprintType)
struct FMapBlockoutPipelineResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutState FinalState;

	/** Final-pass gate (re-validates every prior stage). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FMapBlockoutGateResult FinalGate;

	/** Absolute paths of every PNG/JSON written (8 files on success). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	TArray<FString> OutputFiles;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString OutputDir;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};

// =========================================================================
// Materialization Results
// =========================================================================

USTRUCT(BlueprintType)
struct FMapBlockoutMaterializeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	bool bSuccess = false;

	/** How many primitives were created (splines, actors, painted cells, foliage instances). */
	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	int32 CreatedCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "MapBlockout")
	FString ErrorMessage;
};
