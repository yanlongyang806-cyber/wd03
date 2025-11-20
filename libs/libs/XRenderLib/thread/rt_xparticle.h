#ifndef _RT_XPARTICLE_H_
#define _RT_XPARTICLE_H_

#include "RdrDrawable.h"
#include "xdevice.h"

void rxbxDrawParticlesDirect(RdrDeviceDX *device, RdrSortNode **nodes, int node_count, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data, bool is_low_res_edge_pass, bool manual_depth_test);
void rxbxDrawFastParticlesDirect(RdrDeviceDX *device, RdrDrawableFastParticles *particle_set, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, bool is_low_res_edge_pass, bool manual_depth_test, bool is_underexposed_pass);
void rxbxDrawTriStripDirect(RdrDeviceDX *device, RdrDrawableTriStrip *tristrip, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data);
void rxbxDrawCylinderTrailDirect(RdrDeviceDX *device, RdrDrawableCylinderTrail *cyltrail, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data);

#endif //_RT_XPARTICLE_H_


