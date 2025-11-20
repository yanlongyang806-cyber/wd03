#include "gslCombatAdvantage.h"
#include "Character.h"
#include "Character_combat.h"
#include "Character_target.h"
#include "CharacterAttribs.h"
#include "combatAdvantage.h"
#include "CombatConfig.h"
#include "Entity.h"
#include "MemoryPool.h"

#define COMBAT_ADVANTAGE_QUERY_DIST_ADD	(6.f)

#define COMBAT_ADVANTAGE_ENTITY_IGNORE_FLAGS (ENTITYFLAG_PROJECTILE | ENTITYFLAG_DEAD | ENTITYFLAG_DESTROY | ENTITYFLAG_DONOTDRAW | ENTITYFLAG_DONOTSEND | ENTITYFLAG_IGNORE | ENTITYFLAG_UNSELECTABLE | ENTITYFLAG_UNTARGETABLE)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););




// -----------------------------------------------------------------------------------------------------------------------
static __forceinline void AddAdvantagedCharacter(Character *pChar, EntityRef erAdvantagedEnt, U32 uApplyID)
{
	CombatAdvantageNode *pNode;

	pNode = malloc(sizeof(CombatAdvantageNode));
	pNode->erEntity = erAdvantagedEnt;
	pNode->uApplyID = uApplyID;
	eaPush(&pChar->ppCombatAdvantages, pNode);

	entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
}

// -----------------------------------------------------------------------------------------------------------------------
// Gives the erAdvantagedEnt combat advantage against the given pChar. 
// Will check to make sure we are not re-adding the same advantage
void gslCombatAdvantage_AddAdvantagedCharacter(Character *pChar, EntityRef erAdvantagedEnt, U32 uApplyID)
{
	FOR_EACH_IN_EARRAY(pChar->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if (pNode->erEntity == erAdvantagedEnt && pNode->uApplyID == uApplyID)
			return;	 // already has this entity
	}
	FOR_EACH_END

	AddAdvantagedCharacter(pChar, erAdvantagedEnt, uApplyID);
}

// -----------------------------------------------------------------------------------------------------------------------
void gslCombatAdvantage_AddAdvantageToEveryone(Character *pChar, U32 uApplyID)
{
	gslCombatAdvantage_AddAdvantagedCharacter(pChar, COMBAT_ADVANTAGE_TO_EVERYONE, uApplyID);
}

// -----------------------------------------------------------------------------------------------------------------------
void gslCombatAdvantage_AddDisadvantageToEveryone(Character *pChar, U32 uApplyID)
{
	gslCombatAdvantage_AddAdvantagedCharacter(pChar, COMBAT_DISADVANTAGE, uApplyID);
}

// -----------------------------------------------------------------------------------------------------------------------
void gslCombatAdvantage_ClearPowersAppliedAdvantages(Character *pChar)
{
	bool bRemoved = false;
	FOR_EACH_IN_EARRAY(pChar->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if (pNode->uApplyID != COMBAT_ADVANTAGE_SYSTEM_APPLYID)
		{
			eaRemoveFast(&pChar->ppCombatAdvantages, FOR_EACH_IDX(-,pNode));
			free(pNode);
			bRemoved = true;
		}
	}
	FOR_EACH_END

	if (bRemoved)
	{
		entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
	}
}

// -----------------------------------------------------------------------------------------------------------------------
static void gslCombatAdvantage_ClearSystemAppliedAdvantages(Character *pChar)
{
	bool bRemoved = false;
	FOR_EACH_IN_EARRAY(pChar->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if (pNode->uApplyID == COMBAT_ADVANTAGE_SYSTEM_APPLYID)
		{
			eaRemoveFast(&pChar->ppCombatAdvantages, FOR_EACH_IDX(-,pNode));
			free(pNode);
			bRemoved = true;
		}
	}
	FOR_EACH_END

	if (bRemoved)
	{
		entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
	}
}

// -----------------------------------------------------------------------------------------------------------------------
static __forceinline void RemoveAdvantage(Character *pChar, EntityRef erAdvantagedEnt, U32 uApplyID) 
{
	FOR_EACH_IN_EARRAY(pChar->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if (pNode->erEntity == erAdvantagedEnt && pNode->uApplyID == uApplyID)
		{
			eaRemoveFast(&pChar->ppCombatAdvantages, FOR_EACH_IDX(-,pNode));
			free(pNode);
			entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
			return;
		}
	}
	FOR_EACH_END
}

// -----------------------------------------------------------------------------------------------------------------------
void gslCombatAdvantage_RemoveAdvantagedCharacter(Character *pChar, EntityRef erAdvantagedEnt, U32 uApplyID)
{
	RemoveAdvantage(pChar, erAdvantagedEnt, uApplyID);
}

// -----------------------------------------------------------------------------------------------------------------------
void gslCombatAdvantage_RemoveAdvantageToEveryone(Character *pChar, U32 uApplyID)
{
	RemoveAdvantage(pChar, COMBAT_ADVANTAGE_TO_EVERYONE, uApplyID);
}

// -----------------------------------------------------------------------------------------------------------------------
void gslCombatAdvantage_RemoveDisadvantageToEveryone(Character *pChar, U32 uApplyID)
{
	RemoveAdvantage(pChar, COMBAT_DISADVANTAGE, uApplyID);
}


// -----------------------------------------------------------------------------------------------------------------------
bool gslCombatAdvantage_HasFlankingImmunity(Character *pChar, EntityRef erTarget)
{
	FOR_EACH_IN_EARRAY(pChar->modArray.ppMods, AttribMod, pMod)
	{
		AttribModDef *pModDef = mod_GetDef(pMod);

		if (pModDef->offAspect == kAttribAspect_Immunity && 
			pModDef->offAttrib == kAttribType_CombatAdvantage && 
			(!pModDef->bPersonal || pMod->erPersonal == erTarget))
		{
			return true;
		}
	}
	FOR_EACH_END

	return false;
}

// -----------------------------------------------------------------------------------------------------------------------
static __forceinline bool gslCombatAdvantage_CanEntityFlank(int iPartitionIdx, Entity *pEnt, Entity *pFlankingEnt)
{
	if (pEnt == pFlankingEnt)
		return false;
	if (!pFlankingEnt->pChar || pFlankingEnt->pChar->pNearDeath)
		return false;
	
	if (!character_TargetIsFoe(iPartitionIdx, pEnt->pChar, pFlankingEnt->pChar))
		return false;

	if (entGetDistance(pEnt, NULL, pFlankingEnt, NULL, NULL) > g_CombatConfig.pCombatAdvantage->fFlankingDistance)
		return false;

	return true;
}

// -----------------------------------------------------------------------------------------------------------------------
void gslCombatAdvantage_CalculateFlankingForEntity(int iPartitionIdx, Entity *pEnt)
{
	Vec3 vEntPos;
	CombatAdvantageConfig *pCombatAdvantageConfig = g_CombatConfig.pCombatAdvantage;
	static Entity** s_eaProxEnts = NULL;
	static Entity** s_eaFlankingEnts = NULL;
	static EntityRef *s_eaPrevFlankingEnts = NULL;
	S32 iNumProxEnts;

	if (!pCombatAdvantageConfig)
		return;

	PERFINFO_AUTO_START_FUNC();

	entGetPos(pEnt, vEntPos);
		
	eaClear(&s_eaProxEnts);

	entGridProximityLookupExEArray(	iPartitionIdx, 
									vEntPos, 
									&s_eaProxEnts, 
									(pCombatAdvantageConfig->fFlankingDistance + COMBAT_ADVANTAGE_QUERY_DIST_ADD),
									0, COMBAT_ADVANTAGE_ENTITY_IGNORE_FLAGS, 
									pEnt);

	iNumProxEnts = eaSize(&s_eaProxEnts);
	if (iNumProxEnts > 1)
	{
		S32 i, x;
		

		// cull out any entities that should not be considered
		for (i = iNumProxEnts - 1; i >= 0; --i)
		{
			Entity *pOtherEnt = s_eaProxEnts[i];

			if (!gslCombatAdvantage_CanEntityFlank(iPartitionIdx, pEnt, pOtherEnt))
			{
				eaRemoveFast(&s_eaProxEnts, i);
			}
		}
		
		// go through the remaining ents and see if any are in flanking position
		iNumProxEnts = eaSize(&s_eaProxEnts);

		if (iNumProxEnts <= 1)
		{
			gslCombatAdvantage_ClearSystemAppliedAdvantages(pEnt->pChar);
			PERFINFO_AUTO_STOP();
			return;
		}

		// save the previous flanking entities so we can remove the ones that are no longer flanking
		{
			eaiClear(&s_eaPrevFlankingEnts);

			FOR_EACH_IN_EARRAY(pEnt->pChar->ppCombatAdvantages, CombatAdvantageNode, pNode)
			{
				if (pNode->uApplyID == COMBAT_ADVANTAGE_SYSTEM_APPLYID)
				{
					eaiPush(&s_eaPrevFlankingEnts, pNode->erEntity);
				}
			}
			FOR_EACH_END
		}
		
		eaClear(&s_eaFlankingEnts);

		for (i = iNumProxEnts - 1; i > 0; --i)
		{
			Vec3 vOuterEntPos, vEntToOuter;
			F32 fEntToOuterLenSQR;
			Entity *pEntOuterEnt = s_eaProxEnts[i];
						
			entGetPos(pEntOuterEnt, vOuterEntPos);
			subVec3(vOuterEntPos, vEntPos, vEntToOuter);
			fEntToOuterLenSQR = lengthVec3Squared(vEntToOuter);
			if (fEntToOuterLenSQR == 0.f)
				continue;

			for (x = i - 1; x >= 0; --x)
			{
				Vec3 vInnerEntPos, vInnerToEnt;
				Entity *pEntInnerEnt = s_eaProxEnts[x];
				F32 fDot, fCosAngleSQR, fInnerToEntLenSQR;
			
				entGetPos(pEntInnerEnt, vInnerEntPos);
				subVec3(vEntPos, vInnerEntPos, vInnerToEnt);
				fInnerToEntLenSQR = lengthVec3Squared(vInnerToEnt);
				if (fInnerToEntLenSQR == 0.f)
					continue;

				fDot = dotVec3(vInnerToEnt, vEntToOuter);
				fCosAngleSQR = SQR(fDot) / (fEntToOuterLenSQR * fInnerToEntLenSQR);

				if (fDot > 0 && fCosAngleSQR >= pCombatAdvantageConfig->fFlankingDotProductTolerance)
				{	
					eaPushUnique(&s_eaFlankingEnts, pEntInnerEnt);
					eaPushUnique(&s_eaFlankingEnts, pEntOuterEnt);
				}
			}
		}


		{
			S32 numPrevFlanking = eaiSize(&s_eaPrevFlankingEnts);

			// go through the list of entities that were determined to be flanking this entity and add advantage
			// also, remove entities found on the s_eaPrevFlankingEnts, 
			// so any entities left on the s_eaPrevFlankingEnts can safely be removed from the flanking advantage

			FOR_EACH_IN_EARRAY(s_eaFlankingEnts, Entity, pFlankingEnt)
			{
				EntityRef erFlankingEnt = entGetRef(pFlankingEnt);
				bool bFound = false;
				for (i = numPrevFlanking - 1; i >= 0; --i)
				{
					if (erFlankingEnt == s_eaPrevFlankingEnts[i])
					{
						eaiRemoveFast(&s_eaPrevFlankingEnts, i);
						numPrevFlanking--;
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					AddAdvantagedCharacter(pEnt->pChar, erFlankingEnt, COMBAT_ADVANTAGE_SYSTEM_APPLYID);
				}
			}
			FOR_EACH_END
			
			// remove any advantages that were on the s_eaPrevFlankingEnts list but not on the s_eaFlankingEnts list
			for (i = numPrevFlanking -1; i >= 0; --i)
			{
				RemoveAdvantage(pEnt->pChar, s_eaPrevFlankingEnts[i], COMBAT_ADVANTAGE_SYSTEM_APPLYID);
			}
		}
	}
	else
	{
		gslCombatAdvantage_ClearSystemAppliedAdvantages(pEnt->pChar);
	}

	PERFINFO_AUTO_STOP();
}

// -----------------------------------------------------------------------------------------------------------------------
F32 gslCombatAdvantage_GetStrengthBonus(int iPartitionIdx, Character *pChar, EntityRef erTarget, AttribType offAttrib)
{
	F32 fStrBonus = 0.f;

	// see if we need to apply combat advantage to this attribMod
	if (pChar && erTarget && ATTRIB_DAMAGE(offAttrib) &&
		entGetRef(pChar->pEntParent) != erTarget && 
		g_CombatConfig.pCombatAdvantage)
	{	
		Entity *pEntTarget = entFromEntityRef(iPartitionIdx, erTarget);
		if (pEntTarget && pEntTarget->pChar)
		{
			Character *pFlankingRelativeChar = pChar; 

			// check if this is a special summoned entity, if so use its creator as the flanking relative entity
			if (pChar->uiPowersCreatedEntityTime && 
				(ENTITYFLAG_UNSELECTABLE|ENTITYFLAG_UNTARGETABLE) == entCheckFlag(pChar->pEntParent, (ENTITYFLAG_UNSELECTABLE|ENTITYFLAG_UNTARGETABLE)) && 
				pChar->pEntParent->erCreator)
			{
				Entity *pEntParent = entFromEntityRef(iPartitionIdx, pChar->pEntParent->erCreator);
				if (pEntParent && pEntParent->pChar)
					pFlankingRelativeChar = pEntParent->pChar;
			}

			if (CombatAdvantage_HasAdvantageOnCharacter(pFlankingRelativeChar, pEntTarget->pChar) && 
				!gslCombatAdvantage_HasFlankingImmunity(pEntTarget->pChar, entGetRef(pFlankingRelativeChar->pEntParent)))
			{
				// calculate strengths and resistances 
				F32 fCAResistTrue = 1.f, fCAResist = 1.f, fCAImmune = 0.f, fCABonus = 0.f; 
				F32 fCAStrBonusMag = g_CombatConfig.pCombatAdvantage->fStrengthBonusMag;

				if (pEntTarget->pChar)
				{
					F32 fCAStr = character_GetStrengthGeneric(iPartitionIdx, pFlankingRelativeChar, kAttribType_CombatAdvantage, NULL);
					fCAStrBonusMag *= fCAStr;
				}

				fCAResist = character_GetResistGeneric(iPartitionIdx, pEntTarget->pChar, kAttribType_CombatAdvantage, &fCAResistTrue, NULL);

				fCAStrBonusMag = ModCalculateEffectiveMagnitude(fCAStrBonusMag, fCAResistTrue, fCAResist, 0.f, 0.f, 0.f);
				if (fCAStrBonusMag > 0.f)
				{
					// has combat advantage vs this target, 
					fStrBonus = fCAStrBonusMag;
				}
			}
		}
	}

	return fStrBonus;
}
