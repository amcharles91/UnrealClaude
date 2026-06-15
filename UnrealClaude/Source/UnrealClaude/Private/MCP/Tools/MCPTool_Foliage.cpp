// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Foliage.h"

#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "LandscapeLayerInfoObject.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

// =================================================================
// File-local helpers (no class members — header is frozen for Live Coding).
// Ported from VibeUE's UFoliageService into the MCP tool pattern.
// =================================================================
namespace
{
	/** Find an existing InstancedFoliageActor in the world, or create one for the current level. */
	AInstancedFoliageActor* GetOrCreateFoliageActor(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			return *It;
		}
		return AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, /*bCreateIfNone=*/true);
	}

	/** Find a UFoliageType already registered in the IFA, by direct UFoliageType path or by static mesh path. */
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

	/** Resolve (or transiently create + register) a foliage type for a mesh/foliage-type path, registering it in the IFA. */
	UFoliageType* FindOrCreateFoliageTypeForMesh(const FString& MeshOrFoliageTypePath, AInstancedFoliageActor* IFA)
	{
		if (!IFA)
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

	/** Bridson Poisson-disk sampling in a rectangle, falling back to uniform random if it can't reach Count. */
	TArray<FVector2D> GeneratePoissonDiskSamples(
		float MinX, float MinY, float MaxX, float MaxY,
		int32 Count, FRandomStream& RNG, float MinDistance = 0.0f)
	{
		TArray<FVector2D> Result;
		if (Count <= 0)
		{
			return Result;
		}

		const float Width = MaxX - MinX;
		const float Height = MaxY - MinY;
		if (Width <= 0.0f || Height <= 0.0f)
		{
			return Result;
		}

		if (MinDistance <= 0.0f)
		{
			const float Area = Width * Height;
			MinDistance = FMath::Sqrt(Area / static_cast<float>(Count)) * 0.7f;
		}

		const float CellSize = MinDistance / FMath::Sqrt(2.0f);
		const int32 GridW = FMath::CeilToInt(Width / CellSize);
		const int32 GridH = FMath::CeilToInt(Height / CellSize);

		TArray<int32> Grid;
		Grid.Init(-1, GridW * GridH);

		const FVector2D Initial(RNG.FRandRange(MinX, MaxX), RNG.FRandRange(MinY, MaxY));
		Result.Add(Initial);

		const int32 GX = FMath::Clamp(FMath::FloorToInt((Initial.X - MinX) / CellSize), 0, GridW - 1);
		const int32 GY = FMath::Clamp(FMath::FloorToInt((Initial.Y - MinY) / CellSize), 0, GridH - 1);
		Grid[GY * GridW + GX] = 0;

		TArray<int32> ActiveList;
		ActiveList.Add(0);

		const int32 MaxAttempts = 30;
		while (ActiveList.Num() > 0 && Result.Num() < Count)
		{
			const int32 ActiveIdx = RNG.RandRange(0, ActiveList.Num() - 1);
			const FVector2D Point = Result[ActiveList[ActiveIdx]];

			bool bFound = false;
			for (int32 Attempt = 0; Attempt < MaxAttempts; Attempt++)
			{
				const float Angle = RNG.FRandRange(0.0f, 2.0f * PI);
				const float Dist = RNG.FRandRange(MinDistance, MinDistance * 2.0f);
				const FVector2D Candidate(Point.X + Dist * FMath::Cos(Angle), Point.Y + Dist * FMath::Sin(Angle));

				if (Candidate.X < MinX || Candidate.X > MaxX || Candidate.Y < MinY || Candidate.Y > MaxY)
				{
					continue;
				}

				const int32 CandGX = FMath::Clamp(FMath::FloorToInt((Candidate.X - MinX) / CellSize), 0, GridW - 1);
				const int32 CandGY = FMath::Clamp(FMath::FloorToInt((Candidate.Y - MinY) / CellSize), 0, GridH - 1);

				bool bTooClose = false;
				for (int32 NY = FMath::Max(0, CandGY - 2); NY <= FMath::Min(GridH - 1, CandGY + 2) && !bTooClose; NY++)
				{
					for (int32 NX = FMath::Max(0, CandGX - 2); NX <= FMath::Min(GridW - 1, CandGX + 2) && !bTooClose; NX++)
					{
						const int32 NeighborIdx = Grid[NY * GridW + NX];
						if (NeighborIdx >= 0 && FVector2D::Distance(Candidate, Result[NeighborIdx]) < MinDistance)
						{
							bTooClose = true;
						}
					}
				}

				if (!bTooClose)
				{
					const int32 NewIdx = Result.Num();
					Result.Add(Candidate);
					Grid[CandGY * GridW + CandGX] = NewIdx;
					ActiveList.Add(NewIdx);
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				ActiveList.RemoveAtSwap(ActiveIdx);
			}
		}

		int32 FallbackAttempts = 0;
		while (Result.Num() < Count && FallbackAttempts < Count * 10)
		{
			FallbackAttempts++;
			Result.Add(FVector2D(RNG.FRandRange(MinX, MaxX), RNG.FRandRange(MinY, MaxY)));
		}

		return Result;
	}

	/** Build a single FFoliageInstance at a traced surface point with random scale/yaw and optional normal alignment. */
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

	/** Commit a batch of instances to the IFA for a given foliage type. Returns number added. */
	int32 CommitInstances(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& NewInstances)
	{
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
		return NewInstances.Num();
	}
}

FMCPToolInfo FMCPTool_Foliage::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("foliage");
	Info.Description = TEXT(
		"Create foliage types and place/scatter foliage instances on landscapes and meshes.\n\n"
		"Operations (set 'operation'):\n"
		"- 'add_type': Create a UFoliageType asset from a static mesh ('mesh_path', 'save_path', 'asset_name')\n"
		"- 'scatter': Scatter 'count' instances of 'mesh_path' in a circle ('center_x', 'center_y', 'radius')\n"
		"- 'paint': Scatter only where landscape 'layer_name' weight exceeds 'layer_weight_threshold'\n"
		"- 'remove': Remove instances of 'mesh_path' in a radius, or set 'all'=true to remove every instance of the type\n"
		"- 'list_types': List all foliage types in the level with instance counts (no params)\n"
		"- 'get_instances': Query instances of 'mesh_path' in a circle ('center_x', 'center_y', 'radius')\n\n"
		"Returns: operation-specific data (scatter/remove counts, type list, or instance transforms)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: add_type, scatter, paint, remove, list_types, get_instances"), true),
		FMCPToolParameter(TEXT("mesh_path"), TEXT("string"), TEXT("Path to a UStaticMesh or UFoliageType asset (e.g. '/Game/Meshes/SM_Tree')"), false),
		FMCPToolParameter(TEXT("save_path"), TEXT("string"), TEXT("For 'add_type': directory to save the foliage type (e.g. '/Game/Foliage')"), false),
		FMCPToolParameter(TEXT("asset_name"), TEXT("string"), TEXT("For 'add_type': name for the foliage type asset (e.g. 'FT_PineTree')"), false),
		FMCPToolParameter(TEXT("landscape"), TEXT("string"), TEXT("For 'paint' (and optional for 'scatter'): landscape name or label to constrain placement"), false),
		FMCPToolParameter(TEXT("layer_name"), TEXT("string"), TEXT("For 'paint': landscape paint layer to test (e.g. 'Grass')"), false),
		FMCPToolParameter(TEXT("layer_weight_threshold"), TEXT("number"), TEXT("For 'paint': minimum layer weight 0.0-1.0 for placement (default 0.5)"), false),
		FMCPToolParameter(TEXT("center_x"), TEXT("number"), TEXT("For scatter/remove/get_instances: world center X of the region"), false),
		FMCPToolParameter(TEXT("center_y"), TEXT("number"), TEXT("For scatter/remove/get_instances: world center Y of the region"), false),
		FMCPToolParameter(TEXT("radius"), TEXT("number"), TEXT("For scatter/remove/get_instances: region radius in world units"), false),
		FMCPToolParameter(TEXT("count"), TEXT("number"), TEXT("For scatter/paint: target number of instances"), false),
		FMCPToolParameter(TEXT("min_scale"), TEXT("number"), TEXT("For scatter/paint/add_type: minimum random scale (default 0.8)"), false),
		FMCPToolParameter(TEXT("max_scale"), TEXT("number"), TEXT("For scatter/paint/add_type: maximum random scale (default 1.2)"), false),
		FMCPToolParameter(TEXT("align_to_normal"), TEXT("boolean"), TEXT("For scatter/paint/add_type: align instances to surface normal (default true)"), false),
		FMCPToolParameter(TEXT("random_yaw"), TEXT("boolean"), TEXT("For scatter/paint: apply random yaw rotation (default true)"), false),
		FMCPToolParameter(TEXT("seed"), TEXT("number"), TEXT("For scatter/paint: random seed for reproducibility (0 = random)"), false),
		FMCPToolParameter(TEXT("all"), TEXT("boolean"), TEXT("For 'remove': remove ALL instances of the type (ignores center/radius) when true"), false),
		FMCPToolParameter(TEXT("max_results"), TEXT("number"), TEXT("For 'get_instances': maximum number of instances to return (default 100)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_Foliage::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("add_type"))      { return OpAddType(Params); }
	if (Operation == TEXT("scatter"))       { return OpScatter(Params); }
	if (Operation == TEXT("paint"))         { return OpPaint(Params); }
	if (Operation == TEXT("remove"))        { return OpRemove(Params); }
	if (Operation == TEXT("list_types"))    { return OpListTypes(Params); }
	if (Operation == TEXT("get_instances")) { return OpGetInstances(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: add_type, scatter, paint, remove, list_types, get_instances"), *Operation));
}

FMCPToolResult FMCPTool_Foliage::OpAddType(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath, SavePath, AssetName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("save_path"), SavePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("asset_name"), AssetName, Err)) { return Err.GetValue(); }

	const float MinScale = ExtractOptionalNumber<float>(Params, TEXT("min_scale"), 0.8f);
	const float MaxScale = ExtractOptionalNumber<float>(Params, TEXT("max_scale"), 1.2f);
	const bool bAlignToNormal = ExtractOptionalBool(Params, TEXT("align_to_normal"), true);

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_type: could not load static mesh '%s'"), *MeshPath));
	}

	FString PackageName = SavePath / AssetName;
	if (!PackageName.StartsWith(TEXT("/")))
	{
		PackageName = TEXT("/") + PackageName;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_type: failed to create package at '%s'"), *PackageName));
	}

	UFoliageType_InstancedStaticMesh* FoliageType = NewObject<UFoliageType_InstancedStaticMesh>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!FoliageType)
	{
		return FMCPToolResult::Error(TEXT("add_type: failed to create UFoliageType_InstancedStaticMesh"));
	}

	FoliageType->SetStaticMesh(Mesh);
	FoliageType->Scaling = EFoliageScaling::Uniform;
	FoliageType->ScaleX = FFloatInterval(MinScale, MaxScale);
	FoliageType->ScaleY = FFloatInterval(MinScale, MaxScale);
	FoliageType->ScaleZ = FFloatInterval(MinScale, MaxScale);
	FoliageType->AlignToNormal = bAlignToNormal;
	FoliageType->RandomYaw = true;

	FoliageType->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(FoliageType);

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, FoliageType, *PackageFileName, SaveArgs);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("foliage_type_path"), FoliageType->GetPathName());
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetBoolField(TEXT("saved"), bSaved);
	Data->SetNumberField(TEXT("min_scale"), MinScale);
	Data->SetNumberField(TEXT("max_scale"), MaxScale);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created foliage type '%s' from mesh '%s'%s"),
			*FoliageType->GetPathName(), *MeshPath, bSaved ? TEXT("") : TEXT(" (package save failed)")),
		Data);
#else
	return FMCPToolResult::Error(TEXT("add_type requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Foliage::OpScatter(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> CtxErr = ValidateEditorContext(World)) { return CtxErr.GetValue(); }

	const float CenterX = ExtractOptionalNumber<float>(Params, TEXT("center_x"), 0.0f);
	const float CenterY = ExtractOptionalNumber<float>(Params, TEXT("center_y"), 0.0f);
	const float Radius = ExtractOptionalNumber<float>(Params, TEXT("radius"), 0.0f);
	const int32 Count = ExtractOptionalNumber<int32>(Params, TEXT("count"), 0);
	const float MinScale = ExtractOptionalNumber<float>(Params, TEXT("min_scale"), 0.8f);
	const float MaxScale = ExtractOptionalNumber<float>(Params, TEXT("max_scale"), 1.2f);
	const bool bAlignToNormal = ExtractOptionalBool(Params, TEXT("align_to_normal"), true);
	const bool bRandomYaw = ExtractOptionalBool(Params, TEXT("random_yaw"), true);
	const int32 Seed = ExtractOptionalNumber<int32>(Params, TEXT("seed"), 0);

	if (Count <= 0) { return FMCPToolResult::Error(TEXT("scatter: 'count' must be > 0")); }
	if (Radius <= 0.0f) { return FMCPToolResult::Error(TEXT("scatter: 'radius' must be > 0")); }

	AInstancedFoliageActor* IFA = GetOrCreateFoliageActor(World);
	if (!IFA) { return FMCPToolResult::Error(TEXT("scatter: failed to get or create InstancedFoliageActor")); }

	UFoliageType* FoliageType = FindOrCreateFoliageTypeForMesh(MeshPath, IFA);
	if (!FoliageType) { return FMCPToolResult::Error(FString::Printf(TEXT("scatter: could not load or create foliage type for '%s'"), *MeshPath)); }

	FRandomStream RNG(Seed != 0 ? Seed : FMath::Rand());

	// Over-generate in the bounding box, then keep only points inside the circle.
	TArray<FVector2D> AllSamples = GeneratePoissonDiskSamples(
		CenterX - Radius, CenterY - Radius, CenterX + Radius, CenterY + Radius, Count * 2, RNG);

	TArray<FVector2D> CircleSamples;
	CircleSamples.Reserve(Count);
	const float RadiusSq = Radius * Radius;
	for (const FVector2D& S : AllSamples)
	{
		const float DX = S.X - CenterX;
		const float DY = S.Y - CenterY;
		if (DX * DX + DY * DY <= RadiusSq)
		{
			CircleSamples.Add(S);
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("FoliageMCP", "ScatterFoliage", "Scatter Foliage"));
	IFA->Modify();

	TArray<FFoliageInstance> NewInstances;
	NewInstances.Reserve(Count);
	int32 Rejected = 0;
	for (const FVector2D& Pos : CircleSamples)
	{
		if (NewInstances.Num() >= Count) { break; }
		FVector HitLocation, HitNormal;
		if (!TraceToSurface(World, Pos.X, Pos.Y, HitLocation, HitNormal)) { Rejected++; continue; }
		NewInstances.Add(MakeInstance(HitLocation, HitNormal, MinScale, MaxScale, bAlignToNormal, bRandomYaw, RNG));
	}

	const int32 Added = CommitInstances(IFA, FoliageType, NewInstances);
	MarkActorDirty(IFA);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("foliage_type"), FoliageType->GetName());
	Data->SetNumberField(TEXT("requested"), Count);
	Data->SetNumberField(TEXT("added"), Added);
	Data->SetNumberField(TEXT("rejected"), Rejected);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Scattered %d/%d instances of '%s' (%d rejected, no surface)"), Added, Count, *MeshPath, Rejected), Data);
#else
	return FMCPToolResult::Error(TEXT("scatter requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Foliage::OpPaint(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath, LandscapeName, LayerName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("landscape"), LandscapeName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("layer_name"), LayerName, Err)) { return Err.GetValue(); }

	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> CtxErr = ValidateEditorContext(World)) { return CtxErr.GetValue(); }

	const int32 Count = ExtractOptionalNumber<int32>(Params, TEXT("count"), 0);
	const float MinScale = ExtractOptionalNumber<float>(Params, TEXT("min_scale"), 0.8f);
	const float MaxScale = ExtractOptionalNumber<float>(Params, TEXT("max_scale"), 1.2f);
	const float Threshold = ExtractOptionalNumber<float>(Params, TEXT("layer_weight_threshold"), 0.5f);
	const bool bAlignToNormal = ExtractOptionalBool(Params, TEXT("align_to_normal"), true);
	const bool bRandomYaw = ExtractOptionalBool(Params, TEXT("random_yaw"), true);
	const int32 Seed = ExtractOptionalNumber<int32>(Params, TEXT("seed"), 0);

	if (Count <= 0) { return FMCPToolResult::Error(TEXT("paint: 'count' must be > 0")); }

	// Find the landscape.
	ALandscape* Landscape = nullptr;
	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		ALandscape* L = *It;
		if (L->GetActorLabel().Equals(LandscapeName, ESearchCase::IgnoreCase) ||
			L->GetName().Equals(LandscapeName, ESearchCase::IgnoreCase))
		{
			Landscape = L;
			break;
		}
	}
	if (!Landscape) { return FMCPToolResult::Error(FString::Printf(TEXT("paint: landscape '%s' not found"), *LandscapeName)); }

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo) { return FMCPToolResult::Error(TEXT("paint: no landscape info available")); }

	// Resolve the target paint layer.
	ULandscapeLayerInfoObject* TargetLayerInfo = nullptr;
	for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
	{
		if (LayerSettings.LayerInfoObj &&
			LayerSettings.LayerInfoObj->GetLayerName().ToString().Equals(LayerName, ESearchCase::IgnoreCase))
		{
			TargetLayerInfo = LayerSettings.LayerInfoObj;
			break;
		}
	}
	if (!TargetLayerInfo) { return FMCPToolResult::Error(FString::Printf(TEXT("paint: layer '%s' not found on landscape '%s'"), *LayerName, *LandscapeName)); }

	// Landscape world-space bounds for sampling.
	int32 MinLX, MinLY, MaxLX, MaxLY;
	if (!LandscapeInfo->GetLandscapeExtent(MinLX, MinLY, MaxLX, MaxLY))
	{
		return FMCPToolResult::Error(TEXT("paint: could not get landscape extent"));
	}
	const FVector LandscapeLocation = Landscape->GetActorLocation();
	const FVector LandscapeScale = Landscape->GetActorScale3D();
	const float WorldMinX = LandscapeLocation.X + MinLX * LandscapeScale.X;
	const float WorldMinY = LandscapeLocation.Y + MinLY * LandscapeScale.Y;
	const float WorldMaxX = LandscapeLocation.X + MaxLX * LandscapeScale.X;
	const float WorldMaxY = LandscapeLocation.Y + MaxLY * LandscapeScale.Y;

	AInstancedFoliageActor* IFA = GetOrCreateFoliageActor(World);
	if (!IFA) { return FMCPToolResult::Error(TEXT("paint: failed to get or create InstancedFoliageActor")); }

	UFoliageType* FoliageType = FindOrCreateFoliageTypeForMesh(MeshPath, IFA);
	if (!FoliageType) { return FMCPToolResult::Error(FString::Printf(TEXT("paint: could not load or create foliage type for '%s'"), *MeshPath)); }

	FRandomStream RNG(Seed != 0 ? Seed : FMath::Rand());

	// Over-generate candidates — many get rejected by the weight gate.
	TArray<FVector2D> Samples = GeneratePoissonDiskSamples(WorldMinX, WorldMinY, WorldMaxX, WorldMaxY, Count * 4, RNG);

	FScopedTransaction Transaction(NSLOCTEXT("FoliageMCP", "PaintFoliage", "Paint Foliage On Layer"));
	IFA->Modify();

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	TArray<FFoliageInstance> NewInstances;
	NewInstances.Reserve(Count);
	int32 Rejected = 0;
	for (const FVector2D& Pos : Samples)
	{
		if (NewInstances.Num() >= Count) { break; }

		FVector HitLocation, HitNormal;
		if (!TraceToSurface(World, Pos.X, Pos.Y, HitLocation, HitNormal)) { Rejected++; continue; }

		// GetWeightData takes the X1/Y1/X2/Y2 corners by non-const reference (it clamps/expands them),
		// so use distinct mutable copies for the min and max corners of a 1x1 sample region.
		int32 X1 = FMath::RoundToInt((Pos.X - LandscapeLocation.X) / LandscapeScale.X);
		int32 Y1 = FMath::RoundToInt((Pos.Y - LandscapeLocation.Y) / LandscapeScale.Y);
		int32 X2 = X1;
		int32 Y2 = Y1;

		uint8 WeightSample = 0;
		LandscapeEdit.GetWeightData(TargetLayerInfo, X1, Y1, X2, Y2, &WeightSample, 0);
		const float Weight = WeightSample / 255.0f;
		if (Weight < Threshold) { Rejected++; continue; }

		NewInstances.Add(MakeInstance(HitLocation, HitNormal, MinScale, MaxScale, bAlignToNormal, bRandomYaw, RNG));
	}

	const int32 Added = CommitInstances(IFA, FoliageType, NewInstances);
	MarkActorDirty(IFA);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("foliage_type"), FoliageType->GetName());
	Data->SetStringField(TEXT("landscape"), Landscape->GetActorLabel());
	Data->SetStringField(TEXT("layer"), LayerName);
	Data->SetNumberField(TEXT("threshold"), Threshold);
	Data->SetNumberField(TEXT("requested"), Count);
	Data->SetNumberField(TEXT("added"), Added);
	Data->SetNumberField(TEXT("rejected"), Rejected);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Painted %d/%d instances of '%s' on layer '%s' (>= %.2f weight, %d rejected)"),
			Added, Count, *MeshPath, *LayerName, Threshold, Rejected), Data);
#else
	return FMCPToolResult::Error(TEXT("paint requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Foliage::OpRemove(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> CtxErr = ValidateEditorContext(World)) { return CtxErr.GetValue(); }

	const bool bAll = ExtractOptionalBool(Params, TEXT("all"), false);
	const float CenterX = ExtractOptionalNumber<float>(Params, TEXT("center_x"), 0.0f);
	const float CenterY = ExtractOptionalNumber<float>(Params, TEXT("center_y"), 0.0f);
	const float Radius = ExtractOptionalNumber<float>(Params, TEXT("radius"), 0.0f);

	if (!bAll && Radius <= 0.0f)
	{
		return FMCPToolResult::Error(TEXT("remove: provide 'radius' > 0, or set 'all'=true to remove every instance of the type"));
	}

	const float RadiusSq = Radius * Radius;
	int32 TotalRemoved = 0;

	FScopedTransaction Transaction(NSLOCTEXT("FoliageMCP", "RemoveFoliage", "Remove Foliage"));

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		UFoliageType* FT = FindFoliageTypeInIFA(MeshPath, IFA);
		if (!FT) { continue; }

		FFoliageInfo* InfoPtr = IFA->FindInfo(FT);
		if (!InfoPtr) { continue; }

		IFA->Modify();
		FFoliageInfo& Info = *InfoPtr;

		TArray<int32> IndicesToRemove;
		for (int32 i = 0; i < Info.Instances.Num(); i++)
		{
			if (bAll)
			{
				IndicesToRemove.Add(i);
			}
			else
			{
				const FFoliageInstance& Instance = Info.Instances[i];
				const float DX = Instance.Location.X - CenterX;
				const float DY = Instance.Location.Y - CenterY;
				if (DX * DX + DY * DY <= RadiusSq)
				{
					IndicesToRemove.Add(i);
				}
			}
		}

		if (IndicesToRemove.Num() > 0)
		{
			Info.RemoveInstances(IndicesToRemove, /*bRebuildFoliageTree=*/true);
			TotalRemoved += IndicesToRemove.Num();
			MarkActorDirty(IFA);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetBoolField(TEXT("all"), bAll);
	Data->SetNumberField(TEXT("removed"), TotalRemoved);
	return FMCPToolResult::Success(
		bAll
			? FString::Printf(TEXT("Removed %d instances (all) of '%s'"), TotalRemoved, *MeshPath)
			: FString::Printf(TEXT("Removed %d instances of '%s' within radius %.0f of (%.0f, %.0f)"), TotalRemoved, *MeshPath, Radius, CenterX, CenterY),
		Data);
#else
	return FMCPToolResult::Error(TEXT("remove requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Foliage::OpListTypes(const TSharedRef<FJsonObject>& Params)
{
	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> CtxErr = ValidateEditorContext(World)) { return CtxErr.GetValue(); }

	TArray<TSharedPtr<FJsonValue>> Types;
	int32 TotalTypes = 0;
	int32 TotalInstances = 0;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		const TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliageInfos = IFA->GetFoliageInfos();
		for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : FoliageInfos)
		{
			UFoliageType* FT = Pair.Key;
			if (!FT) { continue; }
			const FFoliageInfo& Info = Pair.Value.Get();

			TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();
			TypeObj->SetStringField(TEXT("name"), FT->GetName());
			TypeObj->SetStringField(TEXT("foliage_type_path"), FT->GetPathName());
			TypeObj->SetNumberField(TEXT("instance_count"), Info.Instances.Num());
			if (UFoliageType_InstancedStaticMesh* ISMT = Cast<UFoliageType_InstancedStaticMesh>(FT))
			{
				if (ISMT->GetStaticMesh())
				{
					TypeObj->SetStringField(TEXT("mesh_path"), ISMT->GetStaticMesh()->GetPathName());
				}
			}
			Types.Add(MakeShared<FJsonValueObject>(TypeObj));
			TotalTypes++;
			TotalInstances += Info.Instances.Num();
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("types"), Types);
	Data->SetNumberField(TEXT("type_count"), TotalTypes);
	Data->SetNumberField(TEXT("total_instances"), TotalInstances);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("%d foliage type(s) in the level, %d total instances"), TotalTypes, TotalInstances), Data);
}

FMCPToolResult FMCPTool_Foliage::OpGetInstances(const TSharedRef<FJsonObject>& Params)
{
	FString MeshPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("mesh_path"), MeshPath, Err)) { return Err.GetValue(); }

	UWorld* World = nullptr;
	if (TOptional<FMCPToolResult> CtxErr = ValidateEditorContext(World)) { return CtxErr.GetValue(); }

	const float CenterX = ExtractOptionalNumber<float>(Params, TEXT("center_x"), 0.0f);
	const float CenterY = ExtractOptionalNumber<float>(Params, TEXT("center_y"), 0.0f);
	const float Radius = ExtractOptionalNumber<float>(Params, TEXT("radius"), 0.0f);
	const int32 MaxResults = ExtractOptionalNumber<int32>(Params, TEXT("max_results"), 100);

	if (Radius <= 0.0f) { return FMCPToolResult::Error(TEXT("get_instances: 'radius' must be > 0")); }

	const float RadiusSq = Radius * Radius;
	int32 TotalInRadius = 0;
	TArray<TSharedPtr<FJsonValue>> Instances;

	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		UFoliageType* FT = FindFoliageTypeInIFA(MeshPath, IFA);
		if (!FT) { continue; }

		const TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliageInfos = IFA->GetFoliageInfos();
		const TUniqueObj<FFoliageInfo>* FoundInfo = FoliageInfos.Find(FT);
		if (!FoundInfo) { continue; }

		const FFoliageInfo& Info = FoundInfo->Get();
		for (int32 i = 0; i < Info.Instances.Num(); i++)
		{
			const FFoliageInstance& Instance = Info.Instances[i];
			const float DX = Instance.Location.X - CenterX;
			const float DY = Instance.Location.Y - CenterY;
			if (DX * DX + DY * DY <= RadiusSq)
			{
				TotalInRadius++;
				if (Instances.Num() < MaxResults)
				{
					TSharedPtr<FJsonObject> InstObj = MakeShared<FJsonObject>();
					InstObj->SetNumberField(TEXT("index"), i);
					InstObj->SetObjectField(TEXT("location"), UnrealClaudeJsonUtils::VectorToJson(Instance.Location));
					InstObj->SetObjectField(TEXT("rotation"), UnrealClaudeJsonUtils::RotatorToJson(Instance.Rotation));
					InstObj->SetObjectField(TEXT("scale"), UnrealClaudeJsonUtils::VectorToJson(
						FVector(Instance.DrawScale3D.X, Instance.DrawScale3D.Y, Instance.DrawScale3D.Z)));
					Instances.Add(MakeShared<FJsonValueObject>(InstObj));
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("mesh_path"), MeshPath);
	Data->SetNumberField(TEXT("total_in_radius"), TotalInRadius);
	Data->SetNumberField(TEXT("returned"), Instances.Num());
	Data->SetArrayField(TEXT("instances"), Instances);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("%d instance(s) of '%s' within radius %.0f of (%.0f, %.0f); returning %d"),
			TotalInRadius, *MeshPath, Radius, CenterX, CenterY, Instances.Num()), Data);
}
