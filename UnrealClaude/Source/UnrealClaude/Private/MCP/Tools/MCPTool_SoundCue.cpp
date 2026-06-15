// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_SoundCue.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"

// SoundCue runtime + node graph
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeLooping.h"
#include "Sound/SoundNodeConcatenator.h"
#include "Sound/SoundNodeDelay.h"
#include "Sound/SoundNodeSwitch.h"
#include "Sound/SoundNodeEnveloper.h"
#include "Sound/SoundNodeDistanceCrossFade.h"
#include "Sound/SoundNodeBranch.h"
#include "Sound/SoundNodeParamCrossFade.h"
#include "Sound/SoundNodeQualityLevel.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Factories/SoundFactory.h"
#include "AssetImportTask.h"
#endif // WITH_EDITOR

// =====================================================================
// File-local helpers (no header members — keeps ABI stable for Live Coding)
// =====================================================================
namespace
{
	/** Normalize a content path to /Game-rooted, trailing-slash-free form. */
	FString NormalizeContentPath(const FString& InPath)
	{
		FString Clean = InPath.TrimStartAndEnd();
		if (!Clean.StartsWith(TEXT("/")))
		{
			Clean = TEXT("/Game/") + Clean;
		}
		while (Clean.EndsWith(TEXT("/")))
		{
			Clean = Clean.LeftChop(1);
		}
		return Clean;
	}

	/** Split a full asset path into ("/Game/Folder", "AssetName"). */
	void SplitAssetPath(const FString& FullPath, FString& OutFolder, FString& OutName)
	{
		const FString Normalized = NormalizeContentPath(FullPath);
		if (!Normalized.Split(TEXT("/"), &OutFolder, &OutName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			OutFolder = TEXT("/Game");
			OutName = Normalized;
		}
	}

	/** Load a USoundCue from a content path, or nullptr. */
	USoundCue* LoadSoundCue(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		return Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(Path));
	}

	/** Load a USoundWave from a content path, or nullptr. */
	USoundWave* LoadSoundWave(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		return Cast<USoundWave>(UEditorAssetLibrary::LoadAsset(Path));
	}

	/** Map a friendly node_type string to a concrete USoundNode subclass, or nullptr. */
	UClass* MapNodeTypeToClass(const FString& NodeType)
	{
		const FString T = NodeType.TrimStartAndEnd();
		if (T.Equals(TEXT("wave_player"), ESearchCase::IgnoreCase))        { return USoundNodeWavePlayer::StaticClass(); }
		if (T.Equals(TEXT("random"), ESearchCase::IgnoreCase))            { return USoundNodeRandom::StaticClass(); }
		if (T.Equals(TEXT("mixer"), ESearchCase::IgnoreCase))             { return USoundNodeMixer::StaticClass(); }
		if (T.Equals(TEXT("modulator"), ESearchCase::IgnoreCase))         { return USoundNodeModulator::StaticClass(); }
		if (T.Equals(TEXT("attenuation"), ESearchCase::IgnoreCase))       { return USoundNodeAttenuation::StaticClass(); }
		if (T.Equals(TEXT("looping"), ESearchCase::IgnoreCase))           { return USoundNodeLooping::StaticClass(); }
		if (T.Equals(TEXT("concatenator"), ESearchCase::IgnoreCase))      { return USoundNodeConcatenator::StaticClass(); }
		if (T.Equals(TEXT("delay"), ESearchCase::IgnoreCase))             { return USoundNodeDelay::StaticClass(); }
		if (T.Equals(TEXT("switch"), ESearchCase::IgnoreCase))            { return USoundNodeSwitch::StaticClass(); }
		if (T.Equals(TEXT("enveloper"), ESearchCase::IgnoreCase))         { return USoundNodeEnveloper::StaticClass(); }
		if (T.Equals(TEXT("distance_crossfade"), ESearchCase::IgnoreCase)){ return USoundNodeDistanceCrossFade::StaticClass(); }
		if (T.Equals(TEXT("branch"), ESearchCase::IgnoreCase))            { return USoundNodeBranch::StaticClass(); }
		if (T.Equals(TEXT("param_crossfade"), ESearchCase::IgnoreCase))   { return USoundNodeParamCrossFade::StaticClass(); }
		if (T.Equals(TEXT("quality_level"), ESearchCase::IgnoreCase))     { return USoundNodeQualityLevel::StaticClass(); }
		return nullptr;
	}

#if WITH_EDITOR
	/**
	 * Build the canonical ordered node list for index-based ops. AllNodes (editor-only) holds
	 * every node constructed via ConstructSoundNode and is the stable index space the editor
	 * graph and these ops share. FirstNode (and any of its descendants) is unioned in defensively
	 * in case a node was wired without going through ConstructSoundNode.
	 */
	void CollectOrderedNodes(USoundCue* Cue, TArray<USoundNode*>& OutNodes)
	{
		OutNodes.Reset();
		if (!Cue)
		{
			return;
		}
		for (const TObjectPtr<USoundNode>& Node : Cue->AllNodes)
		{
			if (Node && !OutNodes.Contains(Node.Get()))
			{
				OutNodes.Add(Node.Get());
			}
		}
		// Union in anything reachable from FirstNode that AllNodes somehow missed.
		TArray<USoundNode*> Reachable;
		Cue->RecursiveFindAllNodes(Cue->FirstNode, Reachable);
		for (USoundNode* Node : Reachable)
		{
			if (Node && !OutNodes.Contains(Node))
			{
				OutNodes.Add(Node);
			}
		}
	}

	/**
	 * Rebuild the editor SoundCueGraph to match the runtime node tree (FirstNode + ChildNodes).
	 *
	 * LinkGraphNodesFromSoundNodes() assumes each graph node already has exactly one input pin per
	 * ChildNode (it asserts InputPins.Num() == ChildNodes.Num()). Graph-node pins are created once
	 * at construction from the then-current ChildNodes count, so after we grow ChildNodes we must
	 * re-sync the pins. ReconstructNode() re-runs AllocateDefaultPins (one input pin per ChildNode)
	 * while preserving existing links, satisfying that invariant before we link.
	 */
	void RefreshEditorGraph(USoundCue* Cue)
	{
		if (!Cue || !USoundCue::GetSoundCueAudioEditor().IsValid())
		{
			return;
		}
		for (const TObjectPtr<USoundNode>& Node : Cue->AllNodes)
		{
			if (Node)
			{
				if (UEdGraphNode* GraphNode = Node->GetGraphNode())
				{
					GraphNode->ReconstructNode();
				}
			}
		}
		Cue->LinkGraphNodesFromSoundNodes();
	}

	/** Save a cue asset, returning false (with bool result of SaveAsset) on failure. */
	bool SaveCueChecked(const FString& Path)
	{
		return UEditorAssetLibrary::SaveAsset(Path, /*bOnlyIfIsDirty=*/false);
	}
#endif // WITH_EDITOR
}

FMCPToolInfo FMCPTool_SoundCue::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("sound_cue");
	Info.Description = TEXT(
		"Author Unreal Engine SoundCue assets and their node graphs (create, inspect, build, edit).\n\n"
		"Audio flows FROM leaf nodes (WavePlayer) TOWARD the cue's root/output node. "
		"connect_nodes wires a child node's output into a parent node's input slot.\n\n"
		"Operations (set 'operation'):\n"
		"- 'create': Create a SoundCue at 'asset_path'; optional 'sound_wave_path' adds an initial WavePlayer\n"
		"- 'get_graph': List all sound nodes (index, class, child indices, position, is_root)\n"
		"- 'add_node': Add a sound node of 'node_type' (wave_player, random, mixer, modulator, attenuation, "
		"looping, concatenator, delay, switch, enveloper, etc.) at optional 'pos_x'/'pos_y'\n"
		"- 'connect_nodes': Wire 'child_index' into 'parent_index' at 'input_slot'\n"
		"- 'set_node_property': Set 'property' = 'value' on node at 'node_index' via reflection\n"
		"- 'import_wave': Import a .wav/.mp3 from 'file_path' on disk to a SoundWave at 'asset_path'\n\n"
		"Node indices come from 'get_graph' and are invalidated by any structural change; "
		"re-query after add/connect operations.\n\n"
		"Returns: operation-specific data (node list, new node index, or affected asset path)."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: create, get_graph, add_node, connect_nodes, set_node_property, import_wave"), true),
		FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Full content path of the SoundCue (e.g. '/Game/Audio/SC_Footstep'). For import_wave: destination SoundWave path"), false),
		FMCPToolParameter(TEXT("sound_wave_path"), TEXT("string"), TEXT("For 'create'/'add_node' (wave_player): SoundWave asset to reference (optional)"), false),
		FMCPToolParameter(TEXT("node_type"), TEXT("string"), TEXT("For 'add_node': wave_player, random, mixer, modulator, attenuation, looping, concatenator, delay, switch, enveloper, distance_crossfade, branch, param_crossfade, quality_level"), false),
		FMCPToolParameter(TEXT("num_inputs"), TEXT("number"), TEXT("For 'add_node' on mixer/concatenator/switch/crossfade nodes: initial input slot count (default 2)"), false),
		FMCPToolParameter(TEXT("pos_x"), TEXT("number"), TEXT("For 'add_node': X position in the graph editor"), false),
		FMCPToolParameter(TEXT("pos_y"), TEXT("number"), TEXT("For 'add_node': Y position in the graph editor"), false),
		FMCPToolParameter(TEXT("parent_index"), TEXT("number"), TEXT("For 'connect_nodes': index of the consuming (parent) node"), false),
		FMCPToolParameter(TEXT("child_index"), TEXT("number"), TEXT("For 'connect_nodes': index of the producing (child) node"), false),
		FMCPToolParameter(TEXT("input_slot"), TEXT("number"), TEXT("For 'connect_nodes': zero-based input slot on the parent node"), false),
		FMCPToolParameter(TEXT("node_index"), TEXT("number"), TEXT("For 'set_node_property': index of the target node (from get_graph)"), false),
		FMCPToolParameter(TEXT("property"), TEXT("string"), TEXT("For 'set_node_property': exact C++ property name, e.g. 'PitchMin', 'bLooping'"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For 'set_node_property': new value as string, e.g. '0.9', 'true'"), false),
		FMCPToolParameter(TEXT("file_path"), TEXT("string"), TEXT("For 'import_wave': absolute path to the .wav/.mp3 file on disk"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_SoundCue::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("create"))            { return OpCreate(Params); }
	if (Operation == TEXT("get_graph"))         { return OpGetGraph(Params); }
	if (Operation == TEXT("add_node"))          { return OpAddNode(Params); }
	if (Operation == TEXT("connect_nodes"))     { return OpConnectNodes(Params); }
	if (Operation == TEXT("set_node_property")) { return OpSetNodeProperty(Params); }
	if (Operation == TEXT("import_wave"))       { return OpImportWave(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create, get_graph, add_node, connect_nodes, set_node_property, import_wave"), *Operation));
}

// =====================================================================
// create
// =====================================================================
FMCPToolResult FMCPTool_SoundCue::OpCreate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	const FString SoundWavePath = ExtractOptionalString(Params, TEXT("sound_wave_path"));

	const FString FullAssetPath = NormalizeContentPath(AssetPath);
	FString Folder, AssetName;
	SplitAssetPath(FullAssetPath, Folder, AssetName);

	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath) && UEditorAssetLibrary::LoadAsset(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("SoundCue already exists at: %s"), *FullAssetPath));
	}

	// The editor graph helpers (CreateGraph/SetupSoundNode/LinkGraphNodesFromSoundNodes) are
	// supplied by the AudioEditor module's ISoundCueAudioEditor. If it is not registered we
	// cannot author a graph that opens correctly, so fail honestly rather than produce a
	// half-built asset.
	if (!USoundCue::GetSoundCueAudioEditor().IsValid())
	{
		return FMCPToolResult::Error(TEXT("create: SoundCue graph editor (AudioEditor module) is not available"));
	}

	UPackage* Package = CreatePackage(*FullAssetPath);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package for: %s"), *FullAssetPath));
	}

	USoundCue* Cue = NewObject<USoundCue>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!Cue)
	{
		return FMCPToolResult::Error(TEXT("Failed to create SoundCue object"));
	}

	// Build the editor graph first so ConstructSoundNode's SetupSoundNode has a graph to attach to.
	Cue->CreateGraph();

	FString WaveResult;
	if (!SoundWavePath.IsEmpty())
	{
		USoundWave* Wave = LoadSoundWave(SoundWavePath);
		if (!Wave)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("sound_wave_path not found or not a SoundWave: %s"), *SoundWavePath));
		}

		USoundNodeWavePlayer* WavePlayer = Cue->ConstructSoundNode<USoundNodeWavePlayer>();
		if (!WavePlayer)
		{
			return FMCPToolResult::Error(TEXT("Failed to construct initial WavePlayer node"));
		}
		WavePlayer->SetSoundWave(Wave);
		Cue->FirstNode = WavePlayer;
		RefreshEditorGraph(Cue);
		if (UEdGraphNode* WaveGraphNode = WavePlayer->GetGraphNode())
		{
			WaveGraphNode->NodePosX = -250;
			WaveGraphNode->NodePosY = 0;
		}
		WaveResult = SoundWavePath;
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Cue);
	Cue->PostEditChange();

	if (!SaveCueChecked(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Created SoundCue in memory but failed to save asset: %s"), *FullAssetPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), FullAssetPath);
	Data->SetStringField(TEXT("name"), AssetName);
	if (!WaveResult.IsEmpty())
	{
		Data->SetStringField(TEXT("sound_wave_path"), WaveResult);
	}
	return FMCPToolResult::Success(FString::Printf(TEXT("Created SoundCue '%s'"), *FullAssetPath), Data);
#else
	return FMCPToolResult::Error(TEXT("create requires an editor build"));
#endif
}

// =====================================================================
// get_graph
// =====================================================================
FMCPToolResult FMCPTool_SoundCue::OpGetGraph(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	const FString FullAssetPath = NormalizeContentPath(AssetPath);
	USoundCue* Cue = LoadSoundCue(FullAssetPath);
	if (!Cue)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("SoundCue not found: %s"), *FullAssetPath));
	}

	TArray<USoundNode*> Nodes;
	CollectOrderedNodes(Cue, Nodes);

	TArray<TSharedPtr<FJsonValue>> NodeArray;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		USoundNode* Node = Nodes[i];
		TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
		NObj->SetNumberField(TEXT("index"), i);
		NObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NObj->SetBoolField(TEXT("is_root"), Node == Cue->FirstNode);

		// Child indices, mapped back into this ordered list (or -1 for empty/unknown slots).
		TArray<TSharedPtr<FJsonValue>> ChildIndices;
		for (const TObjectPtr<USoundNode>& Child : Node->ChildNodes)
		{
			const int32 ChildIdx = Child ? Nodes.IndexOfByKey(Child.Get()) : INDEX_NONE;
			ChildIndices.Add(MakeShared<FJsonValueNumber>(ChildIdx));
		}
		NObj->SetArrayField(TEXT("child_indices"), ChildIndices);
		NObj->SetNumberField(TEXT("max_child_nodes"), Node->GetMaxChildNodes());

		// Graph position, if an editor graph node is present.
		if (UEdGraphNode* GraphNode = Node->GetGraphNode())
		{
			NObj->SetNumberField(TEXT("pos_x"), GraphNode->NodePosX);
			NObj->SetNumberField(TEXT("pos_y"), GraphNode->NodePosY);
		}

		// Convenience: surface the referenced SoundWave for wave players.
		if (USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(Node))
		{
			if (USoundWave* Wave = WavePlayer->GetSoundWave())
			{
				NObj->SetStringField(TEXT("sound_wave"), Wave->GetPathName());
			}
		}

		NodeArray.Add(MakeShared<FJsonValueObject>(NObj));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), FullAssetPath);
	Data->SetNumberField(TEXT("node_count"), NodeArray.Num());
	Data->SetNumberField(TEXT("root_index"),
		Cue->FirstNode ? Nodes.IndexOfByKey(Cue->FirstNode.Get()) : INDEX_NONE);
	Data->SetArrayField(TEXT("nodes"), NodeArray);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("SoundCue '%s': %d node(s)"), *Cue->GetName(), NodeArray.Num()), Data);
#else
	// SoundCueGraph (editor graph) and AllNodes are editor-only data.
	return FMCPToolResult::Error(TEXT("get_graph requires an editor build"));
#endif
}

// =====================================================================
// add_node
// =====================================================================
FMCPToolResult FMCPTool_SoundCue::OpAddNode(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, NodeType;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("node_type"), NodeType, Err)) { return Err.GetValue(); }
	const FString SoundWavePath = ExtractOptionalString(Params, TEXT("sound_wave_path"));
	const int32 NumInputs = ExtractOptionalNumber<int32>(Params, TEXT("num_inputs"), 2);
	const int32 PosX = ExtractOptionalNumber<int32>(Params, TEXT("pos_x"), 0);
	const int32 PosY = ExtractOptionalNumber<int32>(Params, TEXT("pos_y"), 0);

	const FString FullAssetPath = NormalizeContentPath(AssetPath);
	USoundCue* Cue = LoadSoundCue(FullAssetPath);
	if (!Cue)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("SoundCue not found: %s"), *FullAssetPath));
	}

	if (!USoundCue::GetSoundCueAudioEditor().IsValid())
	{
		return FMCPToolResult::Error(TEXT("add_node: SoundCue graph editor (AudioEditor module) is not available"));
	}

	UClass* NodeClass = MapNodeTypeToClass(NodeType);
	if (!NodeClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Unknown node_type '%s'. Valid: wave_player, random, mixer, modulator, attenuation, looping, "
				"concatenator, delay, switch, enveloper, distance_crossfade, branch, param_crossfade, quality_level"),
			*NodeType));
	}

	Cue->Modify();

	// Ensure the editor graph exists for SetupSoundNode (created via the templated factory below).
	Cue->CreateGraph();

	// ConstructSoundNode<USoundNode>(SubclassOf) constructs of the requested concrete class while
	// running the editor-side AllNodes registration + graph-node setup.
	USoundNode* NewNode = Cue->ConstructSoundNode<USoundNode>(NodeClass);
	if (!NewNode)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to construct node of type '%s'"), *NodeType));
	}

	// Wave players reference a SoundWave.
	if (USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(NewNode))
	{
		if (!SoundWavePath.IsEmpty())
		{
			USoundWave* Wave = LoadSoundWave(SoundWavePath);
			if (!Wave)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("sound_wave_path not found or not a SoundWave: %s"), *SoundWavePath));
			}
			WavePlayer->SetSoundWave(Wave);
		}
	}

	// Establish default connectors, then grow input slots for multi-input nodes if requested.
	NewNode->CreateStartingConnectors();
	const int32 MaxChildren = NewNode->GetMaxChildNodes();
	if (MaxChildren > 1 && NumInputs > NewNode->ChildNodes.Num())
	{
		const int32 DesiredInputs = FMath::Min(NumInputs, MaxChildren);
		while (NewNode->ChildNodes.Num() < DesiredInputs)
		{
			NewNode->InsertChildNode(NewNode->ChildNodes.Num());
		}
	}

	// Rebuild the editor graph so the new node (and its pins) are represented, then position it.
	RefreshEditorGraph(Cue);
	if (UEdGraphNode* GraphNode = NewNode->GetGraphNode())
	{
		GraphNode->NodePosX = PosX;
		GraphNode->NodePosY = PosY;
	}

	Cue->MarkPackageDirty();
	Cue->PostEditChange();

	if (!SaveCueChecked(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Added node but failed to save asset: %s"), *FullAssetPath));
	}

	// Report the new node's index in the canonical ordered list.
	TArray<USoundNode*> Nodes;
	CollectOrderedNodes(Cue, Nodes);
	const int32 NewIndex = Nodes.IndexOfByKey(NewNode);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), FullAssetPath);
	Data->SetNumberField(TEXT("node_index"), NewIndex);
	Data->SetStringField(TEXT("class"), NewNode->GetClass()->GetName());
	Data->SetNumberField(TEXT("input_count"), NewNode->ChildNodes.Num());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added %s node (index %d) to %s"), *NodeType, NewIndex, *FullAssetPath), Data);
#else
	return FMCPToolResult::Error(TEXT("add_node requires an editor build"));
#endif
}

// =====================================================================
// connect_nodes
// =====================================================================
FMCPToolResult FMCPTool_SoundCue::OpConnectNodes(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	const int32 ParentIndex = ExtractOptionalNumber<int32>(Params, TEXT("parent_index"), INDEX_NONE);
	const int32 ChildIndex = ExtractOptionalNumber<int32>(Params, TEXT("child_index"), INDEX_NONE);
	const int32 InputSlot = ExtractOptionalNumber<int32>(Params, TEXT("input_slot"), 0);

	const FString FullAssetPath = NormalizeContentPath(AssetPath);
	USoundCue* Cue = LoadSoundCue(FullAssetPath);
	if (!Cue)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("SoundCue not found: %s"), *FullAssetPath));
	}

	if (!USoundCue::GetSoundCueAudioEditor().IsValid())
	{
		return FMCPToolResult::Error(TEXT("connect_nodes: SoundCue graph editor (AudioEditor module) is not available"));
	}

	TArray<USoundNode*> Nodes;
	CollectOrderedNodes(Cue, Nodes);

	if (!Nodes.IsValidIndex(ParentIndex))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("parent_index %d out of range (have %d node(s))"), ParentIndex, Nodes.Num()));
	}
	if (!Nodes.IsValidIndex(ChildIndex))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("child_index %d out of range (have %d node(s))"), ChildIndex, Nodes.Num()));
	}
	if (ParentIndex == ChildIndex)
	{
		return FMCPToolResult::Error(TEXT("connect_nodes: parent_index and child_index must differ"));
	}
	if (InputSlot < 0)
	{
		return FMCPToolResult::Error(TEXT("connect_nodes: input_slot must be >= 0"));
	}

	USoundNode* ParentNode = Nodes[ParentIndex];
	USoundNode* ChildNode = Nodes[ChildIndex];

	const int32 MaxChildren = ParentNode->GetMaxChildNodes();
	if (InputSlot >= MaxChildren)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("input_slot %d exceeds parent's max child nodes (%d) for %s"),
			InputSlot, MaxChildren, *ParentNode->GetClass()->GetName()));
	}

	// Growing the child array calls USoundNode::InsertChildNode, which under WITH_EDITOR creates an
	// input pin via CastChecked<USoundCueGraphNode>(GetGraphNode()). A node surfaced from the runtime
	// SoundNode tree (e.g. an externally-authored or cooked cue) may have no editor graph node; rebuild
	// the editor graph once and bail with an honest error if it still has none, rather than crashing on
	// the CastChecked(nullptr).
	if (!ParentNode->GetGraphNode())
	{
		RefreshEditorGraph(Cue);
	}
	if (!ParentNode->GetGraphNode())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("connect_nodes: parent node (index %d, %s) has no editor graph node, so its inputs cannot "
				"be wired. Re-create the cue with this tool, or open it once in the editor to build its graph."),
			ParentIndex, *ParentNode->GetClass()->GetName()));
	}

	Cue->Modify();
	ParentNode->Modify();

	// Grow the parent's child array up to and including the requested slot. InsertChildNode is
	// overridden by multi-input nodes (Mixer/Random/Concatenator/Switch/CrossFade) to keep their
	// per-input side arrays (InputVolume/Weights/etc.) sized correctly.
	while (ParentNode->ChildNodes.Num() <= InputSlot)
	{
		ParentNode->InsertChildNode(ParentNode->ChildNodes.Num());
	}
	ParentNode->ChildNodes[InputSlot] = ChildNode;

	// Rebuild the editor graph pins/links to reflect the new wiring.
	RefreshEditorGraph(Cue);

	Cue->MarkPackageDirty();
	Cue->PostEditChange();

	if (!SaveCueChecked(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Connected nodes but failed to save asset: %s"), *FullAssetPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), FullAssetPath);
	Data->SetNumberField(TEXT("parent_index"), ParentIndex);
	Data->SetNumberField(TEXT("child_index"), ChildIndex);
	Data->SetNumberField(TEXT("input_slot"), InputSlot);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Connected node %d into node %d (slot %d)"), ChildIndex, ParentIndex, InputSlot), Data);
#else
	return FMCPToolResult::Error(TEXT("connect_nodes requires an editor build"));
#endif
}

// =====================================================================
// set_node_property
// =====================================================================
FMCPToolResult FMCPTool_SoundCue::OpSetNodeProperty(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, PropertyName, Value;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("property"), PropertyName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err)) { return Err.GetValue(); }
	const int32 NodeIndex = ExtractOptionalNumber<int32>(Params, TEXT("node_index"), INDEX_NONE);

	const FString FullAssetPath = NormalizeContentPath(AssetPath);
	USoundCue* Cue = LoadSoundCue(FullAssetPath);
	if (!Cue)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("SoundCue not found: %s"), *FullAssetPath));
	}

	TArray<USoundNode*> Nodes;
	CollectOrderedNodes(Cue, Nodes);
	if (!Nodes.IsValidIndex(NodeIndex))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("node_index %d out of range (have %d node(s))"), NodeIndex, Nodes.Num()));
	}

	USoundNode* Node = Nodes[NodeIndex];
	FProperty* Property = FindFProperty<FProperty>(Node->GetClass(), *PropertyName);
	if (!Property)
	{
		// Build a list of editable properties to help the caller.
		FString Available;
		for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
		{
			if (!Available.IsEmpty()) { Available += TEXT(", "); }
			Available += It->GetName();
		}
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Property '%s' not found on %s. Available: [%s]"),
			*PropertyName, *Node->GetClass()->GetName(), *Available));
	}

	Cue->Modify();
	Node->Modify();

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
	const TCHAR* ImportResult = Property->ImportText_Direct(*Value, ValuePtr, Node, PPF_None);
	if (ImportResult == nullptr)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Failed to parse value '%s' for property '%s' (type %s) on %s"),
			*Value, *PropertyName, *Property->GetCPPType(), *Node->GetClass()->GetName()));
	}

	// Notify the node of the property change so any editor-side fixups run.
	FPropertyChangedEvent ChangeEvent(Property);
	Node->PostEditChangeProperty(ChangeEvent);

	// A property change can affect cached cue values (max distance, duration, etc.).
	RefreshEditorGraph(Cue);

	Cue->MarkPackageDirty();

	if (!SaveCueChecked(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Set property but failed to save asset: %s"), *FullAssetPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), FullAssetPath);
	Data->SetNumberField(TEXT("node_index"), NodeIndex);
	Data->SetStringField(TEXT("property"), PropertyName);
	Data->SetStringField(TEXT("value"), Value);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set %s.%s = %s on node %d"),
			*Node->GetClass()->GetName(), *PropertyName, *Value, NodeIndex), Data);
#else
	return FMCPToolResult::Error(TEXT("set_node_property requires an editor build"));
#endif
}

// =====================================================================
// import_wave
// =====================================================================
FMCPToolResult FMCPTool_SoundCue::OpImportWave(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, FilePath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("file_path"), FilePath, Err)) { return Err.GetValue(); }

	if (!FPaths::FileExists(FilePath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("import_wave: source file does not exist: %s"), *FilePath));
	}

	const FString FullAssetPath = NormalizeContentPath(AssetPath);
	FString DestFolder, DesiredName;
	SplitAssetPath(FullAssetPath, DestFolder, DesiredName);

	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath) && UEditorAssetLibrary::LoadAsset(FullAssetPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("SoundWave already exists at: %s"), *FullAssetPath));
	}

	// Drive a USoundFactory explicitly: no auto-cue, no import dialogs. Use an AssetImportTask so
	// we can request the exact destination name (honoring asset_path) and an automated import.
	USoundFactory* SoundFactory = NewObject<USoundFactory>();
	SoundFactory->bAutoCreateCue = false;
	SoundFactory->bIncludeAttenuationNode = false;
	SoundFactory->bIncludeLoopingNode = false;
	SoundFactory->bIncludeModulatorNode = false;
	SoundFactory->SuppressImportDialogs();

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FilePath;
	Task->DestinationPath = DestFolder;
	Task->DestinationName = DesiredName;
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->bAsync = false;
	Task->Factory = SoundFactory;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().ImportAssetTasks({ Task });

	// Resolve the imported object.
	USoundWave* ImportedWave = nullptr;
	for (UObject* Obj : Task->GetObjects())
	{
		if (USoundWave* Wave = Cast<USoundWave>(Obj))
		{
			ImportedWave = Wave;
			break;
		}
	}
	if (!ImportedWave)
	{
		for (const FString& ObjPath : Task->ImportedObjectPaths)
		{
			if (USoundWave* Wave = Cast<USoundWave>(UEditorAssetLibrary::LoadAsset(ObjPath)))
			{
				ImportedWave = Wave;
				break;
			}
		}
	}
	if (!ImportedWave)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("import_wave: import produced no SoundWave from '%s' (unsupported format or import error)"), *FilePath));
	}

	// The factory may name the asset after the source file rather than DesiredName; rename to the
	// requested path if needed so the result matches asset_path.
	if (ImportedWave->GetName() != DesiredName)
	{
		if (!UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
		{
			UEditorAssetLibrary::RenameAsset(ImportedWave->GetOutermost()->GetName(), FullAssetPath);
		}
	}

	// Reload at the final path and save.
	const FString FinalPath = UEditorAssetLibrary::DoesAssetExist(FullAssetPath) ? FullAssetPath : ImportedWave->GetOutermost()->GetName();
	if (!UEditorAssetLibrary::SaveAsset(FinalPath, /*bOnlyIfIsDirty=*/false))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Imported SoundWave but failed to save asset: %s"), *FinalPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), FinalPath);
	Data->SetStringField(TEXT("source_file"), FilePath);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Imported SoundWave '%s' from %s"), *FinalPath, *FilePath), Data);
#else
	return FMCPToolResult::Error(TEXT("import_wave requires an editor build"));
#endif
}
