// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "Debug/TCATDebugGridComponent.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneInterface.h"
#include "Engine/Engine.h"
#include "DynamicMeshBuilder.h"
#include "TCAT.h"
#include "Simulation/TCATGridResource.h"
#include "Core/TCATTypes.h"
#include "Scene/TCATInfluenceVolume.h"

using namespace TCATMapConstant;

// Default constructor for FTCATDebugGridUpdateParams
FTCATDebugGridUpdateParams::FTCATDebugGridUpdateParams()
	: DrawMode(ETCATDebugDrawMode::None)
{
}

static TAutoConsoleVariable<int32> CVarTCATDebugTextStride(
	TEXT("TCAT.Debug.TextStride"),
	8,
	TEXT("Step size for debug text rendering to improve performance."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarTCATDebugTextOffset(
	TEXT("TCAT.Debug.TextOffset"),
	50.0f,
	TEXT("Z-Offset for debug text to prevent clipping."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarTCATDebugTextSize(
	TEXT("TCAT.Debug.TextSize"),
	30.0f,
	TEXT("Character size for debug text rendering (world units)."),
	ECVF_Cheat);

/**
 * Scene proxy for batched debug grid rendering.
 * Renders all grid cells and text labels efficiently with batched draw calls.
 */
class FTCATDebugGridSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FTCATDebugGridSceneProxy(const UTCATDebugGridComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
		, PointSize(InComponent->GetPointSize())
		, TextCharSize(InComponent->GetTextCharSize())
	{
		bWillEverBeLit = false;
	}

	virtual ~FTCATDebugGridSceneProxy() = default;

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + GetAllocatedSize();
	}

	uint32 GetAllocatedSize() const
	{
		uint32 Size = 0;
		for (const FTCATDebugGridLayerData& Layer : RenderData.Layers)
		{
			Size += Layer.Cells.GetAllocatedSize();
			Size += Layer.TextLabels.GetAllocatedSize();
		}
		return Size;
	}

	/**
	 * Thread-safe update of render data from game thread.
	 */
	void UpdateRenderData_RenderThread(FTCATDebugGridRenderData&& InData)
	{
		RenderData = MoveTemp(InData);
	}

	/**
	 * Toggle layer visibility without full data rebuild.
	 */
	void SetLayerVisibility_RenderThread(FName LayerTag, bool bVisible)
	{
		for (FTCATDebugGridLayerData& Layer : RenderData.Layers)
		{
			if (Layer.LayerTag == LayerTag)
			{
				Layer.bVisible = bVisible;
				break;
			}
		}
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_TCATDebugGrid_GetDynamicMeshElements);

		// Early out if no data
		if (RenderData.Layers.Num() == 0)
		{
			return;
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (!(VisibilityMap & (1 << ViewIndex)))
			{
				continue;
			}

			const FSceneView* View = Views[ViewIndex];
			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

			if (!PDI)
			{
				continue;
			}

			const float BasePointSize = PointSize;

			for (const FTCATDebugGridLayerData& Layer : RenderData.Layers)
			{
				if (!Layer.bVisible)
				{
					continue;
				}

				// Draw points
				if (Layer.Cells.Num() > 0)
				{
					const float PointDrawSize = Layer.PointSize * BasePointSize;
					for (const FTCATDebugGridCellData& Cell : Layer.Cells)
					{
						PDI->DrawPoint(FVector(Cell.Position), Cell.Color, PointDrawSize, SDPG_World);
					}
				}

				// Draw text labels using line-based 7-segment digits
				if (Layer.TextLabels.Num() > 0)
				{
					RenderTextLabels(Layer.TextLabels, View, PDI);
				}
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = false;
		Result.bDynamicRelevance = true;
		Result.bStaticRelevance = false;
		Result.bRenderInMainPass = true;
		Result.bUsesLightingChannels = false;
		Result.bRenderCustomDepth = false;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return false;
	}

private:
	/**
	 * Render text labels using line-based 7-segment style digits.
	 * Uses PDI->DrawLine which is reliable and doesn't require materials.
	 */
	void RenderTextLabels(const TArray<FTCATDebugGridTextData>& TextLabels, const FSceneView* View, FPrimitiveDrawInterface* PDI) const
	{
		// Billboard orientation vectors
		const FVector CameraRight = View->GetViewRight();
		const FVector CameraUp = View->GetViewUp();

		const float CharWidth = TextCharSize * 0.5f;
		const float CharHeight = TextCharSize;
		const float LineThickness = FMath::Max(1.0f, TextCharSize * 0.1f);

		// 7-segment display pattern for each digit
		// Segments: top, top-right, bottom-right, bottom, bottom-left, top-left, middle
		//     0
		//    ---
		// 5 |   | 1
		//    ---  6
		// 4 |   | 2
		//    ---
		//     3
		static const uint8 DigitSegments[10] = {
			0b0111111,  // 0: all except middle
			0b0000110,  // 1: right side only
			0b1011011,  // 2: top, top-right, middle, bottom-left, bottom
			0b1001111,  // 3: top, right side, middle, bottom
			0b1100110,  // 4: top-left, top-right, middle, bottom-right
			0b1101101,  // 5: top, top-left, middle, bottom-right, bottom
			0b1111101,  // 6: all except top-right
			0b0000111,  // 7: top, right side
			0b1111111,  // 8: all segments
			0b1101111,  // 9: all except bottom-left
		};

		for (const FTCATDebugGridTextData& TextData : TextLabels)
		{
			const FColor TextColor = TextData.Color;

			// Format value to string
			TCHAR Buffer[32];
			FCString::Snprintf(Buffer, 32, TEXT("%.2f"), TextData.Value);

			int32 StrLen = FCString::Strlen(Buffer);
			float TotalWidth = StrLen * CharWidth * 1.2f;  // Add spacing
			float XOffset = -TotalWidth * 0.5f;

			FVector BasePos(TextData.Position);

			for (int32 i = 0; i < StrLen; i++)
			{
				TCHAR Char = Buffer[i];
				FVector CharPos = BasePos + CameraRight * XOffset;

				// Half dimensions for easier vertex calculation
				float HW = CharWidth * 0.4f;   // Half width
				float HH = CharHeight * 0.5f;  // Half height
				float QH = CharHeight * 0.25f; // Quarter height

				if (Char >= '0' && Char <= '9')
				{
					uint8 Segments = DigitSegments[Char - '0'];

					// Segment 0: Top horizontal
					if (Segments & 0b0000001)
					{
						FVector Start = CharPos + CameraRight * (-HW) + CameraUp * HH;
						FVector End = CharPos + CameraRight * HW + CameraUp * HH;
						PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
					}
					// Segment 1: Top-right vertical
					if (Segments & 0b0000010)
					{
						FVector Start = CharPos + CameraRight * HW + CameraUp * HH;
						FVector End = CharPos + CameraRight * HW;
						PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
					}
					// Segment 2: Bottom-right vertical
					if (Segments & 0b0000100)
					{
						FVector Start = CharPos + CameraRight * HW;
						FVector End = CharPos + CameraRight * HW + CameraUp * (-HH);
						PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
					}
					// Segment 3: Bottom horizontal
					if (Segments & 0b0001000)
					{
						FVector Start = CharPos + CameraRight * (-HW) + CameraUp * (-HH);
						FVector End = CharPos + CameraRight * HW + CameraUp * (-HH);
						PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
					}
					// Segment 4: Bottom-left vertical
					if (Segments & 0b0010000)
					{
						FVector Start = CharPos + CameraRight * (-HW) + CameraUp * (-HH);
						FVector End = CharPos + CameraRight * (-HW);
						PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
					}
					// Segment 5: Top-left vertical
					if (Segments & 0b0100000)
					{
						FVector Start = CharPos + CameraRight * (-HW);
						FVector End = CharPos + CameraRight * (-HW) + CameraUp * HH;
						PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
					}
					// Segment 6: Middle horizontal
					if (Segments & 0b1000000)
					{
						FVector Start = CharPos + CameraRight * (-HW);
						FVector End = CharPos + CameraRight * HW;
						PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
					}
				}
				else if (Char == '.')
				{
					// Draw a small square for decimal point using lines
					float DotSize = HW * 0.3f;
					FVector DotCenter = CharPos + CameraUp * (-HH);
					FVector BL = DotCenter + CameraRight * (-DotSize) + CameraUp * (-DotSize);
					FVector BR = DotCenter + CameraRight * DotSize + CameraUp * (-DotSize);
					FVector TR = DotCenter + CameraRight * DotSize + CameraUp * DotSize;
					FVector TL = DotCenter + CameraRight * (-DotSize) + CameraUp * DotSize;
					PDI->DrawLine(BL, BR, TextColor, SDPG_Foreground, LineThickness);
					PDI->DrawLine(BR, TR, TextColor, SDPG_Foreground, LineThickness);
					PDI->DrawLine(TR, TL, TextColor, SDPG_Foreground, LineThickness);
					PDI->DrawLine(TL, BL, TextColor, SDPG_Foreground, LineThickness);
				}
				else if (Char == '-')
				{
					// Draw horizontal line in middle
					FVector Start = CharPos + CameraRight * (-HW);
					FVector End = CharPos + CameraRight * HW;
					PDI->DrawLine(Start, End, TextColor, SDPG_Foreground, LineThickness);
				}
				else if (Char == '+')
				{
					// Draw cross
					FVector HStart = CharPos + CameraRight * (-HW);
					FVector HEnd = CharPos + CameraRight * HW;
					FVector VStart = CharPos + CameraUp * (-QH);
					FVector VEnd = CharPos + CameraUp * QH;
					PDI->DrawLine(HStart, HEnd, TextColor, SDPG_Foreground, LineThickness);
					PDI->DrawLine(VStart, VEnd, TextColor, SDPG_Foreground, LineThickness);
				}

				XOffset += CharWidth * 1.2f;
			}
		}
	}

	FTCATDebugGridRenderData RenderData;
	float PointSize;
	float TextCharSize;
};

//////////////////////////////////////////////////////////////////////////
// UTCATDebugGridComponent

UTCATDebugGridComponent::UTCATDebugGridComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);

	// Use parent's bounds - this prevents the component from affecting GetComponentsBoundingBox()
	// while still having valid bounds for frustum culling
	bUseAttachParentBound = true;

	// Ensure visibility
	SetVisibility(true);
	SetHiddenInGame(false);
	bVisibleInReflectionCaptures = false;
	bVisibleInRayTracing = false;

	// Initialize with invalid bounds
	GridBounds = FBox(ForceInit);
}

FPrimitiveSceneProxy* UTCATDebugGridComponent::CreateSceneProxy()
{
	if (CurrentRenderData.Layers.Num() == 0)
	{
		return nullptr;
	}

	// Update text size from CVar
	TextCharSize = FMath::Max(1.0f, CVarTCATDebugTextSize.GetValueOnGameThread());

	FTCATDebugGridSceneProxy* Proxy = new FTCATDebugGridSceneProxy(this);

	// Transfer current data to proxy
	FTCATDebugGridRenderData DataCopy = CurrentRenderData;
	ENQUEUE_RENDER_COMMAND(TCATDebugGridInitData)(
		[Proxy, Data = MoveTemp(DataCopy)](FRHICommandListImmediate& RHICmdList) mutable
		{
			Proxy->UpdateRenderData_RenderThread(MoveTemp(Data));
		});

	return Proxy;
}

FBoxSphereBounds UTCATDebugGridComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// With bUseAttachParentBound=true, this is only called as fallback
	// Return GridBounds if valid, otherwise minimal bounds
	if (GridBounds.IsValid)
	{
		return FBoxSphereBounds(GridBounds);
	}

	// Minimal bounds - won't affect parent's GetComponentsBoundingBox much
	return FBoxSphereBounds(FSphere(LocalToWorld.GetLocation(), 1.0f));
}

void UTCATDebugGridComponent::UpdateGridData(FTCATDebugGridRenderData&& InRenderData)
{
	// Store game thread copy
	CurrentRenderData = MoveTemp(InRenderData);
	DataVersion++;

	int32 TotalCells = 0;
	for (const FTCATDebugGridLayerData& Layer : CurrentRenderData.Layers)
	{
		TotalCells += Layer.Cells.Num();
	}

	// Transfer to render thread
	if (SceneProxy)
	{
		FTCATDebugGridSceneProxy* DebugProxy = static_cast<FTCATDebugGridSceneProxy*>(SceneProxy);
		FTCATDebugGridRenderData DataCopy = CurrentRenderData;

		ENQUEUE_RENDER_COMMAND(TCATDebugGridUpdate)(
			[DebugProxy, Data = MoveTemp(DataCopy)](FRHICommandListImmediate& RHICmdList) mutable
			{
				DebugProxy->UpdateRenderData_RenderThread(MoveTemp(Data));
			});
	}
	else if (CurrentRenderData.Layers.Num() > 0)
	{
		// Force proxy recreation if we have data but no proxy
		MarkRenderStateDirty();
	}
}

void UTCATDebugGridComponent::SetLayerVisibility(FName LayerTag, bool bInVisible)
{
	// Update game thread copy
	for (FTCATDebugGridLayerData& Layer : CurrentRenderData.Layers)
	{
		if (Layer.LayerTag == LayerTag)
		{
			Layer.bVisible = bInVisible;
			break;
		}
	}

	// Update render thread
	if (SceneProxy)
	{
		FTCATDebugGridSceneProxy* DebugProxy = static_cast<FTCATDebugGridSceneProxy*>(SceneProxy);

		ENQUEUE_RENDER_COMMAND(TCATDebugGridVisibility)(
			[DebugProxy, LayerTag, bInVisible](FRHICommandListImmediate& RHICmdList)
			{
				DebugProxy->SetLayerVisibility_RenderThread(LayerTag, bInVisible);
			});
	}
}

void UTCATDebugGridComponent::SetGridBounds(const FBox& InBounds)
{
	GridBounds = InBounds;
	UpdateBounds();
}

void UTCATDebugGridComponent::UpdateFromVolumeData(const FTCATDebugGridUpdateParams& Params)
{
	// Handle None mode - clear the visualization
	if (Params.DrawMode == ETCATDebugDrawMode::None)
	{
		FTCATDebugGridRenderData EmptyData;
		UpdateGridData(MoveTemp(EmptyData));
		return;
	}

	// Validate required data
	if (!Params.InfluenceLayers || !Params.DebugSettings || Params.Resolution.X <= 0 || Params.Resolution.Y <= 0)
	{
		return;
	}

	const int32 TextStride = FMath::Max(1, CVarTCATDebugTextStride.GetValueOnGameThread());
	const float TextOffset = CVarTCATDebugTextOffset.GetValueOnGameThread();

	// Build render data
	FTCATDebugGridRenderData RenderData;
	RenderData.Layers.Reserve(Params.InfluenceLayers->Num());

	const float MinX = Params.Bounds.Min.X;
	const float MinY = Params.Bounds.Min.Y;
	const float PointSizeBase = (Params.CellSize * CELL_CENTER_OFFSET) * 0.9f;

	for (const auto& Pair : *Params.InfluenceLayers)
	{
		const FName& Tag = Pair.Key;
		const FTCATGridResource& Resource = Pair.Value;

		const FTCATLayerDebugSettings* Settings = Params.DebugSettings->Find(Tag);

		// If no settings found, skip
		if (!Settings) continue;

		// Visibility Check
		const bool bLayerVisible = (Params.DrawMode == ETCATDebugDrawMode::All) || Settings->bVisible;
		if (!bLayerVisible) continue;

		if (Resource.Grid.Num() == 0) continue;

		const FLinearColor& PosColor = Settings->PositiveColor;
		const FLinearColor& NegColor = Settings->NegativeColor;
		const FLinearColor& ZeroColor = Settings->ZeroColor;

		// Create layer data
		FTCATDebugGridLayerData LayerData;
		LayerData.LayerTag = Tag;
		LayerData.bVisible = bLayerVisible;
		LayerData.PointSize = PointSizeBase;

		// Estimate capacity (rough estimate to reduce reallocations)
		const int32 TotalCells = Params.Resolution.X * Params.Resolution.Y;
		LayerData.Cells.Reserve(FMath::Min(TotalCells, 4096));  // Cap to avoid over-allocation
		if (Params.bDrawText)
		{
			LayerData.TextLabels.Reserve(FMath::Min(TotalCells / (TextStride * TextStride), 1024));
		}

		// Build cell data
		for (int32 Y = 0; Y < Params.Resolution.Y; ++Y)
		{
			for (int32 X = 0; X < Params.Resolution.X; ++X)
			{
				const int32 Index = Y * Params.Resolution.X + X;
				if (!Resource.Grid.IsValidIndex(Index)) continue;

				const float Value = Resource.Grid[Index];
				if (FMath::Abs(Value) < KINDA_SMALL_NUMBER) continue;

				FLinearColor FinalColor;
				if (Value > 0.0f)
				{
					float Alpha = FMath::Clamp(Value, 0.0f, 1.0f);
					FinalColor = FLinearColor::LerpUsingHSV(ZeroColor, PosColor, Alpha);
				}
				else
				{
					float Alpha = FMath::Clamp(FMath::Abs(Value), 0.0f, 1.0f);
					FinalColor = FLinearColor::LerpUsingHSV(ZeroColor, NegColor, Alpha);
				}

				// Height Logic
				float DrawZ = Params.GridOriginZ;
				if (Params.HeightGrid && Params.HeightGrid->IsValidIndex(Index))
				{
					DrawZ = (*Params.HeightGrid)[Index];
				}
				DrawZ += Settings->HeightOffset;

				FVector3f Center(MinX + (X + CELL_CENTER_OFFSET) * Params.CellSize, MinY + (Y + CELL_CENTER_OFFSET) * Params.CellSize, DrawZ);
				const FColor CellColor = FinalColor.ToFColor(true);
				LayerData.Cells.Emplace(Center, CellColor);

				if (Settings->bVisible && (X % TextStride == 0) && (Y % TextStride == 0) && Params.bDrawText)
				{
					FVector3f TextPos(Center.X, Center.Y, Center.Z + TextOffset);
					LayerData.TextLabels.Emplace(TextPos, Value, Params.TextColor);
				}
			}
		}

		if (LayerData.Cells.Num() > 0)
		{
			RenderData.Layers.Add(MoveTemp(LayerData));
		}
	}

	// Update bounds and render data
	SetGridBounds(Params.Bounds);
	UpdateGridData(MoveTemp(RenderData));
}
