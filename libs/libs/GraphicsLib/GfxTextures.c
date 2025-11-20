#include "GfxTextures.h"
#include "GfxTexturesInline.h"
#include "CrypticDXT.h"
#include "GfxDXT.h"
#include "GfxTextureTools.h"
#include "GfxMaterials.h"
#include "GfxTexAtlas.h"
#include "GfxTexWords.h"
#include "GfxConsole.h"
#include "Color.h"
#include "texWords.h"
#include "UnitSpec.h"
#include "DynamicCache.h"
#include "ScratchStack.h"
#include "genericlist.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "strings_opt.h"
#include "memlog.h"
#include "DirectDrawTypes.h"
#include "jpeg.h"
#include "crunch.h"
#include "RdrTexture.h"
#include "RdrState.h"
#include "GraphicsLibPrivate.h"
#include "TexOpts.h"
#include "texUnload.h"
#include "tex_gen.h"
#include "GfxLoadScreens.h"
#include "GfxTexAtlasPrivate.h"
#include "fileLoader.h"
#include "fileLoaderStats.h"
#include "endian.h"
#include "wlEditorIncludes.h"
#include "MemRef.h"
#include "hoglib.h"
#include "GfxSpriteList.h"
#include "logging.h"
#include "MemoryPoolDebug.h"
#include "ImageUtil.h"

#include "GfxTexturesPublic_h_ast.c"

// Checking that padding/alignment works as I think it should, also preventing anyone from
//  growing this structure (large memory cost as there are 10s of thousands of these)
#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
STATIC_ASSERT(sizeof(BasicTexture) == 15*4);
STATIC_ASSERT(offsetof(BasicTexture, tex_is_loaded) == 9*4);
STATIC_ASSERT(offsetof(BasicTexture, bt_texopt_flags) == 13*4);
STATIC_ASSERT(offsetof(BasicTexture, fullname) == 14*4);
#else
STATIC_ASSERT(sizeof(BasicTexture) == 14*4);
STATIC_ASSERT(offsetof(BasicTexture, tex_is_loaded) == 9*4);
STATIC_ASSERT(offsetof(BasicTexture, bt_texopt_flags) == 12*4);
STATIC_ASSERT(offsetof(BasicTexture, fullname) == 13*4);
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););
AUTO_RUN_ANON(memBudgetAddMapping("GfxTexturesPublic.h", BUDGET_Materials););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:texBackgroundLoadingThread", BUDGET_Renderer););

void texReadInfoMemRefAlloc_dbg(TexReadInfo * info, size_t sizeBytes, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS)
{
	if (info->texture_data)
		texReadInfoMemRefDecrement_dbg(info, pBasicTexture, funcName MEM_DBG_PARMS_CALL);
	info->texture_data = memrefAllocInternal(sizeBytes MEM_DBG_PARMS_CALL);
	memlog_printf(rdrGetTextureMemLog(), "%s:%d memrefAlloc(%d) = %p, BT = %p '%s' tex_handle = %"FORM_LL"u", 
		funcName, line, sizeBytes, info->texture_data, pBasicTexture, pBasicTexture ? pBasicTexture->name : "'No BT'", pBasicTexture ? pBasicTexture->tex_handle : 0L);
}

void texReadInfoAssignMemRefAlloc_dbg(TexReadInfo * info, void *data_refcounted, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS)
{
	int newDataRefCount = 0;
	if (info->texture_data)
		texReadInfoMemRefDecrement_dbg(info, pBasicTexture, funcName MEM_DBG_PARMS_CALL);
	info->texture_data = data_refcounted;

	memrefIncrement(info->texture_data);
	newDataRefCount = memrefDecrement(info->texture_data);

	memlog_printf(rdrGetTextureMemLog(), "%s:%d assigned memref count (%p) = %d, BT = %p '%s' tex_handle = %"FORM_LL"u", 
		funcName, line, info->texture_data, newDataRefCount, pBasicTexture, pBasicTexture ? pBasicTexture->name : "'No BT'", pBasicTexture ? pBasicTexture->tex_handle : 0L);
}

int texReadInfoMemRefIncrement_dbg(TexReadInfo * info, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS)
{
	int refCount = memrefIncrement(info->texture_data);
	memlog_printf(rdrGetTextureMemLog(), "%s:%d memrefIncrement(%p) = %d, BT = %p %s tex_handle = %"FORM_LL"u", 
		funcName, line, info->texture_data, refCount, pBasicTexture, pBasicTexture ? pBasicTexture->name : "'No BT'", pBasicTexture ? pBasicTexture->tex_handle : 0L);
	return refCount;
}

int texReadInfoMemRefDecrement_dbg(TexReadInfo * info, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS)
{
	int refCount = memrefDecrement(info->texture_data);
	memlog_printf(rdrGetTextureMemLog(), "%s:%d memrefDecrement(%p) = %d, BT = %p %s tex_handle = %"FORM_LL"u", 
		funcName, line, info->texture_data, refCount, pBasicTexture, pBasicTexture ? pBasicTexture->name : "'No BT'", pBasicTexture ? pBasicTexture->tex_handle : 0L);
	if (!refCount)
		info->texture_data = NULL;
	return refCount;
}

// This function dumps the state of the given texture to an attached debugger.
// See texLog and ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE. Modify this fuction to control
// the information in the detailed texture lifetime tracing/logging to debug texture 
// loading/reloading/streaming issues.
void texLogState(const BasicTexture *bind, const char *strEvent, MemLog *destMemlog)
{
	const BasicTextureLoadedData *loaded_data = bind->loaded_data;
	OutputDebugStringf("%s %s: loaded_data=0x%p, %s, %s. %s, Low mipped on 0x%x, LowMIPLoad %s, Loaded on 0x%x, Loading for 0x%x, %s, %d levels\n", strEvent, 
		bind->name,
		loaded_data, 
		loaded_data ? ( !loaded_data->loading ? "Loaded" : "Loading" ) : "Not loaded", 
		loaded_data ? ( loaded_data->tex_is_unloading ? "Unloading" : "NotUnloading" ) : "Not loaded", 
		loaded_data && loaded_data->loading ? "Loading" : "",
		loaded_data ? loaded_data->mip_bound_on : 0, 
		loaded_data && loaded_data->mip_loading ? "Y" : "N",
		bind->tex_is_loaded,
		loaded_data ? loaded_data->tex_is_loading_for : 0,
		loaded_data && texIsFullyLoadedInline(bind) ? "Fully loaded!" : "Not ready!",
		loaded_data ? loaded_data->levels_loaded : 0);
}

#define MAX_TEX_SIZE	2048
#define MAX_TEX_LEVELS	12

#define MAX_MIP_CACHE_SIZE 16 // Largest size of mip map that could be cached/preloaded (actual size is probably 8x8)
#define STATIC_EXTRA_DATA_SIZE (MAX_MIP_CACHE_SIZE*MAX_MIP_CACHE_SIZE*4*6 + MAX_PATH)

#define TEX_HAS_MIPDATA(bind) ((bind)->mip_type != TEXMIP_NONE)

typedef struct TexThreadPackage
{
	struct TexThreadPackage * next;
	struct TexThreadPackage * prev;

	BasicTexture *bind;		// bind being worked on
	TexReadInfo info;
	U32 needRawData:1;
	U32 fromHeap:1;
	U32 fromHtex:1;
	U32 needHtex:1;
	U32 firstLevel:4;
	U32 levelsNeeded:4;
	U32 needsFree:1;
} TexThreadPackage;

int g_needTextureBudgetInfo = 0;
int disable_parallel_tex_thread_loader = 0; // Doesn't work anymore, I think
int delay_texture_loading = 0;  // Adds a delay to simulate slow loading
BasicTexture *white_tex, *invisible_tex, *dummy_bump_tex, *dummy_cube_tex, *black_tex, *dummy_volume_tex;
BasicTexture *default_env_cubetex, *default_env_spheretex, *default_ambient_cube;
BasicTexture *tex_from_sky_file;
BasicTexture *tex_use_pn_tris;
static BasicTexture fake_white = {0};
static MemLog tex_memlog;

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
__forceinline void texLog(const BasicTexture *bind, const char *strEvent)
{
	if (bind && bind->bEnableDetailedLifetimeLog)
		texLogState(bind, strEvent, &tex_memlog);
}
#else
#define texLog( bind, strEvent )
#endif

// Adds a delay to simulate slow loading of textures
AUTO_CMD_INT(delay_texture_loading, delayTextureLoading) ACMD_CMDLINE;

int texLoadCalls;

static CRITICAL_SECTION CriticalSectionTexLoadQueues; //for the thread linked list communication
static CRITICAL_SECTION CriticalSectionQueueingLoads; // blocked as long as the thread is still queueing loads
static CRITICAL_SECTION CriticalSectionTexLoadData; // blocked whenever texLoadData is running (to allow it to be called from both threads)
static volatile long numTexLoadsInThread=0;
static TexThreadPackage * basicTexturesReadyForFinalProcessing;
static TexThreadPackage *queuedTexLoadPkgs;
static int texsSentToRenderer;
volatile U32 texMemoryUsage[TEX_MEM_MAX]={0};

static int texCheckForSwaps(BasicTexture *basicTexture);
static void texFindNeededBinds(void);
static void texCheckThreadLoader(void);
void texCheckSwapList(void);
static void texLoadBasicInternalInternal(BasicTexture *match, TexLoadHow mode, int rawData, U32 num_levels);
static void texConvertToPooledFormat(TexReadInfo *info, bool reversed_mips);
void texAddToProcessingList(TexThreadPackage *pkg);

BasicTexture **g_basicTextures;
StashTable g_basicTextures_ht=0;
MP_DEFINE(BasicTexture);
MP_DEFINE(BasicTextureRareData);
MP_DEFINE(BasicTextureLoadedData);
MP_DEFINE(TexThreadPackage);

ParseTable parse_TextureFileHeader[] = {
	{"header_size", TOK_INT(TextureFileHeader, header_size, 0)},
	{"file_size",	TOK_INT(TextureFileHeader, file_size, 0)},
	{"width",		TOK_INT(TextureFileHeader, width, 0)},
	{"height",		TOK_INT(TextureFileHeader, height, 0)},
	{"flags",		TOK_INT(TextureFileHeader, flags, 0)},
//	{"unused2",		TOK_F32(TextureFileHeader, unused2, 0)},
//	{"unused",		TOK_F32(TextureFileHeader, unused, 0)},
	{"rdr_format",	TOK_INT(TextureFileHeader, rdr_format, 0)},
	{"alpha",		TOK_U8(TextureFileHeader, alpha, 0)},
	{"verpad",		TOK_FIXEDSTR(TextureFileHeader, verpad)},
	{ 0 },
};

ParseTable parse_DDCOLORKEY[] = {
	{"dwColorSpaceLowValue",	TOK_INT(DDCOLORKEY, dwColorSpaceLowValue, 0)},
	{"dwColorSpaceHighValue",	TOK_INT(DDCOLORKEY, dwColorSpaceHighValue, 0)},
	{0},
};
ParseTable parse_DDPIXELFORMAT[] = {
	{"dwSize",			TOK_INT(DDPIXELFORMAT, dwSize, 0)},
	{"dwFlags",			TOK_INT(DDPIXELFORMAT, dwFlags, 0)},
	{"dwFourCC",		TOK_INT(DDPIXELFORMAT, dwFourCC, 0)},
	{"dwRGBBitCount",	TOK_INT(DDPIXELFORMAT, dwRGBBitCount, 0)},
	{"dwRBitMask",		TOK_INT(DDPIXELFORMAT, dwRBitMask, 0)},
	{"dwGBitMask",		TOK_INT(DDPIXELFORMAT, dwGBitMask, 0)},
	{"dwBBitMask",		TOK_INT(DDPIXELFORMAT, dwBBitMask, 0)},
	{"dwRGBAlphaBitMask", TOK_INT(DDPIXELFORMAT, dwRGBAlphaBitMask, 0)},
	{0},
};
ParseTable parse_DDSCAPS2[] = {
	{"dwCaps",	TOK_INT(DDSCAPS2, dwCaps, 0)},
	{"dwCaps2", TOK_INT(DDSCAPS2, dwCaps2, 0)},
	{"dwCaps3", TOK_INT(DDSCAPS2, dwCaps3, 0)},
	{"dwCaps4", TOK_INT(DDSCAPS2, dwCaps4, 0)},
	{0},
};
ParseTable parse_DDSURFACEDESC2[] = {
	{"dwSize",			TOK_INT(DDSURFACEDESC2, dwSize, 0)},
	{"dwFlags",			TOK_INT(DDSURFACEDESC2, dwFlags, 0)},
	{"dwHeight",		TOK_INT(DDSURFACEDESC2, dwHeight, 0)},
	{"dwWidth",			TOK_INT(DDSURFACEDESC2, dwWidth, 0)},
	{"dwLinearSize",	TOK_INT(DDSURFACEDESC2, dwLinearSize, 0)},
	{"dwDepth",			TOK_INT(DDSURFACEDESC2, dwDepth, 0)},
	{"dwMipMapCount",	TOK_INT(DDSURFACEDESC2, dwMipMapCount, 0)},
	{"dwAlphaBitDepth", TOK_INT(DDSURFACEDESC2, dwAlphaBitDepth, 0)},
	{"dwReserved",		TOK_INT(DDSURFACEDESC2, dwReserved, 0)},
	{"lpSurface",		TOK_INT(DDSURFACEDESC2, lpSurface, 0)},
	{"ddckCKDestOverlay", TOK_EMBEDDEDSTRUCT(DDSURFACEDESC2, ddckCKDestOverlay, parse_DDCOLORKEY) },
	{"ddckCKDestBlt",	TOK_EMBEDDEDSTRUCT(DDSURFACEDESC2, ddckCKDestBlt, parse_DDCOLORKEY) },
	{"ddckCKSrcOverlay",TOK_EMBEDDEDSTRUCT(DDSURFACEDESC2, ddckCKSrcOverlay, parse_DDCOLORKEY) },
	{"ddckCKSrcBlt",	TOK_EMBEDDEDSTRUCT(DDSURFACEDESC2, ddckCKSrcBlt, parse_DDCOLORKEY) },
	{"ddpfPixelFormat", TOK_EMBEDDEDSTRUCT(DDSURFACEDESC2, ddpfPixelFormat, parse_DDPIXELFORMAT) },
	{"ddsCaps",			TOK_EMBEDDEDSTRUCT(DDSURFACEDESC2, ddsCaps, parse_DDSCAPS2) },
	{"dwTextureStage",	TOK_INT(DDSURFACEDESC2, dwTextureStage, 0)},
	{0},
};
ParseTable parse_TextureFileMipHeader[] = {
	{"structsize",	TOK_INT(TextureFileMipHeader, structsize, 0)},
	{"width",		TOK_INT(TextureFileMipHeader, width, 0)},
	{"height",		TOK_INT(TextureFileMipHeader, height, 0)},
	{0},
};

MemLog *texGetMemLog(void) {
	return &tex_memlog;
}

void texDisableThreadedLoading(void) {
	disable_parallel_tex_thread_loader++;
}

void texEnableThreadedLoading(void) {
	disable_parallel_tex_thread_loader--;
	if (disable_parallel_tex_thread_loader<0) {
		assert(!"Bad!");
		disable_parallel_tex_thread_loader=0;
	}
}

const char *texGetName(const BasicTexture *texBind)
{
	const TexWordParams *texWordParams;
	if (texBind && texGetRareDataConst(texBind) && (texWordParams = texGetRareDataConst(texBind)->texWordParams))
	{
		int i;
		char *ret=NULL;
		const char *ret2;
		estrStackCreate(&ret);

		estrAppend2(&ret, "\\");
		estrAppend2(&ret, texBind->name);
		for (i = 0; i < eaSize(&texWordParams->parameters); i++)
		{
			estrAppend2(&ret, "\\");
			estrAppend2(&ret, texWordParams->parameters[i]);
		}
		ret2 = allocAddString(ret);
		estrDestroy(&ret);
		return ret2;
	}
	return SAFE_MEMBER(texBind, name);
}

const char *texGetFullname(const BasicTexture *texBind)
{
	return SAFE_MEMBER(texBind, fullname);
}

bool texIsNormalmap(const BasicTexture *texBind)
{
	return !!(texBind->bt_texopt_flags & (TEXOPT_BUMPMAP|TEXOPT_NORMALMAP));
}

bool texIsDXT5nm(const BasicTexture *texBind)
{
	return ((texBind->bt_texopt_flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT) == COMPRESSION_DXT5NM;
}

bool texIsCubemap(const BasicTexture *texBind)
{
	return !!(texBind->bt_texopt_flags & TEXOPT_CUBEMAP);
}

bool texIsVolume(const BasicTexture *texBind)
{
	return !!(texBind->bt_texopt_flags & TEXOPT_VOLUMEMAP);
}

bool texIsAlphaBordered(const BasicTexture *texBind)
{
	return !!(texBind->bt_texopt_flags & TEXOPT_ALPHABORDER);
}


BasicTextureRareData *texAllocRareData(BasicTexture *bind)
{
	if (!bind->has_rare)
	{
		assert(!bind->rare);
		bind->has_rare = 1;
		bind->rare = MP_ALLOC(BasicTextureRareData);
		return bind->rare;
	} else {
		assert(bind->rare);
		return bind->rare;
	}
}

void texRareDataForceRelease(BasicTexture *bind)
{
	if (bind->has_rare)
	{
		assert(bind->rare);
		MP_FREE(BasicTextureRareData, bind->rare);
		bind->rare = NULL;
		bind->has_rare = 0;
	} else {
		assert(!bind->rare);
	}
}

void texRareDataOptionallyRelease(BasicTexture *bind)
{
	if (bind->has_rare)
	{
		BasicTextureRareData *rare_data = bind->rare;
		U32 *p = (U32*)rare_data;
		int len = sizeof(*rare_data) / sizeof(U32);
		p++;
		len--;
		while (len)
		{
			if (*p) // Anything non-zero
				return;
			p++;
			len--;
		}
		// Everything zeroed, get rid of it!
		texRareDataForceRelease(bind);
	}
}

U16 texGetOrigWidth(const BasicTexture *bind)
{
	const BasicTextureRareData *rare_data = texGetRareDataConst(bind);
	return (rare_data && rare_data->origWidth)?rare_data->origWidth:bind->width;
}

U16 texGetOrigHeight(const BasicTexture *bind)
{
	const BasicTextureRareData *rare_data = texGetRareDataConst(bind);
	return (rare_data && rare_data->origHeight)?rare_data->origHeight:bind->height;
}

U16 texGetDepth(const BasicTexture *bind)
{
	const BasicTextureRareData *rare_data = texGetRareDataConst(bind);
	return rare_data?rare_data->realDepth:0;
}

TexWord *texGetTexWord(BasicTexture *bind)
{
	BasicTextureRareData *rare_data = texGetRareData(bind);
	return rare_data?rare_data->texWord:NULL;
}

TexWordParams *texGetTexWordParams(BasicTexture *bind)
{
	BasicTextureRareData *rare_data = texGetRareData(bind);
	return rare_data?rare_data->texWordParams:NULL;
}

U32 texGetRawReferenceCount(BasicTexture *bind)
{
	BasicTextureRareData *rare_data = texGetRareData(bind);
	return rare_data?rare_data->rawReferenceCount:0;
}

U32 texGetDynamicReferenceCount(BasicTexture *bind)
{
	BasicTextureRareData *rare_data = texGetRareData(bind);
	return rare_data?rare_data->dynamicReferenceCount:0;
}



void texLoadedDataRelease(BasicTexture *bind)
{
	assert(!bind->tex_is_loaded);
	if (bind->loaded_data)
	{
		MP_FREE(BasicTextureLoadedData, bind->loaded_data);
		bind->loaded_data = NULL;
	} else {
		assert(!bind->loaded_data);
	}
}

void texLoadedDataOptionallyRelease(BasicTexture *bind)
{
	if (!bind->tex_is_loaded)
	{
		if (bind->loaded_data)
		{
			devassert(!bind->loaded_data->tex_memory_use[TEX_MEM_LOADING]);
			devassert(!bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO]);
			devassert(!bind->loaded_data->tex_memory_use[TEX_MEM_RAW]);
		}
		texLoadedDataRelease(bind);
	}
}


BasicTextureLoadedData *texAllocLoadedData(BasicTexture *bind)
{
	if (!bind->loaded_data)
	{
		bind->loaded_data = MP_ALLOC(BasicTextureLoadedData);
	}
	return bind->loaded_data;
}




typedef void (*NewTextureCallback)(BasicTexture *bind);
static NewTextureCallback newTextureCallback;
void texSetNewTextureCallback(NewTextureCallback callback)
{
	newTextureCallback = callback;
}

// 0=RGBA8, 1=F16, 2=F32
static int volumeTextureMode=0;
AUTO_CMD_INT(volumeTextureMode, volumeTextureMode) ACMD_CATEGORY(Debug);

// bitfield of axises to shrink on, 1=x, 2=y, 4=z - can shrink twice by also doing 8=x, 16=y, 32=z (e.g. 63 shrinks all axises twice)
static int volumeTextureShrink=0;
AUTO_CMD_INT(volumeTextureShrink, volumeTextureShrink) ACMD_CATEGORY(Debug);

void texSetVolumeGlobalParams(bool needF16, U32 shrinkmask)
{
	if (needF16)
		volumeTextureMode = 1;
	else
		volumeTextureMode = 0;
	volumeTextureShrink = shrinkmask;
}

const static U8 rgb_5_to_8[] = {
	0,8,16,25,33,41,49,58,
	66,74,82,90,99,107,115,123,
	132,140,148,156,165,173,181,189,
	197,206,214,222,230,239,247,255,
};

static RdrTexType texTypeFromFlags(TexOptFlags texopt_flags)
{
	RdrTexType tex_type = RTEX_2D;
	if (texopt_flags & TEXOPT_CUBEMAP)
		tex_type = RTEX_CUBEMAP;
	else if (texopt_flags & TEXOPT_VOLUMEMAP)
		tex_type = RTEX_3D;
	return tex_type;
}

static void *texGetTempStreamBuf(size_t bufSz)
{
    static void *textureStreamTmpBuf;
    static size_t textureStreamTmpBufSz;

	if (bufSz > textureStreamTmpBufSz) {
		textureStreamTmpBuf = realloc(textureStreamTmpBuf, bufSz);
		textureStreamTmpBufSz = bufSz;
	}

	return textureStreamTmpBuf;
}

static int crnLoad(FILE *fp, TexReadInfo *info, int needRawData, U32 file_bytes, U32 levelsNeeded, RdrTexType texType, const BasicTexture *bind)
{
	char fileHeader[CRUNCH_HEADER_SIZE];
	const TexBlockInfo *blockInfo;
	U32 width, height, maxLevels, skipLevels;
	size_t crnSize;
	void *crnData;

	fread(&fileHeader, sizeof(fileHeader), 1, fp);

	info->tex_format = texFormatFromCrn(fileHeader);
	if (info->tex_format == RTEX_INVALID_FORMAT) {
		return 0;
	}

	crnGetTextureDims(fileHeader, &width, &height, &maxLevels);

	if (!needRawData && levelsNeeded > 0) {
		assert(levelsNeeded <= maxLevels);
	} else {
		levelsNeeded = maxLevels;
	}

	blockInfo = imgBlockInfo(info->tex_format);
	skipLevels = maxLevels - levelsNeeded;
	info->width = MAX(width >> skipLevels, 1);
	info->width = ALIGNUP(info->width, blockInfo->width);
	info->height = MAX(height >> skipLevels, 1);
	info->height = ALIGNUP(info->height, blockInfo->height);
	info->depth = (texType == RTEX_CUBEMAP ? 6 : 1);
	info->level_count = levelsNeeded;

	// read in the compressed data
	crnSize = crnSizeForLevels(fileHeader, levelsNeeded);

	crnData = texGetTempStreamBuf(crnSize);
	fread(crnData, 1, crnSize, fp);

	info->size = crnDdsSizeForLevels(fileHeader, levelsNeeded, false);
	if (!info->size) {
		return 0;
	}

	info->texture_data = NULL;

	// allocate memory for the uncompressed data
	if (!needRawData) {
		info->texture_data = rdrTextureLoadAlloc(info->size);
		info->ringbuffer_data = 1;
	}

	if (!info->texture_data) {
		// Not sending this texture to the renderer, or too big to fit in the 
		// ring buffer. Alloc refcounted memory.
		texReadInfoMemRefAlloc(info, info->size, bind);
		info->ringbuffer_data = 0;
	}

    if (!info->texture_data) {
        return 0;
    }

	info->size = crnDecompress(info->texture_data, fileHeader, crnData, levelsNeeded, crnNeedsWorkaround(fileHeader, file_bytes));

	return info->size != 0;
}

/*called only by TexLoad Image*/ 
static int ddsLoad(FILE *fp, TexReadInfo *info, int needRawData, U32 levelsNeeded, RdrTexType tex_type, const char *debug_fn, const BasicTexture *bind)
{
	struct {
		char filecode[4];
		DDSURFACEDESC2	ddsd;
	} dds;

	TexOptFlags texopt_flags = bind->bt_texopt_flags;
    U8 *dest;

	if (needRawData)
		levelsNeeded = 0;

	memset(info,0,sizeof(TexReadInfo));

	/* verify the type of file */
	fread(&dds, 1, sizeof(dds), fp);
	if (strncmp(dds.filecode, "DDS ", 4) != 0)
		return 0;

	/* get the surface desc */
	endianSwapStructIfBig(parse_DDSURFACEDESC2, &dds.ddsd);

	info->tex_format = texFormatFromDDSD(&dds.ddsd);
	if (info->tex_format == (RdrTexFormat)-1) {
		return 0;
	}

	{
		U32 mip_count = (dds.ddsd.dwFlags & DDSD_MIPMAPCOUNT) ? dds.ddsd.dwMipMapCount : 1;
        const TexBlockInfo *blockInfo;
		U32 mipsToSkip, origWidth, origHeight;
		int bytes_to_skip=0;
		int pow2Width;
		int pow2Height;
		int i;

		if (levelsNeeded > 0) {
			levelsNeeded = MIN(levelsNeeded, mip_count);
		} else {
			levelsNeeded = mip_count;
		}
		mipsToSkip = mip_count - levelsNeeded;

		origWidth = dds.ddsd.dwWidth;
		origHeight = dds.ddsd.dwHeight;
        blockInfo = imgBlockInfo(info->tex_format);
		dds.ddsd.dwWidth = MAX(dds.ddsd.dwWidth >> mipsToSkip, 1);
		dds.ddsd.dwWidth = ALIGNUP(dds.ddsd.dwWidth, blockInfo->width);
		dds.ddsd.dwHeight = MAX(dds.ddsd.dwHeight >> mipsToSkip, 1);
		dds.ddsd.dwHeight = ALIGNUP(dds.ddsd.dwHeight, blockInfo->height);
		dds.ddsd.dwMipMapCount = levelsNeeded;

		info->ringbuffer_data = 0;
		info->size = imgByteCount(tex_type, info->tex_format, dds.ddsd.dwWidth, dds.ddsd.dwHeight, dds.ddsd.dwWidth, levelsNeeded);
		info->level_count = levelsNeeded;
		info->width	= dds.ddsd.dwWidth;
		info->height = dds.ddsd.dwHeight;

		if ((bind->flags & TEX_REVERSED_MIPS) == 0) {
			bytes_to_skip = imgByteCount(tex_type, info->tex_format, origWidth, origHeight, origWidth, mip_count); 
			bytes_to_skip -= info->size;
		}

		if (tex_type == RTEX_CUBEMAP)
		{
			info->depth = 6;
		}
		else if (tex_type == RTEX_3D)
		{
			assert(dds.ddsd.dwWidth == dds.ddsd.dwHeight);
			info->depth = info->width;
		}
		else
		{
			info->depth = 1;
		}

		if (needRawData) {
			int total_bytes = info->size + sizeof(dds);
			// Need the "whole" .dds file, but with the new parameters
			texReadInfoMemRefAlloc(info, total_bytes, bind);
			memcpy(info->texture_data, &dds, sizeof(dds));
			dest = info->texture_data + sizeof(dds);
			info->size = total_bytes;
			info->tex_format = TEXFMT_RAW_DDS;
		} else {
			// Normal texture read
           	texReadInfoMemRefAlloc(info, info->size, bind);
			dest = info->texture_data;
		}

		if (bytes_to_skip) {
			fseek(fp, bytes_to_skip, SEEK_CUR);
		}

		fread(dest, 1, info->size, fp);

		pow2Width = pow2(info->width);
		pow2Height = pow2(info->height);
		assertmsgf(!texIsCompressedFormat(info->tex_format) || (pow2Width % 4 == 0 && pow2Height % 4 == 0), "Bad texture dimensions for file %s", debug_fn);
		if(   (pow2Width != info->width || pow2Height != info->height)
			  && !gfx_state.dxt_non_pow2 && info->tex_format != TEXFMT_RAW_DDS)
		{
			int pow2BytesToRead = imgByteCount(RTEX_2D, info->tex_format, pow2Width, pow2Height, 1, levelsNeeded );
			U32 srcPitch = imgMinPitch(info->tex_format, info->width);
			U32 dstPitch = imgMinPitch(info->tex_format, pow2Width);
			U32 blocksH = (info->height + blockInfo->height - 1) >> blockInfo->hshift;
			U8* data;
			U32 it;

			if( levelsNeeded > 1 ) {
				ErrorFilenamef(debug_fn, "Non power of two textures encountered with mipmaps.  This is not allowed." );
				return 0;
			}
			assert( !needRawData );

			data = memrefAlloc( pow2BytesToRead);
            for( it = 0; it < blocksH; ++it ) {
                memcpy( data + it * dstPitch, info->texture_data + it * srcPitch, srcPitch);
            }
			texReadInfoAssignMemRefAlloc(info, data, bind);
			info->size = pow2BytesToRead;
			info->width = pow2Width;
			info->height = pow2Height;
		}

		if (((texopt_flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT) == COMPRESSION_1555 &&
			(info->tex_format == RTEX_BGRA_U8 || info->tex_format == RTEX_BGR_U8) && !rdr_state.dx11Renderer && !needRawData)
		{
			// Want 16bpp format, but was processed as RGBA uncompressed (new textures, since DX11 can't use it)
			int new_size = imgByteCount(RTEX_2D, RTEX_BGRA_5551, info->width, info->height, 1, levelsNeeded );
			U8* data;
			U32 it, mip;
			U32 mip_w=info->width;
			U32 mip_h=info->height;
			ColorBGRA *walkin = (ColorBGRA*)info->texture_data;
			Color5551 *walkout;

			data = memrefAlloc(new_size);
			walkout = (Color5551*)data;

			for (mip=0; mip<levelsNeeded; mip++)
			{
				for (it=0; it<mip_w*mip_h; it++, walkout++)
				{
					walkout->r = ((int)walkin->r + 4)*31/255;
					walkout->g = ((int)walkin->g + 4)*31/255;
					walkout->b = ((int)walkin->b + 4)*31/255;
					if (info->tex_format == RTEX_BGR_U8)
					{
						walkout->a = 1;
						walkin = (ColorBGRA*)((U8*)walkin + 3);
					} else {
						walkout->a = walkin->a>127;
						walkin++;
					}
				}
				mip_w>>=1;
				if (!mip_w)
					mip_w = 1;
				mip_h>>=1;
				if (!mip_h)
					mip_h = 1;
			}

			texReadInfoAssignMemRefAlloc(info, data, bind);
			info->size = new_size;
			info->tex_format = RTEX_BGRA_5551;
		}
		if (info->tex_format == RTEX_BGRA_5551 && rdr_state.dx11Renderer && !needRawData)
		{
			// Doesn't support it, convert to truecolor
			int new_size = imgByteCount(RTEX_2D, RTEX_BGRA_U8, info->width, info->height, 1, levelsNeeded );
			U8* data;
			U32 it, mip;
			U32 mip_w=info->width;
			U32 mip_h=info->height;
			Color5551 *walkin = (Color5551*)info->texture_data;
			ColorBGRA *walkout;

			data = memrefAlloc(new_size);
			walkout = (ColorBGRA*)data;

			for (mip=0; mip<levelsNeeded; mip++)
			{
				for (it=0; it<mip_w*mip_h; it++, walkin++, walkout++)
				{
					walkout->r = rgb_5_to_8[walkin->r];
					walkout->g = rgb_5_to_8[walkin->g];
					walkout->b = rgb_5_to_8[walkin->b];
					walkout->a = walkin->a*255;
				}
				mip_w>>=1;
				if (!mip_w)
					mip_w = 1;
				mip_h>>=1;
				if (!mip_h)
					mip_h = 1;
			}

			texReadInfoAssignMemRefAlloc(info, data, bind);
			info->size = new_size;
			info->tex_format = RTEX_BGRA_U8;
		}

		if (tex_type == RTEX_3D && 
			(info->tex_format == RTEX_DXT1 || info->tex_format == RTEX_DXT3 || info->tex_format == RTEX_DXT5) &&
			!(gfx_state.allRenderersFeatures & FEATURE_DXT_VOLUME_TEXTURE))
		{
			int new_tex_format = RTEX_BGRA_U8;
			size_t new_bytes_to_read = imgByteCount(RTEX_2D, new_tex_format, info->width, info->height, 1, 1);
			size_t bytes_to_read = imgByteCount(RTEX_2D, info->tex_format, info->width, info->height, 1, 1);
			int new_size = new_bytes_to_read * info->depth;
			U8 *new_data = memrefAlloc(new_size);
			U32 it;

			assert(levelsNeeded == 1);

			for (it = 0; it < info->depth; ++it)
				dxtDecompressDirect(info->texture_data + it * bytes_to_read, new_data + it * new_bytes_to_read, info->width, info->height, info->tex_format);

			texReadInfoAssignMemRefAlloc(info, new_data, bind);
			info->size = new_size;
			info->tex_format = new_tex_format;
			bytes_to_read = new_bytes_to_read;
		}

		if (tex_type == RTEX_3D && info->tex_format == RTEX_BGRA_U8)
		{
			int tex_size[3];
			int tex_scale[3] = {1, 1, 1};
			int tex_data_size;
			
			tex_size[0] = info->width;
			tex_size[1] = info->height;
			tex_size[2] = info->depth;

			for (i=0; i<3; i++) 
			{
				if (volumeTextureShrink & (1<<i))
				{
					tex_size[i]/=2;
					tex_scale[i]*=2;
				}
				if (volumeTextureShrink & (1<<(i+3)))
				{
					tex_size[i]/=2;
					tex_scale[i]*=2;
				}
			}

			if (volumeTextureShrink)
			{
				int x, y, z;
				PERFINFO_AUTO_START("resize", 1);
				for (z=0; z<tex_size[2]; z++)
				{
					for (y=0; y<tex_size[1]; y++)
					{
						for (x=0; x<tex_size[0]; x++)
						{
							for (i=0; i<4; i++)
							{
								info->texture_data[(x + y*tex_size[0] + z*tex_size[0]*tex_size[1])*4 + i] = info->texture_data[(x*tex_scale[0] + y*tex_scale[1]*info->width + z*tex_scale[2]*info->width*info->height)*4 + i];
							}
						}
					}
				}
				PERFINFO_AUTO_STOP();
			}

			if (volumeTextureMode == 0)
			{
				tex_data_size = tex_size[0]*tex_size[1]*tex_size[2]*4*sizeof(U8);
			}
			else if (volumeTextureMode == 1)
			{
				// FP16 textures seem to fix the interpolation issue on GF7 cards
				F16 *temp;
				tex_data_size = tex_size[0]*tex_size[1]*tex_size[2]*4*sizeof(F16);
				temp = memrefAlloc(tex_data_size);
				PERFINFO_AUTO_START("convert to F16", 1);
				for (i=0; i<tex_size[0]*tex_size[1]*tex_size[2]; i++) {
					temp[i*4+0] = U8toF16[info->texture_data[i*4+2]];
					temp[i*4+1] = U8toF16[info->texture_data[i*4+1]];
					temp[i*4+2] = U8toF16[info->texture_data[i*4+0]];
					temp[i*4+3] = U8toF16[info->texture_data[i*4+3]];
				}
				PERFINFO_AUTO_STOP();
				texReadInfoAssignMemRefAlloc(info, temp, bind);
				info->tex_format = RTEX_RGBA_F16;
			}
			else
			{
				F32 *temp;
				tex_data_size = tex_size[0]*tex_size[1]*tex_size[2]*4*sizeof(F32);
				temp = memrefAlloc(tex_data_size);
				PERFINFO_AUTO_START("convert to F32", 1);
				for (i=0; i<tex_size[0]*tex_size[1]*tex_size[2]; i++) {
					temp[i*4+0] = info->texture_data[i*4+2] * (1.f/255.f);
					temp[i*4+1] = info->texture_data[i*4+1] * (1.f/255.f);
					temp[i*4+2] = info->texture_data[i*4+0] * (1.f/255.f);
					temp[i*4+3] = info->texture_data[i*4+3] * (1.f/255.f);
				}
				PERFINFO_AUTO_STOP();
				texReadInfoAssignMemRefAlloc(info, temp, bind);
				info->tex_format = RTEX_RGBA_F32;
			}

			info->width = tex_size[0];
			info->height = tex_size[1];
			info->depth = tex_size[2];
			info->size = tex_data_size;
		}
	}
	return 1;
}

static void makeTexPow2(TexReadInfo *info)
{
	U32	i,w,h;
	U8	*data;

	w = 1 << log2(info->width);
	h = 1 << log2(info->height);
	if (w == info->width && h == info->height)
		return;
	data = memrefAlloc(w*h*3);
	for(i=0;i<info->height;i++)
	{
		memcpy(&data[i*w*3],&info->texture_data[i*info->width*3],info->width*3);
	}
	texReadInfoAssignMemRefAlloc(info, data, NULL);
	info->width = w;
	info->height = h;
	info->size = w*h*3;
}

static void EnterQueueingLoadsCS()
{
	EnterCriticalSection(&CriticalSectionQueueingLoads);
}

static void LeaveQueueingLoadsCS()
{
	LeaveCriticalSection(&CriticalSectionQueueingLoads);
}

void texFindFullName(const BasicTexture *texBind, char *filename, size_t filename_size)
{
	strcpy_s(SAFESTR2(filename), texBind->fullname);
	//STR_COMBINE_SSSSS_S(filename, filename_size, "texture_library/", texBind->dirname, "/", texBind->name, ".wtex");
}

void texRecordNewMemUsage(BasicTexture *bind, TexMemIndex mem_index, U32 new_size)
{
	if (bind->loaded_data)
	{
		S32 delta = new_size - bind->loaded_data->tex_memory_use[mem_index];
		bind->loaded_data->tex_memory_use[mem_index] = new_size;
		InterlockedExchangeAdd(&texMemoryUsage[mem_index], delta);
	}
}

/* load texture data from the .wtex file.  Called from either thread */
static bool texLoadData(const char *filename, TexThreadPackage *pkg)
{
	FILE	*tex_file;
	TexReadInfo * info;
	BasicTexture * bind;
	RdrTexType tex_type;
	U32 file_bytes;
	U32 levelsNeeded;

	//printf("loading tex: %s\n", pkg->bind->name);

	bind = pkg->bind;
	info = &pkg->info;


	if (bind->loaded_data->tex_is_unloading || (pkg->fromHtex && bind->loaded_data->levels_loaded == 0))
	{
		texLog(bind, "texLoadData canceled");
		// we got vetoed.  Don't load this anymore
		return false;
	}

	if (pkg->fromHtex) {
		levelsNeeded = pkg->levelsNeeded - bind->base_levels;
	} else {
		levelsNeeded = MIN(pkg->levelsNeeded, bind->base_levels);
	}

	// to simulate slow texture loading
#pragma warning(suppress:6240) // a && 1 always == a
#pragma warning(suppress:6326) // potential comparison of a constant with another constant
	if (delay_texture_loading && (TexLoadHowFromTexture(bind) != TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD)) {
		Sleep(delay_texture_loading);
	}

	assertmsg(!texGetTexWordParams(bind), "Should not go through this pipe anymore");

	//memlog_printf(&tex_memlog, "%u: texLoadData %s%s", gfx_state.client_frame_timestamp, pkg->needRawData?"RAW ":" ", bind->name);

	EnterCriticalSection(&CriticalSectionTexLoadData);

	tex_type = texTypeFromFlags(bind->bt_texopt_flags);

	tex_file = fileOpen(filename, "rb");
	file_bytes = fileSize(filename);

	if (!tex_file) {
		Errorf("File '%s' not found when loading texture '%s'", filename, bind->name);
		LeaveCriticalSection(&CriticalSectionTexLoadData);
		return false;
	}

	if (bind->flags & (TEX_REVERSED_MIPS | TEX_CRUNCH))
		setvbuf(tex_file, NULL, _IONBF, 0);

	if (!pkg->fromHtex) {
		file_bytes -= bind->file_pos;
        fseek(tex_file,bind->file_pos,0);
	}

	if (bind->flags & TEX_JPEG)
	{
		char	*mem = malloc(file_bytes);
		void	*decodedJPGData = NULL;
		int w, h;

		fileLoaderStartLoad();
		fread(mem,file_bytes,1,tex_file);
		fileLoaderCompleteLoad(file_bytes);

		jpegLoadMemRef(mem,file_bytes,&decodedJPGData,&w, &h, &info->size);
		info->width = w;
		info->height = h;
		info->depth = 1;
		info->level_count = 1;
		info->tex_format = RTEX_BGR_U8;

		texReadInfoAssignMemRefAlloc(info, decodedJPGData, bind);
		free(mem);
		if (info->texture_data)// JE: can probably do this:  && !gfx_state.dxt_non_pow2
			makeTexPow2(info);
	} else if (bind->flags & TEX_CRUNCH) {
		fileLoaderStartLoad();
		crnLoad(tex_file, info, pkg->needRawData, file_bytes, levelsNeeded, tex_type, bind);
       	fileLoaderCompleteLoad(file_bytes);
		if (!pkg->needRawData) {
			bind->rdr_format = info->tex_format;
			bind->realWidth = info->width;
			bind->realHeight = info->height;
		}
	} else {
		fileLoaderStartLoad();
		ddsLoad(tex_file, info, pkg->needRawData, levelsNeeded, tex_type, filename, bind);
		fileLoaderCompleteLoad(file_bytes);
		if (!pkg->needRawData) {
			if (tex_type != RTEX_3D && bind->rdr_format != RTEX_BGRA_5551 && info->tex_format != RTEX_BGRA_5551) // CD: ddsLoad can change the texture format for 3D textures; JE: Also for 5551 textures on DX11
				assert(!info->tex_format || info->tex_format == bind->rdr_format); // Otherwise bad data?  Can happen with -quickloadtextures?
			bind->rdr_format = info->tex_format;
			bind->realWidth = info->width;
			bind->realHeight = info->height;
		}
	}

	bind->loaded_data->levels_loaded = info->level_count + (pkg->fromHtex ? bind->base_levels : 0);
	texLog(bind, "texLoadData done");

	fclose(tex_file);
	LeaveCriticalSection(&CriticalSectionTexLoadData);

	if (!info->texture_data) {
		Errorf("Error loading texture data from '%s'", filename);
		return false;
	}

	if (pkg->needRawData && info->texture_data) {
		TexReadInfo *rawInfo;
		//assert(bind->rawReferenceCount);
		// Need to flag as loaded in the main thread to prevent thread unsafe modifying of variables
		texAllocRareData(bind)->bt_rawInfo = rawInfo = calloc(sizeof(TexReadInfo), 1);
		*rawInfo = *info;
	}

	return true;
}

static TexOpt *texoptFromTextureDirName(const char *dirname, const char *name, TexOptFlags *texopt_flags)
{
	char	buf[1000];

	STR_COMBINE_SSS(buf, dirname, "/", name);
	return texoptFromTextureName(buf, texopt_flags);
}


/* Removes tex file extension, 
s = input name, res = chopped name (can be same ptr) */
void texFixName(const char *s,char *res, int size)
{
	char *ext;

	if (s != res)
		strcpy_s(res, size, s);
	if (res[0]!='#' && res[0]!='\\' && !strchr(res, '{'))
	{
		ext = strrchr(res, '.');
		if (ext)
			*ext = '\0';
		strcat_s(res, size, ".wtex");
	}
}

int texResetTexOptParametersBasic(BasicTexture *bind)
{
	TexOptFlags texopt_flags = 0;
	TexFlags old_flags = bind->flags;
	TexOptFlags old_texopt_flags = bind->bt_texopt_flags;
	TexOpt	*texopt;

	// Clear flags that should only be set in this function
	bind->flags &= ~(0); // Nothing

	texopt = texoptFromTextureName(bind->fullname, &texopt_flags);
	if (texopt && !texopt->is_folder) {
		char path[MAX_PATH];
		changeFileExt(bind->fullname, ".TexOpt", path);
		if (stricmp(path, texopt->file_name)!=0) {
			ErrorFilenameGroupf(texopt->file_name, "Art", 14, "Texture with an associated TexOpt has moved, you must also move the TexOpt to the correct location (\"%s\").", path);
		}
	}

	bind->bt_texopt_flags = texopt_flags;

	// Check to see if any flags changed that need a reload
#define RELOAD_TEXOPT_FLAGS (TEXOPT_MAGFILTER_POINT|TEXOPT_CLAMPS|TEXOPT_CLAMPT|TEXOPT_MIRRORS|TEXOPT_MIRRORT|TEXOPT_NO_ANISO|TEXOPT_COMPRESSION_MASK)
	if ((old_texopt_flags&RELOAD_TEXOPT_FLAGS) != (bind->bt_texopt_flags&RELOAD_TEXOPT_FLAGS))
		return 1;
	return 0;

}

static bool g_check_np2_tiled=true;
AUTO_CMD_INT(g_check_np2_tiled, check_np2_tiled) ACMD_CMDLINE;

static void texSetBindsSubBasic(BasicTexture *bind)
{
	BasicTextureRareData *rare_data;
	TexWord *texWord;

	if (bind->width && // Dynamic textures have no dimensions at create time
		!(bind->bt_texopt_flags & TEXOPT_NOMIP) &&
		!(isPower2(bind->width) && isPower2(bind->height)))
	{
		ErrorFilenameGroupf(bind->fullname, "Art", 14, "All mipmapped textures must be a power of two.  Texture is %d x %d.", bind->width, bind->height);
	}

	if (g_check_np2_tiled &&
		bind->width &&
		(!isPower2(bind->width) && !(bind->bt_texopt_flags & TEXOPT_CLAMPS) ||
		!isPower2(bind->height) && !(bind->bt_texopt_flags & TEXOPT_CLAMPT)))
	{
		ErrorFilenamef(bind->fullname, "Texture is both non-power of 2 (%dx%d) and is tiled, this is not allowed", bind->width, bind->height);
	}

	bind->tex_is_loaded = 0;
	texLog(bind, "texSetBindsSubBasic releasing");
	texLoadedDataRelease(bind);

	rare_data = texGetRareData(bind);
	if (rare_data)
	{
		rare_data->hasBeenComposited = 0;
		rare_data->texWord = NULL;
	}
	texWord = texWordFind(bind->name, 0); // Note: this returns NULL originally because TexWords are not yet loaded
	if (texWord)
	{
		if (!rare_data)
			rare_data = texAllocRareData(bind);
		rare_data->texWord = texWord;
		rare_data->origWidth = bind->width;
		rare_data->origHeight = bind->height;
	}
}

//Set flags and such in the Tex_Binds.  Must be done after all texLoadHeaders because 
//tex_links can refer to binds in any .rom file, so all need to be loaded.
static void texSetBinds(void)
{
	int	i,count;

	//set bind flags and such (done in separate for loop so texbind names are all in place for texfind
	count = eaSize(&g_basicTextures);
	for( i = 0 ; i < count ; i++ )
	{
		texSetBindsSubBasic(g_basicTextures[i]);
	}
}

void texResetBinds(void)
{
	int	i,count;

	count = eaSize(&g_basicTextures);
	for( i = 0 ; i < count; i++ )
	{
		if (texResetTexOptParametersBasic(g_basicTextures[i])) {
			//printf("reload: texture %s needs to be reloaded from disk because of TexOpt changes\n", g_basicTextures[i]->name);
			texFree(g_basicTextures[i], 0);
		}
	}
}

void texResetAnisotropic(void)
{
	gfx_state.rendererNeedsAnisoReset = gfx_state.allRenderersFlag;
}

void texResetReduceTimestamps(void)
{
	int i;
	for (i=eaSize(&g_basicTextures)-1; i>=0; i--) {
		if (g_basicTextures[i]->loaded_data)
			g_basicTextures[i]->loaded_data->tex_reduce_last_time = 0;
	}
}

static void texResetAnisotropicInternal(void)
{
	int i;
	for (i=eaSize(&g_basicTextures)-1; i>=0; i--) {
		BasicTexture *bind = g_basicTextures[i];
		//fonts always need anisotropic filtering
		if (bind->tex_handle && !(bind->use_category & WL_FOR_FONTS)) {
			int aniso = gfx_state.settings.texAniso;
			if (bind->bt_texopt_flags&(TEXOPT_MAGFILTER_POINT|TEXOPT_NO_ANISO))
				aniso = 1;
			rdrSetTextureAnisotropy(gfx_state.currentDevice->rdr_device, bind->tex_handle, aniso);
		}
	}
}

static U32 texEstimateMemUsage(BasicTexture *bind, U32 num_levels)
{
	U32 width, height, depth;
	RdrTexType tex_type = texTypeFromFlags(bind->bt_texopt_flags);
	const TexBlockInfo *blockInfo = imgBlockInfo(bind->rdr_format);
	int levels_skipped = bind->max_levels - num_levels;
	U32 memusage;

	width = MAX(1, bind->width >> levels_skipped);
	width = ALIGNUP(width, blockInfo->width);
	height = MAX(1, bind->height >> levels_skipped);
	height = ALIGNUP(height, blockInfo->height);

	if (tex_type == RTEX_3D) {
		depth = MAX(1, width >> levels_skipped);
	} else {
		depth = width;
	}

	// Anticipate the amount of VRAM/managed pool memory the texture will need
	memusage = imgByteCount(tex_type, bind->rdr_format, width, height, depth, num_levels);
	if (num_levels > bind->base_levels) {
		// If loading an htex, we need to count that memory again as we still need CPU side space for it
		// after VRAM has been alloced.
		memusage += memusage - imgByteCount(tex_type, bind->rdr_format, width, height, depth, num_levels - bind->base_levels);
	}

	return memusage;
}

void gfxTexReduce(BasicTexture *tex, int new_levels)
{
	if (!tex->loaded_data || tex->loaded_data->tex_is_unloading)
		return;

	if (tex->loaded_data->loading || new_levels == tex->loaded_data->levels_loaded) {
		return;
	}
	
	if (texOverMemory(texEstimateMemUsage(tex, new_levels))) {
		return;
	}

	texLog(tex, "gfxTexReduce Start");
    tex->loaded_data->loading = 1;
    // Set tex_is_loading_for == to what it's currently loaded on
    // Must leave RAW bitmask alone, so or it in.
    texAllocLoadedData(tex)->tex_is_loading_for |= tex->tex_is_loaded;
	texLog(tex, "gfxTexReduce End");

	// Change reduction level
	// Start new load going
	texLoadBasicInternalInternal(tex, TEX_LOAD_IN_BACKGROUND, 0, new_levels);
}

// increments or decrements the reduction level on a specific texture
AUTO_COMMAND ACMD_CATEGORY(Debug);
void texReduce(ACMD_NAMELIST(g_basicTextures_ht, STASHTABLE) const char *texname, int reduce)
{
	BasicTexture *tex = texFind(texname, true);
	if (!tex) {
		conPrintf("Unable to find texture %s", texname);
	} else {
		int levels;
		if (tex->tex_is_loaded) {
            levels = CLAMP((int)tex->loaded_data->levels_loaded - reduce, 0, (int)tex->max_levels);
		} else {
            levels = CLAMP((int)tex->max_levels - reduce, 0, (int)tex->max_levels);
		}
		gfxTexReduce(tex, levels);
	}
}

static RdrTexFormat texMipFormatFromType(int mip_type)
{
	if (mip_type == TEXMIP_DXT1)
		return RTEX_DXT1;
	else if (mip_type == TEXMIP_DXT5)
		return RTEX_DXT5;
	else
		assert(0);
}

static int texMipByteSize(BasicTexture *bind)
{
	return imgByteCount(texTypeFromFlags(bind->bt_texopt_flags), texMipFormatFromType(bind->mip_type), 8, 8, 1, 1);
}

typedef struct t32Bytes
{
	U8 data[32];
} t32Bytes;

static t32Bytes *lowmip_data; // 32-byte granularity because that's exactly 1 8x8xDXT1 or 1/2 8x8xDXT5
static int lowmip_data_count=1; // Skipping first block to keep the index from ever being 0
static int lowmip_data_max;
static int *lowmip_free_32b;
static int *lowmip_free_64b;

static void texCopyMipData(BasicTexture *bind, void *payload, int payload_size)
{
	int expected_size = texMipByteSize(bind);
	int expected_size_with_mips = imgByteCount(texTypeFromFlags(bind->bt_texopt_flags), texMipFormatFromType(bind->mip_type), 8, 8, 0, 4);
	int blocks;
	assert(payload_size == expected_size || payload_size == expected_size_with_mips);
	assert(!bind->mip_data_index);
	blocks = (payload_size+31)/32;
	if (blocks == 1 && eaiSize(&lowmip_free_32b))
		bind->mip_data_index = eaiPop(&lowmip_free_32b);
	else if (blocks == 2 && eaiSize(&lowmip_free_64b))
		bind->mip_data_index = eaiPop(&lowmip_free_64b);
	else {
		bind->mip_data_index = lowmip_data_count;
		assert(bind->mip_data_index == lowmip_data_count); // Checking fitting into reduced number of bits
		dynArrayAddStructs(lowmip_data, lowmip_data_count, lowmip_data_max, blocks);
	}
	memcpy(&lowmip_data[bind->mip_data_index], payload, payload_size);
}

static void texSetupMipData(BasicTexture *bind, void *mipdata, int mipdatasize)
{
	TexReadInfo info = {0};
	TextureFileMipHeader *header = mipdata;
	void *payload;
	int payload_size;
	int expected_size;
	bind->mip_type = TEXMIP_NONE;

	//endianSwapStructIfBig(parse_TextureFileMipHeader, header); // Done in texFillInBind

	if (header->structsize != sizeof(TextureFileMipHeader))
		return;
	if (bind->mip_data_index)
	{
		assert(stricmp(bind->fullname, "dynamicTexture")!=0); // Would double-free the lowmip data
		// Note: this leaks some amount when reloading cubemaps, but should only happen in development
		if (bind->mip_type == TEXMIP_DXT1)
			eaiPush(&lowmip_free_32b, bind->mip_data_index);
		else if (bind->mip_type == TEXMIP_DXT5)
			eaiPush(&lowmip_free_64b, bind->mip_data_index);
		bind->mip_data_index = 0;
	}
	payload = header + 1;
	payload_size = mipdatasize - sizeof(*header);
	if (header->width == 8 && header->height == 8)
	{
		if (bind->rdr_format == RTEX_DXT1)
		{
			// Good, and in appropriate format
			bind->mip_type = TEXMIP_DXT1;
			texCopyMipData(bind, payload, payload_size);
			return;
		} else if (bind->rdr_format == RTEX_DXT5)
		{
			bind->mip_type = TEXMIP_DXT5;
			texCopyMipData(bind, payload, payload_size);
			return;
		}
	}
	if (texTypeFromFlags(bind->bt_texopt_flags) != RTEX_2D && texTypeFromFlags(bind->bt_texopt_flags) != RTEX_CUBEMAP)
	{
		assert(0); // Need to deal with converting/compressing volume textures?
		return;
	}
	// Determine desired format
	if (bind->flags & TEX_ALPHA)
		bind->mip_type = TEXMIP_DXT5;
	else
		bind->mip_type = TEXMIP_DXT1;

	expected_size = texMipByteSize(bind);
 	// Convert to Truecolor, scale, compress
	info.width = MAX(header->width, 1);
	info.height = MAX(header->height, 1);
	texReadInfoMemRefAlloc(&info, payload_size, bind);
	memcpy(info.texture_data, payload, payload_size);
	info.level_count = 1;
	info.size = payload_size;
	info.tex_format = bind->rdr_format;
	info.depth = (texTypeFromFlags(bind->bt_texopt_flags) == RTEX_CUBEMAP)?6:1;
	texConvertToPooledFormat(&info,textureMipsReversed(bind)); // Uncompressed 8x8
	assert(info.width == 8 && info.height == 8);
	assert(info.tex_format == RTEX_BGRA_U8);

	// compress
	payload_size = expected_size;
	payload = ScratchAlloc(payload_size);
	{
		U32 i;
		U8 *out=payload;
		for (i=0; i<info.depth; i++)
		{
			out = dxtCompress(info.texture_data + info.height*info.width*4*i, out, info.width, info.height, info.tex_format, texMipFormatFromType(bind->mip_type));
		}
	}
	texCopyMipData(bind, payload, payload_size);
	ScratchFree(payload);
	assert(0==texReadInfoMemRefDecrement(&info, bind));
}

static char *basefolder=NULL;
static int tex_load_header_count;
static bool g_need_alpha_recalc=false;

__forceinline static bool MayCauseNvidiaDriverShaderOptimizerBug(const TextureFileHeader *tfh)
{
	// check for textures smaller-than 64x64 in RGBA format- or formats converted to RGBA.
	return tfh->width<64 && tfh->height<64 &&
		(tfh->rdr_format == RTEX_BGR_U8 ||
		tfh->rdr_format == RTEX_BGRA_U8 ||
		tfh->rdr_format == RTEX_BGRA_5551 ||
		tfh->rdr_format == RTEX_A_U8);
}

static int logNvidiaDriverBugRGBATextures = 0;
AUTO_CMD_INT(logNvidiaDriverBugRGBATextures, logNvidiaDriverBugRGBATextures) ACMD_CMDLINE ACMD_CATEGORY(DEBUG);


// texFillInBind: called from main thread only at load time and reload time
static int texFillInBind(const char *filename, BasicTexture *bind, bool dynamic) {
	TextureFileHeader tfh;
	char *extradata; // extra space to hold name and mip header stuff
	char *mipdata;
	int	mipdatasize;
	char *s;
	size_t numread=0;
	StashElement	element;
	FILE *tex_file;
	char		buf[1000];
	int old_flags;

	old_flags = bind->flags;
	bind->flags &= ~(TEX_ALPHA|TEX_JPEG|TEX_CRUNCH|TEX_REVERSED_MIPS); // clear flags set in this function

	tex_file = fileOpen(filename, "rbh"); // 'h' tells our filesystem we only need the header
	if (!tex_file) {
		// Only happens on reloading:
		//Errorf("Error opening texture file '%s'!", filename);
		return 1;
	}

	tex_load_header_count++;

	// Read the data
	numread=fread(&tfh, 1, sizeof(tfh), tex_file);
	if (numread!=sizeof(tfh)) {
		Errorf("Error loading texture file '%s'!", filename);
		return 1;
	}

	endianSwapStructIfBig(parse_TextureFileHeader, &tfh);
	numread = tfh.header_size - sizeof(tfh);
	if (numread > STATIC_EXTRA_DATA_SIZE) {
		Errorf("Error loading texture file '%s'!  Too large of cached mipmap data.", filename);
		return 1;
	}
	extradata = ScratchAlloc(numread+1); // Enough to hold texture + filename
	numread=fread(extradata, 1, numread, tex_file);
	if (numread!=tfh.header_size - sizeof(tfh)) {
		Errorf("Error loading texture file '%s'!", filename);
		ScratchFree(extradata);
		return 1;
	}

	mipdata = NULL;
	mipdatasize = 0;
	// Texture file v3 or more with MIPs (not all have mips)
	if (tfh.header_size > sizeof(tfh)) {
		// There's extra data, must be mip map data to preload
		mipdata = extradata; // Pointer to begining of mip header data (right past the header)
		mipdatasize = tfh.header_size - (int)sizeof(tfh);
		endianSwapStructIfBig(parse_TextureFileMipHeader, mipdata);
	}

	fclose(tex_file);

	//Get bind->file_pos
	assert(tfh.header_size < 1024); // That's what can fit in the file_pos bitfield
	bind->file_pos = tfh.header_size;
	//assert(tfh.file_size == fileSize(filename) - bind->file_pos); // That's how it's calculated later

	if (!dynamic)
	{
		//Get bind->fullname, and bind->name
		//devassert(allocFindString(filename)); // Might not be valid on newly created textures?
		bind->fullname = allocAddString(filename);

		s = strrchr(filename, '/');
		if (!s) {
			assert(0);
		} else {
			s++;
			assert(*s); // if the texture name ended in a /, we've got problems
			strcpy(buf, s);
		}
		texFixName(buf, SAFESTR(buf));
		assert(strEndsWith(bind->fullname, buf));
		bind->name = bind->fullname + strlen(bind->fullname) - strlen(buf); // Point into fullname for memory savings
		// We don't allow new static/fixed-case strings after startup, but this string is just a substring
		//  of an existing pooled string already in the proper case, so it's okay to override that check here.
		{
			bool bSaved = g_disallow_static_strings;
			g_disallow_static_strings = false;
			bind->name = allocAddStaticString(bind->name);
			g_disallow_static_strings = bSaved;
		}
		//bind->name = allocAddString(buf);
	}
	if (tfh.flags & TEXOPT_JPEG) {
		bind->flags |= TEX_JPEG;
	} else if (tfh.flags & TEXOPT_CRUNCH) {
		// One of these two flags must be set if the crunch flag is set. This guards against
		// wtexs that have somehow had the crunch flag set erroneously.
		if (tfh.flags & (TEXOPT_REVERSED_MIPS | TEXOPT_NOMIP)) {
			bind->flags |= TEX_CRUNCH;
		}
		// Make sure the file header dimensions are correct for crunched half res textures.
		// Textures with the halfres truecolor compression type should not be crunched, but since folder texopts are
		// usually hand-edited an inappropriate combination of flags can be set.
		if ((tfh.flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT == COMPRESSION_HALFRES_TRUECOLOR) {
			tfh.width >>= 1;
			tfh.height >>= 1;
			tfh.width = ALIGNUP(tfh.width, 4);
			tfh.height = ALIGNUP(tfh.height, 4);
		}
	}

	if (tfh.flags & TEXOPT_REVERSED_MIPS) {
		bind->flags |= TEX_REVERSED_MIPS;
	}

	if (tfh.max_levels == 0) {
		bind->max_levels = (tfh.flags & TEXOPT_NOMIP) ? 1 : imgLevelCount(tfh.width, tfh.height, 1);
	} else {
		bind->max_levels = tfh.max_levels;
	}

	bind->base_levels = tfh.base_levels ? tfh.base_levels : bind->max_levels;

#ifdef VALIDATE_TEXTURE_HEADERS
	if ((tfh.flags & (TEXOPT_CRUNCH | TEXOPT_JPEG)) == 0) {
		size_t texelSize = imgByteCount(texTypeFromFlags(tfh.flags), tfh.rdr_format, tfh.width, tfh.height, tfh.width, bind->base_levels);
		texelSize += sizeof(DDSURFACEDESC2) + 4;

		if (texelSize != tfh.file_size) {
			struct tm timeinfo;
			time_t rawtime = fileLastChanged(filename);
			char tmbuf[256];

			localtime_s(&timeinfo, &rawtime );
			asctime_s(tmbuf, sizeof(tmbuf), &timeinfo);
			tmbuf[strlen(tmbuf) - 1] = '\0';	//remove the newline

			Errorf("Texture file '%s' (%s) has inconsistent header data.", filename, tmbuf);
		}
	}
#endif

	if (!stashFindElement(g_basicTextures_ht, bind->name, &element)) {
		stashAddPointer(g_basicTextures_ht, bind->name, bind, false);
	} else {
		// Duplicate!
		BasicTexture *old = stashElementGetPointer(element);
		if (stricmp(old->fullname, bind->fullname)!=0) {
			ErrorFilenameDup(old->fullname, bind->fullname, bind->name, "Texture");
		}
	}

	if (tfh.width>MAX_TEX_SIZE || tfh.height>MAX_TEX_SIZE)
		ErrorFilenamef(filename,"Texture Too big - Max tex size is (%dX%d) this one is (%dX%d)", MAX_TEX_SIZE,MAX_TEX_SIZE,tfh.width,tfh.height);
	if( (tfh.width<=0 || tfh.height<=0) || tfh.alpha<0 || tfh.alpha>1 )
		FatalErrorFilenamef(filename,"GETTEX running or Bad Texture\n");
	// Warn about Nvidia shader optimizer bug with small RGBA textures http://jira:8080/browse/COR-15340
	if (logNvidiaDriverBugRGBATextures && MayCauseNvidiaDriverShaderOptimizerBug(&tfh) && !strstri(filename,"/ui/"))
	{
		if (logNvidiaDriverBugRGBATextures == 1)
			filelog_printf("BadNvidiaTexture.log", "%s, %d ,%d", filename, tfh.width,tfh.height);
		else
			ErrorFilenamef(filename, "Texture may cause Nvidia driver bug in versions near 285.86 (circa 1-1-2012)! RGBA textures (or formats which the engine will convert to RGBA) smaller-than 64x64 can "
				"cause a driver shader optimizer bug, looking like flickering lighting or corrupted shadows. Convert the texture "
				"to DXT1, DXT5, or DXT5Nm, or use at least 64x64 resolution as a work-around. See JIRA http://jira:8080/browse/COR-15340.");
	}

	if(tfh.alpha) // && !(tfh.flags & TEXOPT_FADE))
		bind->flags |= TEX_ALPHA;
	bind->height = tfh.height;
	bind->width = tfh.width;
	bind->realHeight = 1 << log2(bind->height);
	bind->realWidth = 1 << log2(bind->width);
	bind->rdr_format = tfh.rdr_format;
	//bind->load_time_flags = tfh.flags;
	//bind->load_time_alpha_mip_threshold = tfh.alpha_mip_threshold;
	{
		F32 dummy = tfh.unused; // Just to cause compile error when this variable becomes used
	}

	assert(bind->height <= bind->realHeight);
	assert(bind->width <= bind->realWidth);

	texResetTexOptParametersBasic(bind);

	// Store mip header data on the bind
	if (mipdatasize) {
		texSetupMipData(bind, mipdata, mipdatasize);
	} else {
		bind->mip_type = TEXMIP_NONE;
	}
	if (bind->loaded_data)
		bind->loaded_data->mip_bound_on = 0;

	if ((old_flags ^ bind->flags) & TEX_ALPHA) {
		g_need_alpha_recalc = true;
	}

	ScratchFree(extradata);

	return 0;
}

BasicTexture *basicTextureCreate(void)
{
	BasicTexture *bind = MP_ALLOC(BasicTexture);
	return bind;
}

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
void gfxTraceTexLifetime(const char * format, ...)
{
	va_list va;
	va_start(va, format);
	OutputDebugStringv(format, va);
	va_end(va);
}

void logBasicTexUse(BasicTexture *bind, void *pCookie)
{
	memlog_printf(&tex_memlog, "%u: %d: basicTexture use %s (0x%08p) Cookie 0x%08p", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, bind->name, bind, pCookie);
}
#endif

void basicTextureDestroy(BasicTexture *bind)
{
#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
	memlog_printf(&tex_memlog, "%u: %d: basicTextureDestroy %s (0x%08p)", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, bind->name, bind);

	if (bind->queued_draw)
		memlog_printf(&tex_memlog, "texDemandLoad: %p %s queued for draw", bind, bind->name);
	assert(!bind->queued_draw);
	DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DELETE(bind);
#endif

	texLoadedDataRelease(bind);
	texRareDataForceRelease(bind);
	assert(!bind->loaded_data); // Otherwise leaking
	assert(!bind->rare); // Otherwise leaking
	MP_FREE(BasicTexture, bind);
}



static FileScanAction texLoadHeaderProcessor(char *dir, struct _finddata32_t *data, void *pUserData)
{
	BasicTexture *bind;
	static char *ext = ".wtex";
	static int ext_len = 5; // strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0) // not a .wtex file
		return FSA_EXPLORE_DIRECTORY;

	STR_COMBINE_SSS(filename, dir, "/", data->name);
	//printf("%s\n", filename);

	bind = basicTextureCreate();

	if (0==texFillInBind(filename, bind, false)) {
		eaPush(&g_basicTextures, bind);
	}

	return FSA_EXPLORE_DIRECTORY;
}

static void reloadTextureCallback(const char *relpath, int when) {
	char texname[MAX_PATH];
	char filename[MAX_PATH];
	char *s;
	BasicTexture *bind;

	if (strstr(relpath, "/_"))
		return;
	if (dirExists(relpath))
		return; // This should only happen if there's a folder with named something.wtex!
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	strcpy(filename, relpath);
	strcpy(texname, relpath);
	s = texname;
	if (strrchr(texname, '/')) {
		s = strrchr(texname, '/') + 1;
	}
	if (strrchr(s, '.')) {
		*strrchr(s, '.')=0;
	}
	bind = texFind(s, false);
	//waitForGetvrmlToFinish(); // Breaks GetTex, and not needed anymore?
	if (!bind) {
		bind = basicTextureCreate();
		if (fileExists(filename) && 0==texFillInBind(filename, bind, false)) {
			texSetBindsSubBasic(bind);
			texCheckForSwaps(bind);
			eaPush(&g_basicTextures, bind);
			// We added a new texture, reload models in case they need them!
			gfxMaterialRebuildTextures();
			if (newTextureCallback)
				newTextureCallback(bind);
			gfxStatusPrintf("Loaded new texture: %s", s);
		}
	} else {
		if (!SAFE_MEMBER(bind->loaded_data, loading) && !(SAFE_MEMBER(bind->loaded_data, tex_is_loading_for))) // If the texture is loading, don't re-load it
		{
			if (stricmp(bind->name, "white")!=0 &&
				stricmp(bind->name, "grey")!=0 &&
				stricmp(bind->name, "invisible")!=0 &&
				stricmp(bind->name, "black")!=0)// Don't free white (or a bad texture that happens to be filled in with White's data)
			{
				texLog(bind, "reload Start");
				texFree(bind, 0);
				texFree(bind, 1);
				texLog(bind, "reload after texFree");
			} else {
				bind->tex_handle = 0;
			}
			if (0==texFillInBind(filename, bind, false)) { // If this was deleted, then it became white, so we want to re-fill it with useful info
				texSetBindsSubBasic(bind);
				gfxStatusPrintf("Reloaded texture: %s", s);
			}
			texCheckForSwaps(bind);
		}
		else
			texLog(bind, "reloadTextureCallback ignored because loading");
	}
	//releaseGettexLock();
	if (g_need_alpha_recalc) {
		g_need_alpha_recalc = false;
		gfxMaterialsRecalcAlphaSort();
	}
	atlasTexIsReloaded(s);
}

/* Given a path to textures, load all .wtex files in the path
This should only be called once on init
It loads into the global "g_basicTextures"
*/
int texLoadHeaders(void)
{
	int estimatedBinds = 6000; // Estimated number of binds
	assert(g_basicTextures==NULL); // This should only be called once
	assert(g_basicTextures_ht==0);

	// Allocate an array to store all of the binds
	eaSetCapacity(&g_basicTextures, estimatedBinds);
	MP_CREATE(BasicTexture, 2048);
	MP_CREATE(BasicTextureRareData, 64);
	MP_CREATE(BasicTextureLoadedData, 128);
	MP_CREATE(TexThreadPackage, 64);

	// Read into each bind:
	tex_load_header_count = 0;
	basefolder = "texture_library";

	// Load basic textures from disk
	g_basicTextures_ht = stashTableCreateWithStringKeys(estimatedBinds, StashDefault);
	fileScanAllDataDirs(basefolder, texLoadHeaderProcessor, NULL);
	fileScanAllDataDirs("bin/images", texLoadHeaderProcessor, NULL);

	FolderCacheReleaseHogHeaderData();

	texFindNeededBinds();

	// Create default CompositeTextures for each BasicTexture
	//texCreateCompositeTextures();

	// Swap in localized versions of the textures
	texCheckSwapList();

	// Add callback for re-loading textures
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "texture_library/*.wtex", reloadTextureCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "bin/images/*.wtex", reloadTextureCallback);

	texSetBinds(); //must be done right after all headers are loaded

	if (tex_load_header_count==0)
		return 0;

	return 1;
}

/* unloads all texbinds from the graphics card */
void texFreeAll(void)
{
	int i;
	for (i=0; i<eaSize(&g_basicTextures); i++) {
		if (g_basicTextures[i] == invisible_tex)
			continue;
		texFree(g_basicTextures[i], 0);
		texFree(g_basicTextures[i], 1);
	}
}

void texFreeAllNonUI(void)
{
	int i;
	for (i=0; i<eaSize(&g_basicTextures); i++) {
		if (g_basicTextures[i] == invisible_tex)
			continue;
		if (g_basicTextures[i]->use_category & (WL_FOR_UI|WL_FOR_FONTS))
			continue;
		texFree(g_basicTextures[i], 0);
		texFree(g_basicTextures[i], 1);
	}
}


__forceinline bool texIs3D(const BasicTexture * tex)
{
	return tex && (tex->bt_texopt_flags & TEXOPT_VOLUMEMAP);
}

/* unloads all 3D texbinds from the graphics card */
void texFreeAll3D(void)
{
	int i;
	for (i=0; i<eaSize(&g_basicTextures); i++) {
		if (g_basicTextures[i] == invisible_tex || !texIs3D(g_basicTextures[i]))
			continue;
		texFree(g_basicTextures[i], 0);
		texFree(g_basicTextures[i], 1);
	}
}

typedef struct TexActiveHandleData
{
	RdrTexType tex_type;
	RdrTexFormat tex_format;
	int width;
	int height;
	int depth;
	int mip_count;
} TexActiveHandleData;
typedef struct TexHandlePool
{
	TexActiveHandleData data; // Key for this pool
	int max_pooled_handles; // If this is set, we will pool this class of TexHandles
	int member_size; // size of a texture in this pool
	U32 pool_frame_timestamp; // Timestamp of when this was last used
	// dynArray
	TexHandle *pooled_handles;
	int pooled_handles_count;
	int pooled_handles_max;
	// stats:
	int pool_returned_count;
	int pool_used_count;
	int pool_peak_count; // peak number in pool
	int create_count;
	int free_count;
	int peak_count; // peak outstanding allocations
} TexHandlePool;
static StashTable stActiveHandleData; // TexHandle -> TexHandlePool
static StashTable stHandlePoolData; // TexActiveHandleData -> TexHandlePool
static TexHandlePool **allPools;

static bool texturePoolDisable;
// Disables freed texture pooling
AUTO_CMD_INT(texturePoolDisable, texturePoolDisable) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance);
static bool texturePoolResizeDisable;
// Disables resizing textures to specific sizes to better pool
AUTO_CMD_INT(texturePoolResizeDisable, texturePoolResizeDisable) ACMD_CMDLINE ACMD_CATEGORY(Debug);

static int unused_pool_memory; // Current unused amount
static int unused_pool_count; // Current unused count
static int pool_borrows;
static int pool_relinquish_over_count;
static int pool_relinquish_over_mem;
static int pool_timed_releases;
static int pool_timed_release_size;

static F32 texturePoolUnusedTimeout=10; // time, in seconds, after which an element will be removed from an inactive pool

static int texturePoolMaxSize = 8*1024*1024;
// Maximum amount of memory consumed by the texture pool under normal circumstances (may spike to +25% more)
AUTO_CMD_INT(texturePoolMaxSize, texturePoolMaxSize) ACMD_CMDLINE ACMD_CATEGORY(Performance);

static int texturePoolMaxCount = 2048;;
// Maximum number of pooled textures to keep around
AUTO_CMD_INT(texturePoolMaxCount, texturePoolMaxCount) ACMD_CMDLINE ACMD_CATEGORY(Performance);

static void addPoolableType(RdrTexType tex_type, RdrTexFormat tex_format, int width, int height, int depth, int mip_count, int pool_size)
{
	TexHandlePool *pool;
	pool = callocStruct(TexHandlePool);
	pool->data.tex_type = tex_type;
	pool->data.tex_format = tex_format;
	pool->data.width = width;
	pool->data.height = height;
	pool->data.depth = depth;
	pool->data.mip_count = mip_count;
	pool->max_pooled_handles = pool_size;
	pool->member_size = imgByteCount(pool->data.tex_type, pool->data.tex_format, pool->data.width, pool->data.height, pool->data.depth, pool->data.mip_count);
	verify(stashAddPointer(stHandlePoolData, pool, pool, false)); // Otherwise this was added twice
	eaPush(&allPools, pool);
}

static void gfxTexturePoolInit(void)
{
	assert(!stActiveHandleData);
	stActiveHandleData = stashTableCreateInt(512);
	stHandlePoolData = stashTableCreateFixedSize(64, sizeof(TexActiveHandleData));

	// Exceptions to general rules below
	addPoolableType(RTEX_2D,	RTEX_DXT1,		512,	256,	1,	0,	16);
	addPoolableType(RTEX_2D,	RTEX_DXT1,		256,	256,	1,	0,	64);
	addPoolableType(RTEX_2D,	RTEX_DXT1,		256,	128,	1,	0,	16);

	// Low mip pool
	addPoolableType(RTEX_2D,	RTEX_BGRA_U8,	8,		8,		1,	1,	256);
}

static void gfxTexturePoolsOncePerFramePerDevice(void)
{
	static int it=0;
	TexHandlePool *pool;
	if (eaSize(&gfx_state.devices)>1 || !eaSize(&allPools))
		return; // We don't have logic for freeing these handles to multiple devices

	// Just check 1 per frame
	if (++it>=eaSize(&allPools))
		it = 0;
	pool = allPools[it];
	if (pool->pooled_handles_count)
	{
		if (gfx_state.client_frame_timestamp - pool->pool_frame_timestamp > timerCpuSpeed()*texturePoolUnusedTimeout)
		{
			// It's been unused for a certain amount of time, release a handle
			TexHandle to_free = pool->pooled_handles[--pool->pooled_handles_count];
			rdrFreeTexture(gfx_state.currentDevice->rdr_device, to_free);
			unused_pool_memory-=pool->member_size;
			unused_pool_count--;
			pool->pool_frame_timestamp = gfx_state.client_frame_timestamp;
			pool_timed_releases++;
			pool_timed_release_size+=pool->member_size;
		}
	}
}

static void gfxTexturePoolSetDefaultSize(TexHandlePool *pool)
{
	bool bSquare = pool->data.width == pool->data.height;
	int bigdim = MAX(pool->data.width, pool->data.height);

	pool->member_size = imgByteCount(pool->data.tex_type, pool->data.tex_format, pool->data.width, pool->data.height, pool->data.depth, pool->data.mip_count);

	if (pool->data.mip_count<=1)
		return; // No MIPs, do no pooling
	if (bigdim==1024)
		pool->max_pooled_handles = 8;
	else if (bigdim==512)
		pool->max_pooled_handles = 16;
	else if (bigdim==256)
		pool->max_pooled_handles = 32;
	else if (bigdim==128)
		pool->max_pooled_handles = 32;
	else if (bigdim==64)
		pool->max_pooled_handles = 96; // texReduce min
// 	else if (bigdim==8)
// 		pool->max_pooled_handles = 96; // low mips - now converted to fixed format
	else if (bigdim<64)
		pool->max_pooled_handles = 8; // Very few of these

	if (!bSquare)
		pool->max_pooled_handles = 8;
}

static void gfxTexturePoolSwapHandles(TexHandle handle1, TexHandle handle2, TexHandlePool *handle1_pool)
{
	U32 stripped1 = rdrGetTexHandleKey(&handle1);
	U32 stripped2 = rdrGetTexHandleKey(&handle2);
	TexHandlePool *pool1=NULL;
	TexHandlePool *pool2=NULL;

	// Fix freelist
	if (handle1_pool)
	{
		assert(handle1_pool->pooled_handles_count && rdrGetTexHandleKey(&handle1_pool->pooled_handles[handle1_pool->pooled_handles_count-1]) == stripped1);
		//if (handle1_pool->pooled_handles_count && rdrGetTexHandleKey(&handle1_pool->pooled_handles[handle1_pool->pooled_handles_count-1]) == stripped1)
			// handle1 is in the freelist of handle1_pool
			handle1_pool->pooled_handles[handle1_pool->pooled_handles_count-1] = stripped2;
	}

	// Fix active textures being tracked to a pool
	stashIntRemovePointer(stActiveHandleData, stripped1, &pool1);
	stashIntRemovePointer(stActiveHandleData, stripped2, &pool2);

	assert(pool1 && !pool2 || pool2 && !pool1); // Only one of these should be active, the other returned to a pool, or freed

	if (pool1)
	{
		verify(stashIntAddPointer(stActiveHandleData, stripped2, pool1, false));
	}

	if (pool2)
	{
		verify(stashIntAddPointer(stActiveHandleData, stripped1, pool2, false));
	}
}

// Returns true if we reclaimed the texture handle
static bool gfxTexturePoolRelinquishHandle(TexHandle *tex_handle, TexHandlePool **pool_relinquished_to)
{
	TexHandlePool *pool;
	U32 stripped = rdrGetTexHandleKey(tex_handle);
	bool bRet = false;
	if (eaSize(&gfx_state.devices)>1)
		return false;  // Can't clear handle pointer from the caller because another device might still be about to bind it - no pooling on multiple devices
	if (stashIntFindPointer(stActiveHandleData, stripped, &pool))
	{
		// This is a handle we know about
		// Record the free
		pool->free_count++;

		// If we are not reclaiming the handle, remove it from our tracking
		stashIntRemovePointer(stActiveHandleData, stripped, &pool);

		if (pool->pooled_handles_count < pool->max_pooled_handles && !texturePoolDisable)
		{
			if (unused_pool_count < texturePoolMaxCount)
			{
				int newsize = unused_pool_memory + pool->member_size;
				if (newsize < texturePoolMaxSize * 1.25)
				{
					// Keep it, for pooling or freeing later
					TexHandle *store;
					bRet = true;
					store = dynArrayAddStruct(pool->pooled_handles, pool->pooled_handles_count, pool->pooled_handles_max);
					*store = rdrGetTexHandleKey(tex_handle);
					*tex_handle = 0;
					pool->pool_returned_count++;
					MAX1(pool->pool_peak_count, pool->pooled_handles_count);
					unused_pool_memory += pool->member_size;
					pool->pool_frame_timestamp = gfx_state.client_frame_timestamp;
					unused_pool_count++;
					if (pool_relinquished_to)
						*pool_relinquished_to = pool;
				} else {
					pool_relinquish_over_mem++;
				}
			} else {
				pool_relinquish_over_count++;
			}
		}
	}
	return bRet;
}

static void gfxTexturePoolGetTexHandle(TexHandle *tex_handle, RdrTexType tex_type, RdrTexFormat tex_format, int width, int height, int depth, int mip_count)
{
	TexHandle tex_handle_orig = *tex_handle;
	TexActiveHandleData data;
	TexHandlePool *pool, *pool_to_get_handle_from=NULL, *handle1_pool=NULL;
	U32 stripped;
	RdrTexFlags tex_flags = rdrGetTexHandleFlags(tex_handle);
	bool bUsePooled=false;
	static bool bInited=false;
	if (!bInited)
	{
		bInited = true;
		gfxTexturePoolInit();
	}

	if (tex_type == RTEX_3D || tex_type == RTEX_CUBEMAP)
		return; // Not bothering with pooling 3D textures
	assert(depth == 1);

	data.tex_type = tex_type;
	data.tex_format = tex_format;
	data.width = width;
	data.height = height;
	data.depth = depth;
	data.mip_count = mip_count;

	if (!stashFindPointer(stHandlePoolData, &data, &pool))
	{
		pool = callocStruct(TexHandlePool);
		pool->data = data;
		gfxTexturePoolSetDefaultSize(pool);
		verify(stashAddPointer(stHandlePoolData, pool, pool, false));
		eaPush(&allPools, pool);
	}

	// If we can, allocate something from this pool, otherwise let it allocate itself and start tracking it

	pool->create_count++;
	MAX1(pool->peak_count, pool->create_count-pool->free_count);

	if (!texturePoolDisable)
	{
		if (pool->pooled_handles_count)
		{
			bUsePooled = true;
			pool_to_get_handle_from = pool;
		} else if (unused_pool_memory > texturePoolMaxSize) {
			// Nothing available in our pool, but we're over the desired limit, grab another handle of the same size and reuse that
			TexHandlePool *best=NULL;
			int bestcount = 0;
			FOR_EACH_IN_EARRAY(allPools, TexHandlePool, newpool)
			{
				if (newpool->member_size != pool->member_size)
					continue; // Not the same size
				if (newpool->pooled_handles_count > bestcount)
					best = newpool;
			}
			FOR_EACH_END;
			if (best)
			{
				assert(best->pooled_handles_count);
				pool_to_get_handle_from = best;
				bUsePooled = true;
			}
		}
	}

	// If we're already tracking this handle, record the free, and if we're using a new pooled handle, relinquish the old handle back to the old pool
	if (gfxTexturePoolRelinquishHandle(tex_handle, &handle1_pool))
	{
		// Returned to pool, need a new handle
		if (bUsePooled) {
			// We're getting one below
		} else {
			*tex_handle = rdrGenTexHandle(tex_flags);
		}
	} else {
		// Did not relinquish it to a pool
		if (bUsePooled && eaSize(&gfx_state.devices) == 1)
		{
			// Just one device, free to it
			rdrFreeTexture(gfx_state.currentDevice->rdr_device, *tex_handle);
			// Get a new handle below
		} else {
			// Reusing the existing handle
			bUsePooled = false;
		}
	}
	if (bUsePooled)
	{
		TexHandle tex_handle_to_use = pool_to_get_handle_from->pooled_handles[0]; // Take from front and shift to reduce texture reuse on adjacent frames
		--pool_to_get_handle_from->pooled_handles_count;
		unused_pool_memory -= pool_to_get_handle_from->member_size;
		unused_pool_count--;
		memcpy(pool_to_get_handle_from->pooled_handles, &pool_to_get_handle_from->pooled_handles[1], sizeof(pool_to_get_handle_from->pooled_handles[0])*pool_to_get_handle_from->pooled_handles_count);
		rdrChangeTexHandleFlags(&tex_handle_to_use, tex_flags);
		*tex_handle = tex_handle_to_use;
		pool->pool_used_count++;
		pool->pool_frame_timestamp = gfx_state.client_frame_timestamp;
		if (pool != pool_to_get_handle_from)
		{
			pool_borrows++;
			pool_to_get_handle_from->pool_frame_timestamp = gfx_state.client_frame_timestamp;
		}
	}
	stripped = rdrGetTexHandleKey(tex_handle);
	stashIntAddPointer(stActiveHandleData, stripped, &pool->data, true);

	if (tex_handle_orig != *tex_handle)
	{
		// We are returning a different tex handle, but the caller might already have sent this one off to the renderer
		// Switch all meaning between the old (now freed or pooled) tex handle and the new one.
		// Swap on renderer side
		rdrSwapTexHandles(gfx_state.currentDevice->rdr_device, tex_handle_orig, *tex_handle);
		// Swap in pooled cached data side
		gfxTexturePoolSwapHandles(tex_handle_orig, *tex_handle, handle1_pool);

		// Return orig
		*tex_handle = tex_handle_orig;
	}
}

int cmpTexHandlePool(const void **a, const void **b)
{
	const TexHandlePool *p0 = *(const TexHandlePool**)a;
	const TexHandlePool *p1 = *(const TexHandlePool**)b;
	int ret;
	ret = p0->pooled_handles_count*p0->member_size - p1->pooled_handles_count*p1->member_size;
	if (!ret)
		ret = p0->create_count - p1->create_count;
	if (!ret)
		ret = p1->free_count - p0->free_count;
	return ret;
}

// Displays information about the texture pool and texture traffic
AUTO_COMMAND ACMD_CATEGORY(Debug);
void gfxTexturePoolPrintInfo(void)
{
	TexHandlePool **pools=NULL;
	int current_pool_size=0;

	eaCopy(&pools, &allPools);
	eaQSort(pools, cmpTexHandlePool);

	FOR_EACH_IN_EARRAY(pools, TexHandlePool, pool)
	{
		printf("  %4dx%-4d %5s %15s  create:%4d   free:%4d   peak:%4d", 
			pool->data.width, pool->data.height,
			(pool->data.mip_count<=1)?"NoMIP":"",
			rdrTexFormatName(MakeRdrTexFormatObj(pool->data.tex_format)),
			pool->create_count, pool->free_count, pool->peak_count);
		if (pool->max_pooled_handles)
		{
			printf("   pool_size:%4d   returned:%4d   used:%4d   peak_pooled:%4d", pool->max_pooled_handles,
				pool->pool_returned_count, pool->pool_used_count, pool->pool_peak_count);
			current_pool_size += imgByteCount(pool->data.tex_type, pool->data.tex_format, pool->data.width, pool->data.height, pool->data.depth, pool->data.mip_count) *
				pool->pooled_handles_count;
			if (pool->pooled_handles_count)
			{
				printf("   pool_count:%4d (%9s)\n", pool->pooled_handles_count, friendlyBytes(pool->pooled_handles_count*pool->member_size));
			} else {
				printf("\n");
			}

		} else {
			printf("\n");
		}

	}
	FOR_EACH_END;

	assert(current_pool_size == unused_pool_memory);
	printf("Unused pool size: %s  (%d textures)\n", friendlyBytes(current_pool_size), unused_pool_count);
	printf("Pool returns failed because over memory: %d\n", pool_relinquish_over_mem);
	printf("Pool returns failed because over count: %d\n", pool_relinquish_over_count);
	printf("Pool borrows: %d\n", pool_borrows);
	printf("Pool timed releases: %d (%s)\n", pool_timed_releases, friendlyBytes(pool_timed_release_size));

	eaDestroy(&pools);
}

// Frees something from the current renderer
// Unflags tex_is_loaded
static void texFreeInternal( BasicTexture *bind, bool only_free_low_mip )
{
	BasicTextureRareData *rare_data;
	if (!only_free_low_mip)
	{
		memlog_printf(&tex_memlog, "%u: %d: texFree %s (0x%08p)", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, bind->name, bind);
		if ( (rare_data = texGetRareData(bind)) )
			rare_data->hasBeenComposited = 0;
		bind->tex_is_loaded &= ~(gfx_state.currentRendererFlag);
		texLog(bind, "texFreeInternal remove tex");
		if (!(bind->has_rare && texGetRareData(bind)->dont_free_handle))
		{
			if (!gfxTexturePoolRelinquishHandle(&bind->tex_handle, NULL))
			{
				rdrFreeTexture(gfx_state.currentDevice->rdr_device, bind->tex_handle);
				texLog(bind, "texFreeInternal called rdrFreeTexture on entire texture");
			}
		}
	}
	else
	{
		//memlog_printf(&tex_memlog, "%u: %d: texFreeLowMip %s (0x%08p)", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, bind->name, bind);
		bind->loaded_data->mip_bound_on &= ~(gfx_state.currentRendererFlag);
		texLog(bind, "texFreeInternal removes low mip");
		//if (!bind->dont_free_handle)
		if (!gfxTexturePoolRelinquishHandle(&bind->loaded_data->mip_handle, NULL))
		{
			rdrFreeTexture(gfx_state.currentDevice->rdr_device, bind->loaded_data->mip_handle);
			texLog(bind, "texFreeInternal called rdrFreeTexture on low mip");
		}
	}
}

static BasicTexture **queuedTexFrees;
static BasicTexture **queuedTexLowMipFrees;
static void texDoQueuedFrees()
{
	int num_frees_this_frame=0;
	int i;
	for (i=eaSize(&queuedTexFrees)-1; i>=0; i--) {
		BasicTexture *bind = queuedTexFrees[i];
		bind->tex_is_loaded &= (gfx_state.allRenderersFlag | RAW_DATA_BITMASK);
		texRecordNewMemUsage(bind, TEX_MEM_VIDEO, 0);
		texRecordNewMemUsage(bind, TEX_MEM_LOADING, 0);
		if (bind->tex_is_loaded & gfx_state.currentRendererFlag) {
			// Loaded on this device, wants to be freed
			texFreeInternal(bind, false);
		}
		if ((bind->tex_is_loaded & NOT_RAW_DATA_BITMASK) == 0 || !bind->tex_handle) // tex_handle goes to 0 if it was returned to the texture pool, leave active on all cards
		{
			num_frees_this_frame++;
			// Not loaded on anything, remove from list
			// rdrRelinquishTexHandle(bind->tex_handle); bind->tex_handle = NULL;
			if (bind->loaded_data)
			{
				bind->loaded_data->tex_is_unloading = 0;
				texLog(bind, "texDoQueuedFrees");
			}
			eaRemoveFast(&queuedTexFrees, i);
			DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DELETE(bind);
			if (bind->flags & TEX_TEXGEN) {
				// Freeing a TexGen needs the container freed too
				texGenFinalizeFree(bind);
			} else if (bind->flags & TEX_DYNAMIC_TEXWORD) {
				if (texGetDynamicReferenceCount(bind) <= 0) {
					destroyTexWordParams(texGetTexWordParams(bind));
					basicTextureDestroy(bind);
				}
			} else if (bind->flags & TEX_SCRATCH) {
				if (texGetDynamicReferenceCount(bind) <= 0) {
					basicTextureDestroy(bind);
				}
			} else if (bind->name[0] == '#') {
				// Dynamically registered
				if (texGetDynamicReferenceCount(bind) <= 0)
				{
					assert(bind->name != allocFindString(bind->name));
					SAFE_FREE(*(char**)&bind->name);
					basicTextureDestroy(bind);
				}
			}
		}
	}
	for (i=eaSize(&queuedTexLowMipFrees)-1; i>=0; i--) {
		BasicTexture *bind = queuedTexLowMipFrees[i];
		if (!bind->loaded_data)
		{
			// No knowledge of currently being loaded, perhaps was a reload on a deleted texture
			eaRemoveFast(&queuedTexLowMipFrees, i);
			DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DELETE(bind);
		}
		else if (bind->loaded_data->mip_loading)
		{
			// Something else (another device) requested it to be loading, don't get rid of it
			// JE: I'm not completely sure this can happen, but this might fix a crash in the MaterialEditor
			eaRemoveFast(&queuedTexLowMipFrees, i);
			DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DELETE(bind);
		} else {
			bind->loaded_data->mip_bound_on &= (gfx_state.allRenderersFlag | RAW_DATA_BITMASK);
			if (bind->loaded_data->mip_bound_on & gfx_state.currentRendererFlag) {
				// Loaded on this device, wants to be freed
				texFreeInternal(bind, true);
			}
			if (!bind->loaded_data->mip_bound_on) 
				eaRemoveFast(&queuedTexLowMipFrees, i);
				DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DELETE(bind);
		}
	}
}

static __forceinline bool texNotLoadedInRenderer(const BasicTexture *bind)
{
	return !(bind->tex_is_loaded & NOT_RAW_DATA_BITMASK);
}

void texFree( BasicTexture *bind, int freeRawData )
{
	U32 flag;

	if (freeRawData) {
		flag = RAW_DATA_BITMASK;
	} else {
		flag = NOT_RAW_DATA_BITMASK;
	}

	if (!bind->loaded_data)
		return; // Not loaded, can happen in some async calls perhaps

	if (bind->loaded_data->tex_is_loading_for)
	{
		// Warning, may leak memory
		memlog_printf(&tex_memlog, "%u: texFree() %s (0x%08p) SKIPPED - still loading", gfx_state.client_frame_timestamp, bind->name, bind);
		return;
	}

	if ((bind->tex_is_loaded & flag)) {
		//printf("freeing tex: %s\n", bind->name);

		if (freeRawData) {
			if (bind!=white_tex && texGetRawReferenceCount(bind)==0) {
				BasicTextureRareData *rare_data = texGetRareData(bind);
				TexReadInfo *rawInfo = SAFE_MEMBER(rare_data, bt_rawInfo);
				memlog_printf(&tex_memlog, "%u: texFree RAW %s (0x%08p)", gfx_state.client_frame_timestamp, bind->name, bind);
				// Free raw data
				if (rawInfo)
				{
					if (rawInfo->texture_data)
						texReadInfoMemRefDecrement(rawInfo, bind);
					free(rawInfo);
					rare_data->bt_rawInfo = NULL;
				}
				// check for freeing rare_data here
				rare_data = NULL;
				texRareDataOptionallyRelease(bind);
				bind->tex_is_loaded &= ~(flag);

				texRecordNewMemUsage(bind, TEX_MEM_RAW, 0);
				if (texNotLoadedInRenderer(bind))
				{
					texRecordNewMemUsage(bind, TEX_MEM_VIDEO, 0);
					texRecordNewMemUsage(bind, TEX_MEM_LOADING, 0);
				}
				texLoadedDataOptionallyRelease(bind);
			}
		} else {
			// Free GL data
			if (bind->tex_is_loaded && bind->tex_handle && !bind->loaded_data->tex_is_unloading)
			{
				if (bind->flags & (TEX_TEXGEN | TEX_SCRATCH))
				{
					gfxMaterialsAssertTexNotInDrawList(bind);
					if (gfx_state.currentDevice)
						gfxMaterialsAssertTexNotInSpriteList(gfx_state.currentDevice->sprite_list, bind);
				}
				bind->loaded_data->tex_is_unloading = 1;
				texLog(bind, "texFree set unloading");
				memlog_printf(&tex_memlog, "%u: texFree(queued) %s (0x%08p)", gfx_state.client_frame_timestamp, bind->name, bind);

				DEBUG_MARK_TEXTURE_QUEUED_FOR_DELETE(bind);

				eaPush(&queuedTexFrees, bind);
			}
		}
	} else {
		// No graphics data to be unloaded, but may need to free the texture itself!
		if (!freeRawData && !bind->loaded_data->tex_is_unloading)
		{
			if (bind->flags & (TEX_DYNAMIC_TEXWORD | TEX_TEXGEN | TEX_SCRATCH) ||
				bind->name[0]=='#')
			{
				gfxMaterialsAssertTexNotInDrawList(bind);
				if (gfx_state.currentDevice)
					gfxMaterialsAssertTexNotInSpriteList(gfx_state.currentDevice->sprite_list, bind);
				memlog_printf(&tex_memlog, "%u: texFree(queued) %s (0x%08p) - no data loaded", gfx_state.client_frame_timestamp, bind->name, bind);
				bind->loaded_data->tex_is_unloading = 1;
				texLog(bind, "texFree set unloading because not loaded and need to check for freeing");

				DEBUG_MARK_TEXTURE_QUEUED_FOR_DELETE(bind);

				eaPush(&queuedTexFrees, bind);
			}
		}
	}
}

bool texIsFreed(const BasicTexture *bind)
{
	return bind->name == (const char*)MEMORYPOOL_SENTINEL_VALUE;
}

bool texUnloadDynamic( BasicTexture *basicBind )
{
	if (basicBind && texGetTexWordParams(basicBind)) {
		// This is in fact a dynamic texture
		texGetRareData(basicBind)->dynamicReferenceCount--;
		if (texGetDynamicReferenceCount(basicBind) == 0) {
			if (SAFE_MEMBER(basicBind->loaded_data, tex_is_loading_for) & ~(RAW_DATA_BITMASK)) {
				// Wait for it to finish loading?  Kill it when it's done?
				// TODO: May currently leak
			} else {
				// Unloaded or fully loaded
				texFree(basicBind, 0);
				eaFindAndRemoveFast(&g_basicTextures, basicBind);
				return true;
			}
		}
	}
	return false;
}

BasicTexture *g_selected_texture;
void gfxTextureSelect(BasicTexture *tex)
{
	g_selected_texture = tex;
}

AUTO_RUN;
void RegisterTextureDict(void)
{
	resRegisterIndexOnlyDictionary("Texture", RESCATEGORY_ART);
}

// Returns ptr to the Basic TexBind with the given name or zero. 
BasicTexture *texFindAndFlag(const char *name, int isRequired, WLUsageFlags use_category)
{
	char search[MAX_PATH];
	BasicTexture *match = 0;

	assert(PTR_TO_U32(name)!=0xfafafafa);

	if (!name)
		return NULL;

	PERFINFO_AUTO_START_FUNC_L2();

	assert(*(int*)name!=0xfafafafa);

	texFixName(name, search, ARRAY_SIZE(search));
	if (!stashFindPointer(g_basicTextures_ht, search, &match)) {
		if (name[0] == '\\')
		{
			char *s = strdup(name);
			char *last_s = &s[1];
			U32 i, length;
			TexWordParams *params = createTexWordParams();
			BasicTexture *texture;
			const char *layoutName=NULL;
			length = (U32)strlen(s);
			for (i = 1; i < length; i++)
				if (s[i] == '\\')
				{
					s[i] = 0;
					if (last_s == &s[1])
						layoutName = last_s;
					else
						eaPush(&params->parameters, last_s);
					if (i < length-1)
						last_s = &s[i+1];
					else
						last_s = NULL;
				}
			if (last_s) eaPush(&params->parameters, last_s);
			texture = texFindDynamic(layoutName, params, WL_FOR_UTIL, "noname.txt");
			stashAddPointer(g_basicTextures_ht, allocAddString(search), texture, false);
			texture->use_category |= use_category;
			PERFINFO_AUTO_STOP_L2();
			return texture;
		}
		if (isRequired && !quickload && isDevelopmentMode() && !gbNoGraphics) {
			static StashTable stWarnedTextures;
			if (!stWarnedTextures) {
				stWarnedTextures = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
			}
			if (!stashFindInt(stWarnedTextures, name, NULL)) {
				printfColor(COLOR_RED|COLOR_BRIGHT, "Warning: Missing texture: '%s'\n", name);
				stashAddInt(stWarnedTextures, name, 1, true);
			}
		}
		PERFINFO_AUTO_STOP_L2();
		return NULL;
	}
	match->use_category |= use_category;
	PERFINFO_AUTO_STOP_L2();
	return match;
}

static void texRefCountHogForFile(const char *texturepath, bool inc)
{
	char hogpath[MAX_PATH];
	char *s;
	HogFile *handle;
	assert(texturepath[0] == '#');
	strcpy(hogpath, texturepath+1);
	s = strchr(hogpath, '#');
	assert(s);
	*s = '\0';
	if (!inc)
		assert(hogFileIsOpenInMyProcess(hogpath));

	// increases the reference count
	handle = hogFileRead(hogpath, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE);
	assertmsgf(handle, "texRegisterDynamic called on a file in a dynamic hogg which does not exist: %s",texturepath);

	// if we and trying to decrease, we need to decrease it twice!
	if (!inc)
	{
		hogFileDestroy(handle, true);
		hogFileDestroy(handle, true);
	}
}

// Registers a dynamically accessed texture into the global dictionary
// Path can be something like "#file.hogg#file.ext"
BasicTexture *texRegisterDynamic(const char *texturepath)
{
	BasicTexture *bind=NULL;

	assertmsg(texturepath && texturepath[0] == '#', "Registered dynamic textures must currently start with a # and be an explicit inner-hogg path");

	if (stashFindPointer(g_basicTextures_ht, texturepath, &bind))
	{
		texAllocRareData(bind)->dynamicReferenceCount++;
		return bind;
	}

	// open and refcount a hogg handle
	texRefCountHogForFile(texturepath, 1);

	// Allocate a new BasicTexture
	bind = basicTextureCreate();

	// Fill it in
	bind->name = strdup(texturepath);
	bind->actualTexture = bind;
	bind->fullname = "dynamicTexture";
	// Assign from TexOpts, etc (overwritten if we find the base texture)
	texResetTexOptParametersBasic(bind);
	texSetBindsSubBasic(bind);
	// Don't yet know the width/height
	bind->width = 0;
	bind->height = 0;
	bind->realWidth = 0;
	bind->realHeight = 0;
	bind->rdr_format = 0;

	// Adds to hashtable
	texFillInBind(bind->name, bind, true);

	texAllocRareData(bind)->dynamicReferenceCount = 1;

	// Store in global lists
	eaPush(&g_basicTextures, bind);
	return bind;

}

// Registers a dynamically accessed texture into the global dictionary
// Path can be something like "#file.hogg#file.ext"
AUTO_COMMAND ACMD_CATEGORY(Debug);
void texRegisterDynamicCmd(const char *texturepath)
{
	texRegisterDynamic(texturepath);
}

// Removes a dynamically accessed texture from the global dictionary and frees
//  all related data (BasicTexture*s go bad)
AUTO_COMMAND ACMD_CATEGORY(Debug);
void texUnregisterDynamic(const char *texturepath)
{
	bool ret;
	BasicTexture *bind = texFind(texturepath, false);
	assertmsg(bind, "Trying to unregister a texture which was never registered");
	if (!bind)
		return;

	assert(texGetDynamicReferenceCount(bind)>0);
	texGetRareData(bind)->dynamicReferenceCount--;
	if (texGetDynamicReferenceCount(bind)==0)
	{
		texRefCountHogForFile(texturepath, 0);

		// Dynamically registered, remove from dictionaries
		ret = stashRemovePointer(g_basicTextures_ht, bind->name, NULL);
		assert(ret);
		eaFindAndRemoveFast(&g_basicTextures, bind);
		atlasPurge(bind->name);

		texFree(bind, 1);
		texFree(bind, 0);
	}
}

const char *texFindDirName(char *buf, size_t buf_size, const BasicTexture *texbind)
{
	strcpy_s(SAFESTR2(buf), texbind->fullname);
	if (strchr(buf, '/'))
		return getDirectoryName(buf);
	return buf; // No /, could be special key like "dynamicTexture"
}

const char *texFindFullPath(const BasicTexture *texbind)
{
	return texbind->fullname;
}

const char *texFindName(const BasicTexture *texbind)
{
	return texbind->name;
}

const TexWord *texFindTexWord(const BasicTexture *texbind)
{
	return texGetTexWord((BasicTexture*)texbind);
}

U32 texWidth(const BasicTexture *texbind)
{
	return texbind->width;
}

U32 texHeight(const BasicTexture *texbind)
{
	return texbind->height;
}


BasicTexture *texFindRandom(void)
{
	BasicTexture *match = 0;
	StashTableIterator iter;
	StashElement elem;
	int i = 0, idx = getCappedRandom(stashGetCount(g_basicTextures_ht));
	stashGetIterator(g_basicTextures_ht, &iter);
	do
	{
		stashGetNextElement(&iter, &elem);
		match = stashElementGetPointer(elem);
	} while (++i < idx);
	return match;
}


void texAddToProcessingList(TexThreadPackage *pkg)
{
	EnterCriticalSection(&CriticalSectionTexLoadQueues);
	listAddForeignMember(&basicTexturesReadyForFinalProcessing, pkg);
	LeaveCriticalSection(&CriticalSectionTexLoadQueues);
}

void texAddToProcessingListFromBind(BasicTexture *bind)
{
	// Called in multiple threads
	TexThreadPackage *texPkg = calloc(sizeof(*texPkg), 1);
	texPkg->bind = bind;
	texPkg->fromHeap = 1;
	texAddToProcessingList(texPkg);
}

/*Callback that the background loader is given to do
this is called from any thread
if we are queuing, it adds the package to the list
if we are loading on demand (or loading the queued files), we load immediately
*/
static void texDoThreadedTextureLoading(const char *filename, void *dwParam)
{
	TexThreadPackage * pkg;
	bool loaded;

	PERFINFO_AUTO_START_FUNC();

	pkg = dwParam;

	//debug printf("texDoThreadedTextureLoading(%s)\n", pkg->bind.dirname);

	// Either a plain texture or a texWord that needs it's GL data loaded
	PERFINFO_AUTO_START("texLoadData", 1);
	loaded = texLoadData(filename, pkg);
	PERFINFO_AUTO_STOP();

	if (!loaded)
		pkg->needsFree = 1;

	// we need to do this always, so we don't leak.
    texAddToProcessingList(pkg);

	InterlockedDecrement(&numTexLoadsInThread);

	PERFINFO_AUTO_STOP();
}


long texLoadsPending(int include_misc)
{
	int accum = 0;
	accum += numTexLoadsInThread;
	accum += !!queuedTexLoadPkgs;

	if( include_misc ) {
		accum += texWordGetLoadsPending();
	}

	EnterCriticalSection(&CriticalSectionTexLoadQueues);
	accum += !!basicTexturesReadyForFinalProcessing;
	LeaveCriticalSection(&CriticalSectionTexLoadQueues);

	return accum;
}

const char *texMemMonitorNameFromFlags(WLUsageFlags use_flags)
{
	if (use_flags & WL_FOR_WORLD)
		return "Textures:World";
	if (use_flags & WL_FOR_ENTITY)
		return "Textures:Character";
	if (use_flags & WL_FOR_TERRAIN)
		return "Textures:Terrain";
	if (use_flags & WL_FOR_FX)
		return "Textures:FX";
	if (use_flags & (WL_FOR_UI | WL_FOR_PREVIEW_INTERNAL))
		return "Textures:UI";
	if (use_flags & WL_FOR_UTIL)
		return "Textures:Misc";
	if (use_flags & WL_FOR_FONTS)
		return "Textures:Fonts";
	//devassertmsg(0, "Texture without known usage flags");
	return "Textures:Misc";
}

static void texUpdateTexFromBind(BasicTexture *tex_bind, TexHandle *tex_handle, TexReadInfo *info, U32 levels_needed, bool fromhtex)
{
	RdrTexParams *rtex;
	TexOptFlags texopt_flags = tex_bind->bt_texopt_flags;
	U32 image_byte_count;
	RdrTexType tex_type = texTypeFromFlags(texopt_flags);
	RdrTexFlags sampler_flags = 0;
	U32 width, height, depth;

	assert((*tex_handle) != 0);

	if (info->is_low_mip) {
		levels_needed = 1;
	} else if (!levels_needed) {
		levels_needed = info->level_count;
	}

	if (tex_bind->flags & TEX_JPEG)
		assert(info->tex_format == RTEX_BGR_U8);

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

	rdrChangeTexHandleFlags(tex_handle, sampler_flags);

	if (!info->is_low_mip && tex_bind->max_levels > 1) {
		const TexBlockInfo *blockInfo = imgBlockInfo(info->tex_format);
		int levels_skipped = tex_bind->max_levels - levels_needed;

		width = MAX(1, tex_bind->width >> levels_skipped);
		width = ALIGNUP(width, blockInfo->width);
		height = MAX(1, tex_bind->height >> levels_skipped);
		height = ALIGNUP(height, blockInfo->height);

		if (tex_type == RTEX_3D) {
			depth = MAX(1, width >> levels_skipped);
		} else {
			depth = info->depth;
		}
	} else {
		width = info->width;
		height = info->height;
		depth = info->depth;
	}

	if (!fromhtex) {
		// only release the handle back to the pool if we're re-initializing the texture, not updating it
        gfxTexturePoolGetTexHandle(tex_handle, tex_type, info->tex_format, width, height, depth, levels_needed);
	}

	if (info->ringbuffer_data) {
		memlog_printf(rdrGetTextureMemLog(), "%s%s: Queue for renderer %p", tex_bind ? tex_bind->name : "'No BT'", fromhtex ? " (htex)" : "", info->texture_data);
	}

	rtex = rdrStartUpdateTextureEx(gfx_state.currentDevice->rdr_device, NULL, *tex_handle, tex_type, info->tex_format, width, height, depth, levels_needed, &image_byte_count, texMemMonitorNameFromFlags(tex_bind->use_category), info->texture_data, info->ringbuffer_data);
	if (info->texture_data && !info->ringbuffer_data)
	{
		int dataRefCount = 0;
		memrefIncrement(info->texture_data);
		dataRefCount = memrefDecrement(info->texture_data);

		memlog_printf(rdrGetTextureMemLog(), "texUpdateTexFromBind data memref count (%p) = %d, BT = %p '%s' tex_handle = %"FORM_LL"u RTex = %p", 
			info->texture_data, dataRefCount, tex_bind, tex_bind ? tex_bind->name : "'No BT'", *tex_handle, rtex);
	}

	if (info->is_low_mip) {
		rtex->first_level = 0;
		rtex->level_count = 1;
	} else if (fromhtex) {
		rtex->first_level = 0;
		rtex->level_count = levels_needed - tex_bind->base_levels;
	} else {
		rtex->level_count = MIN(levels_needed, tex_bind->base_levels);
		rtex->first_level = levels_needed - rtex->level_count;
	}

	rtex->debug_texture_backpointer = tex_bind;
	rtex->anisotropy = MAX(1,gfx_state.settings.texAniso);
	if (texopt_flags & (TEXOPT_MAGFILTER_POINT|TEXOPT_NO_ANISO))
		rtex->anisotropy = 1;
	//fonts always need anisotropic filtering and don't really benefit from high settings
	if (tex_bind->use_category & WL_FOR_FONTS)
	{
		if (texopt_flags & TEXOPT_MAGFILTER_POINT)
		{
			ErrorFilenamef(tex_bind->fullname, "Font textures %s has point magnification specified, which is incompatible with anisotropy, probably remove the TexOpt from the font or update code to deal with this.", tex_bind->name);
		} else {
			rtex->anisotropy = 2;
		}
	}

	if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
		rtex->is_srgb = (texopt_flags & TEXOPT_SRGB) != 0;
	rtex->reversed_mips = (tex_bind->flags & (TEX_REVERSED_MIPS | TEX_CRUNCH)) == TEX_REVERSED_MIPS;

	rdrEndUpdateTexture(gfx_state.currentDevice->rdr_device);

	if (!info->is_low_mip) {
        if (!fromhtex) {
            texRecordNewMemUsage(tex_bind, TEX_MEM_VIDEO, image_byte_count);
            texRecordNewMemUsage(tex_bind, TEX_MEM_LOADING, tex_bind->loaded_data->tex_memory_use[TEX_MEM_LOADING] - image_byte_count);
        } else {
            // Don't change video count since we allocate all of the video memory when the base wtex is loaded.
            texRecordNewMemUsage(tex_bind, TEX_MEM_LOADING, 0);
        }
	}
}

static void texSendToRenderer(BasicTexture *tex_bind, TexReadInfo * info, U32 levels_needed, bool fromhtex)
{
	static DWORD threadid = 0;

	if (!threadid)
		threadid = GetCurrentThreadId();
	if (threadid != GetCurrentThreadId()) {
		assert(!"texLoad thinks it's in the wrong thread!");
		return;
	}
	if (info->is_low_mip) {
		assert(tex_bind->loaded_data->mip_handle);
		texUpdateTexFromBind(tex_bind, &tex_bind->loaded_data->mip_handle, info, levels_needed, fromhtex);
		tex_bind->loaded_data->mip_bound_on |= gfx_state.currentRendererFlag;
	} else {
		if (!tex_bind->tex_handle)
			tex_bind->tex_handle = rdrGenTexHandle(0);
		if (texGetTexWord(tex_bind)) {
			texWordSendToRenderer(texGetTexWord(tex_bind), tex_bind);
		} else {
			// Normal texture
			texUpdateTexFromBind(tex_bind, &tex_bind->tex_handle, info, levels_needed, fromhtex);
		}
		texLog(tex_bind, "texSendToRenderer");
		tex_bind->loaded_data->tex_is_loading_for &= ~gfx_state.currentRendererFlag;
		tex_bind->tex_is_loaded |= gfx_state.currentRendererFlag;
		assert(!(tex_bind->flags & TEX_TEXGEN));
	}
}

static int texture_per_frame_quota=10;
// Limits how many textures can be sent to each renderer each frame.  -1 is unlimited, 0 will load no textures
AUTO_CMD_INT(texture_per_frame_quota, texture_per_frame_quota) ACMD_CMDLINE ACMD_CATEGORY(Performance) ACMD_ACCESSLEVEL(0);

int texRenderLoadsPending()
{
	int count = 0;
	TexThreadPackage *pkg;

	for (pkg = queuedTexLoadPkgs; pkg; pkg = pkg->next) {
		++count;
	}

	return count;
}

int texLoadsQuota()
{
	return texture_per_frame_quota;
}

int texSentThisFrame()
{
	return texsSentToRenderer;
}

static void queueTexForRenderer(TexThreadPackage *pkg)
{
	listAddForeignMember(&queuedTexLoadPkgs, pkg);
}

static void dequeueTexForRenderer(TexThreadPackage *pkg)
{
	listRemoveMember(pkg, &queuedTexLoadPkgs);
}

static void queueHtexLoad(BasicTexture *bind, int needRawData, U32 levelsNeeded)
{
	TexThreadPackage *pkg;
	char highFn[MAX_PATH];
	const char *filename;

	if (bind->name[0]=='#') {
		// dynamic texture in a hogg or something, just use the name
		filename = bind->name;
	} else {
		filename = bind->fullname;
	}

	pkg = MP_ALLOC(TexThreadPackage);
	pkg->bind = bind;
	pkg->needRawData = needRawData;
	pkg->levelsNeeded = levelsNeeded;
	pkg->fromHtex = 1;

	changeFileExt(filename, ".htex", highFn);

	InterlockedIncrement(&numTexLoadsInThread);
	fileLoaderRequestAsyncExec(allocAddFilename(highFn), FILE_LOWEST_PRIORITY, false, texDoThreadedTextureLoading, pkg);
}

static void texDoQueuedLoads(void)
{
	int quota = texture_per_frame_quota;
	TexThreadPackage *pkg, *next;

	texsSentToRenderer=0;

	if (gfxLoadingIsWaiting() || gfx_state.inEditor)
		quota = -1; // Allow unlimited loads while in a loading state
	texCheckThreadLoader();
	for (pkg = queuedTexLoadPkgs; pkg; pkg = next)
	{
		bool bRemoveIt=false;
		next = pkg->next;

		if (quota >= 0 && rdr_state.texLoadPoolSize == 0 && texsSentToRenderer >= quota &&
			!(pkg->bind->use_category & (WL_FOR_UI|WL_FOR_UTIL|WL_FOR_FONTS)) &&
			!pkg->info.is_low_mip)
			continue;

		// Prevent sending HTEX to renderer if the texture doesn't have any MIP levels
		// loaded, since this means the texture has been freed before the HTEX load could
		// complete. This also allows low MIP loading to complete, since 
		// since HTEX loading is mutually exclusive with low MIP loading.
		if (!pkg->fromHtex || pkg->bind->loaded_data->levels_loaded)
		{
			if (pkg->info.is_low_mip) {
				assert(pkg->bind->loaded_data->mip_loading);
				if (!(pkg->bind->loaded_data->mip_bound_on & gfx_state.currentRendererFlag)) {
					//memlog_printf(&tex_memlog, "%u: %d: texDoQueuedLoads: lowmips %s to renderer #%d", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, pkg->bind->name, gfx_state.currentRendererIndex);
					// low mips
					texSendToRenderer(pkg->bind, &pkg->info, pkg->levelsNeeded, pkg->fromHtex);
					texsSentToRenderer++;
				}
				if ((pkg->bind->loaded_data->mip_bound_on & gfx_state.allRenderersFlag) == gfx_state.allRenderersFlag) {
					bRemoveIt = true;
					pkg->bind->loaded_data->mip_loading = 0;
				}
			} else {
				assert(pkg->bind->loaded_data->tex_is_loading_for);
				if (pkg->bind->loaded_data->tex_is_loading_for & gfx_state.currentRendererFlag) {
					memlog_printf(&tex_memlog, "%u: %d: texDoQueuedLoads: %s%s to renderer #%d", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, pkg->bind->name, pkg->fromHtex?" (htex)":"", gfx_state.currentRendererIndex);
					// Normal texture or htex
					texSendToRenderer(pkg->bind, &pkg->info, pkg->levelsNeeded, pkg->fromHtex);
					texsSentToRenderer++;
				}
				if (0==(NOT_RAW_DATA_BITMASK&pkg->bind->loaded_data->tex_is_loading_for)) {
					bRemoveIt = true;
				}
			}
		}
		else
		{
			bRemoveIt = true;
			texLog(pkg->bind, "skipped texSendToRenderer");
		}

		if (bRemoveIt) {
			//memlog_printf(&tex_memlog, "%u: %d: texDoQueuedLoads: %s no longer needed for any renderer", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, pkg->bind->name);
			// Not being loaded for any renderer
			if (!pkg->info.is_low_mip) {
				if (texGetTexWord(pkg->bind) && !pkg->needHtex) {
					texWordDoneLoading(texGetTexWord(pkg->bind), pkg->bind);
				}
				if (pkg->info.texture_data && !pkg->info.ringbuffer_data) {
					int refCount = texReadInfoMemRefDecrement(&pkg->info, pkg->bind);
					if (refCount == 0) {
					}
				}
				pkg->bind->loaded_data->loading = 0;
				texLog(pkg->bind, "texDoQueuedLoads done");
				if (pkg->bind->loaded_data->mip_bound_on)
					eaPush(&queuedTexLowMipFrees, pkg->bind);
			} else if (pkg->info.is_low_mip && pkg->info.free_low_mip) {
				if (pkg->info.texture_data && !pkg->info.ringbuffer_data)
				{
					int refCount = texReadInfoMemRefDecrement(&pkg->info, pkg->bind);
					if (refCount == 0) {
					}
				}
			}

			dequeueTexForRenderer(pkg);

			// kick off the async htex read if we need it.
			if (pkg->needHtex) {
				texLog(pkg->bind, "queueHtexLoad");
				queueHtexLoad(pkg->bind, pkg->needRawData, pkg->levelsNeeded);
				pkg->bind->loaded_data->loading = 1;
				pkg->bind->loaded_data->tex_is_loading_for = pkg->bind->tex_is_loaded;
				texLog(pkg->bind, "after queueHtexLoad");
			}

			if (pkg->fromHeap)
			{
				SAFE_FREE(pkg);
			} else
				MP_FREE(TexThreadPackage, pkg);
		}
	}
}
/* Called every frame to check to see if the background loader has finished it's work */
static void texCheckThreadLoader(void)
{
	TexThreadPackage *head=NULL, *next;
	TexThreadPackage *pkg;//, *prevPkg;
	int		num_bytes_loaded = 0;
	int		did_something = 0;

	PERFINFO_AUTO_START_FUNC();

	dynamicCacheCheckAll(0);

	if (basicTexturesReadyForFinalProcessing)
	{
		EnterCriticalSection(&CriticalSectionTexLoadQueues); 

		head = basicTexturesReadyForFinalProcessing;
		basicTexturesReadyForFinalProcessing = NULL;

		LeaveCriticalSection(&CriticalSectionTexLoadQueues);
	}

	if(!head)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	for(pkg = head; pkg ; pkg = next)
	{
		bool pkg_queued=false;
		next = pkg->next;

		if (pkg->needsFree)
		{
			// do nothing
			pkg->needsFree = 0;
		}
		else if (pkg->needRawData && pkg->info.texture_data) {
			num_bytes_loaded += texGetRareData(pkg->bind)->bt_rawInfo->size;
			did_something = 1;
		}
		else if (pkg->info.texture_data)
		{
			// queue this and send it to appropriate renderers at SetActive() time
			//memlog_printf(&tex_memlog, "%u: texCheckThreadLoader: %s queued", gfx_state.client_frame_timestamp, pkg->bind->name);
			queueTexForRenderer(pkg);
			pkg_queued = true;

			num_bytes_loaded += pkg->bind->width*pkg->bind->height;
			did_something = 1;
		}
		else if (texGetTexWordParams(pkg->bind) || !pkg->needRawData && texGetTexWord(pkg->bind))
		{
			// TexWord package, data stored elsewhere
			texGetRareData(pkg->bind)->hasBeenComposited = true;
			queueTexForRenderer(pkg);
			pkg_queued = true;
			did_something = 1;
		}
		else
		{
			memlog_printf(&tex_memlog, "%u: BAD TEXTURE %s", gfx_state.client_frame_timestamp, pkg->bind->name);
			printf("Missing/bad texture %s \n  (were files deleted while you were running?)\n", pkg->bind->name);
			errorIsDuringDataLoadingInc(pkg->bind->name); // Never popping, if we crash, we need to re-verify
			//*pkg->bind = *white_tex; // Causes frees to go crazy!
			if (isDevelopmentMode())
			{
				texLog(pkg->bind, "texCheckThreadLoader");
				// Allow this texture to get reloaded
				texAllocLoadedData(pkg->bind)->loading = 0;
				texAllocLoadedData(pkg->bind)->tex_is_loading_for = 0;
			}
		}

		if (pkg->needRawData)
			texAllocRareData(pkg->bind)->tex_last_used_time_stamp_raw = gfx_state.client_frame_timestamp;
		else
			pkg->bind->tex_last_used_time_stamp = gfx_state.client_frame_timestamp;
		if (pkg->needRawData) {
			if (pkg->needHtex) {
				texLog(pkg->bind, "queueHtexLoad raw");
				queueHtexLoad(pkg->bind, pkg->needRawData, pkg->levelsNeeded);
			} else {
				texLog(pkg->bind, "texCheckThreadLoader raw");
				pkg->bind->tex_is_loaded |= RAW_DATA_BITMASK;
				pkg->bind->loaded_data->tex_is_loading_for &= ~RAW_DATA_BITMASK;
			}
		}

		if (!pkg_queued)
		{
			if (pkg->fromHeap)
			{
				SAFE_FREE(pkg);
			} else
				MP_FREE(TexThreadPackage, pkg);
		}
	}

	if (did_something) {
		// Let TexWords fire off any processing that needs to happen based on new textures that finished loading
		PERFINFO_AUTO_START("texWordsCheck", 1);
		texWordsCheck();
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_START("gfxLoadUpdate", 1);
	gfxLoadUpdate(num_bytes_loaded);
	PERFINFO_AUTO_STOP();

	// We have just loaded a texture, let's check to see if any old ones can be removed
	if (did_something) {
		//texUnloadTexturesToFitMemory(); // Happens in main loop now
	}

	PERFINFO_AUTO_STOP();
}

// Order of applying texture permutations:
//   Start with base texture name
//   //Apply scene file swaps
//   Take result from last step and apply language/search path swaps
//   Take base name from last step and look for associated Text overlays

char **textureSearchPath=NULL;
int numTextureSearchPaths=0;
void texSetSearchPath(char *searchPath)
{
	// Expects something like "#English", "#French;#English" or just ""
	char *walk;
	char *freeme;
	char *strtokContext=NULL;
	while (freeme = eaPop(&textureSearchPath)) 
		free(freeme);
	walk = strtok_s(searchPath, ";, \t", &strtokContext);
	while (walk) {
		while (*walk=='#') walk++;
		eaPush(&textureSearchPath, strdup(walk));
		walk = strtok_s(NULL, ";, \t", &strtokContext);
	}
	numTextureSearchPaths = eaSize(&textureSearchPath);
}

static int texCheckForSwaps(BasicTexture *texbind)
{
	char buf[MAX_PATH];
	int ret = -1;
	int i;
	assert(PTR_TO_U32(texbind) != 0xfafafafa);
	assert(PTR_TO_U32(texbind->name) != 0xfafafafa);
	assert(*(int*)texbind->name != 0xfafafafa);
	texbind->actualTexture = texbind;
	// Check for localized swaps
	for (i=0; i<numTextureSearchPaths; i++) {
		BasicTexture *localized;
		sprintf(buf, "%s#%s", texbind->actualTexture->name, textureSearchPath[i]);
		localized = texFind(buf, false);
		if (localized && localized != white_tex) {
			//verbose_printf("Using localized texture %s/%s instead of %s\n", localized->dirname, localized->name, texbind->name);
			texbind->actualTexture = localized;
			break;
		}
	}

	return ret;
}

void texCheckSwapList(void)
{
	int i;

	// assign new textures based on SwapList, and reset any old, non-swapped textures
	for( i = eaSize(&g_basicTextures)-1 ; i >= 0; i-- ) {
		texCheckForSwaps(g_basicTextures[i]);
	}
}

void texForceTexLoaderToComplete(int force_texword_flush)
{
	bool didLock=false;

	if (force_texword_flush) {
		texWordsFlush();
		// LIGHTMAPTODO: lightmapManagerFlush(false, false);
	}

	if (gfx_state.currentDevice && !gfx_state.currentDevice->rdr_device->is_locked_nonthread) {
		// Lock the device, because showing the window can cause WM_ACTIVATEAPP to happen, which calls rxbxIsInactiveDirect()
		didLock = true;
		rdrLockActiveDevice(gfx_state.currentDevice->rdr_device, false);
	}

	while( texLoadsPending(force_texword_flush) )
	{
		Sleep(1);
		texDoQueuedLoads();
		texWordsCheck();
		gfxLoadUpdate(10);
	}

	if (didLock) {
		rdrUnlockActiveDevice(gfx_state.currentDevice->rdr_device, false, false, false);
	}

	texCheckThreadLoader();
	if (force_texword_flush) {
		// LIGHTMAPTODO: lightmapManagerLoadAllRequested(0);
	}
}

static void waitLoaded(BasicTexture *match, int rawData, int renderer)
{
	int flag;

	assertmsg(rawData || gfx_state.currentDevice->rdr_device->is_locked_nonthread,
		"Cannot force load a texture outside of GraphicsLib functions which have the renderer locked!  Use LOAD_IN_BACKGROUND instead");

	if (rawData)
		flag = RAW_DATA_BITMASK;
	else
		flag = 1 << renderer;
	// Wait for the background thread to finish loading.
	while(!(match->tex_is_loaded & flag) && (SAFE_MEMBER(match->loaded_data,tex_is_loading_for)&flag && SAFE_MEMBER(match->loaded_data, loading)) && !disable_parallel_tex_thread_loader)
	{
		Sleep(1);
		texCheckThreadLoader(); //only called from main thread
		if (gfx_state.currentDevice->rdr_device->is_locked_nonthread) { // HACK!
			texDoQueuedLoads();
		}
	}
}

// Yeah, yeah, stupid name, I know...
static void texLoadBasicInternalInternal(BasicTexture *match, TexLoadHow mode, int rawData, U32 num_levels)
{
	bool needHigh;
	const char *filename;

	if (num_levels == 0) {
		num_levels = gfx_state.no_htex?match->base_levels:match->max_levels;
	}

	if (match->name[0]=='#') {
		// dynamic texture in a hogg or something, just use the name
		filename = match->name;
	} else {
		filename = match->fullname;
	}

	needHigh = num_levels > match->base_levels;

	texRecordNewMemUsage(match, TEX_MEM_LOADING, texEstimateMemUsage(match, num_levels));

	if( mode == TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD)
	{
		// If currently in the main thread, but want it now!
		memlog_printf(&tex_memlog, "%u: %d: texLoad %sTEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD %s %d", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, rawData?"RAW ":"", match->name, num_levels);

		texForceTexLoaderToComplete(1);

		{
			TexThreadPackage *pkg;
			pkg = MP_ALLOC(TexThreadPackage);
			pkg->bind = match;
			pkg->needRawData = rawData;
			pkg->levelsNeeded = num_levels;

			InterlockedIncrement(&numTexLoadsInThread);
			texDoThreadedTextureLoading( filename, pkg );
		}
        if (needHigh) {
			TexThreadPackage *pkg;
			char highFn[MAX_PATH];

			pkg = MP_ALLOC(TexThreadPackage);
			pkg->bind = match;
			pkg->needRawData = rawData;
			pkg->levelsNeeded = num_levels;
			pkg->fromHtex = 1;

			changeFileExt(filename, ".htex", highFn);

			InterlockedIncrement(&numTexLoadsInThread);
			texDoThreadedTextureLoading( highFn, pkg );
		}

		texCheckThreadLoader(); //only called from main thread
		waitLoaded(match, rawData, gfx_state.currentRendererIndex);
	}
	else if( mode == TEX_LOAD_IN_BACKGROUND/* || mode == TEX_LOAD_IN_BACKGROUND_FROM_BACKGROUND*/)
	{
        TexThreadPackage *pkg;
		FileLoaderPriority prio = match->tex_is_loaded?FILE_LOWEST_PRIORITY:	//Already loaded and changing resolution - lowest priority
			TEX_HAS_MIPDATA(match)?FILE_LOW_PRIORITY: // we have low-mips - low priority (below geometry)
			FILE_MEDIUM_HIGH_PRIORITY; // we have nothing to draw - higher priority than geometry
		
		memlog_printf(&tex_memlog, "%u: %d: texLoad %sTEX_LOAD_IN_BACKGROUND %s %d", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, rawData?"RAW ":"", match->name, num_levels);

        pkg = MP_ALLOC(TexThreadPackage);
        pkg->bind = match;
        pkg->needRawData = rawData;
        pkg->levelsNeeded = num_levels;
        pkg->needHtex = needHigh;	// don't load the htex now, but after the wtex finishes.

        InterlockedIncrement(&numTexLoadsInThread);
        fileLoaderRequestAsyncExec(filename, prio, false, texDoThreadedTextureLoading, pkg);
	}
	else
		assert(0);
}

// Loads an actual bind (no dereferencing to actualTexture, assume that was already done)
// Sends off to the TexWords system if required
void texLoadBasicInternal(BasicTexture *bind, TexLoadHow mode, WLUsageFlags use_category, U32 num_levels, int rawData)
{
	BasicTexture *match=bind;
	int need_to_load=0;
	int flag;

	assert(!(bind->flags & TEX_TEXGEN));
	assert(rawData == 0 || rawData == 1);

	PERFINFO_AUTO_START_FUNC();

	texAllocLoadedData(bind);
	if (texOverMemory(texEstimateMemUsage(bind, num_levels)) || bind->loaded_data->tex_is_unloading)
	{
		texLog(bind, "texLoadBasicInternal over budget");
		PERFINFO_AUTO_STOP();
		return;
	}

	if (rawData) {
		flag = RAW_DATA_BITMASK;
	} else  if (gfx_state.currentRendererFlag) {
		flag = gfx_state.currentRendererFlag;
	} else {
		texLog(bind, "texLoadBasicInternal no renderer");
		PERFINFO_AUTO_STOP();
		return;
	}

	texLoadCalls++;

	if (disable_parallel_tex_thread_loader &&
		mode == TEX_LOAD_IN_BACKGROUND)
	{
		mode = TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD;
	}

	match->use_category |= use_category;
	if (!match->use_category) {
		match->use_category = WL_FOR_NOTSURE;
	}
	if( gfx_state.currentAction && gfx_state.currentAction->override_usage_flags ) {
		match->use_category = gfx_state.currentAction->override_usage_flags;
	}
	
	//if (mode == TEX_LOAD_DONT_ACTUALLY_LOAD) {
	//	PERFINFO_AUTO_STOP();
	//	return;
	//}

	if ( !(match->tex_is_loaded & flag) && !(SAFE_MEMBER(match->loaded_data, tex_is_loading_for) & flag))
	{
		// Not loaded for the current renderer, and not marked as being needed for this renderer
		if ( !(match->tex_is_loaded & flag)) // Double check for thread syncing
		{
			texLog(match, "texLoadBasic");
			// Not loaded on this renderer
			// Check if it's already being loaded destined for another renderer
			if (rawData) {
				need_to_load = 1;
				match->loaded_data->tex_is_loading_for |= flag;
			} else {
				if (match->loaded_data->loading) {
					// Already being loaded
					match->loaded_data->tex_is_loading_for |= flag; // Flag it for us too!
				} else {
					// Not being loaded
					need_to_load = 1;
					match->loaded_data->tex_is_loading_for |= flag;
					match->loaded_data->loading = 1;
				}
			}
			texLog(match, "texLoadBasic 2");
		}
	}

	if ( need_to_load )
	{
		if (!rawData && texGetTexWord(match)) {
			memlog_printf(&tex_memlog, "%u: %d: texWordLoad %s %s", gfx_state.client_frame_timestamp, gfx_state.currentRendererIndex, (mode == TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD)?"TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD":"TEX_LOAD_IN_BACKGROUND", match->name);
			texWordLoad(texGetTexWord(match), mode, use_category, match);
		} else {
			texLoadBasicInternalInternal(match, mode, rawData, num_levels);
		}
	}

	// If the caller requested for to wait while the texture is loading and the texture is already being loaded in the background...
	if ( mode == TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD )
	{
		waitLoaded(match, rawData, gfx_state.currentRendererIndex);
	}
	// flag the texture as having just been used.  This should effectively flag
	//		all textures when a new map loads
	if (rawData)
		texAllocRareData(match)->tex_last_used_time_stamp_raw = gfx_state.client_frame_timestamp;
	else
		match->tex_last_used_time_stamp = gfx_state.client_frame_timestamp;
	//match->min_draw_dist = 0;

	PERFINFO_AUTO_STOP();
}

BasicTexture *texLoadBasic(const char *name, TexLoadHow mode, WLUsageFlags use_category)
{
	BasicTexture *match;

	//find TexBind from the name
	match = texFind(name, true);
	if(!match) 
		return white_tex; //handle bad texture name

	texLoadBasicInternal(match->actualTexture, mode, use_category,
        (gfx_state.no_htex ? match->actualTexture->base_levels : match->actualTexture->max_levels), 0);
	return match;
}

void texLoadRawDataInternal(BasicTexture *bind, TexLoadHow mode, WLUsageFlags use_category)
{
	texAllocRareData(bind->actualTexture)->rawReferenceCount++;
	texLoadBasicInternal(bind->actualTexture, mode, use_category, (gfx_state.no_htex ? bind->actualTexture->base_levels : bind->actualTexture->max_levels), 1);
}

BasicTexture *texLoadRawData(const char *name, TexLoadHow mode, WLUsageFlags use_category)
{
	BasicTexture *match;

	//find TexBind from the name
	match = texFind(name, true);
	if(!match)
		match = white_tex; //handle bad texture name

	if (!match) // No white.tga, bootstrapping texture_library/
		return NULL;

	if (match == &fake_white)
		return NULL;

	texLoadRawDataInternal(match, mode, use_category);
	return match;
}

void texUnloadRawData(BasicTexture *texBind)
{
	assert(texBind && texGetRawReferenceCount(texBind->actualTexture));
	//assert(texBind->actualTexture->tex_is_loaded & RAW_DATA_BITMASK);
	texAllocRareData(texBind->actualTexture)->rawReferenceCount--;
	// Do not unload it now, the texture atlasing leaves things sitting around
	// loading with no reference count for a bit
}

void texSetupParametersFromBase(BasicTexture *bind)
{
	TexWord *texWord = texGetTexWord(bind);
	BasicTexture *base;
	int width, height;
	if (!texWord)
		return;
		
	base = texWordGetBaseImage(texWord, &width, &height);
	if (base) {
		// Use bottom layer's low mips
		texAllocRareData(bind)->baseTexture = base;
		// Take compression and other flags from the base layer
		bind->mip_type = base->mip_type;
		bind->mip_data_index = base->mip_data_index;
		if (base != invisible_tex)
		{
			// Copy some texopt flags
			bind->bt_texopt_flags |= base->bt_texopt_flags & TEXOPT_FLAGS_TO_COPY_TO_TEXWORD;
			// bind->bt_texopt_flags = base->bt_texopt_flags; // Don't do this - all dynamic textures without a base inherit from invisible, which has no MIPs.
		}
		// Use default sizes, etc (may be overridden in texWordDoComposition)
		bind->width = width;
		bind->height = height;
		bind->realWidth = pow2(width);
		bind->realHeight = pow2(height);
		bind->rdr_format = base->rdr_format;
	} else {
		assert(0);
	}
}

// Allocates a dummy texture to hold a dynamic, TexWords texture
// This is called for all dynamic TexWords at startup, and any
//  unique instances of a dynamic TexWord at runtime.
// Returns NULL if an invalid layout is specified
BasicTexture *texAllocateDynamic(const char *layoutName, bool addToHashtable)
{
	BasicTexture *bind=NULL;
	TexWord *texWord;
	char buf[MAX_PATH];
	
	texWord = texWordFind(layoutName, 1);
	if (!texWord) {
		return NULL;
	}

	// Allocate a new BasicTexture
	bind = basicTextureCreate();

	// Fill it in
	texFixName(layoutName, SAFESTR(buf));
	layoutName = buf;
	bind->name = allocAddString(layoutName);
	bind->actualTexture = bind;
	bind->fullname = "dynamicTexture";
	// Assign from TexOpts, etc (overwritten if we find the base texture)
	texResetTexOptParametersBasic(bind);
	texSetBindsSubBasic(bind);
	texAllocRareData(bind)->texWord = texWord; // Must be *after* texSetBindsSubBasic!
	// Try to get some data from bottom layer
	texSetupParametersFromBase(bind);

	// Store in global lists
	eaPush(&g_basicTextures, bind);

	if (addToHashtable) {
		StashElement element;
		bind->name = allocAddString(layoutName);
		if (!stashFindElement(g_basicTextures_ht, bind->name, &element)) {
			stashAddPointer(g_basicTextures_ht, bind->name, bind, false);
		} else {
			// Duplicate!
			BasicTexture *old = stashElementGetPointer(element);
			if (stricmp(old->fullname, bind->fullname)!=0) {
				ErrorFilenameDup(old->fullname, bind->fullname, layoutName, "Dynamic TexWord");
			}
		}
	}
	return bind;
}

// "Finds" a new, dynamic TexBind based on a TexWord layout file and various parameters
//  takes ownership of the params pointer and all sub-data
BasicTexture *texFindDynamic(const char *layoutName, TexWordParams *params, WLUsageFlags use_category, const char *blameFileName)
{
	BasicTexture *bind=NULL;

	if (layoutName)
		bind = texAllocateDynamic(layoutName, false);

	if (!bind) {
		if (isDevelopmentMode())
			ErrorFilenamef(blameFileName, "Reference to unknown TexWordLayout: \"%s\"", layoutName);
		destroyTexWordParams(params);
		return white_tex;
	}

	bind->flags |= TEX_DYNAMIC_TEXWORD;
	texAllocRareData(bind)->texWordParams = params;
	texAllocRareData(bind)->dynamicReferenceCount++;
	bind->use_category |= use_category;

	return bind;
}

static void texFindNeededBinds(void)
{
	// Find basic textures
	white_tex			= texFindAndFlag("white", true, WL_FOR_UTIL);
	if (!white_tex) {
		assert(gbNoGraphics);
		fake_white.width = fake_white.realWidth = fake_white.height = fake_white.realHeight = 1;
		fake_white.name = "white";
		fake_white.actualTexture = &fake_white;
		white_tex = &fake_white;
	}
	invisible_tex		= texFindAndFlag("invisible", true, WL_FOR_UTIL);
	// Invisible used to be set as an "opaque" texture, since it doesn't need to
	//  be sorted or blended, but this was causing problems with z-prepass, etc.
	//if(invisible_tex)
	//	invisible_tex->flags &= ~TEX_ALPHA;
	dummy_bump_tex		= texFindAndFlag("dummy_bump", true, WL_FOR_UTIL);
	black_tex			= texFindAndFlag("black", true, WL_FOR_UTIL);

	dummy_cube_tex		= texFindAndFlag("test_cube_cube", true, WL_FOR_UTIL);
	default_ambient_cube = texFindAndFlag("default_ambient_cube", true, WL_FOR_UTIL);

	default_env_cubetex = texFindAndFlag("TerrainTest_cube", true, WL_FOR_WORLD);
	if (!default_env_cubetex)
		default_env_cubetex = dummy_cube_tex;
	default_env_spheretex= texFindAndFlag("TerrainTest_spheremap", true, WL_FOR_WORLD);
	if (!default_env_spheretex)
		default_env_spheretex = black_tex;

	tex_from_sky_file = texFindAndFlag("0_from_sky_file", true, WL_FOR_UTIL);
	if (!tex_from_sky_file)
		tex_from_sky_file = black_tex;

	tex_use_pn_tris = texFindAndFlag("Use_PN_Tris", true, WL_FOR_UTIL);
	if (!tex_use_pn_tris)
		tex_use_pn_tris = black_tex;
}

void texMakeDummies(void)
{
	U32 bitmap = 0x00000000;
	if (dummy_volume_tex)
		texGenFree(dummy_volume_tex);

	dummy_volume_tex = texGenNewEx(1, 1, 1, "DummyVolume", TEXGEN_NORMAL, WL_FOR_UTIL);
	texGenUpdate(dummy_volume_tex, (U8*)&bitmap, RTEX_3D, RTEX_BGRA_U8, 1, true, false, true, false);
}

/* Inits global TexBinds */
void texMakeWhite(void)
{
	texLoadRawData("white.tga",TEX_LOAD_IN_BACKGROUND, WL_FOR_UTIL|WL_FOR_UI);

	if (white_tex)
		atlasMakeWhite();

	texMakeDummies();
}

void texMakeDummyCubemap()
{
}

/*
RdrTexFormat texFormatFromOldFormat(int old_format)
{
	switch(old_format) {
	xcase 8: //TEXFMT_ARGB_0888:
		return RTEX_BGR_U8;
	xcase 7: // TEXFMT_ARGB_8888:
		return RTEX_BGRA_U8;
	xcase 0x83f1: // GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
		return RTEX_DXT1;
	xcase 0x83f2: // GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
		return RTEX_DXT3;
	xcase 0x83f3: //GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		return RTEX_DXT5;
	xcase 10: // TEXFMT_RAW_DDS
		return TEXFMT_RAW_DDS;
	xdefault:
		return RTEX_BGRA_U8; // Shouldn't happen
	}
}
*/



void texForEachTexture(TextureCallback callback, void *userData)
{
	int i;
	int total = eaSize(&g_basicTextures);
	for (i=0; i<total; i++)
		callback(g_basicTextures[i], userData);
	texGenForEachTexture(callback, userData, -1);
}

///########### TEX SHOW USAGE #############################

static int used_count;
static int used_size;
static char *g_match_string;
static void showUsagePrintf(BasicTexture *bind, void *userData)
{
	if (g_match_string && g_match_string[0] && !strstri(bind->fullname,g_match_string))
		return;
	if (bind->loaded_data->mip_bound_on) {
		int mipsize = 8192; // Assume we can get this down to at least 8192 in the end, it might take 32K currently
		used_size += mipsize;
		used_count++;
		printf("%6d [low MIPs] %s/%s\n",mipsize,bind->fullname,bind->name);
	}
	if (!(bind->tex_is_loaded & NOT_RAW_DATA_BITMASK))
		return;
	used_size += bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO];
	used_count++;
	printf("%6d [%3d x %3d] %s/%s\n",bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO],bind->width,bind->height,bind->fullname,bind->name);

}

// Safe to call when crashed
void texShowUsagePrintf(char *match_string)
{
	used_count = 0;
	used_size = 0;
	g_match_string = match_string;
	texForEachTexture(showUsagePrintf, NULL);
	printf("Total: %d textures (+%d queued to free) in %s\n", used_count, eaSize(&queuedTexFrees), friendlyBytes(used_size));
}


static void showUsage(BasicTexture *bind, void *userData)
{
	FILE *file = userData;
	char		buf[1000];
	if (g_match_string && g_match_string[0] && !strstri(bind->fullname,g_match_string))
		return;
	if (SAFE_MEMBER(bind->loaded_data, mip_bound_on)) {
		U32 mipsize = bind->file_pos - sizeof(TextureFileHeader);
		used_size += mipsize;
		used_count++;
		sprintf(buf,"%6d [low MIPs] %s/%s",mipsize,bind->fullname,bind->name);
		conPrintf("%s\n", buf);
		if (file)
			fprintf(file,"%s\n",buf);
	}
	if (!(bind->tex_is_loaded & NOT_RAW_DATA_BITMASK))
		return;
	assert(bind->loaded_data);
	used_size += bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO];
	used_count++;
	sprintf(buf,"%6d [%3d x %3d] %s/%s",bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO],bind->width,bind->height,bind->fullname,bind->name);
	conPrintf("%s\n", buf);
	if (file)
		fprintf(file,"%s\n",buf);
}

// Prints out texture memory usage to the console and to c:\texusage.txt.  Pass in a string to search, or "" for everything.
AUTO_COMMAND ACMD_CATEGORY(Performance);
void texShowUsage(char *match_string)
{
	FILE		*file;

	used_count = 0;
	used_size = 0;
	g_match_string = match_string;
	file = fopen("c:/texusage.txt","wt");
	texForEachTexture(showUsage, file);
	conPrintf("Total: %d textures in %s\n", used_count, friendlyBytes(used_size));
	if (file) {
		fprintf(file, "Total: %d textures in %s\n", used_count, friendlyBytes(used_size));
		fclose(file);
	}
}

static void texConvertToPooledFormat(TexReadInfo *info, bool reversed_mips)
{
	U32 desiredw=8, desiredh=8;
	U32 w=info->width, h=info->height;
	if (texturePoolResizeDisable)
		return;
	PERFINFO_AUTO_START_FUNC();
	if (!info->depth)
		info->depth = 1;
	if (info->tex_format != RTEX_BGRA_U8)
	{
		// Convert
		if (!uncompressRawTexInfo(info,reversed_mips))
		{
			devassert(!"Decompressing unsupported texture format");
			PERFINFO_AUTO_STOP();
			return;
		}
	}
	assert(info->tex_format == RTEX_BGRA_U8);
	// Grow/shrink to desired size
	if (info->width != desiredw || info->height != desiredh)
	{
		U8 *data = memrefAlloc(desiredw*desiredh*4*info->depth);
		U32 xrep = desiredw / info->width;
		U32 yrep = desiredh / info->height;
		U32 xinc = info->width / desiredw;
		U32 yinc = info->height / desiredh;
		U32 i, j, k, y;
		U8 *out = data;

		assert(info->depth >= 1);

		MAX1(xrep, 1);
		MAX1(yrep, 1);
		MAX1(xinc, 1);
		MAX1(yinc, 1);

		for (k=0; k<info->depth; k++)
		{
			for (j=0; j<h; j+=yinc)
			{
				for (y=0; y<yrep; y++)
				{
					U8 *src = &info->texture_data[j*info->width*4];
					for (i=0; i<w; i+=xinc)
					{
						memcpy(out, src, xrep*4);
						out+=xrep*4;
						src+=4*xinc;
					}
				}
			}
		}
		assert(out == data + desiredw*desiredh*4*info->depth);
		texReadInfoAssignMemRefAlloc(info, data, NULL);
		info->width = desiredw;
		info->height = desiredh;
		info->size = desiredw*desiredh*4;
		info->level_count = 1;
	}
	info->level_count = 1; // Remove MIPs even if there still are some
	PERFINFO_AUTO_STOP();
}

static void texBindLowMips(BasicTexture *texbind)
{
	TexReadInfo				info = {0};
	BasicTexture *lowMipBind = (texbind->has_rare && texGetRareData(texbind)->baseTexture)?texGetRareData(texbind)->baseTexture:texbind;

	if (!TEX_HAS_MIPDATA(lowMipBind))
	{
		return;
	}

	info.size = texMipByteSize(lowMipBind);
	texReadInfoMemRefAlloc(&info, info.size, texbind);
	memcpy(info.texture_data, &lowmip_data[lowMipBind->mip_data_index], info.size);

	if (!texAllocLoadedData(texbind)->mip_handle)
		texbind->loaded_data->mip_handle = rdrGenTexHandle(0);
	info.width = 8;
	info.height = 8;
	info.level_count = 1;
	info.tex_format = texMipFormatFromType(lowMipBind->mip_type);
	info.is_low_mip = 1;
	info.free_low_mip = 0; // Don't free, we're pointing at static data
	if (!(texbind->bt_texopt_flags & (TEXOPT_CUBEMAP|TEXOPT_VOLUMEMAP))) // Only 2D textures
	{
		texConvertToPooledFormat(&info,textureMipsReversed(texbind));
	}
	if (gfx_state.currentDevice->rdr_device->is_locked_nonthread) {
		texSendToRenderer(texbind, &info, 1, 0);
	} else {
		// Queue it
		TexThreadPackage *pkg = MP_ALLOC(TexThreadPackage);
		pkg->bind = texbind;
		// This assignment violates the policy of only using accessors to modify the data_refcounted member
		// of TexReadInfo, but the following manual ref increment is consistent with the ref counting code,
		// and the allocated object will not have an existing data_refcounted member.
		pkg->info = info;
		texReadInfoMemRefIncrement(&pkg->info, texbind);
		pkg->info.free_low_mip = 1; // Free it, it's a copy
		listAddForeignMember(&queuedTexLoadPkgs, pkg);
		texbind->loaded_data->mip_loading = 1;
	}
	texReadInfoMemRefDecrement(&info, texbind);
}

TexHandle texDemandLoad(BasicTexture *texbind, F32 dist, F32 uv_density, BasicTexture *errortex)
{
	BasicTexture *actualTexture;
	U32 levels_needed;

 	if (!texbind)
		return 0;

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
	assert(!texIsFreed(texbind));
	if (texbind->queued_delete)
		gfxTraceTexLifetime("texDemandLoad: %p %s queued for delete", texbind, texbind->name);
#endif

	PERFINFO_AUTO_START_FUNC();

	actualTexture = texbind->actualTexture;

	texAllocLoadedData(actualTexture);
	assert(actualTexture->loaded_data);

	if (g_needTextureBudgetInfo)
	{
		actualTexture->loaded_data->tex_last_used_count++;
	}

	MAX1F(dist, 0.0f);

	levels_needed = texDesiredLevels(actualTexture, dist, uv_density, gfx_state.currentAction);

	if (actualTexture->tex_last_used_time_stamp != gfx_state.client_frame_timestamp) {
		actualTexture->loaded_data->uv_density = uv_density;
		actualTexture->loaded_data->min_draw_dist = dist;
		actualTexture->max_desired_levels = levels_needed;
	} else {
		MIN1F(actualTexture->loaded_data->uv_density, uv_density);
		MIN1F(actualTexture->loaded_data->min_draw_dist, dist);
		MAX1(actualTexture->max_desired_levels, levels_needed);
	}

	actualTexture->tex_last_used_time_stamp = gfx_state.client_frame_timestamp;
	if (actualTexture == g_selected_texture && (gfx_state.frame_count & 2))
		actualTexture = (gfx_state.frame_count & 1)?white_tex:black_tex;

	if (actualTexture->tex_is_loaded & gfx_state.currentRendererFlag) {
		PERFINFO_AUTO_STOP();
		return actualTexture->tex_handle;
	}

	if (actualTexture->flags & TEX_TEXGEN)
	{
		if (!gfx_state.currentDevice->rdr_device->is_locked_nonthread) {
			if (actualTexture->tex_handle) {
				// Probably loaded this frame, not yet sent to renderer in texGenDoFrame() when the lock happens
				PERFINFO_AUTO_STOP();
				return actualTexture->tex_handle;
			}
		}
		if (gfx_state.currentRendererIndex == 0) // Expected on other devices because of terrain
			OutputDebugStringf("Dynamically generated texture used before getting loaded!\n");
		PERFINFO_AUTO_STOP();
		return errortex->tex_handle;
	}
	if (actualTexture->flags & TEX_SCRATCH)
	{
		PERFINFO_AUTO_STOP();
		return actualTexture->tex_handle;
	}

	if (!(SAFE_MEMBER(actualTexture->loaded_data, tex_is_loading_for) & gfx_state.currentRendererFlag))
	{
		actualTexture->use_category |= texbind->use_category;
		texLoadBasicInternal(actualTexture,TexLoadHowFromTexture(actualTexture), actualTexture->use_category, levels_needed, 0);
	}
	if (TEX_HAS_MIPDATA(actualTexture) && !(actualTexture->loaded_data->mip_loading) && !(actualTexture->loaded_data->mip_bound_on & gfx_state.currentRendererFlag) && (SAFE_MEMBER(actualTexture->loaded_data, tex_is_loading_for) & gfx_state.currentRendererFlag))
	{
		// Bind the low mip levels now!
		texBindLowMips(actualTexture);
	}

	PERFINFO_AUTO_STOP();

	if (actualTexture->loaded_data->mip_loading || (actualTexture->loaded_data->mip_bound_on & gfx_state.currentRendererFlag)) 
		return actualTexture->loaded_data->mip_handle;
	if (actualTexture->tex_is_loaded & gfx_state.currentRendererFlag)
		return actualTexture->tex_handle;
	
	if (texIs3D(actualTexture))
		errortex = dummy_volume_tex;
	if (errortex)
		return errortex->tex_handle;

	return 0;
}

TexHandle texDemandLoadLowMIPs(BasicTexture *texbind, BasicTexture *error_bind)
{
	BasicTexture *actualTexture;
	actualTexture = texbind->actualTexture;
	texAllocLoadedData(actualTexture);
	if (TEX_HAS_MIPDATA(actualTexture) && !(actualTexture->loaded_data->mip_loading) && !(actualTexture->loaded_data->mip_bound_on & gfx_state.currentRendererFlag) && (SAFE_MEMBER(actualTexture->loaded_data, tex_is_loading_for) & gfx_state.currentRendererFlag))
	{
		// Bind the low mip levels now!
		texBindLowMips(actualTexture);
	}
	return (actualTexture->loaded_data->mip_loading || (actualTexture->loaded_data->mip_bound_on & gfx_state.currentRendererFlag)) ? actualTexture->loaded_data->mip_handle : error_bind->tex_handle;
}

void gfxLoadTextures(void) // Loads headers of individual texture files
{
	static bool ranonce=false;

	loadstart_printf("Loading texture headers...");

	assertmsg(!ranonce, "gfxLoadTextures should be called only once!");
	ranonce = true;
	InitializeCriticalSection(&CriticalSectionTexLoadQueues);
	InitializeCriticalSection(&CriticalSectionQueueingLoads);
	InitializeCriticalSection(&CriticalSectionTexLoadData);

	texLoadHeaders();

	loadend_printf(" done (%d Textures).", eaSize(&g_basicTextures));
}


void texClearAllForDevice(int rendererIndex)
{
	int i;
	int flag = (1 << rendererIndex);
	for (i=0; i<eaSize(&g_basicTextures); i++)
	{
		BasicTexture *bind = g_basicTextures[i];
		bind->tex_is_loaded &= ~flag;
		if (bind->loaded_data)
		{
			bind->loaded_data->tex_is_loading_for &= ~flag;
			bind->loaded_data->mip_bound_on &= ~flag;
		}
	}
	// Remove anything in the free queue for this device - happens automatically
	// Remove anything in the done loading queue for this device - happens automatically
	texGenClearAllForDevice(rendererIndex);
}


void texGetMemUsageCallback(BasicTexture *bind, TexMemUsage *usage)
{
	if (bind->tex_is_loaded) {
		WLUsageFlags use_category = bind->use_category;
		int index=0;
		bool recent;
		if (!use_category)
			use_category = WL_FOR_NOTSURE;
		recent = (gfx_state.client_frame_timestamp - bind->tex_last_used_time_stamp < usage->recent_time);
		// Prefer dumping it in a bin which is part of the flags, otherwise dump in the appropriate bin
		// No longer double-counting.
		if (use_category & usage->flags_for_total)
			use_category &= usage->flags_for_total;
		while (use_category) {
			if (use_category & 1) {
				usage->count[index]++;
				usage->partial[usage->current_array_index].video.loaded[index]+=bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO] + bind->loaded_data->tex_memory_use[TEX_MEM_LOADING];
				usage->partial[usage->current_array_index].original.loaded[index]+=imgByteCount(texTypeFromFlags(bind->bt_texopt_flags), bind->rdr_format, bind->width, bind->height, bind->width, bind->max_levels);
				if (recent) {
					usage->partial[usage->current_array_index].video.recent[index]+=bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO] + bind->loaded_data->tex_memory_use[TEX_MEM_LOADING];
                    usage->partial[usage->current_array_index].original.loaded[index]+=imgByteCount(texTypeFromFlags(bind->bt_texopt_flags), bind->rdr_format, bind->width, bind->height, bind->width, bind->max_levels);
				}
				break; // Only one
			}
			index++;
			use_category >>=1;
		}
		if (bind->use_category & usage->flags_for_total) {
			usage->countTotal++;
			usage->partial[usage->current_array_index].video.loadedTotal+=bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO] + bind->loaded_data->tex_memory_use[TEX_MEM_LOADING];
            usage->partial[usage->current_array_index].original.loadedTotal+=imgByteCount(texTypeFromFlags(bind->bt_texopt_flags), bind->rdr_format, bind->width, bind->height, bind->width, bind->max_levels);
			if (recent) {
				usage->partial[usage->current_array_index].video.recentTotal+=bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO] + bind->loaded_data->tex_memory_use[TEX_MEM_LOADING];
                usage->partial[usage->current_array_index].original.recentTotal+=imgByteCount(texTypeFromFlags(bind->bt_texopt_flags), bind->rdr_format, bind->width, bind->height, bind->width, bind->max_levels);
			}
		}
	}
}

#define RECENT_SECONDS 1

void texGetMemUsage(TexMemUsage *usage, WLUsageFlags flags_for_total)
{
	int i, j;
	int iNewArrayIndex;

	usage->recent_time = timerCpuSpeed()*RECENT_SECONDS;
	usage->flags_for_total = flags_for_total;

	{
		int first_unloaded_texture_index = gbNoGraphics ? 0 : texPartialSort();
#define DIVISIONS 8 // Only doing part of the important part of the list - will often take one more frame than this value
		int count = first_unloaded_texture_index / DIVISIONS + 1;
		int iStart = usage->current_tex_index;
		int iEnd = iStart + count;
		if (iEnd >= first_unloaded_texture_index || usage->current_array_index == TEX_NUM_DIVISIONS-1)
		{
			iEnd = first_unloaded_texture_index;
			usage->current_tex_index = 0;
			iNewArrayIndex = 0;
		} else {
			usage->current_tex_index = iEnd;
			iNewArrayIndex = usage->current_array_index+1;
			assert(iNewArrayIndex < TEX_NUM_DIVISIONS);
		}
#undef DIVISIONS

		for (i=iStart; i<iEnd; i++)
			texGetMemUsageCallback(g_basicTextures[i], usage);
	}

	if (usage->current_tex_index == 0)
	{
		// Just finished touching all of them

		ZeroStruct(&usage->data);
		ZeroStruct(&usage->count);
		usage->countTotal = 0;

		// Add in TexGens (ends up in last bucket)
		texGenForEachTexture(texGetMemUsageCallback, usage, -1);

		// Sum all parts
		for (i=0; i<ARRAY_SIZE(usage->partial); i++) {
			for (j=0; j<ARRAY_SIZE(usage->partial[i].video.loaded); j++) {
				usage->data.video.loaded[j] += usage->partial[i].video.loaded[j];
				usage->data.video.recent[j] += usage->partial[i].video.recent[j];
				//usage->data.halfRes.loaded[j] += usage->partial[i].halfRes.loaded[j];
				//usage->data.halfRes.recent[j] += usage->partial[i].halfRes.recent[j];
				usage->data.original.loaded[j] += usage->partial[i].original.loaded[j];
				usage->data.original.recent[j] += usage->partial[i].original.recent[j];
			}
			usage->data.video.loadedTotal += usage->partial[i].video.loadedTotal;
			usage->data.video.recentTotal += usage->partial[i].video.recentTotal;
			//usage->data.halfRes.loadedTotal += usage->partial[i].halfRes.loadedTotal;
			//usage->data.halfRes.recentTotal += usage->partial[i].halfRes.recentTotal;
			usage->data.original.loadedTotal += usage->partial[i].original.loadedTotal;
			usage->data.original.recentTotal += usage->partial[i].original.recentTotal;
		}
		ZeroMemory(usage->partial, sizeof(usage->partial));

		// Save
		memcpy(&usage->saved, &usage->data, sizeof(usage->data));
		memcpy(&usage->savedCount, &usage->count, sizeof(usage->count));
		usage->savedCountTotal = usage->countTotal;

	} else {
		// Use saved
		memcpy(&usage->data, &usage->saved, sizeof(usage->data));
		memcpy(&usage->count, &usage->savedCount, sizeof(usage->count));
		usage->countTotal = usage->savedCountTotal;
	}

	usage->current_array_index = iNewArrayIndex;
}

typedef struct TexMemUsageDetailed
{
	TexMemUsageEntry ***list;
	int count;
	WLUsageFlags flags;
	U32 recent_time;
} TexMemUsageDetailed;

static void texGetMemUsageDetailedCallback(BasicTexture *bind, TexMemUsageDetailed *usage)
{
	char buf[MAX_PATH];
	TexMemUsageEntry *entry;
	if (!(bind->use_category & usage->flags))
		return;
	if (SAFE_MEMBER(bind->loaded_data, mip_bound_on)) {
		int mipsize = 8192; // Assume we can get this down to at least 8192 in the end, it might take 32K currently
		if (usage->count < eaSize(usage->list)) {
			entry = eaGet(usage->list, usage->count);
		} else {
			entry = StructAlloc(parse_TexMemUsageEntry);
			eaPush(usage->list, entry);
		}
		usage->count++;
		texFindDirName(SAFESTR(buf), bind);
		if (strStartsWith(buf, "texture_library"))
			entry->directory = allocAddString(buf+strlen("texture_library/"));
		else
			entry->directory = allocAddString(buf);
		entry->filename = allocAddString(getFileNameNoExt(buf, bind->name));
		entry->memory_use = mipsize;
		entry->width = 8;
		entry->height = 8;
		entry->isLowMips = true;
		entry->recent = false;
		entry->shared = !!(bind->use_category & ~usage->flags);
		entry->countInScene = 0;
		entry->dist = 0;
		entry->reduced = 0;
		entry->origWidth = 0;
		entry->origHeight = 0;
	}
	if (!(bind->tex_is_loaded & NOT_RAW_DATA_BITMASK))
		return;
	assert(bind->loaded_data);

	if (usage->count < eaSize(usage->list)) {
		entry = eaGet(usage->list, usage->count);
	} else {
		entry = StructAlloc(parse_TexMemUsageEntry);
		eaPush(usage->list, entry);
	}
	usage->count++;
	
	texFindDirName(SAFESTR(buf), bind);
	if (strStartsWith(buf, "texture_library"))
		entry->directory = allocAddString(buf+strlen("texture_library/"));
	else
		entry->directory = allocAddString(buf);
	entry->filename = allocAddString(getFileNameNoExt(buf, bind->name));
	entry->memory_use = bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO];
    entry->memory_use_original = imgByteCount(texTypeFromFlags(bind->bt_texopt_flags), bind->rdr_format, bind->width, bind->height, bind->width, bind->max_levels);
	entry->width = bind->realWidth;
	entry->height = bind->realHeight;
	entry->isLowMips = false;
	entry->recent = (gfx_state.client_frame_timestamp - bind->tex_last_used_time_stamp < usage->recent_time);;
	entry->shared = !!(bind->use_category & ~usage->flags);
	entry->countInScene = bind->loaded_data->tex_last_used_count;
	entry->dist = bind->loaded_data->min_draw_dist;
	entry->uv_density = bind->loaded_data->uv_density;
	entry->reduced = bind->max_levels - bind->loaded_data->levels_loaded;
	entry->origWidth = bind->width;
	entry->origHeight = bind->height;
	bind->loaded_data->tex_last_used_count = 0;
}

// Allocates and returns an earray of textures/memory sizes
void texGetMemUsageDetailed(WLUsageFlags flags, TexMemUsageEntry ***entries)
{
	TexMemUsageDetailed usage={0};
	if (!*entries)
		eaCreate(entries);
	usage.flags = flags;
	usage.list = entries;
	usage.count = 0;
	usage.recent_time = timerCpuSpeed()*RECENT_SECONDS;
	texForEachTexture(texGetMemUsageDetailedCallback, &usage);
	assert(eaSize(usage.list) >= usage.count);
	while (eaSize(usage.list) != usage.count)
		StructDestroy(parse_TexMemUsageEntry, eaPop(usage.list));
}


void texOncePerFramePerDevice(void)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	assert(gfx_state.currentDevice->rdr_device->is_locked_nonthread);

	// Move this somewhere else after FightClub is calling gfxUpdateFrame directly
	if (gfx_state.rendererNeedsShaderReload & gfx_state.currentRendererFlag) {
		PERFINFO_AUTO_START("rdrReloadShaders", 1);
		gfx_state.rendererNeedsShaderReload &= ~gfx_state.currentRendererFlag;
		rdrReloadShaders(gfx_state.currentDevice->rdr_device);
		PERFINFO_AUTO_STOP();
	}
	if (gfx_state.rendererNeedsAnisoReset &  gfx_state.currentRendererFlag) {
		PERFINFO_AUTO_START("texResetAnisotropicInternal", 1);
		gfx_state.rendererNeedsAnisoReset &= ~gfx_state.currentRendererFlag;
		texResetAnisotropicInternal();
		PERFINFO_AUTO_STOP();
	}

	texGenDoFrame();

	texDoQueuedFrees();
	texDoQueuedLoads();
	gfxTexturePoolsOncePerFramePerDevice();
	PERFINFO_AUTO_STOP();
}

/// Allocate a texture to fill in dynamically with the requested
/// dimensions and format.
///
/// NOTE: This does not actually allocate memory on the video card or
/// create the texture -- it just fills in all the apropriate data so
/// that you can do this later on.
BasicTexture *texAllocateScratch(const char* name, int width, int height, WLUsageFlags use)
{
	BasicTexture* accum = basicTextureCreate();

	assert( width >= 4 && height >= 4 );
	width = MAX( 4, width );
	height = MAX( 4, height );

	accum->actualTexture = accum;
	accum->name = name ? strdup( name ) : NULL;
	accum->fullname = "scratchTexture";
	accum->width = accum->realWidth = width;
	accum->height = accum->realHeight = height;
	accum->use_category = use;
	texAllocLoadedData(accum);

	// mark this texture as scratch
	accum->flags = TEX_SCRATCH;
	accum->tex_handle = rdrGenTexHandle(0);

	return accum;
}

void texStealSurfaceSnapshot(BasicTexture* bind, RdrSurface* surf, RdrSurfaceBuffer buffer_num, int snapshot_idx)
{
	rdrTexStealSnapshot(gfx_state.currentDevice->rdr_device, bind->tex_handle,
						surf, buffer_num, snapshot_idx,
						texMemMonitorNameFromFlags(bind->use_category), bind);

	// TODO: Move this to a better place.
	//
	// Technically this is a race condition.  The texture will not be
	// "loaded" until the snapshot steal actually happens.  However,
	// the only badness that could happen here is if the texture got
	// freed before the snapshot steal happened, which is very
	// unlikely, and would just cause the texture budget to
	// underreport.
	//
	// Still, this code here is a bit scary.
	bind->tex_is_loaded |= gfx_state.currentRendererFlag;
}

void texGetTexNames(const char*** peaTexNamesOut)
{
	StashTableIterator iter;
	StashElement elem;
	stashGetIterator(g_basicTextures_ht, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		BasicTexture *pTex = stashElementGetPointer(elem);
		if (pTex) 
		{
			eaPush(peaTexNamesOut, pTex->name);
		}
	}
}

