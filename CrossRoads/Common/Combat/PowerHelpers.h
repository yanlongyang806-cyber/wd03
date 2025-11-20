#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct NOCONST(Character)	NOCONST(Character);
typedef struct NOCONST(Power)			NOCONST(Power);

typedef struct PowerDef					PowerDef;
typedef struct GameAccountDataExtract	GameAccountDataExtract;

//////////////////////////////////////////////////////////////////////////
// These helper functions do the meat of the work the powers transactions do,
// but they operate on characters rather than entities. They are also in
// EntityLib so that the client can simulate the powers transactions during
// respecs / character creation. As such, they don't do much sanity checking;
// that should be done in the command/transaction calling them.



// Takes an Entity and a PowerDef, and creates a Power for that Entity to
//  use, without actually adding it to the Entity.  It essentially wraps
//  the init and id calls.  Returns the Power created.
SA_RET_NN_VALID NOCONST(Power) *entity_CreatePowerHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Entity) *pent,
													 SA_PARAM_NN_VALID PowerDef *pdef,
													 int iLevel);


// Takes a new power, initializes its transacted data with proper values, and does 
//  generally useful stuff.  Does NOT set the ID or add it to Character or anything
//  of that sort.  Return false if the Power already has already been initialized.
int power_InitHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Power) *ppow, int iLevel);

// Return the next valid Power ID, which is unique across the entity and any of its pets/siblings
U32 entity_GetNewPowerIDHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Entity) *pent);

// Gets the next PowerID for a Temporary Power.  None of this data is transacted.  Only use for
//  Powers that aren't transacted.  Values are totally transient and will change after a mapmove.
U32 character_GetNewTempPowerID(SA_PARAM_NN_VALID Character *pchar);

// Sets the Power's ID.  Totally bad things can happen if you call this at a bad time
//  or with a bad value (anything not from character_GetNewPowerIDHelper()).
void power_SetIDHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Power) *ppow, U32 uiID);

// Sets the Power's level
void power_SetLevelHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Power) *ppow, int iLevel);

// Sets the Power's hue
void power_SetHueHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Power) *ppow, F32 fHue);

// Sets the Power's emit.  If the passed in value is NULL or not a valid emit name, the emit is cleared.
void power_SetEmitHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Power) *ppow, SA_PARAM_OP_STR const char *cpchEmit);

// Sets the Power's EntCreateCostume
void power_SetEntCreateCostumeHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Power) *ppow, S32 iEntCreateCostume);

// Fixes up the list of Powers granted by the Entity's Character's Class
void entity_FixPowersClassHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Entity) *pent);

// Sets the Power's used charges
void power_SetChargesUsedHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Power) *ppow, int iChargesUsed);

// Resets the PowerIDs on the Powers on the Entity.  If uiSourceMask is (U32)-1 (or any value with
//  bits set in the base bits range), then all Powers have their PowerIDs reset.  If uiSourceMask
//  only contains bits in the source bits range (or is 0), then only Powers with matching source bits
//  in the PowerIDs are reset.
// Returns the number of Powers that had their PowerIDs reset.
S32 entity_ResetPowerIDsHelper(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U32 uiSourceMask, GameAccountDataExtract *pExtract, bool bJustCount);

// Resets the uiPowerIDMax and all PowerIDs on the Powers on the Entity.
// Returns the number of Powers that had their PowerIDs reset.
S32 entity_ResetPowerIDsAllHelper(ATR_ARGS, ATH_ARG SA_PARAM_NN_VALID NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract);