#include "GfxTextureTools.h"
#include "GfxTextures.h"
#include "texUnload.h"
#include "GraphicsLib.h"
#include "CrypticDXT.h"
#include "RdrTexture.h"
#include "DirectDrawTypes.h"
#include "gimmeDLLWrapper.h"
#include "file.h"
#include "TexOpts.h"
#include "tga.h"
#include "rand.h"
#include "logging.h"
#include "TaskProfile.h"

#include "GfxDXT.h"
#include "WritePNG.h"

#include "GfxTextureTools_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););

// Takes texopt_flags as a performance optimization, could do lookup in here instead
void makeTexOptString(char *filename, char *buf, size_t buf_size, TexOpt *texopt, TexOptFlags texopt_flags)
{
	int		len;

	buf[0] = 0;

	//if (texopt_flags & TEXOPT_FADE) {
	//	TexOpt *tex_opt = texoptFromTextureName(filename, NULL);
	//	sprintf_s(buf, buf_size,"Fade %f %f",tex_opt->texopt_fade[0],tex_opt->texopt_fade[1]);
	//}
	if (texopt_flags & TEXOPT_CUBEMAP)
		strcat_s(buf, buf_size,"CubeMap ");
	if (texopt_flags & TEXOPT_VOLUMEMAP)
		strcat_s(buf, buf_size,"Volume ");
	if (texopt_flags & TEXOPT_JPEG)
		strcat_s(buf, buf_size,"Jpeg ");
	if (texopt_flags & TEXOPT_BUMPMAP)
		strcat_s(buf, buf_size,"Bump ");
	if (texopt_flags & (TEXOPT_CLAMPS|TEXOPT_CLAMPT))
		strcat_s(buf, buf_size,"ClampST ");
	if (texopt_flags & TEXOPT_NOMIP)
		strcat_s(buf, buf_size,"NoMip ");
	if (texopt_flags & TEXOPT_FIX_ALPHA_MIPS)
		strcat_s(buf, buf_size,"FixAlphaMIPs ");
	if (texopt_flags & TEXOPT_COLORBORDER)
		strcat_s(buf, buf_size,"ColorBorder ");
	else if ((texopt_flags & TEXOPT_ALPHABORDER) == TEXOPT_ALPHABORDER)
		strcat_s(buf, buf_size,"AlphaBorder ");
	else if (texopt_flags & TEXOPT_ALPHABORDER_LR)
		strcat_s(buf, buf_size,"AlphaBorderLR ");
	else if (texopt_flags & TEXOPT_ALPHABORDER_TB)
		strcat_s(buf, buf_size,"AlphaBorderTB ");
	if (texopt_flags & TEXOPT_REVERSED_MIPS)
		strcat_s(buf, buf_size,"ReversedMIPs ");
	if (texopt_flags & TEXOPT_CRUNCH)
		strcat_s(buf, buf_size,"Crunch ");

	if (texopt_flags & TEXOPT_SPLIT)
		strcat_s(buf, buf_size,"Split ");

	switch (texoptGetQuality(texopt)) {
	xcase 	QUALITY_PRODUCTION:
		//strcat_s(buf, buf_size,"Production ");
	xcase QUALITY_LOWEST:
		strcat_s(buf, buf_size,"Lowest ");
	xcase QUALITY_MEDIUM:
		strcat_s(buf, buf_size,"Medium ");
	xcase QUALITY_HIGHEST:
		strcat_s(buf, buf_size,"Highest ");
	}

	switch (texoptGetMipFilterType(texopt)) {
	xcase MIP_KAISER:
		//strcat_s(buf, buf_size,"Kaiser ");
	xcase MIP_BOX:
		strcat_s(buf, buf_size,"Mip:Box ");
	xcase MIP_CUBIC:
		strcat_s(buf, buf_size,"Mip:Cubic ");
	xcase MIP_MITCHELL:
		strcat_s(buf, buf_size,"Mip:Mitchell ");
	}

	switch (texoptGetCompressionType(texopt, texopt_flags)) {
	xcase COMPRESSION_AUTO:
		// Nothing
	xcase COMPRESSION_DXT1:
		strcat_s(buf, buf_size,"Force DXT1 ");
	xcase COMPRESSION_DXT5:
		strcat_s(buf, buf_size,"Force DXT5 ");
	xcase COMPRESSION_HALFRES_TRUECOLOR:
		strcat_s(buf, buf_size,"Half-Res TrueColor ");
	xcase COMPRESSION_1555:
		strcat_s(buf, buf_size,"5:5:5:1 ");
	xcase COMPRESSION_DXT_IF_LARGE:
		strcat_s(buf, buf_size,"DXTifLarge ");
	xcase COMPRESSION_U8:
		strcat_s(buf, buf_size,"U8 ");
	xcase COMPRESSION_DXT5NM:
		strcat_s(buf, buf_size,"DXT5NM ");
	xcase COMPRESSION_TRUECOLOR:
		strcat_s(buf, buf_size,"TrueColor ");
	}

	len = (int)strlen(buf);
	if (len)
		buf[len-1] = 0;
}


static struct {
	TexOptFlags flag;
	char *name;
} flag_to_name[] = {
//	{TEXOPT_FADE, "TEXOPT_FADE"},
	{TEXOPT_SRGB, "TEXOPT_SRGB"},
	{TEXOPT_MAGFILTER_POINT, "TEXOPT_MAGFILTER_POINT"},
	{TEXOPT_CUBEMAP, "TEXOPT_CUBEMAP"},
	{TEXOPT_FIX_ALPHA_MIPS, "TEXOPT_FIX_ALPHA_MIPS"},
	{TEXOPT_CLAMPS, "TEXOPT_CLAMPS"},
	{TEXOPT_CLAMPT, "TEXOPT_CLAMPT"},
	{TEXOPT_MIRRORS, "TEXOPT_MIRRORS"},
	{TEXOPT_MIRRORT, "TEXOPT_MIRRORT"},
	{TEXOPT_NORMALMAP, "TEXOPT_NORMALMAP"},
	{TEXOPT_BUMPMAP, "TEXOPT_BUMPMAP"},
	{TEXOPT_ALPHABORDER_LR, "TEXOPT_ALPHABORDER_LR"},
	{TEXOPT_ALPHABORDER_TB, "TEXOPT_ALPHABORDER_TB"},
	{TEXOPT_NOMIP, "TEXOPT_NOMIP"},
	{TEXOPT_JPEG, "TEXOPT_JPEG"},
	{TEXOPT_VOLUMEMAP, "TEXOPT_VOLUMEMAP"},
	{TEXOPT_LIGHTMAP, "TEXOPT_LIGHTMAP"},
	{TEXOPT_REVERSED_MIPS, "TEXOPT_REVERSED_MIPS"},
	{TEXOPT_CRUNCH, "TEXOPT_CRUNCH"},
};

int texPrintInfo(const char *filename)
{
	FILE *f = fopen(filename, "rb");
	TextureFileHeader tfh;
	TextureFileMipHeader mh={0};
	bool hasmh=false;
	TexOptCompressionType compression_type;
	int i, j;

	if (!f) {
		printf("Error opening .wtex file : %s\nTry running gettex again after closing the game\n", filename);
		return 1;
	}
	fread(&tfh, sizeof(tfh), 1, f);
	if (ftell(f) < tfh.header_size) {
		fread(&mh, sizeof(mh), 1, f);
		hasmh=true;
	}
	fclose(f);
	printf("Texture file : %s\n", filename);
	printf(" %d x %d (%s)\n", tfh.width, tfh.height, tfh.alpha ? "ALPHA" : "solid");
	printf(" flags: %d\n", tfh.flags);
	for (i=0; i<TEXOPT_COMPRESSION_SHIFT; i++) {
		if (tfh.flags & (1<<i)) {
			bool foundit=false;
			for (j=0; j<ARRAY_SIZE(flag_to_name); j++) {
				if ((1<<i) == flag_to_name[j].flag) {
					printf("\t%s\n", flag_to_name[j].name);
					foundit=true;
				}
			}
			if (!foundit)			
				printf("\tUNKNOWN FLAG : %d\n", 1<<i);
		}
	}
	compression_type = (tfh.flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT;
	switch (compression_type) {
	xcase COMPRESSION_DXT1:
		printf("Force DXT1 Compression\n");
	xcase COMPRESSION_DXT5:
		printf("Force DXT5 Compression\n");
	xcase COMPRESSION_HALFRES_TRUECOLOR:
		printf("Half-Res TrueColor\n");
	xcase COMPRESSION_1555:
		printf("5:5:5:1\n");
	xcase COMPRESSION_DXT_IF_LARGE:
		printf("DXT if large\n");
	xcase COMPRESSION_U8:
		printf("U8\n");
	xcase COMPRESSION_DXT5NM:
		printf("DXT5NM\n");
	}

	printf("\n");
	printf("Debug info:\n");
	printf("Header size: %d\nTexture data size: %d\nLow-mip overhead: %d\n", tfh.header_size, tfh.file_size, tfh.header_size - sizeof(tfh));
	printf("Texture file version: %c%c%c\n", tfh.verpad[0], tfh.verpad[1], tfh.verpad[2]);
	printf("Texture format: %s (%d)\n", rdrTexFormatName(MakeRdrTexFormatObj(tfh.rdr_format)), tfh.rdr_format);
	if (hasmh) {
		printf("Cached mip levels:\n");
		printf("  %d x %d\n", mh.width, mh.height);
	}
	return 0;
}

void gfxGenerateRandomTexture(int size)
{
	U8 *data = calloc(size * size * 4, 1);
	int i;
	for (i=0; i<size*size*4; i+=4) {
		F32 f = randomMersennePositiveF32(NULL);
		U8 v = CLAMP((int)(f*255), 0, 254);
		data[i]=v;
		data[i+1]=v;
		data[i+2]=v;
		data[i+3]=255;
	}
	tgaSave(strdup("C:\\random.tga"), data, size, size, 3);
}

// unmaintained hack for fixing some textures referenced by splats
void texAddTexOptFlag(BasicTexture *tex, TexOptFlags flag)
{
	char path[MAX_PATH];
	TexOptFlags texopt_flags;
	TexOpt glob_texopt = {0};
	TexOpt *texopt;
	TexOpt **eatemp=NULL;
	char srcpath[MAX_PATH];

	changeFileExt(texFindFullPath(tex), ".TexOpt", path);
	if (fileExists(path)) {
		if (fileIsReadOnly(path))
			gimmeDLLDoOperation(path, GIMME_CHECKOUT, 0);
	}
	fileLocateWrite(path, srcpath);
	//assert(!strstri(srcpath, "core"));
	strstriReplace(srcpath, "/data/", "/src/");
	changeFileExt(srcpath, ".tga", srcpath);
	if (fileIsReadOnly(srcpath) || !fileExists(srcpath))
		gimmeDLLDoOperation(srcpath, GIMME_CHECKOUT, 0);
	assert(fileExists(srcpath));

	texopt = texoptFromTextureName(tex->name, &texopt_flags);
	if (texopt)
		glob_texopt = *texopt;
	texopt_flags |= flag;
	assert((texopt_flags & TEXOPT_CLAMPS) && (texopt_flags & TEXOPT_CLAMPT));

	glob_texopt.flags = texopt_flags;
	// Clear the flags set implicitly
	glob_texopt.flags &= ~(TEXOPT_COMPRESSION_MASK);
	// Clear the flags not written to disk
	glob_texopt.flags &= ~(TEXOPT_NORMALMAP | TEXOPT_BUMPMAP);

	eaPush(&eatemp, &glob_texopt);
	ParserWriteTextFile(path, parse_TexOptList, &eatemp, 0, 0);
	eaDestroy(&eatemp);
}

// Send in an uncompressed buffer and have this function compress and write to a wtex file.
// destFormat supports: RTEX_DXT1, RTEX_DXT5, RTEX_BGRA_U8
bool gfxTexWriteWtex(char* filedata, int width, int height, const char* filename, RdrTexFormat destFormat, TexOptFlags tex_flags)
{
	return texWriteWtex(filedata, width, height, 1.0, filename, destFormat, tex_flags, QUALITY_PRODUCTION, NULL);
}

typedef enum PNGSaveMethod
{
	kPNGSaveMethod_File,
	kPNGSaveMethod_StuffBuff,
} PNGSaveMethod;

static bool gfxSaveTextureAsPNGEx(const char *texName, PNGSaveMethod method, void *pvMethodData, bool invertVerticalAxis, SA_PRE_OP_VALID TaskProfile *saveProfile)
{
	BasicTexture *tex = texLoadRawData(texName, TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_NOTSURE);
	BasicTextureRareData *rareData = tex ? texGetRareData(tex) : NULL;
	TexReadInfo *readInfo = rareData ? rareData->bt_rawInfo : NULL;
	int pngPixelSize = -1;
	bool doNxDecompression = false;
	bool texIsBGRA = false;
	bool bSaveSuccess = true;

	if(strEndsWith(texName, "_Nx")){
		doNxDecompression = true;
	}

	if(readInfo)
	{
		if (saveProfile)
		{
			taskStartTimer(saveProfile);
			taskAttributeReadIO(saveProfile, readInfo->size);
		}
		uncompressRawTexInfo(readInfo,textureMipsReversed(tex));

		if(readInfo->tex_format == RTEX_BGRA_U8) {
			texIsBGRA = true;

			pngPixelSize = 4;

		} else if (readInfo->tex_format == RTEX_BGR_U8) {
			texIsBGRA = true;

			pngPixelSize = 3;

		} else {
			log_printf(LOG_ERRORS, "Unexpected texture format in \"%s\", 0x%x\n", texName, readInfo->tex_format);

			if (saveProfile)
				taskStopTimer(saveProfile);
			// Unknown texture format.
			return false;
		}

		switch(method)
		{
			case kPNGSaveMethod_File:
				bSaveSuccess = WritePNG_FileEx(
					readInfo->texture_data,
					readInfo->width,
					readInfo->height,
					readInfo->width, pngPixelSize,
					(const char *)pvMethodData /* fname */,
					invertVerticalAxis, texIsBGRA, doNxDecompression);
				break;

			case kPNGSaveMethod_StuffBuff:
				bSaveSuccess = WritePNG_StuffBuff(
					readInfo->texture_data,
					readInfo->width,
					readInfo->height,
					readInfo->width, pngPixelSize,
					(StuffBuff *)pvMethodData,
					invertVerticalAxis, texIsBGRA, doNxDecompression);
				break;

			default:
				log_printf(LOG_ERRORS, "Unknown PNG save method requested: %d\n", method);
				break;
		}

		if (saveProfile)
		{
			taskStopTimer(saveProfile);
			if (method == kPNGSaveMethod_File)
				taskAttributeWriteIO(saveProfile, fileSize((const char *)pvMethodData));
		}
	}
	else
	{
		log_printf(LOG_ERRORS, "Cannot decompress and save texture \"%s\", might be unloading?\n", texName);
		return false;
	}

	if(tex) {
		TexUnloadMode push_dynamicUnload = texDynamicUnloadEnabled();

		texDynamicUnload(TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL_RAW);

		texUnloadRawData(tex);
		texUnloadTexturesToFitMemory();

		texDynamicUnload(push_dynamicUnload);
	}

	return bSaveSuccess;
}

bool gfxSaveTextureAsPNG(const char *texName, const char *fname, bool invertVerticalAxis, TaskProfile *saveProfile)
{
	return gfxSaveTextureAsPNGEx(texName, kPNGSaveMethod_File, (void *)fname, invertVerticalAxis, saveProfile);
}

bool gfxSaveTextureAsPNG_StuffBuff(const char *texName, StuffBuff *psb, bool invertVerticalAxis)
{
	return gfxSaveTextureAsPNGEx(texName, kPNGSaveMethod_StuffBuff, psb, invertVerticalAxis, NULL);
}

// End of File


