// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_ProjectedPoints.h"
#include "EnvQueryGenerator_TCATGrid.generated.h"

/**
 * Generates grid points aligned with TCAT Influence Volume settings.
 */
UCLASS()
class TCAT_API UEnvQueryGenerator_TCATGrid : public UEnvQueryGenerator_ProjectedPoints
{
	GENERATED_BODY()

public:
	UEnvQueryGenerator_TCATGrid();

	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionTitle() const override;

protected:
	// Target Influence Map to align with.
	UPROPERTY(EditAnywhere, Category = "TCAT", meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions"))
	FName MapTag;

	// Center of the generation.
	UPROPERTY(EditAnywhere, Category = "TCAT")
	TSubclassOf<UEnvQueryContext> GenerateAround;

	// Max radius to generate items.
	UPROPERTY(EditAnywhere, Category = "TCAT")
	FAIDataProviderFloatValue SearchRadius;

	// Distance between points. 
	// If set to 0, it automatically uses the CellSize of the TCAT Influence Volume.
	UPROPERTY(EditAnywhere, Category = "TCAT")
	FAIDataProviderFloatValue SpaceBetween;

	// If true, projects generated points to NavMesh.
	// Defaults to false to avoid filtering out valid IMap cells that might be slightly off-mesh.
	UPROPERTY(EditAnywhere, Category = "TCAT")
	bool bProjectToNavigation = false;
};
