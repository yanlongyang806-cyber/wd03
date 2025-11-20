#include "transactionServer.h"
#include "autogen/TransactionServer_Http_c_ast.h"
#include "estring.h"
#include "serverlib.h"

AUTO_STRUCT;
typedef struct TransServerOverview
{
	int iNumActiveConnections;
	int iNumActiveTransactions;
	U64 iNumCompletedNonSucceededTransactions;
	U64 iNumSucceededTransactions;

	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))
	AST_COMMAND("Reset timing counts for all transactions", "ResetAllRecentTimeCounts $CONFIRM(This will reset the counts of how many transactions of each type have completed in 1 second, 2 seconds, etc.)")

} TransServerOverview;


void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static TransServerOverview overview;

	overview.iNumActiveConnections = gTransactionServer.iNumActiveConnections;
	overview.iNumActiveTransactions = gTransactionServer.iNumActiveTransactions;
	overview.iNumCompletedNonSucceededTransactions = gTransactionServer.iNumCompletedNonSucceededTransactions;
	overview.iNumSucceededTransactions = gTransactionServer.iNumSucceededTransactions;

	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));


	*ppTPI = parse_TransServerOverview;
	*ppStruct = &overview;
}

#include "autogen/TransactionServer_Http_c_ast.c"
