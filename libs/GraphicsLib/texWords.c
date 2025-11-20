#include "texWords.h"
#include "texWordsPrivate.h"
#include "GfxTexWords.h"
#include "GfxTextures.h"
#include "GfxTextureTools.h"
#include "GraphicsLibPrivate.h"
#include "RdrTexture.h"
#include "TimedCallback.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "GfxDXT.h"
#include "CrypticDXT.h"
#include "GfxMipMap.h"
#include "MessageStore.h"
#include "GfxFontPrivate.h"
#include "GfxFont.h"
#include "StringUtil.h"
#include "Color.h"
#include "rgb_hsv.h"
#include "rotoZoom.h"
#include "memlog.h"
#include "AppRegCache.h"
#include "gfxLoadScreens.h"
#include "cpu_count.h"
#include "structInternals.h"
#include "DynamicCache.h"
#include "fileLoader.h"
#include "ScratchStack.h"
#include "MemRef.h"
#include "gimmeDLLWrapper.h"
#include "ImageUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:texWordsThread", BUDGET_Textures_Misc););

#define TEXWORDS_CACHE_VERSION 6

#if PLATFORM_CONSOLE
#define USE_MMX 0
#else
#define USE_MMX 1
#endif

#define lsprintf if (gfx_state.debug.texWordVerbose) loadstart_printf
#define leprintf if (gfx_state.debug.texWordVerbose) loadend_printf

TexWordList texWords_list;
StashTable htTexWords; // Indexed by TexName, only has current locale in hashtable
static char texWordsLocale[MAX_PATH];
static char texWordsLocale2[MAX_PATH];

static DynamicCache *texWords_cache;

int texWords_multiThread=true;
// Enables or disables multi-threaded TexWords generation
AUTO_CMD_INT(texWords_multiThread, texWordsMultiThread) ACMD_CATEGORY(Debug, Graphics) ACMD_CMDLINE;

int texWords_allowDXT=true;
// Enables or disables DXT compression of TexWords textures
AUTO_CMD_INT(texWords_allowDXT, texWordsAllowDXT) ACMD_CATEGORY(Debug, Graphics) ACMD_CMDLINE;

bool texWords_disableCache=false; // Set by the TexWordsEditor
// Disables the TexWords cache
AUTO_CMD_INT(texWords_disableCache, texWordsDisableCache) ACMD_CATEGORY(Debug, Graphics) ACMD_CMDLINE;

bool texWord_doNotYield=false;
U32 texWords_pixels=0; // Count of pixels rendered (more like "bytes")

static CRITICAL_SECTION criticalSectionDoingTexWordRendering; // We use statics here
static CRITICAL_SECTION criticalSectionDoingTexWordInfo;
static CRITICAL_SECTION criticalSectionUpdatingFontTextureRawInfo;
static volatile long numTexWordsInThread=0;

static voidVoidFunc texWords_reloadCallback;

MessageStore* texWordsMessages;

typedef struct TexWordQueuedLoad
{
	TexWord *texWord;
	BasicTexture *texBindParent;
	bool yield;
} TexWordQueuedLoad;

static TexWordQueuedLoad **texWordQueuedLoads; // TexWords queued and waiting for their dependencies to be loaded
MP_DEFINE(TexWordQueuedLoad);

void texWordsLoad(const char *localeName);
void texWordsReloadText(const char *localeName);
static void texWordLoadInternal(TexWord *texWord, TexLoadHow mode, WLUsageFlags use_category, BasicTexture *texBindParent);




StaticDefineInt	TexWordParamLookup[] =
{
	DEFINE_INT
	{ "TexWordParam1",	0},
	{ "TexWordParam2",	1},
	{ "TexWordParam3",	2},
	{ "TexWordParam4",	3},
	{ "TexWordParam5",	4},
	{ "TexWordParam6",	5},
	{ "TexWordParam7",	6},
	{ "TexWordParam8",	7},
	{ "TexWordParam9",	8},
	{ "TexWordParam10",	9},
	DEFINE_END
};

StaticDefineInt	ParseTexWordLayerType[] =
{
	DEFINE_INT
	{ "None",		TWLT_NONE},
	{ "BaseImage",	TWLT_BASEIMAGE},
	{ "Text",		TWLT_TEXT},
	{ "Image",		TWLT_IMAGE},
	DEFINE_END
};


StaticDefineInt	ParseTexWordSizeOptions[] =
{
	DEFINE_INT
	{ "Auto",		0},
	{ "8",		8},
	{ "16",		16},
	{ "32",		32},
	{ "64",		64},
	{ "128",	128},
	{ "256",	256},
	{ "512",	512},
	{ "1024",	1024},
	DEFINE_END
};

StaticDefineInt	ParseTexWordLayerStretch[] =
{
	DEFINE_INT
	{ "None",	TWLS_NONE},
	{ "Full",	TWLS_FULL},
	{ "Tile",	TWLS_TILE},
	DEFINE_END
};

StaticDefineInt	ParseTexWordBlendType[] =
{
	DEFINE_INT
	{ "Overlay",	TWBLEND_OVERLAY},
	{ "Multiply",	TWBLEND_MULTIPLY},
	{ "Add",		TWBLEND_ADD},
	{ "Subtract",	TWBLEND_SUBTRACT},
	{ "Replace",	TWBLEND_REPLACE},
	DEFINE_END
};

StaticDefineInt	ParseTexWordFilterType[] =
{
	DEFINE_INT
	{ "None",		TWFILTER_NONE},
	{ "Blur",		TWFILTER_BLUR},
	{ "DropShadow",	TWFILTER_DROPSHADOW},
	{ "Desaturate",	TWFILTER_DESATURATE},
	DEFINE_END
};

StaticDefineInt ParseTexWordOutline[] =
{
	DEFINE_INT
	{ "None", 0},
	{ "1", 1},
	{ "2", 2},
	{ "3", 3},
	{ "4", 4},
	{ "5", 5},
	{ "6", 6},
	{ "7", 7},
	{ "8", 8},
	{ "9", 9},
	{ "10", 10},
	DEFINE_END
};

StaticDefineInt ParseTexWordFontSize[] =
{
	DEFINE_INT
	{ "Auto", 0},
	{ "8", 8},
	{ "12", 12},
	{ "16", 16},
	{ "24", 24},
	{ "32", 32},
	{ "48", 48},
	{ "64", 64},
	{ "96", 96},
	{ "128", 128},
	{ "192", 192},
	{ "256", 256},
	{ "384", 384},
	{ "512", 512},
	DEFINE_END
};

ParseTable parse_TexWordLayerFont[] = {
	{ "Name",					TOK_POOL_STRING | TOK_STRING(TexWordLayerFont,fontName,0)							},
	{ "Size",					TOK_INT(TexWordLayerFont,drawSize, 16), ParseTexWordFontSize	},
	{ "Italic",					TOK_BOOLFLAG(TexWordLayerFont,italicize,0)						},
	{ "Bold",					TOK_BOOLFLAG(TexWordLayerFont,bold,0)							},
	{ "Outline",				TOK_U8(TexWordLayerFont,outlineWidth,0), ParseTexWordOutline	},
	{ "DropShadow",				TOK_RG(TexWordLayerFont, dropShadowOffset)						},
//	{ "SoftShadow",				TOK_U8(TexWordLayerFont,softShadowSpread)						},
	{ "Align",					TOK_INT(TexWordLayerFont, fontAlign, TWALIGN_FILL), TexWordAlignEnum			},

	{ "EndFont",				TOK_END,		0											},
	{ "", 0, 0 }
};

ParseTable ParseTexWordLayerFilterOffset[] =
{
	{	"",				TOK_STRUCTPARAM | TOK_INT(TexWordLayerFilter, offset[0],0) },
	{	"",				TOK_STRUCTPARAM | TOK_INT(TexWordLayerFilter, offset[1],0) },
	{	"\n",			TOK_END,								0},
	{	"", 0, 0 }
};

ParseTable parse_TexWordLayerFilter[] = {
	{ "Type",					TOK_INT(TexWordLayerFilter,type, 0), ParseTexWordFilterType	},
	{ "Magnitude",				TOK_INT(TexWordLayerFilter,magnitude, 1)	},
	{ "Percent",				TOK_F32(TexWordLayerFilter,percent, 1)		},
	{ "Spread",					TOK_F32(TexWordLayerFilter,spread, 0)		},
	{ "Color",					TOK_INTARRAY(TexWordLayerFilter,rgba)		},
	{ "Offset",					TOK_IVEC2(TexWordLayerFilter,offset)		},
	{ "OffsetX",				TOK_REDUNDANTNAME|TOK_INT(TexWordLayerFilter,offset[0],0)		},
	{ "OffsetY",				TOK_REDUNDANTNAME|TOK_INT(TexWordLayerFilter,offset[1],0)		},

	{ "Blend",					TOK_INT(TexWordLayerFilter,blend, 0), ParseTexWordBlendType	},

	{ "EndFilter",				TOK_END,		0											},
	{ "", 0, 0 }
};



ParseTable parse_TexWordLayer[] = {
	{ "Name",					TOK_STRING(TexWordLayer,layerName,0)	},
	{ "Type",					TOK_INT(TexWordLayer,type, 0), ParseTexWordLayerType	},
	{ "Stretch",				TOK_INT(TexWordLayer,stretch, 0), ParseTexWordLayerStretch},
	{ "TextKey",				TOK_STRING(TexWordLayer,text,0)			},
	{ "Text",					TOK_REDUNDANTNAME|TOK_STRING(TexWordLayer,editor_text,0)			},
	{ "Image",					TOK_POOL_STRING|TOK_STRING(TexWordLayer,imageName,0)	},
	{ "Color",					TOK_INTARRAY(TexWordLayer,rgba)			},
	{ "Color0",					TOK_INTARRAY(TexWordLayer,rgbas[0])		},
	{ "Color1",					TOK_INTARRAY(TexWordLayer,rgbas[1])		},
	{ "Color2",					TOK_INTARRAY(TexWordLayer,rgbas[2])		},
	{ "Color3",					TOK_INTARRAY(TexWordLayer,rgbas[3])		},
	{ "Size",					TOK_VEC2(TexWordLayer,size)			},
	{ "Pos",					TOK_VEC2(TexWordLayer,pos)			},
	{ "Rot",					TOK_F32(TexWordLayer,rot,0)			},
	{ "Hidden",					TOK_BOOLFLAG(TexWordLayer,hidden,0),		},
	{ "Font",					TOK_EMBEDDEDSTRUCT(TexWordLayer,font,parse_TexWordLayerFont)},

	{ "Filter",					TOK_STRUCT(TexWordLayer,filter, parse_TexWordLayerFilter) },

	{ "SubLayer",				TOK_STRUCT(TexWordLayer,sublayer, parse_TexWordLayer) },
	{ "SubBlend",				TOK_INT(TexWordLayer,subBlend, 0), ParseTexWordBlendType	},
	{ "SubBlendWeight",			TOK_F32(TexWordLayer,subBlendWeight, 1), 0},

	{ "EndLayer",				TOK_END,		0									},
	{ "EndSubLayer",			TOK_END,		0									},
	{ "", 0, 0 }
};

ParseTable parse_TexWord[] = {
	{ "TexWord",					TOK_IGNORE,	0}, // hack, so we can read in individual entries
	{ "Name",						TOK_POOL_STRING|TOK_CURRENTFILE(TexWord,filename)				},
	{ "Size",						TOK_IVEC2(TexWord, size), ParseTexWordSizeOptions },
	{ "Layer",						TOK_STRUCT(TexWord,layers, parse_TexWordLayer) },

	{ "EndTexWord",					TOK_END,		0										},
	{ "", 0, 0 }
};



void texWordsSetReloadCallback(voidVoidFunc callback)
{
	texWords_reloadCallback = callback;
}

MP_DEFINE(TexWordParams);
TexWordParams *createTexWordParams(void)
{
	MP_CREATE(TexWordParams, 16);
	return MP_ALLOC(TexWordParams);
}
void destroyTexWordParams(TexWordParams *params)
{
	// this is actually a pointer to someone else's memory: SAFE_FREE(params->layoutName);
	// so are these: eaDestroyContents(&params->parameters, NULL);
	eaDestroy(&params->parameters);
	MP_FREE(TexWordParams, params);
}

MP_DEFINE(TexWordLoadInfo);
TexWordLoadInfo *createTexWordLoadInfo(void)
{
	TexWordLoadInfo *ret;
	EnterCriticalSection(&criticalSectionDoingTexWordInfo);
	MP_CREATE(TexWordLoadInfo, 4);
	ret = MP_ALLOC(TexWordLoadInfo);
	LeaveCriticalSection(&criticalSectionDoingTexWordInfo);
	return ret;
}
void destroyTexWordLoadInfo(TexWordLoadInfo *loadinfo)
{
	EnterCriticalSection(&criticalSectionDoingTexWordInfo);
	if (loadinfo->from_cache_freeme) {
		free(loadinfo->from_cache_freeme); // Invalidates *loadinfo*
		loadinfo = NULL;
	} else {
		SAFE_FREE(loadinfo->data);
		SAFE_FREE(loadinfo->mipmap_data);
		MP_FREE(TexWordLoadInfo, loadinfo);
	}
	LeaveCriticalSection(&criticalSectionDoingTexWordInfo);
}

const char *texWordFindFilename(const TexWord *texWord)
{
	return texWord->filename;
}

const char *texWordsGetLocale(void)
{
	return texWordsLocale;
}


int texWordGetPixelsPerSecond(void)
{
	U32		delta_ticks;
	U32		delta_pixels;
	F32		time;
	static	U32 last_ticks;
	static  U32 last_pixels=0;
	static	U32 last_pps_ticks=0;
	static  int pps=0;

	last_ticks = timerCpuTicks();

	// Calc PPS over a few seconds
	delta_ticks = last_ticks - last_pps_ticks;
	time = (F32)delta_ticks / (F32)timerCpuSpeed();
	if (time > 1.0)
	{
		delta_pixels = texWords_pixels - last_pixels;
		last_pixels = texWords_pixels;
		pps = delta_pixels / time;
		last_pps_ticks = last_ticks;
		if (pps && gfx_state.debug.texWordVerbose) {
			printf("TWPPS: % 9d\n", pps);
		}
	}
	return pps / 1000 * 1000 + numTexWordsInThread + eaSize(&texWordQueuedLoads);
}

int texWordGetTotalPixels(void)
{
	return texWords_pixels;
}

int texWordGetLoadsPending(void)
{
	return numTexWordsInThread + eaSize(&texWordQueuedLoads);
}

static int yield_amount=3072*4; // CoH * 4
static void texWordsPixelsRendered(int pixels, bool yield)
{
	static int yield_sum=0;
	texWords_pixels+=pixels;
	//printf("twpr: % 6d\n", pixels);
	if (yield && !texWord_doNotYield) {
		yield_sum+=pixels;
		if (yield_sum > yield_amount) {
			yield_sum = 0;
			Sleep(1);
		}
	}
	if (!yield) {
		gfxLoadUpdate(pixels/32);
	}
}

void texWordsFlush(void)
{
	// Sets a flag (that may get cleared at some arbitrary time later) that makes the background thread
	//  take up more (up to 50%) CPU
	texWord_doNotYield=true;
}

void initBackgroundTexWordRenderer(void)
{
	InitializeCriticalSection(&criticalSectionDoingTexWordInfo);
	InitializeCriticalSection(&criticalSectionDoingTexWordRendering);
	InitializeCriticalSection(&criticalSectionUpdatingFontTextureRawInfo);
}


static int texWordsNameCmp(const TexWord ** info1, const TexWord ** info2 )
{
	return stricmp( getFileNameConst((*info1)->filename), getFileNameConst((*info2)->filename) );
}

TexWord *texWordFind(const char *texName, int search)
{
	char baseTexName[MAX_PATH];
	char *s;
	char *locale=NULL;
	TexWord *ret;
	strcpy(baseTexName, texName);
	if (strEndsWith(baseTexName, ".wtex"))
		baseTexName[strlen(baseTexName)-5] = '\0';
	if (s = strchr(baseTexName, '#')) {
		*s=0;
		locale = s+1;
	}
	stashFindPointer( htTexWords, baseTexName, &ret ); // Search for TexWord in current locale
	if (!ret && search) { // Search for TexWord in #English locale
		strcat(baseTexName, "#English");
		stashFindPointer( htTexWords, baseTexName, &ret );
	} else if (!ret && !locale) {
		// Is this needed, or should we just add #English ones in texLoadInfo?
		// If a #English .tga does not exist (nor any other specialized locale), and a #English texword does exist, use it!
		strcat(baseTexName, "#English");
		if (!texFind(baseTexName, false)) {
			stashFindPointer( htTexWords, baseTexName, &ret );
		}
	}
	if (!ret && gfx_state.debug.texWordVerbose) {
		// Debug hack to fill everything that would have a texword with a dummy file
		stashFindPointer( htTexWords, "DUMMYLAYOUT", &ret );
	}
	if (!ret)
		return NULL;
	return ret;
}


static void texWordCacheFileName(char *cacheFileName, size_t cacheFileName_size, const TexWord *texWord, BasicTexture *texBindParent)
{
	assert(texGetTexWord(texBindParent) == texWord); // Otherwise must pass something else to dynamicCacheGetAsync
	getFileNameNoExt_s(cacheFileName, cacheFileName_size, texWord->filename);
	strcat_s(cacheFileName, cacheFileName_size, "#");
	strcat_s(cacheFileName, cacheFileName_size, texWordsLocale);
	if (texGetTexWordParams(texBindParent)) {
		int i;
		for (i=0; i<eaSize(&texGetTexWordParams(texBindParent)->parameters); i++) {
			strcat_s(cacheFileName, cacheFileName_size, "_");
			strcat_s(cacheFileName, cacheFileName_size, texGetTexWordParams(texBindParent)->parameters[i]);
		}
	}
}

void texWordMessageStoreFileName(char *messageStoreFileName, size_t messageStoreFileName_size, const TexWord *texWord)
{
	const char *relpath = strstriConst(texWord->filename, "texture_library");
	if (!relpath) {
		changeFileExt_s(texWord->filename, ".ms", messageStoreFileName, messageStoreFileName_size);
	} else {
		sprintf_s(SAFESTR2(messageStoreFileName), "texts/%s/%s", texWordsLocale, relpath);
		changeFileExt_s(messageStoreFileName, ".ms", messageStoreFileName, messageStoreFileName_size);
	}
}


BasicTexture *texWordGetBaseImage(TexWord *texWord, int *width, int *height)
{
	int numlayers = eaSize(&texWord->layers);
	int i;
	BasicTexture *ret=NULL;
	for (i=0; i<numlayers; i++) {
		if (texWord->layers[i]->type == TWLT_BASEIMAGE) {
			ret = texWord->layers[i]->image;
			break;
		}
		if (texWord->layers[i]->type == TWLT_IMAGE)
			ret = texFind(texWord->layers[i]->imageName, true);
	}
	assert(invisible_tex);
	if (!ret)
		ret = invisible_tex;
	if (width) {
		if (texWord->size[0] > 0)
			*width = texWord->size[0];
		else if (ret)
			*width = texGetOrigWidth(ret);
		else
			*width = 0;
	}
	if (height) {
		if (texWord->size[1] > 0)
			*height = texWord->size[1];
		else if (ret)
			*height = texGetOrigHeight(ret);
		else
			*height = 0;
	}
	return ret;
}


static int layerNameCount=0, subLayerNameCount=0;
static bool texWordVerifyLayer(TexWord *texWord, TexWordLayer *layer, bool fix, bool subLayer, TextParserState *tps)
{
	// Return rules:  If passed in "fix", only return false if the error was reported
	//  (e.g. with ErrorFilenamef) and needs manual fixing.
	bool ret=true;

	if (layer->size[0] > 0 && layer->size[0] < 1) {
		if (fix) {
			layer->size[0] = 0;
		} else {
			ErrorFilenamef(texWord->filename, "Layer X size is less than 1 (%f)", layer->size[0]);
			ret = false;
		}
	}
	if (layer->size[1] > 0 && layer->size[1] < 1) {
		if (fix) {
			layer->size[1] = 0;
		} else {
			ErrorFilenamef(texWord->filename, "Layer Y size is less than 1 (%f)", layer->size[1]);
			ret = false;
		}
	}

	if (!layer->layerName || !layer->layerName[0]) {
		if (fix) {
			char buf[256];
			if (subLayer) {
				sprintf(buf, "sub%d", ++subLayerNameCount);
			} else {
				sprintf(buf, "#%d", ++layerNameCount);
			}
			layer->layerName = StructAllocString(buf);
		} else {
			ret = false;
		}
	}

	if (layer->type == TWLT_IMAGE) {
		if (!layer->imageName) {
			if (fix)
				layer->imageName = allocAddString("white");
			else
				ret = false;
		}
		if (layer->imageName) {
			// Verify it exists
			if (!texFind(layer->imageName, 0)) {
				ErrorFilenamef(texWord->filename, "Reference to non-existent texture: %s", layer->imageName);
				ret = false;
			}
		}
	} else if (layer->type == TWLT_BASEIMAGE) {
		char baseName[MAX_PATH];
		getFileNameNoExt(baseName, texWord->filename);
		if (!texFind(baseName, 0)) {
			ErrorFilenamef(texWord->filename, "Reference to non-existent texture: %s", baseName);
			ret = false;
		}
	} else if (layer->type == TWLT_TEXT) {
		if (!layer->text) {
			if (fix)
				layer->text = StructAllocString("Placeholder");
			else
				ret=false;
		}
		if (!layer->font.fontName) {
			if (fix)
				layer->font.fontName = allocAddString("FreeSans");
			else
				ret = false;
		}
		if (layer->font.fontName) {
			if (tps)
			{
				char fontdep[MAX_PATH];
				strcpy(fontdep, "fonts/");
				strcat(fontdep, layer->font.fontName);
				ParserBinAddFileDep(tps, fontdep);
			}
			
			if (!gfxFontGetFontData(layer->font.fontName)) {
				ErrorFilenamef(texWord->filename, "Reference to non-existent font: %s", layer->font.fontName);
				ret = false;
				if (fix)
					layer->font.fontName = allocAddString("FreeSans");
			}
		}
		if (layer->stretch != TWLS_NONE) {
			if (fix)
				layer->stretch = TWLS_NONE;
			else
				ret = false;
		}
	}
	if (layer->sublayer) {
		int numsublayers = eaSize(&layer->sublayer);
		if (numsublayers>1) {
			ErrorFilenamef(texWord->filename, "Error: found more than 1 sublayer in a texword file");
			ret = false;
			if (fix) {
				int i;
				for (i=1; i<numsublayers; i++) {
					StructDestroy(parse_TexWordLayer, layer->sublayer[i]);
					layer->sublayer[i] = NULL;
				}
				eaSetSize(&layer->sublayer, 1);
			}
		}
		ret &= texWordVerifyLayer(texWord, layer->sublayer[0], fix, true, tps);
	}
	if (layer->subBlendWeight < 0 || layer->subBlendWeight > 1) {
		if (fix)
			layer->subBlendWeight = MIN(MAX(layer->subBlendWeight, 0), 1);
		else
			ret = false;
	}

	if (layer->filter) {
		int numfilters = eaSize(&layer->filter);
		int i;
		for (i=0; i<numfilters; i++) {
			TexWordLayerFilter *filter = layer->filter[i];
			if (filter->percent > 1.0) {
				if (fix)
					filter->percent = 1.0;
				else
					ret = false;
			}
			if (filter->magnitude < 0) {
				if (fix)
					filter->magnitude = 0;
				else
					ret = false;
			}
		}
	}

	return ret;
}

bool texWordVerify(TexWord *texWord, bool fix, TextParserState *tps) {
	bool ret=true;
	int numlayers;
	int i;

	if (!texWord)
		return false;

	if (!texWord->size[0] || ! texWord->size[1])
	{
		char buf[1024];
		BasicTexture *bind;
		getFileNameNoExt(buf, texWord->filename);
		bind = texFind(buf, true);
		if (bind)
		{
			texWord->size[0] = bind->width;
			texWord->size[1] = bind->height;
			if (0) // Fixup code for old TexWords that had implicit sizes
			{
				TexWordList dummy={0};
				char fullname[MAX_PATH];
				eaPush(&dummy.texWords, texWord);
				fileLocateWrite(texWord->filename, fullname);
				gimmeDLLDoOperation(fullname, GIMME_CHECKOUT, 0);
				ParserWriteTextFile(fullname, parse_TexWordList, &dummy, 0, 0);
				eaDestroy(&dummy.texWords);
			}
		} else {
			// Dynamic texwords need to have sizes!
			ErrorFilenamef(texWord->filename, "TexWord has no size specified, and has no base texture (or the texture is missing).");
			texWord->size[0] = 128;
			texWord->size[1] = 128;
		}
	}

	numlayers = eaSize(&texWord->layers);
	layerNameCount=numlayers-1;
	subLayerNameCount=0;
	for (i=0; i<numlayers; i++) {
		ret &= texWordVerifyLayer(texWord, texWord->layers[i], fix, false, tps);
	}
	return ret;
}

AUTO_FIXUPFUNC;
TextParserResult fixupTexWordList(TexWordList *twl, enumTextParserFixupType eType, void *pExtraData)
{
	TextParserState *tps = (TextParserState*)pExtraData;
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			int numTexWords=eaSize(&twl->texWords);
			int i;
			int ret=1;
			for (i=0; i<numTexWords; i++) {
				ret &= texWordVerify(twl->texWords[i], true, tps);
			}
		return ret;
		}
	}

	return 1;
}

AUTO_RUN;
void texWordsInitTPIs(void)
{
	ParserSetTableInfoRecurse(parse_TexWordList, sizeof(TexWordList), "TexWordList", NULL, __FILE__, NULL, SETTABLEINFO_NAME_STATIC | SETTABLEINFO_ALLOW_CRC_CACHING);
}

void texWordsLoadInfo(void)
{
	// Free old stuff
	if (htTexWords) {
		stashTableDestroy(htTexWords);
		htTexWords = 0;
	}
	if (texWords_list.texWords) {
		StructDeInit(parse_TexWordList, &texWords_list);
	}

//	if (isProductionMode() && gfx_state.texWordEdit[0]) {
//		// In texWordEdit production, tack on any extra ones sitting around on disk
//		ParserLoadFiles("texts", ".texword", NULL, PARSER_BINS_ARE_SHARED, parse_TexWordList, &texWords_list);
//	} else {
		ParserLoadFiles("texts", ".texword", "texWords.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_TexWordList, &texWords_list);
//	}

	htTexWords = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);

	{
		int num_structs, i;
		int count=0, count2=0;

		//Clean up TexWord names, and sort them
		num_structs = eaSize(&texWords_list.texWords);
		qsort(texWords_list.texWords, num_structs, sizeof(void*), (int (*) (const void *, const void *)) texWordsNameCmp);

		// Add entries of the appropriate locale to the hashtable
		for (i=0; i<num_structs; i++) {
			StashElement element;
			TexWord *texWord = texWords_list.texWords[i];
			// texWord->filename = "texts/Locale/path/texturename.texword"
			char locale[MAX_PATH];
			char textureName[MAX_PATH];
			char textureNameBase[MAX_PATH];
			bool bNeedAllocateDynamic=false;
			const char *sConst = texWord->filename;
			char *s2;
			if (strStartsWith(sConst, "texts"))
				sConst+=strlen("texts");
			while (*sConst=='/' || *sConst=='\\') sConst++;
			strcpy(locale, sConst);
			forwardSlashes(locale);
			s2 = strchr(locale, '/');
			*s2=0;
			// Extract texture name
			strcpy(textureName, getFileNameConst(texWord->filename));
			s2 = strrchr(textureName, '.');
			*s2=0;
			strcpy(textureNameBase, textureName);
			if (!texFind(textureNameBase, 0)) {
				bNeedAllocateDynamic = true;
			}
			if (stricmp(locale, texWordsLocale)==0 || texWordsLocale2[0] && stricmp(locale, texWordsLocale2)==0) {
				// Good!
				count++;
			} else {
				// Add in under specific locale
				strcat(textureName, "#");
				strcat(textureName, locale);
			}
			stashFindElement(htTexWords, textureName, &element);
			if (element) {
				if (texWordsLocale2[0]) {
					// Multiple locales (#UK;#English) might cause this, use locale2
					if (stricmp(locale, texWordsLocale2)==0) {
						// Use this one, remove the old one
						stashRemovePointer(htTexWords, textureName, NULL);
					}
				} else {
					Errorf("Duplicate TexWord defined:\n%s\n%s", ((TexWord*)stashElementGetPointer(element))->filename, texWord->filename);
				}
			}
			stashAddPointer(htTexWords, textureName, texWord, false);

			if (bNeedAllocateDynamic) { // After stashAddPointer
				// This is probably a dynamic texture that needs a BasicTexture created to hold it
				verify(texAllocateDynamic(textureNameBase, true));
			}
		}

		// Fix up textures on reload
		for (i=eaSize(&g_basicTextures)-1; i>=0; i--) {
			BasicTexture *bind = g_basicTextures[i];
			TexWord *texWord = texWordFind(bind->name, 0);
			BasicTextureRareData *rare_data = texGetRareData(bind);
			if (texWord)
			{
				if (!rare_data)
					rare_data = texAllocRareData(bind);
				rare_data->texWord = texWord;
				rare_data->origWidth = bind->width;
				rare_data->origHeight = bind->height;
				count2++;
				if (rare_data->baseTexture != bind) // bind->fullname == "dynamicTexture"
					texSetupParametersFromBase(bind);
			} else {
				if (texGetTexWord(bind))
				{
					rare_data->texWord = NULL;
				}
			}

			if (bind->tex_is_loaded && rare_data && (rare_data->hasBeenComposited || rare_data->texWord))
			{
				// Free the old composited data
				texGenFreeNextFrame(bind);
			}
		}
		if (count2) {
			verbose_printf("Loaded %d texWords for current locale, %d textures assigned texWords\n", count, count2);
		} else {
			verbose_printf("Loaded %d texWords for current locale\n", count);
		}
	}
}

bool texWordsNeedsReload = false;
static void reloadTexWordCallback(const char *relpath, int when){
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	texWordsNeedsReload = true;
}

void texWordsCheckReload(void) {
	if (texWordsNeedsReload && isDevelopmentMode()) {
		if (texWordGetLoadsPending()==0) {
			texWordsNeedsReload = false;
			texWordsReloadText(NULL);
			texWordsLoad(texWordsLocale);
		} else {
			printf("Delaying reload until TexWordRenderer is finished (%d remaining)\n", texWordGetLoadsPending());
			texWord_doNotYield=true;
		}
	}
}

static bool g_allow_loading = false;
void texWordsAllowLoading(bool allow)
{
	g_allow_loading = allow;
}

void texWordsReloadText(const char *localeName)
{
	static char *lastLocale;
	// Reload appropriate message store
	char textResourcePath[512];
	if (!localeName)
		localeName = lastLocale;
	else {
		SAFE_FREE(lastLocale);
		lastLocale = strdup(localeName);
	}
	if (stricmp(localeName, "Base")==0) {
		localeName = "English";
	}
	if(texWordsMessages){
		destroyMessageStore(texWordsMessages);
		texWordsMessages = NULL;
	}
	texWordsMessages = createMessageStore(0);
	sprintf(textResourcePath, "texts\\%s\\texture_library\\", localeName);
	initMessageStore( texWordsMessages, 0, NULL);
	msAddMessageDirectory(texWordsMessages, textResourcePath);
}


void texWordsCheckCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	texWordsCheck();
}


// Sets the locale used for textures
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(texLocale) ACMD_CATEGORY(Debug, Graphics) ACMD_HIDE;
void texWordsLoad(const char *localeName)
{
	static	int inited=0;
	static char override[32];
	char messageStoreName[MAX_PATH]={0};
	char searchPath[MAX_PATH];
	char *platform;
	const char *effLocaleName=localeName;

	if (!g_allow_loading) {
		strcpy(override, localeName);
		return;
	} else if (override[0]) {
		effLocaleName = localeName = override;
	}

	
	texWordsReloadText(localeName);

	strcpy(texWordsLocale2, "");
	// Set the search path
#if _XBOX
	platform = "Xbox360";
#else
	platform = "PC";
#endif
	if (stricmp(effLocaleName, "English")==0) {
		if (strstriConst(regGetAppName(), "EU")) {
			// Assume Britain
			sprintf(searchPath, "#UK-%s;#UK;#%s-%s;#%s;#%s", platform, effLocaleName, platform, effLocaleName, platform);
			strcpy(texWordsLocale2, "UK");
		} else {
			// US
			// #English-Xbox360;#English;#Xbox360
			sprintf(searchPath, "#%s-%s;#%s;#%s", effLocaleName, platform, effLocaleName, platform);
		}
	} else {
		// #French;#English or just #French?
		sprintf(searchPath, "#%s-%s;#%s;#%s;#English", effLocaleName, platform, effLocaleName, platform);
	}
	texSetSearchPath(searchPath);

	strcpy(texWordsLocale, localeName);

	if (texWordGetLoadsPending()!=0) {
		printf("Not reloading texWords immediately...\n");
		texWordsNeedsReload=true;
		override[0] = 0;
		return;
	}
	texWord_doNotYield=false;

	// Load the layout information (all locales loaded, keeps hashtable to current locale)
	texWordsLoadInfo();

	// Should run this at the end (only happens while reloading)
	texCheckSwapList();

	// Only in the tweditor
	if (texWords_reloadCallback)
		texWords_reloadCallback();

	if (!inited) {
		// Add callback for re-loading texWord files
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "texts/*.texword", reloadTexWordCallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "texts/*.ms", reloadTexWordCallback);
		// Timed callback to do periodic checks for the case of texwords which queued only a font load, not a texture load
		TimedCallback_Add(texWordsCheckCallback, NULL, 0.5f);
		inited = 1;
	}
	override[0] = 0;
}

static U8* getFontDFTextureBufferData(BasicTexture* tex);


static void queueFontDataTexLoad(GfxFontData* fontData)
{
	if (!fontData) return;
	
	texLoadRawData(fontData->pFontTexture->actualTexture->name, TEX_LOAD_IN_BACKGROUND, WL_FOR_FONTS);

	queueFontDataTexLoad(fontData->substitutionBold);
	queueFontDataTexLoad(fontData->substitutionBoldItalic);
	queueFontDataTexLoad(fontData->substitutionItalic);
	queueFontDataTexLoad(fontData->substitutionMissingGlyphs);
}

static bool isDataTexIsLoaded(GfxFontData* fontData)
{
	if (!fontData) return true;
	if (!getFontDFTextureBufferData(fontData->pFontTexture)) return false; //we cant load it here

	if (!isDataTexIsLoaded(fontData->substitutionBold)) return false;
	if (!isDataTexIsLoaded(fontData->substitutionBoldItalic)) return false;
	if (!isDataTexIsLoaded(fontData->substitutionItalic)) return false;
	if (!isDataTexIsLoaded(fontData->substitutionMissingGlyphs)) return false;

	return true;
}

static void unloadFontDataTex(GfxFontData* fontData)
{
	if (!fontData) return;
	texUnloadRawData(fontData->pFontTexture);

	unloadFontDataTex(fontData->substitutionBold);
	unloadFontDataTex(fontData->substitutionBoldItalic);
	unloadFontDataTex(fontData->substitutionItalic);
	unloadFontDataTex(fontData->substitutionMissingGlyphs);
}

static VOID CALLBACK texWordLayerLoadFont(void *dwParam)
{
	TexWordLayerFont *font = (TexWordLayerFont *)dwParam;

	PERFINFO_AUTO_START("texWordLayerLoadFont", 1);
	{
		GfxFontData* chosenFont = gfxFontGetFontData(font->fontName);
		GfxFontData* fallbackFont = gfxFontGetFallbackFontData();
		GfxFontData* sansFont = g_font_Sans.fontData;
		
		queueFontDataTexLoad(chosenFont);
		//also always load sans and the fallback since they could be used if there are missing glyphs or fonts
		queueFontDataTexLoad(fallbackFont);
		queueFontDataTexLoad(sansFont);
	}
	PERFINFO_AUTO_STOP();
}


static void texWordLayerLoadDeps(TexWordLayer *layer, TexLoadHow mode, WLUsageFlags use_category, BasicTexture *texBindParent)
{
	if (layer->hidden)
		return;
	if (layer->sublayer && layer->sublayer[0])
		texWordLayerLoadDeps(layer->sublayer[0], mode, use_category, texBindParent);
	switch(layer->type) {
		xcase TWLT_BASEIMAGE:
			// Assume already loaded, as only the loading of this calls this function
			layer->image = texLoadRawData(texBindParent->name, mode, use_category);
			assert(layer->image);
		xcase TWLT_IMAGE:
			layer->image = texLoadRawData(layer->imageName, mode, use_category);
			assert(layer->image);
		xcase TWLT_TEXT:
			texWordLayerLoadFont(&layer->font);
	}
}

void texWordLoadDeps(TexWord *texWord, TexLoadHow mode, WLUsageFlags use_category, BasicTexture *texBindParent)
{
	int i, numlayers = eaSize(&texWord->layers);

	for (i=0; i<numlayers; i++) {
		texWordLayerLoadDeps(texWord->layers[i], mode, use_category, texBindParent);
	}
}

static bool checkEverythingIsLoadedLayer(TexWordLayer *layer, BasicTexture *texBindParent, bool bAssert)
{
	bool ret=true;
	if (layer->hidden)
		return true;
	if (layer->sublayer && layer->sublayer[0])
		ret &= checkEverythingIsLoadedLayer(layer->sublayer[0], texBindParent, bAssert);
	switch(layer->type) {
		xcase TWLT_BASEIMAGE:
		case TWLT_IMAGE:
			assert(layer->image);
			assert(texGetRawReferenceCount(layer->image->actualTexture));
			if (!(layer->image->actualTexture->tex_is_loaded & RAW_DATA_BITMASK)) {
				assert(layer->image->actualTexture->loaded_data->tex_is_loading_for & RAW_DATA_BITMASK);
				ret = false;
				if (bAssert)
					assertmsgf(0, "required texture (%s) not loaded", layer->image->actualTexture->name);
			}

		xcase TWLT_TEXT:
			{
				GfxFontData* chosenFont = gfxFontGetFontData(layer->font.fontName);
				GfxFontData* fallbackFont = gfxFontGetFallbackFontData();
				GfxFontData* sansFont = g_font_Sans.fontData;

				//also always load sans and the fallback since they could be used if there are missing glyphs or fonts
				ret &= (isDataTexIsLoaded(chosenFont)  && isDataTexIsLoaded(fallbackFont) && isDataTexIsLoaded(sansFont));
			}
			
	}
	return ret;
}

static bool checkEverythingIsLoaded(TexWord *texWord, BasicTexture *texBindParent, bool bAssert)
{
	// Could dynamically load stuff here

	// Sanity checks
	int i, numlayers = eaSize(&texWord->layers);
	bool ret = true;

	for (i=0; i<numlayers; i++) {
		ret &= checkEverythingIsLoadedLayer(texWord->layers[i], texBindParent, bAssert);
	}
	return ret;
}

static void unloadDataAfterCompositionLayer(TexWordLayer *layer)
{
	if (layer->hidden)
		return;
	if (layer->sublayer && layer->sublayer[0])
		unloadDataAfterCompositionLayer(layer->sublayer[0]);
	switch(layer->type) {
		xcase TWLT_BASEIMAGE:
		case TWLT_IMAGE:
			if (layer->image)
				texUnloadRawData(layer->image);
		xcase TWLT_TEXT:
			{
				GfxFontData* chosenFont = gfxFontGetFontData(layer->font.fontName);
				GfxFontData* fallbackFont = gfxFontGetFallbackFontData();
				GfxFontData* sansFont = g_font_Sans.fontData;
				unloadFontDataTex(chosenFont);
				unloadFontDataTex(fallbackFont);
				unloadFontDataTex(sansFont);
			}
	}
}

static void unloadDataAfterComposition(TexWord *texWord)
{
	int i, numlayers = eaSize(&texWord->layers);

	for (i=0; i<numlayers; i++) {
		unloadDataAfterCompositionLayer(texWord->layers[i]);
	}
}

static Color makeColor(EArrayIntHandle colors)
{
	Color ret;
	if (eaiSize(&colors)==3) {
		ret.r = colors[2];
		ret.g = colors[1];
		ret.b = colors[0];
		ret.a = 0xff;
	} else if (eaiSize(&colors)==4) {
		ret.r = colors[2];
		ret.g = colors[1];
		ret.b = colors[0];
		ret.a = colors[3];
	} else {
		ret = ColorMagenta;
	}
	return ret;
}


static int bufferSizeX, bufferSizeY;
static void bufferSetSize(int x, int y)
{
	bufferSizeX=x;
	bufferSizeY=y;
}
typedef enum PBName {
	PB_LASTLAYER,
	PB_THISLAYER,
	PB_SUBLAYER,
	PB_TEMP,
} PBName;
static U8* pixbuffers[4] = {0};
static U8* getBuffer(PBName buf) {
	if (!pixbuffers[buf]) {
		pixbuffers[buf] = malloc(bufferSizeX*bufferSizeY*4);
	}
	return pixbuffers[buf];
}
static void freeBuffers()
{
	int i;
	for (i=0; i<ARRAY_SIZE(pixbuffers); i++) {
		SAFE_FREE(pixbuffers[i]);
	}
}
static void swapBuffers(PBName a, PBName b)
{
	U8* temp=pixbuffers[a];
	pixbuffers[a] = pixbuffers[b];
	pixbuffers[b] = temp;
}

static int cleanupColors(Color rgba[4], TexWordLayer *layer)
{
	int i;
	int numColors;
	if (eaiSize(&(layer->rgbas[0]))) {
		for (i=0; i<4; i++) {
			rgba[i] = makeColor(layer->rgbas[i]);
		}
		numColors = 4;
	} else if (eaiSize(&(layer->rgba))) {
		for (i=0; i<4; i++) {
			rgba[i] = makeColor(layer->rgba);
		}
		numColors = 1;
	} else {
		numColors = 0;
	}
	// Simplify colors
	if (numColors==4) {
		if (rgba[0].integer_for_equality_only==rgba[1].integer_for_equality_only &&
			rgba[1].integer_for_equality_only==rgba[2].integer_for_equality_only &&
			rgba[2].integer_for_equality_only==rgba[3].integer_for_equality_only)
			numColors = 1;
	}
	if (numColors==1) {
		if (rgba[0].integer_for_equality_only == ColorWhite.integer_for_equality_only)
			numColors = 0;
	}
	return numColors;
}


// rotate the point x, y around ptx,pty by Rot degrees
void rotatePointAroundPoint(int *x, int *y, int ptx, int pty, F32 rot)
{
	Vec3 rotmat;
	Vec3 pos;
	Vec3 pos2;
	Mat3 mat;
	// Move upper left corner based on rotation
	rotmat[0] = rotmat[1] = 0;
	rotmat[2] = rot*PI/180;
	createMat3YPR(mat, rotmat);
	setVec3(pos, *x - ptx, *y - pty, 0);
	mulVecMat3(pos, mat, pos2);
	*x = ptx + pos2[0];
	*y = pty + pos2[1];
}

typedef struct TexWordImage {
	U8 *buffer;
	int sizeX, sizeY;
	int pitch;
} TexWordImage;

static void renderTexWordImage(TexWordImage *dstImage, TexWordImage* srcImage, int x, int y, int sizeX, int sizeY, F32 rot, Color rgba[], int numColors, int blend, bool yield)
{

	if (sizeX == srcImage->sizeX && sizeY == srcImage->sizeY && rot==0 && !blend) // TODO: support blending on the other draw functions?
	{
		int srcy;
		int w = srcImage->sizeX;
		int h = srcImage->sizeY;
		int xoffs=0, yoffs=0;
		U8 *buffer = dstImage->buffer;

		if (x + w > dstImage->sizeX) {
			w = dstImage->sizeX - x;
		}
		if (y + h > dstImage->sizeY) {
			h = dstImage->sizeY - y;
		}
		if (x<0) {
			xoffs = -x;
			w-=xoffs;
		}
		if (y<0) {
			yoffs = -y;
		}
		if (numColors == 0)
		{
			lsprintf("Blt...");
			w*=4;
			for (srcy=yoffs; srcy<h; srcy++) {
				memcpy(&buffer[((y+srcy)*bufferSizeX + x+xoffs)<<2], &srcImage->buffer[srcy*srcImage->pitch + (xoffs<<2)], w);
				texWordsPixelsRendered(w, yield);
			}
			leprintf("done.");
		} else {
			Color interp;
			Color left, right;
			F32 horizw, vertw;
			lsprintf("BltColorized...");
			interp = rgba[0]; // for 1-color mode
			for (srcy=yoffs; srcy<h; srcy++) {
				int srcx;
				Color *dst=(Color*)&buffer[((y+srcy)*bufferSizeX + x+xoffs)<<2];
				Color *src=(Color*)&srcImage->buffer[srcy*srcImage->pitch + (xoffs<<2)];
#define UL 2
#define UR 3
#define LR 0
#define LL 1
				if (numColors!=1) {
					// four-color version
					// THIS IS SLOW!  Could slope-walk the color values, MMX multiply them
					// But, this is pretty much only used on loading screens, if anywhere
					vertw = srcy/(F32)srcImage->sizeY;
					left.r = rgba[UL].r*vertw + rgba[LL].r*(1-vertw);
					left.g = rgba[UL].g*vertw + rgba[LL].g*(1-vertw);
					left.b = rgba[UL].b*vertw + rgba[LL].b*(1-vertw);
					left.a = rgba[UL].a*vertw + rgba[LL].a*(1-vertw);
					right.r = rgba[UR].r*vertw + rgba[LR].r*(1-vertw);
					right.g = rgba[UR].g*vertw + rgba[LR].g*(1-vertw);
					right.b = rgba[UR].b*vertw + rgba[LR].b*(1-vertw);
					right.a = rgba[UR].a*vertw + rgba[LR].a*(1-vertw);
				}
				if (numColors!=1) {
					// four-color version
					for (srcx=0; srcx<w; srcx++, dst++, src++) {
						horizw = srcx/(F32)w;
						interp.r = left.r * horizw + right.r * (1-horizw);
						interp.g = left.g * horizw + right.g * (1-horizw);
						interp.b = left.b * horizw + right.b * (1-horizw);
						interp.a = left.a * horizw + right.a * (1-horizw);
						dst->r = src->r*interp.r>>8;
						dst->g = src->g*interp.g>>8;
						dst->b = src->b*interp.b>>8;
						dst->a = src->a*interp.a>>8;
					}
				} else {
					// single-color version
					for (srcx=0; srcx<w; srcx++, dst++, src++) {
						dst->r = src->r*interp.r>>8;
						dst->g = src->g*interp.g>>8;
						dst->b = src->b*interp.b>>8;
						dst->a = src->a*interp.a>>8;
					}
				}
				texWordsPixelsRendered(w*4, yield);
			}
			leprintf("done.");
		}
	} else {
		CDXSurface src, dst;
		RECT r;
		int destx, desty;
		double scalex, scaley;
		lsprintf("DrawRotoZoom...");
		r.left = 0;
		r.right = srcImage->sizeX;
		r.top = 0;
		r.bottom = srcImage->sizeY;
		src.buffer = srcImage->buffer;
		src.clipRect.top = 0;
		src.clipRect.bottom = srcImage->sizeY;
		src.clipRect.left = 0;
		src.clipRect.right = srcImage->sizeX;
		src.pitch = srcImage->pitch;
		dst.buffer = dstImage->buffer;
		dst.clipRect.top = 0;
		dst.clipRect.bottom = dstImage->sizeY;
		dst.clipRect.left = 0;
		dst.clipRect.right = dstImage->sizeX;
		dst.pitch = dstImage->pitch;

		destx = x + sizeX/2;
		desty = y + sizeY/2;
		if (rot) {
			// rotate the point destx, desty around x,y by Rot degrees
			rotatePointAroundPoint(&destx, &desty, x, y, rot);
		}
		scalex = sizeX/(double)srcImage->sizeX;
		scaley = sizeY/(double)srcImage->sizeY;
		DrawBlkRotoZoom(&src, &dst, destx, desty, &r, rot*PI/180, scalex, scaley, rgba, numColors, blend);
		leprintf("done.");
		texWordsPixelsRendered(srcImage->sizeX*srcImage->sizeY*4, yield);
	}
}

void renderTexWordLayerImageSoftware(PBName target, TexWordLayer *layer, int x, int y, int sizeX, int sizeY, int screenSizeX, int screenSizeY, F32 rot, bool yield)
{
	BasicTexture *image = layer->image->actualTexture;
	U8*	buffer = getBuffer(target);
	Color rgba[4] = {{0xff,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff}};
	int numColors;
	TexWordImage src, dest;
	BasicTextureRareData *rare_data = texGetRareData(image);
	TexReadInfo *rawInfo = SAFE_MEMBER(rare_data, bt_rawInfo);

	// If you can't find the image, then rawInfo will be null. 
	if (!rawInfo)
		return;
	
	verify(uncompressRawTexInfo(rawInfo,textureMipsReversed(image)));

	numColors = cleanupColors(rgba, layer);

	src.buffer = rawInfo->texture_data;
	src.sizeX = rawInfo->width;
	src.sizeY = rawInfo->height;
	src.pitch = imgMinPitch(rawInfo->tex_format, rawInfo->width);
	dest.buffer = buffer;
	dest.pitch = bufferSizeX*4;
	dest.sizeX = screenSizeX;
	dest.sizeY = screenSizeY;
	renderTexWordImage(&dest, &src, x, y, sizeX, sizeY, rot, rgba, numColors, 0, yield);

}

void renderTexWordLayerImage(PBName target, TexWordLayer *layer, int x, int y, int sizeX, int sizeY, int screenSizeX, int screenSizeY, F32 rot, bool yield)
{
	memset(getBuffer(target), 0, bufferSizeX*bufferSizeY*4);
	texWordsPixelsRendered(sizeX*sizeY, yield);
	if (layer->stretch == TWLS_TILE) {
		int x0=x, y0=y;
		int xwalk, ywalk;
		while (x0 > 0) x0 -= sizeX;
		while (y0 > 0) y0 -= sizeY;
		for (ywalk=y0; ywalk< screenSizeY; ywalk+=sizeY) {
			for (xwalk=x0; xwalk < screenSizeX; xwalk+=sizeX) {
				int x2=xwalk, y2=ywalk;
				rotatePointAroundPoint(&x2, &y2, x, y, rot);
                renderTexWordLayerImageSoftware(target, layer, x2, y2, sizeX, sizeY, screenSizeX, screenSizeY, rot, yield);
			}
		}
	} else {
		renderTexWordLayerImageSoftware(target, layer, x, y, sizeX, sizeY, screenSizeX, screenSizeY, rot, yield);
	}
}

static U8* getFontDFTextureBufferData(BasicTexture* tex)
{
	U8* ret = 0;
	TexReadInfo *rawInfo;
	
	EnterCriticalSection(&criticalSectionUpdatingFontTextureRawInfo);

	if (!(tex->actualTexture->tex_is_loaded & RAW_DATA_BITMASK))
	{
		LeaveCriticalSection(&criticalSectionUpdatingFontTextureRawInfo);
		return NULL; //not loaded yet
	}

	rawInfo = texGetRareData(tex)->bt_rawInfo;

	//these are all distance fields
	if (rawInfo->tex_format == TEXFMT_RAW_DDS) 
	{
		int w=0, h=0, bitdepth, total_size;
		U8* buffer = dxtDecompressMemRef(&w, &h, &bitdepth, &total_size, rawInfo->texture_data );

		texReadInfoAssignMemRefAlloc(rawInfo, buffer, tex);
		rawInfo->width = w;
		rawInfo->height = h;
		rawInfo->size = total_size;
		if (bitdepth == 1) {
			rawInfo->tex_format = RTEX_A_U8;
		} else {
			assertmsg(0, "bad bit depth: You sure this font texture is U8 format?");
		}
	}
	assertmsg(rawInfo->tex_format == RTEX_A_U8, "bad format: You sure this font texture is U8 format?");

	ret = rawInfo->texture_data;

	LeaveCriticalSection(&criticalSectionUpdatingFontTextureRawInfo);
	return ret;
}

typedef struct TextRenderData {
	U8 * buffer;
	int sizeX, sizeY;
	int screenSizeX;
	int screenSizeY;
	int numColors;
	int x, y;
	F32 rot;
	bool yield;
} TextRenderData;

static void textRenderCallback(TextRenderData *renderData, int x, int y, int sizeX, int sizeY, TexWordImage *glyph)
{
	TexWordImage dest;

	dest.buffer = renderData->buffer;
	dest.pitch = bufferSizeX*4;
	dest.sizeX = renderData->screenSizeX;
	dest.sizeY = renderData->screenSizeY;

	if (renderData->rot) {
		// Rotate the x and y location around the text origin
		rotatePointAroundPoint(&x, &y, renderData->x, renderData->y, renderData->rot);
		// Pass rotation down to DrawRotoZoom
	}

	renderTexWordImage(&dest, glyph, x, y, sizeX, sizeY, renderData->rot, 0, 0, 1, renderData->yield);
}


//This code is CPU version of dist_field_sprite_2d.phl if you change one, change the other

typedef struct CPUShader_LAYER_INFO
{
	Vec4 offset; //the zw components are not used, its just a float4 so it packs predictably into registers
	Vec4 densityRange; //min, outline, max, tightness
	Vec4 colorMain;
	Vec4 colorOutline;
} CPUShader_LAYER_INFO;

typedef struct CPUShader_PS_INPUT_SPRITE
{
	Vec4 texcoord;
	Vec4 color;
} CPUShader_PS_INPUT_SPRITE;

//Assumes the images are L8
static __forceinline void CPUShader_tex2D(TexWordImage* image, float x, float y, Vec4 outTexColor)
{
	float actualPosX = x * image->sizeX;
	float actualPosY = y * image->sizeY;
	float pixPosXf[2] = {floorf(actualPosX), ceilf(actualPosX)};
	float pixPosYf[2] = {floorf(actualPosY), ceilf(actualPosY)};
	int pixPosX[2] = {pixPosXf[0], pixPosXf[1]};
	int pixPosY[2] = {pixPosYf[0], pixPosYf[1]};
	float samples[4];
	float weights[4];

	//this should get unrolled by the optimizer
	int i,j;
	for (i = 0; i < 2; i++)
	{
		float wx;
		int ppx = pixPosX[i];
		
		ppx = CLAMP(ppx, 0, image->sizeX);
		if (i == 0)
			wx = 1.0 + pixPosXf[i] - actualPosX;
		else
			wx = (pixPosX[0] == pixPosX[1]) ? 0 : (1.0 + actualPosX - pixPosXf[i]);

		for (j = 0; j < 2; j++)
		{
			float wy;
			int ppy = pixPosY[j];

			ppy = CLAMP(ppy, 0, image->sizeY);
			if (j == 0)
				wy = 1.0 + pixPosYf[j] - actualPosY;
			else
				wy = (pixPosY[0] == pixPosY[1]) ? 0 : (1.0 + actualPosY - pixPosYf[j]);

			weights[i*2+j] = wx*wy;
			samples[i*2+j] = image->buffer[ppy*image->pitch+ppx]/255.0;
		}
	}
	
	setVec3same(outTexColor, 0);
	outTexColor[3] = samples[0]*weights[0] + samples[1]*weights[1] + samples[2]*weights[2] + samples[3]*weights[3];
}

static __forceinline void CPUShader_lerp4(Vec4 a, Vec4 b, float amt, Vec4 outVal)
{
	float invAmt = 1.0 - amt;
	outVal[0] = invAmt*a[0] + amt*b[0]; 
	outVal[1] = invAmt*a[1] + amt*b[1]; 
	outVal[2] = invAmt*a[2] + amt*b[2]; 
	outVal[3] = invAmt*a[3] + amt*b[3]; 
}

static __forceinline void CPUShader_lerp3(Vec4 a, Vec4 b, float amt, Vec4 outVal)
{
	float invAmt = 1.0 - amt;
	outVal[0] = invAmt*a[0] + amt*b[0]; 
	outVal[1] = invAmt*a[1] + amt*b[1]; 
	outVal[2] = invAmt*a[2] + amt*b[2]; 
}

static __forceinline F32 CPUShader_smoothstep(F32 minVal, F32 maxVal, F32 val)
{
	F32 f = saturate((val - minVal)/(maxVal - minVal));
	return f * f * (3.0f - 2.0f * f);
}

//this is Jimb's cheaper replacement for smoothstep, using here to replicate shader behavior exactly
static __forceinline F32 CPUShader_madstep(F32 mn, F32 mx, F32 v)
{
	//want : saturate((v - mn) / (mx - mn))
	F32 delta = mx - mn;
	const F32 sharpen = 0.18; // increase tightness to get the same visual look/good AA
	F32 mulvalue;
	F32 addvalue;

	mx -= delta*sharpen;
	mn += delta*sharpen;
	mulvalue = 1.f/(mx - mn);
	addvalue = -mn * mulvalue;
	return saturate(v * mulvalue + addvalue);
}

static __forceinline void CPUShader_dist_field_sprite_2d(CPUShader_PS_INPUT_SPRITE* fragment, TexWordImage* texture_sampler, CPUShader_LAYER_INFO* layerSettings, int layer_count, Vec4 outColor)
{
	int i;
	setVec4same(outColor, 0);

	for (i = 0; i < layer_count ; i++)
	{
		Vec4 texcolor;
		Vec4 layerColor;
		float density;
		float minDen = layerSettings[i].densityRange[0];
		float outlineDen = layerSettings[i].densityRange[1];
		float maxDen = layerSettings[i].densityRange[2];
		float tightness = layerSettings[i].densityRange[3];
		Vec4 colorOutline;
		Vec4 colorMain;
		float isInsideShape;

		copyVec4(layerSettings[i].colorOutline, colorOutline);
		copyVec4(layerSettings[i].colorMain, colorMain);

		CPUShader_tex2D(texture_sampler, fragment->texcoord[0] - layerSettings[i].offset[0], fragment->texcoord[1] - layerSettings[i].offset[1], texcolor);
		density = texcolor[3];

		CPUShader_lerp4(colorOutline, colorMain, CPUShader_madstep(outlineDen - tightness, outlineDen + tightness, density), layerColor);
		isInsideShape = saturate(CPUShader_madstep(minDen - tightness, minDen + tightness, density) - CPUShader_smoothstep(maxDen - tightness, maxDen + tightness, density));
		layerColor[3] *= isInsideShape;

		if (i > 0)
		{
			//blend with previous layer
			CPUShader_lerp3(outColor, layerColor, layerColor[3], outColor); //lerp3 leaves.a or [3] alone
			outColor[3] = max(outColor[3], layerColor[3]);
		}
		else
		{
			copyVec4(layerColor, outColor);
		}
	} 

	
	outColor[0] *= fragment->color[0];
	outColor[1] *= fragment->color[1];
	outColor[2] *= fragment->color[2];
	outColor[3] *= fragment->color[3];
}




static void renderTexWordLayerText_SpriteOneLayerFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
											 float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
											 float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1,
											 float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	int glyphW = xscale * btex->width;
	int glyphH = yscale * btex->height;
	TexWordImage tempImage, srcTex;
	int i, j;
	
	if (glyphW < 2 || glyphH < 2) return; //this is too small don't render it

	tempImage.buffer = getBuffer(PB_TEMP);

	//assert(glyphW <= bufferSizeX && glyphH <= bufferSizeY);
	//if this happens, shrink it to the buffer. This sometimes happens if the expected size and rendering size are not
	//100% identical
	MIN1(glyphW, bufferSizeX);
	MIN1(glyphH, bufferSizeY);

	tempImage.sizeX = glyphW;
	tempImage.sizeY = glyphH;
	tempImage.pitch = glyphW*4;

	srcTex.buffer = getFontDFTextureBufferData(btex);
	srcTex.sizeX = btex->width;
	srcTex.sizeY = btex->height;
	srcTex.pitch = btex->width; //assume L8 format
	
	{
		CPUShader_PS_INPUT_SPRITE fragment;
		CPUShader_LAYER_INFO layerSettings[1];
		Vec4 outColor;
		Vec4 cornerColors[4];
		U8* outPixel = tempImage.buffer;
		
		setVec2(layerSettings[0].offset, uOffsetL1, vOffsetL1);
		//min, outline, max, tightness
		setVec4(layerSettings[0].densityRange, minDenL1, outlineDenL1, maxDenL1, tightnessL1);
		colorToVec4(layerSettings[0].colorMain, colorFromRGBA(rgbaMainL1));
		colorToVec4(layerSettings[0].colorOutline, colorFromRGBA(rgbaOutlineL1));

		colorToVec4(fragment.color, colorFromRGBA(rgbaOutlineL1));
		colorToVec4(cornerColors[0], colorFromRGBA(rgba));
		colorToVec4(cornerColors[1], colorFromRGBA(rgba2));
		colorToVec4(cornerColors[2], colorFromRGBA(rgba3));
		colorToVec4(cornerColors[3], colorFromRGBA(rgba4));

		for (i = 0; i < tempImage.sizeY; i++)
		{
			float relv = (float)i / (tempImage.sizeY - 1.0f);
			float v = lerp(v1, v2, relv);
			for (j = 0; j < tempImage.sizeX; j++)
			{
				float relu = (float)j / (tempImage.sizeX - 1.0f);
				float u = lerp(u1, u2, relu);
				float colorAmts[4] = {(1.0-relu)*(1.0-relv), (relu)*(1.0-relv), (relu)*(relv), (1.0 - relu)*(relv)};

				setVec4(fragment.texcoord, u, v, 0, 0);
				setVec4same(fragment.color, 0);
				scaleAddVec4(cornerColors[0], colorAmts[0], fragment.color, fragment.color);
				scaleAddVec4(cornerColors[1], colorAmts[1], fragment.color, fragment.color);
				scaleAddVec4(cornerColors[2], colorAmts[2], fragment.color, fragment.color);
				scaleAddVec4(cornerColors[3], colorAmts[3], fragment.color, fragment.color);

				CPUShader_dist_field_sprite_2d(&fragment, &srcTex, layerSettings, 1, outColor);

				outPixel[0] = (U8)(outColor[0]*255.0f);
				outPixel[1] = (U8)(outColor[1]*255.0f);
				outPixel[2] = (U8)(outColor[2]*255.0f);
				outPixel[3] = (U8)(outColor[3]*255.0f);
				outPixel += 4;
			}
		}
	}

	textRenderCallback(userData, xp, yp, glyphW, glyphH, &tempImage);
}

static void renderTexWordLayerText_SpriteTwoLayersFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
											  float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
											  float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, 
											  float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2, int rgbaMainL2, int rgbaOutlineL2,
											  float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	int glyphW = xscale * btex->width;
	int glyphH = yscale * btex->height;
	TexWordImage tempImage, srcTex;
	int i, j;

	if (glyphW < 2 || glyphH < 2) return; //this is too small don't render it

	tempImage.buffer = getBuffer(PB_TEMP);

	//assert(glyphW <= bufferSizeX && glyphH <= bufferSizeY);
	//if this happens, shrink it to the buffer. This sometimes happens if the expected size and rendering size are not
	//100% identical
	MIN1(glyphW, bufferSizeX);
	MIN1(glyphH, bufferSizeY);

	tempImage.sizeX = glyphW;
	tempImage.sizeY = glyphH;
	tempImage.pitch = glyphW*4;

	srcTex.buffer = getFontDFTextureBufferData(btex);
	srcTex.sizeX = btex->width;
	srcTex.sizeY = btex->height;
	srcTex.pitch = btex->width; //assume L8 format

	{
		CPUShader_PS_INPUT_SPRITE fragment;
		CPUShader_LAYER_INFO layerSettings[2];
		Vec4 outColor;
		Vec4 cornerColors[4];
		U8* outPixel = tempImage.buffer;

		setVec2(layerSettings[0].offset, uOffsetL1, vOffsetL1);
		//min, outline, max, tightness
		setVec4(layerSettings[0].densityRange, minDenL1, outlineDenL1, maxDenL1, tightnessL1);
		colorToVec4(layerSettings[0].colorMain, colorFromRGBA(rgbaMainL1));
		colorToVec4(layerSettings[0].colorOutline, colorFromRGBA(rgbaOutlineL1));

		setVec2(layerSettings[1].offset, uOffsetL2, vOffsetL2);
		//min, outline, max, tightness
		setVec4(layerSettings[1].densityRange, minDenL2, outlineDenL2, maxDenL2, tightnessL2);
		colorToVec4(layerSettings[1].colorMain, colorFromRGBA(rgbaMainL2));
		colorToVec4(layerSettings[1].colorOutline, colorFromRGBA(rgbaOutlineL2));
		
		colorToVec4(fragment.color, colorFromRGBA(rgbaOutlineL1));
		colorToVec4(cornerColors[0], colorFromRGBA(rgba));
		colorToVec4(cornerColors[1], colorFromRGBA(rgba2));
		colorToVec4(cornerColors[2], colorFromRGBA(rgba3));
		colorToVec4(cornerColors[3], colorFromRGBA(rgba4));

		for (i = 0; i < tempImage.sizeY; i++)
		{
			float relv = (float)i / (tempImage.sizeY - 1.0f);
			float v = lerp(v1, v2, relv);
			for (j = 0; j < tempImage.sizeX; j++)
			{
				float relu = (float)j / (tempImage.sizeX - 1.0f);
				float u = lerp(u1, u2, relu);
				float colorAmts[4] = {(1.0-relu)*(1.0-relv), (relu)*(1.0-relv), (relu)*(relv), (1.0 - relu)*(relv)};

				setVec4(fragment.texcoord, u, v, 0, 0);
				setVec4same(fragment.color, 0);
				scaleAddVec4(cornerColors[0], colorAmts[0], fragment.color, fragment.color);
				scaleAddVec4(cornerColors[1], colorAmts[1], fragment.color, fragment.color);
				scaleAddVec4(cornerColors[2], colorAmts[2], fragment.color, fragment.color);
				scaleAddVec4(cornerColors[3], colorAmts[3], fragment.color, fragment.color);

				CPUShader_dist_field_sprite_2d(&fragment, &srcTex, layerSettings, 2, outColor);

				outPixel[0] = (U8)(outColor[0]*255.0f);
				outPixel[1] = (U8)(outColor[1]*255.0f);
				outPixel[2] = (U8)(outColor[2]*255.0f);
				outPixel[3] = (U8)(outColor[3]*255.0f);
				outPixel += 4;
			}
		}
	}

	textRenderCallback(userData, xp, yp, glyphW, glyphH, &tempImage);
}


void renderTexWordLayerTextSoftware(PBName target, TexWordLayer *layer, int x, int y, int sizeX, int sizeY, int screenSizeX, int screenSizeY, unsigned char *text, GfxFont *font, Color rgba[], int numColors, F32 rot, bool yield)
{
	U8* buffer = getBuffer(target);
	F32 width, height;
	TextRenderData renderData;
	Vec2 outSize;
	F32 oldSize = font->renderSize;
	float oldAspect = font->aspectRatio;
	int i;
#define NUMLOOPS 15

	memset(buffer, 0x00, bufferSizeX*bufferSizeY*4);
	texWordsPixelsRendered(sizeX*sizeY, yield);

	for (i=0; i<NUMLOOPS; i++)
	{
		F32 xscale, yscale;
		gfxFontMeasureString(font, text, outSize);
		width = outSize[0];
		height = outSize[1];
		xscale = sizeX / width;
		yscale = sizeY / height;

		if (xscale > 1 && yscale > 1 && i>0) // We fit within the confines
			break;

		if (layer->font.fontAlign == TWALIGN_FILL)
		{
			// Fit the text into the box
			// full stretch, no x/y adjustment needed
		} else {
			// Center the text using the font's aspect ratio
			if (xscale > yscale)
			{
				xscale = yscale;
			} else {
				yscale = xscale;
			}
		}

		font->renderSize *= yscale;
		font->aspectRatio *=  xscale/yscale;
	}

	if (layer->font.fontAlign == TWALIGN_FILL)
	{
		// Fit the text into the box
		// full stretch, no x/y adjustment needed
	} else {
		// Center the text using the font's aspect ratio
		if (width < sizeX)
		{
			if (layer->font.fontAlign == TWALIGN_CENTER)
				x += (sizeX-width)/2;
			else if (layer->font.fontAlign == TWALIGN_RIGHT)
				x += sizeX-width-1;
		}
		if (height < sizeY) {
			y -= (sizeY - height)/2;
		}
	}

	renderData.sizeX = sizeX;
	renderData.sizeY = sizeY;
	renderData.buffer = buffer;
	renderData.screenSizeY = screenSizeY;
	renderData.screenSizeX = screenSizeX;
	renderData.numColors = numColors;
	renderData.yield = yield;
	renderData.x = x;
	renderData.y = y;
	renderData.rot = rot;

	gfxFontPrintExWithFuncs(font, x, y+sizeY, 0, text, 0, renderTexWordLayerText_SpriteOneLayerFunc, renderTexWordLayerText_SpriteTwoLayersFunc, &renderData);

	font->renderSize = oldSize;
	font->aspectRatio =  oldAspect;
}


void renderTexWordLayerText(PBName target, TexWordLayer *layer, int x, int y, int sizeX, int sizeY, int screenSizeX, int screenSizeY, F32 scaleX, F32 scaleY, BasicTexture *texBindParent, F32 rot, bool yield)
{
	
	char *text;
#define text_len 2048
	char *effText=NULL;
    Color rgba[4] = {{0xff,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff}};
	GfxFont *fontDefault = &g_font_Sans;
	GfxFont *font = fontDefault;
	int numColors;
	F32 effScale = (scaleX + scaleY)/2;

	numColors = cleanupColors(rgba, layer);

	// Setup font
	if (layer->font.fontName) {
		static GfxFont drawContext;
		int drawSize = layer->font.drawSize?layer->font.drawSize:(sizeY*2);
		GfxFontData* chosenFont;
		StructInit(parse_GfxFont, &drawContext);
		font = &drawContext;

		chosenFont = gfxFontGetFontData(layer->font.fontName);
		if (!chosenFont) {
			if (isDevelopmentMode())
				Errorf("Invalid font specified: %s", layer->font.fontName);
			chosenFont = fontDefault->fontData; // Error, font not found!
		} 

		gfxFontInitalizeFromData(font, chosenFont);
		font->renderSize = drawSize;
		font->italicize = layer->font.italicize;
		font->bold = layer->font.bold;
		font->dropShadow = layer->font.dropShadowOffset[0]||layer->font.dropShadowOffset[1];
		font->dropShadowOffset[0] = round(layer->font.dropShadowOffset[0]*scaleX);
		if (layer->font.dropShadowOffset[0] && !font->dropShadowOffset[0])
			font->dropShadowOffset[0] = 1;
		font->dropShadowOffset[1] = round(layer->font.dropShadowOffset[1]*scaleY);
		if (layer->font.dropShadowOffset[1] && !font->dropShadowOffset[1])
			font->dropShadowOffset[1] = 1;
		font->outlineWidth = layer->font.outlineWidth*effScale;
		if (layer->font.outlineWidth && !font->outlineWidth)
			font->outlineWidth = 1;
		font->softShadow = !!layer->font.softShadowSpread;
		font->softShadowSpread = round(layer->font.softShadowSpread*effScale);
		if (layer->font.softShadowSpread && !font->softShadowSpread)
			font->softShadowSpread = 1;
		
		font->color.uiTopLeftColor = RGBAFromColor(rgba[0]);
		font->color.uiTopRightColor = RGBAFromColor(rgba[1]);
		font->color.uiBottomRightColor = RGBAFromColor(rgba[2]);
		font->color.uiBottomLeftColor = RGBAFromColor(rgba[3]);
		
	}

	if (texGetTexWordParams(texBindParent)) {
		int index = StaticDefineIntGetInt(TexWordParamLookup, layer->text);
		if (index >=0 && index < eaSize(&texGetTexWordParams(texBindParent)->parameters)) {
			effText = texGetTexWordParams(texBindParent)->parameters[index];
		}
	} 
	if (!effText) {
		effText = layer->text;
	}

	text = ScratchAlloc(text_len);
	msPrintf(texWordsMessages, text, text_len, effText);

	renderTexWordLayerTextSoftware(target, layer, x, y, sizeX, sizeY, screenSizeX, screenSizeY, text, font, rgba, numColors, rot, yield);

	ScratchFree(text);
	text = NULL;

	if (font != fontDefault) {
		StructDeInit(parse_GfxFont, font);
	}
}

#pragma warning(push)
#pragma warning(disable:4730)

static void blendLayers(U8* dest, U8* bottom, U8* top, TexWordBlendType blend, F32 topWeight, bool yield)
{
	Color t,b,*d;
	int i, j;
	U8 topWeightByte = topWeight * 255;
	U64 topWeightMMX;
	U8 invTopWeightByte = 255 - topWeightByte;
	U64 invTopWeightMMX;
	F32	float255 = 255.0f;
	if (blend == TWBLEND_REPLACE) {
		texWordsPixelsRendered(bufferSizeX*bufferSizeY, yield);
		memcpy(dest, bottom, bufferSizeX*bufferSizeY*4);
		return;
	}

	if (topWeight!=255) {
		topWeightMMX = topWeightByte<<16|topWeightByte;
		topWeightMMX<<=32;
		topWeightMMX |= topWeightByte<<16|topWeightByte;
		topWeightMMX<<=8;
		invTopWeightMMX = invTopWeightByte<<16|invTopWeightByte;
		invTopWeightMMX<<=32;
		invTopWeightMMX |= invTopWeightByte<<16|invTopWeightByte;
		invTopWeightMMX<<=8;
	}
	for (i=0; i<bufferSizeY; i++) { // height
		Color *rowdest=(Color *)&dest[bufferSizeX*4*i];
		Color *rowbottom=(Color *)&bottom[bufferSizeX*4*i];
		Color *rowtop=(Color *)&top[bufferSizeX*4*i];
		for (j=0; j<bufferSizeX; j++) {
			t = rowtop[j];
			b = rowbottom[j];
			d = &rowdest[j];
			if (topWeightByte==0) {
				*d = b;
				continue;
			}
			switch (blend) {
				case TWBLEND_OVERLAY:
					if (t.a == 0) {
						*d = b;
					} else if (t.a == 255) {
						*d = t;
					} else {
						F32 topAlpha = t.a*U8TOF32_COLOR;
						F32 bottomAlpha = b.a*U8TOF32_COLOR;
						F32 bottomAlphaFact = (1.0 - topAlpha)*bottomAlpha;
						F32 finalAlpha = topAlpha + bottomAlphaFact;
						F32 finalAlphaSaturateFact = 1.0/finalAlpha;
#if !USE_MMX
						// ftol calls happen here!
						d->r = (topAlpha * t.r + bottomAlphaFact * b.r)*finalAlphaSaturateFact;
						d->g = (topAlpha * t.g + bottomAlphaFact * b.g)*finalAlphaSaturateFact;
						d->b = (topAlpha * t.b + bottomAlphaFact * b.b)*finalAlphaSaturateFact;
						d->a = finalAlpha * 255;
#else
						// Same thing but without ftol calls
						// Also does rounding instead of truncating
						// Doesn't actually seem to be any faster
						DWORD temp0;
						DWORD temp1;
						// Load interesting values onto the stack
						_asm {
							fld		dword ptr[topAlpha]
							fld		dword ptr[bottomAlphaFact]
							fld		dword ptr[finalAlphaSaturateFact]
							// Now, st(0) = finalAlphaSaturateFact, st(1) = bottomAlphaFact, st(2) = topAlpha
						}
#define OVERLAY_BLEND_ASM(top, bot, out)				\
						temp0=top; temp1=bot;			\
														\
						_asm {fild	dword ptr[temp0]}	\
							/* Now, st(0) = t.r, st(1) = finalAlphaSaturateFact, st(2) = bottomAlphaFact, st(3) = topAlpha	*/ \
						_asm {fmul	st, st(3)}	/* top * topAlpha	*/	\
						_asm {fild	dword ptr[temp1]}	\
							/* Now, st(0) = b.r, st(1) = top*topAlpha, etc */	\
						_asm {fmul	st, st(3)}	/* bot * bottomAlphaFact */		\
						_asm {faddp	st(1), st}	/* st = st(0) + st(1), pop */	\
						_asm {fmul	st, st(1)}	/* *= finalAlphaSaturateFact */	\
						_asm {fistp	dword ptr[temp0]}	\
						out = temp0;

						OVERLAY_BLEND_ASM(t.r, b.r, d->r);
						OVERLAY_BLEND_ASM(t.g, b.g, d->g);
						OVERLAY_BLEND_ASM(t.b, b.b, d->b);
						_asm { // Remove excess stuff from the stack
							fstp	st
							fstp	st
							fstp	st
						}
						//d->a = finalAlpha * 255;
						_asm {
							fld		dword ptr[finalAlpha]
							fld		dword ptr[float255]
							fmulp	st(1), st
							fistp	dword ptr[temp0]
						}
						d->a = temp0;
#endif
					}
				xcase TWBLEND_MULTIPLY:
#if !USE_MMX
					d->r = t.r*b.r >> 8;
					d->g = t.g*b.g >> 8;
					d->b = t.b*b.b >> 8;
					d->a = t.a*b.a >> 8;
#else
					// Same thing in MMX (~12% speedup)
					__asm {
						pxor mm0,mm0
						punpcklbw mm0, dword ptr[t]
						pxor mm1,mm1
						punpcklbw mm1, dword ptr[b]
						pmulhuw mm0, mm1
						psraw mm0, 8
						packsswb mm0, mm1
						mov  edx,dword ptr [d]
						movd dword ptr[edx], mm0
					}
#endif
				xcase TWBLEND_ADD:
					if (t.a==0) {
						*d = b;
					} else if (b.a == 0) {
						*d = t;
					} else {
						// TODO: make this fast?
						F32 topAlpha = t.a*U8TOF32_COLOR;
						F32 bottomAlpha = b.a*U8TOF32_COLOR;
						F32 finalAlpha = MIN(1.0, topAlpha + bottomAlpha);
						if (finalAlpha==1.0) {
							// Just add
							d->r = MIN(t.r*topAlpha+b.r*bottomAlpha,255);
							d->g = MIN(t.g*topAlpha+b.g*bottomAlpha,255);
							d->b = MIN(t.b*topAlpha+b.b*bottomAlpha,255);
						} else {
							F32 invFinalAlpha=1.0/finalAlpha;
							// Add and saturate
							d->r = MIN((t.r*topAlpha+b.r*bottomAlpha)*invFinalAlpha,255);
							d->g = MIN((t.g*topAlpha+b.g*bottomAlpha)*invFinalAlpha,255);
							d->b = MIN((t.b*topAlpha+b.b*bottomAlpha)*invFinalAlpha,255);
						}
						d->a = finalAlpha*255;
					}
				xcase TWBLEND_SUBTRACT:
					d->r = MAX(b.r-t.r,0);
					d->g = MAX(b.g-t.g,0);
					d->b = MAX(b.b-t.b,0);
					d->a = MAX(b.a-t.a,0);
					break;
			}
			// TODO: is the loss of precision here (1.0*1.0 = 254/255) acceptable?
			if (topWeightByte!=255) {
#if !USE_MMX
				d->r = (d->r * topWeightByte + b.r * invTopWeightByte) >> 8;
				d->g = (d->g * topWeightByte + b.g * invTopWeightByte) >> 8;
				d->b = (d->b * topWeightByte + b.b * invTopWeightByte) >> 8;
				d->a = (d->a * topWeightByte + b.a * invTopWeightByte) >> 8;
#else
				// MMX version
				__asm {
					pxor mm0,mm0
					mov  edx,dword ptr [d]
					punpcklbw mm0, dword ptr[edx]
					pxor mm1,mm1
					movq mm1, qword ptr[topWeightMMX]
					pmulhuw mm0, mm1
					// mm0 contains d * topWeightByte in H.O.
					pxor mm2,mm2
					punpcklbw mm2, dword ptr[b]
					pxor mm1,mm1
					movq mm1, qword ptr[invTopWeightMMX]
					pmulhuw mm2, mm1
					// mm2 contains b * invTopWeightByte in W
					paddusw mm0, mm2
					// mm0 contains sum
					psraw mm0, 8
					packsswb mm0, mm1
					mov  edx,dword ptr [d]
					movd dword ptr[edx], mm0
					emms
				}
#endif
			}
		}
		texWordsPixelsRendered(bufferSizeX*4, yield);
	}
#if !USE_MMX
#else
	// Empty Machine State (reset FPU for FP ops instead of MMX)
	__asm {
		emms
	}
#endif
}

#pragma warning(pop)

static void filterNoop(U8* dest, U8* src, TexWordLayerFilter *filter, bool yield, F32 scale)
{
	Color s;
	int i, j;
	for (i=0; i<bufferSizeY; i++) {
		Color *rowdest=(Color *)&dest[bufferSizeX*4*i];
		Color *rowsrc=(Color *)&src[bufferSizeX*4*i];
		for (j=0; j<bufferSizeX; j++) {
			s = rowsrc[j];
			rowdest[j] = s;
		}
		texWordsPixelsRendered(bufferSizeX*4, yield);
	}
}

static void filterKernel(U8* dest, U8* src, S32 *kernel, int kwidth, int kheight, int offsx, int offsy, bool yield)
{
	int kx, ky, i, j, kmx = kwidth/2, kmy = kheight/2, x, y, ki, ybase, xbase;
	S32 r, g, b, a, w, pixw;
	register Color s;
	Color *d;
	for (i=0; i<bufferSizeY; i++) {
		Color *rowdest=(Color *)&dest[bufferSizeX*i<<2];
		ybase = i - kmy - offsy;
		for (j=0; j<bufferSizeX; j++) {
			xbase = j - kmx - offsx;
			r=g=b=a=w=0;
			ki=0;
			for (ky=0; ky<kheight; ky++) {
				y = ybase + ky;
				if (y>=0 && y<bufferSizeY) {
					Color *srcwalk = (Color *)&src[(bufferSizeX*y + xbase)<<2];
					for (kx=0; kx<kwidth; kx++, srcwalk++, ki++) {
						x = xbase + kx;
						if (x>=0 && x<bufferSizeX) {
							pixw=kernel[ki];
							if (pixw) {
								s = *srcwalk;
								w+=pixw;
								if (s.integer_for_equality_only)
								{
									r+=pixw*s.r;
									g+=pixw*s.g;
									b+=pixw*s.b;
									a+=pixw*s.a;
								}
							}
						}
					}
				} else {
					ki+=kwidth;
				}
			}
			d = &rowdest[j];
			if (w==0) {
				d->integer_for_equality_only = 0;
			} else {
				d->r = r/w;
				d->g = g/w;
				d->b = b/w;
				d->a = a/w;
			}
		}
		texWordsPixelsRendered(bufferSizeX*kwidth*kheight, yield);
	}
}

static int kernel_width;
static int *makeKernel(int magnitude)
{
	static int kernel[21*21] = {0};
	int mag = MIN(10, MAX(0, magnitude));
	int i, j;
	int dx, dy;
	kernel_width = mag*2+1;
	for (i=0; i<kernel_width; i++) { // y
		dy = mag - i;
		for (j=0; j<kernel_width; j++) { // x
			dx = mag - j;
			kernel[i*kernel_width+j] = MAX(0,round(mag + 1 - sqrt(dx*dx+dy*dy)));
		}
	}
	return kernel;
}

static void convKernel(int *intKernel, OUT F32 **fkernel, OUT U8 ** bkernelNotZero)
{
	static F32 kernel[21*21]={0};
	static U8 bkernel[21*21]={0};
	int i, j;
	int w=0;
	F32 invKernelWeight;
	for (i=0; i<kernel_width; i++) {
		for (j=0; j<kernel_width; j++) {
			w+=intKernel[i*kernel_width+j];
		}
	}
	invKernelWeight = 1.0/w;
	for (i=0; i<kernel_width; i++) {
		for (j=0; j<kernel_width; j++) {
			kernel[i*kernel_width+j] = intKernel[i*kernel_width+j]*invKernelWeight;
			bkernel[i*kernel_width+j] = kernel[i*kernel_width+j]!=0;
		}
	}
	*fkernel = kernel;
	*bkernelNotZero = bkernel;
}

static void filterBlur(U8* dest, U8* src, TexWordLayerFilter *filter, bool yield, F32 scale)
{
	int *kernel = makeKernel(filter->magnitude*scale);
	filterKernel(dest, src, kernel, kernel_width, kernel_width, filter->offset[0], -filter->offset[1], yield);
}

static void filterKernelColorize(U8* dest, U8* src, S32 *kernel, int kwidth, int kheight, int offsx, int offsy, Color color, U8 spread, bool yield)
{
	int kx, ky, i, j, kmx = kwidth/2, kmy = kheight/2, x, y, ki, ybase, xbase;
	S32 r, g, b, a, w, pixw;
	Color s, *d;
	F32 alphaScale = color.a*U8TOF32_COLOR;
	U8 invspread = 255-spread;
	// Slow function, not actually used

	texWordsPixelsRendered(bufferSizeX*bufferSizeY*kwidth*kheight, yield);

	for (i=0; i<bufferSizeY; i++) { // loop over destination pixels
		Color *rowdest=(Color *)&dest[bufferSizeX*i<<2];
		ybase = i - kmy - offsy;
		for (j=0; j<bufferSizeX; j++) {
			xbase = j - kmx - offsx;
			r=g=b=a=w=0;
			ki=0;
			for (ky=0; ky<kheight; ky++) {
				y = ybase + ky;
				if (y>=0 && y<bufferSizeY) {
					Color *srcwalk = (Color *)&src[(bufferSizeX*y + xbase)<<2];
					for (kx=0; kx<kwidth; kx++, srcwalk++, ki++) {
						x = xbase + kx;
						if (x>=0 && x<bufferSizeX) {
							pixw=kernel[ki];
							if (pixw) {
								s = *srcwalk;
								w+=pixw;
								if (s.a) {
									a+=pixw*s.a;
								}
							}
						}
					}
				} else {
					ki+=kwidth;
				}
			}
			d = &rowdest[j];
			if (w==0) {
				d->integer_for_equality_only = 0;
			} else {
				*d = color; // Write RGB (A gets overwritten)
				if (spread) {
					U8 effa = a/w; // 0..255
					if (effa >= invspread) {
						if (a) {
							d->a = alphaScale * 255;
						} else {
							d->a = 0;
						}
					} else {
						// Map 0..invspread to 0..255
						d->a = a*255/(w*invspread)*alphaScale;
					}
				} else {
					d->a = a*alphaScale/w;
				}
			}
		}
	}
}

// Applies a spread to the alpha, unpacks a buffer full of just alpha into rgba
static void filterSpreadColorUnpack(U8* buffer, U8 spread, Color c, bool yield)
{
	Color s;
	int i, j;
	U8 invspread = 255-spread;
	F32 alphaFactor = c.a*U8TOF32_COLOR;

	s = c;
	for (i=bufferSizeY-1; i>=0; i--) {
		Color *rowout=(Color *)&buffer[bufferSizeX*i<<2];
		U8 *rowsrc=&buffer[bufferSizeX*i];
		for (j=bufferSizeX-1; j>=0; j--) {
			s.a = rowsrc[j];
			if (s.a==0) {
				//s = c0;
			} else if (!spread) {
				// keep the alpha, copy the color
				//s.integer = (s.a<<24)|c0.integer;
				s.a = s.a * alphaFactor;
			} else {
				// spread and alpha
				if (s.a >= invspread) {
					// alpha of max (255)
					s.a = c.a;
				} else {
					// Map 0..invspread to 0..alpha (255)
					s.a = s.a*c.a/invspread;
				}
			}
			rowout[j] = s;
		}
		texWordsPixelsRendered(bufferSizeX, yield);
	}
}

// Approximates the GL version (identical?)
static void filterKernelColorizeNoSpreadNoAlpha(U8* dest, U8* src, S32 *intKernel, int kwidth, int kheight, int offsx, int offsy, Color color, bool yield)
{
	int i, j, kmx = kwidth/2, kmy = kheight/2, x, y, ki, xbase, ymax, xmax;
	F32 *kernel;
	U8 *kernelNotZero;
	Color s;
	F32 alphaScale = color.a*U8TOF32_COLOR;
	Color baseColor = color;
	int pixelsPerLineCount = bufferSizeX*kwidth*kheight;
	baseColor.a = 0;

	lsprintf("zeroing...");
	// Set base color
	ZeroMemory(dest, bufferSizeX*bufferSizeY);
	leprintf("done.");
	lsprintf("the rest...");
	convKernel(intKernel, &kernel, &kernelNotZero);

	// walk over all source, and add to dest
	for (i=0; i<bufferSizeY; i++) {
		Color *srcwalk=(Color *)&src[bufferSizeX*i<<2];
		ymax = MIN(i - kmy - offsy + kheight, bufferSizeY);
		for (j=0; j<bufferSizeX; j++, srcwalk++) {
			s = *srcwalk;
			if (!s.a)
				continue;
			y = i - kmy - offsy;
			ki = 0;
			xbase = j - kmx + offsx;
			xmax = xbase + kwidth;
			for (; y<ymax; y++) {
				if (y<0) {
					ki+=kwidth;
					continue;
				}
				for (x=xbase; x<xmax; x++, ki++) {
					if (x<0 || x>=bufferSizeX)
						continue;
					if (kernelNotZero[ki]) {
						F32 kw = kernel[ki];
						// Orig: dest[(bufferSizeX*y + x)] += s.a*kw;
						DWORD dwadd;
#if !USE_MMX
						dwadd = kw * s.a;
#else
						DWORD alpha = s.a;
						_asm {
							// fadd = falpha*kw;
							fild	dword ptr[alpha]
							fmul	dword ptr[kw]
							// add = fadd;
							fistp	dword ptr[dwadd] // Round to nearest
						}
#endif
						dwadd += dest[(bufferSizeX*y + x)];
						if (dwadd > 255) {
							dest[(bufferSizeX*y + x)] = 255;
						} else {
							dest[(bufferSizeX*y + x)] = dwadd;
						}
					}
				}
			}
		}
		texWordsPixelsRendered(pixelsPerLineCount, yield);
	}
	leprintf("done.");
}

static void filterKernelColorizeFast(U8* dest, U8* src, S32 *kernel, int kwidth, int kheight, int offsx, int offsy, Color color, U8 spread, bool yield)
{
//	int i, j;
	U8 alpha = color.a;
	lsprintf("filter..");
	filterKernelColorizeNoSpreadNoAlpha(dest, src, kernel, kwidth, kheight, offsx, offsy, color, yield);
	leprintf("      done.");
	lsprintf("spread..");
	filterSpreadColorUnpack(dest, spread, color, yield);
	leprintf("done.");
}

static void filterDropShadow(U8* dest, U8* src, TexWordLayerFilter *filter, bool yield, F32 scale)
{
	int *kernel = makeKernel(filter->magnitude*scale);
	if (0)
	{
		// This version will be perfectly accurate, but much, much slower
		filterKernelColorize(dest, src, kernel, kernel_width, kernel_width, filter->offset[0], -filter->offset[1], makeColor(filter->rgba), filter->spread*255, yield);
	}
	else 
	{
		filterKernelColorizeFast(dest, src, kernel, kernel_width, kernel_width, filter->offset[0], -filter->offset[1], makeColor(filter->rgba), filter->spread*255, yield);
	}
}

static void filterDesaturate(U8* dest, U8* src, TexWordLayerFilter *filter, bool yield, F32 scale)
{
	int i, j;
	register Color s;
	Color *d;
	Vec3 hsv, rgb;
	F32 invmag = 1 - filter->percent;
	for (i=0; i<bufferSizeY; i++) {
		Color *rowdest=(Color *)&dest[bufferSizeX*i<<2];
		Color *rowsrc =(Color *)&src[bufferSizeX*i<<2];
		for (j=0; j<bufferSizeX; j++) {
			s = rowsrc[j];
			rgb[0] = s.r*U8TOF32_COLOR;
			rgb[1] = s.g*U8TOF32_COLOR;
			rgb[2] = s.b*U8TOF32_COLOR;
			rgbToHsv(rgb, hsv);
			hsv[1]*= invmag;
			hsvToRgb(hsv, rgb);
			d = &rowdest[j];
			d->r = rgb[0]*255;;
			d->g = rgb[1]*255;;
			d->b = rgb[2]*255;;
			d->a = s.a;
		}
		texWordsPixelsRendered(bufferSizeX*4, yield);
	}
}

static void filterLayer(U8* dest, U8* src, TexWordLayerFilter *filter, bool yield, F32 scaleX, F32 scaleY)
{
	int i;
	F32 effScale = (scaleX + scaleY)/2;
	struct {
		TexWordFilterType type;
		void (*filterFunc)(U8 *dest, U8 *src, TexWordLayerFilter *filter, bool yield, F32 scale);
	} mapping[] = {
		{TWFILTER_NONE, filterNoop},
		{TWFILTER_BLUR, filterBlur},
		{TWFILTER_DROPSHADOW, filterDropShadow},
		{TWFILTER_DESATURATE, filterDesaturate},
	};

	for (i=0; i<ARRAY_SIZE(mapping); i++) {
		if (mapping[i].type == filter->type) {
			mapping[i].filterFunc(dest, src, filter, yield, effScale);
			return;
		}
	}
	if (isDevelopmentMode()) {
		Errorf("Invalid filter function!");
	}
	filterNoop(dest, src, filter, yield, effScale);
}

static int g_layernum;
void renderTexWordLayer(PBName destBuf, TexWordLayer *layer, int screenPosX, int screenPosY, int screenSizeX, int screenSizeY, F32 scaleX, F32 scaleY, BasicTexture *texBindParent, bool yield)
{
	static int debug_colors[] = {
		0xff0000ff,
		0xff007fff,
		0xff00ffff,
		0xff00ff00,
		0xffff0000,
		0xffff00ff,
	};
	int sizeX;
	int sizeY;
	int x, y, i;
	PBName renderMeInto = destBuf;
	bool renderSubLayer = layer->sublayer && layer->sublayer[0] && !layer->sublayer[0]->hidden;
	bool filter = eaSize(&layer->filter)>0;

	if (layer->hidden) {
		memset(getBuffer(destBuf), 0, 4*bufferSizeX*bufferSizeY);
		return;
	}

	if (renderSubLayer) {
		lsprintf("getBuffer...");
		getBuffer(renderMeInto);
		getBuffer(PB_SUBLAYER);
		getBuffer(PB_THISLAYER);
		leprintf("done.");

		lsprintf("Rendering sub-layer...");
		renderTexWordLayer(PB_SUBLAYER, layer->sublayer[0], screenPosX, screenPosY, screenSizeX, screenSizeY, scaleX, scaleY, texBindParent, yield);
		renderMeInto = PB_THISLAYER;
		leprintf("    done rendering sub-layer.");
	} else {
		lsprintf("getBuffer...");
		getBuffer(renderMeInto);
		leprintf("done.");
	}

	if (layer->size[0])
		sizeX = layer->size[0]*scaleX;
// 	else if (layer->image)
// 		sizeX = layer->image->origWidth*scaleX;
	else
		sizeX = screenSizeX;
	if (layer->size[1])
		sizeY = layer->size[1]*scaleY;
// 	else if (layer->image)
// 		sizeY = layer->image->origHeight*scaleY;
	else
		sizeY = screenSizeY;
	if (sizeX<1)
		sizeX = 1; // May happen with decreased texture scaling
	if (sizeY<1)
		sizeY = 1; // May happen with decreased texture scaling

	if (layer->pos[0])
		x = layer->pos[0]*scaleX;
	else
		x = screenPosX;
	if (layer->pos[1])
		y = layer->pos[1]*scaleY;
	else
		y = screenPosY;

	lsprintf("Drawing...");
	memset(getBuffer(renderMeInto), 0, 4*bufferSizeX*bufferSizeY);

	switch(layer->type) {
		case TWLT_BASEIMAGE:
		case TWLT_IMAGE:
			switch(layer->stretch) {
				case TWLS_FULL:
					renderTexWordLayerImage(renderMeInto, layer, screenPosX, screenPosY, screenSizeX, screenSizeY, screenSizeX, screenSizeY, layer->rot, yield);
					break;
				case TWLS_NONE:
				case TWLS_TILE:
					renderTexWordLayerImage(renderMeInto, layer, x, y, sizeX, sizeY, screenSizeX, screenSizeY, layer->rot, yield);
					break;
			}
			break;
		case TWLT_TEXT:
			renderTexWordLayerText(renderMeInto, layer, x, y, sizeX, sizeY, screenSizeX, screenSizeY, scaleX, scaleY, texBindParent, layer->rot, yield);
			break;
	}

	leprintf("    done.");

	if (renderSubLayer) {
		lsprintf("Blending with sub-layer (%s)...", StaticDefineIntRevLookup(ParseTexWordBlendType, layer->subBlend));
		blendLayers(getBuffer(destBuf), getBuffer(renderMeInto), getBuffer(PB_SUBLAYER), layer->subBlend, layer->subBlendWeight, yield);
		leprintf("done.");
	}
	// Perform after effect filtering
	for (i=0; i<eaSize(&layer->filter)>0; i++)
	{
		PBName tempbuf = (destBuf == PB_THISLAYER)?PB_SUBLAYER:PB_THISLAYER;
		lsprintf("Filtering...");
		filterLayer(getBuffer(tempbuf), getBuffer(destBuf), layer->filter[i], yield, scaleX, scaleY);
		leprintf("    done.");
		lsprintf("Blending with filter (%s)...", StaticDefineIntRevLookup(ParseTexWordBlendType, layer->filter[i]->blend));
		blendLayers(getBuffer(destBuf), getBuffer(tempbuf), getBuffer(destBuf), layer->filter[i]->blend, 1.0, yield);
		leprintf("done.");
	}
}



static void addReferencedFilesLayer(TexWordLayer *layer, FileList *file_list)
{
	char filename[CRYPTIC_MAX_PATH];
	if (layer->hidden)
		return;
	if (layer->sublayer && layer->sublayer[0])
		addReferencedFilesLayer(layer->sublayer[0], file_list);
	switch(layer->type) {
		xcase TWLT_BASEIMAGE:
		case TWLT_IMAGE:
			if (layer->image) {
				texFindFullName(layer->image, SAFESTR(filename)); 
				FileListInsert(file_list, filename, 0);
			}	
		xcase TWLT_TEXT:
			strcpy(filename, "fonts/");
			strcat(filename, layer->font.fontName);
			FileListInsert(file_list, filename, 0);
	}
}

static void addReferencedFiles(TexWord *texWord, FileList *file_list)
{
	int i, numlayers = eaSize(&texWord->layers);

	for (i=0; i<numlayers; i++) {
		addReferencedFilesLayer(texWord->layers[i], file_list);
	}
}

static void texWordAddToCache(TexWord *texWord, BasicTexture *texBindParent)
{
	FileList file_list=NULL;
	char cacheFileName[CRYPTIC_MAX_PATH];
	char messageStoreFileName[CRYPTIC_MAX_PATH];
	int dataSize;
	void *data;
	TexWordLoadInfo *texWordLoadInfo;
	// Assemble FileList
	if (isProductionMode())
		FileListInsert(&file_list, "bin/TexWords.bin", 0);
	else
		FileListInsert(&file_list, texWord->filename, 0);
	// associated .ms file (in the right locale)
	texWordMessageStoreFileName(SAFESTR(messageStoreFileName), texWord);
	FileListInsert(&file_list, messageStoreFileName, 0);
	// All referenced font files
	// All referenced textures
	addReferencedFiles(texWord, &file_list);

	// Package up data
	texWordLoadInfo = texGetRareData(texBindParent)->texWordLoadInfo;
	assert(texWordLoadInfo);
	dataSize = sizeof(TexWordLoadInfo) + texWordLoadInfo->data_size + texWordLoadInfo->mipmap_data_size;
	data = malloc(dataSize);
	*(TexWordLoadInfo*)data = *(texWordLoadInfo);
	memcpy(OFFSET_PTR(data, sizeof(TexWordLoadInfo)),
		texWordLoadInfo->data,
		texWordLoadInfo->data_size);
	if (texWordLoadInfo->mipmap_data_size) {
		memcpy(OFFSET_PTR(data, sizeof(TexWordLoadInfo) + texWordLoadInfo->data_size),
			texWordLoadInfo->mipmap_data,
			texWordLoadInfo->mipmap_data_size);
	}
	texWordCacheFileName(SAFESTR(cacheFileName), texWord, texBindParent);
	dynamicCacheUpdateFile(texWords_cache, cacheFileName, data, dataSize, &file_list);

	SAFE_FREE(data);
	FileListDestroy(&file_list);
}

static void texWordDoComposition(TexWord *texWord, BasicTexture *texBindParent, bool yield)
{	// Let's do it!
	int desired_aa=4;
	int required_aa=1;
	int sizeX, sizeY;
	int actualSizeX, actualSizeY;
	int ULx, ULy;
	F32 scaleX=1.0, scaleY=1.0;
	U8 * pixbuf;
	int do_dxt_compression=0;
	int i;
	int numlayers = eaSize(&texWord->layers);
	TexWordLoadInfo *texWordLoadInfo;

	memlog_printf(texGetMemLog(), "twDoComposition()		%s", texBindParent->name);

	if (gfx_state.debug.texWordVerbose)
		setLoadTimingPrecistion(3);

	checkEverythingIsLoaded(texWord, texBindParent, true);

	if (!yield)
		// If the threaded renderer is rendering something and pausing to finish,
		// we just want it to finish ASAP so we can render something in the foreground
		texWord_doNotYield = true;
	EnterCriticalSection(&criticalSectionDoingTexWordRendering);
	if (!yield)
		texWord_doNotYield = false;
	lsprintf("Compositing texture...");

	// 0. Misc init

	sizeX = texWord->size[0];
	sizeY = texWord->size[1];
	if (sizeX==0)
		sizeX = texBindParent->width;
	if (sizeY==0)
		sizeY = texBindParent->height;

	if (sizeX==0)
		sizeX = 128;
	if (sizeY==0)
		sizeY = 128; // Default fallback

	if (gfx_state.reduce_mip_world && !(texBindParent->bt_texopt_flags & TEXOPT_NOMIP)) {
		int mipLevel = gfx_state.reduce_mip_world;
		for (i = 0; i < mipLevel; i++)
		{
			sizeX >>= 1;
			scaleX /= 2;
			sizeY >>= 1;
			scaleY /= 2;
		}
	}
	ULx = 0;
	ULy = 0;
	actualSizeX = 1 << log2(sizeX);
	actualSizeY = 1 << log2(sizeY);

	bufferSetSize(actualSizeX, actualSizeY);

	// 2. Go through layers and draw to pbuffer
	for (i=0; i<numlayers; i++) {
		g_layernum=i;
		lsprintf("Rendering layer #%d...", i);
		renderTexWordLayer(PB_THISLAYER, texWord->layers[i], ULx, ULy, sizeX, sizeY, scaleX, scaleY, texBindParent, yield);
		leprintf("  done rendering layer.");
		// Blend with last layer
		if (pixbuffers[PB_LASTLAYER]) {
			lsprintf("Blending layer #%d with previous (%s)...", i, StaticDefineIntRevLookup(ParseTexWordBlendType, TWBLEND_OVERLAY));
			blendLayers(getBuffer(PB_LASTLAYER), getBuffer(PB_THISLAYER), getBuffer(PB_LASTLAYER), TWBLEND_OVERLAY, 1.0, yield);
			leprintf("done.");
		} else {
			swapBuffers(PB_LASTLAYER, PB_THISLAYER);
		}
	}
	lsprintf("Finalizing...");
	
	//windowUpdate();

	// 3. Extract image to memory
	pixbuf = pixbuffers[PB_LASTLAYER];

	// Do alpha bordering
	if (texBindParent->bt_texopt_flags & TEXOPT_ALPHABORDER)
		alphaBorderBuffer(texBindParent->bt_texopt_flags, pixbuf, sizeX, sizeY, RTEX_BGRA_U8);

	// Package up data for main thread to process
	assert(!texAllocRareData(texBindParent)->texWordLoadInfo);
	texWordLoadInfo = texAllocRareData(texBindParent)->texWordLoadInfo = createTexWordLoadInfo();
	texWordLoadInfo->data = pixbuf;
	pixbuffers[PB_LASTLAYER] = NULL;
	texWordLoadInfo->data_size = actualSizeX * actualSizeY * 4;
	texWordLoadInfo->rdr_format = RTEX_BGRA_U8;
	texWordLoadInfo->actualSizeX = actualSizeX;
	texWordLoadInfo->actualSizeY = actualSizeY;
	texWordLoadInfo->sizeX = sizeX/scaleX;
	texWordLoadInfo->sizeY = sizeY/scaleY;
	// TODO: Do all rendering at non-reduced settings, and then just skip high LODs while loading
	texWordLoadInfo->reduce_mip_setting = gfx_state.reduce_mip_world;
	texWordLoadInfo->flags = 0;
	for (i=0; i<sizeX * sizeY; i++) {
		if (pixbuf[i*4+3] != 255) {
			texWordLoadInfo->flags |= TEX_ALPHA;
			break;
		}
	}

	freeBuffers();

	if (texWords_allowDXT && (texBindParent->rdr_format == RTEX_DXT1 || texBindParent->rdr_format == RTEX_DXT5))
	{
		do_dxt_compression = 1;
	}

	// Generate MipMaps
	if (!(texBindParent->bt_texopt_flags & TEXOPT_NOMIP)) {
		PERFINFO_AUTO_START("buildMipMaps",1);
		lsprintf("Mipmap Generation...");
		texWordLoadInfo->mipmap_data = buildMipMaps(texBindParent->bt_texopt_flags, pixbuf, actualSizeX, actualSizeY, RTEX_BGRA_U8, do_dxt_compression?texBindParent->rdr_format:RTEX_BGRA_U8, &texWordLoadInfo->mipmap_data_size);
		texWordsPixelsRendered(actualSizeX*actualSizeY, yield);
		leprintf("  done.");
		PERFINFO_AUTO_STOP();
	}

	// Do DXT compression (in-thread)
	if (do_dxt_compression)
	{
		U8 *dxt_data;
		U32 byte_count = imgByteCount(RTEX_2D, texBindParent->rdr_format, actualSizeX, actualSizeY, 1, 1);
		PERFINFO_AUTO_START("dxtCompress",1);
		lsprintf("DXT Compression...");
		dxt_data = malloc(byte_count);
		if (dxtCompress(pixbuf, dxt_data, actualSizeX, actualSizeY, RTEX_BGRA_U8, texBindParent->rdr_format)) {
			texWordsPixelsRendered(byte_count, yield);
			SAFE_FREE(texWordLoadInfo->data);
			texWordLoadInfo->data = dxt_data;
			texWordLoadInfo->data_size = byte_count;
			texWordLoadInfo->rdr_format = texBindParent->rdr_format;
			leprintf("  done.");
		} else {
			// Failed for some reason
			SAFE_FREE(dxt_data);
			leprintf("  failed.");
		}
		PERFINFO_AUTO_STOP();
	}

	// Add to cache
	if (!texWords_disableCache)
		texWordAddToCache(texWord, texBindParent);

	leprintf("  done.");
	leprintf("done.");
	unloadDataAfterComposition(texWord);
	LeaveCriticalSection(&criticalSectionDoingTexWordRendering);
	if (texWord_doNotYield) {
		// We are in the background thread and the foreground requested we quickly speed through things to give
		//  it control, let's sleep for a ms to yield to the foreground
		Sleep(1);
	}
}

void texWordSendToRenderer(TexWord *texWord, BasicTexture *texBindParent)
{
	RdrTexParams *rtex;
	TexOptFlags texopt_flags = texBindParent->bt_texopt_flags;
	U32 image_byte_count;
	RdrTexType tex_type = RTEX_2D;
	U32 mip_count=1;
	RdrTexFlags sampler_flags = 0;
	TexWordLoadInfo *texWordLoadInfo;

	PERFINFO_AUTO_START("texWordSendToRenderer", 1);

	memlog_printf(texGetMemLog(), "twSendToRenderer()			%s", texBindParent->name);
	lsprintf("Sending new texture to Renderer...");
	texWordLoadInfo = texGetRareData(texBindParent)->texWordLoadInfo;
	assert(texWordLoadInfo);

	assert(texBindParent->tex_handle!=0);

	assert(!(texopt_flags & TEXOPT_CUBEMAP));
	assert(!(texopt_flags & TEXOPT_VOLUMEMAP));

	if (texWordLoadInfo->mipmap_data)
		mip_count = imgLevelCount(texWordLoadInfo->actualSizeX, texWordLoadInfo->actualSizeY, 1);

	if (texopt_flags & TEXOPT_MAGFILTER_POINT)
		sampler_flags |= RTF_MAG_POINT;
	if (texopt_flags & TEXOPT_CLAMPS)
		sampler_flags |= RTF_CLAMP_U;
	else if (texopt_flags & TEXOPT_MIRRORS)
		sampler_flags |= RTF_MIRROR_U;
	if (texopt_flags & TEXOPT_CLAMPT)
		sampler_flags |= RTF_CLAMP_V;
	else if (texopt_flags & TEXOPT_MIRRORT)
		sampler_flags |= RTF_MIRROR_V;

	rdrChangeTexHandleFlags(&texBindParent->tex_handle, sampler_flags);

	rtex = rdrStartUpdateTexture(gfx_state.currentDevice->rdr_device, texBindParent->tex_handle, tex_type, texWordLoadInfo->rdr_format,
		texWordLoadInfo->actualSizeX, texWordLoadInfo->actualSizeY,
		1, mip_count, &image_byte_count, texMemMonitorNameFromFlags(texBindParent->use_category), NULL);
	rtex->anisotropy = MAX(1,gfx_state.settings.texAniso);

	//assert(info->size == image_byte_count); // JE: Not true, as the texture file header is padded to 4 bytes (only happens on RGB truecolor images)

	if (texopt_flags & TEXOPT_MAGFILTER_POINT)
		rtex->anisotropy = 1;

	if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
		rtex->is_srgb = (texopt_flags & TEXOPT_SRGB) != 0;

	// Put in new resolution
	texBindParent->width = texWordLoadInfo->sizeX;
	texBindParent->height = texWordLoadInfo->sizeY;
	texBindParent->realWidth = pow2(texBindParent->width);
	texBindParent->realHeight = pow2(texBindParent->height);
	if (texWordLoadInfo->flags & TEX_ALPHA) {
		texBindParent->flags |= TEX_ALPHA;
	} else {
		texBindParent->flags &= ~TEX_ALPHA;
	}

	{
		U32 main_data_size = imgByteCount(RTEX_2D, texWordLoadInfo->rdr_format, 
			texWordLoadInfo->actualSizeX, texWordLoadInfo->actualSizeY, 1, 1);
		U32 mip_data_size = mip_count>1?imgByteCount(RTEX_2D, texWordLoadInfo->rdr_format, 
			texWordLoadInfo->actualSizeX >> 1, texWordLoadInfo->actualSizeY >> 1, 1, mip_count-1):0;
		assert(main_data_size+mip_data_size == image_byte_count);
		memcpy(rtex->data, texWordLoadInfo->data, main_data_size);
		if (mip_data_size) {
			memcpy(main_data_size + (char*)(rtex+1), texWordLoadInfo->mipmap_data, mip_data_size);
		}

		texRecordNewMemUsage(texBindParent, TEX_MEM_RAW, main_data_size + mip_data_size);
		texRecordNewMemUsage(texBindParent, TEX_MEM_LOADING, 0);
	}
	rdrEndUpdateTexture(gfx_state.currentDevice->rdr_device);

	leprintf("done.");

	PERFINFO_AUTO_STOP();
}

void texWordDoneLoading(TexWord *texWord, BasicTexture *texBindParent)
{
	// Free buffer
	destroyTexWordLoadInfo(texGetRareData(texBindParent)->texWordLoadInfo);
	texGetRareData(texBindParent)->texWordLoadInfo = NULL;
}

typedef struct TexWordThreadPackage {
	TexWord *texWord;
	BasicTexture *texBindParent;
	bool yield;
} TexWordThreadPackage;

static void texWordDoneInThread(BasicTexture *texBindParent)
{
	// Called from thread when done
	// Called from main thread as well (from DynamicCache or LOAD_IN_FOREGROUND)
	texAddToProcessingListFromBind(texBindParent); // thread-safe add to list, gets moved to queuedTexLoadPkgs later
}

static void texWordDoThreadedWorkSub(const char *filename_unused, TexWordThreadPackage *twPkg)
{
	PERFINFO_AUTO_START("texWordDoThreadedWorkSub", 1);
		PERFINFO_AUTO_START("texWordDoComposition", 1);
			texWordDoComposition(twPkg->texWord, twPkg->texBindParent, twPkg->yield);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("other", 1);
		{
			texWordDoneInThread(twPkg->texBindParent);
			InterlockedDecrement(&numTexWordsInThread);
			free(twPkg);
		}
		PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

void texWordStartThreadedWork(TexWord *texWord, BasicTexture *texBindParent, bool yield)
{
	TexWordThreadPackage *twPkg;

	if (!texWords_multiThread) {
		yield = false;
	}

	twPkg = calloc(sizeof(TexWordThreadPackage),1);
	twPkg->texWord = texWord;
	twPkg->texBindParent = texBindParent;
	twPkg->yield = yield;
	InterlockedIncrement(&numTexWordsInThread);

	// Do software composition
	// queue this in another thread (or do it immediately in the case of a foreground load)
	if (yield) {
		fileLoaderRequestAsyncExec(texWord->filename, FILE_LOWEST_PRIORITY, false, texWordDoThreadedWorkSub, twPkg);
	} else {
		// Do it now!
		texWordDoThreadedWorkSub(texWord->filename, twPkg);
	}
}


static void texWordsCacheFailedCallback(DynamicCacheElement* elem, BasicTexture *texBindParent)
{
	memlog_printf(texGetMemLog(), "%u: texWordsCacheFailedCallback(%s)", gfx_state.client_frame_timestamp, texBindParent->name);
	texWordLoadInternal(texGetTexWord(texBindParent), TexLoadHowFromTexture(texBindParent), texBindParent->use_category, texBindParent);
}

static bool texWordsCacheCallback(DynamicCacheElement* elem, BasicTexture *texBindParent)
{
	int data_size = dceGetDataSize(elem);
	TexWordLoadInfo *cached = dceGetDataAndAcquireOwnership(elem);
	bool bad_data=false;
	bool was_random_failure_or_settings_change=false;

	memlog_printf(texGetMemLog(), "%u: texWordsCacheCallback(%s)", gfx_state.client_frame_timestamp, texBindParent->name);

	if (sizeof(TexWordLoadInfo) + cached->data_size + cached->mipmap_data_size > (size_t)data_size) {
		// Not enough data
		bad_data = true;
	} else if (cached->rdr_format != RTEX_BGRA_U8 && cached->rdr_format != RTEX_BGR_U8 && cached->rdr_format != RTEX_DXT1 && cached->rdr_format != RTEX_DXT5) {
		// Invalid format
		bad_data = true;
	} else if (cached->data_size != imgByteCount(RTEX_2D, cached->rdr_format, cached->actualSizeX, cached->actualSizeY, 1, 1)) {
		// Invalid size
		bad_data = true;
	} else if (cached->mipmap_data_size && cached->mipmap_data_size != imgByteCount(RTEX_2D, cached->rdr_format, cached->actualSizeX>>1, cached->actualSizeY>>1, 1, 0)) {
		// Invalid sized mip data
		bad_data = true;
	} else if (cached->sizeX > 2048 || cached->actualSizeX > 2048 || cached->sizeY > 2048 || cached->actualSizeY > 2048) {
		// Invalid size values
		bad_data = true;
	} else if (cached->reduce_mip_setting != gfx_state.reduce_mip_world) {
		was_random_failure_or_settings_change = true;
		bad_data = true;
	} else if (dynamicCacheDebugRandomFailure()) {
		was_random_failure_or_settings_change = true;
		bad_data = true;
	}
	
	if (bad_data) {
		devassert(was_random_failure_or_settings_change); // Should only happen if data went bad on disk. Unlikely!
		SAFE_FREE(cached);
		return false;
	} else {
		// Good!
		cached->data = OFFSET_PTR(cached, sizeof(TexWordLoadInfo));
		if (cached->mipmap_data_size) {
			cached->mipmap_data = OFFSET_PTR(cached, sizeof(TexWordLoadInfo) + cached->data_size);
		} else {
			cached->mipmap_data = NULL;
		}
		cached->from_cache_freeme = cached; // Gets freed later
		assert(!texAllocRareData(texBindParent)->texWordLoadInfo);
		texAllocRareData(texBindParent)->texWordLoadInfo = cached;
		texWordDoneInThread(texBindParent);
		return true;
	}
}

void texWordsStartup(void)
{
	char filename[CRYPTIC_MAX_PATH];
	loadstart_printf("Loading TexWords...");
	initBackgroundTexWordRenderer();
	assert(!texWords_cache);

	sprintf(filename, "%s/texWords.hogg", fileCacheDir());

	texWords_cache = dynamicCacheCreate(filename, TEXWORDS_CACHE_VERSION, 32*1024*1024, 64*1024*1024, 4*3600, DYNAMIC_CACHE_DEFAULT);
	texWordsAllowLoading(true); // Any calls before this (e.g. command line options) just set the locale
	texWordsLoad(locGetName(getCurrentLocale()));
	if (getNumRealCpus() > 1) {
		yield_amount *= 10;
	} else if (getNumVirtualCpus() > 1) {
		yield_amount *= 2;
	}
	loadend_printf(" done (%d TexWords).", eaSize(&texWords_list.texWords));
}

// If it returns 1, the caller should remove it from the list and free it
static int texWordsCheckSingle(TexWordQueuedLoad *queuedLoad)
{
	if (!checkEverythingIsLoaded(queuedLoad->texWord, queuedLoad->texBindParent, false)) {
		return 0;
	}

	// Everything is loaded, fire off the composition step
	texWordStartThreadedWork(queuedLoad->texWord, queuedLoad->texBindParent, queuedLoad->yield);

	// Remove from list (will get added to queuedTexLoadPkgs later)
	return 1;
}

// Called whenever new textures have been loaded
// Needs to check everything queued to see if all of their dependencies are loaded
// Also assert that each dependency is still being loaded?
void texWordsCheck(void)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	for (i=eaSize(&texWordQueuedLoads)-1; i>=0; i--)
	{
		if (texWordsCheckSingle(texWordQueuedLoads[i])) {
			MP_FREE(TexWordQueuedLoad, texWordQueuedLoads[i]);
			eaRemoveFast(&texWordQueuedLoads, i);
		}
	}
	PERFINFO_AUTO_STOP();
}

void texWordLoad(TexWord *texWord, TexLoadHow mode, WLUsageFlags use_category, BasicTexture *texBindParent)
{
	if (!texWords_disableCache) {
		char cacheFileName[CRYPTIC_MAX_PATH];
		// First, check to see if it's in the cache, if so, we can load it immediately!
		texWordCacheFileName(SAFESTR(cacheFileName), texWord, texBindParent);
		memlog_printf(texGetMemLog(), "%u: texWordLoad %s trying cache", gfx_state.client_frame_timestamp, texWord->filename);
		// use it if it's there and up to date
		dynamicCacheGetAsync(texWords_cache, cacheFileName, texWordsCacheCallback, texWordsCacheFailedCallback, texBindParent);
		if (mode == TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD) {
			dynamicCacheForceLoadingToFinish(texWords_cache);
			memlog_printf(texGetMemLog(), "%u: texWordLoad waited for it to load %s", gfx_state.client_frame_timestamp, texWord->filename);
		} else {
			memlog_printf(texGetMemLog(), "%u: texWordLoad returning async load %s", gfx_state.client_frame_timestamp, texWord->filename);
		}
		return;
	}
	texWordLoadInternal(texWord, mode, use_category, texBindParent);
}

static void texWordLoadInternal(TexWord *texWord, TexLoadHow mode, WLUsageFlags use_category, BasicTexture *texBindParent)
{
	memlog_printf(texGetMemLog(), "%u: texWordLoadInternal %s NOT in cache", gfx_state.client_frame_timestamp, texWord->filename);
	// Not in cache, load it!

	// Sends off requests to load dependencies
	// Adds TexWord to local loading queue
	// Needs to add to queuedTexLoadPkgs when done (which will unset ->loading eventually)

	texWordLoadDeps(texWord, mode, use_category, texBindParent);

	if (mode == TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD) {
		TexWordQueuedLoad queuedLoad;
		checkEverythingIsLoaded(texWord, texBindParent, true);
		queuedLoad.texWord = texWord;
		queuedLoad.texBindParent = texBindParent;
		queuedLoad.yield = false;
		verify(texWordsCheckSingle(&queuedLoad));
	} else if (mode == TEX_LOAD_IN_BACKGROUND) {
		TexWordQueuedLoad *queuedLoad;
		MP_CREATE(TexWordQueuedLoad, 16);
		queuedLoad = MP_ALLOC(TexWordQueuedLoad);
		queuedLoad->texWord = texWord;
		queuedLoad->texBindParent = texBindParent;
		queuedLoad->yield = true;
		// Attempt to process it immediately
		if (texWordsCheckSingle(queuedLoad)) {
			MP_FREE(TexWordQueuedLoad, queuedLoad);
		} else {
			eaPush(&texWordQueuedLoads, queuedLoad);
		}
	} else
		assert(0);
}
#include "texWordsPrivate_h_ast.c"
