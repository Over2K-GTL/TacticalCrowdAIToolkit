// Copyright 2025-2026 Over2K. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "Core/TCATSubsystem.h"
#include "BTService_TCATAsyncQuery.generated.h"

class UCurveFloat;
class UTCATInfluenceComponent;

/**
 * Defines the type of query this Behavior Tree Service will perform.
 */

UENUM(BlueprintType)
enum class ETCATServiceQueryMode : uint8
{
    HighestValue    UMETA(DisplayName = "Find Highest Value"),
    LowestValue     UMETA(DisplayName = "Find Lowest Value"),
    ConditionCheck  UMETA(DisplayName = "Check Condition"),
    SamplePosition  UMETA(DisplayName = "Get Value at Position"),
    Gradient        UMETA(DisplayName = "Get Influence Gradient")
};

/**
 * TCAT Async Query Service.
 * Periodically queries the influence map in the background to update Blackboard keys.
 * Best used with "Simple Parallel" node for continuous movement updates.
 */
UCLASS()
class TCAT_API UBTService_TCATAsyncQuery : public UBTService
{
    GENERATED_BODY()

public:
    UBTService_TCATAsyncQuery();

    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    
    // Service runs this periodically instead of ExecuteTask
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    
    // Called when the service becomes inactive (cleanup)
    virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    
    virtual FString GetStaticDescription() const override;

#if WITH_EDITOR
    virtual void DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const override;
#endif

protected:
    // =================================================================
    // [Main Settings]
    // =================================================================
    
    /** Select the type of query to perform. The UI will adapt based on this selection. */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (DisplayPriority = 1))
    ETCATServiceQueryMode QueryMode;

    /** The specific Influence Map to query (e.g., 'Ally', 'Enemy'). Must match a map tag defined in TCAT Settings. */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (DisplayPriority = 2, GetOptions = "TCAT.TCATSettings.GetAllTagOptions"))
    FName MapTag = "Default";
    
    /** Radius (in cm) of the search area around the SearchCenter Location. Hidden if sampling a single position. */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (EditCondition = "QueryMode != ETCATServiceQueryMode::SamplePosition", EditConditionHides, DisplayPriority = 4))
    float SearchRadius = 500.0f;
    
    /** 
     * [Gradient Mode Only] 
     * Distance (in cm) to project the look-ahead point based on the influence slope.
     * - If > 0: Returns a Location (Center + GradientDir * Dist).
     * - If 0: Returns the normalized Direction vector.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Main", meta = (EditCondition = "QueryMode == ETCATServiceQueryMode::Gradient", EditConditionHides))
    float LookAheadDistance = 0.0f;

    // =================================================================
    // [Blackboard Keys]
    // =================================================================

    /** [Input] The center location for the search (e.g., SelfActor, TargetActor). */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard")
    FBlackboardKeySelector CenterLocationKey;

    /** 
     * [Output] Where to store the resulting world position. 
     * Used for Highest/Lowest/Gradient queries.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard", meta = (EditCondition = "QueryMode != ETCATServiceQueryMode::ConditionCheck", EditConditionHides))
    FBlackboardKeySelector ResultLocationKey;

    /** [Output] (Optional) Where to store the raw influence value (score) found at the result location. */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard")
    FBlackboardKeySelector ResultValueKey;

    /** [Output] [Condition Mode Only] Where to store the boolean result (True if condition met, else False).
      * 
      * ConditionCheck result meaning:
      * - true  : query produced at least one valid result (condition satisfied / candidate found)
      * - false : query produced no result (condition not met or nothing found)
      *
      * Note: This is "query success" in the Condition mode, not necessarily "raw sampled value comparison"
      * unless your ETCATQueryType::Condition is defined that way.
      */
    UPROPERTY(EditAnywhere, Category = "TCAT Blackboard", meta = (EditCondition = "QueryMode == ETCATServiceQueryMode::ConditionCheck", EditConditionHides))
    FBlackboardKeySelector ResultBoolKey;

    // =================================================================
    // [Filter Options]
    // =================================================================

    /** 
     * If true, enables conditional filtering for Highest/Lowest value queries.
     * (e.g., "Find the Highest Value, BUT ignore anything below 0.5").
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Filter", meta = (EditCondition = "QueryMode == ETCATServiceQueryMode::HighestValue || QueryMode == ETCATServiceQueryMode::LowestValue", EditConditionHides))
    bool bUseCondition = false;

    /** The comparison operator (Greater, Less, Equal, etc.). */
    UPROPERTY(EditAnywhere, Category = "TCAT Filter", meta = (EditCondition = "QueryMode == ETCATServiceQueryMode::ConditionCheck || bUseCondition", EditConditionHides))
    ETCATCompareType CompareType = ETCATCompareType::Greater;

    /** The threshold value to compare against. */
    UPROPERTY(EditAnywhere, Category = "TCAT Filter", meta = (EditCondition = "QueryMode == ETCATServiceQueryMode::ConditionCheck || bUseCondition", EditConditionHides))
    float CompareValue = 0.5f;

    // =================================================================
    // [Advanced Options]
    // =================================================================
    /** [Expensive] If true, validates whether the candidate location is reachable on the NavMesh. */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    bool bExcludeUnreachableLocation = false;

    /** [Expensive] If true, validates Line of Sight from the center to the candidate location. */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    bool bTraceVisibility = false;

    /** 
     * If true, ignores the TCAT height map and uses 2D distance/logic. 
     * Recommended: False (Use cached height map for accuracy).
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    bool bIgnoreZValue = false;
    
    /**
     * Applies a distance-based bias to the score.
     * - Standard: Linear falloff. Prefer closer targets.
     * - Relaxed: Convex curve. Ok with mid-range targets.
     * - Focused: Concave curve. Strictly prefer immediate surroundings.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay, meta=(AllowedClasses="/Script/Engine.CurveFloat", AllowedPaths=TCAT_CURATED_CURVE_PATH_LITERAL))
    TObjectPtr<UCurveFloat> DistanceBiasCurve = nullptr;

    /** 
     * The weight of the distance bias (0.0 ~ 1.0 recommended). 
     * Higher values make proximity more important than the influence map's raw value.
     */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay, meta=(EditCondition="DistanceBiasCurve != nullptr"))
    float DistanceBiasWeight = 0.5f;

    /** Optional explicit influence component. If unset, the service auto-detects on the AI pawn. */
    UPROPERTY(EditAnywhere, Category = "TCAT Advanced", AdvancedDisplay)
    TObjectPtr<UTCATInfluenceComponent> ExplicitInfluenceComponent = nullptr;

    
private:
    /** Handler for the async query result. */
    void OnQueryFinished(TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp, float Value, FVector Location, bool bSuccess);

    FVector GetLocationFromBB(const FBlackboardKeySelector& Key, UBlackboardComponent* BB);

    /** Current Async Query ID to allow cancellation. */
    int32 AsyncQueryIdx = -1; 
    
    /** Flag to prevent multiple queries from stacking if Tick is faster than Query. */
    bool bIsQuerying = false;
};


