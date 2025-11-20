#include "earray.h"
#include "Entity.h"
#include "EString.h"
#include "gclControlScheme.h"
#include "gclHUDOptions.h"
#include "gclEntity.h"
#include "gclNotify.h"
#include "gclOptions.h"
#include "HUDOptionsCommon.h"
#include "stdtypes.h"
#include "player.h"
#include "StringCache.h"
#include "uiGen.h"
#include "ControlScheme.h"
#include "entity_h_ast.h"
#include "gclOptions_h_ast.h"
#include "player_h_ast.h"
#include "GlobalTypes.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define HUD_CATEGORY "HUDOptions"

typedef enum OverheadEntityOptions {
	OVERHEAD_ENTITY_OPTION_NAME,
	OVERHEAD_ENTITY_OPTION_LIFE,
	OVERHEAD_ENTITY_OPTION_RETICLE,
	OVERHEAD_ENTITY_OPTION_POWERMODENAME,
	OVERHEAD_ENTITY_OPTION_POWERMODELIFE,
	OVERHEAD_ENTITY_OPTION_POWERMODERETICLE,
	OVERHEAD_ENTITY_OPTION_COUNT,
} OverheadEntityOptions;

#define OVERHEAD_ENABLE_MOUSE_OVER			1
#define OVERHEAD_ENABLE_TARGETED			2
#define OVERHEAD_ENABLE_TARGETOFTARGETED	4
#define OVERHEAD_ENABLE_DAMAGED				8
#define OVERHEAD_ENABLE_ALWAYS				16

#define OVERHEAD_ENABLE_SELF_DAMAGED		2
#define OVERHEAD_ENABLE_SELF_ALWAYS			4

// Overhead entity type specific data
// Order must match OverHeadEntityTypes (validated in AUTO_RUN)
#define ENTITY_TYPE(type, textType, field, mouseOver, targeted, targetOfTargeted, damaged, always) { OVERHEAD_ENTITY_TYPE_##type, textType, offsetof(PlayerShowOverhead, field), mouseOver, targeted, targetOfTargeted, damaged, always }
static struct {
	int checkValue;
	const char *name;
	U32 offset;
	U32 maskMouseOver;
	U32 maskTargeted;
	U32 maskTargetOfTargeted;
	U32 maskDamaged;
	U32 maskAlways;
} s_EntityTypeList[] = {
	ENTITY_TYPE(ENEMY, "Enemy", eShowEnemy, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(FRIENDLY_NPC, "FriendlyNPC", eShowFriendlyNPC, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(FRIEND, "Friend", eShowFriends, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(SUPERGROUP, "Supergroup", eShowSupergroup, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(TEAM, "Team", eShowTeam, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(PET, "Pet", eShowPet, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(PLAYER, "Player", eShowPlayer, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(ENEMY_PLAYER, "EnemyPlayer", eShowEnemyPlayer, OVERHEAD_ENABLE_MOUSE_OVER, OVERHEAD_ENABLE_TARGETED, OVERHEAD_ENABLE_TARGETOFTARGETED, OVERHEAD_ENABLE_DAMAGED, OVERHEAD_ENABLE_ALWAYS),
	ENTITY_TYPE(SELF, "Self", eShowSelf, OVERHEAD_ENABLE_MOUSE_OVER, 0, 0, OVERHEAD_ENABLE_SELF_DAMAGED, OVERHEAD_ENABLE_SELF_ALWAYS),
};
STATIC_ASSERT(ARRAY_SIZE(s_EntityTypeList) == OVERHEAD_ENTITY_TYPE_COUNT);

// Overhead entity option specific data
// Order must match OverheadEntityOptions (validated in AUTO_RUN)
#define OVERHEAD_ENTITY_FLAGS(name, textName) { OVERHEAD_ENTITY_OPTION_##name, textName, OVERHEAD_ENTITY_FLAG_ALWAYS_##name, OVERHEAD_ENTITY_FLAG_MOUSE_OVER_##name, OVERHEAD_ENTITY_FLAG_TARGETED_##name, OVERHEAD_ENTITY_FLAG_DAMAGED_##name, OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_##name }
static struct {
	int checkValue;
	const char *name;
	OverHeadEntityFlags always;
	OverHeadEntityFlags mouseOver;
	OverHeadEntityFlags targeted;
	OverHeadEntityFlags damaged;
	OverHeadEntityFlags targetOfTargeted;
} s_OverheadList[] = {
	OVERHEAD_ENTITY_FLAGS(NAME, "Name"),
	OVERHEAD_ENTITY_FLAGS(LIFE, "Life"),
	OVERHEAD_ENTITY_FLAGS(RETICLE, "Reticle"),
	OVERHEAD_ENTITY_FLAGS(POWERMODENAME, "PowerModeName"),
	OVERHEAD_ENTITY_FLAGS(POWERMODELIFE, "PowerModeLife"),
	OVERHEAD_ENTITY_FLAGS(POWERMODERETICLE, "PowerModeReticle"),
};
STATIC_ASSERT(ARRAY_SIZE(s_OverheadList) == OVERHEAD_ENTITY_OPTION_COUNT);

static const char **ppchShowOptionsList = NULL;
static const char **ppchShowOptionsListWithoutTargeted = NULL;
static const char **ppchShowOptionsListWithoutAlways = NULL;
static const char **ppchShowOptionsListWithAlwaysShowContacts = NULL;
static const char **ppchShowReticlesAsList = NULL;
static const char **ppchNotifyAudioModes = NULL;
static U32 uiAdvancedOptionSettings[OVERHEAD_ENTITY_TYPE_COUNT][OVERHEAD_ENTITY_OPTION_COUNT];

static int s_iShowResticlesAsIndex;

static bool s_bShowMapChoice = false;

static PlayerHUDOptions s_CurrentHUDOptions = {0};
static S32 s_iCurrentRegionDisplay = 0;

static void hudSettingsChangedCallback(OptionSetting *setting);
static void hudSettingsComittedCallback(OptionSetting *setting_UNUSED);
static void hudSettingsUpdateCallback(OptionSetting *setting_UNUSED);
static void hudSettingsRestoreDefaultsCallback(OptionSetting *setting_UNUSED);

PlayerHUDOptions* entGetCurrentHUDOptionsEx(Entity* pEnt, bool bGetRegionUI, bool bGetDefaults)
{
	S32 i;
	ControlSchemeRegionType eCurSchemeRegion;
	if (!pEnt || !pEnt->pPlayer || !pEnt->pPlayer->pUI || !pEnt->pPlayer->pUI->pLooseUI)
	{
		return NULL;
	}

	if (!bGetRegionUI || g_bOptionsInit)
	{
		eCurSchemeRegion = getSchemeRegionTypeFromRegionType(entGetWorldRegionTypeOfEnt(pEnt));
	}
	else
	{
		ControlSchemeRegionData* pData = eaGet(&g_eaUIRegions, s_iCurrentRegionDisplay);
		eCurSchemeRegion = pData ? pData->eSchemeRegion : 0;
	}

	for (i = eaSize(&pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions)-1; i >= 0; i--)
	{
		PlayerHUDOptions* pHUDOptions = pEnt->pPlayer->pUI->pLooseUI->eaHUDOptions[i];

		if (pHUDOptions->eRegion == eCurSchemeRegion)
		{
			static PlayerHUDOptions s_UpgradedHUDOptions;
			if (upgradeHUDOptions(pHUDOptions, &s_UpgradedHUDOptions))
				return &s_UpgradedHUDOptions;
			return pHUDOptions;
		}
	}

	if (bGetDefaults)
	{
		return getDefaultHUDOptions(eCurSchemeRegion);
	}
	return NULL;
}

static OverHeadEntityFlags getAlwaysOptionMaskAndList(U32 entType, U32 entShowPart, const char ***optionsListToUse)
{
	if(entType == OVERHEAD_ENTITY_TYPE_ENEMY || entType == OVERHEAD_ENTITY_TYPE_FRIENDLY_NPC) {
		ControlSchemeRegionInfo* pSchemeRegionInfo = schemes_GetSchemeRegionInfo(s_CurrentHUDOptions.eRegion);
		if (!pSchemeRegionInfo || !pSchemeRegionInfo->bEnableAlwaysShowOverheadOption) {
			if (entType == OVERHEAD_ENTITY_TYPE_ENEMY || entShowPart > 0) {
				if (optionsListToUse) {
					*optionsListToUse = ppchShowOptionsListWithoutAlways;
				}
				return OVERHEAD_ENTITY_FLAG_NEVER;
			} else {
				if (optionsListToUse) {
					*optionsListToUse = ppchShowOptionsListWithAlwaysShowContacts;
				}
				return OVERHEAD_ENTITY_FLAG_ALWAYS_NAME_CONTACTS;
			}
		}
	}
	if (optionsListToUse) {
		*optionsListToUse = ppchShowOptionsList;
	}
	return s_OverheadList[entShowPart].always;
}

static void getValuePtrAndMask(PlayerHUDOptions* pHUDOptions, 
							   U32 entType, U32 entShowPart, OverHeadEntityFlags **valuePtr, 
							   OverHeadEntityFlags *alwaysMask, 
							   OverHeadEntityFlags *mouseOverMask, 
							   OverHeadEntityFlags *targetedMask,
							   OverHeadEntityFlags *damagedMask, 
							   OverHeadEntityFlags *targetOfTargetMask) 
{
	if (pHUDOptions)
		*valuePtr = (OverHeadEntityFlags *)((char*)&pHUDOptions->ShowOverhead + s_EntityTypeList[entType].offset);
	else
		*valuePtr = NULL;

	*alwaysMask = getAlwaysOptionMaskAndList(entType, entShowPart, NULL);
	*mouseOverMask = s_OverheadList[entShowPart].mouseOver;
	*targetedMask = s_OverheadList[entShowPart].targeted;
	*damagedMask = s_OverheadList[entShowPart].damaged;
	*targetOfTargetMask = s_OverheadList[entShowPart].targetOfTargeted;
}

static void addAdvancedOptions(bool bInit) 
{
	static PlayerHUDOptionsPowerMode s_InitOptions;
	int i, j, entType;
	PlayerHUDOptionsPowerMode *pPowerModeOption = NULL;

	if (bInit)
	{
		// Make sure these settings are up to date.
		hudSettingsUpdateCallback(NULL);

		// Find the superset of power mode options (only create the ones that are used)
		for (i = eaSize(&g_DefaultHUDOptions.eaPowerModeOptions) - 1; i >= 0; i--)
		{
			pPowerModeOption = g_DefaultHUDOptions.eaPowerModeOptions[i];
			for (j = eaiSize(&pPowerModeOption->eaiEnableTypes) - 1; j >= 0; j--)
				eaiPushUnique(&s_InitOptions.eaiEnableTypes, pPowerModeOption->eaiEnableTypes[j]);
			for (j = eaiSize(&pPowerModeOption->eaiPowerModes) - 1; j >= 0; j--)
				eaiPushUnique(&s_InitOptions.eaiPowerModes, pPowerModeOption->eaiPowerModes[j]);
			s_InitOptions.bEnableName = s_InitOptions.bEnableName || pPowerModeOption->bEnableName;
			s_InitOptions.bEnableLife = s_InitOptions.bEnableLife || pPowerModeOption->bEnableLife;
			s_InitOptions.bEnableReticle = s_InitOptions.bEnableReticle || pPowerModeOption->bEnableReticle;
		}
		pPowerModeOption = &s_InitOptions;
	}
	else
	{
		for (i = eaSize(&g_DefaultHUDOptions.eaPowerModeOptions) - 1; i >= 0; i--)
		{
			if (g_DefaultHUDOptions.eaPowerModeOptions[i]->eRegion == s_CurrentHUDOptions.eRegion)
			{
				pPowerModeOption = g_DefaultHUDOptions.eaPowerModeOptions[i];
				break;
			}
		}
	}

	for(entType = 0; entType < OVERHEAD_ENTITY_TYPE_COUNT; entType++) {
		const char *entTypeStr = s_EntityTypeList[entType].name;
		int entShowPart;

		// Determine hidden flags for this entity type
		bool hiddenFlags[] = {
			false, false, false,
			!pPowerModeOption || !pPowerModeOption->bEnableName || eaiFind(&pPowerModeOption->eaiEnableTypes, entType) < 0,
			!pPowerModeOption || !pPowerModeOption->bEnableLife || eaiFind(&pPowerModeOption->eaiEnableTypes, entType) < 0,
			!pPowerModeOption || !pPowerModeOption->bEnableReticle || eaiFind(&pPowerModeOption->eaiEnableTypes, entType) < 0,
		};

		if (bInit)
		{
			OptionSettingAddDivider(HUD_CATEGORY, entTypeStr, NULL, NULL, NULL);
		}

		for(entShowPart = 0; entShowPart < OVERHEAD_ENTITY_OPTION_COUNT; entShowPart++) {
			const char *showPartStr = s_OverheadList[entShowPart].name;
			char* optionName = NULL;
			const char **optionsListToUse = ppchShowOptionsList;
			bool bHidden = hiddenFlags[entShowPart];
			OptionSetting* pSetting;

			estrPrintf(&optionName, "Show_%s_%s", entTypeStr, showPartStr);

			getAlwaysOptionMaskAndList(entType, entShowPart, &optionsListToUse);

			if(entType == OVERHEAD_ENTITY_TYPE_SELF) {
				// No "targeted" option for self.
				optionsListToUse = ppchShowOptionsListWithoutTargeted;
				if (entShowPart == 2) {
					// Don't give reticle as an option
					continue;
				}
			}

			if (bInit)
			{
				if (bHidden)
					continue;

				pSetting = autoSettingsAddCheckComboBox(HUD_CATEGORY, optionName, &optionsListToUse, false, &(uiAdvancedOptionSettings[entType][entShowPart]), TranslateMessageKey("HudOptions_Never"), hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);

				// Default to hiding the power mode options
				pSetting->bHide = 3 <= entShowPart && entShowPart <= 5;
			}
			else
			{
				pSetting = OptionSettingGet(HUD_CATEGORY, optionName);
				if (pSetting)
				{	
					eaClearStruct(&pSetting->eaComboBoxOptions, parse_OptionSettingComboChoice);
					OptionSettingComboBoxAddOptions(pSetting, HUD_CATEGORY, &optionsListToUse, false);
					OptionSettingChanged(pSetting,false);
					pSetting->bHide = bHidden;
				}
			}
			estrDestroy(&optionName);
		}
	}

}

static void hudSettingsChangedCallback(OptionSetting *setting) 
{
	U32 entType;
	OverHeadEntityFlags alwaysMask, mouseOverMask, targetedMask, targetOfTargetMask, damagedMask;

	// Entity parts.
	for(entType = 0; entType < OVERHEAD_ENTITY_TYPE_COUNT; entType++) {

		OverHeadEntityFlags *valuePtr;
		int entShowPart;

		getValuePtrAndMask(&s_CurrentHUDOptions, entType, 0, &valuePtr, &alwaysMask, &mouseOverMask, &targetedMask, &damagedMask, &targetOfTargetMask);

		if (!valuePtr)
			continue;

		// Reset the set flags
		*valuePtr = 0;

		// Iterate through all the parts for this entity type and build up the flags to what they should be.
		for(entShowPart = 0; entShowPart < OVERHEAD_ENTITY_OPTION_COUNT; entShowPart++) {
			getValuePtrAndMask(&s_CurrentHUDOptions, entType, entShowPart, &valuePtr, &alwaysMask, &mouseOverMask, &targetedMask, &damagedMask, &targetOfTargetMask);

			// The bit shift amounts in here correspond to positions in the
			// list of possible options for each field.
			if(uiAdvancedOptionSettings[entType][entShowPart] & s_EntityTypeList[entType].maskMouseOver) {
				*valuePtr |= mouseOverMask;
			}
			if(uiAdvancedOptionSettings[entType][entShowPart] & s_EntityTypeList[entType].maskTargeted) {
				*valuePtr |= targetedMask;
			}
			if(uiAdvancedOptionSettings[entType][entShowPart] & s_EntityTypeList[entType].maskTargetOfTargeted) {
				*valuePtr |= targetOfTargetMask;
			}
			if(uiAdvancedOptionSettings[entType][entShowPart] & s_EntityTypeList[entType].maskDamaged) {
				*valuePtr |= damagedMask;
			}
			if(uiAdvancedOptionSettings[entType][entShowPart] & s_EntityTypeList[entType].maskAlways) {
				*valuePtr |= alwaysMask;
			}
		}
	}
}

void DEFAULT_LATELINK_gameSpecific_HUDOptions_Init(const char* pchCategory)
{
}

static void hudSettingsComittedCallback(OptionSetting *setting_UNUSED) 
{
	static U32 s_uiLastCommitMs = 0;
	if (s_uiLastCommitMs != g_ui_State.totalTimeInMs)
	{
		Entity* pEnt = entActivePlayerPtr();
		S32 eCurRegion = s_CurrentHUDOptions.eRegion;

		if (pEnt && pEnt->pPlayer && eCurRegion >= 0)
		{
			PlayerHUDOptions* pHUDOptions = getDefaultHUDOptions(eCurRegion);
			switch(s_iShowResticlesAsIndex)
			{
			case 0:
				s_CurrentHUDOptions.ShowOverhead.eShowReticlesAs = OVERHEAD_RETICLE_HIGHLIGHT;
				break;
			case 1:
				s_CurrentHUDOptions.ShowOverhead.eShowReticlesAs = OVERHEAD_RETICLE_BOX;
				break;
			case 2:
				s_CurrentHUDOptions.ShowOverhead.eShowReticlesAs = OVERHEAD_RETICLE_BOX | OVERHEAD_RETICLE_HIGHLIGHT;
				break;
			default:
				s_CurrentHUDOptions.ShowOverhead.eShowReticlesAs = 0;
			}
			if (StructCompare(parse_PlayerHUDOptions, pHUDOptions, &s_CurrentHUDOptions, 0, 0, 0))
			{
				ServerCmd_SetHudOptionsField(&s_CurrentHUDOptions);
			}
			else
			{
				ServerCmd_ResetHudOptions(eCurRegion);
			}

			// Show map choice flag. TODO(MK): this should probably be moved elsewhere
			if (pEnt->pPlayer->pUI->pLooseUI->bShowMapChoice != (U32)s_bShowMapChoice) 
			{
				ServerCmd_SetAlwaysShowMapTransfer(s_bShowMapChoice);
			}
		}
		s_uiLastCommitMs = g_ui_State.totalTimeInMs;
	}
}

static void gclHUDOptions_Update(PlayerHUDOptions* pHUDOptions)
{
	Entity* pEnt = entActivePlayerPtr();
	OverHeadEntityFlags *valuePtr;
	U32 entType;
	OverHeadEntityFlags mouseOverMask, targetedMask, alwaysMask, damagedMask, targetOfTargetMask;

	if (!pEnt || !pEnt->pPlayer || !pHUDOptions) return;

	StructCopyAll(parse_PlayerHUDOptions, pHUDOptions, &s_CurrentHUDOptions);

	if (s_CurrentHUDOptions.ShowOverhead.eShowReticlesAs == OVERHEAD_RETICLE_HIGHLIGHT)
		s_iShowResticlesAsIndex = 0;
	else if (s_CurrentHUDOptions.ShowOverhead.eShowReticlesAs == OVERHEAD_RETICLE_BOX)
		s_iShowResticlesAsIndex = 1;
	else
		s_iShowResticlesAsIndex = 2;

	// Show map choice flag. //TODO(MK): this should probably be moved elsewhere
	if ( gConf.bNoUserMapInstanceChoice && ( pEnt->pPlayer->accessLevel < ACCESS_DEBUG ) )
	{
		// if the global config option is set, then disable the map choice option unless the player has debug accesslevel
		s_bShowMapChoice = false;
	}
	else
	{
		s_bShowMapChoice = pEnt->pPlayer->pUI->pLooseUI->bShowMapChoice;
	}

	for(entType = 0; entType < OVERHEAD_ENTITY_TYPE_COUNT; entType++) {
		int entShowPart;

		for(entShowPart = 0; entShowPart < OVERHEAD_ENTITY_OPTION_COUNT; entShowPart++) 
		{
			getValuePtrAndMask(pHUDOptions, entType, entShowPart, &valuePtr, &alwaysMask, &mouseOverMask, &targetedMask, &damagedMask, &targetOfTargetMask);

			uiAdvancedOptionSettings[entType][entShowPart] = 0;

			if(*valuePtr & mouseOverMask) {
				uiAdvancedOptionSettings[entType][entShowPart] |= s_EntityTypeList[entType].maskMouseOver;
			}
			if(*valuePtr & targetedMask) {
				uiAdvancedOptionSettings[entType][entShowPart] |= s_EntityTypeList[entType].maskTargeted;
			}
			if(*valuePtr & targetOfTargetMask) {
				uiAdvancedOptionSettings[entType][entShowPart] |= s_EntityTypeList[entType].maskTargetOfTargeted;
			}
			if(*valuePtr & damagedMask) {
				uiAdvancedOptionSettings[entType][entShowPart] |= s_EntityTypeList[entType].maskDamaged;
			}
			if(*valuePtr & alwaysMask) {
				uiAdvancedOptionSettings[entType][entShowPart] |= s_EntityTypeList[entType].maskAlways;
			}
		}
	}
}

static bool gclHUDOptions_ShouldUpdate(void)
{
	static U32 s_uiLastUpdateMs = 0;
	if (s_uiLastUpdateMs != g_ui_State.totalTimeInMs)
	{
		s_uiLastUpdateMs = g_ui_State.totalTimeInMs;
		return true;
	}
	return false;
}

static void hudSettingsUpdateCallback(OptionSetting *setting_UNUSED) 
{
	if (gclHUDOptions_ShouldUpdate())
	{
		Entity* pEnt = entActivePlayerPtr();
		PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptionsEx(pEnt, true, true);
		gclHUDOptions_Update(pHUDOptions);
	}
}

static void hudSettingsRestoreDefaultsCallback(OptionSetting *setting_UNUSED) 
{
	static U32 s_uiLastRestoreMs = 0;
	if (s_uiLastRestoreMs != g_ui_State.totalTimeInMs)
	{
		Entity* pEnt = entActivePlayerPtr();
		PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptionsEx(pEnt, true, false);
		PlayerHUDOptions* pHUDDefaults = getDefaultHUDOptions(s_CurrentHUDOptions.eRegion);

		if(pHUDOptions) 
		{
			ServerCmd_ResetHudOptions(pHUDOptions->eRegion);
			StructCopyAll(parse_PlayerHUDOptions, pHUDDefaults, pHUDOptions);
			gclHUDOptions_Update(pHUDOptions);
		}
		s_uiLastRestoreMs = g_ui_State.totalTimeInMs;
	}
}

static void gclHUDOptions_UpdateSelectedRegion(OptionSetting* pSetting, ControlSchemeRegionType eRegion)
{
	if (g_bOptionsInit)
	{
		S32 i;
		for (i = eaSize(&g_eaUIRegions)-1; i >= 0; i--)
		{
			if (g_eaUIRegions[i]->eSchemeRegion == eRegion)
			{
				break;
			}
		}
		if (i >= 0)
		{
			s_iCurrentRegionDisplay = pSetting->iIntValue = i;
		}
		OptionSettingChanged(pSetting, false);
	}
}

static void gclHUDOptions_RegionUpdateCB(OptionSetting* pSetting)
{
	if (gclHUDOptions_ShouldUpdate())
	{
		Entity* pEnt = entActivePlayerPtr();
		PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptionsEx(pEnt, true, true); 
		gclHUDOptions_Update(pHUDOptions);
		OptionsUpdateCategory(HUD_CATEGORY);
		addAdvancedOptions(false);
		gclHUDOptions_UpdateSelectedRegion(pSetting, pHUDOptions->eRegion);
	}
}

static void gclHUDOptions_RegionChangedCB(OptionSetting* pSetting)
{
	if (gclHUDOptions_ShouldUpdate())
	{
		Entity* pEnt = entActivePlayerPtr();
		PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptionsEx(pEnt, true, true); 
		gclHUDOptions_Update(pHUDOptions);
		OptionsUpdateCategory(HUD_CATEGORY);
		addAdvancedOptions(false);
	}
}

static void gclHUDOptions_RegionCommitCB(OptionSetting* pSetting)
{
}

static void gclHUDOptions_RegionRevertCB(OptionSetting* pSetting)
{
}

static void gclHUDOptions_InitRegionList(void)
{
	S32 i;
	const char** ppchSchemeRegionNames = NULL;
	
	gclControlSchemeGenerateRegionList();

	if (eaSize(&g_eaUIRegions)==0)
		return;
	
	eaClear(&ppchSchemeRegionNames);
	
	for (i = 0; i < eaSize(&g_eaUIRegions); i++)
	{
		ControlSchemeRegionData* pData = g_eaUIRegions[i];
		eaPush(&ppchSchemeRegionNames, pData->pchName);
	}

	autoSettingsAddComboBox(HUD_CATEGORY, "HUDRegion", 
							&ppchSchemeRegionNames, false, &s_iCurrentRegionDisplay,
							gclHUDOptions_RegionChangedCB, 
							gclHUDOptions_RegionCommitCB, 
							gclHUDOptions_RegionUpdateCB, 
							gclHUDOptions_RegionRevertCB, NULL, true);

	eaDestroy(&ppchSchemeRegionNames);
}

static void setupHudOptions(void) 
{
	Entity* pEnt = entActivePlayerPtr();
	bool disableMapChoicesOption = gConf.bNoUserMapInstanceChoice;

	if (!pEnt)
	{
		return;
	}

	s_iCurrentRegionDisplay = 0;

	if(!ppchShowOptionsList) {
		eaPush(&ppchShowOptionsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Mouse_Over", "[UNTRANSLATED]Mouse Over")));
		eaPush(&ppchShowOptionsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Targeted", "[UNTRANSLATED]Targeted")));
		eaPush(&ppchShowOptionsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Target_Of_Target", "[UNTRANSLATED]Target of Target")));
		eaPush(&ppchShowOptionsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Recently_Damaged", "[UNTRANSLATED]Recently Damaged")));
		eaPush(&ppchShowOptionsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Always", "[UNTRANSLATED]Always")));
	}

	if(!ppchShowOptionsListWithAlwaysShowContacts) {
		eaPush(&ppchShowOptionsListWithAlwaysShowContacts, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Mouse_Over", "[UNTRANSLATED]Mouse Over")));
		eaPush(&ppchShowOptionsListWithAlwaysShowContacts, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Targeted", "[UNTRANSLATED]Targeted")));
		eaPush(&ppchShowOptionsListWithAlwaysShowContacts, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Target_Of_Target", "[UNTRANSLATED]Target of Target")));
		eaPush(&ppchShowOptionsListWithAlwaysShowContacts, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Recently_Damaged", "[UNTRANSLATED]Recently Damaged")));
		eaPush(&ppchShowOptionsListWithAlwaysShowContacts, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Always_Contacts", "[UNTRANSLATED]Always Contacts")));
	}

	if(!ppchShowOptionsListWithoutTargeted) {
		eaPush(&ppchShowOptionsListWithoutTargeted, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Mouse_Over", "[UNTRANSLATED]Mouse Over")));
		eaPush(&ppchShowOptionsListWithoutTargeted, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Recently_Damaged", "[UNTRANSLATED]Recently Damaged")));
		eaPush(&ppchShowOptionsListWithoutTargeted, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Always", "[UNTRANSLATED]Always")));
	}

	if(!ppchShowOptionsListWithoutAlways) {
		eaPush(&ppchShowOptionsListWithoutAlways, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Mouse_Over", "[UNTRANSLATED]Mouse Over")));
		eaPush(&ppchShowOptionsListWithoutAlways, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Targeted", "[UNTRANSLATED]Targeted")));
		eaPush(&ppchShowOptionsListWithoutAlways, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Target_Of_Target", "[UNTRANSLATED]Target of Target")));
		eaPush(&ppchShowOptionsListWithoutAlways, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Recently_Damaged", "[UNTRANSLATED]Recently Damaged")));
	}

	if(!ppchShowReticlesAsList) {
		eaPush(&ppchShowReticlesAsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Show_Reticles_As_Highlight", "[UNTRANSLATED]Highlight")));
		eaPush(&ppchShowReticlesAsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Show_Reticles_As_Box", "[UNTRANSLATED]Box")));
		eaPush(&ppchShowReticlesAsList, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_Show_Reticles_As_Both", "[UNTRANSLATED]Both")));
	}

	if (!ppchNotifyAudioModes && gclNotifyAudioHasAnyEvents())
	{
		eaPush(&ppchNotifyAudioModes, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_NotifyAudioMode_Off", "[UNTRANSLATED]Off")));
		eaPush(&ppchNotifyAudioModes, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_NotifyAudioMode_Standard", "[UNTRANSLATED]Standard")));
		eaPush(&ppchNotifyAudioModes, allocAddCaseSensitiveString(TranslateMessageKeyDefault("HudOptions_NotifyAudioMode_Suggestion", "[UNTRANSLATED]Suggestion")));		
	}

	gclHUDOptions_InitRegionList();

	autoSettingsAddBit(HUD_CATEGORY, "ShowPlayerTitles", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowPlayerTitles, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	if(gConf.bShowPlayerRoleInfoInHUDOptions)
		autoSettingsAddBit(HUD_CATEGORY, "ShowPlayerRoles", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowPlayerRoles, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowDamageFloaters", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowDamageFloaters, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowPetDamageFloaters", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowPetDamageFloaters, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowTeamDamageFloaters", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowTeamDamageFloaters, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowAllPlayerDamageFloaters", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowAllPlayerDamageFloaters, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowUnrelatedDamageFloaters", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowUnrelatedDamageFloaters, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowHostileHealingFloaters", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowHostileHealingFloaters, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowOwnedEntityDamageFloaters", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowHostileHealingFloaters, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowInteractionIcons", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowInteractionIcons, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	autoSettingsAddBit(HUD_CATEGORY, "ShowPlayerPowerDisplayNames", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowPlayerPowerDisplayNames, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);

	if (gConf.bShowCriticalStatusInfoInHUDOptions)
	{
		autoSettingsAddBit(HUD_CATEGORY, "ShowCriticalStatus", (int*)&s_CurrentHUDOptions.ShowOverhead.bShowCriticalStatusInfo, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	}

	if ( disableMapChoicesOption )
	{
		// players with debug access level still get the option
		if ( pEnt->pPlayer && ( pEnt->pPlayer->accessLevel >= ACCESS_DEBUG ) )
		{
			disableMapChoicesOption = false;
		}
	}

	if ( !disableMapChoicesOption )
	{
		autoSettingsAddBit(HUD_CATEGORY, "ShowMapChoice", (int*)&s_bShowMapChoice, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	}

	autoSettingsAddComboBox(HUD_CATEGORY, "ShowReticlesAs", &ppchShowReticlesAsList, false, (int*)&s_iShowResticlesAsIndex, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);

	if(gclNotifyAudioHasAnyEvents()) {
		autoSettingsAddComboBox(HUD_CATEGORY, "NotifyAudioMode", &ppchNotifyAudioModes, false, (int*)&s_CurrentHUDOptions.eNotifyAudioMode, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	}
	
	autoSettingsAddBit(HUD_CATEGORY, "HideTrayTooltips", (int*)&s_CurrentHUDOptions.bHideTrayTooltips, true, hudSettingsChangedCallback, hudSettingsComittedCallback, hudSettingsUpdateCallback, hudSettingsRestoreDefaultsCallback, NULL, true);
	
	gameSpecific_HUDOptions_Init(HUD_CATEGORY);
	addAdvancedOptions(true);
}

void gclHUDOptionsEnable(void) {
	if (gConf.bDoNotShowHUDOptions)
		return;

	OptionCategoryAdd(HUD_CATEGORY);

	setupHudOptions();
}

void gclHUDOptionsDisable(void) {
	if (gConf.bDoNotShowHUDOptions)
		return;

	OptionCategoryDestroy(HUD_CATEGORY);
}

AUTO_RUN;
void gclHudOptions_AutoRegister(void)
{
	ui_GenInitIntVar("OVERHEAD_SHOW_PART_NAME", 0);
	ui_GenInitIntVar("OVERHEAD_SHOW_PART_LIFE", 1);
	ui_GenInitIntVar("OVERHEAD_SHOW_PART_RETICLE", 2);

#define CHECK_ARRAY(array, value) assert(array[value].checkValue == value)

	// Validate arrays
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_ENEMY);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_FRIENDLY_NPC);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_FRIEND);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_SUPERGROUP);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_TEAM);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_PET);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_PLAYER);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_ENEMY_PLAYER);
	CHECK_ARRAY(s_EntityTypeList, OVERHEAD_ENTITY_TYPE_SELF);

	CHECK_ARRAY(s_OverheadList, OVERHEAD_ENTITY_OPTION_NAME);
	CHECK_ARRAY(s_OverheadList, OVERHEAD_ENTITY_OPTION_LIFE);
	CHECK_ARRAY(s_OverheadList, OVERHEAD_ENTITY_OPTION_RETICLE);
	CHECK_ARRAY(s_OverheadList, OVERHEAD_ENTITY_OPTION_POWERMODENAME);
	CHECK_ARRAY(s_OverheadList, OVERHEAD_ENTITY_OPTION_POWERMODELIFE);
	CHECK_ARRAY(s_OverheadList, OVERHEAD_ENTITY_OPTION_POWERMODERETICLE);
}
