// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "TCATEditor.h"
#include "TCATEditorSettings.h"
#include "TCATLayerConfigCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Selection.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "TCATEditorModule"

//////////////////////////////////////////////////////////////////////////
// FTCATInputProcessor

FTCATInputProcessor::FTCATInputProcessor(FTCATEditorModule* InOwner)
	: Owner(InOwner)
{
}

namespace
{
	bool MatchesShortcut(const FKeyEvent& InKeyEvent, const FInputChord& Shortcut)
	{
		if (!Shortcut.Key.IsValid())
		{
			return false;
		}

		return InKeyEvent.GetKey() == Shortcut.Key
			&& InKeyEvent.IsControlDown() == Shortcut.bCtrl
			&& InKeyEvent.IsShiftDown() == Shortcut.bShift
			&& InKeyEvent.IsAltDown() == Shortcut.bAlt
			&& InKeyEvent.IsCommandDown() == Shortcut.bCmd;
	}
}

bool FTCATInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	const UTCATEditorSettings* Settings = GetDefault<UTCATEditorSettings>();
	if (!Settings || !Settings->bEnableDebugDrawShortcut || !Owner)
	{
		return false;
	}

	// Check Debug Draw Mode toggle (Alt+Shift+I by default)
	if (MatchesShortcut(InKeyEvent, Settings->DebugDrawShortcut))
	{
		Owner->OnToggleDebugDrawMode();
		return true;
	}

	// Check Previous Layer shortcut (Alt+Shift+U by default)
	if (MatchesShortcut(InKeyEvent, Settings->PreviousLayerShortcut))
	{
		Owner->OnCycleToPreviousLayer();
		return true;
	}

	// Check Next Layer shortcut (Alt+Shift+O by default)
	if (MatchesShortcut(InKeyEvent, Settings->NextLayerShortcut))
	{
		Owner->OnCycleToNextLayer();
		return true;
	}

	return false; // Let other handlers process this event
}

//////////////////////////////////////////////////////////////////////////
// FTCATEditorModule

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

	// Register global input processor for shortcut handling (works during PIE too)
	InputProcessor = MakeShareable(new FTCATInputProcessor(this));
	FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
}

void FTCATEditorModule::ShutdownModule()
{
	// Unregister input processor
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		InputProcessor.Reset();
	}

	FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyModule)
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout("TCATBaseLayerConfig");
		PropertyModule->UnregisterCustomPropertyTypeLayout("TCATInfluenceConfigEntry");
		PropertyModule->UnregisterCustomPropertyTypeLayout("TCATCompositeLayerConfig");
	}
}

void FTCATEditorModule::OnToggleDebugDrawMode()
{
	if (!GEditor)
	{
		return;
	}

	// Collect all relevant worlds (editor world + any PIE worlds)
	TArray<UWorld*> Worlds;

	// Add editor world
	if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
	{
		Worlds.Add(EditorWorld);
	}

	// Add PIE worlds
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			Worlds.AddUnique(Context.World());
		}
	}

	if (Worlds.Num() == 0)
	{
		return;
	}

	// Collect target volumes: selected ones first, otherwise all in all worlds
	TArray<ATCATInfluenceVolume*> TargetVolumes;

	// Check for selected influence volumes (editor selection)
	USelection* Selection = GEditor->GetSelectedActors();
	if (Selection)
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (ATCATInfluenceVolume* Volume = Cast<ATCATInfluenceVolume>(*It))
			{
				TargetVolumes.Add(Volume);

				// During PIE, also find the corresponding PIE volume
				for (UWorld* World : Worlds)
				{
					if (World->WorldType == EWorldType::PIE)
					{
						for (TActorIterator<ATCATInfluenceVolume> PIEIt(World); PIEIt; ++PIEIt)
						{
							// Match by name (PIE actors have modified names but share the base)
							if ((*PIEIt)->GetFName().ToString().Contains(Volume->GetFName().ToString()))
							{
								TargetVolumes.AddUnique(*PIEIt);
							}
						}
					}
				}
			}
		}
	}

	// If no volumes are selected, target all volumes in all worlds
	if (TargetVolumes.Num() == 0)
	{
		for (UWorld* World : Worlds)
		{
			for (TActorIterator<ATCATInfluenceVolume> It(World); It; ++It)
			{
				TargetVolumes.AddUnique(*It);
			}
		}
	}

	if (TargetVolumes.Num() == 0)
	{
		return;
	}

	// Cycle through: None -> VisibleOnly -> All -> None
	auto CycleDrawMode = [](ETCATDebugDrawMode CurrentMode) -> ETCATDebugDrawMode
	{
		switch (CurrentMode)
		{
		case ETCATDebugDrawMode::None:
			return ETCATDebugDrawMode::VisibleOnly;
		case ETCATDebugDrawMode::VisibleOnly:
			return ETCATDebugDrawMode::All;
		case ETCATDebugDrawMode::All:
		default:
			return ETCATDebugDrawMode::None;
		}
	};

	// Use the first volume's mode as reference for consistent toggling
	const ETCATDebugDrawMode NewMode = CycleDrawMode(TargetVolumes[0]->DrawInfluence);

	for (ATCATInfluenceVolume* Volume : TargetVolumes)
	{
		Volume->DrawInfluence = NewMode;

		// Only mark dirty for editor world volumes (not PIE copies)
		if (Volume->GetWorld() && Volume->GetWorld()->WorldType == EWorldType::Editor)
		{
			Volume->MarkPackageDirty();
		}
	}

	// On-screen feedback
	const TCHAR* ModeString = nullptr;
	switch (NewMode)
	{
	case ETCATDebugDrawMode::None:
		ModeString = TEXT("None");
		break;
	case ETCATDebugDrawMode::VisibleOnly:
		ModeString = TEXT("Visible Only");
		break;
	case ETCATDebugDrawMode::All:
		ModeString = TEXT("All");
		break;
	}
	GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
		FString::Printf(TEXT("TCAT: Debug Draw Mode: %s"), ModeString));
}

void FTCATEditorModule::OnCycleToPreviousLayer()
{
	if (!GEditor)
	{
		return;
	}

	// Collect all relevant worlds (editor world + any PIE worlds)
	TArray<UWorld*> Worlds;

	if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
	{
		Worlds.Add(EditorWorld);
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			Worlds.AddUnique(Context.World());
		}
	}

	if (Worlds.Num() == 0)
	{
		return;
	}

	// Collect target volumes
	TArray<ATCATInfluenceVolume*> TargetVolumes;

	USelection* Selection = GEditor->GetSelectedActors();
	if (Selection)
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (ATCATInfluenceVolume* Volume = Cast<ATCATInfluenceVolume>(*It))
			{
				TargetVolumes.Add(Volume);

				for (UWorld* World : Worlds)
				{
					if (World->WorldType == EWorldType::PIE)
					{
						for (TActorIterator<ATCATInfluenceVolume> PIEIt(World); PIEIt; ++PIEIt)
						{
							if ((*PIEIt)->GetFName().ToString().Contains(Volume->GetFName().ToString()))
							{
								TargetVolumes.AddUnique(*PIEIt);
							}
						}
					}
				}
			}
		}
	}

	if (TargetVolumes.Num() == 0)
	{
		for (UWorld* World : Worlds)
		{
			for (TActorIterator<ATCATInfluenceVolume> It(World); It; ++It)
			{
				TargetVolumes.AddUnique(*It);
			}
		}
	}

	if (TargetVolumes.Num() == 0)
	{
		return;
	}

	// Use first volume as reference
	ATCATInfluenceVolume* ReferenceVolume = TargetVolumes[0];
	const int32 TotalLayers = ReferenceVolume->LayerDebugSettings.Num();

	if (TotalLayers == 0)
	{
		return;
	}

	// Find current visible layer index
	int32 CurrentIndex = -1;
	for (int32 Index = 0; Index < ReferenceVolume->LayerDebugSettings.Num(); Index++)
	{
		if (ReferenceVolume->LayerDebugSettings[Index].bVisible)
		{
			CurrentIndex = Index;
			break;
		}
	}

	// Calculate new index (previous with wrap-around)
	int32 NewIndex = (CurrentIndex == -1) ? TotalLayers - 1 : (CurrentIndex - 1 + TotalLayers) % TotalLayers;

	// Apply to all target volumes
	for (ATCATInfluenceVolume* Volume : TargetVolumes)
	{
		Volume->DrawInfluence = ETCATDebugDrawMode::VisibleOnly;

		// Set only the target layer visible
		for (int32 Index = 0; Index < Volume->LayerDebugSettings.Num(); Index++)
		{
			Volume->LayerDebugSettings[Index].bVisible = (Index == NewIndex);
		}

		Volume->RebuildRuntimeMaps();

		if (Volume->GetWorld() && Volume->GetWorld()->WorldType == EWorldType::Editor)
		{
			Volume->MarkPackageDirty();
		}
	}

	// On-screen feedback
	FName LayerName = NAME_None;
	if (NewIndex < TotalLayers)
	{
		LayerName = ReferenceVolume->LayerDebugSettings[NewIndex].MapTag;
	}
	GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
		FString::Printf(TEXT("TCAT: Viewing '%s' (%d/%d)"), *LayerName.ToString(), NewIndex + 1, TotalLayers));
}

void FTCATEditorModule::OnCycleToNextLayer()
{
	if (!GEditor)
	{
		return;
	}

	// Collect all relevant worlds (editor world + any PIE worlds)
	TArray<UWorld*> Worlds;

	if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
	{
		Worlds.Add(EditorWorld);
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			Worlds.AddUnique(Context.World());
		}
	}

	if (Worlds.Num() == 0)
	{
		return;
	}

	// Collect target volumes
	TArray<ATCATInfluenceVolume*> TargetVolumes;

	USelection* Selection = GEditor->GetSelectedActors();
	if (Selection)
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (ATCATInfluenceVolume* Volume = Cast<ATCATInfluenceVolume>(*It))
			{
				TargetVolumes.Add(Volume);

				for (UWorld* World : Worlds)
				{
					if (World->WorldType == EWorldType::PIE)
					{
						for (TActorIterator<ATCATInfluenceVolume> PIEIt(World); PIEIt; ++PIEIt)
						{
							if ((*PIEIt)->GetFName().ToString().Contains(Volume->GetFName().ToString()))
							{
								TargetVolumes.AddUnique(*PIEIt);
							}
						}
					}
				}
			}
		}
	}

	if (TargetVolumes.Num() == 0)
	{
		for (UWorld* World : Worlds)
		{
			for (TActorIterator<ATCATInfluenceVolume> It(World); It; ++It)
			{
				TargetVolumes.AddUnique(*It);
			}
		}
	}

	if (TargetVolumes.Num() == 0)
	{
		return;
	}

	// Use first volume as reference
	ATCATInfluenceVolume* ReferenceVolume = TargetVolumes[0];
	const int32 TotalLayers = ReferenceVolume->LayerDebugSettings.Num();

	if (TotalLayers == 0)
	{
		return;
	}

	// Find current visible layer index
	int32 CurrentIndex = -1;
	for (int32 Index = 0; Index < ReferenceVolume->LayerDebugSettings.Num(); Index++)
	{
		if (ReferenceVolume->LayerDebugSettings[Index].bVisible)
		{
			CurrentIndex = Index;
			break;
		}
	}

	// Calculate new index (next with wrap-around)
	int32 NewIndex = (CurrentIndex == -1) ? 0 : (CurrentIndex + 1) % TotalLayers;

	// Apply to all target volumes
	for (ATCATInfluenceVolume* Volume : TargetVolumes)
	{
		Volume->DrawInfluence = ETCATDebugDrawMode::VisibleOnly;

		// Set only the target layer visible
		for (int32 Index = 0; Index < Volume->LayerDebugSettings.Num(); Index++)
		{
			Volume->LayerDebugSettings[Index].bVisible = (Index == NewIndex);
		}

		Volume->RebuildRuntimeMaps();

		if (Volume->GetWorld() && Volume->GetWorld()->WorldType == EWorldType::Editor)
		{
			Volume->MarkPackageDirty();
		}
	}

	// On-screen feedback
	FName LayerName = NAME_None;
	if (NewIndex < TotalLayers)
	{
		LayerName = ReferenceVolume->LayerDebugSettings[NewIndex].MapTag;
	}
	GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
		FString::Printf(TEXT("TCAT: Viewing '%s' (%d/%d)"), *LayerName.ToString(), NewIndex + 1, TotalLayers));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTCATEditorModule, TCATEditor)
