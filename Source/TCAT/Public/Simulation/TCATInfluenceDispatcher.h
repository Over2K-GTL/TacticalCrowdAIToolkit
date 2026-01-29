// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "Core/TCATTypes.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RenderGraphDefinitions.h"

class FRDGBuilder;

/**
 * Per-layer dispatch parameters for generating a single *base* influence map.
 *
 * This struct is intentionally "plain data" so it can be assembled on the Game Thread
 * and consumed by either CPU simulation code or GPU RDG passes.
 *
 * Notes for plugin users (C++ / Blueprint / Details):
 * - The CPU path writes into OutGridData, while the GPU path writes into OutInfluenceMapRHI.
 * - Height/visibility behavior is controlled by ProjectionFlags and the height map inputs.
 */
struct FTCATInfluenceDispatchParams
{
	/** Debug label used for RDG event names and log messages. Typically "VolumeName_LayerTag". */
	FString VolumeName;
	/**
	 * Bitmask of ETCATProjectionFlag.
	 * - MaxInfluenceHeight: culls cells with Z > MaxInfluenceZ for each source
	 * - LineOfSight: performs visibility checks using the height map
	 */
	int32 ProjectionFlags;
	
	/**
	 * Influence sources to evaluate for this layer.
	 * GPU path uploads this array into a structured buffer.
	 * CPU path iterates it directly.
	 *
	 * IMPORTANT: Sources are expected to be in world space (WorldLocation),
	 * and CurveTypeIndex must match the row in the global curve atlas.
	 */
	TArray<FTCATInfluenceSource> Sources;

	/**
	 * CPU-only curve atlas pixels (row-major).
	 * Used to evaluate falloff curves on CPU with the same sampling rules as the GPU atlas.
	 *
	 * Format: AtlasHeight rows (curve types) x AtlasWidth columns (U in [0..1]).
	 */
	TArray<float> CurveAtlasPixelData;
	
	/** Global curve atlas as a GPU texture (PF_R32_FLOAT). Used by GPU influence map logic. */
	FTextureRHIRef GlobalCurveAtlasRHI;

	/** Height map as a GPU texture (PF_R32_FLOAT). Used by both CPU & GPU influence map logic. */
	FTextureRHIRef GlobalHeightMapRHI;
	/**
	 * World-space origin (XY) of the height map coverage.
	 * Z is ignored for UV mapping but may be stored for convenience.
	 */
	FVector3f GlobalHeightMapOrigin;
	/**
	 * World-space size (XY) of the height map coverage.
	 * UV = (WorldXY - OriginXY) / SizeXY
	 */
	FVector2f GlobalHeightMapSize;

	/**
	 * CPU height samples laid out in the same grid resolution as MapSize (row-major).
	 * If nullptr (or too small), CPU will treat cell height as MapStartPos.Z.
	 */
	const TArray<float>* GlobalHeightMapData = nullptr;

	/**
	 * Line-of-sight raymarch step size in centimeters (world units).
	 * Smaller values increase accuracy but cost more on CPU (and can increase GPU work).
	 */
	float RayMarchStepSize = 100.0f;
	/**
	 * Maximum number of raymarch steps for line-of-sight checks.
	 * This is a hard cap; actual steps are also limited by distance / RayMarchStepSize.
	 */
	int32 RayMarchMaxSteps = 32;

	/** World-space start position of the grid (origin of cell [0,0]). Z is used as default cell height. */
	FVector MapStartPos = FVector::ZeroVector;

	/** Cell size in world units (centimeters). */
	float GridSize;

	/** Grid resolution in cells: X = columns, Y = rows. */
	FUintVector2 MapSize;

	/**
	 * CPU output grid (row-major, size = MapSize.X * MapSize.Y).
	 * Required for CPU dispatch. If nullptr, CPU dispatch is a no-op.
	 */
	TArray<float>* OutGridData = nullptr;
	
	// ============================
	// GPU-only outputs / control
	// ============================
	/**
	 * GPU output texture for the influence map (PF_R32_FLOAT).
	 * Must be a valid texture when using the GPU path.
	 */
	FTextureRHIRef OutInfluenceMapRHI;
	/**
	 * Optional async GPU readback object.
	 * If non-null, the dispatcher enqueues a copy of the output texture into this readback.
	 *
	 * Lifetime: must remain valid until the owning ring-buffer or system consumes it.
	 */
	class FRHIGPUTextureReadback* GPUReadback = nullptr;

	/** Width of the curve atlas texture in pixels (columns). Must match atlas build settings. */
	int32 AtlasWidth = 256;

	/**
	 * When false, the dispatcher should skip writing/updating this layer for the current frame.
	 * This is typically set by internal ring-buffer availability checks.
	 */
	bool bEnableWrite = true;

	/**
	 * Forces CPU path to run single-threaded (ParallelFor disabled).
	 * For testing/profiling determinism only; not recommended for shipping content.
	 */
	bool bForceCPUSingleThread = false; // used by CPU logic only.
};

/**
 * Dispatch parameters for composition operations (creating a *composite* influence map).
 *
 * A composite map is produced by chaining FTCATCompositeOperation steps using one or more input layers.
 * The same recipe can run on GPU (RDG compute) or CPU (ParallelFor).
 */
struct FTCATCompositeDispatchParams
{
	/** Debug label used for RDG event names and log messages. Typically the owning volume name. */
	FString VolumeName;
	
	// ============================
	// Composition-specific inputs
	// ============================
	
	/**
	 * GPU inputs: map from layer tag -> RHI texture (PF_R32_FLOAT).
	 * The dispatcher will register these as external RDG textures and sample them in compute passes.
	 */
	TMap<FName, FTextureRHIRef> InputTextureMap;  // GPU: Input volume textures to blend

	/**
	 * CPU inputs: map from layer tag -> pointer to CPU grid data (row-major).
	 * Each grid is expected to be size MapSize.X * MapSize.Y.
	 */
	TMap<FName, TArray<float>*> InputGridDataMap;     // CPU: Input volume grid data

	/**
	 * Ordered list of operations to execute.
	 * NOTE: ETCATCompositeOp::Normalize normalizes the *accumulator* (unary boundary),
	 * while FTCATCompositeOperation::bNormalizeInput normalizes *the current input map*.
	 */
	TArray<FTCATCompositeOperation> Operations;

	// ============================
	// Output configuration
	// ============================
	/** GPU output texture for the composite map (PF_R32_FLOAT). */
	FTextureRHIRef OutInfluenceMapRHI;
	
	/** World-space start position of the grid (origin of cell [0,0]). */
	FVector MapStartPos;

	/** Grid resolution in cells: X = columns, Y = rows. */
	FUintVector2 MapSize = FUintVector2(0, 0);

	/**
	 * CPU output grid (row-major, size = MapSize.X * MapSize.Y).
	 * Required for CPU composite dispatch. If nullptr, CPU dispatch is a no-op.
	 */
	TArray<float>* OutGridData = nullptr;

	// ============================
	// Readback control
	// ============================
	/** Optional async GPU readback object. See FTCATInfluenceDispatchParams::GPUReadback. */
	class FRHIGPUTextureReadback* GPUReadback = nullptr;

	/** When false, the dispatcher should skip writing/updating this composite map for the current frame. */
	bool bEnableWrite = true;

	/** Forces CPU composite path to run single-threaded (ParallelFor disabled). Testing/profiling only. */
	bool bForceCPUSingleThread = false; // For testing purposes. used by CPU logic only.
};

// Composite operation preparation
struct FPreparedCompositeOp
{
	ETCATCompositeOp Operation = ETCATCompositeOp::Add;
	const TArray<float>* Grid = nullptr;
	float Strength = 1.0f;
	bool bClampInput = false;
	float ClampMin = 0.0f;
	float ClampMax = 0.0f;
	bool bNormalizeInput = false;
	float Min = 0.0f;
	float Max = 0.0f;
	float InvRange = 0.0f;
};

struct FNormalizationStats
{
	float Min = 0.0f;
	float Max = 0.0f;
	float InvRange = 0.0f;
};

/**
 * Low-level dispatcher that executes influence map updates on GPU (RDG) or CPU.
 *
 * This is an internal implementation layer used by UTCATSubsystem.
 * Plugin users typically interact with higher-level APIs (Volumes, Components, Queries),
 * not with the dispatcher directly.
 *
 * Threading / callsite expectations:
 * - DispatchGPU_Batched is called from the Render Thread via ENQUEUE_RENDER_COMMAND.
 * - DispatchCPU* functions are called from the Game Thread (or worker threads during measurement),
 *   and rely only on the data provided in the parameter structs.
 */
class FTCATInfluenceDispatcher
{
public:
	static void DispatchGPU_Batched(FRHICommandListImmediate& RHICmdList,
		TArray<FTCATInfluenceDispatchParams>&& InfluenceBatch,
		TArray<FTCATCompositeDispatchParams>&& CompositeBatch);
	
	static void DispatchCPU(const FTCATInfluenceDispatchParams& Params);
	static void DispatchCPU_Composite(const FTCATCompositeDispatchParams& Params);
	
	/**
	 * CPU "partial" base-layer update that corrects the grid by removing OldSources and adding NewSources
	 * without recomputing the entire layer.
	 *
	 * Usage: position prediction correction after GPU readback, where only a subset of sources drifted.
	 *
	 * Requirements:
	 * - OldSources.Num() must equal NewSources.Num() (1:1 correction pairs).
	 * - Params.OutGridData must already contain the current accumulated state to be corrected.
	 */
	static void DispatchCPU_Partial(const FTCATInfluenceDispatchParams& Params, const TArray<FTCATInfluenceSource>& OldSources, const TArray<FTCATInfluenceSource>& NewSources);

	/**
	 * CPU "partial" composite update that recalculates only selected cells.
	 *
	 * Limitations:
	 * - If the composite recipe contains ETCATCompositeOp::Normalize, partial updates are not valid
	 *   without recomputing global min/max, so the implementation may fall back to a full update.
	 */
	static void DispatchCPU_Composite_Partial(const FTCATCompositeDispatchParams& Params, const TArray<int32>& AffectedCellIndices);

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
	
// --- Helper functions for cpu dispatch ---------------

	static FVector CalculateCellWorldPosition(
		int32 CellIndex,
		const FTCATInfluenceDispatchParams& Params,
		const TArray<float>* HeightData,
		bool bUseCellHeight);

	// Calculate influence for a single source at a cell
	static float CalculateSourceInfluence(
		const FTCATInfluenceSource& Source,
		const FVector& CellPos,
		const FTCATInfluenceDispatchParams& Params,
		bool bLimitVerticalRange,
		bool bCheckLineOfSight);

	static void PrepareCompositeOperations(
		const FTCATCompositeDispatchParams& Params,
		TArray<FPreparedCompositeOp>& OutPreparedOps,
		TMap<FName, FNormalizationStats>& OutNormCache);

	// Apply prepared operations to a single cell
	static float ApplyCompositeOperations(
		int32 CellIndex,
		const TArray<FPreparedCompositeOp>& PreparedOps);
};

