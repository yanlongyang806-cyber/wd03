#include "wlModelBinning.h"
#include "wlModelBinningPrivate.h"
#include "wlModelBinningLOD.h"
#include "wlAutoLOD.h"
#include "wlModelInline.h"
#include "wlState.h"
#include "PhysicsSDK.h"

#include "serialize.h"
#include "structInternals.h"
#include "strings_opt.h"
#include "StringCache.h"
#include "mutex.h"
#include "GenericMesh.h"
#include "ScratchStack.h"
#include "zutils.h"
#include "timing.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "utilitiesLib.h"
#include "logging.h"
#include "crypt.h"
#include "ControllerScriptingSupport.h"

static const float POSITION_SCALE = 32768.f;
static const float NORMAL_SCALE = 256.f;
static const float TEX_SCALE = 32768.f;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc);); // Should be 0 after startup

F32 g_bin_timing_lods;
F32 g_bin_timing_cooking;
const char *g_debug_write_collision_data;

const char *modelSourceGetName(ModelSource *model) { return model->name; }
bool modelSourceHasVerts2(ModelSource *model) { return !!model->pack.verts2.unpacksize; }
int modelSourceGetTricount(ModelSource *model) { return model->tri_count; }
F32 modelSourceGetRadius(ModelSource *model) { return model->radius; }
F32 *modelSourceGetAutoLODDists(ModelSource *model) { return model->autolod_dists; }
int modelSourceGetProcessTimeFlags(ModelSource *model) { return model->process_time_flags; }
static void geo2OptimizeModel(ModelSource *model);
void optimizeForDeltaCompression(Vec3 *verts, int vert_count, IVec3 *tris, int tri_count);

static void modelCheckTangentBases(ModelSource *srcmodel, const char *checkStage);
static void geo2CheckTangentBases(Geo2LoadData * gld);

void endianSwapArray(void *datap, int count, PackType type)
{
	if (isBigEndian() && type != PACK_U8)
	{
		int i;

		if (type == PACK_U16)
		{
			U16 *data = datap;
			for (i=0; i<count; i++, data++)
				*data = endianSwapU16(*data);
		}
		else
		{
			U32 *data = datap;
			for (i=0; i<count; i++, data++)
				*data = endianSwapU32(*data);
		}
	}
}

static StashTable stModelLODInfoOverrides;
void modelBinningLODInfoOverride(const char *geoName, const char *modelname, ModelLODInfo *lod_info)
{
	StashTable stSub;
	char buf[MAX_PATH];
	getFileNameNoExt(buf, geoName);
	geoName = allocAddFilename(buf);
	sprintf(buf, "%s.", geoName);
	if (strStartsWith(modelname, buf))
		modelname = allocAddString(modelname + strlen(buf));
	assert(modelname == allocFindString(modelname)); // Should be stringpooled already
	if (!stModelLODInfoOverrides)
		stModelLODInfoOverrides = stashTableCreateWithStringKeys(4, StashDefault);
	if (!stashFindPointer(stModelLODInfoOverrides, geoName, &stSub))
	{
		stSub = stashTableCreateWithStringKeys(4, StashDefault);
		stashAddPointer(stModelLODInfoOverrides, geoName, stSub, true);
	}
	if (!lod_info)
	{
		stashRemovePointer(stSub, modelname, NULL);
		if (stashGetCount(stSub)==0) {
			stashTableDestroy(stSub);
			stashRemovePointer(stModelLODInfoOverrides, geoName, NULL);
		}
	} else {
		stashAddPointer(stSub, modelname, lod_info, true);
	}
}


static void *memNext(U8 *mem, int *mem_pos, int add, int headersize)
{
	if (*mem_pos + add <= headersize)
	{
		*mem_pos += add;
		return &mem[*mem_pos - add];
	}

	assert(0);
	return NULL;
}

static int memInt(U8 *mem, int *mem_pos, int headersize)
{
	int *iptr = memNext(mem, mem_pos, sizeof(int), headersize);
	if (iptr) {
		int v = endianSwapIfBig(U32, *iptr);
		return v;
	}
	return 0;
}

static void unpackNames(void *mem,PackNames *names)
{
	int		i,count;
	int		*imem,*idxs;
	char	**idxs_ptr,*base_offset;

	imem = (void *)mem;
	count = endianSwapIfBig(U32, imem[0]);
	names->count = count;
	idxs = &imem[1];
	idxs_ptr = (void *)idxs;
	names->strings = idxs_ptr;
	base_offset = (char *)(idxs + count);
	for(i=0;i<count;i++)
		idxs_ptr[i] = base_offset + endianSwapIfBig(U32, idxs[i]);
}

SA_RET_NN_VALID static ModelSource **allocModelList(int count, Geo2LoadData *gld)
{
	ModelSource **ret=0;
	ModelSource *rawData;
	int i;

	eaCreate(&ret);
	rawData = calloc(sizeof(ModelSource)*count, 1);
	eaPush(&gld->allocList, rawData);
	for (i=0; i<count; i++) 
		eaPush(&ret, &rawData[i]);
	return ret;
}

static int __cdecl compareModelNames(const ModelSource** m1, const ModelSource** m2)
{
	return stricmp((*m1)->name, (*m2)->name);
}

static void geo2SortModels(Geo2LoadData* gld)
{

	qsort(	gld->models,
			gld->model_count,
			sizeof(gld->models[0]),
			compareModelNames);
}


static int readPackData2(PackData *pdata, int *data)
{
	pdata->packsize = endianSwapIfBig(S32, data[0]);
	pdata->unpacksize = endianSwapIfBig(S32, data[1]);
	pdata->data_offs = endianSwapIfBig(U32, data[2]);
	return 3*sizeof(int);
}

static int readModel2(ModelSource *dst, char *data, int version_num, U8 *objname_base, U8 *texidx_base)
{
	int offset;
	int size = 0;
	int i;

#define COPY(fld,type) { memcpy(&(dst->fld), data, sizeof(type)); data += sizeof(type); dst->fld = endianSwapIfBig(type, (dst->fld)); }
#define COPY2(fld,type) { memcpy(&fld, data, sizeof(type)); data += sizeof(type);  fld = endianSwapIfBig(type, fld); }

	COPY2(size, U32);
	
	COPY(radius, F32);

	COPY(tex_count, U32);

	COPY(vert_count, U32);
	COPY(tri_count, U32);

	COPY2(offset, U32);
	dst->tex_idx = (TexID*)(texidx_base + offset);
	for (i=0; i<dst->tex_count; i++) {
		dst->tex_idx[i].id = endianSwapIfBig(U16, dst->tex_idx[i].id);
		dst->tex_idx[i].count = endianSwapIfBig(U16, dst->tex_idx[i].count);
	}

	COPY2(offset, U32);
	dst->name = objname_base + offset;

#pragma warning(disable : 6385)
	COPY(min[0], F32);
	COPY(min[1], F32);
	COPY(min[2], F32);
	COPY(max[0], F32);
	COPY(max[1], F32);
	COPY(max[2], F32);
#pragma warning(default : 6385)

	centerVec3(dst->min, dst->max, dst->mid);

	data += readPackData2(&dst->pack.tris, (int *)data);
	data += readPackData2(&dst->pack.verts, (int *)data);
	data += readPackData2(&dst->pack.norms, (int *)data);
	data += readPackData2(&dst->pack.binorms, (int *)data);
	data += readPackData2(&dst->pack.tangents, (int *)data);
	data += readPackData2(&dst->pack.sts, (int *)data);
	data += readPackData2(&dst->pack.sts3, (int *)data);
	data += readPackData2(&dst->pack.colors, (int *)data);
	data += readPackData2(&dst->pack.weights, (int *)data);
	data += readPackData2(&dst->pack.matidxs, (int *)data);
	data += 3*sizeof(int); // skip old mesh reduction data
	data += readPackData2(&dst->pack.verts2, (int *)data);
	data += readPackData2(&dst->pack.norms2, (int *)data);

	COPY(process_time_flags, U32);
	COPY(lightmap_size, F32);
#pragma warning(disable : 6385)
	COPY(autolod_dists[0], F32);
	COPY(autolod_dists[1], F32);
	COPY(autolod_dists[2], F32);
#pragma warning(default : 6385)

#undef COPY
#undef COPY2

	dst->lightmap_tex_size = pow2((U32)dst->lightmap_size);

	return size;

}

static ModelLODInfo *modelGetLODInfo(ModelSource *model, const char *geoFilename, bool *isOverride)
{
	char buf[MAX_PATH];
	StashTable stSub;
	ModelLODInfo *lod_info_override = NULL;
	ModelLODInfo *lod_info;

	getFileNameNoExt(buf, geoFilename);
	stSub = stashFindPointerReturnPointer(stModelLODInfoOverrides, buf);
	if (stSub) {
		lod_info_override = stashFindPointerReturnPointer(stSub, model->name);
		if (lod_info_override)
			lod_info_override->is_in_dictionary = 1; // Don't free me!
	}

	// Warning: this could only happen in development, while editing, but this
	//  could theoretically crash if the main thread is reloading or changing
	//  this LODInfo at the same time as it's being duplicate here.  Fix it if
	//  it happens by synchronizing this action and the reload actions.
	lod_info = lod_info_override?lod_info_override:lodinfoFromModelSource(model, geoFilename);
	assert(lod_info);

	if (isOverride)
		*isOverride = !!lod_info_override;

	return lod_info;
}

static bool modelInitLODs2(ModelSource *model, const char *geoFilename, FileList *file_list)
{
	ModelLODInfo *lod_info;
	bool has_lod_override;
	int i;

	lod_info = modelGetLODInfo(model, geoFilename, &has_lod_override);

	if (file_list)
	{
		FileListInsert(file_list, has_lod_override?"NonExistentFile":lod_info->parsed_filename, has_lod_override?0x7fffffff:fileLastChanged(lod_info->parsed_filename));

		if (!has_lod_override && (lod_info->is_automatic || lod_info->force_auto) && strstri(geoFilename, "character_library/"))
			FileListInsert(file_list, CHARLIB_LOD_SCALE_FILENAME, fileLastChanged(CHARLIB_LOD_SCALE_FILENAME));
	}

	model->high_detail_high_lod = lod_info->high_detail_high_lod;
	for (i = 0; i < eaSize(&lod_info->lods); i++)
	{
		AutoLOD *lod = lod_info->lods[i];
		// setup convenience runtime bools - this is wasteful and error-prone
		lod->modelname_specified = !!lod->lod_modelname;
		lod->null_model = (lod->flags == LOD_ERROR_NULL_MODEL);
		lod->do_remesh = (lod->flags == LOD_ERROR_REMESH);
		if (lod_info->is_in_dictionary)
			eaPush(&model->lods, StructClone(parse_AutoLOD, lod));
		else
			eaPush(&model->lods, lod);
	}
	if (!lod_info->is_in_dictionary) {
		eaDestroy(&lod_info->lods);
		free(lod_info);
	}

	return true;
}


#define MIN_VERSION 17
#define MAX_VERSION 17
static Geo2LoadData *geo2LoadModelHeaders(FILE *file, Geo2LoadData *gld, FileList *file_list)
{
	int		i=0,j,ziplen;
	U8		*mem,*zipmem,*texidx_offset;
	char	*aps;
	ModelSource	*model;
	int		texname_blocksize,objname_blocksize,texidx_blocksize;
	char	*objnames;
	int		version_num = 0;
	int		mem_pos, oziplen;
	int		totalSize;
	void	*lod_data = 0;

	int		gldhSize;

		//read the size of the header, then read the header into memory
		if(!fread(&ziplen,1,4,file))
		{
			ErrorFilenamef(gld->filename, "Bad file size.");
			return NULL;
		}
		ziplen = endianSwapIfBig(U32, ziplen);
		oziplen = ziplen;

		fread(&version_num,4,1,file);
		version_num = endianSwapIfBig(U32, version_num);
		if (version_num < MIN_VERSION || version_num > MAX_VERSION)
		{
			ErrorFilenamef(gld->filename, "Bad version: %d", version_num);
			return NULL;
		}

		ziplen -= 8; // was biased to include sizeof(gld->headersize) + sizeof(version_num) so pigg system would cache it
		fread(&gld->headersize,1,4,file);
		gld->headersize= endianSwapIfBig(U32, gld->headersize);

		gld->file_format_version = version_num;

		gldhSize = sizeOfVersion16GeoLoadDataHeader;  // just a model count now

		gld->header_data = mem = malloc(gld->headersize);
		assert(mem);
		zipmem = _alloca(ziplen);
		assert(zipmem);
		fread(zipmem,1,ziplen,file);
		EnterCriticalSection(&model_unpack_cs);
		in_model_unpack_cs++;
		geoUncompress(mem,gld->headersize,zipmem,ziplen,0,gld->filename, NULL);
		in_model_unpack_cs--;
		LeaveCriticalSection(&model_unpack_cs);

		gld->data_offset = oziplen + 4;

// these two macros automate the state changes in `mem' and `mem_pos'
#define MEM_NEXT(add) memNext(mem, &mem_pos, add, gld->headersize)
#define MEM_INT() memInt(mem, &mem_pos, gld->headersize)

		//2 ####### Unpack the animlist
		mem_pos = 0;

		gld->datasize			= MEM_INT();

		texname_blocksize		= MEM_INT();

		objname_blocksize		= MEM_INT();

		texidx_blocksize		= MEM_INT();

		totalSize =	mem_pos +	
					texname_blocksize +
					objname_blocksize +
					texidx_blocksize;

		totalSize += gldhSize;
		
		mem = malloc(totalSize);
		eaPush(&gld->allocList, mem);
		
		memcpy(mem, gld->header_data, totalSize);

		unpackNames(MEM_NEXT(texname_blocksize), &gld->texnames);

		objnames = (void *)MEM_NEXT(objname_blocksize);

		texidx_offset = MEM_NEXT(texidx_blocksize);

		MEM_NEXT(gldhSize);
		
		assert(mem_pos == totalSize);
		
		aps = (void*)((U8*)gld->header_data + mem_pos);

		// Versions 9 and up, skip the name field on versions 9-15
		gld->model_count = endianSwapIfBig(U32,  *(int*)((U8*)mem + mem_pos - 4));

		if (gld->model_count)
		{
			gld->models = allocModelList(gld->model_count, gld);
			eaPush(&gld->eaAllocList, &gld->models);
			gld->model_data = gld->models[0];

			for( j=0 ; j < gld->model_count ; j++ )
			{
				model = gld->models[j]; 
				model->gld = gld;
				aps += readModel2(model, aps, gld->file_format_version, (U8*)objnames, (U8*)texidx_offset);

				//model->header = RefSystem_ReferentFromString(hModelHeaderDict, model->name);
				//if (!model->header)
				//{
				//	// It's possible we couldn't find it because it's a character library piece, which is referenced by geometry name
				//	const char* pcLookupName = wlCharacterModelKey(filename, model->name);
				//	model->header = RefSystem_ReferentFromString(hModelHeaderDict, pcLookupName);
				//}

				//if (!model->header)
				//{
				//	ErrorFilenamef(filename, "Can't find " MODELHEADER_EXTENSION " for model %s! Please check that it's been updated with the new GetVRML (version 14 or later).", model->name);
				//	if (!bGlobalDummyHeaderInit)
				//	{
				//		globalDummyHeader.modelname = allocAddString("DummyModelHeader");
				//		globalDummyHeader.filename = allocAddString("DummyModelFile");
				//		bGlobalDummyHeaderInit = true;
				//	}
				//	model->header = &globalDummyHeader;
				//}
			}
		}
		else
		{
			gld->models = NULL;
			gld->model_data = NULL;
		}

		// Discard ModelFormatOnDisk data as it's been copied into newly alloced structures
		free(gld->header_data);
		gld->header_data = mem;

		// TODO: Do we need the flags and materials set by this function to generate LODs or anything like that?
		//if( gld->model_count )
		//	geoAddModelData(gld, true);


		// Sort models by name for quick lookup.

		geo2SortModels(gld);

		// must be done after the model headers have been filled in and sorted
		for (j = 0; j < gld->model_count; j++)
		{
			model = gld->models[j];
			modelInitLODs2(model, gld->filename, file_list);
		}

	return gld;
}

static void patchPackPtr(PackData *pack, U8 *base)
{
	if (pack->unpacksize)
		pack->data_ptr = pack->data_offs + base;
}

static void geo2UnpackAll(ModelSource *model, const char *filename)
{
	void *mem;
	int i;

	EnterCriticalSection(&model_unpack_cs);
	in_model_unpack_cs++;

#define mallocWrap(bytes) ((mem = malloc(bytes)), eaPush(&model->gld->allocList, mem), mem)
	if (!model->unpack.tris)
	{
		model->unpack.tris = mallocWrap(sizeof(*model->unpack.tris) * 3 * model->tri_count);
		geoUnpackDeltas(&model->pack.tris,model->unpack.tris,3,model->tri_count,PACK_U32,model->name,filename, NULL);
	}

	if (!model->unpack.verts)
	{
		model->unpack.verts = mallocWrap(sizeof(F32) * 3 * model->vert_count);
		geoUnpackDeltas(&model->pack.verts,model->unpack.verts,3,model->vert_count,PACK_F32,model->name,filename, NULL);
	}

	if (!model->unpack.norms && model->pack.norms.unpacksize)
	{
		model->unpack.norms = mallocWrap(sizeof(F32) * 3 * model->vert_count);
		geoUnpackDeltas(&model->pack.norms,model->unpack.norms,3,model->vert_count,PACK_F32,model->name,filename, NULL);
	}

	if (!model->unpack.binorms && model->pack.binorms.unpacksize)
	{
		model->unpack.binorms = mallocWrap(sizeof(F32) * 3 * model->vert_count);
		geoUnpackDeltas(&model->pack.binorms,model->unpack.binorms,3,model->vert_count,PACK_F32,model->name,filename, NULL);
	}

	if (!model->unpack.tangents && model->pack.tangents.unpacksize)
	{
		model->unpack.tangents = mallocWrap(sizeof(F32) * 3 * model->vert_count);
		geoUnpackDeltas(&model->pack.tangents,model->unpack.tangents,3,model->vert_count,PACK_F32,model->name,filename, NULL);
	}

	if (!model->unpack.sts && model->pack.sts.unpacksize)
	{
		model->unpack.sts = mallocWrap(sizeof(F32) * 2 * model->vert_count);
		geoUnpackDeltas(&model->pack.sts,model->unpack.sts,2,model->vert_count,PACK_F32,model->name,filename, NULL);
		for (i = 0; i < model->vert_count; ++i)
			model->unpack.sts[i][1] = 1 - model->unpack.sts[i][1];
	}

	if (!model->unpack.sts3 && model->pack.sts3.unpacksize)
	{
		model->unpack.sts3 = mallocWrap(sizeof(F32) * 2 * model->vert_count);
		geoUnpackDeltas(&model->pack.sts3,model->unpack.sts3,2,model->vert_count,PACK_F32,model->name,filename, NULL);
		for (i = 0; i < model->vert_count; ++i)
			model->unpack.sts3[i][1] = 1 - model->unpack.sts3[i][1];
	}

	if (!model->unpack.colors && model->pack.colors.unpacksize)
	{
		model->unpack.colors = mallocWrap(sizeof(U8) * 4 * model->vert_count);
		geoUnpack(&model->pack.colors,model->unpack.colors,model->name,filename, NULL);
		// TODO: Endian swap colors?
	}

	if (!model->unpack.matidxs && model->pack.matidxs.unpacksize)
	{
		model->unpack.matidxs = mallocWrap(sizeof(U8) * 4 * model->vert_count);
		geoUnpack(&model->pack.matidxs,model->unpack.matidxs,model->name,filename, NULL);
	}

	if (!model->unpack.weights && model->pack.weights.unpacksize)
	{
		model->unpack.weights = mallocWrap(sizeof(U8) * 4 * model->vert_count);
		geoUnpack(&model->pack.weights,model->unpack.weights,model->name,filename, NULL);
		for (i = 0; i < model->vert_count; i++)
			model->unpack.weights[i*4] = 255 - model->unpack.weights[i*4+1] - model->unpack.weights[i*4+2] - model->unpack.weights[i*4+3];
	}

	if (!model->unpack.verts2 && model->pack.verts2.unpacksize)
	{
		model->unpack.verts2 = mallocWrap(sizeof(F32) * 3 * model->vert_count);
		geoUnpackDeltas(&model->pack.verts2,model->unpack.verts2,3,model->vert_count,PACK_F32,model->name,filename, NULL);
	}

	if (!model->unpack.norms2 && model->pack.norms2.unpacksize)
	{
		model->unpack.norms2 = mallocWrap(sizeof(F32) * 3 * model->vert_count);
		geoUnpackDeltas(&model->pack.norms2,model->unpack.norms2,3,model->vert_count,PACK_F32,model->name,filename, NULL);
	}
#undef mallocWrap

	in_model_unpack_cs--;
	LeaveCriticalSection(&model_unpack_cs);
}

static void geo2Pack(PackData *pack, void *delta_data, int delta_len, void *orig_data, int orig_len, Geo2LoadData *gld)
{
	int		len, ziplen, unpacksize;
	U8		*zip_buf, *data;
	bool	use_orig_zip = false, use_orig_data = false;

	if (!delta_data)
		return;

	data = delta_data;
	len = delta_len;

	zip_buf = zipData(delta_data, delta_len, &ziplen);
	unpacksize = delta_len;

	if (orig_data)
	{
		int orig_ziplen;
		U8 *orig_zip_buf = zipData(orig_data, orig_len, &orig_ziplen);
		if (orig_ziplen < ziplen)
		{
			// zipped data is smaller than RLE compressed zipped data
			free(zip_buf);
			zip_buf = orig_zip_buf;
			ziplen = orig_ziplen;
			unpacksize = orig_len;
			use_orig_zip = true;
		}
		else
		{
			free(orig_zip_buf);
		}

		if (orig_len < delta_len)
		{
			// unzipped data is smaller than RLE compressed unzipped data
			use_orig_data = true;
			len = orig_len;
			data = orig_data;
		}
	}

	if (ziplen*50 <= len*45 && (len - ziplen) > 100) // ziplen <= len * 0.9
	{
		// zipping is big enough gain, use zipped data
		pack->data_ptr = realloc(zip_buf, ziplen);
		use_orig_data = false;
	}
	else
	{
		// zipping is not a big enough win, use either RLE compressed data or uncompressed data (whichever is smaller)
		pack->data_ptr = memdup(data, len);
		ziplen = 0;
		unpacksize = len;
		use_orig_zip = false;
		free(zip_buf);
	}
	eaPush(&gld->allocList, pack->data_ptr);

	pack->packsize		= ziplen;
	pack->unpacksize	= (use_orig_zip || use_orig_data) ? -unpacksize : unpacksize;
}

//#define WL_DELTAS_DEBUG

#ifdef WL_DELTAS_DEBUG
static FILE *wdFileBits = NULL;
static FILE *wdFileBytes = NULL;
char *wdContentsBits = NULL;
char *wdContentsBytes = NULL;
const char *wdContentsBitsCursor = NULL;
const char *wdContentsBytesCursor = NULL;
int wdContentsLen = 0;
int wdCount = 0;
#endif

#ifdef WL_DELTAS_DEBUG
#pragma optimize("", off)
static void updateLast(int diff, F32 inv_float_scale, F32 *last)
{
	*last = diff * inv_float_scale + *last;
}
#pragma optimize("", on)


#pragma optimize("", off)
static int calcVal(F32 delta, F32 float_scale, int offset)
{
	return delta * float_scale + offset;
}
#pragma optimize("", on)
#endif

// Do not allow VS to optimize this, as the functions below will create inconsistent data between D and FD.
U8 *wlCompressDeltas(const void *data, int *length, int stride, int count, PackType pack_type, F32 float_scale, F32 inv_float_scale)
{
	const int *iPtr = data;
	const U16 *siPtr = data;
	const U8 *biPtr = data;
	const F32 *fPtr = data;
	int i,j,k,t,val8,val16,val32,iDelta,val,code,cur_byte=0,cur_bit=0,bit_bytes;
	Vec4 fLast = {0,0,0,0};
	IVec4 iLast = {0,0,0,0};
	U8 *packed, *bits, *bytes;
	F32 fDelta=0;

	*length = 0;
	if (!data || !count)
		return 0;

#ifdef WL_DELTAS_DEBUG
	{
		char filename[MAX_PATH];
		sprintf(filename, "%s\\deltas-bits-%d.txt", fileTempDir(), wdCount);
		wdCount++;
		if(fileExists(filename))
		{
			wdContentsBits = fileAlloc(filename, &wdContentsLen);
			sprintf(filename, "%s\\deltas-bytes-%d.txt", fileTempDir(), wdCount);
			wdContentsBytes = fileAlloc(filename, &wdContentsLen);

			wdContentsBitsCursor = wdContentsBits;
			wdContentsBytesCursor = wdContentsBytes;
		}
		else
		{
			wdFileBits = fopen(filename, "wb");
			sprintf(filename, "%s\\deltas-bytes-%d.txt", fileTempDir(), wdCount);
			wdFileBytes = fopen(filename, "wb");
		}
	}
#endif

	bits = calloc((2 * count * stride + 7)/8,1);
	bytes = calloc(count * stride * 4 + 1,1); // Add 1 for the float_scale!

	bytes[cur_byte++] = log2((int)float_scale);
	assert((1 << bytes[0]) == (int)float_scale);

	devassert(stride<=ARRAY_SIZE(fLast));
	devassert(stride<=ARRAY_SIZE(iLast));

	for(i=0;i<count;i++)
	{
		for(j=0;j<stride;j++)
		{
			if (pack_type == PACK_F32)
			{
				fDelta = *fPtr++ - fLast[j];
				val8 = fDelta * float_scale + 0x7f;
				val16 = fDelta * float_scale + 0x7fff;
#ifdef WL_DELTAS_DEBUG
				devassert(calcVal(fDelta, float_scale, 0x7f)==val8);
				devassert(calcVal(fDelta, float_scale, 0x7fff)==val16);
#endif
				val32 = *((int *)&fDelta);
			}
			else
			{
				if (pack_type == PACK_U32)
					t = *iPtr++;
				else if (pack_type == PACK_U16)
					t = *siPtr++;
				else if (pack_type == PACK_U8)
					t = *biPtr++;
				iDelta = t - iLast[j] - 1;
				iLast[j] = t;
				val8 = iDelta + 0x7f;
				val16 = iDelta + 0x7fff;
				val32 = iDelta;
			}
			if (val8 == 0x7f)
			{
				code	= 0;
			}
			else if ((val8 & ~0xff) == 0)
			{
				int diff8 = val8 - 0x7f;

				code	= 1;
				val		= val8;
				fLast[j] = diff8 * inv_float_scale + fLast[j];
				//updateLast(diff8, inv_float_scale, &fLast[j]);
			}
			else if ((val16 & ~0xffff) == 0)
			{
				int diff16 = val16 - 0x7fff;

				code	= 2;
				val		= val16;
				fLast[j] = diff16 * inv_float_scale + fLast[j];
				//updateLast(diff16, inv_float_scale, &fLast[j]);
			}
			else
			{
				code	= 3;
				val		= val32;
				fLast[j]= fDelta + fLast[j];
			}

#ifdef WL_DELTAS_DEBUG
			if(!wdFileBits && !wdFileBytes)
			{
				int cur_bit_byte = cur_bit>>3;
				for(k=0; k<byte_count_code[code]; k++)
				{
					if(wdContentsBytes[cur_byte+k]!=(char)((val>>k*8)&0xFF))
						printf("");
				}
				if(cur_bit_byte>0)
				{
					if(wdContentsBits[cur_bit_byte-1]!=(char)bits[cur_bit_byte-1])
						printf("");
				}
			}
#endif

			devassert(code>=0 && code<=3);
			devassert((cur_bit>>3)>=0 && (cur_bit>>3)<(2*count*stride+7)/8);
			bits[cur_bit >> 3] |= code << (cur_bit & 7);
			devassert(cur_byte>=0 && cur_byte<count*stride*4+1);
			for(k=0;k<byte_count_code[code];k++)
				bytes[cur_byte++] = (val >> k*8) & 255;
			cur_bit+=2;
		}
	}

	bit_bytes = (cur_bit+7)/8;
	packed = calloc(1, bit_bytes + cur_byte);

#ifdef WL_DELTAS_DEBUG
	if(wdFileBits && wdFileBytes)
	{
		fwrite(bits, 1, bit_bytes, wdFileBits);
		fwrite(bytes, 1, cur_byte, wdFileBytes);
	}
#endif

	devassert(bit_bytes>=0 && bit_bytes<=(2*count*stride+7)/8);
	memcpy(packed,bits,bit_bytes);
	devassert(cur_byte>=0 && cur_byte<=4*count*stride+1);
	memcpy(packed+bit_bytes,bytes,cur_byte);
	free(bits);
	free(bytes);
	*length = bit_bytes + cur_byte;

	if (gbMakeBinsAndExit || wl_state.binAllGeos || wl_state.verifyAllGeoBins)
	{
		// Make sure it unpacks correctly
		if (pack_type == PACK_F32)
		{
			F32 *outdata = calloc(count*stride, sizeof(F32));
			wlUncompressDeltas(outdata, packed, stride, count, PACK_F32);
			for (i=0; i<count*stride; i++)
			{
				if (wl_state.checkForCorruptLODs)
				{
					assert(nearSameF32Tol(outdata[i], ((F32*)data)[i], 0.01f));
					//assert(outdata[i] != -65536.0);
					//assert(outdata[i] != 65536.0);
					assert(outdata[i] != -8388608.0);
				}
			}
			free(outdata);
		}
	}

	return packed;
}

__forceinline static F32 quantF32(F32 val,F32 float_scale,F32 inv_float_scale)
{
	int		ival;
	F32		outval;

	assert(FINITE(val));

	ival = val * float_scale;
	ival &= ~1;
	outval = ival * inv_float_scale;
	return outval;
}

__forceinline static F32 quantF16(F32 val)
{
	return F16toF32(F32toF16(val));
}

static void geo2PackTerrainPosDeltas(PackData *pack, void *data, int vert_count, Geo2LoadData *gld)
{
	void	*deltas;
	int		delta_len, len;

	if (!data)
		return;

	{
		F32 *fdata = data;
		int i;
		for (i = 0; i < vert_count * 3; i += 3)
		{
			fdata[i+0] = quantF16(fdata[i+0]);
			fdata[i+1] = quantF32(fdata[i+1], POSITION_SCALE, 1.0f / POSITION_SCALE);
			fdata[i+2] = quantF16(fdata[i+2]);
		}
	}

	len = vert_count * 3 * sizeof(F32);

	deltas	= wlCompressDeltas(data, &delta_len, 3, vert_count, PACK_F32, POSITION_SCALE, 1.0f / POSITION_SCALE);
	endianSwapArray(data, 3 * vert_count, PACK_F32); // if big endian, swap to little endian
	geo2Pack(pack, deltas, delta_len, data, len, gld);
	endianSwapArray(data, 3 * vert_count, PACK_F32); // restore to big endian
	free(deltas);
}

static void geo2PackDeltas(PackData *pack, void *data, int stride, int vert_count, PackType pack_type, F32 float_scale, Geo2LoadData *gld)
{
	F32		inv_float_scale = 1;
	void	*deltas;
	int		delta_len, len;

	if (!data)
		return;

	if (!float_scale)
		float_scale = 1;
	inv_float_scale = 1.f / float_scale;

	if (pack_type == PACK_F32 && float_scale)
	{
		F32 *fdata = data;
		int i;
		for (i = 0; i < stride*vert_count; ++i)
			fdata[i] = quantF32(fdata[i], float_scale, inv_float_scale);
	}

	switch (pack_type)
	{
		xcase PACK_F32:
			len = vert_count * stride * sizeof(F32);

		xcase PACK_U32:
			len = vert_count * stride * sizeof(U32);

		xcase PACK_U16:
			len = vert_count * stride * sizeof(U16);

		xcase PACK_U8:
			len = vert_count * stride * sizeof(U8);

		xdefault:
			assertmsg(0, "Unknown pack type.");
	}

	deltas	= wlCompressDeltas(data, &delta_len, stride, vert_count, pack_type, float_scale, inv_float_scale);
	endianSwapArray(data, stride * vert_count, pack_type); // if big endian, swap to little endian
	geo2Pack(pack, deltas, delta_len, data, len, gld);
	endianSwapArray(data, stride * vert_count, pack_type); // restore to big endian
	free(deltas);
}

static F32 geo2CalcUvDensity(const ModelSource *model)
{
    const Vec3 *verts = model->unpack.verts;
    const Vec2 *texcoords = model->unpack.sts;
    const U32 *tris32 = model->unpack.tris;
    F32 density = FLT_MAX;
    int i;

	if (!model->unpack.verts || !model->unpack.sts) {
		return GMESH_LOG_MIN_UV_DENSITY;
	}

    assert(model->tri_count);
    
    for (i=0; i<model->tri_count; i++)
    {
        F32 tria = triArea3Squared(verts[tris32[i*3+0]], verts[tris32[i*3+1]], verts[tris32[i*3+2]]);
		F32 uva = triArea2(texcoords[tris32[i*3+0]], texcoords[tris32[i*3+1]], texcoords[tris32[i*3+2]]);

        if (tria > 0.000001f && uva > 0.000001f) {
			F32 tri_density;

			F32 duv0 = distance2Squared(texcoords[tris32[i*3+0]], texcoords[tris32[i*3+1]]);
			F32 duv1 = distance2Squared(texcoords[tris32[i*3+1]], texcoords[tris32[i*3+2]]);
			F32 duv2 = distance2Squared(texcoords[tris32[i*3+2]], texcoords[tris32[i*3+0]]);

			duv0 /= distance3Squared(verts[tris32[i*3+0]], verts[tris32[i*3+1]]);
			duv1 /= distance3Squared(verts[tris32[i*3+1]], verts[tris32[i*3+2]]);
			duv2 /= distance3Squared(verts[tris32[i*3+2]], verts[tris32[i*3+0]]);

			tri_density = MAX(MAX(duv0, duv1), duv2);
			MIN1F(density, tri_density);
        }
    }

	if (density == FLT_MAX || density < GMESH_MIN_UV_DENSITY) {
		density = GMESH_LOG_MIN_UV_DENSITY;
	} else {
		density = 0.5f * log2f(density);
	}

	return density;
}

static void logPackExceptions(const ModelSource *model, const char *pack_stage)
{
	U32 exception_flags = getCombinedFPUExceptionStatus();
	_clearfp();
	exception_flags &= ~(_EM_INEXACT | _EM_UNDERFLOW | _EM_DENORMAL);
	if (exception_flags)
		log_printf(LOG_ERRORS, "Source model \"%s\" from %s has errors %s,%s,%s,%s,%s,%s during packing %s\n",
			model->name, model->gld->filename,
			exception_flags & _EM_INEXACT ? "Inexact" : "",
			exception_flags & _EM_UNDERFLOW ? "Underflow" : "",
			exception_flags & _EM_OVERFLOW ? "Overflow" : "",
			exception_flags & _EM_ZERODIVIDE ? "Divide by Zero" : "",
			exception_flags & _EM_INVALID ? "Invalid Operation" : "",
			exception_flags & _EM_DENORMAL ? "Denormal" : "",
			pack_stage
			);
}

// Called on all levels, source data is unpacked, optimized, and then repacked
static void geo2PackAll(ModelSource *model)
{
	U32 default_fpcw, final_fpcw;
	// Note, if we already have packed data, it gets overwritten, but the pointer
	//  is still in the alloc list, so it will be freed, not leaked.

	//if (model->pack.verts.data_ptr)
	//	return;

	model->uv_density = geo2CalcUvDensity(model);

	SET_FP_CONTROL_WORD_DEFAULT;
	_controlfp_s(&default_fpcw, 0, 0);
	FP_NO_EXCEPTIONS_BEGIN;

	_clearfp();

	// Do some optimizations
	geo2OptimizeModel(model);
	logPackExceptions(model, "optimize");

	geo2PackDeltas(&model->pack.tris, model->unpack.tris, 3, model->tri_count, PACK_U32, 0, model->gld);
	logPackExceptions(model, "triangles");

	if (model->collision_only)
		geo2PackTerrainPosDeltas(&model->pack.verts, model->unpack.verts, model->vert_count, model->gld);
	else
		geo2PackDeltas(&model->pack.verts, model->unpack.verts, 3, model->vert_count, PACK_F32, 32768.f, model->gld);
	logPackExceptions(model, "verts");

	geo2PackDeltas(&model->pack.norms, model->unpack.norms, 3, model->vert_count, PACK_F32, 256.f, model->gld);
	logPackExceptions(model, "norms");

	geo2PackDeltas(&model->pack.binorms, model->unpack.binorms, 3, model->vert_count, PACK_F32, 256.f, model->gld);
	logPackExceptions(model, "binorms");

	geo2PackDeltas(&model->pack.tangents, model->unpack.tangents, 3, model->vert_count, PACK_F32, 256.f, model->gld);
	logPackExceptions(model, "tangents");

	geo2PackDeltas(&model->pack.sts, model->unpack.sts, 2, model->vert_count, PACK_F32, 32768.f, model->gld);
	logPackExceptions(model, "sts");

	geo2PackDeltas(&model->pack.sts3, model->unpack.sts3, 2, model->vert_count, PACK_F32, 32768.f, model->gld);
	logPackExceptions(model, "sts3");

	geo2PackDeltas(&model->pack.colors, model->unpack.colors, 4, model->vert_count, PACK_U8, 0, model->gld);
	logPackExceptions(model, "colors");

	geo2PackDeltas(&model->pack.matidxs, model->unpack.matidxs, 4, model->vert_count, PACK_U8, 0, model->gld);
	logPackExceptions(model, "matidxs");

	geo2PackDeltas(&model->pack.weights, model->unpack.weights, 4, model->vert_count, PACK_U8, 0, model->gld);
	logPackExceptions(model, "weights");

	geo2PackDeltas(&model->pack.verts2, model->unpack.verts2, 3, model->vert_count, PACK_F32, 32768.f, model->gld);
	logPackExceptions(model, "verts2");

	geo2PackDeltas(&model->pack.norms2, model->unpack.norms2, 3, model->vert_count, PACK_F32, 256.f, model->gld);
	logPackExceptions(model, "norms2");

	FP_NO_EXCEPTIONS_END;

	_controlfp_s(&final_fpcw, 0, 0);
	assert(default_fpcw == final_fpcw);	
}

static void geo2LoadData(Geo2LoadData* gld, FILE *file)
{
	U8 * mem;
	int j;
	U8 * base_offset;

	//memlog_printf(&geo_memlog, "geo2LoadData(%s)", gld->filename);

	//////////////////////////////////////////////////////////////////////////

	//fseek(file,0,SEEK_SET);
	fseek(file, gld->data_offset, SEEK_SET);

	gld->geo_data = mem = malloc(gld->datasize);
	eaPush(&gld->allocList, mem);
	fread(mem, 1, gld->datasize, file);
	base_offset = mem;

	for( j=0 ; j < gld->model_count ; j++ )
	{
		ModelSource *model = gld->models[j];

		patchPackPtr(&model->pack.tris,base_offset);
		patchPackPtr(&model->pack.verts,base_offset);
		patchPackPtr(&model->pack.norms,base_offset);
		patchPackPtr(&model->pack.binorms,base_offset);
		patchPackPtr(&model->pack.tangents,base_offset);
		patchPackPtr(&model->pack.sts,base_offset);
		patchPackPtr(&model->pack.sts3,base_offset);
		patchPackPtr(&model->pack.colors,base_offset);
		patchPackPtr(&model->pack.weights,base_offset);
		patchPackPtr(&model->pack.matidxs,base_offset);
		patchPackPtr(&model->pack.verts2,base_offset);
		patchPackPtr(&model->pack.norms2,base_offset);

		geo2UnpackAll(model, gld->filename);
	}
}

static Geo2LoadData * geo2Load(const char * name, FileList *file_list)
{
	Geo2LoadData	*gld = 0;
	FILE		*file = 0;

	//printf("loading gld: %s\n", name);
	file = fileOpen(name, "rb");

	if(file)
	{
		gld = calloc(sizeof(Geo2LoadData), 1);

		gld->filename = allocAddFilename(name);

		if(!geo2LoadModelHeaders(file, gld, file_list))
		{
			SAFE_FREE(gld);
		}
		else
		{
			// Load actual data!
			geo2LoadData(gld, file);
		}
		fclose(file);
	}
	return gld;
}

void geo2Destroy(Geo2LoadData *gld)
{
	int i;

	for (i=0; i<gld->model_count; i++)
		eaDestroyStruct(&gld->models[i]->lods, parse_AutoLOD);

	for (i=0; i<eaSize(&gld->eaAllocList); i++)
	{
		void ***ea = gld->eaAllocList[i];
		eaDestroy(ea);
	}
	eaDestroy(&gld->eaAllocList);
	eaDestroyEx(&gld->allocList, freeWrapper);
	free(gld);
}


ModelSource *modelSourceFindSibling(ModelSource *model, const char *siblingname)
{
	int i;
	// Binary search instead?
	for (i=0; i<model->gld->model_count; i++)
	{
		ModelSource *m2 = model->gld->models[i];
		if (stricmp(m2->name, siblingname)==0)
			return m2;
	}
	return NULL;
}






char *geo2BinNameFromGeo2Name(const char *relpath, char *binpath, int binpath_size)
{
	char *ext;
	if (strStartsWith(relpath, "character_library/") || strStartsWith(relpath, "object_library/"))
	{
		char *last=NULL;
		STR_COMBINE_BEGIN_S(binpath, binpath_size);
		if (strStartsWith(relpath, "object_library/"))
		{
			STR_COMBINE_CAT("bin/geobin/ol");
		} else {
			STR_COMBINE_CAT("bin/geobin/cl");
		}
		// Concat and truncate paths
		for (s=strchr(relpath, '/'); *s; )
		{
			if (*s == '/') {
				if (last) {
					c = last+2;
					last = c;
				} else {
					last = c;
				}
			}
			*c++=*s++;
		}
		STR_COMBINE_END(binpath);
	} else {
		// Not character_lib or object_lib, must be a temporary file used in GetVRML?
		assert(fileIsAbsolutePath(relpath));
		assert(strStartsWith(relpath, fileTempDir()));
		changeFileExt_s(relpath, ".mset", SAFESTR2(binpath));
	}
	ext = strrchr(binpath, '.');
	assert(ext);
	strcpy_s(ext, binpath_size - (ext - binpath), ".mset");
	return binpath;
}

AUTO_RUN_LATE;
void verifyModelLODDataCRCHasNotChanged(void)
{
	U32 table_crc = ParseTableCRC(parse_ModelLODData, NULL, 0);
	assertmsg(table_crc == 0x21c585bb || table_crc == 0x01c4bb88, "ModelLODData parsetable CRC has changed.");
// This will cause all model files to be rebuilt, which will generate a slow patch
// (although probably very small), and is generally quite undesirable.  Ideally,
// any changes should simply be made to ModelLOD instead.  If a data format is
// required, do one of the following:
//   a) simply change this assert (all model bins will be rebuilt)
//   b) if the data format is bitwise compatible, relax this assert and the
// various other checks (e.g. if you just added a new flag or CRC tech changed)
//   c) write some fixup code to deal with it (undesireable)
}

static bool geo2IsBinUpToDate(const char *binpath)
{
	char fullbinpath_buf[MAX_PATH];
	const char *fullbinpath;
	bool ret;
	U32 version;
	U32 expected_version;
	int i;
	int offs;
	SimpleBufHandle buf;
	FileList file_list=NULL;
	char temp_buf[MAX_PATH];
	bool is_char_lib = strStartsWith(binpath, "bin/geobin/cl/");


	getFileNameNoExt(temp_buf, binpath);
	if (stashFindPointer(stModelLODInfoOverrides, temp_buf, NULL)) // An override was put in place, rebuild it!
		return false;

	fullbinpath = fileLocateReadBin(binpath, fullbinpath_buf);
	if (is_pigged_path(fullbinpath))
		fullbinpath = binpath;
	if (!fileExists(fullbinpath))
	{
		errorIsDuringDataLoadingInc(fullbinpath);
		assert(isDevelopmentMode()); // Production mode should have bins made!
		errorIsDuringDataLoadingDec();
		return false;
	}

	ret = true;
	buf = SimpleBufOpenRead(fullbinpath, NULL);
	assert(buf);
	SimpleBufReadU32(&offs, buf);
	SimpleBufReadU32(&version, buf); version = endianSwapU32(version);
	if (version != (is_char_lib?GEO2_CL_BIN_VERSION:GEO2_OL_BIN_VERSION))
	{
		verbose_printf("geo2IsBinUpToDate: bin file %s is incorrect version\n", binpath);
		goto failure;
	}
	SimpleBufReadU32(&version, buf); version = endianSwapU32(version);
	expected_version = ParseTableCRC(parse_ModelLODData, NULL, 0);
	if (version != expected_version && version != 0x21c585bb) // pre-UpdateCRCFromParseInfoColumn() CRC
	{
		verbose_printf("geo2IsBinUpToDate: bin file %s is incorrect table CRC\n", binpath);
		goto failure;
	}

	// Skip past header
	SimpleBufSeek(buf, offs, SEEK_SET);

	if (!FileListRead(&file_list, buf))
	{
		verbose_printf("geo2IsBinUpToDate: bin file %s has corrupt FileList\n", binpath);
		goto failure;
	}

	// Check to see if any of the dependent files are newer
	if (isDevelopmentMode()) // No source files in production mode
	{
		for (i=eaSize(&file_list)-1; i>=0; i--) 
		{
			FileEntry* file_entry = file_list[i];
			bool bUpToDate = true;
			static const char *lastPath=NULL;
			__time32_t diskdate = fileLastChanged(file_entry->path);
			if (diskdate != file_entry->date && ABS_UNS_DIFF(diskdate, file_entry->date) != 3600) {
				verbose_printf("geo2IsBinUpToDate: Source file \"%s\" failed timestamp check.\n", file_entry->path);
				goto failure;
			}
		}
	}

	FileListDestroy(&file_list);

	// Looks good!

	goto cleanup;

failure:
	ret = false;

cleanup:
	SerializeClose(buf);
	FileListDestroy(&file_list);
	return ret;
}

// This function does exactly what the original author never wanted to do - directly free the LodModel2s from a ModelSource
// this allows me to keep the Geo2LoadData around so things can be fast
static void geo2FreeLodModels(Geo2LoadData *gld,ModelSource * pModelSource)
{
	int i;
	for (i=eaSize(&pModelSource->lod_models)-1;i>=0;i--)
	{
		free(pModelSource->lod_models[i]);
		eaFindAndRemove(&gld->allocList,pModelSource->lod_models[i]);
	}

	eaFindAndRemove(&gld->eaAllocList,pModelSource->lod_models);
	eaDestroy(&pModelSource->lod_models);
}

static void geo2RebuildModelLOD(Geo2LoadData *gld,ModelHeader *pHeader)
{
	int i, j;
	for (i=0; i<gld->model_count; i++)
	{
		ModelSource *model = gld->models[i];

		if (stricmp(model->name,pHeader->modelname) != 0)
			continue;

		if (eaSize(&model->lod_models))
		{
			// Free the old lod_models
			geo2FreeLodModels(gld,model);
		}

		eaDestroyStruct(&gld->models[i]->lods, parse_AutoLOD);
		modelInitLODs2(gld->models[i], gld->filename, NULL);

		for (j=0; j<eaSize(&model->lods); j++)
		{
			AutoLOD *lod = model->lods[j];
			if (lod->modelname_specified && j != 0)
			{
				// Don't use an AutoLOD, generate a dummy, empty entry
				LodModel2 *lodmodel;
				lodmodel = callocStruct(LodModel2);
				eaPush(&gld->allocList, lodmodel);
				lodmodel->index = j;
				lodmodel->error = 1; // Or 0?
				lodmodel->tri_percent = 1;
				lodmodel->model = NULL;
				eaPush(&model->lod_models, lodmodel);
			} else {
				// Note: we are still generating model data if LOD_ERROR_NULL_MODEL
				//  is set, this is intentional/okay, since the data will not be
				//  loaded (and might be needed for the LOD editor, etc).  Also
				//  do this if a modelname is specified and we're the highest LOD
				ModelSource *model_lod = wlModelBinningGenerateLOD(model, lod->max_error, lod->upscale_amount, (lod->flags & LOD_ERROR_TRICOUNT)?TRICOUNT_RMETHOD:ERROR_RMETHOD, j);
				if (model_lod == model) {
					LodModel2 *lodmodel;
					lodmodel = callocStruct(LodModel2);
					eaPush(&gld->allocList, lodmodel);
					lodmodel->index = j;
					if (j==0)
					{
						// High LOD
						assert(!eaSize(&model->lod_models));
						lodmodel->error = 1; // Or 0?
						lodmodel->tri_percent = 1;
						lodmodel->model = model;
					} else {
						// Some other LOD that wants to use the high one?
						lodmodel->error = 1; // Or 0?
						lodmodel->tri_percent = 1;
						lodmodel->model = NULL;
					}
					eaPush(&model->lod_models, lodmodel);
				}
				assert(eaSize(&model->lod_models) == j+1);
			}
		}
		eaPushUnique(&gld->eaAllocList, &model->lod_models);

		// Pack and optimize after building all LODs
		for (j=0; j<eaSize(&model->lod_models); j++)
		{
			ModelSource *model_lod = model->lod_models[j]->model;
			if (model_lod && (model != model_lod || j==0))
			{
				geo2PackAll(model_lod);
			}
		}
	}
}

static void geo2BuildAllLODs(Geo2LoadData *gld)
{
	int i, j;
	for (i=0; i<gld->model_count; i++)
	{
		ModelSource *model = gld->models[i];

		assert(model->lod_models == NULL);

		for (j=0; j<eaSize(&model->lods); j++)
		{
			AutoLOD *lod = model->lods[j];
			if (lod->modelname_specified && j != 0)
			{
				// Don't use an AutoLOD, generate a dummy, empty entry
				LodModel2 *lodmodel;
				lodmodel = callocStruct(LodModel2);
				eaPush(&gld->allocList, lodmodel);
				lodmodel->index = j;
				lodmodel->error = 1; // Or 0?
				lodmodel->tri_percent = 1;
				lodmodel->model = NULL;
				eaPush(&model->lod_models, lodmodel);
			} else {
				// Note: we are still generating model data if LOD_ERROR_NULL_MODEL
				//  is set, this is intentional/okay, since the data will not be
				//  loaded (and might be needed for the LOD editor, etc).  Also
				//  do this if a modelname is specified and we're the highest LOD
				ModelSource *model_lod = wlModelBinningGenerateLOD(model, lod->max_error, lod->upscale_amount, (lod->flags & LOD_ERROR_TRICOUNT)?TRICOUNT_RMETHOD:ERROR_RMETHOD, j);
				if (model_lod == model) {
					LodModel2 *lodmodel;
					lodmodel = callocStruct(LodModel2);
					eaPush(&gld->allocList, lodmodel);
					lodmodel->index = j;
					if (j==0)
					{
						// High LOD
						assert(!eaSize(&model->lod_models));
						lodmodel->error = 1; // Or 0?
						lodmodel->tri_percent = 1;
						lodmodel->model = model;
					} else {
						// Some other LOD that wants to use the high one?
						lodmodel->error = 1; // Or 0?
						lodmodel->tri_percent = 1;
						lodmodel->model = NULL;
					}
					eaPush(&model->lod_models, lodmodel);
				}
				assert(eaSize(&model->lod_models) == j+1);
			}
		}
		eaPushUnique(&gld->eaAllocList, &model->lod_models);

		// Pack and optimize after building all LODs
		for (j=0; j<eaSize(&model->lod_models); j++)
		{
			ModelSource *model_lod = model->lod_models[j]->model;
			if (model_lod && (model != model_lod || j==0))
			{
				geo2PackAll(model_lod);
			}
		}
	}
}

static void geo2OptimizeModel(ModelSource *model)
{
	geo2OptimizeVertexOrder(model);
}

static void geo2SerializeSingleModelLOD(SimpleBufHandle buf, ModelSource *model, bool use_texnames)
{
	// Create a ModelLODData structure, followed by the packed data, and write it all
	// Writes everything in Big Endian
	ModelLODData model_data = {0};
	int offsHeader = SimpleBufTell(buf);
	int i;
	char **texnameTable = NULL;
	U16 value;

	// Save a spot to write the header
	SimpleBufWrite(&model_data, sizeof(model_data), buf);
	model_data.vert_count = model->vert_count;
	model_data.tri_count = model->tri_count;
	model_data.tex_count = model->tex_count;
	model_data.process_time_flags = model->process_time_flags;
	model_data.texel_density_avg = model->uv_density;
	model_data.texel_density_stddev = 0.0f;

	// Write tex_idx array
	for (i=0; i<model->tex_count; i++) {
		if (use_texnames) {
			int id = eaPushUnique(&texnameTable, model->gld->texnames.strings[model->tex_idx[i].id]);
			// This structure is loaded raw into memory, write as big endian (SimpleBufWrite will swap to little if we're on big, so double-swap)
			SimpleBufWriteU16(endianSwapU16(id), buf);
		} else {
			while (eaSize(&texnameTable) <= model->tex_idx[i].id)
				eaPush(&texnameTable, "");
			SimpleBufWriteU16(endianSwapU16(model->tex_idx[i].id), buf);
		}
		SimpleBufWriteU16(endianSwapU16(model->tex_idx[i].count), buf);
	}

	// Write packed data, recording offsets as we go
	model_data.pack = model->pack;
#define WRITE_PACKED(field)	\
		if (model->pack.field.data_ptr) {										\
			model_data.pack.field.data_offs = SimpleBufTell(buf) - offsHeader;	\
			SimpleBufWrite(model->pack.field.data_ptr, model->pack.field.packsize?model->pack.field.packsize:ABS(model->pack.field.unpacksize), buf);\
		}
	WRITE_PACKED(tris);
	WRITE_PACKED(verts);
	WRITE_PACKED(norms);
	WRITE_PACKED(binorms);
	WRITE_PACKED(tangents);
	WRITE_PACKED(sts);
	WRITE_PACKED(sts3);
	WRITE_PACKED(colors);
	WRITE_PACKED(weights);
	WRITE_PACKED(matidxs);
	WRITE_PACKED(verts2);
	WRITE_PACKED(norms2);
#undef WRITE_PACKED

	model_data.data_size = SimpleBufTell(buf) - offsHeader; // The final amount of data that will be kept around

	// Write the string table at the end (Will be freed after loading/use)
	value = eaSize(&texnameTable);
	SimpleBufWriteU16(endianSwapU16(value), buf);
	for (i=0; i<eaSize(&texnameTable); i++) {
		char *s = texnameTable[i];
		U16 slen = (U16)strlen(s);
		SimpleBufWriteU16(endianSwapU16(slen), buf);
		SimpleBufWrite(s, slen, buf);
	}
	eaDestroy(&texnameTable);

	// Endian swap if Little
	if (!isBigEndian())
	{
		endianSwapStruct(parse_ModelLODData, &model_data);
	}

	// Write actual ModelLODData structure
	SimpleBufSeek(buf, offsHeader, SEEK_SET);
	SimpleBufWrite(&model_data, sizeof(model_data), buf);
	SimpleBufSeek(buf, 0, SEEK_END);
}

static void geo2PreCookCollision(ModelSource *collmodel, void **data_ptr, int *data_size)
{
#if !PSDK_DISABLED
	PSDKMeshDesc mesh_desc = {0};
	Vec3 *verts;
	IVec3 *tris;
	int i, j;

	// unpack packed data instead of using the pre-packing data so that the position values come out the same

	mesh_desc.name = "pre-cooked mesh";

	EnterCriticalSection(&model_unpack_cs);
	in_model_unpack_cs++;

	mesh_desc.vertCount = collmodel->vert_count;
	mesh_desc.vertArray = verts = ScratchAlloc(collmodel->vert_count * sizeof(Vec3));
	geoUnpackDeltas(&collmodel->pack.verts, verts, 3, mesh_desc.vertCount, PACK_F32, collmodel->name, collmodel->gld->filename, NULL);

	mesh_desc.triCount = collmodel->tri_count;
	tris = ScratchAlloc(collmodel->tri_count * 3 * sizeof(U32));
	mesh_desc.triArray = &tris[0][0];
	geoUnpackDeltas(&collmodel->pack.tris, tris, 3, mesh_desc.triCount, PACK_U32, collmodel->name, collmodel->gld->filename, NULL);

	in_model_unpack_cs--;
	LeaveCriticalSection(&model_unpack_cs);

//	optimizeForDeltaCompression(verts, collmodel->vert_count, tris, collmodel->tri_count);

	if (0) // Code to check consistency of NxCookMesh outputs (horribly inconsistent with non-debug CRT :( ).
	{
		void *results[15];
		int results_size[ARRAY_SIZE(results)];
		int results_count[ARRAY_SIZE(results)] = {0};
		bool bBad=false;
		int timer = timerAlloc();
		for (i=0; i<ARRAY_SIZE(results); i++) 
		{
			FILE *f;
			char filename[MAX_PATH];

			PSDKCookedMesh *mesh;
			PSDKMeshDesc meshDesc = {0};

			pdskPreCookTriangleMesh(&mesh_desc, &results[i], &results_size[i]);


			// Log stuff
			meshDesc.preCookedData = memdup(results[i], results_size[i]);
			meshDesc.preCookedSize = results_size[i];
			psdkCookedMeshCreate(&mesh, &meshDesc);

			sprintf(filename, "%s/%s_%d.cookedverts.txt", fileTempDir(), collmodel->name, i);
			f = fopen(filename, "wt");
			if (f)
			{
				U32 k;
				Vec3 *meshverts;
				U32 vert_count;
				psdkCookedMeshGetVertices(mesh, &meshverts, &vert_count);
				for (k=0; k<vert_count; k++)
				{
					fprintf(f, "%1.10f %1.10f %1.10f\n", meshverts[k][0], meshverts[k][1], meshverts[k][2]);
				}
				fclose(f);
			}

			sprintf(filename, "%s/%s_%d.cookedtris.txt", fileTempDir(), collmodel->name, i);
			f = fopen(filename, "wt");
			if (f)
			{
				U32 k;
				S32 *meshtris;
				U32 tri_count;
				psdkCookedMeshGetTriangles(mesh, &meshtris, &tri_count);
				for (k=0; k<tri_count; k++)
				{
					fprintf(f, "%d %d %d\n", meshtris[k*3], meshtris[k*3+1], meshtris[k*3+2]);
				}
				fclose(f);
			}

			sprintf(filename, "%s/%s_%d.cookedstream.txt", fileTempDir(), collmodel->name, i);
			f = fopen(filename, "wt");
			if (f)
			{
				for (j=0; j<results_size[i]; j+=4)
				{
					U8 *r = results[i];
					fprintf(f, "%08x\n", *(U32*)(r + j));
				}
				fclose(f);
			}

		}
		for (i=0; i<ARRAY_SIZE(results); i++)
		{
			for (j=0; j<=i; j++) 
			{
				if (results_size[i]==results_size[j])
				{
					if (memcmp(results[i], results[j], results_size[i])==0) {
						results_count[j]++;
						if (j!=0)
							bBad = true;
						break;
					}
				}
			}
		}
		if (bBad)
		{
			printf("\r%s Cooking non-deterministic (%1.3fs) ", collmodel->name, timerElapsed(timer));
			for (i=0; i<ARRAY_SIZE(results_count); i++)
			{
				if (results_count[i])
					printf(" %d", results_count[i]);
			}
			printf("\n");
		} else {
			printf("\r%s Cooking GOOD in %1.3fs\n", collmodel->name, timerElapsed(timer));
		}
		for (i=0; i<ARRAY_SIZE(results); i++)
			SAFE_FREE(results[i]);
		timerFree(timer);
	}
	pdskPreCookTriangleMesh(&mesh_desc, data_ptr, data_size);

	ScratchFree(tris);
	ScratchFree(verts);
#endif
}


static void geo2SerializeSingleModelColData(SimpleBufHandle buf, ModelSource *model, bool use_texnames, Geo2LoadData *gld, U32 *data_length)
{
	int i;
	// Write cooked collision data stream
	PackData coll_pack_data = {0};
	void *data = NULL;
	int data_size = 0;
	char **texnameTable = NULL;
	int value;
	int timer = timerAlloc();

	geo2PreCookCollision(model, &data, &data_size);
	if (!data_size)
	{
		g_bin_timing_cooking += timerElapsed(timer);
		timerFree(timer);
		assert(!data);
		return;
	}

	if (g_debug_write_collision_data)
	{
		FILE *f = fopen(g_debug_write_collision_data, "wb");
		if (f)
		{
			fwrite(data, data_size, 1, f);
			fclose(f);
		}
	}

	geo2Pack(&coll_pack_data, data, data_size, NULL, 0, gld);
#if !PSDK_DISABLED
	pdskFreeBuffer(data);
#endif
	g_bin_timing_cooking += timerElapsed(timer);
	timerFree(timer);

	*data_length = coll_pack_data.packsize?coll_pack_data.packsize:ABS(coll_pack_data.unpacksize);

	// Data before sizes, because we want to be able to realloc() at load time
	SimpleBufWrite(coll_pack_data.data_ptr, *data_length, buf);
	SimpleBufWriteU32(endianSwapU32(coll_pack_data.packsize), buf);
	SimpleBufWriteU32(endianSwapU32(coll_pack_data.unpacksize), buf);

	// write out materials and texidxs from collmodel
	SimpleBufWriteU16(endianSwapU16(model->tex_count), buf);
	// Write tex_idx array
	for (i=0; i<model->tex_count; i++) {
		if (use_texnames) {
			int id = eaPushUnique(&texnameTable, model->gld->texnames.strings[model->tex_idx[i].id]);
			// This structure is loaded raw into memory, write as big endian (SimpleBufWrite will swap to little if we're on big, so double-swap)
			SimpleBufWriteU16(endianSwapU16(id), buf);
		} else {
			while (eaSize(&texnameTable) <= model->tex_idx[i].id)
				eaPush(&texnameTable, "");
			SimpleBufWriteU16(endianSwapU16(model->tex_idx[i].id), buf);
		}
		SimpleBufWriteU16(endianSwapU16(model->tex_idx[i].count), buf);
	}

	// Write the string table at the end (Will be freed after loading/use)
	value = eaSize(&texnameTable);
	SimpleBufWriteU16(endianSwapU16(value), buf);
	for (i=0; i<eaSize(&texnameTable); i++) {
		char *s = texnameTable[i];
		U16 slen = (U16)strlen(s);
		SimpleBufWriteU16(endianSwapU16(slen), buf);
		SimpleBufWrite(s, slen, buf);
	}
	eaDestroy(&texnameTable);
}

// Writes a .mset file.  All data is saved as big endian
static void geo2SaveBin(SimpleBufHandle buf, Geo2LoadData *gld, FileList *file_list, bool use_texnames)
{
	int i, j;
	int offs, start;
	int filelistoffsoffs;
	U32 u32;
	bool is_char_lib = strStartsWith(gld->filename, "character_library/");
	start = SimpleBufTell(buf);
	SimpleBufWriteU32(0, buf); // Save a spot for the header size
	SimpleBufWriteU32(endianSwapU32((is_char_lib?GEO2_CL_BIN_VERSION:GEO2_OL_BIN_VERSION)), buf);
	u32 = ParseTableCRC(parse_ModelLODData, NULL, 0);
	SimpleBufWriteU32(endianSwapU32(u32), buf);
	filelistoffsoffs = SimpleBufTell(buf);
	// Write header with offsets for each ModelLOD and collision data
	SimpleBufWriteU16(endianSwapU16(gld->model_count), buf);
	for (i=0; i<gld->model_count; i++)
	{
		ModelSource *model = gld->models[i];
		U16 slen = (U16)strlen(model->name);
		U16 lod_count = model->collision_only ? 0 : eaSize(&model->lod_models);
		SimpleBufWriteU16(endianSwapU16(slen), buf);
		SimpleBufWrite(model->name, slen, buf);
		SimpleBufWriteU16(endianSwapU16(lod_count), buf);
		ANALYSIS_ASSUME(lod_count == 0 || model->lod_models);
		for (j=0; j<lod_count; j++)
		{
			LodModel2 *lodmodel = model->lod_models[j];
			if (lodmodel->model == model) {
				assert(j == 0);
			}
			lodmodel->offsetoffset = SimpleBufTell(buf);
			SimpleBufWriteU32(0, buf); // Offset
			SimpleBufWriteU32(0, buf); // Length
		}

		model->colloffsetoffset = SimpleBufTell(buf);
		SimpleBufWriteU32(0, buf); // Offset
		SimpleBufWriteU32(0, buf); // data length
		SimpleBufWriteU32(0, buf); // Total length
	}
	offs = SimpleBufTell(buf);
	assert(offs <= MAX_MODEL_BIN_HEADER_SIZE);
	// Write header size/filelist offs
	SimpleBufSeek(buf, start, SEEK_SET);
	SimpleBufWriteU32(offs, buf); // The one little-endian bit of data (for hogg caching if we need that later)
	// Write FileList
	SimpleBufSeek(buf, offs, SEEK_SET);
	FileListWrite(file_list, buf, NULL, "main");
	// Write actual data
	for (i=0; i<gld->model_count; i++)
	{
		ModelSource *model = gld->models[i];
		U16 lod_count = model->collision_only ? 0 : eaSize(&model->lod_models);
		ModelSource *collmodel = NULL;
		int finaloffs;
		ANALYSIS_ASSUME(lod_count == 0 || model->lod_models);
		for (j=0; j<lod_count; j++)
		{
			LodModel2 *lodmodel = model->lod_models[j];
			if (lodmodel->model)
			{
				// use the high LOD for collision, unless it is set as a high detail LOD, 
				// in which case use the next LOD
				if (!collmodel || (j == 1 && model->high_detail_high_lod))
					collmodel = lodmodel->model;

				// Record offset to where this data is being written
				offs = SimpleBufTell(buf);
				// Write the actual data
				geo2SerializeSingleModelLOD(buf, lodmodel->model, use_texnames);

				// Write the offset to the start and the length of the data
				finaloffs = SimpleBufTell(buf);
				SimpleBufSeek(buf, lodmodel->offsetoffset, SEEK_SET);
				SimpleBufWriteU32(endianSwapU32(offs), buf);
				SimpleBufWriteU32(endianSwapU32(finaloffs - offs), buf);
				SimpleBufSeek(buf, 0, SEEK_END);
			} else {
				// No model - hand-built LOD placeholder?  Or lower LOD that uses the highest?
				// Leaving 0s for offset and size values in header
			}
		}

		if (!model->collision_only)
		{
			// Look for named collision model
			char col_model_name[1024];
			sprintf(col_model_name, "%s_COLL", model->name);
			for (j=0; j<gld->model_count; j++)
			{
				if (stricmp(gld->models[j]->name, col_model_name)==0)
				{
					if (eaSize(&gld->models[j]->lod_models))
					{
						if (gld->models[j]->lod_models[0]->model)
							collmodel = gld->models[j]->lod_models[0]->model;
					}
				}
			}
		}

		if (model->high_detail_high_lod && lod_count > 1 && collmodel == model->lod_models[0]->model)
			collmodel = NULL; // handbuild LOD at slot 1 and that is the collision we want

		if (model->collision_only)
		{
			for (j=0; j<eaSize(&model->lod_models); j++)
			{
				LodModel2 *lodmodel = model->lod_models[j];
				if (lodmodel->model)
				{
					collmodel = lodmodel->model;
					break;
				}
			}
		}

		// Skip pre-cooking collision for character models (can still be on-the-fly cooked)
		if (strStartsWith(gld->filename, "character_library"))
		{
			collmodel = NULL;
		}

		if (collmodel && !collmodel->no_collision && !model->no_collision)
		{
			U32 data_length=0;
			// Record offset to where this data is being written
			offs = SimpleBufTell(buf);

			assert(use_texnames);

			geo2SerializeSingleModelColData(buf, collmodel, use_texnames, gld, &data_length);

			// Write the offset to the start and the length of the data
			finaloffs = SimpleBufTell(buf);
			SimpleBufSeek(buf, model->colloffsetoffset, SEEK_SET);
			SimpleBufWriteU32(endianSwapU32(offs), buf);
			SimpleBufWriteU32(endianSwapU32(data_length), buf);
			SimpleBufWriteU32(endianSwapU32(finaloffs - offs), buf);
			SimpleBufSeek(buf, 0, SEEK_END);
		} else {
			// Leave 0s written earlier
		}
	}
}

static int g_geobin_updates;
static bool g_binning_model_lods=false;
bool geo2BinningModelLODs(void)
{
	return g_binning_model_lods;
}

Geo2LoadData * geo2LoadModelFromSource(ModelHeader * modelheader,FileList *file_list)
{
	char path[MAX_PATH];
	Geo2LoadData *gld;

	changeFileExt(modelheader->filename, ".geo2", path);

	assert(fileExists(path));

	g_geobin_updates++;

	// Build it!
	// Load geo data, collect deps, and unpack it
	gld = geo2Load(path, file_list);

	return gld;
}

// I don't do this in a background thread, cause it's a kabillion times faster than what we were doing before, and I don't really
// know the details of how to get it to happen in a background thread [RMARR - 12/6/11]
void geo2UpdateBinsForLoadData(ModelHeader * modelheader,Geo2LoadData * gld,FileList *file_list)
{
	char path[MAX_PATH];
	char relpath[MAX_PATH];
	char binpath[MAX_PATH];
	char mutexname[MAX_PATH];
	ThreadAgnosticMutex hMutex;
	SimpleBufHandle buf;

	changeFileExt(modelheader->filename, ".geo2", path);

	g_binning_model_lods = true;

	fileRelativePath(path, relpath);

	geo2BinNameFromGeo2Name(relpath, binpath, ARRAY_SIZE_CHECKED(binpath));

	// Grab mutex
	makeLegalMutexName(mutexname, binpath);
	strupr(mutexname);
	hMutex = acquireThreadAgnosticMutex(mutexname);

	// Check if it exists and is up to date
	if (geo2IsBinUpToDate(binpath))
		goto cleanup;

#if _PS3
    printf("\n%s: trying to bin\n%s\n", __FUNCTION__, path);
    assert(!"cannot bin");
#else

	geo2RebuildModelLOD(gld, modelheader);

	buf = SimpleBufOpenWrite(binpath, true, NULL, false, false);
	geo2SaveBin(buf, gld, file_list, true);
	SimpleBufClose(buf);
	FolderCacheForceUpdate(folder_cache, binpath);
#endif

	// Release mutex
	releaseThreadAgnosticMutex(hMutex);

cleanup:
	g_binning_model_lods = false;
}

static void modelCheckTangentBases(ModelSource *srcmodel, const char *checkStage)
{
	const float MIN_NORMAL_LENGTH_SQR = 1.0e-6f;
	int i, firstBadNrm = -1, badNormals = 0, firstBadTan = -1, badTangents = 0, firstBadBinrm = -1, badBinormals = 0;

	if (srcmodel->unpack.norms)
	{
		for (i = 0; i < srcmodel->vert_count; ++i)
		{
			if (lengthVec3Squared(srcmodel->unpack.norms[i]) < MIN_NORMAL_LENGTH_SQR)
			{
				if (firstBadNrm == -1)
					firstBadNrm = i;
				++badNormals;
			}
		}
	}
	if (srcmodel->unpack.tangents)
	{
		for (i = 0; i < srcmodel->vert_count; ++i)
		{
			if (lengthVec3Squared(srcmodel->unpack.tangents[i]) < MIN_NORMAL_LENGTH_SQR)
			{
				if (firstBadTan == -1)
					firstBadTan = i;
				++badTangents;
			}
		}
	}
	if (srcmodel->unpack.binorms)
	{
		for (i = 0; i < srcmodel->vert_count; ++i)
		{
			if (lengthVec3Squared(srcmodel->unpack.binorms[i]) < MIN_NORMAL_LENGTH_SQR)
			{
				if (firstBadBinrm == -1)
					firstBadBinrm = i;
				++badBinormals;
			}
		}
	}

	if (badNormals || badTangents || badBinormals)
	{
		log_printf(LOG_ERRORS, "Source model \"%s\" from %s has bad tangent basis %s(%d bad normals @%d, %d bad tangents @%d, %d bad binormals @%d/%d vertices)\n",
			srcmodel->name, srcmodel->gld->filename, checkStage, badNormals, firstBadNrm, badTangents, firstBadTan, badBinormals, firstBadBinrm, srcmodel->vert_count);
	}
}

static void geo2CheckTangentBases(Geo2LoadData * gld)
{
	int j;
	for (j=0; j<gld->model_count; j++)
	{
		ModelSource *srcmodel = gld->models[j];
		modelCheckTangentBases(srcmodel, "");
	}
}

static void geo2UpdateBinsForGeoDirectInternal(const char *path)
{
	char relpath[MAX_PATH];
	char binpath[MAX_PATH];
	char mutexname[MAX_PATH];
	ThreadAgnosticMutex hMutex;
	Geo2LoadData *gld;
	FileList file_list = NULL;

	//if (!fileExists(path))
	//	return; // No bins to update, perhaps delete output bin if in development mode?

	g_binning_model_lods = true;

	fileRelativePath(path, relpath);

	geo2BinNameFromGeo2Name(relpath, binpath, ARRAY_SIZE_CHECKED(binpath));

	// Grab mutex
	makeLegalMutexName(mutexname, binpath);
	strupr(mutexname);
	hMutex = acquireThreadAgnosticMutex(mutexname);

	// Check if it exists and is up to date
	if (geo2IsBinUpToDate(binpath))
		goto cleanup;

#if _PS3
    printf("\n%s: trying to bin\n%s\n", __FUNCTION__, path);
    assert(!"cannot bin");
#else

	//waitForGetVrmlLock(true); // Causing deadlock in GetVRML

	assert(fileExists(path));

	g_geobin_updates++;

	// Build it!
	// Load geo data, collect deps, and unpack it
	gld = geo2Load(path, &file_list);
	// if we can't load the file now, it may have already been removed by another
	// process
	if (gld)
	{
		SimpleBufHandle buf;
		int timer = timerAlloc();

		if (wl_state.binAllGeos)
			geo2CheckTangentBases(gld);

		// Add other deps
		FileListInsert(&file_list, relpath, fileLastChanged(path));
		// Build all LODs and pack them
		geo2BuildAllLODs(gld);
		g_bin_timing_lods += timerElapsed(timer);
		timerFree(timer);

		// Save
		buf = SimpleBufOpenWrite(binpath, true, NULL, false, false);
		geo2SaveBin(buf, gld, &file_list, true);
		SimpleBufClose(buf);
		FolderCacheForceUpdate(folder_cache, binpath);

		// Free/destroy everything
		geo2Destroy(gld);
	}

	//releaseGetVrmlLock();
#endif

cleanup:
	FileListDestroy(&file_list);
	// Release mutex
	releaseThreadAgnosticMutex(hMutex);

	g_binning_model_lods = false;
}

// make sure 200 models don't request us to load the same bin file.
static StashTable stRequestedGeoLoadPaths;
static CRITICAL_SECTION stashTableCriticalSection;

static void __stdcall geo2UpdateBinsForGeoDirect(GeoRenderInfo *model_UNUSED, void *parent, int param_UNUSED)
{
	const char *path = parent;

	EnterCriticalSection(&stashTableCriticalSection);
	stashRemovePointer(stRequestedGeoLoadPaths,path,NULL);
	LeaveCriticalSection(&stashTableCriticalSection);
	PERFINFO_AUTO_START("doAction:LOAD_FROM_HOGG", 1);
	geo2UpdateBinsForGeoDirectInternal(path);
	PERFINFO_AUTO_STOP();
}

static void geo2FlagBinAsTouched(const char *path)
{
	char relpath[MAX_PATH];
	char binpath[MAX_PATH];
	fileRelativePath(path, relpath);
	geo2BinNameFromGeo2Name(relpath, binpath, ARRAY_SIZE_CHECKED(binpath));
	binNotifyTouchedOutputFile(binpath);
}

void geo2UpdateBinsForGeo(const char *path)
{
	path = allocAddFilename(path);
	if (gbMakeBinsAndExit) // This may hit the disk, so avoid when we can
		geo2FlagBinAsTouched(path);
	
	// no reason to do this here.  Need an init function
	if (!stRequestedGeoLoadPaths)
	{
		stRequestedGeoLoadPaths = stashTableCreateWithStringKeys(100, StashDeepCopyKeys_NeverRelease);
		InitializeCriticalSection(&stashTableCriticalSection);
	}
	if (!stashFindPointer(stRequestedGeoLoadPaths,path,NULL))
	{
		EnterCriticalSection(&stashTableCriticalSection);
		stashAddPointer(stRequestedGeoLoadPaths,path,NULL,true);
		LeaveCriticalSection(&stashTableCriticalSection);
		geoRequestBackgroundExec(geo2UpdateBinsForGeoDirect, path, NULL, (void*)path, 0, FILE_HIGH_PRIORITY); // Must be at least as high priority as heyThreadLoadAColData and heyThreadLoadAGeo so it's binned before being loaded
	}
}

void geo2UpdateBinsForModel(ModelHeader *modelheader)
{
	char path[MAX_PATH];
	changeFileExt(modelheader->filename, ".geo2", path);
	geo2UpdateBinsForGeo(path);
}

static FileScanAction updateGeosCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	if (strEndsWith(data->name, ".geo2")) {
		char relpath[MAX_PATH];
		sprintf(relpath, "%s/%s", dir, data->name);
		geo2UpdateBinsForGeo(relpath);
		(*(int*)pUserData)++;
	}
	return FSA_EXPLORE_DIRECTORY;
}

void geo2UpdateBinsForAllGeos(void)
{
	int count;
	int total;
	int timer = timerAlloc();
	loadstart_printf("Updating all object bins...");

	count = 0;
	loadstart_printf("Queuing object_library updates...");
	fileScanAllDataDirs("object_library", updateGeosCallback, &count);
	loadend_printf("done (%d files).", count);
	total = count;

	count = 0;
	loadstart_printf("Queuing character_library updates...");
	fileScanAllDataDirs("character_library", updateGeosCallback, &count);
	loadend_printf("done (%d files).", count);
	total += count;

	loadstart_printf("Waiting for updates to finish...");
	{
		int pending;
		int spaces=0;
		int total_pending=0;
		int i;
// Don't want to call OutputDebugString or memlog on these printfs!
#pragma push_macro("printf")
#undef printf
		while (pending = geoLoadsPending(true))
		{
			if (!total_pending)
				total_pending = pending;
			Sleep(1000);
			for (i=0; i<spaces; i++) 
				printf("%c", 8);
			spaces = printf("%d left (%1.0f%%)  ", pending, (total_pending - pending) *100.f/ (float)total_pending);
			ControllerScript_TemporaryPause(10, "Geo binning");
		}
		for (i=0; i<spaces; i++) 
			printf("%c", 8);
		for (i=0; i<spaces; i++) 
			printf(" ");
		for (i=0; i<spaces; i++) 
			printf("%c", 8);
#pragma pop_macro("printf")
	}
	geoForceBackgroundLoaderToFinish();
	loadend_printf("done.");

	loadend_printf("Done updating geometry bins (%d updated, %d up to date).", g_geobin_updates, total - g_geobin_updates);
	verbose_printf("BinAllGeos timing:  LODs: %fs  Cooking: %fs  Other: %fs\n", g_bin_timing_lods, g_bin_timing_cooking, timerElapsed(timer) - g_bin_timing_lods - g_bin_timing_cooking);
	timerFree(timer);
}

static FileScanAction verifyGeoBinsCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	if (strEndsWith(data->name, ".mset")) {
		char relpath[MAX_PATH];
		char fullpath[MAX_PATH];
		bool bGood;
		Geo2VerifyLogMode geoVerifyMode = wl_state.verifyAllGeoBins > 1 ? G2VLM_LogDefectiveMSets : G2VLM_LogAllAndVerify;
		sprintf(relpath, "%s/%s", dir, data->name);
		fileLocateWrite(relpath, fullpath);
		
		// Load and verify
		bGood = geo2PrintBinFileInfo(fullpath, geoVerifyMode, true);
		if (!bGood && geoVerifyMode == G2VLM_LogAllAndVerify)
		{
			printf("Deleting %s\n", fullpath);
			fileForceRemove(fullpath);
		}

		(*(int*)pUserData)++;
	}
	return FSA_EXPLORE_DIRECTORY;
}

void geo2VerifyBinsForAllGeos(void)
{
	int count=0;
	loadstart_printf("Verifying all geobins...");
	fileScanAllDataDirs("bin/geobin/cl", verifyGeoBinsCallback, &count);
	fileScanAllDataDirs("bin/geobin/ol", verifyGeoBinsCallback, &count);
	loadend_printf("done (%d files).", count);
}

void modelAddGMeshToGLD(Geo2LoadData **gld_ptr, GMesh *mesh, const char *name, const char **material_names, int material_name_count, bool no_collision, bool collision_only, bool no_tangent_space, SimpleBufHandle header_buf)
{
	int i, j, last_tex = 0, highest_tex = -1;
	ModelSource *model;
	Geo2LoadData *gld = *gld_ptr;
	GTriIdx *meshtri;
	LodModel2 *lodmodel;
	Vec3 model_min, model_max;

	if (!mesh || !mesh->tri_count)
	{
		if (header_buf)
			SimpleBufWriteU32(sizeof(U32), header_buf); // size of data written
		return;
	}

	if (!gld)
	{
		gld = *gld_ptr = calloc(1, sizeof(Geo2LoadData));
		eaCreate(&gld->models);
		eaPush(&gld->eaAllocList, &gld->models);
	}

	model = calloc(1, sizeof(ModelSource));
	model->name = strdup(name);
	model->gld = gld;
	model->no_collision = !!no_collision;
	model->collision_only = !!collision_only;

	if (no_tangent_space)
		model->process_time_flags |= MODEL_PROCESSED_NO_TANGENT_SPACE;

	eaPush(&gld->models, model);
	gld->model_count++;

	eaPush(&gld->allocList, model);
	eaPush(&gld->allocList, (void *)model->name);

	model->tex_count = gmeshSortTrisByTexID(mesh, NULL);
	model->tex_idx = calloc(sizeof(TexID), model->tex_count);

	// initialize tex ids
	meshtri = &mesh->tris[0];
	for (i = 0; i < mesh->tri_count; i++)
	{
		for (j = last_tex; j < model->tex_count; j++)
		{
			if (j > highest_tex)
			{
				model->tex_idx[j].id = meshtri->tex_id;
				highest_tex = j;
			}
			if (model->tex_idx[j].id == meshtri->tex_id)
			{
				last_tex = j;
				break;
			}
		}
		++meshtri;
	}

	if (material_name_count && material_names)
	{
		if (!gld->texnames.strings)
		{
			eaCreate(&gld->texnames.strings);
			eaPush(&gld->eaAllocList, &gld->texnames.strings);
		}

		for (i = 0; i < model->tex_count; ++i)
		{
			for (j = eaSize(&gld->texnames.strings); j < model->tex_idx[i].id; ++j)
				eaPush(&gld->texnames.strings, "");
			if (model->tex_idx[i].id == eaSize(&gld->texnames.strings))
			{
				if (model->tex_idx[i].id < material_name_count)
				{
					char *s = strdup(material_names[model->tex_idx[i].id]);
					eaPush(&gld->texnames.strings, s);
					eaPush(&gld->allocList, s);
				}
				else
				{
					eaPush(&gld->texnames.strings, "");
				}
			}
		}

		gld->texnames.count = eaSize(&gld->texnames.strings);
	}

	// calc bounds
	setVec3same(model_min, 8e16);
	setVec3same(model_max, -8e16);
	for (i = 0; i < mesh->vert_count; ++i)
	{
		vec3RunningMinMax(mesh->positions[i], model_min, model_max);
	}
	if (model_max[0] < model_min[0] || model_max[1] < model_min[1] || model_max[2] < model_min[2])
	{
		zeroVec3(model_min);
		zeroVec3(model_max);
	}

	// convert to model and pack data
	sourceModelFromGMesh(model, mesh, gld);
	geo2PackAll(model);

	lodmodel = callocStruct(LodModel2);
	eaPush(&gld->allocList, lodmodel);
	lodmodel->index = 0;
	lodmodel->error = 1; // Or 0?
	lodmodel->tri_percent = 1;
	lodmodel->model = model;
	eaPush(&model->lod_models, lodmodel);
	eaPush(&gld->eaAllocList, &model->lod_models);

	if (header_buf)
	{
		int start, end;

		start = SimpleBufTell(header_buf);
		SimpleBufWriteU32(0, header_buf); // reserve space for size of header data
		SimpleBufWriteU32(model->tex_count, header_buf);
		SimpleBufWriteU32(mesh->tri_count, header_buf);
		SimpleBufWriteU32(mesh->vert_count, header_buf);
		SimpleBufWriteF32(model_min[0], header_buf);
		SimpleBufWriteF32(model_min[1], header_buf);
		SimpleBufWriteF32(model_min[2], header_buf);
		SimpleBufWriteF32(model_max[0], header_buf);
		SimpleBufWriteF32(model_max[1], header_buf);
		SimpleBufWriteF32(model_max[2], header_buf);
		end = SimpleBufTell(header_buf);

		// write size of header data
		SimpleBufSeek(header_buf, start, SEEK_SET);
		SimpleBufWriteU32(end - start, header_buf);
		SimpleBufSeek(header_buf, end, SEEK_SET);
	}
}

void modelWriteAndFreeBinGLD(Geo2LoadData *gld, SimpleBufHandle geo_buf, bool use_texnames)
{
	FileList file_list = NULL;
	if (!gld)
		return;
	geo2SaveBin(geo_buf, gld, &file_list, use_texnames);
	geo2Destroy(gld);
}


#include "UnitSpec.h"
#include "AutoLOD.h"

static char *getDisplayStr(int size1, int size2)
{
	static char finalbuf[1024];
	char buf1[100], buf2[100];
	friendlyBytesBuf(size1, buf1);
	friendlyBytesBuf(size2, buf2);
	sprintf(finalbuf, "%15s %15s", buf1, buf2);
	return finalbuf;
}

__forceinline static int packSize(PackData *pack)
{
	return pack->packsize?pack->packsize:ABS(pack->unpacksize);
}


// system memory used
static int modelSourceGetBytesCompressed(ModelSource *model)
{
	return sizeof(ModelSource) + packSize(&model->pack.tris) + 
		packSize(&model->pack.verts) + packSize(&model->pack.verts2) + 
		packSize(&model->pack.norms) + packSize(&model->pack.norms2) + 
		packSize(&model->pack.binorms) + packSize(&model->pack.tangents) + 
		packSize(&model->pack.sts) + packSize(&model->pack.sts3) + 
		packSize(&model->pack.colors) + 
		packSize(&model->pack.weights) + packSize(&model->pack.matidxs);
}

// video memory used
static int modelSourceGetBytesUnpacked(ModelSource *model)
{
	int vert_size = 0;
	if (model->pack.verts.unpacksize)
		vert_size += sizeof(Vec3);
	if (model->pack.verts2.unpacksize)
		vert_size += sizeof(Vec3);
	if (model->pack.norms.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->pack.norms2.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->pack.binorms.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->pack.tangents.unpacksize)
		vert_size += sizeof(Vec3_Packed);
	if (model->pack.sts.unpacksize) {
		if (model->process_time_flags & MODEL_PROCESSED_HIGH_PRECISCION_TEXCOORDS)
			vert_size += sizeof(Vec2);
		else
			vert_size += sizeof(F16)*2; // size on video card
	}
	if (model->pack.sts3.unpacksize)
		vert_size += sizeof(Vec2);
	if (model->pack.colors.unpacksize)
		vert_size += 4*sizeof(U8);
	if (model->pack.weights.unpacksize)
		vert_size += sizeof(Vec4); // size on video card
	if (model->pack.matidxs.unpacksize)
		vert_size += 4*sizeof(U16); // size on video card

	return (model->pack.tris.unpacksize?(model->tri_count*3*sizeof(U32)):0) + model->vert_count*vert_size;
}

// total memory in system and video card
static int modelSourceGetBytesTotal(ModelSource *model)
{
	return modelSourceGetBytesCompressed(model) + modelSourceGetBytesUnpacked(model);
}

static void modelPackDataPrintInfo(ModelPackData *pack, int tri_count, int vert_count)
{
	printf("                %15s %15s\n", "System Mem", "Video Mem");
	printf("       Tris:    %s\n", getDisplayStr(packSize(&pack->tris), pack->tris.unpacksize?(tri_count*3*sizeof(U32)):0));
	printf("       Verts:   %s\n", getDisplayStr(packSize(&pack->verts), pack->verts.unpacksize?(vert_count*sizeof(Vec3)):0));
	printf("       Verts2:  %s\n", getDisplayStr(packSize(&pack->verts2), pack->verts2.unpacksize?(vert_count*sizeof(Vec3)):0));
	printf("       Norms:   %s\n", getDisplayStr(packSize(&pack->norms), pack->norms.unpacksize?(vert_count*sizeof(Vec3)):0));
	printf("       Norm2s:  %s\n", getDisplayStr(packSize(&pack->norms2), pack->norms2.unpacksize?(vert_count*sizeof(Vec3)):0));
	printf("       Binorms: %s\n", getDisplayStr(packSize(&pack->binorms), pack->binorms.unpacksize?(vert_count*sizeof(Vec3)):0));
	printf("       Tangnts: %s\n", getDisplayStr(packSize(&pack->tangents), pack->tangents.unpacksize?(vert_count*sizeof(Vec3)):0));
	printf("       Sts:     %s\n", getDisplayStr(packSize(&pack->sts), pack->sts.unpacksize?(vert_count*sizeof(Vec2)):0));
	printf("       Sts3:    %s\n", getDisplayStr(packSize(&pack->sts3), pack->sts3.unpacksize?(vert_count*sizeof(Vec2)):0));
	printf("       Colors:  %s\n", getDisplayStr(packSize(&pack->colors), pack->colors.unpacksize?(vert_count*4*sizeof(U8)):0));
	printf("       Wgts:    %s\n", getDisplayStr(packSize(&pack->weights), pack->weights.unpacksize?(vert_count*sizeof(Vec4)):0));
	printf("       Matid:   %s\n", getDisplayStr(packSize(&pack->matidxs), pack->matidxs.unpacksize?(vert_count*4*sizeof(U16)):0));
}

static void modelSourcePrintModelInfo(ModelSource *model)
{
	char buf[100];

	printf("\n     %-15s (%d verts, %d tris, %d materials, midpoint (%.2f, %.2f, %.2f), %.3g foot radius, %s,%s%s%s%s%s%s%s %s total memory)\n", 
		model->name, model->vert_count, model->tri_count, model->tex_count, 
		model->mid[0], model->mid[1], model->mid[2], model->radius, 
		(model->pack.weights.unpacksize)?"skinned":"unskinned",
		(model->process_time_flags&MODEL_PROCESSED_TRI_OPTIMIZATIONS)?" tri_opt,":"",
		(model->process_time_flags&MODEL_PROCESSED_HIGH_PRECISCION_TEXCOORDS)?" high_preicsion_texcoords,":"",
		(model->process_time_flags&MODEL_PROCESSED_HAS_WIND)?" has_wind,":"",
		(model->process_time_flags&MODEL_PROCESSED_HAS_TRUNK_WIND)?" has_trunk_wind,":"",
		(model->process_time_flags&MODEL_PROCESSED_HIGH_DETAIL_HIGH_LOD)?" high_detail_high_lod,":"",
		(model->process_time_flags&MODEL_PROCESSED_ALPHA_TRI_SORT)?" alpha_tri_sort,":"",
		(model->process_time_flags&MODEL_PROCESSED_VERT_COLOR_SORT)?" vert_color_sort,":"",
		friendlyBytesBuf(modelSourceGetBytesTotal(model), buf));

	modelPackDataPrintInfo(&model->pack, model->tri_count, model->vert_count);
	printf("       TOTAL:   %s\n", getDisplayStr(modelSourceGetBytesCompressed(model), modelSourceGetBytesUnpacked(model)));
	//	testGeoUnpack(model);
}

// Called from GetVrml or in the Command window
void geo2PrintFileInfo(const char *fileName)
{
	FileList file_list = NULL;
	Geo2LoadData *gld;
	int i;
	lodinfoLoad();
	gld = geo2Load(fileName, &file_list);
	if (!gld) {
		printf("Error reading file %s\n", fileName);
	} else {
		geoForceBackgroundLoaderToFinish();
		printf("File %s:\n", gld->filename);
		printf("Model version: %d\n", gld->file_format_version);
		printf("Header size: %d\nData size: %d\n", gld->headersize, gld->datasize);
		printf("    Models: %d\n", gld->model_count);
		for (i=0; i<gld->model_count; i++)
			modelSourcePrintModelInfo(gld->models[i]);
		printf("\nMaterial names (%d):", gld->texnames.count);
		for (i=0; i<gld->texnames.count; i++) 
			printf(" %s,", gld->texnames.strings[i]);
		printf("\n");
	}
	FileListDestroy(&file_list);
}

static void printGarbageVertValue(const char *binFileName, const char *modelName, F32 val)
{
	static char *last_model=NULL;
	char model[MAX_PATH];
	if (!last_model)
		last_model = calloc(MAX_PATH, 1);
	sprintf(model, "%s/%s", binFileName, modelName);
	if (stricmp(model, last_model)!=0)
	{
		strcpy_s(last_model, MAX_PATH, model);
		printf("%s : Possibly garbage vertex value : %f\n", model, val);
	}
}

static bool geo2PrintModelInfo(const char *binFileName, const char *modelname, int lod_index, bool verbose, Geo2VerifyLogMode logLevel, bool bVerify, FILE *logfile)
{
	char sbuf[100];
	ModelLODData *data;
	FILE *f;
	bool bRet=true;
	
	f = geo2OpenLODDataFileByName(binFileName);
	data = geo2LoadLODData(f, modelname, lod_index, binFileName);
	geo2CloseDataFile(f);

	if (data)
	{
		if (logLevel == G2VLM_LogAllAndVerify)
			printf("     %-15s LOD %d (%f uvd %d verts, %d tris, %d materials, %s, %s system memory)\n", 
				modelname, lod_index, data->texel_density_avg, data->vert_count, data->tri_count, data->tex_count, 
				(data->pack.weights.unpacksize)?"skinned":"unskinned", friendlyBytesBuf(data->data_size, sbuf));
		bRet = bRet && data->texel_density_avg > -10.0f;

		if (verbose && logLevel == G2VLM_LogAllAndVerify)
		{
			modelPackDataPrintInfo(&data->pack, data->tri_count, data->vert_count);
			printf("\n");
		}

		if (logfile || bVerify)
		{
			ModelLOD model = {0};
			const Vec3 *vs;
			const Vec2 *vs2;
			const U8 *cs;
			const U32 *ts;
			int i, j;

			model.data = data;

			vs = modelGetVerts(&model);
			if (vs)
			{
				if (logfile)
				{
					fprintf(logfile, "Positions {\n");
					for (i = 0; i < data->vert_count; ++i)
						fprintf(logfile, "  %f,  %f,  %f,\n", vs[i][0], vs[i][1], vs[i][2]);
					fprintf(logfile, "}\n\n");
				}
				if (bVerify)
				{
					for (i=0; i<data->vert_count; i++)
						for (j=0; j<3; j++)
							if (vs[i][j] == -65536.0 || vs[i][j] == -8388608.0)
							{
								printGarbageVertValue(binFileName, modelname, vs[i][j]);
								bRet = false;
							}
				}
				SAFE_FREE(model.unpack.verts);
			}

			vs = modelGetNorms(&model);
			if (vs)
			{
				if (logfile)
				{
					fprintf(logfile, "Normals {\n");
					for (i = 0; i < data->vert_count; ++i)
						fprintf(logfile, "  %f,  %f,  %f,\n", vs[i][0], vs[i][1], vs[i][2]);
					fprintf(logfile, "}\n\n");
				}
				if (bVerify)
				{
					for (i=0; i<data->vert_count; i++)
						for (j=0; j<3; j++)
							if (vs[i][j] == -65536.0 || vs[i][j] == -8388608.0)
							{
								printGarbageVertValue(binFileName, modelname, vs[i][j]);
								bRet = false;
							}
				}
				SAFE_FREE(model.unpack.norms);
			}

			vs = modelGetBinorms(&model);
			if (vs)
			{
				if (logfile)
				{
					fprintf(logfile, "Binormals {\n");
					for (i = 0; i < data->vert_count; ++i)
						fprintf(logfile, "  %f,  %f,  %f,\n", vs[i][0], vs[i][1], vs[i][2]);
					fprintf(logfile, "}\n\n");
				}
				if (bVerify)
				{
					for (i=0; i<data->vert_count; i++)
						for (j=0; j<3; j++)
							if (vs[i][j] == -65536.0 || vs[i][j] == -8388608.0)
							{
								printGarbageVertValue(binFileName, modelname, vs[i][j]);
								bRet = false;
							}
				}
				SAFE_FREE(model.unpack.binorms);
			}

			vs = modelGetTangents(&model);
			if (vs)
			{
				if (logfile)
				{
					fprintf(logfile, "Tangents {\n");
					for (i = 0; i < data->vert_count; ++i)
						fprintf(logfile, "  %f,  %f,  %f,\n", vs[i][0], vs[i][1], vs[i][2]);
					fprintf(logfile, "}\n\n");
				}
				if (bVerify)
				{
					for (i=0; i<data->vert_count; i++)
						for (j=0; j<3; j++)
							if (vs[i][j] == -65536.0 || vs[i][j] == -8388608.0)
							{
								printGarbageVertValue(binFileName, modelname, vs[i][j]);
								bRet = false;
							}
				}
				SAFE_FREE(model.unpack.tangents);
			}

			vs2 = modelGetSts(&model);
			if (vs2)
			{
				if (logfile)
				{
					fprintf(logfile, "Texcoords1 {\n");
					for (i = 0; i < data->vert_count; ++i)
						fprintf(logfile, "  %f,  %f,\n", vs2[i][0], vs2[i][1]);
					fprintf(logfile, "}\n\n");
				}
				if (bVerify)
				{
					for (i=0; i<data->vert_count; i++)
						for (j=0; j<2; j++)
							if (vs2[i][j] == -65536.0 || vs2[i][j] == -8388608.0)
							{
								printGarbageVertValue(binFileName, modelname, vs2[i][j]);
								bRet = false;
							}
				}
				SAFE_FREE(model.unpack.sts);
			}

			vs2 = modelGetSts3(&model);
			if (vs2)
			{
				if (logfile)
				{
					fprintf(logfile, "Texcoords2 {\n");
					for (i = 0; i < data->vert_count; ++i)
						fprintf(logfile, "  %f,  %f,\n", vs2[i][0], vs2[i][1]);
					fprintf(logfile, "}\n\n");
				}
				if (bVerify)
				{
					for (i=0; i<data->vert_count; i++)
						for (j=0; j<2; j++)
							if (vs2[i][j] == -65536.0 || vs2[i][j] == -8388608.0)
							{
								printGarbageVertValue(binFileName, modelname, vs2[i][j]);
								bRet = false;
							}
				}
				SAFE_FREE(model.unpack.sts3);
			}

			cs = modelGetColors(&model);
			if (cs)
			{
				if (logfile)
				{
					fprintf(logfile, "Colors {\n");
					for (i = 0; i < data->vert_count; ++i)
						fprintf(logfile, "  %d,  %d,  %d,  %d,\n", cs[i*4+0], cs[i*4+1], cs[i*4+2], cs[i*4+3]);
					fprintf(logfile, "}\n\n");
				}
				SAFE_FREE(model.unpack.colors);
			}

			ts = modelGetTris(&model);
			if (ts)
			{
				if (logfile)
				{
					fprintf(logfile, "Tris {\n");
					for (i = 0; i < data->tri_count; ++i)
						fprintf(logfile, "  %d,  %d,  %d,\n", ts[i*3+0], ts[i*3+1], ts[i*3+2]);
					fprintf(logfile, "}\n\n");
				}
				if (bVerify)
				{
					for (i=0; i<data->tri_count*3; i++)
						if (ts[i] > (U32)data->vert_count)
						{
							printf("%s/%s : Triangle index out of range : %d\n", binFileName, modelname, ts[i]);
							bRet = false;
						}
				}
				SAFE_FREE(model.unpack.tris);
			}
		}

		free(data);
	} else {
		if (logLevel == G2VLM_LogAllAndVerify)
			printf("     %-15s LOD %d (NULL - either use higher LOD or specified)\n", 
				modelname, lod_index);
	}
	return bRet;
}

static bool geo2PrintModelColInfo(const char *binFileName, const char *modelname, bool verbose, Geo2VerifyLogMode logLevel, bool bVerify, FILE *logfile)
{
	char sbuf1[100];
	char sbuf2[100];
	FILE *f;
	Model model_dummy = {0};
	PackData pack_data = {0};

	f = geo2OpenLODDataFileByName(binFileName);
	geo2LoadColData(f, modelname, &model_dummy, &pack_data);
	geo2CloseDataFile(f);

	if (pack_data.data_ptr)
	{
		if (logLevel == G2VLM_LogAllAndVerify)
			printf("     Cooked collision data (%s packed, %s unpacked) CRC %08x\n", 
				friendlyBytesBuf(pack_data.packsize, sbuf1),
				friendlyBytesBuf(ABS(pack_data.unpacksize), sbuf2),
				cryptAdler32(pack_data.data_ptr, pack_data.packsize?pack_data.packsize:ABS(pack_data.unpacksize)));

		if (verbose)
		{
			int unpacksize = ABS(pack_data.unpacksize);
			U32 *data = malloc(unpacksize);

			EnterCriticalSection(&model_unpack_cs);
			in_model_unpack_cs++;
			geoUnpack(&pack_data, data, modelname, binFileName, binFileName);
			in_model_unpack_cs--;
			LeaveCriticalSection(&model_unpack_cs);

			if (logLevel == G2VLM_LogAllAndVerify)
				printf("       Unpacked CRC: %08x\n", cryptAdler32((U8*)data, unpacksize));
			if (logLevel == G2VLM_LogAllAndVerify)
				printf("\n");

			if (logfile)
			{
				int i;
				fprintf(logfile, "CookedColData {\n");
				for (i = 0; i < unpacksize/4; i++)
					fprintf(logfile, "  %08x\n", data[i]);
				if (unpacksize % 4)
				{
					U8 *d2 = (U8*)data;
					fprintf(logfile, "  ");
					for (i=unpacksize & ~3; i<unpacksize; i++)
						fprintf(logfile, "%02x", (int)(d2[i]));
					fprintf(logfile, "\n");
				}

				fprintf(logfile, "}\n\n");
			}
			SAFE_FREE(data);
		}
	} else {
		if (logLevel == G2VLM_LogAllAndVerify)
			printf("     No cooked collision data.\n");
	}


	SAFE_FREE(model_dummy.collision_data.tex_idx);
	SAFE_FREE(*(char***)&model_dummy.collision_data.tex_names);
	SAFE_FREE(pack_data.data_ptr);

	return true;
}

bool geo2PrintBinFileInfo(const char *fileName, Geo2VerifyLogMode logLevel, bool bVerify) // .mset file
{
	FILE *f;
	U32 version;
	U16 modelcount;
	U32 headersize;
	U32 filesize;
	U8 *data;
	U8 *ptr;
	U8 *ptr_saved;
	U32 i;
	U32 myModelOffs=0, myModelLength=0;
	ModelLODData *ret = NULL;
	FILE *logfile = NULL;
	bool bRet=true;

	if (logLevel == G2VLM_LogAllAndVerify)
		printf("\n\n"
			"*************************\n"
			"*         Summary       *\n"
			"*************************\n\n");

	// Read header, find offset
	if (logLevel == G2VLM_LogAllAndVerify)
		printf("File %s:\n", fileName);

	filesize = fileSize(fileName);
	f = fopen(fileName, "rb");
	if (!f)
	{
		printf("Unable to load file.\n");
		return false;
	}

	// Just the header size is little endian, everything else is big
	fread(&headersize, 4, 1, f);
	headersize = endianSwapIfBig(U32, headersize);
	if (logLevel == G2VLM_LogAllAndVerify)
		printf("Header size: %d\n", headersize);
	assert(headersize <= filesize);

	ptr = data = ScratchAlloc(headersize);
	fread(data, 1, headersize - 4, f);
	fclose(f);
#define fread_u32(var) var = endianSwapIfNotBig(U32, *(U32*)ptr); ptr+=4;
#define fread_u16(var) var = endianSwapIfNotBig(U16, *(U16*)ptr); ptr+=2;
	fread_u32(version);
#if GEO2_CL_BIN_VERSION == GEO2_OL_BIN_VERSION
	if (!(version == GEO2_OL_BIN_VERSION))
#else
	if (!(version == GEO2_CL_BIN_VERSION || version == GEO2_OL_BIN_VERSION))
#endif
	{
		printf("%s: Unexpected version %d\n", fileName, version);
		if (bVerify)
			bRet = false;
		else
			assertmsgf(0, "Bad version number on %s", fileName);
	}
	if (bRet)
	{
		fread_u32(version);
		if (!(version == ParseTableCRC(parse_ModelLODData, NULL, 0) || version == 0x21c585bb))
		{
			printf("%s: Unexpected CRC %08X\n", fileName, version);
			if (bVerify)
				bRet = false;
			else
				assertmsgf(0, "Bad CRC on %s", fileName);
		}
	}

	if (bRet)
	{
		bool bAllModelsAndLODsValid = true;

		fread_u16(modelcount);
		if (logLevel == G2VLM_LogAllAndVerify)
			printf("Models: %d\n", (int)modelcount);
		ptr_saved = ptr;
		for (i=0; i<modelcount; i++)
		{
			U32 j;
			U16 lodcount;
			U16 slen;
			char *modelname;
			fread_u16(slen);
			modelname = ScratchAlloc(slen+1);
			strncpy_s(modelname, slen+1, ptr, slen);
			modelname[slen] = '\0';
			ptr += slen;
			fread_u16(lodcount);
			ptr += lodcount * 8;
			ptr += 12; // skip past collision header
			if (logLevel == G2VLM_LogAllAndVerify)
				printf("  %s : %d LODs\n", modelname, (int)lodcount);
			for (j=0; j<lodcount; j++) {
				bool bValid = geo2PrintModelInfo(fileName, modelname, j, false, logLevel, bVerify, NULL);
				if (bVerify && !bValid)
				{
					// Log only if we find the mset is defective
					if (logLevel == G2VLM_LogDefectiveMSets)
					{
						// Log the MSET filename only on the first validation error
						if (bAllModelsAndLODsValid)
							printf("File %s:\n", fileName);
						// Always log the broken model
						printf("  %s : %d LODs\n", modelname, (int)lodcount);
						geo2PrintModelInfo(fileName, modelname, j, false, G2VLM_LogAllAndVerify, bVerify, NULL);
					}
				}
				bAllModelsAndLODsValid = bRet && bAllModelsAndLODsValid;
				bRet = bRet && bValid;
			}
			bRet = geo2PrintModelColInfo(fileName, modelname, false, logLevel, bVerify, NULL) && bRet;

			ScratchFree(modelname);
		}

		if (logLevel == G2VLM_LogAllAndVerify && 1)
		{
			char fpath[MAX_PATH];
			sprintf(fpath, "%s/modeldata.log", fileTempDir());
			makeDirectoriesForFile(fpath);
			logfile = fopen(fpath, "wt");
			if (logLevel)
				printf("\nLogging all model info to %s\n", fpath);
		}

		if (logLevel == G2VLM_LogAllAndVerify)
		{
			printf("\n\n"
				"*************************\n"
				"*         Details       *\n"
				"*************************\n\n");

			ptr = ptr_saved;
			for (i=0; i<modelcount; i++)
			{
				U32 j;
				U16 lodcount;
				U16 slen;
				char *modelname;
				fread_u16(slen);
				modelname = ScratchAlloc(slen+1);
				strncpy_s(modelname, slen+1, ptr, slen);
				modelname[slen] = '\0';
				ptr += slen;
				fread_u16(lodcount);
				ptr += lodcount * 8;
				ptr += 12; // skip past collision header
				if (logLevel)
					printf("  %s : %d LODs\n", modelname, (int)lodcount);
				for (j=0; j<lodcount; j++) {
					bRet &= geo2PrintModelInfo(fileName, modelname, j, true, bVerify, logLevel, logfile);
				}
				bRet &= geo2PrintModelColInfo(fileName, modelname, true, bVerify, logLevel, logfile);
				ScratchFree(modelname);
			}
		}

		if (logfile)
			fclose(logfile);
	}

	ScratchFree(data);
	return bRet;
}

AUTO_COMMAND;
void testBinFile(const char *path)
{
	geo2UpdateBinsForGeoDirectInternal(path);
}
