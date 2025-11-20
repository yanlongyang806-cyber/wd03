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

// Forward declarations
typedef struct Character			Character;
typedef struct CharacterAttribs		CharacterAttribs;
typedef struct Entity				Entity;
typedef struct DynParamBlock		DynParamBlock;
typedef struct MovementManager		MovementManager;
typedef struct MovementRequester	MovementRequester;

typedef enum PowerAnimFXType		PowerAnimFXType;
typedef enum PowerMoveType			PowerMoveType;

// Redefined for the FX calls
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere

// Defines for identifying various reasons movement can be changed.  IDs below 0-255 are reserved for
//  power activation IDs
#define PMOVE_NEARDEATH	(1 <<  8)
#define PMOVE_ROOT		(1 <<  9)
#define PMOVE_HOLD		(1 << 10)

#define PM_CREATE_SAFE(pchar) if(!(pchar)->pPowersMovement) powersMovementCreate(&(pchar)->pPowersMovement,(pchar->pEntParent)->mm.movement)

void	powersMovementCreate(	MovementRequester** mrOut,
								MovementManager* mm);

// Resets all arrays of movement data and errorfs with the data in the background
void pmReset(SA_PARAM_NN_VALID MovementRequester *mr);

// Creates an event for the given time, passes back out the id
void pmEventCreate(SA_PARAM_NN_VALID MovementRequester *mr,
				   U32 uiUserID,
				   U32 uiTime,
				   SA_PARAM_NN_VALID U32 *puiEventIDOut);

void pmBitsStartFlash(	SA_PARAM_NN_VALID MovementRequester* mr, 
						U32 id, U32 subid, PowerAnimFXType type, EntityRef source, 
						U32 time,
						const char **ppchBits, 
						bool trigger,
						bool triggerIsEntityID,
						bool triggerMultiHit,
						bool bIsKeyword,
						bool bIsFlag,
						bool bNeverCancel,
						bool bAssumeOwnership,
						bool bForceDetailFlag);

void pmBitsStartSticky(	SA_PARAM_NN_VALID MovementRequester* mr, 
						U32 id, U32 subid, PowerAnimFXType type, EntityRef source, 
						U32 time,
						const char **ppchBits, 
						bool trigger,
						bool triggerIsEntityID,
						bool triggerMultiHit);

void pmBitsStop(SA_PARAM_NN_VALID MovementRequester* mr, 
				U32 id, U32 subid, PowerAnimFXType type, EntityRef source, 
				U32 time,
				bool bKeep);

void pmBitsCancel(SA_PARAM_NN_VALID MovementRequester* mr, 
				  U32 id, U32 subid, PowerAnimFXType type, EntityRef source);


void pmReleaseAnim(	MovementRequester* mr,
					U32 spc,
					U32 id,
					const char* reason);

bool pmFxStart(SA_PARAM_NN_VALID MovementRequester* mr, 
			   U32 id, U32 subid, PowerAnimFXType type, EntityRef source, EntityRef target,
			   U32 time, 
			   SA_PARAM_NN_VALID const char **ppchNames, 
			   SA_PARAM_OP_VALID DynParamBlock *params,
			   F32 hue, 
			   F32 fRange,
			   F32 fArc,
			   F32 fYaw,
			   const Vec3 vecSource,
			   const Vec3 vecTarget,
			   Quat quatTarget,
			   EPMFXStartFlags eFXFlags,
			   S32 eNodeSelectType);

bool pmFxReplaceOrStart(MovementRequester* mr,
						U32 id, U32 subid, PowerAnimFXType type, EntityRef source, EntityRef target,
						U32 time,
						const char **ppchNames,
						DynParamBlock *params,
						F32 hue,
						bool flash,
						Vec3 vecSource,
						Vec3 vecTarget,
						bool fromSourceVec);

void pmFxStop(SA_PARAM_NN_VALID MovementRequester* mr, 
			  U32 id, U32 subid, PowerAnimFXType eType, EntityRef source, EntityRef target,
			  U32 time, 
			  const char *pchName);

void pmFxCancel(SA_PARAM_NN_VALID MovementRequester* mr,
				U32 id, U32 subid, PowerAnimFXType eType, EntityRef source, EntityRef target);


void pmHitReactStart(SA_PARAM_NN_VALID MovementRequester* mr,
					 U32 id, U32 subid, EntityRef source,
					 SA_PARAM_OP_VALID const char **ppchBitNames,
					 SA_PARAM_OP_VALID const char **ppchFxNames,
					 SA_PARAM_OP_VALID DynParamBlock *params,
					 F32 hue,
					 U32 spcTimeout);

// Starts ignoring movement input.  ID is either a power activation ID, 
//  or a predefined value indicating the external cause (death, root, etc).
void pmIgnoreStart(	SA_PARAM_NN_VALID Character *c,
					SA_PARAM_NN_VALID MovementRequester *mr,
					U32 uiID, PowerAnimFXType eType,
					U32 uiTime,
					SA_PARAM_OP_VALID char *pchCause);

// Stops ignoring movement input.  ID is either a power activation ID, 
//  or a predefined value indicating the external cause (death, root, etc).
void pmIgnoreStop(	SA_PARAM_NN_VALID Character *c,
					SA_PARAM_NN_VALID MovementRequester *mr,
					U32 uiID, PowerAnimFXType eType,
					U32 uiTime);

// Cancels the ignoring of movement input.  ID is either a power activation ID, 
//  or a predefined value indicating the external cause (death, root, etc).
void pmIgnoreCancel(SA_PARAM_NN_VALID MovementRequester *mr,
					U32 uiID, PowerAnimFXType eType);

void pmMoveCancel(SA_PARAM_NN_VALID MovementRequester *mr, U32 uiID, PowerMoveType eType);

void pmLungeStart(	SA_PARAM_NN_VALID MovementRequester *mr, 
					U32 uiID, 
					U32 timeToStart, 
					U32 timeToStop,
					F32 distToStop,
					F32 fSpeed, 
					U32 timeNotify, 
					F32 distNotify,
					EntityRef erTarget, 
					SA_PRE_OP_RELEMS(3) const Vec3 vecTarget,
					S32 bHorizontalLunge,
					S32 bFaceAway );


void pmFaceStart(	SA_PARAM_NN_VALID MovementRequester *mr, 
					U32 uiID, 
					U32 timeToStart,
					U32 timeToStop,
					EntityRef erTarget, 
					SA_PRE_OP_RELEMS(3) const Vec3 vecTarget,
					S32 bFaceActivateSticky,
					S32 bUseVecAsDirection);

void pmLurchStart(	SA_PARAM_NN_VALID MovementRequester *mr, 
					U32 uiID, 
					U32 timeToStart,
					U32 timeToStop,
					F32 distToStop,					
					F32 fSpeed, 
					F32 fMoveYawOffset,
					EntityRef erTarget, 
					SA_PRE_OP_RELEMS(3) const Vec3 vecDirection,
					F32 entCollisionCapsuleBuffer,
					S32 bFaceActivateSticky,
					S32 bReverseLurch,
					S32 bIgnoreCollision);

void pmLurchSetHitFlag(	SA_PARAM_NN_VALID MovementRequester *mr,
						U8 uchActID, 
						bool bHit);

void pmLurchAnimStart(	SA_PARAM_NN_VALID MovementRequester *mr, 
						U32 uiID,
						SA_PARAM_NN_STR const char *pchAnimGraphName,
						U32 timeToStart,
						U32 timeToStop,
						F32 fMoveYawOffset,
						EntityRef erTarget, 
						SA_PRE_OP_RELEMS(3) const Vec3 vecDirection,
						F32 entCollisionCapsuleBuffer,
						S32 bFaceActivateSticky,
						S32 bIgnoreCollision);

void powersMovementSetRooted(MovementRequester* mr, U32 uiTime, bool bRooted);

// Starts a xy directional projectile requester on the given entity
void pmKnockBackStart(SA_PARAM_NN_VALID Entity *e, 
					  SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecDir, 
					  F32 fMagnitude,
					  U32 uiTime,
					  bool bInstantFacePlant,
					  bool bProneAtEnd,
					  F32 fTimer,
					  bool bIgnoreTravelTime);

// Starts a vertical projectile requester on the given entity
void pmKnockUpStart(SA_PARAM_NN_VALID Entity *e, 
					  F32 fMagnitude,
					  U32 uiTime,
					  bool bInstantFacePlant,
					  bool bProneAtEnd,
					  F32 fTimer,
					  bool bIgnoreTravelTime);

// Starts a direct projectile requester on the given entity
void pmKnockToStart(SA_PARAM_NN_VALID Entity *e,
					SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
					U32 uiTime,
					bool bInstantFacePlant,
					bool bProneAtEnd,
					F32 fTimer,
					bool bIgnoreTravelTime);

// Returns true if this entity is controlled by a projectile requested
bool pmKnockIsActive(SA_PARAM_NN_VALID Entity *e);

// Starts a push requester on the given entity
void pmPushStart(SA_PARAM_NN_VALID Entity *be,
				 SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecDir,
				 F32 fMagnitude,
				 U32 uiTime);

// 
void pmConstantForceStart(	SA_PARAM_NN_VALID Entity *pEnt,
							U32 uiID, 
							U32 uiStartTime,
							U32 uiStopTime,
							SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vForce);

// will start a repel that will be relative to the given repeler entity
void pmConstantForceStartWithRepeller(	SA_PARAM_NN_VALID Entity *pEnt,
										U32 uiID, 
										U32 uiStartTime,
										U32 uiStopTime,
										EntityRef erRepeler,
										F32 fYawOffset,
										F32 fSpeed);

void pmConstantForceStop(	SA_PARAM_NN_VALID Entity *pEnt,
							U32 uiID, 
							U32 uiTime);


// Turns entity to entity collisions on
void pmSetCollisionsEnabled(SA_PARAM_NN_VALID Entity *e);

// Turns entity to entity collisions off
void pmSetCollisionsDisabled(SA_PARAM_NN_VALID Entity *e);

// Sets flight mode on and applies the parameters
void pmSetFlightEnabled(SA_PARAM_NN_VALID Entity *e);

// Sets flight mode off
void pmSetFlightDisabled(SA_PARAM_NN_VALID Entity *e);

void pmUpdateFlightThrottle(Entity* e);

// Initialize flight parameters
void pmInitializeFlightParams(SA_PARAM_NN_VALID Entity *e);

// Update flight parameters
void pmUpdateFlightParams(Entity *e, bool useFakeRoll, bool ignorePitch, bool useJumpBit, bool constantFoward);

// Sets the friction of the entity
void pmSetFriction(Entity *e, F32 f);

// Sets the traction of the entity
void pmSetTraction(Entity *e, F32 f);

// Sets the non-jumping gravity of the entity
void pmSetGravity(Entity *e, F32 f);

// Updates the sprint parameters based on the combat state
void pmUpdateTacticalRunParams(Entity *e, F32 f, bool bIsInCombat);

// Updates a special 'InCombat' anim bit based on the combat state of the entity
void pmUpdateCombatAnimBit(Entity *e, bool bIsInCombat);

// Sets the flourish data
void pmSetFlourishData(Entity *e, bool bEnabled, F32 fTimer);

// Sets the general speed? of the entity
void pmSetSpeed(Entity *e, F32 f);

// Sets the jump height of the entity
void pmSetJumpHeight(Entity *e, F32 f);

// Sets the jump traction of the entity
void pmSetJumpTraction(Entity *e, F32 f);

// Sets the jump speed of the entity
void pmSetJumpSpeed(Entity *e, F32 f);

// Sets the jump gravity of the entity
void pmSetJumpGravity(Entity *e, F32 fUp, F32 fDown);

// Sets the flight speed of the entity
void pmSetFlightSpeed(Entity *e, F32 f);

// Sets flight traction of the entity
void pmSetFlightTraction(Entity *e, F32 f);

// Sets flight friction of the entity
void pmSetFlightFriction(Entity *e, F32 f);

// Sets flight turn rate of the entity
void pmSetFlightTurnRate(Entity *e, F32 f);

// Sets flight glide decent rate
void pmSetGlideDecent(Entity *e, F32 fGlideDecent);

// Sets all movement and control data for the Entity, given an optional set of attributes to delta against
void character_UpdateMovement(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_OP_VALID CharacterAttribs *pAttribsOld);


// Updates PowersMovement's selected target for selected facing.
//  This will tend to cause PowersMovement to try and set the Entity's
//  facing towards the Entity's selected target.
// Note that this is NOT used to control facing with respect to any Power activation.
//  Activate facing is handled with the PMMove structure, which overrides select facing.
void pmUpdateSelectedTarget(SA_PARAM_OP_VALID Entity *e, S32 bForce);

// Changes the state of various flags on the Entity that controls how it does selected facing
void pmEnableFaceSelected(SA_PARAM_OP_VALID Entity *e, S32 bEnable);

// Returns the Entity's selected target according to the foreground
EntityRef pmGetSelectedTarget(SA_PARAM_NN_VALID Entity *e);

// Powers timing functions - Should be used across the powers system for 
//  non-persisted timestamps.

// Returns timestamp offset by the given number of seconds from now
U32 pmTimestamp(F32 fSecondsOffset);

// Returns timestamp offset by the given number of seconds from the input time
U32 pmTimestampFrom(U32 uiTimestampFrom, F32 fSecondsOffset);

// Returns difference between now and input time, in seconds.
//  Positive values indicate the given timestamp is in the future.
F32 pmTimeUntil(U32 uiTimestampFrom);

S32 pmShouldUseRagdoll(Entity *e);
