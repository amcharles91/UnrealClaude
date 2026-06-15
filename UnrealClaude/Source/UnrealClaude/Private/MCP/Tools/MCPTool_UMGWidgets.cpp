// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_UMGWidgets.h"
#include "UnrealClaudeModule.h"

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
// Operation implementations — STUBS. Full logic to follow via Live Coding.
// =============================================================================

FMCPToolResult FMCPTool_UMGWidgets::OpList(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("list: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpCreateWidget(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("create_widget: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_info: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpAddComponent(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_component: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpRemoveComponent(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove_component: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpSetProperty(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_property: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpAddAnimation(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_animation: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpBindEvent(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("bind_event: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpSetMvvmBinding(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_mvvm_binding: not implemented yet"));
}

FMCPToolResult FMCPTool_UMGWidgets::OpValidate(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("validate: not implemented yet"));
}

// =============================================================================
// Private helpers — STUBS. Full logic to follow via Live Coding.
// =============================================================================

UWidgetBlueprint* FMCPTool_UMGWidgets::LoadWidgetBlueprint(const FString& WidgetPath) const
{
	return nullptr;
}

UWidget* FMCPTool_UMGWidgets::FindWidgetByName(UWidgetBlueprint* WidgetBP, const FString& ComponentName) const
{
	return nullptr;
}

UClass* FMCPTool_UMGWidgets::FindWidgetClass(const FString& TypeName) const
{
	return nullptr;
}

UClass* FMCPTool_UMGWidgets::FindViewModelClass(const FString& ClassName) const
{
	return nullptr;
}
