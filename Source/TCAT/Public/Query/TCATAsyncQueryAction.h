// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "Kismet/BlueprintAsyncActionBase.h"
#include "TCATQueryTypes.h"
#include "Core/TCATSubsystem.h"
#include "TCATAsyncQueryAction.generated.h"

class UTCATInfluenceComponent;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTCATAsyncSearchSuccessPin, float, Value, FVector, WorldPos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTCATAsyncSearchFailedPin);

/**
 * Blueprint async wrapper for single-result queries (MaxResults = 1).
 *
 * Use these nodes when you need a single best answer:
 * - "Where is the best cover spot within 10m?"
 * - "Is there any cell with threat < 0?"
 * - "What is the score at my current position?"
 *
 * Under the hood, requests are batched by UTCATSubsystem and executed by FTCATQueryProcessor,
 * so you can safely call these for many AI agents without building your own scheduler.
 */
UCLASS()
class TCAT_API UTCATAsyncSearchAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
	/** Executed when the query successfully finds a result. Returns the Value and World Position. */
    UPROPERTY(BlueprintAssignable)
    FTCATAsyncSearchSuccessPin OnSuccess;

	/** Executed if no valid result is found, or if the query is cancelled/invalid. */
    UPROPERTY(BlueprintAssignable)
    FTCATAsyncSearchFailedPin OnFailed;

     /**
     * Asynchronously searches for the highest influence value within a specified radius.
     * This node leverages an optimized batch processing system, ensuring high performance 
     * even when handling hundreds of simultaneous queries.
     * Typical AI usage: pick a best target point for movement/positioning.
     * 
     * @param MapTag            The specific influence map tag to query (e.g., 'FireDamage', 'CoverSpots'). Also provides default influence data if applicable.
	 * @param SearchCenter      The center position in World Space to perform the search around.
     * @param SearchRadius      The maximum distance from the SearchCenter to scan.
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight from the center to the target.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param DistanceBiasCurve  Optional CurveFloat asset sampled from normalized distance (0 = center, 1 = search radius).
     *                          Use the curated curves under /TCAT/TCAT/Curves to ensure they are baked into the runtime atlas.
     * @param DistanceBiasWeight Multiplier for the distance score.
     *                           Allows negative values to invert the effect (Penalty).
     *                           A value of 1.0 adds the full normalized distance score (0.0~1.0) to the influence value.
     *                           (Only applied when DistanceBiasCurve is not None)
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
     * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
     * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
     * @return An async action node.
    */
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", 
	AdvancedDisplay="bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue,DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category="TCAT|Query")
      static UTCATAsyncSearchAction* SearchHighestValue(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f,
      bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0f, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

	/**
	 * Asynchronously searches for the lowest influence value within the radius.
	 * Shares the same behavior as SearchHighestValue but targets valleys instead of peaks.
	 * Typical AI usage: avoid danger (threat map), find weakest presence, find low-cost region, etc.
	 * 
     * @param MapTag            The specific influence map tag to query (e.g., 'FireDamage', 'CoverSpots').
     * @param SearchCenter      The center position in World Space to perform the search around.
     * @param SearchRadius      The maximum distance from the SearchCenter to scan.
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param DistanceBiasCurve  Optional curve sampled along the normalized distance (0 = center, 1 = radius).
     * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
	 * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
	 * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
	 */
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject",
	AdvancedDisplay="bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue,DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category="TCAT|Query")
		static UTCATAsyncSearchAction* SearchLowestValue(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f,
	 	bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0f, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

    /**
     * Asynchronously checks if any location within the radius meets a specific condition.
     * Use when you only need "yes + one position", not a globally optimal answer.
     * Can be used for: "Find any spot with > 50 Influence" or "Is there any Heal Pack nearby?"
     *
     * @param MapTag            The specific influence map tag to query.
	 * @param SearchCenter      The point in world space around which the condition is evaluated.
     * @param SearchRadius      The radius to scan.
     * @param CompareValue      The threshold value to compare against (e.g., 0.5).
     * @param CompareType       The comparison operator (Greater, Less, Equal, etc.).
     * @param bExcludeUnreachableLocation [Expensive] If true, ignores spots that are not on the NavMesh.
	 * @param bTraceVisibility  [Expensive] Validates Line of Sight.
	 * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param DistanceBiasCurve  Optional curve sampled along the normalized distance (0 = center, 1 = radius).
     * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
     * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
     * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
     */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AdvancedDisplay="bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* SearchCondition(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f, float CompareValue=0.0f, ETCATCompareType CompareType=ETCATCompareType::Greater,
		bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0f, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

    /**
     * Asynchronously retrieves the influence value at a specific world location.
	 * Use for UI/debug, decision thresholds, or feeding other systems (e.g., behavior weights).
	 *
     * @param MapTag            The influence map to query.
	 * @param SamplePosition    World position to evaluate.
     * @return The influence value at the requested location.
     */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* GetValueAtComponent(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SamplePosition);

	/**
     * Calculates the influence gradient (slope) at the component's location.
     * Useful for determining the direction of increasing or decreasing influence (e.g., "Which way is safer?").
	 *  Use when you want continuous steering rather than "jump to best cell".
	 * - LookAheadDistance == 0: returns a direction vector
	 * - LookAheadDistance != 0: returns a reachable target point (if filters enabled)
     *
     * @param MapTag            The specific influence map tag to query.
	 * @param SearchCenter      The world position to measure the gradient from.
     * @param SearchRadius      The range to consider for local gradient calculation.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param LookAheadDistance The distance to sample ahead to determine the slope direction. Pass a negative value to sample in the opposite direction.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
	 * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
	 * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
     * @return Returns the direction vector (slope) of the influence.
    */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AdvancedDisplay="bIgnoreZValue, InfluenceComponent"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* GetInfluenceGradient(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f,  float LookAheadDistance = 100.0f, bool bIgnoreZValue = false, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

    /**
     * Searches for the HIGHEST influence value, but only considers areas meeting a specific condition.
     * Example: "Find the safest point (Highest Safety) where the fire damage is zero (Condition < 0)."
     *
     * @param MapTag            The specific influence map tag to query.
	 * @param SearchCenter      The point in world space to center the scan.
     * @param SearchRadius      The maximum distance from the SearchCenter to scan.
     * @param CompareValue      The threshold value to compare against.
     * @param CompareType       The comparison operator (Greater, Less, etc.).
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight from the center.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     
     * @param DistanceBiasCurve  Optional curve sampled along the normalized distance (0 = center, 1 = radius).
     * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
     * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
     * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
     *
     * 
     */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AdvancedDisplay="bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* SearchHighestInCondition(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f, float CompareValue=0.0f, ETCATCompareType CompareType=ETCATCompareType::Greater,
		bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0f, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);

    /**
	 * Searches for the LOWEST influence value, but only considers areas meeting a specific condition.
	 * Example: "Find the point with least enemy presence (Lowest Threat) that is also within attack range (Condition < Range)."
	 *
     * @param MapTag            The specific influence map tag to query.
	 * @param SearchCenter      The point in world space to center the scan.
     * @param SearchRadius      The maximum distance from the SearchCenter to scan.
     * @param CompareValue      The threshold value to compare against.
     * @param CompareType       The comparison operator (Greater, Less, etc.).
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight from the center.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     
     * @param DistanceBiasCurve  Optional curve sampled along the normalized distance (0 = center, 1 = radius).
     * @param DistanceBiasWeight Multiplier for the distance score.
	 * @param InfluenceComponent [Optimization] Explicitly pass the agent's component to enable the "Fast Path".
     * - If Provided: The system skips runtime component lookup and directly uses cached data for self-influence removal. Recommended for high-frequency AI.
     * - If Null: The system automatically resolves it from the WorldContext (Self). Behavior is identical and perfectly safe, but incurs a minor CPU overhead for the lookup.
	 */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AdvancedDisplay="bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasCurve, DistanceBiasWeight, InfluenceComponent"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* SearchLowestInCondition(UObject* WorldContextObject, UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, FVector SearchCenter, UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f, float CompareValue=0.0f, ETCATCompareType CompareType=ETCATCompareType::Greater,  
		bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, UCurveFloat* DistanceBiasCurve = nullptr, float DistanceBiasWeight = 1.0f, UPARAM(meta=(DefaultToSelf="WorldContextObject")) UTCATInfluenceComponent* InfluenceComponent = nullptr);   
    virtual void Activate() override;

    static void ResetPool();

private:
	/**
	 * Lightweight pooling to avoid per-node allocations when many BP nodes fire per frame.
	 * This is important for "many AI agents" scenarios.
	 */
    static UTCATAsyncSearchAction* GetOrCreateAction(UObject* WorldContextObject);
    void FinishAndRelease();
    
    static TArray<UTCATAsyncSearchAction*> ActionPool;
    ETCATQueryType SelectedQueryType;
    
    UObject* WorldContext;
    
    FName TargetMapTag;
	FVector SearchCenter;
    float SearchRadius;
    float TargetCompareValue;
    ETCATCompareType TargetCompareType;
    
    // Extract from Component
    TWeakObjectPtr<UTCATInfluenceComponent> TargetComponent;
    float InfluenceRadius;
    FTCATCurveCalculateInfo ReverseCalculationCurveInfo;

    // Advanced Settings
    bool bExcludeUnreachableLocation;
    bool bTraceVisibility;
	bool bIgnoreZValue;
	
    TWeakObjectPtr<UCurveFloat> DistanceBiasCurve;
    float DistanceBiasWeight = 0.0f;
	
	static UTCATInfluenceComponent* ResolveInfluenceComponentFromContext(UObject* Context);
};