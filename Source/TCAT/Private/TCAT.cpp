// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "TCAT.h"
#include "ShaderCore.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "FTCATModule"

DEFINE_LOG_CATEGORY(LogTCAT);

void FTCATModule::StartupModule()
{
	// Shader Source Directory Mapping
	TSharedPtr<IPlugin> TCATPlugin = IPluginManager::Get().FindPlugin(TCATConstants::PluginName);
	if (TCATPlugin.IsValid())
	{
		FString PluginShaderDir = FPaths::Combine(TCATPlugin->GetBaseDir(), TCATConstants::ShaderDirectory);
		AddShaderSourceDirectoryMapping(TCATConstants::VirtualShaderPath, PluginShaderDir);

		// Register component icons
		FString ResourcesDir = FPaths::Combine(TCATPlugin->GetBaseDir(), TEXT("Resources"));

		StyleSet = MakeShareable(new FSlateStyleSet(TEXT("TCATStyle")));
		StyleSet->SetContentRoot(ResourcesDir);

		// 16x16 class icon (shown in component list)
		StyleSet->Set(
			"ClassIcon.TCATInfluenceComponent",
			new FSlateImageBrush(
				StyleSet->RootToContentDir(TEXT("InfluenceComponentIcon.png")),
				FVector2D(16.0f, 16.0f)
			)
		);

		// 64x64 thumbnail (shown in larger views)
		StyleSet->Set(
			"ClassThumbnail.TCATInfluenceComponent",
			new FSlateImageBrush(
				StyleSet->RootToContentDir(TEXT("InfluenceComponentIcon_64.png")),
				FVector2D(64.0f, 64.0f)
			)
		);

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
	}
}

void FTCATModule::ShutdownModule()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTCATModule, TCAT)