// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "Core/TCATTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "VisualLogger/VisualLogger.h"
#include "TCATQueryTypes.generated.h"

class UCurveFloat;
class AActor;

enum class ETCATQueryType : uint8
{
	HighestValue,
	LowestValue,
	Condition,
	HighestValueInCondition,
	LowestValueInCondition,
	ValueAtPos,
	Gradient,
};

USTRUCT(BlueprintType)
struct FTCATSingleResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="TCAT")
	float Value = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="TCAT")
	FVector WorldPos = FVector::ZeroVector;
};

namespace TCATQueryConstants
{
	static constexpr int32 INLINE_RESULT_CAPACITY = 8;
	
	static constexpr int32 CURVE_SAMPLE_COUNT = 256;

	// How many candidates to keep beyond MaxResults for reachability filtering.
	static constexpr int32 CANDIDATE_OVER_SAMPLEMULTIPLIER = 8;

	// Hard cap to avoid pathological memory/cpu usage.
	static constexpr int32 CANDIDATE_HARDCAP = 128;

	static constexpr float MIN_INFLUENCE_THRESHOLD = 0.01f;

	static constexpr float GRADIENT_FALLBACK_THRESHOLDSQ = 0.05f;
	
	static constexpr int32 GRID_TRACE_STRIDE = 2;
}

using namespace  TCATQueryConstants;

UENUM(BlueprintType)
enum class ETCATDistanceBias : uint8
{
	/** Distance is ignored. Always returns a score of 1.0. */
	None UMETA(DisplayName = "None (Ignore Distance)"),

	/** [Formula: 1 - x]
	 * Standard linear falloff.
	 * * The score decreases evenly as the distance increases.
	 * @usage Best for general movement costs, fuel consumption, or simple proximity checks.
	 */
	Linear UMETA(DisplayName = "Linear (Standard)"),

	/** [Formula: 1 - x^2]
	 * Influence stays strong for a long time, then drops rapidly near the max radius.
	 * * The curve is convex (bulges outward). 
	 * * Even at 50% distance, the score remains high (75%).
	 * @usage Use for "Ranged Attacks" or "AoE Buffs". 
	 * (e.g., A sniper is almost as accurate at mid-range as they are at close-range).
	 */
	SlowDecay UMETA(DisplayName = "Slow Decay (Maintains Strength)"), // User's "Relaxed"

	/** [Formula: (1 - x)^2]
	 * Influence drops significantly with even a slight increase in distance.
	 * * The curve is concave (dips inward).
	 * * At 50% distance, the score drops drastically to 25%.
	 * @usage Use for "Melee Attacks", "Sense of Smell/Heat", or "Strict Safety Zones".
	 * (e.g., Being slightly out of position is very dangerous).
	 */
	FastDecay UMETA(DisplayName = "Fast Decay (Drops Quickly)"), // User's "Focused"
};

#if ENABLE_VISUAL_LOG
struct FTCATQueryDebugInfo
{
	TWeakObjectPtr<const AActor> DebugOwner;
	FLinearColor BaseColor = FLinearColor::Black;
	float HeightOffset = 0.0f;
	int32 SampleStride = 0;
	bool bEnabled = false;

	bool IsValid() const { return bEnabled && DebugOwner.IsValid(); }
};
#endif

struct FTCATBatchQuery
{
	bool bIsCancelled = false;

	// Basic Info
	ETCATQueryType Type;
	FName MapTag;
	int32 MaxResults = 1;
	uint32 RandomSeed = 0;

	// Search Area
	FVector Center;
	float SearchRadius;

	// Condition (Optional)
	float CompareValue;
	ETCATCompareType CompareType;

	// Source Info (for Self Influence Removal)
	TWeakObjectPtr<UCurveFloat> Curve;
	float SelfRemovalFactor = 0.0f;
	float InfluenceRadius = 0.0f;
	float InfluenceHalfHeight = 0.0f;

	// Options
	bool bIgnoreZValue = false;
	bool bExcludeUnreachableLocation = false;
	bool bTraceVisibility = false;

	ETCATDistanceBias DistanceBiasType = ETCATDistanceBias::None;
	float DistanceBiasWeight = 0.0f;
	
	TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>> OutResults;
	TFunction<void(const TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>&)> OnComplete;

#if ENABLE_VISUAL_LOG
	FTCATQueryDebugInfo DebugInfo;
#endif
};

struct FTCATBatchTickFunction : public FTickFunction
{
	struct FTCATQueryProcessor* Processor;
	
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("TCATBatchTickFunction"); }
};