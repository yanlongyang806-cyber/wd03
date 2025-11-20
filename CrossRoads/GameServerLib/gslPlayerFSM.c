#include "gslPlayerFSM.h"

// Utilities lib
#include "earray.h"
#include "Expression.h"
#include "NameList.h"
#include "StateMachine.h"
#include "StringCache.h"

// World Lib
#include "WorldGrid.h"
#include "WorldLib.h"

// AILib
#include "aiLib.h"

// Crossroads
#include "Entity.h"
#include "EntityIterator.h"
#include "gslMapState.h"
#include "mapstate_common.h"

// AutoGen
#include "gslPlayerFSM_h_ast.h"

struct {
	int contextVarHandle;
	int meVarHandle;
	int curStateTrackerVarHandle;

	const char* contextString;
	const char* meString;
	const char* curStateTrackerString;
	
	ExprFuncTable* funcTable;
	ExprContext *staticContext;

	PlayerFSM **playerFSMs;

	NameList* namelist;
} pfsmState;

#define PLAYER_FSM_TICK 0.5

ExprFuncTable* pfsmGetFuncTable(void)
{
	if(!pfsmState.funcTable)
	{
		pfsmState.funcTable = exprContextCreateFunctionTable();

		exprContextAddFuncsToTableByTag(pfsmState.funcTable, "layerFSM");
		exprContextAddFuncsToTableByTag(pfsmState.funcTable, "encounter_action");
		exprContextAddFuncsToTableByTag(pfsmState.funcTable, "Player");
		exprContextAddFuncsToTableByTag(pfsmState.funcTable, "util");
		exprContextAddFuncsToTableByTag(pfsmState.funcTable, "Entity");
		exprContextAddFuncsToTableByTag(pfsmState.funcTable, "entityutil");
	}

	return pfsmState.funcTable;
}

void pfsmSetupExprContext(ExprContext *exprContext, ExprFuncTable* funcTable, Entity* e)
{
	if(e)
	{
		exprContextSetPartition(exprContext, entGetPartitionIdx(e));
		exprContextSetSelfPtr(exprContext, e);
	}
	else
	{
		exprContextSetSelfPtr(exprContext, (Entity*)0xdeadbeef);
		exprContextClearPartition(exprContext);
	}

	exprContextSetPointerVarPooledCached(	exprContext, 
											pfsmState.contextString, 
											exprContext, 
											parse_ExprContext, 
											true, 
											true, 
											&pfsmState.contextVarHandle);

	exprContextSetPointerVarPooledCached(	exprContext, 
											pfsmState.meString, 
											e, 
											parse_Entity, 
											true, 
											true, 
											&pfsmState.meVarHandle);

	exprContextSetPointerVarPooledCached(	exprContext, 
											pfsmState.curStateTrackerString, 
											NULL, 
											parse_FSMStateTrackerEntry, 
											true, 
											true, 
											&pfsmState.curStateTrackerVarHandle);
	
	exprContextSetFuncTable(exprContext, funcTable);
}


ExprContext* pfsmGetStaticCheckContext(void)
{
	if(!pfsmState.staticContext)
	{
		pfsmState.staticContext = exprContextCreate();

		pfsmSetupExprContext(pfsmState.staticContext, pfsmGetFuncTable(), NULL);

		exprContextSetAllowRuntimeSelfPtr(pfsmState.staticContext);
		exprContextSetAllowRuntimePartition(pfsmState.staticContext);
	}

	return pfsmState.staticContext;
}

AUTO_RUN;
void pfsmRegisterPooledStrings(void)
{
	pfsmState.contextString			= allocAddStaticString("Context");
	pfsmState.meString				= allocAddStaticString("Me");
	pfsmState.curStateTrackerString	= allocAddStaticString("curStateTracker");
}

S32 pfsm_PlayerFSMExists(const char* pfsmName)
{
	const char** fsms = zmapInfoGetPlayerFSMs(NULL);

	FOR_EACH_IN_EARRAY(fsms, const char, fsm)
	{
		if(!stricmp(fsm, pfsmName))
			return true;
	}
	FOR_EACH_END

	return false;
}

PlayerFSM* pfsm_GetByName(Entity* e, const char* pfsmName)
{
	if(!e || !pfsmName || !pfsmName[0])
		return NULL;

	FOR_EACH_IN_EARRAY(pfsmState.playerFSMs, PlayerFSM, pfsm)
	{
		if(pfsm->ref==entGetRef(e))
		{
			FSM *fsm = GET_REF(pfsm->fsmContext->origFSM);

			if(!stricmp(fsm->name, pfsmName))
				return pfsm;
		}
	}
	FOR_EACH_END

	return NULL;
}

NameList* pfsmGetNameList(void)
{
	if(!pfsmState.namelist)
		pfsmState.namelist = CreateNameList_Bucket();

	return pfsmState.namelist;
}

void pfsm_PlayerEnterMap(Entity* e)
{
	const char** fsms;

	PERFINFO_AUTO_START_FUNC();
	
	fsms = zmapInfoGetPlayerFSMs(NULL);

	FOR_EACH_IN_EARRAY(fsms, const char, fsm)
	{
		FSMContext *fsmContext = NULL;
		ExprContext *exprContext = NULL;
		
		fsmContext = fsmContextCreateByName(fsm, NULL);
		exprContext = exprContextCreate();
		
		if(fsmContext && exprContext)
		{
			PlayerFSM *pfsm = StructAlloc(parse_PlayerFSM);

			pfsm->ref = entGetRef(e);
			pfsm->exprContext = exprContext;
			pfsm->fsmContext = fsmContext;
			pfsm->messages = stashTableCreateWithStringKeys(4, StashDefault);
			pfsm->fsmContext->messages = pfsm->messages;

			pfsmSetupExprContext(exprContext, pfsmGetFuncTable(), e);

			eaPush(&pfsmState.playerFSMs, pfsm);
		}
		else
		{
			if(fsmContext)
				fsmContextDestroy(fsmContext);
			if(exprContext)
				exprContextDestroy(exprContext);
		}
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}

void pfsmDestroy(PlayerFSM *pfsm)
{
	aiMessageDestroyAll(pfsm->fsmContext);
	fsmContextDestroy(pfsm->fsmContext);
	exprContextDestroy(pfsm->exprContext);
	StructDestroy(parse_PlayerFSM, pfsm);
}

void pfsm_PlayerLeaveMap(Entity* e)
{
	FOR_EACH_IN_EARRAY(pfsmState.playerFSMs, PlayerFSM, pfsm)
	{
		if(pfsm->ref==entGetRef(e))
		{
			pfsmDestroy(pfsm);
			eaRemoveFast(&pfsmState.playerFSMs, FOR_EACH_IDX(pfsmState.playerFSMs, pfsm));
		}
	}
	FOR_EACH_END
}

void pfsmMapLoad(ZoneMap* zmap)
{
	EntityIterator *iter;
	Entity *e;
	const char** fsms;

	iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_UNTARGETABLE|ENTITYFLAG_DONOTSEND, GLOBALTYPE_ENTITYPLAYER);
	while(e = EntityIteratorGetNext(iter))
	{
		pfsm_PlayerEnterMap(e);
	}
	EntityIteratorRelease(iter);

	fsms = zmapInfoGetPlayerFSMs(NULL);

	FOR_EACH_IN_EARRAY(fsms, const char, fsm)
	{
		NameList_Bucket_AddName(pfsmState.namelist, fsm);
	}
	FOR_EACH_END
}

void pfsmMapUnload(void)
{
	eaClearEx(&pfsmState.playerFSMs, pfsmDestroy);

	NameList_Bucket_Clear(pfsmState.namelist);
}

void pfsm_Tick(PlayerFSM *pfsm)
{
	fsmExecute(pfsm->fsmContext, pfsm->exprContext);
}

void pfsmOncePerFrame(void)
{
	FOR_EACH_IN_EARRAY(pfsmState.playerFSMs, PlayerFSM, pfsm)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pfsm->ref);
		if (pEnt && mapState_IsMapPausedForPartition(entGetPartitionIdx(pEnt))) 
		{
			continue;
		}

		if(ABS_TIME_PARTITION(entGetPartitionIdx(pEnt)) > pfsm->nextTick)
		{
			pfsm_Tick(pfsm);

			pfsm->nextTick = ABS_TIME_PARTITION(entGetPartitionIdx(pEnt)) + SEC_TO_ABS_TIME(PLAYER_FSM_TICK);
		}
	}
	FOR_EACH_END
}

AUTO_STARTUP(PlayerFSMStartup) ASTRT_DEPS(Powers, Critters, AS_Messages, AI, Cutscenes);
void pfsmStartup(void)
{
	fsmLoad("PlayerFSM", 
			"ai/PlayerFSMs", 
			"PlayerFSMs.bin", 
			pfsmGetStaticCheckContext(),
			pfsmGetFuncTable(),
			pfsmGetFuncTable(),
			pfsmGetFuncTable(),
			pfsmGetFuncTable(),
			pfsmGetFuncTable());

	worldLibRegisterMapLoadCallback(pfsmMapLoad);
	worldLibRegisterMapUnloadCallback(pfsmMapUnload);
}

#include "gslPlayerFSM_h_ast.c"