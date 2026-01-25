// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TCATHeightMapModule.h"
#include "Core/TCATTypes.h"
#include "GameFramework/Volume.h"
#include "Simulation/TCATCompositeLogic.h"
#include "Simulation/TCATGridResource.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "TCATInfluenceVolume.generated.h"

class UTCATInfluenceComponent;
class UTextureRenderTarget2D;
struct FTransientSourceWrapper;
class FTextureRenderTargetResource;

UENUM(BlueprintType)
enum class ETCATDebugDrawMode : uint8
{
    None        UMETA(DisplayName = "None"),
    VisibleOnly UMETA(DisplayName = "Visible Layers Only"), // Default
    All         UMETA(DisplayName = "All Layers"),
};

USTRUCT(BlueprintType)
struct FTCATRaymarchingSettings
{
    GENERATED_BODY()

    /** * [Adaptive Precision]
      * The *Minimum* distance traveled per raymarch step. 
      * If the target is far away and exceeds MaxSteps, this step size will effectively increase 
      * to ensure the ray reaches the target within the MaxSteps budget.
      * (Closer objects = High Precision, Farther objects = Lower Precision)
      */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TCAT", meta=(ClampMin="1.0", UIMin="1.0"))
    float LineOfSightStepSize = 50.0f;

    /**
      * [Performance Cap]
      * The hard limit on how many texture samples can be taken per check.
      * This guarantees the GPU cost never exceeds a certain amount, regardless of distance.
      */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TCAT", meta=(ClampMin="1", UIMin="1"))
    int32 LineOfSightMaxSteps = 32;
};

USTRUCT(BlueprintType)
struct FTCATBaseLayerConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (GetOptions = "GetAllTagOptions"))
    FName BaseLayerTag;

    /** 3D rules for this layer */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (Bitmask, BitmaskEnum = "/Script/TCAT.ETCATProjectionFlag"))
    int32 ProjectionMask = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Config", meta=(DisplayName="Advanced"))
    FTCATRaymarchingSettings RayMarchSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FTCATLayerDebugSettings DebugSettings;
};

USTRUCT(BlueprintType)
struct FTCATCompositeLayerConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta=(GetOptions="GetCompositeTagOptions"))
    FName CompositeLayerTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition")
    TObjectPtr<UTCATCompositeLogic> LogicAsset;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FTCATLayerDebugSettings DebugSettings;
};

UCLASS(
    // 1. HideCategories
    HideCategories = (Collision, HLOD, Physics, Networking, Replication, Input, Cooking, LevelInstance, DataLayers, WorldPartition, LOD, Streaming),
    
    // 2. PrioritizeCategories
    // After Transform & BrushSettings
    meta = (PrioritizeCategories = "Transform BrushSettings TCAT")
)
class TCAT_API ATCATInfluenceVolume : public AVolume
{
    GENERATED_BODY()

    friend class UTCATSubsystem;
    friend struct FTCATHeightMapModule;

public:
    ATCATInfluenceVolume();
    
public:
    /** Returns the world-space bounding box of this volume. */
    FORCEINLINE const FBox& GetCachedBounds() const { return CachedBounds; }

    /** Returns the size of a single grid cell. */
    FORCEINLINE float GetCellSize() const { return CellSize; }

    /** Returns the number of columns (X) and rows (Y) in the grid. */
    FORCEINLINE int32 GetColumns() const { return GridResolution.X; }
    FORCEINLINE int32 GetRows() const { return GridResolution.Y; }
    
    /** Retrieves an influence or height value from a specific layer at grid coordinates.
     * @param LayerTag The tag of the layer (e.g., "Enemy", "Ally", or "GlobalHeight").
     */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    float GetInfluenceFromGrid(FName LayerTag, int32 InX, int32 InY) const;
    
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    FVector GetGridOrigin() const;

    UFUNCTION(BlueprintCallable, Category = "TCAT")
    float GetGridHeightIndex(FIntPoint CellIndex) const;

    UFUNCTION(BlueprintCallable, Category = "TCAT")
    float GetGridHeightWorldPos(FVector WorldPos) const;
    
    /** Retrieves the 3D Setting mask for a specific layer. O(1) Lookup. */
    int32 GetProjectionMask(FName LayerTag) const;
    
    const FTCATGridResource* GetLayerResource(FName LayerTag) const;
    void GetLayerMinMax(FName MapTag, float& OutMin, float& OutMax); 

    /** In addition to bEnablePositionPrediction, other conditions are also checked to determine whether prediction is possible. */
    FORCEINLINE bool IsPossiblePrediction() const { return bRefreshWithGPU && bEnablePositionPrediction; }

    UFUNCTION(CallInEditor, Category = "TCAT", meta=(DisplayName="Sync Layers from Components"))
    void SyncLayersFromComponents();
    
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginDestroy() override;

    void UpdateVolumeInfos();
    
    /** Gather All Component, and Categorize by Tag*/
    void RefreshSources();

    /** Allocate RenderTargets and related resources based on tags */
    void InitializeResources();

    void UpdateGridSize();

    void UpdateMemoryStats();
    
    void RebuildRuntimeMaps();
    
    /** Ensures that a base layer exists for the given tag, creating one if necessary. */
    void BatchEnsureBaseLayers(const TSet<FName>& NewTags);

//~=============================================================================
// Self Influence Build
//~=============================================================================
public:
    /** Returns 1.0 / Range for the specified layer. Returns 0 if range is invalid. */
    float GetLayerScaleFactor(FName LayerTag) const;

    /** Retrieves the baked recipe map for a specific component's source tag. */
    const TMap<FName, FTCATSelfInfluenceRecipe>* GetBakedRecipesForSource(FName SourceTag) const;
    
protected:
    /** * [Runtime Cache] 
         * Key: Source Layer Tag (e.g., "Ally")
         * Value: Map of Target Layer -> Recipe 
         * (e.g., "Ally" -> { "TotalDanger" : Recipe, "Ally" : Recipe })
         */
    TMap<FName, TMap<FName, FTCATSelfInfluenceRecipe>> CachedInfluenceRecipes;
    
    /** Builds the dependency graph and calculates reverse recipes. */
    void RebuildInfluenceRecipes();
    
//~=============================================================================
// Configuration
//~=============================================================================
protected:
    /** Size of one grid cell (shared by all layers) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT", meta = (ClampMin = "0.01", UIMin = "0.01"))
    float CellSize = TCATGlobalSettings::DEFAULT_CELL_SIZE;
    
    UPROPERTY(VisibleInstanceOnly, Category= "TCAT", meta=(DisplayName="Grid Resolution"))
    FString GridResolutionDisplay;

    UPROPERTY()
    FIntPoint GridResolution;
    
    /** Per-Layer 3D & Debug Settings */
    UPROPERTY(EditAnywhere, Category="TCAT", meta=(TitleProperty="BaseLayerTag", DisplayName="Base Layer Configs"))
    TArray<FTCATBaseLayerConfig> BaseLayerConfigs;

    /** Runtime Cache of TArray<FTCATBaseLayerConfig> BaseLayerConfigs, O(1) Search */
    UPROPERTY(Transient)
    TMap<FName, FTCATBaseLayerConfig> CachedBaseLayerMap;
    
    /** Defined rules for composite layers & Debug Settings. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TCAT", meta=(TitleProperty="CompositeLayerTag", DisplayName="Composite Layer Configs"))
    TArray<FTCATCompositeLayerConfig> CompositeLayers;
    
//~=============================================================================
// Debug Configuration
//~=============================================================================
protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TCAT|Debug")
    ETCATDebugDrawMode DrawInfluence = ETCATDebugDrawMode::VisibleOnly;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TCAT|Debug", meta=(ShowOnlyInnerProperties))
    FTCATHeightMapModule HeightMap;
    
    /** * Bakes the Height Map using the current CellSize.
     * @note If you have changed the 'Cell Size', it is STRONGLY RECOMMENDED to press this button to update the map.
     */
    UFUNCTION(CallInEditor, Category = "TCAT", meta=(DisplayName="Bake Height Map"))
    void BakeHeightMap();
    
private:
    // Helper to find debug config (now we don't need a cached map, just lookup or cache if needed)
    // For simplicity, we can rebuild a transient map for O(1) access during Draw
    UPROPERTY(Transient)
    TMap<FName, FTCATLayerDebugSettings> CachedDebugSettingsMap;

    UPROPERTY(Transient)
    bool bIsHeightBaked = false;

//~=============================================================================
// Performance Configuration
//~=============================================================================
protected:
    /** If true, when there is little performance difference between updating the map on the GPU or CPU,
    map updates are performed on the CPU(bRefreshWithGPU = false) because CPU Updating avoids the risk of prediction failure.
    When updating on the GPU performs better than on the CPU, map updates are performed on the GPU(bRefreshWithGPU = true). 
    "bRefreshWithGPU" is modified by the TCATSubsystem.
    To modify related detailed settings, go to Project Settings > Plugins > TCAT Global Settings > Advanced > Adaptive GPU/CPU update mode switching.*/
	UPROPERTY(EditAnywhere, Category = "TCAT|Performance")
	bool bAdaptivelySwitchRefreshMode = true;

    /** If true, the influence map is generated using Compute Shaders on the GPU. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TCAT|Performance")
    bool bRefreshWithGPU = true;;
    
    /** If true, enables position extrapolation for influence calculations. 
    Because the results of the influence map update arrive a few frames later, this option predicts the position of the InfluenceComponent a few frames ahead and uses that position for the influence map update. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT|Performance", meta = (EditCondition = "bRefreshWithGPU"))
    bool bEnablePositionPrediction = true;
        
	/** To handle situations where the frame rate suddenly increases and then decreases, 
    if the delta time increases by more than 켼edictionCorrectionThresholdtimes compared to the previous delta time, 
    the PredictionTime is not immediately increased. Instead, the previously used PredictionTime is used.
    For more detailed information, refer to the code that uses the variable. */
    UPROPERTY(EditAnywhere, Category = "TCAT", AdvancedDisplay)
	float PredictionCorrectionThreshold = 2.0f;

    /** The degree to which acceleration is reflected in the prediction */
    UPROPERTY(EditAnywhere, Category = "TCAT", AdvancedDisplay)
    float AccelerationPredictionFactor = 1.0f;

private:
    /** TCATSettings to Editor UI Functions */
    UFUNCTION()
    TArray<FString> GetAllTagOptions() const;
    
    UFUNCTION()
    TArray<FString> GetBaseTagOptions() const;

    UFUNCTION()
    TArray<FString> GetCompositeTagOptions() const;

//~=============================================================================
// Resources
//~=============================================================================
public:
    /** --- Height Resource Accessors --- */
    /** Returns the RenderTarget for the Global Height map. */
    UTextureRenderTarget2D* GetHeightRenderTarget() const;

    /** Returns the RenderTargetResource for the Global Height map. */
    FTextureRenderTargetResource* GetHeightRenderTargetTexture() const;
    
private:
    class UTCATSubsystem* GetTCATSubsystem() const;
        
    /** Height Layer */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "TCAT", AdvancedDisplay)
    FTCATHeightMapResource HeightResource;

    /** Influence Layers per Tag */
    UPROPERTY(VisibleInstanceOnly, Category = "TCAT", AdvancedDisplay)
    TMap<FName, FTCATGridResource> InfluenceLayers;

    /** Source Data per Tag (Updated Every Frame) */
    TMap<FName, TArray<FTCATInfluenceSource>> LayerSourcesMap;

	/** Source Data with Owner Component per Tag (Updated Every Frame) */
    TMap<FName, TArray<FTCATInfluenceSourceWithOwner>> LayerSourcesWithOwners;

	/** Position Prediction Time Cache per Layer Tag */
    TMap<FName, FTCATPredictionInfo> TagToPredictionInfo;
    
    FBox CachedBounds;

    float LastDeltaSeconds = 0.0;
    
//~=============================================================================
// Debug
//~=============================================================================
protected:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TCAT|Performance", meta=(EditCondition="bRefreshWithGPU"))
    bool bLogAsyncFrame = false;

	/** If true, forces single-threaded update of the influence map when bRefreshWithGPU is false, useful for debugging. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TCAT", AdvancedDisplay, meta=(EditCondition="!bRefreshWithGPU"))
	bool bForceCPUSingleThreadUpdate = false;
    
private:
    void DebugDrawGrid();
    
    /** Captures and logs the current state for the Visual Logger. */
    void VLogInfluenceVolume() const;
};
