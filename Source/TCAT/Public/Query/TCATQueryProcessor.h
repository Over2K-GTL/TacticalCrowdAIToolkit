// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Query/TCATQueryTypes.h"

struct FTCATQueryContext;

struct FTCATSearchCandidate
{
	float Value;
	FVector WorldPos;
};

 /**
 * Batch executor for TCAT influence queries.
 *
 * When to use:
 * - You have many AI agents requesting "best/worst spot" or "is there any safe point" every frame.
 * - You want to centralize expensive filters (NavMesh reachability / LoS) and keep caller logic thin.
 *
 * How it runs:
 * - Call EnqueueQuery() from gameplay/BT/async actions.
 * - ExecuteBatch() is called by the subsystem tick (FTCATBatchTickFunction).
 * - Results are dispatched back on the Game Thread via the query's OnComplete.
 */
struct TCAT_API FTCATQueryProcessor
{
public:
	/**
	 * Initializes the query processor and binds it to the world data.
	 * @param InWorld      The world context for debug drawing and spatial queries.
	 * @param InVolumesPtr Pointer to the map-grouped volume registry in UTCATSubsystem.
	 *                     This pointer must remain valid for the lifetime of the processor.
	 */
	void Initialize(UWorld* InWorld, const TMap<FName, TSet<class ATCATInfluenceVolume*>>* InVolumesPtr);
	
	/**
	 * Provides the baked curve atlas (LUT) for fast sampling.
	 * If not set, queries can still sample raw UCurveFloat assets (slower and not deterministic in shipping builds).
	 */
	void SetCurveAtlasData(const TArray<float>* InAtlasData, int32 InAtlasWidth);

	/** 
	 * Cleans up resources and unregisters the tick function.
	 * Must be called before the subsystem is destroyed to prevent crashes.
	 */
	void Shutdown();
	
	/**
	 * Adds a new search query to the batch processing queue.
	 * The query will be processed in the next subsystem tick.
	 * 
	 * @param NewQuery The query data to be added. Passed as an R-value reference (via MoveTemp) to avoid unnecessary memory copying.
	 * @return A unique Query ID (Handle). This ID is required if you wish to cancel the query later via CancelQuery().
	 */
	uint32 EnqueueQuery(FTCATBatchQuery&& NewQuery);

	/**
	 * Attempts to cancel a pending query using its specific Handle ID.
	 * If the query is found in the waiting queue, it will be marked as cancelled and its result callback will not be broadcast.
	 * 
	 * @param QueryID The unique handle returned by EnqueueQuery.
	 */
	void CancelQuery(uint32 QueryID);

	/**
	 * Processes all queued queries for the current frame.
	 * 
	 * Execution Strategy:
	 * - If the number of queries is small, they are processed sequentially on the Game Thread to avoid task overhead.
	 * - If the load is high, queries are distributed across background threads using ParallelFor.
	 * 
	 * After processing, results are dispatched to the Game Thread listeners.
	 */
	void ExecuteBatch();

	void ProcessQueryImmediate(FTCATBatchQuery& InOutQuery);
private:
	/** Core logic for a single query execution. Designed to be Thread-safe. */
	void ProcessSingleQuery(FTCATBatchQuery& Q);

	/** Triggers completion callbacks on the Game Thread. */
	void DispatchResults(TArray<FTCATBatchQuery>& ResultQueue);

private:
	// ================================================
	// Search Value Logic
	// ================================================
    bool SearchConditionInternal(const FTCATQueryContext& Context, FVector& OutPos) const;
    
    float SearchHighestInternal(const FTCATQueryContext& Context, FTCATQueryResultArray& OutResults) const;
    
    float SearchLowestInternal(const FTCATQueryContext& Context, FTCATQueryResultArray& OutResults) const;
	
    float SearchHighestInConditionInternal(const FTCATQueryContext& Context, FTCATQueryResultArray& OutResults) const;
    
    float SearchLowestInConditionInternal(const FTCATQueryContext& Context, FTCATQueryResultArray& OutResults) const;
    
    float GetValueAtInternal(const FTCATQueryContext& Context) const;

	/**
	 * Returns either:
	 * - A normalized direction vector (when LookAheadDistance == 0),
	 * - Or a target world position (when LookAheadDistance != 0).
	 *
	 * Typical usage: steer AI movement along improving influence rather than "teleport to best cell".
	 */
    FVector GetGradientInternal(const FTCATQueryContext& Context, float LookAheadDistance) const;
	
	// ================================================
	// Helpers
	// ================================================
	void InsertTopKHighest(const FTCATSearchCandidate& Candidate, const int32 MaxCount, TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& InOut) const;
	void InsertTopKLowest(const FTCATSearchCandidate& Candidate, const int32 MaxCount, TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& InOut) const;

	/**
	 * Applies optional expensive filters (reachability / LoS) on a pre-sorted candidate list.
	 * Designed to keep query scans cheap by delaying expensive checks until the end.
	 */
	void FindTopReachableCandidates(const FTCATQueryContext& Context, const TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& Candidates, 
		 FTCATQueryResultArray& OutResults) const;

	bool IsPositionReachable(const FVector& From, const FVector& To) const;
	bool HasLineOfSight(const FVector& From, const FVector& To) const;

	/** Heightfield/grid-based LoS. Use this when you want "terrain occlusion" without physics traces. */
	bool CheckGridLineOfSight(const ATCATInfluenceVolume* Volume, const FVector& Start, const FVector& End) const; //Grid-based Line of Sight check.  

	/**
	 * Produces the final score used for ranking:
	 * - Tie-break jitter
	 * - Self influence removal if it's available
	 * - Optional distance bias
	 */
	float CalculateModifiedValue(const FTCATQueryContext& Context, float RawValue, const FVector& CellWorldPos, int32 GridX, int32 GridY) const;

	float EvaluateCurveValue(const UCurveFloat* Curve, int32 CurveIndex, float NormalizedDistance) const;
	float SampleCurveAtlas(int32 CurveIndex, float NormalizedDistance) const;

	/** Computes theoretical bounds for early rejection (used to avoid expensive work on hopeless cells). */
	static void CalculatePotentialDelta(const UCurveFloat& Curve, float Factor, float& OutMaxAdd, float& OutMaxSub);

	/**
	 * Iterates all grid cells inside a circle across relevant volumes.
	 * The callback can early-stop by returning true.
	 *
	 * Performance note: keep ProcessCell cheap—this is the hot loop.
	 */
	void ForEachCellInCircle(const FTCATQueryContext& Context, TFunctionRef<bool(float, const ATCATInfluenceVolume*, int32, int32)> ProcessCell) const;

#if ENABLE_VISUAL_LOG
	void VLogQueryDetails(const struct FTCATBatchQuery& Query) const;
	void VLogQueryDetails(const struct FTCATQueryContext& Context, const struct FTCATBatchQuery& Query) const;
	void DrawPersistentDebug();

	struct FQueryDebugFrame
	{
		TArray<FVector> CellPositions;
		TArray<FColor> CellColors;
		TArray<FString> CellTexts;
		TArray<FVector> ResultPositions;
		TArray<FString> ResultTexts;
		double ExpireTime = 0.0;
	};
#endif
	// ================================================
	// Member Variables
	// ================================================
	/** Pending queries for the current frame. */
	TArray<FTCATBatchQuery> QueryQueue;

	/** Weak reference to the volume data owned by UTCATSubsystem. */
	const TMap<FName, TSet<class ATCATInfluenceVolume*>>* MapGroupedVolumes = nullptr;

	UWorld* CachedWorld = nullptr;
		
	/** Custom tick function instance owned by this processor. */
	FTCATBatchTickFunction TickFunction{};

	/** Cached curve atlas data used for fast LUT sampling. */
	const TArray<float>* CurveAtlasData = nullptr;
	int32 CurveAtlasWidth = 0;
	int32 CurveAtlasRowCount = 0;

#if ENABLE_VISUAL_LOG
	mutable FQueryDebugFrame LastDebugFrame;
#endif
};
