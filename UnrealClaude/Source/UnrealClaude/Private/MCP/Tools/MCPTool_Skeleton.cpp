// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Skeleton.h"

FMCPToolInfo FMCPTool_Skeleton::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("skeleton");
	Info.Description = TEXT(
		"Manage Unreal Engine Skeletons (info, bones, sockets, curves, retargeting, blend profiles).\n\n"
		"Operations (set 'operation'):\n"
		"- 'get_info': Details for the skeleton at 'skeleton_path' (bone count, sockets, curves, virtual bones)\n"
		"- 'list_bones': List the bone hierarchy for the skeleton/mesh at 'asset_path'\n"
		"- 'add_socket': Add socket 'socket_name' attached to 'bone_name' (optional relative transform)\n"
		"- 'remove_socket': Remove socket 'socket_name'\n"
		"- 'get_sockets': List all sockets on the skeleton/mesh at 'asset_path'\n"
		"- 'add_curve': Add curve metadata 'curve_name' to 'skeleton_path'\n"
		"- 'set_retarget': Set the retargeting 'mode' for 'bone_name' (e.g., 'Animation', 'Skeleton', 'AnimationScaled')\n"
		"- 'get_blend_profiles': List blend profiles defined on 'skeleton_path'\n\n"
		"Asset paths are content paths (e.g., '/Game/Characters/Hero_Skeleton').\n\n"
		"Returns: operation-specific data (skeleton info, bone list, socket list, or the modified element)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: get_info, list_bones, add_socket, remove_socket, get_sockets, add_curve, set_retarget, get_blend_profiles"), true),
		FMCPToolParameter(TEXT("skeleton_path"), TEXT("string"), TEXT("Content path of the skeleton (for get_info/add_curve/set_retarget/get_blend_profiles)"), false),
		FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("For list_bones/get_sockets: skeleton or skeletal-mesh path to inspect"), false),
		FMCPToolParameter(TEXT("skeletal_mesh_path"), TEXT("string"), TEXT("For add_socket/remove_socket: skeletal mesh that owns the socket"), false),
		FMCPToolParameter(TEXT("socket_name"), TEXT("string"), TEXT("For add_socket/remove_socket: the socket name"), false),
		FMCPToolParameter(TEXT("bone_name"), TEXT("string"), TEXT("For add_socket: parent bone; for set_retarget: the bone to configure"), false),
		FMCPToolParameter(TEXT("relative_location"), TEXT("object"), TEXT("For 'add_socket': optional relative location { x, y, z }"), false),
		FMCPToolParameter(TEXT("relative_rotation"), TEXT("object"), TEXT("For 'add_socket': optional relative rotation { pitch, yaw, roll }"), false),
		FMCPToolParameter(TEXT("relative_scale"), TEXT("object"), TEXT("For 'add_socket': optional relative scale { x, y, z }"), false),
		FMCPToolParameter(TEXT("curve_name"), TEXT("string"), TEXT("For 'add_curve': the curve metadata name to add"), false),
		FMCPToolParameter(TEXT("mode"), TEXT("string"), TEXT("For 'set_retarget': retargeting mode (Animation, Skeleton, AnimationScaled, AnimationRelative, OrientAndScale)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_Skeleton::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("get_info"))           { return OpGetInfo(Params); }
	if (Operation == TEXT("list_bones"))         { return OpListBones(Params); }
	if (Operation == TEXT("add_socket"))         { return OpAddSocket(Params); }
	if (Operation == TEXT("remove_socket"))      { return OpRemoveSocket(Params); }
	if (Operation == TEXT("get_sockets"))        { return OpGetSockets(Params); }
	if (Operation == TEXT("add_curve"))          { return OpAddCurve(Params); }
	if (Operation == TEXT("set_retarget"))       { return OpSetRetarget(Params); }
	if (Operation == TEXT("get_blend_profiles")) { return OpGetBlendProfiles(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: get_info, list_bones, add_socket, remove_socket, get_sockets, add_curve, set_retarget, get_blend_profiles"), *Operation));
}

FMCPToolResult FMCPTool_Skeleton::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_info: not implemented yet"));
}

FMCPToolResult FMCPTool_Skeleton::OpListBones(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("list_bones: not implemented yet"));
}

FMCPToolResult FMCPTool_Skeleton::OpAddSocket(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_socket: not implemented yet"));
}

FMCPToolResult FMCPTool_Skeleton::OpRemoveSocket(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("remove_socket: not implemented yet"));
}

FMCPToolResult FMCPTool_Skeleton::OpGetSockets(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_sockets: not implemented yet"));
}

FMCPToolResult FMCPTool_Skeleton::OpAddCurve(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_curve: not implemented yet"));
}

FMCPToolResult FMCPTool_Skeleton::OpSetRetarget(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_retarget: not implemented yet"));
}

FMCPToolResult FMCPTool_Skeleton::OpGetBlendProfiles(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_blend_profiles: not implemented yet"));
}
