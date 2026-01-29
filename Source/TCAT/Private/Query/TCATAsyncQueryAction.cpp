// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "Query/TCATAsyncQueryAction.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Scene/TCATInfluenceComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

TArray<UTCATAsyncSearchAction*> UTCATAsyncSearchAction::ActionPool;

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchHighestValue(UObject* WorldContextObject, FName MapTag,
	FVector InSearchCenter, float InSearchRadius, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZvalue, UCurveFloat* DistanceBiasCurve, float DistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetMapTag = MapTag;
	Action->SearchCenter = InSearchCenter;
	Action->SearchRadius = InSearchRadius;

	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZvalue;
	
	Action->DistanceBiasCurve = DistanceBiasCurve;
	Action->DistanceBiasWeight = DistanceBiasWeight;

	Action->TargetComponent = InInfluenceComponent;
	Action->SelectedQueryType = ETCATQueryType::HighestValue;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchLowestValue(UObject* WorldContextObject, FName MapTag,
	FVector InSearchCenter, float InSearchRadius, bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZvalue, UCurveFloat* DistanceBiasCurve, float DistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetMapTag = MapTag;
	Action->SearchCenter = InSearchCenter;
	Action->SearchRadius = InSearchRadius;

	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZvalue;
	
	Action->DistanceBiasCurve = DistanceBiasCurve;
	Action->DistanceBiasWeight = DistanceBiasWeight;
	  
	Action->TargetComponent = InInfluenceComponent;
	Action->SelectedQueryType = ETCATQueryType::LowestValue;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchCondition(UObject* WorldContextObject, FName MapTag,
	FVector InSearchCenter, float InSearchRadius, float CompareValue, ETCATCompareType CompareType,
	bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZvalue, UCurveFloat* DistanceBiasCurve, float DistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetMapTag = MapTag;
	Action->SearchCenter = InSearchCenter;
	Action->SearchRadius = InSearchRadius;
	
	Action->TargetCompareValue = CompareValue;
	Action->TargetCompareType = CompareType;

	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZvalue;
	
	Action->DistanceBiasCurve = DistanceBiasCurve;
	Action->DistanceBiasWeight = DistanceBiasWeight;

	Action->TargetComponent = InInfluenceComponent;

	Action->SelectedQueryType = ETCATQueryType::Condition;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::GetValueAtComponent(UObject* WorldContextObject, FName MapTag, FVector SamplePosition)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetMapTag = MapTag;
	Action->SearchCenter = SamplePosition;
	Action->SearchRadius = 0.0f;

	Action->bExcludeUnreachableLocation = false;
	Action->bTraceVisibility = false;
	Action->bIgnoreZValue = true;

	Action->DistanceBiasCurve = nullptr;
	Action->DistanceBiasWeight = 0.0f;

	Action->SelectedQueryType = ETCATQueryType::ValueAtPos;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::GetInfluenceGradient(UObject* WorldContextObject, FName MapTag, FVector InSearchCenter, float InSearchRadius, float LookAheadDistance, bool bIgnoreZvalue, UTCATInfluenceComponent* InInfluenceComponent)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetMapTag = MapTag;
	Action->SearchCenter = InSearchCenter;
	Action->SearchRadius = InSearchRadius;
	
	Action->TargetCompareValue = LookAheadDistance;
	Action->TargetCompareType = ETCATCompareType::Greater;

	Action->bExcludeUnreachableLocation = false;
	Action->bTraceVisibility = false;
	Action->bIgnoreZValue = bIgnoreZvalue;

	Action->DistanceBiasCurve = nullptr;
	Action->DistanceBiasWeight = 0.0f;

	Action->TargetComponent = InInfluenceComponent;

	Action->SelectedQueryType = ETCATQueryType::Gradient;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchHighestInCondition(UObject* WorldContextObject, FName MapTag, FVector InSearchCenter, float InSearchRadius, float CompareValue, ETCATCompareType CompareType,
	bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZvalue, UCurveFloat* DistanceBiasCurve, float DistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetMapTag = MapTag;
	Action->SearchCenter = InSearchCenter;
	Action->SearchRadius = InSearchRadius;
	
	Action->TargetCompareValue = CompareValue;
	Action->TargetCompareType = CompareType;

	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZvalue;
	
	Action->DistanceBiasCurve = DistanceBiasCurve;
	Action->DistanceBiasWeight = DistanceBiasWeight;

	Action->TargetComponent = InInfluenceComponent;
	
	Action->SelectedQueryType = ETCATQueryType::HighestValueInCondition;

	return Action;
}

UTCATAsyncSearchAction* UTCATAsyncSearchAction::SearchLowestInCondition(UObject* WorldContextObject, FName MapTag,
	FVector InSearchCenter, float InSearchRadius, float CompareValue, ETCATCompareType CompareType,
	bool bExcludeUnreachableLocation, bool bTraceVisibility, bool bIgnoreZvalue, UCurveFloat* DistanceBiasCurve, float DistanceBiasWeight, UTCATInfluenceComponent* InInfluenceComponent)
{
	UTCATAsyncSearchAction* Action = GetOrCreateAction(WorldContextObject);

	Action->TargetMapTag = MapTag;
	Action->SearchCenter = InSearchCenter;
	Action->SearchRadius = InSearchRadius;
	
	Action->TargetCompareValue = CompareValue;
	Action->TargetCompareType = CompareType;

	Action->bExcludeUnreachableLocation = bExcludeUnreachableLocation;
	Action->bTraceVisibility = bTraceVisibility;
	Action->bIgnoreZValue = bIgnoreZvalue;
	
	Action->DistanceBiasCurve = DistanceBiasCurve;
	Action->DistanceBiasWeight = DistanceBiasWeight;

	Action->TargetComponent = InInfluenceComponent;
	
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
	// -------------------------------------------------------------------------
	// Data Extraction
	// -------------------------------------------------------------------------
	FTCATBatchQuery Query;

    Query.Type = SelectedQueryType;
	Query.MapTag = TargetMapTag;
	
    Query.SearchCenter = SearchCenter;
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
	
	Query.MaxResults = 1;
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
	Action->TargetComponent.Reset();
	Action->TargetComponent = ResolveInfluenceComponentFromContext(WorldContextObject);

	Action->SearchCenter = FVector::ZeroVector;
	Action->SearchRadius = 0.0f;
	
	Action->TargetCompareValue = 0.0f;
	Action->TargetCompareType = ETCATCompareType::Greater;
	
	Action->bExcludeUnreachableLocation = false;
	Action->bTraceVisibility = false;
	
	Action->DistanceBiasCurve = nullptr;
	Action->DistanceBiasWeight = 0.0f;
	
	return Action;
}
void UTCATAsyncSearchAction::FinishAndRelease()
{
	SetReadyToDestroy();
    
	OnSuccess.Clear();
	OnFailed.Clear();

	WorldContext = nullptr;
	TargetComponent.Reset();
	SearchCenter = FVector::ZeroVector;
	SearchRadius = 0.0f;
	TargetCompareValue = 0.0f;
	TargetCompareType = ETCATCompareType::Greater;
	bExcludeUnreachableLocation = false;
	bTraceVisibility = false;
	DistanceBiasCurve = nullptr;
	DistanceBiasWeight = 0.0f;

	ActionPool.Add(this);
}

UTCATInfluenceComponent* UTCATAsyncSearchAction::ResolveInfluenceComponentFromContext(UObject* Context)
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