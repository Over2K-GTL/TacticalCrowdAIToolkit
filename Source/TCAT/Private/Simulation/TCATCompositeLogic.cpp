// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Simulation/TCATCompositeLogic.h"
#include "Core/TCATSettings.h"

TArray<FString> UTCATCompositeLogic::GetBaseTagOptions() const
{
	return UTCATSettings::GetBaseTagOptions();
}

TArray<FString> UTCATCompositeLogic::GetCompositeTagOptions() const
{
	return UTCATSettings::GetCompositeTagOptions();
}

TArray<FString> UTCATCompositeLogic::GetAllTagOptions() const
{
	return UTCATSettings::GetAllTagOptions();
}