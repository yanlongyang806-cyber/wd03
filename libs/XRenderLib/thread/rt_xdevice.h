#ifndef _RT_XDEVICE_H_
#define _RT_XDEVICE_H_

#include "xdevice.h"

typedef struct RxbxQuery RxbxQuery;
typedef struct RxbxPixelShader RxbxPixelShader;
typedef struct RdrOcclusionQueryResult RdrOcclusionQueryResult;

const char * rxbxGetTextureFormatString(D3DFORMAT format);

void rxbxCreateDirect(RdrDeviceDX *device, WindowCreateParams *params, WTCmdPacket *packet);
void rxbxCreateDirect11(RdrDeviceDX *device, WindowCreateParams *params, WTCmdPacket *packet);
void rxbxBeginSceneDirect(RdrDeviceDX *device, RdrDeviceBeginSceneParams *params, WTCmdPacket *packet);
void rxbxEndSceneDirect(RdrDeviceDX *device, RdrDeviceEndSceneParams *params, WTCmdPacket *packet);
void rxbxDestroyDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);

int rxbxIsInactiveDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);
void rxbxReactivateDirect(RdrDeviceDX *device);
void rxbxResizeDeviceDirect(RdrDevice *device, DisplayParams *dimensions, RdrResizeFlags flags);
void rxbxHandleLostDeviceDirect(RdrDeviceDX *device);

void rxbxSetVsyncDirect(RdrDeviceDX *device, int *enable_ptr, WTCmdPacket *packet);
void rxbxSetGammaDirect(RdrDeviceDX *device, F32 *gamma_ptr, WTCmdPacket *packet);
void rxbxNeedGammaResetDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);

void rxbxFlushGPUDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);
void rxbxFlushGPUDirectEx(RdrDeviceDX *device, bool allowSleep);

void rxbxDestroyAllSecondarySurfacesDirect(RdrDeviceDX *device);
void rxbxDeviceDetachDeadSurfaceTextures(RdrDeviceDX *device);

void rxbxQueryAllSurfacesDirect(RdrDevice *device, RdrSurfaceQueryAllData **data, WTCmdPacket *packet);

#if !PLATFORM_CONSOLE
void rxbxSetTitleDirect(RdrDeviceDX *device, char *title);
void rxbxSetSizeDirect(RdrDeviceDX *device, U32 width, U32 height);
void rxbxSetIconDirect(RdrDeviceDX *device, int resource_id);
void rxbxProcessWindowsMessagesDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);
void rxbxShowDirect(RdrDeviceDX *device, int *pnCmdShow, WTCmdPacket *packet);
#endif

void rxbxAppCrashed(RdrDevice *device);

int rxbxIssueSyncQuery(RdrDeviceDX *device);

void rxbxStartOcclusionQueryDirect(RdrDeviceDX *device, RxbxPixelShader *pshader, RdrOcclusionQueryResult *rdr_query);
void rxbxFinishOcclusionQueryDirect(RdrDeviceDX *device, RxbxOcclusionQuery *query);
void rxbxProcessQueriesDirect(RdrDeviceDX *device, bool flush);

void rxbxFreeQueryDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);
void rxbxFlushQueriesDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);

// temporary vbo memory allocator, returns false if the lock failed
HRESULT rxbxAppendVBOMemory(RdrDeviceDX *device, RdrVBOMemoryDX *vbo_chunk, const void *data, int byte_count);
HRESULT rxbxAppendIBOMemory(RdrDeviceDX *device, RdrIBOMemoryDX *ibo_chunk, void *data, int byte_count);
bool rxbxAllocTempVBOMemory(RdrDeviceDX *device, const void *data, int byte_count, RdrVertexBufferObj *vbo_out, int *vbo_offset_out, bool fatal);
bool rxbxAllocTempIBOMemory(RdrDeviceDX *device, const void *data, int indices, bool b32Bit, RdrIndexBufferObj *ibo_out, int *ibo_offset_out, bool fatal);

void rxbxDumpDeviceState(const RdrDeviceDX *device, int trivia_log, int dump_objects);
void rxbxDumpDeviceStateOnError(const RdrDeviceDX *device, HRESULT hr);

__forceinline void rxbxSetupVBODrawVerticesUP(RdrDeviceDX *device, int vertex_count, const void * vertex_data, int stride, D3D11_PRIMITIVE_TOPOLOGY d3d11_prim_type)
{
	// emulate UP calls
	RdrVertexBufferObj temp_vbo;
	int vbo_offset_out = 0;

	rxbxAllocTempVBOMemory(device, vertex_data, vertex_count*stride, &temp_vbo, &vbo_offset_out, true);
	rxbxSetVertexStreamSource(device, 0, temp_vbo, stride, vbo_offset_out);

	if (device->d3d11_device)
		ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, d3d11_prim_type);
}

__forceinline int rxbxSetupVBODrawIndexed16VerticesUP(RdrDeviceDX *device, int index_count, const U16 * index_data,
	int vertex_count, const void * vertex_data, int stride)
{
	RdrVertexBufferObj temp_vbo;
	int vbo_offset_out = 0;
	RdrIndexBufferObj temp_ibo;
	int ibo_offset_out = 0;

	rxbxAllocTempVBOMemory(device, vertex_data, vertex_count*stride, &temp_vbo, &vbo_offset_out, true);
	rxbxSetVertexStreamSource(device, 0, temp_vbo, stride, vbo_offset_out);

	rxbxAllocTempIBOMemory(device, index_data, index_count, false, &temp_ibo, &ibo_offset_out, true);
	rxbxSetIndices(device, temp_ibo, false);

	return ibo_offset_out;
}

__forceinline int rxbxSetupVBODrawIndexed32VerticesUP(RdrDeviceDX *device, int index_count, const U32 * index_data, 
	int vertex_count, const void * vertex_data, int stride)
{
	RdrVertexBufferObj temp_vbo;
	int vbo_offset_out = 0;
	RdrIndexBufferObj temp_ibo;
	int ibo_offset_out = 0;

	rxbxAllocTempVBOMemory(device, vertex_data, vertex_count*stride, &temp_vbo, &vbo_offset_out, true);
	rxbxSetVertexStreamSource(device, 0, temp_vbo, stride, vbo_offset_out);

	rxbxAllocTempIBOMemory(device, index_data, index_count, true, &temp_ibo, &ibo_offset_out, true);
	rxbxSetIndices(device, temp_ibo, true);

	return ibo_offset_out;
}


#endif //_RT_XDEVICE_H_


