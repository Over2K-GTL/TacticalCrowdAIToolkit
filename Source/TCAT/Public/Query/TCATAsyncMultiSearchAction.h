// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "TCATQueryTypes.h"
#include "Core/TCATSubsystem.h"
#include "TCATAsyncMultiSearchAction.generated.h"

class UTCATInfluenceComponent;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTCATMultiResultPin, const TArray<FTCATSingleResult>&, Results);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTCATMultiSearchFailedPin);

/**
 * Blueprint async wrapper for multi-result queries (Top-N).
 *
 * Use these nodes when:
 * - You want multiple candidates to choose from (e.g., spread squads, avoid stacking).
 * - You plan to post-process (distance to goal, role-based selection, formation, etc.).
 *
 * Note: enabling reachability/LoS can be expensive with large N. Prefer:
 * - Small MaxResults (e.g., 3~8),
 * - Or keep filters off and do your own cheaper heuristic first.
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
     * @param MapTag            The influence map to query.
     * @param SearchCenter      The center position in World Space to perform the search around.
     * @param SearchRadius      The maximum distance to scan.
     * @param MaxResults        Maximum number of results to return (e.g., Top 3).
     * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param DistanceBiasCurve  Optional curve sampled along the normalized distance (0 = center, 1 = radius).
     * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
	 * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
	 * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
     * @return An async action node.
     */
    UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Highest Values (Multi)", AdvancedDisplay = "bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchHighestValues(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, int32 MaxResults = 3,
    	bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

	/**
	 * Finds the Top-N lowest influence values within a specified radius.
	 * 
     * @param MapTag            The influence map to query.
     * @param SearchRadius      The maximum distance to scan.
     * @param MaxResults        Maximum number of results to return.
     * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param DistanceBiasCurve  Optional curve sampled along the normalized distance (0 = center, 1 = radius).
     * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
	 * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
	 * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Lowest Values (Multi)", AdvancedDisplay = "bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category = "TCAT|Query")
	static UTCATAsyncMultiSearchAction* SearchLowestValues(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, int32 MaxResults = 3,
	bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

	/**
	 * Searches for the HIGHEST influence values, but only considers areas meeting a specific condition.
	 * Example: "Find Top 3 safest points (Highest Safety) where the fire damage is zero (Condition < 0)."
	 *
	 * @param MapTag            The influence map to query.
	 * @param CompareValue      The condition threshold.
	 * @param CompareType       The condition operator.
	 * @param SearchRadius      The search area radius.
	 * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
	 * @param bTraceVisibility  [Expensive] Validates Line of Sight.
	 * @param bIgnoreZValue     If false, respects the terrain height map.
	 * @param DistanceBiasCurve  Determines how distance affects the scoring.
	 * @param DistanceBiasWeight Multiplier for the distance score.
	* @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
	* - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
	* - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup. 
	 * @return Async action node.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Highest Values In Condition(Multi)", AdvancedDisplay = "bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchHighestValuesInCondition(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, float CompareValue = 0.0f, ETCATCompareType CompareType = ETCATCompareType::Greater,
    int32 MaxResults = 3, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

    /**
	 * Searches for the LOWEST influence values, but only considers areas meeting a specific condition.
	 * Example: "Find Top 3 points with least enemy presence (Lowest Threat) that are also within attack range (Condition < Range)."
	 * 
     * @param MapTag            The influence map to query.
     * @param CompareValue      The condition threshold.
     * @param CompareType       The condition operator.
     * @param SearchRadius      The search area radius.
     * @param bExcludeUnreachableLocation [Expensive] Validates NavMesh reachability.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param DistanceBiasCurve  Optional curve sampled along the normalized distance (0 = center, 1 = radius).
     * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
 	 * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
 	 * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Search Lowest Values In Condition(Multi)", AdvancedDisplay = "bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category = "TCAT|Query")
    static UTCATAsyncMultiSearchAction* SearchLowestValuesInCondition(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, float CompareValue = 0.0f, ETCATCompareType CompareType = ETCATCompareType::Greater,
    int32 MaxResults = 3, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

    virtual void Activate() override;
    static void ResetPool();

private:
	/** Internal pooling to reduce allocations for frequent multi queries. */
    static UTCATAsyncMultiSearchAction* GetOrCreateAction(UObject* WorldContextObject);
    void FinishAndRelease();

    static TArray<UTCATAsyncMultiSearchAction*> ActionPool;
    
    UPROPERTY()
    TArray<FTCATSingleResult> CachedResults;

    // Query Parameters
    ETCATQueryType SelectedQueryType;
    UObject* WorldContext;
    
    FName TargetMapTag;
	FVector SearchCenter;
    float SearchRadius;
    int32 MaxResults;
    
    float TargetCompareValue;
    ETCATCompareType TargetCompareType;

    // Extract from Component
    TWeakObjectPtr<UTCATInfluenceComponent> TargetComponent;

    // Advanced Settings
    bool bExcludeUnreachableLocation;
    bool bTraceVisibility;
	bool bIgnoreZValue;

    TWeakObjectPtr<UCurveFloat> DistanceBiasCurve;
    float DistanceBiasWeight = 0.0f;
};