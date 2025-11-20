#pragma once

// if xmlrpcAuthProductName is NULL, XMLRPC will be completely unrestricted
// if commandCategoryFilter is NULL, all autocommands are available (with appropriate access level)

#include "HttpServing.h"

extern const char * DEFAULT_HTTP_CATEGORY_FILTER;

typedef enum GenericHttpServingFlags
{
	GHSFLAG_AUTOUPDATE_ALWAYS_ON = 1,
} GenericHttpServingFlags;

bool GenericHttpServing_Begin(int iPortNum, const char *authProductName, const char *commandCategoryFilter, GenericHttpServingFlags eFlags);
bool GenericHttpServing_BeginCBs(	int iPortNum, 
									const char *authProductName, 
									const char *commandCategoryFilter, 
									HttpServingXPathCallback XPathCB,
									HttpServingCommandCallback CmdCB,
									HttpServingJpegCallback jpgCB,
									GenericHttpServingFlags eFlags);
void GenericHttpServing_Tick(void);

void GenericHttpServer_Activity(void);
U32 GenericHttpServer_GetLastActivityTime(void);

//KELVIN SAYS NEVER TO USE THIS.
void GenericHttpServing_SetPrefixAndSuffixStrings(char *pPrefix, char *pSuffix);

char *getGenericServingStaticDir(void);
