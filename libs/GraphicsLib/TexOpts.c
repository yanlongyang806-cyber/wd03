#include "TexOpts.h"
#include "ImageUtil.h"
#include "GfxTextures.h"
#include "GfxDebug.h"
#include "StashTable.h"
#include "StringCache.h"

#include "error.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "Color.h"
#include "AutoGen/GfxTexOpts_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););
AUTO_RUN_ANON(memBudgetAddMapping("GfxTexOpts", BUDGET_Materials););

static StaticDefineInt tex_flags[] = {
	DEFINE_INT
	{ "None",			0 },
	{ "Exclude",		TEXOPT_EXCLUDE },
	{ "ClampS",			TEXOPT_CLAMPS },
	{ "ClampT",			TEXOPT_CLAMPT },
	{ "RepeatS",		0 },
	{ "RepeatT",		0 },
	{ "MirrorS",		TEXOPT_MIRRORS },
	{ "MirrorT",		TEXOPT_MIRRORT },
	{ "Truecolor",		0 },
	{ "sRGB",			TEXOPT_SRGB },
	{ "NoMip",			TEXOPT_NOMIP },
	{ "Jpeg",			TEXOPT_JPEG },
	{ "MagnificationFilterPoint", TEXOPT_MAGFILTER_POINT },
	{ "FixAlphaMIPs",	TEXOPT_FIX_ALPHA_MIPS },
	{ "Split",			TEXOPT_SPLIT },
	{ "AlphaBorder",	TEXOPT_ALPHABORDER },
	{ "AlphaBorderLR",	TEXOPT_ALPHABORDER_LR },
	{ "AlphaBorderTB",	TEXOPT_ALPHABORDER_TB },
	{ "ColorBorder",	TEXOPT_COLORBORDER },
	{ "ForFallback",	TEXOPT_FOR_FALLBACK},
	{ "ColorBorderLegacy",	TEXOPT_COLORBORDER_LEGACY },
	{ "NoAniso",		TEXOPT_NO_ANISO },
	{ "LightMap",		TEXOPT_LIGHTMAP },
	{ "ReversedMips",	TEXOPT_REVERSED_MIPS },
	{ "Crunch",			TEXOPT_CRUNCH },
	DEFINE_END
};

ParseTable parse_tex_opt[] = { // Needs to be in sync with parse_folder_tex_opt
	// Internal:
	{ "IsFolder",		TOK_INT(TexOpt,is_folder, 0)},
	{ "TexOpt",			TOK_IGNORE,	}, // For loading a single TexOpt file
	{ "F",				TOK_POOL_STRING|TOK_CURRENTFILE(TexOpt,file_name)},
	{ "TS",				TOK_TIMESTAMP(TexOpt,fileAge)},
	{ "TextureName",	TOK_NO_TEXT_SAVE|TOK_POOL_STRING|TOK_STRING(TexOpt,texture_name,0)},
	// Parsed:
	{ "Flags",			TOK_AUTOINT(TexOpt,flags,0),tex_flags, TOK_FORMAT_FLAGS},
	{ "MipFilter",		TOK_INT(TexOpt,mip_filter, 0), TexOptMipFilterTypeEnum},
	{ "MipSharpening",	TOK_INT(TexOpt,mip_sharpening, 0), TexOptMipSharpeningEnum},
	{ "Quality",		TOK_INT(TexOpt,quality, 0), TexOptQualityEnum},
	{ "Compression",	TOK_INT(TexOpt,compression_type, 0), TexOptCompressionTypeEnum},
	{ "Fade",			TOK_IGNORE }, // TOK_VEC2(TexOpt,texopt_fade) },
	{ "AlphaMIPThreshold", TOK_F32(TexOpt, alpha_mip_threshold, 0)},
	{ "BorderColor",	TOK_RGBA(TexOpt, border_color.rgba)},
	{ "TexReduceScale",	TOK_IGNORE },
	{ "HighLevelSize",	TOK_INT16(TexOpt, high_level_size, 0)},
	{ "MinLevelSplit",	TOK_U8(TexOpt, min_level_split, 0)},
	{ "End",			TOK_END,			0},

	{ "", 0, 0 }
};

ParseTable parse_folder_tex_opt[] = { // Needs to be in sync with parse_tex_opt
	// Internal:
	{ "IsFolder",		TOK_INT(TexOpt,is_folder, 1)},
	{ "TexOpt",			TOK_IGNORE,	}, // For loading a single TexOpt file
	{ "F",				TOK_POOL_STRING|TOK_CURRENTFILE(TexOpt,file_name)},
	{ "TS",				TOK_TIMESTAMP(TexOpt,fileAge)},
	// Parsed:
	{ "FolderName",		TOK_STRUCTPARAM|TOK_POOL_STRING|TOK_STRING(TexOpt,texture_name,0)},
	{ "Flags",			TOK_AUTOINT(TexOpt,flags,0),tex_flags, TOK_FORMAT_FLAGS},
	{ "MipFilter",		TOK_INT(TexOpt,mip_filter, 0), TexOptMipFilterTypeEnum},
	{ "MipSharpening",	TOK_INT(TexOpt,mip_sharpening, 0), TexOptMipSharpeningEnum},
	{ "Quality",		TOK_INT(TexOpt,quality, 0), TexOptQualityEnum},
	{ "Compression",	TOK_INT(TexOpt,compression_type, 0), TexOptCompressionTypeEnum},
	{ "Fade",			TOK_IGNORE }, // TOK_VEC2(TexOpt,texopt_fade) },
	{ "AlphaMIPThreshold", TOK_F32(TexOpt, alpha_mip_threshold, 0)},
	{ "BorderColor",	TOK_RGBA(TexOpt, border_color.rgba)},
	{ "TexReduceScale",	TOK_IGNORE },
	{ "HighLevelSize",	TOK_INT16(TexOpt, high_level_size, 0)},
	{ "MinLevelSplit",	TOK_U8(TexOpt, min_level_split, 0)},
	{ "End",			TOK_END,			0},

	{ "", 0, 0 }
};


AUTO_RUN;
void initTexoptTPIs(void)
{
	ParserSetTableInfo(parse_folder_tex_opt, sizeof(TexOpt), "FolderTexOpt", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_tex_opt, sizeof(TexOpt), "TexOpt", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

static TexOptReloadCallback texoptReloadCallback;
TexOptList gTexOptList = { NULL };
StashTable texopt_name_hashes;
NinePatchList gNinePatchList= { NULL };
StashTable stNinePatch;

TexOptFlags texOptFlagsFromName(const char *cName, TexOptFlags origFlags)
{
	TexOptFlags flags=origFlags | TEXOPT_SRGB;
	char namebuf[MAX_PATH];
	char *name, *ext;
	int i, len;
	strcpy(namebuf, cName);
	ext = strrchr(namebuf, '.');
	if (ext)
	{
		*ext = 0;
		if (ext[-1] == '9' && ext[-2] == '.')
			ext[-2] = '\0'; // remove .9
		ext++;
	}
	name = namebuf;
	len = (int)strlen(name);
	if (strEndsWith(name,"_bump"))
		flags |= TEXOPT_BUMPMAP;
	else if (strEndsWith(name,"_normal"))
		flags |= TEXOPT_NORMALMAP;
	else if (strEndsWith(name,"_N"))
		flags |= TEXOPT_NORMALMAP;
	else if (len > 3 && name[len-3]=='_' && (toupper((unsigned char)name[len-2])=='N')) {
		flags |= TEXOPT_NORMALMAP; // "_NZ" or "_NS" etc
		if (toupper((unsigned char)name[len-1]) == 'X' && !(flags & TEXOPT_COMPRESSION_MASK))
		{
			flags |= (COMPRESSION_DXT5NM << TEXOPT_COMPRESSION_SHIFT);
		}
	}
	else if (strEndsWith(name,"_negx") ||
			strEndsWith(name,"_negy") ||
			strEndsWith(name,"_negz") ||
			strEndsWith(name,"_posx") ||
			strEndsWith(name,"_posy") ||
			strEndsWith(name,"_posz") ||
			strEndsWith(name,"_cube")) {
		flags |= TEXOPT_CUBEMAP;
	}
	else if (strEndsWith(name, "_voltex"))
		flags |= TEXOPT_VOLUMEMAP;

	for (i = 1; i < 5 && i < len && !(flags & TEXOPT_VOLUMEMAP); ++i)
	{
		char c = name[len-i];
		name[len-i] = 0;
		if (strEndsWith(name,"_slice"))
			flags |= TEXOPT_VOLUMEMAP | TEXOPT_NOMIP;
		name[len-i] = c;
	}

	if (stricmp(ext, "tif")==0) {
		flags |= TEXOPT_RGBE;
		flags &= ~TEXOPT_SRGB;
	}

	if (flags & (TEXOPT_NORMALMAP | TEXOPT_BUMPMAP)) {
		flags &= ~TEXOPT_SRGB;
		if (!(flags & TEXOPT_COMPRESSION_MASK)) {
            flags |= (COMPRESSION_HALFRES_TRUECOLOR << TEXOPT_COMPRESSION_SHIFT);
		}
	}

	return flags;
}


static void setupTexOpt(TexOpt *tex)
{
	char buf[64];
	char *s;

	if (!tex->is_folder) {
		tex->texture_name = getFileNameConst(tex->file_name);
		if (tex->texture_name[0]=='/')
			tex->texture_name++;
		strcpy(buf, tex->texture_name);
		assert(strEndsWith(buf, ".texopt"));
		s = strrchr(buf, '.');
		*s = '\0';
		// TODO: Use filename.wtex grabbed from the texture for better pooling?
		tex->texture_name = allocAddString(buf);
	} else {
		// Folder.  Nothing special?
	}

	tex->flags &= ~TEXOPT_DO_NOT_SAVE_OR_READ_FLAGS;

	tex->flags |= tex->compression_type << TEXOPT_COMPRESSION_SHIFT;

	tex->flags = texOptFlagsFromName(tex->texture_name, tex->flags); // add TEXOPT_BUMPMAP, COMPRESSION_HALFRES_TRUECOLOR

	// Reset these in case texOptFlagsFromName adjusted them
	tex->compression_type = (tex->flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT;
}

TexOpt *texoptFromTextureName(const char *name, TexOptFlags *texopt_flags)
{
	char	buf[1200],*s,*s2,*slash;
	TexOpt	*tex;

	strcpy(buf,name);
	forwardSlashes(buf);
	slash = strrchr(buf,'/');
	if (!slash++)
		slash = buf;

	// Remove extension and .9
	s = strrchr(buf,'.');
	if (s && s > slash)
	{
		*s = 0;
		if (s[-1] == '9' && s[-2]=='.')
			s[-2] = '\0';
	}
	// Remove texture locale
	s = strrchr(buf, '#');
	if (s)
		*s = 0;

	{
		char *e = slash + strlen(slash) - 5;
		if (e > slash &&
			(!stricmp(e,"_negx") || !stricmp(e,"_negy") || !stricmp(e,"_negz") ||
			!stricmp(e,"_posx") || !stricmp(e,"_posy") || !stricmp(e,"_posz"))) {
			strcpy_s(e, sizeof(buf) - (e - buf), "_Cube");
		}
	}
 
	// look for direct match
	stashFindPointer( texopt_name_hashes,slash, &tex );
	if (tex) {
		if (texopt_flags)
			*texopt_flags = texOptFlagsFromName(name, tex->flags);
		return tex;
	}

	// look for directory match
	strcpy(buf,name);
	forwardSlashes(buf);
	s = strstri(buf,"texture_library");
	if (!s) {
		s = buf;
	} else {
		s += strlen("texture_library");
	}
	if (s[0]=='/') s++;

	for(;;)
	{
		s[strlen(s)-1] = 0;
		s2 = strrchr(s,'/');
		if (!s2)
			break;
		s2[1] = 0;

		stashFindPointer( texopt_name_hashes,s, &tex );
		if (tex) {
			if (texopt_flags)
				*texopt_flags = texOptFlagsFromName(name, tex->flags);
			return tex;
		}
	}
	if (texopt_flags)
		*texopt_flags = texOptFlagsFromName(name, 0);

	return 0;
}

void texoptLoadPreProcess(ParseTable pti[], TexOptList *pList)
{
	int		i;
	for(i=eaSize(&pList->ppTexOpts)-1;i>=0;i--)
		setupTexOpt(pList->ppTexOpts[i]);
}

AUTO_FIXUPFUNC;
TextParserResult fixupTextOptList(TexOptList *pList, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		texoptLoadPreProcess(parse_TexOptList, pList);
	}

	return PARSERESULT_SUCCESS;
}

void texoptLoadPostProcess(void)
{
	int		i;

	if (!texopt_name_hashes)
		texopt_name_hashes = stashTableCreateWithStringKeys(1024,StashDefault);
	else
		stashTableClear(texopt_name_hashes);

	for(i=eaSize(&gTexOptList.ppTexOpts)-1;i>=0;i--) {
		TexOpt	*dup_tex;
		TexOpt *tex = gTexOptList.ppTexOpts[i];
		if (!stashAddPointer(texopt_name_hashes,tex->texture_name,tex, false))
		{
			stashFindPointer( texopt_name_hashes,tex->texture_name, &dup_tex );
			ErrorFilenameDup(tex->file_name, dup_tex->file_name, tex->texture_name, "TexOpt");
		}
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupNinePatch(NinePatch *np, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char buf[MAX_PATH];
			char *s;
			np->texture_name = getFileNameConst(np->file_name);
			if (np->texture_name[0]=='/')
				np->texture_name++;
			strcpy(buf, np->texture_name);
			assert(strEndsWith(buf, ".NinePatch"));
			s = strrchr(buf, '.');
			*s = '\0';
			// TODO: Use filename.wtex grabbed from the texture for better pooling?
			np->texture_name = allocAddString(buf);
		}
	}

	return PARSERESULT_SUCCESS;
}

void ninePatchLoadPostProcess(void)
{
	if (!stNinePatch)
		stNinePatch = stashTableCreateWithStringKeys(32,StashDefault);
	else
		stashTableClear(stNinePatch);

	FOR_EACH_IN_EARRAY(gNinePatchList.ppNinePatches, NinePatch, np)
	{
		NinePatch *dup;
		if (!stashAddPointer(stNinePatch, np->texture_name, np, false))
		{
			stashFindPointer(stNinePatch, np->texture_name, &dup );
			ErrorFilenameDup(np->file_name, dup->file_name, np->texture_name, "NinePatch");
		}
	}
	FOR_EACH_END;
}


#ifndef TEXOPTEDITOR
void texoptSetReloadCallback(TexOptReloadCallback callback)
{
	texoptReloadCallback = callback;
}

static void reloadTexoptProcessor(const char *relpath, int UNUSED_when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	PERFINFO_AUTO_START("TexOptReload",1);
	//	printf("Trick file changed (%s), reloading..\n", relpath);
	PERFINFO_AUTO_START("ParserReloadFile",1);
	if (!ParserReloadFile((char*)relpath, parse_TexOptList, &gTexOptList, NULL, 0))
	{
		PERFINFO_AUTO_STOP();
		ErrorFilenamef(relpath, "Error reloading TexOpt file: %s\n", relpath);
		gfxStatusPrintf("Error reloading texopt file: %s\n.", relpath);
	} else {
		// Fix up hash tables, re-initialize structures, etc
		PERFINFO_AUTO_STOP_START("Post-process",1);
		//loadstart_printf("post-process..");
		PERFINFO_AUTO_START("texoptLoadPreProcess",1);
		//loadstart_printf("trickLoadPostProcess..");
		texoptLoadPreProcess(parse_TexOptList, &gTexOptList);
		PERFINFO_AUTO_START("texoptLoadPostProcess",1);
		texoptLoadPostProcess();
		//loadend_printf("done.");
		PERFINFO_AUTO_STOP();
		texResetBinds();
		//loadend_printf("Done.");
		PERFINFO_AUTO_STOP();
		gfxStatusPrintf("TexOpts reloaded.");
	}

	if (texoptReloadCallback)
		texoptReloadCallback(relpath);

	PERFINFO_AUTO_STOP();
}

static void reloadNinePatchProcessor(const char *relpath, int UNUSED_when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!ParserReloadFile((char*)relpath, parse_NinePatchList, &gNinePatchList, NULL, 0))
	{
		ErrorFilenamef(relpath, "Error reloading NinePatch file: %s\n", relpath);
		gfxStatusPrintf("Error reloading NinePatch file: %s\n.", relpath);
	} else {
		// Fix up hash tables, re-initialize structures, etc
		ninePatchLoadPostProcess();
		gfxStatusPrintf("NinePatch reloaded.");
	}
}
#endif

void texoptLoad()
{
	loadstart_printf("Loading TexOpts...");


	if (!ParserLoadFilesShared("SM_TexOpts", "texture_library", ".texopt", "TexOpts.bin", PARSER_BINS_ARE_SHARED, parse_TexOptList, &gTexOptList))
	{
		// Error loading, already got a pop-up
	}

	texoptLoadPostProcess();

	// These don't get auto-run in TexOptEditor because of StructParser stupidity
	ParserSetTableInfo(parse_NinePatchList, sizeof(NinePatchList), "NinePatchList", NULL, "GfxTexOpts.h", SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_NinePatch, sizeof(NinePatch), "NinePatch", NULL, "GfxTexOpts.h", SETTABLEINFO_ALLOW_CRC_CACHING);

	ParserLoadFiles("texture_library", ".NinePatch", "NinePatch.bin", PARSER_OPTIONALFLAG, parse_NinePatchList, &gNinePatchList);
	ninePatchLoadPostProcess();

#ifndef TEXOPTEDITOR
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "texture_library/*.texopt", reloadTexoptProcessor);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "texture_library/*.NinePatch", reloadNinePatchProcessor);
#endif
	loadend_printf(" done (%d TexOpts)", eaSize(&gTexOptList.ppTexOpts));
}

// F32 *texoptGetFade(TexOpt *texopt)
// {
// 	static F32 null_ret[] = {0, 0};
// 	if (!texopt)
// 		return null_ret;
// 	return texopt->texopt_fade;
// }

TexOptMipFilterType texoptGetMipFilterType(const TexOpt *texopt)
{
	if (texopt)
		return texopt->mip_filter;
	return MIP_KAISER;
}

TexOptMipSharpening texoptGetMipSharpening(const TexOpt *texopt)
{
	if (texopt)
		return texopt->mip_sharpening;
	return SHARPEN_NONE;
}

const char *texoptGetMipSharpeningString(const TexOpt *texopt)
{
	return StaticDefineIntRevLookup(TexOptMipSharpeningEnum, texoptGetMipSharpening(texopt));
}

TexOptQuality texoptGetQuality(const TexOpt *texopt)
{
	if (texopt)
		return texopt->quality;
	return QUALITY_PRODUCTION;
}

TexOptCompressionType texoptGetCompressionType(const TexOpt *texopt, TexOptFlags texopt_flags)
{
	if (texopt && texopt->compression_type != COMPRESSION_AUTO)
		return texopt->compression_type;
	return (texopt_flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT;
}

// returns true if the crunch flag should be set based on the compression type and quality setting
bool texoptShouldCrunch(const TexOpt *tex_opt, TexOptFlags texopt_flags)
{
	switch (texoptGetCompressionType(tex_opt, texopt_flags)) {
	case COMPRESSION_AUTO:
	case COMPRESSION_DXT1:
	case COMPRESSION_DXT5:
	case COMPRESSION_DXT5NM:
		break;
	default:
		return false;
	}

	switch (texoptGetQuality(tex_opt)) {
	case QUALITY_LOWEST:
	case QUALITY_MEDIUM:
		break;
	default:
		return false;
	}

	return true;
}

Color texoptGetBorderColor(const TexOpt *texopt)
{
	if (texopt)
		return texopt->border_color;
	return ColorBlack;
}

F32 texoptGetAlphaMipThreshold(const TexOpt *texopt)
{
	if (texopt && texopt->alpha_mip_threshold)
		return texopt->alpha_mip_threshold;
	return 0.95;
}

const NinePatch *texGetNinePatch(const char *texname)
{
	char buf[MAX_PATH];
	NinePatch *ret;
	getFileNameNoExt(buf, texname);
	if (stashFindPointer(stNinePatch, buf, &ret))
		return ret;
	return NULL;
}




#include "AutoGen/GfxTexOpts_h_ast.c"
