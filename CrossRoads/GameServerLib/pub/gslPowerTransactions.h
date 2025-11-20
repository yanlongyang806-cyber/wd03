/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef GSL_POWER_TRANSACTIONS_H__
#define GSL_POWER_TRANSACTIONS_H__

// Forward Declarations
typedef struct Character			Character;
typedef struct Entity				Entity;
typedef struct NOCONST(Entity)		NOCONST(Entity);
typedef struct Power				Power;
typedef struct PowerDef				PowerDef;
typedef struct PowerTreeSteps		PowerTreeSteps;

typedef enum enumTransactionOutcome enumTransactionOutcome;


// Wrapper for trCharacter_AddPowerPersonal transaction
SA_ORET_OP_VALID Power *character_AddPowerPersonal(int iPartitionIdx,
											   SA_PARAM_NN_VALID Character *pchar,
											   SA_PARAM_NN_VALID PowerDef *pdef,
											   int iLevel,
											   int bAllowDuplicates,
											   GameAccountDataExtract *pExtract);

// The actual transaction so I can call in another transaction.
enumTransactionOutcome trCharacter_AddPowerPersonal(ATR_ARGS,
													NOCONST(Entity)* ent,
													PowerDef *pdef,
													int iLevel,
													int bAllowDuplicates);

// Wrapper for trCharacter_RemovePowerPersonal transaction
int character_RemovePowerPersonal(SA_PARAM_NN_VALID Character *pchar, U32 uiID);

// A callback that is executed when a temporary Power has finished being created.  Includes
//  the Power created and the user data.  The Power may be NULL if it was not properly created.
//  Should return true if the Power was successfully put into use.  If it returns false the
//  Power will be automatically destroyed.
typedef int (*AddPowerTemporaryCallback)(Power *ppow, void *pvUserData);

// Wrapper for all the stuff that needs to be done to add a non-transacted
//  temporary Power to the Character.  Even though the Power is not transacted,
//  this still triggers a transaction on the Character.  This does not add the
//  Power to the Character's general list, it is up to the caller to do so in
//  the callback.
void character_AddPowerTemporary(SA_PARAM_NN_VALID Character *pchar,
								 SA_PARAM_NN_VALID PowerDef *pdef,
								 AddPowerTemporaryCallback callback,
								 SA_PARAM_NN_VALID void *pvUserData);

//Removes a power from the characters temporary powers array.  NOTE: Does not work on personal powers, grant power, be critter etc!
int character_RemovePowerTemporary(SA_PARAM_NN_VALID Character *pchar, U32 uiID);

// Adds or removes the temporary powers a character gets for teaming with a recruit or recruiter.
void character_RecruitingUpdatePowers(SA_PARAM_NN_VALID Character *pchar);

// Adds temporary powers to the character during post load.  Needs to launch transactions to reserve IDs
void character_LoadAddTemporaryPowers(SA_PARAM_NN_VALID Character *pchar);

// Wrapper for trCharacter_SetPowerHue
void character_SetPowerHue(SA_PARAM_NN_VALID Character *pchar, U32 uiID, F32 fHue);

// Wrapper for trCharacter_SetPowerEmit
void character_SetPowerEmit(SA_PARAM_NN_VALID Character *pchar, U32 uiID, SA_PARAM_OP_STR const char *cpchEmit);

// Wrapper for trCharacter_SetPowerEntCreateCostume
void character_SetPowerEntCreateCostume(SA_PARAM_NN_VALID Character *pchar, U32 uiID, S32 iEntCreateCostume);

// Wrapper for trCharacter_PowerUseCharge, safe to call on a Power that doesn't use charges
void character_PowerUseCharge(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow);

// Wrapper for various systems to handle a Power expiring, which will likely involve a transaction.
//  Does NOT check to see if the Power has actually expired.
void character_PowerExpire(SA_PARAM_NN_VALID Character *pchar, U32 uiID, GameAccountDataExtract *pExtract);

// Called when the Entity's Character's Class has changed
void character_SetClassCallback(SA_PARAM_OP_VALID Entity *pent, GameAccountDataExtract *pExtract);

// Transaction to set the CharacterClass of the Entity's Character, optional flag to propagate the change to the build
enumTransactionOutcome trCharacter_SetClass(ATR_ARGS, NOCONST(Entity)* e, const char *cpchClass, U32 bBuild);

// Wrapper for trCharacter_SetClass, makes sure it's an actual change and the new class is valid
void character_SetClass(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_STR const char *cpchClass);

#endif
