// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.
//
// MapBlockout PNG snapshot/heatmap renderers + the full-pipeline orchestrator. Faithful port of the
// render functions (RenderStageSnapshot, RenderFinalDeliverables) and orchestrators (RunFullPipeline,
// RunFullPipelineForLandscape) from VibeUE's UMapBlockoutService_Stages.cpp. All drawing/PNG goes
// through MapBlockoutImage::; the stage generators are called via MapBlockoutPipeline::.
//
// The render code depends on a small set of file-local helpers (FStageCtx + world<->pixel mapping +
// rasterizers) that, in VibeUE, are shared with the Stage generators inside one translation unit's
// anonymous namespace. Our pipeline is split across multiple .cpp files, so those file-local helpers
// (which cannot cross translation units) are replicated here verbatim in this file's own anonymous
// namespace. The Stage generators themselves live in MapBlockout_Stages.cpp and are not redefined here.

#include "MapBlockoutPipeline.h"
#include "MapBlockoutTypes.h"
#include "MapBlockoutMath.h"
#include "MapBlockoutImage.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	// =====================================================================
	// Shared constants + helpers (mirror the Python globals).
	// Replicated from MapBlockout_Stages.cpp's anonymous namespace because
	// anonymous-namespace symbols are file-local and cannot be shared across
	// translation units. Keep byte-for-byte identical to the Stages copy.
	// =====================================================================

	constexpr int32 WORK_W = 1600;
	constexpr int32 WORK_H = 1600;
	constexpr int32 CANVAS_LEFT = 130;
	constexpr int32 CANVAS_TOP = 104;
	constexpr int32 CANVAS_RPANEL = 520;
	constexpr int32 CANVAS_BOT = 46;
	constexpr int32 CANVAS_W = CANVAS_LEFT + WORK_W + CANVAS_RPANEL;
	constexpr int32 CANVAS_H = CANVAS_TOP + WORK_H + CANVAS_BOT;
	constexpr float KM = 100000.0f;

	struct FStageCtx
	{
		// World bounds (square, centred). Span = WorldHi - WorldLo.
		float WorldLo = 0.0f;
		float WorldHi = 0.0f;
		float Span = 1.0f;
		float MapKm = 0.0f;

		// Per-cell weight maps, all up-sampled to WORK_W x WORK_H, row 0 = north.
		TArray<float> Crop;
		TArray<float> Soil;
		TArray<float> Flood;
		TArray<float> Forest;

		// Binary water mask: rivers (precise) OR flood-derived. Dilated buffer.
		TArray<uint8> Water;
		TArray<uint8> WaterBuf;
	};

	// World <-> pixel mapping for the WORK grid (1600x1600, row 0 = north).
	FORCEINLINE int32 W2Col(float X, const FStageCtx& C) {
		return FMath::Clamp(FMath::RoundToInt((X - C.WorldLo) / C.Span * (WORK_W - 1)), 0, WORK_W - 1);
	}
	FORCEINLINE int32 W2Row(float Y, const FStageCtx& C) {
		return FMath::Clamp(FMath::RoundToInt((C.WorldHi - Y) / C.Span * (WORK_H - 1)), 0, WORK_H - 1);
	}
	FORCEINLINE int32 W2PixLen(float WorldUnits, const FStageCtx& C) {
		return FMath::Max(0, FMath::RoundToInt(WorldUnits * WORK_W / C.Span));
	}
	FORCEINLINE FIntPoint W2P(const FVector2D& WorldXY, const FStageCtx& C) {
		return FIntPoint(W2Col(WorldXY.X, C), W2Row(WorldXY.Y, C));
	}
	FORCEINLINE float Dist(const FVector2D& A, const FVector2D& B) {
		return FMath::Sqrt((A.X - B.X) * (A.X - B.X) + (A.Y - B.Y) * (A.Y - B.Y));
	}

	// =====================================================================
	// Context prep (Stage 0 grid -> work-resolution weight maps + water mask)
	// =====================================================================

	void LayerByName(const FMapBlockoutLandcoverGrid& Grid, const FString& Name,
		TArray<float>& OutFull)
	{
		OutFull.Reset();
		if (Name.IsEmpty()) { return; }
		for (const FMapBlockoutLayerMap& L : Grid.Layers)
		{
			if (L.LayerName.Equals(Name, ESearchCase::IgnoreCase))
			{
				// Grid weights: row 0 = south. We want row 0 = north for rendering.
				TArray<float> Flipped = L.Weights;
				MapBlockoutMath::FlipVertical(Flipped, Grid.GridN, Grid.GridN);
				MapBlockoutMath::ResampleBilinear(Flipped, Grid.GridN, Grid.GridN,
					OutFull, WORK_W, WORK_H);
				return;
			}
		}
	}

	void PrepareCtx(const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutConfig& Config,
		FStageCtx& C)
	{
		C.WorldLo = Grid.WorldLo;
		C.WorldHi = Grid.WorldHi;
		C.Span = Grid.WorldHi - Grid.WorldLo;
		C.MapKm = C.Span / KM;

		LayerByName(Grid, Config.Layers.Crop, C.Crop);
		LayerByName(Grid, Config.Layers.Soil, C.Soil);
		LayerByName(Grid, Config.Layers.Flood, C.Flood);
		LayerByName(Grid, Config.Layers.Forest, C.Forest);

		const int32 N = WORK_W * WORK_H;
		if (C.Crop.Num() != N)   { C.Crop.SetNumZeroed(N); }
		if (C.Soil.Num() != N)   { C.Soil.SetNumZeroed(N); }
		if (C.Flood.Num() != N)  { C.Flood.SetNumZeroed(N); }
		if (C.Forest.Num() != N) { C.Forest.SetNumZeroed(N); }

		// Forest gets a light blur for organic edges.
		MapBlockoutMath::GaussianBlur(C.Forest, WORK_W, WORK_H, 3.0f);

		// Water mask: prefer Config.Rivers (precise polylines), else threshold the flood layer.
		C.Water.SetNumZeroed(N);
		if (Config.Rivers.Num() > 0)
		{
			for (const FMapBlockoutRiver& R : Config.Rivers)
			{
				for (int32 I = 0; I + 1 < R.Points.Num(); ++I)
				{
					const FIntPoint P0 = W2P(R.Points[I].World, C);
					const FIntPoint P1 = W2P(R.Points[I + 1].World, C);
					const int32 WidthPx = FMath::Max(2, W2PixLen(R.Points[I].WidthM * 100.0f, C));
					MapBlockoutMath::RasterizeLine(C.Water, WORK_W, WORK_H,
						P0.X, P0.Y, P1.X, P1.Y, WidthPx / 2, 1);
				}
			}
		}
		else
		{
			const float Thr = 0.5f;
			for (int32 i = 0; i < N; ++i) { C.Water[i] = C.Flood[i] > Thr ? 1 : 0; }
			MapBlockoutMath::BinaryOpening(C.Water, WORK_W, WORK_H, 1);
		}
		// Match the Python reference's `binary_dilation(WATER, iterations=3)` which
		// uses a 4-connectivity cross structuring element — Manhattan distance
		// of 3 = 25 cells in the diamond. A radius-2 box (25 cells) is the same
		// area; a radius-3 box (49 cells) over-buffers by ~2x and steals ~5pp of
		// headroom from Stage 3.
		C.WaterBuf = C.Water;
		MapBlockoutMath::Dilate(C.WaterBuf, WORK_W, WORK_H, 2);
	}

	// =====================================================================
	// Rasterizers shared with the render/heatmap code.
	// =====================================================================

	void RasterizeRoads(const TArray<FMapBlockoutRoad>& Roads, const FStageCtx& C,
		TArray<uint8>& OutMask)
	{
		OutMask.Init(0, WORK_W * WORK_H);
		for (const FMapBlockoutRoad& R : Roads)
		{
			const int32 ThickPx = FMath::Max(3,
				W2PixLen(C.Span * (R.Type == EMapBlockoutRoadType::Main ? 0.0037f : 0.0027f), C));
			TArray<FIntPoint> Cells; Cells.Reserve(R.Points.Num());
			for (const FVector2D& P : R.Points) { Cells.Add(FIntPoint(W2Col(P.X, C), W2Row(P.Y, C))); }
			MapBlockoutMath::RasterizePolyline(OutMask, WORK_W, WORK_H, Cells, ThickPx / 2, 1);
		}
		MapBlockoutMath::Dilate(OutMask, WORK_W, WORK_H, 2);
	}

	// Pixel-space rotated rectangle as a polygon (for overlap tests + rasterization).
	TArray<FIntPoint> BuildingPoly(const FMapBlockoutBuilding& B, const FStageCtx& C)
	{
		const float A = FMath::DegreesToRadians(B.YawDegrees);
		const float Ca = FMath::Cos(A), Sa = FMath::Sin(A);
		const float Hx = B.HalfExtents.X, Hy = B.HalfExtents.Y;
		FVector2D Pts[4] = {
			{-Hx, -Hy}, { Hx, -Hy}, { Hx,  Hy}, {-Hx,  Hy}
		};
		TArray<FIntPoint> Out; Out.Reserve(4);
		for (const FVector2D& O : Pts)
		{
			const float Wx = B.World.X + O.X * Ca - O.Y * Sa;
			const float Wy = B.World.Y + O.X * Sa + O.Y * Ca;
			Out.Add(FIntPoint(W2Col(Wx, C), W2Row(Wy, C)));
		}
		return Out;
	}

	void RasterizeBuildings(const TArray<FMapBlockoutBuilding>& Bs, const FStageCtx& C,
		TArray<uint8>& OutMask)
	{
		OutMask.Init(0, WORK_W * WORK_H);
		for (const FMapBlockoutBuilding& B : Bs)
		{
			TArray<FIntPoint> Poly = BuildingPoly(B, C);
			MapBlockoutMath::RasterizePolygonFilled(OutMask, WORK_W, WORK_H, Poly, 1);
		}
		MapBlockoutMath::Dilate(OutMask, WORK_W, WORK_H, 2);
	}

	FString ResolveOutputDir(const FMapBlockoutConfig& Config)
	{
		FString Dir = Config.OutputDir;
		if (Dir.IsEmpty())
		{
			Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MapBlockout"), Config.LevelName);
		}
		FPaths::NormalizeDirectoryName(Dir);
		return FPaths::ConvertRelativePathToFull(Dir);
	}

	// =====================================================================
	// Render drawing helpers (mirror UMapBlockoutService_Stages.cpp).
	// =====================================================================

	// Compose a "base terrain" tinted background bitmap from crop + forest layers.
	void RenderBaseTerrain(const FStageCtx& C, TArray<FColor>& OutBitmap)
	{
		MapBlockoutImage::NewBitmap(OutBitmap, WORK_W, WORK_H, MapBlockoutImage::Colors::Background);
		const int32 N = WORK_W * WORK_H;
		for (int32 i = 0; i < N; ++i)
		{
			const float Cf = (i < C.Crop.Num()) ? C.Crop[i] : 0.0f;
			const float Ff = (i < C.Forest.Num()) ? C.Forest[i] : 0.0f;
			const int32 R = FMath::Clamp(int32(MapBlockoutImage::Colors::Background.R) + int32(Cf * 10.0f + Ff * 6.0f), 0, 255);
			const int32 G = FMath::Clamp(int32(MapBlockoutImage::Colors::Background.G) + int32(Cf * 10.0f + Ff * 9.0f), 0, 255);
			const int32 B = FMath::Clamp(int32(MapBlockoutImage::Colors::Background.B) + int32(Cf *  5.0f + Ff * 6.0f), 0, 255);
			OutBitmap[i] = FColor(R, G, B);
		}
	}

	void OverlayWater(TArray<FColor>& Bmp, const FStageCtx& C)
	{
		MapBlockoutImage::OverlayMask(Bmp, WORK_W, WORK_H, C.Water, MapBlockoutImage::Colors::River);
	}

	void DrawRoadsToBitmap(TArray<FColor>& Bmp, const FStageCtx& C,
		const FMapBlockoutRoadNetworkResult& Roads)
	{
		for (const FMapBlockoutRoad& R : Roads.Roads)
		{
			TArray<FIntPoint> Cells; Cells.Reserve(R.Points.Num());
			for (const FVector2D& P : R.Points) { Cells.Add(FIntPoint(W2Col(P.X, C), W2Row(P.Y, C))); }
			const FColor Col = (R.Type == EMapBlockoutRoadType::Main)
				? MapBlockoutImage::Colors::MainRoad
				: MapBlockoutImage::Colors::DirtRoad;
			const int32 Thick = (R.Type == EMapBlockoutRoadType::Main) ? 3 : 2;
			MapBlockoutImage::DrawPolyline(Bmp, WORK_W, WORK_H, Cells, Col, Thick);
		}
	}

	void DrawBuildingsToBitmap(TArray<FColor>& Bmp, const FStageCtx& C,
		const FMapBlockoutPOIResult& Pois)
	{
		for (const FMapBlockoutPOI& POI : Pois.Pois)
		{
			for (const FMapBlockoutBuilding& B : POI.Buildings)
			{
				TArray<FIntPoint> Poly = BuildingPoly(B, C);
				TArray<uint8> Local; Local.SetNumZeroed(WORK_W * WORK_H);
				MapBlockoutMath::RasterizePolygonFilled(Local, WORK_W, WORK_H, Poly, 1);
				MapBlockoutImage::OverlayMask(Bmp, WORK_W, WORK_H, Local, MapBlockoutImage::Colors::Building);
			}
		}
	}

	void DrawPOIBoundariesToBitmap(TArray<FColor>& Bmp, const FStageCtx& C,
		const FMapBlockoutPOIResult& Pois)
	{
		for (const FMapBlockoutPOI& POI : Pois.Pois)
		{
			MapBlockoutImage::DrawCircleOutline(Bmp, WORK_W, WORK_H,
				W2Col(POI.Center.X, C), W2Row(POI.Center.Y, C),
				W2PixLen(POI.RadiusCm, C),
				MapBlockoutImage::Colors::POIBoundary, 4);
		}
	}

	void DrawRailToBitmap(TArray<FColor>& Bmp, const FStageCtx& C,
		const FMapBlockoutRailwayResult& Rail)
	{
		for (const FMapBlockoutRoad& R : Rail.RailLines)
		{
			TArray<FIntPoint> Cells; Cells.Reserve(R.Points.Num());
			for (const FVector2D& P : R.Points) { Cells.Add(FIntPoint(W2Col(P.X, C), W2Row(P.Y, C))); }
			MapBlockoutImage::DrawPolyline(Bmp, WORK_W, WORK_H, Cells, MapBlockoutImage::Colors::Railway, 3);
		}
	}

	void DrawBridgesToBitmap(TArray<FColor>& Bmp, const FStageCtx& C,
		const FMapBlockoutRailwayResult& Rail)
	{
		for (const FMapBlockoutBridge& B : Rail.Bridges)
		{
			const int32 Cc = W2Col(B.World.X, C);
			const int32 Rr = W2Row(B.World.Y, C);
			MapBlockoutImage::DrawRect(Bmp, WORK_W, WORK_H,
				Cc - 11, Rr - 11, Cc + 11, Rr + 11, MapBlockoutImage::Colors::Bridge);
		}
	}

	// Glue a WORK_W x WORK_H map bitmap into the full canvas-sized output (with
	// the left/top margins for axis labels and right panel for the color key).
	void ComposeCanvas(const TArray<FColor>& MapBmp, TArray<FColor>& OutCanvas)
	{
		MapBlockoutImage::NewBitmap(OutCanvas, CANVAS_W, CANVAS_H, FColor(13, 14, 17));
		for (int32 Y = 0; Y < WORK_H; ++Y)
		{
			for (int32 X = 0; X < WORK_W; ++X)
			{
				OutCanvas[(CANVAS_TOP + Y) * CANVAS_W + (CANVAS_LEFT + X)] = MapBmp[Y * WORK_W + X];
			}
		}
		// Map border.
		MapBlockoutImage::DrawLine(OutCanvas, CANVAS_W, CANVAS_H, CANVAS_LEFT, CANVAS_TOP, CANVAS_LEFT + WORK_W, CANVAS_TOP, FColor(122, 128, 138), 1);
		MapBlockoutImage::DrawLine(OutCanvas, CANVAS_W, CANVAS_H, CANVAS_LEFT, CANVAS_TOP + WORK_H, CANVAS_LEFT + WORK_W, CANVAS_TOP + WORK_H, FColor(122, 128, 138), 1);
		MapBlockoutImage::DrawLine(OutCanvas, CANVAS_W, CANVAS_H, CANVAS_LEFT, CANVAS_TOP, CANVAS_LEFT, CANVAS_TOP + WORK_H, FColor(122, 128, 138), 1);
		MapBlockoutImage::DrawLine(OutCanvas, CANVAS_W, CANVAS_H, CANVAS_LEFT + WORK_W, CANVAS_TOP, CANVAS_LEFT + WORK_W, CANVAS_TOP + WORK_H, FColor(122, 128, 138), 1);
	}

	TArray<MapBlockoutImage::FKeyEntry> KeyForStage(int32 Stage)
	{
		using KE = MapBlockoutImage::FKeyEntry;
		TArray<KE> Out;
		Out.Add(KE(MapBlockoutImage::Colors::MainRoad, TEXT("Main road")));
		Out.Add(KE(MapBlockoutImage::Colors::DirtRoad, TEXT("Dirt road")));
		Out.Add(KE(MapBlockoutImage::Colors::River,    TEXT("River / water")));
		if (Stage >= 2)
		{
			Out.Add(KE(MapBlockoutImage::Colors::Building,    TEXT("Building")));
			Out.Add(KE(MapBlockoutImage::Colors::POIBoundary, TEXT("POI boundary"), TEXT("ring")));
		}
		if (Stage >= 3)
		{
			Out.Add(KE(MapBlockoutImage::Colors::Field, TEXT("Field")));
		}
		if (Stage >= 4)
		{
			Out.Add(KE(MapBlockoutImage::Colors::Forest,   TEXT("Forest")));
			Out.Add(KE(MapBlockoutImage::Colors::Treeline, TEXT("Treeline")));
			Out.Add(KE(MapBlockoutImage::Colors::Scrub,    TEXT("Underbrush")));
		}
		if (Stage >= 5)
		{
			Out.Add(KE(MapBlockoutImage::Colors::Railway, TEXT("Railway"), TEXT("line")));
			Out.Add(KE(MapBlockoutImage::Colors::Bridge,  TEXT("Bridge")));
		}
		return Out;
	}
}

// =========================================================================
// Renderers
// =========================================================================

FString MapBlockoutPipeline::RenderStageSnapshot(
	int32 Stage, const FMapBlockoutState& State, const FString& OutputDir)
{
	if (Stage < 1 || Stage > 5 || OutputDir.IsEmpty()) { return FString(); }
	if (!State.Grid.bSuccess) { return FString(); }

	FStageCtx C; PrepareCtx(State.Grid, State.Config, C);

	TArray<FColor> MapBmp; RenderBaseTerrain(C, MapBmp);
	OverlayWater(MapBmp, C);

	if (Stage >= 3)
	{
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage3Fields.FieldMask.Cells, MapBlockoutImage::Colors::Field);
	}
	if (Stage >= 4)
	{
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage4Foliage.ScrubMask.Cells, MapBlockoutImage::Colors::Scrub);
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage4Foliage.ForestMask.Cells, MapBlockoutImage::Colors::Forest);
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage4Foliage.TreelineMask.Cells, MapBlockoutImage::Colors::Treeline);
	}

	DrawRoadsToBitmap(MapBmp, C, State.Stage1Roads);

	if (Stage >= 5)
	{
		DrawRailToBitmap(MapBmp, C, State.Stage5Railway);
	}

	if (Stage >= 2)
	{
		DrawBuildingsToBitmap(MapBmp, C, State.Stage2Pois);
		DrawPOIBoundariesToBitmap(MapBmp, C, State.Stage2Pois);
	}
	if (Stage >= 5)
	{
		DrawBridgesToBitmap(MapBmp, C, State.Stage5Railway);
	}

	// Compose into full canvas + draw color key panel.
	TArray<FColor> Canvas; ComposeCanvas(MapBmp, Canvas);
	static const TCHAR* TitleFmt[] = {
		TEXT("STAGE 1 - ROADWAYS"),
		TEXT("STAGE 2 - ROADS + Pois"),
		TEXT("STAGE 3 - + FIELDS"),
		TEXT("STAGE 4 - + TREES / FORESTS / SCRUB"),
		TEXT("STAGE 5 - + RAILWAY + BRIDGES"),
	};
	MapBlockoutImage::DrawText5x7(Canvas, CANVAS_W, CANVAS_H,
		CANVAS_LEFT, 30, TitleFmt[Stage - 1], MapBlockoutImage::Colors::Ink, 3);
	MapBlockoutImage::DrawColorKey(Canvas, CANVAS_W, CANVAS_H,
		CANVAS_LEFT + WORK_W + 30, CANVAS_TOP + 4, CANVAS_RPANEL - 58,
		TEXT("COLOR KEY"), KeyForStage(Stage));

	// Filename
	static const TCHAR* StageFile[] = {
		TEXT("Stage1_Roads.png"),
		TEXT("Stage2_Roads_POIs.png"),
		TEXT("Stage3_Roads_POIs_Fields.png"),
		TEXT("Stage4_Roads_POIs_Fields_Foliage.png"),
		TEXT("Stage5_Roads_POIs_Fields_Foliage_Rail.png"),
	};
	const FString Path = FPaths::Combine(OutputDir, StageFile[Stage - 1]);
	if (!MapBlockoutImage::WritePNG(Canvas, CANVAS_W, CANVAS_H, Path)) { return FString(); }
	return FPaths::ConvertRelativePathToFull(Path);
}

TArray<FString> MapBlockoutPipeline::RenderFinalDeliverables(
	const FMapBlockoutState& State, const FString& OutputDir)
{
	TArray<FString> Out;
	if (!State.Grid.bSuccess) { return Out; }

	FStageCtx C; PrepareCtx(State.Grid, State.Config, C);

	// Combined map (same layer set as Stage 5).
	{
		TArray<FColor> MapBmp; RenderBaseTerrain(C, MapBmp);
		OverlayWater(MapBmp, C);
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage3Fields.FieldMask.Cells, MapBlockoutImage::Colors::Field);
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage4Foliage.ScrubMask.Cells, MapBlockoutImage::Colors::Scrub);
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage4Foliage.ForestMask.Cells, MapBlockoutImage::Colors::Forest);
		MapBlockoutImage::OverlayMask(MapBmp, WORK_W, WORK_H,
			State.Stage4Foliage.TreelineMask.Cells, MapBlockoutImage::Colors::Treeline);
		DrawRoadsToBitmap(MapBmp, C, State.Stage1Roads);
		DrawRailToBitmap(MapBmp, C, State.Stage5Railway);
		DrawBuildingsToBitmap(MapBmp, C, State.Stage2Pois);
		DrawPOIBoundariesToBitmap(MapBmp, C, State.Stage2Pois);
		DrawBridgesToBitmap(MapBmp, C, State.Stage5Railway);

		TArray<FColor> Canvas; ComposeCanvas(MapBmp, Canvas);
		MapBlockoutImage::DrawText5x7(Canvas, CANVAS_W, CANVAS_H, CANVAS_LEFT, 30,
			FString::Printf(TEXT("%s - COMBINED MAP"), *State.Config.LevelName.ToUpper()),
			MapBlockoutImage::Colors::Ink, 3);
		MapBlockoutImage::DrawColorKey(Canvas, CANVAS_W, CANVAS_H,
			CANVAS_LEFT + WORK_W + 30, CANVAS_TOP + 4, CANVAS_RPANEL - 58,
			TEXT("COLOR KEY"), KeyForStage(5));

		const FString Path = FPaths::Combine(OutputDir, TEXT("CombinedFoliageAndMap.png"));
		if (MapBlockoutImage::WritePNG(Canvas, CANVAS_W, CANVAS_H, Path))
		{
			Out.Add(FPaths::ConvertRelativePathToFull(Path));
		}
	}

	// Foliage heatmap (fields + scrub + forest + treeline; no key).
	{
		const int32 N = WORK_W * WORK_H;
		TArray<float> Density; Density.SetNumZeroed(N);
		auto Bump = [&](const TArray<uint8>& M, float W) {
			if (M.Num() != N) { return; }
			for (int32 i = 0; i < N; ++i) { if (M[i]) { Density[i] += W; } }
		};
		Bump(State.Stage3Fields.FieldMask.Cells, 0.6f);
		Bump(State.Stage4Foliage.ScrubMask.Cells, 0.4f);
		Bump(State.Stage4Foliage.ForestMask.Cells, 1.0f);
		Bump(State.Stage4Foliage.TreelineMask.Cells, 0.8f);

		TArray<FColor> MapBmp; MapBlockoutImage::RenderHeatmap(Density, WORK_W, WORK_H, MapBmp);
		TArray<FColor> Canvas; ComposeCanvas(MapBmp, Canvas);
		MapBlockoutImage::DrawText5x7(Canvas, CANVAS_W, CANVAS_H, CANVAS_LEFT, 30,
			TEXT("FOLIAGE HEATMAP"), MapBlockoutImage::Colors::Ink, 3);

		const FString Path = FPaths::Combine(OutputDir, TEXT("FoliageHeatMap.png"));
		if (MapBlockoutImage::WritePNG(Canvas, CANVAS_W, CANVAS_H, Path))
		{
			Out.Add(FPaths::ConvertRelativePathToFull(Path));
		}
	}

	// Map heatmap (roads + buildings + rail + bridges; no key).
	{
		const int32 N = WORK_W * WORK_H;
		TArray<float> Density; Density.SetNumZeroed(N);
		TArray<uint8> RoadMask; RasterizeRoads(State.Stage1Roads.Roads, C, RoadMask);
		TArray<FMapBlockoutBuilding> AllB;
		for (const FMapBlockoutPOI& P : State.Stage2Pois.Pois) { AllB.Append(P.Buildings); }
		TArray<uint8> BldMask; RasterizeBuildings(AllB, C, BldMask);
		TArray<uint8> RailMask; RailMask.SetNumZeroed(N);
		for (const FMapBlockoutRoad& R : State.Stage5Railway.RailLines)
		{
			TArray<FIntPoint> Cells; Cells.Reserve(R.Points.Num());
			for (const FVector2D& P : R.Points) { Cells.Add(FIntPoint(W2Col(P.X, C), W2Row(P.Y, C))); }
			MapBlockoutMath::RasterizePolyline(RailMask, WORK_W, WORK_H, Cells, 2, 1);
		}
		for (int32 i = 0; i < N; ++i)
		{
			Density[i] += RoadMask[i] * 1.0f + BldMask[i] * 0.6f + RailMask[i] * 0.8f;
		}
		for (const FMapBlockoutBridge& B : State.Stage5Railway.Bridges)
		{
			const int32 Cc = W2Col(B.World.X, C);
			const int32 Rr = W2Row(B.World.Y, C);
			for (int32 Dy = -3; Dy <= 3; ++Dy)
			for (int32 Dx = -3; Dx <= 3; ++Dx)
			{
				const int32 X = Cc + Dx, Y = Rr + Dy;
				if (X >= 0 && Y >= 0 && X < WORK_W && Y < WORK_H) { Density[Y * WORK_W + X] += 1.2f; }
			}
		}

		TArray<FColor> MapBmp; MapBlockoutImage::RenderHeatmap(Density, WORK_W, WORK_H, MapBmp);
		TArray<FColor> Canvas; ComposeCanvas(MapBmp, Canvas);
		MapBlockoutImage::DrawText5x7(Canvas, CANVAS_W, CANVAS_H, CANVAS_LEFT, 30,
			TEXT("MAP HEATMAP"), MapBlockoutImage::Colors::Ink, 3);

		const FString Path = FPaths::Combine(OutputDir, TEXT("MapHeatMap.png"));
		if (MapBlockoutImage::WritePNG(Canvas, CANVAS_W, CANVAS_H, Path))
		{
			Out.Add(FPaths::ConvertRelativePathToFull(Path));
		}
	}

	return Out;
}

// =========================================================================
// Orchestrators
// =========================================================================

FMapBlockoutPipelineResult MapBlockoutPipeline::RunFullPipeline(
	const FMapBlockoutLandcoverGrid& Grid, const FMapBlockoutConfig& Config)
{
	FMapBlockoutPipelineResult Result;
	Result.OutputDir = ResolveOutputDir(Config);

	if (!Grid.bSuccess)
	{
		Result.ErrorMessage = TEXT("RunFullPipeline: invalid Grid (call ExportLandcoverGrid or LoadLandcoverGridJson first).");
		return Result;
	}

	Result.FinalState.Config = Config;
	Result.FinalState.Grid = Grid;

	Result.FinalState.Stage1Roads = MapBlockoutPipeline::GenerateRoads(Grid, Config);
	if (!Result.FinalState.Stage1Roads.Gate.bAllPassed)
	{
		Result.ErrorMessage = TEXT("Stage 1 gate failed.");
		return Result;
	}

	Result.FinalState.Stage2Pois = MapBlockoutPipeline::PlacePois(Grid, Result.FinalState.Stage1Roads, Config);
	if (!Result.FinalState.Stage2Pois.Gate.bAllPassed)
	{
		Result.ErrorMessage = TEXT("Stage 2 gate failed.");
		return Result;
	}

	Result.FinalState.Stage3Fields = MapBlockoutPipeline::PlaceFields(Grid,
		Result.FinalState.Stage1Roads, Result.FinalState.Stage2Pois, Config);
	if (!Result.FinalState.Stage3Fields.Gate.bAllPassed)
	{
		Result.ErrorMessage = TEXT("Stage 3 gate failed.");
		return Result;
	}

	Result.FinalState.Stage4Foliage = MapBlockoutPipeline::PlaceFoliage(Grid,
		Result.FinalState.Stage1Roads, Result.FinalState.Stage2Pois,
		Result.FinalState.Stage3Fields, Config);
	if (!Result.FinalState.Stage4Foliage.Gate.bAllPassed)
	{
		Result.ErrorMessage = TEXT("Stage 4 gate failed.");
		return Result;
	}

	Result.FinalState.Stage5Railway = MapBlockoutPipeline::PlaceRailway(Grid,
		Result.FinalState.Stage1Roads, Result.FinalState.Stage2Pois,
		Result.FinalState.Stage3Fields, Result.FinalState.Stage4Foliage, Config);
	if (!Result.FinalState.Stage5Railway.Gate.bAllPassed)
	{
		Result.ErrorMessage = TEXT("Stage 5 gate failed.");
		return Result;
	}

	Result.FinalGate = MapBlockoutPipeline::RunFinalPass(Result.FinalState);
	if (!Result.FinalGate.bAllPassed)
	{
		Result.ErrorMessage = TEXT("Final Pass failed.");
		return Result;
	}

	// Render deliverables.
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Result.OutputDir);
	for (int32 S = 1; S <= 5; ++S)
	{
		const FString Path = MapBlockoutPipeline::RenderStageSnapshot(S, Result.FinalState, Result.OutputDir);
		if (!Path.IsEmpty()) { Result.OutputFiles.Add(Path); }
	}
	for (const FString& Path : MapBlockoutPipeline::RenderFinalDeliverables(Result.FinalState, Result.OutputDir))
	{
		if (!Path.IsEmpty()) { Result.OutputFiles.Add(Path); }
	}

	Result.bSuccess = (Result.OutputFiles.Num() == 8);
	if (!Result.bSuccess)
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Pipeline gates passed but wrote %d/8 output files."), Result.OutputFiles.Num());
	}
	return Result;
}

FMapBlockoutPipelineResult MapBlockoutPipeline::RunFullPipelineForLandscape(
	const FString& LandscapeLabel, const FMapBlockoutConfig& Config)
{
	const FMapBlockoutLandcoverGrid Grid = MapBlockoutPipeline::ExportLandcoverGrid(LandscapeLabel, 120);
	if (!Grid.bSuccess)
	{
		FMapBlockoutPipelineResult Result;
		Result.ErrorMessage = FString::Printf(
			TEXT("ExportLandcoverGrid failed for '%s': %s"),
			*LandscapeLabel, *Grid.ErrorMessage);
		return Result;
	}
	return MapBlockoutPipeline::RunFullPipeline(Grid, Config);
}
