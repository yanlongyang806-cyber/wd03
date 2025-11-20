#include "estring.h"
#include "serverlib.h"
#include "GlobalTypes.h"
#include "StashTable.h"
#include "InstancedStateMachine.h"
#include "timing.h"
#include "url.h"
#include "AslJobManager.h"
#include "autogen/aslJobManagerHttp_c_ast.h"
#include "aslJobManagerJobQueues.h"
#include "httpxpathsupport.h"

AUTO_STRUCT;
typedef struct JobManagerOverview
{
	char *pPaused; AST(ESTRING FORMATSTRING(HTML_NO_HEADER = 1))
	char *pPause; AST(ESTRING FORMATSTRING(command=1))
	char *pUnPause; AST(ESTRING FORMATSTRING(command=1))
	JobManagerJobQueue **ppQueues;
	char *pCompletedJobs; AST(ESTRING FORMATSTRING(HTML = 1, HTML_NO_HEADER = 1))
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
} JobManagerOverview;




void JobManager_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static JobManagerOverview overview = {0};

	eaDestroy(&overview.ppQueues);
	if (JobManagerIsPaused())
	{
		estrPrintf(&overview.pPaused, "The Job Manager IS PAUSED");
		estrPrintf(&overview.pUnPause, "PauseJobManager 0 $NORETURN $NOCONFIRM");
		estrClear(&overview.pPause);
	}
	else
	{
		estrPrintf(&overview.pPaused, "The Job Manager is NOT paused");
		estrPrintf(&overview.pPause, "PauseJobManager 1 $NORETURN $NOCONFIRM");
		estrClear(&overview.pUnPause);
	}

	FOR_EACH_IN_STASHTABLE(hJobQueuesByName, JobManagerJobQueue, pQueue)
		eaPush(&overview.ppQueues, pQueue);
	FOR_EACH_END


	estrPrintf(&overview.pCompletedJobs, "<a href=\"%s%sCompletedJobs\">Completed Jobs (if any)</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME);

	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));



	*ppTPI = parse_JobManagerOverview;
	*ppStruct = &overview;
}

#include "autogen/aslJobManagerHttp_c_ast.c"
