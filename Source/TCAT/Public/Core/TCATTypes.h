// Copyright 2025-2026 Over2K. All Rights Reserved

#pragma once
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "TCATTypes.generated.h"

// Shared literal for curated curve content path.
#define TCAT_CURATED_CURVE_PATH_LITERAL "/TCAT/TCAT/Curves"

namespace TCATContentPaths
{
	static constexpr const TCHAR* CuratedCurvePath = TEXT(TCAT_CURATED_CURVE_PATH_LITERAL);
}

namespace TCATGlobalSettings
{
    /** Default grid cell size in centimeters (world units). */
    static constexpr float DEFAULT_CELL_SIZE = 50.0f;
    
    /** Default influence radius in centimeters (world units). */
    static constexpr float DEFAULT_RADIUS = 200.0f;
}

namespace TCATMapConstant
{
    /** Trace start/end offsets used by debug or sampling utilities (centimeters). */
    constexpr float TRACE_OFFSET_UP = 500.0f;
    constexpr float TRACE_OFFSET_DOWN = 500.0f;

    /** Debug draw defaults. */
    constexpr float DEBUG_POINT_SIZE = 5.0f;
    constexpr float DEBUG_HEIGHT_OFFSET = 5.0f;

    /** Cell center offset multiplier (0.5 => center of cell). */
    constexpr float CELL_CENTER_OFFSET = 0.5f;

    /** 4-neighborhood offsets for grid adjacency operations. */
    constexpr int32 NUM_NEIGHBOR_OFFSETS = 4;
    constexpr int32 NeighborOffsets[NUM_NEIGHBOR_OFFSETS][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
}

class UTCATInfluenceComponent;

/**
 * Single influence source payload.
 *
 * This struct is mirrored in shader code and must remain layout-compatible with the GPU representation.
 * (See TCAT/Shaders/InfluenceCommon.ush for the corresponding shader struct.)
 *
 * Blueprint usage:
 * - Designers typically edit InfluenceRadius / Strength / InfluenceZLimitOffset in component or transient configs.
 * - CurveTypeIndex is resolved internally from a UCurveFloat (global atlas) and is not intended for manual editing.
 */
USTRUCT(BlueprintType)
struct FTCATInfluenceSource
{
    GENERATED_BODY()

    /** World-space location of the source (centimeters). Stored as FVector3f for GPU-friendly packing. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Transient, Category = "Influence", meta=(AdvancedDisplay))
    FVector3f WorldLocation;

    /** Maximum effective range of the influence (centimeters). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Influence", meta = (ClampMin = "0.0"))
    float InfluenceRadius;
    
    // MaxValue = Importance of the source (Positive for allies, Negative for enemies)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Influence")
    float Strength;
    
    /**
     * Row index into the global curve atlas (CurveTypeIndex).
     * This is typically filled by the system when resolving a UCurveFloat.
     */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Influence", meta = (ClampMin = "0.0"))
    int32 CurveTypeIndex;
    
    /** Height offset applied to the owner's bounding box top when computing MaxInfluenceZ.
     * MaxInfluenceZ is computed in the volume per update:
     *   MaxInfluenceZ = OwnerBoundingBoxTop.Z + InfluenceZLimitOffset
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Influence", meta=(AdvancedDisplay))
    float InfluenceZLimitOffset;

    /** * Computed maximum Z coordinate where influence applies.
     * * Calculated as: BoundingBoxTop.Z + InfluenceZLimitOffset
     * * Cells above this Z are not affected by this source.
     * * when ETCATProjectionFlag::MaxInfluenceHeight is enabled.
     */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Influence")
    float MaxInfluenceZ;

    /**
     * Eye-level offset (centimeters) used by line-of-sight checks.
     * The visibility test typically starts from (WorldLocation.Z + LineOfSightOffset).
     */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Influence")
    float LineOfSightOffset;
    
    FTCATInfluenceSource()
        : WorldLocation(FVector::ZeroVector)
        , InfluenceRadius(500.0f)
        , Strength(1.0f)
        , CurveTypeIndex(0)
        , InfluenceZLimitOffset(0.0f)
        , MaxInfluenceZ(TNumericLimits<float>::Max())
        , LineOfSightOffset(0.0f)
        {}
};

/** Internal payload used to track ownership for prediction/correction workflows (GPU readback post-fix). */
struct FTCATInfluenceSourceWithOwner
{
    FTCATInfluenceSource Source;
    /**
     * Owner component that produced this source.
     * May be invalid for transient sources or if the actor/component was destroyed.
     */
    TWeakObjectPtr<UTCATInfluenceComponent> OwnerComponent;
};

UENUM(BlueprintType)
enum class ETCATCompareType : uint8
{
    Greater         UMETA(DisplayName = "Greater"),
    GreaterOrEqual  UMETA(DisplayName = "Greater Or Equal"),
    Less            UMETA(DisplayName = "Less"),
    LessOrEqual     UMETA(DisplayName = "Less Or Equal"),
    Equal           UMETA(DisplayName = "Equal"),
    NotEqual        UMETA(DisplayName = "Not Equal")
};

/** * Defines the rules for culling (blocking) influence propagation.
 * Use these flags to control how influence spreads across complex terrain or obstacles.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ETCATProjectionFlag : uint8
{
    None        = 0 UMETA(Hidden),
    /** * [Vertical Range Check]
     * Restricts influence based on MaxInfluenceZ (computed from bounding box top + InfluenceZLimitOffset).
     * * - When Enabled: Influence is cut off for cells with Z > MaxInfluenceZ.
     * - When Disabled: The influence acts like an infinite vertical pillar.
     * * @usage Enable this to prevent ground units from influencing areas on high cliffs.
     */
    MaxInfluenceHeight  = 1 UMETA(DisplayName = "Max Influence Height"), // 1 << 0
    /** * [Physical Obstacle Check]
     * Performs a Line-of-Sight (Raycast/Raymarch) check to ensure the target is not blocked by walls or buildings.
     * * - Uses 'HeightOffset' to determine the observation 'Eye Level'.
     * - This is more expensive than Vertical Range check.
     * * @usage Enable this for realistic visibility handling, such as blocking an aura behind a wall 
     * or preventing influence from leaking through closed doors.
     */
    LineOfSight  = 2 UMETA(DisplayName = "Line Of Sight"),    // 1 << 1
};
ENUM_CLASS_FLAGS(ETCATProjectionFlag);

// Composition operation types for blending influence maps
UENUM(BlueprintType)
enum class ETCATCompositeOp : uint8
{
    Add       UMETA(DisplayName = "Add"),
    Subtract  UMETA(DisplayName = "Subtract"),
    Multiply  UMETA(DisplayName = "Multiply"),
    Divide    UMETA(DisplayName = "Divide"),
    Invert    UMETA(DisplayName = "Invert"),
    /**
     * Normalizes the *accumulator* map (unary boundary):
     * - Computes global min/max of the current accumulated map
     * - Remaps accumulator into [0..1]
     *
     * NOTE: This differs from FTCATCompositeOperation::bNormalizeInput which normalizes the input map only.
     */
    Normalize UMETA(DisplayName = "Normalize")
};

/**
 * One step in a composite recipe.
 *
 * Execution model (conceptual):
 * - There is an internal accumulator map (starts at 0 for each cell).
 * - Each operation updates the accumulator.
 *
 * Normalization model (important for self-influence tracking & user expectations):
 * - bNormalizeInput: normalizes the selected input map (InputMapTag) into [0..1] before applying Strength.
 * - ETCATCompositeOp::Normalize: normalizes the accumulator itself and acts as a boundary for contribution tracking.
 */
USTRUCT(BlueprintType)
struct FTCATCompositeOperation
{
    GENERATED_BODY()

    // Operation type (Add, Subtract, Multiply, Divide, Invert, Normalize)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta=(GetOptions="GetBaseTagOptions"))
    ETCATCompositeOp Operation = ETCATCompositeOp::Add;

    // Input map tag (used for binary operations: Add, Subtract, Multiply, Divide)
    // Not used for unary operations: Invert, Normalize
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta=(
                  GetOptions="GetBaseTagOptions", 
                  EditCondition="Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide", 
                  EditConditionHides,
                  DisplayName = "Input Map Tag"))
    FName InputLayerTag;

    /**
     *  Clamp InputMap before other operations (binary operations only)
     *  Operation order: Clamp -> Normalize -> Strength
     *  When true: clamps input map to [ClampMin, ClampMax] before normalize and strength
     *  NOTE: Clamp may make self-influence removal approximate (recipe will be flagged as approximate),
     *  but it does not automatically disable recipe generation.
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta=(EditCondition="Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide", EditConditionHides))
    bool bClampInput = false;

    // Minimum clamp value for InputMapB (when bClampInput is true)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta=(EditCondition="bClampInput && (Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide)", EditConditionHides))
    float ClampMin = 0.0f;

    // Maximum clamp value for InputMapB (when bClampInput is true)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta=(EditCondition="bClampInput && (Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide)", EditConditionHides))
    float ClampMax = 1.0f;

    // Normalize input map before applying the operation (binary operations only).
    // When true: computes the min/max range of the current input map on the GPU
    // and normalizes values into [0, 1] before applying Strength.
    // [Operation order] Clamp -> Normalize -> Strength
    // NOTE: This is NOT the same as ETCATCompositeOp::Normalize.
    // - bNormalizeInput: normalizes the current input map and still supports self-influence removal recipes.
    // - ETCATCompositeOp::Normalize: acts as a boundary that resets per-source contribution tracking.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition",
              meta=(EditCondition="Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide", EditConditionHides))
    bool bNormalizeInput = false;

    // Strength multiplier applied after clamp and normalize
    // Scales the processed input map before composition
    // Operation order: Clamp -> Normalize -> Strength
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition")
    float Strength = 1.0f;
};

USTRUCT(BlueprintType)
struct FTCATCurveCalculateInfo
{
    GENERATED_BODY()
    /** Component's influence falloff curve used in map calculation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
    TObjectPtr<UCurveFloat> Curve;

    /**
     * Strength applied after normalization (if enabled).
     * Common usage: (MapStrength * ComponentStrength).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
    float Strength = 1.0f;

    /** When true, normalizes sampled values (or relevant intermediate) before applying Strength. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
    bool bIsNormalize = false;
};

/** Prediction bookkeeping used by per-layer ring buffers (debug + runtime). */
struct FTCATPredictionInfo
{
	float PrevPredictionTime = 0.0f; // for debugging
	float PredictionTime = 0.0f;
};

/**
 * Pairs a layer tag with its debug visualization settings.
 * Used in the centralized LayerDebugSettings array on the influence volume.
 */
USTRUCT(BlueprintType)
struct FTCATLayerDebugSettings
{
    GENERATED_BODY()

    /** The layer tag this debug config applies to. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debug")
    FName MapTag;

    /** Whether to enable debug drawing for this specific map. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bVisible = true;

    /** Vertical offset to separate maps visually in the viewport. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    float HeightOffset = 10.0f;
    
    /** Color representing positive influence values (e.g., Ally). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FLinearColor PositiveColor = FLinearColor::Green;

    /** Color representing zero influence values (e.g., Neutral). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FLinearColor ZeroColor = FLinearColor(0.25f, 0.25f, 0.25f);

    /** Color representing negative influence values (e.g., Enemy). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FLinearColor NegativeColor = FLinearColor::Red;
};

USTRUCT(BlueprintType)
struct FTCATSelfInfluenceRecipe
{
    GENERATED_BODY()
    
    /** True if the recipe can be applied exactly given the current operation chain. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    bool bIsReversible = true;

    /** Human-readable reason when bIsReversible is false. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    FString FailureReason;

    /** Accumulated coefficient computed in raw (non-normalized) space. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float RawCoefficient = 0.0f;

    /** Accumulated coefficient computed in normalized space (requires min/max range at runtime). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float NormCoefficient = 0.0f;    
    
    /** If the path includes auto-normalization, store the map tag here (used to fetch runtime range). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT", meta = (DisplayName = "Dynamic Scale Map Tag"))
    FName DynamicScaleLayerTag = NAME_None;

    /** If true, the recipe result is approximate (e.g., due to clamping or unsupported transforms). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    bool bIsApproximate = false;
};


USTRUCT(BlueprintType)
struct FTCATSelfInfluenceResult
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    TObjectPtr<UCurveFloat> Curve = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    int32 CurveTypeIndex = INDEX_NONE;

    /** World-space origin recorded when the influence contribution was emitted. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    FVector SourceLocation = FVector::ZeroVector;

    /**
     * Final removal factor used to subtract a unit's "self" contribution from a composed map.
     *
     * Conceptually:
     *   FinalRemovalFactor = (SourceStrength * RawCoefficient)
     *                     + (SourceStrength * NormCoefficient * (1 / Range))
     *
     * Where Range is the runtime min/max range of the relevant normalization stage.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float FinalRemovalFactor = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    int32 SettingFor3D = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float InfluenceRadius = 0.0f;

    /** Returns true if the result contains enough information to apply a non-zero self-removal. */
    bool IsValid() const
    { 
        const bool bHasCurveData = (Curve != nullptr) || (CurveTypeIndex != INDEX_NONE);
        return bHasCurveData && !FMath::IsNearlyZero(FinalRemovalFactor); 
    }
};