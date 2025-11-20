#ifndef _PROCLIST_H
#define _PROCLIST_H

#include "stdtypes.h"
#include "net/net.h"
#include "GlobalTypeEnum.h"

#define NUM_TICKS 60

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "process_id, exename, exeNameForTracking, container_id, container_type, stateString");
typedef struct ProcessInfo
{
	U32		process_id;
	U32		time_tables[NUM_TICKS];
	U8		tag;
	U32		count;
	U32		mem_used_phys;
	U32		mem_used_phys_max;
	U32		mem_used_virt;
	float	fFPS;
	U32 iLongestFrameMsecs;
	char	exename[100];

	//a substring, ie, "gameserver", used to verify that it's still what we think it is 
	char	exeNameForTracking[100];
	int		container_id;	
	GlobalType	container_type;	
	int		lowLevelControllerIndex;
	int		crashErrorID;
	char	stateString[256];
	bool	bStateStringChanged;
	U32		iLastContactTime;

	U32		iTriedToKillItThen;
	bool	bAlreadyAlertedNonKilling;

	float fCPUUsage; 
	float fCPUUsageLastMinute; 

} ProcessInfo;

typedef struct
{
	ProcessInfo **ppProcessInfos;
	ProcessInfo	total;
	U32			timestamp_tables[NUM_TICKS]; // Array of times (in ms) when samply occured
	U32			total_offset; // The number of accumulated msecs from processees who have exited since our sampling began
} ProcessList;

extern ProcessList process_list;

void procSendTrackedInfo(Packet *pak);
void procGetList(void);
bool trackProcessByKnownProcessID(int iContainerType, int iContainerID, int iProcessID, int iLowLevelControllerIndex, char *pExeName);
U32 trackProcessByExename(int iContainerType, int iContainerID, const char *exeName);
void KillProcessFromTypeAndID(int container_id,int container_type);
void KillAllProcessesWithTypeAndID(const char **ppExeNamesNotToKill, U32 *piPIDsNotToKill);
void HideAndShowWindows();
void HideProcessFromTypeAndID(int container_id,int container_type, bool bHide);
ProcessInfo *FindProcessByPID(int iProcessID);
void HideAllProcesses(bool bHide);

void MarkCrashedServer(U32 iPid, int iErrorTrackerID);
bool processExistsByNameSubString(char *pProcName);

void LogProcInfo(void);

extern U32 giLastControllerTime;

//one and only one process can be left in "ignored" state at a time, this is so that if processes end up inf looping,
//they can be debugged.
//
//0 means no process is being ignored
U32 PidOfProcessBeingIgnored(void);

//only call this if PidOfProcessBeingIgnored() is 0
void BeginIgnoringProcess(ProcessInfo *pProcess, int iTimeToLeaveIt);

#endif
