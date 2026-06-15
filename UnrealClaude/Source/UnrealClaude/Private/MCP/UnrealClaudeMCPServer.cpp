// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealClaudeMCPServer.h"
#include "MCPToolRegistry.h"
#include "MCPResponseFormatter.h"
#include "UnrealClaudeModule.h"
#include "UnrealClaudeConstants.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Misc/Guid.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ===========================================================================
// File-local helpers
// ===========================================================================
namespace
{
	/** MCP protocol revisions we support, newest first. */
	static const TArray<FString>& SupportedProtocolVersions()
	{
		static const TArray<FString> Versions = {
			TEXT("2025-11-25"),
			TEXT("2025-06-18"),
			TEXT("2024-11-05")
		};
		return Versions;
	}

	/**
	 * Versions accepted in the MCP-Protocol-Version request header. Broader than the
	 * negotiation list so we tolerate the 2025-03-26 backwards-compat default; anything
	 * else that is explicitly present is rejected with 400 per the Streamable HTTP spec.
	 * An absent header is allowed (the spec says to assume the negotiated default).
	 */
	bool IsProtocolVersionAcceptable(const TMap<FString, FString>& Headers)
	{
		const FString* Version = Headers.Find(TEXT("mcp-protocol-version"));
		if (!Version || Version->IsEmpty())
		{
			return true;
		}
		static const TArray<FString> Known = {
			TEXT("2025-11-25"), TEXT("2025-06-18"), TEXT("2025-03-26"), TEXT("2024-11-05")
		};
		return Known.Contains(*Version);
	}

	/** Serialize a JSON object to a single-line (condensed) string. */
	FString SerializeJsonCondensed(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	/** Set a JSON-RPC "id" field, preserving numeric vs string vs null. */
	void SetJsonRpcId(const TSharedRef<FJsonObject>& Obj, const FString& RequestId)
	{
		if (RequestId.IsEmpty())
		{
			Obj->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
			return;
		}
		int32 Numeric = 0;
		if (LexTryParseString(Numeric, *RequestId) && FString::FromInt(Numeric) == RequestId)
		{
			Obj->SetNumberField(TEXT("id"), Numeric);
		}
		else
		{
			Obj->SetStringField(TEXT("id"), RequestId);
		}
	}

	/** Build a JSON-Schema inputSchema object from our tool's parameter list. */
	TSharedPtr<FJsonObject> BuildInputSchema(const FMCPToolInfo& Tool)
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> RequiredArr;

		for (const FMCPToolParameter& Param : Tool.Parameters)
		{
			TSharedPtr<FJsonObject> ParamSchema = MakeShared<FJsonObject>();
			const FString JsonType = Param.Type.IsEmpty() ? TEXT("string") : Param.Type;
			ParamSchema->SetStringField(TEXT("type"), JsonType);
			if (!Param.Description.IsEmpty())
			{
				ParamSchema->SetStringField(TEXT("description"), Param.Description);
			}
			// JSON Schema requires "items" for arrays; default to string elements.
			if (JsonType == TEXT("array"))
			{
				TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
				Items->SetStringField(TEXT("type"), TEXT("string"));
				ParamSchema->SetObjectField(TEXT("items"), Items);
			}
			Properties->SetObjectField(Param.Name, ParamSchema);

			if (Param.bRequired)
			{
				RequiredArr.Add(MakeShared<FJsonValueString>(Param.Name));
			}
		}

		Schema->SetObjectField(TEXT("properties"), Properties);
		if (RequiredArr.Num() > 0)
		{
			Schema->SetArrayField(TEXT("required"), RequiredArr);
		}
		return Schema;
	}

	/** Map an FMCPToolResult onto the MCP tools/call result shape (content blocks + isError). */
	TSharedPtr<FJsonObject> BuildMCPCallResult(const FMCPToolResult& Result)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Content;

		// Stringify the optional structured Data payload once.
		FString DataString;
		if (Result.Data.IsValid())
		{
			DataString = SerializeJsonCondensed(Result.Data.ToSharedRef());
		}

		if (Result.ContentType == EMCPToolResultType::Image && !Result.Base64Payload.IsEmpty())
		{
			TSharedPtr<FJsonObject> ImageBlock = MakeShared<FJsonObject>();
			ImageBlock->SetStringField(TEXT("type"), TEXT("image"));
			ImageBlock->SetStringField(TEXT("data"), Result.Base64Payload);
			ImageBlock->SetStringField(TEXT("mimeType"), Result.MimeType.IsEmpty() ? TEXT("image/png") : Result.MimeType);
			Content.Add(MakeShared<FJsonValueObject>(ImageBlock));

			// Companion text block (message + metadata, without re-emitting the base64).
			FString Text = Result.Message;
			if (!DataString.IsEmpty())
			{
				if (!Text.IsEmpty()) { Text += TEXT("\n"); }
				Text += DataString;
			}
			if (!Text.IsEmpty())
			{
				TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
				TextBlock->SetStringField(TEXT("type"), TEXT("text"));
				TextBlock->SetStringField(TEXT("text"), Text);
				Content.Add(MakeShared<FJsonValueObject>(TextBlock));
			}
		}
		else
		{
			FString Text = Result.Message;
			if (!DataString.IsEmpty())
			{
				if (!Text.IsEmpty()) { Text += TEXT("\n"); }
				Text += DataString;
			}
			for (const FString& Warning : Result.Warnings)
			{
				Text += FString::Printf(TEXT("\n[warning] %s"), *Warning);
			}
			TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
			TextBlock->SetStringField(TEXT("type"), TEXT("text"));
			TextBlock->SetStringField(TEXT("text"), Text);
			Content.Add(MakeShared<FJsonValueObject>(TextBlock));
		}

		Out->SetArrayField(TEXT("content"), Content);
		Out->SetBoolField(TEXT("isError"), !Result.bSuccess);
		return Out;
	}

	/**
	 * Execute a tool, marshaling onto the game thread if we're on the socket
	 * worker thread. Ported from VibeUE's FToolExecState pattern: a heap-allocated
	 * shared state outlives whichever of {caller, game-thread task} finishes last.
	 */
	FMCPToolResult ExecuteToolMarshaled(TSharedPtr<FMCPToolRegistry> Registry,
		const FString& ToolName, const TSharedRef<FJsonObject>& Params)
	{
		if (!Registry.IsValid())
		{
			return FMCPToolResult::Error(TEXT("Tool registry not initialized"));
		}

		if (IsInGameThread())
		{
			return Registry->ExecuteTool(ToolName, Params);
		}

		struct FExecState
		{
			FMCPToolResult Result;
			FThreadSafeBool bStarted{ false };
			FThreadSafeBool bTimedOut{ false };
			FEvent* Event = nullptr;
			~FExecState()
			{
				if (Event)
				{
					FPlatformProcess::ReturnSynchEventToPool(Event);
					Event = nullptr;
				}
			}
		};

		TSharedPtr<FExecState> State = MakeShared<FExecState>();
		State->Event = FPlatformProcess::GetSynchEventFromPool(false);

		TWeakPtr<FMCPToolRegistry> WeakReg = Registry;
		AsyncTask(ENamedThreads::GameThread, [State, ToolName, Params, WeakReg]()
		{
			State->bStarted = true;
			if (State->bTimedOut) { return; }
			TSharedPtr<FMCPToolRegistry> Reg = WeakReg.Pin();
			State->Result = Reg.IsValid()
				? Reg->ExecuteTool(ToolName, Params)
				: FMCPToolResult::Error(TEXT("Tool registry is no longer available"));
			if (!State->bTimedOut && State->Event)
			{
				State->Event->Trigger();
			}
		});

		const double TimeoutSeconds = 60.0;
		if (State->Event->Wait(FTimespan::FromSeconds(TimeoutSeconds)))
		{
			return State->Result;
		}

		State->bTimedOut = true;
		return FMCPToolResult::Error(State->bStarted
			? TEXT("Tool execution timed out while running on the game thread")
			: TEXT("Tool execution timed out (the game thread may be blocked)"));
	}
}

// ===========================================================================
// Lifecycle
// ===========================================================================
FUnrealClaudeMCPServer::FUnrealClaudeMCPServer()
{
	ToolRegistry = MakeShared<FMCPToolRegistry>();
}

FUnrealClaudeMCPServer::~FUnrealClaudeMCPServer()
{
	StopServer();
}

bool FUnrealClaudeMCPServer::Start(uint32 Port)
{
	if (bIsRunning)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("MCP Server is already running on port %d"), ServerPort);
		return true;
	}

	ServerPort = Port;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("MCP Server: failed to get socket subsystem"));
		return false;
	}

	// Bind to localhost only (prevents remote/DNS-rebinding access).
	FIPv4Address LocalAddress;
	FIPv4Address::Parse(TEXT("127.0.0.1"), LocalAddress);
	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	Addr->SetIp(LocalAddress.Value);
	Addr->SetPort(ServerPort);

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealClaudeMCPServer"), false);
	if (!ListenerSocket)
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("MCP Server: failed to create listener socket"));
		return false;
	}

	ListenerSocket->SetReuseAddr(true);
	ListenerSocket->SetNonBlocking(true);

	if (!ListenerSocket->Bind(*Addr))
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("MCP Server: failed to bind port %d (is another process using it?)"), ServerPort);
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	if (!ListenerSocket->Listen(8))
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("MCP Server: failed to listen on port %d"), ServerPort);
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	bShouldStop = false;
	bIsRunning = true;

	if (ToolRegistry.IsValid())
	{
		ToolRegistry->StartTaskQueue();
	}

	ServerThread = FRunnableThread::Create(this, TEXT("UnrealClaudeMCPServerThread"), 0, TPri_Normal);
	if (!ServerThread)
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("MCP Server: failed to create worker thread"));
		StopServer();
		return false;
	}

	UE_LOG(LogUnrealClaude, Log, TEXT("MCP Server started on http://127.0.0.1:%d"), ServerPort);
	UE_LOG(LogUnrealClaude, Log, TEXT("  POST   /mcp            - native MCP (JSON-RPC: initialize, tools/list, tools/call, ping)"));
	UE_LOG(LogUnrealClaude, Log, TEXT("  GET    /mcp/tools      - [legacy REST] list tools"));
	UE_LOG(LogUnrealClaude, Log, TEXT("  POST   /mcp/tool/{name}- [legacy REST] execute a tool"));
	UE_LOG(LogUnrealClaude, Log, TEXT("  GET    /mcp/status     - [legacy REST] server status"));
	return true;
}

void FUnrealClaudeMCPServer::StopServer()
{
	if (!bIsRunning && !ServerThread && !ListenerSocket)
	{
		return;
	}

	bShouldStop = true;

	if (ToolRegistry.IsValid())
	{
		ToolRegistry->StopTaskQueue();
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	// Close the listener first to unblock WaitForPendingConnection in Run().
	if (ListenerSocket)
	{
		ListenerSocket->Close();
	}

	if (ServerThread)
	{
		ServerThread->Kill(true); // blocks until Run() returns
		delete ServerThread;
		ServerThread = nullptr;
	}

	if (ListenerSocket)
	{
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ListenerSocket);
		}
		ListenerSocket = nullptr;
	}

	{
		FScopeLock Lock(&SessionLock);
		ActiveSessions.Empty();
	}

	bIsRunning = false;
	UE_LOG(LogUnrealClaude, Log, TEXT("MCP Server stopped"));
}

// ===========================================================================
// FRunnable
// ===========================================================================
bool FUnrealClaudeMCPServer::Init()
{
	return true;
}

uint32 FUnrealClaudeMCPServer::Run()
{
	while (!bShouldStop)
	{
		if (!ListenerSocket)
		{
			break;
		}

		bool bHasPendingConnection = false;
		if (ListenerSocket->WaitForPendingConnection(bHasPendingConnection, FTimespan::FromMilliseconds(100)))
		{
			if (bHasPendingConnection && !bShouldStop)
			{
				FSocket* ClientSocket = ListenerSocket->Accept(TEXT("UnrealClaudeMCPClient"));
				if (ClientSocket)
				{
					HandleConnection(ClientSocket);
				}
			}
		}

		FPlatformProcess::Sleep(0.001f);
	}
	return 0;
}

void FUnrealClaudeMCPServer::Stop()
{
	bShouldStop = true;
}

void FUnrealClaudeMCPServer::Exit()
{
}

// ===========================================================================
// Connection handling
// ===========================================================================
void FUnrealClaudeMCPServer::HandleConnection(FSocket* ClientSocket)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	auto Cleanup = [&]()
	{
		if (ClientSocket)
		{
			ClientSocket->Close();
			if (SocketSubsystem)
			{
				SocketSubsystem->DestroySocket(ClientSocket);
			}
		}
	};

	FString Method, Path, Body;
	TMap<FString, FString> Headers;
	if (!ParseHttpRequest(ClientSocket, Method, Path, Headers, Body))
	{
		SendHttpResponse(ClientSocket, 400, TEXT("Bad Request"), TEXT("text/plain"), TEXT("Bad Request"));
		Cleanup();
		return;
	}

	// CORS preflight.
	if (Method == TEXT("OPTIONS"))
	{
		TMap<FString, FString> H;
		H.Add(TEXT("Access-Control-Allow-Origin"), TEXT("*"));
		H.Add(TEXT("Access-Control-Allow-Methods"), TEXT("GET, POST, DELETE, OPTIONS"));
		H.Add(TEXT("Access-Control-Allow-Headers"), TEXT("Content-Type, Authorization, Mcp-Session-Id, MCP-Protocol-Version, Accept, Last-Event-ID"));
		H.Add(TEXT("Access-Control-Max-Age"), TEXT("86400"));
		SendHttpResponse(ClientSocket, 204, TEXT("No Content"), TEXT(""), TEXT(""), H);
		Cleanup();
		return;
	}

	if (!Path.StartsWith(TEXT("/mcp")))
	{
		SendHttpResponse(ClientSocket, 404, TEXT("Not Found"), TEXT("text/plain"), TEXT("Not Found"));
		Cleanup();
		return;
	}

	if (!ValidateOrigin(Headers))
	{
		SendHttpResponse(ClientSocket, 403, TEXT("Forbidden"), TEXT("text/plain"), TEXT("Forbidden origin"));
		Cleanup();
		return;
	}

	if (!ValidateApiKey(Headers))
	{
		SendHttpResponse(ClientSocket, 401, TEXT("Unauthorized"), TEXT("text/plain"), TEXT("Unauthorized"));
		Cleanup();
		return;
	}

	// Reject an explicitly unsupported MCP-Protocol-Version (Streamable HTTP MUST).
	// An absent header is allowed; the legacy bridge never sends it, so REST is unaffected.
	if (!IsProtocolVersionAcceptable(Headers))
	{
		SendHttpResponse(ClientSocket, 400, TEXT("Bad Request"), TEXT("text/plain"), TEXT("Unsupported MCP-Protocol-Version"));
		Cleanup();
		return;
	}

	// Legacy REST paths (kept so the existing Node bridge keeps working).
	if (Path.StartsWith(TEXT("/mcp/tools")) || Path.StartsWith(TEXT("/mcp/tool")) || Path.StartsWith(TEXT("/mcp/status")))
	{
		HandleLegacyRest(ClientSocket, Method, Path, Body);
		Cleanup();
		return;
	}

	// Native MCP at /mcp.
	if (Method == TEXT("POST"))
	{
		FString SessionId = Headers.FindRef(TEXT("mcp-session-id"));
		bool bIsNotification = false;
		const FString ResponseJson = HandleMCPRequest(Body, SessionId, bIsNotification);

		TMap<FString, FString> H;
		H.Add(TEXT("Access-Control-Allow-Origin"), TEXT("*"));
		if (!SessionId.IsEmpty())
		{
			H.Add(TEXT("Mcp-Session-Id"), SessionId);
		}

		if (bIsNotification)
		{
			// JSON-RPC notifications/responses from the client get 202 with no body.
			SendHttpResponse(ClientSocket, 202, TEXT("Accepted"), TEXT(""), TEXT(""), H);
		}
		else
		{
			// The client MUST accept both application/json and text/event-stream, so a
			// single JSON response is always valid and is the most robust choice. A
			// streamed SSE response is reserved for when we emit progress/server messages
			// mid-call (M3); the SSE helpers below stand ready for that.
			SendHttpResponse(ClientSocket, 200, TEXT("OK"), TEXT("application/json"), ResponseJson, H);
		}
		Cleanup();
		return;
	}

	if (Method == TEXT("DELETE"))
	{
		const FString SessionId = Headers.FindRef(TEXT("mcp-session-id"));
		if (!SessionId.IsEmpty())
		{
			FScopeLock Lock(&SessionLock);
			ActiveSessions.Remove(SessionId);
		}
		SendHttpResponse(ClientSocket, 200, TEXT("OK"), TEXT("text/plain"), TEXT("Session terminated"));
		Cleanup();
		return;
	}

	// We don't (yet) offer a server-initiated GET SSE stream.
	SendHttpResponse(ClientSocket, 405, TEXT("Method Not Allowed"), TEXT("text/plain"), TEXT("Method Not Allowed"));
	Cleanup();
}

bool FUnrealClaudeMCPServer::ParseHttpRequest(FSocket* Socket, FString& OutMethod, FString& OutPath,
	TMap<FString, FString>& OutHeaders, FString& OutBody)
{
	Socket->SetNonBlocking(false);

	TArray<uint8> Raw;
	int32 HeaderEnd = INDEX_NONE;
	int32 ContentLength = 0;
	bool bHaveContentLength = false;
	const int32 MaxSize = UnrealClaudeConstants::MCPServer::MaxRequestBodySize;
	uint8 Buffer[8192];
	int32 EmptyReads = 0;

	auto FindHeaderEnd = [](const TArray<uint8>& Data) -> int32
	{
		for (int32 i = 0; i + 3 < Data.Num(); ++i)
		{
			if (Data[i] == '\r' && Data[i + 1] == '\n' && Data[i + 2] == '\r' && Data[i + 3] == '\n')
			{
				return i;
			}
		}
		return INDEX_NONE;
	};

	auto BytesToString = [](const uint8* Ptr, int32 Len) -> FString
	{
		if (Len <= 0)
		{
			return FString();
		}
		TArray<uint8> Tmp;
		Tmp.Append(Ptr, Len);
		Tmp.Add(0);
		return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Tmp.GetData())));
	};

	while (true)
	{
		if (HeaderEnd == INDEX_NONE)
		{
			HeaderEnd = FindHeaderEnd(Raw);
			if (HeaderEnd != INDEX_NONE)
			{
				const FString HeaderStr = BytesToString(Raw.GetData(), HeaderEnd);
				TArray<FString> Lines;
				HeaderStr.ParseIntoArray(Lines, TEXT("\r\n"), true);
				for (const FString& Line : Lines)
				{
					if (Line.StartsWith(TEXT("Content-Length:"), ESearchCase::IgnoreCase))
					{
						ContentLength = FCString::Atoi(*Line.Mid(15).TrimStartAndEnd());
						bHaveContentLength = true;
					}
				}
			}
		}

		if (HeaderEnd != INDEX_NONE)
		{
			const int32 BodySoFar = Raw.Num() - (HeaderEnd + 4);
			if (!bHaveContentLength || BodySoFar >= ContentLength)
			{
				break;
			}
		}

		if (Raw.Num() > MaxSize)
		{
			break;
		}

		if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0)))
		{
			if (++EmptyReads >= 3)
			{
				break;
			}
			continue;
		}

		int32 BytesRead = 0;
		if (Socket->Recv(Buffer, sizeof(Buffer), BytesRead) && BytesRead > 0)
		{
			EmptyReads = 0;
			Raw.Append(Buffer, BytesRead);
		}
		else if (++EmptyReads >= 3)
		{
			break;
		}
	}

	if (HeaderEnd == INDEX_NONE)
	{
		return false;
	}

	const FString HeaderStr = BytesToString(Raw.GetData(), HeaderEnd);
	TArray<FString> Lines;
	HeaderStr.ParseIntoArray(Lines, TEXT("\r\n"), true);
	if (Lines.Num() == 0)
	{
		return false;
	}

	TArray<FString> RequestLineParts;
	Lines[0].ParseIntoArrayWS(RequestLineParts);
	if (RequestLineParts.Num() < 2)
	{
		return false;
	}
	OutMethod = RequestLineParts[0];
	OutPath = RequestLineParts[1];

	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		int32 ColonIdx = INDEX_NONE;
		if (Lines[i].FindChar(TEXT(':'), ColonIdx))
		{
			const FString Key = Lines[i].Left(ColonIdx).TrimStartAndEnd().ToLower();
			const FString Value = Lines[i].Mid(ColonIdx + 1).TrimStartAndEnd();
			OutHeaders.Add(Key, Value);
		}
	}

	const int32 BodyStart = HeaderEnd + 4;
	const int32 BodyLen = Raw.Num() - BodyStart;
	OutBody = (BodyLen > 0) ? BytesToString(Raw.GetData() + BodyStart, BodyLen) : FString();
	return true;
}

void FUnrealClaudeMCPServer::SendHttpResponse(FSocket* Socket, int32 StatusCode, const FString& StatusText,
	const FString& ContentType, const FString& Body, const TMap<FString, FString>& ExtraHeaders)
{
	if (!Socket)
	{
		return;
	}

	FTCHARToUTF8 BodyUtf8(*Body);
	const int32 BodyByteLength = BodyUtf8.Length();

	FString Head = FString::Printf(TEXT("HTTP/1.1 %d %s\r\n"), StatusCode, *StatusText);
	if (!ContentType.IsEmpty())
	{
		Head += FString::Printf(TEXT("Content-Type: %s; charset=utf-8\r\n"), *ContentType);
	}
	Head += FString::Printf(TEXT("Content-Length: %d\r\n"), BodyByteLength);
	Head += TEXT("Connection: close\r\n");
	for (const TPair<FString, FString>& Header : ExtraHeaders)
	{
		Head += FString::Printf(TEXT("%s: %s\r\n"), *Header.Key, *Header.Value);
	}
	Head += TEXT("\r\n");

	FTCHARToUTF8 HeadUtf8(*Head);
	TArray<uint8> Out;
	Out.Append(reinterpret_cast<const uint8*>(HeadUtf8.Get()), HeadUtf8.Length());
	if (BodyByteLength > 0)
	{
		Out.Append(reinterpret_cast<const uint8*>(BodyUtf8.Get()), BodyByteLength);
	}

	int32 Total = 0;
	while (Total < Out.Num())
	{
		int32 Sent = 0;
		if (!Socket->Send(Out.GetData() + Total, Out.Num() - Total, Sent) || Sent <= 0)
		{
			break;
		}
		Total += Sent;
	}
}

// ===========================================================================
// MCP JSON-RPC
// ===========================================================================
FString FUnrealClaudeMCPServer::HandleMCPRequest(const FString& JsonBody, FString& InOutSessionId, bool& bOutIsNotification)
{
	bOutIsNotification = false;

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return BuildJsonRpcError(FString(), -32700, TEXT("Parse error"));
	}

	FString JsonRpc;
	if (!Root->TryGetStringField(TEXT("jsonrpc"), JsonRpc) || JsonRpc != TEXT("2.0"))
	{
		return BuildJsonRpcError(FString(), -32600, TEXT("Invalid Request: jsonrpc must be \"2.0\""));
	}

	FString Method;
	if (!Root->TryGetStringField(TEXT("method"), Method))
	{
		// A JSON-RPC *response* returning from the client (has result/error, no method)
		// is valid input — the spec says to accept it with 202 Accepted and no body.
		if (Root->HasField(TEXT("result")) || Root->HasField(TEXT("error")))
		{
			bOutIsNotification = true;
			return FString();
		}
		return BuildJsonRpcError(FString(), -32600, TEXT("Invalid Request: missing method"));
	}

	// id may be a number, a string, or absent (= notification).
	FString RequestId;
	bool bHasId = false;
	if (Root->HasField(TEXT("id")))
	{
		const TSharedPtr<FJsonValue> IdVal = Root->TryGetField(TEXT("id"));
		if (IdVal.IsValid() && IdVal->Type != EJson::Null)
		{
			bHasId = true;
			if (IdVal->Type == EJson::Number)
			{
				RequestId = FString::Printf(TEXT("%d"), static_cast<int32>(IdVal->AsNumber()));
			}
			else
			{
				RequestId = IdVal->AsString();
			}
		}
	}
	if (!bHasId)
	{
		bOutIsNotification = true;
	}

	TSharedPtr<FJsonObject> Params;
	const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
	if (Root->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr)
	{
		Params = *ParamsPtr;
	}

	if (Method == TEXT("initialize"))
	{
		const FString Response = HandleInitialize(Params, RequestId);
		const FString NewSession = GenerateSessionId();
		{
			FScopeLock Lock(&SessionLock);
			ActiveSessions.Add(NewSession);
		}
		InOutSessionId = NewSession;
		return Response;
	}
	if (Method == TEXT("notifications/initialized") || Method == TEXT("initialized") ||
		Method == TEXT("notifications/cancelled"))
	{
		bOutIsNotification = true;
		return FString();
	}
	if (Method == TEXT("tools/list"))
	{
		return HandleToolsList(Params, RequestId);
	}
	if (Method == TEXT("tools/call"))
	{
		return HandleToolsCall(Params, RequestId);
	}
	if (Method == TEXT("ping"))
	{
		return HandlePing(RequestId);
	}

	return BuildJsonRpcError(RequestId, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
}

FString FUnrealClaudeMCPServer::HandleInitialize(const TSharedPtr<FJsonObject>& Params, const FString& RequestId)
{
	FString Requested;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("protocolVersion"), Requested);
	}
	const TArray<FString>& Supported = SupportedProtocolVersions();
	const FString Negotiated = Supported.Contains(Requested) ? Requested : Supported[0];

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), Negotiated);

	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());
	Result->SetObjectField(TEXT("capabilities"), Capabilities);

	TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), TEXT("UnrealClaude"));
	ServerInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

	return BuildJsonRpcResponse(RequestId, Result);
}

FString FUnrealClaudeMCPServer::HandleToolsList(const TSharedPtr<FJsonObject>& Params, const FString& RequestId)
{
	TArray<TSharedPtr<FJsonValue>> ToolsArray;

	if (ToolRegistry.IsValid())
	{
		for (const FMCPToolInfo& Tool : ToolRegistry->GetAllTools())
		{
			TSharedPtr<FJsonObject> ToolJson = MakeShared<FJsonObject>();
			ToolJson->SetStringField(TEXT("name"), Tool.Name);
			ToolJson->SetStringField(TEXT("description"), Tool.Description);
			ToolJson->SetObjectField(TEXT("inputSchema"), BuildInputSchema(Tool));

			TSharedPtr<FJsonObject> Annotations = MakeShared<FJsonObject>();
			Annotations->SetBoolField(TEXT("readOnlyHint"), Tool.Annotations.bReadOnlyHint);
			Annotations->SetBoolField(TEXT("destructiveHint"), Tool.Annotations.bDestructiveHint);
			Annotations->SetBoolField(TEXT("idempotentHint"), Tool.Annotations.bIdempotentHint);
			Annotations->SetBoolField(TEXT("openWorldHint"), Tool.Annotations.bOpenWorldHint);
			ToolJson->SetObjectField(TEXT("annotations"), Annotations);

			ToolsArray.Add(MakeShared<FJsonValueObject>(ToolJson));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), ToolsArray);
	return BuildJsonRpcResponse(RequestId, Result);
}

FString FUnrealClaudeMCPServer::HandleToolsCall(const TSharedPtr<FJsonObject>& Params, const FString& RequestId)
{
	if (!Params.IsValid())
	{
		return BuildJsonRpcError(RequestId, -32602, TEXT("Invalid params"));
	}

	FString ToolName;
	if (!Params->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty())
	{
		return BuildJsonRpcError(RequestId, -32602, TEXT("Missing tool name"));
	}

	TSharedPtr<FJsonObject> Arguments;
	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && (*ArgsObj).IsValid())
	{
		Arguments = *ArgsObj;
	}
	else
	{
		Arguments = MakeShared<FJsonObject>();
	}

	if (!ToolRegistry.IsValid())
	{
		return BuildJsonRpcError(RequestId, -32603, TEXT("Tool registry not initialized"));
	}
	if (!ToolRegistry->HasTool(ToolName))
	{
		return BuildJsonRpcError(RequestId, -32602, FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
	}

	const FMCPToolResult Result = ExecuteToolMarshaled(ToolRegistry, ToolName, Arguments.ToSharedRef());
	return BuildJsonRpcResponse(RequestId, BuildMCPCallResult(Result));
}

FString FUnrealClaudeMCPServer::HandlePing(const FString& RequestId)
{
	return BuildJsonRpcResponse(RequestId, MakeShared<FJsonObject>());
}

FString FUnrealClaudeMCPServer::BuildJsonRpcResponse(const FString& RequestId, TSharedPtr<FJsonObject> Result)
{
	TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	SetJsonRpcId(Response, RequestId);
	Response->SetObjectField(TEXT("result"), Result.IsValid() ? Result : MakeShared<FJsonObject>());
	return SerializeJsonCondensed(Response);
}

FString FUnrealClaudeMCPServer::BuildJsonRpcError(const FString& RequestId, int32 Code, const FString& Message)
{
	TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	SetJsonRpcId(Response, RequestId);

	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetNumberField(TEXT("code"), Code);
	Error->SetStringField(TEXT("message"), Message);
	Response->SetObjectField(TEXT("error"), Error);

	return SerializeJsonCondensed(Response);
}

// ===========================================================================
// Legacy REST compatibility (Node bridge)
// ===========================================================================
void FUnrealClaudeMCPServer::HandleLegacyRest(FSocket* Socket, const FString& Verb, const FString& Path, const FString& Body)
{
	TMap<FString, FString> H;
	H.Add(TEXT("Access-Control-Allow-Origin"), TEXT("*"));

	// GET /mcp/status
	if (Path.StartsWith(TEXT("/mcp/status")))
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("status"), TEXT("running"));
		Obj->SetNumberField(TEXT("port"), ServerPort);
		Obj->SetStringField(TEXT("version"), TEXT("1.0.0"));
		Obj->SetNumberField(TEXT("toolCount"), ToolRegistry.IsValid() ? ToolRegistry->GetAllTools().Num() : 0);
		if (ToolRegistry.IsValid())
		{
			TArray<TSharedPtr<FJsonValue>> ToolsArray;
			for (const FMCPToolInfo& ToolInfo : ToolRegistry->GetAllTools())
			{
				TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
				ToolObj->SetStringField(TEXT("name"), ToolInfo.Name);
				ToolObj->SetStringField(TEXT("description"), ToolInfo.Description);
				ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
			}
			Obj->SetArrayField(TEXT("tools"), ToolsArray);
		}
		Obj->SetStringField(TEXT("projectName"), FApp::GetProjectName());
		Obj->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
		SendHttpResponse(Socket, 200, TEXT("OK"), TEXT("application/json"), SerializeJsonCondensed(Obj), H);
		return;
	}

	// GET /mcp/tools
	if (Path.StartsWith(TEXT("/mcp/tools")))
	{
		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		if (ToolRegistry.IsValid())
		{
			for (const FMCPToolInfo& Tool : ToolRegistry->GetAllTools())
			{
				TSharedPtr<FJsonObject> ToolJson = MakeShared<FJsonObject>();
				ToolJson->SetStringField(TEXT("name"), Tool.Name);
				ToolJson->SetStringField(TEXT("description"), Tool.Description);

				TArray<TSharedPtr<FJsonValue>> ParamsArray;
				for (const FMCPToolParameter& Param : Tool.Parameters)
				{
					TSharedPtr<FJsonObject> ParamJson = MakeShared<FJsonObject>();
					ParamJson->SetStringField(TEXT("name"), Param.Name);
					ParamJson->SetStringField(TEXT("type"), Param.Type);
					ParamJson->SetStringField(TEXT("description"), Param.Description);
					ParamJson->SetBoolField(TEXT("required"), Param.bRequired);
					if (!Param.DefaultValue.IsEmpty())
					{
						ParamJson->SetStringField(TEXT("default"), Param.DefaultValue);
					}
					ParamsArray.Add(MakeShared<FJsonValueObject>(ParamJson));
				}
				ToolJson->SetArrayField(TEXT("parameters"), ParamsArray);

				TSharedPtr<FJsonObject> AnnotationsJson = MakeShared<FJsonObject>();
				AnnotationsJson->SetBoolField(TEXT("readOnlyHint"), Tool.Annotations.bReadOnlyHint);
				AnnotationsJson->SetBoolField(TEXT("destructiveHint"), Tool.Annotations.bDestructiveHint);
				AnnotationsJson->SetBoolField(TEXT("idempotentHint"), Tool.Annotations.bIdempotentHint);
				AnnotationsJson->SetBoolField(TEXT("openWorldHint"), Tool.Annotations.bOpenWorldHint);
				ToolJson->SetObjectField(TEXT("annotations"), AnnotationsJson);

				ToolsArray.Add(MakeShared<FJsonValueObject>(ToolJson));
			}
		}
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetArrayField(TEXT("tools"), ToolsArray);
		SendHttpResponse(Socket, 200, TEXT("OK"), TEXT("application/json"), SerializeJsonCondensed(Obj), H);
		return;
	}

	// POST /mcp/tool/{name}
	if (Path.StartsWith(TEXT("/mcp/tool")))
	{
		FString ToolName;
		if (Path.StartsWith(TEXT("/mcp/tool/")))
		{
			ToolName = Path.RightChop(10);
		}
		int32 QueryIdx = INDEX_NONE;
		if (ToolName.FindChar(TEXT('?'), QueryIdx))
		{
			ToolName = ToolName.Left(QueryIdx);
		}

		if (ToolName.IsEmpty())
		{
			TSharedPtr<FJsonObject> Err = UnrealClaude::MCP::BuildErrorEnvelopeJson(TEXT("Tool name not specified. Use POST /mcp/tool/{toolname}"));
			SendHttpResponse(Socket, 400, TEXT("Bad Request"), TEXT("application/json"), SerializeJsonCondensed(Err.ToSharedRef()), H);
			return;
		}

		TSharedPtr<FJsonObject> ParamsJson = MakeShared<FJsonObject>();
		if (!Body.IsEmpty())
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (!FJsonSerializer::Deserialize(Reader, ParamsJson) || !ParamsJson.IsValid())
			{
				TSharedPtr<FJsonObject> Err = UnrealClaude::MCP::BuildErrorEnvelopeJson(TEXT("Invalid JSON body"));
				SendHttpResponse(Socket, 400, TEXT("Bad Request"), TEXT("application/json"), SerializeJsonCondensed(Err.ToSharedRef()), H);
				return;
			}
		}

		if (!ToolRegistry.IsValid())
		{
			TSharedPtr<FJsonObject> Err = UnrealClaude::MCP::BuildErrorEnvelopeJson(TEXT("Tool registry not initialized"));
			SendHttpResponse(Socket, 500, TEXT("Internal Server Error"), TEXT("application/json"), SerializeJsonCondensed(Err.ToSharedRef()), H);
			return;
		}

		const FMCPToolResult Result = ExecuteToolMarshaled(ToolRegistry, ToolName, ParamsJson.ToSharedRef());
		TSharedPtr<FJsonObject> Envelope = UnrealClaude::MCP::BuildToolResultJson(Result);
		const int32 Code = Result.bSuccess ? 200 : 400;
		SendHttpResponse(Socket, Code, Result.bSuccess ? TEXT("OK") : TEXT("Bad Request"),
			TEXT("application/json"), SerializeJsonCondensed(Envelope.ToSharedRef()), H);
		return;
	}

	SendHttpResponse(Socket, 404, TEXT("Not Found"), TEXT("text/plain"), TEXT("Not Found"), H);
}

// ===========================================================================
// Server-Sent Events
// ===========================================================================
bool FUnrealClaudeMCPServer::AcceptsSSE(const TMap<FString, FString>& Headers) const
{
	const FString* Accept = Headers.Find(TEXT("accept"));
	return Accept && Accept->Contains(TEXT("text/event-stream"));
}

void FUnrealClaudeMCPServer::SendSSEResponse(FSocket* Socket, const FString& SessionId)
{
	if (!Socket)
	{
		return;
	}
	FString Head = TEXT("HTTP/1.1 200 OK\r\n");
	Head += TEXT("Content-Type: text/event-stream; charset=utf-8\r\n");
	Head += TEXT("Cache-Control: no-cache\r\n");
	Head += TEXT("Connection: keep-alive\r\n");
	Head += TEXT("Access-Control-Allow-Origin: *\r\n");
	if (!SessionId.IsEmpty())
	{
		Head += FString::Printf(TEXT("Mcp-Session-Id: %s\r\n"), *SessionId);
	}
	Head += TEXT("\r\n");

	FTCHARToUTF8 Utf8(*Head);
	int32 Sent = 0;
	Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent);
}

void FUnrealClaudeMCPServer::SendSSEEvent(FSocket* Socket, const FString& Data, int32 EventId)
{
	if (!Socket)
	{
		return;
	}
	FString Event;
	if (EventId >= 0)
	{
		Event += FString::Printf(TEXT("id: %d\n"), EventId);
	}
	if (Data.IsEmpty())
	{
		Event += TEXT("data: \n");
	}
	else
	{
		TArray<FString> DataLines;
		Data.ParseIntoArray(DataLines, TEXT("\n"), false);
		for (const FString& Line : DataLines)
		{
			Event += FString::Printf(TEXT("data: %s\n"), *Line);
		}
	}
	Event += TEXT("\n");

	FTCHARToUTF8 Utf8(*Event);
	int32 Sent = 0;
	Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent);
}

// ===========================================================================
// Security (local-only)
// ===========================================================================
bool FUnrealClaudeMCPServer::ValidateOrigin(const TMap<FString, FString>& Headers) const
{
	const FString* Origin = Headers.Find(TEXT("origin"));
	if (!Origin)
	{
		// No Origin header — non-browser client (curl, IDE). Allow.
		return true;
	}
	return Origin->Contains(TEXT("localhost"))
		|| Origin->Contains(TEXT("127.0.0.1"))
		|| Origin->StartsWith(TEXT("vscode-webview://"))
		|| Origin->StartsWith(TEXT("file://"));
}

bool FUnrealClaudeMCPServer::ValidateApiKey(const TMap<FString, FString>& Headers) const
{
	if (LocalApiKey.IsEmpty())
	{
		return true; // No key configured — open to localhost.
	}

	const FString* AuthHeader = Headers.Find(TEXT("authorization"));
	if (!AuthHeader)
	{
		return false;
	}
	if (AuthHeader->StartsWith(TEXT("Bearer "), ESearchCase::IgnoreCase))
	{
		return AuthHeader->Mid(7) == LocalApiKey;
	}
	return *AuthHeader == LocalApiKey;
}

FString FUnrealClaudeMCPServer::GenerateSessionId() const
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}
