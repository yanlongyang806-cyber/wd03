#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// PowerModes are simple code- and data-defined named integers that can be 
//  placed temporarily on Characters.  Powers may allow activation based 
//  on the existence or absence of PowerModes on the source Character.  
//  PowerModes may also be checked in requires statements of various types.

#include "structDefines.h"	// For StaticDefineInt

// Forward declarations
typedef struct Character	Character;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PowerDef		PowerDef;


/***** ENUMS *****/

// Enum of code-defined modes.  Any additions to this need to be
//  manually added to PowerModeEnum. If the new mode is set by code, 
//  must go before kPowerMode_LAST_CODE_SET.
typedef enum PowerMode
{
	kPowerMode_Combat = 1,
		// Character is involved in combat

	kPowerMode_Holster,
		// Character has holstered their weapons

	kPowerMode_Riding,
		// Character is riding another Character

	kPowerMode_Mounted,
		// Character is being ridden by another Character

	kPowerMode_Crouch,
		// Character is crouching (stationary).  Pulled from movement manager.

	kPowerMode_CrouchMove,
		// Character is crouching while moving.  Pulled from movement manager.

	kPowerMode_Sprint,
		// Character is sprinting.  Pulled from movement manager.

	kPowerMode_Roll,
		// Character is rolling.  Pulled from movement manager.

	kPowerMode_CritterRunAwayHealing,
		// Character is a critter who is leashing or otherwise griefed

	kPowerMode_ThrottleReverse,
		// The movement throttle is less than 0

	kPowerMode_BattleForm,
		// Enabled when the Character is in BattleForm

	kPowerMode_Bloodied,
		// Character is Bloodied (as specified by fBloodiedThreshold)

	kPowerMode_Shooter,
		// Character is in Shooter mode (TPS/FPS controls)
		
	kPowerMode_LAST_CODE_SET,
		// Only PowerModes after this can be set by data

	// Data-set Code-Defined PowerModes here

	kPowerMode_FIRST_DATA_DEFINED,
		// Everything equal to or greater than this is a completely data-defined PowerMode

} PowerMode;

// StaticDefine that includes all modes, both code and data
extern StaticDefineInt PowerModeEnum[];

/***** END ENUMS *****/


// Structure used to load the names of the data-defined modes
AUTO_STRUCT;
typedef struct PowerModeNames
{
	char **ppchPowerMode; AST(NAME(PowerMode))
} PowerModeNames;

// Structure to hold an earray of PowerModes
AUTO_STRUCT;
typedef struct PowerModes
{
	int *piPowerModes;
} PowerModes;

// Save's the Character's current PowerModes into their PowerMode history
void character_SaveModes(SA_PARAM_NN_VALID Character *pchar);

// Returns true if the character's modes allow the def to be activated
int character_ModesAllowPowerDef(SA_PARAM_NN_VALID const Character *pchar,
								 SA_PARAM_NN_VALID const PowerDef *pdef);

// Handles the updates necessary when the Character's PowerModes change
void character_ModesChanged(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Returns true if the Character has the given PowerMode, otherwise returns false
int character_HasMode(SA_PARAM_OP_VALID const Character *pchar, PowerMode eMode);

// Returns true if the Character has a given personal PowerMode, with a specified target
int character_HasModePersonal(SA_PARAM_OP_VALID const Character *pchar, PowerMode eMode, SA_PARAM_OP_VALID const Character *pcharTarget);

// Returns true if the Character has a given personal PowerMode, with any target
int character_HasModePersonalAnyTarget(SA_PARAM_OP_VALID const Character *character, PowerMode iMode);

// Counts the number of times the character has the specified powermode against any target.
int character_CountModePersonalAnyTarget(const Character *character, PowerMode iMode);