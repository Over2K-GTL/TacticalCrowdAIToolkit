// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_TCATInfluence.generated.h"

/**
 * EQS Test to score or filter items based on TCAT Influence Map values.
 */
UCLASS()
class TCAT_API UEnvQueryTest_TCATInfluence : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_TCATInfluence();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;

protected:
	// The influence layer tag to query (e.g., 'Enemy', 'Cover').
	UPROPERTY(EditAnywhere, Category = "TCAT", meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions"))
	FName MapTag;

	// If true, attempts to remove the Querier's own influence from the calculation.
	// Useful when checking "Is this spot safe?" while standing in a "Danger" zone caused by yourself.
	UPROPERTY(EditAnywhere, Category = "TCAT")
	bool bSubtractSelfInfluence = false;
};
