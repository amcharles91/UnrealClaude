// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_DataAsset.h"
#include "UnrealClaudeModule.h"
#include "Engine/DataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Field.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "UObject/SavePackage.h"
#endif

FMCPToolInfo FMCPTool_DataAsset::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("data_asset");
	Info.Description = TEXT(
		"Manage general-purpose Unreal Engine UDataAsset instances (any UDataAsset subclass).\n\n"
		"Operations (set 'operation'):\n"
		"- 'list': List DataAsset instances under 'search_path' (default /Game); optional 'class_name' filter\n"
		"- 'create': Create a new instance of 'class_name' at 'package_path'/'asset_name'; optional 'properties' object\n"
		"- 'get': Read properties of the DataAsset at 'asset_path'\n"
		"- 'set_property': Set 'property_name' to 'value' (string) on the DataAsset at 'asset_path'\n\n"
		"Property values are passed as Unreal text literals (the same format the editor copy/paste uses). "
		"For object references inside structs/arrays use the full path form '/Game/Path/Asset.Asset'.\n\n"
		"For character-specific config assets, prefer the dedicated 'character_data' tool.\n\n"
		"Returns: operation-specific data (asset list, property list, or the modified property)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: list, create, get, set_property"), true),
		FMCPToolParameter(TEXT("class_name"), TEXT("string"), TEXT("For 'create' (required): UDataAsset subclass name (with or without 'U' prefix). For 'list': optional class filter"), false),
		FMCPToolParameter(TEXT("package_path"), TEXT("string"), TEXT("For 'create': destination directory (default: /Game/Data)"), false),
		FMCPToolParameter(TEXT("asset_name"), TEXT("string"), TEXT("For 'create' (required): name of the new asset"), false),
		FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("For 'get'/'set_property' (required): full object path to the DataAsset"), false),
		FMCPToolParameter(TEXT("search_path"), TEXT("string"), TEXT("For 'list': content path to search recursively (default: /Game)"), false),
		FMCPToolParameter(TEXT("property_name"), TEXT("string"), TEXT("For 'set_property' (required): name of the property to set"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For 'set_property' (required): new value as an Unreal text literal"), false),
		FMCPToolParameter(TEXT("properties"), TEXT("object"), TEXT("For 'create': optional initial property name->value (string) pairs"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_DataAsset::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("list"))         { return OpList(Params); }
	if (Operation == TEXT("create"))       { return OpCreate(Params); }
	if (Operation == TEXT("get"))          { return OpGet(Params); }
	if (Operation == TEXT("set_property")) { return OpSetProperty(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: list, create, get, set_property"), *Operation));
}

// =================================================================
// Reflection / asset helpers
// =================================================================

UClass* FMCPTool_DataAsset::FindDataAssetClass(const FString& ClassName, bool bAllowAbstract)
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	// Try the name as given, plus toggling the 'U' prefix.
	const FString SearchNames[3] = {
		ClassName,
		ClassName.StartsWith(TEXT("U")) ? ClassName.RightChop(1) : FString::Printf(TEXT("U%s"), *ClassName),
		ClassName.StartsWith(TEXT("U")) ? ClassName : FString::Printf(TEXT("U%s"), *ClassName)
	};

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class || !Class->IsChildOf(UDataAsset::StaticClass()))
		{
			continue;
		}
		if (!bAllowAbstract && Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}

		const FString Name = Class->GetName();
		// Skip Blueprint compiler artifacts (skeleton / reinstanced classes).
		if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_")))
		{
			continue;
		}

		for (const FString& SearchName : SearchNames)
		{
			if (Name.Equals(SearchName, ESearchCase::IgnoreCase))
			{
				return Class;
			}
		}
	}

	return nullptr;
}

UDataAsset* FMCPTool_DataAsset::LoadDataAsset(const FString& AssetPath, FString& OutError)
{
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("Asset path is empty");
		return nullptr;
	}

	UDataAsset* Asset = LoadObject<UDataAsset>(nullptr, *AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Failed to load DataAsset: %s"), *AssetPath);
	}
	return Asset;
}

bool FMCPTool_DataAsset::ShouldExposeProperty(const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}
	if (Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
	{
		return false;
	}
	return Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_SaveGame);
}

FString FMCPTool_DataAsset::GetPropertyTypeString(const FProperty* Property)
{
	if (!Property)
	{
		return TEXT("Unknown");
	}

	if (CastField<FBoolProperty>(Property))   { return TEXT("bool"); }
	if (CastField<FIntProperty>(Property))    { return TEXT("int32"); }
	if (CastField<FInt64Property>(Property))  { return TEXT("int64"); }
	if (CastField<FFloatProperty>(Property))  { return TEXT("float"); }
	if (CastField<FDoubleProperty>(Property)) { return TEXT("double"); }
	if (CastField<FStrProperty>(Property))    { return TEXT("FString"); }
	if (CastField<FNameProperty>(Property))   { return TEXT("FName"); }
	if (CastField<FTextProperty>(Property))   { return TEXT("FText"); }

	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		if (const UEnum* Enum = EnumProp->GetEnum())
		{
			return Enum->GetName();
		}
	}
	if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (const UEnum* Enum = ByteProp->Enum)
		{
			return Enum->GetName();
		}
		return TEXT("uint8");
	}
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (const UScriptStruct* Struct = StructProp->Struct)
		{
			return Struct->GetName();
		}
	}
	if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		if (const UClass* PropClass = ObjProp->PropertyClass)
		{
			return FString::Printf(TEXT("%s*"), *PropClass->GetName());
		}
	}
	if (const FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		if (const UClass* PropClass = SoftObjProp->PropertyClass)
		{
			return FString::Printf(TEXT("TSoftObjectPtr<%s>"), *PropClass->GetName());
		}
	}
	if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("TArray<%s>"), *GetPropertyTypeString(ArrayProp->Inner));
	}
	if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return FString::Printf(TEXT("TMap<%s, %s>"),
			*GetPropertyTypeString(MapProp->KeyProp), *GetPropertyTypeString(MapProp->ValueProp));
	}
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return FString::Printf(TEXT("TSet<%s>"), *GetPropertyTypeString(SetProp->ElementProp));
	}

	return Property->GetCPPType();
}

FString FMCPTool_DataAsset::PropertyToString(const FProperty* Property, const void* Container)
{
	if (!Property || !Container)
	{
		return FString();
	}

	FString Value;
	Property->ExportTextItem_Direct(
		Value, Property->ContainerPtrToValuePtr<void>(Container), nullptr, nullptr, PPF_None);
	return Value;
}

bool FMCPTool_DataAsset::SetPropertyFromString(const FProperty* Property, void* Container, const FString& Value, FString& OutError)
{
	if (!Property || !Container)
	{
		OutError = TEXT("Invalid property or container");
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

	const TCHAR* ImportResult = Property->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
	if (ImportResult == nullptr)
	{
		OutError = FString::Printf(
			TEXT("Failed to parse value '%s' for property type %s"), *Value, *GetPropertyTypeString(Property));
		return false;
	}

	// ImportText_Direct returns a pointer to the first unconsumed character. A
	// partial parse (bad inner value / unresolvable object path) returns non-null
	// but leaves text behind and silently keeps a partial value — treat as failure.
	while (*ImportResult == TEXT(' ') || *ImportResult == TEXT('\t') || *ImportResult == TEXT('\r') || *ImportResult == TEXT('\n'))
	{
		++ImportResult;
	}
	if (*ImportResult != TEXT('\0'))
	{
		OutError = FString::Printf(
			TEXT("Value for property type %s was only partially parsed; unparsed remainder: '%s'. ")
			TEXT("For object references inside structs/arrays use the full path form /Game/Path/Asset.Asset"),
			*GetPropertyTypeString(Property), ImportResult);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FMCPTool_DataAsset::PropertyInfoJson(const FProperty* Property, const void* Container)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Property)
	{
		return Obj;
	}

	Obj->SetStringField(TEXT("name"), Property->GetName());
	Obj->SetStringField(TEXT("type"), GetPropertyTypeString(Property));
	Obj->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
	Obj->SetBoolField(TEXT("read_only"), Property->HasAnyPropertyFlags(CPF_EditConst));
	Obj->SetBoolField(TEXT("is_array"), Property->IsA<FArrayProperty>());
	if (Container)
	{
		Obj->SetStringField(TEXT("value"), PropertyToString(Property, Container));
	}
	return Obj;
}

bool FMCPTool_DataAsset::SaveAsset(UObject* Asset, FString& OutError)
{
#if WITH_EDITOR
	if (!Asset)
	{
		OutError = TEXT("Cannot save null asset");
		return false;
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Asset has no package");
		return false;
	}

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	const FSavePackageResultStruct Result = UPackage::Save(Package, Asset, *PackageFileName, SaveArgs);
	if (Result.Result != ESavePackageResult::Success)
	{
		OutError = FString::Printf(TEXT("Failed to save asset: %s"), *PackageFileName);
		return false;
	}
	return true;
#else
	OutError = TEXT("Saving assets requires an editor build");
	return false;
#endif
}

// =================================================================
// Operations
// =================================================================

FMCPToolResult FMCPTool_DataAsset::OpList(const TSharedRef<FJsonObject>& Params)
{
	const FString ClassName = ExtractOptionalString(Params, TEXT("class_name"));
	const FString SearchPath = ExtractOptionalString(Params, TEXT("search_path"), TEXT("/Game"));

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*SearchPath));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;

	if (ClassName.IsEmpty())
	{
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("DataAsset")));
	}
	else
	{
		UClass* TargetClass = FindDataAssetClass(ClassName, true);
		if (!TargetClass)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("DataAsset class not found: %s"), *ClassName));
		}
		Filter.ClassPaths.Add(TargetClass->GetClassPathName());
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		Entry->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		Entry->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("assets"), Arr);
	Data->SetNumberField(TEXT("count"), Arr.Num());
	Data->SetStringField(TEXT("search_path"), SearchPath);

	const FString Msg = ClassName.IsEmpty()
		? FString::Printf(TEXT("Found %d DataAsset(s) under '%s'"), Arr.Num(), *SearchPath)
		: FString::Printf(TEXT("Found %d '%s' DataAsset(s) under '%s'"), Arr.Num(), *ClassName, *SearchPath);
	return FMCPToolResult::Success(Msg, Data);
}

FMCPToolResult FMCPTool_DataAsset::OpCreate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString ClassName, AssetName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("class_name"), ClassName, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("asset_name"), AssetName, Err))
	{
		return Err.GetValue();
	}

	FString PackagePath = ExtractOptionalString(Params, TEXT("package_path"), TEXT("/Game/Data"));
	if (!ValidateBlueprintPathParam(PackagePath, Err))
	{
		return Err.GetValue();
	}

	UClass* DataAssetClass = FindDataAssetClass(ClassName, false);
	if (!DataAssetClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("DataAsset class not found (or is abstract): %s"), *ClassName));
	}

	const FString FullPackagePath = PackagePath / AssetName;
	if (FPackageName::DoesPackageExist(FullPackagePath))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("An asset already exists at '%s'. Delete it first or use a different name."), *FullPackagePath));
	}

	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package: %s"), *FullPackagePath));
	}

	UDataAsset* Asset = NewObject<UDataAsset>(
		Package, DataAssetClass, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Asset)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to instantiate '%s' as '%s'"), *ClassName, *AssetName));
	}

	// Apply optional initial properties.
	TArray<FString> SetOk, SetFailed;
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && (*PropsObj).IsValid())
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FProperty* Property = DataAssetClass->FindPropertyByName(*Pair.Key);
			if (!Property || !ShouldExposeProperty(Property))
			{
				SetFailed.Add(FString::Printf(TEXT("%s: not found or not editable"), *Pair.Key));
				continue;
			}

			FString ValueStr;
			if (!Pair.Value->TryGetString(ValueStr))
			{
				// Non-string JSON values: serialize back to a text literal.
				TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
					TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ValueStr);
				FJsonSerializer::Serialize(Pair.Value.ToSharedRef(), TEXT(""), Writer);
			}

			FString SetErr;
			if (SetPropertyFromString(Property, Asset, ValueStr, SetErr))
			{
				SetOk.Add(Pair.Key);
			}
			else
			{
				SetFailed.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *SetErr));
			}
		}
	}

	Package->MarkPackageDirty();
	FString SaveError;
	if (!SaveAsset(Asset, SaveError))
	{
		return FMCPToolResult::Error(SaveError);
	}

	FAssetRegistryModule::AssetCreated(Asset);
	UE_LOG(LogUnrealClaude, Log, TEXT("Created DataAsset: %s (class: %s)"), *FullPackagePath, *DataAssetClass->GetName());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("asset_name"), AssetName);
	Data->SetStringField(TEXT("class"), DataAssetClass->GetName());
	Data->SetArrayField(TEXT("properties_set"), StringArrayToJsonArray(SetOk));
	Data->SetArrayField(TEXT("properties_failed"), StringArrayToJsonArray(SetFailed));

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Created '%s' DataAsset at %s"), *DataAssetClass->GetName(), *FullPackagePath), Data);
	if (SetFailed.Num() > 0)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Some properties were not applied: %s"), *FString::Join(SetFailed, TEXT("; "))));
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("DataAsset creation requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_DataAsset::OpGet(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err))
	{
		return Err.GetValue();
	}

	FString LoadError;
	UDataAsset* Asset = LoadDataAsset(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	UClass* AssetClass = Asset->GetClass();

	TArray<TSharedPtr<FJsonValue>> PropArr;
	for (TFieldIterator<FProperty> PropIt(AssetClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!ShouldExposeProperty(Property))
		{
			continue;
		}
		PropArr.Add(MakeShared<FJsonValueObject>(PropertyInfoJson(Property, Asset)));
	}

	// Parent class chain (useful context for choosing class_name on create).
	TArray<FString> ParentChain;
	for (UClass* C = AssetClass->GetSuperClass(); C && C != UObject::StaticClass(); C = C->GetSuperClass())
	{
		ParentChain.Add(C->GetName());
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("asset_name"), Asset->GetName());
	Data->SetStringField(TEXT("class"), AssetClass->GetName());
	Data->SetArrayField(TEXT("parent_classes"), StringArrayToJsonArray(ParentChain));
	Data->SetArrayField(TEXT("properties"), PropArr);
	Data->SetNumberField(TEXT("property_count"), PropArr.Num());

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Retrieved '%s' (%d editable propert%s)"),
			*Asset->GetName(), PropArr.Num(), PropArr.Num() == 1 ? TEXT("y") : TEXT("ies")), Data);
}

FMCPToolResult FMCPTool_DataAsset::OpSetProperty(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, PropertyName, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("property_name"), PropertyName, Err))
	{
		return Err.GetValue();
	}
	// 'value' may legitimately be empty (e.g. clearing an FString), so read it directly.
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return FMCPToolResult::Error(TEXT("Missing required parameter: value"));
	}

	FString LoadError;
	UDataAsset* Asset = LoadDataAsset(AssetPath, LoadError);
	if (!Asset)
	{
		return FMCPToolResult::Error(LoadError);
	}

	UClass* AssetClass = Asset->GetClass();
	FProperty* Property = AssetClass->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Property '%s' not found on '%s'"), *PropertyName, *AssetClass->GetName()));
	}
	if (!ShouldExposeProperty(Property))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Property '%s' is not editable"), *PropertyName));
	}
	if (Property->HasAnyPropertyFlags(CPF_EditConst))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Property '%s' is read-only (EditConst)"), *PropertyName));
	}

#if WITH_EDITOR
	Asset->PreEditChange(Property);
#endif

	FString SetError;
	if (!SetPropertyFromString(Property, Asset, Value, SetError))
	{
		return FMCPToolResult::Error(SetError);
	}

#if WITH_EDITOR
	FPropertyChangedEvent ChangeEvent(Property);
	Asset->PostEditChangeProperty(ChangeEvent);
#endif

	Asset->MarkPackageDirty();
	FString SaveError;
	if (!SaveAsset(Asset, SaveError))
	{
		return FMCPToolResult::Error(SaveError);
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Set DataAsset property %s.%s = %s"), *Asset->GetName(), *PropertyName, *Value);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetObjectField(TEXT("property"), PropertyInfoJson(Property, Asset));

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set '%s' on '%s'"), *PropertyName, *Asset->GetName()), Data);
#else
	return FMCPToolResult::Error(TEXT("Editing DataAsset properties requires an editor build"));
#endif
}
