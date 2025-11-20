/***************************************************************************
*     Copyright (c) 2003-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "error.h"
#include "file.h"
#include "message.h"
#include "ResourceManager.h"
#include "allegiance.h"
#include "CharacterClass.h"
#include "mission_common.h"
#include "Player.h"
#include "species_common.h"
#include "Character.h"
#include "GameAccountDataCommon.h"

//-----------------------------------------------------------------------------
// Globals & Statics
//-----------------------------------------------------------------------------

#include "AutoGen/allegiance_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hAllegianceDict = NULL;

static void validateAllegiance(AllegianceDef *pDef)
{
	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"Allegiance '%s' does not have a valid name\n",pDef->pcName);
	}

	if (!GET_REF(pDef->displayNameMsg.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) {
			ErrorFilenamef(pDef->pcFileName,"Allegiance '%s' refers to non-existent message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		} else {
			ErrorFilenamef(pDef->pcFileName,"Allegiance '%s' has no display name\n",pDef->pcName);
		}
	}

	if (IsGameServerBasedType() && pDef->pWarpRestrict) {
		const char* pchMissionName = pDef->pWarpRestrict->pchRequiredMission;
		if (pchMissionName && pchMissionName[0]) {
			MissionDef* pMissionDef = missiondef_FindMissionByName(NULL, pchMissionName);
			if (!pMissionDef) {
				ErrorFilenamef(pDef->pcFileName,"Allegiance '%s' references non-existent MissionDef %s\n",pDef->pcName,pchMissionName);
			}
		}
	}
}

static int allegianceResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, AllegianceDef *pAllegiance, U32 userID)
{
	switch(eType)
	{
		case RESVALIDATE_CHECK_REFERENCES:
			// Only validate on game server, login server and the auction server
			if (IsGameServerSpecificallly_NotRelatedTypes() || IsLoginServer() || IsAuctionServer()) 
			{
				validateAllegiance(pAllegiance);
				return VALIDATE_HANDLED;
			}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(Allegiance) ASTRT_DEPS(CritterFactions, Species);
void critterAllegiance_Load(void)
{
	// Currently loads on both client and server
	resLoadResourcesFromDisk(g_hAllegianceDict, NULL, "defs/config/allegiance.def", "allegiance.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);

	if(!gAllegianceDefaults)
	{
		int i;
		DictionaryEArrayStruct *pArray = resDictGetEArrayStruct(g_hAllegianceDict);
		gAllegianceDefaults = calloc(sizeof(AllegianceDefaults),1);

		for (i = 0; i < eaSize(&pArray->ppReferents); i++)
		{
			AllegianceDef *pAllegiance = eaGet(&pArray->ppReferents, i);
			if (pAllegiance->bDefaultPlayerAllegiance)
			{
				if(IS_HANDLE_ACTIVE(gAllegianceDefaults->hDefaultPlayerAllegiance))
				{
					AllegianceDef* pDefAllegiance = GET_REF(gAllegianceDefaults->hDefaultPlayerAllegiance);
					Errorf("Allegiances %s and %s are both set as the default player Allegiance", pAllegiance->pcName, pDefAllegiance->pcName);
				}
				else
					SET_HANDLE_FROM_STRING(g_hAllegianceDict,pAllegiance->pcName,gAllegianceDefaults->hDefaultPlayerAllegiance);
			}
		}
	} 
}

AUTO_RUN;
int RegisterAllegianceDict(void)
{
	// Set up reference dictionary for parts and such
	g_hAllegianceDict = RefSystem_RegisterSelfDefiningDictionary("Allegiance", false, parse_AllegianceDef, true, true, NULL);

	resDictManageValidation(g_hAllegianceDict, allegianceResValidateCB);

	if (IsServer()) {
		resDictProvideMissingResources(g_hAllegianceDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hAllegianceDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
		}
	} else if (IsClient()) {
		resDictRequestMissingResources(g_hAllegianceDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}

	return 1;
}

AllegianceDef* allegiance_FindByName( const char* pchAllegiance )
{
	return RefSystem_ReferentFromString(g_hAllegianceDict, pchAllegiance);
}

static bool allegiance_CanPlayerUseWarpAllegiance(Entity *pEnt, AllegianceDef *pAllegiance)
{
	if (pAllegiance && pAllegiance->pWarpRestrict)
	{
		S32 iLevel = entity_GetSavedExpLevel(pEnt);
		if (pAllegiance->pWarpRestrict->iRequiredLevel > iLevel)
		{
			return false;
		}
		if (pAllegiance->pWarpRestrict->pchRequiredMission &&
			pAllegiance->pWarpRestrict->pchRequiredMission[0])
		{
			const char* pchMission = pAllegiance->pWarpRestrict->pchRequiredMission;
			CompletedMission* pCompletedMission = eaIndexedGetUsingString(&pEnt->pPlayer->missionInfo->completedMissions, pchMission);
			if (!pCompletedMission)
			{
				return false;
			}
		}
	}

	return true;
}

bool allegiance_CanPlayerUseWarp(Entity* pEnt)
{
	AllegianceDef* pAllegiance = GET_REF(pEnt->hAllegiance);
	AllegianceDef* pSubAllegiance = GET_REF(pEnt->hSubAllegiance);

	if (!allegiance_CanPlayerUseWarpAllegiance(pEnt, pAllegiance))
		return false;
	else if (!allegiance_CanPlayerUseWarpAllegiance(pEnt, pSubAllegiance))
		return false;

	return true;
}

bool allegiance_CanUseNamePrefix(AllegianceNamePrefix* pPrefix, Entity* pEnt, GameAccountData *pGameAccount)
{
	CharacterClass *pClass = pEnt && pEnt->pChar ? character_GetClassCurrent(pEnt->pChar) : NULL;
	SpeciesDef *pSpecies = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hSpecies) : NULL;
	S32 j;

	if (eaSize(&pPrefix->eaClassesAllowed) > 0)
	{
		if (!pClass)
			return false;

		for (j = eaSize(&pPrefix->eaClassesAllowed) - 1; j >= 0; j--)
			if (GET_REF(pPrefix->eaClassesAllowed[j]->hClass) == pClass)
				break;
		if (j < 0)
			return false;
	}

	if (eaSize(&pPrefix->eaSpeciesAllowed) > 0)
	{
		if (!pSpecies)
			return false;

		for (j = eaSize(&pPrefix->eaSpeciesAllowed) - 1; j >= 0; j--)
			if (GET_REF(pPrefix->eaSpeciesAllowed[j]->hSpecies) == pSpecies)
				break;
		if (j < 0)
			return false;;
	}

	if (pPrefix->pchGameAccountDataKey && *pPrefix->pchGameAccountDataKey)
	{
		const char *pchValue;

		if (!pGameAccount)
			return false;

		pchValue = gad_GetAttribString(pGameAccount, pPrefix->pchGameAccountDataKey);
		if (!pchValue || atoi(pchValue) <= 0)
			return false;
	}

	return true;
}

static void allegiance_CheckNamePrefix(AllegianceNamePrefix* pNamePrefix, Entity* pNameEnt, GameAccountData *pGameAccount, const char *pchName, const char **ppchPrefix, const char **ppchExpectedPrefix)
{
	const char *pchCheckPrefix = NULL_TO_EMPTY(pNamePrefix->pchPrefix);

	// Validate prefix allowed
	if (!allegiance_CanUseNamePrefix(pNamePrefix, pNameEnt, pGameAccount))
		return;

	if (ppchExpectedPrefix && !*ppchExpectedPrefix)
		*ppchExpectedPrefix = pchCheckPrefix;

	if (!strStartsWith(pchName, pchCheckPrefix))
		return;

	// Prefer the longer prefix
	if (ppchPrefix && *ppchPrefix && strlen(*ppchPrefix) >= strlen(pchCheckPrefix))
		return;

	if (ppchPrefix)
		*ppchPrefix = pchCheckPrefix;
	if (ppchExpectedPrefix)
		*ppchExpectedPrefix = pchCheckPrefix;
}

const char *allegiance_GetNamePrefix(AllegianceDef* pAllegiance, AllegianceDef* pSubAllegiance, Entity* pNameEnt, GameAccountData *pGameAccount, const char *pchName, const char **ppchExpectedPrefix)
{
	const char *pchExpectedPrefix = NULL;
	const char *pchPrefix = NULL;

	if (!pAllegiance)
	{
		if (ppchExpectedPrefix)
			*ppchExpectedPrefix = NULL;
		return NULL;
	}

	if (eaSize(&pAllegiance->eaNamePrefixes) > 0 || (pSubAllegiance && eaSize(&pSubAllegiance->eaNamePrefixes) > 0))
	{
		S32 i;
		for (i = 0; i < eaSize(&pAllegiance->eaNamePrefixes); i++)
		{
			allegiance_CheckNamePrefix(pAllegiance->eaNamePrefixes[i], pNameEnt, pGameAccount, pchName, &pchPrefix, &pchExpectedPrefix);
		}

		if (pSubAllegiance)
		{
			for (i = 0; i < eaSize(&pSubAllegiance->eaNamePrefixes); i++)
			{
				allegiance_CheckNamePrefix(pSubAllegiance->eaNamePrefixes[i], pNameEnt, pGameAccount, pchName, &pchPrefix, &pchExpectedPrefix);
			}
		}
	}
	else
	{
		if (strstri(pAllegiance->pcName,"Allegiance_Starfleet"))
		{
			pchExpectedPrefix = "U.S.S. ";
			if (strStartsWith(pchName, "U.S.S. "))
				pchPrefix = "U.S.S. ";
		}
		else if (strstri(pAllegiance->pcName,"Allegiance_Klingon"))
		{
			pchExpectedPrefix = "I.K.S. ";
			if (strStartsWith(pchName, "I.K.S. "))
				pchPrefix = "I.K.S. ";
		}
		else if (strstri(pAllegiance->pcName,"Allegiance_Romulan"))
		{
			pchExpectedPrefix = "I.R.W. ";
			if (strStartsWith(pchName, "I.R.W. "))
				pchPrefix = "I.R.W. ";
		}
	}

	if (ppchExpectedPrefix)
		*ppchExpectedPrefix = pchExpectedPrefix;
	return pchPrefix;
}

const char *allegiance_GetSubNamePrefix(AllegianceDef* pAllegiance, AllegianceDef* pSubAllegiance, Entity* pNameEnt, GameAccountData *pGameAccount, const char *pchName, const char **ppchExpectedPrefix)
{
	CharacterClass *pClass = pNameEnt && pNameEnt->pChar ? character_GetClassCurrent(pNameEnt->pChar) : NULL;
	SpeciesDef *pSpecies = pNameEnt && pNameEnt->pChar ? GET_REF(pNameEnt->pChar->hSpecies) : NULL;
	const char *pchExpectedPrefix = NULL;
	const char *pchPrefix = NULL;

	if (!pAllegiance)
	{
		if (ppchExpectedPrefix)
			*ppchExpectedPrefix = NULL;
		return NULL;
	}

	if (eaSize(&pAllegiance->eaSubNamePrefixes) > 0 || (pSubAllegiance && eaSize(&pSubAllegiance->eaSubNamePrefixes) > 0))
	{
		S32 i;
		for (i = 0; i < eaSize(&pAllegiance->eaSubNamePrefixes); i++)
		{
			allegiance_CheckNamePrefix(pAllegiance->eaSubNamePrefixes[i], pNameEnt, pGameAccount, pchName, &pchPrefix, &pchExpectedPrefix);
		}

		if (pSubAllegiance)
		{
			for (i = 0; i < eaSize(&pSubAllegiance->eaSubNamePrefixes); i++)
			{
				allegiance_CheckNamePrefix(pSubAllegiance->eaSubNamePrefixes[i], pNameEnt, pGameAccount, pchName, &pchPrefix, &pchExpectedPrefix);
			}
		}
	}
	else
	{
		pchExpectedPrefix = "NCC-";
		if (strStartsWith(pchName, "NCC-"))
			pchPrefix = "NCC-";

		if (strStartsWith(pchName, "NX-"))
		{
			const char *pchValue = pGameAccount ? gad_GetAttribString(pGameAccount, "ST.Ship_NX_Registry") : NULL;
			pchPrefix = "NX-";
			if (pchValue)
			{
				ANALYSIS_ASSUME(pchValue != NULL);
				if (atoi(pchValue) > 0)
					pchExpectedPrefix = "NX-";
			}
		}
	}

	if (ppchExpectedPrefix)
		*ppchExpectedPrefix = pchExpectedPrefix;
	return pchPrefix;
}

#include "AutoGen/allegiance_h_ast.c"