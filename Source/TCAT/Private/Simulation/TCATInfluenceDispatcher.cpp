// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Simulation/TCATInfluenceDispatcher.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "TCAT.h"
#include "Simulation/Shaders/TCATCompositeCS.h"
#include "Simulation/Shaders/TCATFindMinMaxCS.h"
#include "Simulation/Shaders/TCATInfluenceCS.h"
#include "Async/ParallelFor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHIStaticStates.h"

DECLARE_CYCLE_STAT(TEXT("Influence_GPU_Dispatch"), STAT_TCAT_GPU_Dispatch, STATGROUP_TCAT);
DECLARE_CYCLE_STAT(TEXT("Influence_CPU_Total"), STAT_TCAT_CPU_Total, STATGROUP_TCAT);
DECLARE_CYCLE_STAT(TEXT("Influence_Readback_Wait"), STAT_TCAT_Readback_Wait, STATGROUP_TCAT);

void FTCATInfluenceDispatcher::DispatchGPU_Batched(
	FRHICommandListImmediate& RHICmdList,
	TArray<FTCATInfluenceDispatchParams>&& InfluenceBatch,
	TArray<FTCATCompositeDispatchParams>&& CompositeBatch)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_Dispatch_Total)
    FRDGBuilder GraphBuilder(RHICmdList);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_BuildRDG);
		RDG_EVENT_SCOPE(GraphBuilder, "TCAT_Influence_Update_Group");
		RDG_GPU_STAT_SCOPE(GraphBuilder, TCAT_Influence_Update_Stats);

		TMap<FName, FRDGTextureRef> GlobalRDGTextureMap;
		
		// Process base influence volumes FIRST
    	for (const FTCATInfluenceDispatchParams& Params : InfluenceBatch)
    	{
    		if (!Params.OutInfluenceMapRHI.IsValid())
    		{
    			UE_LOG(LogTCAT, Warning, TEXT("TCATDispatcher, '%s': InfluenceMapRHI is NOT Valid! Skipping..."), *Params.VolumeName);
    			continue;
    		}

    		FString DebugName = FString::Printf(TEXT("TCAT_Out_%s"), *Params.VolumeName);
    		FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(
			  CreateRenderTarget(Params.OutInfluenceMapRHI, *DebugName)
			  );
    		FRDGTextureUAVRef OutputUAV = GraphBuilder.CreateUAV(OutputTexture);

    		FString LayerTagName;
    		if (Params.VolumeName.Split(TEXT("_"), nullptr, &LayerTagName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
    		{
    			if (!LayerTagName.IsEmpty())
    			{
    				GlobalRDGTextureMap.Add(FName(*LayerTagName), OutputTexture);
    			}
    		}

    		const int32 SourceCount = Params.Sources.Num();
    		FTCATInfluenceSource DummySource;
    		FMemory::Memzero(DummySource);
    		const void* SourceData = (SourceCount > 0) ? Params.Sources.GetData() : &DummySource;
    		const int32 UploadCount = FMath::Max(SourceCount, 1);
	        
    		FRDGBufferRef SourceBuffer = CreateStructuredBuffer(
				GraphBuilder, TEXT("TCAT_SourceBuffer"), 
				sizeof(FTCATInfluenceSource), UploadCount, 
				SourceData, sizeof(FTCATInfluenceSource) * UploadCount
			);

    		FRDGTextureRef CurveAtlasTexture = nullptr;
    		if (Params.GlobalCurveAtlasRHI.IsValid())
    		{
    			CurveAtlasTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Params.GlobalCurveAtlasRHI, TEXT("TCAT_CurveAtlas")));
    		}
    		else
    		{
    			FRDGTextureDesc FallbackDesc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable);
    			CurveAtlasTexture = GraphBuilder.CreateTexture(FallbackDesc, TEXT("TCAT_CurveAtlas_Fallback"));
    			AddClearRenderTargetPass(GraphBuilder, CurveAtlasTexture, FLinearColor::Black);
    		}
    		FRDGTextureRef GlobalHeightMapTexture = nullptr;
    		if (Params.GlobalHeightMapRHI.IsValid())
    		{
    			GlobalHeightMapTexture = GraphBuilder.RegisterExternalTexture(
					CreateRenderTarget(Params.GlobalHeightMapRHI, TEXT("TCAT_GlobalHeightMapRT"))
				);
    		}
    		else
    		{
    			FRDGTextureDesc HeightFallbackDesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_R32_FLOAT, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_RenderTargetable);
    			GlobalHeightMapTexture = GraphBuilder.CreateTexture(HeightFallbackDesc, TEXT("TCAT_GlobalHeightMap_Fallback"));
    			AddClearRenderTargetPass(GraphBuilder, GlobalHeightMapTexture, FLinearColor::Black);
    		}

    		
    		FVector2f SafeGlobalHeightSize(
				FMath::Max(Params.GlobalHeightMapSize.X, KINDA_SMALL_NUMBER),
				FMath::Max(Params.GlobalHeightMapSize.Y, KINDA_SMALL_NUMBER));
    		
    		FTCATInfluenceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTCATInfluenceCS::FParameters>();
    		PassParameters->CurveAtlasTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(CurveAtlasTexture));
    		PassParameters->CurveAtlasSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    		PassParameters->GlobalHeightMap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(GlobalHeightMapTexture));
    		PassParameters->GlobalHeightMapSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    		PassParameters->GlobalHeightMapOrigin = Params.GlobalHeightMapOrigin;
    		PassParameters->GlobalHeightMapSize = SafeGlobalHeightSize;

    		PassParameters->RayMarchMaxSteps = Params.RayMarchMaxSteps;
    		PassParameters->RayMarchStepSize = Params.RayMarchStepSize;

    		PassParameters->InSources = GraphBuilder.CreateSRV(SourceBuffer);
    		PassParameters->OutInfluenceMap = OutputUAV;

        PassParameters->MapStartPos = FVector3f(Params.MapStartPos);
        PassParameters->SourceCount = SourceCount;
        PassParameters->RayMarchStepSize = Params.RayMarchStepSize;
        PassParameters->RayMarchMaxSteps = static_cast<uint32>(FMath::Max(Params.RayMarchMaxSteps, 1));
        
        PassParameters->MapSize = Params.MapSize;
        PassParameters->GridSize = Params.GridSize;
        PassParameters->ProjectionFlags = Params.ProjectionFlags;

    		TShaderMapRef<FTCATInfluenceCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    		FIntVector GroupCount(
				FMath::DivideAndRoundUp<int32>(Params.MapSize.X, 8),
				FMath::DivideAndRoundUp<int32>(Params.MapSize.Y, 8),
				1
			);

    		FComputeShaderUtils::AddPass(
				GraphBuilder, RDG_EVENT_NAME("TCAT_UpdateInfluenceMap_%s", *Params.VolumeName),
				ComputeShader, PassParameters, GroupCount
			);

    		if (Params.bIsAsync && Params.GPUReadback)
    		{
    			AddEnqueueCopyPass(GraphBuilder, Params.GPUReadback, OutputTexture);
    		}
    	}

		// Create a single dummy MinMax texture for composite map operations when inputB is not normalized
		FRDGTextureSRVRef DummyMinMaxSRV = nullptr;
				
		auto GetDummyMinMaxSRV = [&]() -> FRDGTextureSRVRef
		{
			if (DummyMinMaxSRV)
			{
				return DummyMinMaxSRV;
			}

			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				FIntPoint(1, 1),
				PF_G32R32F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV
			);

			FRDGTextureRef DummyMinMaxTex = GraphBuilder.CreateTexture(Desc, TEXT("TCAT_MinMax_Dummy"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DummyMinMaxTex), FVector4f(0, 0, 0, 0));
			DummyMinMaxSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(DummyMinMaxTex));
			return DummyMinMaxSRV;
		};
		
		// Process composite volumes
		for (const FTCATCompositeDispatchParams& Params : CompositeBatch)
		{
			if (Params.Operations.Num() == 0)
			{
				continue;
			}

			if (!Params.OutInfluenceMapRHI.IsValid())
			{
				UE_LOG(LogTCAT, Warning, TEXT("Composite Volume `%s` Output RHI is Invalid! Skipping."), *Params.VolumeName);
				continue;
			}

			TMap<FName, FRDGTextureRef> RDGInputTextures;
			for (auto& Pair : Params.InputTextureMap)
			{
				RDGInputTextures.Add(Pair.Key, GraphBuilder.RegisterExternalTexture(
					CreateRenderTarget(Pair.Value, *FString::Printf(TEXT("TCAT_Input_%s"), *Pair.Key.ToString()))
				));
			}
			
			// Register output texture
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(Params.OutInfluenceMapRHI, TEXT("TCAT_CompositeMapRT"))
			);

			// Create transient intermediate texture for fast operation chaining
			// Using transient RDG texture avoids UAV barriers on external textures
			FRDGTextureDesc IntermediateDesc = FRDGTextureDesc::Create2D(
				FIntPoint(Params.MapSize.X, Params.MapSize.Y),
				PF_R32_FLOAT,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV
			);
			FRDGTextureRef PingTex = GraphBuilder.CreateTexture(IntermediateDesc, TEXT("TCAT_Ping"));
			FRDGTextureRef PongTex = GraphBuilder.CreateTexture(IntermediateDesc, TEXT("TCAT_Pong"));

			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PingTex), 0.0f);

			FRDGTextureRef SrcTex = PingTex;
			FRDGTextureRef DstTex = PongTex;
			
			TShaderMapRef<FTCATCompositeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			
			for (int32 OpIdx = 0; OpIdx < Params.Operations.Num(); ++OpIdx)
			{
				const FTCATCompositeOperation& Op = Params.Operations[OpIdx];

				// === Composite operation setup ===
				FRDGTextureSRVRef InputASRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SrcTex));

				FRDGTextureSRVRef InputBSRV = nullptr;

				// Allocate shader parameters
				FTCATCompositeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTCATCompositeCS::FParameters>();
				PassParameters->NormalizeMinMaxTexture = GetDummyMinMaxSRV();
				
				// Bind InputMapB only for binary operations
				FRDGTextureRef InputBTexture = nullptr;  // Track the actual texture for min/max calculation

				// Handle Normalize (unary operation on accumulator)
				if (Op.Operation == ETCATCompositeOp::Normalize)
				{
					// Run min/max reduction on the ACCUMULATOR (SrcTex)
					PassParameters->NormalizeMinMaxTexture = DispatchMinMaxReduction(
						GraphBuilder, SrcTex, Params.MapSize,
						FString::Printf(TEXT("%s_Norm_Op%d"), *Params.VolumeName, OpIdx));

					// InputB is not used for this unary op, but shader requires a valid SRV
					InputBSRV = InputASRV;
				}
				// Handle binary operations (Add, Subtract, Multiply, Divide)
				else if (Op.Operation == ETCATCompositeOp::Add ||
				         Op.Operation == ETCATCompositeOp::Subtract ||
				         Op.Operation == ETCATCompositeOp::Multiply ||
				         Op.Operation == ETCATCompositeOp::Divide)
				{
					FName TargetTagB = Op.InputLayerTag;

					if (FRDGTextureRef* LiveTexture = GlobalRDGTextureMap.Find(TargetTagB))
					{
						InputBTexture = *LiveTexture;
						InputBSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(*LiveTexture));
					}
					else if (FRDGTextureRef* FoundTex = RDGInputTextures.Find(TargetTagB))
					{
						InputBTexture = *FoundTex;
						InputBSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(*FoundTex));
					}

					// Run min/max reduction if this is a Normalize operation on InputB
					if (Op.bNormalizeInput && InputBTexture)
					{
						PassParameters->NormalizeMinMaxTexture = DispatchMinMaxReduction(
							GraphBuilder, InputBTexture, Params.MapSize,
							FString::Printf(TEXT("%s_Op%d"), *Params.VolumeName, OpIdx));
					}
				}

				// Default to InputA if InputB not found (prevents shader errors)
				if (!InputBSRV) InputBSRV = InputASRV;

				// Use Dummy MinMax if operation doesn't need normalization
				// (binary ops without bNormalizeInput, Invert)
				if (Op.Operation != ETCATCompositeOp::Normalize && !Op.bNormalizeInput)
				{
					PassParameters->NormalizeMinMaxTexture = GetDummyMinMaxSRV();
				}
				
				PassParameters->InputMapA = InputASRV;
				PassParameters->InputMapB = InputBSRV;
				PassParameters->OutputMap = GraphBuilder.CreateUAV(DstTex);

				PassParameters->PointSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PassParameters->MapSize = Params.MapSize;
				PassParameters->OperationType = static_cast<uint32>(Op.Operation);
				PassParameters->StrengthParam = Op.Strength;
				PassParameters->bClampInput = Op.bClampInput ? 1u : 0u;
				PassParameters->ClampMin = Op.ClampMin;
				PassParameters->ClampMax = Op.ClampMax;
				PassParameters->bNormalizeInput = Op.bNormalizeInput ? 1u : 0u;
		        
		        PassParameters->InputAUVScale = FVector2f(1.0f, 1.0f);
		        PassParameters->InputAUVOffset = FVector2f(0.0f, 0.0f);
		        PassParameters->InputBUVScale = FVector2f(1.0f, 1.0f);
		        PassParameters->InputBUVOffset = FVector2f(0.0f, 0.0f);

		        FIntVector GroupCount(
		            FMath::DivideAndRoundUp<int32>(Params.MapSize.X, 8),
		            FMath::DivideAndRoundUp<int32>(Params.MapSize.Y, 8),
		            1
		        );
				
		        FComputeShaderUtils::AddPass(
		            GraphBuilder, 
		            RDG_EVENT_NAME("TCAT_Comp_%s_Step%d", *Params.VolumeName, OpIdx),
		            ComputeShader, PassParameters, GroupCount
		        );

				Swap(SrcTex, DstTex);
		    }

			// Single copy from intermediate to output (minimizes external texture interaction)
			AddCopyTexturePass(GraphBuilder, SrcTex, OutputTexture);

			// Handle async readback
			if (Params.bIsAsync && Params.GPUReadback)
			{
				AddEnqueueCopyPass(GraphBuilder, Params.GPUReadback, OutputTexture);
			}
		}
	}

    GraphBuilder.Execute();

	// Readback for influence volumes
    for (const FTCATInfluenceDispatchParams& Params : InfluenceBatch)
    {
    	if (Params.bIsAsync || !Params.OutGridData || !Params.OutInfluenceMapRHI.IsValid())
    	{
    		continue;
    	}
    	TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_SyncReadback_STALL); 
        SCOPE_CYCLE_COUNTER(STAT_TCAT_Readback_Wait);

        const int32 TotalCells = Params.MapSize.X * Params.MapSize.Y;
        Params.OutGridData->SetNumUninitialized(TotalCells);

        uint32 DestStride = 0;
        float* GPUDataPtr = static_cast<float*>(RHICmdList.LockTexture2D(Params.OutInfluenceMapRHI, 0, RLM_ReadOnly, DestStride, false));
    
        if (GPUDataPtr)
        {
        	const uint32 RowPitch = Params.MapSize.X * sizeof(float);
        	if (DestStride == RowPitch)
        	{
        		FMemory::Memcpy(Params.OutGridData->GetData(), GPUDataPtr, TotalCells * sizeof(float));
        	}
        	else
        	{
        		for (uint32 y = 0; y < Params.MapSize.Y; ++y)
        		{
        			FMemory::Memcpy(
						Params.OutGridData->GetData() + (y * Params.MapSize.X),
						reinterpret_cast<uint8*>(GPUDataPtr) + (y * DestStride), RowPitch
					);
        		}
        	}
        	RHICmdList.UnlockTexture2D(Params.OutInfluenceMapRHI, 0, false);
        }
    }

	// Readback for composite volumes
	for (const FTCATCompositeDispatchParams& Params : CompositeBatch)
	{
		if (Params.bIsAsync || !Params.OutGridData || !Params.OutInfluenceMapRHI.IsValid())
		{
			continue;
		}
		
		TRACE_CPUPROFILER_EVENT_SCOPE(TCAT_SyncReadback_STALL); 
		SCOPE_CYCLE_COUNTER(STAT_TCAT_Readback_Wait);
		
		const int32 TotalCells = Params.MapSize.X * Params.MapSize.Y;
		Params.OutGridData->SetNumUninitialized(TotalCells);

		uint32 DestStride = 0;
		float* GPUDataPtr = static_cast<float*>(RHICmdList.LockTexture2D(Params.OutInfluenceMapRHI, 0, RLM_ReadOnly, DestStride, false));

		if (GPUDataPtr)
		{
			const uint32 RowPitch = Params.MapSize.X * sizeof(float);
			if (DestStride == RowPitch)
			{
				FMemory::Memcpy(Params.OutGridData->GetData(), GPUDataPtr, TotalCells * sizeof(float));
			}
			else
			{
				for (uint32 y = 0; y < Params.MapSize.Y; ++y)
				{
					FMemory::Memcpy(
						Params.OutGridData->GetData() + (y * Params.MapSize.X),
						reinterpret_cast<uint8*>(GPUDataPtr) + (y * DestStride), RowPitch
					);
				}
			}
			RHICmdList.UnlockTexture2D(Params.OutInfluenceMapRHI, 0, false);
		}
	}
}

void FTCATInfluenceDispatcher::DispatchCPU(const FTCATInfluenceDispatchParams& Params)
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_CPU_Total);

    if (!Params.OutGridData) return;

    TArray<float>& TargetGrid = *(Params.OutGridData);
    const int32 MapWidth = static_cast<int32>(Params.MapSize.X);
    const int32 MapHeight = static_cast<int32>(Params.MapSize.Y);
    const int32 TotalCells = MapWidth * MapHeight;

    TargetGrid.SetNumZeroed(TotalCells);

    const TArray<float>* HeightData = Params.GlobalHeightMapData;
    const bool bUseCellHeight = HeightData && HeightData->Num() >= TotalCells;
    const bool bLimitVerticalRange = (Params.ProjectionFlags & static_cast<int32>(ETCATProjectionFlag::InfluenceHalfHeight)) != 0;
    const bool bCheckLineOfSight = (Params.ProjectionFlags & static_cast<int32>(ETCATProjectionFlag::LineOfSight)) != 0;
    const FVector2f MapOriginXY(Params.MapStartPos.X, Params.MapStartPos.Y);
    const float HalfGrid = Params.GridSize * 0.5f;

	EParallelForFlags PFFlags = Params.bForceCPUSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;

    ParallelFor(TotalCells, [&](int32 Index)
    {
        const int32 X = Index % MapWidth;
        const int32 Y = Index / MapWidth;

        const FVector2f CellOffset(static_cast<float>(X) * Params.GridSize + HalfGrid,
                                   static_cast<float>(Y) * Params.GridSize + HalfGrid);
        const FVector2f CellWorldXY = MapOriginXY + CellOffset;

        float CellHeight = Params.MapStartPos.Z;
        if (bUseCellHeight && HeightData->IsValidIndex(Index))
        {
            CellHeight = (*HeightData)[Index];
        }

        FVector CellPos(CellWorldXY.X, CellWorldXY.Y, CellHeight);

        float TotalInfluence = 0.0f;

        for (const FTCATInfluenceSource& Src : Params.Sources)
        {
            const FVector SourcePos(Src.WorldLocation);
            const float Distance = FVector::Dist(CellPos, SourcePos);
            if (Distance > Src.InfluenceRadius)
            {
                continue;
            }

            if (bLimitVerticalRange && Src.InfluenceHalfHeight > KINDA_SMALL_NUMBER)
            {
                if (FMath::Abs(CellPos.Z - SourcePos.Z) > Src.InfluenceHalfHeight)
                {
                    continue;
                }
            }

            if (bCheckLineOfSight)
            {
                const float Visibility = CheckVisibilityCPU(Params, SourcePos, Src.LineOfSightOffset, CellPos);
                if (Visibility <= 0.0f)
                {
                    continue;
                }
            }

            const float NormalizedDist = Distance / FMath::Max(Src.InfluenceRadius, KINDA_SMALL_NUMBER);
            const float CurveValue = SampleCurveAtlasCPU(
                Params.CurveAtlasPixelData,
                Params.AtlasWidth,
                Src.CurveTypeIndex,
                NormalizedDist);

            TotalInfluence += CurveValue * Src.Strength;
        }

        TargetGrid[Index] = TotalInfluence;
    }, PFFlags);
}

void FTCATInfluenceDispatcher::DispatchCPU_Partial(
    const FTCATInfluenceDispatchParams& Params,
    const TArray<FTCATInfluenceSource>& OldSources,
    const TArray<FTCATInfluenceSource>& NewSources)
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_CPU_Total);

    if (!Params.OutGridData) return;
    if (OldSources.Num() != NewSources.Num())
    {
        UE_LOG(LogTCAT, Error, TEXT("DispatchCPU_Partial: OldSources and NewSources array size mismatch!"));
        return;
    }

    TArray<float>& TargetGrid = *(Params.OutGridData);
    const int32 MapWidth = static_cast<int32>(Params.MapSize.X);
    const int32 MapHeight = static_cast<int32>(Params.MapSize.Y);
    const int32 TotalCells = MapWidth * MapHeight;

    // Ensure grid is properly sized (don't zero - preserve existing data)
    if (TargetGrid.Num() != TotalCells)
    {
        UE_LOG(LogTCAT, Warning, TEXT("DispatchCPU_Partial: Grid size mismatch! Expected %d, got %d. Resizing..."),
            TotalCells, TargetGrid.Num());
        TargetGrid.SetNumZeroed(TotalCells);
    }

    const TArray<float>* HeightData = Params.GlobalHeightMapData;
    const bool bUseCellHeight = HeightData && HeightData->Num() >= TotalCells;
    const bool bLimitVerticalRange = (Params.ProjectionFlags & static_cast<int32>(ETCATProjectionFlag::InfluenceHalfHeight)) != 0;
    const bool bCheckLineOfSight = (Params.ProjectionFlags & static_cast<int32>(ETCATProjectionFlag::LineOfSight)) != 0;
    const FVector2f MapOriginXY(Params.MapStartPos.X, Params.MapStartPos.Y);
    const float HalfGrid = Params.GridSize * 0.5f;

    // Build affected cells for both old and new sources
    struct FSourceCorrectionData
    {
        TArray<int32> AffectedIndices;
    };

    TArray<FSourceCorrectionData> OldSourcesData;
    TArray<FSourceCorrectionData> NewSourcesData;
    OldSourcesData.SetNum(OldSources.Num());
    NewSourcesData.SetNum(NewSources.Num());

    // Helper lambda to gather affected indices
    auto GatherAffectedCells = [&](const FTCATInfluenceSource& Src, TArray<int32>& OutIndices)
        {
            const FVector SourcePos(Src.WorldLocation);
            const float RadiusSq = Src.InfluenceRadius * Src.InfluenceRadius;

            const FVector2f SourceXY(SourcePos.X, SourcePos.Y);
            const FVector2f RelativePos = SourceXY - MapOriginXY;

            const int32 MinX = FMath::Max(0, FMath::FloorToInt((RelativePos.X - Src.InfluenceRadius) / Params.GridSize));
            const int32 MaxX = FMath::Min(MapWidth - 1, FMath::CeilToInt((RelativePos.X + Src.InfluenceRadius) / Params.GridSize));
            const int32 MinY = FMath::Max(0, FMath::FloorToInt((RelativePos.Y - Src.InfluenceRadius) / Params.GridSize));
            const int32 MaxY = FMath::Min(MapHeight - 1, FMath::CeilToInt((RelativePos.Y + Src.InfluenceRadius) / Params.GridSize));

            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    const int32 Index = Y * MapWidth + X;

                    const FVector2f CellOffset(static_cast<float>(X) * Params.GridSize + HalfGrid,
                        static_cast<float>(Y) * Params.GridSize + HalfGrid);
                    const FVector2f CellWorldXY = MapOriginXY + CellOffset;

                    float CellHeight = Params.MapStartPos.Z;
                    if (bUseCellHeight && HeightData->IsValidIndex(Index))
                    {
                        CellHeight = (*HeightData)[Index];
                    }

                    const FVector CellPos(CellWorldXY.X, CellWorldXY.Y, CellHeight);
                    const float DistSq = FVector::DistSquared(CellPos, SourcePos);

                    if (DistSq <= RadiusSq)
                    {
                        OutIndices.Add(Index);
                    }
                }
            }
        };

    // Gather affected cells for old sources
    for (int32 i = 0; i < OldSources.Num(); ++i)
    {
        GatherAffectedCells(OldSources[i], OldSourcesData[i].AffectedIndices);
    }

    // Gather affected cells for new sources
    for (int32 i = 0; i < NewSources.Num(); ++i)
    {
        GatherAffectedCells(NewSources[i], NewSourcesData[i].AffectedIndices);
    }

    EParallelForFlags PFFlags = Params.bForceCPUSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;

    // Step 1: Remove old influence (from predicted position)
    for (int32 SourceIdx = 0; SourceIdx < OldSources.Num(); ++SourceIdx)
    {
        const FTCATInfluenceSource& Src = OldSources[SourceIdx];
        const FVector OldSourcePos(Src.WorldLocation);
        const TArray<int32>& AffectedIndices = OldSourcesData[SourceIdx].AffectedIndices;

        ParallelFor(AffectedIndices.Num(), [&](int32 ArrayIdx)
            {
                const int32 Index = AffectedIndices[ArrayIdx];
                const int32 X = Index % MapWidth;
                const int32 Y = Index / MapWidth;

                const FVector2f CellOffset(static_cast<float>(X) * Params.GridSize + HalfGrid,
                    static_cast<float>(Y) * Params.GridSize + HalfGrid);
                const FVector2f CellWorldXY = MapOriginXY + CellOffset;

                float CellHeight = Params.MapStartPos.Z;
                if (bUseCellHeight && HeightData->IsValidIndex(Index))
                {
                    CellHeight = (*HeightData)[Index];
                }

                FVector CellPos(CellWorldXY.X, CellWorldXY.Y, CellHeight);
                const float Distance = FVector::Dist(CellPos, OldSourcePos);
                if( Distance > Src.InfluenceRadius)
                {
                    return;
				}

                // Vertical range check
                if (bLimitVerticalRange && Src.InfluenceHalfHeight > KINDA_SMALL_NUMBER)
                {
                    if (FMath::Abs(CellPos.Z - OldSourcePos.Z) > Src.InfluenceHalfHeight)
                    {
                        return;
                    }
                }

                // Line of sight check (using old position)
                if (bCheckLineOfSight)
                {
                    const float Visibility = CheckVisibilityCPU(Params, OldSourcePos, Src.LineOfSightOffset, CellPos);
                    if (Visibility <= 0.0f)
                    {
                        return;
                    }
                }

                // Calculate old influence to remove
                const float NormalizedDist = Distance / FMath::Max(Src.InfluenceRadius, KINDA_SMALL_NUMBER);
                const float CurveValue = SampleCurveAtlasCPU(
                    Params.CurveAtlasPixelData,
                    Params.AtlasWidth,
                    Src.CurveTypeIndex,
                    NormalizedDist);

                const float OldInfluence = CurveValue * Src.Strength;

                TargetGrid[Index] -= OldInfluence;

            }, PFFlags);
    }

    // Step 2: Add new influence (from current position)
    for (int32 SourceIdx = 0; SourceIdx < NewSources.Num(); ++SourceIdx)
    {
        const FTCATInfluenceSource& Src = NewSources[SourceIdx];
        const FVector NewSourcePos(Src.WorldLocation);
        const TArray<int32>& AffectedIndices = NewSourcesData[SourceIdx].AffectedIndices;

        ParallelFor(AffectedIndices.Num(), [&](int32 ArrayIdx)
            {
                const int32 Index = AffectedIndices[ArrayIdx];
                const int32 X = Index % MapWidth;
                const int32 Y = Index / MapWidth;

                const FVector2f CellOffset(static_cast<float>(X) * Params.GridSize + HalfGrid,
                    static_cast<float>(Y) * Params.GridSize + HalfGrid);
                const FVector2f CellWorldXY = MapOriginXY + CellOffset;

                float CellHeight = Params.MapStartPos.Z;
                if (bUseCellHeight && HeightData->IsValidIndex(Index))
                {
                    CellHeight = (*HeightData)[Index];
                }

                FVector CellPos(CellWorldXY.X, CellWorldXY.Y, CellHeight);
                const float Distance = FVector::Dist(CellPos, NewSourcePos);
                if (Distance > Src.InfluenceRadius)
                {
                    return;
                }

                // Vertical range check
                if (bLimitVerticalRange && Src.InfluenceHalfHeight > KINDA_SMALL_NUMBER)
                {
                    if (FMath::Abs(CellPos.Z - NewSourcePos.Z) > Src.InfluenceHalfHeight)
                    {
                        return;
                    }
                }

                // Line of sight check (using new position)
                if (bCheckLineOfSight)
                {
                    const float Visibility = CheckVisibilityCPU(Params, NewSourcePos, Src.LineOfSightOffset, CellPos);
                    if (Visibility <= 0.0f)
                    {
                        return;
                    }
                }

                // Calculate new influence to add
                const float NormalizedDist = Distance / FMath::Max(Src.InfluenceRadius, KINDA_SMALL_NUMBER);
                const float CurveValue = SampleCurveAtlasCPU(
                    Params.CurveAtlasPixelData,
                    Params.AtlasWidth,
                    Src.CurveTypeIndex,
                    NormalizedDist);

                const float NewInfluence = CurveValue * Src.Strength;

				TargetGrid[Index] += NewInfluence;

            }, PFFlags);
    }
}

float FTCATInfluenceDispatcher::SampleCurveAtlasCPU(const TArray<float>& AtlasData, int32 AtlasWidth, int32 RowIndex, float U)
{
	if (RowIndex <= -1) return 0.0f;
    if (AtlasData.IsEmpty() || AtlasWidth <= 0) return 0.0f;

    float ClampedU = FMath::Clamp(U, 0.0f, 1.0f);

    float VirtualCol = ClampedU * (AtlasWidth - 1);

    int32 IndexLeft = FMath::FloorToInt(VirtualCol);
    int32 IndexRight = FMath::Min(IndexLeft + 1, AtlasWidth - 1);

    float Alpha = VirtualCol - IndexLeft;

    int32 RowOffset = RowIndex * AtlasWidth;
    int32 FlattenedIdxLeft = RowOffset + IndexLeft;
    int32 FlattenedIdxRight = RowOffset + IndexRight;

    if (AtlasData.IsValidIndex(FlattenedIdxLeft) && AtlasData.IsValidIndex(FlattenedIdxRight))
    {
        return FMath::Lerp(AtlasData[FlattenedIdxLeft], AtlasData[FlattenedIdxRight], Alpha);
    }

    return 0.0f;
}

bool FTCATInfluenceDispatcher::SampleHeightMapAtUV(const FTCATInfluenceDispatchParams& Params, const FVector2f& UV, float& OutHeight)
{
    if (!Params.GlobalHeightMapData)
    {
        return false;
    }

    const TArray<float>& HeightData = *Params.GlobalHeightMapData;
    const int32 Width = static_cast<int32>(Params.MapSize.X);
    const int32 Height = static_cast<int32>(Params.MapSize.Y);

    if (Width <= 0 || Height <= 0 || HeightData.Num() < Width * Height)
    {
        return false;
    }

    const float SampleX = UV.X * (Width - 1);
    const float SampleY = UV.Y * (Height - 1);

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(SampleX), 0, Width - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SampleY), 0, Height - 1);
    const int32 X1 = FMath::Min(X0 + 1, Width - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, Height - 1);

    const float AlphaX = SampleX - X0;
    const float AlphaY = SampleY - Y0;

    const int32 Index00 = Y0 * Width + X0;
    const int32 Index10 = Y0 * Width + X1;
    const int32 Index01 = Y1 * Width + X0;
    const int32 Index11 = Y1 * Width + X1;

    if (!HeightData.IsValidIndex(Index00) || !HeightData.IsValidIndex(Index10) ||
        !HeightData.IsValidIndex(Index01) || !HeightData.IsValidIndex(Index11))
    {
        return false;
    }

    const float H00 = HeightData[Index00];
    const float H10 = HeightData[Index10];
    const float H01 = HeightData[Index01];
    const float H11 = HeightData[Index11];

    const float HX0 = FMath::Lerp(H00, H10, AlphaX);
    const float HX1 = FMath::Lerp(H01, H11, AlphaX);
    OutHeight = FMath::Lerp(HX0, HX1, AlphaY);

    return true;
}

bool FTCATInfluenceDispatcher::SampleHeightMapAtWorld(const FTCATInfluenceDispatchParams& Params, const FVector2f& WorldXY, float& OutHeight)
{
    const float SafeSizeX = FMath::Max(Params.GlobalHeightMapSize.X, KINDA_SMALL_NUMBER);
    const float SafeSizeY = FMath::Max(Params.GlobalHeightMapSize.Y, KINDA_SMALL_NUMBER);

    const FVector2f UV(
        (WorldXY.X - Params.GlobalHeightMapOrigin.X) / SafeSizeX,
        (WorldXY.Y - Params.GlobalHeightMapOrigin.Y) / SafeSizeY);

    if (UV.X < 0.0f || UV.X > 1.0f || UV.Y < 0.0f || UV.Y > 1.0f)
    {
        return false;
    }

    return SampleHeightMapAtUV(Params, UV, OutHeight);
}

float FTCATInfluenceDispatcher::CheckVisibilityCPU(
    const FTCATInfluenceDispatchParams& Params,
    const FVector& SourceLocation,
    float LineOfSightOffset,
    const FVector& TargetLocation)
{
    if (!Params.GlobalHeightMapData || Params.MapSize.X == 0 || Params.MapSize.Y == 0)
    {
        return 1.0f;
    }

    FVector StartEyePos = SourceLocation;
    StartEyePos.Z += LineOfSightOffset;

    FVector TargetPoint = TargetLocation;
    TargetPoint.Z += 10.0f;

    const FVector Diff = TargetPoint - StartEyePos;
    const float Distance = Diff.Size();
    if (Distance < 0.001f)
    {
        return 1.0f;
    }

    const FVector Dir = Diff / Distance;
    const float StepSize = FMath::Max(Params.RayMarchStepSize, 1.0f);
    const int32 MaxSteps = FMath::Max(Params.RayMarchMaxSteps, 1);
    const int32 Steps = FMath::Min(static_cast<int32>(Distance / StepSize), MaxSteps);

    if (Steps < 1)
    {
        return 1.0f;
    }

    FVector CurrentRayPos = StartEyePos;
    for (int32 StepIndex = 1; StepIndex < Steps; ++StepIndex)
    {
        CurrentRayPos += Dir * StepSize;

        float TerrainHeight = 0.0f;
        if (!SampleHeightMapAtWorld(Params, FVector2f(CurrentRayPos.X, CurrentRayPos.Y), TerrainHeight))
        {
            continue;
        }

        if (TerrainHeight > CurrentRayPos.Z)
        {
            return 0.0f;
        }
    }

    return 1.0f;
}

FRDGTextureSRVRef FTCATInfluenceDispatcher::DispatchMinMaxReduction(
	FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture, FUintVector2 MapSize, const FString& DebugName)
{
	// Create 1x1 RG32F texture to store (Min, Max)
	const FRDGTextureDesc MinMaxTextureDesc = FRDGTextureDesc::Create2D(
		FIntPoint(1, 1),
		PF_G32R32F,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV
	);

	FRDGTextureRef MinMaxTexture = GraphBuilder.CreateTexture(
		MinMaxTextureDesc,
		*FString::Printf(TEXT("TCAT_MinMax_%s"), *DebugName)
	);

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MinMaxTexture), FVector4f(0, 0, 0, 0));

	// Calculate number of tiles (16x16 per tile)
	uint32 NumTilesX = FMath::DivideAndRoundUp(MapSize.X, 16u);
	uint32 NumTilesY = FMath::DivideAndRoundUp(MapSize.Y, 16u);
	uint32 NumTiles = NumTilesX * NumTilesY;

	// Create intermediate buffer for stage 1 results
	FRDGBufferRef IntermediateBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), NumTiles),
		TEXT("TCAT_IntermediateMinMax")
	);

	// === Stage 1: Reduce tiles ===
	{
		TShaderMapRef<FTCATFindMaxCS_Stage1> ReduceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FTCATFindMaxCS_Stage1::FParameters* MinMax1PassParams = GraphBuilder.AllocParameters<FTCATFindMaxCS_Stage1::FParameters>();

		MinMax1PassParams->InputTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(InputTexture));
		MinMax1PassParams->IntermediateMinMaxBuffer = GraphBuilder.CreateUAV(IntermediateBuffer);
		MinMax1PassParams->TextureSize = MapSize;

		FIntVector GroupCount(NumTilesX, NumTilesY, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TCAT_FindMinMax_Stage1"),
			ReduceShader,
			MinMax1PassParams,
			GroupCount
		);
	}

	// === Stage 2: Iterative reduction (handles >256 tiles) ===
	{
		TShaderMapRef<FTCATFindMaxCS_Stage2> ReduceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		// Create output buffer for iterative reduction (ping-pong between input and output)
		uint32 MaxIntermediateSize = FMath::DivideAndRoundUp(NumTiles, 256u);
		FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), FMath::Max(MaxIntermediateSize, 1u)),
			TEXT("TCAT_MinMaxOutputBuffer")
		);

		FRDGBufferRef ReadBuffer = IntermediateBuffer;
		FRDGBufferRef WriteBuffer = OutputBuffer;
		uint32 CurrentCount = NumTiles;

		// Handle edge case: if only 1 tile, we still need to run Stage 2 to write to texture
		do
		{
			uint32 NumGroups = FMath::DivideAndRoundUp(CurrentCount, 256u);
			bool bIsFinalPass = (NumGroups == 1);

			FTCATFindMaxCS_Stage2::FParameters* MinMax2PassParams =
				GraphBuilder.AllocParameters<FTCATFindMaxCS_Stage2::FParameters>();

			MinMax2PassParams->IntermediateMinMaxBufferSRV = GraphBuilder.CreateSRV(ReadBuffer);
			MinMax2PassParams->OutputBuffer = GraphBuilder.CreateUAV(WriteBuffer);
			MinMax2PassParams->OutputMinMaxTexture = GraphBuilder.CreateUAV(MinMaxTexture);
			MinMax2PassParams->IntermediateBufferSize = CurrentCount;
			MinMax2PassParams->InputOffset = 0;
			MinMax2PassParams->OutputOffset = 0;
			MinMax2PassParams->bWriteToTexture = bIsFinalPass ? 1u : 0u;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TCAT_FindMinMax_Stage2_Pass"),
				ReduceShader,
				MinMax2PassParams,
				FIntVector(NumGroups, 1, 1)
			);

			if (!bIsFinalPass)
			{
				Swap(ReadBuffer, WriteBuffer);
				CurrentCount = NumGroups;
			}
			else
			{
				break;
			}
		} while (CurrentCount > 1);
	}

	return GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MinMaxTexture));
}

void FTCATInfluenceDispatcher::DispatchCPU_Composite(const FTCATCompositeDispatchParams& Params)
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_CPU_Total);

	if (!Params.OutGridData || Params.Operations.Num() == 0) { return; }
	if (Params.MapSize.X == 0 || Params.MapSize.Y == 0) { return; }

	const int64 TotalCells64 = (int64)Params.MapSize.X * (int64)Params.MapSize.Y;
	if (TotalCells64 > 100 * 1024 * 1024)
	{
		UE_LOG(LogTCAT, Error, TEXT("DispatchCPU_Composite: Invalid MapSize detected! %ux%u (Total: %lld). Skipping."),
			Params.MapSize.X, Params.MapSize.Y, TotalCells64);
		return;
	}

	TArray<float>& OutputGrid = *(Params.OutGridData);
	const int32 TotalCells = (int32)TotalCells64;
    OutputGrid.SetNumZeroed(TotalCells);

	struct FPreparedOp
    {
        ETCATCompositeOp Operation = ETCATCompositeOp::Add;
        const TArray<float>* Grid = nullptr;

        float Strength = 1.0f;

        bool bClampInput = false;
        float ClampMin = 0.0f;
        float ClampMax = 0.0f;

        bool bNormalizeInput = false;
        float Min = 0.0f;
        float Max = 0.0f;
        float InvRange = 0.0f; // 1/(Max-Min) if valid, else 0
    };

    TArray<FPreparedOp> PreparedOps;
    PreparedOps.Reserve(Params.Operations.Num());

    // Cache min/max per layer tag so repeated tags don't rescan the same grid.
    struct FNormStats { float Min = 0.0f; float Max = 0.0f; float InvRange = 0.0f; };
    TMap<FName, FNormStats> NormCache;
    NormCache.Reserve(Params.Operations.Num());

    for (const FTCATCompositeOperation& Op : Params.Operations)
    {
        // Unary ops: no grid needed
        if (Op.Operation == ETCATCompositeOp::Invert || Op.Operation == ETCATCompositeOp::Normalize)
        {
            FPreparedOp P;
            P.Operation = Op.Operation;
            P.Strength = Op.Strength;
            PreparedOps.Add(P);
            continue;
        }

        // Only prepare supported binary ops
        if (Op.Operation != ETCATCompositeOp::Add &&
            Op.Operation != ETCATCompositeOp::Subtract &&
            Op.Operation != ETCATCompositeOp::Multiply &&
            Op.Operation != ETCATCompositeOp::Divide)
        {
            continue;
        }

        const TArray<float>* Grid = nullptr;
        if (const TArray<float>* const* FoundGridPtr = Params.InputGridDataMap.Find(Op.InputLayerTag))
        {
            Grid = (FoundGridPtr && *FoundGridPtr) ? *FoundGridPtr : nullptr;
        }

        // If missing grid, we still keep the op but it will contribute 0.0f at runtime.
        FPreparedOp P;
        P.Operation = Op.Operation;
        P.Grid = Grid;
        P.Strength = Op.Strength;
        P.bClampInput = Op.bClampInput;
        P.ClampMin = Op.ClampMin;
        P.ClampMax = Op.ClampMax;
        P.bNormalizeInput = Op.bNormalizeInput;

        if (P.bNormalizeInput && P.Grid && P.Grid->Num() > 0)
        {
            if (FNormStats* Cached = NormCache.Find(Op.InputLayerTag))
            {
                P.Min = Cached->Min;
                P.Max = Cached->Max;
                P.InvRange = Cached->InvRange;
            }
            else
            {
                const float MinV = FMath::Min(*P.Grid);
                const float MaxV = FMath::Max(*P.Grid);
                const float Range = MaxV - MinV;

                FNormStats Stats;
                Stats.Min = MinV;
                Stats.Max = MaxV;
                Stats.InvRange = (FMath::Abs(Range) > KINDA_SMALL_NUMBER) ? (1.0f / Range) : 0.0f;

                NormCache.Add(Op.InputLayerTag, Stats);

                P.Min = Stats.Min;
                P.Max = Stats.Max;
                P.InvRange = Stats.InvRange;
            }
        }

        PreparedOps.Add(P);
    }

    EParallelForFlags PFFlags = Params.bForceCPUSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;

    // Check if any Normalize operations exist - if so, we need segment-based processing
    bool bHasNormalize = false;
    for (const FPreparedOp& Op : PreparedOps)
    {
        if (Op.Operation == ETCATCompositeOp::Normalize)
        {
            bHasNormalize = true;
            break;
        }
    }

    if (!bHasNormalize)
    {
        // Fast path: no Normalize operations, process all in single pass
        ParallelFor(TotalCells, [&](int32 Index)
        {
            float Accumulator = 0.0f;

            for (const FPreparedOp& Op : PreparedOps)
            {
                if (Op.Operation == ETCATCompositeOp::Invert)
                {
                    Accumulator = (1.0f - Accumulator) * Op.Strength;
                    continue;
                }

                float ValueB = 0.0f;

                if (Op.Grid && Op.Grid->IsValidIndex(Index))
                {
                    ValueB = (*Op.Grid)[Index];

                    if (Op.bClampInput)
                    {
                        ValueB = FMath::Clamp(ValueB, Op.ClampMin, Op.ClampMax);
                    }

                    if (Op.bNormalizeInput && Op.InvRange > 0.0f)
                    {
                        ValueB = (ValueB - Op.Min) * Op.InvRange;
                    }
                    else if (Op.bNormalizeInput)
                    {
                        ValueB = 0.0f;
                    }
                }

                ValueB *= Op.Strength;

                switch (Op.Operation)
                {
                    case ETCATCompositeOp::Add:      Accumulator += ValueB; break;
                    case ETCATCompositeOp::Subtract: Accumulator -= ValueB; break;
                    case ETCATCompositeOp::Multiply: Accumulator *= ValueB; break;
                    case ETCATCompositeOp::Divide:
                        if (FMath::Abs(ValueB) > KINDA_SMALL_NUMBER)
                        {
                            Accumulator /= ValueB;
                        }
                        break;
                    default:
                        break;
                }
            }

            OutputGrid[Index] = Accumulator;
        }, PFFlags);
    }
    else
    {
        // Segment-based processing: Normalize requires global min/max of accumulator
        // Process operations in segments separated by Normalize operations

        int32 OpStartIdx = 0;

        while (OpStartIdx < PreparedOps.Num())
        {
            // Find the next Normalize operation or end of operations
            int32 OpEndIdx = OpStartIdx;
            while (OpEndIdx < PreparedOps.Num() &&
                   PreparedOps[OpEndIdx].Operation != ETCATCompositeOp::Normalize)
            {
                ++OpEndIdx;
            }

            // Process operations [OpStartIdx, OpEndIdx)
            if (OpEndIdx > OpStartIdx)
            {
                ParallelFor(TotalCells, [&](int32 Index)
                {
                    float Accumulator = OutputGrid[Index];

                    for (int32 OpIdx = OpStartIdx; OpIdx < OpEndIdx; ++OpIdx)
                    {
                        const FPreparedOp& Op = PreparedOps[OpIdx];

                        if (Op.Operation == ETCATCompositeOp::Invert)
                        {
                            Accumulator = (1.0f - Accumulator) * Op.Strength;
                            continue;
                        }

                        float ValueB = 0.0f;

                        if (Op.Grid && Op.Grid->IsValidIndex(Index))
                        {
                            ValueB = (*Op.Grid)[Index];

                            if (Op.bClampInput)
                            {
                                ValueB = FMath::Clamp(ValueB, Op.ClampMin, Op.ClampMax);
                            }

                            if (Op.bNormalizeInput && Op.InvRange > 0.0f)
                            {
                                ValueB = (ValueB - Op.Min) * Op.InvRange;
                            }
                            else if (Op.bNormalizeInput)
                            {
                                ValueB = 0.0f;
                            }
                        }

                        ValueB *= Op.Strength;

                        switch (Op.Operation)
                        {
                            case ETCATCompositeOp::Add:      Accumulator += ValueB; break;
                            case ETCATCompositeOp::Subtract: Accumulator -= ValueB; break;
                            case ETCATCompositeOp::Multiply: Accumulator *= ValueB; break;
                            case ETCATCompositeOp::Divide:
                                if (FMath::Abs(ValueB) > KINDA_SMALL_NUMBER)
                                {
                                    Accumulator /= ValueB;
                                }
                                break;
                            default:
                                break;
                        }
                    }

                    OutputGrid[Index] = Accumulator;
                }, PFFlags);
            }

            // If we hit a Normalize operation, apply it
            if (OpEndIdx < PreparedOps.Num() &&
                PreparedOps[OpEndIdx].Operation == ETCATCompositeOp::Normalize)
            {
                const FPreparedOp& NormOp = PreparedOps[OpEndIdx];

                // Compute min/max of current accumulator
                float MinVal = TNumericLimits<float>::Max();
                float MaxVal = TNumericLimits<float>::Lowest();

                for (int32 i = 0; i < TotalCells; ++i)
                {
                    MinVal = FMath::Min(MinVal, OutputGrid[i]);
                    MaxVal = FMath::Max(MaxVal, OutputGrid[i]);
                }

                // Handle degenerate range
                const float Range = MaxVal - MinVal;
                const float InvRange = (FMath::Abs(Range) > KINDA_SMALL_NUMBER) ? (1.0f / Range) : 0.0f;
                const float Strength = NormOp.Strength;

                // Apply normalization with strength
                ParallelFor(TotalCells, [&](int32 Index)
                {
                    float NormValue = (InvRange > 0.0f)
                        ? ((OutputGrid[Index] - MinVal) * InvRange)
                        : 0.0f;
                    OutputGrid[Index] = NormValue * Strength;
                }, PFFlags);

                ++OpEndIdx; // Move past Normalize
            }

            OpStartIdx = OpEndIdx;
        }
    }
}

void FTCATInfluenceDispatcher::DispatchCPU_Composite_Partial(
    const FTCATCompositeDispatchParams& Params,
    const TArray<int32>& AffectedCellIndices)
{
    SCOPE_CYCLE_COUNTER(STAT_TCAT_CPU_Total);

    if (!Params.OutGridData || Params.Operations.Num() == 0) { return; }
    if (Params.MapSize.X == 0 || Params.MapSize.Y == 0) { return; }
    if (AffectedCellIndices.Num() == 0) { return; }

    TArray<float>& OutputGrid = *(Params.OutGridData);
    const int32 TotalCells = Params.MapSize.X * Params.MapSize.Y;

    if (OutputGrid.Num() != TotalCells)
    {
        UE_LOG(LogTCAT, Warning, TEXT("DispatchCPU_Composite_Partial: Grid size mismatch!"));
        return;
    }

    // Prepare operations (same as full composite)
    struct FPreparedOp
    {
        ETCATCompositeOp Operation = ETCATCompositeOp::Add;
        const TArray<float>* Grid = nullptr;
        float Strength = 1.0f;
        bool bClampInput = false;
        float ClampMin = 0.0f;
        float ClampMax = 0.0f;
        bool bNormalizeInput = false;
        float Min = 0.0f;
        float Max = 0.0f;
        float InvRange = 0.0f;
    };

    TArray<FPreparedOp> PreparedOps;
    PreparedOps.Reserve(Params.Operations.Num());

    struct FNormStats { float Min = 0.0f; float Max = 0.0f; float InvRange = 0.0f; };
    TMap<FName, FNormStats> NormCache;

    for (const FTCATCompositeOperation& Op : Params.Operations)
    {
        if (Op.Operation == ETCATCompositeOp::Invert || Op.Operation == ETCATCompositeOp::Normalize)
        {
            FPreparedOp P;
            P.Operation = Op.Operation;
            P.Strength = Op.Strength;
            PreparedOps.Add(P);
            continue;
        }

        if (Op.Operation != ETCATCompositeOp::Add &&
            Op.Operation != ETCATCompositeOp::Subtract &&
            Op.Operation != ETCATCompositeOp::Multiply &&
            Op.Operation != ETCATCompositeOp::Divide)
        {
            continue;
        }

        const TArray<float>* Grid = nullptr;
        if (const TArray<float>* const* FoundGridPtr = Params.InputGridDataMap.Find(Op.InputLayerTag))
        {
            Grid = (FoundGridPtr && *FoundGridPtr) ? *FoundGridPtr : nullptr;
        }

        FPreparedOp P;
        P.Operation = Op.Operation;
        P.Grid = Grid;
        P.Strength = Op.Strength;
        P.bClampInput = Op.bClampInput;
        P.ClampMin = Op.ClampMin;
        P.ClampMax = Op.ClampMax;
        P.bNormalizeInput = Op.bNormalizeInput;

        if (P.bNormalizeInput && P.Grid && P.Grid->Num() > 0)
        {
            if (FNormStats* Cached = NormCache.Find(Op.InputLayerTag))
            {
                P.Min = Cached->Min;
                P.Max = Cached->Max;
                P.InvRange = Cached->InvRange;
            }
            else
            {
                const float MinV = FMath::Min(*P.Grid);
                const float MaxV = FMath::Max(*P.Grid);
                const float Range = MaxV - MinV;

                FNormStats Stats;
                Stats.Min = MinV;
                Stats.Max = MaxV;
                Stats.InvRange = (FMath::Abs(Range) > KINDA_SMALL_NUMBER) ? (1.0f / Range) : 0.0f;

                NormCache.Add(Op.InputLayerTag, Stats);

                P.Min = Stats.Min;
                P.Max = Stats.Max;
                P.InvRange = Stats.InvRange;
            }
        }

        PreparedOps.Add(P);
    }

    EParallelForFlags PFFlags = Params.bForceCPUSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;

    // Check if Normalize exists
    bool bHasNormalize = false;
    for (const FPreparedOp& Op : PreparedOps)
    {
        if (Op.Operation == ETCATCompositeOp::Normalize)
        {
            bHasNormalize = true;
            break;
        }
    }

    if (!bHasNormalize)
    {
        // Fast path: recalculate only affected cells
        ParallelFor(AffectedCellIndices.Num(), [&](int32 ArrayIdx)
            {
                const int32 Index = AffectedCellIndices[ArrayIdx];
                if (!OutputGrid.IsValidIndex(Index)) return;

                float Accumulator = 0.0f;

                for (const FPreparedOp& Op : PreparedOps)
                {
                    if (Op.Operation == ETCATCompositeOp::Invert)
                    {
                        Accumulator = (1.0f - Accumulator) * Op.Strength;
                        continue;
                    }

                    float ValueB = 0.0f;

                    if (Op.Grid && Op.Grid->IsValidIndex(Index))
                    {
                        ValueB = (*Op.Grid)[Index];

                        if (Op.bClampInput)
                        {
                            ValueB = FMath::Clamp(ValueB, Op.ClampMin, Op.ClampMax);
                        }

                        if (Op.bNormalizeInput && Op.InvRange > 0.0f)
                        {
                            ValueB = (ValueB - Op.Min) * Op.InvRange;
                        }
                        else if (Op.bNormalizeInput)
                        {
                            ValueB = 0.0f;
                        }
                    }

                    ValueB *= Op.Strength;

                    switch (Op.Operation)
                    {
                    case ETCATCompositeOp::Add:      Accumulator += ValueB; break;
                    case ETCATCompositeOp::Subtract: Accumulator -= ValueB; break;
                    case ETCATCompositeOp::Multiply: Accumulator *= ValueB; break;
                    case ETCATCompositeOp::Divide:
                        if (FMath::Abs(ValueB) > KINDA_SMALL_NUMBER)
                        {
                            Accumulator /= ValueB;
                        }
                        break;
                    default:
                        break;
                    }
                }

                OutputGrid[Index] = Accumulator;
            }, PFFlags);
    }
    else
    {
        // Slow path: Normalize requires full grid min/max
        // In this case, we must recalculate the entire grid
        UE_LOG(LogTCAT, Warning,
            TEXT("DispatchCPU_Composite_Partial: Normalize operation detected. Partial update not optimal, falling back to full update."));

        DispatchCPU_Composite(Params);
    }
}
