#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CombatEnums.h"
#include "referencesystem.h"
#include "Powers.h"
#include "PowersEnums.h"
#include "Tray.h"
#include "EntityMovementRequesterDefs.h"

extern StaticDefineInt TargetTypeEnum[];
extern StaticDefineInt ItemCategoryEnum[];
extern StaticDefineInt PowerModeEnum[];

// CombatConfig defines various options and settings for the entire
//  combat system.  If a complex/expensive feature is added to the
//  combat system for a specific game, it can be optionally enabled
//  by adding flags/options here.

AUTO_STRUCT;
typedef struct TimerConfig
{
	F32 fTimerUse;
		// Timer when an entity uses a Power that could affect foes

	F32 fTimerAttack;
		// Timer when an entity uses a Power to attack a foe

	F32 fTimerAttacked;
		// Timer when an entity is hit by a Power from a foe

	F32 fTimerAssist;
		// Timer when an entity affects another entity that has an active timer

	F32 fTimerAggro;
		// Timer when you are aggrod by something


	// THESE ARE VISUALS ONLY
	F32 fTimerTacticalRoll;
		// Timer which defines how long the entity will do combat visuals when they use tactical rolls

	F32 fTimerTacticalAim;
		// Timer which defines how long the entity will do combat visuals when they use tactical aim

	F32 fTimerUseVisual;
		// Timer when an entity uses a Power that could affect foes (but there are no foes)
	
	F32 fTimerAggroDistance;							AST(DEFAULT(80))
		// Distance at which, if enabled, the AI system uses to determine if you're aggroed (such as nearby allies being in combat)

	U32 bDisallowPlayerTeamAggro : 1;
		// if set, won't allow player lead AITeams from putting characters into combat
} TimerConfig;

AUTO_STRUCT;
typedef struct DamageDecayConfig
{
	U32 uiDelayDiscard;				AST(NAME(DelayDiscard))
		// Seconds.  Delay before damage from a particular owner is completely discarded.  0 disables.

	U32 uiDelayDecay;				AST(NAME(DelayDecay))
		// Seconds.  Delay before damage from a particular owner is allowed to decay.  0 disables decay and percentage discard.

	F32 fPercentDiscard;
		// If damage from a particular owner is allowed to decay, but that damage is less than
		//  this percent of the Character's total health, that damage is completely discarded.

	F32 fScaleDecay;
		// Scale applied to healing/regeneration that triggers decay, for the purposes of determining how much
		//  damage to decay.  0 disables decay.

	U32 bRunEveryTick : 1;
		// Set to true during load if decay should run every tick, instead of just when healing/regen occurs.

} DamageDecayConfig;

// Mostly generic structure for defining chance-based events, what attributes affect them, how their
//  math works, etc.  Used for HitChance, as well as other attack avoidance systems.
AUTO_STRUCT;
typedef struct ChanceConfig
{
	S32 eAttribChancePos;			AST(ADDNAMES(AttribHit) SUBTABLE(AttribTypeEnum))
		// The attribute that defines increases in the chance of the event

	S32 eAttribChanceNeg;			AST(ADDNAMES(AttribMiss) SUBTABLE(AttribTypeEnum))
		// The attribute that defines decreases in the chance of the event

	S32 eAttribMag;					AST(SUBTABLE(AttribTypeEnum))
		// The attribute that defines the magnitude of an event, when the event occurs.
		//  If unset, the effective magnitude is 0.

	F32 fChanceNeutral;
		// [0 .. 1] The chance of the event when everything is equal

	F32 fChanceMin;
		// [0 .. 1] Minimum chance

	F32 fChanceMax;
		// [0 .. 1] Maximum chance

	CombatTrackerFlag eFlag;
		// Flag to use instead of the default flag for whatever type of chance this is

	S32 iPowerCategory;				AST(SUBTABLE(PowerCategoriesEnum))
		// Optional PowerCategory which this affects (for avoidance).  If unspecified, this can affect all Powers.

	F32 fCritChanceOverflowMulti;
		// Multiply the hit chance overflow by this value, and add it to the critical chance

	F32 fCritSeverityOverflowMulti;
		// Multiply the hit chance overflow by this value, and add it to the critical severity

	F32 fArc;
		// Arc over which the chance is relevant.  Only used for AvoidChance right now.

	U32 bSimple : 1;
		// If true, the the diff between Pos and Neg is used as an explicit percent, rather than
		//  running it through the asymptote math to find the effective chance.

	U32 bDefenseless : 1;
		// If true, the chance is modified as appropriate when the target is defenseless

	U32 bDamageOnly : 1;
		// If true and this is a partial avoidance mechanic, this indicates it only affects
		//  Damage attributes.

	U32 bOneTimeCancelOnMiss : 1;
		// If true, Powers marked HitChanceOneTime will automatically shut off when they miss

	U32 bOverflow : 1;
		// If true, any chance over the fChanceMax overflows into other bonuses

} ChanceConfig;

AUTO_STRUCT;
typedef struct LevelCombatControlConfig
{
	U32 bItemLevelShift : 1;
		// If true, the use level of all items should shift to same degree as the Character shifts
		//  Otherwise, the default behavior is used (use level shifts down based on the equip level).

} LevelCombatControlConfig;

AUTO_STRUCT;
typedef struct BattleFormConfig
{
	U32 uiTimerEnabled;				AST(NAME(TimerEnabled))
		// Time after BattleForm is enabled that must elapse before it can be disabled

	U32 uiTimerDisabled;			AST(NAME(TimerDisabled))
		// Time after BattleForm is disabled that must elapse before it can be enabled

	const char **ppchMapsOptional;	AST(NAME(MapsOptional), POOL_STRING) 
		// Maps on which BattleForm is optional and forcibly disabled on load/respawn

} BattleFormConfig;

AUTO_STRUCT;
typedef struct TacticalRequesterSprintDef
{
	// todo(RP): clean-up naming
	F32 fRunMaxDurationSeconds;
	F32 fRunMaxDurationSecondsCombat;
	F32 fRunCooldown;
	F32 fRunCooldownCombat;
	F32 fRunFuelRefillRate;
	F32 fRunFuelDelay;
	F32 fSpeedScaleSprint;
	F32 fSpeedScaleSprintCombat;

	// Absolute speed params (these override the relative ones)
	F32 fSpeedSprint;
	F32 fSpeedSprintCombat;

	// if set, aiming will be completely disabled in the tactical requester
	U32 bSprintDisabled : 1;

	// If set, the character will automatically sprint forward
	// when no input is received in sprint mode
	U32 bAutoSprint : 1;
	
} TacticalRequesterSprintDef;

AUTO_STRUCT;
typedef struct TacticalRequesterRollDef
{
	F32		fRollDistance;
	F32		fRollSpeed;

	// what frame to start rolling forward
	S32		iRollFrameStart;

	// acceleration up to the roll speed, the number of frames to do it in
	S32		iRollAccelNumberOfFrames;
	
	// deceleration down to stopping, the number of frames to do it in
	S32		iRollDecelNumberOfFrames;
	
	F32		fRollPostHoldSeconds;
	F32		fRollCooldown;
	F32		fRollFuelCost;

	// if set roll will be allowed in the air
	U32		bAllowInAir : 1;

		
	// if set, rolling will be completely disabled in the tactical requester
	U32		bRollDisabled : 1;					
	
	// if set, will face the character in the direction of the roll
	U32		bRollFacesInRollDirection : 1;

	// if set, rolls will be disabled when the character is rooted
	U32		bRollDisableDuringRootAttrib : 1;


} TacticalRequesterRollDef;

AUTO_STRUCT;
typedef struct TacticalRequesterAimDef
{
	F32 fSpeedScaleCrouch;						AST(DEFAULT(1.0f))

	F32 fSpeed;									AST(NO_TEXT_SAVE)

	F32 fAimMinDurationSeconds;

	F32 fAimCooldown;
	
	// if set, aiming will be completely disabled in the tactical requester
	U32 bAimDisabled : 1;
		
} TacticalRequesterAimDef;

AUTO_STRUCT;
typedef struct TacticalRollConfig
{
	TacticalRequesterRollDef rollDef;			AST(EMBEDDED_FLAT)

	// global roll config
	F32	fRollNoCostDistPercentThreshold;

	S32 eRollCostAttrib;						AST(SUBTABLE(AttribTypeEnum))

	// if eRollCostAttribMax defined, will be a percent of the max, otherwise it's an absolute value
	S32 eRollCostAttribMax;						AST(SUBTABLE(AttribTypeEnum))

	F32 fRollCostAttribCost;

	// player will immediately roll if they start aiming while holding a movement key
	U32 bRollImmediateWhenMovingWhenAiming : 1;
		
	U32 bDisableDoubleTapRoll : 1;

	U32 bRollWhileCrouching : 1;
	
	U32	bRollIgnoresGlobalCooldown : 1;

	// if set, the 'roll' command will not roll until/unless the player presses a direction
	U32 bRollOnlyWithDirectionInput : 1;

	U32 bQueueTacticalRolls : 1;
		// Allow tactical rolls to be queued behind powers

} TacticalRollConfig;

AUTO_STRUCT;
typedef struct TacticalSprintConfig
{
	TacticalRequesterSprintDef		sprintDef;			AST(EMBEDDED_FLAT)

	// Lifting the sprint button will cause sprint turn off
	U32 bSprintToggles : 1;
	
	// If true, sets the cooldown and run duration to 0 for critters using the run movement
	U32 bCrittersIgnoreRunTimeouts : 1;

	// FX used for sprinting
	const char *pchSprintFX;							AST(POOL_STRING)
		
} TacticalSprintConfig;


AUTO_STRUCT;
typedef struct TacticalAimConfig
{
	TacticalRequesterAimDef		aimDef;			AST(EMBEDDED_FLAT)
		
	// Scalar applied to ground speed when crouching.  Must be positive to be valid.
	F32 fCrouchEntityHeightRatio;				AST(DEFAULT(0.6f))

	const int *peAimRequiredItemCategory;		AST(NAME(AimRequiredItemCategory) SUBTABLE(ItemCategoryEnum))
	
	S32 eAimCostAttrib;							AST(SUBTABLE(AttribTypeEnum))
			
	// If set, Aim and Crouch are two different modes
	U32 bSplitAimAndCrouch : 1;

	// If set, Crouch is canceled if the player moves
	U32 bMovementCancelsCrouch : 1;				AST(DEFAULT(1))

	// If set, the Crouch command toggles instead of maintains
	U32 bCrouchModeToggles : 1;					AST(DEFAULT(1))

	// While in aim mode, don't do stance transitions
	U32 bNoStanceTransitionsWhileAiming : 1;

	// If set, aim ignores the global tactical cooldown
	U32	bAimIgnoresGlobalCooldown : 1;			AST(DEFAULT(1))

	U32 bAimStrafes : 1;

	// if set, jump is disabled during aiming
	U32 bAimDisablesJump : 1;


} TacticalAimConfig;


AUTO_STRUCT;
typedef struct TacticalRequesterConfig
{
	TacticalRollConfig		roll;			AST(EMBEDDED_FLAT NAME("Roll"))
	TacticalSprintConfig	sprint;			AST(EMBEDDED_FLAT)
	TacticalAimConfig		aim;			AST(EMBEDDED_FLAT)

	// General cooldown
	F32 fTacticalMoveGlobalCooldown;

	// The power slot executed if the rolling and aiming fails to execute in tacticalSpecial commands
	S32 iTacticalSpecialFallbackPowerSlot;		AST(DEFAULT(-1))
		
	// if bDisablePowersUsageWhileAimIsHeld is set, these are the allowed categories of powers to still be used while blocking
	S32 *piAllowedCategoriesWhileAimIsHeld;		AST(NAME(AllowedCategoriesWhileAimIsHeld), SUBTABLE(PowerCategoriesEnum))
	
	// for all of powers activation & charge that are RootDuringActivate, disable these tactical movement features
	U32 bRollDisableDuringPowers : 1;
	U32 bAimDisableDuringPowers : 1;
	U32 bSprintDisableDuringPowers : 1;

	// set on post-load if bRollDisableDuringPowers, bSprintDisableDuringPowers or bAimDisableDuringPowers are set, for later convenience 
	U32 bTacticalDisableDuringPowers : 1;		NO_AST

	// The flags to determine which tactical movement features are disabled in combat
	U32 bRollDisableDuringCombat : 1;
	U32 bAimDisableDuringCombat : 1;
	U32 bSprintDisableDuringCombat : 1;

	// set on post-load if bAimDisableDuringCombat, bAimDisableDuringCombat or bSprintDisableDuringCombat are set, for later convenience 
	U32 bTacticalDisableDuringCombat : 1;		NO_AST

	// if true, tactical aim may be used to cancel powers before the power's hit frame
	U32 bAimCancelsPowersBeforeHitFrame : 1;
		
	// when the character is disabled via powers disable, disable the ability to aim, roll
	U32 bAimDisableDuringPowerDisableAttrib : 1;
	U32 bRollDisableDuringPowerDisableAttrib : 1;
		
	// When true, after the player just presses aim it will cancel any queued powers
	U32 bTacticalAimCancelsQueuedPowers : 1;

	// When true, after the player just presses sprint it will cancel any queued powers
	U32 bTacticalSprintCancelsQueuedPowers : 1;

	// When true, after the player just presses roll it will cancel any queued powers
	U32 bTacticalRollCancelsQueuedPowers : 1;
	
	// if set, disables any powers from being cast while the block button is held
	U32 bDisablePowersUsageWhileAimIsHeld : 1;

	// used when other systems might control the tactical requester, but we want the user to not directly have input 
	// control it.
	U32 bIgnoreAllTacticalInput : 1;
	
} TacticalRequesterConfig;


AUTO_STRUCT;
typedef struct PowersAlwaysQueueConfig
{
	// if set, will force cancel the current power when a power that is marked as AlwaysQueue is queued.
	U32			bCurrentPowerForceCancel : 1;

	// if set, will refund the cost of a power (if any) if it is cancelled before the hit frame
	U32			bCurrentPowerRefundCost : 1;				AST(DEFAULT(1))

	// if set, will recharge the power if canceled. 
	U32			bCurrentPowerRechargePower : 1;

} PowersAlwaysQueueConfig;

AUTO_STRUCT;
typedef struct CombatAdvantageConfig
{
	// the distance at which it will show the combat advantage rings on enemy characters
	F32			fClientAdvantageRingsShowDistance;

	F32			fFlankAngleTolerance;						AST(DEFAULT(30))

	F32			fFlankingDistance;

	// set on post-load from fFlankAngleTolerance
	F32			fFlankingDotProductTolerance;				NO_AST

	// 
	F32			fStrengthBonusMag;

	F32			fFXInArcHue;
	F32			fFXInArcSaturation;

	F32			fFXHitFrameHue;
	F32			fFXHitFrameSaturation;
	F32			fFXHitFrameTimer;
	
} CombatAdvantageConfig;

AUTO_STRUCT;
typedef struct InteractionConfig
{
	// entity_GetBestInteract function does not set the best interact
	// if the ent->pPlayer->InteractStatus.overrideSet is set and none
	// of the interaction options matches the overridden entity or node.
	// This flag is used when the ent->pPlayer->InteractStatus.overrideSet is set
	// and no entity/node is set as override. Best interaction option within this
	// distance becomes the best interaction option for the player.
	// This config option only is used when the following condition is true:
	// g_CurrentScheme.bMouseLookInteract && gclPlayerControl_IsMouseLooking() 
	F32			fInteractRangeToSetAsOverride;

	// If the corpse looting is enabled, this is the FX played on corpses with loot
	const char *pchCorpseLootFX;				AST(POOL_STRING)

	// If the corpse looting is enabled, this is the FX played on corpses with mission items
	const char *pchCorpseMissionItemLootFX;		AST(POOL_STRING)
} InteractionConfig;

AUTO_STRUCT;
typedef struct ConditionalPlayerHitFX
{
	// The name of the hit FX
	const char *pchPlayerHitFX;					AST(NAME("PlayerHitFX") POOL_STRING)

	// Effective damage interval for the FX to play is (fMinumumHPPercentage, fMaximumHPPercentage]
	// Minimum hit point percentage damage should take [0.0, 1.0]
	// Only the highest damage is evaluated.
	F32 fMinumumHPPercentage;					AST(NAME("MinumumHPPercentage"))

	// Minimum hit point percentage damage should take [0.0, 1.0]
	// Only the highest damage is evaluated.
	F32 fMaximumHPPercentage;					AST(NAME("MaximumHPPercentage") DEFAULT(1))
} ConditionalPlayerHitFX;

AUTO_STRUCT;
typedef struct PlayerHitFXConfig
{
	// Array of optional FX
	ConditionalPlayerHitFX **ppOptionalFX;			AST(NAME("ConditionalPlayerHitFX"))
} PlayerHitFXConfig;

AUTO_STRUCT;
typedef struct LurchConfig
{
	F32 fAddedCapsuleRadius;					AST(NAME("LurchAddedCapsuleRadius"))

	// Lurch settings
	U32 bDisable : 1;							AST(NAME("LurchDisable"))
	U32 bStopOnEntityCollision : 1;				AST(NAME("LurchStopOnEntityCollision"))
	U32 bDisableInAir : 1;						AST(NAME("LurchDisableInAir"))
	U32 bStopOnLedges : 1;						AST(NAME("LurchStopOnLedges"))

} LurchConfig;

AUTO_STRUCT;
typedef struct PowerActivationImmunities
{
	Expression	*pExprAffectsMod;				AST(NAME("AffectsMod"), REDUNDANT_STRUCT("ExprAffectsMod", parse_Expression_StructParam), LATEBIND)
		
	const char **ppchStickyFX;					AST(NAME(StickyFX), POOL_STRING)
	
} PowerActivationImmunities;


AUTO_STRUCT;
typedef struct SpecialCriticalAttribs
{
	AttribType			eCriticalChance;	AST(SUBTABLE(AttribTypeEnum), DEFAULT(-1))
	AttribType			eCriticalSeverity;	AST(SUBTABLE(AttribTypeEnum), DEFAULT(-1))

	// array of attribs that this crit applies to
	AttribType			*eaAttribs;			AST(INT, NAME(Attribs), SUBTABLE(AttribTypeEnum))
} SpecialCriticalAttribs;


AUTO_STRUCT;
typedef struct SpecialAttribModifiers
{
	AttribType					eKnockProneModifier;			AST(SUBTABLE(AttribTypeEnum))
	SpecialCriticalAttribs		**eaSpecialCriticalAttribs;		AST(NAME(CriticalAttrib))
} SpecialAttribModifiers;

AUTO_STRUCT;
typedef struct AutoAttackConfig
{
	F32 fServerFinishDelay; 
		//How long AutoAttackServer powers must wait after an activation to fire.

	S32 *piAutoAttackPowerSlots;						AST(NAME(AutoAttackPowerSlots))
		// list of powerSlots that are valid for auto-attack

	S32 *piPowerCategoriesCanceledByPredictedDeath;	AST(NAME(PowerCategoriesCanceledByPredictedDeath) SUBTABLE(PowerCategoriesEnum))
		// list of power categories that will have the auto-attack power not perform or a queued activation if the target
		// is predicted to be dead

	U32 bClientSchemeFinishDelay : 1;
		// If true, the delay after finishing a non-AutoAttack Power on the Client before queueing an AutoAttack
		//  is copied from the ControlScheme's fAutoAttackDelay (which is normally used as a delayy between AutoAttacks
		//  themselves.

	U32 bAllowInitialAttackToFinish : 1;
		// if set, will allow the current autoAttack power activation to finish or the queued power to activate (if it hadn't went current)

	U32 bNeverDelay : 1;
		// if set, will not try and delay the autoAttacking for any other reason than the player not being able to queue the power.

	U32 bDeactivateChargePowersOnTargetSwitch : 1;		AST(DEFAULT(1))
		// defaults to true 
		// if set, will cancel autoattack activations on target switch

	U32 bDeactivateTargetedMaintainsCancelOnTargetSwitch : 1; AST(DEFAULT(1))
		// defaults to true 
		// if set, will cancel character targeted maintain autoattack activations on target switch

	U32 bDeactivateSelfTargetedMaintainsCancelOnTargetSwitch : 1; AST(DEFAULT(1))
		// defaults to true
		// if set, will cancel character self targeted maintain autoattack activations on target switch

	U32 bUseExplicitPower : 1;					
		// if set, when turning on auto-attack use the actual power in the powerTray that we are trying to autoattack with
		// instead of the autoAttack ID priority list.

	U32 bAlwaysEnableEvenIfNoValidTarget : 1;
		// if set auto-attack will be always be enabled if we have a valid power

	U32 bProvideFeedbackOnAutoAttackFail : 1;
		// if set will provide notifications on failing to queue power
	
} AutoAttackConfig;

AUTO_STRUCT;
typedef struct CombatConfig
{
	TimerConfig *pTimer;
		// Optional substructure of combat timer rules.  If it exists, Combat mode based on the timers is enabled.

	DamageDecayConfig *pDamageDecay;
		// Optional substructure of damage decay rules.  If it exists, damage decay is enabled.

	ChanceConfig *pHitChance;
		// Optional substructure of hit chance rules.  If it exists, hit/miss calculations are enabled.

	ChanceConfig *pAvoidChance;
		// Optional substructure of avoidance chance rules.  If it exists, generic avoidance calculations are enabled.

	LevelCombatControlConfig *pLevelCombatControl;
		// Optional substructure of level combat control rules.

	BattleFormConfig *pBattleForm;
		// Optional substructure of BattleForm rules.

	PowersAlwaysQueueConfig alwaysQueue; AST(NAME("AlwaysQueue"))

	CombatAdvantageConfig *pCombatAdvantage;

	// If this sub struct gets bigger it might warrant a new def file
	InteractionConfig *pInteractionConfig;

	REF_TO(PowerDef) hSidekickUpPower;		AST(NAME("SidekickUpPower"))
	REF_TO(PowerDef) hSidekickDownPower;	AST(NAME("SidekickDownPower"))
	PowerActivationImmunities *pPowerActivationImmunities;
		// optional. Necessary for powers that are flagged to give PowerActivationImmunities

	SpecialAttribModifiers specialAttribModifiers;			AST(NAME(SpecialAttribModifiers))

	AutoAttackConfig autoAttack;							AST(NAME(AutoAttack))

	S32 *peAttribsNet;	AST(NAME(AttribsNet), SUBTABLE(AttribTypeEnum))
		// Attribs for which each Character's current value is sent to all Players.
		//  This effectively allows per-game removal of the SELF_ONLY flag on a per-Attribute basis.

	S32 *peAttribsInnateNet;	AST(NAME(AttribsInnateNet), SUBTABLE(AttribTypeEnum))
		// Attribs for which each Character's innate accrual values are sent to all Players.
		//  This allows clients to predict a target's value for the Attribute with respect to a particular Power.

	F32 fSpeedPenaltyDuringChargeDefault;
		// Default SpeedPenaltyDuringCharge to use when a PowerAnimFX doesn't specify.  Default is 0.

	F32 fSpeedPenaltyDuringActivateDefault;
		// Default SpeedPenaltyDuringActivate to use when a PowerAnimFX doesn't specify.  Default is 0.

	F32 fSpeedPenaltyRemoveDelay;
		// delay the cancelling of the speed penalty during activation via this many seconds

	F32 fCooldownGlobal;
		// General activation cooldown, which is applied when an Activation becomes current, during which
		//  further Activations are not allowed to become current.  Does not block queuing, simply stalls
		//  the queued Activation.

	F32 fCooldownGlobalQueueTime;
		// If set, this is how much time before global cooldown is up that you are able to queue global cooldown powers

	F32 fNormalizedDodgeVal;	AST(DEFAULT(1))
		// The value to which powers execute time is normalized.  The default is 1.
		//  Note: Must be greater than 0!

	F32 fNormalizedDodgeMaxFactor;
		// The maximum amount the dodge can be affected by normalization.  NOT SUPPORTED YET
		//  A value of 1 represents 100%.  0 means it does not get clamped.
		//  Formula: 1 + CLAMP( (PowerExecTime - NormVal)/NormVal,-MaxFactor, MaxFactor);
		
		//  Ex: If the normalized dodge value is 1, and a power took 3 seconds to activate, the factor would want to be
		//   1 + (Time - NormVal) / NormVal : 1 + (3-1)/1 = 3.  But it would be clamped to 2.

	F32 fKnockPitch;					AST(DEFAULT(45))
		// Degrees.  The pitch to use for a KnockBack AttribMod.

	F32 fBloodiedThreshold;
		// Percent.  A Character at or below this percent of their maximum health is
		//  considered Bloodied, which enables a PowerMode, and triggers a CombatEvent
		//  if they were not previously in that state.  If 0, the PowerMode and CombatEvent
		//  are not enabled.

	PowerInterruption eInterruptToggles;	AST(FLAGS)
		// Defines the set of PowerInterrupt types that operate on active Toggle Powers

	TargetType eSoftTargetFlags;			AST(FLAGS DEFAULT(kTargetType_Alive|kTargetType_Foe))
		// The flags to use for soft-targeting

	S32 iTrayMaxSize;									AST(DEFAULT(TRAY_SIZE_MAX))
		// The max tray size across the entire project 

	U32 iBuildTimeWaitCombat;							AST(DEFAULT(60))
		// The amount of time a player must wait to switch builds in combat
	
	U32 iBuildTimeWait;									AST(DEFAULT(10))
		// The amount of time a player must wait to switch builds out of combat

	
	F32 fFlankAngleTolerance;							AST(DEFAULT(30))
		// Size of the arc in which a character can be standing to be considered flanking with another character.
	F32 fFlankingDotProductTolerance;
	

	F32 fCameraTargetingVecTargetAssistAngle;			AST(DEFAULT(90))
		// when using camera targeting, on powers that are flagged to use this assist, 
		// the angle that which a target will be found from the player entity in the direction of the mouse reticle

	F32 fCameraTargetingVecTargetAssistDist;			AST(DEFAULT(15))
		// when using camera targeting, on powers that are flagged to use this assist, 
		// the max distance that a target will be found

	F32 fCameraTargetingVecTargetAssistDistBias;		AST(DEFAULT(0.3))
		// when using camera targeting, a bias of 0 means distance will not be factored in and just facing
		// where a bias of 1 means it will just use distance to determine what to target

	F32 fMouseLookHardTargetStickyTime;
		// if set, when in mouseLook hard targeting mode, the amount of time after no target is present that it will retain
		// your last hard target as your current hard target

	F32 fClientMouseTargetingHardTargetStickyHeuristic;
		// if set, in target_SelectUnderMouseEx when the client has a hardTarget a new target must have a higher heuristic than this 
		// in order to be selected as the new target

	F32 fMultiExecListClearTimer;						AST(DEFAULT(1))
		// The maximum time that powers can remain in the MultiExec list before being purged automatically.

	F32 fIgnoreOrClearOffscreenTargetTime;				AST(DEFAULT(1.5))
		// How long to wait before ignoring/clearing an offscreen target.

	S32 *peDisableQueuingIncludeCategories;				AST(NAME(DisableQueuingIncludeCategories) SUBTABLE(PowerCategoriesEnum))
		// If bDisablePowerQueuing is set, then affect powers of the specified categories. If this is empty, affect all powers.

	S32 *peDisableQueuingExcludeCategories;				AST(NAME(DisableQueuingExcludeCategories) SUBTABLE(PowerCategoriesEnum))
		// If bDisablePowerQueuing is set, then exclude powers of the specified categories.
	
	F32 fLungeDefaultStopDistance;						AST(DEFAULT(1.5))
		// the default distance lunges will stop at if the powerAnimFX's stop distance is negative


	S32 iPowerAnimFxBlockModeRequire;					AST(SUBTABLE(PowerModeEnum) DEFAULT(-1))
		// if a character has the PowerMode, do not play the animHitFX. 
		// TODO: This is a prototype potentially working towards an actual block feature, 
		// where right now blocking in NW is setup through multiple disjointed features. 

	S32 iSpecialClassPowerFallbackPowerSlot;			AST(DEFAULT(-1))
		// The power slot executed for the 'specialClassPower' command

	S32 iMountPowerCategory;							AST(SUBTABLE(PowerCategoriesEnum))
		// if set, the client will predict the removal of a "mount" costume as soon as 
		// it attempts to activate a power or other special activations, and then sends a command to the server to deactivate the toggle

	F32 fAspectResTrueMin;								AST(DEFAULT(-1))
		// For the attribute aspect ResFlag, the minimum the aspect is clamped to
	
	F32 fAspectResTrueMax;								AST(DEFAULT(0.8))
		// For the attribute aspect ResFlag, the maximum the aspect is clamped to
	
	U32 bDisablePowerQueuing : 1;
		// If set, disable power queuing for all powers unless specific include/exclude categories are set

	U32 bApplyModsImmediately : 1;
		// If set, apply power mods immediately instead of waiting for hit, unless delayed hit is set on a poweranim.

	U32 bUseNormalizedDodge : 1;
		// If dodge is normalized, fast acting powers are harder to dodge.
		//  Slow or highly 'charged' powers are easier to dodge.

	U32 bShieldAggroDisable : 1;
		// If damage dealt to Shield AttribMods does not notify the AI

	U32 bCritterStats : 1;
		// Set to true to enable critters being able to use the stat point system
		// defined for players

	U32 bCritterEquipment : 1;
		// Set to true to allow critters to equip items with innate powers

	U32 bFallingDamage : 1;
		// Set to true to enable simple falling damage

	U32 bToggleCooldownOnDeactivation : 1;
		// Set to true to have Toggle Powers trigger PowerCategory Cooldown upon deactivation instead
		//  of finishing initial activation.

	U32 bMoveDuringPowerActivation : 1;
		// The default rule about allowing movement during general Power activation

	U32 bMovementCancelsRootingQueuedPowers : 1;
		// when bMoveDuringPowerActivation is 0, this makes it so when moving it does not cancel queued powers

	U32 bMovementAttemptInterrupt : 1;
		// Player Characters that are attempting to move while charging their current Activation
		//  will trigger a Movement interrupt.

	U32 bFaceActivateSticky : 1;			AST(ADDNAMES(FaceDuringPowerActivation))
		// Activate facing lasts the entire activation, rather that just a short time

	U32 bFaceActivateSoft : 1;				AST(ADDNAMES(ActivationFaceSoft))
		// Causes Activate facing to stop when the player inputs movement keys

	U32 bUseLegacyFacingRules : 1;  AST(DEFAULT(1))
		// Leave this on if you want to enforce the assumptions that:
		// * Setting bDisableFaceActivate on a Character enables target arc checking for power activation
		// * Setting bDisableFaceActivate on a player Character enables checks against targetless firing

	U32 bDisableOutOfRange : 1;
		// Let players activate powers at critters that are out of range
	
	U32 bEnableOutOfRangeForPlayersIfNoTarget : 1;
		// Let players activate powers if they have no target

	U32 bRequireValidTarget : 1;
		// Restrict players from activating powers on a target that does not match main target requirements

	U32 bLoSCheckBackwards : 1;
		// Perform a backwards raycast during LoS checks when the forward raycast is successful

	U32 bTargetArcIgnoresVertical : 1;
		// Set to true to make fTargetArc checks ignore vertical differences (effectively making it a wedge)

	U32 bModAppliedPowersUseOwnerTargetType : 1;
		// Set to true to make Powers applied by AttribMods to always attempt to perform the target type
		//  test using AttribMod Owner (rather than the apply source).

	U32 bCheckMainTarget : 1;
		// Determines whether or not to call PlayerPowerTargetValidMainTarget for power targeting
	
	U32 bCheckProximityInCombat : 1;
		// If set, check proximity for all characters that are in combat and run special poweres in different situations

	U32 bSwitchCapsulesInCombat : 1;
		// If set, switch the capsule set when entering/leaving combat

	U32 bCollideWithPetsInCombat : 1;
		// If set, players will collide with their pets during combat

	U32 bAssistChecksTargetSelfFirst : 1;
		// If set, if your current target isn't valid, then it checks to see if the power can be used on yourself before checking the target's target

	U32 bBuildMinIsZero : 1;
		// If set, the minimum number of builds a Entity is allowed is 0 instead of 1 (generally for games that don't use Builds)

	U32 bExpiresWithoutPowerChecksAlive : 1;
		// If set, expire a power with ExpiresWithoutPower if the source is dead

	U32 bKnockLowAngle : 1;
		// Set to true to make KnockBack and KnockTo launch at a lower angle

	U32 bIgnoreStrEnhanceRequiresCheck : 1;
		// Set to true to ignore any requires expressions on strength enhancement mods
	
	U32 bUseCameraTargeting : 1;
		// Set to true to use the cameras facing direction as a secondary target when using powers

	U32 bCameraTargetingGetsPlayerFacing : 1;
		// Set to true to use the cameras facing direction as a secondary target when using powers

	U32 bCameraTargetingVecTargetAssistIgnoreHardTarget : 1;
		// if a powerDef has the option bUseCameraTargetingVecTargetAssist set, will ignore the entity's hard target

		
	U32 bMaintainedFullPeriods : 1;
		// Force Maintained Powers to complete each Period fully before deactivating

	U32 bForceRespawnOnMapLeave : 1;
		// Force the player to respawn when leaving a map via LeaveMapEx
		
	U32 bInfiniteSidekickRangeInstances : 1;
		// the player can be any distance and remain sidekicked when on a instance type map

	U32 bTestActDynamicEachPeriod : 1;
		// Each period of the power, check character_ActTestDynamic on the target character

	U32 bDelayAllPowerTargetingOnQueuedDefault : 1;
		// If set, before the power goes current, the client will update the targetVec 

	U32 bCursorModeTargetingUseStrictActivationTest : 1;
		// if set, will use more strict activation requirements to check whether the player should enter the cursor mode 

	U32 bAllowRechargingPowersInQueue : 1;
		// If set, characters will be able to place powers in their queue which are still on cooldown, to be activated when the cooldown expires.

	U32 bEnableInCombatAnimBit : 1;
		// If this is true, then set the 'InCombat' anim bit when a character enters combat

	U32 bLungeIgnoresCollisionCapsules : 1;
		// if set, the lunging entity will ignore other collision capsules 

	U32 bClampResFactPosToZero : 1;
		// If set, ResFactPos clamps to a minimum value of zero, allowing for negative ResFactPos modifiers.

	// PREDICTION
	//  This stuff pertains to the way the client performs prediction.  Might deserve its own structure/file.

	S32 *piPowerCategoriesAllowMispredict;					AST(SUBTABLE(PowerCategoriesEnum))
		// a list of power categories that allow for a power combo misprediction. The server will use the subpower the client used.

	U32 bClientPredictMaintained : 1;
		// Set to true to make the client predict the periodic portion of maintained powers

	U32 bClientPredictSeq : 1;
		// Set to true to make the client predict the PowerActivationRequest seq numbers

	U32 bClientPredictHit : 1;
		// Set to true to make the client predict whether its PowerApplications hit or miss the target.
		//  If this is not enabled, the client will assume it always hits.

	U32 bClientChargeData : 1;
		// Set to true to send charge state data down to the client

	U32 bCooldownPowersGetMultiExecedWhenActivated : 1;
		//When true, attempting to activate a power that's on cooldown will throw it in the multiexec list.

	U32 bPowerAnimFXChooseNodesInRangeAndArc : 1;
		// Global flag to tell all FX to choose nodes that are in range and arc powers

	U32 bCharacterClassSpecifiesStrafing : 1;
		// if set, the strafing flag will controlled via the class and not the controlScheme
	
	U32 bAreaEffectSpheresCalculateRadiusFromCombatPos : 1;
		// if set, power's sphere area effect will be calculated from the disance from the character's combat position
		// instead of the distance to the caster's capsule

	// NON-COMBAT GARBAGE
	//  Everything below here should be in its own config file somewhere else
	//  Bits first to save space

	U32 bPowerCustomizationDisabled : 1;
		// If setting Power hue and emit point is disabled for normal users

	U32 bTargetDeadEnts : 1;
		// Should we target dead entities or pets?

	U32 bTargetDeadFoes : 1;
		// Should we target dead enemies?

	U32 bCamTargetingIgnoresFriendlyPlayerOwnedEntities : 1;
		// If set, ignores entities that are owned by friendly players

	U32 bFlightDisableAutoLevel : 1;
		// Whether flight movement should not automatically level (pitch to zero) the entity when the entity isn't moving

	U32 bFlightPointAndDirectionRotationTypesIgnorePitch : 1; AST(NAME(FlightRotationIgnorePitch, FlightPointAndDirectionRotationTypesIgnorePitch))
		// when flying using Point & Direction rotation targets in flight, whether to ignore the pitch when setting the rotation or not
		// Does not ignore pitch for Input or Rotation target types.

	U32 bFlightAllRotationTargetTypesIgnorePitch : 1; 
		// When flying, all movement rotation target types will ignore pitch

	U32 bMakeMedRunSameAsFastRun : 1;
		// Defaults the medium run speed to be the same as the fast run speed, which will set off the trot run speed

	U32 bRagdollAvailable : 1;

	U32 bUseMovementManagerTurning : 1;
		// If true, uses the movement manager for turning instead of the client side 

	U32 bFallingDamageIsFatal : 1;
		// If true, falling damage will kill you.

	U32 bDiscountCostIsAbsolute : 1;
		// If true, the DiscountCost attrip is absolute and not a scalar.

	U32 bRemoveMultiExecPowersIfInvalidTarget : 1;
		//If true, when the top multiexec queue power is ready to go but has no valid target, it will be removed from the queue.
	
	U32 bEmotesUseRequester : 1;

	U32 bPowerAttribSurvivesCharDeath : 1;
		// If true, power won't be cleared when a character dies.
		

	U32 bNearDeathTargetsFaceInteractee : 1;				AST(DEFAULT(1))
			
	U32 bPlayerControlsAllowHardTarget : 1;
		// if set, will allow the player controls to respect the hard targeting. For use with shooterMode, bMouseLookHardTarget and related flags... 

	U32 bSendImmunityCombatTrackers : 1;
		// if set, will send immunities to non-damage type attribs to the client as CombatTrackerNet

	U32 bPowersAEClientViewDisabled : 1;
		// If set, do not use uiTimestampClientView for hit detection on the server

	U32 bSendCombatTrackersForOverhealing : 1;
		// 

	U32 bPlayerControlSetMoveAndFaceYawDuringPowerActivation : 1;
		// if set, circumvents a very specific portion of the player controls that only sets the moveYaw if the player is facing target during power activation. 
		// Other games might be relying on this, so making it an option. 
		// On NW the FaceYaw wasn't getting updated while activating a power causing Roll to not go relative to the current camera facing 
		// and instead go off the character's current facing

	U32 bPayPowerCostAndRechargePostHitframe : 1;
		// If set, the power cost is applied after the hit frame, and recharge is only applied if the hitframe was reached

	U32 bStopAutoForwardOnPowerActivation : 1;
		// if set will stop autoforward on the client when a power is attempted to be queued

	S32 *eaiPerceptionStealthDisabledAttribs;				AST(NAME(PerceptionStealthDisabledAttribs) SUBTABLE(AttribTypeEnum))
		// list of attribs that disable perceptionStealth when querying in character_GetPerceptionDist

	TargetingAssist eClientTargetAssist;
	
	F32 fFlightPitchClamp;
		// What the flight movement should clamp absolute pitch at, in degrees.  If zero, clamps just below 90.

	F32 fBackwardsRunScale;
		// Scalar applied to ground speed when moving backwards.

	F32 fKnockbackProneTimer; 
		// How long to be considered "Prone" after knockback is finished

	F32 fSlowMovementSpeed;
		// How fast you will move while holding a key bound to "+slow".

	F32 fStanceLingerTime; AST(DEFAULT(5.0f))
		// How long to have stances linger for after they start

	F32 fRepelSpeedAnimationThreshold;
		// how fast a repel must be before the repel animation is told to play

	F32 fDeathRespawnStandUpTime; AST(DEFAULT(2.0))
		// when the player respawns, the time it takes before it gives control back to the player (should probably line up with the animation time to "get-up" if your game does that)

	F32 fOnDeathCapsuleLingerTime; 
		// when a character dies, the time their capsule will linger before the entity goes noCollision
	
	// TACTICAL MOVEMENT
	
	CharClassTypes *peTacticalMovementClassTypes;	AST(NAME(TacticalMovementClassTypes) SUBTABLE(CharClassTypesEnum))
		// CharClassTypes that are allowed to use the TacticalMovement requester.  May eventually
		//  be moved into the CharacterClass structure as flag or additional config as necessary.

	TacticalRequesterConfig	tactical;				AST(EMBEDDED_FLAT)

	S32 iForceBuildOnRespawn;					AST(DEFAULT(-1))
		// when respawning the player force the character to respawn in the given build index
		// -1 is ignored 

	F32 fPowerCancelMoveRadius;					AST(DEFAULT(0.2f))

	F32 fFallDamageSpeedThreshold;				AST(DEFAULT(60.0f))
	F32 fFallDamageScale;						AST(DEFAULT(0.5f))

	const char *pchFallingDamagePower;			AST(NAME(FallingDamagePower) POOL_STRING)
		// If this is set, this power is applied to the entity
		// instead of the usual falling damage

	LurchConfig lurch;							AST(EMBEDDED_FLAT)
		
	// SurfaceRequester Movement Manager Settings
	U32 bSurfaceRequester_DoConstantSpeedGroundSnap : 1;
		// If set, the surface requester will snap the entity to the ground if within some hardcoded distance
		// only when using the MST_CONSTANT speed type (used for lurching)
		
	// Player Hit FX config (optional)
	PlayerHitFXConfig *pPlayerHitFXConfig;
} CombatConfig;

extern CombatConfig g_CombatConfig;

// Returns true if BattleForm is optional (based on current Map)
S32 combatconfig_BattleFormOptional(void);

// Returns the tactical disable flags set during combat
TacticalDisableFlags combatconfig_GetTacticalDisableFlagsForCombat(void);

void CombatConfig_PostCombatEvalGenerateExpressions();

bool combatConfig_FindSpecialCritAttrib(AttribType eType, 
										SA_PARAM_NN_VALID AttribType *peCritChanceOut, 
										SA_PARAM_NN_VALID AttribType *peCritSeverityOut);

