#ifndef _GCL_POWERS_AE_DEBUG_H_
#define _GCL_POWERS_AE_DEBUG_H_

typedef struct Entity Entity;
typedef struct PowerDebugAE PowerDebugAE;

void gclPowersAEDebug_Init();
Entity* gclPowersAEDebug_GetDebuggingEnt();
void PowersAEDebug_AddAEDebug(PowerDebugAE *debugData);

#endif

