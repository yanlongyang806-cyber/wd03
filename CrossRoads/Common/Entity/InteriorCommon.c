/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "ResourceManager.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "file.h"
#include "GameAccountData\GameAccountData.h"
#include "GlobalEnums.h"
#include "Player.h"
#include "CharacterClass.h"
#include "Character.h"
#include "entCritter.h"
#include "CombatEnums.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "referencesystem.h"
#include "WorldGrid.h"
#include "InteriorCommon.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "MicroTransactions.h"
#include "inventoryCommon.h"

#include "AutoGen/InteriorCommon_h_ast.h"
#include "AutoGen/CombatEnums_h_ast.h"

//if true, everyone has access to all hideouts
static bool gbDebugUnlockAllInteriors = false;
AUTO_CMD_INT(gbDebugUnlockAllInteriors, DebugUnlockAllInteriors) ACMD_COMMANDLINE;

DictionaryHandle *g_hInteriorDefDict;
DictionaryHandle *g_hInteriorOptionDefDict;
DictionaryHandle *g_hInteriorOptionChoiceDict;
DictionaryHandle *g_hInteriorSettingDict;
static InteriorConfig s_InteriorConfig;

//
// Interior expression context
//
static const char *s_pcVarPlayer = NULL;
static ExprContext *g_pInteriorContext = NULL;
static int s_hVarPlayer = 0;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("InteriorConfig", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("InteriorDef", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("InteriorDefRef", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("EntityInteriorData", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("InteriorInvite", BUDGET_GameSystems););

bool
InteriorCommon_IsCurrentMapInterior(void)
{
	ZoneMapType mapType = zmapInfoGetMapType(NULL);
	bool teamNotRequired = zmapInfoGetTeamNotRequired(NULL);

	return ( ( mapType == ZMTYPE_OWNED ) && teamNotRequired );
}

Entity *
InteriorCommon_GetActiveInteriorOwner(Entity *playerEnt, CharClassCategorySet *pSet)
{
    if ( playerEnt != NULL && playerEnt->pSaved != NULL )
    {
		if( playerEnt->pSaved->pPuppetMaster != NULL )
		{
			int i;
			int n = eaSize(&playerEnt->pSaved->pPuppetMaster->ppPuppets);
			
			if (pSet)
			{
				for ( i = 0; i < n; i++ )
				{
					PuppetEntity *puppet = playerEnt->pSaved->pPuppetMaster->ppPuppets[i];

					if ( ( puppet->eType == (U32)InteriorConfig_InteriorPetType() ) && ( puppet->eState == PUPPETSTATE_ACTIVE ) )
					{
						Entity *petEnt = GET_REF(puppet->hEntityRef);
						CharacterClass *pClass = SAFE_GET_REF2( petEnt, pChar, hClass );
						if ( pClass && eaiFind(&pSet->eaCategories, pClass->eCategory ) >= 0 )
						{
							return petEnt;
						}
					}
				}
			}
			else
			{
				const char* pchZoneMapName = zmapGetName(NULL);
				for ( i = 0; i < n; i++ )
				{
					// This currently relies on the assumption that you have one ship 
					// and one shuttle, and that ships and shuttles cannot have the same
					// interior map. 
					PuppetEntity *puppet = playerEnt->pSaved->pPuppetMaster->ppPuppets[i];
					if ( ( puppet->eType == (U32)InteriorConfig_InteriorPetType() ) && ( puppet->eState == PUPPETSTATE_ACTIVE ) )
					{
						Entity *petEnt = GET_REF(puppet->hEntityRef);
						InteriorDef *pIntDef = InteriorCommon_GetCurrentInteriorDef(petEnt);
						if (pIntDef && stricmp(pIntDef->mapName, pchZoneMapName) == 0)
						{
							return petEnt;
						}
					}
				}
			}
		}
		else if( playerEnt->pSaved->interiorData != NULL )
		{
			return playerEnt;
		}
    }

    return NULL;
}

Entity *
InteriorCommon_GetPetByID(Entity *playerEnt, ContainerID petID)
{
	Entity *petEnt = NULL;
	int i;
	int n;

	if ( playerEnt != NULL )
	{
		n = eaSize(&playerEnt->pSaved->ppOwnedContainers);
		for ( i = 0; i < n; i++ )
		{
			if(playerEnt->pSaved->ppOwnedContainers[i]->conID == petID)
			{
				petEnt = GET_REF(playerEnt->pSaved->ppOwnedContainers[i]->hPetRef);
				break;	
			}
		}
	}

	return petEnt;
}

InteriorDef *
InteriorCommon_GetCurrentInteriorDef(Entity *pEnt)
{
	InteriorDef *interiorDef = NULL;

	if( ( pEnt == NULL ) || ( pEnt->pSaved == NULL ) )
	{
		return interiorDef;
	}

	if ( pEnt->pSaved->interiorData != NULL )
	{
		interiorDef = GET_REF(pEnt->pSaved->interiorData->hInteriorDef);
	}

	if ( interiorDef == NULL && pEnt->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		if ( pEnt->pCritter )
		{
			PetDef *petDef = GET_REF(pEnt->pCritter->petDef);

			// the pet doesn't have an interior def, so use the default, which is the first
			//  in the list from the PetDef

			if ( petDef && eaSize(&petDef->ppInteriorDefs) )
			{
				interiorDef = GET_REF(petDef->ppInteriorDefs[0]->hInterior);
			}
		}
	}

	return interiorDef;
}

//
// Get the named InteriorDef.
// Will only return defs that are valid for the given pet entity.
//
InteriorDef *
InteriorCommon_GetPetInteriorDefByName(Entity *petEnt, const char *interiorDefName)
{
	const char *pooledInteriorDefName = allocAddString(interiorDefName);
	InteriorDef *interiorDef = NULL;
	PetDef *petDef;
	int i;
	int n;

	devassert(petEnt->myEntityType == GLOBALTYPE_ENTITYSAVEDPET);
	if ( petEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET )
	{
		return NULL;
	}

	petDef = GET_REF(petEnt->pCritter->petDef);
	if ( petDef == NULL )
	{
		return NULL;
	}

	// Find the InteriorDef with the matching name
	n = eaSize(&petDef->ppInteriorDefs);
	for ( i = 0; i < n; i++ )
	{
		interiorDef = GET_REF(petDef->ppInteriorDefs[i]->hInterior);
		if ( ( interiorDef != NULL ) && ( interiorDef->name == pooledInteriorDefName ) )
		{
			return interiorDef;
		}
	}

	return NULL;
}

static void 
InteriorDef_Validate(InteriorDef *pDef)
{
	if(IsServer())
	{
		if (!GET_REF(pDef->displayNameMsg.hMessage)) 
		{
			if (!REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage))
				ErrorFilenamef(pDef->filename, "Interior def '%s' does not specify a display name message and one is required", pDef->name);
			else
				ErrorFilenamef(pDef->filename, "Interior def '%s' refers to non-existent message '%s'", pDef->name, REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		}
	}
}

static int interiorDefResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, InteriorDef *pDef, U32 userID)
{
	switch (eType)
	{	
		xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
			InteriorDef_Validate(pDef);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static bool 
InteriorOptionChoice_Validate(InteriorOptionChoice *pChoice)
{
	bool retcode = true;

	if(IsServer())
	{
		if (!GET_REF(pChoice->hDisplayName)) 
		{
			if (!REF_STRING_FROM_HANDLE(pChoice->hDisplayName))
				ErrorFilenamef(pChoice->filename, "InteriorOptionChoice '%s' does not specify a display name message and one is required", pChoice->name);
			else
				ErrorFilenamef(pChoice->filename, "InteriorOptionChoice '%s' refers to non-existent message '%s'", pChoice->name, REF_STRING_FROM_HANDLE(pChoice->hDisplayName));
			retcode = false;
		}
	}

	// generate the requires expression
	if (pChoice->availableExpression)
	{
		if (!exprGenerate(pChoice->availableExpression, g_pInteriorContext))
		{
			retcode = false;
		}
	}

	return retcode;
}

static int interiorOptionChoiceResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, InteriorOptionChoice *pChoice, U32 userID)
{
	switch (eType)
	{	
		case RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
			InteriorOptionChoice_Validate(pChoice);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static void 
InteriorOptionDef_Validate(InteriorOptionDef *pOption)
{
	if(IsServer())
	{
		if (!GET_REF(pOption->hDisplayName)) 
		{
			if (!REF_STRING_FROM_HANDLE(pOption->hDisplayName))
				ErrorFilenamef(pOption->filename, "InteriorOptionDef '%s' does not specify a display name message and one is required", pOption->name);
			else
				ErrorFilenamef(pOption->filename, "InteriorOptionDef '%s' refers to non-existent message '%s'", pOption->name, REF_STRING_FROM_HANDLE(pOption->hDisplayName));
		}
	}
}

static int interiorOptionDefResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, InteriorOptionDef *pOption, U32 userID)
{
	switch (eType)
	{	
		xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
			InteriorOptionDef_Validate(pOption);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


static void 
InteriorSetting_Validate(InteriorSetting *pDef)
{
	if(IsServer())
	{
		if (!GET_REF(pDef->displayNameMsg.hMessage))
		{
			if (!REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage))
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' does not specify a display name message and one is required", pDef->pchName);
			else
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' refers to non-existent message '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		}

		if (!GET_REF(pDef->descriptionMsg.hMessage))
		{
			if (!REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage))
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' does not specify a description message and one is required", pDef->pchName);
			else
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' refers to non-existent message '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage));
		}		
	}

	//Make sure this is for a valid interior
	if(!GET_REF(pDef->hInterior))
	{
		if (!REF_STRING_FROM_HANDLE(pDef->hInterior))
			ErrorFilenamef(pDef->filename, "Interior Setting '%s' does not specify an interior def and it is required!", pDef->pchName);
		else
			ErrorFilenamef(pDef->filename, "Interior Setting '%s' refers to non-existent interior def '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->hInterior));
	}

	//Check all the setting's options and choices
	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaOptionSettings, InteriorOptionRef, pOptionRef)
	{
		if(!GET_REF(pOptionRef->hOption))
		{
			if (!REF_STRING_FROM_HANDLE(pOptionRef->hOption))
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' option ref does not specify an option and it is required!", pDef->pchName);
			else
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' refers to non-existent option '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pOptionRef->hOption));
		}

		if(!GET_REF(pOptionRef->hChoice))
		{
			if (!REF_STRING_FROM_HANDLE(pOptionRef->hChoice))
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' option ref '%s' does not specify a choice and it is required!", pDef->pchName,  REF_STRING_FROM_HANDLE(pOptionRef->hOption));
			else
				ErrorFilenamef(pDef->filename, "Interior Setting '%s' refers to non-existent choice '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pOptionRef->hChoice));
		}
	} FOR_EACH_END;

	/*
	if(IsServer())
	{
		if(pDef->pchPermission && !eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pDef->pchPermission))
		{
			ErrorFilenamef(pDef->filename, "Interior Setting '%s' specifies a permission '%s' that does not exist!", pDef->pchName, pDef->pchPermission);
		}
	}
	*/
}

static int interiorSettingResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, InteriorSetting *pDef, U32 userID)
{
	switch (eType)
	{	
		xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
			InteriorSetting_Validate(pDef);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterInteriorDicts(void)
{
	g_hInteriorOptionChoiceDict = RefSystem_RegisterSelfDefiningDictionary("InteriorOptionChoice", false, parse_InteriorOptionChoice, true, true, NULL);
	resDictManageValidation(g_hInteriorOptionChoiceDict, interiorOptionChoiceResValidateCB);

	g_hInteriorOptionDefDict = RefSystem_RegisterSelfDefiningDictionary("InteriorOptionDef", false, parse_InteriorOptionDef, true, true, NULL);
	resDictManageValidation(g_hInteriorOptionDefDict, interiorOptionDefResValidateCB);

	g_hInteriorDefDict = RefSystem_RegisterSelfDefiningDictionary("InteriorDef", false, parse_InteriorDef, true, true, NULL);
	resDictManageValidation(g_hInteriorDefDict, interiorDefResValidateCB);

	g_hInteriorSettingDict = RefSystem_RegisterSelfDefiningDictionary("InteriorSetting", false, parse_InteriorSetting, true, true, NULL);
	resDictManageValidation(g_hInteriorSettingDict, interiorSettingResValidateCB);

	if(IsServer())
	{
		resDictProvideMissingResources(g_hInteriorOptionChoiceDict);
		resDictProvideMissingResources(g_hInteriorOptionDefDict);
		resDictProvideMissingResources(g_hInteriorDefDict);
		resDictProvideMissingResources(g_hInteriorSettingDict);
		
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hInteriorOptionChoiceDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hInteriorOptionDefDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hInteriorDefDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hInteriorSettingDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
}

AUTO_STARTUP(InteriorDefs) ASTRT_DEPS(InteriorConfig);
void InteriorCommon_LoadInteriors(void)
{
	resLoadResourcesFromDisk(g_hInteriorOptionChoiceDict, NULL, "defs/config/InteriorOptionChoices.def", "InteriorOptionChoices.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
	resLoadResourcesFromDisk(g_hInteriorOptionDefDict, NULL, "defs/config/InteriorOptions.def", "InteriorOptions.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
	resLoadResourcesFromDisk(g_hInteriorDefDict, NULL, "defs/config/Interiors.def", "Interiors.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
	
	//This one should come last because it references choices, options and defs
	resLoadResourcesFromDisk(g_hInteriorSettingDict, NULL, "defs/config/InteriorSettings.def", "InteriorSettings.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
}

AUTO_STARTUP(InteriorConfig) ASTRT_DEPS(CharacterClasses);
void
InteriorCommon_LoadInteriorConfig(void)
{
	loadstart_printf("Loading InteriorConfig...");
	StructInit(parse_InteriorConfig, &s_InteriorConfig);
	ParserLoadFiles(NULL, "defs/config/InteriorConfig.def", "InteriorConfig.bin", PARSER_OPTIONALFLAG, parse_InteriorConfig, &s_InteriorConfig);
	loadend_printf("done.");
}

ContactFlags
InteriorConfig_RequiredContactType(void)
{
	return s_InteriorConfig.interiorChangeContactType;
}

CharClassTypes
InteriorConfig_InteriorPetType(void)
{
	return s_InteriorConfig.interiorPetType;
}

bool
InteriorConfig_PersistAlternates(void)
{
	return s_InteriorConfig.bPersistAlternates;
}

AUTO_TRANS_HELPER;
S32 inv_trh_InteriorChangeCost(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, ATH_ARG NOCONST(Entity)* pPetEnt, bool bDecrementFreeChanges, const ItemChangeReason *pReason, ATH_ARG NOCONST(GameAccountData) *pData)
{
	const char* pchFreeNumeric = s_InteriorConfig.interiorChangeFreeNumeric;
	if ( pchFreeNumeric && pchFreeNumeric[0] )
	{
		S32 iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS, pPetEnt, pchFreeNumeric);
		if (iNumFreeChanges > 0)
		{
			if ( bDecrementFreeChanges )
			{
				inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pPetEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
			}
			return 0;
		}
		iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS, pPlayerEnt, pchFreeNumeric);
		if (iNumFreeChanges > 0)
		{
			if ( bDecrementFreeChanges )
			{
				inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pPlayerEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
			}
			return 0;
		}
		if (NONNULL(pData))
		{
			char text[64];
			*text = '\0';
			strcat(text, GetShortProductName());
			strcat(text, ".");
			strcat(text, pchFreeNumeric);
			iNumFreeChanges = gad_trh_GetAttribInt(pData, text);
			if (!gConf.bDontAllowGADModification && iNumFreeChanges > 0)
			{
#if GAMESERVER
				if ( bDecrementFreeChanges )
				{
					char temp[32];
					sprintf(temp, "%d", iNumFreeChanges - 1);
					slGAD_trh_SetAttrib(ATR_PASS_ARGS, pData, text, temp);
				}
#endif
				return 0;
			}
		}
	}
	return s_InteriorConfig.interiorChangeCost;
}

const char *
InteriorConfig_InteriorChangeCostNumeric(void)
{
	return s_InteriorConfig.interiorChangeCostNumeric;
}

S32
InteriorCommon_InteriorOptionChangeCost(const char *optionName)
{
	InteriorOptionDef *optionDef = InteriorCommon_FindOptionDefByName(optionName);

	if ( optionDef != NULL)
	{
		return optionDef->optionChangeCost;
	}

	return 0;
}

const char *
InteriorCommon_InteriorOptionChangeCostNumeric(const char *optionName)
{
	InteriorOptionDef *optionDef = InteriorCommon_FindOptionDefByName(optionName);

	if ( optionDef != NULL)
	{
		return optionDef->optionChangeCostNumeric;
	}

	return NULL;
}

bool 
InteriorCommon_CanAffordInteriorChange(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID Entity* pPetEnt)
{
	const char* costNumeric = InteriorConfig_InteriorChangeCostNumeric();

	if ( ( costNumeric != NULL ) && ( costNumeric[0] != '\0' ) )
	{
		S32 cost = InteriorConfig_InteriorChangeCost(pPlayerEnt,pPetEnt,entity_GetGameAccount(pPlayerEnt));

		if ( cost > 0 )
		{	
			S32 playerCurrency = inv_GetNumericItemValue(pPlayerEnt,costNumeric);

			if ( cost > playerCurrency )
			{
				return false;
			}
		}
	}

	return true;
}

InteriorInvite *
InteriorCommon_GetCurrentInteriorInvite(Entity* pEnt)
{
	if ( eaSize(&pEnt->pPlayer->interiorInvites) > 0 )
	{
		return pEnt->pPlayer->interiorInvites[0];
	}

	return NULL;
}

bool
InteriorCommon_IsInteriorInvitee(Entity* pEnt)
{
	return ( eaSize(&pEnt->pPlayer->interiorInvites) > 0 );
}

bool
InteriorCommon_CanMoveToInterior(Entity* pEnt)
{
	if ( zmapInfoGetMapType(NULL) != ZMTYPE_STATIC )
	{
		// we only support going to interiors from static maps at the moment.
		return false;
	}

	if ( entIsInCombat(pEnt) )
	{
		// do not allow moving to interiors while in combat
		return false;
	}

	return true;
}

bool InteriorCommon_IsInteriorUnlocked(Entity* pEnt, InteriorDef* pDef)
{
	if (gbDebugUnlockAllInteriors || (pEnt && pEnt->pPlayer && pEnt->pPlayer->bIsDev))
	{
		return true;
	}

	if (pDef)
	{
		if (!pDef->bUnlockable)
		{
			//Nothing to unlock, return true
			return true;
		}
		else if (pEnt && pEnt->pPlayer)
		{
			if (pDef->pchUnlockKey)
			{
				GameAccountData* pAccountData = NULL;

				if (pEnt->pPlayer->pPlayerAccountData)
				{
					pAccountData = GET_REF(pEnt->pPlayer->pPlayerAccountData->hData);
				}
				if (pAccountData)
				{
					//See if the player has a game account key that matches the unlock key
					AttribValuePair* pAttrib = eaIndexedGetUsingString(&pAccountData->eaKeys, pDef->pchUnlockKey);
					if (pAttrib && atoi(pAttrib->pchValue) > 0)
					{
						return true;
					}
				}	
			}
			else
			{
				S32 i;
				//Search the player-specific interior unlocks array
				for (i = eaSize(&pEnt->pPlayer->eaInteriorUnlocks)-1; i >= 0; i--)
				{
					if (pDef == GET_REF(pEnt->pPlayer->eaInteriorUnlocks[i]->hDef))
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

InteriorOptionDef *
InteriorCommon_FindOptionDefByName(const char *optionName)
{
	return RefSystem_ReferentFromString(g_hInteriorOptionDefDict, optionName);
}

InteriorOptionChoice *
InteriorCommon_FindOptionChoiceByName(const char *choiceName)
{
	return RefSystem_ReferentFromString(g_hInteriorOptionChoiceDict, choiceName);
}

InteriorSetting *
InteriorCommon_FindSettingByName(const char *settingName)
{
	return RefSystem_ReferentFromString(g_hInteriorSettingDict, settingName);
}

bool
InteriorCommon_IsOptionAvailableForInterior(InteriorDef *interiorDef, InteriorOptionDef *optionDef)
{
	int i;

	for ( i = eaSize(&interiorDef->optionRefs)-1; i >= 0; i-- )
	{
		if ( GET_REF(interiorDef->optionRefs[i]->hOptionDef) == optionDef )
		{
			return true;
		}
	}

	return false;
}

bool
InteriorCommon_IsChoiceAvailableForOption(InteriorOptionDef *optionDef, const char *choiceName)
{
	return ( eaIndexedFindUsingString(&optionDef->choiceRefs, choiceName) >= 0 );
}

bool
InteriorCommon_IsChoiceActive(Entity *playerEnt, InteriorOptionChoice *pChoice)
{
	MultiVal mvResult = InteriorCommon_Eval(pChoice->availableExpression, playerEnt, MMT_INT32);
	return ( MultiValGetInt(&mvResult, NULL) != 0 );
}

//
// Return an earray of the Interior option choices that are available for the given option and player
// 
void 
InteriorCommon_GetInteriorOptionChoices(Entity *playerEnt, InteriorDef *interiorDef, const char *optionName, InteriorOptionChoice ***out_Choices)
{
	InteriorOptionDef *optionDef;
	int i;

	if ( ( playerEnt == NULL ) || 
		( optionName == NULL ) || ( optionName[0] == '\0' ) || ( out_Choices == NULL ) )
	{
		return;
	}

	optionDef = InteriorCommon_FindOptionDefByName(optionName);

	if ( ( interiorDef == NULL ) || ( optionDef == NULL ) )
	{
		return;
	}

	// not a valid option for this interior
	if ( !InteriorCommon_IsOptionAvailableForInterior(interiorDef, optionDef) )
	{
		return;
	}

	for ( i = eaSize(&optionDef->choiceRefs) - 1; i >= 0; i-- )
	{
		InteriorOptionChoice *pChoice = GET_REF(optionDef->choiceRefs[i]->hChoice);
		if ( pChoice != NULL )
		{
			if ( !pChoice->availableExpression ||
				InteriorCommon_IsChoiceActive(playerEnt, pChoice) )
			{
				eaPush(out_Choices, pChoice);
			}
		}
	}
}

static InteriorOptionChoice *
GetOptionChoice_Internal(Entity *playerEnt, Entity *pTargetEnt, InteriorDef *interiorDef, const char *optionName)
{
	InteriorOptionDef *optionDef;
	InteriorOption *pOption;
	InteriorOptionChoice *pChoice;

	if ( ( playerEnt == NULL ) || ( pTargetEnt == NULL ) || ( pTargetEnt->pSaved == NULL ) || 
		( optionName == NULL ) || ( optionName[0] == '\0' ) )
	{
		return NULL;
	}

	optionDef = InteriorCommon_FindOptionDefByName(optionName);

	if ( ( interiorDef == NULL ) || ( optionDef == NULL ) )
	{
		return NULL;
	}

	// not a valid option for this interior
	if ( !InteriorCommon_IsOptionAvailableForInterior(interiorDef, optionDef) )
	{
		return NULL;
	}

	if ( pTargetEnt->pSaved->interiorData == NULL )
	{
		pOption = NULL;
	}
	else
	{
		pOption = eaIndexedGetUsingString(&pTargetEnt->pSaved->interiorData->options, optionName);
	}

	if ( pOption == NULL )
	{
		pChoice = GET_REF(optionDef->hDefaultChoice);
	}
	else
	{
		pChoice = GET_REF(pOption->hChoice);
	}

	return pChoice;
}


static InteriorOptionChoice *
GetOptionChoice(Entity *playerEnt, ContainerID petID, const char *optionName)
{
	Entity *pTargetEnt = petID ? InteriorCommon_GetPetByID(playerEnt, petID) : playerEnt;
	InteriorDef *interiorDef;

	if ( pTargetEnt == NULL )
	{
		return NULL;
	}

	interiorDef = InteriorCommon_GetCurrentInteriorDef(pTargetEnt);
	if ( interiorDef == NULL )
	{
		return NULL;
	}

	return GetOptionChoice_Internal(playerEnt, pTargetEnt, interiorDef, optionName);
}

static InteriorOptionChoice *
GetOptionChoiceForInterior(Entity *playerEnt, ContainerID petID, const char *interiorName, const char *optionName)
{
	Entity *pTargetEnt = petID ? InteriorCommon_GetPetByID(playerEnt, petID) : playerEnt;
	InteriorDef *interiorDef;

	if ( pTargetEnt == NULL )
	{
		return NULL;
	}

	interiorDef = InteriorCommon_GetCurrentInteriorDef(pTargetEnt);
	if ( interiorDef == NULL )
	{
		return NULL;
	}

	return GetOptionChoice_Internal(playerEnt, pTargetEnt, interiorDef, optionName);
}

const char *
InteriorCommon_GetOptionChoiceName(Entity *playerEnt, ContainerID petID, const char *optionName)
{
	InteriorOptionChoice *pChoice = GetOptionChoice(playerEnt, petID, optionName);
	
	if ( pChoice != NULL )
	{
		return pChoice->name;
	}

	return NULL;
}

const char *
InteriorCommon_GetOptionChoiceDisplayMessage(Entity *playerEnt, ContainerID petID, const char *optionName)
{
	InteriorOptionChoice *pChoice = GetOptionChoice(playerEnt, petID, optionName);

	if ( pChoice != NULL )
	{
		Message *message = GET_REF(pChoice->hDisplayName);
		if ( message != NULL )
		{
			return message->pcMessageKey;
		}
	}
	return NULL;
}

const char *
InteriorCommon_GetOptionChoiceNameForInterior(Entity *playerEnt, ContainerID petID, const char *interiorName, const char *optionName)
{
	InteriorOptionChoice *pChoice = GetOptionChoiceForInterior(playerEnt, petID, interiorName, optionName);

	if ( pChoice != NULL )
	{
		return pChoice->name;
	}

	return NULL;
}

const char *
InteriorCommon_GetOptionChoiceDisplayMessageForInterior(Entity *playerEnt, ContainerID petID, const char *interiorName, const char *optionName)
{
	InteriorOptionChoice *pChoice = GetOptionChoiceForInterior(playerEnt, petID, interiorName, optionName);

	if ( pChoice != NULL )
	{
		Message *message = GET_REF(pChoice->hDisplayName);
		if ( message != NULL )
		{
			return message->pcMessageKey;
		}
	}
	return NULL;
}

S32 
InteriorCommon_GetOptionChoiceValue(Entity *playerEnt, ContainerID petID, const char *optionName)
{
	InteriorOptionChoice *pChoice = GetOptionChoice(playerEnt, petID, optionName);

	if ( pChoice != NULL )
	{
		return pChoice->value;
	}

	return 0;
}

bool
InteriorCommon_IsSettingAvailableForInterior(InteriorDef *interiorDef, InteriorSetting *setting)
{
	if(!interiorDef || !GET_REF(setting->hInterior) || interiorDef != GET_REF(setting->hInterior))
		return false;

	return true;
}

bool
InteriorCommon_IsSettingCurrent(Entity *pEnt, InteriorSetting *setting)
{
	EntityInteriorData *pInteriorData = SAFE_MEMBER2(pEnt, pSaved, interiorData);
	InteriorSetting *pCurrentSetting = SAFE_GET_REF(pInteriorData, hSetting);
	if(!pCurrentSetting)
		return false;

	return pCurrentSetting == setting;
}

bool
InteriorCommon_CanUseSetting(Entity *pEnt, InteriorSetting *setting)
{
	if (gbDebugUnlockAllInteriors || (pEnt && pEnt->pPlayer && pEnt->pPlayer->bIsDev))
	{
		return true;
	}


	if(setting->pchPermission && !GamePermission_EntHasToken(pEnt, setting->pchPermission))
		return false;

	return true;
}

const char *
InteriorCommon_GetFreePurchaseKey(void)
{
	return s_InteriorConfig.pchFreePurchaseKey;
}

InteriorSettingMTRef *
InteriorCommon_FindSettingMTRefByName(const char *pchSetting)
{
	return eaIndexedGetUsingString(&s_InteriorConfig.eaFreeSettings, pchSetting);
}

InteriorSettingMTRef ***
InteriorCommon_GetFreeSettings(void)
{
	return &s_InteriorConfig.eaFreeSettings;
}

AUTO_TRANS_HELPER;
int
InteriorCommon_trh_EntFreePurchasesRemaining(ATH_ARG NOCONST(Entity) *pEnt, GameAccountDataExtract *pExtract)
{
	if(pExtract)
	{
		bool bHasPermission = true;
		AttribValuePair *pFreePurchaseAttrib = NULL;
		S32 iMaxFreePurchases = GamePermissions_trh_GetCachedMaxNumeric(pEnt, GAME_PERMISSION_FREEINTERIORPURCHASES, false);
		S32 iPurchased = 0;

		if(iMaxFreePurchases == NO_NUMERIC_LIMIT)
			iMaxFreePurchases = 0;
		
		pFreePurchaseAttrib = eaIndexedGetUsingString(&pExtract->eaKeys, s_InteriorConfig.pchFreePurchaseKey);
		if(pFreePurchaseAttrib && pFreePurchaseAttrib->pchValue)
			iPurchased = atoi(pFreePurchaseAttrib->pchValue);

		return MAX(0, iMaxFreePurchases - iPurchased);
	}

	return 0;
}

bool
InteriorCommon_EntHasFreePurchase( Entity *pEnt, GameAccountDataExtract *pExtract )
{
	return InteriorCommon_trh_EntFreePurchasesRemaining(CONTAINER_NOCONST(Entity, pEnt), pExtract) > 0 ;
}

void
InteriorCommon_GetSettingsByInterior(InteriorDef *interiorDef, InteriorSetting ***peaSettings)
{
	FOR_EACH_IN_REFDICT(g_hInteriorSettingDict, InteriorSetting, pSetting)
	{
		if(GET_REF(pSetting->hInterior) == interiorDef)
		{
			eaPush(peaSettings, pSetting);
		}
	} FOR_EACH_END;
}

MultiVal 
InteriorCommon_Eval(Expression *pExpr, Entity *pEnt, MMType eReturnType)
{
	MultiVal mvReturn = {0};

	if (pExpr) 
	{
		if (g_pInteriorContext) 
		{
			bool bValid = false;

			exprContextSetSelfPtr(g_pInteriorContext, pEnt);
			exprContextSetPartition(g_pInteriorContext, entGetPartitionIdx(pEnt));
			exprContextSetSilentErrors(g_pInteriorContext, false);
			exprContextSetPointerVarPooledCached(g_pInteriorContext, s_pcVarPlayer, (Entity*)pEnt, parse_Entity, true, true, &s_hVarPlayer);

			exprEvaluate(pExpr, g_pInteriorContext, &mvReturn);

			switch (eReturnType) 
			{
				case MMT_INT32:
				case MMT_INT64:
					MultiValGetInt(&mvReturn, &bValid);
					break;
				case MMT_FLOAT32:
				case MMT_FLOAT64:
					MultiValGetFloat(&mvReturn, &bValid);
					break;
				case MMT_STRING:
					MultiValGetString(&mvReturn, &bValid);
					break;
			}

			if (!bValid) 
			{
				if (mvReturn.type == MULTI_INVALID) 
				{
					Errorf("Error executing interior expression '%s':\n%s", mvReturn.str, exprGetCompleteString(pExpr));
				} 
				else 
				{
					Errorf("Interior expression returned incorrect data type:\n%s", exprGetCompleteString(pExpr));
				}
			}
		}
		else 
		{
			Errorf("Error executing interior expression 'NULL Context':\n%s", exprGetCompleteString(pExpr));
		}
	}

	return mvReturn;
}

AUTO_RUN;
int InteriorCommon_InitExpressions(void)
{
	ExprFuncTable* stFuncs;

	s_pcVarPlayer = allocAddStaticString("Player");

	g_pInteriorContext = exprContextCreate();
	stFuncs = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stFuncs, "player");
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextSetFuncTable(g_pInteriorContext, stFuncs);
	exprContextSetSelfPtr(g_pInteriorContext, NULL);
	exprContextSetPointerVarPooledCached(g_pInteriorContext, s_pcVarPlayer, NULL, parse_Entity, true, true, &s_hVarPlayer);
	exprContextSetAllowRuntimePartition(g_pInteriorContext);
	exprContextSetAllowRuntimeSelfPtr(g_pInteriorContext);

	return 1;
}

#include "InteriorCommon_h_ast.c"
