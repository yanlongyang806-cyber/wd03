/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "Character.h"
#include "CharacterClass.h"
#include "contact_enums.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "entity.h"
#include "EntitySavedData.h"
#include "gslCostume.h"
#include "gslEntity.h"
#include "itemCommon.h"
#include "player.h"
#include "powers.h"
#include "GameAccountDataCommon.h"
#include "species_common.h"
#include "AutoTransDefs.h"

#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/gslCostume_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// --------------------------------------------------------------------------
//  General Utilities
// --------------------------------------------------------------------------


void costumeEntity_SetCostumeRefDirty(Entity* e)
{
	ENTINFO(e).regularDiffNextFrame = 1;

	entity_SetDirtyBit(e, parse_CostumeRef, &e->costumeRef, true);

	// Force this value to be re-sent to clients.
	
	e->costumeRef.dirtiedCount++;
}

void costumeEntity_SetCostume(Entity *e, const PlayerCostume* pCostume, bool bClearSubstitute)
{
	CostumeRef_AutoGen_NoConst *pCostumeRef = (CostumeRef_AutoGen_NoConst *)SAFE_MEMBER_ADDR(e, costumeRef);
	NOCONST(Entity) *pNoConstEnt = CONTAINER_NOCONST(Entity, e);

	if( !e || !pCostume )
		return;

	if (e->myEntityType == GLOBALTYPE_ENTITYPLAYER
		|| e->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		// Players and saved pets should never have a costume set anywhere
		// except in a transaction.  Simply ignore this request to set the costume.
		return;
	}

	// Clear old data as it may have been from previous setCostumes
	REMOVE_HANDLE(pCostumeRef->hReferencedCostume);
	if (pCostumeRef->pcDestructibleObjectCostume)
	{
		StructFreeString(pCostumeRef->pcDestructibleObjectCostume);
		pCostumeRef->pcDestructibleObjectCostume = NULL;
	}
	if (pCostumeRef->pSubstituteCostume!=NULL)
	{
		StructDestroyNoConst(parse_PlayerCostume, pCostumeRef->pSubstituteCostume);
		pCostumeRef->pSubstituteCostume=NULL;
	}

	// WOLF[3Jan12] The incoming costume may be a reference handle or a struct clone of one that
	//  has been modified (e.g. for texture name decals). Assign it to either the hReferencedCostume
	//  field or clone it into the pSubstituteCostume field depending on which it is.
	//  pSubstituteCostume takes precedence when displaying.
	
	if (RefSystem_DoesReferentExist(pCostume))
	{
		SET_HANDLE_FROM_REFERENT("PlayerCostume",pCostume,pCostumeRef->hReferencedCostume);
		if (pCostumeRef->pSubstituteCostume && bClearSubstitute)
		{
			StructDestroyNoConst(parse_PlayerCostume, pCostumeRef->pSubstituteCostume);
			pCostumeRef->pSubstituteCostume = NULL;
		}
	}
	else
	{
		// We are not a referenceable handle, so use the substitute costume slot instead
		pCostumeRef->pSubstituteCostume = StructCloneDeConst(parse_PlayerCostume, pCostume);
	}

	if (!e->eGender)
	{
		// This is okay to use a noconst ent since it is never on a persisted ent
		pNoConstEnt->eGender = costumeEntity_GetEffectiveCostumeGender(e);
		entity_SetDirtyBit(e, parse_Entity, e, false);
	}

	costumeEntity_SetCostumeRefDirty(e);
	costumeEntity_RegenerateCostume(e);
}


// DO NOT call this function on a persisted entity
void costumeEntity_SetCostumeByName(Entity *e, const char* pcCostume)
{
	PlayerCostume *pCostume = (PlayerCostume*)costumeEntity_CostumeFromName(pcCostume);
	if ( !pCostume )
	{
		Errorf("Failed to find costume %s", pcCostume);
		return;
	}
	costumeEntity_SetCostume(e, pCostume, true);
}


// DO NOT call this function on a persisted entity
void costumeEntity_SetDestructibleObjectCostumeByName(Entity *e, const char* pcDestructibleObjectCostume)
{
	Entity *pEnt = e;
	CostumeRef_AutoGen_NoConst *pCostumeRef = (CostumeRef_AutoGen_NoConst *)SAFE_MEMBER_ADDR(pEnt, costumeRef);

	if (!pEnt)
		return;

	REMOVE_HANDLE(pCostumeRef->hReferencedCostume);
	if (pCostumeRef->pcDestructibleObjectCostume)
	{
		StructFreeString(pCostumeRef->pcDestructibleObjectCostume);
		pCostumeRef->pcDestructibleObjectCostume = NULL;
	}

	pEnt->costumeRef.pcDestructibleObjectCostume = StructAllocString(pcDestructibleObjectCostume);
	//gslEntityForceFullSend(e); // Client won't pick up on the costume change unless we force a full send
	costumeEntity_SetCostumeRefDirty(e);
	costumeEntity_RegenerateCostume(pEnt);
}


// Helper function for gslEntityGenerateCostume
static S32 costumeEntity_ApplyNewCostume(Entity *pEnt, PlayerCostume *pNewCostume)
{
	if (!pEnt->costumeRef.pEffectiveCostume ||
		StructCompare(parse_PlayerCostume, pEnt->costumeRef.pEffectiveCostume, pNewCostume, 0, 0, 0) != 0) {
		// Remove previous effective costume (if any)
		if (pEnt->costumeRef.pEffectiveCostume) {
			StructDestroy(parse_PlayerCostume, pEnt->costumeRef.pEffectiveCostume);
		}

		// Apply new costume
		pEnt->costumeRef.pEffectiveCostume = pNewCostume;

		// Force it to push entity on next tick
		costumeEntity_SetCostumeRefDirty(pEnt);

		return true;
	} else {
		// Existing costume matches, so simply free this one as no longer needed
		StructDestroy(parse_PlayerCostume, pNewCostume);

		return false;
	}
}

static S32 costumeEntity_ApplyNewMountCostume(Entity *pEnt, PlayerCostume *pNewCostume, F32 fMountScaleOverride)
{
	if (!pEnt->costumeRef.pMountCostume ||
		pEnt->costumeRef.fMountScaleOverride < fMountScaleOverride-.001f ||
		pEnt->costumeRef.fMountScaleOverride > fMountScaleOverride+.001f ||
		StructCompare(parse_PlayerCostume, pEnt->costumeRef.pMountCostume, pNewCostume, 0, 0, 0) != 0)
	{
		//Apply the new costume
		if (pEnt->costumeRef.pMountCostume) {
			StructDestroy(parse_PlayerCostume, pEnt->costumeRef.pMountCostume);
		}
		pEnt->costumeRef.pMountCostume = pNewCostume;
		pEnt->costumeRef.fMountScaleOverride = fMountScaleOverride;
		costumeEntity_SetCostumeRefDirty(pEnt);
		return true;
	} else {
		//Existing costume matches, so simply free this one as no longer needed
		StructDestroy(parse_PlayerCostume, pNewCostume);
		return false;
	}
}

NOCONST(PlayerCostume) *costumeEntity_CreateCostumeWithItemsAndPowers(int iPartitionIdx, Entity *pEnt, CostumeDisplayData ***peaMountData, GameAccountDataExtract *pExtract)
{
	PlayerCostume *pBaseCostume;
	PCSlotType *pSlotType;
	NOCONST(PlayerCostume) *pNewCostume = NULL;
	CostumeDisplayData **eaData = NULL;
	int i;

	PERFINFO_AUTO_START("Get Item Data", 1);

	pBaseCostume = costumeEntity_GetBaseCostume(pEnt);
	pSlotType = costumeEntity_GetActiveSavedSlotType(pEnt);

	// Get costume data from items
	if (!pBaseCostume || !pBaseCostume->bPlayerCantChange)
	{
		item_GetItemCostumeDataToShow(iPartitionIdx, pEnt, &eaData, pExtract);
	}

	PERFINFO_AUTO_STOP_START("Get Powers Data", 1);

	// Get costume data from powers
	powers_GetPowerCostumeDataToShow(pEnt, &eaData, peaMountData);

	PERFINFO_AUTO_STOP();

	if (eaSize(&eaData))
	{
		PERFINFO_AUTO_START("Got Data", 1);

		pNewCostume = costumeTailor_ApplyOverrideSet(pBaseCostume, pSlotType, eaData, pEnt->pChar ? GET_REF(pEnt->pChar->hSpecies) : NULL);

		// Clean up
		for(i=eaSize(&eaData)-1; i>=0; --i)
		{
			eaDestroy(&eaData[i]->eaCostumes);
			eaDestroy(&eaData[i]->eaAddedFX);
			eaDestroyStruct(&eaData[i]->eaCostumesOwned, parse_PlayerCostume);
			free(eaData[i]);
		}
		eaDestroy(&eaData);
		PERFINFO_AUTO_STOP(); // Got Data
	}
	return pNewCostume;
}

void costumeEntity_ApplyItemsAndPowersToCostume(int iPartitionIdx, Entity *pEnt, S32 bFixOptional, GameAccountDataExtract *pExtract)
{
	bool bDirty = false;
	S32 bChanged = true; // Assume something actually changes
	CostumeDisplayData **eaMountData = NULL;
	NOCONST(PlayerCostume) *pNewCostume;
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (pNewCostume = costumeEntity_CreateCostumeWithItemsAndPowers(iPartitionIdx, pEnt, &eaMountData, pExtract))
	{
		// Put the costume in place and force send as required
		bChanged = costumeEntity_ApplyNewCostume(pEnt, (PlayerCostume*)pNewCostume);
	}
	else if (pEnt->costumeRef.pEffectiveCostume)
	{
		PERFINFO_AUTO_START("Remove costume", 1);
		// Remove previous effective costume (if any)
		StructDestroy(parse_PlayerCostume, pEnt->costumeRef.pEffectiveCostume);
		pEnt->costumeRef.pEffectiveCostume = NULL;
		bDirty = true;
		PERFINFO_AUTO_STOP();// Remove costume
	}

	if (eaSize(&eaMountData))
	{
		F32 fMountScaleOverride;

		PERFINFO_AUTO_START("Got Mount Data", 1);

		pNewCostume = costumeTailor_ApplyMount(eaMountData, &fMountScaleOverride);

		if (pNewCostume) {
			bChanged = costumeEntity_ApplyNewMountCostume(pEnt, (PlayerCostume*)pNewCostume, fMountScaleOverride);
		}

		for(i=eaSize(&eaMountData)-1; i>=0; --i) {
			eaDestroy(&eaMountData[i]->eaCostumes);
			eaDestroyStruct(&eaMountData[i]->eaCostumesOwned, parse_PlayerCostume);
			free(eaMountData[i]);
		}
		eaDestroy(&eaMountData);

		PERFINFO_AUTO_STOP(); // Got Mount Data
	}
	else if (pEnt->costumeRef.pMountCostume)
	{
		PERFINFO_AUTO_START("Remove mount costume", 1);
		// Remove previous mount costume (if any)
		StructDestroy(parse_PlayerCostume, pEnt->costumeRef.pMountCostume);
		pEnt->costumeRef.pMountCostume = NULL;
		bDirty = true;
		PERFINFO_AUTO_STOP();// Remove mount costume
	}

	if (bDirty) {
		costumeEntity_SetCostumeRefDirty(pEnt);
	}

	if(bChanged || !bFixOptional)
	{
		costumeGenerate_FixEntityCostume(pEnt);

		gslEntityUpdateSendDistance(pEnt);
	}

	PERFINFO_AUTO_STOP();
}

bool costumeEntity_CanPlayerEditCostume(Entity *pEnt, Entity *pCostumeEnt)
{
	if (pEnt && pEnt->pPlayer) {
		int iClass = StaticDefineIntGetInt(CharClassTypesEnum, "Space");
		GameAccountData *pData = entity_GetGameAccount(pEnt);

		//Allows the debug tailor to work
		if(entGetAccessLevel(pEnt) >= ACCESS_DEBUG)
			return(true);

		//IF you have tailor payment choices turned on, you cannot use tokens away from a tailor contact to change your costume
		if(!gConf.bTailorPaymentChoice)
		{
			if (costumeEntity_GetFreeChangeTokens(NULL, pCostumeEnt) > 0)
			{
				return true;
			}

			if (costumeEntity_GetFreeChangeTokens(pEnt, pCostumeEnt) > 0)
			{
				return true;
			}

			if (costumeEntity_GetFreeFlexChangeTokens(pEnt) > 0)
			{
				return true;
			}

			if (pData)
			{
				if(costumeEntity_GetAccountChangeTokens(pEnt, pData))
					return true;
			}
		}

		if ((pEnt->pPlayer->InteractStatus.eNearbyContactTypes & ContactFlag_Tailor) && (GetCharacterClassEnum( pCostumeEnt ) != iClass)) {
			return true;
		} else if ((pEnt->pPlayer->InteractStatus.eNearbyContactTypes & ContactFlag_StarshipTailor) && (GetCharacterClassEnum( pCostumeEnt ) == iClass)) {
			return true;
		} else if ((pEnt->pPlayer->InteractStatus.eNearbyContactTypes & ContactFlag_WeaponTailor) && (GetCharacterClassEnum( pCostumeEnt ) != iClass)) {
			return true;
		}
	}
	return false;
}

// Make a plain costume
AUTO_TRANS_HELPER;
NOCONST(PlayerCostume) *costumeEntity_trh_MakePlainCostume(ATH_ARG NOCONST(Entity) *pEnt)
{
	SpeciesDef *pSpecies = NONNULL(pEnt->pChar)?GET_REF(pEnt->pChar->hSpecies):NULL;
	NOCONST(PlayerCostume) *pCostume = CONTAINER_NOCONST(PlayerCostume, costumeEntity_trh_GetActiveSavedCostume(pEnt));
	int i;
	PCSkeletonDef *pSkel = pCostume ? GET_REF(pCostume->hSkeleton) : NULL;
	char **eaPowerFXBones = NULL;	// just needed for calls

	if (!pSkel) {
		// If costumeEntity_GetActiveSavedCostume returned NULL, find a PCSkeletonDef for the Entity
		if (pSpecies && GET_REF(pSpecies->hSkeleton)) {
			pSkel = GET_REF(pSpecies->hSkeleton);
		} else {
			PCSkeletonDef **eaValidSkeletons = NULL;
			NOCONST(PlayerCostume) tmpCostume = {0};
			// Assume a player created costume
			tmpCostume.eCostumeType = kPCCostumeType_Player;
			costumeTailor_GetValidSkeletons(&tmpCostume, pSpecies, &eaValidSkeletons, true, true);
			for (i=0; i<eaSize(&eaValidSkeletons); ++i) {
				if (eaValidSkeletons[i]->eGender == pEnt->eGender) {
					pSkel = eaValidSkeletons[i];
					break;
				}
			}
			if (!pSkel && eaSize(&eaValidSkeletons) > 0) {
				pSkel = eaValidSkeletons[0];
			}
			if (!pSkel) {
				// If pSkel is still NULL, then something is wrong with the data.
				return NULL;
			}
		}
	}

	pCostume = StructCreateNoConst(parse_PlayerCostume);
	pCostume->eCostumeType = kPCCostumeType_Player;

	eaCreate(&eaPowerFXBones);

	SET_HANDLE_FROM_REFERENT(g_hCostumeSkeletonDict, pSkel, pCostume->hSkeleton);

	if (pSkel && pSkel->fDefaultHeight) {
		pCostume->fHeight = pSkel->fDefaultHeight;
	} else {
		pCostume->fHeight = 6;
	}
	if (pSkel && pSkel->fDefaultMuscle) {
		pCostume->fMuscle = pSkel->fDefaultMuscle;
	} else {
		pCostume->fMuscle = 20;
	}
	if (pSkel) {
		for(i=0; i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
			if (i < eafSize(&pSkel->eafDefaultBodyScales)) {
				eafPush(&pCostume->eafBodyScales, pSkel->eafDefaultBodyScales[i]);
			} else {
				eafPush(&pCostume->eafBodyScales, 20);
			}
		} 
	}
	costumeTailor_SetDefaultSkinColor(pCostume, pSpecies, NULL);
	costumeTailor_FillAllBones(pCostume, pSpecies, eaPowerFXBones, costumeEntity_trh_GetActiveSavedSlotType(pEnt), true, false, true);

	eaDestroy(&eaPowerFXBones);
	
	return pCostume;
}


void costumeEntity_ResetCostumeData(Entity *pEnt)
{
	if (pEnt->pSaved) {
		PlayerCostume *pCostume = costumeEntity_GetActiveSavedCostume(pEnt);
		if(pCostume)
		{
			if (pCostume->eGender)
			{
				entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
			}
			else if (GET_REF(pCostume->hSkeleton) && GET_REF(pCostume->hSkeleton)->eGender)
			{
				entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
			}
		}
		//This should make the client pick up any changes in the costume list and index of the costume
		entity_SetDirtyBit(pEnt,parse_PlayerCostumeData, &pEnt->pSaved->costumeData, true);
		entity_SetDirtyBit(pEnt,parse_SavedEntityData, pEnt->pSaved, true);
	}
	
	costumeEntity_ResetStoredCostume(pEnt);
	costumeEntity_RegenerateCostume(pEnt);     // Regenerate costume after change
	costumeEntity_SetCostumeRefDirty(pEnt);

}

void gslCheckCostumePartReferences(PCPart* pPart)
{
	PCTextureDef *pTexDef = NULL;
	int i;
	if (IS_HANDLE_ACTIVE(pPart->hBoneDef) && !RefSystem_IsReferentStringValid(g_hCostumeBoneDict, REF_STRING_FROM_HANDLE(pPart->hBoneDef)))
	{
		Errorf("Currently active player costume references nonexistent BoneDef %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->hBoneDef));
	}
	if (IS_HANDLE_ACTIVE(pPart->hDetailTexture) && !RefSystem_IsReferentStringValid(g_hCostumeTextureDict, REF_STRING_FROM_HANDLE(pPart->hDetailTexture)))
	{
		Errorf("Currently active player costume references nonexistent Detail Texture %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->hDetailTexture));
	}
	if (IS_HANDLE_ACTIVE(pPart->hDiffuseTexture) && !RefSystem_IsReferentStringValid(g_hCostumeTextureDict, REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture)))
	{
		Errorf("Currently active player costume references nonexistent Diffuse Texture %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture));
	}
	if (IS_HANDLE_ACTIVE(pPart->hGeoDef) && !RefSystem_IsReferentStringValid(g_hCostumeGeometryDict, REF_STRING_FROM_HANDLE(pPart->hGeoDef)))
	{
		Errorf("Currently active player costume references nonexistent GeoDef %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->hGeoDef));
	}
	if (IS_HANDLE_ACTIVE(pPart->hMatDef) && !RefSystem_IsReferentStringValid(g_hCostumeMaterialDict, REF_STRING_FROM_HANDLE(pPart->hMatDef)))
	{
		Errorf("Currently active player costume references nonexistent MatDef %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->hMatDef));
	}
	if (IS_HANDLE_ACTIVE(pPart->hPatternTexture) && !RefSystem_IsReferentStringValid(g_hCostumeTextureDict, REF_STRING_FROM_HANDLE(pPart->hPatternTexture)))
	{
		Errorf("Currently active player costume references nonexistent Pattern Texture %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->hPatternTexture));
	}
	if (IS_HANDLE_ACTIVE(pPart->hSpecularTexture) && !RefSystem_IsReferentStringValid(g_hCostumeTextureDict, REF_STRING_FROM_HANDLE(pPart->hSpecularTexture)))
	{
		Errorf("Currently active player costume references nonexistent Specular Texture %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->hSpecularTexture));
	}
	if (pPart->pMovableTexture) {
		pTexDef = GET_REF(pPart->pMovableTexture->hMovableTexture);
		if (!pTexDef && REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture)) {
			Errorf("Currently active player costume references nonexistent Movable Texture %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture));
		}

	}
	if (pPart->pArtistData) {
		for(i=0; i<eaSize(&pPart->pArtistData->eaExtraTextures); ++i) {
			pTexDef = GET_REF(pPart->pArtistData->eaExtraTextures[i]->hTexture);
			if (!pTexDef && REF_STRING_FROM_HANDLE(pPart->pArtistData->eaExtraTextures[i]->hTexture)) {
				Errorf("Currently active player costume references nonexistent Extra Texture %s. You may be using an old character with persisted invalid costume data.", REF_STRING_FROM_HANDLE(pPart->pArtistData->eaExtraTextures[i]->hTexture));
			}
		}
	}
}


void gslCheckForDeprecatedCostumeParts(Entity* pEnt)
{
	PlayerCostume* pPCCostume;
	int i;

	PERFINFO_AUTO_START_FUNC();

	pPCCostume = costumeEntity_GetEffectiveCostume(pEnt);

    if (pPCCostume == NULL)
    {
        costumeEntity_ResetStoredCostume(pEnt);
        pPCCostume = costumeEntity_GetEffectiveCostume(pEnt);
    }

	if (pPCCostume) {
		for(i=0; i<eaSize(&pPCCostume->eaParts); ++i) {
			gslCheckCostumePartReferences(pPCCostume->eaParts[i]);
		}
	} else if (pEnt) {
		ErrorDetailsf("Entity %s", ENTDEBUGNAME(pEnt));
		Errorf("Couldn't get an effective costume when checking for deprecated costume parts");
	}

	PERFINFO_AUTO_STOP();
}

#include "AutoGen/gslCostume_h_ast.c"
