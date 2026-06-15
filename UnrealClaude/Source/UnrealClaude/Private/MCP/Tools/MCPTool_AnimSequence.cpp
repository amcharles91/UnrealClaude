// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AnimSequence.h"

FMCPToolInfo FMCPTool_AnimSequence::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("anim_sequence");
	Info.Description = TEXT(
		"Manage Unreal Engine Animation Sequences (query, curves, keys, notifies, frame rate).\n\n"
		"Operations (set 'operation'):\n"
		"- 'list': List anim sequences under 'search_path'; optional 'skeleton_filter'\n"
		"- 'get_info': Details for the sequence at 'anim_path' (length, frame_rate, frame_count, skeleton, curves, notifies)\n"
		"- 'add_curve': Add a float curve 'curve_name' to 'anim_path' (optional 'is_morph_target')\n"
		"- 'remove_curve': Remove curve 'curve_name' from 'anim_path'\n"
		"- 'add_key': Add a keyframe to curve 'curve_name' at 'time' with 'value'\n"
		"- 'add_notify': Add a notify of 'notify_class' at 'trigger_time' (optional 'notify_name')\n"
		"- 'remove_notify': Remove the notify at 'notify_index'\n"
		"- 'set_frame_rate': Set the sampling 'frame_rate' for 'anim_path'\n\n"
		"Anim paths are content paths (e.g., '/Game/Animations/Run').\n\n"
		"Returns: operation-specific data (sequence list/info, or the modified element)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: list, get_info, add_curve, remove_curve, add_key, add_notify, remove_notify, set_frame_rate"), true),
		FMCPToolParameter(TEXT("anim_path"), TEXT("string"), TEXT("Content path of the anim sequence (e.g., '/Game/Animations/Run')"), false),
		FMCPToolParameter(TEXT("search_path"), TEXT("string"), TEXT("For 'list': content root to search (default: /Game)"), false),
		FMCPToolParameter(TEXT("skeleton_filter"), TEXT("string"), TEXT("For 'list': only return sequences using this skeleton path"), false),
		FMCPToolParameter(TEXT("curve_name"), TEXT("string"), TEXT("For add_curve/remove_curve/add_key: the curve name"), false),
		FMCPToolParameter(TEXT("is_morph_target"), TEXT("boolean"), TEXT("For 'add_curve': mark the curve as a morph-target curve (default: false)"), false),
		FMCPToolParameter(TEXT("time"), TEXT("number"), TEXT("For 'add_key': keyframe time in seconds"), false),
		FMCPToolParameter(TEXT("value"), TEXT("number"), TEXT("For 'add_key': keyframe value"), false),
		FMCPToolParameter(TEXT("notify_class"), TEXT("string"), TEXT("For 'add_notify': the AnimNotify class to instantiate"), false),
		FMCPToolParameter(TEXT("trigger_time"), TEXT("number"), TEXT("For 'add_notify': trigger time in seconds"), false),
		FMCPToolParameter(TEXT("notify_name"), TEXT("string"), TEXT("For 'add_notify': optional notify name"), false),
		FMCPToolParameter(TEXT("notify_index"), TEXT("number"), TEXT("For 'remove_notify': index of the notify to remove"), false),
		FMCPToolParameter(TEXT("frame_rate"), TEXT("number"), TEXT("For 'set_frame_rate': new sampling frame rate (fps)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_AnimSequence::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("list"))           { return OpList(Params); }
	if (Operation == TEXT("get_info"))       { return OpGetInfo(Params); }
	if (Operation == TEXT("add_curve"))      { return OpAddCurve(Params); }
	if (Operation == TEXT("remove_curve"))   { return OpRemoveCurve(Params); }
	if (Operation == TEXT("add_key"))        { return OpAddKey(Params); }
	if (Operation == TEXT("add_notify"))     { return OpAddNotify(Params); }
	if (Operation == TEXT("remove_notify"))  { return OpRemoveNotify(Params); }
	if (Operation == TEXT("set_frame_rate")) { return OpSetFrameRate(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: list, get_info, add_curve, remove_curve, add_key, add_notify, remove_notify, set_frame_rate"), *Operation));
}

FMCPToolResult FMCPTool_AnimSequence::OpList(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("list: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimSequence::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_info: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimSequence::OpAddCurve(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_curve: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimSequence::OpRemoveCurve(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove_curve: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimSequence::OpAddKey(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_key: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimSequence::OpAddNotify(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_notify: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimSequence::OpRemoveNotify(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove_notify: not implemented yet"));
}

FMCPToolResult FMCPTool_AnimSequence::OpSetFrameRate(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_frame_rate: not implemented yet"));
}
