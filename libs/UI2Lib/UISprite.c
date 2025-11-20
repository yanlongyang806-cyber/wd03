/***************************************************************************



***************************************************************************/


#include "inputMouse.h"

#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GfxHeadshot.h"
#include "wlModel.h"

#include "UISprite.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void ui_SpriteSetHeadshotTexture(UISprite *sprite, BasicTexture* texture);
static void ui_SpriteReleaseHeadshotTexture(UISprite *sprite);

void ui_SpriteCalcDrawBox( CBox* box, AtlasTex* atex, BasicTexture* btex, bool fill )
{
	float aspectRatio = CBoxWidth( box ) / CBoxHeight( box );
	float boxCenterX;
	float boxCenterY;
	float spriteWidth = 1;
	float spriteHeight = 1;

	CBoxGetCenter( box, &boxCenterX, &boxCenterY );
	if( btex ) {
		spriteWidth = texWidth( btex );
		spriteHeight = texHeight( btex );
	} else if( atex ) {
		spriteWidth = atex->width;
		spriteHeight = atex->height;
	}

	if( (aspectRatio > spriteWidth / spriteHeight) == !fill ) {
		float height = CBoxHeight( box );
		BuildCBoxFromCenter( box, boxCenterX, boxCenterY, height / spriteHeight * spriteWidth, height );
	} else {
		float width = CBoxWidth( box );
		BuildCBoxFromCenter( box, boxCenterX, boxCenterY, width, width / spriteWidth * spriteHeight );
	}
}

void ui_SpriteTick(UISprite *sprite, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(sprite);
	const char* widgetText = ui_WidgetGetText(UI_WIDGET(sprite));
	AtlasTex *texture = widgetText ? atlasLoadTexture(widgetText) : NULL;
	CBox drawBox;
	
	if( sprite->bPreserveAspectRatio || sprite->bPreserveAspectRatioFill ) {
		drawBox = box;
		if( sprite->bHasBasicTexture ) {
			BasicTexture* basicTexture = (BasicTexture*)UI_WIDGET( sprite )->u64;
			ui_SpriteCalcDrawBox( &drawBox, NULL, basicTexture, sprite->bPreserveAspectRatioFill );
		} else {
			ui_SpriteCalcDrawBox( &drawBox, texture, NULL, sprite->bPreserveAspectRatioFill );
		}
	} else {
		drawBox = box;
	}

	if( sprite->bChildrenUseDrawBox ) {
		x = drawBox.lx;
		y = drawBox.ly;
		w = drawBox.hx - drawBox.lx;
		h = drawBox.hy - drawBox.ly;
	} else {
		x = box.lx;
		y = box.ly;
		w = box.hx - box.lx;
		h = box.hy - box.ly;
	}
	UI_TICK_EARLY(sprite, true, true);
	UI_TICK_LATE(sprite);
}

void ui_SpriteDraw(UISprite *sprite, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(sprite);
	// TODO DJR - when using a headshot or BasicTexture, don't attempt to atlas the given texture
	const char* widgetText = ui_WidgetGetText(UI_WIDGET(sprite));
	AtlasTex *texture = widgetText ? atlasLoadTexture(widgetText) : NULL;
	CBox drawBox;
	UI_DRAW_EARLY(sprite);

	// If we have to preserve the aspect ratio, center inside the sprite's box.
	if( sprite->bPreserveAspectRatio || sprite->bPreserveAspectRatioFill ) {
		drawBox = box;
		if( sprite->bHasBasicTexture ) {
			BasicTexture* basicTexture = (BasicTexture*)UI_WIDGET( sprite )->u64;
			ui_SpriteCalcDrawBox( &drawBox, NULL, basicTexture, sprite->bPreserveAspectRatioFill );
		} else {
			ui_SpriteCalcDrawBox( &drawBox, texture, NULL, sprite->bPreserveAspectRatioFill );
		}
	} else {
		drawBox = box;
	}

	if( sprite->bFlipX ) {
		SWAPF32( drawBox.lx, drawBox.hx );
	}
	if( sprite->bFlipY ) {
		SWAPF32( drawBox.ly, drawBox.hy );
	}

	if (sprite->bDrawBorder) {
		Color borderColor;
		if (UI_GET_SKIN(sprite))
			borderColor = UI_GET_SKIN(sprite)->thinBorder[0];
		else
			borderColor = ColorBlack;
		ui_DrawOutline(&box, z, borderColor, scale);
		CBoxAlter(&box, CBAT_SHRINK, CBAD_ALL, 1);
	}

	{
		AtlasTex* atex = NULL;
		BasicTexture* btex = NULL;
		
		if( sprite->bHasBasicTexture )
		{
			BasicTexture* basic_texture = (BasicTexture*)UI_WIDGET(sprite)->u64;
			if (sprite->bHasHeadshotTexture) {
				if (!sprite->bHasHeadshotCompleted) {
					if (gfxHeadshotRaisePriority(basic_texture))
						sprite->bHasHeadshotCompleted = 1;
					else
						basic_texture = NULL;
				}
			}
			btex = basic_texture;
		}
		else
		{
			atex = texture;
		}

		if( atex || btex ) {
			int tint = RGBAFromColor(sprite->tint);
			CBox spriteBox;
			float width = 0;
			float height = 0;
			if( sprite->rot ) {
				spriteBox = display_sprite_create_cbox_for_rot( &drawBox, (drawBox.lx + drawBox.hx) / 2, (drawBox.ly + drawBox.hy) / 2, sprite->rot );
			} else {
				spriteBox = drawBox;
			}

			if( atex ) {
				width = atex->width;
				height = atex->height;
			} else {
				width = btex->width;
				height = btex->height;
			}

			display_sprite_ex( atex, btex, NULL, NULL,
							   spriteBox.left, spriteBox.top, z,
							   (spriteBox.right - spriteBox.left) / width, (spriteBox.bottom - spriteBox.top) / height,
							   tint, tint, tint, tint,
							   0, 0, 1, 1,
							   0, 0, 1, 1,
							   sprite->rot, 0, clipperGetCurrent() );
		}
	}

	if( sprite->bChildrenUseDrawBox ) {
		x = MIN( drawBox.lx, drawBox.hx );
		y = MIN( drawBox.ly, drawBox.hy );
		w = fabsf( drawBox.hx - drawBox.lx );
		h = fabsf( drawBox.hy - drawBox.ly );
	} else {
		x = box.lx;
		y = box.ly;
		w = box.hx - box.lx;
		h = box.hy - box.ly;
	}
	UI_DRAW_LATE(sprite);
}

UISprite *ui_SpriteCreate(F32 x, F32 y, F32 w, F32 h, const char *textureName)
{
	UISprite *sprite = (UISprite *)calloc(1, sizeof(UISprite));
	ui_SpriteInitialize(sprite, x, y, w, h, textureName);
	return sprite;
}

void ui_SpriteInitialize(UISprite *sprite, F32 x, F32 y, F32 w, F32 h, const char *textureName)
{
	AtlasTex *texture = atlasLoadTexture(textureName);
	ui_WidgetInitialize(UI_WIDGET(sprite), ui_SpriteTick, ui_SpriteDraw, ui_SpriteFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(sprite), x, y);
	ui_SpriteSetTexture(sprite, textureName);
	if (w < 0) w = texture->width;
	if (h < 0) h = texture->height;
	ui_WidgetSetDimensions(UI_WIDGET(sprite), w, h);
	sprite->tint = ColorWhite;
}

void ui_SpriteFreeInternal(UISprite *sprite)
{
	if (sprite->bHasHeadshotTexture)
		ui_SpriteReleaseHeadshotTexture(sprite);
	ui_WidgetFreeInternal(UI_WIDGET(sprite));
}

void ui_SpriteSetTexture(UISprite *sprite, const char *textureName)
{
	BasicTexture * texture;
	ui_WidgetSetTextString(UI_WIDGET(sprite), textureName);
	if (sprite->bHasHeadshotTexture)
		ui_SpriteReleaseHeadshotTexture(sprite);
	UI_WIDGET(sprite)->u64 = 0;
	sprite->bHasBasicTexture = false;

	texture = texFindAndFlag(textureName, false, WL_FOR_PREVIEW_INTERNAL);
	// TODO DJR put this on a switch for testing
#if 0
	if (texture && (texIsCubemap(texture) || texIsVolume(texture)))
	{
		BasicTexture ** eaTextureSwaps = NULL;
		eaPush(&eaTextureSwaps, texFindAndFlag("test_cube_cube", false, WL_FOR_PREVIEW_INTERNAL));
		eaPush(&eaTextureSwaps, texture);
		// TODO DJR - make a standardized texture preview wrapper, use a smaller headshot
		texture = gfxHeadshotCaptureModelEx("TexturePick", 1024, 1024, modelFind("OutwardAndInwardSpheres", true, WL_FOR_PREVIEW_INTERNAL), NULL, ColorTransparent, eaTextureSwaps);
		ui_SpriteSetHeadshotTexture(sprite, texture);
	}
#endif
}

void ui_SpriteSetBasicTexture(UISprite *sprite, BasicTexture* texture)
{
	if (sprite->bHasHeadshotTexture)
		ui_SpriteReleaseHeadshotTexture(sprite);
	ui_WidgetSetTextString(UI_WIDGET(sprite), "");
	UI_WIDGET(sprite)->u64 = (uintptr_t)texture;
	sprite->bHasBasicTexture = true;
}

void ui_SpriteResize(SA_PARAM_NN_VALID UISprite* sprite )
{
	if( sprite->bHasBasicTexture ) {
		BasicTexture* tex = (BasicTexture*)UI_WIDGET(sprite)->u64;
		if( tex ) {
			UI_WIDGET( sprite )->width = texWidth( tex );
			UI_WIDGET( sprite )->height = texHeight( tex );
		}
	} else {
		AtlasTex* tex = atlasLoadTexture( ui_WidgetGetText( UI_WIDGET( sprite )));
		if( tex ) {
			UI_WIDGET( sprite )->width = tex->width;
			UI_WIDGET( sprite )->height = tex->height;
		}
	}
}


static void ui_SpriteSetHeadshotTexture(UISprite *sprite, BasicTexture* texture)
{
	UI_WIDGET(sprite)->u64 = (uintptr_t)texture;
	sprite->bHasBasicTexture = true;
	sprite->bHasHeadshotTexture = true;
	sprite->bHasHeadshotCompleted = false;
}

static void ui_SpriteReleaseHeadshotTexture(UISprite *sprite)
{
	BasicTexture * texture = (BasicTexture*)UI_WIDGET(sprite)->u64;
	UI_WIDGET(sprite)->u64 = (uintptr_t)0;
	sprite->bHasBasicTexture = false;
	sprite->bHasHeadshotTexture = false;
	sprite->bHasHeadshotCompleted = false;
	if (texture)
		gfxHeadshotRelease(texture);
}
