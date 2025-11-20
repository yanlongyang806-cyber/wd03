#pragma once
GCC_SYSTEM

#include "RdrEnums.h"

typedef struct RdrLightDefinition RdrLightDefinition;
typedef struct RdrDevice RdrDevice;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndLightingModel);
typedef struct GfxLightingModel
{
	char *device_id;
	char *name;
	const char *filename; AST(CURRENTFILE)
	char *text;
	char *perlight;
	char *lightshadowbuffer;
} GfxLightingModel;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndMaterialProfile") AST_STRIP_UNDERSCORES;
typedef struct MaterialAssemblerProfile {
	const char *filename; AST( CURRENTFILE )
	char *profile_name;	AST( STRUCTPARAM )
	char *device_id;
	char *gen_temp;
	char *gen_param;
	char *gen_boolparam;
	char *gen_paramMatrix42;
	char *gen_paramMatrix43;
	char *gen_paramMatrix44;
	int gen_param_offset;
	char *comment_begin;
	char *comment_end;
	char *gen_texcoord0;	AST( NAME(TexCoord0) )
	char *gen_texcoord1;	AST( NAME(TexCoord1) )
	char *gen_color0;		AST( NAME(Color0) )
	char *gen_instance_param;		AST( NAME(InstanceParam) )
	char *gen_const[4];		AST( INDEX(0, "GenConst1") INDEX(1, "GenConst2") INDEX(2, "GenConst3") INDEX(3, "GenConst4") )
	char *gen_texname;
	char *swizzlemap;		AST( DEF("xyzw") )
	char *gen_shader_path;
	char *gen_special_shader_path;
	char *gen_texsampler2D;
	char *gen_texsampler3D;
	char *gen_texsamplerCUBE;
	char *gen_texsampler_part2;
	char *gen_texsampler_part2_comparison;
	char *gen_conditional_branch;
	char *gen_conditional_if;
	char *gen_conditional_else;
	char *gen_conditional_elseif;
	char *gen_conditional_end;
	char *shadow_accumulate;
	char *constant_buffer_start;
	char *constant_buffer_end;
	char *material_buffer_name;
	int material_buffer_base_register;
	char *light_buffer_name;
	int light_buffer_base_register;
	int light_ignore_material_const_offset;
} MaterialAssemblerProfile;

const MaterialAssemblerProfile *getMaterialProfileForDevice(const char *device_profile_name);
const char *getCodeForOperation(const MaterialAssemblerProfile *profile, const char *op_type_name, char **filename_ptr);
void gfxLoadMaterialAssemblerProfiles(void);
void gfxMaterialProfileReload(void);
void gfxSetMaterialProfileSettingsForDevice(RdrDevice *device);

const GfxLightingModel *gfxGetLightingModel(const char *device_id, const char *lighting_model_name);
const RdrLightDefinition *gfxGetLightDefinition(const char *device_id, RdrLightType light_type);
const RdrLightDefinition **gfxGetLightDefinitionArray(const char *device_id);
