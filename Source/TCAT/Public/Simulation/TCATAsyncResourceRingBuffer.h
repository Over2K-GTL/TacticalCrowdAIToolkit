// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TCATTypes.h"
#include "TCATAsyncResourceRingBuffer.generated.h"

class UTextureRenderTarget2D;
class FRHIGPUTextureReadback;

/**
 * Single asynchronous resource (RenderTarget + Readback pair)
 * Used by FTCATAsyncResourceRingBuffer for GPU pipeline parallelization
 */
USTRUCT()
struct FTCATAsyncResource
{
    GENERATED_BODY()

    /** GPU render target */
    UPROPERTY(Transient, VisibleInstanceOnly, Category = "TCAT")
    UTextureRenderTarget2D* RenderTarget = nullptr;

    /** GPU Readback object */
    class FRHIGPUTextureReadback* Readback = nullptr;

    /** Frame time when write was requested to this resource */
    double WriteTime = MAX_dbl;

    /** Frame time when this resource was read */
    double ReadTime = MAX_dbl;

	/** Dispatched influence sources at the time of write */
    TArray<FTCATInfluenceSourceWithOwner> DispatchedSourcesWithOwners;

    bool IsEmpty() const
    {
        return WriteTime == MAX_dbl && ReadTime == MAX_dbl;
	}

    bool WasMostRecentActionWrite() const
    {
        return WriteTime != MAX_dbl && ReadTime == MAX_dbl;
	}

    /** Reset resource state */
    void Reset()
    {
        WriteTime = MAX_dbl;
        ReadTime = MAX_dbl;
		DispatchedSourcesWithOwners.Reset();
    }
};

struct FTCATAsyncDebugResource
{
	uint64 WriteFrameNumber = MAX_uint64;
    uint64 ReadFrameNumber = MAX_uint64;
	float PredictionTimeForDebug = 0.0f;
};

/**
 * Asynchronous resource ring buffer
 * Manages multiple RenderTarget/Readback pairs for GPU pipeline parallelization
 */
USTRUCT()
struct FTCATAsyncResourceRingBuffer
{
    GENERATED_BODY()
public:
    static constexpr int32 BufferSize = 5;

    FTCATAsyncResourceRingBuffer();
    ~FTCATAsyncResourceRingBuffer();

    /**
     * Get the most recent frame latency (in time)
     */
    float GetLatestWriteReadLatency() const;

	float GetLatestReadResourcePredictionTime() const;

	uint32 GetLatestWriteReadLatencyFrames() const;

    /**
     * Check if current read position's Readback is ready
     */
    bool IsCurrentReadBackReady() const;

    /**
     * Get current read resource (read-only, does not advance index)
     */
    const FTCATAsyncResource& GetCurrentReadResource() const;

    /**
     * Get current write resource (read-only, does not advance index)
     */
    const FTCATAsyncResource& GetCurrentWriteResource() const;

    /**
     * Get write resource and advance index
     * @param OutCurrentWriteResource Output write resource
     * @param PredictionTimeForDebug Time used in component location prediction. It is set in AsyncDebugResources
	 * @param DispatchedSourcesWithOwners Dispatched influence sources at the time of write
     * @return Success (false if buffer is full)
     */
    bool AdvanceWriteResource(FTCATAsyncResource& OutCurrentWriteResource, const float PredictionTimeForDebug
        , const TArray<FTCATInfluenceSourceWithOwner>* DispatchedSourcesWithOwners = nullptr);

    /**
     * Get read resource and advance index
     * @param OutCurrentReadResource Output read resource
     * @param bLogWriteReadLatency Log
     * @return Success (false if buffer is empty or Readback not ready)
     */
    bool AdvanceReadResource(FTCATAsyncResource& OutCurrentReadResource, bool bLogWriteReadLatency = false);

    /**
     * Initialize ring buffer
     * @param Outer Owner UObject
     * @param Width Texture width
     * @param Height Texture height
     * @param ResourceDebugName Debugging resource name
     */
    void Initialize(UObject* Outer, int32 Width, int32 Height, FName ResourceDebugName = NAME_None);

    /**
     * Release all resources
     */
    void Release();

    /**
     * Peek at the most recently written resource without advancing indices
     * Used by composite volumes to read from the last frame's write target
     * @return Reference to the most recently written async resource
     */
    const FTCATAsyncResource& PeekLastWriteResource() const
    {
        const int32 LastWriteIndex = (WriteIndex + BufferSize - 1) % BufferSize;
        return AsyncResources[LastWriteIndex];
    }

private:
    bool IsReadbackReady(const int32 Index) const;

    bool IsReadable(const int32 Index) const;

	int32 CalculatePhysicalIndex(const int32 LogicalIndex) const;

private:
    UPROPERTY(VisibleInstanceOnly, Category = "TCAT")
    /** Asynchronous resource array (fixed size) */
    FTCATAsyncResource AsyncResources[BufferSize];

	UObject* OwnerObject;

    /** Current read index */
    int32 ReadIndex;

    /** Current write index */
    int32 WriteIndex;

    /** Most recent write-read latency (in seconds) */
    float LatestWriteReadLatencyTime;

	/** Prediction time of Most recently read resources (in seconds) */
	float LatestReadResourcePredictionTime;

	uint32 LatestWriteReadLatencyFrames;

    /** Number of valid resources in buffer */
    int32 ValidCount;
    
    /** Debug resources for AsyncResources */
    FTCATAsyncDebugResource AsyncDebugResources[BufferSize];

    FName DebugName;
};
