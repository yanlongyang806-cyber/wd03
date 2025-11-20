
// JDRAGO 2009/06/30 - All of these functions are intended to be running on a Windows PC
//                     which has a 360 devkit associated with it. 

bool xboxReboot(bool bColdReboot); // Cold Reboot is essentially a power cycle
bool xboxIsReady(void); // Returns whether or not the xbox is responding to questions
bool xboxGetRunningExePath(char **estr);
bool xboxGetXboxName(char **estr);


//run a background thread that does the above queries once a second, then lets you query them
void xboxBeginStatusQueryThread(void);


void xboxQueryStatusFromThread(bool *bIsReady, char **ppXboxName /*estr*/, char **ppRunningExePath /*estr*/);

bool xboxQueryStatusXboxWasEverAttached(void);


//to capture printfs from the xbox, first call this
bool xboxBeginCapturingPrintfs(void);

//then any time you want to get the buffer of printfs, call this. 
//ppBuf is a pointer to the string. pBufSize is the length of the string.
//piCounter is a counter of how many times the string has changed, so you
//can easily check whether anything has changed since your last query.
//
//Note that ppBuf is NOT an eString, just a pointer that is set
void xboxAccessCapturedPrintfs(char **ppBuf, int *pStrLen, int *piCounter);

//after calling the above you MUST call this ASAP, because there's a critical
//section involved
void xboxFinishedAccessingPrintfs(void);

//call this to quickly get the counter without opening up the critical section or anything
int xboxGetPrintfCounter(void);

//call this to reset the buffer to empty
void xboxResetPrintfCapturing(void);


typedef struct XboxFileInfo
{
	char fullName[CRYPTIC_MAX_PATH];
	S64 iSize;
	U32 iCreateTime;
	U32 iModTime;
} XboxFileInfo;

//recursively scan an xbox directory for all files
bool xboxRecurseScanDir(char *pDirName, XboxFileInfo ***pppFiles);


//convenience function... returns "c:\\program files\\microsoft xbox 360 sdk\\bin\\win32" or something along those lines
char *xboxGetBinDir(void);