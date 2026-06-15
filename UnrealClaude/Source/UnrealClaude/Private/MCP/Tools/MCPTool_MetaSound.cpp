// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_MetaSound.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "Misc/Guid.h"
#include "EditorAssetLibrary.h"

// MetaSound Engine (module: MetasoundEngine)
#include "MetasoundSource.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"

// MetaSound Frontend (module: MetasoundFrontend)
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"

#if WITH_EDITOR
// MetaSound Engine builder API (editor authoring)
#include "MetasoundBuilderSubsystem.h"   // UMetaSoundSourceBuilder + SetFormat (not in BuilderBase.h)
#include "MetasoundBuilderBase.h"
#include "MetasoundFrontendDocumentBuilder.h"

// MetaSound Editor (module: MetasoundEditor)
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFactory.h"

// Asset creation
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#endif // WITH_EDITOR

// =====================================================================
// File-local helpers (anonymous namespace — keeps ABI stable for Live Coding,
// no new header members). Mirrors the house pattern in MCPTool_Niagara.cpp and
// the verified VibeUE MetaSound service call sequences.
// =====================================================================
namespace
{
#if WITH_EDITOR
	/** Map a friendly string to EMetaSoundOutputAudioFormat (defaults to Mono). */
	EMetaSoundOutputAudioFormat StringToOutputFormat(const FString& Str)
	{
		if (Str.Equals(TEXT("Stereo"), ESearchCase::IgnoreCase)) { return EMetaSoundOutputAudioFormat::Stereo; }
		if (Str.Equals(TEXT("Quad"),   ESearchCase::IgnoreCase)) { return EMetaSoundOutputAudioFormat::Quad; }
		return EMetaSoundOutputAudioFormat::Mono;
	}

	/** Friendly string for an EMetaSoundOutputAudioFormat. */
	FString OutputFormatToString(EMetaSoundOutputAudioFormat Format)
	{
		switch (Format)
		{
		case EMetaSoundOutputAudioFormat::Mono:        return TEXT("Mono");
		case EMetaSoundOutputAudioFormat::Stereo:      return TEXT("Stereo");
		case EMetaSoundOutputAudioFormat::Quad:        return TEXT("Quad");
		case EMetaSoundOutputAudioFormat::FiveDotOne:  return TEXT("5.1");
		case EMetaSoundOutputAudioFormat::SevenDotOne: return TEXT("7.1");
		default:                                       return TEXT("Unknown");
		}
	}

	/**
	 * Build a frontend literal from a string value for the given DataType.
	 * Float/Int32/Bool get typed literals; everything else (String, and the
	 * trigger/audio data types which carry no meaningful default) uses a string
	 * literal. Mirrors VibeUE's MakeLiteral().
	 */
	FMetasoundFrontendLiteral MakeLiteralFromString(const FString& Value, const FString& DataType)
	{
		FMetasoundFrontendLiteral Lit;
		if (DataType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			Lit.Set(FCString::Atof(*Value));
		}
		else if (DataType.Equals(TEXT("Int32"), ESearchCase::IgnoreCase) ||
				 DataType.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
		{
			Lit.Set((int32)FCString::Atoi(*Value));
		}
		else if (DataType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
		{
			const FString Trimmed = Value.TrimStartAndEnd();
			const bool bVal = Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("1"));
			Lit.Set(bVal);
		}
		else
		{
			Lit.Set(Value);
		}
		return Lit;
	}

	/**
	 * Load a UMetaSoundSource by content path and obtain an editor builder for it.
	 * Returns the builder (and sets OutSource) on success, or nullptr with OutError set.
	 * Mirrors VibeUE's BeginEditing(): the editor subsystem's FindOrBeginBuilding is the
	 * supported 5.5+ path for editing an existing asset's document.
	 */
	UMetaSoundBuilderBase* BeginEditing(const FString& AssetPath, UMetaSoundSource*& OutSource, FString& OutError)
	{
		OutSource = nullptr;

		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("MetaSound source not found: %s"), *AssetPath);
			return nullptr;
		}

		OutSource = Cast<UMetaSoundSource>(Loaded);
		if (!OutSource)
		{
			OutError = FString::Printf(TEXT("Asset is not a MetaSoundSource: %s"), *AssetPath);
			return nullptr;
		}

		UMetaSoundEditorSubsystem& EditorSub = UMetaSoundEditorSubsystem::GetChecked();
		EMetaSoundBuilderResult FindResult = EMetaSoundBuilderResult::Failed;
		TScriptInterface<IMetaSoundDocumentInterface> DocIface(OutSource);
		UMetaSoundBuilderBase* Builder = EditorSub.FindOrBeginBuilding(DocIface, FindResult);

		if (FindResult != EMetaSoundBuilderResult::Succeeded || !Builder)
		{
			OutError = FString::Printf(TEXT("Could not acquire MetaSound builder for: %s"), *AssetPath);
			return nullptr;
		}
		return Builder;
	}

	/**
	 * Persist builder mutations back to the asset: re-register the graph with the
	 * frontend (so the document changes take effect), notify any open editor, mark the
	 * package dirty, and checked-save. Returns false (with OutError) if the save fails.
	 * Mirrors VibeUE's CommitEditing(), plus an explicit MarkPackageDirty + checked save
	 * per the house pattern.
	 */
	bool CommitEditing(const FString& AssetPath, UMetaSoundSource* Source, FString& OutError)
	{
		UMetaSoundEditorSubsystem::GetChecked().RegisterGraphWithFrontend(*Source);
		Source->PostEditChange();
		Source->MarkPackageDirty();

		if (!UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false))
		{
			OutError = FString::Printf(TEXT("Mutated MetaSound document but failed to save asset: %s"), *AssetPath);
			return false;
		}
		return true;
	}

	/** Parse a GUID string (with hyphens), returning false (and an error result) if invalid. */
	bool ParseNodeGuid(const FString& NodeIdStr, FGuid& OutGuid)
	{
		return FGuid::Parse(NodeIdStr, OutGuid);
	}

	/** Comma-joined "Name:Type" list of a node's input or output pins, for honest "pin not found" errors. */
	FString ListNodePinNames(UMetaSoundBuilderBase* Builder, const FGuid& NodeGuid, bool bInputs)
	{
		FString Names;
		Builder->GetConstBuilder().IterateNodes(
			[&](const FMetasoundFrontendClass&, const FMetasoundFrontendNode& Node)
			{
				if (Node.GetID() != NodeGuid) { return; }
				for (const FMetasoundFrontendVertex& V : (bInputs ? Node.Interface.Inputs : Node.Interface.Outputs))
				{
					if (!Names.IsEmpty()) { Names += TEXT(", "); }
					Names += V.Name.ToString() + TEXT(":") + V.TypeName.ToString();
				}
			});
		return Names;
	}

	/** Build a JSON array of {name,type} pin objects from a node's vertex (input or output) list. */
	TArray<TSharedPtr<FJsonValue>> BuildPinArray(const TArray<FMetasoundFrontendVertex>& Vertices)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FMetasoundFrontendVertex& V : Vertices)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), V.Name.ToString());
			PinObj->SetStringField(TEXT("type"), V.TypeName.ToString());
			Arr.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		return Arr;
	}
#endif // WITH_EDITOR
}

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
		"Returns: operation-specific data (node list, new node id, or affected asset path)."
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

// =====================================================================
// create
// =====================================================================
FMCPToolResult FMCPTool_MetaSound::OpCreate(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString PackagePath, AssetName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("package_path"), PackagePath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("asset_name"), AssetName, Err)) { return Err.GetValue(); }
	const FString OutputFormatStr = ExtractOptionalString(Params, TEXT("output_format"), TEXT("Mono"));

	// Normalize the destination folder.
	FString CleanPath = PackagePath;
	if (!CleanPath.StartsWith(TEXT("/")))
	{
		CleanPath = TEXT("/Game/") + CleanPath;
	}
	if (CleanPath.EndsWith(TEXT("/")))
	{
		CleanPath = CleanPath.LeftChop(1);
	}
	const FString FullPath = CleanPath / AssetName;

	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("MetaSound source already exists at: %s"), *FullPath));
	}

	// Create via the editor factory exactly as the editor does on right-click. (VibeUE notes
	// that CreateSourceBuilder + BuildToAsset produces two sets of interface nodes — the
	// builder's Input/Output nodes plus Template nodes injected by FindOrBeginBuilding —
	// leaving visible orphan nodes. The factory path avoids that.)
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UMetaSoundSourceFactory* Factory = NewObject<UMetaSoundSourceFactory>();
	UObject* CreatedObj = AssetTools.CreateAsset(AssetName, CleanPath, UMetaSoundSource::StaticClass(), Factory);
	UMetaSoundSource* NewSource = Cast<UMetaSoundSource>(CreatedObj);
	if (!NewSource)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Factory failed to create MetaSoundSource at: %s"), *FullPath));
	}

	const EMetaSoundOutputAudioFormat DesiredFormat = StringToOutputFormat(OutputFormatStr);

	// Apply the requested output format and strip orphan Template/Invalid binding nodes in a
	// single builder session. Simply assigning UMetaSoundSource::OutputFormat does NOT swap the
	// audio output interface; the format must be changed through the source builder's SetFormat()
	// (which is what the editor's Output Format dropdown does).
	bool bCommitted = false;
	{
		FString LoadError;
		UMetaSoundBuilderBase* Builder = BeginEditing(FullPath, NewSource, LoadError);
		if (Builder)
		{
			bool bModified = false;

			if (NewSource->OutputFormat != DesiredFormat)
			{
				if (UMetaSoundSourceBuilder* SourceBuilder = Cast<UMetaSoundSourceBuilder>(Builder))
				{
					EMetaSoundBuilderResult FormatResult = EMetaSoundBuilderResult::Failed;
					SourceBuilder->SetFormat(DesiredFormat, FormatResult);
					if (FormatResult == EMetaSoundBuilderResult::Succeeded)
					{
						// Keep the asset property in sync with the interface so get_graph / the
						// details panel agree.
						NewSource->OutputFormat = DesiredFormat;
						bModified = true;
					}
				}
			}

			// Remove orphan Template/Invalid nodes injected by FindOrBeginBuilding on first open.
			TArray<FGuid> ToRemove;
			Builder->GetConstBuilder().IterateNodes(
				[&](const FMetasoundFrontendClass& Class, const FMetasoundFrontendNode& Node)
				{
					const EMetasoundFrontendClassType T = Class.Metadata.GetType();
					if (T == EMetasoundFrontendClassType::Template || T == EMetasoundFrontendClassType::Invalid)
					{
						ToRemove.Add(Node.GetID());
					}
				});
			if (ToRemove.Num() > 0)
			{
				EMetaSoundBuilderResult RemoveResult = EMetaSoundBuilderResult::Failed;
				for (const FGuid& Id : ToRemove)
				{
					Builder->RemoveNode(FMetaSoundNodeHandle(Id), RemoveResult);
				}
				bModified = true;
			}

			if (bModified)
			{
				FString CommitError;
				if (!CommitEditing(FullPath, NewSource, CommitError))
				{
					return FMCPToolResult::Error(CommitError);
				}
				bCommitted = true;  // CommitEditing already saved the package.
			}
		}
	}

	// Persist the freshly created asset only if the builder-commit path didn't already save it
	// (avoids a redundant full package write on the common create path).
	if (!bCommitted)
	{
		NewSource->MarkPackageDirty();
		if (!UEditorAssetLibrary::SaveAsset(FullPath, /*bOnlyIfIsDirty=*/false))
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("Created MetaSound source in memory but failed to save asset: %s"), *FullPath));
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), FullPath);
	Data->SetStringField(TEXT("name"), AssetName);
	Data->SetStringField(TEXT("output_format"), OutputFormatToString(NewSource->OutputFormat));
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Created MetaSound source '%s' (%s)"), *FullPath, *OutputFormatToString(NewSource->OutputFormat)), Data);
#else
	return FMCPToolResult::Error(TEXT("create requires an editor build"));
#endif
}

// =====================================================================
// get_graph  (read op — but builder acquisition is editor-only)
// =====================================================================
FMCPToolResult FMCPTool_MetaSound::OpGetGraph(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	UMetaSoundBuilderBase* Builder = BeginEditing(AssetPath, Source, LoadError);
	if (!Builder)
	{
		return FMCPToolResult::Error(LoadError);
	}

	TArray<TSharedPtr<FJsonValue>> NodeArray;
	const FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder->GetConstBuilder();

	DocBuilder.IterateNodes(
		[&](const FMetasoundFrontendClass& Class, const FMetasoundFrontendNode& Node)
		{
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("node_id"), Node.GetID().ToString(EGuidFormats::DigitsWithHyphens));

			const FMetasoundFrontendClassName& ClassName = Class.Metadata.GetClassName();
			NodeObj->SetStringField(TEXT("class"), ClassName.ToString());

			const EMetasoundFrontendClassType ClassType = Class.Metadata.GetType();
			NodeObj->SetStringField(TEXT("class_type"), UEnum::GetValueAsString(ClassType));

			// Title: Template/Invalid classes cannot be resolved in the class registry and
			// GetNodeTitle on them asserts — fall back to the class name string for those.
			if (ClassType == EMetasoundFrontendClassType::Template || ClassType == EMetasoundFrontendClassType::Invalid)
			{
				NodeObj->SetStringField(TEXT("title"), ClassName.ToString());
			}
			else
			{
				NodeObj->SetStringField(TEXT("title"), DocBuilder.GetNodeTitle(Node.GetID()).ToString());
			}

			NodeObj->SetArrayField(TEXT("inputs"), BuildPinArray(Node.Interface.Inputs));
			NodeObj->SetArrayField(TEXT("outputs"), BuildPinArray(Node.Interface.Outputs));

			// Editor-only graph position (first location entry, keyed by editor page GUID).
#if WITH_EDITORONLY_DATA
			if (!Node.Style.Display.Locations.IsEmpty())
			{
				const FVector2D& Loc = Node.Style.Display.Locations.CreateConstIterator().Value();
				NodeObj->SetNumberField(TEXT("pos_x"), Loc.X);
				NodeObj->SetNumberField(TEXT("pos_y"), Loc.Y);
			}
#endif

			NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
		});

	// Graph-level inputs/outputs (the runtime parameter surface).
	EMetaSoundBuilderResult R;
	TArray<FString> GraphInputNames;
	for (const FName& N : Builder->GetGraphInputNames(R)) { GraphInputNames.Add(N.ToString()); }
	TArray<FString> GraphOutputNames;
	for (const FName& N : Builder->GetGraphOutputNames(R)) { GraphOutputNames.Add(N.ToString()); }
	TArray<TSharedPtr<FJsonValue>> GraphInputs = StringArrayToJsonArray(GraphInputNames);
	TArray<TSharedPtr<FJsonValue>> GraphOutputs = StringArrayToJsonArray(GraphOutputNames);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("output_format"), OutputFormatToString(Source->OutputFormat));
	Data->SetNumberField(TEXT("node_count"), NodeArray.Num());
	Data->SetArrayField(TEXT("nodes"), NodeArray);
	Data->SetArrayField(TEXT("graph_inputs"), GraphInputs);
	Data->SetArrayField(TEXT("graph_outputs"), GraphOutputs);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("MetaSound '%s': %d node(s)"), *AssetPath, NodeArray.Num()), Data);
#else
	return FMCPToolResult::Error(TEXT("get_graph requires an editor build"));
#endif
}

// =====================================================================
// add_node
// =====================================================================
FMCPToolResult FMCPTool_MetaSound::OpAddNode(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, NodeNamespace, NodeName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("node_namespace"), NodeNamespace, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("node_name"), NodeName, Err)) { return Err.GetValue(); }
	const FString NodeVariant = ExtractOptionalString(Params, TEXT("node_variant"));
	const int32 MajorVersion = ExtractOptionalNumber<int32>(Params, TEXT("major_version"), 1);
	const float PosX = ExtractOptionalNumber<float>(Params, TEXT("pos_x"), 0.0f);
	const float PosY = ExtractOptionalNumber<float>(Params, TEXT("pos_y"), 0.0f);

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	UMetaSoundBuilderBase* Builder = BeginEditing(AssetPath, Source, LoadError);
	if (!Builder)
	{
		return FMCPToolResult::Error(LoadError);
	}

	// Empty variant maps to NAME_None (most standard nodes have no variant).
	const FMetasoundFrontendClassName NodeClass(
		FName(*NodeNamespace),
		FName(*NodeName),
		NodeVariant.IsEmpty() ? FName() : FName(*NodeVariant));

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	FMetaSoundNodeHandle NodeHandle = Builder->AddNodeByClassName(NodeClass, R, MajorVersion);
	if (R != EMetaSoundBuilderResult::Succeeded || !NodeHandle.IsSet())
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("add_node: no registered MetaSound class '%s.%s%s' (major v%d). ")
			TEXT("Check namespace/name/variant against the node registry."),
			*NodeNamespace, *NodeName,
			NodeVariant.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(".%s"), *NodeVariant),
			MajorVersion));
	}

	EMetaSoundBuilderResult LocR;
	Builder->SetNodeLocation(NodeHandle, FVector2D(PosX, PosY), LocR);

	FString CommitError;
	if (!CommitEditing(AssetPath, Source, CommitError))
	{
		return FMCPToolResult::Error(CommitError);
	}

	const FString NodeId = NodeHandle.NodeID.ToString(EGuidFormats::DigitsWithHyphens);
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("node_id"), NodeId);
	Data->SetStringField(TEXT("class"), NodeClass.ToString());
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added node '%s.%s' (id %s)"), *NodeNamespace, *NodeName, *NodeId), Data);
#else
	return FMCPToolResult::Error(TEXT("add_node requires an editor build"));
#endif
}

// =====================================================================
// connect_nodes
// =====================================================================
FMCPToolResult FMCPTool_MetaSound::OpConnectNodes(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, FromNodeId, OutputName, ToNodeId, InputName;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("from_node_id"), FromNodeId, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("output_name"), OutputName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("to_node_id"), ToNodeId, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("input_name"), InputName, Err)) { return Err.GetValue(); }

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	UMetaSoundBuilderBase* Builder = BeginEditing(AssetPath, Source, LoadError);
	if (!Builder)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FGuid FromGuid, ToGuid;
	if (!ParseNodeGuid(FromNodeId, FromGuid)) { return FMCPToolResult::Error(FString::Printf(TEXT("Invalid from_node_id: '%s'"), *FromNodeId)); }
	if (!ParseNodeGuid(ToNodeId, ToGuid))     { return FMCPToolResult::Error(FString::Printf(TEXT("Invalid to_node_id: '%s'"), *ToNodeId)); }

	// Resolve the output pin handle, listing available pins on failure.
	EMetaSoundBuilderResult OutR = EMetaSoundBuilderResult::Failed;
	const FMetaSoundBuilderNodeOutputHandle OutHandle =
		Builder->FindNodeOutputByName(FMetaSoundNodeHandle(FromGuid), FName(*OutputName), OutR);
	if (OutR != EMetaSoundBuilderResult::Succeeded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("connect_nodes: output pin '%s' not found on node '%s'. Available outputs: [%s]"),
			*OutputName, *FromNodeId, *ListNodePinNames(Builder, FromGuid, /*bInputs=*/false)));
	}

	// Resolve the input pin handle, listing available pins on failure.
	EMetaSoundBuilderResult InR = EMetaSoundBuilderResult::Failed;
	const FMetaSoundBuilderNodeInputHandle InHandle =
		Builder->FindNodeInputByName(FMetaSoundNodeHandle(ToGuid), FName(*InputName), InR);
	if (InR != EMetaSoundBuilderResult::Succeeded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("connect_nodes: input pin '%s' not found on node '%s'. Available inputs: [%s]"),
			*InputName, *ToNodeId, *ListNodePinNames(Builder, ToGuid, /*bInputs=*/true)));
	}

	EMetaSoundBuilderResult ConnR = EMetaSoundBuilderResult::Failed;
	Builder->ConnectNodes(OutHandle, InHandle, ConnR);
	if (ConnR != EMetaSoundBuilderResult::Succeeded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("connect_nodes: failed to connect %s.%s -> %s.%s ")
			TEXT("(data-type mismatch, already connected, or incompatible access type)"),
			*FromNodeId, *OutputName, *ToNodeId, *InputName));
	}

	FString CommitError;
	if (!CommitEditing(AssetPath, Source, CommitError))
	{
		return FMCPToolResult::Error(CommitError);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("from_node_id"), FromNodeId);
	Data->SetStringField(TEXT("output_name"), OutputName);
	Data->SetStringField(TEXT("to_node_id"), ToNodeId);
	Data->SetStringField(TEXT("input_name"), InputName);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Connected %s.%s -> %s.%s"), *FromNodeId, *OutputName, *ToNodeId, *InputName), Data);
#else
	return FMCPToolResult::Error(TEXT("connect_nodes requires an editor build"));
#endif
}

// =====================================================================
// set_pin
// =====================================================================
FMCPToolResult FMCPTool_MetaSound::OpSetPin(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, NodeId, InputName, Value, DataType;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("node_id"), NodeId, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("input_name"), InputName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("value"), Value, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("data_type"), DataType, Err)) { return Err.GetValue(); }

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	UMetaSoundBuilderBase* Builder = BeginEditing(AssetPath, Source, LoadError);
	if (!Builder)
	{
		return FMCPToolResult::Error(LoadError);
	}

	FGuid NodeGuid;
	if (!ParseNodeGuid(NodeId, NodeGuid)) { return FMCPToolResult::Error(FString::Printf(TEXT("Invalid node_id: '%s'"), *NodeId)); }

	EMetaSoundBuilderResult FindR = EMetaSoundBuilderResult::Failed;
	const FMetaSoundBuilderNodeInputHandle InHandle =
		Builder->FindNodeInputByName(FMetaSoundNodeHandle(NodeGuid), FName(*InputName), FindR);
	if (FindR != EMetaSoundBuilderResult::Succeeded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("set_pin: input pin '%s' not found on node '%s'. Available inputs: [%s]"),
			*InputName, *NodeId, *ListNodePinNames(Builder, NodeGuid, /*bInputs=*/true)));
	}

	const FMetasoundFrontendLiteral Lit = MakeLiteralFromString(Value, DataType);

	EMetaSoundBuilderResult SetR = EMetaSoundBuilderResult::Failed;
	Builder->SetNodeInputDefault(InHandle, Lit, SetR);
	if (SetR != EMetaSoundBuilderResult::Succeeded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("set_pin: failed to set default on '%s.%s' (value '%s', type '%s'). ")
			TEXT("The pin may be connected, a constructor pin, or the value/type may be incompatible."),
			*NodeId, *InputName, *Value, *DataType));
	}

	FString CommitError;
	if (!CommitEditing(AssetPath, Source, CommitError))
	{
		return FMCPToolResult::Error(CommitError);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("node_id"), NodeId);
	Data->SetStringField(TEXT("input_name"), InputName);
	Data->SetStringField(TEXT("value"), Value);
	Data->SetStringField(TEXT("data_type"), DataType);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Set %s.%s = %s (%s)"), *NodeId, *InputName, *Value, *DataType), Data);
#else
	return FMCPToolResult::Error(TEXT("set_pin requires an editor build"));
#endif
}

// =====================================================================
// add_graph_input
// =====================================================================
FMCPToolResult FMCPTool_MetaSound::OpAddGraphInput(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, InputName, DataType;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("input_name"), InputName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("data_type"), DataType, Err)) { return Err.GetValue(); }
	const FString DefaultValue = ExtractOptionalString(Params, TEXT("default_value"));

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	UMetaSoundBuilderBase* Builder = BeginEditing(AssetPath, Source, LoadError);
	if (!Builder)
	{
		return FMCPToolResult::Error(LoadError);
	}

	const FMetasoundFrontendLiteral DefaultLit = DefaultValue.IsEmpty()
		? FMetasoundFrontendLiteral()
		: MakeLiteralFromString(DefaultValue, DataType);

	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	Builder->AddGraphInputNode(FName(*InputName), FName(*DataType), DefaultLit, R);
	if (R != EMetaSoundBuilderResult::Succeeded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("add_graph_input: failed to add input '%s' of type '%s'. ")
			TEXT("Check the data_type is a registered MetaSound type (Float/Int32/Bool/String/Audio/Trigger) ")
			TEXT("and the name is not already in use."),
			*InputName, *DataType));
	}

	FString CommitError;
	if (!CommitEditing(AssetPath, Source, CommitError))
	{
		return FMCPToolResult::Error(CommitError);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("input_name"), InputName);
	Data->SetStringField(TEXT("data_type"), DataType);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added graph input '%s' (%s)"), *InputName, *DataType), Data);
#else
	return FMCPToolResult::Error(TEXT("add_graph_input requires an editor build"));
#endif
}

// =====================================================================
// add_graph_output
// =====================================================================
FMCPToolResult FMCPTool_MetaSound::OpAddGraphOutput(const TSharedRef<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString AssetPath, OutputName, DataType;
	TOptional<FMCPToolResult> Err;
	if (!ExtractRequiredString(Params, TEXT("asset_path"), AssetPath, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("output_name"), OutputName, Err)) { return Err.GetValue(); }
	if (!ExtractRequiredString(Params, TEXT("data_type"), DataType, Err)) { return Err.GetValue(); }

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	UMetaSoundBuilderBase* Builder = BeginEditing(AssetPath, Source, LoadError);
	if (!Builder)
	{
		return FMCPToolResult::Error(LoadError);
	}

	const FMetasoundFrontendLiteral EmptyLit;
	EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
	Builder->AddGraphOutputNode(FName(*OutputName), FName(*DataType), EmptyLit, R);
	if (R != EMetaSoundBuilderResult::Succeeded)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("add_graph_output: failed to add output '%s' of type '%s'. ")
			TEXT("Check the data_type is a registered MetaSound type (Float/Int32/Bool/String/Audio/Trigger) ")
			TEXT("and the name is not already in use."),
			*OutputName, *DataType));
	}

	FString CommitError;
	if (!CommitEditing(AssetPath, Source, CommitError))
	{
		return FMCPToolResult::Error(CommitError);
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("output_name"), OutputName);
	Data->SetStringField(TEXT("data_type"), DataType);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Added graph output '%s' (%s)"), *OutputName, *DataType), Data);
#else
	return FMCPToolResult::Error(TEXT("add_graph_output requires an editor build"));
#endif
}
