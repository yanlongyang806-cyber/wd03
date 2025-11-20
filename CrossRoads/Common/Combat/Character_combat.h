/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef CHARACTER_COMBAT_H__
#define CHARACTER_COMBAT_H__
GCC_SYSTEM

#include "PowersEnums.h" // For enums
#include "referencesystem.h"

// Forward declarations
typedef struct AttribMod			AttribMod;
typedef struct Character			Character;
typedef struct CharacterAttribs		CharacterAttribs;
typedef struct CharacterClass		CharacterClass;
typedef struct Entity				Entity;
typedef struct ExprContext			ExprContext;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct Item					Item;
typedef struct PACritical			PACritical;
typedef struct PATrigger			PATrigger;
typedef struct Power				Power;
typedef struct PowerActivation		PowerActivation;
typedef struct PowerAnimFX			PowerAnimFX;
typedef struct PowerApplication		PowerApplication;
typedef struct PowerDef				PowerDef;
typedef struct PowerApplyStrength PowerApplyStrength;
typedef struct PowerActivationRequest	PowerActivationRequest;
typedef struct PowerSubtargetChoice	PowerSubtargetChoice;
typedef struct PTNode				PTNode;
typedef struct WorldVolume			WorldVolume;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionEntry WorldInteractionEntry;

// Redefined for the ApplyPower call
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere

extern S32 g_bCombatPredictionDisabled;

// Bitmask for destructible and throwable objects for wlQueries
extern S32 g_iDestructibleThrowableMask;

// Checks if the source is allowed to affect the target (e.g. OnlyAffectSelf and Safe flags)
S32 entity_CanAffect(int iPartitionIdx, SA_PARAM_OP_VALID Entity *pentSource, SA_PARAM_OP_VALID Entity *pentTarget);

// Returns true if the PowerApplication/PowerActivation will hit the target Character.
//  Because this has to work with either an Application or Activation, it has to take
//  a bunch of extra parameters.
S32 combat_HitTest(	int iPartitionIdx,
					SA_PARAM_OP_VALID PowerApplication *papp,
					SA_PARAM_OP_VALID PowerActivation *pact,
					SA_PARAM_OP_VALID Character *pcharSource,
					SA_PARAM_NN_VALID Character *pcharTarget,
					SA_PARAM_OP_VALID Power *ppow,
					bool bEvalHitChanceWithoutPower,
					SA_PARAM_OP_VALID F32 *pfHitChanceOverflow);

// Causes the character to apply all powers from the item. This treats the powers as unowned.
void character_ApplyUnownedPowersFromItem(int iPartitionIdx,
									      SA_PARAM_NN_VALID Character *pchar, 
									      SA_PARAM_NN_VALID Item *pItem,
									      GameAccountDataExtract *pExtract);

// Causes the character to apply the given power at the given target
bool character_ApplyPower(int iPartitionIdx,
						  SA_PARAM_NN_VALID Character *pchar,
						  SA_PARAM_NN_VALID Power *ppow,
						  SA_PARAM_NN_VALID PowerActivation *pact,
						  EntityRef erTarget,
						  SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
						  SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetSecondary,
						  S32 bPlayActivate,
						  S32 iFramesBeforeHitAdjust,
						  GameAccountDataExtract *pExtract,
						  SA_PARAM_OP_VALID ExprContext **ppOutContext);

// Causes an application of the given power at the given target, from a location
void location_ApplyPowerDef(SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecSource,
							int iPartitionIdx,
							SA_PARAM_NN_VALID PowerDef *pdef,
							EntityRef erTarget,
							SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
							SA_PARAM_OP_VALID WorldVolume*** pppvolTarget,
							SA_PARAM_OP_VALID Character *pcharSourceTargetType,
							SA_PARAM_OP_VALID CharacterClass *pclass,
							int iLevel,
							EntityRef erModOwner);

typedef struct ApplyUnownedPowerDefParams
{
	
	AttribMod				*pmod;					// SA_PARAM_OP_VALID 
	Power					**pppowEnhancements;	// SA_PARAM_OP_VALID 
	EntityRef				erTarget;
	F32						*pVecTarget;			// SA_PRE_OP_ELEMS
	PowerSubtargetChoice	*pSubtarget;			// SA_PARAM_OP_VALID 
	Character				*pcharSourceTargetType; // SA_PARAM_OP_VALID 
	CharacterClass			*pclass;				// SA_PARAM_OP_VALID 
	int						iLevel;
	int						iIdxMulti;
	F32						fTableScale;
	S64						iSrcItemID;
	S32						bLevelAdjusting;
	S32						bEvalHitChanceWithoutPower;
	PowerApplyStrength		**ppStrengths;			// SA_PARAM_OP_VALID 
	PACritical				*pCritical;				// SA_PARAM_OP_VALID 

	// optional. If not set then erModOwner will be derived from the casting character
	EntityRef				erModOwner;
	U32						uiApplyID;
	F32						fHue;
	PATrigger				*pTrigger;				// SA_PARAM_OP_VALID 
	GameAccountDataExtract	*pExtract;
	
	// optional. The chacater that is used to get the list of weapons to use for the power application
	// if not set, will use the Character passed into character_ApplyUnownedPowerDef
	Character				*pCharWeaponPicker;		// SA_PARAM_OP_VALID 

	// if set, then the mods will be counted as already applied for the resist/immunity/shields only affecting first tick 
	U32 bCountModsAsPostApplied : 1;

} ApplyUnownedPowerDefParams;


// Causes the character to apply the given power def to the target.  Requires a bunch of specific additional data.
void character_ApplyUnownedPowerDef(	int iPartitionIdx,
										SA_PARAM_NN_VALID Character *pchar,
										SA_PARAM_NN_VALID PowerDef *pdef,
										SA_PARAM_NN_VALID const ApplyUnownedPowerDefParams *pParams);

// Causes the Character to apply the given PowerDef to themselves with basic parameters.
void character_ApplyUnownedPowerDefToSelf(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef, GameAccountDataExtract *pExtract);

// Causes the Character to take appropriate falling damage based on impact speed
void character_ApplyFalling(SA_PARAM_NN_VALID Character *pchar, F32 fImpactSpeed);

// Attempts to activate the power based on the power activation request structure.  Called by the server.
S32 character_ActivatePowerServer(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerActivationRequest *pActReq, U32 bClient, GameAccountDataExtract *pExtract);

// Starts or stops the given power at the given target.  Makes a lot of assumptions, generally used for AI power activation
S32 character_ActivatePowerServerBasic(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow, SA_PARAM_OP_VALID Entity *eTarget, const Vec3 vecTarget, S32 bStart, S32 bCancelExisting, GameAccountDataExtract *pExtract);

// Starts the given power at the given target.  Makes a lot of assumptions, generally used for AI power activation
bool character_ActivatePowerByNameServerBasic(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_STR const char *pchName, SA_PARAM_OP_VALID Entity *eTarget, SA_PARAM_OP_VALID Vec3 vecTarget, bool bStart, GameAccountDataExtract *pExtract);


// Function to activate a power, usually called by the client
bool character_ActivatePowerByNameClient(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_STR const char *pchName, SA_PARAM_OP_VALID Entity *eTarget, SA_PARAM_OP_VALID Vec3 vecTargetSecondary, bool bStarting, GameAccountDataExtract *pExtract);

// Function to activate a power, usually called by the client
bool character_ActivatePowerByIDClient(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, U32 uiID, SA_PARAM_OP_VALID Entity *eTarget, SA_PARAM_OP_VALID Vec3 vecTargetSecondary, bool bStarting, GameAccountDataExtract *pExtract);

// Function to activate a power, usually called by the client. The power is executed on an arbitrary point
bool character_ActivatePowerByIDClientOnArbitraryPoint(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, U32 uiID, SA_PARAM_NN_VALID Vec3 vecTargetPrimary, SA_PARAM_OP_VALID Vec3 vecTargetSecondary, bool bStarting, GameAccountDataExtract *pExtract);

void character_UpdateTargetingClient(int iPartitionIdx, SA_PARAM_NN_VALID Character *ent, SA_PARAM_NN_VALID PowerDef *pdef, SA_PARAM_NN_VALID Power *ppow, SA_PARAM_NN_VALID Vec3 vecTargetOut, SA_PARAM_NN_VALID Entity **ppTarget);

// Places the character in combat for the given period of time
// bCombatEventActIn - if true will send a combat event kCombatEvent_CombatModeActIn, otherwise kCombatEvent_CombatModeActOut
void character_SetCombatExitTime(	SA_PARAM_NN_VALID Character *pChar, 
									F32 fTimeInCombat, 
									bool bSetActive, 
									bool bCombatEventActIn,
									SA_PARAM_OP_VALID Entity *pSourceEnt,
									SA_PARAM_OP_VALID PowerDef *pSourcePowerDef);

// Places the character in combat visual ONLY mode for the given period of time
void character_SetCombatVisualsExitTime(SA_PARAM_NN_VALID Character *pChar, F32 fTimeInCombat);

// Finds the first item to use as the basis of a PowerApplication for a particular PowerDef
SA_RET_OP_VALID Item * character_DDWeaponPickSlot(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef, GameAccountDataExtract *pExtract, int slot);

// Finds the Item to use as the basis of a PowerApplication for a particular PowerDef
void character_WeaponPick(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef, SA_PARAM_NN_VALID Item ***pppItems, GameAccountDataExtract *pExtract);

// Sets the first weapon based category in the power in estrCatNameOut. If there is no weapon based category, the string is not touched.
bool character_GetWeaponBasedCategoryFromPowerDef(SA_PARAM_NN_VALID PowerDef *pdef, SA_PARAM_NN_VALID char **estrCatNameOut);

bool character_HasAllRequiredItemsEquipped(Character *pchar, PowerDef *pdef, GameAccountDataExtract *pExtract, char const ** ppchMissingCategory);

// Resets cooldown timers if necessary when critter exits combat
void character_ResetCooldownTimersOnExitCombat(SA_PARAM_NN_VALID Character *pChar);

typedef struct WorldInteractionNode WorldInteractionNode;

// Tracking for a potential target
typedef struct CombatTarget
{
	Character *pChar;
	REF_TO(WorldInteractionNode) hObjectNode;
	Vec3 vecHitPos;
	U8 bIsMainTarget : 1;
	U8 bIsHardTarget : 1;
} CombatTarget;


#endif
