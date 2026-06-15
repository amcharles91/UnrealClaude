// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_EngineSettings.h"

FMCPToolInfo FMCPTool_EngineSettings::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("engine_settings");
	Info.Description = TEXT(
		"Read and modify Unreal Engine configuration (settings, console variables, scalability, GC).\n\n"
		"Operations (set 'operation'):\n"
		"- 'get_setting': Read a setting 'key' within 'category' (e.g., category='rendering')\n"
		"- 'set_setting': Set a setting 'key' to 'value' within 'category'\n"
		"- 'get_cvar': Read a console variable by 'name' (e.g., 'r.ReflectionMethod')\n"
		"- 'set_cvar': Set console variable 'name' to 'value'\n"
		"- 'get_scalability': Get current scalability quality levels (all groups)\n"
		"- 'set_scalability': Set scalability 'group' (or all) to quality 'level' (0=Low..4=Cinematic)\n"
		"- 'gc_settings': Read or modify garbage collection settings (gc.*)\n\n"
		"Categories include rendering, physics, audio, gc, threading. Changes are written to "
		"engine config files and some require an editor restart.\n\n"
		"NOTE: This tool is a stub - operations are not implemented yet."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: get_setting, set_setting, get_cvar, set_cvar, get_scalability, set_scalability, gc_settings"), true),
		FMCPToolParameter(TEXT("category"), TEXT("string"), TEXT("For get_setting/set_setting: category id (e.g., 'rendering', 'physics', 'audio', 'gc')"), false),
		FMCPToolParameter(TEXT("key"), TEXT("string"), TEXT("For get_setting/set_setting: setting key name within the category"), false),
		FMCPToolParameter(TEXT("name"), TEXT("string"), TEXT("For get_cvar/set_cvar: console variable name (e.g., 'r.ReflectionMethod')"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For set_setting/set_cvar/gc_settings: new value as string"), false),
		FMCPToolParameter(TEXT("group"), TEXT("string"), TEXT("For set_scalability: group (ViewDistance, AntiAliasing, Shadow, PostProcess, Texture, Effects, Foliage, Shading) or empty for all"), false),
		FMCPToolParameter(TEXT("level"), TEXT("number"), TEXT("For set_scalability: quality level 0=Low, 1=Medium, 2=High, 3=Epic, 4=Cinematic"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_EngineSettings::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("get_setting"))     { return OpGetSetting(Params); }
	if (Operation == TEXT("set_setting"))     { return OpSetSetting(Params); }
	if (Operation == TEXT("get_cvar"))        { return OpGetCVar(Params); }
	if (Operation == TEXT("set_cvar"))        { return OpSetCVar(Params); }
	if (Operation == TEXT("get_scalability")) { return OpGetScalability(Params); }
	if (Operation == TEXT("set_scalability")) { return OpSetScalability(Params); }
	if (Operation == TEXT("gc_settings"))     { return OpGcSettings(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: get_setting, set_setting, get_cvar, set_cvar, get_scalability, set_scalability, gc_settings"), *Operation));
}

FMCPToolResult FMCPTool_EngineSettings::OpGetSetting(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_setting: not implemented yet"));
}

FMCPToolResult FMCPTool_EngineSettings::OpSetSetting(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_setting: not implemented yet"));
}

FMCPToolResult FMCPTool_EngineSettings::OpGetCVar(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_cvar: not implemented yet"));
}

FMCPToolResult FMCPTool_EngineSettings::OpSetCVar(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_cvar: not implemented yet"));
}

FMCPToolResult FMCPTool_EngineSettings::OpGetScalability(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_scalability: not implemented yet"));
}

FMCPToolResult FMCPTool_EngineSettings::OpSetScalability(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_scalability: not implemented yet"));
}

FMCPToolResult FMCPTool_EngineSettings::OpGcSettings(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("gc_settings: not implemented yet"));
}
