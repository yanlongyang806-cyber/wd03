#pragma once
GCC_SYSTEM

/*Why does this file exist, when all its functionality can be achieved just by calling the right
gimmeDLL functions and so forth? Because all the functions in this file are called as 
external threads, and thus, if they crash or fail or hang, these functions can return
failure without crashing*/



typedef struct CheckinInfo CheckinInfo;


//returns -1 on failure
int Gimme_GetBranchNum(const char *pFolderName);

//returns true on success
bool Gimme_UpdateFoldersToTime(U32 iTime, char *pFolderNameString, char **ppFolderNameList, U32 iFailureTime, char *pExtraCmdLine);

//returns true on success
bool Gimme_UpdateFileToTime(U32 iTime, char *pFileName, U32 iFailureTime);


typedef enum enumGimmeGetCheckinsFlags
{
	GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS=1 << 0,
	GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS = 1 << 1,
	GIMMEGETCHECKINS_FLAG_REPLACE_DOLLARSIGNS = 1 << 2,
} enumGimmeGetCheckinsFlags;


//FolderNameString is a semicolon-separated list. FolderNameList is an earray. Only one should be set
bool Gimme_GetCheckinsBetweenTimes_ForceBranch(U32 iFromTime, U32 iToTime, char *pFolderNameSring, char **ppFolderNameList, enumGimmeGetCheckinsFlags eFlags, CheckinInfo ***pppList, U32 iFailureTime, int iForcedBranch, int iForcedCoreBranch);
#define Gimme_GetCheckinsBetweenTimes(iFromRev, iToRev, pFolderNameString, ppFolderNameList, eFlags, pppList, iFailureTime) Gimme_GetCheckinsBetweenTimes_ForceBranch(iFromRev, iToRev, pFolderNameString, ppFolderNameList, eFlags, pppList, iFailureTime, -1, -1)


void Gimme_ApproveFoldersByTime(U32 iTime, char *pFolderNames, U32 iFailureTime);

//returns true for "awerner" and "msimpson" and "sstacy-laptop" but not for "you do not have the latest version" and
//things of that sort
bool Gimme_IsReasonableGimmeName(char *pName);

//turns "awerner" into "awerner", but "sstacy-laptop" into "sstacy". Loads names from
//n:/gimme/EmailAliases.txt if you call Gimme_LoadEmailAliases()
//
//returns POOLED strings
const char *Gimme_GetEmailNameFromGimmeName(char *pName);
void Gimme_LoadEmailAliases(void);


//gimme maintains a list of people who clicked "yes" to do a checkin despite not testing. We want
//to be able to parse that list to see if we need to humiliate and berate people who did that, and then
//the CB broke on that file.

void Gimme_LoadDidTheyClickYesEntries(void);
bool Gimme_DidTheyClickYes(const char *pUserName, const char *pFileName, U32 iStartTime, U32 iEndTime);