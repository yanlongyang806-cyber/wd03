#pragma once
GCC_SYSTEM
typedef struct PerformanceCounter PerformanceCounter;


PerformanceCounter *performanceCounterCreate(char *pInterfaceName);
void performanceCounterDestroy(PerformanceCounter *counter);
int performanceCounterAdd(PerformanceCounter *counter, const char *counterName, long *storage);
int performanceCounterAddF64(PerformanceCounter *counter, const char *counterName, double *storage);


typedef struct AlertOnSlowArgs AlertOnSlowArgs;
//if pSlowAlertString is set, then do SLOW_ALERTs around all the key steps
int performanceCounterQuery(PerformanceCounter *counter, AlertOnSlowArgs *pAlertOnSlow);

