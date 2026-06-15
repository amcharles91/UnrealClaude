// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AnimMontage.h"

FMCPToolInfo FMCPTool_AnimMontage::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("anim_montage");
	Info.Description = TEXT(
		"Manage Unreal Engine Animation Montages (create, query, sections, slots, notifies, branch points, blend).\n\n"
		"Operations (set 'operation'):\n"
		"- 'create': Create a montage from 'anim_sequence_path' (or empty from 'skeleton_path') into 'dest_path' as 'montage_name'\n"
		"- 'get_info': Details for the montage at 'montage_path' (length, sections, slots, notifies, blend settings)\n"
		"- 'add_section': Add section 'section_name' at 'start_time'\n"
		"- 'remove_section': Remove section 'section_name'\n"
		"- 'set_slot': Rename the slot track at 'track_index' to 'slot_name'\n"
		"- 'add_notify': Add a notify of 'notify_class' at 'trigger_time' (optional 'notify_name')\n"
		"- 'add_branch_point': Add a branching point 'notify_name' at 'trigger_time'\n"
		"- 'set_blend': Set blend-in and/or blend-out time and option (e.g., 'Linear')\n\n"
		"Montage paths are content paths (e.g., '/Game/Animations/Attack_Montage').\n\n"
		"Returns: operation-specific data (montage info, or the modified element)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: create, get_info, add_section, remove_section, set_slot, add_notify, add_branch_point, set_blend"), true),
		FMCPToolParameter(TEXT("montage_path"), TEXT("string"), TEXT("Content path of the montage (e.g., '/Game/Animations/Attack_Montage')"), false),
		FMCPToolParameter(TEXT("anim_sequence_path"), TEXT("string"), TEXT("For 'create': source anim sequence to wrap into the montage"), false),
		FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("For 'create': skeleton path when creating an empty montage"), false),
		FMCPToolParameter(TEXT("dest_path"), TEXT("string"), TEXT("For 'create': destination content folder for the new montage"), false),
		FMCPToolParameter(TEXT("montage_name"), TEXT("string"), TEXT("For 'create': asset name for the new montage"), false),
		FMCPToolParameter(TEXT("section_name"), TEXT("string"), TEXT("For add_section/remove_section: the section name"), false),
		FMCPToolParameter(TEXT("start_time"), TEXT("number"), TEXT("For 'add_section': section start time in seconds"), false),
		FMCPToolParameter(TEXT("track_index"), TEXT("number"), TEXT("For 'set_slot': index of the slot track to modify"), false),
		FMCPToolParameter(TEXT("slot_name"), TEXT("string"), TEXT("For 'set_slot': new slot name"), false),
		FMCPToolParameter(TEXT("notify_class"), TEXT("string"), TEXT("For 'add_notify': the AnimNotify class to instantiate"), false),
		FMCPToolParameter(TEXT("trigger_time"), TEXT("number"), TEXT("For add_notify/add_branch_point: trigger time in seconds"), false),
		FMCPToolParameter(TEXT("notify_name"), TEXT("string"), TEXT("For add_notify (optional) / add_branch_point (required): notify name"), false),
		FMCPToolParameter(TEXT("blend_in_time"), TEXT("number"), TEXT("For 'set_blend': blend-in time in seconds"), false),
		FMCPToolParameter(TEXT("blend_out_time"), TEXT("number"), TEXT("For 'set_blend': blend-out time in seconds"), false),
		FMCPToolParameter(TEXT("blend_option"), TEXT("string"), TEXT("For 'set_blend': blend curve option (e.g., 'Linear', 'Cubic'; default: Linear)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_AnimMontage::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("create"))           { return OpCreate(Params); }
	if (Operation == TEXT("get_info"))         { return OpGetInfo(Params); }
	if (Operation == TEXT("add_section"))      { return OpAddSection(Params); }
	if (Operation == TEXT("remove_section"))   { return OpRemoveSection(Params); }
	if (Operation == TEXT("set_slot"))         { return OpSetSlot(Params); }
	if (Operation == TEXT("add_notify"))       { return OpAddNotify(Params); }
	if (Operation == TEXT("add_branch_point")) { return OpAddBranchPoint(Params); }
	if (Operation == TEXT("set_blend"))        { return OpSetBlend(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create, get_info, add_section, remove_section, set_slot, add_notify, add_branch_point, set_blend"), *Operation));
}

FMCPToolResult FMCPTool_AnimMontage::OpCreate(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("create: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimMontage::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_info: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimMontage::OpAddSection(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_section: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimMontage::OpRemoveSection(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove_section: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimMontage::OpSetSlot(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_slot: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimMontage::OpAddNotify(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_notify: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimMontage::OpAddBranchPoint(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_branch_point: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimMontage::OpSetBlend(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_blend: not implemented yet"));
}
