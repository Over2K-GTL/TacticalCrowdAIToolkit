// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Simulation/TCATAsyncResourceRingBuffer.h"
#include "RHIGPUReadback.h"
#include "TCAT.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "VisualLogger/VisualLogger.h"

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

bool FTCATAsyncResourceRingBuffer::IsReadbackReady(const int32 Index) const
{
    const FTCATAsyncResource& AsyncResource = AsyncResources[Index];
    return AsyncResource.Readback && AsyncResource.Readback->IsReady();
}

bool FTCATAsyncResourceRingBuffer::IsCurrentReadBackReady() const
{
    if (ValidCount == 0)
    {
        return false;
    }

	return IsReadbackReady(ReadIndex);
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

    if (!AsyncResources[Index].WasMostRecentActionWrite())
    {
        UE_LOG(LogTCAT, VeryVerbose,
            TEXT("[%s] Read Resource(index: %d) is not resource that most recently called AdvanceWriteResource."), *DebugName.ToString(), Index);
        return false;
    }

    if (!IsReadbackReady(Index))
    {
        UE_LOG(LogTCAT, VeryVerbose, TEXT("[%s] Read Resource(index: %d)'s Readback not ready yet."), *DebugName.ToString(), Index);
        return false;
    }

    return true;
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
        else // The next next resource is also ready.
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

        Resource.Reset();

        // Initialize debug resource
        AsyncDebugResources[i].WriteFrameNumber = MAX_uint64;
        AsyncDebugResources[i].ReadFrameNumber = MAX_uint64;
    }

    ReadIndex = 0;
    WriteIndex = 0;
    ValidCount = 0;

    OwnerObject = Outer;

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
            delete Resource.Readback;
            Resource.Readback = nullptr;
        }
    }

    UE_LOG(LogTCAT, Verbose, TEXT("[RingBuffer] Released all resources"));
}
