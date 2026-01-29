// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
/**
 * TCAT Min/Max Reduction (Compute Shader, 2-stage).
 *
 * Purpose:
 *  - Compute global [min, max] of a float texture efficiently on GPU.
 *  - Typically used for normalization passes (e.g., normalize an influence map to [0,1]).
 *
 * Pipeline overview:
 *  - Stage 1: Reduce the input texture into per-tile min/max values written to an intermediate buffer.
 *             (each tile corresponds to a thread group)
 *  - Stage 2: Reduce the intermediate buffer iteratively until a single [min, max] remains.
 *             The final result can be written to a 1x1 texture or to a buffer (for multi-pass reduction).
 *
 * Notes for plugin users:
 *  - The intermediate buffer contains float2 entries: (min, max).
 *  - Large textures may require multiple Stage 2 dispatches (iterative reduction).
 *  - The output (final) is usually a 1x1 float2 texture: (min, max).
 */

// Stage 1: Reduce texture tiles to intermediate buffer (min/max per tile)
class FTCATFindMaxCS_Stage1 : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTCATFindMaxCS_Stage1);
	SHADER_USE_PARAMETER_STRUCT(FTCATFindMaxCS_Stage1, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		/** Input float texture to reduce (R channel expected). */
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InputTexture)

		/** Output buffer storing one float2(min,max) per tile (thread-group). */
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, IntermediateMinMaxBuffer)
		SHADER_PARAMETER(FUintVector2, TextureSize)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

/** Stage 2: Reduce intermediate buffer to final min/max (supports iterative multi-pass reduction)
 * Supports iterative multi-pass reduction:
 *  - When IntermediateBufferSize > group size, dispatch multiple groups and write to OutputBuffer.
 *  - In the final pass, set bWriteToTexture to write the last result to OutputMinMaxTexture (1x1).
 */
class FTCATFindMaxCS_Stage2 : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTCATFindMaxCS_Stage2);
	SHADER_USE_PARAMETER_STRUCT(FTCATFindMaxCS_Stage2, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		/** Read-only SRV view of the intermediate min/max buffer. */
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, IntermediateMinMaxBufferSRV)

		/** Output buffer for iterative reduction passes (float2(min,max) per group). */
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, OutputBuffer)

		/**
		  * Final output texture (typically 1x1) containing float2(min,max).
		  * Only used when bWriteToTexture != 0.
		  */
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputMinMaxTexture)

		/** Number of float2 entries to reduce in this pass. */
		SHADER_PARAMETER(uint32, IntermediateBufferSize)

		/** Start index into IntermediateMinMaxBufferSRV (supports batching/iteration). */
		SHADER_PARAMETER(uint32, InputOffset)

		/** Start index into OutputBuffer where this pass writes results. */
		SHADER_PARAMETER(uint32, OutputOffset)

		/** If non-zero, write final min/max to OutputMinMaxTexture instead of OutputBuffer. */
		SHADER_PARAMETER(uint32, bWriteToTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};
