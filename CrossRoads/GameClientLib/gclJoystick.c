#include "StringCache.h"
#include "textparser.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ESet.h"
#include "Expression.h"
#include "GameStringFormat.h"
#include "GlobalTypes.h"
#include "StringUtil.h"
#include "sysutil.h"

#include "GameClientLib.h"
#include "inputLib.h"
#include "inputData.h"
#include "inputJoystick.h"
#include "inputGamepad.h"

#include "Entity.h"
#include "gclEntity.h"
#include "gclKeyBind.h"
#include "gclOptions.h"
#include "Player.h"
#include "RegionRules.h"
#include "UIGen.h"
#include "WorldLibEnums.h"
#include "Prefs.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define JOYSTICK_DEFAULT_FILENAME "controller.cfg"
#define JOYSTICK_ENABLED_PREF "JoystickEnabled"
#define JOYSTICK_CRASH_PREF "JoystickCrash"
#define GAMEPAD_ENABLED_PREF "XboxGamepadEnabled"

#define JOYSTICK_ACCEPTABLE_CRASHES 2
#define JOYSTICK_CRASH_VERSION 1
#define JOYSTICK_CRASH_VERSION_PREF "JoystickCrashVersion"

typedef struct JoystickConfig JoystickConfig;

AUTO_STRUCT;
typedef struct JoystickLogicalIdentifier {
	InputJoystickLogical		eIdentifier;	AST(REQUIRED STRUCTPARAM NAME(Input))
	REF_TO(Message)				hDisplayName;	AST(REQUIRED NON_NULL_REF STRUCTPARAM NAME(DisplayName))
} JoystickLogicalIdentifier;

AUTO_STRUCT;
typedef struct JoystickConfig {
	REF_TO(Message)				hDisplayName;	AST(NAME(DisplayName) NON_NULL_REF)
	JoystickLogicalIdentifier	**eaIdentifier;	AST(NAME(Input))
	JoystickConfig				**eaCategory;	AST(NAME(Category))
} JoystickConfig;

AUTO_STRUCT;
typedef struct JoystickMapping {
	const char					*pchIdentifier;	AST(NAME(Identifier) POOL_STRING)
	const char					*pchName;		AST(NAME(Name))
	InputJoystickPhysical		ePhysical;		AST(NAME(Input))
} JoystickMapping;

AUTO_STRUCT;
typedef struct JoystickMappingEntry {
	REF_TO(Message)				hCategoryName;	AST(NAME(CategoryName) NON_NULL_REF)
	JoystickLogicalIdentifier	*pDef;			AST(UNOWNED)
	JoystickMapping				Primary;
	JoystickMapping				Secondary;
	bool						bIsCategory;
} JoystickMappingEntry;

extern ParseTable parse_JoystickConfig[];
#define TYPE_parse_JoystickConfig JoystickConfig
extern ParseTable parse_JoystickMapping[];
#define TYPE_parse_JoystickMapping JoystickMapping
extern ParseTable parse_JoystickMappingEntry[];
#define TYPE_parse_JoystickMappingEntry JoystickMappingEntry

static InputJoystickProfile		s_JoystickProfile;
static JoystickConfig			s_Config;
static JoystickMappingEntry		**s_eaSettings;
static S32						s_iCrashCount;

// TODO(jm): Move this into a data file somewhere
static InputJoystickMapping s_aDefaultMapping[] =
{
	{ kInputJoystickLogical_AB, kInputJoystickPhysical_Button0, NULL },
	{ kInputJoystickLogical_BB, kInputJoystickPhysical_Button1, NULL },
	{ kInputJoystickLogical_XB, kInputJoystickPhysical_Button2, NULL },
	{ kInputJoystickLogical_YB, kInputJoystickPhysical_Button3, NULL },
	{ kInputJoystickLogical_LB, kInputJoystickPhysical_Button4, NULL },
	{ kInputJoystickLogical_RB, kInputJoystickPhysical_Button5, NULL },
	{ kInputJoystickLogical_LeftTrigger, kInputJoystickPhysical_Button6, NULL },
	{ kInputJoystickLogical_RightTrigger, kInputJoystickPhysical_Button7, NULL },
	{ kInputJoystickLogical_Select, kInputJoystickPhysical_Button8, NULL },
	{ kInputJoystickLogical_Start, kInputJoystickPhysical_Button9, NULL },
	{ kInputJoystickLogical_LStick, kInputJoystickPhysical_Button10, NULL },
	{ kInputJoystickLogical_RStick, kInputJoystickPhysical_Button11, NULL },

	{ kInputJoystickLogical_JoypadUp, kInputJoystickPhysical_PovUp0, NULL },
	{ kInputJoystickLogical_JoypadDown, kInputJoystickPhysical_PovDown0, NULL },
	{ kInputJoystickLogical_JoypadLeft, kInputJoystickPhysical_PovLeft0, NULL },
	{ kInputJoystickLogical_JoypadRight, kInputJoystickPhysical_PovRight0, NULL },

	{ kInputJoystickLogical_MovementX, kInputJoystickPhysical_X, NULL },
	{ kInputJoystickLogical_MovementY, kInputJoystickPhysical_Y, NULL },
	{ kInputJoystickLogical_CameraX, kInputJoystickPhysical_Z, NULL },
	{ kInputJoystickLogical_CameraY, kInputJoystickPhysical_Rz, NULL },
};

static S32 gclJoystickGetCrashCount(void)
{
	S32 iCrashCount = GamePrefGetInt(JOYSTICK_CRASH_PREF, 0);
	S32 iCrashVersion = GamePrefGetInt(JOYSTICK_CRASH_VERSION_PREF, JOYSTICK_CRASH_VERSION);
	// Make it so the crash count can be updated.
	if (iCrashVersion != JOYSTICK_CRASH_VERSION)
		iCrashCount = 0;
	return iCrashCount;
}

AUTO_COMMAND ACMD_CMDLINE ACMD_HIDE ACMD_ACCESSLEVEL(0);
void gclJoystickResetCrashes(void)
{
	if (gclJoystickGetCrashCount() > 0)
	{
		// Reset the crash settings
		GamePrefStoreInt(JOYSTICK_CRASH_PREF, 0);
		GamePrefStoreInt(JOYSTICK_CRASH_VERSION_PREF, JOYSTICK_CRASH_VERSION);
	}
}

static bool gclJoystickHandleCrash(void)
{
	// Never absorb the crash while in dev mode
	if (isDevelopmentMode() > 0)
		return false;

	// Turn off the joystick
	joystickSetEnabled(false);
	GamePrefStoreInt(JOYSTICK_ENABLED_PREF, joystickGetEnabled());

	// Increment the global counter
	s_iCrashCount = gclJoystickGetCrashCount() + 1;
	GamePrefStoreInt(JOYSTICK_CRASH_PREF, s_iCrashCount);
	GamePrefStoreInt(JOYSTICK_CRASH_VERSION_PREF, JOYSTICK_CRASH_VERSION);

	if (s_iCrashCount >= JOYSTICK_ACCEPTABLE_CRASHES)
	{
		// First crash should attempt recovery, subsequent crashes should
		// never attempt recovery.
		return false;
	}

	return true;
}

static void gclJoystickUpdateSettings(void)
{
	int i, j;

	for (i = 0; i < eaSize(&s_eaSettings); i++)
	{
		JoystickMappingEntry *pEntry = s_eaSettings[i];
		if (pEntry->bIsCategory || !pEntry->pDef)
			continue;

		ZeroStruct(&pEntry->Primary);
		ZeroStruct(&pEntry->Secondary);
		for (j = 0; j < eaSize(&s_JoystickProfile.eaMapping); j++)
		{
			InputJoystickMapping *pMapping = s_JoystickProfile.eaMapping[j];
			if (pMapping->eLogicalOutput == pEntry->pDef->eIdentifier)
			{
				JoystickMapping *pFill = NULL;
				if (pEntry->Primary.ePhysical == kInputJoystickPhysical_None)
					pFill = &pEntry->Primary;
				else if (pEntry->Secondary.ePhysical == kInputJoystickPhysical_None)
					pFill = &pEntry->Secondary;

				if (pFill)
				{
					pFill->ePhysical = pMapping->ePhysicalInput;
					pFill->pchIdentifier = NULL_TO_EMPTY(pMapping->pchGuidDevice);
					pFill->pchName = joystickGetName(pMapping->pchGuidDevice);
				}
			}
		}
	}
}

static void gclJoystickEnabledChanged(OptionSetting *setting)
{
	if (!stricmp(setting->pchName, "JoystickEnabled"))
	{
		joystickSetEnabled(!!setting->iIntValue);
		GamePrefStoreInt(JOYSTICK_ENABLED_PREF, joystickGetEnabled());
	}
	else if (!stricmp(setting->pchName, "XboxGamepadEnabled"))
	{
		gamepadSetEnabled(!!setting->iIntValue);
		GamePrefStoreInt(GAMEPAD_ENABLED_PREF, gamepadGetEnabled());
	}
}

static void gclJoystickEnabledUpdate(OptionSetting *setting)
{
	if (!stricmp(setting->pchName, "JoystickEnabled"))
	{
		setting->iIntValue = joystickGetEnabled();
	}
	else if (!stricmp(setting->pchName, "XboxGamepadEnabled"))
	{
		setting->iIntValue = gamepadGetEnabled();
	}
}

static void gclJoystickEnableReset(OptionSetting *setting)
{
	if (!stricmp(setting->pchName, "JoystickEnabled"))
	{
		setting->iIntValue = false;
		joystickSetEnabled(!!setting->iIntValue);
		GamePrefStoreInt(JOYSTICK_ENABLED_PREF, joystickGetEnabled());
	}
	else if (!stricmp(setting->pchName, "XboxGamepadEnabled"))
	{
		setting->iIntValue = true;
		gamepadSetEnabled(!!setting->iIntValue);
		GamePrefStoreInt(GAMEPAD_ENABLED_PREF, gamepadGetEnabled());
	}
}

static bool gclJoystickSetProfile(InputJoystickProfile *pProfile, S32 iSave, const char *pchIdentifier, InputJoystickPhysical ePhysicalInput, InputJoystickLogical eLogicalOutput)
{
	const char *pchGuidDevice = NULL;
	InputJoystickMapping *pMapping;
	int i;
	int iRemove = -1;

	if (!pProfile)
		return false;
	if (pchIdentifier && *pchIdentifier)
		// The device has to have been attached at one point for
		// remapping to actually make sense.
		pchGuidDevice = allocFindString(pchIdentifier);
	if (pchIdentifier && *pchIdentifier && !pchGuidDevice)
		return false;
	if (iSave < 0 || iSave >= 2)
		return false;

	for (i = 0; i < eaSize(&pProfile->eaMapping); i++)
	{
		pMapping = pProfile->eaMapping[i];

		if ((!pMapping->pchGuidDevice || pchGuidDevice && !stricmp(pchGuidDevice, pMapping->pchGuidDevice)) && pMapping->ePhysicalInput == ePhysicalInput)
		{
			iRemove = i;
			break;
		}
	}

	for (i = 0; i < eaSize(&pProfile->eaMapping); i++)
	{
		pMapping = pProfile->eaMapping[i];

		if (pMapping->eLogicalOutput != eLogicalOutput)
			continue;

		if (iSave > 0)
		{
			iSave--;
			continue;
		}

		// Update this slot
		if (ePhysicalInput != kInputJoystickPhysical_None)
		{
			pMapping->pchGuidDevice = pchGuidDevice;
			pMapping->ePhysicalInput = ePhysicalInput;
		}
		else
		{
			iRemove = i;
		}
		iSave = -1;
		break;
	}

	if (ePhysicalInput != kInputJoystickPhysical_None && iSave >= 0)
	{
		// Add new slot
		pMapping = StructCreate(parse_InputJoystickMapping);
		pMapping->eLogicalOutput = eLogicalOutput;
		pMapping->pchGuidDevice = pchGuidDevice;
		pMapping->ePhysicalInput = ePhysicalInput;
		eaPush(&pProfile->eaMapping, pMapping);
	}

	if (iRemove >= 0 && iRemove < eaSize(&pProfile->eaMapping))
	{
		// Remove existing bind
		StructDestroy(parse_InputJoystickMapping, eaRemove(&pProfile->eaMapping, iRemove));
	}

	return true;
}

static void gclJoystickLoadLocalProfile(InputJoystickProfile *pProfile, const char *pchFilename)
{
	FILE *pFile;
	char achBuf[1000];

	PERFINFO_AUTO_START_FUNC();

	pFile = fileOpen(pchFilename, "rt");
	if (!pFile)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	while (fgets(achBuf, sizeof(achBuf), pFile))
	{
		char *pchContext = achBuf;
		char *pchLogicalOutput;
		char *pchPhysicalInput;
		char *pchIdentifier;
		InputJoystickLogical eLogicalOutput;
		InputJoystickPhysical ePhysicalInput;

		while (strchr(" \r\n\t", *pchContext))
			pchContext++;
		if (strStartsWith(pchContext, "#") ||
			strStartsWith(pchContext, ";") ||
			strStartsWith(pchContext, "//"))
			continue;

		pchLogicalOutput = strtok_r(NULL, " \r\n\t", &pchContext);
		pchPhysicalInput = strtok_r(NULL, " \r\n\t", &pchContext);
		pchIdentifier = strtok_r(NULL, " \r\n\t", &pchContext);
		eLogicalOutput = pchLogicalOutput && *pchLogicalOutput ? StaticDefineIntGetInt(InputJoystickLogicalEnum, pchLogicalOutput) : -1;
		ePhysicalInput = pchPhysicalInput && *pchPhysicalInput ? StaticDefineIntGetInt(InputJoystickPhysicalEnum, pchPhysicalInput) : -1;

		if (eLogicalOutput > 0 && ePhysicalInput > 0)
		{
			const char *pchGuidDevice = NULL;
			InputJoystickMapping *pMapping;
			if (pchIdentifier && *pchIdentifier)
				pchGuidDevice = allocAddString(pchGuidDevice);
			pMapping = StructCreate(parse_InputJoystickMapping);
			pMapping->eLogicalOutput = eLogicalOutput;
			pMapping->ePhysicalInput = ePhysicalInput;
			pMapping->pchGuidDevice = pchGuidDevice;
			eaPush(&pProfile->eaMapping, pMapping);
		}
	}

	fclose(pFile);

	PERFINFO_AUTO_STOP();
}

static void gclJoystickLoadUserProfile(const char *pchFilename)
{
	char achFullFilename[CRYPTIC_MAX_PATH];
	char achFolder[CRYPTIC_MAX_PATH];

	if (!pchFilename)
		pchFilename = JOYSTICK_DEFAULT_FILENAME;

	strcpy(achFullFilename, pchFilename);
	if (!isFullPath(achFullFilename))
	{
		if(isDevelopmentMode())
			sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
		else
			sprintf(achFullFilename, "%s/%s", getExecutableDir(achFolder), pchFilename);
	}

	eaClearStruct(&s_JoystickProfile.eaMapping, parse_InputJoystickMapping);
	gclJoystickLoadLocalProfile(&s_JoystickProfile, achFullFilename);
	gclJoystickUpdateSettings();
	joystickSetProfile(&s_JoystickProfile);
}

static void gclJoystickSaveLocalProfile(InputJoystickProfile *pProfile, const char *pchFilename)
{
	FILE *pFile;
	S32 i;

	PERFINFO_AUTO_START_FUNC();

	makeDirectoriesForFile(pchFilename);
	pFile = fileOpen(pchFilename, "wt");

	if (!pFile)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	for (i = 0; i < eaSize(&pProfile->eaMapping); i++)
	{
		InputJoystickMapping *pMapping = pProfile->eaMapping[i];
		if (pMapping->pchGuidDevice)
		{
			fprintf(pFile, "%s %s %s\n",
				StaticDefineIntRevLookupNonNull(InputJoystickLogicalEnum, pMapping->eLogicalOutput),
				StaticDefineIntRevLookupNonNull(InputJoystickPhysicalEnum, pMapping->ePhysicalInput),
				pMapping->pchGuidDevice
				);
		}
		else
		{
			fprintf(pFile, "%s %s\n",
				StaticDefineIntRevLookupNonNull(InputJoystickLogicalEnum, pMapping->eLogicalOutput),
				StaticDefineIntRevLookupNonNull(InputJoystickPhysicalEnum, pMapping->ePhysicalInput)
				);
		}
	}

	fclose(pFile);

	PERFINFO_AUTO_STOP();
}

static void gclJoystickSaveUserProfile(const char *pchFilename)
{
	char achFullFilename[CRYPTIC_MAX_PATH];
	char achFolder[CRYPTIC_MAX_PATH];

	if (!pchFilename)
		pchFilename = JOYSTICK_DEFAULT_FILENAME;

	strcpy(achFullFilename, pchFilename);
	if (!isFullPath(achFullFilename))
	{
		if(isDevelopmentMode())
			sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
		else
			sprintf(achFullFilename, "%s/%s", getExecutableDir(achFolder), pchFilename);
	}

	gclJoystickSaveLocalProfile(&s_JoystickProfile, achFullFilename);
}

static void gclJoystickResetToDefaults(void)
{
	int i;
	eaSetSizeStruct(&s_JoystickProfile.eaMapping, parse_InputJoystickMapping, ARRAY_SIZE(s_aDefaultMapping));
	for (i = eaSize(&s_JoystickProfile.eaMapping) - 1; i >= 0; i--)
	{
		s_JoystickProfile.eaMapping[i]->eLogicalOutput = s_aDefaultMapping[i].eLogicalOutput;
		s_JoystickProfile.eaMapping[i]->ePhysicalInput = s_aDefaultMapping[i].ePhysicalInput;
		s_JoystickProfile.eaMapping[i]->pchGuidDevice = s_aDefaultMapping[i].pchGuidDevice && *s_aDefaultMapping[i].pchGuidDevice ? allocAddString(s_aDefaultMapping[i].pchGuidDevice) : NULL;
	}
	gclJoystickUpdateSettings();
	joystickSetProfile(&s_JoystickProfile);
}

static void JoystickConfigReload(const char *pchPath, S32 iWhen)
{
	int i, j, used;

	loadstart_printf("Loading Joystick mappings... ");

	if (pchPath)
		fileWaitForExclusiveAccess(pchPath);
	StructDeInit(parse_JoystickConfig, &s_Config);
	ParserLoadFiles(NULL, "ui/JoystickMapping.def", "JoystickMapping.bin", PARSER_OPTIONALFLAG, parse_JoystickConfig, &s_Config);

	used = 0;
	for (i = 0; i < eaSize(&s_Config.eaCategory); i++)
	{
		JoystickMappingEntry *pEntry = eaGetStruct(&s_eaSettings, parse_JoystickMappingEntry, used++);
		JoystickConfig *pConfig = s_Config.eaCategory[i];
		COPY_HANDLE(pEntry->hCategoryName, pConfig->hDisplayName);
		pEntry->pDef = NULL;
		pEntry->bIsCategory = true;
		for (j = 0; j < eaSize(&pConfig->eaIdentifier); j++)
		{
			pEntry = eaGetStruct(&s_eaSettings, parse_JoystickMappingEntry, used++);
			REMOVE_HANDLE(pEntry->hCategoryName);
			pEntry->pDef = pConfig->eaIdentifier[j];
			pEntry->bIsCategory = false;
		}
	}
	eaSetSizeStruct(&s_eaSettings, parse_JoystickMappingEntry, used);

	gclJoystickUpdateSettings();

	loadend_printf("Done. (%d)", eaSize(&s_Config.eaCategory));
}

AUTO_STARTUP(Joystick) ASTRT_DEPS(BasicOptions, AS_Messages);
void gclJoystickInit(void)
{
	const char *pchMessageFail;
	OptionSetting *pJoystickSetting;

	if (gConf.bDoNotInitJoystick)
		return;

	JoystickConfigReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/JoystickMapping.def", JoystickConfigReload);

	if (pchMessageFail = StaticDefineVerifyMessages(InputJoystickLogicalEnum))
		Errorf("Not all InputJoystickLogical messages were found: %s", pchMessageFail);
	if (pchMessageFail = StaticDefineVerifyMessages(InputJoystickPhysicalEnum))
		Errorf("Not all InputJoystickPhysical messages were found: %s", pchMessageFail);

	joystickSetEnabled(!!GamePrefGetInt(JOYSTICK_ENABLED_PREF, false));
	gamepadSetEnabled(!!GamePrefGetInt(GAMEPAD_ENABLED_PREF, true));

	OptionCategoryAdd("Joystick");
	pJoystickSetting = OptionSettingAddBool("Joystick", "JoystickEnabled", joystickGetEnabled(), gclJoystickEnabledChanged, gclJoystickEnabledChanged, gclJoystickEnabledUpdate, gclJoystickEnableReset, NULL, NULL);
	OptionSettingAddBool("Joystick", "XboxGamepadEnabled", gamepadGetEnabled(), gclJoystickEnabledChanged, gclJoystickEnabledChanged, gclJoystickEnabledUpdate, gclJoystickEnableReset, NULL, NULL);

	// Disable the joystick if it's being crashy
	if (pJoystickSetting && gclJoystickGetCrashCount() >= JOYSTICK_ACCEPTABLE_CRASHES)
	{
		pJoystickSetting->bEnabled = false;
	}

	ui_GenInitStaticDefineVars(InputJoystickLogicalEnum, "LogicalJoystickInput_");
	ui_GenInitStaticDefineVars(InputJoystickPhysicalEnum, "JoystickInput_");

	gclJoystickLoadUserProfile(NULL);
	if (!eaSize(&s_JoystickProfile.eaMapping))
		gclJoystickResetToDefaults();
}

//////////////////////////////////////////////////////////////////////////////
// Commands

AUTO_COMMAND ACMD_NAME(joystick_save_file) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard) ACMD_PRODUCTS(FightClub, StarTrek);
void gclCmdJoystickSaveFile(const char *pchFilename)
{
	gclJoystickSaveUserProfile(pchFilename);
}

AUTO_COMMAND ACMD_NAME(joystick_load_file) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard) ACMD_PRODUCTS(FightClub, StarTrek);
void gclCmdJoystickLoadFile(const char *pchFilename)
{
	gclJoystickLoadUserProfile(pchFilename);
	joystickSetProfile(&s_JoystickProfile);
}

AUTO_COMMAND ACMD_NAME(joystick_save) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard) ACMD_PRODUCTS(FightClub, StarTrek);
void gclCmdJoystickSave(void)
{
	gclJoystickSaveUserProfile(NULL);
}

AUTO_COMMAND ACMD_NAME(joystick_load) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard) ACMD_PRODUCTS(FightClub, StarTrek);
void gclCmdJoystickLoad(void)
{
	gclJoystickLoadUserProfile(NULL);
	joystickSetProfile(&s_JoystickProfile);
}

//////////////////////////////////////////////////////////////////////////////
// Expressions

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickEnable);
void gclExprJoystickEnable(bool bEnable)
{
	joystickSetEnabled(bEnable);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickGetEnabled);
bool gclExprJoystickGetEnabled(void)
{
	return joystickGetEnabled();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GamepadEnable);
void gclExprGamepadEnable(bool bEnable)
{
	gamepadSetEnabled(bEnable);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GamepadGetEnabled);
bool gclExprGamepadGetEnabled(void)
{
	return gamepadGetEnabled();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickGetSettings);
void gclExprJoystickGetSettings(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_eaSettings, parse_JoystickMappingEntry);
}

static InputJoystickLogical s_eMapping;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickBeginRemap);
bool gclExprJoystickBeginRemap(U32 eMapping)
{
	s_eMapping = eMapping;
	if (s_eMapping != kInputJoystickLogical_None)
	{
		joystickSetProcessingEnabled(false);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickIsRemapping);
bool gclExprJoystickIsRemapping(void)
{
	return s_eMapping != kInputJoystickLogical_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickIsRemapInputActive);
bool gclExprJoystickIsRemapInputActive(void)
{
	if (s_eMapping != kInputJoystickLogical_None)
		return joystickGetActiveInput(NULL, NULL);
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickGetActiveRemapName);
const char *gclExprJoystickGetActiveRemapInputName(void)
{
	const char *pchName = "";
	const char *pchIdentifier = NULL;
	if (s_eMapping != kInputJoystickLogical_None)
	{
		if (joystickGetActiveInput(&pchIdentifier, NULL))
		{
			pchName = joystickGetName(pchIdentifier);
		}
	}
	return pchName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickGetActiveRemapInput);
U32 gclExprJoystickGetActiveRemapInput(void)
{
	InputJoystickPhysical eInput = kInputJoystickPhysical_None;
	if (s_eMapping != kInputJoystickLogical_None)
	{
		joystickGetActiveInput(NULL, &eInput);
	}
	return eInput;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickGetRemapInput);
U32 gclExprJoystickGetRemapInput(const char *pchIdentifier)
{
	InputJoystickPhysical eInput = kInputJoystickPhysical_None;
	if (s_eMapping != kInputJoystickLogical_None)
	{
		const char *pchIdent = pchIdentifier && *pchIdentifier ? pchIdentifier : NULL;
		joystickGetActiveInput(&pchIdent, &eInput);
	}
	return eInput;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickUnsetRemap);
bool gclExprJoystickUnsetRemap(S32 iSave)
{
	if (s_eMapping != kInputJoystickLogical_None)
	{
		InputJoystickLogical eLogicalOutput = s_eMapping;

		s_eMapping = kInputJoystickLogical_None;
		joystickSetProcessingEnabled(true);

		if (!gclJoystickSetProfile(&s_JoystickProfile, iSave, NULL, kInputJoystickPhysical_None, eLogicalOutput))
			return false;

		gclJoystickSaveUserProfile(NULL);
		gclJoystickUpdateSettings();
		joystickSetProfile(&s_JoystickProfile);
		return true;
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickSaveRemap);
bool gclExprJoystickSaveRemap(const char *pchIdentifier, U32 eInput, S32 iSave)
{
	if (s_eMapping != kInputJoystickLogical_None)
	{
		InputJoystickLogical eLogicalOutput = s_eMapping;

		s_eMapping = kInputJoystickLogical_None;
		joystickSetProcessingEnabled(true);

		if (!gclJoystickSetProfile(&s_JoystickProfile, iSave, pchIdentifier, eInput, eLogicalOutput))
			return false;

		gclJoystickSaveUserProfile(NULL);
		gclJoystickUpdateSettings();
		joystickSetProfile(&s_JoystickProfile);
		return true;
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickEndActiveRemap);
bool gclExprJoystickEndActiveRemap(S32 iSave)
{
	if (s_eMapping != kInputJoystickLogical_None)
	{
		if (iSave >= 0)
		{
			const char *pchIdentifier = NULL;
			InputJoystickPhysical eInput;
			if (!joystickGetActiveInput(&pchIdentifier, &eInput))
			{
				s_eMapping = kInputJoystickLogical_None;
				joystickSetProcessingEnabled(true);
				return false;
			}

			// TODO: Maybe enable storing the current joystick
			return gclExprJoystickSaveRemap(NULL, eInput, iSave);
		}

		s_eMapping = kInputJoystickLogical_None;
		joystickSetProcessingEnabled(true);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickEndRemap);
bool gclExprJoystickEndRemap(const char *pchIdentifier, S32 iSave)
{
	if (s_eMapping != kInputJoystickLogical_None)
	{
		if (iSave >= 0)
		{
			const char *pchIdent = pchIdentifier && *pchIdentifier ? pchIdentifier : NULL;
			InputJoystickPhysical eInput;
			if (!joystickGetActiveInput(&pchIdent, &eInput))
			{
				s_eMapping = kInputJoystickLogical_None;
				joystickSetProcessingEnabled(true);
				return false;
			}

			// TODO: Maybe enable storing the current joystick
			return gclExprJoystickSaveRemap(NULL, eInput, iSave);
		}

		s_eMapping = kInputJoystickLogical_None;
		joystickSetProcessingEnabled(true);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickResetRemap);
void gclExprJoystickResetRemap(void)
{
	gclJoystickResetToDefaults();
	gclJoystickSaveUserProfile(NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickLogicalState);
bool gclExprJoystickLogicalState(U32 eLogicalInput)
{
	return joystickLogicalState(eLogicalInput);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickLogicalValue);
float gclExprJoystickLogicalValue(U32 eLogicalInput)
{
	return joystickLogicalValue(eLogicalInput);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(JoystickLogicalIsValue);
bool gclExprJoystickLogicalIsValue(U32 eLogicalInput)
{
	return joystickLogicalIsAnalog(eLogicalInput);
}

#include "AutoGen/gclJoystick_c_ast.c"
