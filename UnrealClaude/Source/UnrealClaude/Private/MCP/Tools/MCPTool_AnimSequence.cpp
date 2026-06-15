// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AnimSequence.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "EditorAssetLibrary.h"

#include "Curves/RichCurve.h"
#include "Misc/FrameRate.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimNotifies/AnimNotify.h"

#define LOCTEXT_NAMESPACE "MCPTool_AnimSequence"

// =====================================================================
// File-local helpers (no header members — keeps ABI stable for Live Coding)
// =====================================================================
namespace
{
	/** Load a UAnimSequence from a content path, or nullptr. */
	UAnimSequence* LoadAnimSequence(const FString& AnimPath)
	{
		if (AnimPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AnimPath);
		return Cast<UAnimSequence>(LoadedObject);
	}

	/** Resolve the skeleton object path string for an anim sequence (empty if none). */
	FString GetSkeletonPath(const UAnimSequence* AnimSeq)
	{
		if (!AnimSeq)
		{
			return FString();
		}
		if (const USkeleton* Skeleton = AnimSeq->GetSkeleton())
		{
			return Skeleton->GetPathName();
		}
		return FString();
	}

	/**
	 * Resolve an AnimNotify class from a user-supplied string. Accepts:
	 *  - a full object/class path (e.g. "/Game/MyNotify.MyNotify_C" or "/Script/Engine.AnimNotify_PlaySound")
	 *  - a bare class name, retried against the "/Script/Engine." namespace
	 * Returns a UClass deriving from UAnimNotify, or nullptr.
	 */
	UClass* ResolveNotifyClass(const FString& NotifyClassName)
	{
		if (NotifyClassName.IsEmpty())
		{
			return nullptr;
		}

		// Direct attempt (full path or already-loaded class name).
		UClass* Found = LoadClass<UAnimNotify>(nullptr, *NotifyClassName);
		if (!Found)
		{
			Found = FindObject<UClass>(nullptr, *NotifyClassName);
		}

		// Engine-namespace fallback for bare names like "AnimNotify_PlaySound".
		if (!Found && !NotifyClassName.Contains(TEXT(".")) && !NotifyClassName.Contains(TEXT("/")))
		{
			const FString EnginePath = FString::Printf(TEXT("/Script/Engine.%s"), *NotifyClassName);
			Found = LoadClass<UAnimNotify>(nullptr, *EnginePath);
			if (!Found)
			{
				Found = FindObject<UClass>(nullptr, *EnginePath);
			}
		}

		if (Found && Found->IsChildOf(UAnimNotify::StaticClass()))
		{
			return Found;
		}
		return nullptr;
	}

	/** True if a float curve with the given name exists in the data model. */
	bool FloatCurveExists(const UAnimSequence* AnimSeq, FName CurveName)
	{
#if WITH_EDITOR
		if (const IAnimationDataModel* Model = AnimSeq ? AnimSeq->GetDataModel() : nullptr)
		{
			for (const FFloatCurve& Curve : Model->GetFloatCurves())
			{
				if (Curve.GetName() == CurveName)
				{
					return true;
				}
			}
		}
#endif // WITH_EDITOR
		return false;
	}
}

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

// =====================================================================
// list
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpList(const TSharedRef<FJsonObject>& Params)
{
	FString SearchPath = ExtractOptionalString(Params, TEXT("search_path"));
	if (SearchPath.IsEmpty())
	{
		SearchPath = TEXT("/Game");
	}
	const FString SkeletonFilter = ExtractOptionalString(Params, TEXT("skeleton_filter"));

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter ARFilter;
	ARFilter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	ARFilter.PackagePaths.Add(FName(*SearchPath));
	ARFilter.bRecursivePaths = true;
	ARFilter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(ARFilter, Assets);

	TArray<TSharedPtr<FJsonValue>> SequenceArray;
	for (const FAssetData& Asset : Assets)
	{
		// Resolve skeleton via the asset registry "Skeleton" tag where possible (avoids loading
		// every asset); fall back to loading the sequence only when a filter is supplied and the
		// tag is missing.
		FString SkeletonPath;
		FAssetDataTagMapSharedView::FFindTagResult SkeletonTag = Asset.TagsAndValues.FindTag(TEXT("Skeleton"));
		if (SkeletonTag.IsSet())
		{
			SkeletonPath = SkeletonTag.GetValue();
		}

		if (!SkeletonFilter.IsEmpty())
		{
			// The Skeleton tag is usually an export-text reference like
			// "Skeleton'/Game/Path/SK.SK'"; normalize by checking containment of the filter path,
			// and fall back to loading the asset if the tag was absent.
			bool bMatches = !SkeletonPath.IsEmpty() && SkeletonPath.Contains(SkeletonFilter);
			if (SkeletonPath.IsEmpty())
			{
				if (UAnimSequence* Seq = Cast<UAnimSequence>(Asset.GetAsset()))
				{
					SkeletonPath = GetSkeletonPath(Seq);
					bMatches = !SkeletonPath.IsEmpty() && SkeletonPath.Contains(SkeletonFilter);
				}
			}
			if (!bMatches)
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		Obj->SetStringField(TEXT("skeleton"), SkeletonPath);
		SequenceArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("count"), SequenceArray.Num());
	Data->SetArrayField(TEXT("sequences"), SequenceArray);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Found %d anim sequence(s) under %s"), SequenceArray.Num(), *SearchPath), Data);
}

// =====================================================================
// get_info
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	FString AnimPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("anim_path"), AnimPath, Err)) { return Err.GetValue(); }

	UAnimSequence* AnimSeq = LoadAnimSequence(AnimPath);
	if (!AnimSeq)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Anim sequence not found: %s"), *AnimPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), AnimSeq->GetName());
	Data->SetStringField(TEXT("path"), AnimPath);
	Data->SetNumberField(TEXT("play_length"), AnimSeq->GetPlayLength());
	Data->SetNumberField(TEXT("frame_count"), AnimSeq->GetNumberOfSampledKeys());
	Data->SetStringField(TEXT("skeleton"), GetSkeletonPath(AnimSeq));

	// GetSamplingFrameRate() returns the per-platform TARGET frame rate (PlatformTargetFrameRate),
	// which is a separate property from the data model's sampling frame rate. The data-model rate is
	// the authoritative, mutable value that set_frame_rate changes, so report that as "frame_rate"
	// (when the editor data model is available) and expose the platform target separately.
	Data->SetNumberField(TEXT("platform_target_frame_rate"), AnimSeq->GetSamplingFrameRate().AsDecimal());

	// Curves + notifies + the authoritative frame rate require the editor-only data model.
#if WITH_EDITOR
	if (const IAnimationDataModel* Model = AnimSeq->GetDataModel())
	{
		Data->SetNumberField(TEXT("frame_rate"), Model->GetFrameRate().AsDecimal());
		Data->SetNumberField(TEXT("frame_count_source"), Model->GetNumberOfFrames());

		TArray<TSharedPtr<FJsonValue>> CurveArray;
		for (const FFloatCurve& Curve : Model->GetFloatCurves())
		{
			CurveArray.Add(MakeShared<FJsonValueString>(Curve.GetName().ToString()));
		}
		Data->SetNumberField(TEXT("float_curve_count"), CurveArray.Num());
		Data->SetArrayField(TEXT("float_curves"), CurveArray);
	}
#endif // WITH_EDITOR

	TArray<TSharedPtr<FJsonValue>> NotifyArray;
	for (int32 i = 0; i < AnimSeq->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& Event = AnimSeq->Notifies[i];
		TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
		NObj->SetNumberField(TEXT("index"), i);
		NObj->SetStringField(TEXT("name"), Event.NotifyName.ToString());
		NObj->SetNumberField(TEXT("trigger_time"), Event.GetTriggerTime());
		if (Event.Notify)
		{
			NObj->SetStringField(TEXT("class"), Event.Notify->GetClass()->GetName());
		}
		NotifyArray.Add(MakeShared<FJsonValueObject>(NObj));
	}
	Data->SetNumberField(TEXT("notify_count"), NotifyArray.Num());
	Data->SetArrayField(TEXT("notifies"), NotifyArray);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Anim sequence '%s': %.3fs, %d notifie(s)"),
			*AnimSeq->GetName(), AnimSeq->GetPlayLength(), NotifyArray.Num()), Data);
}

// =====================================================================
// add_curve
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpAddCurve(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AnimPath, CurveName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("anim_path"), AnimPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("curve_name"), CurveName, Err)) { return Err.GetValue(); }
	const bool bIsMorphTarget = ExtractOptionalBool(Params, TEXT("is_morph_target"), false);

	UAnimSequence* AnimSeq = LoadAnimSequence(AnimPath);
	if (!AnimSeq)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Anim sequence not found: %s"), *AnimPath));
	}

	const FName CurveFName(*CurveName);
	if (FloatCurveExists(AnimSeq, CurveFName))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Curve '%s' already exists on %s"), *CurveName, *AnimPath));
	}

	AnimSeq->Modify();

	IAnimationDataController& Controller = AnimSeq->GetController();
	const FAnimationCurveIdentifier CurveId(CurveFName, ERawCurveTrackTypes::RCT_Float);

	Controller.OpenBracket(LOCTEXT("AddCurveBracket", "Add float curve"));
	const bool bAdded = Controller.AddCurve(CurveId, AACF_DefaultCurve);
	Controller.CloseBracket();

	if (!bAdded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Controller failed to add curve '%s' to %s"), *CurveName, *AnimPath));
	}

	// NOTE: In UE5.7 the morph-target designation is no longer a curve flag — it is per-skeleton
	// curve metadata (FAnimCurveType::bMorphTarget). Set it on the owning skeleton so the curve
	// drives morph targets. This modifies the skeleton asset, which is saved separately below.
	bool bMorphMetadataSet = false;
	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (bIsMorphTarget)
	{
		if (Skeleton)
		{
			Skeleton->Modify();
			Skeleton->SetCurveMetaDataMorphTarget(CurveFName, true);
			Skeleton->MarkPackageDirty();
			bMorphMetadataSet = true;
		}
		// If there is no skeleton we proceed without the morph flag (reported in the result).
	}

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();

	if (!UEditorAssetLibrary::SaveAsset(AnimPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Added curve '%s' in memory but failed to save asset: %s"), *CurveName, *AnimPath));
	}
	if (bMorphMetadataSet && Skeleton)
	{
		// Persist the skeleton metadata change too; failure here is non-fatal to the curve add.
		UEditorAssetLibrary::SaveAsset(Skeleton->GetPathName(), /*bOnlyIfIsDirty=*/false);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("anim_path"), AnimPath);
	Data->SetStringField(TEXT("curve_name"), CurveName);
	Data->SetBoolField(TEXT("is_morph_target"), bMorphMetadataSet);
	if (bIsMorphTarget && !bMorphMetadataSet)
	{
		Data->SetStringField(TEXT("morph_target_warning"),
			TEXT("Curve added but morph-target metadata not set: sequence has no skeleton."));
	}
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added float curve '%s' to %s"), *CurveName, *AnimPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_curve requires an editor build"));
#endif
}

// =====================================================================
// remove_curve
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpRemoveCurve(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AnimPath, CurveName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("anim_path"), AnimPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("curve_name"), CurveName, Err)) { return Err.GetValue(); }

	UAnimSequence* AnimSeq = LoadAnimSequence(AnimPath);
	if (!AnimSeq)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Anim sequence not found: %s"), *AnimPath));
	}

	const FName CurveFName(*CurveName);
	if (!FloatCurveExists(AnimSeq, CurveFName))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Curve '%s' not found on %s"), *CurveName, *AnimPath));
	}

	AnimSeq->Modify();

	IAnimationDataController& Controller = AnimSeq->GetController();
	const FAnimationCurveIdentifier CurveId(CurveFName, ERawCurveTrackTypes::RCT_Float);

	Controller.OpenBracket(LOCTEXT("RemoveCurveBracket", "Remove float curve"));
	const bool bRemoved = Controller.RemoveCurve(CurveId);
	Controller.CloseBracket();

	if (!bRemoved)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Controller failed to remove curve '%s' from %s"), *CurveName, *AnimPath));
	}

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();

	if (!UEditorAssetLibrary::SaveAsset(AnimPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Removed curve '%s' in memory but failed to save asset: %s"), *CurveName, *AnimPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("anim_path"), AnimPath);
	Data->SetStringField(TEXT("removed_curve"), CurveName);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed float curve '%s' from %s"), *CurveName, *AnimPath), Data);
#else
	return FMCPToolResult::Error(TEXT("remove_curve requires an editor build"));
#endif
}

// =====================================================================
// add_key
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpAddKey(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AnimPath, CurveName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("anim_path"), AnimPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("curve_name"), CurveName, Err)) { return Err.GetValue(); }

	const float Time = ExtractOptionalNumber<float>(Params, TEXT("time"), 0.0f);
	const float Value = ExtractOptionalNumber<float>(Params, TEXT("value"), 0.0f);

	UAnimSequence* AnimSeq = LoadAnimSequence(AnimPath);
	if (!AnimSeq)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Anim sequence not found: %s"), *AnimPath));
	}

	const FName CurveFName(*CurveName);
	IAnimationDataController& Controller = AnimSeq->GetController();
	const FAnimationCurveIdentifier CurveId(CurveFName, ERawCurveTrackTypes::RCT_Float);

	AnimSeq->Modify();

	Controller.OpenBracket(LOCTEXT("AddKeyBracket", "Add curve key"));

	// Ensure the curve exists; add it if it does not so add_key is self-sufficient.
	if (!FloatCurveExists(AnimSeq, CurveFName))
	{
		if (!Controller.AddCurve(CurveId, AACF_DefaultCurve))
		{
			Controller.CloseBracket();
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Curve '%s' did not exist and could not be created on %s"), *CurveName, *AnimPath));
		}
	}

	const FRichCurveKey NewKey(Time, Value);
	const bool bSet = Controller.SetCurveKey(CurveId, NewKey);

	Controller.CloseBracket();

	if (!bSet)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Controller failed to set key on curve '%s' (%s)"), *CurveName, *AnimPath));
	}

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();

	if (!UEditorAssetLibrary::SaveAsset(AnimPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Set key in memory but failed to save asset: %s"), *AnimPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("anim_path"), AnimPath);
	Data->SetStringField(TEXT("curve_name"), CurveName);
	Data->SetNumberField(TEXT("time"), Time);
	Data->SetNumberField(TEXT("value"), Value);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set key on curve '%s' at t=%.4f = %.4f"), *CurveName, Time, Value), Data);
#else
	return FMCPToolResult::Error(TEXT("add_key requires an editor build"));
#endif
}

// =====================================================================
// add_notify
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpAddNotify(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AnimPath, NotifyClassName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("anim_path"), AnimPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("notify_class"), NotifyClassName, Err)) { return Err.GetValue(); }

	const float TriggerTime = ExtractOptionalNumber<float>(Params, TEXT("trigger_time"), 0.0f);
	const FString NotifyName = ExtractOptionalString(Params, TEXT("notify_name"));

	UAnimSequence* AnimSeq = LoadAnimSequence(AnimPath);
	if (!AnimSeq)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Anim sequence not found: %s"), *AnimPath));
	}

	UClass* NotifyClass = ResolveNotifyClass(NotifyClassName);
	if (!NotifyClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Could not resolve AnimNotify class '%s' (expected a UAnimNotify subclass; try a full path or '/Script/Engine.<Name>')"),
			*NotifyClassName));
	}
	if (NotifyClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("AnimNotify class '%s' is abstract and cannot be instantiated"), *NotifyClassName));
	}

	const float ClampedTime = FMath::Clamp(TriggerTime, 0.0f, AnimSeq->GetPlayLength());

	AnimSeq->Modify();

	FAnimNotifyEvent NewEvent;
	NewEvent.NotifyName = NotifyName.IsEmpty() ? FName(*NotifyClass->GetName()) : FName(*NotifyName);
	NewEvent.Notify = NewObject<UAnimNotify>(AnimSeq, NotifyClass);
	NewEvent.NotifyStateClass = nullptr;
	// Link positions the event on the timeline relative to the sequence (sets TriggerTime).
	NewEvent.Link(AnimSeq, ClampedTime);
	NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(AnimSeq->CalculateOffsetForNotify(ClampedTime));

	const int32 NewIndex = AnimSeq->Notifies.Add(NewEvent);

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();

	if (!UEditorAssetLibrary::SaveAsset(AnimPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Added notify in memory but failed to save asset: %s"), *AnimPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("anim_path"), AnimPath);
	Data->SetStringField(TEXT("notify_class"), NotifyClass->GetName());
	Data->SetStringField(TEXT("notify_name"), NewEvent.NotifyName.ToString());
	Data->SetNumberField(TEXT("trigger_time"), NewEvent.GetTriggerTime());
	Data->SetNumberField(TEXT("notify_index"), NewIndex);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added notify '%s' at t=%.4f to %s"), *NewEvent.NotifyName.ToString(), NewEvent.GetTriggerTime(), *AnimPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_notify requires an editor build"));
#endif
}

// =====================================================================
// remove_notify
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpRemoveNotify(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AnimPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("anim_path"), AnimPath, Err)) { return Err.GetValue(); }

	const int32 NotifyIndex = ExtractOptionalNumber<int32>(Params, TEXT("notify_index"), -1);

	UAnimSequence* AnimSeq = LoadAnimSequence(AnimPath);
	if (!AnimSeq)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Anim sequence not found: %s"), *AnimPath));
	}

	if (NotifyIndex < 0 || NotifyIndex >= AnimSeq->Notifies.Num())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("notify_index %d out of range (sequence has %d notif(ies))"), NotifyIndex, AnimSeq->Notifies.Num()));
	}

	const FString RemovedName = AnimSeq->Notifies[NotifyIndex].NotifyName.ToString();

	AnimSeq->Modify();
	AnimSeq->Notifies.RemoveAt(NotifyIndex);

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();

	if (!UEditorAssetLibrary::SaveAsset(AnimPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Removed notify in memory but failed to save asset: %s"), *AnimPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("anim_path"), AnimPath);
	Data->SetNumberField(TEXT("removed_index"), NotifyIndex);
	Data->SetStringField(TEXT("removed_name"), RemovedName);
	Data->SetNumberField(TEXT("remaining_count"), AnimSeq->Notifies.Num());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed notify '%s' (index %d) from %s"), *RemovedName, NotifyIndex, *AnimPath), Data);
#else
	return FMCPToolResult::Error(TEXT("remove_notify requires an editor build"));
#endif
}

// =====================================================================
// set_frame_rate
// =====================================================================
FMCPToolResult FMCPTool_AnimSequence::OpSetFrameRate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AnimPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("anim_path"), AnimPath, Err)) { return Err.GetValue(); }

	const float FrameRateValue = ExtractOptionalNumber<float>(Params, TEXT("frame_rate"), 0.0f);
	if (FrameRateValue <= 0.0f)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Invalid frame_rate %.4f: must be a positive, non-zero value"), FrameRateValue));
	}

	UAnimSequence* AnimSeq = LoadAnimSequence(AnimPath);
	if (!AnimSeq)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Anim sequence not found: %s"), *AnimPath));
	}

	// FFrameRate is a rational Numerator/Denominator. Whole fps map cleanly to (N, 1); for
	// fractional inputs scale to a denominator of 1000 so e.g. 29.97 -> (29970, 1000).
	FFrameRate NewRate;
	const float Rounded = FMath::RoundToFloat(FrameRateValue);
	if (FMath::IsNearlyEqual(FrameRateValue, Rounded, KINDA_SMALL_NUMBER))
	{
		NewRate = FFrameRate(FMath::RoundToInt(FrameRateValue), 1);
	}
	else
	{
		NewRate = FFrameRate(FMath::RoundToInt(FrameRateValue * 1000.0f), 1000);
	}

	AnimSeq->Modify();

	const FFrameRate PreviousRate = AnimSeq->GetDataModel() ? AnimSeq->GetDataModel()->GetFrameRate() : FFrameRate();

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(LOCTEXT("SetFrameRateBracket", "Set frame rate"));
	Controller.SetFrameRate(NewRate);
	Controller.CloseBracket();

	// UAnimDataController::SetFrameRate silently rejects (via a logged error, not a return value or
	// exception) any rate that is not a multiple or factor of the current rate. Verify the model
	// actually changed before claiming success, so we never report a no-op as a success.
	const IAnimationDataModel* Model = AnimSeq->GetDataModel();
	const FFrameRate ResultRate = Model ? Model->GetFrameRate() : NewRate;
	if (!Model || ResultRate != NewRate)
	{
		// Asset was not saved, so the rejected change does not persist; the in-memory Modify() is
		// harmless without a save.
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Frame rate %.4f fps was rejected: it must be an integer multiple or factor of the ")
			TEXT("current rate %.4f fps (e.g. 15, 30, 60, 120 for a 30 fps asset). The asset is unchanged."),
			NewRate.AsDecimal(), PreviousRate.AsDecimal()));
	}

	AnimSeq->PostEditChange();
	AnimSeq->MarkPackageDirty();

	if (!UEditorAssetLibrary::SaveAsset(AnimPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Set frame rate in memory but failed to save asset: %s"), *AnimPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("anim_path"), AnimPath);
	Data->SetNumberField(TEXT("frame_rate"), ResultRate.AsDecimal());
	Data->SetNumberField(TEXT("numerator"), ResultRate.Numerator);
	Data->SetNumberField(TEXT("denominator"), ResultRate.Denominator);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set frame rate of %s to %.4f fps (%d/%d)"),
			*AnimPath, ResultRate.AsDecimal(), ResultRate.Numerator, ResultRate.Denominator), Data);
#else
	return FMCPToolResult::Error(TEXT("set_frame_rate requires an editor build"));
#endif
}

#undef LOCTEXT_NAMESPACE
