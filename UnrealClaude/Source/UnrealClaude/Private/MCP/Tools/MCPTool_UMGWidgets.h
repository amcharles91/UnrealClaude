// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Manage Unreal Engine UMG Widget Blueprints.
 *
 * Authoring of Widget Blueprints (UMG): create the asset, inspect/modify the
 * widget tree, edit properties, author animations, bind events, and wire up
 * MVVM (Model-View-ViewModel) data bindings.
 *
 * Ported from VibeUE's UWidgetService into the native MCP tool pattern.
 *
 * NOTE: This is currently a STUB. The operation dispatch + schema are real, but
 * every OpXxx() body returns "not implemented yet". Full logic will be added
 * later via Live Coding once the UMG / UMGEditor / ModelViewViewModel module
 * dependencies are wired into Build.cs.
 */
class FMCPTool_UMGWidgets : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// === Discovery / inspection ===
	FMCPToolResult OpList(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpCreateWidget(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetInfo(const TSharedRef<FJsonObject>& Params);

	// === Component management ===
	FMCPToolResult OpAddComponent(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRemoveComponent(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpSetProperty(const TSharedRef<FJsonObject>& Params);

	// === Animation / events ===
	FMCPToolResult OpAddAnimation(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpBindEvent(const TSharedRef<FJsonObject>& Params);

	// === MVVM ===
	FMCPToolResult OpSetMvvmBinding(const TSharedRef<FJsonObject>& Params);

	// === Validation ===
	FMCPToolResult OpValidate(const TSharedRef<FJsonObject>& Params);

	// ----- Members the full implementation will need -----

	/**
	 * Load and validate a Widget Blueprint asset at the given /Game path.
	 * Returns nullptr on failure (logs via LogUnrealClaude). Stubbed for now.
	 */
	class UWidgetBlueprint* LoadWidgetBlueprint(const FString& WidgetPath) const;

	/** Find a widget component by name within a loaded Widget Blueprint. Stubbed for now. */
	class UWidget* FindWidgetByName(class UWidgetBlueprint* WidgetBP, const FString& ComponentName) const;

	/** Resolve a widget class from a type name (native type or custom WBP asset name). Stubbed for now. */
	UClass* FindWidgetClass(const FString& TypeName) const;

	/** Resolve a ViewModel class by name (C++ or Blueprint ViewModel). Stubbed for now. */
	UClass* FindViewModelClass(const FString& ClassName) const;
};
