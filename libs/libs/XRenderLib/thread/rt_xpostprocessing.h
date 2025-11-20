#ifndef _RT_XPOSTPROCESSING_H_
#define _RT_XPOSTPROCESSING_H_

typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct RdrScreenPostProcess RdrScreenPostProcess;
typedef struct RdrShapePostProcess RdrShapePostProcess;

void rxbxSetupPostProcessScreenDrawModeAndDecl(RdrDeviceDX *device);

void rxbxPostProcessScreenDirect(RdrDeviceDX *device, RdrScreenPostProcess *ppscreen, WTCmdPacket *packet);
void rxbxPostProcessShapeDirect(RdrDeviceDX *device, RdrShapePostProcess *ppshape, WTCmdPacket *packet);

#endif


