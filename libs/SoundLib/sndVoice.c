#include "sndVoice.h"

#include "sndFade.h"
#include "soundLib.h"

#include "adebug.h"
#include "ContinuousBuilderSupport.h"
#include "earray.h"
#include "file.h"
#include "logging.h"
#include "GfxConsole.h"
#include "GlobalTypes.h"
#include "GraphicsLib.h"
#include "mathutil.h"
#include "Prefs.h"
#include "logging.h"
#include "textparser.h"

#include "Organization.h"

#include "vivox-config.h"
#include "Vxc.h"
#include "VxcErrors.h"
#include "VxcEvents.h"
#include "VxcResponses.h"
#include "VxcRequests.h"

#define VIVOX_LIB_DIR "../../3rdparty/Vivox/bin"

#include "sndVoice_h_ast.h"
#include "sndVoice_c_ast.h"

#pragma comment(lib, VIVOX_LIB_DIR"/libsndfile-1.lib")
#pragma comment(lib, VIVOX_LIB_DIR"/vivoxplatform.lib")
#pragma comment(lib, VIVOX_LIB_DIR"/vivoxsdk.lib")

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

#define SOUND_VOICE_DEF_GROUP_ID		1
#define SOUND_VOICE_REC_GROUP_ID		2

#define SOUND_VOICE_MIC_FADE_RATE	20.f		// 

#define SOUND_VOICE_ECHO_CHAN "sip:confctl-2@" ORGANIZATION_DOMAIN

typedef struct VoiceChannelGroup VoiceChannelGroup;

AUTO_STRUCT;
typedef struct VoiceChannelGroup {
	RequestState createState;

	char* handle;			// Return value from Vivox

	VoiceChannel **channels;	AST(UNOWNED)

	U32 id;
	U32 recTime;				// In seconds
} VoiceChannelGroup;

// AKA Microphone
AUTO_STRUCT;
typedef struct VoiceCaptureDevice {
	char *deviceName;
	int recordLevel;
	U32 isActive : 1;
	U32 found : 1;				NO_AST
} VoiceCaptureDevice;

AUTO_STRUCT;
typedef struct VoiceRenderDevice {
	char *deviceName;
	int renderLevel;

	U32 isActive : 1;
	U32 found : 1;				NO_AST
} VoiceRenderDevice;

AUTO_STRUCT;
typedef struct VoiceCaptureDeviceList {
	VoiceCaptureDevice **devices;
} VoiceCaptureDeviceList;

AUTO_STRUCT;
typedef struct VoiceRenderDeviceList {
	VoiceRenderDevice **devices;
} VoiceRenderDeviceList;

VoiceState g_VoiceState;
char* s_ServerOverride;

VoiceUser* svFindUser(ContainerID accountID);
VoiceUser* svCreateUser(const char* externName, const char* externURI, ContainerID accountID);
void svFontsGetFinish(int templates, vx_voice_font_t **fonts, int count);
void svFontsGet(int templates);
VoiceFont* svFontGet(int templates, int id, int create);
void svChannelUserDestroy(VoiceChannelUser *vcu);
void svUserDestroy(VoiceUser *user);
void svClearTransmit(void);
void svCaptureDeviceUpdateSettings(VoiceCaptureDevice *device);
void svUserSetMute(VoiceUser *user, S32 mute);
void svUserSetVolume(VoiceUser *user, int volume);
void svCaptureDevicePrefsSave(void);
void svPrefsSave(ParseTable *pti, const char* prefName, void *s);
void svChannelSetUserVolume(VoiceChannel *chan, VoiceUser *user, int vol);
void svChannelUserSetMute(VoiceChannel *chan, VoiceUser *user, S32 mute);
void svChannelSetInactive(VoiceChannel *chan);
void svChannelSetVolume(VoiceChannel *chan, int level);

static S32 svConfigIsFieldSpecified(const char *pchFieldName)
{
	return TokenIsSpecifiedByName(parse_VoiceConfig, &g_VoiceState.config, pchFieldName);
}

void svSetEnabledRegion(U32 uiEnabledRegion)
{
	g_VoiceState.enabled_region = uiEnabledRegion;
}

const char* svGetVoiceServer(void)
{
	return g_VoiceState.acct_server;
}

void svConnect(void)
{
	vx_req_connector_create_t *req = NULL;

	if(g_VoiceState.connectorState!=RS_NONE && g_VoiceState.connectorState!=RS_FAILURE)
		return;
	
	vx_req_connector_create_create(&req);
	if(req)
	{
		req->mode = connector_mode_normal;
		req->acct_mgmt_server = vx_strdup(svGetVoiceServer());
		req->minimum_port = 0;
		req->maximum_port = 0;
		req->client_name = vx_strdup(GetProductName());
		req->log_filename_prefix = vx_strdup("vxlog");
		req->log_filename_suffix = vx_strdup(".log");
		req->log_folder = vx_strdup(logGetDir());
		req->log_level = 3;
		req->base.cookie = vx_strdup(STACK_SPRINTF("%s.Vivox", GetProductName()));
		req->session_handle_type = session_handle_type_unique;
		req->application = vx_strdup(GetProductName());

		vx_issue_request((vx_req_base_t*)req);

		g_VoiceState.connectorState = RS_REQUESTING;
	}
}

void svSignIn(void)
{
	vx_req_account_login_t *req = NULL;
	
	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(g_VoiceState.signed_in || g_VoiceState.signing_in) return;
	if(!g_VoiceState.username || !g_VoiceState.password) return;

	g_VoiceState.signing_in = true;

	vx_req_account_login_create(&req);
	if(req)
	{
		req->acct_name = vx_strdup(g_VoiceState.username);
		req->acct_password = vx_strdup(g_VoiceState.password);
		req->participant_property_frequency = 100;
		req->base.cookie = vx_strdup(STACK_SPRINTF("%s.Vivox", GetProductName()));
		req->connector_handle = vx_strdup(g_VoiceState.connector_handle);
		req->enable_buddies_and_presence = 1;
		req->enable_text = 1;

		vx_issue_request((vx_req_base_t*)req);
	}

	g_VoiceState.first_channel = true;
}

void svSignOut(const char* reason)
{
	vx_req_account_logout_t *req = NULL;
	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in || g_VoiceState.signing_out) return;

	g_VoiceState.signed_in = false;
	g_VoiceState.signing_out = true;
	
	vx_req_account_logout_create(&req);
	if(req)
	{
		req->account_handle = vx_strdup(g_VoiceState.account_handle);
		req->logout_reason = vx_strdup(reason);

		vx_issue_request((vx_req_base_t*)req);
	}
}

void svShutDown(void)
{
	// It would be nice of us to do this here, but it looks like
	// the game slams shut to fast for it to properly finish ?
	// Note: this hasn't been requested yet
	// svSignOut("svShutDown");
}

VoiceChannelGroup* svChannelGroupFind(int id, const char* handle)
{
	FOR_EACH_IN_EARRAY(g_VoiceState.groups, VoiceChannelGroup, group)
	{
		if(id>0 && id==group->id)
			return group;
		if(handle && !stricmp(handle, group->handle))
			return group;
	}
	FOR_EACH_END

	return NULL;
}

void svChannelGroupSetup(U32 id, U32 recTime)
{
	VoiceChannelGroup *group = StructCreate(parse_VoiceChannelGroup);

	group->id = id;
	group->recTime = recTime;
	eaPush(&g_VoiceState.groups, group);
}

void svChannelGroupRegister(VoiceChannelGroup *group)
{
	vx_req_sessiongroup_create_t *req = NULL;

	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;

	vx_req_sessiongroup_create_create(&req);

	if(req)
	{
		req->base.cookie = vx_strdup(STACK_SPRINTF("%d", group->id));
		req->account_handle = vx_strdup(g_VoiceState.account_handle);		
		req->loop_mode_duration_seconds = group->recTime;
		vx_issue_request((vx_req_base_t*)req);

		group->createState = RS_REQUESTING;
	}
}

void svChannelGroupCreateDefaults(void)
{
	svChannelGroupSetup(SOUND_VOICE_DEF_GROUP_ID, 180);

	g_VoiceState.defGroup = svChannelGroupFind(SOUND_VOICE_DEF_GROUP_ID, NULL);
}

void svChannelGroupDumpRecording(VoiceChannelGroup *group)
{
	vx_req_sessiongroup_control_recording_t *req = NULL;

	if(!group) return;
	if(!group->handle) return;

	vx_req_sessiongroup_control_recording_create(&req);
	if(req)
	{
		req->filename = vx_strdup(STACK_SPRINTF("Group_%d.vxa", group->id));
		req->recording_control_type = VX_SESSIONGROUP_RECORDING_CONTROL_FLUSH_TO_FILE;
		req->sessiongroup_handle = vx_strdup(group->handle);

		vx_issue_request((vx_req_base_t*)req);
	}
}

void svChannelGroupSetHandle(VoiceChannelGroup *group, const char* handle)
{
	SAFE_FREE(group->handle);
	group->handle = strdup(handle);
}

void svChannelGroupFinishCreate(VoiceChannelGroup *group)
{
	group->createState = RS_SUCCESS;
}

void svChannelConnect(VoiceChannelGroup *group, VoiceChannel *chan)
{
	vx_req_sessiongroup_add_session_t *req = NULL;
	
	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;
	if(group->createState!=RS_SUCCESS) return;
	if(chan->disconnectState!=RS_SUCCESS && chan->disconnectState!=RS_NONE) return;

	vx_req_sessiongroup_add_session_create(&req);
	if(req)
	{
		req->connect_audio			= 1;
		req->connect_text			= 0;
		req->sessiongroup_handle	= vx_strdup(group->handle);
		req->uri					= vx_strdup(chan->externName);
		req->base.cookie			= vx_strdup(chan->internName);

		vx_issue_request((vx_req_base_t*)req);

		chan->connectState = RS_REQUESTING;
		chan->disconnectState = RS_NONE;

		filelog_printf("voicechat.log", "Session requesting connect: Uri=%s Intern=%s", chan->externName, chan->internName);
	}
}

void svChannelDisconnect(VoiceChannelGroup *group, VoiceChannel *chan)
{
	vx_req_sessiongroup_remove_session_t *req = NULL;

	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;
	if(chan->connectState!=RS_SUCCESS) return;

	vx_req_sessiongroup_remove_session_create(&req);
	if(req)
	{
		req->session_handle			= vx_strdup(chan->handle);
		req->sessiongroup_handle	= vx_strdup(group->handle);

		vx_issue_request((vx_req_base_t*)req);

		chan->disconnectState = RS_REQUESTING;
		chan->connectState = RS_NONE;

		filelog_printf("voicechat.log", "Session requesting disconnect: Uri=%s Intern=%s", chan->externName, chan->internName);
	}
}

VoiceChannel* svChannelFind(const char* internName, const char* externName, const char* handle)
{
	FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chan)
	{
		if(internName && !stricmp(chan->internName, internName))
			return chan;
		if(externName && !stricmp(chan->externName, externName))
			return chan;
		if(handle && !stricmp(chan->handle, handle))
			return chan;
	}
	FOR_EACH_END

	return NULL;
}

void svChannelJoin(VoiceChannelGroup *group, VoiceChannel *chan, int transmitOnJoin)
{
	if(!chan)
		return;

	if(!group)
		group = g_VoiceState.defGroup;

	if(!chan->group)
	{
		chan->group = group;
		eaPush(&group->channels, chan);
	}

	if(g_VoiceState.ad_state==VAS_CHANNELS_LEAVE)
	{
		filelog_printf("voicechat.log", "Channel Join Failed - During Ad Leave: ex: %s - in: %s", chan->externName, chan->internName);
		return;
	}

	if(chan->connectState==RS_NONE)
	{
		chan->connectState = RS_NEED_REQUEST;
		chan->disconnectState = RS_NONE;

		chan->transmitOnJoin = !!transmitOnJoin;

		filelog_printf("voicechat.log", "Channel Join Request: ex: %s - in: %s", chan->externName, chan->internName);
	}
}

VoiceChannel* svChannelJoinByName(VoiceChannelGroup *group, const char* internName, const char* externName, int transmitOnJoin)
{
	VoiceChannel *chan = svChannelFind(internName, externName, NULL);

	if(!chan)
	{
		chan = StructCreate(parse_VoiceChannel);

		chan->internName = StructAllocString(internName);
		chan->externName = StructAllocString(externName);
		eaPush(&g_VoiceState.channels, chan);
	}	

	svChannelJoin(group, chan, transmitOnJoin);

	return chan;
}

void svChannelSetHandle(VoiceChannel *chan, const char* handle)
{
	chan->handle = StructAllocString(handle);
}

void svCaptureDeviceChannelActivated(VoiceCaptureDevice *device, VoiceChannel *chan)
{
	svChannelSetTransmit(chan);

	svMicrophoneSetLevel(device->recordLevel);
}

void svSetMultiChannelMode(MultiChannelMode mode)
{
	g_VoiceState.options.MultiMode = mode;
	FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chan)
	{
		switch(mode)
		{
			xcase MCM_ONEAUDIBLE: {
				if(chan!=g_VoiceState.active_channel)
					svChannelSetVolume(chan, 0);
			}
			xcase MCM_DUCKINACTIVE: {
				if(chan!=g_VoiceState.active_channel)
					svChannelSetVolume(chan, g_VoiceState.options.DuckPercent*50);
			}
			xcase MCM_ALLEQUAL: {
				svChannelSetVolume(chan, 50);
			}
		}
	}
	FOR_EACH_END
}

void svChannelFinishJoin(VoiceChannel *chan)
{
	chan->connectState = RS_SUCCESS;

	if(chan==g_VoiceState.ad_chan)
		g_VoiceState.time_ad_join = ABS_TIME;

	if(!g_VoiceState.active_channel || chan->transmitOnJoin)
	{
		chan->transmitOnJoin = 0;

		if(g_VoiceState.active_capture_device)
			svCaptureDeviceChannelActivated(g_VoiceState.active_capture_device, chan);
	}
	else if(g_VoiceState.active_channel!=chan)
		svChannelSetInactive(chan);

	if (g_VoiceState.joinCB) {
		g_VoiceState.joinCB(g_VoiceState.tutorials.showFirstConnectTutorial, g_VoiceState.options.OpenMic);
		if (TRUE_THEN_RESET(g_VoiceState.tutorials.showFirstConnectTutorial)) {
			svTutorialsSave();
		}
	}
}

void svChannelGroupDestroy(VoiceChannelGroup *group)
{
	eaFindAndRemoveFast(&g_VoiceState.groups, group);
	if(g_VoiceState.defGroup == group)
		g_VoiceState.defGroup = NULL;

	StructDestroy(parse_VoiceChannelGroup, group);
}

void svChannelClearIfActive(VoiceChannel *chan)
{
	if(g_VoiceState.active_channel==chan)
	{
		g_VoiceState.active_channel->transmitting = false;
		g_VoiceState.active_channel = NULL;
		FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chanTest)
		{
			if(chanTest!=chan && chanTest!=g_VoiceState.ad_chan)
			{
				svChannelSetTransmit(chanTest);
				break;
			}
		}
		FOR_EACH_END
	}
}

void svChannelDestroy(VoiceChannel *chan)
{
	svChannelClearIfActive(chan);
	svDebugUIChanLost(chan);

	eaDestroyEx(&chan->allUsers, svChannelUserDestroy);
	eaFindAndRemoveFast(&chan->group->channels, chan);
	eaFindAndRemoveFast(&g_VoiceState.channels, chan);

	if(g_VoiceState.ad_chan==chan)
		g_VoiceState.ad_chan = NULL;
	if(g_VoiceState.pre_ad_active==chan)
		g_VoiceState.pre_ad_active = NULL;

	StructDestroy(parse_VoiceChannel, chan);
}

void svChannelLeaveFinish(VoiceChannel *chan)
{
	chan->disconnectState = RS_SUCCESS;
	
	svChannelClearIfActive(chan);

	if(g_VoiceState.first_channel_wait)
		g_VoiceState.first_channel_wait = false;

	if(chan->destroyOnLeave)
		svChannelDestroy(chan);

	if (g_VoiceState.leaveCB) {
		g_VoiceState.leaveCB();
	}
}

void svChannelLeave(VoiceChannel *chan, U32 destroy)
{
	if(!chan)
		return;

	if(chan->disconnectState==RS_NONE)
	{
		chan->destroyOnLeave = !!destroy;
		chan->disconnectState = RS_NEED_REQUEST;

		filelog_printf("voicechat.log", "Channel Leave Request - : ex: %s - in: %s", chan->externName, chan->internName);
	}
}

void svChannelLeaveByName(const char* internName, const char* externName, U32 destroy)
{
	VoiceChannel *chan = svChannelFind(internName, externName, NULL);

	svChannelLeave(chan, destroy);
}

const char *svChannelGetName(VoiceChannel *chan, int ext)
{
	if(!chan)
		chan = g_VoiceState.active_channel;

	if(!chan)
		return NULL;

	return ext ? chan->externName : chan->internName;
}

void svChannelSetFontByName(const char* internName, const char* externName, int fontID)
{
	VoiceChannel *chan = svChannelFind(internName, externName, NULL);
	VoiceFont *font = svFontGet(false, fontID, false);
	vx_req_session_set_voice_font_t *req = NULL;

	if(!chan || !font) return;

	vx_req_session_set_voice_font_create(&req);
	if(req)
	{
		req->session_handle = vx_strdup(chan->handle);
		req->session_font_id = fontID;

		vx_issue_request((vx_req_base_t*)req);

		chan->active_font = font;
	}
}

VoiceFont* svChannelGetFontByName(const char* internName, const char* externName)
{
	VoiceChannel *chan = svChannelFind(internName, externName, NULL);

	return chan ? chan->active_font : NULL;
}

void svSetOpenMic(int on)
{
	g_VoiceState.options.OpenMic = on;

	if(g_VoiceState.openMicCB)
		g_VoiceState.openMicCB(g_VoiceState.options.OpenMic);

	if(g_VoiceState.options.OpenMic)
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.micVolume, SFT_FLOAT, SOUND_VOICE_MIC_FADE_RATE);
	else
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.micVolume, SFT_FLOAT, -SOUND_VOICE_MIC_FADE_RATE);

	svOptionsSave();
}

int svMicGetOpen(void)
{
	return g_VoiceState.options.OpenMic;
}

void svPushToTalk(int on)
{
	if(g_VoiceState.options.OpenMic)
		return;

	if(on==g_VoiceState.ptt_on)
		return;

	g_VoiceState.ptt_on = on;
	if(g_VoiceState.ptt_on)
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.micVolume, SFT_FLOAT, SOUND_VOICE_MIC_FADE_RATE);
	else
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.micVolume, SFT_FLOAT, -SOUND_VOICE_MIC_FADE_RATE);
}

void svChannelSetVolume(VoiceChannel *chan, int level)
{
	vx_req_session_set_local_speaker_volume_t *req = NULL;

	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(g_VoiceState.signed_in!=true) return;
	if(!chan || !chan->handle) return;
	
	vx_req_session_set_local_speaker_volume_create(&req);
	if(req)
	{
		req->session_handle = vx_strdup(chan->handle);
		req->volume = level;

		vx_issue_request((vx_req_base_t*)req);
	}
}

void svChannelSetActive(VoiceChannel *chan)
{
	g_VoiceState.active_channel = chan;
	svChannelSetVolume(chan, 50);
}

void svChannelSetInactive(VoiceChannel *chan)
{
	if(g_VoiceState.options.MultiMode==MCM_ONEAUDIBLE)
		svChannelSetVolume(chan, 0);
	else if(g_VoiceState.options.MultiMode==MCM_DUCKINACTIVE)
		svChannelSetVolume(chan, g_VoiceState.options.DuckPercent*50);
}

void svChannelSetTransmit(VoiceChannel *chan)
{
	vx_req_sessiongroup_set_tx_session_t *req = NULL;
	VoiceChannelGroup *group = NULL;

	if(g_VoiceState.connectorState!=RS_SUCCESS) 
	{
		filelog_printf("voicechat.log", "Tried to set transmit when not connected.");
		return;
	}
	if(!g_VoiceState.signed_in) 
	{
		filelog_printf("voicechat.log", "Tried to set transmit when not signed in.");
		return;
	}

	if(!chan)
		chan = g_VoiceState.active_channel;

	if(!chan)
	{
		filelog_printf("voicechat.log", "Tried to set transmit when no channel was considered active.");
		return;
	}

	if(chan == g_VoiceState.ad_chan)
		return;

	if(chan->connectState!=RS_SUCCESS || !chan->handle)
		return;

	group = chan->group;

	if(g_VoiceState.active_channel==chan)
	{
		if(g_VoiceState.active_channel->transmitting)
			return;
	}
	else if(g_VoiceState.active_channel)
	{
		svChannelSetInactive(g_VoiceState.active_channel);
		svClearTransmit();
	}
	
	vx_req_sessiongroup_set_tx_session_create(&req);
	if(req)
	{
		req->sessiongroup_handle = vx_strdup(group->handle);
		req->session_handle = vx_strdup(chan->handle);

		svChannelSetActive(chan);

		g_VoiceState.active_channel->transmitting = true;

		vx_issue_request((vx_req_base_t*)req);
	}
}

void svChannelSetTransmitByName(const char* internName, const char* externName)
{
	VoiceChannel *chan = svChannelFind(internName, externName, NULL);

	svChannelSetTransmit(chan);
}

void svPlayAd(const char* channel)
{
	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;

	FOR_EACH_IN_EARRAY(g_VoiceState.groups, VoiceChannelGroup, group)
	{
		FOR_EACH_IN_EARRAY(group->channels, VoiceChannel, chan)
		{
			svChannelLeave(chan, 0);
		}
		FOR_EACH_END
	}
	FOR_EACH_END

	g_VoiceState.pre_ad_active = g_VoiceState.active_channel;
	g_VoiceState.ad_state = VAS_CHANNELS_LEAVE;
}

void svClearTransmit(void)
{
	vx_req_sessiongroup_set_tx_no_session_t *req = NULL;

	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;
	if(!g_VoiceState.active_channel || !g_VoiceState.active_channel->transmitting) return;

	vx_req_sessiongroup_set_tx_no_session_create(&req);
	if(req)
	{
		req->sessiongroup_handle = vx_strdup(g_VoiceState.active_channel->group->handle);
		
		g_VoiceState.active_channel->transmitting = false;
		vx_issue_request((vx_req_base_t*)req);
	}
}

int svTransmitIsEnabled(void)
{
	return g_VoiceState.active_channel && g_VoiceState.active_channel->transmitting;
}

static void svCaptureDeviceSetLevel(VoiceCaptureDevice *device, int level, int scaled)
{
	vx_req_aux_set_mic_level_t *req = NULL;

	if(!device)
		device = g_VoiceState.active_capture_device;

	vx_req_aux_set_mic_level_create(&req);
	if(req)
	{
		// Rescale
		if(!scaled)
			level = 25 + level*0.5;
		req->level = level;
		vx_issue_request((vx_req_base_t*)req);

		svCaptureDevicePrefsSave();
	}
}

void svMicrophoneSetLevel(int level)
{
	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;
	if(!g_VoiceState.active_capture_device) return;

	if(g_VoiceState.active_capture_device)
		g_VoiceState.active_capture_device->recordLevel = level;

	if(g_VoiceState.micCB)
		g_VoiceState.micCB(level);

	g_VoiceState.options.MicLevel = level;
}

void svMicrophoneSetMute(bool mute)
{
	vx_req_connector_mute_local_mic_t *req = NULL;

	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;

	vx_req_connector_mute_local_mic_create(&req);
	if(req)
	{
		req->connector_handle = vx_strdup(g_VoiceState.connector_handle);
		req->mute_level = mute;

		vx_issue_request((vx_req_base_t*)req);
	}
}

int svMicrophoneGetLevel(void)
{
	if(!g_VoiceState.active_capture_device) return 0;
	return g_VoiceState.active_capture_device->recordLevel;
}

void svSpeakersSetLevel(int level)
{
	vx_req_aux_set_speaker_level_t *req = NULL;

	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;

	vx_req_aux_set_speaker_level_create(&req);
	if(req)
	{
		if(g_VoiceState.volCB)
			g_VoiceState.volCB(level);

		g_VoiceState.options.SpeakerVolume = level;

		level = 25 + level * 0.5;
		req->level = level;
		vx_issue_request((vx_req_base_t*)req);
	}
}

U32 svAccountToContainerId(const char* accountName)
{
	const char* idStr = NULL;
	if(!accountName)
		return -1;

	// Comes from the server!
	idStr = strrchr(accountName, '_')+1;
	return atoi(idStr);
}

void svUserPrefsSave(VoiceUser *user)
{
	if(user->isMutedByMe!=user->prefs.mutedByMe ||
		user->volume!=user->prefs.volume)
	{
		char buf[MAX_PATH];

		user->prefs.mutedByMe = user->isMutedByMe;
		user->prefs.volume = user->volume;

		sprintf(buf, "svUserPrefs_%d", user->id);
		svPrefsSave(parse_VoiceUserPrefs, buf, &user->prefs);
	}
}

void svUserPrefsLoad(VoiceUser *user)
{
	char buf[MAX_PATH];

	sprintf(buf, "svUserPrefs_%d", user->id);

	StructInit(parse_VoiceUserPrefs, &user->prefs);
	if (svConfigIsFieldSpecified("defaultUserVolume"))
		user->prefs.volume = g_VoiceState.config.defaultUserVolume;
	
	GamePrefGetStruct(buf, parse_VoiceUserPrefs, &user->prefs);
	
	svUserSetMute(user, user->prefs.mutedByMe);
	svUserSetVolume(user, user->prefs.volume);
}

void svUserUpdateIgnore(int accountId, int ignored)
{
	VoiceUser *user = svFindUser(accountId);

	if(!user)
		return;

	svUserSetMute(user, ignored);
}

VoiceChannelUser* svChannelAddUser(VoiceChannel *chan, VoiceUser *user)
{
	VoiceChannelUser *vcu = StructCreate(parse_VoiceChannelUser);

	vcu->chan = chan;
	vcu->user = user;

	eaPush(&chan->allUsers, vcu);
	eaPush(&user->channels, vcu);

	if(g_VoiceState.isIgnoredCB && g_VoiceState.isIgnoredCB(user->id))
		svChannelUserSetMute(chan, user, true);
	else
		svChannelUserSetMute(chan, user, user->prefs.mutedByMe);

	svChannelSetUserVolume(chan, user, user->prefs.volume);

	return vcu;
}

VoiceChannelUser* svChannelFindUser(VoiceChannel *chan, ContainerID id, const char* externName, const char* externURI, int create)
{
	VoiceChannelUser *vcu = NULL;
	VoiceUser *user = NULL;
	FOR_EACH_IN_EARRAY(chan->allUsers, VoiceChannelUser, userSearch)
	{
		if(userSearch->user->id==id)
			return userSearch;
		if(!stricmp(userSearch->user->externName, externName))
			return userSearch;
		if(!stricmp(userSearch->user->externURI, externURI))
			return userSearch;
	}
	FOR_EACH_END

	user = svFindUser(id);

	if(!user && create)
		user = svCreateUser(externName, externURI, id);

	if(!user)
		return NULL;

	return svChannelAddUser(chan, user);
}

void svChannelUserDestroy(VoiceChannelUser *vcu)
{
	eaFindAndRemoveFast(&vcu->chan->allUsers, vcu);
	eaFindAndRemoveFast(&vcu->user->channels, vcu);

	if(eaSize(&vcu->user->channels)==0)
		svUserDestroy(vcu->user);

	StructDestroy(parse_VoiceChannelUser, vcu);
}

void svChannelRemoveUser(VoiceChannel *chan, ContainerID id)
{
	FOR_EACH_IN_EARRAY(chan->allUsers, VoiceChannelUser, vcu)
	{
		if(vcu->user->id==id)
		{
			svChannelUserDestroy(vcu);
			return;
		}
	}
	FOR_EACH_END
}

const char* svGetLoginStateString(vx_login_state_change_state state)
{
#define STATESTR(state) xcase state: return #state;
	switch(state)
	{
		STATESTR(login_state_logged_out)
		STATESTR(login_state_logged_in)
		STATESTR(login_state_logging_out)
		STATESTR(login_state_logging_in)
		STATESTR(login_state_error)
	}
#undef STATESTR

	return "Undefined state";
}

VoiceCaptureDevice* svCaptureDeviceFind(const char* name, int create)
{
	VoiceCaptureDevice *newDevice;

	if(!name || !name[0])
		return NULL;

	FOR_EACH_IN_EARRAY(g_VoiceState.capture_devices, VoiceCaptureDevice, device)
	{
		if(!device)
			continue;

		if(!stricmp(device->deviceName, name))
			return device;
	}
	FOR_EACH_END

	if(!create)
		return NULL;

	newDevice = StructCreate(parse_VoiceCaptureDevice);
	newDevice->deviceName = StructAllocString(name);
	newDevice->recordLevel = g_VoiceState.options.MicLevel;
	eaPush(&g_VoiceState.capture_devices, newDevice);

	return newDevice;
}

VoiceRenderDevice* svRenderDeviceFind(const char* name, int create)
{
	VoiceRenderDevice *newDevice;

	if(!name || !name[0])
		return NULL;

	FOR_EACH_IN_EARRAY(g_VoiceState.render_devices, VoiceRenderDevice, device)
	{
		if(!device)
			continue;

		if(!stricmp(device->deviceName, name))
			return device;
	}
	FOR_EACH_END

	if(!create)
		return NULL;

	newDevice = StructCreate(parse_VoiceRenderDevice);
	newDevice->deviceName = StructAllocString(name);
	newDevice->renderLevel = g_VoiceState.options.SpeakerVolume;
	eaPush(&g_VoiceState.render_devices, newDevice);

	return newDevice;
}

#define VOICE_CAPTURE_LIST_PREFS "VoiceCaptureList"
#define VOICE_RENDER_LIST_PREFS "VoiceRenderList"

void svPrefsSave(ParseTable *pti, const char* prefName, void *s)
{
	GamePrefStoreStruct(prefName, pti, s);
}

void svCaptureDevicePrefsSave(void)
{
	VoiceCaptureDeviceList list = {0};

	list.devices = g_VoiceState.capture_devices;
	svPrefsSave(parse_VoiceCaptureDeviceList, VOICE_CAPTURE_LIST_PREFS, &list);
}

void svCaptureDeviceUpdateSettings(VoiceCaptureDevice *device)
{
	
}

void svCaptureDeviceSetActiveFinish(VoiceCaptureDevice *device)
{
	if (device) {
		g_VoiceState.active_capture_device = device;
		device->isActive = true;
	}

	svCaptureDevicePrefsSave();

	if (g_VoiceState.active_channel) {
		if (device) {
			svCaptureDeviceChannelActivated(device, g_VoiceState.active_channel);
		} else {
			svChannelSetInactive(g_VoiceState.active_channel);
			svClearTransmit();
		}
	}
}

void svCaptureDeviceSetActive(VoiceCaptureDevice *device)
{
	vx_req_aux_set_capture_device_t *req = NULL;

	if(g_VoiceState.active_capture_device == device)
		return;
	else if(g_VoiceState.active_capture_device)
	{
		g_VoiceState.active_capture_device->isActive = 0;
		g_VoiceState.active_capture_device = NULL;
	}

	vx_req_aux_set_capture_device_create(&req);
	if(req)
	{
		req->capture_device_specifier = vx_strdup(device->deviceName);
		vx_issue_request((vx_req_base_t*)req);
	}
}

void svNoCaptureDevicesAreActive(void)
{
	vx_req_aux_set_capture_device_t *req = NULL;

	if (g_VoiceState.active_capture_device)
	{
		g_VoiceState.active_capture_device->isActive = 0;
		g_VoiceState.active_capture_device = NULL;
	}
	
	vx_req_aux_set_capture_device_create(&req);
	if(req)
	{
		req->capture_device_specifier = vx_strdup("No Device");
		vx_issue_request((vx_req_base_t*)req);
	}
}

void svCaptureDeviceSetActiveByName(const char* name)
{
	VoiceCaptureDevice *device = svCaptureDeviceFind(name, false);

	if(device)
		svCaptureDeviceSetActive(device);
}

void svRenderDevicePrefsSave(void)
{
	VoiceRenderDeviceList list = {0};

	list.devices = g_VoiceState.render_devices;
	svPrefsSave(parse_VoiceRenderDeviceList, VOICE_RENDER_LIST_PREFS, &list);
}

void svRenderDeviceUpdateSettings(VoiceRenderDevice *device)
{

}

void svRenderDeviceSetActiveFinish(VoiceRenderDevice *device)
{
	if (device) {
		g_VoiceState.active_render_device = device;
		device->isActive = true;
	}
	
	svRenderDevicePrefsSave();
}

void svRenderDeviceSetActive(VoiceRenderDevice *device)
{
	vx_req_aux_set_render_device_t *req = NULL;

	if(g_VoiceState.active_render_device == device)
		return;
	else if(g_VoiceState.active_render_device)
	{
		g_VoiceState.active_render_device->isActive = 0;
		g_VoiceState.active_render_device = NULL;
	}

	vx_req_aux_set_render_device_create(&req);
	if(req)
	{
		req->render_device_specifier = vx_strdup(device->deviceName);
		vx_issue_request((vx_req_base_t*)req);
	}
}

void svNoRenderDevicesAreActive(void)
{
	vx_req_aux_set_render_device_t *req = NULL;

	if(g_VoiceState.active_render_device)
	{
		g_VoiceState.active_render_device->isActive = 0;
		g_VoiceState.active_render_device = NULL;
	}

	vx_req_aux_set_render_device_create(&req);
	if(req)
	{
		req->render_device_specifier = vx_strdup("No Device");
		vx_issue_request((vx_req_base_t*)req);
	}
}

void svRenderDeviceSetActiveByName(const char* name)
{
	VoiceRenderDevice *device = svRenderDeviceFind(name, false);

	if(device)
		svRenderDeviceSetActive(device);
}

void svDeviceListsChanged(void)
{
	vx_req_aux_get_capture_devices_t *cap_req = NULL;
	vx_req_aux_get_render_devices_t *rdr_req = NULL;

	vx_req_aux_get_capture_devices_create(&cap_req);
	if(cap_req) {
		g_VoiceState.use_default_capture_device = 1;
		vx_issue_request((vx_req_base_t*)cap_req);
	}

	vx_req_aux_get_render_devices_create(&rdr_req);
	if(rdr_req) {
		g_VoiceState.use_default_render_device = 1;
		vx_issue_request((vx_req_base_t*)rdr_req);
	}
}

void svInitPrefs(void)
{
	VoiceCaptureDeviceList capList = {0};
	VoiceRenderDeviceList rdrList = {0};
	vx_req_aux_get_capture_devices_t *cap_req = NULL;
	vx_req_aux_get_render_devices_t *rdr_req = NULL;

	GamePrefGetStruct("VoiceCaptureList", parse_VoiceCaptureDeviceList, &capList);
	GamePrefGetStruct("VoiceRenderList", parse_VoiceRenderDeviceList, &rdrList);
	
	vx_req_aux_get_capture_devices_create(&cap_req);
	if(cap_req) {
		g_VoiceState.use_default_capture_device = 1;
		vx_issue_request((vx_req_base_t*)cap_req);
	}

	vx_req_aux_get_render_devices_create(&rdr_req);
	if(rdr_req) {
		g_VoiceState.use_default_render_device = 1;
		vx_issue_request((vx_req_base_t*)rdr_req);
	}

	if(capList.devices)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(capList.devices, VoiceCaptureDevice, device)
		{
			if(device)
				eaPush(&g_VoiceState.capture_devices, device);
		}
		FOR_EACH_END

		eaDestroy(&capList.devices);
	}

	if(rdrList.devices)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(rdrList.devices, VoiceRenderDevice, device)
		{
			if(device)
				eaPush(&g_VoiceState.render_devices, device);
		}
		FOR_EACH_END

		eaDestroy(&rdrList.devices);
	}

	// Setup initial state
	svSetOpenMic(g_VoiceState.options.OpenMic);
	svMicrophoneSetLevel(g_VoiceState.options.MicLevel);
	svSpeakersSetLevel(g_VoiceState.options.SpeakerVolume);
	svSetMultiChannelMode(g_VoiceState.options.MultiMode);
}

void svVoiceChannelUserDestroy(VoiceChannelUser *vcu)
{
	eaFindAndRemoveFast(&vcu->chan->allUsers, vcu);
	eaFindAndRemoveFast(&vcu->user->channels, vcu);

	StructDestroy(parse_VoiceChannelUser, vcu);
}

void svChannelSignOut(VoiceChannel *chan)
{
	char *internName, *externName;
	StructFreeStringSafe(&chan->handle);
	
	eaDestroyEx(&chan->allUsers, svVoiceChannelUserDestroy);

	internName = chan->internName;
	externName = chan->externName;
	ZeroStruct(chan);
	chan->internName = internName;
	chan->externName = externName;

	if(g_VoiceState.active_channel==chan)
	{
		g_VoiceState.active_channel = NULL;
		if(eaSize(&g_VoiceState.channels))
			svChannelSetTransmit(g_VoiceState.channels[0]);
	}
}

void svChannelGroupSignOut(VoiceChannelGroup *group)
{
	if(group!=g_VoiceState.defGroup)
		svChannelGroupDestroy(group);
	else
	{
		StructFreeStringSafe(&group->handle);
		group->createState = RS_NONE;
		eaDestroy(&group->channels);
	}
}

void svChannelConnectFailure(VoiceChannel *chan)
{
	chan->connectFailures++;

	if(g_VoiceState.first_channel_wait)
	{
		g_VoiceState.first_channel = true;
		g_VoiceState.first_channel_wait = false;
	}

	if(chan->connectFailures<20)
	{
		chan->timeToWait = ABS_TIME+SEC_TO_ABS_TIME(powf(chan->connectFailures+1,1.5));
		chan->connectState = RS_NEED_REQUEST;

		filelog_printf("voicechat.log", "Channel Connect Failure %d - ex: %s - in: %s", chan->connectFailures, chan->externName, chan->internName);
	}
	else
	{
		ErrorDetailsf(" %s | %s | %s", chan->internName, chan->externName, g_VoiceState.username);
		Errorf("Failed to connect to channel 20 times");
	}

	if (g_VoiceState.failureCB) {
		g_VoiceState.failureCB();
	}
}

void svProcessMessages(void)
{
	vx_message_base_t *msg = NULL;

	while(vx_get_message(&msg)==0)
	{
		if(msg->type == msg_response) 
		{ 
			vx_resp_base_t *resp = (vx_resp_base_t *)msg; 

			if(resp->return_code!=0)
			{
				filelog_printf("voicechat.log", "Error %d/%d-%s in resp %d\n", resp->return_code, resp->status_code, resp->status_string, resp->type);
				//printf("Error %d/%d-%s in resp %d\n", resp->return_code, resp->status_code, resp->status_string, resp->type);
			}

			switch(resp->type)
			{
				xcase resp_connector_create: {
					vx_resp_connector_create_t *spec = (vx_resp_connector_create_t*)resp;

					if(resp->return_code==0)
					{
						g_VoiceState.connectorState = RS_SUCCESS;

#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*g_VoiceState[84]'"
						SAFE_FREE(g_VoiceState.connector_handle);
						g_VoiceState.connector_handle = strdup(spec->connector_handle);

						//printf("Signed into Vivox (%s)\n", g_VoiceState.acct_server);
					}
					else
						filelog_printf("voicechat.log", "Error connecting to Vivox: %d %s\n", resp->status_code, resp->status_string);
				}
				xcase resp_connector_mute_local_mic: {
					vx_resp_connector_mute_local_mic_t *spec = (vx_resp_connector_mute_local_mic_t*)resp;

					filelog_printf("voicechat.log", "Mute Local Mic resp");
				}
				xcase resp_aux_get_capture_devices: {
					vx_resp_aux_get_capture_devices_t *spec = (vx_resp_aux_get_capture_devices_t*)resp;
					VoiceCaptureDevice *firstFoundDevice = NULL;
					int activeFound = 0;
					int captureDevId = -1;
					int i;

					FOR_EACH_IN_EARRAY(g_VoiceState.capture_devices, VoiceCaptureDevice, device) {
						device->found = false;
					} FOR_EACH_END

					for(i=0; i<spec->count; i++)
					{
						vx_device_t *vxdevice = spec->capture_devices[i];
						VoiceCaptureDevice *device = NULL;
						
						if(vxdevice->device_type!=vx_device_type_specific_device)
							continue;
						
						device = svCaptureDeviceFind(vxdevice->device, true);

						if(!device)
							continue;

						device->found = true;
						firstFoundDevice = FIRST_IF_SET(firstFoundDevice, device);
					}

					if (!g_VoiceState.use_default_capture_device)
					{
						FOR_EACH_IN_EARRAY(g_VoiceState.capture_devices, VoiceCaptureDevice, device)
						{
						if(device->isActive && device->found)
						{
								svCaptureDeviceSetActive(device);
								captureDevId = ideviceIndex;
								activeFound = true;							
							}
						}
						FOR_EACH_END
					}
					
					if(!activeFound)
					{
						if (firstFoundDevice &&
							eaSize(&g_VoiceState.capture_devices))
						{
							FOR_EACH_IN_EARRAY(g_VoiceState.capture_devices, VoiceCaptureDevice, chkForDevice) {
								if (firstFoundDevice == chkForDevice) {
									svCaptureDeviceSetActive(firstFoundDevice);
									captureDevId = ichkForDeviceIndex;
									break;
								}
							} FOR_EACH_END;
						} else {
							svNoCaptureDevicesAreActive();
						}
					}

					if(g_VoiceState.inputListCB)
					{
						int useInputDevId = 0;
						static char **eStrings = NULL;
						eaClearEString(&eStrings);

						for(i=0; i<spec->count; i++)
						{
							char *estr = NULL;
							if(spec->capture_devices[i]->device_type!=vx_device_type_specific_device)
								continue;
							estrPrintf(&estr, "%s", spec->capture_devices[i]->device);
							eaPush(&eStrings, estr);

							if (captureDevId >= 0 &&
								!stricmp(spec->capture_devices[i]->device, g_VoiceState.capture_devices[captureDevId]->deviceName))
							{
								useInputDevId = i;
							}
						}

						g_VoiceState.inputListCB(eStrings);
						g_VoiceState.inputCB(useInputDevId);
					}

					g_VoiceState.use_default_capture_device = 0;
				}
				xcase resp_aux_set_capture_device: {
					vx_resp_aux_set_capture_device_t *spec = (vx_resp_aux_set_capture_device_t*)resp;
					vx_req_aux_set_capture_device_t *req = (vx_req_aux_set_capture_device_t*)spec->base.request;
					VoiceCaptureDevice *device = svCaptureDeviceFind(req->capture_device_specifier, false);

					if(!device &&
						stricmp("No Device",req->capture_device_specifier))
					{
						filelog_printf("voicechat.log", "Set capture device that doesn't exist: %s", req->capture_device_specifier);
						break;
					}

					svCaptureDeviceSetActiveFinish(device);
				}
				xcase resp_aux_get_render_devices: {
					vx_resp_aux_get_render_devices_t *spec = (vx_resp_aux_get_render_devices_t*)resp;
					VoiceRenderDevice* firstFoundDevice = NULL;
					int activeFound = 0;
					int renderDevId = -1;
					int i;
					
					FOR_EACH_IN_EARRAY(g_VoiceState.render_devices, VoiceRenderDevice, device) {
						device->found = false;
					} FOR_EACH_END

					for(i=0; i<spec->count; i++)
					{
						vx_device_t *vxdevice = spec->render_devices[i];
						VoiceRenderDevice *device = NULL;

						if(vxdevice->device_type!=vx_device_type_specific_device)
							continue;

						device = svRenderDeviceFind(vxdevice->device, true);

						if(!device)
							continue;

						device->found = true;
						firstFoundDevice = FIRST_IF_SET(firstFoundDevice, device);
					}

					if (!g_VoiceState.use_default_render_device)
					{
						FOR_EACH_IN_EARRAY(g_VoiceState.render_devices, VoiceRenderDevice, device)
						{
						if(device->isActive && device->found)
						{
								svRenderDeviceSetActive(device);
								renderDevId = ideviceIndex;
								activeFound = true;							
							}
						}
						FOR_EACH_END
					}

					if(!activeFound)
					{
						if (firstFoundDevice &&
							eaSize(&g_VoiceState.render_devices))
						{
							FOR_EACH_IN_EARRAY(g_VoiceState.render_devices, VoiceRenderDevice, chkDevice) {
								if (chkDevice == firstFoundDevice) {
									svRenderDeviceSetActive(firstFoundDevice);
									renderDevId = ichkDeviceIndex;
									break;
								}
							} FOR_EACH_END;
						} else {
							svNoRenderDevicesAreActive();
						}
					}

					if(g_VoiceState.outputListCB)
					{
						int useOutputDevId = 0;
						static char **eStrings = NULL;
						eaClearEString(&eStrings);

						for(i=0; i<spec->count; i++)
						{
							char *estr = NULL;
							if(spec->render_devices[i]->device_type!=vx_device_type_specific_device)
								continue;
							estrPrintf(&estr, "%s", spec->render_devices[i]->device);
							eaPush(&eStrings, estr);

							if (renderDevId >= 0 &&
								!stricmp(spec->render_devices[i]->device, g_VoiceState.render_devices[renderDevId]->deviceName))
							{
								useOutputDevId = i;
							}
						}

						g_VoiceState.outputListCB(eStrings);
						g_VoiceState.outputCB(useOutputDevId);
					}

					g_VoiceState.use_default_render_device = 0;
				}
				xcase resp_aux_set_render_device: {
					vx_resp_aux_set_render_device_t *spec = (vx_resp_aux_set_render_device_t*)resp;
					vx_req_aux_set_render_device_t *req = (vx_req_aux_set_render_device_t*)spec->base.request;
					VoiceRenderDevice *device = svRenderDeviceFind(req->render_device_specifier, false);

					if(!device &&
						stricmp("No Device",req->render_device_specifier))
					{
						filelog_printf("voicechat.log", "Set render device that doesn't exist: %s", req->render_device_specifier);
						break;
					}

					svRenderDeviceSetActiveFinish(device);
				}
				xcase resp_account_login: {
					vx_resp_account_login_t *spec = (vx_resp_account_login_t*)resp;

					if(resp->return_code==0)
					{
						devassert(g_VoiceState.acctid==spec->account_id);

						SAFE_FREE(g_VoiceState.account_handle);
						g_VoiceState.account_handle = strdup(spec->account_handle);

						SAFE_FREE(g_VoiceState.acct_uri);
						g_VoiceState.acct_uri = strdup(spec->uri);
					}
					else if(resp->return_code==1)
					{
						switch(resp->status_code)
						{
							xcase 20200: {
								if(g_VoiceState.time_verify_req == 0 || 
									ABS_TIME_SINCE(g_VoiceState.time_verify_req)>SEC_TO_ABS_TIME(60))
								{
									g_VoiceState.time_verify_req = ABS_TIME;

									//printf("Requesting voice username/password verify");
									if(g_audio_state.verify_voice_func)
										g_audio_state.verify_voice_func();
								}
							}
						}
					}
				}
				xcase resp_account_logout: {
					vx_resp_account_logout_t *spec = (vx_resp_account_logout_t*)resp;
					
					filelog_printf("voicechat.log", "Logged out of Vivox: %d %s\n", resp->status_code, resp->status_string);
				}
				xcase resp_sessiongroup_create: {
					vx_resp_sessiongroup_create_t *spec = (vx_resp_sessiongroup_create_t*)resp;
					int id = atoi(resp->request->cookie);
					VoiceChannelGroup *group = svChannelGroupFind(id, NULL);

					svChannelGroupSetHandle(group, spec->sessiongroup_handle);
				}
				xcase resp_sessiongroup_add_session: {
					vx_resp_sessiongroup_add_session_t *spec = (vx_resp_sessiongroup_add_session_t*)resp;
					VoiceChannel *chan = svChannelFind(resp->request->cookie, NULL, NULL);

					if(!chan)
						break;

					if(resp->status_code!=0)
					{
						if(chan==g_VoiceState.ad_chan)
							svChannelDestroy(chan);
						else
							svChannelConnectFailure(chan);
					}
					else 
						svChannelSetHandle(chan, spec->session_handle);
				}
				xcase resp_sessiongroup_remove_session: {
					vx_resp_sessiongroup_remove_session_t *spec = (vx_resp_sessiongroup_remove_session_t*)resp;
					vx_req_sessiongroup_remove_session_t* req = (vx_req_sessiongroup_remove_session_t*)spec->base.request;
				}
				xcase resp_aux_start_buffer_capture: {
					vx_resp_aux_start_buffer_capture_t* spec = (vx_resp_aux_start_buffer_capture_t*)resp;
					
					g_VoiceState.record_active = true;
					filelog_printf("voicechat.log", "Record start");
				}
				xcase resp_aux_capture_audio_stop: {
					vx_resp_aux_capture_audio_stop_t* spec = (vx_resp_aux_capture_audio_stop_t*)resp;
					
					g_VoiceState.record_active = false;
					filelog_printf("voicechat.log", "Record stopped");
				}
				xcase resp_aux_play_audio_buffer: {
					vx_resp_aux_play_audio_buffer_t* spec = (vx_resp_aux_play_audio_buffer_t*)resp;
					//printf("");
				}
				xcase resp_sessiongroup_set_tx_no_session: {
					vx_resp_sessiongroup_set_tx_no_session_t *spec = (vx_resp_sessiongroup_set_tx_no_session_t*)resp;
					filelog_printf("voicechat.log", "Clear transmit");
				}
				xcase resp_sessiongroup_set_tx_session: {
					vx_resp_sessiongroup_set_tx_session_t* spec = (vx_resp_sessiongroup_set_tx_session_t*)resp;
					vx_req_sessiongroup_set_tx_session_t *req = (vx_req_sessiongroup_set_tx_session_t*)resp->request;
					filelog_printf("voicechat.log", "Set transmit: Handle-%s", req->session_handle);
				}
				xcase resp_aux_set_mic_level: {
					vx_resp_aux_set_mic_level_t* spec = (vx_resp_aux_set_mic_level_t*)resp;
					vx_req_aux_set_mic_level_t *req = (vx_req_aux_set_mic_level_t*)resp->request;
					filelog_printf("voicechat.log", "Set mic level: %d", req->level);
				}
				xcase resp_aux_set_speaker_level: {
					vx_resp_aux_set_speaker_level_t* spec = (vx_resp_aux_set_speaker_level_t*)resp;
					vx_req_aux_set_speaker_level_t *req = (vx_req_aux_set_speaker_level_t*)resp->request;
					filelog_printf("voicechat.log", "Set speaker level: %d", req->level);
				}
				xcase resp_account_get_template_fonts: {
					vx_resp_account_get_template_fonts_t* spec = (vx_resp_account_get_template_fonts_t*)resp;

					svFontsGetFinish(true, spec->template_fonts, spec->template_font_count);
				}
				xcase resp_account_get_session_fonts: {
					vx_resp_account_get_session_fonts_t* spec = (vx_resp_account_get_session_fonts_t*)resp;

					svFontsGetFinish(false, spec->session_fonts, spec->session_font_count);
				}
				xcase resp_session_set_participant_mute_for_me: {}
				xcase resp_session_set_participant_volume_for_me: {}
				xcase resp_aux_render_audio_stop: {}
				xdefault: {
					filelog_printf("voicechat.log", "Vivox unhandled resp: %d\n", resp->type);
				}
			}
			
			destroy_resp(resp); 
		} 
		else if(msg->type == msg_event) 
		{ 
			vx_evt_base_t *evt = (vx_evt_base_t *)msg; 

			switch(evt->type)
			{
				xcase evt_buddy_and_group_list_changed: {}
				xcase evt_account_login_state_change: {
					vx_evt_account_login_state_change_t *spec = (vx_evt_account_login_state_change_t*)evt;

					if(spec->state==login_state_logged_in)
					{
						g_VoiceState.signing_in = false;
						g_VoiceState.signing_out = false;
						g_VoiceState.signed_in = true;
						g_VoiceState.needsFonts = true;

						//printf("Signed into Vivox as %s\n", g_VoiceState.username);
						filelog_printf("voicechat.log", "Login State Change: Signed In - handle=%s un=%s s=%s", spec->account_handle, g_VoiceState.username, g_VoiceState.acct_server);
					}
					else if(spec->state==login_state_logged_out)
					{
						FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chan)
						{
							if(chan->adchannel)
								svChannelDestroy(chan);
							else
								svChannelSignOut(chan);
						}
						FOR_EACH_END
						eaForEach(&g_VoiceState.groups, svChannelGroupSignOut);
						g_VoiceState.active_channel = NULL;
						g_VoiceState.signed_in = false;
						g_VoiceState.signing_in = false;
						g_VoiceState.signing_out = false;

						//printf("Signed out of Vivox\n");
						filelog_printf("voicechat.log", "Login State Change: Signed Out - handle=%s", spec->account_handle);
					}
					else
					{
						filelog_printf("voicechat.log", "Login State Change: handle=%s state=%s", spec->account_handle, svGetLoginStateString(spec->state));
					}
				}
				xcase evt_sessiongroup_added: {
					vx_evt_sessiongroup_added_t* spec = (vx_evt_sessiongroup_added_t*)evt;
					VoiceChannelGroup *group = svChannelGroupFind(-1, spec->sessiongroup_handle);

					svChannelGroupFinishCreate(group);

					filelog_printf("voicechat.log", "Sessiongroup Add Event: uri=%s", spec->sessiongroup_handle);
				}
				xcase evt_session_added: {
					vx_evt_session_added_t* spec = (vx_evt_session_added_t*)evt;
					VoiceChannel *chan = svChannelFind(NULL, spec->uri, NULL);

					if(!chan)
						break;

					filelog_printf("voicechat.log", "Session Added: Uri=%s Intern=%s", spec->uri, chan->internName);
					
					svChannelFinishJoin(chan);
					//svMicrophoneSetMute(0);
				}
				xcase evt_session_removed: {
					vx_evt_session_removed_t* spec = (vx_evt_session_removed_t*)evt;
					VoiceChannel *chan = svChannelFind(NULL, NULL, spec->session_handle);

					if(chan && chan->disconnectState==RS_NONE)
						svChannelConnectFailure(chan);
					else if(chan)
						svChannelLeaveFinish(chan);

					filelog_printf("voicechat.log", "Session Removed: Uri=%s", spec->uri);
				}
				xcase evt_session_updated: {
					vx_evt_session_updated_t *spec = (vx_evt_session_updated_t*)evt;
					VoiceChannel *chan = svChannelFind(NULL, NULL, spec->session_handle);

					if(!chan)
						break;

					chan->ext_state.volume = spec->volume/100.0f;
					chan->ext_state.transmitting = spec->transmit_enabled;
					chan->ext_state.muted = spec->is_muted;
					chan->ext_state.fontID = spec->session_font_id;

					if(!spec->transmit_enabled && chan->transmitting)
					{
						chan->transmitting = false;
						svChannelSetTransmit(chan);
					}

					if(spec->is_ad_playing)
					{
						if(chan==g_VoiceState.ad_chan)
						{
							if(!g_VoiceState.ad_started && g_VoiceState.adplayCB)
								g_VoiceState.adplayCB();
							g_VoiceState.ad_started = true;
						}
						else
						{
							Errorf("Advertisement playing on non-ad channel. Exp: %p %s | Rec: %p %s",
									g_VoiceState.ad_chan, g_VoiceState.ad_chan ? g_VoiceState.ad_chan->internName : "NULL",
									chan, chan ? chan->internName : "NULL");
						}
					}
					else
					{
						if(chan==g_VoiceState.ad_chan && g_VoiceState.ad_started)
						{
							svChannelLeave(g_VoiceState.ad_chan, true);
							g_VoiceState.ad_started = false;
						}
					}
						
					filelog_printf("voicechat.log", "Session updated: Uri=%s Muted=%d Font=%d", spec->uri, spec->is_muted, spec->session_font_id);
				}
				xcase evt_sessiongroup_updated: {
					vx_evt_sessiongroup_updated_t* spec = (vx_evt_sessiongroup_updated_t*)evt;
				}
				xcase evt_media_stream_updated: {
					vx_evt_media_stream_updated_t* spec = (vx_evt_media_stream_updated_t*)evt;
				}
				xcase evt_text_stream_updated: {
					vx_evt_text_stream_updated_t* spec = (vx_evt_text_stream_updated_t*)evt;
				}
				xcase evt_aux_audio_properties: {
					vx_evt_aux_audio_properties_t *spec = (vx_evt_aux_audio_properties_t*)evt;

					if(g_VoiceState.record_active)
					{
						g_VoiceState.record_complete = false;
						g_VoiceState.record_mic_active = !!spec->mic_is_active;
						g_VoiceState.record_energy = interpF32(0.7, spec->mic_energy, g_VoiceState.record_energy);
					}
				}
				xcase evt_media_completion: {
					vx_evt_media_completion_t* spec = (vx_evt_media_completion_t*)evt;

					if(g_VoiceState.record_active)
					{
						if(spec->completion_type==aux_buffer_audio_capture)
							g_VoiceState.record_complete = true;
						else if(spec->completion_type==aux_buffer_audio_render)
						{
							g_VoiceState.record_playback_complete = true;
							g_VoiceState.record_playback_stopped = true;
						}
					}
				}
				xcase evt_participant_added: {
					vx_evt_participant_added_t* spec = (vx_evt_participant_added_t*)evt;
					ContainerID id = svAccountToContainerId(spec->account_name);
					VoiceChannel *chan = svChannelFind(NULL, NULL, spec->session_handle);
					
					if(!chan)
					{
						filelog_printf("voicechat.log", "Participant Add failed - No channel: handle=%s", spec->session_handle);
						break;
					}

					if(id<0)
					{
						filelog_printf("voicechat.log", "Participant Add failed - no user: Acct=%s Uri=%s", spec->account_name, spec->participant_uri);
						break;
					}

					svChannelFindUser(chan, id, spec->account_name, spec->participant_uri, true);

					g_VoiceState.first_channel = false;
					g_VoiceState.first_channel_wait = false;
				}
				xcase evt_participant_removed: {
					vx_evt_participant_removed_t* spec = (vx_evt_participant_removed_t*)evt;
					ContainerID id = svAccountToContainerId(spec->account_name);
					VoiceChannel *chan = svChannelFind(NULL, NULL, spec->session_handle);

					if(!chan)
					{
						filelog_printf("voicechat.log", "Participant Remove failed - No channel: handle=%s", spec->session_handle);
						break;
					}
						
					if(id<0)
					{
						filelog_printf("voicechat.log", "Participant Add failed - no user: Acct=%s Uri=%s", spec->account_name, spec->participant_uri);
						break;
					}

					svChannelRemoveUser(chan, id);
				}
				xcase evt_participant_updated: {
					vx_evt_participant_updated_t* spec = (vx_evt_participant_updated_t*)evt;
					VoiceChannel *chan = svChannelFind(NULL, NULL, spec->session_handle);
					VoiceChannelUser *vcu = NULL;

					if(!chan)
						break;
					
					vcu = svChannelFindUser(chan, -1, NULL, spec->participant_uri, 0);

					if(!vcu)
						break;

					if(g_audio_state.ent_talking_func && g_audio_state.account_to_entref_func)
					{
						if(!vcu->isTalking && spec->is_speaking)
							g_audio_state.ent_talking_func(g_audio_state.account_to_entref_func(vcu->user->id), true);
						else if(vcu->isTalking && !spec->is_speaking)
							g_audio_state.ent_talking_func(g_audio_state.account_to_entref_func(vcu->user->id), false);
					}

					vcu->user->volume = spec->volume;
					vcu->user->isMutedByMe = spec->is_muted_for_me;
					vcu->isMutedByMe = spec->is_muted_for_me;
					vcu->volume = spec->volume;
					vcu->isMutedByOp = spec->is_moderator_muted;
					vcu->isTalking = spec->is_speaking;
					vcu->energy = (float)spec->energy;
					
					if(vcu->user->id==g_VoiceState.localAcct)
					{
						if(!chan->has_spoken && vcu->isTalking)
						{
							chan->has_spoken = true;
							if(g_VoiceState.speakCB)
								g_VoiceState.speakCB(chan->internName);
						}
					}
					else
					{
						if(!chan->has_listened && vcu->isTalking)
						{
							chan->has_listened = true;
							if(g_VoiceState.listenCB)
								g_VoiceState.listenCB(chan->internName);
						}
					}

					svUserPrefsSave(vcu->user);
				}
				xdefault: {
					filelog_printf("voicechat.log", "Vivox: unhandled evt %d\n", evt->type);
				}
			}

			destroy_evt(evt); 
		} 
	}
}

void svAdsTick(void)
{
	switch(g_VoiceState.ad_state)
	{
		xcase VAS_NONE: {

		}
		xcase VAS_CHANNELS_LEAVE: {
			int finished = true;

			FOR_EACH_IN_EARRAY(g_VoiceState.groups, VoiceChannelGroup, group)
			{
				FOR_EACH_IN_EARRAY(group->channels, VoiceChannel, chan)
				{
					if(chan->connectState!=RS_NONE)
						finished = false;
					if(chan->disconnectState!=RS_SUCCESS)
						finished = false;
				}
				FOR_EACH_END
			}
			FOR_EACH_END

			if(finished)
			{
				const char *chan_name = g_VoiceState.ad_chan_name;
				g_VoiceState.ad_state = VAS_AD_CHAN_JOIN;

				if(!chan_name)
					chan_name = STACK_SPRINTF("sip:confctl-p-%s@%s", g_VoiceState.username, g_VoiceState.acct_domain);
				g_VoiceState.ad_chan = svChannelJoinByName(NULL, chan_name, chan_name, false);
				g_VoiceState.ad_chan->adchannel = true;

				//g_VoiceState.first_channel = true;
			}
		}
		xcase VAS_AD_CHAN_JOIN: {
			if(g_VoiceState.ad_chan && g_VoiceState.ad_chan->connectState==RS_SUCCESS)
			{
				FOR_EACH_IN_EARRAY(g_VoiceState.groups, VoiceChannelGroup, group)
				{
					FOR_EACH_IN_EARRAY(group->channels, VoiceChannel, chan)
					{
						if(g_VoiceState.ad_chan!=chan)
						{
							svChannelJoin(group, chan, g_VoiceState.pre_ad_active == chan);
							g_VoiceState.pre_ad_active = NULL;
						}
					}
					FOR_EACH_END
				}
				FOR_EACH_END
					g_VoiceState.ad_state = VAS_REJOIN_ALL;
			}
		}
		xcase VAS_REJOIN_ALL: {
			g_VoiceState.ad_state = VAS_NONE;
		}
	}

	if(g_VoiceState.ad_chan && 
		g_VoiceState.time_ad_join)
	{
		if(ABS_TIME_SINCE(g_VoiceState.time_ad_join) > SEC_TO_ABS_TIME(5) &&
			!g_VoiceState.ad_started)
		{
			g_VoiceState.time_ad_join = 0;
			svChannelLeave(g_VoiceState.ad_chan, true);
		}
		else if(ABS_TIME_SINCE(g_VoiceState.time_ad_join) > SEC_TO_ABS_TIME(60) && 
			g_VoiceState.ad_started)
		{
			g_VoiceState.time_ad_join = 0;
			svChannelLeave(g_VoiceState.ad_chan, true);
		}
	}
}

void sndUpdateVoice(Vec3 pos, F32 elapsed)
{
	static F32 last_eff_speaker = -1;
	static F32 last_eff_mic = -1;
	F32 eff_speaker = 0;
	F32 eff_mic = 0;
	PerfInfoGuard* guard;

	if(!gConf.bVoiceChat || g_isContinuousBuilder)
		return;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	if(g_VoiceState.options.MuteInactive && gfxIsInactiveApp())
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.active_volume, SFT_FLOAT, -SND_STANDARD_FADE);
	else
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.active_volume, SFT_FLOAT, SND_STANDARD_FADE);

	if(g_VoiceState.signed_in)
	{
		eff_speaker = (g_VoiceState.options.SpeakerVolume/100.0)*g_VoiceState.active_volume;
		if(eff_speaker!=last_eff_speaker)
		{
			vx_req_aux_set_speaker_level_t *req = NULL;
			last_eff_speaker = eff_speaker;

			vx_req_aux_set_speaker_level_create(&req);
			if(req)
			{
				req->level = eff_speaker * 100;

				vx_issue_request((vx_req_base_t*)req);
			}
		}

		eff_mic = 0.75f * g_VoiceState.options.MicLevel/100.0f;

		eff_mic = eff_mic*g_VoiceState.active_volume*g_VoiceState.micVolume;

		if(eff_mic!=last_eff_mic)
		{
			last_eff_mic = eff_mic;

			svCaptureDeviceSetLevel(NULL, eff_mic * 100.f, true);
		}
	}

	if(!g_VoiceState.once_init)
	{
		svInitPrefs();

		g_VoiceState.once_init = true;
	}

	if (g_VoiceState.enabled_region &&
		g_VoiceState.connectorState != RS_SUCCESS)
	{
		svConnect();
	}

	if (!g_VoiceState.enabled_region)
	{
		svSignOut("Non-enabled Voice Region");
	}
	else if(g_VoiceState.options.NoVoice)
	{
		svSignOut("No Voice");
	}
	else if(g_VoiceState.connectorState == RS_SUCCESS)
	{
		if (g_audio_state.player_exists_func && 
			g_audio_state.player_exists_func())
		{
			const char* un = NULL;  const char* pw = NULL; int id = 0;

			if(g_audio_state.player_voice_func)
			{
				g_audio_state.player_voice_func(&un, &pw, &id);

				if(!un || !pw)
				{
				}
				else if(stricmp(g_VoiceState.username, un) || stricmp(g_VoiceState.password, pw) || g_VoiceState.acctid!=id)
				{
					svSignOut("Credentials changed");
					g_VoiceState.localAcct = svAccountToContainerId(un);
					g_VoiceState.username = strdup(un);
					g_VoiceState.password = strdup(pw);
					g_VoiceState.acctid = id;
				}

				if(!g_VoiceState.signed_in && !g_VoiceState.signing_in && !g_VoiceState.signing_out)
					svSignIn();
			}
			else if(g_VoiceState.signed_in)
			{
				svSignOut("No credential function");
			}
		}
		else if(g_audio_state.is_login_func &&
				g_audio_state.is_login_func())
		{
			// checking the login func first since the player also won't exist while
			// switching between game servers & probably a few other cases.. we only
			// want to log players out of Vivox when they are at the login screen
			svSignOut("Player is at the login screen");
		}
	}

	if(g_VoiceState.signed_in && g_VoiceState.needsFonts)
	{
		g_VoiceState.needsFonts = 0;
		svFontsGet(true);
		svFontsGet(false);
	}

	svProcessMessages();

	if(g_VoiceState.record_play_on_stop && !g_VoiceState.record_active)
	{
		g_VoiceState.record_play_on_stop = 0;
		svRecordPlay(g_VoiceState.record_queue_font);
	}

	svAdsTick();

	if(g_VoiceState.connectorState==RS_SUCCESS && g_VoiceState.signed_in)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(g_VoiceState.groups, VoiceChannelGroup, group)
		{
			if(g_VoiceState.first_channel && g_VoiceState.first_channel_wait)
				break;

			if(group->createState==RS_NONE)
				svChannelGroupRegister(group);

			if(group->createState==RS_SUCCESS)
			{
				FOR_EACH_IN_EARRAY(group->channels, VoiceChannel, chan)
				{
					if(chan->timeToWait > ABS_TIME)
						continue;

					chan->timeToWait = 0;

					if(chan->connectState==RS_NEED_REQUEST && chan->disconnectState==RS_NONE)
					{
						svChannelConnect(g_VoiceState.defGroup, chan);

						if(g_VoiceState.first_channel)
						{
							g_VoiceState.first_channel_wait = true;
							break;
						}
					}
					if(chan->disconnectState==RS_NEED_REQUEST)
						svChannelDisconnect(g_VoiceState.defGroup, chan);

					if(chan->connectState==RS_NEED_REQUEST || chan->connectState==RS_REQUESTING ||
						chan->disconnectState==RS_NEED_REQUEST || chan->disconnectState==RS_REQUESTING)
					{
						// One at a time?
						break;
					}
				}
				FOR_EACH_END
			}
		}
		FOR_EACH_END

		FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chan)
		{
			if(!chan->group && g_VoiceState.defGroup && g_VoiceState.defGroup->createState==RS_SUCCESS)
				svChannelJoinByName(g_VoiceState.defGroup, chan->internName, NULL, true);
		}
		FOR_EACH_END
	}

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

void svChannelGetParticipantListByName(const char* internName, const char* externName, ContainerID **ids, bool speakingOnly)
{
	VoiceChannel *chan = svChannelFind(internName, externName, NULL);

	if(!chan)
		return;

	FOR_EACH_IN_EARRAY_FORWARDS(chan->allUsers, VoiceChannelUser, vcu)
	{
		if(vcu->user->id==g_VoiceState.localAcct)
			continue;
		if(!speakingOnly || vcu->isTalking)
			ea32Push(ids, vcu->user->id);
	}
	FOR_EACH_END
}

void svUserDestroy(VoiceUser *user)
{
	svDebugUIUserLost(user);
	filelog_printf("voicechat.log", "User Destroyed: Uri=%s Id=%d", user->externURI, user->id);

	eaFindAndRemoveFast(&g_VoiceState.users, user);
	StructDestroy(parse_VoiceUser, user);
}

VoiceUser* svCreateUser(const char* externName, const char* externURI, ContainerID accountID)
{
	VoiceUser *user = NULL;

	user = StructAlloc(parse_VoiceUser);
	user->id = accountID;
	user->externName = StructAllocString(externName);
	user->externURI = StructAllocString(externURI);

	filelog_printf("voicechat.log", "User Created: Uri=%s Id=%d", user->externURI, user->id);

	eaPush(&g_VoiceState.users, user);
	svUserPrefsLoad(user);

	return user;
}

VoiceUser* svFindUser(ContainerID accountID)
{
	FOR_EACH_IN_EARRAY(g_VoiceState.users, VoiceUser, userSearch)
	{
		if(userSearch->id==accountID)
			return userSearch;
	}
	FOR_EACH_END

	return NULL;
}

U32 svChannelUserReady(VoiceChannel *chan, VoiceUser *user)
{
	return chan && user && chan->handle && user->externURI;
}

VoiceChannelUser* svChannelFindUserByName(const char* chanName, const char* userName, ContainerID accountID)
{
	VoiceChannel *chan = svChannelFind(chanName, NULL, NULL);

	if(!chan)
		return NULL;

	return svChannelFindUser(chan, accountID, userName, NULL, false);
}

void svChannelUserSetMute(VoiceChannel *chan, VoiceUser *user, S32 mute)
{
	vx_req_session_set_participant_mute_for_me_t *req = NULL;

	if(user->id==g_VoiceState.localAcct)
		return;

	if(!svChannelUserReady(chan, user))
		return;

	vx_req_session_set_participant_mute_for_me_create(&req);
	if(req)
	{
		req->mute = !!mute;
		req->participant_uri = vx_strdup(user->externURI);
		req->session_handle = vx_strdup(chan->handle);

		vx_issue_request((vx_req_base_t*)req);
	}
}

void svUserSetMute(VoiceUser *user, S32 mute)
{
	if(!user)
		return;

	if(user->id==g_VoiceState.localAcct)
		return;

	FOR_EACH_IN_EARRAY(user->channels, VoiceChannelUser, vcu)
	{
		svChannelUserSetMute(vcu->chan, user, mute);
	}
	FOR_EACH_END
	
	// Cache this until the update catches it
	user->isMutedByMe = mute;
}

void svUserSetMuteByID(ContainerID accountID, S32 mute)
{
	VoiceUser *user = NULL;

	user = svFindUser(accountID);

	if(user)
		svUserSetMute(user, mute);
}

void svUserSetSelfMute(ContainerID accountID, S32 mute)
{
	vx_req_connector_mute_local_mic_t *req = NULL;
	VoiceUser *user = svFindUser(accountID);

	vx_req_connector_mute_local_mic_create(&req);
	if(req)
	{
		req->mute_level = !!mute;
		req->connector_handle = vx_strdup(g_VoiceState.account_handle);
		
		vx_issue_request((vx_req_base_t*)req);

		if(user)
			user->isMutedByMe = true;
	}
}

void svChannelSetUserVolume(VoiceChannel *chan, VoiceUser *user, int vol)
{
	vx_req_session_set_participant_volume_for_me_t *req = NULL;

	if(user->id==g_VoiceState.localAcct)
		return;
	
	if(!svChannelUserReady(chan, user))
		return;

	vx_req_session_set_participant_volume_for_me_create(&req);
	if(req)
	{
		req->volume = (int)vol;
		req->participant_uri = vx_strdup(user->externURI);
		req->session_handle = vx_strdup(chan->handle);

		vx_issue_request((vx_req_base_t*)req);
	}
}

void svUserSetVolume(VoiceUser *user, int vol)
{
	if(user->id==g_VoiceState.localAcct)
		return;

	FOR_EACH_IN_EARRAY(user->channels, VoiceChannelUser, vcu)
	{
		svChannelSetUserVolume(vcu->chan, user, vol);
	}
	FOR_EACH_END

	// Cache this until the update catches it
	user->volume = vol;
	user->targetVolume = vol;
}

void svUserSetVolumeByName(const char* internName, const char* externName, ContainerID accountID, int vol)
{
	VoiceUser *user = svFindUser(accountID);

	if(user)
		svUserSetVolume(user, vol);
}

int svRecordInit(void)
{
	int disconnected = true;

	FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chan)
	{
		if(chan->disconnectState!=RS_SUCCESS)
			disconnected = false;

		if(chan->disconnectState==RS_NONE)
			svChannelLeave(chan, false);
	}
	FOR_EACH_END

	g_VoiceState.record_playback_stopped = 0;

	sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.micVolume, SFT_FLOAT, SOUND_VOICE_MIC_FADE_RATE);

	return disconnected;
}

int svRecordClose(void)
{
	int reconnected = true;
	vx_req_aux_render_audio_stop_t *req = NULL;

	if(!g_VoiceState.record_playback_stopped)
	{
		g_VoiceState.record_playback_stopped = 1;

		vx_req_aux_render_audio_stop_create(&req);
		if(req)
			vx_issue_request((vx_req_base_t*)req);
	}

	FOR_EACH_IN_EARRAY(g_VoiceState.channels, VoiceChannel, chan)
	{
		if(chan->connectState!=RS_SUCCESS)
			reconnected = false;

		if(chan->connectState==RS_NONE)
			svChannelJoin(NULL, chan, true);
	}
	FOR_EACH_END

	if (g_VoiceState.options.OpenMic || g_VoiceState.ptt_on) {
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.micVolume, SFT_FLOAT, SOUND_VOICE_MIC_FADE_RATE);
	} else {
		sndFadeManagerAdd(g_audio_state.fadeManager, &g_VoiceState.micVolume, SFT_FLOAT, -SOUND_VOICE_MIC_FADE_RATE);
	}

	g_VoiceState.record_play_on_stop = false;

	return reconnected;
}

void svRecordStart(void)
{
	vx_req_aux_start_buffer_capture_t *req = NULL;

	if(g_VoiceState.connectorState != RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;

	if(g_VoiceState.active_channel) 
		svClearTransmit();

	vx_req_aux_start_buffer_capture_create(&req);
	if(req)
	{
		vx_issue_request((vx_req_base_t*)req);

		g_VoiceState.record_active = true;
	}
}

void svRecordStop(void)
{
	vx_req_aux_capture_audio_stop_t *req = NULL;

	if(g_VoiceState.connectorState != RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;

	vx_req_aux_capture_audio_stop_create(&req);
	if(req)
	{
		vx_issue_request((vx_req_base_t*)req);
	}
}

void svRecordQueuePlay(int fontID)
{
	if(g_VoiceState.record_active)
		svRecordStop();

	g_VoiceState.record_play_on_stop = true;
	g_VoiceState.record_queue_font = fontID;
}

void svRecordPlay(int fontID)
{
	vx_req_aux_play_audio_buffer_t *req = NULL;

	if(g_VoiceState.connectorState != RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;
	if(g_VoiceState.record_active) return;

	vx_req_aux_play_audio_buffer_create(&req);
	if(req)
	{
		req->account_handle = vx_strdup(g_VoiceState.account_handle);

		req->template_font_id = fontID;

		vx_issue_request((vx_req_base_t*)req);

		g_VoiceState.record_playback_complete = 0;
	}
}

VoiceFont* svFontGet(int templates, int id, int create)
{
	VoiceFont *newFont;
	VoiceFont ***fonts = templates ? &g_VoiceState.template_fonts : &g_VoiceState.session_fonts;
	FOR_EACH_IN_EARRAY(*fonts, VoiceFont, font)
	{
		if(font->id==id)
			return font;
	}
	FOR_EACH_END

	if(!create)
		return NULL;

	newFont = StructCreate(parse_VoiceFont);
	newFont->id = id;

	eaPush(fonts, newFont);

	return newFont;
}

void svFontsGetFinish(int templates, vx_voice_font_t **fonts, int count)
{
	int i;

	for(i=0; i<count; i++)
	{
		vx_voice_font_t* vxfont = fonts[i];
		VoiceFont *font = svFontGet(templates, vxfont->id, true);

		StructFreeStringSafe(&font->name);
		font->name = StructAllocString(vxfont->name);

		StructFreeStringSafe(&font->desc);
		font->desc = StructAllocString(vxfont->description);
	}
}

void svFontsGet(int templates)
{

	if(g_VoiceState.connectorState!=RS_SUCCESS) return;
	if(!g_VoiceState.signed_in) return;

	if(templates)
	{
		vx_req_account_get_template_fonts_t *req = NULL;

		vx_req_account_get_template_fonts_create(&req);
		if(req)
		{
			req->account_handle = vx_strdup(g_VoiceState.account_handle);

			vx_issue_request((vx_req_base_t*)req);
		}
	}
	else
	{
		vx_req_account_get_session_fonts_t *req = NULL;

		vx_req_account_get_session_fonts_create(&req);
		if(req)
		{
			req->account_handle = vx_strdup(g_VoiceState.account_handle);

			vx_issue_request((vx_req_base_t*)req);
		}
	}
}

void svFontsPrint(void)
{
	FOR_EACH_IN_EARRAY(g_VoiceState.template_fonts, VoiceFont, font)
	{
		conPrintfUpdate("%3d: %s", font->id, font->name);
	}
	FOR_EACH_END
}

void svVivoxLog(const char* source, const char* level, const char* msg)
{
	filelog_printf("vivox.log", "%s - %s: %s", level, source, msg);
}

void svSetLogLevel(int level)
{
	vx_unregister_logging_handler(NULL, NULL);

	if(level>=log_error && level<=log_trace)
		vx_register_logging_initialization(log_to_callback, NULL, NULL, NULL, level, svVivoxLog);
}

void svOptionsSave(void)
{
	GamePrefStoreStruct("VoiceOptions", parse_VoiceOptions, &g_VoiceState.options);
}

void svTutorialsSave(void)
{
	GamePrefStoreStruct("VoiceTutorials", parse_VoiceTutorials, &g_VoiceState.tutorials);
}

// does nothing but reset the g_VoiceState.options to the defaults
void svOptionsResetToDefaults(void)
{
	StructInit(parse_VoiceOptions, &g_VoiceState.options);

	// only set the defaults if they are specified in the config, otherwise we'll use VoiceOptions AUTO_STRUCT defaults
	if(svConfigIsFieldSpecified("pushToTalkDefault"))
		g_VoiceState.options.OpenMic = !g_VoiceState.config.pushToTalkDefault;
	if(svConfigIsFieldSpecified("defaultSpeakerVolume"))
		g_VoiceState.options.SpeakerVolume = g_VoiceState.config.defaultSpeakerVolume;
	if(svConfigIsFieldSpecified("defaultMicLevel"))
		g_VoiceState.options.MicLevel = g_VoiceState.config.defaultMicLevel;
	if(svConfigIsFieldSpecified("defaultNoVoice"))
		g_VoiceState.options.NoVoice = g_VoiceState.config.defaultNoVoice;
}

void svTutorialsResetToDefaults(void)
{
	StructInit(parse_VoiceTutorials, &g_VoiceState.tutorials);

	if(svConfigIsFieldSpecified("showFirstConnectTutorial"))
		g_VoiceState.tutorials.showFirstConnectTutorial = g_VoiceState.config.showFirstConnectTutorial;
}

void svLoadDefaults(void)
{
	static int inited = false;

	if(inited)
		return;
	inited = true;

	ParserLoadFiles(NULL, "client/VoiceConfig.txt", "VoiceConfig.bin", PARSER_CLIENTSIDE | PARSER_OPTIONALFLAG, parse_VoiceConfig, &g_VoiceState.config);	

	svOptionsResetToDefaults();
	GamePrefGetStruct("VoiceOptions", parse_VoiceOptions, &g_VoiceState.options);

	// overwriting these since we want to initially pull up the default device, not whatever the user was using the last time they played
	g_VoiceState.options.InputDev  = AUDIO_DEFAULT_DEVICE_ID;
	g_VoiceState.options.OutputDev = AUDIO_DEFAULT_DEVICE_ID;

	svTutorialsResetToDefaults();
	GamePrefGetStruct("VoiceTutorials", parse_VoiceTutorials, &g_VoiceState.tutorials);
}

AUTO_COMMAND;
void svTutorialsReset(void)
{
	//use this for testing since it'll be impossible to reset your client otherwise
	svTutorialsResetToDefaults();
	svTutorialsSave();
}

void svOptionsSetCallbacks(VoidIntFunc volume, VoidIntFunc mic, VoidIntFunc openMic, VoidIntFunc inputCB, VoidIntFunc outputCB)
{
	g_VoiceState.micCB = mic;
	g_VoiceState.volCB = volume;
	g_VoiceState.openMicCB = openMic;
	g_VoiceState.inputCB = inputCB;
	g_VoiceState.outputCB = outputCB;
}

void svOptionsDataSetCallbacks(VoidStrArrFunc input, VoidStrArrFunc output)
{
	g_VoiceState.inputListCB = input;
	g_VoiceState.outputListCB = output;
}

void svIgnoreSetCallbacks(BoolU32Func isIgnored)
{
	g_VoiceState.isIgnoredCB = isIgnored;
}

void svSetNameCallback(CharPtrU32Func getName)
{
	g_VoiceState.nameFromID = getName;
}

void svSetLoggingFuncs(VoidStrFunc speakCB, VoidStrFunc listenCB, VoidVoidFunc adplayCB)
{
	g_VoiceState.speakCB = speakCB;
	g_VoiceState.listenCB = listenCB;
	g_VoiceState.adplayCB = adplayCB;
}

void svSetNotifyFuncs(VoidIntIntFunc joinCB, VoidVoidFunc leaveCB, VoidVoidFunc failureCB)
{
	g_VoiceState.joinCB = joinCB;
	g_VoiceState.leaveCB = leaveCB;
	g_VoiceState.failureCB = failureCB;
}

AUTO_STARTUP(Voice_Options);
void svVoiceFallback(void)
{
	if(!gConf.bVoiceChat)
		return;

	svLoadDefaults();
}

AUTO_COMMAND ACMD_NAME(VoiceServer) ACMD_CMDLINE;
void sndVoiceSetServer(char* server)
{
	if(server && server[0])
		s_ServerOverride = strdup(STACK_SPRINTF("https://" ORGANIZATION_DOMAIN "/api2"));
}

AUTO_STARTUP(Voice) ASTRT_DEPS(Voice_Options);
void sndVoiceStartup(void)
{
	char *tmp, *buf;
	if(!gConf.bVoiceChat)
		return;

	g_VoiceState.enabled_region = 0;

	if(s_ServerOverride)
		g_VoiceState.acct_server = s_ServerOverride;

	if(!g_VoiceState.acct_server && isDevelopmentMode())
		g_VoiceState.acct_server = "https://" ORGANIZATION_DOMAIN "/api2";

	if(!g_VoiceState.acct_server)
		g_VoiceState.acct_server = g_VoiceState.config.voiceServer;

	if(!g_VoiceState.acct_server)
		g_VoiceState.acct_server = "https://" ORGANIZATION_DOMAIN "/api2";

	buf = strdup(g_VoiceState.acct_server);
	tmp = strrchr(buf, '/');
	if(tmp) *tmp = '\0';
	g_VoiceState.acct_domain = strdup(strchr(buf, '.')+1);
	free(buf);

	svChannelGroupCreateDefaults();

	vx_register_logging_initialization(log_to_callback, NULL, NULL, NULL, log_info, svVivoxLog);
}

#include "sndVoice_h_ast.c"
#include "sndVoice_c_ast.c"