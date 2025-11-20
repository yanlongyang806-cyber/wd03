#pragma once
GCC_SYSTEM

typedef struct Material Material;
typedef struct Model Model;
typedef struct DynSkeleton DynSkeleton;
typedef struct DynNode DynNode;
typedef struct RdrDrawList RdrDrawList;
typedef struct DynRagdollPartState DynRagdollPartState;

// Extend the collision query for the splat by SPLAT_ENDCAP_TOLERANCE_RADIUS_SCALE *
// radius along the cylinder axis for the start and end positions so we can handle cases
// where an end corner pokes out of the tolerance area.
#define SPLAT_ENDCAP_TOLERANCE_RADIUS_SCALE 1.41421

typedef struct GfxSplat
{
	int splat_flags;
	Vec3 center;
	Vec3 fade_plane_pt;
	Mat4 initial_start;
	F32 initial_radius;
	Vec3 initial_direction;
	float radius;
	float tolerance;
	Material * material;
	Model **splat_models;

	Vec4 color;
	int vertex_count;
	F32 aspect_y;
	Mat44 texture_proj_mat;
	bool shader_tex_project;
} GfxSplat;

typedef enum SplatFlags
{
	GFX_SPLAT_NONE,

	// This flag indicates splat geometry facing away from the projector will be included in the splat.
	GFX_SPLAT_TWO_SIDED					=1<<0,

	// This flag indicates splat geometry not overlapping the unit square [0,1]x[0,1] will be
	// culled from the splat.
	GFX_SPLAT_CULL_UNIT_TEX				=1<<1,

	// This flag indicates the texture projection will center a single tile of the 
	// texture around the projection axis. This transforms the projected texture coordinates
	// from the range [-1,1]x[-1,1] to the unit square [0,1]x[0,1].
	GFX_SPLAT_UNIT_TEXCOORD				=1<<2,

	// This flag indicates the start parameter contains the full transformation
	// to from texture space to world space.
	GFX_SPLAT_USE_SPECIFIED_TRANSFORM	=1<<3,

	// This flag indicates that the model vertices should be created relative to the normalized 
	// version of the start matrix.
	GFX_SPLAT_RELATIVE_COORDS			=1<<4,

	// This flag tells the model generation not to make tangents and binormals.
	GFX_SPLAT_NO_TANGENTSPACE			=1<<5,

} SplatFlags;

GfxSplat * gfxCreateSplatMtx(
	const Mat4 start, const Mat4 textureProjection,
	F32 start_radius, F32 aspect_y,
	const Vec3 direction, F32 end_radius, int splat_options, Material * material,
	F32 motion_tolerance, F32 fFadePlanePt);

void gfxUpdateSplatMtx(
	const Mat4 start, const Mat4 textureProjection,
	F32 start_radius, F32 aspect_y,
	const Vec3 direction, F32 end_radius, GfxSplat * splat, bool bForceResplat,
	F32 fFadePlanePt);

GfxSplat * gfxCreateSplat(
	const Vec3 start, const Mat4 textureProjection,
	F32 start_radius, F32 aspect_y,
	const Vec3 direction, F32 end_radius, int splat_options, Material * material,
	F32 motion_tolerance);

void gfxUpdateSplat(
	const Vec3 start, const Mat4 textureProjection,
	F32 start_radius, F32 aspect_y,
	const Vec3 direction, F32 end_radius, GfxSplat * splat, bool bForceResplat,
	F32 fFadePlanePt);

void gfxDestroySplat(GfxSplat * splat);

Model* gfxSplatGetModel(GfxSplat* splat, int index);
int gfxSplatGetNumModels(const GfxSplat* splat);

Material* gfxSplatGetMaterial(GfxSplat* splat);
void gfxSplatSetMaterial(GfxSplat* splat, Material* mat);

void gfxSplatGetWorldMatrix(GfxSplat* splat, Mat4 world_matrix);
void gfxSplatGetTextureMatrix(GfxSplat* splat, Mat44 tex_matrix);
bool gfxSplatIsUsingShaderProjection(GfxSplat* splat);

GfxSplat * gfxCreateSkeletonShadowSplat(const DynSkeleton* pSkeleton, F32 motion_tolerance);
void gfxUpdateSkeletonShadowSplat(const DynSkeleton* pSkeleton, GfxSplat * splat, bool bForceResplat);

GfxSplat * gfxCreateRagdollPartShadowSplat(const DynSkeleton *pSkeleton, DynRagdollPartState *pRagdollPart, F32 motion_tolerance);
void gfxUpdateRagdollPartShadowSplat(const DynSkeleton *pSkeleton, DynRagdollPartState *pRagdollPart, GfxSplat *pSplat, bool bForceResplat);

GfxSplat * gfxCreateNodeShadowSplat(const DynNode* pNode, Vec2 vSize, F32 motion_tolerance, bool useOrientation);
void gfxUpdateNodeShadowSplat(const DynNode* pNode, GfxSplat * splat, Vec2 vSize, bool useOrientation, bool bForceResplat);

//max_alpha is only used for AO splats
void gfxQueueShadowOrAOSplatToDrawList(GfxSplat* splat, F32 alpha, F32 max_alpha, bool frustum_visible, RdrDrawList* pDrawList);
//These are the same except for the drawlist they use
void gfxQueueShadowSplat(GfxSplat* splat, F32 alpha, bool frustum_visible);
void gfxQueueAOShadowSplat(GfxSplat* splat, F32 alpha, F32 max_alpha, bool frustum_visible);
