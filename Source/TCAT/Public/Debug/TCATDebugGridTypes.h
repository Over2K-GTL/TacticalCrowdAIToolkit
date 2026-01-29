// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Forward declarations
struct FTCATGridResource;
struct FTCATLayerDebugSettings;
enum class ETCATDebugDrawMode : uint8;

/**
 * Parameters for updating debug grid visualization from volume data.
 */
struct FTCATDebugGridUpdateParams
{
	ETCATDebugDrawMode DrawMode;  // Initialized in cpp where enum is fully defined
	const TMap<FName, FTCATGridResource>* InfluenceLayers = nullptr;
	const TMap<FName, FTCATLayerDebugSettings>* DebugSettings = nullptr;
	const TArray<float>* HeightGrid = nullptr;
	FBox Bounds;
	FIntPoint Resolution = FIntPoint::ZeroValue;
	float CellSize = 100.0f;
	float GridOriginZ = 0.0f;
	UWorld* World = nullptr;  // For DrawDebugString (text labels)
	FColor TextColor = FColor::White;  // Color for debug text labels
	bool bDrawText = true;

	FTCATDebugGridUpdateParams();  // Default constructor defined in cpp
};

/**
 * Single cell data for debug grid rendering.
 * Packed for efficient transfer to render thread.
 */
struct FTCATDebugGridCellData
{
	FVector3f Position;
	FColor Color;

	FTCATDebugGridCellData() = default;
	FTCATDebugGridCellData(const FVector3f& InPosition, const FColor& InColor)
		: Position(InPosition), Color(InColor)
	{
	}
};

/**
 * Text label data for batched text rendering.
 * Stores position, value, and color to be rendered in the scene proxy.
 */
struct FTCATDebugGridTextData
{
	FVector3f Position;
	float Value;
	FColor Color;

	FTCATDebugGridTextData() = default;
	FTCATDebugGridTextData(const FVector3f& InPosition, float InValue, const FColor& InColor)
		: Position(InPosition), Value(InValue), Color(InColor)
	{
	}
};

/**
 * Per-layer data containing all visible cells for that layer.
 */
struct FTCATDebugGridLayerData
{
	FName LayerTag;
	TArray<FTCATDebugGridCellData> Cells;
	TArray<FTCATDebugGridTextData> TextLabels;
	bool bVisible = true;
	float PointSize = 1.0f;

	void Reset()
	{
		Cells.Reset();
		TextLabels.Reset();
	}
};

/**
 * Complete render data for all layers, transferred to scene proxy.
 */
struct FTCATDebugGridRenderData
{
	TArray<FTCATDebugGridLayerData> Layers;
	uint32 DataVersion = 0;

	void Reset()
	{
		for (FTCATDebugGridLayerData& Layer : Layers)
		{
			Layer.Reset();
		}
		Layers.Reset();
	}
};
