// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_AnimMontage.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

#include "EditorAssetLibrary.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AlphaBlend.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/AnimMontageFactory.h"
#endif // WITH_EDITOR

// =====================================================================
// File-local helpers (no header members — keeps ABI stable for Live Coding)
// =====================================================================
namespace
{
	/** Load a UAnimMontage from a content path, or nullptr. */
	UAnimMontage* LoadMontage(const FString& MontagePath)
	{
		if (MontagePath.IsEmpty())
		{
			return nullptr;
		}
		UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(MontagePath);
		return Cast<UAnimMontage>(LoadedObject);
	}

	/**
	 * Resolve an AnimNotify class from a user-supplied string. Accepts a full object/class
	 * path (e.g. "/Game/MyNotify.MyNotify_C" or "/Script/Engine.AnimNotify_PlaySound") or a
	 * bare class name retried against the "/Script/Engine." namespace. Returns a UClass
	 * deriving from UAnimNotify, or nullptr. (Mirrors MCPTool_AnimSequence's resolver so the
	 * same notify-class inputs work on both tools.)
	 */
	UClass* ResolveNotifyClass(const FString& NotifyClassName)
	{
		if (NotifyClassName.IsEmpty())
		{
			return nullptr;
		}

		UClass* Found = LoadClass<UAnimNotify>(nullptr, *NotifyClassName);
		if (!Found)
		{
			Found = FindObject<UClass>(nullptr, *NotifyClassName);
		}

		if (!Found && !NotifyClassName.Contains(TEXT(".")) && !NotifyClassName.Contains(TEXT("/")))
		{
			const FString EnginePath = FString::Printf(TEXT("/Script/Engine.%s"), *NotifyClassName);
			Found = LoadClass<UAnimNotify>(nullptr, *EnginePath);
			if (!Found)
			{
				Found = FindObject<UClass>(nullptr, *EnginePath);
			}
		}

		if (Found && Found->IsChildOf(UAnimNotify::StaticClass()) && !Found->HasAnyClassFlags(CLASS_Abstract))
		{
			return Found;
		}
		return nullptr;
	}

	/** Human-readable name for an EAlphaBlendOption value. */
	FString BlendOptionToString(EAlphaBlendOption Option)
	{
		switch (Option)
		{
		case EAlphaBlendOption::Linear:         return TEXT("Linear");
		case EAlphaBlendOption::Cubic:          return TEXT("Cubic");
		case EAlphaBlendOption::HermiteCubic:   return TEXT("HermiteCubic");
		case EAlphaBlendOption::Sinusoidal:     return TEXT("Sinusoidal");
		case EAlphaBlendOption::QuadraticInOut: return TEXT("QuadraticInOut");
		case EAlphaBlendOption::CubicInOut:     return TEXT("CubicInOut");
		case EAlphaBlendOption::QuarticInOut:   return TEXT("QuarticInOut");
		case EAlphaBlendOption::QuinticInOut:   return TEXT("QuinticInOut");
		case EAlphaBlendOption::CircularIn:     return TEXT("CircularIn");
		case EAlphaBlendOption::CircularOut:    return TEXT("CircularOut");
		case EAlphaBlendOption::CircularInOut:  return TEXT("CircularInOut");
		case EAlphaBlendOption::ExpIn:          return TEXT("ExpIn");
		case EAlphaBlendOption::ExpOut:         return TEXT("ExpOut");
		case EAlphaBlendOption::ExpInOut:       return TEXT("ExpInOut");
		case EAlphaBlendOption::Custom:         return TEXT("Custom");
		default:                                return TEXT("Unknown");
		}
	}

	/** Map a blend-option string (case-insensitive) to EAlphaBlendOption. Returns false if unrecognized. */
	bool ParseBlendOption(const FString& In, EAlphaBlendOption& Out)
	{
		struct FPair { const TCHAR* Name; EAlphaBlendOption Value; };
		static const FPair Pairs[] = {
			{ TEXT("Linear"),         EAlphaBlendOption::Linear },
			{ TEXT("Cubic"),          EAlphaBlendOption::Cubic },
			{ TEXT("HermiteCubic"),   EAlphaBlendOption::HermiteCubic },
			{ TEXT("Sinusoidal"),     EAlphaBlendOption::Sinusoidal },
			{ TEXT("QuadraticInOut"), EAlphaBlendOption::QuadraticInOut },
			{ TEXT("CubicInOut"),     EAlphaBlendOption::CubicInOut },
			{ TEXT("QuarticInOut"),   EAlphaBlendOption::QuarticInOut },
			{ TEXT("QuinticInOut"),   EAlphaBlendOption::QuinticInOut },
			{ TEXT("CircularIn"),     EAlphaBlendOption::CircularIn },
			{ TEXT("CircularOut"),    EAlphaBlendOption::CircularOut },
			{ TEXT("CircularInOut"),  EAlphaBlendOption::CircularInOut },
			{ TEXT("ExpIn"),          EAlphaBlendOption::ExpIn },
			{ TEXT("ExpOut"),         EAlphaBlendOption::ExpOut },
			{ TEXT("ExpInOut"),       EAlphaBlendOption::ExpInOut },
			{ TEXT("Custom"),         EAlphaBlendOption::Custom },
		};
		for (const FPair& P : Pairs)
		{
			if (In.Equals(P.Name, ESearchCase::IgnoreCase))
			{
				Out = P.Value;
				return true;
			}
		}
		return false;
	}

	/** Normalize a content folder path: ensure /Game prefix and no trailing slash. */
	FString NormalizeFolderPath(const FString& InPath)
	{
		FString Clean = InPath;
		if (!Clean.StartsWith(TEXT("/")))
		{
			Clean = TEXT("/Game/") + Clean;
		}
		if (Clean.EndsWith(TEXT("/")))
		{
			Clean = Clean.LeftChop(1);
		}
		return Clean;
	}

	/** Build a JSON summary object for a montage (length, sections, slots, notifies, blend). */
	TSharedPtr<FJsonObject> BuildMontageInfoJson(UAnimMontage* Montage, const FString& MontagePath)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("name"), Montage->GetName());
		Data->SetStringField(TEXT("path"), MontagePath);
		Data->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());

		// Sections.
		TArray<TSharedPtr<FJsonValue>> SectionArray;
		for (const FCompositeSection& Section : Montage->CompositeSections)
		{
			TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
			SObj->SetNumberField(TEXT("start_time"), Section.GetTime());
			SObj->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
			SectionArray.Add(MakeShared<FJsonValueObject>(SObj));
		}
		Data->SetArrayField(TEXT("sections"), SectionArray);

		// Slot tracks.
		TArray<TSharedPtr<FJsonValue>> SlotArray;
		for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
		{
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetNumberField(TEXT("index"), i);
			SlotObj->SetStringField(TEXT("slot_name"), Montage->SlotAnimTracks[i].SlotName.ToString());
			SlotObj->SetNumberField(TEXT("segment_count"), Montage->SlotAnimTracks[i].AnimTrack.AnimSegments.Num());
			SlotArray.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
		Data->SetArrayField(TEXT("slots"), SlotArray);

		// Notifies (including branching points).
		TArray<TSharedPtr<FJsonValue>> NotifyArray;
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
			NObj->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
			NObj->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
			NObj->SetNumberField(TEXT("duration"), Notify.GetDuration());
			NObj->SetBoolField(TEXT("is_state"), Notify.NotifyStateClass != nullptr);
			NObj->SetBoolField(TEXT("is_branching_point"),
				Notify.MontageTickType == EMontageNotifyTickType::BranchingPoint);
			NObj->SetNumberField(TEXT("track_index"), Notify.TrackIndex);
			if (Notify.Notify)
			{
				NObj->SetStringField(TEXT("notify_class"), Notify.Notify->GetClass()->GetPathName());
			}
			else if (Notify.NotifyStateClass)
			{
				NObj->SetStringField(TEXT("notify_class"), Notify.NotifyStateClass->GetClass()->GetPathName());
			}
			NotifyArray.Add(MakeShared<FJsonValueObject>(NObj));
		}
		Data->SetArrayField(TEXT("notifies"), NotifyArray);

		// Blend settings.
		Data->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
		Data->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
		Data->SetStringField(TEXT("blend_in_option"), BlendOptionToString(Montage->BlendIn.GetBlendOption()));
		Data->SetStringField(TEXT("blend_out_option"), BlendOptionToString(Montage->BlendOut.GetBlendOption()));

		Data->SetNumberField(TEXT("section_count"), SectionArray.Num());
		Data->SetNumberField(TEXT("slot_count"), SlotArray.Num());
		Data->SetNumberField(TEXT("notify_count"), NotifyArray.Num());

		return Data;
	}
}

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

// =====================================================================
// create
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpCreate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	const FString AnimSequencePath = ExtractOptionalString(Params, TEXT("anim_sequence_path"));
	const FString SkeletonPath = ExtractOptionalString(Params, TEXT("skeleton_path"));

	FString DestPath, MontageName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("dest_path"), DestPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("montage_name"), MontageName, Err)) { return Err.GetValue(); }

	if (AnimSequencePath.IsEmpty() && SkeletonPath.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("create: provide either 'anim_sequence_path' (preferred) or 'skeleton_path'"));
	}

	const FString CleanDest = NormalizeFolderPath(DestPath);
	const FString FullAssetPath = CleanDest / MontageName;

	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath) && UEditorAssetLibrary::LoadAsset(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Asset already exists at: %s"), *FullAssetPath));
	}

	// Resolve the source sequence (preferred) and/or skeleton.
	UAnimSequence* SourceSequence = nullptr;
	USkeleton* Skeleton = nullptr;

	if (!AnimSequencePath.IsEmpty())
	{
		SourceSequence = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimSequencePath));
		if (!SourceSequence)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Source anim sequence not found (or not a UAnimSequence): %s"), *AnimSequencePath));
		}
		Skeleton = SourceSequence->GetSkeleton();
		if (!Skeleton)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Source anim sequence has no skeleton: %s"), *AnimSequencePath));
		}
	}
	else
	{
		Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
		if (!Skeleton)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Skeleton not found (or not a USkeleton): %s"), *SkeletonPath));
		}
	}

	// Create via UAnimMontageFactory through AssetTools (matches the editor's create path and
	// the VibeUE reference). The factory wires the SourceAnimation segment + default section.
	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->SourceAnimation = SourceSequence; // may be null for an empty montage

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UAnimMontage* NewMontage = Cast<UAnimMontage>(
		AssetTools.CreateAsset(MontageName, CleanDest, UAnimMontage::StaticClass(), Factory));

	if (!NewMontage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create montage: %s"), *FullAssetPath));
	}

	NewMontage->Modify();

	// For an empty (skeleton-only) montage the factory does not add a slot/section, so ensure both.
	if (NewMontage->SlotAnimTracks.Num() == 0)
	{
		FSlotAnimationTrack& Track = NewMontage->SlotAnimTracks.AddDefaulted_GetRef();
		Track.SlotName = FName(TEXT("DefaultSlot"));
	}
	// Guarantee a starting section at T=0 (UAnimMontageFactory::EnsureStartingSection does this
	// for the sequence path; replicate for the empty path).
	if (NewMontage->CompositeSections.Num() == 0)
	{
		NewMontage->AddAnimCompositeSection(FName(TEXT("Default")), 0.0f);
	}

	// Default blend settings, matching the editor defaults.
	NewMontage->BlendIn.SetBlendTime(0.25f);
	NewMontage->BlendOut.SetBlendTime(0.25f);
	NewMontage->BlendOutTriggerTime = -1.0f;

	NewMontage->PostEditChange();
	NewMontage->MarkPackageDirty();

	if (!UEditorAssetLibrary::SaveAsset(FullAssetPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Created montage in memory but failed to save asset: %s"), *FullAssetPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), FullAssetPath);
	Data->SetStringField(TEXT("name"), MontageName);
	Data->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
	Data->SetNumberField(TEXT("play_length"), NewMontage->GetPlayLength());
	if (SourceSequence)
	{
		Data->SetStringField(TEXT("source_sequence"), AnimSequencePath);
	}
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created montage '%s' (length %.3fs)"), *FullAssetPath, NewMontage->GetPlayLength()), Data);
#else
	return FMCPToolResult::Error(TEXT("create requires an editor build"));
#endif
}

// =====================================================================
// get_info  (read-only — load + serialize)
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpGetInfo(const TSharedRef<FJsonObject>& Params)
{
	FString MontagePath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Err)) { return Err.GetValue(); }

	UAnimMontage* Montage = LoadMontage(MontagePath);
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));
	}

	TSharedPtr<FJsonObject> Data = BuildMontageInfoJson(Montage, MontagePath);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Montage '%s': length %.3fs, %d section(s), %d slot(s), %d notify(ies)"),
			*Montage->GetName(),
			Montage->GetPlayLength(),
			Montage->CompositeSections.Num(),
			Montage->SlotAnimTracks.Num(),
			Montage->Notifies.Num()), Data);
}

// =====================================================================
// add_section
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpAddSection(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MontagePath, SectionName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("section_name"), SectionName, Err)) { return Err.GetValue(); }
	const float StartTime = ExtractOptionalNumber<float>(Params, TEXT("start_time"), 0.0f);

	UAnimMontage* Montage = LoadMontage(MontagePath);
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));
	}

	if (StartTime < 0.0f || StartTime > Montage->GetPlayLength())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("start_time %.3f is out of range [0, %.3f]"), StartTime, Montage->GetPlayLength()));
	}

	Montage->Modify();

	// AddAnimCompositeSection returns INDEX_NONE if the name is not unique.
	const int32 NewIndex = Montage->AddAnimCompositeSection(FName(*SectionName), StartTime);
	if (NewIndex == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to add section '%s' (name must be unique within the montage)"), *SectionName));
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(MontagePath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Added section but failed to save asset: %s"), *MontagePath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("montage_path"), MontagePath);
	Data->SetStringField(TEXT("section_name"), SectionName);
	Data->SetNumberField(TEXT("start_time"), StartTime);
	Data->SetNumberField(TEXT("section_index"), NewIndex);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added section '%s' at %.3fs (index %d)"), *SectionName, StartTime, NewIndex), Data);
#else
	return FMCPToolResult::Error(TEXT("add_section requires an editor build"));
#endif
}

// =====================================================================
// remove_section
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpRemoveSection(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MontagePath, SectionName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("section_name"), SectionName, Err)) { return Err.GetValue(); }

	UAnimMontage* Montage = LoadMontage(MontagePath);
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));
	}

	const int32 SectionIndex = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIndex == INDEX_NONE)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Section not found: %s"), *SectionName));
	}

	Montage->Modify();

	// DeleteAnimCompositeSection performs the removal + internal section relinking.
	if (!Montage->DeleteAnimCompositeSection(SectionIndex))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to remove section '%s'"), *SectionName));
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(MontagePath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Removed section but failed to save asset: %s"), *MontagePath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("montage_path"), MontagePath);
	Data->SetStringField(TEXT("removed_section"), SectionName);
	Data->SetNumberField(TEXT("section_count"), Montage->CompositeSections.Num());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Removed section '%s' from %s"), *SectionName, *MontagePath), Data);
#else
	return FMCPToolResult::Error(TEXT("remove_section requires an editor build"));
#endif
}

// =====================================================================
// set_slot
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpSetSlot(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MontagePath, SlotName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("slot_name"), SlotName, Err)) { return Err.GetValue(); }
	const int32 TrackIndex = ExtractOptionalNumber<int32>(Params, TEXT("track_index"), 0);

	UAnimMontage* Montage = LoadMontage(MontagePath);
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));
	}

	if (TrackIndex < 0 || TrackIndex >= Montage->SlotAnimTracks.Num())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("track_index %d is out of range [0, %d)"), TrackIndex, Montage->SlotAnimTracks.Num()));
	}

	Montage->Modify();
	const FString OldName = Montage->SlotAnimTracks[TrackIndex].SlotName.ToString();
	Montage->SlotAnimTracks[TrackIndex].SlotName = FName(*SlotName);

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(MontagePath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Renamed slot but failed to save asset: %s"), *MontagePath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("montage_path"), MontagePath);
	Data->SetNumberField(TEXT("track_index"), TrackIndex);
	Data->SetStringField(TEXT("old_slot_name"), OldName);
	Data->SetStringField(TEXT("slot_name"), SlotName);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Renamed slot track %d from '%s' to '%s'"), TrackIndex, *OldName, *SlotName), Data);
#else
	return FMCPToolResult::Error(TEXT("set_slot requires an editor build"));
#endif
}

// =====================================================================
// add_notify
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpAddNotify(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MontagePath, NotifyClass;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("notify_class"), NotifyClass, Err)) { return Err.GetValue(); }
	const float TriggerTime = ExtractOptionalNumber<float>(Params, TEXT("trigger_time"), 0.0f);
	const FString NotifyName = ExtractOptionalString(Params, TEXT("notify_name"));

	UAnimMontage* Montage = LoadMontage(MontagePath);
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));
	}

	if (TriggerTime < 0.0f || TriggerTime > Montage->GetPlayLength())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("trigger_time %.3f is out of range [0, %.3f]"), TriggerTime, Montage->GetPlayLength()));
	}

	// Resolve the notify class (full path, already-loaded class, or bare engine-namespace name).
	UClass* NotifyUClass = ResolveNotifyClass(NotifyClass);
	if (!NotifyUClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Invalid notify class '%s' (must resolve to a concrete UAnimNotify subclass; ")
			TEXT("accepts a full path or a bare engine name like 'AnimNotify_PlaySound')"), *NotifyClass));
	}

	Montage->Modify();

	FAnimNotifyEvent& NewNotify = Montage->Notifies.AddDefaulted_GetRef();
	NewNotify.NotifyName = NotifyName.IsEmpty() ? FName(*NotifyUClass->GetName()) : FName(*NotifyName);

	UAnimNotify* NotifyObj = NewObject<UAnimNotify>(Montage, NotifyUClass, NAME_None, RF_Transactional);
	NewNotify.Notify = NotifyObj;
	NewNotify.Link(Montage, TriggerTime);
	NewNotify.TriggerTimeOffset = 0.0f;
	NewNotify.TrackIndex = 0;

	// RefreshCacheData() re-sorts the notifies and rebuilds the branching-point marker cache.
	// UAnimMontage::PostEditChangeProperty does NOT call RefreshCacheData() in 5.7, so we must
	// call it explicitly; PostEditChange() then handles the remaining bookkeeping.
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(MontagePath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Added notify but failed to save asset: %s"), *MontagePath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("montage_path"), MontagePath);
	Data->SetStringField(TEXT("notify_name"), NewNotify.NotifyName.ToString());
	Data->SetStringField(TEXT("notify_class"), NotifyUClass->GetPathName());
	Data->SetNumberField(TEXT("trigger_time"), TriggerTime);
	Data->SetNumberField(TEXT("notify_count"), Montage->Notifies.Num());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added notify '%s' at %.3fs"), *NewNotify.NotifyName.ToString(), TriggerTime), Data);
#else
	return FMCPToolResult::Error(TEXT("add_notify requires an editor build"));
#endif
}

// =====================================================================
// add_branch_point
//
// In modern UE a montage "branch point" is a regular notify flagged with
// MontageTickType == BranchingPoint. RefreshBranchingPointMarkers() is private,
// but the public RefreshCacheData() rebuilds the BranchingPointMarkers cache from
// the flagged notifies. PostEditChangeProperty does NOT call RefreshCacheData() in
// 5.7, so we invoke it explicitly below.
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpAddBranchPoint(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MontagePath, NotifyName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("notify_name"), NotifyName, Err)) { return Err.GetValue(); }
	const float TriggerTime = ExtractOptionalNumber<float>(Params, TEXT("trigger_time"), 0.0f);

	UAnimMontage* Montage = LoadMontage(MontagePath);
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));
	}

	if (TriggerTime < 0.0f || TriggerTime > Montage->GetPlayLength())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("trigger_time %.3f is out of range [0, %.3f]"), TriggerTime, Montage->GetPlayLength()));
	}

	Montage->Modify();

	FAnimNotifyEvent& NewNotify = Montage->Notifies.AddDefaulted_GetRef();
	NewNotify.NotifyName = FName(*NotifyName);
	NewNotify.Link(Montage, TriggerTime);
	NewNotify.TriggerTimeOffset = 0.0f;
	NewNotify.MontageTickType = EMontageNotifyTickType::BranchingPoint;
	NewNotify.TrackIndex = 0;

	// Rebuilds BranchingPointMarkers from the flagged notify (see note above).
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(MontagePath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Added branch point but failed to save asset: %s"), *MontagePath));
	}

	// Count branching points for the response.
	int32 BranchPointCount = 0;
	for (const FAnimNotifyEvent& N : Montage->Notifies)
	{
		if (N.MontageTickType == EMontageNotifyTickType::BranchingPoint)
		{
			++BranchPointCount;
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("montage_path"), MontagePath);
	Data->SetStringField(TEXT("notify_name"), NotifyName);
	Data->SetNumberField(TEXT("trigger_time"), TriggerTime);
	Data->SetNumberField(TEXT("branch_point_count"), BranchPointCount);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added branch point '%s' at %.3fs"), *NotifyName, TriggerTime), Data);
#else
	return FMCPToolResult::Error(TEXT("add_branch_point requires an editor build"));
#endif
}

// =====================================================================
// set_blend
// =====================================================================
FMCPToolResult FMCPTool_AnimMontage::OpSetBlend(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString MontagePath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("montage_path"), MontagePath, Err)) { return Err.GetValue(); }

	UAnimMontage* Montage = LoadMontage(MontagePath);
	if (!Montage)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Montage not found: %s"), *MontagePath));
	}

	// All three inputs are optional; require at least one to be present.
	double BlendInTime = 0.0, BlendOutTime = 0.0;
	const bool bHasBlendIn = Params->TryGetNumberField(TEXT("blend_in_time"), BlendInTime);
	const bool bHasBlendOut = Params->TryGetNumberField(TEXT("blend_out_time"), BlendOutTime);
	const FString BlendOptionStr = ExtractOptionalString(Params, TEXT("blend_option"));
	const bool bHasOption = !BlendOptionStr.IsEmpty();

	if (!bHasBlendIn && !bHasBlendOut && !bHasOption)
	{
		return FMCPToolResult::Error(
			TEXT("set_blend: provide at least one of 'blend_in_time', 'blend_out_time', 'blend_option'"));
	}

	EAlphaBlendOption Option = EAlphaBlendOption::Linear;
	if (bHasOption && !ParseBlendOption(BlendOptionStr, Option))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown blend_option '%s'. Valid: Linear, Cubic, HermiteCubic, Sinusoidal, QuadraticInOut, ")
			TEXT("CubicInOut, QuarticInOut, QuinticInOut, CircularIn, CircularOut, CircularInOut, ExpIn, ExpOut, ExpInOut, Custom"),
			*BlendOptionStr));
	}

	if (bHasBlendIn && BlendInTime < 0.0)
	{
		return FMCPToolResult::Error(TEXT("blend_in_time must be >= 0"));
	}
	if (bHasBlendOut && BlendOutTime < 0.0)
	{
		return FMCPToolResult::Error(TEXT("blend_out_time must be >= 0"));
	}

	Montage->Modify();

	if (bHasBlendIn)
	{
		Montage->BlendIn.SetBlendTime(static_cast<float>(BlendInTime));
	}
	if (bHasBlendOut)
	{
		Montage->BlendOut.SetBlendTime(static_cast<float>(BlendOutTime));
	}
	if (bHasOption)
	{
		// Apply the curve option to whichever blend(s) the caller is configuring; if only the
		// option is given (no times), apply to both for predictable behavior.
		if (bHasBlendIn || (!bHasBlendIn && !bHasBlendOut))
		{
			Montage->BlendIn.SetBlendOption(Option);
		}
		if (bHasBlendOut || (!bHasBlendIn && !bHasBlendOut))
		{
			Montage->BlendOut.SetBlendOption(Option);
		}
	}

	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	if (!UEditorAssetLibrary::SaveAsset(MontagePath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Set blend but failed to save asset: %s"), *MontagePath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("montage_path"), MontagePath);
	Data->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
	Data->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
	Data->SetStringField(TEXT("blend_in_option"), BlendOptionToString(Montage->BlendIn.GetBlendOption()));
	Data->SetStringField(TEXT("blend_out_option"), BlendOptionToString(Montage->BlendOut.GetBlendOption()));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Updated blend settings on %s (in %.3fs, out %.3fs)"),
			*MontagePath, Montage->BlendIn.GetBlendTime(), Montage->BlendOut.GetBlendTime()), Data);
#else
	return FMCPToolResult::Error(TEXT("set_blend requires an editor build"));
#endif
}
