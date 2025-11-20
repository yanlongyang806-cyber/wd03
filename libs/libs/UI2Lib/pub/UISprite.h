/***************************************************************************



***************************************************************************/

#ifndef UI_SPRITE_H
#define UI_SPRITE_H
GCC_SYSTEM

#include "UILib.h"

typedef struct BasicTexture BasicTexture;

//////////////////////////////////////////////////////////////////////////
// A "sprite" widget that displays a (scaled) texture at some position.
// Regardless of skin, the tint member is used to tint the sprite (it's
// white, i.e. no tint, by default).  A border may be added by setting
// bDrawBorder (no border by default).

typedef struct UISprite
{
	UIWidget widget;
	Color tint;
	float rot;
	bool bDrawBorder : 1;
	bool bHasBasicTexture : 1;
	bool bHasHeadshotTexture : 1;
	bool bHasHeadshotCompleted : 1;
	bool bChildrenUseDrawBox : 1;
	bool bFlipX : 1;
	bool bFlipY : 1;

	// Only one of these are set:
	bool bPreserveAspectRatio : 1;
	bool bPreserveAspectRatioFill : 1;
} UISprite;

#define UI_SPRITE_TYPE UISprite sprite;

// If w or h is < 0, then the width/height is taken from the loaded sprite.
SA_RET_NN_VALID UISprite *ui_SpriteCreate(F32 x, F32 y, F32 w, F32 h, SA_PARAM_NN_STR const char *textureName);
void ui_SpriteInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UISprite *sprite, F32 x, F32 y, F32 w, F32 h, SA_PARAM_NN_STR const char *textureName);
void ui_SpriteFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UISprite *sprite);

// Set a new texture.
void ui_SpriteSetTexture(SA_PARAM_NN_VALID UISprite *sprite, SA_PARAM_NN_STR const char *textureName);
void ui_SpriteSetBasicTexture(SA_PARAM_NN_VALID UISprite *sprite, BasicTexture* texture);
void ui_SpriteResize(SA_PARAM_NN_VALID UISprite* sprite );

void ui_SpriteTick(SA_PARAM_NN_VALID UISprite *sprite, UI_PARENT_ARGS);
void ui_SpriteDraw(SA_PARAM_NN_VALID UISprite *sprite, UI_PARENT_ARGS);

// Utility function that's generally useful
void ui_SpriteCalcDrawBox( CBox* box, AtlasTex* atex, BasicTexture* btex, bool fill );

#endif
