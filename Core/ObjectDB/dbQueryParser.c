/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "dbQueryParser.h"
#include "EString.h"

#include "dbQueryParser_h_ast.h"

static char whitespace[] = " \t\r\n";
static char singleQuote[] = "'";
static char doubleQuote[] = "\"";

bool dbMakeQueryFromString(const char *queryString, AgglomeratedQuery **outQuery)
{
	if (!*outQuery)
	{
		*outQuery = StructCreate(parse_AgglomeratedQuery);
	}
	else 
	{
		StructReset(parse_AgglomeratedQuery, *outQuery);
	}
	if (dbParseQuery(queryString, *outQuery))
	{
		return true;
	}
	else
	{
		return false;
	}
}

void dbDestroyQuery(AgglomeratedQuery **inQuery)
{
	if (*inQuery)
	{
		StructDestroy(parse_AgglomeratedQuery, *inQuery);
	}
	*inQuery = 0;
}

static bool dbParseQuery(const char *queryString, AgglomeratedQuery *query)
{
	char *buffer = estrStackCreateFromStr(queryString);
	char *token = 0;
	bool result = false;
	
	estrStackCreate(&token);
	
	consumeToken(&buffer, &token, whitespace);

	if (strstri(token, "SELECT") != NULL) {
		result = parseSelect(&buffer, &token, query);
	} 
	else if (strstri(token, "UPDATE") != NULL) {
		result = parseUpdate(&buffer, &token, query);
	} 
	else if (strstri(token, "DELETE") != NULL) {
		result = parseDelete(&buffer, &token, query);
	} 
	else if (strstri(token, "INSERT") != NULL) {
		result = parseInsert(&buffer, &token, query);
	}
	else {
		estrPrintf(&query->parser_message, "Could not parse query: %s", queryString);
	}
	
	estrDestroy(&buffer);
	estrDestroy(&token);
	return result;
}

static void consumeToken(char **input, char **token, char delims[])
{
	char *rest = NULL;
	char *next = strtok_r(*input, delims, &rest);
	unsigned int toklen;
	estrClear(token);
	if (!next || !next[0]) return;

	estrConcatf(token, "%s", next);

	//clear the previous tokens.
	toklen = rest - *input;
	while (rest > *input && !rest[-1]) *(--rest) = ' ';
	estrRemove(input, 0, toklen);
}

//This select syntax parser is super jenky.
static bool parseSelect(char **input, char **token, AgglomeratedQuery *query)
{
	char *place;
	bool result = false;
	estrClear(token);
	query->type = QUERYTYPE_SELECT;
	
	//XXXXXX: This is just a naive implementation. if you need to make parsing better, you'd do it here.
	
	if ((place = strstri(*input, "LIMIT")) != NULL)
	{	//XXXXXX:parseWhere should be broken out into a function.
		char *expr = place + strlen("LIMIT");
		//estrPrintf(&query->expr_where, "%s", expr);
		//estrTrimLeadingAndTrailingWhitespace(&query->expr_where);
		place[0] = '\0';
	}

	if ((place = strstri(*input, "ORDER BY")) != NULL)
	{	//XXXXXX:parseWhere should be broken out into a function.
		char *expr = place + strlen("ORDER BY");
		//estrPrintf(&query->expr_where, "%s", expr);
		//estrTrimLeadingAndTrailingWhitespace(&query->expr_where);
		place[0] = '\0';
	}

	if ((place = strstri(*input, "WHERE")) != NULL)
	{	//XXXXXX:parseWhere should be broken out into a function.
		char *expr = place + strlen("WHERE");
		estrPrintf(&query->expr_where, "%s", expr);
		estrTrimLeadingAndTrailingWhitespace(&query->expr_where);
		place[0] = '\0';
	}

	if ((place = strstri(*input, "FROM")) != NULL)
	{	//XXXXXX:parseFrom should be broken out into a function.
		char *expr = place + strlen("FROM");
		estrPrintf(&query->expr_from, "%s", expr);
		estrTrimLeadingAndTrailingWhitespace(&query->expr_from);
		place[0] = '\0';
	}

	//XXXXXX: Needs better SELECT parsing.
	estrTrimLeadingAndTrailingWhitespace(input);
	if (estrLength(input) > 0)
	{
		parseFieldList(*input, query);
	}
	result = true;
	
	return result;
}

static bool parseUpdate(char **input, char **token, AgglomeratedQuery *query)
{
	char *place;
	bool result = false;
	estrClear(token);
	query->type = QUERYTYPE_UPDATE;
	
	//XXXXXX: This is just a naive implementation. if you need to make parsing better, you'd do it here.

	if ((place = strstri(*input, "WHERE")) != NULL)
	{	//XXXXXX:parseWhere should be broken out into a function.
		char *expr = place + strlen("WHERE");
		estrPrintf(&query->expr_where, "%s", expr);
		estrTrimLeadingAndTrailingWhitespace(&query->expr_where);
		place[0] = '\0';
	}

	if ((place = strstri(*input, "SET")) != NULL)
	{	//XXXXXX:parseFrom should be broken out into a function.
		char *expr = place + strlen("SET");
		parseExprList(expr, query);

		place[0] = '\0';
	}

	estrTrimLeadingAndTrailingWhitespace(input);
	estrPrintf(&query->expr_from, "%s", *input);

	result = true;
	
	return result;
}

static bool parseDelete(char **input, char **token, AgglomeratedQuery *query)
{
	estrClear(token);
	query->type = QUERYTYPE_DELETE;
	
	//unimplemented, return false.
	estrPrintf(&query->parser_message, "DELETE queries are not implemented.");
	return false;
}

static bool parseInsert(char **input, char **token, AgglomeratedQuery *query)
{
	estrClear(token);
	query->type = QUERYTYPE_INSERT;

	//unimplemented, return false.
	estrPrintf(&query->parser_message, "INSERT queries are not implemented.");
	return false;
}


#define charIsOkForFieldName(c) (isalnum(c) || (c) == '_' || (c) == ':' || (c) == '.' || (c) == '[' || (c) == ']' || (c) == '*' || (c) == '"')
static void parseFieldList(char *pInString, AgglomeratedQuery *query)
{
	const char *pReadHead = pInString;
	char *pTempString = NULL; //estring

	estrStackCreate(&pTempString);

	do
	{
		while (*pReadHead && !charIsOkForFieldName(*pReadHead))
		{
			pReadHead++;
		}

		if (!(*pReadHead))
		{
			estrDestroy(&pTempString);
			return;
		}

		while (*pReadHead && charIsOkForFieldName(*pReadHead))
		{
			estrConcatChar(&pTempString, *pReadHead);
			pReadHead++;
		}

		assert(pTempString);

		eaPush(&query->field_list, strdup(pTempString));
		if (strcmp(pTempString, "*") == 0)
		{
			eaClear(&query->field_list);
			eaPush(&query->field_list, strdup(pTempString));
			break;
		}
		//stashAddInt(query->field_list, pTempString, 1, false);
		estrSetSize(&pTempString, 0);
	} while (*pReadHead);

	estrDestroy(&pTempString);
}

//explodes the string by commas and pushes the resultant pieces into field_list
static void parseExprList(char *pInString, AgglomeratedQuery *query)
{
	char *pReadHead = pInString;
	char *pTempString = NULL;

	while (pReadHead && pReadHead[0])
	{
		pTempString = strchr(pReadHead, ',');
		if (pTempString)
		{
			pTempString[0] = '\0';
			pTempString++;
			while (pReadHead[0] == ' ' || pReadHead[0] == '\t' || pReadHead[0] == '\n') pReadHead++;	//skip spaces
			eaPush(&query->field_list, strdup(pReadHead));
			pReadHead = pTempString;
			pTempString[-1] = ',';
		}
		else
		{
			while (pReadHead[0] == ' ' || pReadHead[0] == '\t' || pReadHead[0] == '\n') pReadHead++;
			if (strlen(pReadHead) > 0)
			{
				eaPush(&query->field_list, strdup(pReadHead));
			}
			pReadHead = NULL;
		}
	}
}

//
////Only supports first level 
//bool dbQueryHasField(AgglomeratedQuery *query, const char *field)
//{
//	int found = 0;
//	stashFindInt(query->field_list, field, &found);
//	if (found) return true;
//	else
//	{
//		char *buf = 0;
//		char *marker;
//		estrStackCreateSize(&buf, (unsigned int)strlen(field) + 2);
//		marker = buf;
//		do {
//			estrClear(&buf);
//			estrConcatf(&buf, "%s", field);
//
//			marker = strchr(marker, '.');
//			if (marker != NULL)
//			{
//				marker++;
//				marker[0] = '*';
//				marker[1] = '\0';
//				stashFindInt(query->field_list, buf, &found);
//				if (found) break;
//			}
//		} while (marker);
//		estrDestroy(&buf);
//		return found;
//	}
//}

#include "dbQueryParser_h_ast.c"