// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Scene/TCATHeightMapModule.h"
#include "DrawDebugHelpers.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "TextureResource.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Volume.h"
#include "GameFramework/Pawn.h"
#include "Scene/TCATInfluenceVolume.h"
#include "Simulation/TCATGridResource.h"
#include "Engine/World.h"
#include "Async/ParallelFor.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Core/TCATSettings.h"

using namespace TCATMapConstant;

void FTCATHeightMapModule::Bake(ATCATInfluenceVolume* Owner, float CellSize, FIntPoint Resolution)
{
	if (!Owner)
	{
		return;
	}

	const FBox Bounds = Owner->GetComponentsBoundingBox(true);
	if (!Bounds.IsValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("HeightMapModule: Invalid bounds for volume %s"), *Owner->GetName());
		return;
	}

	// Fill CPU Grid : Get reference to owner's HeightResource 
	FTCATHeightMapResource& HeightResource = Owner->HeightResource;
	HeightResource.Resize(Resolution.Y, Resolution.X, Owner, TEXT("HeightBake"));

	PerformLineTraces(Owner, Bounds, CellSize, Resolution, HeightResource);

	if (UTextureRenderTarget2D* RenderTarget = HeightResource.RenderTarget)
	{
		if (FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource())
		{
			const FTextureRHIRef TextureRHI = RTResource->GetTextureRHI();
			if (TextureRHI.IsValid())
			{
				TArray<float> PixelData = HeightResource.Grid;
				const int32 Width = Resolution.X;
				const int32 Height = Resolution.Y;

				ENQUEUE_RENDER_COMMAND(UpdateLocalHeightMap)(
					[TextureRHI, PixelData = MoveTemp(PixelData), Width, Height](FRHICommandListImmediate& RHICmdList)
					{
						FUpdateTextureRegion2D Region(0, 0, 0, 0, Width, Height);
						const uint32 Pitch = Width * sizeof(float);
						RHICmdList.UpdateTexture2D(
							TextureRHI,
							0,
							Region,
							Pitch,
							reinterpret_cast<const uint8*>(PixelData.GetData())
						);
					});
			}
		}
		
	}
	UE_LOG(LogTemp, Log, TEXT("HeightMapModule: Baked %dx%d height map for %s"), 
		Resolution.X, Resolution.Y, *Owner->GetName());
}

void FTCATHeightMapModule::DrawDebug(const ATCATInfluenceVolume* Owner) const
{
	if (!bDrawHeight || !Owner)
	{
		return;
	}

	const FTCATGridResource& HeightResource = Owner->HeightResource;
	if (HeightResource.Grid.Num() == 0)
	{
		return;
	}

	const FBox Bounds = Owner->GetComponentsBoundingBox(true);
	const float CellSize = Owner->GetCellSize();

	DrawHeightDebugPoints(Owner, HeightResource, Bounds, CellSize);
}

void FTCATHeightMapModule::FlushAndRedraw(const ATCATInfluenceVolume* Owner)
{
	if (!Owner)
	{
		return;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	FlushPersistentDebugLines(World);
	DrawDebug(Owner);
}


void FTCATHeightMapModule::PerformLineTraces(ATCATInfluenceVolume* Owner, const FBox& Bounds,
	float CellSize, FIntPoint Resolution, FTCATGridResource& OutResource)
{
	const UTCATSettings* Settings = GetDefault<UTCATSettings>();
	FCollisionObjectQueryParams ObjectParams;

	if (Settings && Settings->HeightMapTraceChannels.Num() > 0)
	{
		for (const auto& Channel : Settings->HeightMapTraceChannels)
		{
			ObjectParams.AddObjectTypesToQuery(Channel);	
		}
	}
	else
	{
		ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	}
	
	const FCollisionQueryParams BaseParams(SCENE_QUERY_STAT(TCAT_HeightBake), false, Owner);
	
	const float ZStart = Bounds.Max.Z + TRACE_OFFSET_UP;
	const float ZEnd = Bounds.Min.Z - TRACE_OFFSET_DOWN;

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	// Parallel trace from each grid cell
	ParallelFor(Resolution.Y * Resolution.X, [&](int32 Index)
	{
		const int32 X = Index % Resolution.X;
		const int32 Y = Index / Resolution.X;

		const float WorldX = Bounds.Min.X + (X * CellSize) + (CellSize * CELL_CENTER_OFFSET);
		const float WorldY = Bounds.Min.Y + (Y * CellSize) + (CellSize * CELL_CENTER_OFFSET);
		
		FVector TraceStart(WorldX, WorldY, ZStart);
		const FVector TraceEnd(WorldX, WorldY, ZEnd);

		FCollisionQueryParams TraceParams = BaseParams;

		float FinalHeight = Bounds.Min.Z;

		int32 PenetrationCount = 0;
		const int32 MaxPenetrations = 10;

		while (PenetrationCount < MaxPenetrations)
		{
			FHitResult HitResult;
			const bool bHit = World->LineTraceSingleByObjectType(
				HitResult,
				TraceStart,
				TraceEnd,
				ObjectParams,
				TraceParams
			);

			if (!bHit) break;

			const AActor* HitActor = HitResult.GetActor();
			if (!HitActor) 
			{
				break;
			}

			bool bShouldIgnore = HitActor->IsA<APawn>();
			if (!bShouldIgnore && HitActor->ActorHasTag(FName("TCAT_IgnoreBake")))
			{
				bShouldIgnore = true;
			}

			if (bShouldIgnore)
			{
				TraceStart = HitResult.Location + (FVector::DownVector * 2.0f);
				TraceParams.AddIgnoredActor(HitActor);
				PenetrationCount++;
				continue;
			}
			
			FinalHeight = HitResult.ImpactPoint.Z;
			break;
		}
		
		OutResource.Grid[Index] = FinalHeight;
	});
}

void FTCATHeightMapModule::DrawHeightDebugPoints(const ATCATInfluenceVolume* Owner,
	const FTCATGridResource& Resource, const FBox& Bounds, float CellSize) const
{
	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	for (int32 Y = 0; Y < Resource.Rows; ++Y)
	{
		for (int32 X = 0; X < Resource.Columns; ++X)
		{
			const int32 Index = Resource.GetIndex(X, Y);
			if (!Resource.Grid.IsValidIndex(Index))
			{
				continue;
			}
			
			const float CurrentHeight = Resource.Grid[Index];
			const float MaxCliffHeight = CalculateCliffHeight(Resource, X, Y);
			
			// Red for steep slopes (walls), Green for flat terrain
			const FColor DebugColor = (MaxCliffHeight > HeightToMark) 
				? FColor::Red 
				: FColor::Green;

			const FVector Position(
				Bounds.Min.X + (X + CELL_CENTER_OFFSET) * CellSize,
				Bounds.Min.Y + (Y + CELL_CENTER_OFFSET) * CellSize,
				CurrentHeight + DEBUG_HEIGHT_OFFSET
			);
			
			DrawDebugPoint(
				World,
				Position,
				DEBUG_POINT_SIZE,
				DebugColor,
				true,  // Persistent
				-1.0f  // Infinite duration
			);
		}
	}
}

float FTCATHeightMapModule::CalculateCliffHeight(const FTCATGridResource& Resource, int32 X, int32 Y) const
{
	const int32 CenterIndex = Resource.GetIndex(X, Y);
	if (!Resource.Grid.IsValidIndex(CenterIndex))
	{
		return 0.0f;
	}

	const float CenterHeight = Resource.Grid[CenterIndex];
	float MaxHalfHeight = 0.0f;

	for (int32 i = 0; i < NUM_NEIGHBOR_OFFSETS; ++i)
	{
		const int32 NeighborX = X + NeighborOffsets[i][0];
		const int32 NeighborY = Y + NeighborOffsets[i][1];
		
		// Bounds check
		if (NeighborX < 0 || NeighborX >= Resource.Columns ||
			NeighborY < 0 || NeighborY >= Resource.Rows)
		{
			continue;
		}

		const int32 NeighborIndex = Resource.GetIndex(NeighborX, NeighborY);
		if (!Resource.Grid.IsValidIndex(NeighborIndex))
		{
			continue;
		}

		const float NeighborHeight = Resource.Grid[NeighborIndex];
		const float Difference = FMath::Abs(CenterHeight - NeighborHeight);
		
		MaxHalfHeight = FMath::Max(MaxHalfHeight, Difference);
	}

	return MaxHalfHeight;
}