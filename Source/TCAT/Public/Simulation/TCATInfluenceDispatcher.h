// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "Core/TCATTypes.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RenderGraphDefinitions.h"

class FRDGBuilder;

struct FTCATInfluenceDispatchParams
{
	FString VolumeName;
	int32 ProjectionFlags;
	
	TArray<FTCATInfluenceSource> Sources;
	
	TArray<float> CurveAtlasPixelData; // Used by CPU influence map logic
	FTextureRHIRef GlobalCurveAtlasRHI; // Used by GPU influence map logic

	FTextureRHIRef GlobalHeightMapRHI; // Used by Both CPU & GPU influence map logic
	FVector3f GlobalHeightMapOrigin;
	FVector2f GlobalHeightMapSize;
	const TArray<float>* GlobalHeightMapData = nullptr; // CPU height samples
	float RayMarchStepSize = 100.0f;
	int32 RayMarchMaxSteps = 32;

	FVector MapStartPos;
	float GridSize;
	FUintVector2 MapSize;
	TArray<float>* OutGridData = nullptr;
	
	// -- GPU ONLY DATA --
	FTextureRHIRef OutInfluenceMapRHI;
	bool bIsAsync = false;
	// -- Async ONLY DATA --
	class FRHIGPUTextureReadback* GPUReadback = nullptr;

	int32 AtlasWidth = 256;

	bool bEnableWrite = true;

	bool bForceCPUSingleThread = false; // For testing purposes. used by CPU logic only.
};

// Dispatch parameters for composition operations
struct FTCATCompositeDispatchParams
{
	FString VolumeName;

	// Composition-specific inputs
	TMap<FName, FTextureRHIRef> InputTextureMap;  // GPU: Input volume textures to blend
	TMap<FName, TArray<float>*> InputGridDataMap;     // CPU: Input volume grid data
	TArray<FTCATCompositeOperation> Operations;

	// Output configuration
	FTextureRHIRef OutInfluenceMapRHI;
	FVector MapStartPos;
	FUintVector2 MapSize = FUintVector2(0, 0);
	TArray<float>* OutGridData = nullptr;

	// Readback control (matches existing pattern)
	bool bIsAsync = false;
	class FRHIGPUTextureReadback* GPUReadback = nullptr;
	
	bool bEnableWrite = true;

	bool bForceCPUSingleThread = false; // For testing purposes. used by CPU logic only.
};

class FTCATInfluenceDispatcher
{
public:
	static void DispatchGPU_Batched(
		FRHICommandListImmediate& RHICmdList,
		TArray<FTCATInfluenceDispatchParams>&& InfluenceBatch,
		TArray<FTCATCompositeDispatchParams>&& CompositeBatch);
	static void DispatchCPU(const FTCATInfluenceDispatchParams& Params);
	static void DispatchCPU_Composite(const FTCATCompositeDispatchParams& Params);

	static void DispatchCPU_Partial(
		const FTCATInfluenceDispatchParams& Params,
		const TArray<FTCATInfluenceSource>& OldSources,
		const TArray<FTCATInfluenceSource>& NewSources);

	static void DispatchCPU_Composite_Partial(
		const FTCATCompositeDispatchParams& Params,
		const TArray<int32>& AffectedCellIndices);

private:
	// Sample Curve Atlas just like GPU does
	static float SampleCurveAtlasCPU(const TArray<float>& AtlasData, int32 AtlasWidth, int32 RowIndex, float U);
	static bool SampleHeightMapAtUV(const FTCATInfluenceDispatchParams& Params, const FVector2f& UV, float& OutHeight);
	static bool SampleHeightMapAtWorld(const FTCATInfluenceDispatchParams& Params, const FVector2f& WorldXY, float& OutHeight);
	static float CheckVisibilityCPU(const FTCATInfluenceDispatchParams& Params, const FVector& SourceLocation, float LineOfSightOffset, const FVector& TargetLocation);

	// Dispatch min/max reduction passes to compute the min and max values of a texture
	static FRDGTextureSRVRef DispatchMinMaxReduction(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef InputTexture,
		FUintVector2 MapSize,
		const FString& DebugName);
};

