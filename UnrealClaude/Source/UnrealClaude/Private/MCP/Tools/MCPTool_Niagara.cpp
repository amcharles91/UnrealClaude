// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Niagara.h"
#include "UnrealClaudeModule.h"

// NOTE: This is a STUB. The real implementation requires the Niagara and
// NiagaraEditor module dependencies (engine PLUGIN "Niagara"); until those are
// added to UnrealClaude.Build.cs / UnrealClaude.uplugin, includes here are kept
// to the header + module log only so the tool compiles against the existing
// module set. Each operation returns "not implemented yet".

FMCPToolInfo FMCPTool_Niagara::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("niagara");
	Info.Description = TEXT(
		"Author and inspect Unreal Engine Niagara particle systems (create, query, emitters, parameters, modules, renderers).\n\n"
		"Operations (set 'operation'):\n"
		"- 'list': List Niagara system assets; optional 'filter' name substring\n"
		"- 'create_system': Create a system 'system_name' under 'destination_path' (optional 'template_asset_path')\n"
		"- 'get_info': Summary for 'system_path' (emitters, parameters, renderers)\n"
		"- 'add_emitter': Add 'emitter_asset_path' to 'system_path' (optional 'emitter_name')\n"
		"- 'remove_emitter': Remove 'emitter_name' from 'system_path'\n"
		"- 'set_system_param': Set system/user parameter 'parameter_name' to 'value' on 'system_path'\n"
		"- 'set_emitter_param': Set module input 'input_name' to 'value' on 'module_name' of 'emitter_name' in 'system_path'\n"
		"- 'add_module': Add module 'module_script_path' (stage 'module_type') to 'emitter_name' in 'system_path'\n"
		"- 'add_renderer': Add a 'renderer_type' renderer to 'emitter_name' in 'system_path'\n\n"
		"System paths are content paths (e.g., '/Game/FX/NS_Fire'). Module types/stages are one of: "
		"EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate.\n\n"
		"Returns: operation-specific data (system list/info, or the set of modified objects)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: list, create_system, get_info, add_emitter, remove_emitter, set_system_param, set_emitter_param, add_module, add_renderer"), true),
		FMCPToolParameter(TEXT("system_path"), TEXT("string"), TEXT("Content path of the Niagara system (e.g., '/Game/FX/NS_Fire'). Required for all ops except list/create_system."), false),
		FMCPToolParameter(TEXT("system_name"), TEXT("string"), TEXT("For 'create_system': asset name of the new system"), false),
		FMCPToolParameter(TEXT("destination_path"), TEXT("string"), TEXT("For 'create_system': content folder for the new system (e.g., '/Game/FX')"), false),
		FMCPToolParameter(TEXT("template_asset_path"), TEXT("string"), TEXT("For 'create_system': optional template/system to base the new system on"), false),
		FMCPToolParameter(TEXT("emitter_name"), TEXT("string"), TEXT("Emitter handle name within the system (for emitter/module/renderer ops)"), false),
		FMCPToolParameter(TEXT("emitter_asset_path"), TEXT("string"), TEXT("For 'add_emitter': content path of the emitter asset/template to add"), false),
		FMCPToolParameter(TEXT("parameter_name"), TEXT("string"), TEXT("For 'set_system_param': name of the system/user parameter to set"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For 'set_system_param'/'set_emitter_param': stringified value to assign"), false),
		FMCPToolParameter(TEXT("module_name"), TEXT("string"), TEXT("For 'set_emitter_param': display name of the module whose input to set"), false),
		FMCPToolParameter(TEXT("module_script_path"), TEXT("string"), TEXT("For 'add_module': content path of the module script to add"), false),
		FMCPToolParameter(TEXT("module_type"), TEXT("string"), TEXT("For 'add_module': stage to add to (EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate)"), false),
		FMCPToolParameter(TEXT("input_name"), TEXT("string"), TEXT("For 'set_emitter_param': name of the module input to set"), false),
		FMCPToolParameter(TEXT("renderer_type"), TEXT("string"), TEXT("For 'add_renderer': renderer class (e.g., Sprite, Ribbon, Mesh, Light)"), false),
		FMCPToolParameter(TEXT("filter"), TEXT("string"), TEXT("For 'list': optional name substring filter"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_Niagara::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("list"))              { return OpList(Params); }
	if (Operation == TEXT("create_system"))     { return OpCreateSystem(Params); }
	if (Operation == TEXT("get_info"))          { return OpGetInfo(Params); }
	if (Operation == TEXT("add_emitter"))       { return OpAddEmitter(Params); }
	if (Operation == TEXT("remove_emitter"))    { return OpRemoveEmitter(Params); }
	if (Operation == TEXT("set_system_param"))  { return OpSetSystemParam(Params); }
	if (Operation == TEXT("set_emitter_param")) { return OpSetEmitterParam(Params); }
	if (Operation == TEXT("add_module"))        { return OpAddModule(Params); }
	if (Operation == TEXT("add_renderer"))      { return OpAddRenderer(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: list, create_system, get_info, add_emitter, remove_emitter, set_system_param, set_emitter_param, add_module, add_renderer"), *Operation));
}

// ===== Operation stubs =====
// Each returns "not implemented yet" until the Niagara/NiagaraEditor dependencies
// are wired and the real bodies (ported from VibeUE) are filled in.

FMCPToolResult FMCPTool_Niagara::OpList(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("list: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpCreateSystem(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("create_system: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_info: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpAddEmitter(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_emitter: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpRemoveEmitter(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove_emitter: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpSetSystemParam(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_system_param: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpSetEmitterParam(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_emitter_param: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpAddModule(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_module: not implemented yet"));
}

FMCPToolResult FMCPTool_Niagara::OpAddRenderer(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_renderer: not implemented yet"));
}
