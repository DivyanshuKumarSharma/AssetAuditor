using UnrealBuildTool;

public class AssetAuditor : ModuleRules
{
	public AssetAuditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public: types that appear in our Public headers, so modules that add rules can compile against them.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"DeveloperSettings",   // UAssetAuditorSettings is in a Public header
			"Engine",
		});

		// Private: implementation-only dependencies; consumers of this module don't inherit them.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"AssetTools",          // IAssetTools::RenameAssets
			"ContentBrowser",      // SyncBrowserToAssets
			"DataValidation",      // UEditorValidatorBase (plugin dependency declared in the .uplugin)
			"DesktopPlatform",     // native Save File dialog for CSV export
			"InputCore",
			"PhysicsCore",         // ECollisionTraceFlag / LexToString
			"Settings",            // ISettingsModule: per-rule Project Settings pages
			"Slate",
			"SlateCore",
			"StaticMeshEditor",    // UStaticMeshEditorSubsystem: LOD generation, collision fitting
			"UnrealEd",
			"WorkspaceMenuStructure",
		});
	}
}
