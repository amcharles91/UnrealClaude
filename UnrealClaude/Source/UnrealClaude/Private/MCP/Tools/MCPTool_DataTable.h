// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class UDataTable;
class UScriptStruct;
class FProperty;
class FJsonValue;

/**
 * MCP Tool: General-purpose Unreal Engine DataTable management.
 *
 * Works with ANY row struct (a USTRUCT deriving from FTableRowBase, or a
 * UserDefinedStruct authored in the editor). Query operations (list / query_rows
 * / get_row) are available in all builds; mutating operations (create_table /
 * add_row / update_row / remove_row) require an editor build.
 *
 * Row values are exchanged as JSON objects keyed by the struct's authored
 * property names, serialized via UE reflection (FProperty), so the tool needs no
 * compile-time knowledge of the row layout.
 *
 * Ported from VibeUE's UDataTableService into the native MCP tool pattern. This
 * is intentionally general-purpose and does NOT duplicate the character-specific
 * stats/config behavior of MCPTool_CharacterData.
 */
class FMCPTool_DataTable : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// Query operations (available in all builds).
	FMCPToolResult OpListTables(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpQueryRows(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetRow(const TSharedRef<FJsonObject>& Params);

	// Mutating operations (editor builds only — see #if WITH_EDITOR in the .cpp).
	FMCPToolResult OpCreateTable(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddRow(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpUpdateRow(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveRow(const TSharedRef<FJsonObject>& Params);

	// ===== Helpers =====

	/** Load a DataTable by object path (e.g. "/Game/Data/DT_Items"); nullptr if not found. */
	static UDataTable* LoadTable(const FString& TablePath);

	/** Resolve a row struct by full path first, then by (optionally F-prefixed) name. */
	static UScriptStruct* ResolveRowStruct(const FString& StructNameOrPath);

	/** A struct is usable as a row struct if it derives from FTableRowBase or is a UserDefinedStruct. */
	static bool IsUsableRowStruct(const UScriptStruct* Struct);

	/** Resolve a JSON key to a struct property (direct name, then authored-name, case-insensitive). */
	static FProperty* ResolveStructProperty(const UScriptStruct* Struct, const FString& Key);

	/** Serialize one struct instance to a JSON object keyed by authored property names. */
	static TSharedPtr<FJsonObject> RowToJson(const UScriptStruct* RowStruct, const void* RowData);

	/** Apply a JSON object onto a struct instance (partial update). Returns false on first error. */
	static bool JsonToRow(const UScriptStruct* RowStruct, void* RowData,
		const TSharedPtr<FJsonObject>& JsonObj, FString& OutError);

	/** Reflection-based single-property serialization. */
	static TSharedPtr<FJsonValue> PropertyToJson(FProperty* Property, const void* Container);
	static TSharedPtr<FJsonValue> ValuePtrToJson(FProperty* Property, const void* ValuePtr);
	static bool JsonToProperty(FProperty* Property, void* Container,
		const TSharedPtr<FJsonValue>& Value, FString& OutError);
	static bool JsonToValuePtr(FProperty* Property, void* ValuePtr,
		const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** Build a one-line JSON object describing a row struct's columns (name + type). */
	static TArray<TSharedPtr<FJsonValue>> ColumnsToJson(const UScriptStruct* RowStruct);
};
