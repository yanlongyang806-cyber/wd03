#ifndef _RT_PRIMITIVE_H_
#define _RT_PRIMITIVE_H_

#include "device.h"


void rwglDrawPrimitiveDirect(RdrDeviceWinGL *device, RdrDrawablePrimitive *primitive);
void rwglDrawQuadDirect(RdrDeviceWinGL *device, RdrQuadDrawable *quad);


#endif //_RT_PRIMITIVE_H_
