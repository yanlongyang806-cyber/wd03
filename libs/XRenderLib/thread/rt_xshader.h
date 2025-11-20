#pragma once
GCC_SYSTEM

#include "rt_xstate.h"
#include "D3DCompiler.h"

typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct RdrShaderParams RdrShaderParams;
typedef struct RdrShaderPerformanceValues RdrShaderPerformanceValues;

typedef char *STR;

#ifdef HLSL
// some typedefs to make Intellisense/VisualAssist smarter on .hlsl files
typedef struct float2 {
	int x, y, xx, yy, xy;
} float2;
typedef struct float3 {
	int x, y, z, xxx, yyy, zzz, xyz;
} float3;
typedef struct float4 {
	int x, y, z, w, xxx, yyy, zzz, wwww, xyzw;
} float4;
#endif

#if _XBOX
typedef struct MicrocodeJump
{
	int bool_constant;
	bool inverted;
	int src_offset;
	int skip_offset;
	int dst_offset;
	char *dst_label; // only used during processing
} MicrocodeJump;
#endif

typedef struct RxbxPixelShader
{
	RdrPixelShaderObj shader; // Must remain first (cast from RxbxVertexShader)

#if _XBOX
	char *microcode_text;
	int microcode_text_len;
	MicrocodeJump **microcode_jumps;
	StashTable shader_variations;
#endif

	// Performance values:
	int instruction_count;
	int texture_fetch_count;
	int temporaries_count;
	int d3d_instruction_slots;
	int d3d_alu_instruction_slots;
	U32 pixel_count;
	U32 pixel_count_last; 
	U32 pixel_count_frame;
	U64 nvps_pps; // If rdr_state.runNvPerfShader, gives the PPS on a NV43-GT card
	const char *debug_name;

	ID3D11ShaderReflection * reflection;
	byte texture_resource_slot[MAX_TEXTURE_UNITS_TOTAL];
	U32 buffer_sizes[PS_CONSTANT_BUFFER_COUNT];

	U32 is_error_shader:1;
	U32 used_shader:1;
} RxbxPixelShader;

typedef struct RxbxVertexShader
{
	RdrVertexShaderObj shader; // Must remain first (cast to RxbxPixelShader)
	const char *debug_name;
	U16 input_signature_index; // For DX11, index into a pooled list of input signatures
	U16 is_error_shader:1;
} RxbxVertexShader;

// the hull and domain shaders are going to be cast as the rxbxtessshader for use in the d3dcreatetessshader
typedef struct RxbxHullShader
{
	ID3D11HullShader *shader; // Must remain first (cast to RxbxPixelShader)
	const char *debug_name;
	U16 is_error_shader:1;
} RxbxHullShader;

typedef struct RxbxDomainShader
{
	ID3D11DomainShader *shader; // Must remain first (cast to RxbxPixelShader)
	const char *debug_name;
	U16 is_error_shader:1;
	byte texture_resource_slot[MAX_DOMAIN_TEXTURE_UNITS_TOTAL];
} RxbxDomainShader;

RdrPixelShaderObj rxbxCreateD3DPixelShader(RdrDeviceDX *device, RxbxPixelShader *pshader, const char *filename, void *compiled_data, int compiled_data_size, bool is_precompiled, bool is_assembled, ShaderHandle shader_handle, U32 new_crc);
// compiled_data might be NULL'd and taken ownership of
RdrVertexShaderObj rxbxCreateD3DVertexShader(RdrDeviceDX *device, RxbxVertexShader *vshader, const char *filename, void **compiled_data, int compiled_data_size, ShaderHandle shader_handle, U32 new_crc);

typedef struct RxbxPreloadedShaderData
{
    const void *data;
    U32 size;
} RxbxPreloadedShaderData;

int rxbxLoadVertexShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count);
void rxbxLoadVertexShaderAsync(RdrDeviceDX *device, RxbxProgramDef *def, ShaderHandleAndFlags *shader);
void rxbxLoadPixelShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count);
int rxbxLoadHullShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count);
int rxbxLoadDomainShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count);

void rxbxSetPixelShaderDataDirect(RdrDeviceDX *device, RdrShaderParams *params, WTCmdPacket *packet);
void rxbxQueryShaderPerfDirect(RdrDeviceDX *device, RdrShaderPerformanceValues **params_ptr, WTCmdPacket *packet);
void rxbxQueryPerfTimesDirect(RdrDeviceDX *device, RdrDevicePerfTimes **params_ptr, WTCmdPacket *packet);

void rxbxFreePixelShader(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxPixelShader *pixel_shader);
void rxbxFreeVertexShaderInternal(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxVertexShader *vertex_shader, bool bCancelIfNotComplete);
void rxbxFreeVertexShader(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxVertexShader *vertex_shader);
void rxbxFreeHullShaderInternal(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxHullShader *hull_shader, bool bCancelIfNotComplete);
void rxbxFreeDomainShaderInternal(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxDomainShader *domain_shader, bool bCancelIfNotComplete);

void rxbxInitBackgroundShaderCompile(void);
void rxbxDealWithAllCompiledResultsDirect(RdrDeviceDX *device);

