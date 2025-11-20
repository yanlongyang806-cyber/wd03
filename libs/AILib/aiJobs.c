#include "aiJobs.h"

#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiTeam.h"

#include "Entity_h_ast.h"
#include "encounter_common.h"
#include "estring.h"
#include "Expression.h"
#include "MemoryPool.h"
#include "StateMachine.h"
#include "StringCache.h"

MP_DEFINE(AIJob);

AIJob* aiJobCreate(void)
{
	MP_CREATE(AIJob, 4);
	return MP_ALLOC(AIJob);
}

void aiJobDestroy(AIJob* job)
{
	if(job->assignedBE)
		aiJobUnassign(entFromEntityRefAnyPartition(job->assignedBE), job);

	if(job->fsmContext)
		fsmContextDestroy(job->fsmContext);
	if(job->exprContext)
		exprContextDestroy(job->exprContext);

	eaDestroyEx(&job->subJobs, aiJobDestroy);

	MP_FREE(AIJob, job);
}

AIJob* aiJobAdd(AIJob*** jobs, AIJobDesc* desc, int iPartitionIdx)
{
	AIJob* job = aiJobCreate();

	job->desc = desc;
	eaPush(jobs, job);

	job->fsmContext = fsmContextCreateByName(desc->fsmName, "Combat");
	job->exprContext = exprContextCreate();

	aiSetupExprContext(NULL, NULL, iPartitionIdx, job->exprContext, false);

	return job;
}

void aiClearJobs(Entity* e, AIJobDesc **jobDecs)
{
	AIVarsBase *aib = e->aibase;
	AITeam *team;

	if(!aib)
		return;

	team = aiTeamGetAmbientTeam(e, aib);

	if(team)
	{
		aiTeamClearJobs(team, jobDecs);

		if(!eaSize(&team->jobs))
			eaDestroy(&team->jobs);
	}

	team = aiTeamGetCombatTeam(e, aib);

	if(team && team->combatTeam)
	{
		ANALYSIS_ASSUME(team != NULL);
		aiTeamClearJobs(team, jobDecs);

		if(!eaSize(&team->jobs))
			eaDestroy(&team->jobs);
	}
}

void aiJobAssign(Entity* be, AIJob* job)
{
	// exit the state for the previous entity so that it stops moving
	// and the next entity will start its state properly
	if(job->assignedBE)
	{
		Entity* formerBE = entFromEntityRef(entGetPartitionIdx(be), job->assignedBE);
		aiJobUnassign(formerBE, job);
	}

	// assignee has to stop doing their current job too if they have one
	if(be->aibase->job)
	{
		// do all exit handlers for the FSM you're switching away from
		aiJobUnassign(be, be->aibase->job);
	}
	else if (be->aibase->fsmContext)
	{	
		fsmExitCurState(be->aibase->fsmContext);
	}

	job->assignedBE = entGetRef(be);
	be->aibase->job = job;

	exprContextSetSelfPtr(job->exprContext, be);
	exprContextSetPointerVar(job->exprContext, "me", be, parse_Entity, false, true);

	job->fsmContext->messages = be->aibase->fsmMessages;
}

void aiJobUnassign(Entity* e, AIJob* job)
{
	ExprLocalData ***data = NULL;

	if(!job->assignedBE)
		return;

	if(e)
		devassert(entGetRef(e) == job->assignedBE);

	FOR_EACH_IN_STASHTABLE(job->fsmContext->stateTrackerTable, FSMStateTrackerEntry, tracker)
	{
		if(tracker && tracker->localData)
			deleteMyData(job->exprContext, parse_FSMLDPatrol, &tracker->localData, 0);			// Because patrol data lives forever, clear it all
	}
	FOR_EACH_END;
	fsmExitCurState(job->fsmContext);
	job->fsmContext->messages = NULL;
	job->assignedBE = 0;

	if(e)
		e->aibase->job = NULL;
}

int aiJobGenerateExpressions(AIJobDesc* desc)
{
	int success = true;
	int i, n = eaSize(&desc->subJobDescs);

	if(desc->jobRequires)
		success &= exprGenerate(desc->jobRequires, aiGetStaticCheckExprContext());
	if(desc->jobRating)
		success &= exprGenerate(desc->jobRating, aiGetStaticCheckExprContext());

	for(i = 0; i < n; i++)
		success &= aiJobGenerateExpressions(desc->subJobDescs[i]);

	return success;
}

const char* aiGetJobName(Entity* be)
{
	static char* jobName = NULL;

	if(be->aibase && be->aibase->job)
		estrPrintf(&jobName, "%s", be->aibase->job->desc->jobName);
	else
		estrPrintf(&jobName, "None");

	return jobName;
}

AUTO_COMMAND ACMD_NAME(aiPrintJob) ACMD_LIST(gEntConCmdList);
void entConAiPrintJob(Entity* be)
{
	printf("%s\n", aiGetJobName(be));
}

#include "autoGen/aiJobs_h_ast.c"
