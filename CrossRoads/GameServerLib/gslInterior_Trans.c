/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "objTransactions.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "GameAccountDataCommon.h"
#include "StringCache.h"
#include "entCritter.h"
#include "InteriorCommon.h"
#include "MicroTransactions.h"
#include "Microtransactions_Transact.h"
#include "inventoryCommon.h"

#include "AutoGen/InteriorCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"

AUTO_TRANS_HELPER_SIMPLE;
bool gslInterior_SettingSetsOption(InteriorSetting *pSetting, InteriorOptionDef *pOptionDef)
{
	FOR_EACH_IN_EARRAY(pSetting->eaOptionSettings, InteriorOptionRef, pOptionSetting)
	{
		InteriorOptionDef *pOptionSettingDef = GET_REF(pOptionSetting->hOption);
		if(pOptionSettingDef 
			&& pOptionDef 
			&& pOptionDef == pOptionSettingDef)
			return true;
	} FOR_EACH_END;

	return false;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome
gslInterior_trh_EntitySetOption(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, InteriorOptionDef *pOptionDef, InteriorOptionChoice *pOptionChoice)
{
	NOCONST(InteriorOption) *pOption = NULL;
	
	if ( pEnt->pSaved->interiorData == NULL )
	{
		// need to create interiorData
		pEnt->pSaved->interiorData = StructCreateNoConst(parse_EntityInteriorData);
	}

	pOption = eaIndexedGetUsingString(&pEnt->pSaved->interiorData->options, pOptionDef->name);
	if ( pOption == NULL )
	{
		// check and see if we are just setting the default option
		if (stricmp(REF_STRING_FROM_HANDLE(pOptionDef->hDefaultChoice), pOptionChoice->name))
		{
			pOption = StructCreateNoConst(parse_InteriorOption);

			// Set the option and choice handles
			SET_HANDLE_FROM_REFERENT(g_hInteriorOptionDefDict, pOptionDef, pOption->hOption);
			SET_HANDLE_FROM_REFERENT(g_hInteriorOptionChoiceDict, pOptionChoice, pOption->hChoice);
			
			eaIndexedEnableNoConst(&pEnt->pSaved->interiorData->options, parse_InteriorOption);
			eaPush(&pEnt->pSaved->interiorData->options, pOption);
		}
	}
	// check and see if this option is already set
	else if (stricmp(REF_STRING_FROM_HANDLE(pOption->hChoice), pOptionChoice->name))
	{
		if (stricmp(REF_STRING_FROM_HANDLE(pOptionDef->hDefaultChoice), pOptionChoice->name))
		{
			// just edit the choice value in place
			REMOVE_HANDLE(pOption->hChoice);
			SET_HANDLE_FROM_REFERENT(g_hInteriorOptionChoiceDict, pOptionChoice, pOption->hChoice);
		}
		else
		{
			eaFindAndRemove(&pEnt->pSaved->interiorData->options,pOption);
			StructDestroyNoConst(parse_InteriorOption, pOption);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
void gslInterior_trh_LoadAlternate(ATH_ARG NOCONST(Entity) *pEnt)
{
	const char *pchInterior = REF_STRING_FROM_HANDLE(pEnt->pSaved->interiorData->hInteriorDef);
	NOCONST(InteriorData) *pAlternate = NULL;

	pAlternate = eaIndexedGetUsingString(&pEnt->pSaved->interiorData->alternates, pchInterior);

	if(!pAlternate)
	{
		REMOVE_HANDLE(pEnt->pSaved->interiorData->hSetting);
		eaDestroyStructNoConst(&pEnt->pSaved->interiorData->options, parse_InteriorOption);
	}
	else
	{
		COPY_HANDLE(pEnt->pSaved->interiorData->hSetting, pAlternate->hSetting);
		eaCopyStructsNoConst(&pAlternate->options, &pEnt->pSaved->interiorData->options, parse_InteriorOption);
	}
}

AUTO_TRANS_HELPER;
void gslInterior_trh_CopyAlternate(ATH_ARG NOCONST(Entity) *pEnt)
{
	const char *pchInterior = REF_STRING_FROM_HANDLE(pEnt->pSaved->interiorData->hInteriorDef);
	NOCONST(InteriorData) *pAlternate = NULL;

	pAlternate = eaIndexedGetUsingString(&pEnt->pSaved->interiorData->alternates, pchInterior);
	if(!pAlternate)
	{
		pAlternate = StructCreateNoConst(parse_InteriorData);
		COPY_HANDLE(pAlternate->hInteriorDef, pEnt->pSaved->interiorData->hInteriorDef);
		eaPush(&pEnt->pSaved->interiorData->alternates, pAlternate);
	}

	COPY_HANDLE(pAlternate->hSetting,pEnt->pSaved->interiorData->hSetting);
	eaCopyStructsNoConst(&pEnt->pSaved->interiorData->options, &pAlternate->options, parse_InteriorOption);
}

AUTO_TRANSACTION
ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Interiordata, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(petEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Pugckillcreditlimit, .Psaved.Interiordata, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pData, ".Eakeys");
enumTransactionOutcome
gslInterior_tr_SetInterior(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(Entity) *petEnt, const char *interiorDefName, const ItemChangeReason *pReason, NOCONST(GameAccountData) *pData)
{
	const char *pooledInteriorDefName = allocAddString(interiorDefName);
	bool found = false;
	const char *costNumeric;
	S32 cost;

	if ( NONNULL(petEnt) )
	{
		if(petEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET
			|| ISNULL(petEnt->pSaved))
			return TRANSACTION_OUTCOME_FAILURE;

		if ( ISNULL(petEnt->pSaved->interiorData) )
		{
			// need to create interiorData
			petEnt->pSaved->interiorData = StructCreateNoConst(parse_EntityInteriorData);
		}

		if(InteriorConfig_PersistAlternates() &&
			IS_HANDLE_ACTIVE(petEnt->pSaved->interiorData->hInteriorDef))
		{
			gslInterior_trh_CopyAlternate(petEnt);
		}

		// set the pet's current interior to the named InteriorDef
		REF_HANDLE_SET_FROM_STRING(g_hInteriorDefDict, pooledInteriorDefName, petEnt->pSaved->interiorData->hInteriorDef);

		if(InteriorConfig_PersistAlternates())
		{
			gslInterior_trh_LoadAlternate(petEnt);
		}
		else
		{
			InteriorSetting *pSetting = GET_REF(petEnt->pSaved->interiorData->hSetting);

			if( pSetting
				&& REF_HANDLE_GET_STRING(petEnt->pSaved->interiorData->hInteriorDef) != REF_HANDLE_GET_STRING(pSetting->hInterior))
			{
				REF_HANDLE_REMOVE(petEnt->pSaved->interiorData->hSetting);
			}
		}
	}
	else
	{
		if(ISNULL(playerEnt->pSaved))
			return TRANSACTION_OUTCOME_FAILURE;

		if ( ISNULL(playerEnt->pSaved->interiorData) )
		{
			// need to create interiorData
			playerEnt->pSaved->interiorData = StructCreateNoConst(parse_EntityInteriorData);
		}

		if(InteriorConfig_PersistAlternates() &&
			IS_HANDLE_ACTIVE(playerEnt->pSaved->interiorData->hInteriorDef))
		{
			gslInterior_trh_CopyAlternate(playerEnt);
		}

		// set the pet's current interior to the named InteriorDef
		REF_HANDLE_SET_FROM_STRING(g_hInteriorDefDict, pooledInteriorDefName, playerEnt->pSaved->interiorData->hInteriorDef);

		if(InteriorConfig_PersistAlternates())
		{
			gslInterior_trh_LoadAlternate(playerEnt);
		}
		else
		{
			InteriorSetting *pSetting = GET_REF(playerEnt->pSaved->interiorData->hSetting);

			if( pSetting
				&& REF_HANDLE_GET_STRING(playerEnt->pSaved->interiorData->hInteriorDef) != REF_HANDLE_GET_STRING(pSetting->hInterior))
			{
				REF_HANDLE_REMOVE(playerEnt->pSaved->interiorData->hSetting);
			}
		}
	}


	// make sure player can afford the cost
	costNumeric = InteriorConfig_InteriorChangeCostNumeric();
	if ( ( costNumeric != NULL ) && ( costNumeric[0] != '\0' ) )
	{
		cost = inv_trh_InteriorChangeCost(ATR_PASS_ARGS,playerEnt,petEnt,true,pReason,pData);
		if ( cost > 0 )
		{
			// check that the owner has enough currency and then subtract the cost from their balance
			S32 ownerCurrency = inv_trh_GetNumericValue( ATR_PASS_ARGS, playerEnt, costNumeric );
			if ( ownerCurrency < cost || !inv_ent_trh_SetNumeric( ATR_EMPTY_ARGS, playerEnt, true, costNumeric, ownerCurrency - cost, pReason ) ) 
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Interiordata, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(petEnt, ".Psaved.Interiordata");
enumTransactionOutcome
gslInterior_tr_SetOptionChoice(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(Entity) *petEnt, const char *optionName, const char *choiceName, const ItemChangeReason *pReason)
{
	InteriorOptionDef *pOptionDef;
	InteriorOptionChoice *pOptionChoice;
	InteriorSetting *pCurrentSetting = NULL;
	enumTransactionOutcome retVal = TRANSACTION_OUTCOME_FAILURE;

	if ( ISNULL(playerEnt) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pOptionDef = InteriorCommon_FindOptionDefByName(optionName);
	pOptionChoice = InteriorCommon_FindOptionChoiceByName(choiceName);

	if ( pOptionDef == NULL || pOptionChoice == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if(NONNULL(petEnt))
	{
		pCurrentSetting = SAFE_MEMBER2(petEnt, pSaved, interiorData) ? GET_REF(petEnt->pSaved->interiorData->hSetting) : NULL;
		devassert(petEnt->myEntityType == GLOBALTYPE_ENTITYSAVEDPET);
		if ( petEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET )
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}

		retVal = gslInterior_trh_EntitySetOption(ATR_PASS_ARGS,petEnt, pOptionDef, pOptionChoice);
		if(retVal != TRANSACTION_OUTCOME_SUCCESS)
			return retVal;

		//The setting is no longer the 'override'
		if(pCurrentSetting && gslInterior_SettingSetsOption(pCurrentSetting,pOptionDef))
			REMOVE_HANDLE(petEnt->pSaved->interiorData->hSetting);
	}
	else
	{
		pCurrentSetting = SAFE_MEMBER2(playerEnt, pSaved, interiorData) ? GET_REF(playerEnt->pSaved->interiorData->hSetting) : NULL;;
		retVal = gslInterior_trh_EntitySetOption(ATR_PASS_ARGS,playerEnt, pOptionDef, pOptionChoice);
		if(retVal != TRANSACTION_OUTCOME_SUCCESS)
			return retVal;

		if(pCurrentSetting && gslInterior_SettingSetsOption(pCurrentSetting,pOptionDef))
			REMOVE_HANDLE(playerEnt->pSaved->interiorData->hSetting);
	}

	// make sure player can afford the cost
	if ( ( pOptionDef->optionChangeCostNumeric != NULL ) && ( pOptionDef->optionChangeCostNumeric[0] != '\0' ) )
	{
		if ( pOptionDef->optionChangeCost > 0 )
		{
			// check that the owner has enough currency and then subtract the cost from their balance
			S32 ownerCurrency = inv_trh_GetNumericValue( ATR_PASS_ARGS, playerEnt, pOptionDef->optionChangeCostNumeric );
			if ( ownerCurrency < pOptionDef->optionChangeCost || !inv_ent_trh_SetNumeric( ATR_PASS_ARGS, playerEnt, true, pOptionDef->optionChangeCostNumeric, ownerCurrency - pOptionDef->optionChangeCost, pReason ) ) 
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Interiordata, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(petEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pchar.Ilevelexp, .Psaved.Interiordata, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pData, ".Eakeys");
enumTransactionOutcome
gslInterior_tr_SetInteriorAndOption(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(Entity) *petEnt, const char *interiorDefName, NOCONST(GameAccountData) *pData, const char *optionName, const char *choiceName, const ItemChangeReason *pReason)
{
	enumTransactionOutcome ret;
		
	ret = gslInterior_tr_SetInterior(ATR_PASS_ARGS, playerEnt, petEnt, interiorDefName, pReason, pData);
	if ( ret == TRANSACTION_OUTCOME_SUCCESS )
	{
		ret = gslInterior_tr_SetOptionChoice(ATR_PASS_ARGS, playerEnt, petEnt, optionName, choiceName, pReason);
	}

	return ret;
}

AUTO_TRANSACTION
	ATR_LOCKS(playerEnt, ".pSaved.interiorData.hSetting")
	ATR_LOCKS(petEnt, ".pSaved.interiorData.hSetting");
enumTransactionOutcome gslInterior_tr_ClearSetting(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(Entity) *petEnt)
{
	if ( NONNULL(petEnt) )
	{
		if(petEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET
			|| ISNULL(petEnt->pSaved)
			|| ISNULL(petEnt->pSaved->interiorData))
			return TRANSACTION_OUTCOME_FAILURE;

		REMOVE_HANDLE(petEnt->pSaved->interiorData->hSetting);
	}
	else
	{
		if(ISNULL(playerEnt->pSaved)
			|| ISNULL(playerEnt->pSaved->interiorData))
			return TRANSACTION_OUTCOME_FAILURE;

		REMOVE_HANDLE(playerEnt->pSaved->interiorData->hSetting);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(playerEnt ,".pSaved.interiorData")
	ATR_LOCKS(petEnt ,".pSaved.interiorData");
enumTransactionOutcome
	gslInterior_tr_SetSetting(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(Entity) *petEnt, const char *settingName)
{
	InteriorSetting *setting = NULL;
	bool found = false;

	setting = RefSystem_ReferentFromString(g_hInteriorSettingDict, settingName);

	if(!setting)
		return TRANSACTION_OUTCOME_FAILURE;

	if ( NONNULL(petEnt) )
	{
		if(petEnt->myEntityType != GLOBALTYPE_ENTITYSAVEDPET
			|| ISNULL(petEnt->pSaved)
			|| ISNULL(petEnt->pSaved->interiorData)
			|| !REF_COMPARE_HANDLES(petEnt->pSaved->interiorData->hInteriorDef, setting->hInterior))
			return TRANSACTION_OUTCOME_FAILURE;
		
		SET_HANDLE_FROM_STRING(g_hInteriorSettingDict, setting->pchName, petEnt->pSaved->interiorData->hSetting);

		FOR_EACH_IN_EARRAY(setting->eaOptionSettings, InteriorOptionRef, pOptionRef)
		{
			InteriorOptionDef *pOptionDef = GET_REF(pOptionRef->hOption);
			InteriorOptionChoice *pChoice = GET_REF(pOptionRef->hChoice);

			if(!pOptionDef || !pChoice)
				continue;

			gslInterior_trh_EntitySetOption(ATR_PASS_ARGS, petEnt, pOptionDef, pChoice);
		} FOR_EACH_END;
	}
	else
	{
		if(ISNULL(playerEnt->pSaved)
			|| ISNULL(playerEnt->pSaved->interiorData)
			|| !REF_COMPARE_HANDLES(playerEnt->pSaved->interiorData->hInteriorDef, setting->hInterior))
			return TRANSACTION_OUTCOME_FAILURE;

		SET_HANDLE_FROM_STRING(g_hInteriorSettingDict, setting->pchName, playerEnt->pSaved->interiorData->hSetting);

		FOR_EACH_IN_EARRAY(setting->eaOptionSettings, InteriorOptionRef, pOptionRef)
		{
			InteriorOptionDef *pOptionDef = GET_REF(pOptionRef->hOption);
			InteriorOptionChoice *pChoice = GET_REF(pOptionRef->hChoice);

			if(!pOptionDef || !pChoice)
				continue;

			gslInterior_trh_EntitySetOption(ATR_PASS_ARGS, playerEnt, pOptionDef, pChoice);
		} FOR_EACH_END;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(playerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Psaved.Interiordata, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(petEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pchar.Ilevelexp, .Psaved.Interiordata, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pData, ".Eakeys");
enumTransactionOutcome
	gslInterior_tr_SetInteriorAndSetting(ATR_ARGS, NOCONST(Entity) *playerEnt, NOCONST(Entity) *petEnt, const char *interiorDefName, NOCONST(GameAccountData) *pData, const char *settingName, const ItemChangeReason *pReason)
{
	enumTransactionOutcome ret;

	ret = gslInterior_tr_SetInterior(ATR_PASS_ARGS, playerEnt, petEnt, interiorDefName, pReason, pData);
	if ( ret == TRANSACTION_OUTCOME_SUCCESS )
	{
		if(settingName && *settingName)
			ret = gslInterior_tr_SetSetting(ATR_PASS_ARGS, playerEnt, petEnt, settingName);
		else
			ret = gslInterior_tr_ClearSetting(ATR_PASS_ARGS, playerEnt, petEnt);
	}

	return ret;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt,".Psaved.Interiordata");
enumTransactionOutcome gslInterior_tr_ClearData(ATR_ARGS, NOCONST(Entity) *pEnt)
{
	if(ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;
	
	StructDestroyNoConstSafe(parse_EntityInteriorData,&pEnt->pSaved->interiorData);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pmicrotransinfo.Eaonetimepurchases, .Psaved.Ppallowedcritterpets, .Pplayer.Pplayeraccountdata.Iversion, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Itemidmax, .Psaved.Ppbuilds, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems, .Pcritter, .Pchar")
ATR_LOCKS(pData, ".Eaallpurchases[], .Iaccountid, .Eakeys, .Iversion, .Eacostumekeys, .Eapermissions, .Eatokens, .Eavanitypets, .Idayssubscribed, .Blinkedaccount, .Bshadowaccount, .Eaaccountkeyvalues, .Umaxcharacterlevelcached, .Bbilled");
enumTransactionOutcome gslInterior_tr_UseFreePurchase(ATR_ARGS, NOCONST(Entity) *pEnt,
														CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
														NOCONST(GameAccountData) *pData,
														MicroTransactionDef *pMTDef,
														const char *pDefName,
														const ItemChangeReason *pReason)
{
	GameAccountDataExtract *pExtract = NULL;
	if(!trhMicroTransactionDef_Grant(ATR_PASS_ARGS, pEnt, eaPets, pData, NULL, pMTDef, pDefName, pReason))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	else
	{
		trhMicroTransDef_AddPurchaseStamp(pData, pMTDef, pDefName, NULL, NULL);
	}

	pExtract = entity_trh_CreateGameAccountDataExtract(pData);

	if(InteriorCommon_trh_EntFreePurchasesRemaining(pEnt, pExtract) <= 0)
	{
		entity_trh_DestroyGameAccountDataExtract(&pExtract);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if(!slGAD_trh_ChangeAttrib(ATR_PASS_ARGS, pData, InteriorCommon_GetFreePurchaseKey(), 1))
	{
		entity_trh_DestroyGameAccountDataExtract(&pExtract);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	entity_trh_DestroyGameAccountDataExtract(&pExtract);
	return TRANSACTION_OUTCOME_SUCCESS;
}
