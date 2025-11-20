#pragma once
#include "globalTypes.h"

typedef struct RemoteCommandGroup RemoteCommandGroup;
typedef struct JobSummary JobSummary;

AUTO_ENUM;
typedef enum JobManagerJobType
{
	//the most basic type of Job... launch a server of a specified type with a 
	//specified command line, wait for it to complete and report its success or failure
	JOB_SERVER_W_CMD_LINE, 

	//send a remote command. Fails if this command fails or doesn't return. Then waits for
	//success or failure as above.
	//The remote command MUST look like this: 
	//
	//AUTO_COMMAND_REMOTE;
	//int myCommandName({some args}, char *pJobName, {other args});
	//
	//That is, the return type MUST be int, which specifically MUST return 1 on success,
	//and it must have a string jobname argument. Then when you specify the string to call, 
	//put $JOBNAME$ in place of the job name, and the job manager will fill that in. For instance, if your command is
	//int doTask(int x, char *pJobName, int y); you might specify remote command string "doTask 5 \"$JOBNAME$\" 7"
	JOB_REMOTE_CMD,

	//wait for a specified number of seconds in iTimeOut. Used by publish testing automation.  
	JOB_WAIT,
} JobManagerJobType;

//extra info for Jobs of type Job_SERVER_W_CMD_LINE
AUTO_STRUCT;
typedef struct JobManagerServerWCmdLineDef
{
	GlobalType eServerTypeToLaunch;
	char *pExtraCmdLine;
} JobManagerServerWCmdLineDef;

AUTO_STRUCT;
typedef struct JobManagerRemoteCommandDef
{
	GlobalType eTypeForCommand;
	ContainerID iIDForCommand;
	char *pCommandString;

	char *pCancelCommandString; 
		//if this job is interrupted for some reason, try to send this
		//command to the same server. This is optional, and shouldn't be atomically depended
		//on

	int iInitialCommandTimeout; AST(DEF(60))

	bool bSlow;
} JobManagerRemoteCommandDef;

AUTO_ENUM;
typedef enum JobManagerGroupResultEnum
{
	JMR_UNKNOWN,
	JMR_ONGOING,
	JMR_SUCCEEDED,
	JMR_FAILED,
	JMR_WAITING, //one individual job is waiting for another

	JMR_QUEUED, //the whole job group is waiting to start. Only a certain number of jobs of each type can run at once
} JobManagerGroupResultEnum;

//each job has a concept of who "owns" it, basically 100% for debugging
AUTO_STRUCT;
typedef struct JobGroupOwner
{
	//an owner is presumably either a user, who has an account name/ID:
	char *pPlayerOwnerAccountName;
	U32 iPlayerAccountID;

	//or a server, which has a type/ID
	GlobalType eServerOwnerType;
	ContainerID iServerOnwerID;
} JobGroupOwner;

AUTO_STRUCT;
typedef struct JobManagerJobStatus
{
	JobManagerGroupResultEnum eResult;
	char *pJobName;
	int iPercentDone;
	char *pCurStatusString;
	U32 iTimeRunning;
} JobManagerJobStatus;

AUTO_STRUCT;
typedef struct JobManagerGroupResult
{
	char *pGroupName;
	JobGroupOwner owner;
	JobManagerGroupResultEnum eResult;
	JobManagerJobStatus **ppJobStatuses; //set only if status is ONGOING
	U32 iTimeStarted; //set if it's not UNKNOWN
	U32 iTimeCompleted; //set if it's SUCCEEDED or FAILED

	int iPlaceInQueue; //set if eResult is QUEUED
} JobManagerGroupResult;

AUTO_STRUCT;
typedef struct JobManagerJobDef
{
	char *pJobName;
	JobManagerJobType eType;
	JobManagerServerWCmdLineDef *pServerWCmdLineDef;
	JobManagerRemoteCommandDef *pRemoteCmdDef;
	int iTimeout; //if runs for this many seconds without succeeding, fail
	char **ppJobsIDependOn; //jobnames of other jobs in this group. I won't start until they're complete

} JobManagerJobDef;




AUTO_STRUCT;
typedef struct JobManagerJobGroupDef
{
	//a generic name for what "type" of job this is, ie, "ugc publish". Used for grouping, so
	//all jobs 
	const char *pGroupTypeName; AST(POOL_STRING)

	JobGroupOwner owner;
	char *pJobGroupName;
	JobManagerJobDef **ppJobs;
	RemoteCommandGroup *pWhatToDoOnSuccess;
	RemoteCommandGroup *pWhatToDoOnFailure;
	RemoteCommandGroup *pWhatToDoOnJobManagerCrash;

	GlobalType eServerTypeForLogUpdates;
	ContainerID iServerIDForLogUpdates;
	U32 iUserDataForLogUpdates;
	char *pNameForLogUpdates;

	bool bAlertOnFailure;

	char *pComment;

} JobManagerJobGroupDef;

