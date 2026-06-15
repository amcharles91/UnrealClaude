// Copyright Natali Caggiano. All Rights Reserved.

using UnrealBuildTool;

public class UnrealClaude : ModuleRules
{
	public UnrealClaude(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
		);
				
		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"UnrealEd",
				"ToolMenus",
				"Projects",
				"EditorFramework",
				"WorkspaceMenuStructure"
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Project Settings -> Plugins -> Unreal Claude (UUnrealClaudeSettings)
				"DeveloperSettings",
				"Json",
				"JsonUtilities",
				"HTTP",
				"HTTPServer",
				"Sockets",
				"Networking",
				"ImageWrapper",
				// Blueprint manipulation
				"Kismet",
				"KismetCompiler",
				"BlueprintGraph",
				"GraphEditor",
				"AssetRegistry",
				"AssetTools",
				// Animation Blueprint manipulation
				"AnimGraph",
				"AnimGraphRuntime",
				// Asset saving
				"EditorScriptingUtilities",
				// Enhanced Input
				"EnhancedInput",
				// Gameplay Tags (query via manager + editor add/remove/rename)
				"GameplayTags",
				"GameplayTagsEditor",
				// Ported-tool dependencies (pre-declared so flesh-out needs no Build.cs changes).
				// UMG / MVVM
				"UMG", "UMGEditor", "MovieScene", "MovieSceneTracks", "PropertyEditor",
				"ModelViewViewModel", "ModelViewViewModelBlueprint", "BlueprintEditorLibrary",
				// Niagara
				"Niagara", "NiagaraEditor", "NiagaraCore",
				// StateTree (+ PropertyBindingUtils: StateTree bindable-struct descriptors derive from it)
				"StateTreeModule", "StateTreeEditorModule", "GameplayStateTreeModule", "PropertyBindingUtils",
				// Animation
				"AnimationCore", "AnimationDataController", "AnimationBlueprintLibrary",
				// Audio
				"AudioEditor", "MetasoundEngine", "MetasoundFrontend", "MetasoundEditor",
				// Landscape / materials
				"Landscape", "LandscapeEditor", "LandscapeEditorUtilities", "MaterialEditor",
				// Foliage
				"Foliage", "FoliageEdit",
				// Mesh / UV
				"MeshDescription", "StaticMeshDescription", "MeshConversionEngineTypes",
				// Settings / viewport / rendering
				"RenderCore", "RHI", "EngineSettings", "LevelEditor"
			}
		);

		// Clipboard support (FPlatformApplicationMisc) on all platforms
		PrivateDependencyModuleNames.Add("ApplicationCore");

		// Windows only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// LiveCoding is only available in editor builds on Windows
			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("LiveCoding");
			}
		}
	}
}
