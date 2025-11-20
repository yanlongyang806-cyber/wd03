#include <stdio.h>
#include "EArray.h"

#include "utilitiesLib.h"
#include "GlobalTypes.h"
#include "Organization.h"

#if !_PS3

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <share.h>
#include <io.h>
#include <sys/types.h>
#include <time.h>
#include <direct.h>
#include <errno.h>

#include <fcntl.h>
#include "wininclude.h"
#include <process.h>

#ifndef _XBOX
	#include <ShlObj.h>
	#include "psapi.h"
#endif

#endif

#include "fileWatch.h"
#include "fileutil.h"
#include "EString.h"
#include "StashTable.h"
#include "net/net.h"
#include "sysutil.h"
#include "memlog.h"
#include "strings_opt.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "stringcache.h"
#include "stringutil.h"
#include "ThreadManager.h"
#include "winutil.h"
#include "qsortG.h"
#include "stringCache.h"
#include "utils_c_ast.h"
#include "endian.h"
#include "mathutil.h"
#include "crypt.h"
#include "systemspecs.h"
#include "mutex.h"
#include "AppRegCache.h"
#include "utils_h_ast.h"
#include "TimedCallback.h"
#include "alerts.h"
#include "timing.h"
#include "httpxpathsupport.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unsorted););

char *loadCmdline(char *cmdFname,char *buf,int bufsize)
{
	FILE	*file;
	int		len;

	memset(buf,0,bufsize);
	file = fopen(cmdFname,"rt");
	if (file)
	{
		fgets(buf, bufsize-1, file);
		fclose(file);
		len = (int)strlen(buf);
		if (len && buf[len-1] == '\n')
			len--;
		buf[len] = ' ';
		buf[len+1] = 0;
	}
	return buf;
}

//this seems like not the most efficient way to do things, but it's just for
//cmdline loading, so who cares?
void loadCmdLineIntoEString(char *cmdFname, char **ppEString)
{
	FILE	*file;
	char c;

	file = fopen(cmdFname,"rt");
	
	if (!file)
	{
		return;
	}

	while (1)
	{
		size_t iBytesRead = fread(&c, 1, 1, file);
		if (!iBytesRead)
		{
			break;
		}

		estrConcatChar(ppEString, c);
	}

	fclose(file);

	estrTruncateAtFirstOccurrence(ppEString, '\n');
	estrConcatChar(ppEString, ' ');
}


void makefullpath_s(const char *dir,char *full,size_t full_size)
{
	char	base[MAX_PATH];

	if (strchr(dir, ':') || strstr(dir, "\\\\") || strstr(dir, "//")) { // Can't use fileIsAbsolutePath() because utilities use this
		strcpy_s(SAFESTR2(full), dir);
	} else {
		fileGetcwd(base, MAX_PATH );
		if (strStartsWith(dir, "./") || strStartsWith(dir, ".\\"))
			dir+=2;
		concatpath_s(base,dir,full,full_size);
	}
	forwardSlashes(full);
}

#if _PS3

#define mkdir(path) mkdir(path, S_IRWXU|S_IRWXG|S_IRWXO)

#elif _XBOX
int mkdir( const char * path )
{
	int nRetCode;

	nRetCode = 0;
	if ( !CreateDirectory( path, NULL ) )
	{
		switch ( GetLastError() )
		{
			xcase ERROR_ALREADY_EXISTS:
				nRetCode = EEXIST;

			xcase ERROR_PATH_NOT_FOUND:
			xcase ERROR_CANNOT_MAKE:
				nRetCode = ENOENT;

			xcase ERROR_ACCESS_DENIED:
				nRetCode = EACCES;
				break;

			default:
				nRetCode = EACCES;
				break;
		}
	}
	return nRetCode;
}
#endif

// Given a path to a file, this function makes sure that all directories
// specified in the path exists.
void mkdirtree(char *path)
{
	char	*s;
#if _XBOX
	backSlashes(path);
#else
	forwardSlashes(path);
#endif

	s = path;
	for(;;)
	{
#if _XBOX
		s = strchr(s,'\\');
#else
		s = strchr(s,'/');
#endif
		if (!s)
			break;
		*s = 0;
		mkdir(path);
#if _XBOX
		*s = '\\';
#else
		*s = '/';
#endif
		s++;
	}
}

void mkdirtree_const(const char *path)
{
	char temp[CRYPTIC_MAX_PATH];
	strcpy(temp, path);
	mkdirtree(temp);
}

void rmdirtree(const char *path)
{
	char	buf[1000],*s;
	struct _finddata32i64_t finddata;
	intptr_t hfile;

	strcpy(buf,path);
	forwardSlashes(buf);
	for(;;)
	{
		int empty=1;
		s = strrchr(buf,'/');
		if (!s || strlen(s)<=3)
			break;
		*s = 0;
		// Verify the directory is empty (it won't be if this is a mount point
		strcat(buf, "/*.*");
		hfile = findfirst32i64_SAFE(buf, &finddata);
		*strrchr(buf, '/')=0;
		if (hfile!=-1L) {
			do {
				if (!(strcmp(finddata.name, ".")==0||strcmp(finddata.name, "..")==0)) {
					empty=0;
					break;
				}
			} while( findnext32i64_SAFE( hfile, &finddata) == 0 );
			_findclose(hfile);
		}
		if (empty) {
			if (-1 == _rmdir(buf) &&
				EACCES == errno)
			{
				// Maybe FileWatcher or someone else had it, release it, try again, a couple times
				Sleep(0);
				if (-1 == _rmdir(buf) &&
					EACCES == errno)
				{
					Sleep(1);
					if (-1 == _rmdir(buf) &&
						EACCES == errno)
					{
						Sleep(10);
						_rmdir(buf);
						// If it didn't succeed here, leave it be!
					}
				}
			}
		} else {
			break;
		}
	}
}

/* Function rmdirtreeExInternal()
 *	The worker function of rmdirtreeExInternal().  This function recursively
 *	deletes contents of the given path.
 */
static void rmdirtreeExInternal(const char* path, int forceRemove)
{
	FWStatType status;

	// If the specified path doesn't exist, do nothing.
	if(fwStat(path, &status))
	{
		return;
	}

	if(forceRemove)
	{
		_chmod(path, _S_IREAD | _S_IWRITE);
	}
	
	// If the path is a directory, enumerate all items in the directory.
	// Recursively process all items in the directory.
	if(status.st_mode & _S_IFDIR)
	{
		char buffer[CRYPTIC_MAX_PATH];
		intptr_t handle;
		struct _finddata32i64_t finddata;


		concatpath(path, "*.*", buffer);

		handle = findfirst32i64_SAFE(buffer, &finddata);
		if(handle != -1) 
		{
			do
			{
				if (strcmp(finddata.name, ".")==0 || strcmp(finddata.name, "..")==0)
					continue;
				concatpath(path, finddata.name, buffer);
				rmdirtreeExInternal(buffer, forceRemove);
			} while(findnext32i64_SAFE(handle, &finddata) == 0);
			_findclose(handle);
		}

		_rmdir(path);
	}
	// If the specified path is a file, remove it.
	else
	{
		remove(path);
	}
}

#if !PLATFORM_CONSOLE

/* Function rmdirtreeEx()
 *	Recursively delete all directories and files starting at the specified path.
 *	This function will not perform a recursive delete if the operation will cause
 *	important windows system directories to be deleted.	
 */
unsigned int systemDirectories[] = 
{
	CSIDL_WINDOWS,
	CSIDL_SYSTEM,
	CSIDL_PROGRAM_FILES,
};
void rmdirtreeEx(const char * path, int forceRemove)
{
	char *pSystemPath = NULL;
	int i;

	// Check the path against important system directories.
	// Do not delete anything in those directories or anything that will
	// result in the deletion of those directories.
	for(i = 0; i < ARRAY_SIZE(systemDirectories); i++)
	{
		if(SHGetSpecialFolderPath_UTF8(NULL, &pSystemPath, systemDirectories[i], 0))
		{
			if(filePathBeginsWith(pSystemPath, path) == 0)
			{
				estrDestroy(&pSystemPath);
				return;
			}
		}
	}

	rmdirtreeExInternal(path, forceRemove);
	estrDestroy(&pSystemPath);
}

#endif 

int isFullPath(const char *dir)
{
	return (fileIsAbsolutePath(dir) && (dir[0] != '.'));
/*
	if (strncmp(dir,"//",2)==0 || strncmp(&(dir[1]),":/",2)==0 || strncmp(&(dir[1]),":\\",2)==0)
		return 1;
	return 0;
*/
}

// Given a path to a directory, this function makes sure that all directories
// specified in the path exists.
int makeDirectories(const char* dirPath)
{
	char	buffer[CRYPTIC_MAX_PATH];
	char	*s;
	int error;

	if(!dirPath || !dirPath[0])
		return 1;

	Strncpyt(buffer, dirPath);

	s = buffer;

#ifdef _XBOX
#define DIR_DIVIDER '\\'
#define ERROR_VAL error
	backSlashes(buffer);
#else
#define DIR_DIVIDER '/'
#define ERROR_VAL errno
	forwardSlashes(buffer);
#endif

	// Look for the first slash that seperates a drive letter from the rest of the
	// path.
	if (isFullPath(buffer))
	{
		char *colon = strchr(s, ':');
		if(colon)
		{
			ANALYSIS_ASSUME(colon);
			if (colon[1] == DIR_DIVIDER && strlen(colon) > 2)
			{
				s = colon + 2;
			}
		}
	}

	for(;;)
	{
		// Locate the next slash.
		s = strchr(s,DIR_DIVIDER);
		if (!s){
			error = mkdir(buffer);
			if(error && EEXIST != ERROR_VAL)
				return 0;
			else
				return 1;
		}

		*s = 0;

		// Try to make the directory.  If the operation didn't succeed and the
		// directory doesn't already exist, the required directory still doesn't
		// exist.  Return an error code.
		if(error = mkdir(buffer))
		{
			if(EEXIST != ERROR_VAL && ENOENT != ERROR_VAL)
				return 0;
		}

		// Otherwise, restore the string and continue processing it.
		*s = DIR_DIVIDER;
		s++;
	}
#undef DIR_DIVIDER
#undef ERROR_VAL
}

// Makes all the directories for given file path.

int makeDirectoriesForFile(const char* filePath)
{
	char buffer[CRYPTIC_MAX_PATH];
	
	if (fileIsAbsolutePath(filePath))
		strcpy(buffer, filePath);
	else
		fileLocateWrite(filePath, buffer);
	
	return makeDirectories(getDirectoryName(buffer));
}

void openURL(SA_PARAM_NN_STR const char *url)
{
#if _PS3
    //YVS
#elif _XBOX
#else
	ulShellExecute(NULL, "open", url, NULL, NULL, SW_SHOW);
#endif
}

void openCrypticWikiPage(SA_PARAM_NN_STR const char *page)
{
	char *url = STACK_SPRINTF("http://" ORGANIZATION_DOMAIN "/display/%s", page);
	openURL(url);
}

#if _PS3
int system_detach(const char *cmd, int minimized, int hidden)
{
    assert(0);
    return 0;
}

int system_poke(int pid)
{
	//Needs to be implemented.
	return 0;
}

int system_pokename(int pid, const char *name)
{
	return 0;
}

#elif defined(_WIN32) && !_XBOX
static int system_internal(char* cmd, int minimized, int hidden, int wait)
{
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	si.cb = sizeof(si);
	if(minimized || hidden)
	{
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = (hidden ? SW_HIDE : SW_MINIMIZE);
	}

	//printf("system_detach('%s');\n", cmd);

	if (!CreateProcess_UTF8(NULL, cmd,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		FALSE, // do NOT let this child inhery handles
		CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		NULL, // inherit current directory
		&si,
		&pi))
	{

		// This is sorta a hack, because the dbserver says "run ./mapserver.exe", and
		//  spawnvp will end up finding it in the path if it's not in the current directory
		//  so this functionality is duplicated here.
		if (cmd[0]=='.' && (cmd[1]=='/' ||cmd[1]=='\\')) {
			return system_internal(cmd+2, minimized, hidden, wait);
		}

	


		//printf("Error creating process '%s'\n", cmd);
		return 0;
	} else {
		int pid = (int)pi.dwProcessId;
		if(wait){
			WaitForSingleObject(pi.hProcess, INFINITE);
		}
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return pid;
	}
}

static int system_internal_wrapper(const char* cmd, int minimized, int hidden, int wait)
{
	char bufferStatic[1000];
	char* buffer;
	size_t len = strlen(cmd) + 1;
	S32 ret;
	
	if(len <= sizeof(bufferStatic)){
		buffer = bufferStatic;
	}else{
		buffer = malloc(len);
	}
	
	strcpy_s(buffer, len, cmd);
	
	ret = system_internal(buffer, minimized, hidden, wait);
	
	if(buffer != bufferStatic){
		SAFE_FREE(buffer);
	}
	
	return ret;
}

int system_detach(const char* cmd, int minimized, int hidden)
{
	int ret;
	PERFINFO_AUTO_START_FUNC();
	ret = system_internal_wrapper(cmd, minimized, hidden, 0);
	PERFINFO_AUTO_STOP();
	return ret;
}

void fixupExeNameForFullDebugFixup(char **ppName)
{
	char *pFDName = NULL;
	U32 iTime;
	U32 iFDTime;

	estrStackCreate(&pFDName);

	if (!strEndsWith(*ppName, ".exe"))
	{
		estrConcatf(ppName, ".exe");
	}

	estrCopy(&pFDName, ppName);

	estrInsert(&pFDName, estrLength(&pFDName) - 4, "FD", 2);

	if (!fileExists(pFDName))
	{
		estrDestroy(&pFDName);
		return;
	}

	iTime = fileLastChangedSS2000(*ppName);
	iFDTime = fileLastChangedSS2000(pFDName);
	
	//this automatically handles the case where the FD files exists and the non-FD one doesn't
	if (iFDTime > iTime)
	{
		estrCopy(ppName, &pFDName);
	}

	estrDestroy(&pFDName);
}

void fixupEntireCommandStringForFullDebugFixup(char **ppCmd)
{
	char *pLocalCopy = NULL;
	estrStackCreate(&pLocalCopy);
	estrCopy(&pLocalCopy, ppCmd);
	estrTrimLeadingAndTrailingWhitespace(&pLocalCopy);

	//frequently, the executable is quoted
	if (pLocalCopy[0] == '"')
	{
		char *pSecondQuote = strchr(pLocalCopy + 1, '"');
		if (pSecondQuote && pSecondQuote - pLocalCopy > 1)
		{
			char *pExeName = NULL;
			estrStackCreate(&pExeName);
			estrSetSize(&pExeName, pSecondQuote - pLocalCopy - 1);
			memcpy(pExeName, pLocalCopy + 1, pSecondQuote - pLocalCopy - 1);
			pExeName[pSecondQuote - pLocalCopy - 1] = 0;

			estrRemove(&pLocalCopy, 1, pSecondQuote - pLocalCopy - 1);

			fixupExeNameForFullDebugFixup(&pExeName);

			estrInsert(&pLocalCopy, 1, pExeName, estrLength(&pExeName));

			estrDestroy(&pExeName);
		}
		else
		{
			AssertOrAlert("BAD_STR_FOR_DETACH_W_FD_FIXUP", "fixupEntireCommandStringForFullDebugFixup called with input string <<%s>>, which is improperly formatted", *ppCmd);
		}
	}
	else //otherwise we assume the executable name ends at the first space
	{
		char *pFirstSpace = strchr(pLocalCopy + 1, ' ');
		if (pFirstSpace)
		{	
			char *pExeName = NULL;
			estrStackCreate(&pExeName);
			estrSetSize(&pExeName, pFirstSpace - pLocalCopy);
			memcpy(pExeName, pLocalCopy, pFirstSpace - pLocalCopy);
			pExeName[pFirstSpace - pLocalCopy] = 0;
			estrRemove(&pLocalCopy, 0, pFirstSpace - pLocalCopy);

			fixupExeNameForFullDebugFixup(&pExeName);

			estrInsert(&pLocalCopy, 0, pExeName, estrLength(&pExeName));

			estrDestroy(&pExeName);
		}
		else
		{
			//no space means that we have just an exe name, that's fine
			fixupExeNameForFullDebugFixup(&pLocalCopy);
		}
	}

	estrCopy(ppCmd, &pLocalCopy);
	estrDestroy(&pLocalCopy);
}

static bool sbDoFullDebugFixupInProdMode = false;
AUTO_CMD_INT(sbDoFullDebugFixupInProdMode, DoFullDebugFixupInProdMode);

int system_detach_with_fulldebug_fixup(const char* cmd, int minimized, int hidden)
{
	int ret;
	char *pLocalCopy = NULL;

	if (isProductionMode() && !sbDoFullDebugFixupInProdMode)
	{
		return system_detach(cmd, minimized, hidden);
	}

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pLocalCopy);
	estrCopy2(&pLocalCopy, cmd);
	fixupEntireCommandStringForFullDebugFixup(&pLocalCopy);

	ret = system_internal_wrapper(pLocalCopy, minimized, hidden, 0);

	estrDestroy(&pLocalCopy);
	PERFINFO_AUTO_STOP();
	return ret;
}


int system_wait(const char* cmd, int minimized, int hidden)
{
	int ret;
	PERFINFO_AUTO_START_FUNC();
	ret = system_internal_wrapper(cmd, minimized, hidden, 1);
	PERFINFO_AUTO_STOP();
	return ret;
}

int system_poke(int pid)
{
	HANDLE hProc;
	DWORD dwPriorityClass = 0;
	hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (hProc == NULL) return 0;
	
	dwPriorityClass = GetPriorityClass( hProc );
	CloseHandle( hProc );

    if( !dwPriorityClass ) return 0;
	else return 1;
}

int system_pokename(int pid, const char *name)
{
	HANDLE hProc;
	DWORD dwPriorityClass = 0;
	bool matchname = true;
	char *spExeFilePath = NULL;

	hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (hProc == NULL) return 0;

	if (name && name[0])
	{
		DWORD			dwSize2;
		HMODULE			hMod[1000];
		if(!EnumProcessModules(hProc, hMod, sizeof( hMod ), &dwSize2 ) ) {
			matchname = false;
		}
		else
		{
			char *exe_name = "total", *s;
			if (0==GetModuleFileNameEx_UTF8( hProc, hMod[0], &spExeFilePath )) {
				estrCopy2(&spExeFilePath, "");
			}
			exe_name = strrchr(spExeFilePath,'\\');
			if (!exe_name++)
				exe_name = spExeFilePath;
			if (s = strrchr(exe_name, '.'))
				*s = 0;

			matchname = (stricmp(exe_name, name) == 0);

			
		}
	}

	estrDestroy(&spExeFilePath);

	dwPriorityClass = GetPriorityClass( hProc );
	CloseHandle( hProc );

	if( !dwPriorityClass || !matchname) return 0;
	else return 1;
}

#elif !_XBOX
// Warning: this method of spawning (on Windows anyway) makes the child process inherit
//  all of the handles of the parent process, meaning that if the parent process
//  terminates unexpectedly, the TCP ports are still open!
int system_detach(char *cmd, int minimized/*not used*/)
{
	char	*args[50],buf[1000];

	assert(strlen(cmd) < ARRAY_SIZE(buf)-1);
	strcpy(buf,cmd);
	tokenize_line(buf,args,0);
	return _spawnvp( _P_DETACH , args[0], args );
}

int system_poke(int pid)
{
	//Needs to be implemented.
	return 0;
}

int system_pokename(int pid, const char *name)
{
	return 0;
}

#endif

#if !PLATFORM_CONSOLE

#undef FILE
S32 didAllocConsole;
bool newConsoleDefaultHidden=false;
void newConsoleWindow(void)
{
	int hCrt,i;
	FILE *hf;
	S32 hidden = !compatibleGetConsoleWindow();
	
	AllocConsole();
	didAllocConsole = 1;
	if (hidden && newConsoleDefaultHidden)
	{
		ShowWindow(compatibleGetConsoleWindow(), SW_HIDE);
	}
	{
		// StdOut
		hCrt = _open_osfhandle(	(intptr_t) GetStdHandle(STD_OUTPUT_HANDLE),_O_TEXT);
		if (hCrt==-1) return;
		hf = _wfdopen( hCrt, L"w" );
		*stdout = *hf;
		// StdIn
		hCrt = _open_osfhandle(	(intptr_t) GetStdHandle(STD_INPUT_HANDLE),_O_TEXT);
		if (hCrt==-1) return;
		hf = _wfdopen( hCrt, L"r" );
		*stdin = *hf;
#ifdef setvbuf
#undef setvbuf
#endif
		i = setvbuf( stdout, NULL, _IONBF, 0 );




	}

	consoleSetToUnicodeFont();
	autoPrintExecutableVersion();
}

AUTO_RUN_FIRST;
void consoleSetToUnicodeFont(void)
{
	/*
	CONSOLE_FONT_INFOEX info;

	if (GetStdHandle(STD_OUTPUT_HANDLE))
	{

		info.cbSize = sizeof(CONSOLE_FONT_INFOEX);
		info.nFont = 7;
		info.dwFontSize.X = 0;
		info.dwFontSize.Y = 0;
		info.FontFamily = 54;
		info.FontWeight = 0;
		wcscpy_s(info.FaceName, ARRAY_SIZE(info.FaceName), L"TerminalVector");

		SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), true, &info);
	}*/

}

void consoleBringToTop(void)
{
	BringWindowToTop(compatibleGetConsoleWindow());
}

void setConsoleTitleEx(const char *msg, int pid)
{
	static	char	last_buf[1000];

	if (!msg)
		return;
	if (strcmp(last_buf,msg)==0)
		return;
	PERFINFO_AUTO_START_FUNC();
	if (pid)
		sprintf(last_buf, "[pid %d] %s", pid, msg);
	else
		Strncpyt(last_buf,msg);
	SetConsoleTitle_UTF8(msg);
	PERFINFO_AUTO_STOP();
}

void setConsoleTitleWithPid(const char *msg)
{
	setConsoleTitleEx(msg, getpid());
}

void setConsoleTitle(const char *msg)
{
	setConsoleTitleEx(msg, 0);
}

#define FG_MASK 0x0F
#define BG_MASK 0xF0
#define FG_DEFAULT_COLOR (COLOR_RED | COLOR_GREEN | COLOR_BLUE)

static WORD sCurConsoleColor = FG_DEFAULT_COLOR;
HANDLE getConsoleHandle(void)
{
	static HANDLE console_handle = NULL;

	if (console_handle==NULL) {
		console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	return console_handle;
}

void consoleSetColor(ConsoleColor fg, ConsoleColor bg)
{
	if (!getConsoleHandle())
		return;
	if (bg==COLOR_LEAVE) {
		bg = sCurConsoleColor & BG_MASK;
	} else {
		bg <<= 4;
	}
	if (fg==COLOR_LEAVE) {
		fg = sCurConsoleColor & FG_MASK;
	}
	sCurConsoleColor = fg | bg;
	SetConsoleTextAttribute(getConsoleHandle(), sCurConsoleColor);
}

ConsoleColor consoleGetColorFG(void)
{
	CONSOLE_SCREEN_BUFFER_INFO sbi = {0};
	GetConsoleScreenBufferInfo(getConsoleHandle(), &sbi);
	return sbi.wAttributes & 0xf;
}

ConsoleColor consoleGetColorBG(void)
{
	CONSOLE_SCREEN_BUFFER_INFO sbi = {0};
	GetConsoleScreenBufferInfo(getConsoleHandle(), &sbi);
	return (sbi.wAttributes & 0xf0) >> 4;
}

#define MAX_CONSOLE_PUSH_DEPTH 8
static ConsoleColor sPushedFG[MAX_CONSOLE_PUSH_DEPTH];
static ConsoleColor sPushedBG[MAX_CONSOLE_PUSH_DEPTH];
int iCurConsolePushDepth = 0;

void consolePopColor(void)
{
	if (iCurConsolePushDepth)
	{
		iCurConsolePushDepth--;
		assert(iCurConsolePushDepth >= 0 && iCurConsolePushDepth < MAX_CONSOLE_PUSH_DEPTH);
		consoleSetColor(sPushedFG[iCurConsolePushDepth], sPushedBG[iCurConsolePushDepth]);
	}
}

void consolePushColor(void)
{
	if (iCurConsolePushDepth < MAX_CONSOLE_PUSH_DEPTH)
	{
		sPushedFG[iCurConsolePushDepth] = consoleGetColorFG();
		sPushedBG[iCurConsolePushDepth] = consoleGetColorBG();
		iCurConsolePushDepth++;
	}
}



void consoleSetDefaultColor()
{
	consoleSetColor(FG_DEFAULT_COLOR, 0);
}


#define MAX_SIZE	16*1024
CHAR_INFO console_capture_buffer[MAX_SIZE];
COORD console_capture_buffer_size;
CHAR_INFO *consoleGetCapturedData(void) {
	return console_capture_buffer;
}
void consoleGetCapturedSize(COORD* coord) {
	if(coord){
		*coord = console_capture_buffer_size;
	}
}


void consoleCapture(void)
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	COORD bufferCoord = {0,0};
	SMALL_RECT rect = {0, 0, 20, 20};
	CONSOLE_SCREEN_BUFFER_INFO cbi;
	BOOL b;

	GetConsoleScreenBufferInfo(hConsole, &cbi);
	console_capture_buffer_size = cbi.dwSize;
	console_capture_buffer_size.X = MIN(console_capture_buffer_size.X, 110);
	console_capture_buffer_size.Y = MIN(console_capture_buffer_size.Y, MAX_SIZE / console_capture_buffer_size.X);
	do {
		rect.Right = console_capture_buffer_size.X;
		rect.Top = 0;
		rect.Bottom = console_capture_buffer_size.Y;
		if (rect.Bottom > cbi.dwCursorPosition.Y+1) {
			rect.Bottom = cbi.dwCursorPosition.Y+1;
		} else if (rect.Bottom < cbi.dwCursorPosition.Y) {
			int dy = cbi.dwCursorPosition.Y - rect.Bottom + 1;
			rect.Top += dy;
			rect.Bottom += dy;
		}

		b = ReadConsoleOutput(
			hConsole,
			console_capture_buffer,
			console_capture_buffer_size,
			bufferCoord,
			&rect);
		if (!b) {
			console_capture_buffer_size.Y /= 2;
		}

	} while (!b && console_capture_buffer_size.Y);
	if (!console_capture_buffer_size.Y)
		return;
	console_capture_buffer_size.Y = rect.Bottom - rect.Top;
//	printf("read : %dx%d characters\n", console_capture_buffer_size.X, console_capture_buffer_size.Y);
//	for (i=0; i<console_capture_buffer_size.Y; i++) {
//		for (j=0; j<console_capture_buffer_size.X; j++) {
//			printf("%c", console_capture_buffer[i*console_capture_buffer_size.X + j]);
//		}
//	}
}

//color: #0f0; background: #f00;
char *GetHTMLColorStringFromAttribute(int iAttribute)
{
	static char temp[64];
	char fgIntensifier = iAttribute & FOREGROUND_INTENSITY ? 'f' : '8';
	char bgIntensifier = iAttribute & BACKGROUND_INTENSITY ? 'f' : '8';

	sprintf(temp, "color: #%c%c%c; background: #%c%c%c;",
		iAttribute & FOREGROUND_RED ? fgIntensifier : '0',
		iAttribute & FOREGROUND_GREEN ? fgIntensifier : '0',
		iAttribute & FOREGROUND_BLUE ? fgIntensifier : '0',
		iAttribute & BACKGROUND_RED ? bgIntensifier : '0',
		iAttribute & BACKGROUND_GREEN ? bgIntensifier : '0',
		iAttribute & BACKGROUND_BLUE ? bgIntensifier : '0');

	return temp;
}

void ConsoleCaptureHTML(char **ppOutString)
{
	int x;
	int y;
	int iLastAttributes = console_capture_buffer[0].Attributes;
	consoleCapture();
	estrPrintf(ppOutString, "<pre style=\"font-family: monospace;\">\n");
	estrConcatf(ppOutString, "<span style=\"%s\">", GetHTMLColorStringFromAttribute(iLastAttributes));

	for (y = 0; y < console_capture_buffer_size.Y; y++)
	{
		for (x = 0; x < console_capture_buffer_size.X; x++)
		{
			CHAR_INFO *pCharInfo = &console_capture_buffer[y * console_capture_buffer_size.X + x];
					
			if (pCharInfo->Attributes != iLastAttributes)
			{
				iLastAttributes = pCharInfo->Attributes;
				estrConcatf(ppOutString, "</span><span style=\"%s\">", GetHTMLColorStringFromAttribute(iLastAttributes));
			}

			switch (pCharInfo->Char.AsciiChar)
			{
			case '&':
				estrConcatf(ppOutString, "&amp;");
				break;
			case '<':
				estrConcatf(ppOutString, "&lt;");
				break;
			case '>':
				estrConcatf(ppOutString, "&gt;");
				break;
			case '\'':
				estrConcatf(ppOutString, "&#39;");
				break;
			case '"':
				estrConcatf(ppOutString, "&quot;");
				break;
			default:
				estrConcatChar_dbg_inline(ppOutString, pCharInfo->Char.AsciiChar, __FILE__, __LINE__);
			}
		}
		estrConcatf(ppOutString, "\n");
	}

	estrConcatf(ppOutString, "</span></pre>");
}

AUTO_STRUCT;
typedef struct ConsoleForServerMon
{
	char *pConsole; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
} ConsoleForServerMon;


bool GetConsoleOverviewForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
    bool bRetVal;
    ConsoleForServerMon *pConsole = StructCreate(parse_ConsoleForServerMon);

	ConsoleCaptureHTML(&pConsole->pConsole);

    bRetVal =  ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
        pConsole, parse_ConsoleForServerMon, iAccessLevel, 0, pStructInfo, eFlags);

	StructDestroy(parse_ConsoleForServerMon, pConsole);

    return bRetVal;
}

AUTO_RUN;
void RegisterConsoleXpath(void)
{
	RegisterCustomXPathDomain(".console", GetConsoleOverviewForHttp, NULL);
}


void consoleGetDims(COORD* coord)
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO cbi;
	COORD ret;

	GetConsoleScreenBufferInfo(hConsole, &cbi);
	ret.X = cbi.srWindow.Right - cbi.srWindow.Left + 1;
	ret.Y = cbi.srWindow.Bottom - cbi.srWindow.Top + 1;
	
	*coord = ret;
}

void consoleUpSize(int minBufferWidth, int minBufferHeight)
{
	CONSOLE_SCREEN_BUFFER_INFO cbi;
	int bufferWidth, bufferHeight;

	GetConsoleScreenBufferInfo(getConsoleHandle(), &cbi);
	bufferWidth = max(cbi.dwSize.X, minBufferWidth);
	bufferHeight = max(cbi.dwSize.Y, minBufferHeight);
	consoleSetSize(bufferWidth, bufferHeight, 0);
}

void consoleSetSize(int bufferWidth, int bufferHeight, int windowHeight)
{
	COORD	coord;
	CONSOLE_SCREEN_BUFFER_INFO cbi;
	SMALL_RECT	rect;
	WINDOWINFO wi = {0};
	WINDOWINFO desktopwi = {0};
	CONSOLE_FONT_INFO cfi = {0};
	
	if(system_specs.isWine || getWineVersion())
		return;

	GetConsoleScreenBufferInfo(getConsoleHandle(), &cbi);
	if(bufferWidth > 0)
		coord.X = bufferWidth;
	else
		coord.X = cbi.dwSize.X;
	if(bufferHeight > 0)
		coord.Y = bufferHeight;
	else
		coord.Y = cbi.dwSize.Y;
	SetConsoleScreenBufferSize(getConsoleHandle(), coord);
	
	GetConsoleScreenBufferInfo(getConsoleHandle(), &cbi);
	rect = cbi.srWindow;
	
	wi.cbSize = sizeof(wi);
	GetWindowInfo(compatibleGetConsoleWindow(), &wi);
	desktopwi.cbSize = sizeof(wi);
	GetWindowInfo(GetDesktopWindow(), &desktopwi);
	GetCurrentConsoleFont(getConsoleHandle(), FALSE, &cfi);
	cfi.dwFontSize = GetConsoleFontSize(getConsoleHandle(), cfi.nFont);
	if (cfi.dwFontSize.X && wi.rcWindow.left != wi.rcWindow.right) {
		S32 foundMonitor = 0;
		
		if(multiMonGetNumMonitors()){
			POINT	ptTopLeft = {wi.rcWindow.left, wi.rcWindow.top};
			S32		borderNoScrollPixelsX = 2 * GetSystemMetrics(SM_CYSIZEFRAME);
			S32		borderWithScrollPixelsX = borderNoScrollPixelsX + GetSystemMetrics(SM_CXVSCROLL);
			S32		maxBufferPixelsX = cbi.dwMaximumWindowSize.X * cfi.dwFontSize.X;
								
			FOR_BEGIN(i, (S32)multiMonGetNumMonitors());
				MONITORINFOEX moninfo;
				RECT r;
				multiMonGetMonitorInfo(i, &moninfo);
				r = moninfo.rcMonitor;
				r.left -= 200;
				r.right -= 200;
				r.top -= 200;
				
				if(PtInRect(&r, ptTopLeft)){
					S32 maxX;
					S32 diffX = moninfo.rcWork.right - ptTopLeft.x;
					
					if(diffX >= maxBufferPixelsX + borderNoScrollPixelsX){
						maxX = cbi.dwMaximumWindowSize.X;
					}else{
						maxX = (diffX - borderWithScrollPixelsX) / cfi.dwFontSize.X;
					}
					
					foundMonitor = 1;
					cbi.dwMaximumWindowSize.X = maxX;
					break;
				}
			FOR_END;
		}
		
		if(!foundMonitor){
			int monitorWidth = cbi.dwMaximumWindowSize.X * cfi.dwFontSize.X;
			if (ABS(wi.rcWindow.left - desktopwi.rcWindow.left) <
				ABS(wi.rcWindow.left - desktopwi.rcWindow.right))
			{
				// Closer to left edge of left monitor
				cbi.dwMaximumWindowSize.X -= (wi.rcWindow.left - desktopwi.rcWindow.left) / cfi.dwFontSize.X;
			} else {
				// Closer to right edge of right monitor, assume on right monitor
				cbi.dwMaximumWindowSize.X = (desktopwi.rcWindow.right - wi.rcWindow.left) / cfi.dwFontSize.X;
			}
		}
	}
	
	bufferWidth = min(cbi.dwMaximumWindowSize.X, cbi.dwSize.X);
	if(bufferWidth > 0)
		rect.Right = bufferWidth + rect.Left - 1;

	windowHeight = min(cbi.dwMaximumWindowSize.Y-2, windowHeight);
	if(windowHeight > 0)
		rect.Bottom = windowHeight + rect.Top - 1;

	SetConsoleWindowInfo(getConsoleHandle(), TRUE, &rect);
}

int consoleYesNo(void) {
	char ch;
	do {
		ch = _getch();
		ch = tolower(ch);
		if (ch=='n' || ch=='\n' || ch=='\r') { printf("\n"); return 0; }
	} while (ch!='y');
	printf("\n");
	return 1;
}

bool consoleIsCursorOnLeft(void)
{
	CONSOLE_SCREEN_BUFFER_INFO cbi;
	PERFINFO_AUTO_START_FUNC();
	GetConsoleScreenBufferInfo(getConsoleHandle(), &cbi);
	PERFINFO_AUTO_STOP();
	return cbi.dwCursorPosition.X == 0;
}


void printfColor_dbg(int color, const char *str, ...)
{
	va_list arg;
	
	printfEnterCS();
	consoleSetColor(color, 0);
	va_start(arg, str);
	vprintf(str, arg);
	va_end(arg);
	consoleSetColor(COLOR_RED|COLOR_GREEN|COLOR_BLUE, 0);
	printfLeaveCS();
}

const char *getUserName()
{
	static int gotUserName = 0;
	static char	name[1000];
	static WCHAR nameW[1000];
	int	name_len = ARRAY_SIZE(name);
	int nameW_len = ARRAY_SIZE(nameW);

	if(!gotUserName)
	{
#ifdef UNICODE
		if(!GetUserName(nameW,&nameW_len))
			nameW[0] = '\0';
#else
		if(!GetUserName(name,&name_len))
			name[0] = '\0';
		MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, name, -1, SAFESTR(nameW));
#endif	
		WideCharToMultiByte (CP_UTF8, 0, nameW, -1, SAFESTR(name), NULL, NULL);
		gotUserName = 1;
	}
	return name;
}

const char *getHostName() 
{
	static char hostname[80] = "";
	if (!hostname[0])
	{
		if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR)
			return "Error getting hostname";
	}
	return hostname;
}

static void generateMachineID(char *buffer, size_t size)
{
	char randomString[1000];
	sprintf(randomString, "%d%s%s%d", cryptSecureRand(), getUserName(), getHostName(), timeSecondsSince2000());
	cryptCalculateSHAHash_s(randomString, buffer, size, true);
}
const char *getMachineID(void)
{
	static char machineID[MACHINE_ID_MAX_LEN] = "";

	if (!machineID[0])
	{
		regGetAppString_ForceAppName("Core", "machineid", "", machineID, ARRAY_SIZE_CHECKED(machineID));
		if (!machineID[0])
		{
			generateMachineID(machineID, ARRAY_SIZE_CHECKED(machineID));
			regPutAppString_ForceAppName("Core", "machineid", machineID);
		}
	}
	return machineID;
}

#else

void setConsoleTitle(const char *msg)
{
}

const char *getHostName() 
{
	static char hostname[80] = "";
	if (!hostname[0])
	{
#if _PS3
		sprintf(hostname, "PS3");
#else
		sprintf(hostname, "XBOX");
#endif
	}
	return hostname;
}

void printfColor_dbg(int color, const char *str, ...)
{
	// Just do a normal printf
	va_list arg;
	va_start(arg, str);
	vprintf(str, arg);
	va_end(arg);	
}

void consoleSetColor(ConsoleColor fg, ConsoleColor bg) {
}

void consolePushColor(void) 
{
}

void consolePopColor(void) 
{
}


void consoleSetDefaultColor()
{
}

int consoleYesNo(void)
{
	return 1;
}

bool consoleIsCursorOnLeft(void)
{
	return true;
}

const char *getUserName()
{
#if _PS3
	static char name[1024];
	// YVS
	strcpy(name,"FAKEUSER");
	return name;
#else
	static char name[256] = { 0 };
	DWORD namesize = sizeof(name);
	DmGetXboxName(name, &namesize);
	return name;
#endif
}

#endif

// Outputs a string to the debug console (useful for remote debugging, and outputting stuff we don't want users to see)
void OutputDebugStringv(FORMAT_STR const char *fmt, va_list va)
{
	if (IsDebuggerPresent()) {
		char buf[4096];
		vsprintf(buf, fmt, va);

		OutputDebugString_UTF8(buf);
		memlog_printf(0, "%s", buf);
	} 
	else
	{
		vprintf(fmt, va);
	}
}

#undef OutputDebugStringf
void OutputDebugStringf(const char *fmt, ... ) { // Outputs a string to the debug console (useful for remote debugging, and outputting stuff we don't want users to see)
	va_list va;

	va_start(va, fmt);
	OutputDebugStringv(fmt, va);
	va_end(va);
}

int strIsAlphaNumeric(const unsigned char* str)
{
	if(!str)
		return 0;

	while(*str != '\0')
	{
		if(!isalnum(*str))
			return 0;
		str++;
	}

	return 1;
}

int strIsAlpha(const unsigned char* str)
{
	if(!str)
		return 0;

	while(*str != '\0')
	{
		if(!isalpha(*str))
			return 0;
		str++;
	}

	return 1;
}

int strIsNumeric(const unsigned char* str)
{
	if(!str)
		return 0;

	while(*str != '\0')
	{
		if(!isdigit(*str))
			return 0;
		str++;
	}

	return 1;
}

/*
void strcatf(char* concatStr, const char* format, ...)
{
	va_list args;

	va_start(args, format);
	vsprintf(concatStr + strlen(concatStr), format, args);
	va_end(args);
}
*/

void strcatf_s(char* concatStr, size_t concatStr_size, const char* format, ...)
{
	va_list args;
	size_t concatStr_len = strlen(concatStr);

	va_start(args, format);
	vsprintf_s(concatStr + concatStr_len, concatStr_size - concatStr_len, format, args);
	va_end(args);
}

char *changeFileExt_s(const char *fname,const char *new_extension,char *buf, size_t buf_size)
{
	char	*s;

	strcpy_s(buf,buf_size,fname);
	s = strrchr(buf,'.');
	if (strrchr(buf,'/') < s && strrchr(buf,'\\') < s)
		*s = 0;
	strcat_s(buf,buf_size,new_extension);
	return buf;
}

int strEndsWithAny(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** endings)
{
    int size = eaSize( &endings );
    int it;

    for( it = 0; it != size; ++it ) {
        if( strEndsWith( str, endings[ it ])) {
            return true;
        }
    }

    return false;
}

int strStartsWithAny(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** starts)
{
    int size = eaSize( &starts );
    int it;

    for( it = 0; it != size; ++it ) {
        if( strStartsWith( str, starts[ it ])) {
            return true;
        }
    }

    return false;
}

int strEndsWithAnyStatic(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** endings)
{
	int it;

	for( it = 0; endings[ it ]; ++it ) {
		if( strEndsWith( str, endings[ it ])) {
			return true;
		}
	}

	return false;
}

int strStartsWithAnyStatic(SA_PARAM_OP_STR const char* str, SA_PARAM_OP_STR const char** starts)
{
	int it;

	for( it = 0; starts[ it ]; ++it ) {
		if( strStartsWith( str, starts[ it ])) {
			return true;
		}
	}

	return false;
}

int strEndsWithSensitive(SA_PARAM_NN_STR const char* str, SA_PARAM_NN_STR const char* ending)
{
	int strLength;
	int endingLength;

	strLength = (int)strlen(str);
	endingLength = (int)strlen(ending);

	if(endingLength > strLength)
		return 0;

	if(strcmp(str + strLength - endingLength, ending) == 0)
		return 1;
	else
		return 0;
}

int printPercentageBar(int filled, int total) {
	int i;
	for (i=0; i<total; i++) {
		printf("%c", (i<filled)?0xdb:0xb0);
	}
	return total;
}

void backSpace(int num, bool clear) {
	int i;
	if (clear) {
		for (i=0; i<num; i++) 
			printf("%c %c", 8, 8);
	} else {
		for (i=0; i<num; i++) 
			printf("%c", 8);
	}

}

char *printDate_s(__time32_t date, char *buf, size_t buf_size)
{
	__time32_t now = _time32(NULL);
	struct tm time;
	struct tm today;

	if (_localtime32_s(&time, &date)) {
		sprintf_s(SAFESTR2(buf), "INVALID");
		return buf;
	}

	_localtime32_s(&today, &now);

	if (today.tm_year == time.tm_year && today.tm_mday==time.tm_mday && today.tm_mon==time.tm_mon) {
		sprintf_s(SAFESTR2(buf), "Today, %02d:%02d:%02d", time.tm_hour, time.tm_min, time.tm_sec);
	} else if (today.tm_year == time.tm_year && today.tm_yday-6<=time.tm_yday && today.tm_yday>=time.tm_yday) {
		int dd = today.tm_yday-time.tm_yday;
		sprintf_s(SAFESTR2(buf), "%d day%s ago, %02d:%02d:%02d", dd, (dd==1)?"":"s", time.tm_hour, time.tm_min, time.tm_sec);
	} else {
		sprintf_s(SAFESTR2(buf), "%04d %02d/%02d %02d:%02d:%02d", time.tm_year+1900, time.tm_mon+1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
	}
	return buf;
}

void printDateEstr_dbg(__time32_t date, char **buf, const char *caller_fname, int line)
{
	__time32_t now = _time32(NULL);
	struct tm time;
	struct tm today;

	if (_localtime32_s(&time, &date)) {
		estrConcatf_dbg(buf, caller_fname, line, "INVALID");
	}

	_localtime32_s(&today, &now);

	if (today.tm_year == time.tm_year && today.tm_mday==time.tm_mday && today.tm_mon==time.tm_mon) {
		estrConcatf_dbg(buf, caller_fname, line, "Today, %02d:%02d:%02d", time.tm_hour, time.tm_min, time.tm_sec);
	} else if (today.tm_year == time.tm_year && today.tm_yday-6<=time.tm_yday && today.tm_yday>=time.tm_yday) {
		int dd = today.tm_yday-time.tm_yday;
		estrConcatf_dbg(buf, caller_fname, line, "%d day%s ago, %02d:%02d:%02d", dd, (dd==1)?"":"s", time.tm_hour, time.tm_min, time.tm_sec);
	} else {
		estrConcatf_dbg(buf, caller_fname, line, "%04d %02d/%02d %02d:%02d:%02d", time.tm_year+1900, time.tm_mon+1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
	}
}


char *filenameInFixedSizeBuffer(const char *filename, int strwidth, char *buf, int buf_size, bool right_align)
{
	char format[10];
	char * align = (right_align ? "" : "-");
	MIN1(strwidth, buf_size-1);

	if ((int)strlen(filename) >= strwidth) {
		sprintf(format, "...%%%s%ds", align, strwidth-3);
		sprintf_s(SAFESTR2(buf), FORMAT_OK(format), filename + strlen(filename) - (strwidth-3));
	} else {
		sprintf(format, "%%%s%ds", align, strwidth);
		sprintf_s(SAFESTR2(buf), FORMAT_OK(format), filename);
	}
	return buf;
}



//#define STR_OVERRUN_DEBUG

char *strncpyt(char *dst, const char *src, int dst_size) // Does a strncpy, null terminates, truncates without asserting
{
	strncpy_s(dst, dst_size, src, dst_size - 1);
	dst[dst_size-1] = 0;
#ifdef STR_OVERRUN_DEBUG
	{
		// Fills all of the area the caller claims is available with a fill, to catch people passing in too small of buffer sizes
		size_t len = strlen(dst)+1;
		if (dst_size > len) {
			memset(dst + len, 0xf9, dst_size - len - 1);
		}
	}
#endif
	return dst;
}

/*
moved open_crytpic() into utf8.h
*/

char *strcpy_unsafe(char *dst, const char *src)
{
	register char *c=dst;
	register const char *s=0;
	for (s=src; *s; *c++=*s++);
	*c=0;
	return dst;
}


// Just so we can use it in the Command window in visual studio
size_t slen(const char *s)
{
	return strlen(s);
}

int mcmp(const void *a, const void *b, size_t size)
{
	return memcmp(a, b, size);
}

static char *printUnitBase_s(char *buf, size_t buf_size, S64 val,S64 *base)
{
	char		*units = "";

	if (val < base[0])
	{
		if (base[0] == 1024)
			sprintf_s(SAFESTR2(buf), "%"FORM_LL"dB",val);
		else
			sprintf_s(SAFESTR2(buf), "%"FORM_LL"d",val);
		return buf;
	}
	else if (val < base[1])
	{
		val /= base[0] / 10;
		units = "K";
	}
	else if (val < base[2])
	{
		val /= base[1] / 10;
		units = "M";
	}
	else if (val < base[3])
	{
		val /= base[2] / 10;
		units = "G";
	}
	else if (val < base[4])
	{
		val /= base[3] / 10;
		units = "T";
	}
	else if (val < base[5])
	{
		val /= base[4] / 10;
		units = "P";
	}
	if (val >= base[0])
		sprintf_s(SAFESTR2(buf), "%d%s",(int)(val/10),units);
	else
		sprintf_s(SAFESTR2(buf), "%d.%d%s",(int)(val/10),(int)(val%10),units);
	return buf;
}

char *printUnit_s(char *buf, size_t buf_size, S64 val)
{
	static	S64 base_2[] = { 1 << 10, 1 << 20, 1 << 30, 1LL << 40, 1LL << 50, 1LL << 60 };

	return printUnitBase_s(buf,buf_size,val,base_2);
}

char *printUnitDecimal_s(char *buf, size_t buf_size, S64 val)
{
	static	S64 base_10[] = { 1000, 1000000, 1000000000, 1000000000000, 1000000000000000, 1000000000000000000 };

	return printUnitBase_s(buf,buf_size,val,base_10);
}

char *printTimeUnit_s(char *buf, size_t buf_size, U32 val)
{
	if (val < 3600)
	{
		sprintf_s(SAFESTR2(buf), "%d:%02d",val/60,val%60);
	}
	else if (val < 24*3600)
	{
		sprintf_s(SAFESTR2(buf), "%d:%02d:%02d",val/3600,(val/60)%60,val%60);
	}
	else
	{
		sprintf_s(SAFESTR2(buf), "%ddays",val/(3600 * 24));
	}
	return buf;
}

char* getCommaSeparatedInt(S64 x){
	static int curBuffer = 0;
	// 27+'\0' is the max length of a 64bit value with commas.
	static char bufferArray[10][30]; 
	
	char* buffer = bufferArray[curBuffer = (curBuffer + 1) % 10];
	int j = 0;
	int addSign = 0;

	buffer += ARRAY_SIZE(bufferArray[0]) - 1;

	*buffer-- = 0;
	
	if(x < 0){
		x = -x;
		addSign = 1;
	}

	do{
		*buffer-- = '0' + (char)(x % 10);

		x = x / 10;

		if(x && ++j == 3){
			j = 0;
			*buffer-- = ',';
		}
	}while(x);
	
	if(addSign){
		*buffer-- = '-';
	}

	return buffer + 1;
}

// Moved from contextQsort
void swap (void *vpa, void *vpb, size_t width)
{
	char *a = (char *)vpa;
	char *b = (char *)vpb;
	char tmp;

	if ( a != b )
		/* Do the swap one character at a time to avoid potential alignment
		problems. */
		while ( width-- ) {
			tmp = *a;
			*a++ = *b;
			*b++ = tmp;
		}
}




void removeLeadingAndFollowingSpaces(char * str)
{
	int len;
	while( str[0] == ' ' )
		memmove(str, str+1, strlen( str ) + 1 );

	len = (int)strlen( str );
	if( len )
	{
		len--;
		while( str[len] == ' ' )
		{
			str[len] = '\0';
			len--;
		}
	}
}

char* strInsert( char * dest, const char * insert_ptr, const char *insert )
{
	static char  *buffer;
	static int buffer_size = 0;
	int requiredBufferSize;

	if(!dest || !insert_ptr || !insert)
		return NULL;

	requiredBufferSize = (int)strlen(dest)+(int)strlen(insert);
	if( buffer_size < requiredBufferSize+1 )
	{
		buffer_size = requiredBufferSize+1;
		buffer = realloc( buffer, buffer_size );
	}

	strncpyt( buffer, dest, insert_ptr-dest + 1);
	strcat_s( buffer, buffer_size, insert );
	strcat_s( buffer, buffer_size, insert_ptr );

	return buffer;
}

char *strchrInsert( char * dest, const char * insert, int character )
{
	static char *str = NULL;
	char *curptr;
	char *dup = strdup(dest);
	char *origdup = dup;
	estrClear(&str);
	
	curptr = strchr(dup,character);

  	while(curptr)
	{
		char tmp;
		*curptr++;
		tmp = *curptr;
	 	*curptr = '\0';
		estrConcatString(&str, dup, curptr-dup+1);
		*curptr = tmp;
		estrConcatf(&str, "%s", insert );		
		dup = curptr;
		curptr = strchr(dup,character);
	}
	estrConcatf( &str, "%s", dup );
	free(origdup);
	return str;
}

char *strstrInsert( const char * src, const char * find, const char * replace )
{
	char *str = NULL;
	char *curptr;
	char *dup = strdup(src);
	char *last_end;

	estrClear(&str);
	curptr = strstri(dup,find);
	last_end = dup;

	while(curptr)
	{
		estrConcat(&str, last_end, curptr-last_end);
		estrConcatf(&str, "%s", replace );		
		curptr += strlen(find);
		last_end = curptr;
		curptr = strstri(last_end, find);
	}

	estrConcatf( &str, "%s", last_end );
	free(dup);
	return str;
}

void strchrReplace( char *dest, int find, int replace )
{
	char * curptr = strchr(dest, find);
	char * destptr = dest;
	while(curptr)
	{
		*curptr = replace;
		destptr = curptr+1;
		curptr = strchr( destptr, find );
	}
}

int strchrCount( const char *str, int find )
{
	int count = 0;
	const char * curptr = strchr(str, find);
	const char * strptr = str;
	while(curptr)
	{
		++count;
		strptr = curptr+1;
		curptr = strchr( strptr, find );
	}

	return count;
}

int strstriReplace_s(char *src, size_t src_size, const char *find, const char *replace)
{
	char *sec2;
	char *endsec1 = strstri(src, find);
	if (!endsec1)
		return 0;

	sec2 = strdup(endsec1 + strlen(find));
	strcpy_s(endsec1, src_size - (endsec1-src), replace);
	strcat_s(src, src_size, sec2);
	free(sec2);
	return 1;
}



// chatserver gets reserved names from mapservers
StashTable receiveHashTable(Packet * pak, int mode)
{		
	StashTable table;
	int i, count;

	count = pktGetBitsPack(pak, 1);

	table = stashTableCreateWithStringKeys(count, mode);

	for(i=0;i<count;i++)
	{
		char s[256];

		pktGetString(pak,s,sizeof(s));
		assert(!s[0]);
		stashAddPointer(table, s, NULL, false);
	}

	return table;
}

void sendHashTable(Packet * pak, StashTable table)
{
	StashElement element;
	StashTableIterator it;

	assert(table);

	pktSendBitsPack(pak, 1, stashGetCount(table));

	stashGetIterator(table, &it);
	while(stashGetNextElement(&it, &element))
	{
		const char * name = stashElementGetStringKey(element);
		assert(name);
		pktSendString(pak, name);
	}
}

char *incrementName(unsigned char *name, unsigned int maxlen)
{
	unsigned int len = (unsigned int)strlen(name);
	if (!isdigit(name[len-1])) {
		// Need to add a digit to the end
		if (len < maxlen) {
			strcat_s(name, maxlen+1, "1");
		} else {
			name[len-1]='1';
		}
	} else {
		// Already ends in a number, increment!
		unsigned char *s = &name[len-1];
		S64 value;
		char valuebuf[33];
		unsigned int valuelen;
		while (isdigit(*s) && s >= name) {
			s--;
			len--;
		}
		s++;
		value = atoi64(s) + 1;
		name[len]='\0'; // clear out digit for strcat later
		sprintf(valuebuf, "%"FORM_LL"d", value);
		if (strlen(valuebuf) > maxlen) { // Loop
			value = 0;
			sprintf(valuebuf, "%"FORM_LL"d", value);
		}
		valuelen = (int)strlen(valuebuf);
		while (valuelen + len > maxlen) {
			len--;
			name[len]='\0';
		}
		strcat_s(name, maxlen+1, valuebuf);
	}
	return name;
}



void bubbleSort(void *base, size_t num, size_t width, const void* context,
				int (__cdecl *comp)(const void* item1, const void* item2, const void* context))
{
	char *tmp = _alloca(width);
	char *start = (char*)base;
	char *p = NULL;
	char *q = NULL;
	char *end = start + num*width;
	
	for(p = start + width; p < end; p += width )
	{
		for( q = p; q > start; q -= width ) 
		{
			char *r = q - width;
			if( 0 < comp(r, q, context))
			{
#define SSRT_MOVE(dst, src) memmove(dst,src,width)
				SSRT_MOVE(tmp, r);
				SSRT_MOVE(r,q);
				SSRT_MOVE(q,tmp);
#undef SSRT_MOVE
			}
			else
			{
				break;
			}
		}
	}
}

void mergeSort(SA_PARAM_NN_VALID void *base, size_t num, size_t width, const void* context,
			   int (__cdecl *comp)(const void* item1, const void* item2, const void* context))
{
	U32 window = 2;
	U32 half = 1;
	U32 index;
	U32 scratchsize = 1;
	U8 *start = base;
	U8 *scratch;
	U8 *left, *lend;
	U8 *right, *rend;
	U8 *bend = start + num * width;
	U8 *mid;

	if (num < 2)
		return;
	
	//make scratch
	while (scratchsize < num) scratchsize = scratchsize << 1;
	if (scratchsize > 1) scratchsize = scratchsize >> 1;
 	scratch = calloc(scratchsize, width);

	while (half < num)
	{	//merge entire array
		index = 0;
		rend = 0;
		while (rend < bend)
		{	//merge single window
			mid = start + index * window * width;
			right = mid + half * width;
			//if there are not enough items in the window to merge, they are already sorted.
			if (right >= bend) break;
			rend = right + half * width;
			if (rend > bend) rend = bend;
			left = scratch;
			lend = left + half * width;

			//if it's already sorted, continue
			if (comp(right, right - width, context) >= 0)
			{
				index++;
				continue;
			}
			memmove(left, mid, half * width);
			
			//if the two halves were just reversed, swap and continue.
			if (comp(left,rend-width, context) > 0) //gt here because equality would require reordering for stability
			{
				memmove(mid, right, rend-right);				//right half might not be a full half-window.
				memmove(mid+(rend-right), left, half * width);	//if it's not, we need to make sure the left starts where the right ends.
				index++;
				continue;
			}

			while (left < lend && right < rend)
			{
				if (comp(left, right, context) <= 0)
				{
					memmove(mid, left, width);
					left += width;
				}
				else
				{	
					memmove(mid, right, width);
					right += width;
				}
				mid += width;
			}
			if (left < lend)
				memmove(mid, left, lend-left);
			else
				memmove(mid, right, rend-right);

			index++;
		}
		half = window;
		window = window << 1;
	}
	
	free(scratch);
}

// how bsearch _should_ have been prototyped..
// returns location where this key should be inserted into array (can be equal to n)
// The compare function should return -1 if the first paramater is less than the second parameter
size_t bfind(const void* key, const void* base, size_t num, size_t width, int (__cdecl *compare)(const void*, const void*))
{
	size_t low, high;
	char* arr = (char*)base;

	if (!base || !num)
		return 0;

	low = 0;
	high = num;
	while (high > low)
	{
		size_t mid = (high + low) / 2;
		int comp = compare(key, &arr[width*mid]);
		if (comp == 0)
			low = high = mid;
		else if (comp < 0)
			high = mid;
		else if (mid == low)
			low = high;
		else
			low = mid;
	}
	return low;
}

// Version of bfinds that takes a context, for thread safety
// The compare function should return -1 if the first paramater is less than the second parameter
size_t bfind_s(const void* key, const void* base, size_t num, size_t width, int (__cdecl *compare)(void *, const void*, const void*), void * context)
{
	size_t low, high;
	char* arr = (char*)base;

	if (!base || !num)
		return 0;

	low = 0;
	high = num;
	while (high > low)
	{
		size_t mid = (high + low) / 2;
		int comp = compare(context, key, &arr[width*mid]);
		if (comp == 0)
			low = high = mid;
		else if (comp < 0)
			high = mid;
		else if (mid == low)
			low = high;
		else
			low = mid;
	}
	return low;
}

#if !_PS3
// important note: disabling Wow64 redirection screws up Winsock
void disableWow64Redirection(void)
{
	PVOID dummy;
	HMODULE hModule;
	typedef BOOL (WINAPI *Wow64DisableWow64FsRedirectionType)(PVOID*);
	Wow64DisableWow64FsRedirectionType Wow64DisableWow64FsRedirection;

	hModule = LoadLibrary(L"kernel32");
	assert(hModule);
	Wow64DisableWow64FsRedirection = (Wow64DisableWow64FsRedirectionType)GetProcAddress(hModule, "Wow64DisableWow64FsRedirection");
	if (Wow64DisableWow64FsRedirection)
		Wow64DisableWow64FsRedirection(&dummy);
}

void enableWow64Redirection(void)
{
	HMODULE hModule;
	typedef BOOLEAN (WINAPI *Wow64EnableWow64FsRedirectionType)(BOOLEAN);
	Wow64EnableWow64FsRedirectionType Wow64EnableWow64FsRedirection;

	hModule = LoadLibrary(L"kernel32");
	assert(hModule);
	Wow64EnableWow64FsRedirection = (Wow64EnableWow64FsRedirectionType)GetProcAddress(hModule, "Wow64EnableWow64FsRedirection");
	if (Wow64EnableWow64FsRedirection)
		Wow64EnableWow64FsRedirection(TRUE);
}
#endif

// Functions to convert between stat time and UTC.
//
// Problem: stat and _stat and _findfirst32 (et al) return time in UTC theoretically, but in
//         real life they return an adjusted value based on whether the current time is in DST.
//
// Known issues: 1. curTime can be checked at a different time than stat, which can straddle the DST switch.
//               2. The one hour overlap when the clock is turned back an hour will cause problems.
//                  Hopefully no one is creating files then?
//
// Resolution: Let's stop using stat, except that I think gimme relies on it.
//
// The rule for thes function is as follows:
//
// --------------------------------------------------------------------------------
// Currently in DST   |   Stat time In DST   |   Seconds to add to stat time
// --------------------------------------------------------------------------------
// no                 |   no                 |   0
// no                 |   yes                |   3600
// yes                |   no                 |   -3600
// yes                |   yes                |   0
// --------------------------------------------------------------------------------

__time32_t statTimeToUTC(__time32_t statTime){
	__time32_t		result;

	PERFINFO_AUTO_START_FUNC();{
	struct tm	t;
	
	if(_localtime32_s(&t, &statTime)){
		// Invalid statTime.

		result = statTime;
	}else{
		__time32_t	curTime = _time32(NULL);
		struct tm	cur;
	
		// I'll assume that curTime is always valid.

		_localtime32_s(&cur, &curTime);
	
		result = statTime + ((S32)(t.tm_isdst?1:0) - (S32)(cur.tm_isdst?1:0)) * 3600;
	}

	}PERFINFO_AUTO_STOP();
	return result;
}

__time32_t statTimeFromUTC(__time32_t utcTime){
	__time32_t	result;

	PERFINFO_AUTO_START_FUNC();{
	struct tm t;
	
	if(_localtime32_s(&t, &utcTime)){
		// Invalid utcTime.
		
		result = utcTime;
	}else{
		__time32_t	curTime = _time32(NULL);
		struct tm	cur;

		_localtime32_s(&cur, &curTime);

		result = utcTime - ((S32)(t.tm_isdst?1:0) - (S32)(cur.tm_isdst?1:0)) * 3600;
	}
	}PERFINFO_AUTO_STOP();
	return result;
}


// The rule for thes function is as follows:
//
// --------------------------------------------------------------------------------
// Currently in DST   |   Stat time In DST   |   Seconds to add to stat time
// and a non-Samba drive
// --------------------------------------------------------------------------------
// no                 |   N/A                |   0
// yes                |   N/A                |   -3600
// --------------------------------------------------------------------------------
//__time32_t statTimeToGimmeTime(__time32_t statTime)
//{
//	return statTime + gimmeGetTimeAdjust();
//}
//
//__time32_t statTimeFromGimmeTime(__time32_t gimmeTime)
//{
//	return gimmeTime - gimmeGetTimeAdjust();
//}


// --------------------------------------------------------------------------------
// Currently in DST   |   Gimme time In DST  |   Seconds to add to gimme time
// and a non-Samba drive
// --------------------------------------------------------------------------------
// N/A                |   no                 |   0
// N/A                |   yes                |   3600
// --------------------------------------------------------------------------------
__time32_t gimmeTimeToUTC(__time32_t gimmeTime)
{
	struct tm	t;

	if(_localtime32_s(&t, &gimmeTime)){
		// localtime returns NULL if statTime is invalid somehow.
		// I dunno what an invalid time is, but I had it happen once.
		// _localtime32_s returns an error code, so this is different now

		return gimmeTime;
	}

	return gimmeTime + (t.tm_isdst?3600:0);
}

__time32_t gimmeTimeFromUTC(__time32_t utcTime)
{
	struct tm	t;

	if(_localtime32_s(&t, &utcTime)){
		// localtime returns NULL if statTime is invalid somehow.
		// I dunno what an invalid time is, but I had it happen once.
		// _localtime32_s returns an error code, so this is different now

		return utcTime;
	}

	return utcTime - (t.tm_isdst?3600:0);
}

#if !PLATFORM_CONSOLE

// for COM objects
#pragma comment ( lib, "ole32.lib" )

// creates a shortcut to {file} at {out}
// file - the full path of the file you are linking to
// out - the full path of the shortcut to make
// icon - the index into the {file} of which icon to use for the shortcut
// working_dir - if not null, the directory {file} will be run from
// args - if not null, command line arguments that are passed when {file} is run
// desc - if not null, a description of {file} (for tool tips)
int createShortcut(char *file, char *out, int icon, char *working_dir, char *args, char *desc)
{
	HRESULT hres;
	IShellLink *psl;
	S16 *pWideFile = NULL;
	S16 *pWideWorkingDir = NULL;
	S16 *pWidePath = NULL;
	S16 *pWideArgs = NULL;
	S16 *pWideDesc = NULL;
	int iRetVal = 0;


	CoInitialize(NULL);
	hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, 
                                 &IID_IShellLink, (void **) &psl); 
    if (SUCCEEDED(hres)) 
    { 
        IPersistFile* ppf; 

        hres = psl->lpVtbl->QueryInterface(psl,&IID_IPersistFile, (void **) &ppf); 
        if (SUCCEEDED(hres)) 
        { 
			char path[CRYPTIC_MAX_PATH], *tmp;

			pWideFile = UTF8_To_UTF16_malloc(file);

			hres = psl->lpVtbl->SetPath(psl,pWideFile); 

			if ( working_dir )
			{
				pWideWorkingDir = UTF8_To_UTF16_malloc(working_dir);
				psl->lpVtbl->SetWorkingDirectory(psl,pWideWorkingDir); 
			}
			else
			{
				strcpy(path, file);
				tmp = strrchr(path, '/');
				if ( !tmp )
					tmp = strrchr(path, '\\');
				if ( !tmp )
				{
					goto done;
				}
				*tmp = 0;
				pWidePath = UTF8_To_UTF16_malloc(path);
				psl->lpVtbl->SetWorkingDirectory(psl,pWidePath); 
			}
			psl->lpVtbl->SetShowCmd(psl,SW_SHOWNORMAL);
			if (icon >= 0) psl->lpVtbl->SetIconLocation(psl,pWideFile,icon); 

			pWideArgs = UTF8_To_UTF16_malloc(args);
			pWideDesc = UTF8_To_UTF16_malloc(desc);

			if (args) psl->lpVtbl->SetArguments(psl,pWideArgs); 
			if (desc) psl->lpVtbl->SetDescription(psl,pWideDesc); 

			if (SUCCEEDED(hres)) 
			{ 
				WCHAR wsz[1024];
				wsz[0]=0; 

				if ( !strEndsWith(out, ".lnk") )
				{
					char newOut[CRYPTIC_MAX_PATH];
					sprintf(newOut, "%s.lnk", out);
					MultiByteToWideChar(CP_ACP, 0, newOut, -1, wsz, 1024); 
				}
				else
					MultiByteToWideChar(CP_ACP, 0, out, -1, wsz, 1024);

				hres=ppf->lpVtbl->Save(ppf,(const WCHAR*)wsz,TRUE); 
				if ( !SUCCEEDED(hres) )
				{
					goto done;
				}
			} 
			else
				goto done;
			ppf->lpVtbl->Release(ppf); 
        } 
        psl->lpVtbl->Release(psl); 
    }
	else
		goto done;

	iRetVal = 1;

done:

	SAFE_FREE(pWideFile);
	SAFE_FREE(pWideWorkingDir);
	SAFE_FREE(pWidePath);
	SAFE_FREE(pWideArgs);
	SAFE_FREE(pWideDesc);

	return iRetVal;
}

int spawnProcess(char *cmdLine, int mode)
{
	char *args[100];
	WCHAR *wideArgs[100];
	int ret;
	//counting the NULL terminator, ie, how many fields in wideArgs are used
	int iArgCount = 0;
	int i;

	tokenize_line_quoted(cmdLine, args, 0);

	for (i = 0; i < ARRAY_SIZE(args); i++)
	{
		if ( args[i] == NULL)
		{
			wideArgs[i] = NULL;
			iArgCount = i + 1;
			break;
		}

		wideArgs[i] = UTF8_To_UTF16_malloc(args[i]);
	}

	assertmsgf(iArgCount > 0, "Too many args in %s passed in to spawnProcess", cmdLine);
	assert(wideArgs[0]);

	ret = _wspawnv(mode, wideArgs[0], wideArgs);
	if ( ret < 0 )
	{
		switch ( errno )
		{
		case (E2BIG):
			assert("Argument list is too long"==0);
			break;
		case (EINVAL):
			assert("Invalid mode specified"==0);
			break;
		case (ENOENT):
			assert("Could not find process"==0);
			break;
		case (ENOEXEC):
			assert("Not an executable"==0);
			break;
		case (ENOMEM):
			assert("Not enough memory to spawn process"==0);
			break;
		}
	}

	for (i = 0; i < iArgCount; i++)
	{
		SAFE_FREE(wideArgs[i]);
	}

	return ret;
}
#endif

extern int quick_vscprintf(const char *format, va_list args);

char *strdupf_dbg(const char *caller_fname, int line, const char *fmt, ...)
{
	va_list ap;
	int bufSize;
	char *buf;
	va_start(ap, fmt);
	bufSize = quick_vscprintf(fmt, ap) + 1;
	buf = smalloc(bufSize);
	quick_vsprintf(buf, bufSize, fmt, ap);
	va_end(ap);
	return buf;
}

#define MAX_STRTOK_TOKEN_LENGTH 128
char *strTokWithSpacesAndPunctuation(const char *pInputString, const char *pPunctuationString)
{
	static const char *pLastInputString = NULL;
	static const char *pReadHead;
	static char returnBuff[MAX_STRTOK_TOKEN_LENGTH];
	int iBytesRead = 0;

	
	if (pInputString == NULL)
	{
		pLastInputString = NULL;
		pReadHead = NULL;
		return NULL;
	}

	if (pInputString != pLastInputString)
	{
		pReadHead = pLastInputString = pInputString;
	}

	while (*pReadHead == ' ')
	{
		pReadHead++;
	}

	if (!(*pReadHead))
	{
		return NULL;
	}

	if (strchr(pPunctuationString, *pReadHead))
	{
		returnBuff[0] = *pReadHead;
		returnBuff[1] = 0;
		pReadHead++;

		return returnBuff;
	}

	do
	{
		assertmsgf(iBytesRead < MAX_STRTOK_TOKEN_LENGTH - 1, "Token overflow while tokenizing %s", pInputString);
		returnBuff[iBytesRead++] = *(pReadHead++);
	} while (*pReadHead && *pReadHead != ' ' && !(strchr(pPunctuationString, *pReadHead)));

	returnBuff[iBytesRead] = 0;

	return returnBuff;
}

void DivideString_Add(char ***pppOutStrings, const char *pInString, int iInLen, enumDivideStringPostProcessFlags ePostProcessFlags)
{
	char *pTempEString = NULL;
	int i;

	estrStackCreate(&pTempEString);
	estrConcat(&pTempEString, pInString, iInLen);

	if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_FORWARDSLASHES)
	{
		for (i=0; i < iInLen; i++)
		{
			if (pTempEString[i] == '\\')
			{
				pTempEString[i] = '/';
			}
		}
	}

	if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_BACKSLASHES)
	{
		for (i=0; i < iInLen; i++)
		{
			if (pTempEString[i] == '/')
			{
				pTempEString[i] = '\\';
			}
		}
	}

	if (ePostProcessFlags & (DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_RESPECT_SIMPLE_QUOTES))
	{
		estrTrimLeadingAndTrailingWhitespace(&pTempEString);
	}

	if (ePostProcessFlags & DIVIDESTRING_RESPECT_SIMPLE_QUOTES)
	{
		if (estrLength(&pTempEString) >= 2 && pTempEString[0] == '"' && pTempEString[estrLength(&pTempEString)-1] == '"')
		{
			estrRemove(&pTempEString, estrLength(&pTempEString)-1, 1);
			estrRemove(&pTempEString, 0, 1);
		}
	}

	if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS)
	{
		if (estrLength(&pTempEString) == 0)
		{
			estrDestroy(&pTempEString);
			return;
		}
	}



	if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_ALLOCADD)
	{
		if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE)
		{
			eaPushUnique(pppOutStrings, (char*)allocAddString(pTempEString));
		}
		else
		{
			eaPush(pppOutStrings, (char*)allocAddString(pTempEString));
		}
	}
	else if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_ALLOCADD_CASESENSITIVE)
	{
		if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE)
		{
			eaPushUnique(pppOutStrings, (char*)allocAddCaseSensitiveString(pTempEString));
		}
		else
		{
			eaPush(pppOutStrings, (char*)allocAddCaseSensitiveString(pTempEString));
		}	
	}
	else
	{
		if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE)
		{
			if (eaFindString(pppOutStrings, pTempEString) == -1)
			{
				eaPush(pppOutStrings, (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_ESTRINGS) ? estrDup(pTempEString) : strdup(pTempEString));
			}
		}
		else
		{
			eaPush(pppOutStrings, (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_ESTRINGS) ? estrDup(pTempEString) : strdup(pTempEString));
		}
	}

	estrDestroy(&pTempEString);
}


static const char *FindNextSeparatorRespectingQuotes(const char *pInString, const char *pSeparators)
{
	bool bInQuotes = false;
	while (*pInString)
	{
		if (*pInString == '"')
		{
			bInQuotes = !bInQuotes;
		}
		else
		{
			if (!bInQuotes)
			{	
				if (strchr(pSeparators, *pInString))
				{
					return pInString;
				}
			}
		}

		pInString++;
	}

	return pInString;
}

void DivideString(const char *pInString, char *pSeparators, char ***pppOutStrings, enumDivideStringPostProcessFlags ePostProcessFlags)
{
	bool bSkipNext = false;
	while (pInString)
	{
		const char *pNextSep;

		if (ePostProcessFlags & DIVIDESTRING_RESPECT_SIMPLE_QUOTES)
		{
			pNextSep = FindNextSeparatorRespectingQuotes(pInString, pSeparators);
		}
		else
		{
			pNextSep = pInString + strcspn(pInString, pSeparators);
		}

		if (*pNextSep)
		{
			int iNewLen = pNextSep - pInString;

		
			if (bSkipNext)
			{
				bSkipNext = false;
			}
			else
			{
				DivideString_Add(pppOutStrings, pInString, iNewLen, ePostProcessFlags);
			}
			
			if (ePostProcessFlags & DIVIDESTRING_POSTPROCESS_WINDOWSNEWLINES && pNextSep[0] == '\r' && pNextSep[1] == '\n')
			{
				bSkipNext = true;
			}


			pInString = pNextSep + 1;
		}
		else
		{
			if (!bSkipNext)
			{
				DivideString_Add(pppOutStrings, pInString, (int)strlen(pInString), ePostProcessFlags);
			}
	
			pInString = NULL;
		}
	}
}


#define ISOKFORALNUMTOKEN(c) ((c) == '_' || isalnum(c) || (c) && pExtraLegalIdentChars && strchr(pExtraLegalIdentChars, (c)))

void ExtractAlphaNumTokensFromStringEx(char *pInString, char ***pppOutStrings, char *pExtraLegalIdentChars)
{
	char *pReadHead = pInString - 1;
	bool bInsideToken = false;
	char *pCurTokenStart = NULL;

	if (!pInString)
	{
		return;
	}

	do
	{
		pReadHead++;

		if (bInsideToken)
		{
			if (ISOKFORALNUMTOKEN(*pReadHead))
			{
				//do nothing
			}
			else
			{
				char *pNewTok = malloc(pReadHead - pCurTokenStart + 1);
				memcpy(pNewTok, pCurTokenStart, pReadHead - pCurTokenStart);
				pNewTok[pReadHead - pCurTokenStart] = 0;
				bInsideToken = false;
				eaPush(pppOutStrings, pNewTok);
			}
		}
		else
		{
			if (ISOKFORALNUMTOKEN(*pReadHead))
			{
				bInsideToken = true;
				pCurTokenStart = pReadHead;
			}
		}
	}
	while (*pReadHead);
}



const char *GetCommandLineWithoutExecutable(void)
{
#if _PS3
    extern char *cl_tail;
    return cl_tail;
#else
	const char *pRetVal = GetCommandLine();

	//first, skip all leading whitespace
	while (IS_WHITESPACE(*pRetVal))
	{
		pRetVal++;
	}

	//if command line has " as its first character, then the executable is "asdfasdf.exe", perhaps with spaces, so skip until the next quote

	if (*pRetVal == '"')
	{
		char *pNextQuote = strchr(pRetVal + 1, '"');

		if (pNextQuote)
		{	
			return pNextQuote + 1;
		}
		else
		{
			return "";
		}
	}
	else
	{
		char *pFirstSpace = strchr(pRetVal, ' ');

		if (pFirstSpace)
		{
			while (*pFirstSpace == ' ')
			{
				pFirstSpace++;
			}

			return pFirstSpace;
		}
	}

	return "";
#endif
}



//given an earray of strings, finds the longest shared prefix they have, puts it in an EString
void FindSharedPrefix(char **ppOutEString, char **ppInStrings)
{
	int iSize = eaSize(&ppInStrings);
	int i, j;

	assert(iSize);

	estrCopy2(ppOutEString, ppInStrings[0]);

	for (i=1; i < iSize; i++)
	{
		int iCurLen = estrLength(ppOutEString);

		for (j=0; j < iCurLen; j++)
		{
			if ((*ppOutEString)[j] != ppInStrings[i][j])
			{
				estrSetSize(ppOutEString, j);
				break;
			}
		}
	}
}

#if !PLATFORM_CONSOLE

typedef struct QueryableProcessHandle
{
	HANDLE hHandle;
	FileWrapper *pOutFile;
	HANDLE pipe_stdin_read, pipe_stdin_write;
	HANDLE pipe_stdout_read, pipe_stdout_write;
} QueryableProcessHandle;

static QueryableProcessHandle *spCurHandleForReadThread = NULL;

static S32 __stdcall QueraybleProcessFileWriteThread(QueryableProcessHandle *pHandle)
{
	
	char buf[17];

	for(;;)
	{
		BOOL success;
		DWORD read;
		
		success = ReadFile(pHandle->pipe_stdout_read, buf, ARRAY_SIZE_CHECKED(buf) - 1, &read, NULL);
			
		if(!success || read == 0) break;

		buf[read] = 0;
		printf("%s", buf);
		fprintf(pHandle->pOutFile, "%s", buf);
	}

	CloseHandle(pHandle->pipe_stdout_read);


	fclose(pHandle->pOutFile);
	pHandle->pOutFile = NULL;
	pHandle = NULL;
	
	return 0;
}

QueryableProcessHandle *StartQueryableProcess_WithFullDebugFixup(const char *pCmdLine, const char *pWorkingDir, bool bRunInMyConsole, bool minimized, bool hidden,
	char *pFileNameForOutput)
{
	char *pLocalCmdString = NULL;
	QueryableProcessHandle *pRetVal;

	estrStackCreate(&pLocalCmdString);
	estrCopy2(&pLocalCmdString, pCmdLine);
	fixupEntireCommandStringForFullDebugFixup(&pLocalCmdString);

	
	pRetVal = StartQueryableProcess(pLocalCmdString, pWorkingDir, bRunInMyConsole, minimized, hidden, pFileNameForOutput);

	estrDestroy(&pLocalCmdString);

	return pRetVal;

}


//returns non-zero
QueryableProcessHandle *StartQueryableProcessEx(const char *pCmdLine, const char *pWorkingDir, bool bRunInMyConsole, 
	bool minimized, bool hidden, char *pFileName, int *piOutPID)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	SECURITY_ATTRIBUTES saAttr;
	char *pStringToUse = NULL;
	QueryableProcessHandle *pHandle;

	PERFINFO_AUTO_START_FUNC();

	pHandle = calloc(sizeof(QueryableProcessHandle), 1);

	ZeroStruct(&si);
	si.cb = sizeof(si);
	if(minimized || hidden)
	{
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = (hidden ? SW_HIDE : SW_MINIMIZE);
	}
	ZeroStruct(&pi);

	estrStackCreate(&pStringToUse);

	if (piOutPID)
	{
		estrCopy2(&pStringToUse, pCmdLine);
	}
	else
	{
		estrPrintf(&pStringToUse, "cmd.exe /c %s", pCmdLine);
	}

	if (pFileName)
	{
		mkdirtree_const(pFileName);
		pHandle->pOutFile = fopen(pFileName, "wb");
		if(!pHandle->pOutFile)
		{
			free(pHandle);
			PERFINFO_AUTO_STOP_FUNC();
			return NULL;
		}

		saAttr.bInheritHandle = TRUE;
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.lpSecurityDescriptor = NULL;
		CreatePipe(&pHandle->pipe_stdin_read, &pHandle->pipe_stdin_write, &saAttr, 0);
		CreatePipe(&pHandle->pipe_stdout_read, &pHandle->pipe_stdout_write, &saAttr, 0);
		si.hStdOutput = pHandle->pipe_stdout_write;
		si.hStdError = pHandle->pipe_stdout_write;
		si.hStdInput = pHandle->pipe_stdin_read;

		si.dwFlags |= STARTF_USESTDHANDLES;
	}

	if (!CreateProcess_UTF8(NULL, pStringToUse,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		pFileName ? TRUE : FALSE, // let the child inherit handles, or not
		(bRunInMyConsole ? 0 : CREATE_NEW_CONSOLE) | CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		pWorkingDir, 
		&si,
		&pi))
	{
		if (pFileName)
		{
			CloseHandle(pHandle->pipe_stdin_write);
			CloseHandle(pHandle->pipe_stdout_write);
			CloseHandle(pHandle->pipe_stdin_read);
			CloseHandle(pHandle->pipe_stdout_read);
		}

		estrDestroy(&pStringToUse);
		free(pHandle);

		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	} 
	else 
	{
		CloseHandle(pHandle->pipe_stdin_write);
		CloseHandle(pHandle->pipe_stdout_write);
		CloseHandle(pHandle->pipe_stdin_read);
		pHandle->hHandle = pi.hProcess;
		estrDestroy(&pStringToUse);

		if (piOutPID)
		{
			*piOutPID = pi.dwProcessId;
		}
	
		CloseHandle(pi.hThread);
			
		if (pFileName)
		{
			DWORD thread_id;	
			_beginthreadex( NULL,				// no security attributes
				64000,			// stack size
				&QueraybleProcessFileWriteThread,	// thread function
				pHandle,		// argument to thread function
				0,					// use default creation flags
				&thread_id);	
		}

		PERFINFO_AUTO_STOP_FUNC();
		return pHandle;
	}
}

bool QueryableProcessComplete(QueryableProcessHandle **ppHandle, int *pReturnVal)
{
	int iCode;

	PERFINFO_AUTO_START_FUNC();

	if (!ppHandle || !*ppHandle)
	{
		//not at all clear what if anything to set returnval to here.... if someone is checking it they're doing
		//something wrong, so I will set it to a presumed failure value
		if (pReturnVal)
		{
			*pReturnVal = -1;
		}

		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	assertmsg((*ppHandle)->hHandle != 0, "Can't query nonexistent");

	if (GetExitCodeProcess((*ppHandle)->hHandle, &iCode))
	{
		if (iCode == STILL_ACTIVE)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		else
		{
			//if we have a background thread writing to the file, it should complete quickly
			while((*ppHandle)->pOutFile)
			{
				Sleep(1);
			}

			CloseHandle((*ppHandle)->hHandle);
			free(*ppHandle);
			*ppHandle = NULL;

			if (pReturnVal)
			{
				*pReturnVal = iCode;
			}
			PERFINFO_AUTO_STOP_FUNC();
			return true;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return false;
}




void KillQueryableProcess(QueryableProcessHandle **ppHandle)
{
	if (!ppHandle || !*ppHandle)
	{
		return;
	}

	if ((*ppHandle)->hHandle)
	{
		int iPid = GetProcessId((*ppHandle)->hHandle);

		if (iPid)
		{
			char *pKillString = NULL;

			estrStackCreate(&pKillString);
			estrPrintf(&pKillString, "TaskKill /PID %d /F /T > nul 2> nul", iPid);
			system(pKillString);
			estrDestroy(&pKillString);
		}

		while((*ppHandle)->pOutFile)
		{
			Sleep(1);
		}

		CloseHandle((*ppHandle)->hHandle);
		free(*ppHandle);
		*ppHandle = NULL;
	}
}

int system_w_output(char *cmd, char **outstr)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	HANDLE pipe_stdin_read, pipe_stdin_write;
	HANDLE pipe_stdout_read, pipe_stdout_write;
	SECURITY_ATTRIBUTES saAttr;
	char buf[1024];
	int returnval;
	size_t total=0;

	saAttr.bInheritHandle = TRUE;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	CreatePipe(&pipe_stdin_read, &pipe_stdin_write, &saAttr, 0);
	CreatePipe(&pipe_stdout_read, &pipe_stdout_write, &saAttr, 0);

	ZeroStruct(&si);
	si.cb = sizeof(si);
	si.dwFlags |= STARTF_USESHOWWINDOW;
	si.dwFlags |= STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = pipe_stdin_read;
	si.hStdOutput = pipe_stdout_write;
	si.hStdError = pipe_stdout_write;
	ZeroStruct(&pi);

	if (!CreateProcess_UTF8(NULL, cmd,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		TRUE, // let this child inherit handles
		/*CREATE_NEW_CONSOLE |*/ CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		NULL, // inherit current directory
		&si,
		&pi))
	{
		return -1;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pipe_stdin_write);
	CloseHandle(pipe_stdout_write);

	for(;;)
	{
		BOOL success;
		DWORD read;
		success = ReadFile(pipe_stdout_read, buf, ARRAY_SIZE_CHECKED(buf), &read, NULL);
		if(!success || read == 0) break;
		estrForceSize(outstr, (int)total+read);
		memcpy((*outstr)+total, buf, read);
		total += read;
	}

	do
	{
		GetExitCodeProcess(pi.hProcess, &returnval);
	} while(returnval == STILL_ACTIVE);
	return returnval;
}

static ULONG PipeSerialNumber;

static
BOOL
APIENTRY
CreatePipeEx(
	OUT LPHANDLE lpReadPipe,
	OUT LPHANDLE lpWritePipe,
	IN LPSECURITY_ATTRIBUTES lpPipeAttributes,
	IN DWORD nSize,
	DWORD dwReadMode,
	DWORD dwWriteMode
)
/*++

Routine Description:

    The CreatePipeEx API is used to create an anonymous pipe I/O device.
    Unlike CreatePipe FILE_FLAG_OVERLAPPED may be specified for one or
    both handles.
    Two handles to the device are created.  One handle is opened for
    reading and the other is opened for writing.  These handles may be
    used in subsequent calls to ReadFile and WriteFile to transmit data
    through the pipe.

Arguments:

    lpReadPipe - Returns a handle to the read side of the pipe.  Data
        may be read from the pipe by specifying this handle value in a
        subsequent call to ReadFile.

    lpWritePipe - Returns a handle to the write side of the pipe.  Data
        may be written to the pipe by specifying this handle value in a
        subsequent call to WriteFile.

    lpPipeAttributes - An optional parameter that may be used to specify
        the attributes of the new pipe.  If the parameter is not
        specified, then the pipe is created without a security
        descriptor, and the resulting handles are not inherited on
        process creation.  Otherwise, the optional security attributes
        are used on the pipe, and the inherit handles flag effects both
        pipe handles.

    nSize - Supplies the requested buffer size for the pipe.  This is
        only a suggestion and is used by the operating system to
        calculate an appropriate buffering mechanism.  A value of zero
        indicates that the system is to choose the default buffering
        scheme.
	dwReadMode - Specify FILE_FLAG_OVERLAPPED for asynchronous I/O; 0 otherwise.
	dwWriteMode - Specify FILE_FLAG_OVERLAPPED for asynchronous I/O; 0 otherwise.

Return Value:

    TRUE - The operation was successful.

    FALSE/NULL - The operation failed. Extended error status is available
        using GetLastError.

--*/
{
    HANDLE ReadPipeHandle, WritePipeHandle;
    DWORD dwError;
    UCHAR PipeNameBuffer[MAX_PATH];

    //
    // Only one valid OpenMode flag - FILE_FLAG_OVERLAPPED
    //

    if((dwReadMode | dwWriteMode) & (~FILE_FLAG_OVERLAPPED))
	{
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if(nSize == 0)
		nSize = 4096;

    sprintf(PipeNameBuffer,
		"\\\\.\\Pipe\\RemoteExeAnon.%08x.%08x",
		GetCurrentProcessId(),
		PipeSerialNumber++
	);

    ReadPipeHandle = CreateNamedPipeA(
		PipeNameBuffer,
		PIPE_ACCESS_INBOUND | dwReadMode,
		PIPE_TYPE_BYTE | PIPE_WAIT,
		1,					// Number of pipes
		nSize,				// Out buffer size
		nSize,				// In buffer size
		0,					// Timeout in ms, defaults to 50 ms when 0.
		lpPipeAttributes
	);
    if(INVALID_HANDLE_VALUE == ReadPipeHandle)
	{
		dwError = GetLastError();
		SetLastError(dwError);
		return FALSE;
	}

    WritePipeHandle = CreateFileA(
        PipeNameBuffer,
        GENERIC_WRITE,
        0,										// No sharing
        lpPipeAttributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | dwWriteMode,
        NULL									// Template file
	);
    if(INVALID_HANDLE_VALUE == WritePipeHandle)
	{
        dwError = GetLastError();
        CloseHandle(ReadPipeHandle);
        SetLastError(dwError);
        return FALSE;
    }

    *lpReadPipe = ReadPipeHandle;
    *lpWritePipe = WritePipeHandle;

    return TRUE;
}

int system_w_output_and_timeout(char *cmd, char **outstr, int iTimeout)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	HANDLE pipe_stdin_read, pipe_stdin_write;
	HANDLE pipe_stdout_read, pipe_stdout_write;
	OVERLAPPED ol;
	SECURITY_ATTRIBUTES saAttr;
	char buf[1024];
	int returnval;
	S64 iStartTime;
	size_t total=0;

	int bytes_read = 0;
	DWORD dwError  = 0;
	BOOL bResult   = FALSE;
	BOOL bContinue = TRUE;

	saAttr.bInheritHandle = TRUE;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	CreatePipeEx(&pipe_stdin_read, &pipe_stdin_write, &saAttr, 0, FILE_FLAG_OVERLAPPED, 0);
	CreatePipeEx(&pipe_stdout_read, &pipe_stdout_write, &saAttr, 0, FILE_FLAG_OVERLAPPED, 0);

	ZeroStruct(&si);
	si.cb = sizeof(si);
	si.dwFlags |= STARTF_USESHOWWINDOW;
	si.dwFlags |= STARTF_USESTDHANDLES;
	si.wShowWindow = SW_SHOW;
	si.hStdInput = pipe_stdin_read;
	si.hStdOutput = pipe_stdout_write;
	si.hStdError = pipe_stdout_write;
	ZeroStruct(&pi);

	if(!CreateProcess_UTF8(NULL, cmd,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		TRUE, // let this child inherit handles
		/*CREATE_NEW_CONSOLE |*/ CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		NULL, // inherit current directory
		&si,
		&pi))
	{
		return -1;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pipe_stdin_write);
	CloseHandle(pipe_stdout_write);
	CloseHandle(pipe_stdin_read);

	iStartTime = timeGetTime();

	ZeroMemory(&ol, sizeof(OVERLAPPED));
	ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	ANALYSIS_ASSUME(ol.hEvent);

	while(bContinue)
	{
		// Default to ending the loop.
		bContinue = FALSE;

		// Attempt an asynchronous read operation.
		bResult = ReadFile(pipe_stdout_read, buf, ARRAY_SIZE_CHECKED(buf), &bytes_read, &ol);
		dwError = GetLastError();
		// Check for a problem or pending operation.
		if(!bResult)
		{ 
			switch(dwError) 
			{
				case ERROR_BROKEN_PIPE:
				case ERROR_HANDLE_EOF:
				{
					// Async EOF did not trigger. This is unexpected for a process output pipe.
					// Not really an error condition though, so we are done.
					break;
				}
				case ERROR_IO_PENDING: 
				{ 
					BOOL bPending = TRUE;

					// Loop until the I/O is complete, that is: the overlapped event is signaled.
					while(bPending)
					{
						bPending = FALSE;

						// Pending asynchronous I/O, do something else
						// and re-check overlapped structure.

						if(timeGetTime() - iStartTime > iTimeout * 1000)
						{
							// We do not want to terminate the process at this point.
							// This was a long discussion between Aaron L and Alex W and others.
							CloseHandle(pipe_stdout_read);
							CloseHandle(pi.hProcess);
							CloseHandle(ol.hEvent);
							return -1;
						}

						// Check the result of the asynchronous read without waiting.
						bResult = GetOverlappedResult(pipe_stdout_read, &ol, &bytes_read, FALSE);
						if(!bResult)
						{ 
							switch(dwError = GetLastError()) 
							{ 
								case ERROR_BROKEN_PIPE:
								case ERROR_HANDLE_EOF:
								{ 
									// Handle an end of file.
									// The default behavior will be to break out of the loop.
									break;
								} 
								case ERROR_IO_INCOMPLETE:
								{
									// Operation is still pending, allow while loop
									// to loop again after printing a little progress.
									bPending = TRUE;
									bContinue = TRUE;
									break;
								}
								default:
								{
									// Some other error occurred
									// We do not want to terminate the process at this point.
									// This was a long discussion between Aaron L and Alex W and others.
									CloseHandle(pipe_stdout_read);
									CloseHandle(pi.hProcess);
									CloseHandle(ol.hEvent);
									return -1;
								}
							}
						} 
						else
						{
							// ReadFile operation completed

							// Manual-reset event should be reset since it is now signaled.
							ResetEvent(ol.hEvent);
						}
					}
					break;
				}

				default:
				{
					// Some other error occurred
					// We do not want to terminate the process at this point.
					// This was a long discussion between Aaron L and Alex W and others.
					CloseHandle(pipe_stdout_read);
					CloseHandle(pi.hProcess);
					CloseHandle(ol.hEvent);
					return -1;
				}
			}
		}
		else
		{
			// EOF did not trigger. This is unexpected for a process output pipe.
			// Not really an error condition though, so carry on.
		}

		// The following operation assumes the output is not extremely large, otherwise 
		// logic would need to be included to adequately account for very large
		// output and manipulate the OffsetHigh member of the OVERLAPPED structure.
		ol.Offset += bytes_read;

		estrForceSize(outstr, (int)total + bytes_read);
		memcpy((*outstr) + total, buf, bytes_read);
		total += bytes_read;

		if(timeGetTime() - iStartTime > iTimeout * 1000)
		{
			// We do not want to terminate the process at this point.
			// This was a long discussion between Aaron L and Alex W and others.
			CloseHandle(pipe_stdout_read);
			CloseHandle(pi.hProcess);
			CloseHandle(ol.hEvent);
			return -1;
		}
	}

	do
	{
		GetExitCodeProcess(pi.hProcess, &returnval);
	} while(returnval == STILL_ACTIVE);

	CloseHandle(pipe_stdout_read);
	CloseHandle(ol.hEvent);

	return returnval;
}

int system_w_input(char *cmd, char *input, size_t intput_len, bool detach, bool hidden)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	HANDLE pipe_stdin_read, pipe_stdin_write;
	HANDLE pipe_stdout_read, pipe_stdout_write;
	SECURITY_ATTRIBUTES saAttr;
	//char buf[1024];
	int returnval;
	size_t total = intput_len;

	saAttr.bInheritHandle = TRUE;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.lpSecurityDescriptor = NULL;
	CreatePipe(&pipe_stdin_read, &pipe_stdin_write, &saAttr, 0);
	CreatePipe(&pipe_stdout_read, &pipe_stdout_write, &saAttr, 0);

	ZeroStruct(&si);
	si.cb = sizeof(si);
	if(hidden)
	{
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
	}
	si.dwFlags |= STARTF_USESTDHANDLES;
	si.hStdInput = pipe_stdin_read;
	si.hStdOutput = pipe_stdout_write;
	si.hStdError = pipe_stdout_write;
	ZeroStruct(&pi);

	if (!CreateProcess_UTF8(NULL, cmd,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		TRUE, // let this child inherit handles
		/*CREATE_NEW_CONSOLE |*/ CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		NULL, // inherit current directory
		&si,
		&pi))
	{
		return -1;
	}
	CloseHandle(pi.hThread);
	//CloseHandle(pipe_stdin_write);
	CloseHandle(pipe_stdout_write);
	CloseHandle(pipe_stdout_read);

	while(total > 0)
	{
		BOOL success;
		DWORD written;
		success = WriteFile(pipe_stdin_write, input, (DWORD)total, &written, NULL);
		assert(success);
		total -= written;
		input += written;
	}
	CloseHandle(pipe_stdin_write);
	CloseHandle(pipe_stdin_read);

	if(detach)
		return STILL_ACTIVE;

	do
	{
		GetExitCodeProcess(pi.hProcess, &returnval);
	} while(returnval == STILL_ACTIVE);
	return returnval;
}


int system_w_workingDir(char *cmd, char *workingDir)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	char *pCmdToUse = NULL;
	char *pExeName = NULL;

	//special case... if cmd is a relative path to an .exe file, and that file exists in workingDir, then
	//fix up cmd to be an absolute path. Otherwise, for reasons unclear to me, 
	//trying to launch "foo.exe" from "c:\foo" won't work, but launching "c:\foo\foo.exe" from "c:\foo" will work.
	estrStackCreate(&pCmdToUse);
	estrCopy2(&pCmdToUse, cmd);
	estrTrimLeadingAndTrailingWhitespace(&pCmdToUse);

	if (pCmdToUse[1] != ':')
	{
		char *pFirstSpace = strchr(pCmdToUse, ' ');
		estrStackCreate(&pExeName);

		if (pFirstSpace)
		{
			*pFirstSpace = 0;
		}

		estrPrintf(&pExeName, "%s\\%s", workingDir, pCmdToUse);

		if (pFirstSpace)
		{
			*pFirstSpace = ' ';
		}

		if (fileExists(pExeName))
		{
			estrInsertf(&pCmdToUse, 0, "%s\\", workingDir);
		}
	}


	ZeroStruct(&si);
	si.cb = sizeof(si);

	ZeroStruct(&pi);

	si.dwFlags |= STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_MINIMIZE;

	if (!CreateProcess_UTF8(NULL, pCmdToUse,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		FALSE, // let this child inherit handles
		CREATE_NEW_PROCESS_GROUP,
		NULL, // inherit environment
		workingDir, // inherit current directory
		&si,
		&pi))
	{
		estrDestroy(&pCmdToUse);
		estrDestroy(&pExeName);
		return -1;
	}

	estrDestroy(&pCmdToUse);
	estrDestroy(&pExeName);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return 0;
}

// Create a pipe buffer.
int pipe_buffer_create(const char *buffer, size_t buffer_size)
{
	SECURITY_ATTRIBUTES sa;
	HANDLE pipe_in, pipe_out;
	BOOL success;
	DWORD len;

	// Create security attributes.
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;		// Same as parent process
	sa.bInheritHandle = TRUE;			// Allow inherit

	// Create pipe.
	success = CreatePipe(&pipe_out, &pipe_in, &sa, (DWORD)buffer_size);
	if (!success)
		return 0;

	// Copy memory into pipe kernel buffer.
	success = WriteFile(pipe_in, buffer, (DWORD)buffer_size, &len, NULL);
	if (!success)
	{
		CloseHandle(pipe_out);
		CloseHandle(pipe_in);
		return 0;
	}

	// Close pipe input.
	success = CloseHandle(pipe_in);
	devassert(success);

	return (int)pipe_out;
}

// Read from a pipe buffer.
size_t pipe_buffer_read(char *buffer, size_t buffer_size, int pipe)
{
	HANDLE handle = (HANDLE)pipe;
	bool success;
	DWORD len;

	// Read from pipe.
	success = ReadFile(handle, buffer, (DWORD)buffer_size, &len, NULL);
	if (!success)
	{
		CloseHandle(handle);
		return 0;
	}
	buffer_size = (size_t)len;

	// Close handle.
	success = CloseHandle(handle);
	devassert(success);

	return len;
}

// Close a pipe buffer.
void pipe_buffer_cleanup(int pipe)
{
	bool success = CloseHandle((HANDLE)pipe);
	devassert(success);
}

#endif

#define NUM_ASSERT_COMPLETES_TRACKING_STRUCTS 4

typedef struct
{
	int iSecsRemaining;
	char *pCommand;
	char *pFile;
	int iLineNum;
	voidVoidFunc callback;
	bool bInUse;
} AssertCompletesTrackingStruct;

AssertCompletesTrackingStruct gAssertCompletesStructs[NUM_ASSERT_COMPLETES_TRACKING_STRUCTS] = {0};

CRITICAL_SECTION AssertCompletesCS;

AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:AssertCompletesThread", BUDGET_EngineMisc););

// Stack overflow exception information.
PEXCEPTION_POINTERS stackOverflowExceptionPointers;

// TIB of thread experiencing stack overflow.
// Note: Instead of doing this, we could call GetThreadSelectorEntry() on the fs register from the context.
// But that seems like it might be less reliable in a pinch.
void *stackOverflowTib;

// Bounding stack pointer value.
void *stackOverflowBoundingFramePointer;

static void stackOverflowCheck(void)
{
	// If you get this, go to Debug | Windows | Threads, and find a thread
	//  in either the function named stackoverflow() or an exception handler,
	//  and that is probably your culprit (more often than not it's simply
	//  the main thread).
	assertExcept(EXCEPTION_STACK_OVERFLOW, stackOverflowExceptionPointers, stackOverflowTib, stackOverflowBoundingFramePointer);
}

//this thread runs and checks the status of ongoing ASSERT_COMPLETES calls
static DWORD WINAPI AssertCompletesThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
	while (1)
	{
		int i;
		
		autoTimerThreadFrameBegin("assertCompletes");
		
		Sleep(1000);

		if(stackOverflowExceptionPointers)
			stackOverflowCheck(); // sub-function to get better ErrorTracker colating

		EnterCriticalSection(&AssertCompletesCS);
		for (i=0; i < NUM_ASSERT_COMPLETES_TRACKING_STRUCTS; i++)
		{
			if (gAssertCompletesStructs[i].bInUse)
			{
				if (--gAssertCompletesStructs[i].iSecsRemaining < 0)
				{
					if (gAssertCompletesStructs[i].callback)
					{
						gAssertCompletesStructs[i].callback();
					} else {
						assertmsgf(0, "ASSERT_COMPLETES failed for command %s, %s(%d)", gAssertCompletesStructs[i].pCommand,
							gAssertCompletesStructs[i].pFile, gAssertCompletesStructs[i].iLineNum);
					}
				}
			}
		}
		LeaveCriticalSection(&AssertCompletesCS);

		autoTimerThreadFrameEnd();
	}
	EXCEPTION_HANDLER_END
}

//returns which "slot" it is using
int AssertCompletes_BeginTiming(int iNumSecs, char *pCommand, voidVoidFunc callback, char *pFile, int iLine)
{
	int i;

	EnterCriticalSection(&AssertCompletesCS);
	for (i=0; i < NUM_ASSERT_COMPLETES_TRACKING_STRUCTS; i++)
	{
		if (gAssertCompletesStructs[i].bInUse)
			continue;

		gAssertCompletesStructs[i].bInUse = true;
		gAssertCompletesStructs[i].iSecsRemaining = iNumSecs;
		gAssertCompletesStructs[i].pCommand = pCommand;
		gAssertCompletesStructs[i].pFile = pFile;
		gAssertCompletesStructs[i].iLineNum = iLine;
		gAssertCompletesStructs[i].callback = callback;

		LeaveCriticalSection(&AssertCompletesCS);
		return i;
	}
	LeaveCriticalSection(&AssertCompletesCS);

	assertmsg(0, "All ASSERT_RETURNS structs occupied");

	return -1;
}

void AssertCompletes_StopTiming(int iSlotNum)
{
	EnterCriticalSection(&AssertCompletesCS);
	gAssertCompletesStructs[iSlotNum].bInUse = false;
	LeaveCriticalSection(&AssertCompletesCS);
}

AUTO_RUN;
void AssertCompletes_InitSystem(void)
{
	InitializeCriticalSection(&AssertCompletesCS);
	tmCreateThread(AssertCompletesThread, NULL);
}


#if !PLATFORM_CONSOLE
int x_system(const char *pCmdLine)
{
	// The specific case this is intending to catch is:
	//
	// 1. The system() call pops up a console window.  This
	// appears to be tied to using stream redirects (<, >, |).
	//
	// 2. The console window steals the focus from our client.
	//
	// 3. Losing focus causes us to minimize our window, and possible
	// the D3D device gets lost.
	//
	// Be cool, don't call system().
	if( GetAppGlobalType() == GLOBALTYPE_CLIENT && (isProductionMode() || isProductionEditMode() )) {
		const char* errStr = "Do not call system() on the GameClient, it may pop up a console window, which will screw up D3D.";
		if( isProductionMode() ) {
			Errorf( "%s", errStr );
		} else {
			FatalErrorf( "%s", errStr );
		}
	}
	
	return system_UTF8(pCmdLine);
}

int system_w_timeout(char *pCmdLine, char *pWorkingDir, U32 iFailureTime)
{
	U32 iStartTime;
	int iRetVal = 0;
	QueryableProcessHandle *pHandle;




	iStartTime = timeSecondsSince2000();
	
	pHandle = StartQueryableProcess(pCmdLine, pWorkingDir, true, false, false, NULL);

	if (!pHandle)
	{
		return -1;
	}


	while (1)
	{
		if (QueryableProcessComplete(&pHandle, &iRetVal))
		{
			break;
		}

		if (timeSecondsSince2000() - iStartTime > iFailureTime)
		{
			KillQueryableProcess(&pHandle);
			iRetVal = -1;
			break;
		}

		Sleep(10);
	}


	return iRetVal;
}
#endif

	

// *********************************************************************************
//  hexString conversions
// *********************************************************************************

unsigned char* hexStrToBinStr(const char* hexString, int strLength_UNUSED)
{
	char* binStr = NULL;
	int binStrSize = 0;
	int i, len;

	// limit reading to the actual length of the string
	len = (int)strlen(hexString);

	// Make sure the output buffer is large enough.
	binStrSize = ((len + 1) / 2) + 1;
	binStr = malloc(binStrSize);
	memset(binStr, 0, binStrSize); // init

	for(i = 0; i < len / 2; i++)
	{
		int value;
		sscanf(&hexString[i*2], "%02x", &value);
		binStr[i] = value;
	}

	if(len%2)
	{
		int value;
		char tmp[3];
		tmp[0] = hexString[len-1];
		tmp[1] = '0';
		tmp[2] = 0;
		sscanf(tmp, "%02x", &value);
		binStr[i++] = value;
	}
	binStr[i] = 0;

	return binStr;
}

__forceinline static char getHex(int value)
{
	if (value < 10)
		return value + '0';
	return value - 10 + 'a';
}

char* binStrToHexStr(const unsigned char* binArray, int arrayLength)
{
	char* hexStr = NULL;
	int hexStrSize = 0;
	int i,len=-1;

	// Make sure the output buffer is large enough.
	hexStr = malloc((arrayLength * 2) + 1);
	hexStrSize = (arrayLength * 2) + 1;
	len = arrayLength;
	for(i=0;i<len;i++)
	{
		// make sure we don't allow an odd-length string to be created
		hexStr[i*2+0] = getHex(binArray[i] >> 4);
		hexStr[i*2+1] = getHex(binArray[i] & 15);
	}
	hexStr[i*2] = '\0';
	return hexStr;
}
	

intptr_t GET_INTPTR_FROM_FLOAT(float f) 
{
	intptr_t n;

	if (f == 0.0f)
	{
		return 0;
	}

	//check if the float is already an int
	n = (intptr_t)f;
	if ((float)n == f && n >= -1024 && n <= 1024)
	{
		return n;
	}

	(*((float*)(&(n)))) = f;

	assert(n < -1024 || n > 1024);

	return n;
}

S32 atomicInitIsFirst(volatile S32* init){
	if(*init & ATOMIC_INIT_DONE_BIT){
		return 0;
	}
	
	if((InterlockedIncrement(init) & ~ATOMIC_INIT_DONE_BIT) == 1){
		return 1;
	}else{
		InterlockedDecrement(init);
		while(!(*init & ATOMIC_INIT_DONE_BIT)){
			Sleep(1);
		}
		return 0;
	}
}

void atomicInitSetDone(S32* init){
	InterlockedExchangeAdd(init, ATOMIC_INIT_DONE_BIT);
}

void freeWrapper(void *data)
{
	free(data);
}

bool nextPermutation(int *list, int N) 
{		
	int min, mo, m;
	int j;
	// find turnover point
	for (j=N-2; j>=0 && list[j]>list[j+1]; j--);
	if (j<0)
		return false;
	// find lowest to right
	min=j+1;
	mo = list[min];
	for (m=j+2; m<N; m++) {
		if ((list[m]<mo) && (list[m]>list[j])) {
			min = m;
			mo = list[m];
		}
	}
	// swap
	list[min]=list[j];
	list[j] = mo;
	qsort(list + j+1, N - (j+1), sizeof(list[0]), intCmp);
	return true;
}

void indexPermutation(int iIndex, int *pList, int iSize)
{
	int i;
	devassert(iSize > 0);
	if (iSize == 1) 
	{
		pList[0] = 0;
		return;
	}
	pList[0] = iIndex % iSize;
	indexPermutation(iIndex / iSize, pList + 1, iSize - 1);

	for (i = 1; i < iSize; i++) 
	{
		if (pList[i] >= pList[0])
		{
			pList[i]++;
		}
	}
}

AUTO_STRUCT;
typedef struct IPToCountryElement
{
	U32 iMinIP;
	U32 iMaxIP;
	char *pCountry; AST(POOL_STRING)
} IPToCountryElement;

static IPToCountryElement **sppIPToCountryList = NULL;


static __forceinline bool MATCHES(U32 iIP, IPToCountryElement *pElement)
{
	return (iIP >= pElement->iMinIP && iIP <= pElement->iMaxIP);
}

//calling this function loads in 4 megabytes of ip directory data, which then hangs around forever
AUTO_COMMAND;
char *ipToCountryName(U32 iIP)
{
	int iCurFirst;
	int iCurLast;

	if (!iIP)
	{
		return "IP_0";
	}

	iIP = endianSwapU32(iIP);

	if (!sppIPToCountryList)
	{
		char *pBuf = fileAlloc("c:/Night/tools/bin/ip-to-country.csv", NULL);
		char **ppLines = NULL;
		int iLineNum;
		
		if (!pBuf)
		{
			return "UNKNOWN";
		}

		DivideString(pBuf, "\n", &ppLines, 0);
		free(pBuf);

		for (iLineNum = 0; iLineNum < eaSize(&ppLines); iLineNum++)
		{
			char **ppWords = NULL;
			DivideString(ppLines[iLineNum], ",\"", &ppWords, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
			
			if (eaSize(&ppWords) == 5)
			{
				bool bFailed = false;
				IPToCountryElement *pElement = StructCreate(parse_IPToCountryElement);
				if (!StringToUint(ppWords[0], &pElement->iMinIP) || !StringToUint(ppWords[1], &pElement->iMaxIP))
				{
					StructDestroy(parse_IPToCountryElement, pElement);
				}
				else
				{
					pElement->pCountry = (char*)allocAddString(ppWords[4]);
					
					//assert that the file is pre-sorted
					if (eaSize(&sppIPToCountryList))
					{
						assert(pElement->iMinIP > sppIPToCountryList[eaSize(&sppIPToCountryList) - 1]->iMaxIP);
					}

					
					eaPush(&sppIPToCountryList, pElement);

				}
			}
				
			eaDestroyEx(&ppWords, NULL);
	
		}

		eaDestroyEx(&ppLines, NULL);
	}

	if (!eaSize(&sppIPToCountryList))
	{
		return "UNKNOWN";
	}

	//now do a binary search. This could probably be more elegant, I haven't written a binary search in a long time
	iCurFirst = 0;
	iCurLast = eaSize(&sppIPToCountryList) - 1;

	while (1)
	{
		int iMid;

		if (iCurFirst == iCurLast)
		{
			if (MATCHES(iIP, sppIPToCountryList[iCurFirst]))
			{
				return sppIPToCountryList[iCurFirst]->pCountry;
			}
			else
			{
				return "UNKNOWN";
			}
		}

		if (iCurLast == iCurFirst + 1)
		{
			if (MATCHES(iIP, sppIPToCountryList[iCurFirst]))
			{
				return sppIPToCountryList[iCurFirst]->pCountry;
			}
			else if (MATCHES(iIP, sppIPToCountryList[iCurLast]))
			{
				return sppIPToCountryList[iCurLast]->pCountry;
			}
			else
			{
				return "UNKNOWN";
			}
		}

		iMid = ((U64)iCurFirst + (U64)iCurLast) / 2;

		assert(iMid > iCurFirst && iMid < iCurLast);

		if (MATCHES(iIP, sppIPToCountryList[iMid]))
		{
			return sppIPToCountryList[iMid]->pCountry;
		}

		if (iIP > sppIPToCountryList[iMid]->iMaxIP)
		{
			iCurFirst = iMid + 1;
		}
		else
		{
			iCurLast = iMid - 1;
		}
	}

	return "UNKNOWN";
}

U32 globMovementLogIsEnabled;

static struct {
	GlobalMovementLogFunc			logFunc;
	GlobalMovementLogCameraFunc		logCameraFunc;
	GlobalMovementLogSegmentFunc	logSegmentFunc;
} gml;

void globMovementLogSetFuncs(	GlobalMovementLogFunc logFunc,
								GlobalMovementLogCameraFunc logCameraFunc,
								GlobalMovementLogSegmentFunc logSegmentFunc)
{
	gml.logFunc = logFunc;
	gml.logCameraFunc = logCameraFunc;
	gml.logSegmentFunc = logSegmentFunc;
}

void wrapped_globMovementLog(	FORMAT_STR const char* format,
								...)	
{
	GlobalMovementLogFunc logFunc = gml.logFunc;
	
	if(logFunc){
		VA_START(va, format);
			logFunc(format, va);
		VA_END();
	}
}


void wrapped_globMovementLogCamera(	const char* tags,
									const Mat4 mat)
{
	GlobalMovementLogCameraFunc logCameraFunc = gml.logCameraFunc;
	
	if(logCameraFunc){
		logCameraFunc(tags, mat);
	}
}

void wrapped_globMovementLogSegment(const char* tags,
									const Vec3 p0,
									const Vec3 p1,
									U32 argb)
{
	GlobalMovementLogSegmentFunc logSegmentFunc = gml.logSegmentFunc;
	
	if(logSegmentFunc){
		logSegmentFunc(tags, p0, p1, argb);
	}
}


//simple way to count lots of things and get the n most common

//if this is ever used for something performance-intensive, it should be re-algorithmed to not be sl



PointerCounter *PointerCounter_Create(void)
{
	return calloc(sizeof(PointerCounter), 1);
}

void PointerCounter_AddSome(PointerCounter *pCounter, const void *pPtr, int iCount)
{
	int i;
	int iSize = eaSize(&pCounter->ppResults);
	PointerCounterResult *pResult;

	for (i=0; i < iSize; i++)
	{
		if (pCounter->ppResults[i]->pPtr == pPtr)
		{
			pCounter->ppResults[i]->iCount += iCount;
			return;
		}
	}

	pResult = calloc(sizeof(PointerCounterResult), 1);
	pResult->pPtr = pPtr;
	pResult->iCount = iCount;

	eaPush(&pCounter->ppResults, pResult);
}

int SortCounterResults(const PointerCounterResult **pInfo1, const PointerCounterResult **pInfo2)
{
	if ((*pInfo1)->iCount < (*pInfo2)->iCount)
	{
		return 1;
	}
	else if ((*pInfo1)->iCount > (*pInfo2)->iCount)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

void PointerCounter_GetMostCommon(PointerCounter *pCounter, int iNumToReturn, PointerCounterResult ***pppResults)
{
	int i;
	PointerCounterResult *pResult;
	eaQSort(pCounter->ppResults, SortCounterResults);
	for (i = 0; i < iNumToReturn && i < eaSize(&pCounter->ppResults); i++)
	{
		pResult = malloc(sizeof(PointerCounterResult));
		memcpy(pResult, pCounter->ppResults[i], sizeof(PointerCounterResult));
		eaPush(pppResults, pResult);
	}
}


	
void PointerCounter_Destroy(PointerCounter **ppCounter)
{
	if (!ppCounter || !(*ppCounter))
	{
		return;
	}

	eaDestroyEx(&(*ppCounter)->ppResults, NULL);
	free(*ppCounter);
	*ppCounter = NULL;
}

#define IDSTRING_ALPHABET_SIZE 34


char IDString_IntToChar(int iInt)
{
	if (iInt < 26)
	{
		return 'A' + iInt;
	}

	return '2' + (iInt - 26);
}

int IDString_CharToInt(char c)
{
	if (c == '0')
	{
		c = 'O';
	}

	if (c == '1')
	{
		c = 'I';
	}

	if (c >= 'A' && c <= 'Z')
	{
		return c - 'A';
	}

	if (c >= '2' && c <= '9')
	{
		return c - '2' + 26;
	}

	return -1;
}

static U8 CheckSum(U32 iVal, U8 iExtraHash)
{
	U8 *pBytes = (U8*)&iVal;
	return (pBytes[0] + pBytes[1] + pBytes[2] + pBytes[3]) ^ iExtraHash;
}

void IDString_IntToString(U32 iInt, char outString[ID_STRING_BUFFER_LENGTH], U8 iExtraHash)
{
	U32 iHashed;

	U64 iHashedAndCheckSummed; 
	int i;

	iHashed = reversibleHash(iInt ^ (iExtraHash + (iExtraHash << 8) + (iExtraHash << 16) + (iExtraHash << 24)), false);



	iHashedAndCheckSummed = (U64)iHashed + (((U64)(CheckSum(iHashed, iExtraHash))) << 32);

	outString[ID_STRING_LENGTH] = 0;

	for (i=ID_STRING_LENGTH-1; i >=0; i--)
	{
		outString[i] = IDString_IntToChar(iHashedAndCheckSummed % IDSTRING_ALPHABET_SIZE);
		iHashedAndCheckSummed /= IDSTRING_ALPHABET_SIZE;
	}

	assert(iHashedAndCheckSummed == 0);
}

bool IDString_StringToInt(const char *pString, U32 *pOutInt, U8 iExtraHash)
{
	U64 iHashedAndCheckSummed = 0;
	U32 iHashed;
	U32 iChecksum;
	int i;


	if (!pString)
	{
		return false;
	}

	if (strlen(pString) != ID_STRING_LENGTH)
	{
		return false;
	}

	for (i=0; i < ID_STRING_LENGTH; i++)
	{
		int iDigit = IDString_CharToInt(pString[i]);
		if (iDigit == -1)
		{
			return false;
		}

		iHashedAndCheckSummed *= IDSTRING_ALPHABET_SIZE;
		iHashedAndCheckSummed += iDigit;
	}



	iHashed = iHashedAndCheckSummed;
	iChecksum = iHashedAndCheckSummed >> 32;

	if (iChecksum != CheckSum(iHashed, iExtraHash))
	{
		return false;
	}

	*pOutInt = reversibleHash(iHashed, true) ^ (iExtraHash + (iExtraHash << 8) + (iExtraHash << 16) + (iExtraHash << 24));
	return true;
}
	

/*
AUTO_RUN;
void IDStringTest(void)
{
	U32 i = 0;
	U32 iOutInt;
	char testString[ID_STRING_BUFFER_LENGTH];

	do
	{
		int iExtraHash;
		for (iExtraHash = 0; iExtraHash < 256; iExtraHash++)
		{
			IDString_IntToString(i, testString, iExtraHash);
			assert(IDString_StringToInt(testString, &iOutInt, iExtraHash));
			assert(iOutInt == i);

			if (i < 1000)
			{
				printf("%d(%d): %s\n", i, iExtraHash, testString);
			}

			i++;
		}
	}
	while (i);
}
*/
		
typedef struct GenericLogReceiver {
	GenericLogReceiverMsgHandler	msgHandler;
	void*							userPointer;
} GenericLogReceiver;

void glrCreate(	GenericLogReceiver** rOut,
				GenericLogReceiverMsgHandler msgHandler,
				void* userPointer)
{
	GenericLogReceiver* r;

	if(!rOut){
		return;
	}

	*rOut = r = callocStruct(GenericLogReceiver);
	r->msgHandler = msgHandler;
	r->userPointer = userPointer;
}

void glrDestroy(GenericLogReceiver** rInOut){
	GenericLogReceiver* r = SAFE_DEREF(rInOut);

	if(!r){
		return;
	}

	SAFE_FREE_STRUCT(*rInOut);
}

void wrapped_glrLog(GenericLogReceiver* r,
					FORMAT_STR const char* format,
					...)
{
	GenericLogReceiverMsg msg = {0};

	if(	!r ||
		!format)
	{
		return;
	}

	VA_START(va, format);
		msg.msgType = GLR_MSG_LOG_TEXT;
		msg.userPointer = r->userPointer;

		msg.logText.format = format;
		msg.logText.va = va;

		r->msgHandler(&msg);
	VA_END();
}

void wrapped_glrLogSegment(	GenericLogReceiver* r,
							const char* tags,
							U32 argb,
							const Vec3 pos1,
							const Vec3 pos2)
{
	GenericLogReceiverMsg	msg = {0};
	Vec3					segments[2];

	if(	!r ||
		!pos1 ||
		!pos2)
	{
		return;
	}

	copyVec3(pos1, segments[0]);
	copyVec3(pos2, segments[1]);

	msg.msgType = GLR_MSG_LOG_SEGMENTS;
	msg.userPointer = r->userPointer;

	msg.logSegments.tags = tags;
	msg.logSegments.argb = argb;
	msg.logSegments.segments = segments;
	msg.logSegments.count = 1;

	r->msgHandler(&msg);
}

void wrapped_glrLogSegmentOffset(	GenericLogReceiver* r,
									const char* tags,
									U32 argb,
									const Vec3 pos,
									const Vec3 offset)
{
	Vec3 pos2;
	addVec3(pos, offset, pos2);
	wrapped_glrLogSegment(r, tags, argb, pos, pos2);
}

AUTO_STRUCT;
typedef struct ConditionCheckerCondition
{
	const char *pKey; AST(POOL_STRING KEY)
	int iLastInternalFrame; //the condition checker's internal frame counter the last time this
		//happened... if it's not the current frame counter minus one, then the condition was not 
		//detected, and thus should be turned off

	U32 iFirstTime; //the first time we heard this
	S64 iFirstUtilitiesLibTicks; //utilitiesLibTicks the first time we heard this
} ConditionCheckerCondition;

AUTO_STRUCT;
typedef struct ConditionChecker
{
	int iSecondsConditionsMustBeTrueFor;
	int iServerFramesConditionMustBeTrueFor;
	int iCurrentFrameNum;

	ConditionCheckerCondition **ppConditions;

} ConditionChecker;

ConditionChecker *ConditionChecker_Create(int iSecondsConditionsMustBeTrueFor, int iServerFramesConditionMustBeTrueFor)
{
	ConditionChecker *pRetVal = StructCreate(parse_ConditionChecker);

	pRetVal->iSecondsConditionsMustBeTrueFor = iSecondsConditionsMustBeTrueFor;
	pRetVal->iServerFramesConditionMustBeTrueFor = iServerFramesConditionMustBeTrueFor;

	return pRetVal;
}

void ConditionChecker_BetweenConditionUpdates(ConditionChecker *pChecker)
{
	int i;

	for (i = eaSize(&pChecker->ppConditions) - 1; i >= 0; i--)
	{
		if (pChecker->ppConditions[i]->iLastInternalFrame != pChecker->iCurrentFrameNum)
		{
			StructDestroy(parse_ConditionCheckerCondition, pChecker->ppConditions[i]);
			eaRemove(&pChecker->ppConditions, i);
		}
	}

	pChecker->iCurrentFrameNum++;
}

bool ConditionChecker_CheckCondition(ConditionChecker *pChecker, const char *pKey)
{
	ConditionCheckerCondition *pCondition = eaIndexedGetUsingString(&pChecker->ppConditions, pKey);

	if (pCondition)
	{
		pCondition->iLastInternalFrame = pChecker->iCurrentFrameNum;

		if (pCondition->iFirstTime < timeSecondsSince2000() - pChecker->iSecondsConditionsMustBeTrueFor 
			&& pCondition->iFirstUtilitiesLibTicks < gUtilitiesLibTicks - pChecker->iServerFramesConditionMustBeTrueFor)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	pCondition = StructCreate(parse_ConditionCheckerCondition);
	pCondition->iFirstTime = timeSecondsSince2000();
	pCondition->iFirstUtilitiesLibTicks = gUtilitiesLibTicks;
	pCondition->pKey = pKey;
	pCondition->iLastInternalFrame = pChecker->iCurrentFrameNum;

	eaPush(&pChecker->ppConditions, pCondition);
	

	return false;
}

void ConditionChecker_Destroy(ConditionChecker **ppChecker)
{
	StructDestroySafe(parse_ConditionChecker, ppChecker);
}





typedef struct RelationshipGrouper
{
	StashTable sGroupIDsByName;
	StashTable sGroupsByGroupID;
	int iNextID;
} RelationshipGrouper;


static void AddNameToGroup(RelationshipGrouper *pGrouper, const char *pName, int iGroupID)
{
	RelationshipGroup *pGroup;
	char *pNameCopy = strdup(pName);
	assert(stashIntFindPointer(pGrouper->sGroupsByGroupID, iGroupID, &pGroup));
	eaPush(&pGroup->ppNames, pNameCopy);
	stashAddInt(pGrouper->sGroupIDsByName, pNameCopy, iGroupID, false);	
}

static void CreateNewGroup(RelationshipGrouper *pGrouper, const char *pName1, const char *pName2)
{
	RelationshipGroup *pGroup = StructCreate(parse_RelationshipGroup);
	pGroup->iIndex = pGrouper->iNextID++;
	stashIntAddPointer(pGrouper->sGroupsByGroupID, pGroup->iIndex, pGroup, true);
	AddNameToGroup(pGrouper, pName1, pGroup->iIndex);
	AddNameToGroup(pGrouper, pName2, pGroup->iIndex);
}

RelationshipGroup *FindAndRemoveGroup(RelationshipGrouper *pGrouper, int iID)
{
	RelationshipGroup *pGroup = NULL;
	int i;
	
	if (!stashIntRemovePointer(pGrouper->sGroupsByGroupID, iID, &pGroup))
	{
		return NULL;
	}


	for (i = 0; i < eaSize(&pGroup->ppNames); i++)
	{
		stashRemoveInt(pGrouper->sGroupIDsByName, pGroup->ppNames[i], NULL);
	}

	return pGroup;
}


RelationshipGrouper *RelationshipGrouper_Create(void)
{
	RelationshipGrouper *pGrouper = calloc(sizeof(RelationshipGrouper), 1);
	pGrouper->sGroupIDsByName = stashTableCreateWithStringKeys(100, StashDefault);
	pGrouper->sGroupsByGroupID = stashTableCreateInt(16);
	pGrouper->iNextID = 1;
	return pGrouper;
}


bool RelationshipGrouper_AreTwoItemsRelated(RelationshipGrouper *pGrouper, const char *pName1, const char *pName2)
{
	int iID1 = 0;
	int iID2 = 0;

	if (!pName1 || !pName2)
	{
		return false;
	}

	stashFindInt(pGrouper->sGroupIDsByName, pName1, &iID1);
	stashFindInt(pGrouper->sGroupIDsByName, pName2, &iID2);

	if (iID1 && iID1 == iID2)
	{
		return true;
	}

	return false;
}


void RelationshipGrouper_AddRelationship(RelationshipGrouper *pGrouper, const char *pName1, const char *pName2)
{
	int iID1 = 0;
	int iID2 = 0;
	RelationshipGroup *pGroup;
	int i;

	if (!pName1 || !pName2 || !pName1[0] || !pName2[0] || stricmp(pName1, pName2) == 0)
	{
		return;
	}



	stashFindInt(pGrouper->sGroupIDsByName, pName1, &iID1);
	stashFindInt(pGrouper->sGroupIDsByName, pName2, &iID2);

	//both are already known, both are in the same group
	if (iID1 && iID1 == iID2)
	{
		return;
	}

	if (iID1 && !iID2)
	{
		AddNameToGroup(pGrouper, pName2, iID1);
		return;
	}

	if (iID2 && !iID1)
	{
		AddNameToGroup(pGrouper, pName1, iID2);
		return;
	}

	if (!iID1 && !iID2)
	{
		CreateNewGroup(pGrouper, pName1, pName2);
		return;
	}

	assert(iID1 && iID1 != iID2);

	pGroup = FindAndRemoveGroup(pGrouper, iID1);

	assert(pGroup);

	for (i = 0; i < eaSize(&pGroup->ppNames); i++)
	{
		AddNameToGroup(pGrouper, pGroup->ppNames[i], iID2);
	}

	StructDestroy(parse_RelationshipGroup, pGroup);
}
	

void RelationshipGrouper_GenerateGroups(RelationshipGrouper *pGrouper, RelationshipGroup ***pppGroups)
{
	StashTableIterator stashIterator;
	StashElement element;

	stashGetIterator(pGrouper->sGroupsByGroupID, &stashIterator);

	while (stashGetNextElement(&stashIterator, &element))
	{
		RelationshipGroup *pGroup = stashElementGetPointer(element);
		eaPush(pppGroups, pGroup);
	}
}

void RelationshipGrouper_Destroy(RelationshipGrouper *pGrouper)
{
	if (!pGrouper)
	{
		return;
	}

	stashTableDestroy(pGrouper->sGroupIDsByName);
	stashTableDestroyStruct(pGrouper->sGroupsByGroupID, NULL, parse_RelationshipGroup);

	free(pGrouper);
}


static bool sbDisableFreeDiskSpaceCheck = false;
AUTO_CMD_INT(sbDisableFreeDiskSpaceCheck, DisableFreeDiskSpaceCheck);

typedef struct PeriodicFreeDiskSpaceCheckStruct
{
	char cDriveLetter;
	int iMinFreePercent;		// Minimum bytes allowed to be free, as a percent
	U64 iMinFreeAbsolute;		// Minimum bytes allowed to be free, as an absolute amount of bytes
	U32 iLastAlertTime;
	int iSecondsBetweenChecks;
	int iSecondsBetweenAlerts;
	diskSpaceCheckCallBack pCallback;
	void *pUserdata;
	U64 iLast64FreeBytesToCaller;	// Warning: Shared between threads, relies on atomic 64-bit write
} PeriodicFreeDiskSpaceCheckStruct;

// Execute callback, now in the context of the main thread.
static void RunDiskSpaceCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	PeriodicFreeDiskSpaceCheckStruct *pStruct = userData;
	pStruct->pCallback(pStruct->iLast64FreeBytesToCaller, pStruct->pUserdata);
}

static S32 __stdcall PeriodicFreeDiskSpaceCheckThreadFunc(PeriodicFreeDiskSpaceCheckStruct *pStruct)
{
	char drive[12];

	unsigned __int64 i64FreeBytesToCaller = 0,
                       i64TotalBytes = 0,
                       i64FreeBytes = 0;

	bool bResult;
	unsigned __int64 iFreePercent;


	EXCEPTION_HANDLER_BEGIN;

	while (1)
	{
		Sleep(pStruct->iSecondsBetweenChecks * 1000);

		if (sbDisableFreeDiskSpaceCheck)
		{
			continue;
		}

		sprintf(drive, "%c:\\", pStruct->cDriveLetter);

		bResult = GetDiskFreeSpaceEx_UTF8(drive,
			(PULARGE_INTEGER)&i64FreeBytesToCaller,
			(PULARGE_INTEGER)&i64TotalBytes,
			(PULARGE_INTEGER)&i64FreeBytes);

		if (bResult)
		{
			iFreePercent = i64FreeBytesToCaller / (i64TotalBytes / 100);
			if (iFreePercent < pStruct->iMinFreePercent || i64FreeBytesToCaller < pStruct->iMinFreeAbsolute)
			{

				// Trigger alert, if necessary.
				if (!pStruct->iLastAlertTime || pStruct->iLastAlertTime <= timeSecondsSince2000() - pStruct->iSecondsBetweenAlerts)
				{
					TriggerAlertDeferred("DISK_NEARLY_FULL", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, "Drive %c has only %d percent free space",
						pStruct->cDriveLetter, (int)iFreePercent);
					pStruct->iLastAlertTime = timeSecondsSince2000();
				}

				// Trigger callback, if necessary.
				if (pStruct->pCallback)
				{
					pStruct->iLast64FreeBytesToCaller = i64FreeBytesToCaller;
					TimedCallback_Run(RunDiskSpaceCallback, pStruct, 0);
				}
			}
		}
	}
	

	EXCEPTION_HANDLER_END;
	return 0;
}


void BeginPeriodicFreeDiskSpaceCheck(char cDriveLetter, int iSecondsBetweenChecks, int iSecondsBetweenAlerts, int iMinFreePercent, U64 iMinFreeAbsolute,
	diskSpaceCheckCallBack pCallback, void *pUserdata)
{
	DWORD thread_id;	
	
	if (!isalpha(cDriveLetter))
	{
		return;
	}
	else
	{
		PeriodicFreeDiskSpaceCheckStruct *pStruct = calloc(sizeof(PeriodicFreeDiskSpaceCheckStruct), 1);

		if (!cDriveLetter)
		{
			cDriveLetter = getExecutableName()[0];
		}

		pStruct->cDriveLetter = cDriveLetter;
		pStruct->iSecondsBetweenAlerts = iSecondsBetweenAlerts;
		pStruct->iSecondsBetweenChecks = iSecondsBetweenChecks;
		pStruct->iMinFreePercent = iMinFreePercent;
		pStruct->iMinFreeAbsolute = iMinFreeAbsolute;
		pStruct->pCallback = pCallback;
		pStruct->pUserdata = pUserdata;

		_beginthreadex( NULL,				// no security attributes
			64000,			// stack size
			&PeriodicFreeDiskSpaceCheckThreadFunc,	// thread function
			pStruct,		// argument to thread function
			0,					// use default creation flags
			&thread_id);		// returns the thread identifier

	}
}


#include "utils_c_ast.c"
#include "utils_h_ast.c"
