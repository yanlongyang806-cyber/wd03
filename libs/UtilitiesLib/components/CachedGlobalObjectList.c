
#include "estring.h"
#include "CachedGlobalObjectList.h"
#include "autogen/CachedGlobalObjectList_h_ast.h"
#include "timing.h"
#include "HttpXPathSupport.h"
#include "ResourceInfo.h"

#define MAX_CACHED_LIST_RAM (50 * 1024 * 1024)


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


static int siNextCachedListID = 1;

static CachedGlobalObjectList **sppCachedGlobalObjectLists = NULL;
static StashTable sCachedGobalObjectTable = NULL;

static U32 iCachedGlobalObjectListTotalRAM = 0;


int CreateCachedGlobalObjectList(char *pDescriptiveString, char *pDictionaryName, char ***pppObjectNames)
{
	CachedGlobalObjectList *pList = StructCreate(parse_CachedGlobalObjectList);
	int i;

	int iCurTime = timeSecondsSince2000();


	//while we're here, purge old cached lists
	while (eaSize(&sppCachedGlobalObjectLists) && iCachedGlobalObjectListTotalRAM > MAX_CACHED_LIST_RAM)
	{
		iCachedGlobalObjectListTotalRAM -= sppCachedGlobalObjectLists[0]->iMySizeBytes;
		stashRemovePointer(sCachedGobalObjectTable, sppCachedGlobalObjectLists[0]->name, NULL);
		StructDestroy(parse_CachedGlobalObjectList, sppCachedGlobalObjectLists[0]);
		eaRemove(&sppCachedGlobalObjectLists, 0);
	}

	estrCopy2(&pList->pDescriptiveName, pDescriptiveString);


	for (i=0; i < eaSize(pppObjectNames); i++)
	{
		CachedGlobalObjectListLink *pLink = StructCreate(parse_CachedGlobalObjectListLink);

		estrPrintf(&pLink->pObjName, "%s", (*pppObjectNames)[i]);

/*		estrPrintf(&pLink->pLink, "<a href=\"%s%s%s[%s]\">%s</a>",
				LinkToThisServer(),
				GLOBALOBJECTS_DOMAIN_NAME,
				pDictionaryName,
				(*pppObjectNames)[i],
				(*pppObjectNames)[i]);*/

		eaPush(&pList->ppLinks, pLink);
	}


	pList->iID = siNextCachedListID++;
	pList->eContainerType = NameToGlobalType(pDictionaryName);
	sprintf(pList->name, "%d", pList->iID);

	eaPush(&sppCachedGlobalObjectLists, pList);

	if (GlobalTypeSchemaType(pList->eContainerType) == SCHEMATYPE_PERSISTED)
	{
		estrPrintf(&pList->pApplyAtransaction, "ServerMonTransactionOnContainerList %d $STRING(Apply what transaction to items in this list?)", 
			pList->iID);
	}

	pList->iMySizeBytes = sizeof(CachedGlobalObjectList) + estrGetCapacity(&pList->pApplyAtransaction) 
		+ estrGetCapacity(&pList->pDescriptiveName) + eaCapacity(&pList->ppLinks);

	for (i=0; i < eaSize(pppObjectNames); i++)
	{
		pList->iMySizeBytes += sizeof(CachedGlobalObjectListLink) /*+ estrGetCapacity(&pList->ppLinks[i]->pLink)*/
			+ estrGetCapacity(&pList->ppLinks[i]->pObjName);
	}
	
	stashAddPointer(sCachedGobalObjectTable, pList->name, pList, false);

	iCachedGlobalObjectListTotalRAM += pList->iMySizeBytes;

	pList->iNumObjects = eaSize(&pList->ppLinks);

	return pList->iID;
}



int GetNumCachedGlobObjLists(const char *dictName, void *pUserData)
{
	return eaSize(&sppCachedGlobalObjectLists);
}



CachedGlobalObjectList *GetCachedGlobalObjectListFromID(int iID)
{
	int i;
	int iCount = eaSize(&sppCachedGlobalObjectLists);
	for (i = iCount - 1; i >=0; i--)
	{
		if (sppCachedGlobalObjectLists[i]->iID == iID)
		{
			return sppCachedGlobalObjectLists[i];
		}
	}

	return NULL;
}
void *GetCachedGlobObjList(const char *dictName, const char *itemName, void *pUserData)
{
	return GetCachedGlobalObjectListFromID(atoi(itemName));
}



AUTO_RUN;
void InitCachedObjectListSystem(void)
{
	sCachedGobalObjectTable = stashTableCreateWithStringKeys(16, StashDefault);
	resRegisterDictionaryForStashTable("CachedGlobalObjectLists", RESCATEGORY_SYSTEM, 0, 
		sCachedGobalObjectTable, parse_CachedGlobalObjectList);
}
		
#include "autogen/CachedGlobalObjectList_h_ast.c"

