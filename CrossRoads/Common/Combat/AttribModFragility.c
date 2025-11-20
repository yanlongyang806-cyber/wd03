/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "AttribModFragility.h"
#include "AttribModFragility_h_ast.h"

#include "Entity.h"
#include "EString.h"
#include "file.h"
#include "GlobalTypes.h"
#include "ResourceManager.h"

#include "AttribCurve.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterAttribsMinimal_h_ast.h"
#include "DamageTracker.h"
#include "Powers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static DictionaryHandle s_hFragileScaleSetDict;

// Allows an incoming or outgoing effect to change the health of an AttribMod
//  pmod->pFragility assumed to be valid
static void FragileModAffect(SA_PARAM_NN_VALID AttribMod *pmod, F32 fMag)
{
	AttribModDef *pdef = pmod->pDef;
	ModDefFragility *pFragilityDef = pdef->pFragility;

	if(verify(pFragilityDef))
	{
		// Affect the mod (gets clamped in character_FragileModsFinalizeHealth())
		pmod->pFragility->fHealth += fMag;

		// Fragile mods without a type, or of the Shield attribute don't actually affect their mag/dur
		if(pdef->eType!=kModType_None && pdef->offAttrib!=kAttribType_Shield)
		{
			// Set mag to % of max health, and scale by proportion
			fMag /= pmod->pFragility->fHealthMax;
			fMag *= pFragilityDef->fProportion;

			if(pdef->eType&kModType_Duration)
			{
				pmod->fDuration += fMag * pmod->fDurationOriginal;
			}

			if(pdef->eType&kModType_Magnitude)
			{
				pmod->fMagnitude += fMag * pmod->fMagnitudeOriginal;
			}
		}
	}
}

// Directly applies the magnitude to the fragile AttribMod's health, with the appropriate side-effect.
//  This should almost never be called directly.
void mod_FragileAffect(AttribMod *pmod, F32 fMagnitude)
{
	if(pmod->pFragility)
	{
		FragileModAffect(pmod,fMagnitude);
	}
}

// Returns the scale applied to damage of a particular type for a fragile AttribMod
F32 character_FragileModScale(int iPartitionIdx, Character *pchar, AttribMod *pmod, AttribType eDamageType, int bIncoming)
{
	F32 fReturn = 1;
	if(pmod->pDef->pFragility)
	{
		FragileScaleSet *pScale = bIncoming ? GET_REF(pmod->pDef->pFragility->hScaleIn) : GET_REF(pmod->pDef->pFragility->hScaleOut);
		if(pScale)
		{
			fReturn = IS_NORMAL_ATTRIB(eDamageType) ? *F32PTR_OF_ATTRIB(pScale->pattrScale,eDamageType) : pScale->fScaleDefault;
		}
	}

	if(fReturn!=0)
	{
		int i;
		F32 fModFragilityScale = 1;
		// Check the list of AttribModFragilityScale AttribMods
		for(i=eaSize(&pchar->ppModsAttribModFragilityScale)-1; i>=0; i--)
		{
			AttribMod *pmodScale = pchar->ppModsAttribModFragilityScale[i];
			AttribModDef *pdefScale = pmodScale->pDef;
			AttribModFragilityScaleParams *pparams = (AttribModFragilityScaleParams*)pdefScale->pParams;
			F32 fScaleBase = pmodScale->fMagnitude;
			FragileScaleSet *pScale = bIncoming ? GET_REF(pparams->hScaleIn) : GET_REF(pparams->hScaleOut);

			if(pScale)
			{
				F32 fScaleBySet = IS_NORMAL_ATTRIB(eDamageType) ? *F32PTR_OF_ATTRIB(pScale->pattrScale,eDamageType) : pScale->fScaleDefault;

				// See if the ScaleSet lets the scale through at all
				if(fScaleBySet==0)
					continue;

				fScaleBase *= fScaleBySet;
			}

			// If the resulting scale is 1, just continue
			if(fScaleBase==1)
				continue;

			// Made sure this will have an effect on the scale before we do the affects check,
			//  since the affects check is probably the most expensive step, if it exists.
			if(!moddef_AffectsModOrPowerChk(iPartitionIdx,pmodScale->pDef,pchar,pmodScale,pmod->pDef,pmod,pmod->pDef->pPowerDef))
				continue;

			fModFragilityScale *= fScaleBase;
		}

		// Assume that we only want to apply the AttribCurve if the result is going to decrease the scale
		//  In that case, invert it and subtract 1 (to put it into "factor" space), apply the curve, and add 1 and invert back
		if(fModFragilityScale > 0 && fModFragilityScale < 1)
		{
			F32 fInvert = 1 / fModFragilityScale;
			fInvert = 1 + character_AttribCurve(pchar,kAttribType_AttribModFragilityScale,kAttribAspect_BasicAbs,fInvert-1,NULL);
			fModFragilityScale = 1 / fInvert;
		}

		fReturn *= fModFragilityScale;
	}

	return fReturn;
}

static void character_FragileModsDamageInternal(int iPartitionIdx, Character *pchar, AttribMod **ppMods, int bIncoming, int bShields)
{
	int s = eaSize(&ppMods);
	if(s)
	{
		DamageTracker **ppTrackers = bIncoming ? pchar->ppDamageTrackersTickIncoming : pchar->ppDamageTrackersTickOutgoing;
		int t = eaSize(&ppTrackers);
		if(t)
		{
			int i;
			EntityRef er = entGetRef(pchar->pEntParent);

			PERFINFO_AUTO_START_FUNC();

			for(i=0; i<s; i++)
			{
				AttribMod *pmod = ppMods[i];
				AttribModDef *pdef = pmod->pDef;
				ModDefFragility *pFragilityDef = pdef->pFragility;

				if(pmod->pFragility && pFragilityDef)
				{
					int j;
					F32 fScale;
					U32 uiApplyIDDiscard = !pFragilityDef->bFragileToSameApply ? pmod->uiApplyID : 0;

					for(j=0; j<t; j++)
					{
						F32 fDamage;
						DamageTracker *pTracker = ppTrackers[j];
						PowerDef *ppowdefTracker = GET_REF(pTracker->hPower);

						if(bIncoming)
						{
							// Skip damaging fragile attrib mods if the attacker is the same 
							//  as the victim.  Only applies on incoming damage trackers.
							if(pTracker->erSource == er)
								continue;

							// If only the source is allowed to damage it
							if(pFragilityDef->bSourceOnlyIn
								&& pmod->erSource != pTracker->erSource)
								continue;

							fDamage = pFragilityDef->bUseResistIn ? pTracker->fDamage : pTracker->fDamageNoResist;
						}
						else
						{
							// If only the source being damaged can damage it
							if(pFragilityDef->bSourceOnlyOut
								&& pmod->erSource != pTracker->erTarget)
								continue;

							fDamage = pFragilityDef->bUseResistOut ? pTracker->fDamage : pTracker->fDamageNoResist;
						}

						// Don't take damage from specific ApplyID
						if(uiApplyIDDiscard && uiApplyIDDiscard==pTracker->uiApplyID)
							continue;

						// Check for excluded PowerTags
						if (ppowdefTracker)
						{
							int u=eaiSize(&pFragilityDef->tagsExclude.piTags);
							if(u)
							{
								if(eaUSize(&ppowdefTracker->ppOrderedMods) > pTracker->uiDefIdx)
								{
									AttribModDef *pmoddefTracker = ppowdefTracker->ppOrderedMods[pTracker->uiDefIdx];
									int k=u-1;
									for(; k>=0; k--)
									{
										if(powertags_Check(&pmoddefTracker->tags,pFragilityDef->tagsExclude.piTags[k]))
											break;
									}
									if(k>=0)
										continue;
								}
							}
						}

						// Check if this tracker only damages certain Attribs
						if(pTracker->puiFragileTypes)
						{
							int k;
							for(k=ea32Size(&pTracker->puiFragileTypes)-1; k>=0; k--)
							{
								if(attrib_Matches(pdef->offAttrib,pTracker->puiFragileTypes[k]))
								{
									break;
								}
							}

							if(k<0)
								continue;
						}
						else if(bShields && !(ppowdefTracker && ppowdefTracker->bIgnoreShieldCheck && bIncoming))
						{
							continue;
						}

						// Scale the damage
						fScale = character_FragileModScale(iPartitionIdx,pchar,pmod,pTracker->eDamageType,bIncoming);
						fDamage *= fScale;

						if(fDamage)
						{
							FragileModAffect(pmod,-fDamage);
						}
					}
				}
			}
			PERFINFO_AUTO_STOP();
		}
	}
}

// Applies incoming or outgoing damage to the character's list of fragile AttribMods
void character_FragileModsDamage(int iPartitionIdx, Character *pchar, int bIncoming)
{
	// Damage fragile mods
	character_FragileModsDamageInternal(iPartitionIdx, pchar, pchar->modArray.ppFragileMods, bIncoming, false);
	// Damage shield mods
	character_FragileModsDamageInternal(iPartitionIdx, pchar, pchar->ppModsShield, bIncoming, true);
}

// Updates the fragile mod's fHealthMax, which may proportionally change the fHealth.
static int FragileModUpdateHealthMax(int iPartitionIdx, SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_OP_VALID AttribMod **ppmodsHealth)
{
	if(pmod->pFragility)
	{
		F32 fHealthMaxCur = pmod->pFragility->fHealthMax;
		F32 fHealthMaxNew = pmod->pFragility->fHealthOriginal;
		
		if(eaSize(&ppmodsHealth))
		{
			// Perform a standard aspect-based accrual
			int i;
			F32 fBasicAbs=0, fBasicFactPos=0, fBasicFactNeg = 0;
			for(i=eaSize(&ppmodsHealth)-1; i>=0; i--)
			{
				AttribMod *pmodHealth = ppmodsHealth[i];
				AttribModDef *pdefHealth = pmodHealth->pDef;
				if(moddef_AffectsModOrPowerChk(iPartitionIdx,pdefHealth,NULL,pmodHealth,pmod->pDef,pmod,pmod->pDef->pPowerDef))
				{
					switch(pdefHealth->offAspect)
					{
					case kAttribAspect_BasicAbs:
						fBasicAbs += pmodHealth->fMagnitude;
						break;
					case kAttribAspect_BasicFactPos:
						fBasicFactPos += pmodHealth->fMagnitude;
						break;
					case kAttribAspect_BasicFactNeg:
						fBasicFactNeg += pmodHealth->fMagnitude;
						break;
					}
				}
			}

			fHealthMaxNew = fBasicAbs + pmod->pFragility->fHealthOriginal * (1 + fBasicFactPos) / (1 + fBasicFactNeg);
		}

		// If we now have a different max health
		if(fHealthMaxNew!=fHealthMaxCur)
		{
			F32 fRatio = fHealthMaxNew / fHealthMaxCur;
			pmod->pFragility->fHealth *= fRatio;
			pmod->pFragility->fHealthMax = fHealthMaxNew;
			return true;
		}
	}
	return false;
}

// Clamps the fragile mod into the proper health range.
//  Also will expire if at 0 health and not marked to stay alive.
static int FragileModClamp(SA_PARAM_OP_VALID Character *pchar, SA_PARAM_NN_VALID AttribMod *pmod)
{
	if(pmod->pFragility)
	{
		ModFragility *pFragility = pmod->pFragility;

		if(!FINITE(pFragility->fHealth))
		{
			PowerDef* pPowerDef = GET_REF(pmod->hPowerDef);
			AttribModDef* pModDef = pmod->pDef;
			const char* pchPowerName = NULL_TO_EMPTY(SAFE_MEMBER(pPowerDef, pchName));
			const char* pchModName = StaticDefineIntRevLookup(AttribTypeEnum,pModDef->offAttrib);
			ErrorDetailsf("Attrib %s, ModIdx %d, Power %s", pchModName, pmod->uiDefIdx, pchPowerName);
			devassert(FINITE(pFragility->fHealth));
			pFragility->fHealth = 0;
		}
		if(pFragility->fHealth <= 0.0f)
		{
			ModDefFragility *pFragilityDef = pmod->pDef->pFragility;

			// If it's just supposed to die now, expire it
			//  Also set magnitude to 0 for safety
			if(pFragilityDef && pFragilityDef->bUnkillable)
			{
				if(pFragility->fHealthMax > 0)
				{
					// Revive the unkillable mod
					FragileModAffect(pmod,-1.f * pFragility->fHealth);

					// Copy of duration expiration test we do on regularly alive mods, since this has been revived
					if(pmod->fDuration<0 && pmod->pDef->eType&kModType_Duration && !mod_ExpireIsValid(pmod))
						character_ModExpireReason(pchar, pmod, kModExpirationReason_Duration);
				}
				else
				{
					// Some jerk managed to drop the fHealthMax to 0 on an unkillable AttribMod.  Fatality!
					ErrorFilenamef(pmod->pDef->pPowerDef->pchFile,"Fragile AttribMod marked as Unkillable, but had its max health reduced to 0");
					character_ModExpireReason(pchar, pmod, kModExpirationReason_FragileDeath);
					pmod->fMagnitude = 0.0f;
				}
			}
			else
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_FragileDeath);
				pmod->fMagnitude = 0.0f;
			}
			return true;
		}
		else
		{
			int changed = false;

			// Make sure health is in line
			if(pFragility->fHealth > pFragility->fHealthMax)
			{
				FragileModAffect(pmod,pFragility->fHealthMax - pFragility->fHealth);
				changed = true;
			}

			// It's a fragile mod, that is still alive, but its duration is below 0, it's type is
			//  duration, and it hasn't been given an expiration reason.  This almost certainly means
			//  it took damage that knocked its duration down below zero this tick.  This counts as
			//  expiration due to duration (since it's still alive), so mark it as such.
			if(pmod->fDuration<0 && pmod->pDef->eType&kModType_Duration && !mod_ExpireIsValid(pmod))
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Duration);
				changed = true;
			}
			return changed;
		}
	}
	return false;
}

// Updates the max health, clamps and expires all the AttribMods in the the earray (which should be fragile).
void character_FragileModsFinalizeHealth(int iPartitionIdx, Character *pchar, AttribMod **ppmods)
{
	int i,s=eaSize(&ppmods);
	if(s)
	{
		PERFINFO_AUTO_START_FUNC();
		for(i=s-1; i>=0; i--)
		{
			int changed = false;

			changed |= FragileModUpdateHealthMax(iPartitionIdx,ppmods[i],pchar->ppModsAttribModFragilityHealth);
			changed |= FragileModClamp(pchar, ppmods[i]);

			if(changed)
			{
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}
		}
		PERFINFO_AUTO_STOP();
	}
}

// Validates an AttribModDef's fragility data, returns true if valid
int moddef_ValidateFragility(AttribModDef *pmoddef, PowerDef *ppowdef)
{
	int bValid = true;
	
	if(!pmoddef->pFragility)
		return bValid;

	if(pmoddef->pFragility->bMagnitudeIsHealth
		&& pmoddef->pFragility->pExprHealth)
	{
		ErrorFilenamef(ppowdef->pchFile,"%s: %d: AttribMod has MagnitudeIsHealth and a Health Expression",ppowdef->pchName,pmoddef->uiDefIdx);
		bValid = false;
	}

	if(pmoddef->pFragility->bReplaceKeepsHealth)
	{
		if(pmoddef->eStack!=kStackType_Replace)
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: AttribMod Fragility has ReplaceKeepsHealth but is not stack type Replace",ppowdef->pchName,pmoddef->uiDefIdx);
			bValid = false;
		}

		if(pmoddef->pFragility->bFragileWhileDelayed)
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: AttribMod Fragility can not be both ReplaceKeepsHealth and FragileWhileDelayed",ppowdef->pchName,pmoddef->uiDefIdx);
			bValid = false;
		}
	}

	// TODO(JW): Validation: Validate health expression

	if(!combatpool_Validate(&pmoddef->pFragility->pool,true,false))
	{
		bValid = false;
	}

	return bValid;
}

AUTO_FIXUPFUNC;
TextParserResult FragileScaleSetFixup(FragileScaleSet *pSet, enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch (eFixupType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			int i,s;
			pSet->pattrScale = StructAlloc(parse_CharacterAttribs);
			for(i=0; IS_NORMAL_ATTRIB(i); i+=SIZE_OF_NORMAL_ATTRIB)
			{
				*F32PTR_OF_ATTRIB(pSet->pattrScale,i) = pSet->fScaleDefault;
			}

			// HACK(JW): Too clever by half - make all fragile scale sets immune to HitPoints by default.
			//  This means that by default heals and other direct HitPoint changes don't do anything to fragile mods.
			*F32PTR_OF_ATTRIB(pSet->pattrScale,kAttribType_HitPoints) = 0;

			s=eaSize(&pSet->ppScales);
			for(i=0; i<s; i++)
			{
				FragileScale *pScale = pSet->ppScales[i];
				if(IS_SPECIAL_ATTRIB(pScale->offAttrib))
				{
					AttribType *pUnroll = attrib_Unroll(pScale->offAttrib);
					if(pUnroll)
					{
						int j;
						for(j=eaiSize(&pUnroll)-1; j>=0; j--)
						{
							*F32PTR_OF_ATTRIB(pSet->pattrScale,pUnroll[j]) = pScale->fScale;
						}
					}
					else
					{
						Errorf("Non-set special attrib in FragileScaleSet %s",pSet->pchName);
						bRet = false;
					}
				}
				else
				{
					*F32PTR_OF_ATTRIB(pSet->pattrScale,pScale->offAttrib) = pScale->fScale;
				}
			}
		}
		break;
	}

	return bRet;
}


// Loads the FragileScaleSet def file
void fragileScaleSets_Load(void)
{
	s_hFragileScaleSetDict = RefSystem_RegisterSelfDefiningDictionary("FragileScaleSet",false,parse_FragileScaleSet,true,true,NULL);

	resLoadResourcesFromDisk(s_hFragileScaleSetDict, NULL, "defs/config/FragileScaleSets.def", "FragileScaleSets.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if (IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(s_hFragileScaleSetDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
}

