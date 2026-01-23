// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

// Stage 1: Reduce texture tiles to intermediate buffer (min/max per tile)
class FTCATFindMaxCS_Stage1 : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTCATFindMaxCS_Stage1);
	SHADER_USE_PARAMETER_STRUCT(FTCATFindMaxCS_Stage1, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InputTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, IntermediateMinMaxBuffer)
		SHADER_PARAMETER(FUintVector2, TextureSize)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

// Stage 2: Reduce intermediate buffer to final min/max (supports iterative multi-pass reduction)
class FTCATFindMaxCS_Stage2 : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTCATFindMaxCS_Stage2);
	SHADER_USE_PARAMETER_STRUCT(FTCATFindMaxCS_Stage2, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, IntermediateMinMaxBufferSRV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, OutputBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputMinMaxTexture)
		SHADER_PARAMETER(uint32, IntermediateBufferSize)
		SHADER_PARAMETER(uint32, InputOffset)
		SHADER_PARAMETER(uint32, OutputOffset)
		SHADER_PARAMETER(uint32, bWriteToTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};
