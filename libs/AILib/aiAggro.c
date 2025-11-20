#include "aiAggro.h"

#include "Character.h"
#include "Character_target.h"
#include "Entity.h"
#include "EntityExtern.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ResourceManager.h"
#include "StringCache.h"

#include "aiConfig.h"
#include "aiDebugShared.h"
#include "aiEnums.h"
#include "aiLib.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "aiAvoid.h"
#include "CharacterAttribs.h"

#include "gslMapState.h"

#include "aiAggro_h_ast.h"
#include "aiAggro_c_ast.h"

// ---------------------------------------------------------------------------------------------------------

static DictionaryHandle gAIAggroDefDict;
static AIAggroDef* g_pDefaultAggro2Def = NULL;

static void aiAggro2_ProcessCounters(	Entity* e, 
										AIAggroDef *pAggroDef,
										AIConfig *pConfig, 
										AIStatusTableEntry *pStatus, 
										int iNumLegalTargets);
static void aiAggro2_ProcessGauges(	Entity* e, 
									AIAggroDef *pAggroDef,
									AIConfig *pConfig,
									AIStatusTableEntry *pStatus, 
									int iNumLegalTargets, 
									F32 fMaxCounterValue);

static void aiAggro2_ResetValues(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIStatusTableEntry* status);

// ---------------------------------------------------------------------------------------------------------
static __forceinline AIAggroDef* aiAggro2_GetAggroDef(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_OP_VALID const AIConfig *pConfig)
{
	AIAggroDef *pDef;

	if (!pConfig)
		pConfig = aiGetConfig(pEnt, pEnt->aibase);

	pDef = GET_REF(pConfig->aggro.hOverrideAggroDef);
	return pDef ? pDef : g_pDefaultAggro2Def;
}


// Removes all of my aggro from the target and its team.
AUTO_EXPR_FUNC(ai) ACMD_NAME(WipeMyAggroFromTeam);
void exprFuncWipeMyAggroFromTeam(ACMD_EXPR_SELF Entity *e, SA_PARAM_NN_VALID Entity *source)
{
	AITeam* combatTeam = aiTeamGetCombatTeam(e, e->aibase);
	aiTeamStatusRemove(combatTeam, source, "WipeMyAggroFromTeam expression");
}

// Remove ALL of my aggro from every critter that knows about me
AUTO_EXPR_FUNC(ai) ACMD_NAME(WipeMyAggroGlobal);
void exprFuncWipeMyAggroGlobal(ACMD_EXPR_SELF Entity *e)
{
	aiCleanupStatus(e, e->aibase);
}

// Removes my aggro from target critter
AUTO_EXPR_FUNC(ai) ACMD_NAME(WipeMyAggroFromTarget);
void exprFuncWipeMyAggroFromTarget(ACMD_EXPR_SELF Entity *e, SA_PARAM_NN_VALID Entity *source)
{
	aiStatusRemove(e, e->aibase, source, "WipeMyAggroFromTarget expression");
}

// Resets tracked aggro counts along with any tracked aggro2 counters
AUTO_EXPR_FUNC(ai) ACMD_NAME(ResetAggroGlobal);
ExprFuncReturnVal exprFuncResetTrackedAggroCounts(ACMD_EXPR_SELF Entity *selfEnt, ACMD_EXPR_ERRSTRING errString)
{
	FOR_EACH_IN_EARRAY(selfEnt->aibase->statusTable, AIStatusTableEntry, pStatus)
	{
		if(aiGlobalSettings.useAggro2)
		{
			aiAggro2_ResetValues(selfEnt, pStatus);
		}
		ZeroArray(pStatus->trackedAggroCounts);
	}
	FOR_EACH_END


	return ExprFuncReturnFinished;
}

// Finds the highest danger status entry on the selfEnt entity and copies the tracked counters 
// over to the status table of the target entity
AUTO_EXPR_FUNC(ai) ACMD_NAME(CopyHighestAggroToTarget);
ExprFuncReturnVal exprFuncCopyHighestAggroToTarget(	ACMD_EXPR_SELF Entity *selfEnt, 
													ACMD_EXPR_ENTARRAY_IN entsIn, 
													ACMD_EXPR_ERRSTRING errString)
{
	AIStatusTableEntry *pBestStatus = NULL;
	S32 i;
	F32 fBestDangerVal = 0.f;

	if(!eaSize(entsIn)) // no ents is ok
		return ExprFuncReturnFinished;
	
	// find the highest aggro on the target's status list
	FOR_EACH_IN_EARRAY(selfEnt->aibase->statusTable, AIStatusTableEntry, pStatus)
	{
		if (pStatus->totalBaseDangerVal > fBestDangerVal)
		{
			fBestDangerVal = pStatus->totalBaseDangerVal;
			pBestStatus = pStatus;
		}
	}
	FOR_EACH_END

	if (!pBestStatus)
		return ExprFuncReturnFinished;

	for (i = eaSize(entsIn) - 1; i >= 0; --i)
	{
		Entity *pTargetEnt = (*entsIn)[i];
		AIStatusTableEntry *pTargetStatus = aiStatusFind(selfEnt, selfEnt->aibase, pTargetEnt, false);
		if (!pTargetStatus)
		{
			estrPrintf(errString, "selfEnt does not have the entity REF(%d) Name(%s) on its status table.", 
									entGetRef(pTargetEnt), entGetLocalName(pTargetEnt) );
			return ExprFuncReturnError;
		}
		if (pTargetStatus != pBestStatus)
		{
			S32 x, j;
			for(x = AI_NOTIFY_TARGET_COUNT-1; x >= 0; x--)
			{
				for(j = AI_NOTIFY_TYPE_TRACKED_COUNT-1; j >= 0; j--)
				{
					pTargetStatus->trackedAggroCounts[x][j] = pBestStatus->trackedAggroCounts[x][j];
				}
			}
		}
	}

	

	return ExprFuncReturnFinished;

}

// Finds the highest danger status entry on the selfEnt entity and copies the tracked counters 
// over to the status table of the ent array
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetEntAggroTowardsMeFromHighest);
ExprFuncReturnVal exprFuncSetEntAggroTowardsMeFromHighest(	ACMD_EXPR_SELF Entity *selfEnt, 
															SA_PARAM_NN_VALID Entity *target, 
															F32 fAggroMultiplier,
															ACMD_EXPR_ERRSTRING errString)
{
	AIStatusTableEntry *pSelfStatus;
	AIStatusTableEntry *pBestStatus = NULL;
	F32 fBestDangerVal = 0.f;
	if (!target)
	{
		estrPrintf(errString, "No target passed into function.");
		return ExprFuncReturnError;
	}

	pSelfStatus = aiStatusFind(target, target->aibase, selfEnt, false);
	if (!pSelfStatus)
	{
		estrPrintf(errString, "target does not have me on its status table.");
		return ExprFuncReturnError;
	}

	// find the highest aggro on the target's status list
	FOR_EACH_IN_EARRAY(target->aibase->statusTable, AIStatusTableEntry, pStatus)
	{
		if (pStatus->totalBaseDangerVal > fBestDangerVal)
		{
			fBestDangerVal = pStatus->totalBaseDangerVal;
			pBestStatus = pStatus;
		}
	}
	FOR_EACH_END

	if (pBestStatus && pSelfStatus != pBestStatus)
	{
		S32 i, j;
		for(i = AI_NOTIFY_TARGET_COUNT-1; i >= 0; i--)
		{
			for(j = AI_NOTIFY_TYPE_TRACKED_COUNT-1; j >= 0; j--)
			{
				pSelfStatus->trackedAggroCounts[i][j] = pBestStatus->trackedAggroCounts[i][j] * fAggroMultiplier;
			}
		}
	}

	return ExprFuncReturnFinished;

}

// a test command for exprFuncCopyHigestAggroFromTargetForSource
AUTO_COMMAND;
void aiCopyHigestAggroFromTargetForSource(Entity *e, EntityRef erTarget)
{
	Entity *target = entFromEntityRef(entGetPartitionIdx(e), erTarget);
	if (e && target)
	{
		char *errString = NULL;
		Entity **eaEnts = NULL;
		eaPush(&eaEnts, e);
		exprFuncCopyHighestAggroToTarget(target, &eaEnts, &errString);
		estrDestroy(&errString);
		eaDestroy(&eaEnts);
	}
}


// Scales all of my damage values on target
AUTO_EXPR_FUNC(ai) ACMD_NAME(ScaleMyAggroFromTarget);
ExprFuncReturnVal exprFuncScaleMyAggroFromTarget(ACMD_EXPR_SELF Entity *e, SA_PARAM_NN_VALID Entity *source, F32 scale, ACMD_EXPR_ERRSTRING errString)
{
	AIStatusTableEntry *status = aiStatusFind(e, e->aibase, source, false);

	if(scale<0 || scale > 1)
	{
		estrPrintf(errString, "Invalid scale: %f (must be >0 and <1)", scale);
		return ExprFuncReturnError;
	}

	if(status)
	{
		int i, j;
		for(i = AI_NOTIFY_TARGET_COUNT-1; i >= 0; i--)
		{
			for(j = AI_NOTIFY_TYPE_TRACKED_COUNT-1; j >= 0; j--)
				status->trackedAggroCounts[i][j] *= scale;
		}
	}
	return ExprFuncReturnFinished;
}

// Removes my aggro from target as a flat subtraction from aggro I've dealt (in damage/status/avoid/threat/healing order)
//  DOES NOT update aggro values of DamageTeam on teammates of target
AUTO_EXPR_FUNC(ai) ACMD_NAME(RemoveEntArrayAggroTowardsMe);
ExprFuncReturnVal exprRemoveEntArrayAggroTowardsMe(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN ents, F32 amount, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	int amountCounter = amount;
	if(amount<0)
	{
		estrPrintf(errString, "Invalid aggro dump amount: %f (must be >0)", amount);
		return ExprFuncReturnError;
	}
	for (i = 0; i < eaSize(ents); i++)
	{
		AIStatusTableEntry *status = aiStatusFind((*ents)[i], (*ents)[i]->aibase, e, 0);
		amountCounter = amount;

		if(status)
		{
			int j;
			for(j=0; j<AI_NOTIFY_TYPE_TRACKED_COUNT; j++)
			{
				if (status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][i] >= 0.f)
				{
					if(amountCounter > status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][j])
					{
						amountCounter -= status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][j];
						status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][j] = 0;
					}
					else
					{
						status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][j] -= amountCounter;
						break;
					}
				}
			}
		}
	}

	return ExprFuncReturnFinished;
}

// Removes my aggro from target as a flat subtraction from aggro I've dealt (in damage/status/avoid/threat/healing order)
//  DOES NOT update aggro values of DamageTeam on teammates of target
AUTO_EXPR_FUNC(ai) ACMD_NAME(DumpMyAggroFromTarget);
ExprFuncReturnVal exprFuncDumpMyAggroFromTarget(ACMD_EXPR_SELF Entity *e, SA_PARAM_NN_VALID Entity *source, F32 amount, ACMD_EXPR_ERRSTRING errString)
{
	AIStatusTableEntry *status = aiStatusFind(e, e->aibase, source, 0);

	if(amount<0)
	{
		estrPrintf(errString, "Invalid aggro dump amount: %f (must be >0)", amount);
		return ExprFuncReturnError;
	}

	if(status)
	{
		int i;
		for(i=0; i<AI_NOTIFY_TYPE_TRACKED_COUNT; i++)
		{
			if (status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][i] >= 0.f)
			{
				if(amount > status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][i])
				{
					amount -= status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][i];
					status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][i] = 0;
				}
				else
				{
					status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][i] -= amount;
					return ExprFuncReturnFinished;
				}
			}
		}
	}

	return ExprFuncReturnFinished;
}

static void aiAggroAddVal(Entity* e, AITeam* combatTeam, Entity* target, AINotifyType notifyType, F32 amount)
{
	int i;
	AIStatusTableEntry *status = aiStatusFind(e, e->aibase, target, 1);

	if(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][notifyType] < amount)
		status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][notifyType] = 0;
	else
		status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][notifyType] -= amount;

	for(i=eaSize(&combatTeam->members)-1; i>=0; i--)
	{
		Entity* memberE = combatTeam->members[i]->memberBE;

		if(memberE==e)
			continue;

		status = aiStatusFind(e, memberE->aibase, target, 1);

		if(status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][notifyType] < amount)
			status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][notifyType] = 0;
		else
			status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][notifyType] -= amount;
	}
}

// Removes a flat amount of up to "amount" from the aggro type specified on all ents passed in
//  Also removes the same amount from the tracked friend aggro of my allies (e.g. dmgFr)
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntArrayRemoveAggro);
ExprFuncReturnVal exprFuncEntArrayRemoveAggro(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ENUM(AINotifyType) const char* aggroTypeStr, F32 amount, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	AINotifyType notifyType;
	AITeam* combatTeam;
	if(amount<0)
	{
		estrPrintf(errString, "Invalid aggro remove amount: %f (must be >0)", amount);
		return ExprFuncReturnError;
	}

	notifyType = StaticDefineIntGetInt(AINotifyTypeEnum, aggroTypeStr);
	if(notifyType<0 || notifyType>=AI_NOTIFY_TYPE_TRACKED_COUNT)
	{
		estrPrintf(errString, "Invalid aggro type : %s (not valid or not a basic aggro type)", aggroTypeStr);
		return ExprFuncReturnError;
	}

	combatTeam = aiTeamGetCombatTeam(e, e->aibase);
	for(i=0; i<eaSize(ents); i++)
		aiAggroAddVal(e, combatTeam, (*ents)[i], notifyType, -1 * amount);

	return ExprFuncReturnFinished;
}

// Adds a flat amount of aggro to the aggro type specified on all ents passed in
//  causeAttack specifies whether the critter will aggro on the target, even if not already aggroed
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntArrayAddAggro);
ExprFuncReturnVal exprFuncEntArrayAddAggro(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ENUM(AINotifyType) const char* aggroTypeStr, F32 amount, int causeAttack, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	AINotifyType notifyType;
	AITeam* combatTeam;
	if(amount<0)
	{
		estrPrintf(errString, "Invalid aggro add amount: %f (must be >0)", amount);
		return ExprFuncReturnError;
	}

	notifyType = StaticDefineIntGetInt(AINotifyTypeEnum, aggroTypeStr);
	if(notifyType<0 || notifyType>=AI_NOTIFY_TYPE_TRACKED_COUNT)
	{
		estrPrintf(errString, "Invalid aggro type : %s (not valid or not a basic aggro type)", aggroTypeStr);
		return ExprFuncReturnError;
	}

	combatTeam = aiTeamGetCombatTeam(e, e->aibase);
	for(i=0; i<eaSize(ents); i++)
	{
		Entity *target = (*ents)[i];

		if(!causeAttack)
			aiAggroAddVal(e, combatTeam, (*ents)[i], notifyType, amount);
		else
			aiNotify(e, target, notifyType, amount, amount, NULL, 0);
	}

	return ExprFuncReturnFinished;
}

// Copies the aggro list of another critter, first by clearing, then by copying
AUTO_EXPR_FUNC(ai) ACMD_NAME(CopyAggroTable);
ExprFuncReturnVal exprFuncCopyAggroTable(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	Entity *target;

	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "CopyAggroTable only takes 1 ent");
		return ExprFuncReturnError;
	}

	if(!eaSize(ents))
		return ExprFuncReturnFinished;

	target = (*ents)[0];

	aiClearStatusTable(e);
	for(i=eaSize(&target->aibase->statusTable)-1; i>=0; i--)
	{
		AIStatusTableEntry *statusT = target->aibase->statusTable[i];
		Entity *statusE = entFromEntityRef(iPartitionIdx, statusT->entRef);
		AITeamStatusEntry *teamStatus;

		if(statusE && critter_IsKOS(iPartitionIdx, statusE, e))
		{
			AIStatusTableEntry *status = aiStatusFind(e, e->aibase, statusE, true);
			int j, k;

			for(j=0; j<AI_NOTIFY_TYPE_TRACKED_COUNT; j++)
			{
				for(k=0; k < AI_NOTIFY_TARGET_COUNT; k++)
					status->trackedAggroCounts[k][j] = statusT->trackedAggroCounts[k][j];
			}

			teamStatus = aiGetTeamStatus(e, e->aibase, status);
			if(teamStatus && teamStatus->legalTarget)
				aiAddLegalTarget(e, e->aibase, statusE);
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetTrackedAggro);
ExprFuncReturnVal exprFuncGetTrackedAggro(ACMD_EXPR_FLOAT_OUT outFloat,
										  SA_PARAM_NN_VALID AIStatusTableEntry* status,
										  ACMD_EXPR_ENUM(AINotifyTargetType) const char* targetTypeStr,
										  ACMD_EXPR_ENUM(AINotifyType) const char* notifyTypeStr,
										  ACMD_EXPR_ERRSTRING errStr)
{
	AINotifyTargetType targetType = StaticDefineIntGetInt(AINotifyTargetTypeEnum, targetTypeStr);
	AINotifyType notifyType = StaticDefineIntGetInt(AINotifyTypeEnum, notifyTypeStr);

	if(targetType < 0 || targetType > AI_NOTIFY_TARGET_COUNT)
	{
		estrPrintf(errStr, "%s is not a valid target type", targetTypeStr);
		return ExprFuncReturnError;
	}

	if(notifyType < 0 || notifyType > AI_NOTIFY_TYPE_TRACKED_COUNT)
	{
		estrPrintf(errStr, "%s is not a valid tracked notify type", notifyTypeStr);
		return ExprFuncReturnError;
	}

	*outFloat = status->trackedAggroCounts[targetType][notifyType];

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetTotalTrackedAggro);
ExprFuncReturnVal exprFuncGetTotalTrackedAggro(ACMD_EXPR_FLOAT_OUT outFloat,
											   SA_PARAM_OP_VALID Entity* e,
											   ACMD_EXPR_ENUM(AINotifyType) const char* notifyTypeStr,
											   ACMD_EXPR_ERRSTRING errStr)
{
	AINotifyType notifyType = StaticDefineIntGetInt(AINotifyTypeEnum, notifyTypeStr);

	if(notifyType < 0 || notifyType > AI_NOTIFY_TYPE_TRACKED_COUNT)
	{
		estrPrintf(errStr, "%s is not a valid tracked notify type", notifyTypeStr);
		return ExprFuncReturnError;
	}

	if(e)
		*outFloat = e->aibase->totalTrackedDamage[notifyType];
	else
		*outFloat = 0;

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetTeamTotalTrackedAggro);
ExprFuncReturnVal exprFuncGetTeamTotalTrackedAggro(ACMD_EXPR_SELF Entity* e,
												   ACMD_EXPR_FLOAT_OUT outFloat,
												   ACMD_EXPR_ENUM(AINotifyType) const char* notifyTypeStr,
												   ACMD_EXPR_ERRSTRING errStr)
{
	AINotifyType notifyType = StaticDefineIntGetInt(AINotifyTypeEnum, notifyTypeStr);

	if(notifyType < 0 || notifyType > AI_NOTIFY_TYPE_TRACKED_COUNT)
	{
		estrPrintf(errStr, "%s is not a valid tracked notify type", notifyTypeStr);
		return ExprFuncReturnError;
	}

	*outFloat = e->aibase->team->trackedDamageTeam[notifyType];
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetTeamCenterAggroScalar);
F32 exprFuncGetTeamCenterAggroScalar( ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_VALID AIStatusTableEntry* pStatus, ACMD_EXPR_ERRSTRING errStr)
{
	Vec3 vCenterPos,vPlayerPos;
	Entity * pEnt = entFromEntityRef(iPartitionIdx, pStatus->entRef);
	AITeam* pTeam = exprContextGetVarPointerUnsafe(context, "Team");
	if (pEnt == NULL || pTeam == NULL)
	{
		return 0.0f;
	}

	aiTeamGetAveragePosition(pTeam,vCenterPos);
	entGetPos(pEnt,vPlayerPos);

	return (50.0f-MIN(50.0f,distance3(vCenterPos,vPlayerPos)))/50.0f;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetSoftAvoidAggroScalar);
F32 exprFuncGetSoftAvoidAggroScalar(ExprContext *context, ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_VALID AIStatusTableEntry *pStatus, ACMD_EXPR_ERRSTRING errStr)
{
	S32 iTotalMagnitude = 0;

	Entity *pEnt = entFromEntityRef(iPartitionIdx, pStatus->entRef);
	Entity* me = exprContextGetVarPointerUnsafe(context, "Me");
	
	if (pEnt && pEnt->aibase && me)
	{
		Vec3 entPos;
		entGetPos(pEnt, entPos);

		FOR_EACH_IN_EARRAY(me->aibase->softAvoid.base.volumeEntities, AIVolumeEntry, volumeEntry)
		{
			AIVolumeSoftAvoidInstance *instance = (AIVolumeSoftAvoidInstance *)volumeEntry->instance;
			devassert(instance);
			devassert(volumeEntry->isSphere);

			if (volumeEntry->isSphere && instance && instance->magnitude > 0)
			{
				Entity *avoidEnt = entFromEntityRef(iPartitionIdx, volumeEntry->entRef);
				if(avoidEnt)
				{
					F32 distSQR;
					Vec3 avoidEntPos;
					entGetPos(avoidEnt, avoidEntPos);
					distSQR = distance3Squared(entPos, avoidEntPos);

					if (distSQR <= SQR(volumeEntry->sphereRadius + 2.f))
					{
						S32 iMagnitudeAdjusted = instance->magnitude;
						iTotalMagnitude += CLAMP(iMagnitudeAdjusted, 1, 100);
					}
				}
			}
		}
		FOR_EACH_END

		// Make sure the total magnitude is not over 100
		MIN1(iTotalMagnitude, 100);
	}

	return iTotalMagnitude / 100.f;
}

// given an AIStatusTableEntry for an entity, returns the total aggro it has on the target entity
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetTotalAggroForTargetEnt);
F32 exprFuncGetTotalAggroFor(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_VALID AIStatusTableEntry* pStatus, 
								ACMD_EXPR_ENTARRAY_IN targetEnt, ACMD_EXPR_ERRSTRING errStr)
{
	EntityRef erTargetRet;
	Entity *pTargetEnt;
	Entity *pBaseEnt;
	if(eaSize(targetEnt)>1)
	{
		estrPrintf(errStr, "Too many entities passed into GetTotalAggroForTargetEnt: %d (1 allowed)", eaSize(targetEnt));
		return 0.f;
	}

	if(eaSize(targetEnt) == 0)
		return 0.f;
	
	pBaseEnt = entFromEntityRef(iPartitionIdx, pStatus->entRef);
	pTargetEnt = (*targetEnt)[0];
	if (!pTargetEnt || !pBaseEnt || !pBaseEnt->aibase)
		return 0.f;
	
	erTargetRet = entGetRef(pTargetEnt);

	// find the highest aggro on the target's status list
	FOR_EACH_IN_EARRAY(pBaseEnt->aibase->statusTable, AIStatusTableEntry, pEntry)
	{
		if (pEntry->entRef == erTargetRet)
		{
			return pEntry->totalBaseDangerVal;
		}
	}
	FOR_EACH_END

	return 0.f;
}


AUTO_STRUCT;
typedef struct aggroExpressions
{
	Expression* damageToMe;			AST(REQUIRED, NAME("DamageToMe"), REDUNDANT_STRUCT("DamageToMe:", parse_Expression_StructParam), LATEBIND)
	F32 damageToMeMin;				AST(REQUIRED, NAME("DamageToMeMin:"))
	F32 damageToMeMax;				AST(REQUIRED, NAME("DamageToMeMax:"))
	F32 defaultDamageToMeFactor;	AST(REQUIRED, NAME("DefaultDamageToMeFactor:"))
	F32 defaultDamageToMeDecayRate;	AST(NAME("DefaultDamageToMeDecayRate:"))

	Expression* damageToFriends;	AST(NAME("DamageToFriends"), REDUNDANT_STRUCT("DamageToFriends:", parse_Expression_StructParam), LATEBIND)
	F32 damageToFriendsMin;			AST(NAME("DamageToFriendsMin:"))
	F32 damageToFriendsMax;			AST(NAME("DamageToFriendsMax:"))
	F32 defaultDamageToFriendsFactor;AST(NAME("DefaultDamageToFriendsFactor:"))
	F32 defaultDamageToFriendsDecayRate;AST(NAME("DefaultDamageToFriendsDecayRate:"))

	Expression* distFromGuardPoint;	AST(NAME("DistFromGuardPoint"), REDUNDANT_STRUCT("DistFromGuardPoint:", parse_Expression_StructParam), LATEBIND)
	F32 distFromGuardPointMin;		AST(NAME("DistFromGuardPointMin:"))
	F32 distFromGuardPointMax;		AST(NAME("DistFromGuardPointMax:"))
	F32 defaultDistFromGuardPointFactor;AST(NAME("DefaultDistFromGuardPointFactor:"))

	Expression* personalSpace;		AST(NAME("PersonalSpace"), REDUNDANT_STRUCT("PersonalSpace:", parse_Expression_StructParam), LATEBIND)
	F32 personalSpaceMin;			AST(NAME("PersonalSpaceMin:"))
	F32 personalSpaceMax;			AST(NAME("PersonalSpaceMax:"))
	F32 defaultPersonalSpaceFactor;	AST(NAME("DefaultPersonalSpaceFactor:"))

	Expression* distFromMe;			AST(NAME("DistFromMe"), REDUNDANT_STRUCT("DistFromMe:", parse_Expression_StructParam), LATEBIND)
	F32 distFromMeMin;				AST(NAME("DistFromMeMin:"))
	F32 distFromMeMax;				AST(NAME("DistFromMeMax:"))
	F32 defaultDistFromMeFactor;	AST(NAME("DefaultDistFromMeFactor:"))

	Expression* statusToMe;			AST(NAME("StatusToMe"), REDUNDANT_STRUCT("StatusToMe:", parse_Expression_StructParam), LATEBIND)
	F32 statusToMeMin;				AST(NAME("StatusToMeMin:"))
	F32 statusToMeMax;				AST(NAME("StatusToMeMax:"))
	F32 defaultStatusToMeFactor;	AST(NAME("DefaultStatusToMeFactor:"))
	F32 defaultStatusToMeDecayRate;	AST(NAME("DefaultStatusToMeDecayRate:"))

	Expression* statusToFriends;	AST(NAME("StatusToFriends"), REDUNDANT_STRUCT("StatusToFriends:", parse_Expression_StructParam), LATEBIND)
	F32 statusToFriendsMin;			AST(NAME("StatusToFriendsMin:"))
	F32 statusToFriendsMax;			AST(NAME("StatusToFriendsMax:"))
	F32 defaultStatusToFriendsFactor;AST(NAME("DefaultStatusToFriendsFactor:"))
	F32 defaultStatusToFriendsDecayRate;AST(NAME("DefaultStatusToFriendsDecayRate:"))

	Expression* healingEnemies;		AST(NAME("HealingEnemies"), REDUNDANT_STRUCT("HealingEnemies:", parse_Expression_StructParam), LATEBIND)
	F32 healingEnemiesMin;			AST(NAME("HealingEnemiesMin:"))
	F32 healingEnemiesMax;			AST(NAME("HealingEnemiesMax:"))
	F32 defaultHealingEnemiesFactor;AST(NAME("DefaultHealingEnemiesFactor:"))
	F32 defaultHealingEnemiesDecayRate;AST(NAME("DefaultHealingEnemiesDecayRate:"))

	Expression* teamOrders;			AST(NAME("TeamOrders"), REDUNDANT_STRUCT("TeamOrders:", parse_Expression_StructParam), LATEBIND)
	F32 teamOrdersMin;				AST(NAME("TeamOrdersMin:"))
	F32 teamOrdersMax;				AST(NAME("TeamOrdersMax:"))
	F32 defaultTeamOrdersFactor;	AST(NAME("DefaultTeamOrdersFactor:"))

	Expression* threatToMe;			AST(NAME("ThreatToMe"), REDUNDANT_STRUCT("ThreatToMe:", parse_Expression_StructParam), LATEBIND)
	F32 threatToMeMin;				AST(NAME("ThreatToMeMin:"))
	F32 threatToMeMax;				AST(NAME("ThreatToMeMax:"))
	F32 defaultThreatToMeFactor;	AST(NAME("DefaultThreatToMeFactor:"))
	F32 defaultThreatToMeDecayRate; AST(NAME("DefaultThreatToMeDecayRate:"))

	Expression* threatToFriends;	AST(NAME("ThreatToFriends"), REDUNDANT_STRUCT("ThreatToFriends:", parse_Expression_StructParam), LATEBIND)
	F32 threatToFriendsMin;			AST(NAME("ThreatToFriendsMin:"))
	F32 threatToFriendsMax;			AST(NAME("ThreatToFriendsMax:"))
	F32 defaultThreatToFriendsFactor;AST(NAME("DefaultThreatToFriendsFactor:"))
	F32 defaultThreatToFriendsDecayRate; AST(NAME("DefaultThreatToFriendsDecayRate:"))

	Expression* leashDecayScale;	AST(NAME("LeashDecayScale"), REDUNDANT_STRUCT("LeashDecayScale:", parse_Expression_StructParam), LATEBIND)
	F32 leashDecayScaleMin;			AST(NAME("LeashDecayScaleMin:"))
	F32 leashDecayScaleMax;			AST(NAME("LeashDecayScaleMax:"))
	F32 defaultLeashDecayScaleMin;	AST(NAME("DefaultLeashDecayScaleMin:"))
	F32 defaultLeashDecayScaleMax;	AST(NAME("DefaultLeashDecayScaleMax:"))

	F32 defaultTargetStickinessFactor;	AST(REQUIRED, NAME("DefaultTargetStickinessFactor:"))
	char* filename; AST(CURRENTFILE)
} aggroExpressions;

AUTO_STRUCT;
typedef struct aggroExpressionList
{
	aggroExpressions** expressions;
	char* filename; AST(CURRENTFILE)
}aggroExpressionList;

static aggroExpressionList aggroExprList;
static aggroExpressions* gAggroExpr;

ExprContext* gAggroExprContext = NULL;

// ---------------------------------------------------------------------------------------------------------
void aiAggroExpressionSetupContexts()
{
	S32 i;
	const char *tags[] = { "util", "ai", "entity", "entityutil", "gameutil", "ai_powers", "ai_movement", "CEFuncsCharacter"};
	ExprFuncTable *pFuncTable;
	gAggroExprContext = exprContextCreate();	

	// Create the function table
	pFuncTable = exprContextCreateFunctionTable("AI_Aggro");
	for(i=0; i<ARRAY_SIZE(tags); i++)
		exprContextAddFuncsToTableByTag(pFuncTable, tags[i]);

	// Set the function table
	exprContextSetFuncTable(gAggroExprContext, pFuncTable);

	exprContextSetPointerVar(gAggroExprContext, "TargetStatus", NULL, parse_AIStatusTableEntry, true, true);
	exprContextSetPointerVar(gAggroExprContext, "Config", NULL, parse_AIConfig, false, true);
	exprContextSetPointerVar(gAggroExprContext, "Team", NULL, parse_AITeam, false, true);
	exprContextSetPointerVar(gAggroExprContext, "me", NULL, parse_Entity, true, false);
	exprContextSetPointerVar(gAggroExprContext, "AI", NULL, parse_AIVarsBase, false, true);
	exprContextSetPointerVar(gAggroExprContext, "AttrBasic", NULL, parse_CharacterAttribs, false, true);
	exprContextSetPointerVar(gAggroExprContext, "CombatRole", NULL, parse_AICombatRolesTeamRole, false, true);
	exprContextSetPointerVar(gAggroExprContext, "Guardee", NULL, parse_Entity, true, false);
	exprContextSetPointerVar(gAggroExprContext, "GuardeeAI", NULL, parse_AIVarsBase, false, true);


	exprContextSetIntVar(gAggroExprContext, "NumTeamMembers", 1);
	exprContextSetIntVar(gAggroExprContext, "NumLegalTargets", 1);

	// objpath doesn't know how to parse these, and expressions can't index themselves yet
	//exprContextAddStaticDefineIntAsVars(gAggroExprContext, AINotifyTypeEnum, NULL);

	exprContextSetFloatVar(gAggroExprContext, "HP", 0);
	exprContextSetFloatVar(gAggroExprContext, "HPMax", 0);
	exprContextSetFloatVar(gAggroExprContext, "Power", 0);
	exprContextSetFloatVar(gAggroExprContext, "PowerMax", 0);
	exprContextSetFloatVar(gAggroExprContext, "TeamHP", 0);
	exprContextSetFloatVar(gAggroExprContext, "TeamHPMax", 0);

	exprContextSetAllowRuntimeSelfPtr(gAggroExprContext);
	exprContextSetAllowRuntimePartition(gAggroExprContext);
}

AUTO_FIXUPFUNC;
TextParserResult fixupaggroDefExpressions(AIAggroDef * pAIAggroDef, enumTextParserFixupType eType, void *pExtraData)
{
	int success = true;

	devassert(pAIAggroDef);

	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			if (pAIAggroDef)
			{
				aiTargetingExprVarsAdd(NULL, NULL, gAggroExprContext, NULL);

				// Generate expressions for each counter
				FOR_EACH_IN_EARRAY(pAIAggroDef->aggroCounters, AIAggroCounterDef, pCounterDef)
				{
					if (pCounterDef && pCounterDef->expr)
					{						
						success &= exprGenerate(pCounterDef->expr, gAggroExprContext);
					}
					if (pCounterDef && pCounterDef->pExprDecayRate)
					{						
						success &= exprGenerate(pCounterDef->pExprDecayRate, gAggroExprContext);
					}
				}
				FOR_EACH_END

					// Generate expressions for each gauge
				FOR_EACH_IN_EARRAY(pAIAggroDef->aggroGauges, AIAggroGaugeDef, pGaugeDef)
				{
					if (pGaugeDef && pGaugeDef->expr)
					{
						success &= exprGenerate(pGaugeDef->expr, gAggroExprContext);						
					}
				}
				FOR_EACH_END

				aiTargetingExprVarsRemove(NULL, NULL, gAggroExprContext);
			}

			break;
		}
	}

	return success;
}

AUTO_FIXUPFUNC;
TextParserResult fixupaggroExpressions(aggroExpressions* aggroExpr, enumTextParserFixupType eType, void *pExtraData)
{
	int success = true;

	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ:
		{
			// TODO fixup text files to use new format and make this always generate
			if(aiGlobalSettings.useAggroExprFile)
			{
				aiTargetingExprVarsAdd(NULL, NULL, gAggroExprContext, NULL);
				if(aggroExpr->damageToMe)
					success &= exprGenerate(aggroExpr->damageToMe, gAggroExprContext);
				if(aggroExpr->damageToFriends)
					success &= exprGenerate(aggroExpr->damageToFriends, gAggroExprContext);
				if(aggroExpr->distFromMe)
					success &= exprGenerate(aggroExpr->distFromMe, gAggroExprContext);
				if(aggroExpr->distFromGuardPoint)
					success &= exprGenerate(aggroExpr->distFromGuardPoint, gAggroExprContext);
				if(aggroExpr->personalSpace)
					success &= exprGenerate(aggroExpr->personalSpace, gAggroExprContext);
				if(aggroExpr->statusToMe)
					success &= exprGenerate(aggroExpr->statusToMe, gAggroExprContext);
				if(aggroExpr->statusToFriends)
					success &= exprGenerate(aggroExpr->statusToFriends, gAggroExprContext);
				if(aggroExpr->healingEnemies)
					success &= exprGenerate(aggroExpr->healingEnemies, gAggroExprContext);
				if(aggroExpr->threatToMe)
					success &= exprGenerate(aggroExpr->threatToMe, gAggroExprContext);
				if(aggroExpr->threatToFriends)
					success &= exprGenerate(aggroExpr->threatToFriends, gAggroExprContext);
				if(aggroExpr->teamOrders)
					success &= exprGenerate(aggroExpr->teamOrders, gAggroExprContext);
				if(aggroExpr->leashDecayScale)
					success &= exprGenerate(aggroExpr->leashDecayScale, gAggroExprContext);
				aiTargetingExprVarsRemove(NULL, NULL, gAggroExprContext);
			}
		}
	}

	return success;
}

// ---------------------------------------------------------------------------------------------------------
static void aiAggroExpressionReload(const char *path, int UNUSED_when)
{
	loadstart_printf("Reloading Aggro Expressions...");
	fileWaitForExclusiveAccess(path);
	errorLogFileIsBeingReloaded(path);
	if(!ParserReloadFile(path, parse_aggroExpressionList, &aggroExprList, NULL, 0))
		Errorf("Error reloading aggro expressions");

	gAggroExpr = aggroExprList.expressions[0];

	if(eaSize(&aggroExprList.expressions) != 1)
		ErrorFilenamef(aggroExprList.filename, "Only allowed to specify one aggro expressions block");

	aiConfigReloadAll();

	loadend_printf(" done");
}



// ---------------------------------------------------------------------------------------------------------
int aiAggro_ShouldScaleHealAggroByLegalTargets()
{
	if (aiGlobalSettings.useAggro2) 
	{
		if (g_pDefaultAggro2Def) 
			return g_pDefaultAggro2Def->scaleHealingByLegalTargets;
	}
	else
	{
		return false;
	}

	return false;
}

// ---------------------------------------------------------------------------------------------------------
int aiAggro_ShouldSeperateHealingByAttackTarget()
{
	if (aiGlobalSettings.useAggro2) 
	{
		if (g_pDefaultAggro2Def) 
			return g_pDefaultAggro2Def->separateHealingByAttackTarget;
	}
	else
	{
		return false;
	}

	return false;
}

// ---------------------------------------------------------------------------------------------------------
int aiAggro_ShouldIgnoreHealing(S32 iPartitionIdx, AIVarsBase* aib)
{
	if (aiGlobalSettings.useAggro2) 
	{
		if (g_pDefaultAggro2Def && g_pDefaultAggro2Def->fTimeSinceSpawnToIgnoreHealing)
		{
			return ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->time.timeSpawned) < 
					SEC_TO_ABS_TIME(g_pDefaultAggro2Def->fTimeSinceSpawnToIgnoreHealing);
		}
	}
	else
	{
		return false;
	}

	return false;
}


// ---------------------------------------------------------------------------------------------------------
static void aiAggro2_Reload(const char *path, int UNUSED_when)
{
	loadstart_printf("Reloading Aggro Expressions...");
	fileWaitForExclusiveAccess(path);
	errorLogFileIsBeingReloaded(path);
	
	ParserReloadFileToDictionary(path, gAIAggroDefDict);

	//if(!ParserReloadFile(path, parse_AIAggroDef, g_pDefaultAggro2Def, NULL, 0))
		//Errorf("Error reloading aggro expressions");
	
	g_pDefaultAggro2Def = RefSystem_ReferentFromString(gAIAggroDefDict, "Default");

	aiConfigReloadAll();

	loadend_printf(" done");
}

// ---------------------------------------------------------------------------------------------------------
static int aiAggroDefValidate(enumResourceValidateType eType, const char* pDictName, 
							   const char* pResourceName, void* pResource, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			// rrp TODO: validate no duplicate bucket names
			//resAddValueDep("AIAggroDefLoadingVersion");
			//TODO wtf calls which callback? :)
			if(!stricmp("Default", pResourceName))
				g_pDefaultAggro2Def = (AIAggroDef*)pResource;
			return VALIDATE_HANDLED;

		xcase RESVALIDATE_FINAL_LOCATION:
		{
				AIAggroDef *aggroDef = (AIAggroDef*)pResource;
				FOR_EACH_IN_EARRAY(aggroDef->aggroCounters, AIAggroCounterDef, pCounter)
				{
					if (pCounter->pExprDecayRate)
					{
						aggroDef->hasDecay = true;
						break;
					}
				}
				FOR_EACH_END
			}
			if(!stricmp("Default", pResourceName))
				g_pDefaultAggro2Def = (AIAggroDef*)pResource;
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

// ---------------------------------------------------------------------------------------------------------
void aiAggroExpressionLoad(void)
{
	loadstart_printf("Loading AI Aggro Expressions.. ");
	ParserLoadFiles(NULL, "ai/AggroExpressions.def", "AggroExpressions.bin",
					PARSER_OPTIONALFLAG, parse_aggroExpressionList, &aggroExprList);

	if(eaSize(&aggroExprList.expressions))
	{
		gAggroExpr = aggroExprList.expressions[0];
	}

	if(eaSize(&aggroExprList.expressions) != 1)
		ErrorFilenamef(aggroExprList.filename, "Only allowed to specify one aggro expressions block");

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/AggroExpressions.def", aiAggroExpressionReload);


	// Aggro 2
	{
		gAIAggroDefDict = RefSystem_RegisterSelfDefiningDictionary("AIAggroDef", 
																	false, 
																	parse_AIAggroDef, 
																	true, 
																	true, 
																	NULL);

		//ParserBinRegisterDepValue("AIAggroDefLoadingVersion", 1);

		resDictManageValidation(gAIAggroDefDict, aiAggroDefValidate);

		resLoadResourcesFromDisk(	gAIAggroDefDict, 
									"ai/aggro", ".aggro", "aiAggro2.bin", 
									PARSER_SERVERSIDE | PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

		//FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/AIAggro2Settings.def", aiAggro2_Reload);
		
		g_pDefaultAggro2Def = RefSystem_ReferentFromString(gAIAggroDefDict, "Default");

		if(aiGlobalSettings.useAggro2 && !g_pDefaultAggro2Def)
			FatalErrorf("Can't run with useAggro2 enabled in aiGlobalSettings.txt and no valid AIAggro2Settings.def");
	}
	

	loadend_printf("done.");
}

// ---------------------------------------------------------------------------------------------------------
void aiFillInDefaultDangerFactors(AIConfig* config)
{
	if(!gAggroExpr)
	{
		return;
	}

	config->dangerFactors.damageToMe = gAggroExpr->defaultDamageToMeFactor;
	config->dangerFactors.damageToFriends = gAggroExpr->defaultDamageToFriendsFactor;
	config->dangerFactors.distFromMe = gAggroExpr->defaultDistFromMeFactor;
	config->dangerFactors.personalSpace = gAggroExpr->defaultPersonalSpaceFactor;
	config->dangerFactors.distFromGuardPoint = gAggroExpr->defaultDistFromGuardPointFactor;
	config->dangerFactors.statusToMe = gAggroExpr->defaultStatusToMeFactor;
	config->dangerFactors.statusToFriends = gAggroExpr->defaultStatusToFriendsFactor;
	config->dangerFactors.healingEnemies = gAggroExpr->defaultHealingEnemiesFactor;
	config->dangerFactors.targetStickiness = gAggroExpr->defaultTargetStickinessFactor;
	config->dangerFactors.teamOrders = gAggroExpr->defaultTeamOrdersFactor;
	config->dangerFactors.threatToMe = gAggroExpr->defaultThreatToMeFactor;
	config->dangerFactors.threatToFriends = gAggroExpr->defaultThreatToFriendsFactor;

	config->dangerScalars.leashDecayScaleMin = gAggroExpr->defaultLeashDecayScaleMin;
	config->dangerScalars.leashDecayScaleMax = gAggroExpr->defaultLeashDecayScaleMax;
}

// ---------------------------------------------------------------------------------------------------------
static __forceinline F32 normalizeF32(F32 src, F32 min, F32 max)
{
	F32 ret = (src - min) / (max - min);

	return CLAMPF32(ret, 0, 1);
}

// ---------------------------------------------------------------------------------------------------------
static __forceinline F32 scaleF32(F32 src, F32 min, F32 max)
{
	return min + src * max;
}

// ---------------------------------------------------------------------------------------------------------
static F32 aiGetMaxAggro(AIConfig* config)
{
	F32 maxAggro = 0;

	maxAggro += config->dangerFactors.damageToMe;
	maxAggro += config->dangerFactors.damageToFriends;
	maxAggro += config->dangerFactors.personalSpace;
	maxAggro += config->dangerFactors.distFromGuardPoint;
	maxAggro += config->dangerFactors.statusToMe;
	maxAggro += config->dangerFactors.statusToFriends;
	maxAggro += config->dangerFactors.healingEnemies;
	maxAggro += config->dangerFactors.targetStickiness;
	maxAggro += config->dangerFactors.teamOrders;
	maxAggro += config->dangerFactors.targetingRatingExpr;
	maxAggro += config->dangerFactors.threatToMe;
	maxAggro += config->dangerFactors.threatToFriends;

	if(gAggroExpr->leashDecayScale)
		maxAggro *= config->dangerScalars.leashDecayScaleMax;

	return maxAggro;
}

// ---------------------------------------------------------------------------------------------------------
void aiTargetingExprVarsAdd(Entity* e, AIVarsBase* aib, ExprContext* context, Entity* target)
{
	exprContextSetPointerVarPooledCached(context, targetEntString, target, parse_Entity, true, true, &targetEntVarHandle);
}

// ---------------------------------------------------------------------------------------------------------
void aiTargetingExprVarsRemove(Entity* be, AIVarsBase* aib, ExprContext* context)
{
	exprContextRemoveVarPooled(context, targetEntString);
}

// ---------------------------------------------------------------------------------------------------------
ExprContext* aiAggroPrepareExprContext(Entity* e, AIVarsBase* aib, AIConfig* config, 
									   AIStatusTableEntry* status, int iNumLegalTargets)
{
	int i, n;
	F32 totalTeamHP = 0;
	F32 totalTeamHPMax = 0;

	PERFINFO_AUTO_START_FUNC();

	exprContextSetPointerVar(gAggroExprContext, "TargetStatus", status, parse_AIStatusTableEntry, true, true);
	exprContextSetPointerVar(gAggroExprContext, "Config", config, parse_AIConfig, false, true);
	exprContextSetPointerVar(gAggroExprContext, "Team", aib->team, parse_AITeam, false, true);
	if(aiGlobalSettings.enableCombatTeams && aib->combatTeam)
		exprContextSetPointerVar(gAggroExprContext, "CombatTeam", aib->combatTeam, parse_AITeam, false, true);
	exprContextSetPointerVar(gAggroExprContext, "AI", aib, parse_AIVarsBase, false, true);
	exprContextSetPointerVar(gAggroExprContext, "me", e, parse_Entity, true, false);
	exprContextSetSelfPtr(gAggroExprContext, e);
	exprContextSetPointerVar(gAggroExprContext, "AttrBasic", e->pChar->pattrBasic, parse_CharacterAttribs, false, true);

	exprContextSetIntVar(gAggroExprContext, "NumTeamMembers", eaSize(&aib->team->members));
	if(aiGlobalSettings.enableCombatTeams && aib->combatTeam)
		exprContextSetIntVar(gAggroExprContext, "NumCombatTeamMembers", eaSize(&aib->combatTeam->members));
	exprContextSetIntVar(gAggroExprContext, "NumLegalTargets", iNumLegalTargets);

	// setup the CombatRole variable
	{
		static AICombatRolesTeamRole s_dummyRole = {0};
		AICombatRolesTeamRole *pRole;
		AITeamMember *pMember = aiGetTeamMember(e, aib);

		if (pMember->pCombatRole)
		{
			pRole = pMember->pCombatRole->pTeamRole;
			devassert(pMember->pCombatRole->pTeamRole);
		}
		else
		{
			pRole = &s_dummyRole;
		}

		exprContextSetPointerVar(gAggroExprContext, "CombatRole", pRole, parse_AICombatRolesTeamRole, false, true);
	}

	// setup the guardeeAI variable
	{
		static AIVarsBase s_dummyAIB = {0};
		AIVarsBase *pGuardeeAIB;
		Entity *guardEnt = aiGetLeashEntity(e, aib);

		if (guardEnt)
		{
			pGuardeeAIB = guardEnt->aibase;
		}
		else
		{
			pGuardeeAIB = &s_dummyAIB;
		}

		exprContextSetPointerVar(gAggroExprContext, "guardee", guardEnt, parse_Entity, true, false);
		exprContextSetPointerVar(gAggroExprContext, "guardeeAI", pGuardeeAIB, parse_AIVarsBase, false, true);
	}

	// add own max hp
	exprContextSetFloatVar(gAggroExprContext, "HP", e->pChar->pattrBasic->fHitPoints);
	exprContextSetFloatVar(gAggroExprContext, "HPMax", e->pChar->pattrBasic->fHitPointsMax);
	// add own power
	exprContextSetFloatVar(gAggroExprContext, "Power", e->pChar->pattrBasic->fPower);
	exprContextSetFloatVar(gAggroExprContext, "PowerMax", e->pChar->pattrBasic->fPowerMax);
	// add team's max hp

	n = eaSize(&aib->team->members);

	for(i = 0; i < n; i++)
	{
		Entity* memberBE = aib->team->members[i]->memberBE;
		if(memberBE->pChar)
		{
			totalTeamHP += memberBE->pChar->pattrBasic->fHitPoints;
			totalTeamHPMax += memberBE->pChar->pattrBasic->fHitPointsMax;
		}
	}

	exprContextSetFloatVar(gAggroExprContext, "TeamHP", totalTeamHP);
	exprContextSetFloatVar(gAggroExprContext, "TeamHPMax", totalTeamHPMax);

	e->aibase->team->teamTotalHealth = totalTeamHPMax;

	PERFINFO_AUTO_STOP();

	return gAggroExprContext;
}

// ---------------------------------------------------------------------------------------------------------
int aiEnableAggroAvgBotheredModel = false;
AUTO_CMD_INT(aiEnableAggroAvgBotheredModel, aiEnableAggroAvgBotheredModel);

//TODO reenable
//AUTO_CMD_INT(aiGlobalSettings.useAggroExprFile, aiUseAggroExprFile);

// ---------------------------------------------------------------------------------------------------------
static void aiProcessAggroExpressions(Entity* e, AIVarsBase* aib, AIConfig* config, 
									  AIStatusTableEntry* status, int iNumLegalTargets)
{
	MultiVal answer;
	Entity *pTargetEnt = NULL;

	aiAggroPrepareExprContext(e, aib, config, status, iNumLegalTargets);

	PERFINFO_AUTO_START("eval expressions", 1);

	if (status)
	{
		pTargetEnt = entFromEntityRef(entGetPartitionIdx(e), status->entRef);
	}

	aiTargetingExprVarsAdd(e, aib, gAggroExprContext, pTargetEnt);
	exprContextSetPartition(gAggroExprContext, entGetPartitionIdx(e));

	exprEvaluate(gAggroExpr->distFromMe, gAggroExprContext, &answer);
	status->dangerVal.distFromMeVal = QuickGetFloat(&answer);

	if(gAggroExpr->personalSpace)
	{
		exprEvaluate(gAggroExpr->personalSpace, gAggroExprContext, &answer);
		status->dangerVal.personalSpaceVal = QuickGetFloat(&answer);
	}

	exprEvaluate(gAggroExpr->distFromGuardPoint, gAggroExprContext, &answer);
	status->dangerVal.distFromGuardPointVal = QuickGetFloat(&answer);

	exprEvaluate(gAggroExpr->damageToMe, gAggroExprContext, &answer);
	status->dangerVal.damageToMeVal = QuickGetFloat(&answer);

	exprEvaluate(gAggroExpr->damageToFriends, gAggroExprContext, &answer);
	status->dangerVal.damageToFriendsVal = QuickGetFloat(&answer);

	if(gAggroExpr->statusToMe)
	{
		exprEvaluate(gAggroExpr->statusToMe, gAggroExprContext, &answer);
		status->dangerVal.statusToMeVal = QuickGetFloat(&answer);
	}

	if(gAggroExpr->statusToFriends)
	{
		exprEvaluate(gAggroExpr->statusToFriends, gAggroExprContext, &answer);
		status->dangerVal.statusToFriendsVal = QuickGetFloat(&answer);
	}

	if(gAggroExpr->healingEnemies)
	{
		exprEvaluate(gAggroExpr->healingEnemies, gAggroExprContext, &answer);
		status->dangerVal.healingEnemiesVal = QuickGetFloat(&answer);
	}

	if(gAggroExpr->threatToMe)
	{
		exprEvaluate(gAggroExpr->threatToMe, gAggroExprContext, &answer);
		status->dangerVal.threatToMeVal = QuickGetFloat(&answer);
	}

	if(gAggroExpr->threatToFriends)
	{
		exprEvaluate(gAggroExpr->threatToFriends, gAggroExprContext, &answer);
		status->dangerVal.threatToFriendsVal = QuickGetFloat(&answer);
	}

	if(gAggroExpr->teamOrders)
	{
		exprEvaluate(gAggroExpr->teamOrders, gAggroExprContext, &answer);
		status->dangerVal.teamOrdersVal = QuickGetFloat(&answer);
	}

	if(gAggroExpr->leashDecayScale)
	{
		exprEvaluate(gAggroExpr->leashDecayScale, gAggroExprContext, &answer);
		status->dangerVal.leashDecayScaleVal = QuickGetFloat(&answer);
	}

	aiTargetingExprVarsRemove(e, aib, gAggroExprContext);
	exprContextClearPartition(gAggroExprContext);
	PERFINFO_AUTO_STOP();
}

// ---------------------------------------------------------------------------------------------------------
static void aiCheckTargetRating(Entity* e, AIVarsBase* aib, AIConfig* config, 
								AIStatusTableEntry* status, Entity* statusBE)
{
	MultiVal answer;

	PERFINFO_AUTO_START_FUNC();

	aiTargetingExprVarsAdd(e, aib, aib->exprContext, statusBE);
	exprEvaluate(config->targetingRating, aib->exprContext, &answer);
	aiTargetingExprVarsRemove(e, aib, aib->exprContext);

	status->dangerVal.targetingRatingExprVal = QuickGetFloat(&answer);
	if(config->targetingRatingMin || config->targetingRatingMax)
	{
		status->dangerVal.targetingRatingExprVal = normalizeF32(status->dangerVal.targetingRatingExprVal,
			config->targetingRatingMin, config->targetingRatingMax);
	}
	status->dangerVal.targetingRatingExprVal *= config->dangerFactors.targetingRatingExpr;
	status->totalBaseDangerVal += status->dangerVal.targetingRatingExprVal;
	PERFINFO_AUTO_STOP();
}

// ---------------------------------------------------------------------------------------------------------
int aiEvalTargetingRequires(Entity *e, AIVarsBase *aib, AIConfig *config, Entity *target)
{
	MultiVal answer;

	if(!config->targetingRequires)
		return 1;

	PERFINFO_AUTO_START("targetingRequires", 1);
	aiTargetingExprVarsAdd(e, aib, aib->exprContext, target);
	exprEvaluate(config->targetingRequires, aib->exprContext, &answer);
	aiTargetingExprVarsRemove(e, aib, aib->exprContext);
	PERFINFO_AUTO_STOP();

	return QuickGetInt(&answer);
}

// ---------------------------------------------------------------------------------------------------------
void aiCalculateAggro1Danger(Entity* e, AIVarsBase* aib, AIConfig* config, AITeam* combatTeam, 
							 Entity* statusBE, AIStatusTableEntry* status, int numLegalTargets)
{
	AITeamMember *combatMember = aiTeamGetMember(e, aib, combatTeam);

	if(config->targetingRating && config->dangerFactors.targetingRatingExpr)
		aiCheckTargetRating(e, aib, config, status, statusBE);

	if(aiGlobalSettings.useAggroExprFile)
		aiProcessAggroExpressions(e, aib, config, status, numLegalTargets);
	else
	{
		F32 totalDamageToMe = aib->totalTrackedDamage[AI_NOTIFY_TYPE_DAMAGE] + aib->totalTrackedDamage[AI_NOTIFY_TYPE_THREAT] + aib->totalTrackedDamage[AI_NOTIFY_TYPE_STATUS];
		F32 avgDamagePerLegalTarget;
		F32 totalTeamDamage = combatTeam->trackedDamageTeam[AI_NOTIFY_TYPE_DAMAGE] + combatTeam->trackedDamageTeam[AI_NOTIFY_TYPE_THREAT] + combatTeam->trackedDamageTeam[AI_NOTIFY_TYPE_STATUS];
		F32 avgTeamDamagePerLegalTarget;

		totalDamageToMe = MAX(totalDamageToMe, 1);
		totalTeamDamage = MAX(totalTeamDamage, 1);

		avgDamagePerLegalTarget = totalDamageToMe / numLegalTargets;
		avgDamagePerLegalTarget = MAX(avgDamagePerLegalTarget, 1);

		avgTeamDamagePerLegalTarget = totalTeamDamage / numLegalTargets;
		avgTeamDamagePerLegalTarget = MAX(avgTeamDamagePerLegalTarget, 1);

		if (!aiGlobalSettings.useCombatRoleDamageSharing || !combatMember->pCombatRole)
		{
			status->dangerVal.damageToFriendsVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_DAMAGE] / avgTeamDamagePerLegalTarget;
			status->dangerVal.threatToFriendsVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_THREAT] / avgTeamDamagePerLegalTarget;
		}
		else
		{	// using the combat role damage pools to normalize the trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS] 
			AICombatRolesTeamRole *pTeamRole = combatMember->pCombatRole->pTeamRole;
			F32 avgRoleDamagePerLegalTarget = pTeamRole->trackedDamageRole[AI_NOTIFY_TYPE_DAMAGE] + 
				pTeamRole->trackedDamageRole[AI_NOTIFY_TYPE_THREAT] +
				pTeamRole->trackedDamageRole[AI_NOTIFY_TYPE_STATUS];

			avgRoleDamagePerLegalTarget = avgRoleDamagePerLegalTarget / numLegalTargets;
			avgRoleDamagePerLegalTarget = MAX(avgRoleDamagePerLegalTarget, 1);

			status->dangerVal.damageToFriendsVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_DAMAGE] / avgRoleDamagePerLegalTarget;
			status->dangerVal.threatToFriendsVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_THREAT] / avgRoleDamagePerLegalTarget;
		}

		status->dangerVal.distFromMeVal = (aib->aggroRadius - status->distanceFromMe) / aib->aggroRadius;
		status->dangerVal.personalSpaceVal = (status->distanceFromMe < 7.5f);
		status->dangerVal.distFromGuardPointVal = (config->combatMaxProtectDist - status->distanceFromSpawnPos) / config->combatMaxProtectDist;
		status->dangerVal.damageToMeVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_DAMAGE] / avgDamagePerLegalTarget;

		status->dangerVal.statusToMeVal = 0;
		status->dangerVal.statusToFriendsVal = 0;
		status->dangerVal.threatToMeVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_THREAT] / avgDamagePerLegalTarget;
		status->dangerVal.healingEnemiesVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_HEALING] * 0.25 / avgDamagePerLegalTarget;

		status->dangerVal.teamOrdersVal = status->inFrontArc && status->isAssignedTarget;
	}

	status->dangerVal.distFromMeVal = normalizeF32(status->dangerVal.distFromMeVal,
		gAggroExpr->distFromMeMin, gAggroExpr->distFromMeMax);
	status->dangerVal.personalSpaceVal = normalizeF32(status->dangerVal.personalSpaceVal,
		gAggroExpr->personalSpaceMin, gAggroExpr->personalSpaceMax);
	status->dangerVal.distFromGuardPointVal = normalizeF32(status->dangerVal.distFromGuardPointVal,
		gAggroExpr->distFromGuardPointMin, gAggroExpr->distFromGuardPointMax);
	status->dangerVal.damageToMeVal = normalizeF32(status->dangerVal.damageToMeVal,
		gAggroExpr->damageToMeMin, gAggroExpr->damageToMeMax);
	status->dangerVal.damageToFriendsVal = normalizeF32(status->dangerVal.damageToFriendsVal,
		gAggroExpr->damageToFriendsMin, gAggroExpr->damageToFriendsMax);
	status->dangerVal.statusToMeVal = normalizeF32(status->dangerVal.statusToMeVal,
		gAggroExpr->statusToMeMin, gAggroExpr->statusToMeMax);
	status->dangerVal.statusToFriendsVal = normalizeF32(status->dangerVal.statusToFriendsVal,
		gAggroExpr->statusToFriendsMin, gAggroExpr->statusToFriendsMax);
	status->dangerVal.healingEnemiesVal = normalizeF32(status->dangerVal.healingEnemiesVal,
		gAggroExpr->healingEnemiesMin, gAggroExpr->healingEnemiesMax);
	status->dangerVal.teamOrdersVal = normalizeF32(status->dangerVal.teamOrdersVal,
		gAggroExpr->teamOrdersMin, gAggroExpr->teamOrdersMax);
	status->dangerVal.threatToMeVal = normalizeF32(status->dangerVal.threatToMeVal,
		gAggroExpr->threatToMeMin, gAggroExpr->threatToMeMax);
	status->dangerVal.threatToFriendsVal = normalizeF32(status->dangerVal.threatToFriendsVal,
		gAggroExpr->threatToFriendsMin, gAggroExpr->threatToFriendsMax);

	status->dangerVal.distFromMeVal *= config->dangerFactors.distFromMe;
	status->dangerVal.personalSpaceVal *= config->dangerFactors.personalSpace;
	status->dangerVal.distFromGuardPointVal *= config->dangerFactors.distFromGuardPoint;
	status->dangerVal.damageToMeVal *= config->dangerFactors.damageToMe;
	status->dangerVal.damageToFriendsVal *= config->dangerFactors.damageToFriends;
	status->dangerVal.statusToMeVal *= config->dangerFactors.statusToMe;
	status->dangerVal.statusToFriendsVal *= config->dangerFactors.statusToFriends;
	status->dangerVal.healingEnemiesVal *= config->dangerFactors.healingEnemies;
	if(statusBE == aib->attackTarget)
		status->dangerVal.targetStickinessVal = config->dangerFactors.targetStickiness;
	else
		status->dangerVal.targetStickinessVal = 0;
	status->dangerVal.teamOrdersVal *= config->dangerFactors.teamOrders;
	status->dangerVal.threatToMeVal *= config->dangerFactors.threatToMe;
	status->dangerVal.threatToFriendsVal *= config->dangerFactors.threatToFriends;

	status->totalBaseDangerVal += status->dangerVal.damageToMeVal;
	status->totalBaseDangerVal += status->dangerVal.damageToFriendsVal;
	status->totalBaseDangerVal += status->dangerVal.distFromGuardPointVal;
	status->totalBaseDangerVal += status->dangerVal.distFromMeVal;
	status->totalBaseDangerVal += status->dangerVal.personalSpaceVal;
	status->totalBaseDangerVal += status->dangerVal.statusToMeVal;
	status->totalBaseDangerVal += status->dangerVal.statusToFriendsVal;
	status->totalBaseDangerVal += status->dangerVal.healingEnemiesVal;
	status->totalBaseDangerVal += status->dangerVal.targetStickinessVal;
	status->totalBaseDangerVal += status->dangerVal.teamOrdersVal;
	status->totalBaseDangerVal += status->dangerVal.threatToMeVal;
	status->totalBaseDangerVal += status->dangerVal.threatToFriendsVal;

	if(gAggroExpr->leashDecayScale)
	{
		status->dangerVal.leashDecayScaleVal = scaleF32(status->dangerVal.leashDecayScaleVal,
			config->dangerScalars.leashDecayScaleMin, config->dangerScalars.leashDecayScaleMax);
		status->dangerVal.leashDecayScaleVal = normalizeF32(status->dangerVal.leashDecayScaleVal,
			gAggroExpr->leashDecayScaleMin, gAggroExpr->leashDecayScaleMax);
		status->totalBaseDangerVal *= status->dangerVal.leashDecayScaleVal;
	}
}


// ---------------------------------------------------------------------------------------------------------
static void SetTotalDangerValForCharTargetInfo(Entity* e, AIStatusTableEntry* status)
{
	
}

// ---------------------------------------------------------------------------------------------------------
// Scales the total base danger value by any AIAggroTotalScale attrib mods
void aiAggro2_ProcessAIAggroTotalScaleMods(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID AIStatusTableEntry* status)
{
	F32 fTotalScaleValue = 1.f;

	// Go through all AI aggro attrib mods in the entity
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->ppModsAIAggro, AttribMod, pMod)
	{
		// For now don't check the attrib type because only AIAggroTotalScale mods are stuffed in this array
		AIAggroTotalScaleParams *pParams = (AIAggroTotalScaleParams *)pMod->pDef->pParams;
		if (pParams)
		{
			Entity *pModSourceEnt = entFromEntityRefAnyPartition(pMod->erSource);

			Entity *pModApplyEnt;

			if (pParams->eApplyType == kAIAggroTotalScaleApplyType_Owner)
			{
				pModApplyEnt = pModSourceEnt && pModSourceEnt->erOwner ? 
					entFromEntityRefAnyPartition(pModSourceEnt->erOwner) : pModSourceEnt;
			}
			else
			{
				pModApplyEnt = pModSourceEnt;
			}

			if (pModApplyEnt && entGetRef(pModApplyEnt) == status->entRef)
			{
				fTotalScaleValue *= pMod->fMagnitude;
			}
		}
	}
	FOR_EACH_END

	// Set the final total base danger value
	status->totalBaseDangerVal *= fTotalScaleValue;
}

// ---------------------------------------------------------------------------------------------------------
void aiAssignDangerValues(Entity* e, AIVarsBase* aib, F32 baseDangerFactor, bool bSendAggro)
{
	int i, numStatusTable = eaSize(&aib->statusTable);
	char* reason = "";
	F32 maxTotalDangerVal = 0;
	AIConfig* config = NULL;
	F32 maxAggro = 0;
	F32 botheredAvgAggro = 0;
	AITeam* combatTeam = NULL;
	AITeamMember *combatMember = NULL;
	AIAggroDef *pAggroDef = NULL;
	S32 iNumLegalTargets = 0;
	F32 maxTotalCounter = 0.f;
	int iPartitionIdx = -1;
	PerfInfoGuard *guard;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	config = aiGetConfig(e, aib);
	maxAggro = aiGetMaxAggro(config);
	combatTeam = aiTeamGetCombatTeam(e, aib);
	combatMember = aiTeamGetMember(e, aib, combatTeam);
	pAggroDef = aiAggro2_GetAggroDef(e, config);
	iPartitionIdx = entGetPartitionIdx(e);

	for(i = 0; i < numStatusTable; i++)
	{
		AIStatusTableEntry* status = aib->statusTable[i];

		if(!status->skipCurTick)
		{
			AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, status);
			if(teamStatus)
				iNumLegalTargets += teamStatus->legalTarget;
		}
	}

	for(i = 0; i < numStatusTable; i++)
	{
		AIStatusTableEntry* status = aib->statusTable[i];
		Entity* statusBE = entFromEntityRef(iPartitionIdx, status->entRef);
		AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, status);

		ZeroStruct(&status->dangerVal);
		status->totalBaseDangerVal = 0.f;
		status->isAssignedTarget = (combatMember->assignedTarget == status->entRef);
		status->assignedValues = false;

		if(status->skipCurTick || !teamStatus || !teamStatus->legalTarget)
			continue;

		if(!statusBE || entGetFlagBits(statusBE) & ENTITYFLAG_UNTARGETABLE)
			continue;

		if(!critter_IsKOS(iPartitionIdx, e, statusBE))
			continue;

		if(status->inAvoidList)
			continue;

		status->assignedValues = true;

		if(aiGlobalSettings.useAggro2)
		{
			aiAggro2_ProcessCounters(e, pAggroDef, config, status, iNumLegalTargets);
			if (status->aggroCounterTotal > maxTotalCounter)
				maxTotalCounter = status->aggroCounterTotal;
		}
		else
		{
			aiCalculateAggro1Danger(e, aib, config, combatTeam, statusBE, status, iNumLegalTargets);
			MAX1(maxTotalDangerVal, status->totalBaseDangerVal);
		}

	}


	for(i = 0; i < numStatusTable; i++)
	{
		AIStatusTableEntry* status = aib->statusTable[i];

		if (!status->assignedValues)
			continue;

		if(aiGlobalSettings.useAggro2)
		{
			// for aggro2, take a second pass to calculate gauges
			Entity* statusBE = entFromEntityRef(iPartitionIdx, status->entRef);
			aiAggro2_ProcessGauges(e, pAggroDef, config, status, iNumLegalTargets, maxTotalCounter);
		}

		// Scale the total aggro by AIAggroTotalScale attrib mods
		aiAggro2_ProcessAIAggroTotalScaleMods(e, status);
		
		MAX1(maxTotalDangerVal, status->totalBaseDangerVal);
	}

	if (bSendAggro && gConf.bClientDangerData)
	{
		int j, numCharTargets;

		if(!maxTotalDangerVal)
			maxTotalDangerVal = 1;

		numCharTargets = eaSize(&e->pChar->ppAITargets);

		// go through all the status entries and set the character AI target info's totalBaseDangerVal 

		for(i = 0; i < numStatusTable; i++)
		{
			AIStatusTableEntry* status = aib->statusTable[i];
			
			// find the CharacterAITargetInfo for this status entry
			for (j = numCharTargets - 1; j >= 0; j--)
			{
				CharacterAITargetInfo *pInfo = e->pChar->ppAITargets[j];
				if (pInfo->entRef == status->entRef)
				{
					pInfo->totalBaseDangerVal = status->totalBaseDangerVal;
					// calculate the normalized danger value for all the CharacterAITargetInfo
					pInfo->relativeDangerVal = pInfo->totalBaseDangerVal / maxTotalDangerVal;			
					break;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

// ---------------------------------------------------------------------------------------------------------
void aiAggroDecay(Entity* e, AIVarsBase* aib, AITeam* team, AICombatRolesTeamRole* teamRole, AIStatusTableEntry* status, int useRoleDamageSharing)
{
	if (aiGlobalSettings.useAggro2)
		return; // disabled in aggro2

	if(gAggroExpr->defaultDamageToMeDecayRate)
	{
		float diff = MIN(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_DAMAGE], gAggroExpr->defaultDamageToMeDecayRate);
		status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_DAMAGE] -= gAggroExpr->defaultDamageToMeDecayRate;
		MAX1(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_DAMAGE], 0);

		aib->totalTrackedDamage[AI_NOTIFY_TYPE_DAMAGE] -= diff;
		MAX1(aib->totalTrackedDamage[AI_NOTIFY_TYPE_DAMAGE], 0);

		team->trackedDamageTeam[AI_NOTIFY_TYPE_DAMAGE] -= diff;
		MAX1(team->trackedDamageTeam[AI_NOTIFY_TYPE_DAMAGE], 0);
	}
	if(gAggroExpr->defaultDamageToFriendsDecayRate)
	{
		float diff = MIN(status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_DAMAGE], gAggroExpr->defaultDamageToFriendsDecayRate);
		status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_DAMAGE] -= gAggroExpr->defaultDamageToFriendsDecayRate;
		MAX1(status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_DAMAGE], 0);

		// I'm not quite sure if this should be decaying here, per member or if this should be decaying
		// once per aiTeam tick. See below in any of the other '*ToFriendsDecayRate' blocks
		// todo: come back and confirm this is correct (lol decay)
		if(useRoleDamageSharing)
		{
			teamRole->trackedDamageRole[AI_NOTIFY_TYPE_DAMAGE] -= diff;
			MAX1(teamRole->trackedDamageRole[AI_NOTIFY_TYPE_DAMAGE], 0);
		}
	}
	if(gAggroExpr->defaultStatusToMeDecayRate)
	{
		float diff = MIN(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_STATUS], gAggroExpr->defaultStatusToMeDecayRate);
		status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_STATUS] -= gAggroExpr->defaultStatusToMeDecayRate;
		MAX1(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_STATUS], 0);

		aib->totalTrackedDamage[AI_NOTIFY_TYPE_STATUS] -= diff;
		MAX1(aib->totalTrackedDamage[AI_NOTIFY_TYPE_STATUS], 0);

		team->trackedDamageTeam[AI_NOTIFY_TYPE_STATUS] -= diff;
		MAX1(team->trackedDamageTeam[AI_NOTIFY_TYPE_STATUS], 0);
	}
	if(gAggroExpr->defaultStatusToFriendsDecayRate)
	{
		float diff = MIN(status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_STATUS], gAggroExpr->defaultStatusToFriendsDecayRate);
		status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_STATUS] -= gAggroExpr->defaultStatusToFriendsDecayRate;
		MAX1(status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_STATUS] , 0);

		if(useRoleDamageSharing)
		{
			teamRole->trackedDamageRole[AI_NOTIFY_TYPE_STATUS] -= diff;
			MAX1(teamRole->trackedDamageRole[AI_NOTIFY_TYPE_STATUS], 0);
		}
	}

	if(gAggroExpr->defaultThreatToMeDecayRate)
	{
		float diff = MIN(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_THREAT], gAggroExpr->defaultThreatToMeDecayRate);
		status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_THREAT] -= gAggroExpr->defaultThreatToMeDecayRate;
		MAX1(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_THREAT], 0);

		aib->totalTrackedDamage[AI_NOTIFY_TYPE_THREAT] -= diff;
		MAX1(aib->totalTrackedDamage[AI_NOTIFY_TYPE_THREAT], 0);

		team->trackedDamageTeam[AI_NOTIFY_TYPE_THREAT] -= diff;
		MAX1(team->trackedDamageTeam[AI_NOTIFY_TYPE_THREAT], 0);
	}
	if(gAggroExpr->defaultThreatToFriendsDecayRate)
	{
		float diff = MIN(status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_THREAT], gAggroExpr->defaultThreatToFriendsDecayRate);
		status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_THREAT] -= gAggroExpr->defaultThreatToFriendsDecayRate;
		MAX1(status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_THREAT], 0);			

		if(useRoleDamageSharing)
		{
			teamRole->trackedDamageRole[AI_NOTIFY_TYPE_THREAT] -= diff;
			MAX1(teamRole->trackedDamageRole[AI_NOTIFY_TYPE_THREAT], 0);
		}
	}


	if(gAggroExpr->defaultHealingEnemiesDecayRate)
	{
		status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_HEALING] -= gAggroExpr->defaultHealingEnemiesDecayRate;
		MAX1(status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_HEALING], 0);
	}
}

// ---------------------------------------------------------------------------------------------------------
// AGGRO 2
// ---------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------
static void aiAggro2_ResetValues(Entity* e, AIStatusTableEntry* status)
{
	AIAggroDef *pAggroDef = NULL;

	status->totalBaseDangerVal = 0.f;
	status->aggroCounterTotal = 0.f;
	status->aggroGaugeTotal = 0.f;

	pAggroDef = aiAggro2_GetAggroDef(e, NULL);

	{
		S32 i;

		// Process all the aggro counters first
		for(i = eafSize(&status->aggroCounters)-1; i >= 0; --i)
		{
			AIAggroCounterDef* curCounter = eaGet(&pAggroDef->aggroCounters, i);
			if (!curCounter)
				continue;

			status->aggroCounters[i] = 0.f;
			//if(curCounter->counterType > AI_NOTIFY_TYPE_TRACKED_COUNT)
			//{
			// can't clear an expression counter type
			//}

			if (curCounter->pExprDecayRate)
			{
				status->aggroCounterDecay[i] = 0.f;
			}
		}
	}

	{
		S32 i;
		for(i = eafSize(&status->aggroGauges)-1; i >= 0; --i)
		{
			status->aggroGauges[i] = 0.f;
		}
		for(i = eafSize(&status->aggroGaugeValues)-1; i >= 0; --i)
		{
			status->aggroGaugeValues[i] = 0.f;
		}

	}
}

// ---------------------------------------------------------------------------------------------------------
// pchName is assumed to be a pooled string
static AIAggroDefBucketOverride* aiAggro2_GetBucketOverride(const AIConfig* pConfig, const char *pchName)
{
	if (pConfig->aggro.eaOverrides)
	{
		FOR_EACH_IN_EARRAY(pConfig->aggro.eaOverrides, AIAggroDefBucketOverride, pOverride)
		{	// pooled strings
			if (pOverride->pchName == pchName)
				return pOverride;
		}
		FOR_EACH_END
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------------
static S32 aiAggro2_GetBucketScale(	const AIConfig* pConfig, 
									const char *pchName, 
									F32 fDefaultPostScale, 
									S32 bDefaultEnabled,
									F32 *pfScaleOut)
{
	AIAggroDefBucketOverride *pOverride = NULL;
	
	*pfScaleOut = 0.f;

	pOverride = aiAggro2_GetBucketOverride(pConfig, pchName);
	if (pOverride)
	{
		if (!pOverride->bEnabled)
			return false;
		*pfScaleOut = pOverride->fPostScale;
	}
	else if (!bDefaultEnabled)
	{
		return false;
	}

	if (*pfScaleOut == 0.f)
	{
		if (fDefaultPostScale == 0.f)
			return false;
		*pfScaleOut = fDefaultPostScale;
	}

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
static F32 aiAggro2_EvaluateExpression(int iPartitionIdx, Entity* pEnt, Entity *pTargetEnt, Expression *pExpr)
{
	MultiVal answer = {0};

	aiTargetingExprVarsAdd(pEnt, pEnt->aibase, gAggroExprContext, pTargetEnt);
	exprContextSetPartition(gAggroExprContext, iPartitionIdx);

	exprEvaluate(pExpr, gAggroExprContext, &answer);

	exprContextClearPartition(gAggroExprContext);
	aiTargetingExprVarsRemove(pEnt, pEnt->aibase, gAggroExprContext);

	return QuickGetFloat(&answer);
}

// ---------------------------------------------------------------------------------------------------------
static void aiAggro2_ProcessCounters(	Entity* pEnt, 
										AIAggroDef *pAggroDef,
										AIConfig* pConfig, 
										AIStatusTableEntry* pStatus, 
										int iNumLegalTargets)
{
	S32 i;
	S32 numCounters = eaSize(&pAggroDef->aggroCounters);
	S32 numGauges = eaSize(&pAggroDef->aggroGauges);
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	eafClear(&pStatus->aggroCounters);
	eafSetSize(&pStatus->aggroCounters, numCounters);

	if (pAggroDef->hasDecay)
	{
		eafSetSize(&pStatus->aggroCounterDecay, numCounters);
	}
	
	// TODO: only prep context when there are expressions
	aiAggroPrepareExprContext(pEnt, pEnt->aibase, pConfig, pStatus, iNumLegalTargets);

	pStatus->aggroCounterTotal = 0.f;

	// Process all the aggro counters first
	for(i = 0; i < numCounters; ++i)
	{
		AIAggroCounterDef* curCounter = pAggroDef->aggroCounters[i];
		F32 fPostScale = 0.f;
		Entity *pTargetEnt = NULL;
		
		if(!curCounter)
			continue;
		
		if (!aiAggro2_GetBucketScale(pConfig, curCounter->name, curCounter->postScale, curCounter->enabled, &fPostScale))
			continue;
		

		if(curCounter->counterType < AI_NOTIFY_TYPE_TRACKED_COUNT)
		{
 			pStatus->aggroCounters[i] = pStatus->trackedAggroCounts[curCounter->targetType][curCounter->counterType];
		}
		else
		{
			// TODO: add validation that either i < AI_NOTIFY_TYPE_COUNT or expr is valid
			pTargetEnt = entFromEntityRef(iPartitionIdx, pStatus->entRef);
						
			pStatus->aggroCounters[i] = aiAggro2_EvaluateExpression(iPartitionIdx, pEnt, pTargetEnt, curCounter->expr);
		}


		if (curCounter->pExprDecayRate)
		{
			if (pStatus->aggroCounterDecay[i] < pStatus->aggroCounters[i])
			{	// if the decay hasn't reached the aggro counter yet, 
				// apply the decay (we assume we are updating at .5 sec intervals)
				// check if there is a timeout, 
				if (!curCounter->decayDelay || ABS_TIME_SINCE_PARTITION(iPartitionIdx, pStatus->lastNotify[i]) > SEC_TO_ABS_TIME(curCounter->decayDelay))
				{
					if (!pTargetEnt)
						pTargetEnt = entFromEntityRef(iPartitionIdx, pStatus->entRef);

					if (pTargetEnt)
					{
						F32 fDecay = aiAggro2_EvaluateExpression(iPartitionIdx, pEnt, pTargetEnt, curCounter->pExprDecayRate);
						pStatus->aggroCounterDecay[i] += fDecay;
						if (pStatus->aggroCounterDecay[i] > pStatus->aggroCounters[i])
						{	
							pStatus->aggroCounterDecay[i] = pStatus->aggroCounters[i];
						}
					}
				}
			} 
			else if (pStatus->aggroCounterDecay[i] > pStatus->aggroCounters[i])
			{	// if the counter decreased for any reason, clamp it to our current
				pStatus->aggroCounterDecay[i] = pStatus->aggroCounters[i];
			}
			
			pStatus->aggroCounterTotal += (pStatus->aggroCounters[i] - pStatus->aggroCounterDecay[i]) * fPostScale;
			pStatus->aggroCounters[i] *= fPostScale;
		}
		else
		{
			pStatus->aggroCounters[i] *= fPostScale;
			pStatus->aggroCounterTotal += pStatus->aggroCounters[i];	
		}
		
	}
}


// ---------------------------------------------------------------------------------------------------------
static void aiAggro2_EvaluateGauge(	Entity* pEnt, 
									AIAggroDef *pAggroDef,
									AIConfig* pConfig, 
									AIStatusTableEntry* pStatus, 
									int gaugeIdx, 
									F32 fMaxCounterValue)
{
	AIAggroGaugeDef* curGauge = pAggroDef->aggroGauges[gaugeIdx];
	F32 fPostScale = 0.f;

	if(!curGauge)
		return;
	if (!aiAggro2_GetBucketScale(pConfig, curGauge->name, curGauge->postScale, curGauge->enabled, &fPostScale))
		return;
			
	switch(curGauge->gaugeType)
	{
		xcase AI_AGGRO_GAUGE_DIST:
		{
			// anything within g_pDefaultAggro2Def->fMeleeRange units is considered as close as we can get
			// and is full aggro. Beyond that will be a slight drop-off in aggro out to aggroRadius

			F32 distanceNorm;
			if (pStatus->distanceFromMe > pAggroDef->fMeleeRange)
				distanceNorm = 1.f - pStatus->distanceFromMe/pEnt->aibase->aggroRadius;
			else
				distanceNorm = 1.f;

			pStatus->aggroGauges[gaugeIdx] = CLAMP(distanceNorm, 0.f, 1.f);
		}
		xcase AI_AGGRO_GAUGE_STICKY:
		{
			pStatus->aggroGauges[gaugeIdx] = (pEnt->aibase->attackTargetRef == pStatus->entRef) ? 1.f : 0.f;
		}
		xcase AI_AGGRO_GAUGE_DATA:
		{
			int iPartitionIdx = entGetPartitionIdx(pEnt);
			Entity *pTargetEnt = entFromEntityRef(iPartitionIdx, pStatus->entRef);

			pStatus->aggroGauges[gaugeIdx] = aiAggro2_EvaluateExpression(iPartitionIdx, pEnt, pTargetEnt, curGauge->expr);
			
		}
		xdefault:
			devassertmsg(0, "Current gauge type is not supported in code but useCode is true");
	}

	pStatus->aggroGauges[gaugeIdx] *= curGauge->postScale;

	switch(curGauge->scaleType)
	{
		xcase AI_AGGRO_GAUGE_SCALE_COUNTER:
			pStatus->aggroGaugeValues[gaugeIdx] = pStatus->aggroGauges[gaugeIdx] *
								pStatus->trackedAggroCounts[curGauge->scaleTargetType][curGauge->scaleCounterType];
		xcase AI_AGGRO_GAUGE_SCALE_POST_COUNTERS:
			pStatus->aggroGaugeValues[gaugeIdx] = pStatus->aggroGauges[gaugeIdx] * pStatus->aggroCounterTotal;
		xcase AI_AGGRO_GAUGE_SCALE_POST_ALL:
			// POST_ALL gauges are multiplicative
			pStatus->aggroGaugeValues[gaugeIdx] = pStatus->aggroGauges[gaugeIdx] * (pStatus->aggroGaugeTotal + pStatus->aggroCounterTotal);
		xcase AI_AGGRO_GAUGE_SCALE_MAX_TOTALCOUNTER:
			pStatus->aggroGaugeValues[gaugeIdx] = pStatus->aggroGauges[gaugeIdx] * fMaxCounterValue;

		xdefault:
			devassertmsg(0, "Current gauge scale type is unsupported");
	}
	pStatus->aggroGaugeTotal += pStatus->aggroGaugeValues[gaugeIdx];
}

// ---------------------------------------------------------------------------------------------------------
static void aiAggro2_ProcessGauges(	Entity* e, 
									AIAggroDef *pAggroDef,
									AIConfig* pConfig,
									AIStatusTableEntry* pStatus, 
									int iNumLegalTargets, 
									F32 fMaxCounterValue)
{
	S32 i;
	S32 numGauges = eaSize(&pAggroDef->aggroGauges);

	eafClear(&pStatus->aggroGauges);
	eafClear(&pStatus->aggroGaugeValues);

	eafSetSize(&pStatus->aggroGauges, numGauges);
	eafSetSize(&pStatus->aggroGaugeValues, numGauges);

	// TODO: only prep context when there are expressions
	aiAggroPrepareExprContext(e, e->aibase, pConfig, pStatus, iNumLegalTargets);

	pStatus->aggroGaugeTotal = 0.f;

	for(i = numGauges-1; i >= 0; i--)
	{
		AIAggroGaugeDef* curGaugeDef = pAggroDef->aggroGauges[i];
		if (curGaugeDef->scaleType != AI_AGGRO_GAUGE_SCALE_POST_ALL)
			aiAggro2_EvaluateGauge(e, pAggroDef, pConfig, pStatus, i, fMaxCounterValue);
	}

	for(i = 0; i < numGauges; ++i)
	{
		AIAggroGaugeDef* curGaugeDef = pAggroDef->aggroGauges[i];
		if (curGaugeDef->scaleType == AI_AGGRO_GAUGE_SCALE_POST_ALL)
			aiAggro2_EvaluateGauge(e, pAggroDef, pConfig, pStatus, i, fMaxCounterValue);
	}

	pStatus->totalBaseDangerVal = pStatus->aggroCounterTotal + pStatus->aggroGaugeTotal;
}


// ---------------------------------------------------------------------------------------------------------
void aiAggro_DoInitialPullAggro(AITeam* team, Entity* initialPuller)
{
	if (!g_pDefaultAggro2Def || g_pDefaultAggro2Def->initialPullThreat <= 0)
		return;

	FOR_EACH_IN_EARRAY(team->members, AITeamMember, member)
	{
		AIVarsBase *aib;
		AIStatusTableEntry* status;
		if (!aiIsEntAlive(member->memberBE))
			continue;
		
		aib = member->memberBE->aibase;
		status = aiStatusFind(member->memberBE, aib, initialPuller, false);
		if (status)
		{
			status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_THREAT] += g_pDefaultAggro2Def->initialPullThreat;
		}
	}	
	FOR_EACH_END
}

S32 aiAggro_DoPowerMissedAggro(Entity *target, Entity *source, F32 threatScale)
{
	if (!g_pDefaultAggro2Def || g_pDefaultAggro2Def->powerMissedThreat <= 0)
		return false; // return false so combat system knows it doesn't need to call this anymore

	aiNotify(target,source,AI_NOTIFY_TYPE_THREAT,g_pDefaultAggro2Def->powerMissedThreat*threatScale,g_pDefaultAggro2Def->powerMissedThreat*threatScale,NULL,0);
	return true;
}


// ---------------------------------------------------------------------------------------------------------
// Debug Info
// ---------------------------------------------------------------------------------------------------------


// ---------------------------------------------------------------------------------------------------------
// Fill in the debug status entry with all the aggro2 info
void aiAggro2_FillInDebugStatusTableEntry(	Entity *pEnt,
											const AIConfig *pConfig,
											AIDebugStatusTableEntry *pDebugEntry, 
											AIStatusTableEntry* status, 
											int bPostDecay, 
											int bUseScaled)
{
	AIDebugAggroBucket *pBucket;
	S32 i, numCounters, numGauges;
	AIAggroDef *pAggroDef = aiAggro2_GetAggroDef(pEnt, pConfig);

	if (!pAggroDef)
		return;

	numCounters = eaSize(&pAggroDef->aggroCounters);
	numGauges = eaSize(&pAggroDef->aggroGauges);

	// set all the totals
	pDebugEntry->aggroCounterTotal = status->aggroCounterTotal;
	pDebugEntry->aggroGaugeTotal = status->aggroGaugeTotal;
	if (bPostDecay)
		pDebugEntry->totalBaseDangerVal = status->totalBaseDangerVal;
	else
		pDebugEntry->totalBaseDangerVal = -1.f;

	// get all the counters
	for(i = 0; i < numCounters; ++i)
	{
		AIAggroCounterDef * pCounterDef = pAggroDef->aggroCounters[i];
		F32 fPostScale;
		if (!pCounterDef)
			continue;
		if (!aiAggro2_GetBucketScale(pConfig, pCounterDef->name, pCounterDef->postScale, pCounterDef->enabled, &fPostScale))
			continue;
				
		pBucket = StructAlloc(parse_AIDebugAggroBucket);
		eaPush(&pDebugEntry->eaAggroBuckets, pBucket);
		pBucket->fValue = eafGet(&status->aggroCounters, i);
		if (bPostDecay && eafSize(&status->aggroCounterDecay) == numCounters)
			pBucket->fValue = pBucket->fValue - status->aggroCounterDecay[i];

		if (!bUseScaled)
		{
			pBucket->fValue = pBucket->fValue / fPostScale;
		}
	}

	// get all the gauge info
	for(i = 0; i < numGauges; ++i)
	{
		AIAggroGaugeDef* curGauge = pAggroDef->aggroGauges[i];
		F32 fPostScale;
		if(!curGauge)
			return;
		if (!aiAggro2_GetBucketScale(pConfig, curGauge->name, curGauge->postScale, curGauge->enabled, &fPostScale))
			continue;

		pBucket = StructAlloc(parse_AIDebugAggroBucket);
		eaPush(&pDebugEntry->eaAggroBuckets, pBucket);

		if (bPostDecay)
		{
			pBucket->fValue = eafGet(&status->aggroGaugeValues, i);
		}
		else
		{
			pBucket->fValue = eafGet(&status->aggroGauges, i);

			if (!bUseScaled)
			{
				pBucket->fValue = pBucket->fValue / fPostScale;
			}
		}
		
	}

}

// ---------------------------------------------------------------------------------------------------------
void aiAggro2_FillInDebugTableHeaders(	Entity *pEnt,
										const AIConfig *pConfig,
										AIDebugAggroTableHeader ***peaAggroTableHeader)
{
	S32 i, count;
	AIAggroDef *pAggroDef = aiAggro2_GetAggroDef(pEnt, pConfig);

	if (!pAggroDef)
		return;

	eaClearEx(peaAggroTableHeader, NULL);

	count = eaSize(&pAggroDef->aggroCounters);
	for (i = 0; i < count; ++i)
	{
		AIAggroCounterDef *pCounterDef = pAggroDef->aggroCounters[i];
		AIDebugAggroTableHeader *pHeader;
		F32 fPostScale;

		if (!aiAggro2_GetBucketScale(pConfig, pCounterDef->name, pCounterDef->postScale, pCounterDef->enabled, &fPostScale))
			continue;

		pHeader = StructAlloc(parse_AIDebugAggroTableHeader);

		if (pCounterDef->name)
		{  
			pHeader->pchName = pCounterDef->name;
		}
		else 
		{
			char nameBuffer[32] = {0};
			sprintf(nameBuffer, "unnamed%d", i);
			pHeader->pchName = allocAddString(nameBuffer);
		}

		pHeader->isGauge = false;
		eaPush(peaAggroTableHeader, pHeader);
	}


	count = eaSize(&pAggroDef->aggroGauges);
	for (i = 0; i < count; ++i)
	{
		AIAggroGaugeDef * pGaugeDef = pAggroDef->aggroGauges[i];
		AIDebugAggroTableHeader *pHeader;
		F32 fPostScale;

		if (!aiAggro2_GetBucketScale(pConfig, pGaugeDef->name, pGaugeDef->postScale, pGaugeDef->enabled, &fPostScale))
			continue;
	
		pHeader = StructAlloc(parse_AIDebugAggroTableHeader);

		if (pGaugeDef->name)
		{  
			pHeader->pchName = pGaugeDef->name;
		}
		else 
		{
			char nameBuffer[32] = {0};
			sprintf(nameBuffer, "unnamed%d", i);
			pHeader->pchName = allocAddString(nameBuffer);
		}

		pHeader->isGauge = true;
		eaPush(peaAggroTableHeader, pHeader);
	}
}





#include "aiAggro_h_ast.c"
#include "aiAggro_c_ast.c"