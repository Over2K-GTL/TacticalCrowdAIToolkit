// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Simulation/TCATAsyncResourceRingBuffer.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "TCAT.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "VisualLogger/VisualLogger.h"

void FTCATReadbackCPUData::CopyFromReadback_RenderThread(FRHIGPUTextureReadback& Readback, uint32 Generation, int32 InWidth, int32 InHeight)
{
	check(IsInRenderingThread());

	// GPU not finished yet. Leave CopiedGeneration untouched so the game thread requests again next frame.
	if (!Readback.IsReady())
	{
		return;
	}

	Width = InWidth;
	Height = InHeight;
	bHasData = false;

	int32 RowPitchInPixels = 0;
	if (const uint8* MappedData = static_cast<const uint8*>(Readback.Lock(RowPitchInPixels)))
	{
		if (InWidth > 0 && InHeight > 0 && RowPitchInPixels >= InWidth)
		{
			Data.SetNumUninitialized(InWidth * InHeight, EAllowShrinking::No);

			const int32 StrideInBytes = RowPitchInPixels * sizeof(float);
			const int32 RowBytes = InWidth * sizeof(float);

			float CurrentMin = FLT_MAX;
			float CurrentMax = -FLT_MAX;

			float* DestBase = Data.GetData();

			// Copy row by row to handle potential padding (Stride) from GPU memory
			for (int32 Y = 0; Y < InHeight; ++Y)
			{
				float* DestRow = DestBase + (Y * InWidth);
				FMemory::Memcpy(DestRow, MappedData + (Y * StrideInBytes), RowBytes);

				for (int32 X = 0; X < InWidth; ++X)
				{
					const float Val = DestRow[X];
					if (Val < CurrentMin) CurrentMin = Val;
					if (Val > CurrentMax) CurrentMax = Val;
				}
			}

			MinValue = CurrentMin;
			MaxValue = CurrentMax;
			bHasData = true;
		}

		Readback.Unlock();
	}

	// Publish: everything written above becomes visible to the game thread once it observes this generation.
	CopiedGeneration.store(Generation, std::memory_order_release);
}

FTCATAsyncResourceRingBuffer::FTCATAsyncResourceRingBuffer()
    : ReadIndex(0)
    , WriteIndex(0)
    , LatestWriteReadLatencyTime(0.0f) // Default 0 seconds
	, LatestReadResourcePredictionTime(0.01f)
    , ValidCount(0)
{
}

FTCATAsyncResourceRingBuffer::~FTCATAsyncResourceRingBuffer()
{
    //Release();
}

float FTCATAsyncResourceRingBuffer::GetLatestWriteReadLatency() const
{
    return LatestWriteReadLatencyTime;
}

float FTCATAsyncResourceRingBuffer::GetLatestReadResourcePredictionTime() const
{
    return LatestReadResourcePredictionTime;
}

uint32 FTCATAsyncResourceRingBuffer::GetLatestWriteReadLatencyFrames() const
{
    return LatestWriteReadLatencyFrames;
}

ETCATSlotState FTCATAsyncResourceRingBuffer::GetSlotState(const int32 Index) const
{
    const FTCATAsyncResource& AsyncResource = AsyncResources[Index];
    if (AsyncResource.WasMostRecentActionWrite())
    {
        // Writing -> Ready is decided by the render thread, which publishes the copied generation
        const bool bCopied = AsyncResource.CPUData.IsValid()
            && AsyncResource.CPUData->CopiedGeneration.load(std::memory_order_acquire) == AsyncResource.WriteGeneration;

        return bCopied ? ETCATSlotState::Ready : ETCATSlotState::Writing;
    }
    return ETCATSlotState::Empty;
}

bool FTCATAsyncResourceRingBuffer::IsCurrentReadResourceReadable() const
{
	return IsReadable(ReadIndex);
}

bool FTCATAsyncResourceRingBuffer::IsReadable(const int32 Index) const
{
    // Check Validity
    if (!AsyncResources[Index].RenderTarget)
    {
        UE_LOG(LogTCAT, Error,
            TEXT("[%s] Read resource(Index: %d) has null RenderTarget!"), *DebugName.ToString(), Index);
        return false;
    }

    if (!AsyncResources[Index].Readback)
    {
        UE_LOG(LogTCAT, Error,
            TEXT("[%s] Read resource(Index: %d) has null Readback!"), *DebugName.ToString(), Index);
        return false;
    }

    // check Readability
    if (ValidCount == 0)
    {
        UE_LOG(LogTCAT, VeryVerbose, TEXT("[%s] Buffer is empty, nothing to read."), *DebugName.ToString());
        return false;
    }

    switch (GetSlotState(Index))
    {
    case ETCATSlotState::Empty:
        UE_LOG(LogTCAT, VeryVerbose,
            TEXT("[%s] Read Resource(index: %d) is not resource that most recently called AdvanceWriteResource."), *DebugName.ToString(), Index);
        return false;

    case ETCATSlotState::Writing:
        UE_LOG(LogTCAT, VeryVerbose, TEXT("[%s] Read Resource(index: %d)'s Readback not copied to CPU yet."), *DebugName.ToString(), Index);
        return false;

    case ETCATSlotState::Ready:
        return true;

    default:
		checkf(false, TEXT("[%s] Invalid slot state for index %d"), *DebugName.ToString(), Index);
		return false;
    }
}

int32 FTCATAsyncResourceRingBuffer::CalculatePhysicalIndex(const int32 LogicalIndex) const
{
    return LogicalIndex % BufferSize;
}

const FTCATAsyncResource& FTCATAsyncResourceRingBuffer::GetCurrentReadResource() const
{
    return AsyncResources[ReadIndex];
}

const FTCATAsyncResource& FTCATAsyncResourceRingBuffer::GetCurrentWriteResource() const
{
    return AsyncResources[WriteIndex];
}

bool FTCATAsyncResourceRingBuffer::AdvanceWriteResource(
    FTCATAsyncResource& OutCurrentWriteResource
    , const float PredictionTimeForDebug
    , const TArray<FTCATInfluenceSourceWithOwner>* DispatchedSourcesWithOwners)
{
    // Check if buffer is full
    if (ValidCount >= BufferSize)
    {
        UE_LOG(LogTCAT, Warning,
            TEXT("[%s] Buffer is full (%d/%d)! GPU is too slow or buffer size is too small."),
            *DebugName.ToString(), ValidCount, BufferSize);
        return false;
    }

    checkf(AsyncResources[WriteIndex].IsEmpty(), TEXT("Logic Error: ValidCount says OK, but slot is dirty!"));

    if (!AsyncResources[WriteIndex].RenderTarget)
    {
        UE_LOG(LogTCAT, Error,
            TEXT("[%s] Write resource(Index: %d) has null RenderTarget!"), *DebugName.ToString(), WriteIndex);
        return false;
    }

    if(!AsyncResources[WriteIndex].Readback)
    {
        UE_LOG(LogTCAT, Error,
            TEXT("[%s] Write resource(Index: %d) has null Readback!"), *DebugName.ToString(), WriteIndex);
        return false;
    }

    // Set current write resource
    FTCATAsyncResource& WriteResource = AsyncResources[WriteIndex];
    WriteResource.WriteTime = OwnerObject->GetWorld()->GetTimeSeconds();
    WriteResource.ReadTime = MAX_dbl; // Not read yet
	// New generation: copies of the slot's previous contents can no longer make this slot Ready
	if (++WriteResource.WriteGeneration == 0)
	{
		WriteResource.WriteGeneration = 1;
	}
	if (DispatchedSourcesWithOwners)
	{
		WriteResource.DispatchedSourcesWithOwners = *DispatchedSourcesWithOwners;
	}

    OutCurrentWriteResource = WriteResource;

    // Record debug information: frame number when write was requested
    FTCATAsyncDebugResource& DebugResource = AsyncDebugResources[WriteIndex];
    DebugResource.WriteFrameNumber = GFrameCounter;
    DebugResource.ReadFrameNumber = MAX_uint64; // Not read yet
    DebugResource.PredictionTimeForDebug = PredictionTimeForDebug;

    // Advance index
    WriteIndex = CalculatePhysicalIndex(WriteIndex + 1);
    ++ValidCount;

    UE_LOG(LogTCAT, VeryVerbose,
        TEXT("[%s] Advanced write resource (Time=%lf, ValidCount=%d/%d, NextWrite=%d, PredictionTime=%.5f)"),
        *DebugName.ToString(), WriteResource.WriteTime, ValidCount, BufferSize, WriteIndex, DebugResource.PredictionTimeForDebug);

    return true;
}

void FTCATAsyncResourceRingBuffer::RequestReadbackCopies()
{
    struct FCopyRequest
    {
        TSharedRef<FTCATReadbackCPUData, ESPMode::ThreadSafe> CPUData;
        FRHIGPUTextureReadback* Readback;
        uint32 Generation;
    };

    TArray<FCopyRequest, TInlineAllocator<BufferSize>> Requests;
    for (int32 i = 0; i < BufferSize; ++i)
    {
        FTCATAsyncResource& Resource = AsyncResources[i];
        if (GetSlotState(i) != ETCATSlotState::Writing || !Resource.Readback || !Resource.CPUData.IsValid())
        {
            continue;
        }

        // Keep at most one request per slot in the render command queue
        if (Resource.CPUData->bCopyQueued.exchange(true, std::memory_order_acq_rel))
        {
            continue;
        }

        // Slots written this frame are not requested until the next call, so the dispatch that
        // enqueues their GPU copy is always ahead of this command in the render command queue.
        Requests.Add({ Resource.CPUData.ToSharedRef(), Resource.Readback, Resource.WriteGeneration });
    }

    if (Requests.Num() == 0)
    {
        return;
    }

    // Readback::Lock/Unlock use the immediate command list, which is only legal on the render thread.
    ENQUEUE_RENDER_COMMAND(TCAT_ReadbackCopy)(
        [Requests = MoveTemp(Requests), CopyWidth = TextureWidth, CopyHeight = TextureHeight](FRHICommandListImmediate& RHICmdList)
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_RT_ReadbackCopy);
            for (const FCopyRequest& Request : Requests)
            {
                Request.CPUData->CopyFromReadback_RenderThread(*Request.Readback, Request.Generation, CopyWidth, CopyHeight);
                Request.CPUData->bCopyQueued.store(false, std::memory_order_release);
            }
        });
}

bool FTCATAsyncResourceRingBuffer::AdvanceReadResource(FTCATAsyncResource& OutCurrentReadResource, bool bLogWriteReadLatency)
{
    // Logic to maintain the latest ReadIndex whenever possible
    while (true)
    {
        const int32 NextReadIndex = CalculatePhysicalIndex(ReadIndex + 1);
        const int32 NextNextReadIndex = CalculatePhysicalIndex(ReadIndex + 2);

        const bool bCanReadCurrent = IsReadable(ReadIndex);
        const bool bCanReadNext = IsReadable(NextReadIndex);
        const bool bCanReadNextNext = IsReadable(NextNextReadIndex);

        if (!bCanReadCurrent)
        {
            if (bLogWriteReadLatency)
            {
                UE_LOG(LogTCAT, Verbose,
                    TEXT("[%s] Cannot read any resource. Will trying next Frame. CurReadIndex: %d"), *DebugName.ToString(), ReadIndex);
            }
            return false;
        }
        else if (bCanReadCurrent && !bCanReadNext)
        {
            if (bLogWriteReadLatency)
            {
                UE_LOG(LogTCAT, Verbose,
                    TEXT("[%s] Read Success. Next Resource is not ready yet. CurReadIndex: %d"), *DebugName.ToString(), ReadIndex);
            }
            break;
        }
        else if (bCanReadCurrent && bCanReadNext && !bCanReadNextNext)
        {
            /*
            When two textures are ready to be read, the reason the texture with the longer request time is read is because the following situation occurs if the latest texture is continuously read:
            - GPU texture processing time fluctuates slightly
                -> WriteRead Frame latency continuously fluctuates slightly (e.g., 2 frames, 3 frames, 2 frames...)
                -> The following problems occur:
                    Predictions frequently fail
                    Frames with no readable textures occur frequently.
            */
            if (bLogWriteReadLatency)
            {
                UE_LOG(LogTCAT, Verbose,
                    TEXT("[%s] Read Success. Next Resource is ready. Next Next Resource is not ready yet. CurReadIndex: %d"), *DebugName.ToString(), ReadIndex);
            }
            break;
        }
        else // The next next resource is also readable.
        {
            if (bLogWriteReadLatency)
            {
                UE_LOG(LogTCAT, Verbose,
                    TEXT("[%s] Three or more resources is ready. I will advance ReadResource"), *DebugName.ToString());
            }

            // Reset resource for reuse
            AsyncResources[ReadIndex].Reset();
            AsyncDebugResources[ReadIndex].WriteFrameNumber = MAX_uint64;
            AsyncDebugResources[ReadIndex].ReadFrameNumber = MAX_uint64;

            // Advance index
            ReadIndex = CalculatePhysicalIndex(ReadIndex + 1);
            --ValidCount;
        }
    }

    // Process current read resource
    FTCATAsyncResource& ReadResource = AsyncResources[ReadIndex];
    ReadResource.ReadTime = OwnerObject->GetWorld()->GetTimeSeconds();

    // Record debug information: frame number when read was completed
    FTCATAsyncDebugResource& DebugResource = AsyncDebugResources[ReadIndex];
    DebugResource.ReadFrameNumber = GFrameCounter;

    // Calculate and store latency
    LatestWriteReadLatencyTime = static_cast<float>(ReadResource.ReadTime - ReadResource.WriteTime);
	LatestReadResourcePredictionTime = DebugResource.PredictionTimeForDebug;
	LatestWriteReadLatencyFrames = static_cast<uint32>(DebugResource.ReadFrameNumber - DebugResource.WriteFrameNumber);
    if (bLogWriteReadLatency)
    {
        UE_LOG(LogTCAT, Log,
            TEXT("[%s] Read completed (WriteTime=%lf, ReadTime=%lf, Latency=%.5f sec, PredictionTime=%.5f sec), (WriteFrame=%llu, ReadFrame=%llu, FrameLatency=%llu), ValidCount=%d/%d"),
            *DebugName.ToString(), ReadResource.WriteTime, ReadResource.ReadTime, LatestWriteReadLatencyTime,
            LatestReadResourcePredictionTime,
            DebugResource.WriteFrameNumber, DebugResource.ReadFrameNumber, DebugResource.ReadFrameNumber - DebugResource.WriteFrameNumber,
            ValidCount, BufferSize);
    }

#if ENABLE_VISUAL_LOG
    if (FVisualLogger::IsRecording())
    {
        UE_VLOG(OwnerObject, DebugName, Log,
            TEXT("Read Completed: WriteTime=%.5f, ReadTime=%.5f, Latency=%.5f sec, PredictionTime=%.5f sec, WriteFrame=%llu, ReadFrame=%llu, FrameLatency=%llu, ValidCount=%d/%d"),
            ReadResource.WriteTime, ReadResource.ReadTime, LatestWriteReadLatencyTime,
            LatestReadResourcePredictionTime,
			DebugResource.WriteFrameNumber, DebugResource.ReadFrameNumber, DebugResource.ReadFrameNumber - DebugResource.WriteFrameNumber,
            ValidCount, BufferSize);
    }
#endif

    // Set output
    OutCurrentReadResource = ReadResource;

    // Reset resource for reuse
    ReadResource.Reset();
    DebugResource.WriteFrameNumber = MAX_uint64;
    DebugResource.ReadFrameNumber = MAX_uint64;

    // Advance index
    ReadIndex = CalculatePhysicalIndex(ReadIndex + 1);
    --ValidCount;

    return true;
}

void FTCATAsyncResourceRingBuffer::Initialize(UObject* Outer, int32 Width, int32 Height, FName ResourceDebugName)
{
    if (!Outer)
    {
        UE_LOG(LogTCAT, Error, TEXT("[RingBuffer] Initialize failed: Outer is null"));
        return;
    }

    const FString OwnerPrefix = Outer ? Outer->GetName() : TEXT("TCAT");
    FString DebugPrefix = !ResourceDebugName.IsNone() ? ResourceDebugName.ToString() : OwnerPrefix;
    DebugPrefix.ReplaceInline(TEXT(" "), TEXT("_"));

    for (int32 i = 0; i < BufferSize; ++i)
    {
        FTCATAsyncResource& Resource = AsyncResources[i];

        // Create RenderTarget
        if (!Resource.RenderTarget)
        {
            const FString BaseNameString = FString::Printf(TEXT("TCAT_%s_RT_%d"), *DebugPrefix, i);
            const FName BaseName(*BaseNameString);
            const FName UniqueName = MakeUniqueObjectName(Outer, UTextureRenderTarget2D::StaticClass(), BaseName);

            Resource.RenderTarget = NewObject<UTextureRenderTarget2D>(Outer, UniqueName);

            if (!Resource.RenderTarget)
            {
                checkf(false, TEXT("Failed to create RenderTarget!"));
                UE_LOG(LogTCAT, Error, TEXT("[RingBuffer] Failed to create RenderTarget for resource %d"), i);
                continue;
            }
        }

        Resource.RenderTarget->bCanCreateUAV = true;
        Resource.RenderTarget->RenderTargetFormat = RTF_R32f;
        Resource.RenderTarget->ClearColor = FLinearColor::Black;
        Resource.RenderTarget->InitAutoFormat(Width, Height);
        Resource.RenderTarget->UpdateResourceImmediate(true);

        // Create Readback
        if (!Resource.Readback)
        {
            const FString ReadbackName = FString::Printf(TEXT("TCAT_Readback_%s_%d"), *DebugPrefix, i);
            Resource.Readback = new FRHIGPUTextureReadback(*ReadbackName);
        }

        if (!Resource.CPUData.IsValid())
        {
            Resource.CPUData = MakeShared<FTCATReadbackCPUData, ESPMode::ThreadSafe>();
        }

        Resource.Reset();

        // Initialize debug resource
        AsyncDebugResources[i].WriteFrameNumber = MAX_uint64;
        AsyncDebugResources[i].ReadFrameNumber = MAX_uint64;
    }

    ReadIndex = 0;
    WriteIndex = 0;
    ValidCount = 0;

    OwnerObject = Outer;
    TextureWidth = Width;
    TextureHeight = Height;

	DebugName = FName(*FString::Printf(TEXT("TCAT.%s_RingBuffer"), *DebugPrefix));

    UE_LOG(LogTCAT, Log,
        TEXT("[RingBuffer] Initialized `%s` with %d resources (%dx%d)"),
        *DebugName.ToString(), BufferSize, Width, Height);
}

void FTCATAsyncResourceRingBuffer::Release()
{
    for (int32 i = 0; i < BufferSize; ++i)
    {
        FTCATAsyncResource& Resource = AsyncResources[i];
        if (Resource.RenderTarget)
        {
            Resource.RenderTarget = nullptr;
        }
        
        if (Resource.Readback)
        {
            // Delete on the render thread: render commands run in order, so any dispatch or copy request
            // that still references this readback finishes before it is freed.
            ENQUEUE_RENDER_COMMAND(TCAT_ReleaseReadback)(
                [Readback = Resource.Readback](FRHICommandListImmediate& RHICmdList)
                {
                    delete Readback;
                });
            Resource.Readback = nullptr;
        }

        // Queued copy requests hold their own reference to CPUData.
        Resource.CPUData.Reset();
        Resource.Reset();
    }

    ReadIndex = 0;
    WriteIndex = 0;
    ValidCount = 0;

    UE_LOG(LogTCAT, Verbose, TEXT("[RingBuffer] Released all resources"));
}
