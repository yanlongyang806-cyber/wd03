#ifndef GFXSPRITE_H
#define GFXSPRITE_H
GCC_SYSTEM

// Public sprite interfaces

#include "RdrEnums.h"

typedef struct AtlasTex AtlasTex;
typedef struct BasicTexture BasicTexture;
typedef struct CBox CBox;
typedef struct Clipper2D Clipper2D;
typedef U64 TexHandle;
typedef struct GfxSpriteList GfxSpriteList;
typedef struct GfxSpriteListEntry GfxSpriteListEntry;
typedef struct RdrDrawList RdrDrawList;
typedef struct RdrSpriteState RdrSpriteState;
typedef struct RdrSpriteVertex RdrSpriteVertex;
typedef struct AtlasTex AtlasTex;
typedef struct BasicTexture BasicTexture;

#include "GfxClipper.h"
#include "GfxTexAtlas.h"
#include "GfxTexturesPublic.h"
#include "GfxTexOpts.h"
#include "CBox.h"
#include "Color.h"
#include "GraphicsLib.h"
#include "StructDefines.h"
#include "GfxSpriteList.h"
#include "utils.h"
#include "RdrDrawable.h"
#include "MatrixStack.h"
#include "mathutil.h"
#include "error.h"

extern int force_disable_font_scaling;
extern bool g_no_sprites_allowed;
extern GfxSpriteList* current_sprite_list;
extern BasicTexture* sprite_line_tex;
extern TransformationMatrix **eaSpriteMatrixStack;

typedef struct SpriteProperties {
	RdrMaterialFlags flags;
	F32 screen_distance;
	bool is_3D;
	bool ignore_depth_test;
} SpriteProperties;

#define gfxMatrixPop() matrixStackPop(&eaSpriteMatrixStack)
#define gfxMatrixPush() matrixStackPush(&eaSpriteMatrixStack)
#define gfxMatrixGet() matrixStackGet(&eaSpriteMatrixStack)

#ifdef _FULLDEBUG
#define SPRITE_INLINE_OPTION
#else
#define SPRITE_INLINE_OPTION __forceinline
#endif

U32 spriteEffectOverrideColor(U32 rgba);

void gfxSetCurrentSpriteList(GfxSpriteList* spriteList);
GfxSpriteList* gfxGetCurrentSpriteList();

//lines are rendered as quads so we have a lookup texture to fake antialiased lines
void gfxSetupSpriteLineTexture();
BasicTexture* gfxGetSpriteLineTexture();

void display_sprite_NinePatch_test(AtlasTex *spr, float xp, float yp, float zp, float width, float height, int rgba);

//on xbox we need to swap the channels around due to the vertex format
__forceinline static void setSpriteVertColorFromRGBA(RdrSpriteVertex* vert, int rgba)
{
#ifdef _XBOX
	Color tmp = colorFromRGBA(rgba);
	vert->color.r = tmp.a;
	vert->color.g = tmp.b;
	vert->color.b = tmp.g;
	vert->color.a = tmp.r;
#else
	vert->color = colorFromRGBA(rgba);
#endif
}

__forceinline static void setSpriteVertColorFromColor(RdrSpriteVertex* vert, Color srcColor)
{
#ifdef _XBOX
	vert->color.r = srcColor.a;
	vert->color.g = srcColor.b;
	vert->color.b = srcColor.g;
	vert->color.a = srcColor.r;
#else
	vert->color = srcColor;
#endif
}

__forceinline static void checkScissor(RdrSpriteState *sprite)
{
	// arbitrarily large number to check for bad data
	const U32 SCISSOR_SANITY_CHECK = 50000;
	//assert(sprite->scissor_x < SCISSOR_SANITY_CHECK); 
	//assert(sprite->scissor_width < SCISSOR_SANITY_CHECK);
	//assert(sprite->scissor_y < SCISSOR_SANITY_CHECK);
	//assert(sprite->scissor_height < SCISSOR_SANITY_CHECK);
	if ((sprite->scissor_x > SCISSOR_SANITY_CHECK)
		|| (sprite->scissor_width > SCISSOR_SANITY_CHECK)
		|| (sprite->scissor_y > SCISSOR_SANITY_CHECK)
		|| (sprite->scissor_height > SCISSOR_SANITY_CHECK))
	{
		ErrorDetailsf("(%u, %u, %u, %u)", sprite->scissor_x, sprite->scissor_y, sprite->scissor_width, sprite->scissor_height);
		Errorf("Sprite failed scissor sanity check!");
	}
}

extern RdrSpriteEffect sprite_override_effect;
extern U32 sprite_override_effect_color;
extern F32 sprite_override_effect_weight;


__forceinline static void spriteFromTriangleDispSprite(RdrSpriteVertex *vertices, AtlasTex* atex1, BasicTexture* btex1, int rgba, int rgba2, int rgba3,
													   float u1_0, float v1_0, float u1_1, float v1_1, float u1_2, float v1_2, 
													   Vec2 ul, Vec2 lr, Vec2 p2)
{
	Vec3 u1, v1;

	if (atex1)
	{
		atlasGetModifiedUVs(atex1, u1_0, v1_0, &u1[0], &v1[0]);
		atlasGetModifiedUVs(atex1, u1_1, v1_1, &u1[1], &v1[1]);
		atlasGetModifiedUVs(atex1, u1_2, v1_2, &u1[2], &v1[2]);
	}
	else if (btex1)
	{
		float umult = ((float)btex1->width)  / ((float)btex1->realWidth);
		float vmult = ((float)btex1->height) / ((float)btex1->realHeight);

		u1[0] = u1_0 * umult;
		u1[1] = u1_1 * umult;
		u1[2] = u1_2 * umult;

		v1[0] = v1_0 * vmult;
		v1[1] = v1_1 * vmult;
		v1[2] = v1_2 * vmult;
	}
	else
	{
		u1[0] = u1_0;
		u1[1] = u1_1;
		u1[2] = u1_2;

		v1[0] = v1_0;
		v1[1] = v1_1;
		v1[2] = v1_2;
	}

	vertices[0].point[0] = ul[0];
	vertices[0].point[1] = ul[1];
	setSpriteVertColorFromRGBA(&vertices[0], rgba);
	setVec4(vertices[0].texcoords, u1[0], v1[0], 0, 0);

	vertices[1].point[0] = lr[0];
	vertices[1].point[1] = lr[1];
	setSpriteVertColorFromRGBA(&vertices[1], rgba2);
	setVec4(vertices[1].texcoords, u1[1], v1[1], 0, 0);

	vertices[2].point[0] = p2[0];
	vertices[2].point[1] = p2[1];
	setSpriteVertColorFromRGBA(&vertices[2], rgba3);
	setVec4(vertices[2].texcoords, u1[2], v1[2], 0, 0);

	vertices[3] = vertices[2];
}

__forceinline static void spriteFromRotDispSprite(RdrSpriteVertex *vertices, AtlasTex* atex1, BasicTexture* btex1, AtlasTex* atex2, BasicTexture* btex2, int rgba, int rgba2, int rgba3, int rgba4, 
												  float u1_0, float v1_0, float u1_1, float v1_1, float u1_2, float v1_2, float u1_3, float v1_3,
												  float u2_0, float v2_0, float u2_1, float v2_1, float u2_2, float v2_2, float u2_3, float v2_3,
												  float angle, Vec2 ul, Vec2 lr)
{
	F32		/*z = -1.001f,*/ scale=1;
	Vec3	dv,xys[4],min,max, diff;
	Mat4	m;
	float halfwidth, halfheight;
	Vec4 u1, v1, u2, v2;

	PERFINFO_AUTO_START_FUNC();

	if (atex1)
	{
		atlasGetModifiedUVs(atex1, u1_0, v1_0, &u1[0], &v1[0]);
		atlasGetModifiedUVs(atex1, u1_1, v1_1, &u1[1], &v1[1]);
		atlasGetModifiedUVs(atex1, u1_2, v1_2, &u1[2], &v1[2]);
		atlasGetModifiedUVs(atex1, u1_3, v1_3, &u1[3], &v1[3]);
	}
	else if (btex1)
	{
		float umult = ((float)btex1->width)  / ((float)btex1->realWidth);
		float vmult = ((float)btex1->height) / ((float)btex1->realHeight);

		u1[0] = u1_0 * umult;
		u1[1] = u1_1 * umult;
		u1[2] = u1_2 * umult;
		u1[3] = u1_3 * umult;

		v1[0] = v1_0 * vmult;
		v1[1] = v1_1 * vmult;
		v1[2] = v1_2 * vmult;
		v1[3] = v1_3 * vmult;
	}
	else
	{
		u1[0] = u1_0;
		u1[1] = u1_1;
		u1[2] = u1_2;
		u1[3] = u1_3;

		v1[0] = v1_0;
		v1[1] = v1_1;
		v1[2] = v1_2;
		v1[3] = v1_2;
	}


	if (atex2)
	{
		atlasGetModifiedUVs(atex2, u2_0, v2_0, &u2[0], &v2[0]);
		atlasGetModifiedUVs(atex2, u2_1, v2_1, &u2[1], &v2[1]);
		atlasGetModifiedUVs(atex2, u2_2, v2_2, &u2[2], &v2[2]);
		atlasGetModifiedUVs(atex2, u2_3, v2_3, &u2[3], &v2[3]);
	}
	else if (btex2)
	{
		float umult = ((float)btex2->width)  / ((float)btex2->realWidth);
		float vmult = ((float)btex2->height) / ((float)btex2->realHeight);

		u2[0] = u2_0 * umult;
		u2[1] = u2_1 * umult;
		u2[2] = u2_2 * umult;
		u2[3] = u2_3 * umult;

		v2[0] = v2_0 * vmult;
		v2[1] = v2_1 * vmult;
		v2[2] = v2_2 * vmult;
		v2[3] = v2_3 * vmult;
	}
	else
	{
		u2[0] = u2_0;
		u2[1] = u2_1;
		u2[2] = u2_2;
		u2[3] = u2_3;

		v2[0] = v2_0;
		v2[1] = v2_1;
		v2[2] = v2_2;
		v2[3] = v2_2;
	}



	copyMat4( unitmat, m );
	yawMat3( angle, m );

#if 0
	min[0] = (-src->spr->width/2 ) * src->xscale * txsc;
	min[2] = (-src->spr->height/2) * src->yscale * tysc;
	max[0] = ( src->spr->width/2 ) * src->xscale * txsc;
	max[2] = ( src->spr->height/2) * src->yscale * tysc;

	diff[0] = src->xp*txsc + src->spr->width / 2 * src->xscale * txsc;
	diff[1] = 0.0;
	diff[2] = pkg_height - src->yp*tysc - src->spr->height / 2 * src->yscale * tysc;
#endif

	halfwidth = (lr[0] - ul[0]) * 0.5f;
	halfheight = (ul[1] - lr[1]) * 0.5f;

	// centered extents
	min[0] = -halfwidth;
	min[2] = -halfheight;
	max[0] = halfwidth;
	max[2] = halfheight;

	// midpoint
	diff[0] = ul[0] + halfwidth;
	diff[1] = 0.0;
	diff[2] = lr[1] + halfheight;

	dv[1] = 0;
	dv[0] = min[0];
	dv[2] = min[2];
	mulVecMat4(dv,m,xys[2]);
	dv[0] = max[0];
	dv[2] = max[2];
	mulVecMat4(dv,m,xys[0]);
	dv[0] = min[0];
	dv[2] = max[2];
	mulVecMat4(dv,m,xys[3]);
	dv[0] = max[0];
	dv[2] = min[2];
	mulVecMat4(dv,m,xys[1]);

	addVec3( xys[0], diff, xys[0] );
	addVec3( xys[1], diff, xys[1] );
	addVec3( xys[2], diff, xys[2] );
	addVec3( xys[3], diff, xys[3] );


	// increase x first
	vertices[0].point[0] = xys[3][0];
	vertices[0].point[1] = xys[3][2];
	setSpriteVertColorFromRGBA(&vertices[0], rgba);
	setVec4(vertices[0].texcoords, u1[0], v1[0], u2[0], v2[0]);

	vertices[1].point[0] = xys[0][0];
	vertices[1].point[1] = xys[0][2];
	setSpriteVertColorFromRGBA(&vertices[1], rgba2);
	setVec4(vertices[1].texcoords, u1[1], v1[1], u2[1], v2[1]);

	vertices[2].point[0] = xys[1][0];
	vertices[2].point[1] = xys[1][2];
	setSpriteVertColorFromRGBA(&vertices[2], rgba3);
	setVec4(vertices[2].texcoords, u1[2], v1[2], u2[2], v2[2]);

	vertices[3].point[0] = xys[2][0];
	vertices[3].point[1] = xys[2][2];
	setSpriteVertColorFromRGBA(&vertices[3], rgba4);
	setVec4(vertices[3].texcoords, u1[3], v1[3], u2[3], v2[3]);

	PERFINFO_AUTO_STOP_FUNC();
}

__forceinline void gfxTransformAndClipVerticies(RdrSpriteVertex* sverts4, Mat3 *pMatrix, Clipper2D *pClipper)
{
	int i;
	Vec3 v3out;
	CBox *pClipBox = clipperGetBox(pClipper);
	int screen_height;
	gfxGetActiveSurfaceSizeInline(NULL, &screen_height); 

#define DOIT(src, dest, var, target, flip)													\
	float delta;																			\
	float factor;																			\
	if (flip)																				\
	{																						\
		delta = target - (screen_height - src.var);											\
		factor = delta / (dest.var - (screen_height - src.var));							\
	}																						\
	else																					\
	{																						\
		delta = target - src.var;															\
		factor = delta / (dest.var - src.var);												\
	}																						\
	src.var += delta;																		\
	src.texcoords[0] = src.texcoords[0] + (dest.texcoords[0] - src.texcoords[0]) * factor;	\
	src.texcoords[1] = src.texcoords[1] + (dest.texcoords[1] - src.texcoords[1]) * factor;	\
	src.texcoords[2] = src.texcoords[2] + (dest.texcoords[2] - src.texcoords[2]) * factor;	\
	src.texcoords[3] = src.texcoords[3] + (dest.texcoords[3] - src.texcoords[3]) * factor;	\
	src.color = ColorLerp(src.color, dest.color, factor)

	// Clip textures if necessary
	if (pClipBox)
	{
		// Make sure that the boxes overlap
		// Also, Y is reversed because of the conflicting coordinate spaces. 
		if (sverts4[0].point[0] < pClipBox->hx
			&& (screen_height - sverts4[0].point[1]) < pClipBox->hy
			&& sverts4[2].point[0] >= pClipBox->lx 
			&& (screen_height - sverts4[2].point[1]) >= pClipBox->ly)
		{

			if (sverts4[0].point[0] < pClipBox->lx) {
				DOIT(sverts4[0], sverts4[1], point[0], pClipBox->lx, 0);
			}
			if (sverts4[3].point[0] < pClipBox->lx) {
				DOIT(sverts4[3], sverts4[2], point[0], pClipBox->lx, 0);
			}
			if (sverts4[0].point[1] < pClipBox->ly) {
				DOIT(sverts4[0], sverts4[3], point[1], pClipBox->ly, 1);
			}
			if (sverts4[1].point[1] < pClipBox->ly) {
				DOIT(sverts4[1], sverts4[2], point[1], pClipBox->ly, 1);
			}
			if (sverts4[1].point[0] >= pClipBox->hx) {
				DOIT(sverts4[1], sverts4[0], point[0], pClipBox->hx, 0);
			}
			if (sverts4[2].point[0] >= pClipBox->hx) {
				DOIT(sverts4[2], sverts4[3], point[0], pClipBox->hx, 0);
			}
			if (sverts4[3].point[1] >= pClipBox->hy) {
				DOIT(sverts4[3], sverts4[0], point[1], pClipBox->hy, 1);
			}
			if (sverts4[2].point[1] >= pClipBox->hy) {
				DOIT(sverts4[2], sverts4[1], point[1], pClipBox->hy, 1);
			}
		}
	}
#undef DOIT

	// Transform points
	for (i = 0; i < 4; i++)
	{
		Vec3 v3in = { sverts4[i].point[0], sverts4[i].point[1], 1};
		mulVecMat3(v3in, *pMatrix, v3out);
		sverts4[i].point[0] = v3out[0];
		sverts4[i].point[1] = v3out[1];
	}
}


#define UVS(u1, v1, u2, v2) u1, v1, u2, v1, u2, v2, u1, v2


SPRITE_INLINE_OPTION
void spriteFromDispSprite(RdrSpriteVertex *vertices, AtlasTex* atex1, BasicTexture* btex1, AtlasTex* atex2, BasicTexture* btex2, int rgba, int rgba2, int rgba3, int rgba4, 
	float u1_0, float v1_0, float u1_1, float v1_1, float u1_2, float v1_2, float u1_3, float v1_3,
	float u2_0, float v2_0, float u2_1, float v2_1, float u2_2, float v2_2, float u2_3, float v2_3,
	float skew, Vec2 ul, Vec2 lr);

SPRITE_INLINE_OPTION
int create_sprite_ex(AtlasTex *atex1, BasicTexture *btex1, 
	AtlasTex *atex2, BasicTexture *btex2, 
	float xp, float yp, float zp, 
	float xscale, float yscale, 
	int rgba, int rgba2, int rgba3, int rgba4, 
	float u1_0, float v1_0, float u1_1, float v1_1, float u1_2, float v1_2, float u1_3, float v1_3,
	float u2_0, float v2_0, float u2_1, float v2_1, float u2_2, float v2_2, float u2_3, float v2_3,
	float angle, int additive, Clipper2D *clipper, 
	RdrSpriteEffect sprite_effect, F32 effect_weight, SpriteFlags sprite_flags);

// Any changes made to this include must be made with the corresponding include in GfxSprite.C
// Only one is ever expected to be active at a time depending on solution configuration.
#ifndef _FULLDEBUG

#include "GfxSprite.inl"

#endif

//this prevents the sprites from being tested against the scissor rectangle on the CPU
//for fonts (which are the only things this function is currently used for)
//this is slower since most of them are inside or just partially clipped
#define SKIP_FONT_CPU_CLIPPING

__forceinline static int create_df_sprite_ex(
	BasicTexture *btex, 
	float xp, float yp, float zp, 
	float xscale, float yscale, 
	int rgba, int rgba2, int rgba3, int rgba4, 
	int topRgba, int bottomRgba, float topPt, float bottomPt,
	float u1_0, float v1_0, float u1_1, float v1_1, float u1_2, float v1_2, float u1_3, float v1_3,
	float angle, float skew, int additive, Clipper2D *clipper, 
	float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1,
	int rgbaMainL1, int rgbaOutlineL1,
	float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2,
	int rgbaMainL2, int rgbaOutlineL2,
	RdrSpriteEffect sprite_effect, SpriteProperties *pProps)
{
	float sprite_width, sprite_height;
	int screen_width, screen_height;
#ifndef SKIP_FONT_CPU_CLIPPING
	float half_sprite_width, half_sprite_height, center_x, center_y;
	Mat4 m;
	Vec2 bbox_ul, bbox_lr;
	Vec3 in_point, out_point;
#endif
	Vec2 sprite_ul, sprite_lr;
	CBox *glBox;
	GfxSpriteListEntry* sprite;
	RdrSpriteState* sstate;
	RdrSpriteVertex* sverts4;
	Mat3 *pMatrix;

	sprite_width = (float)btex->width;
	sprite_height = (float)btex->height;

	PERFINFO_AUTO_START_FUNC_L3();

	gfxGetActiveSurfaceSizeInline(&screen_width, &screen_height);

#ifdef _FULLDEBUG
	//This assert generates a lot of assembly
	assertmsg((maxDenL1 >= 1.0f || maxDenL1 == 0) && (sprite_effect == RdrSpriteEffect_DistField1Layer || sprite_effect == RdrSpriteEffect_DistField1LayerGradient || maxDenL2 >= 1.0f || maxDenL2 == 0),
		"the max density parameter is currently disabled to make the font shader fit in SM2.0. If you need it talk to Lucas");
#endif

	//make the skew have the same units as xscale
	skew *= sprite_width;
	// width and height scaling
	sprite_width *= xscale;
	sprite_height *= yscale;

	// x positions.
	sprite_ul[0] = xp;
	sprite_lr[0] = sprite_ul[0] + sprite_width;

	// y positions.  Flip the y axis for sprites
	sprite_ul[1] = screen_height - yp;
	sprite_lr[1] = sprite_ul[1] - sprite_height;


#ifdef SKIP_FONT_CPU_CLIPPING
	// Do a quick clip check if nothing fancy is being done
	if (angle == 0 && skew == 0
		&& !clipperTestValuesGLSpace(clipper, sprite_ul, sprite_lr))
	{
		PERFINFO_AUTO_STOP_L3();
		return 0;
	}
#else
	// bounding box
	if (angle != 0)
	{
		copyMat4(unitmat, m);
		yawMat3(angle, m);

		half_sprite_width = sprite_width * 0.5f;
		half_sprite_height = sprite_height * 0.5f;

		in_point[1] = 0;

		// add UL rotated point
		in_point[0] = -half_sprite_width;
		in_point[2] = half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		bbox_ul[0] = bbox_lr[0] = out_point[0];
		bbox_ul[1] = bbox_lr[1] = out_point[2];

		// add LL rotated point
		//in_point[0] = -half_sprite_width;
		in_point[2] = -half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		MIN1(bbox_ul[0], out_point[0]);
		MAX1(bbox_lr[0], out_point[0]);
		MAX1(bbox_ul[1], out_point[2]);
		MIN1(bbox_lr[1], out_point[2]);

		// add LR rotated point
		in_point[0] = half_sprite_width;
		//in_point[2] = -half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		MIN1(bbox_ul[0], out_point[0]);
		MAX1(bbox_lr[0], out_point[0]);
		MAX1(bbox_ul[1], out_point[2]);
		MIN1(bbox_lr[1], out_point[2]);

		// add UR rotated point
		//in_point[0] = half_sprite_width;
		in_point[2] = half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		MIN1(bbox_ul[0], out_point[0]);
		MAX1(bbox_lr[0], out_point[0]);
		MAX1(bbox_ul[1], out_point[2]);
		MIN1(bbox_lr[1], out_point[2]);

		// center bbox on unrotated box
		center_x = sprite_ul[0] + half_sprite_width;
		bbox_ul[0] += center_x;
		bbox_lr[0] += center_x;

		center_y = sprite_lr[1] + half_sprite_height;
		bbox_ul[1] += center_y;
		bbox_lr[1] += center_y;
	}
	else if (skew != 0)
	{
		sprite_ul[0] += skew;
		half_sprite_width = sprite_width * 0.5f;
		half_sprite_height = sprite_height * 0.5f;

		in_point[1] = 0;

		// add UL skewed point
		in_point[0] = -half_sprite_width+skew;
		in_point[2] = half_sprite_height;
		bbox_ul[0] = bbox_lr[0] = in_point[0];
		bbox_ul[1] = bbox_lr[1] = in_point[2];

		// add LL skewed point
		in_point[0] = -half_sprite_width-skew;
		in_point[2] = -half_sprite_height;
		MIN1(bbox_ul[0], in_point[0]);
		MAX1(bbox_lr[0], in_point[0]);
		MAX1(bbox_ul[1], in_point[2]);
		MIN1(bbox_lr[1], in_point[2]);

		// add LR skewed point
		in_point[0] = half_sprite_width-skew;
		in_point[2] = -half_sprite_height;
		MIN1(bbox_ul[0], in_point[0]);
		MAX1(bbox_lr[0], in_point[0]);
		MAX1(bbox_ul[1], in_point[2]);
		MIN1(bbox_lr[1], in_point[2]);

		// add UR rotated point
		in_point[0] = half_sprite_width+skew;
		in_point[2] = half_sprite_height;
		MIN1(bbox_ul[0], in_point[0]);
		MAX1(bbox_lr[0], in_point[0]);
		MAX1(bbox_ul[1], in_point[2]);
		MIN1(bbox_lr[1], in_point[2]);

		// center bbox on unskewed box
		center_x = sprite_ul[0] + half_sprite_width;
		bbox_ul[0] += center_x;
		bbox_lr[0] += center_x;

		center_y = sprite_lr[1] + half_sprite_height;
		bbox_ul[1] += center_y;
		bbox_lr[1] += center_y;
	}
	else
	{
		bbox_ul[0] = sprite_ul[0];
		bbox_ul[1] = sprite_ul[1];
		bbox_lr[0] = sprite_lr[0];
		bbox_lr[1] = sprite_lr[1];
	}

	// Test clipper rejection
	if (!clipperTestValuesGLSpace(clipper, bbox_ul, bbox_lr))
	{
		PERFINFO_AUTO_STOP_L3();
		return 0;
	}
#endif

	sprite = gfxStartAddSpriteToList(current_sprite_list, zp, pProps ? pProps->is_3D : false, &sstate, &sverts4);

	sstate->bits_for_test = 0;
	sstate->additive = additive;
	sstate->sprite_effect = sprite_effect;
	if (pProps && pProps->ignore_depth_test)
		sstate->ignore_depth_test = 1;

	gfxSpriteListHookupTextures(current_sprite_list, sprite, 0, btex, 0, 0, pProps ? pProps->is_3D : false);

	//we know the uvs are already correct so no need to pass the texture
	if (angle == 0)
		spriteFromDispSprite(sverts4, 0, 0, 0, 0, rgba, rgba2, rgba3, rgba4, 
		u1_0, v1_0, u1_1, v1_1, u1_2, v1_2, u1_3, v1_3, 
		0, 0, 0, 0, 0, 0, 0, 0, 
		0, sprite_ul, sprite_lr);
	else
		spriteFromRotDispSprite(sverts4, 0, 0, 0, 0, rgba, rgba2, rgba3, rgba4, 
		u1_0, v1_0, u1_1, v1_1, u1_2, v1_2, u1_3, v1_3, 
		0, 0, 0, 0, 0, 0, 0, 0, 
		angle, sprite_ul, sprite_lr);

	pMatrix = matrixStackGet(&eaSpriteMatrixStack);
	if (pMatrix)
	{
		gfxTransformAndClipVerticies(sverts4, pMatrix, clipper);
	}

	// Everything else.
	PERFINFO_AUTO_START_L3("Setup params", 1);

	setVec2(sstate->df_layer_settings[0].offset, uOffsetL1, vOffsetL1);
	sstate->df_layer_settings[0].rgbaColorMain = rgbaMainL1;
	sstate->df_layer_settings[0].rgbaColorOutline = rgbaOutlineL1;
	//skipping the maxDen parameter since the shader currently ignores it
	setVec3(sstate->df_layer_settings[0].densityRange, minDenL1, outlineDenL1, tightnessL1);


	if (sprite_effect == RdrSpriteEffect_DistField2Layer || sprite_effect == RdrSpriteEffect_DistField2LayerGradient)
	{
		setVec2(sstate->df_layer_settings[1].offset, uOffsetL2, vOffsetL2);
		sstate->df_layer_settings[1].rgbaColorMain = rgbaMainL2;
		sstate->df_layer_settings[1].rgbaColorOutline = rgbaOutlineL2;
		setVec3(sstate->df_layer_settings[1].densityRange, minDenL2, outlineDenL2, tightnessL2);
	}

	//copy over gradient settings
	if (sprite_effect == RdrSpriteEffect_DistField1LayerGradient || sprite_effect == RdrSpriteEffect_DistField2LayerGradient)
	{
		setVec2(sstate->df_grad_settings.startStopPts, topPt, bottomPt);
		sstate->df_grad_settings.rgbaTopColor = topRgba;
		sstate->df_grad_settings.rgbaBottomColor = bottomRgba;
	}

	glBox = clipperGetGLBox(clipper);
	if (glBox && !pMatrix)
	{
		sstate->scissor_x = (U16)MAXF(glBox->lx, 0);
		sstate->scissor_y = (U16)MAXF(glBox->ly, 0);
		sstate->scissor_width = (U16)MAXF(glBox->hx - glBox->lx, 0);
		sstate->scissor_height = (U16)MAXF(glBox->hy - glBox->ly, 0);
#ifndef SKIP_FONT_CPU_CLIPPING
		checkScissor(sstate);
#endif
		sstate->use_scissor = 1;
	}
	else
	{
		sstate->use_scissor = 0;
	}

	PERFINFO_AUTO_STOP_L3();

	gfxInsertSpriteListEntry(current_sprite_list, sprite, pProps ? pProps->is_3D : false);

	PERFINFO_AUTO_STOP_L3();

	return 1;
}

__forceinline static int create_sprite_triangle_ex(AtlasTex *atex1, BasicTexture *btex1, 
									 float x0, float y0, float x1, float y1, float x2, float y2,
									 float zp,
									 int rgba, int rgba2, int rgba3, 
									 float u0, float v0, float u1, float v1,
									 float u2, float v2, 
									 int additive, Clipper2D *clipper, 
									 RdrSpriteEffect sprite_effect, F32 effect_weight, bool b3D)
{
	int screen_width, screen_height;
	Vec2 bbox_ul, bbox_lr;
	CBox *glBox = clipperGetGLBox(clipper);
	GfxSpriteListEntry* sprite;
	RdrSpriteState* sstate;
	RdrSpriteVertex* sverts4;
	Vec2 sprite_ul, sprite_lr, sprite_p2;
	Mat3 *pMatrix;

	assert(sprite_effect < RdrSpriteEffect_DistField1Layer || sprite_effect > RdrSpriteEffect_DistField2LayerGradient);

	if ( gbNoGraphics )
		return 0;

	if (!atex1 && !btex1)
		return 0;

	if (atex1)
		btex1 = NULL;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	//if this triangle is ordered clockwise, sort it in CCW order
	if ( ( x1 - x0 ) * ( y2 - y1 ) - ( y1 - y0 ) * ( x2 - x1 ) < -0.001f )
	{
		SWAPF32(x1,x2);
		SWAPF32(y1,y2);
		SWAPF32(u1,u2);
		SWAPF32(v1,v2);
	}

	if (sprite_override_effect)
	{
		sprite_effect = sprite_override_effect;
		effect_weight = sprite_override_effect_weight;
		rgba = spriteEffectOverrideColor(rgba);
		rgba2 = spriteEffectOverrideColor(rgba2);
		rgba3 = spriteEffectOverrideColor(rgba3);
	}

	gfxGetActiveSurfaceSizeInline(&screen_width, &screen_height);

	// x positions.
	// y positions.  Flip the y axis for sprites
	setVec2(sprite_ul, x0, screen_height - y0);
	setVec2(sprite_lr, x1, screen_height - y1);
	setVec2(sprite_p2, x2, screen_height - y2);

	// bounding box

	bbox_ul[0] = MIN(x0, MIN(x1, x2));
	bbox_ul[1] = screen_height - MAX(y0, MAX(y1, y2));
	bbox_lr[0] = MAX(x0, MAX(x1, x2));
	bbox_lr[1] = screen_height - MIN(y0, MIN(y1, y2));

	// Test clipper rejection
	if (!clipperTestValuesGLSpace(clipper, bbox_ul, bbox_lr))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	sprite = gfxStartAddSpriteToList(current_sprite_list, zp, b3D, &sstate, &sverts4);

	sstate->bits_for_test = 0;
	sstate->sprite_effect_weight = effect_weight;
	sstate->additive = additive;
	sstate->sprite_effect = sprite_effect;
	sstate->is_triangle = 1;

    gfxSpriteListHookupTextures(current_sprite_list, sprite, atex1, btex1, 0, 0, b3D);

	spriteFromTriangleDispSprite(sverts4, atex1, btex1, rgba, rgba2, rgba3, u0, v0, u1, v1, u2, v2, sprite_ul, sprite_lr, sprite_p2);

	pMatrix = matrixStackGet(&eaSpriteMatrixStack);
	if (pMatrix)
	{
		gfxTransformAndClipVerticies(sverts4, pMatrix, clipper);
	}

	// Everything else.
	if (glBox)
	{
		sstate->scissor_x = (U16)MAXF(glBox->lx, 0);
		sstate->scissor_y = (U16)MAXF(glBox->ly, 0);
		sstate->scissor_width = (U16)MAXF(glBox->hx - glBox->lx, 0);
		sstate->scissor_height = (U16)MAXF(glBox->hy - glBox->ly, 0);
		checkScissor(sstate);
		sstate->use_scissor = 1;
	}
	else
	{
		sstate->use_scissor = 0;
	}

	gfxInsertSpriteListEntry(current_sprite_list, sprite, b3D);

	PERFINFO_AUTO_STOP();

	return 1;
}



__forceinline static void display_sprite_mask_boxes_rotated(	AtlasTex *atex, BasicTexture* btex, 
									   AtlasTex *amask, BasicTexture* bmask, 
									   CBox* texbox,
									   CBox* maskbox,
									   float zp,
									   int rgba,
									   float texangle )
{
	float tl[2], tr[2], br[2], bl[2];
	float utl, vtl, utr, vtr, ubr, vbr, ubl, vbl;
	float imw, imh, sw, sh;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// If this goes off, then we're queuing stuff inside of the loop in gfxDrawFrame,
	//  and instead the logic should probably be moved to gfxOncePerFramePerDevice().
	assertmsg(!g_no_sprites_allowed, "Creating a new sprite while in the middle of drawing a frame, will break NVPerfAPI stuff.");

	//if ( !CBoxIntersectsRotated( maskbox, texbox, texangle ) )
	//	return;

	//rotate the texture 
	CBoxRotate( texbox, texangle, tl, tr, br, bl );

	//calculate inverse width/height of mask
	imw = 1/CBoxWidth( maskbox );
	imh = 1/CBoxHeight( maskbox );

	//calculate mask uv coords
	utl = ( tl[0] - maskbox->lx ) * imw;
	vtl = ( tl[1] - maskbox->ly ) * imh;
	utr = ( tr[0] - maskbox->lx ) * imw;
	vtr = ( tr[1] - maskbox->ly ) * imh;
	ubr = ( br[0] - maskbox->lx ) * imw;
	vbr = ( br[1] - maskbox->ly ) * imh;
	ubl = ( bl[0] - maskbox->lx ) * imw;
	vbl = ( bl[1] - maskbox->ly ) * imh;

	if ( atex )
	{
		sw = CBoxWidth( texbox )/(float)atex->width;
		sh = CBoxHeight( texbox )/(float)atex->height;
	}
	else if ( btex )
	{
		sw = CBoxWidth( texbox )/(float)btex->width;
		sh = CBoxHeight( texbox )/(float)btex->height;
	}
	else
	{
		return; //no texture!
	}

	create_sprite_ex(		atex, btex, 
		amask, bmask, 
		texbox->lx, texbox->ly, zp,				//positon
		sw, sh,									//scale
		rgba, rgba, rgba, rgba,					//rgba
		UVS(0, 0, 1, 1),						//uvs
		utl, vtl, utr, vtr, ubr, vbr, ubl, vbl, //uvs			
		texangle, 0, clipperGetCurrent(),		//angle,additive,clip
		(amask || bmask) ? RdrSpriteEffect_TwoTex : 0, 0, SPRITE_2D); //effect, weight
	PERFINFO_AUTO_STOP();
}

__forceinline static void display_sprite_mask_boxes(	AtlasTex *atex, BasicTexture* btex, 
							   AtlasTex *amask, BasicTexture* bmask, 
							   CBox* spritebox,
							   CBox* maskbox,
							   float zp,
							   int rgba )
{
	CBox ibox;
	float u1,v1,u2,v2,u3,v3,u4,v4;
	float isx,isy;	
	float inv_width1, inv_width2, inv_height1, inv_height2;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// If this goes off, then we're queuing stuff inside of the loop in gfxDrawFrame,
	//  and instead the logic should probably be moved to gfxOncePerFramePerDevice().
	assertmsg(!g_no_sprites_allowed, "Creating a new sprite while in the middle of drawing a frame, will break NVPerfAPI stuff.");

	BuildCBox(&ibox, 0, 0, 1, 1 ); //because the compiler complained about unitialized memory

	if ( CBoxIntersectClip( spritebox, maskbox, &ibox ) == 0 ) return; //must intersect

	inv_width1 = 1 / ( spritebox->hx - spritebox->lx );  //opt: precache denoms
	inv_width2 = 1 / ( maskbox->hx - maskbox->lx );
	inv_height1 = 1 / ( spritebox->hy - spritebox->ly );
	inv_height2 = 1 / ( maskbox->hy - maskbox->ly );

	u1 = ( ibox.lx - spritebox->lx ) * inv_width1; 
	v1 = ( ibox.ly - spritebox->ly ) * inv_height1;

	u2 = ( ibox.hx - spritebox->lx ) * inv_width1;
	v2 = ( ibox.hy - spritebox->ly ) * inv_height1;

	u3 = ( ibox.lx - maskbox->lx ) * inv_width2;
	v3 = ( ibox.ly - maskbox->ly ) * inv_height2;

	u4 = ( ibox.hx - maskbox->lx ) * inv_width2;
	v4 = ( ibox.hy - maskbox->ly ) * inv_height2;


	if ( atex )
	{
		isx = ( ibox.right - ibox.left ) / (float)atex->width;
		isy = ( ibox.bottom - ibox.top ) / (float)atex->height;
	}
	else if ( btex )
	{
		isx = ( ibox.right - ibox.left ) / (float)btex->width;
		isy = ( ibox.bottom - ibox.top ) / (float)btex->height;
	}
	else
	{
		return; //no texture!
	}

	create_sprite_ex(	atex, btex, 
		amask, bmask, 
		ibox.lx, ibox.ly, zp,			//positon
		isx, isy,						//scale
		rgba, rgba, rgba, rgba,			//rgba
		UVS(u1, v1, u2, v2),			//uvs
		UVS(u3, v3, u4, v4),			//uvs
		0, 0, clipperGetCurrent(),		//angle,additive,clip
		(amask || bmask) ? RdrSpriteEffect_TwoTex : 0, 0, SPRITE_2D);		//effect, weight

	PERFINFO_AUTO_STOP();
}


__forceinline static void display_sprite_mask_ex(	AtlasTex *atex, BasicTexture* btex, 
							AtlasTex *amask, BasicTexture* bmask, 
							float x1, float y1, float xscale1, float yscale1,
							float x2, float y2, float xscale2, float yscale2,
							float zp,
							int rgba )
{
	CBox box1, box2, ibox;
	float u1,v1,u2,v2,u3,v3,u4,v4;
	float sw1, sh1, sw2, sh2;		//sprite_width(sw), sprite_height(sh)
	float isx,isy;					//intersection box scale x,y
	float inv_width1, inv_width2, inv_height1, inv_height2;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// If this goes off, then we're queuing stuff inside of the loop in gfxDrawFrame,
	//  and instead the logic should probably be moved to gfxOncePerFramePerDevice().
	assertmsg(!g_no_sprites_allowed, "Creating a new sprite while in the middle of drawing a frame, will break NVPerfAPI stuff.");

	// must have valid scales!
	if ( xscale1 <= 0 && yscale1 <= 0 || xscale2 <= 0 || yscale2 <= 0 ) return;

	if (atex)
	{
		sw1 = (float)atex->width;
		sh1 = (float)atex->height;
		btex = NULL;
	}
	else if (btex)
	{
		sw1 = (float)btex->width;
		sh1 = (float)btex->height;
	}
	else
	{
		return; //no texture!
	}

	if ( amask )
	{
		sw2 = (float)amask->width;
		sh2 = (float)amask->height;
		bmask = NULL;
	}
	else if ( bmask )
	{
		sw2 = (float)bmask->width;
		sh2 = (float)bmask->height;
	}
	else
	{
		sw2 = 0;
		sh2 = 0;
	}

	BuildCBox(&box1,x1,y1,sw1*xscale1,sh1*yscale1);
	BuildCBox(&box2,x2,y2,sw2*xscale2,sh2*yscale2);
	BuildCBox(&ibox, 0, 0, 1, 1 );

	if ( CBoxIntersectClip( &box1, &box2, &ibox ) == 0 ) return; //must intersect

	inv_width1 = 1 / ( box1.hx - box1.lx ); //opt: precache denoms
	inv_width2 = 1 / ( box2.hx - box2.lx );
	inv_height1 = 1 / ( box1.hy - box1.ly );
	inv_height2 = 1 / ( box2.hy - box2.ly );

	u1 = ( ibox.lx - box1.lx ) * inv_width1;
	v1 = ( ibox.ly - box1.ly ) * inv_height1;

	u2 = ( ibox.hx - box1.lx ) * inv_width1;
	v2 = ( ibox.hy - box1.ly ) * inv_height1;

	u3 = ( ibox.lx - box2.lx ) * inv_width2;
	v3 = ( ibox.ly - box2.ly ) * inv_height2;

	u4 = ( ibox.hx - box2.lx ) * inv_width2;
	v4 = ( ibox.hy - box2.ly ) * inv_height2;

	isx = ( ibox.hx - ibox.lx ) / ( sw1 );
	isy = ( ibox.hy - ibox.ly ) / ( sh1 );

	create_sprite_ex(	atex, btex, 
		amask, bmask, 
		ibox.lx, ibox.ly, zp,			//positon
		isx, isy,						//scale
		rgba, rgba, rgba, rgba,			//rgba
		UVS(u1, v1, u2, v2),			//uvs
		UVS(u3, v3, u4, v4),			//uvs
		0, 0, clipperGetCurrent(),	//angle,additive,clip
		(amask || bmask) ? RdrSpriteEffect_TwoTex : 0, 0, SPRITE_2D);		//effect, weight


	PERFINFO_AUTO_STOP();
}

__forceinline static void display_sprite_ex_inline(AtlasTex *atex1, BasicTexture *btex1, 
					   AtlasTex *atex2, BasicTexture *btex2, 
					   float xp, float yp, float zp, 
					   float xscale, float yscale, 
					   int rgba, int rgba2, int rgba3, int rgba4, 
					   float u1, float v1, float u2, float v2,
					   float u3, float v3, float u4, float v4,
					   float angle, int additive, Clipper2D *clipper, bool is_3d)
{
	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);

	// If this goes off, then we're queuing stuff inside of the loop in gfxDrawFrame,
	//  and instead the logic should probably be moved to gfxOncePerFramePerDevice().
	assertmsg(!g_no_sprites_allowed, "Creating a new sprite while in the middle of drawing a frame, will break NVPerfAPI stuff.");

	create_sprite_ex(
		atex1, btex1, 
		atex2, btex2, 
		xp, yp, zp, 
		xscale, yscale, 
		rgba, rgba2, rgba3, rgba4, 
		UVS(u1, v1, u2, v2), 
		UVS(u3, v3, u4, v4), 
		angle, additive, clipper, 
		RdrSpriteEffect_None, 0, is_3d ? SPRITE_3D | SPRITE_IGNORE_Z_TEST : 0);

	PERFINFO_AUTO_STOP_L2();
}

void display_sprite_ex(AtlasTex *atex1, BasicTexture *btex1, 
	AtlasTex *atex2, BasicTexture *btex2, 
	float xp, float yp, float zp, 
	float xscale, float yscale, 
	int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2,
	float u3, float v3, float u4, float v4,
	float angle, int additive, Clipper2D *clipper);

void display_sprite_3d_ex(AtlasTex *atex1, BasicTexture *btex1, 
	AtlasTex *atex2, BasicTexture *btex2, 
	float xp, float yp, float zp, 
	float xscale, float yscale, 
	int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2,
	float u3, float v3, float u4, float v4,
	float angle, int additive, Clipper2D *clipper);

__forceinline static void display_sprite_effect_ex(AtlasTex *atex, BasicTexture *btex, AtlasTex *atex2, BasicTexture *btex2, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, float u1, float v1, float u2, float v2, float u3, float v3, float u4, float v4, float angle, int additive, Clipper2D *clipper, RdrSpriteEffect sprite_effect, F32 effect_weight, SpriteFlags sprite_flags)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	create_sprite_ex(		atex, btex, 
		atex2, btex2, 
		xp, yp, zp, 
		xscale, yscale, 
		rgba, rgba2, rgba3, rgba4, 
		UVS(u1, v1, u2, v2), 
		UVS(u3, v3, u4, v4),
		angle, additive, clipper, 
		sprite_effect, effect_weight, sprite_flags);
	PERFINFO_AUTO_STOP();
}

__forceinline static void display_sprite_triangle(AtlasTex *atex, BasicTexture *btex, 
							 float x0, float y0, float x1, float y1, float x2, float y2,
							 float zp,
							 int rgba, int rgba2, int rgba3, 
							 float u0, float v0, float u1, float v1,
							 float u2, float v2,
							 int additive, Clipper2D *clipper, 
							 RdrSpriteEffect sprite_effect, F32 effect_weight,
							 bool b3D)
{

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	create_sprite_triangle_ex(	
		atex, btex, 
		x0, y0, x1, y1, x2, y2,
		zp,
		rgba, rgba2, rgba3,
		u0, v0, u1, v1, 
		u2, v2,
		additive, clipper, 
		sprite_effect, effect_weight, b3D);


	PERFINFO_AUTO_STOP();
}

//We actually create quads instead of using real lines
__forceinline static void display_line_as_sprite_ex(F32 x1, F32 y1, F32 zp, F32 x2, F32 y2, int rgba, int rgba2, F32 width, bool antialiased, bool additive, Clipper2D *clipper)
{

	Vec2 normal, tangent;
	int screen_width, screen_height;
	Vec2 sprite_pts[4];	
	Vec2 bbox_ul, bbox_lr;
	CBox *glBox = clipperGetGLBox(clipper);
	GfxSpriteListEntry* sprite;
	RdrSpriteState* sstate;
	RdrSpriteVertex* sverts4;
	Mat3 *pMatrix;

	if ( gbNoGraphics )
		return;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (antialiased)
	{
		gfxSetupSpriteLineTexture();
		MAX1F(width,2.0f);
	}

	gfxGetActiveSurfaceSizeInline(&screen_width, &screen_height);

	//flip the sprite
	y1 = screen_height - y1;
	y2 = screen_height - y2;

	//we need to make sure we're on exact pixels or else the quds get shifted and sometimes disapper for 1px lines
	x1 = round(x1); x2 = round(x2);
	y1 = round(y1); y2 = round(y2);


	setVec2(tangent, x2-x1, y2-y1);
	normalVec2(tangent);
	scaleVec2(tangent, width/4.0f, tangent);

	//Get the normal of the line
	setVec2(normal, y1-y2, x2-x1);
	normalVec2(normal);
	scaleVec2(normal, width/2.0f, normal);

	//extend the ends slightly so a box's coners will be filled
	x1 -= tangent[0];
	x2 += tangent[0];

	y1 -= tangent[1];
	y2 += tangent[1];

	//make sure we actually have at least one pixel
	if (normal[0] >= 0)
	{
		MAX1F(normal[0], 0.5);	
	}
	else
	{
		MIN1F(normal[0], -0.5);	
	}

	if (normal[1] >= 0)
	{
		MAX1F(normal[1], 0.5);	
	}
	else
	{
		MIN1F(normal[1], -0.5);	
	}

	setVec2(sprite_pts[0], x1 - normal[0], y1 - normal[1]);
	setVec2(sprite_pts[1], x1 + normal[0], y1 + normal[1]);
	setVec2(sprite_pts[2], x2 + normal[0], y2 + normal[1]);
	setVec2(sprite_pts[3], x2 - normal[0], y2 - normal[1]);

	bbox_ul[0] = sprite_pts[0][0]; bbox_ul[1] = sprite_pts[0][1];
	bbox_lr[0] = sprite_pts[0][0]; bbox_lr[1] = sprite_pts[0][1];
	MIN1F(bbox_ul[0], sprite_pts[1][0]); MAX1F(bbox_ul[1], sprite_pts[1][1]);
	MAX1F(bbox_lr[0], sprite_pts[1][0]); MIN1F(bbox_lr[1], sprite_pts[1][1]);
	MIN1F(bbox_ul[0], sprite_pts[2][0]); MAX1F(bbox_ul[1], sprite_pts[2][1]);
	MAX1F(bbox_lr[0], sprite_pts[2][0]); MIN1F(bbox_lr[1], sprite_pts[2][1]);
	MIN1F(bbox_ul[0], sprite_pts[3][0]); MAX1F(bbox_ul[1], sprite_pts[3][1]);
	MAX1F(bbox_lr[0], sprite_pts[3][0]); MIN1F(bbox_lr[1], sprite_pts[3][1]);

	// Test clipper rejection
	if (!clipperTestValuesGLSpace(clipper, bbox_ul, bbox_lr))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	sprite = gfxStartAddSpriteToList(current_sprite_list, zp, false, &sstate, &sverts4);

	sstate->bits_for_test = 0;
	sstate->sprite_effect_weight = 0;
	sstate->additive = additive;
	sstate->sprite_effect = RdrSpriteEffect_None;

    gfxSpriteListHookupTextures(current_sprite_list, sprite, 0, antialiased ? sprite_line_tex : white_tex, 0, 0, false);

	sverts4[0].point[0] = sprite_pts[0][0];
	sverts4[0].point[1] = sprite_pts[0][1];

	sverts4[1].point[0] = sprite_pts[1][0];
	sverts4[1].point[1] = sprite_pts[1][1];

	sverts4[2].point[0] = sprite_pts[2][0];
	sverts4[2].point[1] = sprite_pts[2][1];

	sverts4[3].point[0] = sprite_pts[3][0];
	sverts4[3].point[1] = sprite_pts[3][1];

	if (!antialiased)
	{
		setVec2(sverts4[0].texcoords, 0, 0);
		setVec2(sverts4[1].texcoords, 0, 0);
		setVec2(sverts4[2].texcoords, 0, 0);
		setVec2(sverts4[3].texcoords, 0, 0);
	}
	else
	{
		setVec2(sverts4[0].texcoords, 0, 0);
		setVec2(sverts4[1].texcoords, 0, 1);
		setVec2(sverts4[2].texcoords, 0, 1);
		setVec2(sverts4[3].texcoords, 0, 0);
	}

	setSpriteVertColorFromRGBA(&sverts4[0], rgba);
	setSpriteVertColorFromRGBA(&sverts4[1], rgba);
	setSpriteVertColorFromRGBA(&sverts4[2], rgba2);
	setSpriteVertColorFromRGBA(&sverts4[3], rgba2);

	pMatrix = matrixStackGet(&eaSpriteMatrixStack);
	if (pMatrix)
	{
		gfxTransformAndClipVerticies(sverts4, pMatrix, clipper);
	}

	// Everything else.
	if (glBox && !pMatrix)
	{
		sstate->scissor_x = (U16)MAXF(glBox->lx, 0);
		sstate->scissor_y = (U16)MAXF(glBox->ly, 0);
		sstate->scissor_width = (U16)MAXF(glBox->hx - glBox->lx, 0);
		sstate->scissor_height = (U16)MAXF(glBox->hy - glBox->ly, 0);
		checkScissor(sstate);
		sstate->use_scissor = 1;
	}
	else
	{
		sstate->use_scissor = 0;
	}

	gfxInsertSpriteListEntry(current_sprite_list, sprite, false);

	PERFINFO_AUTO_STOP();
}

__forceinline static void display_sprite_distance_field_one_layer(BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, float u1, float v1, float u2, float v2, float angle, float skew, int additive, Clipper2D *clipper, float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, SpriteProperties *pProps)
{
	PERFINFO_AUTO_START_L3(__FUNCTION__, 1);


	create_df_sprite_ex(
		btex, xp, yp, zp,xscale, yscale,
		rgba, rgba2, rgba3, rgba4,
		0, 0, 0, 0,
		u1, v1, u2, v1, u2, v2, u1, v2,
		angle, skew, additive, clipper, 
		uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1,
		0, 0, 0, 0, 0, 0, 0, 0,
		RdrSpriteEffect_DistField1Layer, pProps);

	PERFINFO_AUTO_STOP_L3();
}

__forceinline static void display_sprite_distance_field_two_layers(BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, float u1, float v1, float u2, float v2, float angle, float skew, int additive, Clipper2D *clipper, float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2, int rgbaMainL2, int rgbaOutlineL2, SpriteProperties *pProps)
{
	PERFINFO_AUTO_START_L3(__FUNCTION__, 1);

	create_df_sprite_ex(btex, xp, yp, zp,xscale, yscale,
		rgba, rgba2, rgba3, rgba4,
		0, 0, 0, 0,
		u1, v1, u2, v1, u2, v2, u1, v2,
		angle, skew, additive, clipper, 
		uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1,
		uOffsetL2, vOffsetL2, minDenL2, outlineDenL2, maxDenL2, tightnessL2, rgbaMainL2, rgbaOutlineL2,
		RdrSpriteEffect_DistField2Layer, pProps);

	PERFINFO_AUTO_STOP_L3();
}

__forceinline static void display_sprite_distance_field_one_layer_gradient( BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int topRgba, int bottomRgba, float topPt, float bottomPt, float u1, float v1, float u2, float v2, float angle, float skew, int additive, Clipper2D *clipper, float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, SpriteProperties *pProps)
{
	Color topLeft = {0, 0, 0, 0xFF};
	Color topRight = {0, 0xFF, 0, 0xFF};
	Color bottomRight = {0xFF, 0xFF, 0, 0xFF};
	Color bottomLeft = {0XFF, 0, 0, 0xFF};

	PERFINFO_AUTO_START_L3(__FUNCTION__, 1);

	create_df_sprite_ex(
		btex, xp, yp, zp,xscale, yscale,
		RGBAFromColor(topLeft), RGBAFromColor(topRight), RGBAFromColor(bottomRight), RGBAFromColor(bottomLeft), //goes clockwise from top left
		topRgba,bottomRgba, topPt, bottomPt,
		u1, v1, u2, v1, u2, v2, u1, v2,
		angle, skew, additive, clipper, 
		uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1,
		0, 0, 0, 0, 0, 0, 0, 0,
		RdrSpriteEffect_DistField1LayerGradient, pProps);

	PERFINFO_AUTO_STOP_L3();
}

__forceinline static void display_sprite_distance_field_two_layers_gradient( BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int topRgba, int bottomRgba, float topPt, float bottomPt, float u1, float v1, float u2, float v2, float angle, float skew, int additive, Clipper2D *clipper, float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2, int rgbaMainL2, int rgbaOutlineL2, SpriteProperties *pProps)
{
	Color topLeft = {0, 0, 0, 0xFF};
	Color topRight = {0, 0xFF, 0, 0xFF};
	Color bottomRight = {0xFF, 0xFF, 0, 0xFF};
	Color bottomLeft = {0XFF, 0, 0, 0xFF};


	PERFINFO_AUTO_START_L3(__FUNCTION__, 1);

	create_df_sprite_ex(
		btex, xp, yp, zp,xscale, yscale,
		RGBAFromColor(topLeft), RGBAFromColor(topRight), RGBAFromColor(bottomRight), RGBAFromColor(bottomLeft), //goes clockwise from top left
		topRgba,bottomRgba, topPt, bottomPt,
		u1, v1, u2, v1, u2, v2, u1, v2,
		angle, skew, additive, clipper, 
		uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1,
		uOffsetL2, vOffsetL2, minDenL2, outlineDenL2, maxDenL2, tightnessL2, rgbaMainL2, rgbaOutlineL2,
		RdrSpriteEffect_DistField2LayerGradient, pProps);

	PERFINFO_AUTO_STOP_L3();
}

__forceinline static void display_sprite_box(AtlasTex * spr, const CBox *box, float zp, int rgba)
{
	display_sprite_ex(	spr, NULL, NULL, NULL, 
		box->left, box->top, zp, 
		(box->right-box->left)/spr->width, (box->bottom-box->top)/spr->height, 
		rgba, rgba, rgba, rgba, 
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent());
}

__forceinline static void display_3D_sprite_box(AtlasTex * spr, const CBox *box, float zp, int rgba)
{
	display_sprite_3d_ex(	spr, NULL, NULL, NULL, 
		box->left, box->top, zp, 
		(box->right-box->left)/spr->width, (box->bottom-box->top)/spr->height, 
		rgba, rgba, rgba, rgba, 
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_box_mask(AtlasTex * spr, AtlasTex * mask, CBox *box, float zp, int rgba)
{
	display_sprite_effect_ex(spr, NULL, mask, NULL, 
		box->left, box->top, zp, 
		(box->right-box->left)/spr->width, (box->bottom-box->top)/spr->height, 
		rgba, rgba, rgba, rgba, 
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent(),
		mask ? RdrSpriteEffect_TwoTex : 0, 0, SPRITE_2D);
}

__forceinline static void display_sprite_box2(BasicTexture *spr, CBox *box, float zp, int rgba)
{
	display_sprite_ex(	NULL, spr, NULL, NULL, 
		box->left, box->top, zp, 
		(box->right-box->left)/spr->width, (box->bottom-box->top)/spr->height, 
		rgba, rgba, rgba, rgba, 
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_rotated(AtlasTex *spr, float centerx, float centery, float rotation, float zp, float scale, int rgba)
{
	display_sprite_ex(spr, NULL, NULL, NULL, 
		centerx - spr->width * scale / 2,
		centery - spr->height * scale / 2,
		zp, scale, scale, 
		rgba, rgba, rgba, rgba, 
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		rotation, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_tiled_box(BasicTexture *tex, const CBox *box, float z, int rgba)
{
	display_sprite_ex(	NULL, tex, NULL, NULL, 
		box->lx, box->ly, z, 
		(CBoxWidth(box)/texWidth(tex)), (CBoxHeight(box) / texHeight(tex)), 
		rgba, rgba, rgba, rgba, 
		0, 0, CBoxWidth(box)/texWidth(tex), CBoxHeight(box)/texHeight(tex), 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_tiled_box_scaled(BasicTexture *tex, const CBox *box, float z, int rgba, float fScaleX, float fScaleY)
{
	display_sprite_ex(	NULL, tex, NULL, NULL, 
		box->lx, box->ly, z, 
		(CBoxWidth(box)/texWidth(tex)), (CBoxHeight(box) / texHeight(tex)), 
		rgba, rgba, rgba, rgba, 
		0, 0, CBoxWidth(box)/(texWidth(tex) * fScaleX), CBoxHeight(box)/(texHeight(tex) * fScaleY), 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent());
}

__forceinline static CBox display_sprite_create_cbox_for_rot(const CBox* pBox, float centerX, float centerY, float rot)
{
	// Do the rotation by translate center to origin, rotate, translate center to center
	CBox box = *pBox;
	float width = CBoxWidth( pBox );
	float height = CBoxHeight( pBox );
	
	// untranslate
	box.lx -= centerX;
	box.ly -= centerY;
	box.hx -= centerX;
	box.hy -= centerY;

	// rotate
	rotateXZ( rot, &box.lx, &box.ly );
	rotateXZ( rot, &box.hx, &box.hy );

	// translate
	box.lx += centerX;
	box.ly += centerY;
	box.hx += centerX;
	box.hy += centerY;

	// okay, now make it not rotated
	{
		float newX;
		float newY;
		CBoxGetCenter( &box, &newX, &newY );
		BuildCBox( &box, newX - width / 2, newY - height / 2, width, height );
	}

	return box;
}

__forceinline static void display_sprite_tiled_box_4Color_scaled_rot(BasicTexture *tex, CBox *pBox, float z, int rgba1, int rgba2, int rgba3, int rgba4, float fScaleX, float fScaleY, F32 centerX, F32 centerY, F32 rot)
{
	CBox box;
	if( rot ) {
		box = display_sprite_create_cbox_for_rot(pBox, centerX, centerY, rot);
	} else {
		box = *pBox;
	}
	
	display_sprite_ex(	NULL, tex, NULL, NULL, 
		box.lx, box.ly, z, 
		(CBoxWidth(&box)/texWidth(tex)), (CBoxHeight(&box) / texHeight(tex)), 
		rgba1, rgba2, rgba3, rgba4, 
		0, 0, CBoxWidth(&box)/(texWidth(tex)*fScaleX), CBoxHeight(&box)/(texHeight(tex)*fScaleY), 
		0, 0, 1, 1, 
		rot, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_tiled_box_4Color(BasicTexture *tex, CBox *box, float z, int rgba1, int rgba2, int rgba3, int rgba4)
{
	display_sprite_ex(	NULL, tex, NULL, NULL, 
		box->lx, box->ly, z, 
		(CBoxWidth(box)/texWidth(tex)), (CBoxHeight(box) / texHeight(tex)), 
		rgba1, rgba2, rgba3, rgba4, 
		0, 0, CBoxWidth(box)/texWidth(tex), CBoxHeight(box)/texHeight(tex), 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_tiled_line(BasicTexture *tex, Vec2 point1, Vec2 point2, float z, float scaleX, float scaleY, int rgba)
{
	F32 angle = atan2(point2[1]-point1[1], point2[0]-point1[0]);
	F32 length = sqrtf(SQR(point2[0]-point1[0])+SQR(point2[1]-point1[1]));
	F32 length_scale = length / texWidth(tex);
	display_sprite_ex(	NULL, tex, NULL, NULL, 
		(point1[0]+point2[0])*0.5f - length*0.5f, (point1[1]+point2[1])*0.5f, z, 
		length_scale, scaleY, 
		rgba, rgba, rgba, rgba, 
		0, 0, length_scale / scaleX, 1, 
		0, 0, 1, 1, 
		angle, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_box_additive(AtlasTex * spr, CBox *box, float zp, int rgba)
{
	display_sprite_ex(	spr, NULL, NULL, NULL, 
		box->left, box->top, zp, 
		(box->right-box->left)/spr->width, (box->bottom-box->top)/spr->height, 
		rgba, rgba, rgba, rgba, 
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		0, 1, clipperGetCurrent());
}

__forceinline static void display_sprite_box_4Color_rot(AtlasTex * spr, CBox *pBox, float zp, int rgba1, int rgba2, int rgba3, int rgba4, F32 centerX, F32 centerY, F32 rot)
{
	CBox box;
	if( rot ) {
		box = display_sprite_create_cbox_for_rot(pBox, centerX, centerY, rot);
	} else {
		box = *pBox;
	}
	display_sprite_ex(	spr, NULL, NULL, NULL, 
		box.left, box.top, zp, 
		(box.right-box.left)/spr->width, (box.bottom-box.top)/spr->height, 
		rgba1, rgba2, rgba3, rgba4, 
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		rot, 0, clipperGetCurrent());
}

//
//
__forceinline static int interp(	float vec,			//always 0 - 1.0f
				  unsigned int src,   //always less than dest.
				  unsigned int dest, unsigned int mask, int shift)
{
	U32 delta;

	if ((src & mask) > (dest & mask))
		delta = (src & mask) - (dest & mask);
	else
		delta = (dest & mask) - (src & mask);

	delta = delta >> shift;
	delta = (U32) ((float) delta * vec);
	delta = (delta & 0xff) << shift;

	if ((src & mask) > (dest & mask))
		src = ((src - delta) & mask);
	else
		src = ((src + delta) & mask);

	return src;
}

//
//
__forceinline static void interp_rgba(float vec, int src, int dest, int *res)
{
	*res = interp(vec, src, dest, 0xff000000, 24)
		| interp(vec, src, dest, 0x00ff0000, 16)
		| interp(vec, src, dest, 0x0000ff00, 8)
		| interp(vec, src, dest, 0x000000ff, 0);
}

// convenience functions:



__forceinline static void display_sprite_tex(BasicTexture *tex, float xp, float yp, float zp, float xscale, float yscale, int rgba)
{
	display_sprite_ex(	NULL, tex, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						rgba, rgba, rgba, rgba, 
						0, 0, 1, 1, 
						0, 0, 1, 1, 
						0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_effect_tex(BasicTexture *tex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int additive, RdrSpriteEffect sprite_effect, F32 effect_weight)
{
	display_sprite_effect_ex(	NULL, tex, 
								NULL, NULL,
								xp, yp, zp, 
								xscale, yscale, 
								rgba, rgba, rgba, rgba, 
								0, 0, 1, 1, 
								0, 0, 1, 1, 
								0, additive, clipperGetCurrent(), 
								sprite_effect, effect_weight,
								SPRITE_2D);
}

__forceinline static void display_sprite_tex_UV(BasicTexture *tex, float xp, float yp, float zp, float xscale, float yscale, int rgba, float left, float top, float right, float bottom)
{
	display_sprite_ex(	NULL, tex, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						rgba, rgba, rgba, rgba, 
						left, top, right, bottom, 
						0, 0, 1, 1, 
						0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite(AtlasTex * spr, float xp, float yp, float zp, float xscale, float yscale, int rgba)
{
	display_sprite_ex(	spr, NULL, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						rgba, rgba, rgba, rgba, 
						0, 0, 1, 1, 
						0, 0, 1, 1, 
						0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_effect(AtlasTex * spr, float xp, float yp, float zp, float xscale, float yscale, int rgba, RdrSpriteEffect sprite_effect, F32 effect_weight)
{
	display_sprite_effect_ex(	spr, NULL, 
								NULL, NULL,
								xp, yp, zp, 
								xscale, yscale, 
								rgba, rgba, rgba, rgba, 
								0, 0, 1, 1, 
								0, 0, 1, 1, 
								0, 0, clipperGetCurrent(), 
								sprite_effect, effect_weight,
								SPRITE_2D);
}

__forceinline static void display_sprite_rotated_ex(AtlasTex * aspr, BasicTexture *bspr, float xp, float yp, float zp, float xscale, float yscale, int rgba, float angle, int additive)
{
	display_sprite_ex(	aspr, bspr, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						rgba, rgba, rgba, rgba, 
						0, 0, 1, 1, 
						0, 0, 1, 1, 
						angle, additive, clipperGetCurrent());
}

__forceinline static void display_sprite_4Color(AtlasTex * spr, float xp, float yp, float zp, float xscale, float yscale, int rgba1, int rgba2, int rgba3, int rgba4, int additive)
{
	display_sprite_ex(	spr, NULL, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						rgba1, rgba2, rgba3, rgba4, 
						0, 0, 1, 1, 
						0, 0, 1, 1, 
						0, additive, clipperGetCurrent());
}

__forceinline static void display_sprite_blend(AtlasTex * spr, float xp, float yp, float zp, float xscale, float yscale, int color1, int color2)
{
	display_sprite_ex(	spr, NULL, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						color1, color2, 0, 0, 
						0, 0, 1, 1, 
						0, 0, 1, 1, 
						0, 0, clipperGetCurrent());
}

__forceinline static void display_spriteUV(AtlasTex * spr, float xp, float yp, float zp, float xscale, float yscale, int rgba, float left, float top, float right, float bottom)
{
	display_sprite_ex(	spr, NULL, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						rgba, rgba, rgba, rgba, 
						left, top, right, bottom, 
						0, 0, 1, 1, 
						0, 0, clipperGetCurrent());
}

__forceinline static void display_sprite_UV_4Color(AtlasTex * spr, float xp, float yp, float zp, float xscale, float yscale, int rgba1, int rgba2, int rgba3, int rgba4, float left, float top, float right, float bottom)
{
	display_sprite_ex(	spr, NULL, NULL, NULL, 
						xp, yp, zp, 
						xscale, yscale, 
						rgba1, rgba2, rgba3, rgba4, 
						left, top, right, bottom, 
						0, 0, 1, 1, 
						0, 0, clipperGetCurrent());
}

#undef UVS

#endif
