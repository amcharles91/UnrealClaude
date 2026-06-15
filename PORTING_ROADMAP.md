# UnrealClaude — Native MCP + VibeUE Feature Porting Roadmap

This fork gives the UnrealClaude UE5.7 plugin a **native C++ MCP endpoint** (no Node bridge)
and ports high-value, **local-only** capabilities from [VibeUE](https://github.com/kevinpbuckley/VibeUE)
into the same native MCP tool registry.

## Status

- ✅ **Native MCP endpoint** — raw-socket `FUnrealClaudeMCPServer` (FRunnable) serving Streamable HTTP
  JSON-RPC at `POST http://127.0.0.1:8732/mcp` (+ legacy REST for the bridge during transition).
  Spec-conformant, runtime-verified. Connect: `claude mcp add --transport http unrealclaude http://localhost:8732/mcp`.
- ✅ **gameplay_tags** — first ported tool.

## Conventions (every ported tool follows these)

- One tool = `MCPTool_<Name>.{h,cpp}` in `Source/UnrealClaude/Private/MCP/Tools/`.
- Implement `IMCPTool` via `FMCPToolBase`. **Canonical template: `MCPTool_GameplayTags.{h,cpp}`.**
- Multi-operation tools dispatch on a required `operation` string param → private `OpXxx()` methods.
- Use `FMCPToolBase` helpers (`ExtractRequiredString`, `ExtractOptionalString/Number/Bool`,
  `StringArrayToJsonArray`, `ValidateEditorContext`, validators).
- Return `FMCPToolResult::Success(msg, data)` / `::Error(msg)`; structured payload in a `TSharedPtr<FJsonObject>`.
- Guard editor-mutating ops with `#if WITH_EDITOR` (graceful error in `#else`).
- Copyright header + tabs to match the existing codebase.
- Register in `MCPToolRegistry::RegisterBuiltinTools()`, add to `ExpectedTools` (UnrealClaudeConstants.h),
  add module deps to `Build.cs`, add any engine-plugin deps to `UnrealClaude.uplugin`.

## Per-feature loop (per wave)

1. **Author** all tools in the wave (parallel subagents draft the `MCPTool_X` files; no shared-file edits).
2. **Wire** shared files once: registry, Build.cs, .uplugin, ExpectedTools.
3. **Rebuild** — editor must be **fully closed** (Live Coding can't apply dep changes; .uplugin/Build.cs need a full rebuild).
4. **Reopen** → batch-test every new tool live over `:8732`.
5. **Quality pass** (`/simplify` / review) for standards, consistency, dead code.
6. **Commit per tool**, push to fork.

## Backlog

### Skip — already covered by UnrealClaude
ActorService, AssetDiscoveryService/`manage_asset`, InputService, `read_logs`,
`execute_python_code`/console, Python-introspection helpers.

### Skip — online / not applicable
`terrain_data` (vibeue.com), `deep_research` (web), `vibeue-skills-manager`, `manage_editor_chat`,
in-editor AI chat, license gate, Python proxy.

### Combine — extend an existing tool
| Existing tool | Fold in |
|---|---|
| `blueprint_modify` | interfaces, event dispatchers/delegates, timelines, comment boxes, auto-layout |
| `material` | material graph node editing (MaterialNodeService) |
| `anim_blueprint_modify` | any missing AnimGraphService ops |
| `character_data` / new | generalize to `data_table` + `data_asset` |
| `capture_viewport` | viewport camera/view-mode control + per-asset-editor screenshots |

### New tools (status)
| Tool | VibeUE service | Wave | Status |
|------|----------------|------|--------|
| gameplay_tags | GameplayTagService | — | ✅ done |
| enum_struct | EnumStructService | W1 | ✅ built (13 ops) |
| editor_transaction | EditorTransactionService | W1 | ✅ built (7 ops) |
| data_table | DataTableService | W1 | ✅ built (7 ops) |
| data_asset | DataAssetService | W1 | ✅ built (4 ops) |
| (blueprint_modify extras) | BlueprintService | W2 | |
| (material graph) | MaterialNodeService | W2 | |
| state_tree | StateTreeService | W3 | |
| umg_widgets | WidgetService | W3 | |
| niagara | Niagara{,Emitter,ScratchPad}Service | W4 | |
| anim_sequence | AnimSequenceService | W5 | |
| anim_montage | AnimMontageService | W5 | |
| skeleton | SkeletonService | W5 | |
| sound_cue | SoundCueService | W6 | |
| metasound | MetaSoundService | W6 | |
| landscape (+material, RVT) | Landscape*/RuntimeVirtualTextureService | W7 | |
| foliage | FoliageService | W7 | |
| uv_mapping | UVMappingService | W7 | |
| engine_settings | EngineSettingsService | W8 | |
| project_settings | ProjectSettingsService | W8 | |
| viewport / screenshot | ViewportService / ScreenshotService | W8 | |
| map_blockout | MapBlockoutService | W9 | |
