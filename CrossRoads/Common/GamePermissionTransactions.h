#ifndef GAMEPERMISSIONTRANSACTIONS_H
#define GAMEPERMISSIONTRANSACTIONS_H

#pragma once
GCC_SYSTEM

typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct GamePermissionDefs GamePermissionDefs;

bool GamePermissions_trh_CreateTokens(ATH_ARG NOCONST(GameAccountData) *pData, NON_CONTAINER GamePermissionDefs *pTempPermissions);
bool GamePermissions_trh_CreateTokens_Force(ATH_ARG NOCONST(GameAccountData) *pData, NON_CONTAINER GamePermissionDefs *pShadowPermissions);
bool GamePermissions_trh_UpdateNumerics(ATH_ARG NOCONST(Entity) *pEntity, ATH_ARG NOCONST(GameAccountData) *pData, bool bCheckForUpdate, bool bDoUpdate);

#endif GAMEPERMISSIONTRANSACTIONS_H