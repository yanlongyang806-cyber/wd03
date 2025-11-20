//stuff that supports listing and executing commands via the http server monitor

#pragma once
GCC_SYSTEM

int GetNumCommandCategories(void);
const char *GetNthCommandCategoryName(int n);

typedef struct UrlArgumentList UrlArgumentList;
bool ProcessCommandCategoryIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pUrl, int iAccessLevelOfViewer, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags);
