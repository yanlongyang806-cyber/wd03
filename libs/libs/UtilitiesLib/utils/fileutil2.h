#pragma once
GCC_SYSTEM
//////////////////////////////////////////////////////////////////////////
// This file should only be included by external, non-integrated tools
// (e.g. Gimme, GetVRML, etc) as its functionality does not work in
// conjunction with the FolderCache, FileWatcher, etc.
//////////////////////////////////////////////////////////////////////////


#include "file.h"

typedef struct TextParserBinaryBlock TextParserBinaryBlock;

typedef enum FileScanFolders {
	FSF_FILES=0x01,
	FSF_FOLDERS=0x02,
	FSF_FILES_AND_FOLDERS=0x03, // = FSF_FILES | FSF_FOLDERS
	FSF_UNDERSCORED=0x04, // 0x04 include files beginning with an underscore (or this with the above choices)
	FSF_NOHIDDEN=0x08, // Excludes hidden and system files
	FSF_RETURNSHORTNAMES=0x010, //return filenames without paths or extensions
	FSF_RETURNLOCALNAMES=0x020, //return filenames without paths, but with extensions
} FileScanFolders;

typedef void (*FileScanFeedbackFunc)(int iNumFiles, int iNumFolders);


char **fileScanDir(const char *dir);
char **fileScanDirNoSubdirRecurse(const char *dir); // doesnt recurse into subdirectories
char **fileScanDirFolders(const char *dir, FileScanFolders folders);
char **fileScanDirFoldersNoSubdirRecurse(const char *dir, FileScanFolders folders);
char **fileScanDirRecurseNLevels(const char *dir, int iNumLevels); //if iNumLevels == 1, scans the root and its children, but no grandchildren, etc
char **fileScanDirFoldersWithFeedback(const char *dir, FileScanFolders folders, FileScanFeedbackFunc pCallback);


void fileScanDirFreeNames(char **names);
void fileScanDirRecurseEx(const char* dir, FileScanProcessor processor, void *pUserData);
void fileScanDirRecurseContext(const char* dir, FileScanContext* context);
FileScanAction printAllFileNames(char* dir, struct _finddata32_t* data); // Sample FileScanProcessor

// Do NOT call these functions directly!
// Instead, call fileLocateWrite (if you need a path on disk to pass to a Win32 function or error dialog)
//   or fileRelativePath (if you need a game path)
const char *fileDataDir(void);
const char *fileCoreDataDir(void);

//deletes all files recursively inside a directory older than a certain date, and
//removes all empty subdirectories
int PurgeDirectoryOfOldFiles_Secs(const char *pDir, int iMinSecsOld, char *pStringToMatch, bool bDontRemoveDirectories);
#define PurgeDirectoryOfOldFiles(pDir, iMinDaysOld, pStringToMatch) PurgeDirectoryOfOldFiles_Secs((pDir), (iMinDaysOld) * 24 * 60 * 60, pStringToMatch, false)
#define PurgeDirectoryOfOldFiles_LeaveDirs(pDir, iMinDaysOld, pStringToMatch) PurgeDirectoryOfOldFiles_Secs((pDir), (iMinDaysOld) * 24 * 60 * 60, pStringToMatch, true)

//fancy version which applies different rules to different files. CB uses this so it doesn't have to make multiple passes
AUTO_STRUCT;
typedef struct PurgeDirectoryCriterion
{
	char *pNameToMatch; //NULL if this is the default rule
	int iSecondsOld;
} PurgeDirectoryCriterion;

int PurgeDirectoryOfOldFiles_MultipleCriteria(const char *pDir, PurgeDirectoryCriterion ***pppCriteria);


//uses same logic as above. Useful to see if purging is necessary
U64 GetSizeOfOldFiles_Secs(const char *pDir, int iMinSecsOld, char *pStringToMatch);
#define GetSizeOfOldFiles(pDir, iMinDaysOld, pStringToMatch) GetSizeOfOldFiles_Secs((pDir), (iMinDaysOld) * 24 * 60 * 60, pStringToMatch)

void ReplaceStrings(char *pWorkString, int iWorkStringBufferSize, const char *pLookFor, const char *pReplaceWith, bool bBackslashes);

//replaces any or all of $SRC$, $DATA$, $TOOLSBIN$, $CORESRC$, $COREDATA$, $CORETOOLSBIN$ in the 
//work string with the current correct values, optionally with backslashed versions
void ApplyDirectoryMacros(char workString[1024], bool bBackslashes);
void ApplyDirectoryMacrosToEString(char **ppEString, bool bBackslashes);


//creates an earray of malloced strings
void FindNLargestFilesInDirectory(char *pDirectoryNames, char *pExtension, int iNumToFind, char ***pppNames);


void TouchFile(char *pFileName);





//a file list that can be sent with TextParser
AUTO_STRUCT;
typedef struct TPFileListEntry
{
	char *pName; AST(KEY) //relative to root directory, forward slashes. ie "server/foo.txt"
	TextParserBinaryBlock *pData;
	int iSizeOfNonLoadedFile; //we don't load every file if we are only creating the fileList to compare to
		//a preexisting one, in which case we set the size here and leave pData NULL
} TPFileListEntry;

AUTO_STRUCT; 
typedef struct TPFileList
{
	TPFileListEntry **ppFiles;
} TPFileList;

TPFileList *TPFileList_ReadDirectory(const char *pRootDirectory);

//go through the directory on disk, look at all the files. If the file is not in the pTemplateDirectory,
//then just calcualte its size, but don't bother loading it. (This is useful, for instance, when the controller
//sends a (presumably very short) list of files in the data dir to the launcher, and there's no point in loading into
//RAM all the other files that we're going to delete immediately, but we want to know about them
TPFileList *TPFileList_ReadDirectory_OnlyIfPresentInPreexistingFileList(const char *pRootDirectory,
	TPFileList *pTemplateFileList);

extern ParseTable parse_TPFileList[];
#define TYPE_parse_TPFileList TPFileList

//Does NOT erase the directory or its contents. If you want a perfect copy, you must do so.
void TPFileList_WriteDirectory(const char *pRootDirectory, TPFileList *pFileList);

//generates a short text report, assuming that one set of directory contents is about to be fully replaced by another
//returns true if there's at least one difference
//
//if DontReportOnDeleteCB is set, call it on each file that will be deleted and if it returns true, don't
//report that file will be deleted
typedef bool (*DontReportDeleteCB)(const char *pFileName);


bool TPFileList_CompareAndGenerateReport(TPFileList *pCurrentFiles, TPFileList *pNewFiles, char **ppReport, DontReportDeleteCB pDontDeleteCB);

TPFileListEntry *TPFileList_FindByName(TPFileList *pFileList, char *pName);

FILE *x_fopen_with_retries(const char *fname, const char *how, int iNumRetries, int iSleepTime, const char *caller_fname, int lineNum);
#define fopen_with_retries(fname, how, iNumRetries, iSleepTime) x_fopen_with_retries(fname, how, iNumRetries, iSleepTime, __FILE__, __LINE__)


//insert a line of text at the beginning of a file. Also truncate the file to no more than a certain number of lines. Useful
//for simple logging
void InsertLineAtBeginningOfFileAndTrunc(const char *pFileName, const char *pLine, int iMaxLinesInFile);

bool CountFilesInDir(char *pDirName);

const char *GetJunctionNameFromFolderName(char *pFolderName);

void ReplaceStrings(char *pWorkString, int iWorkStringBufferSize, const char *pLookFor, const char *pReplaceWith, bool bBackslashes);