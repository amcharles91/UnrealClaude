// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: Drive the Unreal Engine editor undo/redo (transaction) system.
 *
 * Wraps GEditor->Trans (UTransBuffer): undo/redo single or multiple steps,
 * inspect the undo/redo history, query counts and the next-undo/next-redo
 * descriptions, and reset the buffer. Most other native MCP tools wrap their
 * mutations in transactions already, so this tool lets an agent revert or
 * replay those operations and see what is on the stack.
 *
 * NOTE: Each MCP call is independent, so VibeUE's stateful BeginTransaction()/
 * EndTransaction() pair (held open across calls) does not translate. Grouping
 * is therefore not exposed as separate begin/end ops; the supported ops are all
 * single-call and stateless. (BeginTransaction without a matching EndTransaction
 * in the same call would leave the editor with a dangling open transaction.)
 *
 * Ported from VibeUE's UEditorTransactionService into the native MCP tool pattern.
 */
class FMCPTool_EditorTransaction : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override;
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	FMCPToolResult OpUndo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpRedo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpCanUndo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpCanRedo(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetHistory(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpGetCounts(const TSharedRef<FJsonObject>& Params);
	FMCPToolResult OpReset(const TSharedRef<FJsonObject>& Params);
};
