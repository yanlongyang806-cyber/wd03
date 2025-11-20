#pragma once 

#include "referencesystem.h"

typedef struct Character Character;
typedef struct Expression Expression;
typedef struct Power Power;
typedef struct PowerDef PowerDef;

AUTO_STRUCT;
typedef struct CombatPowerStateDef
{
	char	*pchName;							AST(STRUCTPARAM POOL_STRING)		
	
	const char	**ppchStickyStanceWords;		AST(NAME(StickyStanceWords) POOL_STRING)
	const char	**ppchFX;						AST(NAME(FX) POOL_STRING)
	const char	**ppchKeyword;					AST(NAME(Keyword) POOL_STRING)
	const char	**ppchStickyFX;					AST(NAME(StickyFX) POOL_STRING)

	REF_TO(PowerDef) hApplyPowerDef;			AST(STRUCTPARAM NAME(ApplyPowerDef) REFDICT(PowerDef))

	// if set, this attribute is the cost required to preform. 
	S32 eRequiredAttrib;						AST(SUBTABLE(AttribTypeEnum))

	// the amount needed to enter the state
	F32 fRequiredAttribAmountToEnter;
	
	// Expression defining the decay when activating
	Expression *pExprAttribDecayPerTick;		AST(NAME(ExprBlockAttribDecayPerTick, ExprAttribDecayPerTickBlock), REDUNDANT_STRUCT(ExprCost, parse_Expression_StructParam), LATEBIND)

	// if set, if the character has the given mode on them, don't allow activation
	S32 iDisallowedPowerMode;					AST(SUBTABLE(PowerModeEnum) DEFAULT(-1))
	
	// expression run on update, if returns true then the mode will be exited
	Expression *pExprPerTickExitMode;			AST(NAME(ExprBlockPerTickExitMode, ExprPerTickExitModeBlock), REDUNDANT_STRUCT(ExprPerTickExitMode, parse_Expression_StructParam), LATEBIND)
	
	// if set, disallows users to exit state manually
	U32 bDisallowUserExitState : 1;

} CombatPowerStateDef;

AUTO_STRUCT;
typedef struct CombatPowerStatePower
{
	REF_TO(PowerDef) hPowerDef;					AST(STRUCTPARAM NAME(Power) REFDICT(PowerDef))
	
	char*	pchState;							AST(STRUCTPARAM POOL_STRING)
} CombatPowerStatePower;


AUTO_STRUCT;
typedef struct CombatPowerStatePowerSet
{
	CombatPowerStatePower	**eaPowers;			AST(NAME(Power))
	
	REF_TO(PowerDef) hBasePowerDef;				AST(STRUCTPARAM NAME(BasePower) REFDICT(PowerDef))

} CombatPowerStatePowerSet;

AUTO_STRUCT;
typedef struct CombatPowerStatePowerSlot
{
	S32		iPowerSlot;

	char*	pchState;							AST(POOL_STRING)
} CombatPowerStatePowerSlot;

AUTO_STRUCT;
typedef struct CombatPowerStatePowerslotSet
{
	CombatPowerStatePowerSlot	**eaPowerSlots;	AST(NAME(PowerSlot))

	S32			iBasePowerSlot;

} CombatPowerStatePowerslotSet;


AUTO_STRUCT;
typedef struct CombatPowerStateSwitchingDef
{
	char* pchName;									AST(STRUCTPARAM KEY POOL_STRING)		

	char* pchFilename;								AST(CURRENTFILE)

	CombatPowerStateDef			**eaStates;			AST(NAME(State))
	
	CombatPowerStatePowerSet	**eaPowerSet;		AST(NAME(PowerSet))
	
	CombatPowerStatePowerslotSet **eaPowerSlotSet;	AST(NAME(PowerSlotSet))

	// if set, only powers that are slotted in the given slot index will be allowed for state switching.
	S32 iSpecialPowerSwitchedSlot;					AST(DEFAULT(-1))

	F32 fSwitchCooldown;

	// if set, will not be available until the character hits this level
	S32 iCombatLevelLockout;
	
	// if set states cannot be cycled and must be entered directly 
	U32 bDisallowStateCycling : 1;	

	// if set, will attempt to dismount 
	U32 bDismountOnStateSwitch : 1;

	// list of attribs that disable state switching
	S32 *eaiDisableActivationAttribs;				AST(NAME(DisableActivationAttribs) SUBTABLE(AttribTypeEnum))
		
	// if set, will not be able to state switch while knocked
	U32 bDisableActivationWhileKnocked : 1;


} CombatPowerStateSwitchingDef;

typedef enum ECombatPowerStateModeFailReason 
{
	ECombatPowerStateModeFailReason_NONE,
	
	ECombatPowerStateModeFailReason_COST,
	
	ECombatPowerStateModeFailReason_DISALLOWEDMODE,

	ECombatPowerStateModeFailReason_KNOCKED,

	ECombatPowerStateModeFailReason_DISABLEDATTRIB,

} ECombatPowerStateModeFailReason;

AUTO_STRUCT;
typedef struct CombatPowerStateSwitchingInfo
{
	const char *pchCurrentState;						NO_AST //AST(POOL_STRING)

	U8		uCurActIdOffset;							NO_AST

	F32		fCooldown;
	
	//the reference to the combatblock def
	REF_TO(CombatPowerStateSwitchingDef) hCombatPowerStateSwitchingDef;

	// cached from def
	S32		iSpecialPowerSwitchedSlot;

	U32		uAutoAttackLastPowerID;						NO_AST
	const char *pchLastState;							NO_AST
		
} CombatPowerStateSwitchingInfo;


Power* CombatPowerStateSwitching_GetSwitchedStatePower(Character *pChar, Power *pPower);

// Given the power, will return the base power (the combo parent if there is one)
// Will return NULL if the power does not have any combat Power state powers
Power* CombatPowerStateSwitching_GetBasePower(Power *pPower);


void CombatPowerStateSwitching_CreateModePowers(Character *pChar, Power *pPow);
void CombatPowerStateSwitching_FixSubPowers(Character *pChar, Power *pPow);

void CombatPowerStateSwitching_InitCharacter(Character *pChar, const char *pszDef);
void CombatPowerStateSwitching_Update(Character *pChar, F32 fRate);

// pchMode is assumed to be a pooled string
CombatPowerStateDef* CombatPowerStateSwitching_GetStateByName(const char *pchMode, CombatPowerStateSwitchingDef *pDef);


void CombatPowerStateSwitching_EnterState(	Character *pChar, 
											CombatPowerStateSwitchingInfo *pState, 
											CombatPowerStateDef *pModeDef, 
											U32 uiStartTime);

void CombatPowerStateSwitching_ExitState(Character *pChar, 
										CombatPowerStateSwitchingDef *pDef, 
										CombatPowerStateSwitchingInfo *pState, 
										CombatPowerStateDef *pModeDef, 
										U32 uiEndTime);

S32 CombatPowerStateSwitching_CanStateSwitch(Character *pChar, CombatPowerStateSwitchingDef *pDef, ECombatPowerStateModeFailReason *peReason);

S32 CombatPowerStateSwitching_CanEnterState(Character *pChar, CombatPowerStateDef *pMode, 
											SA_PARAM_OP_VALID ECombatPowerStateModeFailReason *peReason);

S32 CombatPowerStateSwitching_GetSwitchedStatePowerSlot(Character *pChar, S32 iSlot);

S32 CombatPowerStateSwitching_GetCorrespondingStatePowerSlot(Character *pChar, 
															 SA_PARAM_OP_VALID const char *pchNewState, 
															 SA_PARAM_OP_VALID const char *pchOldState, 
															 S32 iOldSlot);


S32 CombatPowerStateSwitching_DoesPowerMatchMode(SA_PARAM_OP_VALID Power *pPower, const char *pchModeName);

S32 CombatPowerStateSwitching_IsPowerSlottedInSet(	SA_PARAM_NN_VALID Character *pChar, 
													SA_PARAM_NN_VALID CombatPowerStateSwitchingDef *pDef, 
													SA_PARAM_OP_VALID Power *pPower, 
													const char *pchModeName);

S32 CombatPowerStateSwitching_IsPowerIDSlottedInSet(	SA_PARAM_NN_VALID Character *pChar, 
														SA_PARAM_NN_VALID CombatPowerStateSwitchingDef *pDef, 
														U32 powerID, 
														const char *pchModeName);