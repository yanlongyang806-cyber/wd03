#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Entity Entity;
typedef struct SavedTray SavedTray;

// Autofill functions

// Automatically fills the Entity's trays with Powers.  If an earray of preexisting
//  PowerIDs is included, it will not include those.
void entity_TrayAutoFill(SA_PARAM_NN_VALID Entity *e, SA_PARAM_OP_VALID U32 *puiIDs);

// Automatically fills the Entity's trays with Powers.  If an earray of preexisting
//  PowerIDs is included, it will not include those.
// TODO(JW): This doesn't work anymore
void entity_TrayAutoFillPet(SA_PARAM_NN_VALID Entity *e, SA_PARAM_OP_VALID U32 *puiIDs);

// Sets values into the Entity's UITray array.  Returns true on success.
int entity_TraySetUITrayIndex(Entity *e, int iUITray, int iTray);

// Tray Fixup functions

void entity_trh_CleanupOldTray(ATH_ARG NOCONST(Entity) *pEntity);

void entity_TrayFixup(Entity* pEntity);

void entity_CopyOldTrayData(Entity* pEntity, SavedTray* pTray);

void entity_BuildPowerIDListFromTray(Entity* pEnt, U32** ppuiIDs);
void entity_TrayPreSave(Entity* pEnt);