#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

#include "PowersEnums.h"

typedef struct MovementManager		MovementManager;
typedef struct MovementRequester	MovementRequester;
typedef struct TacticalRequesterRollDef		TacticalRequesterRollDef;
typedef struct TacticalRequesterAimDef		TacticalRequesterAimDef;


S32		mrTacticalCreate(	MovementManager* mm,
							MovementRequester** mrOut);

S32		mrTacticalSetGlobalCooldown(	MovementRequester* mr,
										F32 cooldownSeconds);

S32		mrTacticalSetRollParams(MovementRequester* mr,
								const TacticalRequesterRollDef *pDef,
								S32 bRollIgnoresGlobalCooldown,
								S32 bRollOnDoubleTap);

S32		mrTacticalSetRunParams(	MovementRequester* mr,
								S32 enabled,
								F32 runSpeed,
								F32 maxRunDurationSeconds,
								F32 cooldownSeconds,
								bool bAutoSprint,
								bool bSprintToggles);

S32		mrTacticalSetRunFuel(	MovementRequester* mr,
								S32 enabled,
								F32 refillRate,
								F32 refillDelay);

S32		mrTacticalSetAimParams(	MovementRequester* mr,
								const TacticalRequesterAimDef *pDef,
								F32 aimSpeed,
								S32 runPlusCrouchDoesRoll,
								S32 rollWhileAiming,
								S32 bAimIgnoresGlobalCooldown,
								bool bAimStrafes, 
								bool bAimDisablesJump);

S32		mrTacticalSetCrouchParams(MovementRequester* mr,
								  F32 crouchSpeed,
								  S32 runPlusCrouchDoesRoll);

S32		mrTacticalSetRunMode(	MovementRequester* mr,
								S32 doRunning);

S32		mrTacticalSetAimMode(	MovementRequester* mr,
								S32 doAim);

S32		mrTacticalSetCrouchMode(MovementRequester* mr,
								S32 doCrouch);

S32		mrTacticalPerformRoll(	MovementRequester* mr,
								F32 rollYawDir,
								U32 spc);

S32		mrTacticalGetAimState(	MovementRequester* mr,
								S32* isAimingOut,
								S32* aimWhenAvailableOut);

S32		mrTacticalGetCrouchState(MovementRequester* mr,
								 S32* isCrouchingOut,
								 S32* crouchWhenAvailableOut);
								
S32		mrTacticalSetRollOnDoubleTap(	MovementRequester* mr,
										S32 enabled);

S32		mrTacticalIgnoreInput( MovementRequester *mr, S32 bIgnoreInput);

#define IS_SPECIAL_TACTICAL_DISABLE_ID(x)	(((x)&0x40000000) != 0)
#define TACTICAL_DISABLE_UID		0x4000DEAD
#define TACTICAL_COSTDISABLE_UID	0x4000C057
#define TACTICAL_KNOCK_UID			0x40001337
#define TACTICAL_OVERRIDEFSM_UID	0x4000BEEF
#define TACTICAL_HELD_UID			0x4000F00D
#define TACTICAL_ROOT_UID			0x4000FADE
#define TACTICAL_NEARDEATH_UID		0x40000BED
#define TACTICAL_REQUIREMENTS_UID	0x40002EAD

// Disables tactical movement once the character
// gets into combat by dealing or taking damage
// and enables it once the character goes out of
// combat
#define TACTICAL_COMBATDISABLE_UID	0x4000FACE

S32		mrTacticalNotifyPowersStart(MovementRequester* mr,
									U32 id,
									TacticalDisableFlags flags,
									U32 spc);

S32		mrTacticalNotifyPowersStop(MovementRequester* mr,
								   U32 id,
								   U32 spc);

// checks for special tactical disable flags only, returns true if there is an active disable 
// (disregards server process count so there could potentially be prediction issues.)
S32		mrTacticalHasDisableID(	MovementRequester* mr,
								U32 id);

S32		mrTacticalNotifyClearAllDisables(	MovementRequester* mr,
											U32 spc);