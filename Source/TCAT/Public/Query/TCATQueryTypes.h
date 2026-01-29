// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "Core/TCATTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "VisualLogger/VisualLogger.h"
#include "TCATQueryTypes.generated.h"

class UCurveFloat;
class AActor;

/**
 * Pick the smallest set that matches what you need:
 * - Highest/Lowest: "give me the best/worst spot"
 * - Condition: "is there any spot that satisfies a threshold?"
 * - ValueAtPos: "sample the layer at a point"
 * - Gradient: "which direction improves the score?"
 *
 * Note: some query types can be combined at a higher level (e.g., HighestValueInCondition),
 * but the processor handles them as distinct paths for performance and clarity.
 */
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

/** A single result entry returned from a query. */
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
	/**
	 * Inline capacity for the typical "single-result" use case (MaxResults=1),
	 * while still allowing small multi-result queries without heap allocations.
	 */
	static constexpr int32 INLINE_RESULT_CAPACITY = 8;

	/** Curve atlas samples per curve row (LUT). Must match bake settings. */
	static constexpr int32 CURVE_SAMPLE_COUNT = 256;

	/**
	 * When reachability filtering is enabled, we keep extra candidates first,
	 * then discard unreachable ones. This multiplier controls that oversampling.
	 */
	static constexpr int32 CANDIDATE_OVER_SAMPLEMULTIPLIER = 8;

	/** Hard cap to prevent worst-case spikes when users request huge result counts. */
	static constexpr int32 CANDIDATE_HARDCAP = 128;

	/** Small cutoff to ignore numerical noise / near-zero influence. */
	static constexpr float MIN_INFLUENCE_THRESHOLD = 0.01f;

	/**
	 * If the accumulated gradient vector becomes too small (cancelling directions),
	 * we fall back to "direction to the best cell" to avoid returning near-zero.
	 */
	static constexpr float GRADIENT_FALLBACK_THRESHOLDSQ = 0.05f;

	/**
	 * Grid LoS trace step in cells.
	 * Larger stride is faster but can miss thin obstacles in heightfield LoS mode.
	 */
	static constexpr int32 GRID_TRACE_STRIDE = 2;
}

using namespace  TCATQueryConstants;

/**
 * Inline-allocated result container used by TCAT query callbacks.
 *
 * Why inline allocator?
 * - Most queries request a small number of results (Top(1..N)).
 * - Inline allocation avoids heap churn in hot AI/gameplay loops.
 *
 * You can treat it like a normal TArray for reading results.
 */
using FTCATQueryResultArray = TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>;

#if ENABLE_VISUAL_LOG
struct FTCATQueryDebugInfo
{
	TWeakObjectPtr<const AActor> DebugOwner;
	FLinearColor BaseColor = FLinearColor::Black;
	float HeightOffset = 0.0f;
	int32 SampleStride = 0;

	bool IsValid() const
	{
#if WITH_EDITOR
		const AActor* Owner = DebugOwner.Get();
		return Owner && Owner->IsSelected();
#else
		return DebugOwner.IsValid();
#endif
	}
};
#endif

/**
 * Internal representation of a queued query request.
 *
 * This is the "wire format" between Blueprint/Gameplay callers and the batch processor.
 * If you add a new option, prefer a single flag/value here so it stays batch-friendly.
 */
struct

FTCATBatchQuery
{
	/** If true, this query has been cancelled and will be skipped. */
	bool bIsCancelled = false;

	// Where to query
	ETCATQueryType Type;
	FName MapTag;
	int32 MaxResults = 1;
	uint32 RandomSeed = 0;

	// Where to query
	FVector SearchCenter;
	float SearchRadius;

	// Condition (Optional)
	float CompareValue;
	ETCATCompareType CompareType;

	// Self influence removal (optional)
	TWeakObjectPtr<UCurveFloat> Curve;
	int32 SelfCurveID = INDEX_NONE;
	float SelfRemovalFactor = 0.0f;
	float InfluenceRadius = 0.0f;
	FVector SelfOrigin = FVector::ZeroVector;

	// Extra filters / scoring tweaks
	bool bExcludeUnreachableLocation = false;	// NavMesh reachability filter.
	bool bTraceVisibility = false;				// Heightfield/grid LoS filter.
	bool bIgnoreZValue = false;

	// Distance bias: add/subtract a curve score based on normalized distance to center.
	// Useful to prefer "nearby decent" vs "far perfect", depending on the curve + weight.
	TWeakObjectPtr<UCurveFloat> DistanceBiasCurve;
	int32 DistanceBiasCurveID = INDEX_NONE;
	float DistanceBiasWeight = 0.0f;
	
	/** Container for the final query results. */
	FTCATQueryResultArray OutResults;
	
	/** Callback function executed on the Game Thread upon completion. */
	TFunction<void(const FTCATQueryResultArray&)> OnComplete;

#if ENABLE_VISUAL_LOG
	FTCATQueryDebugInfo DebugInfo;
#endif
};

/**
 * Custom tick function that lets the subsystem run query batches during the frame update loop.
 * This is intentionally a separate FTickFunction so it can be registered/unregistered cleanly.
 */
struct FTCATBatchTickFunction : public FTickFunction
{
	struct FTCATQueryProcessor* Processor;
	
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("TCATBatchTickFunction"); }
};
