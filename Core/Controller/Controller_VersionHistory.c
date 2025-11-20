#include "controller.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "utilitieslib.h"
#include "controllerpub_h_ast.h"
#include "file.h"
#include "../../libs/ServerLib/objects/ServerLibPrefStore.h"

#define VERSIONHISTORY_NAME "VersionHistory_64encoded"

typedef enum
{
	VHSTATE_WAITINGFORDB,
	VHSTATE_SENTCOMMAND,
	VHSTATE_RECEIVED,
	VHSTATE_FAILED,
} enumVersionHistoryState;

static enumVersionHistoryState eVHState = VHSTATE_WAITINGFORDB;

VersionHistory *gpVersionHistory = NULL;

void GetVersionHistoryComplete_CB(bool bSucceeded, char *pRetVal, void *pUserData)
{
	const char *pVersionString;
	VersionHistoryEntry *pEntry;
	char *pNewVersionString = NULL;
	pVersionString = GetUsefulVersionString();
	eVHState = VHSTATE_RECEIVED;
	
	if (!bSucceeded)
	{
		gpVersionHistory = StructCreate(parse_VersionHistory);
	}
	else
	{
		gpVersionHistory = StructCreate(parse_VersionHistory);

		ParserReadText(pRetVal, parse_VersionHistory, gpVersionHistory, 0);

		if (eaSize(&gpVersionHistory->ppEntries) && stricmp(gpVersionHistory->ppEntries[0]->pPatchName, pVersionString) == 0)
		{
			return;
		}
	}

	pEntry = StructCreate(parse_VersionHistoryEntry);
	pEntry->iStartTime = timeSecondsSince2000();
	pEntry->pPatchName = strdup(pVersionString);

	eaInsert(&gpVersionHistory->ppEntries, pEntry, 0);

	if (!isProductionMode())
	{
		if (eaSize(&gpVersionHistory->ppEntries) > 10)
		{
			eaSetSizeStruct(&gpVersionHistory->ppEntries, parse_VersionHistoryEntry, 10);
		}
	}

	ParserWriteText(&pNewVersionString, parse_VersionHistory, gpVersionHistory, 0, 0, 0);

	PrefStore_SetString(VERSIONHISTORY_NAME, pNewVersionString, NULL, NULL);

	estrDestroy(&pNewVersionString);
}

void VersionHistory_UpdateFSM(void)
{

	switch (eVHState)
	{
	case VHSTATE_WAITINGFORDB:
		if (gpServersByType[GLOBALTYPE_OBJECTDB] && strstri(gpServersByType[GLOBALTYPE_OBJECTDB]->stateString, "dbMasterHandleRequests"))
		{

			PrefStore_GetString(VERSIONHISTORY_NAME, GetVersionHistoryComplete_CB, NULL);
			eVHState = VHSTATE_SENTCOMMAND;

		}
		break;
	}
}