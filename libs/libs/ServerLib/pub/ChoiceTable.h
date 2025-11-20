//// Server specific code for Choice Tables.
////
//// Main documentation is in ChoiceTable_common.h.
#pragma once
GCC_SYSTEM

typedef struct ChoiceTable ChoiceTable;
typedef struct WorldVariable WorldVariable;

SA_RET_NN_VALID WorldVariable* choice_ChooseValue(SA_PARAM_NN_VALID ChoiceTable* table, const char* name, int timedRandomIndex, U32 seed, U32 uiTimeSecsSince2000);
SA_RET_NN_VALID WorldVariable* choice_ChooseRandomValue(SA_PARAM_NN_VALID ChoiceTable* table, const char* name, int timedRandomIndex);
