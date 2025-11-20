/***************************************************************************
*     Copyright (c) 2003-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "SavedPetCommon.h"
#include "Entity.h"
#include "entCritter.h"
#include "EntitySavedData.h"
#include "EntityLib.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Character_tick.h"
#include "DoorTransitionCommon.h"
#include "GlobalTypes.h"
#include "GameStringFormat.h"
#include "interaction_common.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "RegionRules.h"
#include "PowerModes.h"
#include "Powers.h"
#include "PowerHelpers.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "mission_common.h"
#include "StringUtil.h"
#include "Player.h"
#include "OfficerCommon.h"
#include "GameAccountDataCommon.h"
#if GAMESERVER
#include "inventoryTransactions.h"
#include "GameAccountData\GameAccountData.h"
#endif
#ifdef GAMECLIENT
#include "UIGen.h"
#endif

#include "Message.h"
#include "CombatEnums.h"
#include "CombatEnums_h_ast.h"

#include "ResourceManager.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringFormat.h"
#include "mapstate_common.h"

#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/SavedPetCommon_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/Powers_h_ast.h"
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle *g_hAlwaysPropSlotDefDict;
DefineContext *g_pDefineAlwaysPropSlotCategories = NULL;
DefineContext *g_pDefineAlwaysPropSlotClassRestrict = NULL;
DefineContext *g_pDefinePetAcquireLimit = NULL;
AlwaysPropSlotClassRestrictDefs g_AlwaysPropSlotClassRestrictDefs = {0};
PetRestrictions g_PetRestrictions = {0};

AlwaysPropSlotClassRestrictDef* AlwaysPropSlot_GetClassRestrictDef(AlwaysPropSlotClassRestrictType eType)
{
	int i;
	for (i = eaSize(&g_AlwaysPropSlotClassRestrictDefs.eaRestrictDefs)-1; i >= 0; i--)
	{
		AlwaysPropSlotClassRestrictDef* pRestrictDef = g_AlwaysPropSlotClassRestrictDefs.eaRestrictDefs[i];
		if (pRestrictDef->eClassRestrictType == eType)
		{
			return pRestrictDef;
		}
	}
	return NULL;
}

Entity *SavedPet_GetEntityEx(int iPartitionIdx, const PetRelationship *pRelationship, S32 bGetOwner)
{
	Entity *pEntity = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET,pRelationship->conID);

	if(!pEntity)
	{
		pEntity = GET_REF(pRelationship->hPetRef);
		if (pEntity && pEntity->pChar && !pEntity->pChar->pEntParent)
		{
			pEntity->pChar->pEntParent = pEntity;
		}
	}
	if(pEntity && pEntity->pSaved)
	{
		if (bGetOwner)
		{
			Entity *pOwner = entFromContainerID(iPartitionIdx, pEntity->pSaved->conOwner.containerType,pEntity->pSaved->conOwner.containerID);

			if(pOwner && pOwner->pSaved && pOwner->pSaved->pPuppetMaster)
			{
				if(pOwner->pSaved->pPuppetMaster->curID == entGetContainerID(pEntity) && pOwner->pSaved->pPuppetMaster->curType == entGetType(pEntity))
					return pOwner;
			}
		}
		return pEntity;
	}

	return NULL;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets");
NOCONST(PetRelationship) *trhSavedPet_GetPetFromContainerID(ATH_ARG NOCONST(Entity)* pEnt, U32 uiID, bool bPetsOnly)
{
	int i;
	for (i = 0; i < eaSize(&pEnt->pSaved->ppOwnedContainers); i++)
	{
		if (!bPetsOnly || !trhSavedPet_IsPetAPuppet(pEnt, pEnt->pSaved->ppOwnedContainers[i]))
		{
			if (uiID == pEnt->pSaved->ppOwnedContainers[i]->conID)
			{
				return pEnt->pSaved->ppOwnedContainers[i];
			}
		}
	}
	return NULL;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Pppuppets");
NOCONST(PuppetEntity) *trhSavedPet_GetPuppetFromContainerID(ATH_ARG NOCONST(Entity) *pEnt, ContainerID uiID)
{
	if (NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pPuppetMaster))
	{
		int i;
		for (i=0; i < eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets); i++)
		{
			if (pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID == uiID)
			{
				return pEnt->pSaved->pPuppetMaster->ppPuppets[i];
			}
		}
	}
	return NULL;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pMasterEnt, ".Psaved.Ppuppetmaster.Pppuppets")
	ATR_LOCKS(pRelationship, ".Conid");
NOCONST(PuppetEntity)* trhSavedPet_GetPuppetFromPet(ATH_ARG NOCONST(Entity) *pMasterEnt, ATH_ARG NOCONST(PetRelationship) *pRelationship)
{
	ContainerID uiID = pRelationship->conID;
	int i;

	if (ISNULL(pMasterEnt) || ISNULL(pMasterEnt->pSaved) || ISNULL(pMasterEnt->pSaved->pPuppetMaster))
		return NULL;

	for(i=0;i<eaSize(&pMasterEnt->pSaved->pPuppetMaster->ppPuppets);i++)
	{
		if(pMasterEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID == uiID)
		{
			return pMasterEnt->pSaved->pPuppetMaster->ppPuppets[i];
		}
	}

	return NULL;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pMasterEnt, ".Psaved.Ppuppetmaster.Pppuppets")
	ATR_LOCKS(pRelationship, ".Conid");
bool trhSavedPet_IsPetAPuppet(ATH_ARG NOCONST(Entity) *pMasterEnt, ATH_ARG NOCONST(PetRelationship) *pRelationship)
{
	return NONNULL(trhSavedPet_GetPuppetFromPet(pMasterEnt, pRelationship));
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets")
	ATR_LOCKS(pTree, ".Ppnodes");
void Entity_PuppetCopy_FixPowerTreeIDs(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree)
{
	int n;

	if(ISNULL(pTree))
		return;

	for(n=0;n<eaSize(&pTree->ppNodes);n++)
	{
		NOCONST(PTNode) *pNode = pTree->ppNodes[n];
		int p;

		if(ISNULL(pNode))
			continue;

		for(p=0;p<eaSize(&pNode->ppPowers);p++)
		{
			if(NONNULL(pNode->ppPowers[p]))
			{
				U32 uiID = entity_GetNewPowerIDHelper(pEnt);
				power_SetIDHelper(pNode->ppPowers[p], uiID);
			}

		}
		for(p=0;p<eaSize(&pNode->ppEnhancements);p++)
		{
			if(NONNULL(pNode->ppEnhancements[p]))
			{
				U32 uiID = entity_GetNewPowerIDHelper(pEnt);
				power_SetIDHelper(pNode->ppEnhancements[p], uiID);
			}
		}
	}
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pSrc, ".Pinventoryv2")
	ATR_LOCKS(pDest, ".Pinventoryv2");
void Entity_PuppetCopy_Inventory(ATH_ARG NOCONST(Entity) *pSrc, ATH_ARG NOCONST(Entity) *pDest)
{
	NOCONST(InventoryBag) **ppTempBagsDest=NULL;
	NOCONST(InventoryBag) **ppTempBagsSrc=NULL;
	//All lite bags behave as though they are nocopy
	NOCONST(InventoryBagLite) **pSrcLiteBags=NULL;
	NOCONST(InventoryBagLite) **pDstLiteBags=NULL;
	int i;

	if(pDest->pInventoryV2)
	{
		for(i=eaSize(&pDest->pInventoryV2->ppInventoryBags)-1;i>=0;i--)
		{
			NOCONST(InventoryBag) *pBag = pDest->pInventoryV2->ppInventoryBags[i];

			if(invbag_trh_def(pBag) && (invbag_trh_flags(pBag) & InvBagFlag_NoCopy))
			{
				eaPush(&ppTempBagsDest,pBag);
				eaRemove(&pDest->pInventoryV2->ppInventoryBags,i);
			}
		}
		pDstLiteBags = pDest->pInventoryV2->ppLiteBags;
		pDest->pInventoryV2->ppLiteBags = NULL;
	}


	if(pSrc->pInventoryV2)
	{
		for(i=eaSize(&pSrc->pInventoryV2->ppInventoryBags)-1;i>=0;i--)
		{
			NOCONST(InventoryBag) *pBag = pSrc->pInventoryV2->ppInventoryBags[i];

			if(invbag_trh_def(pBag) && invbag_trh_flags(pBag) & InvBagFlag_NoCopy)
			{
				eaPush(&ppTempBagsSrc,pBag);
				eaRemove(&pSrc->pInventoryV2->ppInventoryBags,i);
			}
		}
		pSrcLiteBags = pSrc->pInventoryV2->ppLiteBags;
		pSrc->pInventoryV2->ppLiteBags = NULL;
	}

	if(!pDest->pInventoryV2 && pSrc->pInventoryV2)
		pDest->pInventoryV2 = StructCreateNoConst(parse_Inventory);

	if(pSrc->pInventoryV2 && pDest->pInventoryV2)
	{
		StructCopyNoConst(parse_Inventory,pSrc->pInventoryV2,pDest->pInventoryV2,0, TOK_PERSIST, TOK_NO_TRANSACT + TOK_PUPPET_NO_COPY);

		pSrc->pInventoryV2->ppLiteBags = pSrcLiteBags;
		pDest->pInventoryV2->ppLiteBags = pDstLiteBags;

	}
	else if(!pSrc->pInventoryV2)
	{
		StructDestroyNoConst(parse_Inventory,pDest->pInventoryV2);
		pDest->pInventoryV2 = NULL;
	}

	for(i=eaSize(&ppTempBagsDest)-1;i>=0;i--)
	{
		if(!pDest->pInventoryV2)
			pDest->pInventoryV2 = StructCreateNoConst(parse_Inventory);
		eaPush(&pDest->pInventoryV2->ppInventoryBags,ppTempBagsDest[i]);
	}
	eaDestroy(&ppTempBagsDest);

	for(i=eaSize(&ppTempBagsSrc)-1;i>=0;i--)
	{
		eaPush(&pSrc->pInventoryV2->ppInventoryBags,ppTempBagsSrc[i]);
	}
	eaDestroy(&ppTempBagsSrc);
}

Entity *SavedPuppet_GetEntity(int iPartitionIdx, PuppetEntity *pPuppet)
{
	Entity *pEntity = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYSAVEDPET,pPuppet->curID);

	if(!pEntity)
		pEntity = GET_REF(pPuppet->hEntityRef);

	return pEntity;
}

Entity *SavedCritter_GetEntity(int iPartitionIdx, CritterPetRelationship *pCritterRelationship)
{
	if ( pCritterRelationship )
	{
		if ( pCritterRelationship->pEntity )
		{
			return pCritterRelationship->pEntity;
		}
		else
		{
			return entFromEntityRef(iPartitionIdx, pCritterRelationship->erPet);
		}
	}

	return NULL;
}

CritterPetRelationship* Entity_FindSavedCritterByRef(Entity* pOwner, EntityRef erCritter)
{
	if ( pOwner && pOwner->pSaved )
	{
		S32 i, iCritterArraySize = eaSize(&pOwner->pSaved->ppCritterPets);
		for ( i = 0; i < iCritterArraySize; i++ )
		{
			if ( pOwner->pSaved->ppCritterPets[i]->erPet == erCritter )
			{
				return pOwner->pSaved->ppCritterPets[i];
			}
		}
	}

	return NULL;
}

CritterPetRelationship* Entity_FindSavedCritterByID(Entity* pOwner, U32 uiCritterPetID)
{
	if ( pOwner && pOwner->pSaved )
	{
		S32 i, iCritterArraySize = eaSize(&pOwner->pSaved->ppCritterPets);
		for ( i = 0; i < iCritterArraySize; i++ )
		{
			if ( pOwner->pSaved->ppCritterPets[i]->uiPetID == uiCritterPetID )
			{
				return pOwner->pSaved->ppCritterPets[i];
			}
		}
	}

	return NULL;
}

Entity* Entity_FindSavedCritterEntityByID(Entity* pOwner, U32 uiCritterPetID)
{
	CritterPetRelationship* pCritterPet = Entity_FindSavedCritterByID(pOwner, uiCritterPetID);

	return SavedCritter_GetEntity(entGetPartitionIdx(pOwner), pCritterPet);
}

PetDef* Entity_FindAllowedCritterPetDefByID(Entity* pOwner, U32 uiPetID)
{
	if ( pOwner && pOwner->pSaved )
	{
		S32 i, iCritterArraySize = eaSize(&pOwner->pSaved->ppAllowedCritterPets);
		for ( i = 0; i < iCritterArraySize; i++ )
		{
			if ( pOwner->pSaved->ppAllowedCritterPets[i]->uiPetID == uiPetID )
			{
				return GET_REF( pOwner->pSaved->ppAllowedCritterPets[i]->hPet );
			}
		}
	}

	return NULL;
}

PetDefRefCont* Entity_FindAllowedCritterPetByDef(Entity *pOwner, PetDef *pPetDef)
{
	if (pOwner && pOwner->pSaved )
	{
		FOR_EACH_IN_EARRAY(pOwner->pSaved->ppAllowedCritterPets, PetDefRefCont, pPetDefRef)
		{
			if (GET_REF(pPetDefRef->hPet) == pPetDef)
				return pPetDefRef;
		}
		FOR_EACH_END
	}
	return NULL;
}

AUTO_RUN;
void RegisterAlwaysPropSlotDict(void)
{
	g_hAlwaysPropSlotDefDict = RefSystem_RegisterSelfDefiningDictionary("AlwaysPropSlotDef", false, parse_AlwaysPropSlotDef, true, true, NULL);

	if(IsServer())
	{
		resDictProvideMissingResources(g_hAlwaysPropSlotDefDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hAlwaysPropSlotDefDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
	else
	{
		resDictRequestMissingResources(g_hAlwaysPropSlotDefDict, 16, false, resClientRequestSendReferentCommand );
	}
}

static void AlwaysPropSlotLoadCategories_Internal(const char *pchPath, S32 iWhen)
{
	int i, s;
	AlwaysPropSlotCategories Categories = {0};

	if (g_pDefineAlwaysPropSlotCategories)
	{
		DefineDestroy(g_pDefineAlwaysPropSlotCategories);
	}
	g_pDefineAlwaysPropSlotCategories = DefineCreate();

	loadstart_printf("Loading AlwaysPropSlotCategories...");
	
	ParserLoadFiles(NULL, 
					"defs/config/AlwaysPropSlotCategories.def", 
					"AlwaysPropSlotCategories.bin", 
					PARSER_OPTIONALFLAG, 
					parse_AlwaysPropSlotCategories, 
					&Categories);
	
	s = eaSize(&Categories.ppCategories);
	for (i = 0; i < s; i++)
	{
		DefineAddInt(g_pDefineAlwaysPropSlotCategories,Categories.ppCategories[i],i+1);
	}
	loadend_printf(" done (%d AlwaysPropSlotCategories).", s);

	StructDeInit(parse_AlwaysPropSlotCategories, &Categories);
}

static void AlwaysPropSlotLoadClassRestrict_Internal(const char *pchPath, S32 iWhen)
{
	int i, s;

	if (g_pDefineAlwaysPropSlotClassRestrict)
	{
		DefineDestroy(g_pDefineAlwaysPropSlotClassRestrict);
	}
	g_pDefineAlwaysPropSlotClassRestrict = DefineCreate();
	StructReset(parse_AlwaysPropSlotClassRestrictDefs, &g_AlwaysPropSlotClassRestrictDefs);

	loadstart_printf("Loading AlwaysPropSlotClassRestrict...");
	
	ParserLoadFiles(NULL, 
					"defs/config/AlwaysPropSlotClassRestrict.def", 
					"AlwaysPropSlotClassRestrict.bin", 
					PARSER_OPTIONALFLAG, 
					parse_AlwaysPropSlotClassRestrictDefs, 
					&g_AlwaysPropSlotClassRestrictDefs);
	
	s = eaSize(&g_AlwaysPropSlotClassRestrictDefs.eaRestrictDefs);
	for (i = 0; i < s; i++)
	{
		const char* pchName = g_AlwaysPropSlotClassRestrictDefs.eaRestrictDefs[i]->pchName;
		S32 eType = i+1;
		g_AlwaysPropSlotClassRestrictDefs.eaRestrictDefs[i]->eClassRestrictType = eType;
		DefineAddInt(g_pDefineAlwaysPropSlotClassRestrict,pchName,eType);
	}
	loadend_printf(" done (%d AlwaysPropSlotClassRestrictTypes).", s);
}

static void AlwaysPropSlotLoadCategories(void)
{
	AlwaysPropSlotLoadClassRestrict_Internal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/AlwaysPropSlotClassRestrict.def", AlwaysPropSlotLoadClassRestrict_Internal);

	AlwaysPropSlotLoadCategories_Internal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/AlwaysPropSlotCategories.def", AlwaysPropSlotLoadCategories_Internal);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(AlwaysPropSlotClassRestrictTypeEnum, "PropSlotClassRestrict_");
	ui_GenInitStaticDefineVars(AlwaysPropSlotCategoryEnum, "PropSlotCategory_");
#endif
}

static void AlwaysPropSlotDefReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading AlwaysProp Dict...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_hAlwaysPropSlotDefDict);

	loadend_printf(" done (%d AlwaysProp Defs)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hAlwaysPropSlotDefDict));
}

void AlwaysPropSlotLoad(void)
{
	AlwaysPropSlotLoadCategories();
	
	resLoadResourcesFromDisk(g_hAlwaysPropSlotDefDict, NULL, "defs/config/AlwaysPropSlot.def", "AlwaysPropSlot.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/AlwaysPropSlot.def", AlwaysPropSlotDefReload);
	}
}

static void PetRestrictions_Validate(void)
{
	S32 i;
	if (g_PetRestrictions.iRequiredPuppetRequestCount > eaSize(&g_PetRestrictions.eaAllowedPuppetRequests))
	{
		Errorf("PetRestrictions: Required puppet request count (%d) is greater than the number of choices (%d)",
			g_PetRestrictions.iRequiredPuppetRequestCount, eaSize(&g_PetRestrictions.eaAllowedPuppetRequests));
	}
	if (g_PetRestrictions.iRequiredPetRequestCount > eaSize(&g_PetRestrictions.eaAllowedPetRequests))
	{
		Errorf("PetRestrictions: Required pet request count (%d) is greater than the number of choices (%d)",
			g_PetRestrictions.iRequiredPetRequestCount, eaSize(&g_PetRestrictions.eaAllowedPetRequests));
	}
	for (i = eaSize(&g_PetRestrictions.eaAllowedPuppetRequests)-1; i >= 0; i--)
	{
		PuppetRequestChoice* pChoice = g_PetRestrictions.eaAllowedPuppetRequests[i];
		if (pChoice->pcAllegiance && !RefSystem_ReferentFromString("Allegiance", pChoice->pcAllegiance))
		{
			Errorf("PetRestrictions: Invalid allegiance specified (%s) for puppet request choice",
				pChoice->pcAllegiance);
		}
		if (pChoice->pcCritterDef && !RefSystem_ReferentFromString("CritterDef", pChoice->pcCritterDef))
		{
			Errorf("PetRestrictions: Invalid CritterDef specified (%s) for puppet request choice",
				pChoice->pcCritterDef);
		}
	}
	for (i = eaSize(&g_PetRestrictions.eaAllowedPetRequests)-1; i >= 0; i--)
	{
		PetRequestChoice* pChoice = g_PetRestrictions.eaAllowedPetRequests[i];
		if (pChoice->pcAllegiance && !RefSystem_ReferentFromString("Allegiance", pChoice->pcAllegiance))
		{
			Errorf("PetRestrictions: Invalid allegiance specified (%s) for pet request choice",
				pChoice->pcAllegiance);
		}
		if (pChoice->pcPetDef && !RefSystem_ReferentFromString("PetDef", pChoice->pcPetDef))
		{
			Errorf("PetRestrictions: Invalid PetDef specified (%s) for pet request choice",
				pChoice->pcPetDef);
		}
	}

	if (g_PetRestrictions.pchRequiredItemForDeceasedPets)
	{
		if(!item_DefFromName(g_PetRestrictions.pchRequiredItemForDeceasedPets))
		{
			Errorf("PetRestrictions: Invalid ItemDef specified (%s) for RequiredItemForDeceasedPets",
				g_PetRestrictions.pchRequiredItemForDeceasedPets);
			g_PetRestrictions.pchRequiredItemForDeceasedPets = NULL;
		}
	}
	for (i = eaSize(&g_PetRestrictions.eaPetIntroWarp)-1; i >= 0; i--)
	{
		PetIntroductionWarp* pWarp = g_PetRestrictions.eaPetIntroWarp[i];
		if (!pWarp->pchMapName || !pWarp->pchMapName[0])
		{
			Errorf("PetRestrictions: Invalid map name specified for pet introduction warp");
		}
		if (pWarp->pchAllegiance && !RefSystem_ReferentFromString("Allegiance", pWarp->pchAllegiance))
		{
			Errorf("PetRestrictions: Invalid allegiance %s specified for pet introduction warp", pWarp->pchAllegiance);
		}
		if (pWarp->pchTransSequence && !DoorTransitionSequence_DefFromName(pWarp->pchTransSequence))
		{
			Errorf("PetRestrictions: Invalid transition sequence %s specified for pet introduction warp", pWarp->pchTransSequence);
		}
	}
	for (i = eaSize(&g_PetRestrictions.eaPetAcquireLimits)-1; i >= 0; i--)
	{
		PetAcquireLimitDef* pAcquireLimit = g_PetRestrictions.eaPetAcquireLimits[i];
		if (!pAcquireLimit->pchName || !pAcquireLimit->pchName[0])
		{
			Errorf("PetRestrictions: No name specified for pet acquire limit");
		}
		if (!pAcquireLimit->iMaxCount)
		{
			Errorf("PetRestrictions: Pet acquire limit %s must have a non-zero MaxCount value", pAcquireLimit->pchName);
		}
	}
}

AUTO_STARTUP(PetRestrictions) ASTRT_DEPS(AS_CharacterClassTypes);
void PetRestrictionsLoad(void)
{
	int i;

	loadstart_printf("Loading PetRestrictions");

	if (g_pDefinePetAcquireLimit)
	{
		DefineDestroy(g_pDefinePetAcquireLimit);
	}
	g_pDefinePetAcquireLimit = DefineCreate();

	ParserLoadFiles(NULL, "defs/config/PetRestrictions.def", "PetRestrictions.bin", PARSER_OPTIONALFLAG, parse_PetRestrictions, &g_PetRestrictions);

	for (i = 0; i < eaSize(&g_PetRestrictions.eaPetAcquireLimits); i++)
	{
		PetAcquireLimitDef* pAcquireLimit = g_PetRestrictions.eaPetAcquireLimits[i];
		pAcquireLimit->eAcquireLimitType = i+1;
		DefineAddInt(g_pDefinePetAcquireLimit, pAcquireLimit->pchName, pAcquireLimit->eAcquireLimitType);
	}
	loadend_printf(" done (PetRestrictions).");
}

AUTO_STARTUP(PetRestrictionsValidate) ASTRT_DEPS(PetStore, DoorTransitionSequence);
void PetRestrictionsValidate(void)
{
	if (isDevelopmentMode() && IsGameServerBasedType())
	{
		PetRestrictions_Validate();
	}
}

int CompareAlwaysPropSlot(const AlwaysPropSlot** left, const AlwaysPropSlot** right)
{
	return (*left)->iSlotID - (*right)->iSlotID;
}

void Entity_getAllPowerTreesFromPuppets(SA_PARAM_NN_VALID Entity *pEnt, PowerTree ***pppPowerTreeOut)
{
	//Find all power trees on current entity
	int i;
	int iPartitionIdx;

	for(i=0;i<eaSize(&pEnt->pChar->ppPowerTrees);i++)
	{
		eaPush(pppPowerTreeOut,pEnt->pChar->ppPowerTrees[i]);
	}

	if(!pEnt->pSaved
		|| !pEnt->pSaved->pPuppetMaster)
		return;

	//Go though puppets and get power trees
	iPartitionIdx = entGetPartitionIdx(pEnt);
	for(i=0;i<eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets);i++)
	{
		Entity *pPuppetEnt = SavedPuppet_GetEntity(iPartitionIdx, pEnt->pSaved->pPuppetMaster->ppPuppets[i]);
		int j;

		if(!pPuppetEnt 
			|| !pPuppetEnt->pChar)
			continue;

		if(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->eState != PUPPETSTATE_ACTIVE)
			continue;

		if(pPuppetEnt == pEnt
			|| pPuppetEnt->myContainerID == pEnt->pSaved->pPuppetMaster->curID)
			continue;

		for(j=0;j<eaSize(&pPuppetEnt->pChar->ppPowerTrees);j++)
		{
			eaPush(pppPowerTreeOut,pPuppetEnt->pChar->ppPowerTrees[j]);
		}
	}

	return;
}

void Entity_GetPetIDList(Entity* pEnt, U32** peaPets)
{
	if (pEnt && pEnt->pSaved)
	{
		S32 i, iSize = eaSize(&pEnt->pSaved->ppOwnedContainers);
		for (i = 0; i < iSize; i++)
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
			PuppetEntity* pPuppet = SavedPet_GetPuppetFromPet(pEnt, pPet);
			if (pPuppet)
			{
				if (pPuppet->curType != pEnt->pSaved->pPuppetMaster->curType ||	
					pPuppet->curID != pEnt->pSaved->pPuppetMaster->curID)
				{
					ea32Push(peaPets, pPuppet->curID);
				}
			}
			else if (pPet->conID)
			{
				ea32Push(peaPets, pPet->conID);
			}
		}
	}
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets");
S32 trhEntity_CountPets(ATH_ARG NOCONST(Entity) *pEntity, bool bCountPets, bool bCountPuppets, bool bCheckClassType)
{
	S32 i, iCount = 0;
	if (ISNULL(pEntity) || ISNULL(pEntity->pSaved))
		return 0;
	for (i = eaSize(&pEntity->pSaved->ppOwnedContainers)-1; i >= 0; i--)
	{
		NOCONST(PuppetEntity)* pPuppet = trhSavedPet_GetPuppetFromPet(pEntity, pEntity->pSaved->ppOwnedContainers[i]);
		if (NONNULL(pPuppet) && bCountPuppets)
		{
			if(!bCheckClassType || eaiFind(&g_PetRestrictions.pePuppetType,pPuppet->eType) != -1)
			{
				iCount++;
			}
		}
		else if (ISNULL(pPuppet) && bCountPets)
		{
			//TODO(MK): need the entity to check the class type
			iCount++;
		}
	}
	return iCount;
}

S32 Entity_CountPetsWithState(SA_PARAM_NN_VALID Entity* pEntity, OwnedContainerState eState, bool bCountPuppets)
{
	S32 i, iCount = 0;
	int iPartitionIdx;

	if (!pEntity->pSaved)
		return 0;

	iPartitionIdx = entGetPartitionIdx(pEntity);
	for (i = eaSize(&pEntity->pSaved->ppOwnedContainers)-1; i >= 0; i--)
	{
		Entity* pPetEnt;
		OwnedContainerState ePetState;

		if (!bCountPuppets && SavedPet_IsPetAPuppet( pEntity, pEntity->pSaved->ppOwnedContainers[i]))
			continue;

		pPetEnt = SavedPet_GetEntity(iPartitionIdx, pEntity->pSaved->ppOwnedContainers[i]);
		ePetState = pEntity->pSaved->ppOwnedContainers[i]->eState;

		if (ePetState != eState)
			continue;

		iCount++;
	}

	return iCount;
}

S32 Entity_CountPetsOfType(SA_PARAM_NN_VALID Entity* pEntity, CharClassTypes eType, bool bCountPuppets)
{
	S32 i, iCount = 0;
	int iPartitionIdx;

	if (!pEntity->pSaved)
		return 0;

	iPartitionIdx = entGetPartitionIdx(pEntity);
	for (i = eaSize(&pEntity->pSaved->ppOwnedContainers)-1; i >= 0; i--)
	{
		Entity* pPetEnt;
		CharClassTypes eClass;

		if (!bCountPuppets && SavedPet_IsPetAPuppet( pEntity, pEntity->pSaved->ppOwnedContainers[i]))
			continue;

		pPetEnt = SavedPet_GetEntity(iPartitionIdx, pEntity->pSaved->ppOwnedContainers[i]);
		eClass = GetCharacterClassEnum(pPetEnt);

		if (eClass != eType)
			continue;

		iCount++;
	}

	return iCount;
}

bool entity_puppetSwapComplete(Entity *pEnt)
{
	PuppetMaster *pPuppetMaster = pEnt && pEnt->pSaved ? pEnt->pSaved->pPuppetMaster : NULL;
	
	if(pPuppetMaster)
	{
		if(!pPuppetMaster->bPuppetCheckPassed)
			return false;

		if(pPuppetMaster->bSkippedSuccessOnLogin)
			return false;

		if(entCheckFlag(pEnt,ENTITYFLAG_PUPPETPROGRESS))
			return false;

		if (pPuppetMaster->expectedID)
		{
			if (pPuppetMaster->expectedType == GLOBALTYPE_ENTITYCRITTER)
			{
				if (pPuppetMaster->expectedID != pPuppetMaster->curTempID)
				{
					return false;
				}
			}
			else if (pPuppetMaster->expectedType != pPuppetMaster->curType ||
					 pPuppetMaster->expectedID != pPuppetMaster->curID)
			{
				return false;
			}
		}
	}

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
bool trhEntity_HasMaxAllowedPuppets(ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract)
{
	S32 iCount = trhEntity_CountPets(pEnt, false, true, true);
	S32 iExtraPuppets = trhOfficer_GetExtraPuppets(pEnt, pExtract);
	return iCount >= g_PetRestrictions.iMaxPuppets + iExtraPuppets;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
bool trhEntity_CanAddPuppet(ATH_ARG NOCONST(Entity)* pEnt, const char* pchClass, GameAccountDataExtract *pExtract)
{
	CharacterClass *pClass = characterclasses_FindByName((char*)pchClass);
	CharClassTypes eClassType = pClass ? pClass->eType : CharClassTypes_None;

	if (eaiFind(&g_PetRestrictions.pePuppetType,eClassType) == -1)
		return true;

	return !trhEntity_HasMaxAllowedPuppets(pEnt, pExtract);
}

// Keep in sync with trhEntity_HasMaxAllowedPets
bool Entity_HasMaxAllowedPets(SA_PARAM_NN_VALID Entity* pEnt)
{
	S32 i, iCount = 0;
	int iPartitionIdx;

	if (!pEnt->pSaved || !g_PetRestrictions.iMaxCount)
		return false;

	iPartitionIdx = entGetPartitionIdx(pEnt);
	for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
	{
		Entity* pPetEnt = SavedPet_GetEntity(iPartitionIdx, pEnt->pSaved->ppOwnedContainers[i]);
		CharClassTypes ePetClass = GetCharacterClassEnum(pPetEnt);

		if (eaiFind(&g_PetRestrictions.peClassType,ePetClass) != -1)
		{
			iCount++;
		}
	}
	return iCount >= g_PetRestrictions.iMaxCount;
}

// Keep in sync with Entity_HasMaxAllowedPets
AUTO_TRANS_HELPER
	ATR_LOCKS(eaPets, ".Pchar");
bool trhEntity_HasMaxAllowedPets(ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets)
{
	S32 i, iCount = 0;

	if (!g_PetRestrictions.iMaxCount)
		return false;

	for (i = eaSize(&eaPets)-1; i >= 0; i--)
	{
		if (NONNULL(eaPets[i]->pChar))
		{
			CharacterClass* pClass = GET_REF(eaPets[i]->pChar->hClass);
			if (NONNULL(pClass))
			{
				if (eaiFind(&g_PetRestrictions.peClassType,pClass->eType) != -1)
				{
					iCount++;
				}
			}
		}
	}
	return iCount >= g_PetRestrictions.iMaxCount;
}

// Keep in sync with trhEntity_CanAddPet
bool Entity_CanAddPet(SA_PARAM_NN_VALID Entity* pEnt, const char* pchClass)
{
	CharacterClass* pClass = characterclasses_FindByName((char*)pchClass);
	CharClassTypes eClassType = pClass ? pClass->eType : CharClassTypes_None;

	if (eaiFind(&g_PetRestrictions.peClassType,eClassType) == -1)
		return true;

	return !Entity_HasMaxAllowedPets(pEnt);
}

// Keep in sync with Entity_CanAddPet
AUTO_TRANS_HELPER
	ATR_LOCKS(eaPets, ".Pchar");
bool trhEntity_CanAddPet(ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, const char* pchClass)
{
	CharacterClass* pClass = characterclasses_FindByName((char*)pchClass);
	CharClassTypes eClassType = pClass ? pClass->eType : CharClassTypes_None;

	if (eaiFind(&g_PetRestrictions.peClassType,eClassType) == -1)
		return true;

	return !trhEntity_HasMaxAllowedPets(eaPets);
}

static PetAcquireLimitDef* SavedPet_GetAcquireLimit(PetAcquireLimit eAcquireLimit)
{
	if (eAcquireLimit != kPetAcquireLimit_None)
	{
		return eaGet(&g_PetRestrictions.eaPetAcquireLimits, eAcquireLimit-1);
	}
	return NULL;
}

AUTO_TRANS_HELPER;
bool trhEntity_CheckAcquireLimit(ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, PetDef* pPetDef, U64 uSrcItemID)
{
	PetAcquireLimitDef* pAcquireLimit = SavedPet_GetAcquireLimit(pPetDef->eAcquireLimit);
	if (pAcquireLimit)
	{
		S32 i, j, iCount = 0;
		for (i = eaSize(&eaPets)-1; i >= 0; i--)
		{
			if (NONNULL(eaPets[i]->pCritter))
			{
				PetDef* pDef = GET_REF(eaPets[i]->pCritter->petDef);
				if (pDef && pDef->eAcquireLimit == pPetDef->eAcquireLimit)
				{
					if (++iCount >= pAcquireLimit->iMaxCount)
					{
						return true;
					}
				}
			}
		}
		if (NONNULL(pEnt->pInventoryV2))
		{
			for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
			{
				NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[i];
				for (j = eaSize(&pBag->ppIndexedInventorySlots)-1; j >= 0; j--)
				{
					NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[j];
					ItemDef* pItemDef = NONNULL(pSlot->pItem) ? GET_REF(pSlot->pItem->hItem) : NULL;
					if (pItemDef && (!uSrcItemID || ISNULL(pSlot->pItem) || pSlot->pItem->id != uSrcItemID))
					{
						PetDef* pDef = GET_REF(pItemDef->hPetDef);
						if (pDef && pDef->eAcquireLimit == pPetDef->eAcquireLimit)
						{
							if (++iCount >= pAcquireLimit->iMaxCount)
							{
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

bool Entity_CheckAcquireLimit(Entity* pEnt, PetDef* pPetDef, U64 uSrcItemID)
{
	PetAcquireLimitDef* pAcquireLimit = SavedPet_GetAcquireLimit(pPetDef->eAcquireLimit);
	if (pAcquireLimit)
	{
		S32 i, j, iCount = 0;
		if (pEnt->pSaved)
		{
			for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
			{
				PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
				Entity* pPetEnt = GET_REF(pPet->hPetRef);
				if (pPetEnt && pPetEnt->pCritter)
				{
					PetDef* pDef = GET_REF(pPetEnt->pCritter->petDef);
					if (pDef && pDef->eAcquireLimit == pPetDef->eAcquireLimit)
					{
						if (++iCount >= pAcquireLimit->iMaxCount)
						{
							return true;
						}
					}
				}
			}
		}
		if (pEnt->pInventoryV2)
		{
			for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
			{
				InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];
				for (j = eaSize(&pBag->ppIndexedInventorySlots)-1; j >= 0; j--)
				{
					InventorySlot* pSlot = pBag->ppIndexedInventorySlots[j];
					ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
					if (pItemDef && (!uSrcItemID || !pSlot->pItem || pSlot->pItem->id != uSrcItemID))
					{
						PetDef* pDef = GET_REF(pItemDef->hPetDef);
						if (pDef && pDef->eAcquireLimit == pPetDef->eAcquireLimit)
						{
							if (++iCount >= pAcquireLimit->iMaxCount)
							{
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

//Keep this in sync with trhEntity_CanAddSavedPet
bool Entity_CanAddSavedPet(Entity *pEnt, PetDef *pPetDef, U64 uSrcItemID, bool bAddAsPuppet, GameAccountDataExtract *pExtract, AddSavedPetErrorType* peError)
{
	CritterDef *pPetCritterDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;

	if (!pPetCritterDef || !entity_CanUsePetDef(pEnt,pPetDef,peError))
	{
		return false;
	}
	if (bAddAsPuppet && !pPetDef->bCanBePuppet)
	{
		if (peError) (*peError) = kAddSavedPetErrorType_NotAPuppet;
		return false;
	}
	if (!Entity_CanAddPet(pEnt,pPetCritterDef->pchClass))
	{
		if (peError) (*peError) = kAddSavedPetErrorType_MaxPets;
		return false;
	}
	if (bAddAsPuppet && !Entity_CanAddPuppet(pEnt,pPetCritterDef->pchClass, pExtract))
	{
		if (peError) (*peError) = kAddSavedPetErrorType_MaxPuppets;
		return false;
	}
	if (pPetDef->eAcquireLimit != kPetAcquireLimit_None)
	{
		if (Entity_CheckAcquireLimit(pEnt, pPetDef, uSrcItemID))
		{
			if (peError) (*peError) = kAddSavedPetErrorType_AcquireLimit;
			return false;
		}
	}
	return true;
}

//Keep this in sync with Entity_CanAddSavedPet
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .pInventoryV2.Ppinventorybags, .pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(eaPets, ".Pcritter, .Pchar");
bool trhEntity_CanAddSavedPet(ATH_ARG NOCONST(Entity) *pEnt, 
							  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
							  PetDef *pPetDef, U64 uSrcItemID, bool bAddAsPuppet,
							  GameAccountDataExtract *pExtract)
{
	CritterDef *pPetCritterDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;

	if (!pPetCritterDef || !trhEntity_CanUsePetDef(pEnt,eaPets,pPetDef))
	{
		return false;
	}
	if (bAddAsPuppet && !pPetDef->bCanBePuppet)
	{
		return false;
	}
	if (!trhEntity_CanAddPet(eaPets,pPetCritterDef->pchClass))
	{
		return false;
	}
	if (bAddAsPuppet && !trhEntity_CanAddPuppet(pEnt,pPetCritterDef->pchClass, pExtract))
	{
		return false;
	}
	if (pPetDef->eAcquireLimit != kPetAcquireLimit_None)
	{
		if (trhEntity_CheckAcquireLimit(pEnt, eaPets, pPetDef, uSrcItemID))
		{
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pPetEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pData, ".Eakeys");
S32 trhPet_GetCostToRename(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Entity)* pPetEnt, bool bDecrementFreeNameChanges, const ItemChangeReason *pReason, ATH_ARG NOCONST(GameAccountData) *pData)
{
	const char* pchFreeNumeric = g_PetRestrictions.pchFreeNameChangeNumeric;

	if ( NONNULL(pEnt) && NONNULL(pPetEnt) && pchFreeNumeric && pchFreeNumeric[0] )
	{
		S32 iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS,pPetEnt,pchFreeNumeric);

		if ( iNumFreeChanges > 0 )
		{
			if ( bDecrementFreeNameChanges )
			{
				inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pPetEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
			}

			return 0;
		}
		else
		{
			iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS,pEnt,pchFreeNumeric);

			if ( iNumFreeChanges > 0 )
			{
				if ( bDecrementFreeNameChanges )
				{
					inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
				}
				return 0;
			}
			else
			{
				if (NONNULL(pData))
				{
					char text[64];
					*text = '\0';
					strcat(text, GetShortProductName());
					strcat(text, ".FreePetNameChange");
					iNumFreeChanges = gad_trh_GetAttribInt(pData, text);
					if (!gConf.bDontAllowGADModification && iNumFreeChanges > 0)
					{
#if GAMESERVER
						if ( bDecrementFreeNameChanges )
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
		}
	}
	
	return g_PetRestrictions.iRenameCost;
}

bool Entity_CanRenamePet(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID Entity* pPetEnt)
{
	const char* pchCostNumeric = g_PetRestrictions.pchRenameCostNumeric;

	if ( pchCostNumeric && pchCostNumeric[0] )
	{
		S32 iCost = Pet_GetCostToRename(pPlayerEnt,pPetEnt,entity_GetGameAccount(pPlayerEnt));

		if ( iCost > 0 )
		{	
			S32 iOwnerCurrency = inv_GetNumericItemValue(pPlayerEnt,pchCostNumeric);
			
			if ( iCost > iOwnerCurrency )
			{
				return false;
			}
		}
	}

	return true;
}

// Keep in sync with trhEntity_CanUsePetDef
bool entity_CanUsePetDef(Entity *pEnt, PetDef *pPetDef, AddSavedPetErrorType* peError)
{
	int i;
	//Check allegiance
	AllegianceDef *pPetAllegiance = GET_REF(pPetDef->hAllegiance);

	if(pPetAllegiance && pPetAllegiance != GET_REF(pEnt->hAllegiance) && pPetAllegiance != GET_REF(pEnt->hSubAllegiance))
	{
		if (peError) (*peError) = kAddSavedPetErrorType_InvalidAllegiance;
		return false;
	}

	//If the pet def is unique, check all existing pets on the entity
	if (pPetDef->bIsUnique)
	{
		for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
		{
			Entity* pPetEnt = GET_REF(pEnt->pSaved->ppOwnedContainers[i]->hPetRef);

			if (pPetEnt && pPetEnt->pCritter)
			{
				if (GET_REF(pPetEnt->pCritter->petDef) == pPetDef)
				{
					if (peError) (*peError) = kAddSavedPetErrorType_UniqueCheck;
					return false;
				}
			}
		}
	}
	return true;
}

// Keep in sync with entity_CanUsePetDef
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance")
	ATR_LOCKS(eaPets, ".Pcritter");
bool trhEntity_CanUsePetDef(ATH_ARG NOCONST(Entity) *pEnt, 
							ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
							PetDef *pPetDef)
{
	int i;
	//Check allegiance
	AllegianceDef *pPetAllegiance = GET_REF(pPetDef->hAllegiance);

	if(pPetAllegiance)
	{
		if(pPetAllegiance != GET_REF(pEnt->hAllegiance) && pPetAllegiance != GET_REF(pEnt->hSubAllegiance))
			return false;
	}

	//If the pet def is unique, check all existing pets on the entity
	if (pPetDef->bIsUnique)
	{
		for (i = eaSize(&eaPets)-1; i >= 0; i--)
		{
			if (NONNULL(eaPets[i]->pCritter))
			{
				if (GET_REF(eaPets[i]->pCritter->petDef) == pPetDef)
				{
					return false;
				}
			}
		}
	}
	return true;
}
 
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pPetEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pData, ".Eakeys");
S32 trhPet_GetCostToChangeSubName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Entity)* pPetEnt, bool bDecrementFreeNameChanges, const ItemChangeReason *pReason, ATH_ARG NOCONST(GameAccountData) *pData)
{
	const char* pchFreeNumeric = g_PetRestrictions.pchFreeSubNameChangeNumeric;

	if ( NONNULL(pEnt) && NONNULL(pPetEnt) && pchFreeNumeric && pchFreeNumeric[0] )
	{
		S32 iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS,pPetEnt,pchFreeNumeric);

		if ( iNumFreeChanges > 0 )
		{
			if ( bDecrementFreeNameChanges )
			{
				inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pPetEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
			}

			return 0;
		}
		else
		{
			pchFreeNumeric = g_PetRestrictions.pchFreeFlexSubNameChangeNumeric;
			if ( pchFreeNumeric && pchFreeNumeric[0] )
			{
				iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS,pEnt,pchFreeNumeric);

				if ( iNumFreeChanges > 0 )
				{
					if ( bDecrementFreeNameChanges )
					{
						inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
					}

					return 0;
				}
				else
				{
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
							if ( bDecrementFreeNameChanges )
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
			}
		}
	}
	
	return g_PetRestrictions.iChangeSubNameCost;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppliteBags[]")
ATR_LOCKS(pData, ".Eakeys");
S32 trhEnt_GetCostToChangeSubName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bDecrementFreeNameChanges, const ItemChangeReason *pReason, ATH_ARG NOCONST(GameAccountData) *pData)
{
	//TODO(jransom): Get player cost - not pet cost
	const char* pchFreeNumeric = g_PetRestrictions.pchFreeSubNameChangeNumeric;

	if ( NONNULL(pEnt) && pchFreeNumeric && pchFreeNumeric[0] )
	{
		S32 iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS,pEnt,pchFreeNumeric);

		if ( iNumFreeChanges > 0 )
		{
			if ( bDecrementFreeNameChanges )
			{
				inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
			}

			return 0;
		}
		else
		{
			pchFreeNumeric = g_PetRestrictions.pchFreeFlexSubNameChangeNumeric;
			if ( pchFreeNumeric && pchFreeNumeric[0] )
			{
				iNumFreeChanges = inv_trh_GetNumericValue(ATR_PASS_ARGS,pEnt,pchFreeNumeric);

				if ( iNumFreeChanges > 0 )
				{
					if ( bDecrementFreeNameChanges )
					{
						inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pEnt,true,pchFreeNumeric,iNumFreeChanges-1, pReason);
					}

					return 0;
				}
				else
				{
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
							if ( bDecrementFreeNameChanges )
							{
#if GAMESERVER
								char temp[32];
								sprintf(temp, "%d", iNumFreeChanges - 1);
								slGAD_trh_SetAttrib(ATR_PASS_ARGS, pData, text, temp);
#endif
							}
							return 0;
						}
					}
				}
			}
		}
	}

	return g_PetRestrictions.iChangeSubNameCost;
}

bool Entity_CanChangeSubNameOnPet(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID Entity* pPetEnt)
{
	const char* pchCostNumeric = g_PetRestrictions.pchChangeSubNameCostNumeric;

	if ( pchCostNumeric && pchCostNumeric[0] )
	{
		S32 iCost = Pet_GetCostToChangeSubName(pPlayerEnt,pPetEnt,entity_GetGameAccount(pPlayerEnt));
		
		if ( iCost > 0 )
		{
			S32 iOwnerCurrency = inv_GetNumericItemValue(pPlayerEnt,pchCostNumeric);

			if ( iCost > iOwnerCurrency )
			{
				return false;
			}
		}
	}

	return true;
}

bool Entity_CanChangeSubName(SA_PARAM_NN_VALID Entity* pPlayerEnt)
{
	//TODO(jransom): Check against player cost - not pet cost
	const char* pchCostNumeric = g_PetRestrictions.pchChangeSubNameCostNumeric;

	if ( pchCostNumeric && pchCostNumeric[0] )
	{
		S32 iCost = Ent_GetCostToChangeSubName(pPlayerEnt,entity_GetGameAccount(pPlayerEnt));

		if ( iCost > 0 )
		{
			S32 iOwnerCurrency = inv_GetNumericItemValue(pPlayerEnt,pchCostNumeric);

			if ( iCost > iOwnerCurrency )
			{
				return false;
			}
		}
	}

	return true;
}

void Entity_GetActivePuppetListByType(Entity *pEntity, CharClassTypes eType, PuppetEntity ***peaOut)
{
	int i;

	if(!pEntity || !pEntity->pSaved || !pEntity->pSaved->pPuppetMaster || !peaOut)
		return;

	for(i=eaSize(&pEntity->pSaved->pPuppetMaster->ppPuppets)-1;i>=0;i--)
	{
		PuppetEntity *pPuppetEntity = pEntity->pSaved->pPuppetMaster->ppPuppets[i];
		if(pPuppetEntity->eType == (U32)eType && pPuppetEntity->eState == PUPPETSTATE_ACTIVE)
		{
			eaPush(peaOut, pPuppetEntity);
		}
	}
}

Entity *Entity_FindCurrentOrPreferredPuppet(Entity *pOwner, CharClassTypes eType)
{
	if(pOwner && pOwner->pSaved && pOwner->pSaved->pPuppetMaster)
	{
		CharacterClass *pClass = SAFE_GET_REF2(pOwner, pChar, hClass);
		bool bFindCurrent = (SAFE_MEMBER(pClass, eType) == eType);
		CharClassCategorySet *pSet = (!bFindCurrent) ? CharClassCategorySet_getPreferredSet(pOwner) : NULL;
		bool bFindPreferred = pSet ? pSet->eClassType == eType : false;
		int i;
		for(i=eaSize(&pOwner->pSaved->pPuppetMaster->ppPuppets)-1;i>=0;i--)
		{
			PuppetEntity *pPuppetEntity = pOwner->pSaved->pPuppetMaster->ppPuppets[i];
			if (pPuppetEntity->eType != (U32)eType || pPuppetEntity->eState != PUPPETSTATE_ACTIVE)
				continue;

			if (bFindCurrent)
			{
				if (pPuppetEntity->curID == pOwner->pSaved->pPuppetMaster->curID)
					return SavedPuppet_GetEntity(entGetPartitionIdx(pOwner), pPuppetEntity);
			}
			else if (bFindPreferred)
			{
				Entity *pEntity = SavedPuppet_GetEntity(entGetPartitionIdx(pOwner), pPuppetEntity);
				if (CharClassCategorySet_checkIfPassEntity(pSet, pEntity))
					return pEntity;
			}
			else
			{
				return SavedPuppet_GetEntity(entGetPartitionIdx(pOwner), pPuppetEntity);
			}
		}
	}
	return NULL;
}

bool Entity_CanModifyPuppet(Entity* pOwner, Entity* pEntity)
{
	if (pOwner->pSaved && pOwner->pSaved->pPuppetMaster && pEntity)
	{
		bool bCheckContacts = false;

		if (pOwner != pEntity) 
		{
			PuppetEntity* pPuppet = SavedPet_GetPuppetFromContainerID(pOwner, entGetContainerID(pEntity));
				
			//The player can always modify active puppets
			if (pPuppet && pPuppet->eState != PUPPETSTATE_ACTIVE)
			{
				bCheckContacts = true;
			}
		}

		if (bCheckContacts)
		{
			if (interaction_IsPlayerNearContact(pOwner, ContactFlag_StarshipChooser))
			{
				ContactInfo** eaContacts = NULL;
				S32 i;
				interaction_GetNearbyInteractableContacts(pOwner, ContactFlag_StarshipChooser, &eaContacts);

				for (i = eaSize(&eaContacts)-1; i >= 0; i--)
				{
					ContactInfo* pInfo = eaContacts[i];

					if (pInfo->bAllowSwitchToLastActivePuppet &&
						pOwner->pSaved->pPuppetMaster->lastActiveID == entGetContainerID(pEntity))
					{
						return true;
					}
					else if (!eaiSize(&pInfo->peAllowedClassCategories))
					{
						return true;
					}
					else
					{
						CharacterClass* pClass = pEntity->pChar ? GET_REF(pEntity->pChar->hClass) : NULL;
						CharClassCategory eCategory = pClass ? pClass->eCategory : CharClassCategory_None;

						if (eaiFind(&pInfo->peAllowedClassCategories, eCategory) >= 0)
						{
							return true;
						}
					}
				}
				eaDestroy(&eaContacts);
			}
			return false;
		}
	}
	return true;
}

void Entity_ForEveryPet(int iPartitionIdx, 
						Entity* pEnt, 
						EntityUserDataCallback pCallback, 
						void* pCallbackData, 
						bool bMustBeAlive, 
						bool bRealEntsOnly)
{
	S32 i;
	for (i = 0; i < eaSize(&pEnt->pSaved->ppOwnedContainers); i++)
	{	
		if (!SavedPet_IsPetAPuppet(pEnt, pEnt->pSaved->ppOwnedContainers[i]))
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
			Entity* pPetEnt;
			
			if (bRealEntsOnly)
			{
				pPetEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYSAVEDPET,pPet->conID);
			}
			else
			{
				pPetEnt = SavedPet_GetEntity(iPartitionIdx, pPet);
			}

			if (pPetEnt && (!bMustBeAlive || (pPetEnt->myEntityFlags & ENTITYFLAG_DEAD)==0))
			{
				pCallback(iPartitionIdx, pPetEnt, pCallbackData);
			}
		}
	}

	for (i = 0; i < eaSize(&pEnt->pSaved->ppCritterPets); i++)
	{
		Entity* pPetEnt = SavedCritter_GetEntity( iPartitionIdx, pEnt->pSaved->ppCritterPets[i] );
		if ( pPetEnt && (!bMustBeAlive || (pPetEnt->myEntityFlags & ENTITYFLAG_DEAD)==0) )
		{
			pCallback(iPartitionIdx, pPetEnt, pCallbackData);
		}
	}
}

AUTO_TRANS_HELPER;
S32 AlwaysPropSlot_trh_FindByPetID(ATH_ARG NOCONST(Entity)* pEntity, U32 uPetID, U32 uPuppetID, AlwaysPropSlotCategory eCategory)
{
	S32 i;
	for (i = eaSize(&pEntity->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
	{
		NOCONST(AlwaysPropSlot)* pPropSlot = pEntity->pSaved->ppAlwaysPropSlots[i];
		AlwaysPropSlotDef* pPropSlotDef = GET_REF(pPropSlot->hDef);
		if (!pPropSlotDef || (eCategory >= 0 && pPropSlotDef->eCategory != eCategory))
		{
			continue;
		}
		if (pPropSlot->iPetID == uPetID
			&& pPropSlot->iPuppetID == uPuppetID)
		{
			break;
		}
	}
	return i;
}

AUTO_TRANS_HELPER;
void AlwaysPropSlot_trh_FindAllByPetID(ATH_ARG NOCONST(Entity)* pEntity, U32 uPetID, U32 uPuppetID, S32** ppiSlots)
{
	S32 i;
	for (i = eaSize(&pEntity->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
	{
		if (pEntity->pSaved->ppAlwaysPropSlots[i]->iPetID == uPetID
			&& pEntity->pSaved->ppAlwaysPropSlots[i]->iPuppetID == uPuppetID)
		{
			ea32Push(ppiSlots, i);
		}
	}
}

AUTO_TRANS_HELPER;
S32 AlwaysPropSlot_trh_FindBySlotID(ATH_ARG NOCONST(Entity)* pEntity, U32 uSlotID)
{
	S32 i;
	for (i = eaSize(&pEntity->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
	{
		NOCONST(AlwaysPropSlot)* pPropSlot = pEntity->pSaved->ppAlwaysPropSlots[i];
		if (pPropSlot->iSlotID == uSlotID)
		{
			break;
		}
	}
	return i;
}

AUTO_TRANS_HELPER;
bool SavedPet_th_AlwaysPropSlotCheckRestrictions(ATH_ARG NOCONST(Entity)* pSavedPet,
												 ATH_ARG NOCONST(PetRelationship)* pRelationShip,
												 AlwaysPropSlotDef* pSlotDef)
{
	CharacterClass* pPetClass = NULL;
	AlwaysPropSlotClassRestrictDef* pRestrictDef = AlwaysPropSlot_GetClassRestrictDef(pSlotDef->eClassRestrictType);

	if(NONNULL(pSavedPet->pChar))
		pPetClass = GET_REF(pSavedPet->pChar->hClass);

	if (ISNULL(pPetClass) || ISNULL(pRestrictDef) || eaiFind(&pRestrictDef->peClassTypes,pPetClass->eType) < 0)
	{
		return false;
	}
	if (pSlotDef->iMinPropPowers > 0)
	{
		S32 iPropPowerCount;
		NOCONST(Power)** ppPropPowers = NULL;
		ent_trh_FindAllPropagatePowers(pSavedPet, pRelationShip, pSlotDef, NULL, &ppPropPowers);
		iPropPowerCount = eaSize(&ppPropPowers);
		eaDestroy(&ppPropPowers);
		if (iPropPowerCount < pSlotDef->iMinPropPowers)
		{
			return false;
		}
	}
	return true;
}

/*********************** Power Propagation Functions **********************/

AUTO_TRANS_HELPER;
void ent_trh_FindAllPropagatedPowersFromPowerTrees(ATH_ARG NOCONST(Entity)* pEntity, S32 eAllowPowerCategory, NOCONST(Power)*** pppPowersOut)
{
	int i;
	for(i=eaSize(&pEntity->pChar->ppPowerTrees)-1; i>=0; i--)
	{
		int j;
		NOCONST(PowerTree) *ptree = pEntity->pChar->ppPowerTrees[i];
		for(j=eaSize(&ptree->ppNodes)-1; j>=0; j--)
		{
			int k;
			NOCONST(PTNode) *pnode = ptree->ppNodes[j];

			if(eaSize(&pnode->ppPowers) == 0)
				continue;

			if (pnode->bEscrow)
				continue;

			if(NONNULL(pnode->ppPowers[0]))
			{
				PowerDef *pdef = GET_REF(pnode->ppPowers[0]->hDef);

				//If the first node is an enhancement, then all the nodes are enhancements, and you should
				//only add the last power
				if(pdef && pdef->eType == kPowerType_Enhancement)
				{
					pdef = GET_REF(pnode->ppPowers[eaSize(&pnode->ppPowers)-1]->hDef);

					if(pdef && pdef->powerProp.bPropPower && 
						(eAllowPowerCategory < 0 || ea32Find(&pdef->piCategories, eAllowPowerCategory)>=0))
					{
						eaPush(pppPowersOut,pnode->ppPowers[eaSize(&pnode->ppPowers)-1]);
					}
					continue;
				}
			}

			for(k=eaSize(&pnode->ppPowers)-1; k>=0; k--)
			{
				PowerDef *pdef = GET_REF(pnode->ppPowers[k]->hDef);

				if(!pdef || ISNULL(pnode->ppPowers[k]))
					continue;

				if(pdef->powerProp.bPropPower && 
					(eAllowPowerCategory < 0 || ea32Find(&pdef->piCategories, eAllowPowerCategory)>=0))
				{
					eaPush(pppPowersOut,pnode->ppPowers[k]);
				}
				// If the Power we just added isn't an Enhancement, we're done with this node
				if(!pdef || pdef->eType!=kPowerType_Enhancement)
				{
					break;
				}
			}
		}
	}
}

static int PropPropSort(int *eaPurposes, const Power **ppPower1, const Power **ppPower2)
{
	int iPos1=0, iPos2 =0;
	PowerDef *pDef1 = GET_REF((*ppPower1)->hDef);
	PowerDef *pDef2 = GET_REF((*ppPower2)->hDef);

	if(pDef1 && pDef2)
	{
		if(ea32Size(&eaPurposes))
		{
			iPos1 = ea32Find(&eaPurposes,pDef1->ePurpose);
			iPos2 = ea32Find(&eaPurposes,pDef2->ePurpose);
		}
		else
		{
			iPos1 = pDef1->ePurpose;
			iPos2 = pDef2->ePurpose;
		}
	}

	if(iPos1 == iPos2)
	{
		iPos1 = (*ppPower1)->uiID;
		iPos2 = (*ppPower2)->uiID;
	}

	return iPos1 < iPos2 ? -1 : iPos1 > iPos2 ? 1 : 0;
}

AUTO_TRANS_HELPER;
void ent_trh_FindAllPropagatePowers(ATH_ARG NOCONST(Entity)* pEntity, 
									ATH_ARG NOCONST(PetRelationship)* pSavedPet, 
									AlwaysPropSlotDef* pPropSlotDef, 
									NOCONST(Power)** ppOldPropPowers, 
									NOCONST(Power)*** pppPowersOut)
{
	int i, iPetID = 0;
	PowerPurpose eIgnorePurpose = pPropSlotDef ? pPropSlotDef->eIgnorePurposeForMax : kPowerPurpose_None;
	S32 eAllowCategory = pPropSlotDef  ? pPropSlotDef->eAllowPowerCategory : -1;

	if(ISNULL(pEntity->pChar))
		return;

	if(pEntity->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		iPetID = pEntity->pSaved->iPetID;
	}

	// Add all Powers in the personal list
	for(i=0; i<eaSize(&pEntity->pChar->ppPowersPersonal); i++)
	{
		PowerDef *pDef = GET_REF(pEntity->pChar->ppPowersPersonal[i]->hDef);

		if(pDef && pDef->powerProp.bPropPower && (eAllowCategory < 0 || ea32Find(&pDef->piCategories, eAllowCategory)>=0))
			eaPush(pppPowersOut,pEntity->pChar->ppPowersPersonal[i]);
	}

	// Add all Powers in the class list
	for(i=0; i<eaSize(&pEntity->pChar->ppPowersClass); i++)
	{
		PowerDef *pDef = GET_REF(pEntity->pChar->ppPowersClass[i]->hDef);

		if(pDef && pDef->powerProp.bPropPower && (eAllowCategory < 0 || ea32Find(&pDef->piCategories, eAllowCategory)>=0))
			eaPush(pppPowersOut,pEntity->pChar->ppPowersClass[i]);
	}

	// Add all Powers in the species list
	for(i=0; i<eaSize(&pEntity->pChar->ppPowersSpecies); i++)
	{
		PowerDef *pDef = GET_REF(pEntity->pChar->ppPowersSpecies[i]->hDef);

		if(pDef && pDef->powerProp.bPropPower && (eAllowCategory < 0 || ea32Find(&pDef->piCategories, eAllowCategory)>=0))
			eaPush(pppPowersOut,pEntity->pChar->ppPowersSpecies[i]);
	}

	ent_trh_FindAllPropagatedPowersFromPowerTrees(pEntity,eAllowCategory,pppPowersOut);

	if(pPropSlotDef)
	{
		if(pPropSlotDef->iMaxPropPowers && eaSize(pppPowersOut) > pPropSlotDef->iMaxPropPowers)
		{
			int iCount = 0;
			eaQSort_s(*pppPowersOut, PropPropSort, pSavedPet->eaPurposes);
			for (i = 0; i < eaSize(pppPowersOut); i++)
			{
				PowerDef* pDef = GET_REF((*pppPowersOut)[i]->hDef);
				if (!pDef)
				{
					eaRemove(pppPowersOut,i--);
				}
				else if (pDef->ePurpose != eIgnorePurpose)
				{
					if (iCount >= pPropSlotDef->iMaxPropPowers)
					{
						eaRemove(pppPowersOut,i--);
					}
					iCount++;
				}
			}
		}
	}
}

void ent_FindAllPropagatePowers(Entity *pEntity,
								PetRelationship *pSavedPet, 
								AlwaysPropSlotDef *pPropSlotDef, 
								Power **ppOldPropPowers,
								Power ***pppPowersOut, 
								bool bNoAlloc)
{
	int i, j;
	Power** ppFoundPowers = NULL;
	ent_trh_FindAllPropagatePowers(CONTAINER_NOCONST(Entity, pEntity),
		CONTAINER_NOCONST(PetRelationship, (pSavedPet)),
		pPropSlotDef,
		(NOCONST(Power)**)(ppOldPropPowers),
		(NOCONST(Power)***)(&ppFoundPowers));

	// Propagate from the inventory if there is no AlwaysPropSlotDef
	if (!pPropSlotDef && pEntity->pInventoryV2)
	{
		for (i = eaSize(&pEntity->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
		{
			InventoryBag* pBag = pEntity->pInventoryV2->ppInventoryBags[i];
			if (invbag_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag))
			{
				for (j = eaSize(&pBag->ppIndexedInventorySlots)-1; j >= 0; j--)
				{
					InventorySlot* pSlot = pBag->ppIndexedInventorySlots[j];
					if (pSlot->pItem)
					{
						int iPower, iNumPowers = item_GetNumItemPowerDefs(pSlot->pItem, true);

						for (iPower = 0; iPower < iNumPowers; iPower++)
						{
							Power* pItemPower = item_GetPower(pSlot->pItem, iPower);
							PowerDef* pDef = SAFE_GET_REF(pItemPower, hDef);
							if (pDef && pDef->powerProp.bPropPower)
							{
								if (item_ItemPowerActive(pEntity, pBag, pSlot->pItem, iPower))
								{
									eaPush(&ppFoundPowers,pItemPower);
								}
							}
						}
					}
				}
			}
		}
	}
	
	if (bNoAlloc)
	{
		eaCopy(pppPowersOut, &ppFoundPowers);
	}
	else
	{
		for(i=0;i<eaSize(&ppFoundPowers);i++)
		{
			NOCONST(Power)* pNewPower = StructCloneDeConst(parse_Power,ppFoundPowers[i]);
			//Check to see if the power was in the old list, and if so, copy the recharge time.
			//Do this because propagated powers are only ever written to the DB when they are 
			//removed. This can happen when the player logs out or when a pet's ALWAYSPROP
			//status flag is cleared.
			for (j = eaSize(&ppOldPropPowers)-1; j >= 0; j--)
			{
				if (ppOldPropPowers[j]->uiID == pNewPower->uiID)
				{
					pNewPower->fTimeRecharge = ppOldPropPowers[j]->fTimeRecharge;
					break;
				}
			}
			eaPush(pppPowersOut,(Power*)pNewPower);
		}
	}
	eaDestroy(&ppFoundPowers);
}

Entity* SavedPet_GetEntityFromPetID(Entity* pOwner, U32 uiPetID)
{
	S32 i, iPetArraySize = (pOwner && pOwner->pSaved) ? eaSize(&pOwner->pSaved->ppOwnedContainers) : 0;

	for (i = 0; i < iPetArraySize; i++)
	{
		PetRelationship* pPet = pOwner->pSaved->ppOwnedContainers[i];

		Entity* pEnt = GET_REF(pPet->hPetRef);

		if (pEnt && pEnt->pSaved && pEnt->pSaved->iPetID == uiPetID)
		{
			if (pEnt->pChar && !pEnt->pChar->pEntParent)
			{
				pEnt->pChar->pEntParent = pEnt;
			}
			return pEnt;
		}
	}
	return NULL;
}

bool Entity_CanSetAsActivePuppet(Entity* pEnt, PuppetEntity* pNewPuppet)
{
	PuppetEntity* pOldPuppet = NULL;
	CharClassCategorySet *pSet = CharClassCategorySet_getCategorySetFromPuppetEntity(entGetPartitionIdx(pEnt), pNewPuppet);

	if (!pEnt || !pEnt->pSaved || !pEnt->pSaved->pPuppetMaster)
		return false;

	if (pNewPuppet)
	{
		Entity* pNewPuppetEnt = GET_REF(pNewPuppet->hEntityRef);
		PetDef* pNewPetDef = SAFE_GET_REF2(pNewPuppetEnt, pCritter, petDef);
		S32 i, iLevel = entity_GetSavedExpLevel(pEnt);
		
		if (!pNewPetDef || iLevel < pNewPetDef->iMinActivePuppetLevel)
		{
			return false;
		}
		for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			PuppetEntity* pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
			CharClassCategory eCategory = CharClassCategory_getCategoryFromPuppetEntity(entGetPartitionIdx(pEnt), pPuppet);
			if (pPuppet->eType == pNewPuppet->eType
				&& pPuppet->eState == PUPPETSTATE_ACTIVE
				&& CharClassCategorySet_checkIfPass(pSet, eCategory, pPuppet->eType))
			{
				pOldPuppet = pPuppet;
				break;
			}
		}
	}
	if (pOldPuppet && pOldPuppet->curID == pNewPuppet->curID)
	{
		return false;
	}
	return true;
}

bool Entity_CanSetAsActivePuppetByID(Entity* pEnt, U32 uiPuppetID)
{
	PuppetEntity* pNewPuppet = NULL;
	S32 i;

	if (!pEnt || !pEnt->pSaved || !pEnt->pSaved->pPuppetMaster)
		return false;

	for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
	{
		PuppetEntity* pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
		if (uiPuppetID == pPuppet->curID)
		{
			pNewPuppet = pPuppet;
			break;
		}
	}
	return Entity_CanSetAsActivePuppet(pEnt, pNewPuppet);
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetFirstNameIfExists");
const char *FormalName_GetFirstNameIfExists(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcSubName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedSubName : NULL;
	static char text[50];
	int i;

	if (!pcSubName) return "";
	*text = '\0';

	if (!strnicmp(pcSubName, "LFM:", 4))
	{
		pcSubName += 4;
		while (*pcSubName != '\0' && *pcSubName != ':') ++pcSubName;
		if (*pcSubName == '\0') return "";
		++pcSubName;
		i = 0;
		while (i < 49 && *pcSubName != '\0' && *pcSubName != ':')
		{
			text[i++] = *pcSubName;
			++pcSubName;
		}
		text[i] = '\0';
		if (i == 0) return "";
	}
	else // "FML:"
	{
		pcSubName += 4;
		i = 0;
		while (i < 49 && *pcSubName != '\0' && *pcSubName != ':')
		{
			text[i++] = *pcSubName;
			++pcSubName;
		}
		text[i] = '\0';
		if (i == 0) return "";
	}
	return text;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetFirstName");
const char *FormalName_GetFirstName(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	const char *temp = FormalName_GetFirstNameIfExists(pEnt);
	if ((!temp) || (!*temp)) return pcName;
	return temp;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(Player) ACMD_NAME("FormalName_GetPlayerFirstName");
const char *FormalName_GetPlayerFirstName(ExprContext* context)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	if (!pEnt) return pcName;
	return FormalName_GetFirstName(pEnt);
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetMiddleNameIfExists");
const char *FormalName_GetMiddleNameIfExists(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcSubName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedSubName : NULL;
	static char text[50];
	int i;

	if (!pcSubName) return "";
	*text = '\0';

	if (!strnicmp(pcSubName, "LFM:", 4))
	{
		pcSubName += 4;
		while (*pcSubName != '\0' && *pcSubName != ':') ++pcSubName;
		if (*pcSubName == '\0') return "";
		++pcSubName;
		while (*pcSubName != '\0' && *pcSubName != ':') ++pcSubName;
		if (*pcSubName == '\0') return "";
		++pcSubName;
		i = 0;
		while (i < 49 && *pcSubName != '\0' && *pcSubName != ':')
		{
			text[i++] = *pcSubName;
			++pcSubName;
		}
		text[i] = '\0';
		if (i == 0) return "";
	}
	else // "FML:"
	{
		pcSubName += 4;
		while (*pcSubName != '\0' && *pcSubName != ':') ++pcSubName;
		if (*pcSubName == '\0') return "";
		++pcSubName;
		i = 0;
		while (i < 49 && *pcSubName != '\0' && *pcSubName != ':')
		{
			text[i++] = *pcSubName;
			++pcSubName;
		}
		text[i] = '\0';
		if (i == 0) return "";
	}
	return text;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetMiddleName");
const char *FormalName_GetMiddleName(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	const char *temp = FormalName_GetMiddleNameIfExists(pEnt);
	if ((!temp) || (!*temp)) return pcName;
	return temp;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetLastNameIfExists");
const char *FormalName_GetLastNameIfExists(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcSubName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedSubName : NULL;
	static char text[50];
	int i;

	if (!pcSubName) return "";
	*text = '\0';

	if (!strnicmp(pcSubName, "LFM:", 4))
	{
		pcSubName += 4;
		i = 0;
		while (i < 49 && *pcSubName != '\0' && *pcSubName != ':')
		{
			text[i++] = *pcSubName;
			++pcSubName;
		}
		text[i] = '\0';
		if (i == 0) return "";
	}
	else // "FML:"
	{
		pcSubName += 4;
		while (*pcSubName != '\0' && *pcSubName != ':') ++pcSubName;
		if (*pcSubName == '\0') return "";
		++pcSubName;
		while (*pcSubName != '\0' && *pcSubName != ':') ++pcSubName;
		if (*pcSubName == '\0') return "";
		++pcSubName;
		i = 0;
		while (i < 49 && *pcSubName != '\0' && *pcSubName != ':')
		{
			text[i++] = *pcSubName;
			++pcSubName;
		}
		text[i] = '\0';
		if (i == 0) return "";
	}
	return text;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetLastName");
const char *FormalName_GetLastName(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	const char *temp = FormalName_GetLastNameIfExists(pEnt);
	if ((!temp) || (!*temp)) return pcName;
	return temp;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(Player) ACMD_NAME("FormalName_GetPlayerLastName");
const char *FormalName_GetPlayerLastName(ExprContext* context)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	if (!pEnt) return pcName;
	return FormalName_GetLastName(pEnt);
}

const char *FormalName_GetFullNameFromSubName(const char *pcSubName)
{
	static char text[128];
	int i, j;

	if (!pcSubName) return NULL;
	if (!(pcSubName[0] && pcSubName[1] && pcSubName[2] && pcSubName[3])) return NULL;
	*text = '\0';

	pcSubName += 4;
	i = 0;
	while (i < 20 && *pcSubName != '\0' && *pcSubName != ':')
	{
		text[i++] = *pcSubName;
		++pcSubName;
	}
	if (*pcSubName == '\0') return NULL;

	++pcSubName;
	j = 0;
	while (j < 20 && *pcSubName != '\0' && *pcSubName != ':')
	{
		if (j == 0 && *text != '\0') text[i++] = ' ';
		text[i++] = *pcSubName;
		++j;
		++pcSubName;
	}
	if (*pcSubName == '\0') return NULL;

	++pcSubName;
	j = 0;
	while (j < 20 && *pcSubName != '\0' && *pcSubName != ':')
	{
		if (j == 0 && *text != '\0') text[i++] = ' ';
		text[i++] = *pcSubName;
		++j;
		++pcSubName;
	}

	text[i] = '\0';
	if (*text == '\0') return NULL;

	return text;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetFullName");
const char *FormalName_GetFullName(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcSubName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedSubName : NULL;
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	const char *pcResult = FormalName_GetFullNameFromSubName(pcSubName);
	if (!pcResult) return pcName;
	return pcResult;
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(Player) ACMD_NAME("FormalName_GetPlayerFullName");
const char *FormalName_GetPlayerFullName(ExprContext* context)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	if (!pEnt) return pcName;
	return FormalName_GetFullName(pEnt);
}

// Get the first name of a formal name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_GetNameOrder");
const char *FormalName_GetNameOrder(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedSubName : NULL;
	if (!pcName) return "FML";
	if (strnicmp("FML:", pcName, 4))
	{
		return "LFM";
	}
	return "FML";
}

bool savedpet_ValidateFormalName(Entity *ent, const char *pcFormalName, char **ppEStringError)
{
	const int c_iNameParts = 3;
	const U32 c_iMaxSubnameLength = 20;
	const int c_iFirstLong = 0 * c_iNameParts;
	const int c_iFirstInvalid = 1 * c_iNameParts;
	const int c_iFirstMissing = 2 * c_iNameParts;

	static const char *s_aapchErrorKeys[][2] = {
		// FML									LFM
		{ "NameFormat_Formal_FirstTooLong",		"NameFormat_Formal_LastTooLong", },			// 0 = First Too Long
		{ "NameFormat_Formal_MiddleTooLong",	"NameFormat_Formal_FirstTooLong", },		// 1 = Second Too Long
		{ "NameFormat_Formal_LastTooLong",		"NameFormat_Formal_MiddleTooLong", },		// 2 = Third Too Long

		{ "NameFormat_Formal_FirstInvalid",		"NameFormat_Formal_LastInvalid", },			// 3 = First Invalid
		{ "NameFormat_Formal_MiddleInvalid",	"NameFormat_Formal_FirstInvalid", },		// 4 = Second Invalid
		{ "NameFormat_Formal_LastInvalid",		"NameFormat_Formal_MiddleInvalid", },		// 5 = Third Invalid

		{ NULL,									"NameFormat_Formal_NoLast", },				// 6 = First Missing
		{ NULL,									NULL, },									// 7 = Second Missing
		{ "NameFormat_Formal_NoLast",			NULL, },									// 8 = Third Missing
	};

	int i;
	S32 textLen;
	char *text;
	char *fullname;
	int iOrder = 0;
	int strerr;

	textLen = pcFormalName ? (S32)strlen(pcFormalName) + 1 : 1;
	text = (char*) alloca(textLen);
	fullname = (char*) alloca(textLen);

	if (pcFormalName && !stricmp(pcFormalName, "NONE"))
	{
		return true;
	}
	else if (pcFormalName && !strnicmp(pcFormalName, "FML:", 4))
	{
		iOrder = 0;
		pcFormalName += 4;
	}
	else if (pcFormalName && !strnicmp(pcFormalName, "LFM:", 4))
	{
		iOrder = 1;
		pcFormalName += 4;
	}
	else
	{
		langFormatMessageKey(entGetLanguage(ent), ppEStringError, "NameFormat_Formal_Tag", STRFMT_END);
		return false;
	}

	fullname[0] = '\0';

	for (i = 0; i < c_iNameParts; i++)
	{
		const char *pc;

		if (!(pc = strchr(pcFormalName, ':')))
			pc = strchr(pcFormalName, 0);
		if (!pc)
			pc = pcFormalName;

		// Subname is empty, check to see if it is not allowed to be empty
		if (pc == pcFormalName)
		{
			if (s_aapchErrorKeys[c_iFirstMissing + i][iOrder])
			{
				langFormatMessageKey(entGetLanguage(ent),
					ppEStringError,
					s_aapchErrorKeys[c_iFirstMissing + i][iOrder],
					STRFMT_END);
				return false;
			}
		}

		// Extract subname
		strncpy_s(text, textLen, pcFormalName, (pc - pcFormalName));

		// Validate that the name is not too long
		if (strlen(text) > c_iMaxSubnameLength)
		{
			langFormatMessageKey(entGetLanguage(ent),
				ppEStringError,
				s_aapchErrorKeys[c_iFirstLong + i][iOrder],
				STRFMT_STRING("fname", text),
				STRFMT_STRING("mname", text),
				STRFMT_STRING("lname", text),
				STRFMT_END);
			return false;
		}

		// Ensure the subnames are valid (ignore min length for the middle name)
		if (text[0] && (strerr = StringIsInvalidFormalName(text, i, entGetAccessLevel(ent))))
		{
			char *pTempEStringError = NULL;
			estrStackCreate(&pTempEStringError);
#ifdef GAMECLIENT
			StringCreateNameError( &pTempEStringError, strerr );
#else
			entStringCreateNameError( ent, &pTempEStringError, strerr );
#endif

			langFormatMessageKey(entGetLanguage(ent),
				ppEStringError,
				s_aapchErrorKeys[c_iFirstInvalid + i][iOrder],
				STRFMT_STRING("reason", pTempEStringError),
				STRFMT_END);

			estrDestroy(&pTempEStringError);
			return false;
		}

		// Append subname to full name
		if (text[0] && fullname[0])
			strcat_s(fullname, textLen, " ");
		if (text[0])
			strcat_s(fullname, textLen, text);

		pcFormalName = pc + (*pc ? 1 : 0);
	}

	// Ensure the full name is valid
	strerr = StringIsInvalidCommonName(fullname, entGetAccessLevel(ent));
	if (strerr)
	{
#ifdef GAMECLIENT
		StringCreateNameError( ppEStringError, strerr );
#else
		entStringCreateNameError( ent, ppEStringError, strerr );
#endif
	}

	return true;
}

bool canDestroyPuppet(Entity *pEnt, PuppetEntity *pPuppet)
{
	if(pPuppet->eState == PUPPETSTATE_ACTIVE)
	{
		CharClassCategorySet *pSet = CharClassCategorySet_getCategorySetFromPuppetEntity(entGetPartitionIdx(pEnt), pPuppet);
		return SAFE_MEMBER(pSet, bAllowDeletionWhileActive);
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanDestroyPuppet);
bool gslCmdCanDestroyPuppet(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPuppetID)
{
	int i;

	if(!pEnt || !pEnt->pSaved || !pEnt->pSaved->pPuppetMaster)
		return false;

	for(i=0;i<eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets);i++)
	{
		if(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID == uiPuppetID)
			return canDestroyPuppet(pEnt,pEnt->pSaved->pPuppetMaster->ppPuppets[i]);
	}

	return false;
}

bool SavedPet_PetDiag_FixupEscrowNodes(SA_PARAM_NN_VALID PetDiag* pPetDiag, NOCONST(PTNodeDefRefCont)*** peaEscrowNodes)
{
	S32 iDiagIdx, iEscrowIdx, iCatIdx;
	PTNodeDef** ppFoundNodeDefs = NULL;
	bool bChanged = false;

	for (iDiagIdx = 0; iDiagIdx < eaSize(&pPetDiag->ppNodes); iDiagIdx++)
	{
		PetDiagNode* pDiagNode = pPetDiag->ppNodes[iDiagIdx];
		for (iEscrowIdx = eaSize(peaEscrowNodes)-1; iEscrowIdx >= 0; iEscrowIdx--)
		{
			PTNodeDef* pNodeDef = GET_REF((*peaEscrowNodes)[iEscrowIdx]->hNodeDef);
			PowerDef* pPowerDef = NULL;

			if (pNodeDef && eaSize(&pNodeDef->ppRanks) > 0)
			{
				pPowerDef = GET_REF(pNodeDef->ppRanks[0]->hPowerDef);
			}
			if (pNodeDef && pPowerDef)
			{
				if (pDiagNode->ePurpose == pNodeDef->ePurpose)
				{
					for (iCatIdx = ea32Size(&pDiagNode->piCategories)-1; iCatIdx >= 0; iCatIdx--)
					{
						if (ea32Find(&pPowerDef->piCategories,pDiagNode->piCategories[iCatIdx])==-1)
						{
							break;
						}
					}
					if (iCatIdx != -1)
					{
						continue;
					}
					eaPush(&ppFoundNodeDefs, pNodeDef);
				}
			}
			else
			{
				StructDestroyNoConst(parse_PTNodeDefRefCont, eaRemove(peaEscrowNodes, iEscrowIdx));
				bChanged = true;
			}
		}

		if (pDiagNode->iCount < eaSize(&ppFoundNodeDefs))
		{
			while (pDiagNode->iCount < eaSize(&ppFoundNodeDefs))
			{
				PTNodeDef* pRemoveNodeDef = eaRemove(&ppFoundNodeDefs, 0);

				for (iEscrowIdx = eaSize(peaEscrowNodes)-1; iEscrowIdx >= 0; iEscrowIdx--)
				{
					if (GET_REF((*peaEscrowNodes)[iEscrowIdx]->hNodeDef) == pRemoveNodeDef)
					{
						StructDestroyNoConst(parse_PTNodeDefRefCont, eaRemove(peaEscrowNodes, iEscrowIdx));
						bChanged = true;
						break;
					}
				}
			}
		}
		else
		{
			S32 iReplaceIdx = 0;
			while (pDiagNode->iCount > eaSize(&ppFoundNodeDefs))
			{
				if (iReplaceIdx < eaSize(&pDiagNode->ppReplacements))
				{
					NOCONST(PTNodeDefRefCont)* pEscrowNode = StructCreateNoConst(parse_PTNodeDefRefCont);
					PTNodeDef* pNodeDef = GET_REF(pDiagNode->ppReplacements[iReplaceIdx]->hNodeDef);
					eaPush(&ppFoundNodeDefs, pNodeDef);
					COPY_HANDLE(pEscrowNode->hNodeDef, pDiagNode->ppReplacements[iReplaceIdx]->hNodeDef);
					eaPush(peaEscrowNodes, pEscrowNode);
					iReplaceIdx++;
					bChanged = true;
				}
				else
				{
					break; //TODO: Error?
				}
			}
		}	
		eaDestroy(&ppFoundNodeDefs);
	}
	return bChanged;
}

/********************************************
*
*		SAVED PET TRANSFER FUNCTIONS
*		Used in trade, mail, and auction
*
********************************************/


// Returns true if pSrcEnt can transfer pPetEnt
bool Entity_CanInitiatePetTransfer(Entity* pSrcEnt, Entity* pPetEnt, char** pestrError)
{
	PetDef* pPetDef = NULL;
	ItemDef* pItemDef = NULL;
	int iPetID;
	int iPet;

	// Check for null entities
	if(!pSrcEnt || !pPetEnt)
	{
		entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_NullEntity",
			STRFMT_PLAYER(pSrcEnt),
			STRFMT_ENTITY_KEY("Pet", pPetEnt),
			STRFMT_END);
		return false;
	}

	// Is pet owned by src?
	if(!pPetEnt->pSaved || pPetEnt->pSaved->conOwner.containerID != pSrcEnt->myContainerID || pPetEnt->pSaved->conOwner.containerType != pSrcEnt->myEntityType)
	{
		entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_UnownedPet",
			STRFMT_PLAYER(pSrcEnt),
			STRFMT_ENTITY_KEY("Pet", pPetEnt),
			STRFMT_END);
		return false;
	}

	// Is pet tradeable?
	if(IsGameServerSpecificallly_NotRelatedTypes())
	{
		pPetDef = pPetEnt->pCritter ? GET_REF(pPetEnt->pCritter->petDef) : NULL;
		pItemDef = pPetDef ? GET_REF(pPetDef->hTradableItem) : NULL;

		if(!pItemDef)
		{
			entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_PetNotTradeable",
				STRFMT_PLAYER(pSrcEnt),
				STRFMT_ENTITY_KEY("Pet", pPetEnt),
				STRFMT_END);
			return false;
		}
	}

	iPetID = entGetContainerID(pPetEnt);

	// Find pet in owned containers
	for(iPet=eaSize(&pSrcEnt->pSaved->ppOwnedContainers)-1;iPet>=0;iPet--)
	{
		if((ContainerID)iPetID == pSrcEnt->pSaved->ppOwnedContainers[iPet]->conID)
		{
			PuppetEntity *pPuppet = SavedPet_GetPuppetFromPet(pSrcEnt,pSrcEnt->pSaved->ppOwnedContainers[iPet]);

			// Is pet online?
			if(pSrcEnt->pSaved->ppOwnedContainers[iPet]->curEntity)
			{
				entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_OnlinePet",
					STRFMT_PLAYER(pSrcEnt),
					STRFMT_ENTITY_KEY("Pet", pPetEnt),
					STRFMT_END);
				return false;
			}

			// Is pet an active puppet?
			if(pPuppet && pPuppet->eState == PUPPETSTATE_ACTIVE)
			{
				entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_ActivePuppet",
					STRFMT_PLAYER(pSrcEnt),
					STRFMT_ENTITY_KEY("Pet", pPetEnt),
					STRFMT_END);
				return false;
			}

			// Does pet have items in its inventory?
			if(pPetEnt && pPetEnt->pInventoryV2)
			{
				int NumBags,ItemCount,ii;

				NumBags = eaSize(&pPetEnt->pInventoryV2->ppInventoryBags);


				ItemCount = 0;
				for(ii=0;ii<NumBags;ii++)
				{
					if(pPetEnt->pInventoryV2->ppInventoryBags[ii]->BagID != InvBagIDs_Numeric)
						ItemCount += inv_bag_CountItems(pPetEnt->pInventoryV2->ppInventoryBags[ii], NULL);
				}

				if(ItemCount)
				{
					entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_PetWithItems",
						STRFMT_PLAYER(pSrcEnt),
						STRFMT_ENTITY_KEY("Pet", pPetEnt),
						STRFMT_END);
					return false;
				}
			}

			break;
		}
	}

	return true;
}

// Returns true if pDstEnt can accept pPetEnt
bool Entity_CanAcceptPetTransfer(Entity* pDstEnt, Entity* pPetEnt, GameAccountDataExtract *pExtract, char** pestrError)
{
	int iPetRank = 0;
	PetDef* pPetDef = NULL;

	// Check for null entities
	if(!pPetEnt || !pDstEnt)
	{
		entFormatGameMessageKey(pDstEnt, pestrError, "TradeError_NullEntity",
			STRFMT_PLAYER(pDstEnt),
			STRFMT_ENTITY_KEY("Pet", pPetEnt),
			STRFMT_END);
		return false;
	}

	iPetRank = Officer_GetRank(pPetEnt);

	// Is dst's rank too low to accept pet?
	if(iPetRank && iPetRank >= Officer_GetRank(pDstEnt))
	{
		entFormatGameMessageKey(pDstEnt, pestrError, "TradeError_PetRankTooHigh",
			STRFMT_PLAYER(pDstEnt),
			STRFMT_ENTITY_KEY("Pet", pPetEnt),
			STRFMT_END);
		return false;
	}
	
	pPetDef = pPetEnt->pCritter ? GET_REF(pPetEnt->pCritter->petDef) : NULL;
		
	// Does dst have enough room for pet?
	if(pPetDef && pPetDef->bCanBePuppet)
	{
		CritterDef* pPetCritterDef = GET_REF(pPetDef->hCritterDef);
		if(!Entity_CanAddPuppet(pDstEnt, pPetCritterDef?pPetCritterDef->pchClass:NULL, pExtract))
		{
			entFormatGameMessageKey(pDstEnt, pestrError, "TradeError_NoRoomForPuppet",
				STRFMT_PLAYER(pDstEnt),
				STRFMT_ENTITY_KEY("Pet", pPetEnt),
				STRFMT_END);
			return false;
		}
	}
	else
	{
		S32 iMaxOfficers = Officer_GetMaxAllowedPets(pDstEnt,GET_REF(pDstEnt->hAllegiance),pExtract);
		S32 iOfficers = Entity_CountPets(pDstEnt,true,false,false);

		if(iMaxOfficers > -1 && iMaxOfficers <= iOfficers)
		{
			entFormatGameMessageKey(pDstEnt, pestrError, "TradeError_NoRoomForPet",
				STRFMT_PLAYER(pDstEnt),
				STRFMT_ENTITY_KEY("Pet", pPetEnt),
				STRFMT_END);
			return false;
		}
	}

	return true;
}

// Returns true if pSrcEnt can transfer pPetEnt to pDstEnt
bool Entity_IsPetTransferValid(Entity* pSrcEnt, Entity* pDstEnt, Entity* pPetEnt, char** pestrError)
{
	AllegianceDef *pSrcAllegiance = NULL;
	AllegianceDef *pDstAllegiance = NULL;
	GameAccountDataExtract *pDstExtract;

	// Check for null entities
	if(!pSrcEnt || !pPetEnt || !pDstEnt)
	{
		entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_NullEntity",
			STRFMT_PLAYER(pSrcEnt),
			STRFMT_TARGET(pDstEnt),
			STRFMT_ENTITY_KEY("Pet", pPetEnt),
			STRFMT_END);
		return false;
	}

	// Can src initiate transfer?
	if(!Entity_CanInitiatePetTransfer(pSrcEnt, pPetEnt, pestrError))
		return false;
	

	// Grab the extract from the destination entity
	pDstExtract = entity_GetCachedGameAccountDataExtract(pDstEnt);

	// Can dst accept transfer?
	if(!Entity_CanAcceptPetTransfer(pDstEnt, pPetEnt, pDstExtract, pestrError))
		return false;

	pSrcAllegiance = GET_REF(pSrcEnt->hAllegiance);
	pDstAllegiance = GET_REF(pDstEnt->hAllegiance);

	// Do src and dst belong to different allegiances?
	if(!pSrcAllegiance || !pDstAllegiance || pSrcAllegiance != pDstAllegiance)
	{
		AllegianceDef *pPetAllegiance = GET_REF(pPetEnt->hAllegiance);
		bool bCanTrade = false;

		if (pSrcAllegiance && pDstAllegiance && pPetAllegiance)
		{
			AllegianceDef *pSrcSubAllegiance = GET_REF(pSrcEnt->hSubAllegiance);
			AllegianceDef *pDstSubAllegiance = GET_REF(pDstEnt->hSubAllegiance);

			if ((pSrcSubAllegiance && pSrcSubAllegiance == pDstAllegiance && pSrcSubAllegiance == pPetAllegiance) ||
				(pDstSubAllegiance && pDstSubAllegiance == pSrcAllegiance && pDstSubAllegiance == pPetAllegiance))
			{
				bCanTrade = true;
			}
		}

		if (!bCanTrade)
		{
			entFormatGameMessageKey(pSrcEnt, pestrError, "TradeError_AllegianceMismatch",
				STRFMT_PLAYER(pSrcEnt),
				STRFMT_TARGET(pDstEnt),
				STRFMT_ENTITY_KEY("Pet", pPetEnt),
				STRFMT_END);
			return false;
		}
	}

	return true;
}
void entGetLuckyCharmInfo(Entity* pOwner, Entity* pTargetEnt, int* pTypeOut, int* pIndexOut)
{
	S32 i;
	const PetTargetingInfo* pLowestIndexTarget = NULL;

	if ( pOwner)
	{
		const PetTargetingInfo** eaTargetingInfo = mapState_GetPetTargetingInfo(pOwner);
		int iPartitionIdx = entGetPartitionIdx(pOwner);
		for ( i = 0; i < eaSize(&eaTargetingInfo); i++) 
		{
			Entity* pEnt = entFromEntityRef(iPartitionIdx, eaTargetingInfo[i]->erTarget);
			if ( pEnt && entIsAlive(pEnt) && 
				pEnt == pTargetEnt)
			{
				*pTypeOut = eaTargetingInfo[i]->eType;
				*pIndexOut = eaTargetingInfo[i]->iIndex;
				return;
			}
		}
	}

	*pTypeOut = -1;
	*pIndexOut = -1;
}

U32 PetCommands_GetLowestIndexEntRefByLuckyCharm(Entity* pOwner, S32 eType)
{
	S32 i;
	const PetTargetingInfo* pLowestIndexTarget = NULL;

	if ( pOwner)
	{
		const PetTargetingInfo** eaTargetingInfo = mapState_GetPetTargetingInfo(pOwner);
		int iPartitionIdx = entGetPartitionIdx(pOwner);
		for ( i = 0; i < eaSize(&eaTargetingInfo); i++) 
		{
			Entity* pEnt = entFromEntityRef(iPartitionIdx, eaTargetingInfo[i]->erTarget);
			if ( pEnt && entIsAlive(pEnt) && 
				eaTargetingInfo[i]->eType == eType &&
				(!pLowestIndexTarget || eaTargetingInfo[i]->iIndex < pLowestIndexTarget->iIndex))
			{
				pLowestIndexTarget = eaTargetingInfo[i];
			}
		}
	}

	return pLowestIndexTarget ? pLowestIndexTarget->erTarget : -1;
}

bool SavedPet_HasRequirementsToResummonDeceasedPet(Entity *pOwner)
{
	if (g_PetRestrictions.pchRequiredItemForDeceasedPets)
	{
		// this pet was dead and we require that the pet need an item to be summoned again
		// check to see if the owner has the item 
		return inv_ent_AllBagsCountItems(pOwner, g_PetRestrictions.pchRequiredItemForDeceasedPets);
	}
	return true;
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(SavedPetsClearAllDeceased);
void exprSavedPet_SavedPetsClearAllDeceased(SA_PARAM_OP_VALID Entity *pEnt)
{
	if(!pEnt || !pEnt->pSaved)
		return;

	FOR_EACH_IN_EARRAY(pEnt->pSaved->ppAllowedCritterPets, PetDefRefCont, pPetRef)
	{
		pPetRef->bPetIsDeceased = false;
	}
	FOR_EACH_END
}

PetIntroductionWarp* Entity_GetPetIntroductionWarp(Entity* pEnt, U32 uPetID)
{
	PetRelationship* pPet = SavedPet_GetPetFromContainerID(pEnt, uPetID, false);
	if (pPet)
	{
		S32 iLevel = entity_GetSavedExpLevel(pEnt);
		bool bIsPuppet = SavedPet_IsPetAPuppet(pEnt, pPet);
		int i;
		for (i = eaSize(&g_PetRestrictions.eaPetIntroWarp)-1; i >= 0; i--)
		{
			PetIntroductionWarp* pWarp = g_PetRestrictions.eaPetIntroWarp[i];
			if (!!pWarp->bPuppet == !!bIsPuppet &&
				pWarp->iRequiredLevel <= iLevel &&
				(pWarp->pchAllegiance == REF_STRING_FROM_HANDLE(pEnt->hAllegiance) ||
				 pWarp->pchAllegiance == REF_STRING_FROM_HANDLE(pEnt->hSubAllegiance)))
			{
				return pWarp;
			}
		}
	}
	return NULL;
}

bool Entity_CanUsePetIntroWarp(Entity* pEnt, PetIntroductionWarp* pWarp)
{
	if (pEnt && pEnt->pSaved && pWarp)
	{
		S32 iLevel = entity_GetSavedExpLevel(pEnt);
		if (pWarp && pWarp->iRequiredLevel > iLevel)
		{
			return false;
		}
		if (!entIsAlive(pEnt) || entIsInCombat(pEnt))
		{
			return false;
		}
		if (!allegiance_CanPlayerUseWarp(pEnt))
		{
			return false;
		}
		return true;
	}
	return false;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Psaved.Pppreferredpetids, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets");
bool trhEntity_CanSetPreferredPet(ATH_ARG NOCONST(Entity)* pEntity, U32 uiPetID, S32 iIndex)
{
	NOCONST(PetRelationship)* pPet;

	if (ISNULL(pEntity) || ISNULL(pEntity->pSaved))
		return false;
	if (iIndex < 0 || iIndex >= TEAM_MAX_SIZE-1 || iIndex > ea32Size(&pEntity->pSaved->ppPreferredPetIDs))
		return false;

	if (uiPetID)
	{
		pPet = trhSavedPet_GetPetFromContainerID(pEntity, uiPetID, true);
		if (ISNULL(pPet))
		{
			return false;
		}
	}
	else if (iIndex == ea32Size(&pEntity->pSaved->ppPreferredPetIDs))
	{
		return false;
	}
	return true;
}

// Get the container ID of the pet from a pet id
ContainerID SavedPet_GetConIDFromPetID(Entity *pEntity, U32 uiPetID)
{
	if(pEntity && pEntity->pSaved)
	{
		S32 i;

		for(i = 0; i < eaSize(&pEntity->pSaved->ppOwnedContainers); ++i)
		{
			if(pEntity->pSaved->ppOwnedContainers[i]->uiPetID == uiPetID)
			{
				return pEntity->pSaved->ppOwnedContainers[i]->conID;
			}
		}
	}

	return 0;
}

void PropEntIDs_FillWithActiveEntIDs(PropEntIDs *pIDs, Entity* pEnt)
{
	int i;
	if (SAFE_MEMBER2(pEnt, pSaved, pPuppetMaster))
	{
		for (i=0; i < eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets); i++)
		{
			if (pEnt->pSaved->pPuppetMaster->ppPuppets[i]->eState == PUPPETSTATE_ACTIVE)
			{
				ea32Push(&pIDs->eauiPropEntIDs, pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID);
			}
		}
	}
}

void PropEntIDs_Destroy(PropEntIDs *pIDs)
{
	ea32Destroy(&pIDs->eauiPropEntIDs);
}

#include "AutoGen/SavedPetCommon_h_ast.c"
