#pragma once

#include "net.h"
#include "Search.h"
typedef struct CookieList CookieList;

void initWebReports();

void expireOldReports();

const char * startReport(SearchData *pSearchData, U32 uIP, void *userData);
const char * startTriviaReport(int iUniqueID, U32 uIP);
void wiReport(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies);
void wiTriviaReport(NetLink *link, char **args, char **values, int count, const char *pReferer, CookieList *pCookies);
void wiDisplayCSV(NetLink *link, SearchData *search, ErrorEntry **entries);
void wiDisplayCopyPaste(NetLink *link, SearchData *search, ErrorEntry **entries);
