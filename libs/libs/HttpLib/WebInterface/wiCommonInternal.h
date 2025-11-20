#pragma once

typedef struct HttpVars HttpVars;
typedef struct HttpRequest HttpRequest;
typedef enum ClearSilverAccessLevel ClearSilverAccessLevel;

AUTO_STRUCT;
typedef struct WICWebRequest
{
	HttpRequest *pReq;		NO_AST
	char **pOutput;			NO_AST
	int iAccessLevel; AST(NAME(NumericAccessLevel)) // Numeric access level
	WIAccessLevel eAccessLevel; // Corresponding permissions level
	char *pRawFileName; AST(ESTRING)
	WIResult eResult;
	bool bDumpHDF : 1;
} WICWebRequest;

AUTO_STRUCT;
typedef struct WICommonWebSite
{
	const char *pVersion;			AST(UNOWNED)
	const char *pBuildVersion;		AST(UNOWNED)
	char *pInstance;				AST(ESTRING)
	const char *pAccess;			AST(UNOWNED)
	char *pContent;
	const char *pExtension;			AST(UNOWNED)
} WICommonWebSite;

AUTO_STRUCT;
typedef struct WICWebRequestData
{
	EARRAY_OF(HttpVars) eaVars;
} WICWebRequestData;
