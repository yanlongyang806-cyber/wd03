#pragma once
GCC_SYSTEM

//all of these functions work with multiple foldernames separated by semicolons, ie
//SVN_UpdateFolder("c:\src\fightclub;c:\src\core");

//returns true on success
bool SVN_UpdateFolders(char **ppFolderNames, U32 iFailureTime);

//returns true on success
bool SVN_AttemptCleanup(char **ppFolderNames, U32 iFailureTime);

//if ppRepositoryURL is non-NULL, sticks the name of the repository into it (ie, svn://code/dev) (estring)
int SVN_GetRevNumOfFolders(char *pFolderNames, char **ppFolderList, char **ppRepositoryURL, U32 iFailureTime);

// Note: patchserver.c::handleReqCheckinsBetweenTimes() uses this structure,
//   so you cannot change it (or if you do, you need to make the PatchServer
//   send it in a more compatible way).
AUTO_STRUCT;
typedef struct CheckinInfo
{
	char *userName; AST(ESTRING)
	U32 iRevNum;
	char *checkinComment; AST(ESTRING)
	U32 iCheckinTimeSS2000;
} CheckinInfo;

AUTO_STRUCT;
typedef struct CheckinList
{
	CheckinInfo**	checkins;
} CheckinList;


//gets every checkin after iFromRev up through (and including) iToRev
//
//returns false on failure
//
//at most one of pFolderNameString, ppFolderNameList and pRepository should be set
//
//pFolderNames is a semicolon-seperated list. ppFolderList is an earray.
#define SVNGETCHECKINS_FLAG_REPLACE_DOLLARSIGNS  (1<<0)
bool SVN_GetCheckins(int iFromRev, int iToRev, char *pFolderNameString, char **ppFolderNameList, char *pRepository, CheckinInfo ***pppList, U32 iFailureTime, U32 iFlags);


//shared by GimmeUtils.c
int SortCheckinInfosByTime(const CheckinInfo **pInfo1, const CheckinInfo **pInfo2);

bool SVN_FolderIsSVNRepository(char *pFolderName);

extern ParseTable parse_CheckinInfo[];
#define TYPE_parse_CheckinInfo CheckinInfo

U32 SVN_GetSVNNumberWhenBranchWasCreated(char *pBranchDepositoryName);


//make sure that these don't get defined on any end-user-available apps (ie, gameclient.exe, crypticlauncher, errortracker.exe)
LATELINK;
char *SVN_GetUserName(void);

LATELINK;
char *SVN_GetPassword(void);
