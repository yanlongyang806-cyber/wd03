#ifndef _RT_XSPRITE_H_
#define _RT_XSPRITE_H_

#include "RdrDrawable.h"
#include "xdevice.h"

void rxbxDrawSpritesDirect(RdrDeviceDX *device, RdrSpritesPkg *pkg, WTCmdPacket *packet);
void rxbxInitSpriteVertexDecl(RdrDeviceDX *device);

void rxbxFreeSpriteIndexBuffer(RdrDeviceDX *device, int index);
//dont specify both data and data32
RdrIndexBufferObj rxbxGetSpriteIndexBuffer(RdrDeviceDX *device, int index, U16* data, U32* data32, int index_count);
void rxbxSpriteEffectSetup(RdrDeviceDX *device, RdrSpriteState *cur_state);
void rxbxSpriteFlushCurrentEffect();
void rxbxSpriteBindBlendSetup(RdrDeviceDX *device, RdrSpriteState *state, bool wireframe);

#endif //_RT_XSPRITE_H_


