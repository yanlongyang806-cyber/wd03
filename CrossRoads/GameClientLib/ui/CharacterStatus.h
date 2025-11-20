#pragma once
GCC_SYSTEM

#include "CharacterAttribsMinimal.h" // For enums

typedef struct Entity Entity;
typedef U32 EntityRef;

bool exprTestOverheadFlag(SA_PARAM_NN_VALID Entity *pEnt, bool mouse_over, const char *pcFlag);
bool EntWasObject(SA_PARAM_NN_VALID Entity *pEnt);