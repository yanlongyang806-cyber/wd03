/***************************************************************************
 
 
 ***************************************************************************/
#include "textparser.h"

#include "pub/GatewayPerf.h"

#include "AutoGen/GatewayPerf_h_ast.c"

GatewayPerf *g_pperfCur = NULL;


void gateperf_SetCurFromString(char *pch)
{
	if(g_pperfCur)
	{
		StructDestroy(parse_GatewayPerf, g_pperfCur);
	}

	g_pperfCur = StructCreateFromString(parse_GatewayPerf, pch);
}

GatewayPerfCounter *gateperf_GetCounter(char *pchName)
{
	if(g_pperfCur)
	{
		EARRAY_FOREACH_REVERSE_BEGIN(g_pperfCur->ppCounters, i);
		{
			if(stricmp(g_pperfCur->ppCounters[i]->pchName, pchName) == 0)
			{
				return g_pperfCur->ppCounters[i];
			}
		}
		EARRAY_FOREACH_END;
	}

	return NULL;
}

U64 gateperf_GetCount(char *pchName, U64 iDefault)
{
	if(g_pperfCur)
	{
		EARRAY_FOREACH_REVERSE_BEGIN(g_pperfCur->ppCounters, i);
		{
			if(stricmp(g_pperfCur->ppCounters[i]->pchName, pchName) == 0)
			{
				return g_pperfCur->ppCounters[i]->iMinute;
			}
		}
		EARRAY_FOREACH_END;
	}

	return iDefault;
}

GatewayPerfTimer *gateperf_GetTimer(char *pchName)
{
	if(g_pperfCur)
	{
		EARRAY_FOREACH_REVERSE_BEGIN(g_pperfCur->ppTimers, i);
		{
			if(stricmp(g_pperfCur->ppTimers[i]->pchName, pchName) == 0)
			{
				return g_pperfCur->ppTimers[i];
			}
		}
		EARRAY_FOREACH_END;
	}

	return NULL;
}

/* End of File */
