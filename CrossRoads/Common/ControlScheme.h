#pragma once
GCC_SYSTEM
/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

typedef struct Player Player;
typedef struct Entity Entity;
typedef struct Message Message;
typedef struct DefineContext DefineContext;
typedef struct DisplayMessage DisplayMessage;

#include "Message.h"
#include "Referencesystem.h"
#include "combatenums.h"

#define DEFAULT_MOUSE_LOOK_HARD_TARGET_RADIUS 0.1

extern DefineContext *g_pDefineControlScehemeRegions;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineControlScehemeRegions);
typedef enum ControlSchemeRegionType
{
	kControlSchemeRegionType_None = 0,	ENAMES(None)
	kControlSchemeRegionType_MAX,		EIGNORE
} ControlSchemeRegionType;

extern StaticDefineInt ControlSchemeRegionTypeEnum[];

AUTO_STRUCT;
typedef struct ControlSchemeRegionInfo
{
	ControlSchemeRegionType eType;						NO_AST
	DisplayMessage	DisplayMsg;							AST(NAME(DisplayMessage) STRUCT(parse_DisplayMessage))
	const char*		pchName;							AST(NAME(Name) POOL_STRING STRUCTPARAM KEY)
	const char**	ppchAllowedSchemes;					AST(NAME(AllowScheme) POOL_STRING)
	const char*		pchFilename;						AST(CURRENTFILE)
	bool			bEnableAlwaysShowOverheadOption;	AST(NAME(EnableAlwaysShowOverheadOption))
} ControlSchemeRegionInfo;

AUTO_STRUCT;
typedef struct ControlSchemeRegions
{
	ControlSchemeRegionInfo** eaSchemeRegions;	AST(NAME(SchemeRegion))
} ControlSchemeRegions;

// Specifies which camera type should be used.
AUTO_ENUM;
typedef enum CameraType
{
	kCameraType_Free = 0, ENAMES(Free OverShoulder)
		// The camera's orientation is independent of the character's facing.

	kCameraType_Follow, ENAMES(Follow TargetLock)
		// The camera follows the target - it tries to keep the target on the screen at all times.

	kCameraType_Chase, ENAMES(Chase)
		// The camera's orientation is dependent on the characters's facing

	kCameraType_Count,			EIGNORE
} CameraType;

AUTO_ENUM;
typedef enum CameraFollowType
{
	kCameraFollowType_OnMove = 0,
	
	kCameraFollowType_Never,

	kCameraFollowType_NoSnap,

	kCameraFollowType_Count,			EIGNORE
} CameraFollowType;

// Specifies how to handle tab targeting
AUTO_ENUM;
typedef enum TargetOrder
{
	kTargetOrder_NearToFar = 0,
		// Start with nearest, tab to farthest

	kTargetOrder_LeftToRight,
		// Go from left to right in screen space

	kTargetOrder_NearestToCenter,
		// Go from nearest to center of camera, and head out

	kTargetOrder_OnlyCenter,
		// Only the center of the screen

	kTargetOrder_LuckyCharms,
		// Start with high-priority lucky charms.

	kTargetOrder_Count,			EIGNORE

} TargetOrder;

// Specifies AutoAttack types, specifically when they activate and deactivate
AUTO_ENUM;
typedef enum AutoAttackType
{
	kAutoAttack_None = 0,
	// No auto attack

	kAutoAttack_Toggle,
	// Using default attack will toggle auto attack on and off, and is cancellable by changing target

	kAutoAttack_ToggleNoCancel,
	// Using default attack will toggle auto attack on and off, isn't cancellable

	kAutoAttack_ToggleCombat,
	// Using default attack will toggle auto attack on and off, and is cancelled when not in combat
	
	kAutoAttack_Maintain,
	// Using default attack will enable auto attack while it is held down

	kAutoAttack_Count,			EIGNORE
} AutoAttackType;

AUTO_ENUM;
typedef enum PowerCursorActivationType
{
	kPowerCursorActivationType_ActivateOnSecondPress,
		// first press turns on the mode, second press activates the power

	kPowerCursorActivationType_ActivateOnRelease,
		// press turns on the mode, releasing will activate the power

	kPowerCursorActivationType_Count,	EIGNORE

} PowerCursorActivationType;


AUTO_STRUCT AST_CONTAINER AST_IGNORE(Tags) AST_IGNORE(bDisableCameraShake) AST_IGNORE(bAlwaysFaceTarget) AST_IGNORE(iMaxTabTargetDist) AST_IGNORE(fTargetCone) AST_IGNORE(bTargetArcAppliesToActivation);
typedef struct ControlScheme
{
	STRING_POOLED pchName;					AST(STRUCTPARAM PERSIST NO_TRANSACT POOL_STRING)
		// The name of the control scheme

	REF_TO(Message) hNameMsg; AST(REQUIRED NON_NULL_REF__ERROR_ONLY PERSIST NO_TRANSACT)
		// Translated name of the scheme

	int iVersion;							AST(PERSIST NO_TRANSACT)
		// If the one in the data file is newer than the one saved on character, reset the character

	// This block is modifiable on the options menu

	CameraType eCameraType;					AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Sets how the camera operates

	CameraFollowType eCameraFollowType;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// How the camera operates in terms of auto-orienting 

	TargetOrder eTabTargetOrder;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Describes how tabbing should work

	TargetingAssist	eTargetAssistOverride;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) DEFAULT(-1))
		// overrides the entry in combatconfig if set

	float fTabTargetMaxDist;				AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// The maximum distance tab targeting is allowed to target

	float fAutoTargetMaxDist;				AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// The maximum distance tab targeting is allowed to target

	TargetOrder eInitialTargetOrder;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Describes how it should pick the initial soft target

	AutoAttackType eAutoAttackType;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// The status of auto attack

	PowerCursorActivationType ePowerCursorActivationType;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))

	bool bStrafing;					AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If true, the character always faces into the screen. The character
		//   will strafe left and right, and will backpedal.
		// If false, the character turns and faces the direction of the movement.

	bool bSoftTarget;				AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If true, continuously select and show the target that will be used
		//   when the player uses a power.

	bool bAutoHardTarget;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Automatically sets the auto-chosen target to be the hard target
		//   when a power is executed on it.

	bool bForceCameraNearOffset;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Forces the camera to use the near offset regardless of regionrules.

	bool bAutoHardTargetIfNoneExists;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Automatically sets the auto-chosen target to be the hard target
		//   when a power is executed on it, but only if you don't already
		//	have a hard target.

	bool bSnapCameraOnAttack;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Rather we should snap the camera to face target when you attack something

	bool bDisableFaceSelected;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) ADDNAMES(bDisableFaceTarget))
		// True if the player should not automatically turn toward his selected target

	bool bRequireHardTargetToExec;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Don't execute a power unless you already had a hard target.

	bool bCancelTargetOnOffClick;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Cancels the current target if you click on nothing

	bool bDeselectIfOffScreen;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Deselect your target if it is off screen

	bool bTargetObjectsLast;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Only tab and auto target objects if there are NO valid enemies

	bool bTargetThreateningFirst;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Tab and auto target will target threatening enemies first (enemies who have aggro)

	bool bNeverTargetObjects;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Tab and auto-targeting will NEVER target objects

	bool bNeverTargetPets;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Tab and auto-targeting will never target pets

	bool bResetTabOverTime;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Reset your tab target if it's been a while since you tabbed

	bool bMeleeIgnoreBadHardTarget; AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Ignore your current hard target if you're trying to execute a power that won't work on them

	bool bAssistTargetIfInvalid;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Attack your target's target, if your current target is the wrong type

	bool bShowMouseLookReticle;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Show mouse look reticle

	bool bMouseLookHardTarget;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// When using mouse look, hard target an entity with x and y screen positions

	bool bMouseLookHardTargetExcludeCorpses;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// When using mouse look hard target, exclude dead entities.

	bool bMouseLookInteract;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// When using mouse look, use the selected intractable object as the default interaction

	bool bTargetAttackerIfAttacked;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Automatically target attacker if you don't currently have a target

	bool bTabTargetOffscreen;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Tab targeting can select targets offscreen

	bool bStopMovingOnInteract;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Disable auto-forward if the interact command is executed

	bool bKeepMovingDuringContactDialogs;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Do not disable auto-forward/throttle when a contact dialog comes up

	bool bTurningTurnsCameraWhenFacingTarget;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) DEFAULT(1))
		// If false, the turning keys become strafe when facing a target

	bool bDoubleTapDirToRoll;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If true, pressing a movement direction will perform "+roll"

	bool bAimModeAsToggle;				AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If true, aim mode will be toggled when a key is pressed. Otherwise it will activate while held

	bool bUseZoomCamWithAimMode;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If true, zoom cam is automatically turned on when aim mode is activated

	bool bTargetLockCamDisableWhenNoTarget;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If set and camera type is "targetlock", don't follow the player's movements while no target is selected

	bool bDelayAutoAttackUntilCombat;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If set, don't execute auto attack if you are out of combat. Slightly different then the no-combat mode above it's enabled and just doesn't execute

	bool bInvertMouseX;					AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If set, invert the mouse X axis of the camera

	bool bInvertMouseY;					AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If set, invert the mouse Y axis of the camera

	bool bCameraShake;					AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) DEFAULT(1))
		// Camera shake

	bool bDisableDefaultOffset;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) DEFAULT(1))
		// Disables STO's forced camera offsets

	F32 fAutoAttackDelay;				AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// If set, delay the activation of auto attack via this many seconds

	F32 fCamDistance;					AST(NAME(iCamDistance, CamDistance) PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// How far away the camera should be.

	F32 fCamMaxDistance;				AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// The max distance the camera can be from the player

	F32 fCamMouseLookSensitivity;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// The scale of mouse sensitivity

	F32 fCamControllerLookSensitivity;	AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// The controller camera sensitivity

	CameraType eLastCameraType;			AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1))
		// Used to store the player set camera type in the case of temporary camera type changes

	F32 iCamHeight;						AST(PERSIST NO_TRANSACT NAME(fCamHeight))
		// How high the camera should be.

	F32 fCamOffset;						AST(PERSIST NO_TRANSACT)
		// How high the camera should be.

	U32 iLockCamPitch;					AST(PERSIST NO_TRANSACT)
		// Whether or not the pitch of the camera should be locked

	U32 bUseThrottle : 1;				AST(PERSIST NO_TRANSACT)
		// Use the set throttle and go forward when it's not zero. (flight only)

	U32 bUseOffsetRotation : 1;			AST(PERSIST NO_TRANSACT)
		// Determines if roll happens from the root or from 3 feet above the root. (flight only)

	U32 bEnableShooterCamera : 1;		AST(PERSIST NO_TRANSACT)
		// If set, the camera will act more like a first person shooter

	U32 bEnableClickToMove	:	1;		AST(PERSIST NO_TRANSACT)
	U32 bAutoFaceHostileTargetsOnly	:	1;		AST(PERSIST NO_TRANSACT)

	F32 fMouseLookHardTargetRadiusAim;	AST(DEFAULT(DEFAULT_MOUSE_LOOK_HARD_TARGET_RADIUS))
		// The radius to use when in aim mode

	F32 fTargetMaxAngleFromPlayerFacing;
		// The max angle a target can reside from the player's facing when performing client targeting checks

	F32 fDefaultActiveWeaponRange;
		// If no active weapon is found, use this distance

	F32 fPitchDiffMultiplier; AST(DEFAULT(0.3))
		// Controls how quickly the player looks up and down

	U32 bDisablePowerQueuing : 1;
		// If this is set, do not queue powers

	U32 bUseFacingPitch : 1;
		// Use the character's facing pitch when getting facing direction
	
	U32 bCheckActiveWeaponRangeForTargeting : 1;
		// Get ranges of powers in the player's active weapon bag when targeting

	U32 bRequireValidTarget : 1;
		// Override for RequireValidTarget in CombatConfig

	U32 bDisableFaceActivate : 1;
		// Disables facing the target when powers are activated

	U32 bUseCameraTargeting : 1;
		// Allows the control scheme to enable camera targeting

	U32 bGetAttackAngleWhenDamaged : 1;
		// Get the attack angle when taking damage (for offscreen damage FX)

	U32 bAutoUnholster : 1;
		// Automatically unholster the player's weapon when switching to this control scheme

	U32 bDisableTrayAutoAttack : 1;
		// Disable tray auto-attack information for this scheme

	U32 bCameraTargetingUsesDirectionKeys : 1;
	U32 bCameraTargetingGetsPlayerFacing : 1;
	U32 bAutoCamMouseLook : 1;

	U32 bDebug : 1;
		// If set, this scheme is only selectable in dev mode or if the player has debug access level



	F32 fCamStartingPitch;				AST(PERSIST NO_TRANSACT)
		// This is the starting pitch of the camera, however, it's only used if the pitch is locked.

	F32 fMouseLookHardTargetX;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) DEFAULT(0.5f))
	// Value 0 - 1.0. Use with bMouseLookHardTarget

	F32 fMouseLookHardTargetY;		AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) DEFAULT(0.5f))
	// Value 0 - 1.0. Use with bMouseLookHardTarget

	F32 fMouseLookHardTargetRadius; AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_1) DEFAULT(DEFAULT_MOUSE_LOOK_HARD_TARGET_RADIUS))
	// Value 0 - 1.0. Use with bMouseLookHardTarget. Calculated baised on the width of the screen

	STRING_POOLED pchKeyProfileToLoad;	AST(STRUCTPARAM PERSIST NO_TRANSACT POOL_STRING USERFLAG(TOK_USEROPTIONBIT_1))
		// The name of the key bind profile to load

	const char** ppchAllowedKeyProfiles; AST(NAME(AllowKeyProfile))
		// Optional list of key profiles that are valid for this scheme. If empty, every profile is valid.

	AutoAttackType* peAllowedAutoAttackTypes; AST(NAME(AllowAutoAttackType) SUBTABLE(AutoAttackTypeEnum))
		// If specified, these are the allowed auto-attack types for this control scheme. If empty, allow all auto attack types.
} ControlScheme;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(iRegion);
typedef struct ControlSchemeRegion
{
	ControlSchemeRegionType		eType;		AST(PERSIST NO_TRANSACT KEY SUBTABLE(ControlSchemeRegionTypeEnum))
	STRING_POOLED				pchScheme;	AST(PERSIST NO_TRANSACT POOL_STRING)
} ControlSchemeRegion;

AUTO_STRUCT AST_CONTAINER;
typedef struct ControlSchemes
{
	int iVersion; AST(PERSIST NO_TRANSACT)
		// Incremented whenever the struct is updated so the client can tell
		//   when to update.

	STRING_POOLED pchCurrent; AST(PERSIST NO_TRANSACT POOL_STRING)
		// Index of the currently chosen scheme

	ControlSchemeRegion **eaSchemeRegions; AST(NAME(SchemeRegion) PERSIST NO_TRANSACT)
		// List of scheme-to-region associations (what scheme to load for what region).

	ControlScheme **eaSchemes; AST(NAME(Scheme) PERSIST NO_TRANSACT)
		// List of schemes. May be empty or sparse. If a requested scheme is
		//   isn't present, a default one is used.
} ControlSchemes;

extern ParseTable parse_ControlSchemes[];
#define TYPE_parse_ControlSchemes ControlSchemes
extern ParseTable parse_ControlScheme[];
#define TYPE_parse_ControlScheme ControlScheme
extern ParseTable parse_ControlSchemeTagNames[];
#define TYPE_parse_ControlSchemeTagNames ControlSchemeTagNames

extern ControlSchemeRegions g_ControlSchemeRegions;
extern ControlSchemes g_DefaultControlSchemes;

ControlScheme *schemes_FindScheme(ControlSchemes *pSchemes, const char *pchName);
	// Finds the names scheme in a set. Returns NULL if not found.
ControlScheme *schemes_FindCurrentScheme(ControlSchemes *pSchemes);
	// Finds the current scheme in a set. Returns NULL if not found.
ControlSchemeRegionInfo* schemes_GetSchemeRegionInfo(ControlSchemeRegionType eSchemeRegion);
	// Finds the current scheme region info in the array. Returns NULL if not found.
ControlSchemeRegionType getSchemeRegionTypeFromRegionType(S32 eRegionType);
	// Gets the ControlSchemeRegionType from a WorldRegionType
S32 schemes_GetAllSchemeRegions(void);
	// Gets all regions as a bit field
bool schemes_IsSchemeSelectable(Entity* pEnt, ControlScheme* pCurrScheme);
	// Returns whether or not this scheme can be selected by the player
ControlScheme* schemes_FindNextSelectableScheme(Entity* pEnt, ControlSchemes* pSchemes, const char* pchCurrent);
	// Find the next scheme in a list of schemes
bool schemes_DisableTrayAutoAttack(Entity* pEnt);
	// Returns whether or not the current scheme allows tray auto-attack
/* End of File */

