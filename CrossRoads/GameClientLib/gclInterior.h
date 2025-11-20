/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM
typedef struct InteriorDef InteriorDef;
typedef struct MicroTransactionUIProduct MicroTransactionUIProduct;

AUTO_STRUCT;
typedef struct InteriorSelectRow
{
	STRING_POOLED displayNameMsg;		AST(POOL_STRING)
	STRING_POOLED interiorName;			AST(POOL_STRING)
	REF_TO(InteriorDef) hInterior;		AST(REFDICT(InteriorDef))
	bool unlocked;

	// Microtransacted interiors
	U32 MicroTransactionID;

	// The prebuilt UI product information structure for the default MicroTransactionID
	MicroTransactionUIProduct *pProduct;

	// The full list of products that grants the unlock
	MicroTransactionUIProduct **eaFullProductList;
	S64 iMinimumProductPrice;						AST(NAME(MinimumProductPrice))
	S64 iMaximumProductPrice;						AST(NAME(MaximumProductPrice))
} InteriorSelectRow;

AUTO_STRUCT;
typedef struct InteriorGuestRow
{
	STRING_MODIFIABLE guestName;
	EntityRef guestRef;
} InteriorGuestRow;

AUTO_STRUCT;
typedef struct InteriorChoiceRow
{
	STRING_POOLED displayMessageName;	AST(POOL_STRING)
	STRING_MODIFIABLE displayMessageText;
	STRING_POOLED name;					AST(POOL_STRING)
	U32 value;

	U32 isChosen : 1;
		//True/false if this option is currently chosen
} InteriorChoiceRow;

AUTO_STRUCT;
typedef struct InteriorSettingRow
{
	STRING_POOLED name;					AST(POOL_STRING)
	STRING_MODIFIABLE displayName;

	U32 isChosen : 1;
		//True/false if this option is currently chosen
} InteriorSettingRow;

AUTO_STRUCT;
typedef struct InteriorSettingRefRow
{
	STRING_POOLED name;					AST(POOL_STRING)
	STRING_MODIFIABLE displayName;

	// Microtransacted interiors
	U32 MicroTransactionID;

	// The prebuilt UI product information structure for the default MicroTransactionID
	MicroTransactionUIProduct *pProduct;

	U32 isOwned : 1;

} InteriorSettingRefRow;

void gclInterior_GameplayLeave(void);
void gclInterior_GameplayEnter(void);