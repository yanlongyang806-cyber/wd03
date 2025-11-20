#ifndef TRIVIA_H
#define TRIVIA_H
#pragma once
GCC_SYSTEM

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct TriviaData
{
	CONST_STRING_MODIFIABLE pKey; AST(KEY NAME(Key), ESTRING, ADDNAMES(pKey), PERSIST)
	CONST_STRING_MODIFIABLE pVal; AST(STRUCTPARAM NAME(Val), ESTRING, ADDNAMES(pVal), PERSIST, FORMATSTRING(XML_ENCODE_BASE64=1))
} TriviaData;
extern ParseTable parse_TriviaData[];
#define TYPE_parse_TriviaData TriviaData

AUTO_STRUCT AST_CONTAINER;
typedef struct TriviaList
{
	CONST_EARRAY_OF(TriviaData) triviaDatas; AST(PERSIST)
} TriviaList;
extern ParseTable parse_TriviaList[];
#define TYPE_parse_TriviaList TriviaList

AUTO_STRUCT AST_CONTAINER;
typedef struct TriviaListMultiple
{
	CONST_EARRAY_OF(TriviaList) triviaLists; AST(PERSIST)
} TriviaListMultiple;

AUTO_STRUCT AST_CONTAINER;
typedef struct TriviaOverviewValue
{
	CONST_STRING_MODIFIABLE pVal; AST(ESTRING, PERSIST, FORMATSTRING(XML_ENCODE_BASE64=1))
	const U32 uCount;
} TriviaOverviewValue;

AUTO_STRUCT AST_CONTAINER;
typedef struct TriviaOverviewItem
{
	CONST_STRING_MODIFIABLE pKey; AST(KEY, ESTRING, PERSIST)
	CONST_EARRAY_OF(TriviaOverviewValue) ppValues;
} TriviaOverviewItem;

AUTO_STRUCT AST_CONTAINER;
typedef struct TriviaOverview
{
	CONST_EARRAY_OF(TriviaOverviewItem) ppTriviaItems;
} TriviaOverview;
AST_PREFIX()

AUTO_STRUCT AST_CONTAINER;
typedef struct CommentEntry
{
	CONST_STRING_MODIFIABLE pDesc; AST(PERSIST) //User-entered comment
	CONST_STRING_MODIFIABLE pIP; AST(PERSIST)	//User's IP address
		//Add any additional fields we want to keep track of here - IP, User, etc.
} CommentEntry;

// Opaque mutex handle.
typedef struct
{
	void *mutex;
} TriviaMutex;

// As far as I can tell, the only purpose of the "dumb mutex" is to prevent reading in an incomplete or incoherent trivia file
// because some other process is writing a trivia file at the same time.  It doesn't seem to provide any additional guarantees, and the mutex
// should never be held for longer than it takes to read (triviaListCreateFromFile()) or write (triviaListWriteToFile()) the file.
TriviaMutex triviaAcquireDumbMutex(const char *name);
void triviaReleaseDumbMutex(TriviaMutex mutex);

// For working with global errortracking trivia
bool triviaRemoveEntry(const char *key);
bool triviaPrintf_dbg(const char *key, FORMAT_STR const char *format, ...);
#define triviaPrintf(key, format, ...) triviaPrintf_dbg(key, FORMAT_STRING_CHECKED(format), __VA_ARGS__)
void triviaPrintStruct(const char *prefix, ParseTable *tpi, const void *structptr);
void triviaPrintFromFile(const char *prefix, const char *fname);
const char* triviaGetValue(const char *key); // return value valid until updated by one of the functions above

TriviaData **triviaGlobalGet(void);
void triviaGlobalRelease(void);


// For reading trivia from a file
TriviaList* triviaListCreate(void);
TriviaList* triviaListCreateFromFile(const char *fname);
const char* triviaListGetValue(TriviaList *list, const char *key);
TriviaData * triviaListFindEntry(TriviaList *list, const char *key);
TriviaData* triviaListRemoveEntry(TriviaList *list, const char *key);
void triviaListDestroy(SA_PRE_OP_VALID SA_POST_OP_NULL TriviaList **pList);

// For writing trivia to a file
bool triviaListPrintf(TriviaList *list, const char *key, FORMAT_STR const char *format, ...);
#define triviaListPrintf(list, key, format, ...) triviaListPrintf(list, key, FORMAT_STRING_CHECKED(format), __VA_ARGS__)
void triviaListWriteToFile(TriviaList *list, const char *fname);
void triviaListClear(TriviaList *list);

typedef struct NOCONST(TriviaOverview) NOCONST(TriviaOverview);
// For merging into a Trivia Overview
void triviaOverviewAddValue(ATH_ARG SA_PARAM_NN_VALID NOCONST(TriviaOverview) *overview, const char *key, const char *value);
void triviaMergeOverview (SA_PARAM_NN_VALID NOCONST(TriviaOverview) *overview, CONST_EARRAY_OF(TriviaData) src, bool bKeepAllTrivia);

//for parsing trivia files that have somehow ended up in RAM already, or some other such weird case
extern ParseTable parse_file_TriviaList[];
#endif
