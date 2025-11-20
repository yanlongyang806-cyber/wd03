#ifndef _RT_XMODEL_H_
#define _RT_XMODEL_H_

#include "xdevice.h"

void rxbxDrawModelDirect(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data, bool manual_depth_test);
void rxbxDrawSkinnedModelDirect(RdrDeviceDX *device, RdrDrawableSkinnedModel *draw_skin, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data, bool manual_depth_test);
void rxbxDrawHeightMapDirect(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data);
void rxbxDrawStarFieldDirect(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data);
void rxbxDrawClothMeshDirect(RdrDeviceDX *device, RdrDrawableClothMesh *cloth_mesh, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data);


#endif //_RT_XMODEL_H_
