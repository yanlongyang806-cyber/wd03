#pragma once
GCC_SYSTEM

#include "NotifyCommon.h"

void GameDialogGenericMessage(const char *pchTitle, const char *pchBody);
void GameDialogError(const char *pchBody);
void GameDialogClearType(NotifyType eType);
void GameDialogTyped(NotifyType eType, const char *pchTitle, const char *pchBody);
bool GameDialogPopup(void);