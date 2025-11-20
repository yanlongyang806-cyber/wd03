/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "SavedPetCommon.h"
#include "GameAccountDataCommon.h"
#include "gslEntityNet.h"
#include "gslExtern.h"
#include "gslEntity.h"
#include "gslLogSettings.h"
#include "StringUtil.h"
#include "gslChat.h"
#include "gslHandleMsg.h"
#include "net/net.h"
#include "GameServerLib.h"
#include "EntityIterator.h"
#include "Entity.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "gslCostume.h"
#include "EntityNet.h"
#include "EntityLib.h"
#include "structnet.h"
#include "DoorTransitionCommon.h"
#include "dynnode.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"
#include "GameStringFormat.h"
#include "timing.h"
#include "testclient_comm.h"
#include "aiLib.h"
#include "objContainer.h"
#include "EntityGrid.h"
#include "wlSkelInfo.h"
#include "dynSkeleton.h"
#include "dynDraw.h"
#include "dynNode.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "gslTransactions.h"
#include "earray.h"
#include "AttribMod.h"
#include "Character.h"
#include "Character_mods.h"
#include "Character_tick.h"
#include "Powers.h"
#include "PowerHelpers.h"
#include "PowerModes.h"
#include "PowersMovement.h"
#include "PowerSlots.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeTransactions.h"
#include "RegionRules.h"
#include "DamageTracker.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldGrid_h_ast.h"
#include "WorldLib.h"
#include "AutoGen/MapDescription_h_ast.h"
#include "gslmaptransfer.h"
#include "gslcritter.h"
#include "nemesis.h"
#include "nemesis_common.h"
#include "gslMission.h"
#include "NotifyCommon.h"
#include "interaction_common.h"
#include "gslInteractionManager.h"
#include "StringCache.h"
#include "Rand.h"
#include "Materials.h"
#include "Team.h"
#include "TeamPetsCommonStructs.h"
#include "ticketnet.h"
#include "ticketenums.h"
#include "gslSendToClient.h"
#include "Character.h"
#include "gslPowerTransactions.h"
#include "gslCommandParse.h"
#include "gslControlScheme.h"
#include "gslDoorTransition.h"
#include "gslEventSend.h"
#include "gslSpawnpoint.h"
#include "gslTray.h"
#include "gslOldEncounter.h"
#include "gslQueue.h"
#include "itemCommon.h"
#include "itemServer.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CharacterAttribs.h"
#include "transactionsystem.h"
#include "aiMovement.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "../common/autogen/GameClientLib_autogen_clientcmdwrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "autogen/ServerLib_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "Character_Target.h"
#include "Quat.h"
#include "gslSavedPet.h"
#include "SavedPetTransactions.h"
#include "LoggedTransactions.h"
#include "referencesystem.h"
#include "fileutil.h"
#include "foldercache.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "logging.h"
#include "chatCommon.h"
#include "contact_common.h"
#include "ControlScheme.h"
#include "Message.h"
#include "CostumeCommon.h"
#include "Player.h"
#include "AlgoPet.h"
#include "CommandQueue.h"
#include "aiAnimList.h"
#include "aiFormation.h"
#include "wlGroupPropertyStructs.h"
#include "gslActivityLog.h"
#include "inventoryTransactions.h"
#include "tradeCommon.h"
#include "gslPartition.h"
#include "gslSuperCritterPet.h"

#include "AutoGen/AttribMod_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/CharacterAttribs_h_ast.h"
#include "AutoGen/CharacterAttribsMinimal_h_ast.h"
#include "AutoGen/entEnums_h_ast.h"
#include "AutoGen/nemesis_common_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/ticketnet_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "Tray.h"
#include "Tray_h_ast.h"

#include "aiConfig.h"

#include "SavedPetCommon_h_ast.h"
#include "AutoGen/TeamPetsCommonStructs_h_ast.h"

#include "EntityMovementDefault.h"
#include "EntityMovementDoor.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

#define CRITTERPETIDFLAG (1 << 31)
#define CRITTERPETIDSOURCE_BITS 10
#define CRITTERPETIDBASEID_BITS 21

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

SavedPetTeamManager g_SavedPetTeamManager;

typedef struct SwapedPetCBData
{
	SavedPetCBData oldPet;
	SavedPetCBData newPet;
} SwapedPetCBData;

typedef struct SetActivePuppetCBData
{
	GlobalType	ePuppetType;
	ContainerID uiOldPuppetID;
	ContainerID	uiNewPuppetID;
} SetActivePuppetCBData;


int bPetTransactionDebug = false;

AUTO_CMD_INT(bPetTransactionDebug, EnablePetTransactionDebug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE;

AUTO_TRANS_HELPER
	ATR_LOCKS(pOwner, ".Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Pppreferredpetids");
bool trh_RemoveSavedPetByID(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, int iPetContainerID)
{
	NOCONST(PetRelationship) *conRelation;
	int i, j;
	bool bFound = false;
	U32 iPetID = 0;

	// Remove pet from puppet list
	if(NONNULL(pOwner->pSaved->pPuppetMaster))
	{
		for (i=0; i < eaSize(&pOwner->pSaved->pPuppetMaster->ppPuppets); i++)
		{
			NOCONST(PuppetEntity) *conPuppet = pOwner->pSaved->pPuppetMaster->ppPuppets[i];
			if(conPuppet->curID == (ContainerID)iPetContainerID)
			{
				eaRemove(&pOwner->pSaved->pPuppetMaster->ppPuppets,i);
				StructDestroyNoConst(parse_PuppetEntity,conPuppet);
				break;
			}
		}
	}

	// Remove pet from owned list
	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		conRelation = pOwner->pSaved->ppOwnedContainers[i];
		if (conRelation->conID == (ContainerID)iPetContainerID)
		{
			iPetID = pOwner->pSaved->ppOwnedContainers[i]->uiPetID;
			eaRemove(&pOwner->pSaved->ppOwnedContainers, i);
			StructDestroyNoConst(parse_PetRelationship, conRelation);
			bFound = true;

			// pOwner has lost a pet.  We need to reset all PowerIDs on the owner and all
			//  of its owned containers that were from the lost pet.
			eaiPush(&pOwner->pSaved->piPetIDsRemovedFixup,iPetID);
#ifdef GAMESERVER
			QueueRemoteCommand_RemoteEntityPetIDsRemovedFixup(ATR_RESULT_SUCCESS,pOwner->myEntityType,pOwner->myContainerID,pOwner->myEntityType,pOwner->myContainerID);
#endif
			// Remove the pet from the preferred pet list
			ea32FindAndRemove(&pOwner->pSaved->ppPreferredPetIDs, iPetContainerID);
			break;
		}
	}

	// Remove pet from prop slots on the owner
	for (i = 0; i < eaSize(&pOwner->pSaved->ppAlwaysPropSlots); i++)
	{
		NOCONST(AlwaysPropSlot)* pPropSlot = pOwner->pSaved->ppAlwaysPropSlots[i];
		if (pPropSlot->iPetID == iPetID)
		{
			pPropSlot->iPetID = 0;
		}
	}

	// Remove the pet from prop slots on each puppet
	if (NONNULL(pOwner->pSaved->pPuppetMaster))
	{
		for (i = eaSize(&pOwner->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			NOCONST(PuppetEntity) *pPuppet = pOwner->pSaved->pPuppetMaster->ppPuppets[i];
			for (j = eaSize(&pPuppet->ppSavedPropSlots)-1; j >= 0; j--)
			{
				NOCONST(AlwaysPropSlot)* pPropSlot = pPuppet->ppSavedPropSlots[j];
				if (pPropSlot->iPetID == iPetID)
				{
					pPropSlot->iPetID = 0;
				}
			}
		}
	}
	return bFound;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOwner, ".Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Pppreferredpetids")
	ATR_LOCKS(pPet, ".Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Hclass");
bool trhRemoveSavedPet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pOwner, ATH_ARG NOCONST(Entity)* pPet)
{
	NOCONST(PuppetEntity)* pPuppet;
	CharacterClass *pClass = SAFE_GET_REF2(pPet, pChar, hClass);
	CharClassCategorySet *pSet = CharClassCategorySet_getCategorySetFromClass(pClass);
	if (ISNULL(pPet) || ISNULL(pPet->pSaved) || ISNULL(pOwner) || ISNULL(pOwner->pSaved))
	{
		return false;
	}
	if (pPet->pSaved->conOwner.containerID != pOwner->myContainerID ||
		pPet->pSaved->conOwner.containerType != pOwner->myEntityType)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Can't remove %s[%d] from being a child of %s[%d], already owned by %s[%d]",
			GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
			GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID,
			GlobalTypeToName(pPet->pSaved->conOwner.containerType),pPet->pSaved->conOwner.containerID);
		return false;
	}

	pPuppet = trhSavedPet_GetPuppetFromContainerID(pOwner, pPet->myContainerID);
	if (NONNULL(pPuppet) && (!pSet || !pSet->bAllowDeletionWhileActive))
	{
		if (pPuppet->eState == PUPPETSTATE_ACTIVE)
		{
			TRANSACTION_APPEND_LOG_FAILURE("Can't remove %s[%d] from being a child of %s[%d]. Can't remove active puppets!",
				GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
				GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
			return false;
		}
	}

	pPet->pSaved->conOwner.containerType = 0;
	pPet->pSaved->conOwner.containerID = 0;

	trh_RemoveSavedPetByID(ATR_PASS_ARGS, pOwner, pPet->myContainerID);

	TRANSACTION_APPEND_LOG_SUCCESS("%s[%d] no longer a child of %s[%d]", 
		GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
		GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Psaved.Pipetidsremovedfixup, .Psaved.Pppreferredpetids, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots")
	ATR_LOCKS(pPet, ".Psaved.Conowner.Containertype, .Psaved.Conowner.Containerid, .Pchar.Hclass");
enumTransactionOutcome trRemoveSavedPet(ATR_ARGS, NOCONST(Entity)* pOwner, NOCONST(Entity)* pPet, U32 uiNewActivePetID)
{
	NOCONST(PuppetEntity) *pPuppet = NONNULL(pPet) ? trhSavedPet_GetPuppetFromContainerID(pOwner, pPet->myContainerID) : NULL;
	NOCONST(PuppetEntity) *pNewActivePuppet = trhSavedPet_GetPuppetFromContainerID(pOwner, uiNewActivePetID);
	bool bSetActive = false;
	if (NONNULL(pNewActivePuppet) && pPuppet->eState == PUPPETSTATE_ACTIVE) {
		if (pNewActivePuppet->eState != PUPPETSTATE_ACTIVE) {
			bSetActive = true;
		} else {
			TRANSACTION_RETURN_LOG_FAILURE("Invalid new active pet argument");
		}
	}
	if(trhRemoveSavedPet(ATR_PASS_ARGS, pOwner, pPet)) {
		if (bSetActive) {
			pNewActivePuppet->eState = PUPPETSTATE_ACTIVE;
		}
		TRANSACTION_RETURN_LOG_SUCCESS("Saved pet, %s[%d], successfully removed from %s[%d]", 
			GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
			GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
	} else {
		if (NONNULL(pOwner) && NONNULL(pPet)) {
			TRANSACTION_RETURN_LOG_FAILURE("Unable to remove saved pet %s[%d] from %s[%d]", 
				GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
				GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
		} else {
			TRANSACTION_RETURN_LOG_FAILURE("Unable to remove saved pet");
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void SavedPet_Remove( Entity* pPlayerEnt, ContainerID uiPetID )
{
	Entity* pEnt = entity_GetSubEntity( entGetPartitionIdx(pPlayerEnt), pPlayerEnt, GLOBALTYPE_ENTITYSAVEDPET, uiPetID );
	PetDef* pDef = pEnt && pEnt->pCritter ? GET_REF(pEnt->pCritter->petDef) : NULL;
	PetRelationship *pRelation;
	int i;
	
	if ( !pDef || !pDef->bCanRemove || !pPlayerEnt->pSaved )
	{
		return;
	}

	//Make sure pet is not active and not a puppet
	for (i = eaSize(&pPlayerEnt->pSaved->ppOwnedContainers)-1; i >= 0 ; --i)
	{
		pRelation = pPlayerEnt->pSaved->ppOwnedContainers[i];
		if (GET_REF(pRelation->hPetRef) && 
			(pRelation->conID == uiPetID) && 
			(GET_REF(pRelation->hPetRef)->myEntityType == GLOBALTYPE_ENTITYSAVEDPET))
		{
			if (SavedPet_IsPetAPuppet(pPlayerEnt, pRelation) || pRelation->curEntity) 
			{
				// Pet is a puppet or is currently active
				break;
			}
		}
	}
	if (i >= 0) 
	{
		// Pet is a puppet or is currently active
		return;
	}

	// Make sure the pet isn't being offered in a trade
	if(trade_IsPetBeingTraded(pEnt, pPlayerEnt))
		return;

	gslDestroySavedPet(pEnt);
}

static bool SavedPet_FlagRequestMaxCheck(Entity *eMasterEntity,PetRelationship *pRelationShip, U32 uiPropEntID, bool bTeamRequest, int iSlotID, AlwaysPropSlotCategory ePropCategory)
{
	Entity *pSavedPet = SavedPet_GetEntity(entGetPartitionIdx(eMasterEntity), pRelationShip);

	if (!pSavedPet)
		return false;
	if (pSavedPet->pChar && eaSize(&pSavedPet->pChar->ppTraining))
		return false;
	if (eMasterEntity->pChar->uiTimeCombatExit) //Cannot change flags around while in combat
		return false;
	
	return SavedPet_th_FlagRequestMaxCheck(CONTAINER_NOCONST(Entity, eMasterEntity),CONTAINER_NOCONST(Entity, pSavedPet),CONTAINER_NOCONST(PetRelationship, pRelationShip),uiPropEntID,bTeamRequest,iSlotID,ePropCategory,false);
}

AUTO_TRANS_HELPER;
int trhFindPowerByID(ATH_ARG NOCONST(Power)** ppPowers, U32 uiID)
{
	int i;
	for (i = eaSize(&ppPowers)-1; i >= 0; i--)
	{
		if (ppPowers[i]->uiID == uiID)
		{
			return i;
		}
	}
	return -1;
}
#define FindPowerByID(ppPowers, uiID) trhFindPowerByID((NOCONST(Power)**)ppPowers, uiID)

bool ent_PetGetPropPowersToSave(Entity* pOwner, Entity* pPetEnt, PetRelationship* pPetRel, AlwaysPropSlotDef* pPropDef, PropPowerSaveData*** pppSaveData)
{
	Power** ppPropPowers = NULL;
	int i, iCount = 0;
	ent_FindAllPropagatePowers(pPetEnt, pPetRel, pPropDef, NULL, &ppPropPowers, true);
	for (i = eaSize(&ppPropPowers)-1; i >= 0; i--)
	{
		Power* pPropPower = ppPropPowers[i];
		int iPowIdx = FindPowerByID(pOwner->pChar->ppPowersPropagation,pPropPower->uiID);
		Power* pPower = eaGet(&pOwner->pChar->ppPowersPropagation, iPowIdx);
		if (pPower && !nearSameF32(pPower->fTimeRecharge,pPropPower->fTimeRecharge))
		{
			PropPowerSaveData* pData;
			pData = eaGetStruct(pppSaveData, parse_PropPowerSaveData, iCount++);
			pData->uiPowerID = pPower->uiID;
			pData->fRecharge = pPower->fTimeRecharge;
		}
	}
	eaDestroy(&ppPropPowers);
	if (iCount > 0)
	{
		eaSetSizeStruct(pppSaveData, parse_PropPowerSaveData, iCount);
		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
NOCONST(Power)* trhEntFindPowerInPowerTreesByID(ATH_ARG NOCONST(Entity)* pEnt, U32 uiID)
{
	int i, j, k;
	for (i = eaSize(&pEnt->pChar->ppPowerTrees)-1; i >= 0; i--)
	{
		NOCONST(PowerTree)* pTree = pEnt->pChar->ppPowerTrees[i];
		for (j = eaSize(&pTree->ppNodes)-1; j >= 0; j--)
		{
			NOCONST(PTNode)* pNode = pTree->ppNodes[j];

			if (pNode->bEscrow || eaSize(&pNode->ppPowers) == 0)
				continue;

			if (pNode->ppPowers[0])
			{
				PowerDef* pDef = GET_REF(pNode->ppPowers[0]->hDef);
				if (pDef && pDef->eType == kPowerType_Enhancement)
				{
					NOCONST(Power)* pPower = pNode->ppPowers[eaSize(&pNode->ppPowers)-1];
					if (pPower->uiID == uiID)
					{
						return pPower;
					}
					continue;
				}
			}
			for (k = eaSize(&pNode->ppPowers)-1; k >= 0; k--)
			{
				PowerDef* pDef = pNode->ppPowers[k] ? GET_REF(pNode->ppPowers[k]->hDef) : NULL;

				if (!pDef || ISNULL(pNode->ppPowers[k]))
					continue;

				if (pNode->ppPowers[k]->uiID == uiID)
				{
					return pNode->ppPowers[k];
				}
				if (!pDef || pDef->eType != kPowerType_Enhancement)
				{
					break;
				}
			}
		}
	}
	return NULL;
}

AUTO_TRANS_HELPER ATR_LOCKS(pEnt, ".Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees");
bool trhEntSavePropPowerRecharges(ATR_ARGS, 
								  ATH_ARG NOCONST(Entity)* pEnt,
								  PropPowerSaveList* pSaveList)
{
	int i;
	if (ISNULL(pEnt->pChar))
	{
		return false;
	}
	for (i = eaSize(&pSaveList->eaData)-1; i >= 0; i--)
	{
		NOCONST(Power)* pPower;
		int iPowIdx;
		PropPowerSaveData* pData = pSaveList->eaData[i];
		iPowIdx = trhFindPowerByID(pEnt->pChar->ppPowersPersonal, pData->uiPowerID);
		if (iPowIdx >= 0)
		{
			pPower = pEnt->pChar->ppPowersPersonal[iPowIdx];
			pPower->fTimeRecharge = pData->fRecharge;
			continue;
		}
		iPowIdx = trhFindPowerByID(pEnt->pChar->ppPowersClass, pData->uiPowerID);
		if (iPowIdx >= 0)
		{
			pPower = pEnt->pChar->ppPowersClass[iPowIdx];
			pPower->fTimeRecharge = pData->fRecharge;
			continue;
		}
		iPowIdx = trhFindPowerByID(pEnt->pChar->ppPowersSpecies, pData->uiPowerID);
		if (iPowIdx >= 0)
		{
			pPower = pEnt->pChar->ppPowersSpecies[iPowIdx];
			pPower->fTimeRecharge = pData->fRecharge;
			continue;
		}
		pPower = trhEntFindPowerInPowerTreesByID(pEnt, pData->uiPowerID);
		if (NONNULL(pPower))
		{
			pPower->fTimeRecharge = pData->fRecharge;
			continue;
		}
	}
	return true;
}

AUTO_TRANSACTION ATR_LOCKS(pEnt, ".Pchar.Pppowersspecies, Pchar.Pppowersspecies[AO], .Pchar.Pppowertrees, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies[AO], .Pchar.Pppowerspersonal[AO], .Pchar.Pppowersclass[AO]");
enumTransactionOutcome trEntSavePropPowerRecharges(ATR_ARGS, 
												   NOCONST(Entity)* pEnt,
												   PropPowerSaveList* pSaveList)
{
	if(!trhEntSavePropPowerRecharges(ATR_PASS_ARGS, pEnt, pSaveList)) {
		TRANSACTION_RETURN_LOG_FAILURE("Unable to save propagated recharge times to %s[%d]", 
			GlobalTypeToName(pEnt->myEntityType), pEnt->myContainerID);
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Successfully saved propagated recharge times to %s[%d]", 
		GlobalTypeToName(pEnt->myEntityType), pEnt->myContainerID);
}

AUTO_TRANSACTION
ATR_LOCKS(pOwner, ".Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppets")
ATR_LOCKS(pSavedPet, ".Pchar.Pppowersclass[AO], .Psaved.Ipetid, .Pchar.Pppowersspecies[AO], .Pchar.Hclass, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .Pchar.Pppowerspersonal[AO]");
enumTransactionOutcome trChangeSavedPetState(ATR_ARGS, 
											 NOCONST(Entity)* pOwner, 
											 NOCONST(Entity) *pSavedPet, 
											 int eState, PropEntIDs *pPropEntIDs, int bTeamRequest, int iSlotID, int ePropCategory,
											 PropPowerSaveList* pSavePowerList)
{
	NOCONST(PetRelationship) *conRelation;
	int i, j;

	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		conRelation = pOwner->pSaved->ppOwnedContainers[i];
		if (conRelation->conID == pSavedPet->myContainerID)
		{	
			bool bFailure = false;
			conRelation->eState = eState;

			for(j = 0; j < ea32Size(&pPropEntIDs->eauiPropEntIDs); j++)
			{
				U32 uiID = pPropEntIDs->eauiPropEntIDs[j];
				
				if(SavedPet_th_FlagRequestMaxCheck(pOwner,pSavedPet,conRelation,uiID,bTeamRequest,iSlotID,ePropCategory,true))
				{
					if (pSavePowerList && eaSize(&pSavePowerList->eaData))
					{
						//copy recharge times from owner to pet
						trhEntSavePropPowerRecharges(ATR_EMPTY_ARGS, pSavedPet, pSavePowerList);
					}
				}
				else
				{
					bFailure = true;
				}
			}

			for(j=0;j<eaSize(&pOwner->pSaved->ppAlwaysPropSlots);j++)
			{
				if(pOwner->pSaved->ppAlwaysPropSlots[j]->iPetID == conRelation->uiPetID
					&& ea32Find(&pPropEntIDs->eauiPropEntIDs,pOwner->pSaved->ppAlwaysPropSlots[j]->iPuppetID) == -1)
				{
					pOwner->pSaved->ppAlwaysPropSlots[j]->iPetID = 0;
				}
			}

			if (bFailure)
			{
				TRANSACTION_RETURN_LOG_FAILURE("%s[%d]:%s[%d] was now allowed to set status", 
					GlobalTypeToName(pSavedPet->myEntityType), pSavedPet->myContainerID, 
					GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
			}

			TRANSACTION_RETURN_LOG_SUCCESS("%s[%d] state updated", 
				GlobalTypeToName(pSavedPet->myEntityType), pSavedPet->myContainerID);
		}		
	}

	TRANSACTION_RETURN_LOG_FAILURE("%s[%d] was not a child of %s[%d]", 
		GlobalTypeToName(pSavedPet->myEntityType), pSavedPet->myContainerID, 
		GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);

}

AUTO_TRANS_HELPER
ATR_LOCKS(pOwner, ".Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppets")
ATR_LOCKS(pOldPetEnt, ".Pchar.Pppowersclass, .Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowersspecies, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .Pchar.Pppowerspersonal")
ATR_LOCKS(pNewPetEnt, ".Pchar.Pppowersspecies, .Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowertrees, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass");
bool trhSwapSavedPetState(ATH_ARG NOCONST(Entity)* pOwner, 
						  ATH_ARG NOCONST(Entity)* pOldPetEnt, 
						  ATH_ARG NOCONST(Entity)* pNewPetEnt, 
						  int iSlotID, int eState, U32 uiPropEntID, int bTeamRequest, int ePropCategory,
						  PropPowerSaveList* pSavePowerList)
{
	NOCONST(PetRelationship)* pOldPetRel = NULL;
	NOCONST(PetRelationship)* pNewPetRel = NULL;
	int i, iNewSlot = -1, iOldSlot = -1;
	bool bSuccess = true;

	if ( NONNULL(pOwner) && NONNULL(pOwner->pSaved) && NONNULL(pNewPetEnt) && NONNULL(pNewPetEnt->pSaved) )
	{
		for (i=eaSize(&pOwner->pSaved->ppOwnedContainers)-1;i>=0;--i)
		{
			NOCONST(PetRelationship)* pPet = pOwner->pSaved->ppOwnedContainers[i];
			if ( pPet->conID == pNewPetEnt->myContainerID )
			{
				pNewPetRel = pPet;
				break;
			}
		}
	}
	
	if ( ISNULL(pNewPetRel) )
		bSuccess = false;

	if ( bSuccess )
	{
		iNewSlot = AlwaysPropSlot_trh_FindByPetID(pOwner, pNewPetEnt->pSaved->iPetID, uiPropEntID, ePropCategory);
	}

	if ( bSuccess && NONNULL(pOldPetEnt) && NONNULL(pOldPetEnt->pSaved) )
	{
		for(i=eaSize(&pOwner->pSaved->ppOwnedContainers)-1;i>=0;--i)
		{
			NOCONST(PetRelationship)* pPet = pOwner->pSaved->ppOwnedContainers[i];
			if ( pPet->conID == pOldPetEnt->myContainerID )
			{
				pOldPetRel = pPet;
				break;
			}
		}
		if ( ISNULL(pOldPetRel) )
			bSuccess = false;

		if ( bSuccess )
		{
			iOldSlot = AlwaysPropSlot_trh_FindByPetID(pOwner, pOldPetEnt->pSaved->iPetID, uiPropEntID, ePropCategory);

			if ( iOldSlot < 0 )
				bSuccess = false;
		}
	}
	else if ( bSuccess && iSlotID >= 0 )
	{
		int iOldPetID = -1;
		iOldSlot = AlwaysPropSlot_trh_FindBySlotID(pOwner, iSlotID);

		if (iOldSlot >= 0)
			iOldPetID = pOwner->pSaved->ppAlwaysPropSlots[iOldSlot]->iPetID;
		else
			bSuccess = false;
	}
	else
	{
		bSuccess = false;
	}

	if ( bSuccess )
	{
		if ( iOldSlot >= 0 )
		{
			pOwner->pSaved->ppAlwaysPropSlots[iOldSlot]->iPetID = 0;
		}
		if ( iNewSlot >= 0 )
		{
			pOwner->pSaved->ppAlwaysPropSlots[iNewSlot]->iPetID = 0;
		}
		if ( NONNULL(pOldPetRel) )
		{
			pOldPetRel->eState = eState;
			if (bTeamRequest)
				pOldPetRel->bTeamRequest = false;
			if ( iNewSlot >= 0 )
			{
				int iNewSlotID = pOwner->pSaved->ppAlwaysPropSlots[iNewSlot]->iSlotID;
				if(SavedPet_th_FlagRequestMaxCheck(pOwner,pOldPetEnt,pOldPetRel,uiPropEntID,bTeamRequest,iNewSlotID,ePropCategory,true))
				{
					pOldPetRel->bTeamRequest |= bTeamRequest;
				}
				else
				{
					// If the old pet cannot be swapped, attempt to remove the old pet
					if(!SavedPet_th_FlagRequestMaxCheck(pOwner,pOldPetEnt,pOldPetRel,uiPropEntID,bTeamRequest,-1,ePropCategory,false))
					{
						bSuccess = false;
					}
				}
			}
		}
		if ( NONNULL(pNewPetRel) && bSuccess )
		{
			pNewPetRel->eState = eState;
			if (bTeamRequest)
				pNewPetRel->bTeamRequest = false;
			if ( iOldSlot >= 0 )
			{
				int iOldSlotID = pOwner->pSaved->ppAlwaysPropSlots[iOldSlot]->iSlotID;
				if(SavedPet_th_FlagRequestMaxCheck(pOwner,pNewPetEnt,pNewPetRel,uiPropEntID,bTeamRequest,iOldSlotID,ePropCategory,true))
				{
					pNewPetRel->bTeamRequest |= bTeamRequest;
				}
				else
				{
					bSuccess = false;
				}
			}
		}
		if (bSuccess && NONNULL(pOldPetEnt) && iNewSlot<0 && pSavePowerList && eaSize(&pSavePowerList->eaData))
		{
			bSuccess = trhEntSavePropPowerRecharges(ATR_EMPTY_ARGS, pOldPetEnt, pSavePowerList);
		}
	}
	return bSuccess;
}

AUTO_TRANSACTION
ATR_LOCKS(pOwner, ".Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppownedcontainers")
ATR_LOCKS(pOldPetEnt, ".Pchar.Pppowersclass[AO], .Pchar.Pppowersspecies[AO], .Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .Pchar.Pppowerspersonal[AO]")
ATR_LOCKS(pNewPetEnt, ".Pchar.Pppowersspecies, .Pchar.Pppowertrees, .Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass");
enumTransactionOutcome trSwapSavedPetStateEnts(ATR_ARGS, 
											   NOCONST(Entity)* pOwner, 
											   NOCONST(Entity)* pOldPetEnt, 
											   NOCONST(Entity)* pNewPetEnt, 
											   int eState, int uiPropEntID, int bTeamRequest, int ePropCategory,
											   PropPowerSaveList* pSavePowerList)
{
	if (ISNULL(pOwner) || ISNULL(pOldPetEnt) || ISNULL(pNewPetEnt))
	{
		TRANSACTION_RETURN_LOG_FAILURE("trSwapSavedPetStateEnts failed because not all entities were valid");
	}
	
	if (trhSwapSavedPetState(pOwner,pOldPetEnt,pNewPetEnt,-1,eState,uiPropEntID,bTeamRequest,ePropCategory,pSavePowerList))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Successfully swapped pet states %s[%d] and %s[%d] for owner %s[%d]", 
			GlobalTypeToName(pOldPetEnt->myEntityType), pOldPetEnt->myContainerID,
			GlobalTypeToName(pNewPetEnt->myEntityType), pNewPetEnt->myContainerID,
			GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
	}

	TRANSACTION_RETURN_LOG_FAILURE("Could not swap pet states %s[%d] and %s[%d] for owner %s[%d]", 
		GlobalTypeToName(pOldPetEnt->myEntityType), pOldPetEnt->myContainerID, 
		GlobalTypeToName(pNewPetEnt->myEntityType), pNewPetEnt->myContainerID,
		GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
}

AUTO_TRANSACTION
ATR_LOCKS(pOwner, ".Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppownedcontainers")
ATR_LOCKS(pNewPetEnt, ".Pchar.Pppowersspecies, .Pchar.Pppowertrees, .Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass");
enumTransactionOutcome trSwapSavedPetState(ATR_ARGS, 
										   NOCONST(Entity)* pOwner, 
										   NOCONST(Entity)* pNewPetEnt, 
										   int iSlotID, int eState, U32 uiPropEntID, int bTeamRequest, int ePropCategory,
										   PropPowerSaveList* pSavePowerList)
{
	if (ISNULL(pOwner) || ISNULL(pNewPetEnt))
	{
		TRANSACTION_RETURN_LOG_FAILURE("trSwapSavedPetState failed because not all entities were valid");
	}

	if (trhSwapSavedPetState(pOwner,NULL,pNewPetEnt,iSlotID,eState,uiPropEntID,bTeamRequest,ePropCategory,pSavePowerList))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Successfully swapped pet state %s[%d] for owner %s[%d]", 
			GlobalTypeToName(pNewPetEnt->myEntityType), pNewPetEnt->myContainerID,
			GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
	}

	TRANSACTION_RETURN_LOG_FAILURE("Could not swap pet state %s[%d] for owner %s[%d]", 
		GlobalTypeToName(pNewPetEnt->myEntityType), pNewPetEnt->myContainerID,
		GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Psaved.Ppownedcontainers");
enumTransactionOutcome trSetPrimarySavedPet(ATR_ARGS, NOCONST(Entity)* pOwner, int petType, int petID)
{
	bool bFound = false;
	NOCONST(PetRelationship) *conRelation;
	int i;

	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		conRelation = pOwner->pSaved->ppOwnedContainers[i];
		if (conRelation->conID == (ContainerID)petID)
		{
			conRelation->eRelationship = CONRELATION_PRIMARY_PET;			
			bFound = true;
		}
		else
		{
			conRelation->eRelationship = CONRELATION_PET;
		}

	}

	if (bFound)
	{
		TRANSACTION_RETURN_LOG_SUCCESS("%s[%d] now primary pet", 
			GlobalTypeToName(petType), petID);
	}
	TRANSACTION_RETURN_LOG_FAILURE("%s[%d] was not a child of %s[%d]", 
		GlobalTypeToName(petType), petID, 
		GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);

}

static void PetContainerMove_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{	
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			int i;
			for (i = 0; i < returnVal->iNumBaseTransactions; i++)
			{			
				if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					if (gbEnablePetAndPuppetLogging)
					{
						const char* pchReason = cbData->pchReason[0] ? cbData->pchReason : "UnknownPetContainerMove";
						objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, pchReason, 
							"Failed because: %s",returnVal->pBaseReturnVals[i].returnString);
					}
					free(cbData);
					return;
				}
			}
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			if (gbEnablePetAndPuppetLogging)
			{
				GlobalType newType = cbData->iPetContainerType;
				ContainerID newID = cbData->iPetContainerID;

				objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "UnSummonPet",
					"Child EntitySavedPet[%d] returned", newID);
			}

			free(cbData);
			return;
		}	
	}
}

static void PetStateSet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData);

static void PetCreate_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		if (gbEnablePetAndPuppetLogging)
		{
			objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "SetPetState",
				"Child EntitySavedPet[%d] can't be found", cbData->iPetContainerID);
		}
		free(cbData);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		sprintf_s(SAFESTR(cbData->pchReason), "PetCreate");
		objRequestContainerMove(objCreateManagedReturnVal(PetContainerMove_CB, cbData),
			cbData->iPetContainerType, cbData->iPetContainerID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
	}
}

void CreatePuppet_PostPetCreate(TransactionReturnVal *returnVal, SavedPetCBData *pPetData);
void CreatePuppetMaster_PostPetCreate(TransactionReturnVal *returnVal, SavedPetCBData *pPetData);

static void AddSavedPet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	Entity *pOwner = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);
	Entity *pSavedPetEnt = entFromContainerIDAnyPartition(cbData->iPetContainerType, cbData->iPetContainerID);
	switch(returnVal->eOutcome)
	{
	xcase TRANSACTION_OUTCOME_FAILURE:
		{
			// Destroy the pet that we just created, to avoid leaking the pet
			if (pSavedPetEnt){
				objRequestContainerDestroy(NULL, cbData->iPetContainerType, cbData->iPetContainerID, objServerType(), objServerID());
			} else {
				objRequestContainerDestroy(NULL, cbData->iPetContainerType, cbData->iPetContainerID, GLOBALTYPE_OBJECTDB, 0);
			}
		}
	xcase TRANSACTION_OUTCOME_SUCCESS:
		if (pOwner && pSavedPetEnt)
		{
			gslActivity_AddPetAddEntry(pOwner, pSavedPetEnt);
			if (cbData->bIsNemesis){
				// See CreateNemesis_CB
			}else{
				char *pch = NULL;
				entFormatGameMessageKey(pOwner, &pch, "Pet_Add_Success",
					STRFMT_ENTITY_KEY("Owner", pOwner),
					STRFMT_ENTITY_KEY("Pet", pSavedPetEnt),
					STRFMT_END);
				notify_NotifySend(pOwner, kNotifyType_PetAdded, pch, pSavedPetEnt->debugName, "");
				estrDestroy(&pch);
			}
		}
	}

	if (pOwner && returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		PuppetEntity* pPuppet = SavedPet_GetPuppetFromContainerID(pOwner, cbData->iPetContainerID);
		if (!pPuppet || pPuppet->eState != PUPPETSTATE_ACTIVE)
		{
			PetCreatedInfo pci;
			PetRelationship *rel = NULL;
			pci.iPetID = cbData->iPetContainerID;
			pci.iPetType = cbData->iPetContainerType;
			pci.iPetIsPuppet = (cbData && (cbData->CallbackFunc == CreatePuppet_PostPetCreate || cbData->CallbackFunc == CreatePuppetMaster_PostPetCreate)) ? 1 : 0;
			ClientCmd_PetCommands_SendClientPetCreated(pOwner, &pci);
		
			if (pOwner->pSaved)
			{
				pOwner->pSaved->uNewPetID = cbData->iPetContainerID;
			}
		}
	}

	if (cbData && cbData->CallbackFunc){
		cbData->CallbackFunc(returnVal, cbData);
	} else {
		free(cbData);
	}
}

static void CreatePet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);

	if (!owningEntity)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			GlobalType newType = GLOBALTYPE_ENTITYSAVEDPET;
			ContainerID newID = atoi(returnVal->pBaseReturnVals[0].returnString);

			objRequestContainerDestroy(NULL,newType,newID,GLOBALTYPE_OBJECTDB,0);
		}
		
		free(cbData);
		return;
	}

	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			int i;
			for (i = 0; i < returnVal->iNumBaseTransactions; i++)
			{			
				if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					if (gbEnablePetAndPuppetLogging)
					{
						entLog(LOG_CONTAINER, owningEntity, "CreatePet", 
							"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
					}
					free(cbData);
					return;
				}
			}
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(owningEntity);
			GlobalType newType = GLOBALTYPE_ENTITYSAVEDPET;
			ContainerID newID = atoi(returnVal->pBaseReturnVals[0].returnString);
			bool playerOwner = cbData->ownerType == cbData->iItemOwnerType && cbData->ownerID == cbData->iItemOwnerID;
			ItemChangeReason reason = {0};

			cbData->iPetContainerID = newID;
			cbData->iPetContainerType = newType;

			// Add the Saved Pet to the Player

			if (cbData->bIsNemesis){
				inv_FillItemChangeReason(&reason, owningEntity, "Pets:CreateNemesis", NULL);

				AutoTrans_trAddNemesisSavedPet(LoggedTransactions_CreateManagedReturnValEnt("CreateNemesisPet", owningEntity, AddSavedPet_CB, cbData), GLOBALTYPE_GAMESERVER, 
						cbData->ownerType, cbData->ownerID, 
						newType, newID, 
						CONRELATION_PET, cbData->newState, &cbData->newPropEntIDs, cbData->bNewTeamRequest, cbData->eNemesisState, &reason, pExtract);
			} else {
				inv_FillItemChangeReason(&reason, owningEntity, "Pets:CreatePet", NULL);

				AutoTrans_trAddSavedPet(LoggedTransactions_CreateManagedReturnValEnt("CreatePet", owningEntity, AddSavedPet_CB, cbData), GLOBALTYPE_GAMESERVER, 
					cbData->ownerType, cbData->ownerID, 
					playerOwner?0:cbData->iItemOwnerType, playerOwner?0:cbData->iItemOwnerID, 
					newType, newID, 
					CONRELATION_PRIMARY_PET, cbData->newState, &cbData->newPropEntIDs, cbData->bNewTeamRequest, cbData->uiItemId, 
					cbData->bMakeActive, &reason, pExtract);
			}

			if (gbEnablePetAndPuppetLogging) {
				entLog(LOG_CONTAINER, owningEntity, "CreatePet", 
					"Child EntitySavedPet[%d] created", newID);
			}

			return;
		}	
	}
}

bool gslCreateSavedPetForOwner(Entity *pOwner, Entity *pEntityToCopy)
{
	NOCONST(Entity) *pTempEntity;
	SavedPetCBData *cbData = calloc(sizeof(SavedPetCBData), 1);
	NOCONST(SavedMapDescription) *pNewDesc;
	Quat spawnRot;

	if (!pOwner || entGetType(pOwner) != GLOBALTYPE_ENTITYPLAYER || !pEntityToCopy)
	{
		free(cbData);
		return false;
	}
	pTempEntity = StructCloneWithCommentDeConst(parse_Entity, pEntityToCopy, "Temp entity for creating saved pet");
	pTempEntity->myEntityType = GLOBALTYPE_ENTITYSAVEDPET;
	pTempEntity->myContainerID = 0;
	if (!pTempEntity->pSaved)
		pTempEntity->pSaved = StructCreateNoConst(parse_SavedEntityData);

	pTempEntity->pSaved->conOwner.containerType = pOwner->myEntityType;
	pTempEntity->pSaved->conOwner.containerID = pOwner->myContainerID;
	pTempEntity->pSaved->uFixupVersion = CURRENT_ENTITY_FIXUP_VERSION;
	pTempEntity->pSaved->uGameSpecificFixupVersion = gameSpecificFixup_Version();
	COPY_HANDLE(pTempEntity->hFaction, pOwner->hFaction);
	if(IS_HANDLE_ACTIVE(pOwner->hFactionOverride))
		COPY_HANDLE(pTempEntity->hFactionOverride, pOwner->hFactionOverride);

	pNewDesc = StructCloneVoid(parse_MapDescription, &gGSLState.gameServerDescription.baseMapDescription);
	entGetPos(pOwner, pNewDesc->spawnPos);
	entGetRot(pOwner, spawnRot);
	quatToPYR(spawnRot, pNewDesc->spawnPYR);

	pNewDesc->spawnPos[0] += 0.01f * randomF32();
	pNewDesc->spawnPos[2] += 0.01f * randomF32();
	pNewDesc->iPartitionID = partition_IDFromIdx(entGetPartitionIdx(pOwner));

	entity_SetCurrentMap(pTempEntity, (SavedMapDescription *)pNewDesc);

	sprintf(pTempEntity->pSaved->savedName, "%s", pEntityToCopy->pSaved->savedName);
	cbData->ownerType = entGetType(pOwner);
	cbData->ownerID = entGetContainerID(pOwner);
	cbData->iPetContainerType = entGetType(pEntityToCopy);
	cbData->iPetContainerID = entGetContainerID(pEntityToCopy);
	cbData->CallbackFunc = NULL;
	cbData->newState = OWNEDSTATE_ACTIVE;
	cbData->bNewTeamRequest = true;

	objRequestContainerCreate(objCreateManagedReturnVal(CreatePet_CB, cbData),
		GLOBALTYPE_ENTITYSAVEDPET, pTempEntity,  objServerType(), objServerID());

	StructDestroyNoConst(parse_Entity, pTempEntity);
	return true;
}

static void Entity_MakePetNameUnique(Entity *pParentEnt, NOCONST(Entity) *pPetEnt, bool bNewEnt)
{
	char *p = pPetEnt->pSaved->savedName;
	int j, len, val, v1, v2;
	int save = -1;
	int iPartitionIdx = entGetPartitionIdx(pParentEnt);
	S32 iOwnedPetSize = eaSize(&pParentEnt->pSaved->ppOwnedContainers);

	for ( j = 0; j < iOwnedPetSize; j++ )
	{
		Entity *pEntity = SavedPet_GetEntity(iPartitionIdx, pParentEnt->pSaved->ppOwnedContainers[j]);
		if (pEntity && pEntity->pSaved)
		{
			if (!stricmp(pEntity->pSaved->savedName,p))
			{
				if ((bNewEnt) || (pPetEnt->myContainerID != pEntity->myContainerID || pPetEnt->myEntityType != pEntity->myEntityType))
				{
					if (j == save) return; //Fail!!! This is not a new Entity; Set bNewEnt to false.

					len = (int)strlen(p);
					while (len)
					{
						--len;
						if (p[len] < '0' || p[len] > '9')
						{
							++len;
							break;
						}
					}

					if (len > MAX_NAME_LEN-4) len = MAX_NAME_LEN-4; //At most 3 digits
					val = 0;
					if (p[len])
					{
						val = p[len] - '0';
						if (p[(len+1)])
						{
							val = (val * 10) + p[(len+1)] - '0';
							if (p[(len+2)])
							{
								val = (val * 10) + p[(len+2)] - '0';
							}
						}
					}

					if (!p[len])
					{
						if (len)
						{
							val = 2;
						}
						else
						{
							val = 1;
						}
					}
					else if (++val > 999)
					{
						val = 0;
					}

					if (( v1 = val/100 ))
					{
						p[len++] = '0' + v1;
					}
					v1 *= 100;
					if (( v2 = ((val-v1)/10) ))
					{
						p[len++] = '0' + v2;
					}
					v2 *= 10;
					p[len++] = '0' + (val-v1-v2);
					p[len] = '\0';

					save = j;
					j = -1; //restart the comparing
				}
			}
		}
	}
}

bool gslAddAllowedCritterPet(Entity *ent, PetDef *pPetDef, U64 uiItemID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, ent, "Pets:AddAllowedCritterPet", pPetDef->pchPetName);

	AutoTrans_trAddAllowedCritterPet(LoggedTransactions_CreateManagedReturnValEnt("gslAddAllowedCritterPuppet",ent,NULL,NULL),GetAppGlobalType(),
			ent->myEntityType, ent->myContainerID,
			pPetDef, uiItemID, &reason, pExtract);
	
	return true;
}

void DEFAULT_LATELINK_FixUpEntityName(Entity *pParentEnt, NOCONST(Entity) *pTempEntity, PetDef *pPetDef, const char* pchPetName)
{
	if (!pchPetName && pPetDef->bChooseRandomName && IS_HANDLE_ACTIVE(pTempEntity->pChar->hSpecies))
	{
		const char *pcTemp;
		char *pcSave = pTempEntity->pSaved->savedSubName;
		pTempEntity->pSaved->savedSubName = NULL;
		pcTemp = algoPetDef_GenerateRandomName(GET_REF(pTempEntity->pChar->hSpecies), &pTempEntity->pSaved->savedSubName, NULL);
		if (pcTemp)
		{
			*pTempEntity->pSaved->savedName = '\0';
			strcat(pTempEntity->pSaved->savedName, pcTemp);
			pTempEntity->pSaved->savedName[32] = '\0';
			if (pcSave) StructFreeString(pcSave);
		}
		else
		{
			pTempEntity->pSaved->savedSubName = pcSave;
		}
	}
}

int Entity_HasActivePuppetIfSet(Entity *pOwner, CharClassCategorySet *pSet)
{
	if (SAFE_MEMBER2(pOwner, pSaved, pPuppetMaster) && pSet)
	{
		int i;
		for (i=eaSize(&pOwner->pSaved->pPuppetMaster->ppPuppets)-1;i>=0;i--)
		{
			if (pOwner->pSaved->pPuppetMaster->ppPuppets[i]->eState == PUPPETSTATE_ACTIVE)
			{
				Entity *pEnt = GET_REF(pOwner->pSaved->pPuppetMaster->ppPuppets[i]->hEntityRef);
				if (CharClassCategorySet_getCategorySetFromEntity(pEnt, pSet->eClassType) == pSet)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool gslCreateSavedPetFromDefEx(int iPartitionIdx, Entity *ent, Entity* pEntSrc, AlgoPet *pAlgoPet, PetDef *pPetDef, const char* pchPetName, const char* pchPetSubName, PlayerCostume* pPetCostume, int iLevel, SavedPetReturnCallback func, void *pCallBackData, U64 uiItemId, OwnedContainerState eState, PropEntIDs *pPropEntIDs, bool bTeamRequest, bool bIsPuppet, GameAccountDataExtract *pExtract)
{
	S32 i;
	NOCONST(Entity) *pTempEntity;
	SavedPetCBData *cbData;
	CritterDef *pPetCritterDef;
	CritterFaction *pFaction;
	const char *pcCritterName, *pcCritterSubName = NULL;
	int iCostume;
	PlayerCostume *pCostume;
	CritterCostume *pCritterCostume;
	const char *pcSlotSetName;
	CharacterClass *pClass;

	if (!pPetDef || !ent)
	{
		return false;
	}

	pPetCritterDef = GET_REF(pPetDef->hCritterDef);

	if(!pPetCritterDef)
	{
		return false;
	}

	PERFINFO_AUTO_START_FUNC();
	
	if(!Entity_CanAddSavedPet(ent,pPetDef,uiItemId,bIsPuppet,pExtract,NULL))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(pPetDef->bCritterPet)
	{
		bool bResult = gslAddAllowedCritterPet(ent,pPetDef, uiItemId);
		PERFINFO_AUTO_STOP();
		return bResult;
	}

	pCostume = pAlgoPet ? pAlgoPet->pCostume : NULL;
	iCostume = pAlgoPet ? pAlgoPet->iCostume : -1;
	pCritterCostume = pPetCritterDef ? eaGet(&pPetCritterDef->ppCostume, iCostume) : NULL;
	if (!pCostume)
	{
		pCostume = pCritterCostume ? GET_REF(pCritterCostume->hCostumeRef) : pPetCostume;
	}

	if(IS_HANDLE_ACTIVE(pPetCritterDef->hFaction))
	{
		pFaction = GET_REF(pPetCritterDef->hFaction);
	}
	else
	{
		pFaction = GET_REF(ent->hFaction);
	}

	{
		CritterCreateParams createParams = {0}; 

		createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
		createParams.iPartitionIdx = PARTITION_UNINITIALIZED;
		createParams.iLevel = iLevel;
		createParams.pFaction = pFaction;
		createParams.pDisplayNameMsg = pCritterCostume ? GET_REF(pCritterCostume->displayNameMsg.hMessage) : NULL;
		createParams.pDisplaySubNameMsg = pCritterCostume ? GET_REF(pCritterCostume->displaySubNameMsg.hMessage) : NULL;
		createParams.erOwner = entGetRef(ent);
		createParams.pCostume = pCostume;
		createParams.bPlaceNumericsOnOwner = bIsPuppet;
		createParams.bFakeEntity = true;
		
		pTempEntity = CONTAINER_NOCONST(Entity, critter_CreateByDef(pPetCritterDef, &createParams, pPetCritterDef->pchFileName, true));
	}

	assert(pTempEntity);

	character_FillSavedAttributesFromClass(pTempEntity->pChar,pTempEntity->pChar->iLevelCombat);
	pClass = SAFE_GET_REF2(pTempEntity, pChar, hClass);
	//Clear the item id's here because critter_CreateByDef created them 
	//under the assumption that the entity was a critter. 
	inv_ClearItemIDsNoConst(pTempEntity); 

	SET_HANDLE_FROM_STRING("PetDef",pPetDef->pchPetName,pTempEntity->pCritter->petDef);
	pTempEntity->myEntityType = GLOBALTYPE_ENTITYSAVEDPET;
	pTempEntity->myContainerID = 0;
	if(!pTempEntity->pSaved)
		pTempEntity->pSaved = StructCreateNoConst(parse_SavedEntityData);

	pTempEntity->pSaved->conOwner.containerType = ent->myEntityType;
	pTempEntity->pSaved->conOwner.containerID = ent->myContainerID;

	pTempEntity->pSaved->uFixupVersion = CURRENT_ENTITY_FIXUP_VERSION;
	pTempEntity->pSaved->uGameSpecificFixupVersion = gameSpecificFixup_Version();

	if (IS_HANDLE_ACTIVE(pPetDef->hAllegiance))
	{
		COPY_HANDLE(pTempEntity->hAllegiance, pPetDef->hAllegiance);
	}

	if ( pchPetName )
	{
		strcpy( pTempEntity->pSaved->savedName, pchPetName );
		if (pchPetSubName)
		{
			pTempEntity->pSaved->savedSubName = StructAllocString(pchPetSubName);
		}
	}
	else 
	{
		//pcCritterName = langTranslateMessageRef(ent->pPlayer->langID, pPetDef->displayNameMsg.hMessage);
		//if (!pcCritterName) {
		if (pAlgoPet && pAlgoPet->pchPetName)
		{
			pcCritterName = pAlgoPet->pchPetName;
			pcCritterSubName = pAlgoPet->pchPetSubName;
		}
		else if(IS_HANDLE_ACTIVE(pTempEntity->pCritter->hDisplayNameMsg))
		{
			pcCritterName = langTranslateMessageRef(ent->pPlayer->langID, pTempEntity->pCritter->hDisplayNameMsg);
			if (IS_HANDLE_ACTIVE(pTempEntity->pCritter->hDisplaySubNameMsg))
			{
				pcCritterSubName = langTranslateMessageRef(ent->pPlayer->langID, pTempEntity->pCritter->hDisplaySubNameMsg);
			}
			else if (pCritterCostume && IS_HANDLE_ACTIVE(pCritterCostume->displaySubNameMsg.hMessage))
			{
				pcCritterSubName = langTranslateMessageRef(ent->pPlayer->langID, pCritterCostume->displaySubNameMsg.hMessage);
			}
		}
		else
		{
			pcCritterName = langTranslateMessageRef(ent->pPlayer->langID, pPetCritterDef->displayNameMsg.hMessage);
			if (pCritterCostume && IS_HANDLE_ACTIVE(pCritterCostume->displaySubNameMsg.hMessage))
			{
				pcCritterSubName = langTranslateMessageRef(ent->pPlayer->langID, pCritterCostume->displaySubNameMsg.hMessage);
			}
		}
		//}
		assertmsg(pcCritterName, "All saved pet critters must have a display name");
		*pTempEntity->pSaved->savedName = '\0';
		strcat(pTempEntity->pSaved->savedName, pcCritterName);
		pTempEntity->pSaved->savedName[32] = '\0';
		if (pcCritterSubName)
		{
			pTempEntity->pSaved->savedSubName = StructAllocString(pcCritterSubName);
		}

		{
			//Some files put 10 at the end of the name; Strip this
			i = (int)strlen(pTempEntity->pSaved->savedName);
			if (i && i <= 32 && pTempEntity->pSaved->savedName[(i-1)] == 10)
			{
				pTempEntity->pSaved->savedName[(i-1)] = '\0';
			}
		}
	}

	//place specified power nodes in escrow
	if(pAlgoPet)
	{
		for (i=0; i<eaSize(&pAlgoPet->ppEscrowNodes); i++)
		{
			PTNodeDef *pNodeDef = GET_REF(pAlgoPet->ppEscrowNodes[i]->hNodeDef);
			PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);

			if(pNodeDef==NULL || pTreeDef==NULL)
				continue;

			entity_PowerTreeNodeEscrowHelper(iPartitionIdx, pTempEntity, NULL, pTreeDef->pchName, pNodeDef->pchNameFull, NULL);
		}
	}
	else
	{
		for ( i = 0; i < eaSize( &pPetDef->ppEscrowPowers ); i++ )
		{
			PTNodeDef* pNodeDef = GET_REF(pPetDef->ppEscrowPowers[i]->hNodeDef);
			PowerTreeDef* pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);

			if ( pNodeDef==NULL || pTreeDef==NULL )
				continue;

			entity_PowerTreeNodeEscrowHelper(iPartitionIdx, pTempEntity, NULL, pTreeDef->pchName, pNodeDef->pchNameFull, NULL );
		}
	}

	// Fix up costume as needed
	if (!pCostume && GET_REF(pTempEntity->costumeRef.hReferencedCostume)) {
		pPetCostume = (PlayerCostume*)GET_REF(pTempEntity->costumeRef.hReferencedCostume);
		if (pPetCostume && pTempEntity->pChar && GET_REF(pPetCostume->hSpecies)) COPY_HANDLE(pTempEntity->pChar->hSpecies, pPetCostume->hSpecies);
	}
	else if (pCostume)
	{
		pPetCostume = pCostume;
		if (GET_REF(pCostume->hSpecies)) COPY_HANDLE(pTempEntity->pChar->hSpecies, pCostume->hSpecies);
	}
	if (pAlgoPet && pTempEntity->pChar && GET_REF(pAlgoPet->hSpecies)) COPY_HANDLE(pTempEntity->pChar->hSpecies, pAlgoPet->hSpecies);
	if (pTempEntity->pChar && (!GET_REF(pTempEntity->pChar->hSpecies)) && GET_REF(pPetCritterDef->hSpecies) ) COPY_HANDLE(pTempEntity->pChar->hSpecies, pPetCritterDef->hSpecies);

	REMOVE_HANDLE(pTempEntity->costumeRef.hReferencedCostume);
	
	FixUpEntityName(ent, pTempEntity, pPetDef, pchPetName);

	//No pet can have the same name
	Entity_MakePetNameUnique(ent, pTempEntity, true);

	// Make sure costume slots are pre-created properly
	pcSlotSetName = costumeEntity_GetSlotSetName((Entity*)pTempEntity, !bIsPuppet);
	costumeEntity_trh_FixupCostumeSlots(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, ent), pTempEntity, pcSlotSetName);

	if (pPetCostume) {
		NOCONST(PlayerCostume) *pCostumeCopy = StructCloneDeConst(parse_PlayerCostume, pPetCostume);
		if (pCostumeCopy) {
			// If no costume slots defined, then make a slot
			if (!eaSize(&pTempEntity->pSaved->costumeData.eaCostumeSlots)) {
				NOCONST(PlayerCostumeSlot) *pSlot;
				PCSlotType *pSlotType;
				int iSlotID = 0;

				pSlotType = costumeEntity_GetSlotType((Entity*)pTempEntity, 0, !bIsPuppet, &iSlotID);

				pTempEntity->pSaved->costumeData.iActiveCostume = 0;
				pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
				pSlot->pCostume = pCostumeCopy;
				pSlot->iSlotID = iSlotID;
				pSlot->pcSlotType = (pSlotType ? pSlotType->pcName : NULL);
				eaPush(&pTempEntity->pSaved->costumeData.eaCostumeSlots, pSlot);
			}

			// Put the costume in
			pCostumeCopy->pcName = allocAddString(pTempEntity->pSaved->savedName);
			costumeEntity_ApplyEntityInfoToCostumeNoConst(pTempEntity, pCostumeCopy);
			pTempEntity->pSaved->costumeData.eaCostumeSlots[0]->pCostume = pCostumeCopy;
		}
	}

	cbData = calloc(sizeof(SavedPetCBData), 1);
	cbData->CallbackFunc = func;
	cbData->pUserData = pCallBackData;
	cbData->ownerType = entGetType(ent);
	cbData->ownerID = entGetContainerID(ent);
	cbData->iItemOwnerType = pEntSrc ? entGetType(pEntSrc) : 0;
	cbData->iItemOwnerID = pEntSrc ? entGetContainerID(pEntSrc) : 0;
	cbData->iPetContainerType = entGetType((Entity*)pTempEntity);
	cbData->iPetContainerID = 0;
	cbData->uiItemId = uiItemId;
	cbData->newState = eState;
	if (pPropEntIDs)
		ea32Copy(&cbData->newPropEntIDs.eauiPropEntIDs, &pPropEntIDs->eauiPropEntIDs);
	cbData->bNewTeamRequest = bTeamRequest;
	cbData->pPetDef = pPetDef;
	cbData->bMakeActive = pClass && !Entity_HasActivePuppetIfSet(ent, CharClassCategorySet_getCategorySetFromClass(pClass));

	sprintf(cbData->chSavedName,"%s",pTempEntity->pSaved->savedName);
		
	objRequestContainerCreate(objCreateManagedReturnVal(CreatePet_CB, cbData),
		GLOBALTYPE_ENTITYSAVEDPET, pTempEntity, GLOBALTYPE_OBJECTDB, 0);

	StructDestroyNoConst(parse_Entity, pTempEntity);

	PERFINFO_AUTO_STOP();
	return true;
}

bool gslCreateDoppelgangerSavedPetFromDef(int iPartitionIdx, Entity *ent, Entity *pEntSrc, AlgoPet *pAlgoPet, PetDef *pPetDef, int iLevel, SavedPetReturnCallback func, void *pCallBackData, U64 uiItemId, OwnedContainerState eState, PropEntIDs *pPropEntIDs, bool bTeamRequest, Entity* pDoppelgangerSrc, GameAccountDataExtract *pExtract)
{
	if(!func)
		func = PetCreate_CB;

	return gslCreateSavedPetFromDefEx( iPartitionIdx, ent, pEntSrc, pAlgoPet, pPetDef, entGetLangName(pDoppelgangerSrc, entGetLanguage(pDoppelgangerSrc)), NULL, costumeEntity_GetEffectiveCostume(pDoppelgangerSrc), iLevel, func, pCallBackData, uiItemId, eState, pPropEntIDs, bTeamRequest, false, pExtract);
}

bool gslCreateSavedPetFromDef(int iPartitionIdx, Entity *ent, Entity *pEntSrc, AlgoPet *pAlgoPet, PetDef *pPetDef, int iLevel, SavedPetReturnCallback func, void *pCallBackData, U64 uiItemId, OwnedContainerState eState, PropEntIDs *pPropEntIDs, bool bTeamRequest, GameAccountDataExtract *pExtract)
{
	if(!func)
		func = PetCreate_CB;

	return gslCreateSavedPetFromDefEx( iPartitionIdx, ent, pEntSrc, pAlgoPet, pPetDef, NULL, NULL, NULL, iLevel, func, pCallBackData, uiItemId, eState, pPropEntIDs, bTeamRequest, false, pExtract);
}

bool gslCreateSavedPetFromAlgoPet(int iPartitionIdx, Entity *ent, Entity *pEntSrc, AlgoPet *pAlgoPet, PetDef *pPetDef, int iLevel, SavedPetReturnCallback func, void *pCallBackData, U64 uiItemId, OwnedContainerState eState, PropEntIDs *pPropEntIDs, bool bTeamRequest, GameAccountDataExtract *pExtract)
{
	if(!func)
		func = PetCreate_CB;

	return gslCreateSavedPetFromDefEx( iPartitionIdx, ent, pEntSrc, pAlgoPet, pPetDef, NULL, NULL, NULL, iLevel, func, pCallBackData, uiItemId, eState, pPropEntIDs, bTeamRequest, false, pExtract);
}

static void DestroyPet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{	
	switch(returnVal->eOutcome)
	{
		xcase TRANSACTION_OUTCOME_FAILURE:
		{
			int i;
			for (i = 0; i < returnVal->iNumBaseTransactions; i++)
			{			
				if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					if (gbEnablePetAndPuppetLogging)
					{
						objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "DestroyPet", 
							"Failed because: %s",returnVal->pBaseReturnVals[i].returnString);
					}
					break;
				}
			}
		}
		xcase TRANSACTION_OUTCOME_SUCCESS:
		{
			if (gbEnablePetAndPuppetLogging)
			{
				objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "DestroyPet",
					"Child EntitySavedPet[%d] Destroyed", cbData->iPetContainerID);
			}
		}	
	}
	SAFE_FREE(cbData);
}

static void RemovePet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
		xcase TRANSACTION_OUTCOME_FAILURE:
		{
			SAFE_FREE(cbData);
		}
		xcase TRANSACTION_OUTCOME_SUCCESS:
		{
			Entity* pOwner = entFromContainerIDAnyPartition(cbData->ownerType,cbData->ownerID);
			Entity* pEntityToDestroy = entFromContainerIDAnyPartition(cbData->iPetContainerType, cbData->iPetContainerID);
			// Add entry about dismissal to activity log
			gslActivity_AddPetDismissEntry(pOwner, pEntityToDestroy);

			// Destroy the container
			if(!cbData->bIsNemesis && pEntityToDestroy && pEntityToDestroy->myRef)
			{
				objRequestContainerDestroy(objCreateManagedReturnVal(DestroyPet_CB, cbData),
					cbData->iPetContainerType, cbData->iPetContainerID, objServerType(), objServerID());
			}
			else
			{
				objRequestContainerDestroy(objCreateManagedReturnVal(DestroyPet_CB, cbData),
					cbData->iPetContainerType, cbData->iPetContainerID, GLOBALTYPE_OBJECTDB, 0);
			}
			if (cbData->oldState == OWNEDSTATE_ACTIVE)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
				character_ResetPowersArray(cbData->iPartitionIdx, pOwner->pChar, pExtract);
			}
			savedpet_destroyOfflineCopy(cbData->iPartitionIdx, cbData->iPetContainerID);
		}
	}
}

static void CreateNemesis_CB(TransactionReturnVal *pReturnVal, SavedPetCBData *pData)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pData->ownerType, pData->ownerID);
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS){
		if (pEnt && pEnt->pPlayer){
			const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Create_Succeeded");
			notify_NotifySend(pEnt, kNotifyType_NemesisAdded, pchError, NULL, NULL);

			nemesis_RefreshNemesisArc(pEnt);
		}
	} else {
		if (pEnt){
			const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_TransactionFailure");
			notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		}
	}

	free(pData);
}


bool gslCreateSavedPetNemesis(Entity *pOwnerEnt, const char* pchNemesisName, const char* pchNemesisDescription, NemesisMotivation motivation, NemesisPersonality personality, NemesisPowerSet *pPowerSet, NemesisMinionPowerSet *pMinionPowerSet, NemesisMinionCostumeSet *pMinionCostumeSet, PlayerCostume* pCostume, PCMood *pMood, F32 fPowerHue, F32 fMinionPowerHue, NemesisState eState)
{
	NOCONST(Entity) *pTempEntity;
	NOCONST(PlayerCostumeSlot) *pSlot;
	SavedPetCBData *cbData = NULL;
	CritterDef *pCritterDef = (pPowerSet?RefSystem_ReferentFromString(g_hCritterDefDict, pPowerSet->pcCritter):NULL);
	PCSlotType *pSlotType;
	int iSlotID = 0;

	if (!pOwnerEnt || !pCritterDef || !pchNemesisName || !pPowerSet || !pMinionPowerSet || !pMinionCostumeSet || !pCostume || !pMood){
		return false;
	}

	if (!(eState > NemesisState_None && eState < NemesisState_Max)){
		return false;
	}

	pTempEntity = StructCreateWithComment(parse_Entity, "Temp entity for creating Nemesis pet");
	assert(pTempEntity);

	pTempEntity->myEntityType = GLOBALTYPE_ENTITYSAVEDPET;
	pTempEntity->myContainerID = 0;
	if(!pTempEntity->pSaved)
		pTempEntity->pSaved = StructCreateNoConst(parse_SavedEntityData);

	pTempEntity->pSaved->conOwner.containerType = pOwnerEnt->myEntityType;
	pTempEntity->pSaved->conOwner.containerID = pOwnerEnt->myContainerID;

	pTempEntity->pSaved->uFixupVersion = CURRENT_ENTITY_FIXUP_VERSION;
	pTempEntity->pSaved->uGameSpecificFixupVersion = gameSpecificFixup_Version();

	// Set up costume
	pSlotType = costumeEntity_GetSlotType((Entity*)pTempEntity, 0, true, &iSlotID);
	SET_HANDLE_FROM_REFERENT("CostumeMood", pMood, pTempEntity->costumeRef.hMood);
	pTempEntity->pSaved->costumeData.iActiveCostume = 0;
	pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
	pSlot->iSlotID = iSlotID;
	pSlot->pcSlotType = (pSlotType ? pSlotType->pcName : NULL);
	pSlot->pCostume = StructCloneDeConst(parse_PlayerCostume, pCostume);
	eaPush(&pTempEntity->pSaved->costumeData.eaCostumeSlots, pSlot);
	
	// Update Gender
	pTempEntity->eGender = costumeTailor_GetGender(pCostume);


	// Set up nemesis-specific data
	if (!pTempEntity->pNemesis)
		pTempEntity->pNemesis = StructCreateNoConst(parse_Nemesis);
	pTempEntity->pNemesis->motivation = motivation;
	pTempEntity->pNemesis->personality = personality;
	pTempEntity->pNemesis->pchPowerSet = pPowerSet->pcName;
	pTempEntity->pNemesis->pchMinionCostumeSet = pMinionCostumeSet->pcName;
	pTempEntity->pNemesis->pchMinionPowerSet = pMinionPowerSet->pcName;
	pTempEntity->pNemesis->fPowerHue = CLAMP(fPowerHue, 0, 360);
	pTempEntity->pNemesis->fMinionPowerHue = CLAMP(fMinionPowerHue, 0, 360);

	if ( pchNemesisName )
	{
		strcpy( pTempEntity->pSaved->savedName, pchNemesisName );
	}
	else
	{
		const char *pcCritterName = langTranslateMessageRef(pOwnerEnt->pPlayer->langID, pCritterDef->displayNameMsg.hMessage);
		assertmsg(pcCritterName, "All saved pet critters must have a display name");
		sprintf(pTempEntity->pSaved->savedName, "%s", pcCritterName);
		pTempEntity->pSaved->savedName[32] = '\0';
	}

	if (pchNemesisDescription && *pchNemesisDescription)
	{
		pTempEntity->pSaved->savedDescription = StructAllocString(pchNemesisDescription);
	}

	cbData = calloc(sizeof(SavedPetCBData), 1);
	cbData->CallbackFunc = CreateNemesis_CB;
	cbData->pUserData = NULL;
	cbData->ownerType = entGetType(pOwnerEnt);
	cbData->ownerID = entGetContainerID(pOwnerEnt);
	cbData->iPetContainerType = entGetType((Entity*)pTempEntity);
	cbData->iPetContainerID = 0;
	cbData->uiItemId = 0;
	cbData->newState = OWNEDSTATE_OFFLINE;
	ea32Clear(&cbData->newPropEntIDs.eauiPropEntIDs);
	cbData->bNewTeamRequest = false;
	cbData->bIsNemesis = true;
	cbData->eNemesisState = eState;

	objRequestContainerCreate(objCreateManagedReturnVal(CreatePet_CB, cbData),
		GLOBALTYPE_ENTITYSAVEDPET, pTempEntity, 0, 0);

	StructDestroyNoConst(parse_Entity, pTempEntity);

	return true;
}

bool gslDestroySavedPet(Entity *pEntityToDestroy)
{
	SavedPetCBData *cbData;
	Entity *pOwner;
	Entity *pNewActive = NULL;
	PuppetEntity *pPuppetToDestroy = NULL;
	PetRelationship *pRel = NULL;

	if (!pEntityToDestroy 
		|| (entGetType(pEntityToDestroy) != GLOBALTYPE_ENTITYSAVEDPET) 
		|| !pEntityToDestroy->pSaved 
		|| !pEntityToDestroy->pSaved->conOwner.containerID)
	{
		return false;
	}

	pOwner = entFromContainerIDAnyPartition(pEntityToDestroy->pSaved->conOwner.containerType, pEntityToDestroy->pSaved->conOwner.containerID);
	if (!pOwner)
	{
		return false;
	}

	pPuppetToDestroy = SavedPet_GetPuppetFromContainerID(pOwner, pEntityToDestroy->myContainerID);

	if (pPuppetToDestroy && pPuppetToDestroy->eState == PUPPETSTATE_ACTIVE)
	{
		CharClassCategorySet *pSet = CharClassCategorySet_getCategorySetFromEntity(pEntityToDestroy, pPuppetToDestroy->eType);
		S32 iPartitionIdx = entGetPartitionIdx(pOwner);
		int i;
		if (pSet)
		{
			for (i=0; i < eaSize(&pOwner->pSaved->pPuppetMaster->ppPuppets); i++)
			{
				PuppetEntity *pPuppetEntity = pOwner->pSaved->pPuppetMaster->ppPuppets[i];
				Entity *pEnt = SavedPuppet_GetEntity(iPartitionIdx, pPuppetEntity);
				CharClassCategorySet *pOtherSet = CharClassCategorySet_getCategorySetFromEntity(pEnt, pPuppetToDestroy->eType);
				if (pPuppetEntity != pPuppetToDestroy
					&& pSet == pOtherSet
					&& pPuppetEntity->eState != PUPPETSTATE_ACTIVE)
				{
					pNewActive = pEnt;
					break;
				}
			}
		}
	}

	pRel = SavedPet_GetPetFromContainerID(pOwner, pEntityToDestroy->myContainerID, false);

	// Create callback data
	cbData = calloc(sizeof(SavedPetCBData), 1);
	cbData->ownerType = pEntityToDestroy->pSaved->conOwner.containerType;
	cbData->ownerID = pEntityToDestroy->pSaved->conOwner.containerID;
	cbData->iPetContainerType = entGetType(pEntityToDestroy);
	cbData->iPetContainerID = entGetContainerID(pEntityToDestroy);
	cbData->iPartitionIdx = entGetPartitionIdx(pOwner);
	cbData->oldState = SAFE_MEMBER(pRel, eState);

	AutoTrans_trRemoveSavedPet(LoggedTransactions_CreateManagedReturnVal("RemovePet", RemovePet_CB, cbData), 
		GLOBALTYPE_GAMESERVER, 
		cbData->ownerType, cbData->ownerID, 
		cbData->iPetContainerType, cbData->iPetContainerID, 
		pNewActive ? entGetContainerID(pNewActive) : 0);

	return true;
}

bool gslDestroySavedPetNemesis(Entity *pEntityToDestroy)
{
	GameAccountDataExtract *pExtract;
	Entity *pOwner;
	SavedPetCBData *cbData;
	ItemChangeReason reason = {0};

	if (!pEntityToDestroy 
		|| entGetType(pEntityToDestroy) != GLOBALTYPE_ENTITYSAVEDPET 
		|| !pEntityToDestroy->pSaved 
		|| !pEntityToDestroy->pSaved->conOwner.containerID 
		|| !pEntityToDestroy->pNemesis)
	{
		return false;
	}
	pOwner = entFromContainerIDAnyPartition(pEntityToDestroy->pSaved->conOwner.containerType, pEntityToDestroy->pSaved->conOwner.containerID);
	if (!pOwner)
	{
		return false;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pOwner);

	inv_FillItemChangeReason(&reason, pOwner, "Pets:DestroyNemesis", NULL);

	cbData = calloc(sizeof(SavedPetCBData), 1);
	cbData->ownerType = entGetType(pOwner);
	cbData->ownerID = entGetContainerID(pOwner);
	cbData->iPetContainerType = entGetType(pEntityToDestroy);
	cbData->iPetContainerID = entGetContainerID(pEntityToDestroy);
	cbData->iPartitionIdx = entGetPartitionIdx(pOwner);
	cbData->bIsNemesis = true;

	AutoTrans_nemesis_tr_RemoveNemesis(
		LoggedTransactions_CreateManagedReturnValEnt("RemoveNemesis", pOwner, RemovePet_CB, cbData), 
		GLOBALTYPE_GAMESERVER, 
		cbData->ownerType, cbData->ownerID, 
		cbData->iPetContainerType, cbData->iPetContainerID,
		&reason, pExtract);

	return true;
}

static void PetUnControl_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);

	if(!owningEntity)
	{
		// Entity is offline, not sure what to do here yet
		return;
	}

	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			int i;
			for (i = 0; i < returnVal->iNumBaseTransactions; i++)
			{			
				if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					if (gbEnablePetAndPuppetLogging)
					{
						entLog(LOG_CONTAINER, owningEntity, "UnControlPet", 
							"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
					}
					free(cbData);
					return;
				}
			}
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			GlobalType newType = GLOBALTYPE_ENTITYSAVEDPET;
			ContainerID newID = atoi(returnVal->pBaseReturnVals[0].returnString);

			//Give control of that entity
			Entity *pPet = entFromContainerIDAnyPartition(cbData->iPetContainerType, cbData->iPetContainerID);
			if(pPet)
			{
				gslEntCancelRide(owningEntity);
			}

			if (gbEnablePetAndPuppetLogging)
			{
				entLog(LOG_CONTAINER, owningEntity, "UnControlPet", 
					"Child EntitySavedPet[%d]", newID);
			}

			free(cbData);

			return;
		}
	}
}
static void PetControl_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);

	if (!owningEntity)
	{
		// Entity is offline, not sure what to do here yet
		return;
	}

	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			int i;
			for (i = 0; i < returnVal->iNumBaseTransactions; i++)
			{			
				if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					if (gbEnablePetAndPuppetLogging)
					{
						entLog(LOG_CONTAINER, owningEntity, "ControlPet", 
							"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
					}
					free(cbData);
					return;
				}
			}
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			GlobalType newType = GLOBALTYPE_ENTITYSAVEDPET;
			ContainerID newID = atoi(returnVal->pBaseReturnVals[0].returnString);

			//Give control of that entity
			Entity *pPet = entFromContainerIDAnyPartition(cbData->iPetContainerType, cbData->iPetContainerID);
			if(pPet)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(owningEntity);
				gslEntRideCritter(owningEntity, pPet, pExtract);
			}

			if (gbEnablePetAndPuppetLogging)
			{
				entLog(LOG_CONTAINER, owningEntity, "SummonPet", 
					"Child EntitySavedPet[%d]", newID);
			}

			free(cbData);

			return;
		}	
	}
}

static void PetSummon_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);

	if (!owningEntity)
	{
		// Entity is offline, not sure what to do here yet
		free(cbData);
		return;
	}

	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			int i;
			for (i = 0; i < returnVal->iNumBaseTransactions; i++)
			{			
				if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					if (gbEnablePetAndPuppetLogging)
					{
						entLog(LOG_CONTAINER, owningEntity, "SummonPet", 
							"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
					}
					free(cbData);
					return;
				}
			}
			free(cbData);
			return;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			if (gbEnablePetAndPuppetLogging)
			{
				GlobalType newType = GLOBALTYPE_ENTITYSAVEDPET;
				ContainerID newID = atoi(returnVal->pBaseReturnVals[0].returnString);

				entLog(LOG_CONTAINER, owningEntity, "SummonPet", 
					"Child EntitySavedPet[%d]", newID);
			}

			free(cbData);
			return;
		}	
	}	
}

static void PetControlSet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		if (gbEnablePetAndPuppetLogging)
		{
			objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "SummonPet",
				"Child EntitySavedPet[%d] can't be found", cbData->iPetContainerID);
		}
		free(cbData);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		{		
			Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);

			if (!owningEntity)
			{
				// Entity is offline, not sure what to do here yet
				free(cbData);
				return;
			}

			objRequestContainerMove(objCreateManagedReturnVal(PetControl_CB, cbData),
				cbData->iPetContainerType, cbData->iPetContainerID, cbData->locationType, cbData->locationID, objServerType(), objServerID());
		}
	}
}

static void PetLocationSet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		if (gbEnablePetAndPuppetLogging)
		{
			objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "SummonPet",
				"Child EntitySavedPet[%d] can't be found", cbData->iPetContainerID);
		}
		free(cbData);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		{		
			Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);

			if (!owningEntity)
			{
				// Entity is offline, not sure what to do here yet
				free(cbData);
				return;
			}

			objRequestContainerMove(objCreateManagedReturnVal(PetSummon_CB, cbData),
				cbData->iPetContainerType, cbData->iPetContainerID, cbData->locationType, cbData->locationID, objServerType(), objServerID());
		}
	}

}

static void PetControlRequest_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	ContainerRef *pLocation = NULL;

	switch(RemoteCommandCheck_ContainerGetOwner(returnVal, &pLocation))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		if (gbEnablePetAndPuppetLogging)
		{
			objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "ControlPet",
				"Child EntitySavePet[%d] can't be found", cbData->iPetContainerID);
		}
		free(cbData);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);
			MapDescription newDesc = {0};

			if(!owningEntity)
			{
				StructDestroy(parse_ContainerRef, pLocation);
				free(cbData);
				return;
			}

			if(pLocation->containerType == objServerType() && pLocation->containerID == objServerID())
			{
				//Entity is in the same server, just give control
				Entity *pPet = entFromContainerIDAnyPartition(cbData->iPetContainerType, cbData->iPetContainerID);
				if(pPet)
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(owningEntity);
					gslEntRideCritter(owningEntity, pPet, pExtract);
				}
				free(cbData);
			}
			else
			{
				Quat spawnRot;

				//Entity is in different server, what should we do?
				StructCopy(parse_MapDescription, &gGSLState.gameServerDescription.baseMapDescription, &newDesc, 0, 0, 0);
				entGetPos(owningEntity, newDesc.spawnPos);
				entGetRot(owningEntity, spawnRot);
				quatToPYR(spawnRot, newDesc.spawnPYR);

				newDesc.spawnPos[0] += 0.01f * randomF32();
				newDesc.spawnPos[2] += 0.01f * randomF32();

				
				//Entity is still in the object DB and needs to be summoned
				if(pLocation->containerType == GLOBALTYPE_OBJECTDB)
				{
					//Entity is on a different server, move it here
					//TODO(MM): this is the incorrect solution
					//cbData->locationType = objServerType();
					//cbData->locationID = objServerID();
					//objRequestContainerMove(objCreateManagedReturnVal(PetControlRequest_CB, cbData),
					//	cbData->petType, cbData->petID, cbData->locationType, cbData->locationID, objServerType(), objServerID());


					cbData->locationType = pLocation->containerType;
					cbData->locationID = pLocation->containerID;
					AutoTrans_trUpdateMapHistory(LoggedTransactions_CreateManagedReturnVal("SummonPet", PetControlSet_CB, cbData),
						GetAppGlobalType(),
						cbData->iPetContainerType,
						cbData->iPetContainerID,
						&newDesc);
				}
				else
				{
					free(cbData);
				}
			}
			StructDestroy(parse_ContainerRef, pLocation);
		}
	}
}


static void PetLocationRequest_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	ContainerRef *pLocation = NULL;

	switch(RemoteCommandCheck_ContainerGetOwner(returnVal, &pLocation))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		if (gbEnablePetAndPuppetLogging)
		{
			objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "SummonPet",
				"Child EntitySavedPet[%d] can't be found", cbData->iPetContainerID);
		}
		free(cbData);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		{		
			Entity *owningEntity = entFromContainerIDAnyPartition(cbData->ownerType, cbData->ownerID);
			MapDescription newDesc = {0};
			Quat spawnRot;

			// Fail out rather than spawning the pet on the web request server.  This is a temporary fix to prevent crashes
			//  in the web request server when it tries to spawn pets.  The spawning of pets is a side effect of the web request
			//  server taking ownership of the player container.  The real fix is to have web request server operate on a
			//  subscribed copy of the player entity.
			if (!owningEntity || !IsGameServerSpecificallly_NotRelatedTypes())
			{
				// Entity is offline, not sure what to do here yet
				StructDestroy(parse_ContainerRef, pLocation);
				free(cbData);
				return;
			}
	
			StructCopy(parse_MapDescription, &gGSLState.gameServerDescription.baseMapDescription, &newDesc, 0, 0, 0);
			entGetPos(owningEntity, newDesc.spawnPos);
			entGetRot(owningEntity, spawnRot);
			quatToPYR(spawnRot, newDesc.spawnPYR);

			//newDesc.spawnPos[0] += 0.01f * randomF32();
			//newDesc.spawnPos[2] += 0.01f * randomF32();
			Entity_SavedPetGetOriginalSpawnPos(owningEntity,NULL,spawnRot,newDesc.spawnPos);
			Entity_GetPositionOffset(entGetPartitionIdx(owningEntity), NULL, spawnRot, cbData->iSeedID, newDesc.spawnPos, owningEntity->iBoxNumber);
			if (pLocation->containerType == objServerType() && pLocation->containerID == objServerID())
			{
				Entity *pPet = entFromContainerIDAnyPartition(cbData->iPetContainerType, cbData->iPetContainerID);
				
				if (pPet)
				{									
					entSetPos(pPet, newDesc.spawnPos, true, __FUNCTION__);
					entSetRot(pPet, spawnRot, true, __FUNCTION__);
				}
				if (gbEnablePetAndPuppetLogging)
				{
					entLog(LOG_CONTAINER, owningEntity, "SummonPet", 
						"Child EntitySavedPet[%d]", cbData->iPetContainerID);
				}
				free(cbData);
			}
			else
			{
				cbData->locationType = pLocation->containerType;
				cbData->locationID = pLocation->containerID;
				AutoTrans_trUpdateMapHistory(LoggedTransactions_CreateManagedReturnVal("SummonPet", PetLocationSet_CB, cbData),
					GetAppGlobalType(),
					cbData->iPetContainerType,
					cbData->iPetContainerID,
					&newDesc);
			}
			
			StructDestroy(parse_ContainerRef, pLocation);
		}
	}

}

static bool PetStateActive_summoncheck(Entity *pMasterEntity, ContainerID petID, PetRelationship *pRelationship, RegionRules *pRegionRules)
{
	if(!pMasterEntity || !pMasterEntity->pSaved)
		return false;

	if(!pRelationship || pRelationship->conID != petID)
	{
		int i;
		//Need to find the pet relationship from the master
		for(i=eaSize(&pMasterEntity->pSaved->ppOwnedContainers)-1;i>=0;i--)
		{
			pRelationship = pMasterEntity->pSaved->ppOwnedContainers[i];

			if(pRelationship->conID == petID)
				break;
		}

		if(i == -1)
			return false;
	}

	if(!(pRelationship->bTeamRequest))
		return false;

	if(!pRegionRules)
	{
		Vec3 vPosition;
		WorldRegion *pRegion;

		entGetPos(pMasterEntity,vPosition);

		pRegion = worldGetWorldRegionByPos(vPosition);
		pRegionRules = getRegionRulesFromRegion(pRegion);
	}

	if(pRegionRules)
	{
		Entity *pPetEntity = SavedPet_GetEntity(entGetPartitionIdx(pMasterEntity), pRelationship);
		CharacterClass *pClass = NULL;

		if (pPetEntity && pPetEntity->pChar)
			pClass = GET_REF(pPetEntity->pChar->hClass);

		if(pRegionRules->iAllowedPetsPerPlayer==0)
			return false;

		if(!pClass || ea32Find(&pRegionRules->ePetType,pClass->eType)==-1)
			return false;

	}

	return true;
}

static int CmpTeamByKeys(const SavedPetTeamList *puiID, const SavedPetTeamList** ppList)
{
	if(puiID->iPartitionIdx == (*ppList)->iPartitionIdx)
	{
		if(puiID->eGlobalType == (*ppList)->eGlobalType)
			return puiID->iUniqueID < (*ppList)->iUniqueID ? -1 : (puiID->iUniqueID==(*ppList)->iUniqueID ? 0 : 1);
		else
			return puiID->eGlobalType < (*ppList)->eGlobalType ? -1 : 1;
	}
	else
		return puiID->iPartitionIdx < (*ppList)->iPartitionIdx ? -1 : 1;
}

int teamlist_sortfunc(const SavedPetTeamList **pListA, const SavedPetTeamList **pListB)
{
	if((*pListA)->eGlobalType == (*pListB)->eGlobalType)
		return (*pListA)->iUniqueID < (*pListB)->iUniqueID ? -1 : ((*pListA)->iUniqueID==(*pListB)->iUniqueID ? 0 : 1);
	else
		return (*pListA)->eGlobalType < (*pListB)->eGlobalType ? -1 : 1;
} 

static SavedPetTeamList *gslTeam_FindPetTeamList(int iPartitionIdx, GlobalType eTeamType, U32 uiTeamID)
{
	int s = eaSize(&g_SavedPetTeamManager.ppTeamList);
	SavedPetTeamList sTeamList;

	sTeamList.eGlobalType = eTeamType;
	sTeamList.iUniqueID = uiTeamID;
	sTeamList.iPartitionIdx = iPartitionIdx;

	if(s)
	{
		SavedPetTeamList **pList = eaBSearch(g_SavedPetTeamManager.ppTeamList,CmpTeamByKeys,sTeamList);

		if(pList && *pList)
			return *pList;
	}

	return NULL;
}

static SavedPetTeamList *gslTeam_CreateNewTeamList(int iPartitionIdx, GlobalType eTeamType, U32 uiTeamID)
{
	SavedPetTeamList *sTeamList = calloc(sizeof(SavedPetTeamList),1);

	sTeamList->eGlobalType = eTeamType;
	sTeamList->iUniqueID = uiTeamID;
	sTeamList->iPartitionIdx = iPartitionIdx;

	eaPush(&g_SavedPetTeamManager.ppTeamList,sTeamList);

	eaQSort(g_SavedPetTeamManager.ppTeamList,teamlist_sortfunc);

	return sTeamList;
}

static void gslTeam_AddPetToTeamList(int iPartitionIdx, GlobalType eTeamType, U32 uiTeamID, U32 uiPetID)
{
	SavedPetTeamList *sTeamList = NULL;
	sTeamList = gslTeam_FindPetTeamList(iPartitionIdx, eTeamType,uiTeamID);

	if(!sTeamList)
	{
		sTeamList = gslTeam_CreateNewTeamList(iPartitionIdx, eTeamType,uiTeamID);
	}

	ea32PushUnique(&sTeamList->uiPetIDs,uiPetID);
}

static void gslTeam_RemovePetTeamList(SavedPetTeamList *pTeamList)
{
	eaFindAndRemove(&g_SavedPetTeamManager.ppTeamList,pTeamList);

	ea32Destroy(&pTeamList->uiPetIDs);
	free(pTeamList);
}

static int gslTeam_GetOpenSlots(int iPartitionIdx, SavedPetTeamList *pTeamList, Team *pTeam)
{
	int iOpenSpots=TEAM_MAX_SIZE;

	if(pTeamList && pTeamList->eGlobalType == GLOBALTYPE_TEAM)
	{
		int iTeamMatesActive=0;

		// WOLF[15Nov12]:  This is used when filling in a STO team with Bridge Officers in the case that a team
		//   has ended up on different partitions/maps/etc.  There is code elsewhere (not found yet) that will
		//   kick out any bridge officer pets if one of the real team members actually makes it into the partition
		//   We will have to rely on that code to deal with disconnecteds as well. To that end, this code
		//   explicitly checks the eamembers directly

		//Get active players for that team
		if(pTeam)
		{
			iTeamMatesActive = team_NumMembersThisServerAndPartition(pTeam,iPartitionIdx);
		}
		else
		{
			iTeamMatesActive = 1; //Assume this if the first (and only) entity from this team so far
		}

		iOpenSpots = TEAM_MAX_SIZE - (pTeamList ? ea32Size(&pTeamList->uiPetIDs) : 0) - iTeamMatesActive;
	}
	else
	{
		//-1 for counting yourself
		iOpenSpots = TEAM_MAX_SIZE - (pTeamList ? ea32Size(&pTeamList->uiPetIDs) : 0) - 1;
	}

	return MAX(iOpenSpots, 0);
}

static int gslTeam_GetOpenSlotsForPlayer(Entity* pEnt, SavedPetTeamList *pTeamList)
{
	int i, j, iOpenSpots=0;
	RegionRules* pRegionRules;
	WorldRegion* pRegion;
	Vec3 vEntPos;
	int iPartitionIdx;
	
	if ( pEnt->pPlayer==NULL )
		return 0;

	entGetPos(pEnt,vEntPos);
	pRegion = worldGetWorldRegionByPos(vEntPos);
	pRegionRules = getRegionRulesFromRegion(pRegion);
	iPartitionIdx = entGetPartitionIdx(pEnt);

	if (pRegionRules==NULL || pRegionRules->iAllowedPetsPerPlayer < 0)
		return gslTeam_GetOpenSlots(iPartitionIdx, pTeamList, GET_REF(pEnt->pTeam->hTeam));

	if (pRegionRules->iAllowedPetsPerPlayer == 0)
		return 0;

	iOpenSpots = pRegionRules->iAllowedPetsPerPlayer;
	
	if(pTeamList)
	{
		for ( i = ea32Size(&pTeamList->uiPetIDs)-1; i >= 0; i-- )
		{
			U32 uiPetID = pTeamList->uiPetIDs[i];

			if(uiPetID & CRITTERPETIDFLAG)
			{
				for ( j = 0; j < eaSize(&pEnt->pSaved->ppCritterPets); j++ )
				{
					if ( pEnt->pSaved->ppCritterPets[j]->uiPetID == uiPetID )
					{
						iOpenSpots--;
						break;
					}
				}
			}
			else
			{
				for ( j = 0; j < eaSize(&pEnt->pSaved->ppOwnedContainers); j++ )
				{
					if ( !SavedPet_IsPetAPuppet(pEnt,pEnt->pSaved->ppOwnedContainers[j]) )
					{
						Entity* pPetEnt = SavedPet_GetEntity(iPartitionIdx, pEnt->pSaved->ppOwnedContainers[j]);

						if ( pPetEnt && entGetContainerID(pPetEnt) == uiPetID )
						{
							iOpenSpots--;
							break;
						}
					}
				}
			}
		}
	}

	return MIN(gslTeam_GetOpenSlots(iPartitionIdx, pTeamList,pEnt->pTeam ? GET_REF(pEnt->pTeam->hTeam) : NULL), MAX(iOpenSpots,0));
}



static void gslTeam_RemovePetFromTeamList(int iPartitionIdx, GlobalType eTeamType, U32 uiTeamID, U32 uiPetID)
{
	SavedPetTeamList *sTeamList = NULL;
	sTeamList = gslTeam_FindPetTeamList(iPartitionIdx,eTeamType,uiTeamID);

	if(sTeamList)
	{
		ea32FindAndRemove(&sTeamList->uiPetIDs,uiPetID);

		if(ea32Size(&sTeamList->uiPetIDs) == 0)
			gslTeam_RemovePetTeamList(sTeamList);
	}

	
}

static void gslTeam_RemovePetFromListEx(Entity *pOwner, U32 uiPetID)
{
	if(pOwner && team_IsMember(pOwner))
	{
		gslTeam_RemovePetFromTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID,uiPetID);
	}
	else if(pOwner)
	{
		gslTeam_RemovePetFromTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_ENTITYPLAYER,pOwner->myContainerID,uiPetID);
	}
}

static S32 gslTeam_IsRoomAvailableForPet(Entity *pOwner, U32 uiPetID, SA_PARAM_NN_VALID S32 *bOutAlreadySummoned)
{
	SavedPetTeamList *pPetTeamList = NULL;

	*bOutAlreadySummoned = false;

	if(team_IsMember(pOwner))
	{
		pPetTeamList = gslTeam_FindPetTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID);
		if(pPetTeamList && ea32Find(&pPetTeamList->uiPetIDs,uiPetID) > -1)
		{
			*bOutAlreadySummoned = true;
			return true;
		}
	}
	else if(pOwner)
	{
		pPetTeamList = gslTeam_FindPetTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_ENTITYPLAYER,pOwner->myContainerID);
		if(pPetTeamList && ea32Find(&pPetTeamList->uiPetIDs,uiPetID) > -1)
		{
			*bOutAlreadySummoned = true;
			return true;
		}
	}

	return gslTeam_GetOpenSlotsForPlayer(pOwner, pPetTeamList) > 0;
}

static bool gslTeam_PetSpawnIfAvailableSlots(Entity *pOwner, U32 uiPetID)
{
	S32 bAlreadySummoned = false;

	if (gslTeam_IsRoomAvailableForPet(pOwner, uiPetID, &bAlreadySummoned))
	{
		if (!bAlreadySummoned)
		{
			if(team_IsMember(pOwner)) {
				gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner), GLOBALTYPE_TEAM, pOwner->pTeam->iTeamID, uiPetID);
			} else {
				gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner), GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pOwner), uiPetID);
			}
		}
		return true;
	}
	
	return false;
}

static bool gslTeam_PetSpawnRequest(Entity *pOwner, U32 uiPetID, bool bCareAboutAwayTeamSize)
{
	SavedPetTeamList *pPetTeamList = NULL;
	S32 bAlreadySummoned = false;
	S32 bOpenSlots;
	
	//gslVerifyServerTypeExistsInShard(GLOBALTYPE_TEAMSERVER);
	//RemoteCommand_aslTeam_PetSpawnRequest(GLOBALTYPE_TEAMSERVER, 0, team_GetTeamID(pOwner), pOwner->myEntityType, pOwner->myContainerID,GLOBALTYPE_ENTITYSAVEDPET,uiPetID,GetAppGlobalID());

	bOpenSlots = gslTeam_IsRoomAvailableForPet(pOwner, uiPetID, &bAlreadySummoned);
	if (bAlreadySummoned)
		return true;
		
	if(bOpenSlots)
	{
		Team *pTeam = team_GetTeam(pOwner);
		if(pTeam!=NULL && pOwner->pSaved)
		{
			int i, iAwayTeamPetsSize = ea32Size(&pOwner->pSaved->ppRequestedPetIDs);

			if (!bCareAboutAwayTeamSize)
			{
				gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID,uiPetID);
				return true;
			}
			//If the high bit is turned on, this is a critter pet
			else if(uiPetID & CRITTERPETIDFLAG)
			{
				U32 uiCritterPetID = 0;
				CritterPetRelationship *pRelationship = NULL;
				PetDef *pPetDef = NULL;

				for(i=0;i<eaSize(&pOwner->pSaved->ppCritterPets);i++)
				{
					if(pOwner->pSaved->ppCritterPets[i]->uiPetID == uiPetID)
					{
						pPetDef = GET_REF(pOwner->pSaved->ppCritterPets[i]->hPetDef);
						break;
					}
				}

				for(i=0;i<eaSize(&pOwner->pSaved->ppAllowedCritterPets);i++)
				{
					if(GET_REF(pOwner->pSaved->ppAllowedCritterPets[i]->hPet) == pPetDef)
					{
						uiCritterPetID = pOwner->pSaved->ppAllowedCritterPets[i]->uiPetID;
						break;
					}
				}

				for ( i = 0; i < ea32Size(&pOwner->pSaved->ppRequestedCritterIDs); i++)
				{
					if ( (U32)pOwner->pSaved->ppRequestedCritterIDs[i] == uiCritterPetID )
					{
						gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID,uiPetID);
						return true;
					}
				}

			}
			else
			{
				for ( i = 0; i < iAwayTeamPetsSize; i++ )
				{
					if ( (U32)pOwner->pSaved->ppRequestedPetIDs[i] == uiPetID )
					{
						gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID,uiPetID);
						return true;
					}
				}
			}

			
			
			return false;
			//gslTeam_AddPetToTeamList(GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID,uiPetID);
		}else if(pOwner){
			gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_ENTITYPLAYER,pOwner->myContainerID,uiPetID);
			return true;
		}
	}

	return false;
}

// Returns an ID for a new critter pet
U32 gslEntity_GetNewCritterPetID(Entity *pOwner)
{
	S32 i;
	U32 uiNewID = 0;

	devassert(pOwner);
	devassert(pOwner->pSaved);

	for(i = 0; i < eaSize(&pOwner->pSaved->ppCritterPets); i++)
		uiNewID = MAX(pOwner->pSaved->ppCritterPets[i]->uiPetID - CRITTERPETIDFLAG + 1, uiNewID);

	uiNewID = CRITTERPETIDFLAG + uiNewID;
	//High bit is turned on, so that when id's are used for these pets, its known that it is
	//a non persisted critter pet, instead of a persisted saved pet. 

	uiNewID |= pOwner->myContainerID << CRITTERPETIDBASEID_BITS;
	//Add OwnerID into the mix to create unique id's across all critter pets

	return uiNewID;
}

CritterPetRelationship *gslEntity_NewCritterRelationship(Entity *pOwner)
{
	CritterPetRelationship *pReturn = StructCreate(parse_CritterPetRelationship);

	pReturn->uiPetID = gslEntity_GetNewCritterPetID(pOwner);	

	eaPush(&pOwner->pSaved->ppCritterPets, pReturn);

	entity_SetDirtyBit(pOwner, parse_SavedEntityData, pOwner->pSaved, false);

	return pReturn;
}

static void gslTeam_AutoFillPetTeamList(int iPartitionIdx, SavedPetTeamList *pTeamList, Team *pTeam, RegionRules *pRules)
{
	// Get open slots
	int iOpenSlots = gslTeam_GetOpenSlots(iPartitionIdx,pTeamList,pTeam);
	Entity **ppTeamMembers = NULL;
	const PetRelationship **ppPets = NULL;
	int iSize;
	int *piCount = NULL;
	int i;

	if(iOpenSlots > 0)
	{
		//Get all possible pets
		eaCreate(&ppPets);

		if(pTeamList->eGlobalType == GLOBALTYPE_TEAM)
		{
			ea32SetSize(&piCount,TEAM_MAX_SIZE);
			for(i=0;i<TEAM_MAX_SIZE;i++)
			{
				piCount[i]=0;
			}

			if(pTeam)
			{
				int j;

				//Collect all the team members who are online and not in combat and on this paritition
				for(i=0;i<eaSize(&pTeam->eaMembers);i++)
				{
					Entity *pTeamMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);

					if(pTeamMember && pTeamMember->pSaved && pTeamMember->pChar && pTeamMember->pChar->uiTimeCombatExit == 0)
					{
						if(eaSize(&pTeamMember->pSaved->ppAllowedCritterPets))
							eaPush(&ppTeamMembers,pTeamMember);

						for(j=0;j<eaSize(&pTeamMember->pSaved->ppOwnedContainers);j++)
						{
							if(pTeamMember->pSaved->ppOwnedContainers[j]->bTeamRequest
								&& !pTeamMember->pSaved->ppOwnedContainers[j]->curEntity)
								eaPush(&ppPets,pTeamMember->pSaved->ppOwnedContainers[j]);
							else if(pTeamMember->pSaved->ppOwnedContainers[j]->curEntity)
								piCount[i]++;
						}

						for(j=0;j<eaSize(&pTeamMember->pSaved->ppCritterPets);j++)
						{
							if(pTeamMember->pSaved->ppCritterPets[j]->pEntity)
								piCount[i]++;
						}
					}
				}
			}
		}
		else
		{
			int j;
			Entity *pOwner = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pTeamList->iUniqueID);

			ea32SetSize(&piCount,1);
			piCount[0]=0;

			if (pOwner && pOwner->pSaved)
			{
				if(eaSize(&pOwner->pSaved->ppAllowedCritterPets))
					eaPush(&ppTeamMembers,pOwner);
				for(j=0;j<eaSize(&pOwner->pSaved->ppOwnedContainers);j++)
				{
					if(pOwner->pSaved->ppOwnedContainers[j]->bTeamRequest
						&& !pOwner->pSaved->ppOwnedContainers[j]->curEntity)
						eaPush(&ppPets,pOwner->pSaved->ppOwnedContainers[j]);
					else if(pOwner->pSaved->ppOwnedContainers[j]->curEntity)
						piCount[0]++;
				}
			}
		}

		iSize = eaSize(&ppPets);

		while(eaSize(&ppPets) > 0 && iOpenSlots > 0)
		{
			int iSelected = randomIntRange(0,iSize-1);
			int uiPetID = ppPets[iSelected]->conID;
			Entity *pPet = SavedPet_GetEntity(iPartitionIdx, ppPets[iSelected]);
			Entity *pOwner = pPet && pPet->pSaved ? entFromContainerID(iPartitionIdx,pPet->pSaved->conOwner.containerType,pPet->pSaved->conOwner.containerID) : NULL;
			int iOwnerSlot = pOwner ? eaFind(&ppTeamMembers,pOwner) : -1;

			if(!pPet || !pOwner || iOwnerSlot<0 || piCount[iOwnerSlot]>=pRules->iAllowedPetsPerPlayer)
			{
				if (!pPet)
				{
					U32 uiOwnerID = pOwner ? entGetContainerID(pOwner) : 0;
					ErrorDetailsf("Pet ID: %d, Owner ID: %d", uiPetID, uiOwnerID);
					Errorf("Pet entity wasn't available while trying to automatically summon pets");
				}
				eaRemove(&ppPets,iSelected);
				iSize--;
				continue;
			}

			//Spawn pet
			gslTeam_AddPetToTeamList(iPartitionIdx,pTeamList->eGlobalType,pTeamList->iUniqueID,uiPetID);
			gslSummonSavedPet(pOwner, GLOBALTYPE_ENTITYSAVEDPET, uiPetID, 0);

			iOpenSlots--;
			iSize--;
			piCount[iOwnerSlot]++;
			eaRemoveFast(&ppPets,iSelected);
		}

		//Add critter pets if there is still open slots
		while(iOpenSlots > 0 && eaSize(&ppTeamMembers) > 0)
		{
			int iSelected = randomIntRange(0,eaSize(&ppTeamMembers)-1);
			Entity* pTeamMember = ppTeamMembers[iSelected];
			int iCritterSelected = randomIntRange(0,eaSize(&pTeamMember->pSaved->ppAllowedCritterPets)-1);
			PetDefRefCont* pPetDefRefCont = pTeamMember->pSaved->ppAllowedCritterPets[iCritterSelected];
			PetDef* pPetDef = GET_REF(pPetDefRefCont->hPet);
			CritterPetRelationship *pNewRelationship = NULL;

			if (!pPetDef || piCount[iSelected]>=pRules->iAllowedPetsPerPlayer)
			{
				if (!pPetDef)
				{
					U32 uiTeamMemberID = entGetContainerID(pTeamMember);
					const char* pchPetDefName = REF_STRING_FROM_HANDLE(pPetDefRefCont->hPet);
					ErrorDetailsf("PetDef: %s, Owner ID: %d", pchPetDefName, uiTeamMemberID);
					Errorf("PetDef wasn't available while trying to automatically summon critter pets");
				}
				eaRemove(&ppTeamMembers,iSelected);
				iSize--;
				continue;
			}
			
			gslSummonCritterPet(pTeamMember,&pNewRelationship,pPetDef);
			gslTeam_AddPetToTeamList(iPartitionIdx,pTeamList->eGlobalType,pTeamList->iUniqueID,pNewRelationship->uiPetID);

			iOpenSlots--;
			piCount[iSelected]++;
		}

		ea32Destroy(&piCount);
		eaDestroy(&ppPets);
		eaDestroy(&ppTeamMembers);
	}
}

static bool Entity_DestroyCritterPet(Entity* pEntity, U32 uiPetID)
{
	if (pEntity && pEntity->pSaved)
	{
		int i, iCritterPetCount = eaSize(&pEntity->pSaved->ppCritterPets);

		for (i = 0; i < iCritterPetCount; i++)
		{
			CritterPetRelationship *pRelation = pEntity->pSaved->ppCritterPets[i];
			if (pRelation && pRelation->uiPetID == uiPetID)
			{
				if (pRelation->pEntity)
					gslQueueEntityDestroy(pRelation->pEntity);

				// Remove the array element
				eaRemove(&pEntity->pSaved->ppCritterPets, i);

				// Destroy the relation
				StructDestroy(parse_CritterPetRelationship, pRelation);

				entity_SetDirtyBit(pEntity,parse_SavedEntityData,pEntity->pSaved, false);
				return true;
			}
		}
	}

	return false;
}

static bool Entity_RemoveCritterPet(Entity* pEntity, U32 uiPetID, bool* pbFailed)
{
	if (pEntity && pEntity->pSaved)
	{
		int i, iCritterPetCount = eaSize(&pEntity->pSaved->ppCritterPets);

		for (i = 0; i < iCritterPetCount; i++)
		{
			if (pEntity->pSaved->ppCritterPets[i]->uiPetID == uiPetID)
			{
				if (pEntity->pSaved->ppCritterPets[i]->erPet > 0)
				{
					gslQueueEntityDestroy(pEntity->pSaved->ppCritterPets[i]->pEntity);
					pEntity->pSaved->ppCritterPets[i]->pEntity = NULL;
					pEntity->pSaved->ppCritterPets[i]->erPet = 0;
					entity_SetDirtyBit(pEntity,parse_SavedEntityData,pEntity->pSaved,false);
					return true;
				}
				else
				{
					if (pbFailed)
					{
						(*pbFailed) = true;
					}
					break;
				}
			}
		}
	}
	return false;
}


static bool gslEntity_HasSavedPetsToRemove(SA_PARAM_NN_VALID Entity* pEnt, SavedPetTeamList* pPetTeamList)
{
	S32 i, j;
	if (!pEnt->pSaved)
	{
		return false;
	}
	for (i = ea32Size(&pPetTeamList->uiPetIDs)-1; i >= 0; i--)
	{
		U32 uiPetID = pPetTeamList->uiPetIDs[i];
		
		if (uiPetID & CRITTERPETIDFLAG)
			continue;

		for (j = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; j >= 0; j--)
		{
			if (pEnt->pSaved->ppOwnedContainers[j]->conID == uiPetID)
			{
				return true;
			}
		}
	}
	return false;
}

static void PetStateSwap_CB(TransactionReturnVal *returnVal, SwapedPetCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		if (gbEnablePetAndPuppetLogging)
		{
			objLog(LOG_CONTAINER, cbData->oldPet.ownerType, cbData->oldPet.ownerID, 0, NULL, NULL, NULL, NULL, "SwapPetState",
				"Child EntitySavedPet[%d] and/or EntitySavedPet[%d] can't be found", cbData->oldPet.iPetContainerID, cbData->newPet.iPetContainerID);
		}
		free(cbData);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		{		
			Entity *pMasterEntity = entFromContainerIDAnyPartition(cbData->oldPet.ownerType,cbData->oldPet.ownerID);
			if (!pMasterEntity)
				pMasterEntity = entFromContainerIDAnyPartition(cbData->newPet.ownerType,cbData->newPet.ownerID);

			//TODO(MM): Find out what to do with Active
			if (cbData->oldPet.iPetContainerID != 0 && cbData->oldPet.newState == OWNEDSTATE_ACTIVE)
			{
				SavedPetCBData *temp;

				if (!(PetStateActive_summoncheck(pMasterEntity,cbData->oldPet.iPetContainerID,NULL,NULL)
					&& gslTeam_PetSpawnRequest(pMasterEntity,cbData->oldPet.iPetContainerID, true)))
				{
					temp = calloc(sizeof(SavedPetCBData), 1);
					*temp = cbData->oldPet;
					sprintf_s(SAFESTR(temp->pchReason), "PetStateSwap");
					objRequestContainerMove(objCreateManagedReturnVal(PetContainerMove_CB, temp),
						cbData->oldPet.iPetContainerType, cbData->oldPet.iPetContainerID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
				}

				if (PetStateActive_summoncheck(pMasterEntity,cbData->newPet.iPetContainerID,NULL,NULL)
					&& gslTeam_PetSpawnRequest(pMasterEntity,cbData->newPet.iPetContainerID, true))
				{
					temp = calloc(sizeof(SavedPetCBData), 1);
					*temp = cbData->newPet;
					RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(PetLocationRequest_CB, temp),
						cbData->newPet.iPetContainerType, cbData->newPet.iPetContainerID);
				}
			}
			if(pMasterEntity)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pMasterEntity);
				character_ResetPowersArray(entGetPartitionIdx(pMasterEntity), pMasterEntity->pChar, pExtract);
			}
			free(cbData);
		}
	}

}

static bool gslSwapSavedPetStateEx(Entity *pOwner, int iSlotID, 
								   Entity* pOldPet, PetRelationship* pOldRel, 
								   Entity* pNewPet, PetRelationship* pNewRel,
								   OwnedContainerState eState, U32 uiPropEntID, bool bTeamRequest,
								   AlwaysPropSlotCategory ePropCategory)
{
	SwapedPetCBData *cbData;
	PropPowerSaveList SaveList = {0};
	bool bSuccess = true;

	if (!pOwner)
	{
		return false;
	}

	cbData = calloc(sizeof(SwapedPetCBData), 1);
	cbData->oldPet.ownerType = entGetType(pOwner);
	cbData->oldPet.ownerID = entGetContainerID(pOwner);

	if (pOldPet && pOldRel)
	{
		cbData->oldPet.iPetContainerType = entGetType(pOldPet);
		cbData->oldPet.iPetContainerID = entGetContainerID(pOldPet);
		cbData->oldPet.newState = eState;
		cbData->oldPet.bOldTeamRequest = pOldRel->bTeamRequest;
	}
	else
	{
		cbData->oldPet.iPetContainerType = 0;
		cbData->oldPet.iPetContainerID = 0;
		cbData->oldPet.newState = 0;
		cbData->oldPet.bOldTeamRequest = 0;
	}

	cbData->newPet.ownerType = entGetType(pOwner);
	cbData->newPet.ownerID = entGetContainerID(pOwner);
	cbData->newPet.iPetContainerType = entGetType(pNewPet);
	cbData->newPet.iPetContainerID = entGetContainerID(pNewPet);
	cbData->newPet.newState = eState;
	cbData->newPet.bOldTeamRequest = pNewRel->bTeamRequest;

	if (pOldPet && pOldPet->pSaved)
	{
		S32 iSlot = AlwaysPropSlot_FindByPetID(pOwner, pOldPet->pSaved->iPetID, uiPropEntID, ePropCategory);
		AlwaysPropSlot* pPropSlot = eaGet(&pOwner->pSaved->ppAlwaysPropSlots, iSlot);
		AlwaysPropSlotDef* pPropSlotDef = pPropSlot ? GET_REF(pPropSlot->hDef) : NULL;
		if (pPropSlotDef)
		{
			ent_PetGetPropPowersToSave(pOwner, pOldPet, pOldRel, pPropSlotDef, &SaveList.eaData);
		}
		else
		{
			bSuccess = false;
		}
	}

	if (bSuccess && pOldPet && pOldRel)
	{
		AutoTrans_trSwapSavedPetStateEnts(LoggedTransactions_CreateManagedReturnVal("SwapPet", PetStateSwap_CB, cbData),
			GetAppGlobalType(),
			cbData->oldPet.ownerType, cbData->oldPet.ownerID,
			cbData->oldPet.iPetContainerType, cbData->oldPet.iPetContainerID, cbData->newPet.iPetContainerType, cbData->newPet.iPetContainerID, 
			eState, uiPropEntID, bTeamRequest, ePropCategory, &SaveList);
	}
	else if (bSuccess)
	{
		AutoTrans_trSwapSavedPetState(LoggedTransactions_CreateManagedReturnVal("SwapPet", PetStateSwap_CB, cbData),
			GetAppGlobalType(),
			cbData->oldPet.ownerType, cbData->oldPet.ownerID,
			cbData->newPet.iPetContainerType, cbData->newPet.iPetContainerID, 
			iSlotID, eState, uiPropEntID, bTeamRequest, ePropCategory, &SaveList);
	}
	else
	{
		SAFE_FREE(cbData);
	}
	StructDeInit(parse_PropPowerSaveList, &SaveList);
	return true;
}

static bool gslSetSavedPetStateEx(Entity *pOwner, GlobalType petType, ContainerID petConID, OwnedContainerState eState, CONST_UINT_EARRAY *peaPropEntIDs, bool bTeamRequest, CONST_UINT_EARRAY *peaOldPropEntIDs, bool bOldTeamRequest, int iSlotID, int ePropCategory)
{
	SavedPetCBData *cbData;
	PropPowerSaveList SaveList = {0};
	bool bSuccess = true;
	int i;

	if (!pOwner || !pOwner->pSaved)
	{
		return false;
	}

	cbData = calloc(sizeof(SavedPetCBData), 1);
	cbData->ownerType = entGetType(pOwner);
	cbData->ownerID = entGetContainerID(pOwner);
	cbData->iPetContainerType = petType;
	cbData->iPetContainerID = petConID;
	cbData->newState = eState;
	if (peaPropEntIDs)
		ea32Copy(&cbData->newPropEntIDs.eauiPropEntIDs, peaPropEntIDs);
	cbData->bNewTeamRequest= bTeamRequest;
	if (peaOldPropEntIDs)
		ea32Copy(&cbData->oldPropEntIDs.eauiPropEntIDs, peaOldPropEntIDs);
	cbData->bOldTeamRequest= bOldTeamRequest;
	cbData->iSlotID = iSlotID;

	if(peaOldPropEntIDs)
	{
		for (i = 0; i < ea32Size(peaOldPropEntIDs); i++)
		{
			U32 uiOldPropEntID = (*peaOldPropEntIDs)[i];
			if (ea32Find(peaPropEntIDs, uiOldPropEntID) < 0)
			{
				PetRelationship* pPetRel = SavedPet_GetPetFromContainerID(pOwner, petConID, false);
				Entity* pPetEnt = pPetRel ? GET_REF(pPetRel->hPetRef) : NULL;
				if (pPetEnt && pPetEnt->pSaved)
				{
					S32 iSlot = AlwaysPropSlot_FindByPetID(pOwner, pPetEnt->pSaved->iPetID, uiOldPropEntID, ePropCategory);
					AlwaysPropSlot* pPropSlot = eaGet(&pOwner->pSaved->ppAlwaysPropSlots, iSlot);
					AlwaysPropSlotDef* pPropSlotDef = pPropSlot ? GET_REF(pPropSlot->hDef) : NULL;
					if (pPropSlotDef)
					{
						ent_PetGetPropPowersToSave(pOwner, pPetEnt, pPetRel, pPropSlotDef, &SaveList.eaData);
					}
					else
					{
						bSuccess = false;
					}
				}
			}
		}
	}
	
	if (bSuccess)
	{
		AutoTrans_trChangeSavedPetState(LoggedTransactions_CreateManagedReturnVal("SummonPet", PetStateSet_CB, cbData),
			GetAppGlobalType(),
			cbData->ownerType,
			cbData->ownerID,
			cbData->iPetContainerType,
			cbData->iPetContainerID,
			cbData->newState,
			&cbData->newPropEntIDs,
			cbData->bNewTeamRequest,
			cbData->iSlotID,
			ePropCategory,
			&SaveList);
	}
	else
	{
		SAFE_FREE(cbData);
	}
	StructDeInit(parse_PropPowerSaveList, &SaveList);
	return true;
}

static bool gslSetSavedPetState(Entity *pOwner, GlobalType petType, ContainerID petID, 
								OwnedContainerState eState, U32 **peaPropEntIDs, bool bTeamRequest,
								AlwaysPropSlotCategory ePropCategory)
{
	return gslSetSavedPetStateEx(pOwner,petType,petID,eState,peaPropEntIDs,bTeamRequest,0,0,-1,ePropCategory);
}

static bool gslEntity_RemoveLeastPreferredPet(Entity* pEnt, SavedPetTeamList* pPetTeamList)
{
	S32 i;
	S32 iLeastPreferredIndex = -1;
	S32 iRemoveIndex = -1;
	Entity* pLeastPreferredPetEnt = NULL;
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	for (i = ea32Size(&pPetTeamList->uiPetIDs)-1; i >= 0; i--)
	{
		S32 iPreferredIndex;
		U32 uiPetID = pPetTeamList->uiPetIDs[i];

		if (uiPetID & CRITTERPETIDFLAG)
			continue;

		iPreferredIndex = ea32Find(&pEnt->pSaved->ppPreferredPetIDs, uiPetID);
		if (iPreferredIndex==-1)
		{
			Entity *pSavedPetEnt = entity_GetSubEntity(iPartitionIdx, pEnt, GLOBALTYPE_ENTITYSAVEDPET, uiPetID);
			if (pSavedPetEnt)
			{
				iRemoveIndex = i;
				pLeastPreferredPetEnt = pSavedPetEnt;
				break;
			}
		}
		else if (iPreferredIndex > iLeastPreferredIndex)
		{
			Entity *pSavedPetEnt = entity_GetSubEntity(iPartitionIdx, pEnt, GLOBALTYPE_ENTITYSAVEDPET, uiPetID);
			if (pSavedPetEnt)
			{
				iLeastPreferredIndex = iPreferredIndex;
				iRemoveIndex = i;
				pLeastPreferredPetEnt = pSavedPetEnt;
			}
		}
	}

	if (iRemoveIndex >= 0 && gslUnSummonSavedPet(pLeastPreferredPetEnt))
	{
		if (gConf.bDeactivateHenchmenWhenDroppedFromTeam)
		{
			gslSetSavedPetState(pEnt, GLOBALTYPE_ENTITYSAVEDPET, 
				entGetContainerID(pLeastPreferredPetEnt), 
				OWNEDSTATE_OFFLINE, 0, 0, -1);
		}
		ea32RemoveFast(&pPetTeamList->uiPetIDs,iRemoveIndex);
		ea32FindAndRemove(&pEnt->pSaved->ppRequestedPetIDs,pPetTeamList->uiPetIDs[iRemoveIndex]);
		return true;
	}
	return false;
}

static S32 gslTeam_RemovePetsForEnt(Entity* pEnt, 
									S32 iTeamMatesActive, 
									S32 iTeamPetsActive, 
									SavedPetTeamList* pPetTeamList, 
									bool bRemoveOne,
									int* piFailedCount)
{
	
	if (pEnt && pEnt->pSaved && pEnt->pPlayer && pPetTeamList)
	{
		S32 i;
		if (iTeamMatesActive + iTeamPetsActive > TEAM_MAX_SIZE)
		{
			// Remove critter pets first
			for (i = ea32Size(&pPetTeamList->uiPetIDs) - 1; i >= 0; i--)
			{
				U32 uiPetID = pPetTeamList->uiPetIDs[i];

				if (uiPetID & CRITTERPETIDFLAG)
				{
					bool bFailed = false;
					if (Entity_RemoveCritterPet(pEnt, uiPetID, &bFailed))
					{
						ea32RemoveFast(&pPetTeamList->uiPetIDs,i);
						ea32FindAndRemove(&pEnt->pSaved->ppRequestedCritterIDs, uiPetID);
						iTeamPetsActive = ea32Size(&pPetTeamList->uiPetIDs);
		
						if (bRemoveOne)
							return iTeamPetsActive;
						if (iTeamMatesActive + iTeamPetsActive <= TEAM_MAX_SIZE)
							break;
					}
					else if (bFailed && piFailedCount)
					{
						(*piFailedCount)++;
					}
				}
			}
		}

		// Try removing saved pets, starting with the least preferred
		while (iTeamMatesActive + iTeamPetsActive > TEAM_MAX_SIZE &&	
			   gslEntity_HasSavedPetsToRemove(pEnt, pPetTeamList))
		{
			if (gslEntity_RemoveLeastPreferredPet(pEnt, pPetTeamList))
			{
				iTeamPetsActive = ea32Size(&pPetTeamList->uiPetIDs);
				if (bRemoveOne)
					break;
			}
			else
			{
				if (piFailedCount)
				{
					(*piFailedCount)++;
				}
				break;
			}
		}
	}
	return iTeamPetsActive;
}

void gslTeam_UnSummonPetsForEnt(int iPartitionIdx, Entity* pEnt)
{
	SavedPetTeamList *pTeamList;
	S32 i;

	if (!pEnt || !pEnt->pSaved || !pEnt->pPlayer)
		return;

	ea32Clear(&pEnt->pSaved->uiRemovedAwayTeamPetIDs);

	// First we need to get the list of entities on the team. Find the SavedPetTeamList if it currently exists
	if(team_IsMember(pEnt))
		pTeamList = gslTeam_FindPetTeamList(iPartitionIdx, GLOBALTYPE_TEAM, pEnt->pTeam->iTeamID);
	else
		pTeamList = gslTeam_FindPetTeamList(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID);

	if(!pTeamList)
		return;

	// First remove all saved pets. Because the RemovedAwayTeamPetIDs list is a queue, we want to put the
	// saved pets first so that when we resummon them, they are preferentially summoned instead of critter pets
	for (i = ea32Size(&pEnt->pSaved->ppAwayTeamPetID) - 1; i >= 0; i--)
	{
		U32 uiPetID = pEnt->pSaved->ppAwayTeamPetID[i];
		Entity *pSavedPetEnt = entity_GetSubEntity(iPartitionIdx, pEnt, GLOBALTYPE_ENTITYSAVEDPET, uiPetID);
		if (pSavedPetEnt)
		{
			ea32Push(&pEnt->pSaved->uiRemovedAwayTeamPetIDs, uiPetID);
			gslUnSummonSavedPet(pSavedPetEnt);
		}
	}

	// Now remove all critter pets and place them on the RemovedAwayTeamPetIDs queue
	for (i = ea32Size(&pTeamList->uiPetIDs) - 1; i >= 0; i--)
	{
		U32 uiPetID = pTeamList->uiPetIDs[i];
		if (uiPetID & CRITTERPETIDFLAG && Entity_RemoveCritterPet(pEnt, uiPetID, NULL))
		{
			ea32RemoveFast(&pTeamList->uiPetIDs,i);
			ea32Push(&pEnt->pSaved->uiRemovedAwayTeamPetIDs, uiPetID);
		}
	}
}

void gslTeam_ReSummonPetsForEnt(Entity *pOwner)
{
	SavedPetTeamList *pTeamList = NULL;
	RegionRules *pRules = NULL;
	Team *pTeam = NULL;
	Vec3 vOwnerPos;
	int iPartitionIdx;
	int iOpenSlots;
	int iPetCount = 0;
	int i;

	if(!pOwner)
		return;

	iPartitionIdx = entGetPartitionIdx(pOwner);
	entGetPos(pOwner, vOwnerPos);
	pRules = RegionRulesFromVec3(vOwnerPos);
	pTeam = team_GetTeam(pOwner);

	// First we need to get the list of entities on the team. Find the SavedPetTeamList if it currently exists, or create one if it does not
	if(team_IsMember(pOwner))
		pTeamList = gslTeam_FindPetTeamList(iPartitionIdx, GLOBALTYPE_TEAM, pOwner->pTeam->iTeamID);
	else
		pTeamList = gslTeam_FindPetTeamList(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pOwner->myContainerID);

	if(!pTeamList)
		pTeamList = gslTeam_CreateNewTeamList(iPartitionIdx, team_IsMember(pOwner) ? GLOBALTYPE_TEAM : GLOBALTYPE_ENTITYPLAYER, team_IsMember(pOwner) ? pOwner->pTeam->iTeamID : pOwner->myContainerID);

	if(!pTeamList)
		return;

	// Get the count of open slots on the team
	iOpenSlots = gslTeam_GetOpenSlots(iPartitionIdx, pTeamList, pTeam);
	if(iOpenSlots <= 0)
		return;

	// Next we need a count of pets that are currently spawned for the player.
	// Theoretically there shouldn't be any if this is being used by PetsDisabled
	// volumes, but I'm putting these here so that this function will play nice
	// with other systems that might someday use it
	for(i=0; i<eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		if(pOwner->pSaved->ppOwnedContainers[i]->curEntity)
			iPetCount++;
	}

	for(i=0; i<eaSize(&pOwner->pSaved->ppCritterPets); i++)
	{
		if(pOwner->pSaved->ppCritterPets[i]->pEntity)
			iPetCount++;
	}

	// Iterate through the list of pet IDs that were previously removed and summon
	// as many as we can until the team is either full, or the region rules allow
	// no more pets to be summoned
	while (ea32Size(&pOwner->pSaved->uiRemovedAwayTeamPetIDs) && iOpenSlots > 0 && iPetCount < pRules->iAllowedPetsPerPlayer)
	{
		int uiPetID = pOwner->pSaved->uiRemovedAwayTeamPetIDs[0];

		if (uiPetID & CRITTERPETIDFLAG) // Critter Pet
		{
			PetDefRefCont* pPetDefRefCont = pOwner->pSaved->ppAllowedCritterPets[0];
			PetDef* pPetDef = GET_REF(pPetDefRefCont->hPet);
			CritterPetRelationship *pNewRelationship = NULL;

			if (!pPetDef)
			{
				U32 uiOwnerID = entGetContainerID(pOwner);
				const char* pchPetDefName = REF_STRING_FROM_HANDLE(pPetDefRefCont->hPet);
				ErrorDetailsf("PetDef: %s, Owner ID: %d", pchPetDefName, uiOwnerID);
				Errorf("PetDef wasn't available while trying to resummon critter pets");
				ea32Remove(&pOwner->pSaved->uiRemovedAwayTeamPetIDs, 0);
				continue;
			}
			
			gslSummonCritterPet(pOwner, &pNewRelationship, pPetDef);
			gslTeam_AddPetToTeamList(iPartitionIdx, pTeamList->eGlobalType, pTeamList->iUniqueID, pNewRelationship->uiPetID);
		}
		else // Saved Pet
		{
			Entity *pPet = entity_GetSubEntity(iPartitionIdx, pOwner, GLOBALTYPE_ENTITYSAVEDPET, uiPetID);
			if (!pPet)
			{
				U32 uiOwnerID = entGetContainerID(pOwner);
				ErrorDetailsf("Pet ID: %d, Owner ID: %d", uiPetID, uiOwnerID);
				Errorf("Pet entity wasn't available while trying to resummon pets");
				ea32Remove(&pOwner->pSaved->uiRemovedAwayTeamPetIDs, 0);
				continue;
			}

			gslTeam_AddPetToTeamList(iPartitionIdx, pTeamList->eGlobalType, pTeamList->iUniqueID, uiPetID);
			gslSummonSavedPet(pOwner, GLOBALTYPE_ENTITYSAVEDPET, uiPetID, 0);
		}

		iOpenSlots--;
		iPetCount++;
		ea32Remove(&pOwner->pSaved->uiRemovedAwayTeamPetIDs, 0);
	}

	ea32Clear(&pOwner->pSaved->uiRemovedAwayTeamPetIDs);
}

static void gslPetTeamList_convert(int iPartitionIdx, GlobalType eOldTeam, ContainerID iOldTeamID, GlobalType eNewTeam, ContainerID iNewTeamID )
{
	SavedPetTeamList *pOldTeam = gslTeam_FindPetTeamList(iPartitionIdx,eOldTeam,iOldTeamID);

	if(pOldTeam)
	{
		int i;

		for(i=0;i<ea32Size(&pOldTeam->uiPetIDs);i++)
		{
			gslTeam_AddPetToTeamList(iPartitionIdx,eNewTeam,iNewTeamID,pOldTeam->uiPetIDs[i]);
		}

		gslTeam_RemovePetTeamList(pOldTeam);
	}
}

void gslPetTeamList_LeaveTeam(Entity *pLeaveEnt, Team *pTeam)
{
	int i, j;

	if(!pLeaveEnt || !pLeaveEnt->pSaved)
		return;

	// Add the owner's pets to a new pet list
	for(i=0;i<eaSize(&pLeaveEnt->pSaved->ppOwnedContainers);i++)
	{
		if(pLeaveEnt->pSaved->ppOwnedContainers[i]->curEntity)
			gslTeam_AddPetToTeamList(entGetPartitionIdx(pLeaveEnt),GLOBALTYPE_ENTITYPLAYER,pLeaveEnt->myContainerID,pLeaveEnt->pSaved->ppOwnedContainers[i]->conID);
	}

	for(i=0;i<eaSize(&pLeaveEnt->pSaved->ppCritterPets);i++)
	{
		if(pLeaveEnt->pSaved->ppCritterPets[i]->pEntity)
			gslTeam_AddPetToTeamList(entGetPartitionIdx(pLeaveEnt),GLOBALTYPE_ENTITYPLAYER,pLeaveEnt->myContainerID,pLeaveEnt->pSaved->ppCritterPets[i]->uiPetID);
	}

	// Remove pets from the old pet team list
	if (pTeam)
	{
		SavedPetTeamList* pPetTeamList = gslTeam_FindPetTeamList(entGetPartitionIdx(pLeaveEnt), GLOBALTYPE_TEAM, pTeam->iContainerID);
		if (pPetTeamList)
		{
			for (i = ea32Size(&pPetTeamList->uiPetIDs)-1; i >= 0; i--)
			{
				U32 uPetID = pPetTeamList->uiPetIDs[i];
				for (j = eaSize(&pLeaveEnt->pSaved->ppOwnedContainers)-1; j >= 0; j--)
				{
					if (pLeaveEnt->pSaved->ppOwnedContainers[j]->conID == uPetID)
					{
						ea32RemoveFast(&pPetTeamList->uiPetIDs, i);
						break;
					}
				}
				if (j >= 0)
				{
					continue;
				}
				for (j = eaSize(&pLeaveEnt->pSaved->ppCritterPets)-1; j >= 0; j--)
				{
					if (pLeaveEnt->pSaved->ppCritterPets[j]->uiPetID == uPetID)
					{
						ea32RemoveFast(&pPetTeamList->uiPetIDs, i);
						break;
					}
				}
			}
			if (ea32Size(&pPetTeamList->uiPetIDs) == 0)
			{
				gslTeam_RemovePetTeamList(pPetTeamList);
			}
		}
	}
}

void gslTeam_CheckPetCount(int iPartitionIdx, Team *pTeam, Entity* pJoinEnt)
{
	bool bRetry = false;
	int iRemoveFailedCount = 0;
	int i, j, iTeamPetsActive, iTeamMatesActive = 0;
	SavedPetTeamList *pPetTeamList;

	if(!pTeam)
		return;

	if (pJoinEnt)
	{
		//Convert old team to new team
		gslPetTeamList_convert(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pJoinEnt->myContainerID,GLOBALTYPE_TEAM,pTeam->iContainerID);
	}

	pPetTeamList = gslTeam_FindPetTeamList(iPartitionIdx,GLOBALTYPE_TEAM,pTeam->iContainerID);

	if(!pPetTeamList)
	{
		iTeamPetsActive = 0;
	}
	else
	{
		iTeamPetsActive = ea32Size(&pPetTeamList->uiPetIDs);
	}

	//Get active players for that team
	iTeamMatesActive = team_NumMembersThisServerAndPartition(pTeam,iPartitionIdx);

	// 1.) Remove pets from joining entity first, if the team size is too large
	iTeamPetsActive = gslTeam_RemovePetsForEnt(pJoinEnt, 
											   iTeamMatesActive, 
											   iTeamPetsActive, 
											   pPetTeamList, 
											   false, 
											   &iRemoveFailedCount);

	// 2.) Remove pets from the rest of the team, if the team size is still too large
	while (iTeamMatesActive + iTeamPetsActive > TEAM_MAX_SIZE)
	{
		for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--)
		{
			Entity *pEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);

			if(pEnt && pEnt != pJoinEnt)
			{
				// On the first pass, remove one pet from each player. On the second pass, remove all pets.
				iTeamPetsActive = gslTeam_RemovePetsForEnt(pEnt,
														   iTeamMatesActive,
														   iTeamPetsActive,
														   pPetTeamList,
														   !bRetry,
														   &iRemoveFailedCount);

				if (iTeamMatesActive + iTeamPetsActive <= TEAM_MAX_SIZE)
				{
					break;
				}
			}
		}
		if (bRetry)
		{
			break;
		}
		bRetry = true;
	}

	if (iTeamMatesActive + iTeamPetsActive > TEAM_MAX_SIZE)
	{
		int iOnMapPetCount = 0;
		if (pPetTeamList)
		{
			// Count the number of pets on the map
			for (i = ea32Size(&pPetTeamList->uiPetIDs)-1; i >= 0; i--)
			{
				U32 uPetID = pPetTeamList->uiPetIDs[i];
				if (uPetID & CRITTERPETIDFLAG)
				{
					Entity* pOwner;
					U32 uOwnerID = uPetID;
					uOwnerID &= ~CRITTERPETIDFLAG;
					uOwnerID >>= CRITTERPETIDBASEID_BITS;
					pOwner = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, uOwnerID);
					
					if (pOwner && pOwner->pSaved)
					{
						for (j = eaSize(&pOwner->pSaved->ppCritterPets)-1; j >= 0; j--)
						{
							if (pOwner->pSaved->ppCritterPets[j]->uiPetID == uPetID)
							{
								iOnMapPetCount++;
								break;
							}
						}
					}
				}
				else if (entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET, uPetID))
				{
					iOnMapPetCount++;
				}
			}
		}
		ErrorDetailsf("Player Count: %d, Pet Count: %d, Pet Count On Map: %d, Removal Failure Count: %d", 
			iTeamMatesActive, iTeamPetsActive, iOnMapPetCount, iRemoveFailedCount);
		Errorf("Player Joined Team: Team couldn't be reduced to a valid size!");
	}

	if(pPetTeamList && ea32Size(&pPetTeamList->uiPetIDs) == 0)
		gslTeam_RemovePetTeamList(pPetTeamList);
}

static void PetStateSet_CB(TransactionReturnVal *returnVal, SavedPetCBData *cbData)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		if (gbEnablePetAndPuppetLogging)
		{
			objLog(LOG_CONTAINER, cbData->ownerType, cbData->ownerID, 0, NULL, NULL, NULL, NULL, "SetPetState",
				"Child EntitySavedPet[%d] can't be found", cbData->iPetContainerID);
		}
		free(cbData);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		{	
			switch (cbData->newState)
			{
				xcase OWNEDSTATE_AUTO_SUMMON:
				{
					RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(PetLocationRequest_CB, cbData),
						cbData->iPetContainerType, cbData->iPetContainerID);
				}
				xcase OWNEDSTATE_AUTO_CONTROL:
				{
					RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(PetControlRequest_CB, cbData),
						cbData->iPetContainerType, cbData->iPetContainerID);
				}
				xcase OWNEDSTATE_ACTIVE:
				{
					Entity *pMasterEntity = entFromContainerIDAnyPartition(cbData->ownerType,cbData->ownerID);

					if (cbData->bNewTeamRequest
						&& !cbData->bOldTeamRequest
						&& PetStateActive_summoncheck(pMasterEntity,cbData->iPetContainerID,NULL,NULL)
						&& gslTeam_PetSpawnRequest(pMasterEntity,cbData->iPetContainerID, true))
					{
						RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(PetLocationRequest_CB, cbData),
						cbData->iPetContainerType, cbData->iPetContainerID);
					}
					else if(!(cbData->bNewTeamRequest))
					{
						sprintf_s(SAFESTR(cbData->pchReason), "PetStateSet");
						objRequestContainerMove(objCreateManagedReturnVal(PetContainerMove_CB, cbData),
							cbData->iPetContainerType, cbData->iPetContainerID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
					}
					else
					{
						free(cbData);
					}

					if(pMasterEntity)
					{
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pMasterEntity);
						character_ResetPowersArray(entGetPartitionIdx(pMasterEntity), pMasterEntity->pChar, pExtract);
					}
				}
				xcase OWNEDSTATE_OFFLINE:
				{
					Entity *pMasterEntity = entFromContainerIDAnyPartition(cbData->ownerType,cbData->ownerID);

					if(pMasterEntity)
					{
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pMasterEntity);
						character_ResetPowersArray(entGetPartitionIdx(pMasterEntity), pMasterEntity->pChar, pExtract);
					}

					sprintf_s(SAFESTR(cbData->pchReason), "PetStateSet");
					objRequestContainerMove(objCreateManagedReturnVal(PetContainerMove_CB, cbData),
						cbData->iPetContainerType, cbData->iPetContainerID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
				}
			}
		}
	}

}

AUTO_TRANSACTION
	ATR_LOCKS(pEntity, ".Psaved.Pppreferredpetids, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets");
enumTransactionOutcome trEntity_SetPreferredPet(ATR_ARGS, NOCONST(Entity)* pEntity, U32 uiPetID, S32 iIndex)
{
	S32 i;
	if (!trhEntity_CanSetPreferredPet(pEntity, uiPetID, iIndex))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (iIndex == ea32Size(&pEntity->pSaved->ppPreferredPetIDs))
	{
		if (uiPetID)
		{
			for (i = ea32Size(&pEntity->pSaved->ppPreferredPetIDs)-1; i >= 0; i--)
			{
				if ((U32)pEntity->pSaved->ppPreferredPetIDs[i] == uiPetID) 
				{
					return TRANSACTION_OUTCOME_FAILURE;
				}
			}
			ea32Push(&pEntity->pSaved->ppPreferredPetIDs, uiPetID);
		}
		else
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	else
	{
		U32 uiID = ea32Get(&pEntity->pSaved->ppPreferredPetIDs, iIndex);
		if (uiID == uiPetID)
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		else if (uiPetID)
		{
			for (i = ea32Size(&pEntity->pSaved->ppPreferredPetIDs)-1; i >= 0; i--)
			{
				if ((U32)pEntity->pSaved->ppPreferredPetIDs[i] == uiPetID)
				{
					ea32Swap(&pEntity->pSaved->ppPreferredPetIDs, i, iIndex);
					break;
				}
			}
			if (i < 0)
			{
				ea32Set(&pEntity->pSaved->ppPreferredPetIDs, uiPetID, iIndex);
			}
		}
		else
		{
			ea32Remove(&pEntity->pSaved->ppPreferredPetIDs, iIndex);
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_NAME(EntitySetPreferredPet) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void Entity_SetPreferredPet(Entity* pEntity, U32 uiPetID, S32 iIndex)
{
	if (!trhEntity_CanSetPreferredPet(CONTAINER_NOCONST(Entity, pEntity), uiPetID, iIndex))
	{
		return;
	}

	AutoTrans_trEntity_SetPreferredPet(NULL, GLOBALTYPE_GAMESERVER, 
		entGetType(pEntity), entGetContainerID(pEntity),
		uiPetID, iIndex);
}

bool Entity_SaveAwayTeamPets(Entity *pEntity, AwayTeamMembers* pMembers)
{
	if ( pEntity->pSaved && pMembers ) 
	{
		//destroy old away team pets
		ea32Destroy( &pEntity->pSaved->ppRequestedPetIDs );
		ea32Destroy( &pEntity->pSaved->ppRequestedCritterIDs );
				
		if ( eaSize(&pMembers->eaMembers) )
		{
			bool bFoundPlayerEntity = false;
			S32 c, i, iPlayerCount = 0;
			for ( c = 0; c < eaSize( &pMembers->eaMembers ); c++ )
			{
				if ( pMembers->eaMembers[c]->eEntType == GLOBALTYPE_ENTITYPLAYER )
					iPlayerCount++;

				if (  pMembers->eaMembers[c]->iEntID == pEntity->myContainerID )
					bFoundPlayerEntity = true;
			}

			if ( bFoundPlayerEntity == false || iPlayerCount < 1 )
			{
				return false;
			}

			//Update the away team pet states
			AutoTrans_trEntity_UpdateAwayTeamPets(LoggedTransactions_CreateManagedReturnVal("SaveAwayTeamPetsToSelf",NULL,NULL),
				GLOBALTYPE_GAMESERVER,entGetType(pEntity),entGetContainerID(pEntity),pMembers);

			//always save a list of pet ids to spawn
			for ( c = 0; c < eaSize( &pMembers->eaMembers ); c++ )
			{
				if ( pMembers->eaMembers[c]->eEntType == GLOBALTYPE_ENTITYPLAYER )
					continue;

				if ( pMembers->eaMembers[c]->eEntType == GLOBALTYPE_ENTITYCRITTER )
				{
					if(pMembers->eaMembers[c]->iEntID != pEntity->myContainerID)
						continue;

					ea32Push(&pEntity->pSaved->ppRequestedCritterIDs,pMembers->eaMembers[c]->uiCritterPetID);
				}
				else
				{
					for ( i = 0; i < eaSize(&pEntity->pSaved->ppOwnedContainers); i++ )
					{
						U32 iPetID = pEntity->pSaved->ppOwnedContainers[i]->conID;

						if ( iPetID == pMembers->eaMembers[c]->iEntID )
						{
							ea32Push( &pEntity->pSaved->ppRequestedPetIDs, pMembers->eaMembers[c]->iEntID );
							break;
						}
					}
				}
				
			}
		}
		return true;
	}

	return false;
}

void Entity_SaveAwayTeamCritterPets(Entity *pEntity,AwayTeamMembers *pAwayTeamMembers)
{
	int i;
	// Do NOT destroy the ppCritterPets array if pAwayTeamMembers is NULL.
	// It's NULL when we are on a ground map and beaming up somewhere,
	// in which case let the critters clean themselves up
	if(!pAwayTeamMembers) 
		return;

	eaDestroyStruct(&pEntity->pSaved->ppCritterPets,parse_CritterPetRelationship);

	for(i=0;i<eaSize(&pAwayTeamMembers->eaMembers);i++)
	{
		if(pAwayTeamMembers->eaMembers[i]->eEntType == GLOBALTYPE_ENTITYCRITTER
			&& pAwayTeamMembers->eaMembers[i]->iEntID == pEntity->myContainerID)
		{
			int j;

			for(j=0;j<eaSize(&pEntity->pSaved->ppAllowedCritterPets);j++)
			{
				if(pEntity->pSaved->ppAllowedCritterPets[j]->uiPetID == pAwayTeamMembers->eaMembers[i]->uiCritterPetID)
				{
					CritterPetRelationship *pNewRelationship = gslEntity_NewCritterRelationship(pEntity);
					PetDef *pPetDef = GET_REF(pEntity->pSaved->ppAllowedCritterPets[j]->hPet);

					SET_HANDLE_FROM_REFERENT("PetDef",pPetDef,pNewRelationship->hPetDef);
					break;
				}
			}
		}
	}
}

bool gslControlSavedPet(Entity *pOwner, GlobalType petType, ContainerID petID)
{
	SavedPetCBData *cbData = calloc(sizeof(SavedPetCBData), 1);

	if (!pOwner)
	{
		free(cbData);
		return false;
	}

	cbData->ownerType = entGetType(pOwner);
	cbData->ownerID = entGetContainerID(pOwner);
	cbData->iPetContainerType = petType;
	cbData->iPetContainerID = petID;

	RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(PetControlRequest_CB, cbData),
		petType, petID);
	return true;
}

void gslSavedPet_SetSpawnLocationRotationForPet(S32 iPartitionIdx, Entity *pOwner, Entity *pPet)
{
	if (pOwner && pPet)
	{
		Vec3 vNewPos;
		Quat qNewRot;

		// try and get the spawn location from the entity's AI formation first
		if (!aiFormation_GetSpawnLocationForEntity(pPet, vNewPos, qNewRot))
		{
			int bFloorFound = false;
			#define SPAWN_SNAP_TO_DIST   7

			entGetPos(pOwner,vNewPos);
			entGetRot(pOwner,qNewRot);
			Entity_SavedPetGetOriginalSpawnPos(pOwner,NULL,qNewRot,vNewPos);
			Entity_GetPositionOffset(iPartitionIdx, NULL, qNewRot, Entity_GetSeedNumber(iPartitionIdx, pPet,vNewPos), vNewPos, pOwner->iBoxNumber);
			worldSnapPosToGround(iPartitionIdx, vNewPos, SPAWN_SNAP_TO_DIST, -SPAWN_SNAP_TO_DIST, &bFloorFound);

			if (gConf.bNewAnimationSystem && bFloorFound) {
				mrSurfaceSetSpawnedOnGround(pPet->mm.mrSurface, true);
			}
		}

		entSetRot(pPet, qNewRot, true, "spawnLoc");
		entSetPos(pPet, vNewPos, 1, "spawnLoc");
	}
}

// Creates the actual entity for a critter pet relationship
void gslCritterPetCreateEntity(SA_PARAM_NN_VALID Entity *pOwner, SA_PARAM_NN_VALID CritterPetRelationship *pCritterRelationship)
{
	PetDef *pPetDef = GET_REF(pCritterRelationship->hPetDef);

	if (pCritterRelationship->pEntity == NULL && pPetDef)
	{
		Entity *pEntity;
		CritterDef *pCritterDef = GET_REF(pPetDef->hCritterDef);
		CritterCreateParams createParams = {0};
		int iPartitionIdx = entGetPartitionIdx(pOwner);

		createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
		createParams.iPartitionIdx = iPartitionIdx;
		if (pOwner->pChar->bLoaded)
			createParams.iLevel = pOwner->pChar->iLevelCombat;
		else
			createParams.iLevel = entity_GetSavedExpLevel(pOwner);
		createParams.pFaction = GET_REF(pOwner->hFaction);
		createParams.pDisplayNameMsg = GET_REF(pCritterRelationship->hDisplayName);
		createParams.erOwner = entGetRef(pOwner);
		createParams.pCostume = GET_REF(pCritterRelationship->hCostume);

		ANALYSIS_ASSUME(pCritterDef != NULL);
		pEntity = critter_CreateByDef(pCritterDef, &createParams, pCritterDef->pchFileName, true);


		pCritterRelationship->pEntity = pEntity;
		pCritterRelationship->erPet = entGetRef(pEntity);

		entity_SetDirtyBit(pOwner, parse_SavedEntityData, pOwner->pSaved, false);

		entSetCodeFlagBits(pEntity,ENTITYFLAG_CRITTERPET);

		gslSavedPet_SetSpawnLocationRotationForPet(iPartitionIdx, pOwner, pEntity); 

		if(!IS_HANDLE_ACTIVE(pCritterRelationship->hCostume))
		{
			SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict,REF_STRING_FROM_HANDLE(pEntity->costumeRef.hReferencedCostume),pCritterRelationship->hCostume);
		}

		if(!IS_HANDLE_ACTIVE(pCritterRelationship->hDisplayName))
		{
			SET_HANDLE_FROM_STRING(gMessageDict,REF_STRING_FROM_HANDLE(pEntity->pCritter->hDisplayNameMsg),pCritterRelationship->hDisplayName);
		}
	}
}

bool gslSummonCritterPet(Entity *pOwner, CritterPetRelationship **ppCritterRelationship, PetDef *pPetDef)
{
	Entity *pEntity = NULL;
	CritterPetRelationship *pCritterRelationship = ppCritterRelationship ? (*ppCritterRelationship) : NULL;

	if(!pOwner || !pOwner->pSaved)
		return false;

	if(!pCritterRelationship)
	{
		int i;

		if(!pPetDef)
			return false;

		for(i=0;i<eaSize(&pOwner->pSaved->ppCritterPets);i++)
		{
			if(!pOwner->pSaved->ppCritterPets[i]->pEntity &&
				GET_REF(pOwner->pSaved->ppCritterPets[i]->hPetDef) == pPetDef)
			{
				pCritterRelationship = pOwner->pSaved->ppCritterPets[i];

				break;
			}
		}

		if(!pCritterRelationship)
		{
			pCritterRelationship = gslEntity_NewCritterRelationship(pOwner);
			SET_HANDLE_FROM_REFERENT("PetDef",pPetDef,pCritterRelationship->hPetDef);
		}

		if(ppCritterRelationship)
			(*ppCritterRelationship) = pCritterRelationship;
	}
	else
	{
		PetDef *pCritterPetDef = GET_REF(pCritterRelationship->hPetDef);

		if(pCritterPetDef != pPetDef)
			pPetDef = pCritterPetDef; //TODO(MM): These don't match, so we have a problem. For now just use the one defined in the critter relationship
	}

	if(pCritterRelationship->pEntity)
		return true;	

	if(pPetDef)
	{
		// Create the entity
		gslCritterPetCreateEntity(pOwner, pCritterRelationship);

		return true;
	}
	return false;
}
AUTO_COMMAND ACMD_NAME(SummonCritterPet);
void gslSummonCritterPet_command(Entity *pOwner, const char *pchPetDef)
{
	PetDef *pPetDef = RefSystem_ReferentFromString("PetDef",pchPetDef);

	if(pPetDef)
		gslSummonCritterPet(pOwner,NULL,pPetDef);
}

AUTO_COMMAND ACMD_NAME(SummonCritterPetExisting);
void gslSummonCritterPetExisting_command(Entity *pOwner, int iPetNum)
{
	if(pOwner && pOwner->pSaved && iPetNum < eaSize(&pOwner->pSaved->ppCritterPets))
	{
		gslSummonCritterPet(pOwner,&pOwner->pSaved->ppCritterPets[iPetNum],NULL);
	}
}

bool gslSummonSavedPet(Entity *pOwner, GlobalType petType, ContainerID petID, int iSummonCount)
{
	SavedPetCBData *cbData = calloc(sizeof(SavedPetCBData), 1);

	if (!pOwner)
	{
		free(cbData);
		return false;
	}

	cbData->ownerType = entGetType(pOwner);
	cbData->ownerID = entGetContainerID(pOwner);
	cbData->iPetContainerType = petType;
	cbData->iPetContainerID = petID;
	cbData->iSeedID = iSummonCount;

	RemoteCommand_ContainerGetOwner(objCreateManagedReturnVal(PetLocationRequest_CB, cbData),
		petType, petID);
	return true;

}

bool gslUnSummonSavedPet(Entity *pEntityToDestroy)
{
	SavedPetCBData *cbData;

	if (!pEntityToDestroy || entGetType(pEntityToDestroy) != GLOBALTYPE_ENTITYSAVEDPET || !pEntityToDestroy->pSaved || !pEntityToDestroy->pSaved->conOwner.containerID)
	{
		return false;
	}
	cbData = calloc(sizeof(SavedPetCBData), 1);
	cbData->ownerType = pEntityToDestroy->pSaved->conOwner.containerType;
	cbData->ownerID = pEntityToDestroy->pSaved->conOwner.containerID;
	cbData->iPetContainerType = entGetType(pEntityToDestroy);
	cbData->iPetContainerID = entGetContainerID(pEntityToDestroy);
	sprintf_s(SAFESTR(cbData->pchReason), "UnsummonPet");

	// Clear the logging in flags
	entClearCodeFlagBits(pEntityToDestroy, ENTITYFLAG_PET_LOGGING_IN);
	// Set the logging out flags
	entSetCodeFlagBits(pEntityToDestroy, ENTITYFLAG_PET_LOGGING_OUT);

	objRequestContainerMove(objCreateManagedReturnVal(PetContainerMove_CB, cbData),
		cbData->iPetContainerType, cbData->iPetContainerID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
	return true;
}

bool gslSetPrimarySavedPet(Entity *pOwner, GlobalType petType, ContainerID petID)
{
	SavedPetCBData *cbData = calloc(sizeof(SavedPetCBData), 1);

	if (!pOwner)
	{
		free(cbData);
		return false;
	}

	cbData->ownerType = entGetType(pOwner);
	cbData->ownerID = entGetContainerID(pOwner);
	cbData->iPetContainerType = petType;
	cbData->iPetContainerID = petID;

	AutoTrans_trSetPrimarySavedPet(LoggedTransactions_CreateManagedReturnVal("SetPrimaryPet", NULL, NULL), 
		GLOBALTYPE_GAMESERVER, cbData->ownerType, cbData->ownerID, petType, petID);

	return true;

}


AUTO_COMMAND_REMOTE;
void RemoteUnSummonPet(CmdContext *context)
{
	if (context)
	{	
		Entity *pTarget = entFromContainerIDAnyPartition(context->clientType, context->clientID);

		if (pTarget)
		{
			gslUnSummonSavedPet(pTarget);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SummonCritterPetByDef(Entity *pOwner, const char *pchPetDefName)
{
	PetDef *pPetDef = RefSystem_ReferentFromString("PetDef", pchPetDefName);

	if (pPetDef &&
		pOwner && 
		!entIsInCombat(pOwner) &&
		pOwner->pSaved && 
		pOwner->pSaved->ppAllowedCritterPets && 
		eaSize(&pOwner->pSaved->ppAllowedCritterPets) > 0)
	{
		bool bPetAllowed = false;
		PetDefRefCont *pFoundAllowedPet = NULL;

		// Get the next critter ID
		U32 uiPetID = gslEntity_GetNewCritterPetID(pOwner);

		// Iterate through the allowed pet list and make sure this petdef is there
		FOR_EACH_IN_EARRAY_FORWARDS(pOwner->pSaved->ppAllowedCritterPets, PetDefRefCont, pAllowedPet)
		{
			PetDef *pCurPetDef = pAllowedPet ? GET_REF(pAllowedPet->hPet) : NULL;
			if (pCurPetDef && pCurPetDef == pPetDef)
			{
				pFoundAllowedPet = pAllowedPet;
				bPetAllowed = true;
				break;
			}
		}
		FOR_EACH_END
		
		if (!pFoundAllowedPet)
			return;

		if (pFoundAllowedPet->bPetIsDeceased && !SavedPet_HasRequirementsToResummonDeceasedPet(pOwner))
			return;

		// Make sure this critter pet is already not summoned
		FOR_EACH_IN_EARRAY_FORWARDS(pOwner->pSaved->ppCritterPets, CritterPetRelationship, pRelationShip)
		{			
			PetDef *pCurPetDef = pRelationShip ? GET_REF(pRelationShip->hPetDef) : NULL;
			if (pCurPetDef && pCurPetDef == pPetDef)
			{
				if (pRelationShip->pEntity)
				{
					bPetAllowed = false;
				}
				uiPetID = pRelationShip->uiPetID;				
				break;
			}
		}
		FOR_EACH_END

		if (bPetAllowed)
		{
			S32 i = 0, iTeamPetsActive = 0, iTeamMatesActive = 0;
			RegionRules *pRegionRules = NULL;
			WorldRegion *pRegion;
			Vec3 vOwnerPos;
			SavedPetTeamList *pPetTeamList = NULL;
			Team* pPlayerTeam = team_GetTeam(pOwner);

			// Get the region rules
			entGetPos(pOwner,vOwnerPos);
			pRegion = worldGetWorldRegionByPos(vOwnerPos);
			pRegionRules = getRegionRulesFromRegion(pRegion);

			// Is the player in a team
			if (pPlayerTeam)
			{
				int iPartitionIdx = entGetPartitionIdx(pOwner);

				// Get the pet list for the team
				pPetTeamList = gslTeam_FindPetTeamList(iPartitionIdx, GLOBALTYPE_TEAM, pPlayerTeam->iContainerID);

				if(!pPetTeamList)
				{
					iTeamPetsActive = 0;
				}
				else
				{
					iTeamPetsActive = ea32Size(&pPetTeamList->uiPetIDs);
				}

				//Get active players for that team
				iTeamMatesActive = team_NumMembersThisServerAndPartition(pPlayerTeam,iPartitionIdx);

				// Team is full, we can no longer summon pets
				if (iTeamMatesActive + iTeamPetsActive >= TEAM_MAX_SIZE)
				{
					return;
				}
			}			

 			if(!pRegionRules || ea32Find(&pRegionRules->peCharClassTypes, petdef_GetCharacterClassType(pPetDef)) >= 0)
			{
				if (gslTeam_PetSpawnRequest(pOwner, uiPetID, false))
				{
					// Summon the pet
					if (!gslSummonCritterPet(pOwner, NULL, pPetDef))
					{
						gslTeam_RemovePetFromListEx(pOwner, uiPetID);
					}
					else if (pFoundAllowedPet->bPetIsDeceased && g_PetRestrictions.pchRequiredItemForDeceasedPets)
					{
						// the pet is deceased and we require an item to summon it
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
						ItemChangeReason reason = {0};

						inv_FillItemChangeReason(&reason, pOwner, "Pets:SummonCritterPet", NULL);

						pFoundAllowedPet->bPetIsDeceased = false;

						// blindly run the transaction as we're not too concerned if this fails or not since this is a fairly trivial item
						AutoTrans_inventory_RemoveItemByDefName(NULL, GetAppGlobalType(), 
																entGetType(pOwner), entGetContainerID(pOwner), 
																g_PetRestrictions.pchRequiredItemForDeceasedPets, 
																1, 
																&reason, pExtract);
					}
				}
			}			
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void FillPetTeamList(Entity *pOwner)
{
	SavedPetTeamList *pList = NULL;
	Vec3 vOwnerPos;
	RegionRules *pRules = NULL;

	if(!pOwner)
		return;

	entGetPos(pOwner,vOwnerPos);
	pRules = RegionRulesFromVec3(vOwnerPos);

	if(team_IsMember(pOwner))
		pList = gslTeam_FindPetTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID);
	else
		pList = gslTeam_FindPetTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_ENTITYPLAYER,pOwner->myContainerID);

	if(!pList)
		pList = gslTeam_CreateNewTeamList(entGetPartitionIdx(pOwner), team_IsMember(pOwner) ? GLOBALTYPE_TEAM : GLOBALTYPE_ENTITYPLAYER, team_IsMember(pOwner) ? pOwner->pTeam->iTeamID : pOwner->myContainerID);

	if(pList)
		gslTeam_AutoFillPetTeamList(entGetPartitionIdx(pOwner),pList,team_GetTeam(pOwner),pRules);
}

AUTO_COMMAND ACMD_SERVERCMD;
void CreatePet(Entity *pOwner)
{
	Entity *pTarget = entity_GetTarget(pOwner);

	if (pTarget)
	{
		gslCreateSavedPetForOwner(pOwner, pTarget);
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void DestroyPet(Entity *pOwner)
{
	Entity *pTarget = entity_GetTarget(pOwner);

	if (pTarget)
	{
		gslDestroySavedPet(pTarget);
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void DismissPet(Entity *pOwner)
{
	Entity *pTarget = entity_GetTarget(pOwner);

	if (pTarget)
	{
		gslSetSavedPetState(pOwner, entGetType(pTarget), entGetContainerID(pTarget), OWNEDSTATE_OFFLINE, 0, 0, -1);
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void PetStay(Entity *pOwner)
{
	Entity *pTarget = entity_GetTarget(pOwner);

	if (pTarget)
	{
		gslSetSavedPetState(pOwner, entGetType(pTarget), entGetContainerID(pTarget), OWNEDSTATE_STATIC, 0, 0, -1);
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void SummonAllPets(Entity *pOwner)
{
	int i;
	WorldRegion *pRegion;
	RegionRules *pRegionRules;
	Vec3 vOwnerPos;
	int iPartitionIdx;

	if (!pOwner || !pOwner->pSaved)
	{
		return;
	}

	entGetPos(pOwner,vOwnerPos);
	pRegion = worldGetWorldRegionByPos(vOwnerPos);
	pRegionRules = getRegionRulesFromRegion(pRegion);
	iPartitionIdx = entGetPartitionIdx(pOwner);
	
	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		PetRelationship *conRelation = pOwner->pSaved->ppOwnedContainers[i];
		if (conRelation->eRelationship == CONRELATION_PET || conRelation->eRelationship == CONRELATION_PRIMARY_PET)
		{
			Entity *pPet = SavedPet_GetEntity(iPartitionIdx, conRelation);
			CharacterClass *pClass = pPet ? GET_REF(pPet->pChar->hClass) : NULL;

			if(!pRegionRules || (pClass && ea32Find(&pRegionRules->peCharClassTypes, pClass->eType)>=0) && pPet != pOwner)
			{
				gslSetSavedPetState(pOwner, GLOBALTYPE_ENTITYSAVEDPET, 
					conRelation->conID, OWNEDSTATE_ACTIVE, 0, 0, -1);
			}
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SummonPetByID(Entity *pOwner, int ID)
{
	int i, iTeamPetsActive, iTeamMatesActive=0;
	WorldRegion *pRegion;
	RegionRules *pRegionRules;
	Vec3 vOwnerPos;
	SavedPetTeamList *pPetTeamList;
	Team* pPlayerTeam = team_GetTeam(pOwner);
	int iPartitionIdx;

	if (!pOwner || !pOwner->pSaved || entIsInCombat(pOwner))
	{
		return;
	}

	entGetPos(pOwner,vOwnerPos);
	pRegion = worldGetWorldRegionByPos(vOwnerPos);
	pRegionRules = getRegionRulesFromRegion(pRegion);
	iPartitionIdx = entGetPartitionIdx(pOwner);

	if (pPlayerTeam)
	{
		pPetTeamList = gslTeam_FindPetTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pPlayerTeam->iContainerID);

		if(!pPetTeamList)
		{
			iTeamPetsActive = 0;
		}
		else
		{
			iTeamPetsActive = ea32Size(&pPetTeamList->uiPetIDs);
		}

		//Get active players for that team
		iTeamMatesActive = team_NumMembersThisServerAndPartition(pPlayerTeam,iPartitionIdx);

		if (iTeamMatesActive + iTeamPetsActive >= TEAM_MAX_SIZE)
		{
			return;
		}
	}
	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		PetRelationship *conRelation = pOwner->pSaved->ppOwnedContainers[i];
		Entity* pPetEnt = GET_REF(pOwner->pSaved->ppOwnedContainers[i]->hPetRef);
		if (pPetEnt && pPetEnt->myContainerID == (ContainerID)ID && (conRelation->eRelationship == CONRELATION_PET || conRelation->eRelationship == CONRELATION_PRIMARY_PET))
		{
			Entity *pPet = SavedPet_GetEntity(iPartitionIdx, conRelation);
			CharacterClass *pClass = pPet ? GET_REF(pPet->pChar->hClass) : NULL;

 			if(!pRegionRules || (pClass && ea32Find(&pRegionRules->peCharClassTypes, pClass->eType)>=0) && pPet != pOwner)
			{
				if (gslTeam_PetSpawnRequest(pOwner, pPet->myContainerID, false))
				{
					gslSetSavedPetState(pOwner, GLOBALTYPE_ENTITYSAVEDPET, 
						conRelation->conID,
						OWNEDSTATE_ACTIVE, 0, true, -1);

					if(pOwner && pPlayerTeam)
					{
						gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_TEAM,pOwner->pTeam->iTeamID,ID);
					}
					else if(pOwner)
					{
						gslTeam_AddPetToTeamList(entGetPartitionIdx(pOwner),GLOBALTYPE_ENTITYPLAYER,pOwner->myContainerID,ID);
					}
				}
			}
		}
	}
}


void DismissPetEx(SA_PARAM_NN_VALID Entity *pOwner, SA_PARAM_NN_VALID Entity *pPet)
{
	// See if this pet is a critter pet
	if (pPet->myEntityType == GLOBALTYPE_ENTITYSAVEDPET && pOwner && pOwner->pSaved && eaSize(&pOwner->pSaved->ppCritterPets))
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pOwner->pSaved->ppCritterPets, CritterPetRelationship, pRelationShip)
		{
			if (pRelationShip && pRelationShip->erPet == entGetRef(pPet))
			{
				U32 uiPetID = pRelationShip->uiPetID;
				
				if ( (!entIsAlive(pPet) || (pPet->pChar && pPet->pChar->pNearDeath)) && g_PetRestrictions.pchRequiredItemForDeceasedPets )
				{	// if the pet is dead, and we require items to summon deceased pets, set this guy to deceased
					PetDef *pPetDef = GET_REF(pRelationShip->hPetDef);
					if (pPetDef)
					{
						PetDefRefCont *pPetRef = Entity_FindAllowedCritterPetByDef(pOwner, pPetDef);
						if (pPetRef)
						{
							pPetRef->bPetIsDeceased = true;
						}
					}
				}

				// Destroy the critter pet
				Entity_DestroyCritterPet(pOwner, uiPetID);

				// Remove from the pet team
				gslTeam_RemovePetFromListEx(pOwner, uiPetID);

				return;
			}
		}
		FOR_EACH_END

		gslSetSavedPetState(pOwner, GLOBALTYPE_ENTITYSAVEDPET, pPet->myContainerID, OWNEDSTATE_OFFLINE, NULL, 0, -1);
	}

	gslTeam_RemovePetFromListEx(pOwner,pPet->myContainerID);
	
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void DismissPetByID(Entity *pOwner, int iRef)
{
	Entity* pPet = entFromEntityRef(entGetPartitionIdx(pOwner), iRef);
	
	if (!pPet || entIsInCombat(pOwner) || entIsInCombat(pPet)) return;
	
	DismissPetEx(pOwner, pPet);
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(PetDismissSelf);
void exprFuncPetDismissSelf(ACMD_EXPR_SELF Entity *pEnt)
{
	Entity *pEntOwner = entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->erOwner);
	if (!pEntOwner)
	{
		return;
	}

	DismissPetEx(pEntOwner, pEnt);
}


AUTO_COMMAND ACMD_SERVERCMD;
void SummonPrimaryPet(Entity *pOwner)
{
	int i;
	if (!pOwner || !pOwner->pSaved)
	{
		return;
	}

	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		PetRelationship *conRelation = pOwner->pSaved->ppOwnedContainers[i];
		if (conRelation->eRelationship == CONRELATION_PRIMARY_PET)
		{
			gslSetSavedPetState(pOwner, GLOBALTYPE_ENTITYSAVEDPET, 
				conRelation->conID, OWNEDSTATE_AUTO_SUMMON, 0, 0, -1);
		}
	}
}

AUTO_COMMAND ACMD_NAME(BecomePet);
void gslBecomePet(Entity *clientEntity, int iPetID)
{
	PetRelationship *pPet = NULL;
	int i;
	int iPartitionIdx;

	if(!clientEntity || !clientEntity->pSaved->ppOwnedContainers)
		return;

	iPartitionIdx = entGetPartitionIdx(clientEntity);
	for(i=0;i<eaSize(&clientEntity->pSaved->ppOwnedContainers); i++)
	{
		Entity *pPetEnt = SavedPet_GetEntity(iPartitionIdx, clientEntity->pSaved->ppOwnedContainers[i]);

		if(pPetEnt->myContainerID == (ContainerID)iPetID)
			pPet = clientEntity->pSaved->ppOwnedContainers[i];
	}

	if(pPet && (pPet->eState == OWNEDSTATE_OFFLINE || pPet->eState == OWNEDSTATE_AUTO_CONTROL))
	{
		//Summon the pet
		gslSetSavedPetState(clientEntity, GLOBALTYPE_ENTITYSAVEDPET, 
			pPet->conID, OWNEDSTATE_AUTO_CONTROL, 0, 0, -1);
	}

}

AUTO_COMMAND ACMD_NAME(BecomeSelf);
void gslBecomeSelf(Entity *clientEntity)
{
	PetRelationship *pPet = NULL;
	Entity *pPetEntity = entGetMount(clientEntity);
	int i;
	int iPartitionIdx;

	if(!clientEntity || !pPetEntity)
		return;

	gslEntCancelRide(clientEntity);
	iPartitionIdx = entGetPartitionIdx(clientEntity);

	for(i=0;i<eaSize(&clientEntity->pSaved->ppOwnedContainers); i++)
	{
		Entity *pPetEntCheck = SavedPet_GetEntity(iPartitionIdx, clientEntity->pSaved->ppOwnedContainers[i]);

		if(pPetEntity->myContainerID == pPetEntCheck->myContainerID)
		{
			pPet = clientEntity->pSaved->ppOwnedContainers[i];
			break;
		}
	}

	if(pPet && pPet->eState == OWNEDSTATE_AUTO_CONTROL)
	{
		//Send the pet away
		gslSetSavedPetState(clientEntity, GLOBALTYPE_ENTITYSAVEDPET,
			pPet->conID, OWNEDSTATE_OFFLINE, 0, 0, -1);
	}
}

void gslCritterPetCleanup(Entity *pCritterPet)
{
	Entity *pOwner = entFromEntityRef(entGetPartitionIdx(pCritterPet), pCritterPet->erOwner);

	if(pOwner && pOwner->pSaved)
	{
		int i;

		for(i=eaSize(&pOwner->pSaved->ppCritterPets)-1;i>=0;i--)
		{
			if(pOwner->pSaved->ppCritterPets[i]->pEntity == pCritterPet)
			{
				CritterPetRelationship *pPetRelationship = pOwner->pSaved->ppCritterPets[i];
				eaRemove(&pOwner->pSaved->ppCritterPets,i);

				gslTeam_RemovePetFromListEx(pOwner,pOwner->pSaved->ppCritterPets[i]->uiPetID);

				pPetRelationship->pEntity = NULL;
				StructDestroy(parse_CritterPetRelationship,pPetRelationship);
			}
		}
	}
}

void gslHandleCritterPetsAtLogout(Entity *pOwner)
{
	int i;

	if(!pOwner || !pOwner->pSaved)
	{
		return;
	}

	for(i=0;i<eaSize(&pOwner->pSaved->ppCritterPets);i++)
	{
		if(pOwner->pSaved->ppCritterPets[i]->pEntity)
		{
			gslTeam_RemovePetFromListEx(pOwner,pOwner->pSaved->ppCritterPets[i]->uiPetID);
			gslQueueEntityDestroy(pOwner->pSaved->ppCritterPets[i]->pEntity);
			pOwner->pSaved->ppCritterPets[i]->pEntity = NULL;
			pOwner->pSaved->ppCritterPets[i]->erPet = 0;
		}
	}
}

static void gslSavedPet_PropagationCleanup(Entity *pEnt)
{
	int i;
	PropPowerSaveList SaveList = {0};
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	//Save propagated powers
	for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
	{
		PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
		Entity* pPetEnt = SavedPet_GetEntity(iPartitionIdx, pPet);
		if (pPetEnt && ent_PetGetPropPowersToSave(pEnt, pPetEnt, NULL, NULL, &SaveList.eaData))
		{
			TransactionReturnVal* pReturn;
			ANALYSIS_ASSUME(pPetEnt != NULL);
			pReturn = LoggedTransactions_CreateManagedReturnVal("SavedPropRecharges", NULL, NULL);
			AutoTrans_trEntSavePropPowerRecharges(pReturn,
				GetAppGlobalType(),
				entGetType(pPetEnt),
				entGetContainerID(pPetEnt),
				&SaveList);
		}
	}
	StructDeInit(parse_PropPowerSaveList, &SaveList);
}


void gslHandleSavedPetsAtLogout(Entity* pOwner)
{
	int i, iPartitionIdx;

	if (!pOwner || !pOwner->pSaved)
	{
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pOwner);
	for(i=0;i<eaSize(&pOwner->pSaved->ppOwnedContainers);i++)
	{
		PetRelationship* pPet = pOwner->pSaved->ppOwnedContainers[i];
		Entity *pSavedPet = pPet->curEntity;

		// If the pet entity pointer hasn't been set, try to find the pet container by ID
		if (!pSavedPet)
		{
			pSavedPet = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET, pPet->conID);
		}
		if (pSavedPet)
		{
			gslUnSummonSavedPet(pSavedPet);
		}
		savedpet_destroyOfflineCopy(iPartitionIdx, pPet->conID);
	}
	gslSavedPet_PropagationCleanup(pOwner);
}

void gslSavedPetLogout(int iPartitionIdx, Entity *ent)
{
	Entity *pMaster = entFromEntityRef(iPartitionIdx, ent->erOwner);

	if(pMaster && pMaster->pSaved)
	{
		int i;
		int found = false;

		//gslTeam_cmd_KickSavedPetByID(pMaster, ent->myContainerID);
		for(i=0;i<eaSize(&pMaster->pSaved->ppOwnedContainers);i++)
		{
			if(pMaster->pSaved->ppOwnedContainers[i]->curEntity == ent)
			{
				pMaster->pSaved->ppOwnedContainers[i]->curEntity = NULL;
				ea32FindAndRemove(&pMaster->pSaved->ppAwayTeamPetID, entGetContainerID(ent));
				found = true;
			}
		}

		//Remove from the saved pet lists
		gslTeam_RemovePetFromListEx(pMaster,ent->myContainerID);

		entity_SetDirtyBit(pMaster, parse_SavedEntityData, pMaster->pSaved, false);
	}
}

//Update all the characters combat level to be the same as the owners
void gslSavedPet_UpdateCombatLevel(Entity *eOwner)
{
	int i;

	if(!eOwner->pSaved)
		return;

	for(i=0;i<eaSize(&eOwner->pSaved->ppOwnedContainers);i++)
	{
		Entity *pSavedPet = eOwner->pSaved->ppOwnedContainers[i]->curEntity;

		if(pSavedPet && pSavedPet->pChar)
		{
			pSavedPet->pChar->iLevelCombat = eOwner->pChar->iLevelCombat;
			entity_SetDirtyBit(pSavedPet,parse_Character,pSavedPet->pChar,false);
		}
	}

	for(i=0;i<eaSize(&eOwner->pSaved->ppCritterPets);i++)
	{
		Entity *pCritterPet = eOwner->pSaved->ppCritterPets[i]->pEntity;

		if(pCritterPet && pCritterPet->pChar)
		{
			pCritterPet->pChar->iLevelCombat = eOwner->pChar->iLevelCombat;
			entity_SetDirtyBit(pCritterPet,parse_Character,pCritterPet->pChar,false);
		}
	}
}

void gslSavedPetLoggedIn(Entity* e, Entity* entOwner)
{
	SavedMapDescription *spawnDescription = NULL;

	if(entOwner)
	{
		int i;
		int added = false;
		int iPetPartitionIdx = entGetPartitionIdx(e);
		int iPartitionIdx = entGetPartitionIdx(entOwner);

		// This should never happen
		if (iPetPartitionIdx != iPartitionIdx)
		{
			ErrorDetailsf("Pet %s Idx %d, Owner %s Idx %d", 
				ENTDEBUGNAME(e), iPetPartitionIdx, ENTDEBUGNAME(entOwner), iPartitionIdx);
			Errorf("Saved pet tried to log in, but has a mismatched partition from owner");
			return;
		}

		e->erOwner = entGetRef(entOwner);

		//Find the pet def and fill in
 		for(i=0;i<eaSize(&entOwner->pSaved->ppOwnedContainers);i++)
 		{
 			Entity *pPetEnt = SavedPet_GetEntity(iPartitionIdx, entOwner->pSaved->ppOwnedContainers[i]);
 			if(pPetEnt && pPetEnt->myContainerID == e->myContainerID && pPetEnt->myEntityType == e->myEntityType)
 			{
				entOwner->pSaved->ppOwnedContainers[i]->curEntity = e;
				ea32PushUnique(&entOwner->pSaved->ppAwayTeamPetID, entGetContainerID(e));
				added = true;
 			}
 		}

		if (e->pChar){
			e->pChar->iLevelCombat = entOwner->pChar->iLevelCombat;
		}
		entity_SetDirtyBit(entOwner, parse_SavedEntityData, entOwner->pSaved, false);
	}

	if (e->pCritter && e->pChar)
	{
		aiInit(e,GET_REF(e->pCritter->critterDef),NULL);
		critter_AddCombat(e, GET_REF(e->pCritter->critterDef), e->pChar->iLevelCombat, 0, 0, randomPositiveF32(), false,false, NULL, false);
		aiInitTeam(e,NULL);

		if(entOwner && entGetType(entOwner)==GLOBALTYPE_ENTITYPLAYER)
		{
			mmCollisionGroupHandleCreateFG(e->mm.movement, &e->mm.mcgHandle, __FILE__, __LINE__, MCG_PLAYER_PET);
			mmCollisionBitsHandleCreateFG(e->mm.movement, &e->mm.mcbHandle, __FILE__, __LINE__, ~0);
		}
	}
	else
	{
		aiInit(e, NULL, NULL);
		aiInitTeam(e, NULL);
	}
	spawnDescription = entity_GetLastMap(e);

	if( ( spawnDescription == NULL ) && entOwner)
	{
		Vec3 vPos;
		Quat qRot;
		//Take the spawn location of the current player
		entGetPos(entOwner,vPos);
		entGetRot(entOwner,qRot);
		
		entSetPos(e,vPos,true, __FUNCTION__);
		entSetRot(e,qRot, true, __FUNCTION__);
	}
	
	if (spawnDescription)
	{
		Entity *pMount;
		Quat spawnRot;

		PYRToQuat(spawnDescription->spawnPYR, spawnRot);

		if(vec3IsZero(spawnDescription->spawnPos) && quatIsZero(spawnRot) && (stricmp("Emptymap", zmapInfoGetPublicName(NULL)) != 0))
			Errorf("Pet entering map with (0,0,0) debugPos; is this an error?");

		if (pMount = entGetMount(e))
		{
			entSetPos(pMount, spawnDescription->spawnPos, true, __FUNCTION__);
			entSetRot(pMount, spawnRot, true, __FUNCTION__);
		}
		entSetPos(e, spawnDescription->spawnPos, true, __FUNCTION__);
		entSetRot(e, spawnRot, true, __FUNCTION__);
	}

	if (e->pChar){
		character_LoadNonTransact(entGetPartitionIdx(e), e, false);

		if(e->pChar->pattrBasic->fHitPoints <= 0)
		{
			e->pChar->pattrBasic->fHitPoints = 1;
			if(entIsAlive(e))
				entClearCodeFlagBits(e,ENTITYFLAG_DEAD);
		}
	}

	if (entOwner)
	{
		//HACK: Door Transitions - saved pets need to be handled this way as they aren't immediately spawned at login
		if (entCheckFlag(entOwner,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS))
		{
			entSetCodeFlagBits(e,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS|ENTITYFLAG_DONOTDRAW);
		}
		GameSpecific_HolsterRequest(e, entOwner, false);
	}

	entClearCodeFlagBits(e,ENTITYFLAG_PET_LOGGING_IN);
	entClearCodeFlagBits(e,ENTITYFLAG_IGNORE);
	mmDisabledHandleDestroy(&e->mm.mdhIgnored);
	gslEntitySetInvisibleTransient(e, 0);
}

AUTO_COMMAND ACMD_NAME(AddNewPet);
void gslAddNewPet(Entity *clientEntity, ACMD_NAMELIST("PetDef", REFDICTIONARY) char *pchPetName, int iLevel)
{
	PetDef *pNewPet = RefSystem_ReferentFromString(g_hPetStoreDict,pchPetName);

	if(pNewPet)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(clientEntity);

		gslCreateSavedPetFromDef(entGetPartitionIdx(clientEntity),clientEntity,NULL,NULL,pNewPet,iLevel,NULL,NULL,0,OWNEDSTATE_OFFLINE,0,0, pExtract);
	}
}

void Entity_ValidateSavedPetsOwnership(Entity *pOwner)
{
	int i;

	for(i=0;i<eaSize(&pOwner->pSaved->ppOwnedContainers);i++)
	{
		PetRelationship *pPet = pOwner->pSaved->ppOwnedContainers[i];
		Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET,pPet->conID);

		if(!pEntity)
			pEntity = GET_REF(pPet->hPetRef);

		if(pEntity && pEntity->pSaved)
		{
			if(!pEntity->pSaved->bFixingOwner &&
				(pEntity->pSaved->conOwner.containerID != pOwner->myContainerID
				 || pEntity->pSaved->conOwner.containerType != pOwner->myEntityType))
			{
				// This flag is used to avoid spamming this transaction
				pEntity->pSaved->bFixingOwner = true;

				AutoTrans_trSavedPet_UpdateOwner(LoggedTransactions_CreateManagedReturnVal("FixupOwnership_SavedPet",NULL,NULL),GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYSAVEDPET,
					pPet->conID,pOwner->myEntityType,pOwner->myContainerID);
			}
		}
	}
}

static void AlwaysPropSlotDef_AddToRefList(AlwaysPropSlotDef* pDef, U32 uiPuppetID, AlwaysPropSlotDefRef*** peaList, S32* piCount)
{
	S32 i;
	for (i = (*piCount)-1; i >= 0; i--)
	{
		AlwaysPropSlotDefRef *pDefRef = (*peaList)[i];
		if (pDef == GET_REF(pDefRef->hDef)
			&& uiPuppetID == pDefRef->uiPuppetID)
		{
			pDefRef->iCount++;
			break;
		}
	}
	if (i < 0)
	{
		AlwaysPropSlotDefRef* pRef = eaGetStruct(peaList, parse_AlwaysPropSlotDefRef, (*piCount)++);
		SET_HANDLE_FROM_REFERENT("AlwaysPropSlotDef",pDef,pRef->hDef);
		pRef->iCount = 1;
		pRef->uiPuppetID = uiPuppetID;
	}
}

static void Entity_ValidateAlwaysPropSlots(Entity *pEntity)
{
	static AlwaysPropSlotDefRef** s_ppFixupSlotRefs = NULL;
	int i, j, iListSize = 0, iSlotCount = 0;
	bool bFixup = false;
	int iPartitionIdx;

	if(!pEntity || !pEntity->pSaved)
		return;

	iPartitionIdx = entGetPartitionIdx(pEntity);

	for(i=0;i<eaSize(&pEntity->pSaved->ppOwnedContainers);i++)
	{
		Entity *pPetEntity = SavedPet_GetEntity(iPartitionIdx, pEntity->pSaved->ppOwnedContainers[i]);
		PetDef *pPetDef = pPetEntity && pPetEntity->pCritter ? GET_REF(pPetEntity->pCritter->petDef) : NULL;

		if(pPetEntity == pEntity)
		{
			pPetEntity = GET_REF(pEntity->pSaved->ppOwnedContainers[i]->hPetRef);
			pPetDef = pPetEntity && pPetEntity->pCritter ? GET_REF(pPetEntity->pCritter->petDef) : NULL;
		}

		if(!pPetEntity)
		{
			pEntity->pSaved->bCheckPets = true;
			return;
		}

		if(!pPetDef)
			continue;

		if(SavedPet_IsPetAPuppet(pEntity,pEntity->pSaved->ppOwnedContainers[i]))
		{ 
			int n;
			for(j=eaSize(&pEntity->pSaved->pPuppetMaster->ppPuppets)-1;j>=0;j--)
			{
				if(pEntity->pSaved->pPuppetMaster->ppPuppets[j]->curID == pPetEntity->myContainerID)
					break;
			}

			if(j<0 || pEntity->pSaved->pPuppetMaster->ppPuppets[j]->eState != PUPPETSTATE_ACTIVE)
				continue;

			//Check to see if there is a type that matches in the temp puppets
			for(n=eaSize(&pEntity->pSaved->pPuppetMaster->ppTempPuppets)-1;n>=0;n--)
			{
				PetDef *pTempPetDef = GET_REF(pEntity->pSaved->pPuppetMaster->ppTempPuppets[n]->hPetDef);
				CharClassTypes eClassType = petdef_GetCharacterClassType(pTempPetDef);

				if(eClassType == (CharClassTypes)pEntity->pSaved->pPuppetMaster->ppPuppets[j]->eType)
					break; 
			}

			if(n>=0)
				continue;
		}

		for(j=0;j<eaSize(&pPetDef->ppAlwaysPropSlot);j++)
		{
			AlwaysPropSlotDef* pSlotDef = GET_REF(pPetDef->ppAlwaysPropSlot[j]->hPropDef);
			if (pSlotDef)
			{
				AlwaysPropSlotDef_AddToRefList(pSlotDef, pPetEntity->myContainerID, &s_ppFixupSlotRefs, &iListSize);
				iSlotCount++;
			}
		}
	}

	if(pEntity->pSaved->pPuppetMaster)
	{
		for(i=0;i<eaSize(&pEntity->pSaved->pPuppetMaster->ppTempPuppets);i++)
		{
			PetDef *pPetDef = GET_REF(pEntity->pSaved->pPuppetMaster->ppTempPuppets[i]->hPetDef);

			for(j=0;j<eaSize(&pPetDef->ppAlwaysPropSlot);j++)
			{
				AlwaysPropSlotDef* pSlotDef = GET_REF(pPetDef->ppAlwaysPropSlot[j]->hPropDef);
				if (pSlotDef)
				{
					AlwaysPropSlotDef_AddToRefList(pSlotDef, pEntity->pSaved->pPuppetMaster->ppTempPuppets[i]->uiID, &s_ppFixupSlotRefs, &iListSize);
					iSlotCount++;
				}
			}
		}
	}

	if (!bFixup)
	{
		if (iSlotCount != eaSize(&pEntity->pSaved->ppAlwaysPropSlots))
		{
			bFixup = true;
		}
		else
		{
			for (i = eaSize(&s_ppFixupSlotRefs)-1; i >= 0; i--)
			{
				S32 iCount = 0;
				for (j = eaSize(&pEntity->pSaved->ppAlwaysPropSlots)-1; j >= 0; j--)
				{
					AlwaysPropSlotDef* pSlotDef = GET_REF(pEntity->pSaved->ppAlwaysPropSlots[j]->hDef);
					if (pSlotDef == GET_REF(s_ppFixupSlotRefs[i]->hDef))
					{
						iCount++;
					}
				}
				if (iCount != s_ppFixupSlotRefs[i]->iCount)
				{
					bFixup = true;
					break;
				}
			}
		}
	}

	if (!bFixup)
	{
		for (i = eaSize(&pEntity->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
		{
			AlwaysPropSlot* pSlot = pEntity->pSaved->ppAlwaysPropSlots[i];
			if (!pSlot->iSlotID)
			{
				bFixup = true;
				break;
			}
			else
			{
				for (j = i-1; j >= 0; j--)
				{
					AlwaysPropSlot* pCheckSlot = pEntity->pSaved->ppAlwaysPropSlots[j];
					if (pCheckSlot->iSlotID == pSlot->iSlotID)
						break;
				}
				if (j >= 0)
				{
					bFixup = true;
					break;
				}
			}
		}
	}

	if (bFixup)
	{
		AlwaysPropSlotDefRefs Refs = {0};

		eaSetSizeStruct(&s_ppFixupSlotRefs, parse_AlwaysPropSlotDefRef, iListSize);
		Refs.eaRefs = s_ppFixupSlotRefs;
		
		AutoTrans_trEntity_FixupAlwaysProps(LoggedTransactions_CreateManagedReturnVal("FixupPropSlots", NULL,NULL),
			GLOBALTYPE_GAMESERVER,pEntity->myEntityType,pEntity->myContainerID,&Refs);

	}
}

//-------------------------Master/Puppet Entity stuff----------------------//

AUTO_TRANS_HELPER
	ATR_LOCKS(pSrc, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2")
	ATR_LOCKS(pDest, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax");
bool Entity_PuppetCopyEx(ATH_ARG NOCONST(Entity) *pSrc, ATH_ARG NOCONST(Entity) *pDest, bool bFixupItemIDs)
{
	if (NONNULL(pDest->pChar))
		StructCopyNoConst(parse_Character,pSrc->pChar,pDest->pChar, 0, TOK_PERSIST, TOK_PUPPET_NO_COPY);
	if (NONNULL(pDest->pSaved))
		StructCopyNoConst(parse_SavedEntityData,pSrc->pSaved,pDest->pSaved, 0, TOK_PERSIST, TOK_PUPPET_NO_COPY);
	StructCopyNoConst(parse_CostumeRef,&pSrc->costumeRef,&pDest->costumeRef, 0, TOK_PERSIST, TOK_PUPPET_NO_COPY);
	Entity_PuppetCopy_Inventory(pSrc,pDest);

	if (bFixupItemIDs)
	{
		// Fix the item IDs on the destination entity
		inv_ent_FixItemIDs(pDest);
	}
	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pSrc, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2")
	ATR_LOCKS(pDest, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax");
bool Entity_PuppetCopy(ATH_ARG NOCONST(Entity) *pSrc, ATH_ARG NOCONST(Entity) *pDest)
{
	return Entity_PuppetCopyEx(pSrc, pDest, false);
}

static void gslTransformToPuppet_HandleSaveMods(SA_PARAM_NN_VALID Entity *pEnt,
												GlobalType eOldPuppetType,
												ContainerID uOldPuppetID)
{
	int i;
	if (!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster || !pEnt->pChar)
	{
		return;
	}
	for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
	{
		PuppetEntity* pPuppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[i];

		if (pPuppetEntity->curType == eOldPuppetType && pPuppetEntity->curID == uOldPuppetID)
		{
			if (pEnt->pChar->bLoaded)
			{
				// Build the saved mods array
				character_SaveAttribMods(pEnt->pChar);
			}
			// Copy the master entity's saved mods onto the PuppetEntity
			if (eaSize(&pEnt->pChar->modArray.ppModsSaved))
			{
				eaCopyStructs(&pEnt->pChar->modArray.ppModsSaved, &pPuppetEntity->ppModsSaved, parse_AttribMod);
			}
			else
			{
				eaClearStruct(&pPuppetEntity->ppModsSaved, parse_AttribMod);
			}
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
			break;
		}
	}
}

void gslTransformToPuppet_HandleLoadMods(SA_PARAM_NN_VALID Entity *pEnt)
{
	int i;
	if (!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster || !pEnt->pChar)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pEnt->pSaved->pPuppetMaster->uSavedModsVersion = pEnt->pSaved->pPuppetMaster->uPuppetSwapVersion;

	devassert(!eaSize(&pEnt->pChar->modArray.ppModsSaved) || !eaSize(&pEnt->pChar->modArray.ppMods));

	for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
	{
		PuppetEntity* pPuppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[i];

		if (pPuppetEntity->curType == pEnt->pSaved->pPuppetMaster->curType &&
			pPuppetEntity->curID == pEnt->pSaved->pPuppetMaster->curID)
		{
			// If this is the new puppet, copy the PuppetEntity's saved mods onto the master entity
			if (eaSize(&pPuppetEntity->ppModsSaved))
			{
				eaCopyStructs(&pPuppetEntity->ppModsSaved, &pEnt->pChar->modArray.ppModsSaved, parse_AttribMod);
			}
			else
			{
				eaClearStruct(&pEnt->pChar->modArray.ppModsSaved, parse_AttribMod);
			}
			break;
		}
	}
	if (i < 0)
	{
		// If the new puppet wasn't found in the array, then clear the master entity's saved mods
		eaClearStruct(&pEnt->pChar->modArray.ppModsSaved, parse_AttribMod);
	}
	entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);

	PERFINFO_AUTO_STOP();
}

static void gslTransformToPuppet_HandleSaveActiveSlots(SA_PARAM_NN_VALID Entity *pEnt)
{
	PuppetEntity *pPuppet = NULL;
	SavedActiveSlots **ppSavedActiveSlots = NULL;
	int i, j;

	if (!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster || !pEnt->pInventoryV2)
		return;

	for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
	{
		PuppetEntity* pPuppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[i];

		if (pPuppetEntity->curType == pEnt->pSaved->pPuppetMaster->curType &&
			pPuppetEntity->curID == pEnt->pSaved->pPuppetMaster->curID)
		{
			pPuppet = pPuppetEntity;
			break;
		}
	}

	if (!pPuppet)
		return;

	eaCopy(&ppSavedActiveSlots, &pPuppet->ppActiveSlotsSaved);
	eaClear(&pPuppet->ppActiveSlotsSaved);

	for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
	{
		InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[i];
		SavedActiveSlots *pActiveSlots = NULL;

		if (eaiSize(&pBag->eaiActiveSlots) <= 0)
			continue;

		for (j = eaSize(&ppSavedActiveSlots) - 1; j >= 0; j--)
		{
			if (ppSavedActiveSlots[j]->eBagID == (U32)pBag->BagID)
			{
				pActiveSlots = eaRemove(&ppSavedActiveSlots, j);
				break;
			}
		}

		if (!pActiveSlots)
			pActiveSlots = StructCreate(parse_SavedActiveSlots);

		pActiveSlots->eBagID = pBag->BagID;
		eaiCopy(&pActiveSlots->eaiActiveSlots, &pBag->eaiActiveSlots);
		eaPush(&pPuppet->ppActiveSlotsSaved, pActiveSlots);
	}

	eaDestroyStruct(&ppSavedActiveSlots, parse_SavedActiveSlots);
}

void gclTransformToPuppet_HandleLoadActiveSlots(SA_PARAM_NN_VALID Entity *pEnt)
{
	PuppetEntity *pPuppet = NULL;
	int i, j;
	bool bChanged = false;

	if (!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster || !pEnt->pInventoryV2)
		return;

	for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
	{
		PuppetEntity* pPuppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[i];

		if (pPuppetEntity->curType == pEnt->pSaved->pPuppetMaster->curType &&
			pPuppetEntity->curID == pEnt->pSaved->pPuppetMaster->curID)
		{
			pPuppet = pPuppetEntity;
			break;
		}
	}

	if (!pPuppet)
		return;

	for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
	{
		InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[i];
		SavedActiveSlots *pActiveSlots = NULL;

		for (j = eaSize(&pPuppet->ppActiveSlotsSaved) - 1; j >= 0; j--)
		{
			if (pPuppet->ppActiveSlotsSaved[j]->eBagID == (U32)pBag->BagID)
			{
				pActiveSlots = pPuppet->ppActiveSlotsSaved[j];
				break;
			}
		}

		if (pActiveSlots)
		{
			eaiCopy(&pBag->eaiActiveSlots, &pActiveSlots->eaiActiveSlots);
		}
		else if (eaiSize(&pBag->eaiActiveSlots) > 0)
		{
			eaiClear(&pBag->eaiActiveSlots);
		}
	}

	if (bChanged)
	{
		entity_SetDirtyBit(pEnt, parse_Inventory, pEnt->pInventoryV2, false);
	}
}

#define LOGGING_IN_FLAGS (ENTITYFLAG_PLAYER_LOGGING_IN|ENTITYFLAG_PLAYER_DISCONNECTED)

static void TransformtoPuppet_TransformComplete(SA_PARAM_NN_VALID Entity *pEnt)
{
	int i;
	bool bSwapOccurredOnMap = false;
	bool bSkippedSuccessOnLogin;
	GameAccountDataExtract *pExtract;
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	//we should not get here with a NULL pChar or pPuppetMaster
	assert(pEnt->pChar && pEnt->pSaved && pEnt->pSaved->pPuppetMaster);

	entClearCodeFlagBits(pEnt,ENTITYFLAG_PUPPETPROGRESS);
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	iPartitionIdx = entGetPartitionIdx(pEnt);

	if(entCheckFlag(pEnt,LOGGING_IN_FLAGS) != LOGGING_IN_FLAGS) 
	{
		// If puppet swap occurs during middle of map, need to clear IGNORE and disable states
		entClearCodeFlagBits(pEnt,ENTITYFLAG_IGNORE);
		mmDisabledHandleDestroy(&pEnt->mm.mdhIgnored);
		character_Cleanup(iPartitionIdx,pEnt->pChar,false,pExtract,false);
		pEnt->pChar->pEntParent = pEnt;
		bSwapOccurredOnMap = true;
	}

	//Failsafe to prevent the character from being loaded twice
	//This shouldn't be necessary, but it's still happening occasionally
	if (pEnt->pChar->bLoaded)
	{
		character_Cleanup(iPartitionIdx,pEnt->pChar,false,pExtract,false);
		pEnt->pChar->pEntParent = pEnt;
		Errorf("Puppet Transform Error: Character has already been loaded.");
	}

	// Initialize the player's health
	if (!pEnt->pChar->pattrBasic)
	{
		pEnt->pChar->pattrBasic = StructCreate(parse_CharacterAttribs);
	}
	pEnt->pChar->pattrBasic->fHitPoints = 1;

	//Load saved AttribMods from the active puppet onto the master entity
	gslTransformToPuppet_HandleLoadMods(pEnt);

	//Load saved active slots from the active puppet onto the master entity
	gclTransformToPuppet_HandleLoadActiveSlots(pEnt);

	bSkippedSuccessOnLogin = SAFE_MEMBER3(pEnt,pSaved,pPuppetMaster,bSkippedSuccessOnLogin);
	if(bSkippedSuccessOnLogin && entCheckFlag(pEnt,LOGGING_IN_FLAGS))
	{
		entClearCodeFlagBits(pEnt,LOGGING_IN_FLAGS);

		HandlePlayerLogin_Success(pEnt,kLoginSuccess_PuppetSwap);
	}
	else
	{
		character_LoadNonTransact(entGetPartitionIdx(pEnt), pEnt, false);
	}

	// All puppet swaps reset the player's health to max
	pEnt->pChar->pattrBasic->fHitPoints = pEnt->pChar->pattrBasic->fHitPointsMax;

	gslCacheEntRegion(pEnt,pExtract);
	
	if (!bSwapOccurredOnMap)
	{
		character_DirtyInnateEquip(pEnt->pChar);
		character_DirtyInnatePowers(pEnt->pChar);
		character_DirtyPowerStats(pEnt->pChar);
		character_DirtyInnateAccrual(pEnt->pChar);
	}
	costumeEntity_ResetStoredCostume(pEnt);
	costumeEntity_RegenerateCostume(pEnt);

	if (pEnt->pSaved->pPuppetMaster->curType == GLOBALTYPE_ENTITYSAVEDPET || (pEnt->pPlayer && pEnt->pPlayer->pUI))
	{
		for ( i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i-- )
		{
			if (	pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curType == pEnt->pSaved->pPuppetMaster->curType
				&&	pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID == pEnt->pSaved->pPuppetMaster->curID )
			{
				break;
			}
		}

		if ( i >= 0 )
		{
			if (pEnt->pSaved->pPuppetMaster->curType == GLOBALTYPE_ENTITYSAVEDPET)
			{
				Entity *pPuppetEnt = GET_REF(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->hEntityRef);
				if (pPuppetEnt)
				{
					bool b = false;
					if (pEnt->pSaved->pPuppetMaster->curPuppetName)
					{
						StructFreeString(pEnt->pSaved->pPuppetMaster->curPuppetName);
						entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
						b = true;
					}
					if (pPuppetEnt->pSaved)
					{
						pEnt->pSaved->pPuppetMaster->curPuppetName = StructAllocString(pPuppetEnt->pSaved->savedName);
						if (!b) entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
					}
				}
				else
				{
					pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed = false;
				}
			}
		}
	}

	if (pEnt->pSaved->pPuppetMaster->curTempID)
	{
		for(i = eaSize(&pEnt->pSaved->pPuppetMaster->ppTempPuppets)-1; i >= 0; --i)
		{
			if ((ContainerID)pEnt->pSaved->pPuppetMaster->ppTempPuppets[i]->uiID == pEnt->pSaved->pPuppetMaster->curTempID)
			{
				break;
			}
		}
		if (i >= 0)
		{
			PetDef* pPetDef = GET_REF(pEnt->pSaved->pPuppetMaster->ppTempPuppets[i]->hPetDef);
			const char* pchDisplayName = pPetDef ? entTranslateDisplayMessage(pEnt, pPetDef->displayNameMsg) : NULL;
			if (pEnt->pSaved->pPuppetMaster->curPuppetName)
			{
				StructFreeString(pEnt->pSaved->pPuppetMaster->curPuppetName);
			}
			pEnt->pSaved->pPuppetMaster->curPuppetName = StructAllocString(pchDisplayName);
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
		}
	}

	pEnt->pSaved->pPuppetMaster->expectedID = 0;
	pEnt->pSaved->pPuppetMaster->expectedType = GLOBALTYPE_NONE;
	gslEntityForceFullSend(pEnt);

	PERFINFO_AUTO_STOP();
}

static void TransformToPuppet_PostTransaction(TransactionReturnVal *returnVal, PuppetTransformData *pData)
{
	int i;
	Entity *pEnt = entFromEntityRefAnyPartition(pData->entRefMaster);
	PuppetMaster *pPuppetMaster = pEnt && pEnt->pSaved ? pEnt->pSaved->pPuppetMaster : NULL;

	PERFINFO_AUTO_START_FUNC();

	if (bPetTransactionDebug) {
		printf("%s: Post transform transaction (%s)\n", pEnt ? pEnt->debugName : "NULL", pEnt ? REF_STRING_FROM_HANDLE(pEnt->pChar->hClass) : "NULL");
	}

	if(!pPuppetMaster)
	{
		if (bPetTransactionDebug) {
			printf("%s: Post transform transaction reporting no puppet master\n", pEnt ? pEnt->debugName : "NULL");
		}
		free(pData);
		if(pEnt)
		{
			entClearCodeFlagBits(pEnt,ENTITYFLAG_PUPPETPROGRESS);
		}
		PERFINFO_AUTO_STOP();
		return;
	}

	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (bPetTransactionDebug) {
			printf("%s: Post transform transaction reporting success\n", pEnt ? pEnt->debugName : "NULL");
		}
		devassertmsg(pData->NewPuppetID == 0 || 
			(pData->NewPuppetType == GLOBALTYPE_ENTITYSAVEDPET && pData->NewPuppetID == pPuppetMaster->curID),
			"Puppet ID's do not match after Puppet Transform, something has gone wrong! Please get Michael McCarry!");
		
		if(entIsAlive(pEnt))
			entClearCodeFlagBits(pEnt,ENTITYFLAG_DEAD);

		if (pData->NewPuppetType == GLOBALTYPE_ENTITYCRITTER ||
			pData->OldPuppetType == GLOBALTYPE_ENTITYCRITTER)
		{
			Entity_ValidateAlwaysPropSlots(pEnt);
		}

		// Handle saving out AttribMods
		gslTransformToPuppet_HandleSaveMods(pEnt, pData->OldPuppetType, pData->OldPuppetID);

		if ((pData->NewPuppetType == GLOBALTYPE_ENTITYSAVEDPET && pData->NewPuppetID == pPuppetMaster->curID) || 
			(pData->NewPuppetType == GLOBALTYPE_ENTITYCRITTER && pPuppetMaster->curTempID))
		{
			TransformtoPuppet_TransformComplete(pEnt);
		}
		else
		{
			pEnt->pSaved->pPuppetMaster->expectedID = pData->NewPuppetID;
			pEnt->pSaved->pPuppetMaster->expectedType = pData->NewPuppetType;
		}
	}
	else
	{
		for (i = 0; i < returnVal->iNumBaseTransactions; i++)
		{			
			if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
			{
				if (bPetTransactionDebug) {
					printf("%s: Post transform transaction reporting failure\n", pEnt ? pEnt->debugName : "NULL");
				}
				if (gbEnablePetAndPuppetLogging) 
				{
					entLog(LOG_CONTAINER, pEnt, "TransformToPuppet", 
						"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
				}
				if(pEnt)
				{
					entClearCodeFlagBits(pEnt,ENTITYFLAG_PUPPETPROGRESS);
				}
				free(pData);
				
				if (pEnt && pEnt->pPlayer && pEnt->pPlayer->clientLink)
				{
					gslSendForceLogout(pEnt->pPlayer->clientLink,"Unable to transform character, forced logout");
				}
				PERFINFO_AUTO_STOP();
				return;
			}
		}
	}

	free(pData);

	if (bPetTransactionDebug) {
		printf("%s: Post transform transaction clearing PUPPETPROGRESS\n", pEnt ? pEnt->debugName : "NULL");
	}
	
	PERFINFO_AUTO_STOP();
}

void gslEntTransformToTempPuppet(Entity *pEnt, PetDef *pPetDef)
{
	GameAccountDataExtract *pExtract;
	PuppetTransformData *pData;
	CharacterPreSaveInfo preSaveInfo = {0};
	int i;

	if(entCheckFlag(pEnt,ENTITYFLAG_PUPPETPROGRESS))
	{
		if (bPetTransactionDebug) {
			printf("%s: Waiting for puppet transform to complete\n", pEnt->debugName);
		}
		return;
	}

	if (!pPetDef)
		return;

	// Have to get whether or not to DisableFaceActivate from the PetDef.  If it doesn't exist, it's set to 0.
	if (GET_REF(pPetDef->hCritterDef))
	{
		pEnt->pChar->bDisableFaceActivate = GET_REF(pPetDef->hCritterDef)->bDisableFaceActivate;
	}
	else
	{
		pEnt->pChar->bDisableFaceActivate = 0;
	}

	if(pEnt->pSaved->pPuppetMaster->curTempID)
	{
		for(i=0;i<eaSize(&pEnt->pSaved->pPuppetMaster->ppTempPuppets);i++)
		{
			PetDef *pTempPetDef = GET_REF(pEnt->pSaved->pPuppetMaster->ppTempPuppets[i]->hPetDef);

			if (pTempPetDef == pPetDef && 
				(ContainerID)pEnt->pSaved->pPuppetMaster->ppTempPuppets[i]->uiID == pEnt->pSaved->pPuppetMaster->curTempID)
			{
				const char* pchDisplayName = entTranslateDisplayMessage(pEnt, pPetDef->displayNameMsg);
				if (pEnt->pSaved->pPuppetMaster->curPuppetName)
				{
					StructFreeString(pEnt->pSaved->pPuppetMaster->curPuppetName);
				}
				pEnt->pSaved->pPuppetMaster->curPuppetName = StructAllocString(pchDisplayName);
				entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);

				if (bPetTransactionDebug) {
					printf("%s: Skipping temp puppet transform  transaction: Already desired puppet", pEnt->debugName);
				}
				return;
			}
		}
	}

	pData = calloc(sizeof(PuppetTransformData), 1);

	pData->ownerID = pEnt->myContainerID;
	pData->ownerType = pEnt->myEntityType;

	if (pEnt->pSaved->pPuppetMaster->curID)
	{
		pData->OldPuppetID = pEnt->pSaved->pPuppetMaster->curID;
		pData->OldPuppetType = pEnt->pSaved->pPuppetMaster->curType;
	}
	else if (pEnt->pSaved->pPuppetMaster->curTempID)
	{
		pData->OldPuppetID = pEnt->pSaved->pPuppetMaster->curTempID;
		pData->OldPuppetType = GLOBALTYPE_ENTITYCRITTER;
	}

	pData->NewPuppetID = 0;
	pData->NewPuppetType = GLOBALTYPE_ENTITYCRITTER;

	pData->entRefMaster = pEnt->myRef;

	gslTransformToPuppet_HandleSaveActiveSlots(pEnt);

	entSetCodeFlagBits(pEnt,ENTITYFLAG_PUPPETPROGRESS);
	//gslSendEntityToDatabase(pEnt,false); // This send is slow and may corrupt data

	if(entCheckFlag(pEnt,ENTITYFLAG_PLAYER_LOGGING_IN) && entCheckFlag(pEnt,ENTITYFLAG_PLAYER_DISCONNECTED))
	{
		// Puppet swap during login stores some temp info
		preSaveInfo.pTempAttributes = StructCreate(parse_TempAttributes);
		eaCopyStructs(&pEnt->pChar->ppSavedAttributes,(SavedAttribute***)&preSaveInfo.pTempAttributes->ppAttributes,parse_SavedAttribute);
	}
	else
	{
		// Puppet swap while in middle of map does IGNORE and disable
		character_FillInPreSaveInfo(pEnt->pChar,&preSaveInfo);
		entSetCodeFlagBits(pEnt, ENTITYFLAG_IGNORE);
		mmDisabledHandleCreate(&pEnt->mm.mdhIgnored, pEnt->mm.movement, __FILE__, __LINE__);
	}

	if (bPetTransactionDebug) {
		printf("%s: Invoking puppet transform transaction %d %d (%d %d -> %d %d)\n", pEnt->debugName, pEnt->myEntityType, pEnt->myContainerID, pData->OldPuppetType, pData->OldPuppetID, GLOBALTYPE_ENTITYCRITTER, 0);
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	AutoTrans_trEntity_TransformToTempPuppet(LoggedTransactions_CreateManagedReturnVal("TransformToTempPuppet",TransformToPuppet_PostTransaction,pData), GLOBALTYPE_GAMESERVER, 
		pEnt->myEntityType, pEnt->myContainerID,
		pData->OldPuppetType, pData->OldPuppetID, 
		pPetDef->pchPetName, &preSaveInfo, pExtract);

	CharacterPreSaveInfo_Destroy(&preSaveInfo);
}

static void gslEntTransformToPuppet(Entity *pEnt, PuppetEntity* pPuppet)
{
	GameAccountDataExtract *pExtract;
	PuppetTransformData *pData; 
	int i;
	CharacterPreSaveInfo preSaveInfo = {0};
	
	if(entCheckFlag(pEnt,ENTITYFLAG_PUPPETPROGRESS))
	{
		if (bPetTransactionDebug) {
			printf("%s: Waiting for puppet transform to complete\n", pEnt->debugName);
		}
		return;
	}

	entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);

	if(pPuppet
		&& (pEnt->pSaved->pPuppetMaster->curID == pPuppet->curID
		&& pEnt->pSaved->pPuppetMaster->curType == pPuppet->curType))
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		Entity *pPuppetEntity = SavedPuppet_GetEntity(iPartitionIdx, pPuppet);
		if (pPuppetEntity)
		{
			bool b = false;
			if (pEnt->pSaved->pPuppetMaster->curPuppetName)
			{
				StructFreeString(pEnt->pSaved->pPuppetMaster->curPuppetName);
				entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
				b = true;
			}
			if (pPuppetEntity->pSaved)
			{
				pEnt->pSaved->pPuppetMaster->curPuppetName = StructAllocString(pPuppetEntity->pSaved->savedName);
				if (!b) entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
			}
		}
		if (bPetTransactionDebug) {
			printf("%s: Skipping puppet transform transaction!?!\n", pEnt->debugName);
		}
		return;
	}

	pData = calloc(sizeof(PuppetTransformData), 1);

	pData->ownerID = pEnt->myContainerID;
	pData->ownerType = pEnt->myEntityType;

	if (pEnt->pSaved->pPuppetMaster->curID)
	{
		pData->OldPuppetID = pEnt->pSaved->pPuppetMaster->curID;
		pData->OldPuppetType = pEnt->pSaved->pPuppetMaster->curType;
	}
	else if (pEnt->pSaved->pPuppetMaster->curTempID)
	{
		pData->OldPuppetID = pEnt->pSaved->pPuppetMaster->curTempID;
		pData->OldPuppetType = GLOBALTYPE_ENTITYCRITTER;
	}

	pData->NewPuppetID = pPuppet ? pPuppet->curID : pEnt->myContainerID;
	pData->NewPuppetType = pPuppet ? pPuppet->curType : pEnt->myEntityType;

	pData->entRefMaster = pEnt->myRef;

	for(i=0;i<eaSize(&pEnt->pSaved->ppOwnedContainers);i++)
	{
		if(pEnt->pSaved->ppOwnedContainers[i]->conID == pPuppet->curID)
		{
			//Make sure this entity is offline
			if(pEnt->pSaved->ppOwnedContainers[i]->eState != OWNEDSTATE_OFFLINE)
			{
				gslSetSavedPetState(pEnt,pPuppet->curType,pPuppet->curID,OWNEDSTATE_OFFLINE,0,0,-1);
			}
		}
	}

	gslTransformToPuppet_HandleSaveActiveSlots(pEnt);

	entSetCodeFlagBits(pEnt,ENTITYFLAG_PUPPETPROGRESS);
	
	//Let the login code know we're waiting on a puppet swap
	if(pEnt->pPlayer)
		pEnt->pPlayer->eLoginWaiting |= kLoginSuccess_PuppetSwap;

	//gslSendEntityToDatabase(pEnt,false); // This send is slow and may corrupt data

	if (bPetTransactionDebug) {
		printf("%s: Invoking puppet transform transaction %d %d (%d %d -> %d %d)\n", pEnt->debugName, pEnt->myEntityType, pEnt->myContainerID, pData->OldPuppetType, pData->OldPuppetID, pData->NewPuppetType, pData->NewPuppetID);
	}

	if(entCheckFlag(pEnt,ENTITYFLAG_PLAYER_LOGGING_IN) && entCheckFlag(pEnt,ENTITYFLAG_PLAYER_DISCONNECTED))
	{
		// Puppet swap during login stores some temp info
		preSaveInfo.pTempAttributes = StructCreate(parse_TempAttributes);
		eaCopyStructs(&pEnt->pChar->ppSavedAttributes,(SavedAttribute***)&preSaveInfo.pTempAttributes->ppAttributes,parse_SavedAttribute);
	}
	else
	{
		// Puppet swap while in middle of map does IGNORE and disable
		character_FillInPreSaveInfo(pEnt->pChar,&preSaveInfo);
		entSetCodeFlagBits(pEnt, ENTITYFLAG_IGNORE);
		mmDisabledHandleCreate(&pEnt->mm.mdhIgnored, pEnt->mm.movement, __FILE__, __LINE__);
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	AutoTrans_trEntity_TransformToPuppet(LoggedTransactions_CreateManagedReturnVal("TransformToPuppet",TransformToPuppet_PostTransaction,pData), GLOBALTYPE_GAMESERVER, 
		pEnt->myEntityType, pEnt->myContainerID,
		pData->OldPuppetType == GLOBALTYPE_ENTITYCRITTER ? 0 : pData->OldPuppetType,pData->OldPuppetType == GLOBALTYPE_ENTITYCRITTER ? 0 : pData->OldPuppetID,
		pData->NewPuppetType, pData->NewPuppetID,
		&preSaveInfo, pExtract);

	CharacterPreSaveInfo_Destroy(&preSaveInfo);
}

AUTO_COMMAND ACMD_NAME(AddPetToPuppetShelf);
void gslAddPetToPuppetShelf (Entity *clientEntity,GlobalType PetContainerType, int iPetContainterID, const char *pchType, const char *pchName)
{
	SavedEntityData *pSaved = clientEntity ? clientEntity->pSaved : NULL;

	if(pSaved && pSaved->ppOwnedContainers)
	{
		AutoTrans_trEntity_AddPuppet(LoggedTransactions_CreateManagedReturnVal("AddPetToPuppetSelf",NULL,NULL),
			GLOBALTYPE_GAMESERVER, clientEntity->myEntityType, clientEntity->myContainerID, PetContainerType, iPetContainterID, pchType, pchName, 0);
	}
}

void CreatePuppet_PostPetCreate(TransactionReturnVal *returnVal, SavedPetCBData *pPetData)
{
	PuppetTransformData *pData = pPetData->pUserData;
	Entity *pMasterEnt = entFromEntityRefAnyPartition(pData->entRefMaster);
	Entity *pNewPuppet = entFromContainerIDAnyPartition(pPetData->iPetContainerType,pPetData->iPetContainerID);
	pData->NewPuppetType = pPetData->iPetContainerType;
	pData->NewPuppetID = pPetData->iPetContainerID;

	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		SavedPetCBData *cbData = calloc(sizeof(SavedPetCBData),1);

		cbData->iPetContainerID = pData->NewPuppetID;
		cbData->iPetContainerType = pData->NewPuppetType;
		sprintf_s(SAFESTR(cbData->pchReason), "CreatePuppet");

		//gslAddPetToPuppetShelf(pClientEntity,pData->NewPuppetType,pData->NewPuppetID,pPetData->pPetDef ? pPetData->pPetDef->pchPetName : "",pPetData->chSavedName);

		objRequestContainerMove(objCreateManagedReturnVal(PetContainerMove_CB, cbData),
			pData->NewPuppetType, pData->NewPuppetID, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);


		free(pData);
		free(pPetData);
		return;
	}
	else if(returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		int i;
		for (i = 0; i < returnVal->iNumBaseTransactions; i++)
		{			
			if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
			{
				if (pMasterEnt && gbEnablePetAndPuppetLogging)
				{
					entLog(LOG_CONTAINER, pMasterEnt, "CreatePuppet_PostPetCreate", 
						"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
				}
				free(pData);
				free(pPetData);
				return;
			}
		}
	}
	free(pData);
	free(pPetData);
}

void CreatePuppetMaster_PostPetCreate(TransactionReturnVal *returnVal, SavedPetCBData *pPetData)
{
	PuppetTransformData *pData = pPetData->pUserData;
	Entity *pClientEntity = entFromEntityRefAnyPartition(pData->entRefMaster);
	Entity *pNewPuppet = entFromContainerIDAnyPartition(pPetData->iPetContainerType,pPetData->iPetContainerID);
	pData->NewPuppetType = pPetData->iPetContainerType;
	pData->NewPuppetID = pPetData->iPetContainerID;	

	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		PuppetTransformData *pNewData = NULL;
		PuppetEntity *pPuppetChoice = NULL;

		if(!pClientEntity || !pNewPuppet)
		{
			free(pData);
			free(pPetData);
			return;
		}

		pNewData = calloc(sizeof(PuppetTransformData), 1);

		pNewData->ownerID = pClientEntity->myContainerID;
		pNewData->ownerType = pClientEntity->myEntityType;

		if (pClientEntity->pSaved->pPuppetMaster->curID)
		{
			pNewData->OldPuppetID = pClientEntity->pSaved->pPuppetMaster->curID;
			pNewData->OldPuppetType = pClientEntity->pSaved->pPuppetMaster->curType;
		}
		else if (pClientEntity->pSaved->pPuppetMaster->curTempID)
		{
			pNewData->OldPuppetID = pClientEntity->pSaved->pPuppetMaster->curTempID;
			pNewData->OldPuppetType = GLOBALTYPE_ENTITYCRITTER;
		}

		pNewData->NewPuppetID = pData->NewPuppetID;
		pNewData->NewPuppetType = pData->NewPuppetType;

		pNewData->entRefMaster = pClientEntity->myRef;

		gslTransformToPuppet_HandleSaveActiveSlots(pClientEntity);

		CreatePuppet_PostPetCreate(returnVal, pPetData);

		AutoTrans_trEntity_SetCurrentPuppetID(LoggedTransactions_CreateManagedReturnVal("AddPetToPuppetSelf",TransformToPuppet_PostTransaction,pNewData),
			GLOBALTYPE_GAMESERVER,entGetType(pClientEntity),entGetContainerID(pClientEntity),entGetType(pNewPuppet),entGetContainerID(pNewPuppet));
	}
	else if(returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		int i;
		for (i = 0; i < returnVal->iNumBaseTransactions; i++)
		{			
			if (returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
			{
				if (gbEnablePetAndPuppetLogging)
				{
					entLog(LOG_CONTAINER, pClientEntity, "CreatePuppet_PostPetCreate", 
						"Failed because: %s", returnVal->pBaseReturnVals[i].returnString);
				}
				free(pData);
				free(pPetData);
				return;
			}
		}
	}
}

void MakePuppetMaster_CB(TransactionReturnVal *returnVal, PuppetTransformData *pData)
{
	Entity *pMaster = entFromEntityRefAnyPartition(pData->entRefMaster);
	
	if(!pMaster)
	{
		free(pData);
		return;
	}

	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pMaster);
		GlobalType newType = GLOBALTYPE_ENTITYSAVEDPET;
		ContainerID newID = atoi(returnVal->pBaseReturnVals[0].returnString);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pMaster, "Pets:CreatePuppetMaster", NULL);

		AutoTrans_trEntity_MakePuppetMaster(LoggedTransactions_CreateManagedReturnVal("MakePuppetMaster",NULL,NULL), GLOBALTYPE_GAMESERVER, 
				pMaster->myEntityType, pMaster->myContainerID, 
				newType, newID, &reason, pExtract);

	}

	free(pData);
}

bool gslCreateMasterPuppetEnt(Entity *ent)
{
	NOCONST(Entity) *pTempEntity;
	PuppetTransformData *cbData;

	if (ent->pSaved->pPuppetMaster->bPuppetMasterCreateInProgress)
		return true;

	PERFINFO_AUTO_START_FUNC();

	cbData = calloc(sizeof(PuppetTransformData),1);
	ent->pSaved->pPuppetMaster->bPuppetMasterCreateInProgress = true;

	cbData->entRefMaster = entGetRef(ent);

	pTempEntity = StructCreateWithComment(parse_Entity, "Temp tentity on puppet master create");

	if(!pTempEntity->pInventoryV2)
		pTempEntity->pInventoryV2 = StructCreateNoConst(parse_Inventory);

	if(!pTempEntity->pChar)
		pTempEntity->pChar = StructCreateNoConst(parse_Character);

	if(!pTempEntity->pSaved)
		pTempEntity->pSaved = StructCreateNoConst(parse_SavedEntityData);
	
	Entity_PuppetCopy(CONTAINER_NOCONST(Entity, ent), pTempEntity);

	if(!pTempEntity)
	{
		free(cbData);
		PERFINFO_AUTO_STOP();
		return false;
	}
	pTempEntity->myEntityType = GLOBALTYPE_ENTITYSAVEDPET;
	pTempEntity->myContainerID = 0;

	pTempEntity->pSaved->uFixupVersion = CURRENT_ENTITY_FIXUP_VERSION;
	pTempEntity->pSaved->uGameSpecificFixupVersion = gameSpecificFixup_Version();

	pTempEntity->pSaved->conOwner.containerType = ent->myEntityType;
	pTempEntity->pSaved->conOwner.containerID = ent->myContainerID;

	sprintf(pTempEntity->pSaved->savedName, "%s", ent->pSaved->savedName);

	objRequestContainerCreate(objCreateManagedReturnVal(MakePuppetMaster_CB, cbData),
		GLOBALTYPE_ENTITYSAVEDPET,pTempEntity,GLOBALTYPE_OBJECTDB, 0);

	StructDestroyNoConst(parse_Entity, pTempEntity);

	PERFINFO_AUTO_STOP();
	return true;
}


//Makes the entity a master entity
//This should happen once for any entity that wishes to have this feature enabled
//TODO(MM): consider making this happen at character creation on a per project basis?
bool gslMakePuppetMaster(Entity *pEnt)
{
	gslCreateMasterPuppetEnt(pEnt);
	return true;
}

AUTO_COMMAND ACMD_NAME(MakeMeAPuppetMaster);
void gslMakeMeAPuppetMaster(Entity *clientEntity)
{
	gslMakePuppetMaster(clientEntity);
}

void gslCreateNewPuppetFromDef(int iPartitionIdx, Entity *pMasterEntity, Entity* pEntSrc, PetDef *pPetDef, int iLevel, U64 iItemID, GameAccountDataExtract *pExtract)
{
	if(pPetDef)
	{
		PuppetTransformData *pData = calloc(sizeof(PuppetTransformData),1);

		pData->entRefMaster = entGetRef(pMasterEntity);

		if (!gslCreateSavedPetFromDefEx(iPartitionIdx, pMasterEntity,pEntSrc,NULL,pPetDef,NULL,NULL,NULL,iLevel,CreatePuppet_PostPetCreate,pData,iItemID,OWNEDSTATE_OFFLINE,0,0,true,pExtract))
		{
			free(pData);
		}
	}
}

AUTO_COMMAND ACMD_NAME(AddNewPuppet);
void gslAddNewPuppet(Entity *clientEntity, ACMD_NAMELIST("PetDef", REFDICTIONARY) char *pchPetName, int iLevel)
{
	PetDef *pNewPet = RefSystem_ReferentFromString(g_hPetStoreDict,pchPetName);

	if(pNewPet)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(clientEntity);
		gslCreateNewPuppetFromDef(entGetPartitionIdx(clientEntity), clientEntity, NULL, pNewPet, 0, 0, pExtract);
	}
}



AUTO_COMMAND ACMD_NAME(ControlPuppet);
void gslControlPuppet(Entity *clientEntity, int iPuppetContainerID)
{
	SavedEntityData *pSaved = clientEntity->pSaved;

	if(pSaved && pSaved->pPuppetMaster)
	{
		int i;
		for(i=0;i<eaSize(&pSaved->pPuppetMaster->ppPuppets);i++)
		{
			PuppetEntity* pPuppet = pSaved->pPuppetMaster->ppPuppets[i];

			if(pPuppet->curID != (ContainerID)iPuppetContainerID)
				continue;
			
			gslEntTransformToPuppet(clientEntity,pSaved->pPuppetMaster->ppPuppets[i]);
		}
	}
}

AUTO_COMMAND ACMD_NAME(ControlTempPuppet);
void gslControlTempPuppet(Entity *clientEntity,ACMD_NAMELIST("PetDef", REFDICTIONARY) char *pchPetName)
{
	PetDef *pPetDef = RefSystem_ReferentFromString(g_hPetStoreDict,pchPetName);

	if(pPetDef)
	{
		gslEntTransformToTempPuppet(clientEntity,pPetDef);
	}
}

AUTO_COMMAND ACMD_NAME(ControlSelf);
void gslControlSelf(Entity *clientEntity)
{
	SavedEntityData *pSaved = clientEntity->pSaved;

	if (pSaved && 
		(pSaved->pPuppetMaster->curID != clientEntity->myContainerID || 
		 pSaved->pPuppetMaster->curType != clientEntity->myEntityType))
	{
		gslEntTransformToPuppet(clientEntity,NULL);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslSetPreferredCategorySet(Entity* pEnt, ACMD_NAMELIST("CharClassCategorySet", REFDICTIONARY) char* pchCategorySet)
{
	TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("SetPreferredCategorySet", pEnt, NULL, NULL);
	GlobalType eEntType = entGetType(pEnt);
	ContainerID uEntID = entGetContainerID(pEnt);
	EntityRef uEntRef = entGetRef(pEnt);
	AutoTrans_trEntity_SetPreferredCategorySet(pReturn, GLOBALTYPE_GAMESERVER, eEntType, uEntID, pchCategorySet);
}

// Don't keep saved AttribMods around for offline puppets
static void gslEntity_PuppetsDestroyOfflineSavedMods(SA_PARAM_NN_VALID Entity* pEnt)
{
	bool bDirty = false;
	int i;

	if (pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
	{
		for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			PuppetEntity* pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
			if (pPuppet->eState == PUPPETSTATE_OFFLINE)
			{
				eaDestroyStruct(&pPuppet->ppModsSaved, parse_AttribMod);
				bDirty = true;
			}
		}
	}
	if (bDirty)
	{
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
	}
}

static void gslSetActivePuppet_CB(TransactionReturnVal* pReturn, void* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition((EntityRef)(intptr_t)pData);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		gslEntity_PuppetsDestroyOfflineSavedMods(pEnt);
		if (pEnt->pSaved->pPuppetMaster)
		{
			pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed = false;
		}
	}
}

static bool gslFindOldActivePuppet(Entity *pEnt, PuppetEntity *pNewActivePuppet, PuppetEntity** ppPuppetOut) 
{
	int i;
	CharClassCategorySet *pNewPuppetCategorySet = CharClassCategorySet_getCategorySetFromPuppetEntity(entGetPartitionIdx(pEnt), pNewActivePuppet);
	for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
	{
		PuppetEntity* pOldPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
		Entity *pOldEntity = GET_REF(pOldPuppet->hEntityRef);
		CharacterClass *pOldClass = SAFE_GET_REF2(pOldEntity, pChar, hClass);

		// The new puppet must be the same type as the old one
		if (pOldPuppet->eType != pNewActivePuppet->eType)
			continue;

		// The puppet we're replacing must be active
		if (pOldPuppet->eState != PUPPETSTATE_ACTIVE)
			continue;

		if (pNewPuppetCategorySet)
		{
			// If there the new puppet belongs to a category set, 
			// the old puppet must have a category
			if (!SAFE_MEMBER(pOldClass, eCategory))
				continue;

			// And that category must be in the same set
			if (ea32Find(&pNewPuppetCategorySet->eaCategories, pOldClass->eCategory) < 0)
				continue;
		}

		// Found a match!
		if (ppPuppetOut)
			(*ppPuppetOut) = pOldPuppet;
	
		return true;
	}

	return false;
}

// Runs a transaction just to set the specified puppet state as active. Doesn't actually change puppets. 
static void gslSetActivePuppetInternal(Entity* pEnt, PuppetEntity* pNewActivePuppet)
{
	TransactionReturnVal* pReturn;
	GlobalType eEntType = entGetType(pEnt);
	ContainerID uEntID = entGetContainerID(pEnt);
	EntityRef uEntRef = entGetRef(pEnt);
	PuppetEntity* pOldActivePuppet = NULL;
	bool bSetLastActiveID = false;
	U32 uOldPuppetID = 0;

	if (!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster || !pNewActivePuppet)
		return;

	if (!Entity_CanSetAsActivePuppet(pEnt, pNewActivePuppet))
	{
		return;
	}

	if (gslFindOldActivePuppet(pEnt, pNewActivePuppet, &pOldActivePuppet))
	{
		S32* peExcludeCategories = g_PetRestrictions.peExcludeLastActivePuppetClassCategories;
		Entity* pPupEnt = GET_REF(pOldActivePuppet->hEntityRef);
		CharacterClass* pClass = pPupEnt && pPupEnt->pChar ? GET_REF(pPupEnt->pChar->hClass) : NULL;

		if (pClass && eaiFind(&peExcludeCategories, pClass->eCategory) < 0)
		{
			bSetLastActiveID = true;
		}

		uOldPuppetID = pOldActivePuppet->curID;
	}

	pReturn = LoggedTransactions_CreateManagedReturnVal("SetActivePuppet",gslSetActivePuppet_CB,(void *)(intptr_t)uEntRef);
	AutoTrans_trEntity_SetPuppetStateActive(pReturn,GLOBALTYPE_GAMESERVER,eEntType,uEntID,GLOBALTYPE_ENTITYSAVEDPET,uOldPuppetID,pNewActivePuppet->curID,SAFE_MEMBER(pOldActivePuppet, curID),bSetLastActiveID);
}

AUTO_COMMAND ACMD_NAME(ForceActivePuppet) ACMD_SERVERCMD;
void gslForceActivePuppet(Entity *pEnt, int iPuppetID)
{
	PuppetEntity* pPuppet = SavedPet_GetPuppetFromContainerID(pEnt, iPuppetID);
	if (pPuppet && pPuppet->eState != PUPPETSTATE_ACTIVE)
	{
		gslSetActivePuppetInternal(pEnt, pPuppet);
	}
}

AUTO_COMMAND ACMD_NAME(SetActivePuppet, SetActiveStarship) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gslSetActivePuppet(Entity *pEnt, int iPuppetID)
{
	PuppetEntity* pPuppet = SavedPet_GetPuppetFromContainerID(pEnt, iPuppetID);
	if (pPuppet && pPuppet->eState != PUPPETSTATE_ACTIVE)
	{
		Entity* pPupEnt = GET_REF(pPuppet->hEntityRef);

		if (Entity_CanModifyPuppet(pEnt, pPupEnt))
		{
			gslSetActivePuppetInternal(pEnt, pPuppet);
		}
	}
}

AUTO_COMMAND ACMD_NAME(SwitchToLastActivePuppet) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslSwitchToLastActivePuppet(Entity *pEnt)
{
	if (pEnt->pSaved && pEnt->pSaved->pPuppetMaster && pEnt->pSaved->pPuppetMaster->lastActiveID)
	{
		S32 i;
		for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			PuppetEntity* pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
			
			if (pPuppet->curID == pEnt->pSaved->pPuppetMaster->lastActiveID)
			{
				gslSetActivePuppet(pEnt, pPuppet->curID);
				break;
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(SetActivePuppetByName) ACMD_SERVERCMD;
void gslSetActivePuppetByName(Entity *clientEntity, const char* pchPupName)
{
	int i;

	if (!clientEntity->pSaved || !clientEntity->pSaved->pPuppetMaster)
		return;

	for(i=0;i<eaSize(&clientEntity->pSaved->pPuppetMaster->ppPuppets);i++)
	{
		PuppetEntity* pPuppet = clientEntity->pSaved->pPuppetMaster->ppPuppets[i];
		Entity* pPupEnt = GET_REF(pPuppet->hEntityRef);

		if (pPupEnt && 
			pPupEnt->pSaved && 
			stricmp(pPupEnt->pSaved->savedName,pchPupName)==0 && 
			pPuppet->eState != PUPPETSTATE_ACTIVE)
		{
			gslSetActivePuppetInternal(clientEntity, pPuppet);
			break;
		}
	}
}

static void gslDestroyPuppet(Entity *pEnt, int iPuppetID)
{
	PuppetEntity* pPuppet = SavedPet_GetPuppetFromContainerID(pEnt, iPuppetID);

	if(pPuppet && canDestroyPuppet(pEnt,pPuppet))
	{
		gslDestroySavedPet(GET_REF(pPuppet->hEntityRef));
	}
}

AUTO_COMMAND ACMD_NAME(Puppet_Destroy) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gslCmdRemovePuppet(Entity *pClientEntity, int iPuppetID)
{
	gslDestroyPuppet(pClientEntity,iPuppetID);
}


AUTO_COMMAND ACMD_NAME(Puppet_DestroyAllOfType) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_HIDE;
void gslCmdRemoveAllPuppetsOfType(Entity *pClientEntity, const char* pchType)
{
	CharClassTypes eType = StaticDefineIntGetInt(CharClassTypesEnum, pchType);
	if (pClientEntity->pSaved && pClientEntity->pSaved->pPuppetMaster && eType != CharClassTypes_None)
	{
		int i;
		for (i = eaSize(&pClientEntity->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			PuppetEntity* pPuppet = pClientEntity->pSaved->pPuppetMaster->ppPuppets[i];
			if ((CharClassTypes)pPuppet->eType == eType && pPuppet->eState == PUPPETSTATE_OFFLINE)
			{
				gslDestroyPuppet(pClientEntity, pPuppet->curID);
			}
		}
	}
}

static void gslSetPetActiveState(Entity *pOwner, ContainerID iPetID, 
								 U32 **peaNewPropEntIDs, bool bNewTeamRequest, 
								 U32 **peaOldPropEntIDs, bool bOldTeamRequest, 
								 AlwaysPropSlotCategory ePropCategory)
{
	gslSetSavedPetStateEx(pOwner,GLOBALTYPE_ENTITYSAVEDPET,iPetID,OWNEDSTATE_ACTIVE,peaNewPropEntIDs,bNewTeamRequest,peaOldPropEntIDs,bOldTeamRequest,-1,ePropCategory);
}

void Entity_Login_ApplyRegionRules(Entity *pEnt)
{
	Vec3 vPlayerPos;
	WorldRegion *pRegion;
	RegionRules *pRegionRules;

	PERFINFO_AUTO_START_FUNC();

	entGetPos(pEnt,vPlayerPos);

	pRegion = worldGetWorldRegionByPos(vPlayerPos);

	pRegionRules = getRegionRulesFromRegion(pRegion);

	if(pRegionRules)
	{
		ClientCmd_STOSpaceshipMovement(pEnt, pRegionRules->bSpaceFlight);

		entity_FixupControlSchemes(pEnt);
		
		// Load the appropriate scheme for the current region
		entity_SetValidControlSchemeForRegion(pEnt, pRegionRules);
	}

	PERFINFO_AUTO_STOP();
}

bool Entity_PuppetRegionValidate(Entity* pEnt)
{
	Vec3 vPlayerPos;
	WorldRegion *pRegion;
	RegionRules *pRegionRules;

	PERFINFO_AUTO_START_FUNC();

	if (!isDevelopmentMode())
	{
		PERFINFO_AUTO_STOP();
		return true;
	}
	entGetPos(pEnt,vPlayerPos);
	pRegion = worldGetWorldRegionByPos(vPlayerPos);
	pRegionRules = getRegionRulesFromRegion(pRegion);

	if (pEnt->pSaved->pPuppetMaster && pRegion && !pRegionRules)
	{
		ErrorFilenamef(zmapGetFilename(worldGetActiveMap()), 
			"Puppet swap failed. Region %s is missing region rules", 
			worldRegionGetRegionName(pRegion));
		PERFINFO_AUTO_STOP();
		return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}

void entity_PuppetSubscribe(Entity *pEnt)
{
	if(pEnt && pEnt->pSaved && pEnt->pSaved->pPuppetMaster && pEnt->pSaved->pPuppetMaster->ppPuppets)
	{
		int i;

		PERFINFO_AUTO_START_FUNC();

		for(i=eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i>=0; i--)
		{
			// Check for unset OR mismatch because the array is not indexed
			if(!IS_HANDLE_ACTIVE(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->hEntityRef)
				|| StringToContainerID(REF_STRING_FROM_HANDLE(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->hEntityRef))!=pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID)
			{
				char idBuf[128];
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID, idBuf), pEnt->pSaved->pPuppetMaster->ppPuppets[i]->hEntityRef);
			}
		}

		PERFINFO_AUTO_STOP_FUNC();
	}
}

static void gslSavedPet_InvalidPuppet_CreateTicket(Entity* pEnt, const char* pchRegionName)
{
	if (pEnt && pEnt->pSaved && pEnt->pPlayer)
	{
		char* estrBuffer = NULL;
		TicketData* pTicketData = StructCreate(parse_TicketData);
		pTicketData->pProductName = strdup(GetProductName());
		pTicketData->eVisibility = TICKETVISIBLE_HIDDEN;
		pTicketData->pPlatformName = strdup(PLATFORM_NAME);
		pTicketData->pMainCategory = strdup("CBug.CategoryMain.GM");
		pTicketData->pCategory = strdup("CBug.Category.GM.Character");
		pTicketData->pSummary = strdup("This player has an invalid puppet");
		pTicketData->pUserDescription = strdupf("This is an automatically generated ticket for a player that has an invalid puppet in region %s", pchRegionName);
		pTicketData->iProductionMode = isProductionMode();
		pTicketData->iMergeID = 0;
		pTicketData->eLanguage = entGetLanguage(pEnt);
		pTicketData->uIsInternal = true;

		pTicketData->pAccountName = strdup(pEnt->pPlayer->privateAccountName);
		pTicketData->pDisplayName = StructAllocString(pEnt->pPlayer->publicAccountName);
		pTicketData->pCharacterName = strdup(pEnt->pSaved->savedName);
		pTicketData->uIsInternal = (pEnt->pPlayer->accessLevel >= ACCESS_GM);

		//Send CSR ticket
		ticketTrackerSendTicket(pTicketData);
		estrDestroy(&estrBuffer);
	}
}


void Entity_PuppetCheck(Entity *pEnt)
{
	Vec3 vPlayerPos;
	WorldRegion *pRegion;
	RegionRules *pRegionRules;
	
	PERFINFO_AUTO_START_FUNC();

	entity_PuppetSubscribe(pEnt);

	entGetPos(pEnt,vPlayerPos);

	pRegion = worldGetWorldRegionByPos(vPlayerPos);

	pRegionRules = getRegionRulesFromRegion(pRegion);
	
	if(entCheckFlag(pEnt,ENTITYFLAG_PUPPETPROGRESS))
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	if (SAFE_MEMBER3(pEnt, pSaved, pPuppetMaster, bPuppetTransformLogoutCheck))
	{
		if (pEnt->pSaved->pPuppetMaster->bSkippedSuccessOnLogin)
		{
			bool bLogoutPlayer = true;
			AllegianceDef* pAllegianceDef = GET_REF(pEnt->hAllegiance);
			if (pAllegianceDef)
			{
				int i;
				for (i = eaSize(&pAllegianceDef->eaDefaultMaps)-1; i >= 0; i--)
				{
					AllegianceDefaultMap* pDefaultMap = pAllegianceDef->eaDefaultMaps[i];
					ZoneMapInfo* pZoneMapInfo = zmapInfoGetByPublicName(pDefaultMap->pchMapName);
					if (pZoneMapInfo)
					{
						RegionRules* pDefaultRegionRules = getRegionRulesFromZoneMap(pZoneMapInfo);
						if (pDefaultRegionRules)
						{
							PuppetEntity *pPuppetChoice = NULL;
							if (entity_ChoosePuppet(pEnt,pDefaultRegionRules,&pPuppetChoice) && pPuppetChoice)
							{
								spawnpoint_MovePlayerToMapAndSpawn(pEnt,pDefaultMap->pchMapName,pDefaultMap->pchSpawn,NULL,0,0,0,0,NULL,NULL,NULL,0,false);
								bLogoutPlayer = false;
								break;
							}
						}
					}
				}
			}
			if (bLogoutPlayer)
			{
				gslLogOutEntityNormal(pEnt);
			}
		}
		PERFINFO_AUTO_STOP();
		return;
	}
	
	if(pRegionRules && pEnt->pSaved->pPuppetMaster)
	{
		TempPuppetChoice *pChoice = NULL;

		if (eaSize(&pRegionRules->ppTempPuppets)!=0 && 
			pEnt->pSaved->pPuppetMaster->curType != GLOBALTYPE_ENTITYPLAYER)
		{
			pChoice = entity_ChooseTempPuppet(pEnt,pRegionRules->ppTempPuppets);
		}

		if(pChoice)
		{
			gslEntTransformToTempPuppet(pEnt,GET_REF(pChoice->hPetDef));
			pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed = true;
		} else {
			PuppetEntity *pPuppetChoice = NULL;
			pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed = entity_ChoosePuppet(pEnt,pRegionRules,&pPuppetChoice);

			if(pPuppetChoice)
			{
				gslEntTransformToPuppet(pEnt, pPuppetChoice);
			}

			//Error if no valid puppets were found
			if(!pPuppetChoice && pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed && eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppetRequests) == 0)
			{
				const char* pchRegionName = StaticDefineIntRevLookup(WorldRegionTypeEnum,worldRegionGetType(pRegion));
				
				Errorf("Unable to find any valid character for this region: %s", pchRegionName);
				gslSavedPet_InvalidPuppet_CreateTicket(pEnt, pchRegionName);
				pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed = false;
				pEnt->pSaved->pPuppetMaster->bPuppetTransformLogoutCheck = true;
				pEnt->pSaved->pPuppetMaster->bPuppetTransformFailed = true;
			}
			else if(!pPuppetChoice)
			{
				pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed = false;
			}
		}
	}

	if (pRegionRules 
		&& (!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster || pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed))
	{
		if (bPetTransactionDebug) {
			printf("%s: Applying login region rules and propagation\n", pEnt->debugName);
		}
		Entity_Login_ApplyRegionRules(pEnt);
		Entity_ValidateAlwaysPropSlots(pEnt);
	}

	PERFINFO_AUTO_STOP();
}

void entity_PetSubscribe(Entity *pEnt)
{
	if(pEnt && pEnt->pSaved && pEnt->pSaved->ppOwnedContainers)
	{
		int i;

		PERFINFO_AUTO_START_FUNC();

		for(i=eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i>=0; i--)
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];

			// Check for unset OR mismatch because the array is not indexed
			if(!IS_HANDLE_ACTIVE(pPet->hPetRef)
				|| StringToContainerID(REF_STRING_FROM_HANDLE(pPet->hPetRef))!=pPet->conID)
			{
				char idBuf[128];
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pPet->conID, idBuf), pPet->hPetRef);
			}
			// Update the curEntity pointer
			pPet->curEntity = entFromContainerID(entGetPartitionIdx(pEnt), GLOBALTYPE_ENTITYSAVEDPET, pPet->conID);
		}

		PERFINFO_AUTO_STOP_FUNC();
	}
}

bool gslHandleSavedPetsAtLoginHelper(Entity* pOwner, PetRelationship* conRelation);

void Entity_PetCheck(Entity *pEnt)
{
	Vec3 vPlayerPos;
	WorldRegion *pRegion;
	RegionRules *pRegionRules;
	int i;

	PERFINFO_AUTO_START_FUNC();

	entity_PetSubscribe(pEnt);

	entGetPos(pEnt,vPlayerPos);

	pRegion = worldGetWorldRegionByPos(vPlayerPos);

	pRegionRules = getRegionRulesFromRegion(pRegion);

	if(pRegionRules)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);

		for(i=0;i<eaSize(&pEnt->pSaved->ppOwnedContainers);i++)
		{
			Entity *pPetCheck = SavedPet_GetEntity(iPartitionIdx, pEnt->pSaved->ppOwnedContainers[i]);

			if (pEnt->pSaved->ppOwnedContainers[i]->bDeferLoginHandling)
			{
				if (!gslHandleSavedPetsAtLoginHelper(pEnt, pEnt->pSaved->ppOwnedContainers[i]))
					pEnt->pSaved->bCheckPets = true;
			}

			//Puppets cannot be spawned for now
			if(SavedPet_IsPetAPuppet(pEnt,pEnt->pSaved->ppOwnedContainers[i]))
			{
				if(pPetCheck && pEnt->pSaved->ppOwnedContainers[i]->eState != OWNEDSTATE_OFFLINE)
				{
					//gslSetPetActiveState(pEnt,pPetCheck->myContainerID,false);
					gslSetSavedPetState(pEnt,GLOBALTYPE_ENTITYSAVEDPET,pEnt->pSaved->ppOwnedContainers[i]->conID,OWNEDSTATE_OFFLINE,0,0,-1);
				}
				continue;
			}
		}
	}

	if(team_IsMember(pEnt))
	{
		Team* pTeam = team_GetTeam(pEnt);
		if(!pTeam)
			pEnt->pSaved->bCheckPets = true;
		else
			gslTeam_CheckPetCount(entGetPartitionIdx(pEnt), pTeam,NULL);
	}

	if(pEnt->pSaved->bCheckPets == false)
	{
		Entity_ValidateSavedPetsOwnership(pEnt);
		Entity_ValidateAlwaysPropSlots(pEnt);
	}

	PERFINFO_AUTO_STOP();
}

void gslHandlePuppetCreateRequests(Entity* pOwner, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();

	if(pOwner->pSaved->pPuppetMaster && 
		!pOwner->pSaved->pPuppetMaster->bPuppetMasterCreateInProgress &&
		(pOwner->pSaved->pPuppetMaster->curType == GLOBALTYPE_ENTITYPLAYER))
	{
		gslCreateMasterPuppetEnt(pOwner);
	}

	if(pOwner->pSaved->pPuppetMaster && eaSize(&pOwner->pSaved->pPuppetMaster->ppPuppetRequests) > 0)
	{
		int i;

		for(i=0;i<eaSize(&pOwner->pSaved->pPuppetMaster->ppPuppetRequests);i++)
		{
			PuppetRequest* pRequest = pOwner->pSaved->pPuppetMaster->ppPuppetRequests[i];
			PetDef *pPetDef = RefSystem_ReferentFromString("PetDef",pRequest->pchType);
			const char* pchPupName = pRequest->pchName;
			PuppetTransformData *pData;

			if(pRequest->bCreateRequest)
				continue;

			pData = calloc(sizeof(PuppetTransformData),1);
			pData->entRefMaster = entGetRef(pOwner);
			pRequest->bCreateRequest = true;

			if(!pExtract)
			{
				pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
			}

			if (!gslCreateSavedPetFromDefEx(entGetPartitionIdx(pOwner),pOwner,NULL,NULL,pPetDef,pchPupName,NULL,NULL,1,CreatePuppet_PostPetCreate,pData,0,OWNEDSTATE_OFFLINE,0,0,true, pExtract))
			{
				//This should never happen
				devassertmsg(0, "Puppet Request: Puppet creation failed");
				free(pData);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppownedcontainers, .Psaved.Pppreferredpetids");
enumTransactionOutcome trEntity_RemoveDeletedSavedPets(ATR_ARGS, NOCONST(Entity)* pEnt, ContainerRefArray *containerRefArray)
{
	if ( NONNULL(pEnt) && NONNULL(pEnt->pSaved) && NONNULL(containerRefArray) )
	{
		int i, n;

		n = eaSize(&containerRefArray->containerRefs);
		for ( i = 0; i < n; i++ )
		{
			if ( containerRefArray->containerRefs[i]->containerType == GLOBALTYPE_ENTITYSAVEDPET )
			{
				int j;
				U32 petContainerID;
				U32 missingContainerID = containerRefArray->containerRefs[i]->containerID;

				// pPuppetMaster can be NULL
				if(NONNULL(pEnt->pSaved->pPuppetMaster))
				{
					// scan puppets for the missing container
					for ( j = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets) - 1; j >= 0; j-- )
					{
						NOCONST(PuppetEntity) *puppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[j];
						petContainerID = puppetEntity->curID;
					
						if ( petContainerID == missingContainerID )
						{
							if ( puppetEntity->eState != PUPPETSTATE_OFFLINE )
							{
								// If an active puppet is missing, then something is terribly wrong, so don't try and complete the fixup.

								TRANSACTION_RETURN_LOG_FAILURE("Active puppet container is missing and can't be fixed up automatically for player %s", pEnt->debugName);
							}
							// remove the puppet entry
							StructDestroyNoConst(parse_PuppetEntity, eaRemove(&pEnt->pSaved->pPuppetMaster->ppPuppets, j));

							TRANSACTION_APPEND_LOG_SUCCESS("Removed reference to deleted puppet %d from player %s", petContainerID, pEnt->debugName);
							break;
						}
					}
				}

				// scan owned containers for the missing container
				for ( j = eaSize(&pEnt->pSaved->ppOwnedContainers) - 1; j >= 0; j-- )
				{
					petContainerID = pEnt->pSaved->ppOwnedContainers[j]->conID;
					if ( petContainerID == missingContainerID )
					{
						// if this is one of the missing containers, then remove it from the owned containers list
						StructDestroyNoConst(parse_PetRelationship, eaRemove(&pEnt->pSaved->ppOwnedContainers, j));

						// Remove the pet from the preferred pet list
						ea32FindAndRemove(&pEnt->pSaved->ppPreferredPetIDs, petContainerID);

						TRANSACTION_APPEND_LOG_SUCCESS("Removed reference to deleted saved pet %d from player %s", petContainerID, pEnt->debugName);
						break;
					}
				}
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void
RemoveDeletedSavedPets_CB(TransactionReturnVal *pReturn, void *pData)
{
	Entity *pEnt = (Entity*)entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (ContainerID)((intptr_t)pData));

	if ( pEnt != NULL && gbEnablePetAndPuppetLogging)
	{
		if ( pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
		{
			entLog(LOG_CONTAINER, pEnt, "RemoveDeletedSavedPets_CB", "failed");
		}
		else
		{
			entLog(LOG_CONTAINER, pEnt, "RemoveDeletedSavedPets_CB", "success");
		}
	}
}

static void
CheckOwnedContainers_CB(TransactionReturnVal *pReturn, void *pData)
{
	Entity *pEnt = (Entity*)entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (ContainerID)((intptr_t)pData));
	ContainerRefArray *containerRefArray = NULL;
	enumTransactionOutcome outcome;
	int i;

	if ( pEnt == NULL )
	{
		// if the entity is gone, then just skip the fixup
		return;
	}

	outcome = RemoteCommandCheck_DBCheckContainersExist(pReturn, &containerRefArray);
	if ( ( outcome != TRANSACTION_OUTCOME_SUCCESS ) || ( containerRefArray == NULL ) )
	{
		// failed to check containers with the objectDB, so just give up
		return;
	}

	// if any of the containers were found to be missing, then call the transaction that deletes the references to them on the player
	if ( eaSize(&containerRefArray->containerRefs) > 0 )
	{
		char *logTrivia = NULL;
		TransactionReturnVal *pReturnNew = objCreateManagedReturnVal(RemoveDeletedSavedPets_CB, (void *)((intptr_t)entGetContainerID(pEnt)));
		AutoTrans_trEntity_RemoveDeletedSavedPets(pReturnNew, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, containerRefArray);

		estrConcatf(&logTrivia, "Player=%s", pEnt->debugName);
		for ( i = eaSize(&containerRefArray->containerRefs) - 1; i >= 0; i-- )
		{
			estrConcatf(&logTrivia, ", missing=%d", containerRefArray->containerRefs[i]->containerID);
		}
		ErrorDetailsf("%s", logTrivia);
		Errorf("Player entity references missing pet containers.  Attempting to fix.");
		entLog(LOG_PLAYER, pEnt, "CheckOwnedContainers_CB", "%s", logTrivia);
		estrDestroy(&logTrivia);
	}

	StructDestroy(parse_ContainerRefArray, containerRefArray);
}

static void
CheckOwnedContainers(Entity *pEnt)
{
	TransactionReturnVal *pReturn;
	int i;
	ContainerRefArray *containerRefArray;
	CONST_EARRAY_OF(PetRelationship) ppOwnedContainers = pEnt->pSaved->ppOwnedContainers;

	PERFINFO_AUTO_START_FUNC();

	pReturn = objCreateManagedReturnVal(CheckOwnedContainers_CB, (void *)((intptr_t)entGetContainerID(pEnt)));
	containerRefArray = StructCreate(parse_ContainerRefArray);

	// create a list of all owned containers
	for(i=eaSize(&ppOwnedContainers)-1;i>=0;i--)
	{
		ContainerRef *containerRef;

		containerRef = StructCreate(parse_ContainerRef);

		containerRef->containerID = ppOwnedContainers[i]->conID;
		containerRef->containerType = GLOBALTYPE_ENTITYSAVEDPET;

		eaPush(&containerRefArray->containerRefs, containerRef);
	}

	// ask the database to confirm that they all exist
	RemoteCommand_DBCheckContainersExist(pReturn, GLOBALTYPE_OBJECTDB, 0, containerRefArray);

	StructDestroy(parse_ContainerRefArray, containerRefArray);

	entLog(LOG_PLAYER, pEnt, "CheckOwnedContainers", "");

	PERFINFO_AUTO_STOP_FUNC();
}

void Entity_PuppetMasterAndPetTick(Entity *pent)
{
	GameAccountDataExtract *pExtract = NULL;
	bool bResetPowersArray = false;

	if (!pent || !pent->pSaved)
		return;

	PERFINFO_AUTO_START_FUNC();

	// If not passed puppet check, perform it again
	if(pent->pSaved->pPuppetMaster &&
		!pent->pSaved->pPuppetMaster->bPuppetCheckPassed)
	{
		Entity_PuppetCheck(pent);
	}

	// If have passed puppet check, and don't need to swap (!PuppetProgress),
	// and we're in login (Disconnected and Logging_In), then finish login
	// If you change the internals of this if statement, do the same in HandlePlayerLogin()
	if (pent->pSaved->pPuppetMaster &&
		pent->pSaved->pPuppetMaster->bPuppetCheckPassed &&
		pent->pSaved->pPuppetMaster->bSkippedSuccessOnLogin &&
		!entCheckFlag(pent,ENTITYFLAG_PUPPETPROGRESS) &&
		entCheckFlag(pent,ENTITYFLAG_PLAYER_DISCONNECTED) &&
		entCheckFlag(pent,ENTITYFLAG_PLAYER_LOGGING_IN))
	{
		// Finish login successfully because don't need to swap
		HandlePlayerLogin_Success(pent,kLoginSuccess_PuppetSwap);

		entClearCodeFlagBits(pent,ENTITYFLAG_PLAYER_DISCONNECTED);
		entClearCodeFlagBits(pent,ENTITYFLAG_PLAYER_LOGGING_IN);
	}

	if(pent->pSaved->bCheckPets)
	{
		pent->pSaved->bCheckPets = false;
		Entity_PetCheck(pent);
	}

	if(pent->pSaved->bPowerPropFail || !pent->pSaved->bValidatedOwnedContainers)
	{
		int iPartitionIdx = entGetPartitionIdx(pent);
		int i;

		for(i=eaSize(&pent->pSaved->ppOwnedContainers)-1;i>=0;i--)
		{
			if(!SavedPet_GetEntity(iPartitionIdx, pent->pSaved->ppOwnedContainers[i]))
				break;
		}

		if(i==-1)
		{
			// getting here means that all owned container references have been satisfied
			if ( pent->pSaved->bPowerPropFail )
			{
				pent->pSaved->bPowerPropFail = false;
				bResetPowersArray = true;
			}

			pent->pSaved->bValidatedOwnedContainers = true;
		}
	}

	// If 10 seconds have passed since we arrived at the map and we haven't yet received all owned containers, then 
	// it is possible that one of them doesn't exist.  If that is the case then we need to fix it.
	if ( !pent->pSaved->bValidatedOwnedContainers && ( pent->pSaved->timeEnteredMap != 0) && ( timeSecondsSince2000() >= ( pent->pSaved->timeEnteredMap + 10 ) ) )
	{
		pent->pSaved->bValidatedOwnedContainers = true;
		CheckOwnedContainers(pent);
	}

	if(!pent->pSaved->bCheckPets
		&& !entCheckFlag(pent,ENTITYFLAG_PLAYER_DISCONNECTED) 
		&& !entCheckFlag(pent,ENTITYFLAG_PLAYER_LOGGING_IN))
	{
		int iPartitionIdx = entGetPartitionIdx(pent);
		int i;
		bool bPowerTreeNotInSync = false;
		//Check all power tree number to make sure they match
		for(i=eaSize(&pent->pSaved->ppOwnedContainers)-1;i>=0;i--)
		{
			Entity *pPetEntity = SavedPet_GetEntity(iPartitionIdx, pent->pSaved->ppOwnedContainers[i]);

			if(pPetEntity && pPetEntity->pChar
				&& pPetEntity->pChar->uiPowerTreeModCount != pent->pSaved->ppOwnedContainers[i]->uiPowerTreeModCount)
			{
				pent->pSaved->ppOwnedContainers[i]->uiPowerTreeModCount = pPetEntity->pChar->uiPowerTreeModCount;
				bPowerTreeNotInSync = true;
			}
		}
		
		if(bPowerTreeNotInSync)
		{
			bResetPowersArray = true;
			character_DirtyInnateAccrual(pent->pChar);
		}
	}

	if(bResetPowersArray)
		pExtract = entity_GetCachedGameAccountDataExtract(pent);

	// Check for possible puppet create requests
	gslHandlePuppetCreateRequests(pent, pExtract);

	// expectedID is set if we ran a puppet swap and it completed, but
	// at the time of completion of the transaction, the puppet data wasn't 
	// in the current entity.  This is how we detect that the data is present
	// and finally call the transform complete.
	if(pent->pChar && pent->pSaved->pPuppetMaster && pent->pSaved->pPuppetMaster->expectedID)
	{
		GlobalType eExpectedType = pent->pSaved->pPuppetMaster->expectedType;
		ContainerID uExpectedID = pent->pSaved->pPuppetMaster->expectedID;
		bool bCompletePuppetTransform = false;

		if (eExpectedType == GLOBALTYPE_ENTITYCRITTER)
		{
			if (uExpectedID == pent->pSaved->pPuppetMaster->curTempID)
			{
				bCompletePuppetTransform = true;
			}
		}
		else if (eExpectedType == pent->pSaved->pPuppetMaster->curType &&
				 uExpectedID == pent->pSaved->pPuppetMaster->curID)
		{
			bCompletePuppetTransform = true;
		}

		if (bCompletePuppetTransform)
		{
			TransformtoPuppet_TransformComplete(pent);
			bResetPowersArray = false;
		}
	}

	if (bResetPowersArray)
	{
		character_ResetPowersArray(entGetPartitionIdx(pent), pent->pChar, pExtract);
	}

	scp_CheckForFinishedTraining(pent);

	PERFINFO_AUTO_STOP_FUNC();
}

void gslHandleCritterPetAtLogin(Entity *pOwner)
{
	Vec3 vOwnerPos;
	WorldRegion *pRegion;
	RegionRules *pRegionRules;
	int *piExpiredRelationships = NULL;
	int i;

	if(!pOwner || !pOwner->pSaved)
		return;

	PERFINFO_AUTO_START_FUNC();

	entGetPos(pOwner,vOwnerPos);

	pRegion = worldGetWorldRegionByPos(vOwnerPos);
	pRegionRules = getRegionRulesFromRegion(pRegion);

	if(!pRegionRules || pRegionRules->iAllowedPetsPerPlayer == 0)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	for(i=0;i<eaSize(&pOwner->pSaved->ppCritterPets);i++)
	{
		PetDef *pPetDef = GET_REF(pOwner->pSaved->ppCritterPets[i]->hPetDef);
		CritterDef *pCritterDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
		CharacterClass *pClass = pCritterDef ? RefSystem_ReferentFromString(g_hCharacterClassDict,pCritterDef->pchClass) : NULL;

		if(!pClass || ea32Find(&pRegionRules->ePetType,pClass->eType) == -1)
		{
			ea32Push(&piExpiredRelationships,i);
			continue;
		}

		if(gslTeam_PetSpawnIfAvailableSlots(pOwner, pOwner->pSaved->ppCritterPets[i]->uiPetID))
			gslSummonCritterPet(pOwner,&pOwner->pSaved->ppCritterPets[i],pPetDef);
	}

	for(i=ea32Size(&piExpiredRelationships)-1;i>=0;i--)
	{
		CritterPetRelationship *pRelationship = pOwner->pSaved->ppCritterPets[i];
		eaRemove(&pOwner->pSaved->ppCritterPets,piExpiredRelationships[i]);

		StructDestroy(parse_CritterPetRelationship,pRelationship);
	}
	ea32Destroy(&piExpiredRelationships);

	PERFINFO_AUTO_STOP();
}

//Returns false if we need to defer handling of this pet until it has arrived on the server.
bool gslHandleSavedPetsAtLoginHelper(Entity* pOwner, PetRelationship* conRelation)
{
	WorldRegion *pRegion;
	RegionRules *pRegionRules;
	Vec3 vOwnerPos;

	if (!pOwner)
		return true;

	entGetPos(pOwner,vOwnerPos);

	pRegion = worldGetWorldRegionByPos(vOwnerPos);
	pRegionRules = getRegionRulesFromRegion(pRegion);

	if (conRelation->eRelationship == CONRELATION_PET || conRelation->eRelationship == CONRELATION_PRIMARY_PET)
	{
		int iPartitionIdx = entGetPartitionIdx(pOwner);

		if (conRelation->eState == OWNEDSTATE_AUTO_SUMMON)
		{
			Entity *pPet = SavedPet_GetEntity(iPartitionIdx, conRelation);
			CharacterClass *pClass = pPet ? GET_REF(pPet->pChar->hClass) : NULL;

			if(!pPet)
			{
				//If this fails, the entity isn't available yet. Defer handling this pet to the tick function.
				conRelation->bDeferLoginHandling = true;
				if (pOwner->pSaved)
					pOwner->pSaved->bCheckPets = true;
				return false;
			}

			if(!pRegionRules || (pClass && ea32Find(&pRegionRules->peCharClassTypes,pClass->eType)>=0))
			{
				//pSpecificPetToApprove = StructCreate(parse_PetToAddToTeam);
				//pSpecificPetToApprove->iPetID = StringToContainerID(REF_STRING_FROM_HANDLE(conRelation->hPet));
				//pSpecificPetToApprove->ePetType = GLOBALTYPE_ENTITYSAVEDPET;
				//eaPush(&PetsToAdd->pets, pSpecificPetToApprove);
				gslSummonSavedPet(pOwner, GLOBALTYPE_ENTITYSAVEDPET, 
					conRelation->conID,
					Entity_GetSeedNumber(iPartitionIdx, pPet,vOwnerPos));
			}
		}

		if (conRelation->eState == OWNEDSTATE_ACTIVE)
		{
			Entity *pPet = SavedPet_GetEntity(iPartitionIdx, conRelation);

			if(!pPet)
			{
				//If this fails, the entity isn't available yet. Defer handling this pet to the tick function.
				conRelation->bDeferLoginHandling = true;
				if (pOwner->pSaved)
					pOwner->pSaved->bCheckPets = true;
				return false;
			}

			if (pPet == pOwner)
				return true;

			if(PetStateActive_summoncheck(pOwner,conRelation->conID,conRelation,pRegionRules))
			{
				if(gslTeam_PetSpawnRequest(pOwner, conRelation->conID, !gConf.IgnoreAwayTeamRulesForTeamPets))
				{
					gslSummonSavedPet(pOwner, GLOBALTYPE_ENTITYSAVEDPET, 
						conRelation->conID,
						Entity_GetSeedNumber(entGetPartitionIdx(pOwner), pPet, vOwnerPos));
				}
				//					gslSummonSavedPet(pOwner, GLOBALTYPE_ENTITYSAVEDPET, 
				//						StringToContainerID(REF_STRING_FROM_HANDLE(conRelation->hPet)));
			}
		}
	}
	return true;
}

void gslHandleSavedPetsAtLogin(Entity* pOwner)
{
	GameAccountDataExtract *pExtract;
	int i;
	//static PetsToAddContainer *PetsToAdd = 0;
	PetToAddToTeam *pSpecificPetToApprove = 0;

	//if (!PetsToAdd)
	//{
	//	PetsToAdd = StructCreate(parse_PetsToAddContainer);
	//}
	//if (!PetsToAdd->pets)
	//{
	//	eaCreate(&PetsToAdd->pets);
	//}

	if (!pOwner || !pOwner->pSaved)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();


	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		gslHandleSavedPetsAtLoginHelper(pOwner, pOwner->pSaved->ppOwnedContainers[i]);
	}
//	if (eaSize(&PetsToAdd->pets) > 0)
//	{
//		gslTeam_RequestPetsToJoinTeam(pOwner, PetsToAdd);
//		eaDestroy(&PetsToAdd->pets);
//		PetsToAdd->pets = 0;
//	}

	pExtract = entity_GetCachedGameAccountDataExtract(pOwner);

	if ( eaSize(&pOwner->pSaved->ppPetRequests) > 0 )
	{
		for(i=0;i<eaSize(&pOwner->pSaved->ppPetRequests);i++)
		{
			PetDef *pPetDef = RefSystem_ReferentFromString("PetDef",pOwner->pSaved->ppPetRequests[i]->pchType);
			const char* pchPetName = pOwner->pSaved->ppPetRequests[i]->pchName;
			PlayerCostume* pPetCostume = pOwner->pSaved->ppPetRequests[i]->pCostume;

			PropEntIDs propEntIDs = { 0 };
			PropEntIDs_FillWithActiveEntIDs(&propEntIDs, pOwner);
			gslCreateSavedPetFromDefEx(entGetPartitionIdx(pOwner),pOwner,NULL,NULL,pPetDef,pchPetName,NULL,pPetCostume,1,PetStateSet_CB,NULL,0,OWNEDSTATE_ACTIVE,&propEntIDs, true, false, pExtract);
			PropEntIDs_Destroy(&propEntIDs);
		}

		AutoTrans_trEntity_ClearPetRequests(LoggedTransactions_CreateManagedReturnVal("ClearPetRequests",NULL,NULL),GLOBALTYPE_GAMESERVER,entGetType(pOwner),entGetContainerID(pOwner));
	}

	gslHandlePuppetCreateRequests(pOwner, pExtract);

	PERFINFO_AUTO_STOP();
}

// Does fixup for removed PetIDs.  Mostly a wrapper for entity_ResetPowerIDsHelper.
//  Takes the main Entity, and an earray of additional Entities (the SavedPet OwnedContainers
//  of the main Entity).
AUTO_TRANSACTION
	ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], .Psaved.Pipetidsremovedfixup, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .pInventoryV2.Ppinventorybags, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
	ATR_LOCKS(ppOwned, "pInventoryV2.ppLiteBags[], .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees, .pInventoryV2.Ppinventorybags, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome trEntity_PetIDsRemovedFixup(ATR_ARGS,
												   NOCONST(Entity)* pEnt,
												   CONST_EARRAY_OF(NOCONST(Entity)) ppOwned,
												   GameAccountDataExtract *pExtract)
{
	int i,j;
	for(i=0; i<eaiSize(&pEnt->pSaved->piPetIDsRemovedFixup); i++)
	{
		S32 iPetID = pEnt->pSaved->piPetIDsRemovedFixup[i];
		U32 uiSourceMask = iPetID ? POWERID_CREATE(0,iPetID,POWERID_TYPE_SAVEDPET) : -1;

		entity_ResetPowerIDsHelper(ATR_PASS_ARGS, pEnt, uiSourceMask, pExtract,false);
		for(j=eaSize(&ppOwned)-1; j>=0; j--)
		{
			entity_ResetPowerIDsHelper(ATR_PASS_ARGS, ppOwned[j], uiSourceMask, pExtract,false);
		}
	}
	eaiDestroy(&pEnt->pSaved->piPetIDsRemovedFixup);
	return TRANSACTION_OUTCOME_SUCCESS;
}

bool findRemovedPetIDInPowers(Entity *pEnt, Entity *pPet)
{
	int i;

	if(!pEnt || !pPet)
		return false;

	for(i=0;i<eaiSize(&pEnt->pSaved->piPetIDsRemovedFixup);i++)
	{
		S32 iPetID = pEnt->pSaved->piPetIDsRemovedFixup[i];
		U32 uiSourceMask = iPetID ? POWERID_CREATE(0,iPetID,POWERID_TYPE_SAVEDPET) : -1;

		if(entity_ResetPowerIDsHelper(ATR_EMPTY_ARGS,(NOCONST(Entity)*)pPet,uiSourceMask,NULL,true))
		{
			return true;
		}
	}

	return false;
}

// Remote Command for calling trEntity_PetIDsRemovedFixup from inside a transaction.
AUTO_COMMAND_REMOTE;
void RemoteEntityPetIDsRemovedFixup(GlobalType eGlobalTypeMain, ContainerID cidMain)
{
	Entity *pEnt;
	
	PERFINFO_AUTO_START_FUNC();

	pEnt = entFromContainerIDAnyPartition(eGlobalTypeMain, cidMain);
	if(pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		U32 *pcidOwnedContainers = entity_GetOwnedContainerIDsArray(pEnt);
		int i;

		for(i=0; i<eaSize(&pEnt->pSaved->ppOwnedContainers); i++)
		{
			Entity *pPet = SavedPet_GetEntity(entGetPartitionIdx(pEnt),pEnt->pSaved->ppOwnedContainers[i]);

			if(!findRemovedPetIDInPowers(pEnt,pPet))
			{
				ea32FindAndRemove(&pcidOwnedContainers,pEnt->pSaved->ppOwnedContainers[i]->conID);
			}
		}

		if(ea32Size(&pcidOwnedContainers) != 0 || findRemovedPetIDInPowers(pEnt,pEnt))
		{
			AutoTrans_trEntity_PetIDsRemovedFixup(LoggedTransactions_MakeReturnVal("EntityPetIDsRemovedFixup"), GLOBALTYPE_GAMESERVER, 
					pEnt->myEntityType, pEnt->myContainerID, 
					GLOBALTYPE_ENTITYSAVEDPET, &pcidOwnedContainers,
					pExtract);
		}

		ea32Destroy(&pcidOwnedContainers);
	}

	PERFINFO_AUTO_STOP();
}

int petDiagNodeSort(const NOCONST(PTNode) **pptr1, const NOCONST(PTNode) **pptr2)
{
	if((*pptr2)->iRank != (*pptr1)->iRank)
		return (*pptr2)->iRank - (*pptr1)->iRank;

	if(eaSize(&(*pptr1)->ppPurchaseTracker) && eaSize(&(*pptr2)->ppPurchaseTracker))
	{
		return (*pptr1)->ppPurchaseTracker[eaSize(&(*pptr1)->ppPurchaseTracker)-1]->uiOrderCreated -
			(*pptr2)->ppPurchaseTracker[eaSize(&(*pptr2)->ppPurchaseTracker)-1]->uiOrderCreated;
	}
	
	return eaSize(&(*pptr2)->ppPurchaseTracker) - eaSize(&(*pptr1)->ppPurchaseTracker);
}

AUTO_TRANS_HELPER;
PowerDef *SavedPet_trh_FindPowerDefFromNode(ATH_ARG NOCONST(PTNode) *pNode)
{
	if(ISNULL(pNode))
		return NULL;

	if(pNode->bEscrow)
	{
		PTNodeDef *pNodeDef = GET_REF(pNode->hDef);

		if(pNodeDef && eaSize(&pNodeDef->ppRanks) > 0)
		{
			return GET_REF(pNodeDef->ppRanks[0]->hPowerDef);
		}
	}
	else
	{
		if(eaSize(&pNode->ppPowers) > 0)
			return GET_REF(pNode->ppPowers[0]->hDef);
	}

	return NULL;
}

static S32 PetDiagSteps(int iPartitionIdx, NOCONST(Entity) *pSavedPet, PetDiag *pPetDiag, PowerTreeSteps *pStepsRespec, PowerTreeSteps *pStepsBuy)
{
	int i;
	NOCONST(PTNode) **ppPowerNodes = NULL;
	PowerTreeStep *pStep;
	int t,n;

	for(t=0;t<eaSize(&pSavedPet->pChar->ppPowerTrees);t++)
	{
		for(n=0;n<eaSize(&pSavedPet->pChar->ppPowerTrees[t]->ppNodes);n++)
		{
			eaPush(&ppPowerNodes,pSavedPet->pChar->ppPowerTrees[t]->ppNodes[n]);
		}
	}

	for(i=0;i<eaSize(&pPetDiag->ppNodes);i++)
	{
		NOCONST(PTNode) **ppFoundNodes = NULL;
		for(n=eaSize(&ppPowerNodes)-1;n>=0;n--)
		{
			NOCONST(PTNode) *pNode = ppPowerNodes[n];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
			PowerDef *pPowerDef = SavedPet_trh_FindPowerDefFromNode(pNode);

			if(pNodeDef && pPowerDef)
			{
				if(pPetDiag->ppNodes[i]->ePurpose == pNodeDef->ePurpose)
				{
					int c;

					for(c=ea32Size(&pPetDiag->ppNodes[i]->piCategories)-1;c>=0;c--)
					{
						if(ea32Find(&pPowerDef->piCategories,pPetDiag->ppNodes[i]->piCategories[c])==-1)
							break;
					}

					if(c!=-1)
						continue;

					eaPush(&ppFoundNodes,ppPowerNodes[n]);
				}
			}
			else
			{
				eaRemove(&ppPowerNodes,n);
			}
		}

		if(pPetDiag->ppNodes[i]->iCount < eaSize(&ppFoundNodes))
		{
			eaQSort(ppFoundNodes,petDiagNodeSort);

			while(pPetDiag->ppNodes[i]->iCount < eaSize(&ppFoundNodes))
			{
				NOCONST(PTNode) *pNodeToRemove = ppFoundNodes[0];
				PTNodeDef *pNodeDef = GET_REF(pNodeToRemove->hDef);
				PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
				eaRemove(&ppFoundNodes,0);
				eaFindAndRemove(&ppPowerNodes,pNodeToRemove);

				if (isDevelopmentMode())
				{
					Alertf("PetDiag Removed node %s from saved pet %d:%s",REF_STRING_FROM_HANDLE(pNodeToRemove->hDef),pSavedPet->myContainerID,pSavedPet->pSaved->savedName);
				}
				if(pNodeToRemove->bEscrow)
				{
					// This very specifically is allowed to remove an Node that is in escrow, which is not generally legal
					pStep = StructCreate(parse_PowerTreeStep);
					pStep->pchTree = pTreeDef->pchName;
					pStep->pchNode = pNodeDef->pchNameFull;
					pStep->bEscrow = true;
					eaPush(&pStepsRespec->ppSteps,pStep);
				}
				else
				{
					int iRank = pNodeToRemove->iRank;
					for(; iRank >= 0; iRank--)
					{
						pStep = StructCreate(parse_PowerTreeStep);
						pStep->pchTree = pTreeDef->pchName;
						pStep->pchNode = pNodeDef->pchNameFull;
						eaPush(&pStepsRespec->ppSteps,pStep);
					}
				}
			}
		}
		else
		{
			n=0;
			while(pPetDiag->ppNodes[i]->iCount > eaSize(&ppFoundNodes))
			{
				if(n>=eaSize(&pPetDiag->ppNodes[i]->ppReplacements))
				{
					devassertmsg(0,"PetDiag does not have enough powers in the replacements list!");
					break;
				}
				else
				{
					PTNodeDef *pNodeDef = GET_REF(pPetDiag->ppNodes[i]->ppReplacements[n]->hNodeDef);
					PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);

					NOCONST(PTNode)* pNode = entity_PowerTreeNodeEscrowHelper(iPartitionIdx,pSavedPet,NULL,pTreeDef->pchName,pNodeDef->pchNameFull,pStepsBuy);
					if(pNode)
					{
						eaPush(&ppFoundNodes,pNode);
					}
					n++;

					if (isDevelopmentMode())
					{
						Alertf("PetDiag placed node %s in escrow for saved pet %d:%s",pNodeDef->pchNameFull,pSavedPet->myContainerID,pSavedPet->pSaved->savedName);
					}
				}	
			}
		}	
		eaDestroy(&ppFoundNodes);
	}
	eaDestroy(&ppPowerNodes);

	pStepsRespec->uiPowerTreeModCount = pSavedPet->pChar->uiPowerTreeModCount;

	return (eaSize(&pStepsRespec->ppSteps) || eaSize(&pStepsBuy->ppSteps));
}

static void PetDiagApply(int iPartitionIdx, Entity *pOwner, SA_PARAM_NN_VALID PetRelationship *pPetRelationship)
{
	Entity *pPetEnt;

	PERFINFO_AUTO_START_FUNC();
	
	pPetEnt = GET_REF(pPetRelationship->hPetRef);
	if(pPetEnt)
	{
		PetDef *pPetDef = pPetEnt->pCritter ? GET_REF(pPetEnt->pCritter->petDef) : NULL;
		PetDiag *pPetDiag = pPetDef ? GET_REF(pPetDef->hPetDiag) : NULL;
		if(pPetDiag)
		{
			NOCONST(Entity)* pPetEntCopy = StructCloneWithCommentDeConst(parse_Entity,pPetEnt, "Temp entity for pet diag test");
			PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
			PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);
			ItemChangeReason reason = {0};

			if(PetDiagSteps(iPartitionIdx,pPetEntCopy,pPetDiag,pStepsRespec,pStepsBuy))
			{
				RespecCBData* pData = Respec_CreateCBData(pPetEnt, false);
				TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("PetRespec", pPetEnt, Respec_CB, pData);

				inv_FillItemChangeReason(&reason, pOwner, "Pets:SavedPetFixup", pPetEnt->debugName);

				AutoTrans_trEntity_PowerTreeStepsRespec(
					pReturn,
					GetAppGlobalType(),
					entGetType(pPetEnt),
					entGetContainerID(pPetEnt),
					0,
					0,
					pStepsRespec,
					pStepsBuy,
					&reason,
					kPTRespecGroup_ALL);
			}
			StructDestroyNoConst(parse_Entity,pPetEntCopy);
			StructDestroy(parse_PowerTreeSteps,pStepsRespec);
			StructDestroy(parse_PowerTreeSteps,pStepsBuy);
		}
	}

	PERFINFO_AUTO_STOP();
}

// Helper for performing fixup on SavedPets during login
AUTO_TRANS_HELPER
ATR_LOCKS(pOwner, ".Pplayer.Skilltype, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Eskillspecialization, .Psaved.Ugamespecificfixupversion, pInventoryV2.ppLiteBags[], .Pplayer.Playertype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
ATR_LOCKS(pSavedPet, ".Psaved.Pscpdata, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pchar.Hspecies, .Psaved.Costumedata.Iactivecostume, .Psaved.Pptrayelems_Obsolete, .Psaved.Ppautoattackelems_Obsolete, .Psaved.Pppreferredpetids, .Pchar.Hclass, .Pchar.Ilevelexp, .Pinventoryv2, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Pppowertrees, .Pchar.Ppassignedstats, .Pchar.Pppowersclass, .Pchar.Pppowerspersonal, .Psaved.Ugamespecificfixupversion, .Psaved.Costumedata.Pcslotset, .Psaved.Costumedata.Islotsetversion, .Psaved.Costumedata.Eacostumeslots, .Pplayer.Playertype");
void SavedPetFixupHelper(ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity) *pSavedPet, bool bIsPuppet, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract, bool bAllowOwnerChange)
{
	if(NONNULL(pSavedPet))
	{
		PERFINFO_AUTO_START_FUNC();

		inv_ent_trh_VerifyInventoryData(ATR_EMPTY_ARGS, pSavedPet, !bIsPuppet, !bIsPuppet, pReason);
		inv_trh_FixupEquipBags(ATR_EMPTY_ARGS, pOwner, pSavedPet, pReason, pExtract, bAllowOwnerChange);

		if(NONNULL(pSavedPet->pChar))
		{
			character_LoadTransact(pSavedPet);
		}

		// This is happening here because all existing pets have bad debug names. 
		// It will not be run if it wouldn't make a difference, as per code below - BZ
		objSetDebugName(pSavedPet->debugName, MAX_NAME_LEN,
			pSavedPet->myEntityType,
			pSavedPet->myContainerID, 0, NULL, NULL);

		{
			PCSlotSet *pSet = costumeEntity_trh_GetSlotSet(ATR_EMPTY_ARGS, pSavedPet, !bIsPuppet);
			costumeEntity_trh_FixupCostumeSlots(ATR_EMPTY_ARGS, pOwner, pSavedPet, pSet ? pSet->pcName : NULL);
		}

		if (bIsPuppet)
		{
			entity_trh_CleanupOldTray(pSavedPet);
			if (NONNULL(pSavedPet->pSaved) && ea32Size(&pSavedPet->pSaved->ppPreferredPetIDs))
			{
				ea32Destroy(&pSavedPet->pSaved->ppPreferredPetIDs);
			}
		}

		PERFINFO_AUTO_STOP();
	} 
}

static bool SavedPet_FixupTray(Entity* pOwner, Entity* pPetEnt, PetRelationship* pPetRelationship)
{
	if (eaSize(&pOwner->pSaved->ppTrayElems_Obsolete) || eaSize(&pOwner->pSaved->ppAutoAttackElems_Obsolete)
		|| eaSize(&pPetEnt->pSaved->ppTrayElems_Obsolete) || eaSize(&pPetEnt->pSaved->ppAutoAttackElems_Obsolete))
	{
		PuppetEntity* pPuppet = SavedPet_GetPuppetFromPet(pOwner, pPetRelationship);
		PuppetMaster* pPuppetMaster = pOwner->pSaved->pPuppetMaster;
		
		if (pPuppet)
		{
			if (pPuppet->curID == pPuppetMaster->curID && pPuppet->curType == pPuppetMaster->curType)
			{
				entity_CopyOldTrayData(pOwner, &pPuppet->PuppetTray);
				entity_SetDirtyBit(pOwner, parse_SavedEntityData, pOwner->pSaved, false);
				return true;
			}
			else
			{
				entity_CopyOldTrayData(pPetEnt, &pPuppet->PuppetTray);
				entity_SetDirtyBit(pOwner, parse_SavedEntityData, pOwner->pSaved, false);
				return true;
			}
		}
	}
	return false;
}

// Returns true if we think we need to run the SavedPetFixup transaction on the owned container
static S32 SavedPetFixupTest(SA_PARAM_NN_VALID Entity *pOwner, SA_PARAM_NN_VALID PetRelationship *pPetRelationship, bool bIsPuppet, GameAccountDataExtract* pExtract)
{
	S32 bReturn = true; // DEFAULT case is yes, just to be safe
	Entity* pPetEnt;

	PERFINFO_AUTO_START_FUNC();
	
	pPetEnt = GET_REF(pPetRelationship->hPetRef);
	if(pPetEnt)
	{
		Entity *pPetEntCopy;
		if (bIsPuppet && pPetEnt->pSaved && pOwner->pSaved && pOwner->pSaved->pPuppetMaster)
		{
			if (SavedPet_FixupTray(pOwner, pPetEnt, pPetRelationship))
			{
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
		pPetEntCopy = StructCloneWithComment(parse_Entity,pPetEnt, "Temp entity for save pet fixup test");
		if(pPetEntCopy)
		{
			S32 bDiff;
			// Note the false parameter for the bAllowOwnerChange in SavedPetFixupHelper(). This is becuase this is just a test and we do not want the owner changed like it would be in a real transaction.
			SavedPetFixupHelper(CONTAINER_NOCONST(Entity, pOwner), CONTAINER_NOCONST(Entity, pPetEntCopy), bIsPuppet, NULL, pExtract, false);
			bDiff = StructCompare(parse_Entity,pPetEnt,pPetEntCopy,0,TOK_PERSIST,TOK_NO_TRANSACT);
			if(!bDiff)
			{
				bReturn = false;
			}
			StructDestroy(parse_Entity,pPetEntCopy);
		}
	}

	PERFINFO_AUTO_STOP();

	return bReturn;
}

void DEFAULT_LATELINK_FixUpOldWeaponBags(Entity* pOwner, Entity* pPetEnt, PetRelationship* pPet, bool bIsPuppet)
{
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Psaved.Pppreferredpetids, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets");
enumTransactionOutcome trEntity_PreferredPetFixup(ATR_ARGS, NOCONST(Entity)* pOwner)
{
	int i;
	if (ISNULL(pOwner) || ISNULL(pOwner->pSaved))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	for (i = ea32Size(&pOwner->pSaved->ppPreferredPetIDs)-1; i >= 0; i--)
	{
		NOCONST(PetRelationship)* pPet;
		pPet = trhSavedPet_GetPetFromContainerID(pOwner, pOwner->pSaved->ppPreferredPetIDs[i], true);
		if (ISNULL(pPet))
		{
			ea32Remove(&pOwner->pSaved->ppPreferredPetIDs, i);
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void Entity_SavedPetFixup(Entity *pOwner)
{
	int i;
	GameAccountDataExtract* pExtract = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	
	for(i=0;i<eaSize(&pOwner->pSaved->ppOwnedContainers);i++)
	{
		PetRelationship* pPet = pOwner->pSaved->ppOwnedContainers[i];
		bool bIsPuppet = SavedPet_IsPetAPuppet(pOwner,pPet);
		Entity *pEntity = GET_REF(pPet->hPetRef);
		PetDef *pPetDef = pEntity && pEntity->pCritter ? GET_REF(pEntity->pCritter->petDef) : NULL;
		bool bMakeActive = false;
		
		if (!pExtract)
		{
			pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
		}
		if(SavedPetFixupTest(pOwner,pPet,bIsPuppet,pExtract))
		{
			TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("SavedPetFixup", pEntity, NULL, NULL);
			ItemChangeReason reason = {0};
			inv_FillItemChangeReason(&reason, pOwner, "Pets:SavedPetFixup", pEntity ? pEntity->debugName : NULL);
			AutoTrans_SavedPetFixup(pReturn,GLOBALTYPE_GAMESERVER,
					GLOBALTYPE_ENTITYPLAYER, pOwner->myContainerID,
					GLOBALTYPE_ENTITYSAVEDPET, pPet->conID,
					bIsPuppet, &reason, pExtract);
		}

		entity_PowerTreeAutoBuy(entGetPartitionIdx(pOwner), pEntity, pOwner);

		PetDiagApply(entGetPartitionIdx(pOwner),pOwner,pPet);

		FixUpOldWeaponBags(pOwner, pEntity, pPet, bIsPuppet);

		if (bIsPuppet)
		{
			CharacterClass *pClass= SAFE_GET_REF2(pEntity, pChar, hClass);
			if (pClass)
			{
				CharClassCategorySet *pSet = CharClassCategorySet_getCategorySetFromClass(pClass);
				bMakeActive = !Entity_HasActivePuppetIfSet(pOwner, pSet);
			}
		}

		if(pPetDef && pPetDef->bCanBePuppet && !bIsPuppet)
			AutoTrans_trEntity_AddPuppet(NULL,GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pOwner->myContainerID,GLOBALTYPE_ENTITYSAVEDPET,pEntity->myContainerID,pPetDef->pchPetName,pEntity->pSaved->savedName,bMakeActive);
	}

	// Remove preferred pet IDs that no longer exist
	for(i=ea32Size(&pOwner->pSaved->ppPreferredPetIDs)-1;i>=0;i--)
	{
		if(!SavedPet_GetPetFromContainerID(pOwner, pOwner->pSaved->ppPreferredPetIDs[i], true))
		{
			AutoTrans_trEntity_PreferredPetFixup(NULL, 
												 GetAppGlobalType(), 
												 entGetType(pOwner), 
												 entGetContainerID(pOwner));
			break;
		}
	}

	// Also do the removed pet fixup if required
	if(eaiSize(&pOwner->pSaved->piPetIDsRemovedFixup))
	{
		RemoteEntityPetIDsRemovedFixup(pOwner->myEntityType,pOwner->myContainerID);
	}

	PERFINFO_AUTO_STOP();
}

void gslHandlePetsAtLogin(Entity *pOwner)
{
	PERFINFO_AUTO_START_FUNC();

	gslHandleCritterPetAtLogin(pOwner);
	gslHandleSavedPetsAtLogin(pOwner);
	Entity_PuppetCheck(pOwner);
	Entity_PetCheck(pOwner);

	Entity_SavedPetFixup(pOwner);
	Entity_SuperCritterPetFixup(pOwner);

	if (SAFE_MEMBER3(pOwner, pSaved, pPuppetMaster, bPuppetTransformFailed))
	{
		char* estrError = NULL;
		estrStackCreate(&estrError);
		entFormatGameMessageKey(pOwner, &estrError, "Puppet_Transformation_Failed", STRFMT_ENTITY_KEY("Owner", pOwner), STRFMT_END);
		notify_NotifySend(pOwner, kNotifyType_PuppetTransformFailed, estrError, NULL, NULL);
		estrDestroy(&estrError);
		pOwner->pSaved->pPuppetMaster->bPuppetTransformFailed = false;
	}
	PERFINFO_AUTO_STOP();
}

Entity *gslSavedPetGetOwner(Entity *pSavedPetEnt)
{
	if (pSavedPetEnt && pSavedPetEnt->pSaved){
		return entFromContainerIDAnyPartition(pSavedPetEnt->pSaved->conOwner.containerType, pSavedPetEnt->pSaved->conOwner.containerID);
	}
	return NULL;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void gslSpawnApprovedPets(GlobalType eOwnerType, U32 iOwnerID, PetsToAddContainer *pPets)
{
	Entity *pOwner = entFromContainerIDAnyPartition(eOwnerType, iOwnerID);
	int i;
	if (pPets && (eaSize(&pPets->pets) > 0))
	{
		for (i = 0; i < eaSize(&pPets->pets); ++i)
		{
			gslSummonSavedPet(pOwner, pPets->pets[i]->ePetType, 
				pPets->pets[i]->iPetID,
				i);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("pm_PrintCurrentRegion");
void cmdPuppetMaster_CheckCurrentRegion(Entity *pEnt)
{
	printf("Current Region for %s: %s\n",pEnt->debugName,StaticDefineIntRevLookup(WorldRegionTypeEnum,
		entGetWorldRegionTypeOfEnt(pEnt)));
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("pm_PrintOwnedPuppets");
void cmdPuppetMaster_PrintOwnedPuppets(Entity *pEnt)
{
	int i;
	int iCount=0;
	int iPartitionIdx;

	if(!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster)
	{
		printf("%s does not have any Puppets\n",pEnt->debugName);
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);
	for(i=0;i<eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets);i++)
	{
		PuppetEntity* pet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
		Entity *pEntPet = SavedPuppet_GetEntity(iPartitionIdx, pEnt->pSaved->pPuppetMaster->ppPuppets[i]);

			if(pEntPet)
				printf("Puppet %s : %s ID %d\n", pEntPet->debugName, pEntPet->pCritter ? REF_STRING_FROM_HANDLE(pEntPet->pCritter->critterDef) : "No critter def", pEntPet->myContainerID);
			else
				printf("Puppet UNKNOWN\n");


			iCount++;
	}

	if(iCount)
		printf("%d total puppets\n",iCount);
	else
		printf("%s does not have any Puppets\n",pEnt->debugName);

	printf("Saved Puppet Status: %s",pEnt->pSaved->pPuppetMaster->bPuppetCheckPassed ? "PASSED" : "NOT-PASSED!");
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("pm_PrintOwnedPets");
void cmdPuppetMaster_PrintOwnedPets(Entity *pEnt)
{
	int i;
	int iCount =0;
	int iPartitionIdx;

	if(!pEnt->pSaved)
	{	
		printf("%s does not have any Owned Pets\n",pEnt->debugName);
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);
	for(i=0;i<eaSize(&pEnt->pSaved->ppOwnedContainers);i++)
	{
		Entity *pEntPet = SavedPet_GetEntity(iPartitionIdx, pEnt->pSaved->ppOwnedContainers[i]);

		if(pEntPet)
			printf("Pet %s : %s ID %d Puppet %s - Status: %s\n",pEntPet->debugName,pEntPet->pCritter ? REF_STRING_FROM_HANDLE(pEntPet->pCritter->critterDef) : "No critter def", pEntPet->myContainerID,SavedPet_IsPetAPuppet(pEnt,pEnt->pSaved->ppOwnedContainers[i]) ? "True" : "False",StaticDefineIntRevLookup(OwnedContainerStateEnum,pEnt->pSaved->ppOwnedContainers[i]->eState));
		else
			printf("Pet UNKNOWN\n");

		iCount++;
	}

	if(iCount)
		printf("%d total pets\n",iCount);
	else
		printf("%s does not have any Owned Pets\n",pEnt->debugName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("pm_PrintCurrentPuppet");
void cmdPuppetMaster_PrintCurrentPuppet(Entity *pEnt)
{
	Entity *pActivePuppet;

	if(!pEnt->pSaved || !pEnt->pSaved->pPuppetMaster)
	{
		printf("%s is not a puppet master!",pEnt->debugName);
		return;
	}

	pActivePuppet = entFromContainerID(entGetPartitionIdx(pEnt), pEnt->pSaved->pPuppetMaster->curType,pEnt->pSaved->pPuppetMaster->curID);

	if(pActivePuppet)
	{
		printf("Debug Name: %s Container ID: %d Puppet Master: %s",pActivePuppet->debugName,pActivePuppet->myContainerID,(entGetType(pActivePuppet) == GLOBALTYPE_ENTITYPLAYER) ? "True" : "False");
	}
}

/*********************** Power Propagation Functions **********************/

void ent_PropagatePowers(int iPartitionIdx, Entity *pEntity, Power** ppOldPropPowers, GameAccountDataExtract *pExtract)
{
	Power **ppPowers = NULL;
	Entity **ppEnts = NULL;
	Entity *curEnt;
	int i;
	int iCount = 0;

	//Step 1: Get all powers that can be shared between pets if this is the owner, or from the owner if this is a pet

	if(!pEntity->pSaved)
		return;

	if (eaSize(&pEntity->pSaved->ppOwnedContainers))
	{
		for(i=0;i<eaSize(&pEntity->pSaved->ppOwnedContainers);i++)
		{
			PetRelationship *pPetRelationship = pEntity->pSaved->ppOwnedContainers[i];
			int j;

			curEnt = SavedPet_GetEntity(iPartitionIdx, pEntity->pSaved->ppOwnedContainers[i]);

			if(!curEnt || !pPetRelationship->uiPetID)
			{
				pEntity->pSaved->bPowerPropFail = true;
				return;
			}

			for(j=eaSize(&pEntity->pSaved->ppAlwaysPropSlots)-1;j>=0;j--)
			{
				if (pEntity->pSaved->ppAlwaysPropSlots[j]->iPetID == pPetRelationship->uiPetID
					&& pEntity->pSaved->ppAlwaysPropSlots[j]->iPuppetID == pEntity->pSaved->pPuppetMaster->curID)
					break;
			}

			// Propagate all powers if the entity is currently online, or is set to active with the always propagate flag turned on
			if (pPetRelationship->eState == OWNEDSTATE_AUTO_SUMMON || j > -1)
			{
				ent_FindAllPropagatePowers(curEnt,pEntity->pSaved->ppOwnedContainers[i],j>-1?GET_REF(pEntity->pSaved->ppAlwaysPropSlots[j]->hDef):NULL,ppOldPropPowers,&ppPowers,false);
			}
			else if(curEnt && pEntity->pSaved->pPuppetMaster && SavedPet_IsPetAPuppet(pEntity,pEntity->pSaved->ppOwnedContainers[i]) && pEntity->pSaved->pPuppetMaster->curID != pPetRelationship->conID)
			{
				ent_FindAllPropagatePowers(curEnt,NULL,NULL,ppOldPropPowers,&ppPowers,false);
			}
		}
	}
	else
	{
		Entity* pOwner = entFromContainerID(iPartitionIdx, pEntity->pSaved->conOwner.containerType, pEntity->pSaved->conOwner.containerID);
		if (pOwner)
		{
			ent_FindAllPropagatePowers(pOwner,NULL,NULL,ppOldPropPowers,&ppPowers,false);
		}
	}

	//Step 2: Find All the Entities that are online, and remove all propagated powers from them
	eaPush(&ppEnts,pEntity);

	for(i=0;i<eaSize(&pEntity->pSaved->ppOwnedContainers);i++)
	{
		if(pEntity->pSaved->ppOwnedContainers[i]->curEntity && pEntity->pSaved->ppOwnedContainers[i]->curEntity->pChar)
		{
			Entity *pEnt = pEntity->pSaved->ppOwnedContainers[i]->curEntity;
			eaPush(&ppEnts,pEnt);

			eaClearStruct(&pEnt->pChar->ppPowersPropagation, parse_Power);
		}
	}

	//Step 3: Find a home for all the powers
	for(i=0;i<eaSize(&ppPowers);i++)
	{
		int iEnt;
		PowerDef *pDef = GET_REF(ppPowers[i]->hDef);

		for(iEnt=0;iEnt<eaSize(&ppEnts);iEnt++)
		{
			if (ent_canTakePower(ppEnts[iEnt],pDef))
			{
				eaPush(&ppEnts[iEnt]->pChar->ppPowersPropagation,ppPowers[i]);

				// If a power is pushed into an entities propagation array, then remove it from ppPowers[].
				// Once we are done any Power structs left in ppPowers[] will be destroyed.
				ppPowers[i] = NULL;

				entity_SetDirtyBit(ppEnts[iEnt],parse_Character,ppEnts[iEnt]->pChar,false);
				break; //only 1 entity can own a power
			}
		}
	}

	//Step 4: Rebuild powers array for all ents except the master
	for(i=0;i<eaSize(&ppEnts);i++)
	{
		if (ppEnts[i] != pEntity) 
		{
			Entity *pCurrEnt = ppEnts[i];
			devassert(pCurrEnt->pChar && pCurrEnt->pChar->pEntParent);
			character_ResetPowersArray(entGetPartitionIdx(pCurrEnt), pCurrEnt->pChar, pExtract);
		}
	}
	eaDestroy(&ppEnts);
	eaDestroyStruct(&ppPowers, parse_Power);
}

bool savedPet_CanRegroup(Entity *pMaster, Entity *pPet)
{
	// If closer than ten feet and alive, don't regroup
	if(entGetDistance(pMaster,NULL,pPet,NULL,NULL) > 10)
		return true;
		
	if(pPet->pChar->pattrBasic->fHitPoints <= 0.f)
		return true;
	

	return false;
}

bool entity_CanRequestPetRegroup(Entity *pMaster)
{
	//Cannot request regroup when in combat
	if(pMaster->pChar->uiTimeCombatExit)
		return false;

	return true;
}

typedef struct SavedPetRegroupData
{
	int							iPartitionIdx;
	EntityRef					erEntRef;
	CommandQueue*				pCmdQueue;
	AIAnimList*					pAnimList;
	Vec3						vPosition;
	Quat						qRotation;
	const char*					pchPostSequenceDef;
} SavedPetRegroupData;

static void gslSavedPet_AnimationAfterMove(SavedPetRegroupData* pRegroupData)
{
	Entity *pEntity = entFromEntityRef(pRegroupData->iPartitionIdx, pRegroupData->erEntRef);

	if(pEntity)
	{
		CommandQueue_ExecuteAllCommands(pRegroupData->pCmdQueue);
		CommandQueue_Destroy(pRegroupData->pCmdQueue);
	}

	free(pRegroupData);
}

static void gslSavedPet_MoveAfterAnimation( SavedPetRegroupData* pRegroupData )
{
	Entity *pEntity = entFromEntityRef(pRegroupData->iPartitionIdx, pRegroupData->erEntRef);
	DoorTransitionSequenceDef* pSequenceDef;
	DoorTransitionSequence* pSequence;

	if(pEntity)
	{
		entSetPos(pEntity,pRegroupData->vPosition,true,"Pet_Regroup");
		entSetRot(pEntity,pRegroupData->qRotation,true,"Pet_Regroup");

		if(pEntity->pChar->pattrBasic->fHitPoints <= 0.0f)
		{
			pEntity->pChar->pattrBasic->fHitPoints = 1.0f;
			entity_SetDirtyBit(pEntity,parse_CharacterAttribs,pEntity->pChar->pattrBasic,false);
		}

		if(pRegroupData->pCmdQueue)
		{
			CommandQueue_ExecuteAllCommands( pRegroupData->pCmdQueue );
			CommandQueue_Destroy( pRegroupData->pCmdQueue );
		}
	}

	pSequenceDef = DoorTransitionSequence_DefFromName(pRegroupData->pchPostSequenceDef);
	pSequence = pSequenceDef ? eaGet(&pSequenceDef->eaSequences, 0) : NULL;

	if(pEntity && pSequence)
	{
		MovementRequester *mr = NULL;

		pRegroupData->pchPostSequenceDef = NULL; //Null it so we can use the data struct again
		pRegroupData->pCmdQueue = CommandQueue_Create( 8, false );
		pRegroupData->pAnimList = GET_REF(pSequence->pAnimation->hAnimList);
		mmRequesterCreateBasicByName(pEntity->mm.movement, &mr, "DoorMovement");

		aiAnimListSet(pEntity,pRegroupData->pAnimList,&pRegroupData->pCmdQueue);

		if(mr)
		{
			if (pSequence->pAnimation->fDuration > 0.0f)
			{
				mrDoorStartWithTime( mr, gslSavedPet_AnimationAfterMove, pRegroupData, 1, pSequence->pAnimation->fDuration );
			}
			else //don't even bother creating a door if the fDuration is <= 0
			{
				gslSavedPet_AnimationAfterMove( pRegroupData );
			}
		}
	}
	else
	{
		free(pRegroupData);
	}

	
}

AUTO_COMMAND ACMD_SERVERCMD;
void SavedPet_RegroupPets(Entity *e)
{
	int i;
	Entity **ppPets = NULL; 
	Vec3 vEnt;
	Quat qEnt;
	RegionRules *pRules = NULL;
	int iSpawnBox[] = {0,0,0};

	if(!e || !e->pSaved)
		return;

	entGetPos(e,vEnt);
	entGetRot(e,qEnt);

	pRules = RegionRulesFromVec3(vEnt);

	for(i=0;i<eaSize(&e->pSaved->ppOwnedContainers);i++)
	{
		if(e->pSaved->ppOwnedContainers[i]->curEntity)
		{
			Entity *pPet = e->pSaved->ppOwnedContainers[i]->curEntity;

			if(!savedPet_CanRegroup(e,pPet))
				continue;
			
			eaPush(&ppPets,e->pSaved->ppOwnedContainers[i]->curEntity);
		}
	}

	for(i=0;i<eaSize(&e->pSaved->ppCritterPets);i++)
	{
		if(!e->pSaved->ppCritterPets[i]->pEntity
			|| !savedPet_CanRegroup(e,e->pSaved->ppCritterPets[i]->pEntity))
		{
			continue;
		}

		eaPush(&ppPets,e->pSaved->ppCritterPets[i]->pEntity);
	}

	for(i=0;i<eaSize(&ppPets);i++)
	{
		Vec3 vSpawnPos;
		RegionRules *pRegionRules = NULL;
		DoorTransitionSequenceDef* pTransSeqDef;
		DoorTransitionSequence* pSequence;
		SavedPetRegroupData *pRegroupData = malloc(sizeof(SavedPetRegroupData));

		copyVec3(vEnt,vSpawnPos);

		pRegionRules = RegionRulesFromVec3(vEnt);

		Entity_GetPositionOffset(entGetPartitionIdx(e), pRules,qEnt,i+1,vSpawnPos,e->iBoxNumber);

		pRegroupData->iPartitionIdx = entGetPartitionIdx(e);
		pRegroupData->erEntRef = entGetRef(ppPets[i]);
		copyVec3(vSpawnPos,pRegroupData->vPosition);
		copyQuat(qEnt,pRegroupData->qRotation);
		pTransSeqDef = pRegionRules ? GET_REF(pRegionRules->hPetRequestArrive) : NULL;
		pSequence = pTransSeqDef ? eaGet(&pTransSeqDef->eaSequences, 0) : NULL;
		pRegroupData->pchPostSequenceDef = SAFE_MEMBER(pTransSeqDef, pchName);

		if(pSequence)
		{
			MovementRequester *mr = NULL;
			CommandQueue* pCmdQueue = CommandQueue_Create( 8, false );
			mmRequesterCreateBasicByName(ppPets[i]->mm.movement, &mr, "DoorMovement");

			pRegroupData->pAnimList = GET_REF(pSequence->pAnimation->hAnimList);
			pRegroupData->pCmdQueue = pCmdQueue;


			if(mr)
			{
				if (pSequence->pAnimation->fDuration > 0.0f)
				{
					mrDoorStartWithTime(mr, gslSavedPet_MoveAfterAnimation, pRegroupData, 1, pSequence->pAnimation->fDuration);
				}
				else //don't even bother creating a door if the fDuration is <= 0
				{
					gslSavedPet_MoveAfterAnimation(pRegroupData);
				}
			}

			aiAnimListSet(ppPets[i],pRegroupData->pAnimList,&pCmdQueue);
		}
		else
		{
			pRegroupData->pCmdQueue = NULL;
			gslSavedPet_MoveAfterAnimation(pRegroupData);
		}
	}

	eaDestroy(&ppPets);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SavedPet_PetRegroupRequest(Entity *e)
{
	if(!e || !entity_CanRequestPetRegroup(e))
		return;

	SavedPet_RegroupPets(e);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SavedPet_StatusFlagRequestTeamRequest(Entity *e, U32 uiPetID, int iSlotID, int ePropCategory, bool bAdd)
{
	// AMA - This function can be deleted I believe 
	//int i;

	//if(!e || !e->pSaved || !e->pChar || e->pChar->uiTimeCombatExit)
	//	return;

	//for(i=0;i<eaSize(&e->pSaved->ppOwnedContainers);i++)
	//{
	//	PetRelationship *pRel = e->pSaved->ppOwnedContainers[i];
	//	if(pRel->uiPetID == uiPetID)
	//	{
	//		ContainerID uiID = pRel->conID;
	//		if(bAdd)
	//		{
	//			if(SavedPet_FlagRequestMaxCheck(e, pRel, eNewStatus, iSlotID, ePropCategory))
	//				gslSetSavedPetStateEx(e,GLOBALTYPE_ENTITYSAVEDPET,uiID,OWNEDSTATE_ACTIVE,eNewStatus,pRel->eStatus,iSlotID,ePropCategory);
	//		}
	//		else
	//		{
	//			eNewStatus = pRel->eStatus ^ eStatus;

	//			if(SavedPet_FlagRequestMaxCheck(e, pRel, eNewStatus, iSlotID, ePropCategory))
	//				gslSetSavedPetStateEx(e,GLOBALTYPE_ENTITYSAVEDPET,uiID,eNewStatus ? OWNEDSTATE_ACTIVE : OWNEDSTATE_OFFLINE,eNewStatus,pRel->eStatus,iSlotID,ePropCategory);
	//		}
	//	}
	//}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SavedPet_StatusFlagRequestAlwaysPropSlot(Entity *e, U32 uiPetID, U32 uiPuppetID, int iSlotID, int ePropCategory, bool bAdd)
{
	int i;

	if(!e || !e->pSaved || !e->pChar || e->pChar->uiTimeCombatExit)
		return;

	for(i=0;i<eaSize(&e->pSaved->ppOwnedContainers);i++)
	{
		PetRelationship *pRel = e->pSaved->ppOwnedContainers[i];
		if(pRel->uiPetID == uiPetID)
		{
			ContainerID uiID = pRel->conID;
			static U32 *eaEntIDs = { 0 };
			static U32 *eaOldEndIDs = { 0 };
			int j;

			ea32ClearFast(&eaEntIDs);
			ea32ClearFast(&eaOldEndIDs);
			for(j=0;j<eaSize(&e->pSaved->ppAlwaysPropSlots);j++)
			{
				if(e->pSaved->ppAlwaysPropSlots[j]->iPetID == uiPetID)
				{
					ea32PushUnique(&eaEntIDs, e->pSaved->ppAlwaysPropSlots[j]->iPuppetID);
					ea32PushUnique(&eaOldEndIDs, e->pSaved->ppAlwaysPropSlots[j]->iPuppetID);
				}
			}
			if(bAdd)
			{
				ea32PushUnique(&eaEntIDs, uiPuppetID);
				if(SavedPet_FlagRequestMaxCheck(e, pRel, uiPuppetID, false, iSlotID, ePropCategory))
				{
					gslSetSavedPetStateEx(e,
						GLOBALTYPE_ENTITYSAVEDPET,
						uiID,
						(pRel->bTeamRequest) ? OWNEDSTATE_ACTIVE : OWNEDSTATE_OFFLINE,
						&eaEntIDs,
						false,
						&eaOldEndIDs,
						pRel->bTeamRequest,
						iSlotID,
						ePropCategory);
				}
			}
			else
			{
				ea32FindAndRemove(&eaEntIDs, uiPuppetID);
				if(SavedPet_FlagRequestMaxCheck(e, pRel, 0, false, iSlotID, ePropCategory))
				{
					gslSetSavedPetStateEx(e,
						GLOBALTYPE_ENTITYSAVEDPET,
						uiID,
						(pRel->bTeamRequest) ? OWNEDSTATE_ACTIVE : OWNEDSTATE_OFFLINE,
						&eaEntIDs,
						false,
						&eaOldEndIDs,
						pRel->bTeamRequest,
						iSlotID,
						ePropCategory);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SavedPet_StatusFlagSwapRequest(Entity* e, ContainerID uiOldPetID, ContainerID uiNewPetID, 
									int iSlotID, U32 uiPropEntID, bool bTeamRequest)
{
	int i;
	PetRelationship* pOldPetRel = NULL;
	PetRelationship* pNewPetRel = NULL;
	Entity* pOldPetEnt = NULL;
	Entity* pNewPetEnt = NULL;
	int iPartitionIdx;

	if(!e || !e->pSaved || uiNewPetID==0 || !e->pChar || e->pChar->uiTimeCombatExit)
		return;

	iPartitionIdx = entGetPartitionIdx(e);

	for(i=eaSize(&e->pSaved->ppOwnedContainers)-1;i>=0;--i)
	{
		if (e->pSaved->ppOwnedContainers[i]->uiPetID == uiNewPetID)
		{
			pNewPetRel = e->pSaved->ppOwnedContainers[i];
			pNewPetEnt = SavedPet_GetEntity(iPartitionIdx, e->pSaved->ppOwnedContainers[i]);
			break;
		}
	}

	if (pNewPetEnt==NULL)
		return;

	for(i=eaSize(&e->pSaved->ppAlwaysPropSlots)-1;i>=0;--i)
	{
		if ((U32)iSlotID == e->pSaved->ppAlwaysPropSlots[i]->iSlotID)
		{
			if (uiNewPetID == e->pSaved->ppAlwaysPropSlots[i]->iPetID)
			{
				return;
			}
			break;
		}
	}

	if (i < 0)
		return;

	for(i=eaSize(&e->pSaved->ppOwnedContainers)-1;i>=0;--i)
	{
		if (e->pSaved->ppOwnedContainers[i]->uiPetID == uiOldPetID)
		{
			pOldPetRel = e->pSaved->ppOwnedContainers[i];
			pOldPetEnt = SavedPet_GetEntity(iPartitionIdx, e->pSaved->ppOwnedContainers[i]);
			break;
		}
	}

	if (pOldPetRel && !pOldPetEnt)
		return;
	if (pOldPetEnt && pOldPetEnt->pChar && eaSize(&pOldPetEnt->pChar->ppTraining))
		return;
	if (pNewPetEnt && pNewPetEnt->pChar && eaSize(&pNewPetEnt->pChar->ppTraining))
		return;

	gslSwapSavedPetStateEx(e,iSlotID,pOldPetEnt,pOldPetRel,pNewPetEnt,pNewPetRel,OWNEDSTATE_ACTIVE,uiPropEntID,bTeamRequest,-1);
}

Entity **ppOfflinePets;
// A list of entities that are offline.

static void HackToSetOfflineEntityRegion(Entity *pOfflineEntity)
{
	RegionRules *pRegionRulesSpace = getRegionRulesFromRegionType(WRT_Space);
	CharClassTypes eType = GetCharacterClassEnum(pOfflineEntity);
	if(pRegionRulesSpace && ea32Find(&pRegionRulesSpace->peCharClassTypes,eType)>=0)
	{
		pOfflineEntity->astrRegion = allocAddString("FAKE_SPACE_REGION");
	}
	else
	{
		pOfflineEntity->astrRegion = allocAddString("FAKE_GROUND_REGION");
	}
}

int savedpet_findOfflineCopy(ContainerID uiID, Entity **ppEntityOut)
{
	int i;

	for(i=0;i<eaSize(&ppOfflinePets);i++)
	{
		if(ppOfflinePets[i]->myContainerID == uiID)
		{
			if(ppEntityOut)
				*ppEntityOut = ppOfflinePets[i];
			return i;
		}
	}

	return -1;
}

void savedpet_destroyOfflineCopy(int iPartitionIdx, ContainerID uiID)
{
	Entity *pSavedPet = NULL;
	int i = savedpet_findOfflineCopy(uiID,&pSavedPet);

	if(i >= 0)
	{
		eaRemoveFast(&ppOfflinePets,i);
		gslCleanupEntityEx(iPartitionIdx, pSavedPet, false, false);
		StructDestroy(parse_Entity,pSavedPet);
	}
}

void savedpet_updateOfflineCopy(int iPartitionIdx, Entity *pOfflineEntity)
{
	if(!pOfflineEntity)
		return;

	if(pOfflineEntity->pChar)
	{
		pOfflineEntity->pChar->pEntParent = pOfflineEntity;
		HackToSetOfflineEntityRegion(pOfflineEntity);
		character_TickOffline(iPartitionIdx, pOfflineEntity->pChar, NULL);
	}
}

Entity *savedpet_createOfflineCopy(Entity *pOwner, PetRelationship *pRelationship)
{
	//See if the entity is online.
	ContainerID uiPetID = pRelationship->conID;
	int iPartitionIdx = entGetPartitionIdx(pOwner);
	Entity *pSavedPet = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYSAVEDPET,uiPetID);
	Entity *pOfflinePet = NULL;

	//Clean up offline copy
	if(pSavedPet)
	{
		savedpet_destroyOfflineCopy(iPartitionIdx, uiPetID);
		return NULL;
	}

	pSavedPet = SavedPet_GetEntity(iPartitionIdx, pRelationship);

	if(!pSavedPet)
		return NULL;

	//Create offline copy if needed
	if(savedpet_findOfflineCopy(uiPetID,&pOfflinePet)==-1)
	{
		pOfflinePet = StructCreateWithComment(parse_Entity, "Offline copy ent from savedpet_CreateOfflineCopy");
		eaPush(&ppOfflinePets,pOfflinePet);
	}
	else
	{
		gslCleanupEntityEx(entGetPartitionIdx(pOwner), pOfflinePet, false, false);
	}

	//Copy all persisted fields that aren't marked as TOK_PUPPET_NO_COPY
	StructCopy(parse_Entity,pSavedPet,pOfflinePet,STRUCTCOPYFLAG_DONT_COPY_NO_ASTS,TOK_PERSIST,TOK_PUPPET_NO_COPY);

	//Special copy step for pet name and sub name because these fields are marked as TOK_PUPPET_NO_COPY
	if (pSavedPet->pSaved)
	{
		NOCONST(Entity)* pOfflinePetNoConst = CONTAINER_NOCONST(Entity, pOfflinePet);
		if (!pOfflinePetNoConst->pSaved)
		{
			pOfflinePetNoConst->pSaved = StructCreateNoConst(parse_SavedEntityData);
		}
		strcpy(pOfflinePetNoConst->pSaved->savedName, pSavedPet->pSaved->savedName);
		StructCopyString(&pOfflinePetNoConst->pSaved->savedSubName, pSavedPet->pSaved->savedSubName);
	}

	if (pOfflinePet->pChar)
	{
		pOfflinePet->pChar->iLevelCombat = entity_GetSavedExpLevel(pOfflinePet);
	}
	if (entGetType(pOfflinePet) == GLOBALTYPE_ENTITYPLAYER)
	{
		CONTAINER_NOCONST(Entity, pOfflinePet)->myEntityType = GLOBALTYPE_ENTITYSAVEDPET;
		CONTAINER_NOCONST(Entity, pOfflinePet)->myContainerID = uiPetID;
	}	
	else if (pOfflinePet->pSaved)
	{
		CONTAINER_NOCONST(Entity, pOfflinePet)->pSaved->conOwner.containerType = entGetType(pOwner);
		CONTAINER_NOCONST(Entity, pOfflinePet)->pSaved->conOwner.containerID = entGetContainerID(pOwner);
	}
	savedpet_updateOfflineCopy(entGetPartitionIdx(pOwner), pOfflinePet);

	return pOfflinePet;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(CreateOfflineCopyForPets_Server);
void gslCreateOfflineCopyForPets(Entity *pClientEntity)
{
	int i;

	if(!pClientEntity || !pClientEntity->pSaved)
		return;

	for(i=0;i<eaSize(&pClientEntity->pSaved->ppOwnedContainers);i++)
	{
		savedpet_createOfflineCopy(pClientEntity, (PetRelationship*)pClientEntity->pSaved->ppOwnedContainers[i]);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(RequestOfflineCopy) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void savedpet_RequestOfflineCopy(Entity *pClientEntity, U32 uiPetID)
{
	int i;
	int iPartitionIdx;

	if(!pClientEntity || !pClientEntity->pSaved)
		return;

	iPartitionIdx = entGetPartitionIdx(pClientEntity);

	for(i=0;i<eaSize(&pClientEntity->pSaved->ppOwnedContainers);i++)
	{
		if(pClientEntity->pSaved->ppOwnedContainers[i]->conID == uiPetID)		
		{
			EntityStruct eStruct = {0};
			Entity* pOfflineCopy = savedpet_createOfflineCopy(pClientEntity, (PetRelationship*)pClientEntity->pSaved->ppOwnedContainers[i]);

			eStruct.pEntity = pOfflineCopy;
			if (!eStruct.pEntity) eStruct.pEntity = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET,uiPetID);
			if (!eStruct.pEntity) eStruct.pEntity = GET_REF(pClientEntity->pSaved->ppOwnedContainers[i]->hPetRef);
			ClientCmd_RecieveOfflineCopy(pClientEntity,uiPetID,&eStruct);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(RequestPetDisplayData) ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void savedpet_RequestPetDisplayData(Entity *pClientEnt, U32 iPetID, S32 eOwnerType, U32 iOwnerID, S32 iNumPowers)
{
	int iPartitionIdx = entGetPartitionIdx(pClientEnt);
	Entity *pPetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET, iPetID);
	SavedPetDisplayData* pDisplayData = StructCreate(parse_SavedPetDisplayData);
	char idBuf[128];
	static REF_TO(Entity) hCachedPetRef = {0};
	
	// entFromContainerID failed; attempt to get pet from owned containers array
	if(!pPetEnt)
	{
		Entity* pOwner = entFromContainerID(iPartitionIdx, eOwnerType, iOwnerID);
		if(pOwner && pOwner->pSaved)
		{
			PetRelationship* pPetRel = SavedPet_GetPetFromContainerID(pOwner, iPetID, false);
			if (pPetRel)
			{
				pPetEnt = SavedPet_GetEntity(iPartitionIdx, pPetRel);
			}
		}
	}

	// If still no entity, try copy dictionary
	if(!pPetEnt)
	{
		pPetEnt = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(iPetID, idBuf));
	}

	// If all else fails, cache the handle and wait for the resource to be sent to the copy dictionary
	if(!pPetEnt)
	{
		if(StringToContainerID(REF_STRING_FROM_HANDLE(hCachedPetRef)) != iPetID)
		{
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(iPetID, idBuf), hCachedPetRef);
		}
		pDisplayData->iPetID = 0;
	} else {
		pDisplayData->iPetID = iPetID;
	}

	pDisplayData->bUpdateRequested = false;

	// Copy required power information
	if(pPetEnt && pPetEnt->pChar && pPetEnt->pChar->ppPowerTrees)
	{
		S32 i, j;
		for (i = 0; i < eaSize(&pPetEnt->pChar->ppPowerTrees); i++)
		{
			PowerTree* pPowerTree = pPetEnt->pChar->ppPowerTrees[i];
			S32 iNodeCount = eaSize(&pPowerTree->ppNodes);
			
			if (iNumPowers != -1)
			{
				iNodeCount = MIN(iNodeCount, iNumPowers);
			}
			for (j = 0; j < iNodeCount; j++)
			{
				PTNode *pNode = pPowerTree->ppNodes[j];
				PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
				if (pNodeDef && pNodeDef->ppRanks && !(pNodeDef->eFlag & kNodeFlag_HideNode))
				{
					SavedPetPowerDisplayData* pData = StructCreate(parse_SavedPetPowerDisplayData);
					pData->iRank = pNode->iRank;
					pData->bEscrow = pNode->bEscrow;
					COPY_HANDLE(pData->hNodeDef, pNode->hDef);
					eaPush(&pDisplayData->eaPowerData, pData);
					if (iNumPowers > 0)
					{
						iNumPowers--;
					}
				}
			}
		}
	}
	ClientCmd_ReceivePetDisplayData(pClientEnt, pDisplayData);
	StructDestroy(parse_SavedPetDisplayData, pDisplayData);
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(PetIntroWarp) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gslSavedPet_PetIntroductionWarp(Entity* pEnt, U32 uPetID)
{
	if (pEnt->pSaved && uPetID && pEnt->pSaved->uNewPetID == uPetID)
	{
		PetIntroductionWarp* pWarp = Entity_GetPetIntroductionWarp(pEnt, uPetID);
		const char* pchMapName = zmapInfoGetPublicName(NULL);
		
		if (pWarp && pWarp->pchMapName != pchMapName && Entity_CanUsePetIntroWarp(pEnt, pWarp))
		{
			const char* pchMap = pWarp->pchMapName;
			const char* pchSpawn = pWarp->pchSpawn;
			DoorTransitionSequenceDef* pTransSequence = DoorTransitionSequence_DefFromName(pWarp->pchTransSequence);
			ZoneMapInfo* pNextZoneMapInfo = zmapInfoGetByPublicName(pchMap);
			RegionRules* pCurrRules = getRegionRulesFromEnt(pEnt);
			RegionRules* pNextRules = pNextZoneMapInfo ? getRegionRulesFromZoneMap(pNextZoneMapInfo) : NULL;

			// Handle leaving a queue map
			gslQueue_HandleLeaveMap(pEnt);

			// Do the map transfer
			gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, 
														 pchMap, 
														 ZMTYPE_UNSPECIFIED, 
														 pchSpawn, 
														 0, 0, 0, 0, 0, 
														 NULL, 
														 pCurrRules, 
														 pNextRules, 
														 pTransSequence, 
														 0);
		}
	}
}

static void StompPetContainerID_CB(TransactionReturnVal *pVal, void* userData)
{

}

// THIS WILL BREAK YOUR CHARACTER IF YOU USE THIS. Used for testing a database corruption in which pets could lose their container IDs.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void gslSavedPet_StompPetContainerID(Entity *pEntPlayer, U32 uiPetID)
{
	int i;
	for (i = 0; i < eaSize(&pEntPlayer->pSaved->ppOwnedContainers); i++)
	{
		if (uiPetID == pEntPlayer->pSaved->ppOwnedContainers[i]->uiPetID)
		{
			PetRelationship *pPet = pEntPlayer->pSaved->ppOwnedContainers[i];
			if (pPet)
			{
				U32 uiContainerID = pPet->conID;

				TransactionReturnVal *pVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, StompPetContainerID_CB, (void*)(intptr_t)entGetRef(pEntPlayer));
				AutoTrans_trSavedPet_StompContainerID(pVal, GetAppGlobalType(), GLOBALTYPE_ENTITYSAVEDPET, uiContainerID);
			}
		}
	}
}

static void FixPetContainerID_CB(TransactionReturnVal *pVal, void* userData)
{

}

// THIS WILL BREAK YOUR CHARACTER IF YOU USE THIS INCORRECTLY. This is used to fix a specific database corruption. If you don't know what you are doing, DO NOT USE THIS
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void gslSavedPet_FixPetContainerID(Entity *pEntPlayer, U32 uiContainerID)
{
	TransactionReturnVal *pVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, FixPetContainerID_CB, (void*)(intptr_t)entGetRef(pEntPlayer));
	AutoTrans_trSavedPet_FixPetContainerID(pVal, GetAppGlobalType(), GLOBALTYPE_ENTITYSAVEDPET, uiContainerID, uiContainerID);
}