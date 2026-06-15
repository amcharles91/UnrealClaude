// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Author and inspect Niagara particle systems.
 *
 * Combines VibeUE's UNiagaraService (system + user-parameter operations),
 * UNiagaraEmitterService (emitter modules + renderers), and
 * UNiagaraScratchPadService into a single native MCP tool. Operates on
 * UNiagaraSystem assets via the Niagara editor (FNiagaraSystemViewModel /
 * UNiagaraSystemFactory) and persists changes to the asset.
 *
 * STUB: schema and operation dispatch are complete, but every operation
 * currently returns "not implemented yet". The real implementation requires
 * the Niagara / NiagaraEditor module dependencies (see Build.cs) which are not
 * yet wired up, so this file is intentionally include-light and compiles
 * against the existing module set.
 *
 * Ported from VibeUE's UNiagaraService / UNiagaraEmitterService /
 * UNiagaraScratchPadService into the native MCP tool pattern.
 */
class FMCPTool_Niagara : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** List Niagara system assets; optional 'filter' name substring. */
	FMCPToolResult OpList(const TSharedRef<FJsonObject>& Params);

	/** Create a new Niagara system asset (system_name, destination_path, optional template_asset_path). */
	FMCPToolResult OpCreateSystem(const TSharedRef<FJsonObject>& Params);

	/** Get info/summary for a system (system_path): emitters, parameters, renderers. */
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);

	/** Add an emitter to a system (system_path, emitter_asset_path, optional emitter_name). */
	FMCPToolResult OpAddEmitter(const TSharedRef<FJsonObject>& Params);

	/** Remove an emitter from a system (system_path, emitter_name). */
	FMCPToolResult OpRemoveEmitter(const TSharedRef<FJsonObject>& Params);

	/** Set a system-level / user parameter value (system_path, parameter_name, value). */
	FMCPToolResult OpSetSystemParam(const TSharedRef<FJsonObject>& Params);

	/** Set a module input on an emitter (system_path, emitter_name, module_name, input_name, value). */
	FMCPToolResult OpSetEmitterParam(const TSharedRef<FJsonObject>& Params);

	/** Add a module to an emitter (system_path, emitter_name, module_script_path, module_type). */
	FMCPToolResult OpAddModule(const TSharedRef<FJsonObject>& Params);

	/** Add a renderer to an emitter (system_path, emitter_name, renderer_type). */
	FMCPToolResult OpAddRenderer(const TSharedRef<FJsonObject>& Params);
};
