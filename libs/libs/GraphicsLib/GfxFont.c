#include "GfxFontPrivate.h"
#include "GfxFont.h"
#include "textparser.h"
#include "qsortG.h"
#include "GfxTextures.h"
#include "fileutil.h"
#include "mathutil.h"
#include "FolderCache.h"
#include "StringUtil.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "GfxConsole.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Fonts););
AUTO_RUN_ANON(memBudgetAddMapping("GfxFontPrivate.h", BUDGET_Fonts););

// Loosely, this is equivalent to AccountAccessLevel > 0.
// We only want to report the missing glyph error messages in development or QA mode.
// I think AccountAccessLevel > 0 would work, so we would only get reports from GMs when some jerk posts
// a crazy character in zone chat, rather than having every player in that zone report the missing glyph
// when they try to draw it.
bool g_ReportMissingGlyphErrors = false;
void gfxFontSetReportMissingGlyphErrors(bool bReport)
{
	g_ReportMissingGlyphErrors = bReport;
}

static GfxFontDataList g_fontDataList = {NULL};
static StashTable g_fontDataMap = NULL;
static GfxFontData* g_fallbackFontData = 0;

GfxFont g_font_Sans;
GfxFont g_font_SansLarge;
GfxFont g_font_SansSmall;
GfxFont g_font_Mono;
GfxFont g_font_Game;
GfxFont g_font_GameLarge;
GfxFont g_font_GameSmall;

//replace fonts that arent used anymore or that wont work with the new system
static const char* g_font_SubstituteList[] = 
{
	//font				//replacement	
	"Fonts/Vera.font", "Fonts/FreeSans.font",
	"Fonts/VeraBD.font", "Fonts/FreeSansBD.font",
	"Fonts/VeraBI.font", "Fonts/FreeSansBI.font",
	"Fonts/VeraI.font", "Fonts/FreeSansI.font",
	"Fonts/VeraSe.font", "Fonts/FreeSerif.font",
	"Fonts/VeraSeBd.font", "Fonts/FreeSerifBD.font",
	"Fonts/VeraI.font", "Fonts/FreeSansI.font",
	"Fonts/Tahoma.font", "Fonts/FreeSans.font",
	"Fonts/Verdana.font", "Fonts/FreeSans.font",
	"Fonts/Verdanab.font", "Fonts/FreeSansBD.font",
};

//These characters are ignored and not printed
static const  U16 g_font_IgnoreCharList[] = 
{
	'\n',
	'\r',
	'\t',
};

static __forceinline bool shouldIgnoreChar(U16 codePt)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(g_font_IgnoreCharList); i++)
	{
		if (codePt == g_font_IgnoreCharList[i])
			return true;
	}
	return false;
}

AUTO_FIXUPFUNC;
TextParserResult GfxFontFixup(GfxFontGlyphData *pGlyph, enumTextParserFixupType eType, void *pUnused)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		if (pGlyph->xAdvance == 0)
		{
			pGlyph->xAdvance = pGlyph->size[0];
		}
	}
	return PARSERESULT_SUCCESS;
}

static void initFontData(GfxFontData* pFontData)
{
	stashAddPointer(g_fontDataMap, pFontData->filename, pFontData, true);

	if (pFontData->pFontTexture == NULL)
	{
		pFontData->pFontTexture = texFindAndFlag(pFontData->fontTextureFile, false, WL_FOR_FONTS);
		// texLoadBasic(pFontData->fontTextureFile, TEX_LOAD_IN_BACKGROUND, WL_FOR_FONTS);
		if (!pFontData->pFontTexture)
		{
			ErrorFilenamef(pFontData->filename, "Font refernces a texture which does not exist: %s", pFontData->fontTextureFile);
			pFontData->pFontTexture = white_tex;
		} else {
			char err_string[1024]="";
			if (pFontData->pFontTexture->rdr_format != RTEX_A_U8)
			{
				strcat(err_string, "This texture does not have U8 compression.\n");
			}
			if ((pFontData->pFontTexture->bt_texopt_flags & (TEXOPT_CLAMPS|TEXOPT_CLAMPT)) != (TEXOPT_CLAMPS|TEXOPT_CLAMPT))
			{
				strcat(err_string, "This texture does not have ClampS and ClampT set.\n");
			}
			if (pFontData->pFontTexture->mip_type)
			{
				strcat(err_string, "This texture has MipMaps.\n");
			}
			if (err_string[0])
			{
				ErrorFilenamef(pFontData->filename, "Textures used as fonts must have U8 compression, ClampS, ClampT, and NoMips.  Errors with this texture:\n%s", err_string);
			}
		}
		pFontData->stFontGlyphs = stashTableCreateInt(eaSize(&pFontData->eaGlyphData));
		FOR_EACH_IN_EARRAY(pFontData->eaGlyphData, GfxFontGlyphData, pGlyph)
		{
			stashIntAddPointer(pFontData->stFontGlyphs, pGlyph->codePoint, pGlyph, false);
		}
		FOR_EACH_END;

		// Do the language-specific glyph substitutions
		FOR_EACH_IN_EARRAY(pFontData->eaLanguageSubstitutionGlyphData, GfxFontLanguageSubGlyphData, pGlyph)
		{
			if( !strcmp(locGetCrypticSpecific2LetterIdentifier(getCurrentLocale()), pGlyph->pcLangCode) )
			{
				stashIntAddPointer(pFontData->stFontGlyphs, pGlyph->glyph.codePoint, &pGlyph->glyph, true);
			}
		}
		FOR_EACH_END;
	}
}

static void initSubstitutionData(GfxFontData* pFontData)
{
	GfxFontData** dataPtrs[] = {&pFontData->substitutionBold, &pFontData->substitutionItalic, &pFontData->substitutionBoldItalic, &pFontData->substitutionMissingGlyphs};
	const char* filenames[] = {pFontData->substitutionBoldFile, pFontData->substitutionItalicFile, pFontData->substitutionBoldItalicFile, pFontData->substitutionMissingGlyphsFile};
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(dataPtrs); i++)
	{
		if (*dataPtrs[i] == NULL && filenames[i])
		{
			if (stricmp(filenames[i], "(null)") == 0) continue;
			*dataPtrs[i] = gfxFontGetFontData(filenames[i]);

			if (!*dataPtrs[i])
			{
				InvalidDataErrorf("Unable to load alternate font %s", filenames[i]);
			}
		}
	}
}

static void deinitFontData(GfxFontData* pFontData)
{
	stashRemovePointer(g_fontDataMap, pFontData->filename, NULL);

	if (pFontData->pFontTexture != NULL)
	{
		texUnloadDynamic(pFontData->pFontTexture);
		pFontData->pFontTexture = 0;
		stashTableDestroySafe(&pFontData->stFontGlyphs);
	}
}

static void setupStashTableData()
{
	if (!g_fontDataMap)
	{
		g_fontDataMap = stashTableCreateWithStringKeys(eaSize(&g_fontDataList.eaFontData), 0);
	}

	stashTableClear(g_fontDataMap);

	FOR_EACH_IN_EARRAY(g_fontDataList.eaFontData, GfxFontData, pFontData)
	{
		initFontData(pFontData);
	}
	FOR_EACH_END;

	//we need to do this after since the substitutions are looked up in the stashtable
	FOR_EACH_IN_EARRAY(g_fontDataList.eaFontData, GfxFontData, pFontData)
	{
		initSubstitutionData(pFontData);
	}
	FOR_EACH_END;
}

static void destroyStashTableData()
{
	stashTableDestroySafe(&g_fontDataMap);
	FOR_EACH_IN_EARRAY(g_fontDataList.eaFontData, GfxFontData, pFontData)
	{
		deinitFontData(pFontData);
	}
	FOR_EACH_END;
}

static int fontReloadCallback(void* structptr, void* oldStructCopy, ParseTable * pt, eParseReloadCallbackType type)
{
	if (oldStructCopy) deinitFontData(oldStructCopy);
	if (structptr)
	{
		initFontData(structptr);
	}

	//we need to do this after since the substitutions are looked up in the stashtable
	FOR_EACH_IN_EARRAY(g_fontDataList.eaFontData, GfxFontData, pFontData)
	{
		initSubstitutionData(pFontData);
	}
	FOR_EACH_END;

	return 0;
}

static void reloadFont( const char* relpath, int when )
{
	loadstart_printf( "Reloading font file..." );

	fileWaitForExclusiveAccess( relpath );
	if (strEndsWith(relpath, ".fontsettings"))
	{
		//if its a font settings reload everything since who knows what includes it
		FOR_EACH_IN_EARRAY(g_fontDataList.eaFontData, GfxFontData, pFontData)
		{
			char* estrFilename = estrCreateFromStr(pFontData->filename);
			ParserReloadFile(estrFilename, parse_GfxFontDataList, &g_fontDataList, fontReloadCallback, PARSER_BINS_ARE_SHARED);
			estrDestroy(&estrFilename);
		}
		FOR_EACH_END;
	}
	else
	{
		ParserReloadFile(relpath, parse_GfxFontDataList, &g_fontDataList, fontReloadCallback, PARSER_BINS_ARE_SHARED);
	}
	

	loadend_printf( "done" );
}



static DictionaryHandle s_ui_FaceDict = NULL;
static StashTable s_ui_stFonts;

static GfxFont *GfxFont_DecodeName(const char *pchName)
{
	char achFontName[MAX_PATH];
	char *pchSize;
	GfxFont *pFont = NULL;
	GfxFontData *pFontData = NULL;
	S32 iSize = 0;

	if (!stricmp(pchName, "Default"))
		return &g_font_Sans;
	if (!stricmp(pchName, "DefaultLarge"))
		return &g_font_SansLarge;
	if (!stricmp(pchName, "DefaultSmall"))
		return &g_font_SansSmall;
	if (!stricmp(pchName, "Mono"))
		return &g_font_Mono;
	if (!stricmp(pchName, "Game"))
		return &g_font_Game;
	if (!stricmp(pchName, "GameLarge"))
		return &g_font_GameLarge;
	if (!stricmp(pchName, "GameSmall"))
		return &g_font_GameSmall;

	if (!s_ui_stFonts)
		s_ui_stFonts = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);

	if (stashFindPointer(s_ui_stFonts, pchName, &pFont))
		return pFont;

	strcpy(achFontName, pchName);

	if (strEndsWith(achFontName, "Small"))
	{
		iSize = FONT_SMALL_RENDER_SIZE;
		achFontName[strlen(achFontName) - 5] = '\0';
	}
	else if (strEndsWith(achFontName, "Large"))
	{
		iSize = FONT_LARGE_RENDER_SIZE;
		achFontName[strlen(achFontName) - 5] = '\0';
	}
	else if (pchSize = strchr(achFontName, ':'))
	{
		*pchSize++ = 0;
		iSize = atoi(pchSize);
	}

	if (!iSize)
		iSize = FONT_NORMAL_RENDER_SIZE;

	pFontData = gfxFontGetFontData(achFontName);

	if (!pFontData)
	{
		InvalidDataErrorf("Unable to load font \"%s\"", achFontName);
		pFontData = g_font_Sans.fontData;
	}

	pFont = gfxFontCreateFromData(pFontData);
	pFont->renderSize = iSize;
	stashAddPointer(s_ui_stFonts, pchName, pFont, false);
	return pFont;
}

//these are for compatibility with the old system
void gfxfont_AddFace(const char *pchName, GfxFont *pFont)
{
	RefSystem_AddReferent("GfxFont", pchName, pFont);
}

GfxFont *gfxfont_GetFace(const char *pString)
{
	return pString ? RefSystem_ReferentFromString("GfxFont", pString) : NULL;
}



void gfxFontLoadFonts()
{
	int font_count = 0;

	loadstart_printf("Loading fonts...");

	ParserLoadFiles("Fonts/", ".font", "fonts.bin", PARSER_BINS_ARE_SHARED, parse_GfxFontDataList, &g_fontDataList);
	font_count = eaSize(&g_fontDataList.eaFontData);
	
	setupStashTableData();

	if (isDevelopmentMode())
	{
		FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "Fonts/*.font", reloadFont );
		FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "Fonts/*.fontsettings", reloadFont );
	}

	gfxFontInitalizeFromName(&g_font_Game, "Fonts/Game.font");
	g_font_Game.outline = 1;
	g_font_Game.renderSize = FONT_NORMAL_RENDER_SIZE;

	gfxFontInitalizeFromName(&g_font_GameSmall, "Fonts/Game.font");
	g_font_GameSmall.outline = 1;
	g_font_GameSmall.renderSize = FONT_SMALL_RENDER_SIZE;

	gfxFontInitalizeFromName(&g_font_GameLarge, "Fonts/Game.font");
	g_font_GameLarge.outline = 1;
	g_font_GameLarge.renderSize = FONT_LARGE_RENDER_SIZE;

	gfxFontInitalizeFromName(&g_font_Sans, "Fonts/FreeSans.font");
	g_font_Sans.outline = 1;
	g_font_Sans.renderSize = FONT_NORMAL_RENDER_SIZE;

	gfxFontInitalizeFromName(&g_font_SansSmall,"Fonts/FreeSans.font");
	g_font_SansSmall.outline = 1;
	g_font_SansSmall.renderSize = FONT_SMALL_RENDER_SIZE;

	gfxFontInitalizeFromName(&g_font_SansLarge, "Fonts/FreeSans.font");
	g_font_SansLarge.outline = 1;
	g_font_SansLarge.renderSize = FONT_LARGE_RENDER_SIZE;

	gfxFontInitalizeFromName(&g_font_Mono, "Fonts/VeraMonoBD.font");
	g_font_Mono.outline = 1;
	g_font_Mono.renderSize = FONT_NORMAL_RENDER_SIZE;

	gfxFontSetFallbackFontData(gfxFontGetFontData("Fonts/FreeSans.font"));

	s_ui_FaceDict = RefSystem_RegisterDictionaryWithStringRefData("GfxFont", GfxFont_DecodeName, false, NULL, false);

	if (isDevelopmentMode())
	{
		gfxFontSetReportMissingGlyphErrors(true);
	}

	loadend_printf( "done (%d fonts).", font_count );
}

void gfxFontUnloadFonts()
{
	destroyStashTableData();
	
	StructReset(parse_GfxFontDataList, &g_fontDataList);
}

char*** gfxFontGetFontNames()
{
	static char **ret = NULL;
	if (!ret)
	{
		FOR_EACH_IN_EARRAY(g_fontDataList.eaFontData, GfxFontData, pFontData)
		{
			eaPush(&ret, estrCreateFromStr(pFontData->filename));
		}
		FOR_EACH_END;
		eaQSort(ret, strCmp);
	}
	return &ret;
}

GfxFontData* gfxFontGetFontData(const char* fontName)
{
	GfxFontData* data = NULL;
	int i = 0;

	if (fontName)
	{
		char tempFontName[CRYPTIC_MAX_PATH];
		strcpy(tempFontName, fontName);

		//do some massaging for better compatibility with other code
		if (strEndsWith(tempFontName, ".ttf"))
			tempFontName[strlen(tempFontName)-4] = 0;

		if (!strEndsWith(tempFontName, ".font"))
			strcat(tempFontName, ".font");

		if (!strStartsWith(tempFontName, "Fonts/"))
		{
			char tmp[CRYPTIC_MAX_PATH];
			strcpy(tmp, tempFontName);
			strcpy(tempFontName, "Fonts/");
			strcat(tempFontName, tmp);
		}

		//the list alternates between font and replacement names
		for (i = 0; i < ARRAY_SIZE(g_font_SubstituteList)/2; i++)
		{
			if(stricmp(tempFontName, g_font_SubstituteList[i*2]) == 0)
			{
				strcpy(tempFontName, g_font_SubstituteList[i*2 + 1]); 
				break;
			}
		}

		stashFindPointer(g_fontDataMap, tempFontName, &data);
	}
	
	return data;
}

GfxFont* gfxFontCreateFromData(GfxFontData* fontData)
{
	GfxFont* font = StructCreate(parse_GfxFont);
	gfxFontInitalizeFromData(font, fontData);

	return font;
}

GfxFont* gfxFontCreateFromName( const char* fontName )
{
	GfxFontData* fontData = gfxFontGetFontData(fontName);
	return fontData ? gfxFontCreateFromData(fontData) : NULL;
}

bool gfxFontInitalizeFromData(GfxFont* font, GfxFontData* fontData)
{
	font->fontDataName = estrCreateFromStr(fontData->filename);
	font->fontData = fontData;
	setVec2same(font->dropShadowOffset, 2.f);
	font->softShadowSpread = 5;
	font->aspectRatio = 1.0f;
	font->sizeIsPixels = false;
	font->outline = true;
	font->color.uiTopLeftColor = RGBAFromColor(ColorWhite);
	font->color.uiTopRightColor = RGBAFromColor(ColorWhite);
	font->color.uiBottomLeftColor = RGBAFromColor(ColorWhite);
	font->color.uiBottomRightColor = RGBAFromColor(ColorWhite);
	font->uiOutlineColor = RGBAFromColor(ColorBlack);
	font->uiDropShadowColor = RGBAFromColor(ColorBlack);
	font->snapToPixels = true;
	return true;
}

bool gfxFontInitalizeFromName(GfxFont* font, const char* fontName)
{
	GfxFontData* fontData = gfxFontGetFontData(fontName);
	if (!fontData) return false;
	return gfxFontInitalizeFromData(font, fontData);
}

GfxFont* gfxFontCopy( GfxFont* font )
{
	GfxFont* dst = StructCreate(parse_GfxFont);
	StructCopyAll(parse_GfxFont, font, dst);
	return dst;
}

void gfxFontFree( GfxFont* font )
{
	StructDestroy(parse_GfxFont, font);
}

void gfxFontSetFontData( GfxFont* font, GfxFontData* fontData )
{
	estrDestroy(&font->fontDataName);
	font->fontDataName = estrCreateFromStr(fontData->filename);
	font->fontData = fontData;
}

static GfxFontData* g_debugDataOverride = 0;

AUTO_COMMAND ACMD_NAME(fontDebugDataOverride) ACMD_CLIENTONLY;
void fontDebugDataOvrideCmd(char* pString)
{
	if (!pString || !strlen(pString))
	{
		g_debugDataOverride = 0;
	}
	else
	{
		g_debugDataOverride = gfxFontGetFontData(pString);
		if (!g_debugDataOverride)
		{
			conPrintf("Cannot load font data %s", pString);
		}
	}
}

static __forceinline GfxFontData* applyFontDataSubstitutions(GfxFont* font, GfxFontData* overrideBaseData, bool* outBold, bool* outItalic)
{
	if (!overrideBaseData) overrideBaseData = font->fontData;
	if (font->bold && font->italicize && overrideBaseData->substitutionBoldItalic)
	{
		*outBold = false;
		*outItalic = false;
		return overrideBaseData->substitutionBoldItalic;
	}
	else if (font->bold && overrideBaseData->substitutionBold)
	{
		*outBold = false;
		*outItalic = false;
		return overrideBaseData->substitutionBold;
	}
	else if (font->italicize && overrideBaseData->substitutionItalic)
	{
		*outItalic = false;
		*outBold = false;
		return overrideBaseData->substitutionItalic;
	}
	else
	{
		*outBold = overrideBaseData->ignoreBold ? false : font->bold;
		*outItalic = overrideBaseData->ignoreItalic ? false : font->italicize;
		return overrideBaseData;
	}
}

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//make sure you keep these functions in sync or else the layout and drawing wont be the same!
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

static __forceinline void gfxFontPrintExImpl(GfxFont* font, F32 x, F32 y, F32 z, const char* pchString, const char* pchStopPtr,
											 gfxFont_display_sprite_distance_field_one_layer_func callback1Layer,
											 gfxFont_display_sprite_distance_field_two_layers_func callback2Layers,
											 void* userData, SpriteProperties *pProps)
{
	const char* curCodePoint = pchString; 
	F32 curx = x;
	F32 fontPixelSize = font->sizeIsPixels ? font->renderSize : gfxFontGetPixelsFromPts(font->renderSize);
	bool isBold, isItalic;
	bool snapToPixels = font->snapToPixels;
	//make sure its a vertical gradient and actually has a gradient
	bool doSpecialGradientMode = (font->color.uiTopLeftColor == font->color.uiTopRightColor &&
		font->color.uiBottomLeftColor == font->color.uiBottomRightColor &&
		font->color.uiTopLeftColor != font->color.uiBottomLeftColor);

	MAX1(fontPixelSize, 1);

	while (curCodePoint && curCodePoint != pchStopPtr && *curCodePoint) 
	{ 
		U32 codePt = UTF8ToCodepoint(curCodePoint); 
		GfxFontGlyphData* glyph = NULL; 
		GfxFontData* curFontData = applyFontDataSubstitutions(font, g_debugDataOverride, &isBold, &isItalic); 
		F32 scaleFactor = fontPixelSize / (F32)curFontData->fontSize; 
		Vec2 texSize = { (F32)curFontData->texSize[0], (F32)curFontData->texSize[1] }; 

		if (codePt && !shouldIgnoreChar(codePt) ) 
		{
			GfxFontData* pSubFontData = curFontData;
			int codePts[] = {codePt, '?'}; //try the one they wanted then try a question mark if that doesn't work
			int i;
			int iRedirectCount; // Don't allow too many substitutions, and avoid infinite looping.
			for (i = 0; i < ARRAY_SIZE(codePts) && glyph == NULL; i++)
			{
				iRedirectCount = 0;
				pSubFontData = curFontData;
				while( pSubFontData != NULL && iRedirectCount < 12 ) 
				{
					if( stashIntFindPointer(pSubFontData->stFontGlyphs, codePts[i], &glyph) )
					{
						break;
					}
					pSubFontData = pSubFontData->substitutionMissingGlyphs;
					iRedirectCount++;
				}
				if( pSubFontData == NULL )
				{
					if( g_ReportMissingGlyphErrors )
					{
						if( iRedirectCount > 1 )
							ErrorFilenamef(curFontData->filename, "Missing Glyph 0x%X for missing glyphs font in string %s. You need to add the glyph to %s or one of its substitutionMissingGlyphs fallbacks.", codePts[i], pchString, curFontData->filename);
						else
							ErrorFilenamef(curFontData->filename, "Missing Glyph 0x%X for Font in string \'%s\'. You should add a line like this to %s:\nsubstitutionMissingGlyphs Heiti-1", codePts[i], pchString, curFontData->filename);
					}

					// Add the missing glyph to the current font as a copy of the question mark or first glyph
					if( curFontData )
					{
						GfxFontGlyphData* insertGlyph = 0;
						if( stashIntFindPointer(curFontData->stFontGlyphs, '?', &insertGlyph) )
							stashIntAddPointer(curFontData->stFontGlyphs, codePt, insertGlyph, false);
						else if( eaSize(&curFontData->eaGlyphData) > 0 && stashIntFindPointer(curFontData->stFontGlyphs, curFontData->eaGlyphData[0]->codePoint, &insertGlyph) )
							stashIntAddPointer(curFontData->stFontGlyphs, codePt, insertGlyph, false);
					}

					// Use the global fallback instead.
					pSubFontData = g_fallbackFontData;
					stashIntFindPointer(pSubFontData->stFontGlyphs, codePts[i], &glyph);
				}
			}

			if( pSubFontData != NULL )
			{
				//this font (or sub) has the glyph
				curFontData = pSubFontData;
				texSize[0] = (F32)curFontData->texSize[0];
				texSize[1] = (F32)curFontData->texSize[1];
				scaleFactor = fontPixelSize / (F32)curFontData->fontSize;
			}
			else
			{
				curx += font->renderSize; //leave a gap
			}
		} 

		if (glyph) 
		{ 
			float xscale = glyph->size[0]*scaleFactor*font->aspectRatio; 
			float yscale = glyph->size[1]*scaleFactor; 
			float xpad = (isBold ? curFontData->padding[0]-0.5f : curFontData->padding[0])*scaleFactor*font->aspectRatio/curFontData->spacingAdjustment; 
			float ypad = curFontData->padding[1]*scaleFactor;
			//you dont want it smaller than 0.006, then here is basically no smoothing. 
			//There's really no rhyme or reason to how this value is computed, it just needs to get smaller
			//as the font is scaled up
			float smoothingTightness = MAX(curFontData->spread / scaleFactor / 128.0f, 0.006f); 
			Vec2 topLeftUV = {(float)glyph->texPos[0]/texSize[0], (float)glyph->texPos[1]/texSize[1]}; 
			Vec2 bottomRightUV = {(float)(glyph->texPos[0] + glyph->size[0])/texSize[0], (float)(glyph->texPos[1] + glyph->size[1])/texSize[1]}; 
			float maxDen = 1.0f; 
			float outlineDenWidth = font->outline ? (font->outlineWidth * curFontData->spread / scaleFactor / 128.0f) : 0.0f; 
			float outlineDen = isBold ? 0.455f : 0.485f; 
			float minDen = outlineDen - outlineDenWidth; 
			float italSkew = isItalic ? (scaleFactor * curFontData->fontSize * font->aspectRatio)/4.0f : 0; 
			float advWidth = glyph->xAdvance*scaleFactor*font->aspectRatio - xpad*2.0  - italSkew/4.0f;
			float advHeight = yscale - ypad*2.0f;

			int vertShift = curFontData->verticalShift == SHRT_MIN ? curFontData->maxDescent/2 : curFontData->verticalShift;
			float curY = y - (curFontData->fontSize + vertShift) * scaleFactor;
			float gradStart = 0.0f;
			float gradEnd = 1.0f;

			if(snapToPixels)
			{
				advWidth = floorf(advWidth);
				advHeight = floorf(advHeight);
			}

			//make it a little wider to compensate for the squishing
			xscale += italSkew/2.0f; 
			if(font->outline && (curFontData->outlineDensityOffset != -100000))
			{
				outlineDen += curFontData->outlineDensityOffset;
				minDen += curFontData->outlineDensityOffset;
			}
			else
			{
				outlineDen += curFontData->densityOffset;
				minDen += curFontData->densityOffset;
			}
			
			//this parameter is disabled
			//maxDen += curFontData->densityOffset;

			if ((curFontData->verticalGradientIntensity || curFontData->verticalGradShift) && doSpecialGradientMode)
			{
				gradStart = (curFontData->maxDescent * curFontData->verticalGradientIntensity) / (F32)curFontData->fontSize;
				gradEnd = 1.0f - (curFontData->maxDescent * curFontData->verticalGradientIntensity) / (F32)curFontData->fontSize;
				if (gradStart > gradEnd)
				{
					gradStart = gradEnd = 0.5f;
				}
				gradStart += curFontData->verticalGradShift / (F32)curFontData->fontSize;
				gradEnd += curFontData->verticalGradShift / (F32)curFontData->fontSize;
			}

			{
				//if its negative use the old behavior
				float oulineSmoothingAmt = curFontData->outlineSmoothingAmt >= 0 ? curFontData->outlineSmoothingAmt : curFontData->smoothingAmt;
				//divide by two. Initially there was a bug that made these half as big as what people put and i dont want to break existing settings
				//plus, its just a random scale factor so the actual values don't matter.
				smoothingTightness *= (font->outline ? oulineSmoothingAmt : curFontData->smoothingAmt) / 2.0f; 
			}
			
			if (font->dropShadow)
			{
				Vec2 dropShadowOffset = {(float)font->dropShadowOffset[0]/texSize[0]/scaleFactor*font->aspectRatio, (float)font->dropShadowOffset[1]/texSize[1]/scaleFactor};
				float shadowTightness = font->softShadow ? (font->softShadowSpread * curFontData->spread / scaleFactor / 128.0f) : smoothingTightness;
				
				//add some extra texture lookup to avoid the dropshadow getting cut off
				int extraPixels = MAX(curFontData->extraWidthPixels, font->dropShadowOffset[0]);
				float screenExtraPixels = extraPixels*scaleFactor*font->aspectRatio;
				xscale += 2.0f*screenExtraPixels; 
				topLeftUV[0] -= (float)extraPixels/texSize[0];
				bottomRightUV[0] += (float)extraPixels/texSize[0];

				callback2Layers(advWidth, advHeight, userData,curFontData->pFontTexture, curx-xpad-screenExtraPixels, curY, z, xscale/texSize[0], yscale/texSize[1],
					font->color.uiTopLeftColor, font->color.uiTopRightColor, font->color.uiBottomRightColor, font->color.uiBottomLeftColor,
					topLeftUV[0], topLeftUV[1], bottomRightUV[0], bottomRightUV[1],
					0, italSkew/texSize[0], 0,
					dropShadowOffset[0], dropShadowOffset[1], minDen, minDen, maxDen, shadowTightness, font->uiDropShadowColor, font->uiDropShadowColor, 
					0				   , 0                  , minDen, outlineDen,maxDen, smoothingTightness, RGBAFromColor(ColorWhite), font->uiOutlineColor, 
					gradStart, gradEnd, pProps);
			}
			else
			{
				callback1Layer(advWidth, advHeight, userData, curFontData->pFontTexture, curx-xpad, curY, z, xscale/texSize[0], yscale/texSize[1],
					font->color.uiTopLeftColor, font->color.uiTopRightColor, font->color.uiBottomRightColor, font->color.uiBottomLeftColor,
					topLeftUV[0], topLeftUV[1], bottomRightUV[0], bottomRightUV[1],
					0, italSkew/texSize[0], 0, 
					0,0, minDen, outlineDen, maxDen, smoothingTightness, RGBAFromColor(ColorWhite), font->uiOutlineColor,
					gradStart, gradEnd, pProps);
			}

			curx += advWidth; 
		}

		curCodePoint = UTF8GetNextCodepoint(curCodePoint); 
	} 

}

static __forceinline void gfxFontDisplaySpriteOneLayerFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
											 float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
											 float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	if (verticalGradStart != 0.f || verticalGradEnd != 1.0f)
	{
		if (rgba != rgba2 || rgba3 != rgba4)
		{
			Errorf("You can only have a vertical gradient if you specify explicit gradient points");
		}
		display_sprite_distance_field_one_layer_gradient(btex, xp, yp, zp, xscale, yscale, rgba, rgba3, verticalGradStart, verticalGradEnd, u1, v1, u2, v2, angle, skew, additive, clipperGetCurrent(), uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1, pProps);
	}
	else
	{
		display_sprite_distance_field_one_layer(btex, xp, yp, zp, xscale, yscale, rgba, rgba2, rgba3, rgba4, u1, v1, u2, v2, angle, skew, additive, clipperGetCurrent(), uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1, pProps);
	}
}

static __forceinline void gfxFontDisplaySpriteTwoLayersFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
											  float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
											  float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, 
											  float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2, int rgbaMainL2, int rgbaOutlineL2, float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	if (verticalGradStart != 0.f || verticalGradEnd != 1.0f)
	{
		if (rgba != rgba2 || rgba3 != rgba4)
		{
			Errorf("You can only have a vertical gradient if you specify explicit gradient points");
		}
		
		display_sprite_distance_field_two_layers_gradient(btex, xp, yp, zp, xscale, yscale, rgba, rgba3, verticalGradStart, verticalGradEnd, u1, v1, u2, v2, angle, skew, additive, clipperGetCurrent(),
			uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1,
			uOffsetL2, vOffsetL2, minDenL1, outlineDenL2, maxDenL2, tightnessL2, rgbaMainL2, rgbaOutlineL2, pProps);

	}
	else
	{
		display_sprite_distance_field_two_layers(btex, xp, yp, zp, xscale, yscale, rgba, rgba2, rgba3, rgba4, u1, v1, u2, v2, angle, skew, additive, clipperGetCurrent(),
			uOffsetL1, vOffsetL1, minDenL1, outlineDenL1, maxDenL1, tightnessL1, rgbaMainL1, rgbaOutlineL1,
			uOffsetL2, vOffsetL2, minDenL1, outlineDenL2, maxDenL2, tightnessL2, rgbaMainL2, rgbaOutlineL2, pProps);
	}
	
}


void gfxFontPrintEx(GfxFont* font, F32 x, F32 y, F32 z, const char* pchString, const char* pchStopPtr, SpriteProperties *pProps)
{
	if ( gbNoGraphics )
		return;
	//this should optimize out to a normal function with no function pointers since gfxFontPrintExImpl is force inline and the two funcs are static
	gfxFontPrintExImpl(font, x, y, z, pchString, pchStopPtr, gfxFontDisplaySpriteOneLayerFunc, gfxFontDisplaySpriteTwoLayersFunc, 0, pProps);
}

static __forceinline void gfxFontMeasureSpriteOneLayerFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
	float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1,
	float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	float* outVec2 = (float*)userData;
	outVec2[0] += advWidth;
	MAX1(outVec2[1], advHeight);
}

static __forceinline void gfxFontMeasureSpriteTwoLayersFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
	float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, 
	float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2, int rgbaMainL2, int rgbaOutlineL2,
	float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	float* outVec2 = (float*)userData;
	outVec2[0] += advWidth;
	MAX1(outVec2[1], advHeight);
}

void gfxFontMeasureStringEx( GfxFont* font, const char* pchString, const char* pchStopPtr, Vec2 outSize )
{
	outSize[0] = 0.f;
	outSize[1] = 0.f;
	//this should optimize out to a normal function with no function pointers since gfxFontPrintExImpl is force inline and the two funcs are static
	gfxFontPrintExImpl(font, 0, 0, 0, pchString, pchStopPtr, gfxFontMeasureSpriteOneLayerFunc, gfxFontMeasureSpriteTwoLayersFunc, outSize, NULL);
}

// For counting how many glyphs fit in a given area
// Used for truncating strings
typedef struct Vec2GlyphCount
{
	Vec2 v2AllowedSize;
	Vec2 v2RunningSize;
	unsigned int uiGlyphCount;
} Vec2GlyphCount;

static __forceinline void gfxFontFitGlyphsInAreaOneLayerFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
	float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1,
	float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	Vec2GlyphCount* outCounter = (Vec2GlyphCount*)userData;
	outCounter->v2RunningSize[0] += advWidth;
	MAX1(outCounter->v2RunningSize[1], advHeight);
	if (outCounter->v2RunningSize[0] < outCounter->v2AllowedSize[0])
		outCounter->uiGlyphCount++;
}

static __forceinline void gfxFontFitGlyphsInAreaTwoLayersFunc(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, 
	float u1, float v1, float u2, float v2, float angle, float skew, int additive, 
	float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, 
	float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2, int rgbaMainL2, int rgbaOutlineL2,
	float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps)
{
	Vec2GlyphCount* outCounter = (Vec2GlyphCount*)userData;
	outCounter->v2RunningSize[0] += advWidth;
	MAX1(outCounter->v2RunningSize[1], advHeight);
	if (outCounter->v2RunningSize[0] < outCounter->v2AllowedSize[0])
		outCounter->uiGlyphCount++;
}

unsigned int gfxFontCountGlyphsInArea( GfxFont* font, const char* pchString, Vec2 v2AllowedSize)
{
	//this should optimize out to a normal function with no function pointers since gfxFontPrintExImpl is force inline and the two funcs are static
	// Or at least that's what the comment in the previous function says! 
	Vec2GlyphCount Counter = { 0 };
	Counter.v2AllowedSize[0] = v2AllowedSize[0];
	Counter.v2AllowedSize[1] = v2AllowedSize[1];
	gfxFontPrintExImpl(font, 0, 0, 0, pchString, NULL, gfxFontFitGlyphsInAreaOneLayerFunc, gfxFontFitGlyphsInAreaTwoLayersFunc, &Counter, NULL);
	return Counter.uiGlyphCount;
}

void gfxFontPrintMultiline(GfxFont* font, F32 x, F32 y, F32 z, const char* pchString)
{
	const char* curCodePoint = pchString; 
	const char* curStart = pchString;

	bool isBold, isItalic;
	GfxFontData* curFontData = applyFontDataSubstitutions(font, g_debugDataOverride, &isBold, &isItalic); 
	F32 fontPixelSize = font->sizeIsPixels ? font->renderSize : gfxFontGetPixelsFromPts(font->renderSize);
	F32 scaleFactor = fontPixelSize / (F32)curFontData->fontSize; 
	F32 origx = x;

	while (true) 
	{ 
		U16 codePt = UTF8ToWideCharConvert(curCodePoint); 
		bool printed = false;
		switch (codePt)
		{
			//handle any combination of /r, /n, /n/r, /r/n
			case '\r':
			case '\n':
			{
				const char* nextCodePt = UTF8GetNextCodepoint(curCodePoint); 
				gfxFontPrintEx(font, x, y, z, curStart, curCodePoint, NULL);
				x = origx;
				y += floorf(fontPixelSize - curFontData->padding[1]*2.0f*scaleFactor);
				//if we are part of a two-byte thing skip the next byte
				if ((*nextCodePt == '\r' || *nextCodePt == '\n') && *nextCodePt != codePt)
				{
					curCodePoint = nextCodePt;
				}
				printed = true;
				break;
			}
			xcase '\t':
				gfxFontPrintEx(font, x, y, z, curStart, curCodePoint, NULL);
				x += floorf(fontPixelSize * 4.f * font->aspectRatio);
				printed = true;
			xcase '\0':
				gfxFontPrintEx(font, x, y, z, curStart, curCodePoint, NULL);
				printed = true;
				return;
		}
		curCodePoint = UTF8GetNextCodepoint(curCodePoint); 
		if (printed) curStart = curCodePoint;
	} 
}

F32 gfxFontGetGlyphWidth(GfxFont* font, U16 codePt)
{
	Vec2 tempOut = {0,0};
	
	//this should optimize out to a normal function with no function pointers since gfxFontPrintExImpl is force inline and the two funcs are static
	gfxFontPrintExImpl(font, 0, 0, 0, WideToUTF8CodepointConvert(codePt), 0, gfxFontMeasureSpriteOneLayerFunc, gfxFontMeasureSpriteTwoLayersFunc, tempOut, NULL);

	return tempOut[0];
}

F32 gfxFontGetPixelHeight(GfxFont* font)
{
	F32 fontPixelSize = font->sizeIsPixels ? font->renderSize : gfxFontGetPixelsFromPts(font->renderSize);
	bool isBold, isItalic;
	GfxFontData* curFontData = applyFontDataSubstitutions(font, g_debugDataOverride, &isBold, &isItalic); 
	F32 scaleFactor = fontPixelSize / (F32)curFontData->fontSize; 
	float ypad = curFontData->padding[1]*scaleFactor; 
	return floorf(fontPixelSize);
}

GfxFontData* gfxFontGetFallbackFontData()
{
	return g_fallbackFontData;
}

void gfxFontSetFallbackFontData(GfxFontData* fontData)
{
	g_fallbackFontData = fontData;
}

void gfxFontPrintExWithFuncs( GfxFont* font, F32 x, F32 y, F32 z, const char* pchString, const char* pchStopPtr, gfxFont_display_sprite_distance_field_one_layer_func callback1Layer, gfxFont_display_sprite_distance_field_two_layers_func callback2Layers, void* userData )
{
	//there needs to be a way of calling this externally for texwords and the function is static forceinline
	gfxFontPrintExImpl(font, x, y, z, pchString, pchStopPtr, callback1Layer, callback2Layers, userData, NULL);
}


#include "GfxFont_h_ast.c"
#include "GfxFontPrivate_h_ast.c"
