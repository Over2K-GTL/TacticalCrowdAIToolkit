// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TCATTypes.h"
#include "TCATAsyncResourceRingBuffer.generated.h"

class UTextureRenderTarget2D;
class FRHIGPUTextureReadback;

/**
 * Single asynchronous resource (RenderTarget + Readback pair)
 *  - one RenderTarget for GPU writes
 *  - one GPU readback object to transfer texture data back to CPU later
 *
 * This struct is intended to be used inside FTCATAsyncResourceRingBuffer.
 *
 * Lifetime / ownership notes:
 *  - RenderTarget is a transient UObject owned by an Outer (typically a volume/subsystem).
 *  - Readback is an RHI object; it must be released correctly to avoid GPU memory leaks.
 *  - Timestamps are used for latency/debug metrics and buffer state tracking.
 */
USTRUCT()
struct FTCATAsyncResource
{
    GENERATED_BODY()

    /** GPU render target */
    UPROPERTY(Transient, VisibleInstanceOnly, Category = "TCAT")
    UTextureRenderTarget2D* RenderTarget = nullptr;

	/**
	 * GPU readback handle used to retrieve RenderTarget contents asynchronously.
	 * This is not a UObject and is managed manually.
	 */
    class FRHIGPUTextureReadback* Readback = nullptr;

	/**
	 * Timestamp (seconds) when a write was requested for this slot.
	 * Used to measure GPU pipeline latency and to detect slot usage state.
	 */
    double WriteTime = MAX_dbl;

	/**
	 * Timestamp (seconds) when this slot was read back / consumed on CPU.
	 * Used to measure GPU pipeline latency and to detect slot usage state.
	 */
    double ReadTime = MAX_dbl;
	
	/**
	 * Snapshot of dispatched influence sources at the moment of write.
	 * Useful for debugging mismatches between "what was rendered" and "what was read back".
	 */
    TArray<FTCATInfluenceSourceWithOwner> DispatchedSourcesWithOwners;

	/** @return True if this slot has never been written nor read since last reset. */
    bool IsEmpty() const
    {
        return WriteTime == MAX_dbl && ReadTime == MAX_dbl;
	}

	/**
	 * @return True if a write has occurred but the slot has not been read yet.
	 * This indicates the slot is "in-flight" (awaiting readback/consumption).
	 */
    bool WasMostRecentActionWrite() const
    {
        return WriteTime != MAX_dbl && ReadTime == MAX_dbl;
	}

	/**
	 * Reset slot state (timestamps and debug snapshots).
	 * Does not automatically destroy RenderTarget/Readback; ownership is handled by the ring buffer.
	 */
    void Reset()
    {
        WriteTime = MAX_dbl;
        ReadTime = MAX_dbl;
		DispatchedSourcesWithOwners.Reset();
    }
};

/**
 * Lightweight debug info associated with each async slot.
 * Stored separately from FTCATAsyncResource so it can be updated without touching GPU objects.
 */
struct FTCATAsyncDebugResource
{
	uint64 WriteFrameNumber = MAX_uint64;
    uint64 ReadFrameNumber = MAX_uint64;
	float PredictionTimeForDebug = 0.0f;
};

/**
 * Fixed-size ring buffer that pipelines multiple (RenderTarget + Readback) pairs.
 *
 * Goal:
 *  - Avoid GPU/CPU sync stalls by allowing GPU writes (frame N) and CPU reads (frame N+k)
 *    to be decoupled across multiple buffered slots.
 *
 * Important behavior:
 *  - "Write" advances the write index and marks a slot as in-flight.
 *  - "Read" advances the read index only if the corresponding Readback is ready.
 *  - If the buffer is full, additional writes fail (caller should skip or throttle).
 *  - If the buffer is empty or the readback is not ready, reads fail.
 *
 * This struct is visible in Details (VisibleInstanceOnly) mainly for debugging.
 */
USTRUCT()
struct FTCATAsyncResourceRingBuffer
{
    GENERATED_BODY()
public:
	/** Fixed number of slots in the ring buffer. Increasing this increases memory usage but can reduce stalls. */
    static constexpr int32 BufferSize = 5;

    FTCATAsyncResourceRingBuffer();
    ~FTCATAsyncResourceRingBuffer();

	/**
	 * Get the most recent write->read latency in seconds, based on the last successful read.
	 * Useful for profiling GPU pipeline delay and tuning buffer size.
	 */
    float GetLatestWriteReadLatency() const;

	/**
	 * Get the prediction time (seconds) stored for the most recently read resource.
	 * This is used only for debugging temporal prediction behavior.
	 */
	float GetLatestReadResourcePredictionTime() const;

	/**
	 * Get the most recent write->read latency expressed in frames (approximate).
	 * This is derived from recorded write/read frame numbers.
	 */
	uint32 GetLatestWriteReadLatencyFrames() const;

	/**
	 * @return True if the current logical read slot has a readback object that reports "ready".
	 * Readiness typically means the GPU has finished the copy and the data can be locked/copied on CPU.
	 */
    bool IsCurrentReadBackReady() const;

	/**
	 * Get the current logical read slot without advancing indices.
	 * This does NOT guarantee that the readback is ready; call IsCurrentReadBackReady() first if needed.
	 */
    const FTCATAsyncResource& GetCurrentReadResource() const;

	/**
	  * Get the current logical write slot without advancing indices.
	  * This does NOT reserve the slot; to actually write, call AdvanceWriteResource().
	  */
    const FTCATAsyncResource& GetCurrentWriteResource() const;

    /**
     * Reserve the next write slot and advance the write index.
     * @param OutCurrentWriteResource Output write resource
     * @param PredictionTimeForDebug Time used in component location prediction. It is set in AsyncDebugResources
	 * @param DispatchedSourcesWithOwners Dispatched influence sources at the time of write
     * @return Success (false if buffer is full)
     */
    bool AdvanceWriteResource(FTCATAsyncResource& OutCurrentWriteResource, const float PredictionTimeForDebug
        , const TArray<FTCATInfluenceSourceWithOwner>* DispatchedSourcesWithOwners = nullptr);

	/**
	 * Consume the next readable slot and advance the read index.
     * @param OutCurrentReadResource Output read resource
     * @param bLogWriteReadLatency Log
     * @return Success (false if buffer is empty or Readback not ready)
     */
    bool AdvanceReadResource(FTCATAsyncResource& OutCurrentReadResource, bool bLogWriteReadLatency = false);

    /**
	 * Initialize all slots and allocate RenderTargets/Readbacks.
     * @param Outer Owner UObject
     * @param Width Texture width
     * @param Height Texture height
     * @param ResourceDebugName Debugging resource name
	 *
	 * Must be called before using AdvanceWriteResource/AdvanceReadResource.
     */
    void Initialize(UObject* Outer, int32 Width, int32 Height, FName ResourceDebugName = NAME_None);

	/**
	 * Release all allocated resources and reset indices/state.
	 * Safe to call multiple times.
	 */
    void Release();

	/**
	 * Peek the most recently written slot without advancing indices.
	 *
	 * This is commonly used when a consumer wants to sample from the last write target
	 * (e.g., composite volumes reading "previous frame" output) without affecting the pipeline state.
	 *
	 * Note:
	 *  - This does not guarantee the slot is readable on CPU (readback may not be ready).
	 */
    const FTCATAsyncResource& PeekLastWriteResource() const
    {
        const int32 LastWriteIndex = (WriteIndex + BufferSize - 1) % BufferSize;
        return AsyncResources[LastWriteIndex];
    }

private:
	/** Check readiness of the readback at a physical index. */
    bool IsReadbackReady(const int32 Index) const;

	/** Check whether the slot at a physical index is logically readable (valid + ready). */
	bool IsReadable(const int32 Index) const;

	/**
	 * Convert a logical index (ReadIndex/WriteIndex in "logical space") to a physical array index.
	 * This exists to keep ring operations robust and debuggable.
	 */
	int32 CalculatePhysicalIndex(const int32 LogicalIndex) const;

private:
	/**
	 * Fixed-size async resource slots.
	 * Visible in Details panel for runtime inspection only (instance-only).
	 */
    UPROPERTY(VisibleInstanceOnly, Category = "TCAT")
    FTCATAsyncResource AsyncResources[BufferSize];

	/** Owner UObject used to create transient render targets. */
	UObject* OwnerObject = nullptr;

    /** Current read index */
	int32 ReadIndex = 0;

    /** Current write index */
	int32 WriteIndex = 0;

    /** Most recent write-read latency (in seconds) */
	float LatestWriteReadLatencyTime = 0.0f;

	/** Prediction time (seconds) associated with the most recently read resource (debug-only). */
	float LatestReadResourcePredictionTime = 0.0f;

	/** Most recent successful write->read latency in frames (approximate). */
	uint32 LatestWriteReadLatencyFrames;

	/** Number of valid in-flight slots currently stored in the ring buffer. */
    int32 ValidCount;
    
    /** Debug resources for AsyncResources */
    FTCATAsyncDebugResource AsyncDebugResources[BufferSize];

	/** Optional debug label for resource instances. */
    FName DebugName;
};