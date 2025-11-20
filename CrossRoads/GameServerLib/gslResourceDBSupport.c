#pragma once

#include "CostumeCommon.h"
#include "CostumeCommonLoad.h"
#include "ResourceDBSupport.h"
#include "contact_common.h"
#include "earray.h"
#include "EntityLib.h"
#include "error.h"
#include "gslMission.h"
#include "gslMission_transact.h"
#include "logging.h"
#include "mission_common.h"
#include "staticworld\worldGridPrivate.h"

#include "gslResourceDBSupport_c_ast.h"

//NOTE NOTE NOTE THIS MUST BE KEPT IN SYNC WITH THE ONE IN aslMapManager.c
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ResourceDB_ReceiveZoneMapInfo(char *pName, ACMD_OWNABLE(ZoneMapInfo) ppZmInfo, char *pComment)
{

	verbose_printf("Receiving ZoneMapInfo %s. Comment: %s\n", pName, pComment);

	if (RefSystem_ReferentFromString(g_ZoneMapDictionary, pName))
	{
		//already got it, do nothing, it will get deleted automatically
		return;
	}

	if (ResourceDBHandleGetObject(g_ZoneMapDictionary, pName, *ppZmInfo, pComment))
	{
		verbose_printf("Added to resource system\n");
		*ppZmInfo = NULL;
	}
}

typedef struct DeferredEntityToAdd
{
	ContainerID entContainerID;

	TransactionReturnCallback cb;
	UserData data;
} DeferredEntityToAdd;

typedef struct DeferredEntityToInit
{
	ContainerID entContainerID;
} DeferredEntityToInit;

AUTO_STRUCT;
typedef struct DeferredMissionTracker
{
	char *pMissionName;
	REF_TO(MissionDef) hRef;

	StashTable stashDeferredEntitiesToAdd_ByEntContainerID; NO_AST
	StashTable stashDeferredEntitiesToInit_ByEntContainerID; NO_AST
} DeferredMissionTracker;

static StashTable sDeferredMissionTrackers = NULL;

DeferredMissionTracker *FindOrCreateDeferredMissionTracker(const char *pMissionName)
{
	DeferredMissionTracker *pRetVal;

	if (!sDeferredMissionTrackers)
	{
		sDeferredMissionTrackers = stashTableCreateWithStringKeys(10, StashDefault);
	}
	
	if (!stashFindPointer(sDeferredMissionTrackers, pMissionName, &pRetVal))
	{
		pRetVal = StructCreate(parse_DeferredMissionTracker);

		pRetVal->stashDeferredEntitiesToAdd_ByEntContainerID = stashTableCreateInt(16);
		pRetVal->stashDeferredEntitiesToInit_ByEntContainerID = stashTableCreateInt(16);

		pRetVal->pMissionName = strdup(pMissionName);
		SET_HANDLE_FROM_REFDATA(g_MissionDictionary, pMissionName, pRetVal->hRef);

		stashAddPointer(sDeferredMissionTrackers, pRetVal->pMissionName, pRetVal, false);
	}

	return pRetVal;
}

void MissionAdd_ResourceDBDeferred(Entity *pEnt, const char *pMissionName, TransactionReturnCallback cb, UserData data)
{
	DeferredMissionTracker *pTracker = FindOrCreateDeferredMissionTracker(pMissionName);

	if(GET_REF(pTracker->hRef))
	{
		AssertOrAlert("MISSIONADD_DEFER_FAILURE", "Mission being added deferred when it already exists");
		missioninfo_AddMission_Fail(cb, data);
		return;
	}

	{
		ContainerID entContainerID = entGetContainerID(pEnt);
		DeferredEntityToAdd *pDeferredEntityToAdd = calloc(1, sizeof(DeferredEntityToAdd));

		pDeferredEntityToAdd->entContainerID = entContainerID;
		pDeferredEntityToAdd->cb = cb;
		pDeferredEntityToAdd->data = data;

		if(!stashIntAddPointer(pTracker->stashDeferredEntitiesToAdd_ByEntContainerID, entContainerID, pDeferredEntityToAdd, /*bOverwriteIfFound=*/false))
		{
			missioninfo_AddMission_Fail(cb, data);
			free(pDeferredEntityToAdd);
		}
	}
}

void MissionInit_ResourceDBDeferred(Entity *pEnt, const char *pMissionName)
{
	DeferredMissionTracker *pTracker = FindOrCreateDeferredMissionTracker(pMissionName);

	if (GET_REF(pTracker->hRef))
	{
		AssertOrAlert("MISSIONINIT_DEFER_FAILURE", "Mission being init'ed deferred when it already exists");
		return;
	}

	{
		ContainerID entContainerID = entGetContainerID(pEnt);
		DeferredEntityToInit *pDeferredEntityToInit = calloc(1, sizeof(DeferredEntityToInit));

		pDeferredEntityToInit->entContainerID = entContainerID;

		if(!stashIntAddPointer(pTracker->stashDeferredEntitiesToInit_ByEntContainerID, entContainerID, pDeferredEntityToInit, /*bOverwriteIfFound=*/false))
			free(pDeferredEntityToInit);
	}
}

void DoDeferredMissionTrackers(const char *pMissionName, MissionDef *pMissionDef)
{
	DeferredMissionTracker *pTracker;

	if (!sDeferredMissionTrackers)
	{
		return;
	}

	if(stashRemovePointer(sDeferredMissionTrackers, pMissionName, &pTracker))
	{
		StashTableIterator stashIterator;
		StashElement element;

		stashGetIterator(pTracker->stashDeferredEntitiesToAdd_ByEntContainerID, &stashIterator);
		while(stashGetNextElement(&stashIterator, &element))
		{
			DeferredEntityToAdd *pDeferredEntityToAdd = stashElementGetPointer(element);

			Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pDeferredEntityToAdd->entContainerID);
			if(pEntity)
			{
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEntity);
				if(pMissionDef)
				{
					if(pMissionInfo)
					{
						missioninfo_AddMission(entGetPartitionIdx(pEntity), pMissionInfo, pMissionDef, NULL, pDeferredEntityToAdd->cb, pDeferredEntityToAdd->data);

						// clear callback and data because missioninfo_AddMission is responsible for it
						pDeferredEntityToAdd->cb = NULL;
						pDeferredEntityToAdd->data = NULL;
					}
				}
				else
				{
					// Namespace mission no longer exists so clean it up
					Mission *pMission = mission_GetMissionByOrigName(pMissionInfo, pMissionName);
					if(pMission)
					{
						entLog(LOG_GSL, pEntity, "Mission-MissingNamespaceMission", "No namespace MissionDef found for mission '%s'. Mission will be removed.", pMissionName);
						pMission->infoOwner = pMissionInfo;
						missioninfo_DropMission(pEntity, pMissionInfo, pMission);
					}
				}
			}

			// this will NOOP if we NULLed out the callback and data above
			missioninfo_AddMission_Fail(pDeferredEntityToAdd->cb, pDeferredEntityToAdd->data);

			free(pDeferredEntityToAdd);
		}
		stashTableDestroy(pTracker->stashDeferredEntitiesToAdd_ByEntContainerID);

		stashGetIterator(pTracker->stashDeferredEntitiesToInit_ByEntContainerID, &stashIterator);
		while(stashGetNextElement(&stashIterator, &element))
		{
			DeferredEntityToInit *pDeferredEntityToInit = stashElementGetPointer(element);

			Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pDeferredEntityToInit->entContainerID);
			if(pEntity)
			{
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEntity);
				Mission *pMission = mission_GetMissionByOrigName(pMissionInfo, pMissionName);
				if(pMission)
				{
					if(pMissionDef)
					{
						mission_PostMissionCreateInitRecursive(entGetPartitionIdx(pEntity), pMission, pMissionInfo, NULL, NULL, true);
					}
					else
					{
						// Namespace mission no longer exists so clean it up
						entLog(LOG_GSL, pEntity, "Mission-MissingNamespaceMission", "No namespace MissionDef found for mission '%s'. Mission will be removed.", pMissionName);
						pMission->infoOwner = pMissionInfo;
						missioninfo_DropMission(pEntity, pMissionInfo, pMission);
					}
				}
			}

			free(pDeferredEntityToInit);
		}
		stashTableDestroy(pTracker->stashDeferredEntitiesToInit_ByEntContainerID);

		StructDestroy(parse_DeferredMissionTracker, pTracker);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ResourceDB_ReceiveMissionDef(char *pName, ACMD_OWNABLE(MissionDef) ppMissionDef, char *pComment)
{
	verbose_printf("Receiving MissionDef %s. Comment: %s\n", pName, pComment);

	if (RefSystem_ReferentFromString(g_MissionDictionary, pName))
	{
		//already got it, do nothing, it will get deleted automatically
		return;
	}

	// Properly set up the mission's ref strings before adding it to the system
	if (*ppMissionDef)
	{
		missiondef_CreateRefStringsRecursive(*ppMissionDef, NULL);
	}

	if (ResourceDBHandleGetObject(g_MissionDictionary, pName, *ppMissionDef, pComment))
	{
		verbose_printf("Added to resource system\n");

		missiondef_RefreshOverrides(*ppMissionDef);

		DoDeferredMissionTrackers(pName, *ppMissionDef);

		*ppMissionDef = NULL;
	}
	else
	{
		DoDeferredMissionTrackers(pName, NULL);
	}

}


AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ResourceDB_ReceiveContactDef(char *pName, ACMD_OWNABLE(ContactDef) ppContactDef, char *pComment)
{
	verbose_printf("Receiving ContactDef %s. Comment: %s\n", pName, pComment);

	if (RefSystem_ReferentFromString(g_ContactDictionary, pName))
	{
		//already got it, do nothing, it will get deleted automatically
		return;
	}

	if (ResourceDBHandleGetObject(g_ContactDictionary, pName, *ppContactDef, pComment))
	{
		verbose_printf("Added to resource system\n");
		*ppContactDef = NULL;
	}
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ResourceDB_ReceivePlayerCostume(char *pName, ACMD_OWNABLE(PlayerCostume) ppPlayerCostume, char *pComment)
{
	verbose_printf("Receiving PlayerCostume %s. Comment: %s\n", pName, pComment);

	if (RefSystem_ReferentFromString(g_hPlayerCostumeDict, pName))
	{
		//already got it, do nothing, it will get deleted automatically
		return;
	}

	if (ResourceDBHandleGetObject(g_hPlayerCostumeDict, pName, *ppPlayerCostume, pComment))
	{
		verbose_printf("Added to resource system\n");

		*ppPlayerCostume = NULL;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ResourceDB_ReceiveItemDef(char *pName, ACMD_OWNABLE(ItemDef) ppItemDef, char* pComment)
{
	verbose_printf("Receiving ItemDef %s. Comment: %s\n", pName, pComment);

	if (RefSystem_ReferentFromString(g_hItemDict, pName))
	{
		//already got it, do nothing, it will get deleted automatically
		return;
	}

	if (ResourceDBHandleGetObject(g_hItemDict, pName, *ppItemDef, pComment))
	{
		verbose_printf("Added to resource system\n");

		*ppItemDef = NULL;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ResourceDB_ReceiveRewardTable(char *pName, ACMD_OWNABLE(RewardTable) ppRewardTable, char* pComment)
{
	verbose_printf("Receiving RewardTable %s. Comment: %s\n", pName, pComment);

	if (RefSystem_ReferentFromString(g_hRewardTableDict, pName))
	{
		//already got it, do nothing, it will get deleted automatically
		return;
	}

	if (ResourceDBHandleGetObject(g_hRewardTableDict, pName, *ppRewardTable, pComment))
	{
		verbose_printf("Added to resource system\n");

		*ppRewardTable = NULL;
	}
}


#include "gslResourceDBSupport_c_ast.c"
