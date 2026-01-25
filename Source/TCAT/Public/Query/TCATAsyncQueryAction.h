// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "Kismet/BlueprintAsyncActionBase.h"
#include "TCATQueryTypes.h"
#include "Core/TCATSubsystem.h"
#include "TCATAsyncQueryAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTCATAsyncSearchSuccessPin, float, Value, FVector, WorldPos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTCATAsyncSearchFailedPin);

/**
 * Asynchronous Blueprint Action for querying the TCAT Influence Map.
 * Handles single-result queries like finding the Highest/Lowest value or checking conditions.
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
     * 
     * @param MapTag            The specific influence layer tag to query (e.g., 'FireDamage', 'CoverSpots').
     * @param SourceComponent   The component acting as the search center. Also provides default influence data if applicable.
     * @param SearchRadius      The maximum distance from the SourceComponent to scan.
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
     *                          Note: Requires the composite logic to be **Reversible**. 
     *                          If the layer logic involves lossy operations (e.g., Clamping, Min/Max), 
     *                          the result will be an **Approximation** and may contain slight precision errors.
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight from the center to the target.
     * @param bIgnoreZValue     If false, projects the result onto the cached height map Z-plane instead of using the volume's 2D base Z.
     * @param bUseRandomizedTiebreaker Prevents all AIs from rushing to the exact same spot when scores are tied.
     * @param DistanceBiasType  Determines how distance affects the scoring.
     *                          - None: Distance is ignored. Only the map value matters.
     *                          - Standard: Linear falloff (1.0 at center, 0.0 at radius).
     *                          - Relaxed: Convex curve (1-x^2). Favors a broader range of nearby areas.
     *                          - Focused: Concave curve ((1-x)^2). Strictly prioritizes the immediate vicinity.
     * @param DistanceBiasWeight Multiplier for the distance score.
     *                           Allows negative values to invert the effect (Penalty).
     *                           A value of 1.0 adds the full normalized distance score (0.0~1.0) to the influence value.
     *                           (Only applied when DistanceBiasType is not None)
     * @param HalfHeightOverride Optional override for the Influence Half Height check. 
     *                           -1: Use Source Component's default radius/height settings.
     *                            0: Disable height check (Infinite cylinder).
     * @param WorldPosToQueryOverride Optional world position override for the search center. If provided, SourceComponent's location is ignored.
     * @param bDebug            If true, draws debug information for this query in the Visual Logger.
     * 
     * @return An async action node.
    */
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", 
	AdvancedDisplay="bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, bUseRandomizedTiebreaker, DistanceBiasType, DistanceBiasWeight, HalfHeightOverride,  WorldPosToQueryOverride, bDebug", 
	AutoCreateRefTerm="WorldPosToQueryOverride"), Category="TCAT|Query")
      static UTCATAsyncSearchAction* SearchHighestValue(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
      UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, bool bUseRandomizedTiebreaker = true, bool bDebug = false,
      ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float HalfHeightOverride = -1.0f, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

	/**
	 * Asynchronously searches for the lowest influence value within the radius.
	 * Shares the same behavior as SearchHighestValue but targets valleys instead of peaks.
	 * 
     * @param MapTag            The specific influence layer tag to query (e.g., 'FireDamage', 'CoverSpots').
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The maximum distance from the SourceComponent to scan.
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight.
     * @param bIgnoreZValue     If false, projects the result onto the cached height map Z-plane.
     * @param bUseRandomizedTiebreaker Prevents all AIs from rushing to the exact same spot when scores are tied.
     * @param DistanceBiasType  Determines how distance affects the scoring.
     * @param DistanceBiasWeight Multiplier for the distance score.
     * @param HalfHeightOverride Optional override for the Influence Half Height check.
     * @param WorldPosToQueryOverride Optional world position override.
     * @param bDebug            If true, draws debug information.
	 */
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject",
	AdvancedDisplay="bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, bUseRandomizedTiebreaker, DistanceBiasType, DistanceBiasWeight, HalfHeightOverride,  WorldPosToQueryOverride, bDebug",
	AutoCreateRefTerm="WorldPosToQueryOverride"), Category="TCAT|Query")
		static UTCATAsyncSearchAction* SearchLowestValue(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent = nullptr,
	 	UPARAM(meta=(ClampMin="0.0")) float SearchRadius = 500.0f, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, bool bUseRandomizedTiebreaker = true, bool bDebug = false,
	 	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float HalfHeightOverride = -1.0f, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

    /**
     * Asynchronously checks if any location within the radius meets a specific condition.
     * Can be used for: "Find any spot with > 50 Cover" or "Is there any Heal Pack nearby?".
     *
     * @param MapTag            The specific influence layer tag to query.
     * @param CompareValue      The threshold value to compare against (e.g., 0.5).
     * @param CompareType       The comparison operator (Greater, Less, Equal, etc.).
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The radius to scan.
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
     * @param bExcludeUnreachableLocation [Expensive] If true, ignores spots that are not on the NavMesh.
	 * @param bTraceVisibility  [Expensive] Validates Line of Sight.
	 * @param bIgnoreZValue     If false, respects the terrain height map.
	 * @param bUseRandomizedTiebreaker Prevents all AIs from rushing to the exact same spot when scores are tied.
	 * @param DistanceBiasType  Determines how distance affects the scoring.
	 * @param DistanceBiasWeight Multiplier for the distance score.
     * @param HalfHeightOverride Optional override for the Influence Half Height check.
     * @param WorldPosToQueryOverride Optional world position override.
     * @param bDebug            If true, draws debug information.
     * 
     * @return Returns the first valid position found, or fails if none exist.
     */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject",
    	AdvancedDisplay="bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasType, DistanceBiasWeight, HalfHeightOverride,  WorldPosToQueryOverride, bUseRandomizedTiebreaker, bDebug",
    	AutoCreateRefTerm="WorldPosToQueryOverride"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* SearchCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent=nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f, float CompareValue=0.0f, ETCATCompareType CompareType=ETCATCompareType::Greater, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false, bool bUseRandomizedTiebreaker = true, bool bDebug = false,
    ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float HalfHeightOverride = -1.0f, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

    /**
     * Asynchronously retrieves the influence value at the component's current location.
     *
     * @param MapTag            The influence layer to query.
     * @param SourceComponent   The target component whose location will be sampled.
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
     *                          Note: Requires the composite logic to be **Reversible**. 
     * @param bIgnoreZValue     If false, samples the Z-height from the baked map for better accuracy on uneven terrain.
     * @param HalfHeightOverride Optional override for the Influence Half Height check.
     * @param WorldPosToQueryOverride Optional world position override.
     * @param bDebug            If true, draws debug information.
     * 
     * @return The influence value at the component's location.
     */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject",
    	AdvancedDisplay="bSubtractSelfInfluence, bIgnoreZValue, HalfHeightOverride,  WorldPosToQueryOverride, bUseRandomizedTiebreaker, bDebug",
    	AutoCreateRefTerm="WorldPosToQueryOverride"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* GetValueAtComponent(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent=nullptr, bool bSubtractSelfInfluence = false, bool bUseRandomizedTiebreaker = true, bool bDebug = false,
    	bool bIgnoreZValue = false, UPARAM(meta=(ClampMin="-1.0")) float HalfHeightOverride = -1.0f,const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

    /**
     * Calculates the influence gradient (slope) at the component's location.
     * Useful for determining the direction of increasing or decreasing influence (e.g., "Which way is safer?").
     *
     * @param MapTag            The specific influence layer tag to query.
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The range to consider for local gradient calculation.
     * @param LookAheadDistance The distance to sample ahead to determine the slope direction.
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
	 * @param bIgnoreZValue     If false, respects the influence height map.
     * @param HalfHeightOverride Optional override for the Influence Half Height check.
     * @param WorldPosToQueryOverride Optional world position override.
     * @param bDebug            If true, draws debug information.
     * 
     * @return Returns the direction vector (slope) of the influence.
     */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AdvancedDisplay="bSubtractSelfInfluence, bIgnoreZValue, HalfHeightOverride,  WorldPosToQueryOverride, bUseRandomizedTiebreaker, bDebug", AutoCreateRefTerm="WorldPosToQueryOverride"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* GetInfluenceGradient(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent=nullptr, bool bUseRandomizedTiebreaker = true, bool bDebug = false,
       UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f, float LookAheadDistance = 100.0f, bool bSubtractSelfInfluence = false, bool bIgnoreZValue = false, UPARAM(meta=(ClampMin="-1.0")) float HalfHeightOverride = -1.0f, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);

    /**
     * Searches for the HIGHEST influence value, but only considers areas meeting a specific condition.
     * Example: "Find the safest point (Highest Safety) where the fire damage is zero (Condition < 0)."
     *
     * @param MapTag            The specific influence layer tag to query.
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The maximum distance from the SourceComponent to scan.
     * @param CompareValue      The threshold value to compare against.
     * @param CompareType       The comparison operator (Greater, Less, etc.).
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight from the center.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param bUseRandomizedTiebreaker Prevents all AIs from rushing to the exact same spot when scores are tied.
     * @param DistanceBiasType  Determines how distance affects the scoring.
     * @param DistanceBiasWeight Multiplier for the distance score.
     * @param HalfHeightOverride Optional override for the Influence Half Height check.
     * @param WorldPosToQueryOverride Optional world position override.
     * @param bDebug            If true, draws debug information.
     */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AdvancedDisplay="bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasType, DistanceBiasWeight, HalfHeightOverride,  WorldPosToQueryOverride, bUseRandomizedTiebreaker, bDebug", AutoCreateRefTerm="WorldPosToQueryOverride"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* SearchHighestInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent=nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f, float CompareValue=0.0f, ETCATCompareType CompareType=ETCATCompareType::Greater, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false,
    ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float HalfHeightOverride = -1.0f, const FVector& WorldPosToQueryOverride = FVector::ZeroVector, bool bUseRandomizedTiebreaker = true, bool bDebug = false);

    /**
	 * Searches for the LOWEST influence value, but only considers areas meeting a specific condition.
	 * Example: "Find the point with least enemy presence (Lowest Threat) that is also within attack range (Condition < Range)."
	 *
     * @param MapTag            The specific influence layer tag to query.
     * @param SourceComponent   The component acting as the search center.
     * @param SearchRadius      The maximum distance from the SourceComponent to scan.
     * @param CompareValue      The threshold value to compare against.
     * @param CompareType       The comparison operator (Greater, Less, etc.).
     * @param bSubtractSelfInfluence If true, attempts to subtract the component's own contribution from the sampled value.
     * @param bExcludeUnreachableLocation [Expensive] If true, validates whether the found location is reachable on the NavMesh.
     * @param bTraceVisibility  [Expensive] Validates Line of Sight from the center.
     * @param bIgnoreZValue     If false, respects the terrain height map.
     * @param bUseRandomizedTiebreaker Prevents all AIs from rushing to the exact same spot when scores are tied.
     * @param DistanceBiasType  Determines how distance affects the scoring.
     * @param DistanceBiasWeight Multiplier for the distance score.
     * @param HalfHeightOverride Optional override for the Influence Half Height check.
     * @param WorldPosToQueryOverride Optional world position override.
     * @param bDebug            If true, draws debug information.
	 */
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", AdvancedDisplay="bSubtractSelfInfluence, bExcludeUnreachableLocation, bTraceVisibility, bIgnoreZValue, DistanceBiasType, DistanceBiasWeight, HalfHeightOverride,  WorldPosToQueryOverride, bUseRandomizedTiebreaker, bDebug", AutoCreateRefTerm="WorldPosToQueryOverride"), Category="TCAT|Query")
        static UTCATAsyncSearchAction* SearchLowestInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent=nullptr,
    UPARAM(meta=(ClampMin="0.0")) float SearchRadius=500.0f, float CompareValue=0.0f, ETCATCompareType CompareType=ETCATCompareType::Greater, bool bSubtractSelfInfluence = false, bool bExcludeUnreachableLocation = false, bool bTraceVisibility = false, bool bIgnoreZValue = false,  bool bUseRandomizedTiebreaker = true, bool bDebug = false,
   ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None, float DistanceBiasWeight = 1.0, UPARAM(meta=(ClampMin="-1.0")) float HalfHeightOverride = -1.0f, const FVector& WorldPosToQueryOverride = FVector::ZeroVector);
    
    virtual void Activate() override;

    static void ResetPool();

private:
    static UTCATAsyncSearchAction* GetOrCreateAction(UObject* WorldContextObject);
    void FinishAndRelease();
    
    static TArray<UTCATAsyncSearchAction*> ActionPool;
    ETCATQueryType SelectedQueryType;
    
    UObject* WorldContext;
    
    FName TargetMapTag;
    float SearchRadius;
    float TargetCompareValue;
    ETCATCompareType TargetCompareType;
    
    // Extract from Component
    TWeakObjectPtr<UTCATInfluenceComponent> TargetComponent;
    FVector SearchCenter;
    float InfluenceRadius;
    float HalfHeightOverride = -1.0f;
    FTCATCurveCalculateInfo ReverseCalculationCurveInfo;

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

	FVector  WorldPosOffset = FVector::ZeroVector;

	bool TryResolveQueryCenter(FVector& OutCenter) const;
};
