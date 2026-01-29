// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "TCATEditorSettings.generated.h"

/**
 * Editor-specific settings for the TCAT plugin.
 * These settings appear in Editor Preferences under Plugins > TCAT.
 */
UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="TCAT"))
class TCATEDITOR_API UTCATEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTCATEditorSettings();

	// Settings location in Editor Preferences
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("TCAT"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override { return INVTEXT("TCAT"); }
	virtual FText GetSectionDescription() const override { return INVTEXT("Editor settings for the TCAT plugin."); }
#endif
	
	/**
	 * Enable the keyboard shortcut to toggle debug draw mode for TCAT Influence Volumes.
	 * When enabled, pressing the shortcut will cycle through None -> Visible Only -> All.
	 * Works both in the editor viewport and during PIE.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Shortcuts")
	bool bEnableDebugDrawShortcut = true;

	/**
	 * Keyboard shortcut to toggle debug draw mode for TCAT Influence Volumes.
	 * Default: Alt+Shift+I
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Shortcuts", meta = (EditCondition = "bEnableDebugDrawShortcut"))
	FInputChord DebugDrawShortcut;

	/**
	 * Keyboard shortcut to cycle to the previous layer/map visibility.
	 * Sets DrawInfluence to VisibleOnly and shows only the previous layer.
	 * Default: Alt+Shift+U
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Shortcuts", meta = (EditCondition = "bEnableDebugDrawShortcut"))
	FInputChord PreviousLayerShortcut;

	/**
	 * Keyboard shortcut to cycle to the next layer/map visibility.
	 * Sets DrawInfluence to VisibleOnly and shows only the next layer.
	 * Default: Alt+Shift+O
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Shortcuts", meta = (EditCondition = "bEnableDebugDrawShortcut"))
	FInputChord NextLayerShortcut;
};
