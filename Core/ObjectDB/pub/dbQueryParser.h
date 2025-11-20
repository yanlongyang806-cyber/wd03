/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
*
*	ObjectDB query parsing functions
*	 This is just a dumb parser. 
*     Feel free to make it smart enough to handle more complex statements.
*
***************************************************************************/

#ifndef DBQUERY_PARSER_H_
#define DBQUERY_PARSER_H_

#include "stashtable.h"

AUTO_ENUM;
typedef enum QueryType {
	QUERYTYPE_SELECT = 0,	//not-modifying will be false.
	QUERYTYPE_UPDATE,
	QUERYTYPE_DELETE,
	QUERYTYPE_INSERT
} QueryType;

//XXXXXXX: This struct should be split up and reorganized.

//This struct is just an agglomeration of expression instructions for the query processor.
AUTO_STRUCT;
typedef struct AgglomeratedQuery {
	QueryType type;
	STRING_EARRAY field_list;
	char *expr_from;		AST(ESTRING)
	char *expr_where;		AST(ESTRING)
	union {
		char *expr_orderby;		AST(ESTRING)
		char *expr_having;		NO_AST
	};
	char *parser_message;	AST(ESTRING)
} AgglomeratedQuery;

bool dbMakeQueryFromString(const char *queryString, AgglomeratedQuery **outQuery);
void dbDestroyQuery(AgglomeratedQuery **inQuery);

static bool dbParseQuery(const char *queryString, AgglomeratedQuery *query);
static void consumeToken(char **input, char **token, char delims[]);

static bool parseSelect(char **input, char **tokenE, AgglomeratedQuery *query);
static bool parseUpdate(char **input, char **tokenE, AgglomeratedQuery *query);
static bool parseDelete(char **input, char **tokenE, AgglomeratedQuery *query);
static bool parseInsert(char **input, char **tokenE, AgglomeratedQuery *query);

static void parseFieldList(char *pInString, AgglomeratedQuery *query);
static void parseExprList(char *pInString, AgglomeratedQuery *query);

bool dbQueryHasField(AgglomeratedQuery *query, const char *field);

#endif //DBQUERY_PARSER_H_
