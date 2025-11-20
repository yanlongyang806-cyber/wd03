#include "EntityMovementRequesterDefs.h"
#include "CharacterClass.h"
#include "Character.h"
#include "CombatConfig.h"
#include "CombatReactivePower.h"
#include "file.h"
#include "ResourceManager.h"
#include "ReferenceSystem.h"
#include "entity.h"
#include "EntityMovementRequesterDefs_h_ast.h"


DictionaryHandle g_hMovementRequesterDefDict;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););


// -------------------------------------------------------------------------------------------------------------------
static int MovementRequesterDefValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, MovementRequesterDef *pDef, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			if (!pDef->pParams)
			{
				ErrorFilenamef(pDef->pchFilename, "MovementRequester: %s has no Params defined.", pDef->pchName);
			}
		} break;
	}

	return VALIDATE_NOT_HANDLED;
}


// ------------------------------------------------------------------------------------------------------------------------------
AUTO_RUN;
int RegisterEntityMovementRequesterDefsDict(void)
{
	g_hMovementRequesterDefDict = RefSystem_RegisterSelfDefiningDictionary("MovementRequesterDef",
																		false, 
																		parse_MovementRequesterDef, 
																		true, 
																		true, 
																		NULL);

	resDictManageValidation(g_hMovementRequesterDefDict, MovementRequesterDefValidateCB);
	resDictSetDisplayName(g_hMovementRequesterDefDict, "MovementRequesterDef", "MovementRequesterDefs", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hMovementRequesterDefDict);
		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hMovementRequesterDefDict, ".Name", NULL, NULL, NULL, NULL);
		}
	}
	else
	{
		resDictRequestMissingResources(g_hMovementRequesterDefDict, 8, false, resClientRequestSendReferentCommand);
	}

	return 1;
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(EntityMovementRequesterDefs);
void EntityMovementRequesterLoadDefs(void)
{
	if(!IsClient())
	{
		resLoadResourcesFromDisk(g_hMovementRequesterDefDict, "defs/requesters", ".requester", "requesters.bin",  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	}
}

void mrFixupTacticalRollDef(TacticalRequesterRollDef *pDef)
{
	MINMAX1(pDef->fRollFuelCost, 0.f, 1000.f);
	MINMAX1(pDef->fRollDistance, 0.f, 100.f);
	MINMAX1(pDef->fRollSpeed, 0.f, 500.f);
	MINMAX1(pDef->fRollPostHoldSeconds, 0.f, 10.f);
	MINMAX1(pDef->fRollCooldown, 0.f, 100.f);
	MINMAX1(pDef->fRollFuelCost, 0.f, 100.f);
}

void mrFixupTacticalSprintDef(TacticalRequesterSprintDef *pDef)
{
	if(pDef->fSpeedScaleSprint <= 0)
	{
		pDef->fSpeedScaleSprint = 1.f;
	}
	if(pDef->fSpeedScaleSprintCombat <= 0)
	{
		pDef->fSpeedScaleSprintCombat = 1.f;
	}

	if (pDef->fSpeedSprint < 0.f)
	{
		pDef->fSpeedSprint = 0.f;
	}

	if (pDef->fSpeedSprintCombat < 0.f)
	{
		pDef->fSpeedSprintCombat = 0.f;
	}

	if (pDef->fSpeedSprint > 0.f || pDef->fSpeedSprintCombat > 0.f)
	{
		pDef->fSpeedScaleSprint = 0.f;
		pDef->fSpeedScaleSprintCombat = 0.f;
	}

	if (pDef->fSpeedSprint > 0.f && pDef->fSpeedSprintCombat == 0.f)
	{
		pDef->fSpeedSprintCombat = pDef->fSpeedSprint;
	}
	else if (pDef->fSpeedSprintCombat > 0.f && pDef->fSpeedSprint == 0.f)
	{
		pDef->fSpeedSprint = pDef->fSpeedSprintCombat;
	}

	if(pDef->fRunMaxDurationSecondsCombat <= 0)
	{
		pDef->fRunMaxDurationSecondsCombat = pDef->fRunMaxDurationSeconds;
	}
	if(pDef->fRunCooldownCombat <= 0)
	{
		pDef->fRunCooldownCombat = pDef->fRunCooldown;
	}
}

TacticalRequesterRollDef* mrRequesterDef_GetRollDefForEntity(SA_PARAM_OP_VALID Entity *e, SA_PARAM_OP_VALID CharacterClass *pClass)
{
	if (e && e->pChar)
	{
		if (e->pChar->pCombatReactivePowerInfo)
		{
			CombatReactivePowerDef *pDef = GET_REF(e->pChar->pCombatReactivePowerInfo->hCombatBlockDef);
			if (pDef->pRoll)
				return pDef->pRoll;
		}

		if (!pClass)
		{
			pClass = character_GetClassCurrent(e->pChar);
		}
	}
	

	if (pClass && pClass->pTacticalRollDef)
		return pClass->pTacticalRollDef;
	
	return &g_CombatConfig.tactical.roll.rollDef;
}

TacticalRequesterSprintDef* mrRequesterDef_GetSprintDefForEntity(SA_PARAM_OP_VALID Entity *e, SA_PARAM_OP_VALID CharacterClass *pClass)
{
	if (!pClass && e && e->pChar)
	{
		pClass = character_GetClassCurrent(e->pChar);
	}

	if (pClass && pClass->pTacticalSprintDef)
		return pClass->pTacticalSprintDef;

	return &g_CombatConfig.tactical.sprint.sprintDef;
}

TacticalRequesterAimDef* mrRequesterDef_GetAimDefForEntity(SA_PARAM_OP_VALID Entity *e, SA_PARAM_OP_VALID CharacterClass *pClass)
{
	if (!pClass && e && e->pChar)
	{
		pClass = character_GetClassCurrent(e->pChar);
	}

	if (pClass && pClass->pTacticalAimDef)
		return pClass->pTacticalAimDef;

	return &g_CombatConfig.tactical.aim.aimDef;
}

#include "AutoGen/EntityMovementRequesterDefs_h_ast.c"
