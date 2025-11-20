
/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityCommon.h"
#include "allegiance.h"
#include "contact_common.h"
#include "Player.h"
#include "ResourceManager.h"

#include "Entity.h"
#include "file.h"
#include "HashFunctions.h"
#include "rand.h"
#include "timing.h"

#include "autogen/ActivityCommon_h_ast.h"
#include "autogen/ActivityCalendar_h_ast.h"

#ifdef APPSERVER
#include "objTransactions.h"
#endif
#ifdef GAMESERVER
#include "gslBulletins.h"
#include "gslActivity.h"
#endif
#ifdef GAMECLIENT
#include "gclEntity.h"
#endif

#include "queue_common.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Dictionary holding the events
DictionaryHandle g_hEventDictionary = NULL;

ActivityDefs g_ActivityDefs = {0};

EventConfig g_EventConfig = {0};

void EventDefValidateRefs(EventDef *pEventDef)
{
	S32 i, j;

#ifdef GAMESERVER
	// Validate references
	if (IS_HANDLE_ACTIVE(pEventDef->hLinkedQueue) && GET_REF(pEventDef->hLinkedQueue) == NULL)
	{
		Errorf("Activity Event %s references an invalid queue def: %s", pEventDef->pchEventName, REF_STRING_FROM_HANDLE(pEventDef->hLinkedQueue));
	}
#endif

	if (pEventDef->pchParentEvent && *pEventDef->pchParentEvent)
	{
		if (EventDef_Find(pEventDef->pchParentEvent) == NULL)
		{
			Errorf("Activity Event %s has a parent event '%s' that does not exist", pEventDef->pchEventName, pEventDef->pchParentEvent);
		}
	}

	if (pEventDef->pBulletin)
	{
		if (!GET_REF(pEventDef->pBulletin->msgTitle.hMessage))
		{
			Errorf("Activity Event %s has a bulletin but doesn't specify a title message", pEventDef->pchEventName);
		}
		if (!GET_REF(pEventDef->pBulletin->msgMessageBody.hMessage))
		{
			Errorf("Activity Event %s has a bulletin but doesn't specify a body message", pEventDef->pchEventName);
		}
	}

	for (i = eaSize(&pEventDef->eaWarpSpawns)-1; i >= 0; i--)
	{
		EventWarpDef *pWarpDef = pEventDef->eaWarpSpawns[i];
		if (!pWarpDef->pchAllegianceName || !pWarpDef->pchAllegianceName[0])
		{
			Errorf("Activity Event %s has an allegiance-specific warp spawn with no allegiance", pEventDef->pchEventName);
		}
		else if (!allegiance_FindByName(pWarpDef->pchAllegianceName))
		{
			Errorf("Activity Event %s has a warp spawn that specifies a non-existent allegiance %s", pEventDef->pchEventName, pWarpDef->pchAllegianceName);
		}
		if (!pWarpDef->pchSpawnMap || !pWarpDef->pchSpawnMap[0])
		{
			Errorf("Activity Event %s has a warp spawn with no map", pEventDef->pchEventName);
		}
	}
	for (i = eaSize(&pEventDef->eaContacts)-1; i >= 0; i--)
	{
		EventContactDef *pEventContact = pEventDef->eaContacts[i];
		ContactDef *pContactDef = contact_DefFromName(pEventContact->pchContactDef);
		if (!pContactDef)
		{
			Errorf("Activity Event %s has a contact entry that references a non-existent ContactDef %s", pEventDef->pchEventName, pEventContact->pchContactDef);
		}
		else if (pEventContact->pchDialogName && pEventContact->pchDialogName[0])
		{
			SpecialDialogBlock *pSpecialDialog = contact_SpecialDialogFromName(pContactDef, pEventContact->pchDialogName);
			if (!pSpecialDialog)
			{
				Errorf("Activity Event %s has a contact entry that references a non-existent SpecialDialog %s for Contact %s", 
					pEventDef->pchEventName, pEventContact->pchDialogName, pEventContact->pchContactDef);
			}
		}
		for (j = eaSize(&pEventContact->ppchAllegiances)-1; j >= 0; j--)
		{
			const char* pchAllegianceName = pEventContact->ppchAllegiances[j];
			if (!pchAllegianceName || !pchAllegianceName[0])
			{
				Errorf("Activity Event %s has an allegiance-specific contact with an invalid allegiance", pEventDef->pchEventName);
			}
			else if (!allegiance_FindByName(pchAllegianceName))
			{
				Errorf("Activity Event %s has a contact that specifies a non-existent allegiance %s", pEventDef->pchEventName, pchAllegianceName);
			}
		}
	}
}

void EventDefValidate(EventDef *pEventDef)
{
	char contextualNameBuffer[1024];
	sprintf(contextualNameBuffer, "[Activity Event %s]", pEventDef->pchEventName);
	
	ShardEventTiming_Validate(&(pEventDef->ShardTimingDef), contextualNameBuffer);
}


static int Activity_ValidateEventsCB(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, EventDef* pEventDef, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
		{
			ShardEventTiming_Fixup(&(pEventDef->ShardTimingDef),hashStringInsensitive(pEventDef->pchEventName));

			if (IsGameServerBasedType())
			{
				EventDefValidate(pEventDef);
			}
			return VALIDATE_HANDLED;
		}
		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			if (IsGameServerBasedType())
			{
#ifdef GAMESERVER
				if (pEventDef->pBulletin)
				{
					gslBulletins_AddEventBulletin(pEventDef);
				}
#endif
				EventDefValidateRefs(pEventDef);
				return VALIDATE_HANDLED;
			}
		}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void Activity_RegisterEventDictionary(void)
{
	g_hEventDictionary = RefSystem_RegisterSelfDefiningDictionary("Event", false, parse_EventDef, true, true, NULL);

	resDictManageValidation(g_hEventDictionary, Activity_ValidateEventsCB);

	if (IsServer()) 
	{
		resDictProvideMissingResources(g_hEventDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hEventDictionary, ".EventName", NULL, NULL, NULL, NULL);
		}
	} 
	else 
	{
		resDictRequestMissingResources(g_hEventDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}


static void Activity_LoadEvents()
{
	// Load game progression nodes, do not load into shared memory
	resLoadResourcesFromDisk(g_hEventDictionary, "defs/events/", ".events", "Event.bin", PARSER_OPTIONALFLAG);
}

static void Activity_LoadActivities()
{
	StructReset(parse_ActivityDefs, &g_ActivityDefs);
	
	ParserLoadFiles("defs/events/", ".activities", "activities.bin", PARSER_OPTIONALFLAG, parse_ActivityDefs, &g_ActivityDefs);
}


static void Activity_ReloadActivities_CB(const char *relpath, int when)
{
	// WOLF[10Feb12]  TODO: Make this work. There are synchronization issues between servers and the map manager.
	//   where if we reload the def files, running events and activities need to be shut down on both sides
	//   in a predictable fashion. Or something.
//	loadstart_printf("Reloading Activities...");
//	Activity_LoadActivities();
//	loadend_printf(" done.");
}

static void Activity_ReloadEvents_CB(const char *relpath, int when)
{
	// WOLF[10Feb12]  TODO: Make this work. There are synchronization issues between servers and the map manager.
	//   where if we reload the def files, running events and activities need to be shut down on both sides
	//   in a predictable fashion. Or something. It also needs to deal with copying over enabled/disable status
	//   of events by recopying the status out of the events container.
//	loadstart_printf("Reloading Events...");
//	Activity_LoadEvents();
//	loadend_printf(" done.");
}

static void ActivityStartup(void)
{
	Activity_LoadEvents();
	Activity_LoadActivities();

	if (isDevelopmentMode())
	{
		// Have reload take effect immediately
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/events/*.events", Activity_ReloadEvents_CB);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/events/*.activities", Activity_ReloadActivities_CB);
	}
}

AUTO_STARTUP(Activities) ASTRT_DEPS(ActivityCalendar, EventConfig);
void ActivityAppServerStartup(void)
{
	ActivityStartup();
}

AUTO_STARTUP(ActivitiesClient) ASTRT_DEPS(ActivityCalendar);
void ActivityStartupClient(void)
{
	if (isDevelopmentMode())
	{
		// Load ActivityDefs in development mode for editors
		Activity_LoadActivities();
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/events/*.activities", Activity_ReloadActivities_CB);
		
	}
}

bool EventDef_MapCheck(EventDef *pDef, const char *pchMapName)
{
	int i;

	if (!pDef)
		return true;

	if(eaSize(&pDef->ppchMapExcluded) > 0)
	{
		for(i=0;i<eaSize(&pDef->ppchMapExcluded);i++)
		{
			if(strcmp(pchMapName,pDef->ppchMapExcluded[i]) == 0)
				return false;
		}
	}

	if(eaSize(&pDef->ppchMapIncluded) > 0)
	{
		for(i=eaSize(&pDef->ppchMapIncluded)-1;i>=0;i--)
		{
			if(strcmp(pchMapName,pDef->ppchMapIncluded[i])==0)
				break;
		}

		if(i==-1)
			return false;
	}

	return true;
}

ActivityDef *ActivityDef_Find(const char *pchName)
{
	if(pchName && *pchName)
	{
		int iIndex = eaIndexedFindUsingString(&g_ActivityDefs.ppDefs,pchName);
		if(iIndex != -1)
			return g_ActivityDefs.ppDefs[iIndex];
	}
	return NULL;
}

EventDef *EventDef_Find(const char *pchName)
{
	return (EventDef *)RefSystem_ReferentFromString(g_hEventDictionary, pchName);
}


EventWarpDef *EventDef_GetWarpForAllegiance(EventDef *pDef, AllegianceDef *pAllegianceDef)
{
	if(pDef)
	{
		EventWarpDef *pWarpDef = NULL;
		if(pAllegianceDef)
		{
			pWarpDef = eaIndexedGetUsingString(&pDef->eaWarpSpawns, pAllegianceDef->pcName);
		}
		if(!pWarpDef && pDef->pchSpawnMap && pDef->pchSpawnMap[0])
		{
			static EventWarpDef s_DefaultWarpDef = {0};
			s_DefaultWarpDef.pchSpawnMap = pDef->pchSpawnMap;
			s_DefaultWarpDef.pchSpawnPoint = pDef->pchSpawnPoint;
			s_DefaultWarpDef.iRequiredLevel = pDef->iWarpRequiredLevel;
			COPY_HANDLE(s_DefaultWarpDef.hTransOverride, pDef->hTransOverride);
			pWarpDef = &s_DefaultWarpDef;
		}
		return pWarpDef;
	}
	return NULL;
}

bool Event_CanPlayerUseWarp(EventWarpDef *pWarpDef, Entity* pEnt, bool bCheckEntStatus)
{
	if (pWarpDef && pEnt)
	{
		S32 iLevel = entity_GetSavedExpLevel(pEnt);
		if (pWarpDef->iRequiredLevel > iLevel)
		{
			return false;
		}
		if (pEnt->pPlayer && pEnt->pPlayer->iVirtualShardID > 0)
		{
			return false;
		}
		if (bCheckEntStatus && (!entIsAlive(pEnt) || entIsInCombat(pEnt)))
		{
			return false;
		}
		if (!allegiance_CanPlayerUseWarp(pEnt))
		{
			return false;
		}
		return true;
	}
	return false;
}

EventContactDef *EventDef_GetContactForAllegiance(EventDef *pDef, AllegianceDef *pAllegianceDef)
{
	if(pDef)
	{
		int i;
		for (i = eaSize(&pDef->eaContacts)-1; i >= 0; i--)
		{
			EventContactDef* pEventContact = pDef->eaContacts[i];

			if (!eaSize(&pEventContact->ppchAllegiances))
			{
				return pEventContact;
			}
			else if (pAllegianceDef && eaFind(&pEventContact->ppchAllegiances, pAllegianceDef->pcName) >= 0)
			{
				return pEventContact;
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(mission, util, encounter_action, reward, ItemAssignments) ACMD_NAME(Activity_EventIsActive);
bool ActivityExpr_EventIsActive(const char *pchEventName)
{
#ifdef GAMESERVER
	EventDef *pDef = EventDef_Find(pchEventName);
	if (pDef)
	{
		return gslEvent_IsActive(pDef);
	}
#endif
#ifdef GAMECLIENT
	Entity *pEnt = entActivePlayerPtr();
	const char *pchEventDef = allocFindString(pchEventName);
	if (pchEventDef && pEnt && pEnt->pPlayer && pEnt->pPlayer->pEventInfo)
	{
		S32 i;
		for (i = eaSize(&pEnt->pPlayer->pEventInfo->eaActiveEvents) - 1; i >= 0; i--)
		{
			if (pEnt->pPlayer->pEventInfo->eaActiveEvents[i]->pchEventName == pchEventDef)
			{
				return true;
			}
		}
	}
#endif
	return false;
}

AUTO_RUN_LATE;
int RegisterEventContainer(void)
{
	objRegisterNativeSchema(GLOBALTYPE_EVENTCONTAINER, parse_EventContainer, NULL, NULL, NULL, NULL, NULL);

#if (defined(APPSERVER))
	{
		const char *pcDictName = GlobalTypeToCopyDictionaryName(GLOBALTYPE_EVENTCONTAINER);

		// set up schema and copy dictionary for container references
		
		RefSystem_RegisterSelfDefiningDictionary(pcDictName, false, parse_EventContainer, false, false, NULL);
		resDictRequestMissingResources(pcDictName, RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	}
#endif
	return 1;
}

static void EventConfig_LoadInternal(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading EventConfig... ");

	StructReset(parse_EventConfig, &g_EventConfig);

	ParserLoadFiles(NULL, 
		"defs/config/EventConfig.def", 
		"EventConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_EventConfig,
		&g_EventConfig);

	loadend_printf(" done.");
}

AUTO_STARTUP(EventConfig);
void EventConfig_Load(void)
{
	EventConfig_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/EventConfig.def", EventConfig_LoadInternal);
}

#include "autogen/ActivityCommon_h_ast.c"
