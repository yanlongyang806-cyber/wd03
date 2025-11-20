/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "Expression.h"
#include "gslInteractable.h"
#include "gslInterior.h"
#include "InteriorCommon.h"
#include "StringCache.h"

// Name of a player in the expression context.  Defined in mission_common.c
extern const char *g_PlayerVarName;

//
// Expression that designers can use to determine the name of the map that the current map owner
//  came from.  Used to determine which texture to use on the view screen in starship bridge interiors.
//
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(InteriorMapOwnerReturnMap);
const char *
exprInteriorMapOwnerReturnMap(ACMD_EXPR_PARTITION iPartitionIdx)
{
	return gslInterior_GetMapOwnerReturnMap(iPartitionIdx);
}

AUTO_EXPR_FUNC(player) ACMD_NAME(MapIsInterior);
bool
exprMapIsInterior(void)
{
	return InteriorCommon_IsCurrentMapInterior();
}

AUTO_EXPR_FUNC(player) ACMD_NAME(IsPlayerInteriorOwner);
bool
exprIsPlayerInteriorOwner(ExprContext* pContext)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if ( pEnt == NULL )
	{
		return false;
	}
	return gslInterior_IsCurrentMapPlayerCurrentInterior(pEnt);
}

AUTO_EXPR_FUNC(gameutil, mission) ACMD_NAME(InteriorMapGetOptionChoiceValue);
S32
exprInteriorMapGetOptionChoiceValue(ACMD_EXPR_PARTITION iPartitionIdx, const char *optionName)
{
	return gslInterior_GetMapOptionChoiceValue(iPartitionIdx, optionName);
}

AUTO_EXPR_FUNC(player) ACMD_NAME(IsInteriorOptionChoiceActiveByValue);
bool
exprIsInteriorOptionChoiceActiveByValue(ExprContext* pContext, const char *optionName, S32 value)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if ( pEnt == NULL )
	{
		return false;
	}
	return gslInterior_InteriorOptionChoiceActiveByValue(pEnt, optionName, value);
}

AUTO_EXPR_FUNC(player) ACMD_NAME(InteriorOptionChoiceSetByValue);
void
exprInteriorOptionChoiceSetByValue(ExprContext* pContext, const char *optionName, S32 value)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if ( pEnt != NULL )
	{
		gslInterior_InteriorOptionChoiceSetByValue(pEnt, optionName, value);
	}
}