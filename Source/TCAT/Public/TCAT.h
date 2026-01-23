// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "Modules/ModuleManager.h"
#include "Stats/Stats.h"
#include "Styling/SlateStyle.h"

namespace TCATConstants
{
	static const FString PluginName = TEXT("TCAT");
	static const FString ShaderDirectory = TEXT("Shaders");
	static const FString VirtualShaderPath = TEXT("/Plugin/TCAT");
}

DECLARE_STATS_GROUP(TEXT("TCAT_Plugin"), STATGROUP_TCAT, STATCAT_Advanced);
DECLARE_LOG_CATEGORY_EXTERN(LogTCAT, Log, All);

class FTCATModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<FSlateStyleSet> StyleSet;
};
