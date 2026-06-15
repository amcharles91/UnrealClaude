// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_MetaSound.h"

// STUB: keep includes minimal. The full implementation will add the MetaSound
// engine/frontend/editor headers (MetasoundBuilderSubsystem.h,
// MetasoundBuilderBase.h, MetasoundSource.h, MetasoundFrontendDocumentBuilder.h,
// MetasoundFrontendSearchEngine.h, MetasoundEditorSubsystem.h,
// Interfaces/MetasoundOutputFormatInterfaces.h, etc.) once the Metasound plugin
// modules (MetasoundEngine / MetasoundFrontend / MetasoundEditor) are wired into
// Build.cs and the Metasound plugin is enabled in the .uplugin.

FMCPToolInfo FMCPTool_MetaSound::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("metasound");
	Info.Description = TEXT(
		"Author Unreal Engine MetaSound source assets and their node graphs (create, inspect, build, edit).\n\n"
		"MetaSound graphs are node-and-pin based: a node has named, typed input and output pins. "
		"Nodes are identified by GUID strings (node ids) returned from 'add_node' and 'get_graph'.\n\n"
		"Operations (set 'operation'):\n"
		"- 'create': Create a MetaSound source at 'package_path'/'asset_name' with 'output_format' (Mono/Stereo/Quad)\n"
		"- 'get_graph': List all nodes (node_id, title, class, input/output pins, position)\n"
		"- 'add_node': Add a DSP node by 'node_namespace' + 'node_name' + optional 'node_variant' (e.g. Metasound.Standard / Sine / Audio)\n"
		"- 'connect_nodes': Wire 'from_node_id'.'output_name' into 'to_node_id'.'input_name'\n"
		"- 'set_pin': Set the default literal 'value' (with 'data_type') on 'node_id'.'input_name'\n"
		"- 'add_graph_input': Add a named runtime graph input 'input_name' of 'data_type' with optional 'default_value'\n"
		"- 'add_graph_output': Add a named graph output 'output_name' of 'data_type'\n\n"
		"DataTypes: Float, Int32, Bool, String, Audio, Trigger.\n\n"
		"Returns: operation-specific data (node list, new node id, or affected asset path).\n\n"
		"STATUS: STUB - operations are not implemented yet."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"), TEXT("One of: create, get_graph, add_node, connect_nodes, set_pin, add_graph_input, add_graph_output"), true),
		FMCPToolParameter(TEXT("asset_path"), TEXT("string"), TEXT("Full content path of the MetaSound source (e.g. '/Game/Audio/MS_Sine'). Required by all ops except 'create'"), false),
		FMCPToolParameter(TEXT("package_path"), TEXT("string"), TEXT("For 'create': content folder, e.g. '/Game/Audio'"), false),
		FMCPToolParameter(TEXT("asset_name"), TEXT("string"), TEXT("For 'create': asset name, e.g. 'MS_Wind'"), false),
		FMCPToolParameter(TEXT("output_format"), TEXT("string"), TEXT("For 'create': 'Mono' | 'Stereo' | 'Quad' (default 'Mono')"), false),
		FMCPToolParameter(TEXT("node_namespace"), TEXT("string"), TEXT("For 'add_node': node namespace, e.g. 'Metasound.Standard'"), false),
		FMCPToolParameter(TEXT("node_name"), TEXT("string"), TEXT("For 'add_node': node name, e.g. 'Sine'"), false),
		FMCPToolParameter(TEXT("node_variant"), TEXT("string"), TEXT("For 'add_node': variant suffix, e.g. 'Audio' (empty when none)"), false),
		FMCPToolParameter(TEXT("major_version"), TEXT("number"), TEXT("For 'add_node': class major version (almost always 1)"), false),
		FMCPToolParameter(TEXT("pos_x"), TEXT("number"), TEXT("For 'add_node': X position in the graph editor"), false),
		FMCPToolParameter(TEXT("pos_y"), TEXT("number"), TEXT("For 'add_node': Y position in the graph editor"), false),
		FMCPToolParameter(TEXT("from_node_id"), TEXT("string"), TEXT("For 'connect_nodes': GUID of the source node"), false),
		FMCPToolParameter(TEXT("output_name"), TEXT("string"), TEXT("For 'connect_nodes': output pin name on the source node. For 'add_graph_output': name of the new graph output"), false),
		FMCPToolParameter(TEXT("to_node_id"), TEXT("string"), TEXT("For 'connect_nodes': GUID of the destination node"), false),
		FMCPToolParameter(TEXT("input_name"), TEXT("string"), TEXT("For 'connect_nodes'/'set_pin': input pin name. For 'add_graph_input': name of the new graph input"), false),
		FMCPToolParameter(TEXT("node_id"), TEXT("string"), TEXT("For 'set_pin': GUID of the node whose input pin default to set"), false),
		FMCPToolParameter(TEXT("value"), TEXT("string"), TEXT("For 'set_pin': literal value as string, e.g. '440.0', 'true'"), false),
		FMCPToolParameter(TEXT("data_type"), TEXT("string"), TEXT("For 'set_pin'/'add_graph_input'/'add_graph_output': Float, Int32, Bool, String, Audio, Trigger"), false),
		FMCPToolParameter(TEXT("default_value"), TEXT("string"), TEXT("For 'add_graph_input': default literal value as string (optional)"), false)
	};
	Info.Annotations = FMCPToolAnnotations::Modifying();
	return Info;
}

FMCPToolResult FMCPTool_MetaSound::Execute(const TSharedRef<FJsonObject>& Params)
{
	FString Operation;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, Err))
	{
		return Err.GetValue();
	}

	if (Operation == TEXT("create"))           { return OpCreate(Params); }
	if (Operation == TEXT("get_graph"))        { return OpGetGraph(Params); }
	if (Operation == TEXT("add_node"))         { return OpAddNode(Params); }
	if (Operation == TEXT("connect_nodes"))    { return OpConnectNodes(Params); }
	if (Operation == TEXT("set_pin"))          { return OpSetPin(Params); }
	if (Operation == TEXT("add_graph_input"))  { return OpAddGraphInput(Params); }
	if (Operation == TEXT("add_graph_output")) { return OpAddGraphOutput(Params); }

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Valid: create, get_graph, add_node, connect_nodes, set_pin, add_graph_input, add_graph_output"), *Operation));
}

FMCPToolResult FMCPTool_MetaSound::OpCreate(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("create: not implemented yet"));
}

FMCPToolResult FMCPTool_MetaSound::OpGetGraph(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("get_graph: not implemented yet"));
}

FMCPToolResult FMCPTool_MetaSound::OpAddNode(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_node: not implemented yet"));
}

FMCPToolResult FMCPTool_MetaSound::OpConnectNodes(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("connect_nodes: not implemented yet"));
}

FMCPToolResult FMCPTool_MetaSound::OpSetPin(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("set_pin: not implemented yet"));
}

FMCPToolResult FMCPTool_MetaSound::OpAddGraphInput(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_graph_input: not implemented yet"));
}

FMCPToolResult FMCPTool_MetaSound::OpAddGraphOutput(const TSharedRef<FJsonObject>& Params)
{
	return FMCPToolResult::Error(TEXT("add_graph_output: not implemented yet"));
}
