// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_Skeleton.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"

#include "Animation/Skeleton.h"
#include "Animation/BlendProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "ReferenceSkeleton.h"

// =====================================================================
// File-local helpers (no header members — keeps ABI stable for Live Coding)
// =====================================================================
namespace
{
	/** Load a USkeleton from a content path, or nullptr. */
	USkeleton* LoadSkeletonAsset(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		return Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(Path));
	}

	/** Load a USkeletalMesh from a content path, or nullptr. */
	USkeletalMesh* LoadSkeletalMeshAsset(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		return Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(Path));
	}

	/**
	 * Resolve a FReferenceSkeleton for an asset that may be either a USkeleton or a
	 * USkeletalMesh. Returns nullptr if the asset is neither (OutResolvedKind left untouched).
	 */
	const FReferenceSkeleton* ResolveReferenceSkeleton(UObject* Asset, FString& OutResolvedKind)
	{
		if (USkeleton* Skeleton = Cast<USkeleton>(Asset))
		{
			OutResolvedKind = TEXT("Skeleton");
			return &Skeleton->GetReferenceSkeleton();
		}
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset))
		{
			OutResolvedKind = TEXT("SkeletalMesh");
			return &Mesh->GetRefSkeleton();
		}
		return nullptr;
	}

	/** Build a JSON object describing a socket's relative transform. */
	TSharedPtr<FJsonObject> SocketToJson(const USkeletalMeshSocket* Socket)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		Obj->SetStringField(TEXT("bone"), Socket->BoneName.ToString());
		// Reuse the shared transform serializers (matches BuildActorInfoWithTransformJson).
		Obj->SetObjectField(TEXT("relative_location"), UnrealClaudeJsonUtils::VectorToJson(Socket->RelativeLocation));
		Obj->SetObjectField(TEXT("relative_rotation"), UnrealClaudeJsonUtils::RotatorToJson(Socket->RelativeRotation));
		Obj->SetObjectField(TEXT("relative_scale"), UnrealClaudeJsonUtils::VectorToJson(Socket->RelativeScale));
		return Obj;
	}

	/** Map a retarget-mode string to EBoneTranslationRetargetingMode. Returns false if unknown. */
	bool ParseRetargetMode(const FString& ModeStr, EBoneTranslationRetargetingMode::Type& OutMode)
	{
		if (ModeStr.Equals(TEXT("Animation"), ESearchCase::IgnoreCase))         { OutMode = EBoneTranslationRetargetingMode::Animation; return true; }
		if (ModeStr.Equals(TEXT("Skeleton"), ESearchCase::IgnoreCase))          { OutMode = EBoneTranslationRetargetingMode::Skeleton; return true; }
		if (ModeStr.Equals(TEXT("AnimationScaled"), ESearchCase::IgnoreCase))   { OutMode = EBoneTranslationRetargetingMode::AnimationScaled; return true; }
		if (ModeStr.Equals(TEXT("AnimationRelative"), ESearchCase::IgnoreCase)) { OutMode = EBoneTranslationRetargetingMode::AnimationRelative; return true; }
		if (ModeStr.Equals(TEXT("OrientAndScale"), ESearchCase::IgnoreCase))    { OutMode = EBoneTranslationRetargetingMode::OrientAndScale; return true; }
		return false;
	}

	/** Friendly string for a blend-profile mode. */
	FString BlendProfileModeToString(EBlendProfileMode Mode)
	{
		switch (Mode)
		{
		case EBlendProfileMode::TimeFactor:   return TEXT("TimeFactor");
		case EBlendProfileMode::WeightFactor: return TEXT("WeightFactor");
		case EBlendProfileMode::BlendMask:    return TEXT("BlendMask");
		default:                              return TEXT("Unknown");
		}
	}
}

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

// =====================================================================
// get_info  (read-only)
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	FString SkeletonPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Err)) { return Err.GetValue(); }

	USkeleton* Skeleton = LoadSkeletonAsset(SkeletonPath);
	if (!Skeleton)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Skeleton not found (or not a USkeleton): %s"), *SkeletonPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Skeleton->GetName());
	Data->SetStringField(TEXT("path"), SkeletonPath);

	// Bones.
	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	Data->SetNumberField(TEXT("bone_count"), RefSkel.GetNum());

	// Sockets.
	TArray<TSharedPtr<FJsonValue>> SocketArray;
	for (const USkeletalMeshSocket* Socket : Skeleton->Sockets)
	{
		if (Socket)
		{
			SocketArray.Add(MakeShared<FJsonValueObject>(SocketToJson(Socket)));
		}
	}
	Data->SetNumberField(TEXT("socket_count"), SocketArray.Num());
	Data->SetArrayField(TEXT("sockets"), SocketArray);

	// Curve metadata names.
	TArray<FName> CurveNames;
	Skeleton->GetCurveMetaDataNames(CurveNames);
	TArray<TSharedPtr<FJsonValue>> CurveArray;
	for (const FName& CurveName : CurveNames)
	{
		CurveArray.Add(MakeShared<FJsonValueString>(CurveName.ToString()));
	}
	Data->SetNumberField(TEXT("curve_count"), CurveArray.Num());
	Data->SetArrayField(TEXT("curves"), CurveArray);

	// Virtual bones.
	TArray<TSharedPtr<FJsonValue>> VirtualBoneArray;
	for (const FVirtualBone& VBone : Skeleton->GetVirtualBones())
	{
		TSharedPtr<FJsonObject> VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("name"), VBone.VirtualBoneName.ToString());
		VObj->SetStringField(TEXT("source_bone"), VBone.SourceBoneName.ToString());
		VObj->SetStringField(TEXT("target_bone"), VBone.TargetBoneName.ToString());
		VirtualBoneArray.Add(MakeShared<FJsonValueObject>(VObj));
	}
	Data->SetNumberField(TEXT("virtual_bone_count"), VirtualBoneArray.Num());
	Data->SetArrayField(TEXT("virtual_bones"), VirtualBoneArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Skeleton '%s': %d bone(s), %d socket(s), %d curve(s), %d virtual bone(s)"),
			*Skeleton->GetName(), RefSkel.GetNum(), SocketArray.Num(), CurveArray.Num(), VirtualBoneArray.Num()), Data);
}

// =====================================================================
// list_bones  (read-only; accepts skeleton or skeletal mesh)
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpListBones(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	FString ResolvedKind;
	const FReferenceSkeleton* RefSkel = ResolveReferenceSkeleton(Asset, ResolvedKind);
	if (!RefSkel)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Asset at %s is neither a USkeleton nor a USkeletalMesh"), *AssetPath));
	}

	const TArray<FMeshBoneInfo>& BoneInfo = RefSkel->GetRefBoneInfo();
	TArray<TSharedPtr<FJsonValue>> BoneArray;
	for (int32 BoneIndex = 0; BoneIndex < BoneInfo.Num(); ++BoneIndex)
	{
		const FMeshBoneInfo& Info = BoneInfo[BoneIndex];
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetNumberField(TEXT("index"), BoneIndex);
		BoneObj->SetStringField(TEXT("name"), Info.Name.ToString());
		BoneObj->SetNumberField(TEXT("parent_index"), Info.ParentIndex);
		BoneObj->SetStringField(TEXT("parent_name"),
			(Info.ParentIndex >= 0 && BoneInfo.IsValidIndex(Info.ParentIndex))
				? BoneInfo[Info.ParentIndex].Name.ToString()
				: FString());
		BoneArray.Add(MakeShared<FJsonValueObject>(BoneObj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), AssetPath);
	Data->SetStringField(TEXT("asset_kind"), ResolvedKind);
	Data->SetNumberField(TEXT("bone_count"), BoneArray.Num());
	Data->SetArrayField(TEXT("bones"), BoneArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("%s '%s': %d bone(s)"), *ResolvedKind, *Asset->GetName(), BoneArray.Num()), Data);
}

// =====================================================================
// add_socket
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpAddSocket(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	TOptional<FMCPToolResult> Err;
	FString SocketName, BoneName;
	if (!ExtractRequiredString(Params, TEXT("socket_name"), SocketName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("bone_name"), BoneName, Err)) { return Err.GetValue(); }

	const FString MeshPath = ExtractOptionalString(Params, TEXT("skeletal_mesh_path"));
	const FString SkeletonPath = ExtractOptionalString(Params, TEXT("skeleton_path"));

	// Determine the owner: mesh if 'skeletal_mesh_path' provided, else skeleton.
	UObject* Owner = nullptr;
	USkeletalMesh* OwnerMesh = nullptr;
	USkeleton* OwnerSkeleton = nullptr;
	const FReferenceSkeleton* RefSkel = nullptr;

	if (!MeshPath.IsEmpty())
	{
		OwnerMesh = LoadSkeletalMeshAsset(MeshPath);
		if (!OwnerMesh)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Skeletal mesh not found: %s"), *MeshPath));
		}
		Owner = OwnerMesh;
		RefSkel = &OwnerMesh->GetRefSkeleton();
	}
	else if (!SkeletonPath.IsEmpty())
	{
		OwnerSkeleton = LoadSkeletonAsset(SkeletonPath);
		if (!OwnerSkeleton)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
		}
		Owner = OwnerSkeleton;
		RefSkel = &OwnerSkeleton->GetReferenceSkeleton();
	}
	else
	{
		return FMCPToolResult::Error(TEXT("add_socket: provide either 'skeletal_mesh_path' or 'skeleton_path'"));
	}

	// Validate the parent bone exists.
	if (RefSkel->FindBoneIndex(FName(*BoneName)) == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Bone '%s' not found on the owner"), *BoneName));
	}

	// Reject duplicate socket names on the owner.
	const FName SocketFName(*SocketName);
	if (OwnerMesh && OwnerMesh->FindSocket(SocketFName))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Socket '%s' already exists on the mesh"), *SocketName));
	}
	if (OwnerSkeleton && OwnerSkeleton->FindSocket(SocketFName))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Socket '%s' already exists on the skeleton"), *SocketName));
	}

	Owner->Modify();

	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Owner);
	Socket->SocketName = SocketFName;
	Socket->BoneName = FName(*BoneName);

	// Optional relative transform — only override when present. The Extract* helpers return
	// the supplied default when the field is absent/invalid, so pass the socket's current
	// defaults to leave them untouched if parsing fails.
	if (Params->HasField(TEXT("relative_location")))
	{
		Socket->RelativeLocation = ExtractVectorParam(Params, TEXT("relative_location"), Socket->RelativeLocation);
	}
	if (Params->HasField(TEXT("relative_rotation")))
	{
		Socket->RelativeRotation = ExtractRotatorParam(Params, TEXT("relative_rotation"), Socket->RelativeRotation);
	}
	if (Params->HasField(TEXT("relative_scale")))
	{
		Socket->RelativeScale = ExtractScaleParam(Params, TEXT("relative_scale"), Socket->RelativeScale);
	}

	// Add to the owner's socket list.
	const FString OwnerPath = MeshPath.IsEmpty() ? SkeletonPath : MeshPath;
	if (OwnerMesh)
	{
		// AddSocket() handles the mesh-only socket list + socket map rebuild.
		OwnerMesh->AddSocket(Socket, /*bAddToSkeleton=*/false);
	}
	else
	{
		OwnerSkeleton->Sockets.Add(Socket);
	}

	Owner->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(OwnerPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Added socket in memory but failed to save asset: %s"), *OwnerPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("owner_path"), OwnerPath);
	Data->SetStringField(TEXT("owner_kind"), OwnerMesh ? TEXT("SkeletalMesh") : TEXT("Skeleton"));
	Data->SetObjectField(TEXT("socket"), SocketToJson(Socket));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added socket '%s' (bone '%s') to %s"), *SocketName, *BoneName, *OwnerPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_socket requires an editor build"));
#endif
}

// =====================================================================
// remove_socket
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpRemoveSocket(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	TOptional<FMCPToolResult> Err;
	FString SocketName;
	if (!ExtractRequiredString(Params, TEXT("socket_name"), SocketName, Err)) { return Err.GetValue(); }

	const FString MeshPath = ExtractOptionalString(Params, TEXT("skeletal_mesh_path"));
	const FString SkeletonPath = ExtractOptionalString(Params, TEXT("skeleton_path"));
	const FName SocketFName(*SocketName);

	if (!MeshPath.IsEmpty())
	{
		USkeletalMesh* Mesh = LoadSkeletalMeshAsset(MeshPath);
		if (!Mesh)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Skeletal mesh not found: %s"), *MeshPath));
		}

		TArray<TObjectPtr<USkeletalMeshSocket>>& SocketList = Mesh->GetMeshOnlySocketList();
		const int32 FoundIndex = SocketList.IndexOfByPredicate(
			[&SocketFName](const TObjectPtr<USkeletalMeshSocket>& S) { return S && S->SocketName == SocketFName; });
		if (FoundIndex == INDEX_NONE)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Socket '%s' not found on mesh %s"), *SocketName, *MeshPath));
		}

		Mesh->Modify();
		SocketList.RemoveAt(FoundIndex);
		Mesh->RebuildSocketMap();
		Mesh->MarkPackageDirty();
		if (!UEditorAssetLibrary::SaveAsset(MeshPath, /*bOnlyIfIsDirty=*/false))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Removed socket in memory but failed to save asset: %s"), *MeshPath));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("owner_path"), MeshPath);
		Data->SetStringField(TEXT("owner_kind"), TEXT("SkeletalMesh"));
		Data->SetStringField(TEXT("removed_socket"), SocketName);
		return FMCPToolResult::Success(FString::Printf(TEXT("Removed socket '%s' from mesh %s"), *SocketName, *MeshPath), Data);
	}

	if (!SkeletonPath.IsEmpty())
	{
		USkeleton* Skeleton = LoadSkeletonAsset(SkeletonPath);
		if (!Skeleton)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
		}

		const int32 FoundIndex = Skeleton->Sockets.IndexOfByPredicate(
			[&SocketFName](const TObjectPtr<USkeletalMeshSocket>& S) { return S && S->SocketName == SocketFName; });
		if (FoundIndex == INDEX_NONE)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Socket '%s' not found on skeleton %s"), *SocketName, *SkeletonPath));
		}

		Skeleton->Modify();
		Skeleton->Sockets.RemoveAt(FoundIndex);
		Skeleton->MarkPackageDirty();
		if (!UEditorAssetLibrary::SaveAsset(SkeletonPath, /*bOnlyIfIsDirty=*/false))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Removed socket in memory but failed to save asset: %s"), *SkeletonPath));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("owner_path"), SkeletonPath);
		Data->SetStringField(TEXT("owner_kind"), TEXT("Skeleton"));
		Data->SetStringField(TEXT("removed_socket"), SocketName);
		return FMCPToolResult::Success(FString::Printf(TEXT("Removed socket '%s' from skeleton %s"), *SocketName, *SkeletonPath), Data);
	}

	return FMCPToolResult::Error(TEXT("remove_socket: provide either 'skeletal_mesh_path' or 'skeleton_path'"));
#else
	return FMCPToolResult::Error(TEXT("remove_socket requires an editor build"));
#endif
}

// =====================================================================
// get_sockets  (read-only; accepts skeleton or skeletal mesh)
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpGetSockets(const TSharedRef<FJsonObject>& Params)
{
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> SocketArray;
	FString ResolvedKind;

	if (USkeleton* Skeleton = Cast<USkeleton>(Asset))
	{
		ResolvedKind = TEXT("Skeleton");
		for (const USkeletalMeshSocket* Socket : Skeleton->Sockets)
		{
			if (Socket)
			{
				SocketArray.Add(MakeShared<FJsonValueObject>(SocketToJson(Socket)));
			}
		}
	}
	else if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset))
	{
		ResolvedKind = TEXT("SkeletalMesh");
		// GetMeshOnlySocketList(): sockets defined directly on the mesh (not the skeleton).
		const TArray<TObjectPtr<USkeletalMeshSocket>>& MeshSockets = Mesh->GetMeshOnlySocketList();
		for (const USkeletalMeshSocket* Socket : MeshSockets)
		{
			if (Socket)
			{
				SocketArray.Add(MakeShared<FJsonValueObject>(SocketToJson(Socket)));
			}
		}
	}
	else
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Asset at %s is neither a USkeleton nor a USkeletalMesh"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), AssetPath);
	Data->SetStringField(TEXT("asset_kind"), ResolvedKind);
	Data->SetNumberField(TEXT("socket_count"), SocketArray.Num());
	Data->SetArrayField(TEXT("sockets"), SocketArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("%s '%s': %d socket(s)"), *ResolvedKind, *Asset->GetName(), SocketArray.Num()), Data);
}

// =====================================================================
// add_curve
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpAddCurve(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	TOptional<FMCPToolResult> Err;
	FString SkeletonPath, CurveName;
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("curve_name"), CurveName, Err)) { return Err.GetValue(); }

	USkeleton* Skeleton = LoadSkeletonAsset(SkeletonPath);
	if (!Skeleton)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
	}

	Skeleton->Modify();

	// AddCurveMetaData returns false if an entry with that name already existed.
	const bool bAdded = Skeleton->AddCurveMetaData(FName(*CurveName), /*bTransact=*/true);
	if (!bAdded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Curve metadata '%s' already exists on %s"), *CurveName, *SkeletonPath));
	}

	Skeleton->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(SkeletonPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Added curve in memory but failed to save asset: %s"), *SkeletonPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Data->SetStringField(TEXT("curve_name"), CurveName);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added curve metadata '%s' to %s"), *CurveName, *SkeletonPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_curve requires an editor build"));
#endif
}

// =====================================================================
// set_retarget
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpSetRetarget(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	TOptional<FMCPToolResult> Err;
	FString SkeletonPath, BoneName, ModeStr;
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("bone_name"), BoneName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("mode"), ModeStr, Err)) { return Err.GetValue(); }

	USkeleton* Skeleton = LoadSkeletonAsset(SkeletonPath);
	if (!Skeleton)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
	}

	const int32 BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(FName(*BoneName));
	if (BoneIndex == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Bone '%s' not found on %s"), *BoneName, *SkeletonPath));
	}

	EBoneTranslationRetargetingMode::Type Mode;
	if (!ParseRetargetMode(ModeStr, Mode))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown retarget mode '%s'. Valid: Animation, Skeleton, AnimationScaled, AnimationRelative, OrientAndScale"), *ModeStr));
	}

	Skeleton->Modify();
	Skeleton->SetBoneTranslationRetargetingMode(BoneIndex, Mode);
	Skeleton->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(SkeletonPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Set retarget mode in memory but failed to save asset: %s"), *SkeletonPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Data->SetStringField(TEXT("bone_name"), BoneName);
	Data->SetNumberField(TEXT("bone_index"), BoneIndex);
	Data->SetStringField(TEXT("mode"), ModeStr);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set retarget mode '%s' on bone '%s' of %s"), *ModeStr, *BoneName, *SkeletonPath), Data);
#else
	return FMCPToolResult::Error(TEXT("set_retarget requires an editor build"));
#endif
}

// =====================================================================
// get_blend_profiles  (read-only)
// =====================================================================
FMCPToolResult FMCPTool_Skeleton::OpGetBlendProfiles(const TSharedRef<FJsonObject>& Params)
{
	FString SkeletonPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("skeleton_path"), SkeletonPath, Err)) { return Err.GetValue(); }

	USkeleton* Skeleton = LoadSkeletonAsset(SkeletonPath);
	if (!Skeleton)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
	}

	TArray<TSharedPtr<FJsonValue>> ProfileArray;
	for (const UBlendProfile* Profile : Skeleton->BlendProfiles)
	{
		if (!Profile)
		{
			continue;
		}
		TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
		// UBlendProfile has no dedicated name field; the UObject name is the profile name.
		PObj->SetStringField(TEXT("name"), Profile->GetName());
		PObj->SetStringField(TEXT("mode"), BlendProfileModeToString(Profile->GetMode()));
		PObj->SetNumberField(TEXT("entry_count"), Profile->GetNumBlendEntries());
		ProfileArray.Add(MakeShared<FJsonValueObject>(PObj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Data->SetNumberField(TEXT("profile_count"), ProfileArray.Num());
	Data->SetArrayField(TEXT("blend_profiles"), ProfileArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Skeleton '%s': %d blend profile(s)"), *Skeleton->GetName(), ProfileArray.Num()), Data);
}
