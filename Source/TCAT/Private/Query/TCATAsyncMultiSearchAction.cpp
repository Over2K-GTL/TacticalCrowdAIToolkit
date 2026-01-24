// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Query/TCATAsyncMultiSearchAction.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Scene/TCATInfluenceComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

TArray<UTCATAsyncMultiSearchAction*> UTCATAsyncMultiSearchAction::ActionPool;

bool UTCATAsyncMultiSearchAction::TryResolveQueryCenter(FVector& OutCenter) const
{
	if (bUseWorldPosOverride)
	{
		OutCenter = WorldPosOverride;
		return true;
	}

	if (TargetComponent.IsValid())
	{
		OutCenter = TargetComponent->ResolveWorldLocation();
		return true;
	}

	if (!WorldContext)
	{
		return false;
	}

	if (const AActor* SourceActor = Cast<AActor>(WorldContext))
	{
		OutCenter = SourceActor->GetActorLocation();
		return true;
	}

	if (const UActorComponent* SourceActorComponent = Cast<UActorComponent>(WorldContext))
	{
		if (const AActor* Owner = SourceActorComponent->GetOwner())
		{
			OutCenter = Owner->GetActorLocation();
			return true;
		}
	}

	return false;
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchHighestValues(UObject* WorldContextObject,
    FName MapTag, UTCATInfluenceComponent* SourceComponent, float SearchRadius, int32 MaxResults,
    bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue, bool bUseRandomizedTiebreaker,
    ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float InfluenceHalfHeightOverride, bool bUseWorldPosOverride, const FVector& WorldPosToQueryOverride)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetComponent = SourceComponent;
    Action->TargetMapTag = MapTag;
    Action->SearchRadius = SearchRadius;
    Action->MaxResults = FMath::Max(1, MaxResults);

    Action->TargetCompareValue = 0.0f;
    Action->TargetCompareType = ETCATCompareType::Greater;

    Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
    Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    Action->bTraceVisibility = bTraceVisibility;
    Action->bIgnoreZValue = bIgnoreZValue;
    Action->bUseRandomizedTiebreaker = bUseRandomizedTiebreaker;

    Action->DistanceBiasType = DistanceBiasType;
    Action->DistanceBiasWeight = DistanceBiasWeight;
    Action->HalfHeightOverride = InfluenceHalfHeightOverride;
    Action->WorldPosOverride = WorldPosToQueryOverride;
    Action->bUseWorldPosOverride = bUseWorldPosOverride;

    Action->SelectedQueryType = ETCATQueryType::HighestValue;

    return Action;
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchLowestValues(UObject* WorldContextObject,
    FName MapTag, UTCATInfluenceComponent* SourceComponent, float SearchRadius, int32 MaxResults,
    bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue, bool bUseRandomizedTiebreaker,
    ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float InfluenceHalfHeightOverride, bool bUseWorldPosOverride, const FVector& WorldPosToQueryOverride)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetComponent = SourceComponent;
    Action->TargetMapTag = MapTag;
    Action->SearchRadius = SearchRadius;
    Action->MaxResults = FMath::Max(1, MaxResults);

    Action->TargetCompareValue = 0.0f;
    Action->TargetCompareType = ETCATCompareType::Greater;

    Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
    Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    Action->bTraceVisibility = bTraceVisibility;
    Action->bIgnoreZValue = bIgnoreZValue;
    Action->bUseRandomizedTiebreaker = bUseRandomizedTiebreaker;

    Action->DistanceBiasType = DistanceBiasType;
    Action->DistanceBiasWeight = DistanceBiasWeight;
    Action->HalfHeightOverride = InfluenceHalfHeightOverride;
    Action->WorldPosOverride = WorldPosToQueryOverride;
    Action->bUseWorldPosOverride = bUseWorldPosOverride;

    Action->SelectedQueryType = ETCATQueryType::LowestValue;

    return Action;
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchHighestValuesInCondition(UObject* WorldContextObject,
    FName MapTag, UTCATInfluenceComponent* SourceComponent, float SearchRadius, float CompareValue, ETCATCompareType CompareType, int32 MaxResults,
    bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue, bool bUseRandomizedTiebreaker,
    ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float InfluenceHalfHeightOverride, bool bUseWorldPosOverride, const FVector& WorldPosToQueryOverride)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetComponent = SourceComponent;
    Action->TargetMapTag = MapTag;
    Action->SearchRadius = SearchRadius;
    Action->MaxResults = FMath::Max(1, MaxResults);

    Action->TargetCompareValue = CompareValue;
    Action->TargetCompareType = CompareType;

    Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
    Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    Action->bTraceVisibility = bTraceVisibility;
    Action->bIgnoreZValue = bIgnoreZValue;
    Action->bUseRandomizedTiebreaker = bUseRandomizedTiebreaker;

    Action->DistanceBiasType = DistanceBiasType;
    Action->DistanceBiasWeight = DistanceBiasWeight;
    Action->HalfHeightOverride = InfluenceHalfHeightOverride;
    Action->WorldPosOverride = WorldPosToQueryOverride;
    Action->bUseWorldPosOverride = bUseWorldPosOverride;

    Action->SelectedQueryType = ETCATQueryType::HighestValueInCondition;

    return Action;
}

UTCATAsyncMultiSearchAction* UTCATAsyncMultiSearchAction::SearchLowestValuesInCondition(UObject* WorldContextObject,
    FName MapTag, UTCATInfluenceComponent* SourceComponent, float SearchRadius, float CompareValue, ETCATCompareType CompareType, int32 MaxResults,
    bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue, bool bUseRandomizedTiebreaker,
    ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float InfluenceHalfHeightOverride, bool bUseWorldPosOverride, const FVector& WorldPosToQueryOverride)
{
    UTCATAsyncMultiSearchAction* Action = GetOrCreateAction(WorldContextObject);

    Action->TargetComponent = SourceComponent;
    Action->TargetMapTag = MapTag;
    Action->SearchRadius = SearchRadius;
    Action->MaxResults = FMath::Max(1, MaxResults);

    Action->TargetCompareValue = CompareValue;
    Action->TargetCompareType = CompareType;

    Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
    Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    Action->bTraceVisibility = bTraceVisibility;
    Action->bIgnoreZValue = bIgnoreZValue;
    Action->bUseRandomizedTiebreaker = bUseRandomizedTiebreaker;

    Action->DistanceBiasType = DistanceBiasType;
    Action->DistanceBiasWeight = DistanceBiasWeight;
    Action->HalfHeightOverride = InfluenceHalfHeightOverride;
    Action->WorldPosOverride = WorldPosToQueryOverride;
    Action->bUseWorldPosOverride = bUseWorldPosOverride;

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

    if (!TargetComponent.IsValid())
    {
        AActor* SearchActor = Cast<AActor>(WorldContext);
        if (!SearchActor)
        {
            if (const UActorComponent* ContextComponent = Cast<UActorComponent>(WorldContext))
            {
                SearchActor = ContextComponent->GetOwner();
            }
        }

        if (SearchActor)
        {
            TargetComponent = SearchActor->FindComponentByClass<UTCATInfluenceComponent>();
        }
    }

    FVector FinalCenter;
    if (!TryResolveQueryCenter(FinalCenter))
    {
        OnFailed.Broadcast();
        FinishAndRelease();
        return;
    }

    ATCATInfluenceVolume* Volume = Subsystem->GetInfluenceVolume(TargetMapTag);

    const bool bComponentHasLayer = TargetComponent.IsValid() && TargetComponent->HasInfluenceLayer(TargetMapTag);
    float ResolvedHalfHeight = HalfHeightOverride;
    if (ResolvedHalfHeight < 0.0f)
    {
        if (bComponentHasLayer)
        {
            ResolvedHalfHeight = TargetComponent->GetInfluenceHalfHeight(TargetMapTag);
        }
        else
        {
            ResolvedHalfHeight = 0.0f;
        }
    }

    CachedResults.Empty(FMath::Max(MaxResults, 8));

    FTCATBatchQuery Query;
    Query.Type = SelectedQueryType;
    Query.MapTag = TargetMapTag;
    Query.SearchRadius = SearchRadius;
    Query.CompareValue = TargetCompareValue;
    Query.CompareType = TargetCompareType;

    Query.Center = FinalCenter;
    Query.InfluenceHalfHeight = ResolvedHalfHeight;

    Query.bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    Query.bTraceVisibility = bTraceVisibility;
    Query.bIgnoreZValue = bIgnoreZValue;
    Query.bUseRandomizedTiebreaker = bUseRandomizedTiebreaker;

    Query.DistanceBiasType = DistanceBiasType;
    Query.DistanceBiasWeight = DistanceBiasWeight;

    Query.MaxResults = FMath::Max(1, MaxResults);
    Query.RandomSeed = HashCombineFast(GetTypeHash(Query.MapTag), GetTypeHash(Query.Center));
    Query.RandomSeed = HashCombineFast(Query.RandomSeed, (uint32)GFrameCounter);

    Query.Curve = nullptr;
    Query.SelfRemovalFactor = 0.0f;
    Query.InfluenceRadius = 0.0f;

    if (bSubtractSelfInfluence && TargetComponent.IsValid() && Volume)
    {
        FTCATSelfInfluenceResult SelfResult = TargetComponent->GetSelfInfluenceResult(TargetMapTag, Volume);

        if (SelfResult.IsValid())
        {
            Query.Curve = SelfResult.Curve;
            Query.SelfRemovalFactor = SelfResult.FinalRemovalFactor;
            Query.InfluenceRadius = SelfResult.InfluenceRadius;
        }
    }

    Query.OnComplete = [this](const TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& Results)
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

#if ENABLE_VISUAL_LOG
    if (TargetComponent.IsValid())
    {
        TargetComponent->ApplyQueryDebugSettings(Query);
    }
#endif

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
    return Action;
}

void UTCATAsyncMultiSearchAction::FinishAndRelease()
{
    SetReadyToDestroy();
    
    OnSuccess.Clear();
    OnFailed.Clear();
    
    CachedResults.Reset();
    TargetComponent.Reset();
    WorldPosOverride = FVector::ZeroVector;
    bUseWorldPosOverride = false;
    
    ActionPool.Add(this);
}