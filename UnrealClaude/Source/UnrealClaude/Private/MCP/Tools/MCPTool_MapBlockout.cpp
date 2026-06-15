// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_MapBlockout.h"
#include "UnrealClaudeModule.h"

// NOTE (STUB): includes are intentionally minimal so this compiles before the
// full implementation lands. The real implementation will additionally need:
//   - Landscape  : "Landscape", "LandscapeEditor", "LandscapeEditorUtilities"  (weight-layer export, splines, paint)
//   - Foliage    : "Foliage", "FoliageEdit"                                    (scatter forest/treeline/scrub)
//   - Rendering  : "ImageWrapper", "RenderCore", "RHI"                         (per-stage PNG snapshots + heatmaps)
//   - Editor     : "UnrealEd", "EditorScriptingUtilities"                      (spawn POI/bridge placeholder actors)
// plus the engine PLUGINS "Landscape" and "Foliage" (and "EditorScriptingUtilities")
// enabled in UnrealClaude.uplugin. See the report for the full module list.

FMCPToolInfo FMCPTool_MapBlockout::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("map_blockout");
	Info.Description = TEXT(
		"Procedurally generate a gated, AAA-style FPS-map blockout from a VibeUE landscape "
		"(roads, POIs, fields, foliage, railway/bridges), then materialize it into engine geometry.\n\n"
		"The plan is built as a staged pipeline; each stage runs its own pass/fail gate and the "
		"orchestrator stops at the first failing gate so you can inspect/repair before advancing:\n"
		"  Stage 0 = landcover grid export, 1 = roads, 2 = POIs, 3 = fields, 4 = foliage, "
		"5 = railway/bridges, 6 = final pass.\n\n"
		"Operations (set 'operation'):\n"
		"- 'generate': Export the landcover grid for 'landscape' and run the whole pipeline "
		"(Stages 0..5 + final pass), writing per-stage PNGs and the final deliverables.\n"
		"- 'run_stage': Run a single 'stage' (0-6) against the accumulated plan and return its gate result. "
		"Use to step the pipeline and repair a failing gate.\n"
		"- 'get_plan': Return the current accumulated plan/state (roads, POIs, field/forest masks, gates, "
		"output files) without mutating the level.\n"
		"- 'materialize': Turn the plan into real engine geometry. 'target' selects what to build: "
		"roads (landscape splines), fields (paint layer), pois (placeholder actors), foliage (scatter), "
		"railway (splines + bridge actors), or all.\n\n"
		"Returns: operation-specific data (gate results, plan structs, or materialized primitive counts)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: generate, run_stage, get_plan, materialize"), true),
		FMCPToolParameter(TEXT("landscape"), TEXT("string"), TEXT("For 'generate'/'materialize': actor label of the source VibeUE landscape"), false),
		FMCPToolParameter(TEXT("level_name"), TEXT("string"), TEXT("For 'generate': blockout name; output is written to <Project>/Saved/MapBlockout/<level_name>/"), false),
		FMCPToolParameter(TEXT("output_dir"), TEXT("string"), TEXT("For 'generate': override output directory (default <Project>/Saved/MapBlockout/<level_name>/)"), false),
		FMCPToolParameter(TEXT("seed"), TEXT("number"), TEXT("For 'generate': RNG seed for deterministic layout (default 7)"), false),
		FMCPToolParameter(TEXT("grid_n"), TEXT("number"), TEXT("For 'generate': landcover grid resolution (default 120)"), false),
		FMCPToolParameter(TEXT("layers"), TEXT("object"), TEXT("For 'generate': design-category -> paint-layer-name map {crop, soil, flood, forest}"), false),
		FMCPToolParameter(TEXT("config"), TEXT("object"), TEXT("For 'generate': full MapBlockoutConfig override (road, pois, coverage bands, thresholds, rivers)"), false),
		FMCPToolParameter(TEXT("stage"), TEXT("number"), TEXT("For 'run_stage': stage index 0-6 (0=grid, 1=roads, 2=pois, 3=fields, 4=foliage, 5=railway, 6=final pass)"), false),
		FMCPToolParameter(TEXT("target"), TEXT("string"), TEXT("For 'materialize': one of roads, fields, pois, foliage, railway, all (default: all)"), false),
		FMCPToolParameter(TEXT("folder_path"), TEXT("string"), TEXT("For 'materialize' pois: world-outliner folder for placeholder actors (e.g. '/MapBlockout/POIs/')"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_MapBlockout::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("generate"))    { return OpGenerate(Params); }
	if (Operation == TEXT("run_stage"))   { return OpRunStage(Params); }
	if (Operation == TEXT("get_plan"))    { return OpGetPlan(Params); }
	if (Operation == TEXT("materialize")) { return OpMaterialize(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: generate, run_stage, get_plan, materialize"), *Operation));
}

FMCPToolResult FMCPTool_MapBlockout::OpGenerate(const TSharedRef<FJsonObject>& Params)
{
	// TODO: ExportLandcoverGrid(landscape) -> RunFullPipeline(grid, config) -> write 8 deliverable files.
	return FMCPToolResult::Error(TEXT("generate: not implemented yet"));
}

FMCPToolResult FMCPTool_MapBlockout::OpRunStage(const TSharedRef<FJsonObject>& Params)
{
	// TODO: run the requested single stage (0..6) against accumulated state, return its FMapBlockoutGateResult.
	return FMCPToolResult::Error(TEXT("run_stage: not implemented yet"));
}

FMCPToolResult FMCPTool_MapBlockout::OpGetPlan(const TSharedRef<FJsonObject>& Params)
{
	// TODO: serialize the current FMapBlockoutState (roads, POIs, masks, gates, output files) to JSON.
	return FMCPToolResult::Error(TEXT("get_plan: not implemented yet"));
}

FMCPToolResult FMCPTool_MapBlockout::OpMaterialize(const TSharedRef<FJsonObject>& Params)
{
	// TODO: per 'target', materialize roads/fields/pois/foliage/railway into engine geometry.
	return FMCPToolResult::Error(TEXT("materialize: not implemented yet"));
}
