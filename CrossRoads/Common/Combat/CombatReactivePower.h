#pragma once

#include "referencesystem.h"

typedef struct Character Character;
typedef struct Expression Expression;
typedef struct PowerDef PowerDef;
typedef struct Power Power;
typedef struct TacticalRequesterRollDef TacticalRequesterRollDef;
extern StaticDefineInt ItemCategoryEnum[];


AUTO_STRUCT;
typedef struct CombatReactivePowerAnimFx
{
	const char **ppchStickyStanceWords;			AST(NAME(StickyStanceWords) POOL_STRING)

	const char **ppchPreactivateFX;				AST(NAME(PreactivateFX) POOL_STRING)

	const char **ppchPreactivateAnimKeyword;	AST(NAME(PreactivateAnimKeyword), POOL_STRING)

	const char **ppchActivateKeyword;			AST(NAME(ActivateKeyword) POOL_STRING)

	const char **ppchActivateStickyFX;			AST(NAME(ActivateStickyFX) POOL_STRING)

	const char **ppchBlockFx;					AST(NAME(BlockFX) POOL_STRING)

} CombatReactivePowerAnimFx;


AUTO_STRUCT;
typedef struct CombatReactivePowerDef
{
	char* pchName;								AST(STRUCTPARAM KEY POOL_STRING)		

	char* pchFilename;							AST(CURRENTFILE)

	// the time it takes before the blocking actually kicks in
	F32	fPreactivateTime;

	// the time it takes before the blocking actually kicks in
	F32	fDeactivateTime;

	// the scale of speed once we start blocking
	F32 fActivateSpeedScale;					AST(DEFAULT(-1))

	// the scale of speed before activating 
	F32 fPreactivateSpeedScale;					AST(DEFAULT(-1))
	
	// the animation and FX def
	CombatReactivePowerAnimFx	animFx;			AST(NAME(AnimFX))
	
	// the power that is activated when the starting block
	REF_TO(PowerDef) hActivatePowerDef;			AST(NAME(ActivatePowerDef) REFDICT(PowerDef))


	const char *pchAttribPoolName;				AST(NAME(AttribPoolName) POOL_STRING)

	// if set, the attribPool will get its timer delayed for this time after deactivating 
	// it will also disallow the attribPool from regenerating while active
	F32 fAttribPoolPostDelayTimer;

	const char *pchCombatPowerState;			AST(NAME(CombatPowerState) POOL_STRING)
	
	// if set, this attribute is the cost required to preform. 
	S32 eCostAttrib;							AST(SUBTABLE(AttribTypeEnum))

	// if this is set, the power is locked out until the cost attrib reaches the max
	S32 eMaxCostAttrib;							AST(SUBTABLE(AttribTypeEnum))

	// only valid if eCostAttrib is set. The amount that is necessary to activate and will be deducted from the character attrib
	F32 fRequiredAmountToActivate;
	
	// The cost deducted from the eCostAttrib every combat tick if the character is moving. Must be positive
	// if fAttribPoolPostDelayTimer is set, it is only applied if moving. 
	Expression *pExprMovementAttribCost;		AST(NAME(ExprBlockMovementAttribCost), REDUNDANT_STRUCT(MovementAttribCost, parse_Expression_StructParam), LATEBIND)

	// only valid if eCostAttrib is set. The initial cost deducted when activating
	F32 fInitialActivationAttribCost;

	// if set, the activating character needs an item category in order to perform
	S32 *peRequiredItemCategory;				AST(NAME(RequiredItemCategory) SUBTABLE(ItemCategoryEnum))

	// when failing due to RequiredItemCategory, this message key will override the default 
	const char *pchRequiredItemCategoryErrorMessageKey; AST(POOL_STRING)

		
	// if set, if the character has the given mode on them, don't allow activation
	S32 iDisallowedPowerMode;					AST(SUBTABLE(PowerModeEnum) DEFAULT(-1))

	// if set, will not be available until the character hits this level
	S32 iCombatLevelLockout;

	// if this reactive power uses roll
	TacticalRequesterRollDef *pRoll;

	// a list of power categories that allow the power to be canceled at any time
	S32 *piAllowCancelPowerCategory;			AST(SUBTABLE(PowerCategoriesEnum))
		
	// list of attribs that disable activation
	S32 *eaDisabledAttribs;						AST(NAME(DisablingAttribs) SUBTABLE(AttribTypeEnum))
			
	// the time the character will be in the combat visuals state
	F32 fInCombatTimer;

	// 
	F32 fCooldown;
	
	// if set, jump is disabled
	U32 bDisablesJump : 1;

	// if set, strafing is enabled 
	U32 bEnableStrafing : 1;
		
	// if set, hit reaction FX will be ignored while activated
	U32 bIgnoreHitFxDuringActivation : 1;

	// if set, will disallow power activation during the reactive activation
	U32 bDisallowPowerActivation : 1;
	
	// if set, activing a power will deactivate the reactive power
	U32 bPowerActivationDeactivates : 1;

	// if set, the reactive power can be activated via double tap if g_CurrentScheme.bDoubleTapDirToRoll is set.
	// if the def specifies TacticalRequesterRollDef, this ignored and defaults to on
	U32 bCanActivateByDoubleTap : 1;

	// if set, disables autoattack on activation
	U32 bActivatingDisablesAutoAttack : 1;	AST(DEFAULT(1))
	
} CombatReactivePowerDef;

typedef enum ECombatReactivePowerState
{
	ECombatReactivePowerState_NONE = 0,
	ECombatReactivePowerState_QUEUED_ACTIVATE,
	ECombatReactivePowerState_PREACTIVATE,
	ECombatReactivePowerState_ACTIVATED,

	ECombatReactivePowerState_QUEUE_DEACTIVATE,
	ECombatReactivePowerState_DEACTIVATING,
	ECombatReactivePowerState_COUNT
} ECombatReactivePowerState;

#define COMBAT_REACTIVE_BLOCK_ACTID_OFFSET_MAX	10

AUTO_STRUCT;
typedef struct CombatReactivePowerInfo
{
	F32		fTimer;						NO_AST

	U32		uiStateTransitionTime;		NO_AST

	F32		fActivateYaw;				NO_AST
	
	U32		uiQueuedTime;				NO_AST

	S32		eState;						NO_AST
	
	S32		bAppliedPower;				NO_AST

	U32		uiQueuedDeactivateTime;		NO_AST

	U32		uiQueuedActivateTime;		NO_AST

	U32		bLockedOutUntilMaxCost;		NO_AST
	
	U32		iQueuedInputValue;			NO_AST
		
	F32		fCooldown;					NO_AST

	U8		uCurActIdOffset;			NO_AST
	
	//the reference to the combatblock def
	REF_TO(CombatReactivePowerDef)		hCombatBlockDef;
	
	U32		bCancelledPowers : 1;		NO_AST

	// cached from def
	U32		bIgnoreHitFxDuringActivation : 1; NO_AST

	U32		bHandlesDoubleTap : 1; NO_AST
	
} CombatReactivePowerInfo;


typedef enum ECombatReactiveActivateFailReason 
{
	ECombatReactiveActivateFailReason_NONE,

	ECombatReactiveActivateFailReason_POWER_ACTIVATION,

	ECombatReactiveActivateFailReason_COST,

	ECombatReactiveActivateFailReason_COSTRECHARGE,

	ECombatReactiveActivateFailReason_KNOCKED,
	
	ECombatReactiveActivateFailReason_ITEM_CATEGORY,
	
	ECombatReactiveActivateFailReason_DISABLED,

	ECombatReactiveActivateFailReason_DISALLOWEDMODE,
	
	ECombatReactiveActivateFailReason_DEAD,

	ECombatReactiveActivateFailReason_NEARDEATH,

	ECombatReactiveActivateFailReason_OTHER,

} ECombatReactiveActivateFailReason;

void CombatReactivePower_InitCharacter(Character *pChar, const char *pszDef);

void CombatReactivePower_Update(Character *pChar, F32 fRate);

S32 CombatReactivePower_CanStartActivation(	Character *pChar, 
											CombatReactivePowerInfo *pInfo, 
											CombatReactivePowerDef *pDef, 
											SA_PARAM_OP_VALID ECombatReactiveActivateFailReason *peFailOut);

S32 CombatReactivePower_CanActivate(Character *pChar, CombatReactivePowerDef *pDef, SA_PARAM_OP_VALID ECombatReactiveActivateFailReason *peFailOut);

S32 CombatReactivePower_IsActive(Character *pChar);

//void CombatReactivePower_ScheduleDisable(Character *pChar, U32 id, U32 spc);
//void CombatReactivePower_ScheduleDisableStop(Character *pChar, U32 id, U32 spc);

bool CombatReactivePower_Begin(	Character *pChar, 
								CombatReactivePowerInfo *pInfo, 
								CombatReactivePowerDef *pDef, 
								F32 fActivateYaw,
								U32 uiStartTime,
								F32 fTimeOffset);

void CombatReactivePower_Stop(	Character *pChar, 
									CombatReactivePowerInfo *pInfo, 
									CombatReactivePowerDef *pDef, 
									U32 uiStartTime, 
									bool bImmediate);

S32 CombatReactivePower_ShouldPlayHitFx(Character *pChar);

S32 CombatReactivePower_CanActivatePowerDef(SA_PARAM_NN_VALID Character *pChar, SA_PARAM_NN_VALID Power *pPow);

S32 CombatReactivePower_CanToggleDeactivate(CombatReactivePowerDef *pDef);

bool CombatReactivePower_IsMoving(Character *pChar);
