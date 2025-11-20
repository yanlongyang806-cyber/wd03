#pragma once
GCC_SYSTEM

#include "url.h"
#include "HttpLib.h"
#include "AutoGen/httputil_h_ast.h"

// HTTP Header Parsing
typedef struct NameValuePairList NameValuePairList;
typedef struct UrlArgumentList UrlArgumentList;
typedef struct NetLink NetLink;
typedef struct NetComm NetComm;
typedef struct NetListen NetListen;
typedef struct HttpLegacySupport HttpLegacySupport;

AUTO_STRUCT;
typedef struct HttpRequest
{
	HttpMethod method;
	unsigned major_version;		AST(INT)
	unsigned minor_version;		AST(INT)
	char *path;
	NameValuePairList *headers;
	UrlArgumentList   *vars;		// Currently merging querystring+postdata, could be split if we care
	NameValuePairList *cookies;		AST(NAME(Cookies))
	NetLink *link;					AST(UNOWNED LATEBIND)
	HttpLegacySupport *legacy;		AST(UNOWNED)
} HttpRequest;

AUTO_ENUM;
typedef enum HttpResponseCode
{
	HTTP_OK = 200,
	HTTP_CREATED = 201,
	HTTP_ACCEPTED = 202,
	HTTP_NON_AUTHORITATIVE_INFORMATION = 203,
	HTTP_NO_RESPONSE = 204,
	HTTP_RESET_CONTENT = 205,
	HTTP_PARTIAL_CONTENT = 206,
	HTTP_MULTIPLE_CHOICES = 300,
	HTTP_MOVED_PERMANENTLY = 301,
	HTTP_FOUND = 302,
	HTTP_SEE_OTHER = 303,
	HTTP_NOT_MODIFIED = 304,
	HTTP_USE_PROXY = 305,
	HTTP_TEMPORARY_REDIRECT = 307,
	HTTP_BAD_REQUEST = 400,
	HTTP_UNAUTHORIZED = 401,
	HTTP_PAYMENT_REQUIRED = 402,
	HTTP_FORBIDDEN = 403,
	HTTP_NOT_FOUND = 404,
	HTTP_METHOD_NOT_ALLOWED = 405,
	HTTP_NOT_ACCEPTABLE = 406,
	HTTP_PROXY_AUTHENTICATION_REQUIRED = 407,
	HTTP_REQUEST_TIMEOUT = 408,
	HTTP_CONFLICT = 409,
	HTTP_GONE = 410,
	HTTP_LENGTH_REQUIRED = 411,
	HTTP_PRECONDITION_FAILED = 412,
	HTTP_REQUEST_ENTITY_TOO_LARGE = 413,
	HTTP_REQUEST_URI_TOO_LARGE = 414,
	HTTP_UNSUPPORTED_MEDIA_TYPE = 415,
	HTTP_REQUESTED_RANGE_NOT_SATISFIABLE = 416,
	HTTP_EXPECTATION_FAILED = 417,
	HTTP_INTERNAL_ERROR = 500,
	HTTP_NOT_IMPLEMENTED = 501,
	HTTP_BAD_GATEWAY = 502,
	HTTP_SERVICE_UNAVAILABLE = 503,
	HTTP_GATEWAY_TIMEOUT = 504,
	HTTP_VERSION_NOT_SUPPORTED = 505,
} HttpResponseCode;

AUTO_STRUCT;
typedef struct HttpResponse
{
	unsigned major_version;			AST(INT)
	unsigned minor_version;		AST(INT)
	HttpResponseCode code;
	char reason[256];
	NameValuePairList *headers;
	NetLink *link;					AST(UNOWNED LATEBIND)
} HttpResponse;

AUTO_STRUCT;
typedef struct HttpVars
{
	char *variable;
	char *value;
} HttpVars;

HttpRequest * hrParse(const char *rawHttpRequest);
HttpResponse * hrParseResponse(const char *rawHttpResponse);
void hrParsePostData(HttpRequest *req, const char *rawPostData);

void hrAppendFilteredQueryString(HttpRequest *req, char **estr, const char *prefix);
void hrBuildQueryString(HttpRequest *req, char **estr);
void hrApplyQueryStringFilter(HttpRequest *req, const char *prefix); // Removes vars that don't have a certain prefix

#define hrFindValue(req, arg) (SAFE_MEMBER((req), vars) ? urlFindValue((req)->vars, (arg)) : NULL)
#define hrFindSafeValue(req, arg) (SAFE_MEMBER((req), vars) ? urlFindSafeValue((req)->vars, (arg)) : "")
#define hrFindBoundedInt(req, name, out, min, max) (SAFE_MEMBER((req), vars) ? urlFindBoundedInt((req)->vars, (name), (out), (min), (max)) : -1)
#define hrAddValue(req, arg, val) (SAFE_MEMBER((req), vars) && urlAddValue((req)->vars, (arg), (val)))
#define hrRemoveValue(req, arg) (SAFE_MEMBER((req), vars) && urlRemoveValue((req)->vars, (arg)))
#define hrCountVars(req) (SAFE_MEMBER((req), vars) ? eaSize((req)->vars->ppUrlArgList) : 0)
bool hrFindBool(HttpRequest *req, const char *arg);
int hrFindInt(HttpRequest *req, const char *arg, int iFailureValue);
S64 hrFindInt64(HttpRequest *req, const char *arg, S64 iFailureValue);
char **hrFindList(HttpRequest *req, const char *arg);
EARRAY_OF(HttpVars) hrGetAllVariables(HttpRequest *req);

const char * hrFindSafeHeader(HttpRequest *req, const char *headerName);
const char * hrFindHeader(HttpRequest *req, const char *headerName);
const char * hrpFindHeader(HttpResponse *response, const char *headerName);
const char * hrFindSafeCookie(HttpRequest *req, const char *cookieName);
const char * hrFindCookie(HttpRequest *req, const char *cookieName);
const char * hrFindAuthString(HttpRequest *req);
const char * hrFindAuthUsername(HttpRequest *req);
void *       hrGetSessionStruct(HttpRequest *req, const char *key, ParseTable *pti);

typedef void (*HttpRequestHandler)(HttpRequest *req, void *userdata);
void hrSetHandler(HttpRequestHandler fn, void *userdata);
void hrEnableXMLRPC(bool enable);
NetListen *hrListen(NetComm *comm, int port, HttpRequestHandler fn, void *userdata);

void hrCallLegacyHandler(HttpRequest *req, HttpPostHandler httpPostHandler, HttpGetHandler httpGetHandler);

// Get the HttpLib per-link user data pointer associated with a request.
void **hrGetLinkUserDataPtr(HttpRequest *req);
