#pragma once


typedef struct NetComm NetComm;

//this API is for receiving status updates from one or more patch clients... for instnace, a controller
//begins all the machines in the shard patching, and then receives status updates from them. The basic idea is that
//you begin monitoring and then call a tick function, and then you will get back callbacks with status
//updates

//THREAD-SAFETY: This is mostly not thread-safe... in fact, many of the functions will assert if you call them from a thread other
//than the thread that the thread that you called PCLStatusMonitoring_Begin from. The exception is PCLStatusMonitoring_Add,
//which can be called from any thread.

AUTO_ENUM;
typedef enum PCLStatusMonitoringState
{
	PCLSMS_UPDATE, //here's an update string
	PCLSMS_FAILED, //patching has failed
	PCLSMS_SUCCEEDED, //patching has succeeded

	PCLSMS_TIMEOUT, //locally generated... after getting one or more updates from a particular
		//client, stopped getting them (an arbitrary number of time intervals can be specified, if 
		//you want warning/severe warning/fatal error, or whatever)
	
	PCLSMS_FAILED_TIMEOUT,
		//like above, but now fatal, you will hear nothing more unless the pcl contacts us again

	PCLSMS_INTERNAL_CREATE, //added by PCLStatusMonitoring_Add, and we've never gotten an update

	PCLSMS_DELETING, //called after a delay after either succeeded or failed
} PCLStatusMonitoringState;

extern StaticDefineInt PCLStatusMonitoringStateEnum[];


//this is what the patchclient sends out
AUTO_STRUCT;
typedef struct PCLStatusMonitoringUpdate_Internal
{
	char *pMyIDString;
	PCLStatusMonitoringState eState;

	//this is the most recent status update, usuall from an update, but can also be
	//a meaningful failure report
	char *pUpdateString; AST(ESTRING)

	char *pPatchName;		//ie, "ST.16.20111016a.4"
	char *pPatchDir;		//root dir into which patching is taking place
	int iMyPID;				//PID of patching process
	U64 uMyHWND;			//hWnd of associating patching console window, if any
} PCLStatusMonitoringUpdate_Internal;

//this is what is handed from the monitoring API out to the calling code
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "internalStatus.State, internalStatus.UpdateString, MostRecentUpdateTime");
typedef struct PCLStatusMonitoringUpdate
{
	PCLStatusMonitoringUpdate_Internal internalStatus;

	U32 iTimeBegan; AST(FORMATSTRING(HTML_SECS_AGO=1))
		
	U32 iMostRecentUpdateTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
	
	U32 iSucceededOrFailedTime; AST(FORMATSTRING(HTML_SECS_AGO=1))

	//can be useful to differentiate between timeout-at-startup and timeout-during-patching
	int iNumUpdatesReceived;

	//if STATUS_TIMEOUT, this is the # seconds since last update
	int iTimeoutLength; AST(FORMATSTRING(HTML_SECS_DURATION=1))
	
	//which timeout interval in the list this was (only meaningful if iTimeoutLength > 0)
	int iTimeoutIndex;

	//set while the client code is iterating, requests that the monitoring code destroy/forget this status
	//(never set internally)
	bool bDestroyRequested;

} PCLStatusMonitoringUpdate;


typedef void (*PCLStatusMonitoringCB)(PCLStatusMonitoringUpdate *pUpdate);

//begin status monitoring... and provide TIMEOUT callbacks at each of the lengths in pTimeOutIntervals, which
//is an ea32. If iPersistTimeAfterFailOrSucceed is set, then the final update will be kept around in iteratable fashion
//for that many seconds after success or fail
//
//returns true if it succeeds, false if it fails (presumably because someone else is already listening on that port)
bool PCLStatusMonitoring_Begin(NetComm *pComm, PCLStatusMonitoringCB pCB, U32 iListeningPortNum, U32 *pTimeOutIntervals, 
	int iPersistTimeAfterFail, int iPersistTimeAfterSucceed);
void PCLStatusMonitoring_Tick(void);

//Someone is starting patching and should fairly shortly contact me and start providing updates... so
//add a fake update from him in order to get status updates
void PCLStatusMonitoring_Add(char *pIDString);

bool PCLStatusMonitoring_IsActive(void);

typedef struct PCLStatusMonitoringIterator PCLStatusMonitoringIterator;

/*
		PCLStatusMonitoringIterator *pIter = NULL;
		PCLStatusMonitoringUpdate *pUpdate = NULL;

		while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIter)))
		{
			//do something
		}
*/


//pass in a NULL iterator pointer to begin iteration. Set bDeletionRequest to delete an update (for instance,
//about to begin priming, want to delete any leftover priming tasks from the last priming run)
PCLStatusMonitoringUpdate *PCLStatusMonitoring_GetNextUpdateFromIterator(PCLStatusMonitoringIterator **ppIterator);




void PCLStatusMonitoring_DismissSucceededOrFailedByName(char *pIDString);

PCLStatusMonitoringUpdate *PCLStatusMonitoring_FindStatusByName(char *pIDString);

//a copy of patchclient.exe is running somewhere, sending status to us... tell it to abort its patching
void PCLStatusMonitoring_AbortPatchingTask(char *pIDString);

//latelinked so that we don't have to link patchclientlib into everything 
//
//puts all the status into the list. You own the earray, but you do not own the structs.
void PCLStatusMonitoring_GetAllStatuses(PCLStatusMonitoringUpdate ***pppUpdates);