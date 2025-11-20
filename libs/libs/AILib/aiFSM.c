#include "aiConfig.h"
#include "aiJobs.h"
#include "aiLib.h"
#include "aiMessages.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiTeam.h"

#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "Character_target.h"
#include "CostumeCommonEntity.h"
#include "entCritter.h"
#include "EntityExtern.h"
#include "EntityGrid.h"
#include "EntityLib.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "EntityMovementTactical.h"
#include "EString.h"
#include "ExpressionPrivate.h"
#include "GameEvent.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventTracker.h"
#include "gslInteractionManager.h"
#include "gslMapState.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "PowerModes.h"
#include "PowerSubtarget.h"
#include "PowersMovement.h"
#include "rand.h"
#include "species_common.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "team.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "WorldColl.h"

#include "aiStruct_h_ast.h"
#include "../GameClientLib/Autogen/GameEvent_h_ast.h" // for parse_GameEvent
#include "Autogen/GameClientLib_autogen_ClientCmdWrappers.h"

extern const char *g_EncounterVarName;
extern const char *g_Encounter2VarName;

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCDoXPerTeam(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* actionDesc, S32 times, ACMD_EXPR_SUBEXPR_IN subExpr)
{
	MultiVal answer = {0};
	
	if(times < 0)
		return ExprFuncReturnError;

	// exprevaluatesubexpr will set the static check error bit if it encounters an error...
	exprEvaluateSubExprStaticCheck(subExpr, context, context, &answer, false);

	if(exprContextCheckStaticError(context))
		return ExprFuncReturnError;

	return ExprFuncReturnFinished;
}

// Every time this expression gets called, it will check its team to see if the tag <actionTag>
// has been executed fewer than <times> times for this team, and if it has, it executes the expression
// passed in as subExpr.
// NOTE: the actual expression can be different even if the action tag is the same for different critters,
// and it is perfectly valid to have multiple entries for DoXPerTeam in one FSM
// Example: DoXPerTeam("YellAttack", 3, \{SayText("The will of Attracto-Man is absolute!", 3)\})
AUTO_EXPR_FUNC(ai) ACMD_NAME(DoXPerTeam) ACMD_EXPR_STATIC_CHECK(exprFuncSCDoXPerTeam);
ExprFuncReturnVal exprFuncDoXPerTeam(ACMD_EXPR_SELF Entity* e, ExprContext* context, const char* actionTag, S32 times, ACMD_EXPR_SUBEXPR_IN subExpr)
{
	MultiVal answer = {0};
	AITeam* combatTeam;

	combatTeam = aiTeamGetCombatTeam(e, e->aibase);
	if(aiTeamActionGetCount(combatTeam, actionTag) < times)
	{
		exprEvaluateSubExpr(subExpr, context, context, &answer, false);
		aiTeamActionIncrementCount(combatTeam, actionTag);
	}

	return ExprFuncReturnFinished;
}

void exprFuncGetStatusEntsHelper(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, int all, int dead, int onlyLegalTargets)
{
	int i;

	for(i=eaSize(&e->aibase->statusTable)-1; i>=0; i--)
	{
		AIStatusTableEntry *status = e->aibase->statusTable[i];
		AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, e->aibase, status);
		Entity *statusE = entFromEntityRef(iPartitionIdx, status->entRef);

		if(statusE && 
			(dead || aiIsEntAlive(statusE)) &&
			(all || !exprFuncHelperShouldExcludeFromEntArray(statusE)) &&
			(!onlyLegalTargets || (teamStatus && teamStatus->legalTarget)))
		{
			eaPush(entsOut, statusE);
		}
	}
}

// Gets all the entities in the status table, which is all the entities that have aggroed or annoyed
// or just gotten to close to the critter.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetStatusEntsLegalTargets);
void exprFuncGetStatusEntsLegalTargets(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	exprFuncGetStatusEntsHelper(e, iPartitionIdx, entsOut, false, false, true);
}

// Gets all the entities in the status table, which is all the entities that have aggroed or annoyed
// or just gotten to close to the critter.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetStatusEnts);
void exprFuncGetStatusEnts(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	exprFuncGetStatusEntsHelper(e, iPartitionIdx, entsOut, false, false, false);
}

// Gets all the entities in the status table, which is all the entities that have aggroed or annoyed
// or just gotten to close to the critter, including untargetable etc.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetStatusEntsAll);
void exprFuncGetStatusEntsAll(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	exprFuncGetStatusEntsHelper(e, iPartitionIdx, entsOut, true, false, false);
}

// Gets all the entities in the status table, which is all the entities that have aggroed or annoyed
// or just gotten to close to the critter, even dead or untargetable ones.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetStatusEntsDeadAll);
void exprFuncGetStatusEntsDeadAll(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	exprFuncGetStatusEntsHelper(e, iPartitionIdx, entsOut, true, true, false);
}

void exprFuncGetEntArrayAttackersHelper(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_ENTARRAY_OUT entsOut, int all, int dead)
{
	int i, j;

	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* e = (*entsIn)[i];
		AIVarsBase* aib = e->aibase;

		for(j = eaSize(&aib->attackerList) - 1; j >= 0; j--)
		{
			Entity* attackerEnt = aib->attackerList[j];
			if(attackerEnt && 
				(dead || aiIsEntAlive(attackerEnt)) &&
				(all || !exprFuncHelperShouldExcludeFromEntArray(attackerEnt)))
			{
				eaPushUnique(entsOut, attackerEnt);
			}
		}
	}
}

// Get your daddy
AUTO_EXPR_FUNC(ai) ACMD_NAME(WhosYourDaddy);
void exprFuncWhosYourDaddy(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	AITeam* combatTeam = aiTeamGetAmbientTeam(e, e->aibase);
	eaPush(entsOut, aiTeamGetLeader(combatTeam));
}

// Get the current entity's team leader
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetTeamLeader);
void exprFuncGetTeamLeader(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	AITeam* combatTeam = aiTeamGetAmbientTeam(e, e->aibase);
	eaPush(entsOut, aiTeamGetLeader(combatTeam));
}

// Gets all the entities attacking anyone in the entity array
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetEntArrayAttackers);
void exprFuncGetEntArrayAttackers(ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	exprFuncGetEntArrayAttackersHelper(entsIn, entsOut, false, false);
}

// Gets all the entities attacking anyone in the entity array including untargetable etc
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetEntArrayAttackersAll);
void exprFuncGetEntArrayAttackersAll(ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	exprFuncGetEntArrayAttackersHelper(entsIn, entsOut, true, false);
}

// Gets all the entities attacking anyone in the entity array including dead and untargetable ents
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetEntArrayAttackersDeadAll);
void exprFuncGetEntArrayAttackersDeadAll(ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	exprFuncGetEntArrayAttackersHelper(entsIn, entsOut, true, true);
}

static int statusEntSortByDanger(const AIStatusTableEntry **left, const AIStatusTableEntry **right)
{
	return (*left)->totalBaseDangerVal > (*right)->totalBaseDangerVal ? -1 : 1;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetStatusEntsSortedByDanger);
void exprFuncGetStatusEntsSortedByDanger(ACMD_EXPR_SELF Entity *be, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	eaQSort(be->aibase->statusTable, statusEntSortByDanger);
	exprFuncGetStatusEnts(be, iPartitionIdx, entsOut);
}

typedef struct ExprSortInfo {
	ExprContext *context;
	AcmdType_ExprSubExpr* subExpr;
	ExprFuncReturnVal returnVal;
	const char **errString;
	int needsStatus;
	Entity *statusFrom;
} ExprSortInfo;

ExprSortInfo entSortInfo;

int exprFuncEntSortExprHelper(const Entity **left, const Entity **right)
{
	MultiVal answerLeft = {0}, answerRight = {0};
	F32 diff;

	exprContextSetPointerVarPooledCached(entSortInfo.context, targetEntString, (Entity*)(*left),
										parse_Entity, false, true, &targetEntVarHandle);
	if(entSortInfo.needsStatus)
	{
		AIStatusTableEntry *status = aiStatusFind(entSortInfo.statusFrom, entSortInfo.statusFrom->aibase, (Entity*)(*left), false);

		if(status)
		{
			exprContextSetPointerVarPooledCached(entSortInfo.context, targetEntStatusString, status, parse_AIStatusTableEntry, 
												false, true, &targetEntStatusVarHandle);
		}
		else
		{
			*entSortInfo.errString = "Sort expression uses status but passed in entity without status.  Restrict to GetStatusEnts().";
			entSortInfo.returnVal = ExprFuncReturnError;
			return 0;
		}
	}
	exprEvaluateSubExpr(entSortInfo.subExpr, entSortInfo.context, entSortInfo.context, &answerLeft, false);

	if(entSortInfo.needsStatus)
	{
		AIStatusTableEntry *status = aiStatusFind(entSortInfo.statusFrom, entSortInfo.statusFrom->aibase, (Entity*)(*right), false);

		if(status)
		{
			exprContextSetPointerVarPooledCached(entSortInfo.context, targetEntStatusString, status, parse_AIStatusTableEntry, 
												false, true, &targetEntStatusVarHandle);
		}
		else
		{
			*entSortInfo.errString = "Sort expression uses status but passed in entity without status.  Restrict to GetStatusEnts().";
			entSortInfo.returnVal = ExprFuncReturnError;
			return 0;
		}
	}
	exprContextSetPointerVarPooledCached(entSortInfo.context, targetEntString, (Entity*)(*right),
										parse_Entity, false, true, &targetEntVarHandle);
	exprEvaluateSubExpr(entSortInfo.subExpr, entSortInfo.context, entSortInfo.context, &answerRight, false);

	if(answerLeft.type!=MULTI_INT && answerLeft.type!=MULTI_FLOAT &&
		answerRight.type!=MULTI_INT && answerRight.type!=MULTI_FLOAT)
	{
		*entSortInfo.errString = "Illegal sort expression return type: only numerics (int/float) allowed.";
		entSortInfo.returnVal = ExprFuncReturnError;
		return 0;
	}

	diff = (answerRight.type==MULTI_INT ? answerRight.intval : answerRight.floatval) -
		(answerLeft.type==MULTI_INT ? answerLeft.intval : answerLeft.floatval);

	if(diff > 0)
		return 1;
	else if(diff < 0)
		return -1;
	else
		return 0;
}

// Sorts an ent array based on the expression passed in.  Generally used with CropNthEnt.
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntSortExpr);
ExprFuncReturnVal exprFuncEntSortExpr(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_SUBEXPR_IN subExpr,
									  ACMD_EXPR_ERRSTRING_STATIC errString)
{
	static ExprContext* sortContext = NULL;

	if(!sortContext)
	{
		sortContext = exprContextCreate();
		exprContextSetSilentErrors(sortContext, true);
		//lokContext->silentErrors = 1; // TODO: remove this when it can be done more intelligently
		// (constant errors when trying to evaluate .pCritter.parentEncounter.staticEnc.basedef because
		// not all evaluated critters had it
		exprContextSetPointerVarPooledCached(sortContext, targetEntString, NULL, parse_Entity,
											false, true, &targetEntVarHandle);
		// TODO: this will fail if anything from the passed in context is used to evaluate the ents...
		// i don't think anyone is doing that yet though, and i don't have time to look at this right now
	}
	
	entSortInfo.context = sortContext;
	entSortInfo.subExpr = subExpr;
	entSortInfo.returnVal = ExprFuncReturnFinished;
	entSortInfo.needsStatus = 0;
	entSortInfo.statusFrom = NULL;
	entSortInfo.errString = errString;
	eaQSort((*entsInOut), exprFuncEntSortExprHelper);

	return entSortInfo.returnVal;
}

// Sorts an ent array based on the expression passed in, but allows the use of targetStatus object pathing.  
// Generally used with CropNthEnt.
// See also: [AIStatusTableEntry]
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntSortStatusExpr);
ExprFuncReturnVal exprFuncEntSortStatusExpr(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_SUBEXPR_IN subExpr,
											ACMD_EXPR_ERRSTRING_STATIC errString)
{
	static ExprContext* sortContext = NULL;

	if(!sortContext)
	{
		sortContext = exprContextCreate();
		exprContextSetSilentErrors(sortContext, true);
		//lokContext->silentErrors = 1; // TODO: remove this when it can be done more intelligently
		// (constant errors when trying to evaluate .pCritter.parentEncounter.staticEnc.basedef because
		// not all evaluated critters had it
		exprContextSetPointerVarPooledCached(sortContext, targetEntString, NULL, parse_Entity,
											false, true, &targetEntVarHandle);
		exprContextSetPointerVarPooledCached(sortContext, targetEntStatusString, NULL, parse_AIStatusTableEntry,
											false, true, &targetEntStatusVarHandle);
		// TODO: this will fail if anything from the passed in context is used to evaluate the ents...
		// i don't think anyone is doing that yet though, and i don't have time to look at this right now
	}

	entSortInfo.context = sortContext;
	entSortInfo.subExpr = subExpr;
	entSortInfo.returnVal = ExprFuncReturnFinished;
	entSortInfo.needsStatus = 1;
	entSortInfo.statusFrom = be;
	entSortInfo.errString = errString;
	eaQSort((*entsInOut), exprFuncEntSortExprHelper);

	return entSortInfo.returnVal;
}

// Returns a new ent array with all entities within this entity's proximity radius that it can
// perceive (does not take into account LOS and automatically excludes all untargetable ents etc)
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetProxEnts);
void exprFuncGetProxEnts(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	AIVarsBase* aib = e->aibase;
	int i;
	int partitionIdx = entGetPartitionIdx(e);

	aiUpdateProxEnts(e, aib);

	for(i = aib->proxEntsCount - 1; i >= 0; i--)
	{
		Entity* curE = aib->proxEnts[i].e;
		if(!exprFuncHelperShouldExcludeFromEntArray(curE))
			eaPush(entsOut, curE);
	}
}

// Returns a new ent array with all entities within this entity's proximity radius that it can
// perceive, including all ents within that range that are untargetable, unselectable and/or 
// invisible (does not take into account LOS)
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetProxEntsAll);
void exprFuncGetProxEntsAll(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	// NOTE: if this should ever be limited to "only part of the dangerous entities" it should
	// be done with entcrop functions, not yet another set of these (one safe function and one
	// dangerous function is how it should be set up imo) - RvP
	AIVarsBase* aib = e->aibase;
	int i;
	int partitionIdx = entGetPartitionIdx(e);

	aiUpdateProxEnts(e, aib);
	for(i = e->aibase->proxEntsCount - 1; i >= 0; i--)
		eaPush(entsOut, e->aibase->proxEnts[i].e);
}

// Returns a new ent array with all entities within this entity's proximity radius
// note: This lookup for dead ents is not cached, so use with care. 
// If this is needed more often, this can be optimized. 
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetProxEntsDead);
void exprFuncGetProxEntsDead(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Vec3 bePos;
	entGetPos(be, bePos);

	PERFINFO_AUTO_START("GetProxEntsDeadLookup", 1);
	
	entGridProximityLookupExEArray(entGetPartitionIdx(be), bePos, entsOut, be->aibase->proximityRadius, ENTITYFLAG_DEAD, 
						ENTITYFLAG_DONOTDRAW | ENTITYFLAG_DONOTSEND | ENTITYFLAG_IGNORE, be);
	
	PERFINFO_AUTO_STOP();
}

ExprFuncReturnVal exprFuncGetTeamEntsHelper(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut, int getAll, int getDead, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	AITeam* team = NULL;

	if(!e->aibase)
	{
		estrPrintf(errString, "Can't use GetTeamEnts* on subscribed entity.");
		return ExprFuncReturnError;
	}
	
	team = aiTeamGetCombatTeam(e, e->aibase);

	for(i = eaSize(&team->members) - 1; i >= 0; i--)
	{
		Entity* memberE = team->members[i]->memberBE;
		if((getAll || !exprFuncHelperShouldExcludeFromEntArray(memberE)) &&
			(getDead || aiIsEntAlive(memberE)))
		{
			eaPush(entsOut, memberE);
		}
	}

	return ExprFuncReturnFinished;
}

// Returns a new ent array with all entities from this entity's team, which is basically every
// entity in their encounter and all entities spawned by them (excludes dead and/or untargetable
// ents etc)
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(GetTeamEnts);
ExprFuncReturnVal exprFuncGetTeamEnts(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncGetTeamEntsHelper(e, entsOut, false, false, errString);
}

// Returns a new ent array with all entities from this entity's team, which is basically every
// entity in their encounter and all entities spawned by them (includes untargetable etc, but
// excludes dead ents)
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(GetTeamEntsAll);
ExprFuncReturnVal exprFuncGetTeamEntsAll(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncGetTeamEntsHelper(e, entsOut, true, false, errString);
}

// Returns a new ent array with all entities from this entity's team, which is basically every
// entity in their encounter and all entities spawned by them (includes all entities including 
// untargetable and dead ones)
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(GetTeamEntsDeadAll);
ExprFuncReturnVal exprFuncGetTeamEntsDeadAll(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncGetTeamEntsHelper(e, entsOut, true, true, errString);
}

// Returns the number of entities on a player's team; this may be more than returned by GetPlayerTeamEntsOnServer()
// because of teammates on other servers
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetPlayerTeamSize);
ExprFuncReturnVal exprFuncGetPlayerTeamSize(ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN playerIn, ACMD_EXPR_ERRSTRING errString)
{
	Team *team;
	Entity *ent;

	if(eaSize(playerIn)!=1)
	{
		estrPrintf(errString, "GetPlayerTeamEnts called with zero or more than one ent.");
		return ExprFuncReturnError;
	}

	ent = (*playerIn)[0];

	if(!ent->pPlayer)
	{
		estrPrintf(errString, "GetPlayerTeamEnts called with a non-player argument.");
		return ExprFuncReturnError;
	}
	team = team_GetTeam(ent);

	*ret = 1;
	if(team)
	{
		*ret = team_NumTotalMembers(team);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetPlayerTeamEntsOnServer);
ExprFuncReturnVal exprFuncGetPlayerTeamEntsOnServer(ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ENTARRAY_IN playerIn, ACMD_EXPR_ERRSTRING errString)
{
	Team *team;
	Entity *ent;

	if(eaSize(playerIn)!=1)
	{
		estrPrintf(errString, "GetPlayerTeamEnts called with zero or more than one ent.");
		return ExprFuncReturnError;
	}

	ent = (*playerIn)[0];

	if(!ent->pPlayer)
	{
		estrPrintf(errString, "GetPlayerTeamEnts called with a non-player argument.");
		return ExprFuncReturnError;
	}
	team = team_GetTeam(ent);

	if(team)
	{
		int iPartitionIdx = entGetPartitionIdx(ent);
		int i;
		for(i=eaSize(&team->eaMembers)-1; i>=0; i--)
		{
			TeamMember *m = team->eaMembers[i];
			Entity *e = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, m->iEntID);
			if(e)
			{
				eaPush(entsOut, e);
			}
		}
	}
	else
	{
		eaPush(entsOut, ent);
	}

	return ExprFuncReturnFinished;
}

static bool entSetFactionOverride(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_OP_STR const char* factionName)
{
	if(RefSystem_ReferentFromString(g_hCritterFactionDict, factionName))
	{
		gslEntity_SetFactionOverrideByName(pEnt, kFactionOverrideType_DEFAULT, factionName);

		// Drop from current team.  Technically, this only needs to happen if my old faction and my
		// new faction hate each other
		// This no longer needs to happen at all, since this should be caught in the team validation step
//		gslTeam_LeaveSansFeedback(pEnt);

		return true;
	}

	return false;
}
static bool entResetFactionOverride(SA_PARAM_NN_VALID Entity* pEnt)
{
	if(IS_HANDLE_ACTIVE(pEnt->hFactionOverride))
	{
		gslEntity_ClearFaction(pEnt, kFactionOverrideType_DEFAULT);
	
		// Drop from current team.  Technically, this only needs to happen if my old faction and my
		// new faction hate each other
		gslTeam_LeaveSansFeedback(pEnt);
	}
	return true;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(SetFaction);
ExprFuncReturnVal exprFuncSetFaction(ACMD_EXPR_SELF Entity *e, const char* factionName, ACMD_EXPR_ERRSTRING errString)
{
	if(!factionName[0])
		return ExprFuncReturnFinished;

	if(!e->pCritter) 
	{
		estrPrintf(errString, "Trying to use SetFaction on non-critter... maybe you meant PlayerSetFaction?");
		return ExprFuncReturnError;
	}

	if(!entSetFactionOverride(e, factionName))
	{
		estrPrintf(errString, "Setting critter to non-existent faction: %s", factionName);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntArrayResetCritterFaction);
ExprFuncReturnVal exprFuncEntArrayResetCritterFaction(ACMD_EXPR_ENTARRAY_IN eaEnts, ACMD_EXPR_ERRSTRING errString)
{
	int i;

	for(i=eaSize(eaEnts)-1; i>=0; i--)
	{
		Entity* e = (*eaEnts)[i];

		if(!e->pCritter || e->pPlayer)
		{
			estrPrintf(errString, "Trying to use ResetCritterFaction on non-critter or player... maybe you meant PlayerSetFaction?");
			return ExprFuncReturnError;
		}

		entResetFactionOverride(e);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntArraySetCritterFaction);
ExprFuncReturnVal exprFunctEntArraySetCritterFaction(ACMD_EXPR_ENTARRAY_IN eaEnts, const char* factionName, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	if(!factionName[0])
		return ExprFuncReturnFinished;

	for(i=eaSize(eaEnts)-1; i>=0; i--)
	{
		Entity* e = (*eaEnts)[i];

		if(!e->pCritter || e->pPlayer)
		{
			estrPrintf(errString, "Trying to use SetCritterFaction on non-critter or player... maybe you meant PlayerSetFaction?");
			return ExprFuncReturnError;
		}

		if(!entSetFactionOverride(e, factionName))
		{
			estrPrintf(errString, "Setting critter to non-existent faction: %s", factionName);
			return ExprFuncReturnError;
		}
	}

	return ExprFuncReturnFinished;
}

// Gets a player or critter's faction.  Doesn't count power faction override (charms), but does count the
// Faction Override that PvP maps use.
// Deprecated in favor of PlayerFactionIsType
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetPlayerFactionName);
ExprFuncReturnVal exprFuncGetPlayerFactionName(ACMD_EXPR_STRING_OUT factionNameOut, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ERRSTRING errString)
{
	CritterFaction* pFaction;
	Entity *e;
	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "Too many entities passed into GetPlayerFactionName: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}

	if(eaSize(ents) == 0)
		return ExprFuncReturnFinished;

	devassert((*ents)); // Fool static check
	e = (*ents)[0];

	if(!e->pPlayer)
	{
		estrPrintf(errString, "Passed in non-player to SetPlayerFaction");
		return ExprFuncReturnError;
	}

	devassert((*ents)); // Fool static check
	e = (*ents)[0];

	pFaction = GET_REF(e->hFactionOverride);
	if(!pFaction)
		pFaction = GET_REF(e->hFaction);

	if(pFaction && factionNameOut)
	{
		(*factionNameOut) = pFaction->pchName;
		return ExprFuncReturnFinished;
	}
	else
	{
		estrPrintf(errString, "Player %s has no faction", e->debugName);
		return ExprFuncReturnError;
	}
}

// Sets an entity's displayname message
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetDisplayName);
ExprFuncReturnVal exprFuncSetDisplayName(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_DICT(Message) const char* displayNameMsg, ACMD_EXPR_ERRSTRING errString)
{
	if(!e->pCritter)
	{
		estrPrintf(errString, "SetDisplayName requires a critter (i.e. not a player)");
		return ExprFuncReturnError;
	}

	SET_HANDLE_FROM_STRING(gMessageDict, displayNameMsg, e->pCritter->hDisplayNameMsg);
	entity_SetDirtyBit(e, parse_Critter, e->pCritter, 0);

	return ExprFuncReturnFinished;
}

// Gets a player's gender.  
// Faction Override that PvP maps use.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerGenderIsType);
ExprFuncReturnVal exprFuncPlayerGenderIsType(ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ENUM(Gender) const char* genderName, ACMD_EXPR_ERRSTRING errString)
{
	Entity *e;
	Gender eGenderToCheck;
	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "Too many entities passed into PlayerGenderIsType: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}

	if(eaSize(ents) == 0)
	{
		(*ret) = false;
		return ExprFuncReturnFinished;
	}

	devassert((*ents)); // Fool static check
	e = (*ents)[0];

	if(!e->pPlayer)
	{
		estrPrintf(errString, "Passed in non-player to PlayerGenderIsType");
		return ExprFuncReturnError;
	}

	devassert((*ents)); // Fool static check
	e = (*ents)[0];

	eGenderToCheck=StaticDefineIntGetInt(GenderEnum, genderName);

	(*ret) = (eGenderToCheck == costumeEntity_GetEffectiveCostumeGender(e));

	return ExprFuncReturnFinished;
}

// Gets a player's faction.  Doesn't count power faction override (charms), but does count the
// Faction Override that PvP maps use.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerFactionIsType);
ExprFuncReturnVal exprFuncPlayerFactionIsType(ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_RES_DICT(CritterFaction) const char* factionName, ACMD_EXPR_ERRSTRING errString)
{
	CritterFaction* pFaction;
	Entity *e;
	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "Too many entities passed into PlayerFactionIsType: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}

	if(eaSize(ents) == 0)
	{
		(*ret) = false;
		return ExprFuncReturnFinished;
	}

	devassert((*ents)); // Fool static check
	e = (*ents)[0];

	if(!e->pPlayer)
	{
		estrPrintf(errString, "Passed in non-player to PlayerFactionIsType");
		return ExprFuncReturnError;
	}

	devassert((*ents)); // Fool static check
	e = (*ents)[0];

	pFaction = GET_REF(e->hFactionOverride);
	if(!pFaction)
		pFaction = GET_REF(e->hFaction);

	if(pFaction)
	{
		(*ret) = !stricmp(pFaction->pchName, factionName);
		return ExprFuncReturnFinished;
	}
	else
	{
		estrPrintf(errString, "Player %s has no faction", e->debugName);
		return ExprFuncReturnError;
	}
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerSetFaction);
ExprFuncReturnVal exprFuncPlayerSetFaction(ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_RES_DICT(CritterFaction) const char* factionName, ACMD_EXPR_ERRSTRING errString)
{
	int i;

	if(eaSize(ents) == 0)
		return ExprFuncReturnFinished;

	for(i = eaSize(ents)-1; i>=0; i--)
	{
		Entity *e = (*ents)[i];

		if(!e->pPlayer)
		{
			estrPrintf(errString, "Passed in non-player to SetPlayerFaction");
			return ExprFuncReturnError;
		}

		if(!entSetFactionOverride(e, factionName))
		{
			estrPrintf(errString, "Setting player to non-existent faction: %s", factionName);
			return ExprFuncReturnError;
		}

	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerResetFaction);
ExprFuncReturnVal exprFuncPlayerResetFaction(ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ERRSTRING errString)
{
	int i, n = eaSize(ents);

	for(i=0; i<n; i++)
	{
		Entity* e = (*ents)[i];

		if(!e->pPlayer)
		{
			estrPrintf(errString, "Passed in non-player to PlayerResetFaction");
			return ExprFuncReturnError;
		}

		entResetFactionOverride(e);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerSetHealthPercent);
ExprFuncReturnVal exprFuncPlayerSetHealthPercent(ACMD_EXPR_ENTARRAY_IN peaEntities, F32 fHealthPercent)
{
	F32 fHealthFraction = fHealthPercent / 100.0;
	int i, iSize = eaSize(peaEntities);

	for(i = 0; i < iSize; i++)
	{
		Entity* pEnt = (*peaEntities)[i];
		Character *pChar = pEnt->pChar;
		if (pChar)
		{
			pChar->pattrBasic->fHitPoints = pChar->pattrBasic->fHitPointsMax * fHealthFraction;
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(JoinFaction);
ExprFuncReturnVal exprFuncJoinFaction(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ERRSTRING errString)
{
	Critter *critter = NULL;
	if(!e->pCritter) 
	{
		estrPrintf(errString, "Trying to use SetFaction on non-critter... maybe you meant SetPlayerFaction?");
		return ExprFuncReturnError;
	}

	if(eaSize(ents) == 0)
		return ExprFuncReturnFinished;

	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "Too many entities passed into SetPlayerFaction: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}

	critter = (*ents)[0]->pCritter;

	if(!critter)
	{
		estrPrintf(errString, "Passed in non-critter as target to JoinFaction");
		return ExprFuncReturnError;
	}

	gslEntity_SetFactionOverrideByHandle(e, kFactionOverrideType_DEFAULT, REF_HANDLEPTR((*ents)[0]->hFaction));

	return ExprFuncReturnFinished;
}

// Adds the calling entity to the team of a target ent
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddSelfToTeam);
ExprFuncReturnVal exprFuncAddSelfToTeam(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN entsIn,
										ACMD_EXPR_ERRSTRING_STATIC errString)
{
	Entity* teamEnt;
	AITeam* otherTeam, *combatTeam;

	if(!eaSize(entsIn))
	{
		*errString = "Empty ent array passed in, cannot join team of no one";
		return ExprFuncReturnError;
	}

	if(eaSize(entsIn) > 1)
	{
		*errString = "Multiple entities passed in, cannot join multiple entities' teams";
		return ExprFuncReturnError;
	}

	teamEnt = (*entsIn)[0];
	otherTeam = aiTeamGetAmbientTeam(teamEnt, teamEnt->aibase);
	aiTeamAdd(otherTeam, e);

	combatTeam = aiTeamGetCombatTeam(teamEnt, teamEnt->aibase);
	if (combatTeam != otherTeam)
		aiTeamAdd(combatTeam, e);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(MakeNewTeam);
ExprFuncReturnVal exprFuncMakeNewTeam(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	AITeam* newTeam;
	
	newTeam = aiTeamCreate(entGetPartitionIdx(e), NULL, e->aibase->insideCombatFSM);
	aiTeamAdd(newTeam, e);

	return ExprFuncReturnFinished;
}

// Sets the passed in entity as the current critter's owner. This allows proper pet leashing etc.
// It also adds the critter to that entity's team
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetMyOwner);
ExprFuncReturnVal exprFuncSetMyOwner(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	Entity* owner;

	if(!eaSize(entsIn))
	{
		*errString = "Cannot set owner to empty ent array";
		return ExprFuncReturnError;
	}
	if(eaSize(entsIn) > 1)
	{
		*errString = "Ent array has more than one entity. Cannot set owner to multiple entities";
		return ExprFuncReturnError;
	}

	owner = (*entsIn)[0];
	e->erOwner = entGetRef(owner);
	return exprFuncAddSelfToTeam(e, entsIn, errString);
}

// Clears the owner from the current entity and puts it on its own team
AUTO_EXPR_FUNC(ai) ACMD_NAME(ClearMyOwner);
void exprFuncClearMyOwner(ACMD_EXPR_SELF Entity *e)
{
	AITeam* newTeam;
	e->erOwner = 0;
		
	newTeam = aiTeamCreate(entGetPartitionIdx(e), NULL, false);
	aiTeamAdd(newTeam, e);
}

// DANGER: Returns a new ent array with all entities within 500 feet of this entity. This will get
// untargetable, unselectable and invisible entities too
// NOTE: Unless you're really sure you know what you're doing, you should not be using this
AUTO_EXPR_FUNC(ai, entity) ACMD_NAME(GetEntsWithinMax);
void exprFuncGetEntsWithinMax(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Vec3 bePos;
	int i;
	entGetPos(be, bePos);
	entGridProximityLookupExEArray(entGetPartitionIdx(be), bePos, (Entity***)entsOut, 0, 0,
		ENTITYFLAG_DEAD | ENTITYFLAG_IGNORE, be);

	for(i = eaSize(entsOut)-1; i >= 0; i--)
	{
		if(exprFuncHelperShouldExcludeFromEntArray((*entsOut)[i]))
			eaRemoveFast(entsOut, i);
	}
}

// DANGER: Returns a new ent array with all players within 500 feet of this entity. This will get
// untargetable, unselectable and invisible entities too
// NOTE: Unless you're really sure you know what you're doing, you should not be using this
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetPlayersWithinMax);
void exprFuncGetPlayersWithinMax(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Vec3 bePos;
	int i;
	entGetPos(be, bePos);
	entGridProximityLookupExEArray(entGetPartitionIdx(be), bePos, (Entity***)entsOut, 0, ENTITYFLAG_IS_PLAYER,
		ENTITYFLAG_DEAD | ENTITYFLAG_IGNORE, be);

	for(i = eaSize(entsOut)-1; i >= 0; i--)
	{
		if(exprFuncHelperShouldExcludeFromEntArray((*entsOut)[i]))
			eaRemoveFast(entsOut, i);
	}
}

// DANGER: Get all players on the map. Make sure you know what you're doing if you use this
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(GetAllMapPlayers);
void exprFuncGetAllMapPlayers(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, EXPRFUNCHELPER_SHOULD_EXCLUDE_FROM_ENTARRAY_FLAGS, GLOBALTYPE_ENTITYPLAYER);
	Entity* e;

	while(e = EntityIteratorGetNext(iter))
	{
		eaPush(entsOut, e);
	}
	EntityIteratorRelease(iter);
}

// DANGER: Get all players on the map, even those who are dead/untargetable/etc. Make sure you
// know what you're doing when you use this
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetAllMapPlayersAll);
void exprFuncGetAllMapPlayersAll(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity* e;

	while(e = EntityIteratorGetNext(iter))
	{
		eaPush(entsOut, e);
	}
	EntityIteratorRelease(iter);
}

// DANGER: Removes every entity from the ent array for which the passed in expression returns false.
// One of the more useful ways to use this function is accessing data through object paths starting
// with targetEnt.
// In general please try to use a more specific function if it's available, as this function can
// be quite expensive.
// Example: GetProxEnts().EntCropExpr(\{targetEnt.name = "Bob"\})
AUTO_EXPR_FUNC(ai, entity, encounter_action, OpenMission) ACMD_NAME(EntCropExpr);
void exprFuncEntCropExpr(ExprContext* context, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_SUBEXPR_IN subExpr)
{
	static ExprContext* lokContext = NULL;
	int i;
	MultiVal answer = {0};

	if(!lokContext)
	{
		lokContext = exprContextCreate();
		exprContextSetSilentErrors(lokContext, true);
		//lokContext->silentErrors = 1; // TODO: remove this when it can be done more intelligently
		// (constant errors when trying to evaluate .pCritter.parentEncounter.staticEnc.basedef because
		// not all evaluated critters had it
		exprContextSetPointerVarPooledCached(lokContext, targetEntString, NULL, parse_Entity,
			true, true, &targetEntVarHandle);
		exprContextSetPointerVarPooledCached(lokContext, targetEntStatusString, NULL, parse_AIStatusTableEntry,
			true, true, &targetEntStatusVarHandle);
		// TODO: this will fail if anything from the passed in context is used to evaluate the ents...
		// i don't think anyone is doing that yet though, and i don't have time to look at this right now
	}
	lokContext->parent = context;

	for(i = eaSize(entsInOut) - 1; i >= 0; i--)
	{
		Entity *selfE = exprContextGetSelfPtr(context);
		AIStatusTableEntry *status = NULL;

		exprContextSetPointerVarPooledCached(lokContext, targetEntString, (*entsInOut)[i],
			parse_Entity, false, true, &targetEntVarHandle);

		if(selfE)
			status = aiStatusFind(selfE, selfE->aibase, (*entsInOut)[i], false);
			
		exprContextSetPointerVarPooledCached(lokContext, targetEntStatusString, status, parse_AIStatusTableEntry, 
			true, true, &targetEntStatusVarHandle);

		exprEvaluateSubExpr(subExpr, context, lokContext, &answer, false);
		if(answer.type == MULTI_INT && !answer.intval)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes entities from the passed in ent array at random until <num> are left
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropXRandom);
void exprFuncEntCropXRandom(ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ENTARRAY_IN entsIn, int num)
{
	int i;

	if(!eaSize(entsIn))
		return;

	if(eaSize(entsIn) <= num)
	{
		eaPushEArray(entsOut, entsIn);
		return;
	}

	for(i = 0; i < num; i++)
	{
		int idx = randomIntRange(0, eaSize(entsIn) - 1);
		eaPush(entsOut, eaRemoveFast(entsIn, idx));
	}
}

// Removes every entity from the passed in ent array except for the closest one
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropClosest);
void exprFuncEntCropClosest(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i;
	int num = eaSize(entsInOut);
	F32 closestDist = FLT_MAX;
	Entity* closestBE = NULL;

	for(i = 0; i < num; i++)
	{
		F32 dist;

		dist = entGetDistance(be, NULL, (*entsInOut)[i], NULL, NULL);

		if(dist < closestDist)
		{
			closestDist = dist;
			closestBE = (*entsInOut)[i];
		}
	}

	eaSetSize(entsInOut, 0);

	if(closestBE)
		eaPush(entsInOut, closestBE);
}

// Removes every entity from the passed in ent array except for the farthest one
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropFarthest);
void exprFuncEntCropFarthest(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i;
	int num = eaSize(entsInOut);
	F32 farthestDist = -FLT_MAX;
	Entity* farthestBE = NULL;

	for(i = 0; i < num; i++)
	{
		F32 dist;

		dist = entGetDistance(be, NULL, (*entsInOut)[i], NULL, NULL);

		if(dist > farthestDist)
		{
			farthestDist = dist;
			farthestBE = (*entsInOut)[i];
		}
	}

	eaSetSize(entsInOut, 0);

	if(farthestBE)
		eaPush(entsInOut, farthestBE);
}

// Removes entities that are not visible to the source entity array
// If visibleToAny is set, then crop the entities that are not visible to any of the source entities,
// otherwise crop the entities that are not visible to all of the source entities.
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropVisible);
void exprFuncEntCropVisible(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN entsIn, bool visibleToAny)
{
	int i, j;
	Vec3 entPos, targetPos;
	WorldCollCollideResults results;

	if (!eaSize(entsIn))
	{
		eaClearFast(entsInOut);
	}
	for (i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		Entity* e = (*entsInOut)[i];
		
		entGetPos(e, targetPos);
		targetPos[1] += entGetHeight(e);

		for (j = eaSize(entsIn)-1; j >= 0; j--)
		{
			Entity* be = (*entsIn)[j];

			entGetPos(be, entPos);
			entPos[1] += entGetHeight(be);

			if (worldCollideRay(iPartitionIdx, entPos, targetPos, WC_FILTER_BIT_POWERS, &results))
			{
				if (!visibleToAny)
					break;
			}
			else if (visibleToAny)
			{
				break;
			}
		}
		if ((visibleToAny && j < 0) || (!visibleToAny && j >= 0))
		{
			eaRemoveFast(entsInOut, i);
		}
	}
}
 
// Removes entities that do not have the specified faction
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropFaction);
ExprFuncReturnVal exprFuncEntCropFaction(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_RES_DICT(CritterFaction) const char* factionName, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	CritterFaction *faction = RefSystem_ReferentFromString(g_hCritterFactionDict, factionName);

	if(!faction)
	{
		estrPrintf(errString, "Faction not found: %s", factionName);
		return ExprFuncReturnError;
	}
	
	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		CritterFaction *f = entGetFaction((*entsInOut)[i]);
		if(f!=faction)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

// Removes entities that do not have the specified Allegiance
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropAllegiance);
ExprFuncReturnVal exprFuncEntCropAllegiance(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_RES_DICT(AllegianceDef) const char* allegianceName, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	AllegianceDef *allegiance = RefSystem_ReferentFromString(g_hAllegianceDict, allegianceName);

	if(!allegiance)
	{
		estrPrintf(errString, "Allegiance not found: %s", allegianceName);
		return ExprFuncReturnError;
	}

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		AllegianceDef *a = GET_REF(((*entsInOut)[i])->hAllegiance);
		AllegianceDef *sa = GET_REF(((*entsInOut)[i])->hSubAllegiance);
		if(a!=allegiance && sa!=allegiance)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

// Removes entities that do not have the specified SpeciesGroup
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropSpeciesGroup);
ExprFuncReturnVal exprFuncEntCropSpeciesGroup(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* speciesGroup, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Entity *e = (*entsInOut)[i];
		if(e && e->pChar)
		{
			SpeciesDef *pSpecies = GET_REF(e->pChar->hSpecies);
			if(pSpecies && 0==stricmp(pSpecies->pcSpeciesGroup,speciesGroup))
				continue;
		}
		eaRemoveFast(entsInOut, i);
	}
	return ExprFuncReturnFinished;
}

// Removes entities that are of the specified sub rank
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropNotSubRank);
ExprFuncReturnVal exprFuncEntCropNotSubRank(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, SA_PARAM_NN_STR const char* subRankName, ACMD_EXPR_ERRSTRING errString)
{
	int i;

	subRankName = allocFindString(subRankName);

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Entity *e = (*entsInOut)[i];

		if(!e->pCritter || e->pCritter->pcSubRank == subRankName)
		{
			eaRemoveFast(entsInOut, i);
		}
	}

	return ExprFuncReturnFinished;
}

// Removes entities that are of the specified sub rank
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropCritterSubRank);
ExprFuncReturnVal exprFuncEntCropCritterSubRank(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, SA_PARAM_NN_STR const char* subRankName, ACMD_EXPR_ERRSTRING errString)
{
	int i;

	if(!critterSubRankExists(subRankName))
	{
		estrPrintf(errString, "Unable to find critter rank: %s", subRankName);
		return ExprFuncReturnError;
	}

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Critter *c = (*entsInOut)[i]->pCritter;

		if(!c || c->pcSubRank!=subRankName)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}


// Removes entities that are not on the specified gang
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropGang);
ExprFuncReturnVal exprFuncEntCropGang(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, U32 gang)
{
	int i;

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Entity *e = (*entsInOut)[i];

		if(!e->pChar || e->pChar->gangID!=gang)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

// Removes every entity from the passed in ent array that's not a player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropPlayers);
void exprFuncEntCropPlayers(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, num = eaSize(entsInOut);

	for(i = num - 1; i >= 0; i--)
	{
		if(!(*entsInOut)[i]->pPlayer)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes non-critters from the past in array
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropCritters);
void exprFuncEntCropCritters(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, num = eaSize(entsInOut);

	for(i = num - 1; i >= 0; i--)
	{
		if(!(*entsInOut)[i]->pCritter)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes every entity from the array that's not an ally
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropAllies);
void exprFuncEntCropAllies(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, num = eaSize(entsInOut);

	for(i = num - 1; i >= 0; i--)
	{
		if(critter_IsKOS(iPartitionIdx, be, (*entsInOut)[i]))
			eaRemoveFast(entsInOut, i);
	}
}

// Removes every entity from the array that's not a foe
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropFoes);
void exprFuncEntCropFoes(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, num = eaSize(entsInOut);

	for(i = num - 1; i >= 0; i--)
	{
		if(!critter_IsKOS(iPartitionIdx, be, (*entsInOut)[i]))
			eaRemoveFast(entsInOut, i);
	}
}

// Removes every entity from the array that's not within the passed in distance
// NOTE: You should probably be using the NEAR, MEDIUM, or FAR cropping functions instead of
// this one unless you're really sure
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropDist);
void exprFuncEntCropDist(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, F32 dist)
{
	int i, n = eaSize(entsInOut);

	for(i = n-1; i >= 0; i--)
	{
		if(entGetDistance(be, NULL, (*entsInOut)[i], NULL, NULL) > dist)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes every entity from the array that's not within the passed in distance from my owner
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropDistFromOwner);
void exprFuncEntCropDistFromOwner(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, F32 dist)
{
	int i, n = eaSize(entsInOut);
	Entity* owner = entFromEntityRef(iPartitionIdx, be->erOwner);

	if(!owner)
	{
		eaClearFast(entsInOut);
		return;
	}

	for(i = n-1; i >= 0; i--)
	{
		if(entGetDistance(owner, NULL, (*entsInOut)[i], NULL, NULL) > dist)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes every entity from the array that's not within the passed in distances
// NOTE: You should probably be using the NEAR, MEDIUM, or FAR cropping functions instead of
// this one unless you're really sure
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropBetweenDist);
void exprFuncEntCropBetweenDist(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, F32 minDist, F32 maxDist)
{
	int i, n = eaSize(entsInOut);

	for(i = n-1; i >= 0; i--)
	{
		F32 entDist = entGetDistance(be, NULL, (*entsInOut)[i], NULL, NULL);
		if(entDist > maxDist || entDist < minDist)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes every entity from the array that's not within NEAR
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropNear);
void exprFuncEntCropNear(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	exprFuncEntCropDist(be, entsInOut, AI_PROX_NEAR_DIST);
}

// Removes every entity from the array that's not within MEDIUM
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropMedium);
void exprFuncEntCropMedium(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	exprFuncEntCropDist(be, entsInOut, AI_PROX_MEDIUM_DIST);
}

// Removes every entity from the array that's not within FAR
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropFar);
void exprFuncEntCropFar(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	exprFuncEntCropDist(be, entsInOut, AI_PROX_FAR_DIST);
}

// Removes every entity from the array that's not between Near and Medium
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropBetweenNearMedium);
void exprFuncEntCropBetweenNearMedium(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	exprFuncEntCropBetweenDist(be, entsInOut, AI_PROX_NEAR_DIST, AI_PROX_MEDIUM_DIST);
}

// Removes every entity from the array that's not between Medium and Far
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropBetweenMediumFar);
void exprFuncEntCropBetweenMediumFar(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	exprFuncEntCropBetweenDist(be, entsInOut, AI_PROX_MEDIUM_DIST, AI_PROX_FAR_DIST);
}

// Removes every entity from the array that's not doing the specified job
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropJob);
void exprFuncEntCropJob(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* jobName)
{
	int i, n = eaSize(entsInOut);

	for(i = n-1; i >= 0; i--)
	{
		if(stricmp(aiGetJobName((*entsInOut)[i]), jobName))
			eaRemoveFast(entsInOut, i);
	}
}


// Removes every entity from the array whose level isn't in the specified range
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropLevel);
ExprFuncReturnVal exprFuncEntCropLevel(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, int minLevel, int maxLevel, ACMD_EXPR_ERRSTRING_STATIC err)
{
	int i, n = eaSize(entsInOut);

	if(minLevel > maxLevel || minLevel < 0)
	{
		estrPrintf(err, "Min/max level out of bounds: %d %d", minLevel, maxLevel);

		return ExprFuncReturnError;
	}


	for(i = n-1; i >= 0; i--)
	{
		bool remove = true;	// Remove critter unless its level is in range
		Entity* pEnt = (*entsInOut)[i];

		if(pEnt && pEnt->pChar)
		{
			int level = pEnt->pChar->iLevelCombat;
			if(level >= minLevel && level <= maxLevel)
				remove = false;
		}

		if(remove)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

// Removes every entity from the array whose level isn't in the specified range
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropExpLevel);
ExprFuncReturnVal exprFuncEntCropExpLevel(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, int minLevel, int maxLevel, ACMD_EXPR_ERRSTRING_STATIC err)
{
	int i, n = eaSize(entsInOut);

	if(minLevel > maxLevel || minLevel < 0)
	{
		estrPrintf(err, "Min/max level out of bounds: %d %d", minLevel, maxLevel);

		return ExprFuncReturnError;
	}

	for(i = n-1; i >= 0; i--)
	{
		bool remove = true;	// Remove critter unless its level is in range
		Entity* pEnt = (*entsInOut)[i];

		if(pEnt && pEnt->pChar)
		{
			int level = entity_GetSavedExpLevel(pEnt);
			if(level >= minLevel && level <= maxLevel)
				remove = false;
		}

		if(remove)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

// Removes every entity from the array that's not in a state with the specified name
// NOTE: this will check against all embedded FSMs the entity is currently using, so you can
// check against any of those states
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropState);
void exprFuncEntCropState(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* stateName)
{
	int i, n = eaSize(entsInOut);

	for(i = n-1; i >= 0; i--)
	{
		const char* stateStr = aiGetState((*entsInOut)[i]);
		if(!stateStr || stricmp(stateStr, stateName))
			eaRemoveFast(entsInOut, i);
	}
}

// Removes entities that are not in the state with the specified name
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropCombatState);
void exprFuncEntCropCombatState(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* stateName)
{
	int i, n = eaSize(entsInOut);

	for(i=n-1; i>=0; i--)
	{
		Entity *e = (*entsInOut)[i];

		if(!e->aibase->inCombat)
			eaRemoveFast(entsInOut, i);
		else
		{
			const char* stateStr = aiGetCombatState((*entsInOut)[i]);

			if(!stateStr || stricmp(stateStr, stateName))
				eaRemoveFast(entsInOut, i);
		}
	}
}

// Removes the current entity from the array
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropNotMe);
void exprFuncEntCropNotMe(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, n = eaSize(entsInOut);

	for(i = n-1; i >= 0; i--)
	{
		if((*entsInOut)[i] == be)
		{
			eaRemoveFast(entsInOut, i);
			break;
		}
	}
}

// Removes everything EXCEPT the current entity from the array
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropMe);
void exprFuncEntCropMe(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, n = eaSize(entsInOut);
	int found = 0;

	for(i=n-1; i>=0; i--)
	{
		if((*entsInOut)[i]==be)
		{
			// Eh, if I find me, just clear it and put me in it
			eaClear(entsInOut);
			eaPush(entsInOut, be);
			found = 1;
			break;
		}
	}
	if(!found)
		eaClear(entsInOut);
}

// Removes all entities that are not legal targets for the current critter
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropLegalTargets);
void exprFuncEntCropLegalTargets(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	AIVarsBase* aib = e->aibase;
	int i, n = eaSize(entsInOut);

	for(i = n-1; i >= 0; i--)
	{
		Entity* target = (*entsInOut)[i];
		AIStatusTableEntry* status = aiStatusFind(e, aib, target, false);
		AITeamStatusEntry *teamStatus = status ? aiGetTeamStatus(e, aib, status) : NULL;

		if(!status || !teamStatus || !teamStatus->legalTarget)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes all entities that are not dead
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropDead);
void exprFuncEntCropDead(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, n = eaSize(entsInOut);

	for(i = n-1; i >= 0; i--)
	{
		Entity* target = (*entsInOut)[i];

		if(aiIsEntAlive(target))
			eaRemoveFast(entsInOut, i);
	}
}

// Removes all entities for which the specified attrib positive
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropAttribPositive);
void exprFuncEntCropAttribPositive(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut,
								  ACMD_EXPR_ENUM(AttribType) const char* attribName)
{
	int i, n = eaSize(entsInOut);
	S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);

	if(eAttrib < 0 || !IS_NORMAL_ATTRIB(eAttrib))
		eaClearFast(entsInOut);
	else
	{
		for(i = n-1; i >= 0; i--)
		{
			Entity* target = (*entsInOut)[i];

			if(!target->pChar || *F32PTR_OF_ATTRIB(target->pChar->pattrBasic,eAttrib) <= 0)
				eaRemoveFast(entsInOut, i);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropStatusAttribs);
void exprFuncEntCropStatusAttribs(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut,
									int hold, int root, int confuse, int knock)
{
	int i;

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		int passed = 0;
		Entity *e = (*entsInOut)[i];

		if(!e->pChar)
		{
			eaRemoveFast(entsInOut, i);
			continue;
		}

		if(hold && character_IsHeld(e->pChar))
			passed = 1;
		if(root && character_IsRooted(e->pChar))
			passed = 1;
		if(confuse && e->pChar->pattrBasic->fConfuse>0)
			passed = 1;
		if(knock && (e->pChar->pattrBasic->fKnockBack>0 || 
					e->pChar->pattrBasic->fKnockUp>0))
			passed = 1;

		if(!passed)
			eaRemoveFast(entsInOut, i);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropHasItem);
ExprFuncReturnVal exprFuncEntCropHasItem(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_RES_DICT(ItemDef) const char* itemName, int count,
											ACMD_EXPR_ERRSTRING errString)
{
	int i;

	if(!item_DefFromName(itemName))
	{
		estrPrintf(errString, "Invalid item def name: %s", itemName);
		return ExprFuncReturnError;
	}

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Entity *e = (*entsInOut)[i];

		if(inv_ent_AllBagsCountItems(e, itemName)<count)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

static int disableMaxProtectRadius = false;
AUTO_CMD_INT(disableMaxProtectRadius, disableMaxProtectRadius);

// Returns true if the overarching Combat FSM would go into combat. This is useful for maintaining
// consistency between all of our simple FSMs and our more complicated ones even when we make 
// changes to how combat works
AUTO_EXPR_FUNC(ai) ACMD_NAME(DefaultEnterCombat);
int exprFuncDefaultEnterCombat(ACMD_EXPR_SELF Entity* e)
{
	AITeam* combatTeam = aiTeamGetCombatTeam(e, e->aibase);
	if(combatTeam)
		return combatTeam->combatState != AITEAM_COMBAT_STATE_AMBIENT;
	else
		return false;
}

// Returns true if the overarching Combat FSM would stop being in combat. See comment at
// DefaultEnterCombat()
AUTO_EXPR_FUNC(ai) ACMD_NAME(DefaultDropOutOfCombat);
int exprFuncDefaultDropOutOfCombat(ACMD_EXPR_SELF Entity* e)
{
	AITeam* combatTeam = aiTeamGetCombatTeam(e, e->aibase);
	if(combatTeam)
		return combatTeam->combatState == AITEAM_COMBAT_STATE_AMBIENT;
	else
		return true;
}

// Returns a new ent array with the entity's attack target if the entity currently has one (or
// empty otherwise)
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetAttackTarget);
void exprFuncGetAttackTarget(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Entity* attackTarget = entity_GetTarget(e);

	if(attackTarget && aiIsEntAlive(attackTarget) &&
		!exprFuncHelperShouldExcludeFromEntArray(attackTarget))
	{
		eaPush(entsOut, attackTarget);
	}
}

// Returns an ent array filled with the attack targets of the entities passed in.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetEntArrayAttackTargets);
void exprFuncGetEntArrayAttackTargets(ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	int i, j;

	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* ent = (*entsIn)[i];
		Entity* attackTarget = entity_GetTarget(ent);

		if(attackTarget && aiIsEntAlive(attackTarget) &&
			!exprFuncHelperShouldExcludeFromEntArray(attackTarget))
		{
			int found = false;

			for(j = eaSize(entsOut)-1; j >= 0; j--)
			{
				if((*entsOut)[j] == attackTarget)
				{
					found = true;
					break;
				}
			}

			if(!found)
				eaPush(entsOut, attackTarget);
		}
	}
}

// Returns a new ent array with the closest player in it. This can actually return players farther
// away than your proximity radius
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetClosestPlayer);
void exprFuncGetClosestPlayer(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	int i, n;
	Vec3 myPos;
	static Entity** proxEnts = NULL;
	F32 closestPlayerDist = FLT_MAX;
	Entity* closestPlayer = NULL;

	entGetPos(be, myPos);

	entGridProximityLookupEArray(entGetPartitionIdx(be), myPos, &proxEnts, true);

	n = eaSize(&proxEnts);
	for(i = 0; i < n; i++)
	{
		Entity* targetBE = proxEnts[i];
		F32 dist;

		devassertmsg(targetBE->pPlayer, "Non player returned from player only lookup array?");

		dist = entGetDistance(be, NULL, targetBE, NULL, NULL);

		if(dist < closestPlayerDist && !exprFuncHelperShouldExcludeFromEntArray(targetBE))
		{
			closestPlayerDist = dist;
			closestPlayer = targetBE;
		}
	}

	if(closestPlayer)
		eaPush(entsOut, closestPlayer);
}

// Returns true if the given tag is found 
AUTO_EXPR_FUNC(ai) ACMD_NAME(ModDefHasTag);
int exprFuncAttribModDefHasTag(SA_PARAM_NN_VALID PowerTagsStruct *tags,
							   ACMD_EXPR_ENUM(PowerTag) const char *tagName) 
{
	int eTag = StaticDefineIntGetInt(PowerTagsEnum,tagName);
	return (eTag>0 && powertags_Check(tags,eTag));
}

// Returns true if all entities in the ent array have the specified mode, and it was placed on
// all of them in the past <time> seconds
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CheckEntArrayMode);
int exprFuncCheckEntArrayMode(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_ENUM(PowerMode) const char* modeName, F32 time)
{
	int i;
	int mode;

	if(!modeName[0])
		return 0;

	if(!eaSize(entsIn))
		return 0;

	mode = StaticDefineIntGetInt(PowerModeEnum,modeName);

	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* ent = (*entsIn)[i];
		if(ent->pChar && !character_HasMode((*entsIn)[i]->pChar, mode))
			return 0;
	}

	return 1;
}

// Removes entities that do not have the specified mode.
// The time parameter is completely ignored, but it's less confusing to leave it in than to try to
// give it a name that indicates that designers should fill in a dummy value
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropMode);
void exprFuncEntCropMode(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENUM(PowerMode) const char* modeName, F32 time)
{
	int i;
	int mode;

	if(!modeName[0])
		return;

	if(!eaSize(entsInOut))
		return;

	mode = StaticDefineIntGetInt(PowerModeEnum,modeName);

	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		Entity* ent = (*entsInOut)[i];
		if(ent->pChar && !character_HasMode(ent->pChar, mode))
			eaRemoveFast(entsInOut, i);
	}
}

// Removes entities that do not have the combat mode
// Adds no functionality to CropMode above, but has a clearer name for designer use
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropInCombat);
void exprFuncEntCropInCombat(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	exprFuncEntCropMode(entsInOut, "Combat", 0);
}

// Removes entities that do not have the specified mode targed towards self
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropPersonalModeIncoming);
void exprFuncEntCropPersonalModeIncoming(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENUM(PowerMode) const char* modeName)
{
	int i;
	int mode;

	if(!modeName[0] || !be || !be->pChar)
		return;

	if(!eaSize(entsInOut))
		return;

	mode = StaticDefineIntGetInt(PowerModeEnum,modeName);

	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		Entity* ent = (*entsInOut)[i];
		if(ent->pChar && !character_HasModePersonal(ent->pChar, mode, be->pChar))
			eaRemoveFast(entsInOut, i);
	}
}

// Removes entities that are not the target of a personal power mode targeted towards them
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropPersonalModeOutgoing);
void exprFuncEntCropPersonalModeOutgoing(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENUM(PowerMode) const char* modeName)
{
	int i;
	int mode;

	if(!modeName[0] || !be || !be->pChar)
		return;

	if(!eaSize(entsInOut))
		return;

	mode = StaticDefineIntGetInt(PowerModeEnum,modeName);

	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		Entity* ent = (*entsInOut)[i];
		if(ent->pChar && !character_HasModePersonal(be->pChar, mode, ent->pChar))
			eaRemoveFast(entsInOut, i);
	}
}

// Returns whether the current entity is within five feet of the passed in expression point
AUTO_EXPR_FUNC(ai) ACMD_NAME(CloseToPoint);
int exprFuncCloseToPoint(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_LOC_MAT4_IN matIn)
{
	F32 dist;

	dist = entGetDistance(be, NULL, NULL, matIn[3], NULL);

	return dist < 5;
}

// Changes the entity's movement speed to be <speed> feet per second
AUTO_EXPR_FUNC(ai) ACMD_NAME(OverrideMovementSpeed);
ExprFuncReturnVal ExprFuncOverrideMovementSpeed(ACMD_EXPR_SELF Entity* be, F32 speed, ACMD_EXPR_ERRSTRING errString)
{
	int retval;
	char* speedstr = NULL;
	
	estrStackCreate(&speedstr);
	estrPrintf(&speedstr, "%f", speed);

	retval = aiConfigModAddFromString(be, be->aibase, "OverrideMovementSpeed", speedstr, errString);

	estrDestroy(&speedstr);

	if(retval)
		return ExprFuncReturnFinished;
	
	return ExprFuncReturnError;
}

#include "aiMovement_h_ast.h"
#include "AILib_autogen_QueuedFuncs.h"

// Runs to the specified encounter point
AUTO_EXPR_FUNC(ai) ACMD_NAME(RunToPoint) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncRunToPoint(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_LOC_MAT4_IN target, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	FSMLDRunToPoint* mydata = getMyData(context, parse_FSMLDRunToPoint, PTR_TO_UINT(target));
	
	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetFSMContext(context)->curTracker;

		if(!tracker->exitHandlers)
			tracker->exitHandlers = CommandQueue_Create(8, false);

		QueuedCommand_aiMovementResetPath(tracker->exitHandlers, be, be->aibase);
		QueuedCommand_deleteMyData(tracker->exitHandlers, context, parse_FSMLDRunToPoint, &tracker->localData, PTR_TO_UINT(target));

		mydata->addedExitHandlers = 1;

		if(vec3IsZero(target[3]))
		{
			*errString = "Being told to run to (0, 0, 0), pretty sure that's not intended";
			return ExprFuncReturnError;
		}

		exprFuncAddConfigMod(be, context, "RoamingLeash", "1", errString);
	}
	
	aiMovementSetTargetPosition(be, 
								be->aibase, 
								target[3], 
								NULL, 
								AI_MOVEMENT_TARGET_CRITICAL | AI_MOVEMENT_TARGET_ERROR_ON_NO_TARGET_BEACON);

	return ExprFuncReturnFinished;
}

// Sets the current entity's position to the passed in points and moves instantaneously
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetPos);
void exprFuncSetPos(ACMD_EXPR_SELF Entity* e, ExprContext *context, ACMD_EXPR_LOC_MAT4_IN target)
{
	entSetPos(e, target[3], true, __FUNCTION__);

	if (e->aibase)
	{
		copyVec3(target[3], e->aibase->ambientPosition);
	}

	exprFuncAddConfigMod(e, context, "RoamingLeash", "1", NULL);
}

// Sets the current entity's position to the passed in points and moves instantaneously
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetRotationAndFacing);
void exprFuncSetRotationAndFacing(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_LOC_MAT4_IN target)
{
	Quat qRot;
	Vec3 pyr;

	mat3ToQuat(target, qRot);
	getMat3YPR(target, pyr);
	
	entSetRot(e, qRot, true, __FUNCTION__);
	entSetFacePY(e, pyr, __FUNCTION__);
}

// Sets the current entity's position to the passed in points and moves instantaneously
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetPositionRotationAndFacing);
void exprFuncSetPositionRotationAndFacing(ACMD_EXPR_SELF Entity* e, ExprContext *context, ACMD_EXPR_LOC_MAT4_IN target)
{
	exprFuncSetPos(e, context, target);
	exprFuncSetRotationAndFacing(e, target);
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(RetreatFromEntArray);
ExprFuncReturnVal exprFuncRetreatFromEntArray(ACMD_EXPR_SELF Entity *e, ExprContext *context, ACMD_EXPR_ENTARRAY_IN ents, F32 distance, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDRetreat *mydata = getMyData(context, parse_FSMLDRetreat, 0);
	
	Vec3 retreatFromPos = {0};
	Vec3 retreatPos;
	Vec3 myPos;
	Vec3 retreatDir;
	Vec3 retreatTarget;
	int i, num = 0;

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry *tracker = exprContextGetFSMContext(context)->curTracker;

		if(!tracker->exitHandlers)
			tracker->exitHandlers = CommandQueue_Create(8, false);

		QueuedCommand_aiMovementResetPath(tracker->exitHandlers, e, e->aibase);
		QueuedCommand_deleteMyData(tracker->exitHandlers, context, parse_FSMLDRetreat, &tracker->localData, 0);

		mydata->addedExitHandlers = 1;
	}

	// determine position to run from by taking the geometric average of the entities' positions
	for(i=0; i<eaSize(ents); i++)
	{
		Vec3 entPos;
		entGetPos((*ents)[i], entPos);
		addVec3(entPos, retreatFromPos, retreatFromPos);
		num++;
	}

	if(num)
		scaleVec3(retreatFromPos, 1.0/num, retreatFromPos);
	else
		return ExprFuncReturnFinished;

	// determine run to position
	entGetPos(e, myPos);
	subVec3(myPos, retreatFromPos, retreatDir);
	normalVec3(retreatDir);
	scaleAddVec3(retreatDir, distance, myPos, retreatPos);

	// Attempt to move our entity
	if(aiFindRunToPos(e, e->aibase, myPos, retreatPos, retreatDir, retreatPos, retreatTarget, PI) ||
		aiMovementGetFlying(e, e->aibase))
	{
		aiMovementSetTargetPosition(e, 
									e->aibase, 
									retreatTarget, 
									NULL, 
			AI_MOVEMENT_TARGET_CRITICAL | AI_MOVEMENT_TARGET_ERROR_ON_NO_TARGET_BEACON);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(Retreat);
ExprFuncReturnVal exprFuncRetreat(ACMD_EXPR_SELF Entity *e, ExprContext *context, ACMD_EXPR_PARTITION iPartitionIdx, F32 distance, int fromTargetOnly, ACMD_EXPR_ERRSTRING errString)
{
	static Entity **entArrayTmp = NULL;

	eaClear(&entArrayTmp);
	if(fromTargetOnly && e->aibase->attackTarget)
	{
		eaPush(&entArrayTmp, e->aibase->attackTarget);
	}
	else
	{
		int i, num = 0;

		for(i=0; i<eaSize(&e->aibase->statusTable); i++)
		{
			AIStatusTableEntry *status = e->aibase->statusTable[i];
			AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, e->aibase, status);

			if(teamStatus && teamStatus->legalTarget)
			{
				Entity* statusE = entFromEntityRef(iPartitionIdx, status->entRef);
				
				eaPush(&entArrayTmp, statusE);
			}
		}
	}

	return exprFuncRetreatFromEntArray(e, context, &entArrayTmp, distance, errString);
}

// Resets the FSM timer
AUTO_EXPR_FUNC(ai) ACMD_NAME(ResetTimer);
void exprFuncResetTimer(ACMD_EXPR_SELF Entity* be, ExprContext* context)
{
	FSMContext *pFSMContext = exprContextGetFSMContext(context);
	if (pFSMContext)
		pFSMContext->timer = 0;
}

// Gets the value of the FSM timer
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetTimer);
int exprFuncGetTimer(ACMD_EXPR_SELF Entity* be, ExprContext* context)
{
	FSMContext *pFSMContext = exprContextGetFSMContext(context);
	if (pFSMContext)
		return pFSMContext->timer;
	return 0;
}

// Gets the AI FSM tick rate in seconds
AUTO_EXPR_FUNC(ai, encounter_action);
F32 exprFuncAIFSMGetTickRate(ExprContext *context)
{
	Entity *e = exprContextGetSelfPtr(context);

	if(e)
		return AI_TICK_TIME(e);
	return AI_TICK_RATE;
}

/*
// Returns the server time of when the team was last bothered
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetLastTeamBotheredTime);
S64 exprFuncGetLastTeamBotheredTime(ACMD_EXPR_SELF Entity* e)
{
	return aiTeamGetCombatTeam(e, e->aibase)->time.lastBothered;
}
*/

AUTO_EXPR_FUNC(ai) ACMD_NAME(TimeSinceTeamBuffed);
F32 exprFuncGetTimeSinceTeamBuffed(ACMD_EXPR_SELF Entity* e)
{
	int partitionIdx = entGetPartitionIdx(e);
	return ABS_TIME_TO_SEC(ABS_TIME_SINCE_PARTITION(partitionIdx, aiTeamGetCombatTeam(e, e->aibase)->time.lastBuff));
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(TimeSinceLastMoved);
F32 exprFuncGetTimeSinceLastMoved(ACMD_EXPR_SELF Entity* e)
{
	int partitionIdx = entGetPartitionIdx(e);
	
	if (e->aibase->time.timeStoppedCurrentlyMoving == 0)
		return 0.f;

	return ABS_TIME_TO_SEC(ABS_TIME_SINCE_PARTITION(partitionIdx, e->aibase->time.timeStoppedCurrentlyMoving));
}



void getGeometricAverage(ACMD_EXPR_ENTARRAY_IN ents, Vec3 outVec)
{
	int i;
	int num = eaSize(ents);

	for(i = num - 1; i >= 0; i--)
	{
		Vec3 targetEntPos;
		entGetPos((*ents)[i], targetEntPos);
		addVec3(outVec, targetEntPos, outVec);
	}

	outVec[0] = outVec[0] / num;
	outVec[1] = outVec[1] / num;
	outVec[2] = outVec[2] / num;
}

// WARNING: Returns the distance between the current entity and the geometric average of the ent
// array passed in. Use only for very map specific distance checks
// WARNING 2: This cannot take into account correct capsule distances, so be even more scared of
// using it
AUTO_EXPR_FUNC(entity) ACMD_NAME(DistToEnts);
F32 exprFuncDistToEnts(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN ents)
{
	AIVarsBase* aib = be->aibase;
	Vec3 targetPos = {0};
	Vec3 sourcePos;

	if(!eaSize(ents))
		return FLT_MAX;

	entGetPos(be, sourcePos);

	getGeometricAverage(ents, targetPos);

	return distance3(sourcePos, targetPos);
}

// WARNING: Returns the distance between the current entity and the geometric average of the ent
// array passed in ignoring height. See comment at DistToEnts()
// WARNING 2: This cannot take into account correct capsule distances, so be even more scared of
// using it
AUTO_EXPR_FUNC(entity) ACMD_NAME(DistToEntsXZ);
F32 exprFuncDistToEntsXZ(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN ents)
{
	AIVarsBase* aib = be->aibase;
	Vec3 targetPos = {0};
	Vec3 sourcePos;

	if(!eaSize(ents))
		return FLT_MAX;

	entGetPos(be, sourcePos);

	getGeometricAverage(ents, targetPos);

	return distance3XZ(sourcePos, targetPos);
}

// DEPRECATED: I can't think of a good reason to have these "generic" geometric average functions,
// let me know if you think it's useful, this function confuses people because they think it'll tell
// them if anyone is within the specified distance, rather than the average of everyone
// Returns whether the distance between the current entity and the geometric average of the ent
// array passed in is closer than NEAR
// WARNING 2: This cannot take into account correct capsule distances, so be even more scared of
// using it
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntsWithinNear);
int exprFuncEntsWithinNear(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN ents)
{
	AIVarsBase* aib = be->aibase;
	Vec3 targetPos = {0};
	Vec3 sourcePos;

	if(!eaSize(ents))
		return false;

	entGetPos(be, sourcePos);

	getGeometricAverage(ents, targetPos);

	return distance3Squared(sourcePos, targetPos) <= SQR(AI_PROX_NEAR_DIST);
}

// DEPRECATED: I can't think of a good reason to have these "generic" geometric average functions,
// let me know if you think it's useful, this function confuses people because they think it'll tell
// them if anyone is within the specified distance, rather than the average of everyone
// Returns whether the distance between the current entity and the geometric average of the ent
// array passed in is closer than MEDIUM
// WARNING 2: This cannot take into account correct capsule distances, so be even more scared of
// using it
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntsWithinMedium);
int exprFuncEntsWithinMedium(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN ents)
{
	AIVarsBase* aib = be->aibase;
	Vec3 targetPos = {0};
	Vec3 sourcePos;

	if(!eaSize(ents))
		return false;

	entGetPos(be, sourcePos);

	getGeometricAverage(ents, targetPos);

	return distance3Squared(sourcePos, targetPos) <= SQR(AI_PROX_MEDIUM_DIST);
}

// DEPRECATED: I can't think of a good reason to have these "generic" geometric average functions,
// let me know if you think it's useful, this function confuses people because they think it'll tell
// them if anyone is within the specified distance, rather than the average of everyone
// Returns whether the distance between the current entity and the geometric average of the ent
// array passed in is closer than FAR
// WARNING 2: This cannot take into account correct capsule distances, so be even more scared of
// using it
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntsWithinFar);
int exprFuncEntsWithinFar(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN ents)
{
	AIVarsBase* aib = be->aibase;
	Vec3 targetPos = {0};
	Vec3 sourcePos;

	if(!eaSize(ents))
		return false;

	entGetPos(be, sourcePos);

	getGeometricAverage(ents, targetPos);

	return distance3Squared(sourcePos, targetPos) <= SQR(AI_PROX_FAR_DIST);
}

// Returns whether the critter's spawn pos is within NEAR
AUTO_EXPR_FUNC(ai) ACMD_NAME(SpawnPosWithinNear);
int exprFuncSpawnPosWithinNear(ACMD_EXPR_SELF Entity* e, ExprContext* context)
{
	FSMLDGenericU64* mydata = getMyData(context, parse_FSMLDGenericU64, (U64)"SpawnPosWithinNear");
	F32 dist;
	int closeEnough;
	AIVarsBase *aib = e->aibase;
	int partitionIdx = entGetPartitionIdx(e);

	dist = aiGetSpawnPosDist(e, aib);

	closeEnough = dist < AI_PROX_NEAR_DIST;

	if(closeEnough)
	{
		if(mydata->myU64 && ABS_TIME_SINCE_PARTITION(partitionIdx, mydata->myU64) > SEC_TO_ABS_TIME(2))
			return true;
		else if(!mydata->myU64)
			mydata->myU64 = ABS_TIME_PARTITION(partitionIdx);

		if(dist < 0.5)
		{
			Quat curRot;

			entGetRot(e, curRot);

			return quatWithinAngle(curRot, e->aibase->spawnRot, RAD(5));
		}
	}
	else
		mydata->myU64 = 0;

	return false;
}

// Returns whether the critter's original spawn pos is within NEAR
AUTO_EXPR_FUNC(ai) ACMD_NAME(OriginalSpawnPosWithinNear);
int exprFuncOriginalSpawnPosWithinNear(ACMD_EXPR_SELF Entity *e, ExprContext* context)
{
	FSMLDGenericU64* mydata = getMyData(context, parse_FSMLDGenericU64, (U64)"OriginalSpawnPosWithinNear");
	F32 dist;
	int closeEnough;
	AIVarsBase *aib = e->aibase;
	Vec3 pos;
	int partitionIdx = entGetPartitionIdx(e);

	entGetPos(e, pos);

	dist = distance3(pos, aib->spawnPos);

	closeEnough = dist < AI_PROX_NEAR_DIST;

	if(closeEnough)
	{
		if(mydata->myU64 && ABS_TIME_SINCE_PARTITION(partitionIdx, mydata->myU64) > SEC_TO_ABS_TIME(2))
			return true;
		else if(!mydata->myU64)
			mydata->myU64 = ABS_TIME_PARTITION(partitionIdx);

		if(dist < 0.5)
		{
			Quat curRot;

			entGetRot(e, curRot);

			return quatWithinAngle(curRot, e->aibase->spawnRot, RAD(5));
		}
	}
	else
		mydata->myU64 = 0;

	return false;
}

// Returns whether all entities in the array are enemies and are visible
AUTO_EXPR_FUNC(ai) ACMD_NAME(CheckVisibleEnemies);
int exprFuncCheckVisibleEnemies(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	AIVarsBase* aib = be->aibase;
	int i;

	if(!eaSize(entsIn))
		return false;

	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		AIStatusTableEntry* status = aiStatusFind(be, aib, (*entsIn)[i], false);
		if(!status || !status->visible)
			return false;
	}

	return true;
}

AUTO_COMMAND_QUEUED();
void aiTurnOffHealing(ACMD_POINTER Entity* e, ACMD_POINTER AIVarsBase* aib)
{
	aib->healing = 0;
}

// Does leash/grieved healing without going anywhere, ignoreLevels tells it to heal to full, regardless of aiconfig
AUTO_EXPR_FUNC(ai) ACMD_NAME(DoGrievedHeal);
void exprFuncDoGrievedHeal(ACMD_EXPR_SELF Entity *e, ExprContext *context, int heal, int combat_reset, int ignoreLevels)
{
	FSMLDGrievedHeal *mydata = getMyData(context, parse_FSMLDGrievedHeal, 0);

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetFSMContext(context)->curTracker;
		AIVarsBase *aib = e->aibase;
		mydata->addedExitHandlers = 1;

		if(!tracker->exitHandlers)
			tracker->exitHandlers = CommandQueue_Create(8 * sizeof(void*), false);

		aiCombatReset(e, e->aibase, heal, combat_reset, ignoreLevels);
		if(aib->healing)
			QueuedCommand_aiTurnOffHealing(tracker->exitHandlers, e, aib);

		QueuedCommand_deleteMyData(tracker->exitHandlers, context, parse_FSMLDGrievedHeal, 
									&tracker->localData, 0);
	}
}

// Runs back to the critter's spawn location, <heal> determines whether to activate the critter's
// fast out of combat healing
AUTO_EXPR_FUNC(ai) ACMD_NAME(GoToSpawnPos) ACMD_EXPR_FUNC_COST_MOVEMENT;
void exprFuncGoToSpawnPos(ACMD_EXPR_SELF Entity* e, ExprContext* context, int heal)
{
	FSMLDGoToSpawnPos* mydata = getMyData(context, parse_FSMLDGoToSpawnPos, 0);

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetFSMContext(context)->curTracker;
		AIVarsBase *aib = e->aibase;
		AIConfig *config = aiGetConfig(e, aib);

		mydata->addedExitHandlers = 1;

		if(!tracker->exitHandlers)
			tracker->exitHandlers = CommandQueue_Create(8 * sizeof(void*), false);

		QueuedCommand_aiMovementResetPath(tracker->exitHandlers, e, e->aibase);
		QueuedCommand_deleteMyData(tracker->exitHandlers, context, parse_FSMLDGoToSpawnPos, 
									&tracker->localData, 0);

		if(heal && !config->dontDoGrievedHealing)
		{
			if(e->pChar)
			{
				aiCombatReset(e, aib, true, true, 0);
				if(aib->healing)		// Only do so if they are actually healing
					QueuedCommand_aiTurnOffHealing(tracker->exitHandlers, e, aib);
			}
			else
			{
				// TODO: exprusageerror
				if(e->pCritter)
				{
					if(e->pCritter->encounterData.pGameEncounter)
					{
						GameEncounter *pEncounter = e->pCritter->encounterData.pGameEncounter;
						const char *actorStr = NULL;
						const char* filename = encounter_GetFilename(pEncounter);

						actorStr = encounter_GetActorName(pEncounter, e->pCritter->encounterData.iActorIndex);
						ErrorFilenamef(filename, "Actor %s of Encounter %s is non-combat but trying to use out of combat healing",
							actorStr, pEncounter->pcName );
					}
					else if(gConf.bAllowOldEncounterData && e->pCritter->encounterData.sourceActor && e->pCritter->encounterData.parentEncounter)
					{
						OldStaticEncounter *statEnc = GET_REF(e->pCritter->encounterData.parentEncounter->staticEnc);
						char *actorStr = NULL;
						const char* filename = SAFE_MEMBER(statEnc, pchFilename);

						estrStackCreate(&actorStr);

						oldencounter_GetActorName(e->pCritter->encounterData.sourceActor, &actorStr);
						ErrorFilenamef(filename, "Actor %s of Static Encounter %s is non-combat but trying to use out of combat healing",
							actorStr, statEnc ? statEnc->name : "N/A");
						estrDestroy(&actorStr);
					}
				}
			}
		}
	}

	aiMovementGoToSpawnPos(e, e->aibase, AI_MOVEMENT_TARGET_CRITICAL);
}

// Sets a UI var <varName>,<floatVal> key value pair
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetUIVar);
void exprFuncSetUIVar(ACMD_EXPR_SELF Entity* be, const char* varName, F32 floatVal)
{
	MultiVal val;

	val.type = MULTI_FLOAT;
	val.floatval = floatVal;
	entSetUIVar(be, varName, &val);
	entSetActive(be);
}

// Sets a UI var <varName>,<messageKey> key value pair
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetUIVarMessage);
void exprFuncSetUIVarMessage(ACMD_EXPR_SELF Entity* be, const char* varName, const char* msgKey)
{
	const char* pchMessage = entTranslateMessageKey(be, msgKey);
	if (pchMessage && pchMessage[0])
	{
		MultiVal val = {0};
		MultiValSetString(&val, pchMessage);
		ANALYSIS_ASSUME(be != NULL);
		entSetUIVar(be, varName, &val);
		entSetActive(be);
		MultiValClear(&val);
	}
	else
	{
		ErrorDetailsf("Key %s", msgKey);
		Errorf("SetUIVarMessage: Couldn't find message for key");
	}
}

// Gets the value of a specific UI var with name <varName>
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetUIVar);
ExprFuncReturnVal exprFuncGetUIVar(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_FLOAT_OUT floatVal, const char* varName)
{
	int varValue = 0;
	MultiVal val;

	//get the value of an entities UI var
	if(entGetUIVar(be, varName, &val))
	{
		if(val.type != MULTI_INT && val.type != MULTI_FLOAT)
		{
			Errorf("Multival type is invalid");
			return ExprFuncReturnError;
		}

		*floatVal = QuickGetFloat(&val);
	}

	return ExprFuncReturnFinished;
}

// Removes a UI var
AUTO_EXPR_FUNC(ai) ACMD_NAME(DeleteUIVar);
void exprFuncDeleteUIVar(ACMD_EXPR_SELF Entity* be, const char* varName)
{
	entDeleteUIVar(be, (char*)varName);
}

AUTO_COMMAND ACMD_LIST(gEntConCmdList);
void aiChangeState(Entity* e, const char* newState)
{
	// a debugging command, ignoring other FSM context that may be controlling this AI
	if (e && newState && e->aibase)
	{
		FSMContext *pFSMContext = aiGetCurrentFSMContext(e);
		if (pFSMContext)
			fsmChangeState(pFSMContext, newState);
	}
}

// WARNING: Enables or disables sleep mode on an entity. Only turn off sleep mode is it's very
// important to keep this entity running even when no players are around
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetDontSleep);
void exprFuncSetDontSleep(ACMD_EXPR_SELF Entity* be, S32 on)
{
	be->aibase->dontSleep = on;
}

static F32 randomFloatInRange(F32 min, F32 max)
{
	float mag = max - min;
	float randFloat = min + (randomPositiveF32() * mag);

	return randFloat;
}

// Don't use this unless you get programmers to sign off on it
AUTO_EXPR_FUNC(gameutil, encounter_action) ACMD_NAME(RandomPointYDrop);
void exprFuncRandomPointYDrop(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_OUT matOut, F32 xMin, F32 xMax, F32 y, F32 zMin, F32 zMax)
{
	matOut[3][0] = randomFloatInRange(xMin, xMax);
	matOut[3][1] = y;
	matOut[3][2] = randomFloatInRange(zMin, zMax);

	worldGetPointFloorDistance(worldGetActiveColl(iPartitionIdx), matOut[3], 30, 60, NULL);
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MakePointAlongYawFromEnt);
ExprFuncReturnVal exprFuncMakePointAlongYawFromEnt(ACMD_EXPR_LOC_MAT4_OUT matOut, ACMD_EXPR_ENTARRAY_IN entsIn, F32 distance, ACMD_EXPR_ERRSTRING errString)
{
	F32 yaw;
	Vec3 pos;
	Vec3 offset;
	Vec3 pyFace;
	Entity* e = eaGet(entsIn, 0);

	if(!e)
	{
		estrPrintf(errString, "MakePointAlongYawFromEnt requires at least one ent!");
		return ExprFuncReturnError;
	}

	entGetFacePY(e, pyFace);
	entGetPos(e, pos);
	yaw = pyFace[1];

	offset[0] = sinf(yaw) * distance;
	offset[1] = 0;
	offset[2] = cosf(yaw) * distance;

	copyMat4(unitmat, matOut);
	addVec3(pos, offset, matOut[3]);

	return ExprFuncReturnFinished;
}

// Returns a point offset from the original point by + or - up to the amount specified for each axis
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PointAddRandomOffsetRadiusYDrop);
void exprFuncPointAddRandomOffsetRadiusYDrop(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_OUT pointOut, ACMD_EXPR_LOC_MAT4_IN pointIn, F32 maxRadius)
{
	F32 radius = randomFloatInRange(0, maxRadius);
	F32 angle = randomFloatInRange(0, TWOPI);

	copyMat4(pointIn, pointOut);

	pointOut[3][0] += cos(angle) * radius;
	pointOut[3][2] += sin(angle) * radius;

	worldSnapPosToGround(iPartitionIdx, pointOut[3], 30, -60, NULL);
}

// Raycasts from current position to target point and, if something is hit, backs the point a short distance away from the collision
// Avoid using this in general
AUTO_EXPR_FUNC(ai) ACMD_NAME(PointRaycastSafe);
void exprFuncPointRaycastSafe(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_OUT posOut, ACMD_EXPR_LOC_MAT4_IN posIn)
{
	Vec3 pos;
	WorldCollCollideResults results = {0};

	entGetPos(e, pos);
	copyMat4(unitmat, posOut);
	if(wcRayCollide(worldGetActiveColl(iPartitionIdx), pos, posIn[3], WC_FILTER_BIT_POWERS, &results))
	{
		Vec3 dir;
		F32 len;

		subVec3(results.posWorldImpact, pos, dir);
		len = normalVec3(dir);
		if(len > 5)
			len -= 5;
		else
			len = 0;
		scaleAddVec3(dir, len, pos, posOut[3]);
	}
	else 
		copyVec3(posIn[3], posOut[3]);
}

// Raycasts from current position towards target point at a given distance. 
// if something is hit, backs the point a short distance away from the collision
AUTO_EXPR_FUNC(ai) ACMD_NAME(PointRaycastDistance);
void exprFuncPointRaycastDistance(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_OUT posOut, ACMD_EXPR_LOC_MAT4_IN posIn, F32 distance, F32 verticalOffset)
{
	Vec3 startPos, endPos, dir;
	WorldCollCollideResults results = {0};

	copyMat3(unitmat, posOut);
	entGetPos(e, startPos);
	startPos[1] += verticalOffset; // add in a vertical step

	subVec3(posIn[3], startPos, dir);
	normalVec3(dir);
	scaleAddVec3(dir, distance, startPos, endPos);
	if(wcRayCollide(worldGetActiveColl(iPartitionIdx), startPos, endPos, WC_FILTER_BIT_POWERS, &results))
	{
		F32 len;
		
		len = distance3(results.posWorldImpact, startPos);
		if(len > 4.f)
			len -= 4.f;
		else
			len = 0;
		scaleAddVec3(dir, len, startPos, posOut[3]);
	}
	else 
		copyVec3(endPos, posOut[3]);
}


// BronzeAge inspired function to create a path for a collisionless movement entity 
AUTO_EXPR_FUNC(ai) ACMD_NAME(CreateThrownProjectilePath);
void exprFuncCreateThrownProjectilePath(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN posTarget, F32 distance, F32 verticalOffset)
{
	Vec3 vCastStartPos, vCastEndPos, vCastDir;
	F32 *eafWaypoints = NULL;
	WorldCollCollideResults results = {0};
	
	eafStackCreate(&eafWaypoints, 6);
	
	entGetPos(e, vCastStartPos);
	vCastStartPos[1] += verticalOffset; // add in a vertical step

	subVec3(posTarget[3], vCastStartPos, vCastDir);
	normalVec3(vCastDir);
	scaleAddVec3(vCastDir, distance, vCastStartPos, vCastEndPos);
	if(wcRayCollide(worldGetActiveColl(iPartitionIdx), vCastStartPos, vCastEndPos, WC_FILTER_BIT_POWERS, &results))
	{
		F32 fDistToImpact;
		fDistToImpact = distance3(results.posWorldImpact, vCastStartPos);
		if(fDistToImpact > 4.f)
			fDistToImpact -= 4.f;
		else
			fDistToImpact = 0;

		scaleAddVec3(vCastDir, fDistToImpact, vCastStartPos, vCastEndPos);
		eafPush3(&eafWaypoints, vCastEndPos);

		// check if we hit what might be the ground
		if (getAngleBetweenNormalizedUpVec3(results.normalWorld) < RAD(20.f))
		{
			Vec3 vLeft;
			F32 fDistRemaining = distance - fDistToImpact;

			crossVec3(results.normalWorld, vCastDir, vLeft);
			crossVec3(vLeft, results.normalWorld, vCastDir);
			normalVec3(vCastDir);

			scaleAddVec3(results.normalWorld, 0.5f, results.posWorldImpact, vCastStartPos);
			scaleAddVec3(vCastDir, fDistRemaining, vCastStartPos, vCastEndPos);
			if(wcRayCollide(worldGetActiveColl(iPartitionIdx), vCastStartPos, vCastEndPos, WC_FILTER_BIT_POWERS, &results))
			{
				fDistToImpact = distance3(results.posWorldImpact, vCastStartPos);
				if(fDistToImpact > 4.f)
					fDistToImpact -= 4.f;
				else
					fDistToImpact = 0;

				scaleAddVec3(vCastDir, fDistToImpact, vCastStartPos, vCastEndPos);
				eafPush3(&eafWaypoints, vCastEndPos);
			}
			else
			{
				eafPush3(&eafWaypoints, vCastEndPos);
			}
		}

	}
	else 
	{	
		// nothing hit, just add the endpoint
		eafPush3(&eafWaypoints, vCastEndPos);
	}

	aiMovementSetWaypointsExplicit(e, eafWaypoints);
}

// returns true if the entity has completed whatever movement it was trying to do
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntIsFinishedMovement);
int exprFuncEntIsFinishedMovement(ACMD_EXPR_SELF Entity* e)
{
	return ! e->aibase->currentlyMoving;
}


// returns true if the entity has completed whatever movement it was trying to do
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCloseToPathEnd);
int exprFuncEntCloseToPathEnd(ACMD_EXPR_SELF Entity* e)
{
	Vec3 vTargetPos;
	F32 dist;

	if (!e->aibase->currentlyMoving)
		return true;

	aiMovementGetTargetPosition(e, e->aibase, vTargetPos);
	dist = entGetDistanceXZ(e, NULL, NULL, vTargetPos, NULL);
	return dist < 5.f; // using same value as closeToPoint
}



// Finds the distance to the ground for the given entity. Somewhat expensive so please use carefully
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntFindGroundDistance);
ExprFuncReturnVal exprFuncEntFindGroundDistance(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_FLOAT_OUT floatOut, ACMD_EXPR_ENTARRAY_IN ent, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	Vec3 entPos;

	if(eaSize(ent) != 1)
	{
		*errString = "EntFindGroundDistance only works on one entity";
		return ExprFuncReturnError;
	}

	entGetPos((*ent)[0], entPos);
	*floatOut = -worldGetPointFloorDistance(worldGetActiveColl(iPartitionIdx), entPos, 1, 100, NULL);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetPowerEntCreateTargetPoint);
ExprFuncReturnVal exprFuncGetPowerEntCreateTargetPoint(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_LOC_MAT4_OUT pointOut)
{
	if (e && e->aibase)
	{
		copyMat4(unitmat, pointOut);
		copyVec3(e->aibase->vecTargetPowersEntCreate, pointOut[3]);
		return ExprFuncReturnFinished;
	}

	return ExprFuncReturnError;
}

// Encounters
//---------------------------------------------------------------

ExprFuncReturnVal exprFuncGetEncounterEntsHelper(ExprContext *context,
												 ACMD_EXPR_PARTITION iPartitionIdx,
												 ACMD_EXPR_ENTARRAY_OUT entsOut,
												 ACMD_EXPR_ERRSTRING errString,
												 int all,
												 int dead)
{
	GameEncounter *pEncounter = exprContextGetVarPointerPooled(context, g_Encounter2VarName, parse_GameEncounter);
	OldEncounter *pOldEncounter = NULL;

	// Check old encounter system
	if (!pEncounter && gConf.bAllowOldEncounterData) 
	{
		pOldEncounter = exprContextGetVarPointerUnsafePooled(context, g_EncounterVarName);
	}

	if (!pEncounter && !pOldEncounter)
	{
		Entity *pEnt = exprContextGetSelfPtr(context);
		pEncounter = pEnt->pCritter->encounterData.pGameEncounter;
		if (!pEncounter && gConf.bAllowOldEncounterData)
		{
			pOldEncounter = pEnt->pCritter->encounterData.parentEncounter;
		}
	}

	if (pEncounter) 
	{
		encounter_GetEntities(iPartitionIdx, pEncounter, entsOut, !all, true);
		return ExprFuncReturnFinished;
	}
	else if (pOldEncounter) 
	{
		int i, n;

		n = eaSize(&pOldEncounter->ents);

		for(i = 0; i < n; i++)
		{
			Entity* encBE = pOldEncounter->ents[i];
			if(encBE && (dead || aiIsEntAlive(encBE)) &&
				(all || !exprFuncHelperShouldExcludeFromEntArray(encBE)))
			{
				eaPush(entsOut, encBE);
			}
		}
		return ExprFuncReturnFinished;
	}
	else
	{
		estrPrintf(errString, "No encounter in context, so cannot do GetEncounterEnts()");
		return ExprFuncReturnError;
	}

}

// Returns a new ent array with all entities from the current encounter ignoring all
// invisible, untargetable and unselectable ones
AUTO_EXPR_FUNC(entity,encounter) ACMD_NAME(GetEncounterEnts);
ExprFuncReturnVal exprFuncGetEncounterEnts(ExprContext *context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncGetEncounterEntsHelper(context, iPartitionIdx, entsOut, errString, false, false);
}

// Returns a new ent array with all entities from the current encounter including all
// invisible untargetable and unselectable ones
AUTO_EXPR_FUNC(entity,encounter) ACMD_NAME(GetEncounterEntsAll);
ExprFuncReturnVal exprFuncGetEncounterEntsAll(ExprContext *context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncGetEncounterEntsHelper(context, iPartitionIdx, entsOut, errString, true, false);
}

// Returns a new ent array with all dead entities from the current encounter including all
// invisible, untargetable and unselectable ones
AUTO_EXPR_FUNC(Entity,encounter) ACMD_NAME(GetEncounterEntsDeadAll);
ExprFuncReturnVal exprFuncGetEncounterEntsDeadAll(ExprContext *context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncGetEncounterEntsHelper(context, iPartitionIdx, entsOut, errString, true, true);
}

static void GetStaticEncounterEnts(ACMD_EXPR_ENTARRAY_OUT entsOut, OldEncounter* encounter, bool filter)
{
	int i, n = eaSize(&encounter->ents);
	for(i = 0; i < n; i++)
	{
		Entity* encBE = encounter->ents[i];
		if(encBE && aiIsEntAlive(encBE) && !(filter && exprFuncHelperShouldExcludeFromEntArray(encBE)))
			eaPush(entsOut, encBE);
	}
}

// Returns a new ent array with all entities from the specified static encounter
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(GetStaticEncounterEnts);
ExprFuncReturnVal exprFuncGetStaticEncounterEnts(ExprContext* context,
												 ACMD_EXPR_PARTITION iPartitionIdx,
												 ACMD_EXPR_ENTARRAY_OUT entsOut,
												 const char* staticEncName,
												 ACMD_EXPR_ERRSTRING errStr)
{
	GameEncounter *pEncounter = encounter_GetByName(staticEncName, context->scope);
	if (pEncounter)
	{
		encounter_GetEntities(iPartitionIdx, pEncounter, entsOut, true, false);
		return ExprFuncReturnFinished;
	}
	else if (gConf.bAllowOldEncounterData)
	{
		OldEncounter* encounter = oldencounter_FromStaticEncounterName(iPartitionIdx, (char*)staticEncName);

		// Prevent errors during layer reloads
		if (!g_EncounterMasterLayer){
			return ExprFuncReturnFinished;
		}

		if(!encounter)
		{
			if(!staticEncName[0])
				estrPrintf(errStr, "\"\" is not allowed as a static encounter name (did you forget to fill out an extern var?");
			else
				estrPrintf(errStr, "%s is not a valid static encounter name", staticEncName);

			return ExprFuncReturnError;
		}

		GetStaticEncounterEnts(entsOut, encounter, true);

		return ExprFuncReturnFinished;
	}
	else
	{
		if(!staticEncName[0])
			estrPrintf(errStr, "\"\" is not allowed as a encounter name (did you forget to fill out an extern var?");
		else
			estrPrintf(errStr, "%s is not a valid encounter name", staticEncName);

		return ExprFuncReturnError;
	}
}

// Returns a new ent array with all entities from the specified static encounter including all
// untargetable, unselectable and invisible ones
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(GetStaticEncounterEntsAll);
ExprFuncReturnVal exprFuncGetStaticEncounterEntsAll(ExprContext* context,
												    ACMD_EXPR_PARTITION iPartitionIdx,
													ACMD_EXPR_ENTARRAY_OUT entsOut,
													const char* staticEncName,
													ACMD_EXPR_ERRSTRING errStr)
{
	GameEncounter *pEncounter = encounter_GetByName(staticEncName, context->scope);
	if (pEncounter)
	{
		encounter_GetEntities(iPartitionIdx, pEncounter, entsOut, false, false);
		return ExprFuncReturnFinished;
	}
	else if (gConf.bAllowOldEncounterData)
	{
		OldEncounter* encounter = oldencounter_FromStaticEncounterName(iPartitionIdx, (char*)staticEncName);

		// Prevent errors during layer reloads
		if (!g_EncounterMasterLayer){
			return ExprFuncReturnFinished;
		}

		if(!encounter)
		{
			if(!staticEncName[0])
				estrPrintf(errStr, "\"\" is not allowed as a static encounter name (did you forget to fill out an extern var?");
			else
				estrPrintf(errStr, "%s is not a valid static encounter name", staticEncName);

			return ExprFuncReturnError;
		}

		GetStaticEncounterEnts(entsOut, encounter, false);

		return ExprFuncReturnFinished;
	}
	else
	{
		if(!staticEncName[0])
			estrPrintf(errStr, "\"\" is not allowed as a static encounter name (did you forget to fill out an extern var?");
		else
			estrPrintf(errStr, "%s is not a valid static encounter name", staticEncName);

		return ExprFuncReturnError;
	}
}

// Like GetStaticEncounterEnts, but doesn't throw an error if the encounter doesn't exist
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(GetStaticEncounterEntsNoErrorChecking);
ExprFuncReturnVal exprFuncGetStaticEncounterEntsNoErrorChecking(ExprContext* context,
																ACMD_EXPR_PARTITION iPartitionIdx,
																ACMD_EXPR_ENTARRAY_OUT entsOut,
																const char* staticEncName,
																ACMD_EXPR_ERRSTRING errStr)
{
	GameEncounter *pEncounter = encounter_GetByName(staticEncName, context->scope);
	if (pEncounter)
	{
		encounter_GetEntities(iPartitionIdx, pEncounter, entsOut, false, false);
		return ExprFuncReturnFinished;
	}
	else if (gConf.bAllowOldEncounterData)
	{
		OldEncounter* encounter = oldencounter_FromStaticEncounterName(iPartitionIdx, (char*)staticEncName);

		// Still check for null encounter name, but don't throw an error if the encounter wasn't found
		if(!encounter)
		{
			if(!staticEncName[0])
			{
				estrPrintf(errStr, "\"\" is not allowed as a static encounter name (did you forget to fill out an extern var?");
				return ExprFuncReturnError;
			}

			return ExprFuncReturnFinished;
		}

		GetStaticEncounterEnts(entsOut, encounter, false);

		return ExprFuncReturnFinished;
	}
	{
		if(!staticEncName[0])
		{
			estrPrintf(errStr, "\"\" is not allowed as a static encounter name (did you forget to fill out an extern var?");
			return ExprFuncReturnError;
		}

		return ExprFuncReturnFinished;
	}
}

// Removes all entities from the entarray that do not match the passed in critter name
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropCritterName);
void exprFuncEntCropCritterName(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* name)
{
	int i;

	for(i = eaSize(entsInOut) - 1; i >= 0; i--)
	{
		CritterDef* critterDef;
		Critter* critter = (*entsInOut)[i]->pCritter;
		if (critter && (critterDef = GET_REF(critter->critterDef)) && critterDef->pchName)
		{
			if(stricmp(critterDef->pchName, name))
				eaRemoveFast(entsInOut, i);
		}
		else
			eaRemoveFast(entsInOut, i);
	}
}

// Removes all entities from the entarray that do not match the passed in critter group
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropCritterGroup);
void exprFuncEntCropCritterGroup(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* name)
{
	int i;

	for(i = eaSize(entsInOut) - 1; i >= 0; i--)
	{
		CritterGroup *group = entGetCritterGroup((*entsInOut)[i]);

		if(!group || stricmp(group->pchName, name))
			eaRemoveFast(entsInOut, i);
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetCritterDefName);
const char* exprFuncGetCritterDefName(ACMD_EXPR_SELF Entity *e)
{
	CritterDef *cdef = NULL;

	if(!e->pCritter)
		return NULL;

	cdef = GET_REF(e->pCritter->critterDef);

	if(!cdef)
		return NULL;

	return cdef->pchName;
}

// Removes all entities from the entarray that do not match the passed in actor name.
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropActorName);
void exprFuncEntCropActorName(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* actorName)
{
	int i;
	char* eStrTmp = NULL;

	estrStackCreate(&eStrTmp);
	for(i = eaSize(entsInOut) - 1; i >= 0; i--)
	{
		Critter* critter = (*entsInOut)[i]->pCritter;
		if (critter && critter->encounterData.pGameEncounter)
		{
			if (stricmp(actorName, encounter_GetActorName(critter->encounterData.pGameEncounter, critter->encounterData.iActorIndex)) != 0)
				eaRemoveFast(entsInOut, i);
		}
		else if (critter && critter->encounterData.sourceActor && gConf.bAllowOldEncounterData)
		{
			oldencounter_GetActorName(critter->encounterData.sourceActor, &eStrTmp);
			if(stricmp(eStrTmp, actorName))
				eaRemoveFast(entsInOut, i);
		}
		else
			eaRemoveFast(entsInOut, i);
	}

	estrDestroy(&eStrTmp);
}

AUTO_EXPR_FUNC(ai, encounter, OpenMission, layerFSM) ACMD_NAME(EntCropCritterRank);
ExprFuncReturnVal exprFuncEntCropCritterRank(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* pcRank, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	
	pcRank = allocAddString(pcRank);
	
	if(!critterRankExists(pcRank))
	{
		estrPrintf(errString, "Unable to find critter rank: %s", pcRank);
		return ExprFuncReturnError;
	}

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Critter *c = (*entsInOut)[i]->pCritter;

		if(!c || c->pcRank!=pcRank)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter, OpenMission) ACMD_NAME(EntCropIsNotOfCritterRank);
ExprFuncReturnVal exprFuncEntCropIsNotOfCritterRank(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* pcRank, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	
	pcRank = allocAddString(pcRank);
	
	if(!critterRankExists(pcRank))
	{
		estrPrintf(errString, "Unable to find critter rank: %s", pcRank);
		return ExprFuncReturnError;
	}

	for(i = eaSize(entsInOut) - 1; i >= 0; i--)
	{
		Critter *c = (*entsInOut)[i]->pCritter;

		if(c && c->pcRank == pcRank)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

void sendEventMessage(FSMContext* fsmContext, GameEvent *ev, GameEvent *specific, int value)
{
	aiMessageSendAbstract(specific->iPartitionIdx, fsmContext, ev->pchEventName, NULL, value, NULL);
}

AUTO_COMMAND_QUEUED();
void stopTracking(ACMD_POINTER FSMContext* context, ACMD_POINTER GameEvent *ev)
{
	devassertmsg(ev->iPartitionIdx!=PARTITION_ENT_BEING_DESTROYED, "Event sent with invalid partition");
	eventtracker_StopTracking(ev->iPartitionIdx, ev, context);
	StructDestroy(parse_GameEvent, ev);
}

// Tells the AI to start recording a specific encounter event, but uses the alias to send the message.
// NOTE: Call this before the event can happen (generally in start state on entry first)
AUTO_EXPR_FUNC(ai, layerFSM) ACMD_NAME(GlobalEventAddListenAliased);
ExprFuncReturnVal exprFuncGlobalEventAddListenAliased(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_SC_TYPE(Event) const char* eventName, const char* eventAlias, ACMD_EXPR_ERRSTRING errString)
{
	GameEvent *ev;
	const char* eventInCache;
	FSMLDGenericEArray *mydata;
	int i;
	static char *estr = NULL;

	if(!eventName[0])
		return ExprFuncReturnFinished;

	eventInCache = allocAddString(eventName);
	mydata = getMyData(context, parse_FSMLDGenericEArray, 0);

	ev = gameevent_EventFromString(eventName);

	if (ev)
	{
		GameEvent *listener = NULL;
		Entity *e = exprContextGetSelfPtr(context);
		FSMContext* fsmContext = exprContextGetFSMContext(context);

		devassert(fsmContext);

		ev->iPartitionIdx = iPartitionIdx;
		ev->pchEventName = (eventAlias && eventAlias[0]) ? allocAddString(eventAlias) : eventInCache;

		devassertmsg(ev->pchEventName, "Trying to listen to event without a name");

		for(i=0; i<eaSize(&mydata->myEArray); i++)
		{
			GameEvent *alreadyListening = (GameEvent*)mydata->myEArray[i];
			if(alreadyListening->pUserData==eventInCache)
			{
				if(alreadyListening->pchEventName!=ev->pchEventName)
				{
					estrPrintf(errString, "Attempting to listen to two matching events with different names - %s and %s (possibly one is aliased?)",
						alreadyListening->pchEventName, ev->pchEventName);
					StructDestroySafe(parse_GameEvent, &ev);
					return ExprFuncReturnError;
				}
				StructDestroySafe(parse_GameEvent, &ev);
				return ExprFuncReturnFinished;
			}
		}

		// Start tracking the event
		if(ev->tMatchSource || ev->tMatchSourceTeam)
		{
			if(!e)
			{
				estrPrintf(errString, "Told a layer FSM to match source/team in an event.  Impossible.");
				StructDestroySafe(parse_GameEvent, &ev);
				return ExprFuncReturnError;
			}
			ev->sourceEntRef = entGetRef(e);
		}

		if(ev->tMatchTarget || ev->tMatchTargetTeam)
		{
			if(!e)
			{
				estrPrintf(errString, "Told a layer FSM to match target/team in an event.  Impossible.");
				StructDestroySafe(parse_GameEvent, &ev);
				return ExprFuncReturnError;
			}
			ev->targetEntRef = entGetRef(e);
		}

		// *
		// * Any code added above this line and after if (ev) that does an early return must destroy ev, otherwise ev will leak
		// *

		listener = eventtracker_StartTracking(ev, NULL, fsmContext, sendEventMessage, sendEventMessage);
		if(!listener)
		{
			estrPrintf(errString, "Failed to listen to event %s - perhaps check the mapname", eventInCache);
			return ExprFuncReturnError;
		}

		listener->pUserData = (void*)eventInCache;

		if(!fsmContext->onDestroyCleanup)
			fsmContext->onDestroyCleanup = CommandQueue_Create(8, false);

		eaPush(&mydata->myEArray, (void*)listener);
		QueuedCommand_stopTracking(fsmContext->onDestroyCleanup, fsmContext, ev);
	}

	return ExprFuncReturnFinished;
}

// Tells the AI to start recording a specific encounter event in its message database. This is the
// only way to query encounter events through the AI.
// NOTE: make sure you call this before the event happens, or you'll miss it
AUTO_EXPR_FUNC(ai, layerFSM) ACMD_NAME(GlobalEventAddListen);
ExprFuncReturnVal exprFuncGlobalEventAddListen(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_SC_TYPE(Event) const char* eventName, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncGlobalEventAddListenAliased(context, iPartitionIdx, eventName, NULL, errString);
}

// Returns a point offset from the original point by the actor's relative position in the encounter,
// similar to PatrolOffset()
AUTO_EXPR_FUNC(ai) ACMD_NAME(PointAddSpawnOffset);
void exprFuncPointAddSpawnOffset(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_LOC_MAT4_OUT pointOut, ACMD_EXPR_LOC_MAT4_IN pointIn)
{
	copyMat4(pointIn, pointOut);

	addVec3(pointOut[3], be->aibase->spawnOffset, pointOut[3]);
}

static void exprFuncSetAttackTargetInternal(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING_STATIC errStr, int curStateOnly, int lockTarget)
{
	AIVarsBase* aib = e->aibase;
	FSMLDGenericSetData* mydata = getMyData(context, parse_FSMLDGenericSetData, (U64)"SetAttackTargetCurState");

	if(eaSize(entIn) > 1)
	{
		*errStr = "Cannot set attack target to more than one entity";
		return;
	}
	else if(!eaSize(entIn))
	{
		*errStr = "Cannot set attack target because the ent array is empty";
		return;
	}

	if(lockTarget)
	{
		if(curStateOnly)
			exprFuncAddConfigModCurStateOnly(e, context, "DontChangeAttackTarget", "1", NULL);
		else
			exprFuncAddConfigMod(e, context, "DontChangeAttackTarget", "1", NULL);
	}

	aiSetAttackTarget(e, aib, (*entIn)[0], NULL, true);
}

// Overrides the critter's current attack target while in the current state
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetAttackTargetCurState);
void exprFuncSetAttackTargetCurState(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	exprFuncSetAttackTargetInternal(e, context, entIn, errStr, true, true);
}

// Overrides the critter's current attack target permanently (or until ClearAttackTarget() is called)
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetAttackTarget);
void exprFuncSetAttackTarget(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	exprFuncSetAttackTargetInternal(e, context, entIn, errStr, false, true);
}

// Overrides the critter's current attack but doesn't keep it from changing its target afterwards
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetAttackTargetNoLock);
void exprFuncSetAttackTargetNoLock(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	exprFuncSetAttackTargetInternal(e, context, entIn, errStr, false, false);
}

ExprFuncReturnVal aiSetPreferredAttackTargetInternal(Entity *e, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING errString)
{
	AIVarsBase *aib = e->aibase;
	if(eaSize(entIn)>1)
	{
		estrPrintf(errString, "Cannot set preferred target to more than one entity");
		return ExprFuncReturnError;
	}

	if(!eaSize(entIn))
	{
		estrPrintf(errString, "Cannot set preferred target to empty entity array");
		return ExprFuncReturnError;
	}

	return aiSetPreferredAttackTarget(e, aib, (*entIn)[0], false) ? ExprFuncReturnFinished : ExprFuncReturnError;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(SetPreferredTarget);
ExprFuncReturnVal exprFuncSetPreferredTarget(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING errString)
{
	return aiSetPreferredAttackTargetInternal(e, entIn, errString);
}

AUTO_COMMAND_QUEUED();
void aiClearPreferredTarget(ACMD_POINTER Entity *e)
{
	AIVarsBase *aib = e->aibase;

	aiClearPreferredAttackTarget(e, aib);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(ClearPreferredTarget);
void exprFuncClearPreferredTarget(ACMD_EXPR_SELF Entity *e)
{
	aiClearPreferredTarget(e);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(SetPreferredTargetCurState);
ExprFuncReturnVal exprFuncSetPreferredTargetCurState(ACMD_EXPR_SELF Entity* e, ExprContext *context, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING errString)
{
	AIVarsBase* aib = e->aibase;
	FSMLDGenericSetData* mydata = getMyData(context, parse_FSMLDGenericSetData, (U64)__FUNCTION__);

	if(!mydata->setData)
	{
		ExprFuncReturnVal ret = aiSetPreferredAttackTargetInternal(e, entIn, errString);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			estrPrintf(errString, "Trying to call CurStateOnly func without exitHandler reference");
			return ExprFuncReturnError;
		}

		QueuedCommand_aiClearPreferredTarget(exitHandlers, e);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDGenericSetData, localData, (U64)__FUNCTION__);
		mydata->setData = 1;

		return ret;
	}

	return ExprFuncReturnFinished;
}

// Clears the critter's attack target
AUTO_EXPR_FUNC(ai) ACMD_NAME(ClearAttackTarget);
void exprFuncClearAttackTarget(ACMD_EXPR_SELF Entity* e)
{
	aiSetAttackTarget(e, e->aibase, NULL, NULL, true);
}

// Sets the entity's subtarget category
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetSubTargetCategory);
ExprFuncReturnVal exprFuncSetSubTargetCategory(ACMD_EXPR_SELF Entity *e, const char *subtarget, ACMD_EXPR_ERRSTRING errString)
{
	PowerSubtargetCategory *cat = powersubtarget_GetCategoryByName(subtarget);

	if(!cat)
	{
		estrPrintf(errString, "Unknown subtarget category: %s", subtarget);
		return ExprFuncReturnError;
	}

	if(e->pChar)
	{
		character_SetSubtargetCategory(e->pChar, cat);
	}

	return ExprFuncReturnFinished;
}

// Clears the entity's subtarget category
AUTO_EXPR_FUNC(ai) ACMD_NAME(ClearSubTargetCategory);
void exprFuncClearSubTargetCategory(ACMD_EXPR_SELF Entity *e)
{
	if(e->pChar)
		character_ClearSubtarget(e->pChar);
}

static bool GetRandomNumberExhaustive(FSMLDGenericU64* mydata, int max, int* intOut)
{
	if(!mydata->myU64)
		mydata->myU64 = (1ll << (max+1)) - 1;

	while(mydata->myU64)
	{
		U64 index = randomIntRange(0, 63);
		U64 bit = 1ll << index;
		if(mydata->myU64 & bit)
		{
			mydata->myU64 -= bit;

			if (index > max)
			{
				continue;
			}
			*intOut = index;
			return true;
		}
	}
	return false;
}

// Returns numbers from 0 to max (currently up to 62), one at a time exhaustively (restarts when
// done)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(RandomNumberExhaustive);
ExprFuncReturnVal exprFuncRandomNumberExhaustive(ExprContext* context, ACMD_EXPR_INT_OUT intOut, int max, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	FSMLDGenericU64* mydata = getMyData(context, parse_FSMLDGenericU64, PTR_TO_UINT("RandomNumberExhaustive"));

	if(max < 0 || max > 62)
	{
		*errString = "Not allowed to have values less than 0 or greater than 62 for RandomNumberExhaustive";
		return ExprFuncReturnError;
	}
	if(!max)
	{
		*intOut = 0;
		return ExprFuncReturnFinished;
	}
	
	if(!GetRandomNumberExhaustive(mydata, max, intOut))
	{
		// It's possible that there were no valid values on the first run. 
		// If this is the case, do another run with a new seed.
		if(!GetRandomNumberExhaustive(mydata, max, intOut))
		{
			*errString = "RandomNumberExhaustive failed to generate a random number";
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(GetLeashPoint);
ExprFuncReturnVal exprFuncGetLeashPoint(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_LOC_MAT4_OUT matOut)
{
	AIVarsBase* aib = e->aibase;
	
	identityMat4(matOut);
	aiGetLeashPosition(e, aib, matOut[3]);
	return ExprFuncReturnFinished;
}

// Returns true if any of the entities were damaged in the last X seconds
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCheckDamagedLastXSec);
int exprFuncEntCheckDamagedLastXSec(ACMD_EXPR_ENTARRAY_IN entsIn, F32 time)
{
	S32 i;
	
	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* e = (*entsIn)[i];
		int partitionIdx = entGetPartitionIdx(e);
		
		if (ABS_TIME_SINCE_PARTITION(partitionIdx, e->aibase->time.lastDamage[AI_NOTIFY_TYPE_DAMAGE]) <= SEC_TO_ABS_TIME(time))
			return 1;
	}

	return 0;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(SetLeashTypeEntity);
ExprFuncReturnVal exprFuncSetLeashTypeEntity(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if (eaSize(entIn) == 0)
		return ExprFuncReturnFinished;

	if(eaSize(entIn)!=1)
	{
		estrPrintf(errString, "SetLeashTypeEntity called with zero or more than one ent.");
		return ExprFuncReturnError;
	}

	aiSetLeashTypeEntity(e, e->aibase, (*entIn)[0]);
	return ExprFuncReturnFinished;
}

// Returns the entity the AI is leashed to.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetLeashToEntity);
void exprFuncGetLeashToEntity(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Entity *pEnt = aiGetLeashEntity(e, e->aibase);
	if (pEnt)
		eaPush(entsOut, pEnt);
}

// Turns any destructables near the critter with the given name into entities
// USE WITH CAUTION
AUTO_EXPR_FUNC(ai) ACMD_NAME(ForceDestructiblesToEnts);
void exprFuncForceDestructiblesToEnts(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* destructibleName, F32 distance)
{
	im_ForceNodesToEntities(e, distance, destructibleName, entsOut);
}

// Shorthand for "TimeSince(curStateTracker.lastEntryTime)" - 
//  returns number of seconds the FSM has been in the current state
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(TimeSinceStateEntry);
F32 exprFuncTimeSinceStateEntry(ExprContext *context, ACMD_EXPR_PARTITION iPartitionIdx)
{
	FSMContext *fsmContext = exprContextGetFSMContext(context);

	if(!SAFE_MEMBER(fsmContext, curTracker))
		return 0;

	return ABS_TIME_TO_SEC(ABS_TIME_SINCE_PARTITION(iPartitionIdx, fsmContext->curTracker->lastEntryTime));
}

// ------------------------------------------------------------------------------------------------------------------

// Sets a override FSM that is meant to be called by power's AICommand attribmods
// The FSM can also be set on players, and it will override the player's input and have the FSM control the entity
// this FSM override will expire when the AICommand attribmod expires
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetPowersOverrideFSM);
ExprFuncReturnVal exprFuncSetPowersOverrideFSM(ACMD_EXPR_SELF Entity* e, ExprContext *context, const char *pchFSMName, ACMD_EXPR_ERRSTRING errString)
{
	AIVarsBase *aib = e->aibase;
	CommandQueue* exitHandlers = NULL;
	U32 fsmOverrideID;

	exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

	if(!exitHandlers)
	{
		estrPrintf(errString, "Trying to call SetHostileToCivilians func without exitHandler reference");
		return ExprFuncReturnError;
	}

	fsmOverrideID = aiPushOverrideFSMByName(e, pchFSMName);
	if (fsmOverrideID)
	{
		if (entIsPlayer(e))
		{	
			mmDetachFromClient(e->mm.movement, NULL);
			e->pPlayer->bIgnoreClientPowerActivations = true;
			entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);

			aib->doProximity = true;
			aib->doBScript = true;
			aib->doAttackAI = true;

			mrTacticalNotifyPowersStart(e->mm.mrTactical, TACTICAL_OVERRIDEFSM_UID, TDF_ALL, pmTimestamp(0));
		}
		
		QueuedCommand_ClearPowersOverrideFSM(exitHandlers, e, fsmOverrideID);
	}


	return ExprFuncReturnFinished;
}

// ------------------------------------------------------------------------------------------------------------------
static void resetPlayerAISettings(Entity *e)
{
	MovementClient *mc = SAFE_MEMBER3(e, pPlayer, clientLink, movementClient);
	mmAttachToClient(e->mm.movement, mc);

	e->pPlayer->bIgnoreClientPowerActivations = false;
	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);

	e->aibase->doProximity = false;
	e->aibase->doBScript = false;
	e->aibase->doAttackAI = false;
	mrTacticalNotifyPowersStop(e->mm.mrTactical, TACTICAL_OVERRIDEFSM_UID, pmTimestamp(0) );
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND_QUEUED();
void ClearPowersOverrideFSM(ACMD_POINTER Entity *e, U32 overrideFSMID)
{
	if (e && e->aibase)
	{
		aiRemoveOverrideFSM(e, overrideFSMID);
		
		if (entIsPlayer(e))
		{
			// check if we still have another override FSM that is of this type
			// if so, don't give back to player
			if (eaSize(&e->aibase->eaOverrideFSMStack) == 0)
			{
				resetPlayerAISettings(e);
			}
		}

	}
}


// ------------------------------------------------------------------------------------------------------------------
void ClearAllPowersOverrideFSM(Entity *e)
{
	if (e && e->aibase)
	{
		if (eaSize(&e->aibase->eaOverrideFSMStack) != 0)
		{
			aiRemoveAllOverrideFSMs(e);
			if (entIsPlayer(e))
			{
				resetPlayerAISettings(e);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------

// Sets the combat coherency override information in the AI
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetCombatCoherencyOverride);
void exprFuncSetCombatCoherencyOverride(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_LOC_MAT4_IN leashPos, F32 distance)
{
	if (be && be->aibase)
	{
		be->aibase->leashTypeOverride = AI_LEASH_TYPE_RALLY_POSITION;
		copyVec3(leashPos[3], be->aibase->rallyPositionOverride);
		be->aibase->coherencyCombatDistOverride = distance;
	}
}

// ------------------------------------------------------------------------------------------------------------------

// Removes the combat coherency override information from the AI
AUTO_EXPR_FUNC(ai) ACMD_NAME(RemoveCombatCoherencyOverride);
void exprFuncRemoveCombatCoherencyOverride(ACMD_EXPR_SELF Entity* be)
{
	if (be && be->aibase)
	{
		be->aibase->leashTypeOverride = AI_LEASH_TYPE_DEFAULT;
		zeroVec3(be->aibase->rallyPositionOverride);
		be->aibase->coherencyCombatDistOverride = 0.f;
	}
}

// ------------------------------------------------------------------------------------------------------------------

// Returns true if all entities passed are in the status table and visible.
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntsVisibleInMyStatusTable);
bool exprFuncEntsVisibleInMyStatusTable(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	S32 i;
	bool bVisible = false;

	if (e && eaSize(entsIn) > 0)
	{
		bVisible = true;

		for(i = 0; i < eaSize(entsIn); i++)
		{
			Entity *pEnemyEnt = *entsIn[i];

			if (pEnemyEnt)
			{
				AIStatusTableEntry *status = aiStatusFind(e, e->aibase, pEnemyEnt, false);

				if (!status || !status->visible)
				{
					bVisible = false;
					break;
				}
			}
			else
			{
				bVisible = false;
				break;
			}
		}
	}

	return bVisible;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropClass);
ExprFuncReturnVal exprFuncEntCropClass(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_DICT(CharacterClass) char *className, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	CharacterClass* pClass = characterclasses_FindByName(className);
	if(!pClass)
	{
		estrPrintf(errString, "Unable to find CharacterClass: %s", className);
		return ExprFuncReturnError;
	}
	for(i = eaSize(entsInOut) - 1; i >= 0; i--)
	{
		Entity* e = (*entsInOut)[i];
		if (!e->pChar || GET_REF(e->pChar->hClass) != pClass)
		{
			eaRemoveFast(entsInOut, i);
		}
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropClassCategory);
ExprFuncReturnVal exprFuncEntCropClassCategory(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char *classCategoryName, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	CharClassCategory eCategory = StaticDefineIntGetInt(CharClassCategoryEnum, classCategoryName);
	if(eCategory < 0)
	{
		estrPrintf(errString, "Unable to find CharacterClassCategory: %s", classCategoryName);
		return ExprFuncReturnError;
	}
	for(i = eaSize(entsInOut) - 1; i >= 0; i--)
	{
		Entity* e = (*entsInOut)[i];
		if (e->pChar)
		{
			CharacterClass *pClass = e->pChar ? GET_REF(e->pChar->hClass) : NULL;
			if (!pClass || pClass->eCategory != eCategory)
			{
				eaRemoveFast(entsInOut, i);
			}
		}
	}
	return ExprFuncReturnFinished;
}
