// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "Query/BT/BTService_TCATAsyncQuery.h"
#include "TCAT.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Core/TCATSubsystem.h"
#include "Scene/TCATInfluenceComponent.h"

UBTService_TCATAsyncQuery::UBTService_TCATAsyncQuery()
{
    NodeName = "TCAT Async Query Service";
    
    bNotifyTick = true;
    bNotifyCeaseRelevant = true;
    bCreateNodeInstance = true;

    Interval = 0.2f;
    RandomDeviation = 0.05f;

    CenterLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_TCATAsyncQuery, CenterLocationKey));
    CenterLocationKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_TCATAsyncQuery, CenterLocationKey), AActor::StaticClass());
    
    ResultLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_TCATAsyncQuery, ResultLocationKey));
    ResultValueKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_TCATAsyncQuery, ResultValueKey));
    ResultBoolKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_TCATAsyncQuery, ResultBoolKey));
}

void UBTService_TCATAsyncQuery::InitializeFromAsset(UBehaviorTree& Asset)
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
        UE_LOG(LogTCAT, Warning, TEXT("BTService [%s]: MapTag is None! This query will fail."), *GetName());
    }

    if (CenterLocationKey.SelectedKeyName.IsNone())
    {
        UE_LOG(LogTCAT, Warning, TEXT("BTService [%s]: Center Location Key is missing!"), *GetName());
    }

    if (QueryMode == ETCATServiceQueryMode::ConditionCheck)
    {
        if (ResultBoolKey.SelectedKeyName.IsNone())
        {
            UE_LOG(LogTCAT, Warning, TEXT("BTService [%s]: Result Bool Key is missing for Condition Check!"), *GetName());
        }
    }
    else
    {
        if (ResultLocationKey.SelectedKeyName.IsNone())
        {
            UE_LOG(LogTCAT, Warning, TEXT("BTService [%s]: Result Location Key is missing!"), *GetName());
        }
    }
}

FString UBTService_TCATAsyncQuery::GetStaticDescription() const
{
    // 0. Tick Info (Manual build to avoid redundant NodeName prefix from Super)
    FString Desc = FString::Printf(TEXT("Tick every %.2fs..%.2fs\n"), Interval, Interval + RandomDeviation);

    // 1. Mode Name
    switch (QueryMode)
    {
    case ETCATServiceQueryMode::HighestValue:   Desc += TEXT("Mode: Find Highest\n"); break;
    case ETCATServiceQueryMode::LowestValue:    Desc += TEXT("Mode: Find Lowest\n"); break;
    case ETCATServiceQueryMode::ConditionCheck: Desc += TEXT("Mode: Check Condition\n"); break;
    case ETCATServiceQueryMode::SamplePosition: Desc += TEXT("Mode: Sample Position\n"); break;
    case ETCATServiceQueryMode::Gradient:       Desc += TEXT("Mode: Get Gradient\n"); break;
    }

    // 2. Map Tag
    Desc += FString::Printf(TEXT("Map: [%s]"), *MapTag.ToString());

    // 2. Input -> Output Flow
    FString InputKeyName = CenterLocationKey.SelectedKeyName.IsNone() ? TEXT("None") : CenterLocationKey.SelectedKeyName.ToString();
    FString OutputKeyName;

    if (QueryMode == ETCATServiceQueryMode::ConditionCheck)
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
        if (QueryMode != ETCATServiceQueryMode::SamplePosition)
        {
            Desc += FString::Printf(TEXT("\nRadius: %.0f"), SearchRadius);
        }
        
        // Show Gradient LookAhead
        if (QueryMode == ETCATServiceQueryMode::Gradient)
        {
            Desc += FString::Printf(TEXT("\nLookAhead: %.0f"), LookAheadDistance);
        }
    }

    return Desc;
}

void UBTService_TCATAsyncQuery::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    if (bIsQuerying) { return; }

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!BB) { return; }

    UTCATSubsystem* TCAT = GetWorld()->GetSubsystem<UTCATSubsystem>();
    if (!TCAT) { return; }
    
    bIsQuerying = true;

    FVector CenterPos = GetLocationFromBB(CenterLocationKey, BB);

    TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp = &OwnerComp;
    
    auto QueryCallback = [this, WeakOwnerComp](const TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& Results)
    {
        if (Results.Num() > 0)
        {
            this->OnQueryFinished(WeakOwnerComp, Results[0].Value, Results[0].WorldPos, true);
        }
        else
        {
            this->OnQueryFinished(WeakOwnerComp, 0.0f, FVector::ZeroVector, false);
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
    NewQuery.RandomSeed = HashCombineFast(GetTypeHash(NewQuery.MapTag), GetTypeHash(NewQuery.Center));
    NewQuery.RandomSeed = HashCombineFast(NewQuery.RandomSeed, (uint32)GFrameCounter);

    NewQuery.bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    NewQuery.bTraceVisibility = bTraceVisibility;
    NewQuery.bIgnoreZValue = bIgnoreZValue;
    NewQuery.bUseRandomizedTiebreaker = bUseRandomizedTiebreaker;
    NewQuery.DistanceBiasType = DistanceBiasType;
    NewQuery.DistanceBiasWeight = DistanceBiasWeight;

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
    case ETCATServiceQueryMode::HighestValue:
        NewQuery.Type = bUseCondition ? ETCATQueryType::HighestValueInCondition : ETCATQueryType::HighestValue;
        break;

    case ETCATServiceQueryMode::LowestValue:
        if (bUseCondition)
        {
            NewQuery.Type = ETCATQueryType::LowestValueInCondition;
        }
        else
        {
            NewQuery.Type = ETCATQueryType::LowestValue;
        }
        break;

    case ETCATServiceQueryMode::ConditionCheck:
        NewQuery.Type = ETCATQueryType::Condition;
        break;

    case ETCATServiceQueryMode::SamplePosition:
        NewQuery.Type = ETCATQueryType::ValueAtPos;
        break;

    case ETCATServiceQueryMode::Gradient:
        NewQuery.Type = ETCATQueryType::Gradient;
        NewQuery.CompareValue = LookAheadDistance; 
        break;
    }

    AsyncQueryIdx = TCAT->RequestBatchQuery(MoveTemp(NewQuery));
}

void UBTService_TCATAsyncQuery::OnQueryFinished(TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp, float Value, FVector Location, bool bSuccess)
{
    bIsQuerying = false;
    AsyncQueryIdx = -1;

    if (!WeakOwnerComp.IsValid()) { return; }

    UBlackboardComponent* BB = WeakOwnerComp->GetBlackboardComponent();
    if (!BB) { return; }

    if (ResultValueKey.IsSet())
    {
        BB->SetValueAsFloat(ResultValueKey.SelectedKeyName, Value);
    }

    if (ResultLocationKey.IsSet() && QueryMode != ETCATServiceQueryMode::ConditionCheck)
    {
        if (bSuccess)
        {
            BB->SetValueAsVector(ResultLocationKey.SelectedKeyName, Location);
        }
    }

    if (ResultBoolKey.IsSet() && QueryMode == ETCATServiceQueryMode::ConditionCheck)
    {
        BB->SetValueAsBool(ResultBoolKey.SelectedKeyName, bSuccess);
    }
}

void UBTService_TCATAsyncQuery::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    Super::OnCeaseRelevant(OwnerComp, NodeMemory);

    if (AsyncQueryIdx != -1)
    {
        UTCATSubsystem* TCAT = GetWorld()->GetSubsystem<UTCATSubsystem>();
        if (TCAT)
        {
            TCAT->CancelBatchQuery(AsyncQueryIdx);
        }
    }

    bIsQuerying = false;
    AsyncQueryIdx = -1;
}

FVector UBTService_TCATAsyncQuery::GetLocationFromBB(const FBlackboardKeySelector& Key, UBlackboardComponent* BB)
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

#if WITH_EDITOR
void UBTService_TCATAsyncQuery::DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const
{
    Super::DescribeRuntimeValues(OwnerComp, NodeMemory, Verbosity, Values);

    if (Verbosity == EBTDescriptionVerbosity::Detailed)
    {
        Values.Add(FString::Printf(TEXT("Status: %s"), bIsQuerying ? TEXT("Querying...") : TEXT("Idle")));
        Values.Add(FString::Printf(TEXT("Next Tick: %.2fs"), GetNextTickRemainingTime(NodeMemory)));
    }
}
#endif
