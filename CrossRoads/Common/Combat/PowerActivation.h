/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef POWERACTIVATION_H__
#define POWERACTIVATION_H__
GCC_SYSTEM

#include "referencesystem.h"

#include "Powers.h" // for PowerRef struct

// Forward declarations
typedef struct Capsule					Capsule;
typedef struct Character				Character;
typedef struct Entity					Entity;
typedef struct ExprContext				ExprContext;
typedef struct GameAccountDataExtract	GameAccountDataExtract;
typedef struct Power					Power;
typedef struct PowerAnimFX				PowerAnimFX;
typedef struct PowerAnimFXRef			PowerAnimFXRef;
typedef struct PowerDef					PowerDef;
typedef struct PowerRef					PowerRef;
typedef struct WorldCollCollideResults	WorldCollCollideResults;
typedef struct WorldInteractionEntry	WorldInteractionEntry;
typedef struct WorldInteractionNode		WorldInteractionNode;
typedef struct WorldVolume				WorldVolume;

// Copied from elsewhere
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere

// to make it very clear what's going on
typedef enum PowerActivationStage
{
	kPowerActivationStage_Charge = 0,
		// charging or maintaining (or brand new)

	kPowerActivationStage_Preactivate,
		// a timed pre-activation stage

	kPowerActivationStage_LungeGrab,
		// lunging or grabbing has started, but activation has not yet started

	kPowerActivationStage_Activate,
		// the power is in the activate stage (but may be lunging or grabbing)

	kPowerActivationStage_PostMaintain,
		// the time after a maintain, only if the powerDef has a fTimePostMaintain
} PowerActivationStage;

// Defines which power the character is currently charging or maintaining
//  These are in a specific order so that it's easy to manipulate
typedef enum ChargeMode
{
	kChargeMode_None = 0,
		// Not charging or maintaining anything

	kChargeMode_CurrentMaintain,
		// Maintaining the current power

	kChargeMode_Current,
		// Charging the current power

	kChargeMode_QueuedMaintain,
		// Maintaining the queued power

	kChargeMode_Queued,
		// Charging the queued power

	kChargeMode_OverflowMaintain,
		// Maintaining the overflow power (server only)

	kChargeMode_Overflow,
		// Charging the overflow power (server only)
} ChargeMode;

// Defines the lunge state of the PowerActivation, which is a
//  state that starts between charging and application/activation, but
//  can overlap with activation
typedef enum LungeMode
{
	kLungeMode_None = 0,
		// No lunge

	kLungeMode_Pending,
		// Want to lunge when it's time, or actively lunging

	kLungeMode_Activated,
		// Activated the Power associated with the lunge, potentially still lunging
		//  In this state it's unknown if the lunge reached the target or not

	kLungeMode_Failure,
		// The Power has been activated, and the lunge has stopped, but it failed to reach the target

	kLungeMode_Success,
		// The Power has been activated, and the lunge has stopped, and it reached the target

} LungeMode;


// Defines the grab state of the PowerActivation, which is very similar
//  to lunge.  It takes place after lunge (though generally the two should
//  not co-exist).  It continues during application/activation, in case we
//  want to cancel the Power if the grab is terminated somehow.
typedef enum GrabMode
{
	kGrabMode_None = 0,
		// No grab

	kGrabMode_Pending,
		// Want to grab when it's time

	kGrabMode_Activated,
		// The grab requester has been started

	kGrabMode_Success,
		// The grab succeeded, so the Power should be applied

} GrabMode;

AUTO_ENUM;
typedef enum MovementInputBits
{
	kMovementInputBits_Forward	= (1 << 0),

	kMovementInputBits_Back		= (1 << 1),

	kMovementInputBits_Left		= (1 << 2),

	kMovementInputBits_Right	= (1 << 3),

	kMovementInputBits_MAX,
} MovementInputBits;

#define MovementInputBits_NUMBITS 4
STATIC_ASSERT((kMovementInputBits_MAX-1) == (1 << (MovementInputBits_NUMBITS - 1)));

// PowerActivation tracks all relevant information about the activation
//  of a power, including the power, its targets, various timers, and
//  other tasty bits.  AUTO_STRUCTed so that its fields are accessible
//  in expressions.
AUTO_STRUCT;
typedef struct PowerActivation
{
	PowerRef ref;						NO_AST
		// Reference to the Power being activated

	PowerRef preActivateRef;			NO_AST
		// Reference to the pre-activate power

	REF_TO(PowerDef) hdef;				AST(NAME(Def), SERVER_ONLY)
		// Def of the power being activated

	EntityRef erTarget;					NO_AST
		// The target of the power

	REF_TO(WorldInteractionNode) hTargetObject;		NO_AST
		// The target object

	Vec3 vecTarget;						NO_AST
		// The locational target of the power, used when there is not entity
		
	Vec3 vecTargetSecondary;			NO_AST
		// Secondary location target

	Vec3 vecSourceLunge;				NO_AST
		// The locational source of the power, used when the activation includes a lunge

	Vec3 vecCharStartPos;				NO_AST
		// The location the character was standing when he started casting, to detect knockbacks

	Vec3 vecSourceDirection;			NO_AST
		// the source direction that the power is being cast in, currently used for self targeted AE attacks

	U8	uchID;							NO_AST
		// The ID of the activation

	U32 uiIDServer;						NO_AST
		// The server-side ID

	U32 uiSeedSBLORN;					NO_AST
		// The static BLORN seed for this Activation, also known by client (assuming the
		//  client is predicting correctly), so anything based on it can technically be
		//  exposed. Note that for client-driven PowerActivaions it's generally going to
		//  increase by one per committed PowerActivation, and for Application purposes
		//  the uiPeriod should be added, so it can't be used as a general RNG seed.

	U32 uiPeriod;						AST(NAME(Period), ADDNAMES(Pulse), SERVER_ONLY)
		// For a periodic power, this is the current period count.  0 is the basic activation stage.

	int iPredictedIdx;					NO_AST
		// The client predicted this index into the sub-power array.  -1 if not relevant.

	EntityRef erProximityAssistTarget;	NO_AST
		// optional, only for self-targeted AE type powers

	EntityRef *perTargetsHit;			NO_AST
		// EArray of targets this Activation hit (as of the last period)

	PowerRef **ppRefEnhancements;		NO_AST
		// Refs to Enhancements this Activation used (as of the last period)

	PowerAnimFXRef *pRefAnimFXMain;		NO_AST
		// Ref to the main PowerAnimFX and hue that this Activation is using

	PowerAnimFXRef **ppRefAnimFXEnh;	NO_AST
		// Refs to the Enhancement PowerAnimFXs and hues that this Activation is using

	// Power costs

	F32 fCostPaid;						AST(NAME(CostPaid), SERVER_ONLY)
		// Cost paid for this activation

	F32 fCostPaidSecondary;				AST(SERVER_ONLY)
		// Secondary cost paid for this activation

	F32 fCostPaidServer;				NO_AST
		// Cost paid for this activation according to the server

	F32 fCostPaidServerSecondary;		NO_AST
		// Secondary cost paid for this activation according to the server



	//Confuse Information

	EntityRef erConfuseTarget;			NO_AST
		//The randomly selected target

	EntityRef *perConfuseTargetList;	NO_AST
		//The list of ents around the target

	U32 uiConfuseSeed;					NO_AST
		//The seed used to generate the random target



	U32 uiTimestampQueued;				NO_AST
		// The time that this activation was queued

	U32 uiTimestampCurrented;			NO_AST
		// The time that this activation is expected to go current

	U32 uiTimestampLungeAnimate;		NO_AST
		// The time that the lunge should start animating

	U32 uiTimestampLungeMoveStart;		NO_AST
		// The time that the lunge should start moving

	U32 uiTimestampLungeMoveStop;		NO_AST
		// The time that the lunge should stop moving

	U32 uiTimestampEnterStance;			NO_AST
		// The time that stance should be entered

	U32 uiTimestampActivate;			NO_AST
		// The time that the activate should start

	U32 uiTimestampActivatePeriodic;	NO_AST
		// The time that the activate should start if it's a periodic activation

	U32 uiRandomSeed;					NO_AST
		// Random seed passed to Application/AttribMods, for any per-Activation randomness

	F32 fDistToTarget;					NO_AST
		// The cached distance to the target

	PowerActivationStage eActivationStage; NO_AST

	LungeMode eLungeMode;				NO_AST
		// Current lunge state of the activation

	GrabMode eGrabMode;					NO_AST
		// Current grab state of the activation
		
	U32 bRange : 1;						NO_AST
		// Set to true if the range check passed

	U32 bDelayActivateToHitCheck : 1;	NO_AST
		// Set to true to delay the activation bits/fx to the apply stage hit check.
		//  Used for unpredicted single-target delayed hits, so that if they miss, we
		//  can send them to a point in space near the target, rather than right at
		//  the target.

	U32 bPlayedActivate : 1;			NO_AST
		// Set to true if the activation bits/fx have already been played

	U32 bPlayedDeactivate : 1;			NO_AST
		// Set to true if the deactivation bits/fx have already been played

	U32 bPlayedImmuneFX : 1;
		// Set to true if the immune fx have already been played
		
	U32 bChargeAtVecTarget : 1;			NO_AST
		// Set to true if the charge fx used the vec target rather than
		//  the entity/object

	U32 bActivateAtVecTarget : 1;		NO_AST
		// Set to true if the activation fx used the vec target rather than
		//  the entity/object

	U32 bCommit : 1;					NO_AST
		// Once committed, it is assumed to be usually inappropriate to cancel an activation
		// Client: Set to true if the activation is locked into becoming current
		// Server: Set to true if the client has claimed this activation has become current
		//  or the AI wants to ensure the activation is not kicked out of the queue

	U32 bActivated : 1;					NO_AST
		// True if this activation has actually been executed and the power applied

	U32 bDeactivate : 1;				NO_AST
		// True if this activation wishes to deactivate.  Used to mark Toggle Power activations
		//  that wish to shut off at the end of their current activation stage.

	U32 bPrimaryPet : 1;				NO_AST
		// True if this power is being activated by the pet and not by the player

	U32 bUnpredicted : 1;				NO_AST
		// True if this activation was not predicted by the client, which influences
		//  a variety of server-side behavior

	U32 bHitPrior : 1;					NO_AST
		// If this activation is for a PowerDef that has HitChanceOneTime, this will
		//  be set to the result on period 0, and will be re-used for the remaining periods

	U32 bHitTargets : 1;				NO_AST
		// If this activation hit (in the hit or miss sense) any targets.  SelfOnce does not count.

	U32 bIncludeLungeRange : 1;			NO_AST
		// Include the lunge range for specific lunges (Away lunges for instance)
		
	U32 bSpeedPenaltyIsSet : 1;			NO_AST
		// Set while there is a live speed penalty with this activation ID.

	U32 bStartedAnimGraph : 1;			NO_AST
		// Set the first time an anim graph is started in the new animation system, to prevent accidental restarts.

	U32 bUseSourceDir : 1;				NO_AST
		// set if the vecSourceDirection is valid and should be used in the power application instead of the character's current facing

	U32 bPaidCost : 1;					NO_AST
		// set once the initial cost has been paid for the power

	U32 eInputDirectionBits : MovementInputBits_NUMBITS;		NO_AST
		// the current directional movement input state of the player character, used to offset lurch direction if 
		// the power is setup to use it. 
		// This assumes the vecTarget direction is in the camera direction. 
		// If we need it to be relative to camera this needs to change to a yaw offset.

	F32 fTimeStalled;					NO_AST
		// How long this activation has waited on the server, ready to be current, 
		//  but bCommit has not been set to true

	F32 fStageTimer;					NO_AST
		// How much time is left in the current stage (for stages between charge and activate)
		// For lunges, this can go negative, and that will cause the lunge to time out

	F32 fTimerActivate;					AST(SERVER_ONLY)
		// Countdown timer for activation processes

	F32 fTimerRemainInQueue;			AST(SERVER_ONLY)
		// How much longer a power must remain queued before it can become current

	F32 fTimeCharged;					AST(NAME(Charged), SERVER_ONLY)
		// How long this activation charged, modified by SpeedCharge

	F32 fTimeChargedTotal;				AST(NAME(ChargedTotal), SERVER_ONLY)
		// The actual amount of time this activation has charged, unmodified by SpeedCharge

	F32 fTimeMaintained;				AST(NAME(Maintained), SERVER_ONLY)
		// How long this activation has been maintained, modified by SpeedPeriod

	F32 fTimeActivating;				AST(NAME(Activated), SERVER_ONLY)
		// How long this activation has been going total, unmodified by SpeedPeriod

	F32 fTimeFinished;					AST(NAME(Finished), SERVER_ONLY)
		// How long ago did this activation finish

	F32 fTimeChargeRequired;			AST(NAME(ChargeRequired), SERVER_ONLY)
		// Time a power must charge for to prevent failing all together
	
	F32 fTimeChargeRequiredCombo;		AST(NAME(ChargeRequiredCombo), SERVER_ONLY)
		// Time a power must charge for to prevent falling to the next power in the combo

	F32 fLungeDistance;					AST(NAME(LungeDistance), SERVER_ONLY)
		// The length of the lunge, if there was one

	F32 fLungeSpeed;					NO_AST
		// the speed of the lunge, which isn't always constant if lunge specifies StrictFrameDuration

	F32 fActHitTime;					
		// calculated activation hit time

} PowerActivation;


// Structure used to save/communicate the state of a PowerActivation.  A valid PowerActivation
//  should be able to be rebuilt from this structure.  Intended to be used for saving the state
//  of passive and toggle PowerActivations across map transfers, and restarting those PowerActivations
//  on the client.
AUTO_STRUCT AST_CONTAINER;
typedef struct PowerActivationState
{
	PowerRef ref;			AST(PERSIST, NO_TRANSACT)
		// Ref to the Power that should be active

	U8 uchID;				AST(PERSIST, NO_TRANSACT)
	U32 uiPeriod;			AST(PERSIST, NO_TRANSACT)
	F32 fTimerActivate;		AST(PERSIST, NO_TRANSACT)
	F32 fTimeCharged;		AST(PERSIST, NO_TRANSACT)
	F32 fTimeChargedTotal;	AST(PERSIST, NO_TRANSACT)
	F32 fTimeActivating;	AST(PERSIST, NO_TRANSACT)
		// Fields directly copied from PowerActivation

} PowerActivationState;



// Structure to wrap all the data the client sends to the server when it 
//  activates or deactivates a power.  Many similar fields to an actual
//  PowerActivation.  AUTO_STRUCTed so it can be sent through the command
//  system.
AUTO_STRUCT;
typedef struct PowerActivationRequest
{
	EntityRef erTarget;
		// If targeting an entity, this is the target
	
	REF_TO(WorldInteractionNode) hObjectNodeKey;
		// If targeting an object, this is the target

	Vec3 vecTarget;
		// If the client thinks it ends up hitting a point in space instead, this is where

	Vec3 vecTargetSecondary;
		// If the Power has a secondary target, this is where the client placed it

	Vec3 vecSourcePos;
		// The position the client claims it was in when it initiated the activation

	Vec3 vecSourceDir;
		// The direction the client claims it was facing when it initiated the activation
		
	IVec3 ivecTarget;
	IVec3 ivecTargetSecondary;
	IVec3 ivecSourcePos;
	IVec3 ivecSourceDir;
		// HACK: vecs cast so StructParser doesn't truncate them when sent through commands

	U32 uiPowerID;
		// ID of the power being activated

	char *pchPowerName;
		// Optional power name, for verification

	U32 uiTimeQueued;
		// The client timestamp when this went into the queue

	U32 uiTimeCurrented;
		// The client timestamp when this will probably become current.
		//  May be different that queue time if there's already an active power when this one was started.


	U32 uiTimeEnterStance;
		// The client timestamp for when the character will enter a stance before activation

	EntityRef erTargetPicking;
		// If the Power is a Combo Power, and the target used for picking is
		//  different than the effective target of the request, this is the
		//  target used for picking.  Generally only useful when the Combo is
		//  doing something dumb, like picking between self-targeting Powers
		//  based on which non-self Entity you happen to have targeted.

	U8 bActivate : 1;
		// Whether this is the button press (activate) or release (deactivate)

	U8 bToggleDeactivate : 1;
		// In the case of a Toggle Power activate, this indicates the client thinks it's stopping a toggle

	U8 bCancelExisting : 1;
		// If set to true, this activation will automatically attempt to cancel all existing activations

	U8 bUseVecTarget : 1;
		// True if the vecTarget should be used
	
	U8 bUseVecSource : 1;
		// True if the vecSource fields should be used

	U8 bUseSourceDir : 1;
		// if set, vecSourceDir should be used to initialize the PowerActivation

	U8 bUseAimTrajectoryPos : 1;
		// if the aim trajectory vector is set

	U8 bAnimateNow : 1;
		// True if the client expects to activate the power immediately.
		//  Determines which timestamp the server uses for animation.

	U8 bPrimaryPet : 1;
		// True if this was activated for the pet and not for the player

	U8 bAutoCommit : 1;
		// True if this request wishes for the resulting activation to automatically be marked committed

	U8 bUnpredicted : 1;
		// True if this request was not predicted by the client, which is copied the the resulting activation

	U8 bDontDelayTargeting : 1;
		// when a power def uses bDelayTargetingOnQueuedAttack, instead perform the power arts immediately

	U8 bUpdateChargeVecTarget : 1;
	
	U32 eInputDirectionBits : MovementInputBits_NUMBITS;
		// the current directional movement input state of the player character, used to offset lurch direction if 
		// the power is setup to use it

	U8 uchActID;
		// The activation ID.  Used for all message between the client and server about
		//  issues related to this particular activation (such as misprediction, etc).

	U32 *puiActIDsCanceled;
		// The activation IDs that this activation caused to be canceled, if any

	ChargeMode eModeDeactivate; AST(INT)
		// For a deactivation, this describes the stage of power that the client believes
		//  it is deactivating

	int iPredictedIdx;
		// The client's predicted index of the sub-power array, if relevant

	int iLungePrediction;
		// The client's predicted lunge state, if relevant

	U32 uiSeq;
		// If bAutoCommit, this is the provided seq number, which is verified by the server

	U32 uiSeqReset;
		// If bAutoCommit, this is the seq reset ack

	//Confuse Information
	EntityRef erConfuseTarget;		
		//The randomly selected target

	U32 *perConfuseTargetList;
		//The list of ents around the target

	U32 uiConfuseSeed;
		//The seed used to generate the random target

	EntityRef erProximityAssistTarget;
		// optional, only for self-targeted AE type powers

} PowerActivationRequest;



// Defines the ways a PowerActivationRequest can fail
typedef enum ActivationFailureReason
{
	kActivationFailureReason_ComboMispredict = -1,
	// NOT AN ACTUAL FAILURE - Happens when a combo power mis-predicts 
	// and we want to inform the client of the mispredict. Any other failure 
	// reason can and should override this one 

	kActivationFailureReason_None = 0,
		// It didn't fail

	kActivationFailureReason_Recharge,
		// Failed because the Power is still recharging

	kActivationFailureReason_Cooldown,
		// Failed because the power is in a category which requires a cooldown

	kActivationFailureReason_Cost,
		// Failed because the Power cost can't be fulfilled

	kActivationFailureReason_PriorActNonInterrupting,
		// Failed because a the Power in a prior Activation doesn't allow
		//  interruption upon request, and thus doesn't currently support
		//  anything being queued behind it.
		// Remove this when the queue system is changed to support queuing
		//  behind a non-interrupting Activation.

	kActivationFailureReason_TargetOutOfRange,
		// Failed because the target is out of range

	kActivationFailureReason_TargetOutOfRangeMin,
		// Failed because the target is too close

	kActivationFailureReason_TargetNotInArc,
		// Failed because the target is not in firing arc

	kActivationFailureReason_TargetInvalid,
		// Failed because the target is invalid

	kActivationFailureReason_TargetLOSFailed,
		// Failed because the target failed LOS check

	kActivationFailureReason_TargetImperceptible,
		// Failed because of stealth or simply sight range

	kActivationFailureReason_DoesNotHaveRequiredItemEquipped,
		// Failed because the player does not have the item required for the power equipped

	kActivationFailureReason_Disabled,
		// failed because the player is disabled

	kActivationFailureReason_Knocked,
		// failed because the player is knocked

	kActivationFailureReason_NoChargesRemaining,
		// Failed because the power has no more charges

	kActivationFailureReason_ReactivePowerDisallow,
		// Failed because the power cannot be activated while using a reactive power

	kActivationFailureReason_PowerModeDisallowsUsage,
		// Failed because the power cannot be activated while being prevented by a power mode

	kActivationFailureReason_Rooted,
		// failed because the player is rooted
	
	kActivationFailureReason_RequiresQueueExpression,
		// failed because the power failed the pExprRequiresQueue expression

	kActivationFailureReason_Other,
		// Failed for nonspecific reason

// IMPORTANT NOTE IF YOU ARE ADDING TO THIS LIST:
// Make sure you add your activation failure to the gameserver failure
// switch statement near the end of character_ActivatePowerServer!
} ActivationFailureReason;

#define SetActivationFailureReason(eFailOut,eReason) if(eFailOut && (*eFailOut) <= 0) *eFailOut = eReason

#define CharacterPowerActID(pchar,act) (pchar)->pPowAct##act ? (pchar)->pPowAct##act->uchID : 0
#define CharacterPowerActIDs(pchar) CharacterPowerActID(pchar,Current),CharacterPowerActID(pchar,Queued),CharacterPowerActID(pchar,Overflow),CharacterPowerActID(pchar,Finished)



// Creates a new empty PowerActivation
PowerActivation *poweract_Create(void);

// Destroys and frees an existing PowerActivation
void poweract_Destroy(SA_PARAM_OP_VALID PowerActivation *pact);

// Destroys and frees an existing PowerActivation, sets the pointer to NULL
void poweract_DestroySafe(SA_PRE_NN_NN_VALID PowerActivation **ppact);



// Returns the next PowerActivation ID
U8 poweract_NextID(void);

// Returns the next server-side PowerActivation ID
U32 poweract_NextIDServer(void);

// Sets the Power* and IDs in the Activation
void poweract_SetPower(SA_PARAM_NN_VALID PowerActivation *pact, SA_PARAM_OP_VALID Power *ppow);

// Searches an earray of Activations for the Power
int poweract_FindPowerInArray(SA_PARAM_NN_VALID PowerActivation ***pppActs, SA_PARAM_NN_VALID Power *ppow);

// searches an earray of PowerActivation for a power that uses the given PowerDef, returns the PowerActivation*
PowerActivation* poweract_FindPowerInArrayByDef(SA_PARAM_NN_VALID PowerActivation **ppActs, SA_PARAM_NN_VALID PowerDef *pPowerDef);

// Updates the target location in the activation based on the target entity
void poweract_UpdateLocation(Entity *eSelf, PowerActivation *pact);

// Notes that the activation should stop tracking the target
void poweract_StopTracking(SA_PARAM_NN_VALID PowerActivation *pact);

// Notifies the server that the activation with the given id has been committed on the client
void character_MarkActCommitted(SA_PARAM_NN_VALID Character *pchar, U8 uchID, U32 uiSeq, U32 uiSeqReset);

// Notifies the server that the activation with the given id has an updated vectarget from the client
void character_UpdateVecTarget(int iPartitionIdx, Character *pchar, U8 uchID, Vec3 vVecTarget, EntityRef erTarget);

// updates the current charge power targeting if the ID matches and we're still charging
int character_CurrentChargePowerUpdateVecTarget(Character *pchar, U8 uchID, Vec3 vVecTarget, EntityRef erTarget);

// Fills the earray with PowerActivationState for the the Character.  Destroys whatever was in the earray before.
void character_SaveActState(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerActivationState ***pppActivationState);

// Uses the earray of PowerActivationState to recreate PowerActivations on the Character.
void character_LoadActState(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerActivationState ***pppActivationState);


// HACK: These two functions handle fixing the Vec<->IVec conversion
void poweractreq_FixCmdSend(SA_PARAM_NN_VALID PowerActivationRequest *pActReq);
void poweractreq_FixCmdRecv(SA_PARAM_NN_VALID PowerActivationRequest *pActReq);

// Retrieves the next PowerActivation Seq numbers.  May decide to not assign
//  a value to either U32, so they should be initialized to 0.
void character_GetPowerActSeq(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID U32 *puiSeq, SA_PARAM_NN_VALID U32 *puiSeqReset);

// Verifies the provided PowerActivation Seq numbers.  Returns the server-valid final seq number.  May
//  trigger resets.
U32 character_VerifyPowerActSeq(SA_PARAM_NN_VALID Character *pchar, U32 uiSeq, U32 uiSeqReset);

// Performs a reset of the Character's PowerActivation Seq (if necessary) and notifies the client
void character_ResetPowerActSeq(SA_PARAM_NN_VALID Character *pchar);


// Copies the entity's position and/or direction, with additional information from the activation.
//  The activation should only be used if the entity is the source.
void entGetCombatPosDir(SA_PARAM_NN_VALID Entity *pent,
						SA_PARAM_OP_VALID PowerActivation *pact,
						SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecPos,
						SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecDir);

void entGetPosRotAtTime(SA_PARAM_NN_VALID Entity *pent, 
						U32 timestamp, 
						SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecPosOut,
						SA_PRE_OP_ELEMS(4) SA_POST_OP_VALID Quat qRot);

bool entValidateClientViewTimestamp(U32 timestamp);


void entOffsetPositionToCombatPos(	Entity *pent, 
									SA_PARAM_NN_VALID Vec3 vPosInOut);

void entOffsetPositionToCombatPosByCapsules(SA_PARAM_NN_VALID Entity *pent,
											SA_PARAM_OP_VALID const Capsule* const* eaCapsules, 
											SA_PARAM_NN_VALID Vec3 vPosInOut);


void entGetActivationSourcePosDir(	SA_PARAM_NN_VALID Entity *pent,
									SA_PARAM_OP_VALID PowerActivation *pact,
									SA_PARAM_NN_VALID PowerDef *pPowerDef,
									SA_PARAM_NN_VALID Vec3 vecPosOut,
									SA_PARAM_NN_VALID Vec3 vecDirOut);

// Creates a powers movement event for this character and activation
void character_ActEventCreate(SA_PARAM_NN_VALID Character *pchar,
							  SA_PARAM_NN_VALID PowerActivation *pact);

// Takes a Character and Activation, and tries to return the Power*
SA_RET_OP_VALID Power *character_ActGetPower(SA_PARAM_NN_VALID Character *pchar,
										  SA_PARAM_NN_VALID PowerActivation *pact);

// Updates the Activation's array of references to Enhancements
void character_ActRefEnhancements(int iPartitionIdx,
								  SA_PARAM_NN_VALID Character *pchar,
								  SA_PARAM_NN_VALID PowerActivation *pact,
								  SA_PARAM_OP_VALID Power ***pppEnhancements);

// Updates the Activation's array of references to PowerAnimFX from the main power and Enhancements
void character_ActRefAnimFX(SA_PARAM_NN_VALID Character *pchar,
							SA_PARAM_NN_VALID PowerActivation *pact);

// Initializes the lunging process to start at the given time
//  without actually starting it, by properly setting timestamps and 
//  flags on the activation
void character_ActLungeInit(int iPartitionIdx,
							SA_PARAM_NN_VALID Character *pchar, 
							SA_PARAM_NN_VALID PowerActivation *pact, 
							U32 uiTimeActivate,
							S32 iLungePrediction);

// Notifies the Character that the lunging Activation with the given ID should actually activate its Power
void character_ActLungeActivate(SA_PARAM_NN_VALID Character *pchar, U32 uiLungeID);

// Notifies the Character that the lunging Activation with the given ID stopped, with the specified result position
void character_ActLungeFinished(SA_PARAM_NN_VALID Character *pchar, U32 uiLungeID, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecPos);

// Sets up the Grab mode
void poweract_GrabInit(SA_PARAM_NN_VALID PowerActivation *pact);

// Finishes the charge process and starts the activate process for the activation
// returns if the power in the activation has changed

// I suspect that most of the calls to this do not actually want to go into the activation stage, and perhaps should use EndCharge,
// but I'm not changing them in case that does something weird with the FX [RMARR - 9/21/11]
S32 character_ActChargeToActivate(int iPartitionIdx,
								  SA_PARAM_NN_VALID Character *pchar,
								  SA_PARAM_NN_VALID PowerActivation *pact,
								  SA_PARAM_OP_VALID PowerAnimFX *pafx);

// Like the above, but does less (does not attempt to lunge or grab or start the activation FX)
S32 character_ActEndCharge(int iPartitionIdx,
								  SA_PARAM_NN_VALID Character *pchar,
								  SA_PARAM_NN_VALID PowerActivation *pact);

void character_ActMoveToPreactivate(int iPartitionIdx,
								  SA_PARAM_NN_VALID Character *pchar,
								  SA_PARAM_NN_VALID PowerActivation *pact);

// Does the rest of what character_ActChargeToActivate does, for after preactivate is done
void character_ActMoveToActivate(int iPartitionIdx,
								  SA_PARAM_NN_VALID Character *pchar,
								  SA_PARAM_NN_VALID PowerActivation *pact);

// Attempts to deactivate an active power (being charged, maintained, etc).
//  If the ID and charge mode are 0, it picks whatever is currently active,
//  and passes back out what it did.  If not, it tries to find a good match.
// Note: if bForced is true, it will ignore the requested interrupt flag on the power def when determining whether
//  to deactivate the activation or not
void character_ActDeactivate(int iPartitionIdx,
							 SA_PARAM_NN_VALID Character *pchar,
							 SA_PARAM_NN_VALID U8 *puchActIDInOut,
							 SA_PARAM_NN_VALID ChargeMode *peModeInOut,
							 U32 uiTimeOfEvent,
							 U32 uiTimeCurrentedNew,
							 S32 bForced);

// Causes the character to attempt to interrupt whatever is being charged
void character_ActInterrupt(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, PowerInterruption eInterrupt);





// Cleans up the finished activation
void character_ActFinishedDestroy(SA_PARAM_NN_VALID Character *pchar);

U8 character_ActInstantFinish(int iPartitionIdx,
	Character *pChar,
	PowerActivation *pact);


// Cancels the Activation that is currently in overflow.  Handles it better
//  if the Power that will be replacing it is passed in.  If IDRequired
//  is non-zero, the cancellation will only occur if the overflow Activation
//  matches that ID.  If the eType is a type ignored by the Power, it will
//  not be canceled. Returns the activation id of the canceled Activation.
U8 character_ActOverflowCancelReason(int iPartitionIdx,
									 SA_PARAM_NN_VALID Character *pchar,
									 SA_PARAM_OP_VALID PowerDef *pdefReplacement,
									 U8 uchIDRequired,
									 AttribType eType,
									 S32 bNotify);

// Wrapper for character_ActOverflowCancelReason()
U8 character_ActOverflowCancel(int iPartitionIdx,
							   SA_PARAM_NN_VALID Character *pchar,
							   SA_PARAM_OP_VALID PowerDef *pdefReplacement,
							   U8 uchIDRequired);


// Cancels the activation that is currently queued.  Handles it better
//  if the power that will be replacing it is passed in.  If IDRequired
//  is non-zero, the cancellation will only occur if the queued activation
//  matches that ID.  If the eType is a type ignored by the Power, it will
//  not be canceled. Returns the activation id of the canceled Activation.
U8 character_ActQueuedCancelReason(int iPartitionIdx,
								   SA_PARAM_NN_VALID Character *pchar,
								   SA_PARAM_OP_VALID PowerDef *pdefReplacement,
								   U8 uchIDRequired,
								   AttribType eType,
								   S32 bNotify);

// Wrapper for character_ActQueuedCancelReason()
U8 character_ActQueuedCancel(int iPartitionIdx,
							 SA_PARAM_NN_VALID Character *pchar,
							 SA_PARAM_OP_VALID PowerDef *pdefReplacement,
							 U8 uchIDRequired);


// Same as above, but takes the type of attrib that caused the cancel
// Cancels the activation that is currently current.  Will not cancel
//  current activations that have successfully reached the 'hit' point
//  of the power, unless bForce is true.  If the eType is a type ignored
//  by the Power, it will not be canceled. Returns the activation id of
//  the canceled Activation.
U8 character_ActCurrentCancelReason(int iPartitionIdx,
									SA_PARAM_NN_VALID Character *pchar,
									S32 bForce,
									S32 bRefundCost,
									S32 bRecharge,
									AttribType eType);

// Wrapper for character_ActCurrentCancelReason()
U8 character_ActCurrentCancel(int iPartitionIdx,
							  SA_PARAM_NN_VALID Character *pchar,
							  S32 bForce,
							  S32 bRecharge);


// Attempts to cancel all Activations
U8 character_ActAllCancelReason(int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								U32 bForceCurrent,
								AttribType eType);

// Wrapper for character_ActAllCancelReason()
U8 character_ActAllCancel(int iPartitionIdx,
						  SA_PARAM_NN_VALID Character *pchar,
						  U32 bForceCurrent);




// Finishes the activation that is current.  A more natural termination
//  than cancel.  Override indicates if another power is overriding it
//  (thus kicking it out early).
void character_ActCurrentFinish(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, bool bOverride);


// Returns true if the character is allowed to activate the given def
S32 character_CanActivatePowerDef(SA_PARAM_NN_VALID Character *pchar,
									SA_PARAM_NN_VALID PowerDef *pdef,
									S32 bTest,
									S32 bActiveToggle,
									ActivationFailureReason *peFailOut,
									GameAccountDataExtract *pExtract,
									SA_PARAM_OP_VALID bool *pbGaveFeedbackOut);

// Returns true if the character can pay the cost of the power.  If the character 
//  cannot afford it they are not charged and the function returns false.
//  If the character can afford it, and bPay is true, they are charged the cost.
S32 character_PayPowerCost(int iPartitionIdx,
						   SA_PARAM_NN_VALID Character *pchar,
						   SA_PARAM_NN_VALID Power *ppow,
						   EntityRef erTarget,
						   SA_PARAM_OP_VALID PowerActivation *pact,
						   S32 bPay,
						   GameAccountDataExtract *pExtract);


// Returns the power the character would queue, if the character
//  attempted to queue the given power.  Returns NULL if a power
//  can not be queued.  The returned power may not be the same
//  as the input power.
//  If you do not need a specific time, use now (pmTimestamp(0))
//  If you are not predicting a specific child power, use -1
//  If for some reason the queue'd Power needs to switch targets,
//   this will try to set one of the out targets.  This should
//   only happen on the client.
SA_RET_OP_VALID Power *character_CanQueuePower(int iPartitionIdx,
											SA_PARAM_NN_VALID Character *pchar,
											SA_PARAM_NN_VALID Power *ppow,
											SA_PARAM_OP_VALID Entity *pentTarget,
											SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetSecondary,
											SA_PARAM_OP_VALID WorldInteractionNode *pnodeTarget,
											SA_PARAM_OP_VALID Entity *pentTargetPicking,
											SA_PARAM_OP_VALID Entity **ppentTargetOut,											
											SA_PARAM_OP_VALID WorldInteractionNode **ppnodeTargetOut,
											SA_PARAM_OP_VALID bool *pbShouldSetHardTarget,
											U32 uiTime,
											S32 iPredictedIdx,
											SA_PARAM_OP_VALID ActivationFailureReason *peFailOut,
											S32 bCheckRange,
											bool bNoFeedback,
											bool bCheckRecharge,
											GameAccountDataExtract *pExtract);


bool character_CheckPowerQueueRequires(int iPartitionIdx,
	Character *pchar,
	Power *ppow,
	Entity *pentTarget,
	Vec3 vecTargetSecondary,
	WorldInteractionNode *pnodeTarget,
	Entity *pentTargetPicking,
	ActivationFailureReason *peFailOut,
	bool bNoFeedback,
	GameAccountDataExtract *pExtract);

S32 character_ActTestStatic(SA_PARAM_NN_VALID Character *pchar,
							SA_PARAM_NN_VALID Power *ppow,
							SA_PARAM_NN_VALID PowerDef *pdef,
							SA_PARAM_OP_VALID ActivationFailureReason *peFailOut,
							GameAccountDataExtract *pExtract,
							bool bNoFeedback);

S32 character_ActTestDynamic(	int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_NN_VALID Power *ppow,
								SA_PARAM_OP_VALID Entity *pentTarget,
								SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetSecondary,
								SA_PARAM_OP_VALID WorldInteractionNode *pnodeTarget,
								SA_PARAM_OP_VALID Entity **ppentTargetOut,
								SA_PARAM_OP_VALID WorldInteractionNode **ppnodeTargetOut,
								SA_PARAM_OP_VALID ActivationFailureReason *peFailOut,
								S32 bCheckRange,
								bool bNoFeedback,
								bool bCheckRecharge);

S32 character_ActTestPowerTargeting(int iPartitionIdx,
									SA_PARAM_NN_VALID Character *pchar, 
									SA_PARAM_NN_VALID Power *ppow, 
									SA_PARAM_NN_VALID PowerDef *pdef,
									bool bNoFeedback,
									GameAccountDataExtract *pExtract);


void character_RefreshTactical(SA_PARAM_NN_VALID Character *pchar);
void character_PayTacticalRollCost(SA_PARAM_NN_VALID Character *pChar);

// General Periodics

// Cleans up the animation state related to periodic Powers.  Exposed independently
//  from the actual deactivation so that it can be called early in case we know ahead
//  of time that it's going to be stopping.
void character_DeactivatePeriodicAnimFX(int iPartitionIdx,
										SA_PARAM_NN_VALID Character *pchar,
										SA_PARAM_NN_VALID PowerActivation *pact,
										U32 uiTimeAnim);

// Checks if the Character's PowerActivation should automatically reapply because its dependencies changed
void character_ActCheckAutoReapply(SA_PARAM_NN_VALID Character *pchar,
									SA_PARAM_NN_VALID PowerActivation *pact,
									SA_PARAM_NN_VALID CharacterAttribs *pOldAttribs,
									SA_PARAM_NN_VALID CharacterAttribs *pNewAttribs);


// Passives

// Sets the state of the passive power (doesn't check if such a setting is allowed).
//  Returns the index of the PowerActivation.
S32 character_ActivatePassive(int iPartitionIdx,
							  SA_PARAM_NN_VALID Character *pchar,
							  SA_PARAM_NN_VALID Power *ppow);

// Sets the state of the passive power (doesn't check if such a setting is allowed)
void character_DeactivatePassive(int iPartitionIdx,
								 SA_PARAM_NN_VALID Character *pchar, 
								 SA_PARAM_NN_VALID PowerActivation *pact);

// Sets all the character's passive powers to their allowed active state
void character_RefreshPassives(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Activate all the character's inactive passive powers (if allowed to be active)
void character_ActivatePassives(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Deactivates all the character's active passive powers (even if they are allowed to be active)
void character_DeactivatePassives(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar);



// Toggles

// Puts a toggle Power Activation into the active toggles earray.  Will deactivate
//  any toggles already in the earray that are mutually exclusive with the new
//  Activation.
void character_ActivateToggle(int iPartitionIdx,
							  SA_PARAM_NN_VALID Character *pchar,
							  SA_PARAM_NN_VALID PowerActivation *pact,
							  U32 uiTimeAnim);

// Deactivates a toggle Power Activation, removes it from the toggle earray and
//  frees it.  Passing in the actual Power is optional.
void character_DeactivateToggle(int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_NN_VALID PowerActivation *pact,
								SA_PARAM_OP_VALID Power *ppow,
								U32 uiTimeAnim,
								int bRecharge);

// Sets all the character's active-flagged toggles to their allowed
//  active state.  Will activate allowed toggles that are not in the
//  toggle list, and will deactivate disallowed toggles.
void character_RefreshToggles(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Deactivates all the character's active toggle powers, cleans the toggle list
void character_DeactivateToggles(int iPartitionIdx,
								 SA_PARAM_NN_VALID Character *pchar,
								 U32 uiTimeAnim,
								 int bRecharge,
								 int bDeadCheck);

// Returns true if the PowerActivation is for a Toggle
S32 poweract_IsToggle(SA_PARAM_NN_VALID PowerActivation *pact);

int poweract_DoesRootPlayer(PowerActivation *pact);


// Maintained

// Stops the maintained power, putting it into a post-deactivate phase if the power has a fTimePostMaintain
void character_ActMoveToPostMaintain(	int iPartitionIdx,
										Character *pChar,
										Power *pPow,
										PowerDef *pPowerDef,
										PowerAnimFX *pafx,
										PowerActivation *pAct,
										U32 uiTimeAnim);

// Deactivates a Maintained Power Activation.  Does not destroy the Activation.
void character_DeactivateMaintained(int iPartitionIdx,
									SA_PARAM_NN_VALID Character *pchar,
									SA_PARAM_NN_VALID Power *ppow,
									SA_PARAM_NN_VALID PowerActivation *pact,
									SA_PARAM_OP_VALID PowerAnimFX *pafx,
									U32 uiTimeAnim);

void character_DeactivateMaintainedAnimFX(	int iPartitionIdx,
											SA_PARAM_NN_VALID Character *pchar,
											SA_PARAM_NN_VALID PowerActivation *pact,
											SA_PARAM_NN_VALID PowerDef *pPowerDef);


// General utility functions involved in power activation


// Validates line of sight checks between a source entity and target entity
S32 combat_ValidateHit(Entity *pentSource, Entity *pentTarget, WorldCollCollideResults *wcResults);
// Validates line of sight checks between a source entity and target entity or node
S32 combat_ValidateHitEx(int iPartitionIdx, Entity *pentSource, Entity *pentTarget, WorldCollCollideResults *wcResults, WorldInteractionEntry *pnodeTarget);

// Returns true if the PowerDef is allowed to activate in the given WorldRegionType
S32 powerdef_RegionAllowsActivate(SA_PARAM_NN_VALID PowerDef *pdef, WorldRegionType eType);

// Checks the line of sight from the source to the target.  Can cast either
//  a ray or a capsule.  Can takes the entities/node involved, if any, so they don't
//  hit themselves (if they're part of the world).  If the world is hit it
//  will put the hit location in the out vector.
// If bIgnoreNoCollCameraObjects is passed as true, the ray casts ignore any object
//  marked to not collide with the camera.
S32 combat_CheckLoS(int iPartitionIdx,
					SA_PRE_NN_RELEMS(3) const Vec3 vecSource,
					SA_PRE_NN_RELEMS(3) const Vec3 vecTarget,
					SA_PARAM_OP_VALID Entity *pentSource,
					SA_PARAM_OP_VALID Entity *pentTarget,
					SA_PARAM_OP_VALID WorldInteractionEntry *pnodeTarget,
					S32 bCapsule,
					bool bIgnoreNoCollCameraObjects,
					SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecHitLocOut);


// Finds the real target entity or location, given the Character and Activation
S32 character_ActFindTarget(int iPartitionIdx,
							SA_PARAM_NN_VALID Character *pchar,
							SA_PARAM_NN_VALID PowerActivation *pact,
							SA_PRE_NN_RELEMS(3) const Vec3 vecSourcePos,
							SA_PRE_NN_RELEMS(3) const Vec3 vecSourceDir,
							SA_PARAM_NN_VALID Entity **ppentTargetOut,
							SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetOut);

// Finds the real target entity or vector, given the power def, source and chosen target
S32 combat_FindRealTargetEx(int iPartitionIdx,
							SA_PARAM_OP_VALID Power *ppow,
							SA_PARAM_NN_VALID PowerDef *pdef,
							SA_PARAM_OP_VALID Entity *pentSource,
							SA_PRE_NN_RELEMS(3) const Vec3 vecSourcePos,
							SA_PRE_OP_RELEMS(3) const Vec3 vecSourceDir,
							EntityRef erTarget, 
							EntityRef erProximityAssistTarget,
							SA_PRE_OP_RELEMS(3) const Vec3 vecTarget,
							SA_PRE_OP_RELEMS(3) const Vec3 vecTargetSecondary,
							SA_PARAM_OP_VALID WorldVolume*** pppvolTarget,
							S32 bTesting,
							U32 uiTimestampClientView,
							SA_PARAM_NN_VALID Entity **ppentTargetOut,
							SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetOut,
							SA_PARAM_OP_VALID F32 *pfDistOut,
							SA_PARAM_OP_VALID S32 *pbRangeOut);

// Finds an entity ref for the activation from an interaction node handle
S32 character_TargetEntFromNode(int iPartitionIdx, Character *pchar, PowerActivation *pact);

// Callback function used by AI to watch Power Activation process
typedef void (*entity_NotifyPowerExecutedCallback)(Entity* pEnt, Power* ppow);
void combat_SetPowerExecutedCallback(entity_NotifyPowerExecutedCallback callback);

// Callback function used by AI to watch Power Recharge process
typedef void (*entity_NotifyPowerRechargedCallback)(Entity* pEnt, Power* ppow);
extern entity_NotifyPowerRechargedCallback g_funcNotifyPowerRechargedCallback;
void combat_SetPowerRechargedCallback(entity_NotifyPowerRechargedCallback callback);

S32 character_ActivationHasReachedHit(SA_PARAM_NN_VALID PowerActivation *pact);

#endif
