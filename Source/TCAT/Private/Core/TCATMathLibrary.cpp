// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Core/TCATMathLibrary.h"

FIntPoint UTCATMathLibrary::WorldToGrid(const FVector& WorldLocation, const FVector& MapStartLocation, float GridSize, int32 MapWidth, int32 MapHeight)
{
	FVector2D RelativePos2D = FVector2D(WorldLocation.X - MapStartLocation.X, WorldLocation.Y - MapStartLocation.Y);

	int32 X = FMath::FloorToInt(RelativePos2D.X / GridSize);
	int32 Y = FMath::FloorToInt(RelativePos2D.Y / GridSize);
	
	X = FMath::Clamp(X, 0, MapWidth - 1);
	Y = FMath::Clamp(Y, 0, MapHeight - 1);

	return FIntPoint(X, Y);
}

FVector UTCATMathLibrary::GridToWorld(const FIntPoint& GridIndex, const FVector& MapStartLocation, float GridSize)
{
	float X = MapStartLocation.X + (GridIndex.X * GridSize) + (GridSize * 0.5f);
	float Y = MapStartLocation.Y + (GridIndex.Y * GridSize) + (GridSize * 0.5f);

	return FVector(X, Y, MapStartLocation.Z);
}

void UTCATMathLibrary::BuildCurveAtlasData(const TArray<UCurveFloat*>& InUniqueCurves, int32 TextureWidth, TArray<float>& OutAtlasData)
{
	OutAtlasData.Empty();
	if (TextureWidth <= 0) return;
	
	int32 TotalPixels = InUniqueCurves.Num() * TextureWidth;
	OutAtlasData.Reserve(TotalPixels);
	
	for (UCurveFloat* Curve : InUniqueCurves)
	{
		for (int32 Col = 0; Col < TextureWidth; Col++)
		{
			const float Denom = FMath::Max(TextureWidth - 1, 1);
			float Time = (float)Col / (float)Denom;

			float Value = 0.0f;

			if (Curve)
			{
				Value = Curve->GetFloatValue(Time);
			}
			else
			{
				Value = 1.0f - Time; 
			}

			OutAtlasData.Add(Value);
		}
	}
}

bool UTCATMathLibrary::CompareFloat(float A, float B, ETCATCompareType Condition)
{
	switch (Condition)
	{
	case ETCATCompareType::Greater:
		return A > B;

	case ETCATCompareType::GreaterOrEqual:
		return A >= B;

	case ETCATCompareType::Less:
		return A < B;

	case ETCATCompareType::LessOrEqual:
		return A <= B;

	case ETCATCompareType::Equal:
		return FMath::IsNearlyEqual(A, B);

	case ETCATCompareType::NotEqual:
		return !FMath::IsNearlyEqual(A, B);

	default:
		checkf(false, TEXT("Unhandled ETCATCompareType in UTCATMathLibrary::CompareFloat"));
		return false;
	}
}
