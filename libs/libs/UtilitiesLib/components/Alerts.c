#include "alerts.h"
#include "alerts_h_ast.h"
#include "timing.h"
#include "stringUtil.h"
#include "resourceinfo.h"
#include "stringCache.h"
#include "TimedCallback.h"
#include "estring.h"
#include "expression.h"
#include "logging.h"
#include "EventCountingHeatMap.h"
#include "superassert.h"
#include "UtilitiesLib.h"
#include "alerts_c_ast.h"
#include "wininclude.h"

CRITICAL_SECTION sAlertsCriticalSection = {0};

static int siTotalCounts[ALERTLEVEL_COUNT] = {0};

static bool sbErrorfOnAlerts = false;
AUTO_CMD_INT(sbErrorfOnAlerts, ErrorfOnAlerts);

static bool sbPrintfAllAlerts = false;
AUTO_CMD_INT(sbPrintfAllAlerts, PrintfAllAlerts);

int giMaxAlertsPerKey = 25;
AUTO_CMD_INT(giMaxAlertsPerKey, MaxAlertsPerKey);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); // Should be 0 bytes

U32 *spDeferredAlertIDsToAcknowledge = NULL;

void UpdateDeferredAlerts(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

typedef struct EventCounter EventCounter;

AUTO_RUN;
void AlertInitStaticPooledStrings(void)
{
	ALERTKEY_GAMESERVERNEVERSTARTED = allocAddStaticString("GAMESERVERNEVERSTARTED");
	ALERTKEY_GAMESERVERNOTRESPONDING = allocAddStaticString("GAMESERVERNOTRESPONDING");
	ALERTKEY_GAMESERVERRUNNINGSLOW = allocAddStaticString("GAMESERVERRUNNINGSLOW");
	ALERTKEY_KILLINGNONRESPONDINGGAMESERVER = allocAddStaticString("KILLINGNONRESPONSIVEGAMESERVER");
	ALERTKEY_UGCEDIT_GAMESERVERNOTRESPONDING = allocAddStaticString("UGCEDIT_GAMESERVERNOTRESPONDING");
	ALERTKEY_UGCEDIT_GAMESERVERRUNNINGSLOW = allocAddStaticString("UGCEDIT_GAMESERVERRUNNINGSLOW");
	ALERTKEY_UGCEDIT_KILLINGNONRESPONDINGGAMESERVER = allocAddStaticString("UGCEDIT_KILLINGNONRESPONSIVEGAMESERVER");
	ALERTKEY_EMAILSENDINGFAILED = allocAddStaticString("EMAILSENDINGFAILED");
	ALERTKEY_VERSIONMISMATCH = allocAddStaticString("VERSIONMISMATCH");
	ALERTKEY_VERSIONMISMATCH_REJECT = allocAddStaticString("VERSIONMISMATCH_REJECT");
}

void SetAlwaysErrorfOnAlert(bool bSet)
{
	sbErrorfOnAlerts = bSet;
}


void InitAlertSystem(void);
static bool bAlertsInitted = false;



AUTO_FIXUPFUNC;
TextParserResult fixupKeyedAlertList(KeyedAlertList* pAlertList, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_GOTTEN_FROM_RES_DICT:
		if (pAlertList->pEventCounter)
		{
			U32 iCurTime = timeSecondsSince2000();
			pAlertList->iLastMinute = EventCounter_GetCount(pAlertList->pEventCounter, EVENTCOUNT_LASTFULLMINUTE, iCurTime);
			pAlertList->iLast15Minutes = EventCounter_GetCount(pAlertList->pEventCounter, EVENTCOUNT_LASTFULL15MINUTES, iCurTime);
			pAlertList->iLastHour = EventCounter_GetCount(pAlertList->pEventCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime);
			pAlertList->iLastDay = EventCounter_GetCount(pAlertList->pEventCounter, EVENTCOUNT_LASTFULLDAY, iCurTime);
		}
	}

	return 1;
}

static KeyedAlertList **ppAllKeyedAlertLists = NULL;
StashTable gKeyedAlertListsByKey = NULL;
static StashTable sAlertsByUID = NULL;

static int siTotalActiveBySeverity[ALERTLEVEL_COUNT] = {0};
static int siTotalAlertCount = 0;
static U32 siNextAlertUID = 1;

static FixupAlertCB **ppFixupCBs = NULL;
static FixupAlertCB *spStatedBasedAlertOffCB = NULL;

const char *ALERTKEY_GAMESERVERNEVERSTARTED;
const char *ALERTKEY_GAMESERVERNOTRESPONDING;
const char *ALERTKEY_GAMESERVERRUNNINGSLOW;
const char *ALERTKEY_KILLINGNONRESPONDINGGAMESERVER;
const char *ALERTKEY_UGCEDIT_GAMESERVERNOTRESPONDING;
const char *ALERTKEY_UGCEDIT_GAMESERVERRUNNINGSLOW;
const char *ALERTKEY_UGCEDIT_KILLINGNONRESPONDINGGAMESERVER;
const char *ALERTKEY_EMAILSENDINGFAILED;
const char *ALERTKEY_VERSIONMISMATCH;
const char *ALERTKEY_VERSIONMISMATCH_REJECT;

void SetStateBasedAlertOffCB(FixupAlertCB *pCB)
{
	spStatedBasedAlertOffCB = pCB;
}

AUTO_FIXUPFUNC;
TextParserResult fixupStateBasedAlert(StateBasedAlert* pAlert, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		stashTableDestroySafe(&pAlert->sTriggerTimesByName_Cur);
		stashTableDestroySafe(&pAlert->sTriggerTimesByName_Last);
		
	}

	return 1;
}


void *GetAlertCB(const char *pDictName, const char *itemName, void *pUserData)
{
	U32 iID;
	Alert *pAlert = NULL;
	
	if (!StringToUint(itemName, &iID))
	{
		return NULL;
	}

	stashIntFindPointer(sAlertsByUID, iID, &pAlert);

	return pAlert;
}

int GetNumAlertsCB(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	return siTotalAlertCount;
}
int Alerts_GetCountByLevel(enumAlertLevel eLevel)
{
	return siTotalActiveBySeverity[eLevel];
}



bool InitIteratorAlertCB(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->index = 0;
	pIterator->index2 = 0;
	return true;
}

bool GetNextAlertCB(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	KeyedAlertList *pList;
	Alert *pAlert;

	//index2 is the index of which list to use, index is the index within that list
	do
	{
		if (pIterator->index2 >= eaSize(&ppAllKeyedAlertLists))
		{
			return false;
		}

		assert(ppAllKeyedAlertLists);

		pList = ppAllKeyedAlertLists[pIterator->index2];

		if (pIterator->index < eaSize(&pList->ppAlerts))
		{
			break;
		}

		pIterator->index = 0;
		pIterator->index2++;
	} while (1);

	assert(pList->ppAlerts);

	pAlert = pList->ppAlerts[pIterator->index++];

	*ppOutName = pAlert->AlertUIDStringed;
	*ppOutObj = pAlert;

	return true;
}









KeyedAlertList *FindKeyedAlertList(const char *pKey)
{
	KeyedAlertList *pList;

	if (!gKeyedAlertListsByKey)
	{
		return NULL;
	}

	if (stashFindPointer(gKeyedAlertListsByKey, pKey, &pList))
	{
		return pList;
	}

	pList = StructCreate(parse_KeyedAlertList);
	pList->pKey = pKey;
	eaPush(&ppAllKeyedAlertLists, pList);
	stashAddPointer(gKeyedAlertListsByKey, pKey, pList, false);

	return pList;
}

int Alerts_GetCountByKey(const char *pKey)
{
	KeyedAlertList *pList = FindKeyedAlertList(pKey);

	if (pList)
	{
		return eaSize(&pList->ppAlerts);
	}

	return 0;
}

int Alerts_GetAllLevelCount(void)
{
	return siTotalAlertCount;
}


void AcknowledgeAlertByListAndIndex(KeyedAlertList *pList, int iIndex)
{
	Alert *pAlert = pList->ppAlerts[iIndex];

	if (pAlert->iLifespan && spStatedBasedAlertOffCB)
	{
		spStatedBasedAlertOffCB(pAlert);
	}

	siTotalActiveBySeverity[pAlert->eLevel]--;
	stashIntRemovePointer(sAlertsByUID, pAlert->iAlertUID, NULL);
	eaRemove(&pList->ppAlerts, iIndex);
	pList->iCurActiveBySeverity[pAlert->eLevel]--;
	siTotalAlertCount--;
	StructDestroy(parse_Alert, pAlert);
}

void DEFAULT_LATELINK_AlertWasJustAddedToList(Alert *pAlert)
{


}

void DeferredAcknowledgeAlert(U32 iID)
{
	ea32Push(&spDeferredAlertIDsToAcknowledge, iID);
}


int Alerts_GetTotalCountByLevel(enumAlertLevel eLevel)
{
	return siTotalCounts[eLevel];
}


void AddAlertInternal(Alert *pAlert)
{
	KeyedAlertList *pList = FindKeyedAlertList(pAlert->pKey);
	siTotalActiveBySeverity[pAlert->eLevel]++;
	stashIntAddPointer(sAlertsByUID, pAlert->iAlertUID, pAlert, false);
	eaPush(&pList->ppAlerts, pAlert);
	pList->iCurActiveBySeverity[pAlert->eLevel]++;
	pList->iTotalBySeverity[pAlert->eLevel]++;
	siTotalAlertCount++;
	pList->iTotalCount++;
	siTotalCounts[pAlert->eLevel]++;
	
	if (!pList->pEventCounter)
	{
		pList->pEventCounter = EventCounter_Create(timeSecondsSince2000());
	}

	EventCounter_ItHappened(pList->pEventCounter, timeSecondsSince2000());

	pAlert->pList = pList;

	AlertWasJustAddedToList(pAlert);

	if (giMaxAlertsPerKey)
	{
		if (eaSize(&pList->ppAlerts) >= giMaxAlertsPerKey)
		{
			DeferredAcknowledgeAlert(pList->ppAlerts[eaSize(&pList->ppAlerts) - giMaxAlertsPerKey]->iAlertUID);
		}
	}
}


bool RetriggerAlertIfActive(const char *pKey, GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer)
{
	KeyedAlertList *pList = FindKeyedAlertList(pKey);
	int i;

	if (!pList)
	{
		return false;
	}

	for (i=0; i < eaSize(&pList->ppAlerts); i++)
	{
		Alert *pAlert = pList->ppAlerts[i];

		if (pAlert->iIDOfObject == iIDOfObject 
			&& pAlert->eContainerTypeOfObject == eContainerTypeOfObject
			&& pAlert->iIDOfServer == iIDOfServer
			&& pAlert->eContainerTypeOfServer == eContainerTypeOfServer)
		{
			pAlert->iMostRecentHappenedTime = timeSecondsSince2000();
			return true;
		}
	}

	return false;
}


Alert *FindAlertByContents(const char *pKey, const char *pString, enumAlertLevel eLevel, enumAlertCategory eCategory,
	GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer,
	int iErrorID)
{
	KeyedAlertList *pList = FindKeyedAlertList(pKey);
	int i;

	for (i=0; i < eaSize(&pList->ppAlerts); i++)
	{
		Alert *pAlert = pList->ppAlerts[i];

		if (pAlert->iIDOfObject == iIDOfObject 
			&& pAlert->eContainerTypeOfObject == eContainerTypeOfObject
			&& pAlert->iIDOfServer == iIDOfServer
			&& pAlert->eContainerTypeOfServer == eContainerTypeOfServer
			&& pAlert->eLevel == eLevel
			&& pAlert->eCategory == eCategory
			&& pAlert->iErrorID == iErrorID
			&& strcmp(pAlert->pString, pString) == 0)
		{
			return pAlert;
		}
	}
	

	return NULL;
}

static AlertRedirectionCB **sppAlertRedirectCBs = NULL;

void AddAlertRedirectionCB(AlertRedirectionCB *pCB)
{
	eaPush((void***)&sppAlertRedirectCBs, pCB);
}


void TriggerAlertfEx(const char *pKey, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer,
	  const char *pMachineName, int iErrorID, const char *pFileName, int iLineNum, FORMAT_STR const char *pFmt, ... )
{
	char *pFullString = NULL;

	estrGetVarArgs(&pFullString, pFmt);



	TriggerAlertEx(pKey, pFullString, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer,
		iIDOfServer, pMachineName, iErrorID, pFileName, iLineNum);

	estrDestroy(&pFullString);
}

char *DEFAULT_LATELINK_GetMachineNameForAlert(Alert *pAlert)
{
	return (char*)getHostName();
}

void TriggerAlertEx(const char *pKey, const char *pString, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer,
	  const char *pMachineName, int iErrorID, const char *pFileName, int iLineNum )
{
	int i;
	Alert *pAlert;
	char tempString[1024];

	if (!eContainerTypeOfObject)
	{
		eContainerTypeOfObject = GetAppGlobalType();
		iIDOfObject = GetAppGlobalID();
	}
	if (!eContainerTypeOfServer)
	{
		eContainerTypeOfServer = GetAppGlobalType();
		iIDOfServer = GetAppGlobalID();
	}


	if (!pKey || !pKey[0])
	{
		char fileShort[CRYPTIC_MAX_PATH];
		char tempKey[CRYPTIC_MAX_PATH + 50];

		getFileNameNoDir(fileShort, pFileName);
		sprintf(tempKey, "UNSPEC_%s_%d", fileShort, iLineNum);
		pKey = allocAddString(tempKey);


	}
	else
	{
		pKey = allocAddString(pKey);
	}

	if (!pString || !pString[0])
	{
		sprintf(tempString, "Unspecified alert from %s(%d)", pFileName, iLineNum);
		pString = tempString;
	}



	if (!bAlertsInitted)
	{
		InitAlertSystem();
	}

	if (sbPrintfAllAlerts)
	{
		printf("ALERT %s: %s\n", pKey, pString);
	}

	if (sbErrorfOnAlerts)
	{
		Errorf("ALERT %s: %s", pKey, pString);
	}

	for (i=0; i < eaSizeUnsafe(&sppAlertRedirectCBs); i++)
	{
		if ((sppAlertRedirectCBs[i])(pKey, pString, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, 
			eContainerTypeOfServer, iIDOfServer, pMachineName, iErrorID))
		{
			return;
		}
	}


	if (iLifespan)
	{
		pAlert = FindAlertByContents(pKey, pString, eLevel, eCategory, eContainerTypeOfObject, iIDOfObject,
			eContainerTypeOfServer, iIDOfServer, iErrorID);

		if (pAlert)
		{
			pAlert->iMostRecentHappenedTime = timeSecondsSince2000();
			return;
		}

	}


	pAlert = StructCreate(parse_Alert);
	pAlert->eCategory = eCategory;
	pAlert->eContainerTypeOfObject = eContainerTypeOfObject;
	pAlert->iIDOfObject = iIDOfObject;
	pAlert->eContainerTypeOfServer = eContainerTypeOfServer;
	pAlert->iIDOfServer = iIDOfServer;
	pAlert->eLevel = eLevel;
	pAlert->iErrorID = iErrorID;
	pAlert->iLifespan = iLifespan;
	pAlert->iMostRecentHappenedTime = timeSecondsSince2000();
	pAlert->pKey = pKey;
	pAlert->pString = strdup(pString);
	pAlert->pPatchVersion = strdup(GetUsefulVersionString());

	pAlert->iAlertUID = siNextAlertUID++;
	sprintf(pAlert->AlertUIDStringed, "%u", pAlert->iAlertUID);
	
	if (pMachineName)
	{
		pAlert->pMachineName = strdup(pMachineName);
	}
	else
	{
		pAlert->pMachineName = strdup(GetMachineNameForAlert(pAlert));
	}

	AddAlertInternal(pAlert);

	objLogWithStruct(LOG_ALERTS, eContainerTypeOfObject, iIDOfObject, 0, NULL, NULL, NULL, "TriggerAlert", NULL, pAlert, parse_Alert);

	for (i=0; i < eaSizeUnsafe(&ppFixupCBs); i++)
	{
		ppFixupCBs[i](pAlert);
	}
}

void AddFixupAlertCB(FixupAlertCB *pCB)
{
	eaPushUnique((void***)(&ppFixupCBs), pCB);
}



void AcknowledgeAlertByUID(U32 iUID)
{
	Alert *pAlert;

	if (stashIntFindPointer(sAlertsByUID, iUID, &pAlert))
	{
		int iIndex;

		iIndex = eaFind(&pAlert->pList->ppAlerts, pAlert);

		if (iIndex != -1)
		{
			AcknowledgeAlertByListAndIndex(pAlert->pList, iIndex);
		}	
	}
}


void alertsStateBasedUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int iListNum;
	U32 iCurTime = timeSecondsSince2000();

	PERFINFO_AUTO_START_FUNC();


	for (iListNum = 0; iListNum < eaSize(&ppAllKeyedAlertLists); iListNum++)
	{
		KeyedAlertList *pList = ppAllKeyedAlertLists[iListNum];
		int iAlertNum;
		
		for (iAlertNum = eaSize(&pList->ppAlerts)-1; iAlertNum >=0; iAlertNum--)
		{
			Alert *pAlert = pList->ppAlerts[iAlertNum];

			if (pAlert->iLifespan && pAlert->iMostRecentHappenedTime + pAlert->iLifespan < iCurTime)
			{
				AcknowledgeAlertByListAndIndex(pList, iAlertNum);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void acknowledgeDeferredAlerts(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	U32 *spTempIDsToAcknowledge = spDeferredAlertIDsToAcknowledge;
	int i;

	spDeferredAlertIDsToAcknowledge = NULL;

	for (i=0; i < ea32Size(&spTempIDsToAcknowledge); i++)
	{
		AcknowledgeAlertByUID(spTempIDsToAcknowledge[i]);
	}

	ea32Destroy(&spTempIDsToAcknowledge);
}

void InitAlertSystem(void)
{
	ATOMIC_INIT_BEGIN;

	gKeyedAlertListsByKey = stashTableCreateAddress(8);
	sAlertsByUID = stashTableCreateInt(16);

	resRegisterDictionary("Alerts", RESCATEGORY_SYSTEM, 0, parse_Alert,
							GetAlertCB,
							GetNumAlertsCB,
							NULL,
							NULL,
							InitIteratorAlertCB,
							GetNextAlertCB,
							NULL,
							NULL,
							NULL,
							NULL, NULL, NULL, NULL);


	resRegisterDictionaryForStashTable("AlertLists", RESCATEGORY_SYSTEM, RESDICTFLAG_USE_FIXUPTYPE_GOTTEN_FROM_RES_DICT, gKeyedAlertListsByKey, parse_KeyedAlertList);


	TimedCallback_Add(alertsStateBasedUpdate, NULL, 5.0f);
	TimedCallback_Add(acknowledgeDeferredAlerts, NULL, 1.0f);
	TimedCallback_Add(UpdateDeferredAlerts, NULL, 3.0f);

	bAlertsInitted = true;

	InitializeCriticalSection(&sAlertsCriticalSection); 

	ATOMIC_INIT_END;

}

static StateBasedAlertList sStateBasedAlertList = {0};
static StateBasedAlertList **ppGroupedStateBasedAlertLists = NULL;

StateBasedAlertList *FindStateBasedAlertList(int iFrequency)
{
	StateBasedAlertList *pNewList;
	int i;

	for (i=0; i < eaSize(&ppGroupedStateBasedAlertLists); i++)
	{
		if (ppGroupedStateBasedAlertLists[i]->iCheckFrequency == iFrequency)
		{
			return ppGroupedStateBasedAlertLists[i];
		}
	}

	pNewList = StructCreate(parse_StateBasedAlertList);
	pNewList->iCheckFrequency = iFrequency;

	eaPush(&ppGroupedStateBasedAlertLists, pNewList);
	return pNewList;
}

static ExprFuncTable* sSharedFuncTable = NULL;

static StaticDefineInt **sppStaticDefineIntsForStateBasedAlerts = NULL;

void AddStaticDefineIntForStateBasedAlerts(StaticDefineInt *pDefines)
{
	eaPushUnique(&sppStaticDefineIntsForStateBasedAlerts, pDefines);
}

static void Alerts_InitSharedFuncTable(void)
{
	if (!sSharedFuncTable)
	{
		sSharedFuncTable = exprContextCreateFunctionTable("Alerts");
		exprContextAddFuncsToTableByTag(sSharedFuncTable, "util");
		exprContextAddFuncsToTableByTag(sSharedFuncTable, "alerts");
	}
}

bool StateBasedAlert_MaybeTrigger(StateBasedAlert *pAlert, char *pGlobObjName, U32 iCurTime, char *pDescString,
	  GlobalType eObjType, ContainerID eObjID)
{


	if (pAlert->iTimeBeforeTriggering)
	{

		if (pGlobObjName)
		{
			U32 iFirstTimeHappened;

			if (!pAlert->sTriggerTimesByName_Cur)
			{
				pAlert->sTriggerTimesByName_Cur = stashTableCreateWithStringKeys(4, StashDeepCopyKeys_NeverRelease);
			}

			if (!stashFindInt(pAlert->sTriggerTimesByName_Last, pGlobObjName, &iFirstTimeHappened))
			{
				iFirstTimeHappened = iCurTime;
			}

			stashAddInt(pAlert->sTriggerTimesByName_Cur, pGlobObjName, iFirstTimeHappened, true);

			if (iFirstTimeHappened > iCurTime - pAlert->iTimeBeforeTriggering)
			{
				return false;
			}
		}
		else
		{

			if (pAlert->iFirstTimeHappened == 0)
			{
				pAlert->iFirstTimeHappened = iCurTime;
				return false;
			}

			if (pAlert->iFirstTimeHappened > iCurTime - pAlert->iTimeBeforeTriggering)
			{
				return false;
			}
		}
	}

	PERFINFO_AUTO_START("TriggerAlert", 1);

	TriggerAlert(pAlert->pAlertKey, pDescString ? pDescString : pAlert->pAlertString, pAlert->eLevel, pAlert->eCategory,
		pAlert->iLifeSpan ? pAlert->iLifeSpan : pAlert->iCheckFrequency * 2,
		eObjType ? eObjType : GetAppGlobalType(), eObjType ? eObjID : GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), NULL, 0);
	PERFINFO_AUTO_STOP();

	return true;
}



void DEFAULT_LATELINK_StateBasedAlertJustTriggered(StateBasedAlert *pStateBasedAlert, const char *pDescription, const char *pTypeName, const char *pName)
{

}


void StateBasedAlerts_Process(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	StateBasedAlertList *pList = (StateBasedAlertList*)userData;
	int i;
	char *pDescription = NULL;
	U32 iCurTime = timeSecondsSince2000();


	if (!pList->ppAlerts)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();


	Alerts_InitSharedFuncTable();

	estrStackCreate(&pDescription);

	for (i=eaSize(&pList->ppAlerts)-1; i >= 0; i--)
	{
		StateBasedAlert *pStateBasedAlert = pList->ppAlerts[i];
		ParseTable *pTPI = NULL;
		void *pObj;
		char *pName;
		int j;
		ExprContext *pContext = NULL;
		ResourceIterator iterator;

		PERFINFO_RUN(
			PERFINFO_AUTO_START_STATIC(pStateBasedAlert->pAlertKey, &pStateBasedAlert->piProcess, 1);
		);

		if (pStateBasedAlert->pGlobObjType && pStateBasedAlert->pGlobObjType[0])
		{
			pTPI = resDictGetParseTable(pStateBasedAlert->pGlobObjType);

			if (!pTPI)
			{
				Errorf("State Based Alert refers to unknown resource dictionary %s\n", 
					pStateBasedAlert->pGlobObjType);
				StructDestroy(parse_StateBasedAlert, pStateBasedAlert);
				eaRemoveFast(&pList->ppAlerts, i);
				continue;
			}
		}

		pContext = exprContextCreate();
		exprContextSetFuncTable(pContext, sSharedFuncTable);

		for (j=0; j < eaSize(&sppStaticDefineIntsForStateBasedAlerts); j++)
		{
			exprContextAddStaticDefineIntAsVars(pContext, sppStaticDefineIntsForStateBasedAlerts[j], NULL);
		}

		if (pStateBasedAlert->pGlobObjType && pStateBasedAlert->pGlobObjType[0])
		{
		
			resInitIterator(pStateBasedAlert->pGlobObjType, &iterator);

			while (1)	
			{
				MultiVal answer = {0};
				int iAnswer;
				bool bRes;
		
				PERFINFO_AUTO_START("call_resIteratorGetNext", 1);
				bRes = resIteratorGetNext(&iterator, &pName, &pObj);
				PERFINFO_AUTO_STOP();
			
				if (!bRes)
				{
					break;
				}


				exprContextSetPointerVar(pContext, "me", pObj, pTPI, false, true);

				PERFINFO_AUTO_START("call_ExprGenerate", 1);

				if (!pStateBasedAlert->pExpression)
				{
					pStateBasedAlert->pExpression = exprCreate();

					exprGenerateFromString(pStateBasedAlert->pExpression, pContext, pStateBasedAlert->pExpressionString, NULL);
				}
				PERFINFO_AUTO_STOP();

				PERFINFO_AUTO_START("call_ExprEvaluate", 1);
				exprEvaluate(pStateBasedAlert->pExpression, pContext, &answer);
				PERFINFO_AUTO_STOP();
			

				iAnswer = QuickGetInt(&answer);

				if (iAnswer)
				{
					GlobalType eType = 0;
					ContainerID iID = 0;
					char *pVerboseName;

					estrStackCreate(&pVerboseName);

					PERFINFO_AUTO_START("resGetVerboseObjectName", 1);
					resGetVerboseObjectName(pStateBasedAlert->pGlobObjType, pName, &pVerboseName);
					PERFINFO_AUTO_STOP();

					PERFINFO_AUTO_START("resFindGlobalTypeAndID", 1);
					resFindGlobalTypeAndID(pStateBasedAlert->pGlobObjType, pName, &eType, &iID);
					PERFINFO_AUTO_STOP();


					estrPrintf(&pDescription, FORMAT_OK(pStateBasedAlert->pAlertString), pVerboseName);

					if (StateBasedAlert_MaybeTrigger(pStateBasedAlert, pName, iCurTime, pDescription, eType, iID))
					{
						StateBasedAlertJustTriggered(pStateBasedAlert, pDescription, pStateBasedAlert->pGlobObjType, pName);
					}

					estrDestroy(&pVerboseName);
				}
			}
			resFreeIterator(&iterator);

			stashTableDestroySafe(&pStateBasedAlert->sTriggerTimesByName_Last);
			pStateBasedAlert->sTriggerTimesByName_Last = pStateBasedAlert->sTriggerTimesByName_Cur;
			pStateBasedAlert->sTriggerTimesByName_Cur = NULL;
		}
		else
		{
			MultiVal answer = {0};
			int iAnswer;
	
			PERFINFO_AUTO_START("call_ExprGenerate", 1);

			if (!pStateBasedAlert->pExpression)
			{
				pStateBasedAlert->pExpression = exprCreate();
				exprGenerateFromString(pStateBasedAlert->pExpression, pContext, pStateBasedAlert->pExpressionString, NULL);
			}
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_START("call_ExprEvaluate", 1);
			exprEvaluate(pStateBasedAlert->pExpression, pContext, &answer);
			PERFINFO_AUTO_STOP();
	
			iAnswer = QuickGetInt(&answer);

			if (iAnswer)
			{
				StateBasedAlert_MaybeTrigger(pStateBasedAlert, NULL, iCurTime, NULL, 0, 0);
			}
			else
			{
				pStateBasedAlert->iFirstTimeHappened = 0;
			}		
		}
		exprContextDestroy(pContext);
		

		PERFINFO_AUTO_STOP();
	}


	estrDestroy(&pDescription);

	PERFINFO_AUTO_STOP();

}

//in reloading mode, if there is an error, it returns the error string. In non-reloading mode, it alerts,
//but returns nothing. Only if something is returned should the process abort
char *LoadStateBasedAlertFile(char *pDirectory, char *pFileName, StateBasedAlertList *pList, bool bReloading)
{
	if (!ParserLoadFiles(pDirectory, pFileName, NULL, PARSER_OPTIONALFLAG, parse_StateBasedAlertList, pList))
	{
		char *pTempString = NULL;
		StateBasedAlertList *pTempList = StructCreate(parse_StateBasedAlertList);
		ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
		ParserLoadFiles(pDirectory, pFileName, NULL, PARSER_OPTIONALFLAG, parse_StateBasedAlertList, pTempList);
		ErrorfPopCallback();
		StructDestroy(parse_StateBasedAlertList, pTempList);

		if (bReloading)
		{
			static char *spRetVal = NULL;
			estrPrintf(&spRetVal, "Text Parser error while reading %s: %s", pFileName, pTempString);
			estrDestroy(&pTempString);
			return spRetVal;
		}

		WARNING_NETOPS_ALERT("STATEBASEDALERT_ERROR", "Text Parser error while reading %s: %s", pFileName, pTempString);
		estrDestroy(&pTempString);
		return NULL;
	}

	return NULL;
}

void ClearStateBasedAlerts(void)
{
	FOR_EACH_IN_EARRAY(ppGroupedStateBasedAlertLists, StateBasedAlertList, pList)
	{
		eaDestroy(&pList->ppAlerts); //these are not owned here, so don't free them
	}
	FOR_EACH_END;

	eaDestroyStruct(&sStateBasedAlertList.ppAlerts, parse_StateBasedAlert);
}

//during reloading, if there are any errors, we just do nothing and leave things as they already were
char *BeginStateBasedAlerts(char *pDirectory, char **ppFileNames, bool bReloading)
{
	int iFileNum;
	int i;
	char *pErrorString = NULL;

	static StateBasedAlertList *pTempList = NULL;

	if (ppGroupedStateBasedAlertLists && !bReloading)
	{
		//prevent double-initialization
		return NULL;
	}

	if (!pDirectory)
	{
		return NULL;
	}

	if (!eaSize(&ppFileNames))
	{
		return NULL;
	}

	if (pTempList)
	{
		StructReset(parse_StateBasedAlertList, pTempList);
	}
	else
	{
		pTempList = StructCreate(parse_StateBasedAlertList);
	}

	if ((pErrorString = LoadStateBasedAlertFile(pDirectory, ppFileNames[0], pTempList, bReloading)))
	{
		return pErrorString;
	}

	for (iFileNum = 1; iFileNum < eaSize(&ppFileNames); iFileNum++)
	{
		StateBasedAlertList sOverrideList = {0};

		if ((pErrorString = LoadStateBasedAlertFile(pDirectory, ppFileNames[iFileNum], &sOverrideList, bReloading)))
		{
			StructDeInit(parse_StateBasedAlertList, &sOverrideList);
			return pErrorString;
		}

		FOR_EACH_IN_EARRAY(sOverrideList.ppAlerts, StateBasedAlert, pOverrideAlert)
		{
			FOR_EACH_IN_EARRAY(pTempList->ppAlerts, StateBasedAlert, pAlert)
			{
				if (pOverrideAlert->pAlertKey == pAlert->pAlertKey)
				{
					StructCopy(parse_StateBasedAlert, pOverrideAlert, pAlert, 0, 0, 0);
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		StructDeInit(parse_StateBasedAlertList, &sOverrideList);
	}

	//if we get this far, then we know we didn't have any textparser issues
	ClearStateBasedAlerts(); //will do nothing when not reloading, that's fine
	StructCopy(parse_StateBasedAlertList, pTempList, &sStateBasedAlertList, 0, 0, 0);
	StructDestroySafe(parse_StateBasedAlertList, &pTempList);


	for (i=eaSize(&sStateBasedAlertList.ppAlerts)-1; i >= 0; i--)
	{
		StateBasedAlertList *pList;
		StateBasedAlert *pStateBasedAlert = sStateBasedAlertList.ppAlerts[i];

		eaRemoveFast(&sStateBasedAlertList.ppAlerts, i);
		pList = FindStateBasedAlertList(pStateBasedAlert->iCheckFrequency);

		eaPush(&pList->ppAlerts, pStateBasedAlert);
	}

	StructDeInit(parse_StateBasedAlertList, &sStateBasedAlertList);

	for (i=0; i < eaSize(&ppGroupedStateBasedAlertLists); i++)
	{
		if (!ppGroupedStateBasedAlertLists[i]->bTimedCallbackAdded)
		{
			TimedCallback_Add(StateBasedAlerts_Process, ppGroupedStateBasedAlertLists[i], (float)(ppGroupedStateBasedAlertLists[i]->iCheckFrequency));
			ppGroupedStateBasedAlertLists[i]->bTimedCallbackAdded = true;
		}
	}

	return NULL;
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void TestTriggerAlert(ACMD_NAMELIST(enumAlertLevelEnum, STATICDEFINE) char *pLevel, ACMD_NAMELIST(enumAlertCategoryEnum, STATICDEFINE) char *pCategory, ACMD_SENTENCE pString)
{
	enumAlertLevel eLevel = StaticDefineIntGetInt(enumAlertLevelEnum, pLevel);
	enumAlertCategory eCategory = StaticDefineIntGetInt(enumAlertCategoryEnum, pCategory);

	if (eLevel == -1)
	{
		eLevel = ALERTLEVEL_WARNING;
	}

	if (eCategory == -1)
	{
		eCategory = ALERTCATEGORY_NETOPS;
	}

	TriggerAlert(allocAddString("TESTALERT"), pString, eLevel, eCategory, 0, GLOBALTYPE_NONE, 0, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0);
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void TestAlertWithKey(char *pKey)
{
	TriggerAlert(allocAddString(pKey), pKey, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_NONE, 0, GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0);
}

void spamAlerts(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	TriggerAlert("SPAM1", "This is horrible spam", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
	TriggerAlert("SPAM2", "This is horrible spam", ALERTLEVEL_CRITICAL, ALERTCATEGORY_GAMEPLAY, 0, 0, 0, 0, 0, NULL, 0);
	TriggerAlert("SPAM3", "This is horrible spam", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
	TriggerAlert("SPAM4", "This is horrible spam", ALERTLEVEL_WARNING, ALERTCATEGORY_GAMEPLAY, 0, 0, 0, 0, 0, NULL, 0);
}

AUTO_COMMAND;
void BeginSpammingAlerts(void)
{
	TimedCallback_Add(spamAlerts, NULL, 0.1f);
}



void AcknowledgeAllAlertsByKey(char *pKey)
{
	KeyedAlertList *pList = FindKeyedAlertList(allocAddString(pKey));

	if (pList)
	{
		while(eaSize(&pList->ppAlerts))
		{
			AcknowledgeAlertByListAndIndex(pList, 0);
		}
	}
}

AUTO_COMMAND;
void AcknowledgeAllDuplicateAlerts(void)
{
	int i;

	for (i=0; i < eaSize(&ppAllKeyedAlertLists); i++)
	{
		while (eaSize(&ppAllKeyedAlertLists[i]->ppAlerts) > 1)
		{
			AcknowledgeAlertByListAndIndex(ppAllKeyedAlertLists[i], 0);
		}
	}
}

AUTO_COMMAND;
void TestDeferredAlerts(void)
{
	int i;

	for (i=0; i < 10; i++)
	{
		ErrorOrAlertDeferred(i % 2, "TEST_DEFERRED_ALERT", "this is test alert %d", i);
	}
}


AUTO_STRUCT;
typedef struct AutoGroupingAlert
{
	const char *pKey; AST(KEY, POOL_STRING)
	int iCount;
	char *pFullString; AST(ESTRING)
	enumAlertLevel eLevel;
	enumAlertCategory eCategory;
} AutoGroupingAlert;

StashTable sAutoGroupingaAlertsByKey = NULL;

void AutoGroupingAlertCB(TimedCallback *callback, F32 timeSinceLastCallback, void *pKey)
{
	AutoGroupingAlert *pGroupingAlert;
	
	if (!stashRemovePointer(sAutoGroupingaAlertsByKey, pKey, &pGroupingAlert))
	{
		assert(0);
	}

	TriggerAlertf(pGroupingAlert->pKey, pGroupingAlert->eLevel, pGroupingAlert->eCategory, 0, 0, 0, 0, 0, NULL, 0, "%d alerts autogrouped:\n%s",
		pGroupingAlert->iCount, pGroupingAlert->pFullString);

	StructDestroy(parse_AutoGroupingAlert, pGroupingAlert);
}


void TriggerAutoGroupingAlert(const char *pKey_in, enumAlertLevel eLevel, enumAlertCategory eCategory, 
	int iGroupingTime, FORMAT_STR const char *pFmtString, ...)
{
	const char *pPooledKey = allocAddString(pKey_in);
	char *pFullString = NULL;
	AutoGroupingAlert *pGroupingAlert = NULL;

	estrGetVarArgs(&pFullString, pFmtString);


	if (!sAutoGroupingaAlertsByKey)
	{
		sAutoGroupingaAlertsByKey = stashTableCreateAddress(16);
	}

	if (!stashFindPointer(sAutoGroupingaAlertsByKey, pPooledKey, &pGroupingAlert))
	{
		pGroupingAlert = StructCreate(parse_AutoGroupingAlert);
		pGroupingAlert->pKey = pPooledKey;
		pGroupingAlert->eLevel = eLevel;
		pGroupingAlert->eCategory = eCategory;
		stashAddPointer(sAutoGroupingaAlertsByKey, pPooledKey, pGroupingAlert, false);
		TimedCallback_Run(AutoGroupingAlertCB, (void*)pPooledKey, iGroupingTime);
	}

	estrConcatf(&pGroupingAlert->pFullString, "%s\n", pFullString);
	pGroupingAlert->iCount++;
}

AUTO_STRUCT;
typedef struct DeferredAlert
{
	const char *pKey; AST(POOL_STRING)
	char *pString; AST(ESTRING)
	enumAlertLevel eLevel;
	enumAlertCategory eCategory;
} DeferredAlert;

DeferredAlert **sppDeferredAlerts = NULL;


bool DEFAULT_LATELINK_SystemIsReadyForAlerts(void)
{
	return true;
}

void UpdateDeferredAlerts(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	DeferredAlert **sppListCopy;

	if (!SystemIsReadyForAlerts())
	{
		return;
	}

	EnterCriticalSection(&sAlertsCriticalSection);

	if (!eaSize(&sppDeferredAlerts))
	{	
		LeaveCriticalSection(&sAlertsCriticalSection);
		return;
	}

	sppListCopy = sppDeferredAlerts;
	sppDeferredAlerts = NULL;

	LeaveCriticalSection(&sAlertsCriticalSection);



	FOR_EACH_IN_EARRAY(sppListCopy, DeferredAlert, pAlert)
	{
		TriggerAlert(pAlert->pKey, pAlert->pString, pAlert->eLevel, pAlert->eCategory, 0, 0, 0, 0, 0, 0, 0);
	}
	FOR_EACH_END

	eaDestroyStruct(&sppListCopy, parse_DeferredAlert);
}


void TriggerAlertDeferred(const char *pKey, enumAlertLevel eLevel, enumAlertCategory eCategory, FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;
	DeferredAlert *pDeferredAlert = StructCreate(parse_DeferredAlert);

	if (!bAlertsInitted)
	{
		InitAlertSystem();
	}

	estrGetVarArgs(&pFullString, pFmt);

	pDeferredAlert->pKey = allocAddString(pKey);
	pDeferredAlert->pString = pFullString;
	pFullString = NULL;
	pDeferredAlert->eLevel = eLevel;
	pDeferredAlert->eCategory = eCategory;

	EnterCriticalSection(&sAlertsCriticalSection);
	eaPush(&sppDeferredAlerts, pDeferredAlert);
	LeaveCriticalSection(&sAlertsCriticalSection);

}

void TriggerAlertByStruct(Alert *pAlert)
{
	int i;

	if (!bAlertsInitted)
	{
		InitAlertSystem();
	}

	if (pAlert->iLifespan != 0)
	{
		AssertOrAlert("BAD_ALERT_LIFESPAN", "Someone tried to trigger a lifespan alert (key %s) via TriggerAlertByStruct, not allowed",
			pAlert->pKey);

		pAlert->iLifespan = 0;
	}

	pAlert->iAlertUID = siNextAlertUID++;
	sprintf(pAlert->AlertUIDStringed, "%u", pAlert->iAlertUID);
	pAlert->iMostRecentHappenedTime = timeSecondsSince2000();

	if (sbPrintfAllAlerts)
	{
		printf("ALERT %s: %s\n", pAlert->pKey, pAlert->pString);
	}

	if (sbErrorfOnAlerts)
	{
		Errorf("ALERT %s: %s", pAlert->pKey, pAlert->pString);
	}

	for (i=0; i < eaSizeUnsafe(&sppAlertRedirectCBs); i++)
	{
		if ((sppAlertRedirectCBs[i])(pAlert->pKey, pAlert->pString, pAlert->eLevel, pAlert->eCategory, 0, pAlert->eContainerTypeOfObject, 
			pAlert->iIDOfObject, 
			pAlert->eContainerTypeOfServer, pAlert->iIDOfServer, pAlert->pMachineName, pAlert->iErrorID))
		{
			return;
		}
	}

	AddAlertInternal(pAlert);

	objLogWithStruct(LOG_ALERTS, pAlert->eContainerTypeOfObject, pAlert->iIDOfObject, 0, NULL, NULL, NULL, "TriggerAlert", NULL, pAlert, parse_Alert);

	for (i=0; i < eaSizeUnsafe(&ppFixupCBs); i++)
	{
		ppFixupCBs[i](pAlert);
	}
}



#include "alerts_h_ast.c"
#include "alerts_c_ast.c"
