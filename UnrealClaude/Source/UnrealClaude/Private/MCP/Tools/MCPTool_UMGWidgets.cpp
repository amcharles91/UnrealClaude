// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_UMGWidgets.h"
#include "UnrealClaudeModule.h"

#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Blueprint/BlueprintSupport.h" // FBlueprintTags::GeneratedClassPath
#include "Misc/PackageName.h"           // FPackageName::ExportTextPathToObjectPath
#endif

// =============================================================================
// Local helpers (file-local, no new members / no .h changes)
// =============================================================================

namespace
{
#if WITH_EDITOR
	/** Mark a Widget Blueprint modified; structural changes trigger a layout/compile refresh. */
	void MarkWidgetBPModified(UWidgetBlueprint* WidgetBP, bool bStructural)
	{
		if (!WidgetBP)
		{
			return;
		}
		WidgetBP->Modify();
		if (bStructural)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
		}
	}

	/** Serialize a single widget's basic info into a JSON object. */
	TSharedPtr<FJsonObject> WidgetToJson(UWidget* Widget, UWidget* RootWidget)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Widget)
		{
			return Obj;
		}
		Obj->SetStringField(TEXT("name"), Widget->GetName());
		Obj->SetStringField(TEXT("type"), Widget->GetClass()->GetName());
		Obj->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);
		Obj->SetBoolField(TEXT("is_root"), Widget == RootWidget);
		if (UPanelWidget* Parent = Widget->GetParent())
		{
			Obj->SetStringField(TEXT("parent"), Parent->GetName());
		}
		return Obj;
	}
#endif // WITH_EDITOR
}

FMCPToolInfo FMCPTool_UMGWidgets::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("umg_widgets");
	Info.Description = TEXT(
		"Manage Unreal Engine UMG Widget Blueprints (create, inspect, edit, animate, bind).\n\n"
		"Operations (set 'operation'):\n"
		"- 'list': List Widget Blueprint assets; optional 'path' prefix filter (default '/Game')\n"
		"- 'create_widget': Create a new Widget Blueprint at 'widget_path' (optional 'parent_class', 'root_widget')\n"
		"- 'get_info': Hierarchy + components for the Widget Blueprint at 'widget_path' (optional 'component_name' for a single widget)\n"
		"- 'add_component': Add a widget of 'component_type' named 'component_name' under 'parent_name' (optional 'is_variable')\n"
		"- 'remove_component': Remove 'component_name' (optional 'remove_children')\n"
		"- 'set_property': Set 'property_name' = 'property_value' on 'component_name'\n"
		"- 'add_animation': Create animation 'animation_name' (optional 'duration', track via 'component_name'/'property_name')\n"
		"- 'bind_event': Bind 'event_name' on 'component_name' to 'function_name'\n"
		"- 'set_mvvm_binding': Bind ViewModel 'view_model_name'.'view_model_property' to 'component_name'.'property_name' ('binding_mode')\n"
		"- 'validate': Validate the widget hierarchy of the Widget Blueprint at 'widget_path'\n\n"
		"Widget Blueprints are UMG assets (/Game/UI/WBP_*). MVVM bindings require the "
		"ModelViewViewModel plugin to be enabled.\n\n"
		"Returns: operation-specific data (asset list, hierarchy, component info, or validation result)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: list, create_widget, get_info, add_component, remove_component, set_property, add_animation, bind_event, set_mvvm_binding, validate"), true),
		FMCPToolParameter(TEXT("widget_path"), TEXT("string"), TEXT("Full /Game path to the Widget Blueprint (e.g. '/Game/UI/WBP_MainMenu')"), false),
		FMCPToolParameter(TEXT("path"), TEXT("string"), TEXT("For 'list': optional path prefix to filter assets (default '/Game')"), false),
		FMCPToolParameter(TEXT("parent_class"), TEXT("string"), TEXT("For 'create_widget': base UserWidget class (default 'UserWidget')"), false),
		FMCPToolParameter(TEXT("root_widget"), TEXT("string"), TEXT("For 'create_widget': root panel widget type to create (e.g. 'CanvasPanel')"), false),
		FMCPToolParameter(TEXT("component_name"), TEXT("string"), TEXT("Name of the target widget component (for get_info/add_component/remove_component/set_property/bind_event/animation track/mvvm)"), false),
		FMCPToolParameter(TEXT("component_type"), TEXT("string"), TEXT("For 'add_component': widget type — native (e.g. 'Button', 'TextBlock') or custom WBP asset name"), false),
		FMCPToolParameter(TEXT("parent_name"), TEXT("string"), TEXT("For 'add_component': name of the parent panel widget (empty = root)"), false),
		FMCPToolParameter(TEXT("is_variable"), TEXT("boolean"), TEXT("For 'add_component': expose the new component as a Blueprint variable (default true)"), false),
		FMCPToolParameter(TEXT("remove_children"), TEXT("boolean"), TEXT("For 'remove_component': also remove child widgets (default false)"), false),
		FMCPToolParameter(TEXT("property_name"), TEXT("string"), TEXT("Property name (for set_property, animation track, or mvvm widget property)"), false),
		FMCPToolParameter(TEXT("property_value"), TEXT("string"), TEXT("For 'set_property': the value to set (as a string)"), false),
		FMCPToolParameter(TEXT("animation_name"), TEXT("string"), TEXT("For 'add_animation': name of the widget animation"), false),
		FMCPToolParameter(TEXT("duration"), TEXT("number"), TEXT("For 'add_animation': animation duration in seconds (default 1.0)"), false),
		FMCPToolParameter(TEXT("event_name"), TEXT("string"), TEXT("For 'bind_event': event on the component (e.g. 'OnClicked')"), false),
		FMCPToolParameter(TEXT("function_name"), TEXT("string"), TEXT("For 'bind_event': Blueprint function to call when the event fires"), false),
		FMCPToolParameter(TEXT("view_model_name"), TEXT("string"), TEXT("For 'set_mvvm_binding': registered ViewModel name/alias"), false),
		FMCPToolParameter(TEXT("view_model_property"), TEXT("string"), TEXT("For 'set_mvvm_binding': property on the ViewModel (e.g. 'CurrentHealth')"), false),
		FMCPToolParameter(TEXT("binding_mode"), TEXT("string"), TEXT("For 'set_mvvm_binding': OneWayToDestination (default), TwoWay, OneTimeToDestination, OneWayToSource, OneTimeToSource"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_UMGWidgets::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("list"))             { return OpList(Params); }
	if (Operation == TEXT("create_widget"))    { return OpCreateWidget(Params); }
	if (Operation == TEXT("get_info"))         { return OpGetInfo(Params); }
	if (Operation == TEXT("add_component"))    { return OpAddComponent(Params); }
	if (Operation == TEXT("remove_component")) { return OpRemoveComponent(Params); }
	if (Operation == TEXT("set_property"))     { return OpSetProperty(Params); }
	if (Operation == TEXT("add_animation"))    { return OpAddAnimation(Params); }
	if (Operation == TEXT("bind_event"))       { return OpBindEvent(Params); }
	if (Operation == TEXT("set_mvvm_binding")) { return OpSetMvvmBinding(Params); }
	if (Operation == TEXT("validate"))         { return OpValidate(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: list, create_widget, get_info, add_component, remove_component, set_property, add_animation, bind_event, set_mvvm_binding, validate"), *Operation));
}

// =============================================================================
// Operation implementations
// =============================================================================

FMCPToolResult FMCPTool_UMGWidgets::OpList(const TSharedRef<FJsonObject>& Params)
{
	const FString PathFilter = ExtractOptionalString(Params, TEXT("path"), TEXT("/Game"));

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/UMGEditor.WidgetBlueprint")));
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	TArray<TSharedPtr<FJsonValue>> Assets;
	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		Obj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		Obj->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
		Assets.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("count"), Assets.Num());
	Data->SetArrayField(TEXT("widgets"), Assets);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d Widget Blueprint(s) under '%s'"), Assets.Num(), *PathFilter), Data);
}

FMCPToolResult FMCPTool_UMGWidgets::OpCreateWidget(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString WidgetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("widget_path"), WidgetPath, Err)) { return Err.GetValue(); }

	if (!WidgetPath.StartsWith(TEXT("/")))
	{
		return FMCPToolResult::Error(TEXT("'widget_path' must be a full content path (e.g. '/Game/UI/WBP_MainMenu')"));
	}

	// If an asset already exists at the path, error out rather than clobber.
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(WidgetPath + TEXT(".") + FPackageName::GetShortName(WidgetPath))).IsValid())
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("An asset already exists at '%s'"), *WidgetPath));
		}
	}

	// Resolve the parent (base) UserWidget class.
	const FString ParentClassName = ExtractOptionalString(Params, TEXT("parent_class"), TEXT("UserWidget"));
	UClass* ParentClass = FindWidgetClass(ParentClassName);
	if (!ParentClass)
	{
		// Fall back to direct class resolution for arbitrary UserWidget subclasses.
		ParentClass = LoadClass<UUserWidget>(nullptr, *ParentClassName);
		if (!ParentClass)
		{
			ParentClass = LoadClass<UUserWidget>(nullptr, *FString::Printf(TEXT("/Script/UMG.%s"), *ParentClassName));
		}
	}
	if (!ParentClass)
	{
		ParentClass = UUserWidget::StaticClass();
	}
	if (!ParentClass->IsChildOf(UUserWidget::StaticClass())
		|| !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("'parent_class' (%s) must be a UserWidget subclass that can be a Blueprint base"), *ParentClassName));
	}

	const FString PackageName = WidgetPath;
	const FString AssetName = FPackageName::GetShortName(PackageName);

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package '%s'"), *PackageName));
	}
	Package->FullyLoad();

	// Canonical Widget Blueprint creation (mirrors UWidgetBlueprintFactory).
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass(),
		NAME_None));

	if (!WidgetBP)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create Widget Blueprint at '%s'"), *WidgetPath));
	}

	// Create the requested root panel widget (default CanvasPanel) if none exists.
	if (WidgetBP->WidgetTree && WidgetBP->WidgetTree->RootWidget == nullptr)
	{
		const FString RootType = ExtractOptionalString(Params, TEXT("root_widget"), TEXT("CanvasPanel"));
		UClass* RootClass = FindWidgetClass(RootType);
		if (!RootClass || !RootClass->IsChildOf(UPanelWidget::StaticClass()))
		{
			RootClass = UCanvasPanel::StaticClass();
		}
		UWidget* Root = WidgetBP->WidgetTree->ConstructWidget<UWidget>(RootClass);
		WidgetBP->WidgetTree->RootWidget = Root;
		if (Root)
		{
			WidgetBP->OnVariableAdded(Root->GetFName());
		}
	}

	FAssetRegistryModule::AssetCreated(WidgetBP);
	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_path"), WidgetPath);
	Data->SetStringField(TEXT("name"), WidgetBP->GetName());
	Data->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	if (WidgetBP->WidgetTree && WidgetBP->WidgetTree->RootWidget)
	{
		Data->SetStringField(TEXT("root_widget"), WidgetBP->WidgetTree->RootWidget->GetClass()->GetName());
	}
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created Widget Blueprint '%s' (parent: %s). Save via the editor to persist."),
			*WidgetPath, *ParentClass->GetName()), Data);
#else
	return FMCPToolResult::Error(TEXT("create_widget requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UMGWidgets::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString WidgetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("widget_path"), WidgetPath, Err)) { return Err.GetValue(); }

	UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint '%s' not found or has no WidgetTree"), *WidgetPath));
	}

	UWidget* RootWidget = WidgetBP->WidgetTree->RootWidget;
	const FString ComponentName = ExtractOptionalString(Params, TEXT("component_name"));

	// Single-component query.
	if (!ComponentName.IsEmpty())
	{
		UWidget* Widget = FindWidgetByName(WidgetBP, ComponentName);
		if (!Widget)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Component '%s' not found in '%s'"), *ComponentName, *WidgetPath));
		}
		TSharedPtr<FJsonObject> Data = WidgetToJson(Widget, RootWidget);

		// List children, if it is a panel.
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			TArray<TSharedPtr<FJsonValue>> Children;
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				if (UWidget* Child = Panel->GetChildAt(i))
				{
					Children.Add(MakeShared<FJsonValueString>(Child->GetName()));
				}
			}
			Data->SetArrayField(TEXT("children"), Children);
		}
		return FMCPToolResult::Success(FString::Printf(TEXT("Component '%s' (%s)"), *ComponentName, *Widget->GetClass()->GetName()), Data);
	}

	// Full hierarchy.
	TArray<UWidget*> AllWidgets;
	WidgetBP->WidgetTree->GetAllWidgets(AllWidgets);
	if (RootWidget)
	{
		AllWidgets.AddUnique(RootWidget);
	}

	TArray<TSharedPtr<FJsonValue>> Components;
	for (UWidget* Widget : AllWidgets)
	{
		if (Widget)
		{
			Components.Add(MakeShared<FJsonValueObject>(WidgetToJson(Widget, RootWidget)));
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_path"), WidgetPath);
	Data->SetStringField(TEXT("root_widget"), RootWidget ? RootWidget->GetName() : TEXT(""));
	Data->SetNumberField(TEXT("count"), Components.Num());
	Data->SetArrayField(TEXT("components"), Components);

	// Animations summary.
	TArray<TSharedPtr<FJsonValue>> Anims;
	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (Anim)
		{
			Anims.Add(MakeShared<FJsonValueString>(Anim->GetName()));
		}
	}
	Data->SetArrayField(TEXT("animations"), Anims);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Widget Blueprint '%s': %d component(s)"), *WidgetPath, Components.Num()), Data);
#else
	return FMCPToolResult::Error(TEXT("get_info requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UMGWidgets::OpAddComponent(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString WidgetPath, ComponentType, ComponentName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("widget_path"), WidgetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("component_type"), ComponentType, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("component_name"), ComponentName, Err)) { return Err.GetValue(); }

	const FString ParentName = ExtractOptionalString(Params, TEXT("parent_name"));
	const bool bIsVariable = ExtractOptionalBool(Params, TEXT("is_variable"), true);

	UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint '%s' not found or has no WidgetTree"), *WidgetPath));
	}

	// Reject duplicate names up front.
	if (FindWidgetByName(WidgetBP, ComponentName) != nullptr)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("A widget named '%s' already exists in '%s'"), *ComponentName, *WidgetPath));
	}

	UClass* WidgetClass = FindWidgetClass(ComponentType);
	if (!WidgetClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown widget type '%s' (native UMG type or compiled custom WBP asset name)"), *ComponentType));
	}

	const bool bIsUserWidget = WidgetClass->IsChildOf(UUserWidget::StaticClass());
	if (bIsUserWidget && WidgetBP->GeneratedClass
		&& (WidgetClass == WidgetBP->GeneratedClass || WidgetClass->IsChildOf(WidgetBP->GeneratedClass)))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Cannot add '%s': circular reference (a Widget Blueprint cannot contain itself)"), *ComponentType));
	}

	// Resolve the parent panel.
	UPanelWidget* ParentPanel = nullptr;
	if (!ParentName.IsEmpty())
	{
		ParentPanel = Cast<UPanelWidget>(FindWidgetByName(WidgetBP, ParentName));
		if (!ParentPanel)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Parent '%s' not found or is not a panel widget"), *ParentName));
		}
	}
	else
	{
		ParentPanel = Cast<UPanelWidget>(WidgetBP->WidgetTree->RootWidget);
	}

	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*ComponentName));
	if (!NewWidget)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to construct widget of type '%s'"), *ComponentType));
	}
	NewWidget->bIsVariable = bIsVariable;

	// Register GUID (required for UMG compilation).
	const FName WidgetFName = NewWidget->GetFName();
	if (!WidgetBP->WidgetVariableNameToGuidMap.Contains(WidgetFName))
	{
		WidgetBP->WidgetVariableNameToGuidMap.Add(WidgetFName, FGuid::NewGuid());
	}

	FString ResolvedParent;
	if (ParentPanel)
	{
		UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
		if (!Slot)
		{
			WidgetBP->WidgetTree->RemoveWidget(NewWidget);
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Failed to add '%s' to parent '%s' (panel may be full or incompatible)"), *ComponentName, *ParentPanel->GetName()));
		}
		ResolvedParent = ParentPanel->GetName();
	}
	else if (!WidgetBP->WidgetTree->RootWidget)
	{
		// GUID already registered above; do not call OnVariableAdded (it ensures !Contains).
		WidgetBP->WidgetTree->RootWidget = NewWidget;
		ResolvedParent = TEXT("(root)");
	}
	else
	{
		WidgetBP->WidgetTree->RemoveWidget(NewWidget);
		return FMCPToolResult::Error(TEXT("No parent specified and root already exists (specify 'parent_name')"));
	}

	WidgetBP->Modify();
	if (bIsUserWidget)
	{
		// Synchronous compile keeps a UserWidget child in a valid state before deferred work.
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_path"), WidgetPath);
	Data->SetStringField(TEXT("component_name"), NewWidget->GetName());
	Data->SetStringField(TEXT("component_type"), ComponentType);
	Data->SetStringField(TEXT("parent"), ResolvedParent);
	Data->SetBoolField(TEXT("is_variable"), bIsVariable);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added '%s' (%s) under '%s' in '%s'"), *NewWidget->GetName(), *ComponentType, *ResolvedParent, *WidgetPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_component requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UMGWidgets::OpRemoveComponent(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString WidgetPath, ComponentName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("widget_path"), WidgetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("component_name"), ComponentName, Err)) { return Err.GetValue(); }

	const bool bRemoveChildren = ExtractOptionalBool(Params, TEXT("remove_children"), false);

	UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint '%s' not found or has no WidgetTree"), *WidgetPath));
	}

	UWidget* WidgetToRemove = FindWidgetByName(WidgetBP, ComponentName);
	if (!WidgetToRemove)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Component '%s' not found in '%s'"), *ComponentName, *WidgetPath));
	}
	if (WidgetToRemove == WidgetBP->WidgetTree->RootWidget)
	{
		return FMCPToolResult::Error(TEXT("Cannot remove the root widget"));
	}

	TArray<FString> Removed;
	TArray<FString> Orphaned;

	// Gather the descendants of the target (full subtree, all nesting levels).
	// RemoveWidget on the parent only detaches the target itself; nested
	// grandchildren remain registered in the WidgetTree. To keep the reported
	// "removed" count honest we either remove the entire subtree (bRemoveChildren)
	// or refuse to touch children and report them as orphaned.
	TArray<UWidget*> Subtree;
	if (UPanelWidget* TargetPanel = Cast<UPanelWidget>(WidgetToRemove))
	{
		TFunction<void(UPanelWidget*)> Gather = [&](UPanelWidget* Panel)
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				if (UWidget* Child = Panel->GetChildAt(i))
				{
					Subtree.Add(Child);
					if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(Child))
					{
						Gather(ChildPanel);
					}
				}
			}
		};
		Gather(TargetPanel);
	}

	if (bRemoveChildren)
	{
		MarkWidgetBPModified(WidgetBP, /*bStructural=*/true);

		// Detach the target from its parent, then remove the entire subtree from
		// the WidgetTree so no grandchildren are left orphaned. Remove deepest
		// (collected) descendants first, then the target itself.
		if (UPanelWidget* Parent = WidgetToRemove->GetParent())
		{
			Parent->RemoveChild(WidgetToRemove);
		}
		for (int32 i = Subtree.Num() - 1; i >= 0; --i)
		{
			if (UWidget* Child = Subtree[i])
			{
				Removed.Add(Child->GetName());
				WidgetBP->WidgetTree->RemoveWidget(Child);
			}
		}
		WidgetBP->WidgetTree->RemoveWidget(WidgetToRemove);
		Removed.Add(ComponentName);
	}
	else
	{
		// Direct children only are reported orphaned (they stay in the tree,
		// detached from the removed parent). Single-level removal of the target.
		if (UPanelWidget* Panel = Cast<UPanelWidget>(WidgetToRemove))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				if (UWidget* Child = Panel->GetChildAt(i))
				{
					Orphaned.Add(Child->GetName());
				}
			}
		}

		MarkWidgetBPModified(WidgetBP, /*bStructural=*/true);

		if (UPanelWidget* Parent = WidgetToRemove->GetParent())
		{
			Parent->RemoveChild(WidgetToRemove);
		}
		WidgetBP->WidgetTree->RemoveWidget(WidgetToRemove);
		Removed.Add(ComponentName);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_path"), WidgetPath);
	Data->SetArrayField(TEXT("removed"), StringArrayToJsonArray(Removed));
	Data->SetArrayField(TEXT("orphaned_children"), StringArrayToJsonArray(Orphaned));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed '%s' from '%s' (%d removed, %d orphaned)"), *ComponentName, *WidgetPath, Removed.Num(), Orphaned.Num()), Data);
#else
	return FMCPToolResult::Error(TEXT("remove_component requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UMGWidgets::OpSetProperty(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString WidgetPath, ComponentName, PropertyName, PropertyValue;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("widget_path"), WidgetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("component_name"), ComponentName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("property_name"), PropertyName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("property_value"), PropertyValue, Err)) { return Err.GetValue(); }

	UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint '%s' not found or has no WidgetTree"), *WidgetPath));
	}

	UWidget* Widget = FindWidgetByName(WidgetBP, ComponentName);
	if (!Widget)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Component '%s' not found in '%s'"), *ComponentName, *WidgetPath));
	}

	// Resolve the property on the widget itself, falling back to its layout slot
	// (so layout/alignment/z-order properties can also be set).
	UObject* TargetObject = Widget;
	FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	bool bWroteToSlot = false;
	if (!Prop && Widget->Slot)
	{
		Prop = Widget->Slot->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (Prop)
		{
			TargetObject = Widget->Slot;
			bWroteToSlot = true;
		}
	}
	if (!Prop)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Property '%s' not found on '%s' (%s) or its slot"), *PropertyName, *ComponentName, *Widget->GetClass()->GetName()));
	}

	const FString OldValue = [&]() {
		FString S;
		Prop->ExportTextItem_Direct(S, Prop->ContainerPtrToValuePtr<void>(TargetObject), nullptr, TargetObject, PPF_None);
		return S;
	}();

	// Snapshot for undo BEFORE mutating: mark both the owning Blueprint and the
	// target object (widget or its layout slot) modified so the transaction
	// buffer captures the prior value, then perform the write.
	WidgetBP->Modify();
	TargetObject->Modify();

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
	if (Prop->ImportText_Direct(*PropertyValue, ValuePtr, TargetObject, PPF_None) == nullptr)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Could not parse value '%s' for property '%s' (%s)"), *PropertyValue, *PropertyName, *Prop->GetCPPType()));
	}

	// Flag structural/non-structural change for layout/compile refresh (Modify already done above).
	if (bWroteToSlot)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	}

	FString NewValue;
	Prop->ExportTextItem_Direct(NewValue, Prop->ContainerPtrToValuePtr<void>(TargetObject), nullptr, TargetObject, PPF_None);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_path"), WidgetPath);
	Data->SetStringField(TEXT("component_name"), ComponentName);
	Data->SetStringField(TEXT("property_name"), PropertyName);
	Data->SetStringField(TEXT("old_value"), OldValue);
	Data->SetStringField(TEXT("value"), NewValue);
	Data->SetBoolField(TEXT("on_slot"), bWroteToSlot);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set %s.%s: %s -> %s"), *ComponentName, *PropertyName, *OldValue, *NewValue), Data);
#else
	return FMCPToolResult::Error(TEXT("set_property requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UMGWidgets::OpAddAnimation(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString WidgetPath, AnimationName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("widget_path"), WidgetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("animation_name"), AnimationName, Err)) { return Err.GetValue(); }

	// Track authoring (component_name + property_name) needs MovieScene property-track
	// resolution + a preview UserWidget instance; that path is deferred to avoid an
	// uncertain API. This op creates the (empty) animation with the requested duration.
	if (!ExtractOptionalString(Params, TEXT("component_name")).IsEmpty()
		|| !ExtractOptionalString(Params, TEXT("property_name")).IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("add_animation: track authoring (component_name/property_name) deferred - requires MovieScene property-track + preview-instance resolution; create the animation without a track instead"));
	}

	const float Duration = FMath::Max(ExtractOptionalNumber<float>(Params, TEXT("duration"), 1.0f), 0.0f);

	UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
	if (!WidgetBP)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetPath));
	}

	// Reject duplicates.
	for (UWidgetAnimation* Existing : WidgetBP->Animations)
	{
		if (Existing && Existing->GetName().Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Animation '%s' already exists in '%s'"), *AnimationName, *WidgetPath));
		}
	}

	WidgetBP->Modify();

	UWidgetAnimation* NewAnimation = NewObject<UWidgetAnimation>(WidgetBP, FName(*AnimationName), RF_Transactional);
	if (!NewAnimation)
	{
		return FMCPToolResult::Error(TEXT("Failed to create UWidgetAnimation object"));
	}
	NewAnimation->SetDisplayLabel(AnimationName);
	NewAnimation->Rename(*AnimationName, WidgetBP);

	NewAnimation->MovieScene = NewObject<UMovieScene>(NewAnimation, FName(*AnimationName), RF_Transactional);
	if (!NewAnimation->MovieScene)
	{
		return FMCPToolResult::Error(TEXT("Failed to create MovieScene for animation"));
	}

	NewAnimation->MovieScene->SetDisplayRate(FFrameRate(20, 1));
	const FFrameTime OutFrame = Duration * NewAnimation->MovieScene->GetTickResolution();
	NewAnimation->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(0, OutFrame.FrameNumber + 1));
	NewAnimation->MovieScene->GetEditorData().WorkStart = 0.0f;
	NewAnimation->MovieScene->GetEditorData().WorkEnd = Duration;

	WidgetBP->Animations.Add(NewAnimation);
	MarkWidgetBPModified(WidgetBP, /*bStructural=*/true);
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_path"), WidgetPath);
	Data->SetStringField(TEXT("animation_name"), NewAnimation->GetName());
	Data->SetNumberField(TEXT("duration"), Duration);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created animation '%s' (%.2fs) in '%s'"), *NewAnimation->GetName(), Duration, *WidgetPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_animation requires an editor build"));
#endif
}

FMCPToolResult FMCPTool_UMGWidgets::OpBindEvent(const TSharedRef<FJsonObject>& Params)
{
	// Event binding requires creating/wiring a Blueprint event graph node for the widget's
	// multicast delegate (FKismetEditorUtilities::CreateNewBoundEventForClass + K2 graph
	// surgery). VibeUE's own implementation is a logging no-op, so there is no proven
	// reference API; deferring rather than guess a path that risks a broken build.
	return FMCPToolResult::Error(TEXT("bind_event: deferred - requires Blueprint event-graph node creation (no safe reference implementation)"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpSetMvvmBinding(const TSharedRef<FJsonObject>& Params)
{
	// MVVM binding construction goes through UMVVMBlueprintView + FMVVMBlueprintViewBinding
	// + FMVVMBlueprintPropertyPath, whose construction API shifts between engine versions.
	// Deferring to avoid an uncertain API on UE5.7 that risks a broken Live Coding build.
	return FMCPToolResult::Error(TEXT("set_mvvm_binding: deferred - MVVMBlueprintView binding construction API is version-sensitive and not verified on 5.7"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpValidate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString WidgetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("widget_path"), WidgetPath, Err)) { return Err.GetValue(); }

	UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
	if (!WidgetBP)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetPath));
	}
	if (!WidgetBP->WidgetTree)
	{
		return FMCPToolResult::Error(TEXT("Widget Blueprint has no WidgetTree"));
	}

	TArray<FString> Errors;

	UWidget* RootWidget = WidgetBP->WidgetTree->RootWidget;
	if (!RootWidget)
	{
		Errors.Add(TEXT("Widget Blueprint has no root widget"));
	}

	TArray<UWidget*> AllWidgets;
	WidgetBP->WidgetTree->GetAllWidgets(AllWidgets);

	// Duplicate names.
	TSet<FString> Seen;
	for (UWidget* Widget : AllWidgets)
	{
		if (!Widget)
		{
			continue;
		}
		const FString Name = Widget->GetName();
		if (Seen.Contains(Name))
		{
			Errors.Add(FString::Printf(TEXT("Duplicate widget name: %s"), *Name));
		}
		Seen.Add(Name);
	}

	// Orphans (not reachable from root).
	if (RootWidget)
	{
		TSet<UWidget*> Reachable;
		TFunction<void(UWidget*)> Collect = [&](UWidget* W)
		{
			if (!W || Reachable.Contains(W))
			{
				return;
			}
			Reachable.Add(W);
			if (UPanelWidget* Panel = Cast<UPanelWidget>(W))
			{
				for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
				{
					Collect(Panel->GetChildAt(i));
				}
			}
		};
		Collect(RootWidget);

		for (UWidget* Widget : AllWidgets)
		{
			if (Widget && !Reachable.Contains(Widget))
			{
				Errors.Add(FString::Printf(TEXT("Orphaned widget not in hierarchy: %s"), *Widget->GetName()));
			}
		}
	}

	const bool bIsValid = (Errors.Num() == 0);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_path"), WidgetPath);
	Data->SetBoolField(TEXT("is_valid"), bIsValid);
	Data->SetNumberField(TEXT("widget_count"), AllWidgets.Num());
	Data->SetArrayField(TEXT("errors"), StringArrayToJsonArray(Errors));
	return FMCPToolResult::Success(
		bIsValid
			? FString::Printf(TEXT("Widget hierarchy of '%s' is valid (%d widgets)"), *WidgetPath, AllWidgets.Num())
			: FString::Printf(TEXT("Widget hierarchy of '%s' has %d issue(s)"), *WidgetPath, Errors.Num()),
		Data);
#else
	return FMCPToolResult::Error(TEXT("validate requires an editor build"));
#endif
}

// =============================================================================
// Private helpers
// =============================================================================

UWidgetBlueprint* FMCPTool_UMGWidgets::LoadWidgetBlueprint(const FString& WidgetPath) const
{
#if WITH_EDITOR
	if (WidgetPath.IsEmpty())
	{
		return nullptr;
	}
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *WidgetPath));
	if (!WidgetBP)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("UMGWidgets: Failed to load Widget Blueprint '%s'"), *WidgetPath);
	}
	return WidgetBP;
#else
	return nullptr;
#endif
}

UWidget* FMCPTool_UMGWidgets::FindWidgetByName(UWidgetBlueprint* WidgetBP, const FString& ComponentName) const
{
#if WITH_EDITOR
	if (!WidgetBP || !WidgetBP->WidgetTree || ComponentName.IsEmpty())
	{
		return nullptr;
	}
	TArray<UWidget*> AllWidgets;
	WidgetBP->WidgetTree->GetAllWidgets(AllWidgets);
	for (UWidget* Widget : AllWidgets)
	{
		if (Widget && Widget->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			return Widget;
		}
	}
	return nullptr;
#else
	return nullptr;
#endif
}

UClass* FMCPTool_UMGWidgets::FindWidgetClass(const FString& TypeName) const
{
#if WITH_EDITOR
	if (TypeName.IsEmpty())
	{
		return nullptr;
	}

	const FString WithPrefix = FString(TEXT("U")) + TypeName;

	// 1. Native UWidget subclass by name (with or without the 'U' prefix).
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UWidget::StaticClass())
			&& !Class->HasAnyClassFlags(CLASS_Abstract)
			&& (Class->GetName().Equals(TypeName, ESearchCase::IgnoreCase)
				|| Class->GetName().Equals(WithPrefix, ESearchCase::IgnoreCase)))
		{
			return Class;
		}
	}

	// 2. Custom Widget Blueprint asset by name (use its compiled GeneratedClass).
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/UMGEditor.WidgetBlueprint")));
	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (!AssetData.AssetName.ToString().Equals(TypeName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		// This is a LOOKUP, not a build step: resolve the already-compiled
		// generated class without compiling (which would be slow and would dirty
		// an otherwise-clean asset). Compiling is only a last resort below.

		// 1. Read the GeneratedClass export path from the asset registry tag -
		//    no asset load required when the class is already in memory.
		FString GeneratedClassPath;
		if (AssetData.GetTagValue(FBlueprintTags::GeneratedClassPath, GeneratedClassPath)
			&& !GeneratedClassPath.IsEmpty())
		{
			const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassPath);
			if (UClass* GenClass = FindObject<UClass>(nullptr, *ClassObjectPath))
			{
				if (GenClass->IsChildOf(UWidget::StaticClass()))
				{
					return GenClass;
				}
			}
		}

		// 2. Load the WBP and use its (already-compiled) GeneratedClass.
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(AssetData.GetAsset());
		if (!WBP)
		{
			continue;
		}
		if (WBP->GeneratedClass && WBP->GeneratedClass->IsChildOf(UWidget::StaticClass()))
		{
			return WBP->GeneratedClass;
		}

		// 3. Last resort: a never-compiled asset has no generated class to resolve,
		//    so compile once to produce it. (Avoided in the common case above.)
		if (!WBP->GeneratedClass)
		{
			FKismetEditorUtilities::CompileBlueprint(WBP);
			if (WBP->GeneratedClass && WBP->GeneratedClass->IsChildOf(UWidget::StaticClass()))
			{
				return WBP->GeneratedClass;
			}
		}
	}

	return nullptr;
#else
	return nullptr;
#endif
}

UClass* FMCPTool_UMGWidgets::FindViewModelClass(const FString& ClassName) const
{
	// Only used by the deferred MVVM path; resolution lives there if/when implemented.
	return nullptr;
}
