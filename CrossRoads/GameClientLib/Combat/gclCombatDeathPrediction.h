#pragma once

typedef struct PowerActivation PowerActivation;
typedef struct Character Character;

void gclCombatDeathPrediction_Shutdown();
void gclCombatDeathPrediction_OncePerFrame();
void gclCombatDeathPrediction_NotifyActCanceled(Character *pChar, const PowerActivation *pAct);

S32 gclCombatDeathPrediction_IsDeathPredicted(Entity *pEnt);