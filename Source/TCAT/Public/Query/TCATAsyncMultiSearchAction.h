// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "TCATQueryTypes.h"
#include "Core/TCATSubsystem.h"
#include "TCATAsyncMultiSearchAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTCATMultiResultPin, const TArray<FTCATSingleResult>&, Results);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTCATMultiSearchFailedPin);

/**
 * Multiple Result Version of Search Actions.
 * Returns Top-N results instead of a single best value.
 */
UCLASS()
class TCAT_API UTCATAsyncMultiSearchAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // Success, return Array(Top N)
    UPROPERTY(BlueprintAssignable)
    FTCATMultiResultPin OnSuccess;

    // Fail, Nothing found
    UPROPERTY(BlueprintAssignable)
    FTCATMultiSearchFailedPin OnFailed;

    /**
     * Finds the Top-N highest influence values within a specified radius.
     * @param MapTag            The influence layer to query.
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The maximum distance to scan.
     * @param MaxResults        Maximum number of results to return (e.g., Top 3).
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own influence.
	 * If true, attempts to subtract the component's own contribution from the sampled value.
	 * * @note Requires the composite logic to be **Reversible**. 
	 * If the layer logic involves lossy operations (e.g., Clamping, Min/Max), 
	 * the result will be an **Approximation** and may contain slight precision errors.
     * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue Projects result to height map.
	 * @param DistanceBiasType  Determines how distance affects the scoring.
	 * - None: Distance is ignored. Only the map value matters.
	 * - Standard: Linear falloff (1.0 at center, 0.0 at radius).
	 * - Relaxed: Convex curve (1-x^2). Favors a broader range of nearby areas.
	 * - Focused: Concave curve ((1-x)^2). Strictly prioritizes the immediate vicinity.
	 * @param DistanceBiasWeight Multiplier for the distance score.
	 * A value of 1.0 adds the full normalized distance score (0.0~1.0) to the influence value.
	 * Higher values make proximity more important than the influence map's raw value.
	 * @param InfluenceHalfHeightOverride Optional Influence Half Height Override -1 uses the source component default, 0 disables the reachability filter.
     */
    UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Highest Values (Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride, bUseWorldPosOverride, WorldPosToQueryOverride", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchHighestValues(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = true,
	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f, bool bUseWorldPosOverride = false, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

	/**
	 * Finds the Top-N lowest influence values within a specified radius.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Lowest Values (Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride, bUseWorldPosOverride, WorldPosToQueryOverride", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
	static UTCATAsyncMultiSearchAction* SearchLowestValues(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
	UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = true,
	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f, bool bUseWorldPosOverride = false, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

	/**
	 * Searches for the HIGHEST influence value, but only considers areas meeting a specific condition.
	 * Example: "Find the safest point (Highest Safety) where the fire damage is zero (Condition < 0)."
	 *
	 * @param MapTag            The influence layer to search for the highest value.
	 * @param CompareValue      The condition threshold.
	 * @param CompareType       The condition operator.
	 * @param SourceComponent   The search origin.
	 * @param SearchRadius      The search area radius.
	 * @param bSubtractSelfInfluence If true, attempts to subtract the component's own influence for the calculation.
	 * If true, attempts to subtract the component's own contribution from the sampled value.
	 * * @note Requires the composite logic to be **Reversible**. 
	 * If the layer logic involves lossy operations (e.g., Clamping, Min/Max), 
	 * the result will be an **Approximation** and may contain slight precision errors.
	 * @param bExcludeUnreachableLocation [Expensive] Validate NavMesh reachability.
	 * @param bTraceVisibility  [Expensive] Validate Line of Sight.
	 * @param bIgnoreZValue Project result to height map.
	 * @param DistanceBiasType  Determines how distance affects the scoring.
	 * - None: Distance is ignored. Only the map value matters.
	 * - Standard: Linear falloff (1.0 at center, 0.0 at radius).
	 * - Relaxed: Convex curve (1-x^2). Favors a broader range of nearby areas.
	 * - Focused: Concave curve ((1-x)^2). Strictly prioritizes the immediate vicinity.
	 * @param DistanceBiasWeight Multiplier for the distance score.
	 * Allows negative values to invert the effect (Penalty).
	 * A value of 1.0 adds the full normalized distance score (0.0~1.0) to the influence value.
	 * Higher values make proximity more important than the influence map's raw value.
	 * (Only applied when DistanceBiasType is not None)
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Highest Values In Condition(Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride, bUseWorldPosOverride, WorldPosToQueryOverride", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchHighestValuesInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, float CompareValue = 0.0f, ETCATCompareType CompareType = ETCATCompareType::Greater, int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = true,
    ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f, bool bUseWorldPosOverride = false, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

	/**
	 * Searches for the LOWEST influence value, but only considers areas meeting a specific condition.
	 * Example: "Find the point with least enemy presence (Lowest Threat) that is also within attack range (Condition < Range)."
	 *
	 * @param MapTag            The influence layer to search for the lowest value.
	 * @param CompareValue      The condition threshold.
	 * @param CompareType       The condition operator.
	 * @param SourceComponent   The search origin.
	 * @param SearchRadius      The search area radius.
	 * @param bSubtractSelfInfluence If true, attempts to subtract the component's own influence for the calculation.
	 * If true, attempts to subtract the component's own contribution from the sampled value.
	 * * @note Requires the composite logic to be **Reversible**. 
	 * If the layer logic involves lossy operations (e.g., Clamping, Min/Max), 
	 * the result will be an **Approximation** and may contain slight precision errors.
	 * @param bExcludeUnreachableLocation [Expensive] Validate NavMesh reachability.
	 * @param bTraceVisibility  [Expensive] Validate Line of Sight.
	 * @param bIgnoreZValue If false, respects the terrain height map.
	 * @param DistanceBiasType  Determines how distance affects the scoring.
	 * - None: Distance is ignored. Only the map value matters.
	 * - Standard: Linear falloff (1.0 at center, 0.0 at radius).
	 * - Relaxed: Convex curve (1-x^2). Favors a broader range of nearby areas.
	 * - Focused: Concave curve ((1-x)^2). Strictly prioritizes the immediate vicinity.
	 * @param DistanceBiasWeight Multiplier for the distance score.
	 * Allows negative values to invert the effect (Penalty).
	 * A value of 1.0 adds the full normalized distance score (0.0~1.0) to the influence value.
	 * Higher values make proximity more important than the influence map's raw value.
	 * (Only applied when DistanceBiasType is not None)
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Lowest Values In Condition(Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride, bUseWorldPosOverride, WorldPosToQueryOverride", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchLowestValuesInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, float CompareValue = 0.0f, ETCATCompareType CompareType = ETCATCompareType::Greater,  int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = true,
	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f, bool bUseWorldPosOverride = false, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

    virtual void Activate() override;
    static void ResetPool();

private:
    static UTCATAsyncMultiSearchAction* GetOrCreateAction(UObject* WorldContextObject);
    void FinishAndRelease();
    bool TryResolveQueryCenter(FVector& OutCenter) const;

    static TArray<UTCATAsyncMultiSearchAction*> ActionPool;
    
    UPROPERTY()
    TArray<FTCATSingleResult> CachedResults;

    // Query Parameters
    ETCATQueryType SelectedQueryType;
    UObject* WorldContext;
    
    FName TargetMapTag;
    float SearchRadius;
    int32 MaxResults;
    
    float TargetCompareValue;
    ETCATCompareType TargetCompareType;

    // Extract from Component
    TWeakObjectPtr<UTCATInfluenceComponent> TargetComponent;
    float HalfHeightOverride = -1.0f;

    // Advanced Settings
    bool bSubtractSelfInfluence;
    bool bExcludeUnreachableLocation;
    bool bTraceVisibility;
    bool bIgnoreZValue;

	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None;
	float DistanceBiasWeight = 0.0f;

	FVector WorldPosOverride = FVector::ZeroVector;
	bool bUseWorldPosOverride = false;
};

