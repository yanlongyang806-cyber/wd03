#include "ControllerScriptingSupport.h"
#include "estring.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);) ;

#undef ControllerScript_Failedf
void ControllerScript_Failedf(const char *query, ...)
{
	va_list va;
	char *commandstr = NULL;

	estrStackCreate(&commandstr);

	va_start( va, query );
	estrConcatfv(&commandstr,query,va);
	va_end( va );

	ControllerScript_Failed(commandstr);
	estrDestroy(&commandstr);

}
void DEFAULT_LATELINK_ControllerScript_Succeeded(void)
{
	assertmsg(0, "ControllerScript_Succeeded not actually defined");
}
void DEFAULT_LATELINK_ControllerScript_Failed(char *pFailureString)
{
	assertmsg(0, "ControllerScript_Succeeded not actually defined");
}


void DEFAULT_LATELINK_ControllerScript_TemporaryPauseInternal(int iNumSeconds, char *pReason)
{
	assertmsg(0, "ControllerScript_TemporaryPauseInternal not actually defined");
}

void ControllerScript_TemporaryPause(int iNumSeconds, FORMAT_STR const char *pReasonFmt, ...)
{
	char *pReason = NULL;
	estrGetVarArgs(&pReason, pReasonFmt);

	ControllerScript_TemporaryPauseInternal(iNumSeconds, pReason);

	estrDestroy(&pReason);
}




