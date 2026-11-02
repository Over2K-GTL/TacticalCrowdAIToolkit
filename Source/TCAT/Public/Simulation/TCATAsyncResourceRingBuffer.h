// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TCATTypes.h"
#include <atomic>
#include "TCATAsyncResourceRingBuffer.generated.h"

class UTextureRenderTarget2D;
class FRHIGPUTextureReadback;

/**
 * CPU-side copy of one slot's GPU readback, shared between the game thread and the render thread.
 *
 * Threading contract:
 *  - The render thread is the only writer of Data/Width/Height/MinValue/MaxValue/bHasData.
 *    It publishes them by storing the copied write generation into CopiedGeneration (release).
 *  - The game thread only reads those fields after observing its own write generation in
 *    CopiedGeneration (acquire), and never writes them.
 *  - At most one copy request per slot is in the render command queue (guarded by bCopyQueued).
 */
struct FTCATReadbackCPUData
{
	/** Write generation whose texture contents are fully copied into Data. 0 = nothing copied yet. */
	std::atomic<uint32> CopiedGeneration{ 0 };

	/** True while a copy request for this slot is waiting in the render command queue. */
	std::atomic<bool> bCopyQueued{ false };

	/** Tightly packed row-major copy of the readback (Width * Height). */
	TArray<float> Data;
	int32 Width = 0;
	int32 Height = 0;
	float MinValue = 0.0f;
	float MaxValue = 0.0f;

	/** False if the readback had no staging data to map (e.g. the copy was never enqueued). */
	bool bHasData = false;

	/**
	 * Render thread only. If the readback is ready, Lock/copy/Unlock it into Data and publish Generation.
	 * Does nothing when the GPU has not finished yet, so the game thread can request again next frame.
	 */
	void CopyFromReadback_RenderThread(FRHIGPUTextureReadback& Readback, uint32 Generation, int32 InWidth, int32 InHeight);
};

/**
 * Single asynchronous resource (RenderTarget + Readback pair)
 *  - one RenderTarget for GPU writes
 *  - one GPU readback object to transfer texture data back to CPU later
 *  - one CPU buffer the render thread copies the readback into
 *
 * This struct is intended to be used inside FTCATAsyncResourceRingBuffer.
 *
 * Lifetime / ownership notes:
 *  - RenderTarget is a transient UObject owned by an Outer (typically a volume/subsystem).
 *  - Readback is an RHI object; it must be released correctly to avoid GPU memory leaks.
 *    It is deleted on the render thread so in-flight copy requests finish first.
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
	 * This is not a UObject and is managed manually. Lock/Unlock it on the render thread only.
	 */
    class FRHIGPUTextureReadback* Readback = nullptr;

	/** CPU copy of the readback, filled on the render thread. Persistent per slot (survives Reset()). */
	TSharedPtr<FTCATReadbackCPUData, ESPMode::ThreadSafe> CPUData;

	/**
	 * Incremented on every AdvanceWriteResource(). The slot is readable once CPUData->CopiedGeneration matches it.
	 * Persistent per slot (survives Reset()). 0 is never used as a valid generation.
	 */
	uint32 WriteGeneration = 0;

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
 * Logical state of a ring buffer slot, as seen by the game thread.
 * Derived from the slot's write/read bookkeeping and its write/copy generations (see GetSlotState()).
 */
enum class ETCATSlotState : uint8
{
	/** No unread write (initial state, or consumed and reset). */
	Empty,

	/**
	 * Write reserved; the result is not in the CPU buffer yet.
	 * Covers GPU compute, the readback copy, and the render-thread Lock/copy.
	 */
	Writing,

	/** The CPU buffer holds this write's result; the game thread may read it. */
	Ready,
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
 *  - "Write" advances the write index and moves a slot from Empty to Writing (see ETCATSlotState).
 *  - "Read" advances the read index only if the slot's readback has been copied to its CPU buffer
 *    on the render thread (see RequestReadbackCopies()). The game thread never locks a readback.
 *  - If the buffer is full, additional writes fail (caller should skip or throttle).
 *  - If the buffer is empty or the readback copy is not complete, reads fail.
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
	 * @return True if the current logical read slot has been copied to its CPU buffer on the render thread,
	 * i.e. the GPU finished and the data can be consumed on the game thread.
	 */
    bool IsCurrentReadResourceReadable() const;

	/**
	 * Get the current logical read slot without advancing indices.
	 * This does NOT guarantee that the readback is ready; call IsCurrentReadResourceReadable() first if needed.
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
	 * Game thread. Enqueue one render command that copies every in-flight slot whose GPU readback is ready
	 * into that slot's CPU buffer. Slots that are not ready yet are re-requested on a later call.
	 * Call this every frame before AdvanceReadResource().
	 */
	void RequestReadbackCopies();

	/**
	 * Consume the next readable slot and advance the read index.
	 * OutCurrentReadResource.CPUData holds the copied data; consume it before the next RequestReadbackCopies().
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
	/** Get the logical state of the slot at a physical index. */
    ETCATSlotState GetSlotState(const int32 Index) const;

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

	/** Texture size the slots were initialized with. */
	int32 TextureWidth = 0;
	int32 TextureHeight = 0;

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