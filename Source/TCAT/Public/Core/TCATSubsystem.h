// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TCATTypes.h"
#include "Query/TCATQueryProcessor.h"
#include "Query/TCATQueryTypes.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Subsystems/WorldSubsystem.h"
#include "TCATSubsystem.generated.h"

enum class ETCATCompareType : uint8;
class ATCATInfluenceVolume;
class UTCATInfluenceComponent;
class UCurveFloat;
struct FTCATInfluenceDispatchParams;
struct FTCATCompositeDispatchParams;

struct FTransientSourceWrapper
{
	FName MapTag;
	FTCATInfluenceSource Data;
	UCurveFloat* CurveAsset = nullptr;
	UCurveFloat* StrengthCurveOverTime = nullptr;
	float ElapsedTime = 0.0f;
	bool bDestroyIfZeroStrength = true;
};

/**
 * The central management hub for the Influence Map system.
 * It gathers influence data from persistent ActorComponents and transient requests,
 * categorizing them by FName tags (e.g., 'Ally', 'Enemy', 'Explosion') for GPU processing.
 */
UCLASS()
class TCAT_API UTCATSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

// =======================================================================
// Public API - Core Lifecycle
// =======================================================================
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTCATSubsystem, STATGROUP_Tickables); }

// =======================================================================
// Public API - Volume & Component & Transient Influence Management
// =======================================================================
public:
	/**
	 * Registers a persistent influence volume.
	 * @param InVolume  Pointer to the volume that will report its influence data every frame.
	 */
	void RegisterVolume(ATCATInfluenceVolume* InVolume);
	
	/**
	 * Unregisters a volume.
	 * @param InVolume  Pointer to the volume to be removed from the registry.
	 */
	void UnregisterVolume(ATCATInfluenceVolume* InVolume);
	
	/**
	 * Retrieves a influence volume by its map group tag.
	 * @param MapTag The unique identifier for the influence map group.
	 * @return Pointer to the first influence volume found matching the MapTag, or nullptr if none found.
	 */
	UFUNCTION(BlueprintCallable, Category = "TCAT")
	ATCATInfluenceVolume* GetInfluenceVolume(FName MapTag);
	
	/**
 	 * Registers a persistent influence component to a specific map group.
	 * @param InComp  Pointer to the component that will report its influence data every frame.
	 */
	void RegisterComponent(UTCATInfluenceComponent* InComp);
	
	/**
	 * Unregisters a component from the specified map group.
	 * @param InComp  Pointer to the component to be removed from the registry.
	 */
	void UnregisterComponent(UTCATInfluenceComponent* InComp);
	
	/**
	 * Retrieves persistent influence components matching the MapTag within a specific spatial bound.
	 * Unlike GetSourcesByTag, this provides access to the actual component objects, allowing the Volume
	 * to read asset data (e.g., UCurveFloat*) needed for generating the Curve Atlas.
	 * @param MapTag          The tag used to filter relevant influence components.
	 * @param InVolumeBounds  The 3D bounding box (AABB) used to spatially filter the components.
	 * @param OutComponents   [Out] Array where the found influence components will be stored.
	 */
	void GetComponentsByTag(FName MapTag, const FBox& InVolumeBounds, TArray<UTCATInfluenceComponent*>& OutComponents);

	/**
	 * Retrieves all influence components within a specific spatial bound, regardless of their MapTag.
	 * This is highly efficient for multi-layered volumes as it performs spatial filtering only once.
	 * @param InVolumeBounds  The 3D bounding box (AABB) of the searching volume.
	 * @param OutComponents   [Out] Array to store components that intersect with the bounds.
	 */
	void GetAllComponentsInBounds(const FBox& InVolumeBounds, TArray<UTCATInfluenceComponent*>& OutComponents);
	
	/**
	 * Adds a one-time, transient influence source to a map group.
	 * Useful for non-persistent events like explosions or bullet impacts.
	 * @param MapTag    The target map group where this temporary influence will be applied.
	 * @param InSource  Data structure containing position, radius, and strength of the source.
	 * @param InStrengthCurveOverTime Curve asset defining the influence strength over time.
	 * @param bDestroyIfZeroStrength If true, the source will be removed once its strength curve reaches zero.
	 * @param InCurve   Curve asset defining the influence falloff over distance.
	 */
	UFUNCTION(BlueprintCallable, Category = "TCAT")
	void AddTransientInfluence(FName MapTag, const FTCATInfluenceSource& InSource, UCurveFloat* InStrengthCurveOverTime, bool bDestroyIfZeroStrength = true, UCurveFloat* InCurve = nullptr);
	
	/**
	 * Retrieves all active transient sources within a specific spatial bound.
	 * @param InVolumeBounds     The 3D bounding box (AABB) of the searching volume.
	 * @param OutWrapperSources  [Out] Array to store transient wrappers that intersect with the bounds.
	 */
	void GetAllTransientSourcesInBounds(const FBox& InVolumeBounds, TArray<FTransientSourceWrapper>& OutWrapperSources);
	
private:
	void SyncVolumeWithExistingComponents(ATCATInfluenceVolume* Volume);
	void AttachComponentTagsToVolumes(UTCATInfluenceComponent* InComp);	
	
// =======================================================================
// Public API - Global Curve System
// =======================================================================
public:
	/**
	 * Get Curve ID from Global Curve Atlas RHI
	 * @param InCurve  
	 */
	int32 GetCurveID(UCurveFloat* InCurve);

	TArray<float>& GetCurveAtlasData() { return GlobalAtlasPixelData; }

	int32 GetMaxMapResolution() const { return CachedMaxMapResolution; }
		
// =======================================================================
// Public API - Query System
// =======================================================================
public:
	/**
	 * Submits a batch query request for processing.
	 * @param NewQuery The batch query containing sample points and query parameters.
	 * * @return A unique Query ID (Handle). This ID is required if you wish to cancel the query later via CancelQuery().
	 */
	uint32 RequestBatchQuery(FTCATBatchQuery&& NewQuery);
	
	/**
	 * Attempts to cancel a pending query using its specific Handle ID.
	 * If the query is found in the waiting queue, it will be marked as cancelled and its result callback will not be broadcast.
	 * * @note If the query is already being processed or has finished, this function will have no effect.
	 * @param QueryID - The unique handle returned by EnqueueQuery.
	 */
	void CancelBatchQuery(uint32 QueryID);

// =========================================================================================================
// Protected - Configuration Cache
// =======================================================================
protected:
	/** Cached maximum resolution from global settings to prevent GPU crashes. */
	int32 CachedMaxMapResolution = 1024;
	
// =========================================================================================================
// Private - Volume & Component Registry
// =======================================================================
#pragma region Registry
private:
	void VLogInfluence();
	
	/** Master list of all registered influence volumes in the world. */
	UPROPERTY()
	TSet<TObjectPtr<ATCATInfluenceVolume>> RegisteredVolumes;
	
	/** Groups persistent volumes by their MapTag for optimized lookups. */
	TMap<FName, TSet<ATCATInfluenceVolume*>> MapGroupedVolumes;
	
	/** Master list of all registered influence components in the world. */
	UPROPERTY()
	TSet<TObjectPtr<UTCATInfluenceComponent>> RegisteredComponents;
	
	/** Groups persistent components by their MapTag for optimized lookups. */
	TMap<FName, TSet<UTCATInfluenceComponent*>> MapGroupedComponents;

	/** Stores one-frame transient influence data. */
	TArray<FTransientSourceWrapper> AllTransientSources;
#pragma endregion

// =======================================================================	
// Private - GPU Dispatch
// =======================================================================
#pragma region Dispatch
private:
	FTCATInfluenceDispatchParams CreateDispatchParams(ATCATInfluenceVolume* Volume, FName LayerTag);
	FTCATCompositeDispatchParams CreateCompositeDispatchParams(ATCATInfluenceVolume* Volume, const FTCATCompositeLayerConfig& CompositeLayer);
	void RetrieveGPUResults(ATCATInfluenceVolume* Volume);

	// ADDED: Lightweight version for CPU-only composite updates (no RingBuffer advance)
	FTCATCompositeDispatchParams CreateCompositeDispatchParamsForCPU(
		ATCATInfluenceVolume* Volume, const FTCATCompositeLayerConfig& CompositeLayer);

	void FixInfluenceForMovedComponents(
		ATCATInfluenceVolume* Volume,
		FName LayerTag,
		FTCATGridResource& LayerRes,
		const TArray<FTCATInfluenceSource>& OldSources,
		const TArray<FTCATInfluenceSource>& NewSources);
#pragma endregion

// =======================================================================	
// Private - Global Curve Atlas System
// =======================================================================
#pragma region CurveAtlas
private:
	void InitializeStaticGlobalCurveAtlas();
	
	UPROPERTY()
	TMap<UCurveFloat*, int32> GlobalCurveIDMap;

	UPROPERTY()
	TObjectPtr<UCurveFloat> DefaultLinearCurveAsset = nullptr;
	
	TArray<float> GlobalAtlasPixelData;
	FTextureRHIRef GlobalCurveAtlasRHI;
	
	// Constants
	const int32 ATLAS_TEXTURE_WIDTH = 256;
	const int32 MAX_ATLAS_HEIGHT = 256; // For now
	const FString CURVE_SEARCH_PATH = TEXT("/TCAT/TCAT/Curves");
#pragma endregion 
	
// =======================================================================	
// Private - Query Processing
// =======================================================================
private:
	FTCATQueryProcessor QueryProcessor;

// =======================================================================	
// Private - Adaptive GPU/CPU Mode Switching
// =======================================================================
#pragma region AdaptiveSwitching
private:
	/** Variables prefixed with `Cached` are synchronized with the values set in TCATSettings. */

	TFuture<float> CPUMeasurementTask;
	bool bIsMeasuringCPU = false;
	bool bShouldMeasureCPUMode = false;

	double CachedAdaptiveModeSwitchingDelay = 5.0;
	float CachedModeSwitchingSafetyMultiplier = 1.0f;
	float CachedWaitTimeMsThresholdForGPUMode = 2.5f;

	/** The initial time at which mode transition condition checks begin. */
	double AdaptiveModeSwitchingStartSeconds = 0.0;

	/** Indicates if this is the first check for adaptive mode switching. */
	bool bIsFirstCheck = true;

	/** If true, all Volumes with bAdaptivelySwitchRefreshMode set to true must update their maps using the GPU.
	Otherwise, those Volumes must update their maps using the CPU.*/
	bool bRefreshWithGPUForAdaptiveVolumes = true;

	float CurTickTimeMs = 0.0;
	float CPUModeTickTimeMs = 0.0;
	float GPUModeTickTimeMs = 0.0;

	double ElapsedTimeSinceConditionCheckStarted = 0.0;
	double CachedSwitchConditionCheckDuration = 5.0;

	uint32 SatisfiedFrameCount = 0;
	uint32 UnsatisfiedFrameCount = 0;

	float CachedRequiredSatisfactionRatio = 0.8f;

	uint64 CachedSourceCountChangeThreshold = 50;

	/** The total number of sources measured during the last CPU mode measurement. */
	uint64 LastMeasuredTotalSourceCount = 0;
#pragma endregion 
};
