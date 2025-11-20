#pragma once
GCC_SYSTEM

// URL manipulation

AUTO_ENUM;
typedef enum HttpMethod
{
	HTTPMETHOD_UNKNOWN = 0,
	HTTPMETHOD_GET,
	HTTPMETHOD_POST,
	HTTPMETHOD_MULTIPART, // Used to tag that an argument should become multipart form data
	HTTPMETHOD_HEADER, // Used to tag an argument that is actually an HTTP header
	HTTPMETHOD_JSON,
} HttpMethod;

#define MIMETYPE_FORM_ENCODED ("application/x-www-form-urlencoded")
#define MIMETYPE_JSON ("application/json")
#define MIMETYPE_TEXT ("text/plain")

char *urlEscape(const char *url, char **url_esc, bool use_plus_for_space, bool allow_slash);
char *urlUnescape(const char *url_esc,char *url,int url_len);
bool urlGetNextArg(char **url, char **arg, char **value);  // Warning: modifies url!
int urlToArgs(char *url,char **args,char **values, int num_elements);
char *urlFind(char *data,char *cmd);

// More complicated URL argument parsing, allows for extremely large amounts of POSTDATA

AUTO_STRUCT;
typedef struct UrlArgument
{
	char *arg;
	char *value;
	const char *value_ext; AST(UNOWNED)
	char *filename;
	char *content_type;
	U32 length; AST(NAME("Length"))

	HttpMethod method;
} UrlArgument;
extern ParseTable parse_UrlArgument[];
#define TYPE_parse_UrlArgument UrlArgument

//for many server-monitoring purposes, there will be a fake URL arg stuck into this list named
//__AUTH which will contain the pAuthNameAndIP string. Make sure you're in a context where that
//will have been added before you depend on it
AUTO_STRUCT;
typedef struct UrlArgumentList
{
	char *pMimeType; AST(ESTRING)
	char *pBaseURL; AST(ESTRING)
	UrlArgument **ppUrlArgList;
} UrlArgumentList;
extern ParseTable parse_UrlArgumentList[];
#define TYPE_parse_UrlArgumentList UrlArgumentList

UrlArgumentList * urlToUrlArgumentList_Internal(const char *url, bool bNoBaseURL);
#define urlToUrlArgumentList(url) urlToUrlArgumentList_Internal(url, false)
void urlPopulateList(UrlArgumentList *list, const char *querystring, HttpMethod method);
void urlAppendQueryStringWithOverrides(UrlArgumentList *list, char **estr, int keyvalpairs, ...);
const char * urlFindValue(UrlArgumentList *list, const char *arg); // Returns NULL on failure
const char * urlFindSafeValue(UrlArgumentList *list, const char *arg); // Returns "" on failure
void urlRemoveValue(UrlArgumentList *list, const char *arg);
UrlArgument *urlAddValue(UrlArgumentList *list, const char *arg, const char *val, HttpMethod method);
UrlArgument * urlAddValuef(UrlArgumentList *list, const char *arg, HttpMethod method, FORMAT_STR const char * fmt, ...);
UrlArgument *urlAddValueExt(UrlArgumentList *list, const char *arg, const char *val, HttpMethod method);

//returns 1 on found, 0 on not found, -1 on error
int urlFindBoundedInt(UrlArgumentList *list, char *pName, int *pOut, int iMin, int iMax);
#define urlFindInt(list, pName, pOut) urlFindBoundedInt(list, pName, pOut, INT_MIN, INT_MAX)

int urlFindBoundedUInt(UrlArgumentList *list, char *pName, U32 *pOut, U32, U32 iMax);
#define urlFindUInt(list, pName, pOut) urlFindBoundedUInt(list, pName, pOut, 0, UINT_MAX)

//returns 0 if the argument isn't there, isn't an int, or is 0
static int __forceinline urlFindNonZeroInt(UrlArgumentList *list, char *pName)
{
	int iRetVal;
	if (urlFindInt(list, pName, &iRetVal))
	{
		return iRetVal;
	}

	return 0;
}

//tries to read in a vec3 like "5.5,3.3,100.0". Returns 1 on success, 0 on not found, -1 on bad formatting
int urlFindVec3(UrlArgumentList *list, char *pName, Vec3 outVec);

void urlDestroy(UrlArgumentList **list);
void urlArgDestroy(UrlArgument **arg);

int urlCmpArgs(const UrlArgument **left, const UrlArgument **right);

// Bake the URL arguments into an HTTP request string;
// Host and Path need to be pulled out prior to calling this, possibly from args->pBaseUrl
void urlCreateHTTPRequest(char **estrRequest, const char *agent, const char *host, const char *path, UrlArgumentList *args);