 /***************************************************************************
 
 
 
 ***************************************************************************/

#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "GfxSpriteList.h"
#include "GfxTexAtlas.h"
#include "GfxTexOpts.h"
#include "GfxTextures.h"
#include "GfxTexturesInline.h"
#include "GraphicsLib.h"
#include "utils.h"

// Any changes made to this include must be made with the corresponding include in GfxSprite.h
// Only one is ever expected to be active at a time depending on solution configuration.
#ifdef _FULLDEBUG
#include "GfxSprite.inl"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

int force_disable_font_scaling;
bool g_no_sprites_allowed;
GfxSpriteList* current_sprite_list = 0;
BasicTexture* sprite_line_tex = 0;
TransformationMatrix **eaSpriteMatrixStack = NULL;

RdrSpriteEffect sprite_override_effect;
U32 sprite_override_effect_color;
F32 sprite_override_effect_weight;

// Overrides the rendering effects on all sprites
AUTO_COMMAND ACMD_CMDLINE;
void spriteOverrideEffect(char *effect_str ACMD_NAMELIST(RdrSpriteEffectEnum, STATICDEFINE), Vec3 color, F32 weight)
{
	Color cTemp;
	RdrSpriteEffect effect = StaticDefineIntGetInt(RdrSpriteEffectEnum, effect_str);
	sprite_override_effect = effect;
	vec3ToColor(&cTemp, color);
	sprite_override_effect_color = RGBAFromColor(cTemp);
	sprite_override_effect_weight = weight;
}

U32 spriteEffectOverrideColor(U32 rgba)
{
	Color c = colorFromRGBA(rgba);
	Color co = colorFromRGBA(sprite_override_effect_color);
	U32 desaturated;
	U32 outcolor;
	F32 dot = (c.r * 0.299f + 
		c.g * 0.587f +
		c.b * 0.114f)*U8TOF32_COLOR;
	co.r*=dot;
	co.g*=dot;
	co.b*=dot;
	desaturated = RGBAFromColor(co);
	outcolor = lerpRGBAColors(rgba, desaturated, sprite_override_effect_weight*sprite_override_effect_weight);
	return (rgba & 0xFF) | (outcolor&0xFFFFFF00);
}

void display_sprite_ex(AtlasTex *atex1, BasicTexture *btex1, 
	AtlasTex *atex2, BasicTexture *btex2, 
	float xp, float yp, float zp, 
	float xscale, float yscale, 
	int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2,
	float u3, float v3, float u4, float v4,
	float angle, int additive, Clipper2D *clipper)
{
	display_sprite_ex_inline(atex1, btex1, 
		atex2, btex2, 
		xp, yp, zp, 
		xscale, yscale, 
		rgba, rgba2, rgba3, rgba4, 
		u1, v1, u2, v2,
		u3, v3, u4, v4,
		angle, additive, clipper, false);
}

void display_sprite_3d_ex(AtlasTex *atex1, BasicTexture *btex1, 
	AtlasTex *atex2, BasicTexture *btex2, 
	float xp, float yp, float zp, 
	float xscale, float yscale, 
	int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2,
	float u3, float v3, float u4, float v4,
	float angle, int additive, Clipper2D *clipper)
{
	display_sprite_ex_inline(atex1, btex1, 
		atex2, btex2, 
		xp, yp, zp, 
		xscale, yscale, 
		rgba, rgba2, rgba3, rgba4, 
		u1, v1, u2, v2,
		u3, v3, u4, v4,
		angle, additive, clipper, true);
}

// IF YOU CHANGE THIS FUNCTION, YOU SHOULD CHANGE DrawNinePatch AND ui_GenDrawNinePatchAtlas
void display_sprite_NinePatch_test(AtlasTex *spr, float xp, float yp, float zp, float width, float height, int rgba)
{
	// Test version of this code.  In general, don't want to be calling texGetNinePatch per sprite.
	const NinePatch *np = texGetNinePatch(spr->name);
	if (!np)
		display_sprite_ex(spr, NULL, NULL, NULL,
		xp, yp, zp,
		width/spr->width, height/spr->height,
		rgba, rgba, rgba, rgba,
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		0, 0, clipperGetCurrent());
	else {
		float sizeX[3] = {np->stretchableX[0], -1, spr->width - np->stretchableX[1] - 1};
		float sizeY[3] = {np->stretchableY[0], -1, spr->height - np->stretchableY[1] - 1};
		float scX[3];
		float scY[3];
		float u[2], v[2];
		float udelta = 0.5f/spr->width;
		float vdelta = 0.5f/spr->height;
		sizeX[1] = width - sizeX[2] - sizeX[0];
		sizeY[1] = height - sizeY[2] - sizeY[0];
		MAX1F(sizeX[1], 0);
		MAX1F(sizeY[1], 0);
		if (sizeX[0] + sizeX[2] > width)
			scaleVec3(sizeX, width / (float)(sizeX[0] + sizeX[2]), sizeX);
		if (sizeY[0] + sizeY[2] > height)
			scaleVec3(sizeY, height / (float)(sizeY[0] + sizeY[2]), sizeY);
		scaleVec3(sizeX, 1.f/spr->width, scX);
		scaleVec3(sizeY, 1.f/spr->height, scY);
		scaleVec2(np->stretchableX, 1.f/spr->width, u);
		scaleVec2(np->stretchableY, 1.f/spr->height, v);

		u[1] += 1.f/spr->width;
		v[1] += 1.f/spr->height;

		display_sprite_ex(spr, NULL, NULL, NULL,
			xp, yp, zp,
			scX[0], scY[0],
			rgba, rgba, rgba, rgba,
			0, 0, u[0], v[0], 
			0, 0, 1, 1, 
			0, 0, clipperGetCurrent());
		if (scX[1]>0)
			display_sprite_ex(spr, NULL, NULL, NULL,
			xp+sizeX[0], yp, zp,
			scX[1], scY[0],
			rgba, rgba, rgba, rgba,
			u[0]+udelta, 0, u[1]-udelta, v[0], 
			0, 0, 1, 1, 
			0, 0, clipperGetCurrent());
		display_sprite_ex(spr, NULL, NULL, NULL,
			xp+sizeX[0]+sizeX[1], yp, zp,
			scX[2], scY[0],
			rgba, rgba, rgba, rgba,
			u[1], 0, 1, v[0], 
			0, 0, 1, 1, 
			0, 0, clipperGetCurrent());
		if (scY[1]>0)
		{
			display_sprite_ex(spr, NULL, NULL, NULL,
				xp, yp+sizeY[0], zp,
				scX[0], scY[1],
				rgba, rgba, rgba, rgba,
				0, v[0]+vdelta, u[0], v[1]-vdelta,
				0, 0, 1, 1,
				0, 0, clipperGetCurrent());
			if (scX[1]>0)
				display_sprite_ex(spr, NULL, NULL, NULL,
				xp+sizeX[0], yp+sizeY[0], zp,
				scX[1], scY[1],
				rgba, rgba, rgba, rgba,
				u[0]+udelta, v[0]+vdelta, u[1]-udelta, v[1]-vdelta,
				0, 0, 1, 1,
				0, 0, clipperGetCurrent());
			display_sprite_ex(spr, NULL, NULL, NULL,
				xp+sizeX[0]+sizeX[1], yp+sizeY[0], zp,
				scX[2], scY[1],
				rgba, rgba, rgba, rgba,
				u[1], v[0]+vdelta, 1, v[1]-vdelta,
				0, 0, 1, 1,
				0, 0, clipperGetCurrent());
		}
		display_sprite_ex(spr, NULL, NULL, NULL,
			xp, yp+sizeY[0]+sizeY[1], zp,
			scX[0], scY[2],
			rgba, rgba, rgba, rgba,
			0, v[1], u[0], 1,
			0, 0, 1, 1,
			0, 0, clipperGetCurrent());
		if (scX[1]>0)
			display_sprite_ex(spr, NULL, NULL, NULL,
			xp+sizeX[0], yp+sizeY[0]+sizeY[1], zp,
			scX[1], scY[2],
			rgba, rgba, rgba, rgba,
			u[0]+udelta, v[1], u[1]-udelta, 1,
			0, 0, 1, 1,
			0, 0, clipperGetCurrent());
		display_sprite_ex(spr, NULL, NULL, NULL,
			xp+sizeX[0]+sizeX[1], yp+sizeY[0]+sizeY[1], zp,
			scX[2], scY[2],
			rgba, rgba, rgba, rgba,
			u[1], v[1], 1, 1,
			0, 0, 1, 1,
			0, 0, clipperGetCurrent());
	}
}

void gfxSetCurrentSpriteList(GfxSpriteList* spriteList)
{
	current_sprite_list = spriteList;
}

GfxSpriteList* gfxGetCurrentSpriteList()
{
	return current_sprite_list;
}

void gfxSetupSpriteLineTexture()
{
	//this is a 1d texture used to make a fake antialiased line
	char texGenData[] = {255,255,255,32,
						 255,255,255,64,
						 255,255,255,128,
						 255,255,255,210,
						 255,255,255,255,
						 255,255,255,255,
						 255,255,255,210,
						 255,255,255,128,
						 255,255,255,64,
						 255,255,255,32};

	if (sprite_line_tex)
		return;

	sprite_line_tex = texGenNew(1, ARRAY_SIZE(texGenData)/4, "sprite_line_tex", TEXGEN_NORMAL, WL_FOR_FONTS);
	texGenUpdate(sprite_line_tex, (U8*)texGenData, RTEX_2D, RTEX_BGRA_U8, 1, true, false, false, false);

}

BasicTexture* gfxGetSpriteLineTexture()
{
	return sprite_line_tex;
}



/* End of File */

