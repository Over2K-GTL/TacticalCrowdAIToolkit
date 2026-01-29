// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIGPUReadback.h"
#include "TCATAsyncResourceRingBuffer.h"
#include "TCATGridResource.generated.h"

class UTextureRenderTarget2D;

/**
 * Generic CPU-side grid resource used by TCAT.
 *
 * This struct stores a 2D grid of float values (row-major) along with basic metadata.
 * It can optionally own an asynchronous GPU pipeline (RenderTarget + Readback ring buffer)
 * to support GPU-written maps that are read back to CPU without stalling the render thread.
 *
 * Data layout:
 *  - Grid is indexed as: Index = X + Y * Columns  (row-major)
 *  - Rows    = number of cells in Y direction
 *  - Columns = number of cells in X direction
 *
 * Notes for plugin users:
 *  - Grid values are "map-defined" floats (e.g., influence value, height value). Units depend on the producer.
 *  - MinMapValue / MaxMapValue are metadata fields; whether they are maintained depends on the producer code.
 *  - AsyncRingBuffer is for GPU pipeline parallelization; it does NOT automatically update Grid unless your system performs readback.
 */
USTRUCT(BlueprintType)
struct FTCATGridResource
{
	GENERATED_BODY()
	
	FTCATGridResource() : Rows(0), Columns(0), MinMapValue(0), MaxMapValue(0){}

	
	/**
	 * CPU-side grid values stored in row-major order.
	 * Size should be Rows * Columns after Resize().
	 */
	TArray<float> Grid;

	/** Grid height / width in cells (Y dimension/ X dimension). */
	int32 Rows, Columns;

	/**
	 * Optional metadata for the minimum/maximum value in the grid.
	 * Whether this is valid depends on the system that fills the grid (not guaranteed by the container itself).
	 */
	float MinMapValue, MaxMapValue; 

	/**
	 * Asynchronous GPU resource ring buffer used to pipeline RenderTarget writes and GPU readbacks.
	 *
	 * Visible in the Details panel for debugging/inspection.
	 * Typically used for influence/composite maps that are computed on GPU and later read back to CPU.
	 */
	UPROPERTY(VisibleInstanceOnly, Category = "TCAT")
	FTCATAsyncResourceRingBuffer AsyncRingBuffer;

	/**
	 * Frame number of the last request that touched this resource.
	 * This is primarily for internal throttling/debugging and may be used to avoid redundant work per frame.
	 */
	uint64 LastRequestFrame = 0;
	
	/**
	 * Convert 2D grid coordinates to a linear index into Grid (row-major).
	 *
	 * @param X Cell coordinate in [0 .. Columns-1]
	 * @param Y Cell coordinate in [0 .. Rows-1]
	 * @return Linear index = X + Y * Columns (no bounds checking).
	 *
	 * Callers should validate bounds before calling if inputs may be out of range.
	 */
	int32 GetIndex(int32 X, int32 Y) const;

	/**
	 * Allocate/resize the CPU grid and (optionally) initialize async GPU resources.
	 *
	 * @param InRows             Number of rows (Y dimension).
	 * @param InCols             Number of columns (X dimension).
	 * @param Outer              UObject owner used for creating UObject-based resources (e.g., render targets).
	 * @param ResourceDebugName  Optional name used for debugging/profiling resource allocations.
	 *
	 * Expected effect:
	 *  - Grid is resized to InRows * InCols.
	 *  - Internal metadata may be reset depending on implementation.
	 */
	void Resize(int32 InRows, int32 InCols, UObject* Outer, FName ResourceDebugName = NAME_None);

	/**
	 * Release owned resources and reset to an empty state.
	 *
	 * This should be called when:
	 *  - The owning volume/component is being destroyed
	 *  - The map resolution changes permanently
	 *  - You want to free GPU/CPU memory explicitly
	 */
	void Release();
};

/**
 * Height-map specialized grid resource.
 *
 * In addition to the CPU grid storage, this resource exposes a synchronous RenderTarget reference
 * that can be updated immediately from CPU-side height baking.
 *
 * Notes:
 *  - RenderTarget is transient and intended as a runtime visualization / GPU consumer input.
 *  - Height values are typically world-space Z (Unreal units), but that depends on the baker.
 */
USTRUCT(BlueprintType)
struct FTCATHeightMapResource : public FTCATGridResource
{
	GENERATED_BODY()

	FTCATHeightMapResource() : FTCATGridResource(), RenderTarget(nullptr){}

	/**
	 * Render target associated with this height map.
	 * Typically updated synchronously by CPU baking code (not pipelined via readback).
	 */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "TCAT")
	UTextureRenderTarget2D* RenderTarget;

	/**
	 * Resize the height map resource and ensure the render target matches the new resolution if applicable.
	 *
	 * @param InRows             Number of rows (Y dimension).
	 * @param InCols             Number of columns (X dimension).
	 * @param Outer              UObject owner used for creating UObject-based resources.
	 * @param ResourceDebugName  Optional name used for debugging/profiling.
	 */
	void Resize(int32 InRows, int32 InCols, UObject* Outer, FName ResourceDebugName = NAME_None);
	void Release();
};