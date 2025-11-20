#include "estring.h"
#include "Message.h"
#include "StringCache.h"
#include "Expression.h"
#include "UIGen.h"

#include "gclOptions.h"
#include "gclOptions_h_ast.h"
#include "gclGraphicsOptions.h"
#include "GameClientLib.h"
#include "WorldGrid.h"
#include "wlState.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static OptionCategory **s_eaOptionSettingCategories = NULL;
static StashTable s_stOptionSettingCategoryNames = NULL;
static int s_bShowHidden = 0;
bool g_bOptionsInit = false;

AUTO_RUN;
void OptionsRegister(void)
{
	ui_GenInitStaticDefineVars(OptionSettingTypeEnum, "OptionSetting");
}

void OptionCategoryMoveToPosition(const char* pcName, int iPos)
{
	OptionCategory* pCat = OptionCategoryGet(pcName);
	eaFindAndRemove(&s_eaOptionSettingCategories, pCat);
	eaInsert(&s_eaOptionSettingCategories, pCat, iPos);
}

void OptionSettingChanged(OptionSetting *pSetting, bool bCallback)
{
	if (!pSetting) return;

	switch (pSetting->eType)
	{
	case kOptionSettingBool:
	case kOptionSettingCheckbox:
		estrPrintf(&pSetting->pchStringValue, "%d", pSetting->iIntValue);
		break;
	case kOptionSettingFloatSlider:
		estrPrintf(&pSetting->pchStringValue, "%1.2f", pSetting->fFloatValue);
		break;
	case kOptionSettingIntSlider:
		estrPrintf(&pSetting->pchStringValue, "%d", pSetting->iIntValue);
		break;
	case kOptionSettingComboBox:
		{
			OptionSettingComboChoice *pChoice = eaGet(&pSetting->eaComboBoxOptions, pSetting->iIntValue);
			pSetting->pComboChoice = pChoice;
			estrPrintf(&pSetting->pchStringValue, "%s", pChoice ? TranslateMessageRef(pChoice->hDisplayName) : NULL);
			break;
		}
	case kOptionSettingCheckComboBox:
		{
			int i;
			OptionSettingComboChoice *pChoice = eaGet(&pSetting->eaComboBoxOptions, pSetting->iIntValue);
			pSetting->pComboChoice = pChoice;
			estrClear(&pSetting->pchStringValue);
			for(i = 0; i < eaSize(&pSetting->eaComboBoxOptions); i++) {
				if(pSetting->iIntValue & 1 << i) {
					const char *pchName = TranslateMessageRef(pSetting->eaComboBoxOptions[i]->hDisplayName);
					if(estrLength(&pSetting->pchStringValue) == 0) {
						estrConcatf(&pSetting->pchStringValue, "%s", pchName);
					} else {
						estrConcatf(&pSetting->pchStringValue, ", %s", pchName);
					}
				}
			}
			if(estrLength(&pSetting->pchStringValue) == 0) {
				estrPrintf(&pSetting->pchStringValue, "%s", pSetting->pchComboDefault);
			}
			break;
		}
	}

	if (pSetting->cbDisplay)
		pSetting->cbDisplay(pSetting);

	if (bCallback && pSetting->cbChanged)
		pSetting->cbChanged(pSetting);
}

void OptionSettingConfirm(OptionSetting *pSetting)
{
	pSetting->iOrigIntValue = pSetting->iIntValue;
	if (pSetting->cbCommitted)
		pSetting->cbCommitted(pSetting);
}

void OptionsReload(void)
{
	int i, j;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *cat = s_eaOptionSettingCategories[i];
		for (j = 0; j < eaSize(&cat->eaSettings); ++j)
		{
			OptionSetting *setting = cat->eaSettings[j];
			if (setting->cbUpdate)
			{
				setting->cbUpdate(setting);
				OptionSettingChanged(setting, false);
			}
		}
	}
}

// Show the options screen.
AUTO_COMMAND ACMD_NAME(Options) ACMD_ACCESSLEVEL(0);
void OptionsMenu(void)
{
	UIGen *pOptions = ui_GenFind("Options", kUIGenTypeNone);
	int i, j;
	g_bOptionsInit = true;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *cat = s_eaOptionSettingCategories[i];
		for (j = 0; j < eaSize(&cat->eaSettings); ++j)
		{
			OptionSetting *setting = cat->eaSettings[j];
			if (setting->cbUpdate)
			{
				setting->cbUpdate(setting);
			}
			setting->iOrigIntValue = setting->iIntValue;
			OptionSettingChanged(setting, false);
		}
	}
	g_bOptionsInit = false;

	ui_GenAddWindow(pOptions);
}

OptionCategory *OptionCategoryGet(const char *pchName)
{
	OptionCategory *pCategory = NULL;

	if (!s_stOptionSettingCategoryNames)
		s_stOptionSettingCategoryNames = stashTableCreateWithStringKeys(32, 0);
	if (!stashFindPointer(s_stOptionSettingCategoryNames, pchName, &pCategory))
		return NULL;
	return pCategory;
}

OptionCategory *OptionCategoryAdd(const char *pchName)
{
	OptionCategory *pCategory = OptionCategoryGet(pchName);
	if (!pCategory)
	{
		char pchDisplayNameRef[1024];
		pCategory = StructCreate(parse_OptionCategory);
		pCategory->pchName = allocAddString(pchName);
		eaPush(&s_eaOptionSettingCategories, pCategory);

		if (!s_stOptionSettingCategoryNames)
			s_stOptionSettingCategoryNames = stashTableCreateWithStringKeys(32, 0);
		stashAddPointer(s_stOptionSettingCategoryNames, pCategory->pchName, pCategory, 0);
		sprintf(pchDisplayNameRef, "Options_Category_%s", pchName);
		SET_HANDLE_FROM_STRING("Message", pchDisplayNameRef, pCategory->hDisplayName);
	}
	return pCategory;
}

bool OptionCategoryDestroy(const char *pchName)
{
	OptionCategory *pCategory;

	if (s_stOptionSettingCategoryNames && stashRemovePointer(s_stOptionSettingCategoryNames, pchName, &pCategory))
	{
		eaFindAndRemove(&s_eaOptionSettingCategories, pCategory);
		StructDestroy(parse_OptionCategory, pCategory);
		return true;
	}
	return false;
}

static OptionSetting *OptionSettingCreate(const char *categoryName, const char *text, OptionSettingType type,
						OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, OptionSettingCallback cbInput, UserData userData)
{
	OptionCategory *cat = OptionCategoryGet(categoryName);
	OptionSetting *ret = StructCreate(parse_OptionSetting);
	char pchDisplayNameRef[1024];
	if (!cat)
		cat = OptionCategoryAdd(categoryName);
	ret->pchName = allocAddString(text);
	ret->eType = type;
	ret->cbChanged = cbChanged;
	ret->cbCommitted = cbCommitted;
	ret->cbUpdate = cbUpdate;
	ret->cbRestoreDefaults = cbRestoreDefaults;
	ret->cbDisplay = cbDisplay;
	ret->cbInput = cbInput;
	ret->pData = userData;
	ret->bEnabled = true;
	sprintf(pchDisplayNameRef, "Options_Setting_%s_%s", categoryName, text);
	if (RefSystem_ReferentFromString("Message", pchDisplayNameRef)) {
		SET_HANDLE_FROM_STRING("Message", pchDisplayNameRef, ret->hDisplayName);
	} else {
		ErrorFilenamef("data/ui/gens/Options.uigen.ms", "Missing display message '%s' for options panel", pchDisplayNameRef);
	}
	sprintf(pchDisplayNameRef, "Options_Setting_%s_%s_Tooltip", categoryName, text);
	if (RefSystem_ReferentFromString("Message", pchDisplayNameRef)) {
		SET_HANDLE_FROM_STRING("Message", pchDisplayNameRef, ret->hTooltip);
	} else {
		ErrorFilenamef("data/ui/gens/Options.uigen.ms", "Missing tooltip message '%s' for options panel", pchDisplayNameRef);
	}
	sprintf(pchDisplayNameRef, "Options_Setting_%s_%s_Disabled_Tooltip", categoryName, text);
	if (RefSystem_ReferentFromString("Message", pchDisplayNameRef)) {
		SET_HANDLE_FROM_STRING("Message", pchDisplayNameRef, ret->hDisabledTooltip);
	} else {
		// Disabled tooltip is optional so does not generate an error
	}
	eaPush(&cat->eaSettings, ret);
	return ret;
}

OptionSetting *OptionSettingGet(const char *categoryName, const char *settingName)
{
	const char *realName = allocFindString(settingName);
	int i;
	OptionCategory *cat = OptionCategoryGet(categoryName);
	if (!cat || !realName)
	{
		return NULL;
	}
	for (i = 0; i < eaSize(&cat->eaSettings); i++)
	{
		if (realName == cat->eaSettings[i]->pchName)
			return cat->eaSettings[i];
	}
	return NULL;
}


OptionSetting *OptionSettingAddDivider(const char *categoryName, const char *settingName,
									   OptionSettingCallback cbUpdate, OptionSettingCallback cbDisplay, UserData userData)
{
	OptionSetting *setting = NULL;
	setting = OptionSettingCreate(categoryName, settingName, kOptionSettingDivider, NULL, NULL, cbUpdate, NULL, cbDisplay, NULL, userData);
	OptionSettingChanged(setting, false);
	return setting;
}

OptionSetting *OptionSettingAddBool(const char *categoryName, const char *settingName, bool defaultVal,
						OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData)
{
	OptionSetting *setting = NULL;
	setting = OptionSettingCreate(categoryName, settingName, kOptionSettingBool, cbChanged, cbCommitted, cbUpdate, cbRestoreDefaults, cbDisplay, NULL, userData);
	setting->iIntValue = defaultVal;
	setting->iOrigIntValue = defaultVal;
	OptionSettingChanged(setting, false);
	return setting;
}

OptionSetting * OptionSettingAddCheckbox( const char *categoryName, const char *settingName, bool defaultVal,
						OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData)
{
	OptionSetting *setting = NULL;
	setting = OptionSettingCreate(categoryName, settingName, kOptionSettingCheckbox, cbChanged, cbCommitted, cbUpdate, cbRestoreDefaults, cbDisplay, NULL, userData);
	setting->iIntValue = defaultVal;
	setting->iOrigIntValue = defaultVal;
	OptionSettingChanged(setting, false);
	return setting;

}

OptionSetting *OptionSettingAddSlider(const char *categoryName, const char *settingName, F32 min, F32 max, F32 defaultVal,
						OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, OptionSettingCallback cbInput, UserData userData)
{
	OptionSetting *setting = NULL;
	setting = OptionSettingCreate(categoryName, settingName, kOptionSettingFloatSlider, cbChanged, cbCommitted, cbUpdate, cbRestoreDefaults, cbDisplay, cbInput, userData);
	setting->fMin = min;
	setting->fMax = max;
	setting->fFloatValue = defaultVal;
	setting->fOrigFloatValue = defaultVal;
	OptionSettingChanged(setting, false);
	return setting;
}

OptionSetting *OptionSettingAddIntSlider(const char *categoryName, const char *settingName, int min, int max, int defaultVal,
						OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, OptionSettingCallback cbInput, UserData userData)
{
	OptionSetting *setting = NULL;
	setting = OptionSettingCreate(categoryName, settingName, kOptionSettingIntSlider, cbChanged, cbCommitted, cbUpdate, cbRestoreDefaults, cbDisplay, cbInput, userData);
	setting->fMin = min;
	setting->fMax = max;
	setting->iIntValue = defaultVal;
	setting->iOrigIntValue = defaultVal;
	OptionSettingChanged(setting, false);
	return setting;
}

void OptionSettingComboBoxAddOptions(OptionSetting *setting, const char *categoryName,
									 const char * const * const *options, bool bTranslateOptions)
{
	const char* settingName = setting->pchName;
	S32 i;
	if (!options)
		return;
	for (i = 0; i < eaSize(options); ++i)
	{
		OptionSettingComboChoice *pChoice = StructCreate(parse_OptionSettingComboChoice);
		char achMessageRef[1024];
		pChoice->pchName = allocAddString((*options)[i]);
		sprintf(achMessageRef, "Options_Setting_%s_%s_Combo_%s", categoryName, settingName, pChoice->pchName);
		if (!bTranslateOptions) {
			if (!RefSystem_ReferentFromString("Message", achMessageRef))
			{
				// TODO: something cleverer
				Message *pmsg = langCreateMessage(achMessageRef, "AutoGen", "UI/Options", pChoice->pchName);
				RefSystem_AddReferent("Message", achMessageRef, pmsg);
			}
		}
		if (RefSystem_ReferentFromString("Message", achMessageRef)) {
			SET_HANDLE_FROM_STRING("Message", achMessageRef, pChoice->hDisplayName);
		} else {
			Errorf("Missing display message '%s' for options panel", achMessageRef);
		}
		pChoice->iIndex = i;
		eaPush(&setting->eaComboBoxOptions, pChoice);
	}
}

void OptionSettingUpdateComboBoxOptions(OptionSetting *setting, const char* categoryName,
										char ***options, bool bTranslateOptions)
{
	if (setting && categoryName && options)
	{
		eaClearStruct(&setting->eaComboBoxOptions, parse_OptionSettingComboChoice);
		OptionSettingComboBoxAddOptions(setting,categoryName,options,bTranslateOptions);
	}
}

OptionSetting *OptionSettingAddComboBox(const char *categoryName, const char *settingName, const char * const * const *options, bool bTranslateOptions, S32 defaultSelection,
										OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData)
{
	OptionSetting *setting = NULL;
	setting = OptionSettingCreate(categoryName, settingName, kOptionSettingComboBox, cbChanged, cbCommitted, cbUpdate, cbRestoreDefaults, cbDisplay, NULL, userData);
	setting->iIntValue = defaultSelection;
	setting->iOrigIntValue = defaultSelection;
	OptionSettingComboBoxAddOptions(setting, categoryName, options, bTranslateOptions);
	OptionSettingChanged(setting, false);
	return setting;
}

OptionSetting *OptionSettingAddCheckComboBox(const char *categoryName, const char *settingName, const char * const * const *options, bool bTranslateOptions, S32 defaultSelection, const char *defaultString,
											 OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData)
{
	OptionSetting *setting = NULL;
	setting = OptionSettingCreate(categoryName, settingName, kOptionSettingCheckComboBox, cbChanged, cbCommitted, cbUpdate, cbRestoreDefaults, cbDisplay, NULL, userData);
	setting->iIntValue = defaultSelection;
	setting->iOrigIntValue = defaultSelection;
	estrPrintf(&(setting->pchComboDefault), "%s", defaultString);
	OptionSettingComboBoxAddOptions(setting, categoryName, options, bTranslateOptions);
	OptionSettingChanged(setting, false);
	return setting;
}

void OptionSettingSetActive(OptionSetting *setting, bool bActive)
{
	if (setting)
	{
		setting->bEnabled = bActive;
	}
}

void OptionSettingHide(OptionSetting *setting, bool bHide)
{
	if (setting)
	{
		setting->bHide = bHide;
	}
}

void OptionSettingSetHideCallback(OptionSetting *setting, OptionSettingShouldHideCallback cb)
{
	if (setting)
	{
		if (!cb)
		{
			setting->bHide = false;
		}

		setting->cbHideShow = cb;
	}
}

void OptionSettingSetDisabledTooltip(OptionSetting *setting, const char* messageName)
{
	if (!setting) return;
	SET_HANDLE_FROM_STRING("Message", messageName, setting->hDisabledTooltip);
}

// Fill in a list of all option categories.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetCategoryList);
void gclOptionsExpr_GetCategoryList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_eaOptionSettingCategories, parse_OptionCategory);
}

// Fill in a list of all options for the given category.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetSettingsForCategory);
void gclOptionsExpr_GetSettingsForCategory(SA_PARAM_NN_VALID UIGen *pGen, S32 iCategory)
{
	OptionCategory *pCat = eaGet(&s_eaOptionSettingCategories, iCategory);
	ui_GenSetList(pGen, pCat ? &pCat->eaSettings : NULL, parse_OptionSetting);
}

// Fill in a list of all options for the given category name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetSettingsForCategoryName);
void gclOptionsExpr_GetSettingsForCategoryName(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCategory)
{
	static OptionSetting **s_eaData = NULL;
	S32 i, j;
	eaClear(&s_eaData);
	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); i++)
	{
		if (!stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
		{
			for (j = 0; j < eaSize(&s_eaOptionSettingCategories[i]->eaSettings); j++)
			{
				OptionSetting *setting = s_eaOptionSettingCategories[i]->eaSettings[j];
				bool bHide = false;
				if (setting->bHide)
				{
					bHide = true;
				}
				else if (setting->cbHideShow)
				{
					bHide = setting->cbHideShow(setting);
				}

				if ( s_bShowHidden || !bHide )
				{
					eaPush(&s_eaData, setting);
				}
			}

			break;
		}
	}
	ui_GenSetListSafe(pGen, &s_eaData, OptionSetting);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetSetting);
SA_RET_OP_VALID OptionSetting *gclOptionsExpr_GetSetting(const char *pchCategory, const char *pchSetting)
{
	S32 i, j;
	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); i++)
	{
		if (s_eaOptionSettingCategories[i] && stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory) == 0)
		{
			for (j = 0; j < eaSize(&s_eaOptionSettingCategories[i]->eaSettings); j++)
			{
				OptionSetting *setting = s_eaOptionSettingCategories[i]->eaSettings[j];
				if (setting && stricmp(setting->pchName, pchSetting) == 0)
				{
					return setting;
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsSettingMax);
float gclOptionsExpr_SettingMax(SA_PARAM_OP_VALID OptionSetting* setting)
{
	return SAFE_MEMBER(setting, fMax);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsSettingMin);
float gclOptionsExpr_SettingMin(SA_PARAM_OP_VALID OptionSetting* setting)
{
	return SAFE_MEMBER(setting, fMin);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsSettingFloatValue);
float gclOptionsExpr_SettingFloatValue(SA_PARAM_OP_VALID OptionSetting* setting)
{
	return SAFE_MEMBER(setting, fFloatValue);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsSettingIntValue);
int gclOptionsExpr_SettingIntValue(SA_PARAM_OP_VALID OptionSetting* setting)
{
	return SAFE_MEMBER(setting, iIntValue);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsSettingIncrement);
float gclOptionsExpr_SettingIncrement(SA_PARAM_OP_VALID OptionSetting* setting)
{
	return SAFE_MEMBER(setting, fIncrement);
}

bool OptionSettingsNameMatchesAnyOf(const char* pchName, const char* pchCheckAgainst)
{
	char* pchCurrentOption;
	char* pchCheckAgainstCopy;
	char* pchContext;
	strdup_alloca(pchCheckAgainstCopy, pchCheckAgainst);

	pchCurrentOption = strtok_r(pchCheckAgainstCopy, " ", &pchContext);
	do
	{
		if (stricmp(pchName, pchCurrentOption) == 0)
		{
			return true;
		}
	} while (pchCurrentOption = strtok_s(NULL, " ", &pchContext));

	return false;
}

void gclOptions_GetSettingsForCategoryName(OptionSetting ***peaSettings, const char *pchCategory)
{
	S32 i, j;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); i++)
	{
		if (!stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
		{
			for (j = 0; j < eaSize(&s_eaOptionSettingCategories[i]->eaSettings); j++)
			{
				eaPush(peaSettings, s_eaOptionSettingCategories[i]->eaSettings[j]);
			}
			return;
		}
	}
}

void gclOptions_GetSettingsForCategoryNameAndOptionName(OptionSetting ***peaSettings, const char *pchCategory, const char ***peaOptionNames)
{
	S32 i, j, k;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); i++)
	{
		if (!stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
		{
			for (j = 0; j < eaSize(&s_eaOptionSettingCategories[i]->eaSettings); j++)
			{
				for (k = 0; k < eaSize(peaOptionNames); ++k)
				{
					if (stricmp(s_eaOptionSettingCategories[i]->eaSettings[j]->pchName, (*peaOptionNames)[k]) == 0)
					{
						eaPush(peaSettings, s_eaOptionSettingCategories[i]->eaSettings[j]);
						break;
					}
				}
			}
			return;
		}
	}
}

void gclOptions_GetSettingsForCategoryNameExcludingOptionName(OptionSetting ***peaSettings, const char *pchCategory, const char ***peaOptionNames)
{
	S32 i, j, k;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); i++)
	{
		if (!stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
		{
			for (j = 0; j < eaSize(&s_eaOptionSettingCategories[i]->eaSettings); j++)
			{
				bool bFound = false;

				for (k = 0; k < eaSize(peaOptionNames); ++k)
				{
					if (stricmp(s_eaOptionSettingCategories[i]->eaSettings[j]->pchName, (*peaOptionNames)[k]) == 0)
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					eaPush(peaSettings, s_eaOptionSettingCategories[i]->eaSettings[j]);
				}
			}
			return;
		}
	}
}

// Fill in a list of all options for the given category name and option name.
// This is really only meant to get the basic options for the graphics menu.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetSettingsForCategoryNameAndOptionName);
void gclOptionsExpr_GetSettingsForCategoryNameAndOptionName(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCategory, const char *pchOptionNames)
{
	static OptionSetting** s_eaOptionSettings = NULL;
	S32 i, j, k;

	eaClearFast(&s_eaOptionSettings);
	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); i++)
	{
		if (!stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
		{
			char* pchTempString;
			char* pchCurrentToken;
			char* pchContext;
			char** eaOptionNames = NULL;

			strdup_alloca(pchTempString, pchOptionNames);
			pchCurrentToken = strtok_r(pchTempString, " ", &pchContext);
			do
			{
				eaPush(&eaOptionNames, pchCurrentToken);
			}
			while (pchCurrentToken = strtok_s(NULL, " ", &pchContext));

			for (j = 0; j < eaSize(&eaOptionNames); j++)
			{
				for (k = 0; k < eaSize(&s_eaOptionSettingCategories[i]->eaSettings); k++)
				{
					if (!stricmp(s_eaOptionSettingCategories[i]->eaSettings[k]->pchName, eaOptionNames[j]))
					{
						eaPush(&s_eaOptionSettings, s_eaOptionSettingCategories[i]->eaSettings[k]);
					}
				}
			}
			ui_GenSetListSafe(pGen, &s_eaOptionSettings, OptionSetting);

			eaDestroy(&eaOptionNames); //string are allocated using alloca
			return;
		}
	}
	ui_GenSetListSafe(pGen, &s_eaOptionSettings, OptionSetting);
}

// Fill in a list of all options for the given category name and option name.
// This is really only meant to get the advanced options for the graphics menu.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetSettingsForCategoryNameExcludingOptionName);
void gclOptionsExpr_GetSettingsForCategoryNameExcludingOptionName(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCategory, const char *pchOptionNames)
{
	static OptionSetting** s_eaOptionSettings = NULL;
	S32 i, j;

	eaClearFast(&s_eaOptionSettings);
	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); i++)
	{
		if (!stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
		{
			for (j = 0; j < eaSize(&s_eaOptionSettingCategories[i]->eaSettings); j++)
			{
				if (!OptionSettingsNameMatchesAnyOf(s_eaOptionSettingCategories[i]->eaSettings[j]->pchName, pchOptionNames))
				{
					eaPush(&s_eaOptionSettings, s_eaOptionSettingCategories[i]->eaSettings[j]);
				}
			}
			ui_GenSetListSafe(pGen, &s_eaOptionSettings, OptionSetting);
			return;
		}
	}
	ui_GenSetListSafe(pGen, &s_eaOptionSettings, OptionSetting);
}

// Get the name of the next category in the category list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetNextCategoryName);
const char *gclOptionsExpr_GetNextCategoryName(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCategory)
{
	S32 i;
	OptionCategory *pCategory = NULL;
	for (i = eaSize(&s_eaOptionSettingCategories) - 1; i >= 0; i--)
	{
		if (pCategory && !stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
			return pCategory->pchName;
		else
			pCategory = s_eaOptionSettingCategories[i];
	}
	return pchCategory;
}

// Get the name of the previous category in the category list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetPreviousCategoryName);
const char *gclOptionsExpr_GetPreviousCategoryName(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCategory)
{
	S32 i;
	OptionCategory *pCategory = NULL;
	for (i = 0; i < eaSize(&s_eaOptionSettingCategories) - 1; i++)
	{
		if (pCategory && !stricmp(s_eaOptionSettingCategories[i]->pchName, pchCategory))
			return pCategory->pchName;
		else
			pCategory = s_eaOptionSettingCategories[i];
	}
	return pchCategory;
}

// Fill in a list of all options for the given combo box setting.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetChoicesForSetting);
void gclOptionsExpr_GetChoicesForSetting(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID OptionSetting *pSetting)
{
	ui_GenSetList(pGen, pSetting ? &pSetting->eaComboBoxOptions : NULL, parse_OptionSettingComboChoice);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsBooleanChanged);
void gclOptionsExpr_BooleanChanged(SA_PARAM_OP_VALID OptionSetting *setting, bool bState)
{
	if (setting && ((bState && !setting->iIntValue) || (!bState && setting->iIntValue) && setting->bEnabled))
	{
		setting->iIntValue = !!bState;
		OptionSettingChanged(setting, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsFloatSliderChanged);
void gclOptionsExpr_FloatSliderChanged(SA_PARAM_OP_VALID OptionSetting *setting, F64 fValue)
{
	if (setting && !nearf(fValue, setting->fFloatValue) && setting->bEnabled)
	{
		setting->fFloatValue = fValue;
		OptionSettingChanged(setting, true);
	}
}

// Cut this function after uigens have been changed to make use of the SetClusterState (the below function)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsChangeCauseMapReload);
bool gclOptionsExpr_OptionsChangeCauseMapReload()
{
	wl_state.gfx_cluster_set_cluster_state(wl_state.gfx_get_cluster_load_setting_callback());
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetClusterState);
void gclOptionsExpr_SetClusterState()
{
	wl_state.gfx_cluster_set_cluster_state(wl_state.gfx_get_cluster_load_setting_callback());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsFloatSliderChangedUser);
void gclOptionsExpr_FloatSliderChangedUser(SA_PARAM_OP_VALID OptionSetting *setting, F64 fValue)
{
	if (setting && setting->bEnabled)
	{
		setting->fFloatValue = fValue;

		if ( setting->cbInput )
			setting->cbInput(setting);

		setting->fFloatValue = CLAMPF32(setting->fFloatValue,setting->fMin,setting->fMax);

		OptionSettingChanged(setting, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsIntSliderChanged);
void gclOptionsExpr_IntSliderChanged(SA_PARAM_OP_VALID OptionSetting *setting, F64 fValue)
{
	if (setting && !nearf(fValue, setting->iIntValue) && setting->bEnabled)
	{
		setting->iIntValue = fValue;
		OptionSettingChanged(setting, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsIntSliderChangedUser);
void gclOptionsExpr_IntSliderChangedUser(SA_PARAM_OP_VALID OptionSetting *setting, F64 fValue)
{
	if (setting && setting->bEnabled)
	{
		setting->iIntValue = fValue;

		if ( setting->cbInput )
			setting->cbInput(setting);

		setting->iIntValue = CLAMP(setting->iIntValue, setting->fMin, setting->fMax);

		OptionSettingChanged(setting, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsComboBoxChanged);
void gclOptionsExpr_ComboBoxChanged(SA_PARAM_OP_VALID OptionSetting *setting, S32 iRow)
{
	if (setting && iRow != setting->iIntValue && setting->bEnabled && iRow >= 0 && iRow < eaSize(&setting->eaComboBoxOptions))
	{
		setting->iIntValue = iRow;
		OptionSettingChanged(setting, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsCheckComboBoxChanged);
void gclOptionsExpr_CheckComboBoxChanged(SA_PARAM_OP_VALID OptionSetting *setting, S32 iRow, bool iValue)
{
	if(setting && iRow >= 0 && iRow < eaSize(&setting->eaComboBoxOptions))
	{
		int iOrigValue = setting->iIntValue;

		if(iValue)
		{
			setting->iIntValue |= 1 << iRow;
		}
		else
		{
			setting->iIntValue &= ~(1 << iRow);
		}

		if(iOrigValue != setting->iIntValue)
		{
			OptionSettingChanged(setting, true);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(OptionsGetCheckComboBoxValue);
bool gclOptionsExpr_GetCheckComboBoxValue(SA_PARAM_OP_VALID OptionSetting *setting, S32 iRow)
{
	if(setting && iRow >= 0 && iRow < eaSize(&setting->eaComboBoxOptions))
	{
		return !!(setting->iIntValue & (1 << iRow));
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetGraphicsShaderFillValue);
F32 gclOptionsExpr_GetPixelShaderFillValue(void)
{
	return gclGraphics_GetPixelFill();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMaxFill);
S32 gclOptionsExpr_GetMaxFillValue(void)
{
	return gfxGetFillMax();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRecommendedRenderScale);
F32 gclOptionsExpr_GetGetRecommendedRenderScale(void)
{
	return gclGraphics_GetRecommendedRenderScale();
}

// Revert changes to game options.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Options_Revert") ACMD_HIDE;
void OptionsRevertAll(void)
{
	int i, j;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *cat = s_eaOptionSettingCategories[i];

		for (j = 0; j < eaSize(&cat->eaSettings); ++j)
		{
			OptionSetting *setting = cat->eaSettings[j];
			if (setting->iOrigIntValue != setting->iIntValue)
			{
				setting->iIntValue = setting->iOrigIntValue;
				OptionSettingChanged(setting, true);
				OptionSettingConfirm(setting);
			}
		}
	}
}

// Restore default/recommended game options.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Options_RestoreDefaults") ACMD_HIDE;
void Options_RestoreDefaults(void)
{
	int i, j;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *pCategory = s_eaOptionSettingCategories[i];
		for (j = 0; j < eaSize(&pCategory->eaSettings); ++j)
		{
			OptionSetting *pSetting = pCategory->eaSettings[j];
			S32 iValue = pSetting->iIntValue;
			if (pSetting->cbRestoreDefaults)
			{
				pSetting->cbRestoreDefaults(pSetting);
			}
			else
			{
				// If we can't restore defaults, at least restore it to what it was
				// when the dialog opened.
				pSetting->iIntValue = pSetting->iOrigIntValue;
			}

			if (pSetting->iIntValue != iValue)
			{
				OptionSettingChanged(pSetting, true);
				OptionSettingConfirm(pSetting);
			}
		}
	}
}

// Restore default/recommended game options for a single category.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Options_RestoreDefaultsForCategory") ACMD_HIDE;
void Options_RestoreDefaultsFor(const char *pchCategory)
{
	int i, j;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *pCategory = s_eaOptionSettingCategories[i];
		if (!stricmp(pCategory->pchName, pchCategory))
		{
			for (j = 0; j < eaSize(&pCategory->eaSettings); ++j)
			{
				OptionSetting *pSetting = pCategory->eaSettings[j];
				S32 iValue = pSetting->iIntValue;
				if (pSetting->cbRestoreDefaults)
				{
					pSetting->cbRestoreDefaults(pSetting);
				}
				else
				{
					// If we can't restore defaults, at least restore it to what it was
					// when the dialog opened.
					pSetting->iIntValue = pSetting->iOrigIntValue;
				}

				if (pSetting->iIntValue != iValue)
				{
					OptionSettingChanged(pSetting, true);
					OptionSettingConfirm(pSetting);
				}
			}
			break;
		}
	}
}

// Confirm changes to game options.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("Options_Finalize", "Options_Confirm") ACMD_HIDE;
void OptionsFinalizeAll(void)
{
	int i, j;

	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *pCategory = s_eaOptionSettingCategories[i];
		for (j = 0; j < eaSize(&pCategory->eaSettings); ++j)
		{
			OptionSetting *pSetting = pCategory->eaSettings[j];

			if (pSetting->iIntValue != pSetting->iOrigIntValue)
				OptionSettingConfirm(pSetting);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Auto-settings convenience functions
typedef struct AutoSettingData {
	void *dataPtr;
	U32 mask; // If only one bit
	F32 fStep, fMax, fMin;
	OptionSettingCallback changedCallback;
	OptionSettingCallback committedCallback;
	OptionSettingCallback updateCallback;
	OptionSettingCallback restoreDefaultsCallback;
	OptionSettingCallback customDisplayCallback;
	OptionSettingCallback customInputCallback;
} AutoSettingData;

static void autoSettingChangedCallback(OptionSetting *setting)
{
	AutoSettingData *userData = setting->pData;
	if (userData->fStep)
	{
		*(F32*)userData->dataPtr = (float)setting->iIntValue * userData->fStep + userData->fMin;
	}
	else
	{
		if (userData->mask) {
			*(U32*)userData->dataPtr = ((*(U32*)userData->dataPtr)&~userData->mask) | (setting->iIntValue?userData->mask:0);
		} else {
			*(U32*)userData->dataPtr = setting->iIntValue;
		}
	}

	if (userData->changedCallback)
		userData->changedCallback(setting);
}

static void autoSettingCommittedCallback(OptionSetting *setting)
{
	AutoSettingData *userData = setting->pData;
	if (userData->fStep)
	{
		*(F32*)userData->dataPtr = (float)setting->iIntValue * userData->fStep + userData->fMin;
	}
	else
	{
		if (userData->mask) {
			*(U32*)userData->dataPtr = ((*(U32*)userData->dataPtr)&~userData->mask) | (setting->iIntValue?userData->mask:0);
		} else {
			*(U32*)userData->dataPtr = setting->iIntValue;
		}
	}
	if (userData->committedCallback)
		userData->committedCallback(setting);
}

static void autoSettingCustomDisplayCallback(OptionSetting *setting);

static void autoSettingUpdateInternal(OptionSetting *setting)
{
	AutoSettingData *userData = setting->pData;
	if (userData->fStep)
	{
		setting->iIntValue = (*(F32*)userData->dataPtr - userData->fMin)/userData->fStep;
	}
	else
	{
		if (userData->mask) {
			setting->iIntValue = (*(U32*)userData->dataPtr & userData->mask)?1:0;
		} else {
			setting->iIntValue = *(U32*)userData->dataPtr;
		}
	}

	autoSettingCustomDisplayCallback(setting);
}

static void autoSettingUpdateCallback(OptionSetting *setting)
{
	AutoSettingData *userData = setting->pData;
	// *before* reading the memory
	if (userData->updateCallback)
		userData->updateCallback(setting);
	autoSettingUpdateInternal(setting);
	OptionSettingChanged(setting, false);
}

static void autoSettingRestoreDefaultsCallback(OptionSetting *setting)
{
	AutoSettingData *userData = setting->pData;
	if (userData->restoreDefaultsCallback)
	{
		// Allow user to change the values
		userData->restoreDefaultsCallback(setting);
		// Get the values from the user's data into the setting struct
		autoSettingUpdateInternal(setting);
	} else {
		// No restore defaults callback, instead just restore to the value we had when entering the options screen?
		if (setting->iIntValue != setting->iOrigIntValue)
		{
			setting->iIntValue = setting->iOrigIntValue;
		}
	}
}

static void autoSettingCustomDisplayCallback(OptionSetting *setting)
{
	AutoSettingData *userData = setting->pData;
	if (userData->customDisplayCallback)
	{
		if (userData->fStep) //we need to fudge the value here to hide the float->int conversion
		{
			int oldVal = setting->iIntValue;
			setting->fFloatValue = (float)setting->iIntValue * userData->fStep + userData->fMin;
			userData->customDisplayCallback(setting);
			setting->iIntValue  = oldVal;
		}
		else
		{
			userData->customDisplayCallback(setting);
		}

	}
}

static void autoSettingCustomInputCallback(OptionSetting *setting)
{
	AutoSettingData *userData = setting->pData;
	if (userData->customInputCallback)
	{
		if (userData->fStep) //we need to fudge the value here to hide the float->int conversion
		{
			setting->fFloatValue = (float)setting->iIntValue * userData->fStep + userData->fMin;
			userData->customInputCallback(setting);
			setting->iIntValue  = (setting->fFloatValue - userData->fMin)/userData->fStep;
		}
		else
		{
			userData->customInputCallback(setting);
		}
	}
}

OptionSetting *autoSettingsAddBit(const char *categoryName, const char *settingName, int *ptr, U32 mask, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled)
{
	AutoSettingData *userData = calloc(sizeof(*userData), 1);
	OptionSetting *setting;
	userData->dataPtr = ptr;
	userData->mask = mask;
	userData->changedCallback = changedCallback;
	userData->committedCallback = committedCallback;
	userData->updateCallback = updateCallback;
	userData->restoreDefaultsCallback = restoreDefaultsCallback;
	userData->customDisplayCallback = customDisplayCallback;
	setting = OptionSettingAddBool(categoryName, settingName, *ptr, autoSettingChangedCallback, autoSettingCommittedCallback, autoSettingUpdateCallback, autoSettingRestoreDefaultsCallback, autoSettingCustomDisplayCallback, userData);
	OptionSettingSetActive(setting, enabled);
	return setting;
}

OptionSetting *autoSettingsAddBitCheckbox(const char *categoryName, const char *settingName, int *ptr, U32 mask, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled)
{
	AutoSettingData *userData = calloc(sizeof(*userData), 1);
	OptionSetting *setting;
	userData->dataPtr = ptr;
	userData->mask = mask;
	userData->changedCallback = changedCallback;
	userData->committedCallback = committedCallback;
	userData->updateCallback = updateCallback;
	userData->restoreDefaultsCallback = restoreDefaultsCallback;
	userData->customDisplayCallback = customDisplayCallback;
	setting = OptionSettingAddCheckbox(categoryName, settingName, *ptr, autoSettingChangedCallback, autoSettingCommittedCallback, autoSettingUpdateCallback, autoSettingRestoreDefaultsCallback, autoSettingCustomDisplayCallback, userData);
	OptionSettingSetActive(setting, enabled);
	return setting;
}

OptionSetting *autoSettingsAddComboBox(const char *categoryName, const char *settingName, const char * const * const * options, bool bTranslateOptions, int *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled)
{
	AutoSettingData *userData = calloc(sizeof(*userData), 1);
	OptionSetting *setting;
	userData->dataPtr = ptr;
	userData->changedCallback = changedCallback;
	userData->committedCallback = committedCallback;
	userData->updateCallback = updateCallback;
	userData->restoreDefaultsCallback = restoreDefaultsCallback;
	userData->customDisplayCallback = customDisplayCallback;
	setting = OptionSettingAddComboBox(categoryName, settingName, options, bTranslateOptions, *ptr, autoSettingChangedCallback, autoSettingCommittedCallback, autoSettingUpdateCallback, autoSettingRestoreDefaultsCallback, autoSettingCustomDisplayCallback, userData);
	OptionSettingSetActive(setting, enabled);
	return setting;
}

OptionSetting *autoSettingsAddCheckComboBox(const char *categoryName, const char *settingName, const char * const * const * options, bool bTranslateOptions, int *ptr, const char *defaultString, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled)
{
	AutoSettingData *userData = calloc(sizeof(*userData), 1);
	OptionSetting *setting;
	userData->dataPtr = ptr;
	userData->changedCallback = changedCallback;
	userData->committedCallback = committedCallback;
	userData->updateCallback = updateCallback;
	userData->restoreDefaultsCallback = restoreDefaultsCallback;
	userData->customDisplayCallback = customDisplayCallback;
	setting = OptionSettingAddCheckComboBox(categoryName, settingName, options, bTranslateOptions, *ptr, defaultString, autoSettingChangedCallback, autoSettingCommittedCallback, autoSettingUpdateCallback, autoSettingRestoreDefaultsCallback, autoSettingCustomDisplayCallback, userData);
	OptionSettingSetActive(setting, enabled);
	return setting;
}

OptionSetting *autoSettingsAddIntSlider(const char *categoryName, const char *settingName, int iMin, int iMax, int *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, OptionSettingCallback customInputCallback, bool enabled)
{
	AutoSettingData *userData = calloc(sizeof(*userData), 1);
	OptionSetting *setting;
	userData->dataPtr = ptr;
	userData->changedCallback = changedCallback;
	userData->committedCallback = committedCallback;
	userData->updateCallback = updateCallback;
	userData->restoreDefaultsCallback = restoreDefaultsCallback;
	userData->customDisplayCallback = customDisplayCallback;
	userData->customInputCallback = customInputCallback;
	setting = OptionSettingAddIntSlider(categoryName, settingName, iMin, iMax, *ptr, autoSettingChangedCallback, autoSettingCommittedCallback, autoSettingUpdateCallback, autoSettingRestoreDefaultsCallback, autoSettingCustomDisplayCallback, autoSettingCustomInputCallback, userData);
	OptionSettingSetActive(setting, enabled);
	return setting;
}

OptionSetting *autoSettingsAddFloatSlider(const char *categoryName, const char *settingName, F32 fMin, F32 fMax, F32 *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, OptionSettingCallback customInputCallback, bool enabled)
{
	AutoSettingData *userData = calloc(sizeof(*userData), 1);
	OptionSetting *setting;
	userData->dataPtr = ptr;
	userData->changedCallback = changedCallback;
	userData->committedCallback = committedCallback;
	userData->updateCallback = updateCallback;
	userData->restoreDefaultsCallback = restoreDefaultsCallback;
	userData->customDisplayCallback = customDisplayCallback;
	userData->customInputCallback = customInputCallback;
	setting = OptionSettingAddSlider(categoryName, settingName, fMin, fMax, *ptr, autoSettingChangedCallback, autoSettingCommittedCallback, autoSettingUpdateCallback, autoSettingRestoreDefaultsCallback, autoSettingCustomDisplayCallback, autoSettingCustomInputCallback, userData);
	OptionSettingSetActive(setting, enabled);
	return setting;
}

OptionSetting * autoSettingsAddFloatSliderEx( const char *categoryName, const char *settingName, F32 fMin, F32 fMax, F32 fStep, F32 *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, OptionSettingCallback customInputCallback, bool enabled )
{
	AutoSettingData *userData = calloc(sizeof(*userData), 1);
	OptionSetting *setting;
	userData->dataPtr = ptr;
	userData->changedCallback = changedCallback;
	userData->committedCallback = committedCallback;
	userData->updateCallback = updateCallback;
	userData->restoreDefaultsCallback = restoreDefaultsCallback;
	userData->customDisplayCallback = customDisplayCallback;
	userData->customInputCallback = customInputCallback;
	userData->fMin = fMin;
	userData->fMax = fMax;
	userData->fStep = fStep;
	//actually make an int slider since the float sliders cant have arbitrary steps
	setting = OptionSettingAddIntSlider(categoryName, settingName, 0, (fMax - fMin)/fStep,(*ptr - fMin)/fStep, autoSettingChangedCallback, autoSettingCommittedCallback, autoSettingUpdateCallback, autoSettingRestoreDefaultsCallback, autoSettingCustomDisplayCallback, autoSettingCustomInputCallback, userData);
	OptionSettingSetActive(setting, enabled);
	return setting;
}

void displayFloatAsPercentage(OptionSetting *setting)
{
	estrPrintf(&setting->pchStringValue, "%5.0f%%", setting->fFloatValue * 100);
}

void inputFloatPercentage(OptionSetting *setting)
{
	AutoSettingData* pData = (AutoSettingData*)(setting->pData);

	if (pData)
	{
		F32 fValue = setting->fFloatValue/100.0f + FLT_EPSILON;
		F32 fStep = pData->fStep > FLT_EPSILON ? pData->fStep : 1.0f;
		setting->fFloatValue = fValue / fStep;
	}
}

void inputIntPercentageScaled(OptionSetting *setting)
{
	AutoSettingData* pData = (AutoSettingData*)(setting->pData);

	if (pData)
	{
		int iValue = setting->iIntValue;
		setting->iIntValue = iValue / 10;
	}
}


void OptionsUpdateAll()
{
	int i, j;
	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *cat = s_eaOptionSettingCategories[i];
		for (j = 0; j < eaSize(&cat->eaSettings); ++j)
		{
			OptionSetting *setting = cat->eaSettings[j];
			if (setting->cbUpdate)
			{
				setting->cbUpdate(setting);
			}
		}
	}
}

void OptionsUpdateCategory(const char* pchCategory)
{
	int i, j;
	for (i = 0; i < eaSize(&s_eaOptionSettingCategories); ++i)
	{
		OptionCategory *cat = s_eaOptionSettingCategories[i];
		if (!stricmp(cat->pchName,pchCategory))
		{
			for (j = 0; j < eaSize(&cat->eaSettings); ++j)
			{
				OptionSetting *setting = cat->eaSettings[j];
				if (setting->cbUpdate)
				{
					setting->cbUpdate(setting);
				}
			}
			break;
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void OptionsMenuShowHidden(int bShowHidden)
{
	s_bShowHidden = bShowHidden;
}

static bool optionAlwaysHideCallback(OptionSetting *setting)
{
	return true;
}

void options_HideOption(const char* pchCategory, const char *pOptionName, OptionSettingShouldHideCallback cb)
{
	OptionSetting *pSetting = OptionSettingGet(pchCategory, pOptionName);
	if (pSetting)
	{
		if (!cb)
			cb = optionAlwaysHideCallback;

		OptionSettingSetHideCallback(pSetting, cb);
	}
}

void options_UnhideOption(const char* pchCategory, const char *pOptionName)
{
	OptionSetting *pSetting = OptionSettingGet(pchCategory, pOptionName);
	if (pSetting)
	{
		OptionSettingSetHideCallback(pSetting, NULL);
	}
}

#include "gclOptions_h_ast.c"
