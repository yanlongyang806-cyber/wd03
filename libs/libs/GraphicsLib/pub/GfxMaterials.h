#ifndef GFXMATERIALS_H
#define GFXMATERIALS_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "GfxTextureEnums.h"
#include "MaterialEnums.h"
#include "WorldLibEnums.h"
#include "RdrDrawable.h"
#include "../GfxEnums.h" // TODO: Move this header file to be public

typedef struct BasicTexture BasicTexture;
typedef struct Material Material;
typedef struct ShaderGraph ShaderGraph;
typedef struct ShaderTemplate ShaderTemplate;
typedef struct ShaderInput ShaderInput;
typedef struct RdrSubobject RdrSubobject;
typedef struct StashTableImp* StashTable;
typedef struct ShaderGraphRenderInfo ShaderGraphRenderInfo;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct MaterialNamedTexture MaterialNamedTexture;
typedef struct RdrLightDefinition RdrLightDefinition;
typedef struct RdrShaderPerformanceValues RdrShaderPerformanceValues;
typedef struct MaterialAssemblerProfile MaterialAssemblerProfile;
typedef struct MaterialDraw MaterialDraw;
typedef struct MaterialConstantMapping MaterialConstantMapping;
typedef struct MaterialNamedDynamicConstant MaterialNamedDynamicConstant;
typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;
typedef struct MaterialSwap MaterialSwap;
typedef struct RemeshAssetSwap RemeshAssetSwap;
typedef struct TaskProfile TaskProfile;
typedef struct WorldClusterGeoStats WorldClusterGeoStats;

typedef struct ShaderGraphHandleData {
	ShaderHandle handle;
	RdrMaterialShader shader_num;
	S64 shader_HashKey;
	ShaderGraphRenderInfo *graph_render_info; // backpointer
	U32 load_state:31;			// Which renderers have been sent the data.  Bit mask.
	U32 load_needed:1;
	U32 loading:1;				// Either we've sent text to the card and are waiting for it to be compiled, or we're loading the cached compiled result
	U32 sent_to_renderer:1;		// A handle to this was sent to the renderer
	U32 sent_to_dcache:1;		// A handle to this was sent to the dynamic cache
	U32 freeme:1;				// We want this freed, destroy it once it comes back from the renderer
	U32 rendererIndex:5;		// When sent to the dynamic cache, what renderer requested it
	U32 reflectionType:2;		// What kind of reflection was this shader compiled with
	U32 graph_data_last_used_time_stamp;
	RdrShaderPerformanceValues *performance_values;
} ShaderGraphHandleData;

typedef struct ShaderGraphRenderInfo {
	RdrMaterialShader cached_last_key;
	ShaderGraphHandleData *cached_last_value;
	StashTable shader_handles;

	U32 graph_last_used_time_stamp;
	U32 graph_last_used_count;
	U32 graph_last_used_tricount;
	U32 graph_last_used_count_swapped; // Previous frame
	U32 graph_last_used_tricount_swapped; // Previous frame
	U32 graph_last_used_swap_frame; // What frame the swapped value is from

	U32 graph_last_updated_time_stamp; // What frame this was last added to queuedShaderGraphLoads
	ShaderGraph *shader_graph; // Backpointer to parent, for demand loading
	char *shader_text; // Cache for needing to build on other devices or for different shader modes, EString
	FileList source_files; // List of source files and timestamps used to assemble the shader text
//
	char	   *shader_text_pre_light; // text before lighting, EString
	int			shader_text_pre_light_length;
	const char *shader_text_per_light; // per-light loop text, pointer into a GfxLightingModel somewhere
	int         shader_text_per_light_length;
	const char *shader_text_post_light; // text after lighting, pointer into shader_text_pre_light
	int         shader_text_post_light_length;
	const char *shader_text_shadow_buffer; // shadow buffer glue text, pointer into a GfxLightingModel somewhere
	int			shader_text_shadow_buffer_length;
//
	int num_textures;
//
	U32 num_input_texnames;
	U32 num_input_vectors;
	U32 num_input_mappings; // Number of constant mappings required
	U32 num_drawable_mappings; // Number of per-drawable constant mappings required
	U32 instance_param_index;
	U32 instance_param_size:3; // 0-4
	U32 need_texture_screen_color:1;
	U32 need_texture_screen_depth:1;
	U32 need_texture_screen_color_blurred:1;
	U32 has_been_preloaded:1;

	RdrShaderPerformanceValues *performance_values;
	MaterialAssemblerProfile const *mat_profile;  // If this changes, we must rebuild
} ShaderGraphRenderInfo;

typedef struct MaterialRenderInfo {
	ShaderGraphRenderInfo *graph_render_info;
	// Mapping between programmable constants (scrolling, constant colors, etc) and indices into vec4s (just an array of ShaderDataTypes?)
	MaterialConstantMapping *constant_mapping;
	U32 constant_mapping_count:8;
	U32 usage_flags:WLUsageFlags_NumBits;
	ShaderGraphReflectionType graph_reflection_type:3;
	U32 max_reflect_resolution:4; // log2 of it

	const char **constant_names; // Array of named constants, length(rdr_material.const_count)*4 (one for each swizzle start pos), allocAddString()'d

	BasicTexture **textures; // NOT an EArray
	const char **texture_names;
	BasicTexture *override_cubemap_texture;
	BasicTexture *override_spheremap_texture;
	BasicTexture *heightmap;
	F32 heightmap_scale;
	F32 uv_scale;

	U32 material_last_used_time_stamp; // For tracking what frame this material was added to the list
	U32 material_last_used_count;
	F32 material_min_uv_density;
	F32 material_min_draw_dist[GfxAction_MAX];

	// actual data sent to the renderer
	RdrMaterial rdr_material;
	RdrNonPixelMaterial *rdr_domain_material;
} MaterialRenderInfo;

void gfxLoadMaterials(void); // Loads material definitions
void gfxDemandLoadMaterialAtQueueTimeEx(RdrSubobject *subobject, Material *material, BasicTexture **eaTextureSwaps, const MaterialNamedConstant **eaNamedConstants, const MaterialNamedTexture **eaNamedTextures, const MaterialNamedDynamicConstant **eaNamedDynamicConstants, bool apply_use_flags_to_textures, Vec4 instance_param, F32 dist, F32 uv_density);
#define gfxDemandLoadMaterialAtQueueTime(subobject, material, eaTextureSwaps, eaNamedConstants, eaNamedTextures, eaNamedDynamicConstants, instance_param, dist, uv_density) gfxDemandLoadMaterialAtQueueTimeEx(subobject, material, eaTextureSwaps, eaNamedConstants, eaNamedTextures, eaNamedDynamicConstants, true, instance_param, dist, uv_density)
MaterialRenderInfo *gfxDemandLoadPreSwappedMaterialAtQueueTime(RdrSubobject *subobject, MaterialDraw *draw_material, F32 dist, F32 uv_density);
void gfxMaterialsClearAllForDevice(int rendererIndex);
void gfxMaterialsRecalcAlphaSort(void);
void gfxMaterialsReloadAll(void);
void gfxMaterialRebuildTextures(void);
void gfxDemandLoadMaterials(bool earlyFrameManualCall);
void gfxMaterialsOncePerFrame(void);
void gfxMaterialsHandleNewDevice(void);
void gfxMaterialsAssertTexNotInDrawList(const BasicTexture *bind);
void gfxMaterialTemplateUnloadShaders(ShaderTemplate *templ);
void gfxMaterialsReloadShaders(void);
void gfxMaterialTemplateUnloadShaderText(ShaderTemplate *templ);
void gfxMaterialsFreeShaderText(void);
void gfxMaterialMaxReflectionChanged(void);

void gfxMaterialsInitMaterial(Material *material, int bInitTextures);
void gfxMaterialsDeinitMaterial(Material *material);

void gfxMaterialsDeinitShaderTemplate(ShaderTemplate *templ);

ShaderHandle gfxMaterialFillShader(ShaderGraphRenderInfo *graph_render_info, RdrMaterialShader shader_num, U32 tri_count);

char *gfxAssembleLights(RdrMaterialShader shader_num, const MaterialAssemblerProfile *mat_profile, 
						int first_light_const_index, int first_light_bool_index, int first_light_tex_index,
						const char *pre_light_text, int pre_light_text_length,
						const char *per_light_text, int per_light_text_length,
						const char *post_light_text, int post_light_text_length,
						const char *shadow_buffer_text, int shadow_buffer_text_length,
						bool shadow_cast_only,
						FileList *file_list);

bool gfxMaterialCanOcclude(Material *material);
bool gfxMaterialCheckSwaps(Material *material, const char **eaTextureSwaps, 
						   const MaterialNamedConstant **eaNamedConstants, 
						   const MaterialNamedTexture **eaNamedTextures, 
						   const MaterialNamedDynamicConstant **eaNamedDynamicConstants, 
						   int *texturesNeeded, int *constantsNeeded, int *constantMappingsNeeded,
						   Vec4 instance_param);
void gfxMaterialApplySwaps(MaterialDraw *draw_material, Material *material, const char **eaTextureSwaps, 
						   const MaterialNamedConstant **eaNamedConstants, 
						   const MaterialNamedTexture **eaNamedTextures, 
						   const MaterialNamedDynamicConstant **eaNamedDynamicConstants, 
						   WLUsageFlags use_category);

bool gfxMaterialUsesTexture(Material *material, const char *texturename);

U32 gfxMaterialScoreFromValues(const RdrShaderPerformanceValues *perf_values);
U32 gfxMaterialScoreFromValuesRaw(U32 tex_count, U32 alu_count, U32 temp_count, U32 dynamic_constant_count);

F32 gfxMaterialGetPerformanceScore(const Material *material);

typedef struct MaterialData MaterialData;
bool gfxMaterialValidateMaterialData(MaterialData *material_data, ShaderTemplate **overrides);

AUTO_STRUCT;
typedef struct ShaderGraphUsageEntry
{
	const char *filename; AST( POOL_STRING )
	U32 countInScene;
	U32 tricountInScene;
	U32 variationCount;
	U32 materialScore;
	U32 rawMaterialScore;
	int instruction_count;
	int texture_fetch_count;
	int temporaries_count;
	int dynamic_constant_count;
	U32 pixels;
	U64 factor;
} ShaderGraphUsageEntry;
extern ParseTable parse_ShaderGraphUsageEntry[];
#define TYPE_parse_ShaderGraphUsageEntry ShaderGraphUsageEntry
void gfxMaterialGetShaderUsageDetailed(ShaderGraphUsageEntry ***entry_list);

AUTO_STRUCT;
typedef struct MaterialUsageEntry
{
	const char *filename; AST( POOL_STRING )
	U32 countInScene;
} MaterialUsageEntry;
extern ParseTable parse_MaterialUsageEntry[];
#define TYPE_parse_MaterialUsageEntry MaterialUsageEntry
void gfxMaterialGetUsageDetailed(MaterialUsageEntry ***entry_list);

void gfxMaterialSelectByFilename(const char *filename);
void gfxNoErrorOnNonPreloadedInternal(int disable);
void gfxMaterialsInitShaderTemplate(ShaderTemplate *templ);
bool gfxMaterialShaderTemplateIsPreloaded(SA_PARAM_NN_VALID const ShaderTemplate *shader_template);

void gfxMaterialSetOverrideCubeMap(const char *cubemap_name);

ShaderTemplate **materialGetTemplatesUsedByMap(const char *zmap);


bool gfxMaterialValidateForFx(Material *material, const char **ppcTemplateName, bool isForSplat);

bool gfxMaterialNeedsScreenGrab(SA_PARAM_NN_VALID const Material *material);
bool shaderDataTypeNeedsMapping(ShaderDataType data_type);
bool gfxMaterialsShouldSkipOpInput(const ShaderInput *op_input, const ShaderGraph *graph);

void gfxMaterialDrawFixup(MaterialDraw *draw_const);

typedef struct SimplygonMaterial SimplygonMaterial;
typedef struct SimplygonMaterialTable SimplygonMaterialTable;
typedef struct TextureSwap TextureSwap;
typedef struct ModelClusterTextures ModelClusterTextures;
SimplygonMaterial *gfxSimplygonMaterialFromMaterial(
	Material *material, TextureSwap **eaTextureSwaps, MaterialSwap **eaMaterialSwaps,
	const char *uniqueName, const char *tempDir,
	int *totalTexSize, StashTable materialTextures);
int gfxGetSimplygonMaterialIdFromTable(
	SimplygonMaterialTable *table, Material *material,
	TextureSwap **eaTextureSwaps, const char *tempDir, TaskProfile *texExport, 
	WorldClusterGeoStats *inputGeoStats);

void gfxMaterialClusterTexturesFromMaterial(
	Material *material,
	RemeshAssetSwap **eaTextureSwaps,
	RemeshAssetSwap **eaMaterialSwaps,
	const char *tempDir,
	ModelClusterTextures ***mcTextures,
	StashTable materialTextures,
	TaskProfile *texExport, 
	WorldClusterGeoStats *inputGeoStats);

bool gfxMaterialHasTransparency(Material *material);
int gfxMaterialGetTextures(Material* material, const BasicTexture ***textureHolder);

extern StashTable g_stTextureOpOverride;

#endif
