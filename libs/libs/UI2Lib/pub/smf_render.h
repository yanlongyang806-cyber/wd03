#pragma once
GCC_SYSTEM
/***************************************************************************
 
 
 
 ***************************************************************************/
#ifndef SMF_RENDER_H__
#define SMF_RENDER_H__

#ifndef _STDTYPES_H
#include "stdtypes.h" // for bool
#endif

typedef struct SMBlock SMBlock;
typedef struct UIStyleFont UIStyleFont;
typedef struct TextAttribs TextAttribs;
typedef struct CBox CBox;
typedef struct SpriteProperties SpriteProperties;
typedef struct UIGen UIGen;

typedef void (*SMFPlaySoundCallbackFunc)(const char *pcSoundEventPath);
typedef const char *(*SMFImageSkinOverrideFunc)(const char *pcTexture);

typedef struct SMFBlock
{
	U32 ulCrc;
	SMBlock *pBlock;
	S32 lastFontScale;

	float fMinRenderScale;
	float fMaxRenderScale;
	float fRenderScale;

	S16 iLastWidth;

	bool dont_reparse_ever_again : 1;

	// Setting bNoWrap will shut off word wrapping. Any width and height
	//   given will be ignored.
	bool bNoWrap : 1;

	// Setting bScaleToFit will shut off word wrap and calculate fRenderScale
	//   such that rendering at that scale will fit the SMF block in the given
	//   width. fRenderScale is clamped to the Min and Max, if they are non-zero.
	// Setting bScaleToFit will force bNoWrap.
	bool bScaleToFit : 1;

} SMFBlock;

void smf_Render(SMBlock *pBlock, TextAttribs *pattrs, int iXBase, int iYBase, float fZBase, unsigned char chAlpha, float fScale, int (*callback)(char *pch), const CBox *pClip, SpriteProperties *pSpriteProps);
void smf_Interact(SMFBlock *pBlock, TextAttribs *pattrs, int iXBase, int iYBase, int (*callback)(const char *pch), int (*hoverCallback)(const char *pch, UIGen* pGen), UIGen* pGen);
int smf_ParseAndFormat(SMFBlock *pSMFBlock, const char *pch, int x, int y, float z, int w, int h, bool bReparse, bool bReformat, bool bSafeOnly, TextAttribs *ptaDefaults);
int smf_ParseAndDisplay(SMFBlock *pSMFBlock, const char *pch, int x, int y, float z, int w, int h, bool bReparse, bool bReformat, bool bSafeOnly, TextAttribs *ptaDefaults, unsigned char chAlpha, int (*callback)(char *pch), SpriteProperties *pSpriteProps);
int smf_Navigate(const char *pch);
void smf_Clear(SMFBlock *pSMFBlock);
void smf_EntityReplace(char* str);

void smf_setPlaySoundCallbackFunc(SMFPlaySoundCallbackFunc func);
void smf_setImageSkinOverrideCallbackFunc(SMFImageSkinOverrideFunc func);

void smfblock_Destroy( SMFBlock *hItem );
SMFBlock* smfblock_Create( void );

S32 smfblock_GetWidth(SMFBlock *pBlock);
S32 smfblock_GetMinWidth(SMFBlock *pBlock);
S32 smfblock_GetHeight(SMFBlock *pBlock);
bool smf_GetTime(SA_PARAM_NN_VALID SMBlock *pBlock, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfStart, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfEnd);

SA_RET_NN_VALID TextAttribs *smf_DefaultTextAttribs(void);
SA_RET_NN_VALID TextAttribs *smf_CloneDefaultTextAttribs(void);
SA_RET_NN_VALID TextAttribs *smf_TextAttribsFromFont(SA_PARAM_OP_VALID TextAttribs *pAttribs, SA_PARAM_OP_VALID UIStyleFont *pFont);
void smf_TextAttribsSetScale(SA_PARAM_NN_VALID TextAttribs *pAttribs, F32 fScale);

TextAttribs *InitTextAttribs(TextAttribs *pattrs, TextAttribs *pdefaults);

#endif /* #ifndef SMF_RENDER_H__ */

/* End of File */

