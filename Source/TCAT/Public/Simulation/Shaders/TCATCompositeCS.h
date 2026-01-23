// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FTCATCompositeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTCATCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FTCATCompositeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Input maps
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InputMapA)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InputMapB)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float2>, NormalizeMinMaxTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)

		// Output map
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMap)

		// Operation parameters
		SHADER_PARAMETER(FUintVector2, MapSize)
		SHADER_PARAMETER(uint32, OperationType)
		SHADER_PARAMETER(float, StrengthParam)
		SHADER_PARAMETER(uint32, bClampInput)
		SHADER_PARAMETER(float, ClampMin)
		SHADER_PARAMETER(float, ClampMax)
		SHADER_PARAMETER(uint32, bNormalizeInput)

		// Resampling parameters
		SHADER_PARAMETER(FVector2f, InputAUVScale)
		SHADER_PARAMETER(FVector2f, InputAUVOffset)
		SHADER_PARAMETER(FVector2f, InputBUVScale)
		SHADER_PARAMETER(FVector2f, InputBUVOffset)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};
