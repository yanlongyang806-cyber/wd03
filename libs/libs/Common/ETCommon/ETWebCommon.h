#pragma once

typedef struct NetLink NetLink;
typedef struct CookieList CookieList;
typedef struct UrlArgumentList UrlArgumentList;
typedef struct HttpClientStateDefault HttpClientStateDefault;
typedef struct ErrorEntry ErrorEntry;
typedef struct TriviaData TriviaData;
typedef struct TriviaOverviewItem TriviaOverviewItem;

const char *getJiraHost(void);
int getJiraPort(void);

void ETWeb_InitWebRootDirs(void);
void ETWeb_SetSourceDir(const char *dirpath);
void ETWeb_SetDataDir(const char *dirpath);
const char *ETWeb_GetSourceDir(void);
const char *ETWeb_GetDataDir(void);

typedef bool (*ETWebDefaultGetHandlerFunc)(NetLink *link, char **args, char **values, int count);
typedef bool (*ETWebDefaultPostHandlerFunc)(NetLink *link, UrlArgumentList *args);
typedef void (*ETWebGetHandler) (NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies);
typedef void (*ETWebPostHandler) (NetLink *link, const char *url, UrlArgumentList *args, const char *pReferer, CookieList *pCookies);

void ETWeb_SetDefaultPage (const char *page);
void ETWeb_SetDefaultGetHandler(ETWebDefaultGetHandlerFunc func);
void ETWeb_SetDefaultPostHandler(ETWebDefaultPostHandlerFunc func);
// Include the '/' before the URL!
void ETWeb_RegisterGetHandler(const char *url, ETWebGetHandler func);
void ETWeb_RegisterPostHandler(const char *url, ETWebPostHandler func);

/*bool ETWeb_RunGetHandler(const char *url, NetLink *link, char **args, char **values, int count, 
						 const char *pReferer, CookieList *pCookies);
bool ETWeb_RunPostHandler(const char *url, NetLink *link, UrlArgumentList *args, const char *pReferer, 
						  CookieList *pCookies, bool setCookies);*/

void ETWeb_httpHandlePost(NetLink *link, HttpClientStateDefault *pClientState);
void ETWeb_httpHandleGet(char *data, NetLink *link, HttpClientStateDefault *pClientState);

void wiSetFlags(int iFlags);
int wiGetFlags(void);
void ETWeb_Init(void);
void shutdownWebInterface(void);
void updateWebInterface(void);

void ETWeb_SetDefaultTitle(const char *title);
void ETWeb_EnableDynamicHTMLTitles(bool bEnable);
void constructHtmlHeaderEnd(char **estr, bool isLoggedIn, CookieList *pList, const char *pRedirect);
void httpSendWrappedString(NetLink *link, const char *pBody, const char *pTitle, CookieList *pList);
void httpSendWrappedStringPlusCookies(NetLink *link, const char *pBody, const char *pTitle, CookieList *pList, const char *pRedirect);
void httpSendWrappedStringClearCookies(NetLink *link, const char *pBody, const char *pTitle, CookieList *pList, CookieList *pToRemove, const char *pRedirect);

void appendVersionLink(char **estr, const char *version);
void appendCallStackToString(ErrorEntry *p, char **estr);
void appendTriviaStrings(char **estr, CONST_EARRAY_OF(TriviaData) ppTriviaData);
void appendTriviaOverview (char **estr, CONST_EARRAY_OF(TriviaOverviewItem) ppTriviaItems);

void ETWeb_DumpDataToString(ErrorEntry *p, char **estr, U32 iFlags);
void ET_DumpEntryToString(char **estr, ErrorEntry *pEntry, U32 iFlags, bool returnFullEntry);


LATELINK;
char *httpGetAppCustomHeaderString(void);