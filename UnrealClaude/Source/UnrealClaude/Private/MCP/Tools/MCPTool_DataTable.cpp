// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_DataTable.h"
#include "UnrealClaudeModule.h"

#include "Engine/DataTable.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/SoftObjectPtr.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/DataTableFactory.h"
#endif

// ============================================================================
// Tool info / dispatch
// ============================================================================

FMCPToolInfo FMCPTool_DataTable::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("data_table");
	Info.Description = TEXT(
		"General-purpose Unreal Engine DataTable management for any row struct.\n\n"
		"Operations (set 'operation'):\n"
		"- 'list_tables': List DataTable assets; optional 'filter' (path/name substring) and 'row_struct' filter\n"
		"- 'query_rows': All rows of the table at 'table_path' as JSON (keyed by row name)\n"
		"- 'get_row': Single row 'row_name' from 'table_path' as JSON\n"
		"- 'create_table': Create a DataTable named 'asset_name' in 'package_path' using row struct 'row_struct' (editor only)\n"
		"- 'add_row': Add 'row_name' to 'table_path' with field values from 'data' (object) (editor only)\n"
		"- 'update_row': Partially update 'row_name' in 'table_path' from 'data' (object) (editor only)\n"
		"- 'remove_row': Delete 'row_name' from 'table_path' (editor only)\n\n"
		"Row values are JSON objects keyed by the row struct's authored property names. Supported "
		"field types: numbers, bool, string, name, enum (by name), nested struct, and arrays. "
		"'row_struct' may be a full struct path (e.g. '/Script/MyGame.ItemRow' or a UserDefinedStruct "
		"asset path) or a bare name (e.g. 'ItemRow').\n\n"
		"Returns: operation-specific data (table list, row data, or the modified row name)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"),
			TEXT("One of: list_tables, query_rows, get_row, create_table, add_row, update_row, remove_row"), true),
		FMCPToolParameter(TEXT("table_path"), TEXT("string"),
			TEXT("Object path to the DataTable (e.g. '/Game/Data/DT_Items')"), false),
		FMCPToolParameter(TEXT("row_name"), TEXT("string"),
			TEXT("Row name for get_row/add_row/update_row/remove_row"), false),
		FMCPToolParameter(TEXT("data"), TEXT("object"),
			TEXT("For add_row/update_row: JSON object of field values keyed by property name"), false),
		FMCPToolParameter(TEXT("row_struct"), TEXT("string"),
			TEXT("For create_table: row struct path or name. For list_tables: optional row-struct filter"), false),
		FMCPToolParameter(TEXT("package_path"), TEXT("string"),
			TEXT("For create_table: destination package path (default: '/Game/Data')"), false, TEXT("/Game/Data")),
		FMCPToolParameter(TEXT("asset_name"), TEXT("string"),
			TEXT("For create_table: new asset name (e.g. 'DT_Items')"), false),
		FMCPToolParameter(TEXT("filter"), TEXT("string"),
			TEXT("For list_tables: optional path/name substring filter"), false),
		FMCPToolParameter(TEXT("include_columns"), TEXT("boolean"),
			TEXT("For query_rows/get_row: also return the row struct's column metadata (default: false)"), false, TEXT("false"))
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_DataTable::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("list_tables")) { return OpListTables(Params); }
	if (Operation == TEXT("query_rows"))  { return OpQueryRows(Params); }
	if (Operation == TEXT("get_row"))     { return OpGetRow(Params); }
	if (Operation == TEXT("create_table")){ return OpCreateTable(Params); }
	if (Operation == TEXT("add_row"))     { return OpAddRow(Params); }
	if (Operation == TEXT("update_row"))  { return OpUpdateRow(Params); }
	if (Operation == TEXT("remove_row"))  { return OpRemoveRow(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: list_tables, query_rows, get_row, create_table, add_row, update_row, remove_row"),
		*Operation));
}

// ============================================================================
// Query operations
// ============================================================================

FMCPToolResult FMCPTool_DataTable::OpListTables(const TSharedRef<FJsonObject>& Params)
{
	const FString Filter = ExtractOptionalString(Params, TEXT("filter"));
	const FString RowStructFilter = ExtractOptionalString(Params, TEXT("row_struct"));

	const FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter ARFilter;
	ARFilter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
	ARFilter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(ARFilter, Assets);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& Asset : Assets)
	{
		const FString ObjectPath = Asset.GetObjectPathString();
		const FString AssetName = Asset.AssetName.ToString();

		if (!Filter.IsEmpty()
			&& !ObjectPath.Contains(Filter, ESearchCase::IgnoreCase)
			&& !AssetName.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		// Row struct comes from the asset registry tag without loading the asset.
		FString RowStructPath;
		Asset.GetTagValue(TEXT("RowStructName"), RowStructPath);
		FString RowStructName = RowStructPath;
		int32 LastDot;
		if (RowStructName.FindLastChar(TEXT('.'), LastDot))
		{
			RowStructName = RowStructName.Mid(LastDot + 1);
		}

		if (!RowStructFilter.IsEmpty()
			&& !RowStructName.Contains(RowStructFilter, ESearchCase::IgnoreCase)
			&& !RowStructPath.Contains(RowStructFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), AssetName);
		Obj->SetStringField(TEXT("path"), ObjectPath);
		Obj->SetStringField(TEXT("row_struct"), RowStructName);
		if (!RowStructPath.IsEmpty())
		{
			Obj->SetStringField(TEXT("row_struct_path"), RowStructPath);
		}
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Arr.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		return A->AsObject()->GetStringField(TEXT("name")) < B->AsObject()->GetStringField(TEXT("name"));
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tables"), Arr);
	Data->SetNumberField(TEXT("count"), Arr.Num());
	return FMCPToolResult::Success(FString::Printf(TEXT("Found %d DataTable(s)"), Arr.Num()), Data);
}

FMCPToolResult FMCPTool_DataTable::OpQueryRows(const TSharedRef<FJsonObject>& Params)
{
	FString TablePath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("table_path"), TablePath, Err))
	{
		return Err.GetValue();
	}

	UDataTable* Table = LoadTable(TablePath);
	if (!Table)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable '%s' has no row struct"), *TablePath));
	}

	TSharedPtr<FJsonObject> RowsObj = MakeShared<FJsonObject>();
	const TArray<FName> RowNames = Table->GetRowNames();
	for (const FName& RowName : RowNames)
	{
		const void* RowData = Table->FindRowUnchecked(RowName);
		if (RowData)
		{
			RowsObj->SetObjectField(RowName.ToString(), RowToJson(RowStruct, RowData));
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("table_path"), TablePath);
	Data->SetStringField(TEXT("row_struct"), RowStruct->GetName());
	Data->SetNumberField(TEXT("count"), RowNames.Num());
	Data->SetObjectField(TEXT("rows"), RowsObj);
	if (ExtractOptionalBool(Params, TEXT("include_columns")))
	{
		Data->SetArrayField(TEXT("columns"), ColumnsToJson(RowStruct));
	}

	return FMCPToolResult::Success(
		FString::Printf(TEXT("DataTable '%s' has %d row(s)"), *Table->GetName(), RowNames.Num()), Data);
}

FMCPToolResult FMCPTool_DataTable::OpGetRow(const TSharedRef<FJsonObject>& Params)
{
	FString TablePath, RowName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("table_path"), TablePath, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("row_name"), RowName, Err))
	{
		return Err.GetValue();
	}

	UDataTable* Table = LoadTable(TablePath);
	if (!Table)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	const void* RowData = Table->FindRowUnchecked(FName(*RowName));
	if (!RowStruct || !RowData)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' not found in '%s'"), *RowName, *TablePath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("table_path"), TablePath);
	Data->SetStringField(TEXT("row_name"), RowName);
	Data->SetObjectField(TEXT("row"), RowToJson(RowStruct, RowData));
	if (ExtractOptionalBool(Params, TEXT("include_columns")))
	{
		Data->SetArrayField(TEXT("columns"), ColumnsToJson(RowStruct));
	}

	return FMCPToolResult::Success(FString::Printf(TEXT("Row '%s' from '%s'"), *RowName, *Table->GetName()), Data);
}

// ============================================================================
// Mutating operations (editor only)
// ============================================================================

FMCPToolResult FMCPTool_DataTable::OpCreateTable(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString RowStructName, AssetName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("row_struct"), RowStructName, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("asset_name"), AssetName, Err))
	{
		return Err.GetValue();
	}

	FString PackagePath = ExtractOptionalString(Params, TEXT("package_path"), TEXT("/Game/Data"));
	if (PackagePath.IsEmpty())
	{
		PackagePath = TEXT("/Game/Data");
	}
	if (!PackagePath.StartsWith(TEXT("/Game")) && !PackagePath.StartsWith(TEXT("/Engine")))
	{
		PackagePath = PackagePath.StartsWith(TEXT("/")) ? (TEXT("/Game") + PackagePath) : (TEXT("/Game/") + PackagePath);
	}

	UScriptStruct* RowStruct = ResolveRowStruct(RowStructName);
	if (!RowStruct)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Row struct not found: '%s'. Provide a full struct path or an exact name."), *RowStructName));
	}
	if (!IsUsableRowStruct(RowStruct))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Struct '%s' cannot back a DataTable (must derive from FTableRowBase or be a UserDefinedStruct)"),
			*RowStruct->GetName()));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = RowStruct;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UDataTable::StaticClass(), Factory);
	UDataTable* Table = Cast<UDataTable>(NewAsset);
	if (!Table)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to create DataTable '%s' at '%s' (does it already exist?)"), *AssetName, *PackagePath));
	}

	Table->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Created DataTable %s (row struct: %s)"),
		*Table->GetPathName(), *RowStruct->GetName());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("table_path"), Table->GetPathName());
	Data->SetStringField(TEXT("row_struct"), RowStruct->GetName());
	Data->SetArrayField(TEXT("columns"), ColumnsToJson(RowStruct));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created DataTable '%s' with row struct '%s'"), *AssetName, *RowStruct->GetName()), Data);
#else
	return FMCPToolResult::Error(TEXT("DataTable creation requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_DataTable::OpAddRow(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString TablePath, RowName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("table_path"), TablePath, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("row_name"), RowName, Err))
	{
		return Err.GetValue();
	}

	UDataTable* Table = LoadTable(TablePath);
	if (!Table)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable '%s' has no row struct"), *TablePath));
	}
	if (Table->FindRowUnchecked(FName(*RowName)))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Row '%s' already exists. Use update_row to modify it."), *RowName));
	}

	// Allocate a temporary row instance, fill from JSON, hand it to the table, then free.
	void* NewRowData = FMemory::Malloc(RowStruct->GetStructureSize());
	RowStruct->InitializeStruct(NewRowData);

	FString ApplyError;
	const TSharedPtr<FJsonObject>* DataObj = nullptr;
	if (Params->TryGetObjectField(TEXT("data"), DataObj) && DataObj && (*DataObj).IsValid())
	{
		JsonToRow(RowStruct, NewRowData, *DataObj, ApplyError);
	}

	Table->AddRow(FName(*RowName), *static_cast<FTableRowBase*>(NewRowData));

	RowStruct->DestroyStruct(NewRowData);
	FMemory::Free(NewRowData);

	Table->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Added row '%s' to DataTable %s"), *RowName, *Table->GetName());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("table_path"), TablePath);
	Data->SetStringField(TEXT("row_name"), RowName);
	if (const void* Written = Table->FindRowUnchecked(FName(*RowName)))
	{
		Data->SetObjectField(TEXT("row"), RowToJson(RowStruct, Written));
	}

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Added row '%s' to '%s'"), *RowName, *Table->GetName()), Data);
	if (!ApplyError.IsEmpty())
	{
		Result.Warnings.Add(FString::Printf(TEXT("Some field(s) were not applied: %s"), *ApplyError));
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("Adding DataTable rows requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_DataTable::OpUpdateRow(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString TablePath, RowName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("table_path"), TablePath, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("row_name"), RowName, Err))
	{
		return Err.GetValue();
	}

	UDataTable* Table = LoadTable(TablePath);
	if (!Table)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	void* RowData = Table->FindRowUnchecked(FName(*RowName));
	if (!RowStruct || !RowData)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' not found in '%s'"), *RowName, *TablePath));
	}

	const TSharedPtr<FJsonObject>* DataObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("data"), DataObj) || !DataObj || !(*DataObj).IsValid())
	{
		return FMCPToolResult::Error(TEXT("update_row requires a 'data' object of field values"));
	}

	FString ApplyError;
	const bool bAllApplied = JsonToRow(RowStruct, RowData, *DataObj, ApplyError);

	Table->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Updated row '%s' in DataTable %s"), *RowName, *Table->GetName());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("table_path"), TablePath);
	Data->SetStringField(TEXT("row_name"), RowName);
	Data->SetObjectField(TEXT("row"), RowToJson(RowStruct, RowData));

	FMCPToolResult Result = FMCPToolResult::Success(
		FString::Printf(TEXT("Updated row '%s' in '%s'"), *RowName, *Table->GetName()), Data);
	if (!bAllApplied && !ApplyError.IsEmpty())
	{
		Result.Warnings.Add(FString::Printf(TEXT("Some field(s) were not applied: %s"), *ApplyError));
	}
	return Result;
#else
	return FMCPToolResult::Error(TEXT("Updating DataTable rows requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_DataTable::OpRemoveRow(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString TablePath, RowName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("table_path"), TablePath, Err))
	{
		return Err.GetValue();
	}
	if (!ExtractRequiredString(Params, TEXT("row_name"), RowName, Err))
	{
		return Err.GetValue();
	}

	UDataTable* Table = LoadTable(TablePath);
	if (!Table)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}
	if (!Table->FindRowUnchecked(FName(*RowName)))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' not found in '%s'"), *RowName, *TablePath));
	}

	Table->RemoveRow(FName(*RowName));
	Table->MarkPackageDirty();
	UE_LOG(LogUnrealClaude, Log, TEXT("Removed row '%s' from DataTable %s"), *RowName, *Table->GetName());

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("table_path"), TablePath);
	Data->SetStringField(TEXT("removed"), RowName);
	return FMCPToolResult::Success(FString::Printf(TEXT("Removed row '%s' from '%s'"), *RowName, *Table->GetName()), Data);
#else
	return FMCPToolResult::Error(TEXT("Removing DataTable rows requires an editor build"));
#endif
}

// ============================================================================
// Helpers
// ============================================================================

UDataTable* FMCPTool_DataTable::LoadTable(const FString& TablePath)
{
	if (TablePath.IsEmpty())
	{
		return nullptr;
	}
	return LoadObject<UDataTable>(nullptr, *TablePath);
}

bool FMCPTool_DataTable::IsUsableRowStruct(const UScriptStruct* Struct)
{
	if (!Struct || Struct->GetName().StartsWith(TEXT("STRUCT_REINST_")))
	{
		return false;
	}
	// UserDefinedStructs can never derive from FTableRowBase but the editor's own
	// DataTable factory accepts them. Match by class name to stay version-agnostic.
	return Struct->IsChildOf(FTableRowBase::StaticStruct())
		|| Struct->GetClass()->GetFName() == FName(TEXT("UserDefinedStruct"));
}

UScriptStruct* FMCPTool_DataTable::ResolveRowStruct(const FString& StructNameOrPath)
{
	if (StructNameOrPath.IsEmpty())
	{
		return nullptr;
	}

	// 1) Direct find / load by path.
	if (UScriptStruct* Found = FindObject<UScriptStruct>(nullptr, *StructNameOrPath))
	{
		return Found;
	}
	if (UScriptStruct* Loaded = LoadObject<UScriptStruct>(nullptr, *StructNameOrPath))
	{
		return Loaded;
	}

	// 2) Search by name (with and without an 'F' prefix) over usable row structs.
	const FString NameWithF = TEXT("F") + StructNameOrPath;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!IsUsableRowStruct(Struct))
		{
			continue;
		}
		if (Struct->GetName().Equals(StructNameOrPath, ESearchCase::IgnoreCase)
			|| Struct->GetName().Equals(NameWithF, ESearchCase::IgnoreCase))
		{
			return Struct;
		}
	}

	return nullptr;
}

FProperty* FMCPTool_DataTable::ResolveStructProperty(const UScriptStruct* Struct, const FString& Key)
{
	if (!Struct)
	{
		return nullptr;
	}
	if (FProperty* Direct = Struct->FindPropertyByName(*Key))
	{
		return Direct;
	}
	// UserDefinedStruct properties have mangled names (Damage_2_<guid>); match the authored name.
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (It->GetAuthoredName().Equals(Key, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

TArray<TSharedPtr<FJsonValue>> FMCPTool_DataTable::ColumnsToJson(const UScriptStruct* RowStruct)
{
	TArray<TSharedPtr<FJsonValue>> Columns;
	if (!RowStruct)
	{
		return Columns;
	}
	for (TFieldIterator<FProperty> It(RowStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		TSharedPtr<FJsonObject> Col = MakeShared<FJsonObject>();
		Col->SetStringField(TEXT("name"), Property->GetAuthoredName());
		Col->SetStringField(TEXT("type"), Property->GetClass()->GetName());
		Col->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		Columns.Add(MakeShared<FJsonValueObject>(Col));
	}
	return Columns;
}

// ----- Row <-> JSON -----

TSharedPtr<FJsonObject> FMCPTool_DataTable::RowToJson(const UScriptStruct* RowStruct, const void* RowData)
{
	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	if (!RowStruct || !RowData)
	{
		return JsonObj;
	}
	for (TFieldIterator<FProperty> It(RowStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		// Authored name so UserDefinedStruct columns round-trip with editor-facing names.
		JsonObj->SetField(Property->GetAuthoredName(), PropertyToJson(Property, RowData));
	}
	return JsonObj;
}

bool FMCPTool_DataTable::JsonToRow(const UScriptStruct* RowStruct, void* RowData,
	const TSharedPtr<FJsonObject>& JsonObj, FString& OutError)
{
	if (!RowStruct || !RowData || !JsonObj.IsValid())
	{
		OutError = TEXT("Invalid parameters");
		return false;
	}

	bool bAllSuccess = true;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonObj->Values)
	{
		FProperty* Property = ResolveStructProperty(RowStruct, Pair.Key);
		if (!Property)
		{
			if (bAllSuccess)
			{
				OutError = FString::Printf(TEXT("Property '%s' not found"), *Pair.Key);
				bAllSuccess = false;
			}
			continue;
		}

		FString PropertyError;
		if (!JsonToProperty(Property, RowData, Pair.Value, PropertyError))
		{
			if (bAllSuccess)
			{
				OutError = FString::Printf(TEXT("Failed to set '%s': %s"), *Pair.Key, *PropertyError);
				bAllSuccess = false;
			}
		}
	}
	return bAllSuccess;
}

// ----- Property <-> JSON (reflection) -----

TSharedPtr<FJsonValue> FMCPTool_DataTable::PropertyToJson(FProperty* Property, const void* Container)
{
	if (!Property || !Container)
	{
		return MakeShared<FJsonValueNull>();
	}
	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
	return ValuePtrToJson(Property, ValuePtr);
}

TSharedPtr<FJsonValue> FMCPTool_DataTable::ValuePtrToJson(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	// Enum-backed bytes first: FByteProperty is an FNumericProperty and would otherwise
	// export the raw integer instead of the enum name.
	if (FByteProperty* ByteEnumProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteEnumProp->Enum)
		{
			const uint8 Value = ByteEnumProp->GetPropertyValue(ValuePtr);
			return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(Value));
		}
	}
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty();
		const int64 Value = Underlying->GetSignedIntPropertyValue(ValuePtr);
		return MakeShared<FJsonValueString>(EnumProp->GetEnum()->GetNameStringByValue(Value));
	}

	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		if (NumericProp->IsFloatingPoint())
		{
			return MakeShared<FJsonValueNumber>(NumericProp->GetFloatingPointPropertyValue(ValuePtr));
		}
		return MakeShared<FJsonValueNumber>(static_cast<double>(NumericProp->GetSignedIntPropertyValue(ValuePtr)));
	}
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
	}
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
	}
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
	}
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
	}
	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
		return MakeShared<FJsonValueString>(SoftPtr->ToString());
	}
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		const UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
		return MakeShared<FJsonValueString>(Obj ? Obj->GetPathName() : FString());
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		for (int32 i = 0; i < ArrayHelper.Num(); ++i)
		{
			JsonArray.Add(ValuePtrToJson(ArrayProp->Inner, ArrayHelper.GetRawPtr(i)));
		}
		return MakeShared<FJsonValueArray>(JsonArray);
	}
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		TSharedPtr<FJsonObject> StructObj = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
		{
			FProperty* InnerProp = *It;
			StructObj->SetField(InnerProp->GetAuthoredName(), PropertyToJson(InnerProp, ValuePtr));
		}
		return MakeShared<FJsonValueObject>(StructObj);
	}

	// Fallback: export as text.
	FString ExportedText;
	Property->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(ExportedText);
}

bool FMCPTool_DataTable::JsonToProperty(FProperty* Property, void* Container,
	const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Property || !Container || !Value.IsValid())
	{
		OutError = TEXT("Invalid parameters");
		return false;
	}
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
	return JsonToValuePtr(Property, ValuePtr, Value, OutError);
}

bool FMCPTool_DataTable::JsonToValuePtr(FProperty* Property, void* ValuePtr,
	const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Property || !ValuePtr || !Value.IsValid())
	{
		OutError = TEXT("Invalid parameters");
		return false;
	}

	// Enum-backed byte: accept the enum name string (numeric falls through below).
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProp->Enum)
		{
			FString EnumStr;
			if (Value->TryGetString(EnumStr))
			{
				const int64 EnumValue = Enum->GetValueByNameString(EnumStr);
				if (EnumValue == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Invalid enum value: %s"), *EnumStr);
					return false;
				}
				ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(EnumValue));
				return true;
			}
		}
	}
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString EnumStr;
		if (Value->TryGetString(EnumStr))
		{
			const int64 EnumValue = EnumProp->GetEnum()->GetValueByNameString(EnumStr);
			if (EnumValue == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Invalid enum value: %s"), *EnumStr);
				return false;
			}
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
			return true;
		}
	}

	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		double NumValue;
		if (!Value->TryGetNumber(NumValue))
		{
			OutError = TEXT("Expected numeric value");
			return false;
		}
		if (NumericProp->IsFloatingPoint())
		{
			NumericProp->SetFloatingPointPropertyValue(ValuePtr, NumValue);
		}
		else
		{
			NumericProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(NumValue));
		}
		return true;
	}
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		BoolProp->SetPropertyValue(ValuePtr, Value->AsBool());
		return true;
	}
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		StrProp->SetPropertyValue(ValuePtr, Value->AsString());
		return true;
	}
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		NameProp->SetPropertyValue(ValuePtr, FName(*Value->AsString()));
		return true;
	}
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		TextProp->SetPropertyValue(ValuePtr, FText::FromString(Value->AsString()));
		return true;
	}
	// FSoftObjectProperty derives from FObjectPropertyBase, so this one check covers
	// hard and soft references alike. Object references are set by path via ImportText.
	if (CastField<FObjectPropertyBase>(Property))
	{
		const FString PathStr = Value->AsString();
		Property->ImportText_Direct(*PathStr, ValuePtr, nullptr, PPF_None);
		return true;
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
		if (!Value->TryGetArray(JsonArray) || !JsonArray)
		{
			OutError = TEXT("Expected array value");
			return false;
		}
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		ArrayHelper.Resize(JsonArray->Num());
		bool bOk = true;
		for (int32 i = 0; i < JsonArray->Num(); ++i)
		{
			FString ElemError;
			if (!JsonToValuePtr(ArrayProp->Inner, ArrayHelper.GetRawPtr(i), (*JsonArray)[i], ElemError))
			{
				bOk = false;
				OutError = ElemError;
			}
		}
		return bOk;
	}
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		const TSharedPtr<FJsonObject>* StructObj = nullptr;
		if (!Value->TryGetObject(StructObj) || !StructObj || !(*StructObj).IsValid())
		{
			OutError = TEXT("Expected object value for struct");
			return false;
		}
		bool bOk = true;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StructObj)->Values)
		{
			FProperty* InnerProp = ResolveStructProperty(StructProp->Struct, Pair.Key);
			if (!InnerProp)
			{
				continue;
			}
			FString InnerError;
			if (!JsonToProperty(InnerProp, ValuePtr, Pair.Value, InnerError))
			{
				bOk = false;
				OutError = InnerError;
			}
		}
		return bOk;
	}

	// Fallback: import from text.
	const FString TextValue = Value->AsString();
	if (Property->ImportText_Direct(*TextValue, ValuePtr, nullptr, PPF_None) == nullptr)
	{
		OutError = FString::Printf(TEXT("Unsupported property type: %s"), *Property->GetClass()->GetName());
		return false;
	}
	return true;
}
