// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Manage Unreal Engine Animation Montages.
 *
 * Create montages and edit their sections, slot tracks, notifies, branch
 * points, and blend settings against UAnimMontage assets.
 *
 * STUB: scaffolded from VibeUE's UAnimMontageService. Operation dispatch and
 * schema are in place; the OpXxx() bodies return "not implemented yet" until
 * the underlying implementation is ported.
 */
class FMCPTool_AnimMontage : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddSection(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveSection(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetSlot(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddNotify(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddBranchPoint(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetBlend(const TSharedRef<FJsonObject>& Params);
};
