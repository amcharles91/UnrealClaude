// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Viewport.h"
#include "UnrealClaudeUtils.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "Modules/ModuleManager.h"
#endif

#if WITH_EDITOR
namespace
{
	/** Get the active level editor viewport widget (may be invalid if no level editor is open). */
	TSharedPtr<SLevelViewport> GetActiveLevelViewport()
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (!LevelEditorModule)
		{
			return nullptr;
		}
		return LevelEditorModule->GetFirstActiveLevelViewport();
	}

	/**
	 * Resolve the active level editor viewport client. Prefers the first active
	 * level viewport; falls back to GEditor's first valid level viewport client.
	 */
	FLevelEditorViewportClient* GetActiveViewportClient()
	{
		TSharedPtr<SLevelViewport> LevelViewport = GetActiveLevelViewport();
		if (LevelViewport.IsValid())
		{
			return &LevelViewport->GetLevelViewportClient();
		}

		if (!GEditor)
		{
			return nullptr;
		}

		const TArray<FLevelEditorViewportClient*>& Clients = GEditor->GetLevelViewportClients();
		for (FLevelEditorViewportClient* Client : Clients)
		{
			if (Client)
			{
				return Client;
			}
		}
		return nullptr;
	}

	/**
	 * Force the viewport to visually redraw. In non-realtime mode the Slate
	 * viewport has no active tick timer, so we must invalidate both the client
	 * (marks display dirty) and the SLevelViewport widget (wakes it for redraw).
	 */
	void ForceViewportRedraw()
	{
		if (FLevelEditorViewportClient* Client = GetActiveViewportClient())
		{
			Client->Invalidate();
		}
		TSharedPtr<SLevelViewport> LevelViewport = GetActiveLevelViewport();
		if (LevelViewport.IsValid())
		{
			LevelViewport->Invalidate();
		}
	}

	/** Map a view-mode string to EViewModeIndex. Returns false if unknown. */
	bool ViewModeFromString(const FString& In, EViewModeIndex& OutMode)
	{
		const FString Lower = In.ToLower().TrimStartAndEnd();
		if (Lower == TEXT("lit"))                  { OutMode = VMI_Lit; }
		else if (Lower == TEXT("unlit"))           { OutMode = VMI_Unlit; }
		else if (Lower == TEXT("wireframe"))       { OutMode = VMI_Wireframe; }
		else if (Lower == TEXT("detaillighting"))  { OutMode = VMI_Lit_DetailLighting; }
		else if (Lower == TEXT("lightingonly"))    { OutMode = VMI_LightingOnly; }
		else if (Lower == TEXT("lightcomplexity")) { OutMode = VMI_LightComplexity; }
		else if (Lower == TEXT("shadercomplexity")){ OutMode = VMI_ShaderComplexity; }
		else if (Lower == TEXT("pathtracing"))     { OutMode = VMI_PathTracing; }
		else { return false; }
		return true;
	}

	/** Reverse map EViewModeIndex to a string for result payloads. */
	FString ViewModeToString(EViewModeIndex Mode)
	{
		switch (Mode)
		{
		case VMI_Lit:                return TEXT("lit");
		case VMI_Unlit:              return TEXT("unlit");
		case VMI_Wireframe:          return TEXT("wireframe");
		case VMI_Lit_DetailLighting: return TEXT("detaillighting");
		case VMI_LightingOnly:       return TEXT("lightingonly");
		case VMI_LightComplexity:    return TEXT("lightcomplexity");
		case VMI_ShaderComplexity:   return TEXT("shadercomplexity");
		case VMI_PathTracing:        return TEXT("pathtracing");
		default:                     return TEXT("unknown");
		}
	}

	// NOTE: viewport pane-layout switching is editor-internal in UE5.7
	// (SLevelViewport::OnSetViewportConfiguration + the LevelViewportConfigurationNames
	// constants were deprecated in 5.1 and moved into FViewportTabContent), so there is no
	// stable public entry point. set_layout is therefore reported as unsupported below.
}
#endif // WITH_EDITOR

FMCPToolInfo FMCPTool_Viewport::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("viewport");
	Info.Description = TEXT(
		"Control the Unreal Editor level viewport (camera, view mode, exposure, layout).\n\n"
		"Operations (set 'operation'):\n"
		"- 'set_camera': Set camera 'location' {x,y,z} and/or 'rotation' {pitch,yaw,roll}\n"
		"- 'set_view_mode': Set rendering mode 'view_mode' (lit, unlit, wireframe, detaillighting, lightingonly, shadercomplexity, pathtracing)\n"
		"- 'set_fov': Set perspective horizontal field of view to 'fov' degrees (typically 60-120)\n"
		"- 'set_exposure': Set 'fixed' exposure (bool) and 'ev100' value, or auto game settings\n"
		"- 'set_layout': (unsupported on UE5.7 - viewport pane layout is editor-internal)\n"
		"- 'toggle_game_view': Enable/disable Game View via 'enable' (hides editor-only visualizations)\n\n"
		"Operates on the active level editor viewport. Requires an editor build."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: set_camera, set_view_mode, set_fov, set_exposure, set_layout, toggle_game_view"), true),
		FMCPToolParameter(TEXT("location"), TEXT("object"), TEXT("For set_camera: world location { x, y, z }"), false),
		FMCPToolParameter(TEXT("rotation"), TEXT("object"), TEXT("For set_camera: world rotation { pitch, yaw, roll }"), false),
		FMCPToolParameter(TEXT("view_mode"), TEXT("string"), TEXT("For set_view_mode: lit, unlit, wireframe, detaillighting, lightingonly, shadercomplexity, pathtracing"), false),
		FMCPToolParameter(TEXT("fov"), TEXT("number"), TEXT("For set_fov: horizontal field of view in degrees"), false),
		FMCPToolParameter(TEXT("fixed"), TEXT("boolean"), TEXT("For set_exposure: true for fixed exposure, false for auto eye adaptation"), false),
		FMCPToolParameter(TEXT("ev100"), TEXT("number"), TEXT("For set_exposure: fixed EV100 exposure value (used when fixed=true)"), false),
		FMCPToolParameter(TEXT("layout"), TEXT("string"), TEXT("For set_layout: layout name (OnePane, TwoPanesHoriz, FourPanes2x2, etc.)"), false),
		FMCPToolParameter(TEXT("enable"), TEXT("boolean"), TEXT("For toggle_game_view: true to enable Game View, false to disable"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_Viewport::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("set_camera"))       { return OpSetCamera(Params); }
	if (Operation == TEXT("set_view_mode"))    { return OpSetViewMode(Params); }
	if (Operation == TEXT("set_fov"))          { return OpSetFov(Params); }
	if (Operation == TEXT("set_exposure"))     { return OpSetExposure(Params); }
	if (Operation == TEXT("set_layout"))       { return OpSetLayout(Params); }
	if (Operation == TEXT("toggle_game_view")) { return OpToggleGameView(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: set_camera, set_view_mode, set_fov, set_exposure, set_layout, toggle_game_view"), *Operation));
}

FMCPToolResult FMCPTool_Viewport::OpSetCamera(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FLevelEditorViewportClient* Client = GetActiveViewportClient();
	if (!Client)
	{
		return FMCPToolResult::Error(TEXT("set_camera: no active level editor viewport"));
	}

	const bool bHasLocation = HasVectorParam(Params, TEXT("location"));
	const TSharedPtr<FJsonObject>* RotationObj = nullptr;
	const bool bHasRotation = Params->TryGetObjectField(TEXT("rotation"), RotationObj) && RotationObj && (*RotationObj).IsValid();
	if (!bHasLocation && !bHasRotation)
	{
		return FMCPToolResult::Error(TEXT("set_camera: provide 'location' {x,y,z} and/or 'rotation' {pitch,yaw,roll}"));
	}

	if (bHasLocation)
	{
		const FVector NewLocation = ExtractVectorParam(Params, TEXT("location"), Client->GetViewLocation());
		Client->SetViewLocation(NewLocation);
	}
	if (bHasRotation)
	{
		const FRotator NewRotation = ExtractRotatorParam(Params, TEXT("rotation"), Client->GetViewRotation());
		Client->SetViewRotation(NewRotation);
	}

	ForceViewportRedraw();

	const FVector Location = Client->GetViewLocation();
	const FRotator Rotation = Client->GetViewRotation();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("location"), UnrealClaudeJsonUtils::VectorToJson(Location));
	Data->SetObjectField(TEXT("rotation"), UnrealClaudeJsonUtils::RotatorToJson(Rotation));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Camera set to location (%.1f, %.1f, %.1f), rotation (P=%.1f, Y=%.1f, R=%.1f)"),
			Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll),
		Data);
#else
	return FMCPToolResult::Error(TEXT("set_camera requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Viewport::OpSetViewMode(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString ViewModeStr;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("view_mode"), ViewModeStr, Err))
	{
		return Err.GetValue();
	}

	EViewModeIndex NewMode;
	if (!ViewModeFromString(ViewModeStr, NewMode))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("set_view_mode: unknown view_mode '%s'. Valid: lit, unlit, wireframe, detaillighting, lightingonly, lightcomplexity, shadercomplexity, pathtracing"),
			*ViewModeStr));
	}

	FLevelEditorViewportClient* Client = GetActiveViewportClient();
	if (!Client)
	{
		return FMCPToolResult::Error(TEXT("set_view_mode: no active level editor viewport"));
	}

	Client->SetViewMode(NewMode);
	ForceViewportRedraw();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("view_mode"), ViewModeToString(Client->GetViewMode()));
	return FMCPToolResult::Success(FString::Printf(TEXT("View mode set to '%s'"), *ViewModeToString(NewMode)), Data);
#else
	return FMCPToolResult::Error(TEXT("set_view_mode requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Viewport::OpSetFov(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	double FovValue;
	if (!Params->TryGetNumberField(TEXT("fov"), FovValue))
	{
		return FMCPToolResult::Error(TEXT("set_fov: 'fov' (number, degrees) is required"));
	}

	FLevelEditorViewportClient* Client = GetActiveViewportClient();
	if (!Client)
	{
		return FMCPToolResult::Error(TEXT("set_fov: no active level editor viewport"));
	}

	const float Fov = FMath::Clamp(static_cast<float>(FovValue), 5.0f, 170.0f);
	Client->ViewFOV = Fov;
	Client->FOVAngle = Fov;
	ForceViewportRedraw();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("fov"), Client->ViewFOV);
	return FMCPToolResult::Success(FString::Printf(TEXT("Field of view set to %.1f degrees"), Client->ViewFOV), Data);
#else
	return FMCPToolResult::Error(TEXT("set_fov requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Viewport::OpSetExposure(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FLevelEditorViewportClient* Client = GetActiveViewportClient();
	if (!Client)
	{
		return FMCPToolResult::Error(TEXT("set_exposure: no active level editor viewport"));
	}

	const bool bFixed = ExtractOptionalBool(Params, TEXT("fixed"), Client->ExposureSettings.bFixed);
	const float EV100 = ExtractOptionalNumber<float>(Params, TEXT("ev100"), Client->ExposureSettings.FixedEV100);

	Client->ExposureSettings.bFixed = bFixed;
	Client->ExposureSettings.FixedEV100 = EV100;
	ForceViewportRedraw();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("fixed"), Client->ExposureSettings.bFixed);
	Data->SetNumberField(TEXT("ev100"), Client->ExposureSettings.FixedEV100);
	return FMCPToolResult::Success(
		bFixed
			? FString::Printf(TEXT("Exposure fixed at EV100 = %.2f"), EV100)
			: TEXT("Exposure set to auto (game settings / eye adaptation)"),
		Data);
#else
	return FMCPToolResult::Error(TEXT("set_exposure requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Viewport::OpSetLayout(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString LayoutStr;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("layout"), LayoutStr, Err))
	{
		return Err.GetValue();
	}

	(void)LayoutStr;
	return FMCPToolResult::Error(TEXT(
		"set_layout is not supported on UE5.7 - viewport pane layout is handled internally by "
		"the editor (FViewportTabContent) with no stable public API. Use set_camera, set_view_mode, "
		"set_fov, set_exposure, or toggle_game_view instead."));
#else
	return FMCPToolResult::Error(TEXT("set_layout requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_Viewport::OpToggleGameView(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	bool bEnable;
	if (!Params->TryGetBoolField(TEXT("enable"), bEnable))
	{
		return FMCPToolResult::Error(TEXT("toggle_game_view: 'enable' (boolean) is required"));
	}

	FLevelEditorViewportClient* Client = GetActiveViewportClient();
	if (!Client)
	{
		return FMCPToolResult::Error(TEXT("toggle_game_view: no active level editor viewport"));
	}

	if (Client->IsInGameView() != bEnable)
	{
		Client->SetGameView(bEnable);
		ForceViewportRedraw();
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("game_view"), Client->IsInGameView());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Game View %s"), Client->IsInGameView() ? TEXT("enabled") : TEXT("disabled")),
		Data);
#else
	return FMCPToolResult::Error(TEXT("toggle_game_view requires an editor build"));
#endif
}
