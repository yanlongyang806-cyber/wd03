#include "accountnet.h"
#include "Alerts.h"
#include "AutoTransDefs.h"
#include "Character.h"
#include "entCritter.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "EntityLib.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountData_h_ast.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionsCommon_h_ast.h"
#include "GamePermissionTransactions.h"
#include "GameStringFormat.h"
#include "gslGameAccountData.h"
#include "gslGameAccountData_h_ast.h"
#include "LoggedTransactions.h"
#include "gslMicroTransactions.h"
#include "gslPowerTransactions.h"
#include "microtransactions_common.h"
#include "MicroTransactions.h"
#include "Microtransactions_Transact.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "logging.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "Powers.h"
#include "objTransactions.h"
#include "StringCache.h"
#include "accountnet_h_ast.h"
#include "file.h"

#include "autogen/AppServerLib_autogen_remotefuncs.h"
#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "autogen/ServerLib_autogen_remotefuncs.h"

//Global game account data permissions
GADAttribPermission **g_eaGADPermissions = NULL;

//////////////////////////////////////////
// Game Account Data Permissions
//////////////////////////////////////////

AUTO_STARTUP(GameAccount);
void gslGAD_LoadPermissions(void)
{
	GADAttribPermissions pPermissions = {0};
	S32 i;

	loadstart_printf("Loading Game Account Data Permissions... ");

	ParserLoadFiles(NULL, "defs/config/GameAccountPermissions.def", "GameAccountPermissions.bin", PARSER_OPTIONALFLAG, parse_GADAttribPermissions, &pPermissions);

	for(i = eaSize(&pPermissions.eaPermissions)-1; i >= 0; i--)
	{
		//Everything is server access
		pPermissions.eaPermissions[i]->ePermission |= kGADPermission_Server;
		
		eaPush(&g_eaGADPermissions, eaRemoveFast(&pPermissions.eaPermissions, i));
		eaIndexedEnable(&g_eaGADPermissions, parse_GADAttribPermission);
	}

	StructDeInit(parse_GADAttribPermissions, &pPermissions);

	loadend_printf(" done (%d Permissions).", eaSize(&g_eaGADPermissions));
}

static U32 GADValidatePermission(const char *pchAttrib, GADPermission eCallerPermission)
{
	GADAttribPermission *pPermission = eaIndexedGetUsingString(&g_eaGADPermissions, pchAttrib);
	if(!pPermission)
	{
		return(eCallerPermission & kGADPermission_Server);
	}
	else
	{
		return(eCallerPermission & pPermission->ePermission);
	}
}

/////////////////////////////////////////////////////////
// Vanity Pet Unlock
/////////////////////////////////////////////////////////

typedef struct GADCBStruct
{
	S32 iAccountID;
	EntityRef iRef;
} GADCBStruct;

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void entityCmd_VanityPetUnlocked(CmdContext* context, const char *pchName)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if ( pEnt &&
		pEnt->pPlayer )
	{
		char * tmpS = NULL;
		PowerDef *pDef = powerdef_Find(pchName);
		estrStackCreate(&tmpS);
		if(pDef)
		{
			entFormatGameMessageKey(pEnt, &tmpS, "Item.UI.VanityPetUnlock", 
				STRFMT_DISPLAYMESSAGE("PetName", pDef->msgDisplayName),
				STRFMT_END);
		}
		else
		{
			entFormatGameMessageKey(pEnt, &tmpS, "Item.UI.VanityPetUnlock", 
			STRFMT_STRING("PetName", pchName),
			STRFMT_END);
		}
		notify_NotifySend(pEnt, kNotifyType_VanityPetUnlocked, tmpS, NULL, NULL);
		estrDestroy(&tmpS);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void entityCmd_DisplayMicroTransRewards(CmdContext* context, MicroTransactionRewards* pRewards)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if (pEnt && pEnt->pPlayer && pRewards)
	{
		InvRewardRequest Request = {0};
		S32 i;
		for (i = 0; i < eaSize(&pRewards->eaRewards); i++)
		{
			MicroTransactionPartRewards *pPartRewards = pRewards->eaRewards[i];
			inv_FillRewardRequest(pPartRewards->eaBags, &Request);
		}

		ClientCmd_gclMicroTrans_RecvRewards(pEnt, &Request);
		StructDeInit(parse_InvRewardRequest, &Request);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Pchar.Pppowerspersonal, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Pchar.Ilevelexp") 
ATR_LOCKS(pAccountData, ".Iversion, .Eavanitypets, .Eakeys, .Eatokens, .Eacostumekeys, .Idayssubscribed, .Blinkedaccount, .Bshadowaccount, .Eaaccountkeyvalues");
enumTransactionOutcome item_ent_tr_UnlockVanityPets(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(GameAccountData) *pAccountData, ItemDef *pItemDef, U64 ulItemID, const ItemChangeReason *pReason)
{
	S32 iPetIdx;
	bool bUnlockedSomething = false;
	GameAccountDataExtract *pExtract;

	for(iPetIdx = eaSize(&pItemDef->ppItemVanityPetRefs)-1; iPetIdx >= 0; iPetIdx--)
	{
		ItemVanityPet *pPet = pItemDef->ppItemVanityPetRefs[iPetIdx];
		PowerDef *pPowerDef = pPet ? GET_REF(pPet->hPowerDef) : NULL;

		if(NONNULL(pPet) && NONNULL(pPowerDef))
		{
			if(trhMicroTrans_UnlockVanityPet(ATR_PASS_ARGS, pAccountData, pPowerDef))
			{
				bUnlockedSomething = true;

				trCharacter_AddPowerPersonal(ATR_RECURSE, pEnt, pPowerDef, 0, false);
				
				QueueRemoteCommand_entityCmd_VanityPetUnlocked(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pPowerDef->pchName);
				
				TRANSACTION_APPEND_LOG_SUCCESS("EntityPlayer[%d] Unlocked vanity pet %s",
					pEnt->myContainerID, pPowerDef->pchName);
			}
		}
	}

	pExtract = entity_trh_CreateGameAccountDataExtract(pAccountData);
	if(!bUnlockedSomething)
	{
		entity_trh_DestroyGameAccountDataExtract(&pExtract);
		TRANSACTION_RETURN_LOG_FAILURE("EntityPlayer[%d] Item %"FORM_LL"d has no vanity pets this player can unlock.",
			pEnt->myContainerID, ulItemID);
	}
	else if(!inv_RemoveItemByID(ATR_PASS_ARGS, pEnt, ulItemID, 1, 0, pReason, pExtract))
	{
		entity_trh_DestroyGameAccountDataExtract(&pExtract);
		TRANSACTION_RETURN_LOG_FAILURE("EntityPlayer[%d] Unable to remove item by id %"FORM_LL"d",
				pEnt->myContainerID, ulItemID);
	}
	else
	{
		entity_trh_DestroyGameAccountDataExtract(&pExtract);
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
}

void gslGAD_UnlockVanityPet(Entity *pEnt, ItemDef *pItemDef, U64 ulItemID)
{
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerAccountData &&
		pItemDef && pItemDef->eType == kItemType_VanityPet)
	{
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Pets:UnlockVanityPet", NULL);

		AutoTrans_item_ent_tr_UnlockVanityPets(LoggedTransactions_MakeEntReturnVal("ItemVanityPet", pEnt), 
			GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), 
			GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), pItemDef, ulItemID,
			&reason);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.Pppowerspersonal, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets")
ATR_LOCKS(pData, ".Eavanitypets");
bool gslGAD_trh_GrantEachVanityPet(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG  NOCONST(GameAccountData) *pData)
{
	bool bFailed = false;

	if(NONNULL(pEnt) &&
		NONNULL(pEnt->pChar) &&
		NONNULL(pEnt->pPlayer) && 
		NONNULL(pEnt->pPlayer->pPlayerAccountData) )
	{
		S32 iPetIdx;
		for(iPetIdx = eaSize(&pData->eaVanityPets)-1; iPetIdx >= 0; iPetIdx--)
		{
			PowerDef *pDef = powerdef_Find(pData->eaVanityPets[iPetIdx]);
			if(pDef)
			{
				trCharacter_AddPowerPersonal(ATR_RECURSE, pEnt, pDef, 0, false);
			}
			else
				bFailed = true;
		}
	}

	return(bFailed);
}

/////////////////////////////////////////////////////////
// GameAccount Version Processing
/////////////////////////////////////////////////////////

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pData, ".Eakeys, .Iversion, .Eavanitypets, .Eacostumekeys");
bool gslGAD_trh_CommitPendingChanges(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(GameAccountData) *pData)
{
	if(NONNULL(pEnt) &&
		NONNULL(pEnt->pPlayer) &&
		NONNULL(pData))
	{
		S32 iCount = 0;
		char *pchItem = NULL;
		char *pchGameTitle = NULL;
		MicroItemType eType = kMicroItemType_None;
		char *pchItemName = NULL;
		bool bSuccess = false;
		S32 iIdx;

		estrStackCreate(&pchItem);
		
		for(iIdx = eaSize(&pEnt->pPlayer->pPlayerAccountData->eaPendingKeys)-1; iIdx >= 0; iIdx--)
		{
			AttribValuePair *pPair = CONTAINER_RECONST(AttribValuePair, eaGet(&pEnt->pPlayer->pPlayerAccountData->eaPendingKeys, iIdx));
			if(!pPair || !pPair->pchValue)
				continue;

			estrCopy2(&pchItem, pPair->pchAttribute);
			
			iCount = MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName);

			if(  !pchGameTitle || 
				 stricmp(GetShortProductName(), pchGameTitle) )
			{
				continue;
			}

			//If its not a standard style key, then just set the attrib in the main field
			if(!pchItemName ||
				eType <= kMicroItemType_None)
			{
				//Increment operation
				if(pPair->pchValue && 
					(pPair->pchValue[0] == '-' ||
					pPair->pchValue[0] == '+') )
				{
					S32 iValue = atoi(pPair->pchValue);
					if( slGAD_trh_ChangeAttrib(ATR_PASS_ARGS, pData, pPair->pchAttribute, iValue) )
					{
						bSuccess = true;
					}
				}
				//Set operation
				else 
				{
					if( slGAD_trh_SetAttrib(ATR_PASS_ARGS, pData, pPair->pchAttribute, pPair->pchValue) == TRANSACTION_OUTCOME_SUCCESS )
					{
						bSuccess = true;
					}
				}
			}
			else
			{
				switch(eType)
				{
				case kMicroItemType_VanityPetUnlock:
					{
						PowerDef *pPowerDef = powerdef_Find(pchItemName);
						if(pPowerDef && trhMicroTrans_UnlockVanityPet(ATR_PASS_ARGS, pData, pPowerDef))
						{
							bSuccess = true;
							TRANSACTION_APPEND_LOG_SUCCESS("EntityPlayer[%d] Unlocked vanity pet %s",
								pEnt->myContainerID, pPowerDef->pchName);
						}
						break;
					}
				case kMicroItemType_PlayerCostume:
					{
						PlayerCostume *pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchItemName);
						if(!pCostume)
							break;

						if(trhMicroTrans_UnlockCostumeRef(ATR_PASS_ARGS, pEnt, pData, pCostume))
						{
							bSuccess = true;
						}
						break;
					}
				case kMicroItemType_Costume:
					{
						ItemDef *pItemDef = item_DefFromName(pchItemName);
						if(!pItemDef)
							break;

						if(trhMicroTrans_UnlockCostume(ATR_PASS_ARGS, pEnt, pData, pItemDef))
						{
							bSuccess = true;
						}
						break;
					}
				default:
					{
						if( slGAD_trh_SetAttrib(ATR_PASS_ARGS, pData, pPair->pchAttribute, pPair->pchValue) == TRANSACTION_OUTCOME_SUCCESS )
						{
							bSuccess = true;
						}
					}
					break;
				}
			}
		}

		//Destroy the temporary estring
		estrDestroy(&pchItem);

		//clear the pending keys
		eaClearStructNoConst(&pEnt->pPlayer->pPlayerAccountData->eaPendingKeys, parse_AttribValuePair);

		return(bSuccess);
	}
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccountData, ".Eakeys, .Eacostumekeys");
bool gslGAD_trh_GrantEachItem(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pAccountData)
{
	S32 iAttribIdx, iAttribSize = eaSize(&pAccountData->eaKeys);
	S32 iCount = 0;
	char *pchItem = NULL;
	char *pchGameTitle = NULL;
	MicroItemType eType = kMicroItemType_None;
	char *pchItemName = NULL;
	bool bUnlockedSomething = false;

	estrStackCreate(&pchItem);

	for(iAttribIdx = 0; iAttribIdx < iAttribSize; iAttribIdx++)
	{
		AttribValuePair *pPair = CONTAINER_RECONST(AttribValuePair, eaGet(&pAccountData->eaKeys, iAttribIdx));
		if(!pPair || !pPair->pchValue || atoi(pPair->pchValue) <= 0)
			continue;

		estrCopy2(&pchItem, pPair->pchAttribute);
		
		iCount = MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName);

		if(  !pchGameTitle || 
			 !pchItemName || 
			 eType == kMicroItemType_None || 
			 iCount <= 0 ||
			 stricmp(GetShortProductName(), pchGameTitle) )
		{
			continue;
		}

		switch(eType)
		{
		default:
			break;
		case kMicroItemType_Costume:
			{
				ItemDef *pDef = item_DefFromName(pchItemName);
				if(pDef)
				{
					S32 iCostumeIdx;
					for(iCostumeIdx = eaSize(&pDef->ppCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
					{
						char *pchCostumeRef = NULL;
						NOCONST(AttribValuePair) *pCostumePair = NULL;
						MicroTrans_FormItemEstr(&pchCostumeRef, 
							GetShortProductName(), kMicroItemType_PlayerCostume, 
							REF_STRING_FROM_HANDLE(pDef->ppCostumes[iCostumeIdx]->hCostumeRef), 1);

						pCostumePair = eaIndexedGetUsingString(&pAccountData->eaCostumeKeys, pchCostumeRef);
						if(ISNULL(pCostumePair))
						{
							RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);
							pCostumePair = StructCreateNoConst(parse_AttribValuePair);
							pCostumePair->pchAttribute = StructAllocString(pchCostumeRef);
							pCostumePair->pchValue = StructAllocString("1");
							eaPush(&pAccountData->eaCostumeKeys, pCostumePair);

							bUnlockedSomething = true;
						}

						estrDestroy(&pchCostumeRef);
					}
				}
				break;
			}
		}
	}

	estrDestroy(&pchItem);

	return bUnlockedSomething;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata, .Psaved.Ppallowedcritterpets, .Pchar.Pppowerspersonal, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers")
ATR_LOCKS(pAccountData, ".Iversion, .Eakeys, .Eavanitypets, .Eacostumekeys");
enumTransactionOutcome gslGAD_tr_ProcessNewVersion(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData) *pAccountData, int bForceProcess)
{	
	if(NONNULL(pEnt) &&
		NONNULL(pEnt->pPlayer) && 
		NONNULL(pAccountData))
	{
		NOCONST(PlayerAccountData) *pPlayerData = pEnt->pPlayer->pPlayerAccountData;
		
		//Check to see if I have any pending keys the need to be pushed to the game account data
		if(eaSize(&pPlayerData->eaPendingKeys))
		{
			// If I do, push them into the game account data
			if(gslGAD_trh_CommitPendingChanges(ATR_PASS_ARGS, pEnt, pAccountData))
			{
				//On success, update the version
				pAccountData->iVersion++;
			}
		}
		
		//Now do the full key-value pairs (including anything new from the pending changes)
		if(pPlayerData->iVersion != pAccountData->iVersion || bForceProcess)
		{
			gslGAD_trh_GrantEachItem(ATR_PASS_ARGS, pAccountData);

			gslGAD_trh_GrantEachVanityPet(ATR_PASS_ARGS, pEnt, pAccountData);
			pPlayerData->iVersion = pAccountData->iVersion;
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

/////////////////////////////////////////////////////////
// GameAccount Unlocks from the AccountServer key-value pairs
/////////////////////////////////////////////////////////

static void AccountUnlock_CB(TransactionReturnVal *pReturn, GADCBStruct *pCBStruct)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pCBStruct->iRef);

	if( pEnt &&
		pEnt->pChar &&
		pEnt->pSaved &&
		pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
		entity_SetDirtyBit(pEnt, parse_PlayerCostumeData, &pEnt->pSaved->costumeData, false);
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
	}

	SAFE_FREE(pCBStruct);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Iversion")
ATR_LOCKS(pAccountData, ".Iversion, .Eacostumekeys, .Eavanitypets");
enumTransactionOutcome gslGAD_tr_AccountProcessUnlocks(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData) *pAccountData, AccountProxyKeyValueInfoList *pList )
{
	char *pchItem = NULL;
	estrStackCreate(&pchItem);
	
	EARRAY_CONST_FOREACH_BEGIN(pList->ppList, i, s);
	AccountProxyKeyValueInfo *pInfo = eaGet(&pList->ppList, i);
	if(pInfo && pInfo->pValue && atoi(pInfo->pValue) > 0)
	{
		char *pchGameTitle = NULL;
		MicroItemType eType = kMicroItemType_None;
		char *pchItemName = NULL;
		S32 iCount;
		
		estrCopy2(&pchItem, pInfo->pKey);
		
		iCount = MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName);

		if(  !pchGameTitle || 
			 !pchItemName ||
			 !iCount ||
			 stricmp(GetShortProductName(), pchGameTitle) )
		{
			continue;
		}

		
		switch(eType)
		{
		default:
			break;
		case kMicroItemType_PlayerCostume:
			{
				PlayerCostume *pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchItemName);
				if(!pCostume)
					break;
				trhMicroTrans_UnlockCostumeRef_Force(ATR_PASS_ARGS, pEnt, pAccountData, pCostume);
				break;
			}
		case kMicroItemType_Costume:
			{
				ItemDef *pItemDef = item_DefFromName(pchItemName);
				if(!pItemDef)
					break;
				trhMicroTrans_UnlockCostume_Force(ATR_PASS_ARGS, pEnt, pAccountData, pItemDef);
				break;
			}
		case kMicroItemType_VanityPetUnlock:
			{
				PowerDef *pPowerDef = powerdef_Find(pchItemName);
				if(!pPowerDef)
					break;
				trhMicroTrans_UnlockVanityPet_Force(ATR_PASS_ARGS, pAccountData, pPowerDef);
				break;
			}
		}
	}
	EARRAY_FOREACH_END;

	estrDestroy(&pchItem);

	return TRANSACTION_OUTCOME_SUCCESS;
}


static void AddPurchase_CB(TransactionReturnVal *pReturn, GADCBStruct *pCBStruct)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEntity = entFromEntityRefAnyPartition(pCBStruct->iRef);
		if(pEntity && pEntity->pPlayer)
		{
			entity_SetDirtyBit(pEntity, parse_PlayerMTInfo, pEntity->pPlayer->pMicroTransInfo, false);
			entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
		}
	}
	SAFE_FREE(pCBStruct);
}

static bool gslGAD_IsPotentialUnlock(SA_PARAM_NN_VALID Entity* pEnt, MicroItemType eType, 
									 const char* pchProduct, const char* pchItemName)
{
	GameAccountData* pAccountData = NULL;
	
	if (pEnt->pPlayer && pEnt->pPlayer->pPlayerAccountData)
	{
		pAccountData = GET_REF(pEnt->pPlayer->pPlayerAccountData->hData);
	}
	if (!pAccountData)
	{
		switch (eType)
		{
			case kMicroItemType_PlayerCostume:
			case kMicroItemType_VanityPetUnlock:
			{
				return true;
			}
		}
		return false;
	}

	switch (eType)
	{
		case kMicroItemType_PlayerCostume:
		{
			AttribValuePair* pAttrib = eaIndexedGetUsingString(&pAccountData->eaCostumeKeys, pchProduct);
			return (!pAttrib || !pAttrib->pchValue || atoi(pAttrib->pchValue) <= 0);
		}
		case kMicroItemType_VanityPetUnlock:
		{
			S32 iIdx = eaFindString((char***)&pAccountData->eaVanityPets, pchItemName);
			return (iIdx < 0);
		}
	}
	return false;
}

static void GetKeys_CB(TransactionReturnVal *pReturn, GADCBStruct *pCBStruct)
{
	AccountProxyKeyValueInfoList *pList = NULL;
	assert(pCBStruct);
	switch(RemoteCommandCheck_aslAPCmdGetAllKeyValues(pReturn, &pList))
	{
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			Entity *pEnt = entFromEntityRefAnyPartition(pCBStruct->iRef);
			if(pEnt && pEnt->pPlayer && pList)
			{
				char *pchItem = NULL;
				char *pchGameTitle = NULL;
				MicroItemType eType = kMicroItemType_None;
				char *pchItemName = NULL;
				S32 iKeyIdx, iCount = 0;
				S32 bNotify = false;
				char **eaMessageKeys = NULL;
				PlayerMTInfo *pMicroInfo = pEnt->pPlayer->pMicroTransInfo;
				AccountProxyKeyValueInfoList *pUnlockList = StructCreate(parse_AccountProxyKeyValueInfoList);

				estrStackCreate(&pchItem);

				//Send the cache back to the user so they can see them
				ClientCmd_gclAPCmdCacheSetAllKeyValues(pEnt, pList);
				
				//Go through all the account's keys
				for(iKeyIdx = eaSize(&pList->ppList)-1;iKeyIdx >= 0; iKeyIdx--)
				{
					AccountProxyKeyValueInfo *pInfo = eaGet(&pList->ppList, iKeyIdx);

					//Only interested if it's non-null and > 0 value
					if(pInfo && pInfo->pValue &&
						atoi(pInfo->pValue) > 0)
					{
						//Parse the key
						estrCopy2(&pchItem ,pInfo->pKey);

						iCount = MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName);
						
						//Is it a special key that we haven't seen before?
						if( MicroTrans_IsSpecialKey(pInfo->pKey) && 
							eaFindString((char***)(&pMicroInfo->eaOneTimePurchases), pInfo->pKey) < 0)
						{
							GADCBStruct *pNewCBStruct = calloc(1, sizeof(GADCBStruct));
							TransactionReturnVal *pVal = objCreateManagedReturnVal(AddPurchase_CB, pNewCBStruct);
							const char *pchSpecialMesgKey = MicroTrans_SpecialKeyMesg(pInfo->pKey);
							
							memcpy(pNewCBStruct, pCBStruct, sizeof(GADCBStruct));

							AutoTrans_entity_tr_AddOneTimePurchase(pVal, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), pInfo->pKey);
							bNotify = true;
							
							if(pchSpecialMesgKey)
								eaPush(&eaMessageKeys, strdup(pchSpecialMesgKey));
						}

						if(  !iCount || !pchGameTitle || !pchItemName || 
							 stricmp(GetShortProductName(), pchGameTitle) )
						{
							continue;
						}

						if (gslGAD_IsPotentialUnlock(pEnt, eType, pInfo->pKey, pchItemName))
						{
							eaPush(&pUnlockList->ppList, StructClone(parse_AccountProxyKeyValueInfo, pInfo));
						}
					}
				}
				estrDestroy(&pchItem);

				//If we found a vanity pet or costume item/ref to unlock, run the unlock transaction
				if(eaSize(&pUnlockList->ppList))
				{
					GADCBStruct *pUnlockCBStruct = calloc(1, sizeof(GADCBStruct));
					memcpy(pUnlockCBStruct, pCBStruct, sizeof(GADCBStruct));
					AutoTrans_gslGAD_tr_AccountProcessUnlocks(LoggedTransactions_CreateManagedReturnValEnt("GameAccount_UnlockAll",pEnt, AccountUnlock_CB, pUnlockCBStruct), 
						GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), 
						GLOBALTYPE_GAMEACCOUNTDATA, pCBStruct->iAccountID, pUnlockList );
				}

				//If we found a special key we hadn't seen, send the notify message
				if(bNotify && eaSize(&eaMessageKeys))
				{
					S32 iMsgIdx;

					for(iMsgIdx = eaSize(&eaMessageKeys)-1; iMsgIdx >= 0; iMsgIdx--)
					{
						char *pchMesg = NULL;
						
						entFormatGameMessageKey(pEnt, &pchMesg, eaMessageKeys[iMsgIdx],
							STRFMT_END);

						notify_NotifySend(pEnt, kNotifyType_MicroTrans_SpecialItems, pchMesg, NULL, NULL);
						
						estrDestroy(&pchMesg);
					}
					
					eaDestroyEx(&eaMessageKeys, NULL);
				}

				//Get rid of the unlock list
				StructDestroySafe(parse_AccountProxyKeyValueInfoList, &pUnlockList);

			}
			break;
		}
	default:
		break;
	}
	//Destroy the list
	StructDestroySafe(parse_AccountProxyKeyValueInfoList, &pList);

	SAFE_FREE(pCBStruct);
}

/////////////////////////////////////////////////////////
// Setting the reference to the game account data
/////////////////////////////////////////////////////////

//This transaction just sets up the reference on the entity for the game account data container.
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Accountid, .Pplayer.Pplayeraccountdata.Iaccountid, .Pplayer.Pplayeraccountdata.Hdata");
enumTransactionOutcome gslGAD_tr_SetReference(ATR_ARGS, NOCONST(Entity) *pEnt)
{
	if(NONNULL(pEnt) &&
		NONNULL(pEnt->pPlayer) )
	{
		char idBuf[128];
		ContainerID iContID = pEnt->pPlayer->accountID;
		pEnt->pPlayer->pPlayerAccountData->iAccountID = pEnt->pPlayer->accountID;
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), ContainerIDToString(iContID, idBuf), pEnt->pPlayer->pPlayerAccountData->hData);
	}
	return(TRANSACTION_OUTCOME_SUCCESS);
}

/////////////////////////////////////////////////////////
// Migration code for EntitySavedData -> GameAccountData
/////////////////////////////////////////////////////////

static void AccountCostumeUnlock_CB(TransactionReturnVal *pReturn, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pEnt = entFromEntityRefAnyPartition(*pRef);
	if(pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		/*
		char *pchMesg = NULL;

		entFormatGameMessageKey(pEnt, &pchMesg,  "CostumeUnlock_AccountWide",
			STRFMT_END);

		notify_NotifySend(pEnt, kNotifyType_CostumeUnlocked, pchMesg, NULL, NULL);

		estrDestroy(&pchMesg);
		*/

		entity_SetDirtyBit(pEnt, parse_PlayerCostumeData, &pEnt->pSaved->costumeData, false);
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
	}
	SAFE_FREE(pRef);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Eaunlockedcostumerefs")
ATR_LOCKS(pAccountData, ".Eacostumekeys");
enumTransactionOutcome gslGAD_tr_AccountUnlockCostumes(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(GameAccountData) *pAccountData)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);

	if(NONNULL(pEnt) &&
		NONNULL(pEnt->pSaved) &&
		NONNULL(pAccountData))
	{
		S32 iUnlocked = 0;
		S32 iCostumeIdx;
		for(iCostumeIdx = eaSize(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs)-1; iCostumeIdx >= 0; iCostumeIdx--)
		{
			NOCONST(PlayerCostumeRef) *pCostumeRef = pEnt->pSaved->costumeData.eaUnlockedCostumeRefs[iCostumeIdx];
			PlayerCostume *pCostume = pCostumeRef ? GET_REF(pCostumeRef->hCostume) : NULL;

			if(NONNULL(pCostume) && pCostume->bAccountWideUnlock)
			{
				NOCONST(AttribValuePair) *pPair = NULL;
				char *pchCostumeName = NULL;
				estrStackCreate(&pchCostumeName);

				MicroTrans_FormItemEstr(&pchCostumeName, GetShortProductName(), kMicroItemType_PlayerCostume, pCostume->pcName, 1);

				//Destroy and remove the old costume reference
				eaRemove(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs, iCostumeIdx);
				StructDestroyNoConst(parse_PlayerCostumeRef, pCostumeRef);

				//Now, add in the attrib value pair to the costume ref list on the game account data	
				pPair = eaIndexedGetUsingString(&pAccountData->eaCostumeKeys, pchCostumeName);
				if(ISNULL(pPair))
				{
					pPair = StructCreateNoConst(parse_AttribValuePair);
					pPair->pchAttribute = StructAllocString(pchCostumeName);
					pPair->pchValue = StructAllocString("1");
					eaPush(&pAccountData->eaCostumeKeys, pPair);

					TRANSACTION_APPEND_LOG_SUCCESS("Successfully migrated a costume '%s' from EntitySavedData to GameAccount key '%s'",
						pCostume->pcName,
						pchCostumeName);
				}
				else
				{
					TRANSACTION_APPEND_LOG_SUCCESS("Removed an already migrated a costume '%s' from EntitySavedData",
						pCostume->pcName);
				}

				iUnlocked++;
				
				estrDestroy(&pchCostumeName);
			}
		}
		if(iUnlocked > 0)
		{
			TRANSACTION_RETURN_LOG_SUCCESS("EntityPlayer[%d] moved [%d] account-wide costumes from EntitySavedData to GameAccountData.",
			pEnt->myContainerID, iUnlocked);
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("EntityPlayer[%d] had no account-wide costumes to move from EntitySavedData to GameAccountData.",
				pEnt->myContainerID);
		}
	}

	TRANSACTION_RETURN_LOG_FAILURE("Invalid entity, entity saved data or game account data.");
}

/////////////////////////////////////////////////////////
// Main "Do Stuff" to GameAccount at login
/////////////////////////////////////////////////////////

S32 gslGAD_CostumeFixupNeeded(GameAccountData *pData)
{
	bool bUnlockedSomething = false;
	
	if(pData)
	{
		S32 iAttribIdx, iAttribSize = eaSize(&pData->eaKeys);
		S32 iCount = 0;
		char *pchItem = NULL;
		char *pchGameTitle = NULL;
		MicroItemType eType = kMicroItemType_None;
		char *pchItemName = NULL;
		

		estrStackCreate(&pchItem);

		for(iAttribIdx = 0; iAttribIdx < iAttribSize && !bUnlockedSomething; iAttribIdx++)
		{
			AttribValuePair *pPair = eaGet(&pData->eaKeys, iAttribIdx);
			if(!pPair || !pPair->pchValue || atoi(pPair->pchValue) <= 0)
				continue;

			estrCopy2(&pchItem, pPair->pchAttribute);

			iCount = MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName);

			if(  !pchGameTitle || 
				!pchItemName || 
				eType == kMicroItemType_None || 
				iCount <= 0 ||
				stricmp(GetShortProductName(), pchGameTitle) )
			{
				continue;
			}

			switch(eType)
			{
			default:
				break;
			case kMicroItemType_Costume:
				{
					ItemDef *pDef = item_DefFromName(pchItemName);
					if(pDef)
					{
						S32 iCostumeIdx;
						for(iCostumeIdx = eaSize(&pDef->ppCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
						{
							char *pchCostumeRef = NULL;
							MicroTrans_FormItemEstr(&pchCostumeRef, 
								GetShortProductName(), kMicroItemType_PlayerCostume, 
								REF_STRING_FROM_HANDLE(pDef->ppCostumes[iCostumeIdx]->hCostumeRef), 1);

							if(!eaIndexedGetUsingString(&pData->eaCostumeKeys, pchCostumeRef))
							{
								bUnlockedSomething = true;
							}

							estrDestroy(&pchCostumeRef);

							if(bUnlockedSomething)
								break;
						}
					}
					break;
				}
			}
		}

		estrDestroy(&pchItem);
	}

	return bUnlockedSomething;
}
void gslGAD_ProcessNewVersion(Entity *pEnt, bool bForceProcess, bool bInitialLogin)
{
	if(!pEnt || !pEnt->pPlayer)
		return;

	//Run the transaction if it's the initial login or there's a call to force a reprocess event (Usually costume key fix up)
	if(bForceProcess || bInitialLogin)
	{
		TransactionReturnVal *pVersionReturn = LoggedTransactions_MakeEntReturnVal("Process_GameAccountData", pEnt);
		AutoTrans_gslGAD_tr_ProcessNewVersion(pVersionReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID, bForceProcess);
	}
}

void gslGAD_FixupGameAccount(Entity *pEnt, bool bInitialLogin)
{
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->accountID > 0)
	{
		int bForceProcess = false;

		PERFINFO_AUTO_START_FUNC();

		if (bInitialLogin)
		{
			GADCBStruct *pCBStruct = calloc(1,sizeof(GADCBStruct));
			TransactionReturnVal *pNewReturn = objCreateManagedReturnVal_TransactionMayTakeALongTime(GetKeys_CB, pCBStruct);
			pCBStruct->iAccountID = pEnt->pPlayer->accountID;
			pCBStruct->iRef = entGetRef(pEnt);

			//Get all the keys
			RemoteCommand_aslAPCmdGetAllKeyValues(pNewReturn, GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pEnt->pPlayer->accountID, entGetContainerID(pEnt));
		}

		if(IS_HANDLE_ACTIVE(pEnt->pPlayer->pPlayerAccountData->hData))
		{
			bForceProcess = gslGAD_CostumeFixupNeeded(GET_REF(pEnt->pPlayer->pPlayerAccountData->hData));
		}

		//Process the new version of the game account data
		
		if (bInitialLogin || bForceProcess)
		{
			gslGAD_ProcessNewVersion(pEnt, bForceProcess, bInitialLogin);
		}

		//If I haven't set the reference, then set it
		if(entGetAccountID(pEnt) && 
			entGetAccountID(pEnt) != pEnt->pPlayer->pPlayerAccountData->iAccountID)
		{
			AutoTrans_gslGAD_tr_SetReference(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt) );
		}

		//Ports the costumes that have already been unlocked into the GameAccountData if they are now marked as account-wide unlocks
		if(pEnt && pEnt->pSaved && eaSize(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs))
		{
			bool bRun = false;
			S32 iCostumeIdx;
			for(iCostumeIdx = eaSize(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs)-1; iCostumeIdx >= 0; iCostumeIdx--)
			{
				PlayerCostume *pCostume = GET_REF(pEnt->pSaved->costumeData.eaUnlockedCostumeRefs[iCostumeIdx]->hCostume);
				if(pCostume && pCostume->bAccountWideUnlock)
				{
					bRun = true;
					break;
				}
			}

			if(bRun)
			{
				EntityRef *pRef = calloc(1, sizeof(EntityRef));
				TransactionReturnVal *pCostumeReturn = LoggedTransactions_CreateManagedReturnValEnt("MigrateAccountCostumes", pEnt, AccountCostumeUnlock_CB, pRef);
				*pRef = entGetRef(pEnt);

				AutoTrans_gslGAD_tr_AccountUnlockCostumes(pCostumeReturn, GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID);
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

/////////////////////////////////////////////////////////
// Server Command(s)
/////////////////////////////////////////////////////////

// Set the value associated with a GameAccountData attribute.
AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void gslGAD_SetAttrib(Entity *pEnt, const char *pchAttrib, const ACMD_SENTENCE pchValue)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerAccountData && pEnt->pPlayer->pPlayerAccountData->iAccountID)
	{
		if(!GADValidatePermission(pchAttrib, kGADPermission_Client))
		{
			// Noisy error in Dev mode so that it can be fixed, maybe.
			if (isDevelopmentMode())
			{
				Errorf("%s tried to set game account data %s to %s.  Not allowed to set that attribute.", ENTDEBUGNAME(pEnt), pchAttrib, pchValue);
			}
			entLog(LOG_GSL, pEnt, "SetAttribError", "Received invalid request to set game account data %s to %s.  Not allowed to set that attribute.", pchAttrib, pchValue);
		}
		else
		{
			entLog(LOG_GSL, pEnt, "SetAttribError", "Requesting transaction to set game account data %s to %s.", pchAttrib, pchValue);
			AutoTrans_slGAD_tr_SetAttrib(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA,
				pEnt->pPlayer->pPlayerAccountData->iAccountID, pchAttrib, pchValue);
		}
	}
	else if (pEnt && pEnt->pPlayer)
	{
		entLog(LOG_GSL, pEnt, "SetAttribError", "Requested setting game account data %s to %s, but this player has no account data.", pchAttrib, pchValue);
	}
	else if (pEnt)
	{
		entLog(LOG_GSL, pEnt, "SetAttribError", "Requested setting game account data %s to %s, but this is not a player.", pchAttrib, pchValue);
	}
}

//////////////////////////////////////////
// CSR COMMANDS
//////////////////////////////////////////

//TODO(BH): Add some
//TODO(BH): Validate Permissions

//////////////////////////////////////////
// DEBUGGING COMMANDS
//////////////////////////////////////////

AUTO_COMMAND ACMD_NAME("GameAccount_SetAttrib") ACMD_ACCESSLEVEL(9);
void gslGADCmd_SetAttrib(Entity *pEnt, const char *pchKey, ACMD_SENTENCE pchValue)
{
	if(pEnt && entGetAccountID(pEnt) > 0)
	{
		AutoTrans_slGAD_tr_SetAttrib(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), pchKey, pchValue);
	}
}

AUTO_COMMAND ACMD_NAME("GameAccount_UnlockCostumeItem") ACMD_ACCESSLEVEL(9);
void gslGADCmd_UnlockCostumeItem(Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchName)
{
	ItemDef *pDef = item_DefFromName(pchName);
	if(pDef && pEnt && entGetAccountID(pEnt) > 0)
	{
		if(eaSize(&pDef->ppCostumes))
		{
			int i;
			AccountProxyKeyValueInfoList *pUnlockList = StructCreate(parse_AccountProxyKeyValueInfoList);
			for(i=eaSize(&pDef->ppCostumes)-1; i>=0; i--)
			{
				AccountProxyKeyValueInfo *pPair = StructCreate(parse_AccountProxyKeyValueInfo);
				char *estrItem = NULL;
				const char *pchHandleName = REF_STRING_FROM_HANDLE(pDef->ppCostumes[i]->hCostumeRef);
				if(pchHandleName)
				{
					MicroTrans_FormItemEstr(&estrItem, GetShortProductName(), kMicroItemType_PlayerCostume, pchHandleName, 1);
					pPair->pKey = estrCreateFromStr(estrItem);
					pPair->pValue = estrCreateFromStr("1");
					eaPush(&pUnlockList->ppList, pPair);
				}
				else
				{
					StructDestroy(parse_AccountProxyKeyValueInfo, pPair);
				}
				estrDestroy(&estrItem);
			}
			
			AutoTrans_gslGAD_tr_AccountProcessUnlocks(LoggedTransactions_MakeEntReturnVal("GameAccount_UnlockAllDebug",pEnt), 
				GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), 
				GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt),  pUnlockList);
			
			StructDestroy(parse_AccountProxyKeyValueInfoList, pUnlockList);
			
		}
		else
		{
			notify_NotifySend(pEnt, kNotifyType_Failed, "No costumes on that item.", NULL, NULL);
		}
	}
}

AUTO_COMMAND ACMD_NAME("GameAccount_UnlockCostumeRef") ACMD_ACCESSLEVEL(9);
void gslGADCmd_UnlockCostumeRef(Entity *pEnt, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char *pchName)
{
	PlayerCostume *pDef = (PlayerCostume*)RefSystem_ReferentFromString(g_hPlayerCostumeDict,pchName);
	if(pDef && pEnt && entGetAccountID(pEnt) > 0)
	{
		AccountProxyKeyValueInfoList *pUnlockList = StructCreate(parse_AccountProxyKeyValueInfoList);
		AccountProxyKeyValueInfo *pPair = StructCreate(parse_AccountProxyKeyValueInfo);
		char *estrItem = NULL;
		MicroTrans_FormItemEstr(&estrItem, GetShortProductName(), kMicroItemType_PlayerCostume, pchName, 1);
		pPair->pKey = estrCreateFromStr(estrItem);
		pPair->pValue = estrCreateFromStr("1");
		eaPush(&pUnlockList->ppList, pPair);

		AutoTrans_gslGAD_tr_AccountProcessUnlocks(LoggedTransactions_MakeEntReturnVal("GameAccount_UnlockAllDebug",pEnt), 
			GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), 
			GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt),  pUnlockList);

		StructDestroy(parse_AccountProxyKeyValueInfoList, pUnlockList);
		estrDestroy(&estrItem);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Earecruits");
enumTransactionOutcome gslGAD_tr_AddRecruit(ATR_ARGS, NOCONST(GameAccountData) *pData, U32 iAccountID, const char *pchProductName, U32 uiTime)
{
	S32 idx;
	NOCONST(RecruitContainer) *pNewRecruit = NULL;
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	for(idx = eaSize(&pData->eaRecruits)-1; idx>=0; idx--)
	{
		NOCONST(RecruitContainer) *pRecruit = pData->eaRecruits[idx];
		if(stricmp(pRecruit->pProductInternalName, pchProductName) == 0 && pRecruit->uAccountID == iAccountID)
			break;
	}
	if(idx < 0)
	{
		pNewRecruit = StructCreateNoConst(parse_RecruitContainer);
		eaPush(&pData->eaRecruits, pNewRecruit);
	}
	else
	{
		pNewRecruit = pData->eaRecruits[idx];
	}
	
	if(NONNULL(pNewRecruit))
	{
		pNewRecruit->eRecruitState = RS_Accepted;
		pNewRecruit->uAccountID = iAccountID;
		if(uiTime)
		{
			pNewRecruit->uAcceptedTimeSS2000 = uiTime;
			pNewRecruit->uCreatedTimeSS2000 = uiTime;
			pNewRecruit->uOfferedTimeSS2000 = uiTime;
		}
		else
		{
			pNewRecruit->uAcceptedTimeSS2000 = timeSecondsSince2000();
			pNewRecruit->uCreatedTimeSS2000 = timeSecondsSince2000();
			pNewRecruit->uOfferedTimeSS2000 = timeSecondsSince2000();
		}
		pNewRecruit->pProductInternalName = StructAllocString(pchProductName);
		
		return (TRANSACTION_OUTCOME_SUCCESS);
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_NAME("GameAccount_AddRecruit") ACMD_ACCESSLEVEL(9);
void gslGADCmd_AddRecruit(Entity *pEnt, U32 iAccountID)
{
	if(pEnt && entGetAccountID(pEnt) > 0)
	{
		AutoTrans_gslGAD_tr_AddRecruit(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), iAccountID, GetProductName(), 0);
	}
}


AUTO_COMMAND ACMD_NAME("GameAccount_AddRecruitDate") ACMD_ACCESSLEVEL(9);
void gslGADCmd_AddRecruitDate(Entity *pEnt, U32 iAccountID, const char *pchDate)
{
	if(pEnt && entGetAccountID(pEnt) > 0)
	{
		AutoTrans_gslGAD_tr_AddRecruit(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), iAccountID, GetProductName(), timeGetSecondsSince2000FromDateString(pchDate));
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Earecruits");
enumTransactionOutcome gslGAD_tr_SetRecruitState(ATR_ARGS, NOCONST(GameAccountData) *pData, U32 iAccountID, const char *pchProductName, S32 eRecruitState)
{
	S32 idx;
	NOCONST(RecruitContainer) *pNewRecruit = NULL;
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	for(idx = eaSize(&pData->eaRecruits)-1; idx>=0; idx--)
	{
		NOCONST(RecruitContainer) *pRecruit = pData->eaRecruits[idx];
		if(stricmp(pRecruit->pProductInternalName, pchProductName) == 0 && pRecruit->uAccountID == iAccountID)
			break;
	}
	if(idx < 0)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pData->eaRecruits[idx]->eRecruitState = eRecruitState;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_NAME("GameAccount_SetRecruitState") ACMD_ACCESSLEVEL(9);
void gslGADCmd_SetRecruitState(Entity *pEnt, U32 iAccountID, S32 eRecruitState)
{
	if(pEnt && entGetAccountID(pEnt) > 0)
	{
		AutoTrans_gslGAD_tr_SetRecruitState(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), iAccountID, GetProductName(), eRecruitState);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Earecruiters");
enumTransactionOutcome gslGAD_tr_AddRecruiter(ATR_ARGS, NOCONST(GameAccountData) *pData, U32 iAccountID, const char *pchProductName, U32 uiTime)
{
	S32 idx = eaIndexedFindUsingString(&pData->eaRecruiters, pchProductName);
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if(idx >= 0)
	{
		pData->eaRecruiters[idx]->uAccountID = iAccountID;
		if(uiTime)
		{
			pData->eaRecruiters[idx]->uAcceptedTimeSS2000 = uiTime;
		}
		else
		{
			pData->eaRecruiters[idx]->uAcceptedTimeSS2000 = timeSecondsSince2000();
		}
	}
	else
	{
		NOCONST(RecruiterContainer) *pNewRecruiter = StructCreateNoConst(parse_RecruiterContainer);

		pNewRecruiter->uAccountID = iAccountID;
		pNewRecruiter->pProductInternalName = StructAllocString(pchProductName);
		if(uiTime)
		{
			pNewRecruiter->uAcceptedTimeSS2000 = uiTime;
		}
		else
		{
			pNewRecruiter->uAcceptedTimeSS2000 = timeSecondsSince2000();
		}

		eaPush(&pData->eaRecruiters, pNewRecruiter);
	}

	return (TRANSACTION_OUTCOME_SUCCESS);
}

AUTO_COMMAND ACMD_NAME("GameAccount_AddRecruiter") ACMD_ACCESSLEVEL(9);
void gslGADCmd_AddRecruiter(Entity *pEnt, U32 iAccountID)
{
	if(pEnt && entGetAccountID(pEnt) > 0)
	{
		AutoTrans_gslGAD_tr_AddRecruiter(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), iAccountID, GetProductName(), 0);
	}
}

AUTO_COMMAND ACMD_NAME("GameAccount_AddRecruiterDate") ACMD_ACCESSLEVEL(9);
void gslGADCmd_AddRecruiterDate(Entity *pEnt, U32 iAccountID, const char *pchDate)
{
	if(pEnt && entGetAccountID(pEnt) > 0)
	{
		AutoTrans_gslGAD_tr_AddRecruiter(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), iAccountID, GetProductName(), timeGetSecondsSince2000FromDateString(pchDate));
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Eapermissions, .Eatokens");
enumTransactionOutcome GameAccount_tr_AddPermission(ATR_ARGS, NOCONST(GameAccountData) *pData, GamePermissionDef *pDef, U32 bTokens)
{
	NOCONST(GamePermission) *pPerm = eaIndexedGetUsingString(&pData->eaPermissions, pDef->pchName);

	if(!pPerm)
	{
		pPerm = StructCreateNoConst(parse_GamePermission);
		pPerm->pchName = pDef->pchName;
		eaPush(&pData->eaPermissions, pPerm);
	}

	if(bTokens)
	{
		gamePermission_trh_GetTokens(pDef, &pData->eaTokens);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Eapermissions, .Eatokens");
enumTransactionOutcome GameAccount_tr_RemovePermission(ATR_ARGS, NOCONST(GameAccountData) *pData, GamePermissionDef *pDef, U32 bTokens)
{
	int index = eaIndexedFindUsingString(&pData->eaPermissions, pDef->pchName);
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if(index >= 0)
	{
		NOCONST(GamePermission) *pPermission;
		pPermission = eaRemove(&pData->eaPermissions, index);
		StructDestroyNoConst(parse_GamePermission, pPermission);
	}

	if(bTokens)
	{
		NOCONST(GameToken) **eaTokens = NULL;
		S32 i;

		//GetTokens assumes indexed
		eaIndexedEnableNoConst(&eaTokens, parse_GameToken);

		gamePermission_trh_GetTokens(pDef, &eaTokens);
		
		for(i = eaSize(&eaTokens)-1; i>=0; i--)
		{
			index = eaIndexedFindUsingString(&pData->eaTokens, eaTokens[i]->pchKey);
			if(index>=0)
			{
				NOCONST(GameToken) *pToken;
				pToken = eaRemove(&pData->eaTokens, index);
				StructDestroyNoConst(parse_GameToken, pToken);
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Eapermissions, .Eatokens");
enumTransactionOutcome GameAccount_tr_ClearPermissions(ATR_ARGS, NOCONST(GameAccountData) *pData, U32 bClearTokens)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	if(ISNULL(pData))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	//clear the permissions
	eaClearStructNoConst(&pData->eaPermissions, parse_GamePermission);

	//And the tokens if desired
	if(bClearTokens)
	{
		eaClearStructNoConst(&pData->eaTokens, parse_GameToken);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(GameAccount_AddPermission);
void gslGameAccountCmd_AddPermission(Entity *pEnt, ACMD_SENTENCE pchName)
{
	U32 iAccountID = entGetAccountID(pEnt);
	GamePermissionDef *pDef = NULL;
	if(!iAccountID)
		return;
	
	pDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pchName);
	if(pDef)
	{
		AutoTrans_GameAccount_tr_AddPermission(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, iAccountID, pDef, false);
	}
}
AUTO_COMMAND ACMD_NAME(GameAccount_AddPermissionAndTokens);
void gslGameAccountCmd_AddPermissionAndTokens(Entity *pEnt, ACMD_SENTENCE pchName)
{
	U32 iAccountID = entGetAccountID(pEnt);
	GamePermissionDef *pDef = NULL;
	if(!iAccountID)
		return;

	pDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pchName);
	if(pDef)
	{
		AutoTrans_GameAccount_tr_AddPermission(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, iAccountID, pDef, true);
	}
}

AUTO_COMMAND ACMD_NAME(GameAccount_RemovePermission);
void gslGameAccountCmd_RemovePermission(Entity *pEnt, ACMD_SENTENCE pchName)
{
	U32 iAccountID = entGetAccountID(pEnt);
	GamePermissionDef *pDef = NULL;
	if(!iAccountID)
		return;

	pDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pchName);
	if(pDef)
	{
		AutoTrans_GameAccount_tr_RemovePermission(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, iAccountID, pDef, false);
	}
}

AUTO_COMMAND ACMD_NAME(GameAccount_RemovePermissionAndTokens);
void gslGameAccountCmd_RemovePermissionAndTokens(Entity *pEnt, ACMD_SENTENCE pchName)
{
	U32 iAccountID = entGetAccountID(pEnt);
	GamePermissionDef *pDef = NULL;
	if(!iAccountID)
		return;

	pDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pchName);
	if(pDef)
	{
		AutoTrans_GameAccount_tr_RemovePermission(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, iAccountID, pDef, true);
	}
}

AUTO_COMMAND ACMD_NAME(GameAccount_ClearPermissions);
void gslGameAccountCmd_CleanPermissions(Entity *pEnt, U32 bTokensToo)
{
	U32 iAccountID = entGetAccountID(pEnt);
	if(!iAccountID)
		return;
	AutoTrans_GameAccount_tr_ClearPermissions(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, iAccountID, bTokensToo);
}

AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_NAME(GameAccountMakeNumericPurchase);
void gslGameAccountCmd_MakeNumericPurchase(Entity *pEnt, const char* pchDefName)
{
	GameAccountDataNumericPurchaseDef* pDef = GAD_NumericPurchaseDefFromName(pchDefName);
	GameAccountData* pData = entity_GetGameAccount(pEnt);

	if (GAD_EntCanMakeNumericPurchase(pEnt, pData, pDef, true))
	{
		ItemChangeReason reason = {0};
		inv_FillItemChangeReason(&reason, pEnt, "GAD:PurchaseWithNumerics", pchDefName);
		AutoTrans_GameAccount_tr_EntNumericPurchase(NULL, 
													GLOBALTYPE_GAMESERVER, 
													GLOBALTYPE_ENTITYPLAYER, 
													entGetContainerID(pEnt), 
													GLOBALTYPE_GAMEACCOUNTDATA, 
													pData->iAccountID,
													pchDefName,
													&reason);
	}
}


#include "AutoGen/gslGameAccountData_h_ast.c"
