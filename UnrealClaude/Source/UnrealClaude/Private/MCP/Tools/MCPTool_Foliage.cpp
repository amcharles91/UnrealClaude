// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Foliage.h"

// NOTE: STUB. Includes are intentionally minimal so the tool compiles before the
// Foliage / FoliageEdit module dependencies are added to Build.cs. The real
// implementation will add: #include "FoliageType.h", "InstancedFoliageActor.h", etc.

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
		"NOTE: This tool is a stub; operations are not implemented yet.\n\n"
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
	return FMCPToolResult::Error(TEXT("add_type: not implemented yet"));
}

FMCPToolResult FMCPTool_Foliage::OpScatter(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("scatter: not implemented yet"));
}

FMCPToolResult FMCPTool_Foliage::OpPaint(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("paint: not implemented yet"));
}

FMCPToolResult FMCPTool_Foliage::OpRemove(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove: not implemented yet"));
}

FMCPToolResult FMCPTool_Foliage::OpListTypes(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("list_types: not implemented yet"));
}

FMCPToolResult FMCPTool_Foliage::OpGetInstances(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_instances: not implemented yet"));
}
