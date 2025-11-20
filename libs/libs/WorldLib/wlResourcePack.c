
#include "strings_opt.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "timing.h"
#include "StringCache.h"

#include "WorldLib.h"
#include "WorldGrid.h"
#include "wlResourcePack.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define GEO_EXT ".geo2"
#define TEX_EXTS ".wtex .htex .TexWord"

AUTO_STRUCT AST_STARTTOK("Pack") AST_ENDTOK("End") AST_STRIP_UNDERSCORES WIKI("Resource Pack File Doc");
typedef struct ResourcePackParsed
{
	const char *filename;		AST( FILENAME POOL_STRING )
	char *pack_name;			AST( STRUCTPARAM WIKI("A unique name for the pack that will be referenced by the map files.") )
	char **packed_dirs;			AST( NAME(Folder) WIKI("Folders to include in the pack (recursive).") )
} ResourcePackParsed;

#include "wlResourcePack_h_ast.c"
#include "wlResourcePack_c_ast.c"

static ParseTable parse_packs[] = {
	{ "Pack",			TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	0,	sizeof(ResourcePackParsed),		parse_ResourcePackParsed},
	{ "", 0, 0 }
};

AUTO_RUN;
void initParsePacks(void)
{
	ParserSetTableInfo(parse_packs, sizeof(ResourcePackParsed*), "ParsedPacks", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}


typedef struct ResourcePackContainer
{
	ResourcePackParsed **parsed_packs;
	StashTable parsed_pack_hash;
	StashTable resource_pack_hash;
	char **pack_names;
} ResourcePackContainer;

typedef struct ResourceTypeData
{
	char *pack_dir;
	char *pack_bin;
	char *pack_exts;
} ResourceTypeData;

static ResourceTypeData pack_type_data[RPACK_TYPE_COUNT] = 
{
	{ "Packs/WorldObjects", "WorldObjectPacks.bin", GEO_EXT },
	{ "Packs/WorldTextures", "WorldTexturePacks.bin", TEX_EXTS },
	{ "Packs/DynObjects", "DynObjectPacks.bin", GEO_EXT },
	{ "Packs/DynTextures", "DynTexturePacks.bin", TEX_EXTS },
};

static ResourcePackContainer pack_containers[RPACK_TYPE_COUNT];

static ResourcePackFileNode *addPackEntry(ResourcePackFolderNode *folder, char *name, char *filename)
{
	int i;
	char *slash;

	if (slash = strchr(name, '/'))
	{
		int name_len = (int)(slash - name);
		ResourcePackFolderNode *child_folder = NULL;
		for (i = 0; i < eaSize(&folder->folders); ++i)
		{
			if (strnicmp(name, folder->folders[i]->name, name_len)==0)
			{
				child_folder = folder->folders[i];
				break;
			}
		}

		if (!child_folder)
		{
			child_folder = StructCreate(parse_ResourcePackFolderNode);
			child_folder->name = StructAllocStringLen(name, name_len);
			eaPush(&folder->folders, child_folder);
		}

		return addPackEntry(child_folder, slash+1, filename);
	}
	else
	{
		ResourcePackFileNode *file = NULL;

		for (i = 0; i < eaSize(&folder->files); ++i)
		{
			if (stricmp(name, folder->files[i]->name)==0)
			{
				file = folder->files[i];
				break;
			}
		}

		if (file)
		{
			StructFreeString(file->name);
		}
		else
		{
			file = StructCreate(parse_ResourcePackFileNode);
			eaPush(&folder->files, file);
		}

		file->name = StructAllocString(name);
		file->filename = allocAddFilename(filename);

		return file;
	}

	return NULL;
}

static ResourcePack *cur_pack;
static char *cur_exts;

static FileScanAction fillPackCallback(char *dir, struct _finddata32_t *data, void *pUserData)
{
	char filename[MAX_PATH];
	char *slash;
	char *ext;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;
	ext = strrchr(data->name, '.');
	if (!ext || !strstri(cur_exts, ext))
		return FSA_EXPLORE_DIRECTORY;

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// Do actual processing
	if (slash = strchr(filename, '/'))
	{
		int name_len = (int)(slash - filename);
		ResourcePackFolderNode *child_folder = NULL;
		ResourcePackFileNode *file;
		int i;

		for (i = 0; i < eaSize(&cur_pack->folders); ++i)
		{
			if (strnicmp(filename, cur_pack->folders[i]->name, name_len)==0)
			{
				child_folder = cur_pack->folders[i];
				break;
			}
		}

		if (!child_folder)
		{
			child_folder = StructCreate(parse_ResourcePackFolderNode);
			child_folder->name = StructAllocStringLen(filename, name_len);
			eaPush(&cur_pack->folders, child_folder);
		}

		file = addPackEntry(child_folder, slash+1, filename);

		if (file)
		{
			char *s = strrchr(filename, '.');
			if (s)
				*s = 0;
			stashAddPointer(cur_pack->files, filename, file, true);

			s = strrchr(filename, '/');
			if (!(s++))
				s = filename;
			stashAddPointer(cur_pack->files_nopath, s, file, true);
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}

static void fillPackFromParsed(ResourcePack *pack, ResourcePackParsed *parsed, ResourcePackType type)
{
	int i;

	if (pack->files)
		stashTableDestroy(pack->files);
	if (pack->files_nopath)
		stashTableDestroy(pack->files_nopath);
	StructDeInit(parse_ResourcePack, pack);

	ZeroStruct(pack);

	pack->name = StructAllocString(parsed->pack_name);
	pack->files = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);
	pack->files_nopath = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);

	cur_pack = pack;
	cur_exts = pack_type_data[type].pack_exts;

	for (i = 0; i < eaSize(&parsed->packed_dirs); ++i)
		fileScanAllDataDirs(parsed->packed_dirs[i], fillPackCallback, NULL);
}

static ResourcePackType cur_type;
static int reloadPackSubStructCallback(ResourcePackParsed *newpack, ResourcePackParsed *oldpack, ParseTable *tpi, eParseReloadCallbackType callback_type)
{
	int i;

	if (tpi != parse_ResourcePackParsed)
		return 1;

	if (oldpack)
	{
		ResourcePack *pack = NULL;

		for (i = 0; i < eaSize(&pack_containers[cur_type].pack_names); ++i)
		{
			if (stricmp(pack_containers[cur_type].pack_names[i], oldpack->pack_name)==0)
				eaRemove(&pack_containers[cur_type].pack_names, i--);
		}

		stashRemovePointer(pack_containers[cur_type].parsed_pack_hash, oldpack->pack_name, NULL);
		stashRemovePointer(pack_containers[cur_type].resource_pack_hash, oldpack->pack_name, &pack);

		if (pack)
		{
			if (pack->files)
				stashTableDestroy(pack->files);
			if (pack->files_nopath)
				stashTableDestroy(pack->files_nopath);
			StructDestroy(parse_ResourcePack, pack);
		}
	}

	if (callback_type == eParseReloadCallbackType_Delete)
		return 1;

	stashAddPointer(pack_containers[cur_type].parsed_pack_hash, newpack->pack_name, newpack, false);
	eaPush(&pack_containers[cur_type].pack_names, newpack->pack_name);

	return 1;
}

static void reloadPackCallback(const char *relpath, int when)
{
	int i;
	for (i = 0; i < RPACK_TYPE_COUNT; ++i)
	{
		if (strnicmp(relpath, pack_type_data[i].pack_dir, strlen(pack_type_data[i].pack_dir))==0)
		{
			fileWaitForExclusiveAccess(relpath);
			errorLogFileIsBeingReloaded(relpath);
			cur_type = i;
			if (!ParserReloadFile(relpath, parse_packs, &pack_containers[i].parsed_packs, reloadPackSubStructCallback, 0))
				wlStatusPrintf("Error reloading Pack file: %s", relpath);
			else
				wlStatusPrintf("Pack file reloaded: %s", relpath);
		}
	}
}

void wlLoadPacks(void)
{
	int i, j, total = 0;
	char buf[1024];

	loadstart_printf("Loading resource packs..");
	for (i = 0; i < RPACK_TYPE_COUNT; ++i)
	{
		ParserLoadFiles(pack_type_data[i].pack_dir, ".txt", pack_type_data[i].pack_bin, PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, parse_packs, &pack_containers[i].parsed_packs);

		pack_containers[i].parsed_pack_hash = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);
		pack_containers[i].resource_pack_hash = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);
		for (j = 0; j < eaSize(&pack_containers[i].parsed_packs); ++j)
		{
			stashAddPointer(pack_containers[i].parsed_pack_hash, pack_containers[i].parsed_packs[j]->pack_name, pack_containers[i].parsed_packs[j], false);
			eaPush(&pack_containers[i].pack_names, pack_containers[i].parsed_packs[j]->pack_name);
		}

		// Add callback for reloading
		strcpy(buf, pack_type_data[i].pack_dir);
		strcat(buf, "/*.txt");
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, buf, reloadPackCallback);

		total += eaSize(&pack_containers[i].parsed_packs);
	}
	loadend_printf(" done (%d packs).", total);
}

int wlCheckPackExists(const char *pack_name, ResourcePackType pack_type)
{
	assert(pack_type < RPACK_TYPE_COUNT);
	assert(pack_containers[pack_type].parsed_pack_hash);

	return !!stashFindPointerReturnPointer(pack_containers[pack_type].parsed_pack_hash, pack_name);
}

ResourcePack *wlGetResourcePack(const char *pack_name, ResourcePackType pack_type)
{
	ResourcePackParsed *pack_parsed;
	ResourcePack *pack;

	PERFINFO_AUTO_START_FUNC();

	assert(pack_type < RPACK_TYPE_COUNT);
	assert(pack_containers[pack_type].parsed_pack_hash);

	pack_parsed = stashFindPointerReturnPointer(pack_containers[pack_type].parsed_pack_hash, pack_name);
	if (!pack_parsed)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pack = stashFindPointerReturnPointer(pack_containers[pack_type].resource_pack_hash, pack_name);

	if (!pack)
	{
		pack = StructCreate(parse_ResourcePack);
		fillPackFromParsed(pack, pack_parsed, pack_type);
		stashAddPointer(pack_containers[pack_type].resource_pack_hash, pack_name, pack, false);
	}

	PERFINFO_AUTO_STOP();

	return pack;
}

static int freeResourcePackData(StashElement elem)
{
	ResourcePack *pack = stashElementGetPointer(elem);
	if (pack)
	{
		if (pack->files)
			stashTableDestroy(pack->files);
		if (pack->files_nopath)
			stashTableDestroy(pack->files_nopath);
		StructDestroy(parse_ResourcePack, pack);
	}
	return 1;
}

void wlReloadResourcePackType(ResourcePackType pack_type)
{
	stashForEachElement(pack_containers[pack_type].resource_pack_hash, freeResourcePackData);
	stashTableDestroy(pack_containers[pack_type].resource_pack_hash);
	pack_containers[pack_type].resource_pack_hash = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);
}

char ***wlGetResourcePackNames(ResourcePackType pack_type)
{
	assert(pack_type < RPACK_TYPE_COUNT);
	assert(pack_containers[pack_type].parsed_pack_hash);

	return &pack_containers[pack_type].pack_names;
}
