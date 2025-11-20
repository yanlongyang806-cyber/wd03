#ifndef RDRENUMS_H
#define RDRENUMS_H
GCC_SYSTEM

#define RDR_VERTEX_LIGHT_CONSTANT_COUNT 4

typedef enum RdrSurfaceBuffer
{
	SBUF_0 = 0,
	SBUF_1 = 1,
	SBUF_2 = 2,
	SBUF_3 = 3,

	SBUF_DEPTH = 4,

	SBUF_MAXMRT = 4,

	SBUF_MAX = 5,

} RdrSurfaceBuffer;

typedef enum RdrSurfaceBufferMaskBits
{
	MASK_SBUF_0 = 1<<SBUF_0,
	MASK_SBUF_1 = 1<<SBUF_1,
	MASK_SBUF_2 = 1<<SBUF_2,
	MASK_SBUF_3 = 1<<SBUF_3,
	MASK_SBUF_DEPTH = 1<<SBUF_DEPTH,
	MASK_SBUF_STENCIL = MASK_SBUF_DEPTH<<1,

	MASK_SBUF_ALL_COLOR = MASK_SBUF_0|MASK_SBUF_1|MASK_SBUF_2|MASK_SBUF_3,
	MASK_SBUF_ALL = MASK_SBUF_ALL_COLOR|MASK_SBUF_DEPTH,

} RdrSurfaceBufferMaskBits;

typedef enum RdrClearBits
{
	CLEAR_STENCIL = MASK_SBUF_DEPTH<<1,
	CLEAR_ALL = CLEAR_STENCIL|MASK_SBUF_ALL,

} RdrClearBits;

typedef enum RdrSurfaceFace
{
	RSF_POSITIVE_X,
	RSF_NEGATIVE_X,
	RSF_POSITIVE_Y,
	RSF_NEGATIVE_Y,
	RSF_POSITIVE_Z,
	RSF_NEGATIVE_Z,
} RdrSurfaceFace;

typedef enum RdrStencilMode
{
	RDRSTENCILMODE_NONE,
	RDRSTENCILMODE_WRITEVALUE,
	RDRSTENCILMODE_MASKEQUAL,
	RDRSTENCILMODE_MASKNOTEQUAL,
	RDRSTENCILMODE_WRITE255,

	RDRSTENCILMODE_MAX
};

// This must match RdrMaterialFlags RMATERIAL_STENCILMODE0, etc.
typedef enum RdrMaterialFlagsCharacteristics
{
	RMATERIAL_STENCILMODESHIFT = 21, // first stencil mode bit index
	RMATERIAL_STENCILMODEBITS = 3, // count of bits for stencil mode
	RMATERIAL_STENCILMODEMASK = (1 << RMATERIAL_STENCILMODEBITS) - 1,
} RdrMaterialFlagsCharacteristics;

typedef enum RdrMaterialFlags
{
	RMATERIAL_ADDITIVE = 1 << 0,
	RMATERIAL_SUBTRACTIVE = 1 << 1,
	RMATERIAL_DOUBLESIDED = 1 << 2,
	RMATERIAL_NOZWRITE = 1 << 3,
	RMATERIAL_NOZTEST = 1 << 4,
	RMATERIAL_NOFOG = 1 << 5,
	RMATERIAL_BACKFACE = 1 << 6,
	RMATERIAL_NOTINT = 1 << 7,
	RMATERIAL_NONDEFERRED = 1 << 8,
	RMATERIAL_DEPTHBIAS = 1 << 9,
	RMATERIAL_FORCEFARDEPTH = 1 << 10,
	RMATERIAL_WORLD_TEX_COORDS_XZ = 1 << 11,
	RMATERIAL_DECAL = 1 << 12,
	RMATERIAL_NOINSTANCE = 1 << 13, // set in code if this material needs per-instance constants
	RMATERIAL_ALPHA_FADE_PLANE = 1 << 14, // only set in code, for splats
	RMATERIAL_NOBLOOM = 1 << 15, // do tone mapping but do not draw into bloom pass
	RMATERIAL_ALPHA_TO_COVERAGE = 1 << 16, // set by code, comes from template flag
	RMATERIAL_WORLD_TEX_COORDS_XY = 1 << 17,
	RMATERIAL_WORLD_TEX_COORDS_YZ = 1 << 18,
	RMATERIAL_SCREEN_TEX_COORDS = 1 << 19,
	RMATERIAL_LOW_RES_ALPHA = 1 << 20, // try to use low res alpha rendering

	// This must match RdrMaterialFlagsCharacteristics.
	RMATERIAL_STENCILMODE0 = 1 << 21, // stencil mode bit 0
	RMATERIAL_STENCILMODE1 = 1 << 22, // stencil mode bit 1
	RMATERIAL_STENCILMODE2 = 1 << 23, // stencil mode bit 2

	RMATERIAL_VS_TEXCOORD_SPLAT = 1 << 24, // only set in code, for splats that generate tex coords in the shader
	RMATERIAL_NOCOLORWRITE = 1 << 25, // writes only to depth
	RMATERIAL_DEPTH_EQUALS = 1 << 26, // use depth test of EQUALS
	RMATERIAL_UNLIT = 1 << 27, // Only used CPU-side, could pass this option other ways if we need to reclaim this bit
	RMATERIAL_ALPHA_NO_DOF = 1 << 28,  // Only used CPU-side, could pass this option other ways if we need to reclaim this bit

	RMATERIAL_TESSELLATE = 1 << 29,
	RMATERIAL_NO_CLUSTER = 1 << 30,
} RdrMaterialFlags;

typedef enum RdrTessellationFlags
{
	TESSELLATE_PN_TRIANGLES = 1 << 0,
	TESSELLATE_HAS_HEIGHTMAP = 1 << 1,
} RdrTessellationFlags;

__forceinline static int rdrMaterialFlagsPackStencilMode(int mode)
{
	return (mode & RMATERIAL_STENCILMODEMASK) << RMATERIAL_STENCILMODESHIFT;
}

__forceinline static int rdrMaterialFlagsUnpackStencilMode(int flags)
{
	return (flags >> RMATERIAL_STENCILMODESHIFT) & RMATERIAL_STENCILMODEMASK;
}

typedef enum RdrGeoUsageBits
{
	RUSE_NONE			= 0,
	RUSE_POSITIONS		= 1 << 0,		// Note: 3 F32s
	RUSE_NORMALS		= 1 << 1,		// Note: 11:11:10 (Xbox) or 16:16:16:16 (PC)
	RUSE_TANGENTS		= 1 << 2,		// Note: 11:11:10 (Xbox) or 16:16:16:16 (PC)
	RUSE_BINORMALS		= 1 << 3,		// Note: 11:11:10 (Xbox) or 16:16:16:16 (PC)
	RUSE_TEXCOORDS		= 1 << 4,		// Note: 2 F16s
	RUSE_TEXCOORD2S		= 1 << 5,		// Note: 2 F16s
	RUSE_BONEWEIGHTS	= 1 << 6,		// Note: 4 U8s
	RUSE_BONEIDS		= 1 << 7,		// Note: 4 U16s
	RUSE_COLORS			= 1 << 8,		// Note: 4 U8s

	RUSE_NUM_COMBINATIONS_NON_FLAGS = 1 << 9,
	
	RUSE_TEXCOORDS_HI_FLAG	= 1 << 9,	// Note: changes TEXCOORDs and TEXCOORDS2S to 2 F32s each
	RUSE_BONEWEIGHTS_HI_FLAG = 1 << 10,	// Note: changes BONEWEIGHTS to 4 F32s

	RUSE_NUM_COMBINATIONS = 1 << 11,
	RUSE_MASK = RUSE_NUM_COMBINATIONS-1,

	// Below here are not part of the "vertex declaration combinations" system.  They are for making a unique signature that can
	// be used as a stash table key.  Primarily for models

	// Each of these requires an extra implied stream which the low-level code is expected to implicitly understand

	// these 2 are mutually exclusive
	RUSE_KEY_MORPH = 1 << 11,
	RUSE_KEY_INSTANCE = 1 << 12,

	RUSE_KEY_VERTEX_LIGHTS = 1 << 13,

	RUSE_KEY_TOTAL_BITS = 14
} RdrGeoUsageBits;

typedef enum RdrGeometryType
{
	// if you change this, change the draw_type_sort_order array in rt_xdrawlist.c as well
	RTYPE_MODEL,
	RTYPE_SKINNED_MODEL,
	RTYPE_CURVED_MODEL,
	RTYPE_TERRAIN,
	RTYPE_PRIMITIVE,
	RTYPE_PRIMITIVE_MESH,
	RTYPE_PARTICLE,
	RTYPE_FASTPARTICLES,
	RTYPE_TRISTRIP,
	RTYPE_CLOTHMESH,
	RTYPE_CYLINDERTRAIL,
	RTYPE_STARFIELD,

	RTYPE_END					//< must be at the end
} RdrGeometryType;

typedef enum RdrDrawableMaterialParam
{
	RDMP_INVMODELVIEWMATRIX, // SDT_INVMODELVIEWMATRIX
	RDMP_VOLUMEFOGMATRIX, // SDT_VOLUMEFOGMATRIX
	RDMP_SUNDIRECTIONVIEWSPACE, // SDT_SUNDIRECTIONVIEWSPACE
	RDMP_SUNCOLOR, // SDT_SUNCOLOR
	RDMP_MODELPOSITIONVIEWSPACE, // SDT_MODELPOSITIONVIEWSPACE
	RDMP_UNIQUEOFFSET, // SDT_UNIQUEOFFSET
} RdrDrawableMaterialParam;

typedef enum RdrFogMode
{
	RFM_NONE,
	RFM_LINEAR,
	RFM_VOLUME,
} RdrFogMode;

typedef enum RdrSortType
{
	// if you change this, change the sort_type_to_bucket_type array in RdrDrawList.c as well
	RST_ZPREPASS,
	RST_OPAQUE_ONEPASS,
	RST_DEFERRED,
	RST_ZPREPASS_NO_OUTLINE,
	RST_NONDEFERRED,
	RST_ALPHA_PREDOF_NEEDGRAB,
	RST_ALPHA_PREDOF,
	RST_ALPHA,
	RST_ALPHA_LOW_RES_ALPHA,
	RST_ALPHA_LOW_RES_ADDITIVE,
	RST_ALPHA_LOW_RES_SUBTRACTIVE,
	RST_ALPHA_LATE,
	RST_ALPHA_POST_GRAB_LATE,
	RST_AUTO,

	RST_MAX,
} RdrSortType;

/// ////////////////////////////////////////////////////////////////////////////
/// The types are listed in the order they get drawn
typedef enum RdrSortBucketType
{
	RSBT_INVALID,

	RSBT_SHADOWMAP, 

	RSBT_ZPREPASS,				// Depth of (outlines and z-pre) e.g. World
	RSBT_OPAQUE_ONEPASS,		// Depth+Color of (outlines and !z-pre) e.g. Characters
	// Decals are always no-zpre, no decal step here
	// Depth grabbed for outlining here
	RSBT_ZPREPASS_NO_OUTLINE,	// Depth of (!outlines and z-pre) e.g. Trees

	// Shadow buffers calculated here

	RSBT_OPAQUE_PRE_GRAB,		// Color of (outlines and z-pre) e.g. World
	RSBT_DECAL_PRE_GRAB,		// If not doing outlining, RSBT_DECAL_POST_GRAB gets merged down here

	// Apply outlining

	RSBT_OPAQUE_POST_GRAB_NO_ZPRE,	// Depth+Color of (!outlines and !z-pre) e.g. what?
	RSBT_OPAQUE_POST_GRAB_IN_ZPRE,	// Color of (!outlines and z-pre) e.g. Trees
	RSBT_DECAL_POST_GRAB,			// All decals on top of opaque objects
	RSBT_ALPHA_PRE_DOF,				// Alpha objects beyond a preset distance that we want to get DoF applied to
	RSBT_ALPHA_NEED_GRAB_PRE_DOF,	// First refraction pass if rdr_state.alphaInDOF

	// Apply DoF, etc post-processing here, grab image for refraction

	RSBT_ALPHA_NEED_GRAB,			// First refraction pass if not rdr_state.alphaInDOF.  Blurred refraction in either case
	RSBT_ALPHA,
	RSBT_ALPHA_LOW_RES_ALPHA,		// Rendering is done at quarter resolution
	RSBT_ALPHA_LOW_RES_ADDITIVE,	// Also done at quarter resolution
	RSBT_ALPHA_LOW_RES_SUBTRACTIVE,	// Also done at quarter resolution

	RSBT_ALPHA_LATE,	  // for really close objects, happens after low res

	// If necessary, grab image for second refraction pass

	RSBT_ALPHA_NEED_GRAB_LATE,		// Late refraction

	RSBT_WIREFRAME,

	RSBT_COUNT,
} RdrSortBucketType;

typedef enum RdrShaderMode
{
	RDRSHDM_ZPREPASS,
	RDRSHDM_VISUAL,
	RDRSHDM_TARGETING, // Solid color-only for targeting
	RDRSHDM_VISUAL_HDR, // Will be different than _VISUAL if there is a z-prepass, and this is not an alpha object
	RDRSHDM_VISUAL_LRA_HIGH_RES, 
	RDRSHDM_SHADOW,
	RDRSHDM_DUMMY, // just used for frustum culling, no draw calls

	RDRSHDM_COUNT,
} RdrShaderMode;

typedef enum RdrLightType
{
	RDRLIGHT_NONE = 0,
	RDRLIGHT_DIRECTIONAL = 1,
	RDRLIGHT_POINT = 2,
#define RDRLIGHT_SPOT 3 // Not in enum to keep debugger happy
	RDRLIGHT_PROJECTOR = 4,
#define RDRLIGHT_TYPE_MAX 5 // Not in enum to keep debugger happy
	// Can have light types with indices up to 7
	RDRLIGHT_SHADOWED = 8, // on/off bit
#define RDRLIGHT_TYPE_MAX_WITH_SHADOWS (RDRLIGHT_SHADOWED | RDRLIGHT_TYPE_MAX)
	RDRLIGHT_DELETING = 16, // In process of deleting bit

#define RDRLIGHT_TYPE_MASK ((1<<3)-1)
#define RDRLIGHT_MASK ((1<<4)-1)
	// 0-15 taken, the bits in RdrMaterialShader will need to be shifted if you need to add more
	// Also the UBERLIGHT_BITS_PER_LIGHT would have to change
} RdrLightType;

extern StaticDefineInt RdrLightTypeEnum[];

typedef enum RdrLightColorType
{
	RLCT_WORLD,
	RLCT_CHARACTER,

	RLCT_COUNT,
} RdrLightColorType;
#define RdrLightColorType_NumBits 2
STATIC_ASSERT(RLCT_COUNT <= (1 << (RdrLightColorType_NumBits-1)));


typedef enum RdrMaterialRenderMode
{
	MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE	= 0,
	MATERIAL_RENDERMODE_HAS_ALPHA			= 1,
	MATERIAL_RENDERMODE_DEPTH_ONLY			= 2,
	MATERIAL_RENDERMODE_NOLIGHTS			= 3,
} RdrMaterialRenderMode;



typedef enum RdrMaterialShaderMask {
	// For VS debugger only, use other macros in code
#ifndef _PREFAST_ // This will cause these symbols to be in the final PDB, but not exist while compiling/analyzing code (HORRIBLE HACK ;)
	RenderMode_HasAlpha  = 1<<0,
	RenderMode_DepthOnly = 1<<1,
#endif

#define	MATERIAL_SHADER_RENDERMODE			3 // bit mask

	MATERIAL_SHADER_NOALPHAKILL				= 1<<2,
	MATERIAL_SHADER_SIMPLE_LIGHTING			= 1<<3,
	MATERIAL_SHADER_SHADOW_BUFFER			= 1<<4, // Use shadow buffer
	MATERIAL_SHADER_UBERLIGHT				= 1<<5, // light types ignored
	MATERIAL_SHADER_UBERSHADOW				= 1<<6, // light types ignored
	MATERIAL_SHADER_HAS_HDR_TEXTURE			= 1<<7,
	MATERIAL_SHADER_FORCE_2_LIGHTS			= 1<<8,
	MATERIAL_SHADER_VERTEX_ONLY_LIGHTING	= 1<<9,
	MATERIAL_SHADER_FORCE_SM20				= 1<<10,
	MATERIAL_SHADER_MANUAL_DEPTH_TEST		= 1<<11,
	MATERIAL_SHADER_COVERAGE_DISABLE		= 1<<12,
	MATERIAL_SHADER_D3D11					= 1<<13,
	MATERIAL_SHADER_STEREOSCOPIC			= 1<<14, // Only relevant to PostProcessing shaders

} RdrMaterialShaderMask;

// If the following enum is ever changed, the value contained in the AST_IGNORE field above the PreloadedLightCombo struct must also be changed so the fields will be properly rebinned
typedef enum RdrLightShaderMask {
	// (RdrLightType enum)
	// offsets for getting the light types back out
#define MATERIAL_SHADER_LIGHT0_OFFSET	0
#define MATERIAL_SHADER_LIGHT1_OFFSET	4
#define MATERIAL_SHADER_LIGHT2_OFFSET	8
#define MATERIAL_SHADER_LIGHT3_OFFSET	12
#define MATERIAL_SHADER_LIGHT4_OFFSET	16

	// For VS debugger only, use other macros in code
#ifndef _PREFAST_ // This will cause these symbols to be in the final PDB, but not exist while compiling/analyzing code (HORRIBLE HACK ;)

	// May be slightly confusing in the debugger, but Dir+Point is Spot
	Light0_Dir		= 1<<(MATERIAL_SHADER_LIGHT0_OFFSET+0),
	Light0_Point	= 1<<(MATERIAL_SHADER_LIGHT0_OFFSET+1),
	Light0_Proj		= 1<<(MATERIAL_SHADER_LIGHT0_OFFSET+2),
	Light0_Shadowed	= 1<<(MATERIAL_SHADER_LIGHT0_OFFSET+3),

	Light1_Dir		= 1<<(MATERIAL_SHADER_LIGHT1_OFFSET+0),
	Light1_Point	= 1<<(MATERIAL_SHADER_LIGHT1_OFFSET+1),
	Light1_Proj		= 1<<(MATERIAL_SHADER_LIGHT1_OFFSET+2),
	Light1_Shadowed	= 1<<(MATERIAL_SHADER_LIGHT1_OFFSET+3),

	Light2_Dir		= 1<<(MATERIAL_SHADER_LIGHT2_OFFSET+0),
	Light2_Point	= 1<<(MATERIAL_SHADER_LIGHT2_OFFSET+1),
	Light2_Proj		= 1<<(MATERIAL_SHADER_LIGHT2_OFFSET+2),
	Light2_Shadowed	= 1<<(MATERIAL_SHADER_LIGHT2_OFFSET+3),

	Light3_Dir		= 1<<(MATERIAL_SHADER_LIGHT3_OFFSET+0),
	Light3_Point	= 1<<(MATERIAL_SHADER_LIGHT3_OFFSET+1),
	Light3_Proj		= 1<<(MATERIAL_SHADER_LIGHT3_OFFSET+2),
	Light3_Shadowed	= 1<<(MATERIAL_SHADER_LIGHT3_OFFSET+3),

	Light4_Dir		= 1<<(MATERIAL_SHADER_LIGHT4_OFFSET+0),
	Light4_Point	= 1<<(MATERIAL_SHADER_LIGHT4_OFFSET+1),
	Light4_Proj		= 1<<(MATERIAL_SHADER_LIGHT4_OFFSET+2),
	Light4_Shadowed	= 1<<(MATERIAL_SHADER_LIGHT4_OFFSET+3), // Following may no longer be true // Breaks the enum debugger view somehow! (high bit), but we only support 2 shadowed lights anyway, so this shouldn't come up

#endif

	LIGHT_MATERIAL_SHADER_SHADOW_BUFFER = (1<<30),	// This is used in the function getLightComboRecordUsageKey() for lightComboRecordUsage()

} RdrLightShaderMask;

typedef union {
	S64 key;
	struct {
		RdrMaterialShaderMask shaderMask;
		RdrLightShaderMask lightMask;
	};
} RdrMaterialShader;

__forceinline RdrMaterialShader getRdrMaterialShader(RdrMaterialShaderMask shaderMask, RdrLightShaderMask lightMask) {
	RdrMaterialShader matShader;

	matShader.lightMask = lightMask;
	matShader.shaderMask = shaderMask;
	return matShader;
}

__forceinline RdrMaterialShader getRdrMaterialShaderByKey(S64 shaderKey) {
	RdrMaterialShader matShader;
	matShader.key = shaderKey;
	return matShader;
}

typedef enum RdrDrawListLightingMode
{
	RDLLM_SIMPLE,
	RDLLM_NORMAL,
	RDLLM_CCLIGHTING,
	RDLLM_VERTEX_ONLY,
	RDLLM_UBERLIGHTING,
} RdrDrawListLightingMode;

typedef enum RdrObjectCategory
{
	ROC_UNKNOWN = 0,
	ROC_WORLD,
	ROC_WORLD_HIGH_DETAIL,
	ROC_TERRAIN,
	ROC_CHARACTER,
	ROC_FX,
	ROC_SKY,
	ROC_PRIMITIVE,
	ROC_EDITOR_ONLY,
	ROC_RENDERER,

	ROC_COUNT,
} RdrObjectCategory;

#define RDR_DEPTH_BUCKET_COUNT 10
#define RDR_SIZE_BUCKET_COUNT 10

AUTO_STRUCT;
typedef struct RdrDrawListPassStats
{
//#if RDR_ENABLE_DRAWLIST_HISTOGRAMS
	// These members are disabled via comment instead of preproccessor due to 
	// StructParser limitations. Uncomment when enabling RDR_ENABLE_DRAWLIST_HISTOGRAMS.
	//int depth_histogram[RDR_DEPTH_BUCKET_COUNT];
	//int size_histogram[RDR_SIZE_BUCKET_COUNT];
//#endif
	int objects_drawn[ROC_COUNT];
	int triangles_drawn[ROC_COUNT];
	int opaque_objects_drawn;
	int alpha_objects_drawn;
	int opaque_triangles_drawn;
	int alpha_triangles_drawn;
} RdrDrawListPassStats;

extern ParseTable parse_RdrDrawListPassStats[];
#define TYPE_parse_RdrDrawListPassStats RdrDrawListPassStats

AUTO_STRUCT;
typedef struct RdrDrawListStats
{
	RdrDrawListPassStats pass_stats[RDRSHDM_COUNT]; AST( INDEX(RDRSHDM_ZPREPASS, zprepass) INDEX(RDRSHDM_VISUAL, visual) INDEX(RDRSHDM_VISUAL_HDR, visual_hdr) INDEX(RDRSHDM_SHADOW, shadows) )

	int	opaque_instanced_objects;
	int alpha_instanced_objects;

	int failed_draw_this_frame;

} RdrDrawListStats;

extern ParseTable parse_RdrDrawListStats[];
#define TYPE_parse_RdrDrawListStats RdrDrawListStats

typedef enum RdrFeature
{
	FEATURE_ANISOTROPY			  = 1<<0,
	FEATURE_NONPOW2TEXTURES		  = 1<<1,
	FEATURE_NONSQUARETEXTURES	  = 1<<2,
	FEATURE_MRT2				  = 1<<3,
	FEATURE_MRT4				  = 1<<4,
	FEATURE_SM20				  = 1<<5,
	FEATURE_TXAA				  = 1<<6,
	FEATURE_SM2B				  = 1<<7,
	FEATURE_SM30				  = 1<<8,
	FEATURE_SRGB				  = 1<<9,
	FEATURE_DX10_LEVEL_CARD		  = 1<<10,	// For checking >= GF8 vs <= GF7
	FEATURE_INSTANCING			  = 1<<11,
	FEATURE_VFETCH				  = 1<<12,
	FEATURE_DEPTH_TEXTURE		  = 1<<13,	// a depth buffer that can be mapped as a texture
	FEATURE_24BIT_DEPTH_TEXTURE	  = 1<<14,	// a 24 bit depth buffer that can be mapped as a texture.
	FEATURE_STENCIL_DEPTH_TEXTURE = 1<<15,	// a stencil buffer is available w/ depth buffers that can be mapped as a texture   
	FEATURE_DECL_F16_2			  = 1<<16,	// Supports D3DDTCAPS_FLOAT16_2 for texture coordinates
	FEATURE_SBUF_FLOAT_FORMATS	  = 1<<17,	// Supports SBT_RGBA_FLOAT and SBT_FLOAT
	FEATURE_DXT_VOLUME_TEXTURE	  = 1<<18,
	FEATURE_DEPTH_TEXTURE_MSAA	  = 1<<19,	// Supports reading depth textures from MSAA render targets
	FEATURE_NV_CSAA_SURFACE		  = 1<<20,	// Supports nvidia specific CSAA surfaces
	FEATURE_DX11_RENDERER		  = 1<<21,	// Using the DX11 renderer (possibly on a DX10 card, etc)
	FEATURE_TESSELLATION			  = 1<<22,	// Tessellation is only supported in D3D11
	FEATURE_D3D9EX				  = 1<<23,	// Device has D3D9Ex features and they are enabled.

} RdrFeature;

AUTO_ENUM;
typedef enum RdrSpriteEffect {
	RdrSpriteEffect_Undefined = -1,
	RdrSpriteEffect_None = 0,
	RdrSpriteEffect_TwoTex,
	RdrSpriteEffect_Desaturate,
	RdrSpriteEffect_Desaturate_TwoTex,
	RdrSpriteEffect_Smooth,
	RdrSpriteEffect_DistField1Layer,
	RdrSpriteEffect_DistField2Layer,
	RdrSpriteEffect_DistField1LayerGradient,
	RdrSpriteEffect_DistField2LayerGradient,
	RdrSpriteEffect_MAX,
} RdrSpriteEffect;
extern StaticDefineInt RdrSpriteEffectEnum[];
#define RdrSpriteEffect_NumBits 4
STATIC_ASSERT(RdrSpriteEffect_MAX <= (1<<RdrSpriteEffect_NumBits)); // Keep this STATIC_ASSERT last in the file, it confuses CTAGS/WWhiz

typedef enum RdrPPBlendType
{
	RPPBLEND_REPLACE,	   // keep this one first so it is the default
	RPPBLEND_ALPHA,
	RPPBLEND_ADD,
	RPPBLEND_SUBTRACT,
	RPPBLEND_LOW_RES_ALPHA_PP, //< used by low res particles, to combine things back
} RdrPPBlendType;

typedef enum RdrPPDepthTestMode
{
	RPPDEPTHTEST_OFF, // keep this one first so it is the default
	RPPDEPTHTEST_LEQUAL,
	RPPDEPTHTEST_LESS,
	RPPDEPTHTEST_EQUAL,
	RPPDEPTHTEST_DEFAULT
} RdrPPDepthTestMode;

typedef enum RdrStereoOption
{
	SURF_STEREO_AUTO,
	SURF_STEREO_FORCE_ON,
	SURF_STEREO_FORCE_OFF,
} RdrStereoOption;

#endif
