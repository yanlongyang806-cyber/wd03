#pragma once

#define MAX_TICKET_REQUEST 100
#include "ticketenums.h"

//Fetching User names and IDs
AUTO_STRUCT;
typedef struct TT_NameAndId
{
	U32 id;						AST(KEY)
	char *name;				AST(STRUCTPARAM ESTRING)
} TT_NameAndId;


AUTO_STRUCT;
typedef struct TT_NameAndIdList
{
	EARRAY_OF(TT_NameAndId) list;			AST(FORMATSTRING(XML_DECODE_KEY = 1))
} TT_NameAndIdList;

AUTO_STRUCT;
typedef struct TT_KeyValue
{
	char *key;					AST(KEY ESTRING)
	char *value;				AST(STRUCTPARAM ESTRING)
} TT_KeyValue;

AUTO_STRUCT;
typedef struct TT_KeyValueList
{
	EARRAY_OF(TT_KeyValue) list;			AST(FORMATSTRING(XML_DECODE_KEY = 1))
} TT_KeyValueList;

AUTO_STRUCT;
typedef struct TT_IdList
{
	U32 *uIDs;
} TT_IdList;

AUTO_STRUCT;
typedef struct TT_BatchActionStruct
{
	int iPriority; 
	char *assignTo;
	TicketStatus eStatus;
	TicketInternalStatus eInternalStatus;
	TicketVisibility eVisibility; // X-1 to get the actual TicketVisibilty enum value (0-1 = -1 = Unknown)
	char *mainCategory; 
	char *subcategory;
	char *jiraKey; 
	char *kbSolution; 
	char *rcSolution; 
	char *ticketResponse;
} TT_BatchActionStruct;


#include "autogen/ticketapi_h_ast.h"