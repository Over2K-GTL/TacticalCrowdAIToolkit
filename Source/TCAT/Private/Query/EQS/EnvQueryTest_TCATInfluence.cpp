// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Query/EQS/EnvQueryTest_TCATInfluence.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "Core/TCATSubsystem.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Scene/TCATInfluenceComponent.h"
#include "Core/TCATMathLibrary.h"
#include "Engine/World.h"

UEnvQueryTest_TCATInfluence::UEnvQueryTest_TCATInfluence()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
	SetWorkOnFloatValues(true);
}

void UEnvQueryTest_TCATInfluence::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();
	if (!QueryOwner || MapTag.IsNone()) return;

	UWorld* World = QueryOwner->GetWorld();
	if (!World) return;

	UTCATSubsystem* TCAT = World->GetSubsystem<UTCATSubsystem>();
	if (!TCAT) return;

	// Bind threshold values for filtering/scoring
	FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
	float MinThreshold = FloatValueMin.GetValue();

	FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);
	float MaxThreshold = FloatValueMax.GetValue();

	ATCATInfluenceVolume* Volume = TCAT->GetInfluenceVolume(MapTag);
	if (!Volume) return;

	// 2. Prepare Self-Influence subtraction if needed
	bool bDoSubtract = false;
	FVector QuerierLocation = FVector::ZeroVector;
	float SelfRadius = 0.f;
	FTCATSelfInfluenceResult SelfInfluence;

	if (AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get()))
	{
		if (UTCATInfluenceComponent* Comp = QuerierActor->FindComponentByClass<UTCATInfluenceComponent>())
		{
			QuerierLocation = Comp->ResolveWorldLocation();
			SelfRadius = Comp->GetRadius(MapTag);

			if (SelfRadius > KINDA_SMALL_NUMBER)
			{
				FTCATSelfInfluenceResult SelfResult = Comp->GetSelfInfluenceResult(MapTag, Volume);
				if (SelfResult.IsValid())
				{
					SelfInfluence = SelfResult;
					bDoSubtract = true;
				}
			}
		}
	}

	// Iterate Items
	const FVector GridOrigin = Volume->GetGridOrigin();
	const float GridSize = Volume->GetCellSize();
	const int32 Cols = Volume->GetColumns();
	const int32 Rows = Volume->GetRows();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FVector ItemLoc = GetItemLocation(QueryInstance, It.GetIndex());
		
		FIntPoint GridIdx = UTCATMathLibrary::WorldToGrid(ItemLoc, GridOrigin, GridSize, Cols, Rows);
		float Value = Volume->GetInfluenceFromGrid(MapTag, GridIdx.X, GridIdx.Y);

		// Apply subtraction
		if (bDoSubtract)
		{
			const float Dist = FVector::Dist(ItemLoc, QuerierLocation);
			const float Time = FMath::Clamp(Dist / SelfRadius, 0.0f, 1.0f);
			const float CurveVal = SelfInfluence.Curve->GetFloatValue(Time);
			const float SelfVal = CurveVal * SelfInfluence.FinalRemovalFactor;
			Value -= SelfVal;
		}

		It.SetScore(TestPurpose, FilterType, Value, MinThreshold, MaxThreshold);
	}
}

FText UEnvQueryTest_TCATInfluence::GetDescriptionTitle() const
{
	return FText::FromString(FString::Printf(TEXT("TCAT: %s"), *MapTag.ToString()));
}

FText UEnvQueryTest_TCATInfluence::GetDescriptionDetails() const
{
	return FText::FromString(TEXT("Score items based on TCAT Influence Map values"));
}