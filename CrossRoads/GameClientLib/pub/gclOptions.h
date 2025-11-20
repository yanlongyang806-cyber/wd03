#ifndef GCL_OPTIONS_H
#define GCL_OPTIONS_H
GCC_SYSTEM

#include "ReferenceSystem.h"

typedef struct Message Message;
typedef struct OptionSetting OptionSetting;
typedef struct UIWidget UIWidget;

typedef void (*OptionSettingCallback)(OptionSetting *setting);
// return true if the option is to be shown
typedef bool (*OptionSettingShouldHideCallback)(OptionSetting *setting);


AUTO_ENUM;
typedef enum OptionSettingType
{
	kOptionSettingBool,
	kOptionSettingCheckbox,
	kOptionSettingFloatSlider,
	kOptionSettingIntSlider,
	kOptionSettingComboBox,
	kOptionSettingDivider,
	kOptionSettingCheckComboBox,
} OptionSettingType;

AUTO_STRUCT;
typedef struct OptionSettingComboChoice
{
	const char *pchName; AST(KEY POOL_STRING)

	// Built automatically as Options_Setting_<CategoryName>_<OptionName>_Combo_<ChoiceName>
	REF_TO(Message) hDisplayName; AST(NAME(DisplayName))

	bool bHideChoice;
	U32 iIndex; // In case choices are hidden, this is a reliable way to match the choice in the UI with the choice in the array
} OptionSettingComboChoice;

AUTO_STRUCT;
typedef struct OptionSetting
{
	const char *pchName; AST(KEY POOL_STRING)
	OptionSettingType eType;

	// Built automatically as Options_Setting_<CategoryName>_<OptionName>
	REF_TO(Message) hDisplayName; AST(NAME(DisplayName))

	// Built automatically as Options_Setting_<CategoryName>_<OptionName>_Tooltip
	REF_TO(Message) hTooltip; AST(NAME(Tooltip))

	// Built automatically as Options_Setting_<CategoryName>_<OptionName>_Disabled_Tooltip
	REF_TO(Message) hDisabledTooltip; AST(NAME(DisabledTooltip))

	union
	{
		S32 iIntValue;
		F32 fFloatValue; AST(REDUNDANTNAME)
	};

	union
	{
		S32 iOrigIntValue;
		F32 fOrigFloatValue; AST(REDUNDANTNAME)
	};

	// the value of the setting as an estring
	char *pchStringValue; AST(ESTRING)

	// minimum value for a slider setting
	F32 fMin;

	// maximum value for a slider setting
	F32 fMax;

	//the increment amount a slider can be set to
	F32 fIncrement;

	// option strings for a combo box setting
	OptionSettingComboChoice **eaComboBoxOptions; AST(NO_INDEX)
	OptionSettingComboChoice *pComboChoice; AST(UNOWNED)
	char *pchComboDefault; AST(ESTRING)

	// called when an option's value is changed by the user.
	OptionSettingCallback cbChanged; NO_AST

	// called if an option is dirty and the user clicks the "OK" or "Apply"
	// buttons or otherwise confirms "heavy" options.
	OptionSettingCallback cbCommitted; NO_AST

	// called before the window is shown to make sure the values in this
	// OptionSetting reflect the current state of things, in case they
	// were changed by e.g. / commands.
	OptionSettingCallback cbUpdate; NO_AST

	// reset this OptionSetting to its recommended default value. cbChanged
	// will be called immediately after this, so you don't need to do
	// anything other than set IntValue/FloatValue.
	OptionSettingCallback cbRestoreDefaults; NO_AST

	// custom display callback
	OptionSettingCallback cbDisplay; NO_AST

	// custom input callback
	OptionSettingCallback cbInput; NO_AST

	// if the option has the potential to be hidden for some reason
	OptionSettingShouldHideCallback cbHideShow; NO_AST

	// whether or not the user can edit this setting
	bool bEnabled;
	bool bHide;

	UserData pData; NO_AST
} OptionSetting;

AUTO_STRUCT;
typedef struct OptionCategory
{
	const char *pchName; AST(KEY POOL_STRING)
	OptionSetting **eaSettings; AST(NO_INDEX)

	// Built automatically as Options_Category_<CategoryName>
	REF_TO(Message) hDisplayName; AST(NAME(DisplayName))
} OptionCategory;

// Category functions

OptionCategory *OptionCategoryAdd(const char *pchName);
OptionCategory *OptionCategoryGet(const char *pchName);
bool OptionCategoryDestroy(const char *pchName);


// Setting functions

// These will add the category if it doesn't already exist.
OptionSetting *OptionSettingAddBool(const char *categoryName, const char *settingName, bool defaultVal, 
									OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData);
OptionSetting *OptionSettingAddCheckbox(const char *categoryName, const char *settingName, bool defaultVal, 
									OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData);

OptionSetting *OptionSettingAddSlider(const char *categoryName, const char *settingName, F32 min, F32 max, F32 defaultVal,
									  OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, OptionSettingCallback cbInput, UserData userData);
OptionSetting *OptionSettingAddIntSlider(const char *categoryName, const char *settingName, int min, int max, int defaultVal,
										 OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, OptionSettingCallback cbInput, UserData userData);
void OptionSettingComboBoxAddOptions(OptionSetting *setting, const char *categoryName,
									 const char * const * const *options, bool bTranslateOptions);
void OptionSettingUpdateComboBoxOptions(OptionSetting *setting, const char* categoryName, char ***options, bool bTranslateOptions);
OptionSetting *OptionSettingAddComboBox(const char *categoryName, const char *settingName, const char * const * const * options, bool bTranslateOptions, S32 defaultSelection,
										OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData);
OptionSetting *OptionSettingAddCheckComboBox(const char *categoryName, const char *settingName, const char * const * const * options, bool bTranslateOptions, S32 defaultSelection, const char *defaultString,
										OptionSettingCallback cbChanged, OptionSettingCallback cbCommitted, OptionSettingCallback cbUpdate, OptionSettingCallback cbRestoreDefaults, OptionSettingCallback cbDisplay, UserData userData);
OptionSetting *OptionSettingAddDivider(const char *categoryName, const char *settingName,
									   OptionSettingCallback cbUpdate, OptionSettingCallback cbDisplay, UserData userData);

OptionSetting *OptionSettingGet(const char *categoryName, const char *settingName);
void OptionSettingChanged(OptionSetting *pSetting, bool bCallback);

void OptionSettingSetActive(OptionSetting *setting, bool bActive);
void OptionSettingHide(OptionSetting *setting, bool bHide);
void OptionSettingSetHideCallback(OptionSetting *setting, OptionSettingShouldHideCallback cb);
void OptionSettingSetDisabledTooltip(OptionSetting *setting, const char* messageName);

void gclGraphicsOptions_Init(void);

void OptionsUpdateAll();
void OptionsUpdateCategory(const char* pchCategory);

void Options_RestoreDefaultsFor(const char *pchCategory);
void OptionSettingConfirm(OptionSetting *pSetting);

void gclOptions_GetSettingsForCategoryName(OptionSetting ***peaSettings, const char *pchCategory);
void gclOptions_GetSettingsForCategoryNameAndOptionName(OptionSetting ***peaSettings, const char *pchCategory, const char ***peaOptionNames);
void gclOptions_GetSettingsForCategoryNameExcludingOptionName(OptionSetting ***peaSettings, const char *pchCategory, const char ***peaOptionNames);

//////////////////////////////////////////////////////////////////////////
// Auto-settings convenience functions
OptionSetting *autoSettingsAddBit(const char *categoryName, const char *settingName, int *ptr, U32 mask, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled);
OptionSetting *autoSettingsAddBitCheckbox(const char *categoryName, const char *settingName, int *ptr, U32 mask, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled);
OptionSetting *autoSettingsAddComboBox(const char *categoryName, const char *settingName, const char * const * const * options, bool bTranslateOptions, int *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled);
OptionSetting *autoSettingsAddCheckComboBox(const char *categoryName, const char *settingName, const char * const * const * options, bool bTranslateOptions, int *ptr, const char *defaultString, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, bool enabled);
OptionSetting *autoSettingsAddIntSlider(const char *categoryName, const char *settingName, int iMin, int iMax, int *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, OptionSettingCallback customInputCallback, bool enabled);
OptionSetting *autoSettingsAddFloatSlider(const char *categoryName, const char *settingName, F32 fMin, F32 fMax, F32 *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, OptionSettingCallback customInputCallback, bool enabled);
OptionSetting *autoSettingsAddFloatSliderEx(const char *categoryName, const char *settingName, F32 fMin, F32 fMax, F32 fStep, F32 *ptr, OptionSettingCallback changedCallback, OptionSettingCallback committedCallback, OptionSettingCallback updateCallback, OptionSettingCallback restoreDefaultsCallback, OptionSettingCallback customDisplayCallback, OptionSettingCallback customInputCallback, bool enabled);

// handy customDisplayCallback
void displayFloatAsPercentage(OptionSetting *setting);
void inputFloatPercentage(OptionSetting *setting);
void inputIntPercentageScaled(OptionSetting *setting);

// Reload options, if loaded
void  OptionsReload(void);

typedef bool (*OptionsShouldHideCallback)(OptionSetting *setting);

void options_HideOption(SA_PARAM_NN_STR const char* pchCategory, SA_PARAM_NN_STR const char *pOptionName, SA_PARAM_OP_VALID OptionsShouldHideCallback cb);
void options_UnhideOption(SA_PARAM_NN_STR const char* pchCategory, SA_PARAM_NN_STR const char *pOptionName);

SA_RET_OP_VALID OptionSetting *gclOptionsExpr_GetSetting(const char *pchCategory, const char *pchSetting);
void OptionCategoryMoveToPosition(const char* pcName, int iPos);

extern bool g_bOptionsInit;

#endif
