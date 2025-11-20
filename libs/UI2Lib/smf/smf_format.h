/***************************************************************************
 
 
 
 ***************************************************************************/
#ifndef SMF_FORMAT_H__
#define SMF_FORMAT_H__
GCC_SYSTEM

typedef struct SMBlock SMBlock;
typedef struct GfxFont GfxFont;

#define SMF_FONT_SCALE (100.0f)

// These need to match the order of the tag def (SMFTags) entries.
// TextAttribs, FormatTags, and the SMFTag first N entries must all match.
// If you add one here, add one there. And vice versa.

typedef struct TextAttribs
{
	void **ppBold;
	void **ppItalic;
	void **ppColorTop;
	void **ppColorBottom;
	void **ppOutlineColor;
	void **ppDropShadowColor;
	void **ppScale;
	void **ppFace;
	void **ppFont;
	void **ppAnchor;
	void **ppLink;
	void **ppLinkBG;
	void **ppLinkHover;
	void **ppLinkHoverBG;
	void **ppOutline;
	void **ppShadow;
	void **ppSnapToPixels;
} TextAttribs;

// These need to match the order of the tag def (SMFTags) entries
// TextAttribs, FormatTags, and the SMFTag first N entries must all match.
// If you add one here, add one there. And vice versa.

typedef enum FormatTags
{
	kFormatTags_Bold,
	kFormatTags_Italic,
	kFormatTags_ColorTop,
	kFormatTags_ColorBottom,
	kFormatTags_OutlineColor,
	kFormatTags_DropShadowColor,
	kFormatTags_Scale,
	kFormatTags_Face,
	kFormatTags_Font,
	kFormatTags_Anchor,
	kFormatTags_Link,
	kFormatTags_LinkBG,
	kFormatTags_LinkHover,
	kFormatTags_LinkHoverBG,
	kFormatTags_Outline,
	kFormatTags_Shadow,
	kFormatTags_SnapToPixels,
	kFormatTags_Count,
} FormatTags;

// These need to match the order of the tag def (SMFTags) entries.
// TextAttribs, FormatTags, and the SMFTag first N entries must all match.
// If you add one here, add one there. And vice versa.

// smf_parse.h
SMBlock *smf_CreateAndParse(const char *pch, bool bSafeOnly);
void smf_Destroy(SMBlock *pBlock);

// smf_format.h
bool smf_Format(SMBlock *pBlock, TextAttribs *pattrs, int iWidth, int iHeight, bool bNoWrap);
bool smf_HandleFormatting(SMBlock *pBlock, TextAttribs *pattrs);
void smf_MakeFont(GfxFont **pFont, GfxFont* pCopyRenderParams, TextAttribs *pattrs);
void smf_PushAttrib(SMBlock *pBlock, TextAttribs *pattrs);
void *smf_PopAttrib(SMBlock *pBlock, TextAttribs *pattr);
void *smf_PopAttribInt(int idx, TextAttribs *pattr);


#endif /* #ifndef SMF_FORMAT_H__ */

/* End of File */

