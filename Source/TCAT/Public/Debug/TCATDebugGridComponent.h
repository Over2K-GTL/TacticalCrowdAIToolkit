// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/TCATDebugGridTypes.h"
#include "TCATDebugGridComponent.generated.h"

class FTCATDebugGridSceneProxy;

/**
 * Debug visualization component for TCAT influence grids.
 * Uses a custom scene proxy for efficient batched rendering.
 *
 * This component renders behind transformation gizmos (SDPG_World)
 * so users can still interact with selected volumes.
 */
UCLASS(ClassGroup = (TCAT), meta = (BlueprintSpawnableComponent = "false"))
class TCAT_API UTCATDebugGridComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UTCATDebugGridComponent();

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UPrimitiveComponent Interface

	/**
	 * Updates the debug grid data. Call this when influence data changes.
	 * Thread-safe transfer to render thread.
	 * @param InRenderData The new render data containing all layer cells.
	 */
	void UpdateGridData(FTCATDebugGridRenderData&& InRenderData);

	/**
	 * Toggle visibility for a specific layer without full data rebuild.
	 * @param LayerTag The layer to toggle.
	 * @param bVisible New visibility state.
	 */
	void SetLayerVisibility(FName LayerTag, bool bVisible);

	/** Set the bounds used for culling. */
	void SetGridBounds(const FBox& InBounds);

	/**
	 * Update visualization from volume data. Builds render data internally.
	 * This is the main entry point - handles visibility checks, color interpolation,
	 * height lookup, cell building, and text rendering.
	 * @param Params All data needed to build the visualization.
	 */
	void UpdateFromVolumeData(const FTCATDebugGridUpdateParams& Params);

	/** Get the current point size multiplier. */
	float GetPointSize() const { return PointSize; }

	/** Set the point size multiplier for rendering. */
	void SetPointSize(float InSize) { PointSize = FMath::Max(0.1f, InSize); }

	/** Get text character size. */
	float GetTextCharSize() const { return TextCharSize; }

protected:
	/** Cached bounds for the grid visualization. */
	FBox GridBounds;

	/** Point size multiplier for debug rendering. */
	float PointSize = 1.0f;

	/** Character size for text rendering. */
	float TextCharSize = 10.0f;

	/** Current render data (game thread copy). */
	FTCATDebugGridRenderData CurrentRenderData;

	/** Version counter for lazy updates. */
	uint32 DataVersion = 0;
};
