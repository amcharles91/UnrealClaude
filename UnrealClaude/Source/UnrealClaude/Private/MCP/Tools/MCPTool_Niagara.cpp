// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Niagara.h"
#include "UnrealClaudeModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"

// Niagara runtime
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraComponentRendererProperties.h"

#if WITH_EDITOR
// Niagara editor authoring
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorModule.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "EdGraphSchema_Niagara.h"
#include "EdGraph/EdGraphPin.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#endif // WITH_EDITOR

// =====================================================================
// File-local helpers (no header members — keeps ABI stable for Live Coding)
// =====================================================================
namespace
{
	/** Load a UNiagaraSystem from a content path, or nullptr. */
	UNiagaraSystem* LoadNiagaraSystem(const FString& SystemPath)
	{
		if (SystemPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(SystemPath);
		return Cast<UNiagaraSystem>(LoadedObject);
	}

	/** Load a UNiagaraEmitter from a content path, or nullptr. */
	UNiagaraEmitter* LoadNiagaraEmitter(const FString& EmitterPath)
	{
		if (EmitterPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(EmitterPath);
		return Cast<UNiagaraEmitter>(LoadedObject);
	}

	/** Find an emitter handle by display name or unique instance name (case-insensitive). */
	FNiagaraEmitterHandle* FindEmitterHandle(UNiagaraSystem* System, const FString& EmitterName)
	{
		if (!System)
		{
			return nullptr;
		}
		for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase) ||
				Handle.GetUniqueInstanceName().Equals(EmitterName, ESearchCase::IgnoreCase))
			{
				return &Handle;
			}
		}
		return nullptr;
	}

	/** Human-readable name for a Niagara renderer properties object. */
	FString RendererTypeName(UNiagaraRendererProperties* Renderer)
	{
		if (!Renderer)                                              { return TEXT("Unknown"); }
		if (Cast<UNiagaraSpriteRendererProperties>(Renderer))       { return TEXT("Sprite"); }
		if (Cast<UNiagaraMeshRendererProperties>(Renderer))         { return TEXT("Mesh"); }
		if (Cast<UNiagaraRibbonRendererProperties>(Renderer))       { return TEXT("Ribbon"); }
		if (Cast<UNiagaraLightRendererProperties>(Renderer))        { return TEXT("Light"); }
		if (Cast<UNiagaraComponentRendererProperties>(Renderer))    { return TEXT("Component"); }
		return Renderer->GetClass()->GetName();
	}

	/** Friendly type-name string for a Niagara variable's type. */
	FString NiagaraTypeToString(const FNiagaraTypeDefinition& TypeDef)
	{
		if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())   { return TEXT("Float"); }
		if (TypeDef == FNiagaraTypeDefinition::GetIntDef())     { return TEXT("Int"); }
		if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())    { return TEXT("Bool"); }
		if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())    { return TEXT("Vector2"); }
		if (TypeDef == FNiagaraTypeDefinition::GetVec3Def())    { return TEXT("Vector"); }
		if (TypeDef == FNiagaraTypeDefinition::GetVec4Def())    { return TEXT("Vector4"); }
		if (TypeDef == FNiagaraTypeDefinition::GetColorDef())   { return TEXT("Color"); }
		if (TypeDef == FNiagaraTypeDefinition::GetQuatDef())    { return TEXT("Quat"); }
		if (TypeDef.IsEnum())                                   { return TEXT("Enum"); }
		return TypeDef.GetName();
	}

	/**
	 * Parse "(x,y,z[,w])" or "x,y,z[,w]" into floats, requiring exactly ExpectedCount
	 * numeric components. Returns false (and leaves Out empty) if the component count is
	 * wrong or any component is not a number, so callers can report an error instead of
	 * silently zero-padding malformed input.
	 */
	bool ParseFloatComponents(const FString& Value, int32 ExpectedCount, TArray<float>& Out)
	{
		Out.Reset();
		FString Trimmed = Value.TrimStartAndEnd();
		Trimmed.RemoveFromStart(TEXT("("));
		Trimmed.RemoveFromEnd(TEXT(")"));
		TArray<FString> Parts;
		Trimmed.ParseIntoArray(Parts, TEXT(","));

		if (Parts.Num() != ExpectedCount)
		{
			return false;
		}

		for (const FString& P : Parts)
		{
			const FString Component = P.TrimStartAndEnd();
			if (Component.IsEmpty() || !Component.IsNumeric())
			{
				return false;
			}
			Out.Add(FCString::Atof(*Component));
		}
		return true;
	}
}

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

// =====================================================================
// list
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpList(const TSharedRef<FJsonObject>& Params)
{
	const FString Filter = ExtractOptionalString(Params, TEXT("filter"));

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter ARFilter;
	ARFilter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
	ARFilter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(ARFilter, Assets);

	TArray<TSharedPtr<FJsonValue>> SystemArray;
	for (const FAssetData& Asset : Assets)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !AssetName.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), AssetName);
		Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		SystemArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("count"), SystemArray.Num());
	Data->SetArrayField(TEXT("systems"), SystemArray);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d Niagara system(s)"), SystemArray.Num()), Data);
}

// =====================================================================
// create_system
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpCreateSystem(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString SystemName, DestinationPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_name"), SystemName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("destination_path"), DestinationPath, Err)) { return Err.GetValue(); }
	const FString TemplateAssetPath = ExtractOptionalString(Params, TEXT("template_asset_path"));

	// Normalize the destination folder.
	FString CleanPath = DestinationPath;
	if (!CleanPath.StartsWith(TEXT("/Game")))
	{
		CleanPath = TEXT("/Game/") + CleanPath;
	}
	if (CleanPath.EndsWith(TEXT("/")))
	{
		CleanPath = CleanPath.LeftChop(1);
	}
	const FString FullAssetPath = CleanPath / SystemName;

	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath) && UEditorAssetLibrary::LoadAsset(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system already exists at: %s"), *FullAssetPath));
	}

	// Template-based creation: duplicate an existing system asset.
	if (!TemplateAssetPath.IsEmpty())
	{
		UObject* TemplateAsset = UEditorAssetLibrary::LoadAsset(TemplateAssetPath);
		UNiagaraSystem* TemplateSystem = Cast<UNiagaraSystem>(TemplateAsset);
		if (!TemplateSystem)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Template asset not found or not a NiagaraSystem: %s"), *TemplateAssetPath));
		}

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		UObject* Duplicated = AssetToolsModule.Get().DuplicateAsset(SystemName, CleanPath, TemplateSystem);
		UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(Duplicated);
		if (!NewSystem)
		{
			return FMCPToolResult::Error(TEXT("Failed to duplicate Niagara system template"));
		}
		NewSystem->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(FullAssetPath, /*bOnlyIfIsDirty=*/true);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("path"), FullAssetPath);
		Data->SetStringField(TEXT("from_template"), TemplateAssetPath);
		return FMCPToolResult::Success(FString::Printf(TEXT("Created Niagara system '%s' from template"), *FullAssetPath), Data);
	}

	// Empty-system creation via the factory.
	UPackage* Package = CreatePackage(*FullAssetPath);
	if (!Package)
	{
		return FMCPToolResult::Error(TEXT("Failed to create package"));
	}

	UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
	UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(Factory->FactoryCreateNew(
		UNiagaraSystem::StaticClass(), Package, *SystemName, RF_Public | RF_Standalone, nullptr, GWarn));
	if (!NewSystem)
	{
		return FMCPToolResult::Error(TEXT("Failed to create Niagara system"));
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewSystem);

	// Save via UEditorAssetLibrary for consistency with the template/duplicate branch, and
	// check the result rather than reporting success unconditionally.
	if (!UEditorAssetLibrary::SaveAsset(FullAssetPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Created Niagara system in memory but failed to save asset: %s"), *FullAssetPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), FullAssetPath);
	Data->SetStringField(TEXT("name"), SystemName);
	return FMCPToolResult::Success(FString::Printf(TEXT("Created empty Niagara system '%s'"), *FullAssetPath), Data);
#else
	return FMCPToolResult::Error(TEXT("create_system requires an editor build"));
#endif
}

// =====================================================================
// get_info
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	FString SystemPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, Err)) { return Err.GetValue(); }

	UNiagaraSystem* System = LoadNiagaraSystem(SystemPath);
	if (!System)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system not found: %s"), *SystemPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), System->GetName());
	Data->SetStringField(TEXT("path"), SystemPath);

	// Emitters (+ renderers per emitter).
	TArray<TSharedPtr<FJsonValue>> EmitterArray;
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
		EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
		EmitterObj->SetStringField(TEXT("unique_name"), Handle.GetUniqueInstanceName());
		EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (EmitterData)
		{
			EmitterObj->SetStringField(TEXT("sim_target"),
				EmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPUComputeSim") : TEXT("CPUSim"));

			TArray<TSharedPtr<FJsonValue>> RendererArray;
			const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
			for (int32 i = 0; i < Renderers.Num(); ++i)
			{
				if (UNiagaraRendererProperties* Renderer = Renderers[i])
				{
					TSharedPtr<FJsonObject> RObj = MakeShared<FJsonObject>();
					RObj->SetNumberField(TEXT("index"), i);
					RObj->SetStringField(TEXT("type"), RendererTypeName(Renderer));
					RObj->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
					RendererArray.Add(MakeShared<FJsonValueObject>(RObj));
				}
			}
			EmitterObj->SetArrayField(TEXT("renderers"), RendererArray);
		}
		EmitterArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
	}
	Data->SetNumberField(TEXT("emitter_count"), EmitterArray.Num());
	Data->SetArrayField(TEXT("emitters"), EmitterArray);

	// User / exposed parameters.
	TArray<TSharedPtr<FJsonValue>> ParamArray;
	const FNiagaraUserRedirectionParameterStore& UserParamStore = System->GetExposedParameters();
	TArray<FNiagaraVariable> UserParams;
	UserParamStore.GetParameters(UserParams);
	for (const FNiagaraVariable& Param : UserParams)
	{
		TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
		PObj->SetStringField(TEXT("name"), Param.GetName().ToString());
		PObj->SetStringField(TEXT("type"), NiagaraTypeToString(Param.GetType()));
		ParamArray.Add(MakeShared<FJsonValueObject>(PObj));
	}
	Data->SetNumberField(TEXT("user_parameter_count"), ParamArray.Num());
	Data->SetArrayField(TEXT("user_parameters"), ParamArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Niagara system '%s': %d emitter(s), %d user parameter(s)"),
			*System->GetName(), EmitterArray.Num(), ParamArray.Num()), Data);
}

// =====================================================================
// add_emitter
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpAddEmitter(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString SystemPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, Err)) { return Err.GetValue(); }
	const FString EmitterAssetPath = ExtractOptionalString(Params, TEXT("emitter_asset_path"));
	const FString DesiredName = ExtractOptionalString(Params, TEXT("emitter_name"));

	UNiagaraSystem* System = LoadNiagaraSystem(SystemPath);
	if (!System)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system not found: %s"), *SystemPath));
	}

	System->Modify();

	UNiagaraEmitter* SourceEmitter = nullptr;
	FGuid EmitterVersion;

	if (EmitterAssetPath.IsEmpty() || EmitterAssetPath.Equals(TEXT("minimal"), ESearchCase::IgnoreCase))
	{
		// Prefer the configured default empty emitter, like the editor UI does.
		const UNiagaraEditorSettings* EditorSettings = GetDefault<UNiagaraEditorSettings>();
		if (EditorSettings && !EditorSettings->DefaultEmptyEmitter.IsNull())
		{
			SourceEmitter = Cast<UNiagaraEmitter>(EditorSettings->DefaultEmptyEmitter.TryLoad());
		}
		if (!SourceEmitter)
		{
			SourceEmitter = NewObject<UNiagaraEmitter>(GetTransientPackage());
			UNiagaraEmitterFactoryNew::InitializeEmitter(SourceEmitter, /*bAddDefaultModules=*/false);
		}
		EmitterVersion = SourceEmitter->GetExposedVersion().VersionGuid;
	}
	else
	{
		SourceEmitter = LoadNiagaraEmitter(EmitterAssetPath);
		if (!SourceEmitter)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load emitter asset: %s"), *EmitterAssetPath));
		}
		EmitterVersion = SourceEmitter->GetExposedVersion().VersionGuid;
	}

	// bCreateCopy=true so the system owns a proper copy of the source emitter (which may
	// live in the transient package); matches the editor's "Add Emitter" behavior and
	// avoids GC/orphan risk on the source.
	const FGuid NewHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *SourceEmitter, EmitterVersion, /*bCreateCopy=*/true);
	if (!NewHandleId.IsValid())
	{
		return FMCPToolResult::Error(TEXT("Failed to add emitter to system"));
	}

	// Resolve / apply the resulting handle name.
	FString ResultName;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetId() == NewHandleId)
		{
			if (!DesiredName.IsEmpty())
			{
				Handle.SetName(FName(*DesiredName), *System);
			}
			ResultName = Handle.GetName().ToString();
			break;
		}
	}

	if (UNiagaraSystemEditorData* SystemEditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData()))
	{
		SystemEditorData->SynchronizeOverviewGraphWithSystem(*System);
	}

	// Wait for the recompile to finish before saving; saving mid-compile can persist a
	// stale emitter count and crash (matches remove_emitter / set_emitter_param / add_module).
	System->RequestCompile(false);
	System->WaitForCompilationComplete();

	if (TSharedPtr<FNiagaraSystemViewModel> SystemViewModel = FNiagaraEditorModule::Get().GetExistingViewModelForSystem(System))
	{
		SystemViewModel->RefreshAll();
	}

	System->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SystemPath, /*bOnlyIfIsDirty=*/true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("system_path"), SystemPath);
	Data->SetStringField(TEXT("emitter_name"), ResultName);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added emitter '%s' to %s"), *ResultName, *SystemPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_emitter requires an editor build"));
#endif
}

// =====================================================================
// remove_emitter
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpRemoveEmitter(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString SystemPath, EmitterName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("emitter_name"), EmitterName, Err)) { return Err.GetValue(); }

	UNiagaraSystem* System = LoadNiagaraSystem(SystemPath);
	if (!System)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system not found: %s"), *SystemPath));
	}

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Emitter not found: %s"), *EmitterName));
	}

	System->Modify();

	TSet<FGuid> IdsToRemove;
	IdsToRemove.Add(Handle->GetId());
	System->RemoveEmitterHandlesById(IdsToRemove);

	// Recompile after removal to sync internal state (prevents stale-count crashes on save).
	System->RequestCompile(false);
	System->WaitForCompilationComplete();

	if (UNiagaraSystemEditorData* SystemEditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData()))
	{
		SystemEditorData->SynchronizeOverviewGraphWithSystem(*System);
	}

	System->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SystemPath, /*bOnlyIfIsDirty=*/true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("system_path"), SystemPath);
	Data->SetStringField(TEXT("removed_emitter"), EmitterName);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed emitter '%s' from %s"), *EmitterName, *SystemPath), Data);
#else
	return FMCPToolResult::Error(TEXT("remove_emitter requires an editor build"));
#endif
}

// =====================================================================
// set_system_param  (user / exposed parameters)
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpSetSystemParam(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString SystemPath, ParameterName, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("parameter_name"), ParameterName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err)) { return Err.GetValue(); }

	UNiagaraSystem* System = LoadNiagaraSystem(SystemPath);
	if (!System)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system not found: %s"), *SystemPath));
	}

	FNiagaraUserRedirectionParameterStore& UserParamStore = System->GetExposedParameters();
	TArray<FNiagaraVariable> UserParams;
	UserParamStore.GetParameters(UserParams);

	// Resolve the target parameter: exact (case-insensitive) match wins outright. Only if no
	// exact match exists do we fall back to EndsWith; if EndsWith hits more than one parameter
	// the request is ambiguous and we error rather than guessing.
	int32 MatchIndex = INDEX_NONE;
	{
		TArray<int32> ExactMatches;
		TArray<int32> SuffixMatches;
		for (int32 i = 0; i < UserParams.Num(); ++i)
		{
			const FString ParamName = UserParams[i].GetName().ToString();
			if (ParamName.Equals(ParameterName, ESearchCase::IgnoreCase))
			{
				ExactMatches.Add(i);
			}
			else if (ParamName.EndsWith(ParameterName, ESearchCase::IgnoreCase))
			{
				SuffixMatches.Add(i);
			}
		}

		if (ExactMatches.Num() == 1)
		{
			MatchIndex = ExactMatches[0];
		}
		else if (ExactMatches.Num() > 1)
		{
			// Multiple parameters with the identical name should not happen, but guard anyway.
			MatchIndex = ExactMatches[0];
		}
		else if (SuffixMatches.Num() == 1)
		{
			MatchIndex = SuffixMatches[0];
		}
		else if (SuffixMatches.Num() > 1)
		{
			FString MatchList;
			for (int32 Idx : SuffixMatches)
			{
				if (!MatchList.IsEmpty()) { MatchList += TEXT(", "); }
				MatchList += UserParams[Idx].GetName().ToString();
			}
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Ambiguous parameter '%s' on %s: matches multiple user parameters [%s]. ")
				TEXT("Specify the full parameter name."),
				*ParameterName, *SystemPath, *MatchList));
		}
	}

	if (MatchIndex != INDEX_NONE)
	{
		FNiagaraVariable& Param = UserParams[MatchIndex];
		const FString ParamName = Param.GetName().ToString();

		const FNiagaraTypeDefinition& TypeDef = Param.GetType();
		if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
		{
			UserParamStore.SetParameterValue(FCString::Atof(*Value), Param);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
		{
			UserParamStore.SetParameterValue((int32)FCString::Atoi(*Value), Param);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
		{
			const bool bVal = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("1"));
			UserParamStore.SetParameterValue(bVal, Param);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
		{
			FLinearColor Color;
			Color.InitFromString(Value);
			UserParamStore.SetParameterValue(Color, Param);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())
		{
			TArray<float> C;
			if (!ParseFloatComponents(Value, 2, C))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Invalid Vector2 value '%s' for parameter '%s'. Expected 2 numeric components, e.g. '(1.0,2.0)'."),
					*Value, *ParamName));
			}
			FVector2f Vec(C[0], C[1]);
			UserParamStore.SetParameterValue(Vec, Param);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec3Def())
		{
			TArray<float> C;
			if (!ParseFloatComponents(Value, 3, C))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Invalid Vector value '%s' for parameter '%s'. Expected 3 numeric components, e.g. '(1.0,2.0,3.0)'."),
					*Value, *ParamName));
			}
			FVector3f Vec(C[0], C[1], C[2]);
			UserParamStore.SetParameterValue(Vec, Param);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec4Def())
		{
			TArray<float> C;
			if (!ParseFloatComponents(Value, 4, C))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Invalid Vector4 value '%s' for parameter '%s'. Expected 4 numeric components, e.g. '(1.0,2.0,3.0,4.0)'."),
					*Value, *ParamName));
			}
			FVector4f Vec(C[0], C[1], C[2], C[3]);
			UserParamStore.SetParameterValue(Vec, Param);
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Unsupported user-parameter type '%s' for '%s' (supported: float, int, bool, color, vector2/3/4)"),
				*NiagaraTypeToString(TypeDef), *ParameterName));
		}

		System->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(SystemPath, /*bOnlyIfIsDirty=*/true);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("system_path"), SystemPath);
		Data->SetStringField(TEXT("parameter"), ParamName);
		Data->SetStringField(TEXT("value"), Value);
		return FMCPToolResult::Success(FString::Printf(TEXT("Set user parameter '%s' = %s"), *ParamName, *Value), Data);
	}

	return FMCPToolResult::Error(FString::Printf(
		TEXT("User parameter '%s' not found on %s (only system/user exposed parameters are settable here)"),
		*ParameterName, *SystemPath));
#else
	return FMCPToolResult::Error(TEXT("set_system_param requires an editor build"));
#endif
}

// =====================================================================
// set_emitter_param  (module input pin default)
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpSetEmitterParam(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString SystemPath, EmitterName, ModuleName, InputName, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("emitter_name"), EmitterName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("module_name"), ModuleName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("input_name"), InputName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err)) { return Err.GetValue(); }

	UNiagaraSystem* System = LoadNiagaraSystem(SystemPath);
	if (!System)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system not found: %s"), *SystemPath));
	}

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Emitter not found: %s"), *EmitterName));
	}

	FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
	UNiagaraScriptSource* ScriptSource = EmitterData ? Cast<UNiagaraScriptSource>(EmitterData->GraphSource) : nullptr;
	if (!ScriptSource || !ScriptSource->NodeGraph)
	{
		return FMCPToolResult::Error(TEXT("Emitter has no editable script graph"));
	}
	UNiagaraGraph* Graph = ScriptSource->NodeGraph;

	// Locate the module's function-call node by name.
	TArray<UNiagaraNodeFunctionCall*> AllFunctionCalls;
	Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(AllFunctionCalls);

	UNiagaraNodeFunctionCall* TargetModule = nullptr;
	for (UNiagaraNodeFunctionCall* FunctionCall : AllFunctionCalls)
	{
		if (!FunctionCall) { continue; }
		const FString FuncName = FunctionCall->GetFunctionName();
		if (FuncName.Equals(ModuleName, ESearchCase::IgnoreCase) || FuncName.Contains(ModuleName, ESearchCase::IgnoreCase))
		{
			TargetModule = FunctionCall;
			break;
		}
	}
	if (!TargetModule)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Module not found: %s (in emitter %s)"), *ModuleName, *EmitterName));
	}

	// Resolve the input pin. Module inputs live in the "Module." namespace; accept several
	// well-known prefix variants and a final case-insensitive substring fallback.
	const TArray<FString> Candidates = {
		InputName,
		FString::Printf(TEXT("Module.%s"), *InputName),
		FString::Printf(TEXT("Output.%s"), *InputName),
		FString::Printf(TEXT("Particles.%s"), *InputName),
	};

	UEdGraphPin* TargetPin = nullptr;
	for (const FString& Candidate : Candidates)
	{
		for (UEdGraphPin* Pin : TargetModule->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) { continue; }
			if (Pin->PinName.ToString().Equals(Candidate, ESearchCase::IgnoreCase))
			{
				TargetPin = Pin;
				break;
			}
		}
		if (TargetPin) { break; }
	}
	if (!TargetPin)
	{
		for (UEdGraphPin* Pin : TargetModule->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) { continue; }
			if (Pin->PinName.ToString().Contains(InputName, ESearchCase::IgnoreCase))
			{
				TargetPin = Pin;
				break;
			}
		}
	}
	if (!TargetPin)
	{
		FString Available;
		for (UEdGraphPin* Pin : TargetModule->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input)
			{
				if (!Available.IsEmpty()) { Available += TEXT(", "); }
				Available += Pin->PinName.ToString();
			}
		}
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Input '%s' not found on module '%s'. Available input pins: [%s]. ")
			TEXT("(Note: inputs stored as rapid-iteration parameters rather than pins are not settable via this op.)"),
			*InputName, *ModuleName, *Available));
	}

	Graph->Modify();
	TargetModule->Modify();

	const UEdGraphSchema_Niagara* NiagaraSchema = Cast<UEdGraphSchema_Niagara>(Graph->GetSchema());
	if (NiagaraSchema)
	{
		NiagaraSchema->TrySetDefaultValue(*TargetPin, Value, true);
	}
	else
	{
		TargetPin->DefaultValue = Value;
	}

	System->MarkPackageDirty();
	System->RequestCompile(false);
	System->WaitForCompilationComplete();
	UEditorAssetLibrary::SaveAsset(SystemPath, /*bOnlyIfIsDirty=*/true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("system_path"), SystemPath);
	Data->SetStringField(TEXT("emitter_name"), EmitterName);
	Data->SetStringField(TEXT("module_name"), ModuleName);
	Data->SetStringField(TEXT("input_name"), InputName);
	Data->SetStringField(TEXT("value"), Value);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set %s.%s = %s on emitter %s"), *ModuleName, *InputName, *Value, *EmitterName), Data);
#else
	return FMCPToolResult::Error(TEXT("set_emitter_param requires an editor build"));
#endif
}

// =====================================================================
// add_module
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpAddModule(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString SystemPath, EmitterName, ModuleScriptPath, ModuleType;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("emitter_name"), EmitterName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("module_script_path"), ModuleScriptPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("module_type"), ModuleType, Err)) { return Err.GetValue(); }

	UNiagaraSystem* System = LoadNiagaraSystem(SystemPath);
	if (!System)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system not found: %s"), *SystemPath));
	}

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Emitter not found: %s"), *EmitterName));
	}

	UNiagaraScript* ModuleScript = Cast<UNiagaraScript>(UEditorAssetLibrary::LoadAsset(ModuleScriptPath));
	if (!ModuleScript)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Module script not found (or not a NiagaraScript): %s"), *ModuleScriptPath));
	}

	FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
	UNiagaraScriptSource* ScriptSource = EmitterData ? Cast<UNiagaraScriptSource>(EmitterData->GraphSource) : nullptr;
	if (!ScriptSource || !ScriptSource->NodeGraph)
	{
		return FMCPToolResult::Error(TEXT("Emitter has no editable script graph"));
	}
	UNiagaraGraph* Graph = ScriptSource->NodeGraph;

	// Map the stage string to a script usage + the emitter's script for that stage.
	ENiagaraScriptUsage TargetUsage;
	UNiagaraScript* TargetScript = nullptr;
	if (ModuleType.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase))
	{
		TargetUsage = ENiagaraScriptUsage::ParticleSpawnScript;
		TargetScript = EmitterData->SpawnScriptProps.Script;
	}
	else if (ModuleType.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase))
	{
		TargetUsage = ENiagaraScriptUsage::ParticleUpdateScript;
		TargetScript = EmitterData->UpdateScriptProps.Script;
	}
	else if (ModuleType.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))
	{
		TargetUsage = ENiagaraScriptUsage::EmitterSpawnScript;
		TargetScript = EmitterData->EmitterSpawnScriptProps.Script;
	}
	else if (ModuleType.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase))
	{
		TargetUsage = ENiagaraScriptUsage::EmitterUpdateScript;
		TargetScript = EmitterData->EmitterUpdateScriptProps.Script;
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unrecognized stage '%s'. Use exactly one of: ParticleSpawn, ParticleUpdate, EmitterSpawn, EmitterUpdate"),
			*ModuleType));
	}

	if (!TargetScript)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("No target script for stage '%s'"), *ModuleType));
	}

	// Find the output node for that usage.
	UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(TargetUsage, TargetScript->GetUsageId());
	if (!OutputNode)
	{
		TArray<UNiagaraNodeOutput*> AllOutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(AllOutputNodes);
		for (UNiagaraNodeOutput* TestNode : AllOutputNodes)
		{
			if (TestNode && TestNode->GetUsage() == TargetUsage)
			{
				OutputNode = TestNode;
				break;
			}
		}
	}
	if (!OutputNode)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Could not find output node for stage '%s'"), *ModuleType));
	}

	Graph->Modify();

	UNiagaraNodeFunctionCall* NewModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
		ModuleScript, *OutputNode, INDEX_NONE, FString(), FGuid());
	if (!NewModuleNode)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add module '%s' to stage '%s'"), *ModuleScriptPath, *ModuleType));
	}

	System->MarkPackageDirty();
	System->RequestCompile(false);
	System->WaitForCompilationComplete();
	UEditorAssetLibrary::SaveAsset(SystemPath, /*bOnlyIfIsDirty=*/true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("system_path"), SystemPath);
	Data->SetStringField(TEXT("emitter_name"), EmitterName);
	Data->SetStringField(TEXT("module"), NewModuleNode->GetFunctionName());
	Data->SetStringField(TEXT("stage"), ModuleType);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added module '%s' to %s/%s"), *ModuleScriptPath, *EmitterName, *ModuleType), Data);
#else
	return FMCPToolResult::Error(TEXT("add_module requires an editor build"));
#endif
}

// =====================================================================
// add_renderer
// =====================================================================
FMCPToolResult FMCPTool_Niagara::OpAddRenderer(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString SystemPath, EmitterName, RendererType;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("system_path"), SystemPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("emitter_name"), EmitterName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("renderer_type"), RendererType, Err)) { return Err.GetValue(); }

	UNiagaraSystem* System = LoadNiagaraSystem(SystemPath);
	if (!System)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Niagara system not found: %s"), *SystemPath));
	}

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Emitter not found: %s"), *EmitterName));
	}

	UClass* RendererClass = nullptr;
	if (RendererType.Equals(TEXT("Sprite"), ESearchCase::IgnoreCase))         { RendererClass = UNiagaraSpriteRendererProperties::StaticClass(); }
	else if (RendererType.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase))      { RendererClass = UNiagaraMeshRendererProperties::StaticClass(); }
	else if (RendererType.Equals(TEXT("Ribbon"), ESearchCase::IgnoreCase))    { RendererClass = UNiagaraRibbonRendererProperties::StaticClass(); }
	else if (RendererType.Equals(TEXT("Light"), ESearchCase::IgnoreCase))     { RendererClass = UNiagaraLightRendererProperties::StaticClass(); }
	else if (RendererType.Equals(TEXT("Component"), ESearchCase::IgnoreCase)) { RendererClass = UNiagaraComponentRendererProperties::StaticClass(); }
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown renderer type '%s'. Valid: Sprite, Mesh, Ribbon, Light, Component"), *RendererType));
	}

	FVersionedNiagaraEmitter VersionedEmitter = Handle->GetInstance();
	UNiagaraEmitter* Emitter = VersionedEmitter.Emitter.Get();
	if (!Emitter)
	{
		return FMCPToolResult::Error(TEXT("Failed to resolve emitter instance"));
	}

	UNiagaraRendererProperties* NewRenderer = NewObject<UNiagaraRendererProperties>(Emitter, RendererClass, NAME_None, RF_Transactional);
	if (!NewRenderer)
	{
		return FMCPToolResult::Error(TEXT("Failed to create renderer"));
	}

	Emitter->AddRenderer(NewRenderer, VersionedEmitter.Version);

	System->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SystemPath, /*bOnlyIfIsDirty=*/true);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("system_path"), SystemPath);
	Data->SetStringField(TEXT("emitter_name"), EmitterName);
	Data->SetStringField(TEXT("renderer_type"), RendererTypeName(NewRenderer));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added %s renderer to emitter %s"), *RendererTypeName(NewRenderer), *EmitterName), Data);
#else
	return FMCPToolResult::Error(TEXT("add_renderer requires an editor build"));
#endif
}
