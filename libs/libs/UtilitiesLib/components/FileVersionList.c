// Utilities for reading and writing file version lists, which are identical in format to
// the manifest files used by the patchserver/client

#include "structInternals.h"
#include "FileVersionList.h"
#include "trivia.h"

#include "AutoGen/FileVersionList_h_ast.h"

SavedFileVersionList *CreateFileVersionList(void)
{
	return StructCreate(parse_SavedFileVersionList);
}

void DestroyFileVersionList(SavedFileVersionList *list)
{
	StructDestroy(parse_SavedFileVersionList, list);
}

SavedFileVersion *GetFileVersionFromList(SavedFileVersionList *list, const char *filename)
{
	char fixedFilename[CRYPTIC_MAX_PATH];
	strcpy(fixedFilename, filename);
	forwardSlashes(fixedFilename);
	fixDoubleSlashes(fixedFilename);

	return eaIndexedGetUsingString(&list->versions, fixedFilename);
}

SavedFileVersion *AddFileVersionToList(SavedFileVersionList *list, const char *filename, U32 modified, U32 size, U32 checksum, U32 header_size, U32 header_checksum, U32 flags)
{
	SavedFileVersion *pVersion;
	char fixedFilename[CRYPTIC_MAX_PATH];
	strcpy(fixedFilename, filename);
	forwardSlashes(fixedFilename);
	fixDoubleSlashes(fixedFilename);

	pVersion =  eaIndexedGetUsingString(&list->versions, fixedFilename);
	if (!pVersion)
	{
		pVersion = StructCreate(parse_SavedFileVersion);
		pVersion->filename = strdup(fixedFilename);
		eaIndexedAdd(&list->versions, pVersion);
	}
	pVersion->modified = modified;
	pVersion->size = size;
	pVersion->checksum = checksum;
	pVersion->header_size = header_size;
	pVersion->header_checksum = header_checksum;
	pVersion->flags = flags;

	return pVersion;
}

void RemoveFileVersionFromList(SavedFileVersionList *list, const char *filename)
{
	int index = -1;
	SavedFileVersion *pVersion;
	char fixedFilename[CRYPTIC_MAX_PATH];
	strcpy(fixedFilename, filename);
	forwardSlashes(fixedFilename);
	fixDoubleSlashes(fixedFilename);

	index = eaIndexedFindUsingString(&list->versions, fixedFilename);

	if (index >= 0)
	{
		pVersion = eaRemove(&list->versions, index);
		StructDestroy(parse_SavedFileVersion, pVersion);
	}
}


static bool parseString(char **line, char **str)
{
	if(*line == NULL)
		return false;
	*str = *line;
	*line = strchr(*line, '\t');
	if(*line)
		*((*line)++) = '\0';
	return true;
}

static bool parseInt(char **line, int *i)
{
	if(*line == NULL || (!isdigit(**line) && **line != '-'))
		return false;
	*i = atoi(*line);
	*line = strchr(*line, '\t');
	if(*line)
		++*line;
	return true;
}

bool ReadFileVersionList(SavedFileVersionList *list, const char *filename)
{
	char fixedFilename[MAX_PATH];
	char *manifest_str, *line, *nextline;
	bool bSuccess = true;
	TriviaMutex mutex;

	strcpy(fixedFilename, filename);
	forwardSlashes(fixedFilename);
	fixDoubleSlashes(fixedFilename);

	mutex = triviaAcquireDumbMutex(fixedFilename);
	manifest_str = fileAlloc(fixedFilename, NULL);
	triviaReleaseDumbMutex(mutex);

	if(!manifest_str)
	{
//		printf("Couldn't load manifest \"%s\"\n", manifest_name);
		return false;
	}

	for(line = manifest_str; *line != '\0'; line = nextline)
	{
		char *path;
		int verflags = 0;
		U32 modified, size, checksum, header_size, header_checksum;

		nextline = strchr(line, '\n');
		if(!nextline)
		{
			bSuccess = false;
			break;
		}
		*(nextline++) = '\0';

		if(*line == '#')
			continue;

		if(!parseString(&line, &path))
		{
			bSuccess = false;
			break;
		}
		
		if( !parseInt(&line, &modified) ||
			!parseInt(&line, &size) ||
			!parseInt(&line, &checksum) ||
			!parseInt(&line, &header_size) ||
			!parseInt(&line, &header_checksum) ||
			!parseInt(&line, &verflags))
		{
			bSuccess = false;
			break;
		}

		AddFileVersionToList(list, path, modified, size, checksum, header_size, header_checksum, verflags);
	}

	//printf("done.\n");

	free(manifest_str);
	return bSuccess;
}

bool WriteFileVersionList(SavedFileVersionList *list, const char *filename)
{
	char fixedFilename[MAX_PATH], absolutePath[MAX_PATH];
	bool bSuccess = true;
	TriviaMutex mutex;
	FILE* file;
	int i;

	strcpy(fixedFilename, filename);
	forwardSlashes(fixedFilename);
	fixDoubleSlashes(fixedFilename);

	mutex = triviaAcquireDumbMutex(fixedFilename);
	
	if (fileLocateWrite(fixedFilename, absolutePath))
	{	
		file = fopen(absolutePath, "wt");

		if (file)
		{		
			for (i = 0; i < eaSize(&list->versions); i++)
			{
				SavedFileVersion *ver = list->versions[i];

				fprintf(file, "%s\t%d\t%d\t%d\t%d\t%d\t%d\n",
					ver->filename, ver->modified,
					ver->size, ver->checksum,
					ver->header_size, ver->header_checksum, ver->flags);
			}
			fclose(file);
			bSuccess = true;
		}
		else
		{
			bSuccess = false;
		}
	}
	else
	{
		bSuccess = false;
	}

	triviaReleaseDumbMutex(mutex);

	return bSuccess;
}

#include "AutoGen/FileVersionList_h_ast.c"