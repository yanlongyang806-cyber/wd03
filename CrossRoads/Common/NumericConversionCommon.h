/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "ReferenceSystem.h"


typedef struct ItemDef ItemDef;
typedef U32 ContainerID;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct NOCONST(Entity) NOCONST(Entity);

AUTO_STRUCT;
typedef struct NumericConversionBonusDef
{
	S32 bonusQuantity;
	const char *GamePermissionValue; AST(NAME(RequiredPermission)) 
}NumericConversionBonusDef;

AUTO_STRUCT;
typedef struct NumericConversionDef
{
    // The logical name of this conversion type.
    const char *name;                       AST(STRUCTPARAM KEY POOL_STRING)

    // Filename
    const char* filename;			        AST(CURRENTFILE)

    // Which numeric to convert from.
    REF_TO(ItemDef) hSourceNumeric;

    // Which numeric to convert to.
    REF_TO(ItemDef) hDestNumeric;

    // Time time interval over which we limit quantity converted.  In seconds.
    U32 timeIntervalSeconds;

    // The quantity that may be converted per time interval.
    S32 quantityPerInterval;

	NumericConversionBonusDef **BonusDefs; AST(NAME(BonusDef))

	// The gamepermission token that is used to specify the number of automatic conversions.  
	// If not specified, then automatic conversion is disabled.
	const char * autoConversionMaxIntervalsToken;


} NumericConversionDef;

AUTO_STRUCT AST_CONTAINER;
typedef struct NumericConversionState
{
    // Which type of conversion this is the state for.
    CONST_REF_TO(NumericConversionDef) hNumericConversionDef;       AST(PERSIST KEY REFDICT(NumericConversionDef))

    // The time that the last conversion occurred.
    const U32 lastConversionTime;                                   AST(PERSIST)

    // The total quantity that was converted during the time interval of the last conversion.
    const S32 quantityConverted;                                    AST(PERSIST)

	// The time that the last auto-conversion occurred.
	const U32 lastAutoConversionTime;								AST(PERSIST)
} NumericConversionState;

NumericConversionDef* NumericConversion_DefFromName(const char* numericConversionName);
S32 NumericConversion_QuantityRemaining(Entity *pEnt, NumericConversionState * const * const * hConversionStates, NumericConversionDef *conversionDef);
S32	NumericConversion_trh_GetBonusQuantity(NOCONST(Entity) *pEnt, NumericConversionDef *conversionDef, GameAccountDataExtract* pExtract);
#define NumericConversion_GetBonusQuantity(pEnt,conversionDef,pExtract) NumericConversion_trh_GetBonusQuantity((NOCONST(Entity)*)pEnt,conversionDef, pExtract)