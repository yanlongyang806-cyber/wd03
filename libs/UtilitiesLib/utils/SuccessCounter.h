#pragma once

typedef struct SuccessCounter SuccessCounter;

SuccessCounter *SuccessCounter_Create(int iRingSize);
void SuccessCounter_Destroy(SuccessCounter **ppCounter);
void SuccessCounter_ItHappened(SuccessCounter *pCounter, bool bSucceeded);
int SuccessCounter_GetSuccessCount(SuccessCounter *pCounter);
int SuccessCounter_GetFailureCount(SuccessCounter *pCounter);
int SuccessCounter_GetTotalCount(SuccessCounter *pCounter);
int SuccessCounter_GetSuccessPercentLastN(SuccessCounter *pCounter);
int SuccessCounter_GetSuccessPercentTotal(SuccessCounter *pCounter);



