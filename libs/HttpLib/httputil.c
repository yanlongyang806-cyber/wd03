#include "httputil.h"

#include "AutoGen/httputil_c_ast.h"
#include "net.h"
#include "HttpLib.h"
#include "estring.h"
#include "earray.h"
#include "textparser.h"
#include "mathutil.h"
#include "strings_opt.h"
#include "StringUtil.h"
#include "NameValuePair.h"
#include "url.h"
#include "timing.h"
#include "timing_profiler.h"

#include "AutoGen/url_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static HttpRequestHandler defaultRequestHandler = NULL;
static void *defaultRequestHandlerUserData = NULL;
static bool g_xmlrpc_enabled = false;

AUTO_STRUCT;
typedef struct HttpLegacySupport
{
	HttpClientStateDefault *pClientState;
	char *data;
} HttpLegacySupport;

// Return true if a character matches the criteria for an HTTP LWS character.
static bool isLwsChar(char c)
{
	return c == ' ' || c == '\t';
}

// Return true if a character is an HTTP separator.
static bool isSeparator(char c)
{
	switch (c)
	{
		case '(':
		case ')':
		case '<':
		case '>':
		case '@':
		case ',':
		case ';':
		case ':':
		case '\\':
		case '"':
		case '/':
		case '[':
		case ']':
		case '?':
		case '=':
		case '{':
		case '}':
		case ' ':
		case '\t':
			return true;
	}
	return false;
}

// Return true if c is a valid character in an HTTP token.
static bool isValidTokenChar(char c)
{
	return c >= 32 && c <= 126 && !isSeparator(c);
}

// Return true if str is a valid HTTP token.
static bool isValidToken(const char *str)
{
	const char *i;
	if (!*str)
		return false;
	for (i = str; *i; ++i)
	{
		if (!isValidTokenChar(*i))
			return false;
	}
	return true;
}

// Skip LWS, if present.
static char *skipLws(char *str)
{
	while (isLwsChar(*str))
		++str;
	return str;
}

// Return a pointer to the first character of trailing LWS, or the null terminator if none present.
static char *trailingLws(char *str)
{
	char *end = str + strlen(str);
	while (end > str && isLwsChar(*(end - 1)))
		--end;
	return end;
}

static void hrParseCookies(HttpRequest *req, char *pCookieLine)
{
	static const char* sDelimiters = ";";
	char *pCookieString = NULL;
	char *pNameValueSplit = NULL;
	char *pTemp = NULL;
	NameValuePair *pNewCookie;

	StructDestroy(parse_NameValuePairList, req->cookies);
	req->cookies = StructCreate(parse_NameValuePairList);

	pCookieString = strtok_s(pCookieLine, sDelimiters, &pTemp);
	while (pCookieString)
	{
		while (*pCookieString == ' ')
			pCookieString++;
		pNameValueSplit = strchr(pCookieString, '=');
		if (pNameValueSplit)
		{
			*pNameValueSplit++ = 0;
		}

		pNewCookie = StructCreate(parse_NameValuePair);
		pNewCookie->pName = strdup(pCookieString);
		pNewCookie->pValue = strdup(pNameValueSplit);
		eaPush(&req->cookies->ppPairs, pNewCookie);

		pCookieString = strtok_s(NULL, sDelimiters, &pTemp);
	}
}

HttpRequest * hrParse(const char *rawHttpRequest)
{
	bool success = false;
	HttpRequest *req;
	NameValuePair *pair;
	char *tempHttpRequest = NULL;
	char *c;
	char *lastLine = NULL;
	char *line;
	char *loc;

	req = StructCreate(parse_HttpRequest);
	estrStackCreate(&tempHttpRequest);
	estrCopy2(&tempHttpRequest, rawHttpRequest);
	line = strtok_r(tempHttpRequest, "\r\n", &lastLine);

	// First line ... parse out method and path
	if(line && *line)
	{
		int matches;

		c = line;
		if(!strnicmp(c, "GET ", 4))
		{
			req->method = HTTPMETHOD_GET;
			c += 4;
		}
		else if(!strnicmp(c, "POST ", 5))
		{
			req->method = HTTPMETHOD_POST;
			c += 5;
		}
		else
			goto cleanup;

		if(*c != '/')
			goto cleanup;

		loc = strchr(c, ' ');
		if(!loc)
			goto cleanup;

		*loc = 0;
		loc++;
		if(strnicmp(loc, "HTTP/", 5)) // This fires if the URL had a raw space in it
		{
			goto cleanup;
		}
		matches = sscanf(loc, "HTTP/%u.%u",
			&req->major_version,
			&req->minor_version);
		if (matches != 2)
			goto cleanup;

		loc = strchr(c, '?');
		if(loc)
		{
			*loc = 0;
			loc++;
			//req->path = strdup(c);
			req->path = calloc(strlen(c)+1, 1);
			urlUnescape(c, req->path, (int)strlen(c)+1);
			c = loc;
			
			req->vars = urlToUrlArgumentList_Internal(c, true);
		}
		else
		{
			req->path = calloc(strlen(c)+1, 1);
			urlUnescape(c, req->path, (int)strlen(c)+1);
		}
	}

	// Remaining lines are all headers
	req->headers = StructCreate(parse_NameValuePairList);
	while((line = strtok_r(NULL, "\r\n", &lastLine)) && *line)
	{
		// Find end of header field value and null-terminate it.
		loc = strchr(line, ':');
		if(!loc)
			goto cleanup; // Someone sent a naughty header ... maybe we shouldn't care?
		*loc = 0;
		if (!isValidToken(line))
			goto cleanup;

		// Trim leading and trailing LWS.
		++loc;
		loc = skipLws(loc);
		*trailingLws(loc) = 0;

		if(!strcmp(line, "Cookie"))
		{
			hrParseCookies(req, loc);
		}
		else
		{
			pair = StructCreate(parse_NameValuePair);
			pair->pName  = strdup(line);
			pair->pValue = strdup(loc);
			eaPush(&req->headers->ppPairs, pair);
		}
	}

	// We made it!
	success = true;

cleanup:
	if(!success)
	{
		StructDestroy(parse_HttpRequest, req);
		req = NULL;
	}
	estrDestroy(&tempHttpRequest);
	return req;
}

HttpResponse * hrParseResponse(const char *rawHttpResponse)
{
	bool success = false;
	HttpResponse *response;
	NameValuePair *pair;
	char *tempHttpResponse = NULL;
	char *lastLine = NULL;
	char *line;
	char *loc;

	PERFINFO_AUTO_START_FUNC();

	response = StructCreate(parse_HttpResponse);
	estrStackCreate(&tempHttpResponse);
	estrCopy2(&tempHttpResponse, rawHttpResponse);
	line = strtok_r(tempHttpResponse, "\r\n", &lastLine);

	// First line ... parse out method and path
	if(line && *line)
	{
		int matches = sscanf_s(line, "HTTP/%u.%u %d %[^\r]",
			&response->major_version,
			&response->minor_version,
			&response->code,
			SAFESTR(response->reason));
		if (matches != 4)
			goto cleanup;
	}

	// Remaining lines are all headers
	response->headers = StructCreate(parse_NameValuePairList);
	while((line = strtok_r(NULL, "\r\n", &lastLine)) && *line)
	{
		// Find end of header field value and null-terminate it.
		loc = strchr(line, ':');
		if(!loc)
			goto cleanup; // Someone sent a naughty header ... maybe we shouldn't care?
		*loc = 0;
		if (!isValidToken(line))
			goto cleanup;

		// Trim leading and trailing LWS.
		++loc;
		loc = skipLws(loc);
		*trailingLws(loc) = 0;

		// Add pair to list.
		pair = StructCreate(parse_NameValuePair);
		pair->pName  = strdup(line);
		pair->pValue = strdup(loc);
		eaPush(&response->headers->ppPairs, pair);
	}

	// We made it!
	success = true;

cleanup:
	if(!success)
	{
		StructDestroy(parse_HttpResponse, response);
		response = NULL;
	}
	estrDestroy(&tempHttpResponse);
	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

static void hrHandleMultipartEntry(HttpRequest *req, const U8 *pData, size_t size)
{
	static const char *pNameString = "name=\"";
	size_t headerSize = 0;
	char *pName = NULL;
	const char *pNameLoc = NULL;
	const U8 *pValue = NULL;
	size_t valueSize = 0;

	// Find the size of the header.
	while (headerSize < size)
	{
		if (headerSize >= 4 &&
			pData[headerSize - 1] == '\n' && pData[headerSize - 2] == '\r' &&
			pData[headerSize - 3] == '\n' && pData[headerSize - 4] == '\r')
		{
			break;
		}

		headerSize++;
	}

	// Value size is the rest, minus a CRLF
	valueSize = size - headerSize;
	if (valueSize > 0)
	{
		valueSize -= 2;
	}
	pValue = pData + headerSize;

	// Get name of value from headers
	pNameLoc = strstr((const char*)pData, pNameString);
	if (pNameLoc)
	{
		const char *pNameLocEnd = strchr(pNameLoc + strlen(pNameString), '"');
		pNameLoc += strlen(pNameString);

		if (pNameLocEnd)
		{
			estrConcat(&pName, pNameLoc, pNameLocEnd - pNameLoc);
		}
	}

	if (pName)
	{
		UrlArgument *p = StructCreate(parse_UrlArgument);

		p->method = HTTPMETHOD_POST;
		p->arg   = strdup(pName);

		if (valueSize > 0)
		{
			p->value = calloc(1, valueSize + 1); // 1 extra for NULL byte in case it is interpreted as a string
			memcpy(p->value, pValue, valueSize);
			p->length = (U32)valueSize;
		}
		else
		{
			p->value = calloc(1, 1); // empty string
		}
		
		eaPush(&req->vars->ppUrlArgList, p);

		estrDestroy(&pName);
	}
}

void hrParsePostData(HttpRequest *req, const char *rawPostData)
{
	const char *pContentType = NULL;
	static const char *pBoundaryString = "boundary=";

	if(rawPostData && *rawPostData == '<') // Early out on XMLRPC-ish post data. Sneaky!
		return;

	if(!req->vars)
		req->vars = StructCreate(parse_UrlArgumentList);

	pContentType = hrFindSafeHeader(req, "content-type");
	if (strStartsWith(pContentType, "multipart/form-data;"))
	{
		const U8 *pBoundary = strstr(pContentType, pBoundaryString);
		if (pBoundary && rawPostData)
		{
			const U8 * pPostData = rawPostData;
			size_t postDataSize = estrLength(&rawPostData);
			size_t curPos = 0;
			size_t boundarySize = 0;
			char *pValue = NULL;

			pBoundary += strlen(pBoundaryString);
			boundarySize = strlen(pBoundary);

			estrStackCreate(&pValue);

			for (curPos = 0; curPos < postDataSize; curPos++)
			{
				if (postDataSize - curPos >= boundarySize + 2 && pPostData[curPos] == '-' && pPostData[curPos + 1] == '-' &&
					!memcmp(pPostData + curPos + 2, pBoundary, boundarySize))
				{
					size_t len = estrLength(&pValue);

					if (len > 0)
					{
						hrHandleMultipartEntry(req, pValue, len);
						estrClear(&pValue);
					}
					
					curPos += boundarySize + 2;
				}
				else
				{
					estrConcat(&pValue, pPostData + curPos, 1);
				}
			}

			estrDestroy(&pValue);
		}
	}
	else
	{
		urlPopulateList(req->vars, rawPostData, HTTPMETHOD_POST);
	}
}

void hrAppendFilteredQueryString(HttpRequest *req, char **estr, const char *prefix)
{
	int i;
	if(!req || !req->vars)
		return;

	for (i=0; i < eaSize(&req->vars->ppUrlArgList); i++)
	{
		if (strStartsWith(req->vars->ppUrlArgList[i]->arg, prefix))
		{
			estrConcatf(estr, "&%s=%s", req->vars->ppUrlArgList[i]->arg, req->vars->ppUrlArgList[i]->value);
		}
	}
}

void hrBuildQueryString(HttpRequest *req, char **estr)
{
	int i;
	int first = 1;

	if(!req || !req->vars)
		return;

	for(i=0; i<eaSize(&req->vars->ppUrlArgList); i++)
	{
		UrlArgument *arg = req->vars->ppUrlArgList[i];
		
		if(arg->method!=HTTPMETHOD_GET)
			continue;

		if(first)
		{
			first = 0;
			estrPrintf(estr, "%s=%s", arg->arg, arg->value);
		}
		else
		{
			estrConcatf(estr, "&%s=%s", arg->arg, arg->value);
		}
	}
}

void hrApplyQueryStringFilter(HttpRequest *req, const char *prefix)
{
	if(!req || !req->vars)
		return;

	EARRAY_FOREACH_BEGIN(req->vars->ppUrlArgList, i);
	{
		if (strStartsWith(req->vars->ppUrlArgList[i]->arg, prefix))
		{
			StructDestroy(parse_UrlArgument, req->vars->ppUrlArgList[i]);
			eaRemove(&req->vars->ppUrlArgList, i);
		}
	}
	EARRAY_FOREACH_END;
}

bool hrFindBool(HttpRequest *req, const char *arg)
{
	const char *tempVal = hrFindValue(req, arg);
	if(tempVal)
	{
		if(tempVal[0] == '1')
			return true;
	}

	return false;
}

int hrFindInt(HttpRequest *req, const char *arg, int iFailureValue)
{
	const char *tempVal = hrFindValue(req, arg);
	if(tempVal)
		return atoi(tempVal);

	return iFailureValue;
}

S64 hrFindInt64(HttpRequest *req, const char *arg, S64 iFailureValue)
{
	const char *tempVal = hrFindValue(req, arg);
	if(tempVal)
		return atoi64(tempVal);

	return iFailureValue;
}

char **hrFindList(HttpRequest *req, const char *arg)
{
	char **arr = NULL;

	if(!req || !req->vars)
		return NULL;

	FOR_EACH_IN_EARRAY(req->vars->ppUrlArgList, UrlArgument, urlarg)
	{
		if(stricmp(urlarg->arg, arg)==0)
			eaPush(&arr, urlarg->value);
	}
	FOR_EACH_END

	return arr;
}

EARRAY_OF(HttpVars) hrGetAllVariables(HttpRequest *req)
{
	EARRAY_OF(HttpVars) eaVars = NULL;

	if(!req || !req->vars)
		return NULL;

	FOR_EACH_IN_EARRAY(req->vars->ppUrlArgList, UrlArgument, urlarg)
	{
		HttpVars *pVar = StructCreate(parse_HttpVars);

		pVar->variable = strdup(urlarg->arg);
		pVar->value = strdup(urlarg->value);

		eaPush(&eaVars, pVar);
	}
	FOR_EACH_END

	return eaVars;
}

const char * hrFindHeader(HttpRequest *req, const char *headerName)
{
	if(!req || !req->headers)
		return NULL;

	EARRAY_FOREACH_BEGIN(req->headers->ppPairs, i);
	{
		if(!stricmp(req->headers->ppPairs[i]->pName, headerName))
		{
			return req->headers->ppPairs[i]->pValue;
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

const char * hrpFindHeader(HttpResponse *response, const char *headerName)
{
	if(!response || !response->headers)
		return NULL;

	EARRAY_FOREACH_BEGIN(response->headers->ppPairs, i);
	{
		if(!stricmp(response->headers->ppPairs[i]->pName, headerName))
		{
			return response->headers->ppPairs[i]->pValue;
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

const char * hrFindSafeHeader(HttpRequest *req, const char *headerName)
{
	const char *pVal = hrFindHeader(req, headerName);
	if(pVal)
		return pVal;

	return "";
}

const char * hrFindCookie(HttpRequest *req, const char *cookieName)
{
	if(!req || !req->cookies)
		return NULL;

	EARRAY_FOREACH_BEGIN(req->cookies->ppPairs, i);
	{
		if(!stricmp(req->cookies->ppPairs[i]->pName, cookieName))
		{
			return req->cookies->ppPairs[i]->pValue;
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

const char * hrFindSafeCookie(HttpRequest *req, const char *cookieName)
{
	const char *pVal = hrFindCookie(req, cookieName);
	if(pVal)
		return pVal;

	return "";
}

const char * hrFindAuthString(HttpRequest *req)
{
	const char *pAuthHeaderVal = hrFindHeader(req, "Authorization");
	if(pAuthHeaderVal)
	{
		const char *pAuthString = strstr(pAuthHeaderVal, "Basic ");
		if(pAuthString)
		{
			pAuthString += 6; // strlen("Basic ")
			return pAuthString;
		}
	}

	return NULL;
}

const char * hrFindAuthUsername(HttpRequest *req)
{
	const char *pUsername = NULL;
	const char *pAuthString = NULL;

	pAuthString = hrFindAuthString(req);
	pUsername = httpFindAuthenticationUsernameFromAuthString(pAuthString);

	return pUsername;
}

void * hrGetSessionStruct(HttpRequest *req, const char *key, ParseTable *pti)
{
	return httpGetSessionStruct(req->link, hrFindAuthString(req), key, pti);
}

void hrEnableXMLRPC(bool enable)
{
	g_xmlrpc_enabled = enable;
}

static void hrPostHandler(NetLink *link, HttpClientStateDefault *pClientState)
{
	HttpRequest *req = hrParse(pClientState->pPostHeaderString);
	if(!req)
	{
		// Some kind of bad request
		linkShutdown(&link);
		return;
	}
	req->link = link;
	req->legacy = callocStruct(HttpLegacySupport);
	req->legacy->data = NULL;
	req->legacy->pClientState = pClientState;

	if(g_xmlrpc_enabled && stricmp(req->path, "/xmlrpc")==0)
	{
		httpProcessXMLRPC(link, pClientState, pClientState->pPostDataString, HAR_SUCCESS);
	}
	else
	{
		hrParsePostData(req, pClientState->pPostDataString);
		if(defaultRequestHandler)
			defaultRequestHandler(req, defaultRequestHandlerUserData);
	}

	free(req->legacy);
	StructDestroy(parse_HttpRequest, req);
}

static void hrGetHandler(char *data, NetLink *link, HttpClientStateDefault *pClientState)
{
	HttpRequest *req = hrParse(data);
	if(!req)
	{
		// Some kind of bad request
		linkShutdown(&link);
		return;
	}
	req->link = link;
	req->legacy = callocStruct(HttpLegacySupport);
	req->legacy->data = data;
	req->legacy->pClientState = pClientState;

	if(defaultRequestHandler)
		defaultRequestHandler(req, defaultRequestHandlerUserData);

	free(req->legacy);
	StructDestroy(parse_HttpRequest, req);
}

void hrSetHandler(HttpRequestHandler fn, void *userdata)
{
	defaultRequestHandler = fn;
	defaultRequestHandlerUserData = userdata;
	setHttpDefaultGetHandler(hrGetHandler);
	setHttpDefaultPostHandler(hrPostHandler);
}

NetListen *hrListen(NetComm *comm, int port, HttpRequestHandler fn, void *userdata)
{
	hrSetHandler(fn, userdata);
	return commListen(comm, LINKTYPE_TOUNTRUSTED_1MEG, LINK_HTTP, port, httpDefaultMsgHandler, httpDefaultConnectHandler, httpDefaultDisconnectHandler, sizeof(HttpClientStateDefault));
}

void hrCallLegacyHandler(HttpRequest *req, HttpPostHandler httpPostHandler, HttpGetHandler httpGetHandler)
{
	if (req && req->legacy)
	{
		switch (req->method)
		{
		xcase HTTPMETHOD_GET:
			if (httpGetHandler)
			{
				httpGetHandler(req->legacy->data, req->link, req->legacy->pClientState);
			}
			
		xcase HTTPMETHOD_POST:
			if (httpPostHandler)
			{
				httpPostHandler(req->link, req->legacy->pClientState);
			}
		}
	}
}

// Get the HttpLib per-link user data pointer associated with a request.
void **hrGetLinkUserDataPtr(HttpRequest *req)
{
	return &req->legacy->pClientState->pLinkUserData;
}

#include "autogen/httputil_h_ast.c"
#include "AutoGen/httputil_c_ast.c"
