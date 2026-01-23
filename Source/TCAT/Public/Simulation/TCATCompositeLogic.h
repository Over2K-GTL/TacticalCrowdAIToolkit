// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Core/TCATTypes.h"
#include "TCATCompositeLogic.generated.h"

UCLASS(BlueprintType)
class UTCATCompositeLogic : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Sequence of operations to perform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition")
	TArray<FTCATCompositeOperation> Operations;

	UFUNCTION()
	TArray<FString> GetBaseTagOptions() const;

	UFUNCTION()
	TArray<FString> GetCompositeTagOptions() const;

	UFUNCTION()
	TArray<FString> GetAllTagOptions() const;
};
