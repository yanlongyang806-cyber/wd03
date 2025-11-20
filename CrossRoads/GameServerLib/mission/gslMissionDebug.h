/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Mission Mission;

// Updates the debugging information attached to the mission
void missiondebug_UpdateDebugInfo(Mission *pMission);
void missiondebug_UpdateAllDebugInfo(CONST_EARRAY_OF(Mission) eaMissions);
