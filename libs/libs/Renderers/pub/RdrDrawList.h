#ifndef _RDRDRAWLIST_H_
#define _RDRDRAWLIST_H_
GCC_SYSTEM

#include "utils.h"
#include "RdrDrawable.h"

typedef struct RdrDrawList RdrDrawList;
typedef struct RdrLightParams RdrLightParams;
typedef struct RdrDevice RdrDevice;
typedef struct ShaderGraphRenderInfo ShaderGraphRenderInfo;
typedef struct Frustum Frustum;
typedef struct RdrLightDefinition RdrLightDefinition;
typedef struct RdrDrawListPassData RdrDrawListPassData;
typedef struct ModelLOD ModelLOD;
typedef struct DynSkinningMatSet DynSkinningMatSet;

typedef ShaderHandle (*RdrMaterialFillShaderCallback)(ShaderGraphRenderInfo *graph_render_info, RdrMaterialShader shader_num, U32 tri_count);
void rdrSetFillShaderCallback(RdrMaterialFillShaderCallback callback);

typedef void (*RdrDrawListDataSkinningMatDecFunc)(DynSkinningMatSet* skinning_mat_set);
void rdrSetSkinningMatDecFunc(RdrDrawListDataSkinningMatDecFunc callback);

SA_RET_NN_VALID RdrDrawList *rdrCreateDrawList(bool is_primary_drawlist);
void rdrFreeDrawList(SA_PARAM_OP_VALID RdrDrawList *draw_list);

void rdrDrawListBeginFrame(SA_PARAM_NN_VALID RdrDrawList *draw_list, bool zprepass, bool zprepass_test, bool outlining, 
						   bool sort_opaque_models, bool use_shadow_buffer, bool globally_using_shadow_buffer, bool use_ssao, RdrDrawListLightingMode light_mode, 
						   bool separate_hdr_pass, bool has_hdr_texture, F32 bloom_brightness, F32 dof_distance, F32 mem_multiplier,
						   bool aux_visual_pass);
int rdrDrawListAddPass(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_OP_VALID Frustum *frustum, RdrShaderMode shader_mode, SA_PARAM_OP_VALID RdrLight *shadow_caster, int shadowmap_idx, int parent_pass_idx, bool depth_only);
RdrDrawListPassData *rdrDrawListGetPasses(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrDrawListPassData ***passes, Vec4 visual_pass_view_z); // returns the visible frustum bit
void rdrDrawListSendLightsToRenderer(RdrDevice *device, RdrDrawList *draw_list, const RdrLightDefinition **light_defs);
void rdrDrawListSendVisualPassesToRenderer(SA_PARAM_NN_VALID RdrDevice *device, SA_PARAM_NN_VALID RdrDrawList *draw_list, const RdrLightDefinition **light_defs);
void rdrDrawListSendShadowPassesToRenderer(SA_PARAM_NN_VALID RdrDevice *device, SA_PARAM_NN_VALID RdrDrawList *draw_list, const RdrLightDefinition **light_defs);
int rdrDrawListDrawObjects(SA_PARAM_NN_VALID RdrDevice *device, SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrSortType sort_type, bool hdr_pass, bool is_low_res_edge_pass, bool manual_depth_test);
void rdrDrawListDrawShadowPassObjects(SA_PARAM_NN_VALID RdrDevice *device, SA_PARAM_NN_VALID RdrDrawList *draw_list, int pass_num, F32 depth_bias, F32 slope_scale_depth_bias);
void rdrDrawListDrawAuxVisualPassObjects(RdrDevice *device, RdrDrawList *draw_list);
void rdrDrawListEndFrame(SA_PARAM_OP_VALID RdrDrawList *draw_list);

void rdrDrawListGetForegroundStats(SA_PARAM_NN_VALID RdrDrawList *draw_list, 
						 		   int *objects_drawn, int *triangles_drawn, 
						 		   int *prepass_objects_drawn, int *prepass_triangles_drawn,
						 		   int *shadow_objects_drawn, int *shadow_triangles_drawn,
						 		   int *hdr_objects_drawn, int *hdr_triangles_drawn);
void rdrDrawListAddStats(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawListStats *stats);

int rdrDrawListNeedsScreenGrab(SA_PARAM_NN_VALID RdrDrawList *draw_list);
int rdrDrawListNeedsScreenGrabLate(SA_PARAM_NN_VALID RdrDrawList *draw_list);
int rdrDrawListNeedsScreenDepthGrab(SA_PARAM_NN_VALID RdrDrawList *draw_list); // Early or late
int rdrDrawListNeedsBlurredScreenGrab(SA_PARAM_NN_VALID RdrDrawList *draw_list);

int rdrDrawListPassObjectCount(SA_PARAM_NN_VALID RdrDrawList *draw_list, int pass_num);
int rdrDrawListObjectCount(SA_PARAM_NN_VALID RdrDrawList *draw_list);
int rdrDrawListIsEmpty(SA_PARAM_NN_VALID RdrDrawList *draw_list);
bool rdrDrawListSortBucketIsEmpty(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrSortBucketType sbt);
int rdrDrawListHasAuxPassObjects(RdrDrawList *draw_list);

void rdrSetDrawListSurfaceTexHandleFixup(SA_PARAM_NN_VALID RdrDrawList *draw_list, int surface_fixup_index, 
									TexHandle fixedup_surface_texture); 

void rdrSortLights(SA_PARAM_NN_VALID RdrLightParams *light_params);
bool rdrDrawListLightsFit(const RdrLight * const lights[MAX_NUM_OBJECT_LIGHTS], const PreloadedLightCombo *light_combo, bool using_shadowbuffer);
StashTable rdrDrawListGetLightComboUsage(void);

//////////////////////////////////////////////////////////////////////////

typedef struct RdrLightParams
{
	RdrLight				*lights[MAX_NUM_OBJECT_LIGHTS];
	RdrAmbientLight			*ambient_light;
	RdrVertexLight			*vertex_light;
	RdrLightColorType		light_color_type;
} RdrLightParams;

typedef struct RdrAddInstanceParams
{
	RdrInstance				instance;
	RdrSubobject			**subobjects;
	RdrInstancePerDrawableData *per_drawable_data;
	const RdrLightParams	*light_params;
	Vec3					ambient_multiplier;
	Vec3					ambient_offset;

	F32						distance_offset;
	int						frustum_visible;
	F32						screen_area;

	RdrMaterialFlags		add_material_flags;

	Vec4					wind_params;

	U32						no_outlining:1;
	U32						no_zprepass:1;
	U32						wireframe:2;
	U32						force_no_shadow_receive:1;
	U32						uses_far_depth_range:1;
	U32						needs_late_alpha_pass_if_need_grab:1; // For FX doing refraction
	U32						camera_centered:1;
	U32						aux_visual_pass:1;
	U32						debug_me:1;
	U32						has_wind:1;
	U32						has_trunk_wind:1;
	U32						ignore_vertex_colors:1;
	U32						two_bone_skinning:1;
	U32						no_two_bone_skinning:1;
	U32						disable_vertex_light:1;
	U32						force_hdr_pass:1; // If possible, force this object into the HDR pass
	U32						need_dof:1; // If an alpha object, go into the pre-dof alpha bucket
	U32						force_shadow_cast:1;
	U32						skyDepth:1;

	U32						queued_as_alpha:1; // Passed back up to caller
	U32						engine_fade:1;
							// 19 bits

	U32						uniqueDrawCount; // Passed back up to caller

	F32						zdist;  // Just used by caller for now

} RdrAddInstanceParams;

typedef struct RdrAddFastParticleParams
{
	F32						zdist;
	F32						zbias;
	F32						brightness;
	F32						screen_area;
	U32						wireframe:2;
	U32						uses_far_depth_range:1;
} RdrAddFastParticleParams;


#define RDRALLOC_SUBOBJECT_PTRS(instance_params, material_count) (instance_params).subobjects = _alloca((material_count) * sizeof(RdrSubobject *))

#define RDRALLOC_SUBOBJECTS(draw_list, instance_params, model, iter)							\
			RDRALLOC_SUBOBJECT_PTRS(instance_params, (model)->data->tex_count);					\
			for ((iter) = 0; (iter) < (model)->data->tex_count; ++(iter)) { (instance_params).subobjects[(iter)] = rdrDrawListAllocSubobject((draw_list), (model)->data->tex_idx[(iter)].count); }


SA_RET_NN_VALID RdrSubobject *rdrDrawListAllocSubobject(SA_PARAM_NN_VALID RdrDrawList *draw_list, int tri_count);

SA_RET_OP_VALID RdrDrawableGeo *rdrDrawListAllocGeo(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, SA_PARAM_OP_VALID ModelLOD *debug_model_backpointer, int material_count, int num_vs_constants, int num_vs_textures);
void rdrDrawListAddGeoInstance(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableGeo *draw, SA_PARAM_NN_VALID RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category, bool allow_instance);

void rdrDrawListClaimSkinningMatSet(RdrDrawList* draw_list, DynSkinningMatSet* skinning_mat_set);

SA_RET_OP_VALID RdrDrawableSkinnedModel *rdrDrawListAllocSkinnedModel(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, ModelLOD *debug_model_backpointer, int material_count, int num_vs_constants, int num_bones, SkinningMat4* external_skinning_mat_array);
void rdrDrawListAddSkinnedModel(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableSkinnedModel *draw, SA_PARAM_NN_VALID RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category);

SA_RET_OP_VALID RdrDrawablePrimitive *rdrDrawListAllocPrimitive(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, bool sprite_state_alloc);
void rdrDrawListAddPrimitive(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawablePrimitive *draw, RdrSortType type, RdrObjectCategory category);

SA_RET_OP_VALID RdrDrawableMeshPrimitive *rdrDrawListAllocMeshPrimitive(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, U32 num_verts, U32 num_strips, bool bMallocVerts);
SA_RET_NN_VALID RdrDrawableMeshPrimitiveStrip* rdrDrawListAllocMeshPrimitiveStrip(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableMeshPrimitive* draw, U32 strip_index, U32 num_indices);
void rdrDrawListAddMeshPrimitive(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableMeshPrimitive *draw, const Mat4 world_matrix, RdrSortType type, RdrObjectCategory category);

SA_RET_OP_VALID RdrDrawableFastParticles *rdrDrawListAllocFastParticles(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, int vert_count, bool streak, int num_bones);
void rdrDrawListAddFastParticles(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableFastParticles *draw, SA_PARAM_NN_VALID RdrAddFastParticleParams *params, RdrSortType type, RdrObjectCategory category);

SA_RET_OP_VALID RdrDrawableTriStrip *rdrDrawListAllocTriStrip(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, int vert_count);
void rdrDrawListAddTriStrip(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableTriStrip *draw, SA_PARAM_NN_VALID RdrAddInstanceParams *instance_params, const Vec3 world_mid, RdrSortType type, RdrObjectCategory category);

SA_RET_OP_VALID RdrDrawableClothMesh *rdrDrawListAllocClothMesh(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, SA_PARAM_OP_VALID ModelLOD *debug_model_backpointer, int vert_count, int strip_count, int material_count, int num_vs_constants);
RdrDrawableIndexedTriStrip* rdrDrawListAllocClothMeshStripIndices(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableClothMesh* draw, U32 strip_index, U32 num_indices);
void rdrDrawListAddClothMesh(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableClothMesh *draw, SA_PARAM_NN_VALID RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category);

SA_RET_OP_VALID RdrDrawableCylinderTrail *rdrDrawListAllocCylinderTrail(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type, int vert_count, int index_count, int num_vertex_shader_constants);
void rdrDrawListAddCylinderTrail(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableCylinderTrail *draw, RdrAddInstanceParams* instance_params, const Vec3 world_mid, RdrSortType type, RdrObjectCategory category);

SA_RET_OP_VALID RdrDrawableParticle *rdrDrawListAllocParticle(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrGeometryType draw_type);
void rdrDrawListAddParticleLink(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableParticle *draw);
void rdrDrawListAddParticle(SA_PARAM_NN_VALID RdrDrawList *draw_list, SA_PARAM_NN_VALID RdrDrawableParticle *draw, SA_PARAM_NN_VALID RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category, F32 zdist);

typedef struct RdrDrawListDebugState
{
	int overrideNoAlphaKill;
	int overrideNoAlphaKillValue;
	int overridePerMaterialFlags;
	int overrideSortBucket;
	RdrSortBucketType overrideSortBucketValue;
} RdrDrawListDebugState;
extern RdrDrawListDebugState draw_list_debug_state;

void rdrDrawListSelectByShaderGraph(ShaderGraphRenderInfo *graph_render_info);
void rdrDrawListSelectByGeoHandle(GeoHandle geo_handle);

void rdrDrawListCheckLinearAllocator(RdrDrawList *draw_list);

F32 rdrDrawListCalcZDist(SA_PARAM_NN_VALID RdrDrawList *draw_list, const Vec3 world_mid);

static __forceinline int rdrIsVisualOrZpreShaderMode(RdrShaderMode shader_mode)
{
	return shader_mode >= RDRSHDM_ZPREPASS && shader_mode <= RDRSHDM_VISUAL_HDR;
}

static __forceinline int rdrIsVisualShaderMode(RdrShaderMode shader_mode)
{
	return shader_mode >= RDRSHDM_VISUAL && shader_mode <= RDRSHDM_VISUAL_HDR;
}

static __forceinline int rdrIsShadowShaderMode(RdrShaderMode shader_mode)
{
	return shader_mode == RDRSHDM_SHADOW;
}

void rdrSetHDRSlopeScaleDepthBias(F32 bias);
void rdrSetHDRDepthBias(F32 bias);
F32 rdrGetHDRSlopeScaleDepthBias();
F32 rdrGetHDRDepthBias();

const char * rdrLightGetTypeString(const RdrLight * light);
void rdrLightDumpToStr( const RdrLight * rdr_light, int light_num, char * string, int max_len );

// HACK FIXME - This is to let screen-space particles
// know if it's okay to draw yet because the initialization
// order keeps getting messed up. -Cliff
bool rdrDrawListHasData(const RdrDrawList *list);

void rdrSetDebugBuffer(RdrDevice *device, Vec4 parms);

#endif //_RDRDRAWLIST_H_
