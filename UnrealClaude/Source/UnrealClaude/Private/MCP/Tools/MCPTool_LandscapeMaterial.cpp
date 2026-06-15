// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_LandscapeMaterial.h"
#include "UnrealClaudeModule.h"

// NOTE: STUB implementation. Real ops require the Landscape module (for
// ULandscapeLayerInfoObject, UMaterialExpressionLandscapeLayerBlend,
// UMaterialExpressionLandscapeGrassOutput) plus UnrealEd / MaterialEditor for
// asset creation and material-graph mutation (e.g. "Materials/Material.h",
// "MaterialEditingLibrary.h", "AssetToolsModule.h"). These are intentionally
// NOT included yet so the stub compiles before the module deps are added.

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
	return FMCPToolResult::Error(TEXT("create_auto_material: not implemented yet"));
}

FMCPToolResult FMCPTool_LandscapeMaterial::OpAddLayerBlend(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_layer_blend: not implemented yet"));
}

FMCPToolResult FMCPTool_LandscapeMaterial::OpSetLayerInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_layer_info: not implemented yet"));
}

FMCPToolResult FMCPTool_LandscapeMaterial::OpAddGrassOutput(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_grass_output: not implemented yet"));
}
