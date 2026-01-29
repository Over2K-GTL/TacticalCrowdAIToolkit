// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_TCATInfluence.generated.h"

/**
 * EQS Test: Scores and/or filters candidate items using TCAT Influence Map values.
 *
 * Typical usage in an EQS Query:
 *  1) Generator creates candidate locations (e.g., TCAT Grid generator)
 *  2) This test samples TCAT influence at each location
 *  3) EQS uses the sampled value to score or filter items (Min/Max thresholds)
 *
 * Notes:
 * - If MapTag is None or the target volume does not exist, the test does nothing.
 * - Works on Vector items (locations).
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
	// The influence map tag to query (e.g., 'Enemy', 'Cover').
	UPROPERTY(EditAnywhere, Category = "TCAT", meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions"))
	FName MapTag;
};
