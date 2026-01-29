// Copyright 2025-2026 Over2K. All Rights Reserved.
#include "Query/TCATQueryProcessor.h"
#include "NavigationSystem.h"
#include "TCAT.h"
#include "Async/ParallelFor.h"
#include "Core/TCATMathLibrary.h"
#include "Core/TCATSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Scene/TCATInfluenceVolume.h"
#include "VisualLogger/VisualLogger.h"
#include "DrawDebugHelpers.h"
#include "HAL/PlatformTime.h"

DECLARE_CYCLE_STAT(TEXT("Query_ExecuteBatch"), STAT_TCAT_QueryExecuteBatch, STATGROUP_TCAT);
DECLARE_DWORD_COUNTER_STAT(TEXT("Query_Count"), STAT_TCAT_QueryCount, STATGROUP_TCAT);

DECLARE_CYCLE_STAT(TEXT("Query_ForEachCell"), STAT_TCAT_ForEachCell, STATGROUP_TCAT);
DECLARE_CYCLE_STAT(TEXT("Query_TopK_Insert"), STAT_TCAT_TopKInsert, STATGROUP_TCAT);
DECLARE_CYCLE_STAT(TEXT("Query_Reachability"), STAT_TCAT_Reachability, STATGROUP_TCAT);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Query_AvgCellsProcessed"), STAT_TCAT_AvgCells, STATGROUP_TCAT);

static TAutoConsoleVariable<int32> CVarTCATQueryLogStride(
	TEXT("TCAT.Debug.QueryStride"),
	8,
	TEXT("Sampling stride used when visualizing query cells."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarTCATQueryTextOffset(
	TEXT("TCAT.Debug.QueryTextOffset"),
	10.0f,
	TEXT("Z offset applied to Visual Logger text while drawing queries."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarTCATQueryTextDuration(
	TEXT("TCAT.Debug.QueryTextDuration"),
	0.5f,
	TEXT("How long (in seconds) TCAT query debug text remains visible."),
	ECVF_Cheat);

namespace ETCATContextFlags
{
    enum Type : uint8
    {
        None        = 0,
        HasSelfInfluence    = 1 << 0, // 0x01: Self Influence
        HasDistanceBias     = 1 << 1, // 0x02: Distance Curve
        IsLowestQuery    = 1 << 2, // 0x04 : Is Lowest?
        NeedDistance = HasSelfInfluence | HasDistanceBias 
    };
}

struct FTCATQueryContext
{
    // [8/16 Bytes] Large types (Pointers, Vectors)
    const UCurveFloat* Curve = nullptr;
    const UCurveFloat* DistanceBiasCurve = nullptr;
    FVector SearchCenter;
    FVector SelfOrigin;

    // [4 Bytes] Floats & Ints
    FName MapTag;
    float SearchRadius;
    float InfluenceRadius;
    float SelfRemovalFactor = 0.0f;
    float CompareValue;
    
    int32 MaxResults;
    uint32 RandomSeed;

    // [1 Byte] Enum & Bools
    ETCATCompareType CompareType;

    // Bitfields for boolean flags
    uint8 bIgnoreZValue : 1;
    uint8 bExcludeUnreachableLocation : 1;
    uint8 bTraceVisibility : 1;
    uint8 ContextFlags = 0;

    int32 SelfCurveID = INDEX_NONE;
    int32 DistanceCurveID = INDEX_NONE;
    float DistanceBiasWeight = 0.0f;
    
    FTCATQueryContext(const struct FTCATBatchQuery& InQuery)
        : Curve(InQuery.Curve.Get())
        , DistanceBiasCurve(InQuery.DistanceBiasCurve.Get())
        , SearchCenter(InQuery.SearchCenter)
        , SelfOrigin(InQuery.SelfOrigin)
        , MapTag(InQuery.MapTag)
        , SearchRadius(InQuery.SearchRadius)
        , InfluenceRadius(InQuery.InfluenceRadius)
        , SelfRemovalFactor(InQuery.SelfRemovalFactor)
        , CompareValue(InQuery.CompareValue)
        , MaxResults(InQuery.MaxResults)
        , RandomSeed(InQuery.RandomSeed)
        , CompareType(InQuery.CompareType)
        , bIgnoreZValue(InQuery.bIgnoreZValue)
        , bExcludeUnreachableLocation(InQuery.bExcludeUnreachableLocation)
        , bTraceVisibility(InQuery.bTraceVisibility)
        , SelfCurveID(InQuery.SelfCurveID)
        , DistanceCurveID(InQuery.DistanceBiasCurveID)
        , DistanceBiasWeight(InQuery.DistanceBiasWeight)
    {
        if ((InfluenceRadius > KINDA_SMALL_NUMBER) && (FMath::Abs(SelfRemovalFactor)>KINDA_SMALL_NUMBER) && (SelfCurveID != INDEX_NONE || Curve))
        {
            ContextFlags |= ETCATContextFlags::HasSelfInfluence;
        }
        
        if ((SearchRadius > KINDA_SMALL_NUMBER) && (FMath::Abs(DistanceBiasWeight) > KINDA_SMALL_NUMBER) && (DistanceCurveID != INDEX_NONE || DistanceBiasCurve))
        {
            ContextFlags |= ETCATContextFlags::HasDistanceBias;
        }
        
        if (InQuery.Type == ETCATQueryType::LowestValueInCondition || InQuery.Type == ETCATQueryType::LowestValue)
        {
            ContextFlags |= ETCATContextFlags::IsLowestQuery;
        }
    }
};

// ============================================================
// Lifecycle
// ============================================================

void FTCATQueryProcessor::Initialize(UWorld* InWorld, const TMap<FName, TSet<class ATCATInfluenceVolume*>>* InVolumesPtr)
{
    MapGroupedVolumes = InVolumesPtr;
    CachedWorld = InWorld;

    if (CachedWorld)
    {
        TickFunction.Processor = this;
        TickFunction.bCanEverTick = true;
        TickFunction.TickGroup = TG_DuringPhysics;
        TickFunction.bStartWithTickEnabled = true;
        TickFunction.bTickEvenWhenPaused = true;
        TickFunction.RegisterTickFunction(CachedWorld->PersistentLevel);
    }
}

void FTCATQueryProcessor::SetCurveAtlasData(const TArray<float>* InAtlasData, int32 InAtlasWidth)
{
    CurveAtlasData = InAtlasData;
    if (CurveAtlasData && InAtlasWidth > 1)
    {
        CurveAtlasWidth = InAtlasWidth;
        CurveAtlasRowCount = CurveAtlasWidth > 0 ? CurveAtlasData->Num() / CurveAtlasWidth : 0;
    }
    else
    {
        CurveAtlasWidth = 0;
        CurveAtlasRowCount = 0;
    }
}

void FTCATQueryProcessor::Shutdown()
{
    TickFunction.UnRegisterTickFunction();
}

uint32 FTCATQueryProcessor::EnqueueQuery(FTCATBatchQuery&& NewQuery)
{
    return QueryQueue.Emplace(MoveTemp(NewQuery));
}

void FTCATQueryProcessor::CancelQuery(uint32 QueryID)
{
    if (QueryQueue.IsValidIndex(QueryID))
    {
        QueryQueue[QueryID].bIsCancelled = true;
    }
}

// ============================================================
// Batch
// ============================================================
void FTCATQueryProcessor::DispatchResults(TArray<FTCATBatchQuery>& ResultQueue)
{
    for (FTCATBatchQuery& Query : ResultQueue)
    {
#if ENABLE_VISUAL_LOG
        if (Query.DebugInfo.IsValid())
        {
            if (Query.DebugInfo.SampleStride <= 0)
            {
                Query.DebugInfo.SampleStride = CVarTCATQueryLogStride.GetValueOnGameThread();
            }

            if (Query.DebugInfo.HeightOffset <= 0.0f)
            {
                Query.DebugInfo.HeightOffset = CVarTCATQueryTextOffset.GetValueOnGameThread();
            }

            if (Query.DebugInfo.BaseColor == FLinearColor::White)
            {
                Query.DebugInfo.BaseColor = FLinearColor::Green;
            }

            VLogQueryDetails(Query);
        }
#endif
        if (Query.OnComplete)
        {
            Query.OnComplete(Query.OutResults);
        }
    }
}

void FTCATQueryProcessor::ExecuteBatch()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_QueryExecuteBatch);
    SCOPE_CYCLE_COUNTER(STAT_TCAT_QueryExecuteBatch);
    SET_DWORD_STAT(STAT_TCAT_QueryCount, QueryQueue.Num());

#if ENABLE_VISUAL_LOG
    DrawPersistentDebug();
#endif

    while (QueryQueue.Num() > 0)
    {
        const int32 QueryCount = QueryQueue.Num();
        const int32 WorkerCount = FMath::Max(1, FMath::Min(FTaskGraphInterface::Get().GetNumWorkerThreads(), QueryCount));
        ParallelFor(WorkerCount, [&](int32 WorkerIndex)
        {
            for (int32 QueryIndex = WorkerIndex; QueryIndex < QueryCount; QueryIndex += WorkerCount)
            {
                if (QueryQueue[QueryIndex].bIsCancelled) { continue; }
                ProcessSingleQuery(QueryQueue[QueryIndex]);
            }
        });

        TArray<FTCATBatchQuery> WorkingQueue = MoveTemp(QueryQueue);
        QueryQueue.Reset();

        DispatchResults(WorkingQueue);
    }

#if ENABLE_VISUAL_LOG
    DrawPersistentDebug();
#endif
}

/**
 * Executes a single query immediately (synchronous).
 * This is the shared "immediate path" used by the query builder's RunImmediate* APIs.
 *
 * Expected behavior
 * - Runs ProcessSingleQuery(InOutQuery) and fills InOutQuery.OutResults (if any).
 * - Does not schedule background work; everything happens on the calling thread.
 */
void FTCATQueryProcessor::ProcessQueryImmediate(FTCATBatchQuery& InOutQuery)
{
    ProcessSingleQuery(InOutQuery);
}

void FTCATQueryProcessor::ProcessSingleQuery(FTCATBatchQuery& Query)
{
    Query.OutResults.Reset();
    
    FTCATQueryContext Context(Query);

    switch (Query.Type)
    {
    case ETCATQueryType::HighestValue:
        SearchHighestInternal(Context, Query.OutResults);
        break;

    case ETCATQueryType::LowestValue:
        SearchLowestInternal(Context, Query.OutResults);
        break;

    case ETCATQueryType::HighestValueInCondition:
        SearchHighestInConditionInternal(Context, Query.OutResults);
        break;

    case ETCATQueryType::LowestValueInCondition:
        SearchLowestInConditionInternal(Context, Query.OutResults);
        break;

    case ETCATQueryType::Condition:
        {
            FVector FoundPos = FVector::ZeroVector;
            if (SearchConditionInternal(Context, FoundPos))
            {
                Query.OutResults.Add({ Query.CompareValue, FoundPos });
            }
            break;
        }

    case ETCATQueryType::ValueAtPos:
        {
            const float Value = GetValueAtInternal(Context);
            Query.OutResults.Add({ Value, Context.SearchCenter });
            break;
        }

    case ETCATQueryType::Gradient:
        {
            const FVector GradientDir = GetGradientInternal(Context, Query.CompareValue);
            if (!GradientDir.IsNearlyZero())
            {
                Query.OutResults.Add({ 1.0f, GradientDir });
            }
            break;
        }
    default: break;
    }
}

// ============================================================
// Core Search
// ============================================================
bool FTCATQueryProcessor::SearchConditionInternal(const FTCATQueryContext& Context, FVector& OutPos) const
{
    bool bFound = false;

    ForEachCellInCircle(Context,
        [&](float RawValue, const ATCATInfluenceVolume* Volume, int32 GridX, int32 GridY) -> bool
        {
            if (!Volume)
            {
                OutPos = Context.SearchCenter;
                bFound = false;
                return true; // Stop
            }
            
            const float CellSize = Volume->GetCellSize();
            FVector CellWorldPos = Volume->GetGridOrigin();
            CellWorldPos.X += (GridX * CellSize) + (CellSize * 0.5f);
            CellWorldPos.Y += (GridY * CellSize) + (CellSize * 0.5f);
            CellWorldPos.Z = Context.bIgnoreZValue ? Context.SearchCenter.Z : Volume->GetGridHeightIndex({GridX, GridY});
            
            const float FinalValue = CalculateModifiedValue(Context, RawValue, CellWorldPos, GridX, GridY);
            
            if (UTCATMathLibrary::CompareFloat(FinalValue, Context.CompareValue, Context.CompareType))
            {
                OutPos = CellWorldPos;
                bFound = true;
                return true; // Stop
            }
            return false;
       });

    return bFound;
}

float FTCATQueryProcessor::SearchHighestInternal(const FTCATQueryContext& Context, FTCATQueryResultArray& OutResults) const
{
    const int32 MaxCandidates = !Context.bExcludeUnreachableLocation ? Context.MaxResults :  
        FMath::Clamp(Context.MaxResults * CANDIDATE_OVER_SAMPLEMULTIPLIER, Context.MaxResults, CANDIDATE_HARDCAP);

    TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>> TopCandidates;
    TopCandidates.Reserve(FMath::Min(MaxCandidates, CANDIDATE_HARDCAP));

    float MaxPotentialGain = 0.0f;
    float Dummy = 0.0f;
    
    if (Context.ContextFlags & ETCATContextFlags::HasSelfInfluence)
    {
        CalculatePotentialDelta(*Context.Curve, Context.SelfRemovalFactor, MaxPotentialGain, Dummy);    
    }
    MaxPotentialGain += KINDA_SMALL_NUMBER;

    float CurrentMinInTopK = -FLT_MAX;
    
    ForEachCellInCircle(Context, 
            [&](float RawValue, const ATCATInfluenceVolume* Volume, int32 GridX, int32 GridY) -> bool
            {
                if (!Volume) return false; 

                if (TopCandidates.Num() >= Context.MaxResults)
                {
                    if (RawValue + MaxPotentialGain <= CurrentMinInTopK)
                    {
                        return false;
                    }
                }

                const float CellSize = Volume->GetCellSize();
                FVector CellWorldPos = Volume->GetGridOrigin();
                CellWorldPos.X += (GridX * CellSize) + (CellSize * 0.5f);
                CellWorldPos.Y += (GridY * CellSize) + (CellSize * 0.5f);
                CellWorldPos.Z = Context.bIgnoreZValue ? Context.SearchCenter.Z : Volume->GetGridHeightIndex({GridX, GridY});

                const float FinalValue = CalculateModifiedValue(Context, RawValue, CellWorldPos, GridX, GridY);

                if (TopCandidates.Num() >= MaxCandidates)
                {
                    if (FinalValue <= CurrentMinInTopK)
                    {
                        return false;
                    }
                }

                InsertTopKHighest({ FinalValue, CellWorldPos }, MaxCandidates, TopCandidates);

                if (TopCandidates.Num() >= MaxCandidates)
                {
                    float NewMin = FLT_MAX;
                    for (const auto& C : TopCandidates)
                    {
                        if (C.Value < NewMin) NewMin = C.Value;
                    }
                    CurrentMinInTopK = NewMin;
                }
                
                    return false;
                });

    TopCandidates.Sort([](const FTCATSearchCandidate& A, const FTCATSearchCandidate& B) { return A.Value > B.Value; });

    FindTopReachableCandidates(Context, TopCandidates, OutResults);

    return (OutResults.Num() > 0) ? OutResults[0].Value : -FLT_MAX;
}

float FTCATQueryProcessor::SearchLowestInternal(const FTCATQueryContext& Context, FTCATQueryResultArray& OutResults) const
{
    const int32 MaxCandidates = !Context.bExcludeUnreachableLocation ? Context.MaxResults :  
        FMath::Clamp(Context.MaxResults * CANDIDATE_OVER_SAMPLEMULTIPLIER, Context.MaxResults, CANDIDATE_HARDCAP);

    TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>> BottomCandidates;
    BottomCandidates.Reserve(FMath::Min(MaxCandidates, CANDIDATE_HARDCAP));

    float Dummy = 0.0f;
    float MaxPotentialPenalty = 0.0f;
    if (Context.ContextFlags & ETCATContextFlags::HasSelfInfluence)
    {
        CalculatePotentialDelta(*Context.Curve, Context.SelfRemovalFactor, Dummy, MaxPotentialPenalty);    
    }
    MaxPotentialPenalty += KINDA_SMALL_NUMBER;

    float CurrentMaxInBottomK = FLT_MAX;
    
    ForEachCellInCircle(Context, 
            [&](float RawValue, const ATCATInfluenceVolume* Volume, int32 GridX, int32 GridY) -> bool
            {
                if (!Volume) return false; 

                if (BottomCandidates.Num() >= Context.MaxResults)
                {
                    if (RawValue - MaxPotentialPenalty >= CurrentMaxInBottomK)
                    {
                        return false;
                    }
                }

                const float CellSize = Volume->GetCellSize();
                FVector CellWorldPos = Volume->GetGridOrigin();
                CellWorldPos.X += (GridX * CellSize) + (CellSize * 0.5f);
                CellWorldPos.Y += (GridY * CellSize) + (CellSize * 0.5f);
                CellWorldPos.Z = Context.bIgnoreZValue ? Context.SearchCenter.Z : Volume->GetGridHeightIndex({GridX, GridY});

                const float FinalValue = CalculateModifiedValue(Context, RawValue, CellWorldPos, GridX, GridY);

                if (BottomCandidates.Num() >= MaxCandidates)
                {
                    if (FinalValue >= CurrentMaxInBottomK)
                    {
                        return false;
                    }
                }

                InsertTopKLowest({ FinalValue, CellWorldPos }, MaxCandidates, BottomCandidates);

                if (BottomCandidates.Num() >= MaxCandidates)
                {
                    float NewMax = -FLT_MAX;
                    for (const auto& C : BottomCandidates)
                    {
                        if (C.Value > NewMax) NewMax = C.Value;
                    }
                    CurrentMaxInBottomK = NewMax;
                }
                
                return false;
            });

    if (BottomCandidates.Num() == 0) return FLT_MAX;
    
    // Lowest Value First
    BottomCandidates.Sort([](const FTCATSearchCandidate& A, const FTCATSearchCandidate& B) { return A.Value < B.Value; });

    FindTopReachableCandidates(Context, BottomCandidates, OutResults);

    return (OutResults.Num() > 0) ? OutResults[0].Value : FLT_MAX;
}

float FTCATQueryProcessor::SearchHighestInConditionInternal(const FTCATQueryContext& Context, 
    FTCATQueryResultArray& OutResults) const
{
    const int32 MaxCandidates = !Context.bExcludeUnreachableLocation ? Context.MaxResults :  
        FMath::Clamp(Context.MaxResults * CANDIDATE_OVER_SAMPLEMULTIPLIER, Context.MaxResults, CANDIDATE_HARDCAP);

    TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>> TopCandidates;
    TopCandidates.Reserve(FMath::Min(MaxCandidates, CANDIDATE_HARDCAP));

    float MaxPotentialGain = 0.0f;
    float Dummy = 0.0f;
    if (Context.ContextFlags & ETCATContextFlags::HasSelfInfluence)
    {
        CalculatePotentialDelta(*Context.Curve, Context.SelfRemovalFactor, MaxPotentialGain, Dummy);
    }
    MaxPotentialGain += KINDA_SMALL_NUMBER;

    float CurrentMinInTopK = -FLT_MAX;

    ForEachCellInCircle(Context,
        [&](float RawValue, const ATCATInfluenceVolume* Volume, int32 GridX, int32 GridY) -> bool
        {
            if (!Volume) return false;

            // Early reject with potential gain
            if (TopCandidates.Num() >= MaxCandidates)
            {
                if (RawValue + MaxPotentialGain <= CurrentMinInTopK)
                {
                    return false;
                }
            }

            // Calculate world position
            const float CellSize = Volume->GetCellSize();
            FVector CellWorldPos = Volume->GetGridOrigin();
            CellWorldPos.X += (GridX * CellSize) + (CellSize * 0.5f);
            CellWorldPos.Y += (GridY * CellSize) + (CellSize * 0.5f);
            CellWorldPos.Z = Context.bIgnoreZValue ? Context.SearchCenter.Z : Volume->GetGridHeightIndex({GridX, GridY});

            // Calculate final value
            const float FinalValue = CalculateModifiedValue(Context, RawValue, CellWorldPos, GridX, GridY);

            // Condition check
            if (!UTCATMathLibrary::CompareFloat(FinalValue, Context.CompareValue, Context.CompareType))
            {
                return false;
            }

            // Second check with final value
            if (TopCandidates.Num() >= MaxCandidates)
            {
                if (FinalValue <= CurrentMinInTopK)
                {
                    return false;
                }
            }

            InsertTopKHighest({ FinalValue, CellWorldPos }, MaxCandidates, TopCandidates);

            // Update threshold
            if (TopCandidates.Num() >= MaxCandidates)
            {
                float NewMin = FLT_MAX;
                for (const auto& C : TopCandidates)
                {
                    if (C.Value < NewMin) NewMin = C.Value;
                }
                CurrentMinInTopK = NewMin;
            }

            return false;
        });

    if (TopCandidates.Num() == 0) return -FLT_MAX;

    TopCandidates.Sort([](const FTCATSearchCandidate& A, const FTCATSearchCandidate& B) { return A.Value > B.Value; });

    FindTopReachableCandidates(Context, TopCandidates, OutResults);

    return (OutResults.Num() > 0) ? OutResults[0].Value : -FLT_MAX;
}

float FTCATQueryProcessor::SearchLowestInConditionInternal(const FTCATQueryContext& Context,
    FTCATQueryResultArray& OutResults) const
{
    const int32 MaxCandidates = !Context.bExcludeUnreachableLocation ? Context.MaxResults :  
        FMath::Clamp(Context.MaxResults * CANDIDATE_OVER_SAMPLEMULTIPLIER, Context.MaxResults, CANDIDATE_HARDCAP);

    TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>> BottomCandidates;
    BottomCandidates.Reserve(FMath::Min(MaxCandidates, CANDIDATE_HARDCAP));

    float Dummy = 0.0f;
    float MaxPotentialPenalty = 0.0f;
    if (Context.ContextFlags & ETCATContextFlags::HasSelfInfluence)
    {
        CalculatePotentialDelta(*Context.Curve, Context.SelfRemovalFactor, MaxPotentialPenalty, Dummy);    
    }
    MaxPotentialPenalty += KINDA_SMALL_NUMBER;

    float CurrentMaxInBottomK = FLT_MAX;

    ForEachCellInCircle(Context,
     [&](float RawValue, const ATCATInfluenceVolume* Volume, int32 GridX, int32 GridY) -> bool
     {
         if (!Volume) return false;

         if (BottomCandidates.Num() >= MaxCandidates)
         {
            if (RawValue - MaxPotentialPenalty >= CurrentMaxInBottomK)
            {
                return false;
            }
         }

         // Calculate world position
         const float CellSize = Volume->GetCellSize();
         FVector CellWorldPos = Volume->GetGridOrigin();
         CellWorldPos.X += (GridX * CellSize) + (CellSize * 0.5f);
         CellWorldPos.Y += (GridY * CellSize) + (CellSize * 0.5f);
         CellWorldPos.Z = Context.bIgnoreZValue ? Context.SearchCenter.Z : Volume->GetGridHeightIndex({GridX, GridY});
              
         // Calculate final value
         const float FinalValue = CalculateModifiedValue(Context, RawValue, CellWorldPos, GridX, GridY);

         // Condition check
         if (!UTCATMathLibrary::CompareFloat(FinalValue, Context.CompareValue, Context.CompareType))
         {
             return false;
         }

         // Second check with final value
         if (BottomCandidates.Num() >= MaxCandidates)
         {
             if (FinalValue >= CurrentMaxInBottomK)
             {
                 return false;
             }
         }

         InsertTopKLowest({ FinalValue, CellWorldPos }, MaxCandidates, BottomCandidates);
 
         // Update threshold
         if (BottomCandidates.Num() >= MaxCandidates)
         {
             float NewMax = -FLT_MAX;
             for (const auto& C : BottomCandidates)
             {
                 if (C.Value > NewMax) NewMax = C.Value;
             }
             CurrentMaxInBottomK = NewMax;
         }
         
         return false;
        });

    if (BottomCandidates.Num() == 0) return FLT_MAX;
    
    BottomCandidates.Sort([](const FTCATSearchCandidate& A, const FTCATSearchCandidate& B) { return A.Value < B.Value; });

    FindTopReachableCandidates(Context, BottomCandidates, OutResults);

    return (OutResults.Num() > 0) ? OutResults[0].Value : FLT_MAX;
}

float FTCATQueryProcessor::GetValueAtInternal(const FTCATQueryContext& Context) const
{
    if (Context.MapTag.IsNone() || !MapGroupedVolumes) return 0.0f;

    const TSet<ATCATInfluenceVolume*>* VolumeSet = MapGroupedVolumes->Find(Context.MapTag);
    if (!VolumeSet) return 0.0f;

    for (ATCATInfluenceVolume* Volume : *VolumeSet)
    {
        if (!IsValid(Volume)) continue;
        if (!Volume->GetCachedBounds().IsInside(Context.SearchCenter)) continue;

        const float InvCellSize = 1.0f / Volume->GetCellSize();
        const FVector Origin = Volume->GetGridOrigin();

        const int32 GridX = FMath::Clamp(FMath::FloorToInt((Context.SearchCenter.X - Origin.X) * InvCellSize), 0, Volume->GetColumns() - 1);
        const int32 GridY = FMath::Clamp(FMath::FloorToInt((Context.SearchCenter.Y - Origin.Y) * InvCellSize), 0, Volume->GetRows() - 1);
        
        return Volume->GetInfluenceFromGrid(Context.MapTag, GridX, GridY);
    }
    return 0.0f;
}

FVector FTCATQueryProcessor::GetGradientInternal(const FTCATQueryContext& Context, float LookAheadDistance) const
{
    FVector GradientVector = FVector::ZeroVector;
    float TotalWeight = 0.0f;

    FVector HighestPos = Context.SearchCenter;
    float HighestValue = -FLT_MAX;
	float LowestValue = FLT_MAX;

    float CenterValue = -FLT_MAX;
    bool bFoundCenter = false;
    
    const FVector SelfCenter = Context.SelfOrigin;

    ForEachCellInCircle(Context,
    [&](float RawValue, const ATCATInfluenceVolume* Volume, int32 GridX, int32 GridY) -> bool
    {
        // Calculate world position
        const float CellSize = Volume->GetCellSize();
        FVector CellWorldPos = Volume->GetGridOrigin();
        CellWorldPos.X += (GridX * CellSize) + (CellSize * 0.5f);
        CellWorldPos.Y += (GridY * CellSize) + (CellSize * 0.5f);
        CellWorldPos.Z = Context.bIgnoreZValue ? Context.SearchCenter.Z : Volume->GetGridHeightIndex({GridX, GridY});
        
        float FinalValue = RawValue;
        if ((Context.ContextFlags & ETCATContextFlags::HasSelfInfluence) && Context.InfluenceRadius > KINDA_SMALL_NUMBER)
        {
            const float SelfDist = FVector::Dist(CellWorldPos, SelfCenter);
            const float NormalizedDist = SelfDist / Context.InfluenceRadius;
            const float CurveVal = EvaluateCurveValue(Context.Curve, Context.SelfCurveID, NormalizedDist);
            FinalValue -= (CurveVal * Context.SelfRemovalFactor);
        }

        if (!bFoundCenter)
        {
             if (FVector::DistSquared(CellWorldPos, Context.SearchCenter) < (CellSize * CellSize * 0.25f))
             {
                 CenterValue = FinalValue;
                 bFoundCenter = true;
             }
        }

        const FVector Direction = (CellWorldPos - Context.SearchCenter).GetSafeNormal();
        GradientVector += Direction * FinalValue;
        TotalWeight += FMath::Abs(FinalValue);

        if (FinalValue > HighestValue)
        {
            HighestValue = FinalValue;
            HighestPos = CellWorldPos;
        }
        if (FinalValue < LowestValue)
        {
            LowestValue = FinalValue;
        }
        return false;
    });

    if (bFoundCenter && (HighestValue - CenterValue) <= 0.01f && (CenterValue - LowestValue) <= 0.01f)
    {
        return FVector::ZeroVector;
    }
    
    FVector FinalDirection;
    if (GradientVector.SizeSquared() < GRADIENT_FALLBACK_THRESHOLDSQ && TotalWeight > 0.0f)
    {
        FinalDirection = (HighestPos - Context.SearchCenter).GetSafeNormal();
    }
    else
    {
        FinalDirection = GradientVector.GetSafeNormal();
    }

    if (FMath::Abs(LookAheadDistance) > UE_KINDA_SMALL_NUMBER)
    {
        const FVector TargetPos = Context.SearchCenter + (FinalDirection * LookAheadDistance);
        
        if (Context.bExcludeUnreachableLocation && CachedWorld)
        {
            if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(CachedWorld))
            {
                FVector HitLocation;
                if (NavSys->NavigationRaycast(CachedWorld, Context.SearchCenter, TargetPos, HitLocation))
                {
                    return HitLocation;
                }
            }
        }
        return TargetPos;
    }
    return FinalDirection;
}

// ============================================================
// Candidate Maintenance
// ============================================================
void FTCATQueryProcessor::InsertTopKHighest(const FTCATSearchCandidate& Candidate, const int32 MaxCount,
    TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& InOut) const
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_TopKInsert);
    if (InOut.Num() < MaxCount)
    {
        InOut.Add(Candidate);
        return;
    }

    // Find current minimum in Top-K
    int32 MinIndex = 0;
    float MinValue = InOut[0].Value;

    for (int32 Index = 1; Index < InOut.Num(); ++Index)
    {
        if (InOut[Index].Value < MinValue)
        {
            MinValue = InOut[Index].Value;
            MinIndex = Index;
        }
    }

    if (Candidate.Value <= MinValue)
        return;
    
    InOut[MinIndex] = Candidate;
}

void FTCATQueryProcessor::InsertTopKLowest(const FTCATSearchCandidate& Candidate, const int32 MaxCount,
    TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& InOut) const
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_TopKInsert);
    if (InOut.Num() < MaxCount)
    {
        InOut.Add(Candidate);
        return;
    }

    // Find current maximum in Top-K (because we keep lowest values)
    int32 MaxIndex = 0;
    float MaxValue = InOut[0].Value;

    for (int32 Index = 1; Index < InOut.Num(); ++Index)
    {
        if (InOut[Index].Value > MaxValue)
        {
            MaxValue = InOut[Index].Value;
            MaxIndex = Index;
        }
    }

    if (Candidate.Value >= MaxValue)
        return;
    
    InOut[MaxIndex] = Candidate;
}

// ============================================================
// Reachability
// ============================================================
void FTCATQueryProcessor::FindTopReachableCandidates(const FTCATQueryContext& Context, 
    const TArray<FTCATSearchCandidate, TInlineAllocator<CANDIDATE_HARDCAP>>& Candidates, 
    FTCATQueryResultArray& OutResults) const
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_Reachability);
    OutResults.Reset();
    int32 FoundCount = 0;

    for (const auto& Candidate : Candidates)
    {
        if (Context.bExcludeUnreachableLocation && !IsPositionReachable(Context.SearchCenter, Candidate.WorldPos))
        {
            continue;
        }
        
        if (Context.bTraceVisibility && !HasLineOfSight(Context.SearchCenter, Candidate.WorldPos))
        {
            continue;
        }

        OutResults.Add({ Candidate.Value, Candidate.WorldPos });
        FoundCount++;
        if (FoundCount >= Context.MaxResults) break;
    }
}

bool FTCATQueryProcessor::IsPositionReachable(const FVector& From, const FVector& To) const
{
    if (!CachedWorld)
    {
        return true;
    }

    UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(CachedWorld);
    const ANavigationData* NavigationData = NavigationSystem ? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::ECreateIfEmpty::DontCreate) : nullptr;

    if (!NavigationSystem || !NavigationData)
    {
        return true;
    }

    FVector HitLocation = FVector::ZeroVector;

    // Fast check: nav raycast
    const bool bBlocked = NavigationSystem->NavigationRaycast(CachedWorld, From, To, HitLocation);
    if (!bBlocked)
    {
        return true;
    }

    // Slow check: pathfinding test
    FPathFindingQuery Query(nullptr, *NavigationData, From, To);
    Query.SetAllowPartialPaths(false);

    return NavigationSystem->TestPathSync(Query, EPathFindingMode::Regular);
}

bool FTCATQueryProcessor::HasLineOfSight(const FVector& From, const FVector& To) const
{
    if (FVector::DistSquared(From, To) < FMath::Square(50.0f)) return true;

    if (MapGroupedVolumes)
    {
        for (const auto& Pair : *MapGroupedVolumes)
        {
            for (ATCATInfluenceVolume* Volume : Pair.Value)
            {
                if (IsValid(Volume) && Volume->GetCachedBounds().IsInside(From))
                {
                    if (Volume->GetCachedBounds().IsInside(To))
                    {
                        return CheckGridLineOfSight(Volume, From, To);
                    }
                }
            }
        }
    }
    return false;
}

bool FTCATQueryProcessor::CheckGridLineOfSight(const ATCATInfluenceVolume* Volume, const FVector& Start, const FVector& End) const
{
    if (!Volume) return true;

    // Base Data Caching
    const float CellSize = Volume->GetCellSize();
    const float InvCellSize = 1.0f / CellSize;
    const FVector Origin = Volume->GetGridOrigin();
    const int32 GridW = Volume->GetColumns();
    const int32 GridH = Volume->GetRows();

    // World Coordi to Grid Coordi
    const float StartGridX = (Start.X - Origin.X)*InvCellSize;
    const float StartGridY = (Start.Y - Origin.Y)*InvCellSize;
    const float EndGridX = (End.X - Origin.X)*InvCellSize;
    const float EndGridY = (End.Y - Origin.Y)*InvCellSize;

    // Move Vector
    float DeltaX = EndGridX - StartGridX;
    float DeltaY = EndGridY - StartGridY;
    const float DistGrid = FMath::Sqrt(DeltaX*DeltaX + DeltaY*DeltaY);

    if (DistGrid < (float)GRID_TRACE_STRIDE) return true;

    const float StepScale = (float)GRID_TRACE_STRIDE / DistGrid;
    const float StepX = DeltaX * StepScale;
    const float StepY = DeltaY * StepScale;
    
    const float HeightBias = 10.0f; // Z-fighting


    const float StartZ = Start.Z + HeightBias;
    const float EndZ = End.Z + HeightBias;

    float CurGridX = StartGridX;
    float CurGridY = StartGridY;

    const int32 NumSteps = FMath::FloorToInt(DistGrid / (float)GRID_TRACE_STRIDE);
    
    for (int32 i = 0; i<=NumSteps; i++)
    {
        const int32 IX = FMath::FloorToInt(CurGridX);
        const int32 IY = FMath::FloorToInt(CurGridY);

        if (IX >= 0 && IX<GridW && IY >= 0 && IY<GridH)
        {
            const float Alpha = (float)i / (float)NumSteps;
            const float RayZ = FMath::Lerp(StartZ, EndZ, Alpha);

            const float TerrainZ = Volume->GetGridHeightIndex({IX, IY});
            if (TerrainZ > RayZ) return false;
        }
        CurGridX += StepX;
        CurGridY += StepY;
    }

    return true;
}

// ============================================================
// Helper
// ============================================================

void FTCATQueryProcessor::ForEachCellInCircle(const FTCATQueryContext& Context, 
    TFunctionRef<bool(float, const ATCATInfluenceVolume*, int32, int32)> ProcessCell) const
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_ForEachCell);
    int32 ProcessedCellCount = 0;

    if (Context.SearchRadius <= 0.0f || Context.MapTag.IsNone() || !MapGroupedVolumes)
    {
        UE_LOG(LogTCAT, Warning, TEXT("[FTCATQueryProcessor] ForEachCellInCircle: Radius must be greater than zero. Or MapTag is None"));
        return;
    }

    const TSet<ATCATInfluenceVolume*>* VolumeSet = MapGroupedVolumes->Find(Context.MapTag);
    if (!VolumeSet || VolumeSet->IsEmpty())
    {
        return;
    }
    
    for (ATCATInfluenceVolume* Volume : *VolumeSet)
    {
        if (!IsValid(Volume)) continue;

        const FBox VolumeBounds = Volume->GetCachedBounds();
        FBox SearchBox(Context.SearchCenter - FVector(Context.SearchRadius), Context.SearchCenter + FVector(Context.SearchRadius));
        if (!VolumeBounds.Intersect(SearchBox)) continue;
        
        const FTCATGridResource* LayerRes = Volume->GetLayerResource(Context.MapTag);
        if (!LayerRes || LayerRes->Grid.Num() == 0) continue;

        const float* __restrict GridDataPtr = LayerRes->Grid.GetData();
        const int32 GridWidth = Volume->GetColumns();
        const int32 GridHeight = Volume->GetRows();
        
        const float CellSize = Volume->GetCellSize();
        const float InvCellSize = 1.0f / CellSize;
        const FVector GridOrigin = Volume->GetGridOrigin();

        // Grid Space Conversion
        const float GridCenterX = (Context.SearchCenter.X - GridOrigin.X) * InvCellSize;
        const float GridCenterY = (Context.SearchCenter.Y - GridOrigin.Y) * InvCellSize;
        const float GridRadius = Context.SearchRadius * InvCellSize;
        const float GridRadiusSq = GridRadius * GridRadius;

        // Clamped Search Bounds
        const int32 MinY = FMath::Clamp(FMath::FloorToInt(GridCenterY - GridRadius), 0, GridHeight - 1);
        const int32 MaxY = FMath::Clamp(FMath::CeilToInt(GridCenterY + GridRadius), 0, GridHeight - 1);
        const int32 MaxXLimit = GridWidth - 1;

        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            const float DistY = (float)Y - GridCenterY;
            const float DistYSq = DistY * DistY;
            
            if (DistYSq > GridRadiusSq) continue;
            
            const float XSpan = FMath::Sqrt(GridRadiusSq - DistYSq);
            const int32 MinX = FMath::Max(0, FMath::FloorToInt(GridCenterX - XSpan));
            const int32 MaxX = FMath::Min(MaxXLimit, FMath::CeilToInt(GridCenterX + XSpan));

            const float* RowPtr = GridDataPtr + (Y * GridWidth);
            
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                ProcessedCellCount++; // Stat
                
                float RawValue = RowPtr[X];
                                
                if (ProcessCell(RawValue, Volume, X, Y))
                {
                    INC_FLOAT_STAT_BY(STAT_TCAT_AvgCells, (float)ProcessedCellCount);
                    return;
                }
            }
        }
    }
}

#if ENABLE_VISUAL_LOG
void FTCATQueryProcessor::VLogQueryDetails(const struct FTCATBatchQuery& Query) const
{
    if (!Query.DebugInfo.IsValid() || !CachedWorld)
    {
        return;
    }

    FTCATQueryContext Context(Query);
    VLogQueryDetails(Context, Query);
}

void FTCATQueryProcessor::VLogQueryDetails(const FTCATQueryContext& Context, const FTCATBatchQuery& Query) const
{
    if (!CachedWorld)
    {
        return;
    }

    const FTCATQueryDebugInfo& DebugInfo = Query.DebugInfo;
    const AActor* DebugOwner = DebugInfo.DebugOwner.Get();
    if (!DebugOwner)
    {
        return;
    }

    const bool bShouldVLog = FVisualLogger::IsRecording();
    const FName LogCat = TEXT("TCAT_QueryDebug");
    const FVector AdjustedCenter = Context.SearchCenter + FVector(0.0f, 0.0f, DebugInfo.HeightOffset);
    const FColor RangeColor = DebugInfo.BaseColor.ToFColor(true);
    const int32 ActiveStride = FMath::Max(1, Query.DebugInfo.SampleStride);
    const float TextZOffset = CVarTCATQueryTextOffset.GetValueOnGameThread();
    const float DebugDuration = FMath::Max(0.0f, CVarTCATQueryTextDuration.GetValueOnGameThread());
    const double BaseTime = CachedWorld ? CachedWorld->GetTimeSeconds() : FPlatformTime::Seconds();

    FQueryDebugFrame& DebugFrame = LastDebugFrame;
    DebugFrame.CellPositions.Reset();
    DebugFrame.CellColors.Reset();
    DebugFrame.CellTexts.Reset();
    DebugFrame.ResultPositions.Reset();
    DebugFrame.ResultTexts.Reset();
    DebugFrame.ExpireTime = BaseTime + DebugDuration;

    if (bShouldVLog)
    {
        UE_VLOG_CIRCLE(DebugOwner, LogCat, Log, AdjustedCenter, FVector::UpVector, Context.SearchRadius, RangeColor,
            TEXT("[%s] Radius=%.0f"), *Context.MapTag.ToString(), Context.SearchRadius);
    }
    
    ForEachCellInCircle(Context,
        [&, ActiveStride, bShouldVLog, TextZOffset](float RawValue, const ATCATInfluenceVolume* Volume, int32 GridX, int32 GridY) -> bool
        {
            if (!Volume)
            {
                return false;
            }
            
            if (ActiveStride > 1 && ((GridX % ActiveStride) != 0 || (GridY % ActiveStride) != 0))
            {
                return false;
            }
            
            const float CellSize = Volume->GetCellSize();
            FVector CellWorldPos = Volume->GetGridOrigin();
            CellWorldPos.X += (GridX * CellSize) + (CellSize * 0.5f);
            CellWorldPos.Y += (GridY * CellSize) + (CellSize * 0.5f);
            CellWorldPos.Z = Context.bIgnoreZValue ? Context.SearchCenter.Z : Volume->GetGridHeightIndex({GridX, GridY});

            const FVector VisualPos = CellWorldPos + FVector(0.0f, 0.0f, DebugInfo.HeightOffset);
            const float Dist = FVector::Dist(CellWorldPos, Context.SearchCenter);
            const float SelfDist = FVector::Dist(CellWorldPos, Context.SelfOrigin);

            float SelfInf = 0.0f;
            if ((Context.ContextFlags & ETCATContextFlags::HasSelfInfluence) && Context.InfluenceRadius > KINDA_SMALL_NUMBER)
            {
                const float NormalizedDist = SelfDist / Context.InfluenceRadius;
                SelfInf = EvaluateCurveValue(Context.Curve, Context.SelfCurveID, NormalizedDist) * Context.SelfRemovalFactor;
            }
            
            float DistBiasVal = 0.0f;
            if ((Context.ContextFlags & ETCATContextFlags::HasDistanceBias) && Context.SearchRadius > KINDA_SMALL_NUMBER)
            {
                const float NormalizedDist = Dist / Context.SearchRadius;
                const float DistanceScore = EvaluateCurveValue(Context.DistanceBiasCurve, Context.DistanceCurveID, NormalizedDist);
                const float Sign = (Context.ContextFlags & ETCATContextFlags::IsLowestQuery) ? -1.0f : 1.0f;
                DistBiasVal = (DistanceScore * Context.DistanceBiasWeight * Sign);
            }

            const float CurrentVal = RawValue - SelfInf + DistBiasVal;
            FString DebugText = FString::Printf(TEXT("Raw: %.2f"), RawValue);

            if (!FMath::IsNearlyZero(SelfInf))
            {
                DebugText += FString::Printf(TEXT("\n-Self: %.2f"), SelfInf);
            }
            if (!FMath::IsNearlyZero(DistBiasVal))
            {
                DebugText += FString::Printf(TEXT("\n+Dist: %.2f"), DistBiasVal);
            }

            DebugText += FString::Printf(TEXT("\nFinal: %.2f"), CurrentVal);
            
            FColor LogColor = FColor::White;
            if (CurrentVal <= 0.0f) LogColor = FColor::Red;
            else if (CurrentVal > 0.5f) LogColor = FColor::Green;
            else LogColor = FColor::Yellow;

            const FVector TextPos = VisualPos + FVector(0.0f, 0.0f, TextZOffset);
            DebugFrame.CellPositions.Add(TextPos);
            DebugFrame.CellColors.Add(LogColor);
            DebugFrame.CellTexts.Add(DebugText);

            if (bShouldVLog)
            {
                UE_VLOG_LOCATION(DebugOwner, LogCat, Log, TextPos, 0.0f, LogColor, TEXT("%s"), *DebugText);
            }

            return false;
        });

    for (const FTCATSingleResult& Result : Query.OutResults)
    {
        const FVector ResultPos = Result.WorldPos + FVector(0.0f, 0.0f, DebugInfo.HeightOffset);
        const FVector ResultTextPos = ResultPos + FVector(0.0f, 0.0f, TextZOffset);
        const FString ResultText = FString::Printf(TEXT("Result %.2f"), Result.Value);

        DebugFrame.ResultPositions.Add(ResultTextPos);
        DebugFrame.ResultTexts.Add(ResultText);

        if (bShouldVLog)
        {
            UE_VLOG_LOCATION(DebugOwner, LogCat, Warning, ResultPos, 25.0f, FColor::Cyan,
                TEXT("Result %.2f"), Result.Value);
        }
    }

    const_cast<FTCATQueryProcessor*>(this)->DrawPersistentDebug();
}

void FTCATQueryProcessor::DrawPersistentDebug()
{
    if (!CachedWorld)
    {
        return;
    }

    const double Now = CachedWorld->GetTimeSeconds();
    if (LastDebugFrame.ExpireTime <= Now)
    {
        return;
    }

    const float Duration = FMath::Max(0.0f, CVarTCATQueryTextDuration.GetValueOnGameThread());

    const int32 CellCount = LastDebugFrame.CellTexts.Num();
    for (int32 Index = 0; Index < CellCount; ++Index)
    {
        const FVector& Pos = LastDebugFrame.CellPositions[Index];
        const FString& Text = LastDebugFrame.CellTexts[Index];
        const FColor& Color = LastDebugFrame.CellColors[Index];
        DrawDebugString(CachedWorld, Pos, Text, nullptr, Color, Duration, false);
    }

    const int32 ResultCount = LastDebugFrame.ResultTexts.Num();
    for (int32 Index = 0; Index < ResultCount; ++Index)
    {
        DrawDebugString(CachedWorld, LastDebugFrame.ResultPositions[Index], LastDebugFrame.ResultTexts[Index], nullptr, FColor::Cyan, Duration, false);
    }
}
#endif

float FTCATQueryProcessor::CalculateModifiedValue(const FTCATQueryContext& Context, float RawValue, const FVector& CellWorldPos , int32 GridX, int32 GridY) const
{
    float FinalValue = RawValue;
    
    const float JitterScale = 0.0001f; 
    const float Noise = UTCATMathLibrary::GetSpatialHash(GridX, GridY, Context.RandomSeed);
    const float Sign = (Context.ContextFlags & ETCATContextFlags::IsLowestQuery) ? -1.0f : 1.0f;

    FinalValue += (Noise * JitterScale * Sign);    
       
    if (!(Context.ContextFlags & ETCATContextFlags::NeedDistance))
    {
        return FinalValue;
    }
    
    const float DistToCenter = FVector::Dist(CellWorldPos, Context.SearchCenter);

    if ((Context.ContextFlags & ETCATContextFlags::HasSelfInfluence) && Context.InfluenceRadius > KINDA_SMALL_NUMBER)
    {
        const float SelfDistance = FVector::Dist(CellWorldPos, Context.SelfOrigin);
        const float NormalizedSelfDistance = SelfDistance / Context.InfluenceRadius;
        const float CurveVal = EvaluateCurveValue(Context.Curve, Context.SelfCurveID, NormalizedSelfDistance);
        FinalValue -= (CurveVal * Context.SelfRemovalFactor);
    }

    if ((Context.ContextFlags & ETCATContextFlags::HasDistanceBias) && Context.SearchRadius > KINDA_SMALL_NUMBER)
    {
        const float NormalizedDistance = DistToCenter / Context.SearchRadius;
        const float DistanceScore = EvaluateCurveValue(Context.DistanceBiasCurve, Context.DistanceCurveID, NormalizedDistance);
        
        FinalValue += (DistanceScore * Context.DistanceBiasWeight * Sign);
    }
        
    return FinalValue;
}

float FTCATQueryProcessor::EvaluateCurveValue(const UCurveFloat* Curve, int32 CurveIndex, float NormalizedDistance) const
{
    const float Time = FMath::Clamp(NormalizedDistance, 0.0f, 1.0f);

    if (CurveIndex != INDEX_NONE && CurveAtlasData && CurveAtlasWidth > 1 && CurveAtlasRowCount > CurveIndex)
    {
        return SampleCurveAtlas(CurveIndex, Time);
    }

    if (Curve)
    {
        return Curve->GetFloatValue(Time);
    }

    return 0.0f;
}

float FTCATQueryProcessor::SampleCurveAtlas(int32 CurveIndex, float NormalizedDistance) const
{
    if (!CurveAtlasData || CurveAtlasWidth <= 1 || CurveAtlasRowCount <= 0)
    {
        return 0.0f;
    }

    if (CurveIndex < 0 || CurveIndex >= CurveAtlasRowCount)
    {
        return 0.0f;
    }

    const float Time = FMath::Clamp(NormalizedDistance, 0.0f, 1.0f);
    const float VirtualColumn = Time * (CurveAtlasWidth - 1);
    const int32 IndexLeft = FMath::Clamp(FMath::FloorToInt(VirtualColumn), 0, CurveAtlasWidth - 1);
    const int32 IndexRight = FMath::Min(IndexLeft + 1, CurveAtlasWidth - 1);
    const float Alpha = VirtualColumn - IndexLeft;

    const int32 RowOffset = CurveIndex * CurveAtlasWidth;
    const TArray<float>& Data = *CurveAtlasData;
    const int32 LeftIndex = RowOffset + IndexLeft;
    const int32 RightIndex = RowOffset + IndexRight;

    if (!Data.IsValidIndex(LeftIndex) || !Data.IsValidIndex(RightIndex))
    {
        return 0.0f;
    }

    return FMath::Lerp(Data[LeftIndex], Data[RightIndex], Alpha);
}

void FTCATQueryProcessor::CalculatePotentialDelta(const UCurveFloat& Curve, float Factor, float& OutMaxAdd, float& OutMaxSub)
{
    OutMaxAdd = 0.0f;
    OutMaxSub = 0.0f;

    if (FMath::IsNearlyZero(Factor))
    {
        return;
    }
    
    float MinCurveVal = 0.f, MaxCurveVal = 0.f;
    Curve.GetValueRange(MinCurveVal, MaxCurveVal);
    
    float Delta1 = -(MinCurveVal * Factor);
    float Delta2 = -(MaxCurveVal * Factor);

    OutMaxAdd = FMath::Max(0.0f, FMath::Max(Delta1, Delta2));
    OutMaxSub = FMath::Max(0.0f, FMath::Max(-Delta1, -Delta2));
}
