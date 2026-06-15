// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_EngineSettings.h"
#include "UnrealClaudeModule.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Dom/JsonObject.h"

namespace
{
	/** Read a console variable's value as a string. Returns false if not found. */
	bool GetCVarString(const FString& Name, FString& OutValue)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!CVar)
		{
			return false;
		}
		OutValue = CVar->GetString();
		return true;
	}

	/** The scalability quality groups exposed for get/set, mapped to FQualityLevels fields. */
	void QualityLevelsToJson(const Scalability::FQualityLevels& Q, const TSharedPtr<FJsonObject>& Obj)
	{
		Obj->SetNumberField(TEXT("ViewDistance"), Q.ViewDistanceQuality);
		Obj->SetNumberField(TEXT("AntiAliasing"), Q.AntiAliasingQuality);
		Obj->SetNumberField(TEXT("Shadow"), Q.ShadowQuality);
		Obj->SetNumberField(TEXT("GlobalIllumination"), Q.GlobalIlluminationQuality);
		Obj->SetNumberField(TEXT("Reflection"), Q.ReflectionQuality);
		Obj->SetNumberField(TEXT("PostProcess"), Q.PostProcessQuality);
		Obj->SetNumberField(TEXT("Texture"), Q.TextureQuality);
		Obj->SetNumberField(TEXT("Effects"), Q.EffectsQuality);
		Obj->SetNumberField(TEXT("Foliage"), Q.FoliageQuality);
		Obj->SetNumberField(TEXT("Shading"), Q.ShadingQuality);
	}

	/** Apply a quality level to a single named group. Returns false if the group name is unknown. */
	bool SetQualityGroup(Scalability::FQualityLevels& Q, const FString& Group, int32 Level)
	{
		if (Group.Equals(TEXT("ViewDistance"), ESearchCase::IgnoreCase))           { Q.ViewDistanceQuality = Level; }
		else if (Group.Equals(TEXT("AntiAliasing"), ESearchCase::IgnoreCase))      { Q.AntiAliasingQuality = Level; }
		else if (Group.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase) || Group.Equals(TEXT("Shadows"), ESearchCase::IgnoreCase)) { Q.ShadowQuality = Level; }
		else if (Group.Equals(TEXT("GlobalIllumination"), ESearchCase::IgnoreCase) || Group.Equals(TEXT("GI"), ESearchCase::IgnoreCase)) { Q.GlobalIlluminationQuality = Level; }
		else if (Group.Equals(TEXT("Reflection"), ESearchCase::IgnoreCase) || Group.Equals(TEXT("Reflections"), ESearchCase::IgnoreCase)) { Q.ReflectionQuality = Level; }
		else if (Group.Equals(TEXT("PostProcess"), ESearchCase::IgnoreCase))       { Q.PostProcessQuality = Level; }
		else if (Group.Equals(TEXT("Texture"), ESearchCase::IgnoreCase) || Group.Equals(TEXT("Textures"), ESearchCase::IgnoreCase)) { Q.TextureQuality = Level; }
		else if (Group.Equals(TEXT("Effects"), ESearchCase::IgnoreCase))           { Q.EffectsQuality = Level; }
		else if (Group.Equals(TEXT("Foliage"), ESearchCase::IgnoreCase))           { Q.FoliageQuality = Level; }
		else if (Group.Equals(TEXT("Shading"), ESearchCase::IgnoreCase))           { Q.ShadingQuality = Level; }
		else { return false; }
		return true;
	}

	/** Resolve a settings "category" to its UDeveloperSettings CDO by section name (e.g. "rendering" -> URendererSettings). */
	UDeveloperSettings* ResolveDeveloperSettings(const FString& Category)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Cls = *It;
			if (!Cls->IsChildOf(UDeveloperSettings::StaticClass()) || Cls->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			UDeveloperSettings* CDO = Cast<UDeveloperSettings>(Cls->GetDefaultObject());
			if (!CDO)
			{
				continue;
			}
			if (CDO->GetSectionName().ToString().Equals(Category, ESearchCase::IgnoreCase)
				|| Cls->GetName().Equals(Category, ESearchCase::IgnoreCase))
			{
				return CDO;
			}
		}
		return nullptr;
	}
}

FMCPToolInfo FMCPTool_EngineSettings::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("engine_settings");
	Info.Description = TEXT(
		"Read and modify Unreal Engine configuration (settings, console variables, scalability, GC).\n\n"
		"Operations (set 'operation'):\n"
		"- 'get_setting': Read setting 'key' on a UDeveloperSettings 'category' (e.g., category='Rendering')\n"
		"- 'set_setting': Set setting 'key' to 'value' on 'category' (persists to the section's config)\n"
		"- 'get_cvar': Read a console variable by 'name' (e.g., 'r.ReflectionMethod')\n"
		"- 'set_cvar': Set console variable 'name' to 'value'\n"
		"- 'get_scalability': Get current scalability quality levels (all groups)\n"
		"- 'set_scalability': Set scalability 'group' (or empty for all) to quality 'level' (0=Low..4=Cinematic)\n"
		"- 'gc_settings': Read all gc.* cvars, or set the gc 'name' cvar to 'value'\n\n"
		"Settings categories are UDeveloperSettings section names (Rendering, Physics, Audio, etc.). "
		"Some changes require an editor restart to fully apply."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: get_setting, set_setting, get_cvar, set_cvar, get_scalability, set_scalability, gc_settings"), true),
		FMCPToolParameter(TEXT("category"), TEXT("string"), TEXT("For get_setting/set_setting: UDeveloperSettings section name (e.g., 'Rendering', 'Physics', 'Audio')"), false),
		FMCPToolParameter(TEXT("key"), TEXT("string"), TEXT("For get_setting/set_setting: property name within the category"), false),
		FMCPToolParameter(TEXT("name"), TEXT("string"), TEXT("For get_cvar/set_cvar/gc_settings: console variable name (e.g., 'r.ReflectionMethod', 'gc.MaxObjectsInEditor')"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For set_setting/set_cvar/gc_settings: new value as string"), false),
		FMCPToolParameter(TEXT("group"), TEXT("string"), TEXT("For set_scalability: ViewDistance, AntiAliasing, Shadow, GlobalIllumination, Reflection, PostProcess, Texture, Effects, Foliage, Shading (empty = all)"), false),
		FMCPToolParameter(TEXT("level"), TEXT("number"), TEXT("For set_scalability: quality level 0=Low, 1=Medium, 2=High, 3=Epic, 4=Cinematic"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_EngineSettings::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("get_setting"))     { return OpGetSetting(Params); }
	if (Operation == TEXT("set_setting"))     { return OpSetSetting(Params); }
	if (Operation == TEXT("get_cvar"))        { return OpGetCVar(Params); }
	if (Operation == TEXT("set_cvar"))        { return OpSetCVar(Params); }
	if (Operation == TEXT("get_scalability")) { return OpGetScalability(Params); }
	if (Operation == TEXT("set_scalability")) { return OpSetScalability(Params); }
	if (Operation == TEXT("gc_settings"))     { return OpGcSettings(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: get_setting, set_setting, get_cvar, set_cvar, get_scalability, set_scalability, gc_settings"), *Operation));
}

FMCPToolResult FMCPTool_EngineSettings::OpGetCVar(const TSharedRef<FJsonObject>& Params)
{
	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}

	FString Value;
	if (!GetCVarString(Name, Value))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Console variable '%s' not found"), *Name));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	Data->SetStringField(TEXT("value"), Value);
	return FMCPToolResult::Success(FString::Printf(TEXT("%s = %s"), *Name, *Value), Data);
}

FMCPToolResult FMCPTool_EngineSettings::OpSetCVar(const TSharedRef<FJsonObject>& Params)
{
	FString Name, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err)) { return Err.GetValue(); }

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Console variable '%s' not found"), *Name));
	}

	const FString OldValue = CVar->GetString();
	CVar->Set(*Value, ECVF_SetByConsole);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	Data->SetStringField(TEXT("old_value"), OldValue);
	Data->SetStringField(TEXT("value"), CVar->GetString());
	return FMCPToolResult::Success(FString::Printf(TEXT("Set %s: %s -> %s"), *Name, *OldValue, *CVar->GetString()), Data);
}

FMCPToolResult FMCPTool_EngineSettings::OpGetScalability(const TSharedRef<FJsonObject>& Params)
{
	const Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	QualityLevelsToJson(Q, Data);
	return FMCPToolResult::Success(TEXT("Current scalability quality levels (0=Low..4=Cinematic)"), Data);
}

FMCPToolResult FMCPTool_EngineSettings::OpSetScalability(const TSharedRef<FJsonObject>& Params)
{
	const int32 Level = ExtractOptionalNumber<int32>(Params, TEXT("level"), -1);
	if (Level < 0 || Level > 4)
	{
		return FMCPToolResult::Error(TEXT("'level' is required and must be 0..4 (0=Low, 1=Medium, 2=High, 3=Epic, 4=Cinematic)"));
	}

	const FString Group = ExtractOptionalString(Params, TEXT("group"));
	Scalability::FQualityLevels Q = Scalability::GetQualityLevels();

	if (Group.IsEmpty())
	{
		Q.SetFromSingleQualityLevel(Level);
	}
	else if (!SetQualityGroup(Q, Group, Level))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Unknown scalability group '%s'"), *Group));
	}

	Scalability::SetQualityLevels(Q, /*bForce=*/true);
	Scalability::SaveState(GGameUserSettingsIni);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	QualityLevelsToJson(Q, Data);
	return FMCPToolResult::Success(
		Group.IsEmpty()
			? FString::Printf(TEXT("Set all scalability groups to level %d"), Level)
			: FString::Printf(TEXT("Set scalability '%s' to level %d"), *Group, Level),
		Data);
}

FMCPToolResult FMCPTool_EngineSettings::OpGcSettings(const TSharedRef<FJsonObject>& Params)
{
	const FString Name = ExtractOptionalString(Params, TEXT("name"));
	const FString Value = ExtractOptionalString(Params, TEXT("value"));

	// Set a specific gc cvar.
	if (!Name.IsEmpty() && !Value.IsEmpty())
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!CVar)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("GC console variable '%s' not found"), *Name));
		}
		const FString OldValue = CVar->GetString();
		CVar->Set(*Value, ECVF_SetByConsole);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("name"), Name);
		Data->SetStringField(TEXT("old_value"), OldValue);
		Data->SetStringField(TEXT("value"), CVar->GetString());
		return FMCPToolResult::Success(FString::Printf(TEXT("Set %s: %s -> %s"), *Name, *OldValue, *CVar->GetString()), Data);
	}

	// Read a single gc cvar by name.
	if (!Name.IsEmpty())
	{
		FString V;
		if (!GetCVarString(Name, V))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("GC console variable '%s' not found"), *Name));
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("name"), Name);
		Data->SetStringField(TEXT("value"), V);
		return FMCPToolResult::Success(FString::Printf(TEXT("%s = %s"), *Name, *V), Data);
	}

	// List common GC cvars.
	static const TArray<FString> GcCVars = {
		TEXT("gc.TimeBetweenPurgingPendingKillObjects"),
		TEXT("gc.MaxObjectsInEditor"),
		TEXT("gc.IncrementalBeginDestroyEnabled"),
		TEXT("gc.AllowParallelGC"),
		TEXT("gc.NumRetriesBeforeForcingGC"),
		TEXT("gc.MultithreadedDestructionEnabled")
	};
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	for (const FString& C : GcCVars)
	{
		FString V;
		if (GetCVarString(C, V))
		{
			Data->SetStringField(C, V);
		}
	}
	return FMCPToolResult::Success(TEXT("Common garbage-collection console variables"), Data);
}

FMCPToolResult FMCPTool_EngineSettings::OpGetSetting(const TSharedRef<FJsonObject>& Params)
{
	FString Category, Key;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("category"), Category, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("key"), Key, Err)) { return Err.GetValue(); }

	UDeveloperSettings* Settings = ResolveDeveloperSettings(Category);
	if (!Settings)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Settings category '%s' not found"), *Category));
	}

	FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
	if (!Prop)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Setting '%s' not found on category '%s'"), *Key, *Category));
	}

	FString Value;
	Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(Settings), nullptr, Settings, PPF_None);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("category"), Settings->GetSectionName().ToString());
	Data->SetStringField(TEXT("key"), Key);
	Data->SetStringField(TEXT("value"), Value);
	Data->SetStringField(TEXT("type"), Prop->GetCPPType());
	return FMCPToolResult::Success(FString::Printf(TEXT("%s.%s = %s"), *Category, *Key, *Value), Data);
}

FMCPToolResult FMCPTool_EngineSettings::OpSetSetting(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Category, Key, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("category"), Category, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("key"), Key, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err)) { return Err.GetValue(); }

	UDeveloperSettings* Settings = ResolveDeveloperSettings(Category);
	if (!Settings)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Settings category '%s' not found"), *Category));
	}

	FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
	if (!Prop)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Setting '%s' not found on category '%s'"), *Key, *Category));
	}

	const FString OldValue = [&]() { FString S; Prop->ExportTextItem_Direct(S, Prop->ContainerPtrToValuePtr<void>(Settings), nullptr, Settings, PPF_None); return S; }();

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Settings);
	const TCHAR* Result = Prop->ImportText_Direct(*Value, ValuePtr, Settings, PPF_None);
	if (Result == nullptr)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Could not parse value '%s' for setting '%s' (%s)"), *Value, *Key, *Prop->GetCPPType()));
	}

	// Persist to the section's default config file.
	Settings->TryUpdateDefaultConfigFile();

	FString NewValue;
	Prop->ExportTextItem_Direct(NewValue, Prop->ContainerPtrToValuePtr<void>(Settings), nullptr, Settings, PPF_None);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("category"), Settings->GetSectionName().ToString());
	Data->SetStringField(TEXT("key"), Key);
	Data->SetStringField(TEXT("old_value"), OldValue);
	Data->SetStringField(TEXT("value"), NewValue);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set %s.%s: %s -> %s (some changes need an editor restart)"), *Category, *Key, *OldValue, *NewValue), Data);
#else
	return FMCPToolResult::Error(TEXT("set_setting requires an editor build"));
#endif
}
