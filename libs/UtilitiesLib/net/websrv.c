#include "websrv.h"
#include "earray.h"
#include "estring.h"
#include "StringUtil.h"
#include "textparser.h"

#include "AutoGen/websrv_h_ast.h"

void websrvKVList_Add(WebSrvKeyValueList *list, const char *key, const char *value)
{
	WebSrvKeyValue *kv;	
	if (nullStr(key) || !list)
		return;
	kv = eaIndexedGetUsingString(&list->kvList, key);
	if (!kv)
	{
		kv = StructCreate(parse_WebSrvKeyValue);
		kv->key = StructAllocString(key);
		if (!list->kvList)
			eaIndexedEnable(&list->kvList, parse_WebSrvKeyValue);
		eaIndexedAdd(&list->kvList, kv);
	}
	else
		SAFE_FREE(kv->value);
	kv->value = StructAllocString(value);
}

void websrvKVList_Addf(WebSrvKeyValueList *list, const char *key, const char *fmt, ...)
{
	char *value = NULL;
	va_list ap;
	va_start(ap, fmt);
	estrConcatfv(&value, fmt, ap);
	va_end(ap);
	websrvKVList_Add(list, key, value);
	estrDestroy(&value);
}

#include "AutoGen/websrv_h_ast.c"