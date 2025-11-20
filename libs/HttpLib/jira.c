#include "jira.h"
#include "estring.h"
#include "net/net.h"
#include "HttpLib.h"
#include "HttpClient.h"
#include "sock.h"
#include "StashTable.h"

#include "XMLRPC.h"

#include "jira_h_ast.h"
#include "jira_c_ast.h"

#include "error.h"

// Internal authentication state
static char *spJiraAuth   = NULL;
static char *spJiraServer = NULL;
static int   siJiraPort   = 0;

static char *spDefaultJiraHost = NULL;
static int siDefaultJiraPort = 8080;
AUTO_CMD_INT(siDefaultJiraPort, jiraPort);

static int siJiraLoginRetryPeriod = 10;
AUTO_CMD_INT(siJiraLoginRetryPeriod, jiraLoginRetry);

// Set a default host.
static void jiraInitDefaultHost(void)
{
	if (!spDefaultJiraHost)
		estrCopy2(&spDefaultJiraHost, "jira");
}

void jiraSetDefaultAddress (char *host, int port)
{
	if (host && *host)
		estrCopy2(&spDefaultJiraHost, host);
	if (port)
		siDefaultJiraPort = port;
}

const char *jiraGetDefaultURL()
{
	static char *url = NULL;
	jiraInitDefaultHost();
	estrDestroy(&url);
	estrPrintf(&url, "http://%s:%d/", spDefaultJiraHost, siDefaultJiraPort);
	return url;
}

const char *jiraGetAddress(void)
{
	if (!spDefaultJiraHost)
		jiraInitDefaultHost();
	return spDefaultJiraHost;
}
int jiraGetPort(void)
{
	return siDefaultJiraPort;
}

// ------------------------------------------------------------------------------------------
static const char * spDefaultProject           = "UNKNOWN";
static const char * spCreateIssueAdditionalXML = NULL;
static int          siDefaultType              = 1;  // "Bug"
static int			siDefaultType2			   = 11; // "Code Defect"
static int			siDefaultSeverity		   = 1;

static StashTable sJiraStatusTable = NULL;
static StashTable sJiraResolutionTable = NULL;

void jiraSetDefaults(const char *pProject, int iType, const char *pCreateIssueAdditionalXML)
{
	spDefaultProject           = pProject;
	siDefaultType              = iType;
	spCreateIssueAdditionalXML = pCreateIssueAdditionalXML;
}
// ------------------------------------------------------------------------------------------
// Jira XML Response Parsing code
#include "XMLParsing.h"

typedef struct JiraParseList
{
	void **ppCompleteParses; // done parses
	void *pCurrentParse;
} JiraParseList;

typedef struct JiraXMLState JiraXMLState;
typedef enum
{
	XML_ENTER = 0,
	XML_CHARACTER,
	XML_EXIT,
} XMLStateEnum;

typedef void (*JiraXMLHandler) (JiraXMLState *state, XMLStateEnum eStateAction);
AUTO_STRUCT;
typedef struct JiraXMLState
{
	char *el; AST(ESTRING)
	char *characters; AST(ESTRING)

	void *data; NO_AST

	JiraXMLHandler cb; NO_AST
	JiraXMLState *parentState; NO_AST
	JiraXMLState *childState; NO_AST
	XML_Parser parser; NO_AST

	// Custom Error Info
	char *faultCode; AST(ESTRING)
	char *faultString; AST(ESTRING)
	bool bFailedParse;
} JiraXMLState;


static void jiraXMLStart(JiraXMLState *state, const char *el, const char **attr)
{
	JiraXMLState *newState = StructCreate(parse_JiraXMLState);
	estrCopy2(&newState->el, el);
	newState->parentState = state;
	newState->parser = state->parser;
	newState->bFailedParse = state->bFailedParse;
	
	if (el && stricmp(el, "soapenv:Fault") == 0)
	{
		newState->bFailedParse = true;
	}
	else
	{
		newState->cb = state->cb;
		newState->data = state->data;
	}
	state->childState = newState;

	XML_SetUserData(state->parser, newState);
	if (newState->cb)
		newState->cb(newState, XML_ENTER);
}

static void jiraXMLEnd(JiraXMLState *state, const char *el)
{
	if (state)
	{
		JiraXMLState *parent = state->parentState;
		if (state->cb)
			state->cb(state, XML_EXIT);
		XML_SetUserData(state->parser, parent);
		if (parent) 
		{
			parent->childState = NULL;
			if (state->bFailedParse)
			{
				// Copy error info
				parent->bFailedParse = true;
				if (state->faultCode && !parent->faultCode)
				{
					parent->faultCode = state->faultCode;
					state->faultCode = NULL;
				}
				if (state->faultString && !parent->faultString)
				{
					parent->faultString = state->faultString;
					state->faultString = NULL;
				}
			}
		}

		StructDestroy(parse_JiraXMLState, state);
	}
}

static void jiraXMLCharacters (JiraXMLState *state, const XML_Char *s, int len)
{
	if (state)
	{
		char *buffer = malloc(len+1);
		strncpy_s(buffer, len+1, s, len);
		buffer[len] = 0;
		if (len > 0)
			estrCopy2(&state->characters, buffer);
		free(buffer);

		if (state->bFailedParse && state->el)
		{
			if (stricmp(state->el, "faultCode") == 0)
			{
				state->faultCode = state->characters;
				state->characters = NULL;
			}
			else if (stricmp(state->el, "faultString") == 0)
			{
				state->faultString = state->characters;
				state->characters = NULL;
			}
		}
		else if (state->cb)
			state->cb(state, XML_CHARACTER);
	}
}

static void jiraXMLDestroyStateStructs(JiraXMLState *state)
{
	if (state)
	{
		jiraXMLDestroyStateStructs(state->childState);
		StructDestroy(parse_JiraXMLState, state);
	}
}

static bool jiraXMLParseString(const char * input, JiraXMLHandler handler, void *data)
{
	XML_Parser p = XML_ParserCreate(NULL);
	JiraXMLState baseState = {0};
	int xmlRet;
	if (!p) return false;

	baseState.parser = p;
	baseState.cb = handler;
	baseState.data = data;
	XML_SetUserData(p, &baseState);

	XML_SetElementHandler(p, jiraXMLStart, jiraXMLEnd);
	XML_SetCharacterDataHandler(p, jiraXMLCharacters);

	xmlRet = XML_Parse(p, input, (int) strlen(input), true);
	XML_ParserFree(p);
	if (xmlRet == 0)
	{
		printf("%s\n", XML_ErrorString(	XML_GetErrorCode(p) ));
		jiraXMLDestroyStateStructs(baseState.childState);
		baseState.childState = NULL;
	}

	if (baseState.bFailedParse)
	{
		xmlRet = 0; // manual setting of error on custom errors
		// TODO do stuff with error string?
		//printf("Jira XML Error: %s - %s\n\n", baseState.faultCode, baseState.faultString);
	}
	devassert(baseState.childState == NULL);
	StructDeInit(parse_JiraXMLState, &baseState);
	
	return (xmlRet != 0); // 0 indicates an error
}

void jiraFixupString (char **estr)
{
	char *copy = alloca(strlen(*estr) + 1);
	char *cur;
	int idx = 0;

	cur = *estr;
	while (*cur)
	{
		if (*cur & 0x80)
		{
			copy[idx++] = '?';
		}
		else
		{
			copy[idx++] = *cur;
		}
		cur++;
	}
	copy[idx] = 0;
	estrCopy2(estr, copy);
}

// ------------------------------------------------------------------------------------------

static char * getClosestTagData(char **pBuffer, const char *pTagStart);

static bool sbJiraDataLoaded = false;
JiraUserList gJiraUserList;
JiraProjectList gJiraProjectList;

bool loadJiraData(void)
{
	if(sbJiraDataLoaded)
	{
		return true;
	}

	if(jiraDefaultLogin())
	{
		jiraGetUsers(&gJiraUserList, "Cryptic");
		jiraGetProjects(&gJiraProjectList);
		jiraLogout();

		sbJiraDataLoaded = true;
	}

	return sbJiraDataLoaded;
}

void unloadJiraData(void)
{
	if(sbJiraDataLoaded)
	{
		StructDeInit(parse_JiraUserList, &gJiraUserList);
		StructDeInit(parse_JiraProjectList, &gJiraProjectList);
		sbJiraDataLoaded = false;
	}
}

JiraProject *findJiraProjectByKey(const char *pProjectKey)
{
	int i;
	for (i=0; i<eaSize(&gJiraProjectList.ppProjects);  i++)
	{
		if (!strcmp(pProjectKey, gJiraProjectList.ppProjects[i]->pKey))
			return gJiraProjectList.ppProjects[i];
	}
	return NULL;
}

static bool findAuthData(char *pJiraResponse)
{
	char *tmp;

	estrClear(&spJiraAuth);

	if(pJiraResponse == NULL)
	{
		return false;
	}

	tmp = getClosestTagData(&pJiraResponse, "<loginReturn ");
	if(tmp)
	{
		estrCopy2(&spJiraAuth, tmp);
		if(strlen(spJiraAuth) > 1)
		{
			return true;
		}
	}

	return false;
}

void formAppendJiraUsers(char **estr, const char *pVarName)
{
	int i;

	loadJiraData();

	estrConcatf(estr, "<select class=\"formdata\" name=\"%s\">\n", pVarName);
	estrConcatf(estr, "<option value=\"--\">Assign To:</option>\n");

	for (i=0; i < eaSize(&gJiraUserList.ppUsers); i++)
	{
		estrConcatf(estr, "<option value=\"%s\">%s</option>\n", 
			gJiraUserList.ppUsers[i]->pUserName,
			gJiraUserList.ppUsers[i]->pFullName);
	}

	estrConcatf(estr, "</select>\n");
}

void formAppendJiraComponents(char **estr, const char *pVarName, JiraComponentList *pComponentsList)
{
	int i;

	estrConcatf(estr, "<select class=\"formdata\" name=\"%s\">\n", pVarName);
	estrConcatf(estr, "<option value=\"\">(None)</option>/n");

	for (i=0; i < eaSize(&pComponentsList->ppComponents); i++)
	{
		estrConcatf(estr, "<option value=\"%s&#x0009;%s\">%s</option>\n",
			pComponentsList->ppComponents[i]->pID, 
			pComponentsList->ppComponents[i]->pName, 
			pComponentsList->ppComponents[i]->pName);
	}
	estrConcatf(estr, "</select>\n");
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN JiraProjectList * TT_GetJiraProjectList()
{
	JiraProjectList* list = NULL;
	loadJiraData();

	list = &gJiraProjectList;
	if (!list || !eaSize(&list->ppProjects))
		Errorf("Failed to get Jira Projects list");
	return list;
}

void formAppendJiraProjects(char **estr, const char *pVarName)
{
	int i;

	loadJiraData();

	estrConcatf(estr, "<select class=\"formdata\" name=\"%s\">\n", pVarName);
	estrConcatf(estr, "<option value=\"--\">Into Project:</option>\n");

	for (i=0; i < eaSize(&gJiraProjectList.ppProjects); i++)
	{
		estrConcatf(estr, "<option value=\"%s\">%s</option>\n", 
			gJiraProjectList.ppProjects[i]->pKey,
			gJiraProjectList.ppProjects[i]->pName);
	}

	estrConcatf(estr, "</select>\n");
}

bool jiraDefaultLogin(void)
{
	jiraInitDefaultHost();
	return jiraLogin(spDefaultJiraHost, siDefaultJiraPort, "errortracker", "errortracker");
}

bool jiraLogin(const char *pServer, int port, const char *pUserName, const char *pPassword)
{
	static U32 siLastFailedLoginTime = 0;
	U32 uTime = timeSecondsSince2000();
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;
	bool bSuccessfulLogin   = false;

	if (siLastFailedLoginTime && siLastFailedLoginTime + (U32) siJiraLoginRetryPeriod > uTime)
		return false;

	estrCopy2(&spJiraServer, pServer);
	siJiraPort = port;

	pJiraClient = httpClientConnect(spJiraServer, siJiraPort, NULL, NULL, NULL, NULL, NULL, true, 0);
	if(!pJiraClient)
	{
		siLastFailedLoginTime = uTime;
		return false;
	}

	estrStackCreate(&pHTTPHeader);
	estrStackCreate(&pRequestString);
	estrStackCreate(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp1:login xmlns:namesp1=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"<in1 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in1>"
		"</namesp1:login>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		
		pUserName,
		pPassword);

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",

		pServer,
		port,
		(int)strlen(pRequestString));

	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		if(findAuthData(pJiraResponse))
		{
			printf("Auth [%d]: %s\n", httpClientGetResponseCode(pJiraClient), spJiraAuth);
			bSuccessfulLogin = true;
		}
	}
	if (!bSuccessfulLogin)
		siLastFailedLoginTime = uTime;
	else
		siLastFailedLoginTime = 0;

	estrDestroy(&pHTTPHeader);
	estrDestroy(&pRequestString);
	estrDestroy(&pJiraResponse);
	httpClientDestroy(&pJiraClient);

	return bSuccessfulLogin;
}

void jiraLogout(void)
{
	// Technically, we aren't really "logged in". Let's just forget our auth and server info.
	estrDestroy(&spJiraAuth);
	estrDestroy(&spJiraServer);
	siJiraPort = 0;
}

bool jiraIsLoggedIn(void)
{
	return (spJiraAuth != NULL);
}

#define USERS_START "<users "
#define MULTIREF_START "<multiRef "
#define USERNAME_START "<name "
#define FULLNAME_START "<fullname "
#define EMAIL_START "<email "
static bool internalParseUsers(JiraUserList *pList, char *pText)
{
	char *pCurrMultiRef;

	char *pUserName;
	char *pFullName;
	char *pEmail;

	pCurrMultiRef = strstr(pText, USERS_START);
	if(!pCurrMultiRef)
	{
		return false;
	}

	while(pCurrMultiRef = strstr(pCurrMultiRef, MULTIREF_START))
	{
		pCurrMultiRef += (int)strlen(MULTIREF_START);

		pUserName = NULL;
		pFullName = NULL;
		pEmail    = NULL;

		pEmail = getClosestTagData(&pCurrMultiRef, EMAIL_START);
		if(!pEmail) break;
		pFullName = getClosestTagData(&pCurrMultiRef, FULLNAME_START);
		if(!pFullName) break;
		pUserName = getClosestTagData(&pCurrMultiRef, USERNAME_START);
		if(!pUserName) break;

		if(pUserName && pFullName && pEmail)
		{
			JiraUser *pUser = StructCreate(parse_JiraUser);
			estrCopy2(&pUser->pUserName, pUserName);
			estrCopy2(&pUser->pFullName, pFullName);
			estrCopy2(&pUser->pEmailAddress, pEmail);
			eaPush(&pList->ppUsers, pUser);
		}
	}

	return true;
}

static int SortByFullName(const JiraUser **pEntry1, const JiraUser **pEntry2)
{
	return stricmp((*pEntry1)->pFullName, (*pEntry2)->pFullName); 
}

bool jiraGetUsers(JiraUserList *pList, const char *pGroupName)
{
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;
	bool bGotUsers          = false;

	pJiraClient = httpClientConnect(spJiraServer, siJiraPort, NULL, NULL, NULL, NULL, NULL, true, 0);
	if(!pJiraClient)
	{
		return false;
	}

	estrStackCreate(&pHTTPHeader);
	estrStackCreate(&pRequestString);
	estrStackCreate(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp2:getGroup xmlns:namesp2=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"<in1 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in1>"
		"</namesp2:getGroup>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		
		spJiraAuth,
		pGroupName);

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",

		spJiraServer,
		siJiraPort,
		(int)strlen(pRequestString));

	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		internalParseUsers(pList, pJiraResponse);
		if(eaSize(&pList->ppUsers) > 0)
		{
			eaQSort(pList->ppUsers, SortByFullName);
		}
	}

	estrDestroy(&pHTTPHeader);
	estrDestroy(&pRequestString);
	estrDestroy(&pJiraResponse);
	httpClientDestroy(&pJiraClient);

	return true;
}

// ---------------------------------------------------------------------------------

static bool internalParseProjects(JiraProjectList *pList, char *pText)
{
	char *pCurrMultiRef;

	char *pID;
	char *pKey;
	char *pName;

	pCurrMultiRef = pText;
	while(pCurrMultiRef = strstr(pCurrMultiRef, MULTIREF_START))
	{
		pCurrMultiRef += (int)strlen(MULTIREF_START);

		pID = getClosestTagData(&pCurrMultiRef, "<id ");
		if(!pID) break;
		pKey = getClosestTagData(&pCurrMultiRef, "<key ");
		if(!pKey) break;
		pName = getClosestTagData(&pCurrMultiRef, "<name ");
		if(!pName) break;

		if(pID && pKey && pName)
		{
			JiraProject *pProject = StructCreate(parse_JiraProject);
			estrCopy2(&pProject->pID,   pID);
			estrCopy2(&pProject->pKey,  pKey);
			estrCopy2(&pProject->pName, pName);
			eaPush(&pList->ppProjects, pProject);
		}
	}

	return true;
}

static int SortByName(const JiraProject **pEntry1, const JiraProject **pEntry2)
{
	return stricmp((*pEntry1)->pName, (*pEntry2)->pName); 
}

bool jiraGetProjects(JiraProjectList *pList)
{
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;
	bool bGotUsers          = false;

	pJiraClient = httpClientConnect(spJiraServer, siJiraPort, NULL, NULL, NULL, NULL, NULL, true, 0);
	if(!pJiraClient)
	{
		return false;
	}

	estrStackCreate(&pHTTPHeader);
	estrStackCreate(&pRequestString);
	estrStackCreate(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp2:getProjectsNoSchemes xmlns:namesp2=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"</namesp2:getProjectsNoSchemes>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		
		spJiraAuth);

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",

		spJiraServer,
		siJiraPort,
		(int)strlen(pRequestString));

	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		internalParseProjects(pList, pJiraResponse);
		if(eaSize(&pList->ppProjects) > 0)
		{
			eaQSort(pList->ppProjects, SortByName);
		}
	}

	estrDestroy(&pHTTPHeader);
	estrDestroy(&pRequestString);
	estrDestroy(&pJiraResponse);
	httpClientDestroy(&pJiraClient);

	return true;
}


// ---------------------------------------------------------------------------------

static bool internalParseComponents(JiraComponentList *pList, char *pText)
{
	char *pCurrMultiRef;

	char *pID;
	char *pName;

	pCurrMultiRef = pText;
	while(pCurrMultiRef = strstr(pCurrMultiRef, MULTIREF_START))
	{
		pCurrMultiRef += (int)strlen(MULTIREF_START);

		pID = getClosestTagData(&pCurrMultiRef, "<id ");
		if(!pID) break;
		pName = getClosestTagData(&pCurrMultiRef, "<name ");
		if(!pName) break;

		if(pID && pName)
		{
			JiraComponent *pComponent = StructCreate(parse_JiraComponent);
			estrCopy2(&pComponent->pID,   pID);
			estrCopy2(&pComponent->pName, pName);
			eaPush(&pList->ppComponents, pComponent);
		}
	}

	return true;
}

static int SortComponentsByName(const JiraComponent **pEntry1, const JiraComponent **pEntry2)
{
	return stricmp((*pEntry1)->pName, (*pEntry2)->pName); 
}

bool jiraGetComponents(JiraProject* pProject, JiraComponentList *pList)
{
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;
	bool bGotUsers          = false;

	if (!jiraDefaultLogin())
		return false;

	pJiraClient = httpClientConnect(spJiraServer, siJiraPort,NULL, NULL, NULL, NULL, NULL, true, 0);
	if(!pJiraClient)
	{
		return false;
	}

	estrStackCreate(&pHTTPHeader);
	estrStackCreate(&pRequestString);
	estrStackCreate(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp2:getComponents xmlns:namesp2=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"<in1 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in1>"
		"</namesp2:getComponents>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		
		spJiraAuth,
		pProject->pKey);

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",

		spJiraServer,
		siJiraPort,
		(int)strlen(pRequestString));

	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		internalParseComponents(pList, pJiraResponse);
		if(eaSize(&pList->ppComponents) > 0)
		{
			eaQSort(pList->ppComponents, SortComponentsByName);
		}
	}

	estrDestroy(&pHTTPHeader);
	estrDestroy(&pRequestString);
	estrDestroy(&pJiraResponse);
	httpClientDestroy(&pJiraClient);

	jiraLogout();

	return true;
}

// ---------------------------------------------------------------------------------

static bool internalParseCreateIssue(char *pText, char *pOutputKey, int iBufferSize)
{
	char *pCurrMultiRef;
	char *pKey;

	pCurrMultiRef = pText;
	if(pCurrMultiRef = strstr(pCurrMultiRef, MULTIREF_START))
	{
		pCurrMultiRef += (int)strlen(MULTIREF_START);

		pKey = getClosestTagData(&pCurrMultiRef, "<key ");
		if(pKey)
		{
			strcpy_s(pOutputKey, iBufferSize, pKey);
			return true;
		}
	}

	return false;
}

bool jiraCreateIssue(const char *pProject, const char *pSummary, const char *pDescription, const char *pAssignee, 
					 int iPriority, int iOrd, const JiraComponent* pComponent, const char *pLabel, char *outputKey,int iBufferSize)
{
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;
	bool bGotUsers          = false;
	bool bRet				= false;
	char *pTempValue        = NULL;
	int iJiraIssue			= siDefaultType;
	char *pLabelCustomField = NULL;

	if (iPriority == 3)
		iPriority++; // hard-coded hack to get around the fact that there are only priorities of 1,2,4 (no 3)

	pJiraClient = httpClientConnect(spJiraServer, siJiraPort, NULL, NULL, NULL, NULL, NULL, true, 0);
	if(!pJiraClient)
	{
		return false;
	}

	if(!pProject)
	{
		pProject = spDefaultProject;
	}
	else if (!stricmp(pProject, "CO"))
	{
		iJiraIssue = siDefaultType2;
	}
	
	if(!pAssignee)
	{
		pAssignee = "";
	}

	estrClear(&pHTTPHeader);
	estrClear(&pRequestString);
	estrClear(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:soapenc=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp2:createIssue xmlns:namesp2=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"<in1 xsi:type=\"ns:RemoteIssue\" xmlns:ns=\"http://beans.soap.rpc.jira.atlassian.com\">",
		
		spJiraAuth);

	estrCopyWithHTMLEscaping(&pTempValue, pAssignee, false);
	estrConcatf(&pRequestString, "<assignee xsi:type=\"xsd:string\">%s</assignee>", pTempValue);

	estrCopyWithHTMLEscaping(&pTempValue, pSummary, false);
	estrConcatf(&pRequestString, "<summary xsi:type=\"xsd:string\">%s</summary>", pTempValue);

	estrCopyWithHTMLEscaping(&pTempValue, pProject, false);
	estrConcatf(&pRequestString, "<project xsi:type=\"xsd:string\">%s</project>", pTempValue);

	estrConcatf(&pRequestString, "<type xsi:type=\"xsd:string\">%d</type>", iJiraIssue);

	estrConcatf(&pRequestString, "<priority xsi:type=\"xsd:string\">%d</priority>", iPriority);

	if (pLabel && *pLabel)
	{
		estrConcatf(&pLabelCustomField, 
			"<customFieldValues xsi:type=\"ns:RemoteCustomFieldValue\">"
			"<customfieldId xsi:type=\"xsd:string\">customfield_10080</customfieldId>"
			"<values xsi:type=\"xsd:string\">%s</values>"
			"</customFieldValues>", pLabel);
	}
	else estrCopy2(&pLabelCustomField, "");

	if (!stricmp(pProject, "CO"))
	{
		estrConcatf(&pRequestString, 
			"<customFieldValues xsi:type=\"jir:ArrayOf_tns1_RemoteCustomFieldValue\" soapenc:arrayType=\"ns:RemoteCustomFieldValue[]\" xmlns:jir=\"http://jira.atlassian.com/rpc/soap/jirasoapservice-v2\">"
			"<customFieldValues xsi:type=\"ns:RemoteCustomFieldValue\">"
			"<customfieldId xsi:type=\"xsd:string\">customfield_10122</customfieldId>"
			"<values xsi:type=\"xsd:string\">1</values>"// Severity = 1
			"</customFieldValues>"

			"<customFieldValues xsi:type=\"ns:RemoteCustomFieldValue\">"
			"<customfieldId xsi:type=\"xsd:string\">customfield_10050</customfieldId>"
			"<values xsi:type=\"xsd:string\">%d</values>"// Ord
			"</customFieldValues>"

			"%s" // Label

			"<customFieldValues xsi:type=\"ns:RemoteCustomFieldValue\">"
			"<customfieldId xsi:type=\"xsd:string\">customfield_10212</customfieldId>"
			"<values xsi:type=\"xsd:string\">IsNotScheduled</values>"
			"</customFieldValues>"
			"</customFieldValues>", 
			iOrd,
			pLabelCustomField);
		estrDestroy(&pLabelCustomField);
	}
	else
	{
		estrConcatf(&pRequestString, 
			"<customFieldValues xsi:type=\"jir:ArrayOf_tns1_RemoteCustomFieldValue\" soapenc:arrayType=\"ns:RemoteCustomFieldValue[]\" xmlns:jir=\"http://jira.atlassian.com/rpc/soap/jirasoapservice-v2\">"
			"<customFieldValues xsi:type=\"ns:RemoteCustomFieldValue\">"
			"<customfieldId xsi:type=\"xsd:string\">customfield_10050</customfieldId>"
			"<values xsi:type=\"xsd:string\">%d</values>"// Ord
			"</customFieldValues>"

			"%s"

			"<customFieldValues xsi:type=\"ns:RemoteCustomFieldValue\">"
			"<customfieldId xsi:type=\"xsd:string\">customfield_10212</customfieldId>"
			"<values xsi:type=\"xsd:string\">IsNotScheduled</values>"
			"</customFieldValues>"
			"</customFieldValues>", 
			iOrd,
			pLabelCustomField);
		estrDestroy(&pLabelCustomField);
	}
	
	estrCopyWithHTMLEscaping(&pTempValue, pDescription, false);
	estrConcatf(&pRequestString, "<description xsi:type=\"xsd:string\">%s</description>", pTempValue);

	if (pComponent)
	{
		estrConcatf(&pRequestString,
			"<components xsi:type=\"jir:ArrayOf_tns1_RemoteComponent\" soapenc:arrayType=\"ns:RemoteComponent[]\" xmlns:jir=\"http://jira.atlassian.com/rpc/soap/jirasoapservice-v2\">"
			"<components xsi:type=\"ns:RemoteComponent\">"
			"<id xsi:type=\"xsd:string\">%s</id>"
			"<name xsi:type=\"xsd:string\">%s</name>"
			"</components>"
			"</components>",

			pComponent->pID,
			pComponent->pName);
	}

	if(spCreateIssueAdditionalXML)
	{
		estrConcatf(&pRequestString, "%s", spCreateIssueAdditionalXML);
	}

	estrConcatf(&pRequestString, 
		"</in1>"
		"</namesp2:createIssue>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>");

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",

		spJiraServer,
		siJiraPort,
		(int)strlen(pRequestString));

	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		bRet = internalParseCreateIssue(pJiraResponse, outputKey, iBufferSize);
	}

	estrDestroy(&pHTTPHeader);
	estrDestroy(&pRequestString);
	estrDestroy(&pJiraResponse);
	estrDestroy(&pTempValue);
	httpClientDestroy(&pJiraClient);

	return bRet;
}

static void IssueParseHandler (JiraXMLState *state, XMLStateEnum eAction)
{
	switch (eAction)
	{
	case XML_CHARACTER:
		if (state->el && state->data && state->characters)
		{
			NOCONST(JiraIssue) *issue = CONTAINER_NOCONST(JiraIssue, state->data);
			if (stricmp(state->el, "assignee") == 0)
			{
				estrCopy2(&issue->assignee, state->characters);
			}
			else if (stricmp(state->el, "status") == 0)
			{
				issue->status = atoi(state->characters);
			}
			else if (stricmp(state->el, "resolution") == 0)
			{
				issue->resolution = atoi(state->characters);
			}
		}
	}
}
static bool internalParseGetIssue(char *pText, NOCONST(JiraIssue) *issue)
{
	bool bSuccess;
	NOCONST(JiraIssue) copy = {0};
	
	bSuccess = jiraXMLParseString(pText, IssueParseHandler, &copy);

	if (bSuccess)
	{
		estrCopy2(&issue->assignee, copy.assignee);
		issue->status = copy.status;
		issue->resolution = copy.resolution;
	}
	StructDeInitNoConst(parse_JiraIssue, &copy);
	return bSuccess;
}

bool jiraGetIssue(JiraIssue *pConstJiraIssue, NetComm *comm)
{
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;
	bool bRet				= false;
	NOCONST(JiraIssue) *pJiraIssue = CONTAINER_NOCONST(JiraIssue, pConstJiraIssue);

	if (!pJiraIssue)
		return false;

	pJiraClient = httpClientConnect(jiraGetAddress(), jiraGetPort(), NULL, NULL, NULL, NULL, comm ? comm : NULL, true, 0);
	if(!pJiraClient)
	{
		return false;
	}

	estrClear(&pHTTPHeader);
	estrClear(&pRequestString);
	estrClear(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:soapenc=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp2:getIssue xmlns:namesp2=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"<in1 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in1>"
		"</namesp2:getIssue>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		spJiraAuth, pJiraIssue->key);

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",
		spJiraServer, siJiraPort,
		(int)strlen(pRequestString));
	
	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		bRet = internalParseGetIssue(pJiraResponse, pJiraIssue);
	}
	estrDestroy(&pHTTPHeader);
	estrDestroy(&pJiraResponse);
	estrDestroy(&pRequestString);
	httpClientDestroy(&pJiraClient);
	return bRet;
}

static void StatusParseHandler (JiraXMLState *state, XMLStateEnum eAction)
{
	switch (eAction)
	{
	case XML_ENTER:
		if (state->el && stricmp(state->el, "multiref") == 0)
		{
			JiraParseList *pList = (JiraParseList*) state->data;
			if (pList->pCurrentParse)
			{
				StructDeInit(parse_JiraStatus, pList->pCurrentParse);
			}
			else
				pList->pCurrentParse = StructCreate(parse_JiraStatus);
		}
	xcase XML_EXIT:
		if (state->el && stricmp(state->el, "multiref") == 0)
		{
			JiraParseList *pList = (JiraParseList*) state->data;
			JiraStatus *status = (JiraStatus*) pList->pCurrentParse;

			if (status)
			{
				if (status->id && status->pName)
				{
					printf("Jira Status: %d, %s, %s\n", status->id, status->pName, status->pDescription);
					eaPush(&pList->ppCompleteParses, status);
				}
				else
					StructDestroy(parse_JiraStatus, status);
			}
			pList->pCurrentParse = NULL;
		}
	xcase XML_CHARACTER:
		if (state->el && state->data && state->characters)
		{
			JiraParseList *pList = (JiraParseList*) state->data;
			JiraStatus *status = (JiraStatus*) pList->pCurrentParse;
			if (stricmp(state->el, "name") == 0)
			{
				estrCopy2(&status->pName, state->characters);
			}
			else if (stricmp(state->el, "description") == 0)
			{
				estrCopy2(&status->pDescription, state->characters);
			}
			else if (stricmp(state->el, "id") == 0)
			{
				status->id = atoi(state->characters);
			}
		}
	}
}
static bool internalParseGetStatuses(char *pText)
{
	JiraParseList parseList = {0};
	bool bSuccess;
	if (!sJiraStatusTable)
	{
		sJiraStatusTable = stashTableCreateInt(10);
	}

	bSuccess = jiraXMLParseString(pText, StatusParseHandler, &parseList);
	if (!bSuccess)
	{
		Errorf("Error parsing XML.");
		eaDestroyStructVoid(&parseList.ppCompleteParses, parse_JiraStatus);
		if (parseList.pCurrentParse)
			StructDestroy(parse_JiraStatus, parseList.pCurrentParse);
	}
	else
	{
		int i;
		for (i=eaSize(&parseList.ppCompleteParses)-1; i>=0; i--)
		{
			JiraStatus *status = parseList.ppCompleteParses[i];
			if (stashIntAddPointer(sJiraStatusTable, status->id, status, false))
				printf("Added status: %s\n", status->pName);
			else
				StructDestroy(parse_JiraStatus, status);
		}
		eaDestroy(&parseList.ppCompleteParses);
	}
	return bSuccess;
}

static void ResolutionParseHandler (JiraXMLState *state, XMLStateEnum eAction)
{
	switch (eAction)
	{
	case XML_ENTER:
		if (state->el && stricmp(state->el, "multiref") == 0)
		{
			JiraParseList *pList = (JiraParseList*) state->data;
			if (pList->pCurrentParse)
			{
				StructDeInit(parse_JiraResolution, pList->pCurrentParse);
			}
			else
				pList->pCurrentParse = StructCreate(parse_JiraResolution);
		}
	xcase XML_EXIT:
		if (state->el && stricmp(state->el, "multiref") == 0)
		{
			JiraParseList *pList = (JiraParseList*) state->data;
			JiraResolution *resolution = (JiraResolution*) pList->pCurrentParse;

			if (resolution)
			{
				if (resolution->id && resolution->pName)
				{
					printf("Jira Resolution: %d, %s, %s\n", resolution->id, resolution->pName, resolution->pDescription);
					eaPush(&pList->ppCompleteParses, resolution);
				}
				else
					StructDestroy(parse_JiraResolution, resolution);
			}
			pList->pCurrentParse = NULL;
		}
	xcase XML_CHARACTER:
		if (state->el && state->data && state->characters)
		{
			JiraParseList *pList = (JiraParseList*) state->data;
			JiraResolution *resolution = (JiraResolution*) pList->pCurrentParse;
			if (stricmp(state->el, "name") == 0)
			{
				estrCopy2(&resolution->pName, state->characters);
			}
			else if (stricmp(state->el, "description") == 0)
			{
				estrCopy2(&resolution->pDescription, state->characters);
			}
			else if (stricmp(state->el, "id") == 0)
			{
				resolution->id = atoi(state->characters);
			}
		}
	}
}
static bool internalParseGetResolutions(char *pText)
{
	JiraParseList parseList = {0};
	bool bSuccess;

	if (!sJiraResolutionTable)
	{
		sJiraResolutionTable = stashTableCreateInt(10);
	}

	bSuccess = jiraXMLParseString(pText, ResolutionParseHandler, &parseList);
	if (!bSuccess)
	{
		Errorf("Error parsing XML.");
		eaDestroyStructVoid(&parseList.ppCompleteParses, parse_JiraStatus);
		if (parseList.pCurrentParse)
			StructDestroy(parse_JiraStatus, parseList.pCurrentParse);
	}
	else
	{
		int i;
		for (i=eaSize(&parseList.ppCompleteParses)-1; i>=0; i--)
		{
			JiraResolution *resolution = parseList.ppCompleteParses[i];
			if (stashIntAddPointer(sJiraResolutionTable, resolution->id, resolution, false))
				printf("Added resolution: %s\n", resolution->pName);
			else
				StructDestroy(parse_JiraResolution, resolution);
		}
		eaDestroy(&parseList.ppCompleteParses);
	}
	return bSuccess;
}
static bool jiraGetStatuses(void)
{
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;

	pJiraClient = httpClientConnect(spJiraServer, siJiraPort, NULL, NULL, NULL, NULL, NULL, true, 0);
	if(!pJiraClient)
	{
		return false;
	}

	estrClear(&pHTTPHeader);
	estrClear(&pRequestString);
	estrClear(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:soapenc=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp2:getStatuses xmlns:namesp2=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"</namesp2:getStatuses>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		spJiraAuth);

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",
		spJiraServer, siJiraPort,
		(int)strlen(pRequestString));
	
	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		internalParseGetStatuses(pJiraResponse);
	}
	estrDestroy(&pHTTPHeader);
	estrDestroy(&pJiraResponse);
	estrDestroy(&pRequestString);
	httpClientDestroy(&pJiraClient);
	return true;
}

static bool jiraGetResolutions(void)
{
	HttpClient *pJiraClient = NULL;
	char *pHTTPHeader       = NULL;
	char *pRequestString    = NULL;
	char *pJiraResponse     = NULL;

	pJiraClient = httpClientConnect(spJiraServer, siJiraPort, NULL, NULL, NULL, NULL, NULL, true, 0);
	if(!pJiraClient)
	{
		return false;
	}

	estrClear(&pHTTPHeader);
	estrClear(&pRequestString);
	estrClear(&pJiraResponse);

	estrPrintf(&pRequestString,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/1999/XMLSchema-instance\" xmlns:soapenc=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<SOAP-ENV:Body>"
		"<namesp2:getResolutions xmlns:namesp2=\"http://soap.rpc.jira.atlassian.com\">"
		"<in0 xsi:type=\"ns:string\" xmlns:ns=\"http://www.w3.org/2001/XMLSchema\">%s</in0>"
		"</namesp2:getResolutions>"
		"</SOAP-ENV:Body>"
		"</SOAP-ENV:Envelope>",
		spJiraAuth);

	estrPrintf(&pHTTPHeader,
		"POST /rpc/soap/jirasoapservice-v2 HTTP/1.0\n"
		"Host: %s:%d\n"
		"User-Agent: Cryptic JiraLib\n"
		"Content-Length: %d\n"
		"Content-Type: text/xml; charset=utf-8\n"
		"SOAPAction: \"\"\n"
		"\n",
		spJiraServer, siJiraPort,
		(int)strlen(pRequestString));
	
	httpClientSendBytesRaw(pJiraClient, pHTTPHeader,  (int)strlen(pHTTPHeader));
	httpClientSendBytesRaw(pJiraClient, pRequestString, (int)strlen(pRequestString));

	if(httpClientWaitForResponseText(pJiraClient, &pJiraResponse))
	{
		internalParseGetResolutions(pJiraResponse);
	}
	estrDestroy(&pHTTPHeader);
	estrDestroy(&pJiraResponse);
	estrDestroy(&pRequestString);
	httpClientDestroy(&pJiraClient);
	return true;
}

static void jiraLoadStatusStrings(void)
{
	bool bLoggedIn = jiraIsLoggedIn();
	if (!bLoggedIn)
		bLoggedIn = jiraDefaultLogin();
	if (bLoggedIn)
		jiraGetStatuses();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLIntStringList* TT_GetJiraStatusList(void)
{
	XMLIntStringList *list = StructCreate(parse_XMLIntStringList);
	if (!sJiraStatusTable)
		jiraLoadStatusStrings();
	if (sJiraStatusTable)
	{
		FOR_EACH_IN_STASHTABLE2(sJiraStatusTable, el) 
		{
			IntStringPair *item = StructCreate(parse_IntStringPair);
			JiraStatus *status = (JiraStatus*)stashElementGetPointer(el);
			item->i4 = stashElementGetIntKey(el);
			estrPrintf(&item->string, "%s", status->pName);
			eaPush(&list->list, item);
		}
		FOR_EACH_END;
	}
	return list;
}

const char * jiraGetStatusString(int status)
{
	static char statusUnknown[64];
	JiraStatus *statusStruct;
	if (!sJiraStatusTable)
		jiraLoadStatusStrings();

	if (sJiraStatusTable && stashIntFindPointer(sJiraStatusTable, status, &statusStruct))
		return statusStruct->pName;
	sprintf(statusUnknown, "Unknown Status: %d", status);
	return statusUnknown;
}

const char * jiraGetResolutionString(int resolution)
{
	static char resolutionUnknown[64];
	JiraResolution *resolutionStruct;
	if (!sJiraResolutionTable)
	{
		bool bLoggedIn = jiraIsLoggedIn();
		if (!bLoggedIn)
			bLoggedIn = jiraDefaultLogin();
		if (bLoggedIn)
			jiraGetResolutions();
	}

	if (sJiraResolutionTable && stashIntFindPointer(sJiraResolutionTable, resolution, &resolutionStruct))
		return resolutionStruct->pName;
	sprintf(resolutionUnknown, "Unknown Resolution: %d", resolution);
	return resolutionUnknown;
}

static char * getClosestTagData(char **pBuffer, const char *pTagStart)
{
	char *p;

	if(*pBuffer == NULL)
	{
		return NULL;
	}

	// ------------------------------------------------------
	// Find the username
	p = strstr(*pBuffer, pTagStart);
	if(p)
	{
		p = strchr(p, '>');
		if(p)
		{
			char *pTagData = ++p;

			if (*(p-1) == '/') // self-closing tag
				return NULL;

			while(*p && *p != '<') p++;
			if(!*p)
			{
				*pBuffer = NULL; // Eat the rest of the buffer, we have bogus XML
				return NULL;
			}

			*p = 0;
			*pBuffer = p+1;

			return pTagData;
		}
	}

	return NULL;
}

#include "jira_h_ast.c"
#include "jira_c_ast.c"