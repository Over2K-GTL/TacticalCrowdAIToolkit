// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TCATTypes.h"
#include "TCATMathLibrary.generated.h"

/**
 * 
 */
UCLASS()
class TCAT_API UTCATMathLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static FIntPoint WorldToGrid(const FVector& WorldLocation, const FVector& MapStartLocation, float GridSize, int32 MapWidth, int32 MapHeight);

	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static FVector GridToWorld(const FIntPoint& GridIndex, const FVector& MapStartLocation, float GridSize);

	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static void BuildCurveAtlasData(const TArray<UCurveFloat*>& InUniqueCurves, int32 TextureWidth, TArray<float>& OutAtlasData);

	UFUNCTION(BlueprintCallable, Category = "TCAT|Math")
	static bool CompareFloat(float A, float B, ETCATCompareType Condition);

	FORCEINLINE static float GetSpatialHash(int32 X, int32 Y, uint32 Seed)
	{
		uint32 Hash = Seed;
		Hash ^= (X * 73856093);
		Hash ^= (Y * 19349663);
		Hash = (Hash << 13) ^ Hash;
		
		return (Hash & 0x007FFFFF) * (1.0f / 8388607.0f); 
	}
};
