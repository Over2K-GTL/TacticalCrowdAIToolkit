// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "TCATNavigationQueryFilter.generated.h"

/**
 * Modes for calculating pathfinding cost based on influence values.
 */
UENUM(BlueprintType)
enum class ETCATNavigationFilterMode : uint8
{
	/** If influence >= Threshold, apply a fixed CostMultiplier. (Like a wall) */
	BinaryThreshold UMETA(DisplayName = "Binary Threshold"),

	/** Apply penalty linearly: BaseCost * (1.0 + Influence * CostMultiplier). (Soft avoidance) */
	LinearBoost     UMETA(DisplayName = "Linear Boost")
};

/**
 * TCAT Navigation Query Filter
 * 
 * Influence Weighted Navigation:
 * A custom navigation filter that increases pathfinding cost based on TCAT Influence Maps.
 */
UCLASS(Blueprintable)
class TCAT_API UTCATNavigationQueryFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UTCATNavigationQueryFilter();

	virtual void InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const override;

protected:
	/** The cost calculation method to use. */
	UPROPERTY(EditDefaultsOnly, Category = "TCAT")
	ETCATNavigationFilterMode FilterMode = ETCATNavigationFilterMode::BinaryThreshold;

	/** The tag of the Influence Map layer to check (e.g., "Enemy", "Fire"). */
	UPROPERTY(EditDefaultsOnly, Category = "TCAT")
	FName MapTag;

	/** 
	 * In Binary Mode: Multiplied to cost when influence > Threshold.
	 * In Linear Mode: The weight of the influence (FinalCost = Base * (1 + Infl * Mult)).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "TCAT")
	float CostMultiplier = 100.0f;

	/**
	 * [Binary Mode Only] Minimum influence value to trigger the fixed cost penalty.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "TCAT", meta = (Hidden = "FilterMode == ETCATNavigationFilterMode::BinaryThreshold"))
	float InfluenceThreshold = 0.01f;
};