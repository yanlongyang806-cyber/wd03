#if 0
#include "gclScript.h"
#include "pyLib.h"

#include "AttribModFragility.h"
#include "Character.h"
#include "Entity.h"
#include "gclEntity.h"
#include "timing_profiler.h"

PY_FUNC(gclScript_GetPlayerRef)
{
	Entity *pPlayer = NULL;

	PERFINFO_AUTO_START_FUNC();
	PY_PARSE_ARGS_PERF_STOP("");
	pPlayer = entActivePlayerPtr();
	PERFINFO_AUTO_STOP();
	
	if(pPlayer)
		return Py_BuildValue("i", entGetRef(pPlayer));
	else
		return PyLib_IncNone;
}

#define GCLSCRIPT_ENTITY_GET_AND_DO(ent, ret) \
	Entity *ent = NULL; \
	EntityRef iRef = 0; \
	PERFINFO_AUTO_START_FUNC(); \
	PY_PARSE_ARGS_PERF_STOP("i", &iRef); \
	ent = entFromEntityRef(iRef); \
	PERFINFO_AUTO_STOP(); \
	if(ent) { ret } \
	else return PyLib_IncNone

PY_FUNC(gclScript_GetEntityName)
{
	GCLSCRIPT_ENTITY_GET_AND_DO(pEnt,
		return Py_BuildValue("s", entGetLocalName(pEnt));
	);
}

PY_FUNC(gclScript_GetEntityID)
{
	GCLSCRIPT_ENTITY_GET_AND_DO(pEnt,
		return Py_BuildValue("i", entGetContainerID(pEnt));
	);
}

PY_FUNC(gclScript_GetEntityHP)
{
	GCLSCRIPT_ENTITY_GET_AND_DO(pEnt,
		return Py_BuildValue("f", SAFE_MEMBER2(entGetChar(pEnt), pattrBasic, fHitPoints));
	);
}

PY_FUNC(gclScript_GetEntityMaxHP)
{
	GCLSCRIPT_ENTITY_GET_AND_DO(pEnt,
		return Py_BuildValue("f", SAFE_MEMBER2(entGetChar(pEnt), pattrBasic, fHitPointsMax));
	);
}

PY_FUNC(gclScript_GetEntityLevel)
{
	GCLSCRIPT_ENTITY_GET_AND_DO(pEnt,
		return Py_BuildValue("i", SAFE_MEMBER(entGetChar(pEnt), iLevelCombat));
	);
}

PY_FUNC(gclScript_GetEntityTarget)
{
	GCLSCRIPT_ENTITY_GET_AND_DO(pEnt,
		EntityRef iTargetRef = 0;
		entGetClientTarget(pEnt, "selected", &iTargetRef);
		return Py_BuildValue("i", iTargetRef);
	);
}

PY_FUNC(gclScript_GetEntityShields)
{
	GCLSCRIPT_ENTITY_GET_AND_DO(pEnt,
		F32 fShields[4];
		int i;

		for(i = 0; i < 4; ++i)
		{
			fShields[i] = 0;
		}

		if(pEnt->pChar)
		{
			int iSpaceCategory = StaticDefineIntGetInt(PowerCategoriesEnum, "Region_Space");
			int iShieldCategory = StaticDefineIntGetInt(PowerCategoriesEnum, "Shield");

			FOR_EACH_IN_EARRAY(pEnt->pChar->modArray.ppMods, AttribMod, pMod)
			{
				PowerDef *pDef = GET_REF(pMod->hPowerDef);

				if(!pDef || iSpaceCategory == -1 || iShieldCategory == -1 || eaiFind(&pDef->piCategories, iSpaceCategory) == -1 || eaiFind(&pDef->piCategories, iShieldCategory) == -1)
				{
					continue;
				}

				fShields[pMod->uiDefIdx] = SAFE_MEMBER(pMod->pFragility, fHealth) / MAX(1, SAFE_MEMBER(pMod->pFragility, fHealthMax));
			}
			FOR_EACH_END
		}

		return Py_BuildValue("ffff", fShields[0], fShields[1], fShields[2], fShields[3]);
	);
}

PY_FUNC(gclScript_GetEntityPos);
PY_FUNC(gclScript_GetEntityPYR);
PY_FUNC(gclScript_GetEntityDistance);
PY_FUNC(gclScript_GetEntityNearbyFriends);
PY_FUNC(gclScript_GetEntityNearbyHostiles);
PY_FUNC(gclScript_GetEntityNearbyObjects);

// Entity state flags
PY_FUNC(gclScript_IsEntityValid);
PY_FUNC(gclScript_IsEntityDead);
PY_FUNC(gclScript_IsEntityHostile);
PY_FUNC(gclScript_IsEntityCasting);

// Entity mission info
PY_FUNC(gclScript_DoesEntityHaveMission);
PY_FUNC(gclScript_DoesEntityHaveCompletedMission);
PY_FUNC(gclScript_IsEntityImportant);
#endif