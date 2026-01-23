// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Simulation/TCATGridResource.h"
#include "Engine/TextureRenderTarget2D.h"

int32 FTCATGridResource::GetIndex(int32 X, int32 Y) const
{
	return (Y * Columns) + X;
}

void FTCATGridResource::Resize(int32 InRows, int32 InCols, UObject* Outer, FName ResourceDebugName)
{
	if (Rows == InRows && Columns == InCols && RenderTarget != nullptr)
	{
		return;
	}

	Rows = InRows;
	Columns = InCols;

	const int32 TotalCells = Rows * Columns;
	Grid.SetNumZeroed(TotalCells);

	// 1. for async
	AsyncRingBuffer.Initialize(Outer, Columns, Rows, ResourceDebugName);

	// 2. for sync
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(Outer);
		check(RenderTarget);
	}

	// Configure for high-precision influence data (R32 Float)
	RenderTarget->bCanCreateUAV = true;
	RenderTarget->RenderTargetFormat = RTF_R32f;
	RenderTarget->ClearColor = FLinearColor::Black;
    
	// Resize the actual texture resource
	RenderTarget->InitAutoFormat(Columns, Rows);
	RenderTarget->UpdateResourceImmediate(true);
}

void FTCATGridResource::Release()
{
	if (RenderTarget)
	{
		RenderTarget = nullptr;
	}

	AsyncRingBuffer.Release();

	Grid.Empty();
	Rows = 0;
	Columns = 0;
}