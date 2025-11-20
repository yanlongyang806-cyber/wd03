//some servers are launched as "Jobs" by the Job manager. This is how they communicate their
//status back to the Job manager.

//returns NULL if no Job is active

typedef struct JobGroupOwner JobGroupOwner;

char *GetCurJobNameForJobManager(void);

JobGroupOwner *GetOwnerOfCurrentJob(void);


void JobManagerUpdate_Status(int iPercentComplete, char *pString, ...);

//Jobs should eventually call this. Usually, the server is then useless, so set bShutSelfDown to true,
//server will shutdown once handshake is complete
//void JobManagerUpdate_Complete(bool bSucceeded, bool bShutSelfDown, char *pString, ...);
//now a #def below

//Used for remote command jobs, as there can be multiple on the same server at once referring to different jobs
void JobManagerUpdate_CompleteWithJobName(char *pJobName, bool bSucceeded, bool bShutSelfDown, char *pString, ...);

#define JobManagerUpdate_Complete(bSucceeded, bShutsSelfDown, pString, ...) JobManagerUpdate_CompleteWithJobName(NULL, bSucceeded, bShutsSelfDown, pString, __VA_ARGS__)


//ALWAYS use this to get Job group names
char *GetUniqueJobGroupName(char *pBaseString);

LATELINK;
void UpdateJobLogging_Internal(U32 iUserData, char *pName, char *pString);

void JobManagerUpdate_Log(FORMAT_STR const char *pFmt, ...);
void JobManagerUpdate_LogWithJobName(char *pJobName, FORMAT_STR const char *pFmt, ...);