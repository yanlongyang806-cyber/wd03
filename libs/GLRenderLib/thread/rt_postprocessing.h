#ifndef _RT_POSTPROCESSING_H_
#define _RT_POSTPROCESSING_H_

typedef struct RdrDeviceWinGL RdrDeviceWinGL;
typedef struct RdrScreenPostProcess RdrScreenPostProcess;
typedef struct RdrShapePostProcess RdrShapePostProcess;

void rwglPostProcessScreenDirect(RdrDeviceWinGL *device, RdrScreenPostProcess *ppscreen);
void rwglPostProcessShapeDirect(RdrDeviceWinGL *device, RdrShapePostProcess *ppshape);

#endif //_RT_POSTPROCESSING_H_

