// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "TCATEditor.h"
#include "TCATLayerConfigCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "TCATEditorModule"

void FTCATEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	// Struct Customization (Direct Add/Delete Manager Popup)
	PropertyModule.RegisterCustomPropertyTypeLayout(
		"TCATBaseLayerConfig",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FTCATLayerConfigCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomPropertyTypeLayout(
		"TCATInfluenceConfigEntry",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FTCATLayerConfigCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomPropertyTypeLayout(
		"TCATCompositeLayerConfig",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FTCATLayerConfigCustomization::MakeInstance)
	);
}

void FTCATEditorModule::ShutdownModule()
{
	FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyModule)
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout("TCATBaseLayerConfig");
		PropertyModule->UnregisterCustomPropertyTypeLayout("TCATInfluenceConfigEntry");
		PropertyModule->UnregisterCustomPropertyTypeLayout("TCATCompositeLayerConfig");
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTCATEditorModule, TCATEditor)
