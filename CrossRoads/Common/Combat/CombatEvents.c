/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatEvents.h"
#include "CombatEvents_h_ast.h"

#include "Character.h"
#include "CharacterAttribs.h"
#include "Character_combat.h"
#include "CombatEval.h"
#include "CombatConfig.h"
#include "PowerApplication.h"
#include "PowerEnhancements.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
#endif

#include "Entity.h"
#include "rand.h"
#include "timing.h"


#define INVALID_ANGLE_TO_SOURCE	(-42)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

U8 g_ubCombatEventComplexWhitelist[] = {
	0,//kCombatEvent_ActivateSelf,
	0,//kCombatEvent_ActivateInOther,
	0,//kCombatEvent_ActivateOutOther,
	kCombatEvent_AttribDamageIn,
	kCombatEvent_AttribDamageOut,
	kCombatEvent_AttribHealIn,
	kCombatEvent_AttribHealOut,
	0,//kCombatEvent_AttribPowerEmptied,
	kCombatEvent_BlockInTimed,
	0,//kCombatEvent_BloodiedStart,
	0,//kCombatEvent_CombatModeActIn,
	0,//kCombatEvent_CombatModeActOut,
	0,//kCombatEvent_CombatModeStart,
	0,//kCombatEvent_CombatModeStop,
	kCombatEvent_CriticalIn,
	kCombatEvent_CriticalInTimed,
	kCombatEvent_CriticalOut,
	kCombatEvent_CriticalOutTimed,
	kCombatEvent_DamageIn,
	kCombatEvent_DamageOut,
	0,//kCombatEvent_DisabledStart,
	0,//kCombatEvent_DisabledStop,
	kCombatEvent_DodgeIn,
	kCombatEvent_DodgeInTimed,
	kCombatEvent_DodgeOut,
	kCombatEvent_DodgeOutTimed,
	kCombatEvent_HealIn,
	kCombatEvent_HealOut,
	0,//kCombatEvent_HeldStart,
	0,//kCombatEvent_HeldStop,
	0,//kCombatEvent_InteractStart,
	kCombatEvent_KillIn,
	kCombatEvent_KillOut,
	0,//kCombatEvent_KnockIn,
	kCombatEvent_MissIn,
	kCombatEvent_MissInTimed,
	kCombatEvent_MissOut,
	kCombatEvent_MissOutTimed,
	kCombatEvent_PlacateIn,
	kCombatEvent_PlacateOut,
	0,//kCombatEvent_PowerMode,
	kCombatEvent_PowerRecharged,
	kCombatEvent_AttemptRepelIn,
	kCombatEvent_AttemptRepelOut,
	0,//kCombatEvent_RootedStart,
	0,//kCombatEvent_RootedStop,
	kCombatEvent_PowerChargeGained,
	kCombatEvent_NearDeathDead
};

STATIC_ASSERT(ARRAY_SIZE(g_ubCombatEventComplexWhitelist) == kCombatEvent_Count);

// Creates a CombatEventState structure
CombatEventState* combatEventState_Create(void)
{
	return StructCreate(parse_CombatEventState);
}

// Utility function for the very common case of tracking an event between the target (pcharIn) and another Entity (pentOut).
//  Because this can call the complex tracking, it takes all the same arguments, which may end up making
//  it too heavy to use.
void character_CombatEventTrackInOut(Character *pcharIn,
									 CombatEvent eEventIn,
									 CombatEvent eEventOut,
									 Entity *pentOut,
									 PowerDef *pPowerDef,
									 AttribModDef *pAttribModDef,
									 F32 fMag,
									 F32 fMagPreResist,
									 const Vec3 vSourcePosIn,
									 const Vec3 vSourcePosOut)
{
	if(pcharIn && character_CombatEventTrack(pcharIn,eEventIn))
		character_CombatEventTrackComplex(pcharIn,eEventIn,pentOut,pPowerDef,pAttribModDef,fMag,fMagPreResist, vSourcePosIn);

	if(pentOut && pentOut->pChar && pcharIn != pentOut->pChar)
	{
		if(character_CombatEventTrack(pentOut->pChar,eEventOut))
		{
			character_CombatEventTrackComplex(pentOut->pChar, eEventOut, (pcharIn?pcharIn->pEntParent:NULL),
												pPowerDef, pAttribModDef, fMag, fMagPreResist, vSourcePosOut);
		}
	}
}

// Adds full tracking for an event to the Character's CombatEvent trackers.  Should
//  only be called if there is a TriggerComplex AttribMod watching for the event.
void character_CombatEventTrackComplex(Character *pchar,
									   CombatEvent eEvent,
									   Entity *pentOther,
									   PowerDef *pPowerDef,
									   AttribModDef *pAttribModDef,
									   F32 fMag,
									   F32 fMagPreResist,
									   const Vec3 vSourcePos)
{
	if(pchar->pCombatEventState && pchar->pCombatEventState->abCombatEventTriggerComplex[eEvent])
	{
		CombatEventTracker *pTracker = StructCreate(parse_CombatEventTracker);
		devassert(g_ubCombatEventComplexWhitelist[eEvent]);
		pTracker->eEvent = eEvent;
		if(pentOther)
			pTracker->erOther = entGetRef(pentOther);
		pTracker->pPowerDef = pPowerDef;
		pTracker->pAttribModDef = pAttribModDef;
		pTracker->fMag = fMag;
		pTracker->fMagPreResist = fMagPreResist;
		
		if (vSourcePos && !vec3IsZero(vSourcePos))
		{
			pTracker->fAngleToSource = mod_AngleToSourcePosUtil(pchar->pEntParent, vSourcePos);
		}
		else
		{
			pTracker->fAngleToSource = INVALID_ANGLE_TO_SOURCE;
		}

		eaPush(&pchar->pCombatEventState->ppEvents,pTracker);
	}
}

// Returns true if the AttribModDef in question is allowed to affect the CombatTrackerEvent, target mod and/or power.
//  This is basically just a copy of moddef_AffectsModOrPower(), but with the extra param for the CombatTrackerEvent,
//  which none of the normal use cases use.
static int ModDefAffectsTriggerEvent(int iPartitionIdx,
									 AttribModDef *pmoddefAffects,
									 Character *pchar,
									 AttribMod *pmodAffects,
									 AttribModDef *pmoddefTarget,
									 AttribMod *pmodTarget,
									 PowerDef *ppowdefTarget,
									 CombatEventTracker *pTriggerEvent)
{
	int bRet = true;
	if(pmoddefAffects->pExprAffects)
	{
		PERFINFO_AUTO_START_FUNC();
		combateval_ContextSetupAffects(pchar,pmodAffects,ppowdefTarget,pmoddefTarget,pmodTarget,pTriggerEvent);
		bRet = (0.f!=combateval_EvalNew(iPartitionIdx,pmoddefAffects->pExprAffects,kCombatEvalContext_Affects,NULL));
		PERFINFO_AUTO_STOP();
	}
	return bRet;
}



// Takes the current CombatEvents array and copies it to the regular array, and updates
//  the timestamp array as appropriate.  Also handles triggering any TriggerComplex mods.
void character_FinalizeCombatEvents(int iPartitionIdx, Character *pchar, U32 uiTimestamp, GameAccountDataExtract *pExtract)
{
	if(pchar->pCombatEventState)
	{
		S32 i,j;
		CombatEventState *pState = pchar->pCombatEventState;
		
		PERFINFO_AUTO_START_FUNC();
		
		memcpy(pState->auiCombatEventCount,pState->auiCombatEventCountCurrent,sizeof(pState->auiCombatEventCount));
		memset(pState->auiCombatEventCountCurrent,0,sizeof(pState->auiCombatEventCountCurrent));
		for(i=0; i<kCombatEvent_Count; i++)
		{
			if(pState->auiCombatEventCount[i])
				pState->auiCombatEventTimestamp[i] = uiTimestamp;
		}

		if(pState->ppEvents)
		{
			CombatEventTracker **ppEvents = NULL;

			PERFINFO_AUTO_START("CombatEventsComplex",1);
			// Processes all the fully tracked events, but first copy them to a temporary earray
			//  so that any new ones that get generated during the processing get saved.  They may
			//  not have any effect (since they won't get processed until the next tick), but at
			//  least they'll have a chance.
			eaCopy(&ppEvents,&pState->ppEvents);
			eaDestroy(&pState->ppEvents);
			for(i=eaSize(&ppEvents)-1; i>=0; i--)
			{
				CombatEventTracker *pEvent = ppEvents[i];
				// This could be slow, consider optimizing if it's a problem
				for(j=eaSize(&pchar->modArray.ppMods)-1; j>=0; j--)
				{
					AttribMod *pmod = pchar->modArray.ppMods[j];
					AttribModDef *pmoddef = pmod->pDef;
					if(!pmod->bIgnored && pmoddef->offAttrib==kAttribType_TriggerComplex && pmoddef->offAspect==kAttribAspect_BasicAbs)
					{
						TriggerComplexParams *pParams = (TriggerComplexParams*)pmoddef->pParams;
						if(pParams && eaiFind(&pParams->piCombatEvents,pEvent->eEvent)!=-1)
						{
							// This AttribMod is a trigger for this Event, so do that
							S32 bFail = false;
							Character *pcharTargetType = mod_GetApplyTargetTypeCharacter(iPartitionIdx,pmod,&bFail);
							if(!bFail
								&& pParams
								&& (!pParams->pExprChance || character_TriggerAttribModCheckChance(iPartitionIdx, pchar, pmod->erOwner, pParams->pExprChance))
								&& (!pParams->bMagnitudeIsCharges || pmod->fMagnitude >= 1)
								&& (!pmoddef->fArcAffects || 
										(pEvent->fAngleToSource != INVALID_ANGLE_TO_SOURCE && moddef_AffectsModFromAngleToSource(pmoddef, pchar, pEvent->fAngleToSource)))
								&& ModDefAffectsTriggerEvent(iPartitionIdx,pmod->pDef,pchar,pmod,pEvent->pAttribModDef,NULL,pEvent->pPowerDef,pEvent))
							{
								PowerDef *pdef = GET_REF(pParams->hDef);
								if(pdef && pmod->pSourceDetails)
								{
									Entity *pentSource;
									PATrigger trigger = {0};
									EntityRef erSource = entGetRef(pchar->pEntParent); // kTriggerComplexEntity_ModTarget
									EntityRef erTarget = erSource; // kTriggerComplexEntity_ModTarget
									ApplyUnownedPowerDefParams applyParams = {0};
									static Power **s_eaPowEnhancements = NULL;

									if(pParams->eSource==kTriggerComplexEntity_ModSource)
										erSource = pmod->erSource;
									else if(pParams->eSource==kTriggerComplexEntity_ModOwner)
										erSource = pmod->erOwner;
									else if(pParams->eSource==kTriggerComplexEntity_EventOther)
										erSource = pEvent->erOther;

									if(pParams->eTarget==kTriggerComplexEntity_ModSource)
										erTarget = pmod->erSource;
									else if(pParams->eTarget==kTriggerComplexEntity_ModOwner)
										erTarget = pmod->erOwner;
									else if(pParams->eTarget==kTriggerComplexEntity_EventOther)
										erTarget = pEvent->erOther;

									pentSource = entFromEntityRef(iPartitionIdx,erSource);
									if(!(pentSource && pentSource->pChar))
										continue;

									trigger.pCombatEventTracker = pEvent;

									applyParams.pmod = pmod;
									applyParams.erTarget = erTarget;
									applyParams.pcharSourceTargetType = pcharTargetType;
									applyParams.pclass = GET_REF(pmod->pSourceDetails->hClass);
									applyParams.iLevel = pmod->pSourceDetails->iLevel;
									applyParams.iIdxMulti = pmod->pSourceDetails->iIdxMulti;
									applyParams.fTableScale = pmod->pSourceDetails->fTableScale;
									applyParams.iSrcItemID = pmod->pSourceDetails->iItemID;
									applyParams.bLevelAdjusting = pmod->pSourceDetails->bLevelAdjusting;
									applyParams.ppStrengths = pmod->ppApplyStrengths;
									applyParams.pCritical = pmod->pSourceDetails->pCritical;
									applyParams.erModOwner = pmod->erOwner;
									applyParams.uiApplyID = pmod->uiApplyID;
									applyParams.fHue = pmod->fHue;
									applyParams.pTrigger = &trigger;
									applyParams.pExtract = pExtract;
									applyParams.bCountModsAsPostApplied = true;

									if(pmod->erOwner)
									{
										Entity *pModOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
										if (pModOwner && pModOwner->pChar)
										{
											power_GetEnhancementsForAttribModApplyPower(iPartitionIdx, pModOwner->pChar, 
																						pmod, EEnhancedAttribList_DEFAULT, 
																						pdef, &s_eaPowEnhancements);
											applyParams.pppowEnhancements = s_eaPowEnhancements;
										}
									}

									character_ApplyUnownedPowerDef(iPartitionIdx, pentSource->pChar, pdef, &applyParams);
									eaClear(&s_eaPowEnhancements);
									
									// Decrement charges, and expire if necessary
									if(pParams->bMagnitudeIsCharges)
									{
										pmod->fMagnitude -= 1;
										if(pmod->fMagnitude < 1)
										{
											character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
										}
									}
								}
							}
						}
					}
				}
			}

			eaDestroyStruct(&ppEvents,parse_CombatEventTracker);
			PERFINFO_AUTO_STOP();
		}
	
		PERFINFO_AUTO_STOP();
	}
}

// Takes a list of CombatEvents and a time in seconds, which is how far back in time to 
//  look for the CombatEvents.  If 0 or less, will only look for CombatEvents within the
//  last combat tick.
S32 character_CheckCombatEvents(Character *pchar,
								int *piEvents,
								F32 fTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	S32 bEvent = false;
	if(pchar->pCombatEventState)
	{
		CombatEventState *pState = pchar->pCombatEventState;
		if(fTime > 0)
		{
			// Do a full time check
			int i;
			U32 uiTimeCheck = pmTimestampFrom(pmTimestamp(0),-fTime);
			for(i=eaiSize(&piEvents)-1; i>=0; i--)
			{
				if(pState->auiCombatEventTimestamp[piEvents[i]] >= uiTimeCheck)
				{
					bEvent = true;
					break;
				}
			}
		}
		else
		{
			// Do a last tick check
			int i;
			for(i=eaiSize(&piEvents)-1; i>=0; i--)
			{
				if(pState->auiCombatEventCount[piEvents[i]])
				{
					bEvent = true;
					break;
				}
			}
		}
	}
	return bEvent;
#endif
}

// Takes a list of CombatEvents, and finds the number of said events that occurred during
//  the last combat tick.  If the max events is non-zero, will break after finding that
//  many events.
S32 character_CountCombatEvents(Character *pchar,
								int *piEvents,
								S32 iMaxEvents)
{
	S32 iEvents = 0, i;
	if(pchar->pCombatEventState)
	{
		CombatEventState *pState = pchar->pCombatEventState;
		for(i=eaiSize(&piEvents)-1; i>=0; i--)
		{
			iEvents += pState->auiCombatEventCount[piEvents[i]];
			if(iMaxEvents && iEvents >= iMaxEvents)
			{
				iEvents = iMaxEvents;
				break;
			}
		}
	}
	return iEvents;
}


#include "AutoGen/CombatEvents_h_ast.c"
