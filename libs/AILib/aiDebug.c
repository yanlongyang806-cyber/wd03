#include "aiDebug.h"
#include "aiDebugShared.h"

#include "AnimList_Common.h"
#include "aiAnimList.h"
#include "aiAvoid.h"
#include "aiAggro.h"
#include "aiConfig.h"
#include "aiFormation.h"
#include "aiGroupCombat.h"
#include "aiJobs.h"
#include "aiLib.h"
#include "aiMessages.h"
#include "aiMovement.h"
#include "aiPowers.h"
#include "aiTeam.h"

#include "beacon.h"
#include "beaconPath.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Character_target.h"
#include "CommandQueue.h"
#include "entCritter.h"
#include "estring.h"
#include "encounter_common.h"
#include "EntityIterator.h"
#include "ExpressionDebug.h"
#include "gslCritter.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslLayerFSM.h"
#include "gslMapState.h"
#include "../gslPlayerFSM.h"
#include "logging.h"
#include "NameList.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "PowerModes.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "StructMod.h"
#include "ThreadSafeMemoryPool.h"
#include "WorldGrid.h"

#include "aiDebugShared_h_ast.h"
#include "aiStructCommon_h_ast.h"
#include "aiStruct_h_ast.h"
#include "Entity_h_ast.h"
#include "GameClientLib_autogen_ClientCmdWrappers.h"

#include "EntityMovementFlight.h"
#include "aiAmbient.h"
#include "aiMovement_h_ast.h"
#include "CharacterClass_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static int aiDebugLogToFile = 0;
AUTO_CMD_INT(aiDebugLogToFile, aiDebugLogToFile) ACMD_CATEGORY(AI);
static int s_aiDebugHideUnusedStatus = 1;
AUTO_CMD_INT(s_aiDebugHideUnusedStatus, aiDebugHideUnusedStatus) ACMD_CATEGORY(AI);

static void aiDebug_FillAggro2DebugStatusEntry(	AIDebugStatusTableEntry ***peaDebugEntries, 
												AIStatusTableEntry* status, 
												Entity *ent, 
												const AIConfig *config);

static void aiDebug_Aggro1CreateTableHeaders(AIDebugAggroTableHeader ***peaDebugAggroHeaders);
static void aiDebug_Aggro1FillInDebugStatusTableEntry(AIDebugStatusTableEntry ***peaDebugEntries, 
													  AIStatusTableEntry* status, Entity *ent, 
													  const AIConfig *config);


void aiDebugStatusTableEntryDestroy(AIDebugStatusTableEntry* debugEntry)
{
	StructDestroy(parse_AIDebugStatusTableEntry, debugEntry);
}

void aiDebugUpdateAddBasicInfo(AIDebugBasicInfo*** infoArray, const char* str)
{
	AIDebugBasicInfo* entry = StructAlloc(parse_AIDebugBasicInfo);
	entry->str = StructAllocString(str);
	eaPush(infoArray, entry);
}



// ------------------------------------------------------------------------------------------------------------
static void aiDebug_FillInDebugStatusEntry(AIDebug *aid, Entity* e, AIConfig* config, 
											AIStatusTableEntry* status, int bExternal)
{
	AIDebugStatusTableEntry ***peaDebugEntries;
	Entity *targetEnt;

	if(!status)
		return;		// Safety check

	if (bExternal)
	{
		peaDebugEntries = &aid->debugStatusExternEntries;
		targetEnt = e;
	}
	else
	{
		peaDebugEntries = &aid->debugStatusEntries;
		targetEnt = status->entRef ? entFromEntityRef(entGetPartitionIdx(e), status->entRef) : NULL;
	}

	if (!targetEnt)
		return;

	if(aiGlobalSettings.useAggro2)
	{
		aiAggro2_FillInDebugTableHeaders(e, config, &aid->eaAggroTableHeaders);
		aiDebug_FillAggro2DebugStatusEntry(peaDebugEntries, status, targetEnt, config);
	}
	else
	{
		aiDebug_Aggro1CreateTableHeaders(&aid->eaAggroTableHeaders);
		aiDebug_Aggro1FillInDebugStatusTableEntry(peaDebugEntries, status, targetEnt, config);
	}
}



int cmpStatusTableEntry(const AIStatusTableEntry** l, const AIStatusTableEntry** r)
{
	const AIStatusTableEntry* lhs = *l;
	const AIStatusTableEntry* rhs = *r;

	if(rhs->totalBaseDangerVal == lhs->totalBaseDangerVal)
		return rhs->entRef - lhs->entRef;

	return rhs->totalBaseDangerVal > lhs->totalBaseDangerVal ? 1 : -1;
}

void aiDebugEntToString(Entity *ent, char **estr)
{
	char *colorStart = "", *colorEnd = "";

	if(!ent)
	{
		estrConcatf(estr, " REMOVEDENT ");
		return;
	}

	if(!exprFuncHelperEntIsAlive(ent))
	{
		colorStart = "<color blue>"; colorEnd = "</color>";
	}
	if(exprFuncHelperShouldExcludeFromEntArray(ent))
	{
		colorStart = "<color green>"; colorEnd = "</color>";
	}

	estrConcatf(estr, " %s%d - %s%s ", colorStart, entGetRef(ent), entGetLocalName(ent), colorEnd);
}

void aiDebugMultiValToString(MultiVal *val, char **estr)
{
	switch(val->type)
	{
		xcase MULTI_INT: {
			estrPrintf(estr, "%"FORM_LL"d", val->intval);
		}
		xcase MULTI_FLOAT: {
			estrPrintf(estr, "%f", val->floatval);
		}
		xcase MULTI_NP_ENTITYARRAY: {
			int j;

			for(j = eaSize(val->entarray)-1; j >= 0; j--)
			{
				Entity *ent = (*val->entarray)[j];

				aiDebugEntToString(ent, estr);
			}
		}
		xcase MULTI_INTARRAY_F: {
			int j;

			for(j = eaiSize(&val->intptr)-1; j >= 0; j--)
			{
				Entity *ent = entFromEntityRefAnyPartition(val->intptr[j]);
				
				aiDebugEntToString(ent, estr);
			}
		}
		xdefault: {
			estrPrintf(estr, "UNKNOWN");
		}
	}
}

void aiDebugFillVars(AIDebug* aid, ExprContext *exprContext)
{
	int i;
	ExprVarEntry **varEntries = NULL;
	char *estr = NULL;

	estrStackCreate(&estr);

	exprContextGetVarsAsEArray(exprContext, &varEntries);

	for(i = eaSize(&varEntries)-1; i >= 0; i--)
	{
		ExprVarEntry *entry = varEntries[i];
		AIDebugVarEntry *adve = StructAlloc(parse_AIDebugVarEntry);

		adve->name = strdup(entry->name);
		estrClear(&estr);
		aiDebugMultiValToString(&entry->simpleVar, &estr);			
		adve->value = strdup(estr);

		eaPush(&aid->varInfo, adve);
	}
	eaDestroy(&varEntries);

	estrDestroy(&estr);
}

void aiDebugFillExternFSMVars(AIDebug* aid, GameLayerFSM *glfsm)
{
	int i;
	WorldVariable **wVars = NULL;
	char *estr = NULL;
	wVars = layerfsm_GetAllVars(glfsm);

	for(i = eaSize(&wVars)-1; i >= 0; i--)
	{
		WorldVariable *var = wVars[i];
		AIDebugVarEntry *adve = StructAlloc(parse_AIDebugVarEntry);

		adve->name = strdup(var->pcName);
		estrClear(&estr);
		worldVariableToEString(var, &estr);
		adve->value = strdup(estr);
		adve->origin = "LayerFSM";

		eaPush(&aid->exVarInfo, adve);
	}

	eaDestroy(&wVars);

}

static void aiDebugFillMessages(int partitionIdx, AIDebug* aid, FSMContext *fsmContext)
{
	int i;
	StashTableIterator iter;
	StashElement elem;
	char *estr = NULL;

	estrStackCreate(&estr);

	stashGetIterator(fsmContext->messages, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		AIMessage *msg = stashElementGetPointer(elem);
		AIDebugMsgEntry *adme = StructAlloc(parse_AIDebugMsgEntry);

		adme->name = strdup(msg->tag);
		adme->count = eaSize(&msg->entries);
		if(eaSize(&msg->entries))
		{
			estrClear(&estr);
			for(i = eaSize(&msg->entries)-1; i >= 0; i--)
			{
				AIMessageEntry *entry = msg->entries[i];

				if(i==eaSize(&msg->entries)-1)
					adme->timeSince = ABS_TIME_TO_SEC(ABS_TIME_SINCE_PARTITION(partitionIdx, entry->time));
			}
			adme->sources = strdup(estr);
		}
		else
			adme->timeSince = 0;

		if(msg->refArray)
		{
			estrClear(&estr);
			for(i = 0; i < eaiSize(&msg->refArray); i++)
			{
				Entity *ent = entFromEntityRef(partitionIdx, msg->refArray[i]);

				aiDebugEntToString(ent, &estr);
			}

			adme->attachedEnts = strdup(estr);
		}

		eaPush(&aid->msgInfo, adme);
	}

	estrDestroy(&estr);
}

static void aiDebugFillTeam(AIDebugTeamInfo **teamInfoOut, Entity* e, AITeam* team)
{
	int i, n;
	F32 distFromLeash = 0;
	static char* estr = NULL;
	AIDebugTeamInfo *teamInfo = NULL;

	*teamInfoOut = teamInfo = StructAlloc(parse_AIDebugTeamInfo);

	if(team->teamOwner)
	{
		estrPrintf(&estr, "TeamOwner: " ENT_PRINTF_STR, entPrintfParams(team->teamOwner));

		distFromLeash = entGetDistance(e, NULL, team->teamOwner, NULL, NULL);

		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);
	}
	else
	{
		estrPrintf(&estr, "Leash: ");

		if(team->config.ignoreMaxProtectRadius)
			estrConcatf(&estr, "ignoreMaxProtectRadius");
		else
		{
			if(team->roamingLeash)
			{
				if(team->roamingLeashPointValid)
				{
					estrConcatf(&estr, "Roaming (" LOC_PRINTF_STR ") ", vecParamsXYZ(team->roamingLeashPoint));
					distFromLeash = entGetDistance(e, NULL, NULL, team->roamingLeashPoint, NULL);
				}
				else
					estrConcatf(&estr, "Roaming ");
			}
			else
				distFromLeash = entGetDistance(e, NULL, NULL, team->spawnPos, NULL);

			estrConcatf(&estr, "%.0f feet", team->leashDist);
		}

		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);

		if(distFromLeash)
		{
			estrPrintf(&estr, "Distance from leash point: %.2f", distFromLeash);

			aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);
		}
	}

	{
		estrPrintf(&estr, "Heal: ");

		if(team->powInfo.hasHealPowers)
		{
			estrConcatf(&estr, "ABLE ");
		}
		else
			estrConcatf(&estr, "UNABLE");

		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);
	}

	{
		estrPrintf(&estr, "Shield Heal: ");

		if(team->powInfo.hasShieldHealPowers)
		{
			estrConcatf(&estr, "ABLE ");
		}
		else
			estrConcatf(&estr, "UNABLE");

		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);
	}

	{
		estrPrintf(&estr, "Buff: ");

		if(team->powInfo.hasBuffPowers)
		{
			estrConcatf(&estr, "ABLE ");
		}
		else
			estrConcatf(&estr, "UNABLE");

		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);
	}

	{
		estrPrintf(&estr, "Res: ");

		if(team->powInfo.hasResPowers)
		{
			estrConcatf(&estr, "ABLE ");
		}
		else
			estrConcatf(&estr, "UNABLE");

		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);
	}

	if(GET_REF(team->aigcSettings))
	{
		AIGroupCombatSettings *settings = GET_REF(team->aigcSettings);
		estrPrintf(&estr, "AIGroupCombat: %s", settings->name);
		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);

		estrPrintf(&estr, "NumCombatTokens: %.2f", team->combatTokenAccum);
		aiDebugUpdateAddBasicInfo(&teamInfo->teamBasicInfo, estr);
	}

	n = eaSize(&team->members);

	for(i = 0; i < n; i++)
	{
		Entity* memberE = team->members[i]->memberBE;
		AIDebugTeamMember* adtm = StructAlloc(parse_AIDebugTeamMember);
		AICombatRoleTeamMember* role = team->members[i]->pCombatRole;

		adtm->ref = memberE->myRef;
		if(memberE->aibase->job)
		{
			adtm->job_name = StructAllocString(memberE->aibase->job->desc->jobName);
		}
		adtm->critter_name = StructAllocString(memberE->debugName);
		entGetPos(memberE, adtm->pos);
		if(role)
			adtm->role_name = StructAllocString(role->pTeamRole->pchName);

		if(GET_REF(team->aigcSettings) && (team->combatTeam || !aiGlobalSettings.enableCombatTeams))
		{
			AIConfig *config = aiGetConfig(memberE, memberE->aibase);
			adtm->combatTokens			= team->members[i]->numCombatTokens;
			adtm->combatTokenRateSelf	= config->combatTokenRateSelf;
			adtm->combatTokenRateSocial	= config->combatTokenRateSocial;
		}

		eaPush(&teamInfo->members, adtm);
	}

	n = eaSize(&team->healAssignments);
	for (i = 0; i < n; i++)
	{
		AITeamMemberAssignment *tma = team->healAssignments[i];
		AIDebugTeamMemberAssignment *adtma = StructAlloc(parse_AIDebugTeamMemberAssignment);

		adtma->targetName = StructAllocString(tma->target->memberBE->debugName);
		adtma->type = (AIDebugTeamAssignmentType)tma->type;
		if (tma->assignee)
		{
			AIPowerInfo *powInfo;
			adtma->assigneeName = StructAllocString(tma->assignee->memberBE->debugName);
			powInfo = aiPowersFindInfoByID(tma->assignee->memberBE, tma->assignee->memberBE->aibase, 
				tma->powID);
			if (powInfo)
			{
				PowerDef* def = GET_REF(powInfo->power->hDef);
				if (def)
				{
					adtma->powerName = StructAllocString(def->pchName);
				}
			}
		}

		eaPush(&teamInfo->healingAssignments, adtma);
	}
}

void aiDebugFillStructLayerFSM(Entity* debugger, GameLayerFSM *layerFSM, AIDebug *aid)
{
	int iPartitionIdx = entGetPartitionIdx(debugger);
	FSMContext *pFSMContext = layerfsm_GetFSMContext(layerFSM, iPartitionIdx);
	char *estr = NULL;

	estrStackCreate(&estr);

	if(aid->settings.flags & AI_DEBUG_FLAG_BASIC_INFO)
	{
		estrPrintf(&estr, "LayerFSM: %s", layerFSM->pcName);
		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		estrPrintf(&estr, "FSM: %s", fsmGetFullStatePath(pFSMContext));
		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_VARS)
	{
		int partitionIdx = entGetPartitionIdx(debugger);
		LayerFSMPartitionState *partition = eaGet(&layerFSM->eaPartitionStates, partitionIdx);

		if(partition)
			aiDebugFillVars(aid, partition->pExprContext);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_EXVARS)
	{
		aiDebugFillExternFSMVars(aid,layerFSM);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_MSGS)
	{
		aiDebugFillMessages(iPartitionIdx, aid, pFSMContext);
	}

	estrDestroy(&estr);
}

void aiDebugFillStructPlayerFSM(Entity *debugger, PlayerFSM* pfsm, AIDebug *aid)
{
	char *estr = NULL;

	estrStackCreate(&estr);

	if(aid->settings.flags & AI_DEBUG_FLAG_BASIC_INFO)
	{
		estrPrintf(&estr, "PlayerFSM: %s", fsmGetName(pfsm->fsmContext, 0));
		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		estrPrintf(&estr, "FSM: %s", fsmGetFullStatePath(pfsm->fsmContext));
		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_VARS)
	{
		aiDebugFillVars(aid, pfsm->exprContext);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_MSGS)
	{
		aiDebugFillMessages(entGetPartitionIdx(debugger), aid, pfsm->fsmContext);
	}

	estrDestroy(&estr);
}

void aiDebugFillStructEntity(Entity *debugger, Entity *e, AIVarsBase *aib, AIDebug* aid, int dontFillMovement)
{
	int i, n;
	char *estr = NULL;
	AIConfig *config = aiGetConfig(e, aib);
	Vec3 ePos;

	estrStackCreate(&estr);

	aid->attackTargetRef = aib->attackTarget ? entGetRef(aib->attackTarget) : 0;
	entGetPos(e, ePos);

	if(aid->settings.flags & AI_DEBUG_FLAG_BASIC_INFO)
	{
		CritterDef* cdef;
		CritterFaction *faction = NULL;
		Vec3 myPos;
		Vec3 vecVelocity;
		Vec3 zeroVec = { 0 };
		F32 speed;
		FSMContext *pFSMContext = aiGetCurrentBaseFSMContext(e);
		int iPartitionIdx = entGetPartitionIdx(e);
		F32 entAge;

		entGetPos(e, myPos);
		entCopyVelocityFG(e, vecVelocity);
		speed = distance3(zeroVec, vecVelocity);
		
		entAge =  ABS_TIME_TO_SEC(ABS_TIME_PARTITION(iPartitionIdx) - aib->time.timeSpawned);
		estrPrintf(&estr, "EntRef: %d (Id: %d) (Age: %.0f) Name: %s", entGetRef(e), entGetRefIndex(e), entAge,
			 entGetLocalName(e));

		if(e->pCritter)
		{
			const char* encName;
			cdef = GET_REF(e->pCritter->critterDef);
			if(cdef)
			{
				estrConcatf(&estr, " (%s)", cdef->pchName);
			}

			encName = critter_GetEncounterName(e->pCritter);

			if(encName)
			{
				estrConcatf(&estr, " (%s\\%s)", encName, critter_GetActorName(e->pCritter));
			}
			else
				estrConcatf(&estr, " (No-Enc)");

			estrConcatf(&estr, " AIConfig: %s", REF_STRING_FROM_HANDLE(aib->config_use_accessor));
		}

		faction = entGetFaction(e);
		if(faction)
			estrConcatf(&estr, " Faction: %s", faction->pchName);

		if(e->pChar)
			estrConcatf(&estr, " GangID: %d", e->pChar->gangID);

		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		estrPrintf(&estr, "POS: (%.2f, %.2f, %.2f), Spawn POS: (%.2f, %.2f, %.2f), dist from spawn: %.2f, dist from me: %.2f",
			vecParamsXYZ(myPos), vecParamsXYZ(aib->spawnPos), entGetDistance(e, NULL, NULL, aib->spawnPos, NULL), debugger ? entGetDistance(e, NULL, debugger, NULL, NULL) : -1);

		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		estrPrintf(&estr, "VEL: (%.2f, %.2f, %.2f) Speed: %.2f",
			vecParamsXYZ(vecVelocity), speed);

		if(e->mm.mrFlight)
		{
			F32 throttle;

			if(mrFlightGetThrottle(e->mm.mrFlight, &throttle))
			{
				estrConcatf(&estr, " Throttle: %.2f", throttle);
			}
		} 

		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		estrPrintf(&estr, "Ambient FSM: %s", aiGetStatePathWithJob(e));
		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		if(aib->combatFSMContext)
		{
			estrPrintf(&estr, "Combat FSM: %s", fsmGetFullStatePath(aib->combatFSMContext));
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
		}

		if(aib->exprContext && e->aibase && pFSMContext && pFSMContext->curTracker)
		{
			ExprLocalData ***localData = NULL;
			CommandQueue **cmdQueue = NULL;
			FSMLDAmbient* ambientData;
			FSMLDCombatJob *combatJobData;
			
	
			cmdQueue = &pFSMContext->curTracker->exitHandlers;
			localData = &pFSMContext->curTracker->localData;
			exprContextCleanupPush(aib->exprContext, cmdQueue, localData);

			ambientData = getMyData(aib->exprContext, parse_FSMLDAmbient, (U64)"Ambient");
			if(ambientData)
			{
				const char *state = StaticDefineIntRevLookupNonNull(FSMLDAmbientStateEnum, ambientData->currentState);

				estrPrintf(&estr, "Ambient (%s) [%s] Wander [active:%d speed:%.1f dur:%.1f air:%.1f dist:%.1f w:%.1f] Idle [active:%d anim:%s dur:%.1f w:%.1f]", 
						ambientData->isActive ? "active" : "inactive", state,
						ambientData->isWanderActive, ambientData->wanderSpeed, ambientData->wanderDuration, ambientData->airWander, ambientData->wanderDistance, ambientData->wanderWeight,
						ambientData->isIdleActive, ambientData->idleAnimation, ambientData->idleDuration, ambientData->idleWeight
						);
				aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

				estrPrintf(&estr, "                               Chat [active:%d msgKey:%s anim:%s dur:%.1f w:%.1f] Job [active:%d dur:%.1f w:%.1f]", 
					ambientData->isChatActive, ambientData->chatMessageKey, ambientData->chatAnimation, ambientData->chatDuration, ambientData->chatWeight,
					ambientData->isJobActive, ambientData->jobDuration, ambientData->jobWeight);
				aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

			}

			combatJobData = getMyData(aib->exprContext, parse_FSMLDCombatJob, (U64)"CombatJob");
			if (combatJobData)
			{
				if (combatJobData->jobFsmContext)
				{
					estrPrintf(&estr, "Combat Job FSM: %s", fsmGetFullStatePath(combatJobData->jobFsmContext));
					aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
				}
				else if (e->aibase->movingToCombatJob && e->aibase->movementOrderGivenForCombatJob)
				{
					estrPrintf(&estr, "Moving to combat job at (%.2f %.2f %.2f)", e->aibase->vecTargetCombatJobPos[0], e->aibase->vecTargetCombatJobPos[1], e->aibase->vecTargetCombatJobPos[2]);
					aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
				}
			}

			exprContextCleanupPop(aib->exprContext);
		}

		if(aib->team)
		{
			estrPrintf(&estr, "Team combat state: %s", StaticDefineIntRevLookup(AITeamCombatStateEnum, aib->team->combatState));
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
		}

		if(aib->combatTeam)
		{
			estrPrintf(&estr, "Combat team combat state: %s", StaticDefineIntRevLookup(AITeamCombatStateEnum, aib->combatTeam->combatState));
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
		}

		if(aiTeamGetCombatTeam(e, aib)->combatState==AITEAM_COMBAT_STATE_LEASH)
		{
			estrPrintf(&estr, "Leash state: %s", StaticDefineIntRevLookup(AILeashStateEnum, aib->leashState));
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
		}

		estrPrintf(&estr, "Aware: %.2f, Aggro: %.2f, Perception: %.2f, MaxProtectDist: %.2f (Team: %.2f), Proximity: %.2f", 
			aib->awareRadius, aib->aggroRadius, character_GetPerceptionDist(e->pChar, NULL), 
			config->combatMaxProtectDist, aib->combatTeam ? aib->combatTeam->leashDist : aib->team->leashDist, aib->proximityRadius);

		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		estrPrintf(&estr, "PrefMinRange: %.2f, PrefMaxRange: %.2f", config->prefMinRange, config->prefMaxRange);

		aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		if(aib->attackTarget)
		{
			estrClear(&estr);
			if(e->pChar->bTauntActive)
				estrPrintf(&estr, "TAUNTED: ");
			estrConcatf(&estr, "AttackTarget: %s(%d), dist %.2f, dist from spawn: %.2f", aib->attackTarget->debugName,
				entGetRef(aib->attackTarget), sqrt(aib->attackTargetDistSQR), aiGetGuardPointDistance(e, aib, aib->attackTarget));
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
		}

		if(aib->powers->alwaysFlight || config->movementParams.alwaysFly)
		{
			char *reason = "";

			if(aib->powers->alwaysFlight && !aib->powers->hasFlightPowers)
			{
				reason = "Class";
			}
			else if(aib->powers->alwaysFlight)
			{
				reason = "Power";
			}
			else if(aib->localModifiedAiConfig->movementParams.alwaysFly)
			{
				reason = "AICfg";
			}
			estrPrintf(&estr, "--ALWAYSFLY(%s)--", reason);
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
		}

		if(aib->powers->hasFlightPowers)
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, "--CANFLY--");

		estrClear(&estr);

		if(aib->sleeping)
			estrConcatf(&estr, "%sSLEEPING", estr[0] ? " " : "");

		if(e->pChar && e->pChar->bInvulnerable)
			estrConcatf(&estr, "%sINVULNERABLE", estr[0] ? " " : "");

		if(e->pChar && e->pChar->bUnstoppable)
			estrConcatf(&estr, "%sUNSTOPPABLE", estr[0] ? " " : "");

		if(!ENTACTIVE(e))
			estrConcatf(&estr, "%sINACTIVE", estr[0] ? " " : "");

		if(estr[0])
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);

		if(entGetFlagBits(e))
		{
			EntityFlags flagBits = entGetFlagBits(e);
			int numBits = sizeof(flagBits) * 8;
			estrClear(&estr);
			for(i = 0; i < numBits; i++)
			{
				if(flagBits & 1 << i)
				{
					estrConcatf(&estr, "%s%s", estr[0] ? ", " : "",
						StaticDefineIntRevLookup(GlobalEntityFlagsEnum, 1 << i));
				}
			}
			aiDebugUpdateAddBasicInfo(&aid->basicInfo, estr);
		}
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_STATUS_TABLE)
	{
		eaQSort(aib->statusTable, cmpStatusTableEntry);

		n = eaSize(&aib->statusTable);
		for(i = 0; i < n; i++)
		{
			aiDebug_FillInDebugStatusEntry(aid, e, config, aib->statusTable[i], false);
		}
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_STATUS_EXTERN)
	{
		n = eaiSize(&aib->statusCleanup);
		for(i=0; i<n; i++)
		{
			Entity *other = entFromEntityRef(entGetPartitionIdx(e), aib->statusCleanup[i]);

			if(other)
			{
				AIStatusTableEntry *status = aiStatusFind(other, other->aibase, e, false);
				aiDebug_FillInDebugStatusEntry(aid, other, config, status, true);
			}
		}
	}
	
	if(aid->settings.flags & AI_DEBUG_FLAG_POWERS)
	{
		F32 time;
		char timeStr[200];
		timeStr[0] = 0;

		estrPrintf(&estr, "Last power use/activation: ");

		time = ABS_TIME_TO_SEC(aib->time.lastUsedPower);
		timeMakeOffsetStringFromSeconds(timeStr, time);
		estrConcatf(&estr, "%s.%02d/", timeStr, (int)((time - (int)time) * 100));

		time = ABS_TIME_TO_SEC(aib->time.lastActivatedPower);
		timeMakeOffsetStringFromSeconds(timeStr, time);
		estrConcatf(&estr, "%s.%02d", timeStr, (int)((time - (int)time) * 100));

		estrConcatf(&estr, ", Global Power Recharge: %.2f", config->globalPowerRecharge);

		aiDebugUpdateAddBasicInfo(&aid->powerBasicInfo, estr);

		if(e->pChar && eaiSize(&e->pChar->piPowerModes))
		{
			n = eaiSize(&e->pChar->piPowerModes);

			estrPrintf(&estr, "Current power modes: ");

			for(i = 0; i < n; i++)
			{
				estrConcatf(&estr, "%s%s",
					StaticDefineIntRevLookup(PowerModeEnum, e->pChar->piPowerModes[i]),
					i == n-1 ? "" : ", ");
			}

			aiDebugUpdateAddBasicInfo(&aid->powerBasicInfo, estr);
		}

		estrPrintf(&estr, "Legend: A = Attack, H = Heal, R = Res, B = Buff, L = Lunge, OC = OutOfCombat, S = Self, Ad = After Death");

		aiDebugUpdateAddBasicInfo(&aid->powerBasicInfo, estr);

		if(aib->useDynamicPrefRange)
		{
			estrPrintf(&estr, "AIConfig Preferred Range (min: %.2f max: %.2f) :: Dynamic Preferred Range Enabled : (min: %.2f max: %.2f)", config->prefMinRange, config->prefMaxRange, aib->minDynamicPrefRange, aib->maxDynamicPrefRange);
		}
		else
		{
			estrPrintf(&estr, "AIConfig Preferred Range (min: %.2f max: %.2f) :: Dynamic Preferred Range Disabled", config->prefMinRange, config->prefMaxRange);
		}

		aiDebugUpdateAddBasicInfo(&aid->powerBasicInfo, estr);

		n = eaSize(&aib->powers->powInfos);

		for(i = 0; i < n; i++)
		{
			AIDebugPowersInfo* powInfoEntry = StructAlloc(parse_AIDebugPowersInfo);
			AIPowerInfo* curPowInfo = aib->powers->powInfos[i];
			AIPowerConfig* curPowConfig = aiGetPowerConfig(e, aib, curPowInfo);
			PowerDef* def = GET_REF(curPowInfo->power->hDef);

			powInfoEntry->powerName = def ? StructAllocString(def->pchName) : StructAllocString("Unknown");
			powInfoEntry->rechargeTime = curPowInfo->power->fTimeRecharge;
			powInfoEntry->curRating = curPowInfo->curRating;
			powInfoEntry->aiMinRange = curPowConfig->minDist;
			powInfoEntry->aiMaxRange = curPowConfig->maxDist;
			powInfoEntry->absWeight = curPowConfig->absWeight;
			powInfoEntry->lastUsed = curPowInfo->lastUsed;
			powInfoEntry->timesUsed = curPowInfo->timesUsed;
			
			estrClear(&estr);

			if(curPowInfo->isAttackPower)
				estrConcatf(&estr, "A ");
			if(curPowInfo->isHealPower)
				estrConcatf(&estr, "H ");
			if(curPowInfo->isShieldHealPower)
				estrConcatf(&estr, "Sh ");
			if(curPowInfo->isResPower)
				estrConcatf(&estr, "R ");
			if(curPowInfo->isBuffPower)
				estrConcatf(&estr, "B ");
			if(curPowInfo->isLungePower)
				estrConcatf(&estr, "L ");
			if(curPowInfo->isOutOfCombatPower)
				estrConcatf(&estr, "OC ");
			if(curPowInfo->isSelfTarget)
				estrConcatf(&estr, "S ");
			if(curPowInfo->isSelfTargetOnly)
				estrConcatf(&estr, "SO ");
			if(curPowInfo->isAfterDeathPower)
				estrConcatf(&estr, "Ad ");
			if(curPowInfo->isDeadOrAlivePower)
				estrConcatf(&estr, "DoA ");

			powInfoEntry->tags = StructAllocString(estr);

			estrClear(&estr);

			if(curPowConfig->aiRequires)
				estrConcatf(&estr, "AIReq: %s", exprGetCompleteString(curPowConfig->aiRequires));

			if(curPowConfig->aiEndCondition)
				estrConcatf(&estr, "AIEndCond: %s", exprGetCompleteString(curPowConfig->aiEndCondition));

			if(curPowConfig->chainTarget)
			{
				estrConcatf(&estr, "Chain: %s %s%s%s", curPowConfig->chainTarget,
					curPowConfig->chainRequires ? "(" : "",
					curPowConfig->chainRequires ? exprGetCompleteString(curPowConfig->chainRequires) : "",
					curPowConfig->chainRequires ? ")" : "");
			}

			if(curPowConfig->targetOverride)
				estrConcatf(&estr, "TargOver: %s", exprGetCompleteString(curPowConfig->targetOverride));

			if(curPowConfig->weightModifier)
				estrConcatf(&estr, "WghtMod: %s", exprGetCompleteString(curPowConfig->weightModifier));

			if(strlen(estr))
				powInfoEntry->aiExpr = StructAllocString(estr);

			eaPush(&aid->powersInfo, powInfoEntry);
		}
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_TEAM && aib->team)
	{
		aiDebugFillTeam(&aid->teamInfo, e, aib->team);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_COMBATTEAM && aib->combatTeam)
	{
		aiDebugFillTeam(&aid->combatTeamInfo, e, aib->combatTeam);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_VARS)
	{
		aiDebugFillVars(aid, aib->exprContext);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_EXVARS)
	{
		WorldVariable **wVars = NULL;
		OldEncounterVariable **eVars = NULL;
		CritterVar **cVars = NULL;
		CritterVar **gVars = NULL;

		encfsm_GetAllExternVars(e, aib->exprContext, &wVars, &eVars, &cVars, &gVars);

		for(i = eaSize(&wVars)-1; i >= 0; i--)
		{
			WorldVariable *var = wVars[i];
			AIDebugVarEntry *adve = StructAlloc(parse_AIDebugVarEntry);

			adve->name = strdup(var->pcName);
			estrClear(&estr);
			worldVariableToEString(var, &estr);
			adve->value = strdup(estr);
			adve->origin = "Encounter";

			eaPush(&aid->exVarInfo, adve);
		}

		for(i = eaSize(&eVars)-1; i >= 0; i--)
		{
			OldEncounterVariable *var = eVars[i];
			AIDebugVarEntry *adve = StructAlloc(parse_AIDebugVarEntry);

			adve->name = strdup(var->varName);
			estrClear(&estr);
			if(eaSize(&var->parsedStrVals))
			{
				int j;

				for(j=0; j<eaSize(&var->parsedStrVals); j++)
				{
					char *strTemp = NULL;
					aiDebugMultiValToString(var->parsedStrVals[j], &strTemp);
					estrConcatf(&estr, "%s |", strTemp);
					estrDestroy(&strTemp);
				}
			}
			else
				aiDebugMultiValToString(&var->varValue, &estr);
			adve->value = strdup(estr);
			adve->origin = "Encounter";

			eaPush(&aid->exVarInfo, adve);
		}

		for(i = eaSize(&cVars)-1; i >= 0; i--)
		{
			CritterVar *var = cVars[i];
			AIDebugVarEntry *adve = StructAlloc(parse_AIDebugVarEntry);

			adve->name = strdup(var->var.pcName);
			adve->origin = "Critter";
			worldVariableToEString(&var->var, &estr);
			adve->value = strdup(estr);

			eaPush(&aid->exVarInfo, adve);
		}

		for(i = eaSize(&gVars)-1; i >= 0; i--)
		{
			CritterVar *var = gVars[i];
			AIDebugVarEntry *adve = StructAlloc(parse_AIDebugVarEntry);

			adve->name = strdup(var->var.pcName);
			adve->origin = "Group";
			worldVariableToEString(&var->var, &estr);
			adve->value = strdup(estr);

			eaPush(&aid->exVarInfo, adve);
		}

		eaDestroy(&cVars);
		eaDestroy(&eVars);
		eaDestroy(&wVars);
		eaDestroy(&gVars);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_MSGS)
	{
		aiDebugFillMessages(entGetPartitionIdx(e), aid, aib->fsmContext);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_MOVEMENT && !dontFillMovement)
	{
		aid->movementInfo = StructAlloc(parse_AIDebugMovementInfo);

		aid->movementInfo->curWp = aib->debugCurWp;
		entGetPos(e, aid->movementInfo->curPos);

		n = eaSize(&aib->debugCurPath);

		for(i = 0; i < n; i++)
		{
			AIDebugWaypoint* wp = StructClone(parse_AIDebugWaypoint, aib->debugCurPath[i]);
			eaPush(&aid->movementInfo->curPath, wp);
		}

		aiMovementGetSplineTarget(e, aib, aid->movementInfo->splineTarget);
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_LOC_RATINGS)
	{
		for(i=0; i<eaSize(&aib->ratings); i++)
		{
			AIDebugLocRating *rating = StructClone(parse_AIDebugLocRating, aib->ratings[i]);
			eaPush(&aid->locRatings, rating);
		}
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_FORMATION)
	{
		if(aib->pFormationData)
		{
			if (e->erOwner && aib->team && (eaSize(&aib->team->members) == 1))
			{
				devassert(0 && "Abandoned pet!");
			}
			else
			{
				aid->formation = StructCreate(parse_AIDebugFormation);

				aiFormation_GetDebugFormationPositions(entGetPartitionIdx(e), aib->pFormationData->pFormation, aid->formation);
			}
		}
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_CONFIG_MODS)
	{
		for(i = eaSize(&aib->configMods)-1; i >= 0; i--)
		{
			AIDebugStringStringEntry* entry = StructAlloc(parse_AIDebugStringStringEntry);
			StructMod* mod = aib->configMods[i];

			entry->name = StructAllocString(mod->name);
			entry->val = StructAllocString(mod->val);
			eaPush(&aid->configMods, entry);
		}
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_AVOID)
	{
		aid->avoidInfo = StructAlloc(parse_AIDebugAvoid);
		
		// might want an option to turn beacon / avoid volumes on separately
				
		{
			Vec3 pos;
			Beacon **bcns = NULL;

			entGetPos(e, pos);
			beaconGetNearbyBeacons(&bcns, pos, 100, NULL, NULL);

			for(i=0; i<eaSize(&bcns); i++)
			{
				AIDebugAvoidBcn *abcn = StructAlloc(parse_AIDebugAvoidBcn);

				copyVec3(bcns[i]->pos, abcn->pos);
				abcn->avoid = aiShouldAvoidBeacon(entGetRef(e), bcns[i], 0);

				eaPush(&aid->avoidInfo->bcns, abcn);
			}

			eaDestroy(&bcns);
		}
		
		{	
			FOR_EACH_IN_EARRAY(aib->avoid.base.volumeEntities, AIVolumeEntry, pVolume)
			{
				AIDebugAvoidVolume *dbgavoid = StructAlloc(parse_AIDebugAvoidVolume);
				
				if (pVolume->isSphere)
				{
					dbgavoid->fRadius = aiVolumeEntry_GetRadius(pVolume);
				}
				else
				{
					copyMat4(pVolume->boxMat, dbgavoid->mtxBox);
					copyVec3(pVolume->boxLocMin, dbgavoid->vBoxMin);
					copyVec3(pVolume->boxLocMax, dbgavoid->vBoxMax);
				}
				aiVolumeEntry_GetPosition(pVolume, dbgavoid->vPos);

				eaPush(&aid->avoidInfo->volumes, dbgavoid);
			}
			FOR_EACH_END
		}
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_AGGRO && e->pChar)
	{
		AIConfig *cfg = NULL;
		CharacterClass *cl = NULL;
		static Entity **ents = NULL;
		aid->aggroInfo = StructAlloc(parse_AIDebugAggro);

		aid->aggroInfo->socialEnabled = aiGlobalSettings.enableSocialAggroPulse;

		cfg = RefSystem_ReferentFromString("AIConfig", "Default");

		if(!cfg)
		{
			static AIConfig *local = NULL;
			if(!local)
				local = StructCreate(parse_AIConfig);

			cfg = local;
		}

		cl = characterclasses_FindByName("Default");

		if(!cl)
		{
			static CharacterClass *local = NULL;
			if(!local)
				local = StructCreate(parse_CharacterClass);

			cl = local;
		}

		aid->aggroInfo->defAggro = class_GetAttribBasic(cl, kAttribType_Aggro, e->pChar->iLevelCombat);
		aid->aggroInfo->defAware = cfg->awareRatio * aid->aggroInfo->defAggro;

		if(aid->aggroInfo->socialEnabled)
		{
			aid->aggroInfo->defSocPrim = cfg->socialAggroPrimaryDist;
			aid->aggroInfo->defSocSec = cfg->socialAggroSecondaryDist;
		}

		eaClear(&ents);
		entGridProximityLookupExEArray(entGetPartitionIdx(e), ePos, &ents, 300, 0, ENTITYFLAG_PROJECTILE, e);

		FOR_EACH_IN_EARRAY(ents, Entity, nearEnt)
		{
			AIDebugPerEntity *per = StructAlloc(parse_AIDebugPerEntity);
			AIConfig *nearcfg = aiGetConfig(e, e->aibase);
			
			per->myRef = nearEnt->myRef;
			per->aggro = nearEnt->aibase ? nearEnt->aibase->aggroRadius : 0.0f;
			per->aware = nearcfg->awareRatio * per->aggro;

			per->socPrim = nearcfg->socialAggroPrimaryDist;
			per->socSec = nearcfg->socialAggroSecondaryDist;

			eaPush(&aid->entInfo, per);
		}
		FOR_EACH_END;
	}

	if(aid->settings.flags & AI_DEBUG_FLAG_LOG)
	{
		n = eaSize(&aib->logEntries);
		for(i = aib->curLogEntry-1;
			i >= 0 && i % (int)AI_MAX_LOG_ENTRIES < n &&
			i > aib->curLogEntry - (int)AI_MAX_LOG_ENTRIES; i--)
		{
			AIDebugLogEntryClient* entry = StructAlloc(parse_AIDebugLogEntryClient);
			entry->timeInSec = ABS_TIME_TO_SEC(aib->logEntries[i % AI_MAX_LOG_ENTRIES]->time);
			entry->str = StructAllocString(aib->logEntries[i % AI_MAX_LOG_ENTRIES]->logEntry);
			eaPush(&aid->logEntries, entry);
		}
	}

	estrDestroy(&estr);
}

void aiDebugUpdate(Entity* debugger, AIDebug* aid)
{
	int debuggedEntChanged = false;
	Entity *e = NULL;

	if(aid->settings.debugEntRef || aid->settings.updateSelected)
	{
		if(aid->settings.updateSelected==1 && aid->settings.debugEntRef != debugger->pChar->currentTargetRef)
		{
			debuggedEntChanged = true;
			aid->settings.debugEntRef = debugger->pChar->currentTargetRef;
			aiDebugEntRef = aider = aid->settings.debugEntRef;
		}
		else if(aid->settings.updateSelected==2 && aid->settings.debugEntRef != debugger->pChar->erTargetDual)
		{
			debuggedEntChanged = true;
			aid->settings.debugEntRef = debugger->pChar->erTargetDual;
			aiDebugEntRef = aider = aid->settings.debugEntRef;
		}

		e = entFromEntityRef(entGetPartitionIdx(debugger), aid->settings.debugEntRef);

		if(e)
		{
			aiDebugTeam = aiTeamGetAmbientTeam(e, e->aibase);
			aiDebugCombatTeam = aiTeamGetCombatTeam(e, e->aibase);
		}
	}

	memcpy(aid->settings.logSettings, logsettings, sizeof(logsettings));

	if(aid->settings.layerFSMName)
	{
		GameLayerFSM *layerFSM = layerfsm_GetByName(aid->settings.layerFSMName, NULL);

		StructDeInit(parse_AIDebug, aid);

		if(!layerFSM)
			return;

		aid->settings.layerFSMName = allocAddString(layerFSM->pcName);

		aiDebugFillStructLayerFSM(debugger, layerFSM, aid);
	}
	else if(aid->settings.pfsmName)
	{
		PlayerFSM *pfsm;

		gslAddDebugEntityToLinkByRef(entGetClientLink(debugger), aiDebugEntRef);

		StructDeInit(parse_AIDebug, aid);

		if(!e || entGetFlagBits(e) & ENTITYFLAG_DESTROY)
			return;

		pfsm = pfsm_GetByName(e, aid->settings.pfsmName);
		if(!pfsm)
			return;

		aiDebugFillStructPlayerFSM(debugger, pfsm, aid);
	}
	else
	{
		gslAddDebugEntityToLinkByRef(entGetClientLink(debugger), aid->settings.debugEntRef);

		e = entFromEntityRef(entGetPartitionIdx(debugger), aid->settings.debugEntRef);

		StructDeInit(parse_AIDebug, aid);

		if(!e || entGetFlagBits(e) & ENTITYFLAG_DESTROY)
			return;

		aiDebugFillStructEntity(debugger, e, e->aibase, aid, debuggedEntChanged);
	}

	entity_SetDirtyBit(debugger, parse_Player, debugger->pPlayer, false);
}

EntityRef aiDebugEntRef = 0;
EntityRef aider = 0;
AITeam* aiDebugTeam = NULL;
AITeam* aiDebugCombatTeam = NULL;

NameList *pAIDebugTargetBucket;
NameList *pAIDebugLayerFSMBucket;
NameList *pAIDebugPlayerFSMBucket;

AUTO_RUN;
void aiDebugInitTargetBucket(void)
{
	pAIDebugTargetBucket = CreateNameList_Bucket();
	NameList_Bucket_AddName(pAIDebugTargetBucket, "selected");
	NameList_Bucket_AddName(pAIDebugTargetBucket, "selected2");

	pAIDebugLayerFSMBucket = layerfsm_GetNameList();
	pAIDebugPlayerFSMBucket = pfsmGetNameList();
}

AUTO_COMMAND ACMD_NAME(aiDebugLayerFSM) ACMD_ACCESSLEVEL(7);
void aiDebugLayerFSM(Entity* e, ACMD_NAMELIST(pAIDebugLayerFSMBucket) const char* layerFSM)
{
	PlayerDebug* debugInfo;
	if(!layerfsm_LayerFSMExists(layerFSM, NULL))
	{
		debugInfo = entGetPlayerDebug(e, false);
		if(debugInfo && debugInfo->aiDebugInfo)
		{
			StructDestroy(parse_AIDebug, debugInfo->aiDebugInfo);
			debugInfo->aiDebugInfo = NULL;
		}
		return;
	}
	
	debugInfo = entGetPlayerDebug(e, true);

	if(!debugInfo)
		return;

	if(!debugInfo->aiDebugInfo)
		debugInfo->aiDebugInfo = StructAlloc(parse_AIDebug);

	debugInfo->aiDebugInfo->settings.pfsmName = NULL;
	debugInfo->aiDebugInfo->settings.debugEntRef = 0;
	debugInfo->aiDebugInfo->settings.updateSelected = 0;

	debugInfo->aiDebugInfo->settings.layerFSMName = allocAddString(layerFSM);
	debugInfo->aiDebugInfo->settings.flags |= AI_DEBUG_FLAG_BASIC_INFO;

	aiDebugEntRef = aider = 0;
	aiDebugTeam = NULL;
	ClientCmd_aiDebugView(e, true);
	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
}

AUTO_COMMAND ACMD_NAME(AIForceTransitionLayerFSM);
void aiForceTransitionLayerFSM(Entity *e, ACMD_NAMELIST(pAIDebugLayerFSMBucket) const char* layerFSM, int level, int transition)
{
	GameLayerFSM *fsm = layerfsm_GetByName(layerFSM, NULL);
	int iPartitionIdx = entGetPartitionIdx(e);
	FSMContext *pFSMContext;
	
	if(!fsm)
		return;

	pFSMContext = layerfsm_GetFSMContext(fsm, iPartitionIdx);
	if(pFSMContext)
	{
		pFSMContext->debugTransitionLevel = level;
		pFSMContext->debugTransition = transition;
	}
}

AUTO_COMMAND ACMD_NAME(aiDebugPlayerFSM) ACMD_ACCESSLEVEL(7);
void aiDebugPlayerFSM(Entity *e, ACMD_NAMELIST(pAIDebugTargetBucket) const char* target, ACMD_NAMELIST(pAIDebugPlayerFSMBucket) const char* pfsmName)
{
	Entity* debugEnt;
	PlayerDebug* debugInfo;
	EntityRef debugRef;

	if(!pfsm_PlayerFSMExists(pfsmName) || !entGetClientTarget(e, target, &debugRef))
	{
		debugInfo = entGetPlayerDebug(e, false);

		if(debugInfo)
			StructDestroySafe(parse_AIDebug, &debugInfo->aiDebugInfo);
		return;
	}

	debugInfo = entGetPlayerDebug(e, true);

	if(!debugInfo)
		return;

	if(!debugInfo->aiDebugInfo)
		debugInfo->aiDebugInfo = StructAlloc(parse_AIDebug);

	debugInfo->aiDebugInfo->settings.debugEntRef = debugRef;
	debugInfo->aiDebugInfo->settings.pfsmName = allocAddString(pfsmName);
	debugInfo->aiDebugInfo->settings.layerFSMName = NULL;
	debugInfo->aiDebugInfo->settings.flags |= AI_DEBUG_FLAG_BASIC_INFO;

	if(!stricmp(target, "selected"))
		debugInfo->aiDebugInfo->settings.updateSelected = 1;
	else if(!stricmp(target, "selected2"))
		debugInfo->aiDebugInfo->settings.updateSelected = 2;
	else
		debugInfo->aiDebugInfo->settings.updateSelected = 0;

	aiDebugEntRef = aider = debugInfo->aiDebugInfo->settings.debugEntRef;
	if(debugEnt = entFromEntityRefAnyPartition(aiDebugEntRef))
	{
		aiDebugTeam = aiTeamGetAmbientTeam(debugEnt, debugEnt->aibase);
		aiDebugCombatTeam = aiTeamGetCombatTeam(debugEnt, debugEnt->aibase);
	}
	ClientCmd_aiDebugView(e, true);
	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
}

AUTO_COMMAND ACMD_NAME(aiDebugEnt) ACMD_ACCESSLEVEL(7);
void aiDebugEnt(Entity* e, ACMD_NAMELIST(pAIDebugTargetBucket) const char* target)
{
	Entity* debugEnt;
	EntityRef debugRef;
	PlayerDebug* debugInfo;
	U32 selected = !stricmp(target, "selected") || !stricmp(target, "selected2");

	if(!entGetClientTarget(e, target, &debugRef) && !selected)
	{
		debugInfo = entGetPlayerDebug(e, false);
		if(debugInfo && debugInfo->aiDebugInfo)
		{
			StructDestroy(parse_AIDebug, debugInfo->aiDebugInfo);
			debugInfo->aiDebugInfo = NULL;
		}
		return;
	}

	debugInfo = entGetPlayerDebug(e, true);

	if(!debugInfo)
		return;

	if(!debugInfo->aiDebugInfo)
		debugInfo->aiDebugInfo = StructAlloc(parse_AIDebug);

	debugInfo->aiDebugInfo->settings.pfsmName = NULL;
	debugInfo->aiDebugInfo->settings.layerFSMName = NULL;
	debugInfo->aiDebugInfo->settings.debugEntRef = debugRef;
	debugInfo->aiDebugInfo->settings.flags |= AI_DEBUG_FLAG_BASIC_INFO;
	
	if(!stricmp(target, "selected"))
		debugInfo->aiDebugInfo->settings.updateSelected = 1;
	else if(!stricmp(target, "selected2"))
		debugInfo->aiDebugInfo->settings.updateSelected = 2;
	else
		debugInfo->aiDebugInfo->settings.updateSelected = 0;
	
	aiDebugEntRef = aider = debugInfo->aiDebugInfo->settings.debugEntRef;
	if(debugEnt = entFromEntityRefAnyPartition(aiDebugEntRef))
	{
		aiDebugTeam = aiTeamGetAmbientTeam(debugEnt, debugEnt->aibase);
		aiDebugCombatTeam = aiTeamGetCombatTeam(debugEnt, debugEnt->aibase);
	}
	ClientCmd_aiDebugView(e, true);
	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void aiDebugUpdateMode(Entity *e, int selected)
{
	PlayerDebug *debugInfo = entGetPlayerDebug(e, false);

	if(!debugInfo)
		return;

	if(!debugInfo->aiDebugInfo)
		return;

	debugInfo->aiDebugInfo->settings.updateSelected = selected;
}

AUTO_COMMAND;
void aiDebugSetFlags(Entity* e, AIDebugFlags newFlags)
{
	PlayerDebug* debugInfo = entGetPlayerDebug(e, !!newFlags);

	if(!debugInfo)
		return;

	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);

	if(newFlags && !debugInfo->aiDebugInfo)
		debugInfo->aiDebugInfo = StructAlloc(parse_AIDebug);

	debugInfo->aiDebugInfo->settings.flags = newFlags;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void aiDebugToggleFlags(Entity* e, ACMD_NAMELIST(AIDebugFlagsEnum, STATICDEFINE) const char* flagname)
{
	AIDebugFlags toggleFlags = StaticDefineIntGetInt(AIDebugFlagsEnum, flagname);
	PlayerDebug* debugInfo;

	if(toggleFlags == -1)
		return;

	debugInfo = entGetPlayerDebug(e, true);

	devassert(debugInfo);

	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);

	if(!debugInfo->aiDebugInfo)
		debugInfo->aiDebugInfo = StructAlloc(parse_AIDebug);

	debugInfo->aiDebugInfo->settings.flags ^= toggleFlags;
}

U32 logsettings[AI_LOG_COUNT] = {0, 0, 0, 0};
U32* logtags[AI_LOG_COUNT] = {NULL};

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void aiDebugLogEnable(ACMD_NAMELIST(AILogTypeEnum, STATICDEFINE) const char* logtype, U32 loglevel)
{
	static int cleared = 0;
	AILogType type = StaticDefineIntGetInt(AILogTypeEnum, logtype);

	if(!cleared)
	{
		cleared = 1;
		ZeroArray(logsettings);
	}

	if(type==-1 && strIsNumeric(logtype))
		type = atoi(logtype);

	if(type == -1 || type >= AI_LOG_COUNT)
		return;

	if(!logtags[type])
	{
		int count = 0;
		switch(type)
		{
			xcase AI_LOG_MOVEMENT: {
				count = AIMLT_COUNT;
			}
			xcase AI_LOG_FSM: {
				count = AIFLT_COUNT;
			}
			xcase AI_LOG_EXPR_FUNC: {
				count = AIELT_COUNT;
			}
			xcase AI_LOG_COMBAT: {
				count = AICLT_COUNT;
			}
			xcase AI_LOG_TRACE: {
				count = AITLT_COUNT;
			}
		}
		logtags[type] = calloc(count, sizeof(U32));
		memset(logtags[type], 0x1, count*sizeof(U32));
	}

	logsettings[type] = (1 << (loglevel+1)) - 1;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
char* aiDebugTagToggle(ACMD_NAMELIST(AILogTypeEnum, STATICDEFINE) const char* logtype, const char* tagtype)
{
	AILogType type = StaticDefineIntGetInt(AILogTypeEnum, logtype);
	static char* estrRes = NULL;
	int tag = -1;

	if(type==-1 || type>=AI_LOG_COUNT)
	{
		estrPrintf(&estrRes, "Unknown log type: %s", logtype);
		return estrRes;
	}
	
	if(!logtags[type])
	{
		estrPrintf(&estrRes, "Logging not initiated for %s - run aidebuglogenable %s first", logtype, logtype);
		return estrRes;
	}

	switch(type)
	{
		xcase AI_LOG_MOVEMENT: {
			tag = StaticDefineIntGetInt(AIMovementLogTagsEnum, tagtype);
		}
		xcase AI_LOG_COMBAT: {
			tag = StaticDefineIntGetInt(AICombatLogTagsEnum, tagtype);
		}
	}

	if(tag==-1)
	{
		estrPrintf(&estrRes, "Unknown tag %s of %s", tagtype, logtype);
		return estrRes;
	}

	logtags[type][tag] = !logtags[type][tag];

	estrPrintf(&estrRes, "Tag %s of %s %sabled", tagtype, logtype, logtags[type][tag] ? "en" : "dis");

	return estrRes;
}

static ThreadSafeMemoryPool AIDebugLogEntryPool;

AUTO_RUN;
void aiDebugInitPool(void)
{
	threadSafeMemoryPoolInit(&AIDebugLogEntryPool, 32, sizeof(AIDebugLogEntry), "AiDebugLogEntry");
}

AIDebugLogEntry* aiDebugLogEntryCreate()
{
	AIDebugLogEntry* entry;

	entry = threadSafeMemoryPoolAlloc(&AIDebugLogEntryPool);

	return entry;
}

void aiDebugLogEntryDestroy(AIDebugLogEntry* entry)
{
	threadSafeMemoryPoolFree(&AIDebugLogEntryPool, entry);
}

AIDebugLogEntry* aiDebugLogEntryGetNext(Entity* e)
{
	if(eaSize(&e->aibase->logEntries) < AI_MAX_LOG_ENTRIES)
	{
		AIDebugLogEntry* entry = aiDebugLogEntryCreate();
		eaPush(&e->aibase->logEntries, entry);
		e->aibase->curLogEntry++;
		return entry;
	}
	else
		return e->aibase->logEntries[e->aibase->curLogEntry++ % AI_MAX_LOG_ENTRIES];
}

void aiDebugLogEntryFill(int partitionIdx, AIDebugLogEntry* entry, AILogType logtype, U32 loglevel, U32 logtag, const char* filename,
						 U32 lineNumber, const char* formatStr, ...)
{
	va_list ap;
	int charsPrinted;

	PERFINFO_AUTO_START_FUNC();

	entry->logType = logtype;
	entry->logLevel = loglevel;
	entry->logTag = logtag;
	entry->time = ABS_TIME_PARTITION(partitionIdx);

	va_start(ap, formatStr);
	charsPrinted = vsprintf(entry->logEntry, formatStr, ap);
	va_end(ap);

	devassertmsg(!IsDebuggerPresent() || charsPrinted != -1,
		"Log entry string too small for debug line");

	if(aiDebugLogToFile)
		filelog_printf("aiCombatLog", "%.3f %s", ABS_TIME_TO_SEC(ABS_TIME_PARTITION(partitionIdx)), entry->logEntry);

	PERFINFO_AUTO_STOP();
}

void aiDebugLogFlushEntries(Entity* e, AIVarsBase* aib, AIDebugLogEntry*** logEntries)
{
	int i, n = eaSize(logEntries);
	for(i = 0; i < n; i++)
	{
		AIDebugLogEntry* entry = aiDebugLogEntryGetNext(e);
		memcpy(entry, (*logEntries)[i], sizeof(*entry));
	}
	eaClearEx(logEntries, aiDebugLogEntryDestroy);
}

void aiDebugLogClear(Entity* e)
{
	eaDestroyEx(&e->aibase->logEntries, aiDebugLogEntryDestroy);
	e->aibase->curLogEntry = 0;
}

AUTO_COMMAND;
void aiDebugLogClearAll(void)
{
	EntityIterator* iter;
	Entity* e;

	iter = entGetIteratorAllTypesAllPartitions(0, 0);

	while(e = EntityIteratorGetNext(iter))
		aiDebugLogClear(e);

	EntityIteratorRelease(iter);
}

AUTO_COMMAND ACMD_NAME(SetFSM) ACMD_LIST(gEntConCmdList);
void entConSetFSM(Entity *e, ACMD_NAMELIST("FSM", REFDICTIONARY) const char *fsmName)
{
	aiSetFSMByName(e, fsmName);
}

AUTO_COMMAND;
void aiAnimListDebugSetAnim(int ref, int which, int enable,  ACMD_NAMELIST("AIAnimList", REFDICTIONARY) const char *anim, int time)
{
	Entity *e = entFromEntityRefAnyPartition(ref);

	if(which<0 || which>=10)
		return;

	if(e)
	{
		AIVarsBase *aib = e->aibase;

		if(!aib->aidAnimState)
			aib->aidAnimState = StructCreate(parse_AIDebugAnimState);

		aib->aidAnimState->animSettings[which].enabled = enable;
		aib->aidAnimState->animSettings[which].anim = allocAddString(anim);
		aib->aidAnimState->animSettings[which].time = time;

		aib->aidAnimState->curSetting = 0;
		aib->aidAnimState->nextSwitch = 0;
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(DoAnimDebug);
void exprFuncDoAnimDebug(ACMD_EXPR_SELF Entity *e)
{
	AIVarsBase *aib = e->aibase;
	AIAnimList *al;
	AIDebugAnimSetting *s;
	int cur;
	int offset;
	static CommandQueue *cmdQueue = NULL;
	int partitionIdx = entGetPartitionIdx(e);

	if(!aib->aidAnimState)
		return;

	if(aib->aidAnimState->nextSwitch && aib->aidAnimState->nextSwitch > ABS_TIME_PARTITION(partitionIdx))
		return;

	CommandQueue_ExecuteAllCommands(cmdQueue);

	// Pre-setup
	cur = aib->aidAnimState->curSetting;
	s = &aib->aidAnimState->animSettings[cur];
	al = RefSystem_ReferentFromString(g_AnimListDict, s->anim);
	aiAnimListSet(e, al, &cmdQueue);

	// Setup for next tick
	aib->aidAnimState->nextSwitch = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(s->time);
	for(offset=1; offset<=10; offset++)
	{
		int next = (cur+offset)%10;

		s = &aib->aidAnimState->animSettings[next];

		if(s->enabled && s->anim && s->anim[0] && s->time>0)
		{
			aib->aidAnimState->curSetting = next;
			break;
		}
	}
}

AUTO_RUN;
void aiAnimListDebugSetup(void)
{
	static ExprContext *context;

	if(!context)
		context = exprContextCreate();
}

// ---------------------------------------------------------------------------------------------------------
// AI Aggro Debug
// ---------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------
void aiDebug_SetDebugStatusEntryDefault(AIDebugStatusTableEntry* debugStatus, 
										Entity *aiEnt, Entity* statusBE, int bExternal)
{
	if(!bExternal)
	{
		debugStatus->name = StructAllocString(entGetLocalName(statusBE));
		debugStatus->entRef = entGetRef(statusBE);
		debugStatus->index = entGetRefIndex(statusBE);
	}
	else
	{
		debugStatus->name = StructAllocString(entGetLocalName(aiEnt));
		debugStatus->entRef = entGetRef(aiEnt);
		debugStatus->index = entGetRefIndex(aiEnt);
	}
}



// ------------------------------------------------------------------------------------------------------------
void aiDebugCopyDangerValues(AIStatusTableEntryDangerValues *pDangerVal, AIStatusTableEntry* status)
{
	pDangerVal->damageToMeVal			= status->dangerVal.damageToMeVal;
	pDangerVal->damageToFriendsVal		= status->dangerVal.damageToFriendsVal;
	pDangerVal->distFromMeVal			= status->dangerVal.distFromMeVal;
	pDangerVal->personalSpaceVal		= status->dangerVal.personalSpaceVal;
	pDangerVal->distFromGuardPointVal	= status->dangerVal.distFromGuardPointVal;
	pDangerVal->statusToMeVal			= status->dangerVal.statusToMeVal;
	pDangerVal->statusToFriendsVal		= status->dangerVal.statusToFriendsVal;
	pDangerVal->healingEnemiesVal		= status->dangerVal.healingEnemiesVal;
	pDangerVal->targetStickinessVal		= status->dangerVal.targetStickinessVal;
	pDangerVal->teamOrdersVal			= status->dangerVal.teamOrdersVal;
	pDangerVal->threatToMeVal			= status->dangerVal.threatToMeVal;
	pDangerVal->threatToFriendsVal		= status->dangerVal.threatToFriendsVal;
	pDangerVal->targetingRatingExprVal	= status->dangerVal.targetingRatingExprVal;
	pDangerVal->leashDecayScaleVal		= status->dangerVal.leashDecayScaleVal;
}

// ---------------------------------------------------------------------------------------------------------
static void aiDebug_Aggro1CreateTableHeaders(AIDebugAggroTableHeader ***peaDebugAggroHeaders)
{
	eaClearEx(peaDebugAggroHeaders, NULL);

	{
		AIDebugAggroTableHeader *pHeader;
#define CREATE_PUSH_AIDebugAggroBucketInfo(str)					\
		pHeader = StructAlloc(parse_AIDebugAggroTableHeader);	\
		eaPush(peaDebugAggroHeaders, pHeader);					\
		pHeader->pchName = allocAddString(str);

		CREATE_PUSH_AIDebugAggroBucketInfo("sticki");
		CREATE_PUSH_AIDebugAggroBucketInfo("DistMe");
		CREATE_PUSH_AIDebugAggroBucketInfo("dmgMe");
		CREATE_PUSH_AIDebugAggroBucketInfo("dmgFr");
		CREATE_PUSH_AIDebugAggroBucketInfo("healEn");

		CREATE_PUSH_AIDebugAggroBucketInfo("thrtMe");
		CREATE_PUSH_AIDebugAggroBucketInfo("thrtFr");
		
		CREATE_PUSH_AIDebugAggroBucketInfo("statMe");
		CREATE_PUSH_AIDebugAggroBucketInfo("statFr");

		CREATE_PUSH_AIDebugAggroBucketInfo("targEx");

		CREATE_PUSH_AIDebugAggroBucketInfo("PSpace");
		CREATE_PUSH_AIDebugAggroBucketInfo("DistGP");

		CREATE_PUSH_AIDebugAggroBucketInfo("leashSc");

#undef CREATE_PUSH_AIDebugAggroBucketInfo
	}
}

// ---------------------------------------------------------------------------------------------------------
static void aiDebug_Aggro1PushDangerValues(AIDebugStatusTableEntry *pEntry, 
										   AIStatusTableEntryDangerValues *pDangerValues)
{
	AIDebugAggroBucket *pBucket;

#define CREATE_PUSH_AIDebugAggroBucket(value)				\
	pBucket = StructAlloc(parse_AIDebugAggroBucket);		\
	eaPush(&pEntry->eaAggroBuckets, pBucket);			\
	pBucket->fValue = value;		

	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->targetStickinessVal);
	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->distFromMeVal);
	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->damageToMeVal);
	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->damageToFriendsVal);
	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->healingEnemiesVal);

	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->threatToMeVal);
	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->threatToFriendsVal);

	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->statusToMeVal);
	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->statusToFriendsVal);

	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->targetingRatingExprVal);

	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->personalSpaceVal);
	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->distFromGuardPointVal);

	CREATE_PUSH_AIDebugAggroBucket(pDangerValues->leashDecayScaleVal);

#undef CREATE_PUSH_AIDebugAggroBucket


}

// ---------------------------------------------------------------------------------------------------------
static void aiDebugStatusTableEntry_FillInDefault(AIDebugStatusTableEntry *pEntry, 
												 Entity *e, AIStatusTableEntry* status,
												 bool bIngoreName)
{
	AITeamStatusEntry *teamStatus = status->combatStatus ? status->combatStatus : status->ambientStatus;
	if (!bIngoreName)
		pEntry->name = StructAllocString(entGetLocalName(e));
	pEntry->entRef = entGetRef(e);
	pEntry->index = entGetRefIndex(e);
	pEntry->legalTarget = teamStatus->legalTarget;
	pEntry->inFrontArc = status->inFrontArc;
	pEntry->totalBaseDangerVal = status->totalBaseDangerVal;
}


// ---------------------------------------------------------------------------------------------------------
static void aiDebug_Aggro1FillInDebugStatusTableEntry(AIDebugStatusTableEntry ***peaDebugEntries, 
													   AIStatusTableEntry* status, Entity *ent, 
													   const AIConfig *config)
{
	// Row one = absolute values
	{
		AIDebugStatusTableEntry *pDebugStatusEntry;

		AIStatusTableEntryDangerValues dangerValues = {0};
		dangerValues.damageToMeVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_DAMAGE];
		dangerValues.damageToFriendsVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_DAMAGE];
		dangerValues.distFromMeVal = -1;
		dangerValues.personalSpaceVal = -1;
		dangerValues.distFromGuardPointVal = -1;
		dangerValues.statusToMeVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_STATUS];
		dangerValues.statusToFriendsVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_STATUS];
		dangerValues.healingEnemiesVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_HEALING] * 0.25;
		dangerValues.targetStickinessVal = -1;
		dangerValues.teamOrdersVal = -1;
		dangerValues.threatToMeVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_THREAT];
		dangerValues.threatToFriendsVal = status->trackedAggroCounts[AI_NOTIFY_TARGET_FRIENDS][AI_NOTIFY_TYPE_THREAT];
		dangerValues.targetingRatingExprVal = -1;

		// Row one = absolute values
		pDebugStatusEntry = StructAlloc(parse_AIDebugStatusTableEntry);
		eaPush(peaDebugEntries, pDebugStatusEntry);
		
		aiDebugStatusTableEntry_FillInDefault(pDebugStatusEntry, ent, status, false);
		pDebugStatusEntry->totalBaseDangerVal = -1.f;
		aiDebug_Aggro1PushDangerValues(pDebugStatusEntry, &dangerValues);
	}
	
	// Row two = scale values
	{
		AIDebugStatusTableEntry *pDebugStatusEntry;
		AIStatusTableEntryDangerValues dangerValues = {0};
		
		aiDebugCopyDangerValues(&dangerValues, status);

		dangerValues.damageToMeVal /= config->dangerFactors.damageToMe ? config->dangerFactors.damageToMe : 1;
		dangerValues.damageToFriendsVal /= config->dangerFactors.damageToFriends ? config->dangerFactors.damageToFriends : 1;
		dangerValues.distFromMeVal /= config->dangerFactors.distFromMe ? config->dangerFactors.distFromMe : 1;
		dangerValues.personalSpaceVal /= config->dangerFactors.personalSpace ? config->dangerFactors.personalSpace : 1;
		dangerValues.distFromGuardPointVal /= config->dangerFactors.distFromGuardPoint ? config->dangerFactors.distFromGuardPoint : 1;
		dangerValues.statusToMeVal /= config->dangerFactors.statusToMe ? config->dangerFactors.statusToMe : 1;
		dangerValues.statusToFriendsVal /= config->dangerFactors.statusToFriends ? config->dangerFactors.statusToFriends : 1;
		dangerValues.healingEnemiesVal /= config->dangerFactors.healingEnemies ? config->dangerFactors.healingEnemies : 1;
		dangerValues.targetStickinessVal /= config->dangerFactors.targetStickiness ? config->dangerFactors.targetStickiness : 1;
		dangerValues.teamOrdersVal /= config->dangerFactors.teamOrders ? config->dangerFactors.teamOrders : 1;
		dangerValues.threatToMeVal /= config->dangerFactors.threatToMe ? config->dangerFactors.threatToMe : 1;
		dangerValues.threatToFriendsVal /= config->dangerFactors.threatToFriends ? config->dangerFactors.threatToFriends : 1;
		dangerValues.targetingRatingExprVal /= config->dangerFactors.targetingRatingExpr ? config->dangerFactors.targetingRatingExpr : 1;

		
		pDebugStatusEntry = StructAlloc(parse_AIDebugStatusTableEntry);
		eaPush(peaDebugEntries, pDebugStatusEntry);
	
		aiDebugStatusTableEntry_FillInDefault(pDebugStatusEntry, ent, status, false);
		pDebugStatusEntry->totalBaseDangerVal = -1.f;
		aiDebug_Aggro1PushDangerValues(pDebugStatusEntry, &dangerValues);
	}

	// Row three = unscaled values
	{
		AIDebugStatusTableEntry *pDebugStatusEntry;
		AIStatusTableEntryDangerValues dangerValues = {0};
		
		aiDebugCopyDangerValues(&dangerValues, status);

		pDebugStatusEntry = StructAlloc(parse_AIDebugStatusTableEntry);
		eaPush(peaDebugEntries, pDebugStatusEntry);
	
		aiDebugStatusTableEntry_FillInDefault(pDebugStatusEntry, ent, status, false);
		aiDebug_Aggro1PushDangerValues(pDebugStatusEntry, &dangerValues);
	}

}

// ------------------------------------------------------------------------------------------------------------
// Aggro 2
// ------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------
static void aiDebug_FillAggro2DebugStatusEntry(	AIDebugStatusTableEntry ***peaDebugEntries, 
												AIStatusTableEntry* status, 
												Entity *ent, 
												const AIConfig *config)
{
	// non-scaled
	{
		AIDebugStatusTableEntry *pDebugStatus = StructAlloc(parse_AIDebugStatusTableEntry);
		eaPush(peaDebugEntries, pDebugStatus);
		aiDebugStatusTableEntry_FillInDefault(pDebugStatus, ent, status, true);
		pDebugStatus->name = strdup(" non-scaled");
		aiAggro2_FillInDebugStatusTableEntry(ent, config, pDebugStatus, status, false, false);
	}
	
	{
		AIDebugStatusTableEntry *pDebugStatus = StructAlloc(parse_AIDebugStatusTableEntry);
		eaPush(peaDebugEntries, pDebugStatus);
		aiDebugStatusTableEntry_FillInDefault(pDebugStatus, ent, status, true);
		pDebugStatus->name = strdup(" post-scale");
		aiAggro2_FillInDebugStatusTableEntry(ent, config, pDebugStatus, status, false, true);
	}
	// post decay
	{
		AIDebugStatusTableEntry *pDebugStatus = StructAlloc(parse_AIDebugStatusTableEntry);
		eaPush(peaDebugEntries, pDebugStatus);
		aiDebugStatusTableEntry_FillInDefault(pDebugStatus, ent, status, true);
		{
			Entity *statusE = entFromEntityRefAnyPartition(status->entRef);
			if (statusE)
				pDebugStatus->name = StructAllocString(statusE->debugName);
		}
		
		aiAggro2_FillInDebugStatusTableEntry(ent, config, pDebugStatus, status, true, true);
	}
	
}

AUTO_COMMAND;
void aiTestPosition(Entity *e)
{
	Vec3 pos;

	entGetPos(e, pos);
	beaconIsPositionValid(entGetPartitionIdx(e), pos, entGetPrimaryCapsule(e));
}
