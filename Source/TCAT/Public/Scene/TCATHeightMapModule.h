// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "TCATHeightMapModule.generated.h"

struct FTCATGridResource;
class ATCATInfluenceVolume;
/**
 * Height map configuration and baking module.
 * The actual grid data is stored in the owner volume's HeightResource.
 */
USTRUCT(BlueprintType)
struct FTCATHeightMapModule
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, Category="TCAT")
	bool bDrawHeight = false;
	
	// Check Height, and will be appear in Red Dot if it's above HeightToMark in Borderline
	UPROPERTY(EditAnywhere, Category="TCAT", meta = (EditCondition = "bDrawHeight"))
	float HeightToMark = 50.0f;
	
public:
	/**
	 * Bakes the height map by tracing downward from each grid cell.
	 * Stores results in the owner's HeightResource.
	 */
	void Bake(ATCATInfluenceVolume* Owner, float CellSize, FIntPoint Resolution);
	
	/**
	 * Draws debug visualization showing height differences between cells.
	 * Red = steep slope (potential wall), Green = flat terrain.
	 */
	void DrawDebug(const ATCATInfluenceVolume* Owner) const;
	
	/**
	 * Flushes persistent debug lines and redraws if enabled.
	 */
	void FlushAndRedraw(const ATCATInfluenceVolume* Owner);

private:
	void PerformLineTraces(
		ATCATInfluenceVolume* Owner,
		const FBox& Bounds,
		float CellSize,
		FIntPoint Resolution,
		FTCATGridResource& OutResource
	);
	
	void DrawHeightDebugPoints(
		const ATCATInfluenceVolume* Owner,
		const FTCATGridResource& Resource,
		const FBox& Bounds,
		float CellSize
	) const;
	
	float CalculateCliffHeight(
		const FTCATGridResource& Resource,
		int32 X, int32 Y
	) const;
};