#pragma once
GCC_SYSTEM

//These functions are called on servers other than the controller (well, typically other than the 
//controller) that are interacting with controller scripting. In particular, when you execute a
//CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT, your command must call one of these when it completes.
//
//They are latelinked because they are different on the client
//
//The main .h file for controller scripting is Controller_Scripting.h

LATELINK;
void ControllerScript_Succeeded(void);

LATELINK;
void ControllerScript_Failed(char *pFailureString);

LATELINK;
void ControllerScript_TemporaryPauseInternal(int iNumSeconds, char *pReason);

void ControllerScript_TemporaryPause(int iNumSeconds, FORMAT_STR const char *pReasonFmt, ...);

void ControllerScript_Failedf(FORMAT_STR const char *pFormat, ...);
#define ControllerScript_Failedf(pFormat, ...) ControllerScript_Failedf(FORMAT_STRING_CHECKED(pFormat), __VA_ARGS__)
