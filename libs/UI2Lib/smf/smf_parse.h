#pragma once
GCC_SYSTEM
/***************************************************************************
 
 
 
 ***************************************************************************/
#ifndef SMF_PARSE_H__
#define SMF_PARSE_H__

/*************************************************************************/

// Maximum number of columns in a table.
#define SMF_MAX_COLS 10

#define SMF_MAX_IMG_NAME 256

typedef struct SMImage
{
	char achTex[SMF_MAX_IMG_NAME];
	char achTexHover[SMF_MAX_IMG_NAME];
	int iWidth;
	int iHeight;
	int iHighlight;
	int iColor;
	int iSkinOverride;
} SMImage;

typedef struct SMTable
{
	SMAlignment eCollapse;

	// Used for <ul>
	int iIndent;
	char achSymbol[SMF_MAX_IMG_NAME];
} SMTable;

typedef struct SMColumn
{
	int iWidthRequested;
	int iHighlight;
	int iSelected;
	bool bFixedWidth;
} SMColumn;

typedef struct SMRow
{
	int iHighlight;
	int iSelected;
} SMRow;


typedef struct SMFont
{
	int bColorTop        : 1;
	int bColorBottom     : 1;
	int bFace            : 1;
	int bScale           : 1;
	int bOutline         : 1;
	int bShadow          : 1;
	int bOutlineColor    : 1;
	int bDropShadowColor : 1;
	int bBold            : 1;
	int bItalic          : 1;
	int bSnapToPixels    : 1;
} SMFont;

typedef struct SMSound
{
	U8 bPlayed : 1;
	char pcSoundEventPath[];
} SMSound;

typedef struct SMTime
{
	F32 fStart;
	F32 fEnd;
} SMTime;

typedef enum SMAnchorType
{
	SMAnchorLink,
	SMAnchorItem
} SMAnchorType;

typedef struct SMAnchor
{
	bool bHover;
	bool bSelected;
	SMAnchorType type;
	char ach[];
} SMAnchor;

extern SMTagDef smf_aTagDefs[];

typedef enum SMFTags
{
	k_sm_none = 0,
	k_sm_ws,		// whitespace
	k_sm_bsp,		// breaking space
	k_sm_br,		// line-break
	k_sm_image,
	k_sm_table,
	k_sm_table_end,
	k_sm_tr,
	k_sm_tr_end,
	k_sm_td,
	k_sm_td_end,
	k_sm_span,
	k_sm_span_end,
	k_sm_p,
	k_sm_p_end,
	k_sm_nolink,
	k_sm_nolink_end,
	k_sm_colorboth,
	k_sm_colorboth_end,
	k_sm_text,
	k_sm_anchor,

	k_sm_b,
	k_sm_i,
	k_sm_color,
	k_sm_colordummy,
	k_sm_outlinecolor,
	k_sm_dropshadowcolor,
	k_sm_scale,
	k_sm_face,
	k_sm_font,
	k_sm_outline,
	k_sm_shadow,
	k_sm_snaptopixels,
	k_sm_toggle_end,
	k_sm_toggle_link_end,

	k_sm_time,
	k_sm_time_end,

	k_sm_ul,			// unordered list
	k_sm_ul_end,
	k_sm_li,			// list item
	k_sm_li_end,

	k_sm_sound,

} SMFTags;

#endif /* #ifndef SMF_PARSE_H__ */

/* End of File */

