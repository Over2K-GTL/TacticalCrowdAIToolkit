// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "Core/TCATTypes.h"

// Should Match with TCAT\Shader\TCAT_UpdateInfluence.usf Resources && FTCATInfluenceDispatchParams
class FTCATInfluenceCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FTCATInfluenceCS);
    SHADER_USE_PARAMETER_STRUCT(FTCATInfluenceCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    // =========================================================
        // [Global Config]
        // =========================================================
        SHADER_PARAMETER(uint32, ProjectionFlags)
        SHADER_PARAMETER(uint32, SourceCount)
        SHADER_PARAMETER(float, RayMarchStepSize)
        SHADER_PARAMETER(uint32, RayMarchMaxSteps)

        // =========================================================
        // [Source Data]
        // =========================================================
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTCAT_InfluenceSource>, InSources)

        // =========================================================
        // [Environment : Height Map]
        // =========================================================
        SHADER_PARAMETER(FVector3f, GlobalHeightMapOrigin)
        SHADER_PARAMETER(FVector2f, GlobalHeightMapSize)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, GlobalHeightMap)
        SHADER_PARAMETER_SAMPLER(SamplerState, GlobalHeightMapSampler)

        // =========================================================
        // [Environment : Curve Atlas]
        // =========================================================
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, CurveAtlasTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, CurveAtlasSampler)

        // =========================================================
        // [Influence Map (Output Context)]
        // =========================================================
        SHADER_PARAMETER(FVector3f, MapStartPos)
        SHADER_PARAMETER(float, GridSize)
        SHADER_PARAMETER(FUintVector2, MapSize)

        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutInfluenceMap)
    
    END_SHADER_PARAMETER_STRUCT()

public:

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return true;
    }

    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

        OutEnvironment.SetDefine(TEXT("TCAT_PROJECTION_VERTICAL_RANGE"), (uint32)ETCATProjectionFlag::InfluenceHalfHeight);
        OutEnvironment.SetDefine(TEXT("TCAT_PROJECTION_LINE_OF_SIGHT"),  (uint32)ETCATProjectionFlag::LineOfSight);
        
#if defined(UE_IS_DEBUG_OR_DEBUGGAME) && UE_IS_DEBUG_OR_DEBUGGAME
        OutEnvironment.CompilerFlags.Add(CFLAG_Debug);
        OutEnvironment.CompilerFlags.Add(CFLAG_SkipOptimizations);
#endif
    }

};
