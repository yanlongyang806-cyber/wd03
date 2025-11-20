#pragma once
GCC_SYSTEM

#include "RdrDrawable.h"
#include "MaterialEnums.h"

typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;
typedef struct ShaderGraphHandleData ShaderGraphHandleData;
typedef struct ShaderGraphRenderInfo ShaderGraphRenderInfo;
typedef struct RdrShaderFinishedData RdrShaderFinishedData;
typedef struct DynamicCache DynamicCache;
typedef struct RxbxProgramDef RxbxProgramDef;
typedef struct MaterialAssemblerProfile MaterialAssemblerProfile;

typedef struct PrecompiledShaderHeader
{
	U32 updb_filename_size; // includes \0
	U32 updb_size;
	U32 data_size;
} PrecompiledShaderHeader;

typedef struct GfxShaderFinishedData
{
	const char *filename;
	ShaderGraphHandleData *handle_data;
} GfxShaderFinishedData;

typedef struct PrecompiledShaderData {
	ShaderGraphHandleData *handle_data;
	PrecompiledShaderHeader *precompiled_shader; // Needs to be freed // Data comes after this
	U32 rendererIndex:5; // What renderer is this for
} PrecompiledShaderData;

const char *gfxShaderGetDebugName(const char *graph_filename, RdrMaterialShader shader_type);
char *gfxShaderGetText(ShaderGraphRenderInfo *graph_render_info, const MaterialAssemblerProfile *mat_profile, RdrMaterialShader shader_num, FileList *file_list, bool force_get);
void gfxShaderCompileForXbox(ShaderGraphRenderInfo *graph_render_info, RdrMaterialShader shader_num);

void gfxCacheShaderGraphForOtherTarget(ShaderGraphRenderInfo *graph_render_info, ShaderGraphReflectionType reflectionType,
									   RdrMaterialShader shader_num, const char *intrinsic_defines, const char *shader_model,
									   const MaterialAssemblerProfile *mat_profile);

void gfxDemandLoadShaderGraphInternal(ShaderGraphHandleData *handle_data);
void gfxDemandLoadPrecompiled(PrecompiledShaderData *psd);
void gfxLoadPrecompiledShaderInternal(PrecompiledShaderHeader *psh, ShaderHandle handle, const char *shader_debug_name);
void shaderCacheFinishedCompiling(RdrShaderFinishedData *data);
void gfxShaderUpdateCache(DynamicCache *shaderCache, const char *filename, const char *updb_filename, void *updb_data, int updb_data_size, void *shader_data, int shader_data_size, FileList *file_list);
void gfxVerifyLightCombos(RdrMaterialShader shader_num);
void gfxShaderAddDefines(ShaderGraphRenderInfo *graph_render_info, RdrMaterialShader shader_num,
	ShaderGraphReflectionType graph_reflection_type,
	const char *special_defines[], int special_defines_size,
	int *special_defines_count);

extern PrecompiledShaderData **queuedShaderGraphCompileResults;
