/***************************************************************************



***************************************************************************/
#define GENESIS_ALLOW_OLD_HEADERS

#include "WorldCellBinning.h"
#include "WorldCellClustering.h"
#include "WorldCellStreaming.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldCell.h"
#include "WorldCellEntryPrivate.h"
#include "wlVolumes.h"
#include "WorldColl.h"
#include "WorldBounds.h"
#include "wlState.h"
#include "wlAutoLOD.h"
#include "wlTerrainPrivate.h"
#include "wlEncounter.h"
#include "RoomConn.h"
#include "wlModelBinning.h"
#include "wlGenesis.h"
#include "wlSavedTaggedGroups.h"
#include "WorldCellEntry_h_ast.h"
#include "dynWind.h"
#include "wlUGC.h"
#include "bounds.h"
#include "wlTerrain.h"

#include "FolderCache.h"
#include "error.h"
#include "hoglib.h"
#include "qsortG.h"
#include "StringCache.h"
#include "rand.h"
#include "gimmeDLLWrapper.h"
#include "FileVersionList.h"
#include "UnitSpec.h"
#include "partition_enums.h"
#include "StructPack.h"
#include "HashFunctions.h"
#include "ContinuousBuilderSupport.h"
#include "trivia.h"
#include "timing.h"
#include "crypt.h"
#include "hogutil.h"
#include "Serialize.h"
#include "logging.h"
#include "UGCProjectUtils.h"
#include "utilitieslib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

typedef struct WorldCellParsed WorldCellParsed;

#include "AutoGen/WorldCellEntry_h_ast.h"
#include "AutoGen/WorldCellStreamingPrivate_h_ast.h"
#include "Autogen/WorldLib_h_ast.h"

#define PRINT_BOUNDS 0

#define BIN_MIN_SRC_ENTRIES 4

extern char last_load_error[1024];
extern int gWriteBinManifest;
extern int debug_preserve_simplygon_images;

static int show_binning_stats, binDisableOptimizedBinWelding;

// Displays stats after making world cell bins.
AUTO_CMD_INT(show_binning_stats, worldCellShowBinStats) ACMD_COMMANDLINE ACMD_CATEGORY(Debug);

// Turns off undoing of welded bins that are not worth the overhead.
AUTO_CMD_INT(binDisableOptimizedBinWelding, binDisableOptimizedBinWelding) ACMD_COMMANDLINE ACMD_CATEGORY(Debug);

//////////////////////////////////////////////////////////////////////////

// like fileLastChanged, but skips folder cache
U32 bflFileLastChanged(const char *filename)
{
	char filename_full[MAX_PATH];
	if (FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC)
	{
		FolderNode *node;
		U32 ret;
		if ( (node=FolderCacheQuery(folder_cache, filename)) && (node->non_mirrored_file || node->needs_patching))
		{
			ret = fileLastChanged(filename);
		} else {
			fileLocateWrite(filename, filename_full);
			ret = fileLastChangedAbsolute(filename_full);
		}
		FolderNodeLeaveCriticalSection();
		return ret;
	} else {
		return fileLastChanged(filename);
	}
}

void bflAddSourceFile(BinFileList *file_list, const char *filename_in)
{
	BinFileEntry *file_entry = NULL;
	char filename[MAX_PATH];

	if (!filename_in)
		return;

	if (!file_list->source_file_hash)
		file_list->source_file_hash = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);

	strcpy(filename, filename_in);
	forwardSlashes(filename);
	fixDoubleSlashes(filename);

	if (!stashFindPointer(file_list->source_file_hash, filename, &file_entry))
	{
		file_entry = StructAlloc(parse_BinFileEntry);
		file_entry->filename = StructAllocString(filename);
		file_entry->timestamp = bflFileLastChanged(filename);
		eaPush(&file_list->source_files, file_entry);
		stashAddPointer(file_list->source_file_hash, filename, file_entry, false);
	}
}

void bflAddOutputFileEx(BinFileList *file_list, const char *filename_in, bool dont_rebuild_if_doesnt_exist)
{
	BinFileEntry *file_entry = NULL;
	char filename[MAX_PATH];

	if (!filename_in)
		return;

	if (!file_list->output_file_hash)
		file_list->output_file_hash = stashTableCreateWithStringKeys(4096, StashDeepCopyKeys_NeverRelease);

	strcpy(filename, filename_in);
	forwardSlashes(filename);
	fixDoubleSlashes(filename);

	if (!stashFindPointer(file_list->output_file_hash, filename, &file_entry))
	{
		file_entry = StructAlloc(parse_BinFileEntry);
		file_entry->filename = StructAllocString(filename);
		eaPush(&file_list->output_files, file_entry);
		stashAddPointer(file_list->output_file_hash, filename, file_entry, false);
	}

	file_entry->timestamp = bflFileLastChanged(filename);
	assert(!(file_entry->timestamp & IGNORE_EXIST_TIMESTAMP_BIT));
	if (dont_rebuild_if_doesnt_exist)
		file_entry->timestamp |= IGNORE_EXIST_TIMESTAMP_BIT;
}

static bool bflUpdateOutputFileEx(const char *filename_out, bool force_old)
{
#define READCHUNKSIZE 1024
	bool used_new = false;
	char filename_old[MAX_PATH];
	char filename_located[MAX_PATH];
	U8	mem[READCHUNKSIZE];
	int	len;
	U32 hash1[4], hash2[4];
	FILE *inFile = NULL;

	if (isProductionMode())
		return true; // Always use new file in production mode

	strcpy(filename_old, filename_out);
	strcat(filename_old, ".old");

	if(fileLocateRead(filename_out, filename_located)) {
		inFile = fopen(filename_located, "rb");
	}

	if(!inFile)
	{
		Errorf("Failed to read output bin file during bin generation: %s", filename_out);
		return true;//New file used
	}

	do {
		len = (int)fread(mem, 1, READCHUNKSIZE, inFile);
		cryptMD5Update(mem,len);
	} while(len > 0);
	cryptMD5Final(hash1);

	fclose(inFile);
	inFile = NULL;

	if(fileLocateRead(filename_old, filename_located)) {
		inFile = fopen(filename_located, "rb");
	}

	if(inFile)
	{
		do {
			len = (int)fread(mem, 1, READCHUNKSIZE, inFile);
			cryptMD5Update(mem,len);
		} while(len > 0);

		cryptMD5Final(hash2);

		fclose(inFile);
	}
	else
	{
		hash2[0] = hash2[1] = hash2[2] = hash2[3] = 0;
		force_old = false;
	}

	if (force_old || memcmp(hash1, hash2, 4*sizeof(U32)) == 0)
	{
		char file_old[MAX_PATH], file_out[MAX_PATH];
		filelog_printf("world_binning.log", "%s: KEEP OLD. Hashes match.\n", filename_out);
		verbose_printf("%s: KEEP OLD. Hashes match.\n", filename_out);
		// Delete newly created bin
		fileLocateWrite(filename_out, file_out);
		if (fileExists(file_out) && fileForceRemove(file_out) < 0)
		{
			Errorf("Could not delete new bin file during bin generation: %s", filename_out);
			return true;//New file used
		}
		// Rename old bin back
		fileLocateWrite(filename_old, file_old);
		if (rename(file_old, file_out) != 0)
		{
			Alertf("Could not rename bin file during bin generation: %s", filename_out);
			return true;//New file used
		}
		used_new = false;
	}
	else
	{
		char file_del[MAX_PATH];
		// Delete old file
		verbose_printf("%s: KEEP NEW. Hashes %08X%08X%08X%08X / %08X%08X%08X%08X\n", filename_out, hash1[0], hash1[1], hash1[2], hash1[3], hash2[0], hash2[1], hash2[2], hash2[3]);
		fileLocateWrite(filename_old, file_del);
		if (fileExists(file_del) && fileForceRemove(file_del) < 0)
		{
			Errorf("Could not delete old bin file during bin generation: %s", filename_old);
			return true;//New file used
		}
		used_new = true;
	}
	return used_new;
}

bool bflUpdateOutputFile(const char *filename_out)
{
	return bflUpdateOutputFileEx(filename_out, false);
}


static U32 version_timestamps[BFLT_COUNT];

U32 bflGetVersionTimestamp(BinFileListType type)
{
	const char *filename = NULL;
	char filename_long[MAX_PATH], filename_full[MAX_PATH];
	U32 version = 0;
	FILE *f;
	bool matches = false;

	if (version_timestamps[type])
		return version_timestamps[type];

	switch (type)
	{
		xcase BFLT_TERRAIN_BIN:
			filename = "terrain_bin.version";
			version = TERRAIN_BIN_VERSION;

		xcase BFLT_TERRAIN_ATLAS:
			filename = "terrain_atlas.version";
			version = TERRAIN_ATLAS_VERSION;

		xcase BFLT_WORLD_CELL:
			filename = "world_cell.version";
			version = WORLD_STREAMING_BIN_VERSION;

		xcase BFLT_EXTERNAL:
			filename = "external_deps.version";
			version = WORLD_EXTERNAL_BIN_VERSION;

		xdefault:
			assert(0);
	}

	sprintf(filename_long, "%s/%s", fileTempDir(), filename);
	if (f = fopen(filename_long, "rt"))
	{
		U32 disk_version;
		char line[1024];
		fgets(line, ARRAY_SIZE(line), f);
		disk_version = atol(line);
		if (disk_version == version)
			matches = true;
		fclose(f);
	}

	if (!matches)
	{
		f = fopen(filename_long, "wt");
		fprintf(f, "%u", version);
		fclose(f);
	}

	fileLocateWrite(filename_long, filename_full);
	version_timestamps[type] = bflFileLastChanged(filename_full);
	return version_timestamps[type];
}

StashTable bin_manifest_cache;


void bflFreeManifestCache(void)
{
	PERFINFO_AUTO_START_FUNC();
	stashTableDestroyEx(bin_manifest_cache, NULL, freeWrapper);
	bin_manifest_cache = NULL;
	PERFINFO_AUTO_STOP();
}


bool bflFixupAndWrite(BinFileList *file_list, const char *filename, BinFileListType type)
{
	U32 gimme_timestamp;//, version_timestamp = bflGetVersionTimestamp(type);
	U32 file_list_timestamp = 0;//version_timestamp;
	char fullname[MAX_PATH];
	int i, j;
	bool ret;
	SavedFileVersionList *pList = NULL;

	if (gWriteBinManifest)
	{
		pList = CreateFileVersionList();
	}

	if (!bin_manifest_cache)
		bin_manifest_cache = gimmeDLLCacheManifestBinFiles(filename);

	// fixup timestamps
	for (i = 0; i < eaSize(&file_list->output_files); ++i)
	{
		bool dont_rebuild_if_doesnt_exist = !!(file_list->output_files[i]->timestamp & IGNORE_EXIST_TIMESTAMP_BIT);
		U32 desired_timestamp = 0;//version_timestamp;

		if (gimmeDLLCheckBinFileMatchesManifest(bin_manifest_cache, file_list->output_files[i]->filename, &gimme_timestamp))
		{
			// fixup timestamp to be equal to the timestamp of the (identical) version checked in to gimme
			desired_timestamp = gimme_timestamp;
		}
		else if (0)
		{
			// fixup timestamp to be the maximum of the dependent file timestamps
			for (j = 0; j < eaSize(&file_list->source_files); ++j)
			{
				MAX1(desired_timestamp, file_list->source_files[j]->timestamp);
			}
		}

		if (desired_timestamp)
		{
			fileLocateWrite(file_list->output_files[i]->filename, fullname);
			fileSetTimestamp(fullname, desired_timestamp);
			file_list->output_files[i]->timestamp = desired_timestamp;
//			MAX1(file_list_timestamp, desired_timestamp);
			if (dont_rebuild_if_doesnt_exist)
				file_list->output_files[i]->timestamp |= IGNORE_EXIST_TIMESTAMP_BIT;
		}
		if (pList && !dont_rebuild_if_doesnt_exist)
		{
			AddFileVersionToList(pList, file_list->output_files[i]->filename, file_list->output_files[i]->timestamp, 0, 0, 0, 0, 0);
		}
	}

	// write
	ret = ParserWriteBinaryFile(filename, NULL, parse_BinFileList, file_list, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);

	if (pList)
	{
		char versionListPath[MAX_PATH];
		sprintf(versionListPath, "%s.manifest", filename);

		WriteFileVersionList(pList, versionListPath);
		DestroyFileVersionList(pList);
	}

	if (gimmeDLLCheckBinFileMatchesManifest(bin_manifest_cache, filename, &gimme_timestamp))
	{
		// fixup timestamp to be equal to the timestamp of the (identical) version checked in to gimme
		file_list_timestamp = gimme_timestamp;
	}

	if (file_list_timestamp)
	{
		fileLocateWrite(filename, fullname);
		fileSetTimestamp(fullname, file_list_timestamp);
	}

	return ret;
}

void bflAddDepsSourceFile(BinFileListWithCRCs *file_list, const char *filename_in)
{
	bflAddSourceFile(file_list->file_list, filename_in);
}

void bflSetDepsEncounterLayerCRC(BinFileListWithCRCs *file_list, U32 crc)
{
	file_list->encounterlayer_crc = crc;
}

void bflSetDepsGAELayerCRC(BinFileListWithCRCs *file_list, U32 crc)
{
	file_list->gaelayer_crc = crc;
}

//////////////////////////////////////////////////////////////////////////
#if DEBUG_BIN_CRC
extern void (*pUpdateCRCFromParseInfoCB)(ParseTable pti[]);
static U32 id_UpdateCRCFromParseInfo;
static FILE *fCRCLog;
static bool checkLog = 0;
U32 *cryptAdler32GetPtr();

static void binUpdateCRCFromParseInfoCB(ParseTable pti[]) {
	U32 *p = cryptAdler32GetPtr();
	U32 v = *p;
	static U32 oldv;
	char logstr[1024], outstr[1024];

	if (oldv != v)
	{
		oldv = v;
#if _PS3
		v = (v>>16)|(v<<16);
#endif
		sprintf(outstr, "%08x %s : 0x%08x\n", id_UpdateCRCFromParseInfo, (pti?pti->name:""), v);
		if (checkLog)
		{
			logstr[0] = 0;
			fgets(logstr, ARRAY_SIZE(logstr)-1, fCRCLog);
			if (strcmp(logstr, outstr) != 0)
				_DbgBreak();
		}
		else
		{
			fprintf(fCRCLog, "%s", outstr);
		}
		id_UpdateCRCFromParseInfo++;
	}
}
#endif

U32 getWorldCellParseTableCRC(bool client_and_server)
{
	U32 crc, crc2;

#if DEBUG_BIN_CRC
	pUpdateCRCFromParseInfoCB = binUpdateCRCFromParseInfoCB;
	fCRCLog = fopen("C:/WorldBinCRC.log", checkLog ? "rt" : "wt");
	assert(fCRCLog);
	printf("\n\n");
#endif

	if (wlIsServer() || client_and_server)
	{
		crc = ParseTableCRC(parse_WorldCellServerDataParsed, NULL, 0);

		crc2 = ParseTableCRC(parse_WorldZoneMapScope, NULL, 0);
		crc = burtlehash2(&crc2, 1, crc);

		crc2 = ParseTableCRC(parse_WorldRegionServerParsed, NULL, 0);
		crc = burtlehash2(&crc2, 1, crc);

		if (client_and_server)
		{
			crc2 = ParseTableCRC(parse_WorldCellClientDataParsed, NULL, 0);
			crc = burtlehash2(&crc2, 1, crc);

			crc2 = ParseTableCRC(parse_WorldRegionClientParsed, NULL, 0);
			crc = burtlehash2(&crc2, 1, crc);
		}
	}
	else
	{
		crc = ParseTableCRC(parse_WorldCellClientDataParsed, NULL, 0);

		crc2 = ParseTableCRC(parse_WorldRegionClientParsed, NULL, 0);
		crc = burtlehash2(&crc2, 1, crc);
	}

	// This should really be moved last, it gets lost depending on what CRC value is used up 'til here
	crc += gConf.uWorldCellGameCRC;

	crc2 = ParseTableCRC(parse_WorldCellWeldedDataParsed, NULL, 0);
//	printf("parse_WorldCellWeldedDataParsed = 0x%08x\n", crc2);
	crc = burtlehash2(&crc2, 1, crc);

	crc2 = ParseTableCRC(parse_WorldStreamingPackedInfo, NULL, 0);
//	printf("parse_WorldStreamingPackedInfo = 0x%08x\n", crc2);
	crc = burtlehash2(&crc2, 1, crc);

#if DEBUG_BIN_CRC
	printf("Final crc = 0x%08x\n", crc);
	pUpdateCRCFromParseInfoCB = 0;
	fclose(fCRCLog);
	fCRCLog = NULL;
#endif

	return crc;
}


// Changes to the server CRC generally do not cause patch issues for clients, but
// they do cause re-binning that can be annoying on live games.  Changes here can
// also impact UGC content, so we don't want this changing more than necessary.
//
// Feel free to change WITHOUT GETTING PERMISISON on any non-release branch, but e-mail 
// Production and Stephen D'Angelo when you do so just so they know.  
//
// Changes on release branches MUST BE APPROVED by the production team for the 
// appropriate game.
//
// The server CRCs can vary between games because there is a dynamic enum involved
#define STO_SERVER_CRC		0xec083954
#define FC_SERVER_CRC		0x7f7d842d

#define OTHER_SERVER_CRC	0xd5cbd858


// DO NOT CHANGE the client CRC or you will cause all games to rebin on the client
// and may cause substantial patches for all games.  This is a big deal, so check
// with Stephen D'Angelo before changing.
//
// A CRC change may be okay if we're certain the result is bit-wise compatible 
// (such as just adding an AST flag that doesn't change any data) 
// or that the structure changed is not used in many maps.
// Note: reordering fields does change data bit-wise.
// Note: reordering enumerated values does change bit-wise data (but added to end does not)
//
// The client CRCs can vary if gConf.uWorldCellGameCRC is different between projects.
#define FC_CLIENT_CRC    0xda651196

#define OTHER_CLIENT_CRC 0x82c85a60


// This CRC is an amalgam of the server & client CRCs, bin version numbers, encounter CRC and GAE CRC.
// It is used to determine whether we need a full rebin of all UGC project maps.
//
// Feel free to change WITHOUT GETTING PERMISSION on any non-release branch, but e-mail 
// Production and Stephen D'Angelo when you do so just so they know.  
//
// Changes on release branches MUST BE APPROVED by the production team for the 
// appropriate game.
#define STO_UGC_CRC		0x2002ff26
#define NNO_UGC_CRC		0x6302e82a


void verifyWorldCellCRCHasNotChanged(void)
{
	U32 table_crc = getWorldCellParseTableCRC(false);
	if (wlIsServer())
	{
		// NOTE: Do not disable without Stephen D'Angelo's permission
		//       See comment above with the server CRC values for details.

		if (gConf.bUserContent)
		{
			U32 ugc_crc = worldCellGetOverrideRebinCRC();
	
	
			assertmsgf( (ugc_crc == STO_UGC_CRC) ||
					(ugc_crc == NNO_UGC_CRC)
					, "UGC CRC: 0x%x and Server CRC: 0x%x changed due to change in C code or a data file loaded enumerated value used in binning.  This should never be seen on a builder or production environment or else a programmer checked in without testing.  The fix is to alter the CRC value in 'WorldCellBinning.c' with permission.", ugc_crc, table_crc);
		}

			assertmsgf((table_crc == STO_SERVER_CRC) || 
			   (table_crc == FC_SERVER_CRC) ||
				   (table_crc == OTHER_SERVER_CRC)
				   , "WorldCell server parse table CRC changed to 0x%x due to change in C code or a data file loaded enumerated value used in binning.  This should never be seen on a builder or production environment or else a programmer checked in without testing.  The fix is to alter the CRC value in 'WorldCellBinning.c' with permission.", table_crc);
	}
	else
	{
		// See comment above with the client CRC value for details.
				assertmsgf((table_crc == FC_CLIENT_CRC) ||
					(table_crc == OTHER_CLIENT_CRC)
					, "WorldCell client parse table CRC changed to 0x%x due to change in C code.  This CRC should not change without permission.", table_crc);
	}
}


//////////////////////////////////////////////////////////////////////////

static void importStringTable(WorldInfoStringTable *string_table, const WorldInfoStringTable *old_string_table)
{
	int i, max_idx = -1;

	if (!string_table->string_hash)
	{
		string_table->string_hash = stashTableCreateWithStringKeys(4096, StashDefault); // shallow copy keys
		stashAddInt(string_table->string_hash, "", 0, false);
		if (eaSize(&string_table->strings) == 0)
			eaPush(&string_table->strings, StructAllocString(""));
	}

	for (i = 1; i < eaSize(&old_string_table->strings); ++i)
	{
		const char *old_string = old_string_table->strings[i];
		if (old_string && old_string[0])
		{
			stashAddInt(string_table->string_hash, old_string, i, false);
			MAX1(max_idx, i);
		}
		else
		{
			eaiPush(&string_table->available_idxs, i);
		}
	}

	for (i = eaiSize(&string_table->available_idxs) - 1; i >= 0; --i)
	{
		if (string_table->available_idxs[i] > max_idx)
			eaiRemove(&string_table->available_idxs, i);
	}
	eaiReverse(&string_table->available_idxs);

	if (max_idx > 0)
		eaSetSize(&string_table->strings, max_idx+1);
}

static int getStringIdx(WorldStreamingPooledInfo *streaming_info, const char *str)
{
	WorldInfoStringTable *string_table = streaming_info->strings;
	int idx;

	if (!str)
		return -1;

	if (!string_table->string_hash)
	{
		string_table->string_hash = stashTableCreateWithStringKeys(4096, StashDefault); // shallow copy keys
		stashAddInt(string_table->string_hash, "", 0, false);
		if (eaSize(&string_table->strings) == 0)
			eaPush(&string_table->strings, StructAllocString(""));
	}

	if (!stashFindInt(string_table->string_hash, str, &idx))
	{
		if (eaiSize(&string_table->available_idxs) > 0)
		{
			idx = eaiPop(&string_table->available_idxs);
			assert(!string_table->strings[idx]);
			string_table->strings[idx] = StructAllocString(str);
		}
		else
		{
			idx = eaPush(&string_table->strings, StructAllocString(str));
		}
		stashAddInt(string_table->string_hash, string_table->strings[idx], idx, false);
	}
	else if (!string_table->strings[idx])
	{
		string_table->strings[idx] = StructAllocString(str);
	}

	return idx;
}

static int getModelIdx(WorldStreamingPooledInfo *streaming_info, BinFileList *file_list, const Model *model)
{
	// Don't actually depend on .geo2 file because only radius/min/max (which is in the header) is needed?
	// Incorrect: .geo2 file is needed too because materials might change without the header changing.
	char geoname[MAX_PATH];

	if (!model)
		return -1;

	changeFileExt(model->header->filename, ".geo2", geoname);
	bflAddSourceFile(file_list, geoname);
	// .geo2 should be sufficient, don't need to check the header: bflAddSourceFile(file_list, model->header->filename);
	if (model->lod_info)
	{
		ModelLODTemplate *templ = GET_REF(model->lod_info->lod_template);
		if (templ)
			bflAddSourceFile(file_list, templ->filename);
		bflAddSourceFile(file_list, model->lod_info->parsed_filename);
	}

	return getStringIdx(streaming_info, model->name);
}

static int getModelHeaderIdx(WorldStreamingPooledInfo *streaming_info, BinFileList *file_list, const ModelHeader *model_header)
{
	// Don't actually depend on .geo2 file because only radius/min/max (which is in the header) is needed?
	if (!model_header)
		return -1;
	bflAddSourceFile(file_list, model_header->filename);
	return getStringIdx(streaming_info, model_header->modelname);
}

static int getMaterialIdx(WorldStreamingPooledInfo *streaming_info, BinFileList *file_list, const Material *material)
{
	if (!material)
		return -1;

	bflAddSourceFile(file_list, materialGetData(material)->filename);
	bflAddSourceFile(file_list, materialGetData(material)->graphic_props.shader_template->filename);

	return getStringIdx(streaming_info, material->material_name);
}

static int getTextureIdx(WorldStreamingPooledInfo *streaming_info, const BasicTexture *texture)
{
	if (!texture)
		return -1;

	return getStringIdx(streaming_info, wl_state.tex_name_func(texture));
}

static int getMaterialDrawIdx(WorldStreamingPooledInfo *streaming_info, MaterialDraw *material_draw)
{
	int i;

	if (!material_draw)
		return -1;

	i = eaFind(&streaming_info->material_draws, material_draw);
	assert(i >= 0);
	return i;
}

static int getModelDrawIdx(WorldStreamingPooledInfo *streaming_info, ModelDraw *model_draw)
{
	int i;

	if (!model_draw)
		return -1;

	i = eaFind(&streaming_info->model_draws, model_draw);
	assert(i >= 0);
	return i;
}

static int getDrawableSubobjectIdx(WorldStreamingPooledInfo *streaming_info, WorldDrawableSubobject *subobject)
{
	int i;

	if (!subobject)
		return -1;

	i = eaFind(&streaming_info->subobjects, subobject);
	assert(i >= 0);
	return i;
}

static int getDrawableListIdx(WorldStreamingPooledInfo *streaming_info, WorldDrawableList *drawable_list)
{
	int i;

	if (!drawable_list)
		return -1;

	i = eaFind(&streaming_info->drawable_lists, drawable_list);
	assert(i >= 0);
	return i;
}

static int getInstanceParamListIdx(WorldStreamingPooledInfo *streaming_info, WorldInstanceParamList *param_list)
{
	int i;

	if (!param_list)
		return -1;

	i = eaFind(&streaming_info->instance_param_lists, param_list);
	assert(i >= 0);
	return i;
}

static int getMatrixIdx(WorldStreamingPooledInfo *streaming_info, const Mat4 matrix)
{
	Mat4Pooled *new_mat;
	int i;

	assert(validateMat4(matrix));
	assert(isNonZeroMat3(matrix));

	for (i = 0; i < eaSize(&streaming_info->packed_info->pooled_matrices); ++i)
	{
		if (nearSameMat4Tol(matrix, streaming_info->packed_info->pooled_matrices[i]->matrix, 0.0001f, 0.00001f))
			return i;
	}

	new_mat = StructAlloc(parse_Mat4Pooled);
	copyMat4(matrix, new_mat->matrix);
	return eaPush(&streaming_info->packed_info->pooled_matrices, new_mat);
}

static int getAndAddPooledMid(WorldStreamingPooledInfo *streaming_pooled_info, const Vec3 local_mid)
{
	int local_mid_idx = -1;
	int i;

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->shared_local_mids); ++i)
	{
		if (nearSameVec3(local_mid, streaming_pooled_info->packed_info->shared_local_mids[i]->local_mid))
		{
			local_mid_idx = i;
			break;
		}
	}

	if (local_mid_idx < 0)
	{
		WorldCellLocalMidParsed *local_mid_parsed = StructAlloc(parse_WorldCellLocalMidParsed);
		copyVec3(local_mid, local_mid_parsed->local_mid);
		local_mid_idx = eaPush(&streaming_pooled_info->packed_info->shared_local_mids, local_mid_parsed);
	}

	return local_mid_idx;
}

static int getPooledMid(WorldStreamingPooledInfo *streaming_pooled_info, const Vec3 local_mid)
{
	F32 closest_dist = FLT_MAX;
	int i, local_mid_idx = -1;

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->shared_local_mids); ++i)
	{
		F32 dist = distance3Squared(local_mid, streaming_pooled_info->packed_info->shared_local_mids[i]->local_mid);
		if (dist < closest_dist)
		{
			local_mid_idx = i;
			closest_dist = dist;
		}
	}

	assert(closest_dist < 0.002);
	assert(local_mid_idx >= 0);

	return local_mid_idx;
}

static int getSharedBoundsIdx(WorldStreamingPooledInfo *streaming_info, WorldCellEntrySharedBounds *shared_bounds)
{
	int i;

	if (!shared_bounds)
		return -1;

	i = eaFind(&streaming_info->packed_info->shared_bounds, shared_bounds);
	assert(i >= 0);
	return i;
}

static void copySpline(SplineParamsParsed *spline_dst, Mat4 *spline_matrices)
{
	copyVec3(spline_matrices[0][0], spline_dst->param0);
	copyVec3(spline_matrices[0][1], spline_dst->param1);
	copyVec3(spline_matrices[0][2], spline_dst->param2);
	copyVec3(spline_matrices[0][3], spline_dst->param3);
	copyVec3(spline_matrices[1][0], spline_dst->param4);
	copyVec3(spline_matrices[1][1], spline_dst->param5);
	copyVec3(spline_matrices[1][2], spline_dst->param6);
	copyVec3(spline_matrices[1][3], spline_dst->param7);
}

static SplineParamsParsed *dupSpline(GroupSplineParams *spline_src)
{
	SplineParamsParsed *spline_dst;

	if (!spline_src)
		return NULL;

	spline_dst = StructAlloc(parse_SplineParamsParsed);
	copySpline(spline_dst, spline_src->spline_matrices);
	return spline_dst;
}

static void copyBoundsToParsed(WorldStreamingPooledInfo *streaming_info, WorldCellEntryBoundsParsed *bounds_parsed, const WorldCellEntryBounds *bounds, const WorldCellEntrySharedBounds *shared_bounds)
{
	bounds_parsed->object_tag = bounds->object_tag;
	copyMat4(bounds->world_matrix, bounds_parsed->world_matrix);
	
	bounds_parsed->local_mid_idx = -1;

	if (!shared_bounds->use_model_bounds)
	{
		Vec3 local_mid, world_mid;
		centerVec3(shared_bounds->local_min, shared_bounds->local_max, local_mid);
		mulVecMat4(local_mid, bounds->world_matrix, world_mid);
		if (!nearSameVec3(world_mid, bounds->world_mid))
			bounds_parsed->local_mid_idx = getPooledMid(streaming_info, local_mid);
	}
}

static void copyBaseData(WorldStreamingPooledInfo *streaming_info, WorldCellEntryParsed *entry_dst, WorldCellEntry *entry_src)
{
	WorldCellEntryData *data = worldCellEntryGetData(entry_src);
	copyBoundsToParsed(streaming_info, &entry_dst->entry_bounds, &entry_src->bounds, entry_src->shared_bounds);
	entry_dst->shared_bounds_idx = getSharedBoundsIdx(streaming_info, entry_src->shared_bounds);
	entry_dst->interaction_child_idx = data->interaction_child_idx;
	entry_dst->parent_entry_uid = SAFE_MEMBER(data->parent_entry, uid);
	entry_dst->group_id = data->group_id;
}

static void copyDrawableData(WorldStreamingPooledInfo *streaming_info, WorldDrawableEntryParsed *draw_dst, WorldDrawableEntry *draw_src, bool is_near_fade)
{
	int i;
	WorldFXEntry *fx_parent = GET_REF(draw_src->fx_parent_handle);
	WorldAnimationEntry *animation_controller = GET_REF(draw_src->animation_controller_handle);
	F32 world_fade_radius = draw_src->base_entry.shared_bounds->use_model_bounds ? draw_src->world_fade_radius : quantBoundsMax(draw_src->world_fade_radius);

	copyBaseData(streaming_info, &draw_dst->base_data, &draw_src->base_entry);

	draw_dst->fade_mid_idx = -1;

	if (!nearSameF32(world_fade_radius, draw_src->base_entry.shared_bounds->radius))
		draw_dst->fade_radius = quantBoundsMax(draw_src->world_fade_radius);

	if (!nearSameVec3(draw_src->world_fade_mid, draw_src->base_entry.bounds.world_mid))
	{
		Mat4 inv_world_matrix;
		Vec3 local_fade_mid;

		invertMat4Copy(draw_src->base_entry.bounds.world_matrix, inv_world_matrix);
		mulVecMat4(draw_src->world_fade_mid, inv_world_matrix, local_fade_mid);

		draw_dst->fade_mid_idx = getPooledMid(streaming_info, local_fade_mid);
	}

	draw_dst->fx_entry_id = SAFE_MEMBER(fx_parent, id);
	draw_dst->animation_entry_id = SAFE_MEMBER(animation_controller, id);
	copyVec4(draw_src->color, draw_dst->color);
	draw_dst->bitfield.camera_facing = draw_src->camera_facing;
	draw_dst->bitfield.axis_camera_facing = draw_src->axis_camera_facing;
	draw_dst->bitfield.unlit = draw_src->unlit;
	draw_dst->bitfield.occluder = draw_src->occluder;
	draw_dst->bitfield.double_sided_occluder = draw_src->double_sided_occluder;
	draw_dst->bitfield.low_detail = draw_src->low_detail;
	draw_dst->bitfield.high_detail = draw_src->high_detail;
	draw_dst->bitfield.high_fill_detail = draw_src->high_fill_detail;
	draw_dst->bitfield.map_snap_hidden = draw_src->map_snap_hidden;
	draw_dst->bitfield.no_shadow_cast = draw_src->no_shadow_cast;
	draw_dst->bitfield.no_shadow_receive = draw_src->no_shadow_receive;
	draw_dst->bitfield.force_trunk_wind = draw_src->force_trunk_wind;
	draw_dst->bitfield.no_vertex_lighting = draw_src->no_vertex_lighting;
	draw_dst->bitfield.use_character_lighting = draw_src->use_character_lighting;

	eaSetSize(&draw_dst->lod_vertex_light_colors, eaSize(&draw_src->lod_vertex_light_colors));
	for(i = 0; i < eaSize(&draw_src->lod_vertex_light_colors); i++)
	{
		WorldDrawableEntryVertexLightColorsParsed* new_colors = StructCreate(parse_WorldDrawableEntryVertexLightColorsParsed);
		WorldDrawableEntryVertexLightColors* src_colors = draw_src->lod_vertex_light_colors[i];
		new_colors->multipler = src_colors->multipler;
		new_colors->offset = src_colors->offset;
		// transfer ownership;
		new_colors->vertex_light_colors = src_colors->vertex_light_colors;
		src_colors->vertex_light_colors = NULL;

		draw_dst->lod_vertex_light_colors[i] = new_colors;
	}

	draw_dst->draw_list_idx = getDrawableListIdx(streaming_info, draw_src->draw_list);
	draw_dst->instance_param_list_idx = getInstanceParamListIdx(streaming_info, draw_src->instance_param_list);

	draw_dst->is_near_fade = is_near_fade;
	draw_dst->is_clustered = !!(draw_src->should_cluster == CLUSTERED);
}

typedef enum ModelEntryMatrixValid
{
	MEMV_Valid,

	MEMV_Degenerate,
	MEMV_OutOfBounds
} ModelEntryMatrixValid;

// 1/10th scale per axis, plus some estimated factor of numerical error
static const F32 MODELENTRY_MINIMUM_MAT_DETERMINANT = 0.001f * 0.8f;
static const F32 MODELENTRY_MAXMIMUM_DIST_OVERFLOW_S16 = SHRT_MAX;

__forceinline bool worldCellEntryPositionValid(const Vec3 pos)
{
	return lengthVec3Squared(pos) <= MAX_PLAYABLE_DIST_ORIGIN_SQR * 4.0f && 
		fabs(pos[0]) <= MODELENTRY_MAXMIMUM_DIST_OVERFLOW_S16 &&
		fabs(pos[1]) <= MODELENTRY_MAXMIMUM_DIST_OVERFLOW_S16 &&
		fabs(pos[2]) <= MODELENTRY_MAXMIMUM_DIST_OVERFLOW_S16;
}

ModelEntryMatrixValid modelEntryMatrixValid(const Mat4 entry_matrix, F32 *pDeterminant)
{
	F32 entryDeterminant = fabs(mat4Determinant(entry_matrix));
	*pDeterminant = entryDeterminant;
	if (entryDeterminant < MODELENTRY_MINIMUM_MAT_DETERMINANT)
		return MEMV_Degenerate;
	if (!worldCellEntryPositionValid(entry_matrix[3]))
		return MEMV_OutOfBounds;

	return MEMV_Valid;
}

static void copyModelInstanceInfoToParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldModelInstanceInfoParsed *instance_dst,
	const WorldModelInstanceInfo *instance_src, const char *strModelInstanceName)
{
	Mat4 instance_matrix, inv_world_matrix;
	Vec3 local_mid;
	ModelEntryMatrixValid matrixValidity = MEMV_Valid;
	F32 matrixDet = 0.0f;

	copyVec4(instance_src->world_matrix_x, instance_dst->world_matrix_x);
	copyVec4(instance_src->world_matrix_y, instance_dst->world_matrix_y);
	copyVec4(instance_src->world_matrix_z, instance_dst->world_matrix_z);

	copyVec4(instance_src->tint_color, instance_dst->tint_color);
	instance_dst->inst_radius = instance_src->inst_radius;
	instance_dst->axis_camera_facing = instance_src->axis_camera_facing;
	instance_dst->camera_facing = instance_src->camera_facing;

	setMatRow(instance_matrix, 0, instance_dst->world_matrix_x);
	setMatRow(instance_matrix, 1, instance_dst->world_matrix_y);
	setMatRow(instance_matrix, 2, instance_dst->world_matrix_z);

	matrixValidity = modelEntryMatrixValid(instance_matrix, &matrixDet);
	if (matrixValidity != MEMV_Valid)
	{
		ErrorDetailsf("Model entry (\"%s\") at position (%.2f %.2f %.2f), volume scale (%.5f) is %s", 
			strModelInstanceName,
			instance_matrix[3][0], instance_matrix[3][1], instance_matrix[3][2], matrixDet,
			matrixValidity == MEMV_Degenerate ? "too small" : "out of bounds");
		Errorf("Model entry transform %s", matrixValidity == MEMV_Degenerate ? "too small" : "out of bounds");
		identityMat4(instance_matrix); // This should be caught earlier --TomY
	}

	invertMat4Copy(instance_matrix, inv_world_matrix);
	mulVecMat4(instance_src->world_mid, inv_world_matrix, local_mid);
	instance_dst->local_mid_idx = getPooledMid(streaming_pooled_info, local_mid);

	instance_dst->instance_param_list_idx = getInstanceParamListIdx(streaming_pooled_info, instance_src->instance_param_list);
}

static int makeBinFromEntry(WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, BinFileList *file_list, 
							IVec2 *type_histogram, WorldCellParsed *cell_dst, WorldCellEntry *entry_src, U32 *time_offset_seed, U32 *id, 
							StashTable volume_to_id, StashTable interactable_to_id)
{
	Vec3 world_min, world_max;
	int i, entry_count = 1;
	WorldCellEntryData *data;

	if (!entry_src)
		return 0;

	data = worldCellEntryGetData(entry_src);

	mulBoundsAA(entry_src->shared_bounds->local_min, entry_src->shared_bounds->local_max, entry_src->bounds.world_matrix, world_min, world_max);

	switch (entry_src->type)
	{
		xcase WCENT_VOLUME:
		{
			WorldVolumeEntry *ent_src = (WorldVolumeEntry *)entry_src;
			const char **volume_type_strings = NULL;
			U32 bit = 1;

			// do not write out parsed version of volumes for rooms, they will get recreated at load time from the room data
			if (ent_src->room)
				return 0;

			if (wlIsServer())
			{
				WorldVolumeEntryServerParsed *entry_dst = StructAlloc(parse_WorldVolumeEntryServerParsed);
				
				copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);
				if (SAFE_MEMBER(ent_src->base_entry_data.parent_entry, attached_volume_entry) == ent_src)
					entry_dst->attached_to_parent = 1;
				eaCopyStructs(&ent_src->elements, &entry_dst->elements, parse_WorldVolumeElement);
				entry_dst->indoor = ent_src->indoor;
				entry_dst->occluder = ent_src->occluder;

				for (i = 0; i < 32; ++i)
				{
					if (ent_src->volume_type_bits & bit)
					{
						const char *s = wlVolumeBitMaskToTypeName(bit);
						if (s)
							eaPush(&volume_type_strings, s);
					}
					bit = bit << 1;
				}
				eaQSortG(volume_type_strings, strCmp);
				for (i = 0; i < eaSize(&volume_type_strings); ++i)
				{
					eaiPush(&entry_dst->volume_type_string_idxs, getStringIdx(streaming_pooled_info, volume_type_strings[i]));
				}
				eaDestroy(&volume_type_strings);

				StructCopy(parse_GroupVolumePropertiesServer, &ent_src->server_volume, &entry_dst->properties, 0, 0, 0);

				entry_dst->named_volume_id = (*id)++;
				assert(stashAddInt(volume_to_id, ent_src, entry_dst->named_volume_id, false));

				if (!cell_dst->server_data)
					cell_dst->server_data = StructAlloc(parse_WorldCellServerDataParsed);
				eaPush(&cell_dst->server_data->volume_entries, entry_dst);

				type_histogram[entry_src->type][0]++;
				type_histogram[entry_src->type][1] +=	sizeof(*entry_dst) + // TODO(CD) : need a better (and more accurate) way to do this, like a textparser function that gets the recursive size
														sizeof(WorldVolumeElement) * eaSize(&entry_dst->elements) +
														eaiSize(&entry_dst->volume_type_string_idxs) * sizeof(int) +
														(entry_dst->properties.action_volume_properties ? sizeof(*entry_dst->properties.action_volume_properties) : 0) +
														(entry_dst->properties.power_volume_properties ? sizeof(*entry_dst->properties.power_volume_properties) : 0) +
														(entry_dst->properties.warp_volume_properties ? sizeof(*entry_dst->properties.warp_volume_properties) : 0) +
														(entry_dst->properties.landmark_volume_properties ? sizeof(*entry_dst->properties.landmark_volume_properties) : 0) +
														(entry_dst->properties.neighborhood_volume_properties ? sizeof(*entry_dst->properties.neighborhood_volume_properties) : 0) +
														(entry_dst->properties.interaction_volume_properties ? sizeof(*entry_dst->properties.interaction_volume_properties) : 0) +
														(entry_dst->properties.obsolete_optionalaction_properties ? sizeof(*entry_dst->properties.obsolete_optionalaction_properties) : 0) +
														(entry_dst->properties.ai_volume_properties ? sizeof(*entry_dst->properties.ai_volume_properties) : 0) +
														(entry_dst->properties.beacon_volume_properties ? sizeof(*entry_dst->properties.beacon_volume_properties) : 0) +
														(entry_dst->properties.event_volume_properties ? sizeof(*entry_dst->properties.event_volume_properties) : 0) +
														(entry_dst->properties.civilian_volume_properties ? sizeof(*entry_dst->properties.civilian_volume_properties) : 0) +
														(entry_dst->properties.mastermind_volume_properties ? sizeof(*entry_dst->properties.mastermind_volume_properties) : 0);
			}
			else
			{
				WorldVolumeEntryClientParsed *entry_dst = StructAlloc(parse_WorldVolumeEntryClientParsed);

				copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);
				eaCopyStructs(&ent_src->elements, &entry_dst->elements, parse_WorldVolumeElement);
				entry_dst->indoor = ent_src->indoor;
				entry_dst->occluder = ent_src->occluder;

				for (i = 0; i < 32; ++i)
				{
					if (ent_src->volume_type_bits & bit)
					{
						const char *s = wlVolumeBitMaskToTypeName(bit);
						if (s)
							eaPush(&volume_type_strings, s);
					}
					bit = bit << 1;
				}
				eaQSortG(volume_type_strings, strCmp);
				for (i = 0; i < eaSize(&volume_type_strings); ++i)
				{
					eaiPush(&entry_dst->volume_type_string_idxs, getStringIdx(streaming_pooled_info, volume_type_strings[i]));
				}
				eaDestroy(&volume_type_strings);

				StructCopy(parse_GroupVolumePropertiesClient, &ent_src->client_volume, &entry_dst->properties, 0, 0, 0);

				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
				eaPush(&cell_dst->client_data->volume_entries, entry_dst);

				type_histogram[entry_src->type][0]++;
				type_histogram[entry_src->type][1] +=	sizeof(*entry_dst) + // TODO(CD) : need a better (and more accurate) way to do this, like a textparser function that gets the recursive size
														sizeof(WorldVolumeElement) * eaSize(&entry_dst->elements) +
														eaiSize(&entry_dst->volume_type_string_idxs) * sizeof(int) +
														(entry_dst->properties.sky_volume_properties ? sizeof(*entry_dst->properties.sky_volume_properties) : 0) +
														(entry_dst->properties.water_volume_properties ? sizeof(*entry_dst->properties.water_volume_properties) : 0) +
														(entry_dst->properties.indoor_volume_properties ? sizeof(*entry_dst->properties.indoor_volume_properties) : 0) +
														(entry_dst->properties.sound_volume_properties ? sizeof(*entry_dst->properties.sound_volume_properties) : 0) +
														(entry_dst->properties.fx_volume_properties ? sizeof(*entry_dst->properties.fx_volume_properties) : 0);
			}
		}

		xcase WCENT_COLLISION:
		{
			WorldCollisionEntry *ent_src = (WorldCollisionEntry *)entry_src;
			WorldCollisionEntryParsed *entry_dst;

			// don't bin heightmaps or editor objects
			if (ent_src->filter.shapeGroup != WC_SHAPEGROUP_WORLD_BASIC)
			{
				return 0;
			}

			entry_dst = StructAlloc(parse_WorldCollisionEntryParsed);
			copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);
			assert(ent_src->model);
			entry_dst->model_idx = getModelIdx(streaming_pooled_info, file_list, ent_src->model);
			for(i = 0; i < eaSize(&ent_src->eaMaterialSwaps); i++)
				eaPush(&entry_dst->eaMaterialSwaps, strdup(ent_src->eaMaterialSwaps[i]));
			entry_dst->spline = dupSpline(ent_src->spline);
			copyVec3(ent_src->scale, entry_dst->scale);

			entry_dst->collision_radius = ent_src->collision_radius;
			copyVec3(ent_src->collision_min, entry_dst->collision_min);
			copyVec3(ent_src->collision_max, entry_dst->collision_max);
			entry_dst->filterBits = ent_src->filter.filterBits;

			if (wlIsServer())
			{
				if (!cell_dst->server_data)
					cell_dst->server_data = StructAlloc(parse_WorldCellServerDataParsed);
				eaPush(&cell_dst->server_data->collision_entries, entry_dst);
			}
			else
			{
				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
				eaPush(&cell_dst->client_data->collision_entries, entry_dst);
			}
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] +=	sizeof(*entry_dst) + 
													(entry_dst->spline ? sizeof(*entry_dst->spline) : 0);

			vec3RunningMin(world_min, cell_dst->coll_min);
			vec3RunningMax(world_max, cell_dst->coll_max);
		}

		xcase WCENT_ALTPIVOT:
		{
			WorldAltPivotEntry *ent_src = (WorldAltPivotEntry *)entry_src;
			WorldAltPivotEntryParsed *entry_dst = StructAlloc(parse_WorldAltPivotEntryParsed);

			copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);

			entry_dst->hand_pivot = ent_src->hand_pivot;
			entry_dst->mass_pivot = ent_src->mass_pivot;
			entry_dst->carry_anim_bit_string_idx = getStringIdx(streaming_pooled_info, ent_src->carry_anim_bit);

			if (wlIsServer())
			{
				if (!cell_dst->server_data)
					cell_dst->server_data = StructAlloc(parse_WorldCellServerDataParsed);
				eaPush(&cell_dst->server_data->altpivot_entries, entry_dst);
			}
			else
			{
				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
				eaPush(&cell_dst->client_data->altpivot_entries, entry_dst);
			}
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] += sizeof(*entry_dst);
		}

		xcase WCENT_INTERACTION:
		{
			WorldInteractionEntry *ent_src = (WorldInteractionEntry *)entry_src;
			bool inserted = false;

			assert(validateInteractionChildren(ent_src, NULL));

			if (wlIsServer())
			{
				WorldInteractionEntryServerParsed *entry_dst = StructAlloc(parse_WorldInteractionEntryServerParsed);

				copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);

				entry_dst->uid = ent_src->uid;
				entry_dst->visible_child_count = ent_src->visible_child_count;
				entry_dst->initial_selected_child = ent_src->initial_selected_child;

				entry_dst->full_interaction_properties = StructClone(parse_WorldInteractionProperties, ent_src->full_interaction_properties);

				assert(stashAddInt(interactable_to_id, ent_src, entry_dst->uid, false));

				if (!cell_dst->server_data)
					cell_dst->server_data = StructAlloc(parse_WorldCellServerDataParsed);

				// have to add this in such a way that it is located AFTER its parent entry
				for (i = 0; i < eaSize(&cell_dst->server_data->interaction_entries); i++)
				{
					if (cell_dst->server_data->interaction_entries[i]->base_data.parent_entry_uid == entry_dst->uid)
					{
						eaInsert(&cell_dst->server_data->interaction_entries, entry_dst, i);
						inserted = true;
						break;
					}
				}
				if (!inserted)
					eaPush(&cell_dst->server_data->interaction_entries, entry_dst);

				type_histogram[entry_src->type][0]++;
				type_histogram[entry_src->type][1] +=	sizeof(*entry_dst) + 
														sizeof(*entry_dst->full_interaction_properties);
			}
			else
			{
				WorldInteractionEntryClientParsed *entry_dst = StructAlloc(parse_WorldInteractionEntryClientParsed);

				copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);

				entry_dst->uid = ent_src->uid;
				entry_dst->visible_child_count = ent_src->visible_child_count;
				entry_dst->initial_selected_child = ent_src->initial_selected_child;

				entry_dst->base_interaction_properties = StructClone(parse_WorldBaseInteractionProperties, ent_src->base_interaction_properties);

				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);

				// have to add this in such a way that it is located AFTER its parent entry
				for (i = 0; i < eaSize(&cell_dst->client_data->interaction_entries); i++)
				{
					if (cell_dst->client_data->interaction_entries[i]->base_data.parent_entry_uid == entry_dst->uid)
					{
						eaInsert(&cell_dst->client_data->interaction_entries, entry_dst, i);
						inserted = true;
						break;
					}
				}
				if (!inserted)
					eaPush(&cell_dst->client_data->interaction_entries, entry_dst);

				type_histogram[entry_src->type][0]++;
				type_histogram[entry_src->type][1] +=	sizeof(*entry_dst) + 
														sizeof(*entry_dst->base_interaction_properties);
			}
		}

		xcase WCENT_SOUND:
		{
			WorldSoundEntry *ent_src = (WorldSoundEntry *)entry_src;
			WorldSoundEntryParsed *entry_dst = StructAlloc(parse_WorldSoundEntryParsed);

			copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);

			entry_dst->event_name_idx = getStringIdx(streaming_pooled_info, ent_src->event_name);
			entry_dst->excluder_str_idx = getStringIdx(streaming_pooled_info, ent_src->excluder_str);
			entry_dst->dsp_str_idx = getStringIdx(streaming_pooled_info, ent_src->dsp_str);
			entry_dst->editor_group_str_idx = getStringIdx(streaming_pooled_info, ent_src->editor_group_str);
			entry_dst->sound_group_str_idx = getStringIdx(streaming_pooled_info, ent_src->sound_group_str);
			entry_dst->sound_group_ord_idx = getStringIdx(streaming_pooled_info, ent_src->sound_group_ord);

			if (!cell_dst->client_data)
				cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
			eaPush(&cell_dst->client_data->sound_entries, entry_dst);
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] += sizeof(*entry_dst);
		}

		xcase WCENT_LIGHT:
		{
			WorldLightEntry *ent_src = (WorldLightEntry *)entry_src;
			WorldLightEntryParsed *entry_dst = StructAlloc(parse_WorldLightEntryParsed);
			WorldAnimationEntry *animation_controller = GET_REF(ent_src->animation_controller_handle);

			copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);
			
			entry_dst->light_data = StructCloneFields(parse_LightData, ent_src->light_data);
			entry_dst->animation_entry_id = SAFE_MEMBER(animation_controller, id);

			if (!cell_dst->client_data)
				cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
			eaPush(&cell_dst->client_data->light_entries, entry_dst);
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] += sizeof(*entry_dst) + sizeof(*entry_dst->light_data);
		}

		xcase WCENT_FX:
		{
			WorldFXEntry *ent_src = (WorldFXEntry *)entry_src;
			WorldFXEntryParsed *entry_dst = StructAlloc(parse_WorldFXEntryParsed);
			WorldAnimationEntry *animation_controller = GET_REF(ent_src->animation_controller_handle);
		
			copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);

			entry_dst->id = ent_src->id;
			MAX1(streaming_info->fx_entry_id_counter, ent_src->id+1);

			entry_dst->fx_condition_idx = getStringIdx(streaming_pooled_info, SAFE_MEMBER(ent_src->fx_condition, name));
			entry_dst->fx_params_idx = getStringIdx(streaming_pooled_info, ent_src->fx_params);
			entry_dst->fx_faction_idx = getStringIdx(streaming_pooled_info, ent_src->faction_name);
			entry_dst->fx_hue = ent_src->fx_hue;
			entry_dst->has_target_node = ent_src->has_target_node;
			entry_dst->target_no_anim = ent_src->target_no_anim;
			copyMat4(ent_src->target_node_mat, entry_dst->target_node_mat);
			entry_dst->fx_name_idx = getStringIdx(streaming_pooled_info, ent_src->fx_name);
			entry_dst->interaction_node_owned = ent_src->interaction_node_owned;
			entry_dst->animation_entry_id = SAFE_MEMBER(animation_controller, id);

			entry_dst->low_detail = ent_src->low_detail;
			entry_dst->high_detail = ent_src->high_detail;
			entry_dst->high_fill_detail = ent_src->high_fill_detail;

			if (ent_src->debris_model_name)
			{
				entry_dst->debris = StructAlloc(parse_WorldFXDebrisParsed);
				entry_dst->debris->model_idx = getModelIdx(streaming_pooled_info, file_list, groupModelFind(ent_src->debris_model_name, WL_FOR_WORLD));
				copyVec3(ent_src->debris_scale, entry_dst->debris->scale);
				copyVec4(ent_src->debris_tint_color, entry_dst->debris->tint_color);
				entry_dst->debris->draw_list_idx = getDrawableListIdx(streaming_pooled_info, ent_src->debris_draw_list);
				entry_dst->debris->instance_param_list_idx = getInstanceParamListIdx(streaming_pooled_info, ent_src->debris_instance_param_list);
			}

			if (!cell_dst->client_data)
				cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
			eaPush(&cell_dst->client_data->fx_entries, entry_dst);
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] +=	sizeof(*entry_dst) + 
													(entry_dst->debris ? sizeof(*entry_dst->debris) : 0);
		}

		xcase WCENT_ANIMATION:
		{
			WorldAnimationEntry *ent_src = (WorldAnimationEntry *)entry_src;
			WorldAnimationEntryParsed *entry_dst = StructAlloc(parse_WorldAnimationEntryParsed);
			WorldAnimationEntry *parent_animation_controller = GET_REF(ent_src->parent_animation_controller_handle);

			copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);

			entry_dst->id = ent_src->id;
			MAX1(streaming_info->animation_entry_id_counter, ent_src->id+1);

			StructCopy(parse_WorldAnimationProperties, &ent_src->animation_properties, &entry_dst->animation_properties, 0, 0, 0);
			if (entry_dst->animation_properties.time_offset < 0)
				entry_dst->animation_properties.time_offset = (1 + randomF32Seeded(time_offset_seed, RandType_LCG)) * 50.f;

			entry_dst->parent_animation_entry_id = SAFE_MEMBER(parent_animation_controller, id);

			if (!cell_dst->client_data)
				cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
			eaPush(&cell_dst->client_data->animation_entries, entry_dst);
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] += sizeof(*entry_dst);
		}

		xcase WCENT_WIND_SOURCE:
		{
			WorldWindSourceEntry *ent_src = (WorldWindSourceEntry *)entry_src;
			WorldWindSourceEntryParsed *entry_dst = StructAlloc(parse_WorldWindSourceEntryParsed);

			copyBaseData(streaming_pooled_info, &entry_dst->base_data, entry_src);

			StructCopyAll(parse_WorldWindSourceProperties, &ent_src->source_data, &entry_dst->source_data);

			if (!cell_dst->client_data)
				cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
			eaPush(&cell_dst->client_data->wind_source_entries, entry_dst);
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] += sizeof(*entry_dst);
		}
		xdefault:
		{
			entry_count = 0;
			assert(0);
		}
	}

	// update cell bounds
	vec3RunningMin(world_min, cell_dst->bounds_min);
	vec3RunningMax(world_max, cell_dst->bounds_max);

	return entry_count;
}

const char *modelInstanceEntryGetModelName(const WorldModelInstanceEntry *ent_src)
{
	const Model *srcModel = ent_src->model;
	if (!srcModel)
		srcModel = ent_src->base_drawable_entry.base_entry.shared_bounds->model;
	return srcModel ? srcModel->name : NULL;
}

static int makeBinFromDrawableEntry(WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, 
									BinFileList *file_list, IVec2 *type_histogram, WorldCellParsed *cell_dst, WorldCell *cell_src, 
									WorldDrawableEntry *draw_src, WorldRegionGraphicsData *region_data, 
									WorldCellDrawableDataParsed *drawable_data_ptr_override, bool is_near_fade)
{
	WorldCellEntry *entry_src;
	WorldCellEntryData *data;
	Vec3 world_min, world_max;
	int entry_count = 1;

	// don't bin editor only objects
	if (!draw_src || draw_src->editor_only)
		return 0;

	entry_src = &draw_src->base_entry;

	data = worldCellEntryGetData(entry_src);

	switch (entry_src->type)
	{
		xcase WCENT_MODEL:
		{
			WorldModelEntry *ent_src = (WorldModelEntry *)draw_src;
			WorldModelEntryParsed *ent_dst = StructAlloc(parse_WorldModelEntryParsed);

			copyDrawableData(streaming_pooled_info, &ent_dst->base_drawable, &ent_src->base_drawable_entry, is_near_fade);
			copyVec3(ent_src->model_scale, ent_dst->model_scale);
			copyVec4(ent_src->wind_params, ent_dst->wind_params);
			ent_dst->base_drawable.is_clustered = draw_src->should_cluster == CLUSTERED ? 1 : 0;

			if (drawable_data_ptr_override)
			{
				eaPush(&drawable_data_ptr_override->model_entries, ent_dst);
			}
			else
			{
				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
				eaPush(&cell_dst->client_data->drawables.model_entries, ent_dst);
			}
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] += sizeof(*ent_dst);
		}

		xcase WCENT_MODELINSTANCED:
		{
			WorldModelInstanceEntry *ent_src = (WorldModelInstanceEntry *)draw_src;
			WorldModelInstanceEntryParsed *ent_dst = StructAlloc(parse_WorldModelInstanceEntryParsed);
			int i;
			const char *strModelName = modelInstanceEntryGetModelName(ent_src); 

			copyDrawableData(streaming_pooled_info, &ent_dst->base_drawable, &ent_src->base_drawable_entry, is_near_fade);
			copyVec4(ent_src->wind_params, ent_dst->wind_params);
			ent_dst->base_drawable.is_clustered = draw_src->should_cluster == CLUSTERED ? 1 : 0;
			ent_dst->lod_idx = ent_src->lod_idx;
			
			for (i = 0; i < eaSize(&ent_src->instances); ++i)
			{
				WorldModelInstanceInfoParsed *instance_dst = StructAlloc(parse_WorldModelInstanceInfoParsed);
				copyModelInstanceInfoToParsed(streaming_pooled_info, instance_dst, ent_src->instances[i], strModelName);
				eaPush(&ent_dst->instances, instance_dst);
			}

			if (drawable_data_ptr_override)
			{
				eaPush(&drawable_data_ptr_override->model_instance_entries, ent_dst);
			}
			else
			{
				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
				eaPush(&cell_dst->client_data->drawables.model_instance_entries, ent_dst);
			}
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] +=	sizeof(*ent_dst) +
													(sizeof(WorldModelInstanceInfoParsed)) * eaSize(&ent_dst->instances);
		}

		xcase WCENT_SPLINE:
		{
			WorldSplinedModelEntry *ent_src = (WorldSplinedModelEntry *)draw_src;
			WorldSplinedModelEntryParsed *ent_dst = StructAlloc(parse_WorldSplinedModelEntryParsed);
			Mat4 spline_mats[2];

			copyDrawableData(streaming_pooled_info, &ent_dst->base_drawable, &ent_src->base_drawable_entry, is_near_fade);

			skinningMat4toMat4(ent_src->spline_mats[0], spline_mats[0]);
			skinningMat4toMat4(ent_src->spline_mats[1], spline_mats[1]);
			copySpline(&ent_dst->spline, spline_mats);

			if (drawable_data_ptr_override)
			{
				eaPush(&drawable_data_ptr_override->spline_entries, ent_dst);
			}
			else
			{
				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
				eaPush(&cell_dst->client_data->drawables.spline_entries, ent_dst);
			}
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] += sizeof(*ent_dst);
		}

		xcase WCENT_OCCLUSION:
		{
			WorldOcclusionEntry *ent_src = (WorldOcclusionEntry *)draw_src;
			WorldOcclusionEntryParsed *ent_dst = StructAlloc(parse_WorldOcclusionEntryParsed);

			copyBaseData(streaming_pooled_info, &ent_dst->base_data, &ent_src->base_drawable_entry.base_entry);

			ent_dst->model_idx = -1;
			ent_dst->type_flags = ent_src->type_flags;
			ent_dst->volume_radius = ent_src->volume_radius;
			copyVec3(ent_src->volume_min, ent_dst->volume_min);
			copyVec3(ent_src->volume_max, ent_dst->volume_max);
			ent_dst->occluder = ent_src->base_drawable_entry.occluder;
			ent_dst->double_sided_occluder = ent_src->base_drawable_entry.double_sided_occluder;

			if (ent_src->model)
			{
				assert(!!ent_src->owns_model == modelIsTemp(ent_src->model)); // JE: I think those flags should be kept in sync?
				if (ent_src->owns_model) { // JE: was: model->generic_mesh) {
					ent_dst->gmesh = modelToParsedFormat(ent_src->model);
				} else
					ent_dst->model_idx = getModelIdx(streaming_pooled_info, file_list, ent_src->model);
			}
			else
			{
				ent_dst->volume_faces = ent_src->volume_faces;
			}

			if (drawable_data_ptr_override)
			{
				eaPush(&drawable_data_ptr_override->occlusion_entries, ent_dst);
			}
			else
			{
				if (!cell_dst->client_data)
					cell_dst->client_data = StructAlloc(parse_WorldCellClientDataParsed);
				eaPush(&cell_dst->client_data->drawables.occlusion_entries, ent_dst);
			}
			type_histogram[entry_src->type][0]++;
			type_histogram[entry_src->type][1] +=	sizeof(*ent_dst) + 
													gmeshParsedGetSize(ent_dst->gmesh);
		}

		xdefault:
		{
			entry_count = 0;
			assert(0);
		}
	}

	// update cell bounds
	mulBoundsAA(entry_src->shared_bounds->local_min, entry_src->shared_bounds->local_max, entry_src->bounds.world_matrix, world_min, world_max);
	vec3RunningMin(world_min, cell_dst->bounds_min);
	vec3RunningMax(world_max, cell_dst->bounds_max);

	if (is_near_fade)
	{
		U32 near_lod_near_vis_dist_level = worldCellGetVisDistLevelNear(cell_src->region, entry_src->shared_bounds->near_lod_near_dist, entry_src);
		WorldCellParsed *child_cell_parsed;
		WorldCell *child_cell;
		IVec3 grid_pos;
		int child_idx;

		worldPosToGridPos(draw_src->world_fade_mid, grid_pos, CELL_BLOCK_SIZE);

		// update child cell bounds for all cells this near fade object will affect
		for (child_cell = cell_src, child_cell_parsed = cell_dst; 
			child_cell && child_cell_parsed && child_cell->vis_dist_level >= near_lod_near_vis_dist_level; 
			)
		{
			vec3RunningMin(world_min, child_cell_parsed->draw_min);
			vec3RunningMax(world_max, child_cell_parsed->draw_max);

			ANALYSIS_ASSUME(child_cell); // fairly sure this is correct
			child_cell = worldCellGetChildForGridPos(child_cell, grid_pos, &child_idx, false);
			child_cell_parsed = child_cell_parsed->children[child_idx];
		}
	}
	else if (!drawable_data_ptr_override)
	{
		vec3RunningMin(world_min, cell_dst->draw_min);
		vec3RunningMax(world_max, cell_dst->draw_max);
	}

	return entry_count;
}

static void updateCellBounds(WorldCellParsed *cell)
{
	int i;

	if (!cell)
		return;

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		WorldCellParsed *child = cell->children[i];
		if (child)
		{
			updateCellBounds(child);
			if (!child->no_drawables)
			{
				vec3RunningMin(child->draw_min, cell->draw_min);
				vec3RunningMax(child->draw_max, cell->draw_max);
			}
			if (!child->no_collision)
			{
				vec3RunningMin(child->coll_min, cell->coll_min);
				vec3RunningMax(child->coll_max, cell->coll_max);
			}
			if (!child->is_empty)
			{
				vec3RunningMin(child->bounds_min, cell->bounds_min);
				vec3RunningMax(child->bounds_max, cell->bounds_max);
			}
		}
	}

	if (cell->draw_max[0] < cell->draw_min[0])
	{
		setVec3same(cell->draw_min, 0);
		setVec3same(cell->draw_max, 0);
		cell->no_drawables = 1;
	}

	if (cell->coll_max[0] < cell->coll_min[0])
	{
		setVec3same(cell->coll_min, 0);
		setVec3same(cell->coll_max, 0);
		cell->no_collision = 1;
	}

	if (cell->bounds_max[0] < cell->bounds_min[0])
	{
		setVec3same(cell->bounds_min, 0);
		setVec3same(cell->bounds_max, 0);
		cell->is_empty = 1;
	}

	setVec3(cell->draw_min, quantBoundsMin(cell->draw_min[0]), quantBoundsMin(cell->draw_min[1]), quantBoundsMin(cell->draw_min[2]));
	setVec3(cell->draw_max, quantBoundsMax(cell->draw_max[0]), quantBoundsMax(cell->draw_max[1]), quantBoundsMax(cell->draw_max[2]));

	setVec3(cell->coll_min, quantBoundsMin(cell->coll_min[0]), quantBoundsMin(cell->coll_min[1]), quantBoundsMin(cell->coll_min[2]));
	setVec3(cell->coll_max, quantBoundsMax(cell->coll_max[0]), quantBoundsMax(cell->coll_max[1]), quantBoundsMax(cell->coll_max[2]));

	setVec3(cell->bounds_min, quantBoundsMin(cell->bounds_min[0]), quantBoundsMin(cell->bounds_min[1]), quantBoundsMin(cell->bounds_min[2]));
	setVec3(cell->bounds_max, quantBoundsMax(cell->bounds_max[0]), quantBoundsMax(cell->bounds_max[1]), quantBoundsMax(cell->bounds_max[2]));
}

static U32 getCellID(const BlockRange *block_range)
{
	int vis_dist_level, x, y, z;
	IVec3 size;

	rangeSize(block_range, size);
	assert(size[0] == size[1]);
	assert(size[0] == size[2]);
	vis_dist_level = log2(size[0]);
	x = block_range->min_block[0] + (1<<8);
	y = block_range->min_block[1] + (1<<8);
	z = block_range->min_block[2] + (1<<8);
	assert(x >= 0 && x < (1<<9));
	assert(y >= 0 && y < (1<<9));
	assert(z >= 0 && z < (1<<9));
	assert(vis_dist_level >= 0 && vis_dist_level < (1<<5));

	return x | (z << 9) | (y << 18) | (vis_dist_level << 27);
}

// This function must match getCellID.
__forceinline static U32 getVistDistLevelFromCellID(U32 cell_id)
{
	return cell_id >> 27;
}

static WorldCellParsed *createCell(WorldRegionCommonParsed *region, WorldCell *cell_src)
{
	WorldCellParsed *cell_dst;

	if (!cell_src)
		return NULL;

	// create and init cell
	cell_dst = StructAlloc(parse_WorldCellParsed);
	CopyStructs(&cell_dst->block_range, &cell_src->cell_block_range, 1);
	setVec3same(cell_dst->draw_min, 8e16);
	setVec3same(cell_dst->draw_max, -8e16);
	setVec3same(cell_dst->coll_min, 8e16);
	setVec3same(cell_dst->coll_max, -8e16);
	setVec3same(cell_dst->bounds_min, 8e16);
	setVec3same(cell_dst->bounds_max, -8e16);

	cell_dst->cell_id = getCellID(&cell_dst->block_range);

	region->cell_count++;
	eaPush(&region->cells, cell_dst);

	return cell_dst;
}

static void refreshAllBins(WorldCell *cell)
{
	int i;

	if (!cell)
		return;

	worldCellRefreshBins(cell);

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		refreshAllBins(cell->children[i]);
}

static void optimizeBinWelding(WorldCell *cell)
{
	int i, j;

	if (!cell)
		return;

	// must do this bottom to top, so recurse first
	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		optimizeBinWelding(cell->children[i]);

	if (!cell->parent)
		return;

	// now go through the welded bins to find ones that are not worth the overhead
	for (i = eaSize(&cell->drawable.bins) - 1; i >= 0; --i)
	{
		WorldCellWeldedBin *bin = cell->drawable.bins[i];
		WorldCellCullHeader *cell_header;
		bool can_undo = true, debug_me = false, is_near_fade[BIN_MIN_SRC_ENTRIES];
		WorldCellEntryData *entry_data[BIN_MIN_SRC_ENTRIES];

		if (eaSize(&bin->src_entries) >= BIN_MIN_SRC_ENTRIES)
			continue; // it has enough entries, keep it welded

		for (j = eaSize(&bin->src_entries) - 1; j >= 0; --j)
		{
			WorldDrawableEntry *entry = bin->src_entries[j];

			entry_data[j] = worldCellEntryGetData(&entry->base_entry);
			is_near_fade[j] = eaFind(&entry_data[j]->cell->drawable.near_fade_entries, entry) >= 0;

			if (entry->debug_me)
				debug_me = true;

			// we can only move entries up one level at a time
			if (entry_data[j]->cell != cell)
			{
				can_undo = false;
				break;
			}
		}

		if (!can_undo)
			continue;

		// this welded bin is not worth the expense, remove its entries and free it

		for (j = eaSize(&bin->src_entries) - 1; j >= 0; --j)
		{
			WorldDrawableEntry *entry = bin->src_entries[j];
			WorldCell *old_cell;

			// remove bin from entry's bin list
			eaFindAndRemoveFast(&entry_data[j]->bins, bin);

			// remove entry from old cell
			old_cell = worldRemoveCellEntryEx(&entry->base_entry, true, false);
			assert(old_cell == cell);

			// move entry to new cell
			worldAddCellEntryToCell(old_cell->parent, &entry->base_entry, entry_data[j], is_near_fade[j]);
		}

		worldCellFreeWeldedBin(bin);
		eaRemoveFast(&cell->drawable.bins, i);

		if (cell_header = worldCellGetHeader(cell))
		{
			SAFE_FREE(cell_header->welded_entry_headers);
			cell_header->welded_entries_inited = false;
		}
	}
}

static int cmpWorldCellEntryParsed(const WorldCellEntryParsed **pEntry1, const WorldCellEntryParsed **pEntry2)
{
	const WorldCellEntryParsed *entry1 = *pEntry1, *entry2 = *pEntry2;
	int t;
	
	t = entry1->shared_bounds_idx - entry2->shared_bounds_idx;
	if (t)
		return SIGN(t);

	t = entry1->group_id - entry2->group_id;
	if (t)
		return SIGN(t);

	t = cmpVec3XZY(entry1->entry_bounds.world_matrix[3], entry2->entry_bounds.world_matrix[3]);
	if (t)
		return SIGN(t);

	return memcmp(entry1->entry_bounds.world_matrix, entry2->entry_bounds.world_matrix, sizeof(Mat3));
}

static int cmpCollisionEntryParsed(const WorldCollisionEntryParsed **pEntry1, const WorldCollisionEntryParsed **pEntry2)
{
	const WorldCollisionEntryParsed *entry1 = *pEntry1, *entry2 = *pEntry2;
	int t;

	t = entry1->model_idx - entry2->model_idx;
	if (t)
		return SIGN(t);

	return cmpWorldCellEntryParsed((const WorldCellEntryParsed **)pEntry1, (const WorldCellEntryParsed **)pEntry2);
}

static int cmpFXEntryParsed(const WorldFXEntryParsed **pEntry1, const WorldFXEntryParsed **pEntry2)
{
	const WorldFXEntryParsed *entry1 = *pEntry1, *entry2 = *pEntry2;
	int t;

	t = entry1->fx_name_idx - entry2->fx_name_idx;
	if (t)
		return SIGN(t);

	t = !entry1->debris - !entry2->debris;
	if (t)
		return SIGN(t);

	if (entry1->debris)
	{
		t = entry1->debris->model_idx - entry2->debris->model_idx;
		if (t)
			return SIGN(t);

		t = entry1->debris->draw_list_idx - entry2->debris->draw_list_idx;
		if (t)
			return SIGN(t);

		t = entry1->debris->instance_param_list_idx - entry2->debris->instance_param_list_idx;
		if (t)
			return SIGN(t);
	}

	return cmpWorldCellEntryParsed((const WorldCellEntryParsed **)pEntry1, (const WorldCellEntryParsed **)pEntry2);
}

static int cmpDrawableEntryParsed(const WorldDrawableEntryParsed **pEntry1, const WorldDrawableEntryParsed **pEntry2)
{
	const WorldDrawableEntryParsed *entry1 = *pEntry1, *entry2 = *pEntry2;
	int t;

	t = entry1->draw_list_idx - entry2->draw_list_idx;
	if (t)
		return SIGN(t);

	t = entry1->instance_param_list_idx - entry2->instance_param_list_idx;
	if (t)
		return SIGN(t);

	t = entry1->bitfield_u32 - entry2->bitfield_u32;
	if (t)
		return SIGN(t);

	t = !entry1->lod_vertex_light_colors - !entry2->lod_vertex_light_colors;
	if (t)
		return SIGN(t);

	return cmpWorldCellEntryParsed((const WorldCellEntryParsed **)pEntry1, (const WorldCellEntryParsed **)pEntry2);
}

static int cmpOcclusionEntryParsed(const WorldOcclusionEntryParsed **pEntry1, const WorldOcclusionEntryParsed **pEntry2)
{
	const WorldOcclusionEntryParsed *entry1 = *pEntry1, *entry2 = *pEntry2;
	int t;

	t = entry1->model_idx - entry2->model_idx;
	if (t)
		return SIGN(t);

	t = !entry1->gmesh - !entry2->gmesh;
	if (t)
		return SIGN(t);

	return cmpDrawableEntryParsed((const WorldDrawableEntryParsed **)pEntry1, (const WorldDrawableEntryParsed **)pEntry2);
}

static GMesh * createTestClusterMesh(const WorldCell * cell)
{
	GMesh * mesh = calloc(sizeof(GMesh), 1);
	Vec3 minBBox = {cell->cell_block_range.min_block[0] * CELL_BLOCK_SIZE + 8, 
		cell->cell_block_range.min_block[1] * CELL_BLOCK_SIZE + 8, 
		cell->cell_block_range.min_block[2] * CELL_BLOCK_SIZE + 8};
	Vec3 maxBBox = {(cell->cell_block_range.max_block[0] + 1 ) * CELL_BLOCK_SIZE - 8, 
		(cell->cell_block_range.max_block[1] + 1 ) * CELL_BLOCK_SIZE - 8, 
		(cell->cell_block_range.max_block[2] + 1 ) * CELL_BLOCK_SIZE - 8};

	gmeshFromBoundingBoxWithAttribs(mesh, minBBox, maxBBox, unitmat);

	return mesh;
}

static void worldCellClusterQueueUpdateParentsOfDependency(worldCellClusterGenerateCellClusterData *parent)
{
	++parent->unprocessedChildrenRemaining;
	if (parent->parent)
		worldCellClusterQueueUpdateParentsOfDependency(parent->parent);
}

static void makeCellClusterQueue(WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, BinFileList *file_list, 
	int *histogram, IVec2 *type_histogram, WorldRegionCommonParsed *region, WorldRegion *src_region, WorldCellParsed *cell_dst, 
	WorldCell *cell_src, WorldRegionGraphicsData *region_data, worldCellClusterGenerateCellClusterData *parent, worldCellClusterGenerateCellClusterData ***cellClusterQueue)
{
	int i;
	worldCellClusterGenerateCellClusterData *myCellGenerationData = NULL;
	bool clusterCurrentCell = !cell_src->vis_dist_level || cell_src->region->cluster_options->cluster_all_levels;

	if (!cell_src)
		return;

	if (clusterCurrentCell) {
		myCellGenerationData = StructCreateDefault(worldCellClusterGenerateCellClusterData);

		myCellGenerationData->parent = parent;
		myCellGenerationData->unprocessedChildrenRemaining = 0;
		myCellGenerationData->remeshState = CRS_NotQueued;
	}

	STATIC_INFUNC_ASSERT(ARRAY_SIZE(cell_src->children) == ARRAY_SIZE(cell_dst->children));

	// recurse first so child cells exist when going to update their bounds for near fade entries
	for (i = 0; i < ARRAY_SIZE(cell_dst->children); ++i)
	{
		if (cell_dst->children[i])
		{
			if (clusterCurrentCell && !cell_src->region->cluster_options->gatherFromLeafLevel)
				++myCellGenerationData->unprocessedChildrenRemaining;
			makeCellClusterQueue(streaming_info, streaming_pooled_info, file_list, 
				histogram, type_histogram, region, src_region,
				cell_dst->children[i], cell_src->children[i], 
				region_data, myCellGenerationData, cellClusterQueue);
		}
	}
	if (clusterCurrentCell) 
	{
		myCellGenerationData->cell_src = cell_src;
		myCellGenerationData->region_dst = region;
		myCellGenerationData->clusterState = cell_src->region->cluster_options;
		myCellGenerationData->source_models = modelClusterSourceCreateDefault();

		if (cell_src->vis_dist_level != 0)
			modelClusterProcessVolumes(myCellGenerationData->source_models,src_region->cluster_options,cell_src);

		worldCellGetClusterName(SAFESTR(myCellGenerationData->clusterBaseName), cell_src);
		worldCellClusterSetupImagePaths(myCellGenerationData);

		cell_src->region->cluster_options->total_blocks++;
		eaPush(cellClusterQueue,myCellGenerationData);

		if (myCellGenerationData->clusterState->gatherFromLeafLevel && cell_src->vis_dist_level == 0)
			worldCellClusterQueueUpdateParentsOfDependency(parent);
	}
}

bool worldCellClusterQueueEntryReadyToRemesh(const worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	return clusterQueueEntry->remeshState == CRS_NotQueued && !clusterQueueEntry->unprocessedChildrenRemaining;
}

bool worldCellClusterQueueEntryTimedOut(const worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	return clusterQueueEntry->remeshState == CRS_RemeshingDistributed && 
		_time32(NULL) >= clusterQueueEntry->timeTaskRequestIssued + clusterQueueEntry->clusterState->debug.distributed_remesh_retry_timeout;
}

int allow_local_remesh = 1;
void setAllowLocalRemesh(int newVal)
{
	allow_local_remesh = newVal;
}
AUTO_CMD_INT(allow_local_remesh, debug_cluster_allow_local_remesh) ACMD_CMDLINEORPUBLIC;

void worldCellClusterQueueDecrementParentDependency(worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	InterlockedDecrement(&clusterQueueEntry->unprocessedChildrenRemaining);
	if (clusterQueueEntry->parent)
		worldCellClusterQueueDecrementParentDependency(clusterQueueEntry->parent);
}

void worldCellClusterQueueCountDependencyComplete(worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	worldCellClusterGenerateCellClusterData * parentEntry = clusterQueueEntry->parent;
	if (clusterQueueEntry->clusterState->gatherFromLeafLevel) {
		if (clusterQueueEntry->cell_src->vis_dist_level == 0 && parentEntry)
			worldCellClusterQueueDecrementParentDependency(parentEntry);
	} else {
		if (parentEntry)
			InterlockedDecrement(&parentEntry->unprocessedChildrenRemaining);
	}
}

void worldCellClusterQueueNotifyRemeshComplete(worldCellClusterGenerateCellClusterData *clusterQueueEntry, const char *taskResultFile, bool retryRemeshing)
{
	if (retryRemeshing)
	{
		// reset to unqueued state from whichever tasked processing state (local or distributed remesh) it is in
		worldCellClusterQueueEntryChangeState(clusterQueueEntry, CRS_NotQueued, clusterQueueEntry->remeshState);
	}
	else
	{
		// transition to complete state from whichever tasked processing state (local or distributed remesh) it is in
		worldCellClusterQueueEntryChangeState(clusterQueueEntry, CRS_Complete, clusterQueueEntry->remeshState);
		worldCellClusterQueueCountDependencyComplete(clusterQueueEntry);
	}
}

void worldCellClusterQueueNotifyDistributedRemeshComplete(worldCellClusterGenerateCellClusterData *clusterQueueEntry, const char *taskResultFile, bool retryRemeshing)
{
	worldCellClusterQueueNotifyRemeshComplete(clusterQueueEntry, taskResultFile, retryRemeshing);
}

bool worldCellClusterQueueEntryCancelOnAfterMaxFailures(worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	bool bForceCancel = clusterQueueEntry->buildStats.numBuilds >= clusterQueueEntry->clusterState->debug.max_retries;
	if (bForceCancel)
	{
		bool bCancelTransitionComplete = false;
		do 
		{
			bCancelTransitionComplete = worldCellClusterQueueEntryChangeState(clusterQueueEntry,
				CRS_Canceled, clusterQueueEntry->remeshState);
		} while (!bCancelTransitionComplete);
		worldCellClusterQueueCountDependencyComplete(clusterQueueEntry);
	}
	return bForceCancel;
}

bool worldCellClusterAdvanceToFinalState(worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	bool bFinalTransitionComplete = false;
	if (clusterQueueEntry->remeshState == CRS_Canceled || clusterQueueEntry->remeshState == CRS_Complete)
	{
		do 
		{
			bFinalTransitionComplete = worldCellClusterQueueEntryChangeState(clusterQueueEntry, CRS_Final,
				clusterQueueEntry->remeshState);
		} while (!bFinalTransitionComplete);
	}
	return bFinalTransitionComplete;
}

static int cmpClusterByVisDist(const void *itemLHS, const void *itemRHS)
{
	const worldCellClusterGenerateCellClusterData *clusterEntryLHS = (const worldCellClusterGenerateCellClusterData*)*(const void**)itemLHS;
	const worldCellClusterGenerateCellClusterData *clusterEntryRHS = (const worldCellClusterGenerateCellClusterData*)*(const void**)itemRHS;
	return clusterEntryLHS->cell_src->vis_dist_level - clusterEntryRHS->cell_src->vis_dist_level;
}


static void queueCellClusters(WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, BinFileList *file_list, 
	int *histogram, IVec2 *type_histogram, WorldRegionCommonParsed *region, WorldRegion *src_region, WorldCellParsed *cell_dst, 
	WorldCell *cell_src, WorldRegionGraphicsData *region_data)
{
	U32 max_distributed_remesh_tasks = src_region->cluster_options->debug.max_distributed_remesh_tasks;
	worldCellClusterGenerateCellClusterData **cellClusterQueue = NULL;
	worldCellClusterGenerateCellClusterData **cellClusterWaitingBucket = NULL;
	WorldClusterState *clusterState = src_region->cluster_options;
	U32 errorTimer = timerAlloc();
	U32 complainTimer = timerAlloc();
	float complainTime = 2.0f;
	int lastQueueSize;

	timerStart(errorTimer);
	timerStart(complainTimer);

	clusterState->numDistributedRemeshes = 0;

	cell_src->region->cluster_options->gatherFromLeafLevel |= src_region->cluster_options->debug.high_precision_remesh;
	cell_src->region->cluster_options->cluster_all_levels |= src_region->cluster_options->debug.cluster_all_levels;
	assertmsg(allow_local_remesh || max_distributed_remesh_tasks, "Neither local remeshing nor distributed remeshing is currently allowed.");
	makeCellClusterQueue(streaming_info, streaming_pooled_info, file_list, histogram, type_histogram,
		region, src_region, cell_dst, cell_src, 
		region_data, NULL, &cellClusterQueue);

	modelClusterSourceExportDefaultTextures(clusterState, NULL);

	lastQueueSize = eaSize(&cellClusterQueue);

	while (eaSize(&cellClusterQueue)) {

		int i;

		// Reset timer if the list is actually getting changed.
		if(lastQueueSize != eaSize(&cellClusterQueue)) {
			lastQueueSize = eaSize(&cellClusterQueue);
			timerStart(errorTimer);
			complainTime = 2.0f;
		}

		if(timerElapsed(errorTimer) > 1.0f && timerElapsed(complainTimer) > complainTime) {
			filelog_printf(WORLD_CLUSTER_LOG, "Build possibly stalled due to task server. %f seconds since last result.\n", timerElapsed(errorTimer));
			timerStart(complainTimer);
			complainTime *= 2.0f;
		}

		for (i = eaSize(&cellClusterQueue) - 1; i >= 0; i--)
		{
			worldCellClusterGenerateCellClusterData *currentCellCluster = cellClusterQueue[i];

			if (worldCellClusterQueueEntryTimedOut(currentCellCluster))
			{
				loadupdate_printf("Cluster %s timed out; retrying.\n", currentCellCluster->clusterBaseName);
				worldCellClusterQueueEntryForceRetry(currentCellCluster);
			}
			if (worldCellClusterQueueEntryCancelOnAfterMaxFailures(currentCellCluster))
			{
				loadupdate_printf("Cluster %s permanently failed; giving up retrying.\n", currentCellCluster->clusterBaseName);
			}
			else
			if (worldCellClusterQueueEntryReadyToRemesh(currentCellCluster))
			{
				bool allowDistribute = false;

				if (clusterState->numDistributedRemeshes < max_distributed_remesh_tasks)
				{
					allowDistribute = true;
					currentCellCluster->remeshState = CRS_RemeshingDistributed;
				}
				else
				if (allow_local_remesh)
					currentCellCluster->remeshState = CRS_Remeshing;

				if (currentCellCluster->remeshState != CRS_NotQueued)
					worldCellClusterGenerateCellCluster(currentCellCluster);
			}

			if (worldCellClusterAdvanceToFinalState(currentCellCluster))
			{
				eaRemove(&cellClusterQueue,i);
				eaPush(&cellClusterWaitingBucket, currentCellCluster);
			}
		}

	}
	timerFree(complainTimer);
	timerFree(errorTimer);
	eaDestroy(&cellClusterQueue);

	eaQSortG(cellClusterWaitingBucket, cmpClusterByVisDist);
	worldCellClusterWriteBuildStatsCSV(cellClusterWaitingBucket);

	FOR_EACH_IN_EARRAY_FORWARDS(cellClusterWaitingBucket, worldCellClusterGenerateCellClusterData, currentCellCluster);
	{
		if (currentCellCluster->bWroteClientBins)
		{
			char clusterBinHoggName[MAX_PATH];
			const char *clusterBinNameStringCached = NULL;

			// for world cell bin dependency checking, list the client cluster hogg bins as outputs
			worldCellGetClusterBinHoggPath(SAFESTR(clusterBinHoggName), currentCellCluster->cell_src);
			clusterBinNameStringCached = allocAddString(clusterBinHoggName);
			bflUpdateOutputFile(clusterBinNameStringCached);
			bflAddOutputFile(file_list, clusterBinNameStringCached);
		}

		StructDestroy(parse_worldCellClusterGenerateCellClusterData, currentCellCluster);
		cellClusterWaitingBucket[icurrentCellClusterIndex] = NULL;
	}
	FOR_EACH_END;
	eaDestroy(&cellClusterWaitingBucket);
}

static void makeCellEntryBins(WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, BinFileList *file_list, 
							  int *histogram, IVec2 *type_histogram, WorldRegionCommonParsed *region, WorldCellParsed *cell_dst, 
							  WorldCell *cell_src, WorldRegionGraphicsData *region_data, U32 *time_offset_seed, U32 *id, 
							  StashTable volume_to_id, StashTable interactable_to_id)
{
	int i, j;

	STATIC_INFUNC_ASSERT(ARRAY_SIZE(cell_src->children) == ARRAY_SIZE(cell_dst->children));

	if (!cell_src)
		return;

	if (cell_src->cluster_related)
		cell_dst->contain_cluster = 1;

	// recurse first so child cells exist when going to update their bounds for near fade entries
	for (i = 0; i < ARRAY_SIZE(cell_dst->children); ++i)
	{
		if (cell_src->children[i])
		{
			if (!cell_dst->children[i])
				cell_dst->children[i] = createCell(region, cell_src->children[i]);

			makeCellEntryBins(streaming_info, streaming_pooled_info, file_list, 
							  histogram, type_histogram, region, 
							  cell_dst->children[i], cell_src->children[i], 
							  region_data, time_offset_seed, id, 
							  volume_to_id, interactable_to_id);
		}
	}


	// make entries

	for (i = 0; i < eaSize(&cell_src->drawable.drawable_entries); ++i)
	{
		histogram[cell_src->vis_dist_level] += makeBinFromDrawableEntry(streaming_info, streaming_pooled_info, 
																		file_list, type_histogram, cell_dst, cell_src, 
																		cell_src->drawable.drawable_entries[i], region_data, 
																		NULL, false);
	}

	for (i = 0; i < eaSize(&cell_src->drawable.near_fade_entries); ++i)
	{
		histogram[cell_src->vis_dist_level] += makeBinFromDrawableEntry(streaming_info, streaming_pooled_info, 
																		file_list, type_histogram, cell_dst, cell_src, 
																		cell_src->drawable.near_fade_entries[i], region_data, 
																		NULL, true);
	}

	for (i = 0; i < eaSize(&cell_src->drawable.bins); ++i)
	{
		WorldCellDrawableDataParsed *bin_dst = StructAlloc(parse_WorldCellDrawableDataParsed);
		WorldCellWeldedBin *bin_src = cell_src->drawable.bins[i];

		for (j = 0; j < eaSize(&bin_src->drawable_entries); ++j)
		{
			histogram[cell_src->vis_dist_level] += makeBinFromDrawableEntry(streaming_info, streaming_pooled_info, 
																			file_list, type_histogram, cell_dst, cell_src, 
																			bin_src->drawable_entries[j], region_data, 
																			bin_dst, false);
		}

		if (!cell_dst->welded_data)
			cell_dst->welded_data = StructAlloc(parse_WorldCellWeldedDataParsed);
		j = eaPush(&cell_dst->welded_data->bins, bin_dst);
		assert(i == j);
	}

	for (i = 0; i < eaSize(&cell_src->collision.entries); ++i)
	{
		histogram[cell_src->vis_dist_level] += makeBinFromEntry(streaming_info, streaming_pooled_info, 
																file_list, type_histogram, cell_dst, 
																&cell_src->collision.entries[i]->base_entry, 
																time_offset_seed, id, 
																volume_to_id, interactable_to_id);
	}

	for (i = 0; i < eaSize(&cell_src->nondrawable_entries); ++i)
	{
		histogram[cell_src->vis_dist_level] += makeBinFromEntry(streaming_info, streaming_pooled_info, 
																file_list, type_histogram, cell_dst, 
																cell_src->nondrawable_entries[i], 
																time_offset_seed, id, 
																volume_to_id, interactable_to_id);
	}

	if (cell_dst->client_data)
	{
#if SORT_ENTRIES
		eaQSortG(cell_dst->client_data->altpivot_entries, cmpWorldCellEntryParsed);
		eaQSortG(cell_dst->client_data->collision_entries, cmpCollisionEntryParsed);
		eaQSortG(cell_dst->client_data->interaction_entries, cmpWorldCellEntryParsed);
		eaQSortG(cell_dst->client_data->volume_entries, cmpWorldCellEntryParsed);
		eaQSortG(cell_dst->client_data->animation_entries, cmpWorldCellEntryParsed);
		eaQSortG(cell_dst->client_data->fx_entries, cmpFXEntryParsed);
		eaQSortG(cell_dst->client_data->light_entries, cmpWorldCellEntryParsed);
		eaQSortG(cell_dst->client_data->model_entries, cmpDrawableEntryParsed);
		eaQSortG(cell_dst->client_data->model_instance_entries, cmpDrawableEntryParsed);
		eaQSortG(cell_dst->client_data->occlusion_entries, cmpOcclusionEntryParsed);
		eaQSortG(cell_dst->client_data->sound_entries, cmpWorldCellEntryParsed);
		eaQSortG(cell_dst->client_data->spline_entries, cmpDrawableEntryParsed);
		eaQSortG(cell_dst->client_data->wind_source_entries, cmpWorldCellEntryParsed);
#endif

#if DELTA_ENCODE_ENTRIES
		deltaEncodeWorldCellEntries(cell_dst->client_data->altpivot_entries);
#endif
	}
}

static void getMidFromCellEntry(WorldStreamingPooledInfo *streaming_pooled_info, WorldCellEntry *entry)
{
	WorldCellEntrySharedBounds *shared_bounds;
	WorldCellEntryBounds *bounds;
	Vec3 local_mid, world_mid;

	if (!entry)
		return;

	bounds = &entry->bounds;
	shared_bounds = entry->shared_bounds;

	if (!shared_bounds->use_model_bounds)
	{
		centerVec3(shared_bounds->local_min, shared_bounds->local_max, local_mid);
		mulVecMat4(local_mid, bounds->world_matrix, world_mid);
		if (!nearSameVec3(world_mid, bounds->world_mid))
			getAndAddPooledMid(streaming_pooled_info, local_mid);
	}
}

static void getMidFromDrawableEntry(WorldStreamingPooledInfo *streaming_pooled_info, WorldDrawableEntry *entry)
{
	Mat4 inv_world_matrix;
	Vec3 local_mid;

	if (!entry)
		return;

	getMidFromCellEntry(streaming_pooled_info, &entry->base_entry);

	if (!nearSameVec3(entry->world_fade_mid, entry->base_entry.bounds.world_mid))
	{
		invertMat4Copy(entry->base_entry.bounds.world_matrix, inv_world_matrix);
		mulVecMat4(entry->world_fade_mid, inv_world_matrix, local_mid);
		getAndAddPooledMid(streaming_pooled_info, local_mid);
	}

	if (entry->base_entry.type == WCENT_MODELINSTANCED)
	{
		WorldModelInstanceEntry *instance_entry = (WorldModelInstanceEntry *)entry;
		Mat4 instance_matrix;
		int i;

		for (i = 0; i < eaSize(&instance_entry->instances); ++i)
		{
			setMatRow(instance_matrix, 0, instance_entry->instances[i]->world_matrix_x);
			setMatRow(instance_matrix, 1, instance_entry->instances[i]->world_matrix_y);
			setMatRow(instance_matrix, 2, instance_entry->instances[i]->world_matrix_z);

			invertMat4Copy(instance_matrix, inv_world_matrix);
			mulVecMat4(instance_entry->instances[i]->world_mid, inv_world_matrix, local_mid);
			getAndAddPooledMid(streaming_pooled_info, local_mid);
		}
	}
}

static void getCellEntryMids(WorldStreamingPooledInfo *streaming_pooled_info, WorldCell *cell_src)
{
	int i, j;

	if (!cell_src)
		return;

	for (i = 0; i < eaSize(&cell_src->drawable.drawable_entries); ++i)
		getMidFromDrawableEntry(streaming_pooled_info, cell_src->drawable.drawable_entries[i]);

	for (i = 0; i < eaSize(&cell_src->drawable.near_fade_entries); ++i)
		getMidFromDrawableEntry(streaming_pooled_info, cell_src->drawable.near_fade_entries[i]);

	for (i = 0; i < eaSize(&cell_src->drawable.bins); ++i)
	{
		for (j = 0; j < eaSize(&cell_src->drawable.bins[i]->drawable_entries); ++j)
			getMidFromDrawableEntry(streaming_pooled_info, cell_src->drawable.bins[i]->drawable_entries[j]);
	}

	for (i = 0; i < eaSize(&cell_src->collision.entries); ++i)
		getMidFromCellEntry(streaming_pooled_info, &cell_src->collision.entries[i]->base_entry);

	for (i = 0; i < eaSize(&cell_src->nondrawable_entries); ++i)
		getMidFromCellEntry(streaming_pooled_info, cell_src->nondrawable_entries[i]);


	// recurse
	for (i = 0; i < ARRAY_SIZE(cell_src->children); ++i)
	{
		if (cell_src->children[i])
			getCellEntryMids(streaming_pooled_info, cell_src->children[i]);
	}
}

static int cmpLocalMid(const WorldCellLocalMidParsed *local_mid1, const WorldCellLocalMidParsed *local_mid2)
{
	return cmpVec3XYZ(local_mid1->local_mid, local_mid2->local_mid);
}

static F32 distLocalMid(const WorldCellLocalMidParsed *local_mid1, const WorldCellLocalMidParsed *local_mid2)
{
	return distance3Squared(local_mid1->local_mid, local_mid2->local_mid);
}

typedef struct LightTraverseData
{
	int iPartitionIdx;
	GfxLight **all_lights;
	WorldVolume **all_volumes;
} LightTraverseData;

static bool createLightsAndVolumes(LightTraverseData *data, GroupDef *def, GroupInfo *streaming_info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	if (!needs_entry)
		return true;

	if (groupHasLight(def))
	{
		LightData *light_data = groupGetLightData(inherited_info->parent_defs, streaming_info->world_matrix);
		GfxLight *light;
		light_data->region = streaming_info->region;
		light = wl_state.update_light_func(NULL, light_data, light_data->outer_radius * LIGHT_RADIUS_VIS_MULTIPLIER * streaming_info->lod_scale, NULL);
		eaPush(&data->all_lights, light);
		StructDestroySafe(parse_LightData, &light_data);
	}

	if (groupIsVolumeType(def, "Occluder") || groupIsVolumeType(def, "SkyFade"))
	{
		U32 volume_type_bits = 0;
		int i;

		if(def->property_structs.hull) {
			for (i = 0; i < eaSize(&def->property_structs.hull->ppcTypes); ++i)
				volume_type_bits |= wlVolumeTypeNameToBitMask(def->property_structs.hull->ppcTypes[i]);
		}

		if (volume_type_bits)
		{
			WorldVolume *volume;
			WorldVolumeEntry *volume_entry = createWorldVolumeEntry(def, streaming_info, volume_type_bits);
			GroupVolumeProperties *props;
			//worldEntryInit(&volume_entry->base_entry, streaming_info, cell_entry_list, def, !in_editor);
			assert(!volume_entry->base_entry.shared_bounds->ref_count);
			volume_entry->base_entry.shared_bounds = lookupSharedBounds(streaming_info->zmap, volume_entry->base_entry.shared_bounds);

			props = def->property_structs.volume;
			if (props && props->eShape == GVS_Sphere)
				volume = wlVolumeCreateSphere(data->iPartitionIdx, volume_type_bits, volume_entry, streaming_info->world_matrix, zerovec3, props->fSphereRadius);
			else if(props && props->eShape == GVS_Box)
				volume = wlVolumeCreateBox(data->iPartitionIdx, volume_type_bits, volume_entry, streaming_info->world_matrix, props->vBoxMin, props->vBoxMax, def->property_structs.physical_properties.iOccluderFaces);
			else 
				volume = wlVolumeCreateBox(data->iPartitionIdx, volume_type_bits, volume_entry, streaming_info->world_matrix, zerovec3, zerovec3, def->property_structs.physical_properties.iOccluderFaces);
			eaPush(&data->all_volumes, volume);
		}
	}

	return true;
}

static bool worldCellEntryHasStaticLighting(const WorldCellEntry *entry)
{
	return entry->type == WCENT_MODEL || 
		entry->type == WCENT_MODELINSTANCED || entry->type == WCENT_SPLINE;
}

static void applyStaticLighting(WorldCellEntry **cell_entries, WorldRegion *region, BinFileList *file_list)
{
	int i;
	for (i = 0; i < eaSize(&cell_entries); ++i)
	{
		WorldCellEntry *entry = cell_entries[i];
		if (worldCellEntryHasStaticLighting(entry))
		{
			WorldDrawableEntry *draw_entry = (WorldDrawableEntry*)entry;
			wl_state.compute_static_lighting_func(draw_entry, region, file_list);
		}
	}
}

static void freeStaticLightingData(WorldCellEntry **cell_entries)
{
	int i;
	for (i = 0; i < eaSize(&cell_entries); ++i)
	{
		WorldCellEntry *entry = cell_entries[i];
		if (worldCellEntryHasStaticLighting(entry))
		{
			WorldDrawableEntry *draw_entry = (WorldDrawableEntry*)entry;
			if (draw_entry->light_cache)
			{
				wl_state.free_static_light_cache_func(draw_entry->light_cache);
				draw_entry->light_cache = NULL;
			}
		}
	}
}

static void stashGetKeysAsSortedEArray(StashTable st, char ***eaStrings)
{
	StashTableIterator iter;
	StashElement pElem;
	stashGetIterator(st, &iter);
	while (stashGetNextElement(&iter, &pElem)) {
		char *str = stashElementGetKey(pElem);
		eaPush(eaStrings, str);
	}
	eaQSort(*eaStrings, strCmp); 
}

static struct
{
	StashTable stMaterialDeps;
	StashTable stTextureDeps;
	StashTable stSkyDeps;
} world_deps;

static void worldCellDepsFinalize(BinFileList *file_list, const char *output_filename)
{
	if (wlIsClient())
	{
		// Write dependency streaming_info
		char filename[MAX_PATH];
		WorldDependenciesList *list;
		WorldDependenciesParsed *data;

		list = StructAlloc(parse_WorldDependenciesList);
		data = StructAlloc(parse_WorldDependenciesParsed);
		eaPush(&list->deps, data);

		stashGetKeysAsSortedEArray(world_deps.stMaterialDeps, &data->material_deps);
		stashGetKeysAsSortedEArray(world_deps.stTextureDeps, &data->texture_deps);
		stashGetKeysAsSortedEArray(world_deps.stSkyDeps, &data->sky_deps);

		changeFileExt(output_filename, ".deps", filename);
		ParserWriteTextFile(filename, parse_WorldDependenciesList, list, 0, 0);
		bflAddOutputFile(file_list, filename);

		StructDestroy(parse_WorldDependenciesList, list);

		stashTableDestroy(world_deps.stMaterialDeps);
		stashTableDestroy(world_deps.stTextureDeps);
		stashTableDestroy(world_deps.stSkyDeps);
		world_deps.stMaterialDeps = NULL;
		world_deps.stTextureDeps = NULL;
		world_deps.stSkyDeps = NULL;
	}
}

static void worldCellDepsReset(void)
{
	if (wlIsClient()) {
		// Setup streaming_info to save dependency information
		world_deps.stMaterialDeps = stashTableCreateWithStringKeys(128, StashDefault);
		world_deps.stTextureDeps = stashTableCreateWithStringKeys(128, StashDefault);
		world_deps.stSkyDeps= stashTableCreateWithStringKeys(16, StashDefault);
		//stashAddInt(world_deps.stMaterialDeps, "Testing", 1, false);
	}
}

static const char *disallowed_world_material_templates[] =
{
	"DefaultTemplate",
	"ErrorTemplate",
	"TerrainMaterial",
	"TerrainEdge",
	"TerrainDebug",
	"Terrain_0Detail",
	"Terrain_1Detail",
	"Terrain_2Detail",
	"Terrain_3Detail"
};

void worldDepsReportMaterial(const char *material_name, GroupDef *def)
{
	if (world_deps.stMaterialDeps && material_name && material_name[0])
	{
		const MaterialData *mat_data = materialFindData(material_name);
		material_name = allocAddString(material_name);
		stashAddInt(world_deps.stMaterialDeps, material_name, 1, false);

		if (mat_data && def)
		{
			int i;
			for (i = 0; i < ARRAY_SIZE(disallowed_world_material_templates); ++i)
			{
				if (stricmp(mat_data->graphic_props.default_fallback.shader_template_name, disallowed_world_material_templates[i])==0)
				{
					ErrorFilenamef(def->filename, "Object \"%s\" uses material \"%s\" which uses a disallowed material template \"%s\".", def->name_str, material_name, disallowed_world_material_templates[i]);
				}
			}
		}
	}
}

void worldDepsReportTexture(const char *texture_name)
{
	if (world_deps.stTextureDeps && texture_name && texture_name[0])
	{
		texture_name = allocAddString(texture_name);
		stashAddInt(world_deps.stTextureDeps, texture_name, 1, false);
	}
}

void worldDepsReportSky(const char *sky_name)
{
	if (world_deps.stSkyDeps && sky_name && sky_name[0])
	{
		sky_name = allocAddString(sky_name);
		stashAddInt(world_deps.stSkyDeps, sky_name, 1, false);
	}
}

//extern ErrorCallback errorfCallback;
bool had_errors;

static void worldMoveMapSnapImagesToHogg(HogFile *hog_file, const char *image_name, const char *backup_hog_filename, const char *zmap_path, bool down_rez)
{
	U8 *file_data;
	int file_size;
	U8 *backup_file_data;
	int backup_file_size;
	char image_filename[MAX_PATH];
	char backup_image_filename[MAX_PATH];
	sprintf(image_filename, "%s/bin/geobin/%s/map_snap_images/%s", fileTempDir(), zmap_path, image_name);
	file_data = fileAlloc(image_filename, &file_size);
	assert(file_data);
	if(backup_hog_filename)
	{
		sprintf(backup_image_filename, "#%s#%s", backup_hog_filename, image_name);
		backup_file_data = fileAlloc(backup_image_filename, &backup_file_size);
		if(backup_file_data && backup_file_size == file_size && wl_state.gfx_update_map_photo)
		{
			//Copy over any blocks that are nearly the same to help keep bins consistent
			wl_state.gfx_update_map_photo(file_data, backup_file_data, file_size);
		}
		SAFE_FREE(backup_file_data);
	}
	if (down_rez && wl_state.gfx_downrez_map_photo)
	{
		wl_state.gfx_downrez_map_photo(file_data, &file_size);
	}
	hogFileModifyUpdateNamed(hog_file, image_name, file_data, file_size, 0, NULL);
}

static void catchError(ErrorMessage *errMsg, void *userdata)
{
	had_errors = true;
	ErrorfPopCallback();
	ErrorfCallCallback(errMsg);
	ErrorfPushCallback(catchError, userdata);
}

void zoneMapClusterDepFileName(ZoneMap *zMap, char* filename, S32 filename_size)
{
	const char *zmapFilename = zmapGetFilename(zMap);
	worldGetTempBaseDir(zmapFilename, SAFESTR2(filename));
	strcat_s(SAFESTR2(filename), "/cluster.dep");
}

void worldCellClusterClear(WorldCell *cell)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cell->children); i++)
	{
		if (cell->children[i])
			worldCellClusterClear(cell->children[i]);
	}
	worldCellCloseClusterBins(cell);
}

static void worldCellMakeMapPhotos(ZoneMap *zmap, WorldStreamingInfo *streaming_info, BinFileList *file_list, const char *base_dir,
		char *map_snap_hog_filename, size_t map_snap_hog_filename_size, char *map_snap_hog_mini_filename, size_t map_snap_hog_mini_filename_size)
{
	char map_snap_hog_filename_full[MAX_PATH];
	char map_snap_hog_mini_filename_full[MAX_PATH];
	char map_snap_backup_hog_filename[MAX_PATH];
	HogFile *map_snap_hog_file;
	HogFile *map_snap_mini_hog_file = NULL;
	HogFile *map_snap_backup_hog_file=NULL;
	char partition_name[256];
	bool hog_created;
	int err_return;
	bool prev_disable_anim_flag;
	bool prev_disable_associated_regions_flag;
	F32 prev_time_step;
	int partition_cnt = 0;
	char **map_snap_output_list=NULL;
	bool backup_exists = false;
	bool is_space_map = false;
	int i, j, k, l;

	loadstart_printf("Taking Map Photos...");
	SendStringToCB(CBSTRING_COMMENT, "Taking map photos for %s", zmapInfoGetPublicName(zmapGetInfo(zmap)));

	for( i=0; i < eaSize(&streaming_info->regions_parsed); i++ )
	{
		//Fill list for taking images
		WorldRegionClientParsed *region = (WorldRegionClientParsed *)streaming_info->regions_parsed[i];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region->common.region_name);

		mapSnapCalculateRegionData(region_src);
		region->mapsnap_data = region_src->mapsnap_data;

		if(region->conn_graph)
		{
			if(worldRegionGetType(region_src) == WRT_Space)
				is_space_map = true;
			for( j=0; j < eaSize(&region->conn_graph->rooms); j++ )
			{
				RoomClientParsed *room = region->conn_graph->rooms[j];
				ZoneMapLayer *layer = zmapGetLayer(zmap, room->layer_idx);
				for( k=0; k < eaSize(&room->partitions); k++ )
				{
					RoomInstanceMapSnapAction **actions = NULL;
					bool texture_override = (room->partitions[0]->partition_data ? room->partitions[0]->partition_data->texture_override : false);
					const char *texture_name = (room->partitions[0]->partition_data ? room->partitions[0]->partition_data->texture_name : NULL);
					sprintf(partition_name, "Mapsnap_%04d", partition_cnt);
					partition_cnt++;
					if(room->partitions[0]->partition_data && room->partitions[0]->partition_data->no_photo)
						continue;
					if(room->partitions[k]->partition_data)
						actions = room->partitions[k]->partition_data->actions;
					wl_state.gfx_add_map_photo_func(	partition_name, 
						&room->partitions[k]->mapSnapData,
						room->partitions[k]->bounds_min, 
						room->partitions[k]->bounds_mid, 
						room->partitions[k]->bounds_max, 
						actions,
						region_src,
						layer ? layerGetFilename(layer) : "(null)",
						room->def_name,
						texture_override,
						texture_name);

					// the above function outputs data into the RoomClientParsed structure, which will later be read into the run-time struct
				}
			}
		}
	}

	//Take images and save to tmp dir
	prev_disable_anim_flag = wl_state.debug.disable_world_animation;
	wl_state.debug.disable_world_animation = true;
	prev_disable_associated_regions_flag = wl_state.debug.disable_associated_regions;
	wl_state.debug.disable_associated_regions = is_space_map;
	prev_time_step = wl_state.timeStepScaleDebug;
	wl_state.timeStepScaleDebug = 0.0000001;
	if(wl_state.photo_iterations == 1)
	{
		while(wl_state.photo_iterations == 1)
		{
			if(!wl_state.gfx_take_map_photos_func(zmapGetFilename(zmap), &map_snap_output_list, true))
				had_errors = true;
		}
		if(!wl_state.gfx_take_map_photos_func(zmapGetFilename(zmap), &map_snap_output_list, false))
			had_errors = true;
	}
	else
	{
		int iterations = wl_state.photo_iterations;
		if(iterations == 0)
			iterations = 1;
		for( i=0; i < iterations; i++ )
		{
			if(!wl_state.gfx_take_map_photos_func(zmapGetFilename(zmap), &map_snap_output_list, i==(iterations-1)?false:true))
				had_errors = true;
		}
	}
	wl_state.debug.disable_world_animation = prev_disable_anim_flag;
	wl_state.debug.disable_associated_regions = prev_disable_associated_regions_flag;
	wl_state.timeStepScaleDebug = prev_time_step;

	for( i=eaSize(&map_snap_output_list)-1; i >= 0; i--)
	{
		char *output_name = eaPop(&map_snap_output_list);
		bflAddOutputFileEx(file_list, output_name, true);
		StructFreeString(output_name);
	}

	//Move images to bin files
	quick_sprintf(map_snap_hog_filename, map_snap_hog_filename_size, "%s/map_snap.hogg", base_dir);
	fileLocateWrite(map_snap_hog_filename, map_snap_hog_filename_full);
	map_snap_hog_file = hogFileReadEx(map_snap_hog_filename_full, &hog_created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE | HOG_NO_INTERNAL_TIMESTAMPS, 1024);
	sprintf(map_snap_backup_hog_filename, "%s/%s", fileTempDir(), map_snap_hog_filename);
	backup_exists = fileExists(map_snap_backup_hog_filename);
	if (backup_exists)
	{
		map_snap_backup_hog_file = hogFileReadEx(map_snap_backup_hog_filename, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE, 0);
		hogFileLock(map_snap_backup_hog_file);
	}

	hogFileLock(map_snap_hog_file);
	hogDeleteAllFiles(map_snap_hog_file);

	if( zmapInfoGetUsedInUGC( zmapGetInfo( zmap )) != ZMAP_UGC_UNUSED ) {
		quick_sprintf(map_snap_hog_mini_filename, map_snap_hog_mini_filename_size, "%s/map_snap_mini.hogg", base_dir);
		fileLocateWrite(map_snap_hog_mini_filename, map_snap_hog_mini_filename_full);
		map_snap_mini_hog_file = hogFileReadEx(map_snap_hog_mini_filename_full, &hog_created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE | HOG_NO_INTERNAL_TIMESTAMPS, 1024);

		hogFileLock(map_snap_mini_hog_file);
		hogDeleteAllFiles(map_snap_mini_hog_file);
	}

	for( i=0; i < eaSize(&streaming_info->regions_parsed); i++ )
	{
		WorldRegionClientParsed *region = (WorldRegionClientParsed *)streaming_info->regions_parsed[i];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region->common.region_name);
		if(region->conn_graph)
		{
			for( j=0; j < eaSize(&region->conn_graph->rooms); j++ )
			{
				RoomClientParsed *room = region->conn_graph->rooms[j];
				for( k=0; k < eaSize(&room->partitions); k++ )
				{
					RoomPartitionParsed *partition = room->partitions[k];
					for( l=0; l < eaSize(&partition->mapSnapData.image_name_list) ; l++ )
						worldMoveMapSnapImagesToHogg(map_snap_hog_file, partition->mapSnapData.image_name_list[l], backup_exists ? map_snap_backup_hog_filename : NULL, zmapGetFilename(zmap), false);
					if(partition->mapSnapData.overview_image_name)
					{
						worldMoveMapSnapImagesToHogg(map_snap_hog_file, partition->mapSnapData.overview_image_name, backup_exists ? map_snap_backup_hog_filename : NULL, zmapGetFilename(zmap), false);
					}

					if( zmapInfoGetUsedInUGC( zmapGetInfo( zmap )) != ZMAP_UGC_UNUSED ) {
						if( partition->mapSnapData.overview_image_name ) {
							worldMoveMapSnapImagesToHogg(map_snap_mini_hog_file, partition->mapSnapData.overview_image_name, backup_exists ? map_snap_backup_hog_filename : NULL, zmapGetFilename(zmap), true);
						} else if( eaSize( &partition->mapSnapData.image_name_list ) == 1 ) {
							worldMoveMapSnapImagesToHogg(map_snap_mini_hog_file, partition->mapSnapData.image_name_list[0], backup_exists ? map_snap_backup_hog_filename : NULL, zmapGetFilename(zmap), true);
						}
					}
				}
			}
		}
	}

	if (backup_exists)
	{
		hogFileUnlock(map_snap_backup_hog_file);
		hogFileDestroy(map_snap_backup_hog_file, true);
	}

	hogFileUnlock(map_snap_hog_file);
	hogFileDestroy(map_snap_hog_file, true);

	if( zmapInfoGetUsedInUGC( zmapGetInfo( zmap )) != ZMAP_UGC_UNUSED ) {
		hogFileUnlock(map_snap_mini_hog_file);
		hogFileDestroy(map_snap_mini_hog_file, true);
	}

	// Remove any slack space
	//
	// DON'T DO THIS IN UGC!  Here's why:
	//
	// If you are in fullscreen (not windowed-maximized), this
	// function can cause the fullscreen window to loose its
	// focus, leading to the GameClient minimizing itself.  This
	// is because it calls fileMove() internally.
	//
	// This function only reduces patch size, anyway, so I don't
	// think it'll help.
	if( !isProductionEditMode() ) {
		hogDefrag(map_snap_hog_filename_full, 1024, HogDefrag_Tight);
	}

	//Delete Temp Data
	{
		char dir[MAX_PATH], out_dir[MAX_PATH];

		sprintf(dir, "%s/bin/geobin/%s", fileTempDir(), zmapGetFilename(zmap));
		fileLocateWrite(dir, out_dir);
		if (dirExists(out_dir)) {
			rmdirtreeEx( out_dir, true );
		}
	}

	loadend_printf(" done.");
}

static void worldCellCreateClusters(ZoneMap *zmap, WorldRegionCommonParsed *region, WorldRegion *region_src, HogFile *region_hog_file,
			WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info,
			BinFileList *file_list, int *entry_histogram, IVec2 *entry_type_histogram, bool *clustersProcessed, S64 *cluster_elapsed_cycles)
{
	char baseZMapTempDir[MAX_PATH];
	S64	timeStart,timeEnd,timeRegionDelta;
	F32 timeRegionDeltaSeconds;

	log_printf(LOG_SIMPLYGON,"Begin Clustering Region %s for map %s\n",region->region_name?region->region_name:"Default", zmap->map_info.map_name);

	GET_CPU_TICKS_64(timeStart);

	worldCellClusterClear(region_src->root_world_cell);

	worldCellClusterGetTempDir(zmapGetFilename(zmap), SAFESTR(baseZMapTempDir));

	region_src->cluster_options->timeClusteringStart = _time32(NULL);

	worldCellClusteringOpenRegionClusterHog(region_src->cluster_options, region_src, baseZMapTempDir);

	region->hog_file = region_hog_file;
	queueCellClusters(streaming_info, streaming_pooled_info, file_list, entry_histogram, entry_type_histogram, 
		region, region_src, region->cells[0], region_src->root_world_cell, 
		worldRegionGetGraphicsData(region_src));
	region->hog_file = NULL;
	*clustersProcessed = true;

	worldCellClusteringCloseRegionClusterHog(region_src->cluster_options);

	if (!debug_preserve_simplygon_images && isDevelopmentMode()) {
		char clusterDirTemp[MAX_PATH];
		char clusterDirAbsolute[MAX_PATH];
		worldCellClusterGetTempDir(zmapGetFilename(region_src->zmap_parent),SAFESTR(clusterDirTemp));
		fileLocateWrite(clusterDirTemp,clusterDirAbsolute);
		rmdirtreeEx(clusterDirAbsolute,false);
	}

	region_src->cluster_options->timeClusteringComplete = _time32(NULL);

	GET_CPU_TICKS_64(timeEnd);

	timeRegionDelta = timeEnd - timeStart;
	timeRegionDeltaSeconds = timerSeconds64(timeRegionDelta);
	log_printf(LOG_SIMPLYGON,"Cluster Region %s Time: %f milliseconds\n",region->region_name?region->region_name:"Default",timeRegionDeltaSeconds);
	*cluster_elapsed_cycles += timeRegionDelta;
}

BinFileList *worldCellSaveBins(const char *base_dir, const char *output_filename, 
							   const char *pooled_output_filename, WorldStreamingPooledInfo *old_pooled_data, 
							   const char *header_filename, const char *bounds_filename)
{
	WorldStreamingInfo *streaming_info;
	WorldStreamingPooledInfo *streaming_pooled_info;
	BinFileList *file_list;
	ZoneMap *zmap = worldGetActiveMap();
	int *entry_histogram = NULL, *cell_histogram = NULL;
	IVec2 *entry_type_histogram = NULL;
	int total_entries = 0, total_cells = 0, total_size = 0;
	int model_draw_size = 0, material_draw_size = 0, subobject_size = 0, drawable_list_size = 0, costume_size = 0, instance_param_list_size = 0;
	int max_vis_level = 1;
	LightTraverseData light_data = {0};
	ZoneMapRegionBounds zonemap_region_bounds = {0};
	StashTable volume_entry_to_id;
	StashTable interactable_entry_to_id;
	U32 named_object_id = 1;
	int i, j, k, l;
	int shared_bounds_count = 0, unique_bounds_count = 0;
	U32 time_offset_seed = 19845728;
	char map_snap_hog_filename[MAX_PATH] = "";
	char map_snap_hog_mini_filename[MAX_PATH] = "";
	bool use_old_map_snap = true;
	bool doCluster = worldCellClusteringFeatureEnabled();
	bool lightProcessed = false;
	bool clustersProcessed = false;
	S64 cluster_elapsed_cycles = 0;
	F32 cluster_elapsed_seconds;
	ShaderGraphQuality actual_quality = wl_state.desired_quality;

	wlSetDesiredMaterialScore(SGRAPH_QUALITY_MAX_VALUE);

	assert(zmap);

	SET_FP_CONTROL_WORD_DEFAULT;

#if PLATFORM_CONSOLE
	if (gbDontDoAlerts)
		assertmsgf(0, "You must load the map on the PC client before running on a console.  Load error: %s", last_load_error);
	else
		Alertf("You must load the map on the PC client before running on a console.  Load error: %s", last_load_error);
	wlSetDesiredMaterialScore(actual_quality);
	return NULL;
#endif

	loadstart_printf("Creating world cell bins...");
	SendStringToCB(CBSTRING_COMMENT, "Creating world cell bins for %s", zmapInfoGetPublicName(zmapGetInfo(zmap)));
	triviaPrintf("ZoneMapState", "Binning");

	had_errors = false;

	streaming_info = StructAlloc(parse_WorldStreamingInfo);
	streaming_info->packed_data = calloc(sizeof(*streaming_info->packed_data), 1);
	PackedStructStreamInit(streaming_info->packed_data, STRUCT_PACK_BITPACK_LARGE_STRINGTABLE);

	streaming_pooled_info = StructAlloc(parse_WorldStreamingPooledInfo);
	streaming_pooled_info->packed_info_crc = ParseTableCRC(parse_WorldStreamingPackedInfo, NULL, 0);
	streaming_pooled_info->strings = StructAlloc(parse_WorldInfoStringTable);
	if (old_pooled_data && old_pooled_data->strings)
		importStringTable(streaming_pooled_info->strings, old_pooled_data->strings);
	streaming_pooled_info->packed_info = StructAlloc(parse_WorldStreamingPackedInfo);
	streaming_pooled_info->packed_data = calloc(sizeof(*streaming_pooled_info->packed_data), 1);
	PackedStructStreamInit(streaming_pooled_info->packed_data, STRUCT_PACK_BITPACK_LARGE_STRINGTABLE);

	file_list = StructAlloc(parse_BinFileList);

	ErrorfPushCallback(catchError, NULL);

	worldCellDepsReset();

	if (wlIsServer())
	{
		volume_entry_to_id = stashTableCreate(256, StashDefault, StashKeyTypeAddress, sizeof(void*));
		interactable_entry_to_id = stashTableCreate(256, StashDefault, StashKeyTypeAddress, sizeof(void*));
	}

	// Add zone map dependencies
	if (zmap->map_info.genesis_data && !isProductionEditMode())
	{
		if (zmap->map_info.from_ugc_file)
			bflAddSourceFile(file_list, zmap->map_info.from_ugc_file);
		else
			bflAddSourceFile(file_list, zmapGetFilename(zmap));
		if(zmap->map_info.genesis_data->genesis_exterior || zmap->map_info.genesis_data->genesis_exterior_nodes)
		{
			for ( i=0; i < eaSize(&zmap->map_info.secondary_maps); i++ )
			{
				ZoneMapInfo *secondary_zmap = worldGetZoneMapByPublicName(zmap->map_info.secondary_maps[i]->map_name);
				bflAddSourceFile(file_list, zmapInfoGetFilename(secondary_zmap));
			}
		}
	}

	for (i = 0; i<eaSize(&zmap->map_info.regions); i++)
	{
		if (zmap->map_info.regions[i]->sky_group)
		{
			for (j=0; j<eaSize(&zmap->map_info.regions[i]->sky_group->override_list); j++)
			{
				worldDepsReportSky(REF_STRING_FROM_HANDLE(zmap->map_info.regions[i]->sky_group->override_list[j]->sky));
			}
		}
	}

	// load layers, recalc bounds, build unpopulated cell tree
	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		ZoneMapLayer *layer = zmap->layers[i];
		char **dependent_files;
		const char *layer_filename;

		layerUnload(layer);
		layerLoadGroupSource(layer, zmap, NULL, false);

		layer_filename = layerGetFilename(layer);
		eaPush(&file_list->layer_names, StructAllocString(layer_filename));
		eaPush(&file_list->layer_region_names, layer->region_name);
		if(!layerIsGenesis(layer))
			bflAddSourceFile(file_list, layer->filename);
		if (!isProductionEditMode())
		{
			dependent_files = layerGetDependentFileNames(layer);
			for (j = 0; j < eaSize(&dependent_files); ++j)
				bflAddSourceFile(file_list, dependent_files[j]);
			eaDestroyEx(&dependent_files, NULL);
		}
	}

	ugcZoneMapLayerSaveUGCData(zmap);

	zmapRecalcBounds(zmap, true);
	worldBuildCellTree(zmap, false);

	loadstart_printf("Creating bin data...");

	worldDrawableListPoolReset(&zmap->world_cell_data.drawable_pool);

	//////////////////////////////////////////////////////////////////////////
	// Step 1: Setup region structures.
	for (i = 0; i < eaSize(&zmap->map_info.regions); ++i)
	{
		WorldRegion *region_src = zmap->map_info.regions[i];
		WorldRegionCommonParsed *region_dst;

		if (region_src->is_editor_region)
			continue;

		if (wlIsServer())
		{
			WorldRegionServerParsed *server_region_dst = StructAlloc(parse_WorldRegionServerParsed);
			region_dst = &server_region_dst->common;
		}
		else
		{
			WorldRegionClientParsed *client_region_dst = StructAlloc(parse_WorldRegionClientParsed);
			region_dst = &client_region_dst->common;
		}

		region_dst->region_name = StructAllocString(region_src->name);
		CopyStructs(&region_dst->cell_extents, &region_src->world_bounds.cell_extents, 1);
		copyVec3(region_src->world_bounds.world_min, region_dst->world_min);
		copyVec3(region_src->world_bounds.world_max, region_dst->world_max);
		region_dst->root_cell = createCell(region_dst, region_src->root_world_cell);
		if (region_src->root_world_cell)
			MAX1(max_vis_level, region_src->root_world_cell->vis_dist_level + 1);
		eaPush(&streaming_info->regions_parsed, region_dst);
	}

	entry_histogram = _alloca(max_vis_level * sizeof(int));
	memset(entry_histogram, 0, max_vis_level * sizeof(int));

	cell_histogram = _alloca(max_vis_level * sizeof(int));
	memset(cell_histogram, 0, max_vis_level * sizeof(int));

	entry_type_histogram = _alloca(WCENT_COUNT * sizeof(*entry_type_histogram));
	memset(entry_type_histogram, 0, WCENT_COUNT * sizeof(*entry_type_histogram));

	worldCellFXReset(zmap);
	worldAnimationEntryResetIDCounter(zmap);
	worldCellInteractionReset(zmap);
	worldDrawableListPoolReset(&zmap->world_cell_data.drawable_pool);
	worldCellEntryResetSharedBounds(zmap);

	//////////////////////////////////////////////////////////////////////////
	// 2. Load layers one by one and create parsable structs from the entries 
	//    they create, updating cell bounds in the process.  Also load all
	//    lights and occlusion volumes for calculating static lighting.

	loadstart_printf("Creating cell entries and calculating room hulls...");

	assert(zmap->world_cell_data.interaction_costume_idx == 0);

	if (doCluster)
	{
		loadstart_printf("Setting up clustering options...");

		worldCellClusteringSetupRegionClustering(zmap);

		loadend_printf(" done.");
	}

	for (j = 0; j < eaSize(&zmap->layers); ++j)
	{
		ZoneMapLayer *layer = zmap->layers[j];
		layerCreateCellEntries(layer);
	}

	if (doCluster)
	{
		loadstart_printf("Gathering world cell cluster lists...");

		worldCellClusteringFinishRegionClusterGather(zmap);

		loadend_printf(" done.");
	}

	// Create room connectivity graph since the static lighting needs to access it
	for (i = 0; i < eaSize(&streaming_info->regions_parsed); i++)
	{
		WorldRegionCommonParsed *region = streaming_info->regions_parsed[i];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region->region_name);

		if (region_src && region_src->room_conn_graph)
		{
			// Ensure room hulls are calculated
			roomConnGraphUpdate(region_src->room_conn_graph);
		}
	}

	loadend_printf(" done.");

	if (wl_state.update_light_func && wl_state.compute_static_lighting_func && wl_state.free_static_light_cache_func && wlIsClient())
	{
		loadstart_printf("Calculating static lighting...");

		worldGetWorldGraphicsData();
		// prime lighting state in graphics lib if we have client graphics
		if (wl_state.tick_sky_data_func)
			wl_state.tick_sky_data_func();

		// create all lights so we can calculate vertex lighting
		light_data.iPartitionIdx = PARTITION_CLIENT;
		for (j = 0; j < eaSize(&zmap->layers); ++j)
			layerGroupTreeTraverse(zmap->layers[j], createLightsAndVolumes, &light_data, false, true);

		for (j = 0; j < eaSize(&zmap->layers); ++j)
			applyStaticLighting(zmap->layers[j]->cell_entries, layerGetWorldRegion(zmap->layers[j]), file_list);

		if(wl_state.compute_terrain_lighting_func) {

			// Do terrain lighting binning stuff.
			loadstart_printf("Calculating terrain lighting...");

			// Check to see if this map is even set up to do terrain lighting on.
			if(zmapInfoGetTerrainStaticLighting(zmapGetInfo(zmap))) {
				int m;
				const char **terrainLightBinNames = NULL;
				wl_state.compute_terrain_lighting_func(zmap, &terrainLightBinNames, file_list);

				for(m = 0; m < eaSize(&terrainLightBinNames); m++) {
					binNotifyTouchedOutputFile(terrainLightBinNames[m]);
					bflUpdateOutputFile(terrainLightBinNames[m]);
					bflAddOutputFile(file_list, terrainLightBinNames[m]);
				}

				eaDestroy(&terrainLightBinNames);

				loadend_printf(" done.");
			} else {
				loadend_printf(" skipped.");
			}
		}

		for (j = 0; j < eaSize(&zmap->layers); ++j)
			freeStaticLightingData(zmap->layers[j]->cell_entries);

		if (!doCluster)
		{
			if (wl_state.remove_light_func)
				eaDestroyEx(&light_data.all_lights, wl_state.remove_light_func);
			else
				eaDestroy(&light_data.all_lights);
		}
		else
			lightProcessed = true;

		// Close world cell entries to release references to shared bounds
		for (j=0; j<eaSize(&light_data.all_volumes); j++)
		{
			WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(light_data.all_volumes[j]);
			if (volume_entry)
			{
				worldCellEntryFree(&volume_entry->base_entry);
				//removeSharedBoundsRef(volume_entry->base_entry.shared_bounds);
				//volume_entry->base_entry.shared_bounds = NULL;
			}
		}

		wl_state.HACK_disable_game_callbacks = true;
		eaDestroyEx(&light_data.all_volumes, wlVolumeFree);
		wl_state.HACK_disable_game_callbacks = false;

		loadend_printf(" done.");
	}

	if (wlIsServer() && !isProductionEditMode())
	{
		char tagGleanFilename[MAX_PATH];
		loadstart_printf("Generating intermediate tagged object file...");
		sprintf(tagGleanFilename, "%s/tagGlean.tagg", base_dir);
		if (wlGenerateAndSaveTaggedGroups(tagGleanFilename))
		{
			bflUpdateOutputFile(tagGleanFilename);
			bflAddOutputFile(file_list, tagGleanFilename);
		}
		loadend_printf(" done.");
	}


	// Create room connectivity graph
	for (i = 0; i < eaSize(&streaming_info->regions_parsed); i++)
	{
		WorldRegionCommonParsed *region = streaming_info->regions_parsed[i];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region->region_name);

		if (region_src)
		{
			if(region_src->room_conn_graph)
			{
				// Ensure room hulls are calculated
				roomConnGraphUpdate(region_src->room_conn_graph);
				if (wlIsServer())
				{
					WorldRegionServerParsed *server_region = (WorldRegionServerParsed *)region;
					server_region->conn_graph = roomConnGraphGetServerParsed(region_src->room_conn_graph, file_list->layer_names);
				}
				else
				{
					WorldRegionClientParsed *client_region = (WorldRegionClientParsed *)region;
					client_region->conn_graph = roomConnGraphGetClientParsed(region_src->room_conn_graph, file_list->layer_names);
				}
			}

			if(wlIsServer())
			{
				WorldRegionServerParsed *server_region = (WorldRegionServerParsed *)region;
				// Add civ gens!
				eaCopyStructs(&region_src->world_civilian_generators, &server_region->world_civilian_generators, parse_WorldCivilianGenerator);

				// And forbidden locs
				eaCopyStructs(&region_src->world_forbidden_positions, &server_region->world_forbidden_positions, parse_WorldForbiddenPosition);
			}
			else
			{
				WorldRegionClientParsed* client_region = (WorldRegionClientParsed *)region;

				eaCopyStructs(&region_src->world_path_nodes, &client_region->world_path_nodes, parse_WorldPathNode);
			}
		}
	}

	// Make map photos
	if (!wl_state.dont_take_photos && wl_state.gfx_add_map_photo_func && wl_state.gfx_take_map_photos_func && 
		wlIsClient() &&	(!isProductionMode() || isProductionEditMode()))
	{
		worldCellMakeMapPhotos(zmap, streaming_info, file_list, base_dir, map_snap_hog_filename, ARRAY_SIZE_CHECKED(map_snap_hog_filename), map_snap_hog_mini_filename, ARRAY_SIZE_CHECKED(map_snap_hog_mini_filename));
	}

	// Check for golden path nodes to remove
	if( wlIsClient() && resNamespaceIsUGC( zmapGetName( zmap ))) {
		WorldCollCollideResults wcResults = { 0 };

		loadstart_printf( "Looking for path nodes to remove..." );
		worldCreateAllCollScenes();

		// loop over all UGC generated breakable edges, do ray casts
		// between them and possibly break them.
		FOR_EACH_IN_EARRAY( streaming_info->regions_parsed, WorldRegionCommonParsed, region ) {
			WorldRegionClientParsed* clientRegion = (WorldRegionClientParsed*)region;
			FOR_EACH_IN_EARRAY( clientRegion->world_path_nodes, WorldPathNode, pathNode ) {
				FOR_EACH_IN_EARRAY( pathNode->properties.eaConnections, WorldPathEdge, pathEdge ) {
					if( pathEdge->bUGCGenerated ) {
						bool result1 = wcCapsuleCollideHR( worldGetActiveColl( PARTITION_CLIENT ), pathNode->position, pathEdge->v3Other, WC_FILTER_BIT_MOVEMENT, 0, 1.5, &wcResults );
						bool result2 = wcCapsuleCollideHR( worldGetActiveColl( PARTITION_CLIENT ), pathEdge->v3Other, pathNode->position, WC_FILTER_BIT_MOVEMENT, 0, 1.5, &wcResults );
						
						if( result1 || result2 ) {
							eaRemove( &pathNode->properties.eaConnections, FOR_EACH_IDX( pathNode->properties.eaConnections, pathEdge ));
							StructDestroySafe( parse_WorldPathEdge, &pathEdge );
						}
					}
				} FOR_EACH_END;
			} FOR_EACH_END;
		} FOR_EACH_END;

		// MJFTODO - It would be nice to remove node islands that are
		// no longer used.  That would require funelling down which
		// spawn point is the default spawn point.
		
		loadend_printf( "done." );
	}

	SendStringToCB(CBSTRING_COMMENT, "Processing world cell bins for %s", zmapInfoGetPublicName(zmapGetInfo(zmap)));

	for (i = 0; i < eaSize(&streaming_info->regions_parsed); ++i)
	{
		WorldRegionCommonParsed *region_dst = streaming_info->regions_parsed[i];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region_dst->region_name);

		if (wlIsClient())
		{
			// Undo welded bins that are not worth it
			if (!binDisableOptimizedBinWelding)
				optimizeBinWelding(region_src->root_world_cell);

			// Make welded bin drawables
			refreshAllBins(region_src->root_world_cell);
		}

		// Update room graph
		roomConnGraphUpdate(region_src->room_conn_graph);
	}

	worldCellCloseAll();

	getAllMaterialDraws(zmap, &streaming_pooled_info->material_draws); // increments ref count of each material if it is not already in the earray
	getAllModelDraws(zmap, &streaming_pooled_info->model_draws); // increments ref count of each model if it is not already in the earray
	getAllDrawableSubobjects(zmap, &streaming_pooled_info->subobjects); // increments ref count of each subobject if it is not already in the earray
	getAllDrawableLists(zmap, &streaming_pooled_info->drawable_lists); // increments ref count of each list if it is not already in the earray
	getAllSharedBounds(zmap, &streaming_pooled_info->packed_info->shared_bounds, old_pooled_data); // increments ref count of each bounds if it is not already in the earray
	getAllInstanceParamLists(zmap, &streaming_pooled_info->instance_param_lists, old_pooled_data);
	getAllInteractionCostumes(zmap, &streaming_pooled_info->interaction_costumes);

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->shared_bounds); ++i)
	{
		WorldCellEntrySharedBounds *shared_bounds = streaming_pooled_info->packed_info->shared_bounds[i];
		if (shared_bounds->ref_count > 2) // 1 for being in the earray
			shared_bounds_count++;
		else
			unique_bounds_count++;
	}

	for (i = 0; i < eaSize(&streaming_info->regions_parsed); ++i)
	{
		WorldRegionCommonParsed *region_dst = streaming_info->regions_parsed[i];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region_dst->region_name);
		getCellEntryMids(streaming_pooled_info, region_src->root_world_cell);
	}

	if (streaming_pooled_info->packed_info->shared_local_mids && old_pooled_data && old_pooled_data->packed_info)
	{
		streaming_pooled_info->packed_info->shared_local_mids = (WorldCellLocalMidParsed**)maintainOldIndices(streaming_pooled_info->packed_info->shared_local_mids, old_pooled_data->packed_info->shared_local_mids, 
																											  cmpLocalMid, distLocalMid, 20);
	}

	// make cell entry bins
	for (i = 0; i < eaSize(&streaming_info->regions_parsed); ++i)
	{
		WorldRegionCommonParsed *region_dst = streaming_info->regions_parsed[i];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region_dst->region_name);
		if (eaSize(&region_dst->cells))
			makeCellEntryBins(streaming_info, streaming_pooled_info, file_list, entry_histogram, entry_type_histogram, 
								region_dst, region_dst->cells[0], region_src->root_world_cell, 
								worldRegionGetGraphicsData(region_src), &time_offset_seed, 
								&named_object_id, volume_entry_to_id, interactable_entry_to_id);

		FOR_EACH_IN_EARRAY_FORWARDS(region_src->tag_locations, WorldTagLocation, tag_location)
		{
			eaPush(&region_dst->tag_locations, StructClone(parse_WorldTagLocation, tag_location));
		}
		FOR_EACH_END;
	}

	if (wlIsServer())
	{
		// Convert scope data that needs to be binned
		worldZoneMapScopePrepareForBins(zmap->zmap_scope, volume_entry_to_id, interactable_entry_to_id);
		streaming_info->zmap_scope_data_offset = 1 + StructPack(parse_WorldZoneMapScope, zmap->zmap_scope, streaming_info->packed_data);
	}

	for (j = 0; j < eaSize(&zmap->layers); ++j)
	{
		ZoneMapLayer *layer = zmap->layers[j];
		LayerBounds *bounds;

		bounds = StructAlloc(parse_LayerBounds);
		// This does a bunch of calculation.  We do the same calculation in updateRegionBounds in WorldGrid.c, even though it's probably redundant.
		layerGetBounds(layer, bounds->local_min, bounds->local_max);
		copyVec3(layer->bounds.visible_geo_min, bounds->visible_geo_min);
		copyVec3(layer->bounds.visible_geo_max, bounds->visible_geo_max);
		setVec3(bounds->local_min, quantBoundsMin(bounds->local_min[0]), quantBoundsMin(bounds->local_min[1]), quantBoundsMin(bounds->local_min[2]));
		setVec3(bounds->local_max, quantBoundsMax(bounds->local_max[0]), quantBoundsMax(bounds->local_max[1]), quantBoundsMax(bounds->local_max[2]));
		eaPush(&streaming_info->layer_bounds, bounds);
		eaPush(&streaming_info->layer_names, StructAllocString(layerGetFilename(layer)));

		layerUnload(layer);
	}

#if PRINT_BOUNDS
	printf("\n");
	for (j = 0; j < eaSize(&zmap->layers); ++j)
	{
		ZoneMapLayer *layer = zmap->layers[j];
		printf(" %s : (%g, %g, %g) - (%g, %g, %g)\n", layer->filename, expandVec3(streaming_info->layer_bounds[j]->local_min), expandVec3(streaming_info->layer_bounds[j]->local_max));
	}
#endif
	
	//////////////////////////////////////////////////////////////////////////
	// 3. Convert MaterialDraw, ModelDraw, WorldDrawableSubobject, WorldDrawableList, 
	//    RoomConnGraph structs, and Encounter data to parsable form
	for (j = 0; j < eaSize(&streaming_pooled_info->material_draws); ++j)
	{
		MaterialDraw *draw_src = streaming_pooled_info->material_draws[j];
		MaterialDrawParsed *draw_dst = StructAlloc(parse_MaterialDrawParsed);

		draw_dst->material_name_idx = getMaterialIdx(streaming_pooled_info, file_list, draw_src->material);
		draw_dst->fallback_idx = draw_src->material->override_fallback_index;
		for (i = 0; i < draw_src->const_count; ++i)
			eafPush4(&draw_dst->constants, draw_src->constants[i]);
		for (i = 0; i < draw_src->tex_count; ++i)
			eaiPush(&draw_dst->texture_idxs, getTextureIdx(streaming_pooled_info, draw_src->textures[i]));
		for (i = 0; i < draw_src->const_mapping_count; ++i)
			eaPush(&draw_dst->constant_mappings, StructCloneVoid(parse_MaterialConstantMappingFake, &draw_src->constant_mappings[i]));
		draw_dst->has_swaps = draw_src->has_swaps;
		draw_dst->is_occluder = draw_src->is_occluder;

		material_draw_size += sizeof(*draw_dst);
		material_draw_size += eafSize(&draw_dst->constants) * sizeof(F32);
		material_draw_size += eaiSize(&draw_dst->texture_idxs) * sizeof(int);
		material_draw_size += eaSize(&draw_dst->constant_mappings) * sizeof(MaterialConstantMapping);

		i = eaPush(&streaming_pooled_info->packed_info->material_draws_parsed, draw_dst);
		assert(i == j);
	}

	for (j = 0; j < eaSize(&streaming_pooled_info->model_draws); ++j)
	{
		ModelDraw *draw_src = streaming_pooled_info->model_draws[j];
		ModelDrawParsed *draw_dst = StructAlloc(parse_ModelDrawParsed);

		model_draw_size += sizeof(*draw_dst);

		draw_dst->model_idx = getModelIdx(streaming_pooled_info, file_list, draw_src->model);
		draw_dst->lod_idx = draw_src->lod_idx;

		i = eaPush(&streaming_pooled_info->packed_info->model_draws_parsed, draw_dst);
		assert(i == j);
	}

	for (j = 0; j < eaSize(&streaming_pooled_info->subobjects); ++j)
	{
		WorldDrawableSubobject *subobject_src = streaming_pooled_info->subobjects[j];
		WorldDrawableSubobjectParsed *subobject_dst = StructAlloc(parse_WorldDrawableSubobjectParsed);

		subobject_size += sizeof(*subobject_dst);

		for (i = 0; i < eaSize(&subobject_src->material_draws); ++i)
		{
			int idx = getMaterialDrawIdx(streaming_pooled_info, subobject_src->material_draws[i]);
			eaiPush(&subobject_dst->materialdraw_idxs, idx);
		}
		subobject_size += eaiSize(&subobject_dst->materialdraw_idxs) * sizeof(int);

		subobject_dst->modeldraw_idx = getModelDrawIdx(streaming_pooled_info, subobject_src->model);
		subobject_dst->subobject_idx = subobject_src->subobject_idx;

		i = eaPush(&streaming_pooled_info->packed_info->subobjects_parsed, subobject_dst);
		assert(i == j);
	}

	for (j = 0; j < eaSize(&streaming_pooled_info->drawable_lists); ++j)
	{
		WorldDrawableList *list_src = streaming_pooled_info->drawable_lists[j];
		WorldDrawableListParsed *list_dst = StructAlloc(parse_WorldDrawableListParsed);

		drawable_list_size += sizeof(*list_dst);

		list_dst->no_fog = list_src->no_fog;
		list_dst->high_detail_high_lod = list_src->high_detail_high_lod;

		for (i = 0; i < list_src->lod_count; ++i)
		{
			WorldDrawableLod *lod_src = &list_src->drawable_lods[i];
			WorldDrawableLodParsed *lod_dst = StructAlloc(parse_WorldDrawableLodParsed);

			lod_dst->near_dist = lod_src->near_dist;
			lod_dst->far_dist = lod_src->far_dist;
			lod_dst->far_morph = lod_src->far_morph;
			lod_dst->occlusion_materials = lod_src->occlusion_materials;
			lod_dst->no_fade = lod_src->no_fade;
			lod_dst->subobject_count = lod_src->subobject_count;

			if (lod_src->subobjects)
			{
				int s;
				for (s = 0; s < lod_src->subobject_count; ++s)
					eaiPush(&lod_dst->subobject_idxs, getDrawableSubobjectIdx(streaming_pooled_info, lod_src->subobjects[s]));
			}

			drawable_list_size += sizeof(*lod_dst) + eaiSize(&lod_dst->subobject_idxs) * sizeof(int);

			eaPush(&list_dst->drawable_lods, lod_dst);
		}

		i = eaPush(&streaming_pooled_info->packed_info->drawable_lists_parsed, list_dst);
		assert(i == j);
	}

	for (j = 0; j < eaSize(&streaming_pooled_info->instance_param_lists); ++j)
	{
		WorldInstanceParamList *list_src = streaming_pooled_info->instance_param_lists[j];
		WorldInstanceParamListParsed *list_dst = StructAlloc(parse_WorldInstanceParamListParsed);

		instance_param_list_size += sizeof(*list_dst);
		
		list_dst->lod_count = list_src->lod_count;

		for (i = 0; i < list_src->lod_count; ++i)
		{
			WorldInstanceParamPerLod *lod_param = &list_src->lod_params[i];
			eaiPush(&list_dst->instance_params, lod_param->subobject_count);
			for (k = 0; k < lod_param->subobject_count; ++k)
			{
				WorldInstanceParamPerSubObj *subobj_param = &lod_param->subobject_params[k];
				eaiPush(&list_dst->instance_params, subobj_param->fallback_count);
				for (l = 0; l < subobj_param->fallback_count; ++l)
					eafPush4(&list_dst->instance_params, subobj_param->fallback_params[l].instance_param);
			}
		}

		instance_param_list_size += eafSize(&list_dst->instance_params) * sizeof(F32);

		i = eaPush(&streaming_pooled_info->packed_info->instance_param_lists_parsed, list_dst);
		assert(i == j);
	}

	for (j = 0; j < eaSize(&streaming_pooled_info->interaction_costumes); ++j)
	{
		WorldInteractionCostume *costume_src = streaming_pooled_info->interaction_costumes[j];
		WorldInteractionCostumeParsed *costume_dst = StructAlloc(parse_WorldInteractionCostumeParsed);

		costume_size += sizeof(*costume_dst);

		costume_dst->hand_pivot_idx = getMatrixIdx(streaming_pooled_info, costume_src->hand_pivot);
		costume_dst->mass_pivot_idx = getMatrixIdx(streaming_pooled_info, costume_src->mass_pivot);
		costume_dst->carry_anim_bit_string_idx = getStringIdx(streaming_pooled_info, costume_src->carry_anim_bit);

		eaiCopy(&costume_dst->interaction_uids, &costume_src->interaction_uids);
		costume_size += eaiSize(&costume_dst->interaction_uids) * sizeof(int);

		for (i = 0; i < eaSize(&costume_src->costume_parts); ++i)
		{
			WorldInteractionCostumePart *part_src = costume_src->costume_parts[i];
			WorldInteractionCostumePartParsed *part_dst = StructAlloc(parse_WorldInteractionCostumePartParsed);
			Mat4 matrix;

			copyMat4(part_src->matrix, matrix);
			extractScale(matrix, part_dst->scale);
			part_dst->matrix_idx = getMatrixIdx(streaming_pooled_info, matrix);
			part_dst->model_idx = getModelIdx(streaming_pooled_info, file_list, part_src->model);
			part_dst->draw_list_idx = getDrawableListIdx(streaming_pooled_info, part_src->draw_list);
			part_dst->instance_param_list_idx = getInstanceParamListIdx(streaming_pooled_info, part_src->instance_param_list);
			copyVec4(part_src->tint_color, part_dst->tint_color);
			part_dst->collision = part_src->collision;

			costume_size += sizeof(*part_dst);

			eaPush(&costume_dst->costume_parts, part_dst);
		}

		i = eaPush(&streaming_pooled_info->packed_info->interaction_costumes_parsed, costume_dst);
		assert(i == j);
	}

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->shared_bounds); ++i)
	{
		WorldCellEntrySharedBounds *shared_bounds = streaming_pooled_info->packed_info->shared_bounds[i];
		if (shared_bounds->use_model_bounds && !shared_bounds->model)
			shared_bounds->use_model_bounds = 0;
		if (shared_bounds->use_model_bounds)
		{
			zeroVec3(shared_bounds->local_min);
			zeroVec3(shared_bounds->local_max);
			shared_bounds->radius = 0;
		}
		shared_bounds->model_idx = getModelIdx(streaming_pooled_info, file_list, shared_bounds->model);
	}

	loadend_printf(" done.");

	loadstart_printf("Writing bins to disk...");
	SendStringToCB(CBSTRING_COMMENT, "Writing world cell bins for %s", zmapInfoGetPublicName(zmapGetInfo(zmap)));

	//////////////////////////////////////////////////////////////////////////
	// 4. Fixup the cell structures with bounds data and write all bins to disk.

	for (j = 0; j < eaSize(&streaming_info->regions_parsed); ++j)
	{
		WorldRegionCommonParsed *region = streaming_info->regions_parsed[j];
		WorldRegion *region_src = zmapGetWorldRegionByName(zmap, region->region_name);
		WorldRegionBounds *region_bounds = StructAlloc(parse_WorldRegionBounds);
		char region_hog_filename[MAX_PATH], region_hog_filename_full[MAX_PATH];
		HogFile *region_hog_file;
		bool hog_created;
		int err_return;

		if (region->world_max[0] < region->world_min[0])
		{
			setVec3same(region->world_min, 0);
			setVec3same(region->world_max, 0);
		}

		region_bounds->region_name = region->region_name;
		copyVec3(region->world_min, region_bounds->world_min);
		copyVec3(region->world_max, region_bounds->world_max);
		eaPush(&zonemap_region_bounds.regions, region_bounds);


		setVec3(region->world_min, quantBoundsMin(region->world_min[0]), quantBoundsMin(region->world_min[1]), quantBoundsMin(region->world_min[2]));
		setVec3(region->world_max, quantBoundsMax(region->world_max[0]), quantBoundsMax(region->world_max[1]), quantBoundsMax(region->world_max[2]));

		if (eaSize(&region->cells))
			updateCellBounds(region->cells[0]);

		// do not use the cell children pointers after this point, they are not guaranteed to be valid

		sprintf(region_hog_filename, "%s/world_%s_entries.hogg", base_dir, region->region_name ? region->region_name : "Default");
		fileLocateWrite(region_hog_filename, region_hog_filename_full);
		region_hog_file = hogFileReadEx(region_hog_filename_full, &hog_created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE | HOG_NO_INTERNAL_TIMESTAMPS, 1024);
		hogFileLock(region_hog_file);
		hogDeleteAllFiles(region_hog_file);


		// make cell clusters
		if (doCluster && region_src->cluster_options && 
			worldCellClusterIsRemeshEnabled(&region_src->cluster_options->debug))
		{
			if (eaSize(&region->cells))
			{
				worldCellCreateClusters(zmap, region, region_src, region_hog_file, streaming_info, streaming_pooled_info,
					file_list, entry_histogram, entry_type_histogram, &clustersProcessed, &cluster_elapsed_cycles);
			}
		}

		for (i = 0; i < eaSize(&region->cells); ++i)
		{
			WorldCellParsed *cell = region->cells[i];
			IVec3 size;
			U8 vis_dist_level;

			if (doCluster && cell->contain_cluster)
			{
				// if running a prod build, keep the cluster bin alive by marking the 
				// cluster bin hogg file as "touched"
				char clusterBinHoggName[MAX_PATH];
				const char *clusterBinNameStringCached = NULL;

				// for world cell bin dependency checking, list the client cluster hogg bins as outputs
				worldGetClusterBinHoggPath(SAFESTR(clusterBinHoggName), region_src, &cell->block_range, getVistDistLevelFromCellID(cell->cell_id));
				clusterBinNameStringCached = allocAddString(clusterBinHoggName);
				binNotifyTouchedOutputFile(clusterBinNameStringCached);
			}

			if (cell->is_empty)
			{
				assert(!cell->server_data);
				assert(!cell->client_data);
				assert(!cell->welded_data);

				if (!cell->contain_cluster)
				{
					StructDestroy(parse_WorldCellParsed, cell);
					eaRemove(&region->cells, i);
					--i;
				}
				continue;
			}

			rangeSize(&cell->block_range, size);
			assert(size[0] == size[1]);
			assert(size[0] == size[2]);
			vis_dist_level = log2(size[0]);

			cell_histogram[vis_dist_level]++;

			if (cell->server_data)
			{
				char filename[MAX_PATH];
				sprintf(filename, "d%d", cell->cell_id);
				ParserWriteBinaryFile(filename, NULL, parse_WorldCellServerDataParsed, cell->server_data, NULL, NULL, NULL, NULL, 0, 0, region_hog_file, PARSERWRITE_IGNORECRC | PARSERWRITE_ZEROTIMESTAMP, 0);
				assert(!cell->client_data);
			}
			else if (cell->client_data)
			{
				char filename[MAX_PATH];
				sprintf(filename, "d%d", cell->cell_id);
				ParserWriteBinaryFile(filename, NULL, parse_WorldCellClientDataParsed, cell->client_data, NULL, NULL, NULL, NULL, 0, 0, region_hog_file, PARSERWRITE_IGNORECRC | PARSERWRITE_ZEROTIMESTAMP, 0);
			}

			if (cell->welded_data)
			{
				char filename[MAX_PATH];
				sprintf(filename, "w%d", cell->cell_id);
				ParserWriteBinaryFile(filename, NULL, parse_WorldCellWeldedDataParsed, cell->welded_data, NULL, NULL, NULL, NULL, 0, 0, region_hog_file, PARSERWRITE_IGNORECRC | PARSERWRITE_ZEROTIMESTAMP, 0);
			}
		}

		hogFileUnlock(region_hog_file);
		hogFileDestroy(region_hog_file, true);
		if(bflUpdateOutputFile(region_hog_filename))
			use_old_map_snap = false;
		bflAddOutputFile(file_list, region_hog_filename);
	}

	if (clustersProcessed) {
		ClusterDependency cluster_dependencies;
		char clusterName[MAX_PATH];

		zoneMapClusterDepFileName(zmap,SAFESTR(clusterName));

		cluster_dependencies.cluster_version = CLUSTER_TOOL_VERSION;
		ParserWriteTextFile(clusterName, parse_ClusterDependency, &cluster_dependencies, 0, 0);
		filelog_printf("world_binning.log", "Map \"%s\" created a new Cluster.Dep file.", zmapGetFilename(zmap));

		bflUpdateOutputFile(clusterName);
		bflAddOutputFileEx(file_list,clusterName,true);
	}

	if (doCluster)
	{
		loadstart_printf("Cleanup world cell clustering...");

		worldCellClusteringCleanupRegionClustering(zmap, file_list);

		loadend_printf(" done.");

		cluster_elapsed_seconds = timerSeconds64(cluster_elapsed_cycles);
		log_printf(LOG_SIMPLYGON,"Cluster Total Time: %f milliseconds\n", cluster_elapsed_seconds);

		// wipe lights now that calculations have completed
		if (lightProcessed)
			if (wl_state.remove_light_func)
				eaDestroyEx(&light_data.all_lights, wl_state.remove_light_func);
			else
				eaDestroy(&light_data.all_lights);
	}

	streaming_info->parse_table_crcs = getWorldCellParseTableCRC(false);
	streaming_info->cell_size = CELL_BLOCK_SIZE;
	streaming_info->bin_version_number = WORLD_STREAMING_BIN_VERSION + genesisGetBinVersion(zmap);

	for (i = 0; i < eaSize(&streaming_info->regions_parsed); i++)
	{
		if (wlIsServer())
		{
			WorldRegionServerParsed *region = (WorldRegionServerParsed *)streaming_info->regions_parsed[i];
			U32 data_offset = StructPack(parse_WorldRegionServerParsed, region, streaming_info->packed_data);
			eaiPush(&streaming_info->region_data_offsets, data_offset);
		}
		else
		{
			WorldRegionClientParsed *region = (WorldRegionClientParsed *)streaming_info->regions_parsed[i];
			U32 data_offset = StructPack(parse_WorldRegionClientParsed, region, streaming_info->packed_data);
			eaiPush(&streaming_info->region_data_offsets, data_offset);
		}
	}

	streaming_pooled_info->packed_info_offset = StructPack(parse_WorldStreamingPackedInfo, streaming_pooled_info->packed_info, streaming_pooled_info->packed_data) + 1;

	// output
	streaming_info->has_errors = had_errors;
	PackedStructStreamFinalize(streaming_info->packed_data);
	streaming_info->packed_data_serialize = PackedStructStreamSerialize(streaming_info->packed_data);
	ParserWriteBinaryFile(output_filename, NULL, parse_WorldStreamingInfo, streaming_info, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);
	if(bflUpdateOutputFile(output_filename))
		use_old_map_snap = false;
	bflAddOutputFile(file_list, output_filename);

	PackedStructStreamFinalize(streaming_pooled_info->packed_data);
	streaming_pooled_info->packed_data_serialize = PackedStructStreamSerialize(streaming_pooled_info->packed_data);
	ParserWriteBinaryFile(pooled_output_filename, NULL, parse_WorldStreamingPooledInfo, streaming_pooled_info, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);
	bflUpdateOutputFile(pooled_output_filename);
	bflAddOutputFile(file_list, pooled_output_filename);

	//We will only use the new map snap if something changed.
	//This does not detect changes in terrain, but it is non-trivial to detect that the terrain updated here
	if(map_snap_hog_filename[0])
	{
		bflUpdateOutputFileEx(map_snap_hog_filename, use_old_map_snap);
		bflAddOutputFile(file_list, map_snap_hog_filename);
	}
	if(map_snap_hog_mini_filename[0])
	{
		bflUpdateOutputFileEx(map_snap_hog_mini_filename, use_old_map_snap);
		bflAddOutputFile(file_list, map_snap_hog_mini_filename);
	}

	if (bounds_filename && wlIsServer())
	{
		ParserWriteTextFile(bounds_filename, parse_ZoneMapRegionBounds, &zonemap_region_bounds, 0, 0);
		bflUpdateOutputFile(bounds_filename);
		bflAddOutputFile(file_list, bounds_filename);
	}
	StructDeInit(parse_ZoneMapRegionBounds, &zonemap_region_bounds);

	worldCellDepsFinalize(file_list, output_filename);

	bflFixupAndWrite(file_list, header_filename, BFLT_WORLD_CELL);

	loadend_printf(" done.");

	ErrorfPopCallback();


	if (show_binning_stats)
	{
		// print stats
		printf("\n");
		for (i = 0; i < max_vis_level; ++i)
		{
			printf(" Level %d: %d cells, %d entries\n", i, cell_histogram[i], entry_histogram[i]);
			total_cells += cell_histogram[i];
			total_entries += entry_histogram[i];
		}
		printf(" Total cells: %d\n\n", total_cells);

		total_size = 0;
		for (i = 0; i < WCENT_COUNT; ++i)
			total_size += entry_type_histogram[i][1];
		for (i = 0; i < WCENT_COUNT; ++i)
		{
			if (!entry_type_histogram[i][0])
				continue;
			printf(" Type % -14s: % 7d entries, % -10s (% 2.2f%%)\n", StaticDefineIntRevLookup(WorldCellEntryTypeEnum, i), entry_type_histogram[i][0], friendlyBytes(entry_type_histogram[i][1]), 100.f * entry_type_histogram[i][1] / total_size);
		}
		printf(" Total entries:            % 7d, % -10s\n", total_entries, friendlyBytes(total_size));

		total_size = sizeof(int) + eaSize(&streaming_pooled_info->strings->strings); // count + null terminators
		for (i = 0; i < eaSize(&streaming_pooled_info->strings->strings); ++i)
		{
			if (streaming_pooled_info->strings->strings[i])
				total_size += (int)strlen(streaming_pooled_info->strings->strings[i]);
		}

		printf(" Pooled strings:           % 7d, % -10s\n", eaSize(&streaming_pooled_info->strings->strings), friendlyBytes(total_size));
		printf(" Pooled matrices:          % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->pooled_matrices), friendlyBytes(eaSize(&streaming_pooled_info->packed_info->pooled_matrices) * 2 * sizeof(Vec3))); // parsed size of a Mat4 is two Vec3s
		printf(" Pre-swapped materials:    % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->material_draws_parsed), friendlyBytes(material_draw_size));
		printf(" Pre-swapped models:       % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->model_draws_parsed), friendlyBytes(model_draw_size));
		printf(" Pre-swapped subobjects:   % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->subobjects_parsed), friendlyBytes(subobject_size));
		printf(" Pre-swapped draw lists:   % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->drawable_lists_parsed), friendlyBytes(drawable_list_size));
		printf(" Pooled instance params:   % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->instance_param_lists_parsed), friendlyBytes(instance_param_list_size));
		printf(" Interaction costumes:     % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->interaction_costumes_parsed), friendlyBytes(costume_size));
		printf(" Pooled local mids:        % 7d, % -10s\n", eaSize(&streaming_pooled_info->packed_info->shared_local_mids), friendlyBytes(eaSize(&streaming_pooled_info->packed_info->shared_local_mids) * sizeof(WorldCellLocalMidParsed)));
		printf(" Pooled bounds:            % 7d, % -10s (%d shared + %d unique)\n", eaSize(&streaming_pooled_info->packed_info->shared_bounds), friendlyBytes(eaSize(&streaming_pooled_info->packed_info->shared_bounds) * sizeof(WorldCellEntrySharedBounds)), shared_bounds_count, unique_bounds_count);
		printf("\n");
	}


	// free data
	for (i = 0; i < eaSize(&streaming_info->regions_parsed); ++i)
	{
		WorldRegionCommonParsed *region = streaming_info->regions_parsed[i];
		WorldRegion *world_region = zmapGetWorldRegionByName(zmap, region->region_name);

		for (j = 0; j < eaSize(&region->cells); ++j)
		{
			WorldCellParsed *cell = region->cells[j];
			StructDestroySafe(parse_WorldCellServerDataParsed, &cell->server_data);
			StructDestroySafe(parse_WorldCellClientDataParsed, &cell->client_data);
			StructDestroySafe(parse_WorldCellWeldedDataParsed, &cell->welded_data);
		}

		if (world_region)
		{
			worldCellFree(world_region->root_world_cell);
			worldCellFree(world_region->temp_world_cell);
			world_region->root_world_cell = NULL;
			world_region->temp_world_cell = NULL;
			if (wl_state.free_region_graphics_data)
				wl_state.free_region_graphics_data(world_region, false);
			world_region->graphics_data = NULL;
			world_region->world_bounds.needs_update = true;
			world_region->preloaded_cell_data = false;
		}
	}

	if (wlIsServer())
		eaDestroyStructVoid(&streaming_info->regions_parsed, parse_WorldRegionServerParsed);
	else
		eaDestroyStructVoid(&streaming_info->regions_parsed, parse_WorldRegionClientParsed);

	PackedStructStreamDeinit(streaming_info->packed_data);
	SAFE_FREE(streaming_info->packed_data);
	StructDestroySafe(parse_WorldStreamingInfo, &streaming_info);

	freeStreamingPooledInfoSafe(zmap, &streaming_pooled_info);

	stashTableDestroy(file_list->source_file_hash);
	stashTableDestroy(file_list->output_file_hash);
	file_list->source_file_hash = NULL;
	file_list->source_file_hash = NULL;

	if (wlIsServer())
	{
		stashTableDestroy(volume_entry_to_id);
		stashTableDestroy(interactable_entry_to_id);
	}

	worldResetDefPools(false); // CD: breaks multi-maps

	// free zone map scope
	worldZoneMapScopeDestroy(zmap->zmap_scope);
	zmap->zmap_scope = NULL;

	worldCellFXReset(zmap);
	worldAnimationEntryResetIDCounter(zmap);
	worldCellInteractionReset(zmap);
	worldDrawableListPoolReset(&zmap->world_cell_data.drawable_pool);
	worldCellEntryResetSharedBounds(zmap);

	wlSetDesiredMaterialScore(actual_quality);

	loadend_printf(" done.");

	return file_list;
}
