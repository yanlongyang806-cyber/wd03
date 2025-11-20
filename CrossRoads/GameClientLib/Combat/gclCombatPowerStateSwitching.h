#pragma once 

typedef struct Character Character; 
typedef struct CombatPowerStateDef CombatPowerStateDef; 
typedef struct CombatPowerStateSwitchingDef CombatPowerStateSwitchingDef; 
typedef struct CombatPowerStateSwitchingInfo CombatPowerStateSwitchingInfo; 


void gclCombatPowerStateSwitching_CycleNextState(Character *pChar);
S32 gclCombatPowerStateSwitching_CanCycleState(Character *pChar);
void gclCombatPowerStateSwitching_EnterStateByName(Character *pChar, const char *pchMode);
void gclCombatPowerStateSwitching_ExitCurrentState(Character *pChar);

void gclCombatpowerStateSwitching_OnEnter(Character *pChar, 
											CombatPowerStateSwitchingInfo *pState);

void gclCombatpowerStateSwitching_OnExit(	Character *pChar, 
											CombatPowerStateSwitchingDef *pDef, 
											CombatPowerStateSwitchingInfo *pState, 
											CombatPowerStateDef *pModeDef);