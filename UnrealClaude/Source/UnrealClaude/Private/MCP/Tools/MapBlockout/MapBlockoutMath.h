// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.
//
// Image-processing primitives the MapBlockout pipeline needs (numpy/scipy.ndimage equivalents),
// stdlib-only. Row-major TArrays; by convention row 0 = top (north) of the rendered image. (The
// landcover grid USTRUCT uses row 0 = south; conversion happens once at the export boundary.)

#pragma once

#include "CoreMinimal.h"

namespace MapBlockoutMath
{
	// Float fields (weight maps, normalized 0..1)
	void ResampleBilinear(const TArray<float>& Src, int32 SrcW, int32 SrcH, TArray<float>& OutDst, int32 DstW, int32 DstH);
	void GaussianBlur(TArray<float>& InOut, int32 W, int32 H, float Sigma);
	void FlipVertical(TArray<float>& InOut, int32 W, int32 H);
	void FlipVerticalU8(TArray<uint8>& InOut, int32 W, int32 H);

	// Binary masks (0/1 uint8)
	void Dilate(TArray<uint8>& InOut, int32 W, int32 H, int32 Radius);
	void Erode(TArray<uint8>& InOut, int32 W, int32 H, int32 Radius);
	void BinaryOpening(TArray<uint8>& InOut, int32 W, int32 H, int32 Radius);
	void BinaryClosing(TArray<uint8>& InOut, int32 W, int32 H, int32 Radius);
	void ComponentSizes(const TArray<int32>& Labels, int32 NumComponents, TArray<int32>& OutSizes);
	void GenerateNoiseField(TArray<float>& Out, int32 W, int32 H, int32 BlockSize, uint32 Seed);
	void DistanceTransformEDT(const TArray<uint8>& Mask, int32 W, int32 H, TArray<float>& OutDistance);
	int32 LabelConnectedComponents(const TArray<uint8>& Mask, int32 W, int32 H, TArray<int32>& OutLabels);
	void Skeletonize(TArray<uint8>& InOut, int32 W, int32 H);

	// Rasterization
	void RasterizeLine(TArray<uint8>& Mask, int32 W, int32 H, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 ThicknessRadius = 0, uint8 Value = 1);
	void RasterizeDisk(TArray<uint8>& Mask, int32 W, int32 H, int32 Cx, int32 Cy, int32 Radius, uint8 Value = 1);
	void RasterizePolyline(TArray<uint8>& Mask, int32 W, int32 H, const TArray<FIntPoint>& Points, int32 ThicknessRadius = 0, uint8 Value = 1);
	void RasterizePolygonFilled(TArray<uint8>& Mask, int32 W, int32 H, const TArray<FIntPoint>& Polygon, uint8 Value = 1);

	// Stats
	int32 CountNonZero(const TArray<uint8>& Mask);
	float Coverage(const TArray<uint8>& Mask);
	bool MasksOverlap(const TArray<uint8>& A, const TArray<uint8>& B);
	void MaskAnd(const TArray<uint8>& A, const TArray<uint8>& B, TArray<uint8>& OutResult);
	void MaskAndNot(const TArray<uint8>& A, const TArray<uint8>& B, TArray<uint8>& OutResult);
	void MaskOr(const TArray<uint8>& A, const TArray<uint8>& B, TArray<uint8>& OutResult);
}
