#include "Entity.h"
#include "EntityExtern.h"
#include "EntityIterator.h"

// TODO: remove these and move the test functions when cross-lib cmd sets are in
#include "aiLib.h"
#include "aiConfig.h"
#include "aiStruct.h"
#include "aiMovement.h"
#include "Character.h"
#include "Character_target.h"
#include "StateMachine.h"
#include "EntitySystemInternal.h"
#include "EntityMovementManager.h"
#include "EntityMovementFlight.h"
#include "EntityMovementPlatform.h"
#include "EntityMovementDoor.h"
#include "EntityMovementDefault.h"
#include "EntityMovementProjectile.h"
#include "EntityMovementDisableMovement.h"
#include "EntityMovementTactical.h"
#include "EntityMovementInteraction.h"
#include "EntityMovementGrab.h"
#include "EntityMovementTest.h"
#include "EntityMovementEmote.h"
#include "autogen/EntityMovementGrab_h_ast.h"
// END todo

#include "cmdparse.h"
#include "Expression.h"
#include "earray.h"
#include "error.h"
#include "GameAccountDataCommon.h"
#include "gslCommandParse.h"
#include "gslExtern.h"
#include "gslSendToClient.h"
#include "StashTable.h"
#include "Beacon.h"
#include "GameServerLib.h"
#include "BeaconDebug.h"
#include "Character.h"
#include "quat.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "entCritter.h"
#include "oldencounter_common.h"
#include "gslOldEncounter.h"
#include "EntityGrid.h"
#include "EntityLib.h"
#include "Player.h"
#include "StringCache.h"
#include "dynRagdollData.h"
#include "wlSkelInfo.h"
#include "CostumeCommonTailor.h"
#include "dynSkeleton.h"
#include "dynNode.h"
#include "dynNodeInline.h"
#include "CombatConfig.h"

#include "TransactionOutcomes.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

#include "../Common/AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "autogen/EntityMovementProjectile_h_ast.h"
//#include "../WorldLib/AutoGen/beaconDebug_h_ast.h"

EntityRef* entConSet = NULL;

AUTO_COMMAND ACMD_NAME(ecCreateSet) ACMD_ACCESSLEVEL(9) ACMD_SERVERONLY;
void entConCreateSet(Entity* e, ACMD_SENTENCE expression)
{
	Entity* target;
	EntityIterator* entIter;
	Expression* expr = exprCreate();
	static ExprContext* context = NULL;
	MultiVal answer = {0};
	int iPartitionIdx = entGetPartitionIdx(e);
		
	if(!context)
	{
		context = exprContextCreate();
		exprContextSetPointerVar(context, "me", e, parse_Entity, false, true);
		exprContextSetPointerVar(context, "targetEnt", NULL, parse_Entity, false, true);
	}

	exprGenerateFromString(expr, context, expression, NULL);
	eaiSetSize(&entConSet, 0);

	entIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);

	while(target = EntityIteratorGetNext(entIter))
	{
		exprContextSetPointerVar(context, "targetEnt", target, parse_Entity, false, true);
		exprEvaluate(expr, context, &answer);
		if(answer.type!=MULTI_INVALID && answer.intval)
			eaiPush(&entConSet, entGetRef(target));
	}

	EntityIteratorRelease(entIter);
	exprDestroy(expr);
}

AUTO_COMMAND ACMD_NAME(ecCreateSetFriendlies) ACMD_ACCESSLEVEL(9) ACMD_SERVERONLY;
void entConCreateSetFriendlies(Entity* e)
{
	Entity* target;
	EntityIterator* entIter;
	int iPartitionIdx = entGetPartitionIdx(e);

	eaiSetSize(&entConSet, 0);

	entIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);

	while(target = EntityIteratorGetNext(entIter))
	{
		if(!critter_IsKOS(iPartitionIdx, target, e))
			eaiPush(&entConSet, entGetRef(target));
	}

	EntityIteratorRelease(entIter);
}

CmdList gEntConCmdList;

// TODO: figure out a nicer way to pass this to the entcon functions... too lazy now
// this should be in some kind of CmdEntConContext I guess
static Entity* staticEnt;

NameList *pAIEntConTargetBucket;
AUTO_RUN;
void entConInitTargetBucket(void)
{
	pAIEntConTargetBucket = CreateNameList_Bucket();
	NameList_Bucket_AddName(pAIEntConTargetBucket, "me");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "selected");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "selected2");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "-1");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "all");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "notselected");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "set");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "player");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "critter");
	NameList_Bucket_AddName(pAIEntConTargetBucket, "mypets");
}

#define EXECUTE_COMMAND(ent) \
{ \
	svrcontext.clientEntity = ent; \
	entSetActive(ent); \
	cmdParseAndExecute(&gEntConCmdList, ecCmdData, &context); \
}

static int parseNumberFromString(const char* s){
	int n = 0;
	
	if(!strStartsWith(s, "0x")){
		n = atoi(s);
	}else{
		const char* cur = s + 2;
		n = 0;
		while(cur[0]){
			char c = tolower(cur[0]);
			n <<= 4;
			if(c >= '0' && c <= '9'){
				n += c - '0';
			}
			else if(c >= 'a' && c <= 'f'){
				n += 10 + c - 'a';
			}else{
				n = 0;
				break;
			}
			cur++;
		}
	}

	return n;
}

AUTO_COMMAND ACMD_NAME(ec) ACMD_ACCESSLEVEL(7) ACMD_SERVERONLY;
void entCon(Entity* client, ACMD_NAMELIST(pAIEntConTargetBucket) char* target, ACMD_NAMELIST(gEntConCmdList, COMMANDLIST) ACMD_SENTENCE ecCmdData)
{
	EntityIterator* entIter;
	Entity* targetEnt;
	CmdContext context = {0};
	CmdServerContext svrcontext = {0};
	int targetInt;
	int targetNot = target[0]=='n' && target[1]=='o' && target[2]=='t';
	int targetNotInt = targetNot ? atoi(target+3) : 0;
	char* msg = NULL;
	int iPartitionIdx = entGetPartitionIdx(client);
	
	if(!client){
		return;
	}
	
	targetInt = parseNumberFromString(target);

	InitCmdOutput(context, msg);

	context.access_level = ACCESS_DEBUG;

	if (!client){
		return;
	}

	svrcontext.sourceStr = ecCmdData;
	svrcontext.clientEntity = client;

	context.data = &svrcontext;
	context.clientID = client->myContainerID;
	context.clientType = client->myEntityType;

	if(!cmdCheckSyntax(&gEntConCmdList, ecCmdData, &context))
	{
		//conPrintf(client->pPlayer->clientLink, "Command \"%s\" is not valid", ecCmdData);
		if(msg[0])
			gslSendPrintf(client, "%s", msg);
		else
			gslSendPrintf(client, "Incorrect entcon command");
		//Errorf("Command \"%s\" is not valid", ecCmdData);
		CleanupCmdOutput(context);
		return;
	}

	staticEnt = client;

	if(!stricmp(target, "me"))
		EXECUTE_COMMAND(client)
	else if(!stricmp(target, "player"))
	{
		entIter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
		while(targetEnt = EntityIteratorGetNext(entIter))
			EXECUTE_COMMAND(targetEnt);
		EntityIteratorRelease(entIter);
	}
	else if(!stricmp(target, "critter"))
	{
		entIter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYCRITTER);
		while(targetEnt = EntityIteratorGetNext(entIter))
			EXECUTE_COMMAND(targetEnt);
		EntityIteratorRelease(entIter);
	}
	else if(!stricmp(target, "set"))
	{
		int i;
		for(i = eaiSize(&entConSet)-1; i >= 0; i--)
		{
			targetEnt = entFromEntityRef(iPartitionIdx, entConSet[i]);
			if(targetEnt)
				EXECUTE_COMMAND(targetEnt);
		}
	}
	else if(targetInt == -1 ||
			!stricmp(target, "all"))
	{
		entIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);
		while(targetEnt = EntityIteratorGetNext(entIter))
			EXECUTE_COMMAND(targetEnt);
		EntityIteratorRelease(entIter);
	}
	else if(targetInt > 0)
	{
		targetEnt = entFromEntityRef(iPartitionIdx, targetInt);
		
		if(	!targetEnt &&
			targetInt >= 0 && targetInt < MAX_ENTITIES_PRIVATE)
		{
			targetEnt = ENTITY_FROM_INDEX(targetInt);
		}

		if(targetEnt){
			EXECUTE_COMMAND(targetEnt);
		}
	}
	else if(!stricmp(target, "selected"))
	{
		targetEnt = entFromEntityRef(iPartitionIdx, client->pChar->currentTargetRef ? client->pChar->currentTargetRef : client->pChar->erTargetDual);
		if(targetEnt)
			EXECUTE_COMMAND(targetEnt);
	}
	else if(!stricmp(target, "selected2"))
	{
		targetEnt = entFromEntityRef(iPartitionIdx, client->pChar->erTargetDual);
		if(targetEnt)
			EXECUTE_COMMAND(targetEnt);
	}
	else if(!stricmp(target, "focus"))
	{
		targetEnt = entFromEntityRef(iPartitionIdx, client->pChar->erTargetFocus);
		if(targetEnt)
			EXECUTE_COMMAND(targetEnt);
	}
	else if(!stricmp(target, "notselected"))
	{
		Entity* selectedEnt = entFromEntityRef(iPartitionIdx, client->pChar->currentTargetRef);
		Entity* selectedEntDual = entFromEntityRef(iPartitionIdx, client->pChar->erTargetDual);
		entIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);
		while(targetEnt = EntityIteratorGetNext(entIter))
		{
			if(targetEnt==selectedEnt || targetEnt==selectedEntDual)
			{
				continue;
			}
			EXECUTE_COMMAND(targetEnt);
		}
		EntityIteratorRelease(entIter);
	}
	else if(!stricmp(target, "mypets"))
	{
		EntityRef myRef = entGetRef(client);
		entIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);
		while(targetEnt = EntityIteratorGetNext(entIter))
		{
			if(targetEnt->erOwner == myRef)
				EXECUTE_COMMAND(targetEnt);
		}
		EntityIteratorRelease(entIter);
	}
	else if(targetNot && targetNotInt > 0) 
	{
		Entity* notEnt = entFromEntityRef(iPartitionIdx, targetNotInt);
		entIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);
		while(targetEnt = EntityIteratorGetNext(entIter))
		{
			if(targetEnt==notEnt)
			{
				continue;
			}
			EXECUTE_COMMAND(targetEnt);
		}
		EntityIteratorRelease(entIter);
	}
	else
		gslSendPrintf(client, "Unrecognized target type %s", target);

	CleanupCmdOutput(context);
}

//void entConPrintName(Entity* e, char* ecCmdData)
//{
	//printf("%s\n", e->debugName);
//}

//AUTO_RUN;
//int entConInit(void)
//{
	//// add entcon function in this file here
	//entConRegisterCmd("printname", entConPrintName);
	//return 1;
//}

AUTO_COMMAND ACMD_NAME(printname) ACMD_LIST(gEntConCmdList);
void entConPrintName(CmdContext *context, Entity* e)
{
	Entity *caller;
	Vec3 pos;
	char msg[512];
	entGetPos(e, pos);

	sprintf(msg, "%d %s (%.3f, %.3f, %.3f)\n", entGetRef(e), ENTDEBUGNAME(e), pos[0], pos[1], pos[2]);
	printf("%s", msg);
	caller = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if(caller)
		ClientCmd_clientConPrint(caller, msg);
}

// hopefully at some point this can just be an extra reference on the other TestAIExpr
//AUTO_COMMAND ACMD_NAME(ecTestAIExpr) ACMD_SET(entcon);
//void TestAIExpr2(Entity *clientEntity, ACMD_SENTENCE testStr)
//{
	//MultiVal answer;
	//Expression* expr = exprCreate();
	//ExprContext* context = exprContextCreate();
//
	//aiSetupExprContext(clientEntity, context);
	//exprGenerate(expr, context);
	//exprEvaluate(expr, context, &answer);
	//exprDestroy(expr);
	//exprContextDestroy(context);
//}

static Entity* entGetServerTarget(	Entity* be,
									const char* target,
									EntityRef* erTargetOut)
{
	EntityRef erTarget = 0;

	if(target){
		if(!stricmp(target, "selected")){
			erTarget = FIRST_IF_SET(be->pChar->currentTargetRef, be->pChar->erTargetDual);
		}
		else if(!stricmp(target, "selected2")){
			erTarget = be->pChar->erTargetDual;
		}
		else if(!stricmp(target, "me")){
			erTarget = entGetRef(be);
		}else{
			erTarget = parseNumberFromString(target);
		}
	}

	if(erTargetOut){
		*erTargetOut = erTarget;
	}

	return entFromEntityRef(entGetPartitionIdx(be), erTarget);
}

AUTO_COMMAND ACMD_NAME(MoveToMe) ACMD_LIST(gEntConCmdList);
void entConMoveToMe(Entity* e)
{
	aiMovementSetTargetEntity(e, e->aibase, staticEnt, NULL, 0, AI_MOVEMENT_ORDER_ENT_UNSPECIFIED, 
		AI_MOVEMENT_TARGET_CRITICAL | AI_MOVEMENT_TARGET_DONT_SHORTCUT);
}

AUTO_COMMAND ACMD_NAME(MoveToPos) ACMD_LIST(gEntConCmdList);
void entConMoveToPos(Entity* e, Vec3 pos)
{
	aiMovementSetTargetPosition(e, e->aibase, pos, NULL, AI_MOVEMENT_TARGET_CRITICAL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(MovePlayerToPos) ACMD_ACCESSLEVEL(9) ACMD_LIST(gEntConCmdList);
void entConMovePlayerToPos(Entity* e, Vec3 pos)
{
	aiMovementSetTargetPosition(e, e->aibase, pos, NULL, AI_MOVEMENT_TARGET_CRITICAL);
}

AUTO_COMMAND ACMD_NAME(setAttackTarget) ACMD_ACCESSLEVEL(9) ACMD_LIST(gEntConCmdList);
void entConSetAttackTarget(Entity* e, const char* target)
{
	Entity *eTarget = entGetServerTarget(staticEnt, target, NULL);
	AIConfig *cfg = aiGetConfig(e, e->aibase);

	if(e && eTarget && e->pCritter)
	{
		cfg->dontChangeAttackTarget = 1;
		aiSetAttackTarget(e, e->aibase, eTarget, NULL, true);
	}
}

AUTO_COMMAND ACMD_NAME(SetPreferredTarget) ACMD_LIST(gEntConCmdList);
void entConSetPreferredTarget(Entity *e, const char* target)
{
	Entity *targetE = entGetServerTarget(staticEnt, target, NULL);

	if(e && target)
		e->aibase->preferredTargetRef = entGetRef(targetE);
}

AUTO_COMMAND ACMD_NAME(moveRelativeToEnt) ACMD_LIST(gEntConCmdList);
void entCon_moveRelativeToEnt(Entity* e, const char* target, const Vec3 vecOffset)
{
	Entity* eTarget = entGetServerTarget(staticEnt, target, NULL);
	
	if(	e &&
		eTarget)
	{
		Entity *eMount;
		Vec3 pos;
		
		ANALYSIS_ASSUME(eTarget != NULL);
		entGetPos(eTarget, pos);

		if(!mmTranslatePosInSpaceFG(e->mm.movement, pos, vecOffset, pos, NULL)){
			return;
		}

		if(eMount = entGetMount(e)){
			entSetPos(eMount, pos, 1, __FUNCTION__);
		}
		entSetPos(e, pos, 1, __FUNCTION__);
	}
}

AUTO_COMMAND ACMD_NAME(setPosRelativeToEnt) ACMD_LIST(gEntConCmdList);
void entCon_setPosRelativeToEnt(Entity* e, const char* target, const Vec3 vecOffset)
{
	Entity* eTarget = entGetServerTarget(staticEnt, target, NULL);
	
	if(	e &&
		eTarget)
	{
		Entity *eMount;
		Vec3 pos;
		
		ANALYSIS_ASSUME(eTarget != NULL);
		entGetPos(eTarget, pos);

		if(vecOffset){
			addVec3(pos, vecOffset, pos);
		}
		
		if(eMount = entGetMount(e)){
			entSetPos(eMount, pos, 1, __FUNCTION__);
		}
		entSetPos(e, pos, 1, __FUNCTION__);
	}
}

AUTO_COMMAND ACMD_NAME(setPosAtEnt) ACMD_LIST(gEntConCmdList);
void entCon_setPosAtEnt(Entity* e, const char* target)
{
	entCon_setPosRelativeToEnt(e, target, NULL);
}

AUTO_COMMAND ACMD_NAME(mmSendMeLogs) ACMD_LIST(gEntConCmdList);
void entCon_mmSendMeLogs(Entity* e){
	if(e){
		mmLogSend(	SAFE_MEMBER3(staticEnt, pPlayer, clientLink, movementClient),
					entGetRef(e));
	}
}

AUTO_COMMAND ACMD_NAME(mmSendMeLogsByEntRef) ACMD_LIST(gEntConCmdList);
void entCon_mmSendMeLogsByEntRef(EntityRef er){
	mmLogSend(	SAFE_MEMBER3(staticEnt, pPlayer, clientLink, movementClient),
				er);
}

AUTO_COMMAND ACMD_NAME(AssignBScript) ACMD_LIST(gEntConCmdList);
void aiAssignBScript(Entity* e, ACMD_SENTENCE bscriptName)
{
	aiSetFSMByName(e, bscriptName);
}

AUTO_COMMAND ACMD_NAME(DebugTransition) ACMD_LIST(gEntConCmdList);
void entCon_AIDebugTransition(Entity *e, int level, int transition, int combatfsm)
{
	if(combatfsm)
	{
		if(e->aibase->combatFSMContext)
		{
			e->aibase->combatFSMContext->debugTransitionLevel = level;
			e->aibase->combatFSMContext->debugTransition = transition;
		}
	}
	else if(e->aibase->fsmContext)
	{
		FSMContext *pFSMContext = aiGetCurrentBaseFSMContext(e);
		if (pFSMContext)
		{
			pFSMContext->debugTransitionLevel = level;
			pFSMContext->debugTransition = transition;
		}
	}
}

AUTO_COMMAND ACMD_NAME(mmDebug) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetDebugging(Entity* e, S32 enabled)
{
	mmSetDebugging(SAFE_MEMBER(e, mm.movement), enabled);
}

AUTO_COMMAND ACMD_NAME(mmLogPrint) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdLogPrint(Entity* e, ACMD_SENTENCE text)
{
	mmLog(SAFE_MEMBER(e, mm.movement), NULL, "%s", text);
}

AUTO_COMMAND ACMD_NAME(mmLogWriteFiles) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdLogWriteFiles(Entity* e, S32 enabled)
{
	mmSetWriteLogFiles(SAFE_MEMBER(e, mm.movement), enabled);
}

AUTO_COMMAND ACMD_NAME(mmAddRequester) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdAddRequester(Entity* e, const char* name)
{
	mmRequesterCreateBasicByName(SAFE_MEMBER(e, mm.movement), NULL, name);
}

AUTO_COMMAND ACMD_NAME(mmDestroyRequester) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdDestroyRequester(Entity* e, const char* name)
{
	Entity* 		be = e;
	MovementRequester*	mr;
	
	if(mmRequesterGetByNameFG(SAFE_MEMBER(be, mm.movement), name, &mr)){
		mrDestroy(&mr);
	}
}

AUTO_COMMAND ACMD_NAME(mmFly) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdFly(Entity* e, S32 enabled)
{
	gslEntMovementCreateFlightRequester(e);

	mrFlightSetEnabled(	e->mm.mrFlight,
						enabled);

	mrFlightSetMaxSpeed(e->mm.mrFlight,
						50.f);
						
	mrFlightSetTraction(e->mm.mrFlight,
						1.f);

	mrFlightSetFriction(e->mm.mrFlight,
						0.5f);
}

AUTO_COMMAND ACMD_NAME(mmTestThing) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestThing(Entity* e, U32 timeToStart){
	Entity*			be = e;
	MovementRequester*	mr;
	
	if(mmRequesterGetByNameFG(be->mm.movement, "TestMovement", &mr)){
		mmTestSetDoTest(mr, timeToStart);
	}
}

AUTO_COMMAND ACMD_NAME(mmAttachToClient) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdAttachToClient(Entity* e, ACMD_SENTENCE target)
{
	Entity*			eClient = entGetServerTarget(staticEnt, target, NULL);
	MovementClient*	mc = SAFE_MEMBER3(eClient, pPlayer, clientLink, movementClient);
	
	mmAttachToClient(e->mm.movement, mc);
}

AUTO_COMMAND ACMD_NAME(mmDetachFromClient) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdDetachFromClient(Entity* e)
{
	mmDetachFromClient(e->mm.movement, NULL);
}

AUTO_COMMAND ACMD_NAME(mmSetJumpHeight) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetJumpHeight(Entity* e, F32 value)
{
	mrSurfaceSetJumpHeight(e->mm.mrSurface, value);
}

AUTO_COMMAND ACMD_NAME(mmSetJumpTraction) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetJumpTraction(Entity* e, F32 value)
{
	mrSurfaceSetJumpTraction(e->mm.mrSurface, value);
}

typedef struct DoorDebugStruct{
	Entity *e;
	Vec3 pos;
} DoorDebugStruct;

void mrDoorTestCB(DoorDebugStruct *data)
{
	entSetPos(data->e, data->pos, 1, __FUNCTION__);

	free(data);
}

AUTO_COMMAND ACMD_NAME(mmTestDoor) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestDoor(Entity* e)
{
	MovementRequester *mr;
	Vec3 pos;
	DoorDebugStruct *data = NULL;

	entGetPos(e, pos);

	mmRequesterCreateBasicByName(e->mm.movement, &mr, "DoorMovement");

	pos[0] += 10;

	data = callocStruct(DoorDebugStruct);
	setVec3same(data->pos, 50);
	data->e = e;

	mrDoorStart(mr, mrDoorTestCB, data, 1);
}

AUTO_COMMAND ACMD_NAME("mmTestPredictedKnockback") ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestPredictedKnockback(Entity* e,
										const Vec3 vel,
										U32 startProcessCount,
										S32 instantFacePlant,
										S32 proneAtEnd,
										F32 timer,
										S32 ignoreTravelTime)
{
	MovementRequester *mr;

	mmRequesterCreateBasicByName(e->mm.movement, &mr, "ProjectileMovement");

	mrProjectileStartWithVelocity(	mr,
									e, 
									vel,
									startProcessCount,
									instantFacePlant,
									proneAtEnd,
									timer,
									ignoreTravelTime);
}

AUTO_COMMAND ACMD_NAME("mmTestPredictedKnockbackWithTarget") ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestPredictedKnockbackWithTarget(	Entity* e,
													const Vec3 target,
													U32 startProcessCount,
													S32 instantFacePlant,
													S32 proneAtEnd,
													F32 timer,
													S32 ignoreTravelTime)
{
	MovementRequester *mr;

	mmRequesterCreateBasicByName(e->mm.movement, &mr, "ProjectileMovement");

	mrProjectileStartWithTarget(mr,
								e,
								target,
								startProcessCount,
								false,
								mrFlightGetEnabled(e->mm.mrFlight),
								instantFacePlant,
								proneAtEnd,
								timer,
								ignoreTravelTime);
}

AUTO_COMMAND ACMD_NAME(mmTestProjectile) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestProjectile(Entity* e,
								const Vec3 vel,
								F32 timer,
								S32 ignoreTravelTime)
{
	MovementRequester *mr;

	mmRequesterCreateBasicByName(e->mm.movement, &mr, "ProjectileMovement");

	mrProjectileStartWithVelocity(mr, e, vel, 0, false, true, timer, ignoreTravelTime);
}

AUTO_COMMAND ACMD_NAME(mmTestProjectileWithTarget) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestProjectileWithTarget(Entity* e, const Vec3 target, F32 timer, bool ignoreTravelTime)
{
	MovementRequester *mr;

	mmRequesterCreateBasicByName(e->mm.movement, &mr, "ProjectileMovement");

	mrProjectileStartWithTarget(mr, e, target, 0, false, mrFlightGetEnabled(e->mm.mrFlight), false, true, timer, ignoreTravelTime);
}

AUTO_COMMAND ACMD_NAME(mmSetSpeed) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetSpeed(Entity* e, F32 value)
{
	MovementRequester* mr;
	
	if(mmRequesterGetByNameFG(e->mm.movement, "PlatformMovement", &mr)){
		mmPlatformMovementSetSpeed(mr, value);
	}
	mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_FAST, value);
	mrFlightSetMaxSpeed(e->mm.mrFlight, value);
}

AUTO_COMMAND ACMD_NAME(mmSetSpeedRange) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetSpeedRange(Entity* e, F32 loDirScale, F32 hiDirScale)
{
	mrSurfaceSetSpeedRange(e->mm.mrSurface, MR_SURFACE_SPEED_FAST, loDirScale, hiDirScale);
}

AUTO_COMMAND ACMD_NAME(mmSetMediumSpeed) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetMediumSpeed(Entity* e, F32 value)
{
	mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_MEDIUM, value);
}

AUTO_COMMAND ACMD_NAME(mmSetMediumSpeedRange) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetMediumSpeedRange(Entity* e, F32 loDirScale, F32 hiDirScale)
{
	mrSurfaceSetSpeedRange(e->mm.mrSurface, MR_SURFACE_SPEED_MEDIUM, loDirScale, hiDirScale);
}

AUTO_COMMAND ACMD_NAME(mmSetSlowSpeed) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetSlowSpeed(Entity* e, F32 value)
{
	mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_SLOW, value);
}

AUTO_COMMAND ACMD_NAME(mmSetSlowSpeedRange) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetSlowSpeedRange(Entity* e, F32 loDirScale, F32 hiDirScale)
{
	mrSurfaceSetSpeedRange(e->mm.mrSurface, MR_SURFACE_SPEED_SLOW, loDirScale, hiDirScale);
}

AUTO_COMMAND ACMD_NAME(mmSetBackScale) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetBackScale(Entity* e, F32 value)
{
	mrSurfaceSetBackScale(e->mm.mrSurface, value);
}

AUTO_COMMAND ACMD_NAME(mmSetFriction) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetFriction(Entity* e, F32 value)
{
	mrSurfaceSetFriction(e->mm.mrSurface, value);
	mrFlightSetFriction(e->mm.mrFlight, value);
}

AUTO_COMMAND ACMD_NAME(mmSetTraction) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetTraction(Entity* e, F32 value)
{
	mrSurfaceSetTraction(e->mm.mrSurface, value);
	mrFlightSetTraction(e->mm.mrFlight, value);
}

AUTO_COMMAND ACMD_NAME(mmSetTurnRate) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entConn_mmSetTurnRate(Entity *e, F32 value)
{
	mrFlightSetTurnRate(e->mm.mrFlight, value);
}

AUTO_COMMAND ACMD_NAME(mmSetGravity) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetGravity(Entity* e, F32 value)
{
	mrSurfaceSetGravity(e->mm.mrSurface, value);
}

AUTO_COMMAND ACMD_NAME(mmSetJumpGravity) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetJumpGravity(Entity* e, F32 jumpUpGravity, F32 jumpDownGravity)
{
	mrSurfaceSetJumpGravity(e->mm.mrSurface, jumpUpGravity, jumpDownGravity);
}

AUTO_COMMAND ACMD_NAME(mmSetCanStick) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetCanStick(Entity* e, S32 enabled)
{
	mrSurfaceSetCanStick(e->mm.mrSurface, enabled);
}

AUTO_COMMAND ACMD_NAME(mmSetOrientToSurface) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetOrientToSurface(Entity* e, S32 enabled)
{
	mrSurfaceSetOrientToSurface(e->mm.mrSurface, enabled);
}

AUTO_COMMAND ACMD_NAME(mmSetVelocity) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetVelocity(Entity* e, const Vec3 vel)
{
	mrSurfaceSetVelocity(e->mm.mrSurface, vel);
}

AUTO_COMMAND ACMD_NAME(mmSetDoJumpTest) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdSetDoJumpTest(Entity* e, const Vec3 target)
{
	mrSurfaceSetDoJumpTest(e->mm.mrSurface, target);
}

AUTO_COMMAND ACMD_NAME(mmSetIsStrafing) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetIsStrafing(Entity* e, S32 enabled)
{
	gslEntitySetIsStrafing(e, enabled);
}

AUTO_COMMAND ACMD_NAME(mmSetUseThrottle) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetUseThrottle(Entity* e, S32 enabled)
{
	gslEntitySetUseThrottle(e, enabled);
}

AUTO_COMMAND ACMD_NAME(mmSetUseOffsetRotation) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdSetUseOffsetRotation(Entity* e, S32 enabled)
{
	gslEntitySetUseOffsetRotation(e, enabled);
}

AUTO_COMMAND ACMD_NAME(mmTestDoorGeo) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestDoorGeo(	Entity* e,
								Vec3 pos,
								Vec3 pyr,
								F32 timeTotal)
{
	Quat rot;
	
	PYRToQuat(pyr, rot);

	if(!e->mm.mrDoorGeo){
		mmRequesterCreateBasicByName(e->mm.movement, &e->mm.mrDoorGeo, "DoorGeoMovement");
	}

	mrDoorGeoSetTarget(	e->mm.mrDoorGeo,
						pos,
						rot,
						timeTotal);
}

AUTO_COMMAND ACMD_NAME(mmTestDoorGeoDestroy) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestDoorGeoDestroy(Entity* e){
	mrDestroy(&e->mm.mrDoorGeo);
}

AUTO_COMMAND ACMD_NAME(mmTestDisableMovement) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestDisableMovement(Entity* e){
	if(!e->mm.mrDisabled){
		mrDisableCreate(e->mm.movement, &e->mm.mrDisabled);
	}
}

AUTO_COMMAND ACMD_NAME(mmTestDisableMovementDestroy) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entCon_mmCmdTestDisableMovementDestroy(Entity* e){
	mrDestroy(&e->mm.mrDisabled);
}

AUTO_COMMAND ACMD_NAME(mmSetDoCollisionTest) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdSetDoCollisionTest(Entity* e,
									S32 collisionTestFlags,
									const Vec3 loOffset,
									const Vec3 hiOffset,
									F32 radius)
{
	mrSurfaceSetDoCollisionTest(e->mm.mrSurface, collisionTestFlags, loOffset, hiOffset, radius);
}

static void getRandomBody(MovementBody** bodyOut){
	MovementGeometry*			geo;
	MovementGeometryDesc		geoDesc = {0};
	Vec3						verts[8];
	IVec3						tris[12];
	F32							scale = 1.f + qufrand() * 9.f;
	
	FOR_BEGIN(i, 8);
	{
		verts[i][0] = scale * (i & 1 ? 1 : -1);
		verts[i][1] = scale * (i & 2 ? 1 : -1);
		verts[i][2] = scale * (i & 4 ? 1 : -1);
	}
	FOR_END;
	
	FOR_BEGIN(i, 3);
	{
		FOR_BEGIN(j, 2);
		{
			U32 a;
			U32 b;
			U32 c;
			
			// Get the major bit.
			
			a = 1 << i;
			
			// Get the secondary bit.

			b = a == 4 ? 1 : (a << 1);
			
			// Get the tertiary bit.
			
			c = 7 & ~(a | b);
			
			// Turn on or off the major bit.
			
			a = j ? a : 0;

			FOR_BEGIN(k, 2);
			{
				U32 vAxis = a | (k ? b : c);
				U32 vDiagonal = a | b | c;

				tris[i * 4 + j * 2 + k][0] = a;
				tris[i * 4 + j * 2 + k][1] = (!!a ^ !!k) ? vDiagonal : vAxis;
				tris[i * 4 + j * 2 + k][2] = (!!a ^ !!k) ? vAxis : vDiagonal;
			}
			FOR_END;
		}
		FOR_END;
	}
	FOR_END;
	
	geoDesc.geoType = MM_GEO_MESH;

	geoDesc.mesh.vertCount = ARRAY_SIZE(verts);
	geoDesc.mesh.verts = verts[0];
	
	geoDesc.mesh.triCount = ARRAY_SIZE(tris);
	geoDesc.mesh.tris = tris[0];
	
	if(mmGeometryCreate(&geo, &geoDesc)){
		Vec3				pyr = {qfrand() * PI / 2, qfrand() * PI, qfrand() * PI};
		MovementBodyDesc*	bd;

		mmBodyDescCreate(&bd);
		mmBodyDescAddGeometry(bd, geo, zerovec3, pyr);
		
		mmBodyCreate(bodyOut, &bd);
	}		
}

static void getCapsuleBodyIndex(	U32* bodyIndexOut,
									F32 radius,
									F32 length,
									const Vec3 dir,
									const Vec3 pos)
{
	MovementBodyDesc*	bd;
	MovementBody*		b;
	Capsule				c;
	
	copyVec3(pos, c.vStart);
	normalizeCopyVec3(dir, c.vDir);
	c.fLength = length;
	c.fRadius = radius;

	mmBodyDescCreate(&bd);
	mmBodyDescAddCapsule(bd, &c);
	
	mmBodyCreate(&b, &bd);
	mmBodyGetIndex(b, bodyIndexOut);
}

AUTO_COMMAND ACMD_NAME(mmAddBody) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdAddBody(Entity* e)
{
	MovementBody*		b;
	U32					handle;
	
	getRandomBody(&b);
	
	if(mmrBodyCreateFG(e->mm.movement, &handle, b, 0, 0)){
		eaiPush(&e->mm.debugBodyHandles, handle);
	}
}

AUTO_COMMAND ACMD_NAME(mmDestroyBodies) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdDestroyBodies(Entity* e){
	while(eaiSize(&e->mm.debugBodyHandles)){
		U32 handle = eaiPop(&e->mm.debugBodyHandles);
		
		mmResourceDestroyFG(e->mm.movement, &handle);
	}
	
	eaiDestroy(&e->mm.debugBodyHandles);
}

AUTO_COMMAND ACMD_NAME(mmSimBodyTest) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdSimBodyTest(Entity* e)
{
	MovementBody*		b;
	U32					bIndex;
	MovementRequester*	mr;

	getRandomBody(&b);
	mmBodyGetIndex(b, &bIndex);

	if(mmRequesterCreateBasicByName(e->mm.movement, &mr, "SimBodyMovement")){
		mrSimBodySetEnabled(mr, bIndex, 1);
	}
}


AUTO_COMMAND ACMD_NAME(mmRagDollTest) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdRagDollTest(Entity* e)
{
	U32 uiTime = 0;
	MovementRequester* mr;
	if (mmRequesterCreateBasicByName(e->mm.movement,&mr,"RagdollMovement"))
	{
		if (mrRagdollSetup(mr, e, uiTime))
		{
		}
	}
}

AUTO_COMMAND ACMD_NAME(mmSetTacticalRunRoll) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdSetTacticalRunRoll(Entity* e,
									F32 rollSpeed,
									F32 rollDistance,
									F32 rollPostHoldSeconds,
									F32 rollCooldown,
									F32 runSpeed,
									F32 maxRunDurationSeconds,
									F32 runCooldownSeconds,
									F32 globalCooldown,
									F32 rollFuelCost,
									bool bAutoRun)
{
	TacticalRequesterRollDef	rollDef = {0};

	gslEntMovementCreateTacticalRequester(e);

	mrTacticalSetGlobalCooldown(e->mm.mrTactical, 
								globalCooldown);

	
	rollDef.fRollCooldown = rollCooldown;
	rollDef.fRollFuelCost = rollFuelCost;
	rollDef.fRollDistance = rollDistance;
	rollDef.fRollPostHoldSeconds = rollPostHoldSeconds;
	rollDef.fRollSpeed = rollSpeed;


	mrTacticalSetRollParams(e->mm.mrTactical, &rollDef, g_CombatConfig.tactical.roll.bRollIgnoresGlobalCooldown, false);

	mrTacticalSetRunParams(	e->mm.mrTactical,
							true,
							runSpeed, 
							maxRunDurationSeconds, 
							runCooldownSeconds,
							bAutoRun,
							g_CombatConfig.tactical.sprint.bSprintToggles);
}

AUTO_COMMAND ACMD_NAME(mmSetTacticalRunFuel) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdSetTacticalRunFuel(Entity* e,
									S32 enabled,
									F32 refillRate,
									F32 refillDelay)
{
	gslEntMovementCreateTacticalRequester(e);
	
	mrTacticalSetRunFuel(	e->mm.mrTactical,
							enabled,
							refillRate,
							refillDelay);
}

AUTO_COMMAND ACMD_NAME(mmSetTacticalAim) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmCmdSetTacticalAim(Entity* e,
								F32 aimSpeed,
								F32 minAimDurationSeconds,
								F32 aimCooldown,
								F32 globalCooldown,
								S32 runPlusAimDoesRoll,
								S32 rollWhileAiming)
{
	TacticalRequesterAimDef aimDef = { 0 };

	aimDef.fAimMinDurationSeconds = minAimDurationSeconds;
	aimDef.fAimCooldown = aimCooldown;

	gslEntMovementCreateTacticalRequester(e);

	mrTacticalSetGlobalCooldown(e->mm.mrTactical, 
		globalCooldown);

	mrTacticalSetAimParams(	e->mm.mrTactical,
							&aimDef,
							aimSpeed,
							runPlusAimDoesRoll,
							rollWhileAiming,
							false,
							false, 
							false);
}

AUTO_COMMAND ACMD_NAME(mmSetCollGroup) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmSetCollGroup(Entity* e, U32 bit)
{
	if(e->mm.mcgHandleDbg)
		mmCollisionGroupHandleDestroyFG(&e->mm.mcgHandleDbg);

	if(bit)
		mmCollisionGroupHandleCreateFG(e->mm.movement, &e->mm.mcgHandleDbg, __FILE__, __LINE__, bit);
}

AUTO_COMMAND ACMD_NAME(mmSetCollGroupBits) ACMD_LIST(gEntConCmdList) ACMD_SERVERONLY ACMD_NOTESTCLIENT;
void entCon_mmSetCollGroupBits(Entity* e, U32 bits)
{
	if(e->mm.mcbHandleDbg)
		mmCollisionBitsHandleDestroyFG(&e->mm.mcbHandleDbg);

	if(bits)
		mmCollisionBitsHandleCreateFG(e->mm.movement, &e->mm.mcbHandleDbg, __FILE__, __LINE__, bits);
}

AUTO_COMMAND ACMD_NAME(mmRequestNetAutoDebug) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(1);
void entCon_mmCmdRequestNetAutoDebug(Entity* e)
{
	mmClientRequestNetAutoDebug(SAFE_MEMBER(entGetClientLink(e), movementClient));
}

AUTO_COMMAND ACMD_NAME(mmTestInteraction) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(1);
void entCon_mmCmdTestInteraction(	Entity* e,
									const Vec3 posTarget,
									const Vec3 pyrTarget)
{
	MRInteractionPath*		p;
	MRInteractionWaypoint*	wp;

	if(	!e->mm.mrInteraction &&
		!mrInteractionCreate(e->mm.movement, &e->mm.mrInteraction))
	{
		return;
	}
	
	p = StructAlloc(parse_MRInteractionPath);

	wp = StructAlloc(parse_MRInteractionWaypoint);
	eaPush(&p->wps, wp);
	copyVec3(posTarget, wp->pos);
	PYRToQuat(pyrTarget, wp->rot);
	
	mrInteractionSetPath(e->mm.mrInteraction, &p);
}

static Vec3 mmTestInteractionSitOffset = {0, 1.6, -1};
AUTO_COMMAND ACMD_NAME(mmTestInteractionSitOffset);
void entCon_mmTestInteractionSitOffset(	Entity* e,
										const Vec3 vecOffset)
{
	copyVec3(vecOffset, mmTestInteractionSitOffset);
}

static void parseAnimBitNamesIntoHandles(	const char* bitNames,
											U32** bitHandles)
{
	char		bitNamesCopy[1000];
	char*		cursor = bitNamesCopy;
	const char*	bitName;

	strcpy(bitNamesCopy, bitNames);
	while(bitName = strsep(&cursor, "+")){
		eaiPush(bitHandles, mmGetAnimBitHandleByName(bitName, 0));
	}
}

static void mmTestInteractionSitMsgHandler(const MRInteractionOwnerMsg* msg){
	switch(msg->msgType){
		xcase MR_INTERACTION_OWNER_MSG_FINISHED:{
			printf("Sit finished.\n");
		}
		
		xcase MR_INTERACTION_OWNER_MSG_FAILED:{
			printf("Sit failed.\n");
		}
		
		xcase MR_INTERACTION_OWNER_MSG_DESTROYED:{
			printf("Sit destroyed.\n");
		}

		xcase MR_INTERACTION_OWNER_MSG_REACHED_WAYPOINT:{
			printf("Reached waypoint %u.\n", msg->reachedWaypoint.index);
		}

		xcase MR_INTERACTION_OWNER_MSG_WAITING_FOR_TRIGGER:{
			printf("Waiting for trigger of waypoint %u.\n", msg->waitingForTrigger.waypointIndex);
		}
	}
}

AUTO_COMMAND ACMD_NAME(mmTestInteractionSit) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(1);
void entCon_mmCmdTestInteractionSit(Entity* e,
									const Vec3 posFeet,
									const Vec3 pyr,
									const char* bitNamesPre,
									const char* bitNamesHold,
									F32 secondsToMove,
									F32 secondsPostHold,
									S32 triggerIndex)
{
	MRInteractionPath*	p;
	Mat3				mat;
	Quat				rot;
	Vec3				posKnees;
	U32*				bitHandlesPre = NULL;
	U32*				bitHandlesHold = NULL;

	if(!e->mm.mrInteraction){
		if(!mrInteractionCreate(e->mm.movement, &e->mm.mrInteraction)){
			return;
		}
		
		mrInteractionSetOwner(e->mm.mrInteraction, mmTestInteractionSitMsgHandler, NULL);
	}
	
	PYRToQuat(pyr, rot);
	createMat3YPR(mat, pyr);

	mulVecMat3(mmTestInteractionSitOffset, mat, posKnees);
	addVec3(posFeet, posKnees, posKnees);

	parseAnimBitNamesIntoHandles(bitNamesPre, &bitHandlesPre);
	parseAnimBitNamesIntoHandles(bitNamesHold, &bitHandlesHold);

	if(mrInteractionCreatePathForSit(	&p,
										posFeet,
										posKnees,
										rot,
										bitHandlesPre,
										bitHandlesHold,
										secondsToMove,
										secondsPostHold))
	{
		mrInteractionSetPath(e->mm.mrInteraction, &p);
	}
	
	eaiDestroy(&bitHandlesPre);
	eaiDestroy(&bitHandlesHold);
}

AUTO_COMMAND ACMD_NAME(mmTestInteractionTrigger) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(1);
void entCon_mmCmdTestInteractionTrigger(Entity* e,
										U32 waypointIndex)
{
	mrInteractionTriggerWaypoint(e->mm.mrInteraction, waypointIndex);
}

AUTO_COMMAND ACMD_NAME(mmTestGrab) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(1);
void entCon_mmCmdTestGrab(	Entity* eSource,
							const char* target,
							F32 maxSecondsToReachTarget,
							F32 distanceToStartHold,
							F32 distanceToHold,
							F32 secondsToHold,
							const char* bitNamesSource,
							const char* bitNamesTarget)
{
	EntityRef			erTarget;
	Entity*				eTarget = entGetServerTarget(staticEnt, target, &erTarget);
	MovementRequester*	mrSource;
	MovementRequester*	mrTarget;
	MRGrabConfig		c = {0};
	
	if(!eTarget){
		return;
	}

	// Setup the config.
	
	c.actorSource.er = entGetRef(eSource);
	parseAnimBitNamesIntoHandles(bitNamesSource, &c.actorSource.animBitHandles);
	
	c.actorTarget.er = entGetRef(eTarget);
	c.actorTarget.flags.stopMoving = 1;
	parseAnimBitNamesIntoHandles(bitNamesTarget, &c.actorTarget.animBitHandles);
	
	c.maxSecondsToReachTarget = maxSecondsToReachTarget;
	c.distanceToStartHold = distanceToStartHold;
	c.distanceToHold = distanceToHold;
	c.secondsToHold = secondsToHold;
	
	// Create requesters and get their handles.
	
	mrGrabCreate(eSource->mm.movement, &mrSource);
	mrGetHandleFG(mrSource, &c.actorSource.mrHandle);

	mrGrabCreate(eTarget->mm.movement, &mrTarget);
	mrGetHandleFG(mrTarget, &c.actorTarget.mrHandle);

	// Send config to target.
	
	mrGrabSetConfig(mrSource, &c);

	// Send config to source.

	c.flags.isTarget = 1;
	mrGrabSetConfig(mrTarget, &c);
	
	// Cleanup.
	
	StructDeInit(parse_MRGrabConfig, &c);
}

AUTO_COMMAND ACMD_NAME(mmTestEmote) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(1);
void entCon_mmCmdTestEmote(	Entity* e,
							const char* bitNames)
{
	MREmoteSet* set = StructCreate(parse_MREmoteSet);

	set->flags.destroyOnMovement = 1;
	if(gConf.bNewAnimationSystem){
		set->animToStart = mmGetAnimBitHandleByName(bitNames, 0);
	}else{
		parseAnimBitNamesIntoHandles(bitNames, &set->animBitHandles);
	}
	gslEntMovementCreateEmoteRequester(e);
	mrEmoteSetDestroy(e->mm.mrEmote, &e->mm.mrEmoteSetHandle);
	mrEmoteSetCreate(e->mm.mrEmote, &set, &e->mm.mrEmoteSetHandle);
}

AUTO_COMMAND ACMD_NAME(mmTestEmoteStop) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(1);
void entCon_mmCmdTestEmoteStop(Entity* e){
	mrEmoteSetDestroy(e->mm.mrEmote, &e->mm.mrEmoteSetHandle);
}

static void entCon_mmSetCapsule(Entity* e,
								const Capsule* c)
{
	MovementBodyDesc*	bd;
	MovementBody*		b;

	mmBodyDescCreate(&bd);
				
	mmBodyDescAddCapsule(bd, c);

	mmResourceDestroyFG(e->mm.movement,
						&e->mm.movementBodyHandle);
	
	mmBodyCreate(&b, &bd);

	mmrBodyCreateFG(e->mm.movement,
					&e->mm.movementBodyHandle,
					b,
					0,
					0);
}

AUTO_COMMAND ACMD_NAME(mmSetCapsule) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(9);
void entCon_mmCmdSetCapsule(Entity* e,
							F32 length,
							F32 radius)
{
	Capsule c = {0};
				
	MAX1(length, 0.001f);
	MAX1(radius, 0.001f);
				
	c.vDir[1] = 1.0f;
	c.vStart[1] = radius;
	c.fLength = length;
	c.fRadius = radius;
	
	entCon_mmSetCapsule(e, &c);
}

AUTO_COMMAND ACMD_NAME(mmSetCapsuleEx) ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT ACMD_ACCESSLEVEL(9);
void entCon_mmCmdSetCapsuleEx(	Entity* e,
								F32 length,
								F32 radius,
								const Vec3 pos,
								const Vec3 dir)
{
	Capsule c = {0};

	MAX1(length, 0.001f);
	MAX1(radius, 0.001f);
	
	normalizeCopyVec3(dir, c.vDir);
	copyVec3(pos, c.vStart);
	c.fLength = length;
	c.fRadius = radius;

	entCon_mmSetCapsule(e, &c);
}

AUTO_COMMAND ACMD_SERVERONLY;
void dumpEntityStats(void)
{
	EntityIterator* iter = entGetIteratorAllTypesAllPartitions(0, ENTITYFLAG_IGNORE);
	Entity* e;
	Entity** ents = NULL;

	int numCritters = 0;
	int numPlayers = 0;
	int encounterCritters = 0;
	int entCreateCritters = 0;
	int movingCritters = 0;
	int asleepCritters = 0;
	int nodespawnCritters = 0;
	int numActiveCritters = 0;

	int outof500ft = 0;
	int outof300ft = 0;
	int outof150ft = 0;

	while(e = EntityIteratorGetNext(iter))
	{
		if(e->pPlayer)
			numPlayers++;
		else
			numCritters++;

		if (e->pCritter && e->pCritter->encounterData.pGameEncounter)
		{
			encounterCritters++;
			if (encounter_IsNoDespawn(e->pCritter->encounterData.pGameEncounter))
				nodespawnCritters++;
		}
		if(gConf.bAllowOldEncounterData && e->pCritter && e->pCritter->encounterData.parentEncounter)
		{
			if(e->pCritter->encounterData.parentEncounter)
			{
				OldEncounter* enc = e->pCritter->encounterData.parentEncounter;
				OldStaticEncounter* staticEnc = GET_REF(enc->staticEnc);

				encounterCritters++;
				if(staticEnc && staticEnc->noDespawn)
					nodespawnCritters++;
			}
		}
		if(e->erOwner)
			entCreateCritters++;
		if(e->aibase->currentlyMoving)
			movingCritters++;
		if(e->aibase->sleeping)
			asleepCritters++;

		if(!e->pPlayer)
		{
			Vec3 myPos;
			int i;
			F32 minDist = FLT_MAX;

			entGetPos(e, myPos);

			entGridProximityLookupEArray(entGetPartitionIdx(e), myPos, &ents, true);

			for(i = eaSize(&ents)-1; i >= 0; i--)
			{
				Vec3 playerPos;
				F32 distSQR;

				entGetPos(ents[i], playerPos);

				distSQR = distance3Squared(myPos, playerPos);

				if(distSQR < minDist)
					minDist = distSQR;
			}

			if(minDist > SQR(150))
				outof150ft++;
			if(minDist > SQR(300))
				outof300ft++;
			if(minDist > SQR(500))
				outof500ft++;

			if(ENTACTIVE(e))
				numActiveCritters++;
		}
	}

	EntityIteratorRelease(iter);

	printf("num critters: %d\n", numCritters);
	printf("num players: %d\n", numPlayers);
	printf("num encounter critters: %d\n", encounterCritters);
	printf("num entcreate critters: %d\n", entCreateCritters);
	printf("num moving critters: %d\n", movingCritters);
	printf("num asleep critters: %d\n", asleepCritters);
	printf("num noDespawn critters: %d\n", nodespawnCritters);
	printf("num active critters: %d\n", numActiveCritters);
	printf("num critters out of 500 ft: %d\n", outof500ft);
	printf("num critters out of 300 ft: %d\n", outof300ft);
	printf("num critters out of 150 ft: %d\n", outof150ft);

	eaDestroy(&ents);
}

// CombatLevelNatural: Removes any controls over the combat level.
AUTO_COMMAND ACMD_LIST(gEntConCmdList);
void CombatLevelNatural(Entity *e)
{
	if(e && e->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_LevelCombatNatural(entGetPartitionIdx(e), e->pChar, pExtract);
	}
}

// CombatLevelForce <Level>: Forces the combat level.
AUTO_COMMAND ACMD_LIST(gEntConCmdList);
void CombatLevelForce(Entity *e, int level)
{
	if(e && e->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_LevelCombatForce(entGetPartitionIdx(e), e->pChar,level,pExtract);
	}
}

// CombatLevelLink <Target> <Team:0/1>: Links the combat level to the target.  If Team is true, the
//  link requires being on the same team.
AUTO_COMMAND ACMD_LIST(gEntConCmdList);
void CombatLevelLink(Entity *e, char *target, int team)
{
	if(e && e->pChar)
	{
		Entity* eTarget = entGetServerTarget(e, target, NULL);
		if(eTarget && eTarget->pChar)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			character_LevelCombatLink(entGetPartitionIdx(e), e->pChar,entGetRef(eTarget),!!team, 300.0f,pExtract);
		}
	}
}

AUTO_COMMAND ACMD_LIST(gEntConCmdList);
void entSendCountMax(Entity* e, U32 value){
	if(SAFE_MEMBER2(e, pPlayer, clientLink)){
		e->pPlayer->clientLink->entSendCountMax = value;
	}
}

AUTO_TRANS_HELPER;
void containerlock(ATH_ARG NOCONST(Entity)* e)
{
	NOCONST(Entity)* foo = e;
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".*");
enumTransactionOutcome TestFullContainerLockTrans(ATR_ARGS, ATR_ALLOW_FULL_LOCK NOCONST(Entity)* e)
{
	containerlock(e);
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND;
void TestFullContainerLock(Entity* e)
{
	AutoTrans_TestFullContainerLockTrans(NULL, GetAppGlobalType(), entGetType(e), entGetContainerID(e));
}

