#pragma once

typedef struct Character Character;
typedef struct PowerActivation PowerActivation;

bool gslCombatDeathPrediction_IsEnabled();
void gslCombatDeathPrediction_DeathPredictionTick(int iPartitionIdx, SA_PARAM_NN_VALID Character *pChar);
void gslCombatDeathPrediction_NotifyPowerCancel(int iPartition, SA_PARAM_NN_VALID Character *pChar, SA_PARAM_NN_VALID PowerActivation *pAct);
void gslCombatDeathPrediction_Tick();
void gslCombatDeathPrediction_PartitionUnload(int iPartition);
