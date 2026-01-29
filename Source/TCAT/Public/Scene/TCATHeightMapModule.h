// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "TCATHeightMapModule.generated.h"

struct FTCATGridResource;
class ATCATInfluenceVolume;

/**
 * Height-map baking and debug visualization module for TCAT Influence Volumes.
 *
 * This module samples terrain height for each grid cell by performing a vertical line trace.
 * The sampled heights are stored in the owning volume's HeightResource (CPU grid and optional render target).
 *
 * Typical usage:
 *  - Call Bake() when the volume bounds / cell size / level geometry changes (e.g., editor time or map load).
 *  - Enable bDrawHeight to visualize slope-like discontinuities (neighbor height deltas) for debugging navigation or walls.
 *
 * Notes:
 *  - Height values are stored in world-space Z units (Unreal units).
 *  - Debug coloring is based on the maximum absolute height difference to 4-neighbor cells (not a physical cliff detection).
 */
USTRUCT(BlueprintType)
struct FTCATHeightMapModule
{
	GENERATED_BODY()
	
public:
	/**
	 * If enabled, the module draws persistent debug points for each grid cell.
	 * Intended for editor/debug visualization only; disable for shipping gameplay.
	 *
	 * Visualization:
	 *  - Green: max neighbor height delta <= HeightToMark
	 *  - Red  : max neighbor height delta  > HeightToMark
	 */
	UPROPERTY(EditAnywhere, Category="TCAT|DebugSettings")
	bool bDrawHeight = false;
	
	/**
	 * Threshold (world-space Z units) for marking a cell as "steep" in debug visualization.
	 *
	 * The module compares the current cell height against its 4-neighbors (N/E/S/W) and finds
	 * the maximum absolute height difference. If that value exceeds this threshold,
	 * the cell is drawn as a red debug point.
	 *
	 * This does NOT represent an actual geometric cliff height; it is a grid-based height delta heuristic.
	 */
	UPROPERTY(EditAnywhere, Category="TCAT|DebugSettings", meta = (EditCondition = "bDrawHeight"))
	float HeightToMark = 50.0f;
	
public:
	/**
	 * Bake the height map by tracing downward at the center of each grid cell.
	 *
	 * @param Owner      The volume that owns the HeightResource to write into.
	 * @param CellSize   Grid cell size in world units (must match the volume's grid configuration).
	 * @param Resolution Grid resolution in cells (X = columns/width, Y = rows/height).
	 *
	 * Output:
	 *  - Owner->HeightResource.Grid will contain world-space Z heights per cell (row-major).
	 *  - If a render target exists in the resource, it is updated with float height values.
	 *
	 * Performance:
	 *  - Uses parallel tracing per cell; avoid calling every frame.
	 *  - For best results, bake in editor or at level initialization.
	 */
	void Bake(ATCATInfluenceVolume* Owner, float CellSize, FIntPoint Resolution);
	
	/**
	 * Draw persistent debug points representing slope-like discontinuities using HeightResource.
	 *
	 * Requires:
	 *  - bDrawHeight == true
	 *  - Owner->HeightResource.Grid to be baked and non-empty
	 *
	 * Color rule:
	 *  - Red   = max 4-neighbor height delta > HeightToMark
	 *  - Green = otherwise
	 */
	void DrawDebug(const ATCATInfluenceVolume* Owner) const;
	
	/**
	 * Clear all persistent debug lines/points in the current world and redraw (if enabled).
	 *
	 * Use when:
	 *  - Toggling bDrawHeight in editor
	 *  - Updating HeightToMark
	 *  - Re-baking and wanting to refresh visualization
	 */
	void FlushAndRedraw(const ATCATInfluenceVolume* Owner);

private:
	/**
	 * Perform vertical line traces for each grid cell and write results into OutResource.
	 *
	 * Trace shape:
	 *  - Start Z = Bounds.Max.Z + TRACE_OFFSET_UP
	 *  - End   Z = Bounds.Min.Z - TRACE_OFFSET_DOWN
	 *
	 * Filtering:
	 *  - Traces WorldStatic objects
	 *  - Ignores movable components and volume actors
	 *  - Ignores the Owner actor itself
	 *
	 * @param Owner      The owning volume (used for world access and ignore list).
	 * @param Bounds     Bounding box used to compute per-cell world XY and trace range.
	 * @param CellSize   Cell size in world units.
	 * @param Resolution Grid resolution (X columns, Y rows).
	 * @param OutResource Destination grid resource (must be resized beforehand).
	 */
	void PerformLineTraces(ATCATInfluenceVolume* Owner, const FBox& Bounds,
		float CellSize, FIntPoint Resolution, FTCATGridResource& OutResource);

	/**
	 * Draw per-cell debug points at the baked height with a small Z offset for visibility.
	 *
	 * @param Owner      Volume owner (world context).
	 * @param Resource   Baked height grid resource.
	 * @param Bounds     Bounds used to reconstruct cell center positions in world XY.
	 * @param CellSize   Cell size in world units.
	 */
	void DrawHeightDebugPoints(const ATCATInfluenceVolume* Owner, const FTCATGridResource& Resource, const FBox& Bounds, float CellSize) const;

	/**
	 * Compute a grid-based "steepness" measure for the cell (X,Y).
	 *
	 * Returns the maximum absolute height difference between the cell and its 4-neighbors (N/E/S/W).
	 * This is used only for debug coloring and does not replace navigation, physics, or geometric slope checks.
	 *
	 * @return Max absolute neighbor height delta in world-space Z units.
	 */
	float CalculateCliffHeight(const FTCATGridResource& Resource, int32 X, int32 Y) const;
};