// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Viewport.h"

FMCPToolInfo FMCPTool_Viewport::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("viewport");
	Info.Description = TEXT(
		"Control the Unreal Editor level viewport (camera, view mode, exposure, layout).\n\n"
		"Operations (set 'operation'):\n"
		"- 'set_camera': Set camera 'location' {x,y,z} and/or 'rotation' {pitch,yaw,roll}\n"
		"- 'set_view_mode': Set rendering mode 'view_mode' (lit, unlit, wireframe, detaillighting, lightingonly, shadercomplexity, pathtracing)\n"
		"- 'set_fov': Set perspective horizontal field of view to 'fov' degrees (typically 60-120)\n"
		"- 'set_exposure': Set 'fixed' exposure (bool) and 'ev100' value, or auto game settings\n"
		"- 'set_layout': Set viewport 'layout' (OnePane, TwoPanesHoriz, TwoPanesVert, FourPanes2x2, etc.)\n"
		"- 'toggle_game_view': Enable/disable Game View via 'enable' (hides editor-only visualizations)\n\n"
		"Operates on the active level editor viewport. Requires an editor build.\n\n"
		"NOTE: This tool is a stub - operations are not implemented yet."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: set_camera, set_view_mode, set_fov, set_exposure, set_layout, toggle_game_view"), true),
		FMCPToolParameter(TEXT("location"), TEXT("object"), TEXT("For set_camera: world location { x, y, z }"), false),
		FMCPToolParameter(TEXT("rotation"), TEXT("object"), TEXT("For set_camera: world rotation { pitch, yaw, roll }"), false),
		FMCPToolParameter(TEXT("view_mode"), TEXT("string"), TEXT("For set_view_mode: lit, unlit, wireframe, detaillighting, lightingonly, shadercomplexity, pathtracing"), false),
		FMCPToolParameter(TEXT("fov"), TEXT("number"), TEXT("For set_fov: horizontal field of view in degrees"), false),
		FMCPToolParameter(TEXT("fixed"), TEXT("boolean"), TEXT("For set_exposure: true for fixed exposure, false for auto eye adaptation"), false),
		FMCPToolParameter(TEXT("ev100"), TEXT("number"), TEXT("For set_exposure: fixed EV100 exposure value (used when fixed=true)"), false),
		FMCPToolParameter(TEXT("layout"), TEXT("string"), TEXT("For set_layout: layout name (OnePane, TwoPanesHoriz, FourPanes2x2, etc.)"), false),
		FMCPToolParameter(TEXT("enable"), TEXT("boolean"), TEXT("For toggle_game_view: true to enable Game View, false to disable"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_Viewport::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("set_camera"))       { return OpSetCamera(Params); }
	if (Operation == TEXT("set_view_mode"))    { return OpSetViewMode(Params); }
	if (Operation == TEXT("set_fov"))          { return OpSetFov(Params); }
	if (Operation == TEXT("set_exposure"))     { return OpSetExposure(Params); }
	if (Operation == TEXT("set_layout"))       { return OpSetLayout(Params); }
	if (Operation == TEXT("toggle_game_view")) { return OpToggleGameView(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: set_camera, set_view_mode, set_fov, set_exposure, set_layout, toggle_game_view"), *Operation));
}

FMCPToolResult FMCPTool_Viewport::OpSetCamera(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_camera: not implemented yet"));
}

FMCPToolResult FMCPTool_Viewport::OpSetViewMode(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_view_mode: not implemented yet"));
}

FMCPToolResult FMCPTool_Viewport::OpSetFov(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_fov: not implemented yet"));
}

FMCPToolResult FMCPTool_Viewport::OpSetExposure(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_exposure: not implemented yet"));
}

FMCPToolResult FMCPTool_Viewport::OpSetLayout(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_layout: not implemented yet"));
}

FMCPToolResult FMCPTool_Viewport::OpToggleGameView(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("toggle_game_view: not implemented yet"));
}
