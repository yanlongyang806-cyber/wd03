/***************************************************************************



***************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>


#include "soundLib.h"
#include "sndLibPrivate.h"
#include "sndVoice.h"
#include "SndLibPrivate_h_ast.h"

#include "fileUtil.h"
#include "FolderCache.h"
#include "GfxConsole.h"
#include "osdependent.h"
#include "stringcache.h"
#include "ScratchStack.h"
#include "mathutil.h"
#include "partition_enums.h"

#include "globaltypes.h"
// From UtilitiesLib, to set position
// to call all FX shortly before updating events
// Line dist

// From WorldLib, for geometry loading/occlusion
#include "entEnums.h"
#include "WorldLib.h"
#include "Materials.h"
#include "wlPhysicalProperties.h"
// For wlGetTime
#include "wlTime.h"
// For Volume query caches
#include "wlVolumes.h"
#include "wlModelInline.h"

#include "GraphicsLib.h"

#include "ResourceInfo.h"

#include "StashTable.h"

#include "sndMission.h"
#include "ResourceManager.h"
#include "sndQueue.h"
#include "sndAnim.h"
#include "wlPerf.h"

DictionaryHandle g_hSoundDefaultsDict;

#ifdef STUB_SOUNDLIB

	typedef struct SoundMixer SoundMixer;

	typedef struct SoundDebugState { 
		int stub;
	} SoundDebugState;

	typedef struct SoundSourceClusters { 
		int stub;
	} SoundSourceClusters;

#else

	#include "event_sys.h"
	#include "sndSource.h"
	#include "sndConn.h"
	#include "sndSpace.h"
	#include "sndDebug2.h"
	#include "sndMusic.h"
	#include "fmod_event.h"
	#include "sndFade.h"
	#include "sndMemory.h"

	#include "sndCluster.h"

	#include "sndLOD.h"

	#include "sndMixer.h"
	#include "sndFx.h"

	#if _PS3
	#include "sndFmodStub.h"
	#endif

#endif

#define MIN_LISTENER_DIST_FROM_PLAYER 1.0f
#define MAX_LISTENER_DIST_FROM_PLAYER 15.0f

#define MAX_STREAM_LENGTH 2.5*60*1000
#define SND_MAX_CAM_SPEED 30

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("aSfxDsp.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_async.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_buckethash.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_channelgroupi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_channelpool.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_codec_fsb.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_codec_fsb5.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_codec_fsbvorbis.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dspi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dsp_chorus.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dsp_codecpool.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dsp_connectionpool.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dsp_echo.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dsp_flange.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dsp_pitchshift.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_dsp_resampler.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventcategoryi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventgroupi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventimpl_simple.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventimpl_complex.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_event_net_cmdqueue.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventparameteri.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventprojecti.h", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventsound.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventsystemi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_eventuserproperty.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_file.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_os_misc.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_output_dsound.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_output_emulated.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_output_nosound.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_output_software.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_output_wasapi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_pluginfactory.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_profile.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_profile_channel.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_profile_codec.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_profile_cpu.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_profile_dsp.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_reverbi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_simplemempool.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_soundbank.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_string.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("fmod_systemi.cpp", BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddMapping("MeteredSection.cpp", BUDGET_Audio););

extern bool gbNoGraphics;

SoundDebugState g_audio_dbg;

StashTable dspInstances = 0;
StashTable geoInstances = 0;
StashTable replayStash = 0;
StashTable externalSounds = 0;
StashTable sndProjectStash = 0;
StashTable emdStash = 0;  // key is from FMOD_EventSystem_GetSystemID
StashTable gEventToProjectStash = 0;

GlobalParam **globalParams = NULL;
//char **fevList = 0;
StashTable eventLRUStack = 0;

Listener listener;
bool snd_crossfade = 0;
int soundBufferSize = 1024*1024*80; // 80MB

SoundSourceClusters gSndSourceClusters;
SoundMixer *gSndMixer = NULL;

AudioState		g_audio_state = {0};
SoundSpaceState space_state = {0};

VoidIntFunc g_SoundOptionsOutputCB;
VoidVoidFunc g_SoundOptionsOutputDevicesCB;

void sndPrintEvents(char *group);
void sndClearMusic(void);
void sndConnGetDir(SoundSpaceConnector *conn, Vec3 dirOut);
void sndUpdateInactiveFade(void);

#define EVENT_UI_HEIGHT 16

#define DEBUG_SOUND

#ifdef DEBUG_SOUND_TIME
#define OSTS		int __start, __time1, __time2;
#define T0			__time2 = __start = timeGetTime(); \
	__time1 = __time2;
#define TI			__time2 = timeGetTime(); \
	conPrintfUpdate("%s %d: %d", __FUNCTION__, __LINE__, __time2-__time1); \
	__time1 = __time2;
#define TN			__time2 = timeGetTime(); \
	conPrintfUpdate("%s %d: %d", __FUNCTION__, __LINE__, __time2-__time1); \
	conPrintfUpdate("Total %s: %d", __FUNCTION__, __time2 - __start);
#else
#define OSTS		
#define T0			
#define TI			
#define TN
#endif

bool sndIsEventDisabled(const char *name);




#ifdef STUB_SOUNDLIB

//AUTO_STARTUP(Sound_Options);
//void sndSetupOptionsDummy(void) { }
//
//int ignoreX64check = 0;
//AUTO_CMD_INT(ignoreX64check, ignoreX64check) ACMD_ACCESSLEVEL(0) ACMD_COMMANDLINE;
//
//AUTO_STARTUP(GCLSound);
//void fakeGCLSound(void) { }
//
//AUTO_STARTUP(Sound) ASTRT_DEPS(Sound_Options, GCLSound);
//void sndInit(void) { }


void sndLoadDefaults() {}
void sndLibOncePerFrame(F32 elapsed) { }
void sndUpdate(Vec3 pos, Vec3 vel, Vec3 forward, Vec3 up) { }
void sndFadeInType(SoundType type) { }
void sndFadeOutType(SoundType type) { }
F32* sndGetVolumePtrByType(SoundVolumes *volumes, SoundType type) { return NULL; }
F32 sndGetVolumeByType(SoundVolumes *volumes, SoundType type) { return 0; }
void sndSetupDefaultOptionsVolumes(void) { }
U32 sndGetGroupNamesFromProject(const char *projectName, char ***groupNames) { return 0; }
U32 sndGetVoiceSetNamesForCategory(const char *pathToVoiceSet, const char *categoryName, char ***outNames) { return 0; }
//void sndInit(void) { }
int sndEnabled(void) { return 0; }
void sndGamePlayEnter(void) { }
void sndGamePlayLeave(void) { }
void sndShutdown(void) { }
void sndSetLanguage(const char *language) {}
const char *sndGetLanguage() {}
void sndLoadBank(const char *rel_name, const char *full_name) { return false; }
SoundSource* sndPlayAtCharacter(const char *event_name,	const char* filename, U32 entRef, sndCompleteCallback cb, void *userdata) { return NULL; }
SoundSource* sndPlayFromEntity(const char *event_name, U32 entRef, const char *filename, bool failQuietly) { return NULL; }
SoundSource* sndPlayAtPosition(const char *event_name, F32 x, F32 y, F32 z, 
							   const char *filename, U32 entRef, sndCompleteCallback cb, void *userdata, bool failQuietly) { return NULL; }
void sndSetContactDialogSource(SoundSource *source) { }
bool sndIsContactDialogSourceAudible(void) {return false;}
//void sndMusicPlayUI(const char *event_name, const char *filename) { }
//void sndMusicPlayWorld(const char *event_name, const char *filename) { }
//void sndMusicPlayRemote(const char *event_name, const char *filename, U32 entRef) { }
//void sndMusicClearUI(void) { }
//void sndMusicClearWorld(void) { }
//void sndMusicClearRemote(void) { }
int sndEventGetLength(const char* event_name) { return -1; }
U32 sndEventIsOneShot(const char* event_name) { return 0; }
U32 sndEventIsStreamed(void *info_event) { return 0; }
U32 sndEventIsVoice(void *info_event) { return 0; }
void sndGetEventName(void *event, char **name) { }
//void sndStopOneShot(const char *event_name) { }
void sndKillSourceIfPlaying(SoundSource *source, bool immediate) { }
void sndSetActiveOpenMissionCallback(SoundLibActiveOpenMissionCallback cb) { }
void sndSetVelCallback(SoundLibVecCallback cb) { }
void sndSetPlayerInCombatCallback(SoundLibPlayerInCombatCallback cb) { }
void sndSetCameraMatCallback(SoundLibMatCallback cb) { }
void sndSetCutsceneCropDistanceScalar(F32 val) { }
void sndSetPlayerMatCallback(SoundLibMatCallback cb) { }
void sndSetPlayerExistsCallback(SoundLibIntRetCallback cb) { }
void sndSetIsLoginCallback(SoundLibIntRetCallback cb) { }
void sndSetCutsceneActiveCallback(SoundLibIntRetCallback cb) { }
void sndSetEditorActiveFunc(SoundLibBoolRetCallback cb) { }
void sndSetEntPositionCallback(SoundLibEntPositionCallback cb) { }
void sndSetGetEntityNameCallback(SoundLibGetEntityName cb) { }
U32 sndGetEventList(char*** earrayOut, char* group) { return 0; }
int sndGetDebugLevel(void) { return 0; }
void sndReloadAll(const char *relpath) { }
F32 sndGetEventRadius(const char *event_name) { return 0; }
U32 sndEventExists(const char* event_name) { return 1; }
char ***sndGetEventListStatic(void) { return NULL; }
const char ***sndGetDSPListStatic(void) { return NULL; }
void sndPrintError(int error, char *file, int line) { }
int sndGetFXGuid(void) { return 0; }
void sndGetEventsFromEventGroupPath(const char *groupPath, void ***events) { }
void sndGetEventsWithPrefixFromEventGroupPath(const char *groupPath, char *eventNamePrefix, void ***events) { }
void sndSetLastTargetEntRef(U32 entRef) { }
U32 sndLastTargetEntRef() { return 0; }
void sndSetLastTargetClickCount(int clicks) { }
int sndLastTargetClickCount() { return 0; }
void sndSetLastTargetSoundSource(SoundSource *source) { }
SoundSource* sndLastTargetSoundSource() { return NULL; }
void sndSetLastTargetTimestamp(unsigned long timestamp) { }
unsigned long sndLastTargetTimestamp() { return 0; }
void sndEventDecLRU(void *event) { }
void sndChangeDriver(int driverIndex) { }
void sndGetDriverNames(char ***eaDriverNames) { }
void sndDebug2Draw(void) { }
SoundSource* sndPlayUIAudio(const char *pchSound, const char *pchFileContext) { return NULL; }
bool sndGetProjectFileByEvent(const char *pchEventPath, char **ppchFilePath) { return false; }
SoundSource* sndPlayRandomPhraseWithVoice(const char *pchPhrasePath, const char *pchVoicePath, U32 entRef) { return NULL; }

//
// private methods
//
void sndGetListenerPosition(Vec3 pos) {}
void sndGetListenerPanPosition(Vec3 pos) {}
void sndGetListenerRotation(Vec3 rot) {}
void sndGetListenerSpacePos(Vec3 pos) {}
void sndGetPlayerPosition(Vec3 pos) {}
void sndGetPlayerRotation(Vec3 rot) {}
void sndGetPlayerVelocity(Vec3 vel) {}
void sndGetCameraPosition(Vec3 pos) {}
void sndGetCameraRotation(Vec3 rot) {}
static SoundSource* sndStopRemoteEx(const char *event_name, bool bCleanup, bool bImmediate);
SoundSource* sndStopRemote(const char *event_name) {}
//void sndPlayRemote3dV2(const char *event_name, float x, float y, float z, const char *filename, U32 entRef) {}
//void sndPlayRemoteV2(const char *event_name, const char *filename, U32 entRef) {}
void sndDisableSound(void) {}
void sndEnableSound(void) {}
void sndOptionsSetCallbacks(VoidIntFunc output) {}
void sndOptionsDataSetCallbacks(VoidVoidFunc outputDevices) {}
void sndDeviceListsChanged(void) {}
void* sndGetDSP(const char *name, void *user_data) { return NULL; }
EventMetaData *sndFindMetaData(void *fmod_event) { return NULL; }
//void sndPrintPlaying(void) {}
void sndGetPlayingSources(SoundSource ***sources) {}
void sndCritterClean(U32 entRef) {}
int sndGetColor(int type) { return 0; }

void sndSetUISkinName(const char *uiSkinName) { }
char* sndUISkinName() { return NULL; }
bool sndEventGroupExists(char *fullEventGroupPath) { return false; }

#else

void sndSetUISkinName(const char *uiSkinName)
{
	if(g_audio_state.uiSkinName)
	{
		free(g_audio_state.uiSkinName);
	}
	g_audio_state.uiSkinName = strdup(uiSkinName);
}

char* sndUISkinName()
{
	return g_audio_state.uiSkinName;
}

bool sndEventGroupExists(char *fullEventGroupPath)
{
	void *group = NULL;

	return fmodEventSystemGetGroup(fullEventGroupPath, &group) == FMOD_OK;
}


int sndGetDebugLevel(void)
{
	return g_audio_state.debug_level;
}

void sndSetCameraMatCallback(SoundLibMatCallback cb)
{
	g_audio_state.camera_mat_func = cb;
}

void sndSetVelCallback(SoundLibVecCallback cb)
{
	g_audio_state.player_vel_func = cb;
}

void sndSetPlayerInCombatCallback(SoundLibPlayerInCombatCallback cb)
{
	g_audio_state.player_in_combat_func = cb;
}

void sndSetActiveOpenMissionCallback(SoundLibActiveOpenMissionCallback cb)
{
	g_audio_state.active_open_mission_func = cb;
}

void sndSetGetEntityNameCallback(SoundLibGetEntityName cb)
{
	g_audio_state.get_entity_name_func = cb;
}

void sndSetEntityTalkingCallback(SoundLibEntityTalking cb)
{
	g_audio_state.ent_talking_func = cb;
}

void sndSetAccountIDToEntRefCallback(SoundLibAccountToEntRef cb)
{
	g_audio_state.account_to_entref_func = cb;
}

void sndSetVoiceVerifyCallback(VoidVoidFunc cb)
{
	g_audio_state.verify_voice_func = cb;
}

void sndSetEntPositionCallback(SoundLibEntPositionCallback cb)
{
	g_audio_state.get_entity_pos_func = cb;
}

void sndSetPlayerMatCallback(SoundLibMatCallback cb)
{
	g_audio_state.player_mat_func = cb;
}

void sndSetPlayerExistsCallback(SoundLibIntRetCallback cb)
{
	g_audio_state.player_exists_func = cb;
}

void sndSetIsLoginCallback(SoundLibIntRetCallback cb)
{
	g_audio_state.is_login_func = cb;
}

void sndSetPlayerVoiceInfoCallback(SoundLibVoiceInfoCallback cb)
{
	g_audio_state.player_voice_func = cb;
}

void sndSetCutsceneActiveCallback(SoundLibIntRetCallback cb)
{
	g_audio_state.cutscene_active_func = cb;
}

void sndSetEditorActiveFunc(SoundLibBoolRetCallback cb)
{
	g_audio_state.editor_active_func = cb;
}

int sndEventGetLength(const char* event_name)
{
	void *fmod_event = NULL;
	FmodEventInfo fmod_info = {0};

	FMOD_EventSystem_GetEventInfoOnly(event_name, &fmod_event);

	if(fmod_event)
	{
		fmodGetFmodEventInfo(fmod_event, &fmod_info);
		return fmod_info.lengthms;
	}

	return -1;
}

U32 sndEventIsOneShot(const char* event_name)
{
	int loop = 0;
	void *fmod_event = NULL;

	FMOD_EventSystem_GetEventInfoOnly(event_name, &fmod_event);

	if(fmod_event)
	{
		FMOD_EventSystem_EventIsLooping(fmod_event, &loop);
	}

	return !loop;
}

U32 sndEventIsPlaying(const char* event_name)
{
	void *fmod_event = NULL;
	FmodEventInfo fmod_info = {0};

	FMOD_EventSystem_GetEventInfoOnly(event_name, &fmod_event);

	if(fmod_event)
	{
		fmodGetFmodEventInfo(fmod_event, &fmod_info);
		return fmod_info.instancesactive > 0;
	}

	return false;
}

U32 sndEventIsStreamed(void *info_event)
{
	if(info_event)
	{
		EventMetaData *emd = sndFindMetaData(info_event);

		if(emd)
		{
			return emd->streamed;
		}
	}

	return 0;
}

U32 sndEventIsVoice(void *info_event)
{
	if(info_event)
	{
		EventMetaData *emd = sndFindMetaData(info_event);

		if(emd)
		{
			return emd->type==SND_VOICE;
		}
	}

	return 0;
}

F32 sndGetEventRadius(const char *event_name)
{
	void *e = NULL;
	F32 radius = 0.0000;

	FMOD_EventSystem_GetEventInfoOnly(event_name, &e);
	FMOD_EventSystem_EventGetRadius(e, &radius);
	return radius;
}

typedef struct EventRefCount {
	int refs;
	int ticks;
	int lastTagged;
} EventRefCount;

static FMOD_RESULT F_CALLBACK sndEventLRUStart(void *event, int type, void *p1, void *p2, void *data)
{
	int id;
	EventRefCount *ref;
	char *name = NULL;
	if(!eventLRUStack)
	{
		eventLRUStack = stashTableCreateInt(32);
	}
	FMOD_EventSystem_GetSystemID(event, &id);

	fmodEventGetName(event, &name);

	if(stashIntFindPointer(eventLRUStack, id, &ref))
	{
		ref->refs++;
	}
	else
	{
		ref = callocStruct(EventRefCount);
		stashIntAddPointer(eventLRUStack, id, ref, 1);

		ref->refs = 1;
	}

	sndMixerTrackEvent(gSndMixer, event);

	//printf("Start %s: event: %p ref +1 = %d\n", name, event, ref->refs);

	//sndPrintf(1, SNDDBG_MEMORY | SNDDBG_EVENT, "%s: ref +1 = %d\n", name, ref->refs);

	return FMOD_OK;
}

void sndEventDecLRU(void *event)
{
	int id;
	EventRefCount *ref;
	char *name = NULL;
	if(!eventLRUStack)
	{
		return;
	}
	FMOD_EventSystem_GetSystemID(event, &id);

	if(stashIntFindPointer(eventLRUStack, id, &ref))
	{
		if(ref->refs > 0)
		{
			ref->refs--;

			if(ref->refs<=0)
			{
				ref->ticks = timerCpuTicks();
			}

			fmodEventGetName(event, &name);
			//sndPrintf(1, SNDDBG_MEMORY | SNDDBG_EVENT, "%s: ref - 1 = %d\n", name, ref->refs);

			//printf("Stop %s: event: %p ref +1 = %d\n", name, event, ref->refs);
			
		}
	}

	sndMixerStopTrackingEvent(gSndMixer, event);
}

static FMOD_RESULT F_CALLBACK sndEventLRUStop(void *event, int type, void *p1, void *p2, void *data)
{
	sndEventDecLRU(event);

	return FMOD_OK;
}

static void reloadFEVCallback(const char *relpath, int when)
{
	//sndReloadAll(relpath);
}

static void reloadFSBCallback(const char *relpath, int when)
{
	//sndReloadAll(relpath);
}

static void reloadSoundDefaultsCallback(const char *relpath, int when)
{
	ParserReloadFileToDictionary(relpath, g_hSoundDefaultsDict);
}

static void reloadDSPCallback(const char *relpath, int when)
{
	ParserReloadFileToDictionary(relpath, space_state.dsp_dict);
}

FileScanAction sndEventFileLoader(char* dir, struct _finddata32_t* data, void *pUserData)
{
	int ret = FSA_EXPLORE_DIRECTORY;

	//return ret;

	if(strEndsWith(data->name, ".fev") && data->name[0]!='x' && data->name[0]!='_')
	{
		char *file_copy = (char*)malloc(sizeof(char)*MAX_PATH);
		char full_name[MAX_PATH];
		strcpy_s(file_copy, MAX_PATH, data->name);
		sprintf(full_name, "%s/%s", dir, data->name);
		//eaPush(&fevList, file_copy);
		sndLoadBank(data->name, full_name);

		return ret;
	}

	return ret;
}

void sndGetPlayingSources(SoundSource ***sources)
{
	int i;
	void **events = NULL;

	fmodEventSystemGetPlaying(&events);

	for(i=0; i<eaSize(&events); i++)
	{
		void *e = events[i];
		SoundSource *source = NULL;

		fmodEventGetUserData(e, &source);
		if(source)
		{
			eaPush(sources, source);
		}
	}
}

EventMetaData *sndFindMetaData(void *fmod_event)
{
	EventMetaData *data = NULL;
	int id = -1;
	int result;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	devassert(fmod_event);
	result = FMOD_EventSystem_GetSystemID(fmod_event, &id);

	if(result)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	stashIntFindPointer(emdStash, id, &data);
	PERFINFO_AUTO_STOP();

	return data;
}

static void freePointer(void* value)
{
	free(value);
}

void sndChangeDriver(int driverIndex)
{
	if(!g_audio_state.noaudio)
	{
		FMOD_SetDriver(driverIndex);
	}
}

void sndGetDriverNames(char ***eaDriverNames)
{
	SoundDriverInfo *soundDriverInfo;
	int i;

	for(i = 0; i < eaSize(&g_audio_state.soundDrivers); i++)
	{
		soundDriverInfo = g_audio_state.soundDrivers[i];

		eaPush(eaDriverNames, soundDriverInfo->drivername);
	}
}

EventMetaData* sndValidateEvent(const char *name, void *fmod_event)
{
	FMOD_EVENTGROUP *group = NULL;
	FMOD_EVENTPROJECT *project = NULL;
	EventMetaData *emd = NULL;
	FMOD_EVENT_INFO info = {0};
	int use_dsp = 1;
	int id = -1;
	int is2d = 0;
	int allow2d = 0;
	int fade2d = 0;
	int ignore3d = 0;
	int ignorePosition = 0;
	int music = 0;
	F32 volume = 0;
	int playAsGroup = 0;
	int moving = 0;
	int ignoreLOD = 0;
	int alwaysAssignVoice = 0;
	int duckable = 0;
	int conflictPriority = 0;
	int clickie = 0;
	int queueGroup = 0;
	int notification = 0;
	int animate = 0;
	FMOD_MODE rolloff = 0;

	devassert(fmod_event);

	ZeroStruct(&info);
	emd = callocStruct(EventMetaData);

	FMOD_EventSystem_GetSystemID(fmod_event, &id);

	info.maxwavebanks = NUM_WAVEBANK_INFOS;
	info.wavebankinfo = wavebankinfos;
	FMOD_Event_GetInfo(fmod_event, NULL, NULL, &info);
	FMOD_Event_GetParentGroup(fmod_event, &group);

	if(group)
	{

		FMOD_EventGroup_GetParentProject(group, &project);

		FMOD_EventGroup_GetProperty(group, "PlayAsGroup", &playAsGroup);
		
		if(project)
		{
			char *projname = NULL;
			FMOD_EVENT_PROJECTINFO projinfo = {0};

			FMOD_EventProject_GetInfo(project, &projinfo);

			projname = projinfo.name;
			if(strstri(projname, "Amb"))
			{
				emd->type = SND_AMBIENT;
			}
			else if(strstri(projname, "Voice") || strstri(projname, "Warcry"))
			{
				emd->type = SND_VOICE;
			}
			else if(strstri(projname, "Music"))
			{
				emd->type = SND_MUSIC;
			}
			else if(strstri(projname, "Test") || strstri(projname, "Demo"))
			{
				emd->type = SND_TEST;
			}
			else if(!stricmp(projname, "UI"))
			{
				emd->type = SND_UI;
			}
			else if(strstri(projname, "Powers"))
			{
				emd->type = SND_FX;
			}
			else if(strstri(projname, "Notification"))
			{
				emd->type = SND_NOTIFICATION;
			}
			else
			{
				emd->type = SND_FX;
			}
			stashFindPointer(sndProjectStash, projname, &emd->project_filename);
		}
	}

	if(emd->type==SND_VOICE)
	{
		// Find exclusivity
		while(group)
		{
			int ex = 0;
			char *group_name = NULL;
			int exclusivity = 0;
			void *new_group = NULL;
			static StashTable snd_exclusivity_stash = NULL;
			fmodEventGroupGetName(group, &group_name);

			if(!snd_exclusivity_stash)
			{
				snd_exclusivity_stash = stashTableCreateAddress(10);
			}

			if(group_name && stashAddressFindInt(snd_exclusivity_stash, group, &exclusivity))
			{
				emd->exclusivity_group = exclusivity;
				emd->exclusive_group = group_name;
				break;
			}

			if(FMOD_EventSystem_GroupGetProperty(group, "Exclusive", &ex)==FMOD_OK)
			{
				static int exclusivity_counter = 1;
				stashAddressAddInt(snd_exclusivity_stash, group, exclusivity_counter, 1);
				emd->exclusivity_group = exclusivity_counter;
				emd->exclusive_group = group_name;

				exclusivity_counter++;
				break;
			}

			FMOD_EventGroup_GetParentGroup((FMOD_EVENTGROUP*)group, (FMOD_EVENTGROUP**)&new_group);
			if(!new_group)
			{
				break;
			}
			group = new_group;
		}
	}

	

	FMOD_Event_GetProperty(fmod_event, "EnableDSP", &use_dsp, 0);
	emd->use_dsp = !!use_dsp || emd->type==SND_MUSIC;

	FMOD_Event_GetProperty(fmod_event, "2DAllowed", &allow2d, 0);
	emd->allow2d = !!allow2d;

	FMOD_Event_GetProperty(fmod_event, "2DFade", &fade2d, 0);
	emd->fade2d = !!fade2d;

	if(FMOD_Event_GetProperty(fmod_event, "Animate", &animate, 0) == FMOD_OK)
	{
		emd->animate = !!animate;
	}
	else
	{
		emd->animate = 1; // default to on
	}
	

	FMOD_Event_GetProperty(fmod_event, "Notification", &notification, 0);
	if(notification)
	{
		emd->type = SND_NOTIFICATION;
	}
	

	// treat as head-relative 
	// when the sound is triggered, the initial relative position will be determined
	// and maintained for the duration of the sound regardless of what the listener does
	FMOD_Event_GetProperty(fmod_event, "clickie", &clickie, 0);
	emd->clickie = !!clickie;

	// Do not take position into account when determining volume
	FMOD_Event_GetProperty(fmod_event, "IgnorePosition", &ignorePosition, 0);
	emd->ignorePosition = !!ignorePosition;

	// Ignore the LOD system - play anyway
	FMOD_Event_GetProperty(fmod_event, "IgnoreLOD", &ignoreLOD, 0);
	emd->ignoreLOD = !!ignoreLOD;

	// Even if there isn't enough room on the mixer, play this sound
	FMOD_Event_GetProperty(fmod_event, "AlwaysAssignVoice", &alwaysAssignVoice, 0);
	emd->alwaysAssignVoice = !!alwaysAssignVoice;

	// Determines whether a sound originating from a DynFx may be 'ducked' 
	// (at this point, only sounds originating from entities other than the player will be ducked)
	FMOD_Event_GetProperty(fmod_event, "Duckable", &duckable, 0);
	emd->duckable = !!duckable;

	// treat a "normal" 3d event as music
	// such that it does not conflict with other music
	FMOD_Event_GetProperty(fmod_event, "Music", &music, 0);
	emd->music = !!music;

	FMOD_Event_GetProperty(fmod_event, "Ignore3d", &ignore3d, 0);
	emd->ignore3d = !!ignore3d || emd->type==SND_MUSIC;

	if(!FMOD_Event_GetProperty(fmod_event, "ConflictPriority", &conflictPriority, 0))
	{
		emd->conflictPriority = conflictPriority;
	}
	else
	{
		emd->conflictPriority = 0; // make sure it defaults to 0
	}
	
	if(!FMOD_Event_GetProperty(fmod_event, "Queue", &queueGroup, 0)) 
	{
		emd->queueGroup = queueGroup;
	}
	else
	{
		emd->queueGroup = 0;
	}

	if(!FMOD_Event_GetProperty(fmod_event, "Moving", &moving, 0)) 
	{
		emd->moving = !!moving;
	}
	else
	{
		emd->moving = -1; // always play regardless of whether source is moving
	}

	emd->priority = fmodEventGetPriority(fmod_event);
	
	// for music events (belonging to a group) that need to be played simultaneously 
	// (e.g., xfading boss music for multiple health states)
	emd->playAsGroup = !!playAsGroup;

	if(info.maxwavebanks)
	{
		int i = 0;
		char fullfsbname[MAX_PATH];

		for(i=0; i<info.maxwavebanks; i++)
		{
			FMOD_EVENT_WAVEBANKINFO *wbinfo = &wavebankinfos[i];
			//U32 stream = !!(wbinfo->mode & FMOD_CREATESTREAM);
			U32 stream = wbinfo->type == 0;
			
			
			sprintf(fullfsbname, "%s/%s.fsb", fmodGetMediaPath(), wbinfo->name);

			if(!fileExists(fullfsbname))
				ErrorFilenamef(emd->project_filename, "Event: %s references bank nonexistent bank: %s", 
						name, wbinfo->name);

			emd->streamed = stream;
		}
	}

	if(fmodEventSystemGetVolumeProperty(fmod_event)==0)
	{
		ErrorFilenameGroupRetroactivef(	emd->project_filename, "Audio", 10, 6, 13, 2008, 
			"Event: %s has zero base volume", 
			name);
	}

	// TODO:(GT) tmp disabled
	//if(emd->streamed && info.lengthmsnoloop>MAX_STREAM_LENGTH && !strstri(name, "Login"))
	//{
	//	ErrorFilenameGroupRetroactivef(	emd->project_filename, "Audio", 10, 3, 13, 2008, 
	//									"Streamed event: %s greater than 2.5 minutes", 
	//									name);
	//}

	//if(emd->streamed && !(emd->type==SND_AMBIENT || emd->type==SND_VOICE || emd->type==SND_MUSIC || emd->type==SND_TEST))
	//{
	//	ErrorFilenameGroupRetroactivef(	emd->project_filename, "Audio", 10, 3, 13, 2008, 
	//									"Streamed event: %s outside ambient/voice/music banks", 
	//									name);
	//}

	is2d = fmodEventIs2D(fmod_event);
	if(is2d && !(emd->type==SND_VOICE || emd->type==SND_NOTIFICATION || emd->type==SND_UI || emd->type==SND_MUSIC || emd->type==SND_TEST || emd->allow2d))
	{
		FMOD_EVENT_MODE threed = FMOD_3D;

		ErrorFilenameGroupRetroactivef(	emd->project_filename, "Audio", 10, 3, 28, 2008,
										"2D Event: %s outside of voice/ui/music banks!  This will be overwritten.",
										name);
		FMOD_Event_SetPropertyByIndex((FMOD_EVENT*)fmod_event, FMOD_EVENTPROPERTY_MODE, &threed, 0);
	}

	if(!is2d && FMOD_Event_GetPropertyByIndex((FMOD_EVENT*)fmod_event, FMOD_EVENTPROPERTY_3D_ROLLOFF, &rolloff, 0) == FMOD_OK)
	{
		if(rolloff == FMOD_3D_INVERSEROLLOFF)
		{
			ErrorFilenameGroupRetroactivef(emd->project_filename, "Audio", 10, 7, 25, 2012,
											"3D Event: %s has inverse rolloff", name);
		}
	}

	if(fmodEventIs2D(fmod_event))
	{
		emd->ignore3d = 1;
	}

	if(!emdStash)
	{
		emdStash = stashTableCreateInt(20);
	}

	stashIntAddPointer(emdStash, id, emd, 1);

	return emd;
}

U32 sndPreprocessEvent(char* name, void *e_g_c, SoundTreeType type, void *p_userdata, void *userdata, void **child_userdata)
{
	static void *last_project = NULL;
	if(type==STT_PROJECT)
	{
		last_project = e_g_c;
	}

	if(type==STT_EVENT)
	{
		char *eventPath = NULL;
		int four = 4;
		F32 one = 1;
		F32 zero = 0;
		EventMetaData *emd = NULL;
		StashTable eventPathStash = (StashTable)userdata;
		void *stashValue = NULL;
		const char *pool = NULL;

		// construct path
		fmodEventGetFullName(&eventPath, e_g_c, false);

		pool = allocAddString(eventPath);

		// Check for duplicates
		if(stashAddressFindPointer(gEventToProjectStash, pool, &stashValue))
		{
			char *project1 = NULL;
			char *project2 = NULL;

			fmodProjectGetFilename(last_project, &project2);
			fmodProjectGetFilename(stashValue, &project1);

			if(project1 && project2 && !stricmp(project1, project2))
			{
				ErrorFilenameGroupRetroactivef(	project2, "Audio", 10, 8, 28, 2009, 
					"Event %s in project %s with duplicate name in project %s", pool, project1, project2);
			}
		} 

		emd = sndValidateEvent(name, e_g_c);

		// we will handle looping events
		if(fmodEventIsLooping(e_g_c))
			FMOD_Event_SetPropertyByIndex((FMOD_EVENT*)e_g_c, FMOD_EVENTPROPERTY_MAX_PLAYBACKS_BEHAVIOR, &four, 0);

		if(emd->clickie)
		{
			int headRelative = 0x00040000; // FMOD_3D_HEADRELATIVE
			FMOD_Event_SetPropertyByIndex((FMOD_EVENT*)e_g_c, FMOD_EVENTPROPERTY_3D_POSITION, &headRelative, 0);
		}
		//fmodEventSetDopplerScale((FMOD_EVENT*)e_g_c, 1.0);   

		if(!emd->fade2d && !emd->ignorePosition)
			FMOD_Event_SetPropertyByIndex((FMOD_EVENT*)e_g_c, FMOD_EVENTPROPERTY_3D_PANLEVEL, &one, 0);
		else
			FMOD_Event_SetPropertyByIndex((FMOD_EVENT*)e_g_c, FMOD_EVENTPROPERTY_3D_PANLEVEL, &zero, 0);

		stashAddressAddPointer(gEventToProjectStash, pool, last_project, 1);

		estrDestroy(&eventPath);
	}

	return 1;
}

void sndPreprocessEvents(void)
{
	fmodValidateBanks();

	FMOD_EventSystem_TreeTraverse(STT_EVENT, sndPreprocessEvent, NULL, NULL);
}

void sndReloadAll(const char *relpath)
{
	int i;
	SoundSourceGroup **removedGroups = NULL;

	if(g_audio_state.noaudio)
	{
		return;
	}
	
	// Tells events in stop/start all to not free structures
	g_audio_state.d_reloading = 1;

	FMOD_EventSystem_ClearAll();

	sndMixerStopTrackingAllEvents(gSndMixer);
	sndMixerRemoveAllSources(gSndMixer);

	if(!gEventToProjectStash)
	{
		gEventToProjectStash = stashTableCreateAddress(1000);
	}

	eaClear(&g_audio_state.streamed_sources);
	eaClearEx(&g_audio_state.event_list, NULL);  // Don't destroy... it's a static list used by editors
	stashTableClear(g_audio_state.sndSourceGroupTable);
	stashTableClearEx(emdStash, NULL, freePointer);
	stashTableClearEx(sndProjectStash, NULL, freePointer);
	stashTableClear(gEventToProjectStash);
	
	// Load all data
	errorIsDuringDataLoadingInc(NULL);
#ifdef _XBOX
	fileScanAllDataDirs("sound/xbox360", sndEventFileLoader, NULL);
#elif _PS3
    // Use the same files as win32 for now
	fileScanAllDataDirs("sound/win32", sndEventFileLoader, NULL);
#else
	fileScanAllDataDirs("sound/win32", sndEventFileLoader, NULL);
#endif
	errorIsDuringDataLoadingDec();

	sndPreprocessEvents();

	// Restart all "should-be" playing events
	for(i=0; i<eaSize(&space_state.sources); i++)
	{
		SoundSource *source = space_state.sources[i];

		FMOD_EventSystem_GetEventInfoOnly(source->obj.desc_name, &source->info_event);

		source->stopped = 0;

		if(!source->dead && source->has_event)
		{
			source->has_event = 0;
			source->stopped = 0;
			eaFindAndRemoveFast(&source->group->active_sources, source);
			eaPush(&source->group->inactive_sources, source);
		}
		else if(source->dead)
		{
			source->stopped = 1;
		}
	}

	if(g_audio_dbg.debugging)
	{
		sndDebuggerRebuildEventTree();

		sndDebuggerHandleReload();
	}

	// Update all group info objects
	for(i=0; i<eaSize(&space_state.source_groups); i++)
	{
		SoundSourceGroup *group = space_state.source_groups[i];

		FMOD_EventSystem_GetEventInfoOnly(group->name, &group->fmod_info_event);

		if(!group->fmod_info_event)
		{
			eaPush(&removedGroups, group);
		}
		else
		{
			group->emd = sndFindMetaData(group->fmod_info_event);
			FMOD_EventSystem_GetSystemID(group->fmod_info_event, &group->fmod_id);

			stashIntAddPointer(g_audio_state.sndSourceGroupTable, group->fmod_id, group, 1);
		}
	}

	for(i=0; i<eaSize(&removedGroups); i++)
	{
		SoundSourceGroup *group = removedGroups[i];
		
		sndSourceGroupDestroy(group);
	}

	for(i=0; i<eaSize(&space_state.sources); i++)
	{
		SoundSource *source = space_state.sources[i];

		FMOD_EventSystem_GetEventInfoOnly(source->obj.desc_name, &source->info_event);
		source->emd = sndFindMetaData(source->info_event);
	}

	//for(i = 0; i < eaSize(&space_state.global_spaces); i++)
	//{
	//	SoundSpace *soundSpace = space_state.global_spaces[i];
	//	if(soundSpace->fmodEventReverb)
	//	{
	//		// the reverb has been destroyed, create a new one
	//		sndSpaceCreateEventReverb(soundSpace);
	//	}
	//}

	sndGetEventListStatic(); // Refresh static list

	if(relpath!=NULL)
	{
		wlStatusPrintf("Reloaded %s", relpath);
	}

	eaClear(&g_audio_state.streamed_sources);

	// Force panning to update
	space_state.needs_pan_update = 1;

	g_audio_state.d_reloading = 0;
}

void sndValidateTracker(const char* event_name, const char* tracker_handle, const char* layer_file)
{
	//printf("Validating: %s for %s ", tracker_handle, event_name);

	if(!fmodEventExists(event_name))
	{
		//printf("failed.\n");
		ErrorFilenameGroupf(layer_file, "Audio", 3, "Invalid SoundSphere Event: %s.", event_name);
		stashAddPointer(g_audio_state.disabled_spheres, event_name, (void*)1, 0);
	} else {
		//printf("passed.\n");
		stashRemovePointer(g_audio_state.disabled_spheres, event_name, NULL);	
	}
}

char ***sndGetEventListStatic(void)
{
	if(!g_audio_state.event_list || !eaSize(&g_audio_state.event_list))
	{
		sndGetEventList(&g_audio_state.event_list, NULL);
	}

	return &g_audio_state.event_list;
}

const char ***sndGetDSPListStatic(void)
{
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded() || !space_state.dsp_dict)
	{
		return NULL;
	}

	if(!g_audio_state.dsp_list)
	{
		int i;
		DictionaryEArrayStruct *dictstruct = resDictGetEArrayStruct(space_state.dsp_dict);

		for(i=0; i<eaSize(&dictstruct->ppReferents); i++)
		{
			SoundDSP *dsp = (SoundDSP*)dictstruct->ppReferents[i];

			eaPush(&g_audio_state.dsp_list, dsp->name);
		}
	}

	return &g_audio_state.dsp_list;
}

void* sndGetDSP(const char *name, void *inst_userdata)
{
	SoundDSP *dsp_info = (SoundDSP*)RefSystem_ReferentFromString(space_state.dsp_dict, name);
	void *dsp;

	if(!dsp_info)
	{
		return NULL;
	}

	dsp = fmodDSPCreateFromInfo(dsp_info, inst_userdata);

	//sndPrintf(1, SNDDBG_DSP | SNDDBG_MEMORY, "Created dsp: %p\n", dsp);
	
	return dsp;
}

static void sndInitParams(void)
{
	GlobalParam *gp;

	gp = callocStruct(GlobalParam);
	strcpy(gp->param_name, "timeofday");
	gp->value_func = wlTimeGet;

	eaPush(&globalParams, gp);
}

void sndDisableSound(void)
{
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
		return;

	FMOD_EventSystem_SetMute(1);
	fmodEventSystemUpdate();
}

void sndEnableSound(void)
{
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
		return;

	FMOD_EventSystem_SetMute(0);
	fmodEventSystemUpdate();
}

void sndOptionsSetCallbacks(VoidIntFunc output)
{
	g_SoundOptionsOutputCB = output;
}

void sndOptionsDataSetCallbacks(VoidVoidFunc outputDevices)
{
	g_SoundOptionsOutputDevicesCB = outputDevices;
}

void sndDeviceListsChanged(void)
{
	// update the voice system's capture & render device lists
	svDeviceListsChanged();

	// update the in-game audio device list
	fmodUpdateDriverInfo();
	g_SoundOptionsOutputDevicesCB();
	g_SoundOptionsOutputCB(g_audio_state.curDriver);
}

#endif

AUTO_STARTUP(Sound_Options);
void sndSetupOptionsDummy(void)
{
}


#ifndef STUB_SOUNDLIB
void sndSetupDefaultOptionsVolumes(void)
{
	SoundDefaults *pSoundDefaults = RefSystem_ReferentFromString(g_hSoundDefaultsDict, "Default");		
	if(pSoundDefaults)
	{
		g_audio_state.options.main_volume = pSoundDefaults->fMainVolume;
		g_audio_state.options.amb_volume = pSoundDefaults->fAmbVolume;
		g_audio_state.options.fx_volume = pSoundDefaults->fFxVolume;
		g_audio_state.options.music_volume = pSoundDefaults->fMusicVolume;
		g_audio_state.options.voice_volume = pSoundDefaults->fVoiceVolume;
		g_audio_state.options.ui_volume = pSoundDefaults->fUIVolume;
		g_audio_state.options.notification_volume = pSoundDefaults->fNotificationVolume;
		g_audio_state.options.video_volume = pSoundDefaults->fVideoVolume;
		g_audio_state.bMuteVOonContactEnd = pSoundDefaults->bMuteVOonContactEnd;
	}
	else
	{
		g_audio_state.options.main_volume = 0.85;
		g_audio_state.options.amb_volume = 0.7;
		g_audio_state.options.fx_volume = 1;
		g_audio_state.options.music_volume = 0.7;
		g_audio_state.options.voice_volume = 1;
		g_audio_state.options.ui_volume = 0.7;
		g_audio_state.options.notification_volume = 0.7;
		g_audio_state.options.video_volume = 0.8;
		g_audio_state.bMuteVOonContactEnd = 1;
	}
}

void sndSetupDefaults(void)
{
	g_audio_state.doppler = 1;
	g_audio_state.inited = 0;
	g_audio_state.muted = 0;
	g_audio_state.software = 1;
	g_audio_state.surround = 1;
	g_audio_state.d_playerOrient = 0;
	g_audio_state.d_playerPos = 0;
	g_audio_state.d_playerSpace = 0;
	g_audio_state.d_playerPanPos = 1;
	g_audio_state.d_cameraPos = 1;
	g_audio_state.ui_volumes.amb_volume = 0.7;
	g_audio_state.ui_volumes.fx_volume = 1;
	g_audio_state.ui_volumes.voice_volume = 1;
	g_audio_state.ui_volumes.music_volume = 0.7;
	g_audio_state.ui_volumes.main_volume = 0.85;
	g_audio_state.ui_volumes.ui_volume = .7;
	g_audio_state.ui_volumes.notification_volume = 0.7;
	g_audio_state.ui_volumes.video_volume = 0.8;
	g_audio_state.directionality_range = 30;
	g_audio_state.hrtf_range = 20;
}

static SoundSource* sndStopRemoteEx(const char *event_name, bool bCleanup, bool bImmediate)
{
	SoundSource *source = NULL;

	if(stashFindPointer(externalSounds, event_name, &source) && source)
	{
		source->needs_stop = 1;
		
		if(bCleanup)
		{
			source->clean_up = 1;
		}
		if(bImmediate)
		{
			source->immediate = 1;
		}
	}

	return source;
}

SoundSource* sndPlayRandomPhraseWithVoice(const char *pchPhrasePath, const char *pchVoicePath, U32 entRef)
{
	static void **events = NULL;
	SoundSource *pSoundSource = NULL;
	static char *pchSearchPrefix = NULL;

	estrPrintf(&pchSearchPrefix, "%s_", pchPhrasePath);
	eaClear(&events);

	// find any events at the path with the name prefix
	sndGetEventsWithPrefixFromEventGroupPath(pchVoicePath, pchSearchPrefix, &events);
	if(events && eaSize(&events))
	{
		char *eventName = NULL;

		void *chosenEvent = NULL;
		int numEvents = eaSize(&events);

		// pick a random one
		int choice = randInt(numEvents);
		chosenEvent = events[choice];

		if(chosenEvent)
		{
			static char *dstPath = NULL;

			sndGetEventName(chosenEvent, &eventName);

			estrPrintf(&dstPath, "%s/%s", pchVoicePath, eventName);

			//pSoundSource = sndPlayAtCharacter(dstPath, dstPath, -1, NULL, NULL);
			pSoundSource = sndPlayFromEntity(dstPath, entRef, dstPath, false);
		}
	}
	else
	{
		Errorf("Invalid voice (%s) phrase (%s) path", pchVoicePath, pchPhrasePath);
	}

	return pSoundSource;
}

AUTO_COMMAND ACMD_NAME(PlayRandomPhraseWithVoice);
void sndCmdPlayRandomPhraseWithVoice(const char* phrasePath, const char* voicePath)
{
	sndPlayRandomPhraseWithVoice(phrasePath, voicePath, -1);
}

static bool sndGetUIAudioNameAndPath(const char *pchSound, char* ppchDstPathOut)
{
	char *skinName;
	bool bFound = false;

	if (!pchSound)
		return false;

	if(skinName = sndUISkinName()) // NULL if no skin
	{
		char *eventName;

		// assumes the following form
		// <path>/<event>
		//
		// Example:
		// UI/Select
		//
		// Converts To:
		// ui_<skin name>/<event>

		strcpy_s(ppchDstPathOut, MAX_PATH, "ui_");
		strcat_s(ppchDstPathOut, MAX_PATH, skinName);

		// make sure we have an event group for the skin, otherwise, play default
		if(sndEventGroupExists(ppchDstPathOut))
		{
			// find event name
			if(eventName = strstr(pchSound, "/"))
			{
				strcat_s(ppchDstPathOut, MAX_PATH, eventName);

				bFound = sndEventExists(ppchDstPathOut);
			}
		}
	}

	if(!bFound)
	{
		// default path
		strcpy_s(ppchDstPathOut, MAX_PATH, pchSound);
	}

	return true;
}

SoundSource* sndPlayUIAudio(const char *pchSound, const char *pchFileContext)
{
	char pchDstPath[MAX_PATH];
	SoundSource *pSoundSource = NULL;
	
	if (sndGetUIAudioNameAndPath(pchSound, pchDstPath))
	{
		pSoundSource = sndPlayAtCharacter(pchDstPath, pchFileContext, -1, NULL, NULL);
	}
	return pSoundSource;
}

SoundSource* sndStopUIAudio(const char *pchSound)
{
	char pchDstPath[MAX_PATH];
	SoundSource *pSoundSource = NULL;

	if (sndGetUIAudioNameAndPath(pchSound, pchDstPath))
	{
		pSoundSource = sndStopRemoteEx(pchDstPath, true, true);
	}
	return pSoundSource;
}


void sndSetCutsceneCropDistanceScalar(F32 val)
{
	g_audio_state.fCutsceneCropDistanceScalar = CLAMP(val, 0.0, 1.0);
}


void sndGamePlayEnter(void)
{
	sndFadeInType(SND_AMBIENT);
	sndFadeInType(SND_VOICE);
	sndFadeInType(SND_FX);
	sndMusicClearUI();
}

void sndGamePlayLeave(void)
{
	sndFadeOutType(SND_AMBIENT);
	sndFadeOutType(SND_VOICE);
	sndFadeOutType(SND_FX);
}

void sndMapUnload(void)
{
	sndMusicClearWorld();	
	sndMusicClearRemote();
}

void sndLoadDefaults()
{
	// Load defaults
	g_hSoundDefaultsDict = RefSystem_RegisterSelfDefiningDictionary("SoundDefaults", false, parse_SoundDefaults, true, true, NULL);
	ParserLoadFilesToDictionary("defs/sound", "SoundDefaults.def", "SoundDefaults.bin", PARSER_CLIENTSIDE | PARSER_OPTIONALFLAG, g_hSoundDefaultsDict);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/sound/SoundDefaults.def", reloadSoundDefaultsCallback);
}

void sndGetSystemSpecs(void)
{
#if !PLATFORM_CONSOLE
	getDriverVersion(SAFESTR(system_specs.audioDriverVersion), "dsound.dll" );
	if(eaSize(&g_audio_state.soundDrivers))
	{
		g_audio_state.curDriver = CLAMP(g_audio_state.curDriver, 0, eaSize(&g_audio_state.soundDrivers)-1);

		sprintf(system_specs.audioDriverOutput, "%s", g_audio_state.soundDrivers[g_audio_state.curDriver]->drivername);
		if (strlen(system_specs.audioDriverOutput)>10 && system_specs.audioDriverOutput[strlen(system_specs.audioDriverOutput)-6]=='[' &&
			system_specs.audioDriverOutput[strlen(system_specs.audioDriverOutput)-1] == ']')
		{
			// ends in "[0123]" - some unique per-user value
			system_specs.audioDriverOutput[strlen(system_specs.audioDriverOutput)-7] = '\0';
		}
	}
	sprintf(system_specs.audioDriverName, "DirectSound");
	system_specs.fmodVersion = g_audio_state.fmod_version;
#endif

	systemSpecsUpdateString();
}

#endif

int ignoreX64check = 0;
AUTO_CMD_INT(ignoreX64check, ignoreX64check) ACMD_ACCESSLEVEL(0) ACMD_COMMANDLINE;


AUTO_STARTUP(GCLSound);
void fakeGCLSound(void)
{

}


AUTO_STARTUP(Sound) ASTRT_DEPS(Sound_Options, GCLSound);
void sndInit(void)
{
#ifdef STUB_SOUNDLIB
	loadstart_printf("No sound... ");
#else
	sndFxInit();
	sndQueueManagerInit();
	sndSetCutsceneCropDistanceScalar(1.0);

	g_audio_state.fxDuckScaleFactor = 1.0; // initialize the scale factor
	g_audio_state.uiSkinName = NULL;

	if(gbNoGraphics)
	{
		g_audio_state.noaudio = 1;
		return;
	}

	loadstart_printf("Initializing sound... ");

	if(g_audio_state.noaudio)
	{
		loadend_printf("skipping audio (-noaudio)");
		return;
	}

	fmodInitDriverInfo();
	sndGetSystemSpecs();
	
	if(IsUsingX64() && !IsUsingVista())
	{
		if(eaSize(&g_audio_state.soundDrivers) && 
			strstri(g_audio_state.soundDrivers[g_audio_state.curDriver]->drivername, "RealTek"))
		{
			if (!ignoreX64check)
			{
				loadend_printf("skipping audio (RealTek on Non-Vista WoW64)");
				g_audio_state.noaudio = 1;
				return;
			} else {
				system_specs.audioX64CheckSkipped = 1;
			}
		}
	}


#if !PLATFORM_CONSOLE
	//if(isDevelopmentMode() && UserIsInGroup("Audio"))
	//{
	//	//soundBufferSize = 60*1024*1024;
	//}
#endif

	FMOD_ErrCheck(fmodEventSystemInit());

	if(g_audio_state.noaudio)
	{
		loadend_printf("fmod init failed");
		return;
	}

	geoInstances = stashTableCreateAddress(20);
	fxSounds = stashTableCreateInt(50);
	replayStash = stashTableCreateAddress(8);
	externalSounds = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);	

	// Setup mixer
	if(!gSndMixer)
	{
		gSndMixer = sndMixerCreate();
	}
	sndMixerInit(gSndMixer);
	sndMissionInit();
	sndSpaceCreateAndRegisterNullSpace();
	g_SoundAnim = sndAnimCreate();


	g_audio_state.disabled_spheres = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);	

	//sndAnalysisCreateDSP(NULL, &g_audio_state.ampAnalysisDSP);

	// Yay callbacks!
	// For determining when sounds are added and removed (called in trackerOpen and trackerClose)
	// For adding geometry LODs to a stash so they can be checked async'ly
	worldLibSetSoundFunctions(	sndSphereCreate, 
								sndSphereDestroy, 
								sndValidateTracker, 
								sndGetEventRadius,
								NULL,
								sndSpaceCreateFromRoom,
								sndSpaceDestroy,
								sndConnCreate,
								sndConnDestroy,
								sndEventExists,
								sndGetProjectFileByEvent);

	// Setup the DynFx callbacks (e.g., Powers, Animation, etc..)
	sndFxSetupCallbacks();

	if (!g_audio_state.snd_volume_type)
		g_audio_state.snd_volume_type = wlVolumeQueryCacheTypeNameToBitMask("Player");

	g_audio_state.snd_volume_query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, g_audio_state.snd_volume_type, NULL);
	g_audio_state.snd_volume_portal_query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, g_audio_state.snd_volume_type, NULL);
	g_audio_state.snd_volume_playable_query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, g_audio_state.snd_volume_type, NULL);

	// Setup clustering
	sndClustersInit(&gSndSourceClusters, 10.0);

	// Assert sound kills
	assertSetSndCB(sndDisableSound);

	sndReloadAll(NULL);

	// For reloading of FEV and FSBs, but not implemented yet.
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "sound/*.fev", reloadFEVCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "sound/*.fsb", reloadFSBCallback);


	// Load dsps
	space_state.dsp_dict = RefSystem_RegisterSelfDefiningDictionary("SoundDSPs", false, parse_SoundDSP, true, true, NULL);
	ParserLoadFilesToDictionary("sound/dsps", ".fmod_dsp", "FMOD_DSPs.bin", PARSER_CLIENTSIDE | PARSER_OPTIONALFLAG, space_state.dsp_dict);
	dspInstances = stashTableCreateWithStringKeys(20, StashDefault);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "sound/dsps/*.fmod_dsp", reloadDSPCallback);

	g_audio_state.main_thread_id = GetCurrentThreadId();

	g_audio_state.inited = 1;
 
	sndInitParams();
	FMOD_EventSystem_AddCallback(NULL, 1, 1, fmodGetStartCBType(), sndSourceStartCB, NULL);
	FMOD_EventSystem_AddCallback(NULL, 1, 1, fmodGetEndCBType(), sndSourceStopCB, NULL);
	FMOD_EventSystem_AddCallback(NULL, 1, 1, fmodGetStlCBType(), sndSourceStolenCB, NULL);

	FMOD_EventSystem_AddCallback(NULL, 1, 0, fmodGetStartCBType(), sndEventLRUStart, NULL);
	FMOD_EventSystem_AddCallback(NULL, 1, 0, fmodGetEndCBType(), sndEventLRUStop, NULL);

	fmodPreProcessEvents();

	sndDebuggerRegister();

	sndMemInit();

	music_state.fadeManager = sndFadeManagerCreate(0);
	g_audio_state.fadeManager = sndFadeManagerCreate(0);

	sndDebugSetCallbacks();

	sndSetupDefaults();

	sndLODInit(&gSndLOD);

	loadend_printf("done.");
#endif
}

#ifndef STUB_SOUNDLIB

int sndEnabled(void)
{
	return !g_audio_state.noaudio && !FMOD_EventSystem_ProjectNotLoaded();
}

U32 sndGetGroupNamesFromProject(const char *projectName, char ***groupNames) 
{
	return fmodGetGroupNamesFromProject(projectName, groupNames);
}

void sndKillSourceIfPlaying(SoundSource *source, bool immediate)
{
	bool doesExist = eaFind(&space_state.sources, source) != -1;	
	if(doesExist)
	{
		// devassert(source->fmod_event); called function checks for event
		FMOD_EventSystem_StopEvent(source->fmod_event, immediate);
	}
}

void sndLoadGeometry(GeoInst *inst)
{
	 
	Vec3*	verts = NULL;
	const int*	tris = NULL;
	int		index, mat_index, mat_count;
	PhysicalProperties *profile;
	ModelLOD *model_lod;

	PERFINFO_AUTO_START("sndGeometryLoader", 1);

	model_lod = modelGetLOD(inst->model, 0);
	assert(model_lod && modelLODIsLoaded(model_lod));
 
	verts = ScratchAlloc(model_lod->vert_count * sizeof(Vec3));
	memcpy(verts, modelGetVerts(model_lod), model_lod->vert_count * sizeof(Vec3));
	tris = modelGetTris(model_lod);

	// Now we load into FMOD!  :D

	for (index = 0; index < model_lod->vert_count; index++)
	{
		// Unpack gives coords in model space, so we have to transform them
		Vec3 temp;
		mulVecMat4(verts[index], inst->loc, temp);
		copyVec3(temp, verts[index]);
	}

	mat_index = 0;
	mat_count = model_lod->data->tex_idx[0].count;
	profile = GET_REF(model_lod->materials[mat_index]->world_props.physical_properties);
	if (!profile)
		profile = physicalPropertiesGetDefault();
	for(index = 0; index < model_lod->tri_count; index++)
	{	
		if(profile)
		{
			FMOD_ErrCheck(FMOD_EventSystem_AddPolygon(inst->geometry, verts[tris[index*3+0]],
				verts[tris[index*3+1]],
				verts[tris[index*3+2]],
				profile->occlude, profile->reverb));
		}
		else
		{
			FMOD_ErrCheck(FMOD_EventSystem_AddPolygon(inst->geometry, verts[tris[index*3+0]],
				verts[tris[index*3+1]],
				verts[tris[index*3+2]],
				0, 0));
		}
		
		mat_count--;
		if(mat_count == 0)
		{
			mat_index++;
			if(mat_index<model_lod->data->tex_count)
			{
				mat_count = model_lod->data->tex_idx[mat_index].count;
				profile = GET_REF(model_lod->materials[mat_index]->world_props.physical_properties);
			}
		}
	}

	ScratchFree(verts);

	PERFINFO_AUTO_STOP();
}

void geometryUpdate(void)
{
	GeoInst *temp;
	StashTableIterator iter;
	StashElement elem;

	PERFINFO_AUTO_START("geometryUpdate", 1);

	stashGetIterator(geoInstances, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		temp = (GeoInst*)stashElementGetPointer(elem);
		if(!modelLODIsLoaded(modelLoadLOD(temp->model, 0)) || temp->loaded)
		{
			continue;
		}
		else
		{
			sndLoadGeometry(temp);
			temp->loaded = 1;
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndUpdateParams(void)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i=0; i<eaSize(&globalParams); i++)
	{
		GlobalParam *gp = globalParams[i];
		int j;

		for(j=0; j<eaSize(&gp->sources); j++)
		{
			SoundSource *source = gp->sources[j];

			if(source->has_event)
			{
				// devassert(source->fmod_event); called function checks for this via FMOD_Result
				fmodEventSetParam(source->fmod_event, gp->param_name, gp->value_func());
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void sndGetLimitedBoomPos(Vec3 vPlayerPos,Vec3 vCameraPos,Vec3 vOut)
{
	F32 fDistCamToPlayer;
	Vec3 vCamToPlayer;
	subVec3(vCameraPos,vPlayerPos,vCamToPlayer);
	if (lengthVec3Squared(vCamToPlayer) < MIN_LISTENER_DIST_FROM_PLAYER*MIN_LISTENER_DIST_FROM_PLAYER)
	{
		copyVec3(vCameraPos,vOut);
		return;
	}
	fDistCamToPlayer = normalVec3(vCamToPlayer);
	if (fDistCamToPlayer > MAX_LISTENER_DIST_FROM_PLAYER)
		fDistCamToPlayer = MAX_LISTENER_DIST_FROM_PLAYER;
	scaleAddVec3(vCamToPlayer,fDistCamToPlayer,vPlayerPos,vOut);
}

void sndGetLastListenerPosition(Vec3 lastPos)
{
	if(g_audio_state.d_playerPos && (!g_audio_state.cutscene_active_func || !g_audio_state.cutscene_active_func()))
	{
		copyVec3(listener.last_player_pos, lastPos);
	}
	else if (g_audio_state.d_cameraPos)
	{
		copyVec3(listener.last_camera_pos, lastPos);
	}
	else
	{
		sndGetLimitedBoomPos(listener.last_player_pos,listener.last_camera_pos,lastPos);
	}
}

void sndGetListenerPosition(Vec3 pos)
{
	if(g_audio_state.d_playerPos && (!g_audio_state.cutscene_active_func || !g_audio_state.cutscene_active_func()))
	{
		copyVec3(listener.player_pos, pos);
	}
	else if (g_audio_state.d_cameraPos)
	{
		copyVec3(listener.camera_pos, pos);
	}
	else
	{
		// fancy stuff
		sndGetLimitedBoomPos(listener.player_pos,listener.camera_pos,pos);
	}
}

void sndGetListenerPanPosition(Vec3 pos)
{	
	if(g_audio_state.d_playerPanPos && (!g_audio_state.cutscene_active_func || !g_audio_state.cutscene_active_func()))
	{
		copyVec3(listener.player_pos, pos);
	}
	else
	{
		copyVec3(listener.camera_pos, pos);
	}
}

void sndGetListenerRotation(Vec3 fwd)
{
	if(g_audio_state.d_playerPos)
	{
		copyVec3(listener.player_fwd, fwd);
	}
	else
	{
		copyVec3(listener.camera_fwd, fwd);
	}
}

void sndGetListenerSpacePos(Vec3 pos)
{
	if(g_audio_state.d_playerSpace && (!g_audio_state.cutscene_active_func || !g_audio_state.cutscene_active_func()))
	{
		copyVec3(listener.player_pos, pos);
	}
	else
	{
		sndGetListenerPosition(pos);
	}
}

void sndGetPlayerPosition(Vec3 pos)
{
	if(!g_audio_state.cutscene_active_func || !g_audio_state.cutscene_active_func())
	{
		copyVec3(listener.player_pos, pos);
	}
	else
	{
		copyVec3(listener.camera_pos, pos);
	}
}

void sndGetPlayerForward(Vec3 fwd)
{
	if(!g_audio_state.cutscene_active_func || !g_audio_state.cutscene_active_func())
	{
		copyVec3(listener.player_fwd, fwd);
	}
	else
	{
		copyVec3(listener.camera_fwd, fwd);
	}
}

void sndGetPlayerVelocity(Vec3 vel)
{
	copyVec3(listener.player_vel, vel);
}

void sndGetCameraPosition(Vec3 pos)
{
	copyVec3(listener.camera_pos, pos);
}

void sndGetCameraRotation(Vec3 fwd)
{
	copyVec3(listener.camera_fwd, fwd);
}

#define SOUND_UNLOAD_TIME 3 // 20

void sndUpdateLRU(void)
{
	int curticks = timerCpuTicks();
	StashElement elem;
	StashTableIterator iter;

	PERFINFO_AUTO_START_FUNC();

	if(!eventLRUStack)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	stashGetIterator(eventLRUStack, &iter);

	while(stashGetNextElement(&iter, &elem))
	{
		EventRefCount *ref = stashElementGetPointer(elem);
		F32 seconds = timerSeconds(curticks-ref->ticks);
		F32 seconds2 = timerSeconds(curticks-ref->lastTagged);

		ref->lastTagged = curticks;

		if(ref->refs<=0 && seconds > SOUND_UNLOAD_TIME && seconds2 < (float)SOUND_UNLOAD_TIME/10.0)
		{
			void *event = NULL;
			int iKey = stashElementGetIntKey(elem);

			//char *name = NULL;
			fmodEventSystemGetEventBySystemID(iKey, &event, NULL);

			if(!event)
			{
				// not good
				continue;
			}
			
			//fmodEventGetName(event, &name);
			//sndPrintf(1, SNDDBG_MEMORY|SNDDBG_EVENT, "%s : ref = 0, freeing\n", name);

			if (!fmodEventSystemFreeEventData(event))
				continue; // Data still loading

			stashIntRemovePointer(eventLRUStack, iKey, NULL);
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndUpdateListener(F32 elapsed)
{
	Vec3 vel = {0}, pos = {0}, forward = {0}, up = {0}, lastPos = {0}, deltaPos = {0};
	Mat4 cmat, pmat;
	/*
	static S64 last_update_time = 0;
	static Vec3 last_update_cam_pos;
	static Vec3 last_update_ca_pos;
	*/

	PERFINFO_AUTO_START_FUNC();

	listener.orient_valid = 0;

	if(g_audio_state.player_exists_func && g_audio_state.player_exists_func())
	{	
		if(g_audio_state.player_mat_func && g_audio_state.player_vel_func && g_audio_state.camera_mat_func)
		{
			F32 timeDelta;

			g_audio_state.camera_mat_func(cmat);
			g_audio_state.player_mat_func(pmat);

			if(!g_audio_state.doppler)
			{
				g_audio_state.player_vel_func(vel);
			}

			// Store camera/player relative positions and orients
			copyVec3(pmat[2], listener.player_fwd);
			copyVec3(cmat[2], listener.camera_fwd);
			scaleVec3(listener.camera_fwd, -1.0, listener.camera_fwd);

			copyVec3(listener.player_pos, listener.last_player_pos);
			if(!nearSameVec3(pmat[3], listener.player_pos))
			{
				copyVec3(pmat[3], listener.player_pos);
			}
			listener.player_pos[1] += 4.5; // Move it off the ground

			copyVec3(listener.camera_pos, listener.last_camera_pos);
			if(!nearSameVec3(cmat[3], listener.camera_pos))
			{
				copyVec3(cmat[3], listener.camera_pos);
			}

			invertMat3(pmat, listener.player_inv);
			copyMat3(cmat, listener.camera_mat);

			// This gets the correct one based on options
			sndGetListenerPosition(pos);
			sndGetListenerRotation(forward);
			//sndGetPlayerVelocity(vel);

			// manually calculate velocity by getting position delta
			sndGetLastListenerPosition(lastPos);

			// make sure we have a value
			if(listener.last_updated_time > 0)
			{
				timeDelta = timerSeconds(timerCpuTicks() - listener.last_updated_time);
				if(timeDelta > 0) 
				{
					Vec3 tmpVec, tmpVec2;

					// get delta pos (scaled by time)
					subVec3(pos, lastPos, deltaPos);
					scaleVec3(deltaPos, 1.0 / timeDelta, deltaPos);

					// averaging filter 
					scaleVec3(deltaPos, 0.25, tmpVec);
					scaleVec3(listener.last_velocity, 0.75, tmpVec2);
					addVec3(tmpVec, tmpVec2, listener.player_vel);
			 
					scaleVec3(listener.player_vel, 0.5, listener.player_vel);

					copyVec3(listener.player_vel, listener.last_velocity);

					//printf("v={%f %f %f} p={%f %f %f} d=%f\n", deltaPos[0], deltaPos[1], deltaPos[2], pos[0], pos[1], pos[2], timeDelta);
				}
			}

			listener.last_updated_time = timerCpuTicks();

			// Set up the vectors to be correct!
			up[0] = 0;
			up[1] = 1;
			up[2] = 0;

			forward[1] = 0;
			normalVec3XZ(forward);

			copyVec3(forward, listener.forward);
			copyVec3(up, listener.up);
			crossVec3(forward, up, listener.left);

			listener.orient_valid = 1;

			//wlVolumeCacheQuerySphere(g_audio_state.snd_volume_query, listener.player_pos, 0);
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndCheckForErrors(void)
{
	PERFINFO_AUTO_START_FUNC();
	fmodEventSystemCheckWavebanks();
	PERFINFO_AUTO_STOP();
}

void sndLibOncePerFrame(F32 elapsed)
{
	Vec3 pos = {0}, vel = {0};
	// Using the position of the player for distance checks
	// Velocity of player for doppler effects
	// Forward and 'up' are from the camera's perspective
	
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		// no audio
		
		PERFINFO_AUTO_STOP();
		return;
	}

	wlPerfStartSoundBudget();

	sndUpdateListener(elapsed);
	
	sndMixerOncePerFrame(gSndMixer, elapsed);

	sndUpdateParams();
	
	sndUpdateInactiveFade();

	sndUpdateFadeManagers(elapsed);

	sndAnimTick(g_SoundAnim);

	if(gConf.bOpenMissionMusic)
	{
		sndMissionOncePerFrame();
	}
	
	sndLODUpdate(&gSndLOD, elapsed);

	sndUpdateLRU();
	
	if(g_audio_state.debug_level > 4)
	{
		sndSourceValidatePlaying();
	}

	sndGetListenerPosition(pos);

	sndGetPlayerVelocity(vel);
	
	sndUpdate(pos, vel, listener.orient_valid ? listener.forward : NULL, listener.orient_valid ? listener.up : NULL);

	sndUpdateVoice(pos, elapsed);
	
	sndCheckForErrors();

	if(g_audio_state.editor_active_func && g_audio_state.editor_active_func())
	{
		g_audio_state.last_editor_ticks = timerCpuTicks();
	}

	wlPerfEndSoundBudget();

	PERFINFO_AUTO_STOP();
}

/*
U32 boxSpaceColl(Vec3 min, Vec3 max, SoundSpace *space)
{
	switch(space->type)
	{
		xcase SST_VOLUME: {
			return boxBoxCollision(min, max, space->volume.min, space->volume.max);
		}
		xcase SST_SPHERE: {
			return boxSphereCollision(min, max, space->sphere.mid, space->sphere.radius);
		}
	}
}
*/

void sndUpdate(Vec3 pos, Vec3 vel, Vec3 forward, Vec3 up)
{
	S32 result;
	PERFINFO_AUTO_START("sndUpdate", 1);

	FMOD_ErrCheck(FMOD_EventSystem_set3DListenerAttributes(pos, vel, forward, up));

	FMOD_ErrCheck(result = fmodEventSystemUpdate());

	if(result==FMOD_ERR_EVENT_MAXSTREAMS)
	{
		Errorf("Reached max streams.  Make sure wavebanks aren't limiting this.");
	}

	PERFINFO_AUTO_STOP();
}

void sndLoadBank(const char *bank, const char* full_name)
{
	FMOD_RESULT result;

	if(g_audio_state.noaudio)
	{
		return;
	}

	//sndPrintEvents(NULL);
	if(g_audio_state.debug_level>0)
	{
		loadstart_printf("Loading %s...", bank);
	}

	if(!sndProjectStash)
	{
		sndProjectStash = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	}

	result = FMOD_EventSystem_LoadBank(bank, full_name);
	FMOD_ErrCheck(result);

	if(result==FMOD_ERR_VERSION)
	{
		Errorf("You are using version %x of FMOD which is incompatible with: %s.  "
			"Please get latest executables.  If that doesn't fix it, ask the audio programmer.\n", 
			g_audio_state.fmod_version, bank);
	}

	if(g_audio_state.debug_level>0)
	{
		int current, max;
		loadend_printf(" done!");

		FMOD_EventSystem_GetMemStats(&current, &max);

		printf("SoundRAM Usage - Current: %d Bytes - Max: %d Bytes\n", current, max);
	}

	return;
}

FMOD_RESULT __stdcall sndRemoveOneShot(void *event, int type, void *p1, void *p2, void *ud)
{
	SoundSource *source = NULL;
	fmodEventGetUserData(event, (void**)&source);

	if(!source)
	{
		//badness, but not fatal
		return FMOD_OK;
	}
	stashRemovePointer(externalSounds, source->obj.desc_name, NULL);

	return FMOD_OK;
}

SoundSource* sndStopRemote(const char *event_name)
{
	return sndStopRemoteEx(event_name, false, false);
}

void sndCritterClean(U32 entRef)
{
	int i;

	StashTableIterator iter;
	StashElement elem;

	stashGetIterator(externalSounds, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		SoundSource *source = stashElementGetPointer(elem);

		if(source->type == ST_POINT && source->point.entRef==entRef)
		{
			// devassert(source->fmod_event); called function checks for event
			//printf("DEBUG(6): FMOD_EventSystem_StopEvent: %s\n", source->obj.desc_name);
			FMOD_EventSystem_StopEvent(source->fmod_event, 0);
		}		
	}

	for(i=eaSize(&music_state.playing)-1; i>=0; i--)
	{
		SoundSource *source = music_state.playing[i];

		if(source->type==ST_MUSIC)
		{
			if(source->music.entRef==entRef)
			{
				// devassert(source->fmod_event); called function checks for event
				//printf("DEBUG(7): FMOD_EventSystem_StopEvent: %s\n", source->obj.desc_name);
				FMOD_EventSystem_StopEvent(source->fmod_event, 0);
			}
		}
	}
}

typedef struct CBAndData {
	sndCompleteCallback cb;
	void *data;
} CBAndData;

FMOD_RESULT F_CALLBACK sndCallCallback(void *e, int type, void *p1, void *p2, void *ud)
{
	CBAndData *cbdata = (CBAndData*)ud;
	if(cbdata && cbdata->cb)
	{
		cbdata->cb(cbdata->data);
	}

	SAFE_FREE(cbdata);

	return 0;
}

SoundSource* sndPlayAtCharacter(const char *event_name, const char* filename, U32 entRef, sndCompleteCallback cb, void *userdata)
{
	Vec3 pos;
	void *eventInfo = NULL;
	EventMetaData *emd = NULL;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return NULL;
	}

	// auto-determine play function based on type of sound event (music or normal sound event)
	if (FMOD_EventSystem_GetEventInfoOnly(event_name, &eventInfo) == FMOD_OK)
		emd = sndFindMetaData(eventInfo);

	if(emd && emd->type == SND_MUSIC)
	{
		sndMusicPlayWorld(event_name, filename);
		return NULL;
	}
	else
	{
		sndGetPlayerPosition(pos);
		return sndPlayAtPosition(event_name, vecParamsXYZ(pos), filename, entRef, cb, userdata, false);
	}
}

typedef struct SoundLastTarget {
	U32 entRef;
	unsigned long timestamp;
	int clickCount;
	SoundSource *source;
} SoundLastTarget;

SoundLastTarget gSoundLastTarget;

void sndSetLastTargetEntRef(U32 entRef)
{
	gSoundLastTarget.entRef = entRef;
}

U32 sndLastTargetEntRef()
{
	return gSoundLastTarget.entRef;
}

void sndSetLastTargetClickCount(int clicks)
{
	gSoundLastTarget.clickCount = clicks;
}

int sndLastTargetClickCount()
{
	return gSoundLastTarget.clickCount;
}

void sndSetLastTargetSoundSource(SoundSource *source)
{
	gSoundLastTarget.source = source;
}

SoundSource* sndLastTargetSoundSource()
{
	return gSoundLastTarget.source;
}

void sndSetLastTargetTimestamp(unsigned long timestamp)
{
	gSoundLastTarget.timestamp = timestamp;
}

unsigned long sndLastTargetTimestamp()
{
	return gSoundLastTarget.timestamp;
}

void sndGetEventsWithPrefixFromEventGroupPath(const char *groupPath, char *eventNamePrefix, void ***events)
{
	void *eventGroup = NULL;

	if(!FMOD_EventSystem_FindEventGroupByName(groupPath, &eventGroup))
	{
		fmodGroupGetEventsWithPrefix(eventGroup, eventNamePrefix, events);
	}
}

void sndGetEventsFromEventGroupPath(const char *groupPath, void ***events)
{
	void *eventGroup = NULL;

	if(!FMOD_EventSystem_FindEventGroupByName(groupPath, &eventGroup))
	{
		fmodGroupGetEvents(eventGroup, events);
	}
}

void sndGetEventGroupsFromEventGroupPath(const char *groupPath, void ***eventGroups)
{
	void *eventGroup = NULL;

	if(!FMOD_EventSystem_FindEventGroupByName(groupPath, &eventGroup))
	{
		fmodGroupGetEventGroups(eventGroup, eventGroups);
	}
}


void sndGetEventName(void *event, char **name)
{
	fmodEventGetName(event, name);
}

U32 sndGetVoiceSetNamesForCategory(const char *pathToVoiceSet, const char *categoryName, char ***outNames)
{
	void *project = NULL;
	U32 result = 0;

	if(!project)
	{
		fmodProjectByName("Nemtact", &project);
	}
	
	if(project)
	{
		static StashTable groupStash = 0;
		void *eventGroup;

		if(!groupStash)
		{
			groupStash = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
		}
		else
		{
			stashTableClear(groupStash);
		}
		fmodProjectGetLeafGroupStash(project, groupStash);


		if(stashFindPointer(groupStash, pathToVoiceSet, &eventGroup))
		{
			if(categoryName != NULL)
			{
				void **events = NULL;
				int numEvents, i;
				size_t catNameLen;

				catNameLen = strlen(categoryName);

				fmodGroupGetEvents(eventGroup, &events);

				numEvents = eaSize(&events);
				for(i = 0; i < numEvents; i++)
				{
					char *name;
					fmodEventGetName(events[i], &name);

					if(!strncmp(categoryName, name, catNameLen))
					{
						eaPush(outNames, name);
					}
				}
			}

			result = 1;
		}

	}

	return result;
}

U32 sndEventExists(const char* event_name)
{
	return fmodEventExists(event_name);
}


SoundSource* sndPlayFromEntity(const char *event_name, U32 entRef, const char *filename, bool failQuietly)
{
	SoundSource* source = NULL;
	Vec3 entityPos;

	if(g_audio_state.get_entity_pos_func && entRef > 0 && g_audio_state.get_entity_pos_func(entRef, entityPos))
	{
		entityPos[1] += 5.0; // Add 5ft to keep it from the floor

		source = sndPlayAtPosition(event_name, vecX(entityPos), vecY(entityPos), vecZ(entityPos), filename, entRef, NULL, NULL, failQuietly);
		if(source)
		{
			source->updatePosFromEnt = 1;

			// add to watch list
			sndAnimAddSource(g_SoundAnim, source);
		}
	}
	else if(entRef == 0)
	{
		sndGetPlayerPosition(entityPos);

		source = sndPlayAtPosition(event_name, vecParamsXYZ(entityPos), filename, entRef, NULL, NULL, false);
		}

	return source;
}

SoundSource* sndPlayAtPosition(const char *event_name, F32 x, F32 y, F32 z, 
					   const char *filename, U32 entRef, sndCompleteCallback cb, void *userdata, bool failQuietly)
{
	Vec3 pos = {x, y, z};
	SoundSource *source;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return NULL;
	}

	source = sndSourceCreate(filename, filename, event_name, pos, ST_POINT, SO_REMOTE, NULL, entRef, failQuietly);
	if(!source)
	{
		return NULL;
	}
	source->point.entRef = entRef;
	stashAddPointer(externalSounds, event_name, source, 1);

	// source->fmod_event will be NULL here since we haven't gotten an event for the one shot yet!
	// We'd need to add this callback when we're looping over out toActive earrary and getting the event handles
	FMOD_EventSystem_AddCallback(source->fmod_event, 0, 0, fmodGetEndCBType(), sndRemoveOneShot, NULL);

	if(cb)
	{
		CBAndData *cbstruct = NULL;

		cbstruct = callocStruct(CBAndData);
		cbstruct->cb = cb;
		cbstruct->data = userdata;

		FMOD_EventSystem_AddCallback(source->info_event, 0, 0, fmodGetEndCBType(), sndCallCallback, cbstruct);
	}

	return source;
}

void sndAddContactDialogSource(SoundSource *source)
{
	sndAnimAddContactDialogSource(source);
}

bool sndIsContactDialogSourceAudible(void)
{
	return sndAnimIsContactDialogSourceAudible();
}

F32 sndGetVolumeByType(SoundVolumes *volumes, SoundType type)
{
	F32 *volume = sndGetVolumePtrByType(volumes, type);

	if(volume) return *volume;

	return 1;
}

F32 sndGetAdjustedVolumeByType(SoundType eSoundType)
{
	F32 main_volume = 1;
	F32 specific_volume = 1;

	main_volume =		g_audio_state.active_volume * 
						sndGetVolumeByType(&g_audio_state.options, SND_MAIN) * 
						sndGetVolumeByType(&g_audio_state.ui_volumes, SND_MAIN);
	specific_volume =	sndGetVolumeByType(&g_audio_state.options, eSoundType) * 
						sndGetVolumeByType(&g_audio_state.ui_volumes, eSoundType);

	return main_volume * specific_volume;
}

F32* sndGetVolumePtrByType(SoundVolumes *volumes, SoundType type)
{
	switch(type)
	{
		xcase SND_MAIN: {
			return &volumes->main_volume;
		}
		xcase SND_AMBIENT: {
			return &volumes->amb_volume;
		}
		xcase SND_FX: {
			return &volumes->fx_volume;
		}
		xcase SND_UI: {
			return &volumes->ui_volume;
		}
		xcase SND_VOICE: {
			return &volumes->voice_volume;
		}
		xcase SND_MUSIC: {
			return &volumes->music_volume;
		}
		xcase SND_NOTIFICATION: {
			return &volumes->notification_volume;
		}
		xcase SND_VIDEO: {
			return &volumes->video_volume;
		}
	}

	return NULL;
}

bool sndGetProjectFileByEvent(const char *pchEventPath, char **ppchFilePath)
{
	bool result = false;
	void *pEventInfo = NULL;

	if(ppchFilePath && pchEventPath && pchEventPath[0])
	{
		// default
		*ppchFilePath = NULL;

		FMOD_EventSystem_GetEventInfoOnly(pchEventPath, &pEventInfo);
		if(pEventInfo)
		{
			EventMetaData *pEventMetaData = sndFindMetaData(pEventInfo);

			if(pEventMetaData && pEventMetaData->project_filename)
			{
				*ppchFilePath = pEventMetaData->project_filename;
				result = true;
			}
		}
	}

	return result;
}

void sndFadeInType(SoundType type)
{
	float *volume = NULL;

	if(g_audio_state.noaudio)
	{
		return;
	}

	volume = sndGetVolumePtrByType(&g_audio_state.ui_volumes, type);
	if(volume)
	{
		sndFadeManagerAdd(g_audio_state.fadeManager, volume, SFT_FLOAT, SND_STANDARD_FADE);
	}
}

void sndFadeOutType(SoundType type)
{
	float *volume = NULL;

	if(g_audio_state.noaudio)
	{
		return;
	}

	volume = sndGetVolumePtrByType(&g_audio_state.ui_volumes, type);
	if(volume)
	{
		sndFadeManagerAdd(g_audio_state.fadeManager, volume, SFT_FLOAT, -SND_STANDARD_FADE);
	}
}

void sndUpdateInactiveFade(void)
{
	if(!g_audio_state.d_override_active)
	{
		if(gfxIsInactiveApp())
		{
			sndFadeManagerAdd(g_audio_state.fadeManager, &g_audio_state.active_volume, SFT_FLOAT, -SND_STANDARD_FADE);
		}
		else
		{
			sndFadeManagerAdd(g_audio_state.fadeManager, &g_audio_state.active_volume, SFT_FLOAT, SND_STANDARD_FADE);
		}
	}
	else
	{
		g_audio_state.active_volume = 1;
		sndFadeManagerRemove(g_audio_state.fadeManager, &g_audio_state.active_volume);
	}
}

void sndShutdown(void)
{
	// Not calling this, it can be very slow (waits for all file IO to finish
	//  which might involve waiting for patching to finish!)
	//FMOD_EventSystem_ShutDown();

	svShutDown();
}

void sndSetLanguage(const char *language)
{
	fmodSetLanguage(language);
}

const char *sndGetLanguage(void)
{
	return fmodGetLanguage();
}

U32 sndGetEventList(char*** earrayOut, char* group)
{
	if(g_audio_state.noaudio)
	{
		return 0;
	}

	return FMOD_EventSystem_GetEventList(earrayOut, group);
}

void sndPrintError(int error, char *file, int line)
{
	U32 fatal = FMOD_ErrIsFatal(error);
	if(error && (g_audio_state.debug_level || fatal))
	{
		conPrintfUpdate("SoundError: %s on %s at line %d", FMOD_ErrorString(error), file, line);
	}
}

int sndGetColor(int type)
{
	int colors[] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFF000F0, 0xFFF0F000, 0xFF00F0F0};
	int color = 0;
	int i;

	for(i=0; i<ARRAY_SIZE(colors); i++)
	{
		int bit = 1<<i;
		if(type & bit)
		{
			color |= bit;
		}
	}

	return color;
}

#endif



AUTO_FIXUPFUNC;
TextParserResult fixupSoundDSP(SoundDSP *dsp, enumTextParserFixupType type, void *pExtraData)
{
#ifndef STUB_SOUNDLIB
	switch(type)
	{
		xcase FIXUPTYPE_POST_TEXT_READ: {
			char name[256];
			getFileNameNoExt(name, dsp->filename);
			dsp->name = allocAddString(name);
		}

		xcase FIXUPTYPE_POST_RELOAD: {
			SoundDSPInstanceList *list = NULL;

			if(stashFindPointer(dspInstances, dsp->name, &list))
			{
				int i;

				for(i=0; i<eaSize(&list->instances); i++)
				{
					fmodDSPSetValuesByInfo(dsp, list->instances[i]->fmod_dsp);
				}
			}
		}
	}
#endif
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPDistortion(DSP_Distortion *dis, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			dis->distortion_level = 0.5;
		}
	}

	return PARSERESULT_SUCCESS;
}


AUTO_FIXUPFUNC;
TextParserResult fixupDSPHighpass(DSP_HighPass *hp, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPEcho(DSP_Echo *echo, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			echo->echo_decayratio = 0.5;
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPChorus(DSP_Chorus *ch, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			ch->chorus_drymix = 0.5;
			ch->chorus_wetmix1 = 0.5;
			ch->chorus_wetmix2 = 0.5;
			ch->chorus_wetmix3 = 0.5;
			ch->chorus_rate = 0.8;
			ch->chorus_depth = 0.03;
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPCompressor(DSP_Compressor *com, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPFlange(DSP_Flange *f, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			f->flange_drymix = 0.45;
			f->flange_wetmix = 0.55;
			f->flange_rate = 0.1;
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPLowpass(DSP_Lowpass *lp, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPSLowpass(DSP_SLowpass *lp, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {

		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPNormalize(DSP_Normalize *norm, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			norm->normalize_threshold = 0.1;			// 0.1
		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPParamEQ(DSP_ParamEQ *peq, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {

		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPPitchshift(DSP_Pitchshift *ps, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {

		}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupDSPSFXReverb(DSP_SfxReverb *sr, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			sr->sfxreverb_decayhfratio = 0.5;
			sr->sfxreverb_reflectionsdelay = 0.02;
			sr->sfxreverb_reverbdelay = 0.04;
		}
	}

	return PARSERESULT_SUCCESS;
}


#include "SndLibPrivate_h_ast.c"
//#include "SoundLib_autogen_QueuedFuncs.c"
#include "SoundLib_h_ast.c"

