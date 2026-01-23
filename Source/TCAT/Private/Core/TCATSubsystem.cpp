// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Core/TCATSubsystem.h"
#include "TCAT.h"
#include "Core/TCATSettings.h"
#include "Scene/TCATInfluenceComponent.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Simulation/TCATInfluenceDispatcher.h"
#include "Engine/TextureRenderTarget2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h" 
#include "RHIGPUReadback.h"
#include "Core/TCATMathLibrary.h"
#include "Query/TCATAsyncQueryAction.h"
#include "Query/TCATAsyncMultiSearchAction.h"
#include "Engine/World.h"
#include "Engine/Engine.h" 
#include "Curves/CurveFloat.h"
#include "RenderingThread.h"
#include "Async/Async.h"
#include "TextureResource.h" 
#include "VisualLogger/VisualLogger.h"

DECLARE_CYCLE_STAT(TEXT("GPU_Readback_Retrieve"), STAT_TCAT_Readback_Retrieve, STATGROUP_TCAT);
DECLARE_CYCLE_STAT(TEXT("GPU_Readback_LockCopy"), STAT_TCAT_Readback_LockCopy, STATGROUP_TCAT);

void UTCATSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Fetch the global settings defined in Project Settings
	const UTCATSettings* Settings = GetDefault<UTCATSettings>();
    
	if (Settings)
	{
		CachedMaxMapResolution = Settings->MaxMapResolution;
	}
	else
	{
		// Fallback to safe hardcoded defaults if settings are missing
		CachedMaxMapResolution = 1024;
		UE_LOG(LogTCAT, Warning, TEXT("TCATSubsystem: Settings not found! Using hardware safety defaults."));
	}

	QueryProcessor.Initialize(GetWorld(), &MapGroupedVolumes);
	InitializeStaticGlobalCurveAtlas();

	CachedAdaptiveModeSwitchingDelay = Settings->AdaptiveModeSwitchingDelay;
	CachedModeSwitchingSafetyMultiplier = Settings->ModeSwitchingSafetyMultiplier;
	CachedWaitTimeMsThresholdForGPUMode = Settings->WaitTimeMsThresholdForGPUMode;
	CachedSwitchConditionCheckDuration = Settings->SwitchConditionCheckDuration;
	CachedRequiredSatisfactionRatio = Settings->RequiredSatisfactionRatio;
	CachedSourceCountChangeThreshold = Settings->SourceCountChangeThreshold;

	AdaptiveModeSwitchingStartSeconds = GetWorld()->GetTimeSeconds() + CachedAdaptiveModeSwitchingDelay;
	bIsFirstCheck = true;

	UE_LOG(LogTCAT, Log, TEXT("[TCATSubsystem] TCATSubsystem Initialized!"));
}

void UTCATSubsystem::Deinitialize()
{
	UTCATAsyncSearchAction::ResetPool();
	UTCATAsyncMultiSearchAction::ResetPool();
	QueryProcessor.Shutdown();
	GlobalCurveAtlasRHI.SafeRelease();
	Super::Deinitialize();
}

void UTCATSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_Subsystem_Tick);

	if (!GlobalCurveAtlasRHI.IsValid()) { return; }

	// Wait time 2 frames prior
	const wchar_t* CurAdaptiveRefreshModeStr = bRefreshWithGPUForAdaptiveVolumes ? TEXT("GPU") : TEXT("CPU");
	UE_LOG(LogTCAT, VeryVerbose, TEXT("[TCAT Subsystem] Frame Wait Time prior to 2 frames: %.2f ms, AdaptivelyRefreshMode: %s"), FPlatformTime::ToMilliseconds(GGameThreadWaitTime)
		, CurAdaptiveRefreshModeStr);

	// Check if CPU measurement task has completed
	if (bIsMeasuringCPU && CPUMeasurementTask.IsReady())
	{
		CPUModeTickTimeMs = CPUMeasurementTask.Get();
		bIsMeasuringCPU = false;
		UE_LOG(LogTCAT, Log, TEXT("[TCAT Subsystem] CPU Mode Measurement Complete: %.2f ms"), CPUModeTickTimeMs);
	}

	const float TickStartTimeMs = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());

	TArray<FTCATInfluenceDispatchParams> InfluenceBatch;
	TArray<FTCATCompositeDispatchParams> CompositeBatch;
	InfluenceBatch.Reserve(16);
	CompositeBatch.Reserve(8);

	TArray<FTCATInfluenceDispatchParams> CPUMeasurementInfluenceParams;
	TArray<FTCATCompositeDispatchParams> CPUMeasurementCompositeParams;
	CPUMeasurementInfluenceParams.Reserve(16);
	CPUMeasurementCompositeParams.Reserve(8);

	// --- Phase 1: Data Preparation & Source Pass ---
	uint64 CurrentTotalSourceCount = 0;
	for (auto Volume : RegisteredVolumes)
	{
		if (!Volume) { continue; }

		if (Volume->bAdaptivelySwitchRefreshMode)
		{
			Volume->bRefreshWithGPU = bRefreshWithGPUForAdaptiveVolumes;
		}

		RetrieveGPUResults(Volume);
		Volume->UpdateVolumeInfos();

		for (const auto& Layer : Volume->BaseLayerConfigs)
		{
			const FName& Tag = Layer.BaseLayerTag;

			TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_SourcePass);
			FTCATInfluenceDispatchParams Params = CreateDispatchParams(Volume, Tag);
			CurrentTotalSourceCount += static_cast<uint64>(Params.Sources.Num());

			if (Volume->bRefreshWithGPU && Params.bEnableWrite)
			{
				InfluenceBatch.Add(Params);
			}
			else if (!Volume->bRefreshWithGPU)
			{
				FTCATInfluenceDispatcher::DispatchCPU(Params);
			}

			// Collect parameters for CPU measurement (only for adaptive volumes)
			if (Volume->bAdaptivelySwitchRefreshMode && bShouldMeasureCPUMode)
			{
				checkf(Volume->bRefreshWithGPU, TEXT("Volume must be set to refresh with GPU when async CPU measurement is started."));
				CPUMeasurementInfluenceParams.Add(Params);
			}
		}
	}

	// --- Phase 2: Internal Composite Pass ---
	for (auto Volume : RegisteredVolumes)
	{
		if (!IsValid(Volume) || Volume->CompositeLayers.Num() == 0) { continue; }

		if (Volume->bAdaptivelySwitchRefreshMode)
		{
			Volume->bRefreshWithGPU = bRefreshWithGPUForAdaptiveVolumes;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_CompositePass);

		for (const FTCATCompositeLayerConfig& Layer : Volume->CompositeLayers)
		{
			if (!Layer.LogicAsset || Layer.LogicAsset->Operations.Num() == 0) 
			{
				continue;
			}

			FTCATCompositeDispatchParams Params = CreateCompositeDispatchParams(Volume, Layer);

			if (Volume->bRefreshWithGPU && Params.bEnableWrite)
			{
				CompositeBatch.Add(Params);
			}
			else if (!Volume->bRefreshWithGPU)
			{
				FTCATInfluenceDispatcher::DispatchCPU_Composite(Params);
			}

			// Collect parameters for CPU measurement (only for adaptive volumes)
			if (Volume->bAdaptivelySwitchRefreshMode && bShouldMeasureCPUMode)
			{
				checkf(Volume->bRefreshWithGPU, TEXT("Volume must be set to refresh with GPU when async CPU measurement is started."));
				CPUMeasurementCompositeParams.Add(Params);
			}
		}
	}

	// --- Phase 3: Execute both batches in single RDG graph ---
	if (InfluenceBatch.Num() > 0 || CompositeBatch.Num() > 0)
	{
		ENQUEUE_RENDER_COMMAND(TCAT_DispatchBatchUpdate)(
			[IBatch = MoveTemp(InfluenceBatch), CBatch = MoveTemp(CompositeBatch)](FRHICommandListImmediate& RHICmdList) mutable
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_RT_ExecuteDispatch);
				FTCATInfluenceDispatcher::DispatchGPU_Batched(RHICmdList, MoveTemp(IBatch), MoveTemp(CBatch));
			});
	}

	// --- Phase 4: Transient Influence Update ---
	for (int32 i = AllTransientSources.Num() - 1; i >= 0; --i)
	{
		FTransientSourceWrapper& SourceWrapper = AllTransientSources[i];
		SourceWrapper.ElapsedTime += DeltaTime;
		SourceWrapper.Data.Strength = SourceWrapper.StrengthCurveOverTime->GetFloatValue(SourceWrapper.ElapsedTime);

		if (SourceWrapper.Data.Strength <= UE_KINDA_SMALL_NUMBER && SourceWrapper.bDestroyIfZeroStrength)
		{
			AllTransientSources.RemoveAtSwap(i);
		}
	}

	// --- Phase 5:Launch CPU Measurement Task (if needed) ---
	if (bShouldMeasureCPUMode && !bIsMeasuringCPU &&
		(CPUMeasurementInfluenceParams.Num() > 0 || CPUMeasurementCompositeParams.Num() > 0))
	{
		bIsMeasuringCPU = true;
		LastMeasuredTotalSourceCount = CurrentTotalSourceCount;

		CPUMeasurementTask = Async(EAsyncExecution::ThreadPool, [
			InfluenceParams = MoveTemp(CPUMeasurementInfluenceParams),
			CompositeParams = MoveTemp(CPUMeasurementCompositeParams)
		]() -> float
			{
				const double StartTime = FPlatformTime::Seconds();

				// Execute CPU dispatch for measurement only
				for (const FTCATInfluenceDispatchParams& Params : InfluenceParams)
				{
					// Create a copy to avoid modifying actual grid data
					FTCATInfluenceDispatchParams MeasureParams = Params;
					TArray<float> TempGrid;
					TempGrid.SetNumUninitialized(Params.MapSize.X * Params.MapSize.Y);
					MeasureParams.OutGridData = &TempGrid;

					FTCATInfluenceDispatcher::DispatchCPU(MeasureParams);
				}

				for (const FTCATCompositeDispatchParams& Params : CompositeParams)
				{
					// Create a copy to avoid modifying actual grid data
					FTCATCompositeDispatchParams MeasureParams = Params;
					TArray<float> TempGrid;
					TempGrid.SetNumUninitialized(Params.MapSize.X * Params.MapSize.Y);
					MeasureParams.OutGridData = &TempGrid;

					FTCATInfluenceDispatcher::DispatchCPU_Composite(MeasureParams);
				}

				const double EndTime = FPlatformTime::Seconds();
				return static_cast<float>((EndTime - StartTime) * 1000.0); // Convert to milliseconds
			});

		bShouldMeasureCPUMode = false;
		UE_LOG(LogTCAT, Log, TEXT("[TCAT Subsystem] Started CPU Mode Measurement on separate thread."));
	}

	const float TickEndTimeMs = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());
	CurTickTimeMs = TickEndTimeMs - TickStartTimeMs;

	// --- Phase 6: Process the measurement results and determine whether to switch the map update mode for volumes where bAdaptivelySwitchRefreshMode is true. ---
	if (bRefreshWithGPUForAdaptiveVolumes)
	{
		GPUModeTickTimeMs = CurTickTimeMs;
	}
	else
	{
		CPUModeTickTimeMs = CurTickTimeMs;
	}

	if (GetWorld()->GetTimeSeconds() > AdaptiveModeSwitchingStartSeconds)
	{
		if (bIsFirstCheck)
		{
			check(!bIsMeasuringCPU);

			UE_LOG(LogTCAT, Log, TEXT("[TCAT Subsystem] Since the %.2f s has elapsed, we will now determine whether to switch the map update mode for Adaptive Volumes.")
				, AdaptiveModeSwitchingStartSeconds);
			bShouldMeasureCPUMode = true;
			ElapsedTimeSinceConditionCheckStarted = 0.0;
			bIsFirstCheck = false;
		}
		else
		{
			const float WaitTimeMs = FPlatformTime::ToMilliseconds(GGameThreadWaitTime);
			ElapsedTimeSinceConditionCheckStarted += DeltaTime;

			if ((bRefreshWithGPUForAdaptiveVolumes && CPUModeTickTimeMs < GPUModeTickTimeMs + WaitTimeMs * CachedModeSwitchingSafetyMultiplier)
				|| (!bRefreshWithGPUForAdaptiveVolumes && WaitTimeMs < CachedWaitTimeMsThresholdForGPUMode))
			{
				SatisfiedFrameCount++;
			}
			else
			{
				UnsatisfiedFrameCount++;
			}

			if (ElapsedTimeSinceConditionCheckStarted >= CachedSwitchConditionCheckDuration)
			{
				const wchar_t* TargetAdaptiveRefreshModeStr = bRefreshWithGPUForAdaptiveVolumes ? TEXT("CPU") : TEXT("GPU");
				const float SatisfactionRatio = (float)SatisfiedFrameCount / (float)(SatisfiedFrameCount + UnsatisfiedFrameCount);

				if (SatisfactionRatio >= CachedRequiredSatisfactionRatio)
				{
					bRefreshWithGPUForAdaptiveVolumes = !bRefreshWithGPUForAdaptiveVolumes;
					UE_LOG(LogTCAT, Log, TEXT("[TCAT Subsystem] Success to Satisfy Switching Condition to %s mode. Proceed with switching. Satisfaction Ratio: %.2f%% (Threshold: %.2f%%).")
						, TargetAdaptiveRefreshModeStr, SatisfactionRatio * 100.0f, CachedRequiredSatisfactionRatio * 100.0f);
				}
				else
				{
					UE_LOG(LogTCAT, Log, TEXT("[TCAT Subsystem] Failed to Satisfy Switching Condition to %s mode. Satisfaction Ratio: %.2f%% (Threshold: %.2f%%).")
						, TargetAdaptiveRefreshModeStr, SatisfactionRatio * 100.0f, CachedRequiredSatisfactionRatio * 100.0f);
				}

				UE_LOG(LogTCAT, Verbose, TEXT("[TCAT Subsystem] Adaptive Mode Switching Check Complete. Satisfaction Ratio: %.2f%% (Threshold: %.2f%%). SatisfiedFrameCount: %u, UnsatisfiedFrameCount: %u. WaitTimeMs: %.2f")
					, SatisfactionRatio * 100.0f, CachedRequiredSatisfactionRatio * 100.0f, SatisfiedFrameCount, UnsatisfiedFrameCount, WaitTimeMs);

				ElapsedTimeSinceConditionCheckStarted = 0.0;
				SatisfiedFrameCount = 0;
				UnsatisfiedFrameCount = 0;

				if (!bIsMeasuringCPU && bRefreshWithGPUForAdaptiveVolumes 
					&& FMath::Abs(static_cast<int64>(LastMeasuredTotalSourceCount) - static_cast<int64>(CurrentTotalSourceCount)) > static_cast<int64>(CachedSourceCountChangeThreshold))
				{
					UE_LOG(LogTCAT, Log, TEXT("[TCAT Subsystem] Significant change in total source count detected (%llu -> %llu). Forcing re-measurement of CPU mode.")
						, LastMeasuredTotalSourceCount, CurrentTotalSourceCount);
					bShouldMeasureCPUMode = true;
				}
				else
				{
					UE_LOG(LogTCAT, Verbose, TEXT("[TCAT Subsystem] LastMeasuredTotalSourceCount: %llu, CurrentTotalSourceCount: %llu"), LastMeasuredTotalSourceCount, CurrentTotalSourceCount);
				}
			}
		}

	}

	// --- Phase 6: VLog ---
	VLogInfluence();
}
void UTCATSubsystem::RegisterVolume(ATCATInfluenceVolume* InVolume)
{
	if (!InVolume) { return; }
	RegisteredVolumes.Add(InVolume);

	for (const auto& Layer : InVolume->BaseLayerConfigs)
	{
		const FName& Tag = Layer.BaseLayerTag;
		MapGroupedVolumes.FindOrAdd(Tag).Add(InVolume);
	}

	for (const FTCATCompositeLayerConfig& CompositeLayer : InVolume->CompositeLayers)
	{
		const FName& Tag = CompositeLayer.CompositeLayerTag;
		if (Tag.IsNone()) { continue; }
		MapGroupedVolumes.FindOrAdd(Tag).Add(InVolume);
	}
	
	SyncVolumeWithExistingComponents(InVolume);
}

void UTCATSubsystem::UnregisterVolume(ATCATInfluenceVolume* InVolume)
{
	if (!InVolume) { return; }
	RegisteredVolumes.Remove(InVolume);

	for (const auto& Layer : InVolume->BaseLayerConfigs)
	{
		const FName& Tag = Layer.BaseLayerTag;
		if (auto VolumeSet =  MapGroupedVolumes.Find(Tag))
		{
			VolumeSet->Remove(InVolume);
		}
	}

	for (const FTCATCompositeLayerConfig& CompositeLayer : InVolume->CompositeLayers)
	{
		const FName& Tag = CompositeLayer.CompositeLayerTag;
		if (Tag.IsNone()) { continue; }
		if (TSet<ATCATInfluenceVolume*>* VolumeSet = MapGroupedVolumes.Find(Tag))
		{
			VolumeSet->Remove(InVolume);
		}
	}
}

ATCATInfluenceVolume* UTCATSubsystem::GetInfluenceVolume(FName MapTag)
{
	if (MapTag.IsNone())
	{
		UE_LOG(LogTCAT, Warning, TEXT("TCATSubsystem: GetInfluenceVolume called with None MapTag."));
		return nullptr; 
	}

	if (TSet<ATCATInfluenceVolume*>* VolumeSet = MapGroupedVolumes.Find(MapTag))
	{
		for (ATCATInfluenceVolume* Volume : *VolumeSet)
		{
			if (IsValid(Volume))
			{
				return Volume;
			}
		}
	}
	return nullptr;
}

void UTCATSubsystem::RegisterComponent(UTCATInfluenceComponent* InComp)
{
	if (!InComp) { return; }
	RegisteredComponents.Add(InComp);
	for (const auto& Layer: InComp->GetInfluenceLayers())
	{
		FName LayerTag = Layer.MapTag;
		MapGroupedComponents.FindOrAdd(LayerTag).Add(InComp);
	}

	AttachComponentTagsToVolumes(InComp);
}

void UTCATSubsystem::UnregisterComponent(UTCATInfluenceComponent* InComp)
{
	if (!InComp) { return; }
	
	RegisteredComponents.Remove(InComp);
	
	for (const auto& Layer: InComp->GetInfluenceLayers())
	{
		FName LayerTag = Layer.MapTag;
		if (TSet<UTCATInfluenceComponent*>* ComponentSet = MapGroupedComponents.Find(LayerTag))
		{
			ComponentSet->Remove(InComp);
		}
	}
}

void UTCATSubsystem::SyncVolumeWithExistingComponents(ATCATInfluenceVolume* Volume)
{
	if (!Volume) { return; }

	TArray<UTCATInfluenceComponent*> OverlappingComponents;
	GetAllComponentsInBounds(Volume->GetCachedBounds(), OverlappingComponents);

	TSet<FName> TagsToRegister;
	for (UTCATInfluenceComponent* Comp : OverlappingComponents)
	{
		if (!IsValid(Comp)) { continue; }

		for (const FTCATInfluenceConfigEntry& Layer : Comp->GetInfluenceLayers())
		{
			if (Layer.MapTag.IsNone() || Volume->CachedBaseLayerMap.Contains(Layer.MapTag))
			{
				continue;
			}

			TagsToRegister.Add(Layer.MapTag);
		}
	}

	if (TagsToRegister.Num() > 0)
	{
		Volume->BatchEnsureBaseLayers(TagsToRegister);
	}
}

void UTCATSubsystem::AttachComponentTagsToVolumes(UTCATInfluenceComponent* InComp)
{
	if (!InComp) { return; }
	
	TSet<FName> ComponentTags;
	for (const FTCATInfluenceConfigEntry& Layer : InComp->GetInfluenceLayers())
	{
		if (!Layer.MapTag.IsNone())
		{
			ComponentTags.Add(Layer.MapTag);
		}
	}

	if (ComponentTags.Num() == 0) return;
	
	const FVector CompLocation = InComp->ResolveWorldLocation();

	for (ATCATInfluenceVolume* Volume : RegisteredVolumes)
	{
		if (!IsValid(Volume)) { continue; }

		const float DistSq = Volume->GetCachedBounds().ComputeSquaredDistanceToPoint(CompLocation);

		TSet<FName> TagsForThisVolume;
		
		for (const FName& Tag : ComponentTags)
		{
			const float Radius = InComp->GetRadius(Tag);
			if (Radius <= 0.0f) { continue; }

			if (DistSq <= (Radius * Radius) && !Volume->CachedBaseLayerMap.Contains(Tag))
			{
				TagsForThisVolume.Add(Tag);
			}
		}
		
		if (TagsForThisVolume.Num() > 0)
		{
			Volume->BatchEnsureBaseLayers(TagsForThisVolume);
		}
	}	
}

void UTCATSubsystem::GetComponentsByTag(FName MapTag, const FBox& InVolumeBounds, TArray<UTCATInfluenceComponent*>& OutComponents)
{
	OutComponents.Reset();
	if (MapTag.IsNone()) return;

	if (TSet<UTCATInfluenceComponent*>* SetPtr = MapGroupedComponents.Find(MapTag))
	{
		for (UTCATInfluenceComponent* Comp : *SetPtr)
		{
			if (!IsValid(Comp)) continue;
			
			float DistSq = InVolumeBounds.ComputeSquaredDistanceToPoint(FVector(Comp->ResolveWorldLocation()));
			for (const auto& Layer: Comp->GetInfluenceLayers())
			{
				float Radius = Comp->GetRadius(Layer.MapTag);
				if (DistSq <= (Radius * Radius))
				{
					OutComponents.Add(Comp);
					break;
				}
			}
		}
	}
}

void UTCATSubsystem::GetAllComponentsInBounds(const FBox& InVolumeBounds, TArray<UTCATInfluenceComponent*>& OutComponents)
{
	OutComponents.Reset();

	for (UTCATInfluenceComponent* Comp : RegisteredComponents)
	{
		if (!IsValid(Comp)) continue;

		const FVector CompLocation = Comp->ResolveWorldLocation();
		float DistSq = InVolumeBounds.ComputeSquaredDistanceToPoint(CompLocation);
		for (const auto& Layer: Comp->GetInfluenceLayers())
		{
			float Radius = Comp->GetRadius(Layer.MapTag);
			if (DistSq <= (Radius * Radius))
			{
				OutComponents.Add(Comp);
				break;
			}
		}
	}
}

void UTCATSubsystem::AddTransientInfluence(FName MapTag, const FTCATInfluenceSource& InSource, UCurveFloat* InStrengthCurveOverTime, bool bDestroyIfZeroStrength, UCurveFloat* InCurve)
{
	if (MapTag.IsNone())
	{
		UE_LOG(LogTCAT, Error, TEXT("TCATSubsystem: MapTag is None!"));
		return;
	}

	if (!InStrengthCurveOverTime)
	{
		UE_LOG(LogTCAT, Error, TEXT("TCATSubsystem: InStrengthCurveOverTime is null!"));
		return;
	}

	FTransientSourceWrapper Wrapper;
	Wrapper.MapTag = MapTag;
	Wrapper.Data = InSource;
	Wrapper.CurveAsset = InCurve;
	Wrapper.StrengthCurveOverTime = InStrengthCurveOverTime;
	Wrapper.ElapsedTime = 0.0f;
	Wrapper.bDestroyIfZeroStrength = bDestroyIfZeroStrength;

	Wrapper.Data.Strength = Wrapper.StrengthCurveOverTime->GetFloatValue(Wrapper.ElapsedTime);

	AllTransientSources.Add(Wrapper);
}

void UTCATSubsystem::GetAllTransientSourcesInBounds(const FBox& InVolumeBounds, TArray<FTransientSourceWrapper>& OutWrapperSources)
{
	OutWrapperSources.Reset();

	for (const FTransientSourceWrapper& Wrapper : AllTransientSources)
	{
		if (Wrapper.Data.Strength <= 0.0f) { continue; }

		// Convert FVector3f (GPU data format) to FVector for spatial calculation.
		const FVector SourcePos = FVector(Wrapper.Data.WorldLocation);
        
		float DistSq = InVolumeBounds.ComputeSquaredDistanceToPoint(SourcePos);
        
		const float Radius = Wrapper.Data.InfluenceRadius;
		if (DistSq <= (Radius * Radius))
		{
			OutWrapperSources.Add(Wrapper);
		}
	}
}

int32 UTCATSubsystem::GetCurveID(UCurveFloat* InCurve)
{
	if (!InCurve) return 0;
	
	if (const int32* FoundID = GlobalCurveIDMap.Find(InCurve))
	{
		return *FoundID;
	}

#if WITH_EDITOR
	// Fallback
	UE_LOG(LogTCAT, Warning, TEXT("TCAT: Curve '%s' is not in %s! It will be ignored."), *InCurve->GetName(), *CURVE_SEARCH_PATH);
#endif
	return 0; 
}

uint32 UTCATSubsystem::RequestBatchQuery(FTCATBatchQuery&& NewQuery)
{
	return QueryProcessor.EnqueueQuery(MoveTemp(NewQuery));
}

void UTCATSubsystem::CancelBatchQuery(uint32 QueryID)
{
	QueryProcessor.CancelQuery(QueryID);
}

void UTCATSubsystem::VLogInfluence()
{
#if ENABLE_VISUAL_LOG
	if (!FVisualLogger::IsRecording()) { return; }
	
	for (auto Volume: RegisteredVolumes)
	{
		Volume->VLogInfluenceVolume();
	}
	
	for (auto Component: RegisteredComponents)
	{
		Component->VLogInfluence();
	}
#endif
}

FTCATInfluenceDispatchParams UTCATSubsystem::CreateDispatchParams(ATCATInfluenceVolume* Volume, FName LayerTag)
{
    FTCATInfluenceDispatchParams Params;

    FTCATGridResource* LayerRes = const_cast<FTCATGridResource*>(Volume->GetLayerResource(LayerTag));
    TArray<FTCATInfluenceSource>* LayerSources = Volume->LayerSourcesMap.Find(LayerTag);
	TArray<FTCATInfluenceSourceWithOwner>* LayerSourcesWithOwners = Volume->LayerSourcesWithOwners.Find(LayerTag);

	const FTCATPredictionInfo* PredictionPtr = Volume->TagToPredictionInfo.Find(LayerTag);
	const float PredictionTime = PredictionPtr ? PredictionPtr->PredictionTime : 0.0f;

    if (!LayerRes || !LayerSources || !LayerSourcesWithOwners)
    {
	    Params.bEnableWrite = false;
	    return Params;
    }

    Params.VolumeName = FString::Printf(TEXT("%s_%s"), *Volume->GetName(), *LayerTag.ToString());
    Params.Sources = MoveTemp(*LayerSources);

    Params.bIsAsync = Volume->bAsyncReadback;
	if (Volume->bRefreshWithGPU)
	{
		if (Params.bIsAsync)
		{
			FTCATAsyncResource WriteResource;
			// Each layer now manages its own RingBuffer
			if (!LayerRes->AsyncRingBuffer.AdvanceWriteResource(WriteResource, PredictionTime, LayerSourcesWithOwners))
			{
				Params.bEnableWrite = false;
				UE_LOG(LogTCAT, Warning, TEXT("Layer[%s] in Volume[%s] Async Ring Buffer is full!"),
				   *LayerTag.ToString(), *Volume->GetActorNameOrLabel());
				return Params;
			}

			FTextureRenderTargetResource* RTResource = WriteResource.RenderTarget->GameThread_GetRenderTargetResource();
			Params.OutInfluenceMapRHI = RTResource ? RTResource->GetRenderTargetTexture() : FTextureRHIRef();
			Params.GPUReadback = WriteResource.Readback;

			LayerRes->LastRequestFrame = GFrameCounter;
		}
		else
		{
			FTextureRenderTargetResource* RTResource = LayerRes->RenderTarget->GameThread_GetRenderTargetResource();
			Params.OutInfluenceMapRHI = RTResource ? RTResource->GetRenderTargetTexture() : FTextureRHIRef();
		}
	}

    Params.CurveAtlasPixelData = GlobalAtlasPixelData;
    Params.GlobalCurveAtlasRHI = GlobalCurveAtlasRHI;

    FTextureRenderTargetResource* HeightRT = Volume->GetHeightRenderTargetTexture();
    Params.GlobalHeightMapRHI = HeightRT ? HeightRT->GetRenderTargetTexture() : FTextureRHIRef();

    // Use the volume's grid origin as the heightmap reference
    Params.GlobalHeightMapOrigin = FVector3f(Volume->GetGridOrigin());

    // The heightmap size is now the volume's resolution * cell size
    FVector2D HeightSize(Volume->GetColumns() * Volume->GetCellSize(), Volume->GetRows() * Volume->GetCellSize());
    Params.GlobalHeightMapSize = FVector2f(HeightSize.X, HeightSize.Y);
    Params.GlobalHeightMapData = &Volume->HeightResource.Grid;

    const int32 RequestedProjectionMask = Volume->GetProjectionMask(LayerTag);
    int32 EffectiveMask = RequestedProjectionMask;
    if (!Params.GlobalHeightMapRHI.IsValid())
    {
        EffectiveMask &= ~(int32)ETCATProjectionFlag::InfluenceHalfHeight;
    }
    Params.ProjectionFlags = EffectiveMask;
    Params.AtlasWidth = ATLAS_TEXTURE_WIDTH;
	
    const FTCATBaseLayerConfig* LayerConfig = Volume->CachedBaseLayerMap.Find(LayerTag);
    if (LayerConfig)
    {
        Params.RayMarchStepSize = FMath::Max(LayerConfig->RayMarchSettings.LineOfSightStepSize, 1.0f);
        Params.RayMarchMaxSteps = FMath::Max(LayerConfig->RayMarchSettings.LineOfSightMaxSteps, 1);
    }

    Params.MapStartPos = Volume->GetGridOrigin();
    Params.GridSize = Volume->GetCellSize();
    Params.MapSize = FUintVector2(Volume->GetColumns(), Volume->GetRows());
	
    // Point to the specific layer's CPU grid for readback
    Params.OutGridData = &LayerRes->Grid;

	Params.bForceCPUSingleThread = Volume->bForceCPUSingleThreadUpdate;

    return Params;
}

FTCATCompositeDispatchParams UTCATSubsystem::CreateCompositeDispatchParams(ATCATInfluenceVolume* Volume, const FTCATCompositeLayerConfig& CompositeLayer)
{
    FTCATCompositeDispatchParams Params;

    Params.VolumeName = Volume->GetName();
	if (CompositeLayer.LogicAsset)
	{
		Params.Operations = CompositeLayer.LogicAsset->Operations;
	}
	else
	{
		Params.bEnableWrite = false;
		return Params;
	}
	Params.MapSize = FUintVector2(Volume->GetColumns(), Volume->GetRows());
	
	TSet<FName> RequiredInputTags;
	for (const FTCATCompositeOperation& Op : Params.Operations)
	{
		if (!Op.InputLayerTag.IsNone()) RequiredInputTags.Add(Op.InputLayerTag);
	}
	
	for (const FName& Tag : RequiredInputTags)
    {
        const FTCATGridResource* LayerRes = Volume->GetLayerResource(Tag);
        if (!LayerRes) continue;

		Params.InputGridDataMap.Add(Tag, const_cast<TArray<float>*>(&LayerRes->Grid));

        FTextureRHIRef InputTextureRHI;
        if (Volume->bAsyncReadback)
        {
            const FTCATAsyncResource& LastWrite = LayerRes->AsyncRingBuffer.PeekLastWriteResource();
            if (LastWrite.RenderTarget && LastWrite.RenderTarget->GetResource())
                InputTextureRHI = LastWrite.RenderTarget->GetResource()->GetTextureRHI();
        }
        else
        {
            if (LayerRes->RenderTarget && LayerRes->RenderTarget->GetResource())
                InputTextureRHI = LayerRes->RenderTarget->GetResource()->GetTextureRHI();
        }

        if (InputTextureRHI.IsValid())
        {
			Params.InputTextureMap.Add(Tag, InputTextureRHI);
        }
    }

    FName FinalTargetTag = CompositeLayer.CompositeLayerTag;
    FTCATGridResource* TargetRes = const_cast<FTCATGridResource*>(Volume->GetLayerResource(FinalTargetTag));
	const FTCATPredictionInfo* PredictionPtr = Volume->TagToPredictionInfo.Find(FinalTargetTag);
	const float PredictionTime = PredictionPtr ? PredictionPtr->PredictionTime : 0.0f;
    
    if (!TargetRes)
    {
        Params.bEnableWrite = false;
        return Params;
    }

    Params.MapStartPos = Volume->GetGridOrigin();
    Params.OutGridData = &TargetRes->Grid;
    Params.bIsAsync = Volume->bAsyncReadback;

	if (Volume->bRefreshWithGPU)
	{
		if (Params.bIsAsync)
		{
			FTCATAsyncResource WriteResource;
			if (!TargetRes->AsyncRingBuffer.AdvanceWriteResource(WriteResource, PredictionTime))
			{
				Params.bEnableWrite = false;
				return Params;
			}

			if (WriteResource.RenderTarget && WriteResource.RenderTarget->GetResource())
			{
				Params.OutInfluenceMapRHI = WriteResource.RenderTarget->GetResource()->GetTextureRHI();
				Params.GPUReadback = WriteResource.Readback;
				WriteResource.WriteTime = GFrameCounter;
			}
		}
		else
		{
			if (TargetRes->RenderTarget && TargetRes->RenderTarget->GetResource())
			{
				Params.OutInfluenceMapRHI = TargetRes->RenderTarget->GetResource()->GetTextureRHI();
			}
		}

		Params.bEnableWrite = Params.OutInfluenceMapRHI.IsValid();
	}
    
	Params.bForceCPUSingleThread = Volume->bForceCPUSingleThreadUpdate;

    return Params;
}

void UTCATSubsystem::RetrieveGPUResults(ATCATInfluenceVolume* Volume)
{
	SCOPE_CYCLE_COUNTER(STAT_TCAT_Readback_Retrieve);
	TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_Readback_Retrieve);
	
	if (!Volume || !Volume->bRefreshWithGPU || !Volume->bAsyncReadback) return;

	for (auto& LayerPair : Volume->InfluenceLayers)
    {
		const FName& LayerTag = LayerPair.Key;
        FTCATGridResource& LayerRes = LayerPair.Value;

        // Try to advance the read resource for this specific layer's ring buffer
        FTCATAsyncResource ReadResource;
        if (LayerRes.AsyncRingBuffer.AdvanceReadResource(ReadResource, Volume->bLogAsyncFrame))
        {
			SCOPE_CYCLE_COUNTER(STAT_TCAT_Readback_LockCopy);

			FRHIGPUTextureReadback* Readback = ReadResource.Readback;
			// Latency logging (Uses volume-level LastRequestFrame or could be per-layer)
			uint64 CurrentFrame = GFrameCounter;
			uint64 Latency = CurrentFrame - LayerRes.LastRequestFrame;
           
			if (Volume->bLogAsyncFrame)
			{
				UE_LOG(LogTCAT, Log, TEXT("Layer[%s] in Volume[%s] Readback Success"),
					*LayerTag.ToString(), *Volume->GetActorNameOrLabel());
			}
           
			// 1. Lock and Copy the GPU memory to the CPU grid
			int32 StrideInElements = 0;
			if (float* MappedData = static_cast<float*>(Readback->Lock(StrideInElements)))
			{
				const int32 Cols = Volume->GetColumns();
				const int32 Rows = Volume->GetRows();

				// Ensure the CPU grid buffer is correctly sized
				if (LayerRes.Grid.Num() != Cols * Rows)
				{
					LayerRes.Grid.SetNumUninitialized(Cols * Rows);
				}

				const int32 StrideInBytes = StrideInElements * sizeof(float);
				const uint32 RowPitch = Cols * sizeof(float);

				float CurrentMin = FLT_MAX;
				float CurrentMax = -FLT_MAX;

				float* DestBase = LayerRes.Grid.GetData();
           	
				// Copy row by row to handle potential padding (Stride) from GPU memory
				for (int32 Y = 0; Y < Rows; ++Y)
				{
					float* DestRow = DestBase + (Y * Cols);
					const uint8* SrcRow = reinterpret_cast<uint8*>(MappedData) + (Y * StrideInBytes);

					FMemory::Memcpy(DestRow, SrcRow, RowPitch);
              	
					for (int32 X = 0; X < Cols; ++X)
					{
              			const float Val = DestRow[X];
              			if (Val < CurrentMin) CurrentMin = Val;
              			if (Val > CurrentMax) CurrentMax = Val;
					}
				}

				LayerRes.MinMapValue = CurrentMin;
				LayerRes.MaxMapValue = CurrentMax;

				Readback->Unlock();
			}

			// 2. For components that failed prediction, update the MAP again using the CPU.
			const TArray<FTCATInfluenceSourceWithOwner>& DispatchedSourcesWithOwners = ReadResource.DispatchedSourcesWithOwners;
			if (DispatchedSourcesWithOwners.Num() > 0)
			{
				TArray<FTCATInfluenceSource> OldSources;
				TArray<FTCATInfluenceSource> NewSources;

				OldSources.Reserve(DispatchedSourcesWithOwners.Num());
				NewSources.Reserve(DispatchedSourcesWithOwners.Num());

				for (const FTCATInfluenceSourceWithOwner& SourceWithOwner : DispatchedSourcesWithOwners)
				{
					// Skip if component is invalid or nullptr (transient sources)
					if (!SourceWithOwner.OwnerComponent.IsValid())
						continue;

					const UTCATInfluenceComponent* Comp = SourceWithOwner.OwnerComponent.Get();
					const FTCATInfluenceSource& OldSource = SourceWithOwner.Source;

					const FVector3f CurrentLocation(Comp->ResolveWorldLocation());
					const float DistanceSq = FVector3f::DistSquared(CurrentLocation, OldSource.WorldLocation);
					const float PositionErrorTolerance = Comp->GetPositionErrorTolerance();

					if (DistanceSq > (PositionErrorTolerance * PositionErrorTolerance))
					{
						OldSources.Add(OldSource);

						// Create new source with current position
						FTCATInfluenceSource NewSource = OldSource;
						NewSource.WorldLocation = CurrentLocation;
						NewSources.Add(NewSource);

						UE_LOG(LogTCAT, Verbose,
							TEXT("Layer[%s] Component[%s] position error. Proceed position correction: %.2f cm. tolerance: %.2f cm."),
							*LayerTag.ToString(),
							*Comp->GetOwner()->GetName(),
							FMath::Sqrt(DistanceSq),
							PositionErrorTolerance
						);
					}
				}

				if (OldSources.Num() > 0)
				{
					FixInfluenceForMovedComponents(Volume, LayerTag, LayerRes, OldSources, NewSources);
				}
			}
        }
    }
}

FTCATCompositeDispatchParams UTCATSubsystem::CreateCompositeDispatchParamsForCPU(
	ATCATInfluenceVolume* Volume,
	const FTCATCompositeLayerConfig& CompositeLayer)
{
	FTCATCompositeDispatchParams Params;

	Params.VolumeName = Volume->GetName();

	if (CompositeLayer.LogicAsset)
	{
		Params.Operations = CompositeLayer.LogicAsset->Operations;
	}
	else
	{
		Params.bEnableWrite = false;
		return Params;
	}

	Params.MapSize = FUintVector2(Volume->GetColumns(), Volume->GetRows());

	// Gather required input tags
	TSet<FName> RequiredInputTags;
	for (const FTCATCompositeOperation& Op : Params.Operations)
	{
		if (!Op.InputLayerTag.IsNone())
		{
			RequiredInputTags.Add(Op.InputLayerTag);
		}
	}

	// Map input grids (CPU-only, no GPU textures needed)
	for (const FName& Tag : RequiredInputTags)
	{
		const FTCATGridResource* LayerRes = Volume->GetLayerResource(Tag);
		if (!LayerRes) continue;

		Params.InputGridDataMap.Add(Tag, const_cast<TArray<float>*>(&LayerRes->Grid));

		// Note: InputTextureMap is left empty since we're doing CPU-only updates
	}

	// Get output grid
	FName FinalTargetTag = CompositeLayer.CompositeLayerTag;
	FTCATGridResource* TargetRes = const_cast<FTCATGridResource*>(Volume->GetLayerResource(FinalTargetTag));

	if (!TargetRes)
	{
		Params.bEnableWrite = false;
		return Params;
	}

	Params.MapStartPos = Volume->GetGridOrigin();
	Params.OutGridData = &TargetRes->Grid;
	Params.bIsAsync = false; // CPU-only, no async
	Params.bEnableWrite = true;
	Params.bForceCPUSingleThread = Volume->bForceCPUSingleThreadUpdate;

	// Note: OutInfluenceMapRHI and GPUReadback are left null since we're CPU-only

	return Params;
}

void UTCATSubsystem::FixInfluenceForMovedComponents(
	ATCATInfluenceVolume* Volume,
	FName LayerTag,
	FTCATGridResource& LayerRes,
	const TArray<FTCATInfluenceSource>& OldSources,
	const TArray<FTCATInfluenceSource>& NewSources)
{
	// ============== 1. Fix base layer ==================
	
	// Create dispatch params for CPU partial update
	FTCATInfluenceDispatchParams FixParams;
	FixParams.MapStartPos = Volume->GetGridOrigin();
	FixParams.GridSize = Volume->GetCellSize();
	FixParams.MapSize = FUintVector2(Volume->GetColumns(), Volume->GetRows());
	FixParams.OutGridData = &LayerRes.Grid;
	FixParams.bEnableWrite = true;
	FixParams.CurveAtlasPixelData = GlobalAtlasPixelData;
	FixParams.GlobalHeightMapData = &Volume->HeightResource.Grid;
	FixParams.GlobalHeightMapOrigin = FVector3f(Volume->GetGridOrigin());

	FVector2D HeightSize(Volume->GetColumns() * Volume->GetCellSize(), Volume->GetRows() * Volume->GetCellSize());
	FixParams.GlobalHeightMapSize = FVector2f(HeightSize.X, HeightSize.Y);
	FixParams.ProjectionFlags = Volume->GetProjectionMask(LayerTag);
	FixParams.AtlasWidth = ATLAS_TEXTURE_WIDTH;

	const FTCATBaseLayerConfig* LayerConfig = Volume->CachedBaseLayerMap.Find(LayerTag);
	if (LayerConfig)
	{
		FixParams.RayMarchStepSize = FMath::Max(LayerConfig->RayMarchSettings.LineOfSightStepSize, 1.0f);
		FixParams.RayMarchMaxSteps = FMath::Max(LayerConfig->RayMarchSettings.LineOfSightMaxSteps, 1);
	}

	// Execute CPU partial update (remove old, add new)
	FTCATInfluenceDispatcher::DispatchCPU_Partial(FixParams, OldSources, NewSources);

	UE_LOG(LogTCAT, Log,
		TEXT("Fixed %d components for Layer[%s] using CPU partial update"),
		OldSources.Num(), *LayerTag.ToString()
	);

	// =========== 2. Fix affected composite layers ================

	TSet<int32> AffectedCellIndices;
	const int32 MapWidth = Volume->GetColumns();
	const int32 MapHeight = Volume->GetRows();
	const FVector2f MapOriginXY(Volume->GetGridOrigin().X, Volume->GetGridOrigin().Y);
	const float HalfGrid = Volume->GetCellSize() * 0.5f;

	// Gather all affected cell indices from NewSources
	for (const FTCATInfluenceSource& Src : NewSources)
	{
		const FVector SourcePos(Src.WorldLocation);
		const float RadiusSq = Src.InfluenceRadius * Src.InfluenceRadius;

		const FVector2f SourceXY(SourcePos.X, SourcePos.Y);
		const FVector2f RelativePos = SourceXY - MapOriginXY;

		const int32 MinX = FMath::Max(0, FMath::FloorToInt((RelativePos.X - Src.InfluenceRadius) / Volume->GetCellSize()));
		const int32 MaxX = FMath::Min(MapWidth - 1, FMath::CeilToInt((RelativePos.X + Src.InfluenceRadius) / Volume->GetCellSize()));
		const int32 MinY = FMath::Max(0, FMath::FloorToInt((RelativePos.Y - Src.InfluenceRadius) / Volume->GetCellSize()));
		const int32 MaxY = FMath::Min(MapHeight - 1, FMath::CeilToInt((RelativePos.Y + Src.InfluenceRadius) / Volume->GetCellSize()));

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const int32 Index = Y * MapWidth + X;
				AffectedCellIndices.Add(Index);
			}
		}
	}

	if (AffectedCellIndices.Num() == 0)
	{
		return;
	}

	TArray<int32> AffectedCellArray = AffectedCellIndices.Array();

	// Find and update composite layers that use this base layer
	for (const FTCATCompositeLayerConfig& CompositeLayer : Volume->CompositeLayers)
	{
		if (!CompositeLayer.LogicAsset || CompositeLayer.LogicAsset->Operations.Num() == 0)
		{
			continue;
		}

		// Check if this composite layer uses the corrected base layer
		bool bUsesThisLayer = false;
		for (const FTCATCompositeOperation& Op : CompositeLayer.LogicAsset->Operations)
		{
			if (Op.InputLayerTag == LayerTag)
			{
				bUsesThisLayer = true;
				break;
			}
		}

		if (!bUsesThisLayer)
		{
			continue;
		}

		// Create composite dispatch params
		FTCATCompositeDispatchParams CompositeParams = CreateCompositeDispatchParamsForCPU(Volume, CompositeLayer);
		if (!CompositeParams.bEnableWrite)
		{
			continue;
		}

		// Execute partial composite update
		FTCATInfluenceDispatcher::DispatchCPU_Composite_Partial(CompositeParams, AffectedCellArray);

		UE_LOG(LogTCAT, Log,
			TEXT("Updated composite layer[%s] for %d affected cells"),
			*CompositeLayer.CompositeLayerTag.ToString(), AffectedCellArray.Num()
		);
	}
}

void UTCATSubsystem::InitializeStaticGlobalCurveAtlas()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    
    TArray<FAssetData> AssetDataList;
    FARFilter Filter;
    Filter.ClassPaths.Add(UCurveFloat::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(*CURVE_SEARCH_PATH); // "/TCAT/Curves"

    AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);

    TArray<UCurveFloat*> AllCurves;
    GlobalCurveIDMap.Empty();

	if (!DefaultLinearCurveAsset)
	{
		DefaultLinearCurveAsset = NewObject<UCurveFloat>(this, TEXT("TCAT_DefaultLinearCurve_1_to_0"), RF_Transient);

		// Build curve keys: at time 0 => 1, at time 1 => 0
		FRichCurve& RC = DefaultLinearCurveAsset->FloatCurve;
		RC.Reset();

		RC.AddKey(0.0f, 1.0f);
		RC.AddKey(1.0f, 0.0f);
	}

	AllCurves.Add(DefaultLinearCurveAsset.Get());
	GlobalCurveIDMap.Add(DefaultLinearCurveAsset.Get(), 0);
	
    for (const FAssetData& AssetData : AssetDataList)
    {
        if (AllCurves.Num() >= MAX_ATLAS_HEIGHT)
        {
            UE_LOG(LogTemp, Warning, TEXT("TCAT: Curve limit reached (%d). Stopping scan."), MAX_ATLAS_HEIGHT);
            break;
        }
    	
        if (UCurveFloat* CurveAsset = Cast<UCurveFloat>(AssetData.GetAsset()))
        {
            if (!GlobalCurveIDMap.Contains(CurveAsset))
            {
                int32 NewID = AllCurves.Num();
                AllCurves.Add(CurveAsset);
                GlobalCurveIDMap.Add(CurveAsset, NewID);
            }
        }
    }
	
    const int32 AtlasWidth = ATLAS_TEXTURE_WIDTH;
    const int32 AtlasHeight = FMath::Max(AllCurves.Num(), 1); 

	if (AllCurves.Num() > 0)
	{
		UTCATMathLibrary::BuildCurveAtlasData(AllCurves, AtlasWidth, GlobalAtlasPixelData);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TCAT: No curves found! Creating a default Linear Atlas."));
        
		GlobalAtlasPixelData.SetNumUninitialized(AtlasWidth);
		for (int32 i = 0; i < AtlasWidth; ++i)
		{
			GlobalAtlasPixelData[i] = (float)i / (float)(AtlasWidth - 1);
		}
	}
	
    ENQUEUE_RENDER_COMMAND(CreateStaticAtlas)(
        [this, PixelData = GlobalAtlasPixelData, AtlasWidth, AtlasHeight](FRHICommandListImmediate& RHICmdList)
        {
            if (GlobalCurveAtlasRHI.IsValid()) GlobalCurveAtlasRHI.SafeRelease();

            const FRHITextureCreateDesc Desc =
                FRHITextureCreateDesc::Create2D(TEXT("TCAT_StaticGlobalAtlas"), AtlasWidth, AtlasHeight, PF_R32_FLOAT)
                .SetFlags(ETextureCreateFlags::ShaderResource);
            
            GlobalCurveAtlasRHI = RHICreateTexture(Desc);

            FUpdateTextureRegion2D Region(0, 0, 0, 0, AtlasWidth, AtlasHeight);
            uint32 Pitch = AtlasWidth * sizeof(float);
            
            RHICmdList.UpdateTexture2D(GlobalCurveAtlasRHI, 0, Region, Pitch, (const uint8*)PixelData.GetData());
        });
}