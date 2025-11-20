#ifndef _RT_MODEL_H_
#define _RT_MODEL_H_

#include "device.h"

void rwglDrawModelDirect(RdrDeviceWinGL *device, RdrDrawableModel *model_draw, RdrSortNode *sort_node);
void rwglDrawSkinnedModelDirect(RdrDeviceWinGL *device, RdrDrawableSkinnedModel *skin_draw, RdrSortNode *sort_node);
void rwglDrawTerrainDirect(RdrDeviceWinGL *device, RdrDrawableGeo *draw, RdrSortNode *sort_node);



#endif //_RT_MODEL_H_
