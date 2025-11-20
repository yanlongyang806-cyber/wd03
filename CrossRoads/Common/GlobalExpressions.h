/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"

typedef struct Expression Expression;

AUTO_STRUCT;
typedef struct GlobalExpressions
{
	Expression *pExprItemEPValue;			AST(LATEBIND NAME(ItemEPValue))
		// This is the value of an item in economy points
	Expression *pExprStoreEPConversion;		AST(LATEBIND NAME(StoreEPConversion))
	// This converts an item's actual EP value to it's sell value at a given store
	Expression *pInteractGlowDistance;		AST(LATEBIND NAME(InteractGlowDistance))
	// Defines an expression to determine how far away interactables should start glowing.
} GlobalExpressions;

extern GlobalExpressions g_GlobalExpressions;
