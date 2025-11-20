#include "NewControllerTracker_AlertTrackers.h"
#include "NewControllerTracker_AlertTrackers_h_ast.h"
#include "alerts_h_ast.h"
#include "stashTable.h"
#include "estring.h"
#include "earray.h"
#include "resourceInfo.h"
#include "timing.h"
#include "eventCountingHeatMap.h"
#include "newControllerTracker_CriticalSystems.h"
#include "NameList.h"
#include "StringCache.h"
#include "utils.h"
#include "file.h"
#include "TimedCallback.h"
#include "EventCountingHeatMap.h"
#include "StringUtil.h"

#define MAX_FULL_TEXTS_PER_ALERT 30

StashTable sAlertTrackersByKey = NULL;
AlertTrackerList *spAlertTrackerList = NULL;

#define ALERTTRACKERS_FILE "c:\\CriticalSystems\\AlertComments.txt"

void WriteOutAlertTrackers(void);

AUTO_FIXUPFUNC;
TextParserResult AlertTracker_Fixup(AlertTracker *pTracker, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		pTracker->pCounter = EventCounter_Create(timeSecondsSince2000());
		break;


	case FIXUPTYPE_DESTRUCTOR:
		EventCounter_Destroy(pTracker->pCounter);
		break;

	case FIXUPTYPE_GOTTEN_FROM_RES_DICT:
		{
			U32 iCurTime = timeSecondsSince2000();
			int i;
			pTracker->iTotalCount = EventCounter_GetTotalTotal(pTracker->pCounter);
			pTracker->iLast15Minutes = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULL15MINUTES, iCurTime);
			pTracker->iLastHour = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime);
			pTracker->iLast6Hours = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULL6HOURS, iCurTime);
			pTracker->iLastDay = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULLDAY, iCurTime);

			if (eaSize(&pTracker->ppNetopsComments) == 0)
			{
				estrPrintf(&pTracker->pComment_ForServerMonitoring, "(No comment)");
				estrDestroy(&pTracker->pCommentType_ForServerMonitoring);
			}
			else if (eaSize(&pTracker->ppNetopsComments) == 1)
			{
				estrCopy2(&pTracker->pComment_ForServerMonitoring, pTracker->ppNetopsComments[0]->pText ? pTracker->ppNetopsComments[0]->pText : "");
				estrCopy2(&pTracker->pCommentType_ForServerMonitoring, StaticDefineIntRevLookup(AlertTrackerNetopsCommentTypeEnum, pTracker->ppNetopsComments[0]->eType));
			}
			else
			{
				estrPrintf(&pTracker->pComment_ForServerMonitoring, "(%d comments)", eaSize(&pTracker->ppNetopsComments));
				estrDestroy(&pTracker->pCommentType_ForServerMonitoring);
			}


			if (eaSize(&pTracker->ppCountsPerSystem))
			{
				estrDestroy(&pTracker->pSystemsString_ForServerMonitoring);
				for (i = 0; i < eaSize(&pTracker->ppCountsPerSystem); i++)
				{
					estrConcatf(&pTracker->pSystemsString_ForServerMonitoring, "%s%s", i == 0 ? "" : ", ", pTracker->ppCountsPerSystem[i]->pCritSysName);
				}
			}
			else
			{
				estrPrintf(&pTracker->pSystemsString_ForServerMonitoring, "(none)");
			}
		}
		break;
	}

	return true;
}


AUTO_FIXUPFUNC;
TextParserResult AlertCounts_SingleCritSys_Fixup(AlertCounts_SingleCritSys *pTracker, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pTracker->pCounter)
		{
			EventCounter_Destroy(pTracker->pCounter);
		}
		break;

	case FIXUPTYPE_GOTTEN_FROM_RES_DICT:
		{
			U32 iCurTime = timeSecondsSince2000();
			pTracker->iTotalCount = EventCounter_GetTotalTotal(pTracker->pCounter);
			pTracker->iLast15Minutes = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULL15MINUTES, iCurTime);
			pTracker->iLastHour = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime);
			pTracker->iLast6Hours = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULL6HOURS, iCurTime);
			pTracker->iLastDay = EventCounter_GetCount(pTracker->pCounter, EVENTCOUNT_LASTFULLDAY, iCurTime);
		}
		break;
	}

	return true;
}

AlertTracker *FindOrCreateAlertTracker(Alert *pAlert, U32 iCurTime)
{
	AlertTracker *pRetVal;
	if (stashFindPointer(sAlertTrackersByKey, pAlert->pKey, &pRetVal))
	{
		//an alert tracker can be created with category/level NONE if a comment is being set for an alert that hasn't yet
		//happened. If so, when we actually get a copy of that alert, update the tracker
		if (pRetVal->eCategory == ALERTCATEGORY_NONE && pAlert->eCategory != ALERTCATEGORY_NONE 
			|| pRetVal->eLevel == ALERTLEVEL_NONE && pAlert->eLevel != ALERTLEVEL_NONE)
		{
			pRetVal->pAlertKey = pAlert->pKey;
			pRetVal->eCategory = pAlert->eCategory;
			WriteOutAlertTrackers();
		}

		return pRetVal;
	}

	pRetVal = StructCreate(parse_AlertTracker);
	pRetVal->pAlertKey = pAlert->pKey;
	pRetVal->eCategory = pAlert->eCategory;
	pRetVal->eLevel = pAlert->eLevel;

	stashAddPointer(sAlertTrackersByKey, pRetVal->pAlertKey, pRetVal, false);
	eaPush(&spAlertTrackerList->ppTrackers, pRetVal);
	WriteOutAlertTrackers();

	return pRetVal;
}

void SetLinkStringForSingleCritSys(AlertCounts_SingleCritSys *pSingle, CriticalSystem_Status *pSystem, AlertTracker *pTracker)
{
	if (pSystem->pLink1 && !pSingle->pLink)
	{
		char *pActualLink = NULL;
		estrCopy2(&pActualLink, pSystem->pLink1);
		estrTruncateAtLastOccurrence(&pActualLink, '"');
		estrRemoveUpToFirstOccurrence(&pActualLink, '"');

		estrPrintf(&pSingle->pLink, "<a href=\"%s/viewxpath?svrFieldsToShow=&svrFilter=me.key+%%3D+%%22%s%%22&xpath=Controller[1].globobj.alerts\">Link</a>",
			pActualLink, pTracker->pAlertKey);

		estrDestroy(&pActualLink);
	}
}

AlertCounts_SingleCritSys *FindOrCreateCritSysCounts(AlertTracker *pTracker, CriticalSystem_Status *pSystem, U32 iCurTime)
{
	AlertCounts_SingleCritSys *pRetVal = eaIndexedGetUsingString(&pTracker->ppCountsPerSystem, pSystem->pName_Internal);
	char *pActualLink = NULL;
	if (pRetVal)
	{
		//sometimes pLink1 wasn't yet set when the tracker is created, so it takes a second or third try
		SetLinkStringForSingleCritSys(pRetVal, pSystem, pTracker);

		return pRetVal;
	}

	pRetVal = StructCreate(parse_AlertCounts_SingleCritSys);
	pRetVal->pCritSysName = strdup(pSystem->pName_Internal);

	SetLinkStringForSingleCritSys(pRetVal, pSystem, pTracker);

	pRetVal->pCounter = EventCounter_Create(iCurTime);

	eaPush(&pTracker->ppCountsPerSystem, pRetVal);

	return pRetVal;
}

void AlertTrackers_TrackAlert(Alert *pAlert, CriticalSystem_Status *pSystem)
{
	AlertTracker *pTracker;
	AlertCounts_SingleCritSys *pCritSysCounts;
	AlertTextsPerSystem *pTextsPerSystem;

	U32 iCurTime = timeSecondsSince2000();
	int i;


	pTracker = FindOrCreateAlertTracker(pAlert, iCurTime);
	pCritSysCounts = FindOrCreateCritSysCounts(pTracker, pSystem, iCurTime);

	EventCounter_ItHappened(pTracker->pCounter, iCurTime);
	EventCounter_ItHappened(pCritSysCounts->pCounter, iCurTime);

	pTextsPerSystem = eaIndexedGetUsingString(&pTracker->ppTextsPerSystem, pSystem->pName_Internal);
	if (!pTextsPerSystem)
	{
		pTextsPerSystem = StructCreate(parse_AlertTextsPerSystem);
		pTextsPerSystem->pCritSysName = strdup(pSystem->pName_Internal);
		eaPush(&pTracker->ppTextsPerSystem, pTextsPerSystem);
	}

	if (eaSize(&pTextsPerSystem->ppRecentFullTexts) == MAX_FULL_TEXTS_PER_ALERT)
	{
		free(pTextsPerSystem->ppRecentFullTexts[0]);
		eaRemove(&pTextsPerSystem->ppRecentFullTexts, 0);
	}

	eaPush(&pTextsPerSystem->ppRecentFullTexts, strdupf("%s\n%s", timeGetLocalDateStringFromSecondsSince2000(pAlert->iMostRecentHappenedTime), pAlert->pString));

	for (i = eaSize(&pTracker->ppNetopsComments) - 1; i >= 0; i--)
	{
		AlertTrackerNetopsComment *pComment = pTracker->ppNetopsComments[i];
		if (pComment->eType == COMMENTTYPE_TIMED_RESETTING)
		{
			pComment->iCommentCreationTime = iCurTime;
		}
	}
}

static void UpdateAlertTrackerTimeouts(AlertTracker *pTracker)
{
	int i;

	for (i = eaSize(&pTracker->ppNetopsComments) - 1; i >= 0; i--)
	{
		AlertTrackerNetopsComment *pComment = pTracker->ppNetopsComments[i];

		if (pComment->eType == COMMENTTYPE_TIMED || pComment->eType == COMMENTTYPE_TIMED_RESETTING)
		{
			if (timeSecondsSince2000() - pComment->iLifespan > pComment->iCommentCreationTime)
			{
				StructDestroy(parse_AlertTrackerNetopsComment, pComment);
				eaRemoveFast(&pTracker->ppNetopsComments, i);
			}
		}
	}
}

void AlertTrackers_PeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	FOR_EACH_IN_EARRAY(spAlertTrackerList->ppTrackers, AlertTracker, pTracker)
	{
		UpdateAlertTrackerTimeouts(pTracker);
	}
	FOR_EACH_END

	WriteOutAlertTrackers();
}

void AlertTrackers_InitSystem(void)
{
	NameList *pAlertTrackerNetopsCommentType = CreateNameList_StaticDefine((StaticDefine*)AlertTrackerNetopsCommentTypeEnum);
	NameList_AssignName(pAlertTrackerNetopsCommentType, "AlertTrackerNetopsCommentType");

	sAlertTrackersByKey = stashTableCreateAddress(64);
	resRegisterDictionaryForStashTable("AlertTrackers", RESCATEGORY_SYSTEM, RESDICTFLAG_USE_FIXUPTYPE_GOTTEN_FROM_RES_DICT, sAlertTrackersByKey, parse_AlertTracker);

	spAlertTrackerList = StructCreate(parse_AlertTrackerList);

	if (fileExists(ALERTTRACKERS_FILE))
	{
		int i;
		ParserReadTextFile(ALERTTRACKERS_FILE, parse_AlertTrackerList, spAlertTrackerList, 0);

		for (i=eaSize(&spAlertTrackerList->ppTrackers)-1; i >=0; i--)
		{
			AlertTracker *pTracker = spAlertTrackerList->ppTrackers[i];
			UpdateAlertTrackerTimeouts(pTracker);
			if (eaSize(&pTracker->ppNetopsComments) == 0)
			{
				StructDestroy(parse_AlertTracker, pTracker);
				eaRemoveFast(&spAlertTrackerList->ppTrackers, i);
			}
			else
			{
				stashAddPointer(sAlertTrackersByKey, pTracker->pAlertKey, pTracker, false);
			}
		}
	}

	TimedCallback_Add(AlertTrackers_PeriodicUpdate, NULL, 3600.0f);
}

AlertTrackerNetopsComment *FindCommentByID(AlertTracker *pTracker, int iID, int *pOutIndex)
{
	int i;

	for (i=0; i < eaSize(&pTracker->ppNetopsComments); i++)
	{
		if (pTracker->ppNetopsComments[i]->iID == iID)
		{
			if (pOutIndex)
			{
				*pOutIndex = i;
			}
			return pTracker->ppNetopsComments[i];
		}
	}

	return NULL;
}

int GetLowestUnusedCommentID(AlertTracker *pTracker)
{
	int iRetVal = 1;

	while (1)
	{
		if (!FindCommentByID(pTracker, iRetVal, NULL))
		{
			return iRetVal;
		}

		iRetVal++;
	}
}

void WriteOutAlertTrackers(void)
{
	mkdirtree_const(ALERTTRACKERS_FILE);
	ParserWriteTextFile(ALERTTRACKERS_FILE, parse_AlertTrackerList, spAlertTrackerList, 0, 0);
}

AUTO_COMMAND;
char *AddCommentForAlertTracker(char *pTrackerKey_in, char *pTypeName ACMD_NAMELIST(AlertTrackerNetopsCommentTypeEnum, STATICDEFINE), int iDaysDuration, char *pCategoryOrSystemNames, ACMD_SENTENCE pText)
{
	const char *pKey = allocAddString(pTrackerKey_in);
	AlertTracker *pTracker;
	AlertTrackerNetopsComment *pComment;
	AlertTrackerNetopsCommentType eType;

	char **ppCategoryOrSystems = NULL;

	int i;

	static char *pRetString = NULL;


	if (!stashFindPointer(sAlertTrackersByKey, pKey, &pTracker))
	{
		Alert dummy = {0};

		//if someone tries to add a comment for an alert that doesn't yet exist, just add it by creating a dummy alert
		dummy.pKey = allocAddString(pKey);
		pTracker = FindOrCreateAlertTracker(&dummy, timeSecondsSince2000());
	}

	eType = StaticDefineIntGetInt(AlertTrackerNetopsCommentTypeEnum, pTypeName);

	if (eType == -1)
	{
		return "Unknown type";
	}

	if (eType == COMMENTTYPE_PERMANENT && iDaysDuration || eType != COMMENTTYPE_PERMANENT && iDaysDuration <= 0)
	{
		return "type and duration don't match... non-permanent types must have positive durations";
	}

	if (stricmp(pCategoryOrSystemNames, "all") != 0)
	{
		DivideString(pCategoryOrSystemNames, ", ", &ppCategoryOrSystems, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

		for (i = 0; i < eaSize(&ppCategoryOrSystems); i++)
		{
			if (!IsCategoryOrSystemName(ppCategoryOrSystems[i]))
			{
				estrPrintf(&pRetString, "Unknown category or system: %s", ppCategoryOrSystems[i]);
				eaDestroyEx(&ppCategoryOrSystems, NULL);
				return pRetString;
			}
		}
	}

	pComment = StructCreate(parse_AlertTrackerNetopsComment);
	pComment->pParentKey = pTracker->pAlertKey;
	pComment->iID = GetLowestUnusedCommentID(pTracker);
	pComment->eType = eType;
	pComment->iLifespan = iDaysDuration * 24 * 60 * 60;
	pComment->iCommentCreationTime = timeSecondsSince2000();
	pComment->ppSystemOrCategoryNames = ppCategoryOrSystems;
	pComment->pText = strdup(pText);

	eaPush(&pTracker->ppNetopsComments, pComment);

	WriteOutAlertTrackers();

	return "Comment added successfully";


}



AUTO_COMMAND;
char *SetRedirectForAlertTracker(char *pTrackerKey_in, ACMD_SENTENCE pText)
{
	const char *pKey = allocAddString(pTrackerKey_in);
	AlertTracker *pTracker;


	if (!stashFindPointer(sAlertTrackersByKey, pKey, &pTracker))
	{
		return "can't find that alert tracker";
	}

	if (!pText || StringIsAllWhiteSpace(pText))
	{
		estrDestroy(&pTracker->pRedirectAddress);
	}
	else
	{
		estrCopy2(&pTracker->pRedirectAddress, pText);
		estrTrimLeadingAndTrailingWhitespace(&pTracker->pRedirectAddress);
	}

	WriteOutAlertTrackers();
	
	if (pTracker->pRedirectAddress)
	{
		return "Redirect address set";
	}
	else
	{
		return "Redirect address cleared";
	}
}

char *AlertTrackers_GetRedirectAddressForAlert(const char *pAlertKey)
{
	const char *pKey = allocAddString(pAlertKey);
	AlertTracker *pTracker;


	if (!stashFindPointer(sAlertTrackersByKey, pKey, &pTracker))
	{
		return NULL;
	}

	return pTracker->pRedirectAddress;
}

AUTO_COMMAND;
void RemoveComment(char *pKey, int iID)
{
	AlertTracker *pTracker;
	int iIndex;
	AlertTrackerNetopsComment *pComment;

	if (!stashFindPointer(sAlertTrackersByKey, allocAddString(pKey), &pTracker))
	{
		return;
	}

	pComment = FindCommentByID(pTracker, iID, &iIndex);

	if (pComment)
	{
		eaRemove(&pTracker->ppNetopsComments, iIndex);
		StructDestroy(parse_AlertTrackerNetopsComment, pComment);
	}

	WriteOutAlertTrackers();

}

char *AlertTrackers_GetCommentsForAlert(const char *pAlertKey, char **ppSystemOrCategoryNames, bool bExpandList)
{

	static char *pRetString = NULL;
	AlertTracker *pTracker;
	int i;
	char **ppListToUse = NULL;

	if (!stashFindPointer(sAlertTrackersByKey, allocAddString(pAlertKey), &pTracker))
	{
		return NULL;
	}

	if (bExpandList)
	{
		ExpandListOfCategoryOrSystemNames(ppSystemOrCategoryNames, &ppListToUse);
	}
	else
	{
		eaCopy(&ppListToUse, &ppSystemOrCategoryNames);
	}


	estrDestroy(&pRetString);

	if (eaSize(&ppListToUse))
	{
		FOR_EACH_IN_EARRAY(pTracker->ppNetopsComments, AlertTrackerNetopsComment, pComment)
		{
			if (eaSize(&pComment->ppSystemOrCategoryNames) == 0)
			{
				estrConcatf(&pRetString, "%sNETOPS COMMENT: %s", estrLength(&pRetString) == 0 ? "" : "\r\n\r\n", pComment->pText);
			}
			else
			{
				for (i = 0; i < eaSize(&pComment->ppSystemOrCategoryNames); i++)
				{
					if (eaFindString(&ppListToUse, pComment->ppSystemOrCategoryNames[i]) != -1)
					{
						estrConcatf(&pRetString, "%sNETOPS COMMENT (specific to category %s): %s", estrLength(&pRetString) == 0 ? "" : "\r\n\r\n", 
							pComment->ppSystemOrCategoryNames[i], pComment->pText);
						break;
					}
				}
			}
		}
		FOR_EACH_END
	}
	else
	{
		FOR_EACH_IN_EARRAY(pTracker->ppNetopsComments, AlertTrackerNetopsComment, pComment)
		{
			if (eaSize(&pComment->ppSystemOrCategoryNames) == 0)
			{
				estrConcatf(&pRetString, "%sNETOPS COMMENT: %s", estrLength(&pRetString) == 0 ? "" : "\r\n\r\n", pComment->pText);
			}
		}
		FOR_EACH_END

	}
	
	eaDestroy(&ppListToUse);

	return pRetString;

}


char *AlertTrackers_GetCommentsForAlert_OneSystemOrCategory(const char *pAlertKey, char *pSystemOrCategoryName, bool bExpandList)
{
	char **ppTempList = NULL;
	char *pRetVal;

	eaPush(&ppTempList, (char*)allocAddString(pSystemOrCategoryName));
	pRetVal = AlertTrackers_GetCommentsForAlert(pAlertKey, ppTempList, bExpandList);
	eaDestroy(&ppTempList);
	return pRetVal;
}










#include "NewControllerTracker_AlertTrackers_h_ast.c"