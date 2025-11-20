#ifndef _SOUND_COMMON_H_
#define _SOUND_COMMON_H_
GCC_SYSTEM

#define GLOBAL_GAE_LAYER_FILENAME "sound/gaelayers/global.gaelayer"

typedef struct GameEvent GameEvent;
typedef struct GameAudioEventMap GameAudioEventMap;
typedef const void* DictionaryHandle;

typedef enum StatusWatchType {
	StatusWatch_Entity,
	StatusWatch_Pos,
	StatusWatch_Imm_Source,
	StatusWatch_Imm_All,
} StatusWatchType;

AUTO_STRUCT;
typedef struct GameAudioEventPair {
	GameAudioEventMap *map;			NO_AST

	GameEvent *game_event;
	GameEvent *end_event;			NO_AST
	const char *audio_event;		AST(POOL_STRING)

	U32 one_shot : 1;	
	U32 is_music : 1;
	U32 invalid  : 1;
} GameAudioEventPair;

AUTO_STRUCT;
typedef struct GameAudioEventMap {
	const char *filename;			AST( KEY CURRENTFILE )
	const char *name;				AST(POOL_STRING)

	GameAudioEventPair **pairs;

	char zone_dir[260];		NO_AST

	U32 invalid : 1;				NO_AST
	U32 is_global : 1;				NO_AST
	U32 is_tracking : 1;			NO_AST
} GameAudioEventMap;

// 2 dictionaries needed to
// keep the local map data separate from the global

extern DictionaryHandle *g_GAEMapDict;

void reloadGlobalGAECallback(const char *relpath, int when);

void sndCommonCreateGAEDict(void);
void sndCommonCreateGlobalGAEDict(void);

void sndCommonLoadToGAEDict(const char *path);
void sndGAEMapValidate(GameAudioEventMap *map, int noError);
StatusWatchType sndCommonGetStatusWatchType(GameEvent *ge);

typedef U32 (*SndCommonEventExistsFunc)(const char *event_name);
void sndCommonSetEventExistsFunc(SndCommonEventExistsFunc func);

// Used for general callback to notify when the GAE layers reload takes place
typedef void (*SndCommonGAELayersChanged)(const char *relativePath, int when, void *userData);
typedef struct SndCommonChangedCallbackInfo {
	struct SndCommonChangedCallbackInfo *next; // order matters for 1st two elements
	struct SndCommonChangedCallbackInfo *prev;
	
	SndCommonGAELayersChanged func;
	void *userData;
} SndCommonChangedCallbackInfo;

void sndCommonAddChangedCallback(SndCommonGAELayersChanged func, void *userData);

void sndCommon_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);

#endif