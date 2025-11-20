#include "patchfilespec.h"
#include "patchdb.h"
#include "patchcommonutils.h"

#include "error.h"
#include "FilespecMap.h"
#include "GlobalTypes.h"
#include "hoglib.h"
#include "logging.h"
#include "file.h"
#include "utils.h"
#include "sysutil.h"

#include "patchfilespec_h_ast.h"

#include "windefinclude.h"

#if _XBOX
#include "xbox.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

typedef struct FileSpecMapper 
{
	U32 hoggs;
	U32 folders;
	StringArray hogg_names;
	StringArray hogg_names_no_path;
	StringArray hogg_strip_paths;
	HogFile *** hogg_handles;
	FilespecMap * spec_map;
	FilespecMap ** flag_maps;
	FilespecMap *control_map;
	StringArray root_folders;
	bool single_app_mode;
	U32 *hogg_flags;
	U32 *mirror_stripped;
	fileSpecLogFptr logger;
	void *loggerdata;
} FileSpecMapper;

bool fileSpecUnloadHoggs(FileSpecMapper * fsm)
{
	U32 i, j;
	bool success = true;

	if(fsm->hogg_handles)
	{
		for(i = 0; i < fsm->folders; i++)
		{
			for(j=0; j<fsm->hoggs; j++)
			{
				if(fsm->hogg_handles[i][j])
				{
					HogFile *hogg = fsm->hogg_handles[i][j];

					// If someone else has a handle to this hogg, the destroy below will not close it.
					// In this case, we'll flush, and return false, to indicate the hogg file has not
					// been closed.
					if (hogFileGetSharedRefCount(hogg) != 1)
						success = false;
					hogFileModifyFlush(hogg);

					// Close the hogg.
					hogFileDestroy(hogg, true);
				}
			}
		}
		free(fsm->hogg_handles);
		fsm->hogg_handles = NULL;
	}
	return success;
}

bool fileSpecHasHoggsLoaded(FileSpecMapper * fsm)
{
	return !!fsm->hogg_handles;
}


void fileSpecFlushHoggs(FileSpecMapper * fsm)
{
	bool flushed = false;

	if(fsm && fsm->hogg_handles)
	{
		U32 i, j;

		for(i = 0; i < fsm->folders; i++)
		{
			for(j = 0; j < fsm->hoggs; j++)
			{
				if(fsm->hogg_handles[i][j])
				{
					hogFileModifyFlush(fsm->hogg_handles[i][j]);
					flushed = true;
				}
			}
		}
	}

#ifdef _XBOX
	if(!flushed)
		XFlushUtilityDrive();
#endif
}

int fileSpecGetHoggIndexForHandle(FileSpecMapper * fsm, HogFile * hogg)
{
	U32 i, j;

	if(!fsm->hogg_handles)
		return -1;

	for(i = 0; i < fsm->folders; i++)
	{
		for(j = 0; j < fsm->hoggs; j++)
		{
			if(fsm->hogg_handles[i][j] == hogg)
				return j;
		}
	}

	return -1;
}

bool fileSpecIsAHogg(FileSpecMapper *fsm, const char *filename)
{
	int i;
	char buf[MAX_PATH];

	if(!filename)
		return false;

	strcpy(buf, filename);
	forwardSlashes(buf);

	for(i = 0; i < (int)fsm->hoggs; i++)
		if(fsm->hogg_names[i] && !stricmp(fsm->hogg_names[i], buf))
			return true;
	return false;
}

char * fileSpecGetStripPath(FileSpecMapper * fsm, int index)
{
	return fsm->hogg_strip_paths[index];
}

bool fileSpecGetMirrorStriped(FileSpecMapper * fsm, int index)
{
	return fsm->mirror_stripped[index];
}

// Return the index of a flag.
static int filespecGetFlagIndex(int flag_to_check)
{
	int index = 0;

	while(flag_to_check > 1)
	{
		flag_to_check = (flag_to_check >> 1);
		index++;
	}

	return index;
}

static int filespecGetNumElements(FileSpecMapper * fsm, int flag_to_check)
{
	int index = filespecGetFlagIndex(flag_to_check);

	if(eaSize(&fsm->flag_maps) <= index || !(fsm->flag_maps[index]))
		return -1;
	else
		return filespecMapGetNumElements(fsm->flag_maps[index]);
}

static bool fileSpecCheckFlag(FileSpecMapper * fsm, const char * filename, int flag_to_check, bool fallback, bool *found, size_t *count, const char **match)
{
	int map_value;
	int index = filespecGetFlagIndex(flag_to_check);

	if(eaSize(&fsm->flag_maps) <= index || !(fsm->flag_maps[index]))
	{
		if (found)
			*found = false;
		if (count)
			*count = -1;
		if (match)
			*match = NULL;
		return fallback;
	}
	else
	{
		if (!filespecMapGetInt(fsm->flag_maps[index], filename, &map_value))
		{
			if (found)
				*found = false;
			if (count)
				*count = 0;
			if (match)
				*match = NULL;
			return false;
		}
		if (found)
			*found = true;
		if (count)
			*count = filespecMapGetCount(fsm->flag_maps[index], filename);
		if (match)
			*match = filespecMapGetMatchFilespec(fsm->flag_maps[index], filename);
		return !!map_value;
	}
}

bool fileSpecIsMirrored(FileSpecMapper * fsm, const char * filename)
{
	return fileSpecCheckFlag(fsm, filename, FILESPEC_MIRRORED_OUTSIDE_HOGGS, false, NULL, NULL, NULL);
}

bool fileSpecIsNotRequired(FileSpecMapper * fsm, const char * filename)
{
	return fileSpecCheckFlag(fsm, filename, FILESPEC_NOT_REQUIRED, false, NULL, NULL, NULL);
}

bool fileSpecIsNotRequiredDebug(FileSpecMapper * fsm, const char * filename, char ** estrDebug)
{
	bool found;
	size_t count;
	const char *match;
	bool result = fileSpecCheckFlag(fsm, filename, FILESPEC_NOT_REQUIRED, false, &found, &count, &match);

	if (estrDebug)
		estrPrintf(estrDebug, "found = %d, match_count = %lu, total = %d, match = \"%s\"", !!found, (unsigned long)count, filespecGetNumElements(fsm, FILESPEC_NOT_REQUIRED),
			NULL_TO_EMPTY(match));

	return result;
}

bool fileSpecIsIncluded(FileSpecMapper * fsm, const char * filename)
{
	return fileSpecCheckFlag(fsm, filename, FILESPEC_INCLUDED, true, NULL, NULL, NULL);
}

bool fileSpecIsNoWarn(FileSpecMapper * fsm, const char * filename)
{
	return fileSpecCheckFlag(fsm, filename, FILESPEC_NOWARN, false, NULL, NULL, NULL);
}

bool fileSpecIsUnderSourceControl(FileSpecMapper *fsm, const char *filename)
{
	int type;
	return !filespecMapGetInt(fsm->control_map, filename, &type) || type != CONTROLSPEC_EXCLUDED;
}

bool fileSpecIsBin(FileSpecMapper *fsm, const char *filename)
{
	int type;
	if(!fsm) return false;
	return filespecMapGetInt(fsm->control_map, filename, &type) && type == CONTROLSPEC_BINFILES;
}

bool fileSpecHasNotRequired(FileSpecMapper *fsm)
{
	int flag = FILESPEC_NOT_REQUIRED;
	int index = filespecGetFlagIndex(flag);

	return eaGet(&fsm->flag_maps, index) != NULL;
}

bool fileSpecIsHogFile(FileSpecMapper * fsm, const char * filename)
{
	U32 i;
	assert(filename);

	for(i = 0; i < fsm->hoggs; i++)
	{
		if(fsm->hogg_names[i] && stricmp(filename, fsm->hogg_names[i]) == 0)
			return true;
	}

	return false;
}

void fileSpecLoadHoggs(FileSpecMapper * fsm)
{
	U32 i, j;
	int ret;
	bool created;
	int unlink_ret;
	U32 flags;

	if(fsm->hogg_handles || !fsm->hoggs)
	{
		return;
	}
	fsm->hogg_handles = calloc(fsm->folders*(fsm->hoggs+1), sizeof(void*));

	for(i = 0; i < fsm->folders; i++)
	{
		fsm->hogg_handles[i] = (HogFile**)(fsm->hogg_handles + fsm->folders + (fsm->hoggs * i));
		assert(fsm->hogg_handles[i]);
		flags = fsm->hogg_flags[i];

		// TODO: Jimb is going to think about if there is a better way to handle this.
		// For now we can't use readonly because hoggs with a journal will assert when it
		// tries to apply. <NPK 2010-03-22 t:COR-8264>
		flags &= ~HOG_READONLY; 

		for(j = 0; j < fsm->hoggs; j++)
		{
			char hog_fname[MAX_PATH];

			fileSpecGetHoggName(fsm, i, j, SAFESTR(hog_fname));
			if(hog_fname[0] == '\0')
			{
				//printf("%i: (null hogg)\n", i);
				fsm->hogg_handles[i][j] = NULL;
			}
			else
			{
				//printf("opening hogg file %i: %s\n", i, hog_fname);
				if(!(flags & HOG_READONLY))
				{
					flags |= HOG_MUST_BE_WRITABLE;
					chmod(hog_fname, _S_IWRITE);
				}
				fsm->hogg_handles[i][j] = hogFileRead(hog_fname, &created, PIGERR_PRINTF, &ret, flags);
				if(!fsm->hogg_handles[i][j] && !i)
				{
					if(fileExists(hog_fname))
					{
						if (fsm->logger)
						{
							char *buffer = NULL;
							estrStackCreate(&buffer);
							estrPrintf(&buffer, "Deleting hogg %s after return value %i on hogFileRead\n", hog_fname, ret);
							fsm->logger(buffer, fsm->loggerdata);
							estrDestroy(&buffer);
						}
						unlink_ret = unlink(hog_fname);
					}

					fsm->hogg_handles[i][j] = hogFileRead(hog_fname, &created, PIGERR_PRINTF, NULL, flags);
				}

				if(fsm->hogg_handles[i][j])
				{
					hogFileSetSingleAppMode(fsm->hogg_handles[i][j], fsm->single_app_mode);
				}
			}
		}
	}
}

void fileSpecSetSingleAppMode(FileSpecMapper * fsm, bool singleAppMode)
{
	U32 i, j;

	fsm->single_app_mode = singleAppMode;

	if(!fsm->hogg_handles)
		return;

	for(i = 0; i < fsm->folders; i++)
	{
		for(j = 0; j < fsm->hoggs; j++)
		{
			if(fsm->hogg_handles[i][j])
				hogFileSetSingleAppMode(fsm->hogg_handles[i][j], fsm->single_app_mode);
		}
	}
}

void fileSpecSetRoot(FileSpecMapper * fsm, const char ** root_folders, U32 *hogg_flags)
{
	char folder[MAX_PATH], **tmp_folders=NULL;
	bool changed = false;
	int i;

	for(i = 0; i < eaSize(&root_folders); i++)
	{
		if(fileIsAbsolutePath(root_folders[i]))
			strcpy(folder, root_folders[i]);
		else
			sprintf(folder, "./%s", root_folders[i]);

		forwardSlashes(folder);
		eaPush(&tmp_folders, strdup(folder));

		if(i < (int)fsm->folders && stricmp(fsm->root_folders[i], folder)!=0)
			changed = true;
	}

	if(fsm->folders != eaSize(&root_folders))
		changed = true;

	if(changed)
	{
		fileSpecUnloadHoggs(fsm);
		if(fsm->root_folders)
			eaDestroyEx(&fsm->root_folders, NULL);
		assert(fsm->hogg_handles==NULL);
		fsm->folders = eaSize(&tmp_folders);
		fsm->root_folders = tmp_folders;
		ea32Copy(&fsm->hogg_flags, &hogg_flags);
		assertmsg(fsm->folders==ea32Size(&fsm->hogg_flags), "Different number of folders vs. hogg_flags");
	}
	else
	{
		eaDestroyEx(&tmp_folders, NULL);
	}
}

void fileSpecInit(FileSpecMapper * fsm,  FileSpecs * fsp, bool createHoggs, const char ** root_folders, U32 *hogg_flags)
{
	int i, names, specs, j, flags, flag, index;
	char * str;

	fsm->spec_map = filespecMapCreate();
	fsm->control_map = filespecMapCreateNoSorting();
	fsm->single_app_mode = true;

	if(root_folders)
		fileSpecSetRoot(fsm, root_folders, hogg_flags);

	fsm->hoggs = names = eaSize(&fsp->hoggspecs);
	for(i = 0; i < names; i++)
	{
		HogSpec * hspec = fsp->hoggspecs[i];
		if(hspec->filename)
		{
			forwardSlashes(hspec->filename);
			str = strrchr(hspec->filename, '/');
			if(str)
				str++;
			else
				str = hspec->filename;
			eaPush(&fsm->hogg_names, strdup(hspec->filename));
			eaPush(&fsm->hogg_names_no_path, strdup(str));
		}
		else
		{
			eaPush(&fsm->hogg_names, NULL);
			eaPush(&fsm->hogg_names_no_path, NULL);
		}
		if(hspec->strip)
			eaPush(&fsm->hogg_strip_paths, strdup(hspec->strip));
		else
			eaPush(&fsm->hogg_strip_paths, NULL);
		ea32Push(&fsm->mirror_stripped, hspec->mirror_stripped);
		specs = eaSize(&hspec->filespecs);
		for(j = 0; j < specs; j++)
			filespecMapAddInt(fsm->spec_map, hspec->filespecs[j]->spec, i);
	}

	flags = eaSize(&fsp->flagspecs);
	for(i = 0; i < flags; i++)
	{
		FlagSpec * fspec = fsp->flagspecs[i];

		index = 0;
		flag = fspec->flag;
		index = filespecGetFlagIndex(flag);

		if(index >= eaSize(&fsm->flag_maps))
			eaSetSize(&fsm->flag_maps, index + 1);

		if(! fsm->flag_maps[index])
			fsm->flag_maps[index] = filespecMapCreate();

		specs = eaSize(&fspec->filespecs);
		for(j = 0; j < specs; j++)
			filespecMapAddInt(fsm->flag_maps[index], fspec->filespecs[j]->spec, !fspec->filespecs[j]->exclude);
	}

	for(i = 0; i < eaSize(&fsp->controlspecs.specs); i++)
		filespecMapAddInt(fsm->control_map, fsp->controlspecs.specs[i]->spec, fsp->controlspecs.specs[i]->type);

	if(createHoggs)
		fileSpecLoadHoggs(fsm);
	else
		fsm->hogg_handles = NULL;
}

FileSpecMapper * fileSpecLoad(const char * filename, const char *extra_filename, bool createHoggs, const char ** root_folders, U32 *hogg_flags, fileSpecLogFptr logger, void *loggerdata)
{
	FileSpecs fsp = {0};
	FileSpecMapper * fsm = calloc(1,sizeof(FileSpecMapper));
	StructInit(parse_FileSpecs, &fsp);

	fsm->logger = logger;
	fsm->loggerdata = logger;

	if(filename)
	{
		char adjusted_path[MAX_PATH];
		int result;
		machinePath(adjusted_path,filename);
		result = ParserReadTextFile(adjusted_path, parse_FileSpecs, &fsp, 0);
		if (!result)
		{
			StructDeInit(parse_FileSpecs, &fsp);
			free(fsm);
			return NULL;
		}
		
		// Read .filespecoverride, if present, to simulate the effect of a Patch Server applying one.
		changeFileExt(adjusted_path, ".filespecoverride", adjusted_path);
		if (fileExists(adjusted_path))
			// Appends to EArrays, etc
			ParserReadTextFile(adjusted_path, parse_FileSpecs, &fsp, 0);

		// Read any extra filespec provided.  This will typically be another .filespecoverride, again to simulate a production filespec.
		if (extra_filename)
		{
			machinePath(adjusted_path,extra_filename);
			if (fileExists(adjusted_path))
				ParserReadTextFile(adjusted_path, parse_FileSpecs, &fsp, 0);
			else
				ParserReadTextFile(extra_filename, parse_FileSpecs, &fsp, 0);
		}
	}
	fileSpecInit(fsm, &fsp, createHoggs, root_folders, hogg_flags);

	StructDeInit(parse_FileSpecs, &fsp);
	return fsm;
}

FileSpecMapper * fileSpecLoadFromData(const char * data, size_t data_len, bool createHoggs, const char ** root_folders, U32 *hogg_flags, fileSpecLogFptr logger, void *loggerdata)
{
	FileSpecs fsp = {0};
	FileSpecMapper * fsm = calloc(1,sizeof(FileSpecMapper));
	char *data_copy = NULL;

	StructInit(parse_FileSpecs, &fsp);

	fsm->logger = logger;
	fsm->loggerdata = loggerdata;

	if(data && data[0])
	{
		int result;
		estrStackCreate(&data_copy);
		estrConcat(&data_copy, data, (int)data_len);
		result = ParserReadText(data_copy, parse_FileSpecs, &fsp, 0);
		if (!result)
		{
			StructDeInit(parse_FileSpecs, &fsp);
			free(fsm);
			return NULL;
		}
		estrDestroy(&data_copy);
	}
	fileSpecInit(fsm, &fsp, createHoggs, root_folders, hogg_flags);

	StructDeInit(parse_FileSpecs, &fsp);
	return fsm;
}

bool fileSpecPreprocessFile(const char * input_filename, const char * output_filename)
{
	FileSpecs fsp = {0};
	int result;

	// Try to read in the input file.
	result = ParserReadTextFile(input_filename, parse_FileSpecs, &fsp, 0);
	if (!result)
	{
		StructDeInit(parse_FileSpecs, &fsp);
		return false;
	}

	// Write body of filespec.
	result = ParserWriteTextFileEx(output_filename, parse_FileSpecs, &fsp, 0, 0, 0,
		"// WARNING - This file was automatically generated from %s by %s.\n"
		"// Please do not edit directly, or your changes will be lost!\n\n",
		NULL);
	if (!result)
	{
		ErrorFilenamef(output_filename, "Unable write struct to filespec!");
		result = fileForceRemove(output_filename);
		if (result)
			Errorf("Failed to remove partial output file after failing to write struct to filespec");
		StructDeInit(parse_FileSpecs, &fsp);
		return false;
	}

	return true;
}

int fileSpecHoggCount(FileSpecMapper * fsm)
{
	return fsm->hoggs;
}

int fileSpecFolderCount(FileSpecMapper * fsm)
{
	return fsm->folders;
}


const char * fileSpecGetHoggNameNoPath(FileSpecMapper * fsm, int index)
{
	return fsm->hogg_names_no_path[index];
}

void fileSpecGetHoggName(FileSpecMapper * fsm, U32 folder_index, U32 hogg_index, char * buf, int buf_size)
{
	if(fsm->hogg_names[hogg_index])
	{
		char *fname, *root_folder = fsm->root_folders[folder_index];
		if(root_folder && root_folder[0])
		{
			fname = alloca(buf_size);
			if(isSlash(root_folder[strlen(root_folder)-1]))
				snprintf_s(fname,buf_size,"%s%s",root_folder,fsm->hogg_names[hogg_index]);
			else
				snprintf_s(fname,buf_size,"%s/%s",root_folder,fsm->hogg_names[hogg_index]);
		}
		else
		{
			fname = fsm->hogg_names[hogg_index];
		}

		machinePath_s(buf, buf_size, fname);
	}
	else if(buf_size)
	{
		buf[0] = '\0';
	}
}

HogFile * fileSpecGetHoggHandleEx(FileSpecMapper * fsm, int folder_index, int hogg_index)
{
	if(folder_index == -1 || hogg_index == -1 || folder_index >= (int)fsm->folders || hogg_index >= (int)fsm->hoggs)
		return NULL;

	return fsm->hogg_handles[folder_index][hogg_index];
}

char * fileSpecGetHoggNameForFile(FileSpecMapper * fsm, const char * filename)
{
	int index = fileSpecGetHoggIndexForFile(fsm, filename);

	if(index == -1)
		return NULL;
	else
		return fsm->hogg_names[index];
}

int fileSpecGetHoggIndexForFile(FileSpecMapper * fsm, const char * filename)
{
	char * hogg_name = NULL;
	int index;
	bool result;

	result = filespecMapGetInt(fsm->spec_map, filename, &index);

	if(!result || index == -1)
		return -1;
	else
		return index;
}

HogFile * fileSpecGetHoggHandleForFile(FileSpecMapper * fsm, const char * filename)
{
	return fileSpecGetHoggHandle(fsm, fileSpecGetHoggIndexForFile(fsm, filename));
}

int fileSpecGetFileOrHoggForFileVersion(FileSpecMapper * fsm, FileVersion * ver, int hogg_index, bool allow_inexact, bool useFileOverlay, HogFileIndex * hfi_out, char (* full_path)[MAX_PATH], U32 * check_out)
{
	U32 i, newest_i = -1, newest_time = 0;
	char *stripped, fixed_path[MAX_PATH], test_path[MAX_PATH];
	bool newest_isFromFile = false;

	strcpy(fixed_path, ver->parent->path);

	// If we got an index, use that strip path.
	if(hogg_index != -1)
	{
		// Remove the strip path
		stripped = fileSpecGetStripPath(fsm, hogg_index);
		stripPath(fixed_path, stripped);
	}

	for(i = 0; i < fsm->folders; i++)
	{
		U32 time = 0;
		HogFile *hogg;
		HogFileIndex hfi = HOG_INVALID_INDEX;
		bool isFromFile = false;
		hogg = fileSpecGetHoggHandleEx(fsm, i, hogg_index);
		if(hogg)
		{
			hfi = hogFileFind(hogg, fixed_path);
			if(hfi != HOG_INVALID_INDEX)
			{
				time = hogFileGetFileTimestamp(hogg, hfi);
			}
		}
		if (!time && useFileOverlay)
		{
			sprintf(test_path, "%s/%s", fsm->root_folders[i], fixed_path);
			if(fileExists(test_path))
			{
				time = fileLastChangedAbsolute(test_path);
				isFromFile = true;
			}
		}
		if (!time)
			continue;
		if(time > newest_time)
		{
			newest_time = time;
			newest_i = i;
			newest_isFromFile = isFromFile;
		}
		if (hogg && hfi != HOG_INVALID_INDEX)
		{
			if(hogFileGetFileSize(hogg, hfi) == ver->size && hogFileGetFileChecksum(hogg, hfi) == ver->checksum)
			{
				if(hfi_out)
					*hfi_out = hfi;
				return i;
			}
		}
		else
		{
			if(fileSize(test_path) == ver->size && patchChecksumFile(test_path) == ver->checksum) //TODO: No checksums in main thread
			{
				if (check_out)
					*check_out = ver->checksum;
				strcpy(*full_path, test_path);
				return -1;
			}
		}

	}

	if(newest_isFromFile)
	{
		if (allow_inexact)
			sprintf(*full_path, "%s/%s", fsm->root_folders[newest_i], fixed_path);
		return -1;
	}
	// Not found.
	return allow_inexact ? newest_i : -1;
}

int fileSpectGetFolderIndexForFileVersion(FileSpecMapper * fsm, FileVersion * ver, int hogg_index, bool allow_inexact, HogFileIndex * hfi_out)
{
	U32 i, newest_i = -1, newest_time = 0;
	HogFile *hogg;
	HogFileIndex hfi;
	char *stripped, fixed_path[MAX_PATH];
	
	// Not in a hogg at all, chain the failure.
	if(hogg_index == -1)
		return -1;

	// Remove the strip path
	stripped = fileSpecGetStripPath(fsm, hogg_index);
	strcpy(fixed_path, ver->parent->path);
	stripPath(fixed_path, stripped);

	for(i = 0; i < fsm->folders; i++)
	{
		U32 time;
		hogg = fileSpecGetHoggHandleEx(fsm, i, hogg_index);
		if(!hogg)
			continue;
		hfi = hogFileFind(hogg, fixed_path);
		if(hfi == HOG_INVALID_INDEX)
			continue;
		time = hogFileGetFileTimestamp(hogg, hfi);
		if(time > newest_time)
		{
			newest_time = time;
			newest_i = i;
		}
		if(hogFileGetFileSize(hogg, hfi) == ver->size && hogFileGetFileChecksum(hogg, hfi) == ver->checksum)
		{
			if(hfi_out)
				*hfi_out = hfi;
			return i;
		}
	}

	// Not found. ???: Should this return 0 instead of -1? <NPK 2009-05-26>
	return allow_inexact ? newest_i : -1;
}

HogFile * fileSpecGetHoggHandleForFileVersionEx(FileSpecMapper * fsm, FileVersion * ver, bool allow_inexact, bool useFileOverlay, HogFileIndex * hfi_out, char (* full_path)[MAX_PATH], U32 * check_out)
{
	int hogg_index = fileSpecGetHoggIndexForFile(fsm, ver->parent->path);
	int folder_index = fileSpecGetFileOrHoggForFileVersion(fsm, ver, hogg_index, allow_inexact, useFileOverlay, hfi_out, full_path, check_out);
	return fileSpecGetHoggHandleEx(fsm, folder_index, hogg_index);
}

void fileSpecDestroy(FileSpecMapper ** fsm)
{
	if(*fsm == NULL)
		return;

	fileSpecUnloadHoggs(*fsm);

	if((*fsm)->spec_map)
		filespecMapDestroy((*fsm)->spec_map);
	if((*fsm)->control_map)
		filespecMapDestroy((*fsm)->control_map);

	eaDestroyEx(&(*fsm)->flag_maps, filespecMapDestroy);
	eaDestroyEx(&(*fsm)->hogg_names, NULL);
	eaDestroyEx(&(*fsm)->hogg_names_no_path, NULL);
	eaDestroyEx(&(*fsm)->hogg_strip_paths, NULL);
	ea32Destroy(&(*fsm)->mirror_stripped);

	eaDestroyEx(&(*fsm)->root_folders, NULL);

	free(*fsm);
	*fsm = NULL;
}

#include "patchfilespec_h_ast.c"