/***************************************************************************
*     Copyright (c) 2003-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef UIENUMS_H
#define UIENUMS_H

#include "stdtypes.h"

AUTO_ENUM AEN_APPEND_TO(UIGenState);
typedef enum UIGenStateGameClient
{
	kUIGenStateTrayElemRefillingCharges = kUIGenState_MAX, ENAMES(TrayElemRefillingCharges)
	kUIGenStateTrayElemMaintainTimeRemaining,              ENAMES(TrayElemMaintainTimeRemaining)
	kUIGenStateTrayElemNotActivatable,                     ENAMES(TrayElemNotActivatable)
	kUIGenStateTrayElemHasCharges,                         ENAMES(TrayElemHasCharges)
	kUIGenStateTrayElemNoChargesRemaining,                 ENAMES(TrayElemNoChargesRemaining)
	kUIGenStateTrayElemCooldown,                           ENAMES(TrayElemCooldown)
	kUIGenStateTrayElemActive,                             ENAMES(TrayElemActive)
	kUIGenStateTrayElemAutoActivate,                       ENAMES(TrayElemAutoActivate)
	kUIGenStateTrayElemEmpty,                              ENAMES(TrayElemEmpty)
	kUIGenStateTrayElemLocked,                             ENAMES(TrayElemLocked)
	kUIGenStateTrayElemGlobalCooldown,                     ENAMES(TrayElemGlobalCooldown)

	kUIGenStateInventorySlotEmpty,                         ENAMES(InventorySlotEmpty)
	kUIGenStateInventorySlotFilled,                        ENAMES(InventorySlotFilled)
	kUIGenStateInventorySlotInExperiment,                  ENAMES(InventorySlotInExperiment)
	kUIGenStateInventorySlotInTrade,                       ENAMES(InventorySlotInTrade)

	kUIGenState_GAMECLIENTMAX,                          EIGNORE
} UIGenStateGameClient;

#endif // UIENUMS_H
