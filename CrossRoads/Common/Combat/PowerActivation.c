/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerActivation.h"

#include "allegiance.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "itemcommon.h"
#include "MemoryPool.h"
#include "NotifyCommon.h"
#include "rand.h"
#include "RegionRules.h"
#include "TriCube/vec.h"
#include "wlCostume.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "Character.h"
#include "Character_target.h"
#include "Character_combat.h"
#include "Character_tick.h"
#include "CharacterAttribs.h"
#include "CombatCallbacks.h"
#include "CombatEval.h"
#include "CombatConfig.h"
#include "Combat_DD.h"
#include "CombatReactivePower.h"
#include "PowerAnimFX.h"
#include "AutoGen/PowerAnimFX_h_ast.h"
#include "PowerApplication.h"
#include "PowerEnhancements.h"
#include "PowerModes.h"
#include "PowerSlots.h"
#include "SuperCritterPet.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
	#include "EntityMovementManager.h"
	#include "EntityMovementDefault.h"
	#include "EntityMovementTactical.h"
	#include "CombatPowerStateSwitching.h"
#endif

#if GAMESERVER
	#include "gslCombatDeathPrediction.h"
	#include "Character_h_ast.h"
	#include "localtransactionmanager.h"
	#include "LoggedTransactions.h"
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
	#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#endif

#if GAMECLIENT
	#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
	#include "cmdParse.h"
	#include "soundLib.h"
	#include "GameStringFormat.h"
	#include "gclCombatDeathPrediction.h"
#endif

#include "AutoGen/PowerActivation_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define POWERACT_SEQ_ERRORS 0 // Set to 1 to enabled seq error reporting

// If the recharge/cooldown on a power is less than this value on the server, allow it to be queued
#define POWERACT_ALLOWED_RECHARGE_TIMELEFT 0.3

// Callback function used by AI to watch Power Activation process
static entity_NotifyPowerExecutedCallback s_funcNotifyExecutedCallback = NULL;
entity_NotifyPowerRechargedCallback g_funcNotifyPowerRechargedCallback = NULL;

MP_DEFINE(PowerActivation);


// Creates a new empty PowerActivation
PowerActivation *poweract_Create(void)
{
	if(entIsServer())
	{
		MP_CREATE_COMPACT(PowerActivation, 20, 256, 0.8);
	}
	else
	{
		MP_CREATE_COMPACT(PowerActivation, 4, 128, 0.8);
	}

	return MP_ALLOC(PowerActivation);
}

// Destroys and frees an existing PowerActivation
void poweract_Destroy(PowerActivation *pact)
{
	if(pact)
	{
		devassert(!pact->bSpeedPenaltyIsSet);
		REMOVE_HANDLE(pact->ref.hdef);
		REMOVE_HANDLE(pact->preActivateRef.hdef);
		REMOVE_HANDLE(pact->hdef);
		REMOVE_HANDLE(pact->hTargetObject);
		eaiDestroy(&pact->perTargetsHit);
		eaDestroyEx(&pact->ppRefEnhancements,powerref_Destroy);
		StructDestroy(parse_PowerAnimFXRef,pact->pRefAnimFXMain);
		eaDestroyStruct(&pact->ppRefAnimFXEnh,parse_PowerAnimFXRef);
		MP_FREE(PowerActivation,pact);
	}
}

// Destroys and frees an existing PowerActivation, sets the pointer to NULL
void poweract_DestroySafe(PowerActivation **ppact)
{
	if(*ppact)
	{
		poweract_Destroy(*ppact);
		*ppact = NULL;
	}
}



// Returns the next PowerActivation ID
U8 poweract_NextID(void)
{
	// Static U8 that rolls over, skips 0
	// Works fine for the AI's IDs as well, since the activations created
	//  by the AI are generally legal, and this is just a communication ID.
	static U8 s_uchID = 0;
	s_uchID++;
	if(!s_uchID) s_uchID = 1;
	return s_uchID;
}

// Returns the next server-side PowerActivation ID
U32 poweract_NextIDServer(void)
{
	// Static U32 that rolls over and skips 0
	static U32 s_uiIDServer = 0;
#ifdef GAMESERVER
	s_uiIDServer++;
	if(!s_uiIDServer)
		s_uiIDServer = 1;
#endif
	return s_uiIDServer;
}



// Sets the embedded PowerRef, does not
void poweract_SetPower(PowerActivation *pact, Power *ppow)
{
	powerref_Set(&pact->ref,ppow);
	REMOVE_HANDLE(pact->hdef);
	if(ppow)
	{
		COPY_HANDLE(pact->hdef,ppow->hDef);
	}
}

// Searches an earray of Activations for the Power
int poweract_FindPowerInArray(PowerActivation ***pppActs, Power *ppow)
{
	int i;
	U32 uiID = 0;
	int iIdxSub = -1;
	S16 iLinkedIdx = -1;
	power_GetIDAndSubIdx(ppow, &uiID, &iIdxSub, &iLinkedIdx);
	for(i=eaSize(pppActs)-1; i>=0; i--)
	{
		PowerActivation *pact = (*pppActs)[i];
		if(uiID==pact->ref.uiID && 
			(iIdxSub == -1 || iIdxSub == pact->ref.iIdxSub) &&
			(iLinkedIdx == -1 || iLinkedIdx == pact->ref.iLinkedSub))
		{
			break;
		}
	}
	return i;
}

// searches an earray of PowerActivation for a power that uses the given PowerDef, returns the PowerActivation*
PowerActivation* poweract_FindPowerInArrayByDef(SA_PARAM_NN_VALID PowerActivation **ppActs, SA_PARAM_NN_VALID PowerDef *pPowerDef)
{
	FOR_EACH_IN_EARRAY(ppActs, PowerActivation, pAct)
	{
		PowerDef *pActPowerDef = GET_REF(pAct->hdef);
		if (pActPowerDef == pPowerDef)
			return pAct;
	}
	FOR_EACH_END

	return NULL;
}

// Updates the target location in the activation based on the target entity
void poweract_UpdateLocation(Entity *eSelf, PowerActivation *pact)
{
	Entity *e = entFromEntityRef(entGetPartitionIdx(eSelf),pact->erTarget);
	if(e && (e != eSelf || !pact->bRange))
	{
		if(IS_HANDLE_ACTIVE(e->hCreatorNode)) //In case the source is moving around the object
		{
			WorldInteractionNode *pNode = GET_REF(e->hCreatorNode);
			character_FindNearestPointForObject(e->pChar,NULL,pNode,pact->vecTarget,true);
		}
		else
			entGetCombatPosDir(e,NULL,pact->vecTarget,NULL);
	}
}

// Notes that the activation should stop tracking the target
void poweract_StopTracking(PowerActivation *pact)
{
	pact->erTarget = 0;
}

void CharacterPlayDelayedFX(int iPartitionIdx, Character *pchar, PowerActivation *pact);

// Notifies the server that the activation with the given id has been committed on the client
void character_MarkActCommitted(Character *pchar, U8 uchID, U32 uiSeq, U32 uiSeqReset)
{
	if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uchID)
	{
		if(!pchar->pPowActOverflow->bCommit)
		{
			// Only do this if it hasn't already been committed, otherwise the client
			//  could send useless commits to control the seed.
			uiSeq = character_VerifyPowerActSeq(pchar,uiSeq,uiSeqReset);
			pchar->pPowActOverflow->uiSeedSBLORN = uiSeq;
		}
		pchar->pPowActOverflow->bCommit = true;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Overflow %d: Received commit claim\n", uchID);
	}
	else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uchID)
	{
		if(!pchar->pPowActQueued->bCommit)
		{
			// Only do this if it hasn't already been committed, otherwise the client
			//  could send useless commits to control the seed.
			uiSeq = character_VerifyPowerActSeq(pchar,uiSeq,uiSeqReset);
			pchar->pPowActQueued->uiSeedSBLORN = uiSeq;
		}
		pchar->pPowActQueued->bCommit = true;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Received commit claim\n",uchID);
	}
	else if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uchID)
	{
		if(g_CombatConfig.bClientPredictSeq)
		{
			// By this time it's too late, so this can't affect the seed, and the server
			//  has already moved to the next seq number, so all we can do is trigger a reset.
			// If this happens a lot, we could consider making the server use a random
			//  seed in this case instead of the next valid value.  That would allow the
			//  client to hold the seed at one value by never committing, but never committing
			//  results in all the activations being stalled, which may or may not be a
			//  significant penalty.
			// If the server has already set up a reset on this Character, we probably don't
			//  need to generate an error here, but for now we will, and record whether there
			//  was already a reset in the details.
			//character_VerifyPowerActSeq(pchar,uiSeq,uiSeqReset);
#if POWERACT_SEQ_ERRORS
			ErrorDetailsf("%s %s; (%d %d %d %d); %d %d; %d %d",CHARDEBUGNAME(pchar),REF_STRING_FROM_HANDLE(pchar->pPowActCurrent->hdef),CharacterPowerActIDs(pchar),uiSeq,uiSeqReset,pchar->uiPowerActSeq,pchar->uchPowerActSeqReset);
			Errorf("PowerActivation Seq reset due to receiving a commit claim for current PowerActivation");
#endif
			character_ResetPowerActSeq(pchar);
		}

		pchar->pPowActCurrent->bCommit = true;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %d: Received commit claim\n",uchID);
	}
	else if(pchar->pPowActFinished && pchar->pPowActFinished->uchID==uchID)
	{
		if(g_CombatConfig.bClientPredictSeq)
		{
			// Similar deal to receiving the commit for something that's already current (see above giant comment)
#if POWERACT_SEQ_ERRORS
			ErrorDetailsf("%s %s; (%d %d %d %d); %d %d; %d %d",CHARDEBUGNAME(pchar),REF_STRING_FROM_HANDLE(pchar->pPowActFinished->hdef),CharacterPowerActIDs(pchar),uiSeq,uiSeqReset,pchar->uiPowerActSeq,pchar->uchPowerActSeqReset);
			Errorf("PowerActivation Seq reset due to receiving a commit claim for finished PowerActivation");
#endif
			character_ResetPowerActSeq(pchar);
		}
	}
	else
	{
		if(g_CombatConfig.bClientPredictSeq)
		{
			// Similar deal to receiving the commit for something that's already current (see above giant comment)
#if POWERACT_SEQ_ERRORS
			ErrorDetailsf("%s %d (%d %d %d %d); %d %d; %d %d",CHARDEBUGNAME(pchar),uchID,CharacterPowerActIDs(pchar),uiSeq,uiSeqReset,pchar->uiPowerActSeq,pchar->uchPowerActSeqReset);
			Errorf("PowerActivation Seq reset due to receiving a commit claim for nonexistent PowerActivation");
#endif
			character_ResetPowerActSeq(pchar);
		}

		PowersError("Jered needs to know: character_MarkActCommitted: Received commit claim, no matching activations\n");
	}
}

static void UpdateTarget(PowerActivation *pAct, Vec3 vVecTarget, EntityRef erTarget)
{
	PowerDef *pDef = GET_REF(pAct->hdef);
	if(pDef && powerddef_ShouldDelayTargeting(pDef))
	{
		copyVec3(vVecTarget, pAct->vecTarget);
		pAct->erTarget = erTarget;
	}
}



int character_CurrentChargePowerUpdateVecTarget(Character *pchar, U8 uchID, Vec3 vVecTarget, EntityRef erTarget)
{
	if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uchID && pchar->eChargeMode == kChargeMode_Current)
	{
		copyVec3(vVecTarget, pchar->pPowActCurrent->vecTarget);
		pchar->pPowActCurrent->erTarget = erTarget;

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CurrentChargePower %d: Received UpdateVecTarget"
					" p(%1.2f, %1.2f, %1.2f)\n",
					uchID,
					vecParamsXYZ(pchar->pPowActCurrent->vecTarget));
		return true;
	}
	return false;
}

// Notifies the server that the activation with the given id has an updated vectarget from the client
void character_UpdateVecTarget(int iPartitionIdx, Character *pchar, U8 uchID, Vec3 vVecTarget, EntityRef erTarget)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uchID)
	{
		UpdateTarget(pchar->pPowActOverflow, vVecTarget, erTarget);
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Overflow %d: Received UpdateVecTarget"
			" p(%1.2f, %1.2f, %1.2f)\n",
			uchID,
			vecParamsXYZ(pchar->pPowActOverflow->vecTarget));
		CharacterPlayDelayedFX(iPartitionIdx, pchar, pchar->pPowActOverflow);

	}
	else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uchID)
	{
		UpdateTarget(pchar->pPowActQueued, vVecTarget, erTarget);

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Received UpdateVecTarget"
			" p(%1.2f, %1.2f, %1.2f)\n",
			uchID,
			vecParamsXYZ(pchar->pPowActQueued->vecTarget));
		CharacterPlayDelayedFX(iPartitionIdx, pchar, pchar->pPowActQueued);
	}
	else if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uchID)
	{
		// By this time it's too late
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "WARNING: Current %d: Received UpdateVecTarget, but too late.\n",uchID);
	}
	else
	{
		// Similar deal to receiving the update for something that's already current
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "character_UpdateVecTarget: Received update, no matching activations\n");
	}
#endif
}

//Simple switching function so I don't have to rewrite this everywhere
static void CharacterActCancelPowerRef(SA_PARAM_NN_VALID Character *pchar, 
										SA_PARAM_NN_VALID PowerRef *ppowerRef, 
										EntityRef er, 
										U32 uiActivationID,
										S32 bDisable)
{
	if(ppowerRef->uiID)
	{
		character_CancelModsFromPowerID(pchar, ppowerRef->uiID, er, uiActivationID, bDisable);
	}
	else
	{
		PowerDef *pDef = GET_REF(ppowerRef->hdef);
		if(pDef)
		{
			character_CancelModsFromDef(pchar, pDef, er, uiActivationID, bDisable);
		}
	}
}

// Attempts to remove or disable all AttribMods created by a Character's PowerActivation
static void CharacterActCancelMods(	int iPartitionIdx, 
									SA_PARAM_NN_VALID Character *pchar, 
									SA_PARAM_NN_VALID PowerActivation *pact, 
									S32 bDisable)
{
	int i,j;
	EntityRef er = entGetRef(pchar->pEntParent);

	PERFINFO_AUTO_START_FUNC();

	// Cancel the mods on myself
	CharacterActCancelPowerRef(pchar, &pact->ref, er, pact->uiIDServer, bDisable);
	CharacterActCancelPowerRef(pchar, &pact->preActivateRef, er, 0, bDisable);
	for(j=eaSize(&pact->ppRefEnhancements)-1; j>=0; j--)
	{
		CharacterActCancelPowerRef(pchar, pact->ppRefEnhancements[j], er, pact->uiIDServer, bDisable);
	}

	// Cancel the mods on the targets besides myself
	for(i=eaiSize(&pact->perTargetsHit)-1; i>=0; i--)
	{
		if(er!=pact->perTargetsHit[i])
		{
			Entity *e = entFromEntityRef(iPartitionIdx, pact->perTargetsHit[i]);
			Character *pcharTarget = e ? e->pChar : NULL;
			if(pcharTarget)
			{
				CharacterActCancelPowerRef(pcharTarget, &pact->ref, er, pact->uiIDServer, bDisable);
				CharacterActCancelPowerRef(pcharTarget, &pact->preActivateRef, er, 0, bDisable);
				for(j=eaSize(&pact->ppRefEnhancements)-1; j>=0; j--)
				{
					CharacterActCancelPowerRef(pcharTarget, pact->ppRefEnhancements[j], er, pact->uiIDServer, bDisable);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

// Fills the earray with PowerActivationState for the the Character.  Destroys whatever was in the earray before.
void character_SaveActState(Character *pchar, PowerActivationState ***pppActivationState)
{
	int i,s;
	eaDestroyStruct(pppActivationState,parse_PowerActivationState);

	/* TODO(JW): Saved ActivationState of Passives is now entirely disabled, as it's just not generally
	 *  useful.  Once a PowerDef flag is added indicating a particular Passive needs it state saved
	 *  this can be turned back on.  It's an easy flag to add, but the minimal change for maximal
	 *  benefit for now is just turning this off.
	// Passives
	s = eaSize(&pchar->ppPowerActPassive);
	for(i=0; i<s; i++)
	{
		PowerActivation *pact = pchar->ppPowerActPassive[i];
		PowerActivationState *pState = StructAlloc(parse_PowerActivationState);
		StructCopy(parse_PowerRef,&pact->ref,&pState->ref,0,0,0);
		pState->uchID = pact->uchID;
		pState->uiPeriod = 0; // TODO(JW): Period isn't really important for Passive tracking, so just fix it to 0
		pState->fTimerActivate = 0;  // TODO(JW): For now pState->fTimerActivate is unused for Passives
		pState->fTimeCharged = pact->fTimeCharged;
		pState->fTimeChargedTotal = pact->fTimeChargedTotal;
		pState->fTimeActivating = pact->fTimeActivating;
		eaPush(pppActivationState,pState);
	}
	*/

	// Toggles
	s = eaSize(&pchar->ppPowerActToggle);
	for(i=0; i<s; i++)
	{
		PowerActivation *pact = pchar->ppPowerActToggle[i];
		PowerActivationState *pState = StructAlloc(parse_PowerActivationState);
		PowerDef *pdef = GET_REF(pact->ref.hdef);
		StructCopy(parse_PowerRef,&pact->ref,&pState->ref,0,0,0);
		pState->uchID = pact->uchID;
		pState->uiPeriod = (pdef && pdef->uiPeriodsMax) ? pact->uiPeriod : 0; // Only save the exact period if there's a limit
		pState->fTimerActivate = 0; // TODO(JW): For now pState->fTimerActivate is unused for Toggles
		pState->fTimeCharged = pact->fTimeCharged;
		pState->fTimeChargedTotal = pact->fTimeChargedTotal;
		pState->fTimeActivating = pact->fTimeActivating;
		eaPush(pppActivationState,pState);
	}
}

// Uses the earray of PowerActivationState to recreate PowerActivations on the Character.
void character_LoadActState(int iPartitionIdx, Character *pchar, PowerActivationState ***pppActivationState)
{
	int i,s = eaSize(pppActivationState);

	for(i=0; i<s; i++)
	{
		PowerActivationState *pState = (*pppActivationState)[i];
		Power *ppow = character_FindPowerByRef(pchar,&pState->ref);
		if(ppow)
		{
			PowerDef *pdef = GET_REF(ppow->hDef);
			if(pdef && pdef==GET_REF(pState->ref.hdef))
			{
				if(pdef->eType==kPowerType_Passive)
				{
					S32 j = character_ActivatePassive(iPartitionIdx,pchar,ppow);
					if(j>=0)
					{
						PowerActivation *pact = pchar->ppPowerActPassive[j];
						pact->uchID = pState->uchID;
						pact->uiIDServer = poweract_NextIDServer();
						pact->uiPeriod = 1; // Load directly to 1, so any "first-period-only" AttribMods don't refire
						pact->fTimerActivate = pState->fTimerActivate;
						pact->fTimeCharged = pState->fTimeCharged;
						pact->fTimeChargedTotal = pState->fTimeChargedTotal;
						pact->fTimeActivating = pState->fTimeActivating;

						// TODO(JW): Loading Passive Activation State: Restart instantly?
						pact->fTimerActivate = 0;
					}
				}
				else if(pdef->eType==kPowerType_Toggle)
				{
					// Little more complicated for toggles
					PowerActivation *pact = poweract_Create();
					poweract_SetPower(pact,ppow);
					pact->uchID = pState->uchID;
					pact->uiIDServer = poweract_NextIDServer();
					pact->uiPeriod = MAX(pState->uiPeriod,1); // Load saved value or 1, so any "first-period-only" AttribMods don't refire
					pact->fTimerActivate = pState->fTimerActivate;
					pact->fTimeCharged = pState->fTimeCharged;
					pact->fTimeChargedTotal = pState->fTimeChargedTotal;
					pact->fTimeActivating = pState->fTimeActivating;

					// TODO(JW): Loading Toggle Activation State: Restart instantly?
					pact->fTimerActivate = 0;

					eaPush(&pchar->ppPowerActToggle,pact);

					// Ensure it's flagged as active
					power_SetActive(ppow,true);

					// TODO(JW): Toggles: I'm sure there's way more I need to do here eventually
				}
			}
		}
	}
}


static void Vec3ToIVec3(Vec3 vec, IVec3 ivec)
{
	if(!ISZEROVEC3(vec))
	{
		ivec[0] = *(S32*)&vec[0];
		ivec[1] = *(S32*)&vec[1];
		ivec[2] = *(S32*)&vec[2];
		ZEROVEC3(vec);
	}
}

static void Vec3FromIVec3(Vec3 vec, IVec3 ivec)
{
	vec[0] = *(F32*)&ivec[0];
	vec[1] = *(F32*)&ivec[1];
	vec[2] = *(F32*)&ivec[2];
}

// HACK: These two functions handle fixing the Vec<->IVec conversion
void poweractreq_FixCmdSend(PowerActivationRequest *pActReq)
{
	Vec3ToIVec3(pActReq->vecTarget,pActReq->ivecTarget);
	Vec3ToIVec3(pActReq->vecTargetSecondary,pActReq->ivecTargetSecondary);
	Vec3ToIVec3(pActReq->vecSourcePos,pActReq->ivecSourcePos);
	Vec3ToIVec3(pActReq->vecSourceDir,pActReq->ivecSourceDir);
}

// HACK: These two functions handle fixing the Vec<->IVec conversion
void poweractreq_FixCmdRecv(PowerActivationRequest *pActReq)
{
	Vec3FromIVec3(pActReq->vecTarget,pActReq->ivecTarget);
	Vec3FromIVec3(pActReq->vecTargetSecondary,pActReq->ivecTargetSecondary);
	Vec3FromIVec3(pActReq->vecSourcePos,pActReq->ivecSourcePos);
	Vec3FromIVec3(pActReq->vecSourceDir,pActReq->ivecSourceDir);
}

// Retrieves the next PowerActivation Seq numbers.  May decide to not assign
//  a value to either U32, so they should be initialized to 0.
void character_GetPowerActSeq(Character *pchar, U32 *puiSeq, U32 *puiSeqReset)
{
#ifdef GAMESERVER
	*puiSeq = pchar->uiPowerActSeq;
	pchar->uiPowerActSeq++;
#endif

#ifdef GAMECLIENT
	if(g_CombatConfig.bClientPredictSeq)
	{
		// If the server has sent a seq reset, use it now
		if(pchar->uchPowerActSeqReset)
		{
			*puiSeqReset = pchar->uchPowerActSeqReset;
			pchar->uiPowerActSeq = *puiSeqReset;
			pchar->uchPowerActSeqReset = 0;
		}

		*puiSeq = pchar->uiPowerActSeq;
		pchar->uiPowerActSeq++;
	}
#endif

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "GetPowerActSeq %d\n",pchar->uiPowerActSeq);
}


static U32 s_uiPowerActSeqResetLimit = 0; // 8

// Verifies the provided PowerActivation Seq numbers.  Returns the server-valid final seq number.  May
//  trigger resets.
U32 character_VerifyPowerActSeq(Character *pchar, U32 uiSeq, U32 uiSeqReset)
{
#ifdef GAMESERVER
	if(!g_CombatConfig.bClientPredictSeq || (s_uiPowerActSeqResetLimit && pchar->uchPowerActSeqResets > s_uiPowerActSeqResetLimit))
	{
		// If the client isn't predicting, we just use the server's value
		uiSeq = pchar->uiPowerActSeq;
	}
	else if(uiSeqReset)
	{
		if(uiSeqReset==uiSeq && uiSeqReset==pchar->uchPowerActSeqReset)
		{
			// Legit reset ack, go ahead and reset
			pchar->uiPowerActSeq = uiSeqReset;
			pchar->uchPowerActSeqReset = 0;
		}
		else
		{
			// Incorrect reset ack.  Change the seq number, and then
			//  resend the old reset (or create one).
			U32 uiSeqSent = uiSeq;
			uiSeq = pchar->uiPowerActSeq;
#if POWERACT_SEQ_ERRORS
			ErrorDetailsf("%s %d %d %d",CHARDEBUGNAME(pchar),uiSeqReset,uiSeqSent,pchar->uchPowerActSeqReset);
			Errorf("PowerActivation Seq verification had bad seq reset");
#endif
			character_ResetPowerActSeq(pchar);
		}
	}
	else if(pchar->uchPowerActSeqReset)
	{
		// We're expecting a reset ack, and we didn't get it.  Change the seq number,
		//  and then resend the old reset.
		U32 uiSeqSent = uiSeq;
		uiSeq = pchar->uiPowerActSeq;
		character_ResetPowerActSeq(pchar);
	}
	else if(uiSeq!=pchar->uiPowerActSeq)
	{
		// The client sent a seq number we weren't expecting.  Change it to the
		//  seq number we WERE expecting, and start a reset.  This will cause
		//  mispredictions, but there's really nothing to do about that.
		U32 uiSeqSent = uiSeq;
		uiSeq = pchar->uiPowerActSeq;
#if POWERACT_SEQ_ERRORS
		ErrorDetailsf("%s %d %d",CHARDEBUGNAME(pchar),uiSeqSent,pchar->uiPowerActSeq);
		if(uiSeqSent==0)
		{
			// The seq number was 0, which probably means the client had a full receive
			//  of its Entity, which blows away the Character which is where that's stored.
			Errorf("PowerActivation Seq verification had seq 0");
		}
		else if(uiSeqSent < uiSeq || uiSeqSent > uiSeq + 10)
		{
			// The seq number we got went backwards, or jumped way ahead
			// Either something went terribly wrong with their network connection
			//  or they're trying to cheat.
			Errorf("PowerActivation Seq verification had very bad seq");
			devassertmsg(0,"PowerActivation Seq verification had very bad seq"); // devassert for debugging
		}
		else
		{
			// The seq number was wrong, but it went forward, and not too far, so
			//  we'll just assume the network lost a few.
			Errorf("PowerActivation Seq verification had bad seq");
		}
#endif

		character_ResetPowerActSeq(pchar);
	}

	pchar->uiPowerActSeq++;

#endif

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "VerifyPowerActSeq %d\n",pchar->uiPowerActSeq);

	return uiSeq;
}

// Performs a reset of the Character's PowerActivation Seq (if necessary) and notifies the client
void character_ResetPowerActSeq(Character *pchar)
{
#ifdef GAMESERVER
	if(!s_uiPowerActSeqResetLimit || pchar->uchPowerActSeqResets <= s_uiPowerActSeqResetLimit)
	{
		if(!pchar->uchPowerActSeqReset)
		{
			pchar->uchPowerActSeqResets++;
			if(s_uiPowerActSeqResetLimit && pchar->uchPowerActSeqResets > s_uiPowerActSeqResetLimit)
			{
#if POWERACT_SEQ_ERRORS
				ErrorDetailsf("%s",CHARDEBUGNAME(pchar));
				devassertmsg(0,"Disabling PowerAct Seq prediction due to high number of resets");
				//Errorf("Disabling PowerAct Seq prediction due to high number of resets"); // Temporary downgrade to error
#endif
			}
			else
			{
				pchar->uchPowerActSeqReset = randomIntRange(1,255);
			}
		}
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PowerActSeq Reset sent %d\n",pchar->uchPowerActSeqReset);
		ClientCmd_PowerActSeqReset(pchar->pEntParent,pchar->uchPowerActSeqReset);
	}
#endif
}


void entGetActivationSourcePosDir(	Entity *pent,
									PowerActivation *pact,
									PowerDef *pPowerDef,
									Vec3 vPosInOut,
									Vec3 vDirInOut)
{
	if (pPowerDef->bAreaEffectOffsetsRootRelative)
	{
		Quat qRot;
		entGetRot(pent, qRot);
		quatToMat3_2(qRot, vDirInOut);
	}
	else if (pact && pact->bUseSourceDir)
	{
		copyVec3(pact->vecSourceDirection, vDirInOut);
	}
	
	// see if we need to offset our source position
	if(pPowerDef->bHasEffectAreaPositionOffsets)
	{
		F32 fAngle;
		Vec3 vDir;

		copyVec3(vDirInOut, vDir);
		vDir[1] = 0;
		normalVec3(vDir);
		scaleAddVec3XZ(vDir, pPowerDef->fFrontOffset, vPosInOut, vPosInOut);

		fAngle = atan2(-vDir[2],vDir[0]);
		fAngle += HALFPI;
		vDir[0] = cos(fAngle);
		vDir[2] = -sin(fAngle);
		scaleAddVec3XZ(vDir, pPowerDef->fRightOffset, vPosInOut, vPosInOut);

		vPosInOut[1] += pPowerDef->fUpOffset;
	}
	
	if(pPowerDef->fPitch)
	{
		Mat3 xMat;
		orientMat3(xMat, vDirInOut);
		pitchMat3(RAD(pPowerDef->fPitch), xMat);
		copyVec3(xMat[2], vDirInOut);
	}

	if(pPowerDef->fYaw)
	{
		Quat qRot;
		Vec3 vTmp;
		yawQuat(RAD(-pPowerDef->fYaw),qRot);
		copyVec3(vDirInOut, vTmp);
		quatRotateVec3(qRot, vTmp, vDirInOut);
	}

}

void entOffsetPositionToCombatPosByCapsules(Entity *pent, const Capsule* const* eaCapsules, Vec3 vPosInOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(eaSize(&eaCapsules))
	{
		Vec3 vecPosPreferred;
		Vec3 vecDelta;
		Quat qRot;
		F32 fCapsuleMidline;
		Vec2 vFacePY;

		entGetFacePY(pent, vFacePY);
		if (pent->pChar && pent->pChar->pattrBasic->fFlight <= 0.f)
		{	// if we know we are not flying, ignore the pitch
			vFacePY[0] = 0.f;
		}

		PYToQuat(vFacePY, qRot);

		// Use ~5/6 up the middle of the first (hopefully main) capsule
		fCapsuleMidline = 0.85f;

		// While crouched, multiply by the crouch height ratio
		if (pent->pChar && pent->pChar->bIsCrouching)
		{
			fCapsuleMidline *= g_CombatConfig.tactical.aim.fCrouchEntityHeightRatio;
		}
		CapsuleMidlinePoint(eaCapsules[0], vPosInOut, qRot, fCapsuleMidline, vecPosPreferred);

		if (!pent->pChar || !pent->pChar->bSpecialLargeMonster)
		{	// Verify that the preferred position is entirely inside the world collision cylinder (non-rotating 6' height, 1' radius)
			// If the preferred position is on surface or entirely outside the world collision cylinder, use the default
			subVec3(vecPosPreferred,vPosInOut,vecDelta);
			if(vecDelta[1] <= 0 || vecDelta[1] >= 6 || lengthVec3SquaredXZ(vecDelta) >= 1)
			{
				vPosInOut[1] += 5.f;
			}
			else
			{
				copyVec3(vecPosPreferred,vPosInOut);
			}
		}
		else
		{
			copyVec3(vecPosPreferred,vPosInOut);
		}
	}
	else
	{
		vPosInOut[1] += 5.f;
	}
#endif
}

void entOffsetPositionToCombatPos(Entity *pent, Vec3 vPosInOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	// If we want the position and there's an activation with lunging involved
	const Capsule*const*	capsules = NULL;
	
	mmGetCapsules(pent->mm.movement, &capsules);
	entOffsetPositionToCombatPosByCapsules (pent, capsules, vPosInOut);
#endif
}

// Copies the entity's position and/or direction, with additional information from the activation.
//  The activation should only be used if the entity is the source.
void entGetCombatPosDir(Entity *pent,
						PowerActivation *pact,
						Vec3 vecPosOut,
						Vec3 vecDirOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	entGetPosDir(pent, vecPosOut, vecDirOut);

	// If we want the position and there's an activation with lunging involved
	if(vecPosOut)
	{
		if(pact && pact->uiTimestampLungeAnimate)
		{
			copyVec3(pact->vecSourceLunge,vecPosOut);
		}

		entOffsetPositionToCombatPos(pent, vecPosOut);
	}
#endif
}

bool entValidateClientViewTimestamp(U32 timestamp)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	F32 fTimeSince = timestamp ? pmTimeUntil(timestamp) : (-2.f);
	// ignore times more than a second in the past, or in the present view/future
	return (fTimeSince >= -1.f && fTimeSince < 0.f);
#endif 
}

void entGetPosRotAtTime(Entity *pent, U32 timestamp, Vec3 vecPosOut, Quat qRot)
{
#if GAMESERVER

	F32 fTimeSince = timestamp ? pmTimeUntil(timestamp) : (-2.f);
	// ignore times more than a second in the past, or in the present view/future
	if ((fTimeSince < -1.f || fTimeSince >= 0.f) || 
		!mmGetPositionRotationAtTimeFG(pent->mm.movement, timestamp, vecPosOut, qRot))
	{
		entGetPos(pent, vecPosOut);
		if (qRot)
			entGetRot(pent, qRot);
	}

#elif GAMECLIENT

	entGetPos(pent, vecPosOut);
	if (qRot)
		entGetRot(pent, qRot);

#else

	return;

#endif
}

void entGetCombatPosAtTime(Entity *pEnt, U32 timestamp, Vec3 vPosOut)
{
	entGetPosRotAtTime(pEnt, timestamp, vPosOut, NULL);
	entOffsetPositionToCombatPos(pEnt, vPosOut);
}


// Creates a powers movement event for this character and activation
void character_ActEventCreate(Character *pchar,
							  PowerActivation *pact)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	U32 uiEventID = 0;
	PM_CREATE_SAFE(pchar);
	pmEventCreate(pchar->pPowersMovement,pact->uchID,pact->uiTimestampCurrented,&uiEventID);
#endif
}

// Takes a Character and Activation, and tries to return the Power*
Power *character_ActGetPower(Character *pchar,
							 PowerActivation *pact)
{
	return character_FindPowerByRef(pchar, &pact->ref);
}

// Updates the Activation's array of references to Enhancements
void character_ActRefEnhancements(int iPartitionIdx,
								  Character *pchar,
								  PowerActivation *pact,
								  Power ***pppEnhancements)
{
	static Power **s_ppEnhancements = NULL;
	Power *ppow = character_ActGetPower(pchar,pact);
	eaDestroyEx(&pact->ppRefEnhancements,powerref_Destroy);
	if(ppow)
	{
		int i;
		power_GetEnhancements(iPartitionIdx,pchar,ppow,&s_ppEnhancements);
		for(i=eaSize(&s_ppEnhancements)-1; i>=0; i--)
		{
			eaPush(&pact->ppRefEnhancements,power_CreateRef(s_ppEnhancements[i]));
		}
		if(pppEnhancements) eaCopy(pppEnhancements,&s_ppEnhancements);
		eaClear(&s_ppEnhancements);
	}
}

// Updates the Activation's array of references to PowerAnimFX from the main power and Enhancements
void character_ActRefAnimFX(SA_PARAM_NN_VALID Character *pchar,
							SA_PARAM_NN_VALID PowerActivation *pact)
{
	Power *ppow = character_ActGetPower(pchar,pact);
	StructDestroySafe(parse_PowerAnimFXRef,&pact->pRefAnimFXMain);
	eaDestroyStruct(&pact->ppRefAnimFXEnh,parse_PowerAnimFXRef);
	if(ppow)
	{
		PowerDef *pdef = GET_REF(ppow->hDef);
		if(pdef && GET_REF(pdef->hFX))
		{
			int i;
			F32 fHuePrimary;
			pact->pRefAnimFXMain = StructCreate(parse_PowerAnimFXRef);
			COPY_HANDLE(pact->pRefAnimFXMain->hFX,pdef->hFX);
			fHuePrimary = pact->pRefAnimFXMain->fHue = powerapp_GetHue(pchar,ppow,pact,pdef);

			// If we found a base, append Enhancements
			for(i=eaSize(&pact->ppRefEnhancements)-1; i>=0; i--)
			{
				PowerAnimFXRef *pref;
				PowerRef *ppowref = pact->ppRefEnhancements[i];
				pdef = GET_REF(ppowref->hdef);
				if(pdef && GET_REF(pdef->hFX))
				{
					pref = StructCreate(parse_PowerAnimFXRef);
					COPY_HANDLE(pref->hFX,pdef->hFX);
					pref->fHue = powerapp_GetHue(pchar,character_FindPowerByRef(pchar,ppowref),pact,pdef);
					pref->uiSrcEquipSlot = ppowref ? ppowref->uiSrcEquipSlot : 0;
					if(!pref->fHue) pref->fHue = fHuePrimary;
					eaPush(&pact->ppRefAnimFXEnh,pref);
				}
			}
		}
	}
}

// Initializes the lunging process to start at the given time
//  without actually starting it, by properly setting timestamps and
//  flags on the activation
void character_ActLungeInit(int iPartitionIdx,
							Character *pchar,
							PowerActivation *pact,
							U32 uiTimeActivate,
							S32 iLungePrediction)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PowerDef *pdef = GET_REF(pact->hdef);
	PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
	pact->eLungeMode = kLungeMode_None;	// Default
	if(pafx)
	{
		// Start with the default
		pact->uiTimestampActivate = uiTimeActivate;

		// If this is an activation that wants to lunge
		if(pafx->pLunge)
		{
			F32 fExpDist = pafx->pLunge->fRange;
			F32 fTempDist = fExpDist;
			F32 fTimeLunge = pafx->pLunge->iFrameStart/PAFX_FPS;
			F32 fTimeImpact, fTimeAnimate;
			Vec3 vecSourcePos, vecSourceDir, vecTarget, vecDist;
			Entity *eTarget = NULL;
			F32 fLungeSpeed = pafx->pLunge->fSpeed;

			// Mark that we want to lunge
			pact->eLungeMode = kLungeMode_Pending;
			pact->eActivationStage = kPowerActivationStage_LungeGrab;

			// Get the source world position and direction
			entGetPosDir(pchar->pEntParent,vecSourcePos,vecSourceDir);

			// If lunging away, include the lunge distance in the range check
			if(pafx->pLunge->eDirection==kPowerLungeDirection_Away)
				pact->bIncludeLungeRange = true;

			// Get the target position
			if(pafx->pLunge->eDirection==kPowerLungeDirection_Down)
			{
				const Capsule *pCap = entGetPrimaryCapsule(pchar->pEntParent);
				WorldCollCollideResults WcResults;
				copyVec3(vecSourcePos,vecTarget);
				vecTarget[1] -= pafx->pLunge->fRange;
				if(pCap)
				{
					if(wcCapsuleCollideEx(	worldGetActiveColl(iPartitionIdx), *pCap,
											vecSourcePos, vecTarget,
											WC_QUERY_BITS_WORLD_ALL,
											&WcResults))
					{
						vecTarget[1] = WcResults.posWorldEnd[1];
					}
				}
				else
				{
					if(wcCapsuleCollide(	worldGetActiveColl(iPartitionIdx),
											vecSourcePos, vecTarget,
											WC_QUERY_BITS_WORLD_ALL,
											&WcResults))
					{
						vecTarget[1] = WcResults.posWorldEnd[1];
					}
				}
			}
			else if (pdef->fRangeSecondary)
			{
				copyVec3(pact->vecTargetSecondary, vecTarget);
			}
			else if(pact->erTarget)
			{
				eTarget = entFromEntityRef(iPartitionIdx,pact->erTarget);
				if(eTarget)
				{
					entGetPos(eTarget,vecTarget);
				}
				else
				{
					copyVec3(pact->vecTarget,vecTarget);
				}
			}
			else
			{
				copyVec3(pact->vecTarget,vecTarget);
			}

			// Check if we need to fire straight ahead
			//  TODO(JW): Lunge: Need a better check than if this is a zero vec
			if(ISZEROVEC3(vecTarget) && !eTarget)
			{
				// Fire in the facing direction for the range of the lunge
				scaleAddVec3(vecSourceDir,fExpDist,vecSourcePos,vecTarget);
			}

			// Reverse direction for lunging away
			if(pafx->pLunge->eDirection==kPowerLungeDirection_Away)
			{
				Vec3 vecDelta;
				subVec3(vecTarget,vecSourcePos,vecDelta);
				normalVec3(vecDelta);
				scaleAddVec3(vecDelta,-fExpDist,vecSourcePos,vecTarget);
				eTarget = NULL;
			}

			// Check how far we think we need to go to get to the target
			if (pafx->pLunge->bUseRootDistance)
			{
				fTempDist = distance3(vecSourcePos, vecTarget);
			}
			else if(!eTarget)
			{
				fTempDist = entGetDistance(pchar->pEntParent, NULL, NULL, vecTarget, NULL);
			}
			else if(eTarget)
			{
				// Use entity as the target
				//  Don't save the target point, since that will be somewhere other than the root
				//  and screw up the rest of our calculations.  If we actually need the target
				//  point on the Entity, we'll have to change the way we calculate the target point
				//  of the lunge in general
				fTempDist = entGetDistance(pchar->pEntParent, NULL, eTarget, NULL, NULL);
			}

			// Calculate how far we actually expect to go
			MIN1(fExpDist,fTempDist);

			if(pafx->pLunge->fRangeMin)
			{

#ifdef GAMECLIENT

				// Check if we really want to do this
				if(pafx->pLunge->fRangeMin > fExpDist)
					pact->eLungeMode = kLungeMode_None;

#elif GAMESERVER

				// -1 Prediction means do what the server wants, None means the client
				//  decided not to
				if(iLungePrediction==-1 && pafx->pLunge->fRangeMin > fExpDist)
					pact->eLungeMode = kLungeMode_None;
				else if(iLungePrediction==kLungeMode_None)
					pact->eLungeMode = kLungeMode_None;

#endif

				if(pact->eLungeMode==kLungeMode_None)
					return;
			}

			// Calculate the distance and timing
			subVec3(vecTarget,vecSourcePos,vecDist);
			
			if (pafx->pLunge->iStrictFrameDuration <= 0)
			{
				fTimeImpact = fExpDist/pafx->pLunge->fSpeed;
			}
			else
			{
				// this lunge should last a set amount of time, derive the speed
				fTimeImpact = pafx->pLunge->iStrictFrameDuration/PAFX_FPS;
				fLungeSpeed = fExpDist / fTimeImpact;
			}

			fTimeAnimate = MAX(0,fTimeImpact - pafx->pLunge->iFramesOfActivate/PAFX_FPS);
			
			pact->fLungeSpeed = fLungeSpeed;

			// Derive timestamps
			pact->uiTimestampLungeAnimate = uiTimeActivate;
			pact->uiTimestampLungeMoveStart = pmTimestampFrom(uiTimeActivate,fTimeLunge);

			// for the stop timestamp- always give an extra server process tick to allow the lunge to finish
			pact->uiTimestampLungeMoveStop = pmTimestampFrom(pact->uiTimestampLungeMoveStart,fTimeImpact) + 1;
			// and at the minimum, always allow at least two server process ticks to process the lunge, 
			//	otherwise it won't complete the full distance
			//	this is more noticeable with very very high lunge speeds
			if (pact->uiTimestampLungeMoveStop - pact->uiTimestampLungeMoveStart < 2)
				pact->uiTimestampLungeMoveStop = pact->uiTimestampLungeMoveStart + 2;

			pact->uiTimestampActivate = pmTimestampFrom(uiTimeActivate,fTimeLunge + fTimeAnimate);

			// Adjust the source vector to be the final point of the lunge
			normalVec3(vecDist);
			scaleAddVec3(vecDist,fExpDist,vecSourcePos,pact->vecSourceLunge);

			// TODO(JW): Lunge: Should this derived from the actual distance lunged,
			//  rather than just the predicted distance?
			pact->fLungeDistance = fExpDist;

			// Save the timer
  			pact->fStageTimer = fTimeLunge + fTimeImpact;
		}
	}
#endif
}

// Notifies the Character that the lunging Activation with the given ID should actually activate its Power
void character_ActLungeActivate(Character *pchar, U32 uiLungeID)
{
	PowerActivation *pact = NULL;
	if(pchar->pPowActCurrent
		&& pchar->pPowActCurrent->uchID == uiLungeID)
	{
		pact = pchar->pPowActCurrent;
	}
	else if(pchar->pPowActQueued
		&& pchar->pPowActQueued->uchID == uiLungeID)
	{
		pact = pchar->pPowActQueued;
	}

	if(pact)
	{
		//pact->eLungeMode = kLungeMode_Activated;
	}
}

// Notifies the Character that the lunging Activation with the given ID stopped, with the specified result position
void character_ActLungeFinished(Character *pchar, U32 uiLungeID, Vec3 vecPos)
{
	PowerActivation *pact = NULL;
	if(pchar->pPowActCurrent
		&& pchar->pPowActCurrent->uchID == uiLungeID)
	{
		pact = pchar->pPowActCurrent;
	}
	else if(pchar->pPowActQueued
		&& pchar->pPowActQueued->uchID == uiLungeID)
	{
		pact = pchar->pPowActQueued;
	}

	if(pact)
	{
		pact->eLungeMode = kLungeMode_Failure;
		copyVec3(vecPos,pact->vecSourceLunge);
		{
			PowerDef *pdef = GET_REF(pact->hdef);
			if(pdef)
			{
				// TODO(JW): Lunge: Get real range (should be stuffed into activation somewhere?)
				F32 fDist, fRange;
				Vec3 vecSourcePos, vecOffset;
				Power *ppow = character_FindPowerByRef(pchar,&pact->ref);

				fRange = power_GetRange(ppow, pdef);

				if(pact->bIncludeLungeRange)
					fRange += pact->fLungeDistance;
				entGetPos(pchar->pEntParent,vecSourcePos);
				subVec3(vecPos,vecSourcePos,vecOffset);
				fDist = entGetDistanceOffset(pchar->pEntParent,NULL,vecOffset,NULL,pact->vecTarget,NULL);
				if(fDist<fRange)
				{
					// TODO(JW): Lunge: Probably need a LoS check here
					pact->eLungeMode = kLungeMode_Success;
				}
			}
		}
	}
}

// Sets up the Grab mode
void poweract_GrabInit(PowerActivation *pact)
{
	PowerDef *pdef = GET_REF(pact->hdef);
	PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
	pact->eGrabMode = kGrabMode_None;
	if(pafx && pafx->pGrab)
	{
		pact->eGrabMode = kGrabMode_Pending;
		pact->eActivationStage = kPowerActivationStage_LungeGrab;
	}
}



// Handles the standard side effects when the charging stage of activation is finished
// returns if the power is still activating
static S32 CharacterActChargeFinished(int iPartitionIdx,
									  SA_PARAM_NN_VALID Character *pchar,
									  SA_PARAM_NN_VALID PowerActivation *pact,
									  U32 uiTimeActivate,
									  S32 bAllowMaintain,
									  SA_PARAM_NN_VALID S32 *pbPowerChangedOut)
{
	Power *ppow = character_ActGetPower(pchar,pact);
	PowerDef *ppowdef = GET_REF(pact->hdef);

	*pbPowerChangedOut = false;

	if (!ppowdef)
		return false; // if the def is missing, it's probably not activating.

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Charge: Finishing charging at %f (%s)\n",pact->fTimeCharged,ppowdef->pchName);

	// Set the charge mode based on the type of power being activated
	if(bAllowMaintain && ppowdef->eType==kPowerType_Maintained)
	{
		// This will convert kChargeMode_Queued to kChargeMode_QueuedMaintain and kChargeMode_Current
		//  to kChargeMode_CurrentMaintain
		pchar->eChargeMode--;
	}
	else
	{
		pchar->eChargeMode = kChargeMode_None;
	}

#ifdef GAMESERVER
	if(g_CombatConfig.bClientChargeData && pchar->pChargeData)
	{
		StructDestroySafe(parse_CharacterChargeData,&pchar->pChargeData);
		pchar->bChargeDataDirty = true;
	}
#endif

	// Required Charge Time check
	// If the power needs a minimum charge, we need to check and see if that actually happened
	// TODO(JW): Activation: This needs to be made very, very solid, and probably libified
	if(pact->fTimeChargeRequiredCombo != 0.0f
		&& pchar->eChargeMode!=kChargeMode_Current
		&& pact->fTimeCharged<pact->fTimeChargeRequiredCombo)
	{
		Power *ppowNew = NULL;
		U32 uiAnimTime = pact->uiTimestampActivate;

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Charge %s FAIL: Did not reach minimum charge combo time of %f\n",ppowdef->pchName,pact->fTimeChargeRequiredCombo);

		// If this power is inside a combo, find who it falls through to
		if(ppow && ppow->pParentPower)
		{
			int i;
			PowerDef *pdefParent = GET_REF(ppow->pParentPower->hDef);
			Power **ppSiblings = ppow->pParentPower->ppSubPowers;
			// Get the size, less one, so we don't bother checking the last power
			int iNumPowers = eaSize(&ppSiblings)-1;
			for(i=0; i<iNumPowers; i++)
			{
				if(ppow==ppSiblings[i])
				{
					// Found me inside the parent's list, so go to the next usable power in the list
					int c;

					for(c=i+1;c<=iNumPowers;c++)
					{
						if(pdefParent->ppOrderedCombos[c]->fPercentChargeRequired*ppowdef->fTimeCharge <= pact->fTimeCharged)
						{
							ppowNew = ppow->pParentPower->ppSubPowers[c];
							break;
						}
					}
				}
			}
		}



		if(ppowNew)
		{
			ppowdef = GET_REF(ppowNew->hDef);
			if (!ppowdef)
				return false;
			// Replace variables in the activation
			poweract_SetPower(pact,ppowNew);

			// Set the required charge time to 0 so we don't check anymore
			pact->fTimeChargeRequiredCombo = 0.0f;

			*pbPowerChangedOut = true;
		}
		else
		{
			// No power to replace this, so it just fails
			if(pact->pRefAnimFXMain)
			{
				character_AnimFXChargeOff(iPartitionIdx,pchar,pact,uiTimeActivate,true);
			}

			if(pchar->pPowActCurrent == pact)
				character_ActCurrentCancel(iPartitionIdx,pchar,true,true);
			else if(pchar->pPowActQueued == pact)
				character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
			else if(pchar->pPowActOverflow == pact)
				character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);

			return false;
		}
	}


	if(pact->fTimeChargeRequired != 0.0f
		&& pchar->eChargeMode!=kChargeMode_Current
		&& pact->fTimeCharged<pact->fTimeChargeRequired)
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Charge %s FAIL: Did not reach minimum charge time of %f\n",ppowdef->pchName,pact->fTimeChargeRequired);
		// The power fails
		if(pact->pRefAnimFXMain)
		{
			character_AnimFXChargeCancel(iPartitionIdx,pchar,pact,true);
			//character_AnimFXChargeOff(pchar,pact,uiTimeActivate,true);
		}
		if(pchar->pPowActCurrent == pact)
			character_ActCurrentCancel(iPartitionIdx,pchar,true,true);
		else if(pchar->pPowActQueued == pact)
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
		else if(pchar->pPowActOverflow == pact)
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		return false;
	}

	// Update the activate timestamp
	pact->uiTimestampActivate = uiTimeActivate;

	// Turn off charge bits/fx/root
	if(pact->pRefAnimFXMain)
	{
		character_AnimFXChargeOff(iPartitionIdx,pchar,pact,uiTimeActivate,true);
	}

	CharacterActCancelPowerRef(pchar, &pact->preActivateRef, entGetRef(pchar->pEntParent), 0, false);
	return true;
}

// Finishes the charge process and starts the activate process for the activation
// returns if the power in the activation has changed
S32 character_ActChargeToActivate(int iPartitionIdx,Character *pchar, PowerActivation *pact, PowerAnimFX *pafx)
{
	PowerDef *ppowdef = GET_REF(pact->hdef);
	S32 bPowerChanged = false;

	if (!ppowdef)
		return false;

	if(!pafx)
		pafx = GET_REF(ppowdef->hFX);


	// Normal shutdown
	if(!CharacterActChargeFinished(iPartitionIdx,pchar,pact,pact->uiTimestampActivate,true,&bPowerChanged))
		return false;

	if(bPowerChanged)
	{
		ppowdef = GET_REF(pact->hdef);
		pafx = GET_REF(ppowdef->hFX);

		if(pact->pRefAnimFXMain)
		{
			REMOVE_HANDLE(pact->pRefAnimFXMain->hFX);
			COPY_HANDLE(pact->pRefAnimFXMain->hFX,ppowdef->hFX);
		}

		// TODO(JW): Bug: Seems like we'd need to reset the enhancement references here as well
	}

	character_ActLungeInit(iPartitionIdx,pchar,pact,pact->uiTimestampActivate,-1);
	poweract_GrabInit(pact);

	// We might be in the lunge state already thanks to ActLungeInit
	if (pact->eActivationStage == kPowerActivationStage_Charge)
	{
		pact->eActivationStage = kPowerActivationStage_Activate;
	}

	if(pafx && !pact->bPlayedActivate && !pact->bDelayActivateToHitCheck)
	{
		Vec3 vecSourcePos,vecSourceDir,vecTarget;
		Entity *pentRealTarget = NULL;

		entGetCombatPosDir(pchar->pEntParent,pact,vecSourcePos,vecSourceDir);
		entGetActivationSourcePosDir(pchar->pEntParent, pact, ppowdef, vecSourcePos, vecSourceDir);

		if(character_ActFindTarget(iPartitionIdx,pchar,pact,vecSourcePos,vecSourceDir,&pentRealTarget,vecTarget))
		{
			character_MoveFaceStart(iPartitionIdx,pchar,pact,kPowerAnimFXType_ActivateSticky);
			character_MoveLungeStart(pchar,pact);
			character_AnimFXLunge(iPartitionIdx,pchar,pact);
			character_AnimFXActivateOn(iPartitionIdx,pchar,NULL,pact,NULL,pentRealTarget?pentRealTarget->pChar:NULL,vecTarget,pact->uiTimestampActivate,pact->uchID,pact->uiPeriod,0);
		}
		else
		{
			character_AnimFXChargeCancel(iPartitionIdx,pchar,pact,false);
		}
	}
	return bPowerChanged;
}

// Finishes the charge process and starts the activate process for the activation
// returns if the power in the activation has changed
S32 character_ActEndCharge(int iPartitionIdx,Character *pchar, PowerActivation *pact)
{
	PowerDef *ppowdef = GET_REF(pact->hdef);
	S32 bPowerChanged = false;

	if (!ppowdef)
		return false;

	// Normal shutdown
	if(!CharacterActChargeFinished(iPartitionIdx,pchar,pact,pact->uiTimestampActivate,true,&bPowerChanged))
		return false;

	if(bPowerChanged)
	{
		ppowdef = GET_REF(pact->hdef);

		if(pact->pRefAnimFXMain)
		{
			REMOVE_HANDLE(pact->pRefAnimFXMain->hFX);
			COPY_HANDLE(pact->pRefAnimFXMain->hFX,ppowdef->hFX);
		}

		// TODO(JW): Bug: Seems like we'd need to reset the enhancement references here as well
	}

	return bPowerChanged;
}

// Finishes the charge process and starts the activate process for the activation
// returns if the power in the activation has changed
void character_ActMoveToPreactivate(int iPartitionIdx,Character *pchar, PowerActivation *pact)
{
	PowerDef *ppowdef = GET_REF(pact->hdef);
	PowerAnimFX * pafx;

	if (!ppowdef)
		return;

	pafx = GET_REF(ppowdef->hFX);

	if(pafx)
	{
		Vec3 vecSourcePos,vecSourceDir,vecTarget;
		Entity *pentRealTarget = NULL;

		entGetCombatPosDir(pchar->pEntParent,pact,vecSourcePos,vecSourceDir);
		entGetActivationSourcePosDir(pchar->pEntParent, pact, ppowdef, vecSourcePos, vecSourceDir);
		pact->eActivationStage = kPowerActivationStage_Preactivate;
		pact->fStageTimer += ppowdef->fTimePreactivate;
		if(character_ActFindTarget(iPartitionIdx,pchar,pact,vecSourcePos,vecSourceDir,&pentRealTarget,vecTarget))
		{
			U32 uiTime = pact->uiTimestampActivate;
			character_MoveFaceStart(iPartitionIdx,pchar,pact,kPowerAnimFXType_PreactivateSticky);
			character_AnimFXPreactivateOn(iPartitionIdx,pchar,NULL,pact,NULL,pentRealTarget?pentRealTarget->pChar:NULL,vecTarget,uiTime,pact->uchID,pact->uiPeriod);
		}
		else
		{
			character_AnimFXChargeCancel(iPartitionIdx,pchar,pact,false);
		}
	}
}

// Finishes the charge process and starts the activate process for the activation
// returns if the power in the activation has changed
void character_ActMoveToActivate(int iPartitionIdx,Character *pchar, PowerActivation *pact)
{
	PowerDef *ppowdef = GET_REF(pact->hdef);
	PowerAnimFX * pafx;

	if (!ppowdef)
		return;

	pafx = GET_REF(ppowdef->hFX);

	character_ActLungeInit(iPartitionIdx,pchar,pact,pact->uiTimestampActivate,-1);
	poweract_GrabInit(pact);

	// We might be in the lunge state already thanks to ActLungeInit
	if (pact->eActivationStage == kPowerActivationStage_Charge)
	{
		pact->eActivationStage = kPowerActivationStage_Activate;
	} 
	else if (pact->eActivationStage == kPowerActivationStage_Preactivate)
	{
		pact->eActivationStage = kPowerActivationStage_Activate;
	}

	if(pafx && !pact->bPlayedActivate && !pact->bDelayActivateToHitCheck)
	{
		Vec3 vecSourcePos,vecSourceDir,vecTarget;
		Entity *pentRealTarget = NULL;

		entGetCombatPosDir(pchar->pEntParent,pact,vecSourcePos,vecSourceDir);
		entGetActivationSourcePosDir(pchar->pEntParent, pact, ppowdef, vecSourcePos, vecSourceDir);

		if (character_ActFindTarget(iPartitionIdx, pchar, pact, vecSourcePos, vecSourceDir, &pentRealTarget, vecTarget))
		{
			character_MoveLungeStart(pchar, pact);
			character_AnimFXLunge(iPartitionIdx, pchar, pact);
		}

		character_MoveFaceStart(iPartitionIdx, pchar, pact, kPowerAnimFXType_ActivateSticky);
		character_AnimFXActivateOn(iPartitionIdx, pchar, NULL, pact, NULL,
									(pentRealTarget?pentRealTarget->pChar:NULL),
									vecTarget, pact->uiTimestampActivate, pact->uchID, pact->uiPeriod, 0);
	}
}

// Attempts to deactivate an active power (being charged, maintained, etc).
//  If the ID and charge mode are 0, it picks whatever is currently active,
//  and passes back out what it did.  If not, it tries to find a good match.
// Note: if bForced is true, it will ignore the requested interrupt flag on the power def when determining whether
//  to deactivate the activation or not
void character_ActDeactivate(int iPartitionIdx,
							 Character *pchar,
							 U8 *puchActIDInOut,
							 ChargeMode *peModeInOut,
							 U32 uiTimeOfEvent,
							 U32 uiTimeCurrentedNew,
							 S32 bForced)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	U8 uchID = *puchActIDInOut;
	ChargeMode eMode = *peModeInOut;

	PERFINFO_AUTO_START_FUNC();

	// Reset the outputs
	*puchActIDInOut = 0;
	*peModeInOut= kChargeMode_None;

	if(pchar->pPowActOverflow && (!uchID || pchar->pPowActOverflow->uchID==uchID))
	{
		// We are deactivating the overflow power
		PowerActivation *pact = pchar->pPowActOverflow;
		PowerDef *pdefOverflow = GET_REF(pact->hdef);
		if(!bForced && pdefOverflow && !(pdefOverflow->eInterrupts&kPowerInterruption_Requested))
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Ignored - No requested interrupts\n",pact->uchID);
		}
		else if(pchar->eChargeMode==kChargeMode_Overflow)
		{
			PowerAnimFX *pafx = pdefOverflow ? GET_REF(pdefOverflow->hFX) : NULL;

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Deactivating overflow charge\n",pact->uchID);

			*puchActIDInOut = pact->uchID;
			*peModeInOut = kChargeMode_Overflow;

			if(eMode!=kChargeMode_None && eMode!=kChargeMode_Overflow && eMode!=kChargeMode_Queued)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Client thought this was current! %d %d\n",pact->uchID,uiTimeOfEvent,uiTimeCurrentedNew);
				uiTimeCurrentedNew = uiTimeOfEvent;
			}

			// Update the predicted currented time
			pact->uiTimestampCurrented = MAX(uiTimeCurrentedNew,pact->uiTimestampCurrented);

			pact->uiTimestampActivate = pact->uiTimestampCurrented;

			character_ActChargeToActivate(iPartitionIdx,pchar,pact,pafx);
		}
		else if(pchar->eChargeMode==kChargeMode_OverflowMaintain)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Deactivating overflow maintain\n",pact->uchID);
			*puchActIDInOut = pact->uchID;
			*peModeInOut = kChargeMode_OverflowMaintain;

			//Check to see if its an instant deactivation
			if(pdefOverflow->bInstantDeactivation)
			{
				character_ActOverflowCancel(iPartitionIdx,pchar,NULL,pact->uchID);
			}
			else
			{
				// Prep deactivation to happen on next combat tick
				character_DeactivateMaintainedAnimFX(iPartitionIdx, pchar, pact, pdefOverflow);
				pchar->eChargeMode = kChargeMode_None;
			}
		}
		else
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Ignored - not charging\n",pact->uchID);
		}
	}
	else if(pchar->pPowActQueued && (!uchID || pchar->pPowActQueued->uchID==uchID))
	{
		// We are deactivating the queued power
		PowerActivation *pact = pchar->pPowActQueued;
		PowerDef *pdefQueued = GET_REF(pact->hdef);
		if(!bForced && pdefQueued && !(pdefQueued->eInterrupts&kPowerInterruption_Requested))
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Ignored - No requested interrupts\n",pact->uchID);
		}
		else if(pchar->eChargeMode==kChargeMode_Queued)
		{
			PowerAnimFX *pafx = pdefQueued ? GET_REF(pdefQueued->hFX) : NULL;

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Deactivating queued charge\n",pact->uchID);

			*puchActIDInOut = pact->uchID;
			*peModeInOut = kChargeMode_Queued;

			if(eMode!=kChargeMode_None && eMode!=kChargeMode_Queued)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Client thought this was current! %d %d\n",pact->uchID,uiTimeOfEvent,uiTimeCurrentedNew);
				uiTimeCurrentedNew = uiTimeOfEvent;
			}

			// Update the predicted currented time
			pact->uiTimestampCurrented = MAX(uiTimeCurrentedNew,pact->uiTimestampCurrented);

			pact->uiTimestampActivate = pact->uiTimestampCurrented;

			character_ActChargeToActivate(iPartitionIdx,pchar,pact,pafx);
		}
		else if(pchar->eChargeMode==kChargeMode_QueuedMaintain)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Deactivating queued maintain\n",pact->uchID);
			*puchActIDInOut = pact->uchID;
			*peModeInOut = kChargeMode_QueuedMaintain;

			//Check to see if its an instant deactivation
			if(pdefQueued->bInstantDeactivation)
			{
				character_ActQueuedCancel(iPartitionIdx,pchar,NULL,pact->uchID);
			}
			else
			{
				// Prep deactivation to happen on next combat tick
				character_DeactivateMaintainedAnimFX(iPartitionIdx, pchar, pact, pdefQueued);
				pchar->eChargeMode = kChargeMode_None;
			}
		}
		else
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Ignored - not charging\n",pact->uchID);
		}
	}
	else if(pchar->pPowActCurrent && (!uchID || pchar->pPowActCurrent->uchID==uchID))
	{
		// We are deactivating the current power
		PowerActivation *pact = pchar->pPowActCurrent;
		PowerDef *pdefCurrent = GET_REF(pact->hdef);
		if(!bForced && pdefCurrent && !(pdefCurrent->eInterrupts&kPowerInterruption_Requested))
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Ignored - No requested interrupts\n",pact->uchID);
		}
		else if(pchar->eChargeMode==kChargeMode_Current)
		{
			PowerAnimFX *pafx = pdefCurrent ? GET_REF(pdefCurrent->hFX) : NULL;

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Deactivating current charge\n",pact->uchID);

			*puchActIDInOut = pact->uchID;
			*peModeInOut = kChargeMode_Current;

			pact->uiTimestampActivate = uiTimeOfEvent;

			character_ActChargeToActivate(iPartitionIdx,pchar,pact,pafx);
		}
		else if(pchar->eChargeMode==kChargeMode_CurrentMaintain)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Deactivating current maintain\n",pact->uchID);
			*puchActIDInOut = pact->uchID;
			*peModeInOut = kChargeMode_CurrentMaintain;

			//Check to see if its an instant deactivation
			if(pdefCurrent->bInstantDeactivation)
			{
				Power *ppow = character_ActGetPower(pchar,pact);

				if(ppow)
				{
					PowerAnimFX *pafx = GET_REF(pdefCurrent->hFX);
					U32 uiAnimTime = pmTimestampFrom(pact->uiTimestampActivate,pact->fTimeActivating);
					character_DeactivateMaintained(iPartitionIdx, pchar, ppow, pact, pafx, uiTimeOfEvent);
				}
				
				character_ActCurrentFinish(iPartitionIdx,pchar,false);
				pchar->eChargeMode=kChargeMode_None;
			}
			else
			{
				character_DeactivateMaintainedAnimFX(iPartitionIdx, pchar, pact, pdefCurrent);
				pchar->eChargeMode = kChargeMode_None;
			}
		}
		else
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Ignored - not charging\n",pact->uchID);
		}
	}
	else
	{
		// Deactivate didn't match with anything
		if(entIsServer())
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Nothing charging or maintained\n",uchID);
		}
		else
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate: Nothing charging or maintained\n");
		}
	}

	PERFINFO_AUTO_STOP();
#endif
}

// Causes the character to attempt to interrupt whatever is being charged
void character_ActInterrupt(int iPartitionIdx, Character *pchar, PowerInterruption eInterrupt)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int s;

	if(pchar->eChargeMode!=kChargeMode_None)
	{
		PowerActivation *pact;

		PERFINFO_AUTO_START("character_ActInterrupt Charging",1);

		if(pchar->eChargeMode==kChargeMode_Current || pchar->eChargeMode==kChargeMode_CurrentMaintain)
		{
			pact=pchar->pPowActCurrent;
		}
		else if(pchar->eChargeMode==kChargeMode_Queued || pchar->eChargeMode==kChargeMode_QueuedMaintain)
		{
			pact=pchar->pPowActQueued;
		}
		else
		{
			pact=pchar->pPowActOverflow;
		}

		if(pact)
		{
			PowerDef *pdef = GET_REF(pact->hdef);
			if(pdef && pdef->eInterrupts&eInterrupt)
			{
				ChargeMode eChargeModePrior = pchar->eChargeMode;
				F32 fTimeChargeNow = pact->fTimeCharged ? character_TickRound(pact->fTimeCharged) : 0.f;

				pact->uiTimestampActivate = pmTimestampFrom(pact->uiTimestampCurrented, fTimeChargeNow);

#ifdef GAMESERVER
				ClientCmd_PowersInterruptPower(pchar->pEntParent,pact->uchID,pact->fTimeCharged,pact->uiTimestampActivate);
#endif
				// Stop any normal charging
				if(pchar->eChargeMode==kChargeMode_Current || pchar->eChargeMode==kChargeMode_Queued || pchar->eChargeMode==kChargeMode_Overflow)
					character_ActChargeToActivate(iPartitionIdx,pchar,pact,NULL);

				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Interrupt %d: ChargeMode %d %d\n",pact->uchID,eChargeModePrior,pchar->eChargeMode);

				// Further, stop any maintaining
				pchar->eChargeMode=kChargeMode_None;
			}
		}

		PERFINFO_AUTO_STOP();
	}

	// On the server, we may interrupt Toggles based on the CombatConfig
	if(entIsServer() && g_CombatConfig.eInterruptToggles & eInterrupt && (s=eaSize(&pchar->ppPowerActToggle)))
	{
		int i;

		PERFINFO_AUTO_START("character_ActInterrupt Toggles",1);
		for(i=s-1; i>=0; i--)
		{
			PowerActivation *pact = pchar->ppPowerActToggle[i];
			PowerDef *pdef = GET_REF(pact->ref.hdef);
			if(pdef && pdef->eInterrupts&eInterrupt)
			{
				Power *ppow = character_ActGetPower(pchar,pact);
				character_DeactivateToggle(iPartitionIdx,pchar,pact,ppow,pmTimestamp(0),true);
			}
		}
		PERFINFO_AUTO_STOP();
	}
#endif
}


// Cleans up the finished activation
void character_ActFinishedDestroy(Character *pchar)
{
	// Nothing else to do but destroy it at the moment
	if(pchar->pPowActFinished)
	{
		// If the Activation is a toggle just set the finished to NULL,
		//  the Activation will clean itself up on its own
		PowerDef *pdef = GET_REF(pchar->pPowActFinished->hdef);
		if(pdef && pdef->eType==kPowerType_Toggle)
		{
			pchar->pPowActFinished = NULL;
		}
		else
		{
			poweract_DestroySafe(&pchar->pPowActFinished);
		}
	}
}



// Cancels the Activation that is currently in overflow.  Handles it better
//  if the Power that will be replacing it is passed in.  If IDRequired
//  is non-zero, the cancellation will only occur if the overflow Activation
//  matches that ID.  If the eType is a type ignored by the Power, it will
//  not be cancelled. Returns the activation id of the canceled activation.
U8 character_ActOverflowCancelReason(int iPartitionIdx,
									 Character *pchar,
									 PowerDef *pdefReplacement,
									 U8 uchIDRequired,
									 AttribType eType,
									 S32 bNotify)
{
	U8 uchID = 0;
	PowerActivation *pact = pchar->pPowActOverflow;
	S32 bChangeChargeMode = true;

	if(pact && (!uchIDRequired || pact->uchID==uchIDRequired))
	{
		PowerDef *pdef = GET_REF(pact->hdef);

		if(pdef && (eType == kAttribType_Null || !powerdef_IgnoresAttrib(pdef,eType)) )
		{
			U32 uiTime = pact->uiTimestampCurrented;
			PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
			uchID = pact->uchID;

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Overflow %d: CANCEL: at %d, replacement %s\n",pact->uchID,uiTime,pdefReplacement?pdefReplacement->pchName:"NULL");

			if(pafx)
			{
				// If the stances are different, and the one that's queued is what's active,
				//  exit it by entering the NULL stance
				if(!poweranimfx_StanceMatch(pdef,pdefReplacement) &&
					pchar->uiStancePowerAnimFXID==(U32)pact->uchID)
				{
					character_EnterStance(iPartitionIdx,pchar,NULL,NULL,true,uiTime);
				}
				if (!poweranimfx_PersistStanceMatch(pdef,pdefReplacement) &&
					pchar->uiPersistStancePowerAnimFXID==(U32)pact->uchID)
				{
					character_EnterPersistStance(iPartitionIdx,pchar,NULL,pdefReplacement,NULL,uiTime,0,false);
				}

				if(pchar->eChargeMode==kChargeMode_Overflow)
				{
					// Cancel all the stuff charging had started
					character_AnimFXChargeCancel(iPartitionIdx,pchar,pact,false);
				}
				else if(pact->bPlayedActivate)
				{
					character_AnimFXActivateCancel(iPartitionIdx,pchar,pact,false,false);
				}
			}

			if(bNotify)
			{
#ifdef GAMESERVER
				// Notify the client that its power has been canceled
				ClientCmd_PowersCancelPowers(pchar->pEntParent,pact->uchID,true,false,pact->ref.uiID);
#elif GAMECLIENT
				// Notify the server that we canceled the activation
				ServerCmd_PowersActCancelServer(pact->uchID,pdefReplacement?pdefReplacement->pchName:NULL);
#endif
			}

			CharacterActCancelPowerRef(pchar, &pact->preActivateRef, entGetRef(pchar->pEntParent), 0, false);

			// Free the old overflow activation
			poweract_DestroySafe(&pchar->pPowActOverflow);
		}
		else if(pdef)
		{
			// Power ignores the reason, so we don't want to change the charge mode
			bChangeChargeMode = false;
		}
	}

	// Make sure we clean up the ChargeMode regardless
	if(bChangeChargeMode && (pchar->eChargeMode==kChargeMode_Overflow || pchar->eChargeMode==kChargeMode_OverflowMaintain))
	{
		pchar->eChargeMode = kChargeMode_None;
	}

	return uchID;
}

// Wrapper for character_ActOverflowCancelReason()
U8 character_ActOverflowCancel(int iPartitionIdx,
							   Character *pchar,
							   PowerDef *pdefReplacement,
							   U8 uchIDRequired)
{
	return character_ActOverflowCancelReason(iPartitionIdx, pchar, pdefReplacement, uchIDRequired, kAttribType_Null, true);
}



// Cancels the activation that is currently queued.  Handles it better
//  if the power that will be replacing it is passed in.  If IDRequired
//  is non-zero, the cancellation will only occur if the queued activation
//  matches that ID.  If the eType is a type ignored by the Power, it will
//  not be canceled. Returns the activation id of the canceled Activation.
U8 character_ActQueuedCancelReason(int iPartitionIdx,
								   Character *pchar,
								   PowerDef *pdefReplacement,
								   U8 uchIDRequired,
								   AttribType eType,
								   S32 bNotify)
{
	U8 uchID = 0;
	PowerActivation *pact = pchar->pPowActQueued;
	S32 bChangeChargeMode = true;

	if(pact && (!uchIDRequired || pact->uchID==uchIDRequired))
	{
		PowerDef *pdef = GET_REF(pact->hdef);
		if(pdef && (eType == kAttribType_Null || !powerdef_IgnoresAttrib(pdef,eType)) )
		{
			U32 uiTime = pact->uiTimestampCurrented;
			PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
			uchID = pact->uchID;

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queued %d: CANCEL: at %d,%s replacement %s\n",pact->uchID,uiTime,pchar->pPowActCurrent?"":" NO CURRENT,",pdefReplacement?pdefReplacement->pchName:"NULL");

			if(pafx)
			{
				// If the stances are different, and the one that's queued is what's active,
				//  exit it by entering the NULL stance
				if(!poweranimfx_StanceMatch(pdef,pdefReplacement) &&
					pchar->uiStancePowerAnimFXID==(U32)pact->uchID)
				{
					character_EnterStance(iPartitionIdx,pchar,NULL,NULL,true,uiTime);
				}
				if (!poweranimfx_PersistStanceMatch(pdef,pdefReplacement) &&
					pchar->uiPersistStancePowerAnimFXID==(U32)pact->uchID)
				{
					character_EnterPersistStance(iPartitionIdx,pchar,NULL,pdefReplacement,NULL,uiTime,0,false);
				}

				if(pchar->eChargeMode==kChargeMode_Queued)
				{
					// Cancel all the stuff charging had started
					character_AnimFXChargeCancel(iPartitionIdx,pchar,pact,false);
				}
				else if(pact->bPlayedActivate)
				{
					character_AnimFXActivateCancel(iPartitionIdx,pchar,pact,false,false);
				}
			}

			if(bNotify)
			{
#ifdef GAMESERVER
				// Notify the client that its power has been canceled
				ClientCmd_PowersCancelPowers(pchar->pEntParent,pact->uchID,true,false,pact->ref.uiID);
#elif GAMECLIENT
				// Notify the server that we canceled the activation
				ServerCmd_PowersActCancelServer(pact->uchID,pdefReplacement?pdefReplacement->pchName:NULL);
#endif
			}

			if(s_funcNotifyExecutedCallback)
			{
				Power *ppow = character_ActGetPower(pchar,pact);
				if(ppow) s_funcNotifyExecutedCallback(pchar->pEntParent, ppow);
			}

			CharacterActCancelPowerRef(pchar, &pact->preActivateRef, entGetRef(pchar->pEntParent), 0, false);

			// Free the old queued activation
			poweract_DestroySafe(&pchar->pPowActQueued);
		}
		else if(pdef)
		{
			// Power ignores the reason, so we don't want to change the charge mode
			bChangeChargeMode = false;
		}
	}

	// Make sure we clean up the ChargeMode regardless
	if(bChangeChargeMode && (pchar->eChargeMode==kChargeMode_Queued || pchar->eChargeMode==kChargeMode_QueuedMaintain))
	{
		pchar->eChargeMode = kChargeMode_None;
	}

	return uchID;
}

// Wrapper for character_ActQueuedCancelReason()
U8 character_ActQueuedCancel(int iPartitionIdx,
							 Character *pchar,
							 PowerDef *pdefReplacement,
							 U8 uchIDRequired)
{
	return character_ActQueuedCancelReason(iPartitionIdx, pchar, pdefReplacement, uchIDRequired, kAttribType_Null, true);
}

U8 character_ActInstantFinish(int iPartitionIdx,
								Character *pchar,
								PowerActivation *pact)
{
	U8 uchID = 0;

	if(pact)
	{
		U32 uiTime = pact->uiTimestampCurrented;
		PowerDef *pdef = GET_REF(pact->hdef);
		PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;

		// We NEED to find this power, it might be on an item and needs to go on cooldown no matter what
		Power *ppow = character_FindPowerByRefComplete(pchar,&pact->ref);

		uchID = pact->uchID;

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Instant %d: FINISH: at %d\n",uchID,uiTime);

		if (ppow)
		{
			power_SetRechargeDefault(iPartitionIdx,pchar,ppow,!pact->bHitTargets);
			power_SetCooldownDefault(iPartitionIdx,pchar,ppow);
			power_SetActive(ppow,false);
		}
	}

	return uchID;
}


S32 character_ActivationHasReachedHit(SA_PARAM_NN_VALID PowerActivation *pact)
{
	F32 fTimeActAdjust = 0.f;
	if(pact->eLungeMode != kLungeMode_None)
	{
		fTimeActAdjust = (pact->uiTimestampLungeMoveStop - pact->uiTimestampLungeMoveStart)/PAFX_FPS;
	}

	return MAX(pact->fTimeActivating, 0.f) + fTimeActAdjust >= pact->fActHitTime;
}

// Same as above, but takes the type of attrib that caused the cancel
// Cancels the activation that is currently current.  Will not cancel
//  current activations that have successfully reached the 'hit' point
//  of the power, unless bForce is true.  If the eType is a type ignored
//  by the Power, it will not be canceled. Returns the activation id of
//  the canceled Activation.
U8 character_ActCurrentCancelReason(int iPartitionIdx,
									Character *pchar,
									S32 bForce,
									S32 bRefundCost,
									S32 bRecharge,
									AttribType eType)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	U8 uchID = 0;
	PowerActivation *pact = pchar->pPowActCurrent;
	S32 bChangeChargeMode = true;
	bool bSetClientRecharge = false;

	if(pact)
	{
		PowerDef *pdef = GET_REF(pact->hdef);
		// If the type is null
		//  Or the type is Interrupt and the PowerDef allows Attrib-type interruptions
		//  Or the type isn't Interrupt and the PowerDef doesn't ignore that Attrib altogether
		if(pdef
			&& (eType == kAttribType_Null
				|| (eType == kAttribType_Interrupt && pdef->eInterrupts&kPowerInterruption_InterruptAttrib)
				|| (eType != kAttribType_Interrupt && !powerdef_IgnoresAttrib(pdef,eType))))
		{
			U32 uiTime = pact->uiTimestampCurrented;
			PowerAnimFX *pafx = GET_REF(pdef->hFX);
			S32 bCancel = true;
			S32 bHit = false;

			// We NEED to find this power, it might be on an item and needs to go on cooldown no matter what
			Power *ppow = character_FindPowerByRefComplete(pchar,&pact->ref);

			uchID = pact->uchID;

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %d: CANCEL: at %d\n",uchID,uiTime);

			if(pafx)
			{
				if(pact->bActivated)
				{
					if(pact->eLungeMode == kLungeMode_Pending)
						pact->eLungeMode = kLungeMode_None;
					
					// See if the animation has reached the hit point
					bHit = character_ActivationHasReachedHit(pact);;
				}
			}

			// Don't bother mucking with the stance

			if(!bForce && pdef->bDoNotAllowCancelBeforeHitFrame && !bHit)
			{	// the current active power doesn't allow
				bCancel = false;
				uchID = 0;
			}
			else if(pafx && pchar->eChargeMode==kChargeMode_Current)
			{
				// Cancel all the stuff charging had started
				character_AnimFXChargeCancel(iPartitionIdx,pchar,pact, bForce || (eType == kAttribType_Interrupt));
			}
			else
			{
				// If it's a maintained power that is being maintained, deactivate it
				if(ppow && pchar->eChargeMode==kChargeMode_CurrentMaintain)
				{
					character_DeactivateMaintained(iPartitionIdx, pchar, ppow, pact, NULL, pmTimestamp(0));
					pchar->eChargeMode = kChargeMode_None;
				}

				// Check if it's already created AttribMods
				if(pact->bActivated)
				{
					if(bHit && !bForce)
					{
						// We're not forcing this to shut down, and it's made it past the first hit
						//  so we're going to let it continue
						bCancel = false;
						uchID = 0;
					}
					else if(bRecharge && ppow && pdef->eType != kPowerType_Toggle && 
							(!g_CombatConfig.bPayPowerCostAndRechargePostHitframe || bHit))
					{
						// Put it into recharge
						power_SetRechargeDefault(iPartitionIdx,pchar,ppow,!pact->bHitTargets);
						power_SetCooldownDefault(iPartitionIdx,pchar,ppow);
						bSetClientRecharge = true;
					}
				}
			}

			bChangeChargeMode = bCancel;

			if(bCancel)
			{
				PowerTarget* ppowTarget = GET_REF(pdef->hTargetMain);

				// if we haven't put it into recharge yet, force the recharge now if it was canceled due to an interrupt type
				if (!bSetClientRecharge && ppow && pdef->bForceRechargeOnInterrupt && 
					pdef->eType != kPowerType_Toggle && IS_INTERRUPT_ASPECT(eType))
				{
					power_SetRechargeDefault(iPartitionIdx,pchar,ppow,!pact->bHitTargets);
					power_SetCooldownDefault(iPartitionIdx,pchar,ppow);
					bSetClientRecharge = true;
				}

				if (g_CombatConfig.bPayPowerCostAndRechargePostHitframe && bHit && !pact->bPaidCost && ppow)
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pchar->pEntParent);
					character_PayPowerCost(iPartitionIdx, pchar, ppow, pact->erTarget, pact, true, pExtract);
				}

				if(pact->bPlayedActivate)
				{
					character_AnimFXActivateCancel(iPartitionIdx, pchar, pact, true, bHit);
				}
				else if (pact->eActivationStage == kPowerActivationStage_Preactivate)
				{
					character_AnimFXPreactivateCancel(iPartitionIdx, pchar, pact);
				}

				if(pact->bActivated && !bHit)
				{
					CharacterActCancelMods(iPartitionIdx,pchar,pact,false);
#if GAMESERVER
					gslCombatDeathPrediction_NotifyPowerCancel(entGetPartitionIdx(pchar->pEntParent), pchar, pact);
#endif
				}
				else
				{
					CharacterActCancelPowerRef(pchar, &pact->preActivateRef, entGetRef(pchar->pEntParent), 0, false);
				}

#ifdef GAMECLIENT
				// for death prediction on the client- always notify that the power is canceled
				// just in the case that the client is ahead and thinks the hit occurred, but it wasn't on the server
				// revisit this if it happens often and makes death look off too much
				gclCombatDeathPrediction_NotifyActCanceled(pchar, pact);
#endif


#ifdef GAMESERVER
				if(bRefundCost && !bHit && pact->fCostPaid > 0)
				{
					AttribType eAttribCost = POWERDEF_ATTRIBCOST(pdef);
					*F32PTR_OF_ATTRIB(pchar->pattrBasic,eAttribCost) += pact->fCostPaid;
					character_DirtyAttribs(pchar);
				}

				if(g_CombatConfig.bClientChargeData && pchar->pChargeData)
				{
					StructDestroySafe(parse_CharacterChargeData,&pchar->pChargeData);
					pchar->bChargeDataDirty = true;
				}

				// Notify the client that its power has been canceled
				ClientCmd_PowersCancelPowers(pchar->pEntParent,pact->uchID,true,bSetClientRecharge,pact->ref.uiID);
#endif

				if(ppow && s_funcNotifyExecutedCallback)
					s_funcNotifyExecutedCallback(pchar->pEntParent, ppow);

				if(ppow)
				{
					power_SetActive(ppow,false);
				}
				
				if (pdef->bActivationImmunity)
				{
					pchar->bPowerActivationImmunity = false;
					character_AnimFxPowerActivationImmunityCancel(iPartitionIdx, pchar, pact);
				}

				// Free the activation
				poweract_DestroySafe(&pchar->pPowActCurrent);
			}
		}
		else if(pdef)
		{
			// Power ignores the reason, so we don't want to change the charge mode
			bChangeChargeMode = false;
		}
	}
#ifdef GAMESERVER
	else if (g_CombatConfig.bPayPowerCostAndRechargePostHitframe && 
			pchar->pPowActFinished && pchar->pPowActFinished->fTimeFinished <= gConf.combatUpdateTimer)
	{	// check to see if we *just* finished an activation and if we did, just send the recharge time to the client
		// just to make sure the client's recharge is correct.
		Power *ppow = character_FindPowerByID(pchar, pchar->pPowActFinished->ref.uiID);

		PowersDebugPrintEnt(EPowerDebugFlags_RECHARGE, pchar->pEntParent, 
							"PowersCancelPowers cmd: Client cancel mispredict. Reset recharge. %.2f time finished.\n",
							pchar->pPowActFinished->fTimeFinished);

		if (ppow)
		{
			ClientCmd_PowerSetRechargeClient(pchar->pEntParent, ppow->uiID, ppow->fTimeRecharge);
		}
	}
#endif
	// Make sure we clean up the ChargeMode regardless
	if(bChangeChargeMode && (pchar->eChargeMode==kChargeMode_Current || pchar->eChargeMode==kChargeMode_CurrentMaintain))
	{
		pchar->eChargeMode = kChargeMode_None;
	}

	return uchID;
#endif
}

// Wrapper for character_ActCurrentCancelReason()
U8 character_ActCurrentCancel(int iPartitionIdx,
							  Character *pchar,
							  S32 bForce,
							  S32 bRecharge)
{
	return character_ActCurrentCancelReason(iPartitionIdx, pchar, bForce, false, bRecharge ,kAttribType_Null );
}



// Attempts to cancel all Activations
U8 character_ActAllCancelReason(int iPartitionIdx,
								Character *pchar,
								U32 bForceCurrent,
								AttribType eType)
{
	U8 bCancelledOverflow, bCancelledQueued, bCancelledCurrent;
	U8 uchOverflow, uchQueued, uchCurrent;
	PERFINFO_AUTO_START_FUNC();
	uchOverflow = pchar->pPowActOverflow ? pchar->pPowActOverflow->uchID : 0;
	uchQueued = pchar->pPowActQueued ? pchar->pPowActQueued->uchID : 0;
	uchCurrent = pchar->pPowActCurrent ? pchar->pPowActCurrent->uchID : 0;
	bCancelledOverflow = character_ActOverflowCancelReason(iPartitionIdx, pchar, NULL, 0, eType, true);
	bCancelledQueued = character_ActQueuedCancelReason(iPartitionIdx, pchar, NULL, 0, eType, true);
	bCancelledCurrent = character_ActCurrentCancelReason(iPartitionIdx, pchar, bForceCurrent, false, true, eType);
	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActAllCancelReason %d %d: %d %d %d; %d %d %d\n",bForceCurrent,eType,uchOverflow,uchQueued,uchCurrent,bCancelledOverflow,bCancelledQueued,bCancelledCurrent);
	PERFINFO_AUTO_STOP();
	return (bCancelledOverflow || bCancelledQueued || bCancelledCurrent);
}

// Wrapper for character_ActAllCancelReason()
U8 character_ActAllCancel(int iPartitionIdx,
						  Character *pchar,
						  U32 bForceCurrent)
{
	return character_ActAllCancelReason(iPartitionIdx, pchar, bForceCurrent, kAttribType_Null);
}



// Finishes the activation that is current.  A more natural termination
//  than cancel.  Override indicates if another power is overriding it
//  (thus kicking it out early).
void character_ActCurrentFinish(int iPartitionIdx, Character *pchar, bool bOverride)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PowerActivation *pact = pchar->pPowActCurrent;

	if(pact)
	{
		Power *ppow = character_ActGetPower(pchar,pact);
		if(bOverride)
		{
			// TODO(JW): Finish: Do some cleanup for overrides?
		}

		if (g_CombatConfig.bPayPowerCostAndRechargePostHitframe && !pact->bPaidCost && ppow)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pchar->pEntParent);
			character_PayPowerCost(iPartitionIdx, pchar, ppow, pact->erTarget, pact, true, pExtract);
		}

		// Throw away old finished, move current into finished
		character_ActFinishedDestroy(pchar);
		if(!verify(pchar->pPowActFinished==NULL))
			poweract_DestroySafe(&pchar->pPowActFinished);
		pchar->pPowActFinished = pact;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Current %d: Finished activation (%f)%s\n",pact->uchID,pact->fTimeActivating,bOverride?" (Override)":"");
		pchar->pPowActCurrent = NULL;

		if(ppow)
		{
			PowerDef *pdef = GET_REF(ppow->hDef);

			if (pdef)
			{
				PowerTarget* ppowTarget = GET_REF(pdef->hTargetMain);

				if(pdef->eType==kPowerType_Toggle)
				{
					if(!g_CombatConfig.bToggleCooldownOnDeactivation)
						power_SetCooldownDefault(iPartitionIdx,pchar,ppow);
				}
				else
				{
					power_SetActive(ppow,false);
				}

				if (pdef->bActivationImmunity)
				{
					pchar->bPowerActivationImmunity = false;
					character_AnimFxPowerActivationImmunityCancel(iPartitionIdx, pchar, pact);
				}

				// Update the PowersMovement target
				// JW: This shouldn't do this - activation facing should have nothing to do
				//  with selected facing.
				if (ppowTarget && ppowTarget->bFaceActivateSticky)
					pmUpdateSelectedTarget(pchar->pEntParent, false);
			}
			else
			{
				power_SetActive(ppow,false);
			}
		}

		// Call this after setting inactive, since the AI needs to think about powers usage
		if(s_funcNotifyExecutedCallback && ppow)
			s_funcNotifyExecutedCallback(pchar->pEntParent, ppow);
	}
#endif
}


// Returns true if the character is allowed to activate the given def
S32 character_CanActivatePowerDef(Character *pchar,
								  PowerDef *pdef,
								  S32 bTest,
								  S32 bActiveToggle,
								  ActivationFailureReason *peFailOut,
								  GameAccountDataExtract *pExtract,
								  bool *pbGaveFeedbackOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (pbGaveFeedbackOut) 
		*pbGaveFeedbackOut = false;

	// Can't activate innate or enhancement powers
	if(pdef->eType==kPowerType_Innate || pdef->eType==kPowerType_Enhancement)
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Innate or Enhancement power (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

	// Can't activate anything while NearDeath
	if(pchar->pNearDeath && !(pdef->eActivateRules & kPowerActivateRules_SourceDead))
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while NearDeath (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

	// Can't activate something that needs you alive if you're dead
	if(!entIsAlive(pchar->pEntParent) && !(pdef->eActivateRules & kPowerActivateRules_SourceDead))
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while dead (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

	// Can't activate something that needs you dead if you're alive
	if(entIsAlive(pchar->pEntParent) && !pchar->pNearDeath && !(pdef->eActivateRules & kPowerActivateRules_SourceAlive))
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while alive (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

	// Can't activate most things if you could be in BattleForm but aren't
	if(g_CombatConfig.pBattleForm
		&& !pchar->bBattleForm
		&& IS_HANDLE_ACTIVE(pchar->pEntParent->hAllegiance)
		&& !pdef->bActivateWhileMundane)
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while mundane (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

	// Can't activate something if you don't got the modes
	if(!character_ModesAllowPowerDef(pchar,pdef))
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Prevented by modes (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_PowerModeDisallowsUsage);
		return false;
	}

	// Can't activate most powers while held
	if(SAFE_MEMBER(pchar->pattrBasic, fHold) > 0.0f 
		&& pdef->eType!=kPowerType_Passive 
		&& !powerdef_IgnoresAttrib(pdef,kAttribType_Hold))
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while held (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

	// Can't activate most powers while disabled
	if(SAFE_MEMBER(pchar->pattrBasic, fDisable) > 0.0f 
		&& pdef->eType!=kPowerType_Passive 
		&& !powerdef_IgnoresAttrib(pdef,kAttribType_Disable))
	{
		if (!bTest)
		{
			ActivationFailureParams failureParams = { 0 };
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while disabled (%s)\n",pdef->pchName);
			character_ActivationFailureFeedback(kActivationFailureReason_Disabled, &failureParams);
			
			if (pbGaveFeedbackOut) 
				*pbGaveFeedbackOut = true;
		}
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Disabled);
		return false;
	}

	if(pdef->bDisallowWhileRooted && 
		SAFE_MEMBER(pchar->pattrBasic, fRoot) > 0.0f 
		&& pdef->eType!=kPowerType_Passive)
	{
		if (!bTest)
		{
			ActivationFailureParams failureParams = { 0 };
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Power disallowed while rooted (%s)\n",pdef->pchName);
			character_ActivationFailureFeedback(kActivationFailureReason_Rooted, &failureParams);

			if (pbGaveFeedbackOut) 
				*pbGaveFeedbackOut = true;
		}
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Rooted);
		return false;
	}


	// Can't activate most powers while being knocked.  Active toggles are allowed to continue if the CombatConfig
	//  doesn't include knock as a toggle interruption.  Ignore if dead, since dead things likely have ragdoll on them
	if(pmKnockIsActive(pchar->pEntParent) && entIsAlive(pchar->pEntParent)
		&& pdef->eType!=kPowerType_Passive
		&& !powerdef_IgnoresAttrib(pdef,kAttribType_KnockBack)
		&& (!bActiveToggle || (g_CombatConfig.eInterruptToggles&kPowerInterruption_Knock && 
								pdef->eInterrupts&kPowerInterruption_Knock)))
	{
		if (!bTest)
		{
			ActivationFailureParams failureParams = { 0 };
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while knocked (%s)\n",pdef->pchName);
			character_ActivationFailureFeedback(kActivationFailureReason_Knocked, &failureParams);
	
			if (pbGaveFeedbackOut) 
				*pbGaveFeedbackOut = true;
		}
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Knocked);
		return false;
	}

	// Can't activate most powers while holding an object physically
	if(!bActiveToggle
		&& IS_HANDLE_ACTIVE(pchar->hHeldNode)
		&& pdef->eType!=kPowerType_Passive
		&& !powerdef_IgnoresAttrib(pdef,kAttribType_BePickedUp))
	{
		if (!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed while holding an object (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

	// if it's a weapon based power, check if we have the right items equipped, 
	// but don't evaluate needed weapons for entCreated entities 
	// (we might want to do this for all entities, and only check for players?)
	if (pdef->bWeaponBased && !pchar->uiPowersCreatedEntityTime)
	{
		char const * pchMissingCategory;
		if (!character_HasAllRequiredItemsEquipped(pchar, pdef, pExtract, &pchMissingCategory))
		{
			if (!bTest)
			{
				ActivationFailureParams failureParams = { 0 };

				failureParams.pchStringParam = pchMissingCategory;

				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: No valid equipped item (%s)\n",pdef->pchName);

				character_ActivationFailureFeedback(kActivationFailureReason_DoesNotHaveRequiredItemEquipped, &failureParams);
				
				if (pbGaveFeedbackOut) 
					*pbGaveFeedbackOut = true;
			}
			SetActivationFailureReason(peFailOut, kActivationFailureReason_DoesNotHaveRequiredItemEquipped);
			return false;
		}
	}

	if (!character_HasRequiredAnyTacticalModes(pchar, pdef))
	{
		if(!bTest)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Power requires the player to be in a given tactical mode(%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return false;
	}

#if defined(GAMECLIENT) || defined(GAMESERVER)
	// Can't activate powers that are restricted by the region rules
	if(entGetWorldRegionTypeOfEnt(pchar->pEntParent) > -1)
	{
		if(!powerdef_RegionAllowsActivate(pdef,entGetWorldRegionTypeOfEnt(pchar->pEntParent)))
		{
			if(!bTest)
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanActivate: FAIL: Not allowed to activate this power in this region (%s)\n",pdef->pchName);
			SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
			return false;
		}
	}
#endif

	return true;
#endif
}

// Gets the available "cost" with respect to a particular Attribute
static F32 GetPowerCostAvailable(SA_PARAM_NN_VALID Character *pchar, AttribType eAttrib)
{
	F32 fAttrib = *F32PTR_OF_ATTRIB(pchar->pattrBasic,eAttrib);

#ifdef GAMECLIENT
	if(pchar->pPowActCurrent)
	{
		PowerActivation *pact = pchar->pPowActCurrent;
		PowerDef *pdef = GET_REF(pact->hdef);
		AttribType eAttribCostCurrent = POWERDEF_ATTRIBCOST(pdef);
		if(eAttrib==eAttribCostCurrent)
		{
			fAttrib -= pact->fCostPaid;
			fAttrib += pact->fCostPaidServer;
		}
		else if(eAttrib==kAttribType_Power)
		{
			// Current might also have a secondary cost which is always Power
			fAttrib -= pact->fCostPaidSecondary;
			fAttrib += pact->fCostPaidServerSecondary;
		}
	}
#endif

	// If the cost is HitPoints we pretend you've got just slightly less than
	//  what you've actually got - just to make sure you can't kill yourself.
	if(eAttrib==kAttribType_HitPoints)
		fAttrib -= 0.01;

	return fAttrib;
}

S32 character_PayPowerCost(int iPartitionIdx,
						   Character *pchar,
						   Power *ppow,
						   EntityRef erTarget,
						   PowerActivation *pact,
						   S32 bPay,
						   GameAccountDataExtract *pExtract)
{
	static PowerActivation *s_pact = NULL;
	PowerDef *pdef;
	ItemDef* pRecipe;
	Expression *pExprCost;
	F32 fCost = 0;
	F32 fCostSecondary = 0;
	F32 fAvailable = 0;
	F32 fAvailableSecondary = 0;
	AttribType eAttribCost = kAttribType_Power;

	pdef = GET_REF(ppow->hDef);
	if (!pdef)
		return false; // This can happen if the power def is not loaded yet

	// Check PowerMode if it's specified and this isn't a periodic activation
	if(pdef->iCostPowerMode && !(pact && pact->uiPeriod))
	{
		if(!character_HasMode(pchar,pdef->iCostPowerMode))
		{
			if(bPay)
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: FAIL: Can't pay PowerMode %s (%s)\n",StaticDefineIntRevLookup(PowerModeEnum,pdef->iCostPowerMode),pdef->pchName);
			return false;
		}
	}

	pExprCost = (pact && pact->uiPeriod && pdef->pExprCostPeriodic) ? pdef->pExprCostPeriodic : pdef->pExprCost;

	if(pExprCost)
	{
		Entity *eSource = pchar->pEntParent;
		Entity *eTarget = entFromEntityRef(iPartitionIdx,erTarget);
		Expression *pExprCostSecondary;
		F32 fDiscount;

		eAttribCost = POWERDEF_ATTRIBCOST(pdef);

		if(!s_pact)
		{
			s_pact = poweract_Create();
		}

		if(!pact)
		{
			pact = s_pact;
			// TODO(JW): Activation: Clean up and/or fill in the static activation with useful data if required
			pact->fCostPaid = 0;
		}

		combateval_ContextReset(kCombatEvalContext_Activate);
		combateval_ContextSetupActivate(pchar,eTarget ? eTarget->pChar : NULL,pact,kCombatEvalPrediction_None);
		fCost = combateval_EvalNew(iPartitionIdx,pExprCost,kCombatEvalContext_Activate,NULL);

		fDiscount = character_PowerBasicAttrib(iPartitionIdx,pchar,ppow,kAttribType_DiscountCost,0);
		if (g_CombatConfig.bDiscountCostIsAbsolute)
		{
			if(fDiscount<0)
			{
				fDiscount = 0;
			}
			fCost = max(fCost-fDiscount,0);
		}
		else
		{
			if(fDiscount<=0)
			{
				fDiscount = 1;
			}
			fCost /= fDiscount;
		}
		fAvailable = GetPowerCostAvailable(pchar,eAttribCost);
		if(fCost>0.f && fAvailable < fCost)
		{
			if(bPay)
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: FAIL: Can't pay %f %s (has %f) (%s)\n",fCost,StaticDefineIntRevLookup(AttribTypeEnum,eAttribCost),fAvailable,pdef->pchName);
			return false;
		}

		// Check secondary cost if there is one
		pExprCostSecondary = (pact && pact->uiPeriod && pdef->pExprCostPeriodicSecondary) ? pdef->pExprCostPeriodicSecondary : pdef->pExprCostSecondary;
		if(pExprCostSecondary)
		{
			fCostSecondary = combateval_EvalNew(iPartitionIdx,pExprCostSecondary,kCombatEvalContext_Activate,NULL);

			if (g_CombatConfig.bDiscountCostIsAbsolute)
				fCostSecondary = max(fCostSecondary-fDiscount,0);
			else
				fCostSecondary /= fDiscount;

			fAvailableSecondary = GetPowerCostAvailable(pchar,kAttribType_Power);
			if(fCostSecondary>0.f && fAvailableSecondary<fCostSecondary)
			{
				if(bPay)
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: FAIL: Can't pay %f %s (has %f) (%s)\n",fCostSecondary,StaticDefineIntRevLookup(AttribTypeEnum,kAttribType_Power),fAvailableSecondary,pdef->pchName);
				return false;
			}
		}
	}

	pRecipe = GET_REF(pdef->hCostRecipe);
	if (pRecipe && !inv_EntCouldCraftRecipe(pchar->pEntParent, pRecipe))
	{
		if(bPay)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: FAIL: Can't afford power cost recipe.\n");
		return false;
	}

	if(bPay)
	{
		pact->bPaidCost = true;

#ifdef GAMESERVER
		if (pRecipe)
		{
			ItemChangeReason reason = {0};
			Entity *pEnt = pchar->pEntParent;
			inv_FillItemChangeReason(&reason, pEnt, "Powers:PayCost", pdef->pchName);

			if (AutoTrans_tr_InventoryRemoveRecipeComponents(NULL, GetAppGlobalType(),
						entGetType(pEnt), entGetContainerID(pEnt),
						pRecipe->pchName, &reason, pExtract) == TRANSACTION_OUTCOME_FAILURE)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: FAIL: Item removal transaction failed.\n");
				return false;
			}
			else
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: Paid items according to recipe %s.\n", pRecipe->pchName);
			}
		}

		if(pdef->iCostPowerMode && !(pact && pact->uiPeriod))
		{
			int i;
			for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
			{
				AttribModDef *pmoddef = pchar->modArray.ppMods[i]->pDef;
				if(pmoddef->offAttrib==kAttribType_PowerMode && pmoddef->offAspect==kAttribAspect_BasicAbs)
				{
					PowerModeParams *pParams = (PowerModeParams*)pmoddef->pParams;
					if(pParams && pParams->iPowerMode==pdef->iCostPowerMode)
					{
						character_ModExpireReason(pchar, pchar->modArray.ppMods[i], kModExpirationReason_Unset);
					}
				}
			}
		}
#endif

		if(fCost > 0)
		{
			if(pact)
				pact->fCostPaid = fCost;

#ifdef GAMESERVER
			*F32PTR_OF_ATTRIB(pchar->pattrBasic,eAttribCost) -= fCost;
			character_DirtyAttribs(pchar);
			ClientCmd_Powers_SetCostPaidServer(pchar->pEntParent,pact->uchID,pact->fCostPaid);
#endif
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: Paid cost %f %s (has %f left) (%s)\n",fCost,StaticDefineIntRevLookup(AttribTypeEnum,eAttribCost),fAvailable-fCost,pdef->pchName);
		}

		if(fCostSecondary > 0)
		{
			if(pact)
				pact->fCostPaidSecondary = fCostSecondary;

#ifdef GAMESERVER
			*F32PTR_OF_ATTRIB(pchar->pattrBasic,kAttribType_Power) -= fCostSecondary;
			character_DirtyAttribs(pchar);
			ClientCmd_Powers_SetCostPaidServerSecondary(pchar->pEntParent,pact->uchID,pact->fCostPaidSecondary);
#endif
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "PayCost: Paid cost %f %s (has %f left) (%s)\n",fCostSecondary,StaticDefineIntRevLookup(AttribTypeEnum,kAttribType_Power),fAvailableSecondary-fCostSecondary,pdef->pchName);
		}
	}

	return true;
}

// helper function for CharacterActivationAllowsQueue, checks if the current activation is blocking power queueing
static bool CharacterActivationIsActivationBlockingQueuing(	PowerActivation *pAct, PowerDef *pDefAct, Power *pPowActParent, 
															Power *pPowQueue, Power *pPowQueueParent, PowerDef *pPowQueueDef)
{
	if (!entIsServer() && pPowQueueParent == pPowActParent && (!pPowQueueDef->bAlwaysQueue || pPowQueue == pPowActParent))
	{
		if (pAct->eActivationStage != kPowerActivationStage_PostMaintain)
		{
			return (pDefAct->fTimeAllowQueue < pDefAct->fTimeActivate - pAct->fTimeActivating);
		}
		else
		{
			return pDefAct->fTimeAllowQueue < pAct->fStageTimer;
		}
	}

	return false;
}


static bool CharacterActivationAllowsQueue(Character *pchar,
										   PowerActivation *pact,
										   Power *ppow,
										   PowerDef *pdef,
										   Power *ppowParent,
										   ActivationFailureReason *peFailOut)
{
	PowerDef *pdefAct;

	pdefAct = GET_REF(pact->hdef);

	if(verify(pdefAct))
	{
		PowerDef *ppowActParentDef = NULL;
		Power *ppowActParent = character_ActGetPower(pchar,pact);
		if(ppowActParent && ppowActParent->pParentPower)
		{
			ppowActParent = ppowActParent->pParentPower;
		}
		if(ppowActParent)
		{
			ppowActParentDef = GET_REF(ppowActParent->hDef);
		}

		// Technically the PowerMode cost should be checked as cost time, but
		//  it's just way easier to check it here and completely prevent queueing
		//  if another activation has or will consume the same PowerMode.  Otherwise
		//  we have to start predicting PowerMode state on the client, which is a pain.
		if(pdef->iCostPowerMode && pdef->iCostPowerMode==pdefAct->iCostPowerMode)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "AllowQueue: FAIL: Activation %d also cost PowerMode %s (%s)\n",pact->uchID,StaticDefineIntRevLookup(PowerModeEnum,pdef->iCostPowerMode),pdef->pchName);
			return false;
		}

		// Activation wants to recharge
		if(powerdef_GetRechargeDefault(pdefAct) > 0)
		{
			// Not allowed because currently needs to recharge and they have the same parent
			if(ppowActParent==ppowParent)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "AllowQueue: FAIL: Activation %d requires recharging, shared parent (%s)\n",pact->uchID,pdef->pchName);
				return false;
			}
		}

		if(ppowActParentDef && ppowActParentDef->bRequiresCooldown && pdefAct->bRequiresCooldown)
		{
			//See if they have the same cooldown required
			int i;

			for(i=0;i<ea32Size(&ppowActParentDef->piCategories);i++)
			{
				if(ea32Find(&pdef->piCategories,ppowActParentDef->piCategories[i]) > -1)
				{
					PowerCategory *pCategory = g_PowerCategories.ppCategories[ppowActParentDef->piCategories[i]];
					if(pCategory->fTimeCooldown > 0.f)
					{
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "AllowQueue: FAIL: Activation %d requires a shared cooldown (%s)\n",pact->uchID,pdef->pchName);
						return false;
					}
				}
			}
		}

		// Players are not allowed to activate because the act's power is blocking queueing
		if (CharacterActivationIsActivationBlockingQueuing(pact, pdefAct, ppowActParent, ppow, ppowParent, pdef))
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "AllowQueue: FAIL: Activation %d isn't allowing queuing (%f %f) (%s)\n",pact->uchID,pdefAct->fTimeAllowQueue,pdefAct->fTimeActivate - pact->fTimeActivating,pdef->pchName);
			return false;
		}

		// Not allowed to activate because the act's power is an auto-charge/maintain that isn't already
		//  and the end of its life, and is still being charged/maintained
		if(!(pdefAct->eInterrupts&kPowerInterruption_Requested)
			&& (!pdefAct->uiPeriodsMax || pact->uiPeriod!=pdefAct->uiPeriodsMax))
		{
			if((pact==pchar->pPowActCurrent && (pchar->eChargeMode==kChargeMode_Current || pchar->eChargeMode==kChargeMode_CurrentMaintain))
				|| (pact==pchar->pPowActQueued && (pchar->eChargeMode==kChargeMode_Queued || pchar->eChargeMode==kChargeMode_QueuedMaintain)))
			{
				SetActivationFailureReason(peFailOut, kActivationFailureReason_PriorActNonInterrupting);
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "AllowQueue: FAIL: Activation %d is auto-charging (%s)\n",pact->uchID,pdef->pchName);
				return false;
			}
		}
	}

	return true;
}

// Returns true if the given power had an associated base power activated via the CombatPowerStateSwitch system
static bool character_IsPowerCombatPowerStateSwitchActviated(Character *pChar, Power *pTestStatePower, PowerActivation* pAct)
{
#if GAMESERVER || GAMECLIENT
	Power *pCurrentPower = character_ActGetPower(pChar, pAct);
	if (pCurrentPower)
	{
		if (pTestStatePower == CombatPowerStateSwitching_GetBasePower(pCurrentPower))
		{
			return true;
		}
	}
#endif

	return false;
}


S32 character_ActTestStatic(Character *pchar,
							Power *ppow,
							PowerDef *pdef,
							ActivationFailureReason *peFailOut,
							GameAccountDataExtract *pExtract,
							bool bNoFeedback)
							
{
	ActivationFailureReason failOut = kActivationFailureReason_None;
	bool bGaveFeedback = false;

	// Can't queue passives
	if(pdef->eType==kPowerType_Passive)
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestStatic: FAIL: Passive (%s)\n",pdef->pchName);
		return 0;
	}

	if (!peFailOut)
		peFailOut = &failOut;

	// Not allowed to activate the def in general
	if(!character_CanActivatePowerDef(pchar, pdef, true, false, peFailOut, pExtract, &bGaveFeedback))
	{
		if (!bNoFeedback && !bGaveFeedback)
		{
			ActivationFailureParams failureParams = { 0 };
			character_ActivationFailureFeedback(*peFailOut, &failureParams);
		}
				
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestStatic: FAIL: Not allowed to activate def (%s)\n",pdef->pchName);
		return 0;
	}

	// Can't queue toggles that are already current or queued
	if(pdef->eType==kPowerType_Toggle)
	{
		if((pchar->pPowActCurrent && character_ActGetPower(pchar,pchar->pPowActCurrent)==ppow)
			|| (pchar->pPowActQueued && character_ActGetPower(pchar,pchar->pPowActQueued)==ppow)
			|| (pchar->pPowActOverflow && character_ActGetPower(pchar,pchar->pPowActOverflow)==ppow))
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestStatic: FAIL: Toggle already current or queued (%s)\n",pdef->pchName);
			SetActivationFailureReason(peFailOut,kActivationFailureReason_Other);
			return 0;
		}
	}

#if GAMESERVER || GAMECLIENT
	if (pchar->pPowActCurrent)
	{
		Power *pTestStatePower = CombatPowerStateSwitching_GetBasePower(ppow);
		if (pTestStatePower)
		{
			if ( (pchar->pPowActCurrent && character_IsPowerCombatPowerStateSwitchActviated(pchar, pTestStatePower, pchar->pPowActCurrent))  || 
				 (pchar->pPowActQueued && character_IsPowerCombatPowerStateSwitchActviated(pchar, pTestStatePower, pchar->pPowActQueued))  || 
				 (pchar->pPowActOverflow && character_IsPowerCombatPowerStateSwitchActviated(pchar, pTestStatePower, pchar->pPowActOverflow)))
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestStatic: FAIL: Cannot Queue a combat power state when its associated power is current (%s)\n",pdef->pchName);
				SetActivationFailureReason(peFailOut,kActivationFailureReason_Other);
				return 0;
			}
		}
	}
#endif

	return 1;
}

S32 character_ActTestDynamic(	int iPartitionIdx,
								Character *pchar,
								Power *ppow,
								Entity *pentTarget,
								Vec3 vecTargetSecondary,
								WorldInteractionNode *pnodeTarget,
								Entity **ppentTargetOut,
								WorldInteractionNode **ppnodeTargetOut,
								ActivationFailureReason *peFailOut,
								S32 bCheckRange,
								bool bNoFeedback,
								bool bCheckRecharge)
{
	Power *ppowParent = ppow;
	PowerDef *pdef, *pdefParent;
	Vec3 vNodeTargetPos;
	bool bNodePosSet = false;

	pdef = GET_REF(ppow->hDef);

	if(!pdef)
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Unknown def\n");
		return 0;
	}

	assert(pchar->pEntParent);

	ppowParent = ppow;
	pdefParent = pdef;

	pdef = GET_REF(ppow->hDef);
	ppowParent = ppow->pParentPower ? ppow->pParentPower : ppow;
	pdefParent = GET_REF(ppowParent->hDef);
	ANALYSIS_ASSUME(pdefParent);

	// If power does not allow for activating powers towards an illegal target
	if(character_PowerRequiresValidTarget(pchar, pdef))
	{
		PowerTarget *pPowTarget = GET_REF(pdef->hTargetMain);
		PowerTarget *pPowTargetAffected = GET_REF(pdef->hTargetAffected);


		if(pPowTarget)
		{
			if(pentTarget)
			{
				if(!character_TargetMatchesPowerType(iPartitionIdx,pchar,pentTarget->pChar,pPowTarget))
				{
					if (!bNoFeedback)
					{
						ActivationFailureParams failureParams = { 0 };
						failureParams.pEnt = pentTarget;

						character_ActivationFailureFeedback(kActivationFailureReason_TargetInvalid, &failureParams);
					}
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Target type mismatch (%s %s)\n",pdef->pchName,ENTDEBUGNAME(pentTarget));
					SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetInvalid);
					return 0;
				}
				if(!character_CanPerceive(iPartitionIdx,pchar,pentTarget->pChar))
				{
					if (!bNoFeedback)
					{
						ActivationFailureParams failureParams = { 0 };
						failureParams.pEnt = pentTarget;

						character_ActivationFailureFeedback(kActivationFailureReason_TargetImperceptible, &failureParams);
					}
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Target imperceptible (%s %s)\n",pdef->pchName,ENTDEBUGNAME(pentTarget));
					SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetImperceptible);
					return 0;
				}
				if(pdef->eTargetVisibilityMain == kTargetVisibility_LineOfSight)
				{
					Vec3 vSource, vTarget;
					entGetCombatPosDir(pchar->pEntParent, NULL, vSource, NULL);
					entGetCombatPosDir(pentTarget, NULL, vTarget, NULL);
					if(!combat_CheckLoS(iPartitionIdx,vSource, vTarget, pchar->pEntParent, pentTarget, NULL, false, false, NULL))
					{
						// HACK(JW): If a node was also sent in, that means we're attempting to LoS check
						//  against a node that was just swapped into an entity, which means the physics
						//  may not be up-to-date, so we don't fail.
						if(!pnodeTarget)
						{
							if (!bNoFeedback)
							{
								ActivationFailureParams failureParams = { 0 };
								failureParams.pEnt = pentTarget;

								character_ActivationFailureFeedback(kActivationFailureReason_TargetLOSFailed, &failureParams);
							}
							PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Target failed LoS check (%s %s)\n",pdef->pchName,ENTDEBUGNAME(pentTarget));
							SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetLOSFailed);
							return 0;
						}
					}

					// Secondary target LOS check
					if (pdef->fRangeSecondary > 0.0f && vecTargetSecondary && !ISZEROVEC3(vecTargetSecondary) &&
						!combat_CheckLoS(iPartitionIdx,vSource, vecTargetSecondary, pchar->pEntParent, NULL, NULL, false, false, NULL))
					{
						if (!bNoFeedback)
						{
							ActivationFailureParams failureParams = { 0 };
							failureParams.pEnt = pentTarget;

							character_ActivationFailureFeedback(kActivationFailureReason_TargetLOSFailed, &failureParams);
						}

						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Target failed LoS check (%s %s)\n",pdef->pchName,ENTDEBUGNAME(pentTarget));
						SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetLOSFailed);
						return 0;
					}
				}
			}
			else if(pnodeTarget)
			{
				if(!character_TargetMatchesPowerTypeNode(pchar,pPowTarget))
				{
					if (!bNoFeedback)
					{
						ActivationFailureParams failureParams = { 0 };

						character_ActivationFailureFeedback(kActivationFailureReason_TargetInvalid, &failureParams);
					}
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Node target type mismatch (%s)\n",pdef->pchName);
					SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetInvalid);
					return 0;
				}
				if(pdef->eTargetVisibilityMain == kTargetVisibility_LineOfSight)
				{
					WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pnodeTarget);
					Vec3 vSource;
					entGetCombatPosDir(pchar->pEntParent, NULL, vSource, NULL);
					if(!bNodePosSet)
					{
						character_FindNearestPointForObject(pchar,NULL,pnodeTarget,vNodeTargetPos,true);
						bNodePosSet = true;
					}
					if(!combat_CheckLoS(iPartitionIdx, vSource, vNodeTargetPos, pchar->pEntParent, NULL, pEntry, false, false, NULL))
					{
						if (!bNoFeedback)
						{
							ActivationFailureParams failureParams = { 0 };
							failureParams.pEnt = pentTarget;

							character_ActivationFailureFeedback(kActivationFailureReason_TargetLOSFailed, &failureParams);
						}

						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Node target failed LoS check (%s)\n",pdef->pchName);
						SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetLOSFailed);
						return 0;
					}
				}
			}
			else if (entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER) && !pdef->bApplyObjectDeath)
			{
				//If no target, see if self matches
				if(!character_TargetMatchesPowerType(iPartitionIdx,pchar,pchar,pPowTarget))
				{
					if (!bNoFeedback)
					{
						ActivationFailureParams failureParams = { 0 };

						character_ActivationFailureFeedback(kActivationFailureReason_TargetInvalid, &failureParams);
					}

					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: No target provided, Self not valid (%s)\n",pdef->pchName);
					SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetInvalid);
					return 0;
				}
			}
		}

		//Only affect self code, cannot queue powers that affect anything but self
		if(pchar->pattrBasic->fOnlyAffectSelf > 0 &&
			(pPowTargetAffected && pPowTarget) )
		{
			if( pnodeTarget || !pentTarget || pchar != pentTarget->pChar || !pdef->bSafeForSelfOnly )
			{
				if (!bNoFeedback)
				{
					ActivationFailureParams failureParams = { 0 };

					character_ActivationFailureFeedback(kActivationFailureReason_TargetInvalid, &failureParams);
				}
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Invalid target as per OnlyAffectSelf (%s)\n",pdef->pchName);
				SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetInvalid);
				return 0;
			}
		}

		// "Safe" check, cannot target anyone with different bSafe flag, can't target foes even if
		//  both are Safe
		if(pentTarget && pentTarget->pChar && pchar != pentTarget->pChar)
		{
			if(pchar->bSafe != pentTarget->pChar->bSafe
				|| (pchar->bSafe
					&& character_TargetIsFoe(iPartitionIdx,pchar,pentTarget->pChar)))
			{
				if (!bNoFeedback)
				{
					ActivationFailureParams failureParams = { 0 };

					character_ActivationFailureFeedback(kActivationFailureReason_TargetInvalid, &failureParams);
				}

				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Invalid target as per Safe (%s %s)\n",pdef->pchName,ENTDEBUGNAME(pentTarget));
				SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetInvalid);
				return 0;
			}
		}
	}

	// Validates that the target is in the firing arc if the source can't turn to face
	if(pdef->fTargetArc
		&& pentTarget
		&& !entity_TargetInArc(pchar->pEntParent,pentTarget,NULL,RAD(pdef->fTargetArc),ppowParent->fYaw))
	{
		if (!bNoFeedback)
		{
			ActivationFailureParams failureParams = { 0 };
			failureParams.pEnt = pentTarget;

			character_ActivationFailureFeedback(kActivationFailureReason_TargetNotInArc, &failureParams);
		}
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Target not in firing arc (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetNotInArc);
		return 0;
	}

	if(pentTarget && pentTarget != pchar->pEntParent && entCheckFlag(pentTarget, ENTITYFLAG_UNTARGETABLE))
	{
		if (!bNoFeedback)
		{
			ActivationFailureParams failureParams = { 0 };
			failureParams.pEnt = pentTarget;

			character_ActivationFailureFeedback(kActivationFailureReason_TargetInvalid, &failureParams);
		}

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Target is untargetable (%s)\n",pdef->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetInvalid);
		return 0;
	}

	// If combat config does not allow for activating with a target out of range
	if(bCheckRange && g_CombatConfig.bDisableOutOfRange && character_PowerRequiresValidTarget(pchar, pdef))
	{
		if ((!pdef->bApplyObjectDeath || pchar->currentTargetRef || GET_REF(pchar->currentTargetHandle)))
		{
			F32 fDist = 0;
			F32 fTotalRange;
			S32 eRangeFailure = kActivationFailureReason_None;
			PowerAnimFX *pafx = GET_REF(pdef->hFX);

			// By default, no target is also an out of range target for players
			if(!pentTarget && !pnodeTarget && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER)
				&& !g_CombatConfig.bEnableOutOfRangeForPlayersIfNoTarget)
			{
				if (!bNoFeedback)
				{
					ActivationFailureParams failureParams = { 0 };

					character_ActivationFailureFeedback(kActivationFailureReason_TargetInvalid, &failureParams);
				}

				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: No target (%s)\n",pdef->pchName);
				SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetInvalid);
				return 0;
			}

			if(pnodeTarget && !bNodePosSet)
			{
				character_FindNearestPointForObject(pchar,NULL,pnodeTarget,vNodeTargetPos,true);
				bNodePosSet = true;
			}

			if((pentTarget || pnodeTarget) &&
				!character_TargetInPowerRangeEx(pchar, ppow, pdef, pentTarget, vNodeTargetPos, &eRangeFailure))
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Target out of range (%s)\n",pdef->pchName);
				if (!bNoFeedback)
				{
					ActivationFailureParams failureParams = { 0 };
					failureParams.pEnt = pentTarget;
					failureParams.pNode = pnodeTarget;

					character_ActivationFailureFeedback(eRangeFailure, &failureParams);
				}
				SetActivationFailureReason(peFailOut, eRangeFailure);
				return 0;
			}

			// Validate secondary target range. The range is calculated from the target to the secondary target
			if (pdef->fRangeSecondary > 0.0f)
			{
				Vec3 vecTargetSecondaryFinal;
				Vec3 vecSource;

				// Set the source vector
				if(pentTarget)
				{
					// Use target entity's position
					entGetPos(pentTarget, vecSource);
				}
				else if (pnodeTarget)
				{
					if (!bNodePosSet)
					{
						character_FindNearestPointForObject(pchar, NULL, pnodeTarget, vNodeTargetPos, true);
						bNodePosSet = true;
					}
					copyVec3(vNodeTargetPos, vecSource);
				}
				else
				{
					// Use char position
					entGetPos(pchar->pEntParent, vecSource);
				}

				// Set the target vector
				if (vecTargetSecondary && !ISZEROVEC3(vecTargetSecondary))
				{
					// Use the secondary target when available
					copyVec3(vecTargetSecondary, vecTargetSecondaryFinal);
				}
				else
				{
					// Use char position instead
					entGetPos(pchar->pEntParent, vecTargetSecondaryFinal);
				}

				fDist = entGetDistance(NULL, vecSource, NULL, vecTargetSecondary, NULL);

				// Account for lunge
				fTotalRange = pdef->fRangeSecondary + ((pafx && pafx->pLunge && pafx->pLunge->eDirection != kPowerLungeDirection_Away) ? pafx->pLunge->fRange : 0);

				if(fDist > fTotalRange)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Secondary target out of range (%s)\n",pdef->pchName);
					if (!bNoFeedback)
					{
						ActivationFailureParams failureParams = { 0 };
						copyVec3(vecTargetSecondaryFinal, failureParams.vector);

						character_ActivationFailureFeedback(kActivationFailureReason_TargetOutOfRange, &failureParams);
					}
					SetActivationFailureReason(peFailOut, kActivationFailureReason_TargetOutOfRange);
					return 0;
				}
			}
		}
		else
		{
			pentTarget = NULL;
			if (ppentTargetOut)
			{
				*ppentTargetOut = NULL;
			}
			pnodeTarget = NULL;
			if (ppnodeTargetOut)
			{
				*ppnodeTargetOut = NULL;
			}
		}
	}

	// Not allowed to activate because it's recharging
	if (bCheckRecharge)
	{
		F32 fAllowedTimeleft = entIsServer() ? POWERACT_ALLOWED_RECHARGE_TIMELEFT : 0.0f;

		if(power_GetRecharge(ppow)>fAllowedTimeleft || character_GetCooldownFromPowerDef(pchar,pdef)>fAllowedTimeleft)
		{
			if (!bNoFeedback)
			{
#ifdef GAMECLIENT
				if (!gConf.bHideCombatMessages)
				{
					char *pchTemp = NULL;
					estrStackCreate(&pchTemp);

					FormatGameMessageKey(&pchTemp,"PowersMessage.Float.Recharging",STRFMT_FLOAT("Time", power_GetRecharge(ppow)),STRFMT_END);
					notify_NotifySend(pchar->pEntParent, kNotifyType_PowerExecutionFailed, pchTemp, pdef->pchName, "");
					estrDestroy(&pchTemp);
				}
#endif
			}
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Recharging (%s)\n",pdefParent->pchName);
			SetActivationFailureReason(peFailOut, kActivationFailureReason_Recharge);
			return 0;
		}

		if(character_GetCooldownFromPowerDef(pchar,pdefParent)>fAllowedTimeleft)
		{
			//Right now the sounds and message are the same as recharging
			if (!bNoFeedback)
			{
	#ifdef GAMECLIENT
				if (!gConf.bHideCombatMessages)
				{
					char *pchTemp = NULL;
					estrStackCreate(&pchTemp);

					FormatGameMessageKey(&pchTemp,"PowersMessage.Float.Recharging",STRFMT_FLOAT("Time", character_GetCooldownFromPowerDef(pchar,pdefParent)),STRFMT_END);
					notify_NotifySend(pchar->pEntParent, kNotifyType_PowerExecutionFailed, pchTemp, pdef->pchName, "");
					estrDestroy(&pchTemp);
				}
	#endif
			}
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: In Cooldown (%s)\n",pdefParent->pchName);
			SetActivationFailureReason(peFailOut, kActivationFailureReason_Cooldown);
			return 0;
		}
	}
	else
	{
		if(power_GetRecharge(ppow) == POWERS_FOREVER)
		{

			if (!bNoFeedback)
			{
#ifdef GAMECLIENT
				if (!gConf.bHideCombatMessages)
					notify_NotifySend(pchar->pEntParent, kNotifyType_PowerExecutionFailed, TranslateMessageKeySafe("PowersMessage.Float.Recharging"), pdef->pchName, "");
#endif
			}
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Recharging (%s)\n",pdefParent->pchName);
			SetActivationFailureReason(peFailOut, kActivationFailureReason_Recharge);
			return 0;
		}
	}
	// Not allowed to activate because it's disabled, even if you aren't
	if(character_PowerBasicDisable(iPartitionIdx,pchar,ppow) > 0
		&& !powerdef_IgnoresAttrib(pdef,kAttribType_Disable))
	{
		if (!bNoFeedback)
		{
			ActivationFailureParams failureParams = { 0 };
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Disabled (%s)\n",pdefParent->pchName);
			character_ActivationFailureFeedback(kActivationFailureReason_Disabled, &failureParams);
		}
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Disabled);
		return 0;
	}

	if (entIsRolling(pchar->pEntParent))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "ActTestDynamic: FAIL: Ent is Rolling (%s)\n",pdefParent->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return 0;
	}

	return 1;
}

S32 character_ActTestPowerTargeting(int iPartitionIdx,
									Character *pchar,
									Power* ppow,
									PowerDef *pDef,
									bool bNoFeedback,
									GameAccountDataExtract *pExtract)
{
	ActivationFailureReason eFail = kActivationFailureReason_None;

	if (!character_ActTestStatic(pchar, ppow, pDef, &eFail, pExtract, bNoFeedback))
	{
		return false;
	}

	if (g_CombatConfig.bCursorModeTargetingUseStrictActivationTest)
	{
		if(!character_ActTestDynamic(iPartitionIdx, pchar, ppow, NULL, NULL, NULL, NULL, NULL, NULL, false, bNoFeedback,true))
		{
			return false;
		}

		if(!character_PayPowerCost(iPartitionIdx, pchar, ppow, 0, NULL, false, pExtract))
		{
			if(!bNoFeedback)
			{
				ActivationFailureParams failureParams = { 0 };
				failureParams.pEnt = pchar->pEntParent;
				failureParams.pPow = ppow;

				character_ActivationFailureFeedback(kActivationFailureReason_Cost, &failureParams);
			}

			return false;
		}

		if (!CombatReactivePower_CanActivatePowerDef(pchar, ppow))
		{
			if (!bNoFeedback)
			{
				ActivationFailureParams failureParams = { 0 };
				failureParams.pEnt = pchar->pEntParent;
				failureParams.pPow = ppow;
				character_ActivationFailureFeedback(kActivationFailureReason_ReactivePowerDisallow, &failureParams);
			}
			return false;
		}

		// if we are already in the act of activing this power
		if (pchar->pPowActCurrent && GET_REF(pchar->pPowActCurrent->hdef) == pDef)
		{
			return false;
		}
		else if (!pchar->pPowActCurrent && pchar->pPowActQueued && GET_REF(pchar->pPowActQueued->hdef) == pDef)
		{
			return false;
		}
	}


	return true;
}

// Returns the power the character would queue, if the character
//  attempted to queue the given power.  Returns NULL if a power
//  can not be queued.  The returned power may not be the same
//  as the input power.
//  If you do not need a specific time, use now (pmTimestamp(0))
//  If you are not predicting a specific child power, use -1
//  If for some reason the queue'd Power needs to switch targets,
//   this will try to set one of the out targets.  This should
//   only happen on the client.
Power *character_CanQueuePower(int iPartitionIdx,
							   Character *pchar,
							   Power *ppow,
							   Entity *pentTarget,
							   Vec3 vecTargetSecondary,
							   WorldInteractionNode *pnodeTarget,
							   Entity *pentTargetPicking,
							   Entity **ppentTargetOut,
							   WorldInteractionNode **ppnodeTargetOut,
							   bool *pbShouldSetHardTarget,
							   U32 uiTime,
							   S32 iPredictedIdx,
							   ActivationFailureReason *peFailOut,
							   S32 bCheckRange,
							   bool bNoFeedback,
							   bool bCheckRecharge,
							   GameAccountDataExtract *pExtract)
{
	Power *ppowParent = ppow, *ppowActual;
	PowerDef *pdef, *pdefParent;
	static PowerActivation *s_pact = NULL;
	F32 fCooldownGlobalQueueTime;

	pdef = GET_REF(ppow->hDef);

	if(!pdef)
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Unknown def\n");
		return NULL;
	}

	// Check for slotting
	if(power_SlottingRequired(ppow) && !character_PowerSlotted(pchar,ppow))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Power isn't slotted (%s)\n",pdef->pchName);
		return NULL;
	}

	// Check for AutoAttackServer
	if(pdef->bAutoAttackServer)
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Power is flagged for AutoAttackServer (%s)\n",pdef->pchName);
		return NULL;
	}

	// Check for BecomeCritter restriction
	if(pchar->bBecomeCritter && ppow->eSource!=kPowerSource_AttribMod)
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Only Powers from AttribMods are allowed in BecomeCritter state (%s)\n",pdef->pchName);
		return NULL;
	}

	// Check for a queued roll
	if(pchar->bIsWaitingToRoll)
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "%s CanQueue: FAIL: Character has a tactical roll queued (%s)\n",CHARDEBUGNAME(pchar),pdef->pchName);
		return NULL;
	}

	//Instant powers will by pass the queue entirely
	if(pdef->eType != kPowerType_Instant)
	{
		// For now, specifically Unpredicted Powers don't queue behind anything
		if(pdef->bUnpredicted
			&& (pchar->pPowActCurrent
			|| pchar->pPowActQueued
			|| pchar->pPowActOverflow))
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Unpredicted Powers can't queue behind existing activations (%s)\n",pdef->pchName);
			return NULL;
		}

		// If a power is already active or queued, check to see if queuing is disabled for this power
		if(((pchar->pPowActCurrent && !entIsServer()) || pchar->pPowActQueued || pchar->pPowActOverflow) &&
			character_IsQueuingDisabledForPower(pchar, pdef))
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: A Power is already active or queued and queuing is disabled for Power (%s)\n",pdef->pchName);
			return NULL;
		}
	}

	assert(pchar->pEntParent);

	ppowParent = ppow;
	pdefParent = pdef;

	if(!s_pact)
	{
		s_pact = poweract_Create();
	}

	// In order to determine if we can queue it, we need to know what exactly we'd be queuing.
	//  That means we have to know which power would be activated.
	// TODO(JW): Queue: Does the activation need to be cleaned?
	s_pact->iPredictedIdx = iPredictedIdx;
	s_pact->uiTimestampQueued = uiTime;
	ppowActual = character_PickActivatedPower(iPartitionIdx,pchar,ppow,pentTargetPicking?pentTargetPicking:pentTarget,ppentTargetOut,ppnodeTargetOut,pbShouldSetHardTarget,s_pact,false,true,peFailOut);
	
	if(!ppowActual || !GET_REF(ppowActual->hDef))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: No valid actual power (%s)\n",pdefParent->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return NULL;
	}

	// Set the actual power we are casting
	ppow = ppowActual;
	pdef = GET_REF(ppow->hDef);
	ppowParent = ppow->pParentPower ? ppow->pParentPower : ppow;
	pdefParent = GET_REF(ppowParent->hDef);
	ANALYSIS_ASSUME(pdefParent);
	
	if (pentTarget && ppentTargetOut)
	{
		PowerTarget *pPowTarget = GET_REF(pdef->hTargetMain);
		if (pPowTarget)
		{
			if(!character_TargetMatchesPowerType(iPartitionIdx, pchar, pentTarget->pChar, pPowTarget))
			{	// see if it can target self
				if(character_TargetMatchesPowerType(iPartitionIdx, pchar, pchar, pPowTarget))
				{
					*ppentTargetOut = pchar->pEntParent;
				}
			}
		}
	}

	if(ppentTargetOut && *ppentTargetOut)
	{
		pentTarget = *ppentTargetOut;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: Target switched to %s\n",pentTarget->debugName);
	}

	if(ppnodeTargetOut && *ppnodeTargetOut)
	{
		pnodeTarget = *ppnodeTargetOut;
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: Target switched to %s\n",wlInteractionNodeGetKey(pnodeTarget));
	}


	if (pdef->eType == kPowerType_Instant &&
		pdef->bRequiresCooldown)
	{
		S32 itCat;
		// Make sure we don't queue instant powers from the same cooldown category at the same time.
		for (itCat = 0; itCat < ea32Size(&pdef->piCategories); itCat++)
		{
			PowerCategory *pPowerCategory = g_PowerCategories.ppCategories[pdef->piCategories[itCat]];
			if (pPowerCategory->fTimeCooldown > 0.f)
			{
				// Make sure there is no other queued instant power from the same cooldown category
				FOR_EACH_IN_CONST_EARRAY_FORWARDS(pchar->ppPowerActInstant, PowerActivation, pOtherAct)
				{
					PowerDef *pOtherPowerDef = GET_REF(pOtherAct->hdef);
					if (pOtherPowerDef && pOtherPowerDef->bRequiresCooldown)
					{
						S32 itCatOther;
						for (itCatOther = 0; itCatOther < ea32Size(&pOtherPowerDef->piCategories); itCatOther++)
						{
							PowerCategory *pOtherPowerCategory = g_PowerCategories.ppCategories[pOtherPowerDef->piCategories[itCatOther]];
							if (pOtherPowerCategory == pPowerCategory)
							{
								PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, 
									"CanQueue: FAIL: Cannot activate instant powers from the same cooldown category at the same time (%s)\n", 
									pdefParent->pchName);
								SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
								return NULL;
							}
						}
					}
				}
				FOR_EACH_END
			}
		}		
	}


	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: Picked %s to be activated (%s)\n",pdef->pchName,pdefParent->pchName);

	if(pdef->pExprRequiresQueue)
	{
		combateval_ContextSetupActivate(pchar,pentTarget?pentTarget->pChar:NULL,s_pact,entIsServer()?kCombatEvalPrediction_True:kCombatEvalPrediction_None);
		if(!combateval_EvalNew(iPartitionIdx,pdef->pExprRequiresQueue,kCombatEvalContext_Activate,NULL))
		{
			if (!bNoFeedback)
			{
				ActivationFailureParams failureParams = { 0 };
				failureParams.pchStringParam = pdef->pchRequiresQueueFailMsgKey;
				character_ActivationFailureFeedback(kActivationFailureReason_RequiresQueueExpression, &failureParams);
			}

			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: ExprRequiresQueue (%s)\n",pdefParent->pchName);
			SetActivationFailureReason(peFailOut, kActivationFailureReason_RequiresQueueExpression);
			return NULL;
		}
	}

	if(!character_ActTestStatic(pchar, ppow, pdef, peFailOut, pExtract, bNoFeedback))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Activation Static Test (%s)\n",pdefParent->pchName);
		return NULL;
	}

	if(!character_ActTestDynamic( iPartitionIdx, pchar, ppow, pentTarget, vecTargetSecondary, pnodeTarget, ppentTargetOut, ppnodeTargetOut, peFailOut, bCheckRange, bNoFeedback, bCheckRecharge))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Activation Dynamic Test (%s)\n",pdefParent->pchName);
		return NULL;
	}

	// Can't use a warp power
	if(pdef->bHasWarpAttrib && !character_CanUseWarpPower(pchar, pdef))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "%s CanQueue: FAIL: Cannot use warp power (%s)\n",pdefParent->pchName);
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return NULL;
	}

	// Can't pay the upfront cost
	if(!character_PayPowerCost(iPartitionIdx,pchar,ppow,pentTarget?entGetRef(pentTarget):0,s_pact,false,pExtract))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Can't pay cost (%s)\n",pdef->pchName);
		if (!bNoFeedback)
		{
			ActivationFailureParams failureParams = { 0 };
			failureParams.pEnt = pchar->pEntParent;
			failureParams.pPow = ppow;

			character_ActivationFailureFeedback(kActivationFailureReason_Cost, &failureParams);
		}

		SetActivationFailureReason(peFailOut, kActivationFailureReason_Cost);
		return NULL;
	}

	if (!CombatReactivePower_CanActivatePowerDef(pchar, ppow))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Can't activate while in reactive power (%s)\n",pdef->pchName);
		if (!bNoFeedback)
		{
			ActivationFailureParams failureParams = { 0 };
			failureParams.pEnt = pchar->pEntParent;
			failureParams.pPow = ppow;

			character_ActivationFailureFeedback(kActivationFailureReason_ReactivePowerDisallow, &failureParams);
		}

		SetActivationFailureReason(peFailOut, kActivationFailureReason_ReactivePowerDisallow);
		return NULL;
	}

	// Not enough charges left
	//  Can't just use the power_IsExpired call here because there may be charges uses queued
	if(pdefParent->iCharges>0)
	{
		int iUsed = power_GetChargesUsed(ppowParent);
		if(pchar->pPowActCurrent && !pchar->pPowActCurrent->bActivated && ppowParent->uiID == pchar->pPowActCurrent->ref.uiID && pdefParent==GET_REF(pchar->pPowActCurrent->hdef))
		{
			iUsed++;
		}
		if(pchar->pPowActQueued && pchar->pPowActQueued->bCommit && ppowParent->uiID == pchar->pPowActQueued->ref.uiID && pdefParent==GET_REF(pchar->pPowActQueued->hdef))
		{
			iUsed++;
		}
		if(pdefParent->iCharges<=iUsed)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Used %d of %d charges (%s)\n",iUsed,pdefParent->iCharges,pdefParent->pchName);

			if (!bNoFeedback)
			{
				ActivationFailureParams failureParams = { 0 };
				failureParams.pEnt = pchar->pEntParent;
				failureParams.pPow = ppow;

				character_ActivationFailureFeedback(kActivationFailureReason_NoChargesRemaining, &failureParams);
			}
			SetActivationFailureReason(peFailOut, kActivationFailureReason_NoChargesRemaining);			
			return NULL;
		}
	}

	fCooldownGlobalQueueTime = g_CombatConfig.fCooldownGlobalQueueTime;

	// If it's a global cooldown power check the global cooldown queue time
	if (!entIsServer() && !pdef->bCooldownGlobalNotChecked && fCooldownGlobalQueueTime)
	{
		if (pchar->fCooldownGlobalTimer > fCooldownGlobalQueueTime)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "CanQueue: FAIL: Can't queue global cooldown power yet (%s)\n",pdefParent->pchName);
			SetActivationFailureReason(peFailOut, kActivationFailureReason_Cooldown);
			return NULL;
		}
	}

	// Make sure the current activation allows it
	if(pdef->eType != kPowerType_Instant
		&& pchar->pPowActCurrent
		&& !CharacterActivationAllowsQueue(pchar,pchar->pPowActCurrent,ppow,pdef,ppowParent,peFailOut))
	{
		SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
		return NULL;
	}

	if(entIsServer())
	{
		if(!entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
		{
			if(pchar->pPowActQueued	&& pchar->pPowActQueued->bCommit)
			{
				// AI is committed, not going to allow anything else to be queued
				return NULL;
			}
		}
		else
		{
			// On the server for players, we also have to make sure the queued activation allows it
			//  if the queued activation is committed
			if(pdef->eType != kPowerType_Instant
				&& pchar->pPowActQueued
				&& pchar->pPowActQueued->bCommit
				&& !CharacterActivationAllowsQueue(pchar,pchar->pPowActQueued,ppow,pdef,ppowParent,peFailOut))
			{
				SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
				return NULL;
			}
		}
	}
	else
	{
		// On the client, we also have to make sure the queued activation allows it if the
		//  queued activation is committed
		if(pdef->eType != kPowerType_Instant
			&& pchar->pPowActQueued
			&& pchar->pPowActQueued->bCommit
			&& !CharacterActivationAllowsQueue(pchar,pchar->pPowActQueued,ppow,pdef,ppowParent,peFailOut))
		{
			SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
			return NULL;
		}
	}

	// If Cooldown on Toggles happens upon deactivation, and this Toggle requires Cooldown and might shut
	//  off another Toggle, we need to check if the Toggles it will shut off will trigger its Cooldowns.
	if(g_CombatConfig.bToggleCooldownOnDeactivation && pdef->bRequiresCooldown && pdef->bToggleExclusive)
	{
		int i,s,t;
		int *piExclusive = NULL;
		int *piCooldown = NULL;
		// Find the exclusive and cooldown categories
		for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
		{
			if(g_PowerCategories.ppCategories[pdef->piCategories[i]]->bToggleExclusive)
				eaiPush(&piExclusive,pdef->piCategories[i]);
			if(g_PowerCategories.ppCategories[pdef->piCategories[i]]->fTimeCooldown > 0)
				eaiPush(&piCooldown,pdef->piCategories[i]);
		}
		s = eaiSize(&piExclusive);
		t = eaiSize(&piCooldown);

		for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
		{
			PowerDef *pdefOld = GET_REF(pchar->ppPowerActToggle[i]->ref.hdef);
			if(pdefOld && pdefOld->bToggleExclusive && pdefOld->bRequiresCooldown)
			{
				int j;
				for(j=0; j<s; j++)
				{
					if(-1!=eaiFind(&pdefOld->piCategories,piExclusive[j]))
						break; // Found a matching exclusive category
				}
				if(j<s)
				{

					for(j=0; j<t; j++)
					{
						if(-1!=eaiFind(&pdefOld->piCategories,piCooldown[j]))
							break; // Found a matching cooldown category
					}
					if(j<t)
						break;
				}
			}
		}
		eaiDestroy(&piExclusive);
		eaiDestroy(&piCooldown);

		if(i>=0)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "AllowQueue: FAIL: Activation %d requires a shared cooldown (%s)\n",pchar->ppPowerActToggle[i]->uchID,pdef->pchName);
			SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
			return NULL;
		}
	}

	if(ppow->pSourceItem)
	{
		Entity *pEnt = pchar->pEntParent;

		if(pEnt->pInventoryV2 && eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest))
		{
			int i, s;

			for(i=0;i<eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest);i++)
			{
				InventoryBag* pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), pEnt->pInventoryV2->ppSlotSwitchRequest[i]->eBagID, pExtract));
				if (pBag)
				{
					for(s=0;s<eaSize(&pBag->ppIndexedInventorySlots);s++)
					{
						if(pBag->ppIndexedInventorySlots[s]->pItem == ppow->pSourceItem)
						{
							SetActivationFailureReason(peFailOut, kActivationFailureReason_Other);
							return NULL;
						}
					}
				}
			}
		}
	}

	return ppow;
}


// Checks the power's QueueRequires expression.
bool character_CheckPowerQueueRequires(int iPartitionIdx,
										Character *pchar,
										Power *ppow,
										Entity *pentTarget,
										Vec3 vecTargetSecondary,
										WorldInteractionNode *pnodeTarget,
										Entity *pentTargetPicking,
										ActivationFailureReason *peFailOut,
										bool bNoFeedback,
										GameAccountDataExtract *pExtract)
{
	Power *ppowActual;
	PowerDef *pdef;
	static PowerActivation *s_pact = NULL;

	pdef = GET_REF(ppow->hDef);

	if(!pdef)
	{
		return false;
	}

	ppowActual = character_PickActivatedPower(iPartitionIdx,pchar,ppow,pentTargetPicking?pentTargetPicking:pentTarget,NULL,NULL,false,s_pact,false,true,peFailOut);

	if(!ppowActual || !GET_REF(ppowActual->hDef))
	{
		return false;
	}

	// This is what we'd actually be trying to do
	ppow = ppowActual;
	pdef = GET_REF(ppow->hDef);

	if(pdef->pExprRequiresQueue)
	{
		combateval_ContextSetupActivate(pchar,pentTarget?pentTarget->pChar:NULL,s_pact,entIsServer()?kCombatEvalPrediction_True:kCombatEvalPrediction_None);
		if(!combateval_EvalNew(iPartitionIdx,pdef->pExprRequiresQueue,kCombatEvalContext_Activate,NULL))
		{
			return false;
		}
	}

	return true;
}

void character_RefreshTactical(SA_PARAM_NN_VALID Character *pchar)
{
	character_Wake(pchar);

	if(	entIsRolling(pchar->pEntParent) ||
		(g_CombatConfig.tactical.bAimCancelsPowersBeforeHitFrame && entIsCrouching(pchar->pEntParent)))
	{
		character_ActAllCancel(entGetPartitionIdx(pchar->pEntParent),pchar,true);
	}

#if GAMESERVER || GAMECLIENT
	if (g_CombatConfig.pTimer)
	{
		bool bPlaceInCombat = false;
		F32 fCombatTimer = 0.f;

		if (g_CombatConfig.pTimer->fTimerTacticalRoll > 0.f && 
			entIsRolling(pchar->pEntParent))
		{
			bPlaceInCombat = true;
			MAX1(fCombatTimer, g_CombatConfig.pTimer->fTimerTacticalRoll);
		}

		if (g_CombatConfig.pTimer->fTimerTacticalAim > 0.f &&
			entIsAiming(pchar->pEntParent))
		{
			bPlaceInCombat = true;
			MAX1(fCombatTimer, g_CombatConfig.pTimer->fTimerTacticalAim);
		}

		if (bPlaceInCombat)
		{
			character_SetCombatVisualsExitTime(pchar, fCombatTimer);
		}
	}
#endif
}

// assumes that a roll was just performed and deducts the given attrib costs if defined in the combat config.
void character_PayTacticalRollCost(SA_PARAM_NN_VALID Character *pChar)
{
	TacticalRollConfig *pTacticalRoll = &g_CombatConfig.tactical.roll;
	if(pTacticalRoll->eRollCostAttrib && IS_NORMAL_ATTRIB(pTacticalRoll->eRollCostAttrib))
	{
		F32 fCost = pTacticalRoll->fRollCostAttribCost;
		F32 *pfCur = F32PTR_OF_ATTRIB(pChar->pattrBasic,pTacticalRoll->eRollCostAttrib);

		if (pTacticalRoll->eRollCostAttribMax && IS_NORMAL_ATTRIB(pTacticalRoll->eRollCostAttribMax))
		{
			fCost *= *F32PTR_OF_ATTRIB(pChar->pattrBasic, pTacticalRoll->eRollCostAttribMax);
		}

		*pfCur -= fCost;
		if (*pfCur < 0.f)
			*pfCur = 0.f;
		character_DirtyAttribs(pChar);
	}
}


// General Periodics

// Cleans up the animation state related to periodic Powers.  Exposed independently
//  from the actual deactivation so that it can be called early in case we know ahead
//  of time that it's going to be stopping.
void character_DeactivatePeriodicAnimFX(int iPartitionIdx,
										Character *pchar,
										PowerActivation *pact,
										U32 uiTimeAnim)
{
	PowerDef *pdef = GET_REF(pact->hdef);

	if(pdef)
	{
		PowerAnimFX *pafx = GET_REF(pdef->hFX);

		if(pafx)
		{
			U32 uiID = pact && pact->uchID ? pact->uchID : powerref_AnimFXID(&pact->ref);

			// Periodic non-maintained keep their stance on while they're active
			if(pdef->eType!=kPowerType_Maintained)
			{
				character_ExitStance(pchar,pafx,uiID,uiTimeAnim);
			}

			if(pdef->eType!=kPowerType_Passive)
			{
				character_MoveFaceStop(pchar,pact,uiTimeAnim);
			}

			// Deactivation bits/fx
			character_AnimFXActivateOff(iPartitionIdx,pchar,pact,uiTimeAnim);
			character_AnimFXDeactivate(iPartitionIdx,pchar,pact,uiTimeAnim);
		}
	}
}

// Checks if the Character's PowerActivation should automatically reapply because its dependencies changed
void character_ActCheckAutoReapply(Character *pchar,
									PowerActivation *pact,
									CharacterAttribs *pOldAttribs,
									CharacterAttribs *pNewAttribs)
{
	PowerDef *pdef = GET_REF(pact->hdef);
	if(pdef && pdef->bAutoReapply)
	{
		int i;
		for(i=eaiSize(&pdef->piAttribDepend)-1; i>=0; i--)
		{
			F32 *pfOld = F32PTR_OF_ATTRIB(pOldAttribs,pdef->piAttribDepend[i]);
			F32 *pfNew = F32PTR_OF_ATTRIB(pNewAttribs,pdef->piAttribDepend[i]);
			if(*pfOld!=*pfNew)
			{
				pact->fTimerActivate = 0;
				character_Wake(pchar);
				break;
			}
		}
	}
}

// Handles the misc cleanup when we turn off a periodic power.
//  Optionally will clean the mods from the power off the character.
static void CharacterDeactivatePeriodic(int iPartitionIdx,
										SA_PARAM_NN_VALID Character *pchar,
										SA_PARAM_NN_VALID PowerActivation *pact,
										U32 uiTimeAnim)
{
	PowerDef *pdef = GET_REF(pact->hdef);

	if(pdef)
	{
		character_DeactivatePeriodicAnimFX(iPartitionIdx,pchar,pact,uiTimeAnim);

		if(pdef->bDeactivationDisablesMods)
		{
			CharacterActCancelMods(iPartitionIdx,pchar,pact,true);
		}
		else if(!pdef->bDeactivationLeavesMods)
		{
			CharacterActCancelMods(iPartitionIdx,pchar,pact,false);
		}
	}
}




// Passives

// Sets the state of the passive power (doesn't check if such a setting is allowed).
//  Returns the index of the PowerActivation.
S32 character_ActivatePassive(int iPartitionIdx,
							  Character *pchar,
							  Power *ppow)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int i = poweract_FindPowerInArray(&pchar->ppPowerActPassive,ppow);
	if(i<0)
	{
		PowerActivation *pact = poweract_Create();
		U32 uiTime = pmTimestamp(0);

		poweract_SetPower(pact,ppow);
		pact->uiPeriod = 0;
		pact->uiIDServer = poweract_NextIDServer();
		i = eaPush(&pchar->ppPowerActPassive,pact);

		character_EnterStance(iPartitionIdx,pchar,ppow,pact,false,uiTime);
	}

	// Ensure it's flagged as active
	power_SetActive(ppow,true);

	return i;
#endif
}

// Sets the state of the passive power (doesn't check if such a setting is allowed)
void character_DeactivatePassive(int iPartitionIdx,
								 Character *pchar,
								 PowerActivation *pact)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
	CharacterDeactivatePeriodic(iPartitionIdx,pchar,pact,pmTimestamp(0));
	eaFindAndRemoveFast(&pchar->ppPowerActPassive,pact);
	poweract_Destroy(pact);
	// Ensure it's flagged as inactive
	if(ppow) power_SetActive(ppow,false);
#endif
}

// Sets all the character's passive powers to their allowed active state
void character_RefreshPassives(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowers[i];
		PowerDef *pdef = verify(ppow) ? GET_REF(ppow->hDef) : NULL;

		if(pdef && pdef->eType==kPowerType_Passive)
		{
			// Check for slotting (passives can't be in combos, so just checking the base is safe)
			int bActive = character_CanActivatePowerDef(pchar,pdef,false,false,NULL,pExtract,NULL)
				&& (!pdef->bSlottingRequired || character_PowerIDSlotted(pchar,ppow->uiID));

			// Check for BecomeCritter restriction
			if(bActive && pchar->bBecomeCritter && ppow->eSource!=kPowerSource_AttribMod)
			{
				bActive = false;
			}

			if(bActive)
			{
				character_ActivatePassive(iPartitionIdx,pchar,ppow);
			}
			else
			{
				int j = poweract_FindPowerInArray(&pchar->ppPowerActPassive,ppow);
				if(j>=0) character_DeactivatePassive(iPartitionIdx,pchar,pchar->ppPowerActPassive[j]);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

// Activate all the character's inactive passive powers (if allowed to be active)
void character_ActivatePassives(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	int i;
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowers[i];
		PowerDef *pdef = verify(ppow) ? GET_REF(ppow->hDef) : NULL;

		// Check for BecomeCritter restriction
		// Check for slotting (passives can't be in combos, so just checking the base is safe)
		if(pdef
			&& pdef->eType==kPowerType_Passive
			&& !(pchar->bBecomeCritter && ppow->eSource!=kPowerSource_AttribMod)
			&& character_CanActivatePowerDef(pchar,pdef,false,false,NULL,pExtract,NULL)
			&& (!pdef->bSlottingRequired || character_PowerIDSlotted(pchar,ppow->uiID)))
		{
			character_ActivatePassive(iPartitionIdx,pchar,ppow);
		}
	}
}

// Deactivates all the character's active passive powers (even if they are allowed to be active)
void character_DeactivatePassives(int iPartitionIdx,Character *pchar)
{
	int i;
	for(i=eaSize(&pchar->ppPowerActPassive)-1; i>=0; i--)
	{
		character_DeactivatePassive(iPartitionIdx,pchar,pchar->ppPowerActPassive[i]);
	}
}


// Toggles

// Puts a toggle Power Activation into the active toggles earray.  Will deactivate
//  any toggles already in the earray that are mutually exclusive with the new
//  Activation.

void character_ActivateToggle(int iPartitionIdx,
							  Character *pchar,
							  PowerActivation *pact,
							  U32 uiTimeAnim)
{
	PowerDef *pdef = GET_REF(pact->ref.hdef);

	// Deal with mutually exclusive toggles
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
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,uiTimeAnim,true);
				}
			}
		}
		eaiDestroy(&piExclusive);
	}


	// The actual activation is actually very simple
	eaPush(&pchar->ppPowerActToggle,pact);
}


// Deactivates a toggle Power Activation, removes it from the toggle earray and
//  frees it.  Passing in the actual Power is optional.
void character_DeactivateToggle(int iPartitionIdx,
								Character *pchar,
								PowerActivation *pact,
								Power *ppow,
								U32 uiTimeAnim,
								int bRecharge)
{
	U32 uiID = pact->uchID;
	S32 bHitTargets = pact->bHitTargets;

	PERFINFO_AUTO_START_FUNC();

	if(!ppow)
		ppow = character_FindPowerByRefComplete(pchar,&pact->ref);

	// Do normal deactivation stuff
	CharacterDeactivatePeriodic(iPartitionIdx,pchar,pact,uiTimeAnim);

	// Clean up finished if it matches
	if(pchar->pPowActFinished==pact)
	{
		character_ActFinishedDestroy(pchar);
	}
	eaFindAndRemoveFast(&pchar->ppPowerActToggle,pact);
	poweract_Destroy(pact);

#ifdef GAMESERVER
	ClientCmd_Powers_DeactivateTogglePower(pchar->pEntParent,uiID,true);
#endif

	if(ppow)
	{
		if (g_CombatConfig.pPowerActivationImmunities)
		{
			PowerDef *pDef = GET_REF(ppow->hDef);
			if (pDef && pDef->bActivationImmunity)
			{
				
				pchar->bPowerActivationImmunity = false;
				character_AnimFxPowerActivationImmunityCancel(iPartitionIdx, pchar, pact);
			}
		}


		// Mark it inactive
		power_SetActive(ppow,false);

		// Set its recharge
		if(bRecharge)
		{
			power_SetRechargeDefault(iPartitionIdx,pchar,ppow,!bHitTargets);
			if(g_CombatConfig.bToggleCooldownOnDeactivation)
				power_SetCooldownDefault(iPartitionIdx,pchar,ppow);
		}
	}

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate: Toggle done (%s)\n",POWERNAME(ppow));

	PERFINFO_AUTO_STOP();
}

// Sets all the character's active-flagged toggles to their allowed
//  active state.  Will activate allowed toggles that are not in the
//  toggle list, and will deactivate disallowed toggles.
// TODO(JW): Toggles: Activate toggles marked active and allowed but not in the toggle list
void character_RefreshToggles(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int i;
	U32 uiTime = pmTimestamp(0);

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowers[i];
		PowerDef *pdef = verify(ppow) ? GET_REF(ppow->hDef) : NULL;

		if(verify(pdef)
			&& pdef->eType==kPowerType_Toggle
			&& !!ppow->bActive)
		{
			int j = poweract_FindPowerInArray(&pchar->ppPowerActToggle,ppow);

			if(character_CanActivatePowerDef(pchar,pdef,false,true,NULL,pExtract,NULL))
			{
				power_SetActive(ppow,true);
			}
			else
			{
				if(j>=0)
				{
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[j],ppow,uiTime,true);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
#endif
}

// Deactivates all the character's active toggle powers, cleans the toggle list
void character_DeactivateToggles(int iPartitionIdx,
								 Character *pchar,
								 U32 uiTimeAnim,
								 int bRecharge,
								 int bDeadCheck)
{
	int i;
	for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
	{
		PowerActivation* pact = pchar->ppPowerActToggle[i];
		Power* ppow = character_FindPowerByRef(pchar,&pact->ref);
		PowerDef* ppowDef = SAFE_GET_REF(ppow, hDef);
		if (!bDeadCheck || !ppowDef || !(ppowDef->eActivateRules & kPowerActivateRules_SourceDead))
		{
			character_DeactivateToggle(iPartitionIdx,pchar,pact,ppow,uiTimeAnim,bRecharge);
		}
	}
	if (!eaSize(&pchar->ppPowerActToggle))
	{
		eaDestroy(&pchar->ppPowerActToggle);
	}
}

// Returns true if the PowerActivation is for a Toggle
S32 poweract_IsToggle(PowerActivation *pact)
{
	PowerDef *pdef = GET_REF(pact->hdef);
	return (pdef && pdef->eType==kPowerType_Toggle);
}

int poweract_DoesRootPlayer(PowerActivation *pact)
{
	if (pact)
	{
		PowerDef *pDef = GET_REF(pact->hdef);
		if (pDef)
		{
			PowerAnimFX *pPowerArt = GET_REF(pDef->hFX);
			if (pPowerArt)
			{
				return (pPowerArt->fSpeedPenaltyDuringActivate == 1.f ||
					pPowerArt->fSpeedPenaltyDuringCharge == 1.f ||
					pPowerArt->fSpeedPenaltyDuringCharge == 1.f);
			}
		}
	}

	return false;
}

// Maintained

// Performs the deactivation process for the Anim/FX of a maintained Power Activation,
//  but does not actually deactivate it.  Used when we know a Maintained will be stopping
//  before we actually should stop it, so the Anim/FX predict properly.
void character_DeactivateMaintainedAnimFX(	int iPartitionIdx,
											SA_PARAM_NN_VALID Character *pchar,
											SA_PARAM_NN_VALID PowerActivation *pact,
											SA_PARAM_NN_VALID PowerDef *pPowerDef)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	U32 uiAnimTime;
	S32 bKeepAnimsAndPenalties = false;
	
	if (gConf.bNewAnimationSystem) {
		uiAnimTime = pact->uiTimestampCurrented;
	} else {
		Power *ppow = character_ActGetPower(pchar, pact);
		F32 fSpeed = ppow ? character_GetSpeedPeriod(iPartitionIdx, pchar, ppow) : 1;
		F32 fSecondsOffset = pact->fTimeActivating + (pact->fTimerActivate / fSpeed);
		uiAnimTime = pmTimestampFrom(pact->uiTimestampActivate, fSecondsOffset);
	}

	bKeepAnimsAndPenalties = (pchar->eChargeMode == kChargeMode_CurrentMaintain && (pPowerDef->fTimePostMaintain > 0.f));
		
	character_AnimFXActivateOffEx(iPartitionIdx, pchar, pact, uiAnimTime, bKeepAnimsAndPenalties);

	character_AnimFXDeactivate(iPartitionIdx, pchar, pact, uiAnimTime);
#endif
}

// Deactivates a Maintained Power Activation.  Does not destroy the Activation.
void character_DeactivateMaintained(int iPartitionIdx,
									Character *pchar,
									Power *ppow,
									PowerActivation *pact,
									PowerAnimFX *pafx,
									U32 uiTimeAnim)
{
	// Do normal deactivation stuff
	CharacterDeactivatePeriodic(iPartitionIdx,pchar,pact,uiTimeAnim);

	// Set its recharge
	power_SetRechargeDefault(iPartitionIdx,pchar,ppow,!pact->bHitTargets);
	power_SetCooldownDefault(iPartitionIdx,pchar,ppow);

	// Mark it inactive
	power_SetActive(ppow,false);

#if GAMESERVER || GAMECLIENT
	if(pafx)
	{
		if(pafx->fSpeedPenaltyDuringActivate==1)
			character_PowerActivationRootStop(pchar, pact->uchID, kPowerAnimFXType_ActivateSticky, uiTimeAnim);
		else if(TRUE_THEN_RESET(pact->bSpeedPenaltyIsSet))
			mrSurfaceSpeedPenaltyStop(pchar->pEntParent->mm.mrSurface, pact->uchID, uiTimeAnim);

		if(g_CombatConfig.tactical.bTacticalDisableDuringPowers)
			mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical, pact->uchID, uiTimeAnim);
	}
#endif

#ifdef GAMESERVER
	// Notify the client that the maintain was deactivated
	ClientCmd_Powers_DeactivateMaintainedPower(pchar->pEntParent, pact->uchID);
#endif

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate: Maintained done (%s)\n",POWERNAME(ppow));
}

// Stops the maintained power, putting it into a post-deactivate phase if the power has a fTimePostMaintain
// otherwise it just deactivates the maintain
void character_ActMoveToPostMaintain(	int iPartitionIdx,
										Character *pChar,
										Power *pPow,
										PowerDef *pPowerDef,
										PowerAnimFX *pafx,
										PowerActivation *pAct,
										U32 uiTimeAnim)
{
	if (pPowerDef->fTimePostMaintain > 0.f && pAct->eActivationStage == kPowerActivationStage_Activate)
	{
		// deactivate any mods 
		if(pPowerDef->bDeactivationDisablesMods)
		{
			CharacterActCancelMods(iPartitionIdx, pChar, pAct, true);
		}
		else if(!pPowerDef->bDeactivationLeavesMods)
		{
			CharacterActCancelMods(iPartitionIdx, pChar, pAct, false);
		}
	
		pAct->eActivationStage = kPowerActivationStage_PostMaintain;
		pAct->fStageTimer = pPowerDef->fTimePostMaintain; 

		// make sure we're going to wake up in time
		character_SetSleep(pChar, pPowerDef->fTimePostMaintain);

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pChar->pEntParent, "Current %s: Maintained stopping, moving to post-maintain\n",pPowerDef->pchName);
	}
	else
	{
		character_DeactivateMaintained(iPartitionIdx, pChar, pPow, pAct, pafx, uiTimeAnim);
	}
}

// Returns true if the PowerDef is allowed to activate in the given WorldRegionType
S32 powerdef_RegionAllowsActivate(PowerDef *pdef, WorldRegionType eType)
{
	RegionRules *pRegionRules = getRegionRulesFromRegionType(eType);
	int i;

	if(pRegionRules)
	{
		if(ea32Size(&pRegionRules->piCategoryExclude) > 0)
		{
			//Check to see if any of the categories match up
			for(i=0;i<ea32Size(&pRegionRules->piCategoryExclude);i++)
			{
				if(ea32Find(&pdef->piCategories,pRegionRules->piCategoryExclude[i]) > -1)
				{
					return false;
				}
			}
		}

		if(ea32Size(&pRegionRules->piCategoryRequire) > 0)
		{
			bool bFoundRequired = false;

			for(i=0;i<ea32Size(&pRegionRules->piCategoryRequire);i++)
			{
				if(ea32Find(&pdef->piCategories,pRegionRules->piCategoryRequire[i]) > -1)
				{
					bFoundRequired = true;
					break;
				}
			}

			if(!bFoundRequired)
			{
				return false;
			}
		}
	}
	return true;
}

static __forceinline S32 combat_CapsuleCast(int iPartitionIdx, const Vec3 vecSource, const Vec3 vecTarget, U32 uiFilterBits, WorldCollCollideResults *wcResults)
{
	Vec3 vecSourceCapsule, vecTargetCapsule;
	// TODO(JW): Lunge: Capsule size based on entity
	F32 fRadius = 1.5;
	F32 fHeight = 1.5;
	F32 fOffset = 0.0f;

	// Radius and height are scaled to 90%, and the offset is shifted half that
	fRadius *= 0.9;
	fHeight *= 0.9;
	fOffset = (1.5*0.9)/2.0f;

	copyVec3(vecSource,vecSourceCapsule);
	vecSourceCapsule[1] += fOffset;

	copyVec3(vecTarget, vecTargetCapsule);
	vecTargetCapsule[1] += fOffset;

	return worldCollideCapsuleEx(iPartitionIdx, vecSourceCapsule, vecTargetCapsule, uiFilterBits, fHeight, fRadius, wcResults);
}

__forceinline static S32 combat_ValidateHitEntity(Entity *pentHit, Entity *pentSource, Entity *pentTarget, WorldInteractionEntry *pnodeTarget)
{
	// Hit an object, i.e. a moving platform/entity-with-collision-geometry
	// Check to see if target is the same
	if(pentHit) // Probably could assert, as it's kinematic, it must exist.
	{
		if(pentHit==pentSource || pentHit==pentTarget)
		{
			return false;
		}
		else if(pnodeTarget && IS_HANDLE_ACTIVE(pentHit->hCreatorNode))
		{
			if(GET_REF(pnodeTarget->hInteractionNode))
				return false;
		}
	}
	return true;
}

// Validates line of sight checks between a source entity and target entity
S32 combat_ValidateHit(Entity *pentSource, Entity *pentTarget, WorldCollCollideResults *wcResults)
{
#if !GAMESERVER && !GAMECLIENT
	return false;
#else
	Entity *pentHit = NULL;
	if(mmGetUserPointerFromWCO(wcResults->wco, &pentHit))
	{
		if(!combat_ValidateHitEntity(pentHit,pentSource,pentTarget,NULL))
		{
			return false;
		}
	}
	return true;
#endif
}

// Validates line of sight checks between a source entity and target entity or node
S32 combat_ValidateHitEx(int iPartitionIdx, Entity *pentSource, Entity *pentTarget, WorldCollCollideResults *wcResults, WorldInteractionEntry *pnodeTarget)
{
#if !GAMESERVER && !GAMECLIENT
	return false;
#else
	Entity *pentHit = NULL;
	if(mmGetUserPointerFromWCO(wcResults->wco, &pentHit))
	{
		if(!combat_ValidateHitEntity(pentHit,pentSource,pentTarget,pnodeTarget))
		{
			return false;
		}
	}
	else if(pnodeTarget)
	{
		if (wlInteractionCheckCollObject(iPartitionIdx,pnodeTarget,wcResults->wco))
		{
			return false;
		}
	}

	return true;
#endif
}

// Checks the line of sight from the source to the target.  Can cast either
//  a ray or a capsule.  Can takes the entities involved, if any, so they don't
//  hit themselves (if they're part of the world).  If the world is hit it
//  will put the hit location in the out vector.
// If bIgnoreNoCollCameraObjects is passed as true, the ray casts ignore any object
//  marked to not collide with the camera.
S32 combat_CheckLoS(int iPartitionIdx,
					const Vec3 vecSource,
					const Vec3 vecTarget,
					Entity *pentSource,
					Entity *pentTarget,
					WorldInteractionEntry *pnodeTarget,
					S32 bCapsule,
					bool bIgnoreNoCollCameraObjects,
					Vec3 vecHitLocOut)
{
	S32 bHitWorld;
	S32 bHitReverse;
	WorldCollCollideResults wcResults;
	WorldCollCollideResults wcReverse;
	U32 filterBits = bIgnoreNoCollCameraObjects ? WC_QUERY_BITS_TARGETING : WC_QUERY_BITS_COMBAT;

	PERFINFO_AUTO_START_FUNC();

	// TODO (AM): Once we have faked two-sided hulls, this function should use a single raycast
	if(bCapsule)
	{
		bHitWorld = combat_CapsuleCast(iPartitionIdx, vecSource, vecTarget, filterBits, &wcResults);
	}
	else
	{
		bHitWorld = worldCollideRay(iPartitionIdx, vecSource, vecTarget, filterBits, &wcResults);
	}

	if(pentTarget && vecHitLocOut && !IS_HANDLE_ACTIVE(pentTarget->hCreatorNode)) //Destructible objects already know there near point
	{
		Vec3 vecDistance;
		F32 fDist;

		subVec3(vecTarget,vecSource,vecDistance);
		fDist = lengthVec3(vecDistance);

		normalVec3(vecDistance);

		fDist = entLineDistanceEx(vecSource, pentSource ? entGetPrimaryCapsuleRadius(pentSource) : 0,
								  vecDistance,fDist,pentTarget,vecHitLocOut,true);

		vecDistance[0] = vecDistance[0] * fDist;
		vecDistance[1] = vecDistance[1] * fDist;
		vecDistance[2] = vecDistance[2] * fDist;

		addVec3(vecHitLocOut,vecDistance,vecHitLocOut);
	}

	if(bHitWorld)
	{
		bHitWorld = combat_ValidateHitEx(iPartitionIdx, pentSource, pentTarget, &wcResults, pnodeTarget);

		// If it's not a UGC map, just return
		if(bHitWorld && !zmapIsUGCGeneratedMap(NULL))
		{
			// TODO(JW): LoS: Fudge factor?
			if(vecHitLocOut) copyVec3(wcResults.posWorldImpact,vecHitLocOut);
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	// Skip the reverse check unless the CombatConfig has it turned on, except for UGC maps
	if(!g_CombatConfig.bLoSCheckBackwards && !zmapIsUGCGeneratedMap(NULL))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	if(bCapsule)
	{
		bHitReverse = combat_CapsuleCast(iPartitionIdx, vecTarget, vecSource, filterBits, &wcReverse);
	}
	else
	{
		bHitReverse = worldCollideRay(iPartitionIdx, vecTarget, vecSource, filterBits, &wcReverse);
	}

	if(bHitReverse)
	{
		bHitReverse = combat_ValidateHitEx(iPartitionIdx, pentSource, pentTarget, &wcReverse, pnodeTarget);

		// If it's not a UGC map, just return
		if(bHitReverse && !zmapIsUGCGeneratedMap(NULL))
		{
			// TODO(JW): LoS: Fudge factor?
			if(vecHitLocOut) copyVec3(wcReverse.posWorldImpact,vecHitLocOut);
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(zmapIsUGCGeneratedMap(NULL))
	{
		if(bHitWorld && bHitReverse)
		{
			// Had to hit on both, so wcResults has to have a position, and forward cast result is probably closer
			if(vecHitLocOut) copyVec3(wcResults.posWorldImpact, vecHitLocOut);
			PERFINFO_AUTO_STOP();
			return false;
		}

	}

	PERFINFO_AUTO_STOP();
	return true;
}

// Finds the real target entity or location, given the Character and Activation
S32 character_ActFindTarget(int iPartitionIdx,
							Character *pchar,
							PowerActivation *pact,
							const Vec3 vecSourcePos,
							const Vec3 vecSourceDir,
							Entity **ppentTargetOut,
							Vec3 vecTargetOut)
{
	S32 r, bRange = false;
	F32 fDist = -1.0f;
	PowerDef *pdef = GET_REF(pact->hdef);
	Power *ppow = character_ActGetPower(pchar, pact);
	if(!pdef)
	{
		return false;
	}

	r = combat_FindRealTargetEx(iPartitionIdx,
								ppow,
								pdef,
								pchar->pEntParent,
								vecSourcePos,
								vecSourceDir,
								pact->erTarget,
								pact->erProximityAssistTarget,
								pact->vecTarget,
								pact->vecTargetSecondary,
								NULL,
								false,
								0, 
								ppentTargetOut,
								vecTargetOut,
								&fDist,
								&bRange);

	pact->fDistToTarget = fDist;
	pact->bRange = !!bRange;
	return r;
}




// Finds the real target entity or vector, given the power def, source and chosen target
S32 combat_FindRealTargetEx(int iPartitionIdx,
							Power* ppow,
							PowerDef *pdef,
							Entity *pentSource,
							const Vec3 vecSourcePos,
							const Vec3 vecSourceDir,
							EntityRef erTarget,
							EntityRef erProximityAssistTarget,
							const Vec3 vecTarget,
							const Vec3 vecTargetSecondary,
							WorldVolume*** pppvolTarget,
							S32 bTesting,
							U32 uiTimestampClientView,
							Entity **ppentTargetOut,
							Vec3 vecTargetOut,
							F32 *pfDistOut,
							S32 *pbRangeOut)
{
	PowerTarget *ppowertarget;
	//  TODO(JW): FindTarget: Get real range of power using enhancements etc
	F32 fRange = power_GetRange(ppow, pdef);
	F32 fRangeMin = pdef->fRangeMin;

	PERFINFO_AUTO_START_FUNC();

	ppowertarget = GET_REF(pdef->hTargetMain);

	if(pbRangeOut)
	{
		*pbRangeOut = true;
	}

	// Completely invalid call, return false
	if(!ppowertarget)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// If this actually targets self, use input entity as the real target
	if(ppowertarget->bRequireSelf)
	{
		Vec3 vecResult;
		*ppentTargetOut = pentSource;

		if (pdef &&
			pdef->fRangeSecondary > 0.0f)
		{
			if (vecTargetSecondary && !ISZEROVEC3(vecTargetSecondary))
			{
				// Use the secondary target
				copyVec3(vecTargetSecondary, vecTargetOut);
			}
			else
			{
				// Use self
				copyVec3(vecSourcePos,vecTargetOut);
			}
		}
		else if(fRange==0)
		{
			copyVec3(vecSourcePos,vecTargetOut);
		}
		else
		{
			if(	SAFE_MEMBER2(pentSource, pChar, bUseCameraTargeting) &&
				!ppowertarget->bDoNotTargetUnlessRequired &&
				!ISZEROVEC3(vecTarget))
			{
				if (erProximityAssistTarget)
				{
					Entity *pTargetProxEnt = entFromEntityRef(iPartitionIdx, erProximityAssistTarget);
					if (pTargetProxEnt)
					{
						Vec3 vTargetCombatPos, vSourceCombatPos, vPowerDirection;

						entGetCombatPosAtTime(pentSource, uiTimestampClientView, vSourceCombatPos);
						entGetCombatPosAtTime(pTargetProxEnt, uiTimestampClientView, vTargetCombatPos);
						subVec3(vTargetCombatPos, vSourceCombatPos, vPowerDirection);
						normalVec3(vPowerDirection);
						scaleAddVec3(vPowerDirection, fRange, vSourceCombatPos, vecTargetOut);
					}
					else
					{
						copyVec3(vecTarget,vecTargetOut);
					}
				}
				else
				{
					copyVec3(vecTarget,vecTargetOut);
				}

			}
			else
			{
				// we're getting our point via the source direction & position.
				// Apply any offsets from the power to get the vecTargetOut
				Vec3 vecSourceDirFinal;
				bool bUpdatedDir = false;

				copyVec3(vecSourceDir, vecSourceDirFinal);
				
				if (powerdef_ignorePitch(pdef))
				{
					vecSourceDirFinal[1] = 0.f;
					if (!vec3IsZero(vecSourceDirFinal))
					{
						normalVec3XZ(vecSourceDirFinal);
					}
					else
					{
						vecSourceDirFinal[2] = 1.f;
					}
				}

				if (ppow && ABS(ppow->fYaw) > FLT_EPSILON)
				{
					Mat3 xMat;
					orientMat3(xMat, vecSourceDirFinal);
					yawMat3(ppow->fYaw, xMat);
					copyVec3(xMat[2], vecSourceDirFinal);
				}

				// Fire in the facing direction for the range of the power
				scaleAddVec3(vecSourceDirFinal, fRange, vecSourcePos, vecTargetOut);
			}

			if(pdef->eTargetVisibilityMain == kTargetVisibility_LineOfSight)
			{
				// Check if we hit the world
				if(!combat_CheckLoS(iPartitionIdx, vecSourcePos, vecTargetOut, pentSource, NULL, NULL, false, false, vecResult))
				{
					// Use world hit location as the locational target
					copyVec3(vecResult,vecTargetOut);
				}
				else if (pdef->bSimpleProjectileMotion)
				{
					Vec3 vHorizontalTarget;
					setVec3(vHorizontalTarget, vecTargetOut[0], vecSourcePos[1], vecTargetOut[2]);

					// If we didn't hit anything, find a target using the horizontal component of the direction
					if (!nearSameF32(vecSourcePos[1], vecTargetOut[1]) &&
						!combat_CheckLoS(iPartitionIdx, vecSourcePos, vHorizontalTarget, pentSource, NULL, NULL, false, false, vecResult))
					{
						copyVec3(vecResult, vecTargetOut);
					}
					else
					{
						Vec3 vFinalTarget;
						scaleAddVec3(upvec, fRange * -0.5f, vHorizontalTarget, vFinalTarget);

						// Try to find a position on the ground (this may be slightly beyond the range of the power)
						if (!combat_CheckLoS(iPartitionIdx, vecTargetOut, vFinalTarget, pentSource, NULL, NULL, false, false, vecResult))
						{
							copyVec3(vecResult, vecTargetOut);
						}
						else
						{
							copyVec3(vFinalTarget, vecTargetOut);
						}
					}
				}
			}

		}
	}
	else
	{
		Vec3 vecEntTarget;
		F32 fDistFound;

		//This is now hooked into NW's SuperCritterPets instead of being commented out.
		if(ppowertarget->eRequire & kTargetType_PrimaryPet)
		{
			if(pentSource && pentSource->pChar)
			{
				erTarget = scp_GetSummonedPetEntRef(pentSource);
			}
			else
			{
				erTarget = 0;
			}
		}
		

		// With g_CombatConfig.bRequireValidTarget, Charged attacks that have unrestricted targeting
		// but use bUpdateChargeTargetOnDeactivate, fail finding a target
		if (!erTarget && character_PowerRequiresValidTarget(pentSource?pentSource->pChar:NULL,pdef) &&
			pdef->bUpdateChargeTargetOnDeactivate && pdef->fTimeCharge > 0.f)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		// Figure out where this is going, by default that is the erTarget or the vecTarget
		*ppentTargetOut = entFromEntityRef(iPartitionIdx,erTarget);

		if(*ppentTargetOut)
		{
			if(vecTarget && IS_HANDLE_ACTIVE((*ppentTargetOut)->hCreatorNode))
			{
				character_FindNearestPointForObject((pentSource?pentSource->pChar:NULL),
													vecSourcePos, 
													GET_REF((*ppentTargetOut)->hCreatorNode),
													vecEntTarget,true);
			}
			else
			{
				entGetCombatPosAtTime(*ppentTargetOut, uiTimestampClientView, vecEntTarget);
			}

			// if there's a valid range and the effect area is cylinder or cone, extend the vecTargetOut
			// to the max of the range
			if (fRange && (pdef->eEffectArea == kEffectArea_Cylinder ||  pdef->eEffectArea == kEffectArea_Cone))
			{
				Vec3 vDirToTarget;
				subVec3 (vecEntTarget, vecSourcePos, vDirToTarget);
				normalVec3(vDirToTarget);
				scaleAddVec3(vDirToTarget, fRange, vecSourcePos, vecTargetOut);
			}
			else
			{
				copyVec3(vecEntTarget, vecTargetOut);

			}

		}
		else
		{
			if(vecTarget)
			{
				copyVec3(vecTarget,vecTargetOut);
			}
			else
			{
				zeroVec3(vecTargetOut);
			}
		}

		// If we have an entity target
		if(*ppentTargetOut)
		{
			// LoS check
			if(pdef->eTargetVisibilityMain==kTargetVisibility_LineOfSight)
			{
				Vec3 vecResult;
				if(!combat_CheckLoS(iPartitionIdx, vecSourcePos,vecEntTarget,pentSource,*ppentTargetOut,NULL,false,false,vecResult))
				{
					if (!bTesting)
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pentSource, "FindTarget: Failed LoS test (%s)\n",pdef->pchName);

					// Don't have LoS, so clear entity target, use the world hit location as the locational target
					*ppentTargetOut = NULL;
					copyVec3(vecResult,vecTargetOut);

					if (pdef->eRequireValidTarget == kPowerRequireValidTarget_Always)
					{
						PERFINFO_AUTO_STOP();
						return false;
					}
				}
			}
		}
		else
		{
			// Check if it should just fire straight ahead as far as possible
			if((!vecTarget || ISZEROVEC3(vecTarget)) && !*ppentTargetOut && !pppvolTarget && vecSourceDir)
			{
				Vec3 vecResult;

				// Fire in the facing direction for the range of the power
				// TODO(JW): FindTarget: Get real range of power using enhancements etc
				scaleAddVec3(vecSourceDir,fRange,vecSourcePos,vecTargetOut);

				// Check if we hit the world
				if(!combat_CheckLoS(iPartitionIdx, vecSourcePos, vecTargetOut, pentSource, NULL, NULL, false, false, vecResult))
				{
					// Use world hit location as the locational target
					copyVec3(vecResult,vecTargetOut);
				}
				else if (pdef->bSimpleProjectileMotion)
				{
					Vec3 vHorizontalTarget;
					setVec3(vHorizontalTarget, vecTargetOut[0], vecSourcePos[1], vecTargetOut[2]);

					// If we didn't hit anything, find a target using the horizontal component of the direction
					if (!nearSameF32(vecSourcePos[1], vecTargetOut[1]) &&
						!combat_CheckLoS(iPartitionIdx, vecSourcePos, vHorizontalTarget, pentSource, NULL, NULL, false, false, vecResult))
					{
						copyVec3(vecResult, vecTargetOut);
					}
					else
					{
						Vec3 vFinalTarget;
						scaleAddVec3(upvec, fRange * -0.5f, vHorizontalTarget, vFinalTarget);

						// Try to find a position on the ground (this may be slightly beyond the range of the power)
						if (!combat_CheckLoS(iPartitionIdx, vecTargetOut, vFinalTarget, pentSource, NULL, NULL, false, false, vecResult))
						{
							copyVec3(vecResult, vecTargetOut);
						}
						else
						{
							copyVec3(vFinalTarget, vecTargetOut);
						}
					}
				}
			}
		}

		// Distance check
		//  Distance to the list of target volumes doesn't make much sense.  Ignore it for now.
		if(!pppvolTarget)
		{
			PowerAnimFX *pafx = GET_REF(pdef->hFX);
			F32 fTotalRange;
			// Account for lunge
			fTotalRange = fRange + ((pafx && pafx->pLunge) ? pafx->pLunge->fRange : 0);


			if(pentSource)
			{
				// vecSourcePos is the intended combat position of the source Entity, if we have a source Entity.
				//  That means it's probably several feet from Entity's actual position (since combat position is
				//  generally chest height or so), and in the case of Lunge may be very far offset spatially in
				//  general.  To properly take the source's capsule into account, we must calculate its combat position
				//  at its current position, find the difference between that and the intended combat position, and
				//  inform entGetDistance of the offset.
				Vec3 vecSourceCurrentPos, vecSourceOffset;
				entGetCombatPosDir(pentSource,NULL,vecSourceCurrentPos,NULL);
				subVec3(vecSourcePos,vecSourceCurrentPos,vecSourceOffset);
				fDistFound = entGetDistanceOffset(pentSource, vecSourcePos, vecSourceOffset, *ppentTargetOut, vecTargetOut, NULL);
			}
			else
			{
				fDistFound = entGetDistance(NULL, vecSourcePos, *ppentTargetOut, vecTargetOut, NULL);
			}

			if (pfDistOut)
			{
				(*pfDistOut) = fDistFound;
			}
			if (fDistFound > fTotalRange || fDistFound < fRangeMin)
			{
				S32 bShort = (fDistFound > fTotalRange);
				if (!bTesting)
				{
					ActivationFailureParams failureParams = { 0 };
					failureParams.pEnt = *ppentTargetOut;
					copyVec3(vecTargetOut, failureParams.vector);
					character_ActivationFailureFeedback(bShort?kActivationFailureReason_TargetOutOfRange:kActivationFailureReason_TargetOutOfRangeMin, &failureParams);
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pentSource, "FindTarget: Out of range %f (%f - %f)\n",fDistFound, fRangeMin, fTotalRange);
				}

				if(pbRangeOut)
					*pbRangeOut = false;

				if(g_CombatConfig.bDisableOutOfRange)
				{
					//We are short, and therefore should cancel the power
					PERFINFO_AUTO_STOP();
					return false;
				}
				else
				{
					// If we're short, clear entity target and update locational target
					// Fire towards the target for the range of the power
					Vec3 vecDelta;
					subVec3(vecTargetOut,vecSourcePos,vecDelta);
					normalVec3(vecDelta);
					scaleAddVec3(vecDelta,bShort?fRange:fRangeMin,vecSourcePos,vecTargetOut);
					*ppentTargetOut = NULL;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}

// Called on the client to find an entity for the node of an activation
S32 character_TargetEntFromNode(int iPartitionIdx, Character *pchar, PowerActivation *pact)
{
	Entity *currEnt;
	S32 bEntFound = false;
	WorldInteractionNode *pActNode = GET_REF(pact->hTargetObject);

	if(pActNode)
	{
		EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE | ENTITYFLAG_UNTARGETABLE | ENTITYFLAG_UNSELECTABLE, GLOBALTYPE_ENTITYCRITTER);
		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			if(currEnt)
			{
				WorldInteractionNode *pCreator = GET_REF(currEnt->hCreatorNode);

				if(pCreator == pActNode)
				{
					pact->erTarget = currEnt->myRef;
					bEntFound = true;
					break;
				}
			}
		}

		EntityIteratorRelease(iter);
	}

	return(bEntFound);
}

// Callback function used by AI to watch Power Activation process
void combat_SetPowerExecutedCallback(entity_NotifyPowerExecutedCallback callback)
{
	s_funcNotifyExecutedCallback = callback;
}

void combat_SetPowerRechargedCallback(entity_NotifyPowerRechargedCallback callback)
{
	g_funcNotifyPowerRechargedCallback = callback;
}


#include "AutoGen/PowerActivation_h_ast.c"
