// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "Query/TCATAsyncMultiSearchAction.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Scene/TCATInfluenceComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

TArray<UTCATAsyncMultiSearchAction*> UTCATAsyncMultiSearchAction::ActionPool;

namespace
{
    static UTCATInfluenceComponent* ResolveInfluenceComponentFromContext(UObject* Context)
    {
        if (UTCATInfluenceComponent* DirectComp = Cast<UTCATInfluenceComponent>(Context))
        {
            return DirectComp;
        }

        if (const UActorComponent* ActorComp = Cast<UActorComponent>(Context))
        {
            if (const AActor* Owner = ActorComp->GetOwner())
            {
                return Owner->FindComponentByClass<UTCATInfluenceComponent>();
            }
        }
        else if (const AActor* Actor = Cast<AActor>(Context))
        {
            return Actor->FindComponentByClass<UTCATInfluenceComponent>();
        }

        return nullptr;
    }
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchHighestValues(UObject* WorldContextObject, FName MapTag, FVector InSearchCenter, float InSearchRadius, int32 InMaxResults,
    bool bInExcludeUnreachableLocation, bool bInTraceVisibility, bool bInIgnoreZValue, UCurveFloat* InDistanceBiasCurve, float InDistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetMapTag = MapTag;
    Action->SearchCenter = InSearchCenter;
    Action->SearchRadius = InSearchRadius;
    Action->MaxResults = FMath::Max(1, InMaxResults);

    Action->TargetCompareValue = 0.0f;
    Action->TargetCompareType = ETCATCompareType::Greater;

    Action->bExcludeUnreachableLocation = bInExcludeUnreachableLocation;
    Action->bTraceVisibility = bInTraceVisibility;
    Action->bIgnoreZValue = bInIgnoreZValue;

    Action->TargetComponent = InInfluenceComponent;
    Action->DistanceBiasCurve = InDistanceBiasCurve;
    Action->DistanceBiasWeight = InDistanceBiasWeight;

    Action->SelectedQueryType = ETCATQueryType::HighestValue;

    return Action;
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchLowestValues(UObject* WorldContextObject, FName MapTag, FVector InSearchCenter, float InSearchRadius, int32 InMaxResults,
    bool bInExcludeUnreachableLocation, bool bInTraceVisibility, bool bInIgnoreZValue, UCurveFloat* InDistanceBiasCurve, float InDistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetMapTag = MapTag;
    Action->SearchCenter = InSearchCenter;
    Action->SearchRadius = InSearchRadius;
    Action->MaxResults = FMath::Max(1, InMaxResults);

    Action->TargetCompareValue = 0.0f;
    Action->TargetCompareType = ETCATCompareType::Greater;

    Action->bExcludeUnreachableLocation = bInExcludeUnreachableLocation;
    Action->bTraceVisibility = bInTraceVisibility;
    Action->bIgnoreZValue = bInIgnoreZValue;

    Action->TargetComponent = InInfluenceComponent;
    Action->DistanceBiasCurve = InDistanceBiasCurve;
    Action->DistanceBiasWeight = InDistanceBiasWeight;

    Action->SelectedQueryType = ETCATQueryType::LowestValue;

    return Action;
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchHighestValuesInCondition(UObject* WorldContextObject,FName MapTag, FVector InSearchCenter, float InSearchRadius,
    float CompareValue, ETCATCompareType CompareType, int32 InMaxResults, bool bInExcludeUnreachableLocation, bool bInTraceVisibility, bool bInIgnoreZValue, UCurveFloat* InDistanceBiasCurve, float InDistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetMapTag = MapTag;
    Action->SearchCenter = InSearchCenter;
    Action->SearchRadius = InSearchRadius;
    Action->MaxResults = FMath::Max(1, InMaxResults);

    Action->TargetCompareValue = CompareValue;
    Action->TargetCompareType = CompareType;

    Action->bExcludeUnreachableLocation = bInExcludeUnreachableLocation;
    Action->bTraceVisibility = bInTraceVisibility;
    Action->bIgnoreZValue = bInIgnoreZValue;

    Action->TargetComponent = InInfluenceComponent;
    Action->DistanceBiasCurve = InDistanceBiasCurve;
    Action->DistanceBiasWeight = InDistanceBiasWeight;

    Action->SelectedQueryType = ETCATQueryType::HighestValueInCondition;

    return Action;
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchLowestValuesInCondition(UObject* WorldContextObject, FName MapTag, FVector InSearchCenter, float InSearchRadius,
    float CompareValue, ETCATCompareType CompareType, int32 InMaxResults, bool bInExcludeUnreachableLocation, bool bInTraceVisibility, bool bInIgnoreZValue, UCurveFloat* InDistanceBiasCurve, float InDistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetMapTag = MapTag;
    Action->SearchCenter = InSearchCenter;
    Action->SearchRadius = InSearchRadius;
    Action->MaxResults = FMath::Max(1, InMaxResults);

    Action->TargetCompareValue = CompareValue;
    Action->TargetCompareType = CompareType;

    Action->bExcludeUnreachableLocation = bInExcludeUnreachableLocation;
    Action->bTraceVisibility = bInTraceVisibility;
    Action->bIgnoreZValue = bInIgnoreZValue;

    Action->TargetComponent = InInfluenceComponent;
    Action->DistanceBiasCurve = InDistanceBiasCurve;
    Action->DistanceBiasWeight = InDistanceBiasWeight;

    Action->SelectedQueryType = ETCATQueryType::LowestValueInCondition;

    return Action;
}

void UTCATAsyncMultiSearchAction::Activate()
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        FinishAndRelease();
        return;
    }

    UTCATSubsystem* Subsystem = World->GetSubsystem<UTCATSubsystem>();
    if (!Subsystem)
    {
        OnFailed.Broadcast();
        FinishAndRelease();
        return;
    }

    AActor* ContextActor = Cast<AActor>(WorldContext);
    if (!ContextActor)
    {
        if (const UActorComponent* ContextComp = Cast<UActorComponent>(WorldContext))
        {
            ContextActor = ContextComp->GetOwner();
        }
    }

    if (ContextActor && !TargetComponent.IsValid())
    {
        TargetComponent = ContextActor->FindComponentByClass<UTCATInfluenceComponent>();
    }

    ATCATInfluenceVolume* Volume = Subsystem->GetInfluenceVolume(TargetMapTag);
    const FVector FinalCenter = SearchCenter;

    CachedResults.Empty(FMath::Max(MaxResults, 8));

    FTCATBatchQuery Query;
    Query.Type = SelectedQueryType;
    Query.MapTag = TargetMapTag;
    Query.SearchCenter = FinalCenter;
    Query.SearchRadius = SearchRadius;
    Query.CompareValue = TargetCompareValue;
    Query.CompareType = TargetCompareType;

    Query.bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    Query.bTraceVisibility = bTraceVisibility;
    Query.bIgnoreZValue = bIgnoreZValue;

    Query.DistanceBiasCurve = DistanceBiasCurve;
    Query.DistanceBiasCurveID = DistanceBiasCurve.IsValid() ? Subsystem->GetCurveID(DistanceBiasCurve.Get()) : INDEX_NONE;
    Query.DistanceBiasWeight = DistanceBiasWeight;

#if ENABLE_VISUAL_LOG
    Query.DebugInfo.DebugOwner = ContextActor;
#endif
    Query.MaxResults = FMath::Max(1, MaxResults);
    Query.RandomSeed = HashCombineFast(GetTypeHash(Query.MapTag), GetTypeHash(Query.SearchCenter));
    Query.RandomSeed = HashCombineFast(Query.RandomSeed, (uint32)GFrameCounter);

    Query.Curve = nullptr;
    Query.SelfRemovalFactor = 0.0f;
    Query.InfluenceRadius = 0.0f;

    if (TargetComponent.IsValid() && Volume)
    {
        FTCATSelfInfluenceResult SelfResult = TargetComponent->GetSelfInfluenceResult(TargetMapTag, Volume);
            if (SelfResult.IsValid())
            {
                Query.Curve = SelfResult.Curve;
                Query.SelfCurveID = SelfResult.CurveTypeIndex;
                Query.SelfRemovalFactor = SelfResult.FinalRemovalFactor;
                Query.InfluenceRadius = SelfResult.InfluenceRadius;
                Query.SelfOrigin = SelfResult.SourceLocation;
            }
        }

    Query.OnComplete = [this](const FTCATQueryResultArray& Results)
    {
        CachedResults.Reset();

        if (Results.Num() > 0)
        {
            CachedResults.Append(Results);
            OnSuccess.Broadcast(CachedResults);
        }
        else
        {
            OnFailed.Broadcast();
        }

        FinishAndRelease();
    };

    Subsystem->RequestBatchQuery(MoveTemp(Query));
}

void UTCATAsyncMultiSearchAction::ResetPool()
{
    for (UTCATAsyncMultiSearchAction* Action : ActionPool)
    {
        if (Action && Action->IsValidLowLevel())
        {
            Action->RemoveFromRoot();
            Action->SetReadyToDestroy();
        }
    }
    ActionPool.Empty();
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::GetOrCreateAction(UObject* WorldContextObject)
{
    UTCATAsyncMultiSearchAction* Action = nullptr;
    if (ActionPool.Num() > 0)
    {
        Action = ActionPool.Pop();
    }
    else
    {
        Action = NewObject<UTCATAsyncMultiSearchAction>();
        Action->AddToRoot();
    }

    Action->WorldContext = WorldContextObject;
    Action->TargetComponent.Reset();
    Action->TargetComponent = ResolveInfluenceComponentFromContext(WorldContextObject);

    Action->TargetMapTag = NAME_None;
    Action->SearchCenter = FVector::ZeroVector;
    Action->SearchRadius = 0.0f;
    Action->MaxResults = 0;
    Action->TargetCompareValue = 0.0f;
    Action->TargetCompareType = ETCATCompareType::Greater;
    Action->bExcludeUnreachableLocation = false;
    Action->bTraceVisibility = false;
    Action->bIgnoreZValue = false;
    Action->DistanceBiasCurve = nullptr;
    Action->DistanceBiasWeight = 0.0f;

    return Action;
}

void UTCATAsyncMultiSearchAction::FinishAndRelease()
{
    SetReadyToDestroy();

    OnSuccess.Clear();
    OnFailed.Clear();

    CachedResults.Reset();
    TargetComponent.Reset();
    WorldContext = nullptr;
    DistanceBiasCurve = nullptr;
    DistanceBiasWeight = 0.0f;

    ActionPool.Add(this);
}