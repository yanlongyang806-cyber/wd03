#pragma once

#include "cmdparse.h"
#include "../../serverlib/AutoSettings.h"







void ControllerAutoSetting_NormalOperationStarted(void);
bool ControllerAutoSetting_SystemIsActive(void);
void Controller_DumpAllAutoSettingsAndQuit(void);

char **ControllerAutoSettings_GetCommandStringsForServerType(GlobalType eServerType);

extern StashTable gControllerAutoSettingCategories;


