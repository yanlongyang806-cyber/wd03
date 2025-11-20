#pragma once
GCC_SYSTEM

typedef struct ResourceSearchRequest ResourceSearchRequest;
typedef struct ResourceSearchResult ResourceSearchResult;

// Request a certain search
void RequestResourceSearch(ResourceSearchRequest *pRequest);

// Display the result in the search window
void SendSearchResourcesResult(ResourceSearchResult *pResult);

// Display/hide the search window
void ShowSearchWindow(bool bShow);

// Returns if the search window is visible
bool CheckSearchWindow(void);

// Returns reference to searc window state
bool *GetSearchWindowStatus(void);

// Simple searches
void RequestListAll(const char *pDictName);
void RequestUsageSearch(const char *pDictName, const char *pResourceName);
void RequestReferencesSearch(const char *pDictName, const char *pResourceName);
void RequestTagSearch(const char *pDictName, const char *pTagString);
void RequestExpressionSearch(const char *pDictName, const char *pTagString);
void RequestDisplayNameSearch(const char *pDictName, const char *pValueString);
void RequestFieldSearch(const char *pDictName, const char *pFieldString, const char *pValueString);