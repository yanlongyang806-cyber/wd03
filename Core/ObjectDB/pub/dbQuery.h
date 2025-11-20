/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
*
*	dbQuery adds support for querying the ObjectDB in an SQL-like fashion.
*
***************************************************************************/
#ifndef DBQUERY_H_
#define DBQUERY_H_

#include "HttpXpathSupport.h"
#include "dbQueryParser.h"
#include "HybridObj.h"

#define DBQUERY_ERROR "DBAQL Error:"

AUTO_STRUCT;
typedef struct ResultField {
	char *field;
} ResultField;

AUTO_STRUCT;
typedef struct Result {
	int row_index;
} Result;

AUTO_STRUCT;
typedef struct ResultSet {
	bool bSuccess;
	char *pMessage;		AST(ESTRING)

	U32	row_count;			//number of rows in the resultset, usually just the size of the earray
	EARRAY_OF(Result) ppRows;	AST(INDEX_DEFINE)
} ResultSet;

typedef enum {
	IterateObjects = 0,
	DirectGet,
	IndexGet
} GetMethod;

//http: //knishikawa/viewxpath=ObjectDB[0].query.<type>&q=<escaped query string>
#define DB_QUERY_DOMAIN_NAME ".query"
#define DB_QUERY_DEFAULT_GLOBALTYPE "ENTITYPLAYER"

//only parameters prefixed by "svr" are forwarded to the server from the MCP ServerMon
#define DB_QUERY_URL_PARAMETER "svrQuery"

//ObjectDB.query xpath support
void dbRegisterQueryXpathDomain(void);
bool dbQueryXpathProcessingCB(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags);

void dbRunQuery(const char *queryString, ResultSet *pRSinout, ParseTable **pTPIout, HybridObjHandle **pReturnHandleOut);

//Build create a new resultset TPI with a custom result tpi.
static ParseTable * makeResultSetTPI(ParseTable* innerTPI, bool *isIndexedOut);
static void destroyResultSetTPI(ParseTable *rsTPI);

#endif //DBQUERY_H_