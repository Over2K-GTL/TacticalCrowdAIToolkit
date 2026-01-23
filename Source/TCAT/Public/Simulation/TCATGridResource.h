// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIGPUReadback.h"
#include "TCATAsyncResourceRingBuffer.h"
#include "TCATGridResource.generated.h"

class UTextureRenderTarget2D;

USTRUCT(BlueprintType)
struct FTCATGridResource
{
	GENERATED_BODY()
	
	FTCATGridResource() : Rows(0), Columns(0), MinMapValue(0), MaxMapValue(0), RenderTarget(nullptr)
	{
	}

	TArray<float> Grid;
	int32 Rows, Columns;
	float MinMapValue, MaxMapValue; 
	
	// synchronous rsource
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "TCAT")
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(VisibleInstanceOnly, Category = "TCAT")
	// Asynchronous rsource ring buffer
	FTCATAsyncResourceRingBuffer AsyncRingBuffer;

	uint64 LastRequestFrame = 0;

	int32 GetIndex(int32 X, int32 Y) const;
	void Resize(int32 InRows, int32 InCols, UObject* Outer, FName ResourceDebugName = NAME_None);
	void Release();
};

