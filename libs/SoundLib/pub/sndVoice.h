#pragma once

#include "stdtypes.h"

typedef U32 ContainerID;
typedef struct OptionSetting OptionSetting;
typedef struct VoiceChannel VoiceChannel;
typedef struct VoiceChannelGroup VoiceChannelGroup;
typedef struct VoiceCaptureDevice VoiceCaptureDevice;
typedef struct VoiceRenderDevice VoiceRenderDevice;
typedef struct VoiceUser VoiceUser;

AUTO_STRUCT;
typedef struct VoiceConfig {
	// External voice server address, defaults to Vivox dev server
	const char* voiceServer;

	// if set, the default g_VoiceState.options SpeakerVolume
	int defaultSpeakerVolume;

	// if set, the default g_VoiceState.options MicLevel
	int defaultMicLevel;

	// if set, the default per user volume
	int defaultUserVolume;

	// Specifies that PTT is default, clearly
	U32 pushToTalkDefault : 1;

	// Show a tutorial explaining which key to push to talk the 1st time someone joins a voice chat channel
	U32 showFirstConnectTutorial : 1;

	// NOTE: the options below are not hooked up yet:
	// When not in PTT mode, connecting to a channel will unmute the mic
	U32 unmuteDefault : 1;
	// Allow listening only to a single channel
	U32 singleChannelRx : 1;
	// Allow transmit only to a single channel
	U32 singleChannelTx : 1;
	// Disable voice chat by default
	U32 defaultNoVoice : 1;

	U32 usedFields[1];  AST(USEDFIELD)
} VoiceConfig;
extern ParseTable parse_VoiceConfig[];
#define TYPE_parse_VoiceConfig VoiceConfig

AUTO_STRUCT;
typedef struct VoiceUserPrefs {
	int volume;					AST(NAME("Volume") DEFAULT(40))

	U32 mutedByMe : 1;			AST(NAME("Muted") DEFAULT(0))
} VoiceUserPrefs;

AUTO_STRUCT;
typedef struct VoiceChannelUser {
	VoiceChannel *chan;			AST(UNOWNED)
	VoiceUser *user;			AST(UNOWNED)

	F32 energy;
	F32 volume;

	U32 isTalking		: 1;
	U32 isMutedByOp		: 1;
	U32 isMutedByMe		: 1;
} VoiceChannelUser;

AUTO_STRUCT;
typedef struct VoiceUser {
	VoiceUserPrefs prefs;		// Stored preferences
	ContainerID id;				// Account id
	const char* externName;		// Vivox Username
	char* externURI;			// Vivox URI, generally "sip:externName@domain", e.g. "sip:user_Bob_1231@domain"

	VoiceChannelUser **channels;

	int volume;					// While a technically per-user-per-channel state, I'm making this global
	int targetVolume;			// UI hint to show what the volume will be when Vivox catches up. 
	U32	isMutedByMe		: 1;	// Same
} VoiceUser;

AUTO_ENUM;
typedef enum MultiChannelMode {
	MCM_ONEAUDIBLE,
	MCM_DUCKINACTIVE,
	MCM_ALLEQUAL,
} MultiChannelMode;
extern StaticDefineInt MultiChannelModeEnum[];

AUTO_STRUCT;
typedef struct VoiceTutorials {
	int showFirstConnectTutorial; AST(DEFAULT(0))
} VoiceTutorials;

AUTO_STRUCT;
typedef struct VoiceOptions {
	int SpeakerVolume;			AST(DEFAULT(40))
	int MuteInactive;			AST(DEFAULT(0))
	int MicLevel;				AST(DEFAULT(40))
	U32 OpenMic;				AST(DEFAULT(0))
	U32 NoVoice;				AST(DEFAULT(0))
	MultiChannelMode MultiMode; AST(DEFAULT(0))
	F32 DuckPercent;			AST(DEFAULT(0.5))
	int InputDev;				AST(DEFAULT(0))			
	int OutputDev;				AST(DEFAULT(0))
} VoiceOptions;

AUTO_ENUM;
typedef enum RequestState {
	RS_NONE,
	RS_NEED_REQUEST,
	RS_REQUESTING,
	RS_SUCCESS,
	RS_FAILURE,
} RequestState;

typedef void (*VoidVoidFunc)(void);
typedef void (*VoidIntFunc)(int i);
typedef void (*VoidIntIntFunc)(int i, int j);
typedef bool (*BoolU32Func)(U32 i);
typedef char* (*CharPtrU32Func)(U32 i);
typedef void (*VoidStrArrFunc)(char **strings);

typedef void (*VoidStrFunc)(const char* str);

AUTO_STRUCT;
typedef struct VoiceFont {
	int id;
	char *name;
	char *desc;
} VoiceFont;

AUTO_STRUCT;
typedef struct VoiceChannelState {
	F32 volume;

	int fontID;

	U32 transmitting : 1;
	U32 muted;	
} VoiceChannelState;

AUTO_STRUCT;
typedef struct VoiceChannel {
	char* internName;
	char* externName;		// SIP URI for Vivox

	char* handle;			// Return value from Vivox

	VoiceChannelState ext_state;

	VoiceChannelUser **allUsers;	AST(UNOWNED)

	VoiceChannelGroup *group;		AST(UNOWNED)

	VoiceFont *active_font;			AST(UNOWNED)

	RequestState connectState;
	RequestState disconnectState;

	S64 timeToWait;
	U32 connectFailures;

	U32 transmitting : 1;
	U32 destroyOnLeave : 1;
	U32 transmitOnJoin : 1;
	U32 adchannel : 1;
	U32 has_spoken : 1;
	U32 has_listened : 1;
} VoiceChannel;

extern ParseTable parse_VoiceFont[];

AUTO_ENUM;
typedef enum VoiceAdState {
	VAS_NONE,
	VAS_CHANNELS_LEAVE,
	VAS_AD_CHAN_JOIN,
	VAS_REJOIN_ALL,
} VoiceAdState;

typedef struct VoiceState {
	VoiceConfig config;
	VoiceOptions options;
	VoiceTutorials tutorials;
	ContainerID localAcct;

	const char* acct_server;
	const char* acct_domain;

	const char* username;
	const char* password;
	int			acctid;

	const char* connector_handle;

	const char* account_handle;
	const char* acct_uri;

	VoiceChannelGroup *defGroup;
	VoiceChannelGroup **groups;

	VoiceChannel *active_channel;
	VoiceChannel **channels;

	VoiceUser **users;

	VoiceCaptureDevice **capture_devices;
	VoiceCaptureDevice *active_capture_device;

	VoiceRenderDevice **render_devices;
	VoiceRenderDevice *active_render_device;

	VoiceFont **template_fonts;
	VoiceFont **session_fonts;

	RequestState connectorState;

	// For keeping options UI in sync
	VoidIntFunc volCB;
	VoidIntFunc micCB;
	VoidIntFunc openMicCB;
	VoidIntFunc inputCB;
	VoidIntFunc outputCB;

	VoidStrArrFunc inputListCB;
	VoidStrArrFunc outputListCB;

	// Callback for testing ignore on channel join
	BoolU32Func isIgnoredCB;

	// Callbacks for logging
	VoidStrFunc speakCB;
	VoidStrFunc listenCB;
	VoidVoidFunc adplayCB;

	// Callbacks for providing messages about state
	VoidIntIntFunc joinCB;
	VoidVoidFunc leaveCB;
	VoidVoidFunc failureCB;

	// For debugging
	CharPtrU32Func nameFromID;

	F32 record_energy;

	// For managing app inactivity fading
	F32 active_volume;

	// For managing mic fading from PTT or Open switch
	F32 micVolume;

	// For record play queuing
	int record_queue_font;

	// For managing ad playback
	VoiceAdState ad_state;
	const char* ad_chan_name;
	VoiceChannel *ad_chan;
	VoiceChannel *pre_ad_active;
	S64 time_ad_join;
	S64 time_verify_req;
	U32 num_verify_reqs;

	U32 enabled_region : 1;
	U32 ptt_on : 1;
	U32 signed_in : 1;
	U32 signing_in : 1;
	U32 signing_out : 1;
	U32 once_init : 1;
	U32 needsFonts : 1;
	U32 first_channel : 1;   // Race condition in Vivox
	U32 first_channel_wait : 1;
	U32 ad_started : 1;
	U32 record_active : 1;
	U32 record_mic_active : 1;
	U32 record_complete : 1;
	U32 record_playback_complete : 1;
	U32 record_playback_stopped : 1;
	U32 record_play_on_stop : 1;
	U32 use_default_capture_device : 1;
	U32 use_default_render_device : 1;
} VoiceState;

extern VoiceState g_VoiceState;

void svSetEnabledRegion(U32 uiEnabledRegion);

VoiceChannelGroup* svChannelGroupFind(int id, const char* handle);

void svChannelGetParticipantListByName(const char* internName, const char* externName, ContainerID **ids, bool speakingOnly);
void svChannelJoin(VoiceChannelGroup *group, VoiceChannel *chan, int transmitOnJoin);
VoiceChannel* svChannelJoinByName(VoiceChannelGroup *group, const char* internName, const char* externName, int transmitOnJoin);
void svChannelLeave(VoiceChannel *chan, U32 destroy);
VoiceFont* svChannelGetFontByName(const char* internName, const char* externName);
void svChannelSetFontByName(const char* internName, const char* externName, int fontID);
void svChannelLeaveByName(const char* internName, const char* externName, U32 destroy);
VoiceChannelUser* svChannelFindUserByName(const char* chanName, const char* externName, ContainerID accountID);
VoiceUser* svFindUser(ContainerID accountID);

void sndUpdateVoice(Vec3 pos, F32 elapsed);

void svDeviceListsChanged(void);

AUTO_ENUM;
typedef enum VivoxLogLevels {
	VLL_Error,
	VLL_Warning,
	VLL_Info,
	VLL_Debug,
	VLL_Trace,
} VivoxLogLevels;
extern StaticDefineInt VivoxLogLevelsEnum[];

void svOptionsSetCallbacks(VoidIntFunc volume, VoidIntFunc mic, VoidIntFunc openmic, VoidIntFunc input, VoidIntFunc output);
void svOptionsDataSetCallbacks(VoidStrArrFunc input, VoidStrArrFunc output);
void svIgnoreSetCallbacks(BoolU32Func isIgnored);
void svSetNameCallback(CharPtrU32Func getName);
void svSetLoggingFuncs(VoidStrFunc speakCB, VoidStrFunc listenCB, VoidVoidFunc adplayCB);
void svSetNotifyFuncs(VoidIntIntFunc joinCB,  VoidVoidFunc leaveCB, VoidVoidFunc failureCB);

void svLoadDefaults(void);

void svOptionsResetToDefaults(void);
void svOptionsSave(void);

void svTutorialsResetToDefaults(void);
void svTutorialsSave(void);

// Accessors for UI
void svFontsPrint(void);

void svMicrophoneSetLevel(int level);
int svMicrophoneGetLevel(void);
void svSpeakersSetLevel(int level);
void svMicrophoneSetMute(bool mute);
void svSetLogLevel(VivoxLogLevels level);

void svCaptureDeviceSetActiveByName(const char* name);
void svRenderDeviceSetActiveByName(const char* name);

void svChannelGroupDumpRecording(VoiceChannelGroup *group);

void svSetOpenMic(int on);
int svMicGetOpen(void);
void svPushToTalk(int on);
void svSetMultiChannelMode(MultiChannelMode mode);
void svChannelSetTransmit(VoiceChannel *chan);
void svChannelSetTransmitByName(const char* internName, const char* externName);
const char *svChannelGetName(VoiceChannel *chan, int ext);
void svClearTransmit(void);
int svTransmitIsEnabled(void);
void svPlayAd(const char* channel);

void svUserSetVolumeByName(const char* internName, const char* externName, ContainerID accountID, int vol);
void svUserSetMuteByID(ContainerID accountID, S32 mute);
void svUserUpdateIgnore(int accountId, int ignored);
void svUserSetSelfMute(ContainerID accountID, S32 mute);

int svRecordInit(void);
int svRecordClose(void);
void svRecordStart(void);
void svRecordStop(void);
void svRecordPlay(int fontID);
void svRecordQueuePlay(int fontID);

void svShutDown(void);

#define svChannelIsActive() \
	(g_VoiceState.options.OpenMic || g_VoiceState.ptt_on)