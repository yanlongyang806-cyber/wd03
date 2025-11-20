/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gameserverlib.h"
#include "Entity.h"
#include "EntityMovementTactical.h"
#include "EntitySavedData.h"
#include "cmdparse.h"
#include "cmdServerCombat.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "Character_combat.h"
#include "CombatConfig.h"
#include "DamageTracker.h"
#include "entCritter.h"
#include "GameAccountDataCommon.h"
#include "Powers.h"
#include "PowerAnimFX.h"
#include "PowerActivation.h"
#include "PowersMovement.h"
#include "PowerModes.h"
#include "PowerTree.h"
#include "PowerTreeTransactions.h"
#include "gslArmamentSwap.h"
#include "gslEntity.h"
#include "gslOldEncounter.h"
#include "gslPetCommand.h"
#include "gslPowerTransactions.h"
#include "gslSpawnPoint.h"
#include "gslSuperCritterPet.h"
#include "mission_common.h"
#include "gslCommandParse.h"
#include "gslMission.h"
#include "reward.h"
#include "player.h"
#include "WorldGrid.h"
#include "aiLib.h"
#include "aiTeam.h"

#include "cmdServerCombat_c_ast.h"
#include "Powers_h_ast.h"
#include "ResourceInfo.h"

AUTO_STRUCT;
typedef struct NNODefAndTime
{
	const char *pchName;	AST(STRUCTPARAM)
	F32 fActivateTime;		AST(STRUCTPARAM)
	F32 fAnimTime;			AST(STRUCTPARAM)
	F32 fRatio;				AST(STRUCTPARAM)
}NNODefAndTime;

AUTO_STRUCT;
typedef struct NNOFixupList
{
	NNODefAndTime **ppDefs;
}NNOFixupList;


AUTO_COMMAND ACMD_CATEGORY(Debug, Powers);
void NNOUnFuckery()
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("PowerDef");
	NNOFixupList *pList = StructCreate(parse_NNOFixupList);
	int i;

	for(i=0;i<eaSize(&pArray->ppReferents);i++)
	{
		PowerDef *pPower = (PowerDef*)pArray->ppReferents[i];
		F32 fTime = pPower->fTimePreactivate + pPower->fTimeActivate;
		PowerAnimFX *pFX = GET_REF(pPower->hFX);
		F32 fAnimTime = pFX && pFX->piFramesBeforeHit ? pFX->piFramesBeforeHit[0] / 20.0f : 0.0f;
		F32 fRation = fAnimTime ? fTime / fAnimTime : 0.0f;

		if(fAnimTime > fTime)
		{
			NNODefAndTime *pNew = StructCreate(parse_NNODefAndTime);

			pNew->pchName = pPower->pchName;
			pNew->fActivateTime = fTime;
			pNew->fAnimTime = fAnimTime;
			pNew->fRatio = fRation;

			eaPush(&pList->ppDefs,pNew);
		}
	}

	ParserWriteTextFile("c:\\temp\\PowerOutput.txt",parse_NNOFixupList,pList,0,0);
}


// Power_Add <PowerName>: Adds the power of the given name to the player, outside of the scope of a tree
AUTO_COMMAND ACMD_NAME(Power_Add, Add_Power) ACMD_CATEGORY(Debug, Powers, csr) ACMD_ACCESSLEVEL(7);
void Power_Add(Entity *clientEntity, ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *pchName)
{
	if(clientEntity && clientEntity->pChar)
	{
		PowerDef *pdef = powerdef_Find(pchName);
		if(pdef)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(clientEntity);
			int iPartitionIdx = entGetPartitionIdx(clientEntity);

			character_AddPowerPersonal(iPartitionIdx, clientEntity->pChar,pdef,0,false, pExtract);
		}
	}
}

// Power_Add_FromTreeRank <PowerTreeName> <Rank>: Adds powers from a power tree at the specific rank, outside of the scope of a tree. Tree "all" adds all trees. Rank 0 adds all ranks.
AUTO_COMMAND ACMD_CATEGORY(Debug, Powers);
void Power_Add_FromTreeRank(Entity *clientEntity, ACMD_NAMELIST("PowerTreeDef", REFDICTIONARY) char *pchName, int iRank)
{
	if(clientEntity && clientEntity->pChar && pchName)
	{
		if(!stricmp(pchName,"all"))
		{
			RefDictIterator iter;
			PowerTreeDef *pdefTree;
			RefSystem_InitRefDictIterator(g_hPowerTreeDefDict, &iter);
			while(pdefTree = (PowerTreeDef*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				Power_Add_FromTreeRank(clientEntity,pdefTree->pchName,iRank);
			}
		}
		else
		{
			PowerTreeDef *pdefTree = powertreedef_Find(pchName);
			if(pdefTree)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(clientEntity);
				int iPartitionIdx = entGetPartitionIdx(clientEntity);
				int i,j,k;
				for(i=0; i<eaSize(&pdefTree->ppGroups); i++)
				{
					for(j=0; j<eaSize(&pdefTree->ppGroups[i]->ppNodes); j++)
					{
						for(k=0; k<eaSize(&pdefTree->ppGroups[i]->ppNodes[j]->ppRanks); k++)
						{
							if(!iRank || iRank==(k+1))
							{
								PowerDef *pdef = GET_REF(pdefTree->ppGroups[i]->ppNodes[j]->ppRanks[k]->hPowerDef);
								if(pdef)
								{
									character_AddPowerPersonal(iPartitionIdx, clientEntity->pChar,pdef,0,false,pExtract);
								}
							}
						}
					}
				}
			}
		}
	}
}

// Power_Add_FromTree <PowerTreeName>: Adds all powers from a power tree, outside of the scope of a tree.  Tree "all" will add all trees.
AUTO_COMMAND ACMD_CATEGORY(Debug, Powers);
void Power_Add_FromTree(Entity *clientEntity, ACMD_NAMELIST("PowerTreeDef", REFDICTIONARY) char *pchName)
{
	Power_Add_FromTreeRank(clientEntity, pchName, 0);
}

// Power_Add_FromCritter <CritterName>: Adds all powers from a critter, outside of the scope of a tree
AUTO_COMMAND ACMD_CATEGORY(Debug, Powers);
void Power_Add_FromCritter(Entity *clientEntity, ACMD_NAMELIST("CritterDef", REFDICTIONARY) char *pchName)
{
	if(clientEntity && clientEntity->pChar && pchName)
	{
		CritterDef *pdefCritter = critter_DefGetByName(pchName);
		if(pdefCritter)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(clientEntity);
			int iPartitionIdx = entGetPartitionIdx(clientEntity);
			int i;
			for(i=0; i<eaSize(&pdefCritter->ppPowerConfigs); i++)
			{
				PowerDef *pdef = GET_REF(pdefCritter->ppPowerConfigs[i]->hPower);

				if(pdefCritter->ppPowerConfigs[i]->bDisabled)
					continue;
				
				if(pdef)
				{
					character_AddPowerPersonal(iPartitionIdx, clientEntity->pChar,pdef,0,false,pExtract);
				}
			}
		}
	}
}

// PowerTree_AddWithoutRules <PowerTreeName> <Rank> <EnhRank>: Purchases the PowerTree and given rank of all nodes
//  and enhancements, ignoring all purchasing rules, including cost.  This command will fail on a live server.
AUTO_COMMAND ACMD_CATEGORY(Debug, Powers);
void PowerTree_AddWithoutRules(Entity *clientEntity, ACMD_NAMELIST("PowerTreeDef", REFDICTIONARY) char *pchName, int iRank, int iRankEnh)
{
	entity_PowerTreeAddWithoutRules(clientEntity, pchName, iRank, iRankEnh);
}


// Power_Remove <PowerName>: Removes the power of the given name from the player, if that power is outside the scope of a tree
AUTO_COMMAND ACMD_NAME(Power_Remove, Remove_Power) ACMD_CATEGORY(Debug, Powers) ACMD_ACCESSLEVEL(7);
void Power_Remove(Entity *clientEntity, ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *pchName)
{
	if(clientEntity && clientEntity->pChar && pchName)
	{
		Power *ppow = character_FindPowerByNamePersonal(clientEntity->pChar,pchName);
		if(ppow)
		{
			character_RemovePowerPersonal(clientEntity->pChar,ppow->uiID);
		}
	}
}

// Refill character hp/end to max
AUTO_COMMAND ACMD_ACCESSLEVEL(5);
void Refill_HP_POW(Entity *e)
{
	if(e && e->pChar)
	{
		e->pChar->pattrBasic->fHitPoints = e->pChar->pattrBasic->fHitPointsMax;
		e->pChar->pattrBasic->fPower = e->pChar->pattrBasic->fPowerMax;
		character_DirtyAttribs(e->pChar);
	}
}

AUTO_COMMAND;
void Drain_HP_POW(Entity *e)
{
	if (e && e->pChar)
	{
		e->pChar->pattrBasic->fHitPoints = e->pChar->pattrBasic->fHitPointsMax / 20.0;
		e->pChar->pattrBasic->fPower = e->pChar->pattrBasic->fPowerMax / 20.0;
		character_DirtyAttribs(e->pChar);
	}
}

// BattleForm <0/1>: Disable/Enable BattleForm
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void BattleForm(Entity *e, int enable)
{
	if(e && e->pChar) 
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_SetBattleForm(entGetPartitionIdx(e), e->pChar,enable,false,true,pExtract);
	}
}

// BattleFormForce <0/1>: Force Disable/Enable BattleForm
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Powers,Debug);
void BattleFormForce(Entity *e, int enable)
{
	if(e && e->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_SetBattleForm(entGetPartitionIdx(e), e->pChar,enable,true,false,pExtract);
	}
}

//If bForceRespawn is set, then ignore the player's respawn timer
//If bForceIfNotDead is set, then run all respawn code even if the player is alive
bool gslPlayerRespawn(Entity* pPlayerEnt, bool bForceRespawn, bool bForceIfNotDead)
{
	bool bApplyDeathPenalty = true;

	if (!pPlayerEnt || !pPlayerEnt->pPlayer || ((entIsAlive(pPlayerEnt) && !pPlayerEnt->pChar->pNearDeath) && !bForceIfNotDead))
		return false;

	//if the respawn timer hasn't expired yet and this isn't a forced respawn, don't respawn
	if (!bForceRespawn && 
		(!pPlayerEnt->pPlayer->uiRespawnTime || timeSecondsSince2000() < pPlayerEnt->pPlayer->uiRespawnTime || pPlayerEnt->pPlayer->bDisableRespawn))
		return false;

	if (pPlayerEnt->pChar && pPlayerEnt->pChar->pNearDeath)
	{
		character_TriggerCombatNearDeathDeadEvent(entGetPartitionIdx(pPlayerEnt), pPlayerEnt->pChar);
	}

	if(zmapInfoGetRespawnType(NULL) == ZoneRespawnType_NearTeam)
	{
		spawnpoint_MovePlayerToSpawnPointNearTeam(pPlayerEnt, true, true);
	}
	else
	{
		spawnpoint_MovePlayerToNearestSpawn(pPlayerEnt, true, true);
	}
	
	// reset the player's respawn time
	pPlayerEnt->pPlayer->uiRespawnTime = 0;
	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	
	if (pPlayerEnt->pChar)
	{
		F32 fPowerEquilibrium = pPlayerEnt->pChar->pattrBasic->fPowerEquilibrium;
//			Entity* killer = damageTracker_GetHighestDamagerEntity(pPlayerEnt->pChar);
//			Entity* killingBlow = character_FindKiller(pPlayerEnt->pChar,NULL);

		// Don't apply the death penalty if a player or PvP flagged critter did most of the
		// damage or landed the killing blow or this is a PvP map and DP is disabled.
		if((zmapInfoGetMapType(NULL) == ZMTYPE_PVP && gConf.bDisableDeathPenaltyOnPvPMaps)
			||	damageTracker_HasBeenDamagedInPvP(entGetPartitionIdx(pPlayerEnt), pPlayerEnt->pChar))
		{
			bApplyDeathPenalty = false;
		}
		
		/*
		// 			if(killer != pPlayerEnt && entIsPvPFlagged(killer))
		// 				bApplyDeathPenalty = false;
		// 			else if(killingBlow != pPlayerEnt && entIsPvPFlagged(killingBlow))
		// 				bApplyDeathPenalty = false;
		*/

		// Reset the player's status
		pPlayerEnt->pChar->pattrBasic->fHitPoints = pPlayerEnt->pChar->pattrBasic->fHitPointsMax;

		if (!g_CombatConfig.bPowerAttribSurvivesCharDeath)
			pPlayerEnt->pChar->pattrBasic->fPower = fPowerEquilibrium;

		character_DirtyAttribs(pPlayerEnt->pChar);
		character_AttribPoolRespawn(pPlayerEnt->pChar);
		if (pPlayerEnt->pChar->pNearDeath)
			character_NearDeathRevive(pPlayerEnt->pChar);

		damageTracker_ClearAll(pPlayerEnt->pChar);
				
		character_Wake(pPlayerEnt->pChar);

		// Reset the pet's status
		if(pPlayerEnt->pSaved)
		{
			PetCommands_RespawnPets(pPlayerEnt);
		}
		//Super Critter Pets
		gslSCPPlayerRespawn(pPlayerEnt);
		
		if (pPlayerEnt->aibase)
		{
			AITeam *pAITeam;

			aiResetForRespawn(pPlayerEnt);

			// Clear the team status for the player
			pAITeam = aiTeamGetCombatTeam(pPlayerEnt, pPlayerEnt->aibase);
			if (pAITeam)
			{
				aiTeamClearStatusTable(pAITeam, "Respawn");
			}
		}

		// Exit BattleForm if it's optional on this map
		if(g_CombatConfig.pBattleForm && combatconfig_BattleFormOptional())
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
			character_SetBattleForm(entGetPartitionIdx(pPlayerEnt),pPlayerEnt->pChar,false,true,false,pExtract);
		}

		if (g_CombatConfig.iForceBuildOnRespawn != -1)
		{
			entity_BuildSetCurrentEx(pPlayerEnt, g_CombatConfig.iForceBuildOnRespawn, false, true);
		}
		
		gslArmamentSwapOnDeath(pPlayerEnt);
		// as a fail-safe, tell the tactical requester that we're clearing all the disables
		mrTacticalNotifyClearAllDisables(pPlayerEnt->mm.mrTactical, pmTimestamp(0));
	}

	if(g_RewardConfig.bNoDeathPenaltyInPowerHouse)
	{
		// is this a powerhouse?
		const char *pcCurrentMap = zmapInfoGetPublicName(NULL);
		ZoneMapInfo* pCurrZoneMap = worldGetZoneMapByPublicName(pcCurrentMap);

		if(pCurrZoneMap && zmapInfoConfirmPurchasesOnExit(pCurrZoneMap))
		{
			bApplyDeathPenalty = false;
		}
	}

	//execute the death penalty reward table on the player
	if(bApplyDeathPenalty)
	{
		reward_RespawnPenaltyExec(pPlayerEnt);
	}

	return true;
}

// Respawns a player if all the conditions needed for respawn are met
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void PlayerRespawn(Entity* playerEnt)
{
	gslPlayerRespawn(playerEnt, false, false);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void PlayerExpireNearDeath(Entity* playerEnt)
{
	if (playerEnt && playerEnt->pChar && playerEnt->pChar->pNearDeath)
	{
		playerEnt->pChar->bKill = true;
		character_Wake(playerEnt->pChar);
	}
}

// "Recovers" a dead player if all the conditions are met (player gets back up with a debuff)
// Uses a power named "Recover" if one exists.
/* Design has decided to cut this feature. */
//AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
//void PlayerRecover(Entity* playerEnt)
//{
	/*
	if (playerEnt && entCheckFlag(playerEnt, ENTITYFLAG_DEAD) && playerEnt->pChar)
	{
		PowerDef* pRecoveryPowerDef = (PowerDef*)RefSystem_ReferentFromString(g_hPowerDefDict, "Recover");

		if(!character_HasMode(playerEnt->pChar, kPowerMode_NoRecovery) && pRecoveryPowerDef)
		{
			Power_Use(playerEnt, pRecoveryPowerDef, 0, playerEnt->pChar->iLevelCombat);
		}
	}
	*/
//}

// Activate a Power
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void entUsePowerServer(Entity* e, PowerActivationRequest *pAct)
{
	if(e && e->pChar && pAct)
	{
		GameAccountDataExtract *pExtract;
		int iPartitionIdx;

		if(e->pPlayer && (e->pPlayer->iStasis || e->pPlayer->bStuckRespawn || e->pPlayer->bIgnoreClientPowerActivations))
		{
			// Stasis/stuck players can't activate powers.
			return;
		}

		poweractreq_FixCmdRecv(pAct);

		pExtract = entity_GetCachedGameAccountDataExtract(e);
		iPartitionIdx = entGetPartitionIdx(e);

		if(pAct->bPrimaryPet)
		{
			Entity *ePet = entGetPrimaryPet(e);
			if (ePet && ePet->pChar)
			{
				character_ActivatePowerServer(iPartitionIdx, ePet->pChar,pAct,true,pExtract);
			}
		}
		else
		{	
			character_ActivatePowerServer(iPartitionIdx, e->pChar,pAct,true,pExtract);
		}
	}
}

// Activate a Power without using prediction
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void entUsePowerServerUnpredicted(Entity *e, U32 uiID, EntityRef erTarget, S32 bStart)
{
	if(e && e->pChar)
	{
		Power *ppow = character_FindPowerByID(e->pChar,uiID);
		Entity *eTarget = entFromEntityRef(entGetPartitionIdx(e), erTarget);
		if(ppow)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);

			character_ActivatePowerServerBasic(entGetPartitionIdx(e), e->pChar,ppow,eTarget,NULL,bStart,false,pExtract);
		}
	}
}

// Notifies the server that you've committed to a power activation
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void MarkActCommitted(Entity* e, U32 uiActID, U32 uiSeq, U32 uiSeqReset)
{
	if(e && e->pChar && (U8)uiActID)
	{
		character_MarkActCommitted(e->pChar,(U8)uiActID,uiSeq,uiSeqReset);
	}
}

// Notifies the server that you've committed to a power activation
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void MarkActUpdateTarget(Entity* e, U32 uiActID, U32 uiSeq, U32 uiSeqReset, Vec3 vVecTargetUpdate, EntityRef erTarget)
{
	if(e && e->pChar && (U8)uiActID)
	{
		character_UpdateVecTarget(entGetPartitionIdx(e),e->pChar,(U8)uiActID,vVecTargetUpdate,erTarget);
	}
}

// Notifies the server that you've turned AutoAttack on or off
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void SetAutoAttackServer(Entity *e, S32 enabled)
{
	if(e && e->pChar)
	{
		e->pChar->bAutoAttackServer = !!enabled;
		if(e->pChar->bAutoAttackServer)
			e->pChar->bAutoAttackServerCheck = true;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void DismountServer(Entity *e)
{
	if (e && e->pChar && g_CombatConfig.iMountPowerCategory)
	{
		if (e->pChar->ppPowerActToggle)
		{
			FOR_EACH_IN_EARRAY(e->pChar->ppPowerActToggle, PowerActivation, pAct)
			{
				PowerDef *pDef = GET_REF(pAct->hdef);
				if (pDef && eaiFind(&pDef->piCategories, g_CombatConfig.iMountPowerCategory) >= 0)
				{
					character_DeactivateToggle(entGetPartitionIdx(e), e->pChar, pAct, NULL, pmTimestamp(0), true);
				}
			}
			FOR_EACH_END
		}
	}
}

#include "cmdServerCombat_c_ast.c"