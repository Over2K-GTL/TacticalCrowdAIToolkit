// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Scene/TCATInfluenceComponent.h"
#include "TCAT.h"
#include "Core/TCATSubsystem.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Core/TCATSettings.h"
#include "Engine/World.h"
#include "Query/TCATQueryTypes.h"
#include "VisualLogger/VisualLogger.h"

DECLARE_CYCLE_STAT(TEXT("Component_TickComponent"), STAT_TCAT_TickInfluenceComponent, STATGROUP_TCAT);

//~=============================================================================
// Initialization
//~=============================================================================

UTCATInfluenceComponent::UTCATInfluenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

//~=============================================================================
// Component Lifecycle
//~=============================================================================

void UTCATInfluenceComponent::OnRegister()
{
	Super::OnRegister();
	
	RebuildSourceMap();
	
	if (UTCATSubsystem* Subsystem = GetTCATSubsystem())
	{
		Subsystem->RegisterComponent(this);
	}

	// Initialize previous location for motion tracking
	if (GetOwner())
	{
		PrevLocation = GetOwner()->GetActorLocation();
	}
}

void UTCATInfluenceComponent::OnUnregister()
{
	if (UTCATSubsystem* Subsystem = GetTCATSubsystem())
	{
		Subsystem->UnregisterComponent(this);
	}
	
	Super::OnUnregister();
}

#if WITH_EDITOR
void UTCATInfluenceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Rebuild source map when influence configuration changes in editor
	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(UTCATInfluenceComponent, InfluenceLayerMap) ||
			PropName == GET_MEMBER_NAME_CHECKED(FTCATInfluenceConfigEntry, MapTag) ||
			PropName == GET_MEMBER_NAME_CHECKED(FTCATInfluenceConfigEntry, FalloffCurve) ||
			PropName == GET_MEMBER_NAME_CHECKED(FTCATInfluenceSource, InfluenceRadius))
		{
			RebuildSourceMap();
		}
	}
}
#endif

//~=============================================================================
// Transform & Motion Tracking
//~=============================================================================

FVector UTCATInfluenceComponent::ResolveWorldLocation() const
{
	return GetComponentLocation();
}

void UTCATInfluenceComponent::RefreshMotionStatus()
{
	if (PrevFrameNumber == GFrameNumber)
	{
		return; // Already updated this frame
	}

	const float DeltaSeconds = FMath::Max(GetWorld()->GetDeltaSeconds(), KINDA_SMALL_NUMBER);
	
	Location = ResolveWorldLocation();
	Velocity = (Location - PrevLocation) / DeltaSeconds;
	Acceleration = (Velocity - PrevVelocity) / DeltaSeconds;

	/*const FQuat CurrentRotation = GetOwner() ? GetOwner()->GetActorQuat() : FQuat::Identity;
	DeltaRotation = CurrentRotation * PrevRotation.Inverse();
	
	DeltaRotation.ToAxisAndAngle(DeltaRotationAxis, DeltaRotationAngleRad);
	if (DeltaRotationAngleRad > UE_PI) DeltaRotationAngleRad -= UE_TWO_PI;
	else if (DeltaRotationAngleRad < -UE_PI) DeltaRotationAngleRad += UE_TWO_PI;*/

	DeltaRotation = FQuat::FindBetweenVectors(PrevVelocity, Velocity);

	DeltaRotation.ToAxisAndAngle(DeltaRotationAxis, DeltaRotationAngleRad);
	if (DeltaRotationAngleRad > UE_PI) DeltaRotationAngleRad -= UE_TWO_PI;
	else if (DeltaRotationAngleRad < -UE_PI) DeltaRotationAngleRad += UE_TWO_PI;

	PrevLocation = Location;
	PrevVelocity = Velocity;
	//PrevRotation = CurrentRotation;
	PrevFrameNumber = GFrameNumber;
}

//~=============================================================================
// Configuration & Source Map Management
//~=============================================================================

void UTCATInfluenceComponent::RebuildSourceMap()
{
	RuntimeSourceMap.Empty(InfluenceLayerMap.Num());

	UTCATSubsystem* Subsystem = GetTCATSubsystem();
    
	for (const FTCATInfluenceConfigEntry& Entry : InfluenceLayerMap)
	{
		// Skip invalid entries
		if (Entry.MapTag.IsNone())
		{
			continue;
		}
		
		FTCATInfluenceSource SourceData = Entry.SourceData;
		SourceData.LineOfSightOffset = this->LineOfSightOffset;
		
		// Convert curve pointer to curve ID for runtime use
		if (Subsystem && Entry.FalloffCurve)
		{
			SourceData.CurveTypeIndex = Subsystem->GetCurveID(Entry.FalloffCurve);
		}
		else
		{
			SourceData.CurveTypeIndex = 0; // Default to linear falloff
		}
		
		RuntimeSourceMap.Add(Entry.MapTag, SourceData);
	}
}

float UTCATInfluenceComponent::GetRadius(FName MapTag) const
{
	// Check if this is a direct source layer
	if (const FTCATInfluenceSource* Src = RuntimeSourceMap.Find(MapTag))
	{
		return Src->InfluenceRadius;
	}

	// Check if this is a composite layer with cached recipes
	if (const TArray<FCachedRemovalStep>* StepList = CachedRemovalStepsRuntime.Find(MapTag))
	{
		// Return radius from first contributing source layer
		for (const FCachedRemovalStep& Step : *StepList)
		{
			if (const FTCATInfluenceSource* OriginalSrc = RuntimeSourceMap.Find(Step.MySourceTag))
			{
				return OriginalSrc->InfluenceRadius;
			}
		}
	}

	return 0.0f;
}

//~=============================================================================
// Self-Influence & Reverse Calculation
//~=============================================================================

FTCATCurveCalculateInfo UTCATInfluenceComponent::GetReverseCalculationInfo(FName MapTag) const
{
	if (const FTCATCurveCalculateInfo* FoundInfo = CurveCalculateInfos.Find(MapTag))
	{
		return *FoundInfo;
	}
	
	return FTCATCurveCalculateInfo();
}

void UTCATInfluenceComponent::AddReverseCalculationInfo(FName MapTag, const FTCATCurveCalculateInfo& NewInfo)
{
	if (MapTag.IsNone())
	{
		return;
	}
	
	CurveCalculateInfos.Add(MapTag, NewInfo);
}

FTCATSelfInfluenceResult UTCATInfluenceComponent::GetSelfInfluenceResult(
	FName TargetMapTag,
	const ATCATInfluenceVolume* Volume) const
{
	FTCATSelfInfluenceResult Result;

	// Update cached recipes if volume changed
	if (Volume != CachedRecipeVolume.Get())
	{
		UpdateCachedRecipes(Volume);
	}

	// Priority 1: User-defined override
	if (const FTCATCurveCalculateInfo* ManualInfo = CurveCalculateInfos.Find(TargetMapTag))
	{
		Result.Curve = ManualInfo->Curve;
		float CalculatedStrength = ManualInfo->Strength;
		
		// Apply normalization if requested
		if (ManualInfo->bIsNormalize && Volume)
		{
			const float ScaleFactor = Volume->GetLayerScaleFactor(TargetMapTag);
			CalculatedStrength *= ScaleFactor;
		}
		
		Result.FinalRemovalFactor = CalculatedStrength;
		return Result;
	}

	// Priority 2: Cached recipe steps
	const TArray<FCachedRemovalStep>* Steps = CachedRemovalStepsRuntime.Find(TargetMapTag);
	if (!Steps)
	{
		return Result; // No data available
	}

	// Accumulate contributions from all source layers
	float TotalFactor = 0.0f;
	UCurveFloat* RepresentativeCurve = nullptr;
	float RepresentativeRadius = 0.0f;

	for (const FCachedRemovalStep& Step : *Steps)
	{
		// Find the source configuration entry
		const FTCATInfluenceConfigEntry* ConfigEntry = InfluenceLayerMap.FindByPredicate(
			[&](const FTCATInfluenceConfigEntry& Entry)
			{
				return Entry.MapTag == Step.MySourceTag;
			});
		
		if (!ConfigEntry)
		{
			continue;
		}

		// Calculate step factor (raw + dynamic scaling)
		float StepFactor = Step.RawCoefficient;
		if (!Step.DynamicScaleTag.IsNone() && Volume)
		{
			StepFactor += Step.NormCoefficient * Volume->GetLayerScaleFactor(Step.DynamicScaleTag);
		}

		// Accumulate weighted contribution
		TotalFactor += (ConfigEntry->SourceData.Strength * StepFactor);
		
		// Store representative curve/radius from first valid source
		if (!RepresentativeCurve)
		{
			RepresentativeCurve = ConfigEntry->FalloffCurve;
			RepresentativeRadius = ConfigEntry->SourceData.InfluenceRadius;
		}
	}

	Result.Curve = RepresentativeCurve;
	Result.FinalRemovalFactor = TotalFactor;
	Result.InfluenceRadius = RepresentativeRadius;
	
	return Result;
}

void UTCATInfluenceComponent::UpdateCachedRecipes(const ATCATInfluenceVolume* Volume) const
{
	CachedRecipeVolume = Volume;
	CachedRemovalStepsRuntime.Reset();
	
	if (!Volume)
	{
		return;
	}

	// Build cached removal steps from volume's baked recipes
	for (const auto& SourceEntry : RuntimeSourceMap)
	{
		const FName& MySourceTag = SourceEntry.Key;
		const auto* BakedRecipes = Volume->GetBakedRecipesForSource(MySourceTag);
		
		if (!BakedRecipes)
		{
			continue;
		}

		// Process each recipe for this source
		for (const auto& RecipePair : *BakedRecipes)
		{
			const FName& TargetMapTag = RecipePair.Key;
			const FTCATSelfInfluenceRecipe& Recipe = RecipePair.Value;

			// Skip non-reversible recipes
			if (!Recipe.bIsReversible)
			{
				continue;
			}

			// Create cached step
			FCachedRemovalStep Step;
			Step.MySourceTag = MySourceTag;
			Step.RawCoefficient = Recipe.RawCoefficient;
			Step.NormCoefficient = Recipe.NormCoefficient;
			Step.DynamicScaleTag = Recipe.DynamicScaleLayerTag;

			CachedRemovalStepsRuntime.FindOrAdd(TargetMapTag).Add(Step);
		}
	}
}

//~=============================================================================
// Debug - Visual Logger
//~=============================================================================

#if ENABLE_VISUAL_LOG

void UTCATInfluenceComponent::ApplyQueryDebugSettings(FTCATBatchQuery& Query) const
{
	if (!bDebugMyQueries || !FVisualLogger::IsRecording())
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Enable debug visualization
	Query.DebugInfo.bEnabled = true;
	Query.DebugInfo.DebugOwner = Owner;
	
	// Generate unique color per component based on query seed and actor ID
	const uint32 ColorSeed = HashCombineFast(Query.RandomSeed, static_cast<uint32>(Owner->GetUniqueID()));
	const uint8 Hue = static_cast<uint8>(ColorSeed & 0xFF);
	Query.DebugInfo.BaseColor = FLinearColor::MakeFromHSV8(Hue, 200, 255);
	
	// Apply sample stride
	Query.DebugInfo.SampleStride = FMath::Max(1, DebugQueryStride);

	// Stack queries vertically for better visibility
	const float HeightStep = FMath::Max(5.0f, DebugQueryHeightStep);
	Query.DebugInfo.HeightOffset = HeightStep * GetNextDebugQueryLayer();
}

int32 UTCATInfluenceComponent::GetNextDebugQueryLayer() const
{
	// Reset counter on new frame
	if (LastDebugQueryFrame != GFrameNumber)
	{
		LastDebugQueryFrame = GFrameNumber;
		DebugQueryCounter = 0;
	}

	return DebugQueryCounter++;
}

#endif // ENABLE_VISUAL_LOG

void UTCATInfluenceComponent::VLogInfluence() const
{
	static const FName LogCategory = TEXT("TCATInfluenceSources");

	const FVector Center = ResolveWorldLocation();

	for (const auto& Pair : RuntimeSourceMap)
	{
		const FName& MapTag = Pair.Key;
		const FTCATInfluenceSource& Src = Pair.Value;

		// Skip near-zero strength sources
		if (FMath::IsNearlyZero(Src.Strength))
		{
			continue;
		}

		const float Radius = Src.InfluenceRadius;
		const float Strength = Src.Strength;
		const FVector VelocityVec = GetCurrentVelocity();
		const FVector AccelVec = GetCurrentAcceleration();
		const FVector RotAxis = GetDeltaRotationAxis();
		const float RotAngle = GetDeltaRotationAngleRad();
		const FVector ToPredicted = FVector(GetPredictedLocation()) - Center;
		
		// Color coding: Blue for positive influence, Red for negative
		const FLinearColor BaseColor = (Strength > 0.0f) ? FLinearColor::Blue : FLinearColor::Red;
		const float Intensity = FMath::Clamp(FMath::Abs(Strength), 0.3f, 1.0f);
		const FColor FinalColor = (BaseColor * Intensity).ToFColor(true);

		// Draw influence sphere
		UE_VLOG_SPHERE(this, LogCategory, Log, Center, Radius, FinalColor, 
			TEXT("[%s] R=%.0f S=%.1f, RotAxis={%.2f, %.2f, %.2f}, RotAngle=%.2f"), 
			*MapTag.ToString(), Radius, Strength, RotAxis.X, RotAxis.Y, RotAxis.Z, RotAngle);

		// Draw velocity arrow (black)
		UE_VLOG_ARROW(this, LogCategory, Log, Center, Center + VelocityVec,
			FLinearColor::Black.ToFColor(true), 
			TEXT("Velocity: {%.2f, %.2f, %.2f}, Size: %.2f"), 
			VelocityVec.X, VelocityVec.Y, VelocityVec.Z, VelocityVec.Size());

		// Draw acceleration arrow (red)
		UE_VLOG_ARROW(this, LogCategory, Log, Center, Center + AccelVec,
			FLinearColor::Red.ToFColor(true), 
			TEXT("Acceleration: {%.2f, %.2f, %.2f}, Size: %.2f"), 
			AccelVec.X, AccelVec.Y, AccelVec.Z, AccelVec.Size());
		
		// Draw predicted location arrow (green)
		UE_VLOG_ARROW(this, LogCategory, Log, Center, Center + ToPredicted,
			FLinearColor::Green.ToFColor(true), 
			TEXT("To Predicted: {%.2f, %.2f, %.2f}, Size: %.2f"), 
			ToPredicted.X, ToPredicted.Y, ToPredicted.Z, ToPredicted.Size());
	}
}

//~=============================================================================
// Utility Functions
//~=============================================================================

TArray<FString> UTCATInfluenceComponent::GetBaseTagOptions() const
{
	return UTCATSettings::GetBaseTagOptions();
}

UTCATSubsystem* UTCATInfluenceComponent::GetTCATSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTCATSubsystem>() : nullptr;
}