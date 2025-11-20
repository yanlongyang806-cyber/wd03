#include "aiMessages.h"

#include "aiAvoid.h"
#include "aiAggro.h"
#include "aiConfig.h"
#include "aiCivilian.h"
#include "aiDebug.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiTeam.h"
#include "aiPowers.h"

#include "Character_target.h"
#include "CharacterAttribs.h"
#include "cmdparse.h"
#include "CommandQueue.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "EString.h"
#include "gslMapState.h"
#include "MemoryPool.h"
#include "PowerActivation.h"
#include "StringCache.h"
#include "StateMachine.h"

#include "oldencounter_common.h"

#include "AILib_autogen_QueuedFuncs.h"
#include "aiEnums_h_ast.h"
#include "aiMovement_h_ast.h"
#include "CharacterAttribs_h_ast.h"

MP_DEFINE(AIMessageEntry);
AIMessageEntry* aiMessageEntryCreate(void)
{
	MP_CREATE(AIMessageEntry, 32);
	return MP_ALLOC(AIMessageEntry);
}

void aiMessageEntryDestroy(AIMessageEntry* entry)
{
	MP_FREE(AIMessageEntry, entry);
}

MP_DEFINE(AIMessage);

static __forceinline void aiMessagePrune(int partitionIdx, AIMessage* msg, U32 age)
{
	if(eaSize(&msg->entries))
	{
		if((!msg->timeLastPruneCheck || ABS_TIME_PASSED_PARTITION(partitionIdx, msg->timeLastPruneCheck, 20)) &&
			ABS_TIME_PASSED_PARTITION(partitionIdx, msg->entries[0]->time, AIMESSAGE_MEMORY_TIME))
		{
			int end = 1;
			int size = eaSize(&msg->entries);

			msg->timeLastPruneCheck = ABS_TIME_PARTITION(partitionIdx);

			aiMessageEntryDestroy(msg->entries[0]);
			while(end<size && ABS_TIME_PASSED_PARTITION(partitionIdx, msg->entries[end]->time, AIMESSAGE_MEMORY_TIME))
			{
				aiMessageEntryDestroy(msg->entries[end]);
				end++;
			}

			eaRemoveRange(&msg->entries, 0, end);
		}
	}
}

static __forceinline void aiMessageAddEntry(int partitionIdx, AIMessage* msg, AIMessageEntry* entry)
{
	// Update total counts
	msg->totalCount += 1;
	msg->totalValue += entry->value;
	msg->timeLastReceived = ABS_TIME_PARTITION(partitionIdx);

	eaPush(&msg->entries, entry);
}

void aiMessageReceive(int partitionIdx, FSMContext* fsmContext, const char* tag, Entity* source, F32 value, ACMD_EXPR_ENTARRAY_IN entArrayData)
{
	AIMessage* msg;
	AIMessageEntry* entry;

	fsmContext->messageRecvd = true;

	if(!fsmContext->messages)
		return;

	if(!tag || !tag[0])
		return;

	//fsmContext->messages = stashTableCreateWithStringKeys(4, StashDefault);

	if(!stashFindPointer(fsmContext->messages, tag, &msg))
	{
		MP_CREATE(AIMessage, 16);
		msg = MP_ALLOC(AIMessage);
		msg->tag = allocAddString(tag);
		msg->totalCount = 0;
		msg->totalValue = 0;
		stashAddPointer(fsmContext->messages, msg->tag, msg, false);
	}

	entry = aiMessageEntryCreate();
	entry->time = ABS_TIME_PARTITION(partitionIdx)+1;
	entry->value = value;

	if(entArrayData)
	{
		int i;
		eaiClearFast(&msg->refArray);
		for(i = eaSize(entArrayData)-1; i >= 0; i--)
			eaiPush(&msg->refArray, entGetRef((*entArrayData)[i]));
	}
	else if(source)
	{
		eaiSetSize(&msg->refArray, 1);
		msg->refArray[0] = entGetRef(source);
	}

	// Clear out old messages (might as well do it before)
	aiMessagePrune(partitionIdx, msg, AIMESSAGE_MEMORY_TIME);

	aiMessageAddEntry(partitionIdx, msg, entry);	
}

// Send a message to a target FSM, which always receives the message
void aiMessageSendAbstract(int partitionIdx, FSMContext* fsmContext, const char* tag, Entity* source, F32 value, ACMD_EXPR_ENTARRAY_IN entArrayData)
{
	aiMessageReceive(partitionIdx, fsmContext, tag, source, value, entArrayData);
}

// Send a message from one ent to another.  Creates an animation, requires the ents to be close to each other
void aiMessageSendEntToEnt(Entity* e, AIVarsBase* aib, EntityRef target,
				   const char* tag, F32 value, ACMD_EXPR_ENTARRAY_IN entArrayData, F32 maxDist,
				   const char* anim, FSMLDSendMessage* mydata)
{
	Entity* targetE = entFromEntityRef(entGetPartitionIdx(e), target);
	F32 dist;
	int partitionIdx = entGetPartitionIdx(e);

	// TODO: Make this actually clean up its animation if the critter switches states
	// while doing the animation

	devassertmsg(!!anim == !!mydata, "Can't have an anim without a struct to track its state");

	if(!targetE)
		return;

	dist = entGetDistance(e, NULL, targetE, NULL, NULL);

	if(!maxDist || dist < SQR(maxDist))
	{
		aiMessageSendAbstract(partitionIdx, targetE->aibase->fsmContext, tag, e, value, entArrayData);
		
		if(anim && anim[0] && !mydata->animOn)
		{
			U32 bitHandle = mmGetAnimBitHandleByName(anim, 0);
			mydata->animOn = 1;
			aiMovementAddAnimBitHandle(e, bitHandle, &mydata->handle);
		}
	}
	else
	{
		aiMovementSetTargetEntity(e, aib, targetE, NULL, 0, AI_MOVEMENT_ORDER_ENT_UNSPECIFIED, AI_MOVEMENT_TARGET_CRITICAL);
		if(mydata && mydata->animOn)
		{
			mydata->animOn = 0;
			aiMovementRemoveAnimBitHandle(e, mydata->handle);
		}
	}
}

static __forceinline AIMessage* getMessage(FSMContext* fsmContext, const char* tag)
{
	AIMessage* msg = NULL;

	stashFindPointer(fsmContext->messages, tag, &msg);

	return msg;
}

S64 aiMessageGetReceivedTime(FSMContext* fsmContext, const char* msgTag)
{
	AIMessage *msg = getMessage(fsmContext, msgTag);

	if(msg)
	{
		AIMessageEntry *lastEntry = eaTail(&msg->entries);

		return lastEntry ? lastEntry->time : 0;
	}

	return 0;
}

void aiMessageClearMessage(FSMContext* fsmContext, const char* tag)
{
	AIMessage* msg = getMessage(fsmContext, tag);

	if(!msg)
		return;

	eaClearEx(&msg->entries, aiMessageEntryDestroy);

	msg->totalCount = 0;
	msg->totalValue = 0;
	msg->timeLastReceived = 0;
}

int aiMessageCheck(FSMContext* fsmContext, const char* tag, AIMessage** retMsg)
{
	AIMessage* msg = getMessage(fsmContext, tag);

	if(retMsg)
		*retMsg = msg;

	if(msg)
		return msg->totalCount;
	else
		return 0;
}

F32 aiMessageCheckValue(FSMContext* fsmContext, const char* tag)
{
	AIMessage* msg = getMessage(fsmContext, tag);

	if(msg)
		return msg->totalValue;
	else
		return 0;
}

int aiMessageCheckLastAbsTime(int partitionIdx, FSMContext* fsmContext, const char* tag, S64 absTimeDiff)
{
	AIMessage* msg = getMessage(fsmContext, tag);
	S64 cutOff = ABS_TIME_PARTITION(partitionIdx) - absTimeDiff;
	U32 numMsgs;
	int i;
	U32 count = 0;

	if(!msg)
		return 0;

	numMsgs = eaSize(&msg->entries);

	for(i = numMsgs-1; i >= 0 && msg->entries[i]->time > cutOff; i--)
		count++;

	return count;
}

int aiMessageCheckLastXSec(int partitionIdx, FSMContext* fsmContext, const char* tag, F32 sec)
{
	return aiMessageCheckLastAbsTime(partitionIdx, fsmContext, tag, SEC_TO_ABS_TIME(sec));
}

F32 aiMessageCheckLastAbsTimeValue(int partitionIdx, FSMContext* fsmContext, const char* tag, S64 absTimeDiff)
{
	AIMessage* msg = getMessage(fsmContext, tag);
	S64 cutOff = ABS_TIME_PARTITION(partitionIdx) - absTimeDiff;
	U32 numMsgs;
	int i;
	F32 value = 0;

	if(!msg)
		return 0;

	numMsgs = eaSize(&msg->entries);

	for(i = numMsgs-1; i >= 0 && msg->entries[i]->time > cutOff; i--)
		value += msg->entries[i]->value;

	return value;
}

F32 aiMessageCheckLastXSecValue(int partitionIdx, FSMContext* fsmContext, const char* tag, F32 sec)
{
	return aiMessageCheckLastAbsTimeValue(partitionIdx, fsmContext, tag, SEC_TO_ABS_TIME(sec));
}

void aiMessageDestroyHelper(void* message)
{
	AIMessage* msg = (AIMessage*) message;

	eaDestroyEx(&msg->entries, aiMessageEntryDestroy);
	eaiDestroy(&msg->refArray);

	MP_FREE(AIMessage, msg);
}

void aiMessageDestroyAll(FSMContext* fsmContext)
{
	stashTableDestroyEx(fsmContext->messages, NULL, aiMessageDestroyHelper);
	fsmContext->messages = NULL;
}

int enableHealingAggro = 1;
AUTO_CMD_INT(enableHealingAggro, enableHealingAggro);

// used for combat role damage tracking, to not add in damage for a role more than once per message 
static U32 s_damageNotifyFlag = 0;

static void aiMessageSendNotify(Entity* e, Entity* sourceE, AINotifyType notifyType, 
								const char* dmgMsg, const char* dmgDealtMsg, F32 damageVal)
{
	AIVarsBase* aib = e->aibase;
	Entity* sourceOwner = NULL;
	AIVarsBase* sourceAIB = sourceE->aibase;
	int partitionIdx = entGetPartitionIdx(e);
	F32 damageForOwner = 0.f;

	if((sourceE->erCreator && (sourceOwner = entFromEntityRef(partitionIdx, sourceE->erCreator))) ||
		(sourceE->erOwner && (sourceOwner = entFromEntityRef(partitionIdx, sourceE->erOwner))))
	{
		AIConfig* sourceConfig = aiGetConfig(sourceE, sourceAIB);
		if(sourceConfig->ownerAggroDistributionInitial)
		{
			damageForOwner = damageVal * sourceConfig->ownerAggroDistributionInitial;
			damageVal *= 1.f - sourceConfig->ownerAggroDistributionInitial;
		}
		else 
		{
			// Give a very, very tiny amount
			damageForOwner = 0.001f * damageVal;
			damageVal *= 0.999f;
		}
	}

	if(e!=sourceE)
	{
		aiMessageReceive(partitionIdx, aib->fsmContext, dmgMsg, sourceE, damageVal, NULL);

		aib->time.lastDamage[notifyType] = ABS_TIME_PARTITION(partitionIdx);
		if(!aib->time.lastInitialDamage[notifyType])
			aib->time.lastInitialDamage[notifyType] = ABS_TIME_PARTITION(partitionIdx);
	}

	if(sourceAIB)
		aiMessageReceive(partitionIdx, sourceAIB->fsmContext, dmgDealtMsg, e, damageVal, NULL); 

	if(damageVal > 0 || notifyType == AI_NOTIFY_TYPE_THREAT)
	{
		if(notifyType==AI_NOTIFY_TYPE_HEALING && !enableHealingAggro)
			return;

		if(!sourceE->pChar)
			return;

		if(e==sourceE)
		{
			if(notifyType!=AI_NOTIFY_TYPE_HEALING)
				return;  // Ignore non-healing from self
			else if(!aiGlobalSettings.selfHealingCountsAsAggro)
				return;  // Only ignore healing from self if not specified
		}
		else if(!critter_IsKOS(partitionIdx, e, sourceE) && notifyType!=AI_NOTIFY_TYPE_HEALING)
		{
			if (entIsCivilian(e))
			{
				aiCivScarePedestrian(e, sourceE, NULL);
			}
			return;  // Ignore non-healing from friends for weird, designer-y cases
		}

		// might not have gotten the ai tick to make the team yet here
		if(notifyType == AI_NOTIFY_TYPE_HEALING)
		{
			// parallel lists of cached entity info
			static Entity **s_eaHealNotifyEnts = NULL;
			static AIStatusTableEntry **s_eaStatusEntries = NULL;
			int i;
			F32 healingAggro;
			int seperateTargetHealing;
		
			eaClear(&s_eaHealNotifyEnts);
			eaClear(&s_eaStatusEntries);
			// go through and find all the valid ents that we will be telling of our fatty heals
			for(i = eaiSize(&aib->statusCleanup)-1; i >= 0; i--)
			{
				Entity* trackingEnt = entFromEntityRef(partitionIdx, aib->statusCleanup[i]);
				AIStatusTableEntry* trackingStatus;
				AITeamStatusEntry* trackingTeamStatus = NULL;
				if(!trackingEnt || !critter_IsKOS(partitionIdx, trackingEnt, sourceE))
					continue;
				if (aiAggro_ShouldIgnoreHealing(partitionIdx, trackingEnt->aibase))
					continue;

				trackingStatus = aiStatusFind(trackingEnt, trackingEnt->aibase, e, false);
				if(trackingStatus)
					trackingTeamStatus = aiGetTeamStatus(trackingEnt, trackingEnt->aibase, trackingStatus);
				if(trackingStatus && trackingTeamStatus && trackingTeamStatus->legalTarget)
				{
					eaPush(&s_eaHealNotifyEnts, trackingEnt);
					eaPush(&s_eaStatusEntries, trackingStatus);
				}
			}
		
			if (eaSize(&s_eaHealNotifyEnts) == 0)
				return; // no one to share healing aggro with

			if (aiAggro_ShouldScaleHealAggroByLegalTargets())
			{	// check if we should split the healing aggro among the statusCleanup ents
				healingAggro = damageVal / (F32)eaSize(&s_eaHealNotifyEnts);
			}
			else
			{
				healingAggro = damageVal;
			}
		
			seperateTargetHealing = aiAggro_ShouldSeperateHealingByAttackTarget();
		
			for(i = eaSize(&s_eaHealNotifyEnts)-1; i >= 0; i--)
			{
				Entity* trackingEnt = s_eaHealNotifyEnts[i];
				AIStatusTableEntry* trackingStatus = s_eaStatusEntries[i];
			
				AIStatusTableEntry* healerStatus = aiStatusFind(trackingEnt, trackingEnt->aibase, sourceE, true);
				AITeamStatusEntry* healerTeamStatus = aiGetTeamStatus(trackingEnt, trackingEnt->aibase, healerStatus);
				if(healerTeamStatus && !healerTeamStatus->legalTarget)
				{
					healerTeamStatus->timeLastAggressiveAction = ABS_TIME_PARTITION(partitionIdx);
					aiAddLegalTarget(trackingEnt, trackingEnt->aibase, sourceE);
				}

				if (!seperateTargetHealing)
				{
					healerStatus->lastNotify[AI_NOTIFY_TYPE_HEALING] = ABS_TIME_PARTITION(partitionIdx);
					healerStatus->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_HEALING] += healingAggro;
				}
				else
				{	// check to see if the target (e) is this entities current attack target
					if (trackingEnt->aibase->attackTarget == e) 
					{	// source is healing my current attack target
						healerStatus->lastNotify[AI_NOTIFY_TYPE_HEALING] = ABS_TIME_PARTITION(partitionIdx);
						healerStatus->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_HEALING] += healingAggro;
					}
					else
					{
						healerStatus->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_HEALING] += healingAggro;
					}
				}
			}
		}
		else if(notifyType < AI_NOTIFY_TYPE_TRACKED_COUNT)
		{
			AITeam* combatTeam;
			AITeamStatusEntry* teamStatus;
		
			aiAddLegalTarget(e, aib, sourceE);

			// Get combat team AFTER adding legal target
			combatTeam = aiTeamGetCombatTeam(e, aib);
			teamStatus = aiTeamStatusFind(combatTeam, sourceE, true, true);

			combatTeam->time.lastDamaged = ABS_TIME_PARTITION(partitionIdx);
			combatTeam->trackedDamageTeam[notifyType] += damageVal;

			s_damageNotifyFlag++;

			FOR_EACH_IN_EARRAY(combatTeam->members, AITeamMember, pMember)
			{
				Entity* memberE = pMember->memberBE;
				AIVarsBase* memberAIB = memberE->aibase;
				AIStatusTableEntry* status;
				AITeamStatusEntry* memberTeamStatus;

				if(!memberE->pChar)
					continue;

				// An AOE that damages your friends shouldn't add you to their status table...
				if(!critter_IsKOS(partitionIdx, sourceE, memberE))
					continue;

				status = aiStatusFind(memberE, memberAIB, sourceE, true);
				status->time.lastCheckedLOS = ABS_TIME_PARTITION(partitionIdx);
				status->time.lastVisible = ABS_TIME_PARTITION(partitionIdx);
				status->visible = 1;

				memberTeamStatus = aiGetTeamStatus(memberE, memberAIB, status);
				if (memberTeamStatus)
					entGetPos(memberE, memberTeamStatus->lastKnownPos);

				if(notifyType < AI_NOTIFY_TYPE_TRACKED_COUNT)
				{
					if(memberE == e)
					{
						status->lastNotify[notifyType] = ABS_TIME_PARTITION(partitionIdx);
						status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][notifyType] += damageVal;
						memberAIB->totalTrackedDamage[notifyType] += damageVal;
					}
					else 
					{	// notifying a friend, choose whether we blindly track the damage
						// or whether we only share friend aggro with explict ents
						if (!aiGlobalSettings.useCombatRoleDamageSharing)
						{
							status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][notifyType] += damageVal;
						}
						else if (pMember->pCombatRole)
						{	
							// see if this member is an ent that I share my aggro with
							if (eaiFind(&aib->eaSharedAggroEnts, memberE->myRef) != -1)
							{
								AICombatRolesTeamRole *pTeamRole = pMember->pCombatRole->pTeamRole;
							
								status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][notifyType] += damageVal;
							
								devassert(pTeamRole);
								if (pTeamRole->trackedMessageFlag != s_damageNotifyFlag)
								{
									// sum up the damage on the role of this member for normalization later
									pTeamRole->trackedDamageRole[notifyType] += damageVal;
									// set the counter for this message, so we do not add the damage to this role again
									pTeamRole->trackedMessageFlag = s_damageNotifyFlag;
								}
							}
						}
					}
				}
			}
			FOR_EACH_END

			if (entIsCivilian(e))
			{
				aiCivScarePedestrian(e, sourceE, NULL);
			}

			teamStatus->timeLastAggressiveAction = ABS_TIME_PARTITION(partitionIdx);
		}
		else if(notifyType == AI_NOTIFY_TYPE_AVOID || notifyType == AI_NOTIFY_TYPE_SOFT_AVOID)
		{
			aiAddLegalTarget(e, aib, sourceE);
		}
		else
			Errorf("Found unhandled damage tracking type in aiNotify, please tell Raoul");
	}
	
	// We do this last to avoid an order problem with pets and initial aggro in Aggro2
	if(damageForOwner)
	{
		aiMessageSendNotify(e, sourceOwner, notifyType, dmgMsg, dmgDealtMsg, damageForOwner);
	}
}

int aiNotify_ShouldIgnoreApplyIDDueToRespawn(Entity* sourceBE, U32 uiApplyID)
{
	return (sourceBE->aibase && sourceBE->aibase->uiRespawnApplyID > uiApplyID);
}

// Called when "be" takes damage from "source"
void aiNotify(Entity* e, Entity* sourceE, AINotifyType notifyType, F32 damageVal, F32 damageValNoOverage, void *params, int uid)
{
	if(aiGlobalSettings.disableOverageAggro)
		damageVal = damageValNoOverage;

	// Projectiles don't get notifys because they don't have AIVarsBase on which to store it
	if(entGetType(e) == GLOBALTYPE_ENTITYPROJECTILE || 
		sourceE && entGetType(sourceE) == GLOBALTYPE_ENTITYPROJECTILE)
		return;

	if(sourceE)
	{
		switch(notifyType)
		{
			case AI_NOTIFY_TYPE_DAMAGE:
				{
					if (!aiNotify_ShouldIgnoreApplyIDDueToRespawn(sourceE, uid))
						aiMessageSendNotify(e, sourceE, notifyType, "Powers.Damage", "Powers.DamageDealt", damageVal);

					// wake up!  You just got hit!
					if (e->pPlayer == NULL && e->aibase && !e->aibase->inCombat)
						aiForceThinkTick(e,e->aibase);
				}
			xcase AI_NOTIFY_TYPE_STATUS:
				if (!aiNotify_ShouldIgnoreApplyIDDueToRespawn(sourceE, uid))
					aiMessageSendNotify(e, sourceE, notifyType, "Powers.StatusDmg", "Powers.StatusDmgDealt", damageVal);
			xcase AI_NOTIFY_TYPE_THREAT:
				if (!aiNotify_ShouldIgnoreApplyIDDueToRespawn(sourceE, uid))
					aiMessageSendNotify(e, sourceE, notifyType, "Powers.Threat", "Powers.ThreatDealt", damageVal);
			xcase AI_NOTIFY_TYPE_AVOID:
			{
				AIAvoidParams *avoidParams = (AIAvoidParams*)params;
				devassert(avoidParams->params.eType == kAttribType_AIAvoid);

				if(avoidParams->eVolumeType == AIAvoidVolumeType_AVOID || avoidParams->eVolumeType == AIAvoidVolumeType_ENEMY_AVOID)
				{
					bool bShouldApply = true;

					if (avoidParams->eVolumeType == AIAvoidVolumeType_ENEMY_AVOID)
					{
						bShouldApply = false;
						if (critter_IsKOSEx(entGetPartitionIdx(sourceE),e,sourceE,true))
						{
							bShouldApply = true;
						}
					}

					if (bShouldApply)
					{
						F32 dist = entGetDistance(e, NULL, sourceE, NULL, NULL);

						if(dist < avoidParams->fRadius+5)
							aiMessageSendNotify(e, sourceE, notifyType, "Powers.Avoid", "Powers.Avoided", damageVal);
					
						if(e != sourceE)
						{
							// avoid instances are on the SOURCE not the TARGET of the power
							aiAvoidAddInstance(sourceE, sourceE->aibase, damageVal, avoidParams->fRadius, uid);
						}
					}
				}
				else 
				{
					devassert(avoidParams->eVolumeType == AIAvoidVolumeType_ATTRACT);
					if(e == sourceE)
					{
						// attraction instances are on the SOURCE not the TARGET of the power
						aiAttractAddInstance(sourceE, sourceE->aibase, avoidParams->fRadius, uid);
					}
				}

				//aiForceThinkTick(e,e->aibase);
			}
			xcase AI_NOTIFY_TYPE_SOFT_AVOID:
			{
				AISoftAvoidParams *avoidParams = (AISoftAvoidParams*)params;
				devassert(avoidParams->params.eType == kAttribType_AISoftAvoid);

				if(e != sourceE)
				{
					// avoid instances are on the SOURCE not the TARGET of the power
					aiSoftAvoidAddInstance(sourceE, sourceE->aibase, damageVal, avoidParams->fRadius, uid);
				}

			}
			xcase AI_NOTIFY_TYPE_HEALING:
				if (!aiNotify_ShouldIgnoreApplyIDDueToRespawn(sourceE, uid))
					aiMessageSendNotify(e, sourceE, notifyType, "Powers.Healing", "Powers.HealingDealt", damageVal);
			xdefault:
				devassertmsg(0, "Unknown notify type");
		}
	}
}

void aiNotifyPowerEnded(Entity* be, Entity* sourceEnt, AINotifyType notifyType, int oldUid, int newUid, void *params)
{
	if(notifyType == AI_NOTIFY_TYPE_AVOID)
	{
		AIAvoidParams *avoidParams = (AIAvoidParams*)params;
		AIVarsBase* sourceAIB = sourceEnt->aibase;

		devassert(avoidParams->params.eType == kAttribType_AIAvoid);

		// this can get called by powers after the AI for the critter has already been
		// cleaned up
		if(!sourceAIB)
			return;

		if(newUid)
		{
			if (avoidParams->eVolumeType == AIAvoidVolumeType_AVOID || avoidParams->eVolumeType == AIAvoidVolumeType_ENEMY_AVOID)
				aiAvoidUpdateInstance(sourceEnt, sourceEnt->aibase, oldUid, newUid);
			else 
				aiAttractUpdateInstance(sourceEnt, sourceEnt->aibase, oldUid, newUid);
		}
		else
		{
			if (avoidParams->eVolumeType == AIAvoidVolumeType_AVOID || avoidParams->eVolumeType == AIAvoidVolumeType_ENEMY_AVOID)
				aiAvoidRemoveInstance(sourceEnt, sourceEnt->aibase, oldUid);
			else 
				aiAttractRemoveInstance(sourceEnt, sourceEnt->aibase, oldUid);
		}
	}
	else if (notifyType == AI_NOTIFY_TYPE_SOFT_AVOID)
	{
		AISoftAvoidParams *softAvoidParams = (AISoftAvoidParams*)params;
		AIVarsBase* sourceAIB = sourceEnt->aibase;

		devassert(softAvoidParams->params.eType == kAttribType_AISoftAvoid);

		// this can get called by powers after the AI for the critter has already been
		// cleaned up
		if(!sourceAIB)
			return;

		aiSoftAvoidRemoveInstance(sourceEnt, sourceEnt->aibase, oldUid);
	}
}

S32 aiNotifyPowerMissed(Entity* target, Entity* source, F32 threatScale)
{
	return aiAggro_DoPowerMissedAggro(target,source,threatScale);
}

void aiNotifyInteracted(Entity* e, Entity* source)
{
	int partitionIdx = entGetPartitionIdx(e);
	aiMessageSendAbstract(partitionIdx, e->aibase->fsmContext, "Interacted", source, 1, NULL);
}

AUTO_COMMAND;
void aiNotifyDebug(Entity* be, char* affected, char* source,
				   ACMD_NAMELIST(AINotifyTypeEnum, STATICDEFINE) char* notifyType,
				   F32 damageVal, F32 radius)
{
	Entity* affectedBE = entGetClientTarget(be, affected, NULL);
	Entity* sourceBE = entGetClientTarget(be, source, NULL);
	AINotifyType notifyTypeInt = StaticDefineIntGetInt(AINotifyTypeEnum, notifyType);
	AIAvoidParams avoidParams = {0};

	if(notifyTypeInt == -1 || notifyTypeInt >= AI_NOTIFY_TYPE_COUNT)
		return;

	StructInit(parse_AIAvoidParams, &avoidParams);
	avoidParams.eVolumeType = AIAvoidVolumeType_AVOID;
	avoidParams.fRadius = radius;

	aiNotify(affectedBE, sourceBE, notifyTypeInt, damageVal, damageVal, &avoidParams, 0);
}

AUTO_RUN;
int addCallback(void)
{
	combat_SetPowerExecutedCallback(aiNotifyPowerExecuted);
	combat_SetPowerRechargedCallback(aiNotifyPowerRecharged);
	return 1;
}

void aiNotifyPowerRecharged(Entity* e, Power* power)
{
	// only recalculate if the recharge time is significant
	PowerDef *powerDef = GET_REF(power->hDef);
	if(powerDef && powerDef->fTimeRecharge >= 5.0)
	{
		AIVarsBase* aib = e->aibase;
	
		aiPowersCalcDynamicPreferredRange(e, aib); // recalculate
	}
}

void aiNotifyPowerExecuted(Entity* e, Power* power)
{
	AIVarsBase* aib = e->aibase;
	AIPowerInfo* lastUsed;
	int partitionIdx;

	if (aib == NULL)
		return;
	
	lastUsed = aib->powers->lastUsedPower;
	partitionIdx = entGetPartitionIdx(e);

	if(!lastUsed || lastUsed->power != power && lastUsed->power != power->pParentPower)
	{
		char* estr = NULL;
		PowerDef* execDef = GET_REF(power->hDef);
		PowerDef* lastUsedDef = lastUsed ? GET_REF(lastUsed->power->hDef) : NULL;

		if(execDef)
		{
			estrStackCreate(&estr);
			estrPrintf(&estr, "Got execution notification for %s, but thought I executed %s last... Please get Raoul",
				execDef->pchName, lastUsed ? lastUsedDef->pchName : "nothing");
			devassertmsg(!lastUsed || lastUsed->power != power, estr);
			estrDestroy(&estr);
		}
	}
	aib->time.lastActivatedPower = ABS_TIME_PARTITION(partitionIdx);
	aib->powers->lastUsedPower = NULL;

	if(lastUsed && lastUsed->power && lastUsed->power->fTimeRecharge >= 5.0)
	{
		// recalculate
		aiPowersCalcDynamicPreferredRange(e, aib);
	}


	if(AI_DEBUG_ENABLED(AI_LOG_COMBAT, 6))
	{
		PowerDef* execDef = GET_REF(power->hDef);

		if(execDef)
			AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 6, "Got Execution notification for power %s", execDef->pchName);
	}

	aiCombatOnPowerExecuted(e, aib, lastUsed);
}

static void aiAddLegalTargetIfVisible(Entity* source, Entity* target)
{
	AIVarsBase* sourceAIB = source->aibase;
	AITeam* combatTeam = aiTeamGetCombatTeam(source, sourceAIB);
	if(combatTeam && (combatTeam->config.addLegalTargetWhenTargeted || 
						combatTeam->config.addLegalTargetWhenMemberAttacks))
	{
		aiAddLegalTarget(source, sourceAIB, target);
	}
}

void aiNotifyUpdateCombatTimer(Entity* source, Entity* target, bool bSourceOnly)
{
	if(entGetType(source)==GLOBALTYPE_ENTITYPROJECTILE || entGetType(target)==GLOBALTYPE_ENTITYPROJECTILE)
		return;

	// catch players or pet owners firing off an initial power to have their pets aggro
	aiAddLegalTargetIfVisible(source, target); 
	if(!bSourceOnly)
	{
		// catch players shooting at a team of guys
		aiAddLegalTargetIfVisible(target, source);
	}
}

void aiMessageProcessChat(Entity* e, Entity* src, const char *msg)
{
	int i;
	AIVarsBase *aib = e->aibase;
	int partitionIdx = entGetPartitionIdx(e);

	if(!e->pCritter && !e->pSaved)
		return;

	if(!msg || !msg[0])
		return;

	for(i=eaSize(&aib->messageListens)-1; i>=0; i--)
	{
		if(strstri(msg, aib->messageListens[i]))
			aiMessageReceive(partitionIdx, aib->fsmContext, aib->messageListens[i], src, 1, NULL);
	}
}

void aiMessageProcessTarget(Entity* e, Entity* src)
{
	AIVarsBase *aib = e->aibase;
	int partitionIdx = entGetPartitionIdx(e);
	if(!aib->targetListen)
		return;

	aiMessageReceive(partitionIdx, aib->fsmContext, "Targeted", src, 1, NULL);
}

// Enables the "targeted" message for the FSM
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetTargetingListen);
void exprFuncSetTargetingListen(ACMD_EXPR_SELF Entity* e, int on)
{
	AIVarsBase *aib = e->aibase;

	aib->targetListen = !!on;
}

// Adds the given string to be listened for in chat
AUTO_EXPR_FUNC(ai) ACMD_NAME(ChatAddListen);
ExprFuncReturnVal exprFuncChatAddListen(ACMD_EXPR_SELF Entity *e, const char *msg, ACMD_EXPR_ERRSTRING errString)
{
	AIVarsBase *aib = e->aibase;

	if(!e->pCritter && !e->pSaved)
		return ExprFuncReturnFinished;

	if(!msg || !msg[0])
	{
		estrPrintf(errString, "Empty or NULL string passed to ChatAddListen");
		return ExprFuncReturnError;
	}

	eaPushUnique(&aib->messageListens, allocAddString(msg));

	return ExprFuncReturnFinished;
}

// Sends a message with the specified tag (of specified value and with entarray attached) to every entity in the ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMessageValueEntArray);
void exprFuncSendMessageValueEntArray(ExprContext* exprContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char* msgTag, ACMD_EXPR_ENTARRAY_IN entArrayData, F32 value)
{
	Entity *e = exprContextGetSelfPtr(exprContext);
	int i;

	for(i = eaSize(ents) - 1; i >= 0; i--)
	{
		if(e)
			aiMessageSendEntToEnt(e, e->aibase, entGetRef((*ents)[i]), msgTag, value, entArrayData, 0, NULL, NULL);
		else
			aiMessageSendAbstract(iPartitionIdx, (*ents)[i]->aibase->fsmContext, msgTag, NULL, value, entArrayData);
	}
}

// Sends a message from entFrom to all ents in entsTo - CANNOT send from more than one ent
//  Also, you should generally avoid using this (except for, e.g., AICommands)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMessageExplicitFrom);
ExprFuncReturnVal exprFuncSendMessageExplicitFrom(ExprContext* exprContext, ACMD_EXPR_ENTARRAY_IN entFrom, ACMD_EXPR_ENTARRAY_IN entsTo, const char* msgTag, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	int i;
	Entity *src;

	if(eaSize(entFrom)==0)
		return ExprFuncReturnFinished;

	if(eaSize(entFrom)>1)
	{
		*errString = "Cannot send a message from more than a single ent";
		return ExprFuncReturnError;
	}

	src = (*entFrom)[0];
	for(i=eaSize(entsTo)-1; i>=0; i--)
	{
		aiMessageSendEntToEnt(src, src->aibase, entGetRef((*entsTo)[i]), msgTag, 1, NULL, 0, NULL, NULL);
	}

	return ExprFuncReturnFinished;
}

// Sends a message with the specified tag to every entity in the ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMessage);
void exprFuncSendMessage(ExprContext* exprContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char* msgTag)
{
	exprFuncSendMessageValueEntArray(exprContext, iPartitionIdx, ents, msgTag, NULL, 1);
}

// Sends a message with the specified tag (with specified value) to every entity in the ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMessageValue);
void exprFuncSendMessageValue(ExprContext* exprContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char* msgTag, F32 value)
{
	exprFuncSendMessageValueEntArray(exprContext, iPartitionIdx, ents, msgTag, NULL, value);
}

// Sends a message with the specified tag to every entity in the ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMessageEntArray);
void exprFuncSendMessageEntArray(ExprContext* exprContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char* msgTag, ACMD_EXPR_ENTARRAY_IN entArrayData)
{
	exprFuncSendMessageValueEntArray(exprContext, iPartitionIdx, ents, msgTag, entArrayData, 1);
}

// Sends a message to the target ent if within range, moves towards the entity if too far away.
// Also optionally plays an anim bit when specified
// TODO: This should eventually take an animlist instead of an animbit, let me know if that becomes urgent
AUTO_EXPR_FUNC(ai) ACMD_NAME(SendMessageWithDistAndAnim);
void exprFuncSendMessageWithDistAndAnim(ACMD_EXPR_SELF Entity* be, ExprContext* context,
										ACMD_EXPR_ENTARRAY_IN entsIn, const char* msgTag,
										F32 dist, const char* animStr, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	FSMLDSendMessage* mydata = getMyData(context, parse_FSMLDSendMessage, 0);

	if(!eaSize(entsIn))
		return;

	if(eaSize(entsIn) > 1)
	{
		*errString = "Only allowed to do SendMessageWithDistAndAnim on one entity";
		return;
	}

	aiMessageSendEntToEnt(be, be->aibase, entGetRef((*entsIn)[0]), msgTag, 1, NULL, dist, animStr, mydata);
}

// Sends specified message to all team members
AUTO_EXPR_FUNC(ai) ACMD_NAME(SendMessageTeam);
void exprFuncSendMessageTeam(ACMD_EXPR_SELF Entity* e, const char* msgTag)
{
	AITeam* team = e->aibase->insideCombatFSM ? aiTeamGetAmbientTeam(e, e->aibase) : aiTeamGetCombatTeam(e, e->aibase);
	int i;

	// Entities should always have teams, but not all ways to spawn things add them to teams
	// right now (will fix that soon hopefully)
	if(!team)
		return;

	for(i = eaSize(&team->members)-1; i >= 0; i--)
		aiMessageSendEntToEnt(e, e->aibase, entGetRef(team->members[i]->memberBE), msgTag, 1, NULL, 0, NULL, NULL);
}

// tries to return the entity's base fsmContext first, otherwise if no self entity prt, return the context's FSM
FSMContext* aiMessagesGetFSMContext(ExprContext* context)
{
	return exprContextGetFSMContext(context);
}

// Returns the time the message was last received (useful with TimeSince())
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(TimeMessageLastReceived);
S64 exprFuncTimeMessageLastReceived(ExprContext* context, const char* msgTag)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling TimeSinceLastMessage without an FSM context");
	else
		return aiMessageGetReceivedTime(fsmContext, msgTag);

	return 0;
}

// Clears the history of this message, useful for restarting an FSM
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(ClearMessage);
void exprFuncClearMessage(ExprContext* context, const char* msgTag)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling Clearmessage requires an FSM context");
	else
		aiMessageClearMessage(fsmContext, msgTag);
}

// Returns the total number of messages received for the specified tag
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessage);
int exprFuncCheckMessage(ExprContext* context, const char* msgTag)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	int retVal = -1;

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessage without an FSM context");
	else
		retVal = aiMessageCheck(fsmContext, msgTag, NULL);

	return retVal;
}

// Returns the total value of messages received for the specified tag
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageValue);
int exprFuncCheckMessageValue(ExprContext* context, const char* msgTag)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	int retVal = -1;

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessage without an FSM context");
	else
		retVal = aiMessageCheckValue(fsmContext, msgTag);

	return retVal;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCCheckMessageLastXSec(ExprContext* context, ACMD_EXPR_PARTITION partitionIdx, ACMD_EXPR_INT_OUT valueOut, const char* msgTag, F32 time, ACMD_EXPR_ERRSTRING errString)
{
	if(time>AIMESSAGE_MEMORY_TIME)
	{
		estrPrintf(errString, "Individual messages older than 120s are not stored.  Please use a smaller check or just look at the value.");
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Returns the total number of messages received in the last <time> seconds for the specified tag
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageLastXSec) ACMD_EXPR_STATIC_CHECK(exprFuncSCCheckMessageLastXSec);
ExprFuncReturnVal exprFuncCheckMessageLastXSec(ExprContext* context, ACMD_EXPR_PARTITION partitionIdx, ACMD_EXPR_INT_OUT countOut, const char* msgTag, F32 time, ACMD_EXPR_ERRSTRING errString)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	
	*countOut = -1;

	if(!fsmContext)
	{
		estrPrintf(errString, "Calling CheckMessageLastXSec without an FSM context");
		return ExprFuncReturnError;
	}
	else
		*countOut = aiMessageCheckLastXSec(partitionIdx, fsmContext, msgTag, time);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCCheckMessageLastXSecValue(ExprContext* context, ACMD_EXPR_PARTITION partitionIdx, ACMD_EXPR_FLOAT_OUT valueOut, const char* msgTag, F32 time, ACMD_EXPR_ERRSTRING errString)
{
	if(time>AIMESSAGE_MEMORY_TIME)
	{
		estrPrintf(errString, "Messages older than 120s are not stored.  Please use a smaller check or just look at the value.");
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Returns the total value for messages received for a specified tag in the past <time> seconds
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageLastXSecValue) ACMD_EXPR_STATIC_CHECK(exprFuncSCCheckMessageLastXSecValue);
ExprFuncReturnVal exprFuncCheckMessageLastXSecValue(ExprContext* context, ACMD_EXPR_PARTITION partitionIdx, ACMD_EXPR_FLOAT_OUT valueOut, const char* msgTag, F32 time, ACMD_EXPR_ERRSTRING errString)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	F32 retVal = -1;

	if(!fsmContext)
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageLastXSecValue without an FSM context");
		return ExprFuncReturnError;
	}
	else
		retVal = aiMessageCheckLastXSecValue(partitionIdx, fsmContext, msgTag, time);

	if(valueOut)
		*valueOut = retVal;

	return ExprFuncReturnFinished;
}

// Returns the count of the number of times the message was received for a specified tag, 0 if no ents
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageEntArray);
int exprFuncCheckMessageEntArray(ACMD_EXPR_ENTARRAY_IN entsIn, const char* msgTag, F32 time)
{
	S32 i;
	int retVal = 0;
	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* e = (*entsIn)[i];
		FSMContext *pFSMContext = e->aibase->fsmContext;

		retVal += aiMessageCheck(pFSMContext, msgTag, NULL);
	}

	return retVal;
}

// Returns the total value sent with messages received for a specified tag, 0 if no ents
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageEntArrayValue);
F32 exprFuncCheckMessageEntArrayValue(ACMD_EXPR_ENTARRAY_IN entsIn, const char* msgTag, F32 time)
{
	S32 i;
	F32 retVal = 0;
	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* e = (*entsIn)[i];
		FSMContext *pFSMContext = e->aibase->fsmContext;

		retVal += aiMessageCheckValue(pFSMContext, msgTag);
	}

	return retVal;
}

// Returns the count of the number of times the message was received for a specified tag in the past <time> seconds
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageEntArrayLastXSec);
int exprFuncCheckMessageEntArrayLastXSec(ACMD_EXPR_ENTARRAY_IN entsIn, const char* msgTag, F32 time)
{
	S32 i;
	int retVal = 0;
	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* e = (*entsIn)[i];
		FSMContext *pFSMContext = e->aibase->fsmContext;
		int partitionIdx = entGetPartitionIdx(e);

		retVal += aiMessageCheckLastXSec(partitionIdx, pFSMContext, msgTag, time);
	}
	
	return retVal;
}

// Note, this does NOT HAVE A PER ENTITY TIMER, so be careful if your entity array may change
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageEntArraySinceLastCheck);
int exprFuncCheckMessageEntArraySinceLastCheck(ExprContext *context, ACMD_EXPR_PARTITION partitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, const char* msgTag)
{
	S32 i;
	int retVal = 0;
	FSMLDGenericU64ExitHandlers *mydata = getMyData(context, parse_FSMLDGenericU64ExitHandlers, PTR_TO_INT(msgTag));
	
	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* e = (*entsIn)[i];
		FSMContext *fsmContext = e->aibase->fsmContext;

		if(!fsmContext)
			ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageSinceLastCheck without an FSM context");
		else
			retVal += aiMessageCheckLastAbsTime(partitionIdx, fsmContext, msgTag, ABS_TIME_SINCE_PARTITION(partitionIdx, mydata->myU64));
	}

	mydata->myU64 = ABS_TIME_PARTITION(partitionIdx);

	return retVal;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(CheckOwnerMessageLastXSec);
int exprFuncCheckOwnerMessageLastXSec(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, const char* msgTag, F32 time)
{
	Entity *owner = e->erOwner ? entFromEntityRef(iPartitionIdx, e->erOwner) : NULL;
	FSMContext *ownerFSMContext;

	if(!owner)
		return 0;

	ownerFSMContext = owner->aibase->fsmContext;

	return aiMessageCheckLastXSec(iPartitionIdx, ownerFSMContext, msgTag, time);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(CheckOwnerMessageLastXSecValue);
F32 exprFuncCheckOwnerMessageLastXSecValue(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, const char *msgTag, F32 time)
{
	Entity *owner = e->erOwner ? entFromEntityRef(iPartitionIdx, e->erOwner) : NULL;
	FSMContext *ownerFSMContext;

	if(!owner)
		return 0;

	ownerFSMContext = owner->aibase->fsmContext;

	return aiMessageCheckLastXSecValue(iPartitionIdx, ownerFSMContext, msgTag, time);
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageSinceLastCheck);
int exprFuncCheckMessageSinceLastCheck(ExprContext *context, ACMD_EXPR_PARTITION partitionIdx, const char *msgTag)
{
	int retval = -1;
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	FSMLDGenericU64ExitHandlers *mydata = getMyData(context, parse_FSMLDGenericU64ExitHandlers, PTR_TO_INT(msgTag));

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageSinceLastCheck without an FSM context");
	else
		retval = aiMessageCheckLastAbsTime(partitionIdx, fsmContext, msgTag, ABS_TIME_SINCE_PARTITION(partitionIdx, mydata->myU64));

	mydata->myU64 = ABS_TIME_PARTITION(partitionIdx);

	return retval;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageSinceLastCheckGlobal);
int exprFuncCheckMessageSinceLastCheckGlobal(ExprContext *context, ACMD_EXPR_PARTITION partitionIdx, const char *msgTag)
{
	int retval = -1;
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	const char* msgTagPool = allocAddString(msgTag); // make this a global pointer
	FSMLDGenericU64ExitHandlers *mydata = getMyData(context, parse_FSMLDGenericU64ExitHandlers, PTR_TO_INT(msgTagPool));

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageSinceLastCheck without an FSM context Global");
	else
		retval = aiMessageCheckLastAbsTime(partitionIdx, fsmContext, msgTagPool, ABS_TIME_SINCE_PARTITION(partitionIdx, mydata->myU64));

	mydata->myU64 = ABS_TIME_PARTITION(partitionIdx);

	return retval;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckMessageSinceLastCheckValue);
F32 exprFuncCheckMessageSinceLastCheckValue(ExprContext *context, ACMD_EXPR_PARTITION partitionIdx, const char *msgTag)
{
	F32 retval = -1;
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	FSMLDGenericU64ExitHandlers *mydata = getMyData(context, parse_FSMLDGenericU64ExitHandlers, PTR_TO_INT(msgTag));
	
	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageSinceLastCheck without an FSM context");
	else
		retval = aiMessageCheckLastAbsTimeValue(partitionIdx, fsmContext, msgTag, ABS_TIME_SINCE_PARTITION(partitionIdx, mydata->myU64));

	mydata->myU64 = ABS_TIME_PARTITION(partitionIdx);

	return retval;
}

static void getMessageEntArrayData(int iPartitionIdx, FSMContext* fsmContext, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* msgTag, int dontExclude, int includeDead)
{
	AIMessage* msg = getMessage(fsmContext, msgTag);
	if(msg)
	{
		int i;

		for(i = eaiSize(&msg->refArray)-1; i >= 0; i--)
		{
			Entity* ent = entFromEntityRef(iPartitionIdx, msg->refArray[i]);
			if(ent && (dontExclude || !exprFuncHelperShouldExcludeFromEntArray(ent)) &&
				(includeDead || exprFuncHelperEntIsAlive(ent)))
			{
				eaPush(entsOut, ent);
			}
		}
	}
}

// Returns the ent array last passed to a specific message
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetMessageEntArrayData);
void exprFuncGetMessageEntArrayData(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* msgTag)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	F32 retVal = -1;

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageLastXSecValue without an FSM context");
	else
		getMessageEntArrayData(iPartitionIdx, fsmContext, entsOut, msgTag, false, false);
}

// Returns the ent array last passed to a specific message
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetMessageEntArrayDataAll);
void exprFuncGetMessageEntArrayDataAll(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* msgTag)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	F32 retVal = -1;

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageLastXSecValue without an FSM context");
	else
		getMessageEntArrayData(iPartitionIdx, fsmContext, entsOut, msgTag, true, false);
}

// Returns the ent array last passed to a specific message
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetMessageEntArrayDataDeadAll);
void exprFuncGetMessageEntArrayDataDeadAll(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* msgTag)
{
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);
	F32 retVal = -1;

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling CheckMessageLastXSecValue without an FSM context");
	else
		getMessageEntArrayData(iPartitionIdx, fsmContext, entsOut, msgTag, true, true);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetOwnerMessageEntArrayData);
void exprFuncGetOwnerMessageEntArrayData(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* msgTag)
{
	Entity *owner = e->erOwner ? entFromEntityRef(iPartitionIdx, e->erOwner) : NULL;
	FSMContext *ownerFSMContext;

	if(!owner)
		return;

	ownerFSMContext = owner->aibase->fsmContext;

	getMessageEntArrayData(iPartitionIdx, ownerFSMContext, entsOut, msgTag, false, false);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetOwnerMessageEntArrayDataAll);
void exprFuncGetOwnerMessageEntArrayDataAll(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* msgTag)
{
	Entity *owner = e->erOwner ? entFromEntityRef(iPartitionIdx, e->erOwner) : NULL;
	FSMContext *ownerFSMContext;

	if(!owner)
		return;

	ownerFSMContext = owner->aibase->fsmContext;

	getMessageEntArrayData(iPartitionIdx, ownerFSMContext, entsOut, msgTag, true, false);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetOwnerMessageEntArrayDataDeadAll);
void exprFuncGetOwnerMessageEntArrayDataDeadAll(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* msgTag)
{
	Entity *owner = e->erOwner ? entFromEntityRef(iPartitionIdx, e->erOwner) : NULL;
	FSMContext *ownerFSMContext;

	if(!owner)
		return;

	ownerFSMContext = owner->aibase->fsmContext;

	getMessageEntArrayData(iPartitionIdx, ownerFSMContext, entsOut, msgTag, true, true);
}


// Returns the timestamp of the last message received for the specified tag
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetLastMessageTime);
S64 exprFuncGetLastMessageTime(ExprContext* context, const char* msgTag)
{
	AIMessage* msg = NULL;
	FSMContext* fsmContext = aiMessagesGetFSMContext(context);

	if(!fsmContext)
		ErrorFilenamef(exprContextGetBlameFile(context), "Calling GetLastMessageTime without an FSM context");
	else
		aiMessageCheck(fsmContext, msgTag, &msg);

	return msg ? msg->timeLastReceived : 0;
}

AIMessage** debugMsgArray = NULL;

int aiMessageAddToDebugArray(StashElement element)
{
	AIMessage* msg = stashElementGetPointer(element);
	eaPush(&debugMsgArray, msg);
	return 1;
}

static int cmpAiMsg(const void *a, const void *b)
{
	return stricmp((*(AIMessage**)a)->tag,(*(AIMessage**)b)->tag);
}

// Prints all received messages to the server console for the current (entcon) entity
AUTO_COMMAND ACMD_NAME(aiPrintAllMessages) ACMD_LIST(gEntConCmdList);
void entConAiPrintAllMessages(Entity* e)
{
	AIVarsBase* aibase = e->aibase;
	int i, n;
	int partitionIdx = entGetPartitionIdx(e);

	eaClear(&debugMsgArray);

	stashForEachElement(aibase->fsmContext->messages, aiMessageAddToDebugArray);

	n = eaSize(&debugMsgArray);

	if(!n)
	{
		printf("No messages received\n");
		return;
	}

	eaQSort(debugMsgArray, cmpAiMsg);
	for(i = 0; i < n; i++)
	{
		AIMessage* msg = debugMsgArray[i];
		int numEntries = eaSize(&msg->entries);
		if(!numEntries)
			continue;
		printf("%s\t%d\t%.1f\n", msg->tag, numEntries, ABS_TIME_TO_SEC(ABS_TIME_SINCE_PARTITION(partitionIdx, msg->entries[numEntries-1]->time)));
	}
}

AUTO_COMMAND ACMD_NAME(AISendMessage) ACMD_LIST(gEntConCmdList);
void entConAISendMessage(CmdContext *context, Entity *e, const char* msg)
{
	Entity *caller = NULL;
	AIVarsBase *aib = e->aibase;
	int partitionIdx = entGetPartitionIdx(e);

	caller = entFromContainerID(partitionIdx, context->clientType, context->clientID);

	aiMessageSendAbstract(partitionIdx, e->aibase->fsmContext, msg, caller, 1, NULL);	
}
