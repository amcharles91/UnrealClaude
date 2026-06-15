// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Read and modify Unreal Engine configuration.
 *
 * Provides access to engine settings categories (rendering, physics, audio, GC,
 * threading), console variables (cvars), scalability quality groups, and direct
 * engine INI access. Changes are written to engine config files and some require
 * an editor restart.
 *
 * Ported from VibeUE's UEngineSettingsService into the native MCP tool pattern.
 *
 * NOTE: This is a STUB. All operations return "not implemented yet" until the
 * underlying engine integration is wired up.
 */
class FMCPTool_EngineSettings : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpGetSetting(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetSetting(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetCVar(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetCVar(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetScalability(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetScalability(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGcSettings(const TSharedRef<FJsonObject>& Params);
};
