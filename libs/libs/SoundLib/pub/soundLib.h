/***************************************************************************



***************************************************************************/

/* This file contains the public interface to the sound library */

#pragma once
GCC_SYSTEM

#ifndef _SOUNDLIB_H
#define _SOUNDLIB_H

#include "stdtypes.h"

C_DECLARATIONS_BEGIN

#define AUDIO_DEFAULT_DEVICE_ID 0

typedef struct StashTableImp* StashTable;
typedef struct HWND__ *HWND;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct WorldVolumeQueryCache WorldVolumeQueryCache;
typedef struct SoundSource SoundSource;
typedef	struct SoundFadeManager SoundFadeManager;
typedef struct StaticDefineInt StaticDefineInt;
typedef struct SoundDriverInfo SoundDriverInfo;
typedef struct SoundSpace SoundSpace;
typedef struct Mission Mission;
typedef struct OpenMission OpenMission;
typedef U32 ContainerID;

extern int soundBufferSize;  // = 1024*1024*29
extern int snd_enable_debug_mem;

// For getting player's position, velocity, and camera's position
typedef void (*SoundLibVecCallback)(Vec3 vector);
// For getting position of an entity
typedef bool (*SoundLibEntPositionCallback)(U32 entRef, Vec3 vector);

typedef void (*SoundLibMatCallback)(Mat4 mat);
// For determining if player exists 
typedef int (*SoundLibIntRetCallback)(void);
typedef bool (*SoundLibBoolRetCallback)(void);

typedef void (*SoundLibVoiceInfoCallback)(const char** usernameOut, const char** passwordOut, int *accountIDOut);

typedef OpenMission* (*SoundLibActiveOpenMissionCallback)();
typedef bool (*SoundLibPlayerInCombatCallback)(void);

typedef const char *(*SoundLibGetEntityName)(U32 entRef);
typedef void (*SoundLibEntityTalking)(U32 entRef, bool bEnabled);

typedef U32 (*SoundLibAccountToEntRef)(ContainerID acctid);

typedef void (*VoidIntFunc)(int);
typedef void (*VoidVoidFunc)(void);

enum
{
	SOUND_MUSIC = 100000,
	SOUND_GAME,
	SOUND_PLAYER,
	SOUND_AMBIENT,
};
 
AUTO_ENUM;
typedef enum 
{
	SND_MAIN	= 1 << 0,
	SND_FX		= 1 << 1,
	SND_AMBIENT = 1 << 2, 
	SND_MUSIC	= 1 << 3,
	SND_TEST	= 1 << 4,
	SND_UI		= 1 << 5,
	SND_VOICE	= 1 << 6,
	SND_NOTIFICATION = 1 << 7,
	SND_VIDEO = 1 << 8,
	SND_ALL_TYPES = 0xFFFFFFFF,
} SoundType;

extern StaticDefineInt SoundTypeEnum[];

typedef enum
{
	SNDDBG_MEMORY			= 1 << 0,
	SNDDBG_EVENT			= 1 << 1,
	SNDDBG_SPACE			= 1 << 2,
	SNDDBG_CONN				= 1 << 3,
	SNDDBG_DSP				= 1 << 4,
	SNDDBG_MUSIC			= 1 << 5,
	SNDDBG_ALL				= 0xFFFFFFFF,
} SoundDebugType;

typedef struct
{
	Vec3	pos;
	F32		radius;
	void	*def_tracker;
} SoundEditorInfo;

typedef struct SoundVolumes {
	F32 main_volume;
	F32 fx_volume;
	F32 amb_volume;
	F32 music_volume;
	F32 voice_volume;
	F32 ui_volume;
	F32 notification_volume;
	F32 video_volume;
} SoundVolumes;

typedef struct AudioState
{
	int doppler;		//whether or not Doppler is enabled
	int surround;		//whether or not surround is enabled
	int uisurround;		//what the user wants AudioState.surround to be set to
	int hrtf;			//level of HRTF enabled, corresponds to the DirectSound HRTF enums
	int software;		//whether or not sound processing is done in software
	int noaudio;		//true if all audio is disabled
	int surroundFailed;	//if this happens we want to unset the UI option for surround sound
	int muted;			//Did the user temporarily mute the sound?

	int inited;			//True after sound is initialized

	
	char *outputFilePath;

	int fmod_version;

	// debug flags
	int debug_level;
	SoundDebugType debug_type;

	SoundVolumes options;
	SoundVolumes ui_volumes;

	SoundFadeManager *fadeManager;

	int d_checkDisable;
	int last_editor_ticks;

	char **event_list;
	const char **dsp_list;

	U32 snd_volume_type;
	WorldVolumeQueryCache *snd_volume_query;
	WorldVolumeQueryCache *snd_volume_portal_query;
	WorldVolumeQueryCache *snd_volume_playable_query;

	// For for fading in and out
	F32 active_volume;

	// Current ducking scale factor (for non-player FX sounds)
	F32 fxDuckScaleFactor;

	// Determines range of directionality fading through a connector - default = 30
	F32 directionality_range;
	// Determines range of directionality fading from the pan position (simulates HRTF) - default = 20
	F32 hrtf_range;

	int curDriver;
	SoundDriverInfo **soundDrivers;

	bool dsp_enabled;

	int sound_developer;
	int loadtimeErrorCheck;
	int main_thread_id;
	U32	d_override_active		: 1;
	U32 noSoundCard				: 1;
	U32	d_playerOrient			: 1;
	U32 d_playerPos				: 1;
	U32 d_playerPanPos			: 1;
	U32 d_playerSpace			: 1;
	U32 d_cameraPos				: 1; // if this is not set, and d_playerPos is not set, we will limit the boom length
	U32 d_reloading				: 1;
	U32 d_useNetListener		: 1;
	U32 validateHeapMode		: 1;
	U32 audition				: 1;

	StashTable disabled_spheres;
	StashTable sndSourceGroupTable;
	SoundSource **streamed_sources;

	//void *ampAnalysisDSP; // pointer to FMOD DSP for doing amp analysis

	SoundLibBoolRetCallback editor_active_func;
	SoundLibMatCallback camera_mat_func;
	SoundLibMatCallback player_mat_func;
	SoundLibVecCallback player_vel_func;
	SoundLibIntRetCallback player_exists_func;
	SoundLibIntRetCallback is_login_func;
	SoundLibVoiceInfoCallback player_voice_func;
	SoundLibIntRetCallback cutscene_active_func;
	SoundLibEntPositionCallback get_entity_pos_func;
	SoundLibActiveOpenMissionCallback active_open_mission_func;
	SoundLibPlayerInCombatCallback player_in_combat_func;
	SoundLibGetEntityName get_entity_name_func;
	SoundLibEntityTalking ent_talking_func;
	SoundLibAccountToEntRef account_to_entref_func;
	VoidVoidFunc verify_voice_func;

	char *uiSkinName;
	F32 fCutsceneCropDistanceScalar; // Crop out world sound FX based on the distance to listener - Ex: a value of 0.5 will cut out sounds that

	//If true contact voice will end when the contact dialog ends
	bool bMuteVOonContactEnd;
} AudioState;



extern AudioState g_audio_state;

void sndSetUISkinName(const char *uiSkinName);
// returns NULL if none
char* sndUISkinName();

bool sndEventGroupExists(char *fullEventGroupPath);

SoundSource* sndPlayUIAudio(const char *pchSound, const char *pchFileContext);
SoundSource* sndStopUIAudio(const char *pchSound);

// General sound functions
void sndLibOncePerFrame(F32 elapsed);
void sndUpdate(Vec3 pos, Vec3 vel, Vec3 forward, Vec3 up);

void sndLoadDefaults();

// Volume manipulations
void sndFadeInType(SoundType type);
void sndFadeOutType(SoundType type);

F32* sndGetVolumePtrByType(SoundVolumes *volumes, SoundType type);
F32 sndGetVolumeByType(SoundVolumes *volumes, SoundType type);
F32 sndGetAdjustedVolumeByType(SoundType eSoundType);

void sndSetupDefaultOptionsVolumes(void);

U32 sndGetGroupNamesFromProject(const char *projectName, char ***groupNames);
U32 sndGetVoiceSetNamesForCategory(const char *pathToVoiceSet, const char *categoryName, char ***outNames);

// Starts the sound engine.
void sndInit(void);
int sndEnabled(void);
void sndGamePlayEnter(void);
void sndGamePlayLeave(void);
void sndMapUnload(void);

void sndShutdown(void);

void sndSetLanguage(const char *language);
const char *sndGetLanguage(void);

// Loads a bank file from specified filename and precaches the events if told to do so.
void sndLoadBank(const char *rel_name, const char *full_name);

/* sndGetProjectFileByEvent - get file path to FEV project for a sound event
//		pchEventPath - sound event path  (Ex: "path/to/sound")
//		ppchFilePath - on success, it will point to the file path pointer  (Ex: "sound/win32/ProjectName.fev")
//						note: a copy should be made for long term storage
//
// returns true on success
*/
bool sndGetProjectFileByEvent(const char *pchEventPath, char **ppchFilePath);

// Plays a "remote" event.
/*
void sndPlayRemoteV2(const char *event_name, const char* filename, U32 entRef);
void sndPlayRemote3dV2(const char *event_name, F32 x, F32 y, F32 z, const char* filename, U32 entRef);
*/

typedef void (*sndCompleteCallback)(void *userdata);
SoundSource* sndPlayAtCharacter(const char *event_name,	const char* filename, U32 entRef, sndCompleteCallback cb, void *userdata);
SoundSource* sndPlayFromEntity(const char *event_name, U32 entRef, const char *filename, bool failQuietly);
SoundSource* sndPlayAtPosition(const char *event_name, F32 x, F32 y, F32 z, 
							   const char *filename, U32 entRef, sndCompleteCallback cb, void *userdata, bool failQuietly);

SoundSource* sndPlayRandomPhraseWithVoice(const char *pchPhrasePath, const char *pchVoicePath, U32 entRef);

void sndMusicPlayUI(const char *event_name, const char *filename);
void sndMusicPlayWorld(const char *event_name, const char *filename);
void sndMusicPlayRemote(const char *event_name, const char *filename, U32 entRef);
void sndMusicClearUI(void);
void sndMusicClearWorld(void);
void sndMusicClearRemote(void);
void sndMusicClear(bool immediate);

// Length in milliseconds of this event. Note: Will be -1 if the length of the event can't be determined i.e. if it has looping sounds.
int sndEventGetLength(const char* event_name);
// Determines if an event is looping
U32 sndEventIsOneShot(const char* event_name);
// Determines if an event is streamed
U32 sndEventIsStreamed(void *info_event);
// Determines if an event is voice
U32 sndEventIsVoice(void *info_event);

void sndGetEventName(void *event, char **name);

// Stops a currently playing sound
void sndStopOneShot(const char *event_name);

// Kills a playing event.  Has no effect if handle doesn't reference a valid event.  If immediate is true,
// sound fadeout time is ignored.
void sndKillSourceIfPlaying(SoundSource *source, bool immediate);

// Set function used to get player velocity
void sndSetVelCallback(SoundLibVecCallback cb);

// Sets function used to get camera position (used in determining the "forward" vector)
void sndSetCameraMatCallback(SoundLibMatCallback cb);
// Same as above except for player
void sndSetPlayerMatCallback(SoundLibMatCallback cb);
// Callback to determine if a player exists
void sndSetPlayerExistsCallback(SoundLibIntRetCallback cb);
// Callback to determine if a player is at the login screen
void sndSetIsLoginCallback(SoundLibIntRetCallback cb);
// Callback to get voice info off Player
void sndSetPlayerVoiceInfoCallback(SoundLibVoiceInfoCallback cb);
// Is player currently in a cutscene?  (If so, uses camera as position)
void sndSetCutsceneActiveCallback(SoundLibIntRetCallback cb);
// Is the game in edit mode
void sndSetEditorActiveFunc(SoundLibBoolRetCallback cb);
// Need to get entity position by entRef
void sndSetEntPositionCallback(SoundLibEntPositionCallback cb);
// get the name of the entity by entRef
void sndSetGetEntityNameCallback(SoundLibGetEntityName cb);
// get the active open mission
void sndSetActiveOpenMissionCallback(SoundLibActiveOpenMissionCallback cb);
// determine whether player is in combat
void sndSetPlayerInCombatCallback(SoundLibPlayerInCombatCallback cb);
// ability to turn talking on/off
void sndSetEntityTalkingCallback(SoundLibEntityTalking cb);

void sndSetAccountIDToEntRefCallback(SoundLibAccountToEntRef cb);

void sndSetVoiceVerifyCallback(VoidVoidFunc cb);

// Fills an earray with the desired events.
// Does not take ownership.  All strings are dup'd, so free them when you're done.
U32 sndGetEventList(char*** earrayOut, char* group);

//typedef void (*sndEventCallback)(int type, void *event, void *userdata);
//U32 sndAddEventCallback(int type, void *event, sndEventCallback cb);

int sndGetDebugLevel(void);

void sndKillAll(void);
void sndReloadAll(const char *relpath);

// Gets the radius for a given event name
F32 sndGetEventRadius(const char *event_name);

// Determines if an event exists (tries to grab it from fmod)
U32 sndEventExists(const char* event_name);

// Gets an event list but uses a static copy
char ***sndGetEventListStatic(void);

// Gets a list of all DSPs
const char ***sndGetDSPListStatic(void);

// Print an error to the screen
void sndPrintError(int error, char *file, int line);

// Increments a static int
int sndGetFXGuid(void);

U32 sndEventIsPlaying(const char* event_name);

void sndGetEventsFromEventGroupPath(const char *groupPath, void ***events);
void sndGetEventsWithPrefixFromEventGroupPath(const char *groupPath, char *eventNamePrefix, void ***events);
void sndGetEventGroupsFromEventGroupPath(const char *groupPath, void ***eventGroups);

void sndSetLastTargetEntRef(U32 entRef);
U32 sndLastTargetEntRef();
void sndSetLastTargetClickCount(int clicks);
int sndLastTargetClickCount();
void sndSetLastTargetSoundSource(SoundSource *source);
SoundSource* sndLastTargetSoundSource();
void sndSetLastTargetTimestamp(unsigned long timestamp);
unsigned long sndLastTargetTimestamp();

void sndEventDecLRU(void *event);

void sndChangeDriver(int driverIndex);
void sndGetDriverNames(char ***eaDriverNames);

void sndSetCutsceneCropDistanceScalar(F32 val);

void sndAddContactDialogSource(SoundSource *source);
bool sndIsContactDialogSourceAudible(void);

void sndOptionsSetCallbacks(VoidIntFunc output);
void sndOptionsDataSetCallbacks(VoidVoidFunc outputDevices);

//-------------------------------- sndDebug2.c ------------------------------------

// Draws locations where sounds were played - in sndDebug2.c
void sndDebug2Draw(void);

C_DECLARATIONS_END

#endif
