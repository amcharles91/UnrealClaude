// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.
//
// Image I/O + bitmap drawing helpers for the MapBlockout snapshot renderer. RGBA8 buffers stored
// row-major (row 0 = top). PNG encode/decode via IImageWrapperModule (ImageWrapper module, linked).

#pragma once

#include "CoreMinimal.h"

namespace MapBlockoutImage
{
	/** Authoritative MapDesigner color chart. */
	namespace Colors
	{
		extern const FColor MainRoad;
		extern const FColor DirtRoad;
		extern const FColor Treeline;
		extern const FColor Forest;
		extern const FColor Building;
		extern const FColor Bridge;
		extern const FColor Railway;
		extern const FColor POIBoundary;
		extern const FColor Field;
		extern const FColor Scrub;
		extern const FColor River;
		extern const FColor Background;
		extern const FColor Panel;
		extern const FColor Grid;
		extern const FColor Ink;
	}

	void NewBitmap(TArray<FColor>& OutBitmap, int32 Width, int32 Height, FColor Fill);
	void DrawLine(TArray<FColor>& Bitmap, int32 W, int32 H, int32 X0, int32 Y0, int32 X1, int32 Y1, FColor Color, int32 ThicknessRadius = 0);
	void DrawPolyline(TArray<FColor>& Bitmap, int32 W, int32 H, const TArray<FIntPoint>& Points, FColor Color, int32 ThicknessRadius = 0);
	void DrawDisk(TArray<FColor>& Bitmap, int32 W, int32 H, int32 Cx, int32 Cy, int32 Radius, FColor Color);
	void DrawCircleOutline(TArray<FColor>& Bitmap, int32 W, int32 H, int32 Cx, int32 Cy, int32 Radius, FColor Color, int32 Thickness = 2);
	void DrawRect(TArray<FColor>& Bitmap, int32 W, int32 H, int32 X0, int32 Y0, int32 X1, int32 Y1, FColor Color);
	void OverlayMask(TArray<FColor>& Bitmap, int32 W, int32 H, const TArray<uint8>& Mask, FColor Color);
	void RenderHeatmap(const TArray<float>& Density, int32 W, int32 H, TArray<FColor>& OutBitmap);

	/** One entry in the color key (a color swatch + text label). Style: "swatch"|"line"|"ring". */
	struct FKeyEntry
	{
		FColor Color;
		FString Label;
		FString Style;
		FKeyEntry(FColor InColor, const FString& InLabel, const FString& InStyle = TEXT("swatch"))
			: Color(InColor), Label(InLabel), Style(InStyle) {}
	};

	int32 DrawColorKey(TArray<FColor>& Bitmap, int32 W, int32 H, int32 PanelLeft, int32 PanelTop, int32 PanelWidth, const FString& Title, const TArray<FKeyEntry>& Entries);
	void DrawText5x7(TArray<FColor>& Bitmap, int32 W, int32 H, int32 X, int32 Y, const FString& Text, FColor Color, int32 Scale = 2);
	bool WritePNG(const TArray<FColor>& Bitmap, int32 W, int32 H, const FString& OutputPath);
	bool LoadGrayscalePNG(const FString& FilePath, TArray<uint8>& OutPixels, int32& OutW, int32& OutH);
}
