
#include "MemoryPool.h"
#include "memlog.h"

#include "rt_xgeo.h"

#if SUPPORT_VERTEX_BUFFER_COMBINE
#define D3D_VERTEX_BUFFER_USAGE 0
#else
#define D3D_VERTEX_BUFFER_USAGE D3DUSAGE_WRITEONLY
#endif

#define MAX_COALESCE_BUFFERS 64
typedef struct RdrVertexBufferCoalesceData
{
	int disable;
	int nBuffers;
	RdrGeometryDataDX * geom[ MAX_COALESCE_BUFFERS ];
	RdrVertexBufferObj vertex_buffer;
	RdrIndexBufferObj index_buffer;
} RdrVertexBufferCoalesceData;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc););

MP_DEFINE(RdrGeometryDataDX);

const char INSTANCEGEO_MEMMONITOR_NAME[] = "Geo:Models";

static MemLog rt_xgeo_memlog;


__forceinline static RdrGeometryDataDX *getGeoData(RdrDeviceDX *device, GeoHandle handle, int remove_it)
{
	RdrGeometryDataDX *geo_data;
	if (remove_it)
		stashIntRemovePointer(device->geometry_cache, handle, &geo_data);
	else
		stashIntFindPointer(device->geometry_cache, handle, &geo_data);
	return geo_data;
}

#define MAX_ELEMENTS 20
static RdrVertexDeclarationObj createDXVertexDeclaration(RdrDeviceDX *device, RdrGeoUsage const * pUsage)
{
	HRESULT hr;
	int idx = 0, iStream;
	VertexDecl vertex_declaration_data = { device->d3d11_device != NULL, 0 };
	RdrVertexDeclarationObj object_vertex_declaration = { 0 };
	bool bInstanced = (pUsage->key & RUSE_KEY_INSTANCE) != 0;

#define COPYTOELEMENT_INSTANCED(stream, typedefine, offset, instanced)	\
	rxbxVertexDeclAddElement(&vertex_declaration_data, typedefine, offset, stream, instanced)
#define COPYTOELEMENT(stream, typedefine, offset) COPYTOELEMENT_INSTANCED(stream, typedefine, offset, 0)

	for (iStream=0;iStream<pUsage->iNumPrimaryStreams;iStream++)
	{
		int i;
		RdrVertexDeclaration * pDecl;
		int bits = pUsage->bits[iStream];
		if (bits & RUSE_TEXCOORD2S)
		{
			// remove the bit so that the correct vertex declaration is used
			devassert(bits & RUSE_TEXCOORDS);
			bits &= ~RUSE_TEXCOORDS;
		}

		pDecl = rdrGetVertexDeclaration(bits);

		for (i = 1; i < RUSE_NUM_COMBINATIONS_NON_FLAGS; i<<=1)
		{
			if (bits & i)
			{
				assert(idx < MAX_ELEMENTS);

				switch (i)
				{
					xcase RUSE_POSITIONS:
					{
						COPYTOELEMENT(iStream, VPOS, pDecl->position_offset);
					}

					xcase RUSE_NORMALS:
					{
						COPYTOELEMENT(iStream, VNORMAL_PACKED, pDecl->normal_offset);
					}

					xcase RUSE_TANGENTS:
					{
						COPYTOELEMENT(iStream, VTANGENT_PACKED, pDecl->tangent_offset);
					}

					xcase RUSE_BINORMALS:
					{
						COPYTOELEMENT(iStream, VBINORMAL_PACKED, pDecl->binormal_offset);
					}

					xcase RUSE_TEXCOORDS:
					{
						if (!rdrSupportsFeature(&device->device_base, FEATURE_DECL_F16_2) && !(pUsage->bits[iStream] & RUSE_TEXCOORDS_HI_FLAG) )
						{
							assertmsg(0, "Trying to use F16s when they're disable or not supported");
						}
						if (bits & RUSE_TEXCOORDS_HI_FLAG)
							COPYTOELEMENT(iStream, VTEXCOORD32, pDecl->texcoord_offset);
						else
							COPYTOELEMENT(iStream, VTEXCOORD16, pDecl->texcoord_offset);
					}

					xcase RUSE_TEXCOORD2S:
					{
						if (!rdrSupportsFeature(&device->device_base, FEATURE_DECL_F16_2) && !(pUsage->bits[iStream] & RUSE_TEXCOORDS_HI_FLAG) )
						{
							assertmsg(0, "Trying to use F16s when they're disable or not supported");
						}
						// we use the FIRST texcoord channel offset here, as texcoord2 is treated as a double long version that includes the first channel
						// for the purposes of vertex declaration
						if (bits & RUSE_TEXCOORDS_HI_FLAG)
							COPYTOELEMENT(iStream, V2TEXCOORD32, pDecl->texcoord_offset);
						else
							COPYTOELEMENT(iStream, V2TEXCOORD16, pDecl->texcoord_offset);
					}

					xcase RUSE_BONEWEIGHTS:
					{
						assert(!bInstanced);  // Overlapping Usage=Texcoord, UsageIndex=5
						if (bits & RUSE_BONEWEIGHTS_HI_FLAG)
							COPYTOELEMENT(iStream, VBONEWEIGHT32, pDecl->boneweight_offset);
						else
							COPYTOELEMENT(iStream, VBONEWEIGHT, pDecl->boneweight_offset);
					}

					xcase RUSE_BONEIDS:
					{
						assert(!bInstanced);  // Overlapping Usage=Texcoord, UsageIndex=4
						COPYTOELEMENT(iStream, VBONEIDX, pDecl->boneid_offset);
					}

					xcase RUSE_COLORS:
					{
						COPYTOELEMENT(iStream, VCOLORU8, pDecl->color_offset);
					}
				}

				idx++;
			}
		}
	}

	// stream 1
	if (pUsage->key & RUSE_KEY_MORPH)
	{
		devassert(!bInstanced);

		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT(iStream, VPOS2, 0);
		idx++;

		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT(iStream, VNORMAL2_PACKED, sizeof(Vec3));
		idx++;

		iStream++;
	}
	else if (bInstanced)
	{
		int stream_offset = 0;

		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT_INSTANCED(iStream, VINST_MATX, stream_offset, 1);
		stream_offset += sizeof(Vec4);
		idx++;

		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT_INSTANCED(iStream, VINST_MATY, stream_offset, 1);
		stream_offset += sizeof(Vec4);
		idx++;

		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT_INSTANCED(iStream, VINST_MATZ, stream_offset, 1);
		stream_offset += sizeof(Vec4);
		idx++;

		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT_INSTANCED(iStream, VINST_COLOR, stream_offset, 1);
		stream_offset += sizeof(Vec4);
		idx++;

		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT_INSTANCED(iStream, VINST_PARAM, stream_offset, 1);
		stream_offset += sizeof(Vec4);
		idx++;

		iStream++;
	}

	// stream 2
	if (pUsage->key & RUSE_KEY_VERTEX_LIGHTS)
	{
		assert(idx < MAX_ELEMENTS);
		COPYTOELEMENT(iStream, VLIGHT, 0);
		idx++;

		iStream++;
	}

	assert(idx < MAX_ELEMENTS);
	idx = rxbxVertexDeclEnd(&vertex_declaration_data);

	// If this returns E_FAIL, there are probably two entries in the elements list with the same UsageIndex (e.g. instance and VBONEIDX)
	hr = rxbxCreateVertexDeclarationFromDecl(device, &vertex_declaration_data, device->device_state.active_vertex_shader_wrapper, &object_vertex_declaration);
	rxbxFatalHResultErrorf(device, hr, "creating vertex declaration", "");
	return object_vertex_declaration;
}

void rxbxSetGeometryDataDirect(RdrDeviceDX *device, RdrGeometryData *data, WTCmdPacket *packet)
{
	RdrGeometryDataDX *geo_data, *prior_geo_data;
	U32 index_bytes, index_count;
	void *buffer_mem;
	HRESULT hr;

	PERFINFO_AUTO_START_FUNC();

	prior_geo_data = geo_data = getGeoData(device, data->handle, 0);

	if (geo_data)
		rxbxFreeGeometryDirect(device, &data->handle, NULL);

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	MP_CREATE(RdrGeometryDataDX, 1024);
	geo_data = MP_ALLOC(RdrGeometryDataDX);
	geo_data->base_data.debug_name = data->debug_name;

	memlog_printf(&rt_xgeo_memlog, "Updating VBO 0x%p with handle %d on device 0x%p, data 0x%p, old geo 0x%p", geo_data, data->handle, device, data, prior_geo_data);

	CopyStructs(&geo_data->base_data, data, 1);

	index_count = geo_data->base_data.tri_count * 3;
	index_bytes = index_count * sizeof(*geo_data->base_data.tris);

	if (index_count)
	{
		hr = rxbxCreateIndexBuffer(device, index_bytes, BUF_MANAGED, &geo_data->index_buffer);
		rxbxFatalHResultErrorf(device, hr, "creating index buffer", "");

		if (device->d3d11_device)
		{
			ID3D11DeviceContext_UpdateSubresource(device->d3d11_imm_context, geo_data->index_buffer.index_buffer_resource_d3d11,
				0,NULL,geo_data->base_data.tris, index_bytes, 0);
		}
		else
		{
			buffer_mem = rxbxIndexBufferLockWrite(device, geo_data->index_buffer, RDRLOCK_WRITEONCE, 0, 0, NULL);
				memcpy_writeCombined(buffer_mem, geo_data->base_data.tris, index_bytes);
			rxbxIndexBufferUnlock(device, geo_data->index_buffer);

#if ENABLE_XBOX_HW_INSTANCING
			hr = rxbxCreateVertexBuffer(device, index_count * sizeof(U32), // must be 4 byte indices
				BUF_MANAGED, &geo_data->vertex_index_buffer);
			rxbxFatalHResultErrorf(device, hr, "creating vertex index buffer", "");
			IDirect3DVertexBuffer9_Lock(geo_data->vertex_index_buffer, 0, 0, &buffer_mem, 0);
			for (i = 0; i < index_count; ++i)
				((U32 *)buffer_mem)[i] = geo_data->base_data.tris[i];
			IDirect3DVertexBuffer9_Unlock(geo_data->vertex_index_buffer);
			memMonitorTrackUserMemory(INSTANCEGEO_MEMMONITOR_NAME, 1, index_count * sizeof(U32), MM_ALLOC);
#endif
		}

		ADD_MISC_COUNT(index_bytes, "Index Buffer Bytes");
	}

	if (geo_data->base_data.vert_count)
	{
		int i;
		int iTotalBytes=0;
		U8 * pVertData = geo_data->base_data.vert_data;
		int iTotalStreams = geo_data->base_data.vert_usage.iNumPrimaryStreams;
		if (geo_data->base_data.vert_usage.bHasSecondary)
		{
			// The morph data is stored with the rest of the data.  (Also, morph data is hijacked for terrain lighting)
			// Currently, that is not true for any other "special" streams (instance data, vertex lights)
			iTotalStreams++;
		}
		for (i=0;i<iTotalStreams;i++)
		{
			int iBytes;
			geo_data->aVertexBufferInfos[i].iStride = rdrGetVertexDeclaration(geo_data->base_data.vert_usage.bits[i])->stride;
			iBytes = geo_data->aVertexBufferInfos[i].iStride*geo_data->base_data.vert_count;
			hr = rxbxCreateVertexBuffer(device, iBytes, BUF_MANAGED, &geo_data->aVertexBufferInfos[i].buffer);
			rxbxFatalHResultErrorf(device, hr, "creating vertex buffer", "");

			if (device->d3d11_device)
			{
				ID3D11DeviceContext_UpdateSubresource(device->d3d11_imm_context, geo_data->aVertexBufferInfos[i].buffer.vertex_buffer_resource_d3d11,
					0,NULL, pVertData, iBytes, 0);
			}
			else
			{
				buffer_mem = rxbxVertexBufferLockWrite(device, geo_data->aVertexBufferInfos[i].buffer, RDRLOCK_WRITEONCE);
				memcpy_writeCombined(buffer_mem, pVertData, iBytes);
				rxbxVertexBufferUnlock(device, geo_data->aVertexBufferInfos[i].buffer);
			}

			iTotalBytes += iBytes;
			pVertData += iBytes;
		}


		ADD_MISC_COUNT(iTotalBytes, "Vertex Buffer Bytes");
	}

	geo_data->base_data.tris = 0;
	geo_data->base_data.vert_data = 0;

	if (index_count)
	{
		assert(geo_data->base_data.subobject_count > 0);
		assert(geo_data->base_data.subobject_tri_counts);
		assert(geo_data->base_data.subobject_tri_bases);

		geo_data->base_data.subobject_tri_counts = memcpy(malloc(geo_data->base_data.subobject_count * sizeof(int)), geo_data->base_data.subobject_tri_counts, geo_data->base_data.subobject_count * sizeof(int));
		geo_data->base_data.subobject_tri_bases = memcpy(malloc(geo_data->base_data.subobject_count * sizeof(int)), geo_data->base_data.subobject_tri_bases, geo_data->base_data.subobject_count * sizeof(int));
	}
	else
	{
		geo_data->base_data.subobject_tri_counts = NULL;
		geo_data->base_data.subobject_tri_bases = NULL;
	}

	assert(stashIntAddPointer(device->geometry_cache, geo_data->base_data.handle, geo_data, false));

	PERFINFO_AUTO_STOP();
}

void rxbxFreeGeometryDirect(RdrDeviceDX *device, GeoHandle *handle_ptr, WTCmdPacket *packet)
{
	GeoHandle handle = *handle_ptr;
	RdrGeometryDataDX *geo_data = getGeoData(device, handle, 1);
	int ref_count;
	int i;

	if (!geo_data)
		return;

	// this function does not require the device to be locked since it does not do any drawing or touch the current_state global variable

	memlog_printf(&rt_xgeo_memlog, "Freeing VBO with handle %d from device 0x%p", handle, device);

	SAFE_FREE(geo_data->base_data.subobject_tri_counts);
	SAFE_FREE(geo_data->base_data.subobject_tri_bases);

	if (geo_data->index_buffer.typeless_index_buffer)
	{
		rxbxNotifyIndexBufferFreed(device, geo_data->index_buffer);
#if _XBOX
		IDirect3DIndexBuffer9_BlockUntilNotBusy(geo_data->index_buffer);
#endif
		ref_count = rxbxReleaseIndexBuffer(device, geo_data->index_buffer);
		geo_data->index_buffer.index_buffer_d3d9 = NULL;
		if (ref_count)
			memlog_printf(0, "Index buffer still referenced after we released it %d refs Dev 0x%p RdrGeometryDataDX 0x%p", ref_count, device, geo_data);
	}

	for (i=0;i<MAX_VERTEX_STREAMS;i++)
	{
		if (geo_data->aVertexBufferInfos[i].buffer.typeless_vertex_buffer)
		{
			rxbxNotifyVertexBufferFreed(device, geo_data->aVertexBufferInfos[i].buffer);
#if _XBOX
			IDirect3DVertexBuffer9_BlockUntilNotBusy(geo_data->vertex_buffer.vertex_buffer_d3d9);
#endif
			ref_count = rxbxReleaseVertexBuffer(device, geo_data->aVertexBufferInfos[i].buffer);
			geo_data->aVertexBufferInfos[i].buffer.typeless_vertex_buffer = NULL;
			if (ref_count)
				memlog_printf(0, "Vertex buffer still referenced after we released it %d refs Dev 0x%p RdrGeometryDataDX 0x%p", ref_count, device, geo_data);
		}
	}

	MP_FREE(RdrGeometryDataDX, geo_data);
}

RdrGeometryDataDX *rxbxGetGeoDataDirect(RdrDeviceDX *device, GeoHandle handle)
{
	return getGeoData(device, handle, 0);
}

RdrVertexDeclarationObj rxbxGetVertexDeclarationDirect(RdrDeviceDX *device, RdrGeoUsage const * pUsage)
{
	int key = pUsage->key;
	RdrVertexDeclarationObj vertex_declaration;
	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (device->d3d11_device)
	{
		// Add in shader usage bits
		assert(!(pUsage->key & ~((1<<RUSE_KEY_TOTAL_BITS)-1))); // Otherwise we're not shifting enough
		key = pUsage->key | (device->device_state.active_vertex_shader_wrapper->input_signature_index << RUSE_KEY_TOTAL_BITS);
	}

	stashIntFindPointer(device->vertex_declarations, key, &vertex_declaration.typeless_decl);
	if (!vertex_declaration.typeless_decl)
	{
		vertex_declaration = createDXVertexDeclaration(device, pUsage);
		verify(stashIntAddPointer(device->vertex_declarations, key, vertex_declaration.typeless_decl, false));
	}

	return vertex_declaration;
}

void rxbxFreeAllGeometryDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	StashElement elem;
	StashTableIterator iter;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	stashGetIterator(device->geometry_cache, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		GeoHandle handle = stashElementGetIntKey(elem);
		rxbxFreeGeometryDirect(device, &handle, NULL);
	}

	stashTableClear(device->geometry_cache);
}
