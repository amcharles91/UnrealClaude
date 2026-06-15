// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_ProjectSettings.h"
#include "Engine/DeveloperSettings.h"
#include "GameMapsSettings.h"
#include "GeneralProjectSettings.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "Misc/App.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Dom/JsonObject.h"

namespace
{
	/**
	 * Friendly category aliases map a short id to a concrete settings UClass name.
	 * Several project settings live on plain UObject config classes (GeneralProjectSettings,
	 * GameMapsSettings) that are NOT UDeveloperSettings subclasses, so we resolve them by
	 * class name rather than by GetSectionName().
	 */
	struct FCategoryAlias
	{
		const TCHAR* Id;
		const TCHAR* ClassName;
	};

	static const FCategoryAlias CategoryAliases[] = {
		{ TEXT("general"),   TEXT("GeneralProjectSettings") },
		{ TEXT("maps"),      TEXT("GameMapsSettings") },
		{ TEXT("rendering"), TEXT("RendererSettings") },
		{ TEXT("physics"),   TEXT("PhysicsSettings") },
		{ TEXT("input"),     TEXT("InputSettings") },
		{ TEXT("audio"),     TEXT("AudioSettings") },
	};

	/** Resolve a friendly alias id (e.g. "general") to its concrete settings class name. Returns empty if no alias. */
	FString ResolveCategoryAlias(const FString& Category)
	{
		for (const FCategoryAlias& Alias : CategoryAliases)
		{
			if (Category.Equals(Alias.Id, ESearchCase::IgnoreCase))
			{
				return Alias.ClassName;
			}
		}
		return FString();
	}

	/**
	 * Resolve a settings "category" to a config-backed CDO. Accepts:
	 *   - a friendly alias ("general", "maps", "rendering", ...)
	 *   - a UDeveloperSettings section name ("Rendering", "Physics", ...)
	 *   - a raw UClass name ("GeneralProjectSettings", "RendererSettings", ...)
	 * Returns the CDO whose properties are read/written, or nullptr if not found.
	 */
	UObject* ResolveSettingsObject(const FString& Category)
	{
		// Friendly alias takes priority so "general"/"maps" hit the plain UObject configs.
		const FString AliasClassName = ResolveCategoryAlias(Category);
		const FString TargetClassName = AliasClassName.IsEmpty() ? Category : AliasClassName;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Cls = *It;
			if (!Cls || Cls->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}

			// Match by raw class name (works for both DeveloperSettings and plain config UObjects).
			if (Cls->GetName().Equals(TargetClassName, ESearchCase::IgnoreCase))
			{
				return Cls->GetDefaultObject();
			}

			// Also match UDeveloperSettings by their section name (e.g. "Rendering" -> URendererSettings).
			if (Cls->IsChildOf(UDeveloperSettings::StaticClass()))
			{
				if (UDeveloperSettings* DevCDO = Cast<UDeveloperSettings>(Cls->GetDefaultObject()))
				{
					if (DevCDO->GetSectionName().ToString().Equals(Category, ESearchCase::IgnoreCase))
					{
						return DevCDO;
					}
				}
			}
		}
		return nullptr;
	}

	/** Should a property be exposed for get/set? Only config/editable, non-deprecated, non-transient. */
	bool ShouldExposeProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}
		if (Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
		{
			return false;
		}
		return Property->HasAnyPropertyFlags(CPF_Config | CPF_GlobalConfig | CPF_Edit);
	}

	/** Export a property value on a container object to a string. */
	FString ExportProperty(const FProperty* Property, const UObject* Container)
	{
		FString Value;
		Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Container), nullptr, const_cast<UObject*>(Container), PPF_None);
		return Value;
	}
}

FMCPToolInfo FMCPTool_ProjectSettings::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("project_settings");
	Info.Description = TEXT(
		"Read and modify Unreal Engine project settings (Default*.ini configuration).\n\n"
		"Operations (set 'operation'):\n"
		"- 'get': Read a setting 'key' within 'category' (e.g., category='general', key='ProjectName')\n"
		"- 'set': Set a setting 'key' to 'value' within 'category'\n"
		"- 'list_categories': List all available project settings categories\n"
		"- 'set_startup_map': Set the editor/game default startup map to 'map' (a /Game/... path)\n"
		"- 'get_project_metadata': Read project metadata (name, company, version, description)\n\n"
		"Categories include general, maps, rendering, physics, input, audio and any discovered "
		"UDeveloperSettings subclass. Changes are written to the project's config files and some "
		"require an editor restart."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: get, set, list_categories, set_startup_map, get_project_metadata"), true),
		FMCPToolParameter(TEXT("category"), TEXT("string"), TEXT("For get/set: category id (e.g., 'general', 'maps', 'rendering')"), false),
		FMCPToolParameter(TEXT("key"), TEXT("string"), TEXT("For get/set: setting key name within the category (e.g., 'ProjectName')"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For set: new value as string"), false),
		FMCPToolParameter(TEXT("map"), TEXT("string"), TEXT("For set_startup_map: map asset path (e.g., '/Game/Maps/MainMenu')"), false),
		FMCPToolParameter(TEXT("startup_type"), TEXT("string"), TEXT("For set_startup_map: 'editor' or 'game' (default: both)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_ProjectSettings::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("get"))                  { return OpGet(Params); }
	if (Operation == TEXT("set"))                  { return OpSet(Params); }
	if (Operation == TEXT("list_categories"))      { return OpListCategories(Params); }
	if (Operation == TEXT("set_startup_map"))      { return OpSetStartupMap(Params); }
	if (Operation == TEXT("get_project_metadata")) { return OpGetProjectMetadata(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: get, set, list_categories, set_startup_map, get_project_metadata"), *Operation));
}

FMCPToolResult FMCPTool_ProjectSettings::OpGet(const TSharedRef<FJsonObject>& Params)
{
	FString Category, Key;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("category"), Category, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("key"), Key, Err)) { return Err.GetValue(); }

	UObject* Settings = ResolveSettingsObject(Category);
	if (!Settings)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Settings category '%s' not found"), *Category));
	}

	FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
	if (!Prop)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Setting '%s' not found on category '%s'"), *Key, *Category));
	}

	const FString Value = ExportProperty(Prop, Settings);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("category"), Category);
	Data->SetStringField(TEXT("class"), Settings->GetClass()->GetName());
	Data->SetStringField(TEXT("key"), Key);
	Data->SetStringField(TEXT("value"), Value);
	Data->SetStringField(TEXT("type"), Prop->GetCPPType());
	return FMCPToolResult::Success(FString::Printf(TEXT("%s.%s = %s"), *Category, *Key, *Value), Data);
}

FMCPToolResult FMCPTool_ProjectSettings::OpSet(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Category, Key, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("category"), Category, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("key"), Key, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err)) { return Err.GetValue(); }

	UObject* Settings = ResolveSettingsObject(Category);
	if (!Settings)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Settings category '%s' not found"), *Category));
	}

	FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
	if (!Prop)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Setting '%s' not found on category '%s'"), *Key, *Category));
	}

	if (Prop->HasAnyPropertyFlags(CPF_EditConst))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Setting '%s.%s' is read-only"), *Category, *Key));
	}

	const FString OldValue = ExportProperty(Prop, Settings);

	// Notify the object before/after the change so runtime effects fire (mirrors the editor).
	Settings->PreEditChange(Prop);

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Settings);
	const TCHAR* Result = Prop->ImportText_Direct(*Value, ValuePtr, Settings, PPF_None);
	if (Result == nullptr)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Could not parse value '%s' for setting '%s' (%s)"), *Value, *Key, *Prop->GetCPPType()));
	}

	FPropertyChangedEvent ChangedEvent(Prop, EPropertyChangeType::ValueSet);
	Settings->PostEditChangeProperty(ChangedEvent);

	// Persist the CDO's current values to the section's default config file (DefaultGame.ini, DefaultEngine.ini, ...).
	Settings->TryUpdateDefaultConfigFile();

	const FString NewValue = ExportProperty(Prop, Settings);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("category"), Category);
	Data->SetStringField(TEXT("class"), Settings->GetClass()->GetName());
	Data->SetStringField(TEXT("key"), Key);
	Data->SetStringField(TEXT("old_value"), OldValue);
	Data->SetStringField(TEXT("value"), NewValue);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set %s.%s: %s -> %s (some changes need an editor restart)"), *Category, *Key, *OldValue, *NewValue), Data);
#else
	return FMCPToolResult::Error(TEXT("set requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_ProjectSettings::OpListCategories(const TSharedRef<FJsonObject>& Params)
{
	TSet<FString> Seen;
	TArray<TSharedPtr<FJsonValue>> CategoriesJson;

	auto AddCategory = [&CategoriesJson, &Seen](const FString& Id, const FString& ClassName, UObject* CDO)
	{
		if (!CDO || Seen.Contains(ClassName))
		{
			return;
		}
		Seen.Add(ClassName);

		int32 Count = 0;
		for (TFieldIterator<FProperty> PropIt(CDO->GetClass()); PropIt; ++PropIt)
		{
			if (ShouldExposeProperty(*PropIt))
			{
				++Count;
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Id);
		Entry->SetStringField(TEXT("class"), ClassName);
		Entry->SetNumberField(TEXT("setting_count"), Count);

		if (UDeveloperSettings* DevCDO = Cast<UDeveloperSettings>(CDO))
		{
			Entry->SetStringField(TEXT("section"), DevCDO->GetSectionName().ToString());
		}
		CategoriesJson.Add(MakeShared<FJsonValueObject>(Entry));
	};

	// Friendly aliases first (general, maps, rendering, ...).
	for (const FCategoryAlias& Alias : CategoryAliases)
	{
		UObject* CDO = ResolveSettingsObject(FString(Alias.Id));
		if (CDO)
		{
			AddCategory(FString(Alias.Id), CDO->GetClass()->GetName(), CDO);
		}
	}

	// Then every discovered UDeveloperSettings subclass (keyed by section name).
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls
			|| !Cls->IsChildOf(UDeveloperSettings::StaticClass())
			|| Cls == UDeveloperSettings::StaticClass()
			|| Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}
		if (UDeveloperSettings* DevCDO = Cast<UDeveloperSettings>(Cls->GetDefaultObject()))
		{
			AddCategory(DevCDO->GetSectionName().ToString(), Cls->GetName(), DevCDO);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("categories"), CategoriesJson);
	Data->SetNumberField(TEXT("count"), CategoriesJson.Num());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d project settings categories"), CategoriesJson.Num()), Data);
}

FMCPToolResult FMCPTool_ProjectSettings::OpSetStartupMap(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Map;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("map"), Map, Err)) { return Err.GetValue(); }

	const FString StartupType = ExtractOptionalString(Params, TEXT("startup_type"), TEXT("both"));
	const bool bSetEditor = StartupType.Equals(TEXT("both"), ESearchCase::IgnoreCase) || StartupType.Equals(TEXT("editor"), ESearchCase::IgnoreCase);
	const bool bSetGame   = StartupType.Equals(TEXT("both"), ESearchCase::IgnoreCase) || StartupType.Equals(TEXT("game"), ESearchCase::IgnoreCase);

	if (!bSetEditor && !bSetGame)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Invalid startup_type '%s'. Use 'editor', 'game', or 'both'"), *StartupType));
	}

	UGameMapsSettings* MapsSettings = GetMutableDefault<UGameMapsSettings>();
	if (!MapsSettings)
	{
		return FMCPToolResult::Error(TEXT("set_startup_map: GameMapsSettings unavailable"));
	}

	const FSoftObjectPath MapPath(Map);
	const FString OldEditorMap = MapsSettings->EditorStartupMap.ToString();
	const FString OldGameMap = UGameMapsSettings::GetGameDefaultMap();

	if (bSetEditor)
	{
		MapsSettings->EditorStartupMap = MapPath;
	}
	if (bSetGame)
	{
		UGameMapsSettings::SetGameDefaultMap(MapPath.ToString());
	}

	MapsSettings->TryUpdateDefaultConfigFile();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("map"), Map);
	Data->SetStringField(TEXT("startup_type"), StartupType);
	if (bSetEditor)
	{
		Data->SetStringField(TEXT("old_editor_startup_map"), OldEditorMap);
		Data->SetStringField(TEXT("editor_startup_map"), MapsSettings->EditorStartupMap.ToString());
	}
	if (bSetGame)
	{
		Data->SetStringField(TEXT("old_game_default_map"), OldGameMap);
		Data->SetStringField(TEXT("game_default_map"), UGameMapsSettings::GetGameDefaultMap());
	}
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set startup map (%s) to '%s'"), *StartupType, *Map), Data);
#else
	return FMCPToolResult::Error(TEXT("set_startup_map requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_ProjectSettings::OpGetProjectMetadata(const TSharedRef<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	// Authoritative project metadata lives on the GeneralProjectSettings CDO.
	const UGeneralProjectSettings* GeneralSettings = GetDefault<UGeneralProjectSettings>();
	if (GeneralSettings)
	{
		Data->SetStringField(TEXT("project_name"), GeneralSettings->ProjectName);
		Data->SetStringField(TEXT("project_version"), GeneralSettings->ProjectVersion);
		Data->SetStringField(TEXT("company_name"), GeneralSettings->CompanyName);
		Data->SetStringField(TEXT("description"), GeneralSettings->Description);
		Data->SetStringField(TEXT("copyright_notice"), GeneralSettings->CopyrightNotice);
		Data->SetStringField(TEXT("homepage"), GeneralSettings->Homepage);
		Data->SetStringField(TEXT("project_id"), GeneralSettings->ProjectID.ToString());
	}

	// The on-disk project name (the .uproject base name) is always available.
	Data->SetStringField(TEXT("app_project_name"), FApp::GetProjectName());

	// The loaded .uproject descriptor supplies engine association + its own description.
	if (const FProjectDescriptor* Descriptor = IProjectManager::Get().GetCurrentProject())
	{
		Data->SetStringField(TEXT("engine_association"), Descriptor->EngineAssociation);
		Data->SetStringField(TEXT("descriptor_description"), Descriptor->Description);
		Data->SetBoolField(TEXT("is_enterprise"), Descriptor->bIsEnterpriseProject);
	}

	const FString ProjectName = GeneralSettings ? GeneralSettings->ProjectName : FString();
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Project metadata for '%s'"),
			ProjectName.IsEmpty() ? FApp::GetProjectName() : *ProjectName),
		Data);
}
