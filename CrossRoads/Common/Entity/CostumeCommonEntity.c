/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntitySavedData.h"
#include "Expression.h"
#include "file.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "itemCommon.h"
#include "microtransactions_common.h"
#include "mission_common.h"
#include "Player.h"
#include "PowerVars.h"
#include "RegionRules.h"
#include "rewardCommon.h"
#include "StringCache.h"
#include "TransformationCommon.h"
#include "wlCostume.h"
#include "GamePermissionsCommon.h"
#include "inventoryCommon.h"

#ifdef GAMESERVER
#include "gslEntity.h"
#include "gslCostume.h"
#endif

#ifdef GAMECLIENT
#include "gclLogin2.h"
#include "Login2CharacterDetail.h"
#endif

#include "AutoGen/character_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// --------------------------------------------------------------------------
//  Static Data 
// --------------------------------------------------------------------------

bool gValidateCostumePartsNextTick = false;
static bool gUpdateEntityCostumesOnNextTick = false;
static bool gCostumeAssetsAdded = false;
bool gCostumeAssetsModified = false;
static const char **geaChangedCostumes = NULL;

extern PCSlotSets g_CostumeSlotSets;

ExprContext *s_pCostumeContext = NULL;


// --------------------------------------------------------------------------
// General Logic
// --------------------------------------------------------------------------

PlayerCostume *costumeEntity_CostumeFromName(const char *pcName)
{
	return RefSystem_ReferentFromString(g_hPlayerCostumeDict, pcName);
}


WLCostume* costumeEntity_GetWLCostume( Entity* e )
{
	if( e )
		return GET_REF( e->hWLCostume );
	else
		return NULL;
}


// This function gets the non-persisted costume that is currently effective for the entity.  
// It will return NULL on any entity that is not alive and being processed.
// This costume will include the effects of any items and powers.
// Use "costumeEntity_GetSavedCostume()" on those non-live entities.
PlayerCostume *costumeEntity_GetEffectiveCostume( Entity *pEnt )
{
	PlayerCostume *pCostume = NULL;

	if (pEnt) {
#ifdef GAMECLIENT
		if (pEnt->costumeRef.pTransformation && pEnt->costumeRef.pTransformation->pCurrentCostume) {
			return pEnt->costumeRef.pTransformation->pCurrentCostume;
		}
#endif
		// If has effective costume, use it
		if (pEnt->costumeRef.pEffectiveCostume) {
			return pEnt->costumeRef.pEffectiveCostume;
		}

		// It has stored costume, use it
		if (pEnt->costumeRef.pStoredCostume) {
			return pEnt->costumeRef.pStoredCostume;
		}

		// If no persisted costume, look for substitute one, then referenced one
		if (!pCostume) {
			pCostume = pEnt->costumeRef.pSubstituteCostume;
		}
		if (!pCostume) {
			pCostume = GET_REF(pEnt->costumeRef.hReferencedCostume);
		}
	}

	return pCostume;
}


Gender costumeEntity_GetEffectiveCostumeGender(SA_PARAM_NN_VALID Entity* pEnt)
{
	PlayerCostume* pCostume = costumeEntity_GetEffectiveCostume(pEnt);
	if (pCostume) {
		return costumeTailor_GetGender(pCostume);
	} else {
		return Gender_Unknown;
	}
}


// This function gets the non-persisted costume that is the base for the entity.
// It will return NULL on any entity that is not alive and being processed.
// This costume will include the effects of any items and powers.
// Use "costumeEntity_GetSavedCostume()" on those non-live entities.
PlayerCostume *costumeEntity_GetBaseCostume( Entity *pEnt )
{
	PlayerCostume *pCostume = NULL;

	if (pEnt) {
		// It has stored costume, use it
		if (pEnt->costumeRef.pStoredCostume) {
			return pEnt->costumeRef.pStoredCostume;
		}

		// If no persisted costume, look for substitute one, then referenced one
		if (!pCostume) {
			pCostume = pEnt->costumeRef.pSubstituteCostume;
		}
		if (!pCostume) {
			pCostume = GET_REF(pEnt->costumeRef.hReferencedCostume);
		}
	}

	return pCostume;
}

//This gets the mount costume on the entity.
PlayerCostume *costumeEntity_GetMountCostume(Entity *pEnt, F32 *fOutMountScaleOverride)
{
	if (pEnt)
	{
		*fOutMountScaleOverride = pEnt->costumeRef.fMountScaleOverride;
		return pEnt->costumeRef.pMountCostume;
	}
	return NULL;
}

// This gets the saved costume on the entity.  
// This will return NULL on critters or any other non-persistent entity.
// This costume will NOT include the effects of any items or powers.  Those
// are only available via "costumeEntity_GetEffectiveCostume" while the entity is live.
PlayerCostume *costumeEntity_GetSavedCostume( Entity *pEnt, int index )
{
	if (pEnt && pEnt->pSaved){
		PlayerCostumeSlot *pSlot = eaGet(&pEnt->pSaved->costumeData.eaCostumeSlots, index);
		if (pSlot) {
			return pSlot->pCostume;
		}
	}
	return NULL;
}

// This gets the Active saved costume on the entity.  
// This will return NULL on critters or any other non-persistent entity.
// This costume will NOT include the effects of any items or powers.  Those
// are only available via "costumeEntity_GetEffectiveCostume" while the entity is live.
AUTO_TRANS_HELPER;
PlayerCostume *costumeEntity_trh_GetActiveSavedCostume(ATH_ARG NOCONST(Entity) *pEnt )
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved)){
		PlayerCostumeSlot *pSlot = CONTAINER_RECONST(PlayerCostumeSlot, eaGet(&pEnt->pSaved->costumeData.eaCostumeSlots, pEnt->pSaved->costumeData.iActiveCostume));
		if (pSlot) {
			return pSlot->pCostume;
		}
	}
	return NULL;
}


// This gets the Active saved costume's slot type on the entity.  
// This will return NULL on critters or any other non-persistent entity.
AUTO_TRANS_HELPER;
PCSlotType *costumeEntity_trh_GetActiveSavedSlotType(ATH_ARG NOCONST(Entity) *pEnt )
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved)){
		PlayerCostumeSlot *pSlot = CONTAINER_RECONST(PlayerCostumeSlot, eaGet(&pEnt->pSaved->costumeData.eaCostumeSlots, pEnt->pSaved->costumeData.iActiveCostume));
		if (pSlot) {
			return costumeLoad_GetSlotType(pSlot->pcSlotType);
		}
	}
	return NULL;
}

void costumeEntity_ResetStoredCostume( Entity *pEnt )
{
	if (IsServer() && pEnt && pEnt->pSaved) {
		PlayerCostume *pCostume = NULL;

		// Clean up previous stored costume
		StructDestroySafe(parse_PlayerCostume, &pEnt->costumeRef.pStoredCostume);

		// Find preferred new stored costume
		if (pEnt->pSaved->costumeData.iActiveCostume < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots)) {
			pCostume = pEnt->pSaved->costumeData.eaCostumeSlots[pEnt->pSaved->costumeData.iActiveCostume]->pCostume;
		} 
		if (!pCostume && eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots)) {
			pCostume = pEnt->pSaved->costumeData.eaCostumeSlots[0]->pCostume;
		}

		// Copy the found costume
		if (pCostume) {
			pEnt->costumeRef.pStoredCostume = StructClone(parse_PlayerCostume, pCostume);
		}

#ifdef GAMESERVER
		// Mark it dirty
		costumeEntity_SetCostumeRefDirty(pEnt);
#endif //GAMESERVER
	}
}


void costumeEntity_RegenerateCostumeEx(int iPartitionIdx, Entity *pEnt, GameAccountDataExtract *pExtract)
{
#ifdef GAMESERVER
	costumeEntity_ApplyItemsAndPowersToCostume(iPartitionIdx, pEnt, false, pExtract);
#else
	costumeGenerate_FixEntityCostume(pEnt);
#endif
}


void costumeEntity_RegenerateCostume(Entity *pEnt)
{
#ifdef GAMESERVER
	costumeEntity_RegenerateCostumeEx(entGetPartitionIdx(pEnt), pEnt, entity_GetCachedGameAccountDataExtract(pEnt));
#else
	costumeGenerate_FixEntityCostume(pEnt);
#endif
}


// --------------------------------------------------------------------------
// Cost to Change Logic
// --------------------------------------------------------------------------

// Gets the cost of changing a particular part
static S32 costumeEntity_GetCostToChangeBone(CostumePrices *pPrices, NOCONST(PlayerCostume) *pSrcCostume, NOCONST(PlayerCostume) *pDstCostume, PCBoneDef *pBone, bool *changeDetected) {
	const char *pcNone = allocAddString("None");
	S32 cost = 0;
	NOCONST(PCPart) *pCurPart = costumeTailor_GetPartByBone(pDstCostume, pBone, NULL);
	NOCONST(PCPart) *pStartPart = costumeTailor_GetPartByBone(pSrcCostume, pBone, NULL);
	F32 colorDif0, colorDif1, colorDif2, colorDif3;

	if ((pCurPart != NULL && REF_STRING_FROM_HANDLE(pCurPart->hGeoDef) != pcNone) || (pStartPart != NULL && REF_STRING_FROM_HANDLE(pStartPart->hGeoDef) != pcNone)) {
		if (pCurPart == NULL || pStartPart == NULL || GET_REF(pCurPart->hGeoDef) != GET_REF(pStartPart->hGeoDef)) {
			*changeDetected = true;
			if (pPrices) {
				cost += pPrices->iGeometry;
			}
		} else {
			const char *pcCurName = NULL;
			const char *pcStartName = NULL;

			pcCurName = REF_STRING_FROM_HANDLE(pCurPart->hMatDef);
			pcStartName = REF_STRING_FROM_HANDLE(pStartPart->hMatDef);
			if (!((!pcCurName || pcCurName == pcNone) && (!pcStartName || pcStartName == pcNone)) && GET_REF(pCurPart->hMatDef) != GET_REF(pStartPart->hMatDef)) {
				*changeDetected = true;
				if (pPrices) {
					cost += pPrices->iMaterial;
				}
			} else {
				pcCurName = REF_STRING_FROM_HANDLE(pCurPart->hPatternTexture);
				pcStartName = REF_STRING_FROM_HANDLE(pStartPart->hPatternTexture);
				if (!((!pcCurName || pcCurName == pcNone) && (!pcStartName || pcStartName == pcNone)) && pcCurName != pcStartName) {
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iPattern;
					}
				}
				pcCurName = REF_STRING_FROM_HANDLE(pCurPart->hDetailTexture);
				pcStartName = REF_STRING_FROM_HANDLE(pStartPart->hDetailTexture);
				if (!((!pcCurName || pcCurName == pcNone) && (!pcStartName || pcStartName == pcNone)) && pcCurName != pcStartName) {
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iDetail;
					}
				}
				pcCurName = REF_STRING_FROM_HANDLE(pCurPart->hSpecularTexture);
				pcStartName = REF_STRING_FROM_HANDLE(pStartPart->hSpecularTexture);
				if (!((!pcCurName || pcCurName == pcNone) && (!pcStartName || pcStartName == pcNone)) && pcCurName != pcStartName) {
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iSpecular;
					}
				}
				pcCurName = REF_STRING_FROM_HANDLE(pCurPart->hDiffuseTexture);
				pcStartName = REF_STRING_FROM_HANDLE(pStartPart->hDiffuseTexture);
				if (!((!pcCurName || pcCurName == pcNone) && (!pcStartName || pcStartName == pcNone)) && pcCurName != pcStartName) {
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iDiffuse;
					}
				}
				if (pCurPart->pMovableTexture) {
					pcCurName = REF_STRING_FROM_HANDLE(pCurPart->pMovableTexture->hMovableTexture);
				} else {
					pcCurName = NULL;
				}
				if (pStartPart->pMovableTexture) {
					pcStartName = REF_STRING_FROM_HANDLE(pStartPart->pMovableTexture->hMovableTexture);
				} else {
					pcStartName = NULL;
				}
				if (!((!pcCurName || pcCurName == pcNone) && (!pcStartName || pcStartName == pcNone)) && pcCurName != pcStartName) {
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iMovable;
					}
				}

				colorDif0 = pCurPart->color0[0] - pStartPart->color0[0];
				colorDif1 = pCurPart->color0[1] - pStartPart->color0[1];
				colorDif2 = pCurPart->color0[2] - pStartPart->color0[2];
				colorDif3 = pCurPart->color0[3] - pStartPart->color0[3];
				if(colorDif0 < -COSTUME_MAX_COLOR_DIF || colorDif0 > COSTUME_MAX_COLOR_DIF ||
					colorDif1< -COSTUME_MAX_COLOR_DIF || colorDif1 > COSTUME_MAX_COLOR_DIF ||
					colorDif2 < -COSTUME_MAX_COLOR_DIF || colorDif2 > COSTUME_MAX_COLOR_DIF ||
					colorDif3 < -COSTUME_MAX_COLOR_DIF || colorDif3 > COSTUME_MAX_COLOR_DIF)
				{
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iColor0;
					}
				}
				colorDif0 = pCurPart->color1[0] - pStartPart->color1[0];
				colorDif1 = pCurPart->color1[1] - pStartPart->color1[1];
				colorDif2 = pCurPart->color1[2] - pStartPart->color1[2];
				colorDif3 = pCurPart->color1[3] - pStartPart->color1[3];
				if(colorDif0 < -COSTUME_MAX_COLOR_DIF || colorDif0 > COSTUME_MAX_COLOR_DIF ||
					colorDif1< -COSTUME_MAX_COLOR_DIF || colorDif1 > COSTUME_MAX_COLOR_DIF ||
					colorDif2 < -COSTUME_MAX_COLOR_DIF || colorDif2 > COSTUME_MAX_COLOR_DIF ||
					colorDif3 < -COSTUME_MAX_COLOR_DIF || colorDif3 > COSTUME_MAX_COLOR_DIF)
				{
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iColor1;
					}
				}
				colorDif0 = pCurPart->color2[0] - pStartPart->color2[0];
				colorDif1 = pCurPart->color2[1] - pStartPart->color2[1];
				colorDif2 = pCurPart->color2[2] - pStartPart->color2[2];
				colorDif3 = pCurPart->color2[3] - pStartPart->color2[3];
				if(colorDif0 < -COSTUME_MAX_COLOR_DIF || colorDif0 > COSTUME_MAX_COLOR_DIF ||
					colorDif1< -COSTUME_MAX_COLOR_DIF || colorDif1 > COSTUME_MAX_COLOR_DIF ||
					colorDif2 < -COSTUME_MAX_COLOR_DIF || colorDif2 > COSTUME_MAX_COLOR_DIF ||
					colorDif3 < -COSTUME_MAX_COLOR_DIF || colorDif3 > COSTUME_MAX_COLOR_DIF)
				{
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iColor2;
					}
				}
				colorDif0 = pCurPart->color3[0] - pStartPart->color3[0];
				colorDif1 = pCurPart->color3[1] - pStartPart->color3[1];
				colorDif2 = pCurPart->color3[2] - pStartPart->color3[2];
				colorDif3 = pCurPart->color3[3] - pStartPart->color3[3];
				if(colorDif0 < -COSTUME_MAX_COLOR_DIF || colorDif0 > COSTUME_MAX_COLOR_DIF ||
					colorDif1< -COSTUME_MAX_COLOR_DIF || colorDif1 > COSTUME_MAX_COLOR_DIF ||
					colorDif2 < -COSTUME_MAX_COLOR_DIF || colorDif2 > COSTUME_MAX_COLOR_DIF ||
					colorDif3 < -COSTUME_MAX_COLOR_DIF || colorDif3 > COSTUME_MAX_COLOR_DIF)
				{
					*changeDetected = true;
					if (pPrices) {
						cost += pPrices->iColor3;
					}
				}
			}
		}
	}
	
	return cost;
}


// Gets the cost of changing from the first costume to the second
S32 costumeEntity_GetCostToChange(Entity *pEnt, PCCostumeStorageType eStorageType, NOCONST(PlayerCostume) *pSrcCostume, NOCONST(PlayerCostume) *pDstCostume, bool *pbCostumeChanged)
{
	int i, j, k;
	S32 cost;
	bool changeDetected = false;
	PCSkeletonDef *pSkeleton = GET_REF(pDstCostume->hSkeleton);
	CostumePrices *pPrices = NULL;

	if(pEnt && pEnt->pPlayer && (pEnt->pPlayer->InteractStatus.eNearbyContactTypes & ContactFlag_TailorFree) && !pbCostumeChanged) {
		return 0;
	}

	for(i=eaSize(&g_CostumeConfig.eaPrices)-1; i>=0; --i) {
		if (g_CostumeConfig.eaPrices[i]->eStorageType == eStorageType) {
			pPrices = g_CostumeConfig.eaPrices[i];
			break;
		}
	}
	if (!pPrices && !pbCostumeChanged) {
		return 0;
	}
	
	cost = 0;

	if (pSkeleton) {
		for (i = eaSize(&pSkeleton->eaRequiredBoneDefs)-1; i >= 0; i--) {
			PCBoneDef *pBone = GET_REF(pSkeleton->eaRequiredBoneDefs[i]->hBone);
			if (pBone) {
				cost += costumeEntity_GetCostToChangeBone(pPrices, pSrcCostume, pDstCostume, pBone, &changeDetected);
			}
		}
		for (i = eaSize(&pSkeleton->eaOptionalBoneDefs)-1; i >= 0; i--) {
			PCBoneDef *pBone = GET_REF(pSkeleton->eaOptionalBoneDefs[i]->hBone);
			if (pBone) {
				cost += costumeEntity_GetCostToChangeBone(pPrices, pSrcCostume, pDstCostume, pBone, &changeDetected);
			}
		}
		if (ea32Size(&pSrcCostume->eafBodyScales) == eaSize(&pSkeleton->eaBodyScaleInfo) && ea32Size(&pDstCostume->eafBodyScales) == eaSize(&pSkeleton->eaBodyScaleInfo))
		{
			for (i = eaSize(&pSkeleton->eaBodyScaleInfo)-1; i >= 0; i--) {
				if (pSrcCostume->eafBodyScales[i] != pDstCostume->eafBodyScales[i])
				{
					changeDetected = true;
					if (pPrices) {
						cost += pPrices->iBodyScale;
					}
				}
			}
		}
		if (pSrcCostume->fMuscle != pDstCostume->fMuscle)
		{
			changeDetected = true;
			if (pPrices) {
				cost += pPrices->iBodyScale;
			}
		}
		if (pSrcCostume->fHeight != pDstCostume->fHeight)
		{
			changeDetected = true;
			if (pPrices) {
				cost += pPrices->iBodyScale;
			}
		}
		for (i = eaSize(&pSkeleton->eaScaleInfoGroups)-1; i >= 0; i--) {
			PCScaleInfoGroup *scaleInfoGroup = pSkeleton->eaScaleInfoGroups[i];
			for (j = eaSize(&scaleInfoGroup->eaScaleInfo)-1; j >= 0; j--) {
				PCScaleInfo *scaleInfo = scaleInfoGroup->eaScaleInfo[j];
				NOCONST(PCScaleValue) *a = NULL, *b = NULL;
				for (k = eaSize(&pSrcCostume->eaScaleValues)-1; k >= 0; k--) {
					if (!stricmp(pSrcCostume->eaScaleValues[k]->pcScaleName,scaleInfo->pcName))
					{
						a = pSrcCostume->eaScaleValues[k];
						break;
					}
				}
				for (k = eaSize(&pDstCostume->eaScaleValues)-1; k >= 0; k--) {
					if (!stricmp(pDstCostume->eaScaleValues[k]->pcScaleName,scaleInfo->pcName))
					{
						b = pDstCostume->eaScaleValues[k];
						break;
					}
				}
				if ((a == NULL) != (b == NULL))
				{
					changeDetected = true;
					if (pPrices) {
						cost += pPrices->iBoneScale;
					}
				}
				else if (a && b && a->fValue != b->fValue)
				{
					changeDetected = true;
					if (pPrices) {
						cost += pPrices->iBoneScale;
					}
				}
			}
		}
		for (j = eaSize(&pSkeleton->eaScaleInfo)-1; j >= 0; j--) {
			PCScaleInfo *scaleInfo = pSkeleton->eaScaleInfo[j];
			NOCONST(PCScaleValue) *a = NULL, *b = NULL;
			for (k = eaSize(&pSrcCostume->eaScaleValues)-1; k >= 0; k--) {
				if (!stricmp(pSrcCostume->eaScaleValues[k]->pcScaleName,scaleInfo->pcName))
				{
					a = pSrcCostume->eaScaleValues[k];
					break;
				}
			}
			for (k = eaSize(&pDstCostume->eaScaleValues)-1; k >= 0; k--) {
				if (!stricmp(pDstCostume->eaScaleValues[k]->pcScaleName,scaleInfo->pcName))
				{
					b = pDstCostume->eaScaleValues[k];
					break;
				}
			}
			if ((a == NULL) != (b == NULL))
			{
				changeDetected = true;
				if (pPrices) {
					cost += pPrices->iBoneScale;
				}
			}
			else if (a && b && a->fValue != b->fValue)
			{
				changeDetected = true;
				if (pPrices) {
					cost += pPrices->iBoneScale;
				}
			}
		}
	}

	{
		F32 colorDif0, colorDif1, colorDif2, colorDif3;
		colorDif0 = pSrcCostume->skinColor[0] - pDstCostume->skinColor[0];
		colorDif1 = pSrcCostume->skinColor[1] - pDstCostume->skinColor[1];
		colorDif2 = pSrcCostume->skinColor[2] - pDstCostume->skinColor[2];
		colorDif3 = pSrcCostume->skinColor[3] - pDstCostume->skinColor[3];
		if(colorDif0 < -COSTUME_MAX_COLOR_DIF || colorDif0 > COSTUME_MAX_COLOR_DIF ||
			colorDif1< -COSTUME_MAX_COLOR_DIF || colorDif1 > COSTUME_MAX_COLOR_DIF ||
			colorDif2 < -COSTUME_MAX_COLOR_DIF || colorDif2 > COSTUME_MAX_COLOR_DIF ||
			colorDif3 < -COSTUME_MAX_COLOR_DIF || colorDif3 > COSTUME_MAX_COLOR_DIF)
		{
			changeDetected = true;
			if (pPrices) {
				cost += pPrices->iSkinColor;
			}
		}
	}

	if (changeDetected && pPrices) {
		cost += pPrices->iBase;
	}
	
	if (pEnt && pPrices) {
		S32 iLevel = entity_GetSavedExpLevel(pEnt);
		if (iLevel >= 1 && iLevel <= MAX_LEVELS) {
			cost *= pPrices->fLevelMultipliers[iLevel-1];
		}
	}

	// Reduced cost to change costume based on permission
	if(pEnt && cost > 0)
	{
		bool bFound;
		S32 iDiscount = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEnt), GAME_PERMISSION_TAILORDISCOUNT, false, &bFound);
		if(bFound && iDiscount > 0)
		{
			F32 fCost = ((F32)(100 - iDiscount))/100.0f;
			if(fCost < 0.0f)
			{
				fCost = 0.0f;
			}

			cost *= fCost;
		}
	}

	if (pbCostumeChanged) {
		*pbCostumeChanged = changeDetected;
	}
	
	return cost;
}


// --------------------------------------------------------------------------
// Expression Function Logic
// --------------------------------------------------------------------------


int costumeEntity_EvaluateExpr(Entity *pPlayerEnt, Entity *pEnt, Expression *pExpr)
{
	MultiVal mvResultVal;

	exprContextSetSelfPtr(s_pCostumeContext, pEnt);
	exprContextSetPartition(s_pCostumeContext, entGetPartitionIdx(pPlayerEnt));

	// If the entity is a player, add it to the context as "Player"
	if (entGetPlayer(pPlayerEnt)) {
		exprContextSetPointerVarPooled(s_pCostumeContext, g_PlayerVarName, pPlayerEnt, NULL, false, true);
	} else {
		exprContextSetPointerVarPooled(s_pCostumeContext, g_PlayerVarName, NULL, NULL, false, true);
	}

	exprEvaluate(pExpr, s_pCostumeContext, &mvResultVal);
	return MultiValGetInt(&mvResultVal, NULL);
}


ExprFuncTable* costumeEntity_CreateExprFuncTable()
{
	static ExprFuncTable* s_costumeFuncTable = NULL;
	if(!s_costumeFuncTable)
	{
		s_costumeFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_costumeFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_costumeFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_costumeFuncTable, "PTECharacter");
	}
	return s_costumeFuncTable;
}


AUTO_RUN;
void costumeEntity_InitSystem(void)
{
	s_pCostumeContext = exprContextCreate();
	exprContextSetAllowRuntimeSelfPtr(s_pCostumeContext);
	exprContextSetAllowRuntimePartition(s_pCostumeContext);
	exprContextSetFuncTable(s_pCostumeContext, costumeEntity_CreateExprFuncTable());
}


// --------------------------------------------------------------------------
// Costume Slot Logic
// --------------------------------------------------------------------------


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.Hclass, .Hallegiance, .Hsuballegiance");
PCSlotSet *costumeEntity_trh_GetSlotSet( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bIsPet )
{
	PCSlotSet *pDefault = NULL;
	CharacterClass *pClass;
	PCRegionType eRegionType = kPCRegionType_Ground;
	int i;

	// Figure out entity's region type if it is a puppet
	if (!bIsPet) {
		pClass = NONNULL(pEnt->pChar) ? GET_REF(pEnt->pChar->hClass) : NULL;
		if (pClass) {
			RegionRules *pRegion = getRegionRulesFromRegionType(WRT_Ground);
			if (pRegion && ea32Find(&pRegion->peCharClassTypes, pClass->eType) >= 0) {
				eRegionType = kPCRegionType_Ground;
			} else {
				pRegion = getRegionRulesFromRegionType(WRT_Space);
				if (pRegion && ea32Find(&pRegion->peCharClassTypes, pClass->eType) >= 0) {
					eRegionType = kPCRegionType_Space;
				}
			}
		}
	}

	for(i=eaSize(&g_CostumeSlotSets.eaSlotSets)-1; i>=0; --i) {
		PCSlotSet *pSet = g_CostumeSlotSets.eaSlotSets[i];
		const char *pcAllegiance = REF_STRING_FROM_HANDLE(pSet->hAllegiance);
		if (pSet->bIsDefault) {
			pDefault = pSet;
		}
		if (!GET_REF(pSet->hAllegiance) || (GET_REF(pSet->hAllegiance) == GET_REF(pEnt->hAllegiance)) ||
			(GET_REF(pSet->hAllegiance) == GET_REF(pEnt->hSubAllegiance))) {
			if (pSet->eEntityType == GLOBALTYPE_ENTITYPLAYER) {
				if (!bIsPet && (pSet->eRegionType == eRegionType)) {
					return pSet;
				}
			} else if (pSet->eEntityType == GLOBALTYPE_ENTITYSAVEDPET) {
				if (bIsPet && (pSet->eRegionType == eRegionType)) {
					return pSet;
				}
			} else {
				Errorf("Unexpected entity type on costumeEntity_GetSlotSet");
			}
		}
	}

	return pDefault;
}


PCSlotSet *costumeEntity_GetSlotSet( Entity *pEnt, bool bIsPet )
{
	return costumeEntity_trh_GetSlotSet( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), bIsPet );
}


const char *costumeEntity_GetSlotSetName( Entity *pEnt, bool bIsPet )
{
	PCSlotSet *pSet = costumeEntity_GetSlotSet(pEnt, bIsPet);
	if (pSet) {
		return pSet->pcName;
	}
	return NULL;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Pchar.Hclass, .Hallegiance, .Hsuballegiance");
PCSlotDef *costumeEntity_trh_GetSlotIndexDef( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iIndex, bool bIsPet  )
{
	PCSlotSet *pSlotSet = costumeEntity_trh_GetSlotSet(ATR_PASS_ARGS, pEnt, bIsPet);
	if (pSlotSet) {
		if ((iIndex >= 0) && (iIndex < eaSize(&pSlotSet->eaSlotDefs))) {
			return pSlotSet->eaSlotDefs[iIndex];
		} else if (pSlotSet->pExtraSlotDef) {
			return pSlotSet->pExtraSlotDef;
		}
	}
	return NULL;
}

PCSlotDef *costumeEntity_GetSlotIndexDef( Entity *pEnt, int iIndex, bool bIsPet )
{
	return costumeEntity_trh_GetSlotIndexDef(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), iIndex, bIsPet);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.Hclass, .Hallegiance, .Hsuballegiance");
PCSlotType *costumeEntity_trh_GetSlotType( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iIndex, bool bIsPet, int *piSlotID  )
{
	PCSlotSet *pSlotSet = costumeEntity_trh_GetSlotSet(ATR_PASS_ARGS, pEnt, bIsPet);
	if (pSlotSet) {
		if ((iIndex >= 0) && (iIndex < eaSize(&pSlotSet->eaSlotDefs))) {
			if (piSlotID) {
				*piSlotID = pSlotSet->eaSlotDefs[iIndex]->iSlotID;
			}
			return costumeLoad_GetSlotType(pSlotSet->eaSlotDefs[iIndex]->pcSlotType);
		} else if (pSlotSet->pExtraSlotDef) {
			if (piSlotID) {
				*piSlotID = pSlotSet->pExtraSlotDef->iSlotID;
			}
			return costumeLoad_GetSlotType(pSlotSet->pExtraSlotDef->pcSlotType);
		}
	}
	if (piSlotID) {
		*piSlotID = 0;
	}
	return NULL;
}

PCSlotType *costumeEntity_GetSlotType( Entity *pEnt, int iIndex, bool bIsPet, int *piSlotID )
{
	return costumeEntity_trh_GetSlotType(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), iIndex, bIsPet, piSlotID);
}

bool costumeEntity_IsCostumeSlotUnlocked(Entity *pPlayerEnt, Entity *pEnt, int iIndex)
{
	PCSlotDef *pSlotDef;

	if (!pPlayerEnt || !pPlayerEnt->pSaved) {
		return false;
	}

	if (!pEnt || !pEnt->pSaved) {
		return false;
	}
	if (iIndex >= 0 && iIndex < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots)) {
		pSlotDef = costumeEntity_GetSlotDef(pEnt, pEnt->pSaved->costumeData.eaCostumeSlots[iIndex]->iSlotID);
	} else {
		pSlotDef = costumeEntity_GetExtraSlotDef(pEnt);
	}
	if (!pSlotDef || !pSlotDef->pExprUnlock) {
		return true;
	}
	return costumeEntity_EvaluateExpr(pPlayerEnt, pEnt, pSlotDef->pExprUnlock);
}

bool costumeEntity_IsSlotUnlocked(Entity *pPlayerEnt, Entity *pEnt, PCSlotDef *pDef)
{
	if (!pPlayerEnt || !pPlayerEnt->pSaved || !pEnt || !pEnt->pSaved) {
		return false;
	}

	if (!pDef) {
		return true;
	}

	if (!pDef->pExprUnlock || costumeEntity_EvaluateExpr(pPlayerEnt, pEnt, pDef->pExprUnlock)) {
		return true;
	}
	return false;
}

bool costumeEntity_IsSlotHidden(Entity *pPlayerEnt, Entity *pEnt, PCSlotDef *pDef)
{
	if (!pPlayerEnt || !pPlayerEnt->pSaved || !pEnt || !pEnt->pSaved || !pDef) {
		return false;
	}

	if (!pDef->pExprUnhide || costumeEntity_EvaluateExpr(pPlayerEnt, pEnt, pDef->pExprUnhide)) {
		return false;
	}
	return true;
}

PCSlotDef *costumeEntity_GetSlotDef(Entity *pEnt, int iSlotID)
{
	PCSlotSet *pSlotSet;

	if (!pEnt || !pEnt->pSaved || !pEnt->pSaved->costumeData.pcSlotSet) {
		return NULL;
	}

	pSlotSet = costumeLoad_GetSlotSet(pEnt->pSaved->costumeData.pcSlotSet);
	if (pSlotSet) {
		int i;
		for(i=eaSize(&pSlotSet->eaSlotDefs)-1; i>=0; --i) {
			if (pSlotSet->eaSlotDefs[i]->iSlotID == iSlotID) {
				return pSlotSet->eaSlotDefs[i];
			}
		}
	}

	return NULL;
}

PCSlotDef *costumeEntity_GetExtraSlotDef(Entity *pEnt)
{
	PCSlotSet *pSlotSet;

	if (!pEnt || !pEnt->pSaved) {
		return 0;
	}

	pSlotSet = costumeLoad_GetSlotSet(pEnt->pSaved->costumeData.pcSlotSet);
	if (!pSlotSet || !pSlotSet->pExtraSlotDef) {
		return 0;
	}

	return pSlotSet->pExtraSlotDef;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEntOwner, ".Psaved.Ugamespecificfixupversion")
ATR_LOCKS(pEnt, ".Psaved.Ugamespecificfixupversion, .Psaved.Costumedata.Iactivecostume, .Psaved.Costumedata.Pcslotset, .Psaved.Costumedata.Islotsetversion, .Psaved.Costumedata.Eacostumeslots");
bool costumeEntity_trh_FixupCostumeSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEntOwner, ATH_ARG NOCONST(Entity)* pEnt, const char *pcSlotSet)
{
	NOCONST(PlayerCostumeSlot) **eaSlots = NULL;
	NOCONST(PlayerCostumeSlot) *pNewSlot;
	PCSlotSet *pSet;
	int i,j;
	bool bChangedActive = false;
	U32 uGameSpecificFixupVersion = NONNULL(pEnt->pSaved) ? pEnt->pSaved->uGameSpecificFixupVersion : 0;

	if (pEnt->myEntityType == GLOBALTYPE_ENTITYSAVEDPET && NONNULL(pEntOwner))
	{
		uGameSpecificFixupVersion = pEntOwner->pSaved->uGameSpecificFixupVersion;
	}

	// Don't perform this on entities with wrong fixup version
	if (uGameSpecificFixupVersion < (U32)gameSpecificFixup_Version()) {
		return false;
	}

	pSet = costumeLoad_GetSlotSet(pcSlotSet);
	if (!pSet || ISNULL(pEnt->pSaved)) {
		return false;
	}
	
	pEnt->pSaved->costumeData.pcSlotSet = allocAddString(pcSlotSet);
	pEnt->pSaved->costumeData.iSlotSetVersion = pSet->iSetVersion;

	// Match up slot defs
	for(i=0; i<eaSize(&pSet->eaSlotDefs); ++i) {
		PCSlotDef *pDef = pSet->eaSlotDefs[i];
		bool bFound = false;

		for(j=0; j<eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots); ++j) {
			NOCONST(PlayerCostumeSlot) *pSlot = pEnt->pSaved->costumeData.eaCostumeSlots[j];
			if (pSlot && (pSlot->iSlotID == pDef->iSlotID)) {
				pNewSlot = pSlot; 
				pNewSlot->pcSlotType = pDef->pcSlotType;
				pEnt->pSaved->costumeData.eaCostumeSlots[j] = NULL;
				eaPush(&eaSlots, pNewSlot);
				bFound = true;

				// Update active costume if it moves so no apparent change
				if (!bChangedActive && (j == pEnt->pSaved->costumeData.iActiveCostume)) {
					pEnt->pSaved->costumeData.iActiveCostume = eaSize(&eaSlots)-1;
					bChangedActive = true;
				}
				break;
			}
		}
		if (!bFound) {
			pNewSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
			pNewSlot->iSlotID = pDef->iSlotID;
			pNewSlot->pcSlotType = pDef->pcSlotType;
			eaPush(&eaSlots, pNewSlot);
		}
	}

	// Match with extra slot def
	{
		PCSlotDef *pDef = pSet->pExtraSlotDef;
		if (pDef) {
			for(j=0; j<eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots); ++j) {
				NOCONST(PlayerCostumeSlot) *pSlot = pEnt->pSaved->costumeData.eaCostumeSlots[j];
				if (pSlot) {
					pNewSlot = pSlot; 
					pNewSlot->pcSlotType = pDef->pcSlotType;
					pNewSlot->iSlotID = pDef->iSlotID;
					pEnt->pSaved->costumeData.eaCostumeSlots[j] = NULL;
					eaPush(&eaSlots, pNewSlot);
				}
			}
		}
	}

	// Any remaining slots don't match any existing and we don't have an extra
	
	// Put fixed up slots list into the costume data
	eaDestroyStructNoConst(&pEnt->pSaved->costumeData.eaCostumeSlots, parse_PlayerCostumeSlot);
	pEnt->pSaved->costumeData.eaCostumeSlots = eaSlots;

	if (!eaGet(&pEnt->pSaved->costumeData.eaCostumeSlots,pEnt->pSaved->costumeData.iActiveCostume) || 
		(!pEnt->pSaved->costumeData.eaCostumeSlots[pEnt->pSaved->costumeData.iActiveCostume]->pCostume))
	{
		//Find valid costume slot
		pEnt->pSaved->costumeData.iActiveCostume = 0;
		for(j=0; j<eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots); ++j)
		{
			if (pEnt->pSaved->costumeData.eaCostumeSlots[j]->pCostume)
			{
				pEnt->pSaved->costumeData.iActiveCostume = j;
				break;
			}
		}
	}

	return true;
}


// --------------------------------------------------------------------------
// Costume Unlock Logic
// --------------------------------------------------------------------------

#ifdef GAMECLIENT
void gclLogin_GetUnlockedCostumes(PlayerCostumeRef ***pppCostumes);
#else
void costumeServer_GetUnlockedCostumes(Entity *pPlayerEnt, PlayerCostumeRef ***pppCostumes, GameAccountData *pGameAccount)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer)
	{
		int i;
		//Unlock costumes that are for this game, of the costume type and have an item found with costumes on it
		if (pGameAccount)
		{
			for(i=0; i<eaSize(&pGameAccount->eaKeys);i++)
			{
				AttribValuePair *pPair = eaGet(&pGameAccount->eaKeys, i);
				if(pPair->pchValue && atoi(pPair->pchValue) > 0)
				{
					char *pchItem = estrStackCreateFromStr(pPair->pchAttribute);
					char *pchGameTitle = NULL;
					MicroItemType eType = kMicroItemType_None;
					char *pchItemName = NULL;

					if(!MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName))
					{
						estrDestroy(&pchItem);
						continue;
					}

					if(	pchGameTitle &&
						eType == kMicroItemType_Costume &&
						pchItemName &&
						!stricmp(GetShortProductName(), pchGameTitle) )
					{
						ItemDef *pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, pchItemName);
						if(pItemDef)
						{
							S32 iIdx;
							for(iIdx = eaSize(&pItemDef->ppCostumes)-1; iIdx >= 0; iIdx--)
							{
								const char *pchCostumeRefString = REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[iIdx]->hCostumeRef);
								if(pchCostumeRefString && pchCostumeRefString[0])
								{
									PlayerCostumeRef *pCostume = StructCreate(parse_PlayerCostumeRef);
									SET_HANDLE_FROM_STRING("PlayerCostume", pchCostumeRefString, pCostume->hCostume);
									eaPush(pppCostumes, pCostume);
								}
							}
						}
					}
					estrDestroy(&pchItem);
				}
			}

			//Unlock the costume keys
			for(i=0; i<eaSize(&pGameAccount->eaCostumeKeys);i++)
			{
				AttribValuePair *pPair = eaGet(&pGameAccount->eaCostumeKeys, i);
				if(pPair->pchValue)
				{
					char *pchItem = estrStackCreateFromStr(pPair->pchAttribute);
					char *pchGameTitle = NULL;
					MicroItemType eType = kMicroItemType_None;
					char *pchCostumeName = NULL;

					if(!MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchCostumeName))
					{
						estrDestroy(&pchItem);
						continue;
					}

					if(	pchGameTitle &&
						eType == kMicroItemType_PlayerCostume &&
						pchCostumeName &&
						!stricmp(GetShortProductName(), pchGameTitle) )
					{
						PlayerCostume *pCostume = (PlayerCostume*)RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeName);
						if(pCostume)
						{
							PlayerCostumeRef *pCostumeRef = StructCreate(parse_PlayerCostumeRef);
							SET_HANDLE_FROM_STRING("PlayerCostume", pCostume->pcName, pCostumeRef->hCostume);
							eaPush(pppCostumes, pCostumeRef);
						}
					}
					estrDestroy(&pchItem);
				}
			}
		}
	}
}
#endif

static void costumeEntity_GetUnlockCostumesExpr(Entity *pPlayerEnt, Entity *pEnt, PlayerCostume ***peaUnlockedCostumes)
{
	int iCostumeIdx;

	//Go through the costume sets and add costumes that the player/pet unlocked there
	if (pPlayerEnt && pEnt)
	{
		DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeSet");

		for(iCostumeIdx=eaSize(&deas->ppReferents)-1; iCostumeIdx>=0; --iCostumeIdx)
		{
			PCCostumeSet *cs = (PCCostumeSet*)deas->ppReferents[iCostumeIdx];
			if (cs->eCostumeType != kPCCostumeType_Player && cs->eCostumeType != kPCCostumeType_Overlay && cs->eCostumeType != kPCCostumeType_Item) continue;
			if (!(cs->eCostumeSetFlags & kPCCostumeSetFlags_Unlockable)) continue;
			if ((!cs->pExprUnlock) || costumeEntity_EvaluateExpr(pPlayerEnt, pEnt, cs->pExprUnlock))
			{
				int i;
				for (i = eaSize(&cs->eaPlayerCostumes)-1; i >= 0; --i)
				{
					PlayerCostume *pCostume = GET_REF(cs->eaPlayerCostumes[i]->hPlayerCostume);
					if (!pCostume) continue;
					eaPush(peaUnlockedCostumes, pCostume);
				}
			}
		}
	}
}


static void costumeEntity_GetUnlockCostumesRefExpr(Entity *pPlayerEnt, Entity *pEnt, PlayerCostumeRef ***peaUnlockedCostumeRefs)
{
	int iCostumeIdx;

	//Go through the costume sets and add costumes that the player/pet unlocked there
	if (pPlayerEnt && pEnt)
	{
		DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeSet");

		for(iCostumeIdx=eaSize(&deas->ppReferents)-1; iCostumeIdx>=0; --iCostumeIdx)
		{
			PCCostumeSet *cs = (PCCostumeSet*)deas->ppReferents[iCostumeIdx];
			if (cs->eCostumeType != kPCCostumeType_Player && cs->eCostumeType != kPCCostumeType_Overlay && cs->eCostumeType != kPCCostumeType_Item) continue;
			if (!(cs->eCostumeSetFlags & kPCCostumeSetFlags_Unlockable)) continue;
			if ((!cs->pExprUnlock) || costumeEntity_EvaluateExpr(pPlayerEnt, pEnt, cs->pExprUnlock))
			{
				int i;
				for (i = eaSize(&cs->eaPlayerCostumes)-1; i >= 0; --i)
				{
					PlayerCostumeRef *pcr = StructCreate(parse_PlayerCostumeRef);
					COPY_HANDLE(pcr->hCostume,cs->eaPlayerCostumes[i]->hPlayerCostume);
					eaPush(peaUnlockedCostumeRefs, pcr);
				}
			}
		}
	}
}


static bool costumeEntity_IsUnlockedCostumeRefExpr(Entity *pPlayerEnt, Entity *pEnt, const char *pchCostumeName)
{
	int iCostumeIdx;

	//Go through the costume sets and add costumes that the player/pet unlocked there
	if (pPlayerEnt && pEnt)
	{
		DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeSet");

		for(iCostumeIdx=eaSize(&deas->ppReferents)-1; iCostumeIdx>=0; --iCostumeIdx)
		{
			PCCostumeSet *cs = (PCCostumeSet*)deas->ppReferents[iCostumeIdx];
			if (cs->eCostumeType != kPCCostumeType_Player && cs->eCostumeType != kPCCostumeType_Overlay && cs->eCostumeType != kPCCostumeType_Item) continue;
			if (!(cs->eCostumeSetFlags & kPCCostumeSetFlags_Unlockable)) continue;
			if ((!cs->pExprUnlock) || costumeEntity_EvaluateExpr(pPlayerEnt, pEnt, cs->pExprUnlock))
			{
				int i;
				for (i = eaSize(&cs->eaPlayerCostumes)-1; i >= 0; --i)
				{
					return REF_STRING_FROM_HANDLE(cs->eaPlayerCostumes[i]->hPlayerCostume) == pchCostumeName;
				}
			}
		}
	}
	return false;
}


bool costumeEntity_IsUnlockedCostumeRef(CONST_EARRAY_OF(PlayerCostumeRef) ppCostumes, GameAccountData *pData, Entity *pPlayerEnt, Entity *pEnt, const char *pchCostumeName)
{
	int iCostumeIdx;
	PlayerCostumeRef **ppLoginCostumes = NULL;

	// Make sure it's a pooled string, because if it isn't there,
	// then it couldn't possibly be unlocked.
	pchCostumeName = pchCostumeName ? allocFindString(pchCostumeName) : NULL;
	if (!pchCostumeName)
		return false;

	//Get the costumes provided by the login server (Null except during character creation)
#ifdef GAMECLIENT
	gclLogin_GetUnlockedCostumes(&ppLoginCostumes);
#else
	costumeServer_GetUnlockedCostumes(pPlayerEnt, &ppLoginCostumes, pData);
#endif
	if(eaSize(&ppLoginCostumes))
	{
		for(iCostumeIdx = eaSize(&ppLoginCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
		{
			if (pchCostumeName == REF_STRING_FROM_HANDLE(ppLoginCostumes[iCostumeIdx]->hCostume))
			{
				break;
			}
		}
		eaDestroyStruct(&ppLoginCostumes, parse_PlayerCostumeRef);
		if (iCostumeIdx >= 0)
			return true;
	}

	if(pData)
	{
		char *pchItem = NULL;
		estrStackCreate(&pchItem);
		for(iCostumeIdx = eaSize(&pData->eaCostumeKeys)-1; iCostumeIdx >= 0; iCostumeIdx--)
		{
			char *pchGameTitle = NULL;
			MicroItemType eType = kMicroItemType_None;
			char *pchKeyCostumeName = NULL;
			PlayerCostumeRef *pRef = NULL;
			AttribValuePair *pPair = pData->eaCostumeKeys[iCostumeIdx];

			if(!pPair || !pPair->pchValue || atoi(pPair->pchValue) <= 0)
				continue;

			estrCopy2(&pchItem, pPair->pchAttribute);

			if(!MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchKeyCostumeName))
			{
				continue;
			}

			if(  !pchGameTitle ||
				!pchKeyCostumeName ||
				stricmp(GetShortProductName(), pchGameTitle))
			{
				continue;
			}

			if (0==stricmp(pchKeyCostumeName, pchCostumeName))
			{
				break;
			}
		}
		estrDestroy(&pchItem);
		if (iCostumeIdx >= 0)
			return true;
	}

	//Add in whatever costumes were passed in (usually entity saved data)
	for(iCostumeIdx = eaSize(&ppCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
	{
		if (REF_STRING_FROM_HANDLE(ppCostumes[iCostumeIdx]->hCostume) == pchCostumeName)
		{
			return true;
		}
	}

#if defined(GAMESERVER) || defined(APPSERVER)
	return costumeEntity_IsUnlockedCostumeRefExpr(pPlayerEnt, pEnt, pchCostumeName);
#endif
#ifdef GAMECLIENT
	//if(!GSM_IsStateActive(GCL_LOGIN))
	{
		return costumeEntity_IsUnlockedCostumeRefExpr(pPlayerEnt, pEnt, pchCostumeName);
	}
#endif
	return false;
}


void costumeEntity_GetUnlockCostumesRef(CONST_EARRAY_OF(PlayerCostumeRef) ppCostumes, GameAccountData *pData, Entity *pPlayerEnt, Entity *pEnt, PlayerCostumeRef ***peaUnlockedCostumeRefs)
{
	int iCostumeIdx;
	PlayerCostumeRef **ppLoginCostumes = NULL;

	// make sure unlocked costumes are clear as the refs are going to be cleared
	eaClearStruct(peaUnlockedCostumeRefs, parse_PlayerCostumeRef);

	//Get the costumes provided by the login server (Null except during character creation)
#ifdef GAMECLIENT
	gclLogin_GetUnlockedCostumes(&ppLoginCostumes);
#else
	costumeServer_GetUnlockedCostumes(pPlayerEnt, &ppLoginCostumes, pData);
#endif
	if(eaSize(&ppLoginCostumes))
	{
		for(iCostumeIdx = eaSize(&ppLoginCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
		{
			eaPush(peaUnlockedCostumeRefs, StructClone(parse_PlayerCostumeRef, eaGet(&ppLoginCostumes,iCostumeIdx)));
		}
		eaDestroyStruct(&ppLoginCostumes, parse_PlayerCostumeRef);
	}

	if(pData)
	{
		char *pchItem = NULL;
		estrStackCreate(&pchItem);
		for(iCostumeIdx = eaSize(&pData->eaCostumeKeys)-1; iCostumeIdx >= 0; iCostumeIdx--)
		{
			char *pchGameTitle = NULL;
			MicroItemType eType = kMicroItemType_None;
			char *pchCostumeName = NULL;
			PlayerCostumeRef *pRef = NULL;
			AttribValuePair *pPair = pData->eaCostumeKeys[iCostumeIdx];

			if(!pPair || !pPair->pchValue || atoi(pPair->pchValue) <= 0)
				continue;

			estrCopy2(&pchItem, pPair->pchAttribute);

			if(!MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchCostumeName))
			{
				continue;
			}

			if(  !pchGameTitle ||
				!pchCostumeName ||
				stricmp(GetShortProductName(), pchGameTitle))
			{
				continue;
			}
			pRef = StructCreate(parse_PlayerCostumeRef);
			SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pchCostumeName, pRef->hCostume);
			eaPush(peaUnlockedCostumeRefs, pRef);
		}
		estrDestroy(&pchItem);
	}

	//Add in whatever costumes were passed in (usually entity saved data)
	for(iCostumeIdx = eaSize(&ppCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
	{
		eaPush(peaUnlockedCostumeRefs, StructClone(parse_PlayerCostumeRef, eaGet(&ppCostumes,iCostumeIdx)));
	}

#if defined(GAMESERVER) || defined(APPSERVER)
	costumeEntity_GetUnlockCostumesRefExpr(pPlayerEnt, pEnt, peaUnlockedCostumeRefs);
#endif
#ifdef GAMECLIENT
	//if(!GSM_IsStateActive(GCL_LOGIN))
	{
		costumeEntity_GetUnlockCostumesRefExpr(pPlayerEnt, pEnt, peaUnlockedCostumeRefs);
	}
#endif
}


void costumeEntity_GetUnlockCostumes(CONST_EARRAY_OF(PlayerCostumeRef) ppCostumes, GameAccountData *pData, Entity *pPlayerEnt, Entity *pEnt, PlayerCostume ***peaUnlockedCostumes)
{
	int iCostumeIdx;
	PlayerCostumeRef **ppLoginCostumes = NULL;

	// make sure unlocked costumes are clear as the refs are going to be cleared
	eaClear(peaUnlockedCostumes);

	//Get the costumes provided by the login server (Null except during character creation)
#ifdef GAMECLIENT
	gclLogin_GetUnlockedCostumes(&ppLoginCostumes);
#else
	costumeServer_GetUnlockedCostumes(pPlayerEnt, &ppLoginCostumes, pData);
#endif
	if(eaSize(&ppLoginCostumes))
	{
		for(iCostumeIdx = eaSize(&ppLoginCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
		{
			PlayerCostume *pCostume = GET_REF(ppLoginCostumes[iCostumeIdx]->hCostume);
			if (!pCostume) continue;
			eaPush(peaUnlockedCostumes, pCostume);
		}
		eaDestroyStruct(&ppLoginCostumes, parse_PlayerCostumeRef);
	}

	if(pData)
	{
		char *pchItem = NULL;
		estrStackCreate(&pchItem);
		for(iCostumeIdx = eaSize(&pData->eaCostumeKeys)-1; iCostumeIdx >= 0; iCostumeIdx--)
		{
			char *pchGameTitle = NULL;
			MicroItemType eType = kMicroItemType_None;
			char *pchCostumeName = NULL;
			PlayerCostumeRef *pRef = NULL;
			AttribValuePair *pPair = pData->eaCostumeKeys[iCostumeIdx];
			PlayerCostume *pCostume;

			if(!pPair || !pPair->pchValue || atoi(pPair->pchValue) <= 0)
				continue;

			estrCopy2(&pchItem, pPair->pchAttribute);

			if(!MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchCostumeName))
			{
				continue;
			}

			if(  !pchGameTitle ||
				!pchCostumeName ||
				stricmp(GetShortProductName(), pchGameTitle))
			{
				continue;
			}
			pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchCostumeName);
			if (pCostume) eaPush(peaUnlockedCostumes, pCostume);
		}
		estrDestroy(&pchItem);
	}

	//Add in whatever costumes were passed in (usually entity saved data)
	for(iCostumeIdx = eaSize(&ppCostumes)-1; iCostumeIdx >= 0; iCostumeIdx--)
	{
		PlayerCostume *pCostume = GET_REF(ppCostumes[iCostumeIdx]->hCostume);
		if (!pCostume) continue;
		eaPush(peaUnlockedCostumes, pCostume);
	}

#if defined(GAMESERVER) || defined(APPSERVER)
	costumeEntity_GetUnlockCostumesExpr(pPlayerEnt, pEnt, peaUnlockedCostumes);
#endif
#ifdef GAMECLIENT
	//if(!GSM_IsStateActive(GCL_LOGIN))
	{
		costumeEntity_GetUnlockCostumesExpr(pPlayerEnt, pEnt, peaUnlockedCostumes);
	}
#endif
}

S32 costumeEntity_GetStoreCostumeEntities(Entity *pEnt, PCCostumeStorageType eStorageType, Entity ***peaStoreEntities)
{
	S32 i, iCharClassType = 0, iEntityCount = 0;
	CharClassTypes eSpaceClassType = CharClassTypes_None;
	Entity *pCurrentPuppet = NULL;
	bool bIncludeSelf = true;
	GlobalType eCurrentPuppetType = GLOBALTYPE_NONE;
	ContainerID uCurrentPuppetID = 0;
#ifdef GAMECLIENT
	Login2CharacterDetail *pLoginCharacterDetail = pEnt ? gclLogin2_CharacterDetailCache_Get(entGetContainerID(pEnt)) : NULL;
#endif

	if (peaStoreEntities)
		eaClear(peaStoreEntities);

	if (!pEnt || !pEnt->pSaved)
		return iEntityCount;

	eSpaceClassType = StaticDefineIntGetInt(CharClassTypesEnum, "Space");

	if (pEnt->pSaved->pPuppetMaster)
	{
		eCurrentPuppetType = pEnt->pSaved->pPuppetMaster->curType;
		uCurrentPuppetID = pEnt->pSaved->pPuppetMaster->curID;
	}

	for (i = 0; i < eaSize(&pEnt->pSaved->ppOwnedContainers); i++)
	{
		PetRelationship *pRelationship = pEnt->pSaved->ppOwnedContainers[i];
		PuppetEntity *pPuppetEnt = SavedPet_GetPuppetFromPet(pEnt, pRelationship);
		Entity *pSubEnt = GET_REF(pRelationship->hPetRef);
		bool bInclude = false;

#ifdef GAMECLIENT
		if (!IS_HANDLE_ACTIVE(pRelationship->hPetRef) && pLoginCharacterDetail)
		{
			// Grab entity from login detail
			S32 j;
			for (j = eaSize(&pLoginCharacterDetail->activePuppetEnts) - 1; j >= 0; j--)
			{
				if (entGetContainerID(pLoginCharacterDetail->activePuppetEnts[j]) == pRelationship->conID)
				{
					pSubEnt = pLoginCharacterDetail->activePuppetEnts[j];
					break;
				}
			}
		}
#endif

		// Remap entity to current entity if the owned container is indeed the current entity
		if (eCurrentPuppetType == GLOBALTYPE_ENTITYSAVEDPET && uCurrentPuppetID == pRelationship->conID)
			pSubEnt = pEnt;

		// If the entity is unavailable, ignore
		if (!pSubEnt)
			continue;

		switch (eStorageType)
		{
			xcase kPCCostumeStorageType_Primary:
			acase kPCCostumeStorageType_Secondary:
				// This could be the current entity or a Ground puppet in Star Trek
				if (eCurrentPuppetType != GLOBALTYPE_NONE)
				{
					if (pPuppetEnt && pPuppetEnt->eState == PUPPETSTATE_ACTIVE && GetCharacterClassEnum(pSubEnt) != eSpaceClassType)
						bInclude = true;
				}
				else if (pSubEnt == pEnt)
				{
					bInclude = true;
				}

			xcase kPCCostumeStorageType_Pet:
				// This would be a Bridge Officer entity in Star Trek
				if (!pPuppetEnt)
					bInclude = true;

			xcase kPCCostumeStorageType_SpacePet:
				// This would be a Space puppet in Star Trek
				if (pPuppetEnt && pPuppetEnt->eState == PUPPETSTATE_ACTIVE && GetCharacterClassEnum(pSubEnt) == eSpaceClassType)
					bInclude = true;

			xcase kPCCostumeStorageType_Nemesis:
				// This would be a nemesis entity in Champs
				if (!pPuppetEnt && pSubEnt->pNemesis)
					bInclude = true;
		}

		if (bInclude)
		{
			// Add entity to list
			if (peaStoreEntities)
				eaPush(peaStoreEntities, pSubEnt);
			iEntityCount++;

			// Don't include entity again
			if (pSubEnt == pEnt)
				bIncludeSelf = false;
		}
	}

	if (bIncludeSelf && (eStorageType == kPCCostumeStorageType_Primary || eStorageType == kPCCostumeStorageType_Secondary))
	{
		if (peaStoreEntities)
			eaPush(peaStoreEntities, pEnt);
		iEntityCount++;
	}

	return iEntityCount;
}

Entity *costumeEntity_GetStoreCostumeEntity(Entity *pEnt, PCCostumeStorageType eStorageType, U32 uStoreContainerID)
{
	S32 i, iCharClassType = 0, iEntityCount = 0;
	CharClassTypes eSpaceClassType = CharClassTypes_None;
	Entity *pCurrentPuppet = NULL;
	bool bIncludeSelf = true;
	GlobalType eCurrentPuppetType = GLOBALTYPE_NONE;
	ContainerID uCurrentPuppetID = 0;
#ifdef GAMECLIENT
	Login2CharacterDetail *pLoginCharacterDetail = pEnt ? gclLogin2_CharacterDetailCache_Get(entGetContainerID(pEnt)) : NULL;
#endif

	if (!pEnt)
		return NULL;

	if (!pEnt->pSaved)
	{
		if (eStorageType == kPCCostumeStorageType_Primary || eStorageType == kPCCostumeStorageType_Secondary)
			return pEnt;
		return NULL;
	}

	eSpaceClassType = StaticDefineIntGetInt(CharClassTypesEnum, "Space");

	if (pEnt->pSaved->pPuppetMaster)
	{
		eCurrentPuppetType = pEnt->pSaved->pPuppetMaster->curType;
		uCurrentPuppetID = pEnt->pSaved->pPuppetMaster->curID;
	}

	for (i = 0; i < eaSize(&pEnt->pSaved->ppOwnedContainers); i++)
	{
		PetRelationship *pRelationship = pEnt->pSaved->ppOwnedContainers[i];
		PuppetEntity *pPuppetEnt = SavedPet_GetPuppetFromPet(pEnt, pRelationship);
		Entity *pSubEnt = GET_REF(pRelationship->hPetRef);
		bool bInclude = false;

#ifdef GAMECLIENT
		if (!IS_HANDLE_ACTIVE(pRelationship->hPetRef) && pLoginCharacterDetail)
		{
			// Grab entity from login detail
			S32 j;
			for (j = eaSize(&pLoginCharacterDetail->activePuppetEnts) - 1; j >= 0; j--)
			{
				if (entGetContainerID(pLoginCharacterDetail->activePuppetEnts[j]) == pRelationship->conID)
				{
					pSubEnt = pLoginCharacterDetail->activePuppetEnts[j];
					break;
				}
			}
		}
#endif

		// Remap entity to current entity if the owned container is indeed the current entity
		if (eCurrentPuppetType == GLOBALTYPE_ENTITYSAVEDPET && uCurrentPuppetID == pRelationship->conID)
			pSubEnt = pEnt;

		// If the entity is unavailable, ignore
		if (!pSubEnt)
			continue;

		switch (eStorageType)
		{
			xcase kPCCostumeStorageType_Primary:
			acase kPCCostumeStorageType_Secondary:
				// This could be the current entity or a Ground puppet in Star Trek
				if (eCurrentPuppetType != GLOBALTYPE_NONE)
				{
					if (pPuppetEnt && pPuppetEnt->eState == PUPPETSTATE_ACTIVE && GetCharacterClassEnum(pSubEnt) != eSpaceClassType
						&& entGetContainerID(pSubEnt) == uStoreContainerID)
					{
						bInclude = true;
					}
				}
				else if (pSubEnt == pEnt && entGetContainerID(pSubEnt) == uStoreContainerID)
				{
					bInclude = true;
				}

			xcase kPCCostumeStorageType_Pet:
				// This would be a Bridge Officer entity in Star Trek
				if (!pPuppetEnt && entGetContainerID(pSubEnt) == uStoreContainerID)
					bInclude = true;

			xcase kPCCostumeStorageType_SpacePet:
				// This would be a Space puppet in Star Trek
				if (pPuppetEnt && pPuppetEnt->eState == PUPPETSTATE_ACTIVE && GetCharacterClassEnum(pSubEnt) == eSpaceClassType
					&& entGetContainerID(pSubEnt) == uStoreContainerID)
				{
					bInclude = true;
				}

			xcase kPCCostumeStorageType_Nemesis:
				// This would be a nemesis entity in Champs
				if (!pPuppetEnt && pSubEnt->pNemesis && entGetContainerID(pSubEnt) == uStoreContainerID)
					bInclude = true;
		}

		if (bInclude)
			return pSubEnt;
	}

	if (eStorageType == kPCCostumeStorageType_Primary || eStorageType == kPCCostumeStorageType_Secondary)
		return pEnt;

	return NULL;
}

bool costumeEntity_GetStoreCostumeSlot(Entity *pEnt, Entity *pStoreEnt, S32 iIndex, PCSlotDef **ppSlotDef, PlayerCostumeSlot **ppCostumeSlot)
{
	PCSlotSet *pSlotSet = NULL;
	PCSlotDef *pSlotDef = NULL;
	bool bValidIndex = true;
	PuppetEntity *pPuppetEnt = NULL;
	bool bPet;

	if (ppSlotDef)
		*ppSlotDef = NULL;
	if (ppCostumeSlot)
		*ppCostumeSlot = NULL;

	if (!pEnt || !pStoreEnt)
		return false;

	// Get the puppet info
	if (pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
	{
		S32 i;
		for (i = 0; i < eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets); i++)
		{
			if (pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curType == entGetType(pStoreEnt)
				&& pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID == entGetContainerID(pStoreEnt))
			{
				pPuppetEnt = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
				break;
			}
		}
	}

	// Infer the entity is a pet
	bPet = !pPuppetEnt && pEnt != pStoreEnt;

	// A space puppet or a pet entity may only have one costume
	if (bPet || (pPuppetEnt && GetCharacterClassEnum(pStoreEnt) == StaticDefineIntGetInt(CharClassTypesEnum, "Space")))
		bValidIndex = iIndex == 0;

	// The store entity is a pet if it's not the entity and it's not a puppet
	pSlotSet = costumeEntity_GetSlotSet(pStoreEnt, bPet);
	pSlotDef = costumeEntity_GetSlotIndexDef(pStoreEnt, iIndex, bPet);

	if (ppSlotDef)
		*ppSlotDef = pSlotDef;
	if (ppCostumeSlot)
		*ppCostumeSlot = eaGet(&pStoreEnt->pSaved->costumeData.eaCostumeSlots, iIndex);

	return bValidIndex && (pSlotSet == NULL || pSlotDef != NULL);
}

// --------------------------------------------------------------------------
// Entity Management Logic
// --------------------------------------------------------------------------


// This is registered with the dictionaries to be called if a costume changes
void costumeEntity_UpdateEntityCostumes(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	// If something is modified, removed, or added, need to update costumes on entities
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {
		gUpdateEntityCostumesOnNextTick = true;
		eaPush(&geaChangedCostumes, allocAddString(pRefData));
	}
}

// This is registered with the dictionarys to be called if an asset changes
void costumeEntity_UpdateEntityCostumeParts(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	// If something is modified, removed, or added, need to update costumes on entities
	if (eType == RESEVENT_RESOURCE_ADDED) {
		gUpdateEntityCostumesOnNextTick = true;
		gCostumeAssetsAdded = true;
	} else if (!isProductionMode() &&
			   ((eType == RESEVENT_RESOURCE_MODIFIED) ||
			    (eType == RESEVENT_RESOURCE_REMOVED))
			  )	{
		gUpdateEntityCostumesOnNextTick = true;
		gCostumeAssetsModified = true;
	}
}

void costumeEntity_ForceGlobalReload(void)
{
	gUpdateEntityCostumesOnNextTick = gCostumeAssetsModified = true;
}

void costumeEntity_TickCheckEntityCostumes(void)
{
	if (gUpdateEntityCostumesOnNextTick) {
		Entity* currEnt;
		EntityIterator* iter;
		int count = 0;

		gUpdateEntityCostumesOnNextTick = false;

		if (gCostumeAssetsModified) {
			// When assets are modified, regenerate everything

			PERFINFO_AUTO_START("regenerate all entity costumes", 1);
			coarseTimerAddInstance(NULL, "regenerate all entity costumes");

			// Iterate all entities
			iter = entGetIteratorAllTypesAllPartitions(0, ENTITYFLAG_IGNORE);
			while (currEnt = EntityIteratorGetNext(iter)) {
				costumeEntity_RegenerateCostume(currEnt);
			}
			EntityIteratorRelease(iter);
			
			coarseTimerStopInstance(NULL, "regenerate all entity costumes");
			PERFINFO_AUTO_STOP();
		} else if (gCostumeAssetsAdded) {
			// Regenerate all incomplete entity costumes

			PERFINFO_AUTO_START("regenerate incomplete entity costumes", 1);
			coarseTimerAddInstance(NULL, "regenerate incomplete entity costumes");

			// Iterate all entities
			iter = entGetIteratorAllTypesAllPartitions(0, ENTITYFLAG_IGNORE);
			while (currEnt = EntityIteratorGetNext(iter)) {
				WLCostume *pWLCostume = GET_REF(currEnt->hWLCostume);
				if (!pWLCostume || !pWLCostume->bComplete) {
					costumeEntity_RegenerateCostume(currEnt);				
				}
			}
			EntityIteratorRelease(iter);

			coarseTimerStopInstance(NULL, "regenerate incomplete entity costumes");
			PERFINFO_AUTO_STOP();
		} else if (eaSize(&geaChangedCostumes) > 0) {
			// Both critters and saved critters MIGHT have costume references

			PERFINFO_AUTO_START("regenerate changed costumes", 1);
			coarseTimerAddInstance(NULL, "regenerate changed costumes");

			iter = entGetIteratorAllTypesAllPartitions(0, ENTITYFLAG_IGNORE);
			while (currEnt = EntityIteratorGetNext(iter)) {
				const char *pcCostumeName = REF_STRING_FROM_HANDLE(currEnt->costumeRef.hReferencedCostume);
				int i;
				for(i=eaSize(&geaChangedCostumes)-1; i>=0; --i) {
					if (pcCostumeName && (stricmp(pcCostumeName, geaChangedCostumes[i]) == 0)) {
						costumeEntity_RegenerateCostume(currEnt);
						break;
					}
				}
			}
			EntityIteratorRelease(iter);

			coarseTimerStopInstance(NULL, "regenerate changed costumes");
			PERFINFO_AUTO_STOP();
		}

		gCostumeAssetsModified = false;
		gCostumeAssetsAdded = false;
		eaDestroy(&geaChangedCostumes);
	}

	if (gValidateCostumePartsNextTick) {
		gValidateCostumePartsNextTick = false;

		costumeLoad_ValidateAll();
	}
}


// --------------------------------------------------------------------------
// Transaction Helpers with general usefulness
// --------------------------------------------------------------------------

AUTO_TRANS_HELPER
ATR_LOCKS(pPCCostume, ".Eatexwords");
void costumeEntity_trh_SetTexWordsValue(ATR_ARGS, ATH_ARG NOCONST(PlayerCostume) *pPCCostume, const char *pcKey, const char *pcValue)
{
	NOCONST(PCTexWords) *pTexWords;
	int i;

	for(i=eaSize(&pPCCostume->eaTexWords)-1; i>=0; --i) {
		pTexWords = pPCCostume->eaTexWords[i];
		if (pTexWords->pcKey && (stricmp(pcKey, pTexWords->pcKey) == 0)) {
			StructFreeString(pTexWords->pcText);
			pTexWords->pcText = StructAllocString(pcValue);
			return;
		}
	}
	
	pTexWords = StructCreateNoConst(parse_PCTexWords);
	pTexWords->pcKey = allocAddString(pcKey);
	pTexWords->pcText = StructAllocString(pcValue);
	eaPush(&pPCCostume->eaTexWords, pTexWords);
}


// Gets the cost of changing from the first costume to the second
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Savedsubname, .Psaved.Savedname")
ATR_LOCKS(pCostume, ".Hskeleton, .Eatexwords");
void costumeEntity_trh_ApplyEntityInfoToCostume(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PlayerCostume) *pCostume)
{
	PCSkeletonDef *pSkel;

	if (ISNULL(pCostume) || ISNULL(pEnt) || ISNULL(pEnt->pSaved)) {
		return;
	}

	pSkel = GET_REF(pCostume->hSkeleton);
	if (pSkel) {
		if (pSkel->bCopyName) {
			costumeEntity_trh_SetTexWordsValue(ATR_PASS_ARGS, pCostume, "NameString", pEnt->pSaved->savedName);
		}
		if (pSkel->bCopySubName) {
			costumeEntity_trh_SetTexWordsValue(ATR_PASS_ARGS, pCostume, "SubNameString", pEnt->pSaved->savedSubName ? pEnt->pSaved->savedSubName : "");
		}
	}
}

#ifdef GAMESERVER
bool costumeEntity_ApplyCritterInfoToCostume(NOCONST(Entity) *pEnt, NOCONST(PlayerCostume) *pCostume, bool bMakeClone)
{
	NOCONST(PlayerCostume) *pCostumeTemp = NULL;
	PCSkeletonDef *pSkel;
	bool bResult = false;
	const char *message = NULL;

	if (ISNULL(pCostume) || ISNULL(pEnt)) {
		return false;
	}

	pSkel = GET_REF(pCostume->hSkeleton);
	if (pSkel) {
		if (pSkel->bCopyName) {
			message = TranslateMessageRef(pEnt->pCritter->hDisplayNameMsg);
			if (message && *message)
			{
				if (bMakeClone)
				{
					if (!pCostumeTemp)
					{
						pCostumeTemp = StructCloneNoConst(parse_PlayerCostume,pCostume);
						if (pCostumeTemp)
						{
							char buf[128];
							*buf = '\0';
							strcat(buf, "clone");
							strcat(buf, pCostume->pcName);
							pCostumeTemp->pcName = allocAddString(buf);
						}
					}
					if (pCostumeTemp)
					{
						costumeEntity_trh_SetTexWordsValue(ATR_EMPTY_ARGS, pCostumeTemp, "NameString", message);
						bResult = true;
					}
				}
				else
				{
					costumeEntity_trh_SetTexWordsValue(ATR_EMPTY_ARGS, pCostume, "NameString", message);
					bResult = true;
				}
			}
		}
		if (pSkel->bCopySubName) {
			message = TranslateMessageRef(pEnt->pCritter->hDisplaySubNameMsg);
			if (message && *message)
			{
				if (bMakeClone)
				{
					if (!pCostumeTemp)
					{
						pCostumeTemp = StructCloneNoConst(parse_PlayerCostume,pCostume);
						if (pCostumeTemp)
						{
							char buf[128];
							*buf = '\0';
							strcat(buf, "clone");
							strcat(buf, pCostume->pcName);
							pCostumeTemp->pcName = allocAddString(buf);
						}
					}
					if (pCostumeTemp)
					{
						costumeEntity_trh_SetTexWordsValue(ATR_EMPTY_ARGS, pCostumeTemp, "SubNameString", message);
						bResult = true;
					}
				}
				else
				{
					costumeEntity_trh_SetTexWordsValue(ATR_EMPTY_ARGS, pCostume, "SubNameString", message);
					bResult = true;
				}
			}
		}
	}

	if (pCostumeTemp)
	{
		costumeEntity_SetCostume((Entity*)pEnt, (PlayerCostume*)pCostumeTemp, false);
		StructDestroyNoConst(parse_PlayerCostume,pCostumeTemp);
	}
	else
	{
		costumeEntity_SetCostume((Entity*)pEnt, (PlayerCostume*)pCostume, false);
	}
	return bResult;
}
#endif

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.Hspecies, .Psaved.Costumedata.Pcslotset, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Hclass, pInventoryV2.ppLiteBags[], .Pchar.Ilevelexp")
ATR_LOCKS(pData, ".Eakeys, .Eatokens, .Eacostumekeys, .Idayssubscribed, .Blinkedaccount, .Bshadowaccount, .Eaaccountkeyvalues");
S32 costumeEntity_trh_GetNumCostumeSlots(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(GameAccountData) *pData)
{
	int iLevel;
	S32 iMaxSlots = 0;
	PCSlotSet *pSlotSet;

	// Get the base count from the slot set (if any)
	pSlotSet = costumeLoad_GetSlotSet(pEnt->pSaved->costumeData.pcSlotSet);
	if (pSlotSet) {
		iMaxSlots += eaSize(&pSlotSet->eaSlotDefs);
	}

	// Add the number you get per level
	if(powertable_Find("PrimaryCostumeSlots"))
	{
		GameAccountDataExtract *pExtract = entity_trh_CreateGameAccountDataExtract(pData);
		if(GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_COSTUME_EXTRA_BY_LEVEL, true))
		{
			iLevel = entity_trh_GetSavedExpLevelLimited(pEnt);
			iMaxSlots += entity_PowerTableLookupAtHelper(pEnt, "PrimaryCostumeSlots", iLevel-1);
		}
		else
		{
			// does not have game permission for extra slots by level
			// add one
			++iMaxSlots;
		}
		entity_trh_DestroyGameAccountDataExtract(&pExtract);
	}

	iMaxSlots = MAX(1, iMaxSlots);

	//Add the number of numerics you have
	iMaxSlots += inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pEnt, "PrimaryCostume");

	iMaxSlots += MAX(0, gad_trh_GetAttribInt(pData, MicroTrans_GetCostumeSlotsGADKey()));

	iMaxSlots += MAX(0, gad_trh_GetAccountValueInt(pData, MicroTrans_GetCostumeSlotsASKey()));

	//Return 1 or the max number of slots available.
	return(MAX(1, iMaxSlots));
}

bool costumeEntity_trh_CostumeUnlockedLocal(ATH_ARG NOCONST(Entity) *pEnt, PlayerCostume *pCostume)
{
	S32 idx;
	if(ISNULL(pEnt) || ISNULL(pEnt->pSaved) || ISNULL(pCostume))
		return(false);

	for (idx = eaSize(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs)-1; idx >= 0; idx--)
	{
		if (REF_STRING_FROM_HANDLE(pEnt->pSaved->costumeData.eaUnlockedCostumeRefs[idx]->hCostume) == pCostume->pcName)
		{
			break;
		}
	}

	return(idx >= 0);
}

int costumeEntity_GetFreeChangeTokens(Entity *pOwnerEnt, Entity *pEnt)
{
	int iTokens = 0;

	if(GetCharacterClassEnum(pEnt) == StaticDefineIntGetInt(CharClassTypesEnum, "Space"))
	{
		iTokens = inv_GetNumericItemValue(pOwnerEnt ? pOwnerEnt : pEnt, MicroTrans_GetFreeShipCostumeChangeKeyID());
	}
	else
	{
		iTokens = inv_GetNumericItemValue(pOwnerEnt ? pOwnerEnt : pEnt, MicroTrans_GetFreeCostumeChangeKeyID());
	}

	return iTokens;
}

int costumeEntity_GetFreeFlexChangeTokens(Entity *pEnt)
{
	return inv_GetNumericItemValue(pEnt, "FreeCostumeChangeFlex");
}

int costumeEntity_GetAccountChangeTokens(Entity *pEnt, const GameAccountData *pData)
{
	int iTokens = 0;

	if(GetCharacterClassEnum(pEnt) == StaticDefineIntGetInt(CharClassTypesEnum, "Space"))
	{
		iTokens = gad_GetAttribInt(pData, MicroTrans_GetFreeShipCostumeChangeGADKey());
	}
	else
	{
		iTokens = gad_GetAttribInt(pData, MicroTrans_GetFreeCostumeChangeGADKey());
	}

	return iTokens;
}

// This assumes it is not a pet or puppet !!!
int costumeEntity_CanChangeForFree(Entity *pEnt)
{
	const GameAccountData *pData = entity_GetGameAccount(pEnt);

	return costumeEntity_GetFreeChangeTokens(NULL, pEnt) 
		+ costumeEntity_GetFreeFlexChangeTokens(pEnt) 
		+ costumeEntity_GetAccountChangeTokens(pEnt, pData);
}

// Check to see if this costume index is free to change
bool costumeEntity_TestCostumeForFreeChange(Entity *pPlayerEnt, Entity *pEnt, S32 iSlot)
{
	if(g_CostumeConfig.bInvalidCostumesAreFreeToChange && pEnt && pEnt->pSaved && iSlot >= 0 && iSlot < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots))	
	{
		NOCONST(PlayerCostume) *pCostume = CONTAINER_NOCONST(PlayerCostume, pEnt->pSaved->costumeData.eaCostumeSlots[iSlot]->pCostume);
		if(pCostume)
		{
			PCSlotType *pSlotType = costumeEntity_GetSlotType(pEnt, iSlot, (pPlayerEnt != pEnt), NULL);
			SpeciesDef *pSpecies = GET_REF(pCostume->hSpecies);
			// If the costume is not valid mark it as so.
			if(!costumeValidate_ValidatePlayerCreated((PlayerCostume *)pCostume, pSpecies, pSlotType, pPlayerEnt, pEnt, NULL, NULL, NULL, false))
			{
				return true;
			}
		}
	}
	
	return false;
}
