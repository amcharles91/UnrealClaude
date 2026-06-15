// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Read and modify Unreal Engine project settings.
 *
 * Provides access to project configuration categories (general, maps, rendering,
 * physics, input, audio) backed by UDeveloperSettings subclasses, plus direct INI
 * access and project metadata (name, company, version). Changes are written to the
 * project's Default*.ini files and some require an editor restart.
 *
 * Ported from VibeUE's UProjectSettingsService into the native MCP tool pattern.
 *
 * NOTE: This is a STUB. All operations return "not implemented yet" until the
 * underlying engine integration is wired up.
 */
class FMCPTool_ProjectSettings : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpGet(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSet(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpListCategories(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetStartupMap(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetProjectMetadata(const TSharedRef<FJsonObject>& Params);
};
