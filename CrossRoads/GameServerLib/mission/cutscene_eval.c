/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "cutscene_common.h"
#include "cutscene.h"
#include "Entity.h"
#include "error.h"
#include "Estring.h"
#include "Expression.h"
#include "gslNamedPoint.h"
#include "mission_common.h"
#include "StringCache.h"


// ----------------------------------------------------------------------------------
// Static data
// ----------------------------------------------------------------------------------

static U32 s_CutscenesDisabled = 0;


// ----------------------------------------------------------------------------------
// Cut Scenes (Player)
// ----------------------------------------------------------------------------------

// Commandline option to disable cutscenes
AUTO_CMD_INT(s_CutscenesDisabled, DisableCutscenes) ACMD_CMDLINE;

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(CutsceneDefStartEntArray);
ExprFuncReturnVal exprFuncCutsceneDefStartEntArray(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* cutsceneName, ACMD_EXPR_ERRSTRING errString)
{
	if(!s_CutscenesDisabled)
	{
		CutsceneDef* pCutscene = RefSystem_ReferentFromString(g_hCutsceneDict, cutsceneName);
		int i, n = eaSize(entsInOut);
		Entity *e;

		if(!pCutscene)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Unknown cutscene def %s", cutsceneName);
			return ExprFuncReturnError;
		}

		// Validate all ents first
		for(i = n-1; i >= 0; i--)
		{
			e = (*entsInOut)[i];
			if(!e->pPlayer)
			{
				estrPrintf(errString, "Passed in non-player to CutsceneDefStartEntArray");
				return ExprFuncReturnError;
			}
			if(entGetPartitionIdx(e) != iPartitionIdx)
			{
				estrPrintf(errString, "Passed in player that is not part of the partition to CutsceneDefStartEntArray");
				return ExprFuncReturnError;			
			}
		}

		// if single player and multiple players in cutscene
		
		if(!pCutscene->bSinglePlayer && n > 1)
		{
			// just play it for the first ent -- as all entities will get the msg anyway
			e = (*entsInOut)[0];
			cutscene_StartOnServer(pCutscene, e, false);
		}
		else
		{
			for(i = n-1; i >= 0; i--)
			{
				e = (*entsInOut)[i];
				cutscene_StartOnServer(pCutscene, e, false);
			}
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(CutsceneDefStart);
void exprFuncCutsceneDefStart(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, const char* cutsceneName)
{
	if(!s_CutscenesDisabled)
	{
		Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
		CutsceneDef* pCutscene = RefSystem_ReferentFromString(g_hCutsceneDict, cutsceneName);

		// TODO: add an error if there's already a cutscene running on the player

		if(!pCutscene)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Unknown cutscene def %s", cutsceneName);
			return;
		}

		// If this is a single-player cutscene, there must be a player in the context
		if(playerEnt || !pCutscene->bSinglePlayer)
		{
			cutscene_StartOnServerEx(pCutscene, playerEnt, iPartitionIdx, NULL, 0, false);
		}
		else
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Cutscene def %s is a single-player cutscene, but there is no player in the context", cutsceneName);
			return;
		}
	}
}

//This is for testing purposes only
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL( 9 );
void cutsceneStartFromClientCommand_ForcePartition(Entity* playerEnt, const char* cutsceneName, int iPartitionIdx)
{
	CutsceneDef* pCutscene = RefSystem_ReferentFromString(g_hCutsceneDict, cutsceneName);
	if(!pCutscene)
	{
		Alertf("Unknown cutscene def %s", cutsceneName);
		return;
	}

	// If this is a single-player cutscene, there must be a player in the context
	if(!pCutscene->bSinglePlayer)
	{
		cutscene_StartOnServerEx(pCutscene, playerEnt, iPartitionIdx, NULL, 0, false);
	}
	else
	{
		Alertf("Failed to run %s because cutscene def can not be single player", cutsceneName);
		return;
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL( 9 );
void cutsceneStartFromClientCommand(Entity* playerEnt, const char* cutsceneName)
{
	CutsceneDef* pCutscene = RefSystem_ReferentFromString(g_hCutsceneDict, cutsceneName);
	if(!pCutscene)
	{
		Alertf("Unknown cutscene def %s", cutsceneName);
		return;
	}

	// If this is a single-player cutscene, there must be a player in the context
	if(playerEnt)
	{
		cutscene_StartOnServer(pCutscene, playerEnt, false);
	}
	else
	{
		Alertf("Cutscene def %s could not be played because there is no player in the context", cutsceneName);
		return;
	}
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(CutsceneInProgress);
bool exprFuncCutsceneInProgress(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, const char* cutsceneName)
{
	if(cutsceneName && cutscene_FindActiveCutsceneByName(cutsceneName, iPartitionIdx))
		return true;

	return false;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCropWatchingCutscene);
ExprFuncReturnVal exprFuncEntCropWatchingCutscene(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* cutsceneName, ACMD_EXPR_ERRSTRING errString)
{
	ActiveCutscene* pCutscene;
	Entity *e;
	int i, n = eaSize(entsInOut);
	int j, k;

	if(!cutsceneName)
	{
		estrPrintf(errString, "No cutscene name given for CutsceneInProgressForPlayer");
		return ExprFuncReturnError;		
	}

	pCutscene=cutscene_FindActiveCutsceneByName(cutsceneName, iPartitionIdx);

	// Validate all ents first
	for(i=n-1; i>=0; i--)
	{
		e = (*entsInOut)[i];
		if(!e->pPlayer)
		{
			estrPrintf(errString, "Passed in non-player to CutsceneInProgressForPlayer");
			return ExprFuncReturnError;
		}
	}

	k = pCutscene ? eaiSize(&pCutscene->pPlayerRefs) : 0;

	// Now check to see if each entity is watching the cutscene
	for(i=n-1; i>=0; i--)
	{
		bool bRemove = true;
		e = (*entsInOut)[i];

		for(j=k-1; j>=0; j--)
		{
			if(e->myRef == pCutscene->pPlayerRefs[j])
			{
				bRemove = false;
			}
		}
		if(bRemove)
		{
			eaRemoveFast(entsInOut, i);
		}
	}

	return ExprFuncReturnFinished;
}
