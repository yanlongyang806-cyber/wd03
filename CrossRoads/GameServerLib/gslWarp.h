#ifndef GSLWARP_H
#define GSLWARP_H

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct PlayerWarpToData PlayerWarpToData;

void gslWarp_WarpToTarget_ChargeItem(Entity *pEnt, U32 iTargetEntID, U64 uiItemId);
void gslWarp_WarpToLocation(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID PlayerWarpToData *pData);

#endif //GSLWARP_H