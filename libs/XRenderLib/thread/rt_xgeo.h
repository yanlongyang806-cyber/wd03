#ifndef _RT_XGEO_H_
#define _RT_XGEO_H_

#include "RdrState.h"
#include "RdrGeometry.h"
#include "xdevice.h"
#include "rt_xdrawmode.h"

typedef struct RdrGeoStreamInfoDX
{
	RdrVertexBufferObj buffer;
	int iStride;
} RdrGeoStreamInfoDX;

typedef struct RdrGeometryDataDX
{
	RdrGeometryData base_data;
	// I need a separate buffer for each stream.  This won't scale very tidily if go to full de-interleaving
	RdrGeoStreamInfoDX aVertexBufferInfos[MAX_VERTEX_STREAMS];
	RdrIndexBufferObj index_buffer;
} RdrGeometryDataDX;

void rxbxSetGeometryDataDirect(RdrDeviceDX *device, RdrGeometryData *data, WTCmdPacket *packet);
RdrGeometryDataDX *rxbxGetGeoDataDirect(RdrDeviceDX *device, GeoHandle handle);
RdrVertexDeclarationObj rxbxGetVertexDeclarationDirect(RdrDeviceDX *device, RdrGeoUsage const * pUsage);
void rxbxFreeGeometryDirect(RdrDeviceDX *device, GeoHandle *handle_ptr, WTCmdPacket *packet);
void rxbxFreeAllGeometryDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);

#if SUPPORT_VERTEX_BUFFER_COMBINE
void rxbxTryCoalesceGeometryDataDirect(RdrDeviceDX *device, RdrGeometryDataDX *geo_data, int nCombineThreshold);
#endif

#endif //_RT_XGEO_H_


