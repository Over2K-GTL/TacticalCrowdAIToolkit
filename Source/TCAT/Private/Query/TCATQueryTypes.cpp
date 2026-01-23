// Copyright 2025-2026 Over2K. All Rights Reserved.


#include "Query/TCATQueryTypes.h"
#include "Query/TCATQueryProcessor.h"

void FTCATBatchTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Processor)
	{
		Processor->ExecuteBatch();
	}
}
