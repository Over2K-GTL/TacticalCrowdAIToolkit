// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Query/TCATAsyncQueryAction.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Scene/TCATInfluenceComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

TArray<UTCATAsyncSearchAction*> UTCATAsyncSearchAction::ActionPool;

bool UTCATAsyncSearchAction::TryResolveQueryCenter(FVector& OutCenter) const
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

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchHighestValue(UObject* WorldContextObject, FName MapTag,
UTCATInfluenceComponent* SourceComponent, float SearchRadius, bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue,
  ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float HalfHeightOverride, bool bUseWorldPosOverride,const FVector& WorldPosToQueryOverride)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);
    
	Action->TargetComponent = SourceComponent;
	Action->TargetMapTag = MapTag;
	Action->SearchRadius = SearchRadius;

	Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZValue;

	Action->DistanceBiasType = DistanceBiasType;
	Action->DistanceBiasWeight = DistanceBiasWeight;
	Action->HalfHeightOverride = HalfHeightOverride;
	Action->WorldPosOverride = WorldPosToQueryOverride;
	Action->bUseWorldPosOverride = bUseWorldPosOverride;

	Action->SelectedQueryType = ETCATQueryType::HighestValue;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchLowestValue(UObject* WorldContextObject, FName MapTag,
UTCATInfluenceComponent* SourceComponent, float SearchRadius, bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue,
  ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float HalfHeightOverride, bool bUseWorldPosOverride,const FVector& WorldPosToQueryOverride)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetComponent = SourceComponent;
	Action->TargetMapTag = MapTag;
	Action->SearchRadius = SearchRadius;

	Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZValue;

	Action->DistanceBiasType = DistanceBiasType;
	Action->DistanceBiasWeight = DistanceBiasWeight;
	Action->HalfHeightOverride = HalfHeightOverride;
	Action->WorldPosOverride = WorldPosToQueryOverride;
	Action->bUseWorldPosOverride = bUseWorldPosOverride;

	Action->SelectedQueryType = ETCATQueryType::LowestValue;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent,
float SearchRadius, float CompareValue, ETCATCompareType CompareType, bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue,
ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float HalfHeightOverride,  bool bUseWorldPosOverride,const FVector& WorldPosToQueryOverride)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);
    
	Action->TargetComponent = SourceComponent;
	Action->TargetMapTag = MapTag;
	Action->SearchRadius = SearchRadius;
    
	Action->TargetCompareValue = CompareValue;
	Action->TargetCompareType = CompareType;

	Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZValue;

	Action->DistanceBiasType = DistanceBiasType;
	Action->DistanceBiasWeight = DistanceBiasWeight;
	Action->HalfHeightOverride = HalfHeightOverride;
	Action->WorldPosOverride = WorldPosToQueryOverride;
	Action->bUseWorldPosOverride = bUseWorldPosOverride;
	
	Action->SelectedQueryType = ETCATQueryType::Condition;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::GetValueAtComponent(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent, bool bSubtractSelfInfluence,
	bool bIgnoreZValue, float HalfHeightOverride,  bool bUseWorldPosOverride,const FVector& WorldPosToQueryOverride)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);
    
	Action->TargetComponent = SourceComponent;
	Action->TargetMapTag = MapTag;

	Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
	Action->bIgnoreZValue = bIgnoreZValue;
	Action->HalfHeightOverride = HalfHeightOverride;
	Action->WorldPosOverride = WorldPosToQueryOverride;
	Action->bUseWorldPosOverride = bUseWorldPosOverride;
	Action->SelectedQueryType = ETCATQueryType::ValueAtPos;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::GetInfluenceGradient(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent, 
	float SearchRadius, float LookAheadDistance, bool bSubtractSelfInfluence, bool bIgnoreZValue, float HalfHeightOverride, bool bUseWorldPosOverride,const FVector& WorldPosToQueryOverride)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);
    
	Action->TargetComponent = SourceComponent;
	Action->TargetMapTag = MapTag;
	Action->SearchRadius = SearchRadius;

	// Reuse CompareValue
	Action->TargetCompareValue = LookAheadDistance; 

	Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
	Action->bIgnoreZValue = bIgnoreZValue;
	Action->HalfHeightOverride = HalfHeightOverride;
	Action->WorldPosOverride = WorldPosToQueryOverride;
	Action->bUseWorldPosOverride = bUseWorldPosOverride;
	Action->SelectedQueryType = ETCATQueryType::Gradient;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchHighestInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent,
float SearchRadius, float CompareValue, ETCATCompareType CompareType, bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue,
ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float HalfHeightOverride,  bool bUseWorldPosOverride,const FVector& WorldPosToQueryOverride)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);
    
	Action->TargetComponent = SourceComponent;
	Action->TargetMapTag = MapTag;
	Action->SearchRadius = SearchRadius;

	Action->TargetCompareValue = CompareValue;
	Action->TargetCompareType = CompareType;

	Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZValue;
	
	Action->DistanceBiasType = DistanceBiasType;
	Action->DistanceBiasWeight = DistanceBiasWeight;
	Action->HalfHeightOverride = HalfHeightOverride;
	Action->WorldPosOverride = WorldPosToQueryOverride;
	Action->bUseWorldPosOverride = bUseWorldPosOverride;

	Action->SelectedQueryType = ETCATQueryType::HighestValueInCondition;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchLowestInCondition(UObject* WorldContextObject, FName MapTag, UTCATInfluenceComponent* SourceComponent,
float SearchRadius, float CompareValue, ETCATCompareType CompareType, bool bSubtractSelfInfluence, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZValue,
ETCATDistanceBias DistanceBiasType, float DistanceBiasWeight, float HalfHeightOverride,  bool bUseWorldPosOverride,const FVector& WorldPosToQueryOverride)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);
    
	Action->TargetComponent = SourceComponent;
	Action->TargetMapTag = MapTag;
	Action->SearchRadius = SearchRadius;

	Action->TargetCompareValue = CompareValue;
	Action->TargetCompareType = CompareType;

	Action->bSubtractSelfInfluence = bSubtractSelfInfluence;
	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZValue;
	
	Action->DistanceBiasType = DistanceBiasType;
	Action->DistanceBiasWeight = DistanceBiasWeight;
	Action->HalfHeightOverride = HalfHeightOverride;
	Action->WorldPosOverride = WorldPosToQueryOverride;
	Action->bUseWorldPosOverride = bUseWorldPosOverride;

	Action->SelectedQueryType = ETCATQueryType::LowestValueInCondition;

	return Action;
}

void UTCATAsyncSearchAction::Activate()
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
			if (const UActorComponent* ContextComp = Cast<UActorComponent>(WorldContext))
			{
				SearchActor = ContextComp->GetOwner();
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
	// -------------------------------------------------------------------------
	// Data Extraction
	// -------------------------------------------------------------------------
	// Default Value
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
	
	FTCATBatchQuery Query;
	
    Query.Type = SelectedQueryType;
	Query.MapTag = TargetMapTag;
	Query.SearchRadius = SearchRadius;
	Query.CompareValue = TargetCompareValue;
	Query.CompareType = TargetCompareType;

	// Extract from Component
    Query.Center = FinalCenter;
	Query.InfluenceHalfHeight = ResolvedHalfHeight;
	
    Query.bExcludeUnreachableLocation = bExcludeUnreachableLocation;
    Query.bTraceVisibility = bTraceVisibility;
    Query.bIgnoreZValue = bIgnoreZValue;

	Query.DistanceBiasType = DistanceBiasType;
	Query.DistanceBiasWeight = DistanceBiasWeight;
	
	Query.MaxResults = 1;
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
	   if (Results.Num() > 0)
	   {
	      OnSuccess.Broadcast(Results[0].Value, Results[0].WorldPos);
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

void UTCATAsyncSearchAction::ResetPool()
{
	for (UTCATAsyncSearchAction* Action : ActionPool)
	{
		if (Action && Action->IsValidLowLevel())
		{
			Action->RemoveFromRoot();
			Action->SetReadyToDestroy();
		}
	}
	ActionPool.Empty();
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::GetOrCreateAction(UObject* WorldContextObject)
{
	UTCATAsyncSearchAction* Action = nullptr;
	if (ActionPool.Num() > 0) 
	{
		Action = ActionPool.Pop();
	}
	else
	{
		Action = NewObject<UTCATAsyncSearchAction>();
		Action->AddToRoot();
	}

	Action->WorldContext = WorldContextObject;
	return Action;
}

void UTCATAsyncSearchAction::FinishAndRelease()
{
	SetReadyToDestroy();
    
	OnSuccess.Clear();
	OnFailed.Clear();

	ActionPool.Add(this);
}
