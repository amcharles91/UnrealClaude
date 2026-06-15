// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Procedural FPS-map blockout generator.
 *
 * Reads weight layers from a VibeUE-generated landscape and produces a gated,
 * AAA-style map blockout through a staged pipeline (roads -> POIs -> fields ->
 * foliage -> railway/bridges -> final pass), then optionally materializes the
 * plan into real engine geometry (landscape splines, paint layers, spawned
 * actors, foliage instances).
 *
 * Ported from VibeUE's UMapBlockoutService into the native MCP tool pattern.
 *
 * STUB: schema + op-dispatch are complete; Execute bodies are not implemented
 * yet (each OpXxx returns "<op>: not implemented yet").
 */
class FMCPTool_MapBlockout : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	/** End-to-end: export landcover grid for a landscape, run Stages 0..5 + final pass, write deliverables. */
	FMCPToolResult OpGenerate(const TSharedRef<FJsonObject>& Params);

	/** Run a single pipeline stage (0..6) against accumulated state and return its gate result. */
	FMCPToolResult OpRunStage(const TSharedRef<FJsonObject>& Params);

	/** Return the current blockout plan / accumulated state (roads, POIs, masks, gates) without mutating the level. */
	FMCPToolResult OpGetPlan(const TSharedRef<FJsonObject>& Params);

	/** Turn the plan into actual engine geometry (splines, paint, actors, foliage). */
	FMCPToolResult OpMaterialize(const TSharedRef<FJsonObject>& Params);
};
