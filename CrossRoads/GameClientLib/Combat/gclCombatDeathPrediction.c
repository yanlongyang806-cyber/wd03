#include "Entity.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "gclEntity.h"
#include "EntityMovementDead.h"
#include "EntityMovementManager.h"
#include "Powers.h"
#include "Character.h"
#include "dynAnimInterface.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct CombatDeathPrediction
{
	U32						uiTime;
	EntityRef				erTarget;
	PowerRef				powerRef;
	U8						uchActID;
} CombatDeathPrediction;

static bool s_bDeathPredictionDebugging = false;

static CombatDeathPrediction **s_eaCombatDeathPrediction = NULL;
static CombatDeathPrediction **s_eaRecentDeathPrediction = NULL;

#define CombatDeathPredictionDebugPrint(format,...) if(s_bDeathPredictionDebugging) combatdebug_PowersDebugPrint(NULL,EPowerDebugFlags_DEATHPREDICT,format,##__VA_ARGS__)

// --------------------------------------------------------------------------------------------------------------------
void gclCombatDeathPrediction_Shutdown()
{
	FOR_EACH_IN_EARRAY(s_eaCombatDeathPrediction, CombatDeathPrediction, pPrediction)
		if (GET_REF(pPrediction->powerRef.hdef))
			REMOVE_HANDLE(pPrediction->powerRef.hdef);
	FOR_EACH_END
	eaDestroyEx(&s_eaCombatDeathPrediction, NULL);

	FOR_EACH_IN_EARRAY(s_eaRecentDeathPrediction, CombatDeathPrediction, pPrediction)
		if (GET_REF(pPrediction->powerRef.hdef))
			REMOVE_HANDLE(pPrediction->powerRef.hdef);
	FOR_EACH_END
	eaDestroyEx(&s_eaRecentDeathPrediction, NULL);

}

// --------------------------------------------------------------------------------------------------------------------
static void gclCombatDeathPrediction_Free(CombatDeathPrediction *pPrediction)
{
	if (pPrediction)
	{
		if (GET_REF(pPrediction->powerRef.hdef))
			REMOVE_HANDLE(pPrediction->powerRef.hdef);
		free (pPrediction);
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatDeathPrediction_OncePerFrame()
{
	U32 uiTime = pmTimestamp(0);
	Entity * pEnt = entActivePlayerPtr();

	if (!pEnt || !pEnt->pChar)
		return;

	FOR_EACH_IN_EARRAY(s_eaCombatDeathPrediction, CombatDeathPrediction, pPrediction)
	{
		if (uiTime >= pPrediction->uiTime)
		{
			Entity *eTarget = entFromEntityRefAnyPartition(pPrediction->erTarget);
			PowerDef *pDef = GET_REF(pPrediction->powerRef.hdef);

			if (eTarget && entIsAlive(eTarget))
			{
				PowerAnimFX *pAfx = NULL;

				if (pDef)
					pAfx = GET_REF(pDef->hFX);

				eTarget->bDeathPredicted = true;

				mmSetInDeathPredictionFG(eTarget->mm.movement, true);

				entity_DeathAnimationUpdate(eTarget, true, uiTime);
					
				if (pAfx)
					character_AnimFXDeath(PARTITION_CLIENT, eTarget->pChar, pEnt->pChar, pAfx, uiTime);
			}
			
			eaRemoveFast(&s_eaCombatDeathPrediction, FOR_EACH_IDX(-,pPrediction));
			// save this prediction incase something bad happens and we need to undo it
			eaPush(&s_eaRecentDeathPrediction, pPrediction);
		}
	}
	FOR_EACH_END

	// go through the recent predictions and remove old ones
	FOR_EACH_IN_EARRAY(s_eaRecentDeathPrediction, CombatDeathPrediction, pPrediction)
	{
		if (uiTime > pPrediction->uiTime && (uiTime - pPrediction->uiTime)/MM_PROCESS_COUNTS_PER_SECOND > 5)
		{
			gclCombatDeathPrediction_Free(pPrediction);
			
			eaRemoveFast(&s_eaRecentDeathPrediction, FOR_EACH_IDX(-,pPrediction));
		}
	}
	FOR_EACH_END
}

// --------------------------------------------------------------------------------------------------------------------
static CombatDeathPrediction* gclCombatDeathPrediction_Add(EntityRef erTarget, PowerRef *pPowerRef, U8 uchActID, U32 uiTime)
{
	CombatDeathPrediction *pPrediction = calloc(1, sizeof(CombatDeathPrediction));
	if (pPrediction)
	{
		pPrediction->erTarget = erTarget;
		pPrediction->uiTime = uiTime;
		pPrediction->uchActID = uchActID;
		
		if (pPowerRef)
		{
			pPrediction->powerRef = *pPowerRef;
			COPY_HANDLE(pPrediction->powerRef.hdef, pPowerRef->hdef);
		}
		return pPrediction;				
	}
	
	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
static void gclCombatDeathPrediction_UndoPredictedDeath(Entity *pEnt)
{
	if (pEnt && pEnt->bDeathPredicted)
	{
		pEnt->bDeathPredicted = false;

		mmSetInDeathPredictionFG(pEnt->mm.movement, false);

		if (pEnt->mm.mrDead)
		{
			mrDestroy(&pEnt->mm.mrDead);
		}
				
		dtSkeletonEndDeathAnimation(pEnt->dyn.guidSkeleton);

		CombatDeathPredictionDebugPrint("DeathPrediction: MIRESPREDICTAlready started death prediction for EntityRef(%d). Undoing.", entGetRef(pEnt) );
	}
}

// --------------------------------------------------------------------------------------------------------------------
static void _NotifyActCanceled(U8 uchID)
{
	bool bFound = false;
	FOR_EACH_IN_EARRAY(s_eaCombatDeathPrediction, CombatDeathPrediction, pPrediction)
	{
		if (pPrediction->uchActID == uchID)
		{
			eaRemoveFast(&s_eaCombatDeathPrediction, FOR_EACH_IDX(-, pPrediction));
			eaPush(&s_eaRecentDeathPrediction, pPrediction);
			bFound = true;

			CombatDeathPredictionDebugPrint("DeathPrediction: Canceling death prediction for EntityRef(%d).", pPrediction->erTarget);
		}
	}
	FOR_EACH_END
	
	// check any death predictions that have recently happened- do we need to undo ?
	FOR_EACH_IN_EARRAY(s_eaRecentDeathPrediction, CombatDeathPrediction, pPrediction)
	{
		if (pPrediction->uchActID == uchID)
		{
			Entity *pTarget = entFromEntityRefAnyPartition(pPrediction->erTarget);
			if (pTarget)
			{
				gclCombatDeathPrediction_UndoPredictedDeath(pTarget);
				bFound = true;
			}
		}
	}
	FOR_EACH_END


	if (!bFound)
	{
		CombatDeathPrediction *pPrediction = gclCombatDeathPrediction_Add(0, NULL, uchID, pmTimestamp(0));

		CombatDeathPredictionDebugPrint("DeathPrediction: Prediction not found for ActID(%d).", uchID);

		eaPush(&s_eaRecentDeathPrediction, pPrediction);
	}
}

// --------------------------------------------------------------------------------------------------------------------
// if the local player canceled an activation, see if the activation pertained to a predicted death instance
void gclCombatDeathPrediction_NotifyActCanceled(Character *pChar, const PowerActivation *pAct)
{
	_NotifyActCanceled(pAct->uchID);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersUnpredictCritterDeath(U8 uchActID)
{
	_NotifyActCanceled(uchActID);
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowersPredictCritterDeath(EntityRef erTarget, PowerRef *pPowerRef, U8 uchActID, U32 uiTime)
{
	Entity * pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar)
	{
		CombatDeathPrediction *pPrediction = NULL;

		// we need to validate that the server isn't telling us about a predicted death of a power that we *just* cancelled ourselves
		FOR_EACH_IN_EARRAY(s_eaRecentDeathPrediction, CombatDeathPrediction, pRecent)
		{
			if (pRecent->uchActID == uchActID)
				return; // we already cancelled this power ourselves, ignore this prediction
		}
		FOR_EACH_END

		pPrediction = gclCombatDeathPrediction_Add(erTarget, pPowerRef, uchActID, uiTime);

		eaPush(&s_eaCombatDeathPrediction, pPrediction);
	}
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void CombatDeathPredictionDebugClient(int bOn)
{
	s_bDeathPredictionDebugging	= !!bOn;
}

// --------------------------------------------------------------------------------------------------------------------
S32 gclCombatDeathPrediction_IsDeathPredicted(Entity *pEnt)
{
	EntityRef erTarget = entGetRef(pEnt);

	FOR_EACH_IN_EARRAY(s_eaCombatDeathPrediction, CombatDeathPrediction, pPrediction)
	{
		if (pPrediction->erTarget == erTarget)
		{
			return true;
		}
	}
	FOR_EACH_END
	
	return false;
}