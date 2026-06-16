// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.
//
// MapBlockout Stage 0 — landcover grid export, JSON round-trip, and river centerline extraction.
// Ported faithfully from VibeUE's UMapBlockoutService.cpp (Stage 0 functions only). The one
// adaptation: ExportLandcoverGrid samples the landscape DIRECTLY via the engine APIs our landscape
// tool uses (FLandscapeEditDataInterface), rather than through VibeUE's ULandscapeService PNG path.

#include "MapBlockoutPipeline.h"
#include "MapBlockoutTypes.h"
#include "MapBlockoutMath.h"

#include "EngineUtils.h"

#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#if WITH_EDITOR
#include "LandscapeEdit.h"
#endif

#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Algo/Reverse.h"

// =========================================================================
// File-local helpers
// =========================================================================
namespace
{
	/** Resolve the target ALandscape by actor name or display label (same pattern as MCPTool_Landscape). */
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
	/** Find the first editor world that contains a registered landscape. */
	UWorld* FindEditorWorld()
	{
		if (GWorld && GWorld->WorldType == EWorldType::Editor)
		{
			return GWorld;
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Editor && Context.World())
				{
					return Context.World();
				}
			}
		}
		return GWorld;
	}
#endif // WITH_EDITOR
}


// =========================================================================
// Stage 0 — Landcover grid export
//
// KEY ADAPTATION vs VibeUE: VibeUE's ExportLandcoverGrid drove its own
// ULandscapeService to write a PNG per layer/height and then decoded + downsampled
// those PNGs. We don't have that service, so we sample the landscape DIRECTLY using
// FLandscapeEditDataInterface (the merged final read), exactly like our landscape tool:
//   - ResolveLandscape(...) via TActorIterator<ALandscape>
//   - ULandscapeInfo::GetLandscapeExtent(...) for native resolution + bounds
//   - GetHeightDataFast(...) for the heightmap, GetWeightData(...) per paint layer
//   - downsample native-res -> GridN x GridN via MapBlockoutMath::ResampleBilinear
// World-bounds and flood-synthesis logic are preserved verbatim from VibeUE.
//
// Orientation: the native landscape read and ResampleBilinear produce row 0 = north
// (top). The FMapBlockoutLandcoverGrid struct stores row 0 = SOUTH, so we apply a single
// FlipVertical at this export boundary (height + every layer) — the one-time flip VibeUE
// also performs at its export boundary.
// =========================================================================

FMapBlockoutLandcoverGrid MapBlockoutPipeline::ExportLandcoverGrid(
	const FString& LandscapeLabel, int32 GridN,
	bool bSynthesizeFloodFromHeight, float FloodPercentile)
{
	FMapBlockoutLandcoverGrid Result;
	Result.LandscapeLabel = LandscapeLabel;
	Result.GridN = FMath::Max(8, GridN);

#if WITH_EDITOR
	if (LandscapeLabel.IsEmpty())
	{
		Result.ErrorMessage = TEXT("ExportLandcoverGrid: empty LandscapeLabel.");
		return Result;
	}

	UWorld* World = FindEditorWorld();
	if (!World)
	{
		Result.ErrorMessage = TEXT("ExportLandcoverGrid: no editor world available.");
		return Result;
	}

	ALandscape* Landscape = ResolveLandscape(World, LandscapeLabel);
	if (!Landscape)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Landscape '%s' not found."), *LandscapeLabel);
		return Result;
	}

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Landscape '%s' has no ULandscapeInfo."), *LandscapeLabel);
		return Result;
	}

	int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Landscape '%s': failed to query extent."), *LandscapeLabel);
		return Result;
	}

	const int32 SrcW = MaxX - MinX + 1;
	const int32 SrcH = MaxY - MinY + 1;
	if (SrcW <= 1 || SrcH <= 1)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Landscape '%s': degenerate extent %dx%d."), *LandscapeLabel, SrcW, SrcH);
		return Result;
	}

	const FVector Scale = Landscape->GetActorScale3D();

	// World bounds: symmetric, origin-centred square half-span — preserved verbatim from VibeUE.
	// Span derives from the landscape's resolution * scale (cm/quad).
	const float HalfSpanX = (SrcW - 1) * (float)Scale.X * 0.5f;
	const float HalfSpanY = (SrcH - 1) * (float)Scale.Y * 0.5f;
	const float HalfSpan = FMath::Max(HalfSpanX, HalfSpanY);
	Result.WorldLo = -HalfSpan;
	Result.WorldHi = +HalfSpan;

	const int32 N = Result.GridN;

	// Read the MERGED final landscape data deterministically. The default-ctor FLandscapeEditDataInterface
	// reads through the landscape's *currently selected* edit layer; if the user has one active in the
	// Landscape panel, that would silently sample only that layer's contribution. Clear the editing layer
	// for the duration of the read, then restore it.
	ALandscape* LandscapeActor = LandscapeInfo->LandscapeActor.Get();
	const FGuid SavedEditLayer = LandscapeActor ? LandscapeActor->GetEditingLayer() : FGuid();
	if (LandscapeActor) { LandscapeActor->SetEditingLayer(FGuid()); }

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);

	// ---- Heightmap: read native uint16 -> normalize -> downsample -> flip to row-0-south. ----
	{
		TArray<uint16> Heights;
		Heights.SetNumZeroed(SrcW * SrcH);
		// GetHeightDataFast reads the full requested region (does not rewrite the bounds args).
		LandscapeEdit.GetHeightDataFast(MinX, MinY, MaxX, MaxY, Heights.GetData(), 0);

		TArray<float> SrcFloat;
		SrcFloat.SetNumUninitialized(SrcW * SrcH);
		const float InvMax = 1.0f / 65535.0f;
		for (int32 i = 0; i < SrcW * SrcH; ++i)
		{
			SrcFloat[i] = Heights[i] * InvMax;
		}

		MapBlockoutMath::ResampleBilinear(SrcFloat, SrcW, SrcH, Result.HeightNormalized, N, N);
		// Native read + resample give row 0 = north; storage wants row 0 = south.
		MapBlockoutMath::FlipVertical(Result.HeightNormalized, N, N);
	}

	// ---- Weight layers: one per paint layer reported by ULandscapeInfo. ----
#if WITH_EDITORONLY_DATA
	for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
	{
		ULandscapeLayerInfoObject* LayerInfo = LayerSettings.LayerInfoObj;
		if (!LayerInfo)
		{
			continue;
		}

		FMapBlockoutLayerMap LayerMap;
		LayerMap.LayerName = LayerSettings.GetLayerName().ToString();
		LayerMap.GridN = N;

		TArray<uint8> Weights;
		Weights.SetNumZeroed(SrcW * SrcH);
		// GetWeightData rewrites its X1/Y1/X2/Y2 args by reference (to the valid-data sub-extent).
		// Pass scratch copies so the originals keep describing the full native region — the buffer
		// stride (SrcW) and the resample below both depend on the full-region layout.
		int32 GX1 = MinX, GY1 = MinY, GX2 = MaxX, GY2 = MaxY;
		LandscapeEdit.GetWeightData(LayerInfo, GX1, GY1, GX2, GY2, Weights.GetData(), 0);

		TArray<float> SrcFloat;
		SrcFloat.SetNumUninitialized(SrcW * SrcH);
		const float Inv = 1.0f / 255.0f;
		for (int32 i = 0; i < SrcW * SrcH; ++i)
		{
			SrcFloat[i] = Weights[i] * Inv;
		}

		MapBlockoutMath::ResampleBilinear(SrcFloat, SrcW, SrcH, LayerMap.Weights, N, N);
		MapBlockoutMath::FlipVertical(LayerMap.Weights, N, N);

		Result.Layers.Add(MoveTemp(LayerMap));
	}
#endif // WITH_EDITORONLY_DATA

	// Restore the editor's previously-selected edit layer.
	if (LandscapeActor) { LandscapeActor->SetEditingLayer(SavedEditLayer); }

	// Optional: synthesize a "FloodFromHeight" layer by thresholding the lowest FloodPercentile%
	// of the heightmap. Mirrors build_inputs.py --flood-from-height. Useful when the source
	// landscape has no painted water layer. (Ported verbatim from VibeUE.)
	if (bSynthesizeFloodFromHeight && Result.HeightNormalized.Num() == N * N)
	{
		const float Pct = FMath::Clamp(FloodPercentile, 0.1f, 99.9f);
		TArray<float> Sorted = Result.HeightNormalized;
		Sorted.Sort();
		const int32 K = FMath::Clamp(FMath::RoundToInt(Sorted.Num() * Pct * 0.01f), 1, Sorted.Num() - 1);
		const float Threshold = Sorted[K];

		FMapBlockoutLayerMap FloodLM;
		FloodLM.LayerName = TEXT("FloodFromHeight");
		FloodLM.GridN = N;
		FloodLM.Weights.SetNumUninitialized(Result.HeightNormalized.Num());
		for (int32 i = 0; i < Result.HeightNormalized.Num(); ++i)
		{
			FloodLM.Weights[i] = (Result.HeightNormalized[i] <= Threshold) ? 1.0f : 0.0f;
		}
		Result.Layers.Add(MoveTemp(FloodLM));
	}

	Result.bSuccess = (N > 0 && Result.WorldHi > Result.WorldLo && Result.HeightNormalized.Num() == N * N);
	if (!Result.bSuccess)
	{
		Result.ErrorMessage = TEXT("ExportLandcoverGrid: invalid bounds or empty heightmap from landscape.");
	}
	return Result;

#else  // !WITH_EDITOR
	Result.bSuccess = false;
	Result.ErrorMessage = TEXT("ExportLandcoverGrid requires the editor (landscape sampling is editor-only).");
	return Result;
#endif // WITH_EDITOR
}


// =========================================================================
// JSON I/O (compatible with landcover_grid.json from the host-Python reference)
// =========================================================================

FString MapBlockoutPipeline::WriteLandcoverGridJson(
	const FMapBlockoutLandcoverGrid& Grid, const FString& OutputFilePath)
{
	if (OutputFilePath.IsEmpty()) { return FString(); }

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("N"), Grid.GridN);
	Root->SetNumberField(TEXT("lo"), Grid.WorldLo);
	Root->SetNumberField(TEXT("hi"), Grid.WorldHi);
	Root->SetStringField(TEXT("landscape"), Grid.LandscapeLabel);

	// Reference format: each layer is a list of lists [N][N], row 0 = south (matches our storage).
	const int32 N = Grid.GridN;
	for (const FMapBlockoutLayerMap& Layer : Grid.Layers)
	{
		if (Layer.Weights.Num() < N * N) { continue; }
		TArray<TSharedPtr<FJsonValue>> Rows;
		Rows.Reserve(N);
		for (int32 Y = 0; Y < N; ++Y)
		{
			TArray<TSharedPtr<FJsonValue>> Cols;
			Cols.Reserve(N);
			for (int32 X = 0; X < N; ++X)
			{
				const float W = Layer.Weights[Y * N + X];
				Cols.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(W * 1000.0f) / 1000.0f));
			}
			Rows.Add(MakeShared<FJsonValueArray>(Cols));
		}
		Root->SetArrayField(Layer.LayerName, Rows);
	}

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root, Writer)) { return FString(); }

	const FString Dir = FPaths::GetPath(OutputFilePath);
	if (!Dir.IsEmpty())
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);
	}
	if (!FFileHelper::SaveStringToFile(Text, *OutputFilePath)) { return FString(); }
	return FPaths::ConvertRelativePathToFull(OutputFilePath);
}


// =========================================================================
// Read landcover_grid.json (for tests + alternative entry points)
// =========================================================================

FMapBlockoutLandcoverGrid MapBlockoutPipeline::LoadLandcoverGridJson(const FString& FilePath)
{
	FMapBlockoutLandcoverGrid Out;

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		Out.ErrorMessage = FString::Printf(TEXT("LoadLandcoverGridJson: cannot read %s"), *FilePath);
		return Out;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Out.ErrorMessage = TEXT("LoadLandcoverGridJson: malformed JSON");
		return Out;
	}

	Out.GridN = static_cast<int32>(Root->GetNumberField(TEXT("N")));
	Out.WorldLo = static_cast<float>(Root->GetNumberField(TEXT("lo")));
	Out.WorldHi = static_cast<float>(Root->GetNumberField(TEXT("hi")));
	Root->TryGetStringField(TEXT("landscape"), Out.LandscapeLabel);

	if (Out.GridN <= 0 || Out.WorldHi <= Out.WorldLo)
	{
		Out.ErrorMessage = TEXT("LoadLandcoverGridJson: invalid N or bounds");
		return Out;
	}

	const int32 N = Out.GridN;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		if (Pair.Key == TEXT("N") || Pair.Key == TEXT("lo") || Pair.Key == TEXT("hi") || Pair.Key == TEXT("landscape")) { continue; }
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Pair.Value->TryGetArray(Rows) || Rows->Num() != N) { continue; }
		FMapBlockoutLayerMap LM;
		LM.LayerName = Pair.Key;
		LM.GridN = N;
		LM.Weights.SetNumZeroed(N * N);
		for (int32 Y = 0; Y < N; ++Y)
		{
			const TArray<TSharedPtr<FJsonValue>>* Cols = nullptr;
			if (!(*Rows)[Y]->TryGetArray(Cols) || Cols->Num() != N) { continue; }
			for (int32 X = 0; X < N; ++X)
			{
				LM.Weights[Y * N + X] = static_cast<float>((*Cols)[X]->AsNumber());
			}
		}
		Out.Layers.Add(MoveTemp(LM));
	}
	Out.bSuccess = true;
	return Out;
}


// =========================================================================
// River centerline extraction (skeletonize binary water -> simplified polylines)
//
// Algorithm (port of reference/river_centerline_reference.py):
//   1. Pick the largest 8-connected water component (drops ponds/lakes).
//   2. Euclidean distance transform: each water cell's distance to the
//      nearest non-water cell — recovers the medial axis (high DT = center
//      of the river, low DT = bank).
//   3. Two BFS passes to find the geodesic diameter endpoints (the two
//      cells with the longest shortest-path between them in the water
//      component).
//   4. Dijkstra from one endpoint to the other, with edge cost
//      step * (1 + K*(maxDt - dt)) — favours hugging the high-DT ridge.
//   5. Tributary: farthest cell from any main-spine cell, traced back via
//      Dijkstra to the nearest main-spine point.
//   6. Resample to ~26-cell steps, convert to world coords, width per
//      point = 2 * dt * meters_per_pixel so wide rivers carry wide-river
//      width and narrow tributaries carry narrow.
// =========================================================================

namespace
{
	struct FPQNode
	{
		float Cost;
		int32 Idx;
		// HeapPush/HeapPop default to <, so reverse the comparison for a min-heap.
		bool operator<(const FPQNode& Other) const { return Cost < Other.Cost; }
	};

	/** Keep only the largest 8-connected component of Mask, in place. */
	void KeepLargestComponent8(TArray<uint8>& Mask, int32 W, int32 H)
	{
		if (W <= 0 || H <= 0 || Mask.Num() < W * H) { return; }
		TArray<int32> Labels; Labels.Init(0, W * H);

		TArray<int32> CompSize; CompSize.Add(0);    // label 0 sentinel
		TArray<int32> Q; Q.Reserve(W * H / 4 + 8);

		for (int32 Start = 0; Start < W * H; ++Start)
		{
			if (!Mask[Start] || Labels[Start] != 0) { continue; }
			const int32 NewLabel = CompSize.Num();
			CompSize.Add(0);
			Labels[Start] = NewLabel;
			Q.Reset();
			Q.Add(Start);
			int32 Head = 0;
			while (Head < Q.Num())
			{
				const int32 U = Q[Head++];
				++CompSize[NewLabel];
				const int32 X = U % W, Y = U / W;
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						if (!Dx && !Dy) continue;
						const int32 Nx = X + Dx, Ny = Y + Dy;
						if (Nx < 0 || Ny < 0 || Nx >= W || Ny >= H) continue;
						const int32 V = Ny * W + Nx;
						if (!Mask[V] || Labels[V] != 0) continue;
						Labels[V] = NewLabel;
						Q.Add(V);
					}
				}
			}
		}

		// Find the biggest.
		int32 BestLabel = 0, BestSize = 0;
		for (int32 L = 1; L < CompSize.Num(); ++L)
		{
			if (CompSize[L] > BestSize) { BestSize = CompSize[L]; BestLabel = L; }
		}

		// Erase everything but the biggest.
		for (int32 i = 0; i < W * H; ++i)
		{
			if (Labels[i] != BestLabel) { Mask[i] = 0; }
		}
	}

	/** BFS over an 8-connected mask. Returns the farthest cell index from SrcIdx and (optionally) the distance map. */
	int32 BFSFarthestCell(const TArray<uint8>& Mask, int32 W, int32 H, int32 SrcIdx, TArray<int32>* OutDist = nullptr)
	{
		const int32 N = W * H;
		TArray<int32> Dist; Dist.Init(-1, N);
		TArray<int32> Q; Q.Reserve(N / 4 + 8);
		Q.Add(SrcIdx); Dist[SrcIdx] = 0;
		int32 Far = SrcIdx, FarDist = 0;
		int32 Head = 0;
		while (Head < Q.Num())
		{
			const int32 U = Q[Head++];
			const int32 X = U % W, Y = U / W;
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					if (!Dx && !Dy) continue;
					const int32 Nx = X + Dx, Ny = Y + Dy;
					if (Nx < 0 || Ny < 0 || Nx >= W || Ny >= H) continue;
					const int32 V = Ny * W + Nx;
					if (!Mask[V] || Dist[V] >= 0) continue;
					Dist[V] = Dist[U] + 1;
					Q.Add(V);
					if (Dist[V] > FarDist) { FarDist = Dist[V]; Far = V; }
				}
			}
		}
		if (OutDist) { *OutDist = MoveTemp(Dist); }
		return Far;
	}

	/**
	 * Dijkstra from SrcIdx to DstIdx over an 8-connected mask, with edge cost
	 *   step * (1 + K * (MaxDt - Dt[v]))
	 * favouring cells with high distance-transform value (medial axis).
	 * Returns the path (Src→Dst inclusive); empty on failure.
	 */
	void DijkstraMedialPath(
		const TArray<uint8>& Mask, int32 W, int32 H,
		const TArray<float>& Dt, float MaxDt,
		int32 SrcIdx, int32 DstIdx,
		float K, TArray<int32>& OutPath)
	{
		OutPath.Reset();
		const int32 N = W * H;
		TArray<float> Best; Best.Init(TNumericLimits<float>::Max(), N);
		TArray<int32> Prev; Prev.Init(-1, N);
		TArray<FPQNode> PQ; PQ.Reserve(N / 4 + 8);
		Best[SrcIdx] = 0.0f;
		PQ.HeapPush(FPQNode{0.0f, SrcIdx});
		bool bReached = false;
		while (PQ.Num())
		{
			FPQNode Cur; PQ.HeapPop(Cur);
			if (Cur.Idx == DstIdx) { bReached = true; break; }
			if (Cur.Cost > Best[Cur.Idx]) { continue; }
			const int32 X = Cur.Idx % W, Y = Cur.Idx / W;
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					if (!Dx && !Dy) continue;
					const int32 Nx = X + Dx, Ny = Y + Dy;
					if (Nx < 0 || Ny < 0 || Nx >= W || Ny >= H) continue;
					const int32 V = Ny * W + Nx;
					if (!Mask[V]) continue;
					const float Step = (Dx == 0 || Dy == 0) ? 1.0f : 1.41421356f;
					const float Pen = 1.0f + K * (MaxDt - Dt[V]);
					const float NC = Cur.Cost + Step * Pen;
					if (NC < Best[V])
					{
						Best[V] = NC;
						Prev[V] = Cur.Idx;
						PQ.HeapPush(FPQNode{NC, V});
					}
				}
			}
		}
		if (!bReached) { return; }

		// Reconstruct Src → Dst.
		int32 Cursor = DstIdx;
		while (Cursor != -1)
		{
			OutPath.Add(Cursor);
			if (Cursor == SrcIdx) { break; }
			Cursor = Prev[Cursor];
		}
		Algo::Reverse(OutPath);
	}

	/** Resample a path keeping samples ~StepPx apart in pixel space. */
	void ResamplePath(const TArray<int32>& Path, int32 W, int32 StepPx, TArray<int32>& Out)
	{
		Out.Reset();
		if (Path.Num() == 0) { return; }
		Out.Add(Path[0]);
		float Acc = 0.0f;
		for (int32 I = 1; I < Path.Num(); ++I)
		{
			const int32 Y0 = Path[I - 1] / W, X0 = Path[I - 1] % W;
			const int32 Y1 = Path[I] / W,     X1 = Path[I] % W;
			const float Dy = float(Y1 - Y0), Dx = float(X1 - X0);
			Acc += FMath::Sqrt(Dy * Dy + Dx * Dx);
			if (Acc >= float(StepPx))
			{
				Out.Add(Path[I]);
				Acc = 0.0f;
			}
		}
		if (Out.Last() != Path.Last()) { Out.Add(Path.Last()); }
	}

	float PathLengthCells(const TArray<int32>& Path, int32 W)
	{
		float L = 0.0f;
		for (int32 I = 1; I < Path.Num(); ++I)
		{
			const int32 Y0 = Path[I - 1] / W, X0 = Path[I - 1] % W;
			const int32 Y1 = Path[I] / W,     X1 = Path[I] % W;
			const float Dy = float(Y1 - Y0), Dx = float(X1 - X0);
			L += FMath::Sqrt(Dy * Dy + Dx * Dx);
		}
		return L;
	}
}

TArray<FMapBlockoutRiver> MapBlockoutPipeline::ExtractRiverCenterlines(
	const FMapBlockoutMask& WaterMask, float WorldLo, float WorldHi, float MinLengthCm)
{
	TArray<FMapBlockoutRiver> Out;
	if (WaterMask.Width <= 0 || WaterMask.Height <= 0) { return Out; }
	if (WaterMask.Cells.Num() < WaterMask.Width * WaterMask.Height) { return Out; }
	if (WorldHi <= WorldLo) { return Out; }

	const int32 W = WaterMask.Width, H = WaterMask.Height;
	const float Span = WorldHi - WorldLo;
	const float CellSpanCm = Span / FMath::Max(1, W - 1);
	const float MetersPerPx = CellSpanCm * 0.01f;  // cm → m

	// 1) Largest 8-connected water component (drops disconnected ponds).
	TArray<uint8> CC = WaterMask.Cells;
	MapBlockoutMath::BinaryClosing(CC, W, H, 1);
	KeepLargestComponent8(CC, W, H);
	if (MapBlockoutMath::CountNonZero(CC) < 16) { return Out; }

	// 2) Distance transform: distance from each water cell to nearest non-water.
	// Our EDT measures distance TO the cells where the mask is non-zero, so to
	// get "distance to nearest non-water from each water cell" we invert.
	TArray<float> Dt;
	{
		TArray<uint8> Inverted; Inverted.SetNumUninitialized(W * H);
		for (int32 i = 0; i < W * H; ++i) { Inverted[i] = CC[i] ? 0 : 1; }
		MapBlockoutMath::DistanceTransformEDT(Inverted, W, H, Dt);
		// Zero out non-water cells (their distance to non-water is 0).
		for (int32 i = 0; i < W * H; ++i) { if (!CC[i]) { Dt[i] = 0.0f; } }
	}
	float MaxDt = 0.0f;
	for (float V : Dt) { MaxDt = FMath::Max(MaxDt, V); }
	if (MaxDt < 1.0f) { return Out; }

	// Pick any water cell to seed the first BFS.
	int32 SeedIdx = -1;
	for (int32 i = 0; i < W * H; ++i) { if (CC[i]) { SeedIdx = i; break; } }
	if (SeedIdx < 0) { return Out; }

	// 3) Two BFS passes for geodesic diameter endpoints.
	const int32 A = BFSFarthestCell(CC, W, H, SeedIdx);
	const int32 B = BFSFarthestCell(CC, W, H, A);
	if (A == B) { return Out; }

	// 4) Dijkstra A → B with medial-axis cost.
	constexpr float MedialK = 0.06f;
	TArray<int32> Main;
	DijkstraMedialPath(CC, W, H, Dt, MaxDt, A, B, MedialK, Main);
	if (Main.Num() < 2) { return Out; }

	// 5) Tributary: farthest cell from ANY main-spine cell. BFS from the union
	// of all main cells outward, find the farthest reached.
	TArray<int32> Trib;
	{
		TArray<int32> DistToMain; DistToMain.Init(-1, W * H);
		TArray<int32> Q; Q.Reserve(Main.Num() + 64);
		for (int32 I : Main) { DistToMain[I] = 0; Q.Add(I); }
		int32 TribTip = -1, TribDist = 0;
		int32 Head = 0;
		while (Head < Q.Num())
		{
			const int32 U = Q[Head++];
			const int32 X = U % W, Y = U / W;
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					if (!Dx && !Dy) continue;
					const int32 Nx = X + Dx, Ny = Y + Dy;
					if (Nx < 0 || Ny < 0 || Nx >= W || Ny >= H) continue;
					const int32 V = Ny * W + Nx;
					if (!CC[V] || DistToMain[V] >= 0) continue;
					DistToMain[V] = DistToMain[U] + 1;
					Q.Add(V);
					if (DistToMain[V] > TribDist) { TribDist = DistToMain[V]; TribTip = V; }
				}
			}
		}
		// Tributary is only worth emitting if it's substantially off the spine.
		if (TribTip >= 0 && TribDist > 40)
		{
			// Find the nearest main-spine cell to the tributary tip (Euclidean).
			const int32 TY = TribTip / W, TX = TribTip % W;
			int32 NearestMain = -1; int64 NearestSq = TNumericLimits<int64>::Max();
			for (int32 I : Main)
			{
				const int32 Y = I / W, X = I % W;
				const int64 D = int64(X - TX) * (X - TX) + int64(Y - TY) * (Y - TY);
				if (D < NearestSq) { NearestSq = D; NearestMain = I; }
			}
			if (NearestMain >= 0)
			{
				DijkstraMedialPath(CC, W, H, Dt, MaxDt, TribTip, NearestMain, MedialK, Trib);
			}
		}
	}

	// 6) Convert paths to world-coord polylines with per-point widths.
	auto EmitRiver = [&](const FString& Name, const TArray<int32>& Path) -> FMapBlockoutRiver
	{
		FMapBlockoutRiver River;
		River.Name = Name;
		TArray<int32> Resampled;
		ResamplePath(Path, W, /*StepPx=*/26, Resampled);
		River.Points.Reserve(Resampled.Num());
		for (int32 Idx : Resampled)
		{
			const int32 Y = Idx / W, X = Idx % W;
			FMapBlockoutRiverPoint RP;
			// Cell (col, row) → world (X east, Y north). Row 0 = north (mask convention).
			const float Wx = WorldLo + (Span * X) / FMath::Max(1, W - 1);
			const float Wy = WorldHi - (Span * Y) / FMath::Max(1, H - 1);
			RP.World = FVector2D(Wx, Wy);
			// Width = full diameter at this point. dt is "distance to bank" in cells.
			RP.WidthM = FMath::Max(1.0f, 2.0f * Dt[Idx] * MetersPerPx);
			River.Points.Add(RP);
		}
		return River;
	};

	const float MinLengthCells = MinLengthCm / FMath::Max(1.0f, CellSpanCm);
	if (PathLengthCells(Main, W) >= MinLengthCells)
	{
		Out.Add(EmitRiver(TEXT("Main"), Main));
	}
	if (Trib.Num() >= 2 && PathLengthCells(Trib, W) >= MinLengthCells * 0.5f)
	{
		Out.Add(EmitRiver(TEXT("Tributary"), Trib));
	}
	return Out;
}
