#ifndef GSLMICROTRANSACTIONS_H
#define GSLMICROTRANSACTIONS_H

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct MicroTransactionRewards MicroTransactionRewards;

S32 MicroTrans_IsSpecialKey(const char *pchKey);
const char* MicroTrans_SpecialKeyMesg(const char *pchKey);

bool MicroTrans_GenerateRewardBags(int iPartitionIdx, Entity *pEnt, MicroTransactionDef *pDef, MicroTransactionRewards* pRewards);

#endif //GSLMICROTRANSACTIONS_H