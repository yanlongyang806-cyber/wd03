#pragma once


typedef struct Character Character;
typedef struct CombatReactivePowerInfo CombatReactivePowerInfo;
typedef struct CombatReactivePowerDef CombatReactivePowerDef;
typedef enum MovementInputValueIndex MovementInputValueIndex;

void gclCombatReactivePower_Exec(bool bActive);

void gclCombatReactivePower_Deactivate(Character *pChar);
void gclCombatReactivePower_Activate(Character *pChar);

void gclCombatReactivePower_UpdateNoneState(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef);

void gclCombatReactivePower_OnStopBlock(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef);

bool gclCombatReactivePower_ShouldTrapMovementInput(MovementInputValueIndex input, S32 bOn);

void gclCombatReactivePower_State_QueuedActivate_Cancel(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef);

void gclCombatReactivePower_State_Preactivate_Cancel(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef);

void gclCombatReactivePower_State_Activated_Cancel(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef);

void gclCombatReactivePower_State_Preactivate_Enter(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef);

bool gclCombatReactivePower_HandleDoubleTap(MovementInputValueIndex input);


void gclCombatReactivePower_UpdateActivated(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef);
