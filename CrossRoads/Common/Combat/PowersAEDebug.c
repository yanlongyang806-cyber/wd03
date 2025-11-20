#include "PowersAEDebug.h"
#include "Entity.h"
#include "Capsule.h"
#include "Character.h"
#include "Character_Combat.h"
#include "PowersAEDebug_h_ast.h"
#include "AutoGen/Powers_h_ast.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
#endif

#if GAMESERVER
	#include "gslPowersAEDebug.h"
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#elif GAMECLIENT
	#include "gclEntity.h"
	#include "gclPowersAEDebug.h"
#endif

S32 g_powersDebugAEOn = false;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ------------------------------------------------------------------------------------------------
static bool shouldSendDebugDrawEnt(Entity *e, PowerDef *pPowerDef)
{
	if (!g_powersDebugAEOn)
		return false;

#ifdef GAMESERVER
	if (!gslPowersAEDebug_ShouldSendForEnt(e))
		return false;
#endif

	{
		if (pPowerDef)
		{
			int eType = StaticDefineIntGetInt(PowerTypeEnum, "Passive");
			if (pPowerDef->eType == eType)
				return false;
		}
	}

	return true;
}


// ------------------------------------------------------------------------------------------------
static void fixupCastLocation(Entity *e, const Vec3 vst, const Vec3 ved, Vec3 vOutDir, Vec3 vOutPos)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	subVec3(ved, vst, vOutDir);
	normalVec3(vOutDir);

	if (e)
	{
		const Capsule*const* capsSource = NULL;
		if (mmGetCapsules(e->mm.movement, &capsSource) && eaSize(&capsSource))
		{
			scaleAddVec3(vOutDir, capsSource[0]->fRadius, vst, vOutPos);
		}
	}
	else
	{
		copyVec3(vst, vOutPos);
	}
#endif
}

// ------------------------------------------------------------------------------------------------
PowerDebugAE *createPowerDebugData(Entity *e, CombatTarget **eaTargets)
{
	PowerDebugAE *p = calloc(1, sizeof(PowerDebugAE));
	if (p)
	{
		if (e)
		{
			p->erEnt = entGetRef(e);
			entGetPos(e, p->vCasterPos);
		}

		FOR_EACH_IN_EARRAY(eaTargets, CombatTarget, ptarget)
		{
			if (ptarget->pChar && ptarget->pChar->pEntParent)
			{
				AEPowersDebugHit *pHit = malloc(sizeof(AEPowersDebugHit));
				pHit->erEnt = entGetRef(ptarget->pChar->pEntParent);
				entGetPos(ptarget->pChar->pEntParent, pHit->vPos);
				eaPush(&p->eaHitEnts, pHit);
			}
		}
		FOR_EACH_END

			return p;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------
static PowerDebugAE* PowersAEDebug_PrepAEDebug(Entity *e, PowerDef *pPowerDef, CombatTarget **eaTargets, Entity **ppdebugger)
{
	PowerDebugAE	*pDebugData = NULL;
	
	if (!pPowerDef)
		return NULL;

#ifdef GAMECLIENT
	*ppdebugger = gclPowersAEDebug_GetDebuggingEnt();
#endif
#ifdef GAMESERVER
	*ppdebugger = gslPowersAEDebug_GetDebuggingEnt();
#endif

	if (!*ppdebugger || !shouldSendDebugDrawEnt(e, pPowerDef))
		return NULL;
	return createPowerDebugData(e, eaTargets);
}

// ------------------------------------------------------------------------------------------------
static void PowersAEDebug_SendThenDestroyAEDebug(Entity *debugger, PowerDebugAE* pDebugData)
{
#ifdef GAMECLIENT
	pDebugData->isClient = true;
	PowersAEDebug_AddAEDebug(pDebugData);
#endif
#ifdef GAMESERVER
	ClientCmd_PowersAEDebug_AddAEDebug(debugger, pDebugData);
#endif

	StructDestroy(parse_PowerDebugAE, pDebugData);
}

// ------------------------------------------------------------------------------------------------
void PowersAEDebug_AddLocation(	Entity *e, 
								PowerDef *p, 
								CombatTarget **eaTargets,
								const Vec3 vLoc)
{
	Entity *debugger = NULL;
	PowerDebugAE* pDebugData = PowersAEDebug_PrepAEDebug(e, p, eaTargets, &debugger);

	if (!pDebugData)
		return;

	pDebugData->eType = kEffectArea_Location;
	copyVec3(vLoc, pDebugData->vCastLoc);
	
	PowersAEDebug_SendThenDestroyAEDebug(debugger, pDebugData);
}

// ------------------------------------------------------------------------------------------------
void PowersAEDebug_AddCylinder( Entity *e, 
								  PowerDef *p, 
								  CombatTarget **eaTargets,
								  const Vec3 vStartLoc, 
								  const Vec3 vDir, 
								  F32 length, 
								  F32 radius)
{
	Entity *debugger = NULL;
	PowerDebugAE* pDebugData = PowersAEDebug_PrepAEDebug(e, p, eaTargets, &debugger);

	if (!pDebugData)
		return;

	pDebugData->eType = kEffectArea_Cylinder;
	{
		copyVec3(vStartLoc, pDebugData->vCastLoc);
		scaleAddVec3(vDir, length, vStartLoc, pDebugData->vTargetLoc);

		pDebugData->fRadius = radius;
		pDebugData->fLength = length;
	}

	PowersAEDebug_SendThenDestroyAEDebug(debugger, pDebugData);

}

// ------------------------------------------------------------------------------------------------
void PowersAEDebug_AddCone(Entity *e, 
								  PowerDef *p,
								  CombatTarget **eaTargets,
								  const Vec3 vStartLoc, 
								  const Vec3 vDir, 
								  F32 angle, 
								  F32 len,
								  F32 startRadius)
{
	Entity *debugger = NULL;
	PowerDebugAE* pDebugData = PowersAEDebug_PrepAEDebug(e, p, eaTargets, &debugger);

	if (!pDebugData)
		return;

	pDebugData->eType = kEffectArea_Cone;

	{
		copyVec3(vStartLoc, pDebugData->vCastLoc);
		scaleAddVec3(vDir, len, vStartLoc, pDebugData->vTargetLoc);

		pDebugData->fArc = angle;
		pDebugData->fLength = len;
		pDebugData->fRadius = startRadius;
	}

	PowersAEDebug_SendThenDestroyAEDebug(debugger, pDebugData);
}

// ------------------------------------------------------------------------------------------------
void PowersAEDebug_AddSphere(Entity *e, 
									PowerDef *p,
									CombatTarget **eaTargets,
									const Vec3 vLoc, 
									F32 fRadius)
{
	Entity *debugger = NULL;
	PowerDebugAE* pDebugData = PowersAEDebug_PrepAEDebug(e, p, eaTargets, &debugger);

	if (!pDebugData)
		return;

	pDebugData->eType = kEffectArea_Sphere;

	{
		copyVec3(vLoc, pDebugData->vCastLoc);
		pDebugData->fRadius = fRadius;
	}

	PowersAEDebug_SendThenDestroyAEDebug(debugger, pDebugData);
}

#include "PowersAEDebug_h_ast.c"