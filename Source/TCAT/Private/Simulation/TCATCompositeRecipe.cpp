// Copyright 2025-2026 Over2K. All Rights Reserved.
#include "Simulation/TCATCompositeRecipe.h"
#include "Core/TCATSettings.h"

#define LOCTEXT_NAMESPACE "TCATCompositeRecipe"

TArray<FString> UTCATCompositeRecipe::GetBaseTagOptions() const
{
	return UTCATSettings::GetBaseTagOptions();
}

TArray<FString> UTCATCompositeRecipe::GetCompositeTagOptions() const
{
	return UTCATSettings::GetCompositeTagOptions();
}

TArray<FString> UTCATCompositeRecipe::GetAllTagOptions() const
{
	return UTCATSettings::GetAllTagOptions();
}

void UTCATCompositeRecipe::GatherSelfInfluenceWarnings(TArray<FTCATSelfInfluenceWarningMessage>& OutWarnings) const
{
	OutWarnings.Reset();

	const UEnum* CompositeEnum = StaticEnum<ETCATCompositeOp>();
	if (!CompositeEnum)
	{
		return;
	}

	TSet<FName> ActiveSources;
	TSet<FName> DisabledSources;

	const auto MarkDisabled = [&](const FName& LayerTag, ETCATCompositeOp Operation)
	{
		if (LayerTag.IsNone() || DisabledSources.Contains(LayerTag))
		{
			return;
		}

		const FText OperationText = CompositeEnum->GetDisplayNameTextByValue(static_cast<int64>(Operation));
		OutWarnings.Emplace(LayerTag, FText::Format(
			LOCTEXT("SelfInfluenceDisabledFormat", "{0} Self Influence Disabled : {1}"),
			FText::FromName(LayerTag),
			OperationText));

		DisabledSources.Add(LayerTag);
	};

	for (const FTCATCompositeOperation& Operation : Operations)
	{
		switch (Operation.Operation)
		{
		case ETCATCompositeOp::Add:
		case ETCATCompositeOp::Subtract:
			if (!Operation.InputLayerTag.IsNone() && !DisabledSources.Contains(Operation.InputLayerTag))
			{
				ActiveSources.Add(Operation.InputLayerTag);
			}
			break;

		case ETCATCompositeOp::Multiply:
		case ETCATCompositeOp::Divide:
			if (!Operation.InputLayerTag.IsNone())
			{
				MarkDisabled(Operation.InputLayerTag, Operation.Operation);
			}

			for (const FName& ActiveTag : ActiveSources)
			{
				MarkDisabled(ActiveTag, Operation.Operation);
			}

			ActiveSources.Reset();
			break;

		case ETCATCompositeOp::Normalize:
			for (const FName& ActiveTag : ActiveSources)
			{
				MarkDisabled(ActiveTag, Operation.Operation);
			}

			ActiveSources.Reset();
			break;

		default:
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
