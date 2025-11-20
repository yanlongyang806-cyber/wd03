#include "sndLibPrivate.h"
#include "event_sys.H"
#include "earray.h"

// CmdContext
#include "cmdparse.h"

// Conprintfupdate
#include "GfxConsole.h"

// From UI2Lib, for sound info printing
#include "CBox.h"

#include "sndSpace.h"
#include "fmod_event.h"

#include "sndMusic.h"


#ifndef STUB_SOUNDLIB

#include "sndMixer.h"
extern SoundMixer *gSndMixer;

#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

int snd_enable_debug_mem = 1;
AUTO_CMD_INT(snd_enable_debug_mem, sndEnableDebugMem) ACMD_CMDLINE;

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void sndDebugLevel(int level)
{
#ifndef STUB_SOUNDLIB
	if(g_audio_state.noaudio)
	{
		return;
	}

	g_audio_state.debug_level = level;
#endif
}

// Disable all playing of sound
AUTO_COMMAND ACMD_NAME(sndDisable) ACMD_ACCESSLEVEL(0);
void sndCmdDisableSound(void)
{
#ifndef STUB_SOUNDLIB
	sndDisableSound();
#endif
}

// Enable playing of sound
AUTO_COMMAND ACMD_NAME(sndEnable) ACMD_ACCESSLEVEL(0);
void sndCmdEnableSound(void)
{
#ifndef STUB_SOUNDLIB
	sndEnableSound();
#endif
}

// Language control
AUTO_COMMAND;
void sndCmdSetLanguage(char *language)
{
#ifndef STUB_SOUNDLIB
	sndSetLanguage(language);
#endif
}
AUTO_COMMAND;
const char *sndCmdGetLanguage(void)
{
#ifndef STUB_SOUNDLIB
	return sndGetLanguage();
#else
	return allocAddString("SoundLib Stubbed");
#endif
}

AUTO_COMMAND;
void sndStopOneShotTest(char *event_name)
{
#ifndef STUB_SOUNDLIB
	sndStopOneShot(event_name);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void sndPlayRemote3dRel(char *event_name, float x, float y, float z, char *filename, U32 entRef)
{
#ifndef STUB_SOUNDLIB
	Vec3 pos = {x, y, z};
	Vec3 player;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	sndGetPlayerPosition(player);
	addVec3(pos, player, pos);

	sndPlayRemote3dV2(event_name, vecParamsXYZ(pos), filename, entRef);
#endif
}

AUTO_COMMAND;
void sndPlayRemote3dRelTest(char* event_name, float x, float y, float z)
{
#ifndef STUB_SOUNDLIB
	Vec3 pos = {x, y, z};

	sndPlayRemote3dRel(event_name, x, y, z, NULL, -1);
#endif
}

AUTO_COMMAND;
void sndPlayMusicTest(char *event_name)
{
#ifndef STUB_SOUNDLIB
	sndMusicPlayRemote(event_name, NULL, -1);
#endif
}

AUTO_COMMAND;
void sndClearMusicTest(void)
{
#ifndef STUB_SOUNDLIB
	sndMusicClear(false);
#endif
}

AUTO_COMMAND;
void sndReplaceMusicTest(char *eventname)
{
#ifndef STUB_SOUNDLIB
	sndMusicReplace(eventname, NULL, -1);
#endif
}

AUTO_COMMAND;
void sndEndMusicTest(void)
{
#ifndef STUB_SOUNDLIB
	sndMusicEnd(false);
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;
void audiooutput(char *path)
{
#ifndef STUB_SOUNDLIB
	if(path)
	{
		g_audio_state.outputFilePath = strdup(path);
	}
#endif
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;
void noaudio(int noaudio)
{
#ifndef STUB_SOUNDLIB
	if(!g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded() && !g_audio_state.inited)
	{
		g_audio_state.noaudio = 1;
	}
	if(g_audio_state.inited)
	{
		Alertf("Unable to disable audio system post initialization. (Use on command line.)");
	}
#endif
}

AUTO_COMMAND;
void sndDebugPrint()
{	
#ifndef STUB_SOUNDLIB
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	//FMOD_ErrCheck(FMOD_EventSystem_PrintDebug(EVENT_DBGPRT_ALL, conPrintfUpdate));
#endif
}

AUTO_COMMAND;
void sndEventMemUsage(void)
{
#ifndef STUB_SOUNDLIB
	int current, max;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	FMOD_EventSystem_GetMemStats(&current, &max);


	conPrintfUpdate("SoundRAM Usage - Current: %d Bytes - Max: %d Bytes", current, max);
#endif
}

AUTO_COMMAND;
void sndDisplay3D()
{
#ifndef STUB_SOUNDLIB
	Vec3 pos, vel, forward, up;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	FMOD_ErrCheck(FMOD_EventSystem_get3DListenerAttributes(pos, vel, forward, up));

	conPrintfUpdate("Pos: %.2f %.2f %.2f - Vel: %.2f %.2f %.2f - Forward: %.2f %.2f %.2f - Up: %.2f %.2f %.2f",
		pos[0], pos[1], pos[2], vel[0], vel[1], vel[2], forward[0], forward[1], forward[2], up[0], up[1], up[2]);
#endif
}

AUTO_COMMAND;
void sndPlayRemoteTest(char *event_name)
{
#ifndef STUB_SOUNDLIB
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	sndPlayRemoteV2(event_name, NULL, -1);
#endif
}

AUTO_COMMAND;
void sndPlayRemoteTestTimes(char *event_name, int count)
{
#ifndef STUB_SOUNDLIB
	int i;
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	for(i=0; i<count; i++)
	{
		sndPlayRemoteV2(event_name, NULL, -1);
	}
#endif
}

AUTO_COMMAND;
void sndSetVolumeByCategory(char *category, F32 vol)
{
#ifndef STUB_SOUNDLIB
	FMOD_EventSystem_SetVolumeByCategory(category, vol);
#endif
}

AUTO_COMMAND;
void sndPrintEvents(char *group)
{
#ifndef STUB_SOUNDLIB
	sndPrintPlaying();
#endif
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(sndPrintEvents);
void sndPrintAllEvents(CmdContext *cmd)
{	
#ifndef STUB_SOUNDLIB
	sndPrintEvents(NULL);
#endif
}

AUTO_COMMAND ACMD_NAME(snddev, sounddeveloper) ACMD_CMDLINE;
void sndSetDeveloperFlag(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.sound_developer = !!d;
	g_audio_state.audition = !!d;
#endif
}

AUTO_COMMAND ACMD_NAME(sndLoadtimeErrorCheck) ACMD_CMDLINE;
void sndSetLoadtimeErrorCheck(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.loadtimeErrorCheck = !!d;
#endif
}

AUTO_COMMAND;
void sndValidateHeap(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.validateHeapMode = !!d;
#endif
}

AUTO_COMMAND;
void sndSetOptions(int pos, int orient, int space)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.d_playerPos = pos;
	g_audio_state.d_playerOrient = orient;
	g_audio_state.d_playerSpace = space;
#endif
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(sndSetOptions);
void sndSetOptionsError(void)
{
#ifndef STUB_SOUNDLIB
	conPrintfUpdate("sndSetOptions <UsePlayerPosition> <UsePlayerOrientation> <UsePlayerForSpaces>");
	conPrintfUpdate("If not using Player data, sndLib will use Camera data.  Default is 0 0 0");
#endif
}

/*struct {
SoundSource *srcA;
SoundSource *srcC;
SoundSource *srcAC;
int eventswap;
int paramswap;
int paramval;
} testSources;

FMOD_RESULT F_CALLBACK sndMarkerEventSwap(void *e, int type, void *p1, void *p2, void *ud)
{
SoundSource *src = NULL;

fmodEventGetUserData(e, &src);
assert(src);

if(testSources.eventswap)
{
testSources.eventswap = 0;
//FMOD_EventSystem_StopEvent(e, 1);
if(src==testSources.srcA)
{
FMOD_EventSystem_AddCallback(testSources.srcC->fmod_event, 0, 0, fmodGetMkrCBType(), sndMarkerEventSwap, NULL);
FMOD_EventSystem_SetVolume(testSources.srcC->fmod_event, 1);
FMOD_EventSystem_SetVolume(testSources.srcA->fmod_event, 0);
}
else
{
FMOD_EventSystem_AddCallback(testSources.srcA->fmod_event, 0, 0, fmodGetMkrCBType(), sndMarkerEventSwap, NULL);
FMOD_EventSystem_SetVolume(testSources.srcA->fmod_event, 1);
FMOD_EventSystem_SetVolume(testSources.srcC->fmod_event, 0);
}
}

return FMOD_OK;
}

FMOD_RESULT F_CALLBACK sndMarkerParamSwap(void *e, int type, void *p1, void *p2, void *ud)
{
SoundSource *src = NULL;

fmodEventGetUserData(e, &src);
assert(src);

if(testSources.paramswap)
{
testSources.paramswap = 0;
testSources.paramval = !testSources.paramval;
fmodEventSetParam(e, "Swap", testSources.paramval);
}

return FMOD_OK;
}


AUTO_COMMAND;
void sndTestSwapEvent(void)
{
testSources.eventswap = 1;
}

AUTO_COMMAND;
void sndTestSwapParam(void)
{
testSources.paramswap = 1;
}

AUTO_COMMAND;
void sndInitEventTest(void)
{
FMOD_EventSystem_StopEvent(testSources.srcA->fmod_event, 1);
FMOD_EventSystem_StopEvent(testSources.srcC->fmod_event, 1);
FMOD_EventSystem_StopEvent(testSources.srcAC->fmod_event, 1);

FMOD_EventSystem_PlayEvent(testSources.srcA->fmod_event);
FMOD_EventSystem_SetVolume(testSources.srcC->fmod_event, 0);
FMOD_EventSystem_PlayEvent(testSources.srcC->fmod_event);
}

AUTO_COMMAND;
void sndInitParamTest(void)
{
//FMOD_EventSystem_StopEvent(testSources.srcA->fmod_event, 1);
//FMOD_EventSystem_StopEvent(testSources.srcC->fmod_event, 1);
FMOD_EventSystem_StopEvent(testSources.srcAC->fmod_event, 1);

FMOD_EventSystem_PlayEvent(testSources.srcAC->fmod_event);
}

AUTO_COMMAND;
void sndSetupMarkerTest(void)
{
// Set up events
sndSoundSourceCreate(&testSources.srcA, "MarkerTest/Master/SinA", SE_SPHERE);
sndSoundSourceCreate(&testSources.srcC, "MarkerTest/Master/SinC", SE_SPHERE);
sndSoundSourceCreate(&testSources.srcAC, "MarkerTest/Master/SinAC", SE_SPHERE);

fmodEventSystemGetEvent(testSources.srcA->full_name, &testSources.srcA->fmod_event);
fmodEventSystemGetEvent(testSources.srcC->full_name, &testSources.srcC->fmod_event);
fmodEventSystemGetEvent(testSources.srcAC->full_name, &testSources.srcAC->fmod_event);

FMOD_EventSystem_AddCallback(testSources.srcA->fmod_event, 0, 0, fmodGetMkrCBType(), sndMarkerEventSwap, NULL);
FMOD_EventSystem_AddCallback(testSources.srcC->fmod_event, 0, 0, fmodGetMkrCBType(), sndMarkerEventSwap, NULL);
FMOD_EventSystem_AddCallback(testSources.srcAC->fmod_event, 0, 0, fmodGetMkrCBType(), sndMarkerParamSwap, NULL);
}*/

AUTO_COMMAND ACMD_NAME(snd0mem);
void sndTestNoMemory(int unused)
{
#ifndef STUB_SOUNDLIB
	soundBufferSize = 1024*1024*1;
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndSetMemSize(int size)
{
#ifndef STUB_SOUNDLIB
	soundBufferSize = 1024*1024*size;
#endif
}

// Used for machines without a card but wanting to load data
AUTO_COMMAND ACMD_NAME(noSoundCard) ACMD_CMDLINE;
void sndSetNoSoundCard(int unused)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.noSoundCard = 1;
#endif
}

AUTO_COMMAND ACMD_NAME(noInactive) ACMD_CMDLINE;
void sndSetInactiveOverride(int unused)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.d_override_active = !!unused;
#endif
}

AUTO_COMMAND;
void sndRemCurDSP(void)
{
	; // this might need to be updated, it wasn't doing anything before
}

AUTO_COMMAND;
void sndTestSpaceChannelGroupVolume(F32 vol)
{
#ifndef STUB_SOUNDLIB
	fmodChannelGroupSetVolume(space_state.current_space->obj.fmod_channel_group, vol);
#endif
}

AUTO_COMMAND;
void sndForceRebuild(void)
{
#ifndef STUB_SOUNDLIB
	space_state.needs_rebuild = 1;
#endif
}

AUTO_COMMAND;
void sndForceReconnect(void)
{
#ifndef STUB_SOUNDLIB
	space_state.needs_reconnect = 1;
#endif
}

AUTO_COMMAND ACMD_NAME("ST.EventInfo");
void sndTestEventInfo(void)
{
#ifndef STUB_SOUNDLIB
	void *e = NULL;
	void *i = NULL;
	FMOD_EVENT *e_array[10] = {0};
	FMOD_EVENT_INFO info = {0};

	info.numinstances = 10;
	info.instances = (FMOD_EVENT**)e_array;

	fmodEventSystemGetEvent("TestProject/Pulse", &e, NULL);

	FMOD_EventSystem_GetEventInfoOnly("TestProject/Pulse", &i);
	fmodGetEventInfo(i, &info);

	printf("hi.\n");
#endif
}

AUTO_COMMAND;
void sndKillAll(void)
{
#ifndef STUB_SOUNDLIB
	int i;
	void **ea = NULL;

	if(g_audio_state.noaudio)
		return;

	fmodEventSystemGetPlaying(&ea);

	for(i=0; i<eaSize(&ea); i++)
	{
		FMOD_EventSystem_StopEvent(ea[i], 0);
	}
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndAudition(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.audition = !!d;
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndDSPNet(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.d_useNetListener = !!d;
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CLIENTONLY;
void sndSetMaxPlaybacks(int numPlaybacks)
{
#ifndef STUB_SOUNDLIB
	sndMixerSetMaxPlaybacks(gSndMixer, numPlaybacks);
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndDebugEvents(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.debug_type ^= SNDDBG_EVENT;
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndDebugMusic(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.debug_type ^= SNDDBG_MUSIC;
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndDebugMemory(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.debug_type ^= SNDDBG_MEMORY;
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndDebugConns(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.debug_type ^= SNDDBG_CONN;
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void sndDebugSpaces(int d)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.debug_type ^= SNDDBG_SPACE;
#endif
}

AUTO_COMMAND;
void sndSetPanLevelFadeRange(F32 range)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.directionality_range = range;
#endif
}

AUTO_COMMAND;
void sndSetHRTFPanLevelFadeRange(F32 range)
{
#ifndef STUB_SOUNDLIB
	g_audio_state.hrtf_range = range;
#endif
}

AUTO_COMMAND;
void sndReload(void)
{
#ifndef STUB_SOUNDLIB
	sndReloadAll("Sound Data");
#endif
}

AUTO_COMMAND;
void sndPlayRandomPhrase(const char *pchVoice, const char *pchPhrase)
{
#ifndef STUB_SOUNDLIB
	sndPlayRandomPhraseWithVoice(pchPhrase, pchVoice, 0);
#endif
}
