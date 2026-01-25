// Copyright Epic Games, Inc. All Rights Reserved.

using System.Configuration;
using UnrealBuildTool;

public class TCAT : ModuleRules
{
	public TCAT(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		IWYUSupport = IWYUSupport.Full;
		bLegacyPublicIncludePaths = false;

		bUseUnity = true;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Projects",
				"DeveloperSettings",
				"NavigationSystem",
				"AIModule",
            }
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore",
				"RHI",
				"Renderer",
				"Slate",
				"SlateCore",
			}
			);

        // for debug and debug game and debug game editor builds, we want to enable debug-specific code paths
        if (Target.Configuration == UnrealTargetConfiguration.Debug ||
            Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            PrivateDefinitions.Add("UE_IS_DEBUG_OR_DEBUGGAME=1");
        }
    }
}
