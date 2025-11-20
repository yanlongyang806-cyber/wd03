#ifndef _RDRDRAWABLE_H_
#define _RDRDRAWABLE_H_
GCC_SYSTEM

#include "stdtypes.h"
#include "RdrEnums.h"

typedef int GeoHandle;
typedef U64 TexHandle;
typedef int ShaderHandle;
typedef struct RdrDevice RdrDevice;
typedef struct RdrDrawListData RdrDrawListData;
typedef struct RdrLight RdrLight;
typedef struct RdrLightData RdrLightData;
typedef struct RdrSortNode RdrSortNode;
typedef struct RdrSpriteState RdrSpriteState;
typedef struct Frustum Frustum;
typedef struct ShaderGraphRenderInfo ShaderGraphRenderInfo;

#define MAX_NUM_SHADER_LIGHTS 				5
#define MAX_NUM_OBJECT_LIGHTS 				MAX_NUM_SHADER_LIGHTS
#define SHADER_PER_LIGHT_CONST_COUNT		7
#define SHADER_PER_LIGHT_CONST_COUNT_SIMPLE	6

// call this for actual clamping
int rdrMaxSupportedObjectLights( void );

// maximum shadowmap passes for a single light source
#define MAX_LIGHT_SHADOW_PASSES 6

// two texture slots are reserved, so only 14 are available
#define RDR_MAX_TEXTURES 14

// can't be more than 32 because we are packing 2 bits per frustum into a U64 (search for FRUSTUM_CLIP_TYPE)
// 3 point lights at 7 passes apiece = 21 + visual + hdr + zprepass + low res alpha + targeting = 26
#define MAX_RENDER_PASSES 26

// would need extra shader code if this number changes
#define MAX_CLOUD_LAYERS 2


typedef struct RdrSortNodeList
{
	RdrSortNode **sort_nodes;
	int total_tri_count;
} RdrSortNodeList;

typedef struct RdrDrawListPassData
{
	// available in main thread only:
	Frustum			*frustum;
	U32				frustum_set_bit;
	U32				frustum_clear_bits;
	U64				frustum_partial_clip_flag; // FRUSTUM_CLIP_TYPE
	U64				frustum_trivial_accept_flag; // FRUSTUM_CLIP_TYPE
	U64				frustum_flag_test_mask; // FRUSTUM_CLIP_TYPE
	U64				frustum_flag_clear_mask; // FRUSTUM_CLIP_TYPE - different set of bits because it may clear other dependent passes


	RdrShaderMode	shader_mode;
	RdrSortNodeList sort_node_buckets[RSBT_COUNT];
	Mat4			viewmat;
	bool			depth_only;
	bool			disable_opaque_depth_writes;
	bool			is_underexposed_pass;

	bool			owned_by_thread;

	// for shadow passes
	int				shadowmap_idx;
	RdrLight		*shadow_light;
	RdrLightData	*shadow_light_data;
	F32				depth_bias;
	F32				slope_scale_depth_bias;
} RdrDrawListPassData;


__forceinline static RdrMaterialRenderMode rdrGetMaterialRenderMode(RdrMaterialShader shader_num)
{
	return (RdrMaterialRenderMode)(shader_num.shaderMask & MATERIAL_SHADER_RENDERMODE);
}

__forceinline static RdrLightType rdrGetSimpleLightType(RdrLightType type)
{
	return (RdrLightType)(type & RDRLIGHT_TYPE_MASK);
}

__forceinline static bool rdrIsShadowedLightType(RdrLightType type)
{
	return !!(type & RDRLIGHT_SHADOWED);
}

__forceinline static RdrLightType rdrGetLightType(RdrLightShaderMask shader_num, int light_num)
{
#ifdef _FULLDEBUG
	assert(light_num >= 0 && light_num < MAX_NUM_SHADER_LIGHTS);
#endif
	switch (light_num)
	{
		xcase 0:
			return (RdrLightType)((shader_num >> MATERIAL_SHADER_LIGHT0_OFFSET) & RDRLIGHT_MASK);
		xcase 1:
			return (RdrLightType)((shader_num >> MATERIAL_SHADER_LIGHT1_OFFSET) & RDRLIGHT_MASK);
		xcase 2:
			return (RdrLightType)((shader_num >> MATERIAL_SHADER_LIGHT2_OFFSET) & RDRLIGHT_MASK);
		xcase 3:
			return (RdrLightType)((shader_num >> MATERIAL_SHADER_LIGHT3_OFFSET) & RDRLIGHT_MASK);
		xcase 4:
			return (RdrLightType)((shader_num >> MATERIAL_SHADER_LIGHT4_OFFSET) & RDRLIGHT_MASK);
	}

#ifdef _FULLDEBUG
	assert(0);
#endif

	return RDRLIGHT_NONE;
}

// Is a single directional light, with or without shadow
__forceinline static bool rdrIsSingleDirectionalLight(RdrLightShaderMask light_shader_num)
{
	return (light_shader_num | (RDRLIGHT_SHADOWED<<MATERIAL_SHADER_LIGHT0_OFFSET)) == ((RDRLIGHT_DIRECTIONAL|RDRLIGHT_SHADOWED)<<MATERIAL_SHADER_LIGHT0_OFFSET);
}


__forceinline static RdrLightShaderMask rdrGetMaterialShaderType(RdrLightType light_type, int light_num)
{
#ifdef _FULLDEBUG
	assert(light_num >= 0 && light_num < MAX_NUM_SHADER_LIGHTS);
#endif
	switch (light_num)
	{
		xcase 0:
			return (light_type & RDRLIGHT_MASK) << MATERIAL_SHADER_LIGHT0_OFFSET;
		xcase 1:
			return (light_type & RDRLIGHT_MASK) << MATERIAL_SHADER_LIGHT1_OFFSET;
		xcase 2:
			return (light_type & RDRLIGHT_MASK) << MATERIAL_SHADER_LIGHT2_OFFSET;
		xcase 3:
			return (light_type & RDRLIGHT_MASK) << MATERIAL_SHADER_LIGHT3_OFFSET;
		xcase 4:
			return (light_type & RDRLIGHT_MASK) << MATERIAL_SHADER_LIGHT4_OFFSET;
	}

#ifdef _FULLDEBUG
	assert(0);
#endif

	return 0;
}

//////////////////////////////////////////////////////////////////////////

typedef struct RdrPerDrawableConstantMapping {
	U32 data_type;
	U32 constant_index; // Or texture index
	U32 constant_subindex;
} RdrPerDrawableConstantMapping;

typedef struct RdrNonPixelMaterial
{
	TexHandle	*textures;
	Vec4		*constants;

	U32			const_count:8;
	U32			tex_count:6;
	// Below is a union to contain flags that may be specific to tessellation shaders or vertex shaders depending on future use.
	union {
		RdrTessellationFlags	tessellation_flags;
	};
}RdrNonPixelMaterial;

typedef struct RdrMaterial
{
	TexHandle		*textures;
	Vec4			*constants;
	RdrNonPixelMaterial	*tessellation_material;
	RdrPerDrawableConstantMapping *drawable_constants;

	U32				const_count:8;
	U32				drawable_const_count:8;
	S32				instance_param_index:8;
	U32				tex_count:6;
	U32				need_alpha_sort:1;
	U32				need_texture_screen_color:1;
	U32				need_texture_screen_depth:1;
	U32				need_texture_screen_color_blurred:1;
	U32				has_transparency:1;
	U32				alpha_pass_only:1; // Has no opaque or depth-only shader available
	U32				no_hdr:1;
	U32				no_tint_for_hdr:1;
	U32				no_normalmap:1; // Pixel shader does no normal mapping, can use simpler vertex shader
	U32				surface_texhandle_fixup:1; // draw list must map texture handles using surface fixup table
	Vec3			lighting_contribution;
	RdrMaterialFlags flags;
} RdrMaterial;

//////////////////////////////////////////////////////////////////////////

typedef struct RdrClearParams
{
	RdrClearBits bits;
	Vec4 clear_color;
	F32 clear_depth;
} RdrClearParams;


//////////////////////////////////////////////////////////////////////////
// vertex types

typedef struct RdrPrimitiveVertex
{
	Vec3 pos;
	Vec4 color;
} RdrPrimitiveVertex;

typedef struct RdrPrimitiveTexturedVertex
{
	union {
		RdrPrimitiveVertex vertex;
		struct {
			Vec3 pos;
			Vec4 color;
		};
	};
	Vec2 texcoord;
} RdrPrimitiveTexturedVertex;

typedef struct RdrPostprocessVertex
{
    Vec3 pos;
} RdrPostprocessVertex;

typedef struct RdrPostprocessExVertex
{
    Vec3 pos;
    Vec4 texcoord;
} RdrPostprocessExVertex;

typedef struct RdrParticleVertex
{
	Vec3	point;
	Vec3	normal;
	Vec4	color;
	Vec2	texcoord;
	Vec3	tangent;
	Vec3	binormal;
	float	tightenup;
} RdrParticleVertex;

typedef struct RdrClothMeshVertex
{
	Vec3	point;
	Vec3	normal;
	Vec2	texcoord;
	Vec3	tangent;
	Vec3	binormal;
} RdrClothMeshVertex;

typedef struct RdrCylinderTrailVertex
{
	//Vec3	point;
	//Vec2	texcoord;
	//Vec4	color;
	S16		boneidx[2];
	F32		angle;
} RdrCylinderTrailVertex;

typedef struct RdrFastParticleVertex
{
	Vec3 point;
	S16 corner_nodeidx[2]; // first is corner, 2nd is node_index
	F32 time;
	F32 seed;
	F32 alpha;
} RdrFastParticleVertex;

typedef struct RdrFastParticleStreakVertex
{
	Vec3 point;
	Vec3 streak_dir;
	S16 corner_nodeidx[2]; // first is corner, 2nd is node_index
	F32 time;
	F32 seed;
	F32 alpha;
} RdrFastParticleStreakVertex;

//////////////////////////////////////////////////////////////////////////
// drawables

typedef struct RdrAmbientLight
{
	Vec3				ambient[RLCT_COUNT];
	Vec3				sky_light_color_front[RLCT_COUNT];
	Vec3				sky_light_color_back[RLCT_COUNT];
	Vec3				sky_light_color_side[RLCT_COUNT];
} RdrAmbientLight;

typedef struct RdrVertexLight
{
	GeoHandle			geo_handle_vertex_light;
	F32					vlight_multiplier;
	F32					vlight_offset;
	bool				ignoreVertColor;
} RdrVertexLight;

typedef struct RdrShadowmap
{
	Mat44				camera_to_shadowmap;		// camera space -> shadowmap space
	F32					near_fade;
	F32					far_fade;
} RdrShadowmap;

typedef struct RdrLightColors
{
	Vec3				ambient;
	Vec3				diffuse;
	Vec3				specular;
	Vec3				secondary_diffuse;
	Vec3				shadow_color;
	F32					min_shadow_val;
	F32					max_shadow_val;
} RdrLightColors;

typedef struct RdrLight
{
	RdrLightType		light_type;
	RdrLightType		rdr_orig_light_type;

	RdrLightColors		light_colors[RLCT_COUNT];

	Mat4				world_mat;

	F32					fade_out;

	struct
	{
		F32					inner_radius, outer_radius;
		F32					inner_cone_angle, outer_cone_angle;
		Vec2				angular_falloff;
	} point_spot_params;

	struct
	{
		Vec4				angular_falloff;
		TexHandle			projected_tex;
	} projector_params;

	Vec3				shadow_mask;

	TexHandle			shadowmap_texture;
	IVec2				shadowmap_texture_size;
	RdrShadowmap		shadowmap[MAX_LIGHT_SHADOW_PASSES];

	TexHandle			cloud_texture;

	struct 
	{
		Mat44				camera_to_cloud_texture;
		F32					multiplier;
	} cloud_layers[MAX_CLOUD_LAYERS];

} RdrLight;

typedef struct RdrLightShaderData
{
	U16				const_count;
	U16				tex_count;
	Vec4			*constants;
	TexHandle		*tex_handles;
} RdrLightShaderData;

typedef struct RdrLightVertexShaderData
{
	Vec4			constants[RDR_VERTEX_LIGHT_CONSTANT_COUNT];
} RdrLightVertexShaderData;

typedef struct RdrLightData
{
	RdrLightType light_type;

	S16 direction_const_idx;
	S16 position_const_idx;
	S16 color_const_idx;

	RdrLightShaderData normal[RLCT_COUNT];
	RdrLightShaderData simple[RLCT_COUNT];
	RdrLightShaderData shadow_test;

	RdrLightVertexShaderData vertex_lighting[RLCT_COUNT];
	RdrLightVertexShaderData single_dir_light[RLCT_COUNT];

} RdrLightData;

typedef struct RdrLightSuperData
{
	// This is ridiculous.  This array is almost always almost empty.  elements 0,5,6,7, and 8 can never be filled, currently  [RMARR - 5/14/12]
	RdrLightData *light_data[RDRLIGHT_TYPE_MAX_WITH_SHADOWS];  // These will be filled in only if used by something
} RdrLightSuperData;

typedef struct RdrInstanceBuffer
{
	// world matrix in row major order
	Vec4 world_matrix_x;
	Vec4 world_matrix_y;
	Vec4 world_matrix_z;
	Vec4 tint_color;
	Vec4 instance_param;
} RdrInstanceBuffer;

typedef struct RdrInstance
{
	Mat4 	world_matrix;
	Vec4 	color;
	F32		morph;

	Vec3	world_mid;
	U32		instance_uid:24;
	U32		camera_facing:1;
	U32		axis_camera_facing:1;
} RdrInstance;

#ifndef DECLARED_RdrInstancePerDrawableData
#define DECLARED_RdrInstancePerDrawableData
typedef struct RdrInstancePerDrawableData
{
	Vec4 instance_param; 
} RdrInstancePerDrawableData;
#endif

typedef struct RdrInstanceLinkList
{
	RdrInstance *instance;
	int count; // count of instances in this linked list, including this one but excluding any before it
	U32 hdr_contribution;
	F32 zdist; // used in render thread for sorting, transient
	RdrInstancePerDrawableData per_drawable_data; // This structure is the only per-instance, per-subobject structure, so I'm putting this here
	struct RdrInstanceLinkList *next;
} RdrInstanceLinkList;

typedef struct RdrSortNodeLinkList
{
	RdrSortNode				*sort_node;
	RdrSortBucketType		sort_bucket_type;
	int						pass_idx;
	struct RdrSortNodeLinkList *next;
} RdrSortNodeLinkList;

typedef struct RdrSubobject
{
	RdrMaterial				*material;
	ShaderGraphRenderInfo	*graph_render_info;
	U32						tri_count;
	bool					inited;

	RdrSortNodeLinkList		*opaque_sort_nodes;
} RdrSubobject;

typedef struct RdrLightList
{
	struct
	{
		U16					lights[MAX_NUM_OBJECT_LIGHTS];
		U16					ambient_light;
		U8					ambient_offset[3];
		U8					ambient_multiplier[3];
		U8					light_color_type:RdrLightColorType_NumBits; // if we make the type RdrLightColorType here, it will take 4 bytes by itself
		U8					use_vertex_only_lighting:1; // currently global, but could be set per-object
		U8					use_single_dirlight:1; // Not needed to be part of comparator, just here to bitpack better
	} comparator;

	RdrLightData		*lights[MAX_NUM_OBJECT_LIGHTS];
	RdrAmbientLight		*ambient_light;
	// vertex lights can NEVER be shared.  This is hugely disruptive to light list merging.  We should consider if we can move this out of the light lists
	RdrVertexLight		*vertex_light;
	// these are almost always not set.  We should consider whether we can put them in a sub-struct, thus reducing them to a U16 in the comparator?
	Vec3				ambient_offset;
	Vec3				ambient_multiplier;

	// these values do not need to be part of the comparator because they are directly dependent on the values in the comparator
	Vec2				light_contribution; // ambient+diffuse, specular
	RdrMaterialShader	light_shader_num;
} RdrLightList;

typedef struct RdrDrawable
{
	RdrGeometryType		draw_type;
} RdrDrawable;

// used for: RTYPE_PRIMITIVE
typedef struct RdrDrawablePrimitive
{
	RdrDrawable			base_drawable; // must be first!

	enum { RP_LINE, RP_TRI, RP_QUAD } type;

	RdrPrimitiveTexturedVertex	vertices[4];
	RdrSpriteState*		sprite_state;
	TexHandle			tex_handle; // if 0, then no texturing is done
	F32					linewidth; // if not filled
	F32					time_to_draw; // how long this primitive will draw
	U32					screen_width_2d, screen_height_2d; // if 2d
	U32					filled : 1;
	U32					in_3d : 1;
	U32					no_ztest : 1;
	U32					tonemapped : 1;
	U32					requeued : 1; // do not free this primitive when clearing the queue
	U32					internal_no_change_pixel_shader:1;
	U32					has_tex_coords : 1;
} RdrDrawablePrimitive;

typedef struct RdrDrawableMeshPrimitiveStrip
{
	enum { RP_TRILIST, RP_TRISTRIP } type;
	U32					num_indices;
	U16*				indices;
} RdrDrawableMeshPrimitiveStrip;

typedef struct RdrDrawableMeshPrimitive
{
	RdrDrawable			base_drawable; // must be first!

	U32					num_strips; 
	RdrDrawableMeshPrimitiveStrip* strips;
	U32					num_verts;
	RdrPrimitiveVertex*	verts;
	F32					linewidth; // if not filled
	U32					filled : 1;
	U32					no_ztest : 1;
	U32					no_zwrite : 1;
	U32					tonemapped : 1;
	U32					internal_no_change_pixel_shader:1;
	U32					owns_verts:1;
} RdrDrawableMeshPrimitive;

// used for: RTYPE_MODEL, RTYPE_TERRAIN, RTYPE_STARFIELD, RTYPE_TREE_BILLBOARD
typedef struct RdrDrawableGeo
{
	RdrDrawable			base_drawable; // must be first!

	GeoHandle			geo_handle_primary;
	GeoHandle			geo_handle_terrainlight; // regrettable!

	U8					num_vertex_textures;
	U8					num_vertex_shader_constants;
	U8					subobject_count;

	Vec4				*vertex_shader_constants;
	TexHandle			*vertex_textures;

	// data cached between adding instances, not needed in the render thread
	RdrSortNodeLinkList	*depth_sort_nodes;

	struct ModelLOD		*debug_model_backpointer;

} RdrDrawableGeo;

// used for RTYPE_SKINNED_MODEL,RTYPE_CURVED_MODEL, RTYPE_TREE_BRANCH,RTYPE_TREE_LEAF,RTYPE_TREE_LEAFMESH
typedef struct RdrDrawableSkinnedModel
{
	RdrDrawableGeo		base_geo_drawable; // must be first!
	SkinningMat4		*skinning_mat_array; // if there are indices present, this is a refcounted array that the indices index into. If not, it's our allocation and we are responsible, and it's already in the correct order when queued.
	U8					*skinning_mat_indices;
	U8					num_bones;
} RdrDrawableSkinnedModel;

typedef struct RdrDrawableParticleLinkList
{
	RdrParticleVertex	verts[4];
	F32					zdist;
	int					particle_count; // = 1 + next->count
	struct RdrDrawableParticleLinkList *next;
} RdrDrawableParticleLinkList;

// used for: RTYPE_PARTICLE
typedef struct RdrDrawableParticle
{
	RdrDrawable			base_drawable; // must be first!
	RdrMaterialFlags	blend_flags;
	TexHandle			tex_handle0;
	TexHandle			tex_handle1;
    U32					is_screen_space : 1;

	RdrDrawableParticleLinkList *particles;
} RdrDrawableParticle;

// used for: RTYPE_TRISTRIP
typedef struct RdrDrawableTriStrip
{
	RdrDrawable			base_drawable; // must be first!
	TexHandle			tex_handle0;
	TexHandle			tex_handle1;
	RdrMaterialFlags	add_material_flags;
	U32					vert_count;
	U32					is_screen_space : 1;
	RdrParticleVertex	*verts;
} RdrDrawableTriStrip;

typedef struct RdrDrawableIndexedTriStrip
{
	U16*				indices;
	U16					num_indices;
	U16					min_index;
	U16					max_index;
} RdrDrawableIndexedTriStrip;

// used for: RTYPE_CLOTHMESH
typedef struct RdrDrawableClothMesh
{
	RdrDrawableGeo					base_geo_drawable; // must be first!
	TexHandle						tex_handle;
	RdrClothMeshVertex*				verts;
	U32								vert_count;
	RdrDrawableIndexedTriStrip*		tri_strips;
	U32								strip_count;
	Vec3							color2;
} RdrDrawableClothMesh;

// used for: RTYPE_CYLINDERTRAIL
typedef struct RdrDrawableCylinderTrail
{
	RdrDrawable			base_drawable; // must be first!
	TexHandle			tex_handle0;
	TexHandle			tex_handle1;
	RdrMaterialFlags	add_material_flags;

	U16					index_count;
	U16					vert_count;
	U32					num_constants;

	RdrCylinderTrailVertex *verts;
	U16					*idxs;
	Vec4				*vertex_shader_constants;
} RdrDrawableCylinderTrail;

// used for: RTYPE_FASTPARTICLES
#define RDR_NUM_FAST_PARTICLE_CONSTANTS 30
typedef struct RdrDrawableFastParticles
{
	RdrDrawable			base_drawable; // must be first!
	TexHandle			tex_handle;
	TexHandle			noise_tex;

	Vec4				time_info;
	Vec4				scale_info;
	Vec4				hsv_info;
	Vec4				modulate_color;
	Vec4				constants[RDR_NUM_FAST_PARTICLE_CONSTANTS];
	Vec4				special_params;

	RdrMaterialFlags	blendmode;

	U32					particle_count;
	RdrFastParticleVertex *verts;
	RdrFastParticleStreakVertex *streakverts;

	SkinningMat4		*bone_infos;
	U32					num_bones : 8;

	U32					link_scale : 1;
	U32					streak : 1;
	U32					rgb_blend : 1;
	U32					no_tonemap : 1;
	U32					soft_particles : 1;
    U32					is_screen_space : 1;
	U32					cpu_fast_particles : 1;
	U32					animated_texture : 1;
} RdrDrawableFastParticles;


#define ZBUCKET_BITS 14
#define ZBUCKET_MAX ((1<<ZBUCKET_BITS) - 1)

#define HDR_CONTRIBUTION_BITS 10
#define SORT_NODE_TRI_COUNT_BITS 22
#define MAX_HDR_CONTRIBUTION ((1 << HDR_CONTRIBUTION_BITS) - 1)
#define MAX_SORT_NODE_TRI_COUNT (1<<SORT_NODE_TRI_COUNT_BITS)

#define SUBOBJECT_COUNT_BITS 5
#define MAX_SUBOBJECT_COUNT (1<<SUBOBJECT_COUNT_BITS)

typedef struct RdrSortNode
{
	// this data is cached here for sorting
	RdrGeometryType		draw_type;
	GeoHandle			geo_handle_primary;
	void				*vtxbuf;
	RdrDrawable			*drawable;
	RdrMaterial			*material;
	RdrNonPixelMaterial	*domain_material;
	RdrMaterialShader	uberlight_shader_num;
	RdrMaterialFlags	add_material_flags;

	ShaderHandle		draw_shader_handle[RDRSHDM_COUNT];
	RdrLightList		*lights;
	RdrInstanceLinkList	*instances;

	U32					zbucket:ZBUCKET_BITS;
	U32					subobject_idx:SUBOBJECT_COUNT_BITS;
	U32					subobject_count:SUBOBJECT_COUNT_BITS;
	U32					has_transparency:1;
	U32					uses_shadowbuffer:1;
	U32					force_no_shadow_receive:1;
	U32					do_instancing:1;
	U32					disable_instance_sorting:1;
	U32					uses_far_depth_range:1;
	U32					camera_centered:1;
	U32					debug_me:1;
	U32					skyDepth:1;
	// 0 bits free in this bitfield

	U32					hdr_contribution:HDR_CONTRIBUTION_BITS;
	U32					tri_count:SORT_NODE_TRI_COUNT_BITS;
	// 0 bits free in this bitfield

	U32					stencil_value:8;
	U32					category:4; // RdrObjectCategory
	U32					has_wind:1;
	U32					has_trunk_wind:1;
	U32					ignore_vertex_colors:1;
	U32					two_bone_skinning:1;
	// 16 bits free in this bitfield

	Vec4				wind_params;

} RdrSortNode;

typedef struct RdrAlphaSortNode
{
	RdrSortNode			base_sort_node; // must be first!
	U8					alpha;
	F32					zdist;
} RdrAlphaSortNode;

//////////////////////////////////////////////////////////////////////////
// sprites

typedef struct RdrSpriteVertex
{
	Vec2	point;
	Color	color;
	Vec4	texcoords;
} RdrSpriteVertex;

typedef struct RdrQuadVertex
{
	Vec2	point;
#if _PS3
	Vec4	color;
	Vec4	texcoords;
#else
	Vec4	texcoords;
	Vec4	color;
#endif
} RdrQuadVertex;

typedef struct RdrSpriteState
{
	TexHandle tex_handle1;
	U8 ignore_depth_test : 1;

	union 
	{
		struct 
		{
			TexHandle tex_handle2;
			F32 sprite_effect_weight;
		};
		struct 
		{
			struct 
			{
				Vec2 offset;
				int rgbaColorMain;
				int rgbaColorOutline;
				Vec3 densityRange;
			} df_layer_settings[2];
			struct
			{
				int rgbaTopColor;
				int rgbaBottomColor;
				Vec2  startStopPts;
			} df_grad_settings;
		};
	};

	

	union
	{
		struct 
		{
			U16 scissor_x, scissor_y, scissor_width, scissor_height;
		};
		U64 scissor_values_for_test;
	};

	union
	{
		struct 
		{
			U8 sprite_effect;// : RdrSpriteEffect_NumBits; // What effect/shader to run
			U8 use_scissor : 1;
			U8 additive: 1;
			U8 is_triangle	: 1;
		};
		U16 bits_for_test;
	};
	
} RdrSpriteState;


typedef struct RdrSpritesPkg
{
	int				 screen_width, screen_height;
	RdrSpriteState  *states;
	RdrSpriteVertex *vertices;
	U16				*indices;			
	U32				*indices32;
	U32				 sprite_count;

	//Dont mess with these values
	U32 state_array_size;
	union
	{
		struct
		{	
			U32 should_free_contiguous_block : 1; //assumes its all in one block as [RdrSpritesPkg][RdrSpriteState][RdrSpriteVertex]...etc
			U32 should_free_states : 1;
			U32 should_free_vertices : 1;
			U32 should_free_indices : 1;
			U32 should_free_pkg_on_send : 1;
			U32 arrays_are_memref : 1; //if the states, vertices and indices are actually memref pointers
		};
		U32 alloc_flags_for_test;
	};
} RdrSpritesPkg;


__forceinline static int spriteScissorStatesEqual(RdrSpriteState *state1, RdrSpriteState *state2)
{
	if (state1->use_scissor)
	{
		if (state1->scissor_values_for_test != state2->scissor_values_for_test)
			return 0;
	}

	return 1;
}

__forceinline static int spriteStatesEqual(RdrSpriteState *state1, RdrSpriteState *state2)
{
	RdrSpriteEffect effect;
	if (state1->bits_for_test != state2->bits_for_test)
		return 0;
	if (state1->tex_handle1 != state2->tex_handle1)
		return 0;

	effect = state1->sprite_effect;
	//tex_handle2 is only valid if we arent a font sprite
	if ((effect < RdrSpriteEffect_DistField1Layer || effect > RdrSpriteEffect_DistField2LayerGradient) && state1->tex_handle2 != state2->tex_handle2)
		return 0;
	

	if (effect)
	{
		if (effect == RdrSpriteEffect_DistField1Layer || effect == RdrSpriteEffect_DistField1LayerGradient)
		{
			if (memcmp(state1->df_layer_settings, state2->df_layer_settings, sizeof(state1->df_layer_settings[0])))
				return 0;
			if (effect == RdrSpriteEffect_DistField1LayerGradient && memcmp(&state1->df_grad_settings, &state2->df_grad_settings, sizeof(state1->df_grad_settings)))
				return 0;
		}
		else if (effect == RdrSpriteEffect_DistField2Layer || effect == RdrSpriteEffect_DistField2LayerGradient)
		{
			if (memcmp(state1->df_layer_settings, state2->df_layer_settings, sizeof(state1->df_layer_settings)))
				return 0;
			if (effect == RdrSpriteEffect_DistField2LayerGradient && memcmp(&state1->df_grad_settings, &state2->df_grad_settings, sizeof(state1->df_grad_settings)))
				return 0;
		}
		else
		{
			if (state1->sprite_effect_weight != state2->sprite_effect_weight)
				return 0;
		}
	}

	return 1;
}


//////////////////////////////////////////////////////////////////////////
// quad

typedef struct RdrQuadDrawable {
	RdrQuadVertex vertices[4];
	ShaderHandle shader_handle;
	RdrMaterial material;
	unsigned alpha_blend : 1;
	unsigned use_normal_vertex_shader : 1;
} RdrQuadDrawable;


//////////////////////////////////////////////////////////////////////////
// postprocessing

typedef struct RdrOcclusionQueryResult {
	RdrDevice			*device;
	bool				data_ready;
	bool				failed;
	U32					frame_ready;
	U32					pixel_count;
	U32					max_pixel_count;
	U32					draw_calls;
	F32					query_user_data;
} RdrOcclusionQueryResult;

typedef struct RdrScreenPostProcess {
	ShaderHandle		shader_handle;
	RdrMaterial 		material;
	int					tex_width, tex_height;
	F32					tex_offset_x, tex_offset_y;
	Vec3				ambient;
	RdrMaterialShader	uberlight_shader_num;
	RdrLightData		*lights[MAX_NUM_OBJECT_LIGHTS];
    Vec4                texcoords[4]; //< in the order: BR, BL, TL, TR
	RdrPPBlendType		blend_type;
	RdrPPDepthTestMode	depth_test_mode;
	U32					write_depth : 1;
	U32					fog : 1;
    U32                 use_normal_vertex_shader : 1;
    U32                 use_texcoords : 1;
	U32					shadow_buffer_render : 1;
	U32					no_offset : 1;
	U32					rdr_internal : 1; // This is an internal call from the renderer, skip counting the 2 triangles
	U32					viewport_independent_textures : 1;
	U32					exact_quad_coverage : 1;
	U32					add_half_pixel : 1;
	U32					stereoscopic : 1;
	U32					draw_count; // used for measuring luminance
	F32					const_increment; // used for measuring luminance
	U32					measurement_mode; // used for measuring luminance, or any scalar from the GPU
	RdrOcclusionQueryResult *occlusion_query;
	Vec2				dest_quad[2];
} RdrScreenPostProcess;

typedef struct RdrShapePostProcess {
	Mat4				world_matrix;
	GeoHandle			geometry;
	ShaderHandle		shader_handle;
	RdrMaterial 		material;
	int					tex_width, tex_height;
	F32					tex_offset_x, tex_offset_y;
	Vec3				ambient;
	RdrMaterialShader	uberlight_shader_num;
	RdrLightData		*lights[MAX_NUM_OBJECT_LIGHTS];
	RdrPPBlendType		blend_type;
	RdrPPDepthTestMode	depth_test_mode;
	U32					write_depth : 1;
	U32					draw_back_faces : 1;
	U32					shadow_buffer_render : 1;
	U32					no_offset : 1;
	RdrOcclusionQueryResult *occlusion_query;
} RdrShapePostProcess;


AUTO_STRUCT AST_IGNORE("V1") AST_ENDTOK(EndLightCombo);
typedef struct PreloadedLightCombo
{
	RdrLightType light_type[5]; AST(FLAGS INDEX(0, Light0) INDEX(1, Light1) INDEX(2, Light2) INDEX(3, Light3) INDEX(4, Light4))
	RdrLightShaderMask light_mask; AST(NO_TEXT_SAVE INT)
} PreloadedLightCombo;

AUTO_STRUCT;
typedef struct PreloadedLightCombosList
{
	PreloadedLightCombo **combos; AST(NAME(LightCombo))
} PreloadedLightCombosList;
extern ParseTable parse_PreloadedLightCombosList[];
#define TYPE_parse_PreloadedLightCombosList PreloadedLightCombosList

extern PreloadedLightCombo **preloaded_light_combos;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#endif //_RDRDRAWABLE_H_
