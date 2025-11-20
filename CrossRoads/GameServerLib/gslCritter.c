/***************************************************************************
*     Copyright (c) 2003-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AnimList_Common.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Character_mods.h"
#include "Character_target.h"
#include "EString.h"
#include "Entity.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonRandom.h"
#include "CostumeCommonTailor.h"
#include "gslCostume.h"
#include "EntityExtern.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "gslPartition.h"
#include "ItemArt.h"
#include "Player.h"
#include "Powers.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "SharedMemory.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringCache.h"
#include "aiAnimList.h"
#include "aiLib.h"
#include "aiExtern.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "aiStructCommon.h"
#include "dynFxInfo.h"
#include "earray.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "EntityMovementDragon.h"
#include "EntityMovementDefault.h"
#include "error.h"
#include "expression.h"
#include "fileutil.h"
#include "gslCritter.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslPowerTransactions.h"
#include "oldencounter_common.h"
#include "rand.h"
#include "textparser.h"
#include "utils.h"
#include "wlCostume.h"
#include "CharacterAttribs.h"
#include "entCritter.h"
#include "timing.h"
#include "WorldGrid.h"
#include "wlEncounter.h"
#include "logging.h"
#include "nemesis.h"
#include "nemesis_common.h"
#include "species_common.h"
#include "mission_common.h"
#include "PowerHelpers.h"

#include "itemCommon.h"
#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "CombatConfig.h"
#include "mapstate_common.h"

#include "AutoGen/Character_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/entEnums_h_ast.h"
#include "AutoGen/Expression_h_ast.h"	// For expression parse struct
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

static void critter_ApplyOverrideDef(Entity* e, Critter *pCritter, CritterOverrideDef *pDef);

static void critter_AddCostume( Entity *be, CritterDef *pDef, int iLevel, S32 iTeamSize, int iCostumeKey, const Message *displayNameMsgOverride, const Message *displaySubNameMsgOverride)
{
	F32 fWeightTotal = 0, fRand, fRunningTotal=0;
	int i;
	PlayerCostume *pCostume = NULL;


	if ( !pDef )
		 return;

	PERFINFO_AUTO_START_FUNC();

	entity_SetDirtyBit(be, parse_Critter, be->pCritter, false);

	//start out with the base display name message
	COPY_HANDLE(be->pCritter->hDisplayNameMsg, pDef->displayNameMsg.hMessage);
	COPY_HANDLE(be->pCritter->hDisplaySubNameMsg, pDef->displaySubNameMsg.hMessage);

	//if an override display name was passed in then set it
	if (displayNameMsgOverride)
		SET_HANDLE_FROM_REFERENT(gMessageDict, (Message *)displayNameMsgOverride, be->pCritter->hDisplayNameMsg);

	if (displaySubNameMsgOverride)
		SET_HANDLE_FROM_REFERENT(gMessageDict, (Message *)displaySubNameMsgOverride, be->pCritter->hDisplaySubNameMsg);

	pCostume = GET_REF(pDef->hOverrideCostumeRef);
	if ( pCostume  )
	{
		if (displayNameMsgOverride || displaySubNameMsgOverride)
		{
			costumeEntity_ApplyCritterInfoToCostume(CONTAINER_NOCONST(Entity, be), CONTAINER_NOCONST(PlayerCostume, pCostume), true);
		}
		else
		{
			costumeEntity_SetCostume(be, pCostume, false);
		}
	}
	else if (pDef->bGenerateRandomCostume && GET_REF(pDef->hSpecies))
	{
		SpeciesDef *pSpecies = GET_REF(pDef->hSpecies);
		NOCONST(PlayerCostume) *pCostumeTemp = NULL;
		pCostumeTemp = StructCreateNoConst(parse_PlayerCostume);
		pCostumeTemp->eGender = pSpecies->eGender;
		COPY_HANDLE(pCostumeTemp->hSkeleton, pSpecies->hSkeleton);
		pCostumeTemp->eCostumeType = kPCCostumeType_NPC;

		costumeRandom_FillRandom(pCostumeTemp, pSpecies, NULL, NULL, NULL, NULL, NULL, true, true, false, false, true, true, true);
		costumeTailor_StripUnnecessary(pCostumeTemp);

		costumeEntity_ApplyCritterInfoToCostume(CONTAINER_NOCONST(Entity, be), pCostumeTemp, false);
		StructDestroyNoConst(parse_PlayerCostume,pCostumeTemp);
	}
	else if ( pDef->ppCostume )
	{
		int iCostumeIndex = iCostumeKey ? eaIndexedFindUsingInt(&pDef->ppCostume,iCostumeKey) : -1;

		for(i=0;i<eaSize(&pDef->ppCostume);i++)
		{
			if ( ( (pDef->ppCostume[i]->iMinLevel == -1) || (iLevel >= pDef->ppCostume[i]->iMinLevel) ) &&
				 ( (pDef->ppCostume[i]->iMaxLevel == -1) || (iLevel <= pDef->ppCostume[i]->iMaxLevel) ) &&
				 ( (pDef->ppCostume[i]->iMinTeamSize == -1) || (iTeamSize >= pDef->ppCostume[i]->iMinTeamSize) ) &&
				 ( (pDef->ppCostume[i]->iMaxTeamSize == -1) || (iTeamSize <= pDef->ppCostume[i]->iMaxTeamSize) ))
			{
				fWeightTotal += pDef->ppCostume[i]->fWeight;
			}
		}

		if (fWeightTotal == 0)
		{
			//no costume for this level range, just force it to the first in the list
			Errorf("No Costume for Level %d on Critter %s, using Corey", iLevel, pDef->pchName );

			//use Corey Costume
			{
				PlayerCostume *pCoreyCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, "Corey");

				if (pCoreyCostume)
				{
					if (displayNameMsgOverride || displaySubNameMsgOverride)
					{
						costumeEntity_ApplyCritterInfoToCostume(CONTAINER_NOCONST(Entity, be), CONTAINER_NOCONST(PlayerCostume, pCoreyCostume), true);
					}
					else
					{
						costumeEntity_SetCostume(be, pCoreyCostume, false);
					}
				}
			}
		}
		else
		{
			fRand = randomMersennePositiveF32(NULL)*fWeightTotal;

			for(i=0; i<eaSize(&pDef->ppCostume); i++)
			{
				if(iCostumeIndex<0)
				{
					if ( ( (pDef->ppCostume[i]->iMinLevel != -1) && (iLevel < pDef->ppCostume[i]->iMinLevel) ) ||
						( (pDef->ppCostume[i]->iMaxLevel != -1) && (iLevel > pDef->ppCostume[i]->iMaxLevel) ) ||
						( (pDef->ppCostume[i]->iMinTeamSize != -1) && (iTeamSize < pDef->ppCostume[i]->iMinTeamSize) ) ||
						( (pDef->ppCostume[i]->iMaxTeamSize != -1) && (iTeamSize > pDef->ppCostume[i]->iMaxTeamSize) ))
					{
						continue;
					}

					if(!pDef->ppCostume[i]->fWeight)
						continue;
				}

				if( (iCostumeIndex>=0 && i==iCostumeIndex)
					|| (iCostumeIndex<0 && fRand>=fRunningTotal && fRand <= fRunningTotal+pDef->ppCostume[i]->fWeight ))
				{
					// does the critter costume specify a voice set?
					if(pDef->ppCostume[i]->voiceSet)
					{
						be->pCritter->voiceSet = pDef->ppCostume[i]->voiceSet;
					}
					else
					{
						// no, so check the group
						CritterGroup *critterGroup = GET_REF(pDef->hGroup);
						if(critterGroup)
						{
							Gender gender = be->eGender;
							switch(gender)
							{
								case Gender_Male:
									be->pCritter->voiceSet = critterGroup->maleVoiceSet;
									break;
								case Gender_Female:
									be->pCritter->voiceSet = critterGroup->femaleVoiceSet;
									break;
								default:
									be->pCritter->voiceSet = critterGroup->neutralVoiceSet;
									break;
							}
						}
					}

					if (!pDef->ppCostume[i]->bCreateIgnoresDisplayName && IS_HANDLE_ACTIVE(pDef->ppCostume[i]->displayNameMsg.hMessage) && !displayNameMsgOverride)
						COPY_HANDLE(be->pCritter->hDisplayNameMsg, pDef->ppCostume[i]->displayNameMsg.hMessage);

					if (IS_HANDLE_ACTIVE(pDef->ppCostume[i]->displaySubNameMsg.hMessage) && !displaySubNameMsgOverride)
						COPY_HANDLE(be->pCritter->hDisplaySubNameMsg, pDef->ppCostume[i]->displaySubNameMsg.hMessage);

					if (displayNameMsgOverride || displaySubNameMsgOverride)
					{
						costumeEntity_ApplyCritterInfoToCostume(CONTAINER_NOCONST(Entity, be), CONTAINER_NOCONST(PlayerCostume, GET_REF(pDef->ppCostume[i]->hCostumeRef)), true);
					}
					else
					{
						costumeEntity_SetCostume(be, GET_REF(pDef->ppCostume[i]->hCostumeRef), false);
					}

					break;
				}

				fRunningTotal += pDef->ppCostume[i]->fWeight;
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void critter_OverrideDisplayMessage( Entity *be, const Message *displayNameMsgOverride, const Message *displaySubNameMsgOverride)
{
	if (displayNameMsgOverride)
	{
		entity_SetDirtyBit(be, parse_Critter, be->pCritter, false);
		SET_HANDLE_FROM_REFERENT(gMessageDict, (Message*)displayNameMsgOverride, be->pCritter->hDisplayNameMsg);
	}
	if (displaySubNameMsgOverride)
	{
		entity_SetDirtyBit(be, parse_Critter, be->pCritter, false);
		SET_HANDLE_FROM_REFERENT(gMessageDict, (Message*)displaySubNameMsgOverride, be->pCritter->hDisplaySubNameMsg);
	}
}

static __forceinline void addItemDefDefToListMaybe( DefaultItemDef *** list, DefaultItemDef * pDid )
{
	F32 fChance = 0.f;

	if( pDid->fChance )
		fChance = randomMersennePositiveF32(NULL);

	if( fChance <= pDid->fChance )
	{
		eaPush(list, pDid);
	}
}

static __forceinline void addItemsFromDefaultItemList(CritterDef *pDef, DefaultItemDef ***list)
{
	int i,j;
	static CritterItemConfigList **weighted_list=0;
	F32 fRand = 0, fWeightTotal = 0, fWeightCumm = 0;

	PERFINFO_AUTO_START_FUNC();

	// first gather groups
	for(i=0; i<eaSize(&pDef->ppCritterItems); i++)
	{
		int added = 0;

		if(pDef->ppCritterItems[i]->bDisabled)
			continue;

		for(j=eaSize(&weighted_list)-1; j>=0; j--)
		{
			if( pDef->ppCritterItems[i]->iGroup == weighted_list[j]->list[0]->iGroup )
			{
				eaPush( &weighted_list[j]->list, pDef->ppCritterItems[i] );
				added = 1;
				break;
			}
		}

		if( !added ) // new group
		{
			CritterItemConfigList * cl = calloc(1, sizeof(CritterItemConfigList));
			eaPush(&cl->list, pDef->ppCritterItems[i]);
			eaPush(&weighted_list, cl);
			fWeightTotal += pDef->ppCritterItems[i]->fWeight;
		}
	}


	// loop over and give everything in non-weighted group
	fRand = randomMersennePositiveF32(NULL)*fWeightTotal;
	for(i=eaSize(&weighted_list)-1; i>=0; i--)
	{
		if( !weighted_list[i]->list[0]->fWeight )
		{
			for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
			{
				addItemDefDefToListMaybe(list, weighted_list[i]->list[j]);
			}
		}
		else
		{
			if(fRand >= fWeightCumm && fRand <= fWeightCumm+weighted_list[i]->list[0]->fWeight )
			{
				for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
				{
					addItemDefDefToListMaybe(list, weighted_list[i]->list[j]);
				}
			}
			if(weighted_list[i])
				fWeightCumm += weighted_list[i]->list[0]->fWeight;
		}
	}

	for(i=eaSize(&weighted_list)-1; i>=0; i--)
		eaDestroy(&weighted_list[i]->list);
	eaClearEx(&weighted_list,0);
	PERFINFO_AUTO_STOP();

}

static void gslCritterInitialize(Entity *be, CritterDef* def, const CritterCreateParams *pCreateParams)
{
	NOCONST(Entity) *pNoConst = CONTAINER_NOCONST(Entity, be);
	PERFINFO_AUTO_START_FUNC();

	if (pCreateParams->pCreatorNode)
	{
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pCreateParams->pCreatorNode, be->hCreatorNode);
	}

	if (pCreateParams->pFaction)
	{
		SET_HANDLE_FROM_STRING(g_hCritterFactionDict, pCreateParams->pFaction->pchName, pNoConst->hFaction);
	}
	else
	{
		COPY_HANDLE(pNoConst->hFaction, FactionDefaults->hDefaultEnemyFaction);
	}
	
	// check if this critter requires a special movement requester
	if (!be->bFakeEntity && IS_HANDLE_ACTIVE(def->hOverrideMovementRequesterDef))
	{
		MovementRequesterDef *pMovementDef = GET_REF(def->hOverrideMovementRequesterDef);
		// destroy the default surface requester and create the necessary requester
		// right now we're just assuming it is the dragon requester
		if (pMovementDef && pMovementDef->pParams)
		{
			if (pMovementDef->pParams->eType == MovementRequesterType_DRAGON)
			{
				if (mmRequesterCreateBasic(be->mm.movement, &be->mm.mrDragon, mrDragonMsgHandler))
				{
					DragonMovementDef *pDragonDef = (DragonMovementDef*)pMovementDef->pParams;
					mrDragon_InitializeSettings(be->mm.mrDragon, pDragonDef);
					mrDestroy(&be->mm.mrSurface);
				}
			}
			else if (pMovementDef->pParams->eType == MovementRequesterType_DEFAULTSURFACE)
			{
				SurfaceMovementDef *pSurfaceDef = (SurfaceMovementDef*)pMovementDef->pParams;
				if (pSurfaceDef->pTurn)
				{
					mrSurfaceSetTurnParameters(be->mm.mrSurface, pSurfaceDef->pTurn);
				}
			}
		}
	}
	
	if (!be->bFakeEntity)
	{
		gslEntity_UpdateMovementMangerFaction(pCreateParams->iPartitionIdx, be);
	}
	
	if (!be->bFakeEntity)
	{
		AIInitParams aiInitParams = {0};

		aiInitParams.fsmOverride = pCreateParams->fsmOverride;
		aiInitParams.fSpawnLockdownTime = pCreateParams->fSpawnTime;
		aiInitParams.pCombatRoleDef = pCreateParams->pCombatRolesDef;
		aiInitParams.pchCombatRoleName = pCreateParams->pcCombatRoleName;
		aiInit(be, def, &aiInitParams);
	}

	critter_AddCombat( be, def, pCreateParams->iLevel, pCreateParams->iTeamSize, pCreateParams->pcSubRank, 
						pCreateParams->fRandom, true, true, NULL, pCreateParams->bPowersEntCreated);

	critter_AddCostume(be, def, pCreateParams->iLevel, pCreateParams->iTeamSize, 
						pCreateParams->iCostumeKey, pCreateParams->pDisplayNameMsg, pCreateParams->pDisplaySubNameMsg );

	if (!be->bFakeEntity && def->bRandomCivilianName && !pCreateParams->pDisplayNameMsg)
	{
		aiCivGiveCritterRandomName(be, def);
	}

	if (!be->bFakeEntity && pCreateParams->iPartitionIdx != PARTITION_IN_TRANSACTION) 
	{
		aiInitTeam(be, pCreateParams->aiTeam);
		if(pCreateParams->aiCombatTeam)
			aiInitTeam(be, pCreateParams->aiCombatTeam);
	}

	if(be->pChar)
	{
		if (pCreateParams->pCostume)
		{
			COPY_HANDLE(pNoConst->pChar->hSpecies, pCreateParams->pCostume->hSpecies);
		}

		if (!be->bFakeEntity)
		{
			if(!GET_REF(be->pChar->hPowerDefStanceDefault))
			{
				character_SetDefaultStance(entGetPartitionIdx(be),be->pChar,GET_REF(def->hDefaultStanceDef));
			}

			if(be->pChar->pattrBasic)
			{
				eventsend_RecordNewHealthState(be, be->pChar->pattrBasic->fHitPointsMax);
			}
		}
	}

	//add interaction info
	if(be->pCritter && def) {
		if(!be->pCritter->uInteractDist && def->uInteractRange)
			be->pCritter->uInteractDist = def->uInteractRange;

		if(GET_REF(def->hInteractionDef) || critterdef_HasOldInteractProps(def)) {
			be->pCritter->bIsInteractable = 1;
		}
	}

	//add info needed by client UI
	if(be->pCritter)
	{
		CritterGroup *pGroup = GET_REF(def->hGroup);
		entity_SetDirtyBit(be, parse_Critter, be->pCritter, false);
		if(def && GET_REF(def->hGroupOverrideDisplayNameMsg.hMessage))
		{
			COPY_HANDLE(be->pCritter->hGroupDisplayNameMsg, def->hGroupOverrideDisplayNameMsg.hMessage);
		}
		else if ( pGroup )
		{
			COPY_HANDLE(be->pCritter->hGroupDisplayNameMsg, pGroup->displayNameMsg.hMessage);
		}
		if (pGroup)
		{
			be->pCritter->pcGroupIcon = pGroup->pcIcon;
		}	
	}

	if(def && be->pCritter)
	{
		be->pCritter->bPseudoPlayer = !!def->bPseudoPlayer;
				
		if (def->ppchStanceWords)
			eaCopy(&be->pCritter->ppchStanceWords, &def->ppchStanceWords);

		// The nemesis costume set
		if(pCreateParams->pCostumeSet)
		{
			be->pCritter->pcNemesisMinionCostumeSet = pCreateParams->pCostumeSet->pcName;
		}
	}

	PERFINFO_AUTO_STOP();
}


CritterDef* critter_GetNemesisCritter(Entity *pNemesisEnt)
{
	if(pNemesisEnt)
	{
		NemesisPowerSet *pPowerSet;
		CritterDef *pCritterDef;

		pPowerSet = nemesis_NemesisPowerSetFromName(pNemesisEnt->pNemesis->pchPowerSet);
		if(!pPowerSet)
			return NULL;

		pCritterDef = RefSystem_ReferentFromString(g_hCritterDefDict, pPowerSet->pcCritter);

		return pCritterDef;
	}

	return NULL;
}

CritterDef* critter_GetNemesisCritterAndSetParams(int iPartitionIdx, Entity *pNemesisEnt, CritterCreateParams *createParams, bool bUseLeader, S32 iTeamIndex)
{
	CritterDef *pCritterDef = critter_GetNemesisCritter(pNemesisEnt);

	if(!pCritterDef)
	{
		// get information from mapstate about this saved info for this nemesis
		const NemesisTeamStruct *pNemTeam = Nemesis_GetTeamStructAtIndex(iPartitionIdx, iTeamIndex, bUseLeader);
		pCritterDef = Nemesis_GetCritterDefAtIndex(iPartitionIdx, iTeamIndex, bUseLeader);
		if(pNemTeam)
		{
			if(pNemTeam->pNemesisCostume)
			{
				createParams->pCostume = pNemTeam->pNemesisCostume;
			}
		}
	}
	else if(pNemesisEnt)
	{
		createParams->pCostume = costumeEntity_GetSavedCostume(pNemesisEnt, 0);
		createParams->fHue = pNemesisEnt->pNemesis->fPowerHue;
	}

	return pCritterDef;
}

void critter_SetupNemesisEntity(Entity* pEnt, Entity *pNemesisEnt, bool bOverrideDisplayName, S32 iPartitionIdx, S32 iTeamIndex, bool bUseTeamLeader)
{
	if(pNemesisEnt)
	{
		char idBuf[128];
		pEnt->pCritter->voiceSet = nemesis_ChooseDefaultVoiceSet(pNemesisEnt);
		if(bOverrideDisplayName)
			pEnt->pCritter->displayNameOverride = StructAllocString(entGetPersistedName(pNemesisEnt));
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pNemesisEnt->myContainerID, idBuf), pEnt->pCritter->hSavedPet);
		COPY_HANDLE(CONTAINER_NOCONST(Entity, pEnt)->costumeRef.hMood, pNemesisEnt->costumeRef.hMood);
		entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	}
	else
	{
		// A nemesis could be spawned an not have an owning ent due to the player leaving the map. If pNemesisEnt is NULL this is the case
		//  use nemesis info in map state
		const NemesisTeamStruct *pNemTeam = Nemesis_GetTeamStructAtIndex(iPartitionIdx, iTeamIndex, bUseTeamLeader);
		if(pNemTeam)
		{
			if(bOverrideDisplayName && pNemTeam->pchNemesisName)
			{
				pEnt->pCritter->displayNameOverride = StructAllocString(pNemTeam->pchNemesisName);
			}
		}
	}
}

CritterGroup* critter_GetNemesisMinionGroup(Entity *pNemesisEnt)
{
	char *buffer = NULL;
	CritterGroup *pCritterGroup;

	if(pNemesisEnt)
	{

		NemesisMinionPowerSet *pMinionPowerSet = nemesis_NemesisMinionPowerSetFromName(pNemesisEnt->pNemesis->pchMinionPowerSet);
		NemesisMinionCostumeSet *pMinionCostumeSet = nemesis_NemesisMinionCostumeSetFromName(pNemesisEnt->pNemesis->pchMinionCostumeSet);

		if (!pMinionPowerSet || !pMinionCostumeSet)
			return NULL;

		estrStackCreate(&buffer);
		estrPrintf(&buffer, "NemesisMinion_%s_%s", pMinionPowerSet->pcName, pMinionCostumeSet->pcName);
		pCritterGroup = RefSystem_ReferentFromString("CritterGroup", buffer);

		// try just a power critter
		if(!pCritterGroup)
		{
			estrPrintf(&buffer, "NemesisMinion_%s", pMinionPowerSet->pcName);
			pCritterGroup = RefSystem_ReferentFromString("CritterGroup", buffer);
		}

		estrDestroy(&buffer);

		return pCritterGroup;
	}

	return NULL;
}

CritterGroup* critter_GetNemesisMinionGroupAndSetParams(int iPartitionIdx, Entity *pNemesisEnt, CritterCreateParams *createParams, bool bUseLeader, S32 iTeamIndex)
{
	CritterGroup *pCritterGroup = critter_GetNemesisMinionGroup(pNemesisEnt);

	if(pCritterGroup)
	{
		NemesisMinionCostumeSet *pMinionCostumeSet = nemesis_NemesisMinionCostumeSetFromName(pNemesisEnt->pNemesis->pchMinionCostumeSet);

		createParams->fHue = pNemesisEnt->pNemesis->fPowerHue;
		createParams->pCostumeSet = pMinionCostumeSet;
	}
	else
	{
		pCritterGroup = Nemesis_GetCritterGroupAtIndex(iPartitionIdx, iTeamIndex, bUseLeader);
		if(pCritterGroup)
		{
			NemesisMinionCostumeSet *pMinionCostumeSet = nemesis_NemesisMinionCostumeSetFromName(Nemesis_GetCostumeAtIndex(iPartitionIdx, iTeamIndex, bUseLeader));
			createParams->pCostumeSet = pMinionCostumeSet;
		}

	}

	return pCritterGroup;
}

void critter_SetupNemesisMinionEntity(Entity* pEnt, Entity *pNemesisEnt)
{
	// A nemesis could be spawned an not have an owning ent due to the player leaving the map. If pNemesisEnt is NULL this is the case
	if(pNemesisEnt)
	{
		char idBuf[128];
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pNemesisEnt->myContainerID, idBuf), pEnt->pCritter->hSavedPetOwner);
		entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	}
}

Entity* critter_CreateNemesisMinion(Entity *pNemesisEnt, const char *pcRank, const char *pchFSM, OldEncounter *pEnc, 
									OldActor *pActor, GameEncounter *pEnc2, int iActorIndex, int iLevel, 
									int iTeamSize, const char *pcSubRank, int iPartitionIdx, CritterFaction* pFaction, 
									Message* pDisplayNameMsg, CritterDef*** peaExcludeDefs, 
									const char * pchSpawnAnim, F32 fSpawnTime, Entity* spawningPlayer, 
									AITeam* aiTeam)
{
	Entity* pEnt = NULL;

	if (pNemesisEnt && pNemesisEnt->pNemesis){
		NemesisMinionPowerSet *pMinionPowerSet = nemesis_NemesisMinionPowerSetFromName(pNemesisEnt->pNemesis->pchMinionPowerSet);
		NemesisMinionCostumeSet *pMinionCostumeSet = nemesis_NemesisMinionCostumeSetFromName(pNemesisEnt->pNemesis->pchMinionCostumeSet);

		if (pMinionPowerSet && pMinionCostumeSet)
		{
			CritterGroup *pMinionGroup = NULL;
			char *buffer = NULL;
			estrStackCreate(&buffer);

			estrPrintf(&buffer, "NemesisMinion_%s_%s", pMinionPowerSet->pcName, pMinionCostumeSet->pcName);
			pMinionGroup = RefSystem_ReferentFromString("CritterGroup", buffer);

			if(!pMinionGroup)
			{
				// Use new nemesis system to get a power set and then a a costume after we have the critter def
				estrPrintf(&buffer, "NemesisMinion_%s", pMinionPowerSet->pcName);
				pMinionGroup = RefSystem_ReferentFromString("CritterGroup", buffer);
			}
			else
			{
				// clear as we have all information needed
				pMinionCostumeSet = NULL;
			}

			if(pMinionGroup)
			{
				// Spawn a critter from the correct Nemesis Minion group
				pEnt = critter_FindAndCreate(pMinionGroup, pcRank, pEnc, pActor, pEnc2, iActorIndex, iLevel, iTeamSize, pcSubRank, GLOBALTYPE_ENTITYCRITTER, iPartitionIdx, pchFSM, pFaction, NULL, pDisplayNameMsg,
					0, peaExcludeDefs, pchSpawnAnim, fSpawnTime, spawningPlayer, aiTeam, NULL, pMinionCostumeSet);

				// Override various things
				if (pEnt && pEnt->pCritter){
					char idBuf[128];
					SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pNemesisEnt->myContainerID, idBuf), pEnt->pCritter->hSavedPetOwner);

					pEnt->fHue = pNemesisEnt->pNemesis->fMinionPowerHue;

					entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
				}
			}

			estrDestroy(&buffer);
		}
	}
	return pEnt;
}

//adds a critter def's items to a critter. For resetting a critter's inventory (SCPs do this).
void critter_AddNewCritterItems(CritterDef* def, Entity* e, int iLevel)
{
	DefaultItemDef **ppitemList = NULL;
	int i;

	inv_ent_trh_AddInventoryItems(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, e), NULL, NULL);

	addItemsFromDefaultItemList(def,&ppitemList);

	for(i=0;i<eaSize(&ppitemList);i++)
	{
		InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, e),ppitemList[i]->eBagID, NULL);
		ItemDef *pItemDef = GET_REF(ppitemList[i]->hItem);

		if((    ppitemList[i]->iMinLevel >= 0 && iLevel < ppitemList[i]->iMinLevel) 
			|| (ppitemList[i]->iMaxLevel >= 0 && iLevel > ppitemList[i]->iMaxLevel))
		{
			continue; // only add powers in level range
		}

		if(pItemDef)
		{
			Item *item = item_FromDefName(pItemDef->pchName);
			NOCONST(Item) *pNoConstItem = CONTAINER_NOCONST(Item, item);
			pNoConstItem->count = ppitemList[i]->iCount;

			inv_AddItem(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),NULL,ppitemList[i]->eBagID,ppitemList[i]->iSlot,item, pItemDef->pchName, ItemAdd_Silent, NULL, NULL);
			StructDestroy(parse_Item,item);
		}
	}

	eaDestroy(&ppitemList);

	// we don't need to add powers from items here because 
	// we do that in character_ResetPowersArray below
	// item_AddPowersFromItems(e);

	if (g_CombatConfig.bCritterEquipment)
	{
		// Inventory is updated make sure innate powers in equipped items are re-evaluated
		character_DirtyInnateEquip(e->pChar);
		costumeEntity_RegenerateCostume(e);
	}
}

Entity* critter_CreateByDef( CritterDef * def, CritterCreateParams *pCreateParams, const char* blameFile, bool bImmediateCombatUpdate)
{
	Entity * e;
	NOCONST(Entity) * nce;
	Critter* critter;
	const char * pchSpawnAnim;
	
	CritterGroup* critterGroup;

	PERFINFO_AUTO_START_FUNC();

	if (!pCreateParams->bFakeEntity) {
		if (pCreateParams->iPartitionIdx != PARTITION_IN_TRANSACTION) {
			assertmsgf(partition_ExistsByIdx(pCreateParams->iPartitionIdx), "Partition %d does not exist for critter create", pCreateParams->iPartitionIdx);
			if (partition_IsDestroyed(pCreateParams->iPartitionIdx)) {
				// Attempting to create critter during partition shutdown
				PERFINFO_AUTO_STOP();
				return NULL;
			}
		}
	}
	//Create a random positive f32 to use as an abitrary value in expressions
	pCreateParams->fRandom = randomPositiveF32();

	if (def->bTemplate)
	{
		if(blameFile)
			ErrorFilenamef(blameFile, "Template critters should not be spawned: %s", def->pchName);
		else
			Errorf("Template critters should not be spawned: %s.  No blame file; critter was not spawned by an encounter.", def->pchName);
	}

	
	pchSpawnAnim = pCreateParams->pchSpawnAnim ? pCreateParams->pchSpawnAnim : def->pchSpawnAnim;
	pCreateParams->fSpawnTime = pCreateParams->pchSpawnAnim ? pCreateParams->fSpawnTime : def->fSpawnLockdownTime;
	if(!EMPTY_TO_NULL(pchSpawnAnim)) {
		critterGroup = GET_REF(def->hGroup);
		pchSpawnAnim = critterGroup ? critterGroup->pchSpawnAnim : NULL;
		pCreateParams->fSpawnTime = critterGroup ? critterGroup->fSpawnLockdownTime : 0;
	}

	if (pCreateParams->bFakeEntity) {
		e = StructCreateWithComment(parse_Entity, "Creating temporary critter entity");
		CONTAINER_NOCONST(Entity, e)->bFakeEntity = true; // Mark this as a fake entity
		CONTAINER_NOCONST(Entity, e)->myEntityType = pCreateParams->enttype;
		CONTAINER_NOCONST(Entity, e)->pCritter = StructCreateNoConst(parse_Critter);
	} else {
		devassertmsgf((pCreateParams->iPartitionIdx == PARTITION_IN_TRANSACTION) || partition_ExistsByIdx(pCreateParams->iPartitionIdx), "Partition %d does not exist", pCreateParams->iPartitionIdx);
		e = gslCreateEntity(pCreateParams->enttype, pCreateParams->iPartitionIdx);
	}

	nce = CONTAINER_NOCONST(Entity, e);

	e->erOwner = pCreateParams->erOwner;
	e->erCreator = pCreateParams->erCreator;
	e->bNoInterpAlphaOnSpawn = def->bNoInterpAlphaOnSpawn;

	entity_SetDirtyBit(e, parse_Entity, e, false);

	critter = SAFE_MEMBER(e, pCritter);
	assert(critter);

	entity_SetDirtyBit(e, parse_Critter, critter, true);

	objSetDebugName(nce->debugName, MAX_NAME_LEN, GLOBALTYPE_ENTITYCRITTER, e->myContainerID, 0, def->pchName, NULL);

	if(!pCreateParams->pFaction)
		pCreateParams->pFaction = GET_REF(def->hFaction);

	SET_HANDLE_FROM_STRING(g_hCritterDefDict, def->pchName, nce->pCritter->critterDef);

	if(pCreateParams->pOverrideDef)
		SET_HANDLE_FROM_REFDATA(g_hCritterOverrideDict, pCreateParams->pOverrideDef, critter->critterOverrideDef);

	MAX1(pCreateParams->iLevel,1);

	if (!e->bFakeEntity) {
		if (pCreateParams->pEncounter && gConf.bAllowOldEncounterData)
			oldencounter_AttachActor(e, pCreateParams->pEncounter, pCreateParams->pActor, pCreateParams->iTeamSize);
		if (pCreateParams->pEncounter2)
			encounter_AddActor(pCreateParams->iPartitionIdx, pCreateParams->pEncounter2, pCreateParams->iActorIndex, e, pCreateParams->iTeamSize, pCreateParams->iBaseLevel);
	}
	if (gConf.bManualSubRank)
	{
		if (def->pcSubRank)
			pCreateParams->pcSubRank = def->pcSubRank;
	}

	if(pCreateParams->fHue && !def->bIgnoreEntCreateHue)
		e->fHue = pCreateParams->fHue;
	else
		e->fHue = def->fHue;

	gslCritterInitialize(e,def,pCreateParams);
	if (pCreateParams->pCritterGroupDisplayNameMsg)
		SET_HANDLE_FROM_REFERENT(gMessageDict, pCreateParams->pCritterGroupDisplayNameMsg, critter->hGroupDisplayNameMsg);
	if (pCreateParams->pDisplayNameMsg)
		SET_HANDLE_FROM_REFERENT(gMessageDict, pCreateParams->pDisplayNameMsg, critter->hDisplayNameMsg);
	if (pCreateParams->pDisplaySubNameMsg)
		SET_HANDLE_FROM_REFERENT(gMessageDict, pCreateParams->pDisplaySubNameMsg, critter->hDisplaySubNameMsg);
	critter->pcRank = def->pcRank;
	critter->pcSubRank = pCreateParams->pcSubRank;
	critter->fMass = def->fMass;
	critter->bRidable = !(def->pExprRidable == NULL); // It MAY be ridable
	critter->eInteractionFlag = def->eInteractionFlags;
	critter->spawningPlayer = pCreateParams->spawningPlayer ? entGetRef(pCreateParams->spawningPlayer) : 0;
	critter->bIgnoreExternalAnimBits = def->bIgnoreExternalAnimBits;
	critter->bUseCapsuleForPowerArcChecks = def->bUseCapsuleForPowerArcChecks;
	critter->bUseClosestPowerAnimNode = def->bUseClosestPowerAnimNode;
	
	if ( pCreateParams->pCostume )
	{
		// WOLF[12Dec11] Modification to previous change. We may be getting a reference costume here in which case
		//  we need to clone it before costumeEntity_ApplyCritterInfoToCostume tries to update override names in it.
		//  Rather than using the substitute costume field, we'll just rely on the cloning mechanism in that function instead.
		
		bool bCreateCostumeCloneIfNeeded = true; // (Note: this only will actually apply if we need to apply substitute names)
												 // If we need to be more concerned about too much cloning, we could choose to Clone
												 // only if it was a referenced costume via RefSystem_DoesReferentExist(pCostume).
		costumeEntity_ApplyCritterInfoToCostume(CONTAINER_NOCONST(Entity, e), CONTAINER_NOCONST(PlayerCostume, pCreateParams->pCostume), bCreateCostumeCloneIfNeeded);
	}

	if(pCreateParams->pOverrideDef)
		critter_ApplyOverrideDef(e,critter,pCreateParams->pOverrideDef);

	e->pCritter->bSetSpawnAnim = 0;
	if(!e->bFakeEntity && pchSpawnAnim && *pchSpawnAnim){
		AIAnimList *animlist = RefSystem_ReferentFromString(g_AnimListDict, pchSpawnAnim);
		if (animlist)
		{
			aiAnimListSetOneTick(e,animlist);
			e->pCritter->bSetSpawnAnim = 1;
		}
	}

	if(!e->bFakeEntity && e->pChar && GET_REF(e->pChar->hPowerDefStanceDefault))
	{
		character_EnterStance(entGetPartitionIdx(e),e->pChar,NULL,NULL,false,pmTimestamp(0));
	}

	//Add critter inventory
	if(e->pChar && IS_HANDLE_ACTIVE(e->pChar->hClass))
	{
		CharacterClass *pClass = GET_REF(e->pChar->hClass);
		DefaultInventory *pInventory = pClass ? GET_REF(pClass->hInventorySet) : NULL;

		if(pInventory)
			inv_ent_trh_InitAndFixupInventory(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, e),pInventory,true,true,NULL);
		else if(eaSize(&def->ppCritterItems) > 0)
			ErrorFilenamef(def->pchFileName,"%s Critter has items, but no inventory set! No items will be equipped",def->pchName);

		critter_AddNewCritterItems(def, e, pCreateParams->iLevel);
	}

	// Start up the Character's innate state immediately, without waiting for a combat tick
	if(e->pChar && !e->bFakeEntity && bImmediateCombatUpdate)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);

		character_ResetPowersArray(iPartitionIdx, pchar, NULL);

		pchar->bSkipAccrueMods = false;

		character_DirtyInnatePowers(pchar);
		character_DirtyPowerStats(pchar);
		character_AccrueMods(iPartitionIdx,pchar,0.0f,NULL);
		character_DirtyInnateAccrual(pchar);

		// Start up passives
		character_RefreshPassives(iPartitionIdx, pchar, NULL);

		if (iPartitionIdx != PARTITION_IN_TRANSACTION) 
		{
			// Update movement state
			character_UpdateMovement(pchar,NULL);
		}
	}

	if (!e->bFakeEntity && entGetPartitionIdx(e) != PARTITION_IN_TRANSACTION)
	{
		if(e->erOwner)
		{
			Entity *owner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);

			if(owner && entGetType(owner)==GLOBALTYPE_ENTITYPLAYER)
			{
				mmCollisionGroupHandleCreateFG(e->mm.movement, &e->mm.mcgHandle, __FILE__, __LINE__, MCG_PLAYER_PET);
				if (gConf.bPetsDontCollideWithPlayer)
					mmCollisionBitsHandleCreateFG(e->mm.movement, &e->mm.mcbHandle, __FILE__, __LINE__, ~(MCG_PLAYER | MCG_PLAYER_PET));
				else
					mmCollisionBitsHandleCreateFG(e->mm.movement, &e->mm.mcbHandle, __FILE__, __LINE__, ~0);
			}
		}
		else if (gConf.bEnemiesDontCollideWithPlayer || gConf.bNPCsDontCollideWithNPCs)
		{
			U32 uCollideBits = 0xffffffff;
			if (gConf.bEnemiesDontCollideWithPlayer)
			{
				uCollideBits &= ~MCG_PLAYER;
			}
			else if (gConf.bNPCsDontCollideWithNPCs)
			{
				uCollideBits &= ~MCG_OTHER;
			}

			mmCollisionGroupHandleCreateFG(e->mm.movement, &e->mm.mcgHandle, __FILE__, __LINE__, MCG_OTHER);
			mmCollisionBitsHandleCreateFG(e->mm.movement, &e->mm.mcbHandle, __FILE__, __LINE__, uCollideBits);
		}
	}

	PERFINFO_AUTO_STOP();

	return e;
}

Entity * critter_Create(const char * name, const char * overridename, int enttype, int iPartitionIdx, const char* fsmOverride, 
						int iLevel, int iTeamSize, const char *pcSubRank, CritterFaction *pFaction, 
						Message * pCritterGroupDisplayNameMsg, Message * pDisplayNameMsg, Message * pDisplaySubNameMsg, char *pchSpawnAnim, F32 fSpawnTime, 
						AITeam *aiTeam, WorldInteractionNode* pCreatorNode)
{
	CritterDef* critter = NULL;
	CritterOverrideDef *critteroverride = NULL;
	CritterCreateParams params = {0};

	critter = critter_DefGetByName(name);

	if( !critter )
		return 0;

	if(overridename)
		critteroverride = critter_OverrideDefGetByName(overridename);

	params.pOverrideDef = critteroverride;
	params.enttype = enttype;
	params.iPartitionIdx = iPartitionIdx;
	params.fsmOverride = fsmOverride;
	params.iLevel = iLevel;
	params.iTeamSize = iTeamSize;
	params.pcSubRank = pcSubRank;
	params.pFaction = pFaction;
	params.pCritterGroupDisplayNameMsg = pCritterGroupDisplayNameMsg;
	params.pDisplayNameMsg = pDisplayNameMsg;
	params.pDisplaySubNameMsg = pDisplaySubNameMsg;
	params.pchSpawnAnim = pchSpawnAnim;
	params.fSpawnTime = fSpawnTime;
	params.aiTeam = aiTeam;
	params.pCreatorNode = pCreatorNode;

	return critter_CreateByDef( critter, &params, NULL, true);
}

Entity * critter_CreateWithCostume(const char * name, const char * overridename, int enttype, int iPartitionIdx, const char* fsmOverride, 
								   int iLevel, int iTeamSize, const char *pcSubRank, CritterFaction *pFaction, 
								   Message * pCritterGroupDisplayNameMsg, Message * pDisplayNameMsg, Message * pDisplaySubNameMsg, char *pchSpawnAnim, F32 fSpawnTime, PlayerCostume* pCostume )
{
	CritterDef* critter = NULL;
	CritterOverrideDef *critteroverride = NULL;
	CritterCreateParams params = {0};

	critter = critter_DefGetByName(name);

	if( !critter )
		return 0;

	if(overridename)
		critteroverride = critter_OverrideDefGetByName(overridename);

	params.pOverrideDef = critteroverride;
	params.enttype = enttype;
	params.iPartitionIdx = iPartitionIdx;
	params.fsmOverride = fsmOverride;
	params.iLevel = iLevel;
	params.iTeamSize = iTeamSize;
	params.pcSubRank = pcSubRank;
	params.pFaction = pFaction;
	params.pCritterGroupDisplayNameMsg = pCritterGroupDisplayNameMsg;
	params.pDisplayNameMsg = pDisplayNameMsg;
	params.pDisplaySubNameMsg = pDisplaySubNameMsg;
	params.pchSpawnAnim = pchSpawnAnim;
	params.fSpawnTime = fSpawnTime;
	params.pCostume = pCostume;

	return critter_CreateByDef(critter, &params, NULL, true);

}

Entity* critter_FindAndCreate( CritterGroup *pGroup, const char *pcRank, OldEncounter *pEncounter, OldActor *pActor, 
							   GameEncounter *pEncounter2, int iActorIndex, int iLevel, int iTeamSize, 
							   const char *pcSubRank, int enttype, int iPartitionIdx, const char* fsmOverride, CritterFaction * pFaction, 
							   Message * pCritterGroupDisplayNameMsg, Message * pDisplayNameMsg, Message * pDisplaySubNameMsg, CritterDef*** excludeDefs, const char * pchSpawnAnim, 
							   F32 fSpawnTime, Entity* spawningPlayer, AITeam* aiTeam, PlayerCostume* pCostume, NemesisMinionCostumeSet *pNemMinionSet)
{
	int totalFail=0;
	Message * pNameMsg = pDisplayNameMsg;
	Message * pSubNameMsg = pDisplaySubNameMsg;
	CritterDef * pDef = NULL;
	Entity * e = NULL;
	CritterCreateParams createParams = {0};

	PERFINFO_AUTO_START_FUNC();

	pDef = critter_DefFind( pGroup, pcRank, pcSubRank, iLevel, &totalFail, excludeDefs);

	if(totalFail)
	{
		const char* filename = NULL;
		if(pEncounter && gConf.bAllowOldEncounterData) 
		{
			OldStaticEncounter* pStaticEnc = GET_REF(pEncounter->staticEnc);
			if(pStaticEnc)
				filename = pStaticEnc->pchFilename;

			if(filename && pStaticEnc)
				ErrorFilenamef(filename, "Encounter %s: No critter matches the given rank/group/level (%s, %s, %d) or spawn limits ruled out the options", pStaticEnc->name, pcRank, pGroup ? pGroup->pchName : "No Group", iLevel);
			else
				Errorf("No critter matches the given rank/group/level (%s, %s, %d)", pcRank, pGroup ? pGroup->pchName : "No Group", iLevel);
		}
		if(pEncounter2)
		{
			WorldEncounterProperties *pProps = pEncounter2->pWorldEncounter->properties;
			EncounterTemplate *pTemplate = pProps ? GET_REF(pProps->hTemplate) : NULL;
			if (pTemplate)
				ErrorFilenamef(pTemplate->pcFilename, "Encounter %s: No critter matches the given rank/group/level (%s, %s, %d) or spawn limits ruled out the options", pTemplate->pcName, pcRank, pGroup ? pGroup->pchName : "No Group", iLevel);
			else
				Errorf("No critter matches the given rank/group/level (%s, %s, %d)", pcRank, pGroup ? pGroup->pchName : "No Group", iLevel);
		}
	}

	if(!pDef)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// check for Nemesis minion
	if(pNemMinionSet)
	{
		PlayerCostume *pNemCostume = nemesis_MinionCostumeByClass(pNemMinionSet, pDef->pchClass);
		if(pNemCostume)
		{
			// Costume found, replace it
			pCostume = pNemCostume;
		}
		else
		{
			ErrorFilenamef(pDef->pchFileName, "Nemesis minion %s of class %s can't find a costume.", pDef->pchName, pDef->pchClass);
		}

		createParams.pCostumeSet = pNemMinionSet;
	}

	createParams.enttype = enttype;
	createParams.iPartitionIdx = iPartitionIdx;
	createParams.fsmOverride = fsmOverride;
	createParams.pEncounter = pEncounter;
	createParams.pActor = pActor;
	createParams.pEncounter2 = pEncounter2;
	createParams.iActorIndex = iActorIndex;
	createParams.iLevel = iLevel;
	createParams.iTeamSize = iTeamSize;
	createParams.pcSubRank = pcSubRank;
	createParams.pFaction = pFaction;
	createParams.pCritterGroupDisplayNameMsg = pCritterGroupDisplayNameMsg;
	createParams.pDisplayNameMsg = pNameMsg;
	createParams.pDisplaySubNameMsg = pSubNameMsg;
	createParams.pchSpawnAnim = pchSpawnAnim;
	createParams.fSpawnTime = fSpawnTime;
	createParams.spawningPlayer = spawningPlayer;
	createParams.aiTeam = aiTeam;
	createParams.pCostume = pCostume;

	e = critter_CreateByDef(pDef, &createParams, NULL, true);
	
	PERFINFO_AUTO_STOP();

	return e;
}

static void critter_ApplyOverrideDef(Entity* e, Critter *pCritter, CritterOverrideDef *pDef)
{
	entity_SetDirtyBit(e, parse_Critter, pCritter, false);
	if(pCritter->fMass > -1)
		pCritter->fMass = pDef->fMass;

	if(pCritter->eInteractionFlag != kCritterOverrideFlag_None)
		pCritter->eInteractionFlag = pDef->eFlags;
}



static int critterdef_ValidateInvClassnames(CritterDef *def, char **classnames)
{
	int res = 1;
	int k;
	int j;
	DefaultInventory *inv;
	int i;
	CharacterClass *critter_class;
	DefaultItemDef **ppitemList = NULL;
	char *last_classname = 0;
	for(i = eaSize(&classnames)-1; i>=0; --i)
	{
		char *classname = classnames[i];
		if(!classname) 
			continue;

		// premature optimization? it appears that the data
		// almost always uses the same name for all of the classnames
		if(last_classname && 0 == stricmp(classname,last_classname))
		   continue;

		last_classname = classname;
		critter_class = characterclasses_FindByName(classname);

		if(!critter_class)
		{
			ErrorFilenamef(def->pchFileName,"couldn't find class %s",classname);
			res = 0;
			continue;
		}
		inv = GET_REF(critter_class->hInventorySet);
		if(!inv)
			continue;
		for(j=eaSize(&def->ppCritterItems)-1; j>=0; j--)
		{
			DefaultItemDef *item_def = def->ppCritterItems[j];
			if(!item_def || item_def->bDisabled)
				continue;
			for(k = eaSize(&inv->InventoryBags);k>=0;--k)
			{
				InvBagDef *bagdef = eaGet(&inv->InventoryBags,k);
				if(!bagdef)
					continue;
				if(bagdef->BagID == item_def->eBagID)
					break;
			}
			if(k<0)
			{
				ErrorFilenamef(def->pchFileName,"no matching bag found for default item %i (%s): needs a bag with id %s",j,SAFE_MEMBER(GET_REF(item_def->hItem),pchName),StaticDefineIntRevLookup(InvBagIDsEnum,item_def->eBagID));
				res = 0;
			}
		}
	}
	return res;
	
}

static int critterdef_ValidateInvGrants(CritterDef *def)
{
	CharacterClassInfo *pClassInfo = NULL;
	if(!def)
		return 0;
	pClassInfo = CharacterClassInfo_FindByName(def->pchClass);
	if(!pClassInfo)
	{
		ErrorFilenamef(def->pchFileName,"invalid class %s",def->pchClass);
		return 0;
	}

	return critterdef_ValidateInvClassnames(def,pClassInfo->ppchWeak)
	  && critterdef_ValidateInvClassnames(def,pClassInfo->ppchNormal)
	  && critterdef_ValidateInvClassnames(def,pClassInfo->ppchTough);
}


int critter_CritterDefPostTextRead(CritterDef *def)
{
	int i;
	static ExprContext* exprContext = NULL;
	int success = true;

	for(i = eaSize(&def->ppPowerConfigs)-1; i >= 0; i--)
	{
		CritterPowerConfig *powConfig = def->ppPowerConfigs[i];
		if(powConfig->pExprAIWeightModifier)
			aiPowersGenerateConfigExpression(powConfig->pExprAIWeightModifier);
		if(powConfig->pExprAIRequires)
			aiPowersGenerateConfigExpression(powConfig->pExprAIRequires);
		if(powConfig->pExprAIEndCondition)
			aiPowersGenerateConfigExpression(powConfig->pExprAIEndCondition);
		if(powConfig->pExprAIChainRequires)
			aiPowersGenerateConfigExpression(powConfig->pExprAIChainRequires);
		if(powConfig->pExprAITargetOverride)
			aiPowersGenerateConfigExpression(powConfig->pExprAITargetOverride);
		if(powConfig->aiPowerConfigDefInst)
			aiPowerConfigDefGenerateExprs(powConfig->aiPowerConfigDefInst);
		if(powConfig->pExprAICureRequires)
			aiPowersGenerateConfigExpression(powConfig->pExprAICureRequires);

		if(powConfig->pExprAddPowerRequires)
		{
			ExprContext *pContext = critter_GetEncounterExprContext();
			exprGenerate(powConfig->pExprAddPowerRequires, pContext);
		}
	}

	if (!exprContext)
	{
		exprContext = exprContextCreate();
		exprContextSetFuncTable(exprContext, exprContextCreateFunctionTable());
	}

	if (def->pExprRidable)
	{
		exprGenerate(def->pExprRidable, exprContext);
	}

	critterdef_ValidateInvGrants(def);

	return success;
}

const char* critter_GetEncounterName(Critter *pCritter)
{
	if(pCritter->encounterData.parentEncounter)
	{
		return oldencounter_GetEncounterName(pCritter->encounterData.parentEncounter);
	}
	else if(pCritter->encounterData.pGameEncounter)
	{
		return encounter_GetName(pCritter->encounterData.pGameEncounter);
	}

	return NULL;
}

const char* critter_GetActorName(Critter* pCritter)
{
	if(pCritter->encounterData.parentEncounter)
	{
		return pCritter->encounterData.sourceActor->name;
	}
	else if(pCritter->encounterData.pGameEncounter)
	{
		return encounter_GetActorName(pCritter->encounterData.pGameEncounter, pCritter->encounterData.iActorIndex);
	}

	return NULL;
}

void critter_SetupEncounterExprContext(ExprContext *pContext, Entity *pEnt, int iPartitionIdx, int iTeamSize, F32 fRandom, OldEncounter *pEncounter, GameEncounter *pGameEncounter)
{
	devassert(pContext != NULL);

	exprContextSetSelfPtr(pContext, pEnt);

	exprContextSetPartition(pContext, iPartitionIdx);
	exprContextSetIntVar(pContext, "PlayerTeamSize", iTeamSize);
	exprContextSetFloatVar(pContext, "RandomVal", fRandom);
	exprContextSetPointerVarPooled(pContext, g_EncounterVarName, pEncounter, parse_OldEncounter, true, false);
	exprContextSetPointerVarPooled(pContext, g_Encounter2VarName, pGameEncounter, parse_GameEncounter, true, false);
}

ExprContext *critter_GetEncounterExprContext()
{
	static ExprContext *s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* s_FuncTable = NULL;
		s_pContext = exprContextCreate();

		s_FuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_FuncTable, "util");
		exprContextAddFuncsToTableByTag(s_FuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_FuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_FuncTable, "encounter");
		exprContextAddFuncsToTableByTag(s_FuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_FuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_FuncTable, "critter");
		exprContextAddFuncsToTableByTag(s_FuncTable, "s_CritterExprFuncList");
		exprContextAddFuncsToTableByTag(s_FuncTable, "gameutil");
		exprContextSetFuncTable(s_pContext, s_FuncTable);

		exprContextSetAllowRuntimeSelfPtr(s_pContext);
		exprContextSetAllowRuntimePartition(s_pContext);
	}

	exprContextClearPartition(s_pContext);
	exprContextSetSelfPtr(s_pContext, NULL);

	exprContextSetIntVar(s_pContext, "PlayerTeamSize", 1);
	exprContextSetFloatVar(s_pContext, "RandomVal", 0.f);
	exprContextRemoveVarPooled(s_pContext, g_EncounterVarName);
	exprContextRemoveVarPooled(s_pContext, g_Encounter2VarName);

	return s_pContext;
}