// Copyright 2025-2026 Over2K. All Rights Reserved

#pragma once
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "TCATTypes.generated.h"

namespace TCATGlobalSettings
{
    static constexpr float DEFAULT_CELL_SIZE = 50.0f; 
    static constexpr float DEFAULT_RADIUS = 200.0f;
}

namespace TCATMapConstant
{
    constexpr float TRACE_OFFSET_UP = 500.0f;
    constexpr float TRACE_OFFSET_DOWN = 500.0f;
    constexpr float DEBUG_POINT_SIZE = 5.0f;
    constexpr float DEBUG_HEIGHT_OFFSET = 5.0f;
    constexpr float CELL_CENTER_OFFSET = 0.5f;
    
    constexpr int32 NUM_NEIGHBOR_OFFSETS = 4;
    constexpr int32 NeighborOffsets[NUM_NEIGHBOR_OFFSETS][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
}

class UTCATInfluenceComponent;

// Should Match with TCAT\Shader\InfluenceCommon.ush's FInfluenceSource
USTRUCT(BlueprintType)
struct FTCATInfluenceSource
{
    GENERATED_BODY()
    
    UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Transient, Category = "Influence")
    FVector3f WorldLocation;

    // Max Influence Radius = Maximum effective range of the influence
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Influence", meta = (ClampMin = "0.0"))
    float InfluenceRadius;

    // MaxValue = Importance of the source (Positive for allies, Negative for enemies)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Influence")
    float Strength;

    // ID of curve types in the LUT (Curve Data)
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Influence", meta = (ClampMin = "0.0"))
    int32 CurveTypeIndex;
    
    /** * Defines the vertical extent of the influence volume from its center, similar to a Capsule Component's Half-Height.
     * * This value represents the distance from the component's center to its top (or bottom).
     * Influence is applied only within the range [Center.Z - HalfHeight, Center.Z + HalfHeight].
     * * @note Set to 0 or a negative value to disable vertical range checking (infinite height).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Influence", meta=(AdvancedDisplay))
    float InfluenceHalfHeight;

    // Only Follows Influence Component's LineOfSightOffset Setting in TCAT|Advanced Category 
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Influence", meta=(AdvancedDisplay))
    float LineOfSightOffset;
    
    FTCATInfluenceSource()
        : WorldLocation(FVector::ZeroVector)
        , InfluenceRadius(500.0f)
        , Strength(1.0f)
        , CurveTypeIndex(0)
        , InfluenceHalfHeight(-1.f)
        , LineOfSightOffset(0.0f)
        {}
};

struct FTCATInfluenceSourceWithOwner
{
    FTCATInfluenceSource Source;
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
     * Restricts influence to a specific vertical range defined by 'InfluenceHalfHeight' in InfluenceComponent.
     * * - When Enabled: The influence forms a cylinder (or capsule) shape. Influence is cut off if the height difference exceeds the limit.
     * - When Disabled: The influence acts like an infinite vertical pillar.
     * * @usage Enable this to prevent ground units from influencing areas on high cliffs, 
     * or to stop flying units from affecting the ground far below.
     */
    InfluenceHalfHeight  = 1 UMETA(DisplayName = "Influence Half Height"), // 1 << 0
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
    Normalize UMETA(DisplayName = "Normalize")
};

USTRUCT(BlueprintType)
struct FTCATCompositeOperation
{
    GENERATED_BODY()

    // Operation type (Add, Subtract, Multiply, Divide, Invert, Normalize)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta=(GetOptions="GetBaseTagOptions"))
    ETCATCompositeOp Operation = ETCATCompositeOp::Add;

    // Input layer tag (used for binary operations: Add, Subtract, Multiply, Divide)
    // Not used for unary operations: Invert, Normalize
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition",
              meta=(GetOptions="GetBaseTagOptions", EditCondition="Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide", EditConditionHides))
    FName InputLayerTag;

    // Clamp InputMap before other operations (binary operations only)
    // When true: clamps input map to [ClampMin, ClampMax] before normalize and strength
    // Operation order: Clamp -> Normalize -> Strength
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition",
              meta=(EditCondition="Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide", EditConditionHides))
    bool bClampInput = false;

    // Minimum clamp value for InputMapB (when bClampInput is true)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition",
              meta=(EditCondition="bClampInput && (Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide)", EditConditionHides))
    float ClampMin = 0.0f;

    // Maximum clamp value for InputMapB (when bClampInput is true)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition",
              meta=(EditCondition="bClampInput && (Operation == ETCATCompositeOp::Add || Operation == ETCATCompositeOp::Subtract || Operation == ETCATCompositeOp::Multiply || Operation == ETCATCompositeOp::Divide)", EditConditionHides))
    float ClampMax = 1.0f;

    // Normalize input map before applying the operation (binary operations only)
    // When true: computes the min/max range of the current input map on the GPU
    // and normalizes values into [0, 1] before applying Strength.
    // Operation order: Clamp -> Normalize -> Strength
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
    // Component's Influence Curve Used in Map's Calculation 
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
    TObjectPtr<UCurveFloat> Curve;

    // [RECOMMEND] Map's Strength * Component's Strength,
    // Multiplied after Normalize
    //[Normalize] -> [Strength]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
    float Strength = 1.0f;

    //[Normalize] -> [Strength]
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
    bool bIsNormalize = false;
};

struct FTCATPredictionInfo
{
	float PrevPredictionTime = 0.0f; // for debugging
	float PredictionTime = 0.0f;
};

USTRUCT(BlueprintType)
struct FTCATLayerDebugSettings
{
    GENERATED_BODY()

    /** Whether to enable debug drawing for this specific layer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bVisible = true;

    /** Vertical offset to separate layers visually in the viewport. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    float HeightOffset = 10.0f;
    
    /** Color representing positive influence values (e.g., Ally). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FLinearColor PositiveColor = FLinearColor::Green;

    /** Color representing negative influence values (e.g., Enemy). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FLinearColor NegativeColor = FLinearColor::Red;
};

USTRUCT(BlueprintType)
struct FTCATSelfInfluenceRecipe
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    bool bIsReversible = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    FString FailureReason;

    // Accumulated Coeffcient about RAW data
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float RawCoefficient = 0.0f;

    // Accumulated Coeffcient about Normalized data
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float NormCoefficient = 0.0f;    
    
    /** If the path includes Auto-Normalization, store the layer tag here. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    FName DynamicScaleLayerTag = NAME_None;

    /** If true, the result is approximate (e.g., Clamp forced). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    bool bIsApproximate = false;
};


USTRUCT(BlueprintType)
struct FTCATSelfInfluenceResult
{
    GENERATED_BODY()
	
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    TObjectPtr<UCurveFloat> Curve = nullptr;

    // Removal Factor
    // = (SourceStrength * RecipeRawCoef) + (SourceStrength * RecipeNormCoef * 1/Range)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float FinalRemovalFactor = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    int32 SettingFor3D = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TCAT")
    float InfluenceRadius = 0.0f;

    // Can we Remove Self Influence?
    bool IsValid() const  
    { 
        return Curve != nullptr && !FMath::IsNearlyZero(FinalRemovalFactor); 
    }
};