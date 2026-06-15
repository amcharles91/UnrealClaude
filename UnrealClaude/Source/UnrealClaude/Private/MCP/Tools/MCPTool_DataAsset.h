// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

class UClass;
class UDataAsset;
class FProperty;

/**
 * MCP Tool: General-purpose Unreal Engine UDataAsset management.
 *
 * Create instances of any UDataAsset subclass, list existing DataAssets (via the
 * AssetRegistry), read properties via reflection, and edit individual properties.
 * Works against arbitrary UDataAsset subclasses by name — for character-specific
 * config assets use the dedicated 'character_data' tool instead.
 *
 * Ported from VibeUE's UDataAssetService into the native MCP tool pattern. Uses
 * UE reflection (FProperty + ImportText_Direct/ExportTextItem_Direct) so any
 * editable property can be read or written from a JSON-supplied string value.
 */
class FMCPTool_DataAsset : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpList(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpCreate(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGet(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetProperty(const TSharedRef<FJsonObject>& Params);

	/** Find a UDataAsset subclass by name (tries with/without 'U' prefix; excludes abstract). */
	static UClass* FindDataAssetClass(const FString& ClassName, bool bAllowAbstract = false);

	/** Load a UDataAsset by object path (e.g. '/Game/Data/DA_Sword'). */
	static UDataAsset* LoadDataAsset(const FString& AssetPath, FString& OutError);

	/** Whether a property should be surfaced to clients (skips transient/deprecated; editable only). */
	static bool ShouldExposeProperty(const FProperty* Property);

	/** Human-readable type name for a property (bool, int32, FString, TArray<...>, ...). */
	static FString GetPropertyTypeString(const FProperty* Property);

	/** Export a property's current value to its text representation. */
	static FString PropertyToString(const FProperty* Property, const void* Container);

	/** Import a text value into a property; returns false (with OutError) on a partial/failed parse. */
	static bool SetPropertyFromString(const FProperty* Property, void* Container, const FString& Value, FString& OutError);

	/** Build a JSON object describing one property (name, type, value, category, read_only, is_array). */
	static TSharedPtr<FJsonObject> PropertyInfoJson(const FProperty* Property, const void* Container);

	/** Save the package owning Asset to disk. */
	static bool SaveAsset(UObject* Asset, FString& OutError);
};
