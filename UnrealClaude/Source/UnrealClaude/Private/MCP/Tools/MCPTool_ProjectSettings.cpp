// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_ProjectSettings.h"

FMCPToolInfo FMCPTool_ProjectSettings::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("project_settings");
	Info.Description = TEXT(
		"Read and modify Unreal Engine project settings (Default*.ini configuration).\n\n"
		"Operations (set 'operation'):\n"
		"- 'get': Read a setting 'key' within 'category' (e.g., category='general', key='ProjectName')\n"
		"- 'set': Set a setting 'key' to 'value' within 'category'\n"
		"- 'list_categories': List all available project settings categories\n"
		"- 'set_startup_map': Set the editor/game default startup map to 'map' (a /Game/... path)\n"
		"- 'get_project_metadata': Read project metadata (name, company, version, description)\n\n"
		"Categories include general, maps, rendering, physics, input, audio and any discovered "
		"UDeveloperSettings subclass. Changes are written to the project's config files and some "
		"require an editor restart.\n\n"
		"NOTE: This tool is a stub - operations are not implemented yet."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: get, set, list_categories, set_startup_map, get_project_metadata"), true),
		FMCPToolParameter(TEXT("category"), TEXT("string"), TEXT("For get/set: category id (e.g., 'general', 'maps', 'rendering')"), false),
		FMCPToolParameter(TEXT("key"), TEXT("string"), TEXT("For get/set: setting key name within the category (e.g., 'ProjectName')"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For set: new value as string"), false),
		FMCPToolParameter(TEXT("map"), TEXT("string"), TEXT("For set_startup_map: map asset path (e.g., '/Game/Maps/MainMenu')"), false),
		FMCPToolParameter(TEXT("startup_type"), TEXT("string"), TEXT("For set_startup_map: 'editor' or 'game' (default: both)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_ProjectSettings::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("get"))                  { return OpGet(Params); }
	if (Operation == TEXT("set"))                  { return OpSet(Params); }
	if (Operation == TEXT("list_categories"))      { return OpListCategories(Params); }
	if (Operation == TEXT("set_startup_map"))      { return OpSetStartupMap(Params); }
	if (Operation == TEXT("get_project_metadata")) { return OpGetProjectMetadata(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: get, set, list_categories, set_startup_map, get_project_metadata"), *Operation));
}

FMCPToolResult FMCPTool_ProjectSettings::OpGet(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get: not implemented yet"));
}

FMCPToolResult FMCPTool_ProjectSettings::OpSet(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set: not implemented yet"));
}

FMCPToolResult FMCPTool_ProjectSettings::OpListCategories(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("list_categories: not implemented yet"));
}

FMCPToolResult FMCPTool_ProjectSettings::OpSetStartupMap(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_startup_map: not implemented yet"));
}

FMCPToolResult FMCPTool_ProjectSettings::OpGetProjectMetadata(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_project_metadata: not implemented yet"));
}
