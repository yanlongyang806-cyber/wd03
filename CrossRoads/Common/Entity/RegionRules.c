#define GENESIS_ALLOW_OLD_HEADERS
#include "RegionRules.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "ControlScheme.h"
#include "cutscene_common.h"
#include "DoorTransitionCommon.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "Powers.h"
#include "rewardCommon.h"
#include "StringCache.h"
#include "Team.h"
#include "TriCube/vec.h"
#include "wlGroupPropertyStructs.h"
#include "WorldGrid.h"
#include "wlGenesis.h"
#include "rand.h"
#include "SavedPetCommon.h"

#include "AutoGen/RegionRules_h_ast.h"
#include "AutoGen/conversions_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hRegionRulesDict;

static ExprContext *s_pRuleContext = NULL;

extern StaticDefineInt WorldRegionTypeEnum[];

static void RegionRulesPostProcess(RegionRules *pRules)
{
	if(pRules->pXRelocation)
		exprGenerate(pRules->pXRelocation,s_pRuleContext);
	if(pRules->pYRelocation)
		exprGenerate(pRules->pYRelocation,s_pRuleContext);
	if(pRules->pZRelocation)
		exprGenerate(pRules->pZRelocation,s_pRuleContext);

	if (pRules->pKillCreditLimit && pRules->pKillCreditLimit->iMaxKills)
	{
		pRules->pKillCreditLimit->fUpdateRate = pRules->pKillCreditLimit->iTimePeriod / (F32)pRules->pKillCreditLimit->iMaxKills;
	}
	else if (pRules->pKillCreditLimit)
	{
		pRules->pKillCreditLimit->fUpdateRate = 0.0f;
	}
}

static void RegionRules_Init()
{
	ExprFuncTable* stTable;
	ExprContext **ppTempChoiceContext = worldGetTempChoiceContext();

	s_pRuleContext = exprContextCreate();
	*ppTempChoiceContext = exprContextCreate();

	// Functions
	//  Generic, Self, Character
	stTable = exprContextCreateFunctionTable();
	exprContextSetIntVar(s_pRuleContext,"Seed",0);
	exprContextAddFuncsToTableByTag(stTable, "util");
	exprContextAddFuncsToTableByTag(stTable, "CEFuncsRegionRules");
	
	exprContextSetFuncTable(s_pRuleContext, stTable);
	exprContextSetFuncTable(*ppTempChoiceContext, stTable);

	exprContextSetSelfPtr(s_pRuleContext,NULL);
	exprContextSetSelfPtr(*ppTempChoiceContext,NULL);
}

// Validates a transition sequence for use as a default arrival or departure transition on RegionRules
static void RegionRulesValidateTransitionSequence(RegionRules* pRules, DoorTransitionSequenceRef* pTransSequence)
{
	DoorTransitionSequenceDef* pTransSeqDef = GET_REF(pTransSequence->hTransSequence);

	if (IS_HANDLE_ACTIVE(pTransSequence->hTransSequence) && !pTransSeqDef)
	{
		ErrorFilenamef(pRules->pchFileName, 
			"Region Rules '%s': Invalid transition sequence '%s'", 
			StaticDefineIntRevLookup(WorldRegionTypeEnum, pRules->eRegionType), 
			REF_STRING_FROM_HANDLE(pTransSequence->hTransSequence));
	}
}

static void RegionRulesValidateDoorTransitions(RegionRules* pRules)
{
	S32 i;
	for (i = 0; i < eaSize(&pRules->eaArriveSequences); i++)
	{
		RegionRulesValidateTransitionSequence(pRules, pRules->eaArriveSequences[i]);
	}
	for (i = 0; i < eaSize(&pRules->eaDepartSequences); i++)
	{
		RegionRulesValidateTransitionSequence(pRules, pRules->eaDepartSequences[i]);
	}
	if (REF_STRING_FROM_HANDLE(pRules->hPetRequestDepart) && !GET_REF(pRules->hPetRequestDepart))
	{
		ErrorFilenamef(pRules->pchFileName, 
			"Region Rules '%s': Specifies non-existent pet request departure transition '%s'", 
			StaticDefineIntRevLookup(WorldRegionTypeEnum, pRules->eRegionType), 
			REF_STRING_FROM_HANDLE(pRules->hPetRequestDepart));
	}
	if (REF_STRING_FROM_HANDLE(pRules->hPetRequestArrive) && !GET_REF(pRules->hPetRequestArrive))
	{
		ErrorFilenamef(pRules->pchFileName, 
			"Region Rules '%s': Specifies non-existent pet request arrival transition '%s'", 
			StaticDefineIntRevLookup(WorldRegionTypeEnum, pRules->eRegionType), 
			REF_STRING_FROM_HANDLE(pRules->hPetRequestArrive));
	}
}

static void RegionRulesValidateKillCreditLimits(RegionRules* pRules)
{
	if (pRules->pKillCreditLimit && pRules->pKillCreditLimit->iMaxKills)
	{
		if (!pRules->pKillCreditLimit->iTimePeriod)
		{
			ErrorFilenamef(pRules->pchFileName, "Kill reward limit specifies a max kill count %d without specifying a time period", pRules->pKillCreditLimit->iMaxKills);
		}
	}
}

static void RegionRulesValidate(RegionRules* pRules)
{
#ifdef GAMESERVER
	S32 i;
	for (i = eaSize(&pRules->eaScanForInteractablesAllegianceFX)-1; i >= 0; i--)
	{
		RegionAllegianceFXData* pAllegianceFX = pRules->eaScanForInteractablesAllegianceFX[i];
		
		if (!GET_REF(pAllegianceFX->hAllegiance) && REF_STRING_FROM_HANDLE(pAllegianceFX->hAllegiance))
		{
			ErrorFilenamef(pRules->pchFileName, "Region rules references non-existent allegiance '%s'", REF_STRING_FROM_HANDLE(pAllegianceFX->hAllegiance));
		}
	}
	if (EMPTY_TO_NULL(pRules->pchGlobalCritterDropRewardTable))
	{
		if (!RefSystem_ReferentFromString(g_hRewardTableDict, pRules->pchGlobalCritterDropRewardTable))
		{
			ErrorFilenamef(pRules->pchFileName, "Region rules references non-existent reward table'%s'", pRules->pchGlobalCritterDropRewardTable);
		}
	}
	if (pRules->bAlwaysCollideWithPets && g_CombatConfig.bCollideWithPetsInCombat)
	{
		ErrorFilenamef(pRules->pchFileName, "Region rules sets AlwaysCollideWithPets, but CollideWithPetsInCombat is set in the combat config!");
	}
	RegionRulesValidateDoorTransitions(pRules);
	RegionRulesValidateKillCreditLimits(pRules);
#endif
}

static int RegionRulesValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	RegionRules *pRules = pResource;
	switch(eType)
	{	
	xcase RESVALIDATE_POST_TEXT_READING:		
		RegionRulesPostProcess(pRules);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		RegionRulesValidate(pRules);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterRegionRulesDict(void)
{
	g_hRegionRulesDict = RefSystem_RegisterSelfDefiningDictionary("RegionRules", false, parse_RegionRules, true, true, NULL);

	resDictManageValidation(g_hRegionRulesDict, RegionRulesValidateCB);
}

void RegionRulesReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading RegionRules...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_hRegionRulesDict);

	loadend_printf(" done (%d RegionRules)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hRegionRulesDict));
}

AUTO_STARTUP(RegionRules) ASTRT_DEPS(Allegiance, CharacterClasses, PowerCategories, DoorTransitionSequence, AS_ControlSchemeRegions, CombatConfig);
void RegionRulesLoad(void)
{
	RegionRules_Init();

	resLoadResourcesFromDisk(g_hRegionRulesDict, NULL, "defs/config/RegionRules.def", "RegionRules.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/RegionRules.def", RegionRulesReload);
	}
}

AUTO_STARTUP(RegionRulesMinimal) ASTRT_DEPS(CharacterClasses, PowerCategories, AS_ControlSchemeRegions);
void RegionRulesLoadMinimal(void)
{
	RegionRulesLoad();
}


AUTO_TRANS_HELPER_SIMPLE;
RegionRules *getRegionRulesFromRegionType(S32 eRegionType)
{
	return RefSystem_ReferentFromString(g_hRegionRulesDict,StaticDefineIntRevLookup(WorldRegionTypeEnum,(WorldRegionType)eRegionType));
}

RegionRules *getRegionRulesFromEnt(Entity* ent){
	return getRegionRulesFromRegionType(entGetWorldRegionTypeOfEnt(ent));
}

RegionRules *getRegionRulesFromSchemeRegionType(S32 eSchemeRegionType)
{
	S32 i;
	for (i = 0; i < WRT_COUNT; i++)
	{
		RegionRules* pRules = getRegionRulesFromRegionType(i);
		if (pRules && pRules->eSchemeRegionType == eSchemeRegionType)
		{
			return pRules;
		}
	}
	return NULL;
}

RegionRules *RegionRulesFromVec3(const Vec3 vPos)
{
	WorldRegion *pRegion = worldGetWorldRegionByPos(vPos);

	if(pRegion)
		return getRegionRulesFromRegion(pRegion);
	else
		return NULL;
}

RegionRules *getRegionRulesFromRegion(WorldRegion *pRegion)
{
	static RegionRules *pReturn = NULL;

	if(pRegion)
	{
		RegionRules *pRules = RefSystem_ReferentFromString(g_hRegionRulesDict,StaticDefineIntRevLookup(WorldRegionTypeEnum,(WorldRegionType)worldRegionGetType(pRegion)));
		RegionRulesOverride *pOverride = worldRegionGetOverrides(pRegion);

		if(!pRules)
			return NULL;

		if(!pReturn)
			pReturn = StructAlloc(parse_RegionRules);

		StructCopyAll(parse_RegionRules,pRules,pReturn);

		if(pOverride)
		{
			if(pOverride->iAllowedPetsPerPlayer != -1)
				pReturn->iAllowedPetsPerPlayer = pOverride->iAllowedPetsPerPlayer;

			pReturn->ppTempPuppets = pOverride->ppTempPuppets;

			if(pOverride->eVehicleRules != kVehicleRules_Inherit)
				pReturn->eVehicleRules = pOverride->eVehicleRules;
		}
		else
		{
			pReturn->iAllowedPetsPerPlayer = -1;
			pReturn->ppTempPuppets = NULL;
		}

		return pReturn;
	}

	return NULL;
}

RegionRules *getRegionRulesFromRegionNoOverride(WorldRegion *pRegion)
{
	if(pRegion)
	{
		RegionRules *pRules = RefSystem_ReferentFromString(g_hRegionRulesDict,StaticDefineIntRevLookup(WorldRegionTypeEnum,(WorldRegionType)worldRegionGetType(pRegion)));
		
		return pRules;
	}

	return NULL;
}

RegionRules *RegionRulesFromVec3NoOverride(const Vec3 vPos)
{
	WorldRegion *pRegion = worldGetWorldRegionByPos(vPos);

	if(pRegion)
		return getRegionRulesFromRegionNoOverride(pRegion);
	else
		return NULL;
}

static bool tempPuppetChoice_eval(TempPuppetChoice *pChoice, Entity *pEntity)
{
	MultiVal mv = {0};
	bool bValid;
	ExprContext **ppTempChoiceContext = worldGetTempChoiceContext();

	if(pChoice->pEvalExpression)
		exprEvaluate(pChoice->pEvalExpression,*ppTempChoiceContext,&mv);
	else
		return true;

	return MultiValGetInt(&mv,&bValid);
}

static bool entity_FindBestPuppetChoiceWithSetRequirement(Entity *pEnt, CharClassCategorySet *pCategorySet, RegionRules *pRegionRules, PuppetEntity **ppChoiceOut) 
{
	bool bPassCheck = true;
	int i;
	(*ppChoiceOut) = NULL;
	for (i=eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1;i>=0;i--)
	{
		PuppetEntity* pPuppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
		Entity *pPetEntCheck = SavedPuppet_GetEntity(entGetPartitionIdx(pEnt), pPuppetEntity);
		CharacterClass *pPetClass = pPetEntCheck && pPetEntCheck->pChar ? GET_REF(pPetEntCheck->pChar->hClass) : NULL;
		bPassCheck = bPassCheck && pPetEntCheck;
		if (pPetEntCheck && pPuppetEntity->eState == PUPPETSTATE_ACTIVE)
		{
			bool bValid = false;

			if (pPetClass 
				&& ea32Find(&pRegionRules->peCharClassTypes, pPetClass->eType) >= 0)
			{
				bValid = true;
			}
			else if (pPuppetEntity->curType == pEnt->pSaved->pPuppetMaster->curType
				&& pPuppetEntity->curID == pEnt->pSaved->pPuppetMaster->curID
				&& ea32Find(&pRegionRules->peCharClassTypes, pPuppetEntity->eType) >= 0)
			{
				bValid = true;
			}
			if (bValid)
			{
				if (pCategorySet)
				{
					if (!pPetClass || !CharClassCategorySet_checkIfPass(pCategorySet, pPetClass->eCategory, pPuppetEntity->eType))
						continue;
				}
				(*ppChoiceOut) = pPuppetEntity;
				return true;
			}
		}
	}
	return bPassCheck;
}

static bool entity_FindBestPuppetChoice(Entity *pEnt, RegionRules *pRegionRules, PuppetEntity **ppChoiceOut) 
{
	CharClassCategorySet *pCategorySet = CharClassCategorySet_getPreferredSetForRegion(pEnt, pRegionRules);
	bool bPassCheck = true;
	int i;
	(*ppChoiceOut) = NULL;
	for (i=eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1;i>=0;i--)
	{
		PuppetEntity* pPuppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
		Entity *pPetEntCheck = SavedPuppet_GetEntity(entGetPartitionIdx(pEnt), pPuppetEntity);
		CharacterClass *pPetClass = pPetEntCheck && pPetEntCheck->pChar ? GET_REF(pPetEntCheck->pChar->hClass) : NULL;
		bPassCheck = bPassCheck && pPetEntCheck;
		if(pPetEntCheck && pPuppetEntity->eState == PUPPETSTATE_ACTIVE)
		{
			bool bValid = false;

			if (pPetClass 
				&& ea32Find(&pRegionRules->peCharClassTypes, pPetClass->eType) >= 0)
			{
				bValid = true;
			}
			else if (pPuppetEntity->curType == pEnt->pSaved->pPuppetMaster->curType
				&& pPuppetEntity->curID == pEnt->pSaved->pPuppetMaster->curID
				&& ea32Find(&pRegionRules->peCharClassTypes, pPuppetEntity->eType) >= 0)
			{
				bValid = true;
			}
			if (bValid)
			{
				(*ppChoiceOut) = pPuppetEntity;
				if (pCategorySet)
				{
					if (pPetClass && CharClassCategorySet_checkIfPass(pCategorySet, pPetClass->eCategory, pPuppetEntity->eType))
						return true;
				}
				else
				{
					return true;
				}
			}
		}
	}
	return bPassCheck;
}

bool entity_ChoosePuppetEx(Entity *pEnt, RegionRules *pRegionRules, ZoneMapInfo *pZoneMapInfo, PuppetEntity **ppChoiceOut)
{
	CharClassCategorySet *pSet = zmapInfoGetRequiredClassCategorySet(pZoneMapInfo);

	if(!pEnt || !pEnt->pSaved || !pEnt->pSaved->pPuppetMaster)
		return false;

	if(!ppChoiceOut)
		return false;

	// If no curID, then creation of puppet master is still pending
	if (!pEnt->pSaved->pPuppetMaster->curID
		&& pEnt->pSaved->pPuppetMaster->curType == GLOBALTYPE_ENTITYPLAYER)
		return false;

	if(!pRegionRules)
	{
		pRegionRules = getRegionRulesFromEnt(pEnt);
	}

	if (pSet)
		return entity_FindBestPuppetChoiceWithSetRequirement(pEnt, pSet, pRegionRules, ppChoiceOut);
	else
		return entity_FindBestPuppetChoice(pEnt, pRegionRules, ppChoiceOut);
}

TempPuppetChoice *entity_ChooseTempPuppet(Entity *pEnt, TempPuppetChoice **ppChoices)
{
	TempPuppetChoice **ppPossibleChoices = NULL;
	TempPuppetChoice *pReturn = NULL;
	int i;
	F32 fTotalWeight = 0.f;

	for(i=0;i<eaSize(&ppChoices);i++)
	{
		if(tempPuppetChoice_eval(ppChoices[i],pEnt))
		{
			eaPush(&ppPossibleChoices,ppChoices[i]);
			fTotalWeight += ppChoices[i]->fWeight;
		}
	}

	if(eaSize(&ppPossibleChoices) == 0)
	{
		return NULL;
	} else {
		F32 fRand = randomPositiveF32() * fTotalWeight;
		i = 0;
		
		while(i < eaSize(&ppPossibleChoices))
		{
			if(fRand < ppPossibleChoices[i]->fWeight)
			{
				pReturn = ppPossibleChoices[i];
				break;
			}
			fRand -= ppPossibleChoices[i]->fWeight;
			i++;
		}
	}

	eaDestroy(&ppPossibleChoices);

	return pReturn;
}

static void Entity_GetOffsetVec3(RegionRules *pRules, int iSeedValue, Vec3 vSpawnPos)
{
	bool bValid = false;
	exprContextSetIntVar(s_pRuleContext,"Seed",iSeedValue);

	if(pRules->pXRelocation)
	{
		MultiVal mv = {0};
		exprEvaluate(pRules->pXRelocation,s_pRuleContext,&mv);
		vSpawnPos[0] = MultiValGetFloat(&mv,&bValid);
	}

	if(pRules->pYRelocation)
	{
		MultiVal mv = {0};
		exprEvaluate(pRules->pYRelocation,s_pRuleContext,&mv);
		vSpawnPos[1] = MultiValGetFloat(&mv,&bValid);
	}

	if(pRules->pZRelocation)
	{
		MultiVal mv = {0};
		exprEvaluate(pRules->pZRelocation,s_pRuleContext,&mv);
		vSpawnPos[2] = MultiValGetFloat(&mv,&bValid);
	}
}

void Entity_SavedPetGetOriginalSpawnPos(Entity *pOwner, RegionRules *pRules, Quat qRot, Vec3 vSpawnPos)
{
	Vec3 vOwnerOffset, vPosFinal;

	if(!pRules)
	{
		//Find rules for this entity
		WorldRegion *pRegion = worldGetWorldRegionByPos(vSpawnPos);

		if(pRegion)
			pRules = getRegionRulesFromRegion(pRegion);
	}

	if(!pRules)
		return;

	ZEROVEC3(vOwnerOffset);

	if(pOwner && team_IsMember(pOwner))
	{
		Entity_GetOffsetVec3(pRules,pOwner->iSeedNumber,vOwnerOffset);
	}else{
		Entity_GetOffsetVec3(pRules,0,vOwnerOffset);
	}

	quatRotateVec3(qRot,vOwnerOffset,vPosFinal);

	subVec3(vSpawnPos,vPosFinal,vSpawnPos);
}

void Entity_GetPositionOffset(int iPartitionIdx, RegionRules *pRules, Quat qRot, int iSeedValue, Vec3 vSpawnPos, int iSpawnBox[3])
{
	Vec3 vPosTemp,vPosFinal;
	S32 bFloorFound = false;

	ZEROVEC3(vPosTemp);

	if(!pRules)
	{
		//Find rules for this entity
		WorldRegion *pRegion = worldGetWorldRegionByPos(vSpawnPos);

		if(pRegion)
			pRules = getRegionRulesFromRegion(pRegion);
	}

	if(!pRules)
		return;

	Entity_GetOffsetVec3(pRules,iSeedValue,vPosTemp);

	quatRotateVec3(qRot,vPosTemp,vPosFinal);

	if(pRules->pSpawnBox)
	{
		vPosTemp[0] += pRules->pSpawnBox->fAreaCheck * iSpawnBox[0];
		vPosTemp[1] += pRules->pSpawnBox->fAreaCheck * iSpawnBox[1];
		vPosTemp[2] += pRules->pSpawnBox->fAreaCheck * iSpawnBox[2];
	}

	addVec3(vPosFinal,vSpawnPos,vSpawnPos);

	// Snap to the ground
	worldSnapPosToGround(iPartitionIdx, vPosTemp, 7, -7, &bFloorFound);

	// If no ground found, try again with a bigger range
	if (!bFloorFound) {
		worldSnapPosToGround(iPartitionIdx, vPosTemp, 20, -20, &bFloorFound);
	}
}

static void Entity_SetSpawnBox(Entity *pEntity, int piSpawnBox[3])
{
	if(pEntity)
	{
		pEntity->iBoxNumber[0] = piSpawnBox[0];
		pEntity->iBoxNumber[1] = piSpawnBox[1];
		pEntity->iBoxNumber[2] = piSpawnBox[2];
		printf("%s spawn box %d %d %d",pEntity->debugName,piSpawnBox[0],piSpawnBox[1],piSpawnBox[2]);
	}
}

void Entity_FindSpawnBox(Entity *pEntity, Vec3 vSpawnPos)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int iSpawnBox[3];
	int iSpawnBoxMax[3];
	int iSpawnBoxMin[3];
	
	RegionRules *pRules = RegionRulesFromVec3(vSpawnPos);

	if(pRules && pRules->pSpawnBox)
	{
		bool bFoundBox = false;
		//Center spawnbox
		iSpawnBox[0] = 0;
		iSpawnBox[1] = 0;
		iSpawnBox[2] = 0;
		//Set max and mins
		iSpawnBoxMax[0] = pRules->pSpawnBox->uiSpawnBoxes[0] / 2;
		iSpawnBoxMax[1] = pRules->pSpawnBox->uiSpawnBoxes[1] / 2;
		iSpawnBoxMax[2] = pRules->pSpawnBox->uiSpawnBoxes[2] / 2;

		iSpawnBoxMin[0] = 0 - pRules->pSpawnBox->uiSpawnBoxes[0] + iSpawnBoxMax[0];
		iSpawnBoxMin[1] = 0 - pRules->pSpawnBox->uiSpawnBoxes[1] + iSpawnBoxMax[1];
		iSpawnBoxMin[2] = 0 - pRules->pSpawnBox->uiSpawnBoxes[2] + iSpawnBoxMax[2];

		while(bFoundBox == false
			&& iSpawnBox[0] > iSpawnBoxMin[0]
			&& iSpawnBox[1] > iSpawnBoxMin[1]
			&& iSpawnBox[2] > iSpawnBoxMin[1])
		{
			int x,y,z;

			for(x=max(iSpawnBox[0]-1,iSpawnBoxMin[0]);x<min(abs(iSpawnBox[0])+1,iSpawnBoxMax[0]);x++)
			{
				for(y=max(iSpawnBox[1]-1,iSpawnBoxMin[1]);y<min(abs(iSpawnBox[1])+1,iSpawnBoxMax[1]);y++)
				{
					for(z=max(iSpawnBox[2]-1,iSpawnBoxMin[2]);z<min(abs(iSpawnBox[2])+1,iSpawnBoxMax[2]);z++)
					{
						Vec3 vSource;
						vSource[0] = x * pRules->pSpawnBox->fAreaCheck;
						vSource[1] = y * pRules->pSpawnBox->fAreaCheck;
						vSource[2] = z * pRules->pSpawnBox->fAreaCheck;

						if(entGridProximityLookupExEArray(entGetPartitionIdx(pEntity),vSource,NULL,pRules->pSpawnBox->fAreaCheck,0,ENTITYFLAG_DEAD,NULL) == 0)
						{
							iSpawnBox[0] = x;
							iSpawnBox[1] = y;
							iSpawnBox[2] = z;

							Entity_SetSpawnBox(pEntity,iSpawnBox);

							return;
						}
					}
				}
			}
			iSpawnBox[0]--;
			iSpawnBox[1]--;
			iSpawnBox[2]--;
		}
	}
#endif
}

RegionRules* getRegionRulesFromGenesisData(GenesisZoneMapData* genesis_data)
{
	//sfenton TODO: if we ever want to support different types, we need to review this
	if(eaSize(&genesis_data->solar_systems) > 0)	
		return  getRegionRulesFromRegionType( WRT_Space );
	if(eaSize(&genesis_data->genesis_interiors) > 0)	
		return  getRegionRulesFromRegionType( WRT_Ground );
	if(genesis_data->genesis_exterior || genesis_data->genesis_exterior_nodes)	
		return  getRegionRulesFromRegionType( WRT_Ground );
	
	return NULL;
}

bool Entity_IsValidControlSchemeForCurrentRegionEx(Entity* pEntity, const char* pchScheme, ControlSchemeRegionInfo** ppInfo)
{
	RegionRules *pRegionRules = pEntity ? getRegionRulesFromEnt(pEntity) : NULL;
	ControlSchemeRegionInfo* pInfo = NULL;

	if ( pEntity && pRegionRules==NULL )
	{
		Vec3 vPlayerPos;
		WorldRegion *pRegion;

		entGetPos(pEntity,vPlayerPos);

		pRegion = worldGetWorldRegionByPos(vPlayerPos);

		pRegionRules = getRegionRulesFromRegion(pRegion);
	}

	if ( pRegionRules )
	{
		pInfo = schemes_GetSchemeRegionInfo( pRegionRules->eSchemeRegionType );
	}

	if ( ppInfo )
		(*ppInfo) = pInfo;

	if ( pInfo && pInfo->ppchAllowedSchemes )
	{
		S32 i;
		for ( i = 0; i < eaSize(&pInfo->ppchAllowedSchemes); i++ )
		{
			if ( stricmp(pInfo->ppchAllowedSchemes[i],pchScheme) == 0 )
			{
				break;
			}
		}
		if ( i == eaSize(&pInfo->ppchAllowedSchemes) )
			return false;
	}

	return true;
}

bool Entity_IsValidControlSchemeForCurrentRegion(Entity* pEntity, const char* pchScheme)
{
	return Entity_IsValidControlSchemeForCurrentRegionEx( pEntity, pchScheme, NULL );
}

// Note: this is a hack for now, just use the first world region in the list to get the region rules
// Passing in NULL gets the region rules for the current map.
RegionRules* getRegionRulesFromZoneMap( ZoneMapInfo* pNextZoneMap )
{
	if (zmapInfoGetGenesisData(pNextZoneMap)) {
		return getRegionRulesFromGenesisData(zmapInfoGetGenesisData(pNextZoneMap));
	} else {
		WorldRegion** eaWorldRegions = zmapInfoGetWorldRegions( pNextZoneMap );

		if ( eaSize( &eaWorldRegions ) > 0 )
			return getRegionRulesFromRegion( eaWorldRegions[0] );
	}

	return NULL;
}

RegionRules* getValidRegionRulesForCharacterClassType(U32 eClassType)
{
	RefDictIterator Iter = {0};
	RegionRules* pRules;

	RefSystem_InitRefDictIterator(g_hRegionRulesDict, &Iter);
	
	while (pRules = RefSystem_GetNextReferentFromIterator(&Iter))
	{
		if (eaiFind(&pRules->peCharClassTypes, eClassType) >= 0)
		{
			return pRules;
		}
	}
	return NULL;
}

#include "AutoGen/RegionRules_h_ast.c"
