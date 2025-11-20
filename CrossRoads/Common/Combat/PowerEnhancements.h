#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// Forward declarations

typedef struct	AttribMod		AttribMod;
typedef struct	Character		Character;
typedef struct	Entity			Entity;
typedef struct	Power			Power;
typedef struct	PowerDef		PowerDef;
typedef struct  PowerActivation PowerActivation;
typedef struct  GameAccountDataExtract GameAccountDataExtract;

// Generates the power's internal list of enhancements.  Automatically
//  updates the lists of all attached powers.  Should be called
//  whenever a power is added to a character.
void power_AttachEnhancements(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow);

// Removes the power from all attached powers.  Should be called
//  whenever a power is removed from a character.
void power_DetachEnhancements(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow);

// Uses the power's internal list of enhancements to make an earray of
//  enhancing powers (or powers it is enhancing).
void power_GetEnhancements(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow, SA_PARAM_NN_VALID Power ***pppAttachedOut);

// Removes all enhancements from the earray that are not legal to apply.  Requires
//  that the combat eval Apply context is already set up.
void power_CheckEnhancements(int iPartitionIdx, SA_PARAM_NN_VALID Power ***pppListOut);

// Filters the list of Local Enhancements to ones that can actually attach, and put them into the
//  attach list.
void power_AttachEnhancementsLocal(int iPartitionIdx, SA_PARAM_NN_VALID Power *ppow, SA_PARAM_OP_VALID Character *pchar, SA_PARAM_OP_VALID Power **ppEnhancementsLocal, SA_PARAM_NN_VALID Power ***pppAttachedOut);

// evaluates the power's pExprEnhanceAttach on the entity 
int power_EnhancementAttachToEntCreateAllowed(	int iPartitionIdx, 
												SA_PARAM_NN_VALID Character *pOwnerChar, 
												SA_PARAM_NN_VALID PowerDef *pdefEnhancement, 
												SA_PARAM_NN_VALID Entity *pEntCreate);

// bIsUnownedPower means the power isn't coming from an owned power- it's most likely an applyPower, expiration def, trigger, etc.
int power_EnhancementAttachIsAllowed(	int iPartitionIdx, 
										SA_PARAM_OP_VALID Character *pChar, 
										SA_PARAM_NN_VALID PowerDef *pdefEnhancement, 
										SA_PARAM_NN_VALID PowerDef *pdefTarget,
										int bIsUnownedPower);

// returns a list of enhancement powers that apply to the given PowerDef in pppAttachedOut
// This is fairly exhaustive as it goes through every enhancement power on the character to see if should attach. 
void power_GetEnhancementsForUnownedPower(int iPartitionIdx, 
											SA_PARAM_NN_VALID Character *pChar, 
											SA_PARAM_NN_VALID PowerDef *pPowDef, 
											SA_PARAM_NN_VALID Power ***pppAttachedOut);

// returns a list of enhancement powers for the given power's attribute
// if bAttribExpiration is set will get the enhancement for the attributes ModExpiration
void power_GetEnhancementsForAttribModApplyPower(int iPartitionIdx, 
													SA_PARAM_NN_VALID Character *pChar, 
													SA_PARAM_NN_VALID AttribMod *pMod, 
													S32 eEnhancementList,  // EEnhancedAttribList
													SA_PARAM_NN_VALID PowerDef *pDef, 
													SA_PARAM_NN_VALID Power ***peaAttachedOut);


void power_CalculateAttachEnhancementPowerFields(int iPartitionIdx, Character *pchar);
