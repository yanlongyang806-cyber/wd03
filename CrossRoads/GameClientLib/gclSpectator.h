#ifndef _GCL_SPECTATOR_H_
#define _GCL_SPECTATOR_H_

void gclSpectator_UpdateLocalPlayer();
int gclSpectator_IsSpectating();
Entity* gclSpectator_GetSpectatingEntity();

void gclSpectator_MapUnload();


#endif