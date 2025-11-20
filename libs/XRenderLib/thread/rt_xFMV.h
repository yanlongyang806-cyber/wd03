#pragma once

typedef struct RxbxFMV RxbxFMV;
typedef struct RdrFMVParams RdrFMVParams;
typedef struct RdrFMV RdrFMV;
typedef struct RdrDevice RdrDevice;
typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct WTCmdPacket WTCmdPacket;

RdrFMV *rxbxFMVCreate(RdrDevice *device);

// Commands from main thread
void rxbxFMVInitDirect(RdrDevice *device, RxbxFMV **fmv, WTCmdPacket *cmd);
void rxbxFMVSetParamsDirect(RdrDevice *device, RdrFMVParams *fmv_params, WTCmdPacket *cmd);
void rxbxFMVPlayDirect(RdrDevice *device, RxbxFMV **fmv, WTCmdPacket *cmd);
void rxbxFMVCloseDirect(RdrDevice *device, RxbxFMV **fmv, WTCmdPacket *cmd);

// Interface from render thread
void rxbxFMVGoDirect(RdrDeviceDX *device, RxbxFMV *fmv);
void rxbxFMVReleaseAllForResetDirect(RdrDeviceDX *xdevice);
