#include "fileutil2.h"
#include "fileWatch.h"
#include "strings_opt.h"

#include "winfiletime.h"
#include "timing.h"

#include "fileutil.h"
#if !_PS3
#include "sys/utime.h"
#endif
#include "estring.h"
#include "FileUtil2_h_ast.h"
#include "stashTable.h"
#include "stringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


typedef struct FileScanData {
	char**				names;
	FileScanFolders		scan_folders;
	int					iFileCount;
	int					iFolderCount;
	FileScanFeedbackFunc pFeedbackCB;
} FileScanData;

static void fileScanDirRecurse(const char *dir,FileScanData* data, int iNumRecurseLevels)
{
	WIN32_FIND_DATAA wfd;
	U32				handle;
	S32				good;
	char			buf[1200];

	data->iFolderCount++;

 	strcpy(buf,dir);
	strcat(buf,"/*");

	for(good = fwFindFirstFile(&handle, buf, &wfd); good; good = fwFindNextFile(handle, &wfd))
	{
		if( wfd.cFileName[0] == '.'
			||
			!(data->scan_folders & FSF_UNDERSCORED) &&
			wfd.cFileName[0] == '_')
		{
			continue;
		}
		if (wfd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) && (data->scan_folders & FSF_NOHIDDEN))
			continue;

		STR_COMBINE_SSS(buf, dir, strchr("\\/", dir[strlen(dir)-1])==0?"/":"", wfd.cFileName);

		if ((data->scan_folders & FSF_FILES) && !(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
				(data->scan_folders & FSF_FOLDERS) && (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			data->iFileCount++;
			if (data->pFeedbackCB)
			{
				data->pFeedbackCB(data->iFileCount, data->iFolderCount);
			}

			// Add to the list
			if (data->scan_folders & FSF_RETURNSHORTNAMES)
			{
				char tempBuf[CRYPTIC_MAX_PATH];
				getFileNameNoExtNoDirs(tempBuf, buf);
	            eaPush( &data->names, strdup( tempBuf ));
			}
			else if (data->scan_folders & FSF_RETURNLOCALNAMES)
			{
				char tempBuf[CRYPTIC_MAX_PATH];
				getFileNameNoDir(tempBuf, buf);
	            eaPush( &data->names, strdup( tempBuf ));
			}
			else
			{
	            eaPush( &data->names, strdup( buf ));
			}
		}
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && iNumRecurseLevels > 0)
		{
			fileScanDirRecurse(buf,data,iNumRecurseLevels - 1);
		}
	}
	fwFindClose(handle);
}

/*given a directory, it returns an string array of the full path to each of the files in that directory,
and it fills count_ptr with the number of files found.  files and sub-folders prefixed with "_" or "." 
are ignored.  This requires an absolute path, and is intented only to be used in utilities.  For the
game code, run fileScanAllDataDirs instead.
*/
char **fileScanDir(const char *dir)
{
	FileScanData data = {0};
	
	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	data.names=0;
	data.scan_folders = FSF_FILES;

	fileScanDirRecurse(dir,&data,INT_MAX);
	
	return data.names;
}

/* same as above, but does not recurse into subdirectories */
char **fileScanDirNoSubdirRecurse(const char *dir)
{
	FileScanData data = {0};
	
	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	data.names=0;
	data.scan_folders = FSF_FILES;

	fileScanDirRecurse(dir,&data,0);
	
	return data.names;
}

char **fileScanDirRecurseNLevels(const char *dir, int iNumLevels) //if iNumLevels == 1, scans the root and its children, but no grandchildren, etc
{
	FileScanData data = {0};
	
	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	data.names=0;
	data.scan_folders = FSF_FILES;

	fileScanDirRecurse(dir,&data,iNumLevels);
	
	return data.names;
}



// Allows the caller to specify whether folders should be returned in the list
// or not (can ask for just files, just folders, or both)
char **fileScanDirFolders(const char *dir, FileScanFolders folders)
{
	FileScanData data = {0};
	
	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	data.names=0;
	data.scan_folders = folders;

	fileScanDirRecurse(dir,&data,INT_MAX);

	return data.names;
}


char **fileScanDirFoldersWithFeedback(const char *dir, FileScanFolders folders, FileScanFeedbackFunc pCallback)
{
	FileScanData data = {0};
	
	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	data.names=0;
	data.scan_folders = folders;
	data.pFeedbackCB = pCallback;

	fileScanDirRecurse(dir,&data,INT_MAX);

	return data.names;
}


/* same as above, but does not recurse into subdirectories */
char **fileScanDirFoldersNoSubdirRecurse(const char *dir, FileScanFolders folders)
{
	FileScanData data = {0};

	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	data.names=0;
	data.scan_folders = folders;

	fileScanDirRecurse(dir,&data,0);

	return data.names;
}


void fileScanDirFreeNames(char **names)
{
    eaDestroyEx( &names, NULL );
}

bool file_scan_dirs_skip_dot_paths=true;

/* Function fileScandirRecurseEx()
 *	This function traverses the specified directory, one item at a time.  The function
 *	passes each encountered item back to FileScanProcessor.
 *
 *	While the function itself is "thread-safe". Its intended usage is not.  The caller
 *	is calling this function to perform processing on multiple items.  Most likely, the
 *	caller will have to keep track of *some* context information when calling this function.
 *	Since this function does not provide any way for the caller to retrieve the said context,
 *  the context must be some global variable.  Thread-safty is then broken.
 *
 *	One way to solve this problem is to require the caller to pass in a structure that
 *	conforms to some interface.
 *
 *
 *
 */
void fileScanDirRecurseEx(const char* dir, FileScanProcessor processor, void *pUserData){
	WIN32_FIND_DATAA wfd;
	U32				handle;
	S32				good;
	char 			buffer[1024];
	char 			dir2[1024];
	FileScanAction	action;

	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	strcpy(dir2, dir);
	strcpy(buffer, dir);
	strcat(buffer, "/*");

	for(good = fwFindFirstFile(&handle, buffer, &wfd); good; good = fwFindNextFile(handle, &wfd)){
		struct _finddata32_t fd;
		
		if (file_scan_dirs_skip_dot_paths)
		{
			if(wfd.cFileName[0] == '.')
				continue;
		} else {
			// only skip "." and ".."
			if(wfd.cFileName[0] == '.' && (wfd.cFileName[1] == '.' || wfd.cFileName[1] == '\0'))
				continue;
		}
			
		strcpy(fd.name, wfd.cFileName);
		
		fd.size = wfd.nFileSizeLow;
		_FileTimeToUnixTime(&wfd.ftLastWriteTime, &fd.time_write, FALSE);
		fd.time_write = statTimeFromUTC(fd.time_write);
		fd.attrib = (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? _A_HIDDEN : 0) |
					(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? _A_SUBDIR : 0) |
					(wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? _A_SYSTEM : 0) |
					(wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY ? _A_RDONLY : _A_NORMAL) |
					0;
		
		action = processor(dir2, &fd, pUserData);

		if(	action & FSA_EXPLORE_DIRECTORY && 
			wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{

			STR_COMBINE_SSS(buffer, dir2, "/", wfd.cFileName);
			
			fileScanDirRecurseEx(buffer, processor, pUserData);
		}

		if(action & FSA_STOP)
			break;
	}
	fwFindClose(handle);
}


FileScanAction printAllFileNames(char* dir, struct _finddata32_t* data){
	printf("%s/%s\n", dir, data->name);
	return FSA_EXPLORE_DIRECTORY;
}

void fileScanDirRecurseContext(const char* dir, FileScanContext* context){
	WIN32_FIND_DATAA wfd;
	U32				handle;
	S32				good;
	char			buffer[1024];
	char			dir2[1024];

	assert(fileIsAbsolutePath(dir) && "Only works on absolute paths!  This function is only to be used in utilities, not in the game code, use fileScanAllDataDirs isntead");

	strcpy(dir2, dir);
	strcpy(buffer, dir);
	if (fileExists(buffer)) {
		// If this is a file, let it find it!
		char *z = max(strrchr(dir2, '/'), strrchr(dir2, '\\'));
		if (z) {
			*++z=0;
		}
	} else {
		// Only add a wildcard if this is a folder passed in.
		if (strchr(buffer, '*')!=0) {
			// Already has a wildcard
			// fix up dir2
			char *z = max(strrchr(dir2, '/'), strrchr(dir2, '\\'));
			if (z) {
				*++z=0;
			}
		} else {
			strcat(buffer, "/*");
		}
	}

	for(good = fwFindFirstFile(&handle, buffer, &wfd); good; good = fwFindNextFile(handle, &wfd)){
		struct _finddata32_t	fd;
		FileScanAction		action;
		
		if(	wfd.cFileName[0] == '.' &&
			(	!wfd.cFileName[1] ||
				wfd.cFileName[1] == '.'))
		{
			continue;
		}

		//for(test = handle = _findfirst32(buffer, &fileinfo); test >= 0; test = _findnext32(handle, &fileinfo)){
		//	if(fileinfo.name[0] == '.' && (fileinfo.name[1] == '\0' || fileinfo.name[1] == '.'))
		//		continue;
		
		strcpy(fd.name, wfd.cFileName);
		
		fd.size = wfd.nFileSizeLow;
		_FileTimeToUnixTime(&wfd.ftLastWriteTime, &fd.time_write, FALSE);
		fd.time_write = statTimeFromUTC(fd.time_write);
		fd.attrib = (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? _A_HIDDEN : 0) |
					(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? _A_SUBDIR : 0) |
					(wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? _A_SYSTEM : 0) |
					(wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY ? _A_RDONLY : _A_NORMAL) |
					0;

		context->dir = dir2;
		context->fileInfo = &fd;
		action = context->processor(context);

		if(	action & FSA_EXPLORE_DIRECTORY && 
			wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			STR_COMBINE_SSS(buffer, dir2, "/", fd.name);
			
			fileScanDirRecurseContext(buffer, context);
		}

		if(action & FSA_STOP)
			break;
	}
	fwFindClose(handle);
}

bool CountFilesInDir(char *pDirName)
{
	int iCount = 0;
#if !PLATFORM_CONSOLE
	char **ppFileList;



	ppFileList = fileScanDirFolders(pDirName, FSF_FILES);
    iCount = eaSize( &ppFileList );
	
	fileScanDirFreeNames(ppFileList);
#endif

	return iCount;
}

static bool FileMatchesCriterion(char *pFileName, U32 iModTime, U32 iCurTime, PurgeDirectoryCriterion *pCriterion)
{
	if (pCriterion->pNameToMatch == NULL || strstri(pFileName, pCriterion->pNameToMatch))
	{
		if ((iCurTime > iModTime) &&  ((int) (iCurTime - iModTime) > pCriterion->iSecondsOld))
		{
			return true;
		}
	}


	return false;
}

static bool FileMatchesCriteria(char *pFileName, U32 iModTime, U32 iCurTime, PurgeDirectoryCriterion ***pppCriteria)
{
	int i;

	for (i=0; i < eaSize(pppCriteria); i++)
	{
		if (FileMatchesCriterion(pFileName, iModTime, iCurTime, (*pppCriteria)[i]))
		{
			return true;
		}
	}

	return false;
}

void PurgeDirFeedbackCB(int iNumFiles, int iNumFolders)
{
	if (iNumFiles % 100 == 0)
	{
		printf("Processed %d files, %d folders\n", iNumFiles, iNumFolders);
	}
}


int PurgeDirectoryOfOldFiles_MultipleCriteria(const char *pDir, PurgeDirectoryCriterion ***pppCriteria)
{
	U32 iCurTime = timeSecondsSince2000();
	int iCountErased = 0;
#if !PLATFORM_CONSOLE
	int iCount, i;
	char **ppFileList;

	printf("About to purge old files from %s\n", pDir);

	ppFileList = fileScanDirFoldersWithFeedback(pDir, FSF_FILES, PurgeDirFeedbackCB);
    iCount = eaSize( &ppFileList );

	for (i = 0 ; i < iCount; i++)
	{
		U32 iModTime = fileLastChangedSS2000(ppFileList[i]);
		backSlashes(ppFileList[i]);
		if (FileMatchesCriteria(ppFileList[i], iModTime, iCurTime, pppCriteria))
		{
			char systemString[1024];
			printf("About to erase %s\n", ppFileList[i]);
			sprintf(systemString, "erase %s", ppFileList[i]);
			iCountErased++;
			backSlashes(systemString);
			system(systemString);
		}
	}

	fileScanDirFreeNames(ppFileList);
	printf("All files erased... will attempte to erase empty directories now\n");

	ppFileList = fileScanDirFolders(pDir, FSF_FOLDERS);
	iCount = eaSize( &ppFileList );

	for (i = 0 ; i < iCount; i++)
	{
		char systemString[1024];
		printf("About to try to remove directory %s\n", ppFileList[i]);
		sprintf(systemString, "rd %s", ppFileList[i]);
		backSlashes(systemString);
		system(systemString);
	}
	fileScanDirFreeNames(ppFileList);
#endif

	return iCountErased;

}



int PurgeDirectoryOfOldFiles_Secs(const char *pDir, int iMinSecsOld, char *pStringToMatch, bool bDontRemoveDirectories)
{
	int iCountErased = 0;
#if !PLATFORM_CONSOLE
	int iCount, i;
	char **ppFileList;

	ppFileList = fileScanDirFolders(pDir, FSF_FILES);
    iCount = eaSize( &ppFileList );

	for (i = 0 ; i < iCount; i++)
	{
		if (((timeSecondsSince2000() - fileLastChangedSS2000(ppFileList[i]))) > (U32)iMinSecsOld &&
			(!pStringToMatch || strstri(ppFileList[i], pStringToMatch)))
		{
			char systemString[1024];
			printf("About to erase %s\n", ppFileList[i]);
			sprintf(systemString, "erase %s", ppFileList[i]);
			iCountErased++;
			backSlashes(systemString);
			system(systemString);
		}
	}
	
	fileScanDirFreeNames(ppFileList);


	if (!bDontRemoveDirectories)
	{
		ppFileList = fileScanDirFolders(pDir, FSF_FOLDERS);
		iCount = eaSize( &ppFileList );

		for (i = 0 ; i < iCount; i++)
		{
			char systemString[1024];
			printf("About to try to remove directory %s\n", ppFileList[i]);
			sprintf(systemString, "rd %s", ppFileList[i]);
			backSlashes(systemString);
			system(systemString);
		}
		fileScanDirFreeNames(ppFileList);
	}
#endif

	return iCountErased;
}

typedef struct FileSizeGetter
{
	char *pStringToMatch;
	U64 iSize;
	U32 iTimeCutoff;
} FileSizeGetter;


static FileScanAction GetSizeOfOldFilesProcessor(char *dir, struct _finddata32_t *data, FileSizeGetter *pGetter)
{

	char fullName[CRYPTIC_MAX_PATH];
	
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;

	sprintf(fullName, "%s/%s", dir, data->name);

	if (pGetter->pStringToMatch)
	{
		if (!strstri(fullName, pGetter->pStringToMatch))
		{
			return FSA_EXPLORE_DIRECTORY;
		}
	}


	if (timeGetSecondsSince2000FromWindowsTime32(data->time_write) >= pGetter->iTimeCutoff)
	{
		return FSA_EXPLORE_DIRECTORY;
	}

	pGetter->iSize += data->size;

	return FSA_EXPLORE_DIRECTORY;
}


U64 GetSizeOfOldFiles_Secs(const char *pDir, int iMinSecsOld, char *pStringToMatch)
{
	FileSizeGetter getter = {0};
	getter.pStringToMatch = pStringToMatch;
	getter.iTimeCutoff = timeSecondsSince2000() - iMinSecsOld;

	fileScanDirRecurseEx(pDir, GetSizeOfOldFilesProcessor, &getter);

	return getter.iSize;
}

void ReplaceStrings(char *pWorkString, int iWorkStringBufferSize, const char *pLookFor, const char *pReplaceWith, bool bBackslashes)
{
	int iLookLen = (int)strlen(pLookFor);
	int iReplaceLen = (int)strlen(pReplaceWith);
	int iWorkLen = (int)strlen(pWorkString);
	int iCount = 0;

	char *pFound;
	
	char *pTemp;

	if (strcmp(pLookFor, pReplaceWith) == 0)
	{
		return;
	}

	while (pFound = strstr(pWorkString, pLookFor))
	{
		int delta;
		iCount++;

		assertmsgf(iCount < 1000, "Presumed infinite recursion in ReplaceStrings, replacing %s with %s",
			pLookFor, pReplaceWith);

		assertmsg(iWorkLen + iReplaceLen - iLookLen < iWorkStringBufferSize - 1, "ReplaceStrings overflow");

		memmove(pFound + iReplaceLen, pFound + iLookLen,  iWorkLen - (pFound - pWorkString) - iLookLen + 1);

		memcpy(pFound, pReplaceWith, iReplaceLen);
		// Advance work pointer past what we replaced to prevent infinite loops
		delta = (pFound + iReplaceLen) - pWorkString;
		iWorkStringBufferSize -= delta;
		iWorkLen -= delta;
		pWorkString += delta;

		if (bBackslashes)
		{
			for (pTemp = pFound; pTemp < pFound + iReplaceLen; pTemp++)
			{
				if (*pTemp == '/')
				{
					*pTemp = '\\';
				}
			}
		}

		iWorkLen += iReplaceLen - iLookLen;
	}
}

void ApplyDirectoryMacros(char workString[1024], bool bBackslashes)
{
	ReplaceStrings(workString, 1024, "$SRC$", fileSrcDir(), bBackslashes);
	ReplaceStrings(workString, 1024, "$DATA$", fileDataDir(), bBackslashes);
	ReplaceStrings(workString, 1024, "$TOOLSBIN$", fileToolsBinDir(), bBackslashes);
	if (fileCoreSrcDir())
	{
		ReplaceStrings(workString, 1024, "$CORESRC$", fileCoreSrcDir(), bBackslashes);
		ReplaceStrings(workString, 1024, "$COREDATA$", fileCoreDataDir(), bBackslashes);
	}
	ReplaceStrings(workString, 1024, "$CORETOOLSBIN$", fileCoreToolsBinDir(), bBackslashes);
}

void ApplyDirectoryMacrosToEString(char **ppEString, bool bBackslashes)
{
	if (bBackslashes)
	{
		char tempString[CRYPTIC_MAX_PATH];

		strcpy(tempString, fileSrcDir());
		backSlashes(tempString);
		estrReplaceOccurrences(ppEString, "$SRC$", tempString);

		strcpy(tempString, fileDataDir());
		backSlashes(tempString);
		estrReplaceOccurrences(ppEString, "$DATA$", tempString);

		strcpy(tempString, fileToolsBinDir());
		backSlashes(tempString);
		estrReplaceOccurrences(ppEString, "$TOOLSBIN$", tempString);

		if (fileCoreSrcDir())
		{
			strcpy(tempString, fileCoreSrcDir());
			backSlashes(tempString);
			estrReplaceOccurrences(ppEString, "$CORESRC$", tempString);

			strcpy(tempString, fileCoreDataDir());
			backSlashes(tempString);
			estrReplaceOccurrences(ppEString, "$COREDATA$", tempString);
		}
		strcpy(tempString, fileCoreToolsBinDir());
		backSlashes(tempString);
		estrReplaceOccurrences(ppEString, "$CORETOOLSBIN$", tempString);
	}
	else
	{
		estrReplaceOccurrences(ppEString, "$SRC$", fileSrcDir());
		estrReplaceOccurrences(ppEString, "$DATA$", fileDataDir());
		estrReplaceOccurrences(ppEString, "$TOOLSBIN$", fileToolsBinDir());
		if (fileCoreSrcDir())
		{
			estrReplaceOccurrences(ppEString, "$CORESRC$", fileCoreSrcDir());
			estrReplaceOccurrences(ppEString, "$COREDATA$", fileCoreDataDir());
		}
		estrReplaceOccurrences(ppEString, "$CORETOOLSBIN$", fileCoreToolsBinDir());
	}
}







static struct _finddata32_t **sppNLargestFiles = NULL; 
static int siNumLargestToFind = 0;
static char *spExtensionForLargestFiles = NULL;

static FileScanAction FindLargestFilesCB(char* dir, struct _finddata32_t* data, void *pUserData)
{
	int i;
	int iCurNumFound = eaSize(&sppNLargestFiles);

	FileScanAction retval = FSA_EXPLORE_DIRECTORY;

	// Ignore all directories.
	if(data->attrib & _A_SUBDIR)
		return retval;

	if(!strEndsWith(data->name, spExtensionForLargestFiles))
		return retval;

	iCurNumFound = eaSize(&sppNLargestFiles);


	for (i=0; i < siNumLargestToFind; i++)
	{
		if (i >= iCurNumFound)
		{
			struct _finddata32_t *pNewData = calloc(sizeof(struct _finddata32_t), 1);
			memcpy(pNewData, data, sizeof(struct _finddata32_t));
			sprintf(pNewData->name, "%s/%s", dir, data->name);
			eaPush(&sppNLargestFiles, pNewData);
			return retval;
		}

		if (data->size > sppNLargestFiles[i]->size)
		{
			struct _finddata32_t *pNewData = calloc(sizeof(struct _finddata32_t), 1);
			memcpy(pNewData, data, sizeof(struct _finddata32_t));
			sprintf(pNewData->name, "%s/%s", dir, data->name);
			eaInsert(&sppNLargestFiles, pNewData, i);

			if (iCurNumFound == siNumLargestToFind)
			{
				free(sppNLargestFiles[siNumLargestToFind]);
				eaRemoveFast(&sppNLargestFiles, siNumLargestToFind);
			}

			return retval;
		}
	}

	return retval;
}



//creates an earray of malloced strings
void FindNLargestFilesInDirectory(char *pDirectoryNames, char *pExtension, int iNumToFind, char ***pppNames)
{
	int i;

	siNumLargestToFind = iNumToFind;
	spExtensionForLargestFiles = pExtension;

	fileScanAllDataDirs(pDirectoryNames, FindLargestFilesCB, NULL);

	for (i=0; i < eaSize(&sppNLargestFiles); i++)
	{
		eaPush(pppNames, strdup(sppNLargestFiles[i]->name));
	}

	eaDestroyEx(&sppNLargestFiles, NULL);
}


void TouchFile(char *pFileName)
{
#if !_PS3
	char localFileName[CRYPTIC_MAX_PATH];
	struct _utimbuf utb = {0};
	FWStatType stat;
	int iRetVal; 

		
	fileLocateWrite(pFileName, localFileName);

	fwStat(localFileName, &stat);
	utb.modtime = utb.actime = stat.st_mtime + 100;

	_chmod(localFileName, _S_IREAD | _S_IWRITE);
	iRetVal = _utime(localFileName, &utb);
	_chmod(localFileName, stat.st_mode);
#endif
}



TPFileList *TPFileList_ReadDirectory(const char *pRootDirectory)
{
	TPFileList *pList = StructCreate(parse_TPFileList);

	char rootDirToUse[CRYPTIC_MAX_PATH];
	char **ppFileNames;
	int iLen;
	int i;

	strcpy(rootDirToUse,pRootDirectory);
	forwardSlashes(rootDirToUse);
	iLen = (int)strlen(rootDirToUse);
	assert(iLen < CRYPTIC_MAX_PATH);
	while (iLen && rootDirToUse[iLen-1] == '/')
	{
		rootDirToUse[iLen-1] = 0;
		iLen--;
	}



	ppFileNames = fileScanDirFolders(rootDirToUse, FSF_FILES);

	for (i=0; i < eaSize(&ppFileNames); i++)
	{
		TPFileListEntry *pEntry = StructCreate(parse_TPFileListEntry);
		pEntry->pName = strdup(ppFileNames[i] + iLen + 1);
		pEntry->pData = TextParserBinaryBlock_CreateFromFile(ppFileNames[i], true);

		eaPush(&pList->ppFiles, pEntry);
	}




	fileScanDirFreeNames(ppFileNames);

	return pList;
}



TPFileList *TPFileList_ReadDirectory_OnlyIfPresentInPreexistingFileList(const char *pRootDirectory,
	TPFileList *pTemplateFileList)
{
	TPFileList *pList = StructCreate(parse_TPFileList);

	char rootDirToUse[CRYPTIC_MAX_PATH];
	char **ppFileNames;
	int iLen;
	int i;

	strcpy(rootDirToUse,pRootDirectory);
	forwardSlashes(rootDirToUse);
	iLen = (int)strlen(rootDirToUse);
	assert(iLen < CRYPTIC_MAX_PATH);
	while (iLen && rootDirToUse[iLen-1] == '/')
	{
		rootDirToUse[iLen-1] = 0;
		iLen--;
	}

	ppFileNames = fileScanDirFolders(rootDirToUse, FSF_FILES);

	for (i=0; i < eaSize(&ppFileNames); i++)
	{
		TPFileListEntry *pEntry = StructCreate(parse_TPFileListEntry);
		pEntry->pName = strdup(ppFileNames[i] + iLen + 1);

		if (TPFileList_FindByName(pTemplateFileList, pEntry->pName))
		{
			pEntry->pData = TextParserBinaryBlock_CreateFromFile(ppFileNames[i], true);
		}
		else
		{
			pEntry->iSizeOfNonLoadedFile = fileSize(ppFileNames[i]);
		}

		eaPush(&pList->ppFiles, pEntry);
	}




	fileScanDirFreeNames(ppFileNames);

	return pList;
}



void TPFileList_WriteDirectory(const char *pRootDirectory, TPFileList *pFileList)
{
	char rootDirToUse[CRYPTIC_MAX_PATH];
	int i, iLen;

	strcpy(rootDirToUse,pRootDirectory);
	forwardSlashes(rootDirToUse);
	iLen = (int)strlen(rootDirToUse);
	assert(iLen < CRYPTIC_MAX_PATH);
	while (iLen && rootDirToUse[iLen-1] == '/')
	{
		rootDirToUse[iLen-1] = 0;
		iLen--;
	}

	for (i=0; i < eaSize(&pFileList->ppFiles); i++)
	{
		char tempFileName[CRYPTIC_MAX_PATH];
		sprintf(tempFileName, "%s/%s", rootDirToUse, pFileList->ppFiles[i]->pName);

		mkdirtree_const(tempFileName);

		TextParserBinaryBlock_PutIntoFile(pFileList->ppFiles[i]->pData, tempFileName);
	}
}

bool TPFileList_CompareAndGenerateReport(TPFileList *pCurrentFiles, TPFileList *pNewFiles, char **ppReport, DontReportDeleteCB pDontDeleteCB)
{
	int iNumCurFiles = eaSize(&pCurrentFiles->ppFiles);
	int iNumNewFiles = eaSize(&pNewFiles->ppFiles);
	int i;
	int iStartingLen = estrLength(ppReport);

	for (i=0; i < iNumCurFiles; i++)
	{
		TPFileListEntry *pCurFile = pCurrentFiles->ppFiles[i];
		TPFileListEntry *pOther = TPFileList_FindByName(pNewFiles, pCurFile->pName);

		if (!pOther)
		{
			if (!pDontDeleteCB || !pDontDeleteCB(pCurFile->pName))
			{
				estrConcatf(ppReport, "TO BE DELETED: %s (%d bytes)\n", pCurFile->pName, 
					pCurFile->pData ? TextParserBinaryBlock_GetSize(pCurFile->pData) : pCurFile->iSizeOfNonLoadedFile);
			}
		}
		else
		{
			if (pCurFile->pData && pOther->pData)
			{
				if (StructCompare(parse_TextParserBinaryBlock, pCurFile->pData, pOther->pData, 0, 0, 0))
				{
					estrConcatf(ppReport, "FILES DIFFER: %s (current: %d bytes. New: %d bytes)\n", 
						pCurFile->pName, 
						pCurFile->pData ? TextParserBinaryBlock_GetSize(pCurFile->pData) : pCurFile->iSizeOfNonLoadedFile, 
						pOther->pData ? TextParserBinaryBlock_GetSize(pOther->pData) : pOther->iSizeOfNonLoadedFile);
				}
			}
			else
			{
				estrConcatf(ppReport, "FILES NOT BOTH PRESENT TO COMPARE: %s  (current: %d bytes. New: %d bytes)\n", 
						pCurFile->pName, 
						pCurFile->pData ? TextParserBinaryBlock_GetSize(pCurFile->pData) : pCurFile->iSizeOfNonLoadedFile, 
						pOther->pData ? TextParserBinaryBlock_GetSize(pOther->pData) : pOther->iSizeOfNonLoadedFile);
			}
		}
	}

	for (i=0; i < iNumNewFiles; i++)
	{
		TPFileListEntry *pCurFile = pNewFiles->ppFiles[i];
		TPFileListEntry *pOther = TPFileList_FindByName(pCurrentFiles, pCurFile->pName);

		if (!pOther)
		{
			estrConcatf(ppReport, "TO BE ADDED: %s (%d bytes)\n", pCurFile->pName, 
				pCurFile->pData ? TextParserBinaryBlock_GetSize(pCurFile->pData) : pCurFile->iSizeOfNonLoadedFile);
		}
	}

	if ((int)estrLength(ppReport) == iStartingLen)
	{
		estrConcatf(ppReport, "ALL FILES IDENTICAL\n");
		return false;
	}

	return true;
}

TPFileListEntry *TPFileList_FindByName(TPFileList *pFileList, char *pName)
{

	TPFileListEntry *pEntry = eaIndexedGetUsingString(&pFileList->ppFiles, pName);

	return pEntry;
}





FILE *x_fopen_with_retries(const char *fname, const char *how, int iNumRetries, int iSleepTime, const char *caller_fname, int lineNum)
{
	FILE *pFile = NULL;

	int i;

	for (i=0; i < iNumRetries; i++)
	{
		pFile = x_fopen(fname, how, caller_fname, lineNum);
		if (pFile)
		{
			return pFile;
		}

		Sleep(iSleepTime);
	}

	return NULL;
}


void InsertLineAtBeginningOfFileAndTrunc(const char *pFileName, const char *pLine, int iMaxLinesInFile)
{
	char *pCurContents = fileAlloc(pFileName, NULL);
	char *pEstrContents = NULL;
	FILE *pOutFile;

	if (!pCurContents)
	{
		pOutFile = fopen(pFileName, "wb");
		fprintf(pOutFile, "%s", pLine);
		fclose(pOutFile);
		return;
	}

	estrCopy2(&pEstrContents, pCurContents);
	free(pCurContents);

	if (iMaxLinesInFile)
	{
		estrTruncateAtNthOccurrence(&pEstrContents, '\n', iMaxLinesInFile);
	}

	pOutFile = fopen(pFileName, "wb");
	fprintf(pOutFile, "%s%s", pLine, strEndsWith(pLine, "\n") ? "" : "\n");
	fprintf(pOutFile, "%s", pEstrContents);

	estrDestroy(&pEstrContents);

	fclose(pOutFile);
}



#define WBZ_BUF_SIZE (16 * 1024)

void *fileAllocWBZ_dbg(const char *pFileName, int *pLen MEM_DBG_PARMS)
{
	void **ppBufs = NULL;
	int iTotalSize = 0;

	FILE *pFile = fopen(pFileName, "rbz");
	char *pOutBuf;
	int iBytesActuallyRead;
	
	int iBytesWritten = 0;

	if (pLen)
	{
		*pLen = 0;
	}
	
	if (!pFile)
	{
		return NULL;
	}

	do
	{
		char *pCurBuf = malloc(WBZ_BUF_SIZE);
		iBytesActuallyRead = (int)fread(pCurBuf, 1, WBZ_BUF_SIZE, pFile);
				
		iTotalSize += iBytesActuallyRead;

		eaPush(&ppBufs, pCurBuf);
	} 
	while (iBytesActuallyRead == WBZ_BUF_SIZE);

	fclose(pFile);

	if (!iTotalSize)
	{
		eaDestroyEx(&ppBufs, NULL);
		return NULL;
	}


	pOutBuf = smalloc(iTotalSize);
	if (pLen)
	{
		*pLen = iTotalSize;
	}

	while (iBytesWritten < iTotalSize)
	{
		int iBytesToCopy = MIN(iTotalSize - iBytesWritten, WBZ_BUF_SIZE);
		memcpy(pOutBuf + iBytesWritten, ppBufs[0], iBytesToCopy);
		free(eaRemove(&ppBufs, 0));
		iBytesWritten += iBytesToCopy;
	}

	eaDestroyEx(&ppBufs, NULL);
	return pOutBuf;
}


static StashTable sJunctionNamesFromFolderNames = NULL;

#define GOOD_JUNCTION_STRING "Substitute Name:"

char *GetJunctionName_Internal(char *pFolderName)
{
	char *pSystemString = NULL;
	char *pOutString = NULL;
	int iRetVal;
	char *pFoundGoodString;
	char *pRetVal;

	estrCopy2(&pSystemString, pFolderName);
	backSlashes(pSystemString);
	estrTrimLeadingAndTrailingWhitespace(&pSystemString);
	if (!estrLength(&pSystemString))
	{
		estrDestroy(&pSystemString);
		return NULL;
	}

	while (pSystemString[estrLength(&pSystemString)-1] == '\\')
	{
		estrRemove(&pSystemString, estrLength(&pSystemString)-1, 1);
	}

	estrInsertf(&pSystemString, 0, "junction -accepteula ");

	iRetVal = system_w_output_and_timeout(pSystemString, &pOutString, 10);
	estrDestroy(&pSystemString);

	if (iRetVal)
	{
		AssertOrAlert("JUNCTION_FAIL", "Couldn't get junction information for %s", pFolderName);
		estrDestroy(&pOutString);
		return NULL;
	}

	if ((pFoundGoodString = strstri_safe(pOutString,GOOD_JUNCTION_STRING)))
	{
		estrRemove(&pOutString, 0, pFoundGoodString - pOutString + (int)(strlen(GOOD_JUNCTION_STRING)));
		estrTrimLeadingAndTrailingWhitespace(&pOutString);
		pRetVal = strdup(pOutString);
		estrDestroy(&pOutString);
		return pRetVal;
	}
	else
	{
		return NULL;
	}
}

void AssembleSegments(char ***pppSegments, int iStartingIndex, int iCount, char **ppOutString)
{
	int i;

	for (i = 0; i < iCount; i++)
	{
		estrConcatf(ppOutString, "%s%s", i == 0 ? "" : "\\", (*pppSegments)[iStartingIndex + i]);
	}
}

void GetPathSegments(char *pFolderName, char ***pppSegments)
{
	DivideString(pFolderName, "\\/", pppSegments, 
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	//presumably the first two are now c: and src or something, and we want the first one to be c:\src, so recombine
	if (eaSize(pppSegments) > 1 && (*pppSegments)[0][1] == ':')
	{
		char temp[CRYPTIC_MAX_PATH];
		sprintf(temp, "%s\\%s", (*pppSegments)[0], (*pppSegments)[1]);
		free(eaRemove(pppSegments, 0));
		free(eaRemove(pppSegments, 0));
		eaInsert(pppSegments, strdup(temp), 0);
	}

}

AUTO_COMMAND;
const char *GetJunctionNameFromFolderName(char *pFolderName)
{
	char **ppSegments = NULL;
	int i;
	char *pRetVal_Internal;
	char *pRetVal = NULL;
	const char *pRetVal_Alloced = NULL;		
	char *pCurPath = NULL;

	if (pFolderName[1] != ':')
	{
		static char *spRetVal_NotAPath = NULL;
		estrCopy2(&spRetVal_NotAPath, pFolderName);
		return spRetVal_NotAPath;
	}

	if (stashFindPointer(sJunctionNamesFromFolderNames, pFolderName, &pRetVal))
	{
		return pRetVal;
	}

	GetPathSegments(pFolderName, &ppSegments);

	for (i = eaSize(&ppSegments); i >= 1; i--)
	{
		estrClear(&pCurPath);
		AssembleSegments(&ppSegments, 0, i, &pCurPath);

		if ((pRetVal_Internal = GetJunctionName_Internal(pCurPath)))
		{
			estrClear(&pRetVal);
			estrCopy2(&pRetVal, pRetVal_Internal);
			if (i < eaSize(&ppSegments))
			{
				estrConcatf(&pRetVal, "\\");
				AssembleSegments(&ppSegments, i, eaSize(&ppSegments) - i, &pRetVal);
			}
				
			pRetVal_Alloced = allocAddString(pRetVal);
				
			goto done;
			
		}
	}

done:
	if (!pRetVal_Alloced)
	{
		pRetVal_Alloced = allocAddString(pFolderName);
	}

	if (!sJunctionNamesFromFolderNames)
	{
		sJunctionNamesFromFolderNames = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);			
	}
				
	stashAddPointer(sJunctionNamesFromFolderNames, pFolderName, pRetVal_Alloced, true);

	eaDestroyEx(&ppSegments, NULL);
	estrDestroy(&pCurPath);
	estrDestroy(&pRetVal);

	return pRetVal_Alloced;
}






/*

//only works for "c:\foo", not "c:\foo\bar". Use non _Root version for that
static const char *GetJunctionNameFromFolderName_Root(char *pFolderName)
{
	const char *pRetVal = NULL;
	char *pSystemString = NULL;
	int iRetVal;
	char *pOutString = NULL;
	char *pFoundGoodString;

	if (!pFolderName)
	{
		return "";
	}

	if (!sJunctionNamesFromFolderNames)
	{
		sJunctionNamesFromFolderNames = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	}

	if (stashFindPointer(sJunctionNamesFromFolderNames, pFolderName, &((char*)pRetVal)))
	{

		return pRetVal;
	}

	estrCopy2(&pSystemString, pFolderName);
	backSlashes(pSystemString);
	estrTrimLeadingAndTrailingWhitespace(&pSystemString);
	if (!estrLength(&pSystemString))
	{
		estrDestroy(&pSystemString);
		return "";
	}

	while (pSystemString[estrLength(&pSystemString)-1] == '\\')
	{
		estrRemove(&pSystemString, estrLength(&pSystemString)-1, 1);
	}

	estrInsertf(&pSystemString, 0, "junction -accepteula ");

	iRetVal = system_w_output_and_timeout(pSystemString, &pOutString, 10);
	estrDestroy(&pSystemString);

	if (iRetVal)
	{
		AssertOrAlert("JUNCTION_FAIL", "Couldn't get junction information for %s", pFolderName);
		estrDestroy(&pOutString);
	}

	if ((pFoundGoodString = strstri_safe(pOutString,GOOD_JUNCTION_STRING)))
	{
		estrRemove(&pOutString, 0, pFoundGoodString - pOutString + (int)(strlen(GOOD_JUNCTION_STRING)));
		estrTrimLeadingAndTrailingWhitespace(&pOutString);
		pRetVal = strdup(pOutString);
		stashAddPointer(sJunctionNamesFromFolderNames, pFolderName, pRetVal, false);
		estrDestroy(&pOutString);
		return pRetVal;
	}
	else
	{
		pRetVal = strdup(pFolderName);
		stashAddPointer(sJunctionNamesFromFolderNames, pFolderName, pRetVal, false);
		estrDestroy(&pOutString);
		return pRetVal;
	}
}

void DividePathIntoRootAndSubDirs(char *pInPath, char **ppOutDriveAndRoot, char **ppOutSubDirs)
{
	int inLen = (int)strlen(pInPath);
	char *pFirstBackSlash;
	char *pFirstForwardSlash;

	if (inLen <= 3)
	{
		estrCopy2(ppOutDriveAndRoot, pInPath);
		return;
	}

	pFirstBackSlash = strchr(pInPath + 3, '\\');
	pFirstForwardSlash = strchr(pInPath + 3, '//');

	if (!pFirstBackSlash && !pFirstForwardSlash)
	{
		estrCopy2(ppOutDriveAndRoot, pInPath);
		return;
	}

	if (!pFirstBackSlash || pFirstForwardSlash && pFirstForwardSlash < pFirstBackSlash)
	{
		pFirstBackSlash = pFirstForwardSlash;
	}

	estrSetSize(ppOutDriveAndRoot, pFirstBackSlash - pInPath);
	memcpy(*ppOutDriveAndRoot, pInPath, pFirstBackSlash - pInPath);
	estrCopy2(ppOutSubDirs, pFirstBackSlash + 1);
}

AUTO_COMMAND;
const char *GetJunctionNameFromFolderName(char *pFolderName)
{
	char *pDriveAndRootDir = NULL;
	char *pSubDirs = NULL;
	static char *spRetVal = NULL;

	char *pTemp = NULL;

	if (pFolderName[1] != ':')
	{
		estrCopy2(&spRetVal, pFolderName);
		return spRetVal;
	}

	estrStackCreate(&pTemp);
	estrStackCreate(&pDriveAndRootDir);
	estrStackCreate(&pSubDirs);

	estrCopy2(&pTemp, pFolderName);
	backSlashes(pTemp);
	estrReplaceOccurrences(&pTemp, "\\\\", "\\");

	DividePathIntoRootAndSubDirs(pTemp, &pDriveAndRootDir, &pSubDirs);
	estrDestroy(&pTemp);

	estrCopy2(&spRetVal, GetJunctionNameFromFolderName_Root(pDriveAndRootDir));

	if (estrLength(&pSubDirs))
	{
		estrConcatf(&spRetVal, "\\%s", pSubDirs);
	}

	estrDestroy(&pDriveAndRootDir);
	estrDestroy(&pSubDirs);

	return spRetVal;
}*/

#include "FileUtil2_h_ast.c"

