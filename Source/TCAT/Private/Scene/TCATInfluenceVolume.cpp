// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Scene/TCATInfluenceVolume.h"
#include "DrawDebugHelpers.h"
#include "GlobalShader.h"
#include "TCAT.h"
#include "TextureResource.h"
#include "Components/BrushComponent.h"
#include "Core/TCATMathLibrary.h"
#include "Core/TCATSettings.h"
#include "Core/TCATSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "Scene/TCATInfluenceComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"

using namespace TCATMapConstant;

DECLARE_CYCLE_STAT(TEXT("Volume_UpdateInfos"), STAT_TCAT_UpdateVolumeInfos, STATGROUP_TCAT);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total_Influence_Sources"), STAT_TCAT_SourceCount, STATGROUP_TCAT);
DECLARE_MEMORY_STAT(TEXT("Influence_Grid_Memory"), STAT_TCAT_Grid_Mem, STATGROUP_TCAT);

static TAutoConsoleVariable<int32> CVarTCATLogStride(
	TEXT("TCAT.Debug.LayerLogStride"),
	4,
	TEXT("Step size for Visual Logger text rendering to improve performance."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarTCATTextOffset(
	TEXT("TCAT.Debug.LayerTextOffset"),
	50.0f,
	TEXT("Z-Offset for Visual Logger text to prevent clipping."),
	ECVF_Cheat);

ATCATInfluenceVolume::ATCATInfluenceVolume()
{
	PrimaryActorTick.bCanEverTick = true;

	GetBrushComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	GetBrushComponent()->Mobility = EComponentMobility::Static;
	
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif
}

float ATCATInfluenceVolume::GetInfluenceFromGrid(FName LayerTag, int32 InX, int32 InY) const
{
	const FTCATGridResource* TargetResource = GetLayerResource(LayerTag);
	if (!TargetResource || TargetResource->Grid.Num() == 0) { return 0.0f; }
	
	const int32 Index = InY * GridResolution.X + InX;
	if (TargetResource->Grid.IsValidIndex(Index))
	{
		return TargetResource->Grid[Index];
	}

	return 0.0f;
}

FVector ATCATInfluenceVolume::GetGridOrigin() const
{
	return FVector(CachedBounds.Min.X, CachedBounds.Min.Y, CachedBounds.GetCenter().Z);
}

float ATCATInfluenceVolume::GetGridHeightIndex(FIntPoint CellIndex) const
{
	int Index = CellIndex.Y * GridResolution.X + CellIndex.X;
	return HeightResource.Grid[Index];
}

float ATCATInfluenceVolume::GetGridHeightWorldPos(FVector WorldPos) const
{
	FIntPoint GridIndex = UTCATMathLibrary::WorldToGrid(WorldPos, GetGridOrigin(), CellSize, GridResolution.X, GridResolution.Y);
	return GetGridHeightIndex(GridIndex);
}

int32 ATCATInfluenceVolume::GetProjectionMask(FName LayerTag) const
{
	if (const FTCATBaseLayerConfig* Config = CachedBaseLayerMap.Find(LayerTag))
	{
		return Config->ProjectionMask;
	}
	return 0; // Default: No Projection
}

const FTCATGridResource* ATCATInfluenceVolume::GetLayerResource(FName LayerTag) const
{
	if (const FTCATGridResource* FoundLayer = InfluenceLayers.Find(LayerTag))
	{
		return FoundLayer;
	}
	return nullptr;
}

void ATCATInfluenceVolume::GetLayerMinMax(FName MapTag, float& OutMin, float& OutMax)
{
	if (const FTCATGridResource* FoundLayer = InfluenceLayers.Find(MapTag))
	{
		OutMin = FoundLayer->MinMapValue;
		OutMax = FoundLayer->MaxMapValue;
	}
}

void ATCATInfluenceVolume::BakeHeightMap()
{
	bIsHeightBaked = true;
	
	UpdateGridSize();

	HeightMap.Bake(this, CellSize, GridResolution);
	
#if WITH_EDITOR
	if (HeightMap.bDrawHeight)
	{
		HeightMap.FlushAndRedraw(this);
	}
#endif
}

#if WITH_EDITOR
void ATCATInfluenceVolume::SyncLayersFromComponents()
{
	UTCATSubsystem* Subsystem = GetTCATSubsystem();
	if (!Subsystem)
	{
		return;
	}
	
	UpdateGridSize(); 
	
	TArray<UTCATInfluenceComponent*> OverlappingComponents;
	Subsystem->GetAllComponentsInBounds(CachedBounds, OverlappingComponents);
	
	TSet<FName> NewTags;
	for (UTCATInfluenceComponent* Comp : OverlappingComponents)
	{
		if (!IsValid(Comp)) continue;

		for (const auto& Layer : Comp->GetInfluenceLayers())
		{
			if (Layer.MapTag.IsNone()) continue;
			
			if (!CachedBaseLayerMap.Contains(Layer.MapTag))
			{
				NewTags.Add(Layer.MapTag);
			}
		}
	}
	
	if (NewTags.Num() > 0)
	{
		BatchEnsureBaseLayers(NewTags);
		UE_LOG(LogTCAT, Log, TEXT("[%s] Synced %d new layers from components."), *GetName(), NewTags.Num());
	}
	else
	{
		UE_LOG(LogTCAT, Log, TEXT("[%s] All layers are already synced."), *GetName());
	}
}

void ATCATInfluenceVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
		
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Handle debug draw toggle for height map
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ATCATInfluenceVolume, HeightMap) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FTCATHeightMapModule, HeightToMark) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FTCATHeightMapModule, bDrawHeight))
	{
		FlushPersistentDebugLines(GetWorld());
		
		if (HeightMap.bDrawHeight)
		{
			HeightMap.FlushAndRedraw(this); 
		}
	}
	RebuildRuntimeMaps();
}
#endif

void ATCATInfluenceVolume::BeginPlay()
{
	Super::BeginPlay();
	
	RebuildRuntimeMaps();
	InitializeResources();

	if (!bIsHeightBaked)
	{
		BakeHeightMap();
	}
	
	if (UTCATSubsystem* Subsystem = GetTCATSubsystem())
	{
		Subsystem->RegisterVolume(this);
	}
}

void ATCATInfluenceVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTCATSubsystem* Subsystem = GetTCATSubsystem())
	{
		Subsystem->UnregisterVolume(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ATCATInfluenceVolume::BeginDestroy()
{
	if (HeightResource.RenderTarget)
	{
		HeightResource.Release();		
	}
		
	for (auto& LayerPair : InfluenceLayers)
	{
		LayerPair.Value.Release();
	}

	InfluenceLayers.Empty();
	LayerSourcesMap.Empty();
	
	Super::BeginDestroy();
}

void ATCATInfluenceVolume::UpdateVolumeInfos()
{
	SCOPE_CYCLE_COUNTER(STAT_TCAT_UpdateVolumeInfos);
	TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_UpdateVolumeInfos);

	// Sync Resolution and Resources
	const FIntPoint OldResolution = GridResolution;
	UpdateGridSize();
	
	const bool bResolutionChanged = (OldResolution != GridResolution);
	const bool bNeedsInitialization = (GridResolution.X * GridResolution.Y != HeightResource.Grid.Num());
	
	if (bResolutionChanged || bNeedsInitialization)
	{
		InitializeResources();
		if (!bIsHeightBaked)
		{
			BakeHeightMap();
		}
	}

	// Refresh Unit Sources (Only for Raw InfluenceTags)
	RefreshSources();

	// Debug Visualization and Memory Stats
	DebugDrawGrid();
	
	UpdateMemoryStats();
}

void ATCATInfluenceVolume::RefreshSources()
{
    UTCATSubsystem* Subsystem = GetTCATSubsystem();
    if (!Subsystem) { return; }

	const FBox VolumeBox = CachedBounds;
	
    TArray<UTCATInfluenceComponent*> OverlappingComponents;
    Subsystem->GetAllComponentsInBounds(VolumeBox, OverlappingComponents);

	const float CurrentDeltaSeconds = GetWorld()->GetDeltaSeconds();
	
	for (const auto& Pair : CachedBaseLayerMap)
	{
		const FName& Tag = Pair.Key;
		LayerSourcesMap.FindOrAdd(Tag).Reset();
		LayerSourcesWithOwners.FindOrAdd(Tag).Reset();
		
		if (IsPossiblePrediction())
		{
			if (FTCATGridResource* LayerRes = InfluenceLayers.Find(Tag))
			{
				/*
				If the delta time difference between the previous frame and the current frame is more than "PredictionCorrectionThreshold", use the smaller value between latency and the previous prediction time.
					- If increased: Use Prediction time (= The smaller of the two)
					- If decreased: Use the smaller of the two
						Situation where latency is chosen: Increased delta time persisted for 3 frames or more. If frame latency is 2 frames, it eases somewhat at frame 4 after the delta time decreases. Eases immediately starting from frame 5.
						Situation where PrevPredictionTime is chosen:
							Increased delta time maintained for 2 frames: If frame latency is 2 frames, relief begins immediately from frame 4 after delta time decreases.
							Increased delta time maintained for 1 frame: If frame latency is 2 frames, relief begins immediately from frame 3 after delta time decreases.
				*/
				const float PrevPredictionTime = LayerRes->AsyncRingBuffer.GetLatestReadResourcePredictionTime();
				const float Latency = LayerRes->AsyncRingBuffer.GetLatestWriteReadLatency();
				const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
				float PredictionTime = 0.0f;

				if (LastDeltaSeconds * PredictionCorrectionThreshold < DeltaSeconds || LastDeltaSeconds > DeltaSeconds * PredictionCorrectionThreshold)
				{
					PredictionTime = FMath::Min(PrevPredictionTime, Latency);
				}
				else
				{
					PredictionTime = Latency;
				}

				TagToPredictionInfo.Add(Tag, { PrevPredictionTime, PredictionTime });
			}
			else { TagToPredictionInfo.Add(Tag, { 0.0f, 0.0f }); }
		}
		else { TagToPredictionInfo.Add(Tag, { 0.0f, 0.0f }); }
	}

	LastDeltaSeconds = CurrentDeltaSeconds;
	
	bool bAddedLayerDuringRefresh = false;

	for (UTCATInfluenceComponent* Comp : OverlappingComponents)
	{
		if (!Comp) { continue; }

		Comp->RefreshMotionStatus();

		const FVector3f CurLocation = static_cast<FVector3f>(Comp->GetCurrentLocation());
		const FVector3f CurVelocity = static_cast<FVector3f>(Comp->GetCurrentVelocity());
		const FVector3f CurAcceleration = static_cast<FVector3f>(Comp->GetCurrentAcceleration());
		const FVector RotationAxis = Comp->GetDeltaRotationAxis();
		const float DeltaAngleRad = Comp->GetDeltaRotationAngleRad();

		// variables for location prediction when rotating
		bool bIsRotating = false;
		FVector ParallelVelocityToRotationAxis = FVector::ZeroVector;
		FVector PerpVelocityToRotationAxis = static_cast<FVector>(CurVelocity);
		if (FMath::Abs(DeltaAngleRad) > 0.0001f)
		{
			bIsRotating = true;

			float V_dot_Axis = FVector::DotProduct(static_cast<FVector>(CurVelocity), RotationAxis);
			ParallelVelocityToRotationAxis = RotationAxis * V_dot_Axis;
			PerpVelocityToRotationAxis = static_cast<FVector>(CurVelocity) - ParallelVelocityToRotationAxis;
		}

		for (const auto& InfluenceLayer : Comp->GetInfluenceLayers())
		{
			const FName& CompLayerTag = InfluenceLayer.MapTag;

			if (const FTCATPredictionInfo* FoundPredInfo = TagToPredictionInfo.Find(CompLayerTag))
			{
				FTCATInfluenceSource NewSource = Comp->GetSource(CompLayerTag);
				const double PredictionTime = static_cast<double>(FoundPredInfo->PredictionTime);
				checkf(PredictionTime >= 0.0, TEXT("PredictionTime should be non-negative"));

				if (PredictionTime == 0.0)
				{
					NewSource.WorldLocation = CurLocation;
				}
				else
				{
					uint32 LatestWriteReadLatencyFrames = 0;
					if (FTCATGridResource* LayerRes = InfluenceLayers.Find(CompLayerTag))
					{
						LatestWriteReadLatencyFrames = LayerRes->AsyncRingBuffer.GetLatestWriteReadLatencyFrames();
					}

					const double PredictionDeltaTime = LatestWriteReadLatencyFrames != 0 ? PredictionTime / static_cast<double>(LatestWriteReadLatencyFrames) : 0.0;
					checkf((PredictionDeltaTime != 0.0 && LatestWriteReadLatencyFrames == 0) == false, TEXT("PredictionDeltaTime should be zero if LatestWriteReadLatencyFrames is zero"));

					if (bIsRotating)
					{
						// --- A. Parallel component: Uniform linear motion ---
						FVector DisplacementPara = ParallelVelocityToRotationAxis * PredictionTime;

						// --- B. Perpendicular component: Modify the CRTV to fit the Semi-Implicit Euler scheme ---

						// B-1. Calculate the ratio at which the vector's size increases over LatestWriteReadLatencyFrames When DeltaTime is 1.0 seconds: sin(n * delta / 2) / sin(delta / 2)
						float SinHalfDeltaTime, CosHalfDeltaTime;
						FMath::SinCos(&SinHalfDeltaTime, &CosHalfDeltaTime, DeltaAngleRad * 0.5f);

						float SinNDeltaTime, CosNDeltaTime;
						FMath::SinCos(&SinNDeltaTime, &CosNDeltaTime, LatestWriteReadLatencyFrames * DeltaAngleRad * 0.5f);

						float Ratio = SinNDeltaTime / SinHalfDeltaTime;

						// B-2. Calculate rotation: (n + 1) * delta / 2
						// Rotate the perpendicular velocity vector (PerpVelocityToRotationAxis) around the rotation axis (RotationAxis) by PhaseAngleRad
						float PhaseAngleRad = (LatestWriteReadLatencyFrames + 1) * DeltaAngleRad * 0.5f;
						FVector RotatedPerpVelocity = PerpVelocityToRotationAxis.RotateAngleAxis(FMath::RadiansToDegrees(PhaseAngleRad), RotationAxis);

						// B-3. Final displacement of the perpendicular component
						FVector DisplacementPerp = RotatedPerpVelocity * (PredictionDeltaTime * Ratio);

						// --- Final predicted position ---
						NewSource.WorldLocation = CurLocation + static_cast<FVector3f>(DisplacementPara + DisplacementPerp);
					}
					else
					{
						// Modify the Taylor series to fit the Semi-Implicit Euler scheme
						NewSource.WorldLocation = CurLocation + (CurVelocity * PredictionTime)
							+ (AccelerationPredictionFactor * CurAcceleration * FMath::Square(PredictionDeltaTime) * LatestWriteReadLatencyFrames * (LatestWriteReadLatencyFrames + 1) * 0.5f);
					}
				}
				
#if !UE_BUILD_SHIPPING
				// For Visual Debugging
				Comp->SetPredictedLocation(NewSource.WorldLocation);
#endif

				NewSource.CurveTypeIndex = Subsystem->GetCurveID(InfluenceLayer.FalloffCurve);
				LayerSourcesMap[CompLayerTag].Add(NewSource);

				FTCATInfluenceSourceWithOwner SourceWithOwner;
				SourceWithOwner.Source = NewSource;
				SourceWithOwner.OwnerComponent = Comp;
				LayerSourcesWithOwners[CompLayerTag].Add(SourceWithOwner);
			}
		}
	}
	
    TArray<FTransientSourceWrapper> AllTransients;
    Subsystem->GetAllTransientSourcesInBounds(VolumeBox, AllTransients);
    for (const FTransientSourceWrapper& Wrapper : AllTransients)
    {
        if (LayerSourcesMap.Contains(Wrapper.MapTag))
        {
            FTCATInfluenceSource NewSource = Wrapper.Data;
            NewSource.CurveTypeIndex = Subsystem->GetCurveID(Wrapper.CurveAsset);
            LayerSourcesMap[Wrapper.MapTag].Add(NewSource);
        }
    }
	if (bAddedLayerDuringRefresh)
	{
		RebuildInfluenceRecipes();
	}

}

void ATCATInfluenceVolume::InitializeResources()
{
	UpdateGridSize();
	
	if (HeightResource.Grid.Num() != GridResolution.X * GridResolution.Y)
	{
		HeightResource.Resize(GridResolution.Y, GridResolution.X, this, TEXT("Height"));
		bIsHeightBaked = false;
	}

	TSet<FName> AllRequiredTags;
	for (const auto& Pair : CachedBaseLayerMap) { AllRequiredTags.Add(Pair.Key); }
	for (const FTCATCompositeLayerConfig& CompositeLayer : CompositeLayers) { AllRequiredTags.Add(CompositeLayer.CompositeLayerTag); }

	for (const FName& Tag : AllRequiredTags)
	{
		FTCATGridResource& LayerRes = InfluenceLayers.FindOrAdd(Tag);
		LayerRes.Resize(GridResolution.Y, GridResolution.X, this, Tag);
	}

	TArray<FName> CurrentKeys;
	InfluenceLayers.GetKeys(CurrentKeys);
	for (const FName& Key : CurrentKeys)
	{
		if (!AllRequiredTags.Contains(Key))
		{
			InfluenceLayers.Remove(Key);
		}
	}
}

void ATCATInfluenceVolume::UpdateGridSize()
{
	const FBox WorldBounds = GetComponentsBoundingBox(true);

	if (!WorldBounds.IsValid)
	{
		CachedBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
		GridResolution = FIntPoint(1, 1);
		return;
	}

	CachedBounds = WorldBounds;
	const FVector BoundsSize = CachedBounds.GetSize();

	int32 ResX = FMath::Max(1, FMath::FloorToInt(BoundsSize.X / CellSize));
	int32 ResY = FMath::Max(1, FMath::FloorToInt(BoundsSize.Y / CellSize));

	int32 MaxRes = 2048; // Default fallback
	if (UTCATSubsystem* Subsystem = GetTCATSubsystem())
	{
		MaxRes = Subsystem->GetMaxMapResolution();
	}

	bool bClamped = false;
	float ScaleFactor = 1.0f;

	// Scale based on the larger axis
	int32 MaxAxis = FMath::Max(ResX, ResY);
	if (MaxAxis > MaxRes)
	{
		ScaleFactor = static_cast<float>(MaxAxis) / static_cast<float>(MaxRes);
		bClamped = true;
	}

	if (bClamped)
	{
		// Adjust Resolution and CellSize
		GridResolution.X = FMath::CeilToInt(ResX / ScaleFactor);
		GridResolution.Y = FMath::CeilToInt(ResY / ScaleFactor);
        
		float NewCellSize = CellSize * ScaleFactor;
        
		UE_LOG(LogTCAT, Warning, TEXT("[%s] Map Resolution (%dx%d) exceeds Limit (%d). Adjusting CellSize: %.2f -> %.2f"), 
			*GetName(), ResX, ResY, MaxRes, CellSize, NewCellSize);

		CellSize = NewCellSize; // Update actual CellSize
	}
	else
	{
		GridResolution.X = ResX;
		GridResolution.Y = ResY;
	}

	GridResolutionDisplay = FString::Printf(TEXT("%d x %d"), GridResolution.X, GridResolution.Y);
}

void ATCATInfluenceVolume::UpdateMemoryStats()
{
	SIZE_T TotalGridMem = HeightResource.Grid.GetAllocatedSize();
    
	for (auto& Layer : InfluenceLayers)
	{
		TotalGridMem += Layer.Value.Grid.GetAllocatedSize();
	}

	SET_MEMORY_STAT(STAT_TCAT_Grid_Mem, TotalGridMem);
}

void ATCATInfluenceVolume::RebuildRuntimeMaps()
{
	// 1. Base Layers
	CachedBaseLayerMap.Empty();
	CachedBaseLayerMap.Reserve(BaseLayerConfigs.Num());

	// 2. Build Debug Settings Map (For Visualization - Base + Composite)
	CachedDebugSettingsMap.Empty();
	CachedDebugSettingsMap.Reserve(BaseLayerConfigs.Num() + CompositeLayers.Num());
	
	for (const auto& Config : BaseLayerConfigs)
	{
		if (!Config.BaseLayerTag.IsNone())
		{
			CachedBaseLayerMap.Add(Config.BaseLayerTag, Config);
			CachedDebugSettingsMap.Add(Config.BaseLayerTag, Config.DebugSettings); // Cache Debug
		}
	}

	// 2. Composite Layers
	for (const auto& Config : CompositeLayers)
	{
		if (!Config.CompositeLayerTag.IsNone())
		{
			CachedDebugSettingsMap.Add(Config.CompositeLayerTag, Config.DebugSettings); // Cache Debug
		}
	}

	RebuildInfluenceRecipes();
}

float ATCATInfluenceVolume::GetLayerScaleFactor(FName LayerTag) const
{
	if (const FTCATGridResource* FoundLayer = InfluenceLayers.Find(LayerTag))
	{
		float Range = FoundLayer->MaxMapValue - FoundLayer->MinMapValue;
		if (Range > KINDA_SMALL_NUMBER)
		{
			return 1.0f / Range;
		}
	}
	return 0.0f;
}

const TMap<FName, FTCATSelfInfluenceRecipe>* ATCATInfluenceVolume::GetBakedRecipesForSource(FName SourceTag) const
{
	return CachedInfluenceRecipes.Find(SourceTag);
}

void ATCATInfluenceVolume::RebuildInfluenceRecipes()
{
    CachedInfluenceRecipes.Empty();

	// 1. Base Layer(Raw map) Initialization
	// Strength = 1.0 (Raw -> Raw)
    for (const auto& Pair : CachedBaseLayerMap)
    {
       const FName& BaseTag = Pair.Key;
       FTCATSelfInfluenceRecipe IdentityRecipe;
       IdentityRecipe.bIsReversible = true;
       IdentityRecipe.RawCoefficient = 1.0f;
       
       // TMap<Source, TMap<Target, Recipe>>
       CachedInfluenceRecipes.FindOrAdd(BaseTag).Add(BaseTag, IdentityRecipe);
    }

    // 2. Composite Layer Initialization
    for (const FTCATCompositeLayerConfig& CompLayer : CompositeLayers)
    {
       const FName& TargetTag = CompLayer.CompositeLayerTag;

    	if (!CompLayer.LogicAsset)
    	{
    		continue; 
    	}
    	
	   // Simulation State : Tracking Each Source's Influence(Contribution) on Current Composite Map
       struct FSourceState
       {
          float RawSlope = 0.0f;  // Range Independent Coefficient
          float NormSlope = 0.0f; // Range Dependent Coefficient(Normalize)
          FName DynamicScaleTag = NAME_None; // Range from DynamicScaleTag(Layer Tag)
          bool bApproximate = false; // Using Clamp
          bool bInvalid = false;     // Using Multiply/Divide etc...
       };

       TMap<FName, FSourceState> SimulationState;

       // Operations Simulation
       for (const FTCATCompositeOperation& Op : CompLayer.LogicAsset->Operations)
       {
          // 1. Binary Ops (Input Exists)
          if (Op.Operation != ETCATCompositeOp::Invert)
          {
             if (!CachedBaseLayerMap.Contains(Op.InputLayerTag)) continue;

             FSourceState& State = SimulationState.FindOrAdd(Op.InputLayerTag);
             if (State.bInvalid) continue;

             if (Op.bClampInput) State.bApproximate = true;

             if (Op.bNormalizeInput)
             {
                State.NormSlope += Op.Strength;
				// Update Normalize Target Map just once, the first one
                if (State.DynamicScaleTag.IsNone()) State.DynamicScaleTag = Op.InputLayerTag;
             }
             else
             {
                State.RawSlope += Op.Strength;
             }

             if (Op.Operation == ETCATCompositeOp::Subtract)
             {
                 if (Op.bNormalizeInput) State.NormSlope -= (2.0f * Op.Strength);
                 else State.RawSlope -= (2.0f * Op.Strength);
             }
             else if (Op.Operation == ETCATCompositeOp::Multiply || Op.Operation == ETCATCompositeOp::Divide)
             {
                 State.bInvalid = true; 
             }
          }
          // 2. Unary Ops (Invert)
          else if (Op.Operation == ETCATCompositeOp::Invert)
          {
             float Factor = -1.0f * Op.Strength;
             for (auto& SimPair : SimulationState)
             {
                if (!SimPair.Value.bInvalid)
                {
                   SimPair.Value.RawSlope *= Factor;
                   SimPair.Value.NormSlope *= Factor;
                }
             }
          }
       }

       // 3. Commit
       for (const auto& SimPair : SimulationState)
       {
          const FName& SourceTag = SimPair.Key;
          const FSourceState& State = SimPair.Value;

          if (State.bInvalid) continue;
          
          if (FMath::IsNearlyZero(State.RawSlope) && FMath::IsNearlyZero(State.NormSlope)) continue;

          FTCATSelfInfluenceRecipe Recipe;
          Recipe.bIsReversible = true;
          Recipe.RawCoefficient = State.RawSlope;
          Recipe.NormCoefficient = State.NormSlope;
          Recipe.DynamicScaleLayerTag = State.DynamicScaleTag; 
          Recipe.bIsApproximate = State.bApproximate;

          // [SourceTag] is contributed [Recipe] to [TargetTag]
          CachedInfluenceRecipes.FindOrAdd(SourceTag).Add(TargetTag, Recipe);
       }
    }
}

void ATCATInfluenceVolume::DebugDrawGrid()
{
	UWorld* World = GetWorld();
    // Use the unified GridResolution instead of individual resource sizes
    if (!World || GridResolution.X <= 0 || GridResolution.Y <= 0) return;

	if (DrawInfluence == ETCATDebugDrawMode::None) return;
	
	for (const auto& Pair : InfluenceLayers)
	{
		const FName& Tag = Pair.Key;
		const FTCATGridResource& Resource = Pair.Value;

		const FTCATLayerDebugSettings* Settings = CachedDebugSettingsMap.Find(Tag);
        
		// If no settings found, skip
		if (!Settings) continue;

		// Visibility Check
		if (DrawInfluence == ETCATDebugDrawMode::VisibleOnly && !Settings->bVisible)
		{
			continue;
		}

		if (Resource.Grid.Num() == 0) continue;

		const float MinX = CachedBounds.Min.X;
		const float MinY = CachedBounds.Min.Y;

		const FLinearColor& PosColor = Settings->PositiveColor;
		const FLinearColor& NegColor = Settings->NegativeColor;
		const FLinearColor MidColor = (PosColor + NegColor) * 0.5f;
		
	    for (int32 Y = 0; Y < GridResolution.Y; ++Y)
	    {
		    for (int32 X = 0; X < GridResolution.X; ++X)
		    {
	    		const int32 Index = Y * GridResolution.X + X;
		    	if (!Resource.Grid.IsValidIndex(Index)) continue;

		    	const float Value = Resource.Grid[Index];
		    	if (FMath::Abs(Value) < KINDA_SMALL_NUMBER) continue;

		    	FLinearColor FinalColor;
		    	if (Value > 0.0f)
		    	{
		    		// Interpolate 0 -> 1 (or Max)
		    		// Assuming value is somewhat normalized or we clamp it for color
		    		float Alpha = FMath::Clamp(Value, 0.0f, 1.0f); 
		    		FinalColor = FLinearColor::LerpUsingHSV(MidColor, PosColor, Alpha);
		    	}
		    	else
		    	{
		    		// Interpolate 0 -> -1
		    		float Alpha = FMath::Clamp(FMath::Abs(Value), 0.0f, 1.0f);
		    		FinalColor = FLinearColor::LerpUsingHSV(MidColor, NegColor, Alpha);
		    	}
                
		    	// Height Logic
		    	float DrawZ = GetGridOrigin().Z;
		    	if (HeightResource.Grid.IsValidIndex(Index))
		    	{
		    		DrawZ = HeightResource.Grid[Index];
		    	}
		    	DrawZ += Settings->HeightOffset;

		    	FVector Center(MinX + (X + CELL_CENTER_OFFSET) * CellSize, MinY + (Y + CELL_CENTER_OFFSET) * CellSize, DrawZ);
		    	
	    		// Render the debug point in the 3D viewport
		    	DrawDebugPoint(World, Center, (CellSize * CELL_CENTER_OFFSET) * 0.9f, FinalColor.ToFColor(true), false, -1.0f);
		    }
	    }
    }
}

void ATCATInfluenceVolume::BatchEnsureBaseLayers(const TSet<FName>& NewTags)
{
	bool bChanged = false;

	for (const FName& Tag : NewTags)
	{
		if (Tag.IsNone()) continue;
		if (CachedBaseLayerMap.Contains(Tag)) continue;

		// Double Check with Array Too
		bool bFound = BaseLayerConfigs.ContainsByPredicate([&](const FTCATBaseLayerConfig& Config)
		{
			return Config.BaseLayerTag == Tag;
		});

		if (!bFound)
		{
			FTCATBaseLayerConfig NewConfig;
			NewConfig.BaseLayerTag = Tag;
			NewConfig.ProjectionMask = 0; 
			NewConfig.DebugSettings.bVisible = true;
			
			BaseLayerConfigs.Add(NewConfig);
			CachedBaseLayerMap.Add(Tag, NewConfig);
			
			bChanged = true;
		}
	}
	
	if (bChanged)
	{
#if WITH_EDITOR
		Modify();
#endif
		RebuildRuntimeMaps();
		InitializeResources();
	}
}

TArray<FString> ATCATInfluenceVolume::GetAllTagOptions() const
{
	return UTCATSettings::GetAllTagOptions();
}

TArray<FString> ATCATInfluenceVolume::GetBaseTagOptions() const
{
	return UTCATSettings::GetBaseTagOptions();
}

TArray<FString> ATCATInfluenceVolume::GetCompositeTagOptions() const
{
	return UTCATSettings::GetCompositeTagOptions();
}

UTextureRenderTarget2D* ATCATInfluenceVolume::GetHeightRenderTarget() const
{
	return HeightResource.RenderTarget;
}

FTextureRenderTargetResource* ATCATInfluenceVolume::GetHeightRenderTargetTexture() const
{
	UTextureRenderTarget2D* TargetRT = GetHeightRenderTarget();
	return TargetRT ? TargetRT->GameThread_GetRenderTargetResource() : nullptr;
}

UTCATSubsystem* ATCATInfluenceVolume::GetTCATSubsystem() const
{
    return GetWorld() ? GetWorld()->GetSubsystem<UTCATSubsystem>() : nullptr;
}

void ATCATInfluenceVolume::VLogInfluenceVolume() const
{    
    TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_VLogFullLayers);

    TMap<FName, const FTCATGridResource*> ResourcesToLog;
    for (const auto& Pair : InfluenceLayers)
    {
        ResourcesToLog.Add(Pair.Key, &Pair.Value);
    }

    const int32 ActiveStride = CVarTCATLogStride.GetValueOnGameThread();

	UE_VLOG(this, LogTCAT, Log, TEXT("PredictionCorrectionThreshold: %.2f, DeltaSeconds: %.5f"), PredictionCorrectionThreshold, LastDeltaSeconds);

	// Set Each map's setting
    for (const auto& ResPair : ResourcesToLog)
    {
        const FName& LayerName = ResPair.Key;
        const FTCATGridResource* Resource = ResPair.Value;
        if (!Resource || Resource->Grid.Num() == 0) continue;
    	
    	const FTCATLayerDebugSettings* Config = CachedDebugSettingsMap.Find(LayerName);

        // Make Each map's check UI in Vislog editor
        const FString CatStr = FString::Printf(TEXT("TCAT.%s"), *LayerName.ToString());
        const FName LogCat(*CatStr);
        const FName TextLogCat(*(CatStr + TEXT(".Text")));
    	
		const FTCATPredictionInfo& PredictionInfo = TagToPredictionInfo.FindRef(LayerName);
        UE_VLOG(this, LogCat, Log, TEXT("Layer: %s, Frame: %llu, GPU: %s, PrevPredictionTime: %.5f, PredictionTime: %.5f"), 
            *LayerName.ToString(), GFrameCounter, bRefreshWithGPU ? TEXT("true") : TEXT("false"), 
            PredictionInfo.PrevPredictionTime, PredictionInfo.PredictionTime);

        // ===========================================
        // Adaptable Sampling : Automatically Adjust Stride Per Map size
        // ===========================================
        const int32 TotalCells = GridResolution.X * GridResolution.Y;
        constexpr int32 TargetMaxCells = 16384;         
        
        int32 AdaptiveStride = 1;
        if (TotalCells > TargetMaxCells)
        {
            AdaptiveStride = FMath::CeilToInt(FMath::Sqrt((float)TotalCells / (float)TargetMaxCells));
        }

        // ===========================================
        // Draw Heat Map! 0.<!
        // ===========================================
        struct FInfluenceBin
        {
            TArray<FVector> Vertices;
            TArray<int32> Indices;
            FColor Color;
        };
        FInfluenceBin Bins[6];
        const float LerpValues[6] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f};
        constexpr float Threshold = 0.01f;

    	const FLinearColor& PosColor =  Config ? Config->PositiveColor : FLinearColor::Green;
    	const FLinearColor& NegColor =  Config ? Config->NegativeColor : FLinearColor::Red;
    	const FLinearColor MidColor = (PosColor + NegColor) * 0.5f;
    	
    	const FLinearColor BaseTargetColor = (LayerName == TEXT("GlobalHeight")) 
			? FLinearColor::White // Height maps go to White
			: PosColor;           // Others go to Positive
                
        for (int32 i = 0; i < 6; ++i)
        {
            Bins[i].Vertices.Reserve(2048);
            Bins[i].Color = FLinearColor::LerpUsingHSV(MidColor, BaseTargetColor, LerpValues[i]).ToFColor(true);
        }

        const float MinX = CachedBounds.Min.X;
        const float MinY = CachedBounds.Min.Y;
		const float ZOffset = (LayerName == TEXT("GlobalHeight")
			? 0.f
			: (Config ? Config->HeightOffset : 10.0f));

        // Sampling
        for (int32 y = 0; y < GridResolution.Y; y += AdaptiveStride)
        {
            for (int32 x = 0; x < GridResolution.X; x += AdaptiveStride)
            {
                const int32 Index = y * GridResolution.X + x;
                if (!Resource->Grid.IsValidIndex(Index)) continue;
                
                const float Value = Resource->Grid[Index];
                if (FMath::Abs(Value) <= Threshold) continue;

				// Get Height
				float CellZ = GetGridOrigin().Z;
				if (HeightResource.Grid.IsValidIndex(Index))
				{
					CellZ = HeightResource.Grid[Index];
				}
				const float FinalZ = CellZ + ZOffset;

                const float AbsValue = FMath::Abs(Value);
                int32 BinIndex = FMath::Clamp(FMath::FloorToInt(AbsValue * 5.0f), 0, 5);

                FInfluenceBin& Bin = Bins[BinIndex];
                const int32 StartIdx = Bin.Vertices.Num();
            	
                const float AdjustedCellSize = CellSize * AdaptiveStride;
                const float CX = MinX + x * CellSize;
                const float CY = MinY + y * CellSize;

                // Add Quad
                Bin.Vertices.Add(FVector(CX, CY, FinalZ));
                Bin.Vertices.Add(FVector(CX + AdjustedCellSize, CY, FinalZ));
                Bin.Vertices.Add(FVector(CX + AdjustedCellSize, CY + AdjustedCellSize, FinalZ));
                Bin.Vertices.Add(FVector(CX, CY + AdjustedCellSize, FinalZ));

                Bin.Indices.Add(StartIdx + 0); 
                Bin.Indices.Add(StartIdx + 1); 
                Bin.Indices.Add(StartIdx + 2);
                Bin.Indices.Add(StartIdx + 0); 
                Bin.Indices.Add(StartIdx + 2); 
                Bin.Indices.Add(StartIdx + 3);

                // Add Text
                float TextZOffset = CVarTCATTextOffset.GetValueOnGameThread();
                if (ActiveStride > 0 && x % (ActiveStride * AdaptiveStride) == 0 && y % (ActiveStride * AdaptiveStride) == 0)
                {
                    FVector TextLocation(
                        CX + AdjustedCellSize * 0.5f, 
                        CY + AdjustedCellSize * 0.5f, 
                        FinalZ + TextZOffset
                    );
                    
                    UE_VLOG_LOCATION(this, TextLogCat, Log, TextLocation, 0.f, Bin.Color, TEXT("%.2f"), Value);
                }
            }
        }
    	
        int32 TotalVertices = 0;
        for (int32 i = 0; i < 6; ++i)
        {
            TotalVertices += Bins[i].Vertices.Num();
            if (Bins[i].Vertices.Num() > 0)
            {
                UE_VLOG_MESH(this, LogCat, Log, Bins[i].Vertices, Bins[i].Indices, Bins[i].Color, 
                    TEXT("Grid Mesh: %s (Stride:%d)"), *LayerName.ToString(), AdaptiveStride);
            }
        }
    	
        UE_VLOG(this, LogCat, Log, TEXT("Rendered: %d/%d cells (Stride: %d, Vertices: %d)"),
            (GridResolution.X / AdaptiveStride) * (GridResolution.Y / AdaptiveStride),
            TotalCells, AdaptiveStride, TotalVertices);
    }
}