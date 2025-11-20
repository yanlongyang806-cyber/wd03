
#include "stdtypes.h"
#include "Prefs.h"
#include "GraphicsLib.h"
#include "gclOptions.h"
#include "GfxSettings.h"
#include "ContinuousBuilderSupport.h"

#define GAMMA_CALIBRATION_VERSION 1

AUTO_COMMAND ACMD_NAME("GammaCalibration_Reset") ACMD_ACCESSLEVEL(0);
void GammaCalibration_Reset()
{
	GamePrefStoreInt("HasSetGamma", 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GammaCalibration_HasGammaSet");
bool GammaCalibration_HasGammaSet()
{
	return g_isContinuousBuilder || (GamePrefGetInt("HasSetGamma", 0) == GAMMA_CALIBRATION_VERSION);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GammaCalibration_GammaFinalized");
void GammaCalibration_GammaFinalize()
{
	OptionSetting *setting = gclOptionsExpr_GetSetting("Graphics", "Gamma");
	if (SAFE_MEMBER(setting, cbCommitted))
	{
		setting->cbCommitted(setting);
	}
	GamePrefStoreInt("HasSetGamma", GAMMA_CALIBRATION_VERSION);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GammaCalibration_SetGamma");
void GammaCalibration_SetGamma(float fValue)
{
	OptionSetting *setting = gclOptionsExpr_GetSetting("Graphics", "Gamma");
	if (setting)
	{
		fValue = CLAMPF(fValue, 0, 1.9);
		setting->fFloatValue = fValue;
		gfxSettingsSetGamma(2 - fValue);
	}
}