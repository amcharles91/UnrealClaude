// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.
//
// Materializers: turn the blockout plan into actual engine geometry.
// VibeUE's original routed each materializer through its own UFUNCTION services
// (ULandscapeService::CreateSplineFromPoints / PaintLayerAtLocation / GetLandscapeInfo,
// UFoliageService::AddFoliageInstances). Here we sample the engine APIs directly,
// reusing the exact patterns from MCPTool_Landscape.cpp (splines + paint) and
// MCPTool_Foliage.cpp (IFA scatter), so this file has no service dependency.

#include "MapBlockoutPipeline.h"
#include "MapBlockoutTypes.h"

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"

#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEditLayer.h"   // ULandscapeEditLayerBase complete type (Landscape.h only forward-declares it)
#include "ScopedTransaction.h"
#endif // WITH_EDITOR

#if WITH_EDITOR
// =========================================================================
// File-local helpers (mirror MCPTool_Landscape.cpp / MCPTool_Foliage.cpp).
// =========================================================================
namespace
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	/** Resolve the target ALandscape by actor label or object name (matches MCPTool_Landscape). */
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

	/**
	 * Resolve a valid editing-layer GUID. On edit-layer landscapes GetEditingLayer() can be
	 * invalid because nothing called SetEditingLayer(); fall back to the first edit layer.
	 * (Copied from MCPTool_Landscape.cpp::ResolveEditLayerGuid.)
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

	UStaticMesh* LoadStaticMesh(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		return LoadObject<UStaticMesh>(nullptr, *Path);
	}

	/** Apply VibeUE's OrganizeActorIntoFolder: set label + folder path. */
	void OrganizeActorIntoFolder(AActor* Actor, const FString& FolderPath, const FString& Label)
	{
		if (!Actor)
		{
			return;
		}
		Actor->SetActorLabel(Label);
		if (!FolderPath.IsEmpty())
		{
			Actor->SetFolderPath(FName(*FolderPath));
		}
	}

	/**
	 * Build one landscape spline along a world-XY polyline (Z resolved from landscape height,
	 * falling back to 0). Returns the number of control points created (== Locations.Num()),
	 * or 0 on failure. Mirrors MCPTool_Landscape.cpp::OpAddSpline exactly.
	 */
	int32 CreateSplineFromPoints(ALandscape* Landscape, const TArray<FVector2D>& Points, float HalfWidth)
	{
		if (!Landscape || Points.Num() < 2)
		{
			return 0;
		}

		Landscape->Modify();
		if (!Landscape->GetSplinesComponent())
		{
			Landscape->CreateSplineComponent();
		}
		ULandscapeSplinesComponent* Splines = Landscape->GetSplinesComponent();
		if (!Splines)
		{
			return 0;
		}
		Splines->Modify();

		const FTransform ActorXform = Landscape->GetActorTransform();

		// Control points store Location in landscape-local space. Z is taken as 0 (matching
		// MCPTool_Landscape::OpAddSpline and the VibeUE source); the spline's bRaise/bLowerTerrain
		// conform the segments to terrain, so a precise per-point Z is unnecessary here.
		TArray<ULandscapeSplineControlPoint*> NewPoints;
		NewPoints.Reserve(Points.Num());
		for (const FVector2D& P : Points)
		{
			const FVector LocalLoc = ActorXform.InverseTransformPosition(FVector(P.X, P.Y, 0.0));

			ULandscapeSplineControlPoint* CP = NewObject<ULandscapeSplineControlPoint>(Splines, NAME_None, RF_Transactional);
			CP->Location = LocalLoc;
			CP->Width = HalfWidth;
			CP->SideFalloff = 500.0f;
			CP->EndFalloff = 500.0f;
			CP->LayerName = NAME_None;
			CP->bRaiseTerrain = true;
			CP->bLowerTerrain = true;
			Splines->GetControlPoints().Add(CP);
			NewPoints.Add(CP);
		}

		auto MakeSegment = [&](ULandscapeSplineControlPoint* A, ULandscapeSplineControlPoint* B)
		{
			ULandscapeSplineSegment* Seg = NewObject<ULandscapeSplineSegment>(Splines, NAME_None, RF_Transactional);
			Seg->Connections[0].ControlPoint = A;
			Seg->Connections[1].ControlPoint = B;
			Seg->LayerName = NAME_None;
			Splines->GetSegments().Add(Seg);
			A->ConnectedSegments.Add(FLandscapeSplineConnection(Seg, 0));
			B->ConnectedSegments.Add(FLandscapeSplineConnection(Seg, 1));
			Seg->AutoFlipTangents();
		};

		for (int32 i = 0; i < NewPoints.Num() - 1; ++i)
		{
			MakeSegment(NewPoints[i], NewPoints[i + 1]);
		}

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

		return NewPoints.Num();
	}

	/** Find a UFoliageType already registered in the IFA (by foliage-type path or static-mesh path). */
	UFoliageType* FindFoliageTypeInIFA(const FString& MeshOrFoliageTypePath, AInstancedFoliageActor* IFA)
	{
		if (!IFA)
		{
			return nullptr;
		}

		UObject* LoadedAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *MeshOrFoliageTypePath);
		const TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliageInfos = IFA->GetFoliageInfos();

		if (UFoliageType* FT = Cast<UFoliageType>(LoadedAsset))
		{
			if (FoliageInfos.Contains(FT))
			{
				return FT;
			}
		}

		UStaticMesh* Mesh = Cast<UStaticMesh>(LoadedAsset);
		if (!Mesh)
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, *MeshOrFoliageTypePath);
		}
		if (Mesh)
		{
			for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : FoliageInfos)
			{
				UFoliageType_InstancedStaticMesh* ISMT = Cast<UFoliageType_InstancedStaticMesh>(Pair.Key);
				if (ISMT && ISMT->GetStaticMesh() == Mesh)
				{
					return Pair.Key;
				}
			}
		}
		return nullptr;
	}

	/** Resolve (or transiently create + register) a foliage type for a mesh/foliage-type path. */
	UFoliageType* FindOrCreateFoliageTypeForMesh(const FString& MeshOrFoliageTypePath, AInstancedFoliageActor* IFA)
	{
		if (!IFA || MeshOrFoliageTypePath.IsEmpty())
		{
			return nullptr;
		}

		if (UFoliageType* Existing = FindFoliageTypeInIFA(MeshOrFoliageTypePath, IFA))
		{
			return Existing;
		}

		if (UFoliageType* FoliageType = LoadObject<UFoliageType>(nullptr, *MeshOrFoliageTypePath))
		{
			return IFA->AddFoliageType(FoliageType);
		}

		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshOrFoliageTypePath);
		if (!Mesh)
		{
			return nullptr;
		}

		UFoliageType_InstancedStaticMesh* NewFT = NewObject<UFoliageType_InstancedStaticMesh>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		NewFT->SetStaticMesh(Mesh);
		return IFA->AddFoliageType(NewFT);
	}

	/** Vertical line trace to find the surface under (X, Y). */
	bool TraceToSurface(UWorld* World, float X, float Y, FVector& OutLocation, FVector& OutNormal)
	{
		if (!World)
		{
			return false;
		}
		const FVector Start(X, Y, 100000.0f);
		const FVector End(X, Y, -100000.0f);

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.bTraceComplex = false;
		QueryParams.bReturnPhysicalMaterial = false;

		if (World->LineTraceSingleByChannel(HitResult, Start, End, ECC_WorldStatic, QueryParams))
		{
			OutLocation = HitResult.ImpactPoint;
			OutNormal = HitResult.ImpactNormal;
			return true;
		}
		return false;
	}

	/** Build one FFoliageInstance with random uniform scale + yaw and optional normal alignment. */
	FFoliageInstance MakeInstance(const FVector& Location, const FVector& Normal,
		float MinScale, float MaxScale, bool bAlignToNormal, bool bRandomYaw, FRandomStream& RNG)
	{
		FFoliageInstance Instance;
		Instance.Location = Location;

		const float Scale = RNG.FRandRange(MinScale, MaxScale);
		Instance.DrawScale3D = FVector3f(Scale, Scale, Scale);

		FRotator Rot = FRotator::ZeroRotator;
		if (bRandomYaw)
		{
			Rot.Yaw = RNG.FRandRange(0.0f, 360.0f);
		}
		if (bAlignToNormal)
		{
			const FQuat NormalQuat = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
			const FQuat YawQuat(FVector::UpVector, FMath::DegreesToRadians(Rot.Yaw));
			Rot = (NormalQuat * YawQuat).Rotator();
		}
		Instance.Rotation = Rot;
		Instance.PreAlignRotation = Instance.Rotation;
		Instance.Flags = 0;
		Instance.ZOffset = 0.0f;
		return Instance;
	}

	/**
	 * Scatter foliage of one type at a set of world-XY positions, tracing each to the surface.
	 * Mirrors MCPTool_Foliage.cpp scatter/commit. Returns instances actually added.
	 */
	int32 ScatterFoliageAtPositions(UWorld* World, AInstancedFoliageActor* IFA, const FString& FoliageTypePath,
		const TArray<FVector>& Positions, float MinScale, float MaxScale, FRandomStream& RNG)
	{
		if (!World || !IFA || Positions.Num() == 0)
		{
			return 0;
		}

		UFoliageType* FoliageType = FindOrCreateFoliageTypeForMesh(FoliageTypePath, IFA);
		if (!FoliageType)
		{
			return 0;
		}

		IFA->Modify();

		TArray<FFoliageInstance> NewInstances;
		NewInstances.Reserve(Positions.Num());
		for (const FVector& Pos : Positions)
		{
			FVector HitLocation, HitNormal;
			if (!TraceToSurface(World, (float)Pos.X, (float)Pos.Y, HitLocation, HitNormal))
			{
				// No collision surface under the point — fall back to the planar XY at Z=0
				// so the instance still lands (the blockout terrain may lack collision yet).
				HitLocation = FVector(Pos.X, Pos.Y, 0.0);
				HitNormal = FVector::UpVector;
			}
			NewInstances.Add(MakeInstance(HitLocation, HitNormal, MinScale, MaxScale,
				/*bAlignToNormal=*/true, /*bRandomYaw=*/true, RNG));
		}

		if (NewInstances.Num() == 0)
		{
			return 0;
		}

		FFoliageInfo* FoliageInfo = IFA->FindInfo(FoliageType);
		if (!FoliageInfo)
		{
			return 0;
		}
		TArray<const FFoliageInstance*> InstancePtrs;
		InstancePtrs.Reserve(NewInstances.Num());
		for (const FFoliageInstance& Inst : NewInstances)
		{
			InstancePtrs.Add(&Inst);
		}
		FoliageInfo->AddInstances(FoliageType, InstancePtrs);
		IFA->MarkPackageDirty();
		return NewInstances.Num();
	}

	/**
	 * Sample a binary mask into a list of world-XY positions, stepping by Step cells.
	 * Faithful to VibeUE's SampleMaskPositions: Row 0 = north edge (Wy decreases with Y).
	 */
	void SampleMaskPositions(const FMapBlockoutMask& Mask, float WorldLo, float WorldHi,
		int32 Step, TArray<FVector>& OutPositions)
	{
		const int32 W = Mask.Width;
		const int32 H = Mask.Height;
		if (W <= 0 || H <= 0 || Mask.Cells.Num() < W * H || WorldHi <= WorldLo || Step < 1)
		{
			return;
		}
		const float Span = WorldHi - WorldLo;
		for (int32 Y = 0; Y < H; Y += Step)
		{
			for (int32 X = 0; X < W; X += Step)
			{
				if (!Mask.Cells[Y * W + X])
				{
					continue;
				}
				const float Wx = WorldLo + (Span * X) / FMath::Max(1, W - 1);
				const float Wy = WorldHi - (Span * Y) / FMath::Max(1, H - 1);
				OutPositions.Add(FVector(Wx, Wy, 0.0f));
			}
		}
	}
} // namespace
#endif // WITH_EDITOR

namespace MapBlockoutPipeline
{
	// =====================================================================
	// Stage 1 — Roads as Landscape Splines
	// =====================================================================
	FMapBlockoutMaterializeResult MaterializeRoadsAsSplines(
		const FMapBlockoutRoadNetworkResult& Roads, const FString& LandscapeLabel)
	{
#if WITH_EDITOR
		FMapBlockoutMaterializeResult Result;
		if (!Roads.bSuccess || !Roads.Gate.bAllPassed)
		{
			Result.ErrorMessage = TEXT("MaterializeRoadsAsSplines: Roads stage has not passed.");
			return Result;
		}
		if (LandscapeLabel.IsEmpty())
		{
			Result.ErrorMessage = TEXT("MaterializeRoadsAsSplines: empty LandscapeLabel.");
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result.ErrorMessage = TEXT("MaterializeRoadsAsSplines: no editor world.");
			return Result;
		}
		ALandscape* Landscape = ResolveLandscape(World, LandscapeLabel);
		if (!Landscape)
		{
			Result.ErrorMessage = FString::Printf(TEXT("MaterializeRoadsAsSplines: landscape '%s' not found."), *LandscapeLabel);
			return Result;
		}

		FScopedTransaction Transaction(NSLOCTEXT("MapBlockout", "MaterializeRoads", "Materialize Roads As Splines"));

		int32 Created = 0;
		for (const FMapBlockoutRoad& R : Roads.Roads)
		{
			if (R.Points.Num() < 2)
			{
				continue;
			}
			Created += CreateSplineFromPoints(Landscape, R.Points, R.WidthCm * 0.5f);
		}

		Result.CreatedCount = Created;
		Result.bSuccess = (Created > 0);
		if (!Result.bSuccess)
		{
			Result.ErrorMessage = TEXT("MaterializeRoadsAsSplines: no spline control points created.");
		}
		return Result;
#else
		FMapBlockoutMaterializeResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("MaterializeRoadsAsSplines requires an editor build.");
		return Result;
#endif // WITH_EDITOR
	}

	// =====================================================================
	// Stage 3 — Fields as Landscape Paint
	// =====================================================================
	FMapBlockoutMaterializeResult MaterializeFieldsAsPaint(
		const FMapBlockoutFieldResult& Fields, const FString& LandscapeLabel, const FString& LayerName)
	{
#if WITH_EDITOR
		FMapBlockoutMaterializeResult Result;
		if (!Fields.bSuccess || !Fields.Gate.bAllPassed)
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: Fields stage has not passed.");
			return Result;
		}
		if (LandscapeLabel.IsEmpty() || LayerName.IsEmpty())
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: missing LandscapeLabel or LayerName.");
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: no editor world.");
			return Result;
		}
		ALandscape* Landscape = ResolveLandscape(World, LandscapeLabel);
		if (!Landscape)
		{
			Result.ErrorMessage = FString::Printf(TEXT("MaterializeFieldsAsPaint: landscape '%s' not found."), *LandscapeLabel);
			return Result;
		}
		ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
		if (!LandscapeInfo)
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: landscape has no ULandscapeInfo.");
			return Result;
		}

		// Resolve the target paint layer by name. Honest failure if absent.
		ULandscapeLayerInfoObject* LayerInfo = LandscapeInfo->GetLayerInfoByName(FName(*LayerName));
		if (!LayerInfo)
		{
			FString Available;
			for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
			{
				if (!Available.IsEmpty()) { Available += TEXT(", "); }
				Available += LayerSettings.GetLayerName().ToString();
			}
			Result.ErrorMessage = FString::Printf(
				TEXT("MaterializeFieldsAsPaint: layer '%s' not found on landscape. Available: [%s]."),
				*LayerName, Available.IsEmpty() ? TEXT("(none)") : *Available);
			return Result;
		}

		const int32 W = Fields.FieldMask.Width;
		const int32 H = Fields.FieldMask.Height;
		const TArray<uint8>& Mask = Fields.FieldMask.Cells;
		if (W <= 0 || H <= 0 || Mask.Num() < W * H)
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: empty/invalid field mask.");
			return Result;
		}

		// World bounds (origin-centred square, as the rest of the pipeline assumes).
		int32 MinLX = 0, MinLY = 0, MaxLX = 0, MaxLY = 0;
		if (!LandscapeInfo->GetLandscapeExtent(MinLX, MinLY, MaxLX, MaxLY))
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: failed to query landscape extent.");
			return Result;
		}
		const FVector Scale = Landscape->GetActorScale3D();
		const float HalfSpanX = (MaxLX - MinLX) * Scale.X * 0.5f;
		const float HalfSpanY = (MaxLY - MinLY) * Scale.Y * 0.5f;
		const float Half = FMath::Max(HalfSpanX, HalfSpanY);
		const float WorldLo = -Half;
		const float WorldHi = +Half;
		const float Span = WorldHi - WorldLo;

		const double QuadCm = Scale.X;
		if (FMath::IsNearlyZero(QuadCm))
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: landscape X scale is zero.");
			return Result;
		}

		// Paint in big tiles (VibeUE: ~80 brush taps along each axis) so we don't write per pixel.
		const int32 Step = FMath::Max(2, FMath::Min(W, H) / 80);
		const float CellSpan = Span / FMath::Max(1, W - 1);
		const float BrushRadius = CellSpan * Step;
		const double RadiusVerts = BrushRadius / QuadCm;

		FScopedTransaction Transaction(NSLOCTEXT("MapBlockout", "MaterializeFields", "Materialize Fields As Paint"));
		Landscape->Modify();

		const FGuid EditLayerGuid = ResolveEditLayerGuid(Landscape);
		FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo, EditLayerGuid);
		const FTransform ActorXform = Landscape->GetActorTransform();

		int32 Painted = 0;
		for (int32 Y = 0; Y < H; Y += Step)
		{
			for (int32 X = 0; X < W; X += Step)
			{
				if (!Mask[Y * W + X])
				{
					continue;
				}
				// VibeUE mapping: row 0 = north (Wy decreases with Y).
				const float Wx = WorldLo + (Span * X) / FMath::Max(1, W - 1);
				const float Wy = WorldHi - (Span * Y) / FMath::Max(1, H - 1);

				// World XY -> landscape vertex (quad) space.
				const FVector Local = ActorXform.InverseTransformPosition(FVector(Wx, Wy, 0.0));
				const FVector2D Center(Local.X, Local.Y);

				int32 X1 = FMath::Clamp(FMath::FloorToInt(Center.X - RadiusVerts), MinLX, MaxLX);
				int32 Y1 = FMath::Clamp(FMath::FloorToInt(Center.Y - RadiusVerts), MinLY, MaxLY);
				int32 X2 = FMath::Clamp(FMath::CeilToInt(Center.X + RadiusVerts), MinLX, MaxLX);
				int32 Y2 = FMath::Clamp(FMath::CeilToInt(Center.Y + RadiusVerts), MinLY, MaxLY);
				if (X2 < X1 || Y2 < Y1)
				{
					continue;
				}
				const int32 RegionW = X2 - X1 + 1;
				const int32 RegionH = Y2 - Y1 + 1;

				// Read-modify-write weights (GetWeightData mutates its corner args by reference).
				TArray<uint8> Weights;
				Weights.SetNumZeroed(RegionW * RegionH);
				int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
				LandscapeEdit.GetWeightData(LayerInfo, GX1, GY1, GX2, GY2, Weights.GetData(), 0);

				for (int32 LY = 0; LY < RegionH; ++LY)
				{
					for (int32 LX = 0; LX < RegionW; ++LX)
					{
						const double VX = (double)(X1 + LX);
						const double VY = (double)(Y1 + LY);
						const double Dist = FVector2D::Distance(FVector2D(VX, VY), Center);
						if (Dist > RadiusVerts)
						{
							continue;
						}
						// Linear falloff, target weight 1.0 (VibeUE strength = 1.0).
						const float FalloffW = 1.0f - (float)(Dist / FMath::Max(1.0, RadiusVerts));
						const int32 Idx = LY * RegionW + LX;
						const float Existing = (float)Weights[Idx] / 255.0f;
						const float NewW = FMath::Lerp(Existing, 1.0f, FalloffW);
						Weights[Idx] = (uint8)FMath::Clamp(FMath::RoundToInt(NewW * 255.0f), 0, 255);
					}
				}

				LandscapeEdit.SetAlphaData(LayerInfo, X1, Y1, X2, Y2, Weights.GetData(), 0,
					ELandscapeLayerPaintingRestriction::None);
				++Painted;
			}
		}

		LandscapeEdit.Flush();
		Landscape->RequestLayersContentUpdateForceAll();
		Landscape->MarkPackageDirty();
		if (ULevel* Level = Landscape->GetLevel())
		{
			Level->MarkPackageDirty();
		}

		Result.CreatedCount = Painted;
		Result.bSuccess = (Painted > 0);
		if (!Result.bSuccess)
		{
			Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint: no cells painted (mask empty over play area).");
		}
		return Result;
#else
		FMapBlockoutMaterializeResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("MaterializeFieldsAsPaint requires an editor build.");
		return Result;
#endif // WITH_EDITOR
	}

	// =====================================================================
	// Stage 2 — POIs as placeholder actors
	// =====================================================================
	FMapBlockoutMaterializeResult MaterializePoisAsActors(
		const FMapBlockoutPOIResult& Pois, const FString& FolderPath, const FString& BuildingMeshPath)
	{
#if WITH_EDITOR
		FMapBlockoutMaterializeResult Result;
		if (!Pois.bSuccess || !Pois.Gate.bAllPassed)
		{
			Result.ErrorMessage = TEXT("MaterializePoisAsActors: Pois stage has not passed.");
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result.ErrorMessage = TEXT("MaterializePoisAsActors: no editor world.");
			return Result;
		}

		// A building mesh path was supplied but failed to load: honest error (no silent fallback).
		UStaticMesh* BuildingMesh = nullptr;
		if (!BuildingMeshPath.IsEmpty())
		{
			BuildingMesh = LoadStaticMesh(BuildingMeshPath);
			if (!BuildingMesh)
			{
				Result.ErrorMessage = FString::Printf(
					TEXT("MaterializePoisAsActors: could not load building mesh '%s'."), *BuildingMeshPath);
				return Result;
			}
		}

		const FString PoiFolder = FolderPath.IsEmpty() ? TEXT("MapBlockout/Pois") : FolderPath;

		FScopedTransaction Transaction(NSLOCTEXT("MapBlockout", "MaterializePois", "Materialize POIs As Actors"));

		int32 Spawned = 0;
		for (const FMapBlockoutPOI& POI : Pois.Pois)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			// Parent placeholder: plain AActor at POI center with a label + folder.
			AActor* Parent = World->SpawnActor<AActor>(AActor::StaticClass(),
				FVector(POI.Center.X, POI.Center.Y, 0.0f), FRotator::ZeroRotator, Params);
			if (!Parent)
			{
				continue;
			}
			OrganizeActorIntoFolder(Parent, PoiFolder, FString::Printf(TEXT("POI_%s"), *POI.Name));
			Parent->MarkPackageDirty();
			++Spawned;

			// Buildings: AStaticMeshActor (if a mesh was given) per building footprint.
			for (int32 I = 0; I < POI.Buildings.Num(); ++I)
			{
				const FMapBlockoutBuilding& B = POI.Buildings[I];
				AStaticMeshActor* Bld = World->SpawnActor<AStaticMeshActor>(
					FVector(B.World.X, B.World.Y, 0.0f),
					FRotator(0.0f, B.YawDegrees, 0.0f),
					Params);
				if (!Bld)
				{
					continue;
				}
				Bld->SetMobility(EComponentMobility::Static);
				if (BuildingMesh && Bld->GetStaticMeshComponent())
				{
					Bld->GetStaticMeshComponent()->SetStaticMesh(BuildingMesh);
					// Scale to footprint (mesh assumed ~unit-cube; 50cm reference half-extent).
					const FVector BScale(
						FMath::Max(0.1f, B.HalfExtents.X / 50.0f),
						FMath::Max(0.1f, B.HalfExtents.Y / 50.0f),
						1.0f);
					Bld->SetActorScale3D(BScale);
				}
				OrganizeActorIntoFolder(Bld, PoiFolder,
					FString::Printf(TEXT("%s_Building_%d"), *POI.Name, I + 1));
				Bld->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
				Bld->MarkPackageDirty();
				++Spawned;
			}
		}

		Result.CreatedCount = Spawned;
		Result.bSuccess = (Spawned > 0);
		if (!Result.bSuccess)
		{
			Result.ErrorMessage = TEXT("MaterializePoisAsActors: no actors spawned.");
		}
		return Result;
#else
		FMapBlockoutMaterializeResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("MaterializePoisAsActors requires an editor build.");
		return Result;
#endif // WITH_EDITOR
	}

	// =====================================================================
	// Stage 4 — Forest / Treeline / Scrub as Foliage
	// =====================================================================
	FMapBlockoutMaterializeResult MaterializeForestAsFoliage(
		const FMapBlockoutFoliageResult& Foliage,
		float WorldLo,
		float WorldHi,
		const FString& ForestFoliageTypePath,
		const FString& TreelineFoliageTypePath,
		const FString& ScrubFoliageTypePath)
	{
#if WITH_EDITOR
		FMapBlockoutMaterializeResult Result;
		if (!Foliage.bSuccess || !Foliage.Gate.bAllPassed)
		{
			Result.ErrorMessage = TEXT("MaterializeForestAsFoliage: Foliage stage has not passed.");
			return Result;
		}
		if (ForestFoliageTypePath.IsEmpty() && TreelineFoliageTypePath.IsEmpty() && ScrubFoliageTypePath.IsEmpty())
		{
			Result.ErrorMessage = TEXT("MaterializeForestAsFoliage: no foliage type paths provided.");
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result.ErrorMessage = TEXT("MaterializeForestAsFoliage: no editor world.");
			return Result;
		}

		AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, /*bCreateIfNone=*/true);
		if (!IFA)
		{
			Result.ErrorMessage = TEXT("MaterializeForestAsFoliage: failed to get or create InstancedFoliageActor.");
			return Result;
		}

		// Play-area world bounds, passed in from the landcover grid (Grid.WorldLo/WorldHi) so foliage
		// lands at the correct world XY for the real landscape size. Fall back to a 1.6 km half-span
		// only if the caller couldn't supply bounds (degenerate WorldHi<=WorldLo).
		float Lo = WorldLo;
		float Hi = WorldHi;
		if (!(Hi > Lo))
		{
			Lo = -80000.0f;
			Hi = +80000.0f;
		}

		// Honest failure tracking: if a non-empty path resolves to no foliage type, report it.
		FString MissingTypes;
		auto NoteMissing = [&MissingTypes](const FString& Path)
		{
			if (!MissingTypes.IsEmpty()) { MissingTypes += TEXT(", "); }
			MissingTypes += Path;
		};

		FScopedTransaction Transaction(NSLOCTEXT("MapBlockout", "MaterializeForest", "Materialize Forest As Foliage"));

		FRandomStream RNG(7);
		int32 Created = 0;

		const int32 ForestStep = 4;   // dense
		const int32 TreelineStep = 5; // medium
		const int32 ScrubStep = 6;    // sparse

		if (!ForestFoliageTypePath.IsEmpty())
		{
			if (FindOrCreateFoliageTypeForMesh(ForestFoliageTypePath, IFA) == nullptr)
			{
				NoteMissing(ForestFoliageTypePath);
			}
			else
			{
				TArray<FVector> Pos;
				SampleMaskPositions(Foliage.ForestMask, Lo, Hi, ForestStep, Pos);
				Created += ScatterFoliageAtPositions(World, IFA, ForestFoliageTypePath, Pos, 0.8f, 1.4f, RNG);
			}
		}
		if (!TreelineFoliageTypePath.IsEmpty())
		{
			if (FindOrCreateFoliageTypeForMesh(TreelineFoliageTypePath, IFA) == nullptr)
			{
				NoteMissing(TreelineFoliageTypePath);
			}
			else
			{
				TArray<FVector> Pos;
				SampleMaskPositions(Foliage.TreelineMask, Lo, Hi, TreelineStep, Pos);
				Created += ScatterFoliageAtPositions(World, IFA, TreelineFoliageTypePath, Pos, 0.7f, 1.2f, RNG);
			}
		}
		if (!ScrubFoliageTypePath.IsEmpty())
		{
			if (FindOrCreateFoliageTypeForMesh(ScrubFoliageTypePath, IFA) == nullptr)
			{
				NoteMissing(ScrubFoliageTypePath);
			}
			else
			{
				TArray<FVector> Pos;
				SampleMaskPositions(Foliage.ScrubMask, Lo, Hi, ScrubStep, Pos);
				Created += ScatterFoliageAtPositions(World, IFA, ScrubFoliageTypePath, Pos, 0.6f, 1.1f, RNG);
			}
		}

		IFA->MarkPackageDirty();

		Result.CreatedCount = Created;
		Result.bSuccess = (Created > 0);
		if (!Result.bSuccess)
		{
			if (!MissingTypes.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(
					TEXT("MaterializeForestAsFoliage: could not resolve foliage type(s): [%s]."), *MissingTypes);
			}
			else
			{
				Result.ErrorMessage = TEXT("MaterializeForestAsFoliage: no instances created (masks empty over play area).");
			}
		}
		return Result;
#else
		FMapBlockoutMaterializeResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("MaterializeForestAsFoliage requires an editor build.");
		return Result;
#endif // WITH_EDITOR
	}

	// =====================================================================
	// Stage 5 — Railway (splines) + Bridges (actors)
	// =====================================================================
	FMapBlockoutMaterializeResult MaterializeRailwayAndBridges(
		const FMapBlockoutRailwayResult& Railway, const FString& LandscapeLabel, const FString& BridgeMeshPath)
	{
#if WITH_EDITOR
		FMapBlockoutMaterializeResult Result;
		if (!Railway.bSuccess || !Railway.Gate.bAllPassed)
		{
			Result.ErrorMessage = TEXT("MaterializeRailwayAndBridges: Railway stage has not passed.");
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result.ErrorMessage = TEXT("MaterializeRailwayAndBridges: no editor world.");
			return Result;
		}

		// A bridge mesh path was supplied but failed to load: honest error.
		UStaticMesh* BridgeMesh = nullptr;
		if (!BridgeMeshPath.IsEmpty())
		{
			BridgeMesh = LoadStaticMesh(BridgeMeshPath);
			if (!BridgeMesh)
			{
				Result.ErrorMessage = FString::Printf(
					TEXT("MaterializeRailwayAndBridges: could not load bridge mesh '%s'."), *BridgeMeshPath);
				return Result;
			}
		}

		FScopedTransaction Transaction(NSLOCTEXT("MapBlockout", "MaterializeRailway", "Materialize Railway And Bridges"));

		int32 Created = 0;

		// Rail polylines -> landscape splines (only if a landscape was named).
		if (!LandscapeLabel.IsEmpty())
		{
			ALandscape* Landscape = ResolveLandscape(World, LandscapeLabel);
			if (!Landscape)
			{
				Result.ErrorMessage = FString::Printf(
					TEXT("MaterializeRailwayAndBridges: landscape '%s' not found."), *LandscapeLabel);
				return Result;
			}
			for (const FMapBlockoutRoad& R : Railway.RailLines)
			{
				if (R.Points.Num() < 2)
				{
					continue;
				}
				Created += CreateSplineFromPoints(Landscape, R.Points, R.WidthCm * 0.5f);
			}
		}

		// Bridges -> AStaticMeshActor placeholders at bridge midpoints.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		int32 BridgeIdx = 0;
		for (const FMapBlockoutBridge& B : Railway.Bridges)
		{
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
				FVector(B.World.X, B.World.Y, 0.0f),
				FRotator(0.0f, B.YawDegrees, 0.0f), Params);
			if (!Actor)
			{
				continue;
			}
			Actor->SetMobility(EComponentMobility::Static);
			if (BridgeMesh && Actor->GetStaticMeshComponent())
			{
				Actor->GetStaticMeshComponent()->SetStaticMesh(BridgeMesh);
				Actor->SetActorScale3D(FVector(FMath::Max(0.5f, B.LengthCm / 100.0f), 1.0f, 1.0f));
			}
			const TCHAR* CarryName =
				(B.Carries == EMapBlockoutRoadType::Railway) ? TEXT("Rail") :
				(B.Carries == EMapBlockoutRoadType::Main)    ? TEXT("Main") : TEXT("Dirt");
			OrganizeActorIntoFolder(Actor, TEXT("MapBlockout/Bridges"),
				FString::Printf(TEXT("Bridge_%s_%d"), CarryName, ++BridgeIdx));
			Actor->MarkPackageDirty();
			++Created;
		}

		Result.CreatedCount = Created;
		Result.bSuccess = (Created > 0);
		if (!Result.bSuccess)
		{
			Result.ErrorMessage = TEXT("MaterializeRailwayAndBridges: nothing created.");
		}
		return Result;
#else
		FMapBlockoutMaterializeResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("MaterializeRailwayAndBridges requires an editor build.");
		return Result;
#endif // WITH_EDITOR
	}
} // namespace MapBlockoutPipeline
