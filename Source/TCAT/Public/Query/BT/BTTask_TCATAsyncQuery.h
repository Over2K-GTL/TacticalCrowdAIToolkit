// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Core/TCATSubsystem.h"
#include "BTTask_TCATAsyncQuery.generated.h"

/**
 * Defines the type of query this Behavior Tree Task will perform.
 */
UENUM(BlueprintType)
enum class ETCATTaskQueryMode : uint8
{
    HighestValue    UMETA(DisplayName = "Find Highest Value"),
    LowestValue     UMETA(DisplayName = "Find Lowest Value"),
    ConditionCheck  UMETA(DisplayName = "Check Condition"),
    SamplePosition  UMETA(DisplayName = "Get Value at Position"),
    Gradient        UMETA(DisplayName = "Get Influence Gradient")
};

/**
 * Universal Async Query Task for TCAT System.
 * Handles heavy influence map queries asynchronously without blocking the game thread.
 */
UCLASS()
class TCAT_API UBTTask_TCATAsyncQuery : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_TCATAsyncQuery();

    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const override;
    
    // UX Improvements
    virtual FString GetStaticDescription() const override;

protected:
    // =================================================================
    // [Main Settings]
    // =================================================================
    
    /** Select the type of query to perform. The UI will adapt based on this selection. */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (DisplayPriority = 1))
    ETCATTaskQueryMode QueryMode;

    /** The identifier for the influence map group to search (e.g., 'Ally', 'Enemy', 'Danger'). */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (DisplayPriority = 2))
    FName MapTag = "Default";

    /** Radius of the search area around the Center Location. Hidden if sampling a single position. */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (EditCondition = "QueryMode != ETCATTaskQueryMode::SamplePosition", EditConditionHides, DisplayPriority = 3))
    float SearchRadius = 500.0f;
    
    /** 
     * [Gradient Mode Only] 
     * Distance to project the look-ahead point based on the influence slope.
     * - If > 0: Returns a Location (Center + GradientDir * Dist).
     * - If 0: Returns the normalized Direction vector.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (EditCondition = "QueryMode == ETCATTaskQueryMode::Gradient", EditConditionHides))
    float LookAheadDistance = 0.0f;

    // =================================================================
    // [Blackboard Keys]
    // =================================================================

    /** The center location for the search (e.g., SelfActor, TargetActor). */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard")
    FBlackboardKeySelector CenterLocationKey;

    /** 
     * Where to store the resulting world position. 
     * Used for Highest/Lowest/Gradient queries.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard", meta = (EditCondition = "QueryMode != ETCATTaskQueryMode::ConditionCheck", EditConditionHides))
    FBlackboardKeySelector ResultLocationKey;

    /** (Optional) Where to store the raw influence value (score) found at the result location. */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard")
    FBlackboardKeySelector ResultValueKey;

    /** [Condition Mode Only] Where to store the boolean result (True if condition met, else False). */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard", meta = (EditCondition = "QueryMode == ETCATTaskQueryMode::ConditionCheck", EditConditionHides))
    FBlackboardKeySelector ResultBoolKey;

    // =================================================================
    // [Filter Options]
    // =================================================================

    /** 
     * If true, enables conditional filtering for Highest/Lowest value queries.
     * (e.g., "Find the Highest Value, BUT ignore anything below 0.5").
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Filter", meta = (EditCondition = "QueryMode == ETCATTaskQueryMode::HighestValue || QueryMode == ETCATTaskQueryMode::LowestValue", EditConditionHides))
    bool bUseCondition = false;

    /** The comparison operator (Greater, Less, Equal, etc.). */
    UPROPERTY(EditAnywhere, Category = "TCAT Filter", meta = (EditCondition = "QueryMode == ETCATTaskQueryMode::ConditionCheck || bUseCondition", EditConditionHides))
    ETCATCompareType CompareType = ETCATCompareType::Greater;

    /** The threshold value to compare against. */
    UPROPERTY(EditAnywhere, Category = "TCAT Filter", meta = (EditCondition = "QueryMode == ETCATTaskQueryMode::ConditionCheck || bUseCondition", EditConditionHides))
    float CompareValue = 0.5f;

    // =================================================================
    // [Advanced Options]
    // =================================================================

    /** 
     * If true, attempts to subtract the component's own influence from the calculation.
     * Essential for finding "Safe Spots" while the agent itself is emitting "Danger".
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    bool bSubtractSelfInfluence = false;

    /** [Expensive] If true, verifies if the candidate location is reachable via NavMesh. */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    bool bExcludeUnreachableLocation = false;

    /** [Expensive] If true, verifies if the candidate location has Line of Sight from the center. */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    bool bTraceVisibility = false;

    /** 
     * If true, snaps the Z-height to the TCAT baked height map for better accuracy. 
     * Disable this if you want 2D-only logic.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    bool bIgnoreZValue = false;

    /**
     * Applies a distance-based bias to the score.
     * - Standard: Linear falloff. Prefer closer targets.
     * - Relaxed: Convex curve. Ok with mid-range targets.
     * - Focused: Concave curve. Strictly prefer immediate surroundings.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None;

    /** 
     * The weight of the distance bias (0.0 ~ 1.0 recommended). 
     * Higher values make proximity more important than the influence map's raw value.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay, meta=(EditCondition="DistanceBiasType != ETCATDistanceBias::None"))
    float DistanceBiasWeight = 0.5f;

    /** 
     * Optional override for the Influence Half Height. 
     * -1: Use Source Component's default. 
     *  0: Disable height check (Infinite cylinder).
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay, meta=(ClampMin="-1.0"))
    float HalfHeightOverride = -1.0f;

private:
    /** Callback function to handle the async result from the TCAT Subsystem. */
    void OnQueryFinished(float Value, FVector Location, bool bSuccess);

    static FVector GetLocationFromBB(const FBlackboardKeySelector& Key, UBlackboardComponent* BB);

    /** Weak pointer to the BT Component to ensure safety if the task is aborted/destroyed. */
    TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

    /** * Handle to the active async request. 
     * Used to cancel the request if the task is aborted.
     */
    int32 AsyncQueryIdx = -1; 
};

