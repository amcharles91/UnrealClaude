// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class UEnum;
class UScriptStruct;
class UUserDefinedEnum;
class UUserDefinedStruct;
struct FEdGraphPinType;

/**
 * MCP Tool: Create and manage user-defined Enums and Structs.
 *
 * Read ops (list_enums / get_enum / list_structs / get_struct) inspect any UEnum /
 * UScriptStruct (native or user-defined). Editor mutations operate on UUserDefinedEnum
 * via FEnumEditorUtils and UUserDefinedStruct via FStructureEditorUtils, with new assets
 * created through UEnumFactory / UStructureFactory and IAssetTools::CreateAsset.
 *
 * Ported from VibeUE's UEnumStructService into the native MCP tool pattern. Only the
 * underlying UE editor-API usage is adapted; none of VibeUE's UCLASS/USTRUCT/Python
 * framework (or its FBlueprintTypeParser) is carried over — type resolution is inlined.
 */
class FMCPTool_EnumStruct : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// ===== Enum operations =====
	FMCPToolResult OpListEnums(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetEnum(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpCreateEnum(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpDeleteEnum(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddEnumValue(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveEnumValue(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetEnumDisplayName(const TSharedRef<FJsonObject>& Params);

	// ===== Struct operations =====
	FMCPToolResult OpListStructs(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetStruct(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpCreateStruct(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpDeleteStruct(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpAddStructProperty(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveStructProperty(const TSharedRef<FJsonObject>& Params);

	// ===== Lookup helpers (path or name) =====
	static UEnum* FindEnumByPathOrName(const FString& PathOrName);
	static UScriptStruct* FindStructByPathOrName(const FString& PathOrName);
	static UUserDefinedEnum* LoadUserDefinedEnum(const FString& PathOrName);
	static UUserDefinedStruct* LoadUserDefinedStruct(const FString& PathOrName);

	/** Locate an enumerator index by internal short name or display name (case-insensitive). */
	static int32 FindEnumValueIndex(UEnum* Enum, const FString& ValueName);

	/** Friendly string for a property's type (e.g. "float", "FVector", "TArray<int32>"). */
	static FString PropertyTypeString(const FProperty* Property);

	/** Build a JSON object describing an enum (values, display names, numeric values). */
	static TSharedPtr<FJsonObject> EnumInfoJson(UEnum* Enum);

	/** Build a JSON object describing a struct (properties with friendly type strings). */
	static TSharedPtr<FJsonObject> StructInfoJson(UScriptStruct* Struct);

#if WITH_EDITOR
	/** GUID of a UserDefinedStruct variable by authored or internal name (invalid if absent). */
	static FGuid FindPropertyGuid(UUserDefinedStruct* Struct, const FString& PropertyName);

	/**
	 * Resolve a friendly type string + container into an FEdGraphPinType usable by
	 * FStructureEditorUtils::AddVariable. Supports bool, byte, int32, int64, float,
	 * double, name, string, text, named user/native enums, structs, and object classes.
	 * @return false (and fills OutError) if the type string cannot be resolved.
	 */
	static bool ResolvePinType(const FString& TypeString, const FString& ContainerType,
		FEdGraphPinType& OutPinType, FString& OutError);
#endif
};
