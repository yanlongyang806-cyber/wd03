/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "gclVoiceChatOptions.h"

#include "gclOptions.h"

#include "gclEntity.h"
#include "GlobalTypes.h"
#include "Player.h"
#include "sndVoice.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

#define VOICE_CATEGORY "Voice"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define OPTION_VOICE_VOLUME "VoiceVolume"
#define OPTION_MIC_LEVEL "MicLevel"
#define OPTION_MUTE_INACTIVE "MuteWhenInactive"
#define OPTION_VOICE_INPUT_DEV "InputDevice"
#define OPTION_VOICE_OUTPUT_DEV "OutputDevice"

struct {
	OptionSetting *recordLevel;
	OptionSetting *volumeLevel;
	OptionSetting *openMic;
	OptionSetting *multiMode;
	OptionSetting *outputDevice;
	OptionSetting *inputDevice;

	char** outputDevices;
	char** inputDevices;
} g_VoiceSettings;

void svSettingsChanged(OptionSetting *option)
{
	if(option==g_VoiceSettings.openMic)
	{
		svSetOpenMic(option->iIntValue);
	}
	else if(!stricmp(option->pchName, OPTION_MUTE_INACTIVE))
	{
		
	}
	else if(option==g_VoiceSettings.volumeLevel)
	{
		F32 vol = option->iIntValue;

		svSpeakersSetLevel(vol);
	}
	else if(option==g_VoiceSettings.recordLevel)
	{
		F32 vol = option->iIntValue;
		svMicrophoneSetLevel(vol);
	}
	else if(option==g_VoiceSettings.multiMode)
	{
		MultiChannelMode mode = option->iIntValue;
		svSetMultiChannelMode(mode);
	}
	else if(option==g_VoiceSettings.outputDevice)
	{
		char *str = eaGet(&g_VoiceSettings.outputDevices, option->iIntValue);
		if(str)
			svRenderDeviceSetActiveByName(str);
	}
	else if(option==g_VoiceSettings.inputDevice)
	{
		char *str = eaGet(&g_VoiceSettings.inputDevices, option->iIntValue);
		if(str)
			svCaptureDeviceSetActiveByName(str);
	}
}

void svSettingsCommit(OptionSetting *option)
{
	svOptionsSave();
}

void svSettingsUpdate(OptionSetting *option)
{
	if(!stricmp(option->pchName, OPTION_VOICE_VOLUME))
	{
		F32 vol = option->iIntValue;
		svSpeakersSetLevel(vol);
	}
	else if(!stricmp(option->pchName, OPTION_MIC_LEVEL))
	{
		F32 vol = option->iIntValue;
		svMicrophoneSetLevel(vol);
	}
}

void svSettingsResetToDefault(OptionSetting *option)
{
	svOptionsResetToDefaults();
}

void displayInt0To100AsPercent(OptionSetting *setting)
{
	estrPrintf(&setting->pchStringValue, "%d%%", setting->iIntValue);
}

void gclVoiceOptionUpdateRecordLevel(int level)
{
	if(g_VoiceSettings.recordLevel)
	{
		g_VoiceSettings.recordLevel->iIntValue = level;
		OptionSettingChanged(g_VoiceSettings.recordLevel, false);
	}
}

void gclVoiceOptionUpdateVolumeLevel(int level)
{
	if(g_VoiceSettings.volumeLevel)
	{
		g_VoiceSettings.volumeLevel->iIntValue = level;
		OptionSettingChanged(g_VoiceSettings.volumeLevel, false);
	}
}

void gclVoiceOptionUpdateOpenMic(int on)
{
	if(g_VoiceSettings.openMic)
	{
		g_VoiceSettings.openMic->iIntValue = !!on;
		OptionSettingChanged(g_VoiceSettings.openMic, false);
	}
}

void gclVoiceOptionUpdateRenderDevice(int useDevice)
{
	if(g_VoiceSettings.outputDevice)
	{
		g_VoiceSettings.outputDevice->iIntValue = useDevice;
		OptionSettingChanged(g_VoiceSettings.outputDevice, false);
	}
}

void gclVoiceOptionUpdateRecordDevice(int useDevice)
{
	if(g_VoiceSettings.inputDevice)
	{
		g_VoiceSettings.inputDevice->iIntValue = useDevice;
		OptionSettingChanged(g_VoiceSettings.inputDevice, false);
	}
}

void gclVoiceUpdateInputDevices(char **devices)
{
	eaCopyEStrings(&devices, &g_VoiceSettings.inputDevices);
	OptionSettingUpdateComboBoxOptions(g_VoiceSettings.inputDevice, VOICE_CATEGORY, &g_VoiceSettings.inputDevices, false);
}

void gclVoiceUpdateOutputDevices(char **devices)
{
	eaCopyEStrings(&devices, &g_VoiceSettings.outputDevices);
	OptionSettingUpdateComboBoxOptions(g_VoiceSettings.outputDevice, VOICE_CATEGORY, &g_VoiceSettings.outputDevices, false);
}

// Sets up the UI for the voice options
static void gclVoiceOptionsSetup(void)
{
	int i;
	static const char **strings = NULL;
	autoSettingsAddBitCheckbox(VOICE_CATEGORY, "DisableVoiceChat", &g_VoiceState.options.NoVoice, 0,
								svSettingsChanged, svSettingsCommit, svSettingsUpdate,
								svSettingsResetToDefault, NULL, 1);

	autoSettingsAddBitCheckbox(VOICE_CATEGORY, OPTION_MUTE_INACTIVE, &g_VoiceState.options.MuteInactive, 0,
								svSettingsChanged, svSettingsCommit, svSettingsUpdate,
								svSettingsResetToDefault, NULL, 1);

	g_VoiceSettings.volumeLevel = autoSettingsAddIntSlider(VOICE_CATEGORY, OPTION_VOICE_VOLUME, 0, 100, 
								&g_VoiceState.options.SpeakerVolume, 
								svSettingsChanged, svSettingsCommit, svSettingsUpdate, 
								svSettingsResetToDefault, displayInt0To100AsPercent, 
								NULL, 1);

	g_VoiceSettings.recordLevel = autoSettingsAddIntSlider(VOICE_CATEGORY, "MicLevel", 0, 100, 
								&g_VoiceState.options.MicLevel, 
								svSettingsChanged, svSettingsCommit, svSettingsUpdate, 
								svSettingsResetToDefault, displayInt0To100AsPercent, 
								NULL, 1);

	g_VoiceSettings.openMic = autoSettingsAddBitCheckbox(VOICE_CATEGORY, "OpenMicrophone", 
								&g_VoiceState.options.OpenMic, 0,
								svSettingsChanged, svSettingsCommit, svSettingsUpdate,
								svSettingsResetToDefault, NULL, 1);

	i=1;
	eaClear(&strings);
	while(MultiChannelModeEnum[i].key)
	{
		eaPush(&strings, MultiChannelModeEnum[i].key);
		i++;
	}

	g_VoiceSettings.multiMode = autoSettingsAddComboBox(VOICE_CATEGORY, "MultiChannelMode", &strings, true, 
									&(int)g_VoiceState.options.MultiMode, 
									svSettingsChanged, svSettingsCommit, svSettingsUpdate,
									svSettingsResetToDefault, NULL, 1);

	g_VoiceSettings.inputDevice = autoSettingsAddComboBox(VOICE_CATEGORY, OPTION_VOICE_INPUT_DEV, &g_VoiceSettings.inputDevices, false, 
									&g_VoiceState.options.InputDev,
									svSettingsChanged, svSettingsCommit, svSettingsUpdate,
									svSettingsResetToDefault, NULL, 1);

	g_VoiceSettings.outputDevice = autoSettingsAddComboBox(VOICE_CATEGORY, OPTION_VOICE_OUTPUT_DEV, &g_VoiceSettings.outputDevices, false, 
									&g_VoiceState.options.OutputDev,
									svSettingsChanged, svSettingsCommit, svSettingsUpdate,
									svSettingsResetToDefault, NULL, 1);

	svOptionsSetCallbacks(	gclVoiceOptionUpdateVolumeLevel, 
							gclVoiceOptionUpdateRecordLevel, 
							gclVoiceOptionUpdateOpenMic,
							gclVoiceOptionUpdateRecordDevice,
							gclVoiceOptionUpdateRenderDevice);

	svOptionsDataSetCallbacks(	gclVoiceUpdateInputDevices,
								gclVoiceUpdateOutputDevices);
}

AUTO_STARTUP(Voice_Options) ASTRT_DEPS(BasicOptions);
void gclVoiceOptionsStartup(void)
{
	if(!gConf.bVoiceChat)
		return;
	
	svOptionsLoadDefaults();
	gclVoiceOptionsSetup();
}