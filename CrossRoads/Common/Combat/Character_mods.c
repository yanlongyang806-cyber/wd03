/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Character_mods.h"

#include "Entity.h"
#if GAMESERVER
	#include "aiStruct.h"
	#include "EntityMovementManager.h"
#endif

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
#endif

#include "AttribCurveImp.h"
#include "AttribModFragility.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterAttribsMinimal_h_ast.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "DamageTracker.h"
#include "ItemArt.h"
#include "Player.h"
#include "PowerAnimFX.h"
#include "PowerModes.h"
#include "Powers.h"
#include "PowerVars.h"

#include "qsortG.h"

#include "AutoGen/Powers_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void CharacterCompileAttribPools(SA_PARAM_NN_VALID Character *pchar,
										SA_PARAM_NN_VALID CharacterAttribs *pattrBasicAbs,
										SA_PARAM_NN_VALID CharacterAttribs *pattrBasicFactPos,
										SA_PARAM_NN_VALID CharacterAttribs *pattrBasicFactNeg,
										SA_PARAM_NN_VALID CharacterAttribs *pattrClass)
{
	if(g_iAttribPoolCount)
	{
		int i,j,s;
		
		PERFINFO_AUTO_START_FUNC();

		for(i=0; i<g_iAttribPoolCount; i++)
		{
			AttribPool *ppool = g_AttribPools.ppPools[i];
			F32 fMaxOld = 0, fMinOld = 0, fMax = 0, fMin = 0;
			S32 bBound = false;
			F32 *pfMax = NULL, *pfMin = NULL;
			F32 *pfCur = F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribCur);

			// Find the max
			if(ppool->eAttribMax)
			{
				AttribType eAttrib = ppool->eAttribMax;
				F32 fBasicAbs = *F32PTR_OF_ATTRIB(pattrBasicAbs,eAttrib);
				F32 fBasicFactPos = *F32PTR_OF_ATTRIB(pattrBasicFactPos,eAttrib);
				F32 fBasicFactNeg = *F32PTR_OF_ATTRIB(pattrBasicFactNeg,eAttrib);
				F32 fClass = *F32PTR_OF_ATTRIB(pattrClass,eAttrib);
				fMax = fBasicAbs + fClass * (1.f + fBasicFactPos) / (1.f + fBasicFactNeg);
				pfMax = F32PTR_OF_ATTRIB(pchar->pattrBasic,eAttrib);
				if(fMax != *pfMax)
				{
					fMaxOld = *pfMax;
					bBound = true;
				}
				*pfMax = fMax;
			}

			// Find the min
			if(ppool->eAttribMin)
			{
				AttribType eAttrib = ppool->eAttribMax;
				F32 fBasicAbs = *F32PTR_OF_ATTRIB(pattrBasicAbs,eAttrib);
				F32 fBasicFactPos = *F32PTR_OF_ATTRIB(pattrBasicFactPos,eAttrib);
				F32 fBasicFactNeg = *F32PTR_OF_ATTRIB(pattrBasicFactNeg,eAttrib);
				F32 fClass = *F32PTR_OF_ATTRIB(pattrClass,eAttrib);
				fMin = fBasicAbs + fClass * (1.f + fBasicFactPos) / (1.f + fBasicFactNeg);
				pfMin = F32PTR_OF_ATTRIB(pchar->pattrBasic,eAttrib);
				if(fMin != *pfMin)
				{
					fMinOld = *pfMin;
					bBound = true;
				}
				*pfMin = fMin;
			}

			// If the min or max have changed, apply the bound
			if(bBound)
			{
				F32 fNew = combatpool_Bound(&ppool->combatPool,*pfCur,fMinOld,fMin,fMaxOld,fMax);
				*pfCur = fNew;
			}

			// Apply direct modifiers to the pool
			{
				AttribType eAttrib = ppool->eAttribCur;
				F32 fBasicAbs = *F32PTR_OF_ATTRIB(pattrBasicAbs,eAttrib);
				*pfCur += fBasicAbs;
				
				if(fMax)
				{
					F32 fBasicFactPos = *F32PTR_OF_ATTRIB(pattrBasicFactPos,eAttrib);
					F32 fBasicFactNeg = *F32PTR_OF_ATTRIB(pattrBasicFactNeg,eAttrib);
					*pfCur += fMax * (fBasicFactPos - fBasicFactNeg);
				}
			}

			// Apply damage attributes (positive values result in negative changes to the current value)
			s=eaiSize(&ppool->peAttribDamage);
			for(j=0; j<s; j++)
			{
				AttribType eAttrib = ppool->peAttribDamage[j];
				F32 fBasicAbs = *F32PTR_OF_ATTRIB(pattrBasicAbs,eAttrib);
				*pfCur -= fBasicAbs;

				if(fMax)
				{
					F32 fBasicFactPos = *F32PTR_OF_ATTRIB(pattrBasicFactPos,eAttrib);
					F32 fBasicFactNeg = *F32PTR_OF_ATTRIB(pattrBasicFactNeg,eAttrib);
					*pfCur -= fMax * (fBasicFactPos - fBasicFactNeg);
				}
			}

			// Apply heal attributes (positive values result in positive changes to the current value)
			s=eaiSize(&ppool->peAttribHeal);
			for(j=0; j<s; j++)
			{
				AttribType eAttrib = ppool->peAttribHeal[j];
				F32 fBasicAbs = *F32PTR_OF_ATTRIB(pattrBasicAbs,eAttrib);
				*pfCur += fBasicAbs;

				if(fMax)
				{
					F32 fBasicFactPos = *F32PTR_OF_ATTRIB(pattrBasicFactPos,eAttrib);
					F32 fBasicFactNeg = *F32PTR_OF_ATTRIB(pattrBasicFactNeg,eAttrib);
					*pfCur += fMax * (fBasicFactPos - fBasicFactNeg);
				}
			}

			// Absorb basic damage if possible.  This does not absorb factors, and stops if
			//  the pool's current value reaches 0.  It directly removes any absorbed damage
			//  from the basic accrual.
			if(ppool->bAbsorbsBasicDamage && *pfCur > 0)
			{
				F32 *pfDamage = (F32*)pattrBasicAbs;
				for(j=0; j<DAMAGETYPECOUNT; j++, pfDamage++)
				{
					if(*pfDamage > 0)
					{
						F32 fDamage = MIN(*pfDamage,*pfCur);
						*pfDamage -= fDamage;
						*pfCur -= fDamage;
						if(*pfCur <= 0)
							break;
					}
				}
			}

			// If the target attrib is not calculated on a per tick basis, treat it 
			// like a current value and calculate its new value out here
			if(ppool->bTargetNotCalculated)
			{
				F32 *pfTarget = F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribTarget);;
				// Apply direct modifiers to the pool
				{
					AttribType eAttrib = ppool->eAttribTarget;
					F32 fBasicAbs = *F32PTR_OF_ATTRIB(pattrBasicAbs,eAttrib);
					*pfTarget += fBasicAbs;

					if(fMax)
					{
						F32 fBasicFactPos = *F32PTR_OF_ATTRIB(pattrBasicFactPos,eAttrib);
						F32 fBasicFactNeg = *F32PTR_OF_ATTRIB(pattrBasicFactNeg,eAttrib);
						*pfTarget += fMax * (fBasicFactPos - fBasicFactNeg);
					}

					// Clamp
					if(ppool->eAttribMax)
					{
						*pfTarget = MIN(*pfTarget,fMax);
					}
					if(ppool->eAttribMin)
					{
						*pfTarget = MAX(*pfTarget,fMin);
					}
				}

				// We have now calculated the new target value, and by setting these values
				// we make sure it doesn't get blown away by the started attribute compile step. 
				*F32PTR_OF_ATTRIB(pattrBasicAbs,ppool->eAttribTarget) = *pfTarget;
				*F32PTR_OF_ATTRIB(pattrBasicFactPos,ppool->eAttribTarget) = 0;
				*F32PTR_OF_ATTRIB(pattrBasicFactNeg,ppool->eAttribTarget) = 0;
			}

			// Clamp
			if(ppool->eAttribMax)
			{
				*pfCur = MIN(*pfCur,fMax);
			}
			if(ppool->eAttribMin)
			{
				*pfCur = MAX(*pfCur,fMin);
			}

			//Target Clamps
			if(ppool->eTargetClamp == kAttribPoolTargetClamp_Max)
			{
				F32 *pfTarget = F32PTR_OF_ATTRIB(pattrBasicAbs,ppool->eAttribTarget);

				*pfCur = MIN(*pfCur,*pfTarget);
			}
			if(ppool->eTargetClamp == kAttribPoolTargetClamp_Min)
			{
				F32 *pfTarget = F32PTR_OF_ATTRIB(pattrBasicAbs,ppool->eAttribTarget);

				*pfCur = MAX(*pfCur,*pfTarget);
			}

			// We have calculated the current value, but we need to make sure that doesn't get blown away
			//  during the standard attribute compile step, which comes next.  In that process, the value
			//  from pattrBasicAbs is added to the class base value, then scaled with pattrBasicFactPos and
			//  pattrBasicFactNeg.  We've already handled all that, so we'll just set pattrBasicAbs to the
			//  current value, and pattrBasicFactPos,pattrBasicFactNeg to 0, and assume the class base value
			//  is 0 (which it should be, since this is just the current value of a AttribPool).  By setting
			//  this way, the standard compile re-sets the value to what we've already calculated it to be.
			*F32PTR_OF_ATTRIB(pattrBasicAbs,ppool->eAttribCur) = *pfCur;
			*F32PTR_OF_ATTRIB(pattrBasicFactPos,ppool->eAttribCur) = 0;
			*F32PTR_OF_ATTRIB(pattrBasicFactNeg,ppool->eAttribCur) = 0;
		}

		PERFINFO_AUTO_STOP();
	}
}

// Specialized compile loop for the 'Basic' set.
// First compiles max hp, scales cur hp and then applies damage
// Then compiles max power and applies power mods
// Then for everything else, sets target=abs+(1+pos)/(1+neg)
// Has an optional Bonus "aspect" for HitPointsMax for STO sidekicking
static void CompileBasicAttribs(SA_PARAM_NN_VALID CharacterAttribs *pattrTarget,
								SA_PARAM_NN_VALID CharacterAttribs *pattrAbs,
								SA_PARAM_NN_VALID CharacterAttribs *pattrPos,
								SA_PARAM_NN_VALID CharacterAttribs *pattrNeg,
								SA_PARAM_NN_VALID CharacterAttribs *pattrClass,
								F32 fHitPointsMaxBonus,
								F32 fHitPointsMaxOverride,
								F32 fPowerMaxOverride,
								bool bBootstrapping)
{
	// F32* directly to a particular attribute in a particular CharacterAttribs structure
	F32 *pfTarget = (F32*)pattrTarget;
	F32 *pfAbs = (F32*)pattrAbs;
	F32 *pfPos = (F32*)pattrPos;
	F32 *pfNeg = (F32*)pattrNeg;
	F32 *pfClass = (F32*)pattrClass;
	
	// F32* directly to target HP (for damage)
	F32 *pfHP;

	int i;
	F32 fMax;

	PERFINFO_AUTO_START_FUNC();
	
	/***** Health Pool *****/
	// Move F32* to max hp (from 0)
	pfTarget += kAttribType_HitPointsMax/4;
	pfAbs += kAttribType_HitPointsMax/4;
	pfPos += kAttribType_HitPointsMax/4;
	pfNeg += kAttribType_HitPointsMax/4;
	pfClass += kAttribType_HitPointsMax/4;
	
	// Save F32 to target HP (Assumes hp is after max hp)
	pfHP = pfTarget+1;
	
	// Save old max hp
	fMax = *pfTarget;
	
	// Calc new max hp
	//  "Bonus" should already be (1.0f + Bonus)
	if(fHitPointsMaxOverride==FLT_MIN)
		*pfTarget = (*pfAbs + *pfClass * (1.0f + *pfPos)/(1.0f + *pfNeg)) * fHitPointsMaxBonus;
	else
		*pfTarget = fHitPointsMaxOverride;

	// Hardcoded HP pool change rule: Scale the pool by the change in the max
	if(fMax>0.0f && fMax!=*pfTarget)
	{	
		*pfHP *= ((*pfTarget)/fMax);
	}
	
	// Save new max hp
	fMax = *pfTarget;

	// Apply untyped damage (mods placed directly in HP pool), assumes hp is after max hp
	pfAbs++; pfPos++; pfNeg++;
	*pfHP += *pfAbs + fMax * (*pfPos - *pfNeg);

	// Typed damage!
	// - Special case - Damage is 'negative', in the sense that the typical usage is to subtract
	//  the magnitude from the hp pool.  We do just that here, in order to make it more sensible
	//  to the designers, and also to get rid of one extra step in all the expressions for damage
	//  AttribMods.  Note we only do it for the Abs modifications.  Fact modifications work like
	//  their name implies.
	// Reset F32* back to start
	pfTarget = (F32*)pattrTarget;
	pfAbs = (F32*)pattrAbs;
	pfPos = (F32*)pattrPos;
	pfNeg = (F32*)pattrNeg;
	for(i=0; i<g_iDamageTypeCount; i++, pfTarget++, pfAbs++, pfPos++, pfNeg++)
	{
		*pfHP += fMax * (*pfPos - *pfNeg) - *pfAbs;
	}

	// Only clamp the HP when we're not bootstrapping.
	// When bootstrapping the basic attribute values are not properly calculated.
	// Clamping the HP might cause the character to die or take damage when they should not.
	if (!bBootstrapping)
	{
		// Clamp
		if(*pfHP > fMax)
			*pfHP = fMax;
	}

	
	/***** Power Pool *****/
	// Move F32* to max power
	pfTarget = (F32*)pattrTarget + kAttribType_PowerMax/4;
	pfAbs = (F32*)pattrAbs + kAttribType_PowerMax/4;
	pfPos = (F32*)pattrPos + kAttribType_PowerMax/4;
	pfNeg = (F32*)pattrNeg + kAttribType_PowerMax/4;
	pfClass = (F32*)pattrClass + kAttribType_PowerMax/4;
	
	// Save old max pow
	fMax = *pfTarget;
	
	// Calc new max pow
	if(fPowerMaxOverride==FLT_MIN)
		*pfTarget = *pfAbs + *pfClass * (1.0f + *pfPos)/(1.0f + *pfNeg);
	else
		*pfTarget = fPowerMaxOverride;
	
	// Hardcoded pow pool change rule: Scale the pool by the change in the max, assumes power is after max power
	if (fMax>0.0f && fMax!=*pfTarget)
	{
		*(pfTarget+1) *= ((*pfTarget)/fMax);
	}

	// Save new max pow
	fMax = *pfTarget;

	// Power! assumes power is after max power
	pfTarget++;	pfAbs++; pfPos++; pfNeg++; pfClass++;
	if(!g_bAttribPoolPower)
		*pfTarget += *pfAbs + fMax * (*pfPos - *pfNeg);

	// Clamp
	if(*pfTarget > fMax)
		*pfTarget = fMax;

	
	/***** Air Pool *****/
	// Move F32* to max air
	pfTarget = (F32*)pattrTarget + kAttribType_AirMax/4;
	pfAbs = (F32*)pattrAbs + kAttribType_AirMax/4;
	pfPos = (F32*)pattrPos + kAttribType_AirMax/4;
	pfNeg = (F32*)pattrNeg + kAttribType_AirMax/4;
	pfClass = (F32*)pattrClass + kAttribType_AirMax/4;
	
	// Save old max air
	fMax = *pfTarget;
	
	// Calc new max air
	*pfTarget = *pfAbs + *pfClass * (1.0f + *pfPos)/(1.0f + *pfNeg);

	// Hardcoded air pool change rule: Scale the pool by the change in the max, assumes air is after max air
	if (fMax>0.0f && fMax!=*pfTarget)
	{
		*(pfTarget+1) *= ((*pfTarget)/fMax);
	}

	// Save new max air
	fMax = *pfTarget;

	// Air! assumes air is after max air
	pfTarget++; pfAbs++; pfPos++; pfNeg++; pfClass++;
	*pfTarget += *pfAbs + fMax * (*pfPos - *pfNeg);

	// Clamp
	if(*pfTarget > fMax)
		*pfTarget = fMax;

	
	// Everything else! (assumed after max air)
	pfTarget++; pfAbs++; pfPos++; pfNeg++; pfClass++;
	for(i=(FIRST_NORMAL_ATTRIBUTE/4); i<g_iCharacterAttribCount; i++, pfTarget++, pfAbs++, pfPos++, pfNeg++, pfClass++)
	{
		*pfTarget = *pfAbs + *pfClass * (1.0f + *pfPos)/(1.0f + *pfNeg);
	}

	PERFINFO_AUTO_STOP();
}

// Slimmed down and tweaked version of character_AccrueMods, specifically for
//  recalculating a character's innate effects.  Returns true if it filled in
//  pSet properly.
S32 character_AccrueModsInnate(int iPartitionIdx, Character *pchar, AttribAccrualSet *pSet)
{
	int i;
	S32 bInnate = false;
	CharacterClass *pClass;
	int iLevel;

	PERFINFO_AUTO_START_FUNC();

	pClass = character_GetClassCurrent(pchar);
	iLevel = entity_GetCombatLevel(pchar->pEntParent);
	combateval_ContextSetupSimple(pchar, iLevel, NULL);

	AttribAccrualSet_SetDefaultValues(pSet, false);
	
	// Find all Innate powers
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowers[i];
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		// Check for BecomeCritter restriction
		if((pchar->bBecomeCritter || pchar->bBecomeCritterTickPhaseTwo) && ppow && ppow->eSource!=kPowerSource_AttribMod)
			continue;

		if(pdef && pdef->eType==kPowerType_Innate)
		{
			// Accumulate all attrib mods
			int j;
			F32 fTableScale = ppow->fTableScale;
			for(j=eaSize(&pdef->ppOrderedMods)-1; j>=0; j--)
			{
				bInnate |= mod_ProcessInnate(iPartitionIdx,pdef->ppOrderedMods[j],pchar,pClass,iLevel,fTableScale,pSet);
			}
		}
	}

	// Find all external innate powers
	if(pchar->pEntParent->externalInnate)
	{
		for(i=eaSize(&pchar->pEntParent->externalInnate->ppPowersExternalInnate)-1; i>=0; i--)
		{
			Power *ppow = pchar->pEntParent->externalInnate->ppPowersExternalInnate[i];
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			if(pdef && pdef->eType==kPowerType_Innate)
			{
				// Accumulate all attrib mods
				int j;
				F32 fTableScale = ppow->fTableScale;
				for(j=eaSize(&pdef->ppOrderedMods)-1; j>=0; j--)
				{
					bInnate |= mod_ProcessInnate(iPartitionIdx,pdef->ppOrderedMods[j],pchar,pClass,iLevel,fTableScale,pSet);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return bInnate;
}

// Non-Character version of character_AccrueModsInnate, which just operates on
//  external Innate powers and uses them to change movement-related values
S32 entity_AccrueModsInnate(int iPartitionIdx, Entity *pent)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	static AttribAccrualSet *s_pSet = NULL;
	int i;

	if(pent->pChar || !pent->externalInnate)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if(!s_pSet)
		s_pSet = StructAlloc(parse_AttribAccrualSet);
	else
		ZeroStruct(s_pSet);

	for(i=eaSize(&pent->externalInnate->ppPowersExternalInnate)-1; i>=0; i--)
	{
		Power *ppow = pent->externalInnate->ppPowersExternalInnate[i];
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
		if(pdef && pdef->eType==kPowerType_Innate)
		{
			// Accumulate all attrib mods
			int j;
			F32 fTableScale = ppow->fTableScale;
			for(j=eaSize(&pdef->ppOrderedMods)-1; j>=0; j--)
			{
				mod_ProcessInnate(iPartitionIdx,pdef->ppOrderedMods[j],NULL,NULL,1,fTableScale,s_pSet);
			}
		}
	}

	// All processed, apply the specific things we care about
	if(s_pSet->CharacterAttribs.attrBasicAbs.fFlight > 0)
	{
		F32 fFriction = s_pSet->CharacterAttribs.attrBasicAbs.fFrictionFlying ? s_pSet->CharacterAttribs.attrBasicAbs.fFrictionFlying : 1.f;
		F32 fTraction = s_pSet->CharacterAttribs.attrBasicAbs.fTractionFlying ? s_pSet->CharacterAttribs.attrBasicAbs.fTractionFlying : 0.5f;
		pmSetFlightEnabled(pent);

		pmSetFlightSpeed(pent, MAX(0,s_pSet->CharacterAttribs.attrBasicAbs.fSpeedFlying));
		pmSetFlightFriction(pent, MAX(0,fFriction));
		pmSetFlightTraction(pent, MAX(0,fTraction));
		pmSetFlightTurnRate(pent, MAX(0,RAD(s_pSet->CharacterAttribs.attrBasicAbs.fTurnRateFlying)));
	}
	else
	{
		pmSetFlightDisabled(pent);
	}

	PERFINFO_AUTO_STOP();

	return true;
#endif
}

static void CharacterProcessAttribModShieldPercentIgnored(int iPartitionIdx, Character *pchar)
{
	int i,j;
	for(i=eaSize(&pchar->ppModsShield)-1; i>=0; i--)
	{
		AttribMod *pmodShield = pchar->ppModsShield[i];
		ShieldParams *pparamsShield = (ShieldParams*)pmodShield->pDef->pParams;
		if(pmodShield->pParams && pparamsShield)
		{
			pmodShield->pParams->vecParam[1] = pparamsShield->fPercentIgnored;
			for(j=eaSize(&pchar->ppModsAttribModShieldPercentIgnored)-1; j>=0; j--)
			{
				AttribMod *pmodPct = pchar->ppModsAttribModShieldPercentIgnored[j];
				if(moddef_AffectsModOrPowerChk(iPartitionIdx,pmodPct->pDef,pchar,pmodPct,pmodShield->pDef,pmodShield,pmodShield->pDef->pPowerDef))
				{
					pmodShield->pParams->vecParam[1] += pmodPct->fMagnitude;
				}
			}
			pmodShield->pParams->vecParam[1] = CLAMPF32(pmodShield->pParams->vecParam[1],0,1);
		}
	}
}

static void character_PreProcessSpeedCooldown(Character* pchar)
{
	int i;
	for (i = eaSize(&pchar->ppSpeedCooldown)-1; i >= 0; i--)
	{
		CooldownRateModifier* pSpeedCooldown = pchar->ppSpeedCooldown[i];
		CooldownRateModifier* pSpeedCooldownInnate = NULL;

		if (pchar->pInnateAccrualSet)
		{
			pSpeedCooldownInnate = eaIndexedGetUsingInt(&pchar->pInnateAccrualSet->ppSpeedCooldown, pSpeedCooldown->iPowerCategory);
		}
		if (pSpeedCooldownInnate)
		{
			pSpeedCooldown->fBasicAbs = pSpeedCooldownInnate->fBasicAbs;
			pSpeedCooldown->fBasicPos = pSpeedCooldownInnate->fBasicPos;
			pSpeedCooldown->fBasicNeg = pSpeedCooldownInnate->fBasicNeg;
		}
		else
		{
			pSpeedCooldown->fBasicAbs = 0.0f;
			pSpeedCooldown->fBasicPos = 0.0f;
			pSpeedCooldown->fBasicNeg = 0.0f;
		}
		pSpeedCooldown->bDirty = false;
	}
}

static void character_PostProcessSpeedCooldown(Character* pchar)
{
	int i;
	for (i = eaSize(&pchar->ppSpeedCooldown)-1; i >= 0; i--)
	{
		CooldownRateModifier* pSpeedCooldown = pchar->ppSpeedCooldown[i];
		if (pSpeedCooldown->bDirty)
		{
			F32 fBasicBase = pchar->pattrBasic ? pchar->pattrBasic->fSpeedCooldown : 1.0f;
			pSpeedCooldown->fValue = pSpeedCooldown->fBasicAbs + fBasicBase * (1.0f + pSpeedCooldown->fBasicPos) / (1.0f + pSpeedCooldown->fBasicNeg);
		}
		else
		{
			StructDestroy(parse_CooldownRateModifier, eaRemove(&pchar->ppSpeedCooldown, i));
		}
	}
	if (!eaSize(&pchar->ppSpeedCooldown))
	{
		eaDestroy(&pchar->ppSpeedCooldown);
	}
}

static S32 CmpS32(const S32 *i, const S32 *j) { return *i - *j; }

static S32 CmpShieldMods(const AttribMod **a, const AttribMod **b)
{
	ShieldParams *pa = (ShieldParams*)((*a)->pDef->pParams);
	ShieldParams *pb = (ShieldParams*)((*b)->pDef->pParams);
	if(pa && pb)
	{
		if(pa->iPriority > pb->iPriority)
			return -1;

		if(pa->iPriority < pb->iPriority)
			return 1;
	}

	return mod_CmpDurationDefIdx(a,b);
}

// Accrues the effects of a character's attrib mods onto the character's attribs
void character_AccrueModsEx(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract, bool bBootstrapping, U32 uiTimeLoggedOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	AttribAccrualSet *pAttrSet = NULL;
	AttribAccrualSet *pInnateAccrualSet;
	int i,s;
	S32 bDirtyAll = false;
	S32 *piDirtyAttribs = NULL;
	CharacterClass *pClass = character_GetClassCurrent(pchar);
	CharacterAttribs *pattrClass = NULL;
		
	S32 bModeCombat, bModeMounted, bModeRiding, bModeCrouch, bModeCrouchMove, bModeSprint, 
		bModeRoll, bModeThrottleReverse, bModeBattleForm, bModeBloodied, bModeShooter, 
		bModeAutoRun = 0;

	PERFINFO_AUTO_START_FUNC();

	if(fRate > 0 || uiTimeLoggedOut > 0)
		character_SaveModes(pchar);

	// Skip accruing mods if at all possible
	// Little worried about the mode test getting slow due to its stupid implementation

	PERFINFO_AUTO_START("CheckSkipPrep", 1);
	bModeCombat = !!pchar->uiTimeCombatExit;
	bModeMounted = !!entGetRider(pchar->pEntParent);
	bModeRiding = !!entGetMount(pchar->pEntParent);
	
	if (g_CombatConfig.tactical.aim.bSplitAimAndCrouch)
	{
		bModeCrouch = entIsCrouching(pchar->pEntParent);
		bModeCrouchMove = entIsAiming(pchar->pEntParent);
	}
	else
	{
		F32 fSpeedXZSqr;
		Vec3 vVel;
		entCopyVelocityFG(pchar->pEntParent, vVel);
		fSpeedXZSqr = lengthVec3SquaredXZ(vVel);
		bModeCrouch = entIsCrouching(pchar->pEntParent);
		bModeCrouchMove = bModeCrouch && fSpeedXZSqr >= 1;
		bModeCrouch &= fSpeedXZSqr < 1;
	}
	bModeSprint = entIsSprinting(pchar->pEntParent);
	bModeRoll = entIsRolling(pchar->pEntParent);
	bModeThrottleReverse = pchar->pEntParent->pPlayer ? pchar->pEntParent->pPlayer->fMovementThrottle < 0 : false;
	bModeBattleForm = pchar->bBattleForm;
	bModeBloodied = g_CombatConfig.fBloodiedThreshold && pchar->pattrBasic->fHitPoints <= g_CombatConfig.fBloodiedThreshold * pchar->pattrBasic->fHitPointsMax;
	bModeShooter = entIsUsingShooterControls(pchar->pEntParent);
	
	PERFINFO_AUTO_STOP(); // CheckSkipPrep

	PERFINFO_AUTO_START("CheckSkip", 1);

	if(!eaSize(&pchar->modArray.ppMods)
		&& !eaSize(&pchar->modArray.ppModsPending)
		&& pchar->pInnateAccrualSet
		&& pchar->pInnatePowersAccrualSet
		&& pchar->pInnateEquipAccrualSet
		&& pchar->pInnateStatPointsSet

		// todo RP: this is getting a little big, some of these we probably don't care about for every game (kPowerMode_Mounted is probably depreciated
		&& (!bModeCombat == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Combat)))
		&& (!bModeRiding == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Riding)))
		&& (!bModeMounted == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Mounted)))
		&& (!bModeCrouch == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Crouch)))
		&& (!bModeCrouchMove == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_CrouchMove)))
		&& (!bModeSprint == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Sprint)))
		&& (!bModeRoll == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Roll)))
		&& (!bModeThrottleReverse == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_ThrottleReverse)))
		&& (!bModeBattleForm == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_BattleForm)))
		&& (!bModeBloodied == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Bloodied)))
		&& (!bModeShooter == (-1 == eaiFind(&pchar->piPowerModes, kPowerMode_Shooter)))
		)
	{
		// No mods this time, no mods last time... only possible change is regen
		if(pchar->bSkipAccrueMods)
		{
			if(pchar->pattrBasic->fHitPoints > pchar->pattrBasic->fHitPointsMax)
			{
				pchar->pattrBasic->fHitPoints = pchar->pattrBasic->fHitPointsMax;
			}

			if(pchar->pattrBasic->fPower > pchar->pattrBasic->fPowerMax)
			{
				pchar->pattrBasic->fPower = pchar->pattrBasic->fPowerMax;
			}
			character_AttribPoolsClamp(pchar);
			PERFINFO_AUTO_STOP(); // CheckSkip
			PERFINFO_AUTO_STOP();
			return;
		}

		pchar->bSkipAccrueMods = true;
	}
	else
	{
		pchar->bSkipAccrueMods = false;
	}

	PERFINFO_AUTO_STOP(); // CheckSkip

	PERFINFO_AUTO_START("InnateCopy", 1);
	pAttrSet = StructAlloc(parse_AttribAccrualSet);

	pInnateAccrualSet = character_GetInnateAccrual(iPartitionIdx,pchar,&bDirtyAll);
	if(pInnateAccrualSet)
	{
		*pAttrSet = *pInnateAccrualSet;
		pAttrSet->ppSpeedCooldown = NULL;
	}
	else
	{
		ZeroStruct(pAttrSet);
	}
	PERFINFO_AUTO_STOP(); // InnateCopy
	
	pchar->fPowerShieldRatio = 0.0f;

	if(fRate > 0 || uiTimeLoggedOut > 0)
	{
		static int *s_piExpiredIdxs = NULL;	// Indices of expired mods
		static int *s_piPowerModesBackup = NULL;
		static U32 *s_perHiddenBackup = NULL;
		static U32 *s_perUntargetableBackup = NULL;
		bool bNeedRewardPass = false;

		PERFINFO_AUTO_START("RandomEAClears", 1);
		// Clear out character's fragile list
		//  eaClear is more heavy-handed than we need to be, but it should help
		//  debugging if we don't have extraneous pointers beyond the size
		eaClear(&pchar->modArray.ppFragileMods);
		eaClear(&pchar->modArray.ppOverrideMods);
		pchar->modArray.bHasBasicDisableAffects = false;
		eaClearFast(&pchar->ppModsAIAggro);

		// Save and clear the Character's untargetable and hidden lists
		ea32Copy(&s_perUntargetableBackup,&pchar->perUntargetable);
		ea32Copy(&s_perHiddenBackup,&pchar->perHidden);
		ea32Clear(&pchar->perUntargetable);
		ea32Clear(&pchar->perHidden);
		PERFINFO_AUTO_STOP(); //RandomEAClears
		
		PERFINFO_AUTO_START("ModesSetup", 1);
		// Backup the old PowerModes list, clear it for this tick, and copy in the modes from the AI
		eaiCopy(&s_piPowerModesBackup,&pchar->piPowerModes);
		eaiClear(&pchar->piPowerModes);
#ifdef GAMESERVER
		if(pchar->pEntParent->aibase)
		{
			eaiCopy(&pchar->piPowerModes,&pchar->pEntParent->aibase->powersModes);
		}
#endif

		// Update the combat mode and related event
		if(bModeCombat)
		{
			eaiPush(&pchar->piPowerModes, kPowerMode_Combat);
			if(-1 == eaiFind(&s_piPowerModesBackup,kPowerMode_Combat))
			{
				#ifdef GAMESERVER
				if (g_CombatConfig.bSwitchCapsulesInCombat && pchar->pEntParent->pPlayer)
				{
					// AI Handles this itself
					mmCollisionSetHandleDestroyFG(&pchar->pEntParent->mm.mcsHandle);
					mmCollisionSetHandleCreateFG(pchar->pEntParent->mm.movement, &pchar->pEntParent->mm.mcsHandle, __FILE__, __LINE__, -1); 
					// -1 means to use second capsule set if available					
				}
				if (g_CombatConfig.bCollideWithPetsInCombat && pchar->pEntParent->pPlayer)
				{
					mmCollisionBitsHandleDestroyFG(&pchar->pEntParent->mm.mcbHandle);
					mmCollisionBitsHandleCreateFG(pchar->pEntParent->mm.movement, &pchar->pEntParent->mm.mcbHandle, __FILE__, __LINE__, ~0); // Collide with everything if in combat
				}
				#endif
				character_CombatEventTrack(pchar,kCombatEvent_CombatModeStart);
				entity_UpdateItemArtAnimFX(pchar->pEntParent);
				pmUpdateTacticalRunParams(pchar->pEntParent, pchar->pattrBasic->fSpeedRunning, true);
				pmUpdateCombatAnimBit(pchar->pEntParent, true);
			}
		}

		// Update the other code-set modes
		if(bModeRiding)
			eaiPush(&pchar->piPowerModes, kPowerMode_Riding);
		if(bModeMounted)
			eaiPush(&pchar->piPowerModes, kPowerMode_Mounted);
		if(bModeCrouch)
			eaiPush(&pchar->piPowerModes, kPowerMode_Crouch);
		if(bModeCrouchMove)
			eaiPush(&pchar->piPowerModes, kPowerMode_CrouchMove);
		if(bModeSprint)
			eaiPush(&pchar->piPowerModes, kPowerMode_Sprint);
		if(bModeRoll)
			eaiPush(&pchar->piPowerModes, kPowerMode_Roll);
		if(bModeThrottleReverse)
			eaiPush(&pchar->piPowerModes, kPowerMode_ThrottleReverse);
		if(bModeBattleForm)
			eaiPush(&pchar->piPowerModes, kPowerMode_BattleForm);
		if(bModeShooter)
			eaiPush(&pchar->piPowerModes, kPowerMode_Shooter);
		if(bModeBloodied)
		{
			eaiPush(&pchar->piPowerModes, kPowerMode_Bloodied);
			if(-1==eaiFind(&s_piPowerModesBackup,kPowerMode_Bloodied))
				character_CombatEventTrack(pchar,kCombatEvent_BloodiedStart);
		}
		
		PERFINFO_AUTO_STOP(); // ModesSetup


		// Process the pending AttribMods
		character_ModsProcessPending(iPartitionIdx,pchar,fRate,pExtract);

		// Find all basic mods and push their attrib into the dirty EArray
/* JW: Commented out until I have more time to work on it
		s = eaSize(&pchar->modArray.ppMods);
		for(i=s-1; i>=0; i--)
		{
			AttribMod *pmod = pchar->modArray.ppMods[i];
			if(IS_NORMAL_ATTRIB(pmod->pDef->offAttrib) 
				&& IS_BASIC_ASPECT(pmod->pDef->offAspect))
			{
				eaiPushUnique(&piDirtyAttribs,pmod->pDef->offAttrib);
			}
		}
*/

		// Apply suppression
		character_ModsSuppress(iPartitionIdx, pchar);

		// Process the removed mods
		character_ModsRemoveExpired(iPartitionIdx,pchar, fRate, pExtract);

		// Pre-process speed cooldown data
		character_PreProcessSpeedCooldown(pchar);

		s = eaSize(&pchar->modArray.ppMods);

		// First pass, find damage triggers/preventers, as well as the two attributes Shields need to know about
		//  to operate properly when damage actually occurs
		// Also find complex triggers so they can flag what events we need to watch for
		PERFINFO_AUTO_START("TriggerPass",1);
		eaClearFast(&pchar->ppModsDamageTrigger);
		eaClearFast(&pchar->ppModsShield);
		if(pchar->pCombatEventState)
		{
			ZeroArray(pchar->pCombatEventState->abCombatEventTriggerComplex);
			ZeroArray(pchar->pCombatEventState->abCombatEventTriggerSimple);
		}
		for(i=s-1; i>=0; i--)
		{
			AttribMod *pmod = pchar->modArray.ppMods[i];
			AttribType eAttrib = pmod->pDef->offAttrib;
			if(eAttrib==kAttribType_DamageTrigger
				|| eAttrib==kAttribType_Shield
				|| eAttrib==kAttribType_AttribModShieldPercentIgnored
				|| eAttrib==kAttribType_AttribModFragilityScale
				|| eAttrib==kAttribType_TriggerComplex 
				|| eAttrib==kAttribType_TriggerSimple)
			{
				mod_Process(iPartitionIdx,pmod,pchar,fRate,pAttrSet,s_piPowerModesBackup, uiTimeLoggedOut, pExtract);
			}
		}
		if(eaSize(&pchar->ppModsShield))
		{
			eaQSort(pchar->ppModsShield,CmpShieldMods);
			CharacterProcessAttribModShieldPercentIgnored(iPartitionIdx,pchar);
		}
		PERFINFO_AUTO_STOP();

		s = eaSize(&pchar->modArray.ppMods);

		// Second pass, everything else
		PERFINFO_AUTO_START("MainPass",1);
		for(i=s-1; i>=0; i--)
		{
			AttribMod *pmod = pchar->modArray.ppMods[i];
			AttribType eAttrib = pmod->pDef->offAttrib;
			if(eAttrib!=kAttribType_DamageTrigger
				&& eAttrib!=kAttribType_Shield
				&& eAttrib!=kAttribType_AttribModShieldPercentIgnored
				&& eAttrib!=kAttribType_AttribModFragilityScale
				&& eAttrib!=kAttribType_TriggerComplex
				&& eAttrib!=kAttribType_TriggerSimple)
			{
				if(eAttrib==kAttribType_GrantReward)
				{
					// rewards can cause a costume check and as the costumes may not be checked yet the reward needs to be done later
					bNeedRewardPass = true;
				}
				else
				{
					mod_Process(iPartitionIdx,pmod,pchar,fRate,pAttrSet,s_piPowerModesBackup, uiTimeLoggedOut, pExtract);
				}
			}
		}
		PERFINFO_AUTO_STOP();

		if(bNeedRewardPass)
		{
			// 3rd pass, rewards, this needs to be last as the fix-up for costumes are here when items are granted, existing costumes must be set in main pass
			PERFINFO_AUTO_START("RewardPass",1);
			for(i=s-1; i>=0; i--)
			{
				AttribMod *pmod = pchar->modArray.ppMods[i];
				AttribType eAttrib = pmod->pDef->offAttrib;
				if(eAttrib == kAttribType_GrantReward)
				{
					mod_Process(iPartitionIdx,pmod,pchar,fRate,pAttrSet,s_piPowerModesBackup, uiTimeLoggedOut, pExtract);
				}
			}
			PERFINFO_AUTO_STOP();
		}

		// Post-process speed cooldown data
		character_PostProcessSpeedCooldown(pchar);
		
		PERFINFO_AUTO_START("EASortCompare",1);
		
		// Sort the various earrays, then compare to the backups
		ea32QSort(pchar->piPowerModes,CmpS32);
		ea32QSort(pchar->perUntargetable,cmpU32);
		ea32QSort(pchar->perHidden,cmpU32);
		
		if(eaiCompare(&s_piPowerModesBackup,&pchar->piPowerModes) != 0)
		{
			character_ModesChanged(iPartitionIdx,pchar,pExtract);
		}

		if(eaiCompare(&s_perUntargetableBackup,&pchar->perUntargetable) != 0
			|| eaiCompare(&s_perHiddenBackup,&pchar->perHidden) != 0)
		{
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

			// Note new Placate events
			for(i=ea32Size(&pchar->perUntargetable)-1; i>=0; i--)
			{
				if(-1==ea32Find(&s_perUntargetableBackup,pchar->perUntargetable[i]))
				{
					Entity *pEntPlacater = entFromEntityRef(iPartitionIdx,pchar->perUntargetable[i]);
					if(pEntPlacater && pEntPlacater->pChar)
					{
						character_CombatEventTrackInOut(pchar, kCombatEvent_PlacateIn, kCombatEvent_PlacateOut,
														pEntPlacater, NULL, NULL, 0, 0, NULL, NULL);
					}
				}
			}
		}
		
		PERFINFO_AUTO_STOP(); //EASortCompare

		// Apply heals
		if(eaSize(&pchar->ppModsHeal))
		{
			PERFINFO_AUTO_START("AttribModHeals",1);
			for(i=eaSize(&pchar->ppModsHeal)-1; i>=0; i--)
			{
				int j;
				AttribMod *pModHeal = pchar->ppModsHeal[i];
				AttribModDef *pdef = pModHeal->pDef;
				AttribModHealParams *pParams = (AttribModHealParams*)pdef->pParams;
				F32 fHealMag = mod_GetEffectiveMagnitude(iPartitionIdx,pModHeal,pdef,pchar);

				if(pParams)
				{
					for(j=eaSize(&pchar->modArray.ppMods)-1; j>=0; j--)
					{
						AttribMod *pmod = pchar->modArray.ppMods[j];

						if(pmod->fDuration >= 0 && !pmod->bIgnored
							&& moddef_AffectsModOrPowerChk(iPartitionIdx,pdef,pchar,pModHeal,pmod->pDef,pmod,pmod->pDef->pPowerDef))
						{
							switch(pParams->eAspect)
							{
							case kAttribModShareAspect_Magnitude:
								pmod->fMagnitude += fHealMag;
								MIN1(pmod->fMagnitude,pmod->fMagnitudeOriginal);
								entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
								break;
							case kAttribModShareAspect_Duration:
								pmod->fDuration += fHealMag;
								MIN1(pmod->fDuration,pmod->fDurationOriginal);
								break;
							case kAttribModShareAspect_Health:
								// Directly affect the fragility health (will get clamped later)
								mod_FragileAffect(pmod,fHealMag);
								entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
								
								if (pParams->bCountsAsHeal)
								{
									Entity *eSource = entFromEntityRef(iPartitionIdx,pModHeal->erSource);
									damageTracker_AddTick(iPartitionIdx,
														  pchar,
														  pModHeal->erOwner,
														  pModHeal->erSource,
														  pchar->pEntParent->myRef,
														  -fHealMag,
														  -fHealMag,
														  pmod->pDef->offAttrib,
														  pModHeal->uiApplyID,
														  GET_REF(pModHeal->hPowerDef),
														  pModHeal->uiDefIdx,
														  NULL,
														  pModHeal->eFlags);

									character_CombatEventTrackInOut(pchar, kCombatEvent_AttribHealIn, kCombatEvent_AttribHealOut,
																	eSource, pModHeal->pDef->pPowerDef, pModHeal->pDef,
																	-fHealMag, -fHealMag, NULL, NULL);
								}

#ifdef GAMESERVER 
								// If the Character's shield was changed, and it's not full, set active
								if(pmod->pDef->offAttrib==kAttribType_Shield
									&& pmod->pFragility
									&& pmod->pFragility->fHealth < pmod->pFragility->fHealthMax)
								{
									entSetActive(pchar->pEntParent);
								}
#endif

								break;
							}
						}
					}
				}
			}
			eaDestroy(&pchar->ppModsHeal);
			PERFINFO_AUTO_STOP();
		}

		// Apply shares
		if(eaSize(&pchar->ppModsShare))
		{
			PERFINFO_AUTO_START("AttribModShares",1);
			for(i=eaSize(&pchar->ppModsShare)-1; i>=0; i--)
			{
				int j;
				AttribMod **ppModsSharing = NULL;
				F32 *pfModShares = NULL;
				F32 fSharing = 0, fShares = 0, fGiveBack = 0, fSubTotal = 0, fTransferPortion = 0, fNumber = 0;
				AttribModDef *pdef = pchar->ppModsShare[i]->pDef;
				AttribModShareParams *pParams = (AttribModShareParams*)pdef->pParams;
				bool NoFavoredMod = true;
				if(pParams)
				{
					for(j=eaSize(&pchar->modArray.ppMods)-1; j>=0; j--)
					{
						AttribMod *pmod = pchar->modArray.ppMods[j];

						if(pmod->fDuration >= 0 && !pmod->bIgnored
							&& moddef_AffectsModOrPowerChk(iPartitionIdx,pdef,pchar,pchar->ppModsShare[i],pmod->pDef,pmod,pmod->pDef->pPowerDef))
						{
							F32 fShare = 1.f;
							if(pParams->pExprShare)
							{
								combateval_ContextSetupAffects(pchar,NULL,pmod->pDef->pPowerDef,pmod->pDef,pmod,NULL);
								fShare = combateval_EvalNew(iPartitionIdx, pParams->pExprShare,kCombatEvalContext_Affects,NULL);
								MAX1(fShare,0);
							}
							if(!pParams->pExprContribution)
							{
								fTransferPortion = 0.99f;// default value, making sure that the target gets as much as possible,
														 // then redistributes the remainder.  This is Jered's advice.
														 // a value of 1.0 will cause problems, working on it.
							}
							else
							{
								fTransferPortion = combateval_EvalNew(iPartitionIdx, pParams->pExprContribution,kCombatEvalContext_Affects,NULL);
							}

							eaPush(&ppModsSharing,pmod);
							eafPush(&pfModShares,fShare);
							fShares += fShare - 1.0f;
							fNumber += 1.0f;

							if (fShare > 1.0f)
							{
								NoFavoredMod = false;
							}

							switch(pParams->eAspect)
							{
							case kAttribModShareAspect_Magnitude:
								fSharing += pmod->fMagnitude*fTransferPortion;
								pmod->fMagnitude = pmod->fMagnitude*(1.0f-fTransferPortion);
								entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
								break;
							case kAttribModShareAspect_Duration:
								fSharing += pmod->fDuration*fTransferPortion;
								pmod->fDuration = pmod->fDuration*(1.0f-fTransferPortion);
								break;
							case kAttribModShareAspect_Health:
								if(pmod->pFragility)
								{
									fSharing += pmod->pFragility->fHealth*fTransferPortion;
									pmod->pFragility->fHealth = pmod->pFragility->fHealth*(1.0f-fTransferPortion);
									entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
								}
								break;
							}
						}
					}
					if (!NoFavoredMod)
					{
						for(j=eaSize(&ppModsSharing)-1; j>=0; j--)
						{
							AttribMod *pmod = ppModsSharing[j];
							F32 fShare = fSharing * ((pfModShares[j]-1.0f) / fShares);
							switch(pParams->eAspect)
							{
							case kAttribModShareAspect_Magnitude:
								pmod->fMagnitude += fShare;
								if (pmod->fMagnitude > pmod->fMagnitudeOriginal)
								{
									fGiveBack += pmod->fMagnitude - pmod->fMagnitudeOriginal; // if something got more than its maximum, 
									pmod->fMagnitude = pmod->fMagnitudeOriginal;			  // cap it and store the remaining
								}
								else
								{
									fSubTotal += pmod->fMagnitude; // adding all values that didn't get 2 shares
								}
								break;
							case kAttribModShareAspect_Duration:
								pmod->fDuration += fShare;
								if (pmod->fDuration > pmod->fDurationOriginal)
								{
									fGiveBack += pmod->fDuration - pmod->fDurationOriginal; // if something got more than its maximum,
									pmod->fDuration = pmod->fDurationOriginal;			  // cap it and store the remaining
								}
								else
								{
									fSubTotal += pmod->fDuration; // adding all values that didn't get 2 shares
								}
								break;
							case kAttribModShareAspect_Health:
								if(pmod->pFragility)
								{
									pmod->pFragility->fHealth += fShare;
									if (pmod->pFragility->fHealth > pmod->pFragility->fHealthMax)
									{
										fGiveBack += pmod->pFragility->fHealth - pmod->pFragility->fHealthMax; // if something got more than its maximum,
										pmod->pFragility->fHealth = pmod->pFragility->fHealthMax;			    // cap it and store the remaining
									}
									else
									{
										fSubTotal += pmod->pFragility->fHealth; // adding all values that didn't get 2 shares
									}
								}
								break;
							}
						}
					}
					while (fGiveBack > 0.0f) // don't want to go through pointless steps
					{
						for(j=eaSize(&ppModsSharing)-1; j>=0; j--)
						{
							AttribMod *pmod = ppModsSharing[j];
							switch(pParams->eAspect)
							{
							case kAttribModShareAspect_Magnitude:
								if (pmod->fMagnitude < pmod->fMagnitudeOriginal)
								{
									pmod->fMagnitude += fGiveBack * pmod->fMagnitude / fSubTotal;  // this redistributes the extra, but does so for relative percentages.
																								   // Al asked me to do it this way.  -- Lat
								}
								break;
							case kAttribModShareAspect_Duration:
								if (pmod->fDuration < pmod->fDurationOriginal)
								{
									pmod->fDuration += fGiveBack * pmod->fDuration / fSubTotal;
								}
								break;
							case kAttribModShareAspect_Health:
								if(pmod->pFragility)
								{
									if (pmod->pFragility->fHealth < pmod->pFragility->fHealthMax)
									{
										pmod->pFragility->fHealth += fGiveBack * pmod->pFragility->fHealth / fSubTotal;
									}
								}
								break;
							}
						}
						fGiveBack = 0.0f;
						fSubTotal = 0.0f;
						
						for(j=eaSize(&ppModsSharing)-1; j>=0; j--)
						{
							AttribMod *pmod = ppModsSharing[j];
							switch(pParams->eAspect)
							{
							case kAttribModShareAspect_Magnitude:
								if (pmod->fMagnitude > pmod->fMagnitudeOriginal)
								{
									fGiveBack += pmod->fMagnitude - pmod->fMagnitudeOriginal;
									pmod->fMagnitude = pmod->fMagnitudeOriginal;
								}
								else if (pmod->fMagnitude < pmod->fMagnitudeOriginal)
								{
									fSubTotal += pmod->fMagnitude;
								}
								break;
							case kAttribModShareAspect_Duration:
								if (pmod->fDuration > pmod->fDurationOriginal)
								{
									fGiveBack += pmod->fDuration - pmod->fDurationOriginal;
									pmod->fDuration = pmod->fDurationOriginal;
								}
								else if (pmod->fDuration < pmod->fDurationOriginal)
								{
									fSubTotal += pmod->fDuration;
								}
								break;
							case kAttribModShareAspect_Health:
								if(pmod->pFragility)
								{
									if (pmod->pFragility->fHealth > pmod->pFragility->fHealthMax)
									{
										fGiveBack += pmod->pFragility->fHealth - pmod->pFragility->fHealthMax;
										pmod->pFragility->fHealth = pmod->pFragility->fHealthMax;
									}
									else if (pmod->pFragility->fHealth < pmod->pFragility->fHealthMax)
									{
										fSubTotal += pmod->pFragility->fHealth;
									}
								}
								break;
							}
						}
						if (fSubTotal == 0.0f)
							fGiveBack = 0.0f; // sanity check
					}
					if (NoFavoredMod)
					{
						fGiveBack = fSharing;
						for(j=eaSize(&ppModsSharing)-1; j>=0; j--)
						{
							AttribMod *pmod = ppModsSharing[j];
							switch(pParams->eAspect)
							{
							case kAttribModShareAspect_Magnitude:
								if (pfModShares[j] <= 1.0f)
								{
									pmod->fMagnitude += fGiveBack /fNumber;  // this redistributes the extra evenly, as Al requested
								}
								break;
							case kAttribModShareAspect_Duration:
								if (pfModShares[j] <= 1.0f)
								{
									pmod->fDuration += fGiveBack /fNumber;
								}
								break;
							case kAttribModShareAspect_Health:
								if(pmod->pFragility)
								{
									if (pfModShares[j] <= 1.0f)
									{
										pmod->pFragility->fHealth += fGiveBack /fNumber;
									}
								}
								break;
							}
						}
					}
					eaDestroy(&ppModsSharing);
					eafDestroy(&pfModShares);
				}
			}
			eaDestroy(&pchar->ppModsShare);
			PERFINFO_AUTO_STOP();
		}

		// Damage all the fragile mods with incoming damage
		//  NOTE: This will leave the mods with potentially unusual magnitude or duration
		//   because they have not been clamped yet.  Before the next accrue mods they
		//   need to have outgoing damage applied (which is why they are not clamped here),
		//   and then clamped.  This happens in TickPhaseThree.
		character_FragileModsDamage(iPartitionIdx,pchar,true);
	
		// Apply power shield
		if(pchar->fPowerShieldRatio > 0)
		{
			PERFINFO_AUTO_START("PowerShield",1);
			for(i=0; i<g_iDamageTypeCount; i++)
			{
				AttribType eType = i * sizeof(F32); // Cheap way to figure the Attribute
				F32 *pfBasic = F32PTR_OF_ATTRIB(&pAttrSet->CharacterAttribs.attrBasicAbs,eType);
				// The power shield
				if(*pfBasic > 0 && pchar->fPowerShieldRatio > 0)
				{
					F32 fPercentDamage = *pfBasic / pchar->pattrBasic->fHitPointsMax;
					F32 fPercentPower = pchar->pattrBasic->fPower / pchar->pattrBasic->fPowerMax;
					F32 fPercentPowerDamage = fPercentDamage / pchar->fPowerShieldRatio;
					if(fPercentPowerDamage >= fPercentPower)
					{
						fPercentPowerDamage = fPercentPower;
						fPercentDamage = fPercentPowerDamage * pchar->fPowerShieldRatio;
						pchar->fPowerShieldRatio = 0;
					}
					pchar->pattrBasic->fPower -= fPercentPowerDamage * pchar->pattrBasic->fPowerMax;
					*pfBasic -= fPercentDamage * pchar->pattrBasic->fHitPointsMax;
				}
			}
			PERFINFO_AUTO_STOP();
		}
	}


	if(pClass && eaiSize(&pClass->piAttribCurveBasic))
	{
		AttribType offAttrib = 0;
		F32 *pfBasicAbs = (F32*)&pAttrSet->CharacterAttribs.attrBasicAbs;
		F32 *pfBasicPos = (F32*)&pAttrSet->CharacterAttribs.attrBasicFactPos;
		F32 *pfBasicNeg = (F32*)&pAttrSet->CharacterAttribs.attrBasicFactNeg;
		AttribCurve **ppCurves = NULL;
		S32 c = 0;

		PERFINFO_AUTO_START("Apply AttribCurves",1);

		if(1)// JW: Commented out until I have more time to work on it: if(bDirtyAll)
		{
			int k;

			PERFINFO_AUTO_START("AttribCurvesAll",1);
			for(k=eaiSize(&pClass->piAttribCurveBasic)-1; k>=0; k--)
			{
				offAttrib = pClass->piAttribCurveBasic[k];
				i = ATTRIB_INDEX(offAttrib);
				ppCurves = class_GetAttribCurveArray(pClass,offAttrib);
				c = eaSize(&ppCurves);
				if(c && verify(ATTRIBASPECT_INDEX(kAttribAspect_BasicFactNeg)<c))
				{
					int j = ATTRIBASPECT_INDEX(kAttribAspect_BasicAbs);
					if(ppCurves[j])
					{
						*(pfBasicAbs + i) = character_ApplyAttribCurve(pchar,ppCurves[j],*(pfBasicAbs + i));
					}
					j = ATTRIBASPECT_INDEX(kAttribAspect_BasicFactPos);
					if(ppCurves[j])
					{
						*(pfBasicPos + i) = character_ApplyAttribCurve(pchar,ppCurves[j],*(pfBasicPos + i));
					}
					j = ATTRIBASPECT_INDEX(kAttribAspect_BasicFactNeg);
					if(ppCurves[j])
					{
						*(pfBasicNeg + i) = character_ApplyAttribCurve(pchar,ppCurves[j],*(pfBasicNeg + i));
					}
				}
			}
			PERFINFO_AUTO_STOP();
		}
		else if(0!=(s=eaiSize(&piDirtyAttribs)))
		{
			int k;
			PERFINFO_AUTO_START("AttribCurvesSubset",1);
			for(k=0; k<s; k++)
			{
				offAttrib = piDirtyAttribs[k];
				i = offAttrib / SIZE_OF_NORMAL_ATTRIB;
				ppCurves = class_GetAttribCurveArray(pClass,offAttrib);
				c = eaSize(&ppCurves);
				if(c && verify(ATTRIBASPECT_INDEX(kAttribAspect_BasicFactNeg)<c))
				{
					int j = ATTRIBASPECT_INDEX(kAttribAspect_BasicAbs);
					if(ppCurves[j])
					{
						*(pfBasicAbs + i) = character_ApplyAttribCurve(pchar,ppCurves[j],*(pfBasicAbs + i));
					}
					j = ATTRIBASPECT_INDEX(kAttribAspect_BasicFactPos);
					if(ppCurves[j])
					{
						*(pfBasicPos + i) = character_ApplyAttribCurve(pchar,ppCurves[j],*(pfBasicPos + i));
					}
					j = ATTRIBASPECT_INDEX(kAttribAspect_BasicFactNeg);
					if(ppCurves[j])
					{
						*(pfBasicNeg + i) = character_ApplyAttribCurve(pchar,ppCurves[j],*(pfBasicNeg + i));
					}
				}
			}
			PERFINFO_AUTO_STOP();
		}
		PERFINFO_AUTO_STOP();
	}

	pattrClass = character_GetClassAttribs(pchar,kClassAttribAspect_Basic);
	if(pattrClass)
	{
		F32 fHitPointsMaxBonus = 1.0f;
		F32 fHitPointsMaxOverride = FLT_MIN;
		F32 fPowerMaxOverride = FLT_MIN;

		if(eaSize(&pchar->modArray.ppOverrideMods))
		{
			// Find overrides to HitPointsMax and PowerMax, since we need to know them ahead of time
			//  to compile them correctly
			PERFINFO_AUTO_START("Override Pool Max",1);
			for(i=eaSize(&pchar->modArray.ppOverrideMods)-1;i>=0;i--)
			{
				AttribModDef *pmoddef = pchar->modArray.ppOverrideMods[i]->pDef;
				if(pmoddef->offAspect==kAttribAspect_BasicAbs)
				{
					AttribType offAttrib = ((AttribOverrideParams*)pmoddef->pParams)->offAttrib;
					if(offAttrib==kAttribType_HitPointsMax)
						fHitPointsMaxOverride = pchar->modArray.ppOverrideMods[i]->fMagnitude;
					else if(offAttrib==kAttribType_PowerMax)
						fPowerMaxOverride = pchar->modArray.ppOverrideMods[i]->fMagnitude;
				}
			}
			PERFINFO_AUTO_STOP();
		}


		// Compile accrued attributes in pools
		CharacterCompileAttribPools(pchar,
			&pAttrSet->CharacterAttribs.attrBasicAbs,
			&pAttrSet->CharacterAttribs.attrBasicFactPos,
			&pAttrSet->CharacterAttribs.attrBasicFactNeg,
			pattrClass);

		// Add in special BasicFactBonus "aspect" for HitPointsMax
		if(g_PowerTables.bBasicFactBonusHitPointsMax && pClass && pClass->iBasicFactBonusHitPointsMaxLevel)
		{
			S32 iBonusMaxLevel = pClass->iBasicFactBonusHitPointsMaxLevel;
			// If your LevelCombat is being controlled, adjust for that (STO-style)
			if(pchar->pLevelCombatControl)
			{
				S32 iLevelDelta = pchar->iLevelCombat - entity_GetSavedExpLevel(pchar->pEntParent);
				iBonusMaxLevel += iLevelDelta;
				MAX1(iBonusMaxLevel,1);
			}
			fHitPointsMaxBonus += class_powertable_Lookup(pClass,POWERTABLE_BASICFACTBONUSHITPOINTSMAX,iBonusMaxLevel-1);
		}

		// Compile the accrued mods into the character's actual attribs
		CompileBasicAttribs(pchar->pattrBasic,
			&pAttrSet->CharacterAttribs.attrBasicAbs,
			&pAttrSet->CharacterAttribs.attrBasicFactPos,
			&pAttrSet->CharacterAttribs.attrBasicFactNeg,
			pattrClass,
			fHitPointsMaxBonus,
			fHitPointsMaxOverride,
			fPowerMaxOverride,
			bBootstrapping);
	}

	if(eaSize(&pchar->modArray.ppOverrideMods))
	{
		PERFINFO_AUTO_START("Override General",1);
		for(i=eaSize(&pchar->modArray.ppOverrideMods)-1;i>=0;i--)
		{
			AttribModDef *pmoddef = pchar->modArray.ppOverrideMods[i]->pDef;
			if(pmoddef->offAspect==kAttribAspect_BasicAbs)
			{
				AttribType offAttrib = ((AttribOverrideParams*)pmoddef->pParams)->offAttrib;
				F32 *pfValue = (F32*)((char*)pchar->pattrBasic + offAttrib);
				if(offAttrib==kAttribType_HitPointsMax || offAttrib==kAttribType_PowerMax)
					continue;
				*pfValue = pchar->modArray.ppOverrideMods[i]->fMagnitude;
			}
		}
		PERFINFO_AUTO_STOP();
	}

	if(fRate > 0)
	{
		// Mark PowerShield mods for expiration if power is 0
		if(pchar->pattrBasic->fPower <= 0)
		{
			PERFINFO_AUTO_START("ExpirePowerShieldMods",1);
			for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
			{
				if(pchar->modArray.ppMods[i]->pDef->offAttrib==kAttribType_PowerShield)
				{
					character_ModExpireReason(pchar, pchar->modArray.ppMods[i], kModExpirationReason_Unset);
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				}
			}
			PERFINFO_AUTO_STOP();
		}
	}

	eaiDestroy(&piDirtyAttribs);
	StructDestroy(parse_AttribAccrualSet, pAttrSet);

	PERFINFO_AUTO_STOP();
#endif
}

