/***************************************************************************
 *     Copyright (c) 2010, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

#include "file.h"
#include "gclEntity.h"
#include "gclMicroTransactions.h"
#include "chatCommonStructs.h"
#include "mission_common.h"
#include "StringCache.h"
#include "timing.h"
#include "UIGen.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/BulletinUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

bool g_bDisableBulletinAutoPopup = false;
static BulletinsStruct s_BulletinsNew = {0};
static BulletinsStruct s_BulletinsAll = {0};
static BulletinCategory s_BulletinCategoryNone = {0};
static bool s_bRequestedAllBulletins = false;
static bool s_bBulletinsShown = false;

AUTO_STRUCT;
typedef struct BulletinClient
{
	U32 uActivateTime;					AST(NAME(ActivateTime))
	U32 uEventTime;						AST(NAME(EventTime))
	BulletinMessage Message;			AST(NAME(Message))
	BulletinMessage CategoryMessage;	AST(NAME(CategoryMessage))
	BulletinMessage EventMessage;		AST(NAME(EventMessage))
	REF_TO(MicroTransactionDef) hMTDef;	AST(NAME(MicroTransactionDef) REFDICT(MicroTransactionDef))
	U32 uMicroTransID;					AST(NAME(MicroTransactionID))
	char* pchLink;						AST(NAME(Link))
	const char* pchEventTexture;		AST(NAME(EventTexture) POOL_STRING)
	const char* pchCategoryTexture;		AST(NAME(CategoryTexture) POOL_STRING)
	char* pchCategoryName;				AST(NAME(CategoryName))
	S32 eCategory;			
	bool bIsHeader;
	bool bHasEventTakenPlace;
	bool bMissionComplete;
	bool bMostRecentCategory;
} BulletinClient;

AUTO_STRUCT;
typedef struct BulletinCategorySortData
{
	BulletinCategory* pCategory; AST(UNOWNED)
	U32 uCategoryEventTime;
} BulletinCategorySortData;

bool gclBulletin_Init(void)
{
	if (!s_BulletinCategoryNone.pchName)
	{
		s_BulletinCategoryNone.pchName = StructAllocString("None");
	}
	if (!s_bBulletinsShown && 
		eaSize(&s_BulletinsNew.eaDefs) > 0 && 
		!g_bDisableBulletinAutoPopup)
	{
		UIGen *pBulletinGen = ui_GenFind("BulletinUI", kUIGenTypeNone);
		if (pBulletinGen)
		{
			ui_GenSendMessage(pBulletinGen, "Show");
			s_bBulletinsShown = true;
			return true;
		}
	}
	return false;
}

void gclBulletin_DeInit(void)
{
	s_bBulletinsShown = false;
	s_bRequestedAllBulletins = false;
	StructDeInit(parse_BulletinsStruct, &s_BulletinsNew);
}

static BulletinsStruct* gclBulletinsGetData(bool bAllBulletins)
{
	if (bAllBulletins)
	{
		return &s_BulletinsAll;
	}
	return &s_BulletinsNew;
}

static BulletinCategory* gclFindBulletinCategoryByName(BulletinsStruct* pBulletins, const char* pchName)
{
	S32 i;
	if (pchName && pchName[0])
	{
		if (stricmp(pchName, s_BulletinCategoryNone.pchName)==0)
		{
			return &s_BulletinCategoryNone;
		}
		for (i = eaSize(&pBulletins->eaCategories)-1; i >= 0; i--)
		{
			BulletinCategory* pCategory = pBulletins->eaCategories[i];
			if (stricmp(pchName, pCategory->pchName)==0)
			{
				return pCategory;
			}
		}
	}
	return NULL;
}

static BulletinCategory* gclFindBulletinCategoryByType(BulletinsStruct* pBulletins, S32 eType)
{
	S32 i;
	if (eType == s_BulletinCategoryNone.eCategory)
	{
		return &s_BulletinCategoryNone;
	}
	for (i = eaSize(&pBulletins->eaCategories)-1; i >= 0; i--)
	{
		BulletinCategory* pCategory = pBulletins->eaCategories[i];
		if (eType == pCategory->eCategory)
		{
			return pCategory;
		}
	}
	return NULL;
}

static int BulletinSortCategories(const BulletinCategorySortData** ppA, const BulletinCategorySortData** ppB)
{
	const BulletinCategorySortData* pA = (*ppA);
	const BulletinCategorySortData* pB = (*ppB);
	return pA->uCategoryEventTime - pB->uCategoryEventTime;
}

static U32 gclBulletinsGetCategoryEventTime(BulletinsStruct* pBulletins, const char* pchCategory)
{
	S32 i, iCount = 0;
	U32 uAverageTime = 0;
	for (i = eaSize(&pBulletins->eaDefs)-1; i >= 0; i--)
	{
		BulletinDef* pDef = pBulletins->eaDefs[i];
		if (stricmp(pDef->pchCategory, pchCategory)==0)
		{
			if (pDef->pEvent)
			{
				uAverageTime += pDef->pEvent->uEventTime;
			}
			else
			{
				uAverageTime += pDef->uActivateTime;
			}
			iCount++;
		}
	}
	if (iCount)
	{
		return uAverageTime / iCount;
	}
	return 0;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclReceiveBulletins(BulletinsStruct* pData, bool bLogin)
{
	BulletinsStruct* pBulletins = gclBulletinsGetData(!bLogin);
	S32 i, iCount = 0;
	
	// Copy the bulletin data
	StructCopyAll(parse_BulletinsStruct, pData, pBulletins);

	// Sort categories
	if (eaSize(&pBulletins->eaCategories) > 0)
	{
		BulletinCategorySortData** eaCategorySortData = NULL;
		
		// Sort the categories chronologically
		for (i = 0; i < eaSize(&pBulletins->eaCategories); i++)
		{
			BulletinCategory* pCategory = pBulletins->eaCategories[i];
			BulletinCategorySortData* pSortData = StructCreate(parse_BulletinCategorySortData);
			U32 uCategoryEventTime = gclBulletinsGetCategoryEventTime(pBulletins, pCategory->pchName);
			pSortData->pCategory = pCategory;
			pSortData->uCategoryEventTime = uCategoryEventTime;
			eaPush(&eaCategorySortData, pSortData);
		}
		eaQSort(eaCategorySortData, BulletinSortCategories);
		eaClear(&pBulletins->eaCategories);
		for (i = 0; i < eaSize(&eaCategorySortData); i++)
		{
			eaPush(&pBulletins->eaCategories, eaCategorySortData[i]->pCategory);
		}
		eaDestroyStruct(&eaCategorySortData, parse_BulletinCategorySortData);
	}

	// Fixup bulletins
	for (i = 0; i < eaSize(&pBulletins->eaCategories); i++)
	{
		BulletinCategory* pCategory = pBulletins->eaCategories[i];
		// Assign an integer to each category for faster category comparisons
		pCategory->eCategory = ++iCount;
		// Set the MicroTransactionDef handle from the name
		SET_HANDLE_FROM_STRING("MicroTransactionDef", pCategory->pchMicroTransDef, pCategory->hMTDef);
	}
	for (i = eaSize(&pBulletins->eaDefs)-1; i >= 0; i--)
	{
		BulletinDef* pDef = pBulletins->eaDefs[i];
		BulletinCategory* pCategory;
		
		// Copy the category integer value onto the BulletinDef
		pCategory = gclFindBulletinCategoryByName(pBulletins, pDef->pchCategory);
		if (pCategory)
		{
			pDef->eCategory = pCategory->eCategory;
		}
		// Set the MicroTransactionDef handle from the name
		SET_HANDLE_FROM_STRING("MicroTransactionDef", pDef->pchMicroTransDef, pDef->hMTDef);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("BulletinsGetListCount");
S32 gclExprBulletinsGetListCount(bool bShowAll)
{
	BulletinsStruct* pData = gclBulletinsGetData(bShowAll);
	return eaSize(&pData->eaDefs);
}

// Deprecated, use BulletinsGetListCount
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("BulletinGetListCount");
S32 gclExprBulletinGetListCount(void)
{
	return gclExprBulletinsGetListCount(s_bRequestedAllBulletins);
}

// If bCountAll is true then count MTs on all bulletins, otherwise just count MTs on new bulletins
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("BulletinsCountMicroTransactions");
S32 gclExprBulletinsCountMicroTransactions(bool bCountAll)
{
	BulletinsStruct* pBulletins = gclBulletinsGetData(bCountAll);
	S32 i, iCount = 0;
	
	for (i = eaSize(&pBulletins->eaCategories)-1; i >= 0; i--)
	{
		BulletinCategory* pCat = pBulletins->eaCategories[i];
		if (GET_REF(pCat->hMTDef))
		{
			iCount++;
		}
	}
	for (i = eaSize(&pBulletins->eaDefs)-1; i >= 0; i--)
	{
		BulletinDef* pDef = pBulletins->eaDefs[i];
		if (GET_REF(pDef->hMTDef))
		{
			iCount++;
		}
	}
	return iCount;
}

static int BulletinSortCategorizedList(BulletinCategory* pPriorityCategory, 
									   const BulletinClient** ppA, 
									   const BulletinClient** ppB)
{
	const BulletinClient* pA = (*ppA);
	const BulletinClient* pB = (*ppB);

	if (pA->eCategory != pB->eCategory)
	{
		if (pPriorityCategory)
		{
			if (pA->eCategory == pPriorityCategory->eCategory)
				return -1;
			if (pB->eCategory == pPriorityCategory->eCategory)
				return 1;
		}
		return pA->eCategory - pB->eCategory;
	}

	if (pA->bIsHeader != pB->bIsHeader)
		return pB->bIsHeader - pA->bIsHeader;

	if (pA->uEventTime || pB->uEventTime)
		return pA->uEventTime - pB->uEventTime;

	if (pA->uActivateTime || pB->uActivateTime)
		return pB->uActivateTime - pA->uActivateTime;

	return stricmp(pA->Message.pchTranslatedTitle, pB->Message.pchTranslatedTitle);
}

static void gclBulletin_GetCategoryList(BulletinsStruct* pBulletins,
										BulletinClient*** peaData, 
										S32* piCount,
										BulletinCategory** eaShowCategories,
										bool bShowAllCategoriesIfEmpty)
{
	S32 i, j;
	BulletinCategory* pMostRecentCategory = eaTail(&pBulletins->eaCategories);

	if (bShowAllCategoriesIfEmpty && !eaSize(&eaShowCategories))
	{
		eaShowCategories = pBulletins->eaCategories;
	}
	for (i = 0; i < eaSize(&eaShowCategories); i++)
	{
		BulletinCategory* pCat = eaShowCategories[i];
		BulletinClient* pData = eaGetStruct(peaData, parse_BulletinClient, (*piCount)++);
		BulletinMessage* pMessage = eaGet(&pCat->eaMessages, 0);
		pData->bIsHeader = true;
		pData->bHasEventTakenPlace = false;
		pData->bMissionComplete = false;
		pData->bMostRecentCategory = (pCat == pMostRecentCategory);
		pData->uActivateTime = 0;
		pData->uEventTime = 0;
		pData->eCategory = pCat->eCategory;
		pData->pchEventTexture = NULL;
		pData->pchCategoryTexture = allocAddString(pCat->pchTexture);
		StructCopyString(&pData->pchCategoryName, pCat->pchName);
		StructFreeStringSafe(&pData->pchLink);
		StructReset(parse_BulletinMessage, &pData->Message);
		StructReset(parse_BulletinMessage, &pData->EventMessage);
		// Copy the category message
		if (pMessage) {
			StructCopyAll(parse_BulletinMessage, pMessage, &pData->CategoryMessage);
		} else {
			StructReset(parse_BulletinMessage, &pData->CategoryMessage);
		}
		// Set the MicroTransaction ID
		if (IS_HANDLE_ACTIVE(pCat->hMTDef))
		{
			if (!pData->uMicroTransID || !REF_COMPARE_HANDLES(pCat->hMTDef, pData->hMTDef))
			{
				for (j = eaSize(&g_pMTList->ppProducts)-1; j >= 0; j--)
				{
					MicroTransactionProduct* pProduct = g_pMTList->ppProducts[j];
					if (REF_COMPARE_HANDLES(pProduct->hDef, pCat->hMTDef)) {
						pData->uMicroTransID = pProduct->uID;
						break;
					}
				}
				if (j < 0)
				{
					pData->uMicroTransID = 0;
				}
			}
		}
		else
		{
			pData->uMicroTransID = 0;
		}
		COPY_HANDLE(pData->hMTDef, pCat->hMTDef);
	}
}

static void gclBulletin_GetList(UIGen *pGen,
								BulletinsStruct* pBulletins,
								U32 uFilterTime, 
								U32 uEventTime,
								BulletinCategory** eaFilterCategories,
								const char* pchFilterTitle,
								bool bAddCategoryHeaders)
{
	BulletinClient*** peaData = ui_GenGetManagedListSafe(pGen, BulletinClient);
	S32 i, j, iCount = 0;
	U32 uCurrentTime = timeSecondsSince2000();
	BulletinCategory** eaHeaderCategories = NULL;
	Entity* pPlayerEnt = entActivePlayerPtr();
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

	for (i = 0; i < eaSize(&pBulletins->eaDefs); i++)
	{
		BulletinDef* pDef = pBulletins->eaDefs[i];
		BulletinMessage* pMessage = eaGet(&pDef->eaMessages, 0);
		BulletinCategory* pCat = gclFindBulletinCategoryByType(pBulletins, pDef->eCategory);
		BulletinMessage* pCategoryMessage = pCat && pCat->eaMessages ? eaGet(&pCat->eaMessages, 0) : NULL;
		BulletinMessage* pEventMessage = pDef->pEvent ? eaGet(&pDef->pEvent->eaMessages, 0) : NULL;
		const char* pchTitle = SAFE_MEMBER(pMessage, pchTranslatedTitle);

		if (uFilterTime <= pDef->uActivateTime &&
			(!uEventTime || uEventTime <= SAFE_MEMBER(pDef->pEvent, uEventTime)) &&
			(!eaSize(&eaFilterCategories) || eaFind(&eaFilterCategories, pCat) >= 0) &&
			(!pchFilterTitle || !pchFilterTitle[0] || strstri(pchTitle,pchFilterTitle)))
		{
			BulletinClient* pData = eaGetStruct(peaData, parse_BulletinClient, iCount++);
			pData->uActivateTime = pDef->uActivateTime;
			pData->uEventTime = SAFE_MEMBER(pDef->pEvent, uEventTime);
			pData->eCategory = pDef->eCategory;
			COPY_HANDLE(pData->hMTDef, pDef->hMTDef);
			pData->pchEventTexture = allocAddString(SAFE_MEMBER(pDef->pEvent, pchTexture));
			pData->pchCategoryTexture = NULL;
			StructCopyString(&pData->pchLink, pDef->pchLink);
			StructFreeStringSafe(&pData->pchCategoryName);
			pData->bIsHeader = false;
			pData->bMissionComplete = false;
			pData->bHasEventTakenPlace = false;
			pData->bMostRecentCategory = false;

			if (pDef->pEvent) 
			{
				// Check to see if the player has already completed the mission associated with the event
				if (pDef->pEvent->pchMissionDef)
				{
					MissionDef* pMissionDef = missiondef_DefFromRefString(pDef->pEvent->pchMissionDef);
					if (mission_GetCompletedMissionByDef(pMissionInfo, pMissionDef))
					{
						pData->bMissionComplete = true;
					}
				}
				// Check to see if the player can participate in the event
				pData->bHasEventTakenPlace = (uCurrentTime >= pDef->pEvent->uEventTime);
			}
			// Copy the bulletin message
			if (pMessage) {
				StructCopyAll(parse_BulletinMessage, pMessage, &pData->Message);
			} else {
				StructReset(parse_BulletinMessage, &pData->Message);
			}
			// Copy the category message
			if (pCategoryMessage && !bAddCategoryHeaders) {
				StructCopyAll(parse_BulletinMessage, pCategoryMessage, &pData->CategoryMessage);
			} else {
				StructReset(parse_BulletinMessage, &pData->CategoryMessage);
			}
			// Copy the event message
			if (pEventMessage) {
				StructCopyAll(parse_BulletinMessage, pEventMessage, &pData->EventMessage);
			} else {
				StructReset(parse_BulletinMessage, &pData->EventMessage);
			}

			// Set the MicroTransaction ID
			if (IS_HANDLE_ACTIVE(pDef->hMTDef))
			{
				if (!pData->uMicroTransID || !REF_COMPARE_HANDLES(pDef->hMTDef, pData->hMTDef))
				{
					for (j = eaSize(&g_pMTList->ppProducts)-1; j >= 0; j--)
					{
						MicroTransactionProduct* pProduct = g_pMTList->ppProducts[j];
						if (REF_COMPARE_HANDLES(pProduct->hDef, pDef->hMTDef)) {
							pData->uMicroTransID = pProduct->uID;
							break;
						}
					}
					if (j < 0)
					{
						pData->uMicroTransID = 0;
					}
				}
			}
			else
			{
				pData->uMicroTransID = 0;
			}
			COPY_HANDLE(pData->hMTDef, pDef->hMTDef);

			if (bAddCategoryHeaders && pCat)
			{
				eaPushUnique(&eaHeaderCategories, pCat);
			}
		}
	}

	if (bAddCategoryHeaders)
	{
		gclBulletin_GetCategoryList(pBulletins, peaData, &iCount, eaHeaderCategories, false);
	}
	eaSetSizeStruct(peaData, parse_BulletinClient, iCount);
	if (eaSize(peaData) > 1)
	{
		eaQSort_s(*peaData, BulletinSortCategorizedList, NULL);
	}
	ui_GenSetManagedListSafe(pGen, peaData, BulletinClient, true);
	eaDestroy(&eaHeaderCategories);
}

// Gets a list of BulletinCategory pointers based on a comma delimited list
static void gclBulletinsGetCategoriesFromString(BulletinsStruct* pBulletins,
												const char* pchCategories, 
												BulletinCategory*** peaCategoriesOut)
{
	static BulletinCategory** s_eaCategories = NULL;
	eaClearFast(&s_eaCategories);
	if (pchCategories && pchCategories[0])
	{
		char* pchContext;
		char* pchStart;
		char* pchCatCopy;
		strdup_alloca(pchCatCopy, pchCategories);
		pchStart = strtok_r(pchCatCopy, " ,\t\r\n", &pchContext);
		do
		{
			if (pchStart)
			{
				BulletinCategory* pCategory = gclFindBulletinCategoryByName(pBulletins, pchStart);
				if (pCategory)
				{
					eaPushUnique(&s_eaCategories, pCategory);
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
	(*peaCategoriesOut) = s_eaCategories;
}

// Get a list of bulletin categories
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetBulletinCategoryList");
void gclExprGenGetBulletinCategoryList(SA_PARAM_NN_VALID UIGen *pGen, 
									   const char* pchFilterCategories,
									   bool bSortMostRecentCategoryToFront)
{
	S32 iCount = 0;
	BulletinClient*** peaData = ui_GenGetManagedListSafe(pGen, BulletinClient);
	BulletinsStruct* pBulletins = gclBulletinsGetData(s_bRequestedAllBulletins);
	BulletinCategory** eaCategories = NULL;
	gclBulletinsGetCategoriesFromString(pBulletins, pchFilterCategories, &eaCategories);
	gclBulletin_GetCategoryList(pBulletins, peaData, &iCount, eaCategories, true);
	eaSetSizeStruct(peaData, parse_BulletinClient, iCount);
	if (eaSize(peaData) > 1)
	{
		BulletinCategory* pCategory = NULL;
		if (bSortMostRecentCategoryToFront)
		{
			pCategory = eaTail(&pBulletins->eaCategories);
		}
		eaQSort_s(*peaData, BulletinSortCategorizedList, pCategory);
	}
	ui_GenSetManagedListSafe(pGen, peaData, BulletinClient, true);
}

// Get a list of bulletins with category headers inserted
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetCategorizedBulletinList");
void gclExprGenGetCategorizedBulletinList(SA_PARAM_NN_VALID UIGen *pGen, 
										  U32 uFilterTime, 
										  U32 uEventTime,
										  const char* pchFilterTitle,
										  const char* pchFilterCategories,
										  bool bShowAllBulletins)
{
	BulletinsStruct* pBulletins = gclBulletinsGetData(bShowAllBulletins);
	BulletinCategory** eaCategories = NULL;
	gclBulletinsGetCategoriesFromString(pBulletins, pchFilterCategories, &eaCategories);
	gclBulletin_GetList(pGen, pBulletins, uFilterTime, uEventTime, eaCategories, pchFilterTitle, true);
}


// Get a list of bulletins with category and event filters
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetBulletinListByCategory");
void gclExprGenGetBulletinListByCategory(SA_PARAM_NN_VALID UIGen *pGen, 
										 U32 uFilterTime, 
										 U32 uEventTime,
										 const char* pchFilterTitle,
										 const char* pchFilterCategories,
										 bool bShowAllBulletins)
{
	BulletinsStruct* pBulletins = gclBulletinsGetData(bShowAllBulletins);
	BulletinCategory** eaCategories = NULL;
	gclBulletinsGetCategoriesFromString(pBulletins, pchFilterCategories, &eaCategories);
	gclBulletin_GetList(pGen, pBulletins, uFilterTime, uEventTime, eaCategories, pchFilterTitle, false);
}

// Get a list of bulletins with basic filtering options
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetBulletinList");
void gclExprGenGetBulletinList(SA_PARAM_NN_VALID UIGen *pGen, U32 uFilterTime, const char* pchFilterTitle)
{
	BulletinsStruct* pBulletins = gclBulletinsGetData(s_bRequestedAllBulletins);
	gclBulletin_GetList(pGen, pBulletins, uFilterTime, 0, NULL, pchFilterTitle, false);
}

AUTO_COMMAND ACMD_NAME(BulletinsRequestAll) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclCmdRequestAllBulletins(void)
{
	if (!s_bRequestedAllBulletins || isDevelopmentMode())
	{
		ServerCmd_gslBulletins_RequestAll();
		s_bRequestedAllBulletins = true;
	}
}

#include "AutoGen/BulletinUI_c_ast.c"
