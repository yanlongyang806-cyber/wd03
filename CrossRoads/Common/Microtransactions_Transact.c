#include "AccountDataCache.h"
#include "accountnet.h"
#include "Alerts.h"
#include "AutoTransDefs.h"
#include "chatCommon.h"
#include "CostumeCommon.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "EString.h"
#include "file.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionTransactions.h"
#include "globalTypes.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "MicroTransactions.h"
#include "microtransactions_common.h"
#include "MicroTransactions_Transact.h"
#include "Money.h"
#include "Player.h"
#include "Powers.h"
#include "SavedPetCommon.h"
#include "species_common.h"
#include "StringCache.h"
#include "utils.h"

#include "MicroTransactions_Transact_h_ast.h"
#include "CostumeCommon_h_ast.h"
#include "Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "GameAccountData_h_ast.h"
#include "itemCommon_h_ast.h"
#include "MicroTransactions_h_ast.h"
#include "microtransactions_common_h_ast.h"
#include "Player_h_ast.h"

AUTO_TRANS_HELPER
ATR_LOCKS(pAccountData, ".Eavanitypets, .Iversion");
static bool trhMicroTrans_UnlockVanityPet_Internal(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pAccountData, PowerDef *pPowerDef)
{
	PERFINFO_AUTO_START_FUNC();
	if(NONNULL(pPowerDef))
	{
		S32 iIdx = eaFindString(&pAccountData->eaVanityPets, pPowerDef->pchName);
		if(iIdx < 0)
		{
			eaPush(&pAccountData->eaVanityPets, StructAllocString(pPowerDef->pchName));
			//Update the GAD's version
			pAccountData->iVersion++;
			PERFINFO_AUTO_STOP();

			return true;
		}
		else
		{
			TRANSACTION_APPEND_LOG_FAILURE("The vanity pet %s was already unlocked.", pPowerDef->pchName);
		}
	}
	PERFINFO_AUTO_STOP();
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccountData, ".Eavanitypets, .Iversion");
bool trhMicroTrans_UnlockVanityPet(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pAccountData, PowerDef *pPowerDef)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);
	return trhMicroTrans_UnlockVanityPet_Internal(ATR_PASS_ARGS, pAccountData, pPowerDef);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccountData, ".Eavanitypets, .Iversion");
bool trhMicroTrans_UnlockVanityPet_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pAccountData, PowerDef *pPowerDef)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return trhMicroTrans_UnlockVanityPet_Internal(ATR_PASS_ARGS, pAccountData, pPowerDef);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eacostumekeys, .Iversion");
static bool trhMicroTrans_UnlockCostume_Internal(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, ItemDef *pDef)
{
	bool bUnlocked = false;
	S32 iCostumeIdx;

	if( ISNULL(pAccountData) || ISNULL(pDef))
		return(bUnlocked);

	PERFINFO_AUTO_START_FUNC();
	for(iCostumeIdx = eaSize(&pDef->ppCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
	{
		char *estrCostumeRef = NULL;
		NOCONST(AttribValuePair) *pPair = NULL;
		MicroTrans_FormItemEstr(&estrCostumeRef, 
			GetShortProductName(), kMicroItemType_PlayerCostume, 
			REF_STRING_FROM_HANDLE(pDef->ppCostumes[iCostumeIdx]->hCostumeRef), 1);

		pPair = eaIndexedGetUsingString(&pAccountData->eaCostumeKeys, estrCostumeRef);
		if(!pPair)
		{
			pPair = StructCreateNoConst(parse_AttribValuePair);
			pPair->pchAttribute = StructAllocString(estrCostumeRef);
			pPair->pchValue = StructAllocString("1");
			eaPush(&pAccountData->eaCostumeKeys, pPair);

			bUnlocked = true;
		}
		estrDestroy(&estrCostumeRef);
	}

	if(bUnlocked)
	{
		//Update versions
		pAccountData->iVersion++;
		if(NONNULL(pEnt) 
			&& pEnt->pPlayer->pPlayerAccountData->iVersion == pAccountData->iVersion-1)
		{
			pEnt->pPlayer->pPlayerAccountData->iVersion++;
		}
	}
	else
	{
		TRANSACTION_APPEND_LOG_FAILURE("Everything on the costume unlock item %s was already unlocked.", pDef->pchName);
	}
	PERFINFO_AUTO_STOP();

	return bUnlocked;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eacostumekeys, .Iversion");
bool trhMicroTrans_UnlockCostume(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, ItemDef *pDef)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);
	return trhMicroTrans_UnlockCostume_Internal(ATR_PASS_ARGS, pEnt, pAccountData, pDef);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eacostumekeys, .Iversion");
bool trhMicroTrans_UnlockCostume_Force(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, ItemDef *pDef)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return trhMicroTrans_UnlockCostume_Internal(ATR_PASS_ARGS, pEnt, pAccountData, pDef);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eacostumekeys, .Iversion");
static bool trhMicroTrans_UnlockCostumeRef_Internal(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, PlayerCostume *pCostume)
{
	bool bUnlocked = false;
	char *estrCostumeID = NULL;
	NOCONST(AttribValuePair) *pAttrib = NULL;

	if( ISNULL(pAccountData) || ISNULL(pCostume) )
		return(bUnlocked);

	PERFINFO_AUTO_START_FUNC();

	//Form the CostumeID string
	MicroTrans_FormItemEstr(&estrCostumeID, GetShortProductName(), kMicroItemType_PlayerCostume, pCostume->pcName, 1);

	//DO NOT RETURN BEFORE DESTROYING pchCostumeID!

	pAttrib = eaIndexedGetUsingString(&pAccountData->eaCostumeKeys, estrCostumeID);
	if(!pAttrib)
	{
		pAttrib = StructCreateNoConst(parse_AttribValuePair);
		pAttrib->pchAttribute = StructAllocString(estrCostumeID);
		eaPush(&pAccountData->eaCostumeKeys, pAttrib);
	}
	if(!pAttrib->pchValue || atoi(pAttrib->pchValue) <= 0)
	{
		//Setup the value for this attribute
		StructFreeString(pAttrib->pchValue);
		pAttrib->pchValue = StructAllocString("1");
		bUnlocked = true;

		//Update versions
		pAccountData->iVersion++;
		if(NONNULL(pEnt) 
			&& pEnt->pPlayer->pPlayerAccountData->iVersion == pAccountData->iVersion-1)
		{
			pEnt->pPlayer->pPlayerAccountData->iVersion++;
		}
	}

	if(!bUnlocked)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Costume %s was already unlocked.", pCostume->pcName);
	}

	estrDestroy(&estrCostumeID);
	PERFINFO_AUTO_STOP();

	return bUnlocked;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eacostumekeys, .Iversion");
bool trhMicroTrans_UnlockCostumeRef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, PlayerCostume *pCostume)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);
	return trhMicroTrans_UnlockCostumeRef_Internal(ATR_PASS_ARGS, pEnt, pAccountData, pCostume);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eacostumekeys, .Iversion");
bool trhMicroTrans_UnlockCostumeRef_Force(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, PlayerCostume *pCostume)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return trhMicroTrans_UnlockCostumeRef_Internal(ATR_PASS_ARGS, pEnt, pAccountData, pCostume);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eakeys, .Iversion");
bool trhMicroTrans_UnlockSpecies(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, SpeciesDef *pSpecies)
{
	bool bUnlocked = false;
	NOCONST(AttribValuePair) *pAttrib = NULL;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);

	if( ISNULL(pAccountData) || ISNULL(pSpecies))
		return(bUnlocked);

	PERFINFO_AUTO_START_FUNC();

	pAttrib = eaIndexedGetUsingString(&pAccountData->eaKeys, pSpecies->pcUnlockCode);

	if(!pAttrib)
	{
		pAttrib = StructCreateNoConst(parse_AttribValuePair);
		pAttrib->pchAttribute = StructAllocString(pSpecies->pcUnlockCode);
		eaPush(&pAccountData->eaKeys, pAttrib);
	}

	if(!pAttrib->pchValue || atoi(pAttrib->pchValue) <= 0)
	{
		//Setup the value for this attribute
		StructFreeString(pAttrib->pchValue);
		pAttrib->pchValue = StructAllocString("1");
		bUnlocked = true;

		//Update versions
		pAccountData->iVersion++;
		if(NONNULL(pEnt) 
			&& pEnt->pPlayer->pPlayerAccountData->iVersion == pAccountData->iVersion-1)
		{
			pEnt->pPlayer->pPlayerAccountData->iVersion++;
		}
	}

	if(!bUnlocked)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Species %s was already unlocked.", pSpecies->pcName);
	}

	PERFINFO_AUTO_STOP();

	return bUnlocked;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Eapermissions, .Eatokens, .Iversion, .Umaxcharacterlevelcached, .Bbilled");
bool trhMicroTrans_GrantPermission(ATR_ARGS, 
								   ATH_ARG NOCONST(Entity) *pEnt,
								   ATH_ARG NOCONST(GameAccountData) *pAccountData,
								   GamePermissionDef *pPermissionDef)
{
	bool bUnlocked = false;
	NOCONST(GamePermission) *pGADPermission = NULL;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);

	if( ISNULL(pAccountData) || ISNULL(pPermissionDef))
		return(bUnlocked);

	PERFINFO_AUTO_START_FUNC();

	pGADPermission = eaIndexedGetUsingString(&pAccountData->eaPermissions, pPermissionDef->pchName);

	if(!pGADPermission)
	{
		pGADPermission = StructCreateNoConst(parse_GamePermission);
		pGADPermission->pchName = allocAddString(pPermissionDef->pchName);

		eaPush(&pAccountData->eaPermissions, pGADPermission);

		GamePermissions_trh_CreateTokens(pAccountData, NULL);

		bUnlocked = true;
		
		//Update versions
		pAccountData->iVersion++;
		if(NONNULL(pEnt) 
			&& pEnt->pPlayer->pPlayerAccountData->iVersion == pAccountData->iVersion-1)
		{
			pEnt->pPlayer->pPlayerAccountData->iVersion++;
		}
	}

	if(!bUnlocked)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Permission %s was already unlocked.", pPermissionDef->pchName);
	}

	PERFINFO_AUTO_STOP();

	return bUnlocked;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
ATR_LOCKS(pAccountData, ".Idayssubscribed, .Eakeys, .Eacostumekeys, .Eatokens, .Blinkedaccount, .Bshadowaccount, .Eaaccountkeyvalues");
bool trhMicroTrans_GrantRewards(ATR_ARGS, 
								ATH_ARG NOCONST(Entity) *pEnt,
								ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
								ATH_ARG NOCONST(GameAccountData) *pAccountData,
								MicroTransactionRewards *pRewards, 
								S32 iPartIdx,
								const ItemChangeReason *pReason)
{
	GameAccountDataExtract *pExtract = entity_trh_CreateGameAccountDataExtract(pAccountData);
	bool bSuccess = false;
	S32 i;

	PERFINFO_AUTO_START_FUNC();

	for(i = eaSize(&pRewards->eaRewards)-1; i >= 0; i--)
	{
		MicroTransactionPartRewards *pPartRewards = pRewards->eaRewards[i];
		if(pPartRewards->iPartIndex == iPartIdx)
		{
			GiveRewardBagsData Data = {0};
			Data.ppRewardBags = pPartRewards->eaBags;
			if(inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, eaPets, &Data, kRewardOverflow_DisallowOverflowBag, NULL, pReason, pExtract, NULL))
			{
				bSuccess = true;
			}
			break;
		}
	}
	entity_DestroyLocalGameAccountDataExtract(&pExtract);
	PERFINFO_AUTO_STOP();
	return bSuccess;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
bool entity_trh_MicroTrans_UniqueCheck(ATR_ARGS, 
									   ATH_ARG NOCONST(Entity) *pEnt, 
									   ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
									   ItemDef *pItemDef, S32 iCount)
{
	PERFINFO_AUTO_START_FUNC();
	if(pItemDef->flags & kItemDefFlag_Unique)
	{
		if(iCount > 1)
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAIL: The item [%s] is unique, however the micro transaction specifies to grant [%d].",
				pItemDef->pchName, iCount);
			PERFINFO_AUTO_STOP();
			return(false);
		}
		else if (inv_ent_trh_HasUniqueItem(ATR_PASS_ARGS, pEnt, eaPets, pItemDef->pchName))
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAIL: The item (%s) is unique and the entity (%s) already one.",
				pItemDef->pchName, pEnt->debugName);
			PERFINFO_AUTO_STOP();
			return(false);
		}
	}
	PERFINFO_AUTO_STOP();

	return(true);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .pInventoryV2.Ppinventorybags, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(eaPets, ".Pcritter, .Pchar");
bool entity_trh_MicroTrans_PetCheck(ATR_ARGS, 
									ATH_ARG NOCONST(Entity) *pEnt, 
									ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
									ItemDef *pItemDef,
									GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	if ((pItemDef->flags & kItemDefFlag_EquipOnPickup))
	{
		PetDef* pPetDef = GET_REF(pItemDef->hPetDef);
		if (pPetDef && !trhEntity_CanAddSavedPet(pEnt,eaPets,pPetDef,0,pItemDef->bMakeAsPuppet,pExtract))
		{
			TRANSACTION_APPEND_LOG_FAILURE(
				"FAIL: Entity (%s) is not allowed to add the pet (%s) from item (%s).",
				pEnt->debugName, pPetDef->pchPetName, pItemDef->pchName);
			PERFINFO_AUTO_STOP();
			return(false);
		}
	}
	else
	{
		PetDef* pPetDef = GET_REF(pItemDef->hPetDef);
		if (pPetDef && trhEntity_CheckAcquireLimit(pEnt, eaPets, pPetDef, 0))
		{
			TRANSACTION_APPEND_LOG_FAILURE(
				"FAIL: Entity (%s) is not allowed to add the pet (%s) from item (%s).",
				pEnt->debugName, pPetDef->pchPetName, pItemDef->pchName);
			PERFINFO_AUTO_STOP();
			return(false);
		}
	}
	PERFINFO_AUTO_STOP();

	return(true);
}

//ENTITY MAY BE NULL
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppliteBags[]")
ATR_LOCKS(pAccountData, ".Eakeys");
bool trh_MicroTransactionDef_GrantSpecial(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
										  ATH_ARG NOCONST(GameAccountData) *pAccountData,
										  const MicroTransactionPart *pPart,
										  const ItemChangeReason *pReason)
{
	bool bReturnValue = false;
	PERFINFO_AUTO_START_FUNC();
	switch(pPart->eSpecialPartType)
	{
	case kSpecialPartType_BankSize:
		{
			if((pPart->iCount > 0) && NONNULL(pEnt))
			{
				if(inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, "BankSizeMicrotrans", pPart->iCount, pReason))
				{
					bReturnValue = true;
				}
			}
			break;
		}
	case kSpecialPartType_SharedBankSize:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetSharedBankSlotGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_InventorySize:
		{
			if((pPart->iCount > 0) && NONNULL(pEnt))
			{
				if(inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, "AddInvSlotsMicrotrans", pPart->iCount, pReason))
				{
					bReturnValue = true;
				}
			}
			break;
		}
    case kSpecialPartType_ItemAssignmentReserveSlots:
        {
            if((pPart->iCount > 0) && NONNULL(pEnt))
            {
                if(inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, "Addreservedoffslotsmicrotrans", pPart->iCount, pReason))
                {
                    bReturnValue = true;
                }
            }
            break;
        }
	case kSpecialPartType_CharSlots:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetCharSlotsGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}

	case kSpecialPartType_SuperPremium:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetSuperPremiumGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}

	case kSpecialPartType_CostumeSlots:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetCostumeSlotsGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_Respec:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetRespecTokensGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_Rename:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetRenameTokensGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_Retrain:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetRetrainTokensGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_OfficerSlots:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetOfficerSlotsGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}

	case kSpecialPartType_CostumeChange:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetFreeCostumeChangeGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_ShipCostumeChange:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetFreeShipCostumeChangeGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_ItemAssignmentCompleteNow:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetItemAssignmentCompleteNowGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	case kSpecialPartType_ItemAssignmentUnslotItem:
		{
			if(gConf.bDontAllowGADModification || slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetItemAssignmentUnslotTokensGADKey(), pPart->iCount, 0, 100000))
			{
				bReturnValue = true;
			}
			break;
		}
	}
	PERFINFO_AUTO_STOP();

	return bReturnValue;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccountData, ".Eakeys");
bool trhMicroTransactionDef_GrantAttribChange(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pAccountData, AttribValuePairChange *pChange)
{
	bool bReturnValue = false;

	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);

	PERFINFO_AUTO_START_FUNC();
	switch(pChange->eType)
	{
	// Boolean change that doesn't fail
	case kAVChangeType_BooleanNoFail:
		bReturnValue = true;
		//Boolean change type.  Fails if the key is already set (or already unset depending on pChange->iVal)
	case kAVChangeType_Boolean:
		{
			NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&pAccountData->eaKeys, pChange->pchAttribute);
			if( !!pChange->iVal && (!pPair || !pPair->pchValue || !atoi(pPair->pchValue)) )
			{
				if(slGAD_trh_SetAttrib(ATR_PASS_ARGS, pAccountData, pChange->pchAttribute, "1") == TRANSACTION_OUTCOME_SUCCESS)
				{
					bReturnValue = true;
				}
			}
			else if(!pChange->iVal && pPair && pPair->pchValue && atoi(pPair->pchValue) == 1)
			{
				if(slGAD_trh_SetAttrib(ATR_PASS_ARGS, pAccountData, pChange->pchAttribute, "0") == TRANSACTION_OUTCOME_SUCCESS)
				{
					bReturnValue = true;
				}
			}
			break;
		}
	// Integer set without fail
	case kAVChangeType_IntSetNoFail:
		bReturnValue = true;
	//Sets a key-value integer attrib pair
	case kAVChangeType_IntSet:
		{
			char buf[16];
			buf[15] = '\0';
			snprintf(buf, 15, "%d", pChange->iVal);

			if(slGAD_trh_SetAttrib(ATR_PASS_ARGS, pAccountData, pChange->pchAttribute, buf) == TRANSACTION_OUTCOME_SUCCESS)
			{
				bReturnValue = true;
			}
			break;
		}
		// Increments an integer attrib pair
	case kAVChangeType_IntIncrement:
		{
			//If the change is clamped, call the clamped function
			if(pChange->bClampValues)
			{
				if( slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, pChange->pchAttribute, 
					pChange->iVal, pChange->iMinVal, pChange->iMaxVal) )
				{
					bReturnValue = true;
				}
			}
			//Else if it's not clamped, just call the change function
			else
			{
				if( slGAD_trh_ChangeAttrib(ATR_PASS_ARGS, pAccountData, pChange->pchAttribute, pChange->iVal) )
				{
					bReturnValue = true;
				}
			}
			break;
		}
		//Set a string value
	case kAVChangeType_String:
		{
			if(slGAD_trh_SetAttrib(ATR_PASS_ARGS, pAccountData, pChange->pchAttribute, pChange->pchStringVal) == TRANSACTION_OUTCOME_SUCCESS)
			{
				bReturnValue = true;
			}
			break;
		}
	}
	PERFINFO_AUTO_STOP();

	return bReturnValue;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pplayeraccountdata.Iversion, .Pplayer.Pugckillcreditlimit, .Psaved.Ppbuilds, .Psaved.Costumedata.Eaunlockedcostumerefs, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppuppetmaster.Pppuppets, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems, .Pcritter, .Pchar")
ATR_LOCKS(pAccountData, ".Eakeys, .Iversion, .Eacostumekeys, .Idayssubscribed, .Eatokens, .Eapermissions, .Eavanitypets, .Blinkedaccount, .Bshadowaccount, .Eaaccountkeyvalues, .Umaxcharacterlevelcached, .Bbilled");
MicroTransGrantPartResult entity_trh_MicroTransactionDef_GrantPart(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
																   ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
																   ATH_ARG NOCONST(GameAccountData) *pAccountData,
																   GameAccountDataExtract **ppExtract,
																   MicroTransactionRewards *pRewards,
																   const MicroTransactionDef *pDef,
																   S32 iPartIdx,
																   const ItemChangeReason *pReason)
{
	const MicroTransactionPart *pPart = pDef->eaParts[iPartIdx];
	MicroTransGrantPartResult eResult = kMicroTransGrantPartResult_Failed;

	PERFINFO_AUTO_START_FUNC();
	switch(pPart->ePartType)
	{
	case kMicroPart_Attrib:
		{
			//Fail case, this shouldn't happen
			if(ISNULL(pPart->pPairChange))
				break;

			PERFINFO_AUTO_START("Attrib", 1);
			//Grant the attrib change
			if(trhMicroTransactionDef_GrantAttribChange(ATR_PASS_ARGS, pAccountData, pPart->pPairChange))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			PERFINFO_AUTO_STOP();
			break;
		}
	
	case kMicroPart_Costume:
		{
			ItemDef *pItemDef = GET_REF(pPart->hItemDef);

			if(ISNULL(pItemDef))
				break;

			PERFINFO_AUTO_START("Costume", 1);
			if(trhMicroTrans_UnlockCostume(ATR_PASS_ARGS, pEnt, pAccountData, pItemDef))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			else
			{
				eResult = kMicroTransGrantPartResult_NothingUnlocked;
			}
			PERFINFO_AUTO_STOP();
			break;
		}
	case kMicroPart_CostumeRef:
		{
			PlayerCostume *pCostume = GET_REF(pPart->hCostumeDef);

			if(ISNULL(pCostume))
				break;

			PERFINFO_AUTO_START("CostumeRef", 1);
			if(trhMicroTrans_UnlockCostumeRef(ATR_PASS_ARGS, pEnt, pAccountData, pCostume))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			else
			{
				eResult = kMicroTransGrantPartResult_NothingUnlocked;
			}
			PERFINFO_AUTO_STOP();
			break;
		}
	case kMicroPart_Item:
		{
			if(ISNULL(pEnt))
			{
				eResult = kMicroTransGrantPartResult_Failed;
				TRANSACTION_APPEND_LOG_FAILURE("FAIL: Cannot grant items in microtransactions without an entity.");
			}
			else
			{
				ItemDef *pItemDef = GET_REF(pPart->hItemDef);

				if(ISNULL(pItemDef) || pPart->iCount <= 0)
					break;

				PERFINFO_AUTO_START("Item", 1);
				if(pItemDef->eType == kItemType_Numeric)
				{
					if(inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pItemDef->pchName, pPart->iCount, pReason))
					{
						eResult = kMicroTransGrantPartResult_Success;
					}
				}
				else
				{
					Item *item = NULL;
					InvBagIDs eBagID = InvBagIDs_Inventory;
					ItemAddFlags eAddFlags = 0;
					if(!entity_trh_MicroTrans_UniqueCheck(ATR_PASS_ARGS, pEnt, eaPets, pItemDef, pPart->iCount))
					{
						PERFINFO_AUTO_STOP();
						break;
					}
					if (!*ppExtract)
					{
						*ppExtract = entity_trh_CreateShallowGameAccountDataExtract(pAccountData);
					}
					if(!entity_trh_MicroTrans_PetCheck(ATR_PASS_ARGS, pEnt, eaPets, pItemDef, *ppExtract))
					{
						PERFINFO_AUTO_STOP();
						break;
					}

					if(pItemDef->flags & kItemDefFlag_EquipOnPickup)
					{
						eBagID = inv_trh_GetBestBagForItemDef(pEnt, pItemDef, pPart->iCount, false, *ppExtract);
					}
					else if(pPart->bAddToBestBag)
					{
						eBagID = inv_trh_GetBestBagForItemDef(pEnt, pItemDef, pPart->iCount, true, *ppExtract);
					}

					if (pPart->bAllowOverflowBag)
						eAddFlags |= ItemAdd_UseOverflow;

					item = item_FromDefName(pItemDef->pchName);

					CONTAINER_NOCONST(Item, item)->count = pPart->iCount;
					if (item && inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, eBagID, -1, item, pItemDef->pchName, eAddFlags, pReason, *ppExtract) == TRANSACTION_OUTCOME_SUCCESS)
					{
						eResult = kMicroTransGrantPartResult_Success;
					}
					StructDestroySafe(parse_Item,&item);
				}
				if(eResult == kMicroTransGrantPartResult_Failed)
				{
					TRANSACTION_APPEND_LOG_FAILURE("FAIL: Couldn't add the item %s to the inventory.", pItemDef->pchName);
				}
				PERFINFO_AUTO_STOP();
			}
			break;
		}
	case kMicroPart_RewardTable:
		{
			PERFINFO_AUTO_START("RewardTable", 1);
			if(trhMicroTrans_GrantRewards(ATR_PASS_ARGS, pEnt, eaPets, pAccountData, pRewards, iPartIdx, pReason))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			else
			{
				eResult = kMicroTransGrantPartResult_Failed;
			}
			PERFINFO_AUTO_STOP();
			break;
		}
	case kMicroPart_Permission:
		{
			GamePermissionDef *pPermissionDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pPart->pchPermission);
			if(!pPermissionDef)
				break;

			PERFINFO_AUTO_START("Permission", 1);
			if(trhMicroTrans_GrantPermission(ATR_PASS_ARGS, pEnt, pAccountData, pPermissionDef))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			else
			{
				eResult = kMicroTransGrantPartResult_NothingUnlocked;
			}
			PERFINFO_AUTO_STOP();
			break;
		}
	case kMicroPart_Species:
		{
			SpeciesDef *pSpeciesDef = GET_REF(pPart->hSpeciesDef);
			if(ISNULL(pSpeciesDef))
				break;

			PERFINFO_AUTO_START("Species", 1);
			if(trhMicroTrans_UnlockSpecies(ATR_PASS_ARGS, pEnt, pAccountData, pSpeciesDef))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			else
			{
				eResult = kMicroTransGrantPartResult_NothingUnlocked;
			}
			PERFINFO_AUTO_STOP();
			break;
		}

	case kMicroPart_Special:
		{
			PERFINFO_AUTO_START("Special", 1);
			if(trh_MicroTransactionDef_GrantSpecial(ATR_PASS_ARGS, pEnt, pAccountData, pPart, pReason))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			PERFINFO_AUTO_STOP();
			break;
		}
	case kMicroPart_VanityPet:
		{
			PowerDef *pPowerDef = GET_REF(pPart->hPowerDef);
			if(ISNULL(pPowerDef))
				break;

			PERFINFO_AUTO_START("VanityPet", 1);
			if(trhMicroTrans_UnlockVanityPet(ATR_PASS_ARGS, pAccountData, pPowerDef))
			{
				eResult = kMicroTransGrantPartResult_Success;
			}
			else
			{
				eResult = kMicroTransGrantPartResult_NothingUnlocked;
			}
			PERFINFO_AUTO_STOP();
			break;
		}
	}
	PERFINFO_AUTO_STOP();

	return eResult;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pmicrotransinfo.Eaonetimepurchases, .Psaved.Ppbuilds, .Pplayer.Pplayeraccountdata.Iversion, .Hallegiance, .Hsuballegiance, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Pplayer.Playertype, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems, .Pcritter, .Pchar")
ATR_LOCKS(pAccountData, ".Eaallpurchases[], .Iaccountid, .Eakeys, .Iversion, .Eacostumekeys, .Idayssubscribed, .Eatokens, .Eapermissions, .Eavanitypets, .Blinkedaccount, .Bshadowaccount, .Eaaccountkeyvalues, .Umaxcharacterlevelcached, .Bbilled");
bool trhMicroTransactionDef_Grant(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
								  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
								  ATH_ARG NOCONST(GameAccountData) *pAccountData,
								  MicroTransactionRewards *pRewards,
								  const MicroTransactionDef *pDef,
								  const char *pDefName,
								  const ItemChangeReason *pReason)
{
	bool bReturnValue = false;
	S32 iPartIdx;
	GameAccountDataExtract *pExtract = NULL;

	if( ISNULL(pAccountData) )
		return bReturnValue;

	PERFINFO_AUTO_START_FUNC();

	if(pDef->bOnePerAccount)
	{
		PERFINFO_AUTO_START("Once per account check", 1);
		if(eaIndexedGetUsingString(&pAccountData->eaAllPurchases, pDefName))
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAIL: Tried to purchase a \"one per account\" microtrans %s but they've already purchased it once.", pDef->pchName);
			PERFINFO_AUTO_STOP();
			return bReturnValue;
		}
		PERFINFO_AUTO_STOP();
	}
	else if(pDef->bOnePerCharacter)
	{
		PERFINFO_AUTO_START("Once per character check", 1);
		if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		{
			TRANSACTION_APPEND_LOG_FAILURE("FAIL: Tried to purchase a \"one time purchase\" microtrans %s without an entity.", pDef->pchName);
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return bReturnValue;
		}
		else
		{
			S32 iIdx;
			if(ISNULL(pEnt->pPlayer->pMicroTransInfo))
			{
				TRANSACTION_APPEND_LOG_FAILURE("Player had no micro transaction information!");
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return bReturnValue;
			}

			for(iIdx = eaSize(&pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases)-1; iIdx >= 0; iIdx--)
			{
				if(stricmp(pDef->pchName,pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases[iIdx]) == 0)
					break;

				//Also support the legacy itemID for migrated version 2 microtransactions
				if(pDef->pchLegacyItemID 
					&& stricmp(pDef->pchLegacyItemID,pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases[iIdx]) == 0)
					break;
			}

			if(iIdx >= 0)
			{
				TRANSACTION_APPEND_LOG_FAILURE("Had already purchased %s and it's a one time purchase.", pDef->pchName);
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return bReturnValue;
			}
			else
			{
				eaPush(&pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases, StructAllocString(pDef->pchName));
			}
		}
		PERFINFO_AUTO_STOP();
	}

	for(iPartIdx = eaSize(&pDef->eaParts)-1; iPartIdx >= 0; iPartIdx--)
	{
		MicroTransGrantPartResult eResult = entity_trh_MicroTransactionDef_GrantPart(ATR_PASS_ARGS, pEnt, eaPets, pAccountData, &pExtract, pRewards, pDef, iPartIdx, pReason);
		if(eResult == kMicroTransGrantPartResult_Failed)
		{
			const char *pchDebugName = "null";
			if(NONNULL(pEnt))
			{
				pchDebugName = pEnt->debugName;
			}
			TRANSACTION_APPEND_LOG_FAILURE("Entity [%s] Account [%d] failed to purchase MicroTransaction [%s] part [%d:%s]",
				pchDebugName,
				pAccountData->iAccountID,
				pDef->pchName,
				iPartIdx,
				StaticDefineIntRevLookup(MicroPartTypeEnum, pDef->eaParts[iPartIdx]->ePartType));
			//Fail it here
			bReturnValue = false;
			break;
		}
		if (eResult == kMicroTransGrantPartResult_Success)
		{
			bReturnValue = true;
		}
	}

	if (pExtract)
		entity_trh_DestroyShallowGameAccountDataExtract(&pExtract);
	PERFINFO_AUTO_STOP();

	return bReturnValue;
}

AUTO_TRANS_HELPER;
void trhMicroTransDef_AddPurchaseStamp(ATH_ARG NOCONST(GameAccountData) *pAccountData,
									   const MicroTransactionDef *pDef,
									   const char *pDefName,
									   const AccountProxyProduct *pProduct,
									   const char *pCurrency)
{
	NOCONST(MicroTransaction) *pPurchase = eaIndexedGetUsingString(&pAccountData->eaAllPurchases, pDefName);
	NOCONST(MTPurchaseStamp) *pStamp = NULL;
	int iPriceIdx;

	// If GAD modification is not allowed, just early out without bothering to add the stamp
	if (gConf.bDontAllowGADModification)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	if(!pPurchase)
	{
		pPurchase = StructCreateNoConst(parse_MicroTransaction);
		pPurchase->pchName = allocAddString(pDefName);
		eaIndexedPushUsingStringIfPossible(&pAccountData->eaAllPurchases, pDefName, pPurchase);
	}

	pStamp = StructCreateNoConst(parse_MTPurchaseStamp);
	pStamp->uiPurchaseTime = timeSecondsSince2000();
	pStamp->iVersion = 0; //TODO(BH): pDef->iVersion;
	if(pProduct && pCurrency)
	{
		for(iPriceIdx = eaSize(&pProduct->ppMoneyPrices)-1; iPriceIdx >= 0; iPriceIdx--)
		{
			if(	pProduct->ppMoneyPrices[iPriceIdx]
				&& !isRealCurrency(moneyCurrency(pProduct->ppMoneyPrices[iPriceIdx]))
				&& stricmp(moneyCurrency(pProduct->ppMoneyPrices[iPriceIdx]), pCurrency) == 0)
			{
				pStamp->iCost = moneyCountPoints(pProduct->ppMoneyPrices[iPriceIdx]);
			}
		}
	}

	eaPush(&pPurchase->ppPurchaseStamps, pStamp);
	PERFINFO_AUTO_STOP();
}


#include "AutoGen/Microtransactions_Transact_h_ast.c"