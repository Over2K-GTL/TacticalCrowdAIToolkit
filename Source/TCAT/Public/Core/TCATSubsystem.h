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
class FTCATQueryBuilder;

struct FTCATInfluenceDispatchParams;
struct FTCATCompositeDispatchParams;

/**
 * Runtime container for transient (one-shot) influence sources.
 *
 * Transient sources are not backed by ActorComponents; they are authored via API calls (Blueprint/C++).
 * They can optionally use:
 * - CurveAsset: distance falloff curve (stored in the global atlas via CurveTypeIndex at dispatch time)
 * - StrengthCurveOverTime: time-varying strength (e.g., explosion decay)
 */
struct FTransientSourceWrapper
{
	/** Target influence map to which this transient source contributes. */
	FName MapTag;
	
	/** GPU/CPU source data (world location, radius, strength, etc.). */
	FTCATInfluenceSource Data;
	
	/** Optional falloff curve asset used to pick a CurveTypeIndex (global atlas row). */
	UCurveFloat* CurveAsset = nullptr;
	
	/** Required: drives Data.Strength over time. */
	UCurveFloat* StrengthCurveOverTime = nullptr;

	/** Elapsed lifetime in seconds since being added. */
	float ElapsedTime = 0.0f;

	/** If true, the source is removed when the evaluated strength becomes ~0. */
	bool bDestroyIfZeroStrength = true;
};

/**
 * The central management hub for the TCAT Influence Map system.
 *
 * High-level responsibilities:
 * - Registers and tracks Influence Volumes and Influence Components in the current UWorld
 * - Builds per-volume, per-layer(=map) dispatch data each tick (sources, atlas, height map, flags)
 * - Executes map updates on GPU (RDG compute) or CPU (ParallelFor) depending on volume settings
 * - Maintains a global curve atlas (CPU pixels + GPU texture) shared across all volumes
 * - Hosts the QueryProcessor for asynchronous/batched queries (BT tasks, C++, Blueprint)
 *
 * Typical usage (plugin user):
 * - Place ATCATInfluenceVolume actors in the level, configure base maps and composite maps in Details
 * - Add UTCATInfluenceComponent to actors to contribute influence into one or more map tags
 * - (Optional) Spawn transient influences via AddTransientInfluence for short-lived events
 * - Consume maps via query APIs (BT nodes / Blueprint async actions / C++)
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

	/**
	 * Per-frame update:
	 * - Prepares dispatch batches for all registered volumes/layers
	 * - Executes GPU batch dispatch (render thread) and/or CPU fallbacks
	 * - Advances transient influences
	 * - Runs adaptive GPU/CPU switching logic (if enabled in settings and volume flags)
	 */
	virtual void Tick(float DeltaTime) override;
	
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTCATSubsystem, STATGROUP_Tickables); }

// =======================================================================
// Public API - Volume & Component & Transient Influence Management
// =======================================================================
public:
	/**
	 * Registers a persistent influence volume.
	 * Volumes define the spatial domain (grid origin/size/resolution) and the set of layers to update.
	 *
	 * @param InVolume  Pointer to the volume that will report its influence data every frame.
	 */
	void RegisterVolume(ATCATInfluenceVolume* InVolume);
	
	/**
	 * Unregisters a volume and removes it from tag group lookups.
	 * 
	 * @param InVolume  Pointer to the volume to be removed from the registry.
	 */
	void UnregisterVolume(ATCATInfluenceVolume* InVolume);
	
	/**
	 * Retrieves a influence volume by its map group tag.
	 * If multiple volumes share the same tag, the first valid volume found is returned.
	 * For deterministic selection, ensure unique tags per volume group in your content.
	 * 
	 * @param MapTag The unique identifier for the influence map group.
	 * @return Pointer to the first influence volume found matching the MapTag, or nullptr if none found.
	 */
	UFUNCTION(BlueprintCallable, Category = "TCAT")
	ATCATInfluenceVolume* GetInfluenceVolume(FName MapTag);
	
	/**
	 * Registers a persistent influence component.
	 * Components contribute one or more influence entries (layer tag -> radius/strength/curve).
	 *
	 * @param InComp  Pointer to the component that will report its influence data every frame.
	 */
	void RegisterComponent(UTCATInfluenceComponent* InComp);
	
	/**
	 * Unregisters a component from the specified map group.
	 * 
	 * @param InComp  Pointer to the component to be removed from the registry.
	 */
	void UnregisterComponent(UTCATInfluenceComponent* InComp);
	
	/**
	 * Retrieves persistent influence components matching the MapTag within a specific spatial bound.
	 * Unlike GetSourcesByTag, this provides access to the actual component objects, allowing the Volume
	 * to read asset data (e.g., UCurveFloat*) needed for generating the Curve Atlas.
	 *
	 * Spatial filtering rule:
	 * - A component is included if its distance to bounds <= (any of its layer radii)^2.
	 * 
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
	void AddTransientInfluence(UPARAM(meta = (GetOptions = "TCAT.TCATSettings.GetAllTagOptions")) FName MapTag, const FTCATInfluenceSource& InSource, UCurveFloat* InStrengthCurveOverTime, bool bDestroyIfZeroStrength = true, UCurveFloat* InCurve = nullptr);
	
	/**
	 * Retrieves all active transient sources within bounds.
	 * 
	 * @param InVolumeBounds     The 3D bounding box (AABB) of the searching volume.
	 * @param OutWrapperSources  [Out] Array to store transient wrappers that intersect with the bounds.
	 */
	void GetAllTransientSourcesInBounds(const FBox& InVolumeBounds, TArray<FTransientSourceWrapper>& OutWrapperSources);
	
private:
	/**
	 * Ensures a newly registered volume knows about already-existing components in its bounds,
	 * and creates any missing base layers required by those components' tags.
	 */
	void SyncVolumeWithExistingComponents(ATCATInfluenceVolume* Volume);
	
	/**
	 * Attempts to attach component tags to nearby volumes during component registration.
	 * This can auto-create missing base layers on volumes when relevant components are discovered.
	 */
	void AttachComponentTagsToVolumes(UTCATInfluenceComponent* InComp);	
	
// =======================================================================
// Public API - Global Curve System
// =======================================================================
public:
	/**
	 * Returns a curve row index (CurveTypeIndex) used in the Global Curve Atlas.
	 *
	 * Rules:
	 * - Returns 0 for null curves or curves not found in the configured scan path (editor builds may log warnings).
	 * - Curve IDs are stable for the current runtime session once the atlas is built.
	 *
	 * @param InCurve Curve asset to resolve.
	 * @return Atlas row index for sampling (CurveTypeIndex).
	 */
	int32 GetCurveID(UCurveFloat* InCurve);

	/** CPU pixel data backing the global curve atlas (row-major). */
	TArray<float>& GetCurveAtlasData() { return GlobalAtlasPixelData; }

	/** Cached map resolution limit from Project Settings (safety clamp for RT/texture creation). */
	int32 GetMaxMapResolution() const { return CachedMaxMapResolution; }
		
// =======================================================================
// Public API - Query System
// =======================================================================
public:
	/**
	 * Submits a batch query request for processing.
	 *
	 * Typical callers:
	 * - Behavior Tree tasks/services (async query actions)
	 * - C++ gameplay systems that want top-K points, conditions, or spatial queries
	 * 
	 * @param NewQuery The batch query containing sample points and query parameters.
	 * * @return A unique Query ID (Handle). This ID is required if you wish to cancel the query later via CancelQuery().
	 */
	uint32 RequestBatchQuery(FTCATBatchQuery&& NewQuery);
	
	/**
	 * Attempts to cancel a pending query using its specific Handle ID.
	 * 
	 * If the query is found in the waiting queue, it will be marked as cancelled and its result callback will not be broadcast.
	 * * @note If the query is already being processed or has finished, this function will have no effect.
	 * @param QueryID - The unique handle returned by EnqueueQuery.
	 */
	void CancelBatchQuery(uint32 QueryID);

	/**
	 * [C++ API] Creates a fluent query builder for TCAT.
	 *
	 * This is the recommended entry point for gameplay programmers who want TCAT queries without EQS/BT nodes.
	 *
	 * Example
	 *   TCAT->MakeQuery("Enemy")
	 *       .From(MyPawn)
	 *       .SearchRadius(800.f)
	 *       .FindLowest()
	 *       .Where(0.2f, ETCATCompareType::LessOrEqual)
	 *       .Top(3)
	 *       .SubmitAsync([](const FTCATQueryResultArray& Results){ ... });
	 */
	FTCATQueryBuilder MakeQuery(FName MapTag);

	/**
	 * [Internal] Used by FTCATQueryBuilder::RunImmediate().
	 * Executes a single query synchronously on the Game Thread and returns the best result (if any).
	 *
	 * Warning
	 * - Synchronous scans can stall the frame if SearchRadius is large or filters are expensive.
	 * - Prefer async submission for AI-style "search area" queries.
	 */
	bool ProcessQueryImmediate(FTCATBatchQuery& InQuery, FTCATSingleResult& OutResult);

	/**
	 * [Internal] Used by FTCATQueryBuilder::RunImmediateMulti().
	 * Executes a query synchronously on the Game Thread and returns up to MaxResults results.
	 */
	bool ProcessQueryImmediateMulti(FTCATBatchQuery& InQuery, TArray<FTCATSingleResult>& OutResults);

// =======================================================================	
// Public API - Adaptive GPU/CPU Mode Switching
// =======================================================================
public:
	bool IsRefreshingWithGPUForAdaptiveVolumes() const { return bRefreshWithGPUForAdaptiveVolumes; }

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
	/** Visual Logger helper (ENABLE_VISUAL_LOG only). */
	void VLogInfluence();
	
	/** Master list of all registered influence volumes in the world. */
	UPROPERTY()
	TSet<TObjectPtr<ATCATInfluenceVolume>> RegisteredVolumes;
	
	/** Groups persistent volumes by their MapTag for optimized lookups. */
	TMap<FName, TSet<ATCATInfluenceVolume*>> MapGroupedVolumes;
	
	/** Master list of all registered influence components in the world. */
	UPROPERTY(Transient)
	TSet<UTCATInfluenceComponent*> RegisteredComponents;
	
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
	/**
	 * Builds dispatch parameters for a base layer update for the given volume/tag.
	 * This includes:
	 * - Gathering sources for the tag within the volume bounds
	 * - Mapping curve IDs into CurveTypeIndex
	 * - Selecting height/visibility flags and resources
	 * - Preparing output targets (ring buffer write resource) for GPU mode
	 */
	FTCATInfluenceDispatchParams CreateDispatchParams(ATCATInfluenceVolume* Volume, FName LayerTag);
	FTCATCompositeDispatchParams CreateCompositeDispatchParams(ATCATInfluenceVolume* Volume, const FTCATCompositeLayerConfig& CompositeLayer);
	/**
	 * Consumes completed GPU readbacks and writes results into each layer's CPU grid.
	 * Also performs post-correction when predicted positions drift beyond tolerance.
	 */
	void RetrieveGPUResults(ATCATInfluenceVolume* Volume);

	/**
	 * Lightweight CPU-only composite params builder:
	 * - Does not advance ring buffers
	 * - Does not bind GPU textures
	 * Used for partial correction workflows after GPU readback.
	 */
	FTCATCompositeDispatchParams CreateCompositeDispatchParamsForCPU(ATCATInfluenceVolume* Volume, const FTCATCompositeLayerConfig& CompositeLayer);

	/**
	 * Applies a CPU partial correction to a base layer and then updates any dependent composite layers
	 * for the affected cells only (when possible).
	 *
	 * This is used when GPU prediction was used, but some components moved unexpectedly.
	 */
	void FixInfluenceForMovedComponents(ATCATInfluenceVolume* Volume, FName LayerTag,
		FTCATGridResource& LayerRes, const TArray<FTCATInfluenceSource>& OldSources, const TArray<FTCATInfluenceSource>& NewSources);
#pragma endregion

// =======================================================================	
// Private - Global Curve Atlas System
// =======================================================================
#pragma region CurveAtlas
private:
	/**
	 * Scans curve assets under the configured search path, builds a CPU atlas buffer and uploads a GPU texture.
	 * Curve row indices become CurveTypeIndex values used in sources for both CPU and GPU sampling.
	 */
	void InitializeStaticGlobalCurveAtlas();

	/** Runtime mapping from curve asset -> atlas row index (CurveTypeIndex). */
	UPROPERTY()
	TMap<UCurveFloat*, int32> GlobalCurveIDMap;

	/**
	 * Built-in fallback curve (time 0 -> 1, time 1 -> 0).
	 * Always included as atlas row 0.
	 */
	UPROPERTY()
	TObjectPtr<UCurveFloat> DefaultLinearCurveAsset = nullptr;

	/** CPU pixels for the atlas (row-major). */
	TArray<float> GlobalAtlasPixelData;
	/** GPU texture for the atlas (PF_R32_FLOAT). */
	FTextureRHIRef GlobalCurveAtlasRHI;
	
	// Constants
	const int32 ATLAS_TEXTURE_WIDTH = 256;
	const int32 MAX_ATLAS_HEIGHT = 256; // For now
	FString CachedCurveSearchPath = TCATContentPaths::CuratedCurvePath;

#pragma endregion 
	
// =======================================================================	
// Private - Query Processing
// =======================================================================
private:
	/** Internal query processor used by async actions and batched requests. */
	FTCATQueryProcessor QueryProcessor;

// =======================================================================	
// Private - Adaptive GPU/CPU Mode Switching
// =======================================================================
#pragma region AdaptiveSwitching
private:
	/**
	 * Async measurement task used to estimate CPU cost of updating the same workload.
	 * Results feed into adaptive switching decisions.
	 */
	TFuture<float> CPUMeasurementTask;

	/** True while CPUMeasurementTask is running. */
	bool bIsMeasuringCPU = false;

	/** When true, the subsystem will launch a CPU measurement pass for adaptive volumes. */
	bool bShouldMeasureCPUMode = false;

	/** Variables prefixed with `Cached` are synchronized with the values set in TCATSettings. */

	/** Delay after BeginPlay before adaptive switching checks start (seconds). From Project Settings. */
	double CachedAdaptiveModeSwitchingDelay = 5.0;
	
	/** GPU mode trigger threshold based on GameThread wait time (ms). From Project Settings. */
	float CachedWaitTimeMsThresholdForGPUMode = 0.5f;

	/** The initial time at which mode transition condition checks begin. */
	double AdaptiveModeSwitchingStartSeconds = 0.0;

	/** Indicates if this is the first check for adaptive mode switching. */
	bool bIsFirstCheck = true;

	/**
	 * Current adaptive decision:
	 * - true  => adaptive volumes refresh with GPU
	 * - false => adaptive volumes refresh with CPU
	 *
	 * Individual volumes can opt into adaptive behavior with bAdaptivelySwitchRefreshMode.
	 */
	bool bRefreshWithGPUForAdaptiveVolumes = true;

	/** Measured Pass time (ms) for the current frame. */
	float CurPassTimeMs = 0.0;

	/** Last measured/estimated CPU mode pass time (ms). */
	float CPUModePassTimeMs = 0.0;

	/** Last measured/estimated GPU mode pass time (ms). */
	float GPUModePassTimeMs = 0.0;

	/** Accumulator for how long we've been evaluating switching conditions (seconds). */
	double ElapsedTimeSinceConditionCheckStarted = 0.0;
	
	/** Evaluation window length (seconds). From Project Settings. */
	double CachedSwitchConditionCheckDuration = 5.0;

	/** Condition satisfaction counters within the window. */
	uint32 SatisfiedFrameCount = 0;
	uint32 UnsatisfiedFrameCount = 0;

	/** Required ratio of satisfied frames to trigger a mode switch. From Project Settings. */
	float CachedRequiredSatisfactionRatio = 0.8f;

	/** Source-count delta threshold that forces re-measurement (to keep decisions workload-aware). */
	uint64 CachedSourceCountChangeThreshold = 50;

	/** The total number of sources measured during the last CPU mode measurement. */
	uint64 LastMeasuredTotalSourceCount = 0;
#pragma endregion 
};