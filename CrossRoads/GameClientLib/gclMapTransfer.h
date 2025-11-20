#ifndef GCLMAPTRANSFER_H
#define GCLMAPTRANSFER_H

#pragma once
GCC_SYSTEM

typedef struct MicroTransactionDef MicroTransactionDef;

MicroTransactionRef **gclMapMoveConfirm_Microtransactions(void);
void gclMapMoveConfirm_Microtransaction_Notify(MicroTransactionDef *pDef, bool bSuccess);

#endif //GCLMAPTRANSFER_H