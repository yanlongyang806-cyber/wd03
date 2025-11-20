/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CombatPool.h"

#include "error.h"
#include "mathutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define CP_UNITS(val,units,min,max) ((units)==kCombatPoolUnit_Absolute ? (val) : ((min) + (val) * ((max) - (min))))

// Validates a CombatPool.  Flags indicates if the CombatPool has actual
//  max and target values.
int combatpool_Validate(CombatPoolDef *pdef,
						int bHasMaxValue,
						int bHasTargetValue)
{
	int bValid = true;

	if(!pdef->eInit)
	{
		bValid = false;	// Must have init point set
	}
	else if(pdef->eInit==kCombatPoolPoint_Target)
	{
		if(!pdef->pTarget)
		{
			bValid = false;	// Must have target data to init to the target
		}
	}

	if(!pdef->eBound)
	{
		bValid = false;	// Must have bounds rules set
	}

	if(pdef->pTarget)
	{
		if(!pdef->pTarget->ePoint)
		{
			bValid = false;	// Must have target point set
		}
		else if(pdef->pTarget->ePoint==kCombatPoolPoint_Target)
		{
			if(!(bHasTargetValue && pdef->pTarget->eUnitTarget))
			{
				bValid = false;	// Must have a target value and a set target unit
			}
		}
		else if(bHasTargetValue)
		{
			bValid = false;	// Target point doesn't use the target value
		}

		if(pdef->pTarget->eUnitTarget==kCombatPoolUnit_Percent
			|| pdef->pTarget->eUnitRegen==kCombatPoolUnit_Percent
			|| pdef->pTarget->eUnitDecay==kCombatPoolUnit_Percent)
		{
			if(!bHasMaxValue)
			{
				bValid = false;	// Can't calculate a percent unit without a max value
			}
		}


		if(!(pdef->pTarget->fTimeTick > 0))
		{
			bValid = false;	// Must take some positive time to tick
		}
	}

	return bValid;
}

// Returns the initial current value of a CombatPool
F32 combatpool_Init(CombatPoolDef *pdef,
					F32 fMin,
					F32 fMax,
					F32 fTar)
{
	switch(pdef->eInit)
	{
	case kCombatPoolPoint_Min:
		return fMin;
	case kCombatPoolPoint_Max:
		return fMax;
	case kCombatPoolPoint_Center:
		return fMin + ((fMax-fMin)/2.f);
	case kCombatPoolPoint_Target:
		if (pdef->pTarget)
			return CP_UNITS(fTar,pdef->pTarget->eUnitTarget,fMin,fMax);
		break;
	}
	Errorf("Bad CombatPool init");
	return 0;
}

// Returns the new current value of a CombatPool based on its old cur as well as min and max
F32 combatpool_Bound(CombatPoolDef *pdef,
					 F32 fCur,
					 F32 fMinOld,
					 F32 fMinNew,
					 F32 fMaxOld,
					 F32 fMaxNew)
{
	switch(pdef->eBound)
	{
	case kCombatPoolBound_None:
		return fCur;
	case kCombatPoolBound_Clamp:
		return CLAMP(fCur,fMinNew,fMaxNew);
	case kCombatPoolBound_Proportional:
		{
			F32 fRangeOld = fMaxOld - fMinOld;
			F32 fRangeNew = fMaxNew - fMinNew;
			if(fRangeOld > 0 && fRangeNew > 0)
			{
				F32 fPct = (fCur - fMinOld) / fRangeOld;
				return fPct * fRangeNew + fMinNew;
			}
			return fCur;
		}
	}
	Errorf("Bad CombatPool bound");
	return fCur;
}

// Evaluates the CombatPool's attempt to approach its target value through regen/decay.  
// returns true if it has reached the target
S32 combatpool_Tick(CombatPoolDef *pdef,
					F32 *pfTimer,
					F32 *pfCurInOut,
					F32 fRate,
					F32 fMin,
					F32 fMax,
					F32 fTar,
					F32 fScaleRegenFreq,
					F32 fScaleRegenMag,
					F32 fScaleDecayFreq,
					F32 fScaleDecayMag)
{
	F32 fTarAbs;
	
	switch(pdef->pTarget->ePoint)
	{
	case kCombatPoolPoint_Min:
		fTarAbs = fMin;
		break;
	case kCombatPoolPoint_Max:
		fTarAbs = fMax;
		break;
	case kCombatPoolPoint_Center:
		fTarAbs = fMin + ((fMax-fMin)/2.f);
		break;
	default:
		fTarAbs = CP_UNITS(fTar,pdef->pTarget->eUnitTarget,fMin,fMax);
	}
	
	if(*pfCurInOut <= fTarAbs)
	{
		*pfTimer -= fRate * MAX(fScaleRegenFreq,0);
	}
	else
	{
		*pfTimer -= fRate * MAX(fScaleDecayFreq,0);
	}

	if(*pfTimer <= 0 && pdef->pTarget->fTimeTick > 0)
	{
		F32 fMag;
		F32 fCount = 1 + floor(-(*pfTimer) / pdef->pTarget->fTimeTick); // How many ticks we actually apply
		
		*pfTimer += fCount * pdef->pTarget->fTimeTick;

		if(*pfCurInOut <= fTarAbs)
		{
			F32 fDelta = fTarAbs - *pfCurInOut;
			fMag = CP_UNITS(pdef->pTarget->fMagRegen*fScaleRegenMag,pdef->pTarget->eUnitRegen,fMin,fMax);
			fMag *= fCount;
			MIN1(fMag,fDelta);
			*pfCurInOut += fMag;
		}
		else
		{
			F32 fDelta = *pfCurInOut - fTarAbs;
			fMag = CP_UNITS(pdef->pTarget->fMagDecay*fScaleDecayMag,pdef->pTarget->eUnitDecay,fMin,fMax);
			fMag *= fCount;
			MIN1(fMag,fDelta);
			*pfCurInOut -= fMag;
		}
	}
	
	return (*pfCurInOut == fTarAbs);
}

#include "AutoGen/CombatPool_h_ast.c"