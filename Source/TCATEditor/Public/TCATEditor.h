// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/IInputProcessor.h"

/**
 * Input processor that handles TCAT shortcuts globally, including during PIE.
 */
class FTCATInputProcessor : public IInputProcessor
{
public:
	FTCATInputProcessor(class FTCATEditorModule* InOwner);

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;

private:
	FTCATEditorModule* Owner;
};

class FTCATEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Handler for the toggle debug draw mode command */
	void OnToggleDebugDrawMode();

	/** Handler for cycling to the previous layer visibility */
	void OnCycleToPreviousLayer();

	/** Handler for cycling to the next layer visibility */
	void OnCycleToNextLayer();

private:
	/** Input processor for handling shortcuts during PIE */
	TSharedPtr<FTCATInputProcessor> InputProcessor;
};
