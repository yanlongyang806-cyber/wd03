/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "gslEncounterLog.h"
#include "entity.h"
#include "stashTable.h"
#include "powers.h"
#include "earray.h"
#include "estring.h"
#include "logging.h"
#include "objTransactions.h"
#include "AutoGen/gslEncounterLog_h_ast.h"
#include "AutoGen/gslEncounterLog_h_ast.c"


StashTable sEncounterLogs;
StashTable sMemberIDs;

void gslEncounterLog_Register(S32 encounterID, int iSpawnLevel, int iSpawnTeamSize, const char *pTemplateName)
{
	EncounterLog *pFound = NULL;
	if (!sEncounterLogs)
	{
		sEncounterLogs = stashTableCreateInt(32);
		sMemberIDs = stashTableCreateInt(64);
	}

	if (pFound = gslEncounterLog_LogFromID(encounterID))
	{
		// Already exists
		return;
	}

	pFound = StructCreate(parse_EncounterLog);

	pFound->encounterID = encounterID;
	pFound->iSpawnLevel = iSpawnLevel;
	pFound->iSpawnTeamSize = iSpawnTeamSize;
	pFound->pTemplateName = pTemplateName;
	eaiSetSize(&pFound->piEnemyPowerUses, GetHighestCategorySortID());
	eaiSetSize(&pFound->piFriendlyPowerUses, GetHighestCategorySortID());

	stashIntAddPointer(sEncounterLogs, encounterID, pFound, false);
}

EncounterLog *gslEncounterLog_LogFromID(S32 encounterID)
{
	EncounterLog *pFound = NULL;
	if (sEncounterLogs)
	{
		if (stashIntFindPointer(sEncounterLogs,encounterID, &pFound))
		{
			// Already exists
			return pFound;
		}
	}
	return pFound;
}

void gslEncounterLog_AddEntity(S32 encounterID, Entity *ent, bool bEnemy)
{
	EncounterLog *pFound = gslEncounterLog_LogFromID(encounterID);
	EncounterLogMember *pMember = NULL;
	EntityRef eRef;

	if (!pFound || !ent)
		return;

	eRef = entGetRef(ent);

	pMember = gslEncounterLog_MemberFromEntity(entGetRef(ent));

	if (pMember)
	{
		if (pMember->encounterID != encounterID)
		{
			// If in multiple encounters, mark both as invalid for logging purposes.
			pFound->bInvalid = true;
			pFound = gslEncounterLog_LogFromID(pMember->encounterID);
			if (pFound)
			{
				pFound->bInvalid = true;
			}
		}
		return;
	}

	pMember = StructCreate(parse_EncounterLogMember);
	pMember->eRef = eRef;
	pMember->containerType = entGetType(ent);
	pMember->containerID = entGetContainerID(ent);
	pMember->iLevel = entity_GetCombatLevel(ent);
	pMember->encounterID = encounterID;
	pMember->bEnemy = bEnemy;
	strcpy(pMember->debugName, ent->debugName);
	eaiSetSize(&pMember->piPowerUses, GetHighestCategorySortID());

	stashIntAddPointer(sMemberIDs, eRef, pMember, false);

	eaPush(&pFound->ppMembers, pMember);
}

EncounterLogMember *gslEncounterLog_MemberFromEntity(EntityRef eRef)
{
	EncounterLogMember *pFound = NULL;
	if (sMemberIDs)
	{
		if (stashIntFindPointer(sMemberIDs,eRef, &pFound))
		{
			// Already exists
			return pFound;
		}
	}
	return pFound;
}

void gslEncounterLog_Finish(S32 encounterID)
{
	EncounterLog *pFound = gslEncounterLog_LogFromID(encounterID);
	int i, j;
	static char *pchTemp = 0;
	static char *pchTemp2 = 0;
	F32 averageLevel = 0;
	int playerCount = 0;
	if (!pFound)
		return;

	for (i = 0; i < eaSize(&pFound->ppMembers); i++)
	{
		EncounterLogMember *pMember = pFound->ppMembers[i];
		if (!pMember->bEnemy)
		{
			playerCount ++;
			averageLevel += pMember->iLevel;
		}
	}

	if(playerCount){
		averageLevel /= playerCount;
	}

	for (i = 0; i < eaSize(&pFound->ppMembers); i++)
	{
		EncounterLogMember *pMember = pFound->ppMembers[i];

		estrClear(&pchTemp);

		for (j = 0; j < eaiSize(&pMember->piPowerUses); j++)
		{
			if (j == 0)
				estrConcatf(&pchTemp, "%d", pMember->piPowerUses[j]);
			else
				estrConcatf(&pchTemp, "/%d", pMember->piPowerUses[j]);


			if (pMember->bEnemy)
				pFound->piEnemyPowerUses[j] += pMember->piPowerUses[j];
			else
				pFound->piFriendlyPowerUses[j] += pMember->piPowerUses[j];
		}

		if (pMember->bEnemy)
			objLog(LOG_COMBAT, pMember->containerType, pMember->containerID, 0, pMember->debugName, NULL, NULL, "EncounterMember", NULL, "For %s Enc %s lvl%d/%d Enemy Used Powers: %s", pFound->bInvalid ? "Invalid" : "Valid", pFound->pTemplateName, pFound->iSpawnLevel, pFound->iSpawnTeamSize, pchTemp);
		else
			objLog(LOG_COMBAT, pMember->containerType, pMember->containerID, 0, pMember->debugName, NULL, NULL, "EncounterMember", NULL, "For %s Enc %s lvl%d/%d Friendly Used Powers: %s", pFound->bInvalid ? "Invalid" : "Valid", pFound->pTemplateName, pFound->iSpawnLevel, pFound->iSpawnTeamSize, pchTemp);
	}

	estrClear(&pchTemp);

	for (j = 0; j < eaiSize(&pFound->piFriendlyPowerUses); j++)
	{
		if (j == 0)
			estrConcatf(&pchTemp, "%d", pFound->piFriendlyPowerUses[j]);
		else
			estrConcatf(&pchTemp, "/%d", pFound->piFriendlyPowerUses[j]);
	}

	estrClear(&pchTemp2);

	for (j = 0; j < eaiSize(&pFound->piEnemyPowerUses); j++)
	{
		if (j == 0)
			estrConcatf(&pchTemp2, "%d", pFound->piEnemyPowerUses[j]);
		else
			estrConcatf(&pchTemp2, "/%d", pFound->piEnemyPowerUses[j]);
	}

	objLog(LOG_COMBAT, objServerType(), objServerID(), 0, NULL, NULL, NULL, "EncounterSummary", NULL, "For %s Enc %s lvl%d/%d Enemy Used Powers: %s VS. lvl%f/%d Friendly Used Powers: %s", pFound->bInvalid ? "Invalid" : "Valid", pFound->pTemplateName, pFound->iSpawnLevel, pFound->iSpawnTeamSize, pchTemp2, averageLevel, playerCount, pchTemp);

	for (i = 0; i < eaSize(&pFound->ppMembers); i++)
	{
		EncounterLogMember *pMember = pFound->ppMembers[i];
		stashIntRemovePointer(sMemberIDs, pMember->eRef, NULL);
	}

	stashIntRemovePointer(sEncounterLogs, pFound->encounterID, NULL);
	StructDestroy(parse_EncounterLog, pFound);
}

void gslEncounterLog_AddPowerActivation(EntityRef eRef, PowerDef *pDef)
{
	EncounterLogMember *pMember = gslEncounterLog_MemberFromEntity(eRef);
	if (pMember)
	{
		int iSortType = powerdef_GetCategorySortID(pDef);
		if (iSortType > 0 && iSortType <= eaiSize(&pMember->piPowerUses))
		{
			pMember->piPowerUses[iSortType - 1]++;
		}
	}
}