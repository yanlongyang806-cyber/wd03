#include "aslLoginServer.h"
#include "autogen/aslLoginServerHttp_c_ast.h"
#include "estring.h"
#include "serverlib.h"
#include "GlobalTypes.h"
#include "StashTable.h"
#include "InstancedStateMachine.h"
#include "timing.h"
#include "GamePermissionsCommon.h"
#include "aslLogin2_Error.h"

extern LoginServerState gLoginServerState;
extern U32 guMaxLogins;

extern U32 guPendingLogins;

AUTO_STRUCT AST_FORMATSTRING(HTML_NOTES_AUTO=1);
typedef struct LoginServerOverview
{
	const char *Reject_All_Logins;
	U32 Pending_Logins;
	U32 Total_Players;

	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))

    GamePermissionDefs *gamePermissionDefs;

    STRING_EARRAY recentLogLines;
} LoginServerOverview;


void LoginServer_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static LoginServerOverview overview = {0};
    int i;
    int logSize;
    int srcIndex;
    char ** recentLogLines = aslLogin2_GetRecentLogLines();

    overview.gamePermissionDefs = NULL;
	StructReset(parse_LoginServerOverview, &overview);
	overview.Reject_All_Logins = strdup(ControllerReportsShardLocked() ? "true" : "false");
	overview.Pending_Logins = guPendingLogins;

	overview.Total_Players = giCurLinkCount;

	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

    overview.gamePermissionDefs = &g_GamePermissions;

    // Free the previous log contents if any.
    logSize = eaSize(&overview.recentLogLines);
    for ( i = 0; i < logSize; i++ )
    {
        free(overview.recentLogLines[i]);
    }

    // Set the size of the recent logs array.
    logSize = eaSize(&recentLogLines);
    eaSetSize(&overview.recentLogLines, logSize);

    // Copy log lines into overview struct.
    srcIndex = aslLogin2_GetNextLogIndex();
    if ( srcIndex >= logSize )
    {
        srcIndex = 0;
    }
    for ( i = 0; i < logSize; i++ )
    {
        overview.recentLogLines[i] = strdup(recentLogLines[srcIndex]);
        srcIndex++;
        if ( srcIndex >= logSize )
        {
            srcIndex = 0;
        }
    }

	*ppTPI = parse_LoginServerOverview;
	*ppStruct = &overview;
	
}

#include "autogen/aslLoginServerHttp_c_ast.c"
