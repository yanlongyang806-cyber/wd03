/***************************************************************************



***************************************************************************/

/* This file contains the public interface to the sound library */

#pragma once
GCC_SYSTEM

#ifndef _SNDLIBPRIVATE_H_
#define _SNDLIBPRIVATE_H_

#include "stdtypes.h"
#include "referencesystem.h"
#include "soundLib.h"
#include "fmod.h"
#include "utils.h"

C_DECLARATIONS_BEGIN

typedef struct UITreeNode UITreeNode;
typedef struct UIWindow UIWindow;
typedef struct UIButton UIButton;
typedef struct UITree UITree;
typedef struct UICheckButton UICheckButton;
typedef struct UISlider UISlider;
typedef struct UITreeNode UITreeNode;
typedef struct UILabel UILabel;
typedef struct GlobalParam GlobalParam;
typedef struct Model Model;
typedef struct DynParticle DynParticle;
typedef struct UICoordinate UICoordinate;

typedef struct SoundEventTreeNode SoundEventTreeNode;
typedef struct SoundMixer SoundMixer;
typedef struct SoundSpace SoundSpace;
typedef struct SoundSpaceConnector SoundSpaceConnector;
typedef struct SoundSource SoundSource;
typedef struct SoundSourceGroup SoundSourceGroup;
typedef struct SoundObject SoundObject;
typedef struct SoundDSPInstanceList SoundDSPInstanceList;
typedef struct SoundDSPInstance SoundDSPInstance;
typedef struct EventMetaData EventMetaData;
typedef const void* DictionaryHandle;
typedef struct CommandQueue CommandQueue;

typedef struct Debugger Debugger;
typedef struct DebuggerRoot DebuggerRoot;
typedef struct DebuggerObject DebuggerObject;
typedef int DebuggerType;
typedef int DebuggerFlag;

typedef struct DSPFlowNode DSPFlowNode;

typedef struct FMOD_EVENT_WAVEBANKINFO FMOD_EVENT_WAVEBANKINFO;

extern CommandQueue *eventQueue;
extern StashTable sndProjectStash;
extern StashTable gEventToProjectStash;

typedef struct GeoInst
{
	Model *model;
	Mat4 loc;
	void *geometry;
	U32 loaded : 1;
} GeoInst;

// Note, for iteration, STT_EVENT/STT_PROJECT is the same as STT_GROUP
typedef enum SoundTreeType
{
	STT_NONE = 0,
	STT_PROJECT = 1,
	STT_CATEGORY,
	STT_GROUP,
	STT_EVENT,
	STT_EVENT_INSTANCE,
} SoundTreeType;

typedef struct Listener
{
	Vec3 player_pos;
	Vec3 camera_pos;

	Vec3 last_camera_pos;
	Vec3 last_player_pos;

	Vec3 player_vel;

	Vec3 player_fwd;
	Vec3 camera_fwd;

	Mat3 player_inv;
	Mat3 camera_mat;

	Vec3 up;
	Vec3 left;
	Vec3 forward;

	Vec3 last_velocity;
	unsigned long last_updated_time;

	U32 orient_valid : 1;
} Listener;

extern Listener listener;

typedef F32 (*sndParamFunc)(void);

typedef struct GlobalParam
{
	char param_name[MAX_PATH];
	sndParamFunc value_func;
	SoundSource **sources;
} GlobalParam;

extern GlobalParam **globalParams;

typedef struct SoundSpaceOp {
	int unused;	
} SoundSpaceOp;

AUTO_STRUCT;
typedef struct DSP_Distortion {
	F32 distortion_level;				// Default(0.5)
} DSP_Distortion;



AUTO_STRUCT;
typedef struct DSP_HighPass {
	F32 highpass_cutoff;				AST(DEFAULT(5000))
	F32 highpass_resonance;				AST(DEFAULT(1))
} DSP_HighPass;

AUTO_STRUCT;
typedef struct DSP_Echo {
	F32 echo_delay;						AST(DEFAULT(500))
	F32 echo_decayratio;				// Default(0.5)
	F32 echo_maxchannels;				AST(DEFAULT(0))
	F32 echo_drymix;					AST(DEFAULT(1.0))
	F32 echo_wetmix;					AST(DEFAULT(1.0))
} DSP_Echo;

AUTO_STRUCT;
typedef struct DSP_Chorus {
	F32 chorus_drymix;					// 0.5
	F32 chorus_wetmix1;					// 0.5
	F32 chorus_wetmix2;					// 0.5
	F32 chorus_wetmix3;					// 0.5
	F32 chorus_delay;					AST(DEFAULT(40))
	F32 chorus_rate;					// 0.8
	F32 chorus_depth;					// 0.03
	F32 chorus_feedback;				AST(DEFAULT(0))
} DSP_Chorus;

AUTO_STRUCT;
typedef struct DSP_Compressor {
	F32 compressor_threshold;			AST(DEFAULT(0))
	F32 compressor_attack;				AST(DEFAULT(50))
	F32 compressor_release;				AST(DEFAULT(50))
	F32 compressor_gainmakeup;			AST(DEFAULT(0))
} DSP_Compressor;

AUTO_STRUCT;
typedef struct DSP_Flange {
	F32 flange_drymix;					// 0.45
	F32 flange_wetmix;					// 0.55
	F32 flange_depth;					AST(DEFAULT(1))
	F32 flange_rate;					// 0.1
} DSP_Flange;

AUTO_STRUCT;
typedef struct DSP_Lowpass {
	F32 lowpass_cutoff;					AST(DEFAULT(5000))
	F32 lowpass_resonance;				AST(DEFAULT(1))
} DSP_Lowpass;

AUTO_STRUCT;
typedef struct DSP_SLowpass {
	F32 lowpass_cutoff;					AST(DEFAULT(5000))
} DSP_SLowpass;

AUTO_STRUCT;
typedef struct DSP_Normalize {
	F32 normalize_fadetime;				AST(DEFAULT(5000))
	F32 normalize_threshold;			// 0.1
	F32 normalize_maxamp;				AST(DEFAULT(20))
} DSP_Normalize;

AUTO_STRUCT;
typedef struct DSP_ParamEQ {
	F32 parameq_center;					AST(DEFAULT(8000))
	F32 parameq_bandwidth;				AST(DEFAULT(1))
	F32 parameq_gain;					AST(DEFAULT(1))
} DSP_ParamEQ;

AUTO_STRUCT;
typedef struct DSP_Pitchshift {
	F32 pitchshift_pitch;				AST(DEFAULT(1))
	F32 pitchshift_fftsize;				AST(DEFAULT(1024))
	F32 pitchshift_maxchannels;			AST(DEFAULT(0))
} DSP_Pitchshift;

AUTO_STRUCT;
typedef struct DSP_SfxReverb {
	F32 sfxreverb_drylevel;				AST(DEFAULT(0))
	F32 sfxreverb_room;					AST(DEFAULT(0))
	F32 sfxreverb_roomhf;				AST(DEFAULT(0))
	F32 sfxreverb_roomrollofffactor;	AST(DEFAULT(10))
	F32 sfxreverb_decaytime;			AST(DEFAULT(1))
	F32 sfxreverb_decayhfratio;			// Default = 0.5
	F32 sfxreverb_reflectionslevel;		AST(DEFAULT(-10000))
	F32 sfxreverb_reflectionsdelay;		// Default = 0.02
	F32 sfxreverb_reverblevel;			AST(DEFAULT(0))
	F32 sfxreverb_reverbdelay;			// 0.04
	F32 sfxreverb_diffusion;			AST(DEFAULT(100))
	F32 sfxreverb_density;				AST(DEFAULT(100))
	F32 sfxreverb_hfreference;			AST(DEFAULT(5000))
	F32 sfxreverb_roomlf;				AST(DEFAULT(0))
	F32 sfxreverb_lfreference;			AST(DEFAULT(250))
} DSP_SfxReverb;



typedef enum DSPOrigin {
	DSPO_System,		// The "just before output" DSP
	DSPO_Event,			
	DSPO_Space,			
	DSPO_Conn,
	DSPO_DSP,
	DSPO_Other,
} DSPOrigin; 

typedef struct DSPFlowNodeSnapShot {
	DSPFlowNode **children;
	float *buffer;
	int channels;
	int length;
	int timestamp;
} DSPFlowNodeSnapShot;

typedef struct DSPFlowNode {
	DSPOrigin dspo;
	DSPFlowNodeSnapShot **history;
	void *origin_data;
	
	U32 base : 1;		// For spaces/conns which have 2 DSPs and an SFX in the middle
	U32 touched : 1;	// Only create a snapshot once per frame, since they're duplicates
} DSPFlowNode;


AUTO_STRUCT;
typedef struct SoundDefaults
{
	const char* pchFileName;	AST( CURRENTFILE )
	char* pchName;				AST(KEY POOL_STRING STRUCTPARAM)

	F32 fMainVolume;			AST(NAME("SndMainVolume") DEFAULT(0.85))
	F32 fAmbVolume;				AST(NAME("SndAmbientVolume") DEFAULT(0.7))
	F32 fFxVolume;				AST(NAME("SndFxVolume") DEFAULT(0.7))
	F32 fMusicVolume;			AST(NAME("SndMusicVolume") DEFAULT(0.7))
	F32 fVoiceVolume;			AST(NAME("SndVoiceVolume") DEFAULT(1.0))
	F32 fUIVolume;				AST(NAME("SndUIVolume") DEFAULT(0.7))
	F32 fNotificationVolume;	AST(NAME("SndNotificationVolume") DEFAULT(0.7))
	F32 fVideoVolume;			AST(NAME("SndVideoVolume") DEFAULT(0.8))
	bool bMuteVOonContactEnd;	AST(NAME("SndMuteVO") DEFAULT(1))
} SoundDefaults;

AUTO_ENUM;
typedef enum 
{
	DSPDISTORTION,
	DSPHIGHPASS,
	DSPECHO,
	DSPCOMPRESSOR,
	DSPFLANGE,
	DSPSLOWPASS,
	DSPLOWPASS,
	DSPNORMALIZE,
	DSPPARAMEQ,
	DSPPITCHSHIFT,
	DSPSFXREVERB,
} DSPType;

AUTO_STRUCT;
typedef struct SoundDSP {
	DSPType		type;
	const char *name; AST(KEY POOL_STRING)
	char *filename;	AST(CURRENTFILE)
	union {
		DSP_Distortion	distortion;		AST( REDUNDANTNAME )
		DSP_HighPass	highpass;		AST( REDUNDANTNAME )
		DSP_Echo		echo;			AST( REDUNDANTNAME )
		DSP_Chorus		chorus;			AST( REDUNDANTNAME )
		DSP_Compressor	compressor;		AST( REDUNDANTNAME )
		DSP_Flange		flange;			AST( REDUNDANTNAME )
		DSP_Lowpass		lowpass;		AST( REDUNDANTNAME )
		DSP_SLowpass	slowpass;		AST( REDUNDANTNAME )
		DSP_Normalize	normalize;		AST( REDUNDANTNAME )
		DSP_ParamEQ		parameq;		AST( REDUNDANTNAME )
		DSP_Pitchshift	pitchshift;		AST( REDUNDANTNAME )
		DSP_SfxReverb	sfxreverb;
	};
} SoundDSP;

typedef struct SoundDSPEffect {
	REF_TO(SoundDSP) dsp_def;

	F32 curinterp;
	// If true, we're moving towards def; otherwise towards null-dsp
	U32 to_target : 1;
	// If true, when curinterp reaches 0 (i.e. null DSP), disconnect and remove
	U32 disconnectOnSource : 1;
	void *fmoddsp;
	void *fmodcg;
} SoundDSPEffect;

typedef struct SoundDSPInstance {
	void *fmod_dsp;
	void *user_data;

	SoundDSPInstanceList *list;
} SoundDSPInstance;

typedef struct SoundDSPInstanceList {
	REF_TO(SoundDSP) dspRef;
	SoundDSPInstance **instances;
} SoundDSPInstanceList;

extern StashTable dspInstances;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct SoundDSPList {
	SoundDSP **dsps;  AST( NAME(SoundDSP) )
} SoundDSPList;

typedef struct SoundSpaceState {
	SoundSpace **global_spaces;
	SoundSpace **non_exclusive_spaces;		// Spaces that are only used for tracking and not for exclusion
	SoundSpaceConnector **global_conns;

	//SoundSpace **leaf_spaces;				// Spaces at the edge of our auditory distance
	SoundSpace *current_space;
	SoundSpace *null_space;					// All space that isn't occupied by an actual space

	SoundObject **objects;
	SoundSource **sources;
	SoundSourceGroup **source_groups;

	DictionaryHandle dsp_dict;

	U32 needs_reconnect		: 1;
	U32 needs_rebuild		: 1;
	U32 needs_pan_update	: 1;
} SoundSpaceState;

typedef struct EventMetaData {
	char *project_filename;
	const char *exclusive_group;
	U32 exclusivity_group;
	SoundType type;
	int moving;  // play only when sound source is moving
	int conflictPriority;
	int queueGroup;
	int priority;

	U32 streamed	: 1;
	U32 test		: 1;
	U32 use_dsp		: 1;
	U32 allow2d		: 1;
	U32 ignore3d	: 1;
	U32 fade2d		: 1;
	U32 ignorePosition : 1;
	U32 music		: 1;
	U32 playAsGroup : 1;
	U32 ignoreLOD : 1;
	U32 duckable : 1;
	U32 clickie : 1;
	U32 animate : 1;
	U32 alwaysAssignVoice : 1; 	// even if there isn't enough room on the mixer, play this sound

} EventMetaData;

typedef struct SoundDriverInfo
{
	int driverid; 
	int minfreq;
	int maxfreq;
	char drivername[100];
	FMOD_CAPS caps;
	FMOD_SPEAKERMODE speakermode;	
} SoundDriverInfo;

extern StashTable externalSounds;
extern SoundSpaceState space_state;
extern StashTable fxSounds;
extern FMOD_EVENT_WAVEBANKINFO *wavebankinfos;
#define NUM_WAVEBANK_INFOS 200 // Feel free to bump this as our audio data grows, but be careful of any hard codes in FMOD (currently it's set to 1,000)

void sndGetListenerPosition(Vec3 pos);
void sndGetListenerPanPosition(Vec3 pos);
void sndGetListenerRotation(Vec3 rot);
void sndGetListenerSpacePos(Vec3 pos);
void sndGetPlayerPosition(Vec3 pos);
void sndGetPlayerRotation(Vec3 rot);
void sndGetPlayerVelocity(Vec3 vel);
void sndGetCameraPosition(Vec3 pos);
void sndGetCameraRotation(Vec3 rot);

SoundSource* sndStopRemote(const char *event_name);

SoundDSPEffect* sndMixerAddDSPEffect(SoundMixer *mixer, SoundDSP *dsp);
void sndMixerDelDSPEffect(SoundMixer *mixer, SoundDSPEffect **effect);

// Used by clientcmds
void sndPlayRemote3dV2(const char *event_name, float x, float y, float z, const char *filename, U32 entRef);
void sndPlayRemoteV2(const char *event_name, const char *filename, U32 entRef);

// State stuff
void sndDisableSound(void);
void sndEnableSound(void);

// Called when the user adds/removes speakers & microphones & such through an FMOD system callback
void sndDeviceListsChanged(void);

void* sndGetDSP(const char *name, void *user_data);

EventMetaData *sndFindMetaData(void *fmod_event);

void sndPrintPlaying(void);
void sndGetPlayingSources(SoundSource ***sources);

void sndCritterClean(U32 entRef);

int sndGetColor(int type);

#define sndPrintf(level,type,fmt,...)													\
	if((level)<=g_audio_state.debug_level && ((type) & g_audio_state.debug_type)) {		\
		int c = sndGetColor(type);														\
        printfColor(c, fmt, ##__VA_ARGS__);												\
	}

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

C_DECLARATIONS_END

#endif