// Copyright 2025-2026 Over2K. All Rights Reserved.

using UnrealBuildTool;

public class TCATEditor : ModuleRules
{
	public TCATEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		IWYUSupport = IWYUSupport.Full;
		bLegacyPublicIncludePaths = false;
		
		bUseUnity = true;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"PropertyEditor",
				"TCAT",
				"InputCore",
				"ToolMenus",
				"AssetRegistry",
				"DeveloperSettings"
			}
		);
	}
}
