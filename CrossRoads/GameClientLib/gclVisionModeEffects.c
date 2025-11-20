#include "gclVisionModeEffects.h"
#include "gclVisionModeEffects_h_ast.h"
#include "StashTable.h"
#include "gfxSettings.h"
#include "PowerModes.h"
#include "gclEntity.h"
#include "entCritter.h"
#include "dynFxInterface.h"
#include "dynFxInfo.h"
#include "StringCache.h"

typedef struct VisionModeEffectsState
{
	S32			bUpdateEntities;
	S32			bInReconMode;

	F32			fTargetSaturation;
	F32			fSaturationSpeed;
	bool		bInterpSaturation;
} VisionModeEffectsState;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static VisionModeEffectsDef s_playerReconEffectsDef = {0};
static VisionModeEffectsState s_playerReconEffectsState = {0};

// ----------------------------------------------------------------------------------------
__forceinline static S32 gclVisionModeEffectsDef_HasValidDef()
{
	return s_playerReconEffectsDef.iPlayerMode != -1;
}

// ----------------------------------------------------------------------------------------
static void gclVisionModeEffectsDef_Fixup(VisionModeEffectsDef *pDef)
{
	if (eaSize(&pDef->eaEntityEffects) == 0)
	{
		pDef->iPlayerMode = -1;
		return;
	}

	pDef->perceptEffectMap = stashTableCreateAddress(8);
	
	if (pDef->perceptEffectMap)
	{
		FOR_EACH_IN_EARRAY(pDef->eaEntityEffects, VisionModeEffectDef, p)
		{
			if (p->pchCritterRank)
				stashAddressAddPointer(pDef->perceptEffectMap, p->pchCritterRank, p, false);
		}
		FOR_EACH_END
	}

	if (pDef->pchPlayerMode)
	{
		pDef->iPlayerMode = StaticDefineIntGetInt(PowerModeEnum, pDef->pchPlayerMode);
	}
	else
	{
		pDef->iPlayerMode = -1;
	}
}


AUTO_STARTUP(VisionModeEffects);
void gclVisionModeEffects_Startup(void)
{
	ParserLoadFiles(NULL,"defs/visionModeEffects.def","visionModeEffects.bin",PARSER_OPTIONALFLAG,parse_VisionModeEffectsDef,&s_playerReconEffectsDef);

	gclVisionModeEffectsDef_Fixup(&s_playerReconEffectsDef);
}

// todo:
// handle enter/exit map

// ----------------------------------------------------------------------------------------
static void gclVisionModeEffects_SetHasPercetion(S32 hasPerception)
{
	if (s_playerReconEffectsState.bInReconMode == hasPerception)
		return;

	s_playerReconEffectsState.bInReconMode = hasPerception;
	s_playerReconEffectsState.bUpdateEntities = true;


	if (s_playerReconEffectsDef.bDoDesaturationEffect)
	{
		s_playerReconEffectsState.fSaturationSpeed = 0.f;

		if (s_playerReconEffectsState.bInReconMode)
		{
			s_playerReconEffectsState.fTargetSaturation = 1.f;

			if (s_playerReconEffectsDef.fDesaturationBlendTimeIn)
				s_playerReconEffectsState.fSaturationSpeed = 1.f / s_playerReconEffectsDef.fDesaturationBlendTimeIn;
		}
		else
		{
			s_playerReconEffectsState.fTargetSaturation = 0.f;

			if (s_playerReconEffectsDef.fDesaturationBlendTimeOut)
				s_playerReconEffectsState.fSaturationSpeed = 1.f / s_playerReconEffectsDef.fDesaturationBlendTimeOut;
		}


		if (s_playerReconEffectsState.fSaturationSpeed == 0.f)
		{
			s_playerReconEffectsState.bInterpSaturation = false;
			gfxSettingsSetSpecialMaterialParam(s_playerReconEffectsState.fTargetSaturation);
		}
		else
		{
			s_playerReconEffectsState.bInterpSaturation = true;
		}
	}

}

// ----------------------------------------------------------------------------------------
void gclVisionModeEffects_OncePerFrame(F32 fDTime)
{
	Entity *playerEnt;
	S32 hasPerception;

	if (!gclVisionModeEffectsDef_HasValidDef())
		return;
	
	if (s_playerReconEffectsState.bInterpSaturation)
	{
		F32 curDesat = gfxSettingsGetSpecialMaterialParam();

		if (curDesat > s_playerReconEffectsState.fTargetSaturation)
		{
			curDesat -= fDTime * s_playerReconEffectsState.fSaturationSpeed;
			if (curDesat <= s_playerReconEffectsState.fTargetSaturation)
			{
				curDesat = s_playerReconEffectsState.fTargetSaturation;
				s_playerReconEffectsState.bInterpSaturation = false;
			}
		}
		else
		{
			curDesat += fDTime * s_playerReconEffectsState.fSaturationSpeed;
			if (curDesat >= s_playerReconEffectsState.fTargetSaturation)
			{
				curDesat = s_playerReconEffectsState.fTargetSaturation;
				s_playerReconEffectsState.bInterpSaturation = false;
			}
		}

		gfxSettingsSetSpecialMaterialParam(curDesat);
	}
	
	// get the active player and see if it has the mode that activates the perception
	hasPerception = false;
	playerEnt = entActivePlayerPtr();
	if (playerEnt && playerEnt->pChar)
	{
		hasPerception = character_HasMode(playerEnt->pChar, s_playerReconEffectsDef.iPlayerMode);
	}
	
	s_playerReconEffectsState.bUpdateEntities = false;

	gclVisionModeEffects_SetHasPercetion (hasPerception);
}

// ----------------------------------------------------------------------------------------
__forceinline static  VisionModeEffectDef *_getReconEffectDefForEntity(Entity *e)
{
	VisionModeEffectDef *pPerceptEffectDef = NULL;
	if (e->pCritter)
	{
		stashAddressFindPointer(s_playerReconEffectsDef.perceptEffectMap, e->pCritter->pcRank, &pPerceptEffectDef);
	}

	return pPerceptEffectDef;
}


// ----------------------------------------------------------------------------------------
void gclVisionModeEffects_InitEntity(Entity *e)
{
	if (!gclVisionModeEffectsDef_HasValidDef())
		return;

	if (s_playerReconEffectsState.bInReconMode)
	{
		VisionModeEffectDef *pPerceptEffectDef = _getReconEffectDefForEntity(e);

		if (pPerceptEffectDef && pPerceptEffectDef->pchEffect)
			dtFxManAddMaintainedFx(e->dyn.guidFxMan, pPerceptEffectDef->pchEffect, NULL, 0, 0, eDynFxSource_HardCoded);
	}
}

// ----------------------------------------------------------------------------------------
void gclVisionModeEffects_UpdateEntity(Entity *e)
{
	VisionModeEffectDef *pPerceptEffectDef = NULL;
	
	if (!gclVisionModeEffectsDef_HasValidDef())
		return;

	if (!s_playerReconEffectsState.bUpdateEntities)
	{
		if (s_playerReconEffectsState.bInReconMode && s_playerReconEffectsDef.pchDeathEffectMessage &&
			!entIsAlive(e) && !e->bVisionEffectDeath)
		{
			pPerceptEffectDef = _getReconEffectDefForEntity(e);
			if (pPerceptEffectDef)
			{
				DynFxMessage message = {0};
								
				message.eSendTo = emtSelf;
				message.pcMessageType = s_playerReconEffectsDef.pchDeathEffectMessage;

				if (message.pcMessageType)
				{
					DynFxMessage **eaMessage = NULL;
					eaStackCreate(&eaMessage, 1);
					eaPush(&eaMessage, &message);
					dtFxManSendMessageMaintainedFx(e->dyn.guidFxMan, pPerceptEffectDef->pchEffect, eaMessage);
				}
				
				e->bVisionEffectDeath = true;
			}
		}
		return;
	}
	
	pPerceptEffectDef = _getReconEffectDefForEntity(e);

	if (pPerceptEffectDef && pPerceptEffectDef->pchEffect)
	{
		if (s_playerReconEffectsState.bInReconMode)
		{
			if (entIsAlive(e))
				dtFxManAddMaintainedFx(e->dyn.guidFxMan, pPerceptEffectDef->pchEffect, NULL, 0, 0, eDynFxSource_HardCoded);
		}
		else
		{
			dtFxManRemoveMaintainedFx(e->dyn.guidFxMan, pPerceptEffectDef->pchEffect);
		}
	}
}


#include "gclVisionModeEffects_h_ast.c"
