#if GAMESERVER
// BZ- This file only compiles on the gameserver, despite being in common and halfway converted

#include "AIAnimList.h"
#include "logging.h"
#include "EntitySavedData.h"
#include "timedeventqueue.h"
#include "EntityLib.h"
#include "Team.h"
#include "Character.h"
#include "CommandQueue.h"
#include "contact_common.h"
#include "oldencounter_common.h"
#include "Expression.h"
#include "mission_common.h"
#include "rewardCommon.h"
#include "StateMachine.h"
#include "StringCache.h"

#if GAMESERVER
#include "aiStruct.h"
#include "EntityGrid.h"
#include "GameServerLib.h"
#include "gslContact.h"
#include "gslCritter.h"
#include "gslEncounter.h"
#include "gslEventSend.h"
#include "gslGameAction.h"
#include "gslInteractable.h"
#include "Player.h"
#include "PowerActivation.h"
#include "Reward.h"
#include "TransactionOutcomes.h"
#include "worldgrid.h"

#include "AutoGen/EntityInteraction_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "Entity_h_ast.h"

#include "AutoGen/gslOldEncounter_h_ast.h"

#endif


AUTO_EXPR_FUNC(s_CritterExprFuncList) ACMD_NAME(CurrentAIState);
int critter_CurrentAIState(ExprContext* context, const char* stateName)
{
	Entity* critter = exprContextGetSelfPtr(context);

	if(!critter || !critter->aibase || !critter->aibase->fsmContext )
		return 0;
	else
		return stricmp(stateName,fsmGetState(critter->aibase->fsmContext)) == 0;
}

AUTO_EXPR_FUNC(s_CritterExprFuncList) ACMD_NAME(GetCritterSelf);
void critter_GetSelf(ExprContext* context, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* unused)
{
	Entity* critter = exprContextGetSelfPtr(context);
	eaPush(entsOut, critter);
}

void critter_ReplaceInPlace(Entity* critterEnt, char* defName, char* fsm, int iLevel)
{
	Entity* newEnt;
	CritterDef* critterDef = critter_DefGetByName(defName);
	Critter* critter = SAFE_MEMBER(critterEnt, pCritter);
	CritterCreateParams createParams = {0};

	if (!critterDef || !critterEnt)
		return;

	createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
	createParams.iPartitionIdx = entGetPartitionIdx(critterEnt);
	createParams.fsmOverride = fsm;
	createParams.pEncounter = critter->encounterData.parentEncounter;
	createParams.pActor = critter->encounterData.sourceActor;
	createParams.pEncounter2 = critter->encounterData.pGameEncounter;
	createParams.iActorIndex = critter->encounterData.iActorIndex;
	createParams.iLevel = iLevel;
	createParams.iTeamSize = critter->encounterData.activeTeamSize;
	createParams.aiTeam = critterEnt->aibase->team;
	createParams.aiCombatTeam = critterEnt->aibase->combatTeam;
	
	newEnt = critter_CreateByDef(critterDef, &createParams, NULL, true);

	if (newEnt && critter)
	{
		Vec3 vPos;
		Quat qRot;
		ANALYSIS_ASSUME(newEnt != NULL);
		entGetPos(critterEnt, vPos);
		entGetRot(critterEnt, qRot);
		entSetPos(newEnt, vPos, 1, __FUNCTION__);
		entSetRot(newEnt, qRot, 1, __FUNCTION__);

//		newEnt->pChar->gangID = critterDef->gangID;
		newEnt->pCritter->bKilled = critter->bKilled;
	}
	entDie(critterEnt, 0, 0, 0, NULL);
}

// Destroys the current critter and replaces it with a new critter using critterdef <newCritter>
// running the <fsm> fsm and spawned at <iLevel>
AUTO_EXPR_FUNC(s_CritterExprFuncList, ai) ACMD_NAME(critterSwap);
void critter_Swap(ExprContext* context, const char* newCritter, const char * fsm, int iLevel )
{
	Entity* critter = exprContextGetVarPointerUnsafe(context, "Me");

	if(!critter )
		return;
	else
	{
		critter_ReplaceInPlace( critter, (char *)newCritter, (char*)fsm, iLevel );
	}
}

#endif // GAMESERVER around whole file
