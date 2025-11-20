#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"

typedef struct Login2State Login2State;
typedef struct NOCONST(Entity) NOCONST(Entity);

bool aslLogin2_InitializeNewCharacter(Login2State *loginState, NOCONST(Entity) *playerEnt);