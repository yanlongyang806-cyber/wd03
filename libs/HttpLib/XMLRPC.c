/***************************************************************************



***************************************************************************/

//This file implements an XMLRPC parser with libexpat.
// KNN 20090218
// tags: xml parsing xmlrpc

#include "XMLRPC.h"

#ifndef _XBOX

//link and configure expat
#include "XMLParsing.h"

//For parsetable conversion functions
#include "tokenstore.h"
#include "textparser.h"
#include "structinternals.h"

//Use compiled ObjectPath functions
#include "textparserUtils.h"

//String type conversion and parsing
#include "StringUtil.h"

//for dateTime.iso8601 string conversion
#include "timing.h"

#include "autogen/xmlrpc_c_ast.h"

//For interlocked variable access.
#include "windefinclude.h"

//For security alerts
#include "alerts.h"
#include "StringCache.h"
#include "objtransactions.h"

//for latelink definition of XMLRPC_WriteSimpleStructResponse, the horror!
#include "UtilitiesLib.h"

static bool gbUseXMLRPCErrorHandler = true;

	



static const char *xmlrpc_type_to_name(XMLRPCType type)
{
	if (type < 0 || type >= XMLRPC_ENUM_END) return "XMLRPC_InvalidType";
	switch (type)
	{
	case XMLRPC_State: return "State";
	case XMLRPC_MethodCall: return "MethodCall";
	case XMLRPC_MethodResponse: return "MethodResponse";
	case XMLRPC_MethodName: return "MethodName";
	case XMLRPC_Params: return "Params";
	case XMLRPC_Param: return "Param";
	case XMLRPC_Value: return "Value";
	case XMLRPC_Int: return "Int";
	case XMLRPC_Boolean: return "Boolean";
	case XMLRPC_String: return "String";
	case XMLRPC_Double: return "Double";
	case XMLRPC_DateTime_iso8601: return "DateTime_iso8601";
	case XMLRPC_Base64: return "Base64";
	case XMLRPC_Struct: return "Struct";
	case XMLRPC_Members: return "Members";
	case XMLRPC_Member: return "Member";
	case XMLRPC_Name: return "Name";
	case XMLRPC_Array: return "Array";
	case XMLRPC_Data: return "Data";
	case XMLRPC_Fault: return "Fault";
	default: return "Uninitialized";
	}
}


//no valid subtags
static void xmlrpc_parse_open(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (el && el[0])
	{
		int faultcount = 0;
		int i;
		ea32Push(&pi->state->tags, XMLRPC_Fault);

		for (i = ea32Size(&pi->state->tags) - 1 ; i >= 0; i-- )
		{
			if (pi->state->tags[i] == XMLRPC_Fault)
				faultcount++;
			else
				break;

			if (faultcount > 5)
			{
				estrPrintf(&pi->error, "XML Parse Error: primitive type <%s> elements shouldn't have children.", el);
				TriggerAlert(allocAddString("XMLRPC_SECURITY_ALERT"), STACK_SPRINTF("XMLRPC Client[%s] is trying to submit a really heinous malformed request. Find them and destroy them.", pi->clientname),
					ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, getHostName(), 0);
			}

		}
	}
}

//inside state
static void xmlrpc_parse_open_state(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "methodCall") == 0)
	{
		XMLParseState *ps = NULL;

		if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &ps, NULL, NULL, 0) || !ps)
		{
			estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
			return;
		}
		ps->methodCall = StructCreate(parse_XMLMethodCall);
		estrPrintf(&ps->methodCall->methodName, "");

		ObjectPathAppend(pi->state->path, "methodCall", LOOKUP_COLUMN, -1, NULL);
		ObjectPathSetTailDescend(pi->state->path, true);
		ea32Push(&pi->state->tags, XMLRPC_MethodCall);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: expecting <methodCall>, found <%s>.", el);
		return;
	}
}

//inside methodcall
static void xmlrpc_parse_open_methodcall(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "methodName") == 0)
	{
		estrClear(&pi->state->characters);
		ea32Push(&pi->state->tags, XMLRPC_MethodName);
	}
	else if (stricmp(el, "params") == 0)
	{
		//TODO: check return value to validate append.
		ObjectPathAppend(pi->state->path, "params", LOOKUP_COLUMN, -1, NULL);
		ObjectPathSetTailDescend(pi->state->path, false);
		ea32Push(&pi->state->tags, XMLRPC_Params);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <methodCall>.", el);
		return;
	}
}

//inside params
static void xmlrpc_parse_open_params(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "param") == 0)
	{
		XMLMethodCall *mc = NULL;
		XMLParam *p = NULL;

		if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &mc, NULL, NULL, 0) || !mc)
		{
			estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
			return;
		}

		p = StructCreate(parse_XMLParam);
		eaPush(&mc->params, p);

		ObjectPathSetTailDescend(pi->state->path, true);
		pi->state->path = ObjectPathAppendIndex(pi->state->path, eaSize(&mc->params)-1, NULL);
		ea32Push(&pi->state->tags, XMLRPC_Param);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <params>.", el);
		return;
	}
}

//inside param
static void xmlrpc_parse_open_param(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "value") == 0) 
	{
		XMLParam *p = NULL;

		if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &p, NULL, NULL, 0) || !p)
		{
			estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
			return;
		}
		if (p->value)
		{
			estrPrintf(&pi->error, "XML Parse Error: <param> tags cannot have multiple <value> children");
			return;
		}

		p->value = StructCreate(parse_XMLValue);

		ObjectPathAppend(pi->state->path, "value", LOOKUP_COLUMN, -1, NULL);
		ea32Push(&pi->state->tags, XMLRPC_Value);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <param>.", el);
		return;
	}
}

//inside value
static void xmlrpc_parse_open_value(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	XMLValue *v = NULL;
	if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
	{
		estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
		return;
	}
	if (v->type != XMLRPC_Uninitialized)
	{
		estrPrintf(&pi->error, 
			"XML Parse Error: value element cannot have multiple children. Already has <%s>, trying to add <%s>",
			xmlrpc_type_to_name(v->type), el);
		return;
	}

	if ((stricmp(el, "int") == 0) || (stricmp(el, "i4") == 0) || (stricmp(el, "i8") == 0)) ea32Push(&pi->state->tags, XMLRPC_Int);
	else if (stricmp(el, "boolean") == 0) ea32Push(&pi->state->tags, XMLRPC_Boolean);
	else if (stricmp(el, "string") == 0) ea32Push(&pi->state->tags, XMLRPC_String);
	else if (stricmp(el, "double") == 0) ea32Push(&pi->state->tags, XMLRPC_Double);
	else if (stricmp(el, "datetime.iso8601") == 0) ea32Push(&pi->state->tags, XMLRPC_DateTime_iso8601);
	else if (stricmp(el, "base64") == 0) ea32Push(&pi->state->tags, XMLRPC_Base64);
	else if (stricmp(el, "struct") == 0)
	{
		XMLStruct *s = NULL;
		v->value_struct = StructCreate(parse_XMLStruct);

		ObjectPathAppend(pi->state->path, "struct", LOOKUP_COLUMN, -1, NULL);
		ObjectPathAppend(pi->state->path, "members", LOOKUP_COLUMN, -1, NULL);
		ObjectPathSetTailDescend(pi->state->path, false);

		ea32Push(&pi->state->tags, XMLRPC_Struct);
	}
	else if (stricmp(el, "array") == 0)
	{
		v->value_array = StructCreate(parse_XMLArray);

		ObjectPathAppend(pi->state->path, "array", LOOKUP_COLUMN, -1, NULL);

		ea32Push(&pi->state->tags, XMLRPC_Array);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <value>.", el);
		return;
	}
	estrClear(&pi->state->characters);
}

//inside struct
static void xmlrpc_parse_open_struct(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "member") == 0) 
	{
		XMLMember *m = NULL;
		XMLStruct *s = NULL;

		if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &s, NULL, NULL, 0) || !s)
		{
			estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
			return;
		}

		m = StructCreate(parse_XMLMember);
		eaPush(&s->members, m);

		ObjectPathSetTailDescend(pi->state->path, true);
		pi->state->path = ObjectPathAppendIndex(pi->state->path, eaSize(&s->members)-1, NULL);
		ea32Push(&pi->state->tags, XMLRPC_Member);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <struct>.", el);
		return;
	}
}

//inside member
static void xmlrpc_parse_open_member(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "name") == 0) 
	{
		estrClear(&pi->state->characters);
		ea32Push(&pi->state->tags, XMLRPC_Name);
	}
	else if (stricmp(el, "value") == 0) 
	{
		XMLMember *m = NULL;
		
		if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &m, NULL, NULL, 0) || !m)
		{
			estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
			return;
		}
		m->value = StructCreate(parse_XMLValue);

		ObjectPathAppend(pi->state->path, "value", LOOKUP_COLUMN, -1, NULL);
		ea32Push(&pi->state->tags, XMLRPC_Value);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <member>.", el);
		return;
	}
}

//inside array
static void xmlrpc_parse_open_array(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "data") == 0)
	{
		ObjectPathAppend(pi->state->path, "data", LOOKUP_COLUMN, -1, NULL);
		ObjectPathSetTailDescend(pi->state->path, false);
		ea32Push(&pi->state->tags, XMLRPC_Data);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <array>.", el);
		return;
	}
}

//inside data
static void xmlrpc_parse_open_data(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr)
{
	if (stricmp(el, "value") == 0)
	{
		XMLArray *a = NULL;
		XMLValue *v = NULL;

		if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &a, NULL, NULL, 0) || !a)
		{
			estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
			return;
		}
		v = StructCreate(parse_XMLValue);
		v->type = XMLRPC_Uninitialized;
		eaPush(&a->data, v);

		ObjectPathSetTailDescend(pi->state->path, true);
		pi->state->path = ObjectPathAppendIndex(pi->state->path, eaSize(&a->data)-1, NULL);
		ea32Push(&pi->state->tags, XMLRPC_Value);
	}
	else
	{
		estrPrintf(&pi->error, "XML Parse Error: invalid tag <%s> as direct child of <data>.", el);
		return;
	}
}

//grab text content
void xmlrpc_characters(void *data, const XML_Char *s, int len)
{
	XMLParseInfo *pi = (XMLParseInfo*)data;
	estrConcat(&pi->state->characters, s, len);
}

void xmlrpc_end(void *data, const XML_Char *el)
{
	XMLParseInfo *pi = (XMLParseInfo*)data;
	XMLRPCType type = ea32Pop(&pi->state->tags);
	
	//bail on previous error
	if (pi->error) return;

	//TODO: check to make sure el matches type.
	switch (type)
	{
	case XMLRPC_MethodName:
		{
			XMLMethodCall *mc = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &mc, NULL, NULL, 0) || !mc)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			estrPrintf(&mc->methodName, "%s", pi->state->characters);
		} break;
	case XMLRPC_Name:
		{
			XMLMember *m = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &m, NULL, NULL, 0) || !m)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			if (!pi->state->characters || !pi->state->characters[0])
			{
				estrPrintf(&pi->error, "Request payload xml was malformed; empty name tag encountered");
				return;
			}
			estrPrintf(&m->name, "%s", pi->state->characters);
		} break;

	case XMLRPC_Member:
		{
			XMLMember *m = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &m, NULL, NULL, 0) || !m)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			if (!m->name || !m->name[0])
			{
				estrPrintf(&pi->error, "Request payload xml was malformed; no name provided for member");
				return;
			}
			ObjectPathChopSegmentIndex(pi->state->path); 
			ObjectPathSetTailDescend(pi->state->path, false);
		} break;
	case XMLRPC_Param: 
		{
			XMLParam *p = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &p, NULL, NULL, 0) || !p)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			if (!p->value)
			{
				estrPrintf(&pi->error, "Request payload xml was malformed; empty param tags not allowed.");
				return;
			}
			ObjectPathChopSegmentIndex(pi->state->path); 
		} break;

	case XMLRPC_Data:
	case XMLRPC_Params: 
		{
			ObjectPathChopSegment(pi->state->path);
			ObjectPathSetTailDescend(pi->state->path, false);
		} break;
	case XMLRPC_Value:
		{	//We have to handle multiple parent types for value.
			XMLRPCType parentType = ea32Tail(&pi->state->tags);
			XMLValue *v = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			if (v->type == XMLRPC_Uninitialized)
			{
				estrPrintf(&pi->error, "Unsupported empty value tag encountered");
				return;
			}

			if (parentType == XMLRPC_Data) ObjectPathChopSegmentIndex(pi->state->path); 
			else ObjectPathChopSegment(pi->state->path);
			ObjectPathSetTailDescend(pi->state->path, false);
		} break;
	case XMLRPC_Int:
		{
			XMLValue *v = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			v->value_int = _atoi64(pi->state->characters);
			v->type      = XMLRPC_Int;
		} break;
	case XMLRPC_Boolean:
		{
			XMLValue *v = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			if (strstri(pi->state->characters, "true"))
				v->value_boolean = true;
			else if (strstri(pi->state->characters, "false"))
				v->value_boolean = false;
			else
			{
				int b;
				if (!StringToInt(pi->state->characters, &b))
				{
					estrPrintf(&pi->error, "An error occured parsing boolean (%s) for: %s",pi->state->characters, el);
					return;
				}
				v->value_boolean = (bool)b;
			}
			v->type = XMLRPC_Boolean;
		} break;
	case XMLRPC_String:
		{
			XMLValue *v = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			estrPrintf(&v->value_string,"%s", pi->state->characters);
			v->type = XMLRPC_String;
		} break;
	case XMLRPC_Double:
		{
			XMLValue *v = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			if (!StringToFloat(pi->state->characters, &v->value_double))
			{
				estrPrintf(&pi->error, "An error occured parsing double (%s) for: %s",pi->state->characters, el);
				return;
			}
			v->type = XMLRPC_Double;
		} break;
	case XMLRPC_DateTime_iso8601:
		{
			XMLValue *v = NULL;
			U32 t;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			t = timeGetSecondsSince2000FromIso8601String(pi->state->characters);
			if (!t)
			{
				estrPrintf(&pi->error, "An error occured parsing dateTime.iso8601: %s", pi->state->characters);
				return;
			}
			v->value_dateTime_iso8601 = t;
			v->type = XMLRPC_DateTime_iso8601;
		} break;
	case XMLRPC_Base64:
		{
			XMLValue *v = NULL;
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			estrPrintf(&v->value_base64,"%s", pi->state->characters);
			v->type = XMLRPC_Base64;
		} break;
	case XMLRPC_Struct:
		{
			XMLValue *v = NULL;
			ObjectPathChopSegment(pi->state->path);
			ObjectPathSetTailDescend(pi->state->path, false);
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			ObjectPathChopSegment(pi->state->path);
			v->type = XMLRPC_Struct;
		} break;
	case XMLRPC_Array:
		{
			XMLValue *v = NULL;
			ObjectPathSetTailDescend(pi->state->path, false);
			if (!ParserResolvePathComp(pi->state->path, pi, NULL, NULL, &v, NULL, NULL, 0) || !v)
			{
				estrPrintf(&pi->error, "An error occured building xml structure: %s", el);
				return;
			}
			ObjectPathChopSegment(pi->state->path);
			v->type = XMLRPC_Array;
		} break;
	}

	estrClear(&pi->state->characters);
}


typedef void (*xmlrpc_open_callback)(XMLParseInfo *pi, const XML_Char *el, const XML_Char **attr);

typedef struct xmlrpc_type_callback {
	const char *tag;
	XMLRPCType type;
	xmlrpc_open_callback cb;
} xmlrpc_type_callback;

static xmlrpc_type_callback xml_tcb[] = {
	{"state", XMLRPC_State, xmlrpc_parse_open_state},
	{"methodCall", XMLRPC_MethodCall, xmlrpc_parse_open_methodcall},
	{"methodName", XMLRPC_MethodName, xmlrpc_parse_open},
	{"params", XMLRPC_Params, xmlrpc_parse_open_params},
	{"param", XMLRPC_Param, xmlrpc_parse_open_param},
	{"value", XMLRPC_Value, xmlrpc_parse_open_value},
	{"int", XMLRPC_Int, xmlrpc_parse_open},
	{"i4", XMLRPC_Int, xmlrpc_parse_open},
	{"i8", XMLRPC_Int, xmlrpc_parse_open},
	{"boolean", XMLRPC_Boolean, xmlrpc_parse_open},
	{"string", XMLRPC_String, xmlrpc_parse_open},
	{"double", XMLRPC_Double, xmlrpc_parse_open},
	{"dateTime.iso8601", XMLRPC_DateTime_iso8601, xmlrpc_parse_open},
	{"base64", XMLRPC_Base64, xmlrpc_parse_open},
	{"struct", XMLRPC_Struct, xmlrpc_parse_open_struct},
	{"member", XMLRPC_Member, xmlrpc_parse_open_member},
	{"array", XMLRPC_Array, xmlrpc_parse_open_array},
	{"data", XMLRPC_Data, xmlrpc_parse_open_data},
	{0}
};

void xmlrpc_start(void *data, const XML_Char *el, const XML_Char **attr)
{
	XMLParseInfo *pi = (XMLParseInfo*)data;
	int i;

	//bail on previous error
	if (pi->error) return;

	for (i = 0; xml_tcb[i].tag ; i++)
	{
		if (xml_tcb[i].type == (int)ea32Tail(&pi->state->tags)) 
		{
			xml_tcb[i].cb(pi, el, attr);
			return;
		}
	}
	//otherwise
	xmlrpc_parse_open_state(pi, el, attr);
}

static XMLParseInfo* xmlrpc_create_parse_info()
{
	XMLParseInfo *pi = StructCreate(parse_XMLParseInfo);
	pi->state = StructCreate(parse_XMLParseState);
	pi->state->path = ObjectPathCreate(parse_XMLParseInfo, "state", -1, -1, NULL);
	ObjectPathSetTailDescend(pi->state->path, true);
	ea32Push(&pi->state->tags, XMLRPC_State);
	return pi;
}

XMLParseInfo * XMLRPC_Parse(const char *string, const char *client)
{
	XMLParseInfo *pi = xmlrpc_create_parse_info();
	XML_Parser p = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	p = XML_ParserCreate(NULL);

	assert (p);
	assert (string);

	if (client && client[0]) estrPrintf(&pi->clientname, "%s", client);

	XML_SetElementHandler(p, xmlrpc_start, xmlrpc_end);
	XML_SetCharacterDataHandler(p, xmlrpc_characters);
	XML_SetUserData(p, pi);

	if (XML_Parse(p, string, (int)strlen(string), 1) == XML_STATUS_ERROR)
	{
		estrPrintf(&pi->error, "Parse error at line %" XML_FMT_INT_MOD "u:\n%s\n",
              XML_GetCurrentLineNumber(p),
              XML_ErrorString(XML_GetErrorCode(p)));
	}

	XML_ParserFree(p);
	ObjectPathDestroy(pi->state->path);
	pi->state->path = NULL;
	PERFINFO_AUTO_STOP();

	return pi;
}

XMLMethodCall * XMLRPC_GetMethodCall(XMLParseInfo *pi)
{
	if (pi->error)
		return NULL;

	assert(pi->state);
	return pi->state->methodCall;
}

static void* xmlrpc_convert_struct(XMLStruct *s, ParseTable tpi[], char **resultString);

static bool xmlrpc_convert_array(XMLArray *a, ParseTable tpi[], int col, void *ptr, char *fieldname, char **resultString)
{
	bool success = true;
	if (TokenStoreStorageTypeIsFixedArray(TOK_GET_TYPE(tpi[col].type)))
	{
		if (resultString) estrPrintf(resultString, "Cannot pass fixed array fields as parameters: %s", fieldname);
		return false;
	}
	if (TOK_HAS_SUBTABLE(tpi[col].type))
	{ //structs
		EARRAY_FOREACH_BEGIN(a->data, i); {
			XMLValue *v = a->data[i];
			void *subptr = NULL;
			if (v->type != XMLRPC_Struct)
			{
				if (resultString) estrPrintf(resultString, "Cannot convert struct parameter in array: %s", fieldname);
				success = false;
				break;
			}
			subptr = xmlrpc_convert_struct(v->value_struct, tpi[col].subtable, resultString);
			if (!subptr)
			{
				success = false;
				break;
			}
			TokenStoreSetPointer(tpi, col, ptr, i, subptr, NULL);
		} EARRAY_FOREACH_END;
	}
	else
	{	//primitives
		switch (TOK_GET_TYPE(tpi[col].type))
		{
		case TOK_INT64_X:
		case TOK_INT_X:	
			{
				EARRAY_FOREACH_BEGIN(a->data, i); {
					XMLValue *v = a->data[i];
					if (v->type == XMLRPC_Int)
						TokenStoreSetIntAuto(tpi, col, ptr, i, v->value_int, NULL, NULL);
					else if (v->type == XMLRPC_Boolean)
						TokenStoreSetInt(tpi, col, ptr, i, v->value_boolean, NULL, NULL);
					else if (v->type == XMLRPC_DateTime_iso8601)
						TokenStoreSetInt(tpi, col, ptr, i, v->value_dateTime_iso8601, NULL, NULL);
					else
					{
						if (resultString) estrPrintf(resultString, "Could not convert array value for field: %s", fieldname);
						success = false;
						break;
					}
				} EARRAY_FOREACH_END;
			} break;
		case TOK_F32_X:
			{
				EARRAY_FOREACH_BEGIN(a->data, i); {
					XMLValue *v = a->data[i];
					if (v->type == XMLRPC_Double)
						TokenStoreSetF32(tpi, col, ptr, i, v->value_double, NULL, NULL);
					else
					{
						if (resultString) estrPrintf(resultString, "Could not convert array value for field: %s", fieldname);
						success = false;
						break;
					}
				} EARRAY_FOREACH_END;
			} break;
		case TOK_STRING_X:
			{
				EARRAY_FOREACH_BEGIN(a->data, i); {
					XMLValue *v = a->data[i];
					if (v->type == XMLRPC_String)
						TokenStoreSetString(tpi, col, ptr, i, v->value_string, NULL, NULL, NULL, NULL);
					else if (v->type == XMLRPC_DateTime_iso8601)
					{
						char buf[18];
						timeMakeLocalIso8601StringFromSecondsSince2000(buf, v->value_dateTime_iso8601);
						TokenStoreSetString(tpi, col, ptr, i, buf, NULL, NULL, NULL, NULL);
					}
					else
					{
						if (resultString) estrPrintf(resultString, "Could not convert array value for field: %s", fieldname);
						success = false;
						break;
					}
				} EARRAY_FOREACH_END;
			} break;
		default:
			success = false;
		}
	} //end primitives
	return success;
}

static bool xmlrpc_convert_nonarray(XMLValue *v, ParseTable tpi[], int col, void *ptr, char *fieldname, char **resultString)
{
	if (TOK_HAS_SUBTABLE(tpi[col].type))
	{ 
		//structs
		if (v->type == XMLRPC_Struct)
		{
			void *subptr = xmlrpc_convert_struct(v->value_struct, tpi[col].subtable, resultString);
			if (subptr)
			{
				U32 storage = TokenStoreGetStorageType(tpi[col].type);
				if(storage == TOK_STORAGE_DIRECT_SINGLE)
				{
					void *dest_struct = TokenStoreGetPointer(tpi, col, ptr, 0, NULL);
					StructCopyVoid(tpi[col].subtable, subptr, dest_struct, 0, 0, 0);
					StructDestroyVoid(tpi[col].subtable, subptr);
				}
				else
					TokenStoreSetPointer(tpi, col, ptr, -1, subptr, NULL);
				return true;
			}
		}
		if (resultString) estrPrintf(resultString, "Could not convert struct value for field: %s", fieldname);
		return false;
	}

	if (v->type == XMLRPC_String && tpi[col].subtable && TYPE_INFO(tpi[col].type).interpretfield(tpi, col, SubtableField) == StaticDefineList)
	{ //StaticDefines
		const char *reinterpret = StaticDefineLookup(tpi[col].subtable, v->value_string);
		if (reinterpret)
		{
			estrPrintf(&v->value_string, "%s", reinterpret);
		}
	}

	//primitives
	switch (TOK_GET_TYPE(tpi[col].type))
	{
	case TOK_U8_X:
		{
			if (v->type == XMLRPC_Int)
			{
				if (v->value_int > U8_MAX || v->value_int < 0)
				{
					if (resultString) estrPrintf(resultString, "Could not convert value (%"FORM_LL"d) to U8 for field: %s", v->value_int, fieldname);
					return false;
				}
				TokenStoreSetU8(tpi, col, ptr, -1, v->value_int, NULL, NULL);
				return true;
			}
			else if (v->type == XMLRPC_String)
			{
				U8 strint;
				char *end = NULL;
				strint = (U8)strtol(v->value_string, &end, 0);
				if (end != v->value_string)
				{
					TokenStoreSetU8(tpi, col, ptr, -1, strint, NULL, NULL);
					return true;
				}
			}
		} break;
	case TOK_INT16_X:		// 16 bit integer
		{
			if (v->type == XMLRPC_Int)
			{
				if (v->value_int > SHRT_MAX || v->value_int < SHRT_MIN)
				{
					if (resultString) estrPrintf(resultString, "Could not convert value (%"FORM_LL"d) to Int16 for field: %s", v->value_int, fieldname);
					return false;
				}					
				TokenStoreSetInt16(tpi, col, ptr, -1, v->value_int, NULL, NULL);
				return true;
			}
			else if (v->type == XMLRPC_String)
			{
				S16 strint;
				char *end = NULL;
				strint = strtol(v->value_string, &end, 0);
				if (end != v->value_string)
				{
					TokenStoreSetInt(tpi, col, ptr, -1, strint, NULL, NULL);
					return true;
				}
			}
		} break;
	case TOK_INT64_X:
	case TOK_INT_X:
		{
			if (v->type == XMLRPC_Int)
			{
				if (TOK_GET_TYPE(tpi[col].type == TOK_INT64_X))
				{
					TokenStoreSetIntAuto(tpi, col, ptr, -1, v->value_int, NULL, NULL);
					return true;
				}
				else
				{
					devassertmsg(v->value_int >= INT_MIN && v->value_int <= UINT_MAX, "Integer truncation");
					TokenStoreSetIntAuto(tpi, col, ptr, -1, (int)(v->value_int), NULL, NULL);
					return true;
				}
			}
			else if (v->type == XMLRPC_Boolean)
			{
				TokenStoreSetInt(tpi, col, ptr, -1, v->value_boolean, NULL, NULL);
				return true;
			}
			else if (v->type == XMLRPC_String)
			{
				int strint;
				char *end = NULL;
				strint = strtol(v->value_string, &end, 0);
				if (end != v->value_string)
				{
					TokenStoreSetInt(tpi, col, ptr, -1, strint, NULL, NULL);
					return true;
				}
			}
			else if (v->type == XMLRPC_DateTime_iso8601)
			{
				TokenStoreSetInt(tpi, col, ptr, -1, v->value_dateTime_iso8601, NULL, NULL);
				return true;
			}
		} break;
	case TOK_F32_X:
		{
			if (v->type == XMLRPC_Double)
			{
				TokenStoreSetF32(tpi, col, ptr, -1, v->value_double, NULL, NULL);
				return true;
			}
		} break;
	case TOK_STRING_X:
		{
			if (v->type == XMLRPC_String)
			{
				TokenStoreSetString(tpi, col, ptr, -1, v->value_string, NULL, NULL, NULL, NULL);
				return true;
			}
		} break;
	}

	if (resultString) estrPrintf(resultString, "Could not convert struct value for field: %s", fieldname);
	return false;
}

static void* xmlrpc_convert_struct(XMLStruct *s, ParseTable tpi[], char **resultString)
{
	void *ptr = StructCreateVoid(tpi);
	bool success = true;
	int i;

	for (i = 0; i < eaSize(&s->members); i++)
	{
		XMLMember *m = s->members[i];
		int col;
		if (!m->value || !m->name)
		{
			if (resultString) estrPrintf(resultString, "Invalid or incomplete struct member.");
			success = false;
			break;
		}
		if (!ParserFindColumn(tpi, m->name, &col))
		{
			if (resultString) estrPrintf(resultString, "Could not find struct parameter field: %s", m->name);
			success = false;
			break;
		}
		if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[col].type)))
		{
			if (m->value->type != XMLRPC_Array)
			{
				if (resultString) estrPrintf(resultString, "Expected an array for field: %s", m->name);
				success = false;
				break;
			}
			success = xmlrpc_convert_array(m->value->value_array, tpi, col, ptr, m->name, resultString);
		}
		else
		{	//non-arrays
			success = xmlrpc_convert_nonarray(m->value, tpi, col, ptr, m->name, resultString);
		}
		if (!success)
		{
			if (resultString && !**resultString) estrPrintf(resultString, "Could not convert value for field: %s", m->name);
			break;
		}
	}

	if (!success)
	{
		StructDestroyVoid(tpi, ptr);
		ptr = NULL;
	}

	return ptr;
}

static MultiVal *xmlrpc_convert_value(XMLValue *v, DataDesc *desc, OptionFlags flags, char **resultString)
{
	MultiVal *result = MultiValCreate();
	
	//perhaps other namelist types should be magically reinterpreted here.
	if (desc->eNameListType == NAMELISTTYPE_STATICDEFINE && v->type == XMLRPC_String)
	{
		const char *reinterpret = StaticDefineLookup((StaticDefine*)desc->ppNameListData, v->value_string);
		if (reinterpret)
		{
			estrPrintf(&v->value_string, "%s", reinterpret);
		}
	}

	switch (desc->type)
	{
	case MULTI_INT:
		{
			if (v->type == XMLRPC_Int)
			{
				MultiValSetInt(result, v->value_int);
				break;
			}
			//maybe this one should have an option flag
			else if (v->type == XMLRPC_Boolean)
			{
				MultiValSetInt(result, v->value_boolean);
				break;
			}
			else if (v->type == XMLRPC_DateTime_iso8601)
			{
				MultiValSetInt(result, v->value_dateTime_iso8601);
				break;
			}
			else if (v->type == XMLRPC_String)
			{
				int strint;
				char *end = NULL;
				strint = strtol(v->value_string, &end, 0);
				if (end != v->value_string)
				{
					MultiValSetInt(result, strint);
					break;
				}
			}
			if (resultString) estrPrintf(resultString, "expected int, type was %s", xmlrpc_type_to_name(v->type));
			MultiValDestroy(result); result = NULL;
			break;
		}
	case MULTI_FLOAT:
		{
			if (v->type == XMLRPC_Double)
			{
				MultiValSetFloat(result, v->value_double);
				break;
			}
			else if (v->type == XMLRPC_String)
			{
				double strdbl;
				char *end = NULL;
				strdbl = strtod(v->value_string, &end);
				if (end != v->value_string)
				{
					MultiValSetFloat(result, strdbl);
					break;
				}
			}
			if (resultString) estrPrintf(resultString, "expected float, type was %s", xmlrpc_type_to_name(v->type));
			MultiValDestroy(result); result = NULL;
			break;
		}
	case MULTI_STRING:
		{
			if (v->type == XMLRPC_String)
			{
				MultiValSetString(result, v->value_string);
				break;
			}
			if (resultString) estrPrintf(resultString, "expected string, type was %s", xmlrpc_type_to_name(v->type));
			MultiValDestroy(result); result = NULL;
			break;
		}
	case MULTI_NP_POINTER:
		{
			if (v->type == XMLRPC_Struct)
			{
				char *substring = NULL;
				void *ptr;
				if (resultString) estrStackCreate(&substring);

				if (!v->value_struct)
				{
					MultiValReferencePointer(result, NULL);
					break;
				}
				
				if (ptr = xmlrpc_convert_struct(v->value_struct, desc->ptr, &substring))
				{
					int size = ParserGetTableSize(desc->ptr);
					MultiValReferencePointer(result, ptr);
					if (substring) estrDestroy(&substring);
					break;
				}
				

				if (resultString) estrPrintf(resultString, "error parsing struct:%s", substring);
				if (substring) estrDestroy(&substring);
			}
			else
			{
				if (resultString) estrPrintf(resultString, "expected struct, type was %s", xmlrpc_type_to_name(v->type));
			}
			MultiValDestroy(result); result = NULL;
			break;
		}
	default:
		{
			MultiValDestroy(result);
			result = NULL;
			if (resultString) estrPrintf(resultString, "could not convert parameter type %d", desc->type);
		}
	}

	return result;
}

static bool xmlrpc_convert_method_call(XMLMethodCall *xmethod, Cmd *cmd, MultiVal ***out, OptionFlags flags, char **resultString)
{
	int i = 0;
	bool success = true;
	char *substring = NULL;

	//if we have an extra parameter and we're looking for 
	if (cmd->flags & CMDF_PASSENTITY)
	{	//Get entity from first param
		XMLParam *p = eaRemove(&xmethod->params, 0);
		devassertmsg(p->value, "XMLRPC Parameters should always have a value when built. Something is wrong.");
		if (p->value->type == XMLRPC_String && p->value->value_string)
		{
			if (resultString)
				estrPrintf(resultString, "%s", p->value->value_string);
			StructDestroy(parse_XMLParam, p);
			return true;
		}
		else if (p->value->type == XMLRPC_Int)
		{
			if (resultString)
				estrPrintf(resultString, "EntityPlayer[%"FORM_LL"d]", p->value->value_int);
			StructDestroy(parse_XMLParam, p);
			return true;
		}
		else if (p->value->type == XMLRPC_Entity)
		{
			if (resultString)
				estrPrintf(resultString, "!EntityPlayer[%"FORM_LL"d]", p->value->value_int);
			StructDestroy(parse_XMLParam, p);
		}
		else
		{
			if (resultString)
				estrPrintf(resultString, "This method requires a PlayerEntity name as the first parameter.");
			StructDestroy(parse_XMLParam, p);
			return false;
		}
	}

	if (cmd->iNumLogicalArgs > eaSize(&xmethod->params))
	{
		if (XMLRPC_FillInDefaultXMLArgsIfPossible(cmd, &xmethod->params))
		{
			//do nothing, we now have enough args
		}
		else
		{

			if (resultString)
				estrPrintf(resultString, "Not enough parameters (%d) passed in XMLRPC call for method: %s (%d needed).",
					eaSize(&xmethod->params), xmethod->methodName, cmd->iNumLogicalArgs);
			return false;
		}
	}
	else if (cmd->iNumLogicalArgs < eaSize(&xmethod->params))
	{
		if (resultString)
			estrPrintf(resultString, "Too many parameters (%d) passed in XMLRPC call for method: %s (%d expected).",
				eaSize(&xmethod->params), xmethod->methodName, cmd->iNumLogicalArgs);
		return false;
	}

	if (resultString) estrStackCreate(&substring);
	for (i = 0; i < cmd->iNumLogicalArgs && success; i++)
	{
		MultiVal *mval = xmlrpc_convert_value(xmethod->params[i]->value, &cmd->data[i], flags, (substring?&substring:NULL));
		if (!mval) {
			if (resultString) estrPrintf(resultString, "Error converting xml parameter %d: %s", i, substring);
			success = false;
		}
		else
		{
			eaPush(out, mval);
		}
	}
	if (substring) estrDestroy(&substring);

	return success;
}

static void xmlrpc_destroy_params(Cmd *cmd, MultiVal ***pppParams)
{
	EARRAY_FOREACH_REVERSE_BEGIN((*pppParams), i); { 
		MultiVal *param = (*pppParams)[i];
		if (cmd->data[i].type == MULTI_NP_POINTER && param->ptr)
		{	//clean up struct
			StructDestroyVoid(cmd->data[i].ptr, param->ptr_noconst);
			MultiValReferencePointer(param, NULL);
		}
		MultiValDestroy(param);
	} EARRAY_FOREACH_END;
	eaDestroy(pppParams);
}

void xmlrpc_insert_entity_param(XMLMethodCall *xmethod, U32 entid)
{
	XMLParam *p = NULL;
	devassert(xmethod);
	if (xmethod)
	{
		p = StructCreate(parse_XMLParam);
		p->value = StructCreate(parse_XMLValue);
		p->value->type = XMLRPC_Entity;
		p->value->value_int = entid;
		eaInsert(&xmethod->params, p, 0);
	}
}

static bool xmlrpc_struct_to_value(DataDesc *type, MultiVal *val, char **data, XMLValue *result)
{
	
	//char *str = NULL;
	//char *readstr = NULL;
	//void *strptr = NULL;
	//ParseTable *tpi = type->ptr;

	if (!(char **)data ||
		!((char **)data)[0] ||
		type->type != MULTI_NP_POINTER ||
		!type->ptr
		)
	{
		return false;
	}

	/*
	strptr = StructCreate(tpi);
	if (!strptr) 
	{
		return false;
	}

	str = readstr = strdup(*data);
	if (!ParserReadTextEscaped(&readstr, tpi, strptr, 0))
	{
		StructDestroyVoid(tpi, strptr);
		strptr = NULL;
	}
	free(str);
	if (!strptr)
	{
		return false;
	}

	ParserWriteXMLEx(&result->value_string, tpi, strptr, TPXML_FORMAT_XMLRPC);
	result->type = XMLRPC_StructString;

	StructDestroyVoid(tpi, strptr);
	*/

	estrPrintf(&result->value_string, "%s", *data);
	result->type = XMLRPC_StructString;
	return true;
}

static XMLValue *xmlrpc_returnval_to_value(DataDesc *type, MultiVal *val, char **data)
{
	XMLValue *result = StructCreate(parse_XMLValue);
	switch (val->type)
	{
	case MULTI_INT: 
		result->value_int = val->intval; 
		result->type = XMLRPC_Int;
		break;
	case MULTI_FLOAT: result->value_double = val->floatval; 
		result->type = XMLRPC_Double;
		break;
	case MULTI_STRING: estrPrintf(&result->value_string, "%s", val->str); 
		result->type = XMLRPC_String;
		break;
	case MULTI_INTARRAY:
		result->type = XMLRPC_Array;
		devassert(false);
		StructDestroy(parse_XMLValue, result);
		result = NULL;
		break;	//TODO: Needs to be implemented.
	case MULTI_FLOATARRAY:
		result->type = XMLRPC_Array;
		devassert(false);
		StructDestroy(parse_XMLValue, result);
		result = NULL;
		break;	//TODO: Needs to be implemented.
	case MULTI_MULTIVALARRAY:
		result->type = XMLRPC_Array;
		devassert(false);
		StructDestroy(parse_XMLValue, result);
		result = NULL;
		break;	//TODO: Needs to be implemented.
	case MULTI_VEC3:
		result->type = XMLRPC_Array;
		devassert(false);
		StructDestroy(parse_XMLValue, result);
		result = NULL;
		break;	//TODO: Needs to be implemented.
	case MULTI_VEC4:
		result->type = XMLRPC_Array;
		devassert(false);
		StructDestroy(parse_XMLValue, result);
		result = NULL;
		break;	//TODO: Needs to be implemented.
	case MULTI_MAT4:
		result->type = XMLRPC_Array;
		devassert(false);
		StructDestroy(parse_XMLValue, result);
		result = NULL;
		break;	//TODO: Needs to be implemented.
	case MULTI_QUAT:
		result->type = XMLRPC_Array;
		devassert(false);
		StructDestroy(parse_XMLValue, result);
		result = NULL;
		break;	//TODO: Needs to be implemented.
	case MULTI_NP_POINTER:
		{
			if (!xmlrpc_struct_to_value(type, val, data, result))
			{
				StructDestroy(parse_XMLValue, result);
				result = NULL;
			}
		} break;
	default:
		result->type = XMLRPC_Uninitialized;
	}
	return result;
}

XMLMethodResponse* XMLRPC_BuildMethodResponse(CmdContext *cmd_context, XMLRPCFault faultCode, char *message)
{
	XMLMethodResponse *response = NULL;
	XMLParam *p = NULL;

	PERFINFO_AUTO_START_FUNC();
	response = StructCreate(parse_XMLMethodResponse);

	if (cmd_context && !faultCode)
	{
		if (cmd_context->slowReturnInfo.bDoingSlowReturn)
		{
			response->slowID = cmd_context->slowReturnInfo.iClientID;
			PERFINFO_AUTO_STOP();
			return response;
		}
		else
		{
			XMLValue *v = NULL;
			v = xmlrpc_returnval_to_value(&cmd_context->found_cmd->return_type, &cmd_context->return_val, cmd_context->output_msg);

			if (v)
			{
				p = StructCreate(parse_XMLParam);
				p->value = v;
			}
			else
			{
				faultCode = XMLRPC_FAULT_RETURNFAILURE;
				message = "Command executed, but return value could not be converted to XML.";
			}
		}
	}
	//Not sure if this is needed
	else if (!cmd_context && faultCode == XMLRPC_FAULT_SLOWCOMMAND)
	{
		p = StructCreate(parse_XMLParam);
		p->value = StructCreate(parse_XMLValue);
		estrPrintf(&p->value->value_string, "%s", message); 
		p->value->type = XMLRPC_String;
	}

	if (p)
	{
		eaPush(&response->params, p);
	}
	else
	{ //failure
		XMLStruct *s;
		XMLMember *m;
		response->fault = StructCreate(parse_XMLFault);
		response->fault->value = StructCreate(parse_XMLValue);
		response->fault->value->type = XMLRPC_Struct;
		response->fault->value->value_struct = s = StructCreate(parse_XMLStruct);

		m = StructCreate(parse_XMLMember);
		estrPrintf(&m->name,"faultCode");
		m->value = StructCreate(parse_XMLValue);
		m->value->type = XMLRPC_Int;
		m->value->value_int = faultCode;
		eaPush(&s->members, m);
		m = NULL;

		m = StructCreate(parse_XMLMember);
		estrPrintf(&m->name,"faultString");
		m->value = StructCreate(parse_XMLValue);
		m->value->type = XMLRPC_String;
		estrPrintf(&m->value->value_string,"%s", message);
		eaPush(&s->members, m);
	}

	PERFINFO_AUTO_STOP();

	return response;
}

static void xmlrpc_capture_errorf_cb(ErrorMessage *errMsg, void *userdata)
{
	//an estring passed in.
	char **errorstring = (char **)userdata;
	if (errorstring)
	{
		estrCopyWithHTMLEscaping(errorstring, errMsg->estrMsg, false);
	}
}

void DEFAULT_LATELINK_XMLRPC_LoadEntity(CmdContext *slowContext, char *ent)
{
	slowContext->data = NULL;
}

XMLMethodResponse* DEFAULT_LATELINK_XMLRPC_DispatchEntityCommand(CmdContext *slowContext, char *name, ContainerID iVirtualShardID)
{
	return XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_NOSLOWCOMMANDS, 
		"XMLRPC Slow command could not be run via this method.");
}

static void xmlrpc_error_no_slowreturn(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData)
{
	Errorf("XMLRPC Tried to call a slow command without a slow context.");
}

XMLMethodResponse* XMLRPC_ConvertAndExecuteCommand(XMLMethodCall *method, int accesslevel, char **categories, const char *pAuthNameAndIP, CmdSlowReturnForServerMonitorInfo *slowReturnInfo)
{
	Cmd *cmd = NULL;
	MultiVal **ppParams = NULL;
	CmdContext cmd_context = {0};
	char *errorstring = NULL;
	char *substring = NULL;
	char *cmdname = method->methodName;
	bool bCommandExecuted = false;
	bool bUseErrorHandler = gbUseXMLRPCErrorHandler; // in case it changes mid-call

	XMLMethodResponse *response = NULL;

	PERFINFO_AUTO_START_FUNC();

	cmd_context.access_level = accesslevel;
	cmd_context.output_msg = &substring;
	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_XMLRPC;
	cmd_context.categories = categories;
	cmd_context.pAuthNameAndIP = pAuthNameAndIP;

	if (!cmdname)
	{
		response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_NOMETHOD,
			STACK_SPRINTF("No xml methodcall name parsed."));
		PERFINFO_AUTO_STOP();
		return response;
	}

	cmd = cmdListFindWithContext(&gGlobalCmdList, cmdname, &cmd_context);
	if (!cmd)
	{
		response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_NOCOMMAND,
			STACK_SPRINTF("Could not find command: %s", method->methodName));
		PERFINFO_AUTO_STOP();
		return response;
	}

	eaCreate(&ppParams);
	estrStackCreate(&substring);
	if (!xmlrpc_convert_method_call(method, cmd, &ppParams, 0, &substring))
	{
		xmlrpc_destroy_params(cmd, &ppParams);

		if (substring && substring[0])
		{
			response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_BADPARAMS,
				STACK_SPRINTF("XMLRPC AutoCommand encountered an error: %s", substring));
		}
		else
		{
			response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_BADPARAMS,
				STACK_SPRINTF("XMLRPC AutoCommand encountered an error: %s", substring));
		}

		estrDestroy(&substring);
		PERFINFO_AUTO_STOP();
		
		return response;
	}
	if (substring && substring[0])
	{
		if (substring[0] == '!')
		{	//supposedly the entity is here. cram it into the context.
			XMLRPC_LoadEntity(&cmd_context, substring+1);
			if (!cmd_context.data)
			{
				estrDestroy(&substring);
				xmlrpc_destroy_params(cmd, &ppParams);

				response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_ENTITYNOTFOUND, 
					"The command failed to retrieve the entity.");
				PERFINFO_AUTO_STOP();
				return response;
			}
		}
		else if (slowReturnInfo)
		{	//an entity name was written out we need to fetch it and complete by slow return.
			CmdContext *slowContext = (CmdContext*)calloc(1, sizeof(CmdContext));
			*slowContext = cmd_context;
			slowContext->commandData = method;
			slowContext->found_cmd = cmd;
			slowContext->slowReturnInfo = *slowReturnInfo;
			slowReturnInfo->bDoingSlowReturn = true;

			//this says that we now own the XMLMethodCall, so it shouldn't be cleaned up by anyone else
			slowReturnInfo->bDontDestroyXMLMethodCall = true;

			/*FIXME_VSHARD*/
			response = XMLRPC_DispatchEntityCommand(slowContext, substring, 0);

			estrDestroy(&substring);
			PERFINFO_AUTO_STOP();
			return response;
		}
		else
		{
			//Error that slow commands are unsupported
			estrDestroy(&substring);
			xmlrpc_destroy_params(cmd, &ppParams);

			response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_NOSLOWCOMMANDS, 
				"XMLRPC Slow command could not be run via this method.");
			PERFINFO_AUTO_STOP();
			return response;
		}
	}
	estrClear(&substring);

	if (slowReturnInfo)
	{
		cmd_context.slowReturnInfo = *slowReturnInfo;
	}
	else
	{
		cmd_context.slowReturnInfo.pSlowReturnCB = xmlrpc_error_no_slowreturn;
	}
	cmd_context.slowReturnInfo.eHowCalled = CMD_CONTEXT_HOWCALLED_XMLRPC;

	estrStackCreate(&errorstring);
	if (bUseErrorHandler) ErrorfPushCallback(xmlrpc_capture_errorf_cb, &errorstring);
	bCommandExecuted = cmdExecuteWithMultiVals(&gGlobalCmdList, cmdname, &cmd_context, &ppParams);
	if (bUseErrorHandler) ErrorfPopCallback();

	if (!bCommandExecuted)
	{
		xmlrpc_destroy_params(cmd, &ppParams);

		response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_COMMANDFAILURE,
			STACK_SPRINTF("Failed to execute command %s with supplied parameters.", cmdname));
		estrDestroy(&substring);
		estrDestroy(&errorstring);
		PERFINFO_AUTO_STOP();

		return response;
	}

	if (slowReturnInfo)
	{
		*slowReturnInfo = cmd_context.slowReturnInfo;
	}
	else
	{
		if (cmd_context.slowReturnInfo.bDoingSlowReturn)
		{	//We attempted a slow return when none was possible.
			if (response) StructDestroy(parse_XMLMethodResponse, response);
			estrPrintf(&errorstring, "The slow method \"%s\" could not be called with this connection type.", cmdname);
			response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_NOSLOWCOMMANDS, errorstring); 
		}
	}
	xmlrpc_destroy_params(cmd, &ppParams);

	if (MultiValIsNull(&cmd_context.return_val) && !(!cmd_context.output_msg || !cmd_context.output_msg[0]) && errorstring[0])
	{
		response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_EXECUTIONFAILURE, errorstring);
	}
	else
	{
		response = XMLRPC_BuildMethodResponse(&cmd_context, XMLRPC_FAULT_NONE, NULL);
	}

	estrDestroy(&substring);
	estrDestroy(&errorstring);
	PERFINFO_AUTO_STOP();
	return response;
}

static void xmlrpc_write_value(char **estrout, XMLValue *val);

static void xmlrpc_write_datetime(char **estrout, XMLValue *val)
{
	char buf[18];
	estrConcatf(estrout, "<dateTime.iso8601>%s</dateTime.iso8601>\n", timeMakeLocalIso8601StringFromSecondsSince2000(buf,val->value_dateTime_iso8601));
}

static void xmlrpc_write_array(char **estrout, XMLArray *a)
{
	estrConcatf(estrout, "\n<array><data>\n");
	EARRAY_FOREACH_BEGIN(a->data,i); {
		xmlrpc_write_value(estrout, a->data[i]);
	} EARRAY_FOREACH_END;
	estrConcatf(estrout, "</data></array>\n");
}

static void xmlrpc_write_struct(char **estrout, XMLStruct *s)
{
	estrConcatf(estrout, "\n<struct>\n");
	EARRAY_FOREACH_BEGIN(s->members,i); {
		XMLMember *m = s->members[i];
		estrConcatf(estrout, "<member>\n");
		estrConcatf(estrout, "<name>%s</name>\n", m->name);
		xmlrpc_write_value(estrout, m->value);
		estrConcatf(estrout, "</member>\n");
	} EARRAY_FOREACH_END;
	estrConcatf(estrout, "</struct>\n");
}

static void xmlrpc_write_value(char **estrout, XMLValue *val)
{
	estrConcatf(estrout, "<value>");
	switch (val->type)
	{
	case XMLRPC_Int: estrConcatf(estrout, "<int>%"FORM_LL"d</int>\n", val->value_int); break;
	case XMLRPC_Boolean: estrConcatf(estrout, "<boolean>%s</boolean>\n", (val->value_boolean?"true":"false")); break;
	case XMLRPC_String: estrConcatf(estrout, "<string>%s</string>\n", val->value_string); break;
	case XMLRPC_Double: estrConcatf(estrout, "<double>%f</double>\n", val->value_double); break;
	case XMLRPC_DateTime_iso8601: xmlrpc_write_datetime(estrout, val); break;
	case XMLRPC_Base64: estrConcatf(estrout, "<base64>%s</base64>\n", val->value_base64); break;
	case XMLRPC_StructString: estrConcatf(estrout, "%s", val->value_string); break;
	case XMLRPC_Struct: xmlrpc_write_struct(estrout, val->value_struct); break;
	case XMLRPC_Array: xmlrpc_write_array(estrout, val->value_array); break;
	default: break;
		//error?
	}
	estrConcatf(estrout, "</value>\n");
}

void XMLRPC_WriteOutMethodResponse(XMLMethodResponse *response, char **estrout)
{
	PERFINFO_AUTO_START_FUNC();
	estrConcatf(estrout, "%s\n", XML_DECLARATION);
	estrConcatf(estrout, "<methodResponse>\n");
	if (response->fault)
	{
		estrConcatf(estrout, "<fault>\n");
		xmlrpc_write_value(estrout, response->fault->value);
		estrConcatf(estrout, "</fault>\n");
	}
	else
	{
		estrConcatf(estrout, "<params>\n");
		EARRAY_FOREACH_BEGIN(response->params, i);
		{
			estrConcatf(estrout, "<param>\n");
			xmlrpc_write_value(estrout, response->params[i]->value);
			estrConcatf(estrout, "</param>\n");
		}
		EARRAY_FOREACH_END;
		estrConcatf(estrout, "</params>\n");
	}
	estrConcatf(estrout, "</methodResponse>\n");
	PERFINFO_AUTO_STOP();
}

void XMLRPC_WriteOutResponse_RawString(char *pInString, char **estrout)
{
	PERFINFO_AUTO_START_FUNC();
	estrConcatf(estrout, "%s\n", XML_DECLARATION);
	estrConcatf(estrout, "<methodResponse>\n");
	estrConcatf(estrout, "<params>\n");
	estrConcatf(estrout, "<param>\n");
	estrConcatf(estrout, "<value>\n");
	estrConcatf(estrout, "%s", pInString);
	estrConcatf(estrout, "</value>\n");
	estrConcatf(estrout, "</param>\n");
	estrConcatf(estrout, "</params>\n");
	estrConcatf(estrout, "</methodResponse>\n");
	PERFINFO_AUTO_STOP();
}


static xmlrpc_describe_type(ParseTable *tpi, TypeDescription *td)
{
	ParseTableInfo *info = ParserGetTableInfo(tpi);
	ParseTableWriteText(&td->type, tpi, info->name, PARSETABLESENDFLAG_FOR_API);
}

//This AUTO_COMMAND will enumerate the return and parameter types for a named command.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME("system.methodIntrospection") ACMD_ACCESSLEVEL(9);
MethodIntrospection* XMLRPC_System_methodIntrospection(CmdContext *pContext, char *cmdname)
{
	Cmd *cmd = NULL;
	CmdContext cmd_context = {0};
	MethodIntrospection *mi = NULL;
	int i = 0;

	cmd_context.access_level = pContext->access_level;
	//cmd_context.output_msg = &substring;
	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_XMLRPC;

	cmd = cmdListFindWithContext(&gGlobalCmdList, cmdname, &cmd_context);
	
	if (!cmd)
	{
		Errorf("Method named \"%s\" could not be found.", cmdname);
		return NULL;
	}

	mi = StructCreate(parse_MethodIntrospection);

	estrPrintf(&mi->name, "%s", cmdname);
	estrPrintf(&mi->origin, "%s", cmd->origin);

	if (cmd->comment) estrPrintf(&mi->comment, "%s", cmd->comment);
	
	for (i = -1; i < cmd->iNumLogicalArgs; i++)
	{
		DataDesc *desc;
		TypeDescription *p = StructCreate(parse_TypeDescription);

		if (i > -1) 
		{
			int j = i;
			desc = &cmd->data[j]; 
		}
		else 
		{
			desc = &cmd->return_type;
		}
		switch(desc->type)
		{
		case MULTI_NP_POINTER: xmlrpc_describe_type(desc->ptr, p); break;
		case MULTI_INT: estrPrintf(&p->type,"int"); break;
		case MULTI_FLOAT:estrPrintf(&p->type,"float"); break;
		case MULTI_STRING:estrPrintf(&p->type,"string"); break;
		case MULTI_NONE: 
			{
				if (i < 0) 
				{
					estrPrintf(&p->type,"void");
					break;
				}
			}
		default: estrPrintf(&p->type,"???"); break;
		}
		if (i>-1) 
		{
			estrPrintf(&p->name, "%s", desc->pArgName);
		}
		else
		{
			estrPrintf(&p->name, "return_value");
		}

		eaPush(&mi->parameters, p);
	}

	if (cmd->flags & CMDF_PASSENTITY)
	{
		TypeDescription *p = StructCreate(parse_TypeDescription);
		estrPrintf(&p->name, "Entity*");
		estrPrintf(&p->type, "int");
		eaInsert(&mi->parameters,p,1);
	}

	return mi;
}

void XMLRPC_UseErrorHandler(bool bShouldUse)
{
	gbUseXMLRPCErrorHandler = bShouldUse;
}


//Test functions

AUTO_STRUCT;
typedef struct XML_TestStruct
{
	int i;
	float f;
	int *aint;
} XML_TestStruct;

AUTO_STRUCT;
typedef struct XML_CollectionOfStuff
{
	EARRAY_OF(XML_TestStruct) ptrs;
} XML_CollectionOfStuff;


AUTO_COMMAND ACMD_ACCESSLEVEL(9);
XML_CollectionOfStuff* TestXMLRPC(int j)
{
	XML_CollectionOfStuff *cos = StructCreate(parse_XML_CollectionOfStuff);
	int k;
	//to prevent 'sploit.
	if (!(j < 10))
	{
		Errorf("TestXMLRPC must take a value less than 10");
		StructDestroy(parse_XML_CollectionOfStuff, cos);
		return NULL;
	}
	for (k = 0; k < j; k++)
	{
		XML_TestStruct *ts = NULL;
		int i;
		if (!ts) ts = StructCreate(parse_XML_TestStruct);
		ts->i = j;
		for (i = 0; i < 4; i++)
			ea32Push(&ts->aint, i);

		eaPush(&cos->ptrs, ts);
	}
	return cos;
}

/*

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
XMLUOTest *TestXMLRPCUO(int i)
{
	XMLUOTest *v = StructCreate(parse_XMLUOTest);
	static IntStringPair isp = {0};
	static bool first = true;

	if (first)
	{
		first = false;
		StructInit(parse_IntStringPair, &isp);
		isp.i4 = i;
		estrPrintf(&isp.string, "test");
	}
	v->isp = &isp;

	return v;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(9);
XMLBase64Test *TestXMLBase64()
{
	XMLBase64Test *x = StructCreate(parse_XMLBase64Test);
	int i;
	estrCreate(&x->data);
	estrSetSize(&x->data, 100);

	for (i = 0; i < 100; i++)
	{
		x->data[i] = (char)i;
	}

	return x;
}

//
//Using mimic-xmlrpc, you can get xml as a base64 like so:
//
//	var request = new XmlRpcRequest("/xmlrpc", "TestXMLXML");
//	var response = request.send();
//	var result = response.parseXML();
//	if(response.isFault() != true)
//	{
//		var decode = result['data'].decode();
//		alert(decode); //decode will be the raw xml string. this can then be stuck in a dom to be parsed.
//	}
//
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
XMLBase64Test *TestXMLXML()
{
	XMLBase64Test *x = StructCreate(parse_XMLBase64Test);
	
	estrPrintf(&x->data, "<string><omg><![CDATA[this is some bogus stringage]]></omg></string>");

	return x;
}



AUTO_COMMAND ACMD_ACCESSLEVEL(9);
float TestXMLRPC2(XML_TestStruct *ptr)
{
	int i;
	char *estr = NULL;
	estrStackCreate(&estr);
	for (i = 0; i < ea32Size(&ptr->aint); i++)
	{
		estrConcatf(&estr,"aint[%d]:%d\n", i, ptr->aint[i]);
	}
	estrDestroy(&estr);
	return ptr->f;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(9);
char * TestXMLRPC3(XML_CollectionOfStuff *cos, int ignore, XML_CollectionOfStuff *cos2)
{
	int i;
	char *estr = NULL;
	char *ret;
	estrStackCreate(&estr);
	for (i = 0; i < eaSize(&cos->ptrs); i++)
	{
		XML_TestStruct *ptr = cos->ptrs[i];
		estrConcatf(&estr,"f[%d]:%4.4f\n", i, ptr->f);
	}
	ret = strdup(estr);
	estrDestroy(&estr);
	return ret;
}
*/


void OVERRIDE_LATELINK_XMLRPC_WriteSimpleStructResponse(char **ppOutString, void *pStruct, ParseTable *pTPI)
{
	char *pStructString = NULL;
	estrStackCreate(&pStructString);
	estrClear(ppOutString);

	ParserWriteXMLEx(&pStructString, pTPI, pStruct, TPXML_FORMAT_XMLRPC|TPXML_NO_PRETTY);

	XMLRPC_WriteOutResponse_RawString(pStructString, ppOutString);

	estrDestroy(&pStructString);
}

XMLParam *XMLRPC_CreateParamInt(int iVal)
{
	XMLParam *pParam = StructCreate(parse_XMLParam);
	pParam->value = StructCreate(parse_XMLValue);
	pParam->value->value_int = iVal;
	pParam->value->type = XMLRPC_Int;

	return pParam;
}

XMLParam *XMLRPC_CreateParamString(char *pVal)
{
	XMLParam *pParam = StructCreate(parse_XMLParam);
	pParam->value = StructCreate(parse_XMLValue);
	if (pVal)
	{
		estrCopy2(&pParam->value->value_string, pVal);
	}
	pParam->value->type = XMLRPC_String;

	return pParam;
}

XMLParam *XMLRPC_CreateNULLStructParam(void)
{
	XMLParam *pParam = StructCreate(parse_XMLParam);
	pParam->value = StructCreate(parse_XMLValue);
	pParam->value->type = XMLRPC_Struct;

	return pParam;
}

//used by XMLRPC code when we know that we don't have enough logical arguments. Checks if all the "trailing" args have default
//values, and if so, fills them in. Returns true on success
bool XMLRPC_FillInDefaultXMLArgsIfPossible(Cmd *pCmd, XMLParam ***pppParams)
{
	int iNumParams = eaSize(pppParams);
	int i;

	if (iNumParams > CMDMAXARGS)
	{
		return false;
	}

	for (i = iNumParams; i < pCmd->iNumLogicalArgs; i++)
	{
		DataDesc *pArg = &pCmd->data[i];

		if (!(pArg->flags & CMDAF_HAS_DEFAULT))
		{
			return false;
		}

		switch (pArg->type)
		{
		case MULTI_INT:
		case MULTI_STRING:	
		case MULTI_NP_POINTER:
			break;

		default:
			return false;
		}
	}

	for (i = iNumParams; i < pCmd->iNumLogicalArgs; i++)
	{
		DataDesc *pArg = &pCmd->data[i];
		
		switch (pArg->type)
		{
		xcase MULTI_INT:
			eaPush(pppParams, XMLRPC_CreateParamInt((int)((intptr_t)(pArg->ppNameListData))));

		xcase MULTI_STRING:
			eaPush(pppParams, XMLRPC_CreateParamString((char*)(pArg->ppNameListData)));

		xcase MULTI_NP_POINTER:
			eaPush(pppParams, XMLRPC_CreateNULLStructParam());
		}
	}


	return true;
}

#include "autogen/xmlrpc_c_ast.c"

#endif //ndef _XBOX

#include "autogen/xmlrpc_h_ast.c"