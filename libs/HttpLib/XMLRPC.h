#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/


#include "autogen/xmlrpc_h_ast.h"

#ifndef _XBOX

//This file implements an XMLRPC parser with libexpat.
// KNN 20090218
// tags: xml parsing xmlrpc

#include "stdtypes.h"
#include "utils.h"
#include "EString.h"

//For methodcall conversion to command system.
#include "cmdparse.h"

typedef struct XMLStruct XMLStruct;
typedef struct XMLArray XMLArray;

AUTO_ENUM;
typedef enum XMLRPCFault
{
	XMLRPC_FAULT_NONE = 0,
	XMLRPC_FAULT_NOMETHOD,
	XMLRPC_FAULT_NOCOMMAND,
	XMLRPC_FAULT_BADPARAMS,
	XMLRPC_FAULT_COMMANDFAILURE,
	XMLRPC_FAULT_RETURNFAILURE,
	XMLRPC_FAULT_EXECUTIONFAILURE,
	XMLRPC_FAULT_AUTHFAILURE,

	//MCP monitoring faults
	XMLRPC_FAULT_XMLRPCUNSUPPORTED = 8,
	XMLRPC_FAULT_SLOWCOMMAND,			//not actually a fault
	XMLRPC_FAULT_NOSLOWCOMMANDS,
	XMLRPC_FAULT_SERVERNOTREADY,
	XMLRPC_FAULT_SERVERDOESNOTEXIST,
	XMLRPC_FAULT_RESPONSETIMEOUT,
	XMLRPC_FAULT_ENTITYOWNED,
	XMLRPC_FAULT_ENTITYNOTFOUND,
	XMLRPC_FAULT_REMOTEFAILURE,

	XMLRPC_FAULT_COUNT,						EIGNORE
} XMLRPCFault;

typedef U64 OptionFlags;

AUTO_ENUM;
typedef enum XMLRPCType
{
	XMLRPC_Uninitialized = 0,
	XMLRPC_State,
	XMLRPC_MethodCall,
	XMLRPC_MethodResponse,
	XMLRPC_MethodName,
	XMLRPC_Params,
	XMLRPC_Param,
	XMLRPC_Value,
	XMLRPC_Int,
	XMLRPC_Boolean,
	XMLRPC_String,
	XMLRPC_Double,
	XMLRPC_DateTime_iso8601,
	XMLRPC_Base64,
	XMLRPC_Struct,
	XMLRPC_StructString,	//not a type, just a string to be non-converted for output.
	XMLRPC_Members,
	XMLRPC_Member,
	XMLRPC_Name,
	XMLRPC_Array,
	XMLRPC_Data,
	XMLRPC_Entity,			//Entity *magic
	XMLRPC_Fault,
	XMLRPC_ENUM_END,				EIGNORE
} XMLRPCType;

AUTO_STRUCT;
typedef struct XMLValue
{
	XMLRPCType	type;
	S64			value_int;				AST( NAME(int) )
	bool		value_boolean;			AST( NAME(boolean) )
	char*		value_string;			AST(ESTRING, NAME(String))
	float		value_double;			AST( NAME(Double) )
	U32			value_dateTime_iso8601;	AST( NAME(dateTime_iso8601) )
	char*		value_base64;			AST(ESTRING, NAME(base64))
	XMLStruct*	value_struct;			AST( NAME(Struct) )
	XMLArray*	value_array;			AST( NAME(Array) )
} XMLValue;

AUTO_STRUCT;
typedef struct XMLMember
{
	char*		name;		AST(ESTRING)
	XMLValue*	value;
} XMLMember;

AUTO_STRUCT;
typedef struct XMLStruct
{
	EARRAY_OF(XMLMember) members;
} XMLStruct;

AUTO_STRUCT;
typedef struct XMLArray
{
	EARRAY_OF(XMLValue) data;
} XMLArray;

AUTO_STRUCT;
typedef struct XMLParam
{
	XMLValue*	value;
} XMLParam;

AUTO_STRUCT;
typedef struct XMLMethodCall
{
	char*		methodName;		AST(ESTRING)
	EARRAY_OF(XMLParam) params;

} XMLMethodCall;

AUTO_STRUCT;
typedef struct XMLFault
{
	XMLValue*	value;
} XMLFault;

AUTO_STRUCT;
typedef struct XMLMethodResponse
{
	EARRAY_OF(XMLParam) params;
	XMLFault*	fault;
	U32			slowID;
} XMLMethodResponse;

AUTO_STRUCT;
typedef struct XMLParseState
{
	XMLMethodCall *methodCall;
	
	ObjectPath *path;
	char *characters;			AST(ESTRING)
	int *tags;
} XMLParseState;

AUTO_STRUCT;
typedef struct XMLParseInfo
{
	XMLParseState *state;
	char *error;				AST(ESTRING)
	char *clientname;			AST(ESTRING)
} XMLParseInfo;


//Entity* magic links
LATELINK;
void XMLRPC_LoadEntity(CmdContext *slowContext, char *ent);
LATELINK;
XMLMethodResponse* XMLRPC_DispatchEntityCommand(CmdContext *slowContext, char *name, ContainerID iVirtualShardID);

void xmlrpc_insert_entity_param(XMLMethodCall *xmethod, U32 entid);

//Parse the request into an XMLMethodCall
XMLParseInfo * XMLRPC_Parse(const char *string, const char *client);

//Convenience method to unwrap the XMLMethodCall if parsing was successful.
XMLMethodCall * XMLRPC_GetMethodCall(XMLParseInfo *pi);

//Build an XMLRPC response string
// *pass NULL to cmd_context to build a generic error response
XMLMethodResponse* XMLRPC_BuildMethodResponse(CmdContext *cmd_context, XMLRPCFault faultCode, char *message);

//Attempt to call the auto command described by this xmlmethodcall. 
//Return values are converted to xmlrpc intermediary structs.
XMLMethodResponse* XMLRPC_ConvertAndExecuteCommand(XMLMethodCall *method, int accesslevel, char **categories, 
	const char *pAuthNameAndIP, CmdSlowReturnForServerMonitorInfo *slowReturnInfo);

//Convert the response to xml text.
void XMLRPC_WriteOutMethodResponse(XMLMethodResponse *response, char **estrout);

// Turn on or off the XML-RPC errorf handler
void XMLRPC_UseErrorHandler(bool bShouldUse);

#endif //ndef _XBOX

typedef struct TypeDescription TypeDescription;

AUTO_STRUCT;
typedef struct TypeDescription
{
	char *name;				AST(ESTRING)
	char *type;				AST(ESTRING)
	EARRAY_OF(TypeDescription) fields;
} TypeDescription;

AUTO_STRUCT;
typedef struct MethodIntrospection
{
	char *name;				AST(ESTRING)
	char *origin;			AST(ESTRING)
	char *comment;			AST(ESTRING NAME(Comment))
	
	EARRAY_OF(TypeDescription) parameters;

} MethodIntrospection;

//Adding FORMATSTRING(XML_UNWRAP_ARRAY = 1) to a list AST makes XMLRPC output the struct as only the array member.
//These auto_structs maybe used for convenience, or you may simply tag your own auto_structs.

AUTO_STRUCT;
typedef struct XMLStringList
{
	char **list;		AST(FORMATSTRING(XML_UNWRAP_ARRAY = 1))
} XMLStringList;

AUTO_STRUCT;
typedef struct XMLIntList
{
	int *list;		AST(FORMATSTRING(XML_UNWRAP_ARRAY = 1))
} XMLIntList;

AUTO_STRUCT;
typedef struct IntStringPair
{
	int i4;			AST(KEY)
	char *string;	AST(STRUCTPARAM ESTRING NAME(String))
} IntStringPair;

AUTO_STRUCT;
typedef struct XMLBase64Test
{
	char *data;		AST(ESTRING FORMATSTRING(XML_ENCODE_BASE64 = 1))
} XMLBase64Test;

AUTO_STRUCT;
typedef struct XMLIntStringList
{
	EARRAY_OF(IntStringPair) list;		AST(FORMATSTRING(XML_DECODE_KEY = 1))
} XMLIntStringList;

AUTO_STRUCT;
typedef struct XMLUOTest
{
	IntStringPair *isp;			AST(UNOWNED)
}XMLUOTest;


//given a struct, generates the response string for an XMLRPC command with that struct
//as the return value
LATELINK;
void XMLRPC_WriteSimpleStructResponse(char **ppOutString, void *pStruct, ParseTable *pTPI);


//used by XMLRPC code when we know that we don't have enough logical arguments. Checks if all the "trailing" args have default
//values, and if so, fills them in. Returns true on success
bool XMLRPC_FillInDefaultXMLArgsIfPossible(Cmd *pCmd, XMLParam ***pppParams);
