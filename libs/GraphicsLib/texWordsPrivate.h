#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "earray.h"
#include "GfxTextureEnums.h"
#include "RdrTextureEnums.h"
#include "textparser.h"
#include "StashTable.h"

typedef enum TexWordLayerType {
	TWLT_NONE,
	TWLT_BASEIMAGE,
	TWLT_TEXT,
	TWLT_IMAGE,
} TexWordLayerType;

typedef enum TexWordLayerStretch {
	TWLS_NONE,
	TWLS_FULL,
	TWLS_TILE,
} TexWordLayerStretch;

typedef enum TexWordBlendType {
	TWBLEND_OVERLAY,
	TWBLEND_MULTIPLY,
	TWBLEND_ADD,
	TWBLEND_SUBTRACT,
	TWBLEND_REPLACE,
} TexWordBlendType;

typedef enum TexWordFilterType {
	TWFILTER_NONE,
	TWFILTER_BLUR,
	TWFILTER_DROPSHADOW,
	TWFILTER_DESATURATE,
} TexWordFilterType;

AUTO_ENUM;
typedef enum TexWordAlign {
	TWALIGN_FILL,
	TWALIGN_CENTER,
	TWALIGN_LEFT,
	TWALIGN_RIGHT,
} TexWordAlign;
extern StaticDefineInt TexWordAlignEnum[];

typedef struct TexWordLayerFont {
	const char *fontName;

	int drawSize;
	bool italicize;
	bool bold;
	U8 outlineWidth;
	U8 dropShadowOffset[2];
	U8 softShadowSpread; // Probably don't use this...
	TexWordAlign fontAlign;

} TexWordLayerFont;

typedef struct TexWordLayer TexWordLayer;

typedef struct TexWordLayerFilter {
	TexWordFilterType type;
	int magnitude; // In px for blur, dropshadow, etc
	F32 percent;   // Magnitude in the 0..1 range
	EArrayIntHandle rgba;
	int offset[2];
	F32 spread;
	TexWordBlendType blend;
	int uid; // For the editor
	TexWordLayer *layer_parent; // For the editor
} TexWordLayerFilter;

typedef struct TWEditDoc TWEditDoc;

typedef struct TexWordLayer {
	char *layerName;
	// Data defining the layer
	TexWordLayerType type;
	TexWordLayerStretch stretch;
	char *text;
	F32 pos[2];
	F32 size[2];
	F32 rot;
	EArrayIntHandle rgba;
	EArrayIntHandle rgbas[4];
	const char *imageName;
	bool hidden;
	TexWordLayerFont font;
	// Filter (one only?)
	TexWordLayerFilter **filter;

	// Sub-layer (should only have one!)
	struct TexWordLayer **sublayer;
	TexWordBlendType subBlend;
	F32 subBlendWeight;

	// Working data
	char *editor_text; // Just for the editor
	BasicTexture *image;
	TWEditDoc *editor_parent; // For callbacks in the editor
	int uid; // For the editor
} TexWordLayer;

typedef struct TexWord {
	const char *filename;
	int size[2];
	TexWordLayer **layers;
} TexWord;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct TexWordList {
	TexWord **texWords; AST(NAME("TexWord") STRUCT(parse_TexWord))
} TexWordList;

// Note, you must update TEXWORDS_CACHE_VERSION if you change this structure
typedef struct TexWordLoadInfo {
	U8 *data;
	int data_size;
	U8 *mipmap_data;
	int mipmap_data_size;
	RdrTexFormat rdr_format;
	int actualSizeX;
	int actualSizeY;
	int sizeX;
	int sizeY;
	int reduce_mip_setting;
	TexFlags flags; // Just using TEX_ALPHA
	void *from_cache_freeme; // If this is set, this load info is from the DynamicCache, and should be freed differently
} TexWordLoadInfo;



extern ParseTable parse_TexWord[];
#define TYPE_parse_TexWord TexWord
extern ParseTable parse_TexWordList[];
#define TYPE_parse_TexWordList TexWordList
extern ParseTable parse_TexWordLayer[];
#define TYPE_parse_TexWordLayer TexWordLayer
extern ParseTable parse_TexWordLayerFont[];
#define TYPE_parse_TexWordLayerFont TexWordLayerFont
extern ParseTable parse_TexWordLayerFilter[];
#define TYPE_parse_TexWordLayerFilter TexWordLayerFilter
extern StaticDefineInt	ParseTexWordLayerType[];
extern StaticDefineInt	ParseTexWordLayerStretch[];
extern StaticDefineInt	ParseTexWordBlendType[];
extern StaticDefineInt	ParseTexWordFilterType[];


extern TexWordList texWords_list;
extern StashTable htTexWords; // Indexed by TexName, only has current locale in hashtable
extern bool texWords_disableCache; // Set by the TexWordsEditor
typedef struct MessageStore MessageStore;
extern MessageStore* texWordsMessages;
typedef struct TextParserState TextParserState;

bool texWordVerify(TexWord *texWord, bool fix, TextParserState *tps);

void texWordMessageStoreFileName(char *messageStoreFileName, size_t messageStoreFileName_size, const TexWord *texWord);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););


