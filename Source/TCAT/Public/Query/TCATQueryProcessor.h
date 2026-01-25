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
 * Handles batch processing of influence map queries.
 * Optimized for cache locality and multi-threaded execution.
 * 
 * This processor manages a queue of async queries and executes them during the Subsystem Tick.
 * It automatically switches between Single-Threaded and Parallel execution based on the workload.
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

private:
	/** Core logic for a single query execution. Designed to be Thread-safe. */
	void ProcessSingleQuery(FTCATBatchQuery& Q);

	/** Triggers completion callbacks on the Game Thread. */
	void DispatchResults(TArray<FTCATBatchQuery>& ResultQueue, int32 DebugMode, const FString& DebugFilter);

private:
	// ================================================
	// Search Value Logic
	// ================================================
    bool SearchConditionInternal(const FTCATQueryContext& Context, FVector& OutPos) const;
    
    float SearchHighestInternal(const FTCATQueryContext& Context, TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& OutResults) const;
    
    float SearchLowestInternal(const FTCATQueryContext& Context, TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& OutResults) const;
	
    float SearchHighestInConditionInternal(const FTCATQueryContext& Context, TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& OutResults) const;
    
    float SearchLowestInConditionInternal(const FTCATQueryContext& Context, TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& OutResults) const;
    
    float GetValueAtInternal(const FTCATQueryContext& Context) const;
    
    FVector GetGradientInternal(const FTCATQueryContext& Context, float LookAheadDistance) const;
	
	// ================================================
	// Helper Func
	// ================================================
	void InsertTopKHighest(const FTCATSearchCandidate& Candidate, const int32 MaxCount, TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& InOut) const;
	void InsertTopKLowest(const FTCATSearchCandidate& Candidate, const int32 MaxCount, TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& InOut) const;
	
	void FindTopReachableCandidates(const FTCATQueryContext& Context, const TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& Candidates, 
		 TArray<FTCATSingleResult, TInlineAllocator<INLINE_RESULT_CAPACITY>>& OutResults) const;
    
	bool IsPositionReachable(const FVector& From, const FVector& To) const;
	bool HasLineOfSight(const FVector& From, const FVector& To) const;
	bool CheckGridLineOfSight(const ATCATInfluenceVolume* Volume, const FVector& Start, const FVector& End) const; //Grid-based Line of Sight check.  

	static float CalculateModifiedValue(const FTCATQueryContext& Context, float RawValue, const FVector& CellWorldPos, int32 GridX, int32 GridY);
	static float CalculateSelfInfluence(const UCurveFloat& Curve, float Distance, float InfluenceRadius);
	static void CalculatePotentialDelta(const UCurveFloat& Curve, float Factor, float& OutMaxAdd, float& OutMaxSub);
	
	void ForEachCellInCircle(const FTCATQueryContext& Context, TFunctionRef<bool(float, const ATCATInfluenceVolume*, int32, int32)> ProcessCell) const;

#if ENABLE_VISUAL_LOG
	void VLogQueryDetails(const struct FTCATBatchQuery& Query) const;
	void VLogQueryDetails(const struct FTCATQueryContext& Context, const struct FTCATBatchQuery& Query) const;
	bool ShouldDrawQueryDebug(const FTCATBatchQuery& Query, int32 DebugMode, const FString& DebugFilter) const;
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
};