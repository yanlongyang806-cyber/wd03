#include "trivia.h"
#include "estring.h"
#include "timing.h"
#include "sysutil.h"
#include "windefinclude.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#include "trivia_h_ast.h"
#include "trivia_h_ast.c"

extern CRITICAL_SECTION gTriviaAccess; // from errornet.c

// this format must never change, to maintain forward and backward compatibility for the patchclient
ParseTable parse_oneline_TriviaData[] =
{
	{ "Key",	TOK_STRUCTPARAM | TOK_ESTRING | TOK_STRING(TriviaData, pKey, 0)				},
	{ "Val",	TOK_STRUCTPARAM | TOK_ESTRING | TOK_STRING(TriviaData, pVal, 0)	},
	{ "\n",		TOK_END															},
	{ "",		0																}
};

ParseTable parse_file_TriviaList[] =
{
	{ "Trivia",	TOK_STRUCT(TriviaList, triviaDatas, parse_oneline_TriviaData)	},
	{ "",		0																}
};

AUTO_RUN;
void initTriviaTPIs(void)
{
	ParserSetTableInfoRecurse(parse_file_TriviaList, sizeof(TriviaList), "TriviaList", NULL, __FILE__, NULL, SETTABLEINFO_NAME_STATIC | SETTABLEINFO_ALLOW_CRC_CACHING);
};


char *g_errorTriviaString = NULL;
TriviaData **g_ppTrivia = NULL;
//NOCONST(TriviaData) *** g_pppTrivia = &g_ppTrivia;

TriviaData **triviaGlobalGet(void)
{
	EnterCriticalSection(&gTriviaAccess);
	return g_ppTrivia;
}

void triviaGlobalRelease(void)
{
	LeaveCriticalSection(&gTriviaAccess);
}

// Returns all current global trivia
AUTO_COMMAND ACMD_CATEGORY(Debug);
const char *printTrivia(void)
{
	return g_errorTriviaString;
}

static TriviaData * findTrivia(CONST_EARRAY_OF(TriviaData) ppTrivia, const char *key)
{
	if(!key)
		return NULL;

	EnterCriticalSection(&gTriviaAccess);

	FOR_EACH_IN_EARRAY(ppTrivia, TriviaData, p)
	{
		if(p->pKey && !stricmp(key, p->pKey))
		{
			LeaveCriticalSection(&gTriviaAccess);
			return p;
		}
	}
	FOR_EACH_END;

	LeaveCriticalSection(&gTriviaAccess);
	return NULL;
}

static void updateTrivia(void)
{
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&gTriviaAccess);
	
	// change to write out as TextParser string ?
	estrClear(&g_errorTriviaString);

	FOR_EACH_IN_EARRAY(g_ppTrivia, TriviaData, p)
	{
		estrConcatf(&g_errorTriviaString, "%s ", p->pKey);
		estrAppendEscaped(&g_errorTriviaString, p->pVal);
		estrConcatf(&g_errorTriviaString, "\n");
	}
	FOR_EACH_END;
	
	LeaveCriticalSection(&gTriviaAccess);
	PERFINFO_AUTO_STOP();
}

bool triviaRemoveEntry(const char *key)
{
	int i;
	EnterCriticalSection(&gTriviaAccess);
	for (i=0; i<eaSize(&g_ppTrivia); i++)
	{
		TriviaData *p = g_ppTrivia[i];
		if(!stricmp(key, p->pKey))
		{
			StructDestroy(parse_TriviaData,p);
			eaRemove(&g_ppTrivia, i);
			updateTrivia();
			LeaveCriticalSection(&gTriviaAccess);
			return true;
		}
	}
	LeaveCriticalSection(&gTriviaAccess);
	return false;
}

static bool triviaPrintfv(TriviaData ***pppTrivia, const char *key_unFixedUp, const char *format, va_list args)
{
	char temp[1024];
	NOCONST(TriviaData) *p;
	bool ret;
	char *pKeyFixedUp = NULL;

	EnterCriticalSection(&gTriviaAccess);

	PERFINFO_AUTO_START_FUNC();
	
	estrStackCreate(&pKeyFixedUp);
	estrCopy2(&pKeyFixedUp, key_unFixedUp);
	strchrReplace(pKeyFixedUp, ' ', '_');

	p = CONTAINER_NOCONST(TriviaData, findTrivia(*pppTrivia, pKeyFixedUp));
	if(!p)
	{
		p = StructAllocNoConst(parse_TriviaData);
		estrCopy2(&p->pKey, pKeyFixedUp);
		eaPush(pppTrivia, (TriviaData*)p);
	}

	estrDestroy(&pKeyFixedUp);


	if (p->pVal && (vsnprintf(temp, _TRUNCATE, format, args) != -1))
	{
		// Fit into temp buffer
		if (strcmp(temp, p->pVal)==0)
		{
			ret = false; // Nothing changed
		} else {
			ret = true;
			estrCopy2(&p->pVal, temp);
		}
	} else {
		// Didn't fit
		estrClear(&p->pVal);
		estrConcatfv(&p->pVal, format, args);
		ret = true;
	}
	
	PERFINFO_AUTO_STOP();
	
	LeaveCriticalSection(&gTriviaAccess);
	return ret;
}

bool triviaPrintf_dbg(const char *key, const char *format, ...)
{
	int ret;

	EnterCriticalSection(&gTriviaAccess);

	VA_START(args, format);
	ret = triviaPrintfv(&g_ppTrivia, key, format, args);
	VA_END();

	if (ret)
		updateTrivia();

	LeaveCriticalSection(&gTriviaAccess);
	return ret;
}

void triviaPrintStruct(const char *prefix, ParseTable *table, const void *structptr)
{
	int i;
	EnterCriticalSection(&gTriviaAccess);
	FORALL_PARSETABLE(table, i)
	{
		char keybuf[1024];
		char buf[1024];
		ParseTable *field = table + i;
		StructTypeField type = TOK_GET_TYPE(field->type);
		if (field->type & (TOK_REDUNDANTNAME) ||
			TOK_GET_TYPE(field->type) == TOK_START ||
			TOK_GET_TYPE(field->type) == TOK_END ||
			TOK_GET_TYPE(field->type) == TOK_IGNORE)
			continue;
		TokenToSimpleString(table, i, structptr, SAFESTR(buf), 0);
		sprintf(keybuf, "%s%s", prefix, field->name);
		triviaPrintf(keybuf, "%s", buf);
	}
	LeaveCriticalSection(&gTriviaAccess);
}

void triviaPrintFromFile(const char *prefix, const char *fname)
{
	NOCONST(TriviaList) list = {0};

	EnterCriticalSection(&gTriviaAccess);

	if(!prefix)
		prefix = "";

	if( ParserReadTextFile(fname, parse_file_TriviaList, &list, 0) &&
		list.triviaDatas )
	{
		int i;
		for(i = eaSize(&list.triviaDatas)-1; i >= 0; --i)
		{
			if(list.triviaDatas[i] && list.triviaDatas[i]->pKey && list.triviaDatas[i]->pVal)
			{
				NOCONST(TriviaData) *pout = CONTAINER_NOCONST(TriviaData, findTrivia(g_ppTrivia, list.triviaDatas[i]->pKey));
				if(pout)
				{
					NOCONST(TriviaData) *pTemp = list.triviaDatas[i];
					estrDestroy(&pout->pVal);
					pout->pVal = pTemp->pVal;
					pTemp->pVal = NULL;
				}
				else
				{
					eaPush(&g_ppTrivia, (TriviaData*) list.triviaDatas[i]);
					list.triviaDatas[i] = NULL;
				}
			}
		}
	}
	StructDeInitNoConst(parse_TriviaList, &list);
	updateTrivia();
	LeaveCriticalSection(&gTriviaAccess);
}

const char* triviaGetValue(const char *key)
{
	TriviaData *p = findTrivia(g_ppTrivia, key);
	return p ? p->pVal : NULL;
}

TriviaList* triviaListCreate(void)
{
	return StructAlloc(parse_TriviaList);
}

TriviaList* triviaListCreateFromFile(const char *fname)
{
	if(fname)
	{
		TriviaList *list = StructAlloc(parse_TriviaList);
		if(ParserReadTextFile(fname, parse_file_TriviaList, list, 0))
			return list;
		StructDestroy(parse_TriviaList, list);
	}
	return NULL;
}

const char* triviaListGetValue(TriviaList *list, const char *key)
{
	TriviaData *p = findTrivia(list->triviaDatas, key);
	return p ? p->pVal : NULL;
}

TriviaData * triviaListFindEntry(TriviaList *list, const char *key)
{
	return findTrivia(list->triviaDatas, key);
}

TriviaData * triviaListRemoveEntry(TriviaList *list, const char *key)
{
	int i, size;
	if(!key || !list)
		return NULL;
	size = eaSize(&list->triviaDatas);

	for (i=0; i<size; i++)
	{
		TriviaData *p = list->triviaDatas[i];
		if(p->pKey && !stricmp(key, p->pKey))
		{
			return (TriviaData*) eaRemove(&CONTAINER_NOCONST(TriviaList, list)->triviaDatas, i);
		}
	}
	return NULL;
}

void triviaListDestroy(TriviaList **pList)
{
	StructDestroySafe(parse_TriviaList, pList);
	ANALYSIS_ASSUME(*pList == NULL);
}


#undef triviaListPrintf
bool triviaListPrintf(TriviaList *list, const char *key, const char *format, ...)
{
	bool ret;

	if(!list)
		return 0;

	EnterCriticalSection(&gTriviaAccess);

	VA_START(args, format);
	ret = triviaPrintfv((TriviaData***) &list->triviaDatas, key, format, args);
	VA_END();

	LeaveCriticalSection(&gTriviaAccess);
	return ret;
}

void triviaListWriteToFile(TriviaList *list, const char *fname)
{
	if(list && fname)
		ParserWriteTextFile(fname, parse_file_TriviaList, list, 0, 0);
}

void triviaListClear(TriviaList *pList)
{
	StructDeInit(parse_TriviaList, pList);
}

TriviaMutex triviaAcquireDumbMutex(const char *name)
{
	TriviaMutex ret;
	const char *in;
    #if PLATFORM_CONSOLE
		char mutex_name[MAX_PATH] = "";
	#else
		char mutex_name[MAX_PATH] = "Global\\";
	#endif
	size_t i = strlen(mutex_name);
	for(in = name; *in != '\0' && i < ARRAY_SIZE(mutex_name) - 1; ++in, ++i)
		mutex_name[i] = *in == '\\' ? '_' : tolower(*in);
	mutex_name[i] = '\0';
	ret.mutex = CreateMutex_UTF8(NULL, FALSE, mutex_name);
	assertmsg(ret.mutex, "Can't acquire mutex, does another session own it?");
	
	{
		DWORD result;
		WaitForSingleObjectWithReturn(ret.mutex, INFINITE, result);
		
		if(	result != WAIT_OBJECT_0 &&
			result != WAIT_ABANDONED)
		{
			// This is impossible.
			assert(0);
		}
	}
	
	return ret;
}

void triviaReleaseDumbMutex(TriviaMutex mutex)
{
	bool success;
	success = ReleaseMutex(mutex.mutex);
	if (!success)
		WinErrorf(GetLastError(), "triviaReleaseDumbMutex() failed: ReleaseMutex()");
    success = CloseHandle(mutex.mutex);
	if (!success)
		WinErrorf(GetLastError(), "triviaReleaseDumbMutex() failed: CloseHandle()");
}

__forceinline static NOCONST(TriviaOverviewItem) *triviaOverviewFindKey(NOCONST(TriviaOverview) *overview, const char *key)
{
	int i, size;
	if (!key || !overview)
		return NULL;
	size = eaSize(&overview->ppTriviaItems);
	for (i=0; i<size; i++)
	{
		if (stricmp(overview->ppTriviaItems[i]->pKey, key) == 0)
			return overview->ppTriviaItems[i];
	}
	return NULL;
}

__forceinline static NOCONST(TriviaOverviewValue) *triviaOverviewFindValue(NOCONST(TriviaOverviewItem) *item, const char *value)
{
	int i, size;
	if (!value || !item)
		return NULL;
	size = eaSize(&item->ppValues);
	for (i=0; i<size; i++)
	{
		if (stricmp(item->ppValues[i]->pVal, value) == 0)
			return item->ppValues[i];
	}
	return NULL;
}

AUTO_TRANS_HELPER ATR_LOCKS(overview, ".*");
void triviaOverviewAddValue(ATH_ARG SA_PARAM_NN_VALID NOCONST(TriviaOverview) *overview, const char *key, const char *value)
{
	NOCONST(TriviaOverviewItem) *item = triviaOverviewFindKey(overview, key);
	NOCONST(TriviaOverviewValue) *triviaValue;
	if (item)
	{
		triviaValue = triviaOverviewFindValue(item, value);
		if (triviaValue)
			triviaValue->uCount++;
		else
		{
			triviaValue = StructCreateNoConst(parse_TriviaOverviewValue);
			estrCopy2(&triviaValue->pVal, value);
			triviaValue->uCount = 1;
			eaPush(&item->ppValues, triviaValue);
		}
	}
	else
	{
		triviaValue = StructCreateNoConst(parse_TriviaOverviewValue);
		item = StructCreateNoConst(parse_TriviaOverviewItem);
		estrCopy2(&item->pKey, key);
		estrCopy2(&triviaValue->pVal, value);
		triviaValue->uCount = 1;
		eaPush(&item->ppValues, triviaValue);
		eaPush(&overview->ppTriviaItems, item);
	}
}

AUTO_TRANS_HELPER ATR_LOCKS(overview, ".*");
void triviaMergeOverview (ATH_ARG SA_PARAM_NN_VALID NOCONST(TriviaOverview) *overview, NON_CONTAINER CONST_EARRAY_OF(TriviaData) src, bool bKeepAllTrivia)
{
	int i, size;
	size = eaSize(&src);
	for (i=0; i<size; i++)
	{
		// Even if we don't want to keep all of the trivia, we still want to keep all copies of "details",
		// because it comes from ErrorDetailsf(), which is important data. Any additional "super important"
		// trivia keys can be checked for in the following if-statement.

		if (src[i]->pKey &&  src[i]->pVal && (bKeepAllTrivia || (!stricmp(src[i]->pKey, "details"))))
		{
			triviaOverviewAddValue(overview, src[i]->pKey, src[i]->pVal);
		}
	}
}
