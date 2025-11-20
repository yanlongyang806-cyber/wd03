#include "BulletinUI.h"
#include "cmdparse.h"
#include "Entity.h"
#include "gclBasicOptions.h"
#include "gclCamera.h"
#include "gclControlScheme.h"
#include "gclEntity.h"
#include "gclOptions.h"
#include "GlobalTypes.h"
#include "inputData.h"
#include "inputGamepad.h"
#include "missionui_eval.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"
#include "Prefs.h"
#include "Team.h"
#include "winutil.h"
#include "WorldGrid.h"
#include "gclDirectionalIndicatorFX.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define BASIC_CATEGORY "Basic"

static bool s_bRumbleDefaultValue = true;
static S32 s_iInvertCameraDefaultValue = 0;
static S32 s_iDifficultyValue = 0;

static const char **s_ppchCursorOverlays;
static S32 s_iCursorSelection = 0;

static void gclBasicOptions_EnableRumbleUpdate(OptionSetting *setting)
{
	setting->iIntValue = gamepadRumbleEnabled();
}

static void gclBasicOptions_EnableRumbleCommitted(OptionSetting *setting)
{
	gamepadEnableRumble(setting->iIntValue);
}

static void gclBasicOptions_DirectionalIndicatorCommitted(OptionSetting *setting)
{
	GamePrefStoreInt("ArrowShow", difx_State.iShow);
	GamePrefStoreFloat("ArrowAlpha", difx_State.fAlpha);
}

static void gclBasicOptions_DirectionalIndicatorShowRevert(OptionSetting *setting)
{
	difx_State.iShow = difx_DefaultState.iShow;
	GamePrefStoreInt("ArrowShow", difx_DefaultState.iShow);
}

static void gclBasicOptions_DirectionalIndicatorAlphaRevert(OptionSetting *setting)
{
	difx_State.fAlpha= difx_DefaultState.fAlpha;
	GamePrefStoreFloat("ArrowAlpha", difx_DefaultState.fAlpha);
}

static void gclBasicOptions_CameraMouseFilterCommitted(OptionSetting *setting)
{
	CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);
	GamePrefStoreFloat("CameraMouseFilter", pCameraSettings->fMouseFilter);
}

static void gclBasicOptions_CameraOptionsCommitted(OptionSetting *setting)
{
	CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);
	
	GamePrefStoreInt("InvertUpDown", input_state.invertUpDown);
	GamePrefStoreInt("EnableCameraShake", pCameraSettings->bEnableCameraShake);
}

void gclBasicOptions_UpdateCameraOptions(void)
{
	CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);
	pCameraSettings->bEnableCameraShake = GamePrefGetInt("EnableCameraShake", pCameraSettings->bEnableCameraShakeByDefault);
	pCameraSettings->fMouseFilter = GamePrefGetFloat("CameraMouseFilter", g_DefaultCameraSettings.fMouseFilter);
}

static void gclBasicOptions_TooltipDelayChanged(OptionSetting *setting)
{
	globCmdParsef("ui_TooltipDelay %f", setting->fFloatValue);
	GamePrefStoreFloat("TooltipDelay", setting->fFloatValue);
}

static void gclBasicOptions_TooltipDelayUpdate(OptionSetting *setting)
{
	setting->fFloatValue = GamePrefGetFloat("TooltipDelay", gConf.fDefaultTooltipDelay);
}

static void gclBasicOptions_TooltipDelayDefault(OptionSetting *setting)
{
	F32 fDelay = GamePrefGetFloat("TooltipDelay", gConf.fDefaultTooltipDelay);
	globCmdParsef("ui_TooltipDelay %f", fDelay);
	if (setting)
		setting->fFloatValue = fDelay;
}

static void gclBasicOptions_DifficultyCommit(OptionSetting* pSetting)
{
	PlayerDifficultySet* pSet = &g_PlayerDifficultySet;
	PlayerDifficulty* pDifficulty = eaGet(&pSet->peaPlayerDifficulties, s_iDifficultyValue);
	Entity* pEnt = entActivePlayerPtr();
	if (pDifficulty)
	{
		ServerCmd_PlayerChangeDifficulty(pDifficulty->iIndex);
		if(pEnt && team_IsTeamLeader(pEnt)) {
			ServerCmd_Team_ChangeDifficulty(pDifficulty->iIndex);
		}
	}
}

static void gclBasicOptions_DisableAutoBulletinPopupCommitted(OptionSetting* pSetting)
{
	g_bDisableBulletinAutoPopup = !!pSetting->iIntValue;
	GamePrefStoreInt("DisableBulletinAutoPopup", g_bDisableBulletinAutoPopup);
}

static void gclBasicOptions_DisableAutoBulletinPopupUpdate(OptionSetting* pSetting)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer)
	{
		pSetting->iIntValue = !!g_bDisableBulletinAutoPopup;
	}
	else
	{
		pSetting->iIntValue = 0;
	}
}

static void gclBasicOptions_DisableAutoHailCommitted(OptionSetting* pSetting)
{
	g_bDisableAutoHail = !!pSetting->iIntValue;
	GamePrefStoreInt("DisableAutoHail", g_bDisableAutoHail);
}

static void gclBasicOptions_DisableAutoHailUpdate(OptionSetting* pSetting)
{
	pSetting->iIntValue = !!g_bDisableAutoHail;
}


static void gclBasicOptions_EnableAutoLootCommitted(OptionSetting* pSetting)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer)
	{
		pEnt->pPlayer->bEnableAutoLoot = !!pSetting->iIntValue;
		GamePrefStoreInt("EnableAutoLoot", pEnt->pPlayer->bEnableAutoLoot);
		ServerCmd_AutoLoot(pEnt->pPlayer->bEnableAutoLoot);
	}
}

static void gclBasicOptions_EnableAutoLootUpdate(OptionSetting* pSetting)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer)
	{
		pSetting->iIntValue = !!pEnt->pPlayer->bEnableAutoLoot;
	}
	else
	{
		pSetting->iIntValue = 0;
	}
}

static void gclBasicOptions_InvertCameraPerSchemeChanged(OptionSetting* pSetting)
{
	g_bInvertMousePerScheme = !!pSetting->iIntValue;
	schemes_UpdateForCurrentControlScheme(false);
	OptionsReload();
}

static void gclBasicOptions_InvertCameraPerSchemeCommitted(OptionSetting* pSetting)
{
	gclBasicOptions_InvertCameraPerSchemeChanged(pSetting);
	GamePrefStoreInt("InvertCameraPerScheme", g_bInvertMousePerScheme);
}

static void gclBasicOptions_InvertCameraPerSchemeUpdate(OptionSetting* pSetting)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer)
	{
		pSetting->iIntValue = !!g_bInvertMousePerScheme;
	}
	else
	{
		pSetting->iIntValue = 0;
	}
}

static void gclBasicOptions_InvertMouseCommitted(OptionSetting* pSetting)
{
	if (stricmp(pSetting->pchName,"Invert Camera X")==0)
	{
		input_state.invertX = !!pSetting->iIntValue;
		GamePrefStoreInt("InvertX", input_state.invertX);
	}
	else
	{
		input_state.invertY = !!pSetting->iIntValue;
		GamePrefStoreInt("InvertY", input_state.invertY);
	}
}

static void gclBasicOptions_CursorSkinCommitted(OptionSetting* pSetting)
{
	if (pSetting->iIntValue < 0)
	{
		pSetting->iIntValue = eaFind(&s_ppchCursorOverlays, ResourceOverlayBaseName("UICursor"));
		MAX1(pSetting->iIntValue, 0);
	}
	GamePrefStoreString("CursorSkin", s_ppchCursorOverlays[pSetting->iIntValue]);
	ResourceOverlayLoad("UICursor", s_ppchCursorOverlays[pSetting->iIntValue]);
}

static void gclBasicOptions_CursorSkinUpdate(OptionSetting* pSetting)
{
	pSetting->iIntValue = eaFind(&s_ppchCursorOverlays, ResourceOverlayBaseName("UICursor"));
	MAX1(pSetting->iIntValue, 0);
}

static bool gclBasicOptions_HideInvertMouseOption(OptionSetting* pSetting)
{
	return g_bInvertMousePerScheme;
}

static void gclBasicOptions_Revert(OptionSetting *setting)
{
	Entity* pEnt = entActivePlayerPtr();
	if (stricmp(setting->pchName,"Enable Rumble")==0)
	{
		gamepadEnableRumble(s_bRumbleDefaultValue);
		setting->iIntValue = s_bRumbleDefaultValue;
	}
	else if (stricmp(setting->pchName,"EnableCameraShake")==0)
	{
		CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);
		GamePrefStoreInt("EnableCameraShake", pCameraSettings->bEnableCameraShakeByDefault);
		setting->iIntValue = pCameraSettings->bEnableCameraShakeByDefault;
	}
	else if (stricmp(setting->pchName,"TooltipDelay")==0)
	{
		GamePrefStoreFloat("TooltipDelay", gConf.fDefaultTooltipDelay);
		setting->fFloatValue = gConf.fDefaultTooltipDelay;
	}
	else if (stricmp(setting->pchName,"CameraMouseFilter")==0)
	{
		CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);
		GamePrefStoreFloat("CameraMouseFilter", g_DefaultCameraSettings.fMouseFilter);
		pCameraSettings->fMouseFilter = g_DefaultCameraSettings.fMouseFilter;
	}
	else if (stricmp(setting->pchName,"PlayerDifficulty")==0)
	{
		s_iDifficultyValue = 0;
		gclBasicOptions_DifficultyCommit(NULL);
	}
	else if (stricmp(setting->pchName,"DisableAutoBulletinPopup")==0)
	{
		setting->iIntValue = 0;
		gclBasicOptions_DisableAutoBulletinPopupCommitted(setting);
	}
	else if (stricmp(setting->pchName,"DisableAutoHail")==0)
	{
		setting->iIntValue = 0;
		gclBasicOptions_DisableAutoHailCommitted(setting);
	}
	else if (stricmp(setting->pchName,"EnableAutoLoot")==0)
	{
		setting->iIntValue = 0;
		gclBasicOptions_EnableAutoLootCommitted(setting);
	}
	else if (stricmp(setting->pchName,"InvertCameraPerScheme")==0)
	{
		setting->iIntValue = 0;
		gclBasicOptions_InvertCameraPerSchemeCommitted(setting);
	}
	else if (stricmp(setting->pchName,"CursorSkin")==0)
	{
		setting->iIntValue = -1;
		gclBasicOptions_CursorSkinCommitted(setting);
	}
	else if (!g_bInvertMousePerScheme)
	{
		setting->iIntValue = s_iInvertCameraDefaultValue;
		gclBasicOptions_InvertMouseCommitted(setting);
	}
}

static bool gclBasicOptions_ShouldEnableDifficultyCombo(void)
{
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);
	if (eMapType == ZMTYPE_MISSION || eMapType == ZMTYPE_OWNED)
	{
		return false;
	}
	if (eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties) <= 1)
	{
		return false;
	}
	return true;
}

static bool gclBasicOptions_ShouldHideDifficultyCombo(OptionSetting* pSetting)
{
	if (!eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties))
	{
		return true;
	}
	if (playerActivePlayerPtr() == NULL)
	{
		return true;
	}
	return false;
}

static void gclBasicOptions_DifficultyUpdate(OptionSetting* pSetting)
{
	S32 i, iSize = eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties);
	const char** ppchDifficulties = NULL;
	Entity* pEnt = entActivePlayerPtr();
	s_iDifficultyValue = pEnt && pEnt->pPlayer ? pEnt->pPlayer->iDifficulty : 0;

	for (i = 0; i < iSize; i++)
	{
		PlayerDifficulty* pDifficulty = g_PlayerDifficultySet.peaPlayerDifficulties[i];
		eaPush(&ppchDifficulties, TranslateMessageRef(pDifficulty->hName));
	}
	OptionSettingUpdateComboBoxOptions(pSetting, BASIC_CATEGORY, (char***)&ppchDifficulties, false); 
	OptionSettingSetActive(pSetting, gclBasicOptions_ShouldEnableDifficultyCombo());
	eaDestroy(&ppchDifficulties);
}

static void gclBasicOptions_AddDifficultyCombo(void)
{
	OptionSetting* pSetting;
	pSetting = autoSettingsAddComboBox(BASIC_CATEGORY, "PlayerDifficulty", 
				NULL, false, &s_iDifficultyValue, NULL, 
				gclBasicOptions_DifficultyCommit, 
				gclBasicOptions_DifficultyUpdate,
				gclBasicOptions_Revert, NULL, true);
	OptionSettingSetHideCallback(pSetting, gclBasicOptions_ShouldHideDifficultyCombo);
}

void DEFAULT_LATELINK_gameSpecific_gclBasicOptions_Init(const char* pchCategory)
{
}

AUTO_STARTUP(BasicOptions) ASTRT_DEPS(AS_Messages);
void gclBasicOptions_Init(void)
{
	CameraSettings* pCameraSettings = gclCamera_GetSettings(NULL);

	gameSpecific_gclBasicOptions_Init(BASIC_CATEGORY);
	gclBasicOptions_AddDifficultyCombo();
	OptionSettingAddBool(BASIC_CATEGORY, "Enable Rumble", s_bRumbleDefaultValue, NULL, gclBasicOptions_EnableRumbleCommitted, gclBasicOptions_EnableRumbleUpdate, gclBasicOptions_Revert, NULL, NULL);

	g_bDisableAutoHail = GamePrefGetInt("DisableAutoHail", 0);
	g_bDisableBulletinAutoPopup = GamePrefGetInt("DisableBulletinAutoPopup", 0);
	input_state.invertX = GamePrefGetInt("InvertX", s_iInvertCameraDefaultValue);
	input_state.invertY = GamePrefGetInt("InvertY", s_iInvertCameraDefaultValue);
	input_state.invertUpDown = GamePrefGetInt("invertUpDown", 1);
#ifdef WIN32
	input_state.reverseMouseButtons = GetSystemMetrics(SM_SWAPBUTTON);
#endif
	g_bInvertMousePerScheme = GamePrefGetInt("InvertCameraPerScheme", g_bInvertMousePerScheme);
	
	//autoSettingsAddBit(BASIC_CATEGORY, "EnableCameraShake", (int*)&pCameraSettings->bEnableCameraShake, 0x1, NULL, gclBasicOptions_CameraOptionsCommitted, NULL, gclBasicOptions_Revert, NULL, true);
	
	if (gConf.bInvertMousePerControlSchemeOption)
	{
		OptionSettingAddBool(BASIC_CATEGORY, "InvertCameraPerScheme", false, gclBasicOptions_InvertCameraPerSchemeChanged, gclBasicOptions_InvertCameraPerSchemeCommitted, gclBasicOptions_InvertCameraPerSchemeUpdate, gclBasicOptions_Revert, NULL, NULL);
	}
	autoSettingsAddBit(BASIC_CATEGORY, "Invert Camera X", &input_state.invertX, 0x1, NULL, gclBasicOptions_InvertMouseCommitted, NULL, gclBasicOptions_Revert, NULL, true);
	autoSettingsAddBit(BASIC_CATEGORY, "Invert Camera Y", &input_state.invertY, 0x1, NULL, gclBasicOptions_InvertMouseCommitted, NULL, gclBasicOptions_Revert, NULL, true);

	if (gConf.bShowCameraFilteringOption)
	{
		autoSettingsAddFloatSlider(BASIC_CATEGORY, "CameraMouseFilter", 0.0f, 1.0f, &pCameraSettings->fMouseFilter, NULL, gclBasicOptions_CameraMouseFilterCommitted, NULL, gclBasicOptions_Revert, NULL, NULL, true);
	}
	if (gConf.bEnableBulletins)
	{
		OptionSettingAddBool(BASIC_CATEGORY, "DisableAutoBulletinPopup", false, NULL, gclBasicOptions_DisableAutoBulletinPopupCommitted, gclBasicOptions_DisableAutoBulletinPopupUpdate, gclBasicOptions_Revert, NULL, NULL);
	}
	if (gConf.bEnableAutoHailDisableOption)
	{
		OptionSettingAddBool(BASIC_CATEGORY, "DisableAutoHail", false, NULL, gclBasicOptions_DisableAutoHailCommitted, gclBasicOptions_DisableAutoHailUpdate, gclBasicOptions_Revert, NULL, NULL);
	}
	if (gConf.bShowAutoLootOption)
	{
		OptionSettingAddBool(BASIC_CATEGORY, "EnableAutoLoot", false, NULL, gclBasicOptions_EnableAutoLootCommitted, gclBasicOptions_EnableAutoLootUpdate, gclBasicOptions_Revert, NULL, NULL);
	}
	gclBasicOptions_TooltipDelayDefault(NULL);
	OptionSettingAddSlider(BASIC_CATEGORY, "TooltipDelay", 0.f, 5.f, gConf.fDefaultTooltipDelay, gclBasicOptions_TooltipDelayChanged, NULL, gclBasicOptions_TooltipDelayUpdate, gclBasicOptions_Revert, NULL, NULL, NULL);

	if (DirectionalIndicatorFX_Enabled())
	{
		autoSettingsAddBit(BASIC_CATEGORY, "ArrowShow", &difx_State.iShow, 0x1, gclBasicOptions_DirectionalIndicatorCommitted, NULL, NULL, gclBasicOptions_DirectionalIndicatorShowRevert, NULL, true);
		autoSettingsAddFloatSlider(BASIC_CATEGORY, "ArrowAlpha", 0.0f, 1.0f, &difx_State.fAlpha, NULL, gclBasicOptions_DirectionalIndicatorCommitted, NULL, gclBasicOptions_DirectionalIndicatorAlphaRevert, NULL, NULL, true);
	}

	options_HideOption(BASIC_CATEGORY, "Invert Camera X", gclBasicOptions_HideInvertMouseOption);
	options_HideOption(BASIC_CATEGORY, "Invert Camera Y", gclBasicOptions_HideInvertMouseOption);

	ResourceOverlayNames("UICursor", &s_ppchCursorOverlays);
	if (eaSize(&s_ppchCursorOverlays) > 0)
	{
		s_iCursorSelection = eaFind(&s_ppchCursorOverlays, GamePrefGetString("CursorSkin", ResourceOverlayBaseName("UICursor")));
		MAX1(s_iCursorSelection, 0);
		autoSettingsAddComboBox(BASIC_CATEGORY, "CursorSkin", &s_ppchCursorOverlays, true, &s_iCursorSelection, NULL, gclBasicOptions_CursorSkinCommitted, gclBasicOptions_CursorSkinUpdate, gclBasicOptions_Revert, NULL, true);
	}
}

void gclBasicOptions_InitPlayerOptions(Entity* pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		pEnt->pPlayer->bEnableAutoLoot = GamePrefGetInt("EnableAutoLoot", 0);
		if (pEnt->pPlayer->bEnableAutoLoot)
		{
			ServerCmd_AutoLoot(pEnt->pPlayer->bEnableAutoLoot);
		}
	}
}
