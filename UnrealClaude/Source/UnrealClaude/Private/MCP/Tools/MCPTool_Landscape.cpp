// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Landscape.h"
#include "UnrealClaudeModule.h"

// NOTE: STUB implementation. Real ops require the Landscape + LandscapeEditor
// module headers (e.g. "Landscape.h", "LandscapeProxy.h", "LandscapeInfo.h",
// "LandscapeEdit.h", "LandscapeStreamingProxy.h") and the RuntimeVirtualTexture
// module ("VT/RuntimeVirtualTexture.h"). These are intentionally NOT included
// yet so the stub compiles before the module dependencies are added to Build.cs.

FMCPToolInfo FMCPTool_Landscape::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("landscape");
	Info.Description = TEXT(
		"Author and edit Unreal Engine Landscapes (create, sculpt, heightmaps, paint layers, splines, holes, RVT).\n\n"
		"Operations (set 'operation'):\n"
		"- 'create': Spawn a new landscape actor at 'location'/'rotation'/'scale' with the given component grid "
		"('sections_per_component', 'quads_per_section', 'component_count_x', 'component_count_y'); optional 'label'\n"
		"- 'get_info': Resolution, component grid, bounds, layers, and material for 'landscape'\n"
		"- 'sculpt': Raise/lower terrain at ('world_x','world_y') with 'brush_radius' and 'strength' (signed world units), "
		"optional 'falloff' (Linear, Smooth, Spherical, Tip)\n"
		"- 'set_heightmap': Import a 16-bit heightmap into 'landscape' from 'file_path' (resolution must match exactly), "
		"or set 'heights' in the region ('start_x','start_y','size_x','size_y')\n"
		"- 'paint_layer': Paint weight for 'layer' at ('world_x','world_y') with 'brush_radius' and 'strength' (0..1)\n"
		"- 'add_layer': Add a paint layer to 'landscape' from a ULandscapeLayerInfoObject at 'layer_info_path'\n"
		"- 'add_spline': Create a spline path from 'points' (array of world locations); optional 'width', "
		"'side_falloff', 'end_falloff', 'paint_layer', 'raise_terrain', 'lower_terrain', 'closed_loop'\n"
		"- 'add_hole': Punch (or fill) a visibility hole at ('world_x','world_y') with 'brush_radius'; "
		"set 'create_hole' false to fill\n"
		"- 'assign_rvt': Assign a Runtime Virtual Texture at 'rvt_path' to 'landscape' at 'slot_index'\n\n"
		"Locations/rotations/scale are nested objects ({x,y,z} / {pitch,yaw,roll}). World coordinates are in cm.\n\n"
		"Returns: operation-specific data (created actor info, landscape info, or edit confirmation)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: create, get_info, sculpt, set_heightmap, paint_layer, add_layer, add_spline, add_hole, assign_rvt"), true),
		FMCPToolParameter(TEXT("landscape"), TEXT("string"), TEXT("Target landscape actor name or label (required for all ops except 'create')"), false),
		// create
		FMCPToolParameter(TEXT("location"), TEXT("object"), TEXT("For 'create': world location {x,y,z} (default 0,0,0)"), false),
		FMCPToolParameter(TEXT("rotation"), TEXT("object"), TEXT("For 'create': world rotation {pitch,yaw,roll} (default 0,0,0)"), false),
		FMCPToolParameter(TEXT("scale"), TEXT("object"), TEXT("For 'create': scale {x,y,z} (default 100,100,100 = 1m/unit)"), false),
		FMCPToolParameter(TEXT("sections_per_component"), TEXT("number"), TEXT("For 'create': 1 or 2 (default 1)"), false),
		FMCPToolParameter(TEXT("quads_per_section"), TEXT("number"), TEXT("For 'create': 7, 15, 31, 63, 127, or 255 (default 63)"), false),
		FMCPToolParameter(TEXT("component_count_x"), TEXT("number"), TEXT("For 'create': components in X (default 8)"), false),
		FMCPToolParameter(TEXT("component_count_y"), TEXT("number"), TEXT("For 'create': components in Y (default 8)"), false),
		FMCPToolParameter(TEXT("label"), TEXT("string"), TEXT("For 'create': optional display label for the new actor"), false),
		// sculpt / paint / hole shared world-brush params
		FMCPToolParameter(TEXT("world_x"), TEXT("number"), TEXT("For sculpt/paint_layer/add_hole: world X (cm)"), false),
		FMCPToolParameter(TEXT("world_y"), TEXT("number"), TEXT("For sculpt/paint_layer/add_hole: world Y (cm)"), false),
		FMCPToolParameter(TEXT("brush_radius"), TEXT("number"), TEXT("For sculpt/paint_layer/add_hole: brush radius in world units"), false),
		FMCPToolParameter(TEXT("strength"), TEXT("number"), TEXT("For 'sculpt': signed height delta (world units). For 'paint_layer': weight 0..1"), false),
		FMCPToolParameter(TEXT("falloff"), TEXT("string"), TEXT("For 'sculpt': brush falloff (Linear, Smooth, Spherical, Tip; default Linear)"), false),
		// set_heightmap
		FMCPToolParameter(TEXT("file_path"), TEXT("string"), TEXT("For 'set_heightmap': absolute path to a 16-bit heightmap (PNG/RAW); resolution must match landscape"), false),
		FMCPToolParameter(TEXT("start_x"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: start vertex X"), false),
		FMCPToolParameter(TEXT("start_y"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: start vertex Y"), false),
		FMCPToolParameter(TEXT("size_x"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: width in vertices"), false),
		FMCPToolParameter(TEXT("size_y"), TEXT("number"), TEXT("For 'set_heightmap'/'add_hole' region: height in vertices"), false),
		FMCPToolParameter(TEXT("heights"), TEXT("array"), TEXT("For 'set_heightmap' region: row-major float heights (size = size_x * size_y)"), false),
		// paint_layer / add_layer
		FMCPToolParameter(TEXT("layer"), TEXT("string"), TEXT("For 'paint_layer': name of the paint layer to paint"), false),
		FMCPToolParameter(TEXT("layer_info_path"), TEXT("string"), TEXT("For 'add_layer': path to the ULandscapeLayerInfoObject asset"), false),
		// add_spline
		FMCPToolParameter(TEXT("points"), TEXT("array"), TEXT("For 'add_spline': ordered array of world locations [{x,y,z},...]"), false),
		FMCPToolParameter(TEXT("width"), TEXT("number"), TEXT("For 'add_spline': control-point half-width (default 500)"), false),
		FMCPToolParameter(TEXT("side_falloff"), TEXT("number"), TEXT("For 'add_spline': side falloff (default 500)"), false),
		FMCPToolParameter(TEXT("end_falloff"), TEXT("number"), TEXT("For 'add_spline': end falloff (default 500)"), false),
		FMCPToolParameter(TEXT("paint_layer"), TEXT("string"), TEXT("For 'add_spline': layer painted under the spline (optional)"), false),
		FMCPToolParameter(TEXT("raise_terrain"), TEXT("boolean"), TEXT("For 'add_spline': raise terrain along spline (default true)"), false),
		FMCPToolParameter(TEXT("lower_terrain"), TEXT("boolean"), TEXT("For 'add_spline': lower terrain along spline (default true)"), false),
		FMCPToolParameter(TEXT("closed_loop"), TEXT("boolean"), TEXT("For 'add_spline': connect last point back to first (default false)"), false),
		// add_hole
		FMCPToolParameter(TEXT("create_hole"), TEXT("boolean"), TEXT("For 'add_hole': true punches a hole, false fills (default true)"), false),
		// assign_rvt
		FMCPToolParameter(TEXT("rvt_path"), TEXT("string"), TEXT("For 'assign_rvt': path to the RuntimeVirtualTexture asset"), false),
		FMCPToolParameter(TEXT("slot_index"), TEXT("number"), TEXT("For 'assign_rvt': slot index in the landscape's RVT array (default 0)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_Landscape::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("create"))        { return OpCreate(Params); }
	if (Operation == TEXT("get_info"))      { return OpGetInfo(Params); }
	if (Operation == TEXT("sculpt"))        { return OpSculpt(Params); }
	if (Operation == TEXT("set_heightmap")) { return OpSetHeightmap(Params); }
	if (Operation == TEXT("paint_layer"))   { return OpPaintLayer(Params); }
	if (Operation == TEXT("add_layer"))     { return OpAddLayer(Params); }
	if (Operation == TEXT("add_spline"))    { return OpAddSpline(Params); }
	if (Operation == TEXT("add_hole"))      { return OpAddHole(Params); }
	if (Operation == TEXT("assign_rvt"))    { return OpAssignRVT(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create, get_info, sculpt, set_heightmap, paint_layer, add_layer, add_spline, add_hole, assign_rvt"), *Operation));
}

FMCPToolResult FMCPTool_Landscape::OpCreate(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("create: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_info: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpSculpt(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("sculpt: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpSetHeightmap(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_heightmap: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpPaintLayer(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("paint_layer: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpAddLayer(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_layer: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpAddSpline(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_spline: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpAddHole(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_hole: not implemented yet"));
}

FMCPToolResult FMCPTool_Landscape::OpAssignRVT(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("assign_rvt: not implemented yet"));
}
