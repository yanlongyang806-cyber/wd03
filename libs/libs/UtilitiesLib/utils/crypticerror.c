/***************************************************************************
****************************************************************************
*PLEASE READ CRYPTICERROR.H BEFORE TOUCHING THIS FILE - YOU HAVE BEEN WARNED
****************************************************************************
***************************************************************************/

#include "crypticerror.h"
#include "utilitiesLib.h"

#include "wininclude.h"
#include <string.h>

static char sVariableAddresses[4096] = "CRYPTICERROR";
static char sCommandLineArgs[128] = {0};

void ceClear()
{
	strcpy(sVariableAddresses, "CRYPTICERROR");
}

void ceAddStringPtr(const char *name, const char *str)
{
	char temp[512];

	if(str == NULL)
		str = "";

	sprintf(temp, "|S:%"FORM_LL"d:%d:%s", (U64)str, strlen(str), name);
	temp[511] = 0;

	strncat_s(sVariableAddresses, ARRAY_SIZE_CHECKED(sVariableAddresses), temp, _TRUNCATE);
}

void ceAddPtr(const char *name, void *ptr)
{
	char temp[512];

	sprintf(temp, "|P:%"FORM_LL"d:%s", (U64)ptr, name);
	temp[511] = 0;

	strncat_s(sVariableAddresses, ARRAY_SIZE_CHECKED(sVariableAddresses), temp, _TRUNCATE);
}

void ceAddInt(const char *name, int i)
{
	char temp[512];
	sprintf(temp, "|I:%d:%s", i, name);
	temp[511] = 0;

	strncat_s(sVariableAddresses, ARRAY_SIZE_CHECKED(sVariableAddresses), temp, _TRUNCATE);
}

char * ceCalcArgs()
{
#if !PLATFORM_CONSOLE
	sprintf(sCommandLineArgs, 
		"-SetSharedMachineIndex %d -ceProcessId %d -ceThreadId %d -ceArgs %"FORM_LL"d:%d",
		UtilitiesLib_GetSharedMachineIndex(),
		GetCurrentProcessId(), GetCurrentThreadId(),
		(U64)sVariableAddresses, strlen(sVariableAddresses));
#endif
	return sCommandLineArgs;
}
