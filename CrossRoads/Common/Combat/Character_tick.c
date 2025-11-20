/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character_tick.h"
#include "ControlScheme.h"
#include "cmdparse.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"
#include "EntityMovementGrab.h"
#include "EntityMovementTactical.h"
#include "GameAccountDataCommon.h"
#include "ItemArt.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "interaction_common.h"
#include "logging.h"
#include "mapstate_common.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "rand.h"
#include "StringCache.h"
#include "TriCube/vec.h"

#ifdef GAMESERVER
#include "aiFCStruct.h"
#include "aiLib.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoTransDefs.h"
#include "cmdServerCharacter.h"
#include "CombatDebug.h"
#include "GameServerLib.h"
#include "gslArmamentSwap.h"
#include "gslCombatAdvantage.h"
#include "gslCombatDeathPrediction.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslEventSend.h"
#include "gslInteraction.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslPowerTransactions.h"
#include "mechanics_common.h"
#include "PowersAutoDesc.h"
#include "team.h"
#include "itemServer.h"
#include "aiLib.h"
#endif

#ifdef GAMECLIENT
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "ClientTargeting.h"
#include "gclControlScheme.h"
#include "cmdClient.h"
#include "ComboTrackerUI.h"
#include "gclCombatDeathPrediction.h"
#endif

#include "AttribModFragility.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterAttribs.h"
#include "CharacterAttribsMinimal_h_ast.h"
#include "Character_combat.h"
#include "Character_mods.h"
#include "Character_target.h"
#include "Character_tick.h"
#include "CharacterClass.h"
#include "CombatCallbacks.h"
#include "CombatReactivePower.h"
#include "CombatEval.h"
#include "CombatConfig.h"
#include "DamageTracker.h"
#include "DamageTracker_h_ast.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerModes.h"
#include "CombatPowerStateSwitching.h"
#include "PowerModes_h_ast.h"
#include "PowersMovement.h"
#include "PowerSlots.h"
#include "PowerSubtarget.h"
#include "PowerTree.h"
#include "PowerVars.h"
#include "EntityMovementTactical.h"
#include "PowerHelpers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););



// Globally available track of how long it's been since the last combat tick
F32 g_fCharacterTickTime = 0.0f;


static ECharacterPhase s_eCharacterTickPhase = ECharacterPhase_NONE;

void characterPhase_SetPhase(ECharacterPhase ePhase)
{
	s_eCharacterTickPhase = ePhase;
}

ECharacterPhase characterPhase_GetCurrentPhase()
{
	return s_eCharacterTickPhase;
}


void character_AccumulateTickTime(	F32 deltaSeconds,
									F32* secondsToApplyOut)
{
	g_fCharacterTickTime += deltaSeconds;
	if(g_fCharacterTickTime >= gConf.combatUpdateTimer)
	{
		globMovementLog("[character.time] Accumulated a full tick %1.3f/%1.3f (frame delta %1.3f).",
						g_fCharacterTickTime,
						gConf.combatUpdateTimer,
						deltaSeconds);

		*secondsToApplyOut = g_fCharacterTickTime;
		g_fCharacterTickTime = 0.f;
	}
	else
	{
		globMovementLog("[character.time] Accumulated a partial tick %1.3f/%1.3f (frame delta %1.3f).",
						g_fCharacterTickTime,
						gConf.combatUpdateTimer,
						deltaSeconds);

		*secondsToApplyOut = 0.f;
	}
}
			   
// Takes the input time and rounds it up to the next combat tick.  This helps
//  ensure that tick-based events have consistent times between server and
//  client
F32 character_TickRound(F32 fTime)
{
	return gConf.combatUpdateTimer * ceil(fTime/gConf.combatUpdateTimer);
}

// Processes the character's combat timer, and removes them from combat if needed
static void CharacterTickCombat(int iPartitionIdx, Character *p, F32 fRate, GameAccountDataExtract *pExtract)
{
#ifdef GAMESERVER
	if (g_CombatConfig.pTimer)
	{
		Entity *pAggroingEntity = NULL;

		// If we are aggrod or have aggro on us, enter combat
		if (g_CombatConfig.pTimer->fTimerAggro > 0.f && 
			aiIsOrHasLegalTarget(p->pEntParent, g_CombatConfig.pTimer->fTimerAggroDistance, g_CombatConfig.pTimer->bDisallowPlayerTeamAggro, &pAggroingEntity))
		{			
			character_SetCombatExitTime(p, g_CombatConfig.pTimer->fTimerAggro, false, true, pAggroingEntity, NULL);
		}
		

		// If the tactical aim timer is defined and the entity is aiming
		// turn on the combat visuals
		if (g_CombatConfig.pTimer->fTimerTacticalAim > 0.f && entIsAiming(p->pEntParent))
		{
			character_SetCombatVisualsExitTime(p, g_CombatConfig.pTimer->fTimerTacticalAim);
		}
	}

#endif
	

	if(p->uiTimeCombatExit)
	{
		F32 fUntil = 0.0f;

		PERFINFO_AUTO_START_FUNC();

		fUntil = pmTimeUntil(p->uiTimeCombatExit);
		if(fUntil<=0.0f)
		{
			// Remove the character from combat
			p->uiTimeCombatExit = 0;
			entity_SetDirtyBit(p->pEntParent, parse_Character, p, false);
			character_CombatEventTrack(p,kCombatEvent_CombatModeStop);
			pmUpdateTacticalRunParams(p->pEntParent, p->pattrBasic->fSpeedRunning, false);
			if (p->uiTimeCombatVisualsExit == 0)
			{
				pmUpdateCombatAnimBit(p->pEntParent, false);
#ifdef GAMESERVER
				entity_UpdateItemArtAnimFX(p->pEntParent);
#endif
			}

#ifdef GAMESERVER
			if(p->bAutoAttackServer && !entCheckFlag(p->pEntParent,ENTITYFLAG_IS_PLAYER))
			{
				p->bAutoAttackServer = false;
			}
			if (g_CombatConfig.bSwitchCapsulesInCombat && p->pEntParent->pPlayer)
			{
				// AI Handles this itself
				mmCollisionSetHandleDestroyFG(&p->pEntParent->mm.mcsHandle);
			}
			if (g_CombatConfig.bCollideWithPetsInCombat && p->pEntParent->pPlayer)
			{
				mmCollisionBitsHandleDestroyFG(&p->pEntParent->mm.mcbHandle);
				mmCollisionBitsHandleCreateFG(p->pEntParent->mm.movement, &p->pEntParent->mm.mcbHandle, __FILE__, __LINE__, ~MCG_PLAYER_PET);
			}
#endif
			if (g_CombatConfig.tactical.bTacticalDisableDuringCombat)
			{
				// Enable the tactical movement again since we're out of combat			
				mrTacticalNotifyPowersStop(p->pEntParent->mm.mrTactical, TACTICAL_COMBATDISABLE_UID, pmTimestamp(0));
			}

			// Reset cooldown timers
			character_ResetCooldownTimersOnExitCombat(p);
		}
		else
		{
			//Else we're still in combat, make sure to keep ourself active.
			entSetActive(p->pEntParent);

			// for players, we need to wake up to refresh the uiTimeCombatExit before it will expire
			// if we don't check well before the client is told to exit combat, 
			// it's possible the client exists combat before the server.
			// This causes prediction issues, such as character_ResetCooldownTimersOnExitCombat()
			if (entIsPlayer(p->pEntParent))
				fUntil -= 0.5f;

			character_SetSleep(p,fUntil);
		}

		PERFINFO_AUTO_STOP();
	}

	// The same checks as above, but just for visuals
	if(p->uiTimeCombatVisualsExit)
	{
		F32 fUntil = 0.0f;

		fUntil = pmTimeUntil(p->uiTimeCombatVisualsExit);
		if(fUntil<=0.0f)
		{
			// Turn off combat visuals
			p->uiTimeCombatVisualsExit = 0;
			entity_SetDirtyBit(p->pEntParent, parse_Character, p, false);

			if (p->uiTimeCombatExit == 0)
			{
				pmUpdateCombatAnimBit(p->pEntParent, false);
#ifdef GAMESERVER
				entity_UpdateItemArtAnimFX(p->pEntParent);
#endif
			}
		}
	}

#ifdef GAMESERVER
	contact_MaybeBeginNotInCombatContact(p->pEntParent);
#endif
}

// Processes the character's cool down list and global cooldown timer
static void CharacterTickCooldown(Character *pchar, F32 fRate)
{
	int i,s;

	if(pchar->fCooldownGlobalTimer > 0)
	{
		pchar->fCooldownGlobalTimer -= fRate;
		MAX1(pchar->fCooldownGlobalTimer,0);
		if(pchar->fCooldownGlobalTimer > 0)
			character_SetSleep(pchar,pchar->fCooldownGlobalTimer);
	}

	s = eaSize(&pchar->ppCooldownTimers);

	if(!s) return;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&pchar->ppCooldownTimers)-1;i>=0;i--)
	{
		CooldownTimer *pTimer = pchar->ppCooldownTimers[i];
		
		if(pTimer->fCooldown > 0)
		{
			CooldownRateModifier* pCooldownModifier = eaIndexedGetUsingInt(&pchar->ppSpeedCooldown, pTimer->iPowerCategory);
			F32 fRateMod;
			
			if (!pCooldownModifier && pchar->pInnateAccrualSet)
			{
				pCooldownModifier = eaIndexedGetUsingInt(&pchar->pInnateAccrualSet->ppSpeedCooldown, pTimer->iPowerCategory);
			}
			fRateMod = pCooldownModifier ? pCooldownModifier->fValue : (pchar->pattrBasic ? pchar->pattrBasic->fSpeedCooldown : 1.0f);
			if (fRateMod <= 0.0f)
			{
				fRateMod = 1.0f;
			}
			pchar->ppCooldownTimers[i]->fCooldown -= fRate * fRateMod;

			if(pTimer->fCooldown <= 0)
			{
				eaRemove(&pchar->ppCooldownTimers,i);
				cooldowntimer_Free(pTimer);
			}
			else
			{
				character_SetSleep(pchar,pTimer->fCooldown);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

// Processes the character's charge refill list
static void CharacterTickChargeRefill(int iPartitionIdx, Character *pchar, F32 fRate)
{
	S32 i;
	FOR_EACH_IN_EARRAY(pchar->ppPowerRefChargeRefill, PowerRef, pRef)
	{
		PowerDef *pdef;
		Power *ppow = character_FindPowerByRef(pchar, pRef);
		Power *pPowerTreePower = NULL;

		i = FOR_EACH_IDX(pchar->ppPowerRefChargeRefill, pRef);

		if(ppow == NULL)
		{
			ppow = character_FindPowerByIDComplete(pchar, pRef->uiID);
		}

		pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		if (pdef)
		{
			// In the future we might need a special attrib mod 
			// which might affect this speed or use kAttribType_SpeedRecharge
			F32 fSpeed = 1.f;

#if defined(GAMECLIENT) || defined(GAMESERVER) // So we don't have to include mapstate_common in appserver and logparser
			fSpeed *= mapState_SpeedRecharge(mapState_FromPartitionIdx(iPartitionIdx));
#endif
			ppow->fTimeChargeRefill -= fRate * fSpeed;

			if(ppow->fTimeChargeRefill <= 0.0f)
			{
				// Decrement the number of charges used
				power_SetChargesUsed(pchar, ppow, pdef, power_GetChargesUsed(ppow) - 1);
				
			}
			else
			{
				character_SetSleep(pchar, ppow->fTimeChargeRefill / fSpeed);
			}
		}
		else if(!ppow)
		{
			// Character doesn't seem to own the Power anymore!
			powerref_Destroy(pRef);
			eaRemoveFast(&pchar->ppPowerRefChargeRefill, i);
		}
	}
	FOR_EACH_END
}

// Processes the character's recharge list
void CharacterTickRecharge(int iPartitionIdx, Character *pchar, F32 fRate, U32 uiTimeLoggedOut)
{
	int i,s;
	
	s = eaSize(&pchar->ppPowerRefRecharge);

	if(!s) return;

	PERFINFO_AUTO_START_FUNC();

	for(i=s-1; i>=0; i--)
	{
		PowerDef *pdef;
		Power *ppow = character_FindPowerByRef(pchar,pchar->ppPowerRefRecharge[i]);

		if(!ppow)
			ppow = character_FindPowerByIDComplete(pchar,pchar->ppPowerRefRecharge[i]->uiID);

		pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		if(pdef && !pdef->bRechargeDisabled)
		{
			// Decrement timer first, then check.  Yes, order matters.  Do we care that the Power
			//  is removed from the list as soon as it hits 0, rather than the next tick?  Seems
			//  reasonable to me.

			F32 fSpeed = power_GetSpeedRecharge(iPartitionIdx,pchar,ppow,pdef);
			if(fSpeed<=0)
			{
				fSpeed = 1;
			}
#if defined(GAMECLIENT) || defined(GAMESERVER) // So we don't have to include mapstate_common in appserver and logparser
			fSpeed *= mapState_SpeedRecharge(mapState_FromPartitionIdx(iPartitionIdx));
#endif
			ppow->fTimeRecharge -= fRate * fSpeed;

			if (pdef->bRechargeWhileOffline && uiTimeLoggedOut > 0)
				ppow->fTimeRecharge -= uiTimeLoggedOut * fSpeed;

			if(ppow->fTimeRecharge <= 0.0f)
			{
				ppow->fTimeRecharge = 0.0f;

#ifdef GAMESERVER
				if(g_funcNotifyPowerRechargedCallback)
					g_funcNotifyPowerRechargedCallback(pchar->pEntParent, ppow);
#endif

				powerref_Destroy(pchar->ppPowerRefRecharge[i]);
				eaRemoveFast(&pchar->ppPowerRefRecharge,i);
				if(character_CombatEventTrack(pchar,kCombatEvent_PowerRecharged))
					character_CombatEventTrackComplex(pchar,kCombatEvent_PowerRecharged,NULL,pdef,NULL,0,0, NULL);
				PowersDebugPrintEnt(EPowerDebugFlags_POWERS, pchar->pEntParent, "Recharge: %s\n",POWERNAME(ppow));
			}
			else
			{
				character_SetSleep(pchar,ppow->fTimeRecharge/fSpeed);
			}
		}
		else if(!ppow)
		{
			// Character doesn't seem to own the Power anymore!
			powerref_Destroy(pchar->ppPowerRefRecharge[i]);
			eaRemoveFast(&pchar->ppPowerRefRecharge,i);
		}
	}

	PERFINFO_AUTO_STOP();
}

// Processes the character's passive list
static void CharacterTickPassive(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	int i,s;
	
	// Don't bother with this on the client anymore, it's not going to be predictable
	//  and there's no interaction involved anyway
	if(!entIsServer()) return;

	s = eaSize(&pchar->ppPowerActPassive);
	pchar->bAutoReapplyPassives = false;

	if(!s) return;

	PERFINFO_AUTO_START_FUNC();

	for(i=s-1; i>=0; i--)
	{
		PowerActivation *pact = pchar->ppPowerActPassive[i];
		Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		// Check for BecomeCritter restriction
		// Check for slotting (passives can't be in combos, so just checking the base is safe)
		if(ppow 
			&& pdef
			&& !(pchar->bBecomeCritter && ppow->eSource!=kPowerSource_AttribMod)
			&& (!pdef->bSlottingRequired || character_PowerIDSlotted(pchar,ppow->uiID))
			&& pdef->eType == kPowerType_Passive)
		{
			F32 fSpeed = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow);

			// Check timer first, then decrement.  Yes, order matters.
			if(pact->fTimerActivate <= 0.0f)
			{
				Vec3 vecTarget;

				entGetCombatPosDir(pchar->pEntParent,pact,vecTarget,NULL);
				pact->uiPeriod++;
				pact->uiTimestampActivatePeriodic = pmTimestamp(0);
				character_ApplyPower(iPartitionIdx,pchar,ppow,pact,entGetRef(pchar->pEntParent),vecTarget,NULL,false,0,pExtract,NULL);

				// Set instead of add.  Doing this to maintain consistency between the duration of the
				//  attrib mods and the activation period; if mod duration is the same as the period, there
				//  shouldn't be any problems with gaps.
				if(pdef->fTimeActivatePeriod > 0)
					pact->fTimerActivate = pdef->fTimeActivatePeriod;
				else
					pact->fTimerActivate = POWERS_FOREVER; // Never automatically apply the next period
			}

			if(pact->fTimerActivate!=POWERS_FOREVER)
				pact->fTimerActivate -= fRate * fSpeed;

			character_SetSleep(pchar,pact->fTimerActivate / fSpeed);

			if(pdef->fLifetimeUsage)
			{
				ppow->fLifetimeUsageUsed += fRate; // Do we want to use the modified rate for this? Is lifetime usage affected by SpeedPeriod? Probably not
				pchar->bLimitedUseDirty = true;
				character_SetSleep(pchar,power_GetLifetimeUsageLeft(ppow));
			}

			if(pdef->bAutoReapply)
				pchar->bAutoReapplyPassives = true;
		}
		else
		{
			// Character doesn't seem to own the Power anymore, or it's otherwise unavailable
			character_DeactivatePassive(iPartitionIdx,pchar,pact);
		}
	}

	PERFINFO_AUTO_STOP();
}

// Processes the character's toggle list
//  TODO(JW): Activate: Better timestamps for these events
static void CharacterTickToggle(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	int i,s;
	bool bIsAlive;

	s = eaSize(&pchar->ppPowerActToggle);
	pchar->bAutoReapplyToggles = false;

	if(!s) return;

	// Don't bother with this on the client anymore, it's not going to be predictable
	//  and there's no interaction involved anyway
	if(!entIsServer()) 
	{
		//HACK: increment the maintained timer so that it can be displayed on the UI
		for(i=s-1; i>=0; i--)
		{
			PowerActivation *pact = pchar->ppPowerActToggle[i];
			Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
			PowerDef *ppowdef = GET_REF(pact->hdef);
			F32 fSpeed = ppow ? character_GetSpeedPeriod(iPartitionIdx, pchar, ppow) : 1;

			pact->fTimeMaintained += fRate * fSpeed;

			if(ppowdef)
			{
				pact->fTimeMaintained = MIN(ppowdef->fTimeMaintain, pact->fTimeMaintained);
			}
		}
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	bIsAlive = entIsAlive(pchar->pEntParent);

	for(i=s-1; i>=0; i--)
	{
		PowerActivation *pact = pchar->ppPowerActToggle[i];
		Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
		int bValid = false;
		if(ppow)
		{
			Entity *eTarget = entFromEntityRef(iPartitionIdx,pact->erTarget);
			PowerDef *pdef = GET_REF(ppow->hDef);
			bValid = pdef && character_CanActivatePowerDef(pchar,pdef,false,true,NULL,pExtract,NULL) && !pact->bDeactivate;

			if(pdef->eType != kPowerType_Toggle)
				bValid = false;

			if ((!bIsAlive || !(pdef->eActivateRules & kPowerActivateRules_SourceAlive)) &&
			    (bIsAlive || !(pdef->eActivateRules & kPowerActivateRules_SourceDead)))
				bValid = false;

			// Only check for events during the tick (so it doesn't prevent activation)
			if(bValid
				&& pdef->piCombatEvents
				&& character_CheckCombatEvents(pchar,pdef->piCombatEvents,pdef->fCombatEventTime))
			{
				bValid = false;
			}

			// If this toggle has a character effect area, check to see if the target is still valid
			if(bValid && pdef->eEffectArea == kEffectArea_Character)
			{
				PowerTarget *pPowerTarget = GET_REF(pdef->hTargetMain);
				if(eTarget && pPowerTarget 
					&& (!character_TargetMatchesPowerType(iPartitionIdx,pchar,eTarget->pChar,pPowerTarget)
						|| !character_CanPerceive(iPartitionIdx,pchar,eTarget->pChar)))
				{
					bValid = false;
				}
			}

			// Check if the target is not in the firing arc anymore
			if(bValid
				&& pdef->fTargetArc
				&& !(eTarget
					&& entity_TargetInArc(pchar->pEntParent,eTarget,NULL,RAD(pdef->fTargetArc),ppow->fYaw)))
			{
				bValid = false;
			}

			// Check for slotting
			if(bValid
				&& power_SlottingRequired(ppow)
				&& !character_PowerSlotted(pchar,ppow))
			{
				bValid = false;
			}

			// Check for a HitChanceOneTime Power that has applied once and didn't hit, and we've
			//  got OneTimeCancelOnMiss enabled
			if(bValid
				&& pdef->bHitChanceOneTime
				&& !pact->bHitPrior
				&& pact->uiPeriod >= 1
				&& g_CombatConfig.pHitChance
				&& g_CombatConfig.pHitChance->bOneTimeCancelOnMiss)
			{
				bValid = false;
			}


			// Check timer first, then decrement.  Yes, order matters.
			if(bValid && pact->fTimerActivate <= 0.0f)
			{
				Vec3 vecTarget;
				if(eTarget)
				{
					entGetCombatPosDir(eTarget,NULL,vecTarget,NULL);
				}
				else
				{
					zeroVec3(vecTarget);
				}

				// Check the period limit
				if(!pdef->uiPeriodsMax || pact->uiPeriod < pdef->uiPeriodsMax)
				{
					int bPaid;

					// Increase the period count
					pact->uiPeriod++;

					bPaid = character_PayPowerCost(iPartitionIdx,pchar,ppow,pact->erTarget,pact,true,pExtract);

					if(bPaid)
					{
						S32 bApplied;
						S32 bPlayActivate = pact->uiPeriod ? true : !pact->bPlayedActivate;
						pact->uiTimestampActivatePeriodic = pmTimestamp(0); 
						bApplied = character_ApplyPower(iPartitionIdx,pchar,ppow,pact,pact->erTarget,vecTarget,
														pact->vecTargetSecondary,bPlayActivate,0,pExtract,NULL);
						if (!bApplied && pdef->pExprRequiresApply)
						{
							bValid = false;
						}
						pact->bPlayedActivate = true;

						// Set instead of add.  Doing this to maintain consistency between the duration of the
						//  attrib mods and the activation period; if mod duration is the same as the period, there
						//  shouldn't be any problems with gaps.
						if(pdef->fTimeActivatePeriod > 0)
							pact->fTimerActivate = pdef->fTimeActivatePeriod;
						else
							pact->fTimerActivate = POWERS_FOREVER; // Never automatically apply the next period
					}
					else
					{
						ActivationFailureParams failureParams = { 0 };
						failureParams.pEnt = pchar->pEntParent;
						failureParams.pPow = ppow;
						character_ActivationFailureFeedback(kActivationFailureReason_Cost, &failureParams);
						bValid = false;
					}
				}
				else
				{
					bValid = false;
				}
			}

			// Still good, make sure it's marked active
			if(bValid)
			{
				F32 fModifiedRate = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow) * fRate;

				power_SetActive(ppow,true);

				pact->fTimeMaintained = MIN(pdef->fTimeMaintain, pact->fTimeMaintained + fModifiedRate);

				if(pact->fTimerActivate!=POWERS_FOREVER)
					pact->fTimerActivate -= fModifiedRate;

				// TODO(JW): Because Toggles recheck their validity every combat tick, we can't
				//  sleep while you've got one on, and that is bad.  We'd really prefer to sleep
				//  until the next period needs to happen, but that could be a long while.  Maybe
				//  we should just take the minimum of the time to the next period and something
				//  reasonable like 1 second.
				character_Wake(pchar);
				
				if(pdef->fLifetimeUsage)
				{
					ppow->fLifetimeUsageUsed += fRate;
					pchar->bLimitedUseDirty = true;
					character_SetSleep(pchar,power_GetLifetimeUsageLeft(ppow));
				}

				if(pdef->bAutoReapply)
					pchar->bAutoReapplyToggles = true;
			}
		}

		if(!bValid)
		{
			// If we've gone invalid in the first period of a 0-activate time toggle, let the bits/fx
			//  linger until the end of the first period, rather than shutting off immediately.
			if(!pact->fTimeActivating && pact->uiPeriod==1)
				character_DeactivateToggle(iPartitionIdx,pchar,pact,ppow,pmTimestamp(pact->fTimerActivate),true);
			else
				character_DeactivateToggle(iPartitionIdx,pchar,pact,ppow,pmTimestamp(0),true);
		}
	}

	PERFINFO_AUTO_STOP();
}


// Processes the character's toggle autoattack list
static void CharacterTickAutoAttackServer(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	int i,s;

	if(!entIsServer())
		return;

	PERFINFO_AUTO_START_FUNC();

	s = eaSize(&pchar->ppPowerActAutoAttackServer);

	// If we want server AutoAttack running, we may need to check for new AutoAttacks
	if(pchar->bAutoAttackServer && pchar->bAutoAttackServerCheck)
	{
		for(i=s-1; i>=0; i--)
		{
			PowerActivation *pact = pchar->ppPowerActAutoAttackServer[i];
			Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
			if(!ppow)
				break;
		}

		if(i<0)
		{
			// No "undead" activations, check if we're missing any AutoAttack powers
			pchar->bAutoAttackServerCheck = false;

			for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
			{
				PowerDef *pdef = GET_REF(pchar->ppPowers[i]->hDef);
				if(pdef && pdef->bAutoAttackServer)
				{
					int j;
					for(j=eaSize(&pchar->ppPowerActAutoAttackServer)-1; j>=0; j--)
					{
						if(pchar->ppPowerActAutoAttackServer[j]->ref.uiID==pchar->ppPowers[i]->uiID)
							break;
					}

					if(j<0)
					{
						PowerActivation *pact = poweract_Create();
						poweract_SetPower(pact,pchar->ppPowers[i]);
						eaPush(&pchar->ppPowerActAutoAttackServer, pact);
						character_EnterStance(iPartitionIdx,pchar,pchar->ppPowers[i],pact,false,pmTimestamp(0));
					}
				}
			}

			s = eaSize(&pchar->ppPowerActAutoAttackServer);
		}
	}

	for(i=s-1; i>=0; i--)
	{
		PowerActivation *pact = pchar->ppPowerActAutoAttackServer[i];
		F32 fModifiedRate = fRate;

		if(pact && pact->fTimerActivate <= 0 && !pchar->pPowActCurrent && 
			(!pchar->pPowActFinished || pchar->pPowActFinished->fTimeFinished >= g_CombatConfig.autoAttack.fServerFinishDelay))
		{
			// Finished timer
			//  If the Power is gone or autoattack is off, destroy the activation
			//  Otherwise attempt to attack
			Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
			if(!ppow || !pchar->bAutoAttackServer)
			{
				PowerDef *pdef = GET_REF(pact->hdef);
				PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
				if(pafx)
				{
					U32 uiID = powerref_AnimFXID(&pact->ref);
					character_ExitStance(pchar,pafx,uiID,pmTimestamp(0));
				}
				poweract_Destroy(pact);
				eaRemoveFast(&pchar->ppPowerActAutoAttackServer,i);
				continue;
			}
			else
			{
				PowerDef *pdef = GET_REF(ppow->hDef);
				Entity *eTarget = character_GetTarget(iPartitionIdx,pchar);
				S32 bTargetValid = false;

				fModifiedRate = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow) * fRate;

				if(eTarget
					&& eTarget->pChar
					&& pdef
					&& character_CanActivatePowerDef(pchar,pdef,false,false,NULL,pExtract,NULL))
				{
					PowerTarget *pPowerTarget = GET_REF(pdef->hTargetMain);
					if(character_TargetMatchesPowerType(iPartitionIdx,pchar,eTarget->pChar,pPowerTarget))
					{
						bTargetValid = true;
					}
					else
					{
						// Try implied assist, similar to clientTarget_SelectBestTargetForPower()
						if(pchar->pEntParent->pPlayer && pchar->pEntParent->pPlayer->pUI)
						{
							ControlScheme *pScheme = schemes_FindCurrentScheme(pchar->pEntParent->pPlayer->pUI->pSchemes);
							if(pScheme
								&& pScheme->bAssistTargetIfInvalid
								&& (!g_CombatConfig.bAssistChecksTargetSelfFirst
									|| !pPowerTarget->bAllowSelf))
							{
								eTarget = entity_GetTarget(eTarget);
								if(eTarget && eTarget->pChar && character_TargetMatchesPowerType(iPartitionIdx,pchar,eTarget->pChar,pPowerTarget))
								{
									bTargetValid = true;
								}
							}
						}
					}

					// Check affectability
					if(bTargetValid)
						bTargetValid = entity_CanAffect(iPartitionIdx,pchar->pEntParent,eTarget);

					// Check targetability
					if(bTargetValid)
						bTargetValid = eTarget==pchar->pEntParent || !entCheckFlag(eTarget, ENTITYFLAG_UNTARGETABLE);

					// Check LoS
					if(bTargetValid)
					{
						Vec3 vecSource,vecTarget;
						entGetCombatPosDir(pchar->pEntParent,NULL,vecSource,NULL);
						entGetCombatPosDir(eTarget,NULL,vecTarget,NULL);
						bTargetValid = combat_CheckLoS(iPartitionIdx,vecSource,vecTarget,pchar->pEntParent,eTarget,NULL,false,false,NULL);
					}

					if(bTargetValid)
					{
						Vec3 vecTarget;
						entGetCombatPosDir(eTarget,NULL,vecTarget,NULL);
						pact->uchID++;
						if(!pact->uchID) pact->uchID = 1;
						pact->uiTimestampActivate = pmTimestamp(0); 
						pact->uiTimestampActivatePeriodic = pact->uiTimestampActivate;
						pact->uiSeedSBLORN = randomU32();
						character_ApplyPower(iPartitionIdx,pchar,ppow,pact,pchar->currentTargetRef,vecTarget,NULL,true,0,pExtract,NULL);

						// Add to keep perfect rate over time
						pact->fTimerActivate += pdef->fTimeRecharge;
						
						// Make sure that the result timer is at least .5s + fModifiedRate, so we're assured that
						//  the attacks are spaced out
						if(pact->fTimerActivate < .5f+fModifiedRate)
							pact->fTimerActivate = .5f+fModifiedRate;
					}
				}
			}
		}
		pact->fTimerActivate -= fModifiedRate;
	}

	PERFINFO_AUTO_STOP();
}


// Processes all the Character's Powers.  Due to ordering issues this
//  probably gives Powers one less tick than they deserve, but I don't
//  think that's a big deal.
static __forceinline void CharacterTickLifetime(SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	int i;
	U32* puiExpiredPowerIDs = NULL;

	if(!entIsServer() || !eaSize(&pchar->ppPowersLimitedUse))
		return;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&pchar->ppPowersLimitedUse)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowersLimitedUse[i];
		PowerDef *pdef = GET_REF(ppow->hDef);

		if(ppow->bExpirationPending)
		{
			continue;
		}
		if(pdef && pdef->bLimitedUse)
		{
			if(pdef->fLifetimeGame)
			{
				ppow->fLifetimeGameUsed += fRate;
				pchar->bLimitedUseDirty = true;
				character_SetSleep(pchar,power_GetLifetimeGameLeft(ppow));
			}
			
			if(pdef->fLifetimeReal)
			{
				character_SetSleep(pchar,power_GetLifetimeRealLeft(ppow));
			}

			if(power_IsExpired(ppow, true))
			{
				if(pdef->iCharges
					&& 0!=power_GetLifetimeRealLeft(ppow)
					&& 0!=power_GetLifetimeGameLeft(ppow)
					&& 0!=power_GetLifetimeUsageLeft(ppow))
				{
					// It has charges, and none of the lifetimes are expired, so
					//  it's expiring due to charges.  In that case, we still don't
					//  expire it unless it's inactive
					if(!!ppow->bActive)
						continue;
				}
				// Defer power expiration to prevent the possibility of the 
				// limited use powers array changing while iterating through it
				eaiPush(&puiExpiredPowerIDs, ppow->uiID);
			}
		}
	}

	// Do power expiration
	for (i = eaiSize(&puiExpiredPowerIDs)-1; i >= 0; i--)
	{
#ifdef GAMESERVER
		character_PowerExpire(pchar,puiExpiredPowerIDs[i],pExtract);
#endif
	}
	eaiDestroy(&puiExpiredPowerIDs);
	PERFINFO_AUTO_STOP();
}

// Processes the character's most recently finished power activation	
static void CharacterTickFinished(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate)
{
	if(pchar->pPowActFinished)
	{
		PERFINFO_AUTO_START_FUNC();

		pchar->pPowActFinished->fTimeFinished += fRate;

		// Clear the finished/stance if we're still in one and haven't activated anything for a while
		{
			PowerActivation *pact = pchar->pPowActFinished;
			PowerDef *pdef = GET_REF(pact->hdef);
			const F32 fStanceAdjustTime = 1.0f;
			F32 fStanceLingerTime = MAXF(g_CombatConfig.fStanceLingerTime-fStanceAdjustTime, 0.0f);

			if((pchar->pPowerRefStance || GET_REF(pchar->hPowerDefStanceDefault))
				&& !pchar->pPowActCurrent 
				&& !pchar->pPowActQueued 
				&& pact->fTimeFinished > fStanceLingerTime)
			{
				// Calculate the stance time, and add extra time because of discrepancies between client and server ticks
				F32 fTimeCharge = SAFE_MEMBER(pdef, fTimeCharge);
				F32 fTimePreactivate = SAFE_MEMBER(pdef, fTimePreactivate);
				F32 fTimeActivate = SAFE_MEMBER(pdef, fTimeActivate);
				F32 fStanceTime = fStanceLingerTime + fTimeCharge + fTimePreactivate + fTimeActivate + fStanceAdjustTime;
				character_EnterStance(iPartitionIdx,pchar,NULL,NULL,true,pmTimestampFrom(pact->uiTimestampCurrented,fStanceTime));
				character_ActFinishedDestroy(pchar);
			}
			else if (fStanceLingerTime-pact->fTimeFinished > 0.f)
			{
				character_SetSleep(pchar,fStanceLingerTime-pact->fTimeFinished);
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

static void CharacterTickTactical(SA_PARAM_NN_VALID Character *pchar)
{
	if(g_CombatConfig.tactical.roll.eRollCostAttrib && IS_NORMAL_ATTRIB(g_CombatConfig.tactical.roll.eRollCostAttrib))
	{
		F32 fRollCostAttrib = *F32PTR_OF_ATTRIB(pchar->pattrBasic, g_CombatConfig.tactical.roll.eRollCostAttrib);
		TacticalDisableFlags flags = TDF_ROLL;

		if (g_CombatConfig.tactical.roll.eRollCostAttrib == g_CombatConfig.tactical.aim.eAimCostAttrib)
		{
			flags |= TDF_AIM;
		}

		if (fRollCostAttrib <= 0.f)
		{
			if (!pchar->bTacticalRollDisabledByCost)
			{
				pchar->bTacticalRollDisabledByCost = true;
				if (flags & TDF_AIM) 
					pchar->bTacticalAimDisabledByCost = true;
				mrTacticalNotifyPowersStart(pchar->pEntParent->mm.mrTactical, 
											TACTICAL_COSTDISABLE_UID, 
											flags, 
											pmTimestamp(0));
			}
			
		}
		else if (pchar->bTacticalRollDisabledByCost)
		{
			pchar->bTacticalRollDisabledByCost = false;
			if (flags & TDF_AIM) 
				pchar->bTacticalAimDisabledByCost = false;
			mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical, 
										TACTICAL_COSTDISABLE_UID, 
										pmTimestamp(0));
		}
	}

	if(g_CombatConfig.tactical.aim.eAimCostAttrib && IS_NORMAL_ATTRIB(g_CombatConfig.tactical.aim.eAimCostAttrib) &&
		g_CombatConfig.tactical.aim.eAimCostAttrib != g_CombatConfig.tactical.roll.eRollCostAttrib)
	{
		F32 fAimCostAttrib = *F32PTR_OF_ATTRIB(pchar->pattrBasic, g_CombatConfig.tactical.aim.eAimCostAttrib);

		if (fAimCostAttrib <= 0.f)
		{
			if (!pchar->bTacticalAimDisabledByCost)
			{
				pchar->bTacticalAimDisabledByCost = true;
				mrTacticalNotifyPowersStart(pchar->pEntParent->mm.mrTactical, 
											TACTICAL_COSTDISABLE_UID, 
											TDF_AIM, 
											pmTimestamp(0));
			}

		}
		else if (pchar->bTacticalAimDisabledByCost)
		{
			pchar->bTacticalAimDisabledByCost = false;
			mrTacticalNotifyPowersStop(	pchar->pEntParent->mm.mrTactical, 
										TACTICAL_COSTDISABLE_UID, 
										pmTimestamp(0));
		}
	}
}



#ifdef GAMECLIENT 
U32 CharacterActivatePowerClient_GetInputDirectionBits();

static int _ShouldCancelQueuedPowers_IsTargetDeathPredicted(Entity *e, PowerActivation *pact)
{
	Vec3 vecSourcePos, vecSourceDir, vecTarget;
	Entity *pentTarget = NULL;
	PowerDef *pDef = GET_REF(pact->hdef);
					
	if (!pDef || !powerdef_hasCategory(pDef, g_CombatConfig.autoAttack.piPowerCategoriesCanceledByPredictedDeath))
		return false;

	entGetCombatPosDir(e, pact, vecSourcePos, vecSourceDir);
	
	if (pDef->eEffectArea == kEffectArea_Character && 
		character_ActFindTarget(PARTITION_CLIENT, e->pChar, pact, vecSourcePos, vecSourceDir, &pentTarget, vecTarget))
	{
		if (pentTarget && pentTarget->pChar &&
			gclCombatDeathPrediction_IsDeathPredicted(pentTarget))
		{
			return true;
		}
	}

	return false;
}


static int gclCharacter_ShouldCancelQueuedPowers(Entity *e, PowerActivation *pact, S32 bBecomeCurrent)
{
	if(!bBecomeCurrent && !pact->bCommit)
	{
		// 
		if (gclAutoAttack_IsEnabled() &&
			g_CombatConfig.bMovementCancelsRootingQueuedPowers && 
			poweract_DoesRootPlayer(pact) && pact->pRefAnimFXMain)
		{
			PowerAnimFX *pAfx = GET_REF(pact->pRefAnimFXMain->hFX);
			if (pAfx->lurch.bLurchSlideInMovementDirection)
			{
				U32 bits = CharacterActivatePowerClient_GetInputDirectionBits();

				if (bits != pact->eInputDirectionBits && 
					(mmGetLastQueuedInputValueBitFG(e->mm.movement,MIVI_BIT_LEFT) 
						|| mmGetLastQueuedInputValueBitFG(e->mm.movement,MIVI_BIT_RIGHT)
						|| mmGetLastQueuedInputValueBitFG(e->mm.movement,MIVI_BIT_BACKWARD)
						|| mmGetLastQueuedInputValueBitFG(e->mm.movement,MIVI_BIT_FORWARD)))
				{
					return true;
				}
			}
		}

		// If we don't normally allow movement, and the player trying to move, and the queued activation isn't
		//  committed, then cancel the queued powers
		if( !gclAutoAttack_IsEnabled() && 
			(g_CombatConfig.bMovementCancelsRootingQueuedPowers && poweract_DoesRootPlayer(pact)) &&
			(mmGetLastQueuedInputValueBitFG(e->mm.movement,MIVI_BIT_FORWARD) 
				|| mmGetLastQueuedInputValueBitFG(e->mm.movement,MIVI_BIT_BACKWARD)))
		{
			return true;
		}
		
		// bTacticalInputSinceLastCharacterTickQueue will only ever get set if 
		// g_CombatConfig.bTacticalAimCancelsQueuedPowers is set
		if (e->pPlayer && e->pPlayer->bTacticalInputSinceLastCharacterTickQueue)
		{
			return true;
		}

		if (_ShouldCancelQueuedPowers_IsTargetDeathPredicted(e, pact))
			return true;
	}
	return false;
}
#endif


void CharacterPlayDelayedFX(int iPartitionIdx, Character *pchar, PowerActivation *pact)
{
	if (!pact->bPlayedActivate)
	{
		Vec3 vecSourcePos,vecSourceDir,vecTarget;
		Entity *pentTarget = NULL;

		assert(pchar->pEntParent);
		entGetCombatPosDir(pchar->pEntParent,pact,vecSourcePos,vecSourceDir);

		if(character_ActFindTarget(iPartitionIdx,pchar,pact,vecSourcePos,vecSourceDir,&pentTarget,vecTarget))
		{
			character_MoveLungeStart(pchar,pact);
			character_AnimFXLunge(iPartitionIdx,pchar,pact);
			character_AnimFXActivateOn(iPartitionIdx,pchar,NULL,pact,NULL,pentTarget?pentTarget->pChar:NULL,vecTarget,pact->uiTimestampActivate,pact->uchID,pact->uiPeriod,0);

			PowersDebugPrintEnt(EPowerDebugFlags_ANIMFX, pchar->pEntParent, "Playing DelayedFX %d: At %d (Cur %d, Act %d)" 
								" p(%1.2f, %1.2f, %1.2f)\n",
								pact->uchID,
								pmTimestamp(0),
								pact->uiTimestampCurrented,
								pact->uiTimestampActivate,
								vecParamsXYZ(pact->vecTarget));
		}
	}
	
}

int CharacterQueuedPowerWillBecomeCurrentSoon(Character *pchar)
{
	PowerDef *pdefCurrent;

	if (!pchar->pPowActCurrent)
		return true;

	pdefCurrent = GET_REF(pchar->pPowActCurrent->hdef);
	if(pdefCurrent)
	{
		U32 completeTime, tillComplete;

		if (pdefCurrent->eType==kPowerType_Maintained)
			return true;
			
		//U32 iNextPowerTime = character_PredictTimeForNewActivation(pchar, true, false);
		//U32 timeSince = (pmTimestamp(0) - pchar->pPowActCurrent->uiTimestampActivate);
		completeTime = pmTimestampFrom(pchar->pPowActCurrent->uiTimestampActivate, pdefCurrent->fTimeActivate);
		tillComplete = completeTime - pmTimestamp(0);
		//if (pchar->pPowActCurrent->fTimerActivate < 0.1f) //pmTimestamp(0) + 2 < iNextPowerTime)

		if (tillComplete < (0.05f * MM_PROCESS_COUNTS_PER_SECOND))
		{
			return true;
		}
	}

	return false;
}

void character_UpdateDeathCapsuleLinger(Entity * pEnt)
{
	if (!pEnt->pChar->uiDeathCollisionTimer)
	{
		pEnt->pChar->uiDeathCollisionTimer = pmTimestamp(g_CombatConfig.fOnDeathCapsuleLingerTime);
		character_SetSleep(pEnt->pChar, g_CombatConfig.fOnDeathCapsuleLingerTime);
	}
	else if (pmTimestamp(0) > pEnt->pChar->uiDeathCollisionTimer)
	{
		pmSetCollisionsDisabled(pEnt);
	}
}


void character_CheckQueuedTargetUpdate(int iPartitionIdx, Character *pchar)
{
#ifdef GAMECLIENT
	if(pchar->pPowActQueued)
	{
		PowerActivation *pact = pchar->pPowActQueued;
		PowerDef *pdef;
		S32 bBecomeCurrent;

		if(pact->bPlayedActivate)
			return;	
		pdef = GET_REF(pact->hdef);
		
		if (!pdef || 
			!powerddef_ShouldDelayTargeting(pdef) || 
			pchar->eChargeMode == kChargeMode_Queued ||
			 pchar->eChargeMode == kChargeMode_QueuedMaintain)
			return;
		
		bBecomeCurrent = !pchar->pPowActCurrent; // Trivial case of becoming current
				
		// Something is still current... can this queued power override that?
		if(!bBecomeCurrent)
		{
			if(CharacterQueuedPowerWillBecomeCurrentSoon(pchar))
			{
				bBecomeCurrent = true;
			}
		}

		// Check the global cooldown timer
		if(!(pdef && pdef->bCooldownGlobalNotChecked) && pchar->fCooldownGlobalTimer > 0)
		{
			bBecomeCurrent = false;
		}
		
		if (bBecomeCurrent)
		{
			Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
			Entity *eTarget = NULL;
			U32 uiSeq = 0, uiSeqReset = 0;
	
			if (!ppow)
				return;
			// if this power should commit with the current camera targeting
			character_UpdateTargetingClient(iPartitionIdx, pchar, pdef, ppow, pact->vecTarget, &eTarget);
			character_GetPowerActSeq(pchar,&uiSeq,&uiSeqReset);
								  
			ServerCmd_MarkActUpdateTarget(pact->uchID,uiSeq,uiSeqReset, pact->vecTarget, eTarget?entGetRef(eTarget):0);
			CharacterPlayDelayedFX(iPartitionIdx,pchar, pact);

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue Updating Target %d: At %d (Cur %d, Act %d)" 
								" p(%1.2f, %1.2f, %1.2f)\n",
								pact->uchID,
								pmTimestamp(0),
								pact->uiTimestampCurrented,
								pact->uiTimestampActivate,
								vecParamsXYZ(pact->vecTarget));
		}

	}
#endif
}

static S32 CharacterShouldQueuedOverrideCurrent(Character *pchar)
{
	PowerDef *pdefCurrent = GET_REF(pchar->pPowActCurrent->hdef);

	if(pdefCurrent)
	{
		if (pdefCurrent->eType != kPowerType_Maintained)
		{
			return pdefCurrent->fTimeOverride > pdefCurrent->fTimeActivate - pchar->pPowActCurrent->fTimeActivating;
		}

		if (pchar->pPowActCurrent->eActivationStage == kPowerActivationStage_PostMaintain)
		{
			return pdefCurrent->fTimeOverride > pchar->pPowActCurrent->fStageTimer;
		}
	}
	
	return false;
}


// Processes the queued power activations and attempts to make them current
static void CharacterTickQueue(int iPartitionIdx, Character *pchar, F32 fRate)
{
	// Update location for overflow if we have one
	if(pchar->pPowActOverflow)
	{
		Power* ppow = character_ActGetPower(pchar, pchar->pPowActOverflow);
		
		PERFINFO_AUTO_START(__FUNCTION__"Overflow",1);

		if(!ppow)
		{
			// Don't have that Power anymore, cancel PowerActivation
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		}
		else
		{
			// TODO(JW): I'd like to think we don't need to wake here, but to do that
			//  I think the rest of the code would have to be instrumented to wake
			//  when the queue state changes.
			character_Wake(pchar);

			poweract_UpdateLocation(pchar->pEntParent,pchar->pPowActOverflow);

			if(!entIsServer() && !pchar->pPowActOverflow->erTarget && GET_REF(pchar->pPowActOverflow->hTargetObject) )
			{
				//find the entity ref target for the activation
				character_TargetEntFromNode(iPartitionIdx, pchar, pchar->pPowActOverflow);
			}

			if(!pchar->pPowActQueued)
			{
				// This shouldn't happen, but in case it does we'll try and handle it gracefully
				pchar->pPowActQueued = pchar->pPowActOverflow;
				pchar->pPowActOverflow = NULL;
				if(pchar->eChargeMode==kChargeMode_Overflow || pchar->eChargeMode==kChargeMode_OverflowMaintain)
					pchar->eChargeMode -= 2; // Overflow->Queue
			}
			else if (pchar->pPowActOverflow->fTimerRemainInQueue > 0.0f)
			{
				pchar->pPowActOverflow->fTimerRemainInQueue -= fRate;
			}
		}
		PERFINFO_AUTO_STOP();
	}

	if(pchar->pPowActQueued)
	{
		PowerActivation *pact = pchar->pPowActQueued;
		// TODO(JW): Queue: Consider copying these variables into the power 
		//  activation struct so we don't do a bunch of GET_REFing every tick
		PowerDef *pdef = GET_REF(pact->hdef);
		PowerDef *pPreActivateDef = NULL;
		Power* ppow = character_ActGetPower(pchar, pact);
		S32 bOverride = false;
		S32 bBecomeCurrent = !pchar->pPowActCurrent; // Trivial case of becoming current

		PERFINFO_AUTO_START_FUNC();

		if(!ppow)
		{
			// Don't have that Power anymore, cancel PowerActivation
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
			PERFINFO_AUTO_STOP();
			return;
		}

		// TODO(JW): I'd like to think we don't need to wake here, but to do that
		//  I think the rest of the code would have to be instrumented to wake
		//  when the queue state changes.
		character_Wake(pchar);

		if(!entIsServer() && !pact->erTarget && GET_REF(pact->hTargetObject))
		{
			//find the entity ref target for the activation
			character_TargetEntFromNode(iPartitionIdx, pchar, pact);
		}

		// Something is still current... can this queued power override that?
		if(!bBecomeCurrent)
		{
			if(pdef && pdef->bOverrides)
			{
				if (CharacterShouldQueuedOverrideCurrent(pchar))
				{	// TODO(JW): Queue: Validate that queued is allowed to become current,
					//  not sure when it ever wouldn't be
					bBecomeCurrent = true;
					bOverride = true;
				}
			}
		}

		// Check the global cooldown timer
		if(!(pdef && pdef->bCooldownGlobalNotChecked) && pchar->fCooldownGlobalTimer > 0)
		{
			bBecomeCurrent = false;
		}

		//Make sure the power isn't recharging.
		if (power_GetRecharge(ppow) > 0 || character_GetCooldownFromPowerDef(pchar, pdef) > 0)
			bBecomeCurrent = false;

		if (pchar->pPowActQueued->fTimerRemainInQueue > 0.0f)
		{
			pchar->pPowActQueued->fTimerRemainInQueue -= fRate;
			
			// Check queue countdown timer
			if (pchar->pPowActQueued->fTimerRemainInQueue > 0.0f)
			{
				bBecomeCurrent = false;
			}
		}

		// RMARR - since this only gets called every tenth of a second, isn't this code a little broken?
#ifdef GAMECLIENT 
		// check if there's any reason to cancel the queued power
		if(gclCharacter_ShouldCancelQueuedPowers(pchar->pEntParent, pact, bBecomeCurrent))
		{
			U8 uchQueued, uchOverflow;
			uchQueued = character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
			uchOverflow = character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
			if(uchQueued)
			{
				ServerCmd_PowersActCancelServer(uchQueued,NULL);
			}
			if(uchOverflow)
			{
				ServerCmd_PowersActCancelServer(uchOverflow,NULL);
			}
			PERFINFO_AUTO_STOP();
			return;
		}
#endif
		// Update location
		poweract_UpdateLocation(pchar->pEntParent,pact);

#ifdef GAMESERVER
		if(bBecomeCurrent && !pact->bUnpredicted && !pact->bCommit)
		{
			// Check to see if this should shut off any exclusive toggles
			// This is here, so click and other power types can shut off toggle powers
			if(pdef && pdef->bToggleExclusive && eaSize(&pchar->ppPowerActToggle) > 0)
			{
				int i,s;
				int *piExclusive = NULL;
				// Find the exclusive categories
				for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
				{
					if(g_PowerCategories.ppCategories[pdef->piCategories[i]]->bToggleExclusive)
					{
						eaiPush(&piExclusive,pdef->piCategories[i]);
					}
				}

				s = eaiSize(&piExclusive);

				for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
				{
					PowerDef *pdefOld = GET_REF(pchar->ppPowerActToggle[i]->ref.hdef);
					if(pdefOld && pdefOld->bToggleExclusive)
					{
						int j;
						for(j=0; j<s; j++)
						{
							if(-1!=eaiFind(&pdefOld->piCategories,piExclusive[j]))
							{
								break;
							}
						}
						if(j<s)
						{
							// Found an exclusive category
							character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pmTimestamp(0),true);
						}
					}
				}
				eaiDestroy(&piExclusive);
			}

			// The client predicted it, but hasn't committed yet, so we'll stall it for up to half a second
			if(pact->fTimeStalled < 0.5f)
			{
				pact->fTimeStalled += fRate;
				bBecomeCurrent = false;
			}
			else
			{
				if (g_CombatConfig.bClientPredictSeq)
				{
					F32 fLastLinkRecvTime = -1.0f;
					if (SAFE_MEMBER(pchar->pEntParent->pPlayer, clientLink))
					{
						fLastLinkRecvTime = linkRecvTimeElapsed(pchar->pEntParent->pPlayer->clientLink->netLink);
					}
					// TODO: We could cancel here instead of letting it go current if it fails character_ActTestDynamic
					ErrorDetailsf("%s %s; (%d, %d, %d, %d); %f",
						CHARDEBUGNAME(pchar), REF_STRING_FROM_HANDLE(pact->hdef), CharacterPowerActIDs(pchar), fLastLinkRecvTime);
					Errorf("PowerActivation Seq reset due to server-stalled queued PowerActivation going current");
					character_ResetPowerActSeq(pchar);
				}
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Stalled for %f, making current\n",pact->uchID,pact->fTimeStalled);
			}
		}
#endif

		if(bBecomeCurrent)
		{
			S32 bHasTarget;
			Vec3 vecSourcePos,vecSourceDir,vecTarget;
			Entity *pentTarget = NULL;
			Character *pcharTarget = NULL;

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Making current at %d (Cur %d, Act %d)\n",pact->uchID,pmTimestamp(0),pact->uiTimestampCurrented,pact->uiTimestampActivate);

			// TODO(JW): Queue: Validate that queued is allowed to become current,
			//  not sure when it ever wouldn't be

#ifdef GAMESERVER
			if(!pact->bCommit && !pact->bUnpredicted)
			{
				// Get the next PowerActivation seq number and use it for uiSeedSBLORN
				U32 uiSeq = 0, uiSeqReset = 0;
				character_GetPowerActSeq(pchar,&uiSeq,&uiSeqReset);
				pact->uiSeedSBLORN = uiSeq;
			}
#endif

#ifdef GAMECLIENT
			if(!pact->bCommit)
			{
				// Notify the server that this activation was committed
				U32 uiSeq = 0, uiSeqReset = 0;
				character_GetPowerActSeq(pchar,&uiSeq,&uiSeqReset);
				pact->bCommit = true;
				pact->uiSeedSBLORN = uiSeq;
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Committed by Queue going Current\n",pact->uchID);
				ServerCmd_MarkActCommitted(pact->uchID,uiSeq,uiSeqReset);
			}
#endif
			if(!pact->bPlayedActivate && 
				powerddef_ShouldDelayTargeting(pdef) && 
				pchar->eChargeMode != kChargeMode_Queued && 
				pchar->eChargeMode != kChargeMode_QueuedMaintain)
			{
				CharacterPlayDelayedFX(iPartitionIdx, pchar, pact);
			}

			// Turn off target tracking if appropriate
			if(pdef && pdef->eTracking==kTargetTracking_UntilCurrent)
			{
				poweract_StopTracking(pact);
			}

			// If we're overriding something that is still running, clean it up
			if(bOverride)
			{
				Power *ppowCurrent = character_ActGetPower(pchar,pchar->pPowActCurrent);
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Overriding current\n",pact->uchID);
				if(ppowCurrent)
				{
					PowerDef *pdefCurrent = GET_REF(ppowCurrent->hDef);
					PowerAnimFX *pafx = pdefCurrent ? GET_REF(pdefCurrent->hFX) : NULL;

					// Power is done, so play the deactivate and set recharge
					if(pafx)
					{
						U32 uiAnimTime = pmTimestampFrom(pchar->pPowActCurrent->uiTimestampActivate, pdefCurrent->fTimeActivate);
						character_AnimFXActivateOff(iPartitionIdx,pchar,pchar->pPowActCurrent,uiAnimTime);
						character_AnimFXDeactivate(iPartitionIdx,pchar,pchar->pPowActCurrent,uiAnimTime);
					}
					character_MoveCancel(pchar, pchar->pPowActCurrent->uchID, 0);
					power_SetRechargeDefault(iPartitionIdx,pchar,ppowCurrent,!pchar->pPowActCurrent->bHitTargets);
					power_SetCooldownDefault(iPartitionIdx,pchar,ppowCurrent);
				}
				character_ActCurrentFinish(iPartitionIdx,pchar,true);
			}

			// we need to do some time adjustments for the CombatSleep optimization
			//	setup the initial timer for whatever state that the power activation will enter first
			//	so the first tick we don't over count any activation timers
			if (pdef)
			{
				if (pdef->fTimeCharge)
				{
					pact->fTimeCharged = -(character_GetSpeedCharge(iPartitionIdx, pchar, ppow) * pchar->fTimeSlept);
					pact->fTimeChargedTotal = -pchar->fTimeSlept;
				}
				else if (pdef->fTimePreactivate)
				{
					pact->fStageTimer = pchar->fTimeSlept;
				}
				else
				{
					pact->fTimeActivating = -pchar->fTimeSlept;
				}
			}

			// Make queued current and overflow queued
			if(!verify(pchar->pPowActCurrent==NULL))
				poweract_DestroySafe(&pchar->pPowActCurrent);
			pchar->pPowActCurrent = pact;
			pchar->pPowActQueued = pchar->pPowActOverflow;
			pchar->pPowActOverflow = NULL;
			if(pchar->eChargeMode>=kChargeMode_QueuedMaintain)
			{
				pchar->eChargeMode -= 2; // Overflow->Queue->Current
			}
			else
			{
				pchar->eChargeMode = kChargeMode_None;
			}

			// if this power has activation immunities, set the flag on the character and start the FX
			if (pdef->bActivationImmunity)
			{
				pchar->bPowerActivationImmunity = true;
				character_AnimFxPowerActivationImmunity(iPartitionIdx, pchar, pact);
			}

			// Track combat events for activations
			assert(pchar->pEntParent);
			entGetCombatPosDir(pchar->pEntParent,pact,vecSourcePos,vecSourceDir);
			if (bHasTarget = character_ActFindTarget(iPartitionIdx,pchar,pact,vecSourcePos,vecSourceDir,&pentTarget,vecTarget))
			{
				pcharTarget = SAFE_MEMBER(pentTarget, pChar);
			}
			if (pchar == pcharTarget)
			{
				character_CombatEventTrack(pchar,kCombatEvent_ActivateSelf);
			}
			else if (pcharTarget)
			{
				character_CombatEventTrackInOut(pcharTarget, kCombatEvent_ActivateInOther, kCombatEvent_ActivateOutOther,
												pchar->pEntParent, NULL, NULL, 0, 0, NULL, NULL);
			}
			else
			{
				character_CombatEventTrack(pchar,kCombatEvent_ActivateOutOther);
			}

			// Apply the global cooldown timer
			if(!pdef->bCooldownGlobalNotApplied)
				pchar->fCooldownGlobalTimer = g_CombatConfig.fCooldownGlobal;

#ifdef GAMESERVER
			if(pdef && pdef->pExprAICommand)
			{
				aiPowersRunAIExpr(pchar->pEntParent,NULL,NULL,pdef->pExprAICommand, NULL, NULL);
			}

			if (pdef && pchar->pEntParent->mm.mrDragon)
			{
				PowerAnimFX *pafx = GET_REF(pdef->hFX);
				if (pafx)
					poweranimfx_DragonStartPowerActivation(pchar, pafx, pact, pdef);
			}

			if(g_CombatConfig.bClientChargeData)
			{
				if(pchar->eChargeMode==kChargeMode_Current)
				{
					StructDestroySafe(parse_CharacterChargeData,&pchar->pChargeData);
					pchar->pChargeData = StructAlloc(parse_CharacterChargeData);
					COPY_HANDLE(pchar->pChargeData->hMsgName,pdef->msgDisplayName.hMessage);
					pchar->pChargeData->fTimeCharge = pdef->fTimeCharge;
					pchar->pChargeData->uiTimestamp = pmTimestamp(0);
					pchar->bChargeDataDirty = true;
				}
				else if(pchar->pChargeData)
				{
					StructDestroySafe(parse_CharacterChargeData,&pchar->pChargeData);
					pchar->bChargeDataDirty = true;
				}
			}

			pPreActivateDef = GET_REF(pdef->hPreActivatePowerDef);
			if(pPreActivateDef)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pchar->pEntParent);
				ApplyUnownedPowerDefParams applyParams = {0};

				applyParams.erTarget = (bHasTarget && pcharTarget) ? entGetRef(pcharTarget->pEntParent) : entGetRef(pchar->pEntParent);
				if(!ISZEROVEC3(vecTarget))
					applyParams.pVecTarget = vecTarget;
				applyParams.pcharSourceTargetType = pchar;
				applyParams.pclass = character_GetClassCurrent(pchar);
				applyParams.iLevel = entity_GetCombatLevel(pchar->pEntParent);
				applyParams.fTableScale = 1.f;
				applyParams.erModOwner = entGetRef(pchar->pEntParent);
				applyParams.pExtract = pExtract;

				if (pdef->bCancelPreActivatePower)
				{
					REMOVE_HANDLE(pact->preActivateRef.hdef);
					COPY_HANDLE(pact->preActivateRef.hdef, pdef->hPreActivatePowerDef);
				}

				character_ApplyUnownedPowerDef(iPartitionIdx, pchar, pPreActivateDef, &applyParams);
			}
#endif

#ifdef GAMECLIENT
			// Turn on/off AutoAttack
			// Comment (BH): I'm not sure we'd want a game that has auto-attack enablers and maintained autoattack type.
			if(pdef->bAutoAttackEnabler && g_CurrentScheme.eAutoAttackType != kAutoAttack_Maintain)
				gclAutoAttack_DefaultAutoAttack(true);
			else if(pdef->bAutoAttackDisabler)
				gclAutoAttack_DefaultAutoAttack(false);

			comboTracker_PowerActivate(pact);
#endif

			// Start the activate animations now if it hasn't already been started
			if(pchar->eChargeMode!=kChargeMode_Current && (!pdef || pdef->fTimePreactivate == 0.0f) && !pact->bPlayedActivate && !pact->bDelayActivateToHitCheck)
			{
				PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
				if(pafx)
				{
					// TODO(RP): get the vecSourcePos/vecSourceDir based on the pact
					if(bHasTarget)
					{
						character_MoveLungeStart(pchar,pact);
						character_AnimFXLunge(iPartitionIdx,pchar,pact);
						character_AnimFXActivateOn(iPartitionIdx,pchar,NULL,pact,ppow,pcharTarget,vecTarget,pact->uiTimestampActivate,pact->uchID,pact->uiPeriod,0);
					}
				}
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

// Exposed call for CharacterTickQueue
void character_TickQueue(int iPartitionIdx, Character *pchar, F32 fRate)
{
	CharacterTickQueue(iPartitionIdx,pchar,fRate);
	if (pchar->pEntParent && pchar->pEntParent->pPlayer)
	{
		pchar->pEntParent->pPlayer->bTacticalInputSinceLastCharacterTickQueue = false;
	}
}


static void CharacterTickInstant(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	Power *ppow;
	int i;

	for(i=eaSize(&pchar->ppPowerActInstant)-1;i>=0;i--)
	{
		PowerActivation *pact = pchar->ppPowerActInstant[i];
		PowerDef *ppowdef = GET_REF(pact->hdef);
		PowerAnimFX *pafx = ppowdef ? GET_REF(ppowdef->hFX) : NULL;
		Entity *eTarget = entFromEntityRef(iPartitionIdx,pact->erTarget);

		character_Wake(pchar);

		ppow = character_ActGetPower(pchar,pact);
		if(ppow)
		{
			if(!entIsServer() && !pact->erTarget && GET_REF(pact->hTargetObject))
			{
				//find the entity ref target for the activation
				character_TargetEntFromNode(iPartitionIdx, pchar, pact);
			}

			// Update location
			poweract_UpdateLocation(pchar->pEntParent,pact);

			// Try to pay cost for real.  Toggle Powers pay as well, even though they don't
			//  do an apply during the initial activate time, because sometimes they're constructed
			//  with an up-front cost.
			if(character_PayPowerCost(iPartitionIdx,pchar,ppow,pact->erTarget,pact,true,pExtract))
			{
				S32 iFrames = 0;
				if(pact->eLungeMode != kLungeMode_None)
				{
					iFrames = MAX(pact->uiTimestampLungeMoveStop - pact->uiTimestampLungeMoveStart,0);
				}

				// Activate it and set the Power active
				pact->uiPeriod = 0;
				pact->bActivated = true;
				power_SetActive(ppow,true);

#ifdef GAMESERVER
				if (!ppowdef->bChargesSetCooldownWhenEmpty || !ppowdef->bRechargeRequiresCombat || entIsInCombat(pchar->pEntParent))
				{				
					character_PowerUseCharge(pchar,ppow);
				}
#endif

				// Apply the power if its not a toggle power
				character_ApplyPower(iPartitionIdx,pchar,ppow,pact,pact->erTarget,pact->vecTarget,pact->vecTargetSecondary,!pact->bPlayedActivate,iFrames,pExtract,NULL);

				// Set the timer (mostly for maintained powers)
				pact->fTimerActivate = ppowdef->fTimeActivate;

				// Turn off target tracking if appropriate
				if(ppowdef->eTracking==kTargetTracking_UntilFirstApply)
				{
					poweract_StopTracking(pact);
				}

				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %s: Timer (%f)\n",ppowdef->pchName,pact->fTimerActivate);
			}
			else
			{
				// Cancel current power activation					
				ActivationFailureParams failureParams = { 0 };
				failureParams.pEnt = pchar->pEntParent;
				failureParams.pPow = ppow;

				character_ActivationFailureFeedback(kActivationFailureReason_Cost, &failureParams);
			}
		}
		character_ActInstantFinish(iPartitionIdx,pchar,pact);
		eaRemoveFast(&pchar->ppPowerActInstant,i);
		poweract_DestroySafe(&pact);
	}
}

// Processes the character's current power activation
static void CharacterTickCurrent(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	Power *ppow = NULL;

	if(!pchar->pPowActCurrent)
		return;

	PERFINFO_AUTO_START_FUNC();

	if(pchar->pPowActCurrent)
	{
		character_Wake(pchar);

		ppow = character_ActGetPower(pchar,pchar->pPowActCurrent);
		if(!ppow)
		{
			// Don't have that Power anymore, cancel current power activation
			character_ActCurrentCancel(iPartitionIdx,pchar,true,true);
		}
	}
	
	if(pchar->pPowActCurrent)
	{
		PowerActivation *pact = pchar->pPowActCurrent;
		PowerDef *ppowdef = GET_REF(pact->hdef);
		PowerAnimFX *pafx = ppowdef ? GET_REF(ppowdef->hFX) : NULL;
		Entity *eTarget = entFromEntityRef(iPartitionIdx,pact->erTarget);
		bool bChargeStopped = false;
		F32 fTimeChargeNow = 0;
		bool bExecute = true;
		bool bSynced = true;

		if(!entIsServer() && !pact->erTarget && GET_REF(pact->hTargetObject))
		{
			//find the entity ref target for the activation
			character_TargetEntFromNode(iPartitionIdx, pchar, pact);
		}

		// If the current power still wants to charge
		PERFINFO_AUTO_START("Charging",1);
		if(ppowdef && pact->eActivationStage==kPowerActivationStage_Charge)
		{
			if (ppowdef == NULL || ppowdef->fTimeCharge == 0.0f)
			{
				if (ppowdef->fTimePreactivate)
				{
					character_ActMoveToPreactivate(iPartitionIdx,pchar,pact);
				}
				else
				{
					pact->eActivationStage = kPowerActivationStage_LungeGrab;
				}
			}
			else
			{
				F32 fTimeDiff = ppowdef->fTimeCharge - pact->fTimeCharged;
				F32 fModifiedRate = character_GetSpeedCharge(iPartitionIdx, pchar, ppow) * fRate;
				F32 fRateRatio = 1;

				if(fTimeDiff <= fModifiedRate && fModifiedRate > 0)
				{
					fRateRatio = fTimeDiff / fModifiedRate;
				}

				devassert(pchar->eChargeMode==kChargeMode_Current);

				// We don't want to visually activate this in the past, so save this value as the practical
				//  activation charge time
				fTimeChargeNow = character_TickRound(pact->fTimeChargedTotal + (fRate * fRateRatio));

				// Check charge requires expression
				if(ppowdef->pExprRequiresCharge)
				{
					combateval_ContextReset(kCombatEvalContext_Activate);
					combateval_ContextSetupActivate(pchar,eTarget ? eTarget->pChar : NULL,pact,kCombatEvalPrediction_None);
					if(!combateval_EvalNew(iPartitionIdx, ppowdef->pExprRequiresCharge,kCombatEvalContext_Activate,NULL))
					{
						bChargeStopped = true;
					}
				}

				if(!bChargeStopped)
				{
					// Remind ourselves to not execute
					bExecute = false;

					// Update location
					poweract_UpdateLocation(pchar->pEntParent,pact);

					// Increment charge time tracker
					pact->fTimeCharged = MIN(ppowdef->fTimeCharge, pact->fTimeCharged + fModifiedRate);
					pact->fTimeChargedTotal = pact->fTimeChargedTotal + fRate;

					// Check to see if we can pay for it
					if(!character_PayPowerCost(iPartitionIdx,pchar,ppow,pact->erTarget,pact,false,pExtract))
					{
						// The character CAN'T afford to pay the cost, which may be because of the charge time.
						//  Stop charging, roll back the charge time two ticks, and recheck to either execute
						//  or cancel.
						pact->fTimeCharged = MAX(0.0f, pact->fTimeCharged - (2.0 * fModifiedRate));
						pact->fTimeChargedTotal = MAX(0.0f, pact->fTimeChargedTotal - (2.0 * fRate));
						bChargeStopped = true;
						if(character_PayPowerCost(iPartitionIdx,pchar,ppow,pact->erTarget,pact,false,pExtract))
						{
							bExecute = true;
						}
						else
						{
							ActivationFailureParams failureParams = { 0 };
							failureParams.pEnt = pchar->pEntParent;
							failureParams.pPow = ppow;
							character_ActivationFailureFeedback(kActivationFailureReason_Cost, &failureParams);

							if(pact->pRefAnimFXMain)
							{
								character_AnimFXChargeOff(iPartitionIdx,pchar,pact,pmTimestampFrom(pact->uiTimestampCurrented,fTimeChargeNow),false);
							}
							character_ActCurrentCancel(iPartitionIdx,pchar,true,true);
						}
					}

					if(pchar && g_CombatConfig.pTimer->fTimerUseVisual > 0.0f)
					{
						character_SetCombatVisualsExitTime(pchar, g_CombatConfig.pTimer->fTimerUseVisual);
					}
				}

				if(!bChargeStopped)
				{
					// Check and see if we've exceeded the allowed charge time
					if(pact->fTimeCharged >= ppowdef->fTimeCharge)
					{
						// clamp the charge to the def's max
						pact->fTimeCharged = ppowdef->fTimeCharge;
						
						// check if the charge is allowed to be held until the user releases 
						if (!ppowdef->bChargeAllowIndefiniteCharging)
						{	
							// Stop charging and attempt to execute
							bExecute = true;
							bChargeStopped = true;
						}
					}
				}

				// Do all the charge shutdown and lunge stuff after the charge failure check
				if(bExecute && bChargeStopped)
				{
					bool bPowerChanged;
					pact->uiTimestampActivate = pmTimestampFrom(pact->uiTimestampCurrented,fTimeChargeNow);
					if (ppowdef->fTimePreactivate)
					{
						// Just ends the charge
						bPowerChanged = character_ActEndCharge(iPartitionIdx,pchar,pact);
					}
					else
					{
						// this one cruises into lunge, and playing activate FX
						bPowerChanged = character_ActChargeToActivate(iPartitionIdx,pchar,pact,pafx);
					}

					if (bPowerChanged)
					{
						// Our power has changed, so we definitely have a power we could activate
						// Update our local variables
						ppow = character_ActGetPower(pchar,pact);
						ppowdef = ppow ? GET_REF(ppow->hDef) : NULL;
						pafx = ppowdef ? GET_REF(ppowdef->hFX) : NULL;
					}
					else
					{
						// Our power hasn't changed (character_ActChargeToActivate could have failed utterly)
						// This case is protected above by a check that pPowActCurrent is non-NULL.  I guess one of the above function calls
						// can cause it to become NULL, even if bPowerChanged is false?
						if(!pchar->pPowActCurrent)
							bExecute = false;
					}

					if (ppowdef->fTimePreactivate)
					{
						character_ActMoveToPreactivate(iPartitionIdx,pchar,pact);
					}
				}
			}
		}
		PERFINFO_AUTO_STOP(); // end charge stuff

		// Am I waiting before activate?
		if (bExecute && pact->eActivationStage==kPowerActivationStage_Preactivate)
		{
			// Wait out the activate time before executing
			pact->fStageTimer -= fRate;
			if(pact->fStageTimer <= 0.0f)
			{
				character_AnimFXPreactivateOff(iPartitionIdx, pchar, pact, pmTimestamp(0), false);
				// We're done, go ahead and do the other stuff
				character_ActMoveToActivate(iPartitionIdx,pchar,pact);
			}
			else
			{
				// we're waiting
				bExecute = false;
			}
		}

		if(bExecute && pact->eActivationStage==kPowerActivationStage_LungeGrab)
		{
			if (pact->eLungeMode == kLungeMode_Pending)
			{
				pact->fStageTimer -= fRate;
				if(pact->fStageTimer <= -0.5f)
				{
					// We're half a second beyond how long we expected to lunge, go ahead and activate
					pact->eLungeMode = kLungeMode_Activated;
				}
				else
				{
					// Busy lunging, so don't execute yet!
					bExecute = false;
				}
			}

			if(bExecute && pact->eGrabMode==kGrabMode_Pending)
			{
				bExecute = false;
				// Need to start trying to grab
				if(character_AnimFXGrab(iPartitionIdx,pchar,pact))
					pact->eGrabMode = kGrabMode_Activated;
				else
					character_ActCurrentCancel(iPartitionIdx,pchar,true,true);
			}
			else if(pact->eGrabMode==kGrabMode_Activated)
			{
				S32 iState = character_GetAnimFXGrabState(pchar);
				if(iState < 0)
				{
					// Failure, cancel
					character_ActCurrentCancel(iPartitionIdx,pchar,true,true);
					bExecute = false;
				}
				else if(iState==0)
				{
					// Still chasing, so don't execute yet
					bExecute = false;
				}
				else
				{
					// Success
					pact->eGrabMode = kGrabMode_Success;
				}
			}
			else if(pact->eGrabMode==kGrabMode_Success)
			{
				// Check if we're still holding on, if not, cancel
				S32 iState = character_GetAnimFXGrabState(pchar);
				if(iState < 0)
				{
					character_ActCurrentCancel(iPartitionIdx,pchar,true,true);
					bExecute = false;
				}
			}

			if (bExecute)
			{
				pact->eActivationStage=kPowerActivationStage_Activate;
			}
		}

		/*
		if(bExecute && ppowdef->eType == kPowerType_Maintained
			&& ppowdef->eInterrupts & kPowerInterruption_LostLOS)
		{
			if(pact->erTarget)
			{
				Entity *pTarget = entFromEntityRef(iPartitionIdx,pact->erTarget);

				if(pTarget && CharacterVisibilityCheck(iPartitionIdx,))
			}
		}
		*/

		// After all of that, it's ok to execute the power
		if(ppowdef && bExecute)
		{
			S32 bActivatedThisTick = false;
			S32 bTypeToggle = (ppowdef->eType == kPowerType_Toggle);
			S32 bPowerIsCharged = (ppowdef->fTimeCharge > 0.f);

			if(!pact->bActivated)
			{
				bool bGood = true;

				PERFINFO_AUTO_START("NewActivation",1);

				// Update location
				poweract_UpdateLocation(pchar->pEntParent,pact);

				//If the power is charged, re-test the dynamic activation requirements.
				// Don't go through with the charge activation if things have changed that disallow it.
				if(bPowerIsCharged)
				{
					if (!character_ActTestDynamic(iPartitionIdx, pchar, ppow, eTarget, pact->vecTargetSecondary, GET_REF(pact->hTargetObject), NULL, NULL, NULL, true,false,true))
					{
						bGood = false;
					}

					// TODO: Fix this to make this better. This is a fix for charge powers that are "tapped"
					// so that the sleep time optimization does not give them negative charge times
					pact->fTimeCharged = MAX(0.0f, pact->fTimeCharged);
					pact->fTimeChargedTotal = MAX(0.0f, pact->fTimeChargedTotal);
				}
				
				if(bGood)
				{
					// Try to pay cost for real.  Toggle Powers pay as well, even though they don't
					//  do an apply during the initial activate time, because sometimes they're constructed
					//  with an up-front cost.
					
					if(character_PayPowerCost(iPartitionIdx, pchar, ppow, pact->erTarget, pact, 
												!g_CombatConfig.bPayPowerCostAndRechargePostHitframe, pExtract))
					{
						bool bApplied = true;

						S32 iFrames = 0;
						if(pact->eLungeMode != kLungeMode_None)
						{
							iFrames = MAX(pact->uiTimestampLungeMoveStop - pact->uiTimestampLungeMoveStart,0);
						}

						// Activate it and set the Power active
						pact->uiPeriod = 0;
						pact->bActivated = bActivatedThisTick = true;
						power_SetActive(ppow,true);
	
#ifdef GAMESERVER
						if (!ppowdef->bChargesSetCooldownWhenEmpty || !ppowdef->bRechargeRequiresCombat || entIsInCombat(pchar->pEntParent))
						{
							character_PowerUseCharge(pchar,ppow);
						}
#endif

						// Apply the power if its not a toggle power
						if(!bTypeToggle)
						{
							bApplied = character_ApplyPower(iPartitionIdx,pchar,ppow,pact,pact->erTarget,pact->vecTarget,
															pact->vecTargetSecondary,!pact->bPlayedActivate,iFrames,pExtract,NULL);
						}
									
						if (!bApplied && ppowdef->pExprRequiresApply)
						{
							// Cancel current activation
							character_ActCurrentCancel(iPartitionIdx,pchar, false, false);
						}
						else
						{
							PowerDef *pPreActivateDef = GET_REF(pact->preActivateRef.hdef);
							if(pPreActivateDef)
							{
								character_CancelModsFromDef(pchar, pPreActivateDef, entGetRef(pchar->pEntParent), pact->uiIDServer, false);
							}

							// Set the timer (mostly for maintained powers)
							pact->fTimerActivate = ppowdef->fTimeActivate;

							// Turn off target tracking if appropriate
							if(ppowdef->eTracking==kTargetTracking_UntilFirstApply)
							{
								poweract_StopTracking(pact);
							}

							PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %s: Timer (%f)\n",ppowdef->pchName,pact->fTimerActivate);
						}

					}
					else
					{
						// Cancel current power activation					
						ActivationFailureParams failureParams = { 0 };
						failureParams.pEnt = pchar->pEntParent;
						failureParams.pPow = ppow;

						//TODO(CM):More-informative error messages when activation fails due to item recipe cost.
						character_ActCurrentCancel(iPartitionIdx,pchar, false, false);
	
						character_ActivationFailureFeedback(kActivationFailureReason_Cost, &failureParams);
					}
				}
				else
				{
					character_ActCurrentCancel(iPartitionIdx,pchar, false, false);
				}
				PERFINFO_AUTO_STOP();
			}

			// May not be a current activation anymore, so refresh this variable
			if(pact = pchar->pPowActCurrent)
			{
				// Set the Power active (again)
				power_SetActive(ppow,true);

				if (g_CombatConfig.bPayPowerCostAndRechargePostHitframe && !pact->bPaidCost)
				{
					if (character_ActivationHasReachedHit(pact))
					{
						character_PayPowerCost(iPartitionIdx, pchar, ppow, pact->erTarget, pact, true, pExtract);
					}
				}

				if(ppowdef->eType!=kPowerType_Maintained)
				{
					PERFINFO_AUTO_START("NonMaintained",1);
					if(!bActivatedThisTick)
					{
						pact->fTimeActivating += fRate;
						pact->fTimerActivate -= character_GetSpeedPeriod(iPartitionIdx, pchar, ppow) * fRate;
					}

					// Check non-maintained powers that have finished the activation
					if(pact->fTimerActivate<=0.0f)
					{
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %s: Finished activation (%f)\n",ppowdef->pchName,pact->fTimeActivating);

						if(bTypeToggle)
						{
							// Toggles get activated with a timer of 0 so they apply immediately
							pact->fTimerActivate = 0.0f;
							character_ActivateToggle(iPartitionIdx,pchar,pchar->pPowActCurrent,pmTimestamp(0));
							if(!entIsServer())
							{
								power_SetActive(ppow,true);
							}
						}
						else
						{
							// Other powers are done, so play the deactivate and set recharge
							if(pafx)
							{
								U32 uiAnimTime = pmTimestampFrom(pact->uiTimestampActivate, ppowdef->fTimeActivate);
								character_AnimFXActivateOff(iPartitionIdx,pchar,pact,uiAnimTime);
								character_AnimFXDeactivate(iPartitionIdx,pchar,pact,uiAnimTime);
							}
							power_SetRechargeDefault(iPartitionIdx,pchar,ppow,!pact->bHitTargets);
							power_SetCooldownDefault(iPartitionIdx,pchar,ppow);
						}
						character_ActCurrentFinish(iPartitionIdx,pchar,false);
					}
					PERFINFO_AUTO_STOP();
				}
				else if (pact->eActivationStage != kPowerActivationStage_PostMaintain)
				{
					// Default reason to deactivate is that we're not maintaining anymore and
					//  we have finished at least the initial activation, and either we're
					//  about to start a new tick, or we don't need to maintain full periods.
					S32 bDeactivate = (pchar->eChargeMode!=kChargeMode_CurrentMaintain
										&& pact->uiPeriod>0
										&& (pact->fTimerActivate <= 0.0f
											|| !g_CombatConfig.bMaintainedFullPeriods));

					F32 fSpeed = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow);

					if(pchar && g_CombatConfig.pTimer->fTimerUseVisual > 0.0f)
					{
						character_SetCombatVisualsExitTime(pchar, g_CombatConfig.pTimer->fTimerUseVisual);
					}

					PERFINFO_AUTO_START("Maintained",1);
					
#define CRAZY_MAINTAINED_PREDICTION 0

					// To support pre-period prediction on maintained powers, if it was activated this tick
					//  turn off the bPlayedActivate flag.  This informs us that we haven't played the
					//  activate for the next period yet.
					if(CRAZY_MAINTAINED_PREDICTION && bActivatedThisTick)
					{
						pact->bPlayedActivate = false;
					}

					// If we're not already planning on deactivating, check to see if this is a
					//  HitChanceOneTime Power that missed, and OneTimeCancelOnMiss is enabled.
					if(!bDeactivate
						&& ppowdef->bHitChanceOneTime
						&& !pact->bHitPrior
						&& g_CombatConfig.pHitChance
						&& g_CombatConfig.pHitChance->bOneTimeCancelOnMiss)
					{
						bDeactivate = true;
#ifdef GAMESERVER
						ClientCmd_Powers_DeactivateMaintainedPower(pchar->pEntParent,pact->uchID);
#endif
					}

					if(!bDeactivate && !bActivatedThisTick)
					{
						F32 fModifiedRate = fSpeed * fRate;

						pact->fTimeActivating += fRate;
						pact->fTimerActivate -= fModifiedRate;

						if(pact->uiPeriod > 0)
						{
							pact->fTimeMaintained = MIN(ppowdef->fTimeMaintain, pact->fTimeMaintained + fModifiedRate);
						}

						if(ppowdef->fLifetimeUsage)
						{
							ppow->fLifetimeUsageUsed += fRate;
							pchar->bLimitedUseDirty = true;
						}
					}

					// Ready to fire the next period
					if(pact->fTimerActivate <= 0.0f)
					{
						// Note if we are planning on starting the next period
						bool bRefire = pchar->eChargeMode==kChargeMode_CurrentMaintain;

						// Change the period and activate timestamp to the next completion
						pact->uiTimestampActivatePeriodic = pmTimestampFrom(pact->uiTimestampActivate,pact->fTimeActivating + (pact->fTimerActivate / fSpeed));
						if(bRefire)
						{
							pact->uiPeriod++;
						}

						// Check if we've completed the entire allowed time
						if(bRefire && ppowdef->uiPeriodsMax && pact->uiPeriod > ppowdef->uiPeriodsMax)
						{
							bRefire = false;
							// Clamp the period so the deactivate timing is correct
							pact->uiPeriod = ppowdef->uiPeriodsMax;
						}

						// Check to see if the current target is still valid
						// Only check if it has an effect type of Character
						if(bRefire && eTarget && ppowdef->eEffectArea == kEffectArea_Character)
						{
							if(!character_TargetMatchesPowerType(iPartitionIdx,pchar,eTarget->pChar,GET_REF(ppowdef->hTargetMain))
								|| !character_CanPerceive(iPartitionIdx,pchar,eTarget->pChar))
							{
								bRefire = false;
							}

							// Check if the target is not in the firing arc anymore
							if(bRefire
								&& ppowdef->fTargetArc
								&& !entity_TargetInArc(pchar->pEntParent,eTarget,NULL,RAD(ppowdef->fTargetArc),ppow->fYaw))
							{
								bRefire = false;
							}

							if (bRefire 
								&& g_CombatConfig.bTestActDynamicEachPeriod
								&& !character_ActTestDynamic(iPartitionIdx, pchar, ppow, eTarget, pact->vecTargetSecondary, GET_REF(pact->hTargetObject), NULL, NULL, NULL, true,false,true))
							{
								bRefire = false;
							}

							
						}

						// Check if we can afford it
						if(bRefire)
						{
							if(!character_PayPowerCost(iPartitionIdx,pchar,ppow,pact->erTarget,pact,true,pExtract))
							{								
								ActivationFailureParams failureParams = { 0 };
								failureParams.pEnt = pchar->pEntParent;
								failureParams.pPow = ppow;

								// Couldn't pay for next tick, so we're done
								bRefire = false;

								character_ActivationFailureFeedback(kActivationFailureReason_Cost, &failureParams);
							}
						}

						if(bRefire)
						{
							if(entIsServer() || g_CombatConfig.bClientPredictMaintained)
							{
								// Apply the power
								bool bApplied;
								PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %s: Maintained apply period %d\n",ppowdef->pchName,pact->uiPeriod);
								bApplied = character_ApplyPower(iPartitionIdx,pchar,ppow,pact,pact->erTarget,pact->vecTarget,pact->vecTargetSecondary,CRAZY_MAINTAINED_PREDICTION?!pact->bPlayedActivate:true,0,pExtract,NULL);
																
								if (!bApplied && ppowdef->pExprRequiresApply)
								{	
									character_DeactivatePeriodicAnimFX(iPartitionIdx,pchar,pact,pmTimestamp(0));
									bDeactivate = true;
								}
								
								if(CRAZY_MAINTAINED_PREDICTION)
								{
									// Mark that we have not yet played the activate for the next period
									pact->bPlayedActivate = false;
								}
							}

							if(pact->uiPeriod==1)
							{
								// We just triggered our first period, add the 'over' time to the maintained timer
								//  so things terminate consistently
								pact->fTimeMaintained -= pact->fTimerActivate;
							}

							if(ppowdef->uiPeriodsMax && pact->uiPeriod == ppowdef->uiPeriodsMax)
							{
								// We happen to know thanks to the period limit that this is our last tick.
								//  In which case we can deactivate the art early, which means fewer mis-predictions.
								if (ppowdef->fTimePostMaintain == 0.f)
								{
									U32 uiTimeDeactivate = pmTimestampFrom(pact->uiTimestampActivate,pact->fTimeActivating + ((pact->fTimerActivate + ppowdef->fTimeActivatePeriod) / fSpeed));
									character_DeactivatePeriodicAnimFX(iPartitionIdx,pchar,pact,uiTimeDeactivate);
								}
								else
								{
									character_DeactivateMaintainedAnimFX(iPartitionIdx, pchar, pact, ppowdef);
								}
							}

							// Exception to our normal rule.  In the case of maintained which can
							//  only be run for a certain amount of time, there are going to be
							//  an expected number of periods.  To make sure all periods are hit
							//  we add the period to the timer instead of setting it.  This means
							//  you might get AttribMod overlap.
							if (!bDeactivate)
							{
								pact->fTimerActivate += ppowdef->fTimeActivatePeriod;
								PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %s: Maintained new timer (%f)\n",ppowdef->pchName,pact->fTimerActivate);
							}
						}
						else
						{
							// We want to deactivate now, either because we're not maintaining
							//  anymore, or we couldn't.
							bDeactivate = true;
						}
					}
					else if(CRAZY_MAINTAINED_PREDICTION && !bDeactivate && !pact->bPlayedActivate)
					{
						// Not beginning a new period, not deactivated, and haven't played activate for next period
						
						// If this only maintains for a certain length of time, and the next period would be over
						//  just mark it as played and move on
						if(ppowdef->uiPeriodsMax && pact->uiPeriod >= ppowdef->uiPeriodsMax)
						{
							//pact->bPlayedActivate = true;
						}
						else
						{
							// Make the activation look like it's one period in the future
							pact->bPlayedActivate = true;
							pact->uiPeriod++;
							pact->uiTimestampActivatePeriodic = pmTimestampFrom(pact->uiTimestampActivate,pact->fTimeActivating + ((pact->fTimerActivate + ppowdef->fTimeActivatePeriod) / fSpeed));

							// Play the activate
							character_AnimFXActivateOn(iPartitionIdx,pchar,NULL,pact,ppow,eTarget ? eTarget->pChar : NULL,pact->vecTarget,pact->uiTimestampActivatePeriodic,pact->uchID,pact->uiPeriod,0);

							// Move back to the previous period
							pact->uiPeriod--;
							pact->uiTimestampActivatePeriodic = pmTimestampFrom(pact->uiTimestampActivate,pact->fTimeActivating + (pact->fTimerActivate / fSpeed));
						}
					}

					// Handle deactivation
					if(bDeactivate)
					{
						U32 uiAnimTime = pmTimestampFrom(pact->uiTimestampActivate,pact->fTimeActivating + (pact->fTimerActivate / fSpeed));

						if (ppowdef->fTimePostMaintain == 0.f)
						{	// normal maintain deactivation, no post-maintain time
							PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %s: Maintained finished activation (%f %f)\n",ppowdef->pchName,pact->fTimeActivating,pact->fTimeMaintained);

							if(CRAZY_MAINTAINED_PREDICTION && pact->bPlayedActivate)
							{
								pact->uiPeriod++;
								character_AnimFXActivatePeriodicCancel(pchar,pact,eTarget?eTarget->pChar:NULL);
								pact->uiPeriod--;
							}

							character_DeactivateMaintained(iPartitionIdx, pchar, ppow, pact, pafx, uiAnimTime);
						
							character_ActCurrentFinish(iPartitionIdx,pchar,false);

							// If the Character was charging/maintaining this, drop to the none charge mode
							if(pchar->eChargeMode <= kChargeMode_Current)
							{
								pchar->eChargeMode = kChargeMode_None;
							}
						}
						else
						{
							character_ActMoveToPostMaintain(iPartitionIdx, pchar, ppow, ppowdef, pafx, pact, uiAnimTime);
						}
					}

					PERFINFO_AUTO_STOP();
				}
				else
				{	// kPowerType_Maintained power 
					devassert(pact->eActivationStage == kPowerActivationStage_PostMaintain);
					
					pact->fStageTimer -= fRate;
					if (pact->fStageTimer <= 0.f)
					{
						F32 fSpeed = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow);
						F32 fSecondsOffset = pact->fTimeActivating + (pact->fTimerActivate / fSpeed) + pact->fStageTimer;
						U32 uiAnimTime = pmTimestampFrom(pact->uiTimestampActivate, fSecondsOffset);

						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %s: Maintained finished activation (%f %f)\n",ppowdef->pchName,pact->fTimeActivating,pact->fTimeMaintained);

						character_DeactivateMaintained(iPartitionIdx, pchar, ppow, pact, pafx, uiAnimTime);

						// If the Character was charging/maintaining this, drop to the none charge mode
						if(pchar->eChargeMode <= kChargeMode_Current)
						{
							pchar->eChargeMode = kChargeMode_None;
						}

						character_ActCurrentFinish(iPartitionIdx,pchar,false);
					}
				}

			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void CharacterTickAttribPools(SA_PARAM_NN_VALID Character *pchar, F32 fRate, S32 bIsDead)
{
	// AttribPool-based regen
	if(g_iAttribPoolCount)
	{
		int i;

		PERFINFO_AUTO_START_FUNC();

		// Make sure we have the proper number of timers
		if(eafSize(&pchar->pfTimersAttribPool)!=g_iAttribPoolCount)
		{
			eafSetSize(&pchar->pfTimersAttribPool,g_iAttribPoolCount);
		}

		for(i=0; i<g_iAttribPoolCount; i++)
		{
			AttribPool *ppool = g_AttribPools.ppPools[i];

			if(!ppool->combatPool.pTarget)
				continue;

			if(ppool->bTickDisabledInCombat && pchar->uiTimeCombatExit)
			{
				pchar->pfTimersAttribPool[i] = ppool->combatPool.pTarget->fTimeTick;
				continue;
			}

			if(bIsDead && !ppool->bTickWhileDead)
				continue;

			{
				// TODO(JW): Optimize: I'm sure this could be made smarter (eg if target unit is
				//  absolute and we're not clamping we don't need min and max), but for now
				//  I'm just going to do the full call.
				F32 *pfCur = F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribCur);
				F32 fMin = ppool->eAttribMin ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribMin) : 0;
				F32 fMax = ppool->eAttribMax ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribMax) : 0;
				F32 fTarget = ppool->eAttribTarget ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribTarget) : 0;
				F32 fRegenRate = ppool->eAttribRegenRate ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribRegenRate) : 1;
				F32 fRegenMag = ppool->eAttribRegenMag ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribRegenMag) : 1;
				F32 fDecayRate = ppool->eAttribDecayRate ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribDecayRate) : 1;
				F32 fDecayMag = ppool->eAttribDecayMag ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribDecayMag) : 1;
				
				S32 bReachedTarget = combatpool_Tick(	&ppool->combatPool,
														&(pchar->pfTimersAttribPool[i]),
														pfCur,
														fRate,
														fMin,
														fMax,
														fTarget,
														fRegenRate,
														fRegenMag,
														fDecayRate,
														fDecayMag);

				// if we haven't reached our target, make sure we wake up to process the next tick
				if (!bReachedTarget) 
					character_SetSleep(pchar,pchar->pfTimersAttribPool[i]);
			}

		}

		PERFINFO_AUTO_STOP();
	}
}



// Causes the character to regenerate hp and end
static void CharacterTickRegen(SA_PARAM_NN_VALID Character *pchar, F32 fRate)
{
	F32 fPowerEquilibrium = pchar->pattrBasic->fPowerEquilibrium;
	F32 fPowerDelta = fPowerEquilibrium - pchar->pattrBasic->fPower;
	F32 fRegened = 0;
	bool bRecover = (fPowerDelta >= 0.0f);

	PERFINFO_AUTO_START_FUNC();

// These numbers are arbitrary but important.  You don't want period so small that
//  you frequently get multi-period ticks.  On the other hand, you don't want the
//  amount healed per period to be extremely large, some granularity is nice.
#define REGEN_PERIOD_HP 2.0
#define REGEN_AMOUNT_HP (REGEN_PERIOD_HP/60.0f)
#define REGEN_PERIOD_END 2.0
#define REGEN_AMOUNT_END (REGEN_PERIOD_END/60.0f)

	// Do timers first?
	// Need to try to keep the timer vs event consistent?
	// Timers first means a long tick will immediately trigger large regen, rather
	//  than delaying it another tick.  Also means that freshly loaded characters
	//  will get a tick of regen immediately if fRate>0, rather than one tick later.

	// HP
	if(!pchar->pNearDeath)
	{
		F32 fRateScale = MAX(0.0f,pchar->pattrBasic->fRegeneration);
		pchar->fTimerRegeneration -= fRate * fRateScale;
		while(pchar->fTimerRegeneration < 0.0f)
		{
			pchar->fTimerRegeneration += REGEN_PERIOD_HP;
			if(pchar->bCanRegen[0])
			{
				pchar->pattrBasic->fHitPoints += REGEN_AMOUNT_HP * pchar->pattrBasic->fHitPointsMax;
				fRegened += REGEN_AMOUNT_HP * pchar->pattrBasic->fHitPointsMax;
			}
		}

		if(pchar->pattrBasic->fHitPoints < pchar->pattrBasic->fHitPointsMax && fRateScale)
			character_SetSleep(pchar,pchar->fTimerRegeneration/fRateScale);
	}

	character_DamageTrackerDecay(pchar,fRegened);

	// Skip Power if there's an AttribPool defined for it
	if(!g_bAttribPoolPower)
	{
		F32 fRateScale;
		// Power works special
		if(bRecover)
		{
			fRateScale = MAX(0.0f,pchar->pattrBasic->fPowerRecovery);
			pchar->fTimerRecovery -= fRate * fRateScale;
		}
		else
		{
			fRateScale = MAX(0.0f,pchar->pattrBasic->fPowerDecay);
			pchar->fTimerRecovery -= fRate * fRateScale;
		}

		while(pchar->fTimerRecovery < 0.0f)
		{
			F32 fAmount = REGEN_AMOUNT_END * pchar->pattrBasic->fPowerMax;

			pchar->fTimerRecovery += REGEN_PERIOD_END;

			if(bRecover)
			{
				if(fAmount > fPowerDelta) fAmount = fPowerDelta;
			}
			else
			{
				fAmount *= -1.0f;
				if(fAmount < fPowerDelta) fAmount = fPowerDelta;
			}
			if(pchar->bCanRegen[1]) pchar->pattrBasic->fPower += fAmount;
			fPowerDelta = fPowerEquilibrium - pchar->pattrBasic->fPower;
		}

		if(fPowerDelta!=0 && fRateScale)
			character_SetSleep(pchar,pchar->fTimerRecovery/fRateScale);
	}

	// Michaels Check for stopping crazy regen crisis
	if(pchar->pattrBasic->fHitPoints > pchar->pattrBasic->fHitPointsMax)
		pchar->pattrBasic->fHitPoints = pchar->pattrBasic->fHitPointsMax;

	CharacterTickAttribPools(pchar, fRate, false);

	PERFINFO_AUTO_STOP();
}


// Accumulates incoming trackers into the main tracker, update net trackers, does not destroy the incoming list
static void CharacterTickDamageTracker(int iPartitionIdx, Character *pchar, CharacterAttribs *pattrOld)
{
#ifdef GAMESERVER
	int s=eaSize(&pchar->ppDamageTrackersTickIncoming);
	if(s)
	{
		int i;
		F32 fCreditDamage=1, fCreditHeal=1;
		PERFINFO_AUTO_START_FUNC();

		character_CombatTrackerListTouch(pchar);

		// Check if we need to check for overkill
		if(pchar->pattrBasic->fHitPoints<=0)
		{
			F32 fLimitDamage = pattrOld->fHitPoints;
			F32 fTrackedDamage = 0;
			for(i=0; i<s; i++)
			{
				DamageTracker * dt = pchar->ppDamageTrackersTickIncoming[i];
				fTrackedDamage += dt->fDamage;
			}
			
			// If the tracked damage results in more than what was possible from last tick
			if(fTrackedDamage>fLimitDamage)
			{
				fCreditDamage = fLimitDamage / fTrackedDamage;
			}
		}
		else if(pchar->pattrBasic->fHitPoints >= pchar->pattrBasic->fHitPointsMax)
		{
			// Same as above, but for overheal
			F32 fLimitHeal = pattrOld->fHitPointsMax - pattrOld->fHitPoints;
			F32 fTrackedHeal = 0;
			for(i=0; i<s; i++)
			{
				DamageTracker * dt = pchar->ppDamageTrackersTickIncoming[i];
				fTrackedHeal -= dt->fDamage;
			}

			if(fTrackedHeal>fLimitHeal)
			{
				fCreditHeal = fLimitHeal / fTrackedHeal;
			}
		}

		// Same death test that happens later, just copied in here so we can mark the appropriate damage
		//  tracker as a kill before we build CombatTrackerNets out of them
		if(pchar->bKill || pchar->pattrBasic->fHitPoints<=0.0f)
		{
			if(entIsAlive(pchar->pEntParent) && !pchar->bUnkillable)
			{
				damagetracker_MarkKillingBlow(pchar);
			}
		}

		for(i=0; i<s; i++)
		{
			F32 fThreatScale = 1;
			F32 fThreatScaledDamage = 0;
			DamageTracker * dt = pchar->ppDamageTrackersTickIncoming[i];
			Entity *pOwnerEnt = entFromEntityRef(iPartitionIdx, dt->erOwner);
			Entity *pSourceEnt = entFromEntityRef(iPartitionIdx, dt->erSource);
			Entity *pAggroEnt = NULL;
			GameEncounter *pEncounter;

			// If this naturally did 0, or it did non-zero and credit was still awarded, create a combat tracker for it
			if(!dt->fDamage
				|| (dt->fDamage>0 && fCreditDamage>0)
				|| (dt->fDamage<0 && (fCreditHeal>0 || g_CombatConfig.bSendCombatTrackersForOverhealing)))
			{
				eaPush(&pchar->combatTrackerNetList.ppEvents,damageTracker_BuildCombatTrackerNet(dt, pOwnerEnt));
			}

			// Commented out by jpanttaja to reduce log load
			//entLog(LOG_COMBAT,pchar->pEntParent,"Damaged","Damage %f, Type %d, Owner %d",dt->fDamage,dt->eDamageType,dt->erOwner);


#ifdef	GAMESERVER
			PERFINFO_AUTO_START("AINotify",1);
			if(pOwnerEnt && pOwnerEnt->pChar)
			{
				// TODO(JW): This is lazier than it should be (probably should be calculated at
				//  mod apply time), but we'll ignore that for now.
				fThreatScale = pOwnerEnt->pChar->pattrBasic->fAIThreatScale;
			}

			pAggroEnt = aiDetermineAggroEntity(pchar->pEntParent, pSourceEnt, pOwnerEnt);

			fThreatScaledDamage = fThreatScale * dt->fDamage;
			if(dt->fDamage<0)
				aiNotify(pchar->pEntParent, pAggroEnt, AI_NOTIFY_TYPE_HEALING, -fThreatScaledDamage, -fThreatScaledDamage * fCreditHeal, NULL, dt->uiApplyID);
			else
				aiNotify(pchar->pEntParent, pAggroEnt, AI_NOTIFY_TYPE_DAMAGE, fThreatScaledDamage, fThreatScaledDamage * fCreditDamage, NULL, dt->uiApplyID);
			PERFINFO_AUTO_STOP();
#endif

			if(dt->fDamage<=0)
			{
				eventsend_RecordHealing(pchar->pEntParent, pOwnerEnt, (int)(-dt->fDamage*fCreditHeal), StaticDefineIntRevLookup(AttribTypeEnum, dt->eDamageType));
				continue;
			}

			// Add this to the longterm tracking
			character_DamageTrackerAccum(iPartitionIdx, pchar, dt, fCreditDamage);

			PERFINFO_AUTO_START("EventLog Damage",1);
			eventsend_RecordDamage(pchar->pEntParent, pOwnerEnt, (int)(dt->fDamage*fCreditDamage), StaticDefineIntRevLookup(AttribTypeEnum, dt->eDamageType));
			PERFINFO_AUTO_STOP();

			// Temporary - Give player and all team member's credit for this encounter
			pEncounter = SAFE_MEMBER3(pchar, pEntParent, pCritter, encounterData.pGameEncounter);
			if (pEncounter && pOwnerEnt)
			{
				GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
				// Give credit to damaging player
				eaiPushUnique(&pState->playerData.eauEntsWithCredit, pOwnerEnt->myRef);
				// Also add all nearby teammates
				if (team_IsMember(pOwnerEnt))
				{
					Entity*** proximatePlayers = encounter_GetNearbyPlayers(iPartitionIdx, pEncounter, -1);
					int j, m = eaSize(proximatePlayers);
					for (j = 0; j < m; j++)
						if (team_OnSameTeam((*proximatePlayers)[j], pOwnerEnt))
							eaiPushUnique(&pState->playerData.eauEntsWithCredit, (*proximatePlayers)[j]->myRef);
				}
			}
			if (gConf.bAllowOldEncounterData)
			{
				OldEncounter *pOldEncounter = NULL;
				pOldEncounter = SAFE_MEMBER3(pchar, pEntParent, pCritter, encounterData.parentEncounter);
				if (pOldEncounter && pOwnerEnt)
				{
					// Give credit to damaging player
					eaiPushUnique(&pOldEncounter->entsWithCredit, pOwnerEnt->myRef);
					// Also add all nearby teammates
					if (team_IsMember(pOwnerEnt))
					{
						EncounterDef *def = oldencounter_GetDef(pOldEncounter);
						Entity*** proximatePlayers = oldencounter_GetNearbyPlayers(pOldEncounter, def->spawnRadius);
						int j, m = eaSize(proximatePlayers);
						for (j = 0; j < m; j++)
							if (team_OnSameTeam((*proximatePlayers)[j], pOwnerEnt))
								eaiPushUnique(&pOldEncounter->entsWithCredit, (*proximatePlayers)[j]->myRef);
					}
				}
			}

			if(pchar->pEntParent && pOwnerEnt!=pchar->pEntParent && entGetType(pchar->pEntParent)==GLOBALTYPE_ENTITYPLAYER)
			{
				Entity* playerEnt = pchar->pEntParent;

				// If a player was hit, also check if they're interacting
				if(playerEnt->pPlayer && (interaction_IsPlayerInteracting(playerEnt) || interaction_IsPlayerInDialog(playerEnt)) && (playerEnt->pPlayer->InteractStatus.bInteractBreakOnDamage))
				{
					interaction_EndInteractionAndDialog(iPartitionIdx, pchar->pEntParent, false, true, true);
				}
			}
		}
		PERFINFO_AUTO_STOP();
	}
#endif
}

// Safety mechanism to prevent the player from getting stuck in the waiting to roll state
#define CHARACTER_WAITING_TO_ROLL_TIMEOUT 10.0f
__forceinline static void character_CheckWaitingToRoll(Character *pchar, F32 fRate)
{
	if (pchar->bIsWaitingToRoll)
	{
		pchar->fRollWaitingTimer += MINF(fRate, 1.0f);
		if (pchar->fRollWaitingTimer >= CHARACTER_WAITING_TO_ROLL_TIMEOUT)
		{
			ErrorDetailsf("Char %s", CHARDEBUGNAME(pchar));
			Errorf("Character was stuck waiting to roll for too long!");
			pchar->bIsWaitingToRoll = false;
			pchar->fRollWaitingTimer = 0.0f;
		}
	}
}

void character_TickPrePhaseOne(int iPartitionIdx, Character *pchar)
{
#ifdef GAMESERVER
	// checking combat advantage
	if(g_CombatConfig.pCombatAdvantage && pchar->uiTimeCombatExit)
	{
		F32 fUntil = pmTimeUntil(pchar->uiTimeCombatExit);
		if(fUntil > 0.0f)
		{
			gslCombatAdvantage_CalculateFlankingForEntity(iPartitionIdx, pchar->pEntParent);
		}
	}
#endif
}

// Does all the character processing before attrib mods are accumulated
void character_TickPhaseOne(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();

	// Server-specific pre-updates
	if(entIsServer())
	{
		if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
		{
#ifdef GAMESERVER
			// this only really needs to be done for players at the moment
			gslArmamentSwapUpdate(pchar->pEntParent);
#endif
		}

		// Update combat level
		character_LevelCombatUpdate(iPartitionIdx, pchar, false, pExtract);

		// Requested a reset of the Powers array
		if(pchar->bResetPowersArray)
		{
			character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
		}
	}

	// Generic pre-updates
	{
		// Movement interrupt check (for non-voluntary moves) and roll cancel check

		// I'm only doing this on the gameserver now, because the client will sometimes guess wrong in the case that
		// the move happened right at the end of the casting.  The fancier way to handle this would be to allow this
		// prediction to occur on the client, but then have the server inform the client when his attack had not
		// actually been canceled, but that will probably be "too late".  [RMARR - 8/26/10]
#ifdef GAMESERVER
		PowerActivation *pact = pchar->pPowActCurrent;
		if (pact)
		{
			Vec3 vecCurrentPos;
			F32 fRadius = g_CombatConfig.fPowerCancelMoveRadius;

			if (fRadius < 0.0f)
				fRadius = 0.0f;

			entGetPos(pchar->pEntParent,vecCurrentPos);

			if(DISTSQRD3(vecCurrentPos,pact->vecCharStartPos) > fRadius*fRadius)
			{
				// this guy has moved 
				character_ActInterrupt(iPartitionIdx,pchar,kPowerInterruption_Movement);
			}
		}
#endif

#ifdef GAMESERVER
		//Check to see if the person moved, and if so, cancel the current logoff attempt for movement reason
		{
			Vec3 vecVelocity;
			entCopyVelocityFG(pchar->pEntParent,vecVelocity);
			if(!ISZEROVEC3(vecVelocity))
			{
				gslLogoff_Cancel(pchar->pEntParent, kLogoffCancel_Movement);
			}
		}
#endif

#ifdef GAMESERVER
		gslUpdateActiveSlotRequests(pchar->pEntParent, fRate, pExtract);
#elif GAMECLIENT
		gclUpdateActiveSlotRequests(pchar->pEntParent, fRate, pExtract);
#endif
		// Check the waiting to roll state on the client and server
		character_CheckWaitingToRoll(pchar, fRate);
	}

	if (!entIsProjectile(pchar->pEntParent))
	{
		CharacterTickCombat(iPartitionIdx, pchar,fRate, pExtract);
	}
	
	CombatPowerStateSwitching_Update(pchar, fRate);


	CharacterTickRecharge(iPartitionIdx, pchar, fRate, 0);

	CharacterTickCooldown(pchar, fRate);

	CharacterTickChargeRefill(iPartitionIdx, pchar, fRate);

	CharacterTickFinished(iPartitionIdx,pchar,fRate);

	CharacterTickQueue(iPartitionIdx, pchar, fRate);

	CharacterTickCurrent(iPartitionIdx,pchar,fRate, pExtract);

	CharacterTickPassive(iPartitionIdx, pchar, fRate, pExtract);

	CharacterTickInstant(iPartitionIdx, pchar, fRate, pExtract);

	CharacterTickTactical(pchar);

	CombatReactivePower_Update(pchar, fRate);

	// Toggle is after current so they can apply immediately after finishing activation
	CharacterTickToggle(iPartitionIdx,pchar,fRate,pExtract);

	if(entIsAlive(pchar->pEntParent))
	{
		CharacterTickAutoAttackServer(iPartitionIdx, pchar, fRate, pExtract);
	}

	if(gConf.bItemArt && pchar->pEntParent->pEquippedArt)
	{
		pchar->pEntParent->pEquippedArt->bCanUpdate = true;//reset the flag that ensures each pChar can only have their itemart updated once per frame.
	}

	CharacterTickLifetime(pchar,fRate, pExtract);

	PERFINFO_AUTO_STOP();
}

// Static CharacterAttribs struct to save a copy of a character's attribs before they're
//  adjusted by TickPhaseTwo.
static CharacterAttribs s_CharacterAttribsOld = {0};

// Does all the accrual of mods and post-processing
// should only ever be called on the server. 
void character_TickPhaseTwo(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
#ifndef GAMESERVER
	devassert(0);
#elif GAMESERVER
	static AttribMod **ppOldCostumeChanges = NULL;
	static AttribMod **ppOldCostumeModifies = NULL;
	static AttribMod **ppOldRewardModifies = NULL;
	pchar->bBecomeCritterTickPhaseTwo = pchar->bBecomeCritter;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Top",1);

	// Apply powers from auto-execute items
	if (eaSize(&pchar->ppAutoExecItems))
	{
		int i;
		for(i = eaSize(&pchar->ppAutoExecItems)-1; i >= 0; i--)
		{
			character_ApplyUnownedPowersFromItem(iPartitionIdx, pchar, pchar->ppAutoExecItems[i], pExtract);
			StructDestroy(parse_Item, eaRemove(&pchar->ppAutoExecItems, i));
		}
	}

	// Powers might have expired from CharacterTickLifetime (or some horrible out-of-band change
	//  between TickPhaseOne and TickPhaseTwo) so we need to check the reset array request here
	//  again in case by some circumstance we need to redo innate accrual or some other
	//  ppPowers-based work.
	if(pchar->bResetPowersArray)
		character_ResetPowersArray(iPartitionIdx, pchar, pExtract);

	PERFINFO_AUTO_START("CopyAttributes",1);
	// Copy the old attribs, to be checked later to see what changed
	memcpy(&s_CharacterAttribsOld,pchar->pattrBasic,g_iCharacterAttribSizeUsed);
	PERFINFO_AUTO_STOP();

	character_CacheAttribMods(pchar, &ppOldCostumeChanges, &ppOldCostumeModifies, &ppOldRewardModifies);

	// Set the BecomeCritter flag to false, will be set to true by any valid BecomeCritter AttribMods
	pchar->bBecomeCritter = false;

	// Reset the CombatTrackerNetList
	// If the net list isn't dirty, that means it's been sent
	// If it's been sent, but has an earray, destroy the earray and mark it shallow dirty
	pchar->combatTrackerNetList.bTouched = 0;
	if(!pchar->combatTrackerNetList.bDirty && eaSize(&pchar->combatTrackerNetList.ppEvents))
	{
		PERFINFO_AUTO_START("ResetCombatTrackerNetList",1);
#ifdef GAMESERVER
		// If CombatLogServer is on, log all these now
		if(g_bCombatLogServer)
		{
			EntityRef er = entGetRef(pchar->pEntParent);
			S32 i,s=eaSize(&pchar->combatTrackerNetList.ppEvents);
			for(i=0; i<s; i++)
				combattracker_CombatLog(pchar->combatTrackerNetList.ppEvents[i],er);
		}
#endif
		eaDestroyStruct(&pchar->combatTrackerNetList.ppEvents,parse_CombatTrackerNet);
		entity_SetDirtyBit(pchar->pEntParent,parse_CombatTrackerNetList,&pchar->combatTrackerNetList,false);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		PERFINFO_AUTO_STOP();
	}

#ifdef GAMESERVER
	// Regen uses the old basic attributes for calculations, applies the results to new health attribute
	if(entIsAlive(pchar->pEntParent))
		CharacterTickRegen(pchar,fRate);
	else
		CharacterTickAttribPools(pchar, fRate, true); // Only run the pools set to run when dead
#endif

	PERFINFO_AUTO_STOP(); // Top

	// Accrue the Character's AttribMods to determine the basic attribute values and other state of the Character
	character_AccrueMods(iPartitionIdx,pchar,fRate,pExtract);

	PERFINFO_AUTO_START("Middle",1);

	// Update the state of conditional bits/FX on the AttribMods
	character_UpdateConditionalModAnimFX(iPartitionIdx,pchar);

	// If the Character uses stats, check if they're dirty, and if they are, clean the old ones out.  That
	//  will result in an update next tick.
	if(g_CombatConfig.bCritterStats || entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		CharacterClass *pClass = character_GetClassCurrent(pchar);
		PowerStat **ppStats = (pClass && pClass->ppStatsFull) ? pClass->ppStatsFull : g_PowerStats.ppPowerStats;
		if(powerstats_CheckDirty(ppStats,&s_CharacterAttribsOld,pchar->pattrBasic))
		{
			character_DirtyInnateAccrual(pchar);
		}
	}

	// Check for any AutoReapply updates
	if(pchar->bAutoReapplyPassives)
	{
		S32 i;
		for(i=eaSize(&pchar->ppPowerActPassive)-1; i>=0; i--)
			character_ActCheckAutoReapply(pchar,pchar->ppPowerActPassive[i],&s_CharacterAttribsOld,pchar->pattrBasic);
	}
	if(pchar->bAutoReapplyToggles)
	{
		S32 i;
		for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			character_ActCheckAutoReapply(pchar,pchar->ppPowerActToggle[i],&s_CharacterAttribsOld,pchar->pattrBasic);
	}

	if(fRate > 0)
	{
		// Accumulate the incoming damage trackers
		CharacterTickDamageTracker(iPartitionIdx,pchar,&s_CharacterAttribsOld);

		// Update any buffered combat trackers
		character_CombatTrackerBufferTick(iPartitionIdx, pchar, fRate);

		// Check for Power attribute emptied CombatEvent
		if(pchar->pattrBasic->fPower <= 0.f && s_CharacterAttribsOld.fPower > 0.0f)
			character_CombatEventTrack(pchar,kCombatEvent_AttribPowerEmptied);
	}

#ifdef GAMESERVER
	gslCombatDeathPrediction_DeathPredictionTick(iPartitionIdx, pchar);
#endif

	// Death, neardeath and undeath
	if(pchar->bKill || pchar->pattrBasic->fHitPoints<=0.0f)
	{
		S32 bDie = entIsAlive(pchar->pEntParent) && !pchar->bUnkillable;

		if(bDie && !pchar->bKill)
		{
			CharacterClass *pClass = character_GetClassCurrent(pchar);
			if(pchar->pNearDeath)
			{
				if(pchar->pNearDeath->fTimer!=POWERS_FOREVER && 
					!eaiSize(&pchar->pNearDeath->perFriendlyInteracts))
				{
					pchar->pNearDeath->fTimer -= fRate;
					entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,false);
					bDie = pchar->pNearDeath->fTimer <= 0;
				}
				else
				{
					bDie = false;
				}
			}
			else if(pClass->pNearDeathConfig && character_MeetsNearDeathRequirements(pchar, pClass->pNearDeathConfig))
			{
				bDie = false;
				character_NearDeathEnter(iPartitionIdx, pchar, 0.f);
			}
		}

		if(bDie)
		{
			character_NearDeathExpire(pchar, pExtract);
		}
		else
		{
			if (!pchar->pNearDeath && g_CombatConfig.fOnDeathCapsuleLingerTime > 0.f)
			{
				character_UpdateDeathCapsuleLinger(pchar->pEntParent);
			}
		}

		pchar->bKill = false;
	}
	else
	{
		if(!entIsAlive(pchar->pEntParent))
		{
			PERFINFO_AUTO_START("Undeath",1);

			// Clear the dead flag
			entClearCodeFlagBits(pchar->pEntParent,ENTITYFLAG_DEAD);

			// Animate undeath
			entity_DeathAnimationUpdate(pchar->pEntParent,false,pmTimestamp(0));

			// Update ItemArt now that they're no longer dead
			entity_UpdateItemArtAnimFX(pchar->pEntParent);

			// Make sure character gets full processing next tick
			pchar->bSkipAccrueMods = false;

			// Switch to alive passives
			character_RefreshPassives(iPartitionIdx,pchar,pExtract);

			// Inform AI
#ifdef GAMESERVER
			if(!entCheckFlag(pchar->pEntParent, ENTITYFLAG_DESTROY))
				aiOnUndeath(pchar->pEntParent);
#endif

			PERFINFO_AUTO_STOP();
		}

		// Remove any lingering NearDeath if they've got hitpoints
		if(pchar->pNearDeath)
		{
			character_NearDeathRevive(pchar);
		}
	}

	if(pchar->erRingoutCredit && !pmKnockIsActive(pchar->pEntParent) && mrSurfaceGetOnGround(pchar->pEntParent->mm.mrSurface))
		pchar->erRingoutCredit = 0;

	// Clamps just to be safe
	MAX1(pchar->pattrBasic->fHitPoints,0.f);
	MAX1(pchar->pattrBasic->fPower,0.f);
	character_AttribPoolsClamp(pchar);

	// Clear the incoming damage trackers
	if(fRate > 0)
		eaDestroyEx(&pchar->ppDamageTrackersTickIncoming,damageTrackerDestroy);

	// Unstoppable!
	if(pchar->bUnstoppable || pchar->bUsingDoor)
	{
		pchar->pattrBasic->fRoot = character_GetClassAttrib(pchar, kClassAttribAspect_Basic, kAttribType_Root);
		pchar->pattrBasic->fHold = character_GetClassAttrib(pchar, kClassAttribAspect_Basic, kAttribType_Hold);
		pchar->pattrBasic->fDisable = character_GetClassAttrib(pchar, kClassAttribAspect_Basic, kAttribType_Disable);
	}

	PERFINFO_AUTO_STOP(); // Middle

	PERFINFO_AUTO_START("AttributeChanges",1);

	// used by Root and Holds, the time after the attrib falls off that we schedule stops with the movement
	// this is to allow time for players to better predict it
#define ROOT_MOVEMENT_FALLOFF_TIME	0.1f

	// Root
	if(pchar->pattrBasic->fRoot > 0.0f)
	{	
		// we are currently rooted, check if the root is new and if it is scheduled
		if(s_CharacterAttribsOld.fRoot <= 0.0f)
		{
			if (!pchar->uiScheduledRootTime)
			{
				pchar->uiScheduledRootTime = pmTimestamp(0);
				PowersDebugPrintEnt(EPowerDebugFlags_ROOT, pchar->pEntParent, "ERROR: New root but no queued time.");
			}

			if (!pchar->bIsRooted && entIsPlayer(pchar->pEntParent))
			{
				ClientCmd_PowersPredictRoot(pchar->pEntParent, entGetRef(pchar->pEntParent), pchar->uiScheduledRootTime);
			}
		
			// schedule the rooting with the different requesters
			character_GenericRoot(pchar, true, pchar->uiScheduledRootTime);
		}
		
		// not considered rooted yet, check the scheduled time and then do all the held game actions if time has passed
		if (!pchar->bIsRooted)
		{
			U32 uiCurTime = pmTimestamp(0);
			if (uiCurTime > pchar->uiScheduledRootTime)
			{
				pchar->bIsRooted = true;
				pchar->uiScheduledRootTime = 0;
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				character_CombatEventTrack(pchar, kCombatEvent_RootedStart);
			}
		}
	}
	else
	{
		// we aren't rooted, check if the attrib used to be on us
		if(s_CharacterAttribsOld.fRoot > 0.0f)
		{
			// schedule the movement root to come off shortly, the rest will just be processed now
			U32 uiStopRootTime = pmTimestamp(ROOT_MOVEMENT_FALLOFF_TIME);

			if (pchar->bIsRooted && entIsPlayer(pchar->pEntParent))
			{
				ClientCmd_PowersPredictRootStop(pchar->pEntParent, entGetRef(pchar->pEntParent), uiStopRootTime);
			}
			
			pchar->bIsRooted = false;
			pchar->uiScheduledRootTime = 0;
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

			character_GenericRoot(pchar, false, uiStopRootTime);
			character_CombatEventTrack(pchar, kCombatEvent_RootedStop);
		}
	}

	// Hold 
	if(pchar->pattrBasic->fHold > 0.0f)
	{
		if(s_CharacterAttribsOld.fHold <= 0.0f)
		{
			// this is a new hold on the character, schedule the hold time 
			if (!pchar->uiScheduledHoldTime)
			{
				pchar->uiScheduledHoldTime = pmTimestamp(0);
				PowersDebugPrintEnt(EPowerDebugFlags_ROOT, pchar->pEntParent, "ERROR: New hold but no queued time.");
			}

			if (!pchar->bIsHeld && entIsPlayer(pchar->pEntParent))
			{
				ClientCmd_PowersPredictHold(pchar->pEntParent, entGetRef(pchar->pEntParent), pchar->uiScheduledHoldTime);
			}

			// schedule Lockdown
			character_GenericHold(pchar, true, pchar->uiScheduledHoldTime);
		}

		// not considered held yet, check the scheduled time and then do all the held game actions if time has passed
		if (!pchar->bIsHeld)
		{
			U32 uiCurTime = pmTimestamp(0);
			if (uiCurTime > pchar->uiScheduledHoldTime)
			{
				pchar->bIsHeld = true;
				pchar->uiScheduledHoldTime = 0;
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

				// Cancel everything
				character_ActAllCancelReason(iPartitionIdx,pchar,false, kAttribType_Hold);

				// Drop held objects
				character_DropHeldObjectOnTarget(iPartitionIdx,pchar,0,pExtract);
				// Break interaction
				if(pchar->pEntParent && pchar->pEntParent->pPlayer && (interaction_IsPlayerInteracting(pchar->pEntParent) || interaction_IsPlayerInDialog(pchar->pEntParent)) && (pchar->pEntParent->pPlayer->InteractStatus.bInteractBreakOnDamage))
				{
					interaction_EndInteractionAndDialog(iPartitionIdx, pchar->pEntParent, false, true, true);
				}

				character_CombatEventTrack(pchar,kCombatEvent_HeldStart);
			}
		}
	}
	else
	{
		if(s_CharacterAttribsOld.fHold > 0.0f)
		{
			// schedule the movement root to come off shortly, the rest will just be processed now
			U32 uiStopHoldTime = pmTimestamp(ROOT_MOVEMENT_FALLOFF_TIME);

			pchar->bIsHeld = false;
			pchar->uiScheduledHoldTime = 0;
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

			if (pchar->bIsHeld && entIsPlayer(pchar->pEntParent))
			{
				ClientCmd_PowersPredictHoldStop(pchar->pEntParent, entGetRef(pchar->pEntParent), uiStopHoldTime);
			}

			// Stop lockdown
			character_GenericHold(pchar, false, uiStopHoldTime);
		}
	}

	// Disable
	if(pchar->pattrBasic->fDisable > 0.0f)
	{
		if(s_CharacterAttribsOld.fDisable <= 0.0f)
		{
			// Cancel everything
			character_ActAllCancelReason(iPartitionIdx,pchar,false, kAttribType_Disable);

			// Drop held objects
			character_DropHeldObjectOnTarget(iPartitionIdx,pchar,0,pExtract);

			// Break interaction
			if(pchar->pEntParent && pchar->pEntParent->pPlayer && (interaction_IsPlayerInteracting(pchar->pEntParent) || interaction_IsPlayerInDialog(pchar->pEntParent)) && (pchar->pEntParent->pPlayer->InteractStatus.bInteractBreakOnDamage))
			{
#ifdef GAMESERVER
				interaction_EndInteractionAndDialog(iPartitionIdx, pchar->pEntParent, false, true, true);
#else
				interaction_ClearPlayerInteractState(pchar->pEntParent);
#endif
			}

			character_CombatEventTrack(pchar,kCombatEvent_DisabledStart);
			if (g_CombatConfig.tactical.bAimDisableDuringPowerDisableAttrib || g_CombatConfig.tactical.bRollDisableDuringPowerDisableAttrib)
			{
				TacticalDisableFlags flags = 0;
				if (g_CombatConfig.tactical.bRollDisableDuringPowerDisableAttrib)
					flags |= TDF_ROLL;
				if (g_CombatConfig.tactical.bAimDisableDuringPowerDisableAttrib)
					flags |= TDF_AIM;
				
				mrTacticalNotifyPowersStart(pchar->pEntParent->mm.mrTactical, TACTICAL_DISABLE_UID, flags, pmTimestamp(0));
			}
		}
	}
	else
	{
		if(s_CharacterAttribsOld.fDisable > 0.0f)
		{
			character_CombatEventTrack(pchar,kCombatEvent_DisabledStop);
			if (g_CombatConfig.tactical.bAimDisableDuringPowerDisableAttrib || g_CombatConfig.tactical.bRollDisableDuringPowerDisableAttrib)
				mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical, TACTICAL_DISABLE_UID, pmTimestamp(0) );
		}
	}

	// Confuse
	if(pchar->pattrBasic->fConfuse > 0.0f)
	{
		
		if(pchar->uiConfuseSeed == 0)
		{
			// Sets the seed used for generating random numbers
			pchar->uiConfuseSeed = timeSecondsSince2000();
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}

		// Break interaction
		if(pchar->pEntParent && pchar->pEntParent->pPlayer && (interaction_IsPlayerInteracting(pchar->pEntParent) || interaction_IsPlayerInDialog(pchar->pEntParent)) && (pchar->pEntParent->pPlayer->InteractStatus.bInteractBreakOnDamage))
		{
#ifdef GAMESERVER
			interaction_EndInteractionAndDialog(iPartitionIdx, pchar->pEntParent, false, true, true);
#else
			interaction_ClearPlayerInteractState(pchar->pEntParent);
#endif
		}
	}
	else
	{
		if(pchar->uiConfuseSeed != 0)
		{
			pchar->uiConfuseSeed = 0;
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
	}

	// Stealth change
	if(s_CharacterAttribsOld.fAggroStealth != pchar->pattrBasic->fAggroStealth
		|| s_CharacterAttribsOld.fPerceptionStealth != pchar->pattrBasic->fPerceptionStealth)
	{
		entSetActive(pchar->pEntParent);
	}

	// fSpeedRecharge change
	if(s_CharacterAttribsOld.fSpeedRecharge != pchar->pattrBasic->fSpeedRecharge
		&& pchar->pEntParent->erOwner)
	{
		// Find the owning Player, find this pet's info, and update its fAttribSpeedRecharge
		Entity *pentOwner = entFromEntityRef(iPartitionIdx,pchar->pEntParent->erOwner);
		if(pentOwner && pentOwner->pPlayer && entCheckFlag(pentOwner,ENTITYFLAG_IS_PLAYER))
		{
			int i;
			EntityRef erPet = entGetRef(pchar->pEntParent);
			for(i=eaSize(&pentOwner->pPlayer->petInfo)-1; i>=0; i--)
			{
				if(pentOwner->pPlayer->petInfo[i]->iPetRef==erPet)
				{
					pentOwner->pPlayer->petInfo[i]->fAttribSpeedRecharge = pchar->pattrBasic->fSpeedRecharge;
					entity_SetDirtyBit(pentOwner,parse_Player,pentOwner->pPlayer,false);
					break;
				}
			}
		}
	}

	// fSpeedCooldown change
	if(s_CharacterAttribsOld.fSpeedCooldown != pchar->pattrBasic->fSpeedCooldown
		&& pchar->pEntParent->erOwner)
	{
		// Find the owning Player, find this pet's info, and update its fAttribSpeedCooldown
		Entity *pentOwner = entFromEntityRef(iPartitionIdx,pchar->pEntParent->erOwner);
		if(pentOwner && pentOwner->pPlayer && entCheckFlag(pentOwner,ENTITYFLAG_IS_PLAYER))
		{
			int i;
			EntityRef erPet = entGetRef(pchar->pEntParent);
			for(i=eaSize(&pentOwner->pPlayer->petInfo)-1; i>=0; i--)
			{
				if(pentOwner->pPlayer->petInfo[i]->iPetRef==erPet)
				{
					pentOwner->pPlayer->petInfo[i]->fAttribSpeedCooldown = pchar->pattrBasic->fSpeedCooldown;
					entity_SetDirtyBit(pentOwner,parse_Player,pentOwner->pPlayer,false);
					break;
				}
			}
		}
	}

	// Movement speed and control changes
	character_UpdateMovement(pchar,&s_CharacterAttribsOld);

	PERFINFO_AUTO_STOP(); // AttributeChanges

	PERFINFO_AUTO_START("Bottom",1);

	character_RegenCostumeIfChanged(pchar, &ppOldCostumeChanges, &ppOldCostumeModifies);
	character_RegenRewardsIfChanged(pchar, &ppOldRewardModifies);

	// Set dirty bit and reset/dirty various things bBecomeCritter changed
	if(pchar->bBecomeCritterTickPhaseTwo != pchar->bBecomeCritter)
	{
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
		character_DirtyInnatePowers(pchar);
		character_DirtyInnateAccrual(pchar);
	}

	// Reset this since we're exiting the tick
	pchar->bBecomeCritterTickPhaseTwo = false;

	GameSpecific_CreateShieldFX(pchar->pEntParent);

#ifdef GAMESERVER
	// Send Health State event if health has changed
	if (s_CharacterAttribsOld.fHitPoints != pchar->pattrBasic->fHitPoints)
	{
		PERFINFO_AUTO_START("EventLog Health",1);
		eventsend_RecordHealthState(pchar->pEntParent, s_CharacterAttribsOld.fHitPoints, pchar->pattrBasic->fHitPoints);
		PERFINFO_AUTO_STOP();
	}
#endif

	PERFINFO_AUTO_START("CompareAttributes",1);
	// Compare the old attribs and the new attribs to see if anything changed, if so, mark the new attribs dirty
	if(memcmp(&s_CharacterAttribsOld,pchar->pattrBasic,g_iCharacterAttribSizeUsed))
	{
		entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs,pchar->pattrBasic,false);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP(); // Bottom

	PERFINFO_AUTO_STOP();
}

// helper function for character_NearDeathUpdateIntercats
static void _NearDeathUpdateIntercats(int iPartitionIdx, Character *pchar, EntityRef *perInteracts)
{
	EntityRef er = entGetRef(pchar->pEntParent);

	FOR_EACH_IN_EARRAY_INT(perInteracts, EntityRef, erInteracter)
	{
		Entity *eInteract = entFromEntityRef(iPartitionIdx, erInteracter);
		if(!(eInteract
			&& eInteract->pChar
			&& ((eInteract->pChar->pPowActCurrent && eInteract->pChar->pPowActCurrent->erTarget==er)
			|| (eInteract->pChar->pPowActQueued && eInteract->pChar->pPowActQueued->erTarget==er))))
		{
			eaiRemoveFast(&perInteracts, FOR_EACH_IDX(-,erInteracter));
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
	}
	FOR_EACH_END
}


// update the friendly/hostile lists of characters that are interacting with me
static void character_NearDeathUpdateIntercats(int iPartitionIdx, Character *pchar)
{
	_NearDeathUpdateIntercats(iPartitionIdx, pchar, pchar->pNearDeath->perFriendlyInteracts);
	_NearDeathUpdateIntercats(iPartitionIdx, pchar, pchar->pNearDeath->perHostileInteracts);
}

// Does the post-post-processing (after everyone has been affected)
void character_TickPhaseThree(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	// Apply outgoing damage to the fragile mods and clamp them
	PERFINFO_AUTO_START_FUNC();
	if(fRate>0.0f)
	{
		character_FragileModsDamage(iPartitionIdx,pchar,false);

		// TODO(JW): Move this somewhere more reasonable
		if(IS_HANDLE_ACTIVE(pchar->hHeldNode))
		{
			int i, s = eaSize(&pchar->ppDamageTrackersTickOutgoing);
			for(i=0; i<s; i++)
			{
				// Mild hack - if the damage tracker has no power, we don't count it against the held object
				// TODO(JW): We really only want to deal damage to the object when you're smashing people with it
				if(!GET_REF(pchar->ppDamageTrackersTickOutgoing[i]->hPower))
					continue;

				pchar->fHeldHealth -= pchar->ppDamageTrackersTickOutgoing[i]->fDamage;
				if(pchar->fHeldHealth <= 0)
				{
					character_DropHeldObjectOnTarget(iPartitionIdx,pchar,pchar->ppDamageTrackersTickOutgoing[i]->erTarget,pExtract);
				}
			}
		}
		eaDestroyEx(&pchar->ppDamageTrackersTickOutgoing,damageTrackerDestroy);

		character_FragileModsFinalizeHealth(iPartitionIdx,pchar,pchar->modArray.ppFragileMods);
		character_FragileModsFinalizeHealth(iPartitionIdx,pchar,pchar->ppModsShield);

		// Fix up the taunt state
		if(eaSize(&pchar->ppModsTaunt) > 0)
		{
			int s = eaSize(&pchar->ppModsTaunt);
			AttribMod *pmodActive = pchar->bTauntActive ? pchar->ppModsTaunt[0] : NULL;
			eaQSort(pchar->ppModsTaunt,mod_CmpDuration);
			if(!pmodActive || pmodActive->fDuration < pchar->ppModsTaunt[s-1]->fDuration/2.0)
			{
				// We have a new winner, swap it into index 0
				AttribMod *pmodNew = pchar->ppModsTaunt[s-1];
				pchar->ppModsTaunt[s-1] = pchar->ppModsTaunt[0];
				pchar->ppModsTaunt[0] = pmodNew;
				pchar->bTauntActive = true;
			}
			else if(pmodActive)
			{
				// We didn't have a winner, so restore "active" to it's rightful place (0th)
				eaSwap(&pchar->ppModsTaunt, eaFind(&pchar->ppModsTaunt, pmodActive), 0);
			}
		}
		else
		{
			// Set for safety
			pchar->bTauntActive = false;
		}

		// Finalize events
		character_FinalizeCombatEvents(iPartitionIdx,pchar,pmTimestamp(0),pExtract);

		if(0 && g_bPowerSubtargets) // JW: Disabled for now, very expensive and not used
		{
			// Update PowerSubtarget status
			character_SubtargetUpdateNet(pchar, pExtract);
		}

		// Update the AttribModNet data
		character_ModsUpdateNet(pchar);

		// Update the NearDeath Interact EntityRef
		if(pchar->pNearDeath)
		{
			character_NearDeathUpdateIntercats(iPartitionIdx, pchar);
		}
	}

	// Requested a reset of the Powers array
	if(pchar->bResetPowersArray)
	{
		character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
	}

	PERFINFO_AUTO_STOP();
#endif
}



// Special client version of character_TickPhaseTwo()
void character_TickPhaseTwoClient(Character *pchar, F32 fRate)
{
#ifdef PURGE_POWER_TREE_POWER_DEFS
	powertrees_CleanPowerDefs(pchar);
#endif

#ifdef GAMECLIENT
	// Update the Player's pet's non-predicted power information
	if(pchar->pEntParent->pPlayer)
	{
		Player *pPlayer = pchar->pEntParent->pPlayer;
		int i,j;
		for(i=eaSize(&pPlayer->petInfo)-1; i>=0; i--)
		{
			F32 fSpeedRecharge = pPlayer->petInfo[i]->fAttribSpeedRecharge;
			F32 fSpeedCooldown = pPlayer->petInfo[i]->fAttribSpeedCooldown;
			F32 fRecharge = (fSpeedRecharge > 0) ? fRate * fSpeedRecharge : fRate;
			F32 fCooldown = (fSpeedCooldown > 0) ? fRate * fSpeedCooldown : fRate;
			fRecharge *= mapState_SpeedRecharge(mapState_FromPartitionIdx(PARTITION_CLIENT));

			// Update recharges
			for(j=eaSize(&pPlayer->petInfo[i]->ppPowerStates)-1; j>=0; j--)
			{
				pPlayer->petInfo[i]->ppPowerStates[j]->fTimerRecharge -= fRecharge;
				MAX1(pPlayer->petInfo[i]->ppPowerStates[j]->fTimerRecharge,0);
				if (pPlayer->petInfo[i]->ppPowerStates[j]->fTimerRecharge == 0)
				{
					pPlayer->petInfo[i]->ppPowerStates[j]->fTimerRechargeBase = 0;
				}
			}
			// Update cooldowns
			for(j=eaSize(&pPlayer->petInfo[i]->ppCooldownTimers)-1; j>=0; j--)
			{
				pPlayer->petInfo[i]->ppCooldownTimers[j]->fCooldown -= fCooldown;
				if (pPlayer->petInfo[i]->ppCooldownTimers[j]->fCooldown <= 0.0f)
				{
					PetCooldownTimer* pTimer = eaRemove(&pPlayer->petInfo[i]->ppCooldownTimers, j);
					StructDestroy(parse_PetCooldownTimer, pTimer);
				}
			}
		}
	}
#endif
}


// Special function to operate on "offline" Characters and make them look like
//  they would if they were online
void character_TickOffline(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	//// Load
	// If this is the first time we're running this offline evaluation on the
	//  Character, we need to run some of the character_Load code on it.
	// Specifically, we call character_LoadNonTransact() with the "offline"
	//  flag to indicate that it just needs to do load processing for an
	//  offline Character
	character_LoadNonTransact(iPartitionIdx, pchar->pEntParent, true);

	//// Prep
	// Since this is the "offline" view of the Character, we don't want to
	//  include any temporary state, so we set OnlyAffectSelf to 1 (to be
	//  safe), run CleanupPartial, ResetPowersArray and RefreshPassives.
	//  This ensures they're clean, with no external AttribMods or active
	//  Toggle Powers.
	// Note that RefreshPassives does perform basic activatability tests,
	//  such as WorldRegionType restrictions, so any relevant non-Character
	//  data must be set properly before calling this function.
	pchar->pattrBasic->fOnlyAffectSelf = 1;
	character_CleanupPartial(iPartitionIdx,pchar,pExtract);
	character_ResetPowersArray(iPartitionIdx,pchar,pExtract);
	character_RefreshPassives(iPartitionIdx,pchar,pExtract);

	//// PhaseOne
	// The only part of TickPhaseOne that might be useful is CharacterTickPassive().
	//  This should be safe to run given a 0 tick and fOnlyAffectSelf set to 1.
	CharacterTickPassive(iPartitionIdx, pchar, 0, pExtract);

	//// PhaseTwo
	// The important part of this process is character_AccrueMods(), however this
	//  probably does way more than we want or need.  Specifically we've applied
	//  Passives, which would have put their mods into the pending list.  So we'll
	//  just cut this down to processing the pending list and then updating the
	//  ModNet data.
	character_ModsProcessPending(iPartitionIdx, pchar, 0, pExtract);
	character_ModsUpdateNet(pchar);

	//// PhaseThree
	// We don't want any of this currently.
}



// Returns the time the character expects to be able to start a new activation
U32 character_PredictTimeForNewActivation(Character *pchar, 
										  S32 bOverride, 
										  S32 bCooldownGlobalNotChecked, 
										  Power* pPowToQueue, 
										  F32* pfDelayInSeconds)
{
	F32 fDelay = 0.0f;
	F32 fTicks = 1.0f;

	PowerDef* pPowToQueueDef = pPowToQueue ? GET_REF(pPowToQueue->hDef) : NULL;

	if(!pPowToQueueDef || pPowToQueueDef->eType != kPowerType_Instant)
	{
		if(pchar->pPowActCurrent)
		{
			PowerActivation *pact = pchar->pPowActCurrent;
			PowerDef *pdef = GET_REF(pact->hdef);
			Power* ppow = character_FindPowerByRef(pchar, &pact->ref);

			if(!pdef || !pdef->bInstantDeactivation)
			{
				F32 fActivationTime;

				if(pdef && (pchar->eChargeMode==kChargeMode_Current || !pact->bActivated))
				{
					// Currently charging, or not yet activated, so we need to 
					//  assume full activation plus a tick to stop charging
					fActivationTime = pdef->fTimeActivate;
				}
				else if (pact->eActivationStage != kPowerActivationStage_PostMaintain)
				{
					// Not charging the current power, so we just need to know when the timer expires...
					fActivationTime = pact->fTimerActivate;
				}
				else
				{
					// kPowerActivationStage_PostMaintain
					fActivationTime = pact->fStageTimer;
				}

				if(bOverride)
				{
					fActivationTime = MAX(0,fActivationTime - (pdef ? pdef->fTimeOverride : 0));
				}
				fTicks = 1.f + ceil(fActivationTime / gConf.combatUpdateTimer);
			}

		}

		if (pPowToQueue)
		{

			MAX1(fTicks,1.f + ceil(power_GetRecharge(pPowToQueue) / gConf.combatUpdateTimer));
			MAX1(fTicks,1.f + ceil(character_GetCooldownFromPowerDef(pchar,pPowToQueueDef) / gConf.combatUpdateTimer));
		}

		if(!bCooldownGlobalNotChecked && pchar->fCooldownGlobalTimer > 0)
		{
			F32 fTicksCooldown = 1.f + ceil(pchar->fCooldownGlobalTimer / gConf.combatUpdateTimer);
			MAX1(fTicks,fTicksCooldown);
		}
	}

	// Time until that tick should start
	fDelay = (fTicks * gConf.combatUpdateTimer) - g_fCharacterTickTime;

	if (pfDelayInSeconds)
	{
		(*pfDelayInSeconds) = fDelay;
	}
	return pmTimestamp(fDelay);
}
