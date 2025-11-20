#ifndef STUB_SOUNDLIB

#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "fmod.h"
#include "fmod_errors.h"

C_DECLARATIONS_BEGIN

typedef enum SoundTreeType SoundTreeType;
typedef struct SoundDSP SoundDSP;
typedef struct FMOD_EVENT_INFO FMOD_EVENT_INFO;
typedef struct FMOD_EVENT_WAVEBANKINFO FMOD_EVENT_WAVEBANKINFO;
typedef struct FMOD_MEMORY_USAGE_DETAILS FMOD_MEMORY_USAGE_DETAILS;
typedef struct StashTableImp StashTableImp;
typedef StashTableImp* StashTable;
typedef struct DSP_SfxReverb DSP_SfxReverb;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

#define MAX_CHANNELS 256

#define EVENT_DBGPRT_DRIVER			0x000001
#define EVENT_DBGPRT_FORMAT			0x000002
#define EVENT_DBGPRT_DSPBUF			0x000004
#define EVENT_DBGPRT_VERSION		0x000008
#define EVENT_DBGPRT_ALL			0xFFFFFF

#define FMOD_ErrCheck(oper)											\
			{														\
				FMOD_RESULT __result = (oper);						\
				sndPrintError(__result, __FILE__, __LINE__);		\
			}

#define FMOD_ErrCheckRetF(res)										\
			{														\
				FMOD_ErrCheck(result)								\
				if(FMOD_ErrIsFatal(res)) return res;					\
			}

#define FMOD_ErrIsFatal(error) ((error)==FMOD_ERR_MEMORY || (error)==FMOD_ERR_OUTPUT_INIT)

#endif

// Wrapper version of FMOD_EVENT_INFO
typedef struct FmodEventInfo
{
    int                      memoryused;        /* This member has been deprecated. */
    int                      positionms;        /* [out] Time passed in playback of this event instance in milliseconds. */
    int                      lengthms;          /* [out] Length in milliseconds of this event. Note: lengthms will be -1 if the length of the event can't be determined i.e. if it has looping sounds. */
	//int			  			 lengthmsnoloop;		  /* [out] Length in milliseconds of this event. Ignores loops. */
    int                      channelsplaying;   /* [out] Number of channels currently playing in this event instance. */
    int                      instancesactive;   /* [out] Number of event instances currently in use. */
    int                      maxwavebanks;      /* [in/out] Out, number of wavebanks refered to by this event.  In. Maximum size of array of wavebankinfo structures supplied by user.  Optional. */
    
	void					 *wavebankinfo;      /* [in] Pointer to array FMOD_EVENT_WAVEBANKINFO structures (max size defined by maxwavebanks).  FMOD will fill these in with detailed information on each wave bank. Optional. */
												// original type: FMOD_EVENT_WAVEBANKINFO

    unsigned int             projectid;         /* [out] The runtime 'EventProject' wide unique identifier for this event. */
    unsigned int             systemid;          /* [out] The runtime 'EventSystem' wide unique identifier for this event.  This is calculated when single or multiple projects are loaded. */
    float                    audibility;        /* [out] current audibility of event. */
    int                      numinstances;      /* [in/out] On entry, maximum number of entries in instances array. On exit, actual number of entries in instances array, or if instances is null, then it is just the number of instances of this event. Optional. */
    
	void             **instances;         /*   [in/out] Pointer to an array that will be filled with the current reference-counted event handles of all instances of this event. Optional. Specify 0 if not needed. Must be used in conjunction with numinstances. Note: Due to reference counting, the event instance handles returned here may be different between subsequent calls to this function. If you use these event handles, make sure your code is prepared for them to be invalid! */
											// original type: FMOD_EVENT
} FmodEventInfo;

#ifndef STUB_SOUNDLIB

typedef enum
{
	FMOD_3DROLLOFF_CUSTOM,
	FMOD_3DROLLOFF_LOG,
	FMOD_3DROLLOFF_LINEAR,
} Fmod3DRolloffType;

//FMOD_RESULT FMODEventSystemInit(void);
S32 fmodEventSystemInit(void);
void fmodInitDriverInfo(void);
void fmodUpdateDriverInfo(void);

void fmodSetLanguage(const char *language);
const char *fmodGetLanguage(void);

FMOD_RESULT FMOD_EventSystem_LoadBank(const char* bank, const char *full_name);
const char* fmodGetMediaPath(void);

FMOD_RESULT FMOD_EventSystem_UnloadBank(const char* bank);

FMOD_RESULT FMOD_EventSystem_UnloadAll(void);

FMOD_RESULT FMOD_EventSystem_PlayEvent(void *event);
FMOD_RESULT FMOD_EventSystem_set3DListenerAttributes(Vec3 pos, Vec3 vel, Vec3 forward, Vec3 up);
FMOD_RESULT FMOD_EventSystem_get3DListenerAttributes(Vec3 pos, Vec3 vel, Vec3 forward, Vec3 up);
//FMOD_RESULT FMODEventSystemUpdate(void);
S32 fmodEventSystemUpdate(void);
FMOD_RESULT FMOD_EventSystem_StartEvent(char *event_name, void **event);
FMOD_RESULT FMOD_EventSystem_StopEvent(void *event, bool immediate);
FMOD_RESULT FMOD_EventSystem_EventSetMute(void *event, U32 mute);
//FMOD_RESULT FMODEventSystemGetEvent(const char *event_name, void **event);
S32 fmodEventSystemGetEvent(const char *event_name, void **event, void *userdata);
FMOD_RESULT FMOD_EventSystem_GetEventEx(const char *event_name, U32 flags, void **event, void *userdata);
FMOD_RESULT FMOD_EventSystem_GetSystemID(void *event, int *id);
FMOD_RESULT FMOD_EventSystem_GetSystemIDStr(const char* event_name, int *id);
FMOD_RESULT FMOD_EventSystem_GetEventInfoOnly(const char *event_name, void **event);

FMOD_RESULT FMOD_EventSystem_CreateGeometry(void **geometry, S32 num_tris);
FMOD_RESULT FMOD_EventSystem_AddPolygon(void *geometry, Vec3 p1, Vec3 p2, Vec3 p3, F32 occlude, F32 reverb_occludh);
FMOD_RESULT FMOD_EventSystem_FreeGeometry(void **geometry);

void FMOD_EventSystem_GetMemStats(int *current, int *max);
F32 fmodGetCPUUsage(void);

FMOD_RESULT FMOD_EventSystem_GetSoundByIndex(void *event, int index, void **sound);

//typedef void (*ESPrintFunc)(FORMAT_STR const char *, ...);
//FMOD_RESULT FMOD_EventSystem_PrintDebug(int which, ESPrintFunc print);

// Basically, only position is emphatically recommended.  You can set any of them to NULL, but the
// documentation says little about what pos=NULL but vel && orient!=NULL means?
FMOD_RESULT FMOD_EventSystem_Set3DEventAttributes(void *event, const Vec3 pos, const Vec3 vel, Vec3 orientation);
FMOD_RESULT FMOD_EventSystem_Get3DEventAttributes(void *event, Vec3 pos, Vec3 vel, Vec3 orientation);

FMOD_RESULT FMOD_EventSystem_SetVolume(void *event, F32 volume);
FMOD_RESULT FMOD_EventSystem_GetVolume(void *event, F32 *vol);

FMOD_RESULT FMOD_EventSystem_GetName(void *event, char **name);
FMOD_RESULT FMOD_EventSystem_GetParentGroup(void *event, void **parent);
FMOD_RESULT FMOD_EventSystem_GetGroupName(void *group, char **name);
S32			fmodEventSystemGetGroup(char *group_name, void **group);

typedef int (*sndEventRunFunc)(void *event, void *userdata);
FMOD_RESULT FMOD_EventSystem_GroupRun(void *group, sndEventRunFunc func, void *userdata);
FMOD_RESULT FMOD_EventSystem_GroupGetNumEvents(void *group, int *event_count);
FMOD_RESULT FMOD_EventSystem_GroupGetEventByIndex(void *group, int event_index, void **event);

FMOD_RESULT FMOD_EventSystem_EventIsLooping(void *event, int *loop);
U32 fmodEventIsLooping(void *event);

U32 fmodGetEventInfo(void *event, FMOD_EVENT_INFO *info);
// wrapper version
U32 fmodGetFmodEventInfo(void *event, FmodEventInfo *info);


FMOD_RESULT FMOD_EventSystem_EventGetRadius(void *event, float *radius);

FMOD_RESULT FMOD_EventSystem_SetMarkerCallback(void *event, void *ud);
FMOD_RESULT FMOD_EventSystem_GetWaveData(float *data, int num_data, int channel);

typedef FMOD_RESULT (F_CALLBACK *sndEventCallback)(void *e, int type, void *p1, void *p2, void *ud);
FMOD_RESULT FMOD_EventSystem_AddCallback(void *event, int master, int final, int type, sndEventCallback cb, void *data);
FMOD_RESULT FMOD_EventSystem_DelCallback(void *event, int master, int final, int type, sndEventCallback cb);

S32 fmodGetGetCBType(void);
S32 fmodGetStartCBType(void);
S32 fmodGetEndCBType(void);
S32 fmodGetModCBType(void);
S32 fmodGetStlCBType(void);
S32 fmodGetMkrCBType(void);

bool FMOD_EventSystem_CBEnd(int type);
bool FMOD_EventSystem_CBMod(int type);

FMOD_RESULT FMOD_EventSystem_GetProperty(void *event, char *property, void *value);
U32 fmodEventHasProperty(void *event, char *property);
FMOD_RESULT FMOD_EventSystem_GroupGetProperty(void *group, char *property, void *value);

FMOD_RESULT FMOD_EventSystem_SetMute(bool mute);
FMOD_RESULT FMOD_EventSystem_SetMasterVolume(F32 volume);

FMOD_RESULT FMOD_EventSystem_StopAllEvents(void);

FMOD_RESULT FMOD_EventSystem_SetVolumeByCategory(char *category, F32 vol);

FMOD_RESULT FMOD_EventSystem_ShutDown(void);
FMOD_RESULT FMOD_EventSystem_ClearAll(void);

bool FMOD_EventSystem_ProjectNotLoaded(void);

U32 FMOD_EventSystem_GetEventList(char*** earrayOut, char* group);

typedef U32 (*sndTreeIterator)(char* name, void *e_g_c, SoundTreeType type, void *p_userdata, void *userdata, void **child_userdata);
U32 FMOD_EventSystem_TreeTraverse(SoundTreeType type, sndTreeIterator iter, void* rootdata, void *userData);

// Should be private
void fmodManualCallback(void *e, int type);

// checks for things that can go wrong at runtime
U32 fmodEventHasRanPastEndWhileLoading(void *event);
U32 fmodEventHasFevFsbMismatch(void *event);

// Is the event "playing", may be loading still
U32 fmodEventIsPlaying(void *event);
// Is the event playing with channels active (i.e. you can hear it)
U32 fmodEventIsActive(void *event);
void fmodEventGetFullName(char **estrOut, void *event, bool includeProject);
bool fmodEventGetProjectFilename(void *event, char **projectFilename);
bool fmodProjectGetFilename(void *project, char **projectFilename);

U32 fmodEventExists(const char* event_name);
U32 fmodEventCanPlay(void *event_info);
U32 fmodEventSetCanPlay(void *event_info, int value);
U32 fmodEventIs2D(void *fmod_event);

void fmodEventGet3DInfo(void *event, Fmod3DRolloffType *rolloff, F32 *minDistance, F32 *maxDistance);

U32 fmodGroupIsExclusive(void *group);
U32 fmodEventSystemGroupHasPlayingEvent(void *group, void **event2);

typedef void (*ExclusiveStolenCB)(void *event);
U32 fmodSetExclusiveStolenCallback(ExclusiveStolenCB cb);

U32 fmodEventSetUserData(void *event, void *data);
U32 fmodEventGetUserData(void *event, void **data);

void fmodEventSetVolume(void *event, F32 volume);
F32 fmodEventGetVolume(void *event);
F32 fmodEventSystemGetVolumeProperty(void *event);
void fmodEventSetVolumeProperty(void *event, F32 value);
S32 fmodEventGetPriority(void *event);

FMOD_RESULT fmodEventSystemGetNumEvents(int *numEvents, int *numInstances, int *numPlaying);

void fmodEventSystemGetPlaying(void ***events);
U32 fmodEventSystemGetNumPlaying(void);

U32 fmodEventHasParam(void *event, char* param);
void fmodEventSetParam(void *event, char *param, F32 value);

int fmodEventGetMaxPlaybacks(void *info_event);
void fmodEventSetMaxPlaybacks(void *info_event, int maxPlaybacks);

FMOD_RESULT fmodDSPGetMemoryInfo(void *dsp, unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details);
FMOD_RESULT fmodEventGetMemoryInfo(void *event, unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details);
FMOD_RESULT fmodEventSystemGetMemoryInfo(unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details);
FMOD_RESULT fmodSystemGetMemoryInfo(unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details);

U32 fmodEventIsPanLevel0(void *event);
F32 fmodEventGetPanLevel(void *event);
void fmodEventSetDopplerScale(void *event, F32 value);
F32 fmodEventGetDopplerScale(void *event);
void fmodEventSetPanLevel(void *event, F32 value);
F32 fmodEventGetMinRadius(void *event);
F32 fmodEventGetMaxRadius(void *event);

U32 fmodGetProjects(void ***projects);
U32 fmodProjectGetGroups(void *project, void ***groups);
U32 fmodGroupGetGroups(void *project, void ***groups);
U32 fmodGroupGetEvents(void *g, void ***events);
U32 fmodGroupGetEventsWithPrefix(void *group, char *eventNamePrefix, void ***events);
U32 fmodEventGetEvents(void *event, void ***events);
U32 fmodGroupGetEventGroups(void *group, void ***eventGroups);

void fmodEventGetInfoEvent(void *event, void **event_info);

void fmodValidateBanks(void);
U32 fmodEventSystemGetEventMemUsage(void *event);				   
void fmodEventSystemCheckWavebanks(void);
S32 fmodEventSystemGetEventBySystemID(int sysid, void **event, void *userdata);
bool fmodEventSystemFreeEventData(void *event); // Returns false if it could not be freed (still loading)
// disabled:(GT)
//#if !_PS3
//void fmodEventSystemSetDSPAllocCallback(FMOD_DSP_ALLOCCALLBACK func);
//void fmodEventSystemSetDSPFreeCallback(FMOD_DSP_FREECALLBACK func);
//void fmodEventSystemSetDSPExecuteCallback(FMOD_DSP_EXECUTECALLBACK func);
//void fmodEventSystemSetDSPConnectionAllocCallback(FMOD_DSPCONNECTION_ALLOCCALLBACK func);
//void fmodEventSystemSetDSPConnectionFreeCallback(FMOD_DSPCONNECTION_ALLOCCALLBACK func);
//#endif
void fmodEventProjectSetUserData(void *project, void *userdata);
void fmodEventProjectGetUserData(void *project, void **userdata);
void fmodEventGroupSetUserData(void *group, void *userdata);
void fmodEventGroupGetUserData(void *group, void **userdata);

void fmodEventProjectGetName(void *project, char **name);
void fmodEventGroupGetName(void *group, char **name);
FMOD_RESULT fmodEventGetName(void *event, char **name);

U32 fmodProjectByName(const char *projectName, void **project);

int fmodEventGetFadeInTime(void *event);
void fmodEventSetFadeInTime(void *event, int val);

int fmodEventGetFadeOutTime(void *event);
void fmodEventSetFadeOutTime(void *event, int val);

U32 fmodEventGroupIsPlaying(void *group);

void FMOD_EventSystem_EventPause(void *event, U32 pause);

typedef int (*FMODMemoryReportFunc)(void *tracker, int origin, void* parent, void *object, const char* name);
void fmodDumpMemory(FMODMemoryReportFunc func);
//const char* fmodMemoryTrackerGetOriginStr(int origin);

U32 fmodEventIsValid(void *event);
F32 fmodEventGetLength(void *event);
//U32 fmodEventGetMemoryUsage(void *event);
//void* fmodEventGetMemTracker(void *event);
void* fmodEventGetChannelGroup(void *event);
void fmodEventConnectToDSP(void *event, void *dsp);
void fmodEventGetWavebankInfo(int *num, char ***array);
U32 fmodEventIsInfoOnly(void *event);

void* fmodChannelGroupCreate(void *userdata, const char *name);
void fmodChannelGroupFixup(void *channel_group);
void fmodChannelGroupDestroy(void **channel_group);
int fmodChannelGroupAddGroup(void *channel_group, void *add_group);
int fmodChannelGroupDisconnect(void *del_group);
void fmodChannelGroupSetVolume(void *channel_group, F32 volume);
F32 fmodChannelGroupGetVolume(void *channel_group);
U32 fmodChannelGroupAddDSP(void *channel_group, void* dsp);
U32 fmodChannelGroupGetNumGroups(void *channel_group);
void* fmodChannelGroupGetDSPHead(void *channel_group);

U32 fmodGetGroupNamesFromProject(const char *projectName, char ***groupNames);
U32 fmodProjectGetLeafGroupStash(void *project, StashTable stashTable);

void fmodDSPSetBypass(void *dsp, bool val);
void fmodDSPGetBypass(void *dsp, bool *val);

U32 fmodDSPCreateWithType(int type, void **dspOut, void *userdata);
U32 fmodSystemAddDSP(void *dsp, void **dspConnection);

void* fmodDSPCreateFromInfo(SoundDSP *dspinfo, void *inst_userdata);
void* fmodDSPCreateCopy(void *orig, void *inst_userdata);
void* fmodDSPGetSystemHead(void);
//void* fmodDSPGetSystemCGMixTarget(void);
void fmodDSPSetValuesByInfo(SoundDSP *dspinfo, void *dsp);
void fmodDSPFree(void **dsp, bool bHasInstanceUserData);
void fmodDSPConnectToDSP(void *source, void *target);
void fmodDSPAddDSP(void *target, void *source, void **connOut);
U32 fmodDSPFindDSP(void *target, void *source, void **connOut);
void fmodDSPGetOutputs(void *dsp, void ***dsps, void ***conns);
/*
void fmodDSPSetUserData(void *dsp, void *userdata);
void* fmodDSPGetUserData(void *dsp);
*/
void fmodDSPGetChildren(void *dsp, void ***dsps);
void* fmodDSPGetOutputConn(void *dsp);

#define fmodDSPConnectionSetVolume(conn, vol) fmodDSPConnectionSetVolumeEx((conn), (vol), __FILE__, __LINE__)
void fmodDSPConnectionSetVolumeEx(void *dsp_conn, F32 volume, char *file, int line);
F32 fmodDSPConnectionGetVolume(void *dsp_conn);
void fmodDSPConnectionDisconnect(void *dsp_conn);
void* fmodDSPConnectionGetOutput(void *dsp_conn);

void fmodFlushDSPConnectionRequests(void);

void fmodClearPerMapFlags(void);
void fmodPreProcessEvents(void);

void fmodChannelGroupSetMute(void *channel_group, bool mute);


U32 FMOD_EventSystemCreateEventReverb(void **eventReverb);
U32 FMOD_EventSystemGetReverbProperties(void *properties);

U32 FMOD_EventReverbSet3DAttributes(void *eventReverb, Vec3 pos, F32 minDistance, F32 maxDistance);
U32 FMOD_EventReverbGet3DAttributes(void *eventReverb, Vec3 *pos, F32 *minDistance, F32 *maxDistance);
U32 FMOD_EventReverbGetActive(void *eventReverb, bool *active);
U32 FMOD_EventReverbSetActive(void *eventReverb, bool active);

void FMOD_SetDriver(int driverIndex);

// valid range -60 to 0dB
int fmodEventSetReverbDryLevel(void *event, F32 level);
F32 fmodEventGetReverbDryLevel(void *event);

// valid range -60 to 0dB
int fmodEventSetReverbWetLevel(void *event, F32 level);
F32 fmodEventGetReverbWetLevel(void *event);

U32 fmodDSPGetNumParameters(void *dsp, int *numParams);
U32 fmodDSPGetParameter(void *dsp, int index, float *value, char *valueStr, int valueStrLen);
U32 fmodDSPGetParameterInfo(void *dsp, int index, char *name, char *label, char *descrption, int descriptionLen, float *min, float *max);

void fmodDSPSetupSfxReverb(DSP_SfxReverb *sfx, void *dsp);
U32 fmodEventGetPropertyByName(void *event, const char *propertyName, void *value, bool thisInstanceOnly);

FMOD_RESULT FMOD_EventSystem_FindEventGroupByName(const char *eventGroupName, void **eventGroup);

FMOD_SYSTEM *fmodGetSystem(void);

const char *fmodGetErrorText(FMOD_RESULT result);

U32 fmodSetDSPUserData(void *dsp, void *userdata);
U32 sndAnalysisCreateDSP(void *userData, void **dspOut);

extern unsigned int gMemAllocated;
extern unsigned int gNumAllocations;

C_DECLARATIONS_END

#endif
