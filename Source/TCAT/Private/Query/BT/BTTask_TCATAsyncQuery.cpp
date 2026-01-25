// Copyright 2025-2026 Over2K. All Rights Reserved.
#include "Query/BT/BTTask_TCATAsyncQuery.h"
#include "TCAT.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Core/TCATSubsystem.h"
#include "Scene/TCATInfluenceComponent.h"
#include "Scene/TCATInfluenceVolume.h"

UBTTask_TCATAsyncQuery::UBTTask_TCATAsyncQuery()
{
    NodeName = "TCAT Async Query";
    bCreateNodeInstance = true;


    CenterLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_TCATAsyncQuery, CenterLocationKey));
    CenterLocationKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_TCATAsyncQuery, CenterLocationKey), AActor::StaticClass());
    ResultLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_TCATAsyncQuery, ResultLocationKey));
    ResultValueKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_TCATAsyncQuery, ResultValueKey));
    ResultBoolKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_TCATAsyncQuery, ResultBoolKey));
}

void UBTTask_TCATAsyncQuery::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);
    UBlackboardData* BBAsset = GetBlackboardAsset();
    if (BBAsset)
    {
        if (CenterLocationKey.SelectedKeyName != NAME_None)
        {
            CenterLocationKey.ResolveSelectedKey(*BBAsset);
        }
        if (ResultLocationKey.SelectedKeyName != NAME_None)
        {
            ResultLocationKey.ResolveSelectedKey(*BBAsset);
        }
        if (ResultValueKey.SelectedKeyName != NAME_None)
        {
            ResultValueKey.ResolveSelectedKey(*BBAsset);
        }
        if (ResultBoolKey.SelectedKeyName != NAME_None)
        {
            ResultBoolKey.ResolveSelectedKey(*BBAsset);
        }
    }

    // Editor Validation
    if (MapTag.IsNone())
    {
        UE_LOG(LogTCAT, Warning, TEXT("BTTask [%s]: MapTag is None! This query will fail."), *GetName());
    }

    if (CenterLocationKey.SelectedKeyName.IsNone())
    {
        UE_LOG(LogTCAT, Warning, TEXT("BTTask [%s]: Center Location Key is missing!"), *GetName());
    }

    if (QueryMode == ETCATTaskQueryMode::ConditionCheck)
    {
        if (ResultBoolKey.SelectedKeyName.IsNone())
        {
            UE_LOG(LogTCAT, Warning, TEXT("BTTask [%s]: Result Bool Key is missing for Condition Check!"), *GetName());
        }
    }
    else
    {
        if (ResultLocationKey.SelectedKeyName.IsNone())
        {
            UE_LOG(LogTCAT, Warning, TEXT("BTTask [%s]: Result Location Key is missing!"), *GetName());
        }
    }
}

FString UBTTask_TCATAsyncQuery::GetStaticDescription() const
{
    FString Desc;

    // 0. Mode Name
    switch (QueryMode)
    {
    case ETCATTaskQueryMode::HighestValue:   Desc += TEXT("Mode: Find Highest\n"); break;
    case ETCATTaskQueryMode::LowestValue:    Desc += TEXT("Mode: Find Lowest\n"); break;
    case ETCATTaskQueryMode::ConditionCheck: Desc += TEXT("Mode: Check Condition\n"); break;
    case ETCATTaskQueryMode::SamplePosition: Desc += TEXT("Mode: Sample Position\n"); break;
    case ETCATTaskQueryMode::Gradient:       Desc += TEXT("Mode: Get Gradient\n"); break;
    }

    // 1. Map Tag
    Desc += FString::Printf(TEXT("Map: [%s]"), *MapTag.ToString());

    // 2. Input -> Output Flow
    FString InputKeyName = CenterLocationKey.SelectedKeyName.IsNone() ? TEXT("None") : CenterLocationKey.SelectedKeyName.ToString();
    FString OutputKeyName;

    if (QueryMode == ETCATTaskQueryMode::ConditionCheck)
    {
        OutputKeyName = ResultBoolKey.SelectedKeyName.IsNone() ? TEXT("None") : ResultBoolKey.SelectedKeyName.ToString();
        Desc += FString::Printf(TEXT("\n%s -> %s"), *InputKeyName, *OutputKeyName);
        
        // Show condition details
        const TCHAR* OpStr = TEXT(">");
        switch(CompareType)
        {
            case ETCATCompareType::Greater: OpStr = TEXT(">"); break;
            case ETCATCompareType::GreaterOrEqual: OpStr = TEXT(">="); break;
            case ETCATCompareType::Less: OpStr = TEXT("<"); break;
            case ETCATCompareType::LessOrEqual: OpStr = TEXT("<="); break;
            case ETCATCompareType::Equal: OpStr = TEXT("=="); break;
            case ETCATCompareType::NotEqual: OpStr = TEXT("!="); break;
        }
        Desc += FString::Printf(TEXT("\nCheck: Value %s %.2f"), OpStr, CompareValue);
    }
    else
    {
        OutputKeyName = ResultLocationKey.SelectedKeyName.IsNone() ? TEXT("None") : ResultLocationKey.SelectedKeyName.ToString();
        Desc += FString::Printf(TEXT("\n%s -> %s"), *InputKeyName, *OutputKeyName);

        // Show radius for search modes
        if (QueryMode != ETCATTaskQueryMode::SamplePosition)
        {
            Desc += FString::Printf(TEXT("\nRadius: %.0f"), SearchRadius);
        }
        
        // Show Gradient LookAhead
        if (QueryMode == ETCATTaskQueryMode::Gradient)
        {
            Desc += FString::Printf(TEXT("\nLookAhead: %.0f"), LookAheadDistance);
        }
    }

    return Desc;
}

EBTNodeResult::Type UBTTask_TCATAsyncQuery::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!BB) return EBTNodeResult::Failed;

    UTCATSubsystem* TCAT = GetWorld()->GetSubsystem<UTCATSubsystem>();
    if (!TCAT)
    {
        UE_LOG(LogTCAT, Error, TEXT("TCATSubsystem not found! Make sure the plugin is loaded."));
        return EBTNodeResult::Failed;
    }

    CachedOwnerComp = &OwnerComp;

    FVector CenterPos = GetLocationFromBB(CenterLocationKey, BB);
    auto QueryCallback = [this](const TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& Results)
    {
        if (Results.Num() > 0)
        {
            this->OnQueryFinished(Results[0].Value, Results[0].WorldPos, true);
        }
        else
        {
            this->OnQueryFinished(0, FVector::ZeroVector, false);
        }
    };

    FTCATBatchQuery NewQuery;
    NewQuery.MapTag = MapTag;
    NewQuery.Center = CenterPos;
    NewQuery.CompareValue = CompareValue;
    NewQuery.CompareType = CompareType;
    NewQuery.SearchRadius = SearchRadius;
    NewQuery.OnComplete = QueryCallback;
    NewQuery.MaxResults = 1;
    
    NewQuery.bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    NewQuery.bTraceVisibility = bTraceVisibility;
    NewQuery.bIgnoreZValue = bIgnoreZValue;
    NewQuery.bUseRandomizedTiebreaker = bUseRandomizedTiebreaker;

#if ENABLE_VISUAL_LOG
    NewQuery.DebugInfo.bEnabled = bDebugQuery;
#endif
    
    NewQuery.DistanceBiasType = DistanceBiasType;
    NewQuery.DistanceBiasWeight = DistanceBiasWeight;

    NewQuery.RandomSeed = HashCombineFast(GetTypeHash(NewQuery.MapTag), GetTypeHash(NewQuery.Center));
    NewQuery.RandomSeed = HashCombineFast(NewQuery.RandomSeed, (uint32)GFrameCounter);
    
    float ResolvedHalfHeight = HalfHeightOverride;
    ATCATInfluenceVolume* Volume = TCAT->GetInfluenceVolume(MapTag);
    NewQuery.Curve = nullptr;
    NewQuery.SelfRemovalFactor = 0.0f;
    NewQuery.InfluenceRadius = 0.0f;

    UTCATInfluenceComponent* InfluenceComp = nullptr;
    if (AActor* OwnerActor = OwnerComp.GetOwner())
    {
        InfluenceComp = OwnerActor->FindComponentByClass<UTCATInfluenceComponent>();
    }

    const bool bComponentHasLayer = InfluenceComp && InfluenceComp->HasInfluenceLayer(MapTag);
    if (ResolvedHalfHeight < 0.0f)
    {
        if (bComponentHasLayer)
        {
            ResolvedHalfHeight = InfluenceComp->GetInfluenceHalfHeight(MapTag);
        }
        else
        {
            ResolvedHalfHeight = 0.0f;
        }
    }

    NewQuery.InfluenceHalfHeight = ResolvedHalfHeight;

    if (bSubtractSelfInfluence && InfluenceComp && bComponentHasLayer && Volume)
    {
        const FTCATSelfInfluenceResult SelfResult = InfluenceComp->GetSelfInfluenceResult(MapTag, Volume);

        if (SelfResult.IsValid())
        {
            NewQuery.Curve = SelfResult.Curve;
            NewQuery.SelfRemovalFactor = SelfResult.FinalRemovalFactor;
            NewQuery.InfluenceRadius = SelfResult.InfluenceRadius;
        }
    }

    switch (QueryMode)
    {
    case ETCATTaskQueryMode::HighestValue:
        {
            if (bUseCondition)
            {
                NewQuery.Type = ETCATQueryType::HighestValueInCondition;
            }
            else
            {
                NewQuery.Type = ETCATQueryType::HighestValue;
            }
            break;   
        }

    case ETCATTaskQueryMode::LowestValue:
        {
            if (bUseCondition)
            {
                NewQuery.Type = ETCATQueryType::LowestValueInCondition;
            }
            else
            {
                NewQuery.Type = ETCATQueryType::LowestValue;
            }
            break;   
        }

    case ETCATTaskQueryMode::ConditionCheck:
        {
            NewQuery.Type = ETCATQueryType::Condition;
            break;
        }
    case ETCATTaskQueryMode::SamplePosition:
        {
            NewQuery.Type = ETCATQueryType::ValueAtPos;
            break;
        }
    case ETCATTaskQueryMode::Gradient:
        {
            NewQuery.Type = ETCATQueryType::Gradient;
            NewQuery.CompareValue = LookAheadDistance;
        }
        break;
    }

    AsyncQueryIdx = TCAT->RequestBatchQuery(MoveTemp(NewQuery));
    return EBTNodeResult::InProgress;
}

void UBTTask_TCATAsyncQuery::OnQueryFinished(float Value, FVector Location, bool bSuccess)
{
    // Check if the BT Component is still valid (it might have been destroyed or aborted)
    if (!CachedOwnerComp.IsValid()) { return; }

    AsyncQueryIdx = -1;

    UBlackboardComponent* BB = CachedOwnerComp->GetBlackboardComponent();
    if (BB)
    {
        // Update Value Key (Optional)
        if (ResultValueKey.IsSet())
        {
            BB->SetValueAsFloat(ResultValueKey.SelectedKeyName, Value);
        }

        // Update Location Key
        if (ResultLocationKey.IsSet() && QueryMode != ETCATTaskQueryMode::ConditionCheck)
        {
            if (bSuccess)
            {
                BB->SetValueAsVector(ResultLocationKey.SelectedKeyName, Location);
            }
            else
            {
                // Optional: Clear value or set to ZeroVector on failure
                BB->ClearValue(ResultLocationKey.SelectedKeyName);
            }
        }

        // Update Bool Key (Condition Mode)
        if (ResultBoolKey.IsSet() && QueryMode == ETCATTaskQueryMode::ConditionCheck)
        {
            BB->SetValueAsBool(ResultBoolKey.SelectedKeyName, bSuccess);
        }
    }

    EBTNodeResult::Type FinalResult = bSuccess ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
    FinishLatentTask(*CachedOwnerComp, FinalResult);
}

EBTNodeResult::Type UBTTask_TCATAsyncQuery::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    if (AsyncQueryIdx != -1)
    {
        UTCATSubsystem* TCAT = GetWorld()->GetSubsystem<UTCATSubsystem>();
        if (TCAT)
        {
            TCAT->CancelBatchQuery(AsyncQueryIdx);
        }
    }

    AsyncQueryIdx = -1;

    return Super::AbortTask(OwnerComp, NodeMemory);
}

void UBTTask_TCATAsyncQuery::DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const
{
    Super::DescribeRuntimeValues(OwnerComp, NodeMemory, Verbosity, Values);
    
    // Useful for Debugging in the Editor
    if (Verbosity == EBTDescriptionVerbosity::Detailed)
    {
        Values.Add(FString::Printf(TEXT("Async Task: %s"), AsyncQueryIdx != -1 ? TEXT("Running") : TEXT("Idle")));
    }
}


FVector UBTTask_TCATAsyncQuery::GetLocationFromBB(const FBlackboardKeySelector& Key, UBlackboardComponent* BB)
{
    if (!BB) { return FVector::ZeroVector; }
    
    if (BB->IsKeyOfType<UBlackboardKeyType_Object>(Key.GetSelectedKeyID()))
    {
        UObject* KeyObj = BB->GetValueAsObject(Key.SelectedKeyName);
        AActor* TargetActor = Cast<AActor>(KeyObj);
        
        return TargetActor ? TargetActor->GetActorLocation() : FVector::ZeroVector;
    }
    
    return BB->GetValueAsVector(Key.SelectedKeyName);
}