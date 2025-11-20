#pragma once
GCC_SYSTEM

#include "xdevice.h"

typedef struct RdrDrawListDrawCmd RdrDrawListDrawCmd;
typedef struct RdrDrawListSortCmd RdrDrawListSortCmd;

void rxbxSortDrawObjectsDirect(RdrDeviceDX *device, RdrDrawListSortCmd *cmd, WTCmdPacket *packet);
void rxbxDrawObjectsDirect(RdrDeviceDX *device, RdrDrawListDrawCmd *cmd, WTCmdPacket *packet);
void rxbxSetDebugBuffer(RdrDeviceDX *device, Vec4 *parm, WTCmdPacket *packet);

