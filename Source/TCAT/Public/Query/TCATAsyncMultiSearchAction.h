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
 * Asynchronous Blueprint Action for Multi-Result queries.
 * Returns the Top-N highest/lowest influence values instead of a single best result.
 */
UCLASS()
class TCAT_API UTCATAsyncMultiSearchAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    /** Executed when results are found. Returns an array of up to 'MaxResults' entries. */
    UPROPERTY(BlueprintAssignable)
    FTCATMultiResultPin OnSuccess;

    /** Executed if no valid results are found. */
    UPROPERTY(BlueprintAssignable)
    FTCATMultiSearchFailedPin OnFailed;

    /**
     * Finds the Top-N highest influence values within a specified radius.
     * 
     * @param MapTag            The influence layer to query.
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The maximum distance to scan.
     * @param MaxResults        Maximum number of results to return (e.g., Top 3).
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
     *                          Note: Requires the composite logic to be **Reversible**.
     * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, projects result to height map.
     * @param bUseRandomizedTiebreaker Prevents all AIs from rushing to the exact same spot when scores are tied.
     * @param DistanceBiasType  Determines how distance affects the scoring.
     * @param DistanceBiasWeight Multiplier for the distance score.
     * @param InfluenceHalfHeightOverride Optional override for the Influence Half Height check.
     *                           -1: Use Source Component's default.
     *                            0: Disable height check (Infinite cylinder).
     * @param bDebug            If true, draws debug information.
     * 
     * @return An async action node.
     */
    UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Highest Values (Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, bUseRandomizedTiebreaker, DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride,  WorldPosToQueryOverride, bDebug", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchHighestValues(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, bool bUseRandomizedTiebreaker = false, bool bDebug = false,
	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

	/**
	 * Finds the Top-N lowest influence values within a specified radius.
	 * 
     * @param MapTag            The influence layer to query.
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The maximum distance to scan.
     * @param MaxResults        Maximum number of results to return.
     * @param bSubtractSelfInfluence If true, subtracts self influence.
     * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, projects result to height map.
     * @param bUseRandomizedTiebreaker Prevents tie-breaking overlap.
     * @param DistanceBiasType  Determines how distance affects the scoring.
     * @param DistanceBiasWeight Multiplier for the distance score.
     * @param InfluenceHalfHeightOverride Optional height override.
     * @param bDebug            If true, draws debug information.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Lowest Values (Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, bUseRandomizedTiebreaker,DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride,  WorldPosToQueryOverride, bDebug", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
	static UTCATAsyncMultiSearchAction* SearchLowestValues(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
	UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, bool bUseRandomizedTiebreaker = false, bool bDebug = false,
	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f,  const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

	/**
	 * Searches for the HIGHEST influence values, but only considers areas meeting a specific condition.
	 * Example: "Find Top 3 safest points (Highest Safety) where the fire damage is zero (Condition < 0)."
	 *
	 * @param MapTag            The influence layer to query.
	 * @param CompareValue      The condition threshold.
	 * @param CompareType       The condition operator.
	 * @param SourceComponent   The search origin.
	 * @param SearchRadius      The search area radius.
	 * @param bSubtractSelfInfluence If true, subtracts self influence.
	 * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
	 * @param bTraceVisibility  [Expensive] Validates Line of Sight.
	 * @param bIgnoreZValue     If false, respects the terrain height map.
	 * @param bUseRandomizedTiebreaker Prevents tie-breaking overlap.
	 * @param DistanceBiasType  Determines how distance affects the scoring.
	 * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceHalfHeightOverride Optional height override.
	 * @param bDebug            If true, draws debug information.
	 * 
	 * @return Async action node.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Highest Values In Condition(Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, bUseRandomizedTiebreaker,DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride,  WorldPosToQueryOverride, bDebug", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchHighestValuesInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, float CompareValue = 0.0f, ETCATCompareType CompareType = ETCATCompareType::Greater, int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, bool bUseRandomizedTiebreaker = false, bool bDebug = false,
    ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f,  const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

	/**
	 * Searches for the LOWEST influence values, but only considers areas meeting a specific condition.
	 * Example: "Find Top 3 points with least enemy presence (Lowest Threat) that are also within attack range (Condition < Range)."
	 * 
     * @param MapTag            The influence layer to query.
     * @param CompareValue      The condition threshold.
     * @param CompareType       The condition operator.
     * @param SourceComponent   The search origin.
     * @param SearchRadius      The search area radius.
     * @param bSubtractSelfInfluence If true, subtracts self influence.
     * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param bUseRandomizedTiebreaker Prevents tie-breaking overlap.
     * @param DistanceBiasType  Determines how distance affects the scoring.
     * @param DistanceBiasWeight Multiplier for the distance score.
     * @param InfluenceHalfHeightOverride Optional height override.
     * @param bDebug            If true, draws debug information.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Lowest Values In Condition(Multi)", AdvancedDisplay = "bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, bUseRandomizedTiebreaker,DistanceBiasType, DistanceBiasWeight, InfluenceHalfHeightOverride,  WorldPosToQueryOverride, bDebug", AutoCreateRefTerm="WorldPosToQueryOverride"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchLowestValuesInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, float CompareValue = 0.0f, ETCATCompareType CompareType = ETCATCompareType::Greater,  int32 MaxResults = 3, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, bool bUseRandomizedTiebreaker = false, bool bDebug = false,
	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float InfluenceHalfHeightOverride = -1.0f,  const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

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

    /** Disables the tie-breaking jitter so identical scores stay identical. */
    bool bUseRandomizedTiebreaker = true;
    bool bDebugQuery = false;

    ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None;
    float DistanceBiasWeight = 0.0f;

	FVector WorldPosOffset = FVector::ZeroVector;
};