#include "CombatReactivePower.h" 
#include "Character.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "PowerAnimFX.h"
#include "ResourceManager.h"
#include "file.h"
#include "Character_combat.h"
#include "CharacterAttribs.h"

#include "PowersMovement.h"
#include "EntityMovementDefault.h"

#include "CombatReactivePower_h_ast.h"

extern S32 g_CombatReactivePowerDebug;

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void CombatReactivePowerDebugServer(S32 enabled)
{
	g_CombatReactivePowerDebug = !!enabled;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void CombatReactivePower_ActivateServer(Entity* pEnt, F32 fActivateYaw, U32 uiActivateTime, U8 uActId, F32 fTimeOffset)
{
	if (pEnt && pEnt->pChar)
	{
		CombatReactivePowerDef *pDef = NULL;
		CombatReactivePowerInfo *pInfo = NULL;

		if (!pEnt->pChar->pCombatReactivePowerInfo)
			return;
		pInfo = pEnt->pChar->pCombatReactivePowerInfo;
		pDef = GET_REF(pInfo->hCombatBlockDef);
		if (!pDef)
			return;

		if (pInfo->eState != ECombatReactivePowerState_NONE)
		{
			CombatReactivePower_Stop(pEnt->pChar, pInfo, pDef, pmTimestamp(0), true);
		}
			
		pInfo->uCurActIdOffset = uActId;
		
		fTimeOffset = CLAMP(fTimeOffset, 0.f, 0.5f);

		CombatReactivePower_Begin(pEnt->pChar, pInfo, pDef, fActivateYaw, uiActivateTime, fTimeOffset);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void CombatReactivePower_DeactivateServer(Entity* pEnt, U32 uiActivateTime, U8 eState, F32 fTimer)
{
	if (pEnt && pEnt->pChar)
	{
		CombatReactivePowerDef *pDef = NULL;
		CombatReactivePowerInfo *pInfo = NULL;

		if (!pEnt->pChar->pCombatReactivePowerInfo)
			return;
		pInfo = pEnt->pChar->pCombatReactivePowerInfo;
		pDef = GET_REF(pInfo->hCombatBlockDef);
		if (!pDef)
			return;

		if (CombatReactivePower_CanToggleDeactivate(pDef))
		{
			CombatReactivePower_Stop(pEnt->pChar, pInfo, pDef, uiActivateTime, false);
		}
	}
}


// --------------------------------------------------------------------------------------------------------------------
void gslCombatReactivePower_Update(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	
	if (pInfo->eState != ECombatReactivePowerState_NONE)
	{
		if (pDef->pchAttribPoolName)
		{
			if (pDef->pExprMovementAttribCost && pDef->eCostAttrib)
			{
				if (!CombatReactivePower_IsMoving(pChar))
					return;
			}
			attribPool_SetOverrideTickTimer(pChar, pDef->pchAttribPoolName, pDef->fAttribPoolPostDelayTimer);
		}
	}
}
