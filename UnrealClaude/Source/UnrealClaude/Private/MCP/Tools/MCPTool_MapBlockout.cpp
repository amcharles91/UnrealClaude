// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_MapBlockout.h"
#include "UnrealClaudeModule.h"

#include "MapBlockout/MapBlockoutPipeline.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"

// File-local helpers for JSON <-> USTRUCT marshaling of the map-blockout plan.
namespace
{
	/** Serialize any map-blockout USTRUCT result into a JSON object for FMCPToolResult::Success. */
	template <typename TStruct>
	TSharedPtr<FJsonObject> StructToJson(const TStruct& Value)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(TStruct::StaticStruct(), &Value, Obj.ToSharedRef(), 0, 0);
		return Obj;
	}

	/**
	 * Build the FMapBlockoutConfig from params: a full "config" object override if present, else assembled
	 * from the convenience top-level fields (level_name, output_dir, seed, layers{crop,soil,flood,forest}).
	 */
	FMapBlockoutConfig BuildConfig(const TSharedRef<FJsonObject>& Params)
	{
		FMapBlockoutConfig Config;

		const TSharedPtr<FJsonObject>* ConfigObj = nullptr;
		if (Params->TryGetObjectField(TEXT("config"), ConfigObj) && ConfigObj && (*ConfigObj).IsValid())
		{
			FJsonObjectConverter::JsonObjectToUStruct((*ConfigObj).ToSharedRef(), FMapBlockoutConfig::StaticStruct(), &Config, 0, 0);
			return Config;
		}

		Params->TryGetStringField(TEXT("level_name"), Config.LevelName);
		Params->TryGetStringField(TEXT("output_dir"), Config.OutputDir);
		double SeedVal = 0.0;
		if (Params->TryGetNumberField(TEXT("seed"), SeedVal)) { Config.Seed = (int32)SeedVal; }

		const TSharedPtr<FJsonObject>* LayersObj = nullptr;
		if (Params->TryGetObjectField(TEXT("layers"), LayersObj) && LayersObj && (*LayersObj).IsValid())
		{
			(*LayersObj)->TryGetStringField(TEXT("crop"), Config.Layers.Crop);
			(*LayersObj)->TryGetStringField(TEXT("soil"), Config.Layers.Soil);
			(*LayersObj)->TryGetStringField(TEXT("flood"), Config.Layers.Flood);
			(*LayersObj)->TryGetStringField(TEXT("forest"), Config.Layers.Forest);
		}
		return Config;
	}

	/** Comma-joined names of the failed checks in a gate, for actionable failure messages. */
	FString FailedCheckNames(const FMapBlockoutGateResult& Gate)
	{
		FString Names;
		for (const FMapBlockoutCheckResult& C : Gate.Checks)
		{
			if (!C.bPassed)
			{
				if (!Names.IsEmpty()) { Names += TEXT(", "); }
				Names += C.Name;
			}
		}
		return Names;
	}

	/**
	 * Strip the bulky per-cell arrays (grid height/weights + stage masks) from a serialized
	 * FMapBlockoutState JSON so get_plan / generate responses stay small. These arrays carry no
	 * decision value for a caller inspecting the plan; roads/POIs/gates/coverage are retained.
	 * Keys are FJsonObjectConverter's camelCase form; missing fields are silently skipped.
	 */
	void PrunePlanArrays(const TSharedPtr<FJsonObject>& State)
	{
		if (!State.IsValid()) { return; }

		const TSharedPtr<FJsonObject>* GridObj = nullptr;
		if (State->TryGetObjectField(TEXT("grid"), GridObj) && GridObj && (*GridObj).IsValid())
		{
			(*GridObj)->RemoveField(TEXT("heightNormalized"));
			const TArray<TSharedPtr<FJsonValue>>* LayersArr = nullptr;
			if ((*GridObj)->TryGetArrayField(TEXT("layers"), LayersArr) && LayersArr)
			{
				for (const TSharedPtr<FJsonValue>& V : *LayersArr)
				{
					const TSharedPtr<FJsonObject> LayerObj = V.IsValid() ? V->AsObject() : nullptr;
					if (LayerObj.IsValid()) { LayerObj->RemoveField(TEXT("weights")); }
				}
			}
		}

		const TSharedPtr<FJsonObject>* S3 = nullptr;
		if (State->TryGetObjectField(TEXT("stage3Fields"), S3) && S3 && (*S3).IsValid())
		{
			const TSharedPtr<FJsonObject>* FM = nullptr;
			if ((*S3)->TryGetObjectField(TEXT("fieldMask"), FM) && FM && (*FM).IsValid()) { (*FM)->RemoveField(TEXT("cells")); }
		}

		const TSharedPtr<FJsonObject>* S4 = nullptr;
		if (State->TryGetObjectField(TEXT("stage4Foliage"), S4) && S4 && (*S4).IsValid())
		{
			for (const TCHAR* MaskField : { TEXT("forestMask"), TEXT("treelineMask"), TEXT("scrubMask") })
			{
				const TSharedPtr<FJsonObject>* MO = nullptr;
				if ((*S4)->TryGetObjectField(MaskField, MO) && MO && (*MO).IsValid()) { (*MO)->RemoveField(TEXT("cells")); }
			}
		}
	}
}

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
		FMCPToolParameter(TEXT("folder_path"), TEXT("string"), TEXT("For 'materialize' pois: world-outliner folder for placeholder actors (e.g. '/MapBlockout/POIs/')"), false),
		FMCPToolParameter(TEXT("field_layer"), TEXT("string"), TEXT("For 'materialize' fields: landscape weight layer to paint (default 'Crop')"), false),
		FMCPToolParameter(TEXT("building_mesh_path"), TEXT("string"), TEXT("For 'materialize' pois: optional static mesh for building placeholders (empty = empty actors)"), false),
		FMCPToolParameter(TEXT("bridge_mesh_path"), TEXT("string"), TEXT("For 'materialize' railway: optional static mesh for bridge placeholders"), false),
		FMCPToolParameter(TEXT("forest_foliage_type"), TEXT("string"), TEXT("For 'materialize' foliage: FoliageType (or static mesh) asset path for dense forest"), false),
		FMCPToolParameter(TEXT("treeline_foliage_type"), TEXT("string"), TEXT("For 'materialize' foliage: FoliageType asset path for treeline rings"), false),
		FMCPToolParameter(TEXT("scrub_foliage_type"), TEXT("string"), TEXT("For 'materialize' foliage: FoliageType asset path for scrub/underbrush"), false)
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
	FString Landscape;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("landscape"), Landscape, Err)) { return Err.GetValue(); }

	const FMapBlockoutConfig Config = BuildConfig(Params);
	const int32 GridN = ExtractOptionalNumber<int32>(Params, TEXT("grid_n"), 120);

	// Stage 0: export the landcover grid from the live landscape.
	const FMapBlockoutLandcoverGrid Grid = MapBlockoutPipeline::ExportLandcoverGrid(Landscape, GridN);
	if (!Grid.bSuccess)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("generate: landcover export failed: %s"), *Grid.ErrorMessage));
	}

	// Run Stages 1..5 + final pass and write the deliverables.
	const FMapBlockoutPipelineResult Result = MapBlockoutPipeline::RunFullPipeline(Grid, Config);

	// Persist the accumulated plan for subsequent run_stage / get_plan / materialize calls.
	CurrentState = MakeShared<FMapBlockoutState>(Result.FinalState);

	FString Msg;
	if (Result.bSuccess)
	{
		Msg = FString::Printf(TEXT("Generated blockout '%s': all gates passed, %d file(s) -> %s"),
			*Config.LevelName, Result.OutputFiles.Num(), *Result.OutputDir);
	}
	else
	{
		// Surface the failing check names from whichever stage gate tripped first.
		const FMapBlockoutState& FS = Result.FinalState;
		const FMapBlockoutGateResult* FailedGate = nullptr;
		if (!FS.Stage1Roads.Gate.bAllPassed && FS.Stage1Roads.Gate.Checks.Num())        { FailedGate = &FS.Stage1Roads.Gate; }
		else if (!FS.Stage2Pois.Gate.bAllPassed && FS.Stage2Pois.Gate.Checks.Num())     { FailedGate = &FS.Stage2Pois.Gate; }
		else if (!FS.Stage3Fields.Gate.bAllPassed && FS.Stage3Fields.Gate.Checks.Num()) { FailedGate = &FS.Stage3Fields.Gate; }
		else if (!FS.Stage4Foliage.Gate.bAllPassed && FS.Stage4Foliage.Gate.Checks.Num()){ FailedGate = &FS.Stage4Foliage.Gate; }
		else if (!FS.Stage5Railway.Gate.bAllPassed && FS.Stage5Railway.Gate.Checks.Num()){ FailedGate = &FS.Stage5Railway.Gate; }
		const FString Failed = FailedGate ? FailedCheckNames(*FailedGate) : FString();
		Msg = Failed.IsEmpty()
			? FString::Printf(TEXT("Blockout '%s' stopped at a failing gate: %s"), *Config.LevelName, *Result.ErrorMessage)
			: FString::Printf(TEXT("Blockout '%s' stopped: %s [failed checks: %s]"), *Config.LevelName, *Result.ErrorMessage, *Failed);
	}

	TSharedPtr<FJsonObject> Data = StructToJson(Result);
	const TSharedPtr<FJsonObject>* FinalStateObj = nullptr;
	if (Data->TryGetObjectField(TEXT("finalState"), FinalStateObj) && FinalStateObj)
	{
		PrunePlanArrays(*FinalStateObj);  // drop the heavy arrays from the embedded state
	}
	return FMCPToolResult::Success(Msg, Data);
}

FMCPToolResult FMCPTool_MapBlockout::OpRunStage(const TSharedRef<FJsonObject>& Params)
{
	double StageVal = -1.0;
	if (!Params->TryGetNumberField(TEXT("stage"), StageVal))
	{
		return FMCPToolResult::Error(TEXT("run_stage: 'stage' (0-6) is required"));
	}
	const int32 Stage = (int32)StageVal;
	if (Stage < 0 || Stage > 6)
	{
		return FMCPToolResult::Error(TEXT("run_stage: 'stage' must be 0-6 (0=grid,1=roads,2=pois,3=fields,4=foliage,5=railway,6=final)"));
	}

	if (!CurrentState.IsValid())
	{
		CurrentState = MakeShared<FMapBlockoutState>();
	}
	// Seed/update the config inline on the first call (or whenever provided).
	if (Params->HasField(TEXT("config")) || Params->HasField(TEXT("level_name")) || Params->HasField(TEXT("layers")))
	{
		CurrentState->Config = BuildConfig(Params);
	}
	FMapBlockoutState& S = *CurrentState;

	auto GateMsg = [Stage](const FMapBlockoutGateResult& Gate) -> FString
	{
		FString Msg = FString::Printf(TEXT("Stage %d gate: %s (%d/%d checks passed)"),
			Stage, Gate.bAllPassed ? TEXT("PASS") : TEXT("FAIL"),
			Gate.Checks.Num() - Gate.FailedCount, Gate.Checks.Num());
		if (!Gate.bAllPassed)
		{
			const FString Failed = FailedCheckNames(Gate);
			if (!Failed.IsEmpty()) { Msg += FString::Printf(TEXT(" — failed: [%s]"), *Failed); }
		}
		return Msg;
	};

	switch (Stage)
	{
	case 0:
	{
		FString Landscape;
		TOptional<FMCPToolResult> Err;
		if (!ExtractRequiredString(Params, TEXT("landscape"), Landscape, Err)) { return Err.GetValue(); }
		const int32 GridN = ExtractOptionalNumber<int32>(Params, TEXT("grid_n"), 120);
		S.Grid = MapBlockoutPipeline::ExportLandcoverGrid(Landscape, GridN);
		if (!S.Grid.bSuccess) { return FMCPToolResult::Error(FString::Printf(TEXT("run_stage 0: %s"), *S.Grid.ErrorMessage)); }
		return FMCPToolResult::Success(FString::Printf(TEXT("Stage 0: exported %dx%d landcover grid (%d layer(s))"), S.Grid.GridN, S.Grid.GridN, S.Grid.Layers.Num()), StructToJson(S.Grid));
	}
	case 1:
		S.Stage1Roads = MapBlockoutPipeline::GenerateRoads(S.Grid, S.Config);
		return FMCPToolResult::Success(GateMsg(S.Stage1Roads.Gate), StructToJson(S.Stage1Roads));
	case 2:
		S.Stage2Pois = MapBlockoutPipeline::PlacePois(S.Grid, S.Stage1Roads, S.Config);
		return FMCPToolResult::Success(GateMsg(S.Stage2Pois.Gate), StructToJson(S.Stage2Pois));
	case 3:
		S.Stage3Fields = MapBlockoutPipeline::PlaceFields(S.Grid, S.Stage1Roads, S.Stage2Pois, S.Config);
		return FMCPToolResult::Success(GateMsg(S.Stage3Fields.Gate), StructToJson(S.Stage3Fields));
	case 4:
		S.Stage4Foliage = MapBlockoutPipeline::PlaceFoliage(S.Grid, S.Stage1Roads, S.Stage2Pois, S.Stage3Fields, S.Config);
		return FMCPToolResult::Success(GateMsg(S.Stage4Foliage.Gate), StructToJson(S.Stage4Foliage));
	case 5:
		S.Stage5Railway = MapBlockoutPipeline::PlaceRailway(S.Grid, S.Stage1Roads, S.Stage2Pois, S.Stage3Fields, S.Stage4Foliage, S.Config);
		return FMCPToolResult::Success(GateMsg(S.Stage5Railway.Gate), StructToJson(S.Stage5Railway));
	default: // 6
	{
		const FMapBlockoutGateResult Gate = MapBlockoutPipeline::RunFinalPass(S);
		return FMCPToolResult::Success(GateMsg(Gate), StructToJson(Gate));
	}
	}
}

FMCPToolResult FMCPTool_MapBlockout::OpGetPlan(const TSharedRef<FJsonObject>& Params)
{
	if (!CurrentState.IsValid())
	{
		return FMCPToolResult::Error(TEXT("get_plan: no plan yet — run 'generate' or 'run_stage' first"));
	}
	TSharedPtr<FJsonObject> Data = StructToJson(*CurrentState);
	PrunePlanArrays(Data);  // drop multi-MB raw grid/mask cell arrays; keep roads/POIs/gates/coverage
	return FMCPToolResult::Success(TEXT("Current map-blockout plan (raw grid/mask cell arrays omitted)"), Data);
}

FMCPToolResult FMCPTool_MapBlockout::OpMaterialize(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	if (!CurrentState.IsValid())
	{
		return FMCPToolResult::Error(TEXT("materialize: no plan yet — run 'generate' first"));
	}
	const FString Target = ExtractOptionalString(Params, TEXT("target"), TEXT("all")).ToLower();
	const FString Landscape = ExtractOptionalString(Params, TEXT("landscape"));
	const FString FolderPath = ExtractOptionalString(Params, TEXT("folder_path"), TEXT("/MapBlockout/POIs/"));
	const FString FieldLayer = ExtractOptionalString(Params, TEXT("field_layer"), TEXT("Crop"));
	const FString BuildingMesh = ExtractOptionalString(Params, TEXT("building_mesh_path"));
	const FString BridgeMesh = ExtractOptionalString(Params, TEXT("bridge_mesh_path"));
	const FString ForestFt = ExtractOptionalString(Params, TEXT("forest_foliage_type"));
	const FString TreelineFt = ExtractOptionalString(Params, TEXT("treeline_foliage_type"));
	const FString ScrubFt = ExtractOptionalString(Params, TEXT("scrub_foliage_type"));

	const bool bAll = Target == TEXT("all");
	if (!bAll && Target != TEXT("roads") && Target != TEXT("fields") && Target != TEXT("pois") && Target != TEXT("foliage") && Target != TEXT("railway"))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("materialize: unknown target '%s'. Valid: roads, fields, pois, foliage, railway, all"), *Target));
	}

	const FMapBlockoutState& S = *CurrentState;
	int32 Total = 0;
	TArray<FString> Notes;
	auto Run = [&](const TCHAR* Name, const FMapBlockoutMaterializeResult& R)
	{
		if (R.bSuccess) { Total += R.CreatedCount; }
		else { Notes.Add(FString::Printf(TEXT("%s: %s"), Name, *R.ErrorMessage)); }
	};

	if (bAll || Target == TEXT("roads"))
	{
		if (Landscape.IsEmpty()) { Notes.Add(TEXT("roads: 'landscape' required")); }
		else { Run(TEXT("roads"), MapBlockoutPipeline::MaterializeRoadsAsSplines(S.Stage1Roads, Landscape)); }
	}
	if (bAll || Target == TEXT("fields"))
	{
		if (Landscape.IsEmpty()) { Notes.Add(TEXT("fields: 'landscape' required")); }
		else { Run(TEXT("fields"), MapBlockoutPipeline::MaterializeFieldsAsPaint(S.Stage3Fields, Landscape, FieldLayer)); }
	}
	if (bAll || Target == TEXT("pois"))
	{
		Run(TEXT("pois"), MapBlockoutPipeline::MaterializePoisAsActors(S.Stage2Pois, FolderPath, BuildingMesh));
	}
	if (bAll || Target == TEXT("foliage"))
	{
		if (ForestFt.IsEmpty() && TreelineFt.IsEmpty() && ScrubFt.IsEmpty())
		{
			Notes.Add(TEXT("foliage: provide forest_foliage_type / treeline_foliage_type / scrub_foliage_type"));
		}
		else { Run(TEXT("foliage"), MapBlockoutPipeline::MaterializeForestAsFoliage(S.Stage4Foliage, S.Grid.WorldLo, S.Grid.WorldHi, ForestFt, TreelineFt, ScrubFt)); }
	}
	if (bAll || Target == TEXT("railway"))
	{
		if (Landscape.IsEmpty()) { Notes.Add(TEXT("railway: 'landscape' required")); }
		else { Run(TEXT("railway"), MapBlockoutPipeline::MaterializeRailwayAndBridges(S.Stage5Railway, Landscape, BridgeMesh)); }
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("target"), Target);
	Data->SetNumberField(TEXT("created_count"), Total);
	Data->SetArrayField(TEXT("notes"), StringArrayToJsonArray(Notes));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Materialized %s: %d primitive(s)%s"), *Target, Total, Notes.Num() ? TEXT(" (with notes)") : TEXT("")), Data);
#else
	return FMCPToolResult::Error(TEXT("materialize requires an editor build"));
#endif
}
