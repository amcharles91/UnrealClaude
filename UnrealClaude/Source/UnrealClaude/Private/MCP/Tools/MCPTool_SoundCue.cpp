// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_SoundCue.h"

// STUB: keep includes minimal. The full implementation will add the engine
// SoundCue / SoundNode headers (Sound/SoundCue.h, Sound/SoundNodeWavePlayer.h,
// SoundCueGraph/SoundCueGraphNode.h, Factories/SoundCueFactoryNew.h, etc.)
// once the AudioEditor module dependency is wired into Build.cs.

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
		"Returns: operation-specific data (node list, new node index, or affected asset path).\n\n"
		"STATUS: STUB - operations are not implemented yet."
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

FMCPToolResult FMCPTool_SoundCue::OpCreate(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("create: not implemented yet"));
}

FMCPToolResult FMCPTool_SoundCue::OpGetGraph(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_graph: not implemented yet"));
}

FMCPToolResult FMCPTool_SoundCue::OpAddNode(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_node: not implemented yet"));
}

FMCPToolResult FMCPTool_SoundCue::OpConnectNodes(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("connect_nodes: not implemented yet"));
}

FMCPToolResult FMCPTool_SoundCue::OpSetNodeProperty(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_node_property: not implemented yet"));
}

FMCPToolResult FMCPTool_SoundCue::OpImportWave(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("import_wave: not implemented yet"));
}
