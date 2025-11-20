/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef COMBATENUMS_H__
#define COMBATENUMS_H__

#include "structDefines.h"

extern DefineContext *g_ExtraCharClassTypeIDs;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_ExtraCharClassTypeIDs);
typedef enum CharClassTypes
{
	CharClassTypes_None = 0
	//None is assumed to be the last fixed bag ID by the dynamic enum loading code.
	//Add any new modes after it

} CharClassTypes;

extern StaticDefineInt CharClassTypesEnum[];

AUTO_ENUM;
typedef enum BolsterType
{
	kBolsterType_None,	EIGNORE
		//No bolster effect, same as setting iBolsterLevel to 0

	kBolsterType_SetTo,
		// Sets the level of the players to iBolsterLevel

	kBolsterType_RaiseTo,
		// Raises the level of the players to iBolsterLevel if their combat level is lower

	kBolsterType_LowerTo,
		// Lowers the level of the players on map to iBolsterLevel if their combat level is higher

	kBolsterType_Count,	EIGNORE
} BolsterType;

extern StaticDefineInt BolsterTypeEnum[];

AUTO_ENUM;
typedef enum TargetingAssist
{
	CombatTargetingAssist_UseCombatconfig = -1,	ENAMES(UseCombatconfig)//For the override in the current controlscheme.
	CombatTargetingAssist_Full,	ENAMES(Full)//Full targeting assist. Will pick any legal target on screen
	CombatTargetingAssist_MouseLook,	ENAMES(Mouselook)// Uses the mouse look targeting code with the targeting requirements of the selected power
	CombatTargetingAssist_Disabled,	ENAMES(Disabled)// Disables targeting assist, with an exception to auto selecting self as a target
}TargetingAssist;

extern StaticDefineInt TargetingAssistEnum[];


#endif