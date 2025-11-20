#pragma once
GCC_SYSTEM

#include "RdrEnums.h"

typedef struct ShaderGraphHandleData ShaderGraphHandleData;
typedef struct MaterialRenderInfo MaterialRenderInfo;

// Per-frame materials system calls go in here (and have optimization on)

void destroyShaderGraphHandleData(ShaderGraphHandleData *handle_data);

void gfxDemandLoadMaterialAtDrawTime(MaterialRenderInfo *render_info, F32 dist, F32 uv_density);

RdrMaterialShader gfxShaderFilterShaderNum(RdrMaterialShader shader_num);

void setupMaterialRenderInfoTexReflection(MaterialRenderInfo *render_info, const char *tex_input_name);

extern const char *cms_scale, *cms_rotation, *cms_rotationRate;
