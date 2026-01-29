// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TCATTypes.h"
#include "TCATMathLibrary.generated.h"

/**
 * Utility math functions used by TCAT.
 *
 * This library provides Blueprint-accessible helpers for:
 *  - Converting between world-space locations and TCAT grid indices
 *  - Building curve-atlas float data for GPU sampling
 *  - Small compare utilities used by query/condition nodes
 *
 * Coordinate convention (recommended):
 *  - MapStartLocation is the world-space origin of the grid (typically the volume bounds min in XY).
 *  - GridIndex.X increases along +X direction, GridIndex.Y increases along +Y direction.
 *  - GridSize is the size of one cell in world units.
 */
UCLASS()
class TCAT_API UTCATMathLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Convert a world-space location to a grid index (cell coordinates).
	 *
	 * @param WorldLocation     Input world-space location.
	 * @param MapStartLocation  World-space origin of the grid (cell (0,0) start).
	 * @param GridSize          Cell size in world units.
	 * @param MapWidth          Grid width in cells (number of columns).
	 * @param MapHeight         Grid height in cells (number of rows).
	 *
	 * @return Grid index (X,Y). If the location is outside the map extents, the returned value may be
	 *         outside [0..MapWidth-1], [0..MapHeight-1] depending on implementation.
	 *
	 * Tip:
	 *  - Callers should clamp or validate bounds if out-of-range indices are not allowed.
	 */
	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static FIntPoint WorldToGrid(const FVector& WorldLocation, const FVector& MapStartLocation, float GridSize, int32 MapWidth, int32 MapHeight);

	/**
	 * Convert a grid index (cell coordinates) to the world-space location of the cell center.
	 *
	 * @param GridIndex         Cell index (X,Y).
	 * @param MapStartLocation  World-space origin of the grid (cell (0,0) start).
	 * @param GridSize          Cell size in world units.
	 *
	 * @return World-space position corresponding to the cell center.
	 *
	 * Note:
	 *  - This function does not validate bounds; callers should ensure GridIndex is within map extents.
	 */
	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static FVector GridToWorld(const FIntPoint& GridIndex, const FVector& MapStartLocation, float GridSize);

	/**
	 * Build float atlas data for a set of unique curves.
	 *
	 * The output is a 1D-packed float buffer representing multiple curves sampled across TextureWidth.
	 * This buffer can be uploaded into a texture/structured buffer and sampled on CPU/GPU.
	 *
	 * @param InUniqueCurves  Unique curve assets to pack into the atlas (order defines atlas rows).
	 * @param TextureWidth    Number of samples per curve (atlas "width").
	 * @param OutAtlasData    Output float array containing packed samples (size depends on curve count * TextureWidth).
	 *
	 * Notes:
	 *  - Curves are typically sampled in normalized X domain [0..1].
	 *  - Curve evaluation and packing rules should match the runtime sampling logic used by TCAT.
	 */
	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static void BuildCurveAtlasData(const TArray<UCurveFloat*>& InUniqueCurves, int32 TextureWidth, TArray<float>& OutAtlasData);

	/**
	 * Compare two float values using a TCAT comparison operator.
	 *
	 * @param A         Left-hand operand.
	 * @param B         Right-hand operand.
	 * @param Condition Comparison type (e.g., Less, Greater, Equal, etc.).
	 *
	 * @return True if the comparison holds.
	 *
	 * Note:
	 *  - If equality is supported, the implementation should define whether it uses exact or tolerance-based compare.
	 */
	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static bool CompareFloat(float A, float B, ETCATCompareType Condition);

	/**
	 * A fast 2D spatial hash that maps integer grid coordinates to a pseudo-random float in [0,1].
	 *
	 * Use cases:
	 *  - Stable per-cell noise for tie-breaking
	 *  - Deterministic randomization for sampling/query results (given a fixed Seed)
	 *
	 * @param X     Grid X coordinate.
	 * @param Y     Grid Y coordinate.
	 * @param Seed  Additional seed to vary the distribution between systems/queries.
	 *
	 * @return Pseudo-random float in [0,1], deterministic for the same (X,Y,Seed).
	 */
	FORCEINLINE static float GetSpatialHash(int32 X, int32 Y, uint32 Seed)
	{
		uint32 Hash = Seed;
		Hash ^= (X * 73856093);
		Hash ^= (Y * 19349663);
		Hash = (Hash << 13) ^ Hash;
		
		return (Hash & 0x007FFFFF) * (1.0f / 8388607.0f); 
	}
};
