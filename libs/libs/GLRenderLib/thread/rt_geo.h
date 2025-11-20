#ifndef _RT_GEO_H_
#define _RT_GEO_H_

#include "RdrGeometry.h"
#include "device.h"

typedef struct RdrGeometryDataWinGL
{
	RdrGeometryData base_data;
	int index_buffer_id;
	int vertex_buffer_id;
	RdrVertexDeclaration *vertex_declaration;
} RdrGeometryDataWinGL;

void rwglSetGeometryDataDirect(RdrDeviceWinGL *device, RdrGeometryData *data);
RdrGeometryDataWinGL *rwglBindGeometryDirect(RdrDeviceWinGL *device, GeoHandle handle);
void rwglFreeGeometryDirect(RdrDeviceWinGL *device, GeoHandle handle);
void rwglFreeAllGeometryDirect(RdrDeviceWinGL *device);


#endif //_RT_GEO_H_

