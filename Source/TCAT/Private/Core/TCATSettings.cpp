// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Core/TCATSettings.h"

UTCATSettings::UTCATSettings() : MaxMapResolution(2048)
{
}

TArray<FString> UTCATSettings::GetAllTagOptions()
{
	TArray<FString> Options;
	if (const UTCATSettings* Settings = GetDefault<UTCATSettings>())
	{
		Options.Reserve(Settings->BaseInfluenceTags.Num() + Settings->CompositeInfluenceTags.Num());
		
		for (const FName& Tag : Settings->BaseInfluenceTags)
		{
			Options.Add(Tag.ToString());
		}
		
		for (const FName& Tag : Settings->CompositeInfluenceTags)
		{
			Options.Add(Tag.ToString());
		}
	}
	return Options;
}

TArray<FString> UTCATSettings::GetBaseTagOptions()
{
	TArray<FString> Options;
	if (const UTCATSettings* Settings = GetDefault<UTCATSettings>())
	{
		Options.Reserve(Settings->BaseInfluenceTags.Num());
		
		for (const FName& Tag : Settings->BaseInfluenceTags)
		{
			Options.Add(Tag.ToString());
		}
	}
	return Options;
}

TArray<FString> UTCATSettings::GetCompositeTagOptions()
{
	TArray<FString> Options;
	if (const UTCATSettings* Settings = GetDefault<UTCATSettings>())
	{
		Options.Reserve(Settings->CompositeInfluenceTags.Num());
		
		for (const FName& Tag : Settings->CompositeInfluenceTags)
		{
			Options.Add(Tag.ToString());
		}
	}
	return Options;
}