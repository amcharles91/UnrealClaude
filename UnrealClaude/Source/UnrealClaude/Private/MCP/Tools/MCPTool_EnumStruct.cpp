// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_EnumStruct.h"
#include "UnrealClaudeModule.h"

#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/EnumFactory.h"
#include "Factories/StructureFactory.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"
#endif

// =============================================================================
// Tool info & dispatch
// =============================================================================

FMCPToolInfo FMCPTool_EnumStruct::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("enum_struct");
	Info.Description = TEXT(
		"Create and manage user-defined Enums and Structs.\n\n"
		"Operations (set 'operation'):\n"
		"- 'list_enums': List enums; optional 'filter' substring, 'user_defined_only' bool\n"
		"- 'get_enum': Details for 'name' enum (values, display names, numeric values)\n"
		"- 'create_enum': Create a UserDefinedEnum 'name' under 'path' (default /Game/Enums)\n"
		"- 'delete_enum': Delete the UserDefinedEnum at 'name' (path)\n"
		"- 'add_enum_value': Add 'value' to enum 'name' (optional 'display_name')\n"
		"- 'remove_enum_value': Remove 'value' from enum 'name'\n"
		"- 'set_enum_display_name': Set 'display_name' for 'value' in enum 'name'\n"
		"- 'list_structs': List structs; optional 'filter' substring, 'user_defined_only' bool\n"
		"- 'get_struct': Details for 'name' struct (properties + friendly type strings)\n"
		"- 'create_struct': Create a UserDefinedStruct 'name' under 'path' (default /Game/Structs)\n"
		"- 'delete_struct': Delete the UserDefinedStruct at 'name' (path)\n"
		"- 'add_struct_property': Add 'property' of 'type' to struct 'name' (optional 'default', 'container')\n"
		"- 'remove_struct_property': Remove 'property' from struct 'name'\n\n"
		"Type strings for add_struct_property: bool, byte, int32, int64, float, double, name, "
		"string, text, an enum/struct name (e.g. 'EWeaponType', 'FVector'), or an object class. "
		"'container' may be Array, Set, or Map.\n\n"
		"Names accept either a short asset name or a full /Game/... path. Editor build required "
		"for create/delete/add/remove/set operations.\n\n"
		"Returns: operation-specific data (enum/struct lists, info, or the modified item)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: list_enums, get_enum, create_enum, delete_enum, add_enum_value, remove_enum_value, set_enum_display_name, list_structs, get_struct, create_struct, delete_struct, add_struct_property, remove_struct_property"), true),
		FMCPToolParameter(TEXT("name"), TEXT("string"), TEXT("Enum/struct asset name or full path (for get/create/delete/add/remove/set)"), false),
		FMCPToolParameter(TEXT("path"), TEXT("string"), TEXT("For create: destination directory (default /Game/Enums or /Game/Structs)"), false),
		FMCPToolParameter(TEXT("filter"), TEXT("string"), TEXT("For list_*: case-insensitive substring filter"), false),
		FMCPToolParameter(TEXT("user_defined_only"), TEXT("boolean"), TEXT("For list_*: if true, only user-defined types (default false)"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("Enum value name for add/remove/set_enum_display_name"), false),
		FMCPToolParameter(TEXT("display_name"), TEXT("string"), TEXT("Display name for add_enum_value / set_enum_display_name"), false),
		FMCPToolParameter(TEXT("property"), TEXT("string"), TEXT("Struct property name for add/remove_struct_property"), false),
		FMCPToolParameter(TEXT("type"), TEXT("string"), TEXT("For add_struct_property: type string (bool, int32, float, FVector, EMyEnum, ...)"), false),
		FMCPToolParameter(TEXT("default"), TEXT("string"), TEXT("For add_struct_property: optional default value as string"), false),
		FMCPToolParameter(TEXT("container"), TEXT("string"), TEXT("For add_struct_property: optional Array, Set, or Map"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_EnumStruct::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("list_enums"))             { return OpListEnums(Params); }
	if (Operation == TEXT("get_enum"))               { return OpGetEnum(Params); }
	if (Operation == TEXT("create_enum"))            { return OpCreateEnum(Params); }
	if (Operation == TEXT("delete_enum"))            { return OpDeleteEnum(Params); }
	if (Operation == TEXT("add_enum_value"))         { return OpAddEnumValue(Params); }
	if (Operation == TEXT("remove_enum_value"))      { return OpRemoveEnumValue(Params); }
	if (Operation == TEXT("set_enum_display_name"))  { return OpSetEnumDisplayName(Params); }
	if (Operation == TEXT("list_structs"))           { return OpListStructs(Params); }
	if (Operation == TEXT("get_struct"))             { return OpGetStruct(Params); }
	if (Operation == TEXT("create_struct"))          { return OpCreateStruct(Params); }
	if (Operation == TEXT("delete_struct"))          { return OpDeleteStruct(Params); }
	if (Operation == TEXT("add_struct_property"))    { return OpAddStructProperty(Params); }
	if (Operation == TEXT("remove_struct_property")) { return OpRemoveStructProperty(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: list_enums, get_enum, create_enum, delete_enum, ")
		TEXT("add_enum_value, remove_enum_value, set_enum_display_name, list_structs, get_struct, ")
		TEXT("create_struct, delete_struct, add_struct_property, remove_struct_property"), *Operation));
}

// =============================================================================
// Lookup helpers
// =============================================================================

UEnum* FMCPTool_EnumStruct::FindEnumByPathOrName(const FString& PathOrName)
{
	if (PathOrName.IsEmpty())
	{
		return nullptr;
	}

#if WITH_EDITOR
	if (UEnum* Loaded = Cast<UEnum>(UEditorAssetLibrary::LoadAsset(PathOrName)))
	{
		return Loaded;
	}
#endif

	// Resolve by full object path, then fall back to a name scan across loaded enums.
	if (UEnum* ByPath = FindObject<UEnum>(nullptr, *PathOrName))
	{
		return ByPath;
	}
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		if (It->GetName().Equals(PathOrName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

UScriptStruct* FMCPTool_EnumStruct::FindStructByPathOrName(const FString& PathOrName)
{
	if (PathOrName.IsEmpty())
	{
		return nullptr;
	}

#if WITH_EDITOR
	if (UScriptStruct* Loaded = Cast<UScriptStruct>(UEditorAssetLibrary::LoadAsset(PathOrName)))
	{
		return Loaded;
	}
#endif

	if (UScriptStruct* ByPath = FindObject<UScriptStruct>(nullptr, *PathOrName))
	{
		return ByPath;
	}
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetName().Equals(PathOrName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

UUserDefinedEnum* FMCPTool_EnumStruct::LoadUserDefinedEnum(const FString& PathOrName)
{
	return Cast<UUserDefinedEnum>(FindEnumByPathOrName(PathOrName));
}

UUserDefinedStruct* FMCPTool_EnumStruct::LoadUserDefinedStruct(const FString& PathOrName)
{
	return Cast<UUserDefinedStruct>(FindStructByPathOrName(PathOrName));
}

int32 FMCPTool_EnumStruct::FindEnumValueIndex(UEnum* Enum, const FString& ValueName)
{
	if (!Enum)
	{
		return INDEX_NONE;
	}

	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		const FString Name = Enum->GetNameStringByIndex(i);
		if (Name.Equals(ValueName, ESearchCase::IgnoreCase))
		{
			return i;
		}

		// Short name after the last "::"
		FString ShortName = Name;
		int32 ColonIndex;
		if (ShortName.FindLastChar(TEXT(':'), ColonIndex))
		{
			ShortName = ShortName.RightChop(ColonIndex + 1);
		}
		if (ShortName.Equals(ValueName, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}

	// UserDefinedEnum entries are stored as opaque NewEnumeratorN; allow lookup by display name.
	if (const UUserDefinedEnum* UserEnum = Cast<const UUserDefinedEnum>(Enum))
	{
		for (int32 i = 0; i < UserEnum->NumEnums(); ++i)
		{
			if (UserEnum->GetDisplayNameTextByIndex(i).ToString().Equals(ValueName, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
	}

	return INDEX_NONE;
}

FString FMCPTool_EnumStruct::PropertyTypeString(const FProperty* Property)
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
		return ByteProp->Enum ? ByteProp->Enum->GetName() : TEXT("uint8");
	}
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return StructProp->Struct ? StructProp->Struct->GetName() : TEXT("Struct");
	}
	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		return ObjProp->PropertyClass ? ObjProp->PropertyClass->GetName() : TEXT("Object");
	}
	if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("TArray<%s>"), *PropertyTypeString(ArrayProp->Inner));
	}
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return FString::Printf(TEXT("TSet<%s>"), *PropertyTypeString(SetProp->ElementProp));
	}
	if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return FString::Printf(TEXT("TMap<%s, %s>"),
			*PropertyTypeString(MapProp->KeyProp), *PropertyTypeString(MapProp->ValueProp));
	}

	return Property->GetCPPType();
}

TSharedPtr<FJsonObject> FMCPTool_EnumStruct::EnumInfoJson(UEnum* Enum)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Enum)
	{
		return Obj;
	}

	Obj->SetStringField(TEXT("name"), Enum->GetName());
	Obj->SetStringField(TEXT("path"), Enum->GetPathName());
	Obj->SetBoolField(TEXT("is_user_defined"), Enum->IsA<UUserDefinedEnum>());

	TArray<TSharedPtr<FJsonValue>> Values;
	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		const FString EntryName = Enum->GetNameStringByIndex(i);
		if (EntryName.EndsWith(TEXT("_MAX")))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), EntryName);
		Entry->SetStringField(TEXT("display_name"), Enum->GetDisplayNameTextByIndex(i).ToString());
		Entry->SetNumberField(TEXT("value"), static_cast<double>(Enum->GetValueByIndex(i)));
		Entry->SetNumberField(TEXT("index"), i);
		Values.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Obj->SetArrayField(TEXT("values"), Values);
	Obj->SetNumberField(TEXT("value_count"), Values.Num());
	return Obj;
}

TSharedPtr<FJsonObject> FMCPTool_EnumStruct::StructInfoJson(UScriptStruct* Struct)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Struct)
	{
		return Obj;
	}

	Obj->SetStringField(TEXT("name"), Struct->GetName());
	Obj->SetStringField(TEXT("path"), Struct->GetPathName());
	Obj->SetBoolField(TEXT("is_user_defined"), Struct->IsA<UUserDefinedStruct>());

	TArray<TSharedPtr<FJsonValue>> Properties;
	int32 Index = 0;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Property = *It;
		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		// UserDefinedStruct authored name (drops the _N_<guid> mangling when available).
		PropObj->SetStringField(TEXT("name"), Property->GetAuthoredName());
		PropObj->SetStringField(TEXT("type"), PropertyTypeString(Property));
		PropObj->SetNumberField(TEXT("index"), Index++);
		Properties.Add(MakeShared<FJsonValueObject>(PropObj));
	}
	Obj->SetArrayField(TEXT("properties"), Properties);
	Obj->SetNumberField(TEXT("property_count"), Properties.Num());
	return Obj;
}

// =============================================================================
// Enum operations
// =============================================================================

FMCPToolResult FMCPTool_EnumStruct::OpListEnums(const TSharedRef<FJsonObject>& Params)
{
	const FString Filter = ExtractOptionalString(Params, TEXT("filter"));
	const bool bUserDefinedOnly = ExtractOptionalBool(Params, TEXT("user_defined_only"), false);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		UEnum* Enum = *It;
		if (!Enum)
		{
			continue;
		}

		const FString Name = Enum->GetName();
		const bool bIsUserDefined = Enum->IsA<UUserDefinedEnum>();
		if (bUserDefinedOnly && !bIsUserDefined)
		{
			continue;
		}
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Enum->GetPathName());
		Entry->SetBoolField(TEXT("is_user_defined"), bIsUserDefined);
		Entry->SetNumberField(TEXT("value_count"), FMath::Max(0, Enum->NumEnums() - 1));
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Arr.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		return A->AsObject()->GetStringField(TEXT("name")) < B->AsObject()->GetStringField(TEXT("name"));
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("enums"), Arr);
	Data->SetNumberField(TEXT("count"), Arr.Num());
	return FMCPToolResult::Success(FString::Printf(TEXT("Found %d enum(s)"), Arr.Num()), Data);
}

FMCPToolResult FMCPTool_EnumStruct::OpGetEnum(const TSharedRef<FJsonObject>& Params)
{
	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}

	UEnum* Enum = FindEnumByPathOrName(Name);
	if (!Enum)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Enum '%s' not found"), *Name));
	}
	return FMCPToolResult::Success(FString::Printf(TEXT("Info for enum '%s'"), *Enum->GetName()), EnumInfoJson(Enum));
}

FMCPToolResult FMCPTool_EnumStruct::OpCreateEnum(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}

	// Enforce the conventional E prefix.
	FString FinalName = Name;
	if (!FinalName.StartsWith(TEXT("E")))
	{
		FinalName = TEXT("E") + FinalName;
	}

	FString PackagePath = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game/Enums"));
	if (PackagePath.IsEmpty())
	{
		PackagePath = TEXT("/Game/Enums");
	}
	if (!PackagePath.StartsWith(TEXT("/")))
	{
		PackagePath = TEXT("/Game/") + PackagePath;
	}

	const FString FullAssetPath = PackagePath / FinalName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Enum '%s' already exists at '%s'"), *FinalName, *FullAssetPath));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UEnumFactory* Factory = NewObject<UEnumFactory>();
	UObject* NewAsset = AssetTools.CreateAsset(FinalName, PackagePath, UUserDefinedEnum::StaticClass(), Factory);
	if (!NewAsset)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create enum at %s"), *FullAssetPath));
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	NewAsset->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Created enum: %s"), *NewAsset->GetPathName());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), FinalName);
	Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
	return FMCPToolResult::Success(FString::Printf(TEXT("Created enum '%s'"), *NewAsset->GetPathName()), Data);
#else
	return FMCPToolResult::Error(TEXT("Enum creation requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_EnumStruct::OpDeleteEnum(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}

	UUserDefinedEnum* Enum = LoadUserDefinedEnum(Name);
	if (!Enum)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedEnum '%s' not found"), *Name));
	}

	const FString Path = Enum->GetPathName();
	if (!UEditorAssetLibrary::DeleteAsset(Path))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to delete enum '%s'"), *Path));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Deleted enum: %s"), *Path);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("deleted"), Path);
	return FMCPToolResult::Success(FString::Printf(TEXT("Deleted enum '%s'"), *Path), Data);
#else
	return FMCPToolResult::Error(TEXT("Enum deletion requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_EnumStruct::OpAddEnumValue(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err))
	{
		return Err.GetValue();
	}
	const FString DisplayName = ExtractOptionalString(Params, TEXT("display_name"));

	UUserDefinedEnum* Enum = LoadUserDefinedEnum(Name);
	if (!Enum)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedEnum '%s' not found"), *Name));
	}
	if (FindEnumValueIndex(Enum, Value) != INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Value '%s' already exists in enum '%s'"), *Value, *Name));
	}

	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);

	// The new enumerator sits just before the implicit _MAX entry.
	const int32 NewIndex = Enum->NumEnums() - 2;
	if (NewIndex < 0)
	{
		return FMCPToolResult::Error(TEXT("Failed to add enum value"));
	}

	const FText DisplayText = FText::FromString(DisplayName.IsEmpty() ? Value : DisplayName);
	FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, DisplayText);
	Enum->MarkPackageDirty();

	UE_LOG(LogUnrealClaude, Log, TEXT("Added enum value '%s' to %s"), *Value, *Name);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("enum"), Enum->GetName());
	Data->SetStringField(TEXT("value"), Value);
	Data->SetNumberField(TEXT("index"), NewIndex);
	return FMCPToolResult::Success(FString::Printf(TEXT("Added value '%s' to enum '%s'"), *Value, *Name), Data);
#else
	return FMCPToolResult::Error(TEXT("Enum modification requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_EnumStruct::OpRemoveEnumValue(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err))
	{
		return Err.GetValue();
	}

	UUserDefinedEnum* Enum = LoadUserDefinedEnum(Name);
	if (!Enum)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedEnum '%s' not found"), *Name));
	}

	const int32 Index = FindEnumValueIndex(Enum, Value);
	if (Index == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Value '%s' not found in enum '%s'"), *Value, *Name));
	}

	FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Index);
	Enum->MarkPackageDirty();

	UE_LOG(LogUnrealClaude, Log, TEXT("Removed enum value '%s' from %s"), *Value, *Name);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("enum"), Enum->GetName());
	Data->SetStringField(TEXT("removed"), Value);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed value '%s' from enum '%s'"), *Value, *Name), Data);
#else
	return FMCPToolResult::Error(TEXT("Enum modification requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_EnumStruct::OpSetEnumDisplayName(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name, Value, DisplayName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("display_name"), DisplayName, Err))
	{
		return Err.GetValue();
	}

	UUserDefinedEnum* Enum = LoadUserDefinedEnum(Name);
	if (!Enum)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedEnum '%s' not found"), *Name));
	}

	const int32 Index = FindEnumValueIndex(Enum, Value);
	if (Index == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Value '%s' not found in enum '%s'"), *Value, *Name));
	}

	const FText DisplayText = FText::FromString(DisplayName);
	if (!FEnumEditorUtils::IsEnumeratorDisplayNameValid(Enum, Index, DisplayText))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Display name '%s' is invalid for enum '%s' (duplicate or violates naming rules)"), *DisplayName, *Name));
	}
	if (!FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, DisplayText))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to set display name '%s' for enum '%s'"), *DisplayName, *Name));
	}
	Enum->MarkPackageDirty();

	UE_LOG(LogUnrealClaude, Log, TEXT("Set enum display name '%s' -> '%s' in %s"), *Value, *DisplayName, *Name);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("enum"), Enum->GetName());
	Data->SetStringField(TEXT("value"), Value);
	Data->SetStringField(TEXT("display_name"), DisplayName);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set display name for '%s' to '%s'"), *Value, *DisplayName), Data);
#else
	return FMCPToolResult::Error(TEXT("Enum modification requires an editor build"));
#endif
}

// =============================================================================
// Struct operations
// =============================================================================

FMCPToolResult FMCPTool_EnumStruct::OpListStructs(const TSharedRef<FJsonObject>& Params)
{
	const FString Filter = ExtractOptionalString(Params, TEXT("filter"));
	const bool bUserDefinedOnly = ExtractOptionalBool(Params, TEXT("user_defined_only"), false);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct)
		{
			continue;
		}

		const FString Name = Struct->GetName();
		// Skip transient reinstancing duplicates left behind by struct recompiles.
		if (Name.StartsWith(TEXT("STRUCT_REINST_")))
		{
			continue;
		}

		const bool bIsUserDefined = Struct->IsA<UUserDefinedStruct>();
		if (bUserDefinedOnly && !bIsUserDefined)
		{
			continue;
		}
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Struct->GetPathName());
		Entry->SetBoolField(TEXT("is_user_defined"), bIsUserDefined);

		int32 PropCount = 0;
		for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
		{
			++PropCount;
		}
		Entry->SetNumberField(TEXT("property_count"), PropCount);
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Arr.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		return A->AsObject()->GetStringField(TEXT("name")) < B->AsObject()->GetStringField(TEXT("name"));
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("structs"), Arr);
	Data->SetNumberField(TEXT("count"), Arr.Num());
	return FMCPToolResult::Success(FString::Printf(TEXT("Found %d struct(s)"), Arr.Num()), Data);
}

FMCPToolResult FMCPTool_EnumStruct::OpGetStruct(const TSharedRef<FJsonObject>& Params)
{
	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}

	UScriptStruct* Struct = FindStructByPathOrName(Name);
	if (!Struct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Struct '%s' not found"), *Name));
	}
	return FMCPToolResult::Success(FString::Printf(TEXT("Info for struct '%s'"), *Struct->GetName()), StructInfoJson(Struct));
}

FMCPToolResult FMCPTool_EnumStruct::OpCreateStruct(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}

	// Enforce the conventional F prefix.
	FString FinalName = Name;
	if (!FinalName.StartsWith(TEXT("F")))
	{
		FinalName = TEXT("F") + FinalName;
	}

	FString PackagePath = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game/Structs"));
	if (PackagePath.IsEmpty())
	{
		PackagePath = TEXT("/Game/Structs");
	}
	if (!PackagePath.StartsWith(TEXT("/")))
	{
		PackagePath = TEXT("/Game/") + PackagePath;
	}

	const FString FullAssetPath = PackagePath / FinalName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Struct '%s' already exists at '%s'"), *FinalName, *FullAssetPath));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UStructureFactory* Factory = NewObject<UStructureFactory>();
	UObject* NewAsset = AssetTools.CreateAsset(FinalName, PackagePath, UUserDefinedStruct::StaticClass(), Factory);
	if (!NewAsset)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create struct at %s"), *FullAssetPath));
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	NewAsset->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Created struct: %s"), *NewAsset->GetPathName());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), FinalName);
	Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
	return FMCPToolResult::Success(FString::Printf(TEXT("Created struct '%s'"), *NewAsset->GetPathName()), Data);
#else
	return FMCPToolResult::Error(TEXT("Struct creation requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_EnumStruct::OpDeleteStruct(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}

	UUserDefinedStruct* Struct = LoadUserDefinedStruct(Name);
	if (!Struct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedStruct '%s' not found"), *Name));
	}

	const FString Path = Struct->GetPathName();
	if (!UEditorAssetLibrary::DeleteAsset(Path))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to delete struct '%s'"), *Path));
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("Deleted struct: %s"), *Path);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("deleted"), Path);
	return FMCPToolResult::Success(FString::Printf(TEXT("Deleted struct '%s'"), *Path), Data);
#else
	return FMCPToolResult::Error(TEXT("Struct deletion requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_EnumStruct::OpAddStructProperty(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name, PropertyName, TypeString;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("property"), PropertyName, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("type"), TypeString, Err))
	{
		return Err.GetValue();
	}
	const FString DefaultValue = ExtractOptionalString(Params, TEXT("default"));
	const FString ContainerType = ExtractOptionalString(Params, TEXT("container"));

	UUserDefinedStruct* Struct = LoadUserDefinedStruct(Name);
	if (!Struct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedStruct '%s' not found"), *Name));
	}
	if (FindPropertyGuid(Struct, PropertyName).IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Property '%s' already exists in struct '%s'"), *PropertyName, *Name));
	}

	// The structure factory seeds every new UserDefinedStruct with a placeholder bool
	// (MemberVar_0). Capture it so it can be dropped once the real property is added.
	FGuid PlaceholderGuid;
	{
		const TArray<FStructVariableDescription>& ExistingVars = FStructureEditorUtils::GetVarDesc(Struct);
		if (ExistingVars.Num() == 1 &&
			ExistingVars[0].FriendlyName.StartsWith(TEXT("MemberVar_")) &&
			ExistingVars[0].Category == UEdGraphSchema_K2::PC_Boolean &&
			!PropertyName.StartsWith(TEXT("MemberVar_")))
		{
			PlaceholderGuid = ExistingVars[0].VarGuid;
		}
	}

	FEdGraphPinType PinType;
	FString TypeError;
	if (!ResolvePinType(TypeString, ContainerType, PinType, TypeError))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Invalid type '%s': %s"), *TypeString, *TypeError));
	}

	if (!FStructureEditorUtils::AddVariable(Struct, PinType))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add property '%s'"), *PropertyName));
	}

	FGuid NewVarGuid;
	const TArray<FStructVariableDescription>& VarDescriptions = FStructureEditorUtils::GetVarDesc(Struct);
	if (VarDescriptions.Num() > 0)
	{
		NewVarGuid = VarDescriptions.Last().VarGuid;
	}
	if (!NewVarGuid.IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to resolve GUID for new property '%s'"), *PropertyName));
	}

	if (!FStructureEditorUtils::RenameVariable(Struct, NewVarGuid, PropertyName))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to rename struct property to '%s'"), *PropertyName);
	}
	if (!DefaultValue.IsEmpty())
	{
		FStructureEditorUtils::ChangeVariableDefaultValue(Struct, NewVarGuid, DefaultValue);
	}
	if (PlaceholderGuid.IsValid())
	{
		FStructureEditorUtils::RemoveVariable(Struct, PlaceholderGuid);
	}

	Struct->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Added struct property '%s' (%s) to %s"), *PropertyName, *TypeString, *Name);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("struct"), Struct->GetName());
	Data->SetStringField(TEXT("property"), PropertyName);
	Data->SetStringField(TEXT("type"), TypeString);
	if (!ContainerType.IsEmpty())
	{
		Data->SetStringField(TEXT("container"), ContainerType);
	}
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added property '%s' of type '%s' to struct '%s'"), *PropertyName, *TypeString, *Name), Data);
#else
	return FMCPToolResult::Error(TEXT("Struct modification requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_EnumStruct::OpRemoveStructProperty(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString Name, PropertyName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("name"), Name, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("property"), PropertyName, Err))
	{
		return Err.GetValue();
	}

	UUserDefinedStruct* Struct = LoadUserDefinedStruct(Name);
	if (!Struct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedStruct '%s' not found"), *Name));
	}

	const FGuid PropertyGuid = FindPropertyGuid(Struct, PropertyName);
	if (!PropertyGuid.IsValid())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Property '%s' not found in struct '%s'"), *PropertyName, *Name));
	}

	if (!FStructureEditorUtils::RemoveVariable(Struct, PropertyGuid))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to remove property '%s'"), *PropertyName));
	}

	Struct->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Removed struct property '%s' from %s"), *PropertyName, *Name);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("struct"), Struct->GetName());
	Data->SetStringField(TEXT("removed"), PropertyName);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed property '%s' from struct '%s'"), *PropertyName, *Name), Data);
#else
	return FMCPToolResult::Error(TEXT("Struct modification requires an editor build"));
#endif
}

// =============================================================================
// Editor-only helpers
// =============================================================================

#if WITH_EDITOR
FGuid FMCPTool_EnumStruct::FindPropertyGuid(UUserDefinedStruct* Struct, const FString& PropertyName)
{
	if (!Struct)
	{
		return FGuid();
	}

	const TArray<FStructVariableDescription>& VarDescriptions = FStructureEditorUtils::GetVarDesc(Struct);
	for (const FStructVariableDescription& Desc : VarDescriptions)
	{
		if (Desc.FriendlyName.Equals(PropertyName, ESearchCase::IgnoreCase) ||
			Desc.VarName.ToString().Equals(PropertyName, ESearchCase::IgnoreCase))
		{
			return Desc.VarGuid;
		}
	}
	return FGuid();
}

bool FMCPTool_EnumStruct::ResolvePinType(const FString& TypeString, const FString& ContainerType,
	FEdGraphPinType& OutPinType, FString& OutError)
{
	const FString Type = TypeString.TrimStartAndEnd();
	if (Type.IsEmpty())
	{
		OutError = TEXT("empty type string");
		return false;
	}

	OutPinType = FEdGraphPinType();
	bool bResolved = false;

	// Primitive types map directly onto K2 pin categories.
	if (Type.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("byte"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("uint8"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("int32"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("float"), ESearchCase::IgnoreCase))
	{
		// Post-LWC: reals are PC_Real with a float/double subcategory.
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("double"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("name"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("FName"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("string"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		bResolved = true;
	}
	else if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("FText"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		bResolved = true;
	}

	// Named enum (accepts user-defined and native enums by name/path).
	if (!bResolved)
	{
		if (UEnum* Enum = FindEnumByPathOrName(Type))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutPinType.PinSubCategoryObject = Enum;
			bResolved = true;
		}
	}

	// Named struct (e.g. FVector, FRotator, a UserDefinedStruct).
	if (!bResolved)
	{
		if (UScriptStruct* Struct = FindStructByPathOrName(Type))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = Struct;
			bResolved = true;
		}
	}

	// Named object/actor class -> object reference pin.
	if (!bResolved)
	{
		if (UClass* Class = FindObject<UClass>(nullptr, *Type))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = Class;
			bResolved = true;
		}
	}

	if (!bResolved)
	{
		OutError = TEXT("unrecognized type — use bool/byte/int32/int64/float/double/name/string/text, or an enum/struct/class name");
		return false;
	}

	// Apply container type to the resolved value type.
	if (!ContainerType.IsEmpty())
	{
		if (ContainerType.Equals(TEXT("Array"), ESearchCase::IgnoreCase))
		{
			OutPinType.ContainerType = EPinContainerType::Array;
		}
		else if (ContainerType.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
		{
			OutPinType.ContainerType = EPinContainerType::Set;
		}
		else if (ContainerType.Equals(TEXT("Map"), ESearchCase::IgnoreCase))
		{
			// The resolved type becomes the map's value type; FStructureEditorUtils
			// seeds a default (string) key, editable later in the struct editor.
			OutPinType.ContainerType = EPinContainerType::Map;
		}
		else
		{
			OutError = FString::Printf(TEXT("unknown container '%s' — use Array, Set, or Map"), *ContainerType);
			return false;
		}
	}

	return true;
}
#endif
