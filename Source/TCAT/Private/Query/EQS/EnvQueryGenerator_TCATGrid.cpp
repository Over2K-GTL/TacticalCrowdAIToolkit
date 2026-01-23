// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Query/EQS/EnvQueryGenerator_TCATGrid.h"
#include "TCAT.h"
#include "Core/TCATSubsystem.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Engine/World.h"

UEnvQueryGenerator_TCATGrid::UEnvQueryGenerator_TCATGrid()
{
	GenerateAround = UEnvQueryContext_Querier::StaticClass();
	SearchRadius.DefaultValue = 500.0f;
	SpaceBetween.DefaultValue = 0.0f;
}

void UEnvQueryGenerator_TCATGrid::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();
	if (!QueryOwner) return;

	SearchRadius.BindData(QueryOwner, QueryInstance.QueryID);
	SpaceBetween.BindData(QueryOwner, QueryInstance.QueryID);

	float RadiusValue = SearchRadius.GetValue();
	float DensityValue = SpaceBetween.GetValue();

	// Get Context Locations
	TArray<FVector> ContextLocations;
	QueryInstance.PrepareContext(GenerateAround, ContextLocations);
	
	// Fetch TCAT Volume Info
	UTCATSubsystem* TCAT = QueryOwner->GetWorld() ? QueryOwner->GetWorld()->GetSubsystem<UTCATSubsystem>() : nullptr;
	ATCATInfluenceVolume* Volume = TCAT ? TCAT->GetInfluenceVolume(MapTag) : nullptr;
	
	if (Volume == nullptr)
	{
		UE_LOG(LogTCAT, Warning, TEXT("EnvQueryGenerator_TCATGrid: Could not find TCAT Influence Volume with tag '%s'. Grid generation skipped."), *MapTag.ToString());
		return;
	}
	
	// Auto-detect CellSize if SpaceBetween is <= 0
	if (DensityValue <= KINDA_SMALL_NUMBER)
	{
		DensityValue = Volume ? Volume->GetCellSize() : 100.0f; // Fallback 100
	}

	// Grid Generation Logic
	TArray<FNavLocation> CandidatePoints;
	CandidatePoints.Reserve((RadiusValue * 2 / DensityValue) * (RadiusValue * 2 / DensityValue));

	const int32 ItemCountHalf = FMath::CeilToInt(RadiusValue / DensityValue);
	const float RadiusSq = FMath::Square(RadiusValue);
	
	for (const FVector& Center : ContextLocations)
	{
		for (int32 X = -ItemCountHalf; X <= ItemCountHalf; ++X)
		{
			for (int32 Y = -ItemCountHalf; Y <= ItemCountHalf; ++Y)
			{
				const FVector Offset(X * DensityValue, Y * DensityValue, 0.0f);
				
				// Circle Check
				if (Offset.SizeSquared2D() > RadiusSq)
				{
					continue;
				}

				FVector Point = Center + Offset;

				// Check if point is inside Volume Bounds (XY only to be safe with height)
				if (!Volume->GetCachedBounds().IsInsideXY(Point))
				{
					continue;
				}
				
				if (Volume)
				{
					// Use TCAT's baked height for better starting point of projection
					Point.Z = Volume->GetGridHeightWorldPos(Point) + 10.0f; 
				}

				// Collect points first
				CandidatePoints.Add(FNavLocation(Point));
			}
		}
	}

	// Optional: Project to NavMesh
	if (bProjectToNavigation)
	{
		ProjectAndFilterNavPoints(CandidatePoints, QueryInstance);
	}

	StoreNavPoints(CandidatePoints, QueryInstance);
}

FText UEnvQueryGenerator_TCATGrid::GetDescriptionTitle() const
{
	return FText::FromString(FString::Printf(TEXT("TCAT Grid: %s"), *MapTag.ToString()));
}