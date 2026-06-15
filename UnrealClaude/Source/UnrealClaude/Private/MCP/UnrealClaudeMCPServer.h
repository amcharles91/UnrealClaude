// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Dom/JsonObject.h"
#include "UnrealClaudeConstants.h"

class FMCPToolRegistry;
class FRunnableThread;
class FSocket;

/**
 * Native MCP server for editor control.
 *
 * Speaks the Model Context Protocol over a hand-rolled HTTP/1.1 layer on a raw
 * TCP socket (localhost only), modeled on the VibeUE plugin's approach. This
 * lets MCP clients (Claude Code, etc.) connect DIRECTLY to the editor — no Node
 * bridge required.
 *
 * Endpoints (all on the same port):
 *   POST   /mcp            - JSON-RPC 2.0: initialize, tools/list, tools/call, ping, notifications/*
 *   GET    /mcp            - opens a Server-Sent-Events stream (parked; hook for future progress pushes)
 *   DELETE /mcp            - terminate an MCP session
 *   GET    /mcp/tools      - [legacy REST] list tools          (kept so the Node bridge keeps working)
 *   POST   /mcp/tool/{name}- [legacy REST] execute a tool      (kept so the Node bridge keeps working)
 *   GET    /mcp/status     - [legacy REST] server status       (kept so the Node bridge keeps working)
 *
 * Threading: a dedicated worker thread runs the accept loop and handles each
 * connection to completion. Tool execution is marshaled onto the game thread.
 */
class FUnrealClaudeMCPServer : public FRunnable
{
public:
	FUnrealClaudeMCPServer();
	virtual ~FUnrealClaudeMCPServer();

	/** Start the MCP server on the specified port. */
	bool Start(uint32 Port = UnrealClaudeConstants::MCPServer::DefaultPort);

	/** Full teardown: stop the worker thread, close sockets, clear sessions. */
	void StopServer();

	/** Check if server is running. */
	bool IsRunning() const { return bIsRunning; }

	/** Get the server port. */
	uint32 GetPort() const { return ServerPort; }

	/** Get the tool registry. */
	TSharedPtr<FMCPToolRegistry> GetToolRegistry() const { return ToolRegistry; }

	// ===== FRunnable =====
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;   // signals the worker loop to exit (called by thread Kill or StopServer)
	virtual void Exit() override;

private:
	// ===== Connection handling =====
	/** Accept-loop entry: parse one request, route it, respond, then close the socket. */
	void HandleConnection(FSocket* ClientSocket);

	/** Read and parse a single HTTP request off the socket. Returns false on malformed/empty. */
	bool ParseHttpRequest(FSocket* Socket, FString& OutMethod, FString& OutPath,
		TMap<FString, FString>& OutHeaders, FString& OutBody);

	/** Write a complete HTTP/1.1 response and flush it. Content-Length is the UTF-8 byte count. */
	void SendHttpResponse(FSocket* Socket, int32 StatusCode, const FString& StatusText,
		const FString& ContentType, const FString& Body,
		const TMap<FString, FString>& ExtraHeaders = TMap<FString, FString>());

	// ===== MCP JSON-RPC =====
	/** Dispatch a single JSON-RPC request body. Sets bOutIsNotification for no-response methods. */
	FString HandleMCPRequest(const FString& JsonBody, FString& InOutSessionId, bool& bOutIsNotification);

	FString HandleInitialize(const TSharedPtr<FJsonObject>& Params, const FString& RequestId);
	FString HandleToolsList(const TSharedPtr<FJsonObject>& Params, const FString& RequestId);
	FString HandleToolsCall(const TSharedPtr<FJsonObject>& Params, const FString& RequestId);
	FString HandlePing(const FString& RequestId);

	FString BuildJsonRpcResponse(const FString& RequestId, TSharedPtr<FJsonObject> Result);
	FString BuildJsonRpcError(const FString& RequestId, int32 Code, const FString& Message);

	// ===== Legacy REST compatibility (Node bridge) =====
	void HandleLegacyRest(FSocket* Socket, const FString& Verb, const FString& Path, const FString& Body);

	// ===== Server-Sent Events =====
	bool AcceptsSSE(const TMap<FString, FString>& Headers) const;
	void SendSSEResponse(FSocket* Socket, const FString& SessionId);
	void SendSSEEvent(FSocket* Socket, const FString& Data, int32 EventId);

	// ===== Security (local-only; no cloud validation) =====
	bool ValidateOrigin(const TMap<FString, FString>& Headers) const;
	bool ValidateApiKey(const TMap<FString, FString>& Headers) const;
	FString GenerateSessionId() const;

private:
	/** Tool registry (owns the built-in tools + async task queue). */
	TSharedPtr<FMCPToolRegistry> ToolRegistry;

	/** Listening socket (localhost). */
	FSocket* ListenerSocket = nullptr;

	/** Worker thread running the accept loop. */
	FRunnableThread* ServerThread = nullptr;

	/** Set true to break the accept loop. */
	FThreadSafeBool bShouldStop = false;

	/** Server state. */
	bool bIsRunning = false;
	uint32 ServerPort = 0;

	/** Active MCP session ids (created on initialize, removed on DELETE). */
	TSet<FString> ActiveSessions;
	mutable FCriticalSection SessionLock;

	/** Monotonic SSE event id. */
	TAtomic<int32> NextEventId{ 0 };

	/** Optional local bearer key (from settings). Empty = open to localhost. */
	FString LocalApiKey;
};
