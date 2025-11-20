#include "error.h"

#include "earray.h"
#include "MemoryPool.h"

#include "rt_geo.h"
#include "rt_state.h"


static const char MODELCACHE_VBO[] = "ModelCache_VBO";
MP_DEFINE(RdrGeometryDataWinGL);

__forceinline static RdrGeometryDataWinGL *getGeoData(RdrDeviceWinGL *device, GeoHandle handle, int remove_it)
{
	U32 idx = ((U32)handle)-1;
	if (idx < (U32)eaSize(&device->geometry_data))
	{
		RdrGeometryDataWinGL *data = device->geometry_data[idx];
		if (remove_it)
			device->geometry_data[idx] = 0;
		return data;
	}
	return 0;
}

__forceinline static void setGeoData(RdrDeviceWinGL *device, GeoHandle handle, RdrGeometryDataWinGL *geo_data)
{
	U32 idx = ((U32)handle)-1;
	if (idx >= (U32)eaSize(&device->geometry_data))
		eaSetSize(&device->geometry_data, idx+1);
	device->geometry_data[idx] = geo_data;
}

void rwglSetGeometryDataDirect(RdrDeviceWinGL *device, RdrGeometryData *data)
{
	RdrGeometryDataWinGL *geo_data = getGeoData(device, data->handle, 0);
	U32 vert_bytes, tri_bytes;

	if (geo_data)
		rwglFreeGeometryDirect(device, data->handle);

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	MP_CREATE(RdrGeometryDataWinGL, 1024);
	geo_data = MP_ALLOC(RdrGeometryDataWinGL);

	CopyStructs(&geo_data->base_data, data, 1);

	geo_data->vertex_declaration = rdrGetVertexDeclaration(geo_data->base_data.vert_usage);

	vert_bytes = geo_data->vertex_declaration->stride * geo_data->base_data.vert_count;
	tri_bytes = geo_data->base_data.tri_count * 3 * sizeof(*geo_data->base_data.tris);

	if (rdr_caps.supports_vbos && !geo_data->base_data.no_vbos)
	{
		geo_data->index_buffer_id = 2 * ((U32)geo_data->base_data.handle);
		CHECKGL;
		rwglBindVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, geo_data->index_buffer_id);
		if (geo_data->base_data.tri_count)
			rwglUpdateVBOData(GL_ELEMENT_ARRAY_BUFFER_ARB, tri_bytes, geo_data->base_data.tris, GL_STATIC_DRAW_ARB, MODELCACHE_VBO);

		geo_data->vertex_buffer_id = 2 * ((U32)geo_data->base_data.handle) + 1;
		CHECKGL;
		rwglBindVBO(GL_ARRAY_BUFFER_ARB, geo_data->vertex_buffer_id);
		if (geo_data->base_data.vert_data)
			rwglUpdateVBOData(GL_ARRAY_BUFFER_ARB, vert_bytes, geo_data->base_data.vert_data, GL_STATIC_DRAW_ARB, MODELCACHE_VBO);

		rwglBindVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
		rwglBindVBO(GL_ARRAY_BUFFER_ARB, 0);

		geo_data->base_data.tris = 0;
		geo_data->base_data.vert_data = 0;
	}
	else
	{
		if (tri_bytes)
			geo_data->base_data.tris = memcpy(malloc(tri_bytes), geo_data->base_data.tris, tri_bytes);
		if (vert_bytes)
			geo_data->base_data.vert_data = memcpy(malloc(vert_bytes), geo_data->base_data.vert_data, vert_bytes);
	}

	if (tri_bytes)
	{
		assert(geo_data->base_data.subobject_count > 0);
		assert(geo_data->base_data.subobject_tri_counts);
		assert(geo_data->base_data.subobject_tri_bases);

		geo_data->base_data.subobject_tri_counts = memcpy(malloc(geo_data->base_data.subobject_count * sizeof(int)), geo_data->base_data.subobject_tri_counts, geo_data->base_data.subobject_count * sizeof(int));
		geo_data->base_data.subobject_tri_bases = memcpy(malloc(geo_data->base_data.subobject_count * sizeof(int)), geo_data->base_data.subobject_tri_bases, geo_data->base_data.subobject_count * sizeof(int));
	}

	setGeoData(device, geo_data->base_data.handle, geo_data);
}

void rwglFreeGeometryDirect(RdrDeviceWinGL *device, GeoHandle handle)
{
	RdrGeometryDataWinGL *geo_data = getGeoData(device, handle, 1);
	if (!geo_data)
		return;

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	free(geo_data->base_data.subobject_tri_counts);
	free(geo_data->base_data.subobject_tri_bases);

	if (geo_data->base_data.vert_data)
		free(geo_data->base_data.vert_data);
	else
		rwglDeleteVBO(GL_ARRAY_BUFFER_ARB, geo_data->vertex_buffer_id, MODELCACHE_VBO);

	if (geo_data->base_data.tris)
		free(geo_data->base_data.tris);
	else
		rwglDeleteVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, geo_data->index_buffer_id, MODELCACHE_VBO);

	MP_FREE(RdrGeometryDataWinGL, geo_data);
}

RdrGeometryDataWinGL *rwglBindGeometryDirect(RdrDeviceWinGL *device, GeoHandle handle)
{
	RdrGeometryDataWinGL *geo_data = getGeoData(device, handle, 0);
	assert(geo_data);

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	rwglBindVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, geo_data->index_buffer_id);
	rwglBindVBO(GL_ARRAY_BUFFER_ARB, geo_data->vertex_buffer_id);

	return geo_data;
}

void rwglFreeAllGeometryDirect(RdrDeviceWinGL *device)
{
	int i;
	for (i = 0; i < eaSize(&device->geometry_data); i++)
		rwglFreeGeometryDirect(device, (GeoHandle)(i+1));
}
