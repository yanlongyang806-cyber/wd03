#include "Prefs.h"
#include "gclOptions.h"
#include "soundLib.h"
#include "earray.h"
#include "message.h"
#include "gclMediaControl.h"

#include "cpu_count.h"

// 
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

#define AUDIO_CATEGORY "Audio"

static int g_player = 0;

void sndAudioSettingsChanged(OptionSetting *option)
{

}

void audioDriverChangedCallback(OptionSetting *option)
{
	sndChangeDriver(option->iIntValue);
}

void audioDriverUpdateCallback(OptionSetting *option)
{
	static char **outputDriverOptions = NULL;

	eaClear(&outputDriverOptions);
	sndGetDriverNames(&outputDriverOptions);

	OptionSettingUpdateComboBoxOptions(option, AUDIO_CATEGORY, &outputDriverOptions, false);
}

void audioDriverResetToDefault(OptionSetting *option)
{
	option->iIntValue = 0;
	sndChangeDriver(option->iIntValue);
}

void sndAudioSettingsCommit(OptionSetting *option)
{
	char **players = gclMediaControlPlayers(NULL);

	GamePrefStoreFloat("SoundOption_Main", g_audio_state.options.main_volume);
	GamePrefStoreFloat("SoundOption_Fx", g_audio_state.options.fx_volume);
	GamePrefStoreFloat("SoundOption_Music", g_audio_state.options.music_volume);
	GamePrefStoreFloat("SoundOption_Amb", g_audio_state.options.amb_volume);
	GamePrefStoreFloat("SoundOption_UI", g_audio_state.options.ui_volume);
	GamePrefStoreFloat("SoundOption_Voice", g_audio_state.options.voice_volume);
	GamePrefStoreFloat("SoundOption_Notification", g_audio_state.options.notification_volume);
	GamePrefStoreFloat("SoundOption_Video", g_audio_state.options.video_volume);
	GamePrefStoreInt("SoundOption_DSP", g_audio_state.dsp_enabled ? 1 : 0);
	GamePrefStoreInt("SoundOption_OutputDevice", g_audio_state.curDriver);
	GamePrefStoreInt("SoundOption_MuteVoice", g_audio_state.bMuteVOonContactEnd);

	if(players)
	{
		assert(g_player >= 0 && g_player < eaSize(&players));
		gclMediaControlSetPlayer(players[g_player]);
	}
}

void sndAudioSettingsUpdate(OptionSetting *option)
{
	
}

void sndAudioSettingsResetToDefaults(OptionSetting *option)
{
	sndSetupDefaultOptionsVolumes();
	gclMediaControlSetPlayer("");
}

static void audioDSPEnabledChangedCallback(OptionSetting *setting)
{
	g_audio_state.dsp_enabled = setting->iIntValue == 1 ? true : false;
}

static void audioDSPEnabledUpdateCallback(OptionSetting *setting)
{
	setting->iIntValue = g_audio_state.dsp_enabled ? 1 : 0;
}

static void sndUpdateDSPEnabled()
{
	if(getNumRealCpus() == 1)
	{
		g_audio_state.dsp_enabled = 0;
	}
	else
	{
		g_audio_state.dsp_enabled = 1;
	}
}

static void audioDSPResetToDefault(OptionSetting *setting)
{
	sndUpdateDSPEnabled();	
	setting->iIntValue = g_audio_state.dsp_enabled ? 1 : 0;
}

static void gclSoundOptions_Init(void)
{
	static char **outputDriverOptions = NULL;
	char **players = gclMediaControlPlayers(&g_player);

	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "Overall", 0, 1, &g_audio_state.options.main_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);
	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "FX", 0, 1, &g_audio_state.options.fx_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);

	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "Music", 0, 1, &g_audio_state.options.music_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);

	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "Ambient", 0, 1, &g_audio_state.options.amb_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);

	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "UI", 0, 1, &g_audio_state.options.ui_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);

	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "Voice", 0, 1, &g_audio_state.options.voice_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);

	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "Notifications", 0, 1, &g_audio_state.options.notification_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);
	
	autoSettingsAddFloatSlider(AUDIO_CATEGORY, "Video", 0, 1, &g_audio_state.options.video_volume, 
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, displayFloatAsPercentage, inputFloatPercentage, 1);

	// probably won't have this list ready at this point
	eaClear(&outputDriverOptions);
	sndGetDriverNames(&outputDriverOptions);

	autoSettingsAddComboBox(AUDIO_CATEGORY, "OutputDevice", &outputDriverOptions, false, (int*) &g_audio_state.curDriver,
							audioDriverChangedCallback, 
							sndAudioSettingsCommit, 
							audioDriverUpdateCallback, 
							audioDriverResetToDefault, NULL, true);

	
	autoSettingsAddBit(AUDIO_CATEGORY, "DSP", (int *)&g_audio_state.dsp_enabled, 1, audioDSPEnabledChangedCallback, sndAudioSettingsCommit, audioDSPEnabledUpdateCallback, audioDSPResetToDefault, NULL, true );

#if !PLATFORM_CONSOLE
	autoSettingsAddComboBox(AUDIO_CATEGORY, "Media Player", &players, true, &g_player,
							sndAudioSettingsChanged, 
							sndAudioSettingsCommit, 
							sndAudioSettingsUpdate, 
							sndAudioSettingsResetToDefaults, NULL, true);
#endif

	autoSettingsAddBitCheckbox(AUDIO_CATEGORY, "EndVoice", (int*) &g_audio_state.bMuteVOonContactEnd, 1,
								sndAudioSettingsChanged, 
								sndAudioSettingsCommit, 
								sndAudioSettingsUpdate, 
								sndAudioSettingsResetToDefaults, NULL, true);
}

// option messages in ui/gens/options.uigen.ms
AUTO_STARTUP(Sound_Options) ASTRT_DEPS(BasicOptions);
void sndSetupAudioOptions(void)
{
	sndLoadDefaults();
	sndSetupDefaultOptionsVolumes();
	sndUpdateDSPEnabled();
	g_audio_state.options.main_volume = GamePrefGetFloat("SoundOption_Main", g_audio_state.options.main_volume);
	g_audio_state.options.fx_volume = GamePrefGetFloat("SoundOption_Fx", g_audio_state.options.fx_volume);
	g_audio_state.options.music_volume = GamePrefGetFloat("SoundOption_Music", g_audio_state.options.music_volume);
	g_audio_state.options.amb_volume = GamePrefGetFloat("SoundOption_Amb", g_audio_state.options.amb_volume);
	g_audio_state.options.ui_volume = GamePrefGetFloat("SoundOption_UI", g_audio_state.options.ui_volume);
	g_audio_state.options.notification_volume = GamePrefGetFloat("SoundOption_Notification", g_audio_state.options.notification_volume);
	g_audio_state.options.video_volume = GamePrefGetFloat("SoundOption_Video", g_audio_state.options.video_volume);
	g_audio_state.options.voice_volume = GamePrefGetFloat("SoundOption_Voice", g_audio_state.options.voice_volume);
	g_audio_state.dsp_enabled = GamePrefGetInt("SoundOption_DSP", g_audio_state.dsp_enabled);
	g_audio_state.curDriver = GamePrefGetInt("SoundOption_OutputDevice", g_audio_state.curDriver);
	g_audio_state.bMuteVOonContactEnd = GamePrefGetInt("SoundOption_MuteVoice", g_audio_state.bMuteVOonContactEnd);
	gclSoundOptions_Init();
}