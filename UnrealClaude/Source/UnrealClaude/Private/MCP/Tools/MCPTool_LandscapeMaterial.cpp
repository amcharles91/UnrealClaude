// Copyright Natali Caggiano. All Rights Reserved.
// Adapted from VibeUE (github.com/kevinpbuckley/VibeUE), MIT (c) 2025 Kevin Buckley / Buckley Builds LLC.

#include "MCPTool_LandscapeMaterial.h"
#include "UnrealClaudeModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"

#include "Engine/Texture.h"

// Material (Engine module)
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "SceneTypes.h" // EMaterialProperty / MP_BaseColor

#if WITH_EDITOR
// Standard material expressions (Engine module: Runtime/Engine/Public/Materials)
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionWorldPosition.h"

// Landscape material expressions + layer info + grass type (Landscape module)
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeGrassType.h"
#include "LandscapeEditTypes.h" // ELandscapeTargetLayerBlendMethod
#include "LandscapeProxy.h"

// Material asset creation + graph editing (UnrealEd / MaterialEditor)
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"

// Editor world (for optional landscape assignment)
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#endif // WITH_EDITOR

// =====================================================================
// File-local helpers (no header members — keeps ABI stable for Live Coding)
// =====================================================================
namespace
{
#if WITH_EDITOR
	/** Normalize a directory path to end with a single trailing slash. */
	FString NormalizeDir(FString Dir)
	{
		if (Dir.IsEmpty())
		{
			return TEXT("/Game/");
		}
		if (!Dir.EndsWith(TEXT("/")))
		{
			Dir += TEXT("/");
		}
		return Dir;
	}

	/** Create + save a brand-new UMaterial asset at Dir/Name. Returns nullptr on failure (OutError set). */
	UMaterial* CreateLandscapeMaterialAsset(const FString& Dir, const FString& Name, FString& OutError)
	{
		const FString PackagePath = NormalizeDir(Dir) + Name;

		if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
		{
			OutError = FString::Printf(TEXT("Asset already exists: %s"), *PackagePath);
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackagePath);
			return nullptr;
		}

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn));

		if (!Material)
		{
			OutError = FString::Printf(TEXT("Failed to create material: %s"), *PackagePath);
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(Material);
		Package->MarkPackageDirty();
		return Material;
	}

	/** Find the first LandscapeLayerBlend expression in a material, optionally matching a node id (object name). */
	UMaterialExpressionLandscapeLayerBlend* FindLayerBlendNode(UMaterial* Material, const FString& NodeId)
	{
		if (!Material)
		{
			return nullptr;
		}
		for (UMaterialExpression* Expr : Material->GetExpressions())
		{
			if (UMaterialExpressionLandscapeLayerBlend* Blend = Cast<UMaterialExpressionLandscapeLayerBlend>(Expr))
			{
				if (NodeId.IsEmpty() || Blend->GetName().Equals(NodeId, ESearchCase::IgnoreCase))
				{
					return Blend;
				}
			}
		}
		return nullptr;
	}

	/** Parse a blend_type string into the engine enum. Defaults to LB_WeightBlend. */
	ELandscapeLayerBlendType ParseBlendType(const FString& In)
	{
		if (In.Equals(TEXT("LB_AlphaBlend"), ESearchCase::IgnoreCase))  { return LB_AlphaBlend; }
		if (In.Equals(TEXT("LB_HeightBlend"), ESearchCase::IgnoreCase)) { return LB_HeightBlend; }
		return LB_WeightBlend;
	}

	const TCHAR* BlendTypeToString(ELandscapeLayerBlendType T)
	{
		switch (T)
		{
		case LB_AlphaBlend:  return TEXT("LB_AlphaBlend");
		case LB_HeightBlend: return TEXT("LB_HeightBlend");
		default:             return TEXT("LB_WeightBlend");
		}
	}

	/** Find the index of a layer by name within a blend node, or INDEX_NONE. */
	int32 FindLayerIndex(UMaterialExpressionLandscapeLayerBlend* Blend, const FString& LayerName)
	{
		if (!Blend)
		{
			return INDEX_NONE;
		}
		for (int32 i = 0; i < Blend->Layers.Num(); ++i)
		{
			if (Blend->Layers[i].LayerName.ToString().Equals(LayerName, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	/** Checked save of an already-existing asset by its content path. */
	bool SaveAssetChecked(const FString& AssetPath, FString& OutError)
	{
		if (!UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false))
		{
			OutError = FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath);
			return false;
		}
		return true;
	}

	/**
	 * Wire a height mask into a layer's HeightInput:
	 *   WorldPosition -> ComponentMask(B) -> SmoothStep(Min=Threshold, Max=Threshold+Blend)
	 * Returns true if all nodes were created and connected.
	 */
	bool WireHeightMask(UMaterial* Material, UMaterialExpressionLandscapeLayerBlend* Blend, int32 LayerIndex,
		float HeightThreshold, float HeightBlend, int32 MaskX, int32 MaskY)
	{
		UMaterialExpression* WorldPosExpr = UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionWorldPosition::StaticClass(), MaskX - 700, MaskY);
		UMaterialExpression* MaskExpr = UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionComponentMask::StaticClass(), MaskX - 400, MaskY);
		UMaterialExpression* SmoothExpr = UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionSmoothStep::StaticClass(), MaskX, MaskY);

		if (!WorldPosExpr || !MaskExpr || !SmoothExpr)
		{
			return false;
		}

		// Mask the world position Z (blue channel) to get height as a scalar.
		UMaterialExpressionComponentMask* MaskNode = Cast<UMaterialExpressionComponentMask>(MaskExpr);
		MaskNode->R = false; MaskNode->G = false; MaskNode->B = true; MaskNode->A = false;
		MaskNode->Input.Connect(0, WorldPosExpr);

		// SmoothStep ramps 0->1 between Threshold and Threshold+Blend.
		UMaterialExpressionSmoothStep* SmoothNode = Cast<UMaterialExpressionSmoothStep>(SmoothExpr);
		SmoothNode->ConstMin = HeightThreshold;
		SmoothNode->ConstMax = HeightThreshold + HeightBlend;
		SmoothNode->Value.Connect(0, MaskNode);

		Blend->Layers[LayerIndex].HeightInput.Connect(0, SmoothNode);
		return true;
	}

	/**
	 * Wire a slope mask into a layer's HeightInput:
	 *   VertexNormalWS -> ComponentMask(B) -> OneMinus -> SmoothStep(slope factors)
	 * Slope factor uses 1 - cos(angle) so steeper slopes produce higher mask values.
	 */
	bool WireSlopeMask(UMaterial* Material, UMaterialExpressionLandscapeLayerBlend* Blend, int32 LayerIndex,
		float SlopeThreshold, float SlopeBlend, int32 MaskX, int32 MaskY)
	{
		const float SlopeFactorMin = 1.0f - FMath::Cos(FMath::DegreesToRadians(SlopeThreshold));
		const float SlopeFactorMax = 1.0f - FMath::Cos(FMath::DegreesToRadians(SlopeThreshold + SlopeBlend));

		UMaterialExpression* NormalExpr = UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionVertexNormalWS::StaticClass(), MaskX - 700, MaskY);
		UMaterialExpression* MaskExpr = UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionComponentMask::StaticClass(), MaskX - 500, MaskY);
		UMaterialExpression* OneMinusExpr = UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionOneMinus::StaticClass(), MaskX - 300, MaskY);
		UMaterialExpression* SmoothExpr = UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionSmoothStep::StaticClass(), MaskX, MaskY);

		if (!NormalExpr || !MaskExpr || !OneMinusExpr || !SmoothExpr)
		{
			return false;
		}

		UMaterialExpressionComponentMask* MaskNode = Cast<UMaterialExpressionComponentMask>(MaskExpr);
		MaskNode->R = false; MaskNode->G = false; MaskNode->B = true; MaskNode->A = false;
		MaskNode->Input.Connect(0, NormalExpr);

		UMaterialExpressionOneMinus* OneMinusNode = Cast<UMaterialExpressionOneMinus>(OneMinusExpr);
		OneMinusNode->Input.Connect(0, MaskNode);

		UMaterialExpressionSmoothStep* SmoothNode = Cast<UMaterialExpressionSmoothStep>(SmoothExpr);
		SmoothNode->ConstMin = SlopeFactorMin;
		SmoothNode->ConstMax = SlopeFactorMax;
		SmoothNode->Value.Connect(0, OneMinusNode);

		Blend->Layers[LayerIndex].HeightInput.Connect(0, SmoothNode);
		return true;
	}
#endif // WITH_EDITOR
}

FMCPToolInfo FMCPTool_LandscapeMaterial::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("landscape_material");
	Info.Description = TEXT(
		"Build and edit Landscape Materials (auto-material, layer-blend nodes, layer info objects, grass output).\n\n"
		"Operations (set 'operation'):\n"
		"- 'create_auto_material': Create a complete auto-blended landscape material at 'material_path'/'material_name' "
		"from 'layers' (array of {name, texture, role}); roles are base/slope/height/paint. Optional 'landscape' to "
		"assign it, 'auto_blend', 'height_threshold', 'height_blend', 'slope_threshold', 'slope_blend'\n"
		"- 'add_layer_blend': Add 'layer' to the LandscapeLayerBlend node 'blend_node_id' in material 'material_path' "
		"with 'blend_type' (LB_WeightBlend, LB_AlphaBlend, LB_HeightBlend). If 'blend_node_id' is omitted a new "
		"blend node is created first\n"
		"- 'set_layer_info': Create a ULandscapeLayerInfoObject named 'layer' at 'destination_path'; "
		"'weight_blended' controls weight blending (default true)\n"
		"- 'add_grass_output': Add a LandscapeGrassOutput node to material 'material_path' with 'grass_types' "
		"(map of input name -> LandscapeGrassType asset path) at graph position ('pos_x','pos_y')\n\n"
		"Returns: operation-specific data (material/asset paths, node IDs)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: create_auto_material, add_layer_blend, set_layer_info, add_grass_output"), true),
		FMCPToolParameter(TEXT("material_path"), TEXT("string"), TEXT("Full path to the landscape material asset (target for add_layer_blend/add_grass_output; directory for create_auto_material)"), false),
		// create_auto_material
		FMCPToolParameter(TEXT("material_name"), TEXT("string"), TEXT("For 'create_auto_material': name of the new material asset"), false),
		FMCPToolParameter(TEXT("landscape"), TEXT("string"), TEXT("For 'create_auto_material': optional landscape name/label to assign the material to (empty = don't assign)"), false),
		FMCPToolParameter(TEXT("layers"), TEXT("array"), TEXT("For 'create_auto_material': layer configs [{name, texture, role}], role one of base/slope/height/paint"), false),
		FMCPToolParameter(TEXT("auto_blend"), TEXT("boolean"), TEXT("For 'create_auto_material': wire height/slope masks for layers with those roles (default true)"), false),
		FMCPToolParameter(TEXT("height_threshold"), TEXT("number"), TEXT("For 'create_auto_material': world Z where the height layer begins (default 5000)"), false),
		FMCPToolParameter(TEXT("height_blend"), TEXT("number"), TEXT("For 'create_auto_material': height blend transition width (default 1000)"), false),
		FMCPToolParameter(TEXT("slope_threshold"), TEXT("number"), TEXT("For 'create_auto_material': slope angle where the slope layer begins, degrees (default 35)"), false),
		FMCPToolParameter(TEXT("slope_blend"), TEXT("number"), TEXT("For 'create_auto_material': slope blend transition width, degrees (default 10)"), false),
		// add_layer_blend
		FMCPToolParameter(TEXT("blend_node_id"), TEXT("string"), TEXT("For 'add_layer_blend': ID of an existing LandscapeLayerBlend node (omit to create one)"), false),
		FMCPToolParameter(TEXT("layer"), TEXT("string"), TEXT("For 'add_layer_blend'/'set_layer_info': layer name"), false),
		FMCPToolParameter(TEXT("blend_type"), TEXT("string"), TEXT("For 'add_layer_blend': LB_WeightBlend (default), LB_AlphaBlend, or LB_HeightBlend"), false),
		// set_layer_info
		FMCPToolParameter(TEXT("destination_path"), TEXT("string"), TEXT("For 'set_layer_info': directory where the layer info asset is created (e.g. /Game/Landscape)"), false),
		FMCPToolParameter(TEXT("weight_blended"), TEXT("boolean"), TEXT("For 'set_layer_info': true for weight-blended (default), false otherwise"), false),
		// add_grass_output
		FMCPToolParameter(TEXT("grass_types"), TEXT("object"), TEXT("For 'add_grass_output': map of input name -> LandscapeGrassType asset path"), false),
		FMCPToolParameter(TEXT("pos_x"), TEXT("number"), TEXT("For 'add_grass_output': X position in the material graph (default 400)"), false),
		FMCPToolParameter(TEXT("pos_y"), TEXT("number"), TEXT("For 'add_grass_output': Y position in the material graph (default 0)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_LandscapeMaterial::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("create_auto_material")) { return OpCreateAutoMaterial(Params); }
	if (Operation == TEXT("add_layer_blend"))      { return OpAddLayerBlend(Params); }
	if (Operation == TEXT("set_layer_info"))       { return OpSetLayerInfo(Params); }
	if (Operation == TEXT("add_grass_output"))     { return OpAddGrassOutput(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create_auto_material, add_layer_blend, set_layer_info, add_grass_output"), *Operation));
}

FMCPToolResult FMCPTool_LandscapeMaterial::OpCreateAutoMaterial(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MaterialName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("material_name"), MaterialName, Err))
	{
		return Err.GetValue();
	}

	const FString MaterialDir = ExtractOptionalString(Params, TEXT("material_path"), TEXT("/Game/Landscape"));

	// Parse layers array: [{name, texture, role}]
	const TArray<TSharedPtr<FJsonValue>>* LayersJson = nullptr;
	if (!Params->TryGetArrayField(TEXT("layers"), LayersJson) || LayersJson->Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("create_auto_material: 'layers' must be a non-empty array of {name, texture, role}"));
	}

	struct FLayerCfg { FString Name; FString Texture; FString Role; };
	TArray<FLayerCfg> Layers;
	for (const TSharedPtr<FJsonValue>& Val : *LayersJson)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Val->TryGetObject(Obj)) { continue; }
		FLayerCfg Cfg;
		(*Obj)->TryGetStringField(TEXT("name"), Cfg.Name);
		(*Obj)->TryGetStringField(TEXT("texture"), Cfg.Texture);
		(*Obj)->TryGetStringField(TEXT("role"), Cfg.Role);
		if (Cfg.Name.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("create_auto_material: every layer entry requires a non-empty 'name'"));
		}
		Layers.Add(MoveTemp(Cfg));
	}
	if (Layers.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("create_auto_material: no valid layer entries parsed from 'layers'"));
	}

	const bool  bAutoBlend      = ExtractOptionalBool(Params, TEXT("auto_blend"), true);
	const float HeightThreshold = ExtractOptionalNumber<float>(Params, TEXT("height_threshold"), 5000.0f);
	const float HeightBlend     = ExtractOptionalNumber<float>(Params, TEXT("height_blend"), 1000.0f);
	const float SlopeThreshold  = ExtractOptionalNumber<float>(Params, TEXT("slope_threshold"), 35.0f);
	const float SlopeBlend      = ExtractOptionalNumber<float>(Params, TEXT("slope_blend"), 10.0f);
	const FString LandscapeName = ExtractOptionalString(Params, TEXT("landscape"));

	// Step 1: create the material asset.
	FString CreateError;
	UMaterial* Material = CreateLandscapeMaterialAsset(MaterialDir, MaterialName, CreateError);
	if (!Material)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("create_auto_material: %s"), *CreateError));
	}
	const FString MaterialAssetPath = Material->GetPackage()->GetPathName();

	Material->Modify();

	// Step 2: create the LandscapeLayerBlend node.
	UMaterialExpressionLandscapeLayerBlend* Blend = Cast<UMaterialExpressionLandscapeLayerBlend>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLandscapeLayerBlend::StaticClass(), 0, 0));
	if (!Blend)
	{
		return FMCPToolResult::Error(TEXT("create_auto_material: failed to create LandscapeLayerBlend node"));
	}

	// Step 3: one FLayerBlendInput per layer; height/slope roles use LB_HeightBlend so the HeightInput mask matters.
	TArray<TSharedPtr<FJsonValue>> WiredLayers;
	TArray<FString> Notes;

	for (const FLayerCfg& Cfg : Layers)
	{
		FLayerBlendInput Input;
		Input.LayerName = FName(*Cfg.Name);
		const bool bRoleUsesHeight = Cfg.Role.Equals(TEXT("height"), ESearchCase::IgnoreCase)
			|| Cfg.Role.Equals(TEXT("slope"), ESearchCase::IgnoreCase);
		Input.BlendType = bRoleUsesHeight ? LB_HeightBlend : LB_WeightBlend;
		Input.PreviewWeight = 1.0f;
		Blend->Layers.Add(Input);
	}

	// Step 4: a texture sample per layer that has a texture, connected to that layer's LayerInput.
	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		const FLayerCfg& Cfg = Layers[i];
		const int32 BaseY = i * 350;

		TSharedPtr<FJsonObject> LayerInfo = MakeShared<FJsonObject>();
		LayerInfo->SetStringField(TEXT("name"), Cfg.Name);
		LayerInfo->SetStringField(TEXT("role"), Cfg.Role);
		LayerInfo->SetStringField(TEXT("blend_type"), BlendTypeToString(Blend->Layers[i].BlendType));

		bool bTextureWired = false;
		if (!Cfg.Texture.IsEmpty())
		{
			UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(Cfg.Texture));
			if (Tex)
			{
				UMaterialExpressionTextureSample* Sampler = Cast<UMaterialExpressionTextureSample>(
					UMaterialEditingLibrary::CreateMaterialExpression(
						Material, UMaterialExpressionTextureSample::StaticClass(), -600, BaseY));
				if (Sampler)
				{
					Sampler->Texture = Tex;
					Blend->Layers[i].LayerInput.Connect(0, Sampler);
					bTextureWired = true;
				}
			}
			else
			{
				Notes.Add(FString::Printf(TEXT("layer '%s': texture not found: %s"), *Cfg.Name, *Cfg.Texture));
			}
		}
		LayerInfo->SetBoolField(TEXT("texture_wired"), bTextureWired);

		// Step 5: auto-blend masks for slope/height roles into this layer's HeightInput.
		bool bMaskWired = false;
		if (bAutoBlend)
		{
			if (Cfg.Role.Equals(TEXT("slope"), ESearchCase::IgnoreCase))
			{
				bMaskWired = WireSlopeMask(Material, Blend, i, SlopeThreshold, SlopeBlend, -200, BaseY);
				if (bMaskWired) { LayerInfo->SetStringField(TEXT("mask"), TEXT("slope")); }
				else { Notes.Add(FString::Printf(TEXT("layer '%s': failed to create slope mask nodes"), *Cfg.Name)); }
			}
			else if (Cfg.Role.Equals(TEXT("height"), ESearchCase::IgnoreCase))
			{
				bMaskWired = WireHeightMask(Material, Blend, i, HeightThreshold, HeightBlend, -200, BaseY);
				if (bMaskWired) { LayerInfo->SetStringField(TEXT("mask"), TEXT("height")); }
				else { Notes.Add(FString::Printf(TEXT("layer '%s': failed to create height mask nodes"), *Cfg.Name)); }
			}
		}
		LayerInfo->SetBoolField(TEXT("mask_wired"), bMaskWired);

		WiredLayers.Add(MakeShared<FJsonValueObject>(LayerInfo));
	}

	// Step 6: connect the blend output to BaseColor.
	bool bBaseColorConnected = UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT(""), MP_BaseColor);
	if (!bBaseColorConnected)
	{
		Notes.Add(TEXT("failed to connect LandscapeLayerBlend to BaseColor"));
	}

	// Finalize the material graph.
	UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
	// RecompileMaterial performs the needed update+recompile; a preceding PostEditChange would
	// compile the same material a second time.
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();

	// Step 7: optional landscape assignment.
	bool bLandscapeAssigned = false;
	if (!LandscapeName.IsEmpty())
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
			{
				ALandscapeProxy* Proxy = *It;
				if (Proxy->GetActorLabel().Equals(LandscapeName, ESearchCase::IgnoreCase)
					|| Proxy->GetName().Equals(LandscapeName, ESearchCase::IgnoreCase))
				{
					Proxy->Modify();
					// EditorSetLandscapeMaterial (the BlueprintSetter) is not LANDSCAPE_API-exported, so
					// it is not linkable from this module. Set the property directly and fire the same
					// PostEditChangeProperty the setter triggers, which rebuilds the material instances.
					Proxy->LandscapeMaterial = Material;
					if (FProperty* MatProp = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(),
						GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterial)))
					{
						FPropertyChangedEvent ChangeEvent(MatProp);
						Proxy->PostEditChangeProperty(ChangeEvent);
					}
					Proxy->MarkPackageDirty();
					bLandscapeAssigned = true;
					break;
				}
			}
			if (!bLandscapeAssigned)
			{
				Notes.Add(FString::Printf(TEXT("landscape '%s' not found in the editor world; material was not assigned"), *LandscapeName));
			}
		}
		else
		{
			Notes.Add(TEXT("no editor world available; landscape assignment skipped"));
		}
	}

	// Save the material.
	FString SaveError;
	const bool bSaved = SaveAssetChecked(MaterialAssetPath, SaveError);
	if (!bSaved)
	{
		Notes.Add(SaveError);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("material_path"), MaterialAssetPath);
	Data->SetStringField(TEXT("blend_node_id"), Blend->GetName());
	Data->SetArrayField(TEXT("layers"), WiredLayers);
	Data->SetBoolField(TEXT("auto_blend"), bAutoBlend);
	Data->SetBoolField(TEXT("base_color_connected"), bBaseColorConnected);
	Data->SetBoolField(TEXT("landscape_assigned"), bLandscapeAssigned);
	Data->SetBoolField(TEXT("saved"), bSaved);
	if (Notes.Num() > 0)
	{
		Data->SetArrayField(TEXT("notes"), StringArrayToJsonArray(Notes));
	}

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Created auto landscape material '%s' with %d layer(s)"), *MaterialName, Layers.Num()),
		Data);
	Result.Warnings = Notes;
	return Result;
#else
	return FMCPToolResult::Error(TEXT("create_auto_material requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_LandscapeMaterial::OpAddLayerBlend(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MaterialPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("material_path"), MaterialPath, Err))
	{
		return Err.GetValue();
	}

	FString LayerName;
	if (!ExtractRequiredString(Params, TEXT("layer"), LayerName, Err))
	{
		return Err.GetValue();
	}

	const FString BlendNodeId = ExtractOptionalString(Params, TEXT("blend_node_id"));
	const ELandscapeLayerBlendType BlendType = ParseBlendType(ExtractOptionalString(Params, TEXT("blend_type")));

	UMaterial* Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(MaterialPath));
	if (!Material)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_layer_blend: failed to load material: %s"), *MaterialPath));
	}

	Material->Modify();

	bool bCreatedNode = false;
	UMaterialExpressionLandscapeLayerBlend* Blend = FindLayerBlendNode(Material, BlendNodeId);
	if (!Blend)
	{
		if (!BlendNodeId.IsEmpty())
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("add_layer_blend: LandscapeLayerBlend node '%s' not found in %s"), *BlendNodeId, *MaterialPath));
		}
		Blend = Cast<UMaterialExpressionLandscapeLayerBlend>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLandscapeLayerBlend::StaticClass(), 0, 0));
		if (!Blend)
		{
			return FMCPToolResult::Error(TEXT("add_layer_blend: failed to create LandscapeLayerBlend node"));
		}
		bCreatedNode = true;
	}

	if (FindLayerIndex(Blend, LayerName) != INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("add_layer_blend: layer '%s' already exists on blend node '%s'"), *LayerName, *Blend->GetName()));
	}

	FLayerBlendInput Input;
	Input.LayerName = FName(*LayerName);
	Input.BlendType = BlendType;
	Input.PreviewWeight = 1.0f;
	Blend->Layers.Add(Input);

	// RecompileMaterial performs the needed update+recompile; a preceding PostEditChange would
	// compile the same material a second time.
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();

	FString SaveError;
	const bool bSaved = SaveAssetChecked(MaterialPath, SaveError);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("material_path"), MaterialPath);
	Data->SetStringField(TEXT("blend_node_id"), Blend->GetName());
	Data->SetStringField(TEXT("layer"), LayerName);
	Data->SetStringField(TEXT("blend_type"), BlendTypeToString(BlendType));
	Data->SetNumberField(TEXT("layer_count"), Blend->Layers.Num());
	Data->SetBoolField(TEXT("created_node"), bCreatedNode);
	Data->SetBoolField(TEXT("saved"), bSaved);

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Added layer '%s' (%s) to blend node '%s'"), *LayerName, BlendTypeToString(BlendType), *Blend->GetName()),
		Data);
	if (!bSaved)
	{
		Result.Warnings.Add(SaveError);
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("add_layer_blend requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_LandscapeMaterial::OpSetLayerInfo(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString LayerName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("layer"), LayerName, Err))
	{
		return Err.GetValue();
	}

	const FString DestDir = NormalizeDir(ExtractOptionalString(Params, TEXT("destination_path"), TEXT("/Game/Landscape")));
	const bool bWeightBlended = ExtractOptionalBool(Params, TEXT("weight_blended"), true);

	// Asset is conventionally named <Layer>_LayerInfo.
	const FString AssetName = LayerName + TEXT("_LayerInfo");
	const FString AssetPath = DestDir + AssetName;

	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("set_layer_info: asset already exists: %s"), *AssetPath));
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("set_layer_info: failed to create package: %s"), *AssetPath));
	}

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
		Package, ULandscapeLayerInfoObject::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
	if (!LayerInfo)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("set_layer_info: failed to create ULandscapeLayerInfoObject: %s"), *AssetPath));
	}

	LayerInfo->Modify();
	// LayerName / bNoWeightBlend are deprecated direct fields in UE 5.7; use the setters.
	// bNoWeightBlend==true maps to BlendMethod::None; weight-blended maps to FinalWeightBlending.
	LayerInfo->SetLayerName(FName(*LayerName), /*bInModify=*/false);
	LayerInfo->SetBlendMethod(
		bWeightBlended ? ELandscapeTargetLayerBlendMethod::FinalWeightBlending : ELandscapeTargetLayerBlendMethod::None,
		/*bInModify=*/false);

	FAssetRegistryModule::AssetCreated(LayerInfo);
	LayerInfo->PostEditChange();
	Package->MarkPackageDirty();

	FString SaveError;
	const bool bSaved = SaveAssetChecked(AssetPath, SaveError);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("layer"), LayerName);
	Data->SetBoolField(TEXT("weight_blended"), bWeightBlended);
	Data->SetBoolField(TEXT("saved"), bSaved);

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Created landscape layer info '%s' (%s)"), *AssetName,
			bWeightBlended ? TEXT("weight-blended") : TEXT("no weight blend")),
		Data);
	if (!bSaved)
	{
		Result.Warnings.Add(SaveError);
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("set_layer_info requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_LandscapeMaterial::OpAddGrassOutput(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MaterialPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("material_path"), MaterialPath, Err))
	{
		return Err.GetValue();
	}

	const TSharedPtr<FJsonObject>* GrassTypesObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("grass_types"), GrassTypesObj) || (*GrassTypesObj)->Values.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("add_grass_output: 'grass_types' must be a non-empty object {input name -> LandscapeGrassType asset path}"));
	}

	const int32 PosX = ExtractOptionalNumber<int32>(Params, TEXT("pos_x"), 400);
	const int32 PosY = ExtractOptionalNumber<int32>(Params, TEXT("pos_y"), 0);

	UMaterial* Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(MaterialPath));
	if (!Material)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("add_grass_output: failed to load material: %s"), *MaterialPath));
	}

	Material->Modify();

	UMaterialExpressionLandscapeGrassOutput* GrassOutput = Cast<UMaterialExpressionLandscapeGrassOutput>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLandscapeGrassOutput::StaticClass(), PosX, PosY));
	if (!GrassOutput)
	{
		return FMCPToolResult::Error(TEXT("add_grass_output: failed to create LandscapeGrassOutput node"));
	}

	GrassOutput->GrassTypes.Empty();
	TArray<TSharedPtr<FJsonValue>> Entries;
	TArray<FString> Notes;

	for (const auto& Pair : (*GrassTypesObj)->Values)
	{
		FString GrassTypePath;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(GrassTypePath))
		{
			Notes.Add(FString::Printf(TEXT("grass input '%s': value is not a string asset path"), *Pair.Key));
			continue;
		}

		FGrassInput Input(FName(*Pair.Key));
		if (!GrassTypePath.IsEmpty())
		{
			ULandscapeGrassType* GrassType = Cast<ULandscapeGrassType>(UEditorAssetLibrary::LoadAsset(GrassTypePath));
			if (GrassType)
			{
				Input.GrassType = GrassType;
			}
			else
			{
				Notes.Add(FString::Printf(TEXT("grass input '%s': LandscapeGrassType not found: %s"), *Pair.Key, *GrassTypePath));
			}
		}
		GrassOutput->GrassTypes.Add(Input);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Pair.Key);
		Entry->SetStringField(TEXT("grass_type"), GrassTypePath);
		Entry->SetBoolField(TEXT("grass_type_loaded"), Input.GrassType != nullptr);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	if (GrassOutput->GrassTypes.Num() == 0)
	{
		return FMCPToolResult::Error(TEXT("add_grass_output: no valid grass type entries were parsed from 'grass_types'"));
	}

	// RecompileMaterial performs the needed update+recompile; a preceding PostEditChange would
	// compile the same material a second time.
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();

	FString SaveError;
	const bool bSaved = SaveAssetChecked(MaterialPath, SaveError);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("material_path"), MaterialPath);
	Data->SetStringField(TEXT("grass_output_id"), GrassOutput->GetName());
	Data->SetArrayField(TEXT("grass_types"), Entries);
	Data->SetBoolField(TEXT("saved"), bSaved);
	if (Notes.Num() > 0)
	{
		Data->SetArrayField(TEXT("notes"), StringArrayToJsonArray(Notes));
	}

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Added LandscapeGrassOutput '%s' with %d grass input(s) to %s"),
			*GrassOutput->GetName(), GrassOutput->GrassTypes.Num(), *MaterialPath),
		Data);
	Result.Warnings = Notes;
	if (!bSaved)
	{
		Result.Warnings.Add(SaveError);
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("add_grass_output requires an editor build"));
#endif
}
