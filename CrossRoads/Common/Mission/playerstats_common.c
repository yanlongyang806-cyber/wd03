/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "playerstats_common.h"

#include "Entity.h"
#include "Error.h"
#include "Estring.h"
#include "file.h"
#include "GameEvent.h"
#include "GlobalTypes.h"
#include "ResourceManager.h"
#include "StringCache.h"

#include "playerstats_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define PLAYERSTATDEF_BASE_DIR "defs/playerstats"
#define PLAYERSTATDEF_EXTENSION "playerstat"


// ----------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------
// Context to track all data-defined modes


// Player Stat Categories
extern DefineContext *g_pDefinePlayerStatCategories = NULL;
PlayerStatCategories g_PlayerStatCategories = {0};
StaticDefineInt PlayerStatCategoryEnum[] =
{
	DEFINE_INT
	{ "None", kPlayerStatCategory_None },
	DEFINE_EMBEDDYNAMIC_INT(g_pDefinePlayerStatCategories)
	DEFINE_END
};

// Player stat tags
DefineContext *s_pDefinePlayerStatTags = NULL;



DictionaryHandle g_PlayerStatDictionary = NULL;

void playerstats_DictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData);


// ----------------------------------------------------------------------------
//  Auto-runs/Start-up code
// ----------------------------------------------------------------------------

static int playerstat_ResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PlayerStatDef *pStatDef, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			playerstatdef_Validate(pStatDef);
			return VALIDATE_HANDLED;

		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename(&pStatDef->pchFilename, PLAYERSTATDEF_BASE_DIR, pStatDef->pchScope, pStatDef->pchName, PLAYERSTATDEF_EXTENSION);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
void RegisterPlayerStatDictionary(void)
{
	g_PlayerStatDictionary = RefSystem_RegisterSelfDefiningDictionary("PlayerStatDef", false, parse_PlayerStatDef, true, true, NULL);

	resDictManageValidation(g_PlayerStatDictionary, playerstat_ResValidateCB);

	if (IsServer()) {
		resDictProvideMissingResources(g_PlayerStatDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_PlayerStatDictionary, ".name", ".scope", NULL, ".notes", NULL);
		}
	} else {
		resDictRequestMissingResources(g_PlayerStatDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}


AUTO_STARTUP(PlayerStats) ASTRT_DEPS(PlayerStatEnums, Powers);
int playerstats_LoadDefs(void)
{
	static int loadedOnce = false;
	
	if (loadedOnce) {
		return 1;
	}

	if (IsServer()) {
#ifdef GAMESERVER
		resDictRegisterEventCallback(g_PlayerStatDictionary, playerstats_DictionaryChangeCB, NULL);
#endif
		resLoadResourcesFromDisk(g_PlayerStatDictionary, "defs/playerstats", ".playerstat", NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

	} else if (IsClient()) {
		if (gConf.bLoadPlayerStatResoucesOnClient) {
			resLoadResourcesFromDisk(g_PlayerStatDictionary, "defs/playerstats", ".playerstat", NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
		}
	}
	loadedOnce = true;

	//playerstats_LoadTags();

	return 1;
}

// ----------------------------------------------------------------------------------
// PlayerStatCategory dynamic enum initialization
// ----------------------------------------------------------------------------------


AUTO_STARTUP(PlayerStatEnums) ASTRT_DEPS(AS_Messages);
void playerstats_LoadDataDefinedEnums(void)
{
	// load the player stat tags
	{
		s_pDefinePlayerStatTags = DefineCreate();
		DefineLoadFromFile(s_pDefinePlayerStatTags, "PlayerStatTag", "PlayerStatTags", NULL,  "defs/config/PlayerStatTags.def", "PlayerStatTags.bin", 0);
	}

	// load the player stat categories
	{
		S32 i,s;
		char *pcTemp = NULL;
		const char *pchMessageFail = NULL;

		estrStackCreateSize(&pcTemp,20);

		loadstart_printf("Loading PlayerStatCategories...");
		ParserLoadFiles(NULL, "defs/config/PlayerStatCategories.def", "PlayerStatCategories.bin", PARSER_OPTIONALFLAG, parse_PlayerStatCategories, &g_PlayerStatCategories);
		g_pDefinePlayerStatCategories = DefineCreate();
		s = eaSize(&g_PlayerStatCategories.eaPlayerStatCategories);

		for (i = 0; i < s; i++) 
		{
			estrPrintf(&pcTemp, "%d", i + kPlayerStatCategory_FIRST_DATA_DEFINED);	// This must be the index, they are indexed directly
			DefineAdd(g_pDefinePlayerStatCategories, g_PlayerStatCategories.eaPlayerStatCategories[i]->pchName, pcTemp);
		}

		loadend_printf(" done (%d PlayerStatCategories).", s);
		estrDestroy(&pcTemp);

		RegisterNamedStaticDefine(PlayerStatCategoryEnum, "PlayerStatCategory");

		if (pchMessageFail = StaticDefineVerifyMessages(PlayerStatCategoryEnum)) {
			Errorf("Not all PlayerStatCategory messages were found: %s", pchMessageFail);
		}
	}
}


// ----------------------------------------------------------------------------
//  Data validation
// ----------------------------------------------------------------------------

bool playerstatdef_Validate(PlayerStatDef *pStatDef)
{
	char buf[128];
	char *estrBuffer = NULL;
 	bool bSuccess = true;
	int i;
	estrStackCreate(&estrBuffer);

	// Basic Name/Scope validation
	if ( !resIsValidName(pStatDef->pchName) ) {
		ErrorFilenamef( pStatDef->pchFilename, "PlayerStatDef name is illegal: '%s'", pStatDef->pchName );
		bSuccess = false;
	}

	if ( !resIsValidScope(pStatDef->pchScope) ) {
		ErrorFilenamef( pStatDef->pchFilename, "PlayerStatDef scope is illegal: '%s'", pStatDef->pchScope );
		bSuccess = false;
	}
	

	// Event validation
	if (pStatDef->eaEditorData) {
		for(i=0; i < eaSize(&pStatDef->eaEditorData); i++) {
			GameEvent *pEvent = pStatDef->eaEditorData[i]->pEvent;

			// Set partition
			pEvent->iPartitionIdx = PARTITION_ANY;

			// Force override event name to be unique
			sprintf(buf, "PlayerEvent_%d", i);
			pEvent->pchEventName = allocAddString(buf);

			if (!gameevent_Validate(pEvent, &estrBuffer, NULL, true)) {
				ErrorFilenamef(pStatDef->pchFilename, "%s", estrBuffer);
				bSuccess = false;
			} else {
				if (!(pEvent->tMatchSource == TriState_Yes || pEvent->tMatchSourceTeam == TriState_Yes || pEvent->tMatchTarget == TriState_Yes || pEvent->tMatchTargetTeam == TriState_Yes)) {
					ErrorFilenamef( pStatDef->pchFilename, "PlayerStatDef '%s' has an unscoped event.  PlayerStatDef Events must have one of the 'Match___' or 'Match___Team' flags set.", pStatDef->pchName);
					bSuccess = false;
				}
			}
		}
	} else {
		for(i=0; i < eaSize(&pStatDef->eaEvents); i++) {
			GameEvent *pEvent = pStatDef->eaEvents[i];

			// Set partition
			pEvent->iPartitionIdx = PARTITION_ANY;

			// Force override event name to be unique
			sprintf(buf, "PlayerEvent_%d", i);
			pEvent->pchEventName = allocAddString(buf);

			if (!gameevent_Validate(pEvent, &estrBuffer, NULL, true)) {
				ErrorFilenamef(pStatDef->pchFilename, "%s", estrBuffer);
				bSuccess = false;
			} else {
				if (!(pEvent->tMatchSource == TriState_Yes || pEvent->tMatchSourceTeam == TriState_Yes || pEvent->tMatchTarget == TriState_Yes || pEvent->tMatchTargetTeam == TriState_Yes)) {
					ErrorFilenamef( pStatDef->pchFilename, "PlayerStatDef '%s' has an unscoped event.  PlayerStatDef Events must have one of the 'Match___' or 'Match___Team' flags set.", pStatDef->pchName);
					bSuccess = false;
				}
			}
		}
	}

	estrDestroy(&estrBuffer);
 	return bSuccess;
}


// ----------------------------------------------------------------------------
//  Accessors
// ----------------------------------------------------------------------------

U32 playerstat_GetValue(PlayerStatsInfo *pStatsInfo, const char *pchStatName)
{
	if (pStatsInfo){
		PlayerStat *pStat = eaIndexedGetUsingString(&pStatsInfo->eaPlayerStats, pchStatName);
		if (pStat)
			return pStat->uValue;
	}
	return 0;
}


#include "AutoGen/playerstats_common_h_ast.c"
