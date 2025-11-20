#include "TestServerMetric.h"
#include "TestServerGlobal.h"
#include "TestServerHttp.h"
#include "TestServerIntegration.h"
#include "textparser.h"

#include "TestServerHttp_h_ast.h"

AUTO_COMMAND ACMD_NAME(ClearMetric);
void TestServer_ClearMetric(const char *pcScope, const char *pcName)
{
	TestServer_ClearGlobal(pcScope, pcName);
}

AUTO_COMMAND ACMD_NAME(GetMetricCount);
int TestServer_GetMetricCount(const char *pcScope, const char *pcName)
{
	TestServerGlobalType eType;
	int count = 0;
	
	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Metric)
	{
		count = TestServer_GetGlobal_Integer(pcScope, pcName, 0);
	}
	TestServer_GlobalAtomicEnd();

	return count;
}

AUTO_COMMAND ACMD_NAME(GetMetricTotal);
float TestServer_GetMetricTotal(const char *pcScope, const char *pcName)
{
	TestServerGlobalType eType;
	float total = 0.0f;
	
	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Metric)
	{
		total = TestServer_GetGlobal_Float(pcScope, pcName, 1);
	}
	TestServer_GlobalAtomicEnd();

	return total;
}

AUTO_COMMAND ACMD_NAME(GetMetricAverage);
float TestServer_GetMetricAverage(const char *pcScope, const char *pcName)
{
	TestServerGlobalType eType;
	float avg = 0.0f;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Metric)
	{
		avg = TestServer_GetGlobal_Float(pcScope, pcName, 1) / TestServer_GetGlobal_Integer(pcScope, pcName, 0);
	}
	TestServer_GlobalAtomicEnd();

	return avg;
}

AUTO_COMMAND ACMD_NAME(GetMetricMinimum);
float TestServer_GetMetricMinimum(const char *pcScope, const char *pcName)
{
	TestServerGlobalType eType;
	float min = 0.0f;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Metric)
	{
		min = TestServer_GetGlobal_Float(pcScope, pcName, 2);
	}
	TestServer_GlobalAtomicEnd();

	return min;
}

AUTO_COMMAND ACMD_NAME(GetMetricMaximum);
float TestServer_GetMetricMaximum(const char *pcScope, const char *pcName)
{
	TestServerGlobalType eType;
	float max = 0.0f;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Metric)
	{
		max = TestServer_GetGlobal_Float(pcScope, pcName, 3);
	}
	TestServer_GlobalAtomicEnd();

	return max;
}

AUTO_COMMAND ACMD_NAME(PushMetric);
int TestServer_PushMetric(const char *pcScope, const char *pcName, float val)
{
	TestServerGlobalType eType;
	float total = 0.0f;
	int count = 0;
	
	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Unset)
	{
		count = 1;
		total = val;
		TestServer_SetGlobal_Integer(pcScope, pcName, 0, count);
		TestServer_SetGlobal_Float(pcScope, pcName, 1, total);
		TestServer_SetGlobal_Float(pcScope, pcName, 2, total);
		TestServer_SetGlobal_Float(pcScope, pcName, 3, total);

		TestServer_SetGlobalValueLabel(pcScope, pcName, 0, "Count");
		TestServer_SetGlobalValueLabel(pcScope, pcName, 1, "Total");
		TestServer_SetGlobalValueLabel(pcScope, pcName, 2, "Minimum");
		TestServer_SetGlobalValueLabel(pcScope, pcName, 3, "Maximum");
		TestServer_SetGlobalType(pcScope, pcName, TSG_Metric);
	}
	else if(eType == TSG_Metric)
	{
		count = TestServer_GetGlobal_Integer(pcScope, pcName, 0) + 1;
		total = TestServer_GetGlobal_Float(pcScope, pcName, 1) + val;

		TestServer_SetGlobal_Integer(pcScope, pcName, 0, count);
		TestServer_SetGlobal_Float(pcScope, pcName, 1, total);

		if(val < TestServer_GetGlobal_Float(pcScope, pcName, 2))
		{
			TestServer_SetGlobal_Float(pcScope, pcName, 2, val);
		}

		if(val > TestServer_GetGlobal_Float(pcScope, pcName, 3))
		{
			TestServer_SetGlobal_Float(pcScope, pcName, 3, val);
		}
	}
	TestServer_GlobalAtomicEnd();

	return count;
}