#ifndef GCLWARP_H
#define GCLWARP_H

#pragma once
GCC_SYSTEM

typedef struct Entity			Entity;
typedef struct Item				Item;
typedef struct ItemDef			ItemDef;

bool gclWarp_StartItemWarp(SA_PARAM_NN_VALID Item *pItem, SA_PARAM_OP_VALID ItemDef *pItemDef);
bool gclWarp_CanExec(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_VALID Item *pItem, SA_PARAM_NN_VALID ItemDef *pItemDef);

#endif // GCLWARP_H