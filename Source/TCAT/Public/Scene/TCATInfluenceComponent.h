// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/TCATTypes.h"
#include "TCATInfluenceComponent.generated.h"

class UTCATSubsystem;
class ATCATInfluenceVolume;
class UCurveFloat;
struct FTCATBatchQuery;

//~=============================================================================
// Influence Configuration Structures
//~=============================================================================

/**
 * Defines influence layer configuration with falloff curve and source parameters.
 * Configured in editor and converted to runtime format via RebuildSourceMap().
 */
USTRUCT(BlueprintType)
struct FTCATInfluenceConfigEntry
{
	GENERATED_BODY()
    
	/** Unique identifier for this influence layer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT", meta=(GetOptions="GetBaseTagOptions"))
	FName MapTag;
    
	/** Curve defining influence falloff over distance (optional, defaults to linear) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT")
	TObjectPtr<UCurveFloat> FalloffCurve;
    
	/** Source parameters: radius, strength, height, etc. */
	UPROPERTY(EditAnywhere, Category="TCAT", meta=(TitleProperty="InfluenceRadius")) 
	FTCATInfluenceSource SourceData;
};

/**
 * Cached step for self-influence removal calculations.
 * Stores coefficients for reversing influence contributions.
 */
struct FCachedRemovalStep
{
	/** Source layer tag contributing to the target layer */
	FName MySourceTag;
    
	/** Range-independent coefficient */
	float RawCoefficient = 0.0f;
    
	/** Range-dependent coefficient (scaled dynamically) */
	float NormCoefficient = 0.0f;
    
	/** Layer tag used for dynamic scaling (if any) */
	FName DynamicScaleTag = NAME_None;
};
//~=============================================================================
// Main Component
//~=============================================================================

/**
 * Component that emits influence into the TCAT (Tactical Combat AI Toolkit) system.
 * 
 * Attach this to actors that should affect influence maps (e.g., enemies, allies, objectives).
 * Each component can emit multiple influence layers with different radii, strengths, and falloff curves.
 * 
 * Key Features:
 * - Multi-layer influence emission with customizable falloff curves
 * - Motion tracking (velocity, acceleration, rotation) for predictive AI
 * - Self-influence removal for composite layers
 * - Runtime query debugging with visual logger
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), 
    HideCategories=("Variable", "Sockets", "ComponentTick", "ComponentReplication", "Activation", "Cooking", "AssetUserData", "Collision"))
class TCAT_API UTCATInfluenceComponent : public USceneComponent
{
    GENERATED_BODY()

    friend class UTCATSubsystem;

public:
    UTCATInfluenceComponent();

//~=============================================================================
// Public API - Configuration
//~=============================================================================
	/** * The vertical offset added to the source location to determine the observation point ('Eye Level') for Line of Sight checks.
	 * * When 'Line Of Sight' is enabled, a ray is traced from [ComponentLocation + UpVector * HeightOffset] to the target cell.
	 * Use this to prevent ground-based clutter (e.g., small rocks, fences) from blocking the unit's view.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT|Advanced")
	float LineOfSightOffset = 0.0f;
	
    /** Checks if this component emits influence to the specified layer */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    bool HasInfluenceLayer(FName MapTag) const { return RuntimeSourceMap.Contains(MapTag); }

    /** Returns the influence radius for the specified layer (handles both source and composite layers) */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    float GetRadius(FName MapTag) const;
    
    /** Returns the influence half-height for the specified layer */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    float GetInfluenceHalfHeight(FName MapTag) const 
    { 
        if (const FTCATInfluenceSource* Src = RuntimeSourceMap.Find(MapTag))
            return Src->InfluenceHalfHeight;
        return 0.0f;
    }

    /** Returns the source data for the specified layer */
    FORCEINLINE const FTCATInfluenceSource& GetSource(FName MapTag) const { return RuntimeSourceMap[MapTag]; }
    
    /** Returns all configured influence layers */
    FORCEINLINE const TArray<FTCATInfluenceConfigEntry>& GetInfluenceLayers() const { return InfluenceLayerMap; }

	FORCEINLINE float GetPositionErrorTolerance() const { return PositionErrorTolerance; }
    /** 
     * Rebuilds the runtime source map from editor configuration.
     * Call this after modifying InfluenceLayerMap at runtime.
     */
    void RebuildSourceMap();

//~=============================================================================
// Public API - Motion & Transform
//~=============================================================================

    /** Returns the current world location of this influence source */
    FORCEINLINE FVector GetCurrentLocation() const { return Location; }

    /** Returns the current velocity (updated per-frame) */
    FORCEINLINE FVector GetCurrentVelocity() const { return Velocity; }

    /** Returns the current acceleration (updated per-frame) */
    FORCEINLINE FVector GetCurrentAcceleration() const { return Acceleration; }

    /** Returns the delta rotation (velocity direction change) */
    FORCEINLINE FQuat GetDeltaRotation() const { return DeltaRotation; }

    /** Returns the axis of rotation for velocity direction change */
    FORCEINLINE FVector GetDeltaRotationAxis() const { return DeltaRotationAxis; }

    /** Returns the angle of rotation (radians) for velocity direction change */
    FORCEINLINE float GetDeltaRotationAngleRad() const { return DeltaRotationAngleRad; }

    /** Returns the predicted future location (set externally for debugging) */
    FORCEINLINE FVector3f GetPredictedLocation() const { return PredictedLocation; }

    /** Sets the predicted future location (for debugging visualization) */
    FORCEINLINE void SetPredictedLocation(const FVector3f& InLocation) { PredictedLocation = InLocation; }

    /** 
     * Returns the world location used for influence calculations.
     * Override this in subclasses to customize positioning logic.
     */
    FVector ResolveWorldLocation() const;
    
    /** 
     * Updates motion tracking (location, velocity, acceleration, rotation).
     * Called automatically by subsystem each frame. Only updates once per frame.
     */
    void RefreshMotionStatus();

//~=============================================================================
// Public API - Self-Influence & Reverse Calculation
//~=============================================================================
    /** 
     * Retrieves custom calculation info for removing self-influence from a layer.
     * Returns default (empty) info if not found.
     */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    FTCATCurveCalculateInfo GetReverseCalculationInfo(FName MapTag) const;
    
    /** Replaces all reverse calculation info (bulk operation) */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    void SetReverseCalculationInfo(const TMap<FName, FTCATCurveCalculateInfo>& InCurveCalculateInfos) 
    { 
        CurveCalculateInfos = InCurveCalculateInfos; 
    }

    /** Adds or updates reverse calculation info for a specific layer */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    void AddReverseCalculationInfo(FName MapTag, const FTCATCurveCalculateInfo& NewInfo);

    /**
     * Computes self-influence removal parameters for a target layer.
     * Priority: 1) User override (CurveCalculateInfos), 2) Cached recipe steps
     * 
     * @param TargetMapTag - The layer to compute removal for
     * @param Volume - The volume containing baked recipes (triggers cache update if changed)
     * @return Result containing curve, strength, and radius for self-removal
     */
    FTCATSelfInfluenceResult GetSelfInfluenceResult(FName TargetMapTag, const ATCATInfluenceVolume* Volume) const;
    
    /** 
     * Updates cached removal recipes from the volume.
     * Called automatically when volume changes in GetSelfInfluenceResult().
     */
    void UpdateCachedRecipes(const ATCATInfluenceVolume* Volume) const;

//~=============================================================================
// Debug - Visual Logger
//~=============================================================================

#if ENABLE_VISUAL_LOG
    /** 
     * Applies debug visualization settings to a query.
     * Generates unique colors per component and staggers query layers vertically.
     */
    void ApplyQueryDebugSettings(struct FTCATBatchQuery& Query) const;
#endif

    /** Enable/disable query debugging for this component */
    UPROPERTY(EditAnywhere, Category = "TCAT|Debug")
    bool bDebugMyQueries = false;

    /** Vertical spacing between stacked debug query visualizations */
    UPROPERTY(EditAnywhere, Category = "TCAT|Debug", meta=(ClampMin="0.0", UIMin="0.0"))
    float DebugQueryHeightStep = 40.0f;

    /** Sample stride for debug visualization (1 = all samples, 2 = every other, etc.) */
    UPROPERTY(EditAnywhere, Category = "TCAT|Debug", meta=(ClampMin="1", UIMin="1"))
    int32 DebugQueryStride = 2;

//~=============================================================================
// Component Lifecycle
//~=============================================================================

protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

//~=============================================================================
// Internal - Configuration Data
//~=============================================================================

protected:
    /** 
     * Editor configuration: influence layers with curves and source parameters.
     * Modified in editor, converted to RuntimeSourceMap on registration.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TCAT", meta=(TitleProperty="MapTag"))
    TArray<FTCATInfluenceConfigEntry> InfluenceLayerMap;

    /** 
     * Runtime cache: optimized source data with curve IDs instead of pointers.
     * Auto-generated from InfluenceLayerMap. If you modify influence properties at runtime,
     * you MUST call RebuildSourceMap() to reflect changes.
     */
    UPROPERTY(Transient)
    TMap<FName, FTCATInfluenceSource> RuntimeSourceMap;

    /** 
     * User-defined overrides for self-influence removal.
     * Takes priority over cached recipes.
     */
    UPROPERTY(Transient)
    TMap<FName, FTCATCurveCalculateInfo> CurveCalculateInfos;
    
    /** 
     * Cached removal steps: TargetMap -> List of source contributions.
     * Built from volume's baked recipes.
     */
    mutable TMap<FName, TArray<FCachedRemovalStep>> CachedRemovalStepsRuntime;
    
    /** Weak reference to the volume that generated the cached recipes */
    mutable TWeakObjectPtr<const ATCATInfluenceVolume> CachedRecipeVolume;

    /** 
    * When updating the influence map in asynchronous GPU mode, 
    * if the distance between the center position of the influence radius of this updated component in the influence map and its current position is greater than or equal to this value, 
    * the influence of that component is updated once more on the CPU.
    * If this value is too small, the frequency of updates to the CPU increases, which may degrade performance.
    */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT")
    float PositionErrorTolerance = 1000.0f;

//~=============================================================================
// Internal - Motion Tracking
//~=============================================================================

private:
    /** Current world location (updated per-frame) */
    FVector Location = FVector::ZeroVector;
    
    /** Current velocity in cm/s (updated per-frame) */
    FVector Velocity = FVector::ZeroVector;
    
    /** Current acceleration in cm/s² (updated per-frame) */
    FVector Acceleration = FVector::ZeroVector;

    /** Rotation delta between previous and current velocity direction */
    FQuat DeltaRotation = FQuat::Identity;
    
    /** Axis of rotation for velocity direction change */
    FVector DeltaRotationAxis = FVector::ZeroVector;
    
    /** Angle of rotation in radians (normalized to [-PI, PI]) */
    float DeltaRotationAngleRad = 0.0f;

    /** Previous frame's location (for velocity calculation) */
    FVector PrevLocation = FVector::ZeroVector;
    
    /** Previous frame's velocity (for acceleration calculation) */
    FVector PrevVelocity = FVector::ZeroVector;

    /** Frame number of last update (prevents duplicate updates) */
    uint64 PrevFrameNumber = 0;

    /** Predicted future location (set externally, used for debug visualization) */
    FVector3f PredictedLocation = FVector3f::ZeroVector;

//~=============================================================================
// Internal - Debug Helpers
//~=============================================================================

#if ENABLE_VISUAL_LOG
    /** Frame number of last debug query (for layer counter reset) */
    mutable uint64 LastDebugQueryFrame = 0;
    
    /** Counter for stacking debug query visualizations */
    mutable int32 DebugQueryCounter = 0;
    
    /** Returns next debug layer index and increments counter */
    int32 GetNextDebugQueryLayer() const;
#endif

    /** Logs influence sources to visual logger (spheres + arrows for velocity/acceleration) */
    void VLogInfluence() const;

//~=============================================================================
// Internal - Utility Functions
//~=============================================================================

private:
    /** Provides dropdown options for MapTag selection in editor (from global settings) */
    UFUNCTION()
    TArray<FString> GetBaseTagOptions() const;

    /** Helper to get TCAT subsystem from current world */
    UTCATSubsystem* GetTCATSubsystem() const;
};