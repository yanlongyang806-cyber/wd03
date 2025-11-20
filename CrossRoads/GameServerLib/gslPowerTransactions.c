/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiPowers.h"
#include "AutoTransDefs.h"
#include "Entity.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntityBuild.h"
#include "EntityBuild_h_ast.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "earray.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gslEventSend.h"
#include "gslEntity.h"
#include "LoggedTransactions.h"
#include "gslPowerTransactions.h"
#include "itemArt.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"
#include "itemserver.h"
#include "NotifyCommon.h"
#include "objTransactions.h"
#include "Player.h"
#include "ReferenceSystem.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "StringUtil.h"
#include "Team.h"
#include "TransactionSystem.h"
#include "gslChat.h"

#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "Character_mods.h"
#include "CombatConfig.h"
#include "PowerHelpers.h"
#include "Powers.h"
#include "AutoGen/Powers_h_ast.h"
#include "PowersMovement.h"
#include "PowerSlots.h"
#include "PowerTree.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "PowerTreeHelpers.h"
#include "species_common.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

#ifdef GAMESERVER

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#endif

typedef struct PowerEntityCallbackData
{
	int iPartitionIdx;
	EntityRef ref;
} PowerEntityCallbackData;

// Adds a Power to the Entity's Character's personal list.
//  Handles verifying it's allowed (in the case that bAllowDuplicates is false), 
//  creating the Power, getting it an ID, and adding it to the Character.
AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pchar.Pppowerspersonal, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets");
enumTransactionOutcome trCharacter_AddPowerPersonal(ATR_ARGS,
													NOCONST(Entity)* ent,
													PowerDef *pdef,
													int iLevel,
													int bAllowDuplicates)
{
	NOCONST(Power) *ppow;

	if(!bAllowDuplicates)
	{
		int i;
		for(i=eaSize(&ent->pChar->ppPowersPersonal)-1; i>=0; i--)
		{
			if(pdef==GET_REF(ent->pChar->ppPowersPersonal[i]->hDef))
			{
				TRANSACTION_RETURN_LOG_FAILURE("Duplicate PowerDef");
			}
		}
	}

	ppow = entity_CreatePowerHelper(ent,pdef,iLevel);
	eaIndexedAdd(&ent->pChar->ppPowersPersonal,ppow);

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Wrapper for trCharacter_AddPowerPersonal transaction
Power *character_AddPowerPersonal(int iPartitionIdx,
								  Character *pchar,
								  PowerDef *pdef,
								  int iLevel,
								  int bAllowDuplicates,
								  GameAccountDataExtract *pExtract)
{
	int iRetVal = 0;
	Power *ppow = NULL;
	Entity *e = pchar->pEntParent;
	PERFINFO_AUTO_START_FUNC();
	if(entGetType(e)==GLOBALTYPE_ENTITYPLAYER || entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET)
	{
		PERFINFO_AUTO_START("Player", 1);
		// Players actually go through the transaction system
		iRetVal = AutoTrans_trCharacter_AddPowerPersonal(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),pdef,iLevel,bAllowDuplicates);
		PERFINFO_AUTO_STOP();
	}
	else if(entGetType(e)==GLOBALTYPE_ENTITYCRITTER)
	{
		// Critters just directly call the transaction function, then call the handler
		enumTransactionOutcome eOut;
		PERFINFO_AUTO_START("Critter", 1);
		eOut = trCharacter_AddPowerPersonal(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),pdef,iLevel,bAllowDuplicates);
		iRetVal = (eOut == TRANSACTION_OUTCOME_SUCCESS);
		PERFINFO_AUTO_STOP();
	}
	else if (entGetType(e) == GLOBALTYPE_ENTITYPROJECTILE)
	{
		// Critters just directly call the transaction function, then call the handler
		enumTransactionOutcome eOut;
		PERFINFO_AUTO_START("Projectile", 1);
		eOut = trCharacter_AddPowerPersonal(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),pdef,iLevel,bAllowDuplicates);
		iRetVal = (eOut == TRANSACTION_OUTCOME_SUCCESS);
		PERFINFO_AUTO_STOP();
	}

	if(iRetVal)
	{
		PERFINFO_AUTO_START("if(iRetVal)", 1);
		ppow = character_FindNewestPowerByDefPersonal(pchar,pdef);		
		
		if(ppow && (entGetType(e)==GLOBALTYPE_ENTITYCRITTER || entGetType(e)==GLOBALTYPE_ENTITYPROJECTILE) && !e->bFakeEntity)
		{
			// Since it didn't go through the transaction system, we have to do this ourselves
			character_AddPower(iPartitionIdx,pchar,ppow,kPowerSource_Personal,pExtract);
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();

	return ppow;
}



// Removes a Power from the Character's personal list
AUTO_TRANSACTION ATR_LOCKS(ent, ".Pchar.Pppowerspersonal");
enumTransactionOutcome trCharacter_RemovePowerPersonal(ATR_ARGS, NOCONST(Entity)* ent, S32 iID)
{
	int idx;

	idx = eaIndexedFindUsingInt(&ent->pChar->ppPowersPersonal,iID);
	if(idx>=0)
	{
		Power *ppow = CONTAINER_RECONST(Power, eaRemove(&ent->pChar->ppPowersPersonal,idx));
		StructDestroySafe(parse_Power, &ppow);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own a Personal Power with ID %d", iID);
	}
}

// Wrapper for trCharacter_RemovePowerPersonal transaction
static int character_RemovePowerPersonalEx(Character *pchar, U32 uiID, 
										   bool* pbRequiresTransaction, 
										   TransactionReturnCallback cbFunc, void* cbData)
{
	int iRetVal = 0;
	bool bRequiresTransaction = false;
	Entity *e = pchar->pEntParent;
	if(entGetType(e)==GLOBALTYPE_ENTITYPLAYER || entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET || entGetType(e) == GLOBALTYPE_ENTITYPUPPET)
	{
		TransactionReturnVal* pReturnVal = NULL;
		if (cbFunc)
		{
			pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("RemovePowerPersonal", pchar->pEntParent, cbFunc, cbData);
		}
		// Players actually go through the transaction system
		iRetVal = AutoTrans_trCharacter_RemovePowerPersonal(pReturnVal,GetAppGlobalType(),entGetType(e),entGetContainerID(e),(S32)uiID);
		bRequiresTransaction = true;
	}
	else if(entGetType(e)==GLOBALTYPE_ENTITYCRITTER)
	{
		// Critters fix up the AI, directly call the transaction function, and then reset their array
		Power *ppow = powers_IndexedFindPowerByID(&pchar->ppPowersPersonal,uiID);
		if(ppow)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			enumTransactionOutcome eOut;
			if(e->aibase)
			{
				aiRemovePower(e,e->aibase,ppow);
			}
			eOut = trCharacter_RemovePowerPersonal(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),(S32)uiID);
			iRetVal = (eOut == TRANSACTION_OUTCOME_SUCCESS);
			character_ResetPowersArray(entGetPartitionIdx(e), pchar, pExtract);
		}
	}
	if (pbRequiresTransaction)
	{
		*pbRequiresTransaction = bRequiresTransaction;
	}
	return (iRetVal!=0);
}


// Wrapper for trCharacter_RemovePowerPersonal transaction
int character_RemovePowerPersonal(Character *pchar, U32 uiID)
{
	return character_RemovePowerPersonalEx(pchar, uiID, NULL, NULL, NULL);
}


// Structure used to pass around required data for temporary Power creation
typedef struct AddPowerTemporaryStruct
{
	PowerDef *pdef;
	AddPowerTemporaryCallback callback;
	void *pvUserData;
} AddPowerTemporaryStruct;

// Called to actually create a temporary Power, if everything went fine
static void AddPowerTemporaryResultHandler(enumTransactionOutcome eOut,
										  const char *pchID,
										  AddPowerTemporaryStruct *pTempStruct)
{
	bool bSuccess = false;

	if(eOut==TRANSACTION_OUTCOME_SUCCESS && pchID)
	{
		U32 uiID = atoi(pchID+strlen("ID:"));
		if(uiID)
		{
			// Create the Power
			Power *ppow = power_Create(pTempStruct->pdef->pchName);
			power_InitHelper(CONTAINER_NOCONST(Power, ppow),0);
			power_SetIDHelper(CONTAINER_NOCONST(Power, ppow),uiID);
			// Callback that we succeeded
			if(!pTempStruct->callback(ppow,pTempStruct->pvUserData))
			{
				// Callback return false, clean up the Power
				StructDestroy(parse_Power,ppow);
			}
			bSuccess = true;
		}
	}
	
	if(!bSuccess)
	{
		// Callback that we failed
		pTempStruct->callback(NULL,pTempStruct->pvUserData);
	}

	// Free the structure created by the wrapper
	free(pTempStruct);
}

// Wrapper for all the stuff that needs to be done to add a non-transacted
//  temporary Power to the Character.  Even though the Power is not transacted,
//  this still triggers a transaction on the Character.  This does not add the
//  Power to the Character's general list, it is up to the caller to do so in
//  the callback.
void character_AddPowerTemporary(Character *pchar,
								 PowerDef *pdef,
								 AddPowerTemporaryCallback callback,
								 void *pvUserData)
{
	Entity *e = pchar->pEntParent;
	if(e && callback)
	{
		AddPowerTemporaryStruct *pTempStruct;
		
		// This struct MUST be free'd in the result handler
		pTempStruct = malloc(sizeof(AddPowerTemporaryStruct));
		pTempStruct->pdef = pdef;
		pTempStruct->callback = callback;
		pTempStruct->pvUserData = pvUserData;

		// Hacky emulation of old process steps using the new temp PowerID system
		//  Done like this for now to minimize code changes
		{
			U32 uiID = character_GetNewTempPowerID(pchar);
			char *pchID = NULL;
			enumTransactionOutcome eOut = TRANSACTION_OUTCOME_SUCCESS;
			estrStackCreate(&pchID);
			estrAppend2(&pchID,"ID:");
			estrConcatf(&pchID,"%d",uiID);
			AddPowerTemporaryResultHandler(eOut,pchID,pTempStruct);
			estrDestroy(&pchID);
		}
	}
}

static int AddRecruitingPower_CB(Power *ppow, PowerEntityCallbackData *pData)
{
	int bSuccess = false;
	PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

	if(ppow && pData && pdef)
	{
		Entity *pEnt = pData->ref ? entFromEntityRef(pData->iPartitionIdx, pData->ref) : NULL;
		if(pEnt && pEnt->pChar)
		{
			Character *pchar = pEnt->pChar;

			if(!eaSize(&pchar->ppPowersTemporary) || 
				(!character_FindPowerByDef(pchar, pdef) && !character_FindPowerByDefTemporary(pchar,pdef)) )
			{
				eaIndexedEnable(&pchar->ppPowersTemporary,parse_Power);
				eaPush(&pchar->ppPowersTemporary, ppow);			

				bSuccess = true;
				pchar->bResetPowersArray = true;
			}
		}
	}

	if(pData)
		free(pData);

	return(bSuccess);
}

void character_RecruitingUpdatePowers(Character *pchar)
{
	Entity *pEnt = pchar->pEntParent;
	Team *pTeam = team_GetTeam(pEnt);
	PowerDef *pdef = powerdef_Find("Recruiting_Bonus_Power");
	
	if(pEnt && pchar && pdef)
	{
		ANALYSIS_ASSUME(pdef);
		//If I'm on a team, try to add it
		if(pTeam)
		{
			//Don't add it if I already have it
			if(!character_FindPowerByDefTemporary(pchar, pdef))
			{	
				S32 iMemberIdx;
				bool bAddPower = false;
				int iPartitionIdx = entGetPartitionIdx(pEnt);
				for(iMemberIdx = eaSize(&pTeam->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
				{
					TeamMember *pTeamMember = eaGet(&pTeam->eaMembers, iMemberIdx);
					Entity *pEntMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
					
					if(!pEntMember || pEnt == pEntMember)
						continue;

					//If this team member is my new recruit
					if(entity_IsNewRecruit(pEnt, pEntMember))
					{
						bAddPower = true;

						//If they don't have the power, tell them to update their list
						if(pEntMember->pTeam && pEntMember->pChar && !character_FindPowerByDefTemporary(pEntMember->pChar, pdef))
							pEntMember->pTeam->bUpdateTeamPowers = true;
					}
					//If this team member is my new recruiter
					if(entity_IsNewRecruiter(pEnt, pEntMember))
					{
						bAddPower = true;

						//If they don't have the power, tell them to update their list
						if(pEntMember->pTeam && pEntMember->pChar && !character_FindPowerByDefTemporary(pEntMember->pChar, pdef))
							pEntMember->pTeam->bUpdateTeamPowers = true;
					}
				}
				if(bAddPower)
				{
					PowerEntityCallbackData *pData = calloc(1,sizeof(PowerEntityCallbackData));
					pData->iPartitionIdx = entGetPartitionIdx(pEnt);
					pData->ref = entGetRef(pEnt);
					character_AddPowerTemporary(pchar,pdef, AddRecruitingPower_CB, pData );
				}
			}
		}
		//Else, try to remove it
		else
		{
			Power *ppow = character_FindPowerByDefTemporary(pchar, pdef);
			if(ppow)
				character_RemovePowerTemporary(pchar, ppow->uiID);
		}
	}
}

void character_LoadAddTemporaryPowers(Character *pchar)
{
	//TODO(BH): Add temporary powers from the zone/region etc?

	if(pchar && pchar->pEntParent && team_IsMember(pchar->pEntParent))
	{
		// Add temporary powers from being a recruiter
		GameAccountData *pData = entity_GetGameAccount(pchar->pEntParent);
		if(pData && (eaSize(&pData->eaRecruiters) || eaSize(&pData->eaRecruits)))
			pchar->pEntParent->pTeam->bUpdateTeamPowers = true;
	}
}

int character_RemovePowerTemporary(Character *pchar, U32 uiID)
{
	int bSuccess = false;
	if(pchar)
	{
		int idx = eaIndexedFindUsingInt(&pchar->ppPowersTemporary, (S32)uiID);
		if(idx >= 0)
		{
			power_Destroy(pchar->ppPowersTemporary[idx],pchar);
			eaRemove(&pchar->ppPowersTemporary, idx);
			
			bSuccess = true;
			pchar->bResetPowersArray = true;
		}
	}
	return(bSuccess);
}


// Tries to find the Power in the PowerTreeNode's Power array
SA_RET_OP_VALID static NOCONST(Power) *PowerTreeNodeFindPowerByID(SA_PARAM_NN_VALID NOCONST(PTNode) *pNode, U32 uiID)
{
	int i;
	for(i=eaSize(&pNode->ppPowers)-1; i>=0; i--)
	{
		if(pNode->ppPowers[i]->uiID==uiID)
		{
			return pNode->ppPowers[i];
		}
	}
	return NULL;
}





// Sets the Character's Power's hue
//  !!!! TAKES AN S32 for ID, NOT U32 !!!!
AUTO_TRANSACTION ATR_LOCKS(e, "pChar.ppPowersPersonal[]");
enumTransactionOutcome trCharacter_SetPowerHue(ATR_ARGS, NOCONST(Entity)* e, S32 iID, F32 fHue)
{
	NOCONST(Power) *ppow = eaIndexedGetUsingInt(&e->pChar->ppPowersPersonal,iID);

	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that power");
	}

	power_SetHueHelper(ppow,fHue);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the Character's Power's hue via PowerTree
AUTO_TRANSACTION ATR_LOCKS(e, "pChar.ppPowerTrees[]");
enumTransactionOutcome trCharacter_SetPowerHueTree(ATR_ARGS, NOCONST(Entity)* e, const char *pchTree, const char *pchNode, U32 uiID, F32 fHue)
{
	NOCONST(PowerTree) *ptree;
	NOCONST(PTNode) *pnode;
	NOCONST(Power) *ppow;

	ptree = eaIndexedGetUsingString(&e->pChar->ppPowerTrees,pchTree);
	if(!ptree)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that tree");
	}

	pnode = eaIndexedGetUsingString(&ptree->ppNodes,pchNode);
	if(!pnode)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that node");
	}

	ppow = PowerTreeNodeFindPowerByID(pnode,uiID);
	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that power");
	}

	power_SetHueHelper(ppow,fHue);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the Player's Power's hue via Item
AUTO_TRANSACTION ATR_LOCKS(e, ".pInventoryV2.Ppinventorybags, pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome trPlayer_SetPowerHueItem(ATR_ARGS, NOCONST(Entity)* e, U32 uiID, F32 fHue)
{
	NOCONST(Power) *ppow;

	ppow = item_trh_FindPowerByID(e,uiID,NULL,NULL,NULL,NULL);
	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Player does not own that power");
	}

	power_SetHueHelper(ppow,fHue);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets all of the Entity's Powers' hues
AUTO_TRANSACTION ATR_LOCKS(e, ".pChar.ppPowersPersonal, .pChar.ppPowerTrees, .pInventoryV2.Ppinventorybags");
enumTransactionOutcome trEntity_SetAllPowersHue(ATR_ARGS, NOCONST(Entity)* e, F32 fHue)
{
	int i,j,k;

	for(i=eaSize(&e->pChar->ppPowersPersonal)-1; i>=0; i--)
		power_SetHueHelper(e->pChar->ppPowersPersonal[i],fHue);

	for(i=eaSize(&e->pChar->ppPowerTrees)-1; i>=0; i--)
		for(j=eaSize(&e->pChar->ppPowerTrees[i]->ppNodes)-1; j>=0; j--)
			for(k=eaSize(&e->pChar->ppPowerTrees[i]->ppNodes[j]->ppPowers)-1; k>=0; k--)
				power_SetHueHelper(e->pChar->ppPowerTrees[i]->ppNodes[j]->ppPowers[k],fHue);

	if(NONNULL(e->pInventoryV2))
	{
		for(i=eaSize(&e->pInventoryV2->ppInventoryBags)-1; i>=0; i--)
			for(j=eaSize(&e->pInventoryV2->ppInventoryBags[i]->ppIndexedInventorySlots)-1; j>=0; j--)
				if(e->pInventoryV2->ppInventoryBags[i]->ppIndexedInventorySlots[j]->pItem)
				{
					for(k=eaSize(&e->pInventoryV2->ppInventoryBags[i]->ppIndexedInventorySlots[j]->pItem->ppPowers)-1; k>=0; k--)
						power_SetHueHelper(e->pInventoryV2->ppInventoryBags[i]->ppIndexedInventorySlots[j]->pItem->ppPowers[k],fHue);
				}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Wrapper for trCharacter_SetPowerHue
void character_SetPowerHue(Character *pchar, U32 uiID, F32 fHue)
{
	Entity *e = pchar->pEntParent;

	// If the requested hue is out of bounds, clamp it
	if(fHue < 0.f || fHue > 360.f)
	{
		fHue = 0.f;
	}

	if(entGetType(e)==GLOBALTYPE_ENTITYPLAYER || entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET || entGetType(e) == GLOBALTYPE_ENTITYPUPPET)
	{
		Power *ppow = NULL;
		if(!uiID)
		{
			// All powers
			AutoTrans_trEntity_SetAllPowersHue(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),fHue);
		}
		else if(ppow = powers_IndexedFindPowerByID(&pchar->ppPowersPersonal,uiID))
		{
			// Personal, transacted
			//  !!!! TAKES AN S32 for ID, NOT U32 !!!!
			S32 iID = (S32)uiID;
			AutoTrans_trCharacter_SetPowerHue(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),iID,fHue);
		}
		else if(ppow = powers_IndexedFindPowerByID(&pchar->modArray.ppPowers,uiID))
		{
			// AttribMod, not transacted
			power_SetHueHelper(CONTAINER_NOCONST(Power, ppow),fHue);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
		else
		{
			// PowerTree, transacted
			PowerTree *ptree = NULL;
			PTNode *pnode = NULL;
			ppow = character_FindPowerByIDTree(pchar,uiID,&ptree,&pnode);
			if(ppow)
			{
				if(ptree && GET_REF(ptree->hDef) && pnode && GET_REF(pnode->hDef))
				{
					AutoTrans_trCharacter_SetPowerHueTree(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),GET_REF(ptree->hDef)->pchName,GET_REF(pnode->hDef)->pchNameFull,uiID,fHue);
				}
			}
			else
			{
				// Item, transacted
				ppow = item_FindPowerByID(pchar->pEntParent,uiID,NULL,NULL,NULL,NULL);
				if(ppow)
				{
					AutoTrans_trPlayer_SetPowerHueItem(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),uiID,fHue);
				}
			}
		}
	}
	else if(entGetType(e)==GLOBALTYPE_ENTITYCRITTER)
	{
		// Critters just run the transactions directly
		if(!uiID)
			trEntity_SetAllPowersHue(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity,e),fHue);
		else
			trCharacter_SetPowerHue(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),uiID,fHue);
	}
}




// Sets the Character's Power's emit
//  !!!! TAKES AN S32 for ID, NOT U32 !!!!
AUTO_TRANSACTION ATR_LOCKS(e, ".pChar.ppPowersPersonal[]");
enumTransactionOutcome trCharacter_SetPowerEmit(ATR_ARGS, NOCONST(Entity)* e, S32 iID, const char *cpchEmit)
{
	NOCONST(Power) *ppow = eaIndexedGetUsingInt(&e->pChar->ppPowersPersonal,iID);

	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that power");
	}

	power_SetEmitHelper(ppow,cpchEmit);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the Character's Power's emit via PowerTree
AUTO_TRANSACTION ATR_LOCKS(e, "pChar.ppPowerTrees[]");
enumTransactionOutcome trCharacter_SetPowerEmitTree(ATR_ARGS, NOCONST(Entity)* e, const char *pchTree, const char *pchNode, U32 uiID, const char *cpchEmit)
{
	NOCONST(PowerTree) *ptree;
	NOCONST(PTNode) *pnode;
	NOCONST(Power) *ppow;

	ptree = eaIndexedGetUsingString(&e->pChar->ppPowerTrees,pchTree);
	if(!ptree)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that tree");
	}

	pnode = eaIndexedGetUsingString(&ptree->ppNodes,pchNode);
	if(!pnode)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that node");
	}

	ppow = PowerTreeNodeFindPowerByID(pnode,uiID);
	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that power");
	}

	power_SetEmitHelper(ppow,cpchEmit);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the Player's Power's emit via Item
AUTO_TRANSACTION ATR_LOCKS(e, ".pInventoryV2.Ppinventorybags, pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome trPlayer_SetPowerEmitItem(ATR_ARGS, NOCONST(Entity)* e, U32 uiID, const char *cpchEmit)
{
	NOCONST(Power) *ppow;

	ppow = item_trh_FindPowerByID(e,uiID,NULL,NULL,NULL,NULL);
	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Player does not own that power");
	}

	power_SetEmitHelper(ppow,cpchEmit);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Wrapper for trCharacter_SetPowerEmit
void character_SetPowerEmit(Character *pchar, U32 uiID, const char *cpchEmit)
{
	Entity *e = pchar->pEntParent;

	if(entGetType(e)==GLOBALTYPE_ENTITYPLAYER || entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET || entGetType(e) == GLOBALTYPE_ENTITYPUPPET)
	{
		Power *ppow = NULL;
		if(ppow = powers_IndexedFindPowerByID(&pchar->ppPowersPersonal,uiID))
		{
			// Personal, transacted
			//  !!!! TAKES AN S32 for ID, NOT U32 !!!!
			S32 iID = (S32)uiID;
			AutoTrans_trCharacter_SetPowerEmit(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),iID,cpchEmit);
		}
		else if(ppow = powers_IndexedFindPowerByID(&pchar->modArray.ppPowers,uiID))
		{
			// AttribMod, not transacted
			power_SetEmitHelper(CONTAINER_NOCONST(Power, ppow),cpchEmit);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
		else
		{
			// PowerTree, transacted
			PowerTree *ptree = NULL;
			PTNode *pnode = NULL;
			ppow = character_FindPowerByIDTree(pchar,uiID,&ptree,&pnode);
			if(ppow)
			{
				if(ptree && GET_REF(ptree->hDef) && pnode && GET_REF(pnode->hDef))
				{
					AutoTrans_trCharacter_SetPowerEmitTree(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),GET_REF(ptree->hDef)->pchName,GET_REF(pnode->hDef)->pchNameFull,uiID,cpchEmit);
				}
			}
			else
			{
				// Item, transacted
				ppow = item_FindPowerByID(pchar->pEntParent,uiID,NULL,NULL,NULL,NULL);
				if(ppow)
				{
					AutoTrans_trPlayer_SetPowerEmitItem(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),uiID,cpchEmit);
				}
			}
		}
	}
	else if(entGetType(e)==GLOBALTYPE_ENTITYCRITTER)
	{
		// Critters just do personal non-transacted
		trCharacter_SetPowerEmit(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),uiID,cpchEmit);
	}
}




// Sets the Character's Power's EntCreateCostume
//  !!!! TAKES AN S32 for ID, NOT U32 !!!!
AUTO_TRANSACTION ATR_LOCKS(e, ".pChar.ppPowersPersonal[]");
enumTransactionOutcome trCharacter_SetPowerEntCreateCostume(ATR_ARGS, NOCONST(Entity)* e, S32 iID, S32 iEntCreateCostume)
{
	NOCONST(Power) *ppow = eaIndexedGetUsingInt(&e->pChar->ppPowersPersonal,iID);

	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that power");
	}

	power_SetEntCreateCostumeHelper(ppow,iEntCreateCostume);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the Character's Power's EntCreateCostume via PowerTree
AUTO_TRANSACTION ATR_LOCKS(e, ".pChar.ppPowerTrees[]");
enumTransactionOutcome trCharacter_SetPowerEntCreateCostumeTree(ATR_ARGS, NOCONST(Entity)* e, const char *pchTree, const char *pchNode, U32 uiID, S32 iEntCreateCostume)
{
	NOCONST(PowerTree) *ptree;
	NOCONST(PTNode) *pnode;
	NOCONST(Power) *ppow;

	ptree = eaIndexedGetUsingString(&e->pChar->ppPowerTrees,pchTree);
	if(!ptree)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that tree");
	}

	pnode = eaIndexedGetUsingString(&ptree->ppNodes,pchNode);
	if(!pnode)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that node");
	}

	ppow = PowerTreeNodeFindPowerByID(pnode,uiID);
	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Character does not own that power");
	}

	power_SetEntCreateCostumeHelper(ppow,iEntCreateCostume);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sets the Character's Power's EntCreateCostume via Item
AUTO_TRANSACTION ATR_LOCKS(e, "pInventoryV2.ppLiteBags[], .pInventoryV2.Ppinventorybags, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome trCharacter_SetPowerEntCreateCostumeItem(ATR_ARGS, NOCONST(Entity)* e, U32 uiID, S32 iEntCreateCostume)
{
	NOCONST(Power) *ppow;

	ppow = item_trh_FindPowerByID(e,uiID,NULL,NULL,NULL,NULL);
	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Player does not own that power");
	}

	power_SetEntCreateCostumeHelper(ppow,iEntCreateCostume);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Wrapper for trCharacter_SetPowerEntCreateCostume
void character_SetPowerEntCreateCostume(Character *pchar, U32 uiID, S32 iEntCreateCostume)
{
	Entity *e = pchar->pEntParent;

	if(entGetType(e)==GLOBALTYPE_ENTITYPLAYER || entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET || entGetType(e) == GLOBALTYPE_ENTITYPUPPET)
	{
		Power *ppow = NULL;
		if(ppow = powers_IndexedFindPowerByID(&pchar->ppPowersPersonal,uiID))
		{
			// Personal, transacted
			//  !!!! TAKES AN S32 for ID, NOT U32 !!!!
			S32 iID = (S32)uiID;
			AutoTrans_trCharacter_SetPowerEntCreateCostume(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),iID,iEntCreateCostume);
		}
		else if(ppow = powers_IndexedFindPowerByID(&pchar->modArray.ppPowers,uiID))
		{
			// AttribMod, not transacted
			power_SetEntCreateCostumeHelper(CONTAINER_NOCONST(Power, ppow),iEntCreateCostume);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
		else
		{
			// PowerTree, transacted
			PowerTree *ptree = NULL;
			PTNode *pnode = NULL;
			ppow = character_FindPowerByIDTree(pchar,uiID,&ptree,&pnode);
			if(ppow)
			{
				if(ptree && GET_REF(ptree->hDef) && pnode && GET_REF(pnode->hDef))
				{
					AutoTrans_trCharacter_SetPowerEntCreateCostumeTree(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),GET_REF(ptree->hDef)->pchName,GET_REF(pnode->hDef)->pchNameFull,uiID,iEntCreateCostume);
				}
			}
			else
			{
				// Item, transacted
				ppow = item_FindPowerByID(pchar->pEntParent,uiID,NULL,NULL,NULL,NULL);
				if(ppow)
				{
					AutoTrans_trCharacter_SetPowerEntCreateCostumeItem(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),uiID,iEntCreateCostume);
				}
			}
		}
	}
	else if(entGetType(e)==GLOBALTYPE_ENTITYCRITTER)
	{
		// Critters just do personal non-transacted
		trCharacter_SetPowerEntCreateCostume(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),uiID,iEntCreateCostume);
	}
}

// Subtracts up to one from the Character's Power's charges via Item
AUTO_TRANSACTION ATR_LOCKS(e, "pInventoryV2.ppLiteBags[], .pInventoryV2.Ppinventorybags, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome trPlayer_PowerSetChargesItem(ATR_ARGS, NOCONST(Entity)* e, U32 uiID, S32 iChargesUse)
{
	NOCONST(Power) *ppow;

	ppow = item_trh_FindPowerByID(e, uiID, NULL, NULL, NULL, NULL);
	if(!ppow)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Player does not own that power");
	}

	power_SetChargesUsedHelper(ppow, iChargesUse);

	return TRANSACTION_OUTCOME_SUCCESS;
}

typedef struct PowerExpireCBData
{
	int iPartitionIdx;
	EntityRef erRef;
	U32 uiPowID;
	const char* pchPowerDefName; // Pooled string
	const char* pchItemDefName; // Pooled string
} PowerExpireCBData;

static void PowerExpire_CB(TransactionReturnVal *pReturnVal, PowerExpireCBData* pData)
{
	if (pData)
	{
		Entity* pEnt = entFromEntityRef(pData->iPartitionIdx, pData->erRef);
		if (pEnt && pEnt->pChar)
		{
			Power* pPower = character_FindPowerByIDComplete(pEnt->pChar, pData->uiPowID);
			if (pPower)
			{
				pPower->bExpirationPending = false;
			}

			if (pData->pchPowerDefName && pData->pchPowerDefName[0] &&
				pData->pchItemDefName && pData->pchItemDefName[0] &&
				pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
			{
				eventsend_RecordItemUsed(pEnt, pData->pchItemDefName, pData->pchPowerDefName);
			}
		}
	}
	SAFE_FREE(pData);
}

static PowerExpireCBData* gslPowerExpire_CreateCBData(int iPartitionIdx, 
													  EntityRef erRef, 
													  U32 uiPowerID, 
													  const char* pchPowerDefName, 
													  const char* pchItemDefName)
{
	PowerExpireCBData* cbData = calloc(1, sizeof(PowerExpireCBData));
	cbData->iPartitionIdx = iPartitionIdx;
	cbData->erRef = erRef;
	cbData->uiPowID = uiPowerID;
	cbData->pchPowerDefName = allocAddString(pchPowerDefName);
	cbData->pchItemDefName = allocAddString(pchItemDefName);
	return cbData;
}

// Wrapper for various systems to handle a Power expiring, which will likely involve a transaction.
//  Does NOT check to see if the Power has actually expired.
void character_PowerExpire(Character *pchar, U32 uiID, GameAccountDataExtract *pExtract)
{
	Power *ppow = NULL;
	PowerExpireCBData* cbData;

	if(ppow = powers_IndexedFindPowerByID(&pchar->ppPowersPersonal,uiID))
	{
		bool bRequiresTransaction = false;
		ppow->bExpirationPending = true;
		// Personal Power, just remove it
		cbData = gslPowerExpire_CreateCBData(entGetPartitionIdx(pchar->pEntParent), entGetRef(pchar->pEntParent), uiID, NULL, NULL);
		character_RemovePowerPersonalEx(pchar,uiID,&bRequiresTransaction,PowerExpire_CB,cbData);
		if (!bRequiresTransaction)
		{
			SAFE_FREE(cbData);
		}
	}
	else if(ppow = powers_IndexedFindPowerByID(&pchar->modArray.ppPowers,uiID))
	{
		// AttribMod, find and expire the mod
		AttribMod *pmod = character_GetPowerCreatorMod(pchar,ppow);
		if(pmod)
		{
			mod_Expire(pmod);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
	}
	else
	{
		Entity* e = pchar->pEntParent;
		InvBagIDs BagID = InvBagIDs_None;
		int SlotIdx = -1;
		int iItemPowerIdx = -1;
		Item *pItem = NULL;
		ItemDef* pDef;
		ItemPowerDef *pItemPowerDef = NULL;
		const char* pchItemUsedKey = "Reward.YouUsedItem";

		// Item
		ppow = item_FindPowerByID(pchar->pEntParent,uiID,&BagID,&SlotIdx,&pItem,&iItemPowerIdx);
		pItemPowerDef = item_GetItemPowerDef(pItem,iItemPowerIdx);
		pDef = pItem ? GET_REF(pItem->hItem) : NULL;

		// Set special expiration message for injury items
		if(ppow && pDef && pDef->eType == kItemType_Injury)
		{
			PowerDef* pPowerDef = GET_REF(ppow->hDef);

			if(pPowerDef && pPowerDef->piCategories)
			{
				int i;
				for(i = 0; i < eaiSize(&pPowerDef->piCategories); i++)
				{
					const char* pchCategoryName = StaticDefineIntRevLookup(PowerCategoriesEnum, pPowerDef->piCategories[i]);
					if(stricmp_safe(pchCategoryName, "Region_Ground") == 0)
					{
						pchItemUsedKey = "Reward.InjuryExpiredGround";
						break;
					} 
					else if(stricmp_safe(pchCategoryName, "Region_SpaceSector") == 0) 
					{
						pchItemUsedKey = "Reward.InjuryExpiredSpace";
						break;
					}
				}
			}
		}

		if(ppow)
		{
			if ( (BagID != InvBagIDs_None) &&
				 (SlotIdx != -1) &&
				 (pItemPowerDef) &&
				 ((pItemPowerDef->flags & kItemPowerFlag_Rechargeable) == 0) &&
				 (pItem) &&
				 (SAFE_MEMBER(pDef, flags) & kItemDefFlag_ExpireOnAnyPower ? item_AnyPowersExpired(pItem, true) : item_AllPowersExpired(pItem, true)) )
			{
				bool bFound = false;
				InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pchar->pEntParent), BagID, pExtract);
				const InvBagDef* pBagDef = invbag_def(pBag);
				S32 iRemoveCount = 1;
				bool bLifetimeExpire = false;

				if(pItem->count > 1 && (SAFE_MEMBER(pDef, flags) & kItemDefFlag_ExpireOnAnyPower ? item_AnyPowersExpired(pItem, false) : item_AllPowersExpired(pItem, false)))
				{
					// This power has expired due to the fact that a time limitation has expired. remove the entire stack at once
					iRemoveCount = pItem->count;
					bLifetimeExpire = true;
				}

				// If its a lifetime expire then you must use the ones in the correct bag otherwise you will get a double expire. i.e. the power will still want to expire in device bag after you destroyed ones in the inventory bag
				if (pBag && pDef && pBagDef && pBagDef->bUseItemsInInventoryFirst && !bLifetimeExpire)
				{
					InvBagIDs eInvBag = InvBagIDs_None;
					int iSlot = -1;
					inv_GetSlotByItemName(pchar->pEntParent,InvBagIDs_Inventory,pDef->pchName,&eInvBag,&iSlot,pExtract);
					if (eInvBag != InvBagIDs_None && iSlot >= 0)
					{
						if(entGetType(pchar->pEntParent)==GLOBALTYPE_ENTITYCRITTER)
						{
							invbag_RemoveItem(ATR_EMPTY_ARGS,(NOCONST(Entity)*)pchar->pEntParent,true,eInvBag,iSlot,iRemoveCount,NULL,NULL);
							character_ResetPowersArray(entGetPartitionIdx(pchar->pEntParent), pchar, NULL);
						}
						else
						{
							ItemChangeReason reason = {0};

							inv_FillItemChangeReason(&reason, pchar->pEntParent, "Powers:PowerExpire", SAFE_MEMBER(pDef,pchName));

							ppow->bExpirationPending = true;
							cbData = gslPowerExpire_CreateCBData(entGetPartitionIdx(pchar->pEntParent), 
																 entGetRef(pchar->pEntParent), 
																 uiID, 
																 REF_STRING_FROM_HANDLE(ppow->hDef), 
																 pDef->pchName);
							if (!item_RemoveFromBagEx(pchar->pEntParent, eInvBag, iSlot, iRemoveCount, BagID, SlotIdx, false,
								pchItemUsedKey, &reason, pExtract, PowerExpire_CB, cbData))
							{
								ppow->bExpirationPending = false;
								SAFE_FREE(cbData);
							}
						}
						bFound = true;
					}
				}
				if (!bFound) //call transaction to remove the item
				{
					if(entGetType(pchar->pEntParent)==GLOBALTYPE_ENTITYCRITTER)
					{
						invbag_RemoveItem(ATR_EMPTY_ARGS,(NOCONST(Entity)*)pchar->pEntParent,true,BagID,SlotIdx,iRemoveCount,NULL, NULL);
						character_ResetPowersArray(entGetPartitionIdx(pchar->pEntParent), pchar, NULL);
					}
					else
					{
						ItemChangeReason reason = {0};

						inv_FillItemChangeReason(&reason, pchar->pEntParent, "Powers:PowerExpire", SAFE_MEMBER(pDef,pchName));

						ppow->bExpirationPending = true;
						cbData = gslPowerExpire_CreateCBData(entGetPartitionIdx(pchar->pEntParent), 
															 entGetRef(pchar->pEntParent), 
															 uiID, 
															 REF_STRING_FROM_HANDLE(ppow->hDef), 
															 SAFE_MEMBER(pDef, pchName));
						if (!item_RemoveFromBagEx(pchar->pEntParent, BagID, SlotIdx, iRemoveCount, 0, -1, false,
							pchItemUsedKey, &reason, pExtract, PowerExpire_CB, cbData))
						{
							ppow->bExpirationPending = false;
							SAFE_FREE(cbData);
						}
					}
				}
			}
		}
	}
}

typedef struct TempClassPowerStruct
{
	int iPartitionIdx;
	EntityRef erTarget;
	int idx;
	REF_TO(CharacterClass) hClass;
} TempClassPowerStruct;

static int AddTempClassPower_CB(Power *ppow, TempClassPowerStruct *pStruct)
{
	int bSuccess = false;
	PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

	if(ppow && pStruct && pdef)
	{
		Entity *pEnt = entFromEntityRef(pStruct->iPartitionIdx, pStruct->erTarget);
		if(pEnt && pEnt->pChar)
		{
			Character *pchar = pEnt->pChar;

			if(ea32Size(&pchar->puiTempClassPowers)>pStruct->idx
				&& pchar->puiTempClassPowers[pStruct->idx] == 0
				&& GET_REF(pStruct->hClass) == GET_REF(pchar->hClassTemporary))
			{
				eaIndexedEnable(&pchar->ppPowersTemporary,parse_Power);
				eaPush(&pchar->ppPowersTemporary, ppow);			

				bSuccess = true;
				pchar->bResetPowersArray = true;

				pchar->puiTempClassPowers[pStruct->idx] = ppow->uiID;
			}
		}
	}

	if(pStruct)
	{
		REMOVE_HANDLE(pStruct->hClass);
		free(pStruct);
	}

	return(bSuccess);
}

// Called when the Entity's Character's Class has changed
void character_SetClassCallback(Entity *pent, GameAccountDataExtract *pExtract)
{
	if(pent && pent->pChar)
	{
		Character *pchar = pent->pChar;
		int iPartitionIdx = entGetPartitionIdx(pent);

		// Stuff that needs to be reset for a Class change
		pchar->bSkipAccrueMods = false;
		character_DirtyInnatePowers(pchar);
		character_DirtyPowerStats(pchar);
		character_DirtyInnateAccrual(pchar);
		character_PowerSlotsValidate(pchar);
		character_UpdateMovement(pchar, NULL);
		character_ResetPowersArray(iPartitionIdx, pent->pChar, pExtract);

		{
			CharacterClass *pClass = character_GetClassCurrent(pchar);
			if (REF_HANDLE_IS_ACTIVE(pClass->hArt))
			{
				entity_UpdateItemArtAnimFX(pent);
			}

			if (g_CombatConfig.bCharacterClassSpecifiesStrafing)
			{
				gslEntitySetIsStrafing(pent, pClass->bStrafing);
			}
		}

		// Update the list of class powers assigned from the temporary class override
		{
			int i;
			CharacterClass *pTempClass = GET_REF(pchar->hClassTemporary);

			for(i=0;i<ea32Size(&pchar->puiTempClassPowers);i++)
			{
				if(pchar->puiTempClassPowers[i])
					character_RemovePowerTemporary(pchar,pchar->puiTempClassPowers[i]);
			}

			ea32Clear(&pchar->puiTempClassPowers);

			if(pTempClass)
			{
				int iSize = eaSize(&pTempClass->ppPowers);

				ea32SetSize(&pchar->puiTempClassPowers,iSize);

				for(i=0;i<iSize;i++)
				{
					TempClassPowerStruct *pStruct = calloc(sizeof(TempClassPowerStruct),1);

					pStruct->iPartitionIdx = entGetPartitionIdx(pent);
					pStruct->erTarget = entGetRef(pent);
					pStruct->idx=i;
					COPY_HANDLE(pStruct->hClass,pchar->hClassTemporary);

					character_AddPowerTemporary(pchar,GET_REF(pTempClass->ppPowers[i]->hdef),AddTempClassPower_CB,pStruct);
				}
			}
		}

		// Update the chat server if they haven't selected a playing type,
		// because then it's based on the class
		if (iPartitionIdx != PARTITION_ENT_BEING_DESTROYED && !SAFE_MEMBER4(pent, pPlayer, pUI, pLooseUI, pchPlayingStyles))
		{
			ServerChat_PlayerUpdate(pent, CHATUSER_UPDATE_SHARD);
		}
	}
}

void CharacterSetClassCallback(TransactionReturnVal *returnVal, PowerEntityCallbackData *pData)
{
	Entity *e = entFromEntityRef(pData->iPartitionIdx, pData->ref);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);

	character_SetClassCallback(e,pExtract);

	free(pData);
}

// Sets the Charater's class, optionally sets the current build's class as well.
AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Uitimestampbuildset, .Pchar.Hclass, .Pchar.Pppowersclass, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets");
enumTransactionOutcome trCharacter_SetClass(ATR_ARGS, NOCONST(Entity)* e, const char *cpchClass, U32 bBuild)
{
	character_SetClassHelper(e->pChar,cpchClass);
	entity_FixPowersClassHelper(e);

	// Copy the Character's Class to its current Build
	if(bBuild && NONNULL(e->pSaved))
	{
		int s = eaSize(&e->pSaved->ppBuilds);
		if(s && e->pSaved->uiIndexBuild<(U32)s && e->pSaved->ppBuilds[e->pSaved->uiIndexBuild])
		{
			COPY_HANDLE(e->pSaved->ppBuilds[e->pSaved->uiIndexBuild]->hClass,e->pChar->hClass);
			e->pSaved->uiTimestampBuildSet = timeSecondsSince2000();
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Wrapper for trCharacter_SetClass, makes sure it's an actual change and the new class is valid
void character_SetClass(Character *pchar, const char *cpchClass)
{
	CharacterClass *pclassOld = GET_REF(pchar->hClass);
	CharacterClass *pclassNew = RefSystem_ReferentFromString(g_hCharacterClassDict,cpchClass);
	if(pclassNew && pclassNew!=pclassOld)
	{
		Entity *e = pchar->pEntParent;
		if(entGetType(e)==GLOBALTYPE_ENTITYPLAYER || entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET || entGetType(e) == GLOBALTYPE_ENTITYPUPPET)
		{
			TransactionReturnVal *pReturn;
			PowerEntityCallbackData *pData = calloc(1,sizeof(PowerEntityCallbackData));
			pData->iPartitionIdx = entGetPartitionIdx(e);
			pData->ref = entGetRef(e);

			pReturn = objCreateManagedReturnVal(CharacterSetClassCallback,pData);
			AutoTrans_trCharacter_SetClass(pReturn,GetAppGlobalType(),entGetType(e),entGetContainerID(e),cpchClass,true);
		}
		else
		{
			PowerEntityCallbackData data;
			data.iPartitionIdx = entGetPartitionIdx(e);
			data.ref = entGetRef(e);
			trCharacter_SetClass(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),cpchClass,false);
			CharacterSetClassCallback(NULL, &data);
		}
	}
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteTrainerUnlockNotify(TrainerUnlockCBData* data, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	Entity* pTrainer = entFromContainerIDAnyPartition(data->uiTrainerType,data->uiTrainerID);
	PTNodeDef* pTrainerUnlock = GET_REF(data->hNodeDef);
	if ( pTrainer==NULL )
		pTrainer = entity_GetSubEntity(entGetPartitionIdx(pEnt),pEnt,data->uiTrainerType,data->uiTrainerID);
	if ( pEnt && pTrainer && pTrainerUnlock )
	{
		const char* pchEntName = entGetLangName(pTrainer, entGetLanguage(pEnt));
		const char* pchTrainerUnlock = pTrainerUnlock ? entTranslateMessage(pEnt, GET_REF(pTrainerUnlock->pDisplayMessage.hMessage)) : NULL;
		char * estrMsg = NULL;
		estrStackCreate(&estrMsg);
		FormatMessageKey(&estrMsg, "Training_Node_Unlocked", STRFMT_STRING("name",pchEntName), STRFMT_STRING("node",pchTrainerUnlock), STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_TrainerNodeUnlocked, estrMsg, NULL, NULL);
		estrDestroy(&estrMsg);	
	}
}

