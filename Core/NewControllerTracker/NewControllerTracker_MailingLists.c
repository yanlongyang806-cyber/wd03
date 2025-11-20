#include "NewControllerTracker_MailingLists.h"
#include "textparser.h"
#include "timing.h"
#include "estring.h"
#include "earray.h"
#include "NewControllerTracker_MailingLists_h_Ast.h"
#include "alerts_h_ast.h"
#include "NewControllerTracker_CriticalSystems.h"
#include "StringUtil.h"
#include "wininclude.h"
#include "netSMTP.h"
#include "net.h"
#include "error.h"
#include "stringcache.h"
#include "file.h"
#include "resourceInfo.h"
#include "httpXpathSupport.h"
#include "newControllerTracker.h"
#include "UtilitiesLib.h"
#include "StashTable.h"
#include "NewControllerTracker_AlertTrackers.h"

bool AlertWithCountMatches(CritSystemEmailList_AlertWithCount *pAlertWithCount, const char *pAlertKey, int iAelrtCount);

CritSystemEmailListOfLists_Internal sMailingLists = {0};

static CritSystemEmailList_AlertGroup *AlertGroupFromString(char *pAlertString, char **ppErrorString)
{
	static CritSystemEmailList_AlertGroup sOutGroup = {0};
	StructReset(parse_CritSystemEmailList_AlertGroup, &sOutGroup);
	sOutGroup.eCategory = ALERTCATEGORY_NONE;
	sOutGroup.eLevel = ALERTLEVEL_NONE;

	if (stricmp(pAlertString, "all") == 0)
	{
		sOutGroup.bAll = true;
	}
	else
	{
		static char **ppTokens = NULL;
		int i;
	
		eaDestroy(&ppTokens);

		DivideString(pAlertString, " ", &ppTokens, 
			DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);

		for (i=0; i < eaSize(&ppTokens); i++)
		{
			int eLevel = StaticDefineIntGetInt(enumAlertLevelEnum, ppTokens[i]);
			int eCategory = StaticDefineIntGetInt(enumAlertCategoryEnum, ppTokens[i]);
			bool bGameServer = (stricmp(ppTokens[i], "GAMESERVER") == 0);
			bool bNotGameServer = (stricmp(ppTokens[i], "NOT_GAMESERVER") == 0);

			if (eLevel == -1 && eCategory == -1 && !bGameServer && !bNotGameServer)
			{
				estrPrintf(ppErrorString, "Unknown alert category or level %s", ppTokens[i]);
				return NULL;
			}

			if (eCategory != -1)
			{
				if (sOutGroup.eCategory != ALERTCATEGORY_NONE)
				{
					estrPrintf(ppErrorString, "two categories in the same alert string: %s", pAlertString);
					return NULL;
				}

				sOutGroup.eCategory = eCategory;
			}

			if (eLevel != -1)
			{
				if (sOutGroup.eLevel != ALERTLEVEL_NONE)
				{
					estrPrintf(ppErrorString, "two levels in the same alert string: %s", pAlertString);
					return NULL;
				}

				sOutGroup.eLevel = eLevel;
			}

			if (bGameServer)
			{
				sOutGroup.bGameServer = true;
			}

			if (bNotGameServer)
			{
				sOutGroup.bNotGameServer = true;
			}
		}
	}

	return StructClone(parse_CritSystemEmailList_AlertGroup, &sOutGroup);
}

static char dayPairs[7][2] = 
{
	{
		'S', 'u',
	},
	{
		'M', 'o',
	},
	{
		'T', 'u',
	},
	{
		'W', 'e',
	},
	{
		'T', 'h',
	},
	{
		'F', 'r',
	},
	{
		'S', 'a',
	},
};

static bool ProcessDaysString(CritSystemEmailList_TimeRestriction *pRestriction, char *pDaysString)
{
	int iLen = (int)strlen(pDaysString);
	int iReadHead;
	int i;

	if (stricmp(pDaysString, "all") == 0)
	{
		for (i=0; i < 7; i++)
		{
			pRestriction->bDays[i] = 1;
		}
		return true;
	}

	if (iLen % 2 == 1)
	{
		return false;
	}

	for (iReadHead = 0; iReadHead < iLen; iReadHead += 2)
	{
		for (i=0; i < 7; i++)
		{
			if (toupper(pDaysString[iReadHead]) == toupper(dayPairs[i][0])
				&& toupper(pDaysString[iReadHead+1]) == toupper(dayPairs[i][1]))
			{
				pRestriction->bDays[i] = true;
				break;
			}
		}

		if (i == 7)
		{
			return false;
		}
	}

	return true;
}







static CritSystemEmailList_TimeRestriction *TimeRestrictionFromString(char *pTimeString, char **ppErrorString)
{
	static CritSystemEmailList_TimeRestriction sLocalRestriction = {0};
	static char **ppTokens = NULL;

	StructReset(parse_CritSystemEmailList_TimeRestriction, &sLocalRestriction);
	eaDestroyEx(&ppTokens, NULL);

	DivideString(pTimeString, " -", &ppTokens, 
			DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	if (!(eaSize(&ppTokens) == 1 || eaSize(&ppTokens) == 3))
	{
		estrPrintf(ppErrorString, "Incorrect time syntax, It should be days [hours]. Days is ALL or some of MoTuWeThFrSaSu, hours is \"0600 - 1800\"");
		return NULL;
	}

	if (!ProcessDaysString(&sLocalRestriction, ppTokens[0]))
	{
		estrPrintf(ppErrorString, "Bad day string syntax. It should be \"All\" or some subset of MoTuWeThFrSaSu stuck together");
		return NULL;
	}

	if (eaSize(&ppTokens) == 1)
	{
		sLocalRestriction.iStartTime = 0;
		sLocalRestriction.iEndTime = 2400;
	}
	else
	{
		if (!StringToInt(ppTokens[1], &sLocalRestriction.iStartTime) || sLocalRestriction.iStartTime < 0 || sLocalRestriction.iStartTime > 2400)
		{
			estrPrintf(ppErrorString, "Bad time syntax: %s. Should be in the range 0000-24000", ppTokens[1]);
			return NULL;
		}
		if (!StringToInt(ppTokens[2], &sLocalRestriction.iEndTime) || sLocalRestriction.iEndTime < 0 || sLocalRestriction.iEndTime > 2400)
		{
			estrPrintf(ppErrorString, "Bad time syntax: %s. Should be in the range 0000-24000", ppTokens[2]);
			return NULL;
		}
	}
	return StructClone(parse_CritSystemEmailList_TimeRestriction, &sLocalRestriction);
}

static bool GetAlertsToSuppress(CritSystemEmailList_Internal *pInternalList, CritSystemEmailList_AlertWithCount ***pppListBeingBuilt, char *pAlertString, char **ppErrorString)
{
	static char **ppDivided = NULL;
	int i;

	eaDestroyEx(&ppDivided, NULL);

	DivideString(pAlertString, ",", &ppDivided, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	for (i = 0; i < eaSize(&ppDivided); i++)
	{
		char *pLessThan;
		char *pGreaterThan;

		if (strchr(ppDivided[i], ' '))
		{
			estrPrintf(ppErrorString, "alert with count \"%s\" has a space in it. This is illegal", ppDivided[i]);
			return false;
		}

		pLessThan = strchr(ppDivided[i], '<');
		pGreaterThan = strchr(ppDivided[i], '>');

		if (pLessThan && pGreaterThan)
		{
			estrPrintf(ppErrorString, "alert with count \"%s\" has both greater than and less than. This is illegal", ppDivided[i]);
			return false;
		}

		if (pLessThan)
		{
			int iVal;
			CritSystemEmailList_AlertWithCount *pAlert;

			if (!StringToInt(pLessThan + 1, &iVal) || iVal < 2)
			{
				estrPrintf(ppErrorString, "bad syntax or invalid value in alert key %s. Should be \"ALERT_NAME<n\", with n > 1",
					ppDivided[i]);
				return false;
			}

			pAlert = StructCreate(parse_CritSystemEmailList_AlertWithCount);
			*pLessThan = 0;
			pAlert->pKey = allocAddString(ppDivided[i]);
			pAlert->iMatchIfLessThan = iVal;
			eaPush(pppListBeingBuilt, pAlert);
		}
		else if (pGreaterThan)
		{
			int iVal;
			CritSystemEmailList_AlertWithCount *pAlert;

			if (!StringToInt(pGreaterThan + 1, &iVal) || iVal < 2)
			{
				estrPrintf(ppErrorString, "bad syntax or invalid value in alert key %s. Should be \"ALERT_NAME>n\", with n > 1",
					ppDivided[i]);
				return false;
			}

			pAlert = StructCreate(parse_CritSystemEmailList_AlertWithCount);
			*pGreaterThan = 0;
			pAlert->pKey = allocAddString(ppDivided[i]);
			pAlert->iMatchIfGreaterThan = iVal;
			eaPush(pppListBeingBuilt, pAlert);
		}
		else
		{
			CritSystemEmailList_AlertWithCount *pAlert = StructCreate(parse_CritSystemEmailList_AlertWithCount);
			pAlert->pKey = allocAddString(ppDivided[i]);
			eaPush(pppListBeingBuilt, pAlert);
		}
	}


/*	DivideString(pPubList->pAlertsToSuppressForShortRecipients, ",", &sInternalList.ppAlertsToSuppressForShortKeys,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_ALLOCADD);

	for (i=0; i < eaSize(&sInternalList.ppAlertsToSuppressForShortKeys); i++)
	{
		if (strchr(sInternalList.ppAlertsToSuppressForShortKeys[i], ' '))
		{
			estrPrintf(ppErrorString, "alert to suppress \"%s\" has a space in it. This is illegal", sInternalList.ppAlertsToSuppressForShortKeys[i]);
			return NULL;
		}
	}*/



	return true;
}

void FixupPointerCountersForServerMon(CritSystemEmailList_Internal *pList)
{
	int i;

	eaDestroyStruct(&pList->ppCriticalAlertCountsForServerMon, parse_PointerCounterResult_ForServerMon);
	eaDestroyStruct(&pList->ppWarningAlertCountsForServerMon, parse_PointerCounterResult_ForServerMon);
	eaDestroyStruct(&pList->ppSuppressedAlertCountsForServerMon, parse_PointerCounterResult_ForServerMon);

	if (pList->pCriticalAlertCounter)
	{
		for (i=0;i < eaSize(&pList->pCriticalAlertCounter->ppResults); i++)
		{
			PointerCounterResult_ForServerMon *pResultForServerMon = StructCreate(parse_PointerCounterResult_ForServerMon);
			pResultForServerMon->iCount = pList->pCriticalAlertCounter->ppResults[i]->iCount;
			pResultForServerMon->pName = pList->pCriticalAlertCounter->ppResults[i]->pPtr;

			eaPush(&pList->ppCriticalAlertCountsForServerMon, pResultForServerMon);
		}
	}

	if (pList->pWarningAlertCounter)
	{
		for (i=0;i < eaSize(&pList->pWarningAlertCounter->ppResults); i++)
		{
			PointerCounterResult_ForServerMon *pResultForServerMon = StructCreate(parse_PointerCounterResult_ForServerMon);
			pResultForServerMon->iCount = pList->pWarningAlertCounter->ppResults[i]->iCount;
			pResultForServerMon->pName = pList->pWarningAlertCounter->ppResults[i]->pPtr;

			eaPush(&pList->ppWarningAlertCountsForServerMon, pResultForServerMon);
		}
	}

	if (pList->pSuppressedAlertCounter)
	{
		for (i=0;i < eaSize(&pList->pSuppressedAlertCounter->ppResults); i++)
		{
			PointerCounterResult_ForServerMon *pResultForServerMon = StructCreate(parse_PointerCounterResult_ForServerMon);
			pResultForServerMon->iCount = pList->pSuppressedAlertCounter->ppResults[i]->iCount;
			pResultForServerMon->pName = pList->pSuppressedAlertCounter->ppResults[i]->pPtr;

			eaPush(&pList->ppSuppressedAlertCountsForServerMon, pResultForServerMon);
		}
	}
}




AUTO_FIXUPFUNC;
TextParserResult CritSystemEmailList_Internal_Fixup(CritSystemEmailList_Internal *pList, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		PointerCounter_Destroy(&pList->pCriticalAlertCounter);
		PointerCounter_Destroy(&pList->pWarningAlertCounter);
		PointerCounter_Destroy(&pList->pSuppressedAlertCounter);
		break;
	case FIXUPTYPE_CONSTRUCTOR:
		pList->iLastAlertCountingTimeBegan = timeSecondsSince2000();
		break;

	case FIXUPTYPE_GOTTEN_FROM_RES_DICT:
		FixupPointerCountersForServerMon(pList);
		break;
	}

	return true;
}


static CritSystemEmailList_Internal *InternalListFromPubList(CritSystemEmailList_Pub *pPubList, char **ppErrorString)
{
	static CritSystemEmailList_Internal sInternalList = {0};
	int i;
	static char **ppAlertStrings = NULL;
	static char **ppTimeStrings = NULL;
	CritSystemEmailList_Internal *pRetVal;
	bool bFoundAllCategoriesOrSystems = false;

	StructReset(parse_CritSystemEmailList_Internal, &sInternalList);

	estrGetDirAndFileName(pPubList->pFileName, NULL, &sInternalList.pName);

	if (pPubList->pCategories)
	{
		if (stricmp(pPubList->pCategories, "all") == 0)
		{
			bFoundAllCategoriesOrSystems = true;
		}
		else
		{
			DivideString(pPubList->pCategories, ",", &sInternalList.ppCategories, 
				DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
				| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);
		}
	}

	if (pPubList->pSystems)
	{
		if (stricmp(pPubList->pSystems, "all") == 0)
		{
			bFoundAllCategoriesOrSystems = true;
		}
		else
		{
			DivideString(pPubList->pSystems, ",", &sInternalList.ppSystems, 
				DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
				| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);
		}
	}

	if (!bFoundAllCategoriesOrSystems && !eaSize(&sInternalList.ppCategories) && !eaSize(&sInternalList.ppSystems))
	{
		estrPrintf(ppErrorString, "Categories and Systems both empty... this is illegal and pointless");
		return NULL;
	}

	for (i=0; i < eaSize(&sInternalList.ppCategories); i++)
	{
		if (!DoesCategoryExist(sInternalList.ppCategories[i]))
		{
			estrPrintf(ppErrorString, "Unknown category \"%s\"", sInternalList.ppCategories[i]);
			return NULL;
		}
	}

	if (pPubList->pCategoriesToSuppressForShortRecipients)
	{
		DivideString(pPubList->pCategoriesToSuppressForShortRecipients, ",", &sInternalList.ppCategoriesToSuppressForShort,
			DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
				| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);

		if (!eaSize(&sInternalList.ppCategoriesToSuppressForShort))
		{
			estrPrintf(ppErrorString, "CategoriesToSuppressForShortRecipients specified but empty");
			return NULL;
		}

		for (i=0; i < eaSize(&sInternalList.ppCategoriesToSuppressForShort); i++)
		{
			if (!DoesCategoryExist(sInternalList.ppCategoriesToSuppressForShort[i]))
			{
				estrPrintf(ppErrorString, "Unknown category to suppres for short recipients \"%s\"", sInternalList.ppCategoriesToSuppressForShort[i]);
				return NULL;
			}

			if (!bFoundAllCategoriesOrSystems)
			{
				if (eaFind(&sInternalList.ppCategories, sInternalList.ppCategoriesToSuppressForShort[i]) == -1)
				{
					estrPrintf(ppErrorString, "Trying to suppress category %s for short recipients, but it's not on our list at all",
						sInternalList.ppCategoriesToSuppressForShort[i]);
					return NULL;
				}
			}
		}
	}

	if (pPubList->pSystemsToSuppressForShortRecipients)
	{
		DivideString(pPubList->pSystemsToSuppressForShortRecipients, ",", &sInternalList.ppSystemsToSuppressForShort,
			DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
				| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);

		if (!eaSize(&sInternalList.ppSystemsToSuppressForShort))
		{
			estrPrintf(ppErrorString, "SystemsToSuppressForShortRecipients specified but empty");
			return NULL;
		}

		for (i=0; i < eaSize(&sInternalList.ppSystemsToSuppressForShort); i++)
		{
	
			if (!bFoundAllCategoriesOrSystems)
			{
				if (eaFind(&sInternalList.ppSystems, sInternalList.ppSystemsToSuppressForShort[i]) == -1)
				{
					estrPrintf(ppErrorString, "Trying to suppress system %s for short recipients, but it's not on our list at all",
						sInternalList.ppSystemsToSuppressForShort[i]);
					return NULL;
				}
			}
		}
	}


	
	sInternalList.bSendAll = pPubList->bSendAll;
	sInternalList.bSendDown = pPubList->bSendDown;
	sInternalList.bSendOther = pPubList->bSendOther;

	eaDestroyEx(&ppAlertStrings, NULL);
	DivideString(pPubList->pAlerts, ",", &ppAlertStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	for (i=0; i < eaSize(&ppAlertStrings); i++)
	{
		CritSystemEmailList_AlertGroup *pAlertGroup = AlertGroupFromString(ppAlertStrings[i], ppErrorString);
		if (!pAlertGroup)
		{
			return NULL;
		}
		eaPush(&sInternalList.ppAlertGroups, pAlertGroup);
	}

	if (!GetAlertsToSuppress(&sInternalList, &sInternalList.ppAlertsToSuppressForShortKeys, pPubList->pAlertsToSuppressForShortRecipients, ppErrorString))
	{
		return NULL;
	}

	if (!GetAlertsToSuppress(&sInternalList, &sInternalList.ppAlertsToAlwaysInclude, pPubList->pAlertsToAlwaysInclude, ppErrorString))
	{
		return NULL;
	}

	if (!GetAlertsToSuppress(&sInternalList, &sInternalList.ppAlertsToAlwaysSuppress, pPubList->pAlertsToAlwaysSuppress, ppErrorString))
	{
		return NULL;
	}



	if (!sInternalList.bSendAll && !sInternalList.bSendDown && !sInternalList.bSendOther && eaSize(&sInternalList.ppAlertGroups) == 0)
	{
		estrPrintf(ppErrorString, "No emails will be sent, there is nothing turned on in the various flags or alerts");
		return NULL;
	}

	eaDestroyEx(&ppTimeStrings, NULL);
	DivideString(pPubList->pTimes, ",", &ppTimeStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	for (i=0; i < eaSize(&ppTimeStrings); i++)
	{
		CritSystemEmailList_TimeRestriction *pTimeRestriction = TimeRestrictionFromString(ppTimeStrings[i], ppErrorString);
		if (!pTimeRestriction)
		{
			return NULL;
		}
		eaPush(&sInternalList.ppTimeRestrictions, pTimeRestriction);
	}

	DivideString(pPubList->pFullRecipients, ",", &sInternalList.ppFullRecipients,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);

	DivideString(pPubList->pShortRecipients, ",", &sInternalList.ppShortRecipients,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);

	DivideString(pPubList->pDailyAlertSummaryRecipients, ",", &sInternalList.ppDailyAlertSummaryRecipients,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE |DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS
			| DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE	| DIVIDESTRING_POSTPROCESS_ALLOCADD);


	if (!eaSize(&sInternalList.ppShortRecipients) && !eaSize(&sInternalList.ppFullRecipients) && !eaSize(&sInternalList.ppDailyAlertSummaryRecipients))
	{
		estrPrintf(ppErrorString, "No recipients specified");
		return NULL;
	}


	pRetVal = StructClone(parse_CritSystemEmailList_Internal, &sInternalList);
	return pRetVal;
}

char *spAutoMailingListRecipient = NULL;
AUTO_CMD_ESTRING(spAutoMailingListRecipient, AutoMailingListRecipient);

CritSystemEmailList_Pub *MakeMailingListForOneRecipient(char *pRecipient)
{
	CritSystemEmailList_Pub *pList = StructCreate(parse_CritSystemEmailList_Pub);

	pList->pFileName = (char*)allocAddString("fake.txt");
	pList->pSystems = strdup("all");
	pList->bSendAll = true;
	pList->pAlerts = strdup("all");
	pList->pFullRecipients = strdup(pRecipient);
	pList->pShortRecipients = strdup(pRecipient);

	return pList;
}



bool LoadMailingLists(char *pDirectory, char **ppErrorString)
{
	static CritSystemEmailListOfLists_Pub sPubLists = {0};
	static CritSystemEmailListOfLists_Internal sInternalLists = {0};
	int i;

	StructInit(parse_CritSystemEmailListOfLists_Internal, &sInternalLists);

	StructReset(parse_CritSystemEmailListOfLists_Pub, &sPubLists);
	StructReset(parse_CritSystemEmailListOfLists_Internal, &sInternalLists);

	ErrorfPushCallback(EstringErrorCallback, (void*)ppErrorString);

	if (!ParserLoadFiles(pDirectory, "List.txt", NULL, PARSER_OPTIONALFLAG, parse_CritSystemEmailListOfLists_Pub, &sPubLists))
	{
		ErrorfPopCallback();
		return false;
	}
	
	ErrorfPopCallback();

	if (spAutoMailingListRecipient)
	{
		eaPush(&sPubLists.ppMailingList, MakeMailingListForOneRecipient(spAutoMailingListRecipient));
	}


	if (!eaSize(&sPubLists.ppMailingList))
	{
		StructReset(parse_CritSystemEmailListOfLists_Internal, &sMailingLists);
		return true;
	}

	for (i = 0; i < eaSize(&sPubLists.ppMailingList); i++)
	{
		CritSystemEmailList_Internal *pInternalList = InternalListFromPubList(sPubLists.ppMailingList[i], ppErrorString);
		if (!pInternalList)
		{
			char *pTempErrorString = NULL;
			estrPrintf(&pTempErrorString, "Failure while parsing %s: %s", sPubLists.ppMailingList[i]->pFileName, *ppErrorString);
			estrCopy(ppErrorString, &pTempErrorString);
			estrDestroy(&pTempErrorString);
			return false;
		}

		eaPush(&sInternalLists.ppLists, pInternalList);
	}

	StructCopy(parse_CritSystemEmailListOfLists_Internal, &sInternalLists, &sMailingLists, 0, 0, 0);

	return true;

}

static bool DoesTimeMatchRestriction(CritSystemEmailList_TimeRestriction *pTimeRestriction, U32 iTime)
{
	SYSTEMTIME t;
	int iMilitaryTime;

	timerLocalSystemTimeFromSecondsSince2000(&t, iTime);
	iMilitaryTime = t.wHour * 100 + t.wMinute;

	if (pTimeRestriction->iEndTime > pTimeRestriction->iStartTime)
	{
		if (iMilitaryTime >= pTimeRestriction->iStartTime && iMilitaryTime <= pTimeRestriction->iEndTime)
		{
			if (pTimeRestriction->bDays[t.wDayOfWeek])
			{
				return true;
			}
		}
	}
	else
	{
		if (iMilitaryTime >= pTimeRestriction->iStartTime && pTimeRestriction->bDays[t.wDayOfWeek])
		{
			return true;
		}

		if (iMilitaryTime <= pTimeRestriction->iEndTime && pTimeRestriction->bDays[(t.wDayOfWeek + 6) % 7])
		{
			return true;
		}
	}

	return false;
}

static bool SystemIsInList(CriticalSystem_Status *pSystem, CritSystemEmailList_Internal *pList)
{
	int i;

	if (eaSize(&pList->ppSystems) == 0 && eaSize(&pList->ppCategories) == 0)
	{
		return true;
	}

	if (eaFindString(&pList->ppSystems, pSystem->pConfig->pName) != -1)
	{
		return true;
	}

	for (i=0; i < eaSize(&pSystem->pConfig->ppCategories); i++)
	{
		if (eaFind(&pList->ppCategories, pSystem->pConfig->ppCategories[i]) != -1)
		{
			return true;
		}
	}

	return false;
}

static bool AlertMatchesRestrictions(CritSystemEmailList_AlertGroup *pGroup, 
	enumAlertCategory eAlertCategory, enumAlertLevel eAlertLevel, GlobalType eAlertServerType, GlobalType eAlertObjType)
{
	bool bGameServer;

	if (pGroup->bAll)
	{
		return true;
	}

	bGameServer = (eAlertServerType == GLOBALTYPE_GAMESERVER) || (eAlertObjType == GLOBALTYPE_GAMESERVER);


	if (pGroup->bGameServer && !bGameServer)
	{
		return false;
	}

	if (pGroup->bNotGameServer && bGameServer)
	{
		return false;
	}


	if (pGroup->eCategory != ALERTCATEGORY_NONE && pGroup->eCategory != eAlertCategory)
	{
		return false;

	}

	if (pGroup->eLevel != ALERTLEVEL_NONE && pGroup->eLevel != eAlertLevel)
	{
		return false;
	}

	return true;
}

char *GetFromAddress(void)
{
	static char *pRetVal = NULL;
	char *pStatusReportingName;

	if (pRetVal)
	{
		return pRetVal;
	}

	pStatusReportingName = StatusReporting_GetMyName();
	if (pStatusReportingName && pStatusReportingName[0])
	{
		estrCopy2(&pRetVal, pStatusReportingName);
		estrMakeAllAlphaNumAndUnderscores(&pRetVal);
		estrConcatf(&pRetVal, "@"ORGANIZATION_DOMAIN);
		return pRetVal;
	}

	estrCopy2(&pRetVal, "CriticalAlerts@" ORGANIZATION_DOMAIN);

	return pRetVal;
}

int gZipAndAttachCutoff = 40000;
AUTO_CMD_INT(gZipAndAttachCutoff, ZipAndAttachCutoff);

int gZipAndAttachPrefixLen = 20000;
AUTO_CMD_INT(gZipAndAttachPrefixLen, ZipAndAttachPrefixLen);




//pRecipients is a comma-separated list of full email addresses
static void SendEmailToList(char **ppRecipients, char *pSubject, char *pBody, int iBodyLength, bool bHTML)
{
	char *pSubjectToUse = NULL;
	SMTPMessageRequest *pReq;
	char *pResultEstr = NULL;
	int i;

	if (!eaSize(&ppRecipients) && !gOnlySendTo[0])
	{
		return;
	}


	estrCopy2(&pSubjectToUse, pSubject);
	
	pReq = StructCreate(parse_SMTPMessageRequest);

	if (gOnlySendTo[0])
	{
		eaPush(&pReq->to, estrDup(gOnlySendTo));
	}
	else
	{
		for (i=0; i < eaSize(&ppRecipients); i++)
		{
			eaPush(&pReq->to, estrDup(ppRecipients[i]));
		}
	}


	estrPrintf(&pReq->from, "%s", GetFromAddress());

	estrPrintf(&pReq->subject, "%s", pSubjectToUse);


	pReq->html = bHTML;


	if (iBodyLength > gZipAndAttachCutoff)
	{
		FILE *pFile = fopen("c:\\temp\\email.gz", "wbz");

		if (pFile)
		{
			fwrite(pBody, 1, iBodyLength + 1, pFile);
			fclose(pFile);
			estrConcat(&pReq->body, pBody, gZipAndAttachPrefixLen);
			estrConcatf(&pReq->body, "\n\n(Truncated... full body in attachment)\n");
			estrPrintf(&pReq->attachfilename, "c:\\temp\\email.gz");
			estrPrintf(&pReq->attachsuggestedname, "email.gz");
		}
		else
		{
			estrConcat(&pReq->body, pBody, gZipAndAttachPrefixLen);
			estrConcatf(&pReq->body, "\n\n(Truncated... attachment attempted but failed)\n");
		}
	}
	else
	{
		estrPrintf(&pReq->body, "%s", pBody ? pBody : "(No body)");
	}

	pReq->pResultCBFunc = GenericSendEmailResultCB;
	pReq->pUserData = strdup(pReq->subject);

	smtpMsgRequestSend_BgThread(pReq);
	
	StructDestroy(parse_SMTPMessageRequest, pReq);
	estrDestroy(&pResultEstr);
}

bool AlertIsSuppressedForList(CritSystemEmailList_Internal *pList, const char *pAlertKey, int iAlertCount)
{
	int i;

	for (i=0;i < eaSize(&pList->ppAlertsToSuppressForShortKeys); i++)
	{
		if (AlertWithCountMatches(pList->ppAlertsToSuppressForShortKeys[i], pAlertKey, iAlertCount))
		{
			return true;
		}
	}

	return false;
}

bool SystemIsSuppressedForShortRecipients(CriticalSystem_Status *pSystem, CritSystemEmailList_Internal *pList)
{
	const char *pPooledName = allocAddString(pSystem->pName_Internal);
	int i;

	if (eaFind(&pList->ppSystemsToSuppressForShort, pPooledName) != -1)
	{
		return true;
	}

	for (i=0; i < eaSize(&pSystem->pConfig->ppCategories); i++)
	{
		if (eaFind(&pList->ppCategoriesToSuppressForShort, pSystem->pConfig->ppCategories[i]) != -1)
		{
			return true;
		}
	}

	return false;
}

void SendSimpleEmail(char *pTo, char *pSubjectLine, FORMAT_STR const char *pBodyFmt, ...)
{
	char *pBodyString = NULL;
	SMTPMessageRequest *pReq;
	char *pResultEstr = NULL;
	static NetComm *pSimpleEmailComm = NULL;

	estrGetVarArgs(&pBodyString, pBodyFmt);



	pReq = StructCreate(parse_SMTPMessageRequest);

	eaPush(&pReq->to, estrDup(pTo));

	estrPrintf(&pReq->from, "%s", GetFromAddress());
	estrPrintf(&pReq->subject, "%s", pSubjectLine);


	pReq->html = false;

	estrPrintf(&pReq->body, "%s", pBodyString);

	pReq->pResultCBFunc = GenericSendEmailResultCB;
	pReq->pUserData = strdup(pReq->subject);

	smtpMsgRequestSend_BgThread(pReq);

	StructDestroy(parse_SMTPMessageRequest, pReq);
	estrDestroy(&pResultEstr);
}

//if we send this many emails within a certain number of seconds to a short-email address, stop sending them for a while
int siThrottleShortEmails_Count = 10;
AUTO_CMD_INT(siThrottleShortEmails_Count, ThrottleShortEmails_Count);

//if we send a bunch of emails within this many seconds to a short-email address, stop sending them for a while
int siThrottleShortEmails_WithinSeconds = 60;
AUTO_CMD_INT(siThrottleShortEmails_WithinSeconds, ThrottleShortEmails_WithinSeconds);

//if we send a bunch of emails within a short time to a short-email address, stop sending them for this many minutes
int siThrottleShortEmails_PauseAfterFailureMinutes = 5;
AUTO_CMD_INT(siThrottleShortEmails_PauseAfterFailureMinutes, ThrottleShortEmails_PauseAfterFailureMinutes);


StashTable hShortRecipientThrottlers = NULL;
bool ShortRecipientPassesThrottlingCheck(char *pShortRecipientName /*pooled*/)
{
	SimpleEventThrottler *pThrottler;

	if (!hShortRecipientThrottlers)
	{
		hShortRecipientThrottlers = stashTableCreateAddress(16);
	}

	if (!stashFindPointer(hShortRecipientThrottlers, pShortRecipientName, &pThrottler))
	{
		pThrottler = SimpleEventThrottler_Create(siThrottleShortEmails_Count, siThrottleShortEmails_WithinSeconds, siThrottleShortEmails_PauseAfterFailureMinutes * 60);
		stashAddPointer(hShortRecipientThrottlers, pShortRecipientName, pThrottler, false);
	}

	switch (SimpleEventThrottler_ItHappened(pThrottler, timeSecondsSince2000()))
	{
	xcase SETR_FIRSTFAIL:
		SendSimpleEmail(pShortRecipientName, "Emails being throttled", "%d emails send to you within last %d seconds. No more will be sent for next %d minutes (short emails only)",
			siThrottleShortEmails_Count, siThrottleShortEmails_WithinSeconds, siThrottleShortEmails_PauseAfterFailureMinutes);

		return false;
	xcase SETR_FAIL:
		return false;
	}

	return true;
}

bool AlertWithCountMatches(CritSystemEmailList_AlertWithCount *pAlertWithCount, const char *pAlertKey, int iAlertCount)
{
	if (pAlertWithCount->pKey != pAlertKey)
	{
		return false;
	}

	if (pAlertWithCount->iMatchIfLessThan == 0 && pAlertWithCount->iMatchIfGreaterThan == 0)
	{
		return true;
	}

	if (pAlertWithCount->iMatchIfLessThan && iAlertCount < pAlertWithCount->iMatchIfLessThan)
	{
		return true;
	}

	if (pAlertWithCount->iMatchIfGreaterThan && iAlertCount > pAlertWithCount->iMatchIfGreaterThan)
	{
		return true;
	}

	return false;
}

void SendEmailWithMailingLists(CriticalSystem_Status *pSystem, enumCritSystemEmailType eType,
	enumAlertCategory eAlertCategory, enumAlertLevel eAlertLevel, const char *pAlertKey, GlobalType eAlertServerType, GlobalType eAlertObjType, int iAlertCount, char *pFullSubjectString, char *pShortSubjectString, char *pFullBodyString,
	char *pShortBodyString, char **ppOutWhoSentToEString)
{
	int iListNum, i;
	U32 iCurTime = timeSecondsSince2000();

	char **ppFullRecipients = NULL;
	char **ppShortRecipients = NULL;

	//truncate short bodies to < 150 chars
	char *pTruncatedShortBodyString = NULL;

	char *pRedirectRecipient = NULL;

	static char *pBodyStringCopy = NULL;
	bool bSendToRedirectRecipient = false;

	estrCopy2(&pBodyStringCopy, pFullBodyString);

	if (pAlertKey)
	{
		char *pCommentString = AlertTrackers_GetCommentsForAlert_OneSystemOrCategory(pAlertKey, pSystem->pName_Internal, true);
		if (pCommentString)
		{
			estrInsertf(&pBodyStringCopy, 0, "%s\r\n\r\n", pCommentString);
		}
	}

	if (pAlertKey)
	{
		pRedirectRecipient = AlertTrackers_GetRedirectAddressForAlert(pAlertKey);
	}

	for (iListNum = 0; iListNum < eaSize(&sMailingLists.ppLists); iListNum++)
	{
		CritSystemEmailList_Internal *pList = sMailingLists.ppLists[iListNum];
		bool bMatchesTime = false;
		bool bForceSuppressed = false;

		if (!SystemIsInList(pSystem, pList))
		{
			continue;
		}

		if (eType == EMAILTYPE_ALERT)
		{
			for (i = 0; i < eaSize(&pList->ppAlertsToAlwaysSuppress); i++)
			{
				if (AlertWithCountMatches(pList->ppAlertsToAlwaysSuppress[i], pAlertKey, iAlertCount))
				{
					bForceSuppressed = true;
					break;
				}
			}
		}	

		if (bForceSuppressed)
		{
			continue;
		}

		if (!pList->bSendAll)
		{
			bool bMatchesErrorType = false;

			switch (eType)
			{
			case EMAILTYPE_SYSTEMDOWN:
			case EMAILTYPE_BEINGREMOVED:
				if (pList->bSendDown)
				{
					bMatchesErrorType = true;
				}
				break;
			case EMAILTYPE_ALERT:
				{
					for (i = 0; i < eaSize(&pList->ppAlertsToAlwaysInclude); i++)
					{
						if (AlertWithCountMatches(pList->ppAlertsToAlwaysInclude[i], pAlertKey, iAlertCount))
						{
							bMatchesErrorType = true;
							break;
						}
					}

					if (!bMatchesErrorType)
					{
						for (i=0; i < eaSize(&pList->ppAlertGroups); i++)
						{
							if (AlertMatchesRestrictions(pList->ppAlertGroups[i], eAlertCategory, eAlertLevel, eAlertServerType, eAlertObjType))
							{
								bMatchesErrorType = true;
							}
						}
					}
				}
				break;
			default:
				if (pList->bSendOther)
				{
					bMatchesErrorType = true;
				}
				break;

			}

			if (!bMatchesErrorType)
			{
				continue;
			}
		}

		if (!eaSize(&pList->ppTimeRestrictions))
		{
			bMatchesTime = true;
		}
		else
		{
			for (i=0; i < eaSize(&pList->ppTimeRestrictions); i++)
			{
				if (DoesTimeMatchRestriction(pList->ppTimeRestrictions[i], iCurTime))
				{
					bMatchesTime = true;
				}
			}
		}

		if (!bMatchesTime)
		{
			continue;
		}

		if (!pAlertKey || !AlertIsSuppressedForList(pList, pAlertKey, iAlertCount))
		{
			if (!SystemIsSuppressedForShortRecipients(pSystem, pList))
			{
				for (i=0; i < eaSize(&pList->ppShortRecipients); i++)
				{
					if (ShortRecipientPassesThrottlingCheck(pList->ppShortRecipients[i]))
					{
						eaPushUnique(&ppShortRecipients, pList->ppShortRecipients[i]);
					}
				}
			}
		}

		for (i=0; i < eaSize(&pList->ppFullRecipients); i++)
		{
			eaPushUnique(&ppFullRecipients, pList->ppFullRecipients[i]);
		}
	}

	estrCopy2(&pTruncatedShortBodyString, pShortBodyString);
	if (estrLength(&pTruncatedShortBodyString) >= 150)
	{
		estrSetSize(&pTruncatedShortBodyString, 149);
	}


	if (pRedirectRecipient)
	{
		bSendToRedirectRecipient = true;
	}
	else
	{
		SendEmailToList(ppShortRecipients, pShortSubjectString, pTruncatedShortBodyString, estrLength(&pTruncatedShortBodyString), false);
		SendEmailToList(ppFullRecipients, pFullSubjectString, pBodyStringCopy, estrLength(&pBodyStringCopy), false);
	

		if (ppOutWhoSentToEString)
		{
			estrClear(ppOutWhoSentToEString);
			for (i=0; i < eaSize(&ppShortRecipients); i++)
			{
				estrConcatf(ppOutWhoSentToEString, "%s%s", estrLength(ppOutWhoSentToEString) ? ", " : "", ppShortRecipients[i]);
			}

			for (i=0; i < eaSize(&ppFullRecipients); i++)
			{
				estrConcatf(ppOutWhoSentToEString, "%s%s", estrLength(ppOutWhoSentToEString) ? ", " : "", ppFullRecipients[i]);
			}
		}
	}

	if (bSendToRedirectRecipient)
	{
		eaDestroy(&ppFullRecipients);
		eaPush(&ppFullRecipients, pRedirectRecipient);
		SendEmailToList(ppFullRecipients, pFullSubjectString, pBodyStringCopy, estrLength(&pBodyStringCopy), false);
	}

	estrDestroy(&pTruncatedShortBodyString);

	eaDestroy(&ppShortRecipients);
	eaDestroy(&ppFullRecipients);
}


void ReportAlertToMailingLists(CriticalSystem_Status *pSystem, const char *pPooledAlertKey, enumAlertLevel eLevel, bool bSuppressed)
{
	int iListNum;
	for (iListNum = 0; iListNum < eaSize(&sMailingLists.ppLists); iListNum++)
	{
		CritSystemEmailList_Internal *pList = sMailingLists.ppLists[iListNum];

		if (!SystemIsInList(pSystem, pList) || !pList->ppDailyAlertSummaryRecipients)
		{
			continue;
		}

		if (bSuppressed)
		{
			if (!pList->pSuppressedAlertCounter)
			{
				pList->pSuppressedAlertCounter = PointerCounter_Create();
			}

			PointerCounter_AddSome(pList->pSuppressedAlertCounter, pPooledAlertKey, 1);
		}
		else
		{
			if (eLevel == ALERTLEVEL_CRITICAL)
			{
				if (!pList->pCriticalAlertCounter)
				{
					pList->pCriticalAlertCounter = PointerCounter_Create();
				}

				PointerCounter_AddSome(pList->pCriticalAlertCounter, pPooledAlertKey, 1);


			}
			else
			{
				if (!pList->pWarningAlertCounter)
				{
					pList->pWarningAlertCounter = PointerCounter_Create();
				}

				PointerCounter_AddSome(pList->pWarningAlertCounter, pPooledAlertKey, 1);
			}
		}
	}
}


void DumpAlertCounterToString(char **ppOutString, PointerCounter *pCounter, char **ppSystemOrCategoryNames)
{
	int i;

	if (!pCounter)
	{
		estrConcatf(ppOutString, "(none)\n\n");
	}
	else
	{
		PointerCounterResult **ppResults = NULL;
				
		PointerCounter_GetMostCommon(pCounter, 10000, &ppResults);
				
		for (i=0; i < eaSize(&ppResults); i++)
		{
			char *pComment = AlertTrackers_GetCommentsForAlert((char*)(ppResults[i]->pPtr), ppSystemOrCategoryNames, true);
			char *pRedirect = AlertTrackers_GetRedirectAddressForAlert((char*)(ppResults[i]->pPtr));
			static char *pRedirectSnippet = NULL;
			if (pRedirect)
			{
				estrPrintf(&pRedirectSnippet, " (REDIRECTING TO %s)", pRedirect);
			}
			else
			{
				estrCopy2(&pRedirectSnippet, "");
			}

			estrConcatf(ppOutString, "%5d <a href=\"http://%s:%u/viewxpath?xpath=ControllerTracker[0].globObj.Alerttrackers[%s]\">%s</a> (%s%s)\n", ppResults[i]->iCount, getHostName(), CONTROLLERTRACKER_HTTP_PORT, (char*)(ppResults[i]->pPtr), (char*)(ppResults[i]->pPtr), pComment ? pComment : "NO COMMENT PROVIDED",
				pRedirectSnippet);
		}

		estrConcatf(ppOutString, "\n\n");
		eaDestroyEx(&ppResults, NULL);
	}
}


AUTO_COMMAND;
void SendAlertSummariesNow(void)
{
	U32 iCurTime = timeSecondsSince2000();


	int iListNum;
	int i;
	for (iListNum = 0; iListNum < eaSize(&sMailingLists.ppLists); iListNum++)
	{
		CritSystemEmailList_Internal *pList = sMailingLists.ppLists[iListNum];

		if (pList->ppDailyAlertSummaryRecipients)
		{
			char *pEmailString = NULL;
			char *pSubjectString = NULL;

			char **ppSystemsAndCategories = NULL;

			if (eaSize(&pList->ppCategories))
			{
				eaPushArray(&ppSystemsAndCategories, pList->ppCategories, eaSize(&pList->ppCategories));
			}

			if (eaSize(&pList->ppSystems))
			{
				eaPushArray(&ppSystemsAndCategories, pList->ppSystems, eaSize(&pList->ppSystems));
			}


			if (pList->ppCategories)
			{
				estrPrintf(&pSubjectString, "Alert summary for categor%s ",
					eaSize(&pList->ppCategories) == 1 ? "y" : "ies");
				estrConcatSeparatedStringEarray(&pSubjectString, &pList->ppCategories, ", ");
			}
			else
			{
				estrPrintf(&pSubjectString, "Alert summary for system%s ",
					eaSize(&pList->ppSystems) == 1 ? "" : "s");
				estrConcatSeparatedStringEarray(&pSubjectString, &pList->ppSystems, ", ");
			}

			estrPrintf(&pEmailString, "Alert summary from %s to ", timeGetLocalDateStringFromSecondsSince2000(pList->iLastAlertCountingTimeBegan));
			estrConcatf(&pEmailString, "%s\n", timeGetLocalDateStringFromSecondsSince2000(iCurTime));

			pList->iLastAlertCountingTimeBegan = iCurTime;

			if (pList->ppCategories)
			{
				estrConcatf(&pEmailString, "Categories: ");
				for (i=0; i < eaSize(&pList->ppCategories); i++)
				{
					estrConcatf(&pEmailString, "%s%s", i == 0 ? "" : ", ", pList->ppCategories[i]);
				}
				estrConcatf(&pEmailString, "\n");
			}

			if (pList->ppSystems)
			{
				estrConcatf(&pEmailString, "Systems: ");
				for (i=0; i < eaSize(&pList->ppSystems); i++)
				{
					estrConcatf(&pEmailString, "%s%s", i == 0 ? "" : ", ", pList->ppSystems[i]);
				}
				estrConcatf(&pEmailString, "\n\n");
			}		

			estrConcatf(&pEmailString, "CRITICAL ALERTS (with count)\n");
			DumpAlertCounterToString(&pEmailString, pList->pCriticalAlertCounter, ppSystemsAndCategories);
			PointerCounter_Destroy(&pList->pCriticalAlertCounter);

			estrConcatf(&pEmailString, "WARNING ALERTS (with count)\n");
			DumpAlertCounterToString(&pEmailString, pList->pWarningAlertCounter, ppSystemsAndCategories);
			PointerCounter_Destroy(&pList->pWarningAlertCounter);

			estrConcatf(&pEmailString, "SUPPRESSED ALERTS (with count)\n");
			DumpAlertCounterToString(&pEmailString, pList->pSuppressedAlertCounter, ppSystemsAndCategories);
			PointerCounter_Destroy(&pList->pSuppressedAlertCounter);

			eaDestroy(&ppSystemsAndCategories);
				
		
			estrReplaceOccurrences(&pEmailString, "\n", "<br />");
			SendEmailToList(pList->ppDailyAlertSummaryRecipients, pSubjectString, pEmailString, estrLength(&pEmailString), true);
			estrDestroy(&pSubjectString);
			estrDestroy(&pEmailString);
		}
	}
}
				




AUTO_COMMAND;
char *ReloadMailingLists(void)
{
	static char *pRetString = NULL;
	estrClear(&pRetString);

	if (!LoadMailingLists("c:\\ControllerTracker", &pRetString))
	{
		return pRetString;
	}
	else
	{
		estrPrintf(&pRetString, "%d mailing lists loaded successfuly", eaSize(&sMailingLists.ppLists));
		return pRetString;
	}
}

int iAlertSummariesSendTime = 900;
AUTO_CMD_INT(iAlertSummariesSendTime, AlertSummariesSendTime);

void MailingLists_Tick(void)
{
	U32 iCurTime = timeSecondsSince2000();
	static U32 siNextEmailTime = 0;

	if (!siNextEmailTime)
	{
		siNextEmailTime = FindNextSS2000WhichMatchesLocalHourOfDay(iCurTime, iAlertSummariesSendTime);
		return;
	}

	if (iCurTime > siNextEmailTime)
	{
		SendAlertSummariesNow();
		siNextEmailTime = FindNextSS2000WhichMatchesLocalHourOfDay(iCurTime, iAlertSummariesSendTime);
	}
}

AUTO_RUN;
void MailingLists_InitSystem(void)
{
	resRegisterDictionaryForEArray("Mailing Lists", RESCATEGORY_OTHER, RESDICTFLAG_USE_FIXUPTYPE_GOTTEN_FROM_RES_DICT, &sMailingLists.ppLists, parse_CritSystemEmailList_Internal);
}

/*
void TimeRestrictionTests(void)
{
	char *pTestStr1 = "ThFrSa 0800 - 2000";
	char *pTestStr2 = "MoTuWeTh 2200 - 1800";
	char *pTestStr3 = "All 1200 - 1540";
	char *pErrorString = NULL;
	
	
	
	U32 iCurTime = timeSecondsSince2000();

	CritSystemEmailList_TimeRestriction *pRestriction1 = TimeRestrictionFromString(pTestStr1, &pErrorString);
	CritSystemEmailList_TimeRestriction *pRestriction2 = TimeRestrictionFromString(pTestStr2, &pErrorString);
	CritSystemEmailList_TimeRestriction *pRestriction3 = TimeRestrictionFromString(pTestStr3, &pErrorString);

	
	bool bMatch1 = DoesTimeMatchRestriction(pRestriction1, iCurTime);
	bool bMatch2 = DoesTimeMatchRestriction(pRestriction2, iCurTime);
	bool bMatch3 = DoesTimeMatchRestriction(pRestriction3, iCurTime);
}
*/

void SendEmail_Simple(char *pTo, char *pSubject, char *pBody)
{
	char **ppRecipients = NULL;

	DivideString(pTo, ",", &ppRecipients, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	if (eaSize(&ppRecipients))
	{
		SendEmailToList(ppRecipients, pSubject, pBody, strlen(pBody), false);
	}

	eaDestroyEx(&ppRecipients, NULL);
}


AUTO_COMMAND;
void SendEmailTest(void)
{
	SendEmail_Simple("awerner", "hi there alex", "You are a big stud");
	SendEmail_Simple("nonexistentperson@f089uq4w093u4-vw349vu", "you do not exist", "do you?");
}






#include "NewControllerTracker_MailingLists_h_Ast.c"