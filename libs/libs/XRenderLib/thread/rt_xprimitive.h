#ifndef _RT_XPRIMITIVE_H_
#define _RT_XPRIMITIVE_H_

#include "xdevice.h"

void rxbxInitPrimitiveVertexDecls(RdrDeviceDX *device);

void rxbxSetupPrimitiveDrawState(RdrDeviceDX* device, bool justVertexDecls);
void rxbxDrawVerticesUP(RdrDeviceDX *device, DWORD primitive_topo, int vertex_count, const void * vertices, int stride);
void rxbxDrawQuadDirect(RdrDeviceDX *device, RdrQuadDrawable *quad, WTCmdPacket *packet);
void rxbxDrawPrimitivesDirect(RdrDeviceDX *device, RdrSortNode **nodes, int node_count, 
							  RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data);
void rxbxDrawMeshPrimitiveDirect(RdrDeviceDX *device, RdrDrawableMeshPrimitive *mesh, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data);

#endif //_RT_XPRIMITIVE_H_
