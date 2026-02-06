// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "Query/Nav/TCATNavigationQueryFilter.h"
#include "NavMesh/RecastNavMesh.h"
#include "Detour/DetourNavMeshQuery.h" 
#include "Core/TCATSubsystem.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Navigation/RecastGraphAStar.h"
#include "NavMesh/RecastHelpers.h"

/**
 * Custom Recast Query Filter (Influence Weighted Navigation)
 */
class FTCATRecastQueryFilter : public FRecastQueryFilter
{
public:
	FTCATRecastQueryFilter(ETCATNavigationFilterMode InMode, float InMultiplier, float InThreshold, ATCATInfluenceVolume* InVolume, FName InMapTag)
		: FRecastQueryFilter(true), FilterMode(InMode), CostMultiplier(InMultiplier), InfluenceThreshold(InThreshold), CachedVolume(InVolume), TargetMapTag(InMapTag)
	{
		SetIsVirtual(true);
	}

	virtual ~FTCATRecastQueryFilter() override {}

	virtual INavigationQueryFilterInterface* CreateCopy() const override
	{
		return new FTCATRecastQueryFilter(*this);
	}

	virtual FVector::FReal getVirtualCost(const FVector::FReal* pa, const FVector::FReal* pb,
		const dtPolyRef prevRef, const dtMeshTile* prevTile, const dtPoly* prevPoly,
		const dtPolyRef curRef, const dtMeshTile* curTile, const dtPoly* curPoly,
		const dtPolyRef nextRef, const dtMeshTile* nextTile, const dtPoly* nextPoly) const override
	{
		const FVector::FReal BaseCost = dtVdist(pa, pb);

		if (!CachedVolume.IsValid())
		{
			return BaseCost;
		}

		const FVector SamplePos = Recast2UnrealPoint(pb);

		const float CellSize = CachedVolume->GetCellSize();
		const FVector Origin = CachedVolume->GetGridOrigin();
		
		const int32 GridX = FMath::FloorToInt((SamplePos.X - Origin.X) / CellSize);
		const int32 GridY = FMath::FloorToInt((SamplePos.Y - Origin.Y) / CellSize);

		if (GridX >= 0 && GridX < CachedVolume->GetColumns() && GridY >= 0 && GridY < CachedVolume->GetRows())
		{
			const float InfluenceValue = CachedVolume->GetInfluenceFromGrid(TargetMapTag, GridX, GridY);

			if (FilterMode == ETCATNavigationFilterMode::BinaryThreshold)
			{
				if (InfluenceValue >= InfluenceThreshold)
				{
					return BaseCost * CostMultiplier;
				}
			}
			else if (FilterMode == ETCATNavigationFilterMode::LinearBoost)
			{
				if (InfluenceValue > 0.0f)
				{
					return BaseCost * (1.0 + (InfluenceValue * CostMultiplier));
				}
			}
		}

		return BaseCost;
	}

private:
	ETCATNavigationFilterMode FilterMode;
	float CostMultiplier;
	float InfluenceThreshold;
	TWeakObjectPtr<ATCATInfluenceVolume> CachedVolume;
	FName TargetMapTag;
};

UTCATNavigationQueryFilter::UTCATNavigationQueryFilter()
{
	FilterMode = ETCATNavigationFilterMode::BinaryThreshold;
	MapTag = NAME_None;
	CostMultiplier = 100.0f;
	InfluenceThreshold = 0.01f;
}

void UTCATNavigationQueryFilter::InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const
{
	Super::InitializeFilter(NavData, Querier, Filter);

	const UWorld* World = NavData.GetWorld();
	if (World)
	{
		UTCATSubsystem* TCAT = World->GetSubsystem<UTCATSubsystem>();
		if (TCAT)
		{
			ATCATInfluenceVolume* Volume = TCAT->GetInfluenceVolume(MapTag);
			if (Volume)
			{
				FTCATRecastQueryFilter CustomFilter(FilterMode, CostMultiplier, InfluenceThreshold, Volume, MapTag);
				Filter.SetFilterImplementation(&CustomFilter);
			}
		}
	}
}
