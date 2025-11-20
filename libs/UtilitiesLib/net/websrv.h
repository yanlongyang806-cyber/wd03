#pragma once

AUTO_STRUCT;
typedef struct WebSrvKeyValue
{
	char *key; AST(KEY)
	char *value;
} WebSrvKeyValue;

AUTO_STRUCT;
typedef struct WebSrvKeyValueList
{
	EARRAY_OF(WebSrvKeyValue) kvList;
} WebSrvKeyValueList;

void websrvKVList_Add(WebSrvKeyValueList *list, const char *key, const char *value);
void websrvKVList_Addf(WebSrvKeyValueList *list, const char *key, const char *fmt, ...);