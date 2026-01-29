// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TCATSubsystem.h"
#include "Scene/TCATInfluenceComponent.h"

/**
 * Fluent C++ builder for submitting TCAT influence map queries.
 *
 * Why this exists
 * - EQS/BT integration is great for designers, but C++ gameplay code often needs a lightweight, readable API.
 * - This builder lets you compose a query in a single expression and run it either:
 *   - Asynchronously (preferred for expensive scans), or
 *   - Immediately (synchronous; use sparingly).
 *
 * Typical usage
 *   // Find the best spot within 600cm around the actor
 *   TCAT->MakeQuery("Enemy")
 *       .From(MyActor)
 *       .SearchRadius(600.f)
 *       .FindHighest()
 *       .Top(1)
 *       .SubmitAsync([](const FTCATQueryResultArray& Results){ ... });
 *
 * Notes / gotchas
 * - You must provide a valid SearchCenter via From(...) unless you use GetValueAt(...).
 * - Where(...) does NOT automatically switch the mode to Condition. Instead it turns Highest/Lowest into
 *   "Highest/Lowest *within* condition" at finalize time.
 * - RunImmediate* executes on the Game Thread. Prefer SubmitAsync for anything that scans an area.
 */
class TCAT_API FTCATQueryBuilder
{
public:
	/** Creates a builder bound to a TCAT subsystem and a target influence layer tag. */
	FTCATQueryBuilder(UTCATSubsystem* InSubsystem, FName InMapTag)
		: TargetSubsystem(InSubsystem)
	{
		Query.MapTag = InMapTag;
		Query.MaxResults = 1;
		Query.Type = ETCATQueryType::HighestValue;
		Query.CompareType = ETCATCompareType::Greater;

		Query.bIgnoreZValue = false; // Default: Respect Height Map
		Query.bExcludeUnreachableLocation = false;
		Query.bTraceVisibility = false;
		
		// Internal flag used to upgrade Highest/Lowest into Highest/LowestInCondition during FinalizeQuery().
		bHasCondition = false;
	}
	~FTCATQueryBuilder() = default;

	// =========================================================
	// 1. Basic Setup
	// =========================================================
	/** Sets the query search origin in world space. */
	FORCEINLINE FTCATQueryBuilder& From(FVector SearchCenter)
	{
		Query.SearchCenter = SearchCenter;
		return *this;
	}

	/**
	 * Sets the search origin from an Actor's location.
	 * Also registers this actor as the debug owner (used by TCAT debug drawing / Visual Logger).
	 */
	FORCEINLINE FTCATQueryBuilder& From(const AActor* Actor)
	{
		if (Actor)
		{
			Query.SearchCenter = Actor->GetActorLocation();
#if ENABLE_VISUAL_LOG
			Query.DebugInfo.DebugOwner = Actor;
#endif
		}
		return *this;
	}

	/**
	 * Sets the search origin from an Influence Component's resolved world location.
	 * Convenience: automatically calls IgnoreSelf(Comp) so the agent does not "vote for itself"
	 * when evaluating the map (useful for "find safe spot" style queries).
	 */
	FORCEINLINE FTCATQueryBuilder& From(const UTCATInfluenceComponent* Comp)
	{
		if (Comp)
		{
			Query.SearchCenter = Comp->ResolveWorldLocation();
#if ENABLE_VISUAL_LOG
			Query.DebugInfo.DebugOwner = Comp->GetOwner();
#endif
        
			IgnoreSelf(Comp); 
		}
		return *this;
	}

	/** Sets the search radius (cm). For sampling a single point, use GetValueAt(...). */
	FORCEINLINE FTCATQueryBuilder& SearchRadius(float InRadius)
	{
		Query.SearchRadius = InRadius;
		return *this;
	}
	
	// =========================================================
	// 2. Query Mode & Condition
	// =========================================================
	
	/** Finds the highest influence value in the search area. */
	FORCEINLINE FTCATQueryBuilder& FindHighest()
	{
		Query.Type = ETCATQueryType::HighestValue;
		return *this;
	}

	/** Finds the lowest influence value in the search area. */
	FORCEINLINE FTCATQueryBuilder& FindLowest()
	{
		Query.Type = ETCATQueryType::LowestValue;
		return *this;
	}

	/**
	 * Condition-only query.
	 * Use with Where(...) to answer "Is there any location that satisfies this condition?"
	 * (Depending on the processor implementation, this may return a matching location/value or just success/failure.)
	 */
	FORCEINLINE FTCATQueryBuilder& FindAny()
	{
		Query.Type = ETCATQueryType::Condition;
		return *this;
	}

	/**
	 * Computes the influence gradient around the search center.
	 * @param LookAheadDistance If > 0, the processor may return a projected point along the gradient direction.
	 *                          If 0, the processor may return a normalized direction (implementation-dependent).
	 */
	FORCEINLINE FTCATQueryBuilder& FindGradient(float LookAheadDistance = 0.0f)
	{
		Query.Type = ETCATQueryType::Gradient;
		Query.CompareValue = LookAheadDistance;
		return *this;
	}

	/**
	 * Samples the influence value at a single world position.
	 * This forces SearchRadius=0 and overrides SearchCenter.
	 */
	FORCEINLINE FTCATQueryBuilder& GetValueAt(FVector Location)
	{
		Query.Type = ETCATQueryType::ValueAtPos;
		Query.SearchCenter = Location;
		Query.SearchRadius = 0.0f;
		Query.bIgnoreZValue = true;
		return *this;
	}
	
	/**
	 * Adds a comparison condition used for filtering.
	 *
	 * Behavior
	 * - If the query mode is Highest/Lowest, TCAT will treat this as:
	 *     "Find Highest/Lowest value among candidates that satisfy the condition"
	 *   by upgrading the query type during FinalizeQuery().
	 * - If you want a pure condition check, call FindAny().Where(...).
	 */
	FORCEINLINE FTCATQueryBuilder& Where(float Value, ETCATCompareType Op = ETCATCompareType::Greater)
	{
		Query.CompareValue = Value;
		Query.CompareType = Op;
		bHasCondition = true;
		return *this;
	}

	// =========================================================
	// 3. Filters & Scoring
	// =========================================================

	/** Excludes candidates that are not reachable on the NavMesh (costly; requires navigation data). */
	FORCEINLINE FTCATQueryBuilder& ReachableOnly(bool bEnable = true)
	{
		Query.bExcludeUnreachableLocation = bEnable;
		return *this;
	}

	/** Excludes candidates not visible from the search center (costly; uses trace/raycast rules). */
	FORCEINLINE FTCATQueryBuilder& VisibleOnly(bool bEnable = true)
	{
		Query.bTraceVisibility = bEnable;
		return *this;
	}

	/**
	 * Controls whether to ignore the terrain height map during distance/validity checks.
	 * - false (Default): Uses 3D distance including height differences.
	 * - true: Projects everything to 2D plane (ignores Z differences).
	 */
	FORCEINLINE FTCATQueryBuilder& IgnoreHeight(bool bIgnore = true)
	{
		Query.bIgnoreZValue = bIgnore;
		return *this;
	}

	/** Requests the top N results instead of a single best candidate. */
	FORCEINLINE FTCATQueryBuilder& Top(int32 ResultCount)
	{
		Query.MaxResults = ResultCount;
		return *this;
	}

	/**
	 * Subtracts this component's own influence contribution from the evaluation.
	 * Useful when the agent itself is emitting "Danger" and you want "true danger excluding myself".
	 *
	 * Implementation detail (high level)
	 * - TCAT uses a pre-baked "self influence recipe" per layer and a curve-based falloff.
	 * - This builder populates the fields needed by the query processor to remove self contribution.
	 */
	FORCEINLINE FTCATQueryBuilder& IgnoreSelf(const UTCATInfluenceComponent* MyComp)
	{
		if (MyComp && TargetSubsystem.IsValid())
		{
			ATCATInfluenceVolume* Volume = TargetSubsystem->GetInfluenceVolume(Query.MapTag);
			if (Volume)
			{
				FTCATSelfInfluenceResult Result = MyComp->GetSelfInfluenceResult(Query.MapTag, Volume);
				if (Result.IsValid())
				{
					Query.Curve = Result.Curve;
					Query.SelfCurveID = Result.CurveTypeIndex;
					Query.SelfRemovalFactor = Result.FinalRemovalFactor;
					Query.InfluenceRadius = Result.InfluenceRadius;
					Query.SelfOrigin = Result.SourceLocation;
				}
			}
		}
		return *this;
	}

	/**
	 * Applies a distance-based bias to the final score (optional).
	 * - InCurve defines how distance maps to bias (typically 0..1).
	 * - InWeight controls how strongly distance bias competes with raw influence value.
	 */
	FORCEINLINE FTCATQueryBuilder& ApplyDistanceBias(UCurveFloat* InCurve, float InWeight = 1.0f)
	{
		Query.DistanceBiasCurve = InCurve;
		Query.DistanceBiasWeight = InWeight;

		if (InCurve && TargetSubsystem.IsValid())
		{
			Query.DistanceBiasCurveID = TargetSubsystem->GetCurveID(InCurve);
		}
		else
		{
			Query.DistanceBiasCurveID = INDEX_NONE;
		}
		return *this;
	}

	/** Sets the debug owner used for query visualization (Visual Logger / debug draw). */
	FORCEINLINE FTCATQueryBuilder& DebugWith(const AActor* DebugActor)
	{
#if ENABLE_VISUAL_LOG
		Query.DebugInfo.DebugOwner = DebugActor;
#endif
		return *this;
	}

	// =========================================================
	// 4. Execution
	// =========================================================

	/**
	 * Submits the query to TCAT's async batch system.
	 *
	 * Best practice
	 * - Prefer this over RunImmediate* for expensive queries (area scans).
	 * - The callback returns an inline-allocated array to minimize heap allocations.
	 *
	 * @return Query handle / request id (0 if submission failed).
	 */
	uint32 SubmitAsync(TFunction<void(const FTCATQueryResultArray&)> OnComplete)
	{
		if (!TargetSubsystem.IsValid()) return 0;

		FinalizeQuery();

		Query.OnComplete = MoveTemp(OnComplete);

		return TargetSubsystem->RequestBatchQuery(MoveTemp(Query));
	}

	/**
	 * Executes the query synchronously (Game Thread).
	 * Use for cheap queries like ValueAtPos, or when you absolutely need the result immediately.
	 */
	bool RunImmediate(FTCATSingleResult& OutResult)
	{
		if (!TargetSubsystem.IsValid()) return false;
        
		FinalizeQuery();
		
		return TargetSubsystem->ProcessQueryImmediate(Query, OutResult);
	}

	/**
	 * Synchronous execution that returns up to MaxResults results.
	 * Use sparingly; large scans can stall the Game Thread.
	 */
	bool RunImmediateMulti(TArray<FTCATSingleResult>& OutResults)
	{
		if (!TargetSubsystem.IsValid()) return false;

		FinalizeQuery();

		return TargetSubsystem->ProcessQueryImmediateMulti(Query, OutResults);
	}
	
private:
	/**
	 * Finalizes derived settings before execution.
	 * - Builds a deterministic-ish seed (MapTag + Center + FrameCounter) used by tie-breaking jitter.
	 * - Upgrades Highest/Lowest into Highest/LowestInCondition if Where(...) was used.
	 */
	void FinalizeQuery()
	{
		uint32 Seed = GetTypeHash(Query.MapTag);
		Seed = HashCombineFast(Seed, GetTypeHash(Query.SearchCenter));
		Seed = HashCombineFast(Seed, (uint32)GFrameCounter);
		Query.RandomSeed = Seed;
		
		if (bHasCondition)
		{
			if (Query.Type == ETCATQueryType::HighestValue)
			{
				Query.Type = ETCATQueryType::HighestValueInCondition;
			}
			else if (Query.Type == ETCATQueryType::LowestValue)
			{
				Query.Type = ETCATQueryType::LowestValueInCondition;
			}
		}
	}
	
private:
	/** Subsystem this builder submits to (weak to avoid lifetime issues). */
	TWeakObjectPtr<UTCATSubsystem> TargetSubsystem;
	
	/** Accumulated query data that will be submitted/executed. */
	FTCATBatchQuery Query;

	/** Set to true when Where(...) is used so FinalizeQuery() can upgrade query types. */
	bool bHasCondition;
};