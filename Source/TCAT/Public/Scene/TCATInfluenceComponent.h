// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/TCATTypes.h"
#include "TCATInfluenceComponent.generated.h"

class UTCATSubsystem;
class ATCATInfluenceVolume;
class UCurveFloat;

//~=============================================================================
// Influence Configuration Structures
//~=============================================================================

/**
 * Defines influence map configuration with falloff curve and source parameters.
 * Configured in editor and converted to runtime format via RebuildSourceMap().
 */
USTRUCT(BlueprintType)
struct FTCATInfluenceConfigEntry
{
    GENERATED_BODY()

    /** Unique identifier for this influence map */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT", meta = (GetOptions = "GetBaseTagOptions"))
    FName MapTag;

    /** Curve defining influence falloff over distance (optional, defaults to linear) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT")
    TObjectPtr<UCurveFloat> FalloffCurve;

    /** Source parameters: radius, strength, height, etc. */
    UPROPERTY(EditAnywhere, Category = "TCAT", meta = (TitleProperty = "InfluenceRadius"))
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

struct FSelfInfluenceCacheEntry
{
    FTCATSelfInfluenceResult Result;
    TWeakObjectPtr<const ATCATInfluenceVolume> Volume;
    uint64 FrameNumber = 0;
};
//~=============================================================================
// Main Component
//~=============================================================================

/**
 * Component that emits influence into TCAT volumes.
 *
 * Attach this to actors that should affect influence maps (e.g., enemies, allies, objectives).
 * Each component can emit multiple influence maps with different radii, strengths, and falloff curves.
 *
 * Key Features:
 * - Multi-Map influence emission with customizable falloff curves
 * - Motion tracking (velocity, acceleration, rotation) for predictive AI
 * - Self-influence removal for composite maps
 * - Runtime query debugging with visual logger
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent),
    HideCategories = ("Variable", "Sockets", "ComponentTick", "ComponentReplication", "Activation", "Cooking", "AssetUserData", "Collision"))
    class TCAT_API UTCATInfluenceComponent : public USceneComponent
{
    GENERATED_BODY()

    friend class UTCATSubsystem;

public:
    UTCATInfluenceComponent();

    //~=============================================================================
    // Public API - Configuration
    //~=============================================================================
        /**
         * Vertical offset used as the "eye level" when Line Of Sight projection is enabled.
         * The LOS ray / sampling starts from:
         *   ResolveWorldLocation() + UpVector * LineOfSightOffset
         *
         * Use this to avoid small ground clutter (rocks/fences) blocking visibility for ground units.
         */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT", AdvancedDisplay)
    float LineOfSightOffset = 0.0f;

    /** Checks if this component emits influence to the specified map */
    UFUNCTION(BlueprintCallable, Category = "TCAT", meta = (DisplayName = "Has Influence Map"))
    bool HasInfluenceLayer(FName MapTag) const { return RuntimeSourceMap.Contains(MapTag); }

    /**
     * Returns an influence radius for the given tag.
     *
     * - If MapTag is a base map emitted by this component: returns that map's InfluenceRadius.
     * - If MapTag is a composite map: returns the InfluenceRadius of the first contributing map
     *   found in the cached removal steps (ordering depends on cached recipe build).
     * - Returns 0 if no match exists.
     */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    float GetRadius(FName MapTag) const;

    /** Returns the influence height offset for the specified map */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    float GetInfluenceHeightOffset(FName MapTag) const
    {
        if (const FTCATInfluenceSource* Src = RuntimeSourceMap.Find(MapTag))
            return Src->InfluenceZLimitOffset;
        return 0.0f;
    }

    /** Returns the source data for the specified map */
    FORCEINLINE const FTCATInfluenceSource& GetSource(FName MapTag) const { return RuntimeSourceMap[MapTag]; }

    /** Returns all configured influence maps */
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
         * Retrieves custom calculation info for removing self-influence from a map.
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

    /** Adds or updates reverse calculation info for a specific map */
    UFUNCTION(BlueprintCallable, Category = "TCAT")
    void AddReverseCalculationInfo(FName MapTag, const FTCATCurveCalculateInfo& NewInfo);

    /**
     * Computes self-influence removal parameters for a target map.
     * Priority: 1) User override (CurveCalculateInfos), 2) Cached recipe steps
     *
     * @param TargetMapTag - The map to compute removal for
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
     * The influence maps to which this component belongs. 
     * And, Configs for the influence emitted on that map.
     * Modified in editor, converted to RuntimeSourceMap on registration.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT", meta = (TitleProperty = "MapTag", DisplayName = "Belonging Maps"))
    TArray<FTCATInfluenceConfigEntry> InfluenceLayerMap;
    

    /**
     *Runtime cache used by volumes during map update.
     * optimized source data with curve IDs instead of pointers.
     * Auto-generated from InfluenceLayerMap(DisplayName: Belonging Maps). If you modify influence properties at runtime,
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

    /** Frame-scoped cache for self influence query results */
    mutable TMap<FName, FSelfInfluenceCacheEntry> SelfInfluenceResultCache;

    /** Weak reference to the volume that generated the cached recipes */
    mutable TWeakObjectPtr<const ATCATInfluenceVolume> CachedRecipeVolume;

    /**
    * Error tolerance for async GPU update correction.
    * When updating the influence map in asynchronous GPU mode,
    * if the distance between the center position of the influence radius of this updated component in the influence map and its current position is greater than or equal to this value,
    * the influence of that component is updated once more on the CPU.
    * If this value is too small, the frequency of updates to the CPU increases, which may degrade performance.
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCAT", AdvancedDisplay)
    float PositionErrorTolerance = 1000.0f;

    //~=============================================================================
    // Internal - Motion Tracking
    //~=============================================================================

private:
    /** Current world location (updated per-frame) */
    FVector Location = FVector::ZeroVector;

    /** Current velocity in cm/s (updated per-frame) */
    FVector Velocity = FVector::ZeroVector;

    /** Current acceleration in cm/s� (updated per-frame) */
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
