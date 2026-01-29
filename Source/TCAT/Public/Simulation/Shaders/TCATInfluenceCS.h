// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "Core/TCATTypes.h"

/**
 * TCAT Influence Compute Shader (RDG / Global Shader).
 *
 * This shader evaluates "influence" for each grid cell by accumulating contributions
 * from multiple influence sources (e.g., allies/enemies/objectives).
 *
 * High-level model:
 *  - One thread = one grid cell (MapSize.x * MapSize.y)
 *  - For each cell:
 *      1) Convert cell index -> world XY
 *      2) Sample global height map to get world Z (terrain height)
 *      3) For each active source:
 *          - Distance cull by InfluenceRadius
 *          - Optional vertical range limitation (MaxInfluenceZ)
 *          - Optional line-of-sight visibility test using height map ray marching
 *          - Sample curve atlas (distance -> curve value) and accumulate (curve * strength)
 *
 * Key user-facing concepts:
 *  - ProjectionFlags controls optional features (vertical range / line-of-sight).
 *  - GlobalHeightMap provides terrain height for both cell Z and LoS occlusion checks.
 *  - CurveAtlasTexture is a packed lookup texture for distance falloff curves (one row per curve type).
 *
 * IMPORTANT (API alignment):
 *  - FParameters layout MUST match the resources declared in the corresponding .usf/.ush files.
 *  - FTCAT_InfluenceSource (GPU struct) MUST match FTCATInfluenceSource (CPU struct) memory layout
 *    used to fill the StructuredBuffer SRV.
 *
 * Performance notes (for plugin users):
 *  - Complexity is roughly O(MapCellCount * SourceCount) with early culling.
 *  - Enabling LoS adds ray marching cost; tune RayMarchStepSize / RayMarchMaxSteps carefully.
 *  - Height map resolution and accuracy directly affect LoS correctness and stability.
 */
class FTCATInfluenceCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FTCATInfluenceCS);
    SHADER_USE_PARAMETER_STRUCT(FTCATInfluenceCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    // =========================================================
        // [Global Config]
        // =========================================================
        /**
        * Feature switches for influence projection.
        * Bitmask of ETCATProjectionFlag values (defined in TCATTypes).
        *
        *  - MaxInfluenceHeight: ignore cells above Src.MaxInfluenceZ
        *  - LineOfSight: perform terrain-based visibility test (height map ray marching)
        */
        SHADER_PARAMETER(uint32, ProjectionFlags)
    
        /** Number of valid entries in InSources. The shader loops [0..SourceCount). */
        SHADER_PARAMETER(uint32, SourceCount)

        /**
       * Step size (world units) for LoS ray marching when LineOfSight flag is enabled.
       * Smaller values increase accuracy but cost more steps.
       */
        SHADER_PARAMETER(float, RayMarchStepSize)

    
        /**
         * Hard cap on LoS ray-march steps to prevent unbounded cost.
         * The shader will adapt step count up to this maximum.
         */
        SHADER_PARAMETER(uint32, RayMarchMaxSteps)

        // =========================================================
        // [Source Data]
        // =========================================================
        /**
          * Structured buffer containing influence sources for this dispatch.
          * Each entry defines world position, radius, strength, curve index, and visibility/height constraints.
          *
          * Memory layout must match the HLSL struct FTCAT_InfluenceSource.
          */
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTCAT_InfluenceSource>, InSources)

        // =========================================================
        // [Environment : Height Map]
        // =========================================================
        /**
          * World origin (min corner) used for mapping world XY -> height map UV.
          * This must align with how the global height map was baked/defined.
          */
        SHADER_PARAMETER(FVector3f, GlobalHeightMapOrigin)

        /**
          * World size (XY) covered by the global height map.
          * Used to convert world XY positions into normalized UV coordinates.
          */
        SHADER_PARAMETER(FVector2f, GlobalHeightMapSize)

        /**
         * Global height map texture (R channel expected).
         * Used for:
         *  - Fetching terrain height at a cell (world Z)
         *  - Line-of-sight occlusion checks (terrain height vs ray height)
         */
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, GlobalHeightMap)
        SHADER_PARAMETER_SAMPLER(SamplerState, GlobalHeightMapSampler)

        // =========================================================
        // [Environment : Curve Atlas]
        // =========================================================
        /**
         * Curve atlas texture encoding influence falloff curves.
         * Common convention:
         *  - U = normalized distance in [0..1]
         *  - V selects curve type (row index + 0.5) / AtlasHeight
         */
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, CurveAtlasTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, CurveAtlasSampler)

        // =========================================================
        // [Influence Map (Output Context)]
        // =========================================================
        SHADER_PARAMETER(FVector3f, MapStartPos)
        SHADER_PARAMETER(float, GridSize)
        SHADER_PARAMETER(FUintVector2, MapSize)

        /**
          * Output influence map (float per cell).
          * One thread writes exactly one pixel at DispatchThreadId.xy.
          */
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutInfluenceMap)
    
    END_SHADER_PARAMETER_STRUCT()

public:
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return true;
    }

    /**
      * Defines compile-time constants that must match shader-side flag values.
      * These are used by the .usf/.ush implementation to branch on ProjectionFlags.
      */
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

        OutEnvironment.SetDefine(TEXT("TCAT_PROJECTION_VERTICAL_RANGE"), (uint32)ETCATProjectionFlag::MaxInfluenceHeight);
        OutEnvironment.SetDefine(TEXT("TCAT_PROJECTION_LINE_OF_SIGHT"),  (uint32)ETCATProjectionFlag::LineOfSight);
        
#if defined(UE_IS_DEBUG_OR_DEBUGGAME) && UE_IS_DEBUG_OR_DEBUGGAME
    	// Debug permutations are helpful when profiling or debugging shader logic,
    	// but significantly slower (no optimizations).
        OutEnvironment.CompilerFlags.Add(CFLAG_Debug);
        OutEnvironment.CompilerFlags.Add(CFLAG_SkipOptimizations);
#endif
    }

};
