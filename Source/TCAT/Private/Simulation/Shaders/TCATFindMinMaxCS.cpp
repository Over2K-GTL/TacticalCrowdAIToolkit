// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Simulation/Shaders/TCATFindMinMaxCS.h"

IMPLEMENT_GLOBAL_SHADER(FTCATFindMaxCS_Stage1, "/Plugin/TCAT/TCAT_FindMinMax.usf", "FindMaxStage1", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTCATFindMaxCS_Stage2, "/Plugin/TCAT/TCAT_FindMinMax.usf", "FindMaxStage2", SF_Compute);
