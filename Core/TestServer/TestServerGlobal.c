#include "TestServerGlobal.h"
#include "EString.h"
#include "file.h"
#include "net.h"
#include "StashTable.h"
#include "StringCache.h"
#include "structNet.h"
#include "TestServerExpression.h"
#include "TestServerHttp.h"
#include "TestServerIntegration.h"
#include "TestServerMetric.h"
#include "TestServerReport.h"
#include "textparser.h"
#include "timing_profiler_interface.h"
#include "windefinclude.h"

#include "TestServerHttp_h_ast.h"
#include "AutoGen/TestServerIntegration_h_ast.h"

static CRITICAL_SECTION cs_sGlobalTable;
static StashTable sGlobalTable = NULL;
static StashTable sGlobalRefTable = NULL;

static const char *TestServer_GetGlobalName_internal(const char *pcScope, const char *pcName)
{
	const char *pcAllocScopedName;
	char *pcScopedName = NULL;

	PERFINFO_AUTO_START_FUNC();
	if(pcScope)
	{
		estrPrintf(&pcScopedName, "%s::%s", pcScope, pcName);
	}
	else
	{
		estrPrintf(&pcScopedName, "%s", pcName);
	}

	pcAllocScopedName = allocAddString(pcScopedName);
	estrDestroy(&pcScopedName);
	PERFINFO_AUTO_STOP();

	return pcAllocScopedName;
}

static void TestServer_SetReference_internal(TestServerGlobalReference *pRef, const char *pcScope, const char *pcName)
{
	PERFINFO_AUTO_START_FUNC();
	if(pcScope && !pcScope[0])
	{
		pcScope = NULL;
	}

	pRef->pcScope = allocAddString(pcScope);
	pRef->pcName = allocAddString(pcName);
	PERFINFO_AUTO_STOP();
}

static void TestServer_AddReference_internal(TestServerGlobalReference *pRef)
{
	TestServerGlobalReference **ppRefs = NULL;
	StashElement pElem = NULL;
	TestServerGlobal *pGlobal = NULL;
	const char *pcPoolName;

	PERFINFO_AUTO_START_FUNC();
	pcPoolName = TestServer_GetGlobalName_internal(pRef->pcScope, pRef->pcName);
	
	if(stashFindElement(sGlobalRefTable, pcPoolName, &pElem))
	{
		ppRefs = stashElementGetPointer(pElem);
	}

	eaPush(&ppRefs, pRef);
	stashAddPointer(sGlobalRefTable, pcPoolName, ppRefs, true);

	if(stashFindPointer(sGlobalTable, pcPoolName, &pGlobal))
	{
		pRef->pGlobal = pGlobal;
		pRef->bSet = true;
	}
	PERFINFO_AUTO_STOP();
}

static void TestServer_SetReferences_internal(TestServerGlobal *pGlobal)
{
	TestServerGlobalReference **ppRefs = NULL;
	StashElement pElem = NULL;
	const char *pcPoolName;
	int i;

	PERFINFO_AUTO_START_FUNC();
	pcPoolName = TestServer_GetGlobalName_internal(pGlobal->pcScope, pGlobal->pcName);

	if(stashFindElement(sGlobalRefTable, pcPoolName, &pElem))
	{
		ppRefs = stashElementGetPointer(pElem);
	}

	for(i = 0; i < eaSize(&ppRefs); ++i)
	{
		ppRefs[i]->pGlobal = pGlobal;
		ppRefs[i]->bSet = true;
	}
	PERFINFO_AUTO_STOP();
}

static void TestServer_RemoveReference_internal(TestServerGlobalReference *pRef)
{
	TestServerGlobalReference **ppRefs = NULL;
	StashElement pElem = NULL;
	const char *pcPoolName;

	PERFINFO_AUTO_START_FUNC();
	pcPoolName = TestServer_GetGlobalName_internal(pRef->pcScope, pRef->pcName);

	if(stashFindElement(sGlobalRefTable, pcPoolName, &pElem))
	{
		ppRefs = stashElementGetPointer(pElem);
	}

	eaFindAndRemove(&ppRefs, pRef);
	pRef->pGlobal = NULL;
	pRef->bSet = false;
	PERFINFO_AUTO_STOP();
}

static void TestServer_ClearReferences_internal(TestServerGlobal *pGlobal)
{
	TestServerGlobalReference **ppRefs = NULL;
	StashElement pElem = NULL;
	const char *pcPoolName;
	int i;

	PERFINFO_AUTO_START_FUNC();
	pcPoolName = TestServer_GetGlobalName_internal(pGlobal->pcScope, pGlobal->pcName);

	if(stashFindElement(sGlobalRefTable, pcPoolName, &pElem))
	{
		ppRefs = stashElementGetPointer(pElem);
	}

	for(i = 0; i < eaSize(&ppRefs); ++i)
	{
		ppRefs[i]->pGlobal = NULL;
		ppRefs[i]->bSet = false;
	}
	PERFINFO_AUTO_STOP();
}

static void TestServer_UnsetGlobalValue_internal(TestServerGlobalValue *pValue)
{
	if(!pValue)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	switch(pValue->eType)
	{
	case TSGV_Integer:
		pValue->iIntVal = 0;
	xcase TSGV_Boolean:
		pValue->bBoolVal = false;
	xcase TSGV_Float:
		pValue->fFloatVal = 0.0f;
	xcase TSGV_String:
		estrDestroy(&pValue->pcStringVal);
		pValue->pcStringVal = NULL;
	xcase TSGV_Password:
		estrDestroy(&pValue->pcPasswordVal);
		pValue->pcPasswordVal = NULL;
	xcase TSGV_Global:
		TestServer_RemoveReference_internal(pValue->pRefVal);
		StructDestroySafe(parse_TestServerGlobalReference, &pValue->pRefVal);
	xdefault:
		break;
	}

	pValue->eType = TSGV_Unset;

	PERFINFO_AUTO_STOP();
	return;
}

static void TestServer_ClearGlobalValue_internal(TestServerGlobalValue *pValue)
{
	if(!pValue)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	TestServer_UnsetGlobalValue_internal(pValue);
	StructDestroy(parse_TestServerGlobalValue, pValue);
	PERFINFO_AUTO_STOP();
}

static void TestServer_UnsetGlobal_internal(TestServerGlobal *pGlobal)
{
	int i;

	if(!pGlobal)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	for(i = eaSize(&pGlobal->ppValues) - 1; i >= 0; --i)
	{
		TestServer_ClearGlobalValue_internal(pGlobal->ppValues[i]);
		eaRemove(&pGlobal->ppValues, i);
	}

	pGlobal->eType = TSG_Unset;
	pGlobal->bPersist = false;
	eaDestroy(&pGlobal->ppValues);
	PERFINFO_AUTO_STOP();
}

static void TestServer_ClearGlobal_internal(TestServerGlobal *pGlobal);

static void TestServer_ClearGlobalsInScope_internal(TestServerGlobal *pGlobal)
{
	StashElement pElem;
	StashTableIterator iter;
	TestServerGlobal *pSubGlobal = NULL;

	if(!pGlobal || pGlobal->pcScope)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	stashGetIterator(sGlobalTable, &iter);

	while(stashGetNextElement(&iter, &pElem))
	{
		pSubGlobal = stashElementGetPointer(pElem);

		if(pSubGlobal->pcScope == pGlobal->pcName)
		{
			// Scoped to parent global, clear it
			TestServer_ClearGlobal_internal(pSubGlobal);
		}
	}
	PERFINFO_AUTO_STOP();
}

static const char *spcLastGlobalScope = NULL;
static const char *spcLastGlobalName = NULL;
static TestServerGlobal *spLastGlobal = NULL;

static void TestServer_ClearGlobal_internal(TestServerGlobal *pGlobal)
{
	if(!pGlobal)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	TestServer_UnsetGlobal_internal(pGlobal);
	stashRemovePointer(sGlobalTable, TestServer_GetGlobalName_internal(pGlobal->pcScope, pGlobal->pcName), NULL);
	TestServer_ClearGlobalsInScope_internal(pGlobal);
	TestServer_ClearReferences_internal(pGlobal);

	if(spcLastGlobalScope == pGlobal->pcScope && spcLastGlobalName == pGlobal->pcName)
	{
		spLastGlobal = NULL;
	}

	StructDestroy(parse_TestServerGlobal, pGlobal);
	PERFINFO_AUTO_STOP();
}

static TestServerGlobal *TestServer_GetGlobal_internal(const char *pcScope, const char *pcName, bool bCreate)
{
	TestServerGlobal *pGlobal = NULL;
	const char *pcAllocScope = NULL;
	const char *pcAllocName;
	const char *pcPoolName;
	char *pcTrimScope = NULL;											// Temp variable = pcScope trimmed of whitespace
	char *pcTrimName = NULL;											// Temp variable = pcName trimmed of whitespace

	PERFINFO_AUTO_START_FUNC();

	estrCopy2(&pcTrimScope, pcScope);									// Copying estring to temp variable to manipulate
	estrCopy2(&pcTrimName, pcName);

	if(pcScope && pcScope[0])
	{
		estrTrimLeadingAndTrailingWhitespaceEx(&pcTrimScope, " ");		// Trim whitespace from global scopes
		pcAllocScope = allocAddString(pcTrimScope);						// Creating CONST version of string
	}

	estrTrimLeadingAndTrailingWhitespaceEx(&pcTrimName, " ");			// Trim whitespace from global names
	pcAllocName = allocAddString(pcTrimName);							// Creating CONST version of string

	estrDestroy(&pcTrimName);											// Deallocating memory for temp estring
	estrDestroy(&pcTrimScope);

	// Cache check
	if(spcLastGlobalScope == pcAllocScope && spcLastGlobalName == pcAllocName && (spLastGlobal || !bCreate))
	{
		PERFINFO_AUTO_START("Cache hit", 1);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return spLastGlobal;
	}

	PERFINFO_AUTO_START("Cache miss", 1);
	pcPoolName = TestServer_GetGlobalName_internal(pcAllocScope, pcAllocName);

	if(!stashFindPointer(sGlobalTable, pcPoolName, &pGlobal) && bCreate)
	{
		pGlobal = StructCreate(parse_TestServerGlobal);
		pGlobal->pcScope = pcAllocScope;
		pGlobal->pcName = pcAllocName;
		stashAddPointer(sGlobalTable, pcPoolName, pGlobal, false);
		TestServer_SetReferences_internal(pGlobal);
	}

	if(pGlobal)
	{
		spcLastGlobalScope = pGlobal->pcScope;
		spcLastGlobalName = pGlobal->pcName;
		spLastGlobal = pGlobal;
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();



	return pGlobal;
}

static TestServerGlobalValue *TestServer_InsertGlobalValue_internal(TestServerGlobal *pGlobal, int pos)
{
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	pValue = StructCreate(parse_TestServerGlobalValue);

	if(pos < 0 || pos >= eaSize(&pGlobal->ppValues))
	{
		eaPush(&pGlobal->ppValues, pValue);
	}
	else
	{
		eaInsert(&pGlobal->ppValues, pValue, pos);
	}

	if(eaSize(&pGlobal->ppValues) > 0 && pGlobal->eType == TSG_Unset)
	{
		pGlobal->eType = TSG_Single;
	}
	else if(eaSize(&pGlobal->ppValues) > 1 && pGlobal->eType == TSG_Single)
	{
		pGlobal->eType = TSG_Array;
	}
	PERFINFO_AUTO_STOP();

	return pValue;
}

static TestServerGlobalValue *TestServer_GetGlobalValue_internal(TestServerGlobal *pGlobal, int pos, bool bCreate)
{
	TestServerGlobalValue *pValue = NULL;

	if(!bCreate && pos >= eaSize(&pGlobal->ppValues))
	{
		return pValue;
	}

	PERFINFO_AUTO_START_FUNC();

	if(pos < 0)
	{
		if(bCreate)
		{
			TestServer_UnsetGlobal_internal(pGlobal);
			pGlobal->eType = TSG_Single;
		}
		else
		{
			pos = eaSize(&pGlobal->ppValues) - 1;
		}
	}

	if(pos < 0 || pos >= eaSize(&pGlobal->ppValues))
	{
		pValue = StructCreate(parse_TestServerGlobalValue);
		eaPush(&pGlobal->ppValues, pValue);

		if(eaSize(&pGlobal->ppValues) > 0 && pGlobal->eType == TSG_Unset)
		{
			pGlobal->eType = TSG_Single;
		}
		else if(eaSize(&pGlobal->ppValues) > 1 && pGlobal->eType == TSG_Single)
		{
			pGlobal->eType = TSG_Array;
		}
	}
	else
	{
		pValue = eaGet(&pGlobal->ppValues, pos);
	}

	PERFINFO_AUTO_STOP();
	return pValue;
}

AUTO_COMMAND ACMD_NAME(ClearGlobalValue);
void TestServer_ClearGlobalValue(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(!pGlobal)
	{
		LeaveCriticalSection(&cs_sGlobalTable);
		PERFINFO_AUTO_STOP();
		return;
	}

	if(pos < 0)
	{
		TestServer_ClearGlobal_internal(pGlobal);
		LeaveCriticalSection(&cs_sGlobalTable);
		PERFINFO_AUTO_STOP();
		return;
	}

	pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);

	if(pValue)
	{
		TestServer_ClearGlobalValue_internal(pValue);
		eaRemove(&pGlobal->ppValues, pos);
	}

	if(eaSize(&pGlobal->ppValues) == 1 && pGlobal->eType == TSG_Array)
	{
		pGlobal->eType = TSG_Single;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(ClearGlobal);
void TestServer_ClearGlobal(const char *pcScope, const char *pcName)
{
	TestServerGlobal *pGlobal;
	
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);
	TestServer_ClearGlobal_internal(pGlobal);
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

TestServerGlobalType TestServer_GetGlobalType(const char *pcScope, const char *pcName)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalType eType = TSG_Unset;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		eType = pGlobal->eType;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return eType;
}

void TestServer_SetGlobalType(const char *pcScope, const char *pcName, TestServerGlobalType eType)
{
	TestServerGlobal *pGlobal;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pGlobal->eType = eType;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

int TestServer_GetGlobalValueCount(const char *pcScope, const char *pcName)
{
	TestServerGlobal *pGlobal;
	int count = 0;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		count = eaSize(&pGlobal->ppValues);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return count;
}

TestServerGlobalValueType TestServer_GetGlobalValueType(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	TestServerGlobalValueType eType = TSGV_Unset;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue)
	{
		eType = pValue->eType;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return eType;
}

const char *TestServer_GetGlobalValueLabel(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	const char *pcLabel = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue)
	{
		pcLabel = pValue->pcLabel;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return pcLabel;
}

void TestServer_SetGlobalValueLabel(const char *pcScope, const char *pcName, int pos, const char *pcLabel)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	if(pcLabel && !pcLabel[0])
	{
		pcLabel = NULL;
	}
	
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue)
	{
		estrCopy2(&pValue->pcLabel, pcLabel);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

bool TestServer_IsGlobalPersisted(const char *pcScope, const char *pcName)
{
	TestServerGlobal *pGlobal;
	bool bPersist = false;
	
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal && pGlobal->eType != TSG_Unset)
	{
		bPersist = pGlobal->bPersist;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
	
	return bPersist;
}

void TestServer_PersistGlobal(const char *pcScope, const char *pcName, bool bPersist)
{
	TestServerGlobal *pGlobal;
	
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal && pGlobal->eType != TSG_Unset)
	{
		pGlobal->bPersist = bPersist;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(GetGlobalInt);
int TestServer_GetGlobal_Integer(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	int val = 0;
	
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue && pValue->eType == TSGV_Integer)
	{
		val = pValue->iIntVal;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return val;
}

AUTO_COMMAND ACMD_NAME(GetGlobalBool);
bool TestServer_GetGlobal_Boolean(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	bool val = false;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);

		if(pGlobal->eType == TSG_Expression && pos == 0)
		{
			TestServer_EvaluateExpressions(pcScope, pcName);
		}
	}

	if(pValue && pValue->eType == TSGV_Boolean)
	{
		val = pValue->bBoolVal;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return val;
}

AUTO_COMMAND ACMD_NAME(GetGlobalFloat);
float TestServer_GetGlobal_Float(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	float val = 0.0f;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);

		if(pGlobal->eType == TSG_Expression && pos == 0)
		{
			TestServer_EvaluateExpressions(pcScope, pcName);
		}
	}

	if(pValue && pValue->eType == TSGV_Float)
	{
		val = pValue->fFloatVal;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return val;
}

AUTO_COMMAND ACMD_NAME(GetGlobalString);
const char *TestServer_GetGlobal_String(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	const char *val = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue && pValue->eType == TSGV_String)
	{
		val = pValue->pcStringVal;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return val;
}

const char *TestServer_GetGlobal_Password(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	const char *val = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue && pValue->eType == TSGV_Password)
	{
		val = pValue->pcPasswordVal;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return val;
}

const char *TestServer_GetGlobal_RefScope(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	const char *val = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue && pValue->eType == TSGV_Global)
	{
		val = pValue->pRefVal->pcScope;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return val;
}

const char *TestServer_GetGlobal_RefName(const char *pcScope, const char *pcName, int pos)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	const char *val = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, false);
	}

	if(pValue && pValue->eType == TSGV_Global)
	{
		val = pValue->pRefVal->pcName;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return val;
}

AUTO_COMMAND ACMD_NAME(SetGlobalInt);
void TestServer_SetGlobal_Integer(const char *pcScope, const char *pcName, int pos, int val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, true);
	}

	if(pValue)
	{
		TestServer_UnsetGlobalValue_internal(pValue);
		pValue->eType = TSGV_Integer;
		pValue->iIntVal = val;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(SetGlobalBool);
void TestServer_SetGlobal_Boolean(const char *pcScope, const char *pcName, int pos, bool val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, true);
	}

	if(pValue)
	{
		TestServer_UnsetGlobalValue_internal(pValue);
		pValue->eType = TSGV_Boolean;
		pValue->bBoolVal = val;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(SetGlobalFloat);
void TestServer_SetGlobal_Float(const char *pcScope, const char *pcName, int pos, float val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, true);
	}

	if(pValue)
	{
		TestServer_UnsetGlobalValue_internal(pValue);
		pValue->eType = TSGV_Float;
		pValue->fFloatVal = val;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(SetGlobalString);
void TestServer_SetGlobal_String(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, true);
	}

	if(pValue)
	{
		TestServer_UnsetGlobalValue_internal(pValue);
		pValue->eType = TSGV_String;
		estrCopy2(&pValue->pcStringVal, val);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

// Passwords MAY NOT be persisted
AUTO_COMMAND ACMD_NAME(SetGlobalPassword);
void TestServer_SetGlobal_Password(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, true);
	}

	if(pValue)
	{
		TestServer_UnsetGlobalValue_internal(pValue);
		pValue->eType = TSGV_Password;
		estrCopy2(&pValue->pcPasswordVal, val);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(SetGlobalRef);
void TestServer_SetGlobal_Ref(const char *pcScope, const char *pcName, int pos, const char *scope, const char *name)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_GetGlobalValue_internal(pGlobal, pos, true);
	}

	if(pValue)
	{
		TestServer_UnsetGlobalValue_internal(pValue);
		pValue->eType = TSGV_Global;
		pValue->pRefVal = StructCreate(parse_TestServerGlobalReference);
		TestServer_SetReference_internal(pValue->pRefVal, scope, name);
		TestServer_AddReference_internal(pValue->pRefVal);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(InsertGlobalInt);
void TestServer_InsertGlobal_Integer(const char *pcScope, const char *pcName, int pos, int val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_InsertGlobalValue_internal(pGlobal, pos);
	}

	if(pValue)
	{
		pValue->eType = TSGV_Integer;
		pValue->iIntVal = val;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(InsertGlobalBool);
void TestServer_InsertGlobal_Boolean(const char *pcScope, const char *pcName, int pos, bool val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_InsertGlobalValue_internal(pGlobal, pos);
	}

	if(pValue)
	{
		pValue->eType = TSGV_Boolean;
		pValue->bBoolVal = val;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(InsertGlobalFloat);
void TestServer_InsertGlobal_Float(const char *pcScope, const char *pcName, int pos, float val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_InsertGlobalValue_internal(pGlobal, pos);
	}

	if(pValue)
	{
		pValue->eType = TSGV_Float;
		pValue->fFloatVal = val;
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(InsertGlobalString);
void TestServer_InsertGlobal_String(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_InsertGlobalValue_internal(pGlobal, pos);
	}

	if(pValue)
	{
		pValue->eType = TSGV_String;
		estrCopy2(&pValue->pcStringVal, val);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(InsertGlobalPassword);
void TestServer_InsertGlobal_Password(const char *pcScope, const char *pcName, int pos, const char *val)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_InsertGlobalValue_internal(pGlobal, pos);
	}

	if(pValue)
	{
		pValue->eType = TSGV_Password;
		estrCopy2(&pValue->pcPasswordVal, val);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(InsertGlobalRef);
void TestServer_InsertGlobal_Ref(const char *pcScope, const char *pcName, int pos, const char *scope, const char *name)
{
	TestServerGlobal *pGlobal;
	TestServerGlobalValue *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, true);

	if(pGlobal)
	{
		pValue = TestServer_InsertGlobalValue_internal(pGlobal, pos);
	}

	if(pValue)
	{
		pValue->eType = TSGV_Global;
		pValue->pRefVal = StructCreate(parse_TestServerGlobalReference);
		TestServer_SetReference_internal(pValue->pRefVal, scope, name);
		TestServer_AddReference_internal(pValue->pRefVal);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();
}

void TestServer_InitGlobals(void)
{
	InitializeCriticalSection(&cs_sGlobalTable);
	sGlobalTable = stashTableCreateWithStringKeys(8, StashDefault);
	sGlobalRefTable = stashTableCreateWithStringKeys(8, StashDefault);
}

void TestServer_GlobalAtomicBegin(void)
{
	EnterCriticalSection(&cs_sGlobalTable);
}

void TestServer_GlobalAtomicEnd(void)
{
	LeaveCriticalSection(&cs_sGlobalTable);
}

void TestServer_SendGlobal(const char *pcScope, const char *pcName, Packet *pkt)
{
	TestServerGlobal *pGlobal;

	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		pktSendBool(pkt, 1);
		ParserSend(parse_TestServerGlobal, pkt, NULL, pGlobal, 0, 0, 0, NULL);
	}
	else
	{
		pktSendBool(pkt, 0);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
}

static void TestServer_AddGlobalToView_internal(TestServerView *pView, TestServerGlobal *pGlobal, bool bExpand)
{
	int i;

	if(!bExpand)
	{
		for(i = 0; i < eaSize(&pGlobal->ppValues); ++i)
		{
			if(pGlobal->ppValues[i]->eType == TSGV_Global)
			{
				TestServer_RemoveReference_internal(pGlobal->ppValues[i]->pRefVal);
			}
		}
	}

	TestServer_EvaluateExpressions(pGlobal->pcScope, pGlobal->pcName);
	eaPush(&pView->ppItems, StructClone(parse_TestServerGlobal, pGlobal));

	if(!bExpand)
	{
		for(i = 0; i < eaSize(&pGlobal->ppValues); ++i)
		{
			if(pGlobal->ppValues[i]->eType == TSGV_Global)
			{
				TestServer_AddReference_internal(pGlobal->ppValues[i]->pRefVal);
			}
		}
	}
}

TestServerView *TestServer_AllGlobalsToView(bool bExpand)
{
	TestServerView *pView;
	StashTableIterator iter;
	StashElement pElem;

	PERFINFO_AUTO_START_FUNC();
	pView = StructCreate(parse_TestServerView);
	pView->pcScope = NULL;
	pView->pcName = NULL;
	pView->bExpanded = bExpand;

	EnterCriticalSection(&cs_sGlobalTable);
	stashGetIterator(sGlobalTable, &iter);

	while(stashGetNextElement(&iter, &pElem))
	{
		TestServerGlobal *pGlobal = stashElementGetPointer(pElem);
		TestServer_AddGlobalToView_internal(pView, pGlobal, bExpand);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return pView;
}

TestServerView *TestServer_ScopedGlobalsToView(const char *pcScope, bool bExpand)
{
	TestServerView *pView;
	StashTableIterator iter;
	StashElement pElem;
	const char *pcAllocScope;
	char *pcTrimScope = NULL;

	PERFINFO_AUTO_START_FUNC();
	if(pcScope && pcScope[0])
	{
		estrCopy2(&pcTrimScope, pcScope);
	}

	estrTrimLeadingAndTrailingWhitespace(&pcTrimScope);
	pcAllocScope = allocAddString(pcTrimScope);

	estrDestroy(&pcTrimScope);

	pView = StructCreate(parse_TestServerView);
	pView->pcScope = pcAllocScope;
	pView->pcName = NULL;
	pView->bExpanded = bExpand;

	EnterCriticalSection(&cs_sGlobalTable);
	stashGetIterator(sGlobalTable, &iter);

	while(stashGetNextElement(&iter, &pElem))
	{
		TestServerGlobal *pGlobal = stashElementGetPointer(pElem);

		if(pGlobal->pcScope == pcAllocScope)
		{
			TestServer_AddGlobalToView_internal(pView, pGlobal, bExpand);
		}
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return pView;
}

TestServerView *TestServer_GlobalToView(const char *pcScope, const char *pcName, bool bExpand)
{
	TestServerView *pView;
	TestServerGlobal *pGlobal;
	const char *pcAllocScope;

	PERFINFO_AUTO_START_FUNC();
	if(pcScope && !pcScope[0])
	{
		pcScope = NULL;
	}

	pcAllocScope = allocAddString(pcScope);
	pView = StructCreate(parse_TestServerView);
	pView->pcScope = pcAllocScope;
	pView->pcName = allocAddString(pcName);
	pView->bExpanded = bExpand;

	EnterCriticalSection(&cs_sGlobalTable);
	pGlobal = TestServer_GetGlobal_internal(pcScope, pcName, false);

	if(pGlobal)
	{
		TestServer_AddGlobalToView_internal(pView, pGlobal, bExpand);
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return pView;
}

TestServerView *TestServer_PersistedGlobalsToView(bool bExpand)
{
	TestServerView *pView;
	StashTableIterator iter;
	StashElement pElem;

	PERFINFO_AUTO_START_FUNC();
	pView = StructCreate(parse_TestServerView);
	pView->pcScope = NULL;
	pView->pcName = NULL;
	pView->bExpanded = bExpand;

	EnterCriticalSection(&cs_sGlobalTable);
	stashGetIterator(sGlobalTable, &iter);

	while(stashGetNextElement(&iter, &pElem))
	{
		TestServerGlobal *pGlobal = stashElementGetPointer(pElem);

		if(pGlobal->bPersist)
		{
			TestServer_AddGlobalToView_internal(pView, pGlobal, bExpand);
		}
	}
	LeaveCriticalSection(&cs_sGlobalTable);
	PERFINFO_AUTO_STOP();

	return pView;
}

void TestServer_ReadInGlobals(void)
{
	TestServerView *pView = StructCreate(parse_TestServerView);
	char *pcFileName = NULL;

	estrPrintf(&pcFileName, "%s/server/TestServer/savedvars.view", fileLocalDataDir());

	if(!fileExists(pcFileName))
	{
		return;
	}

	if(!ParserReadTextFile(pcFileName, parse_TestServerView, pView, 0))
	{
		assertmsg(0, "Reading saved globals failed! File may be corrupted, try using the backup in localdata/server/TestServer/savedvars.view.bak.");
	}

	EnterCriticalSection(&cs_sGlobalTable);
	FOR_EACH_IN_EARRAY_FORWARDS(pView->ppItems, TestServerGlobal, pItem)
	{
		int i;

		for(i = 0; i < eaSize(&pItem->ppValues); ++i)
		{
			switch(pItem->ppValues[i]->eType)
			{
			case TSGV_Integer:
				TestServer_SetGlobal_Integer(pItem->pcScope, pItem->pcName, i, pItem->ppValues[i]->iIntVal);
			xcase TSGV_Boolean:
				TestServer_SetGlobal_Boolean(pItem->pcScope, pItem->pcName, i, pItem->ppValues[i]->bBoolVal);
			xcase TSGV_Float:
				TestServer_SetGlobal_Float(pItem->pcScope, pItem->pcName, i, pItem->ppValues[i]->fFloatVal);
			xcase TSGV_String:
				TestServer_SetGlobal_String(pItem->pcScope, pItem->pcName, i, pItem->ppValues[i]->pcStringVal);
			xcase TSGV_Global:
				TestServer_SetGlobal_Ref(pItem->pcScope, pItem->pcName, i, pItem->ppValues[i]->pRefVal->pcScope, pItem->ppValues[i]->pRefVal->pcName);
			}

			TestServer_SetGlobalValueLabel(pItem->pcScope, pItem->pcName, i, pItem->ppValues[i]->pcLabel);
		}

		TestServer_SetGlobalType(pItem->pcScope, pItem->pcName, pItem->eType);
		TestServer_PersistGlobal(pItem->pcScope, pItem->pcName, pItem->bPersist);
	}
	FOR_EACH_END
	LeaveCriticalSection(&cs_sGlobalTable);
}

void TestServer_WriteOutGlobals(void)
{
	TestServerView *pView;
	char *pcFileName = NULL;

	PERFINFO_AUTO_START_FUNC();
	estrPrintf(&pcFileName, "%s/server/TestServer/savedvars.view", fileLocalDataDir());

	if(fileExists(pcFileName))
	{
		fileRenameToBak(pcFileName);
	}

	pView = TestServer_PersistedGlobalsToView(false);
	ParserWriteTextFile(pcFileName, parse_TestServerView, pView, 0, 0);
	StructDestroy(parse_TestServerView, pView);
	PERFINFO_AUTO_STOP();
}