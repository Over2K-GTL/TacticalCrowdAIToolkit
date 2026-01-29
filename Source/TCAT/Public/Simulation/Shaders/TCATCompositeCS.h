// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/**
 * TCAT Composite Compute Shader.
 *
 * This shader combines two float maps (InputMapA, InputMapB) into an output map using a configurable operation.
 * It is used to build composite tactical fields such as:
 *  - AllyInfluence + Weight * EnemyInfluence
 *  - FireDanger - CoverScore
 *  - Normalize a map for later thresholding / visualization
 *
 * Core behaviors:
 *  - Per-pixel (cell) operation on two input textures (A as accumulator, B as operand).
 *  - Optional clamp/normalize pipeline applied to InputMapB for binary ops:
 *      Clamp -> Normalize(using NormalizeMinMaxTexture) -> Strength multiplier
 *  - Optional normalize/invert operations for InputMapA (accumulator) depending on OperationType.
 *
 * Resampling support:
 *  - Each input map can be sampled with an independent UV scale/offset to support:
 *      - Different resolutions between maps
 *      - Sub-region sampling
 *      - Atlas-like packing of maps into larger textures
 *
 * Notes for plugin users:
 *  - Input textures are assumed to be float maps (R channel).
 *  - NormalizeMinMaxTexture is expected to be a 1x1 float2 texture storing (min, max).
 *  - OperationType should map to your ETCATCompositeOp enum on the CPU side.
 */
class FTCATCompositeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTCATCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FTCATCompositeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// =========================================================
		// Input / Output maps
		// =========================================================
	
		/** Accumulator / base map (typically the running composite result). */
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InputMapA)
	
		/** Operand / modifier map (used by binary ops such as Add/Subtract/Multiply/Divide). */
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InputMapB)

		/**
		 * 1x1 texture containing float2(min,max) for normalization.
		 * Usage depends on operation:
		 *  - Binary ops: normalize InputMapB after optional clamp
		 *  - Normalize op: normalize InputMapA (accumulator)
		 */
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float2>, NormalizeMinMaxTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
	
		/** Output float map written one pixel per thread. */
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMap)
	
		// =========================================================
		// Operation parameters
		// =========================================================

		/** Output resolution (X = width, Y = height). */
		SHADER_PARAMETER(FUintVector2, MapSize)

		/**
		 * Composite operation selector (CPU side should match shader-side switch cases).
		 * Expected mapping: ETCATCompositeOp enum (e.g., Add/Subtract/Multiply/Divide/Invert/Normalize).
		 */
		SHADER_PARAMETER(uint32, OperationType)

		/**
		 * Strength multiplier applied depending on operation:
		 *  - Binary ops: applied to processed InputMapB (after clamp/normalize)
		 *  - Invert/Normalize: applied to accumulator result
		 */
		SHADER_PARAMETER(float, StrengthParam)
	
		/**
		 * If non-zero, clamp InputMapB to [ClampMin, ClampMax] before normalization/strength.
		 * This is only used for binary ops.
		 */
		SHADER_PARAMETER(uint32, bClampInput)
		SHADER_PARAMETER(float, ClampMin)
		SHADER_PARAMETER(float, ClampMax)

		/**
		 * If non-zero, normalize InputMapB to [0,1] using NormalizeMinMaxTexture.
		 * This is only used for binary ops.
		 */
		SHADER_PARAMETER(uint32, bNormalizeInput)

		// =========================================================
		// Resampling parameters
		// =========================================================
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
