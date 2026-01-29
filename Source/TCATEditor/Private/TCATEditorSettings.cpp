// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "TCATEditorSettings.h"

UTCATEditorSettings::UTCATEditorSettings()
{
	// Default shortcut: Alt+Shift+I
	DebugDrawShortcut = FInputChord(EModifierKey::Alt | EModifierKey::Shift, EKeys::I);

	// Default shortcut: Alt+Shift+U (previous layer)
	PreviousLayerShortcut = FInputChord(EModifierKey::Alt | EModifierKey::Shift, EKeys::U);

	// Default shortcut: Alt+Shift+O (next layer)
	NextLayerShortcut = FInputChord(EModifierKey::Alt | EModifierKey::Shift, EKeys::O);
}
