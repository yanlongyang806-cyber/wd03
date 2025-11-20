/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerModes.h"

#include "Entity.h"
#include "error.h"
#include "EString.h"
#include "Expression.h"
#include "timing.h"

#include "Character.h"
#include "PowerActivation.h"
#include "CharacterAttribs.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
#endif

#include "AutoGen/PowerModes_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Context to track all data-defined modes
static DefineContext *s_pDefinePowerModes = NULL;

// StaticDefine that includes all modes, both code and data
StaticDefineInt PowerModeEnum[] =
{
	DEFINE_INT

	// Code defined modes
	{ "Combat", kPowerMode_Combat },
	{ "Holster", kPowerMode_Holster },
	{ "Riding", kPowerMode_Riding },
	{ "Mounted", kPowerMode_Mounted },
	{ "Crouch", kPowerMode_Crouch },
	{ "CrouchMove", kPowerMode_CrouchMove },
	{ "Sprint", kPowerMode_Sprint },
	{ "Roll", kPowerMode_Roll },
	{ "CritterRunAwayHealing", kPowerMode_CritterRunAwayHealing },
	{ "MovementThrottleReverse", kPowerMode_ThrottleReverse},
	{ "BattleForm", kPowerMode_BattleForm},
	{ "Bloodied", kPowerMode_Bloodied},
	{ "Shooter", kPowerMode_Shooter},

	// Data defined modes
	DEFINE_EMBEDDYNAMIC_INT(s_pDefinePowerModes)

	DEFINE_END
};


// Save's the Character's current PowerModes into their PowerMode history
void character_SaveModes(Character *pchar)
{
#define POWER_MODE_HISTORY_SIZE 5 // TODO: This really should be arbitrarily sized with timestamps, but a static will probably suffice

	// Most recent go in the back, oldest is at 0
	PowerModes* pPowerModes;
	if (eaSize(&pchar->ppPowerModeHistory) < POWER_MODE_HISTORY_SIZE)
		pPowerModes = StructCreate(parse_PowerModes);
	else
		pPowerModes = eaRemove(&pchar->ppPowerModeHistory, 0);

	eaiCopy(&pPowerModes->piPowerModes, &pchar->piPowerModes);
	eaPush(&pchar->ppPowerModeHistory, pPowerModes);
}

// Returns true if the character's modes allow the def to be activated
//  TODO(JW): Optimize: PowerMode array is now sorted, so this could be faster
int character_ModesAllowPowerDef(const Character *pchar,
								 const PowerDef *pdef)
{
	int i;

	// Check for the def's required modes
	for(i=eaiSize(&pdef->piPowerModesRequired)-1; i>=0; i--)
	{
		if(-1 == eaiFind(&pchar->piPowerModes,pdef->piPowerModesRequired[i]))
		{
			return false;
		}
	}

	// Check for the def's disallowed modes
	for(i=eaiSize(&pdef->piPowerModesDisallowed)-1; i>=0; i--)
	{
		if(-1 != eaiFind(&pchar->piPowerModes,pdef->piPowerModesDisallowed[i]))
		{
			return false;
		}
	}
	
	return true;
}

// Handles the updates necessary when the Character's PowerModes change
void character_ModesChanged(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PERFINFO_AUTO_START_FUNC();

	entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,false);

	// Refresh Passives and Toggles, which will update them based on modes and other checks
	character_RefreshPassives(iPartitionIdx,pchar,pExtract);
	character_RefreshToggles(iPartitionIdx,pchar,pExtract);
	
	// Check if the new modes allow the current activation.  If not, try to deactivate it.
	if(pchar->pPowActCurrent)
	{
		PowerDef *pdef = GET_REF(pchar->pPowActCurrent->ref.hdef);
		if(pdef && !character_ModesAllowPowerDef(pchar,pdef))
		{
			U8 uchID = pchar->pPowActCurrent->uchID;
			ChargeMode eMode = pchar->eChargeMode;
			character_ActDeactivate(iPartitionIdx,pchar,&uchID,&eMode,pmTimestamp(0),0,true);
		}
	}

	PERFINFO_AUTO_STOP();
#endif
}

// Returns true if the Character has the given PowerMode, otherwise returns false
int character_HasMode(const Character *pchar, PowerMode eMode)
{
	int bRet=false;
	if(pchar)
		bRet = eaiFind(&pchar->piPowerModes,eMode) >= 0; // Since this is sorted we could do a faster search
	return bRet;
}

// Returns true if the Character has a given personal PowerMode, with a specified target
int character_HasModePersonal(const Character *character, PowerMode iMode, const Character *characterTarget)
{
	if(character && characterTarget)
	{
		EntityRef eRef = entGetRef(characterTarget->pEntParent);		

		int i;
		if (character_HasMode(character, iMode))
		{
			return true;
		}

		for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
		{
			AttribModDef *pmoddef = mod_GetDef(character->modArray.ppMods[i]);
			if(pmoddef && pmoddef->offAttrib==kAttribType_PowerMode)
			{
				PowerModeParams *pParams = (PowerModeParams *)pmoddef->pParams;

				if(pParams && pParams->iPowerMode == iMode && character->modArray.ppMods[i]->erPersonal == eRef)
				{
					return true;
				}
			}			
		}
	}
	return false;
}


// Returns true if the Character has a given personal PowerMode, with a any target
int character_HasModePersonalAnyTarget(const Character *character, PowerMode iMode)
{
	if(character)
	{		
		int i;
		if (character_HasMode(character, iMode))
		{
			return true;
		}

		for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
		{
			AttribModDef *pmoddef = mod_GetDef(character->modArray.ppMods[i]);
			if(pmoddef && pmoddef->offAttrib==kAttribType_PowerMode)
			{
				PowerModeParams *pParams = (PowerModeParams *)pmoddef->pParams;

				if(pParams && pParams->iPowerMode == iMode)
				{
					return true;
				}
			}			
		}
	}
	return false;
}


// Counts the number of times the character has the specified powermode against any target.
int character_CountModePersonalAnyTarget(const Character *character, PowerMode iMode)
{
	int retVal = 0;
	if(character)
	{
		int i;
		for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
		{
			AttribModDef *pmoddef = mod_GetDef(character->modArray.ppMods[i]);
			if(pmoddef && pmoddef->offAttrib==kAttribType_PowerMode)
			{
				PowerModeParams *pParams = (PowerModeParams *)pmoddef->pParams;

				if(pParams && pParams->iPowerMode == iMode)
				{
					retVal++;
				}
			}			
		}
	}
	return retVal;
}




// Load data-defined PowerModes
//  No dependencies
AUTO_STARTUP(PowerModes);
void PowerModesLoad(void)
{
	int i,s;
	char *pchTemp = NULL;
	PowerModeNames names = {0};

	estrStackCreateSize(&pchTemp,20);

	loadstart_printf("Loading PowerModes...");
	ParserLoadFiles(NULL,"defs/config/PowerModes.def","PowerModes.bin",PARSER_OPTIONALFLAG,parse_PowerModeNames,&names);
	s_pDefinePowerModes = DefineCreate();
	s = eaSize(&names.ppchPowerMode);
	for(i=0;i<s;i++)
	{
		if(-1!=StaticDefineIntGetInt(PowerModeEnum,names.ppchPowerMode[i]))
		{
			ErrorFilenamef("defs/config/PowerModes.def","PowerMode %s already exists",names.ppchPowerMode[i]);
			continue;
		}
		estrPrintf(&pchTemp,"%d",i+kPowerMode_FIRST_DATA_DEFINED);
		DefineAdd(s_pDefinePowerModes,names.ppchPowerMode[i],pchTemp);
	}
	loadend_printf(" done (%d PowerModes).",s);
	
	estrDestroy(&pchTemp);
}



// this gets called enough I moved it here for performance


// Returns true if the current entity has a particular (powers) mode on it and it was placed on that
// entity in the past <time> seconds
AUTO_EXPR_FUNC(ai) ACMD_NAME(CheckMode);
int exprFuncCheckMode(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENUM(PowerMode) const char* modeName, F32 time)
{
	if(!modeName[0])
		return 0;

	return character_HasMode(be->pChar,StaticDefineIntGetInt(PowerModeEnum,modeName));
}

#include "AutoGen/PowerModes_h_ast.c"
