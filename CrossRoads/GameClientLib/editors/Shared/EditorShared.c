/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EditorShared.h"

#include "EArray.h"
#include "StashTable.h"
#include "WorldGrid.h"
#include "StringCache.h"
#include "Entity.h"
#include "dynSkeleton.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#ifndef NO_EDITORS

StashTable s_EncounterActorEntityRefStash = NULL;

static void encounterStashClear(StashTable pStash)
{
	stashTableDestroy(pStash);
}

void editor_RefreshAllShared(void)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	WorldZoneMapScope *pScope = zmapGetScope(zmap);
	int i;

	if(s_EncounterActorEntityRefStash)
		stashTableClearEx(s_EncounterActorEntityRefStash, NULL, encounterStashClear);
	else
		s_EncounterActorEntityRefStash = stashTableCreateWithStringKeys(8, StashDefault | StashCaseSensitive);

	// Iterate all the encounters that are loaded for editing, because we can only get properties if it's loaded for editing
	if(pScope)
	{
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(pScope->scope.name_to_obj, &iter);
		while(stashGetNextElement(&iter, &elem))
		{
			WorldEncounterObject *obj = stashElementGetPointer(elem);
			char *pcEncounterName = stashElementGetStringKey(elem);

			if(obj && obj->type == WL_ENC_ENCOUNTER && obj->tracker && obj->tracker->def)
			{
				WorldEncounterProperties *pProps = obj->tracker->def->property_structs.encounter_properties;
				if(pProps)
				{
					StashTable actorEntityRefStash = NULL;

					if(!stashFindPointer(s_EncounterActorEntityRefStash, pcEncounterName, (void **)&actorEntityRefStash))
					{
						actorEntityRefStash = stashTableCreateWithStringKeys(8, StashDefault | StashCaseSensitive);
						stashAddPointer(s_EncounterActorEntityRefStash, pcEncounterName, actorEntityRefStash, false);
					}

					for(i=eaSize(&pProps->eaActors)-1; i>=0; --i)
					{
						stashAddPointer(actorEntityRefStash, pProps->eaActors[i]->pcName, NULL, true);
						ServerCmd_encounter_RequestEncounterActorEntityRef(pcEncounterName, pProps->eaActors[i]->pcName);
					}
				}
			}
		}
	}
}

static void listDtor(const char **value)
{
	eaDestroy(&value);
}

static int compareStrings(const char** left, const char** right)
{
	return stricmp(*left, *right);
}

void editor_FillEncounterNames(const char ***eaEncounterList)
{
	eaClear(eaEncounterList);

	if(s_EncounterActorEntityRefStash)
	{
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(s_EncounterActorEntityRefStash, &iter);
		while(stashGetNextElement(&iter, &elem))
		{
			char *pcEncounterName = stashElementGetStringKey(elem);
			eaPush(eaEncounterList, pcEncounterName);
		}
	}

	eaQSort(*eaEncounterList, compareStrings);
}

void editor_FillEncounterActorNames(const char *pcEncounterName, const char ***eaActorList)
{
	eaClear(eaActorList);

	if(s_EncounterActorEntityRefStash)
	{
		StashTable actorEntityRefStash = NULL;
		if(stashFindPointer(s_EncounterActorEntityRefStash, pcEncounterName, (void **)&actorEntityRefStash))
		{
			StashTableIterator iter;
			StashElement elem;

			stashGetIterator(actorEntityRefStash, &iter);
			while(stashGetNextElement(&iter, &elem))
			{
				char *pcActorName = stashElementGetStringKey(elem);
				eaPush(eaActorList, pcActorName);
			}
		}
	}

	eaQSort(*eaActorList, compareStrings);
}

bool editor_GetEncounterActorEntityRef(const char *pcEncounterName, const char *pcActorName, EntityRef *pEntityRef)
{
	if(s_EncounterActorEntityRefStash)
	{
		StashTable actorEntityRefStash = NULL;
		if(stashFindPointer(s_EncounterActorEntityRefStash, pcEncounterName, (void **)&actorEntityRefStash))
		{
			if(stashFindPointer(actorEntityRefStash, pcActorName, (void **)pEntityRef))
				return true;
		}
	}

	return false;
}

void editor_FillEntityRefBoneNames(EntityRef entref, const char ***eaBoneList)
{
	editor_FillEntityBoneNames(entFromEntityRefAnyPartition(entref), eaBoneList);
}

void editor_FillEntityBoneNames(Entity *pEntity, const char ***eaBoneList)
{
	eaClear(eaBoneList);

	if(pEntity)
	{
		DynSkeleton* pSkeleton = dynSkeletonFromGuid(pEntity->dyn.guidSkeleton);
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(pSkeleton->stBoneTable, &iter);
		while(stashGetNextElement(&iter, &elem))
		{
			const char *pcBoneName = stashElementGetStringKey(elem);
			eaPush(eaBoneList, pcBoneName);
		}
	}

	eaQSort(*eaBoneList, compareStrings);
}

#endif

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(9);
void encounter_UpdateEncounterActorEntityRef(const char* pcEncounterName, const char* pcActorName, EntityRef entref)
{
#ifndef NO_EDITORS
	if(!pcActorName || !pcEncounterName)
		return;

	if(s_EncounterActorEntityRefStash)
	{
		StashTable actorEntityRefStash = NULL;
		if(stashFindPointer(s_EncounterActorEntityRefStash, pcEncounterName, (void **)&actorEntityRefStash))
			stashAddPointer(actorEntityRefStash, allocAddString(pcActorName), (const void *)entref, true);
	}
#endif
}
