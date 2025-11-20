#pragma once
GCC_SYSTEM
#ifndef UI_AUX_DEVICE_H
#define UI_AUX_DEVICE_H

typedef struct RdrDevice RdrDevice;
typedef bool (*GfxAuxDeviceCloseCallback)(RdrDevice* element, void *userData);

RdrDevice *ui_AuxDeviceCreate(SA_PARAM_OP_STR const char *pchTitle, GfxAuxDeviceCloseCallback cbClose, UserData pCloseData);
bool ui_AuxDeviceMoveWidgets(RdrDevice *pDevice, UserData pData);

#endif
