#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#include "textparserUtils.h"
#include "MultiVal.h"

// This file contains useful accessors for the object path system
// All of these will use table and structptr as the base for a relative
// path, if the path starts with a .

// Reads in an object path and normalizes it, which means changing redundant names to 
// the non-redundant versions, and ignoring flatembeds
bool objPathNormalize(const char* path_in, ParseTable table_in[], char **path_out);

enum
{
	OBJPATHFLAG_TRAVERSEUNOWNED = 1 << 0, //traverse through unowned structs
	OBJPATHFLAG_TOLERATEMISSPELLEDFIELDS = 1 << 1, //when an unrecognized field name is found, try to find
	                                               //a close approximation, assume it's a misspelling
    OBJPATHFLAG_CREATESTRUCTS = 1 << 2, //create intermediate structs

	OBJPATHFLAG_REPORTWHENENCOUNTERINGNULLPOLY = 1 << 3, //if the error case where an xpath is being traversed
		//with a null object and then a poly type is encountered occurs, return a special result string
		//(SPECIAL_NULL_POLY_ERROR_STRING)
		//so that this error can be differentiated from other errors

	OBJPATHFLAG_GETFINALKEY = 1 << 4,				// If the final element int he path is an array, return the key/index
													// This will not fail if the final array resolve returns NULL
													
	OBJPATHFLAG_SEARCHNONINDEXED = 1 << 5,			// If this is set, allow the path to resolve string keys for non-indexed struct arrays
	OBJPATHFLAG_DONTLOOKUPROOTPATH = 1 << 6,		// Don't try to do any root path lookups
	OBJPATHFLAG_WRITERESULTIFNOBROADCAST = 1 << 7,	// Write out "0" to the result string if the field should not be broadcast

	OBJPATHFLAG_INCREASEREFCOUNT = 1 << 8,		// When returning an ObjectPath, increase the refcount.

	OBJPATHFLAG_TREATIGNOREASSUCCESS = 1 << 9,		// When parsing objpath strings, report success if an AST_IGNORE field is encountered. The default is to report failure.
};

#define FLAGS_TO_INCLUDE_BROADCAST TOK_SUBSCRIBE

extern bool gReportPathRFailure;
extern bool gbUseNewPathFlow;

#define SPECIAL_NULL_POLY_ERROR_STRING "SPECIAL_NULL_POLY_ERROR"



// Resolves the given path, and sets the _out variables. These will be set in a form
// that can be used from functions in textparserutils.h and tokenstore.h
bool objPathResolveField(const char* path_in, ParseTable table_in[], void* structptr_in, 
					   ParseTable** table_out, int* column_out, void** structptr_out, int* index_out, 
						U32 iObjPathFlags);

// Resolve an object path, and also get the result string.
bool objPathResolveFieldWithResult(const char* path_in, ParseTable table_in[], void* structptr_in, 
								   ParseTable** table_out, int* column_out, void** structptr_out, int* index_out,
								   U32 iObjPathFlags, char **ppchResult);

//given an object path, returns the multi val type that path represents, if determinable
MultiValType objPathGetType(const char *path_in, ParseTable table_in[]);

// Sets str to a string representation of the specified object path
bool objPathGetString(const char* path, ParseTable table[], void* structptr, 
					  char* str, int str_size);

// Sets "val" to the value of the specified object path
bool objPathGetInt(const char* path, ParseTable table[], void* structptr, int* val);
bool objPathGetBit(const char* path, ParseTable table[], void* structptr, U32* val);
bool objPathGetF32(const char* path, ParseTable table[], void* structptr, F32* val);

// Sets the value of the specified object path to "val"
// force = true creates the structures needed to resolve the path
bool objPathSetInt(const char* path, ParseTable table[], void* structptr, int val, bool force);
bool objPathSetBit(const char* path, ParseTable table[], void* structptr, U32 val, bool force);
bool objPathSetF32(const char* path, ParseTable table[], void* structptr, F32 val, bool force);


// Sets str to a string representation of the specified object path
//
//FIXME //In at least some cases, this APPENDS rather than setting the result string. I just noticed
//this so am just initializing my string to empty for now. I'm afraid of changing the behavior and
//breaking other things ABW
bool objPathGetEStringWithResult(const char* path, ParseTable table[], void* structptr, 
					  char** estr, char **ppResultEString);
#define objPathGetEString(path, table, structptr, estr) objPathGetEStringWithResult(path, table, structptr, estr, NULL)

// Sets the given object path field to the value of the string str
bool objPathSetString(const char* path, ParseTable table[], void* structptr, 
					  const char* str);

// Gets the value of a path, as a multival
bool objPathGetMultiValWithResult(const char* path, ParseTable table[], void* structptr, 
						MultiVal *result, char **ppResultString);
#define objPathGetMultiVal(path, table, structptr, result) objPathGetMultiValWithResult(path, table, structptr, result, NULL)
// Sets the value of a path, from a multival
bool objPathSetMultiVal(const char* path, ParseTable table[], void* structptr, 
						const MultiVal *result);

// Sets table_out and struct_out to the substructure designated by the given path
bool objPathGetStruct(const char* path, ParseTable table[], void* structptr, 
					  ParseTable **table_out, void **struct_out);

// Returns the EArray corresponding to a given path, and the ParseTable of 
// the members of the EArray, if they are structures
bool objPathGetEArray(const char* path, ParseTable table[], void* structptr, 
					  void ***earray_out, ParseTable **table_out);

// Upon returning false, this function will print the causative error into the estring ppchResult.
bool objPathGetEArrayWithResult(const char* path, ParseTable table[], void* structptr, 
					  void ***earray_out, ParseTable **table_out, char **ppchResult);

// Returns the Int EArray corresponding to a given path
bool objPathGetEArrayInt(const char* path, ParseTable table[], void* structptr, 
						 int **earrayint_out);

// Upon returning false, this function will print the causative error into the estring ppchResult.
bool objPathGetEArrayIntWithResult(const char* path, ParseTable table[], void* structptr, 
								   int **earrayint_out, char **ppchResult);

// Path operations, which use object paths to modify objects

// Types of operations
typedef enum
{
	TRANSOP_INVALID,
	TRANSOP_SET,
	TRANSOP_ADD,
	TRANSOP_SUB,
	TRANSOP_MULT,
	TRANSOP_DIV,
	TRANSOP_OR,
	TRANSOP_AND,
	TRANSOP_GET,
	TRANSOP_GET_NULL_OKAY,
	TRANSOP_GET_ARRAY_ONLY,
	TRANSOP_CREATE,
	TRANSOP_DESTROY,
} ObjectPathOpType;


// Per-object transactions
typedef struct ObjectPathOperation
{
	ObjectPathOpType op;
	char *pathEString;
	char *valueEString;

	int iValueAtoid;

	char pathStringInitialBuff[300];
	char valueStringInitialBuff[300];

	bool quotedValue;

} ObjectPathOperation;


// Apply a single path operation, such as adding, removing, etc
int objPathApplySingleOperation(ParseTable *table, int column, void *object, int index, ObjectPathOpType op, const char *value, bool quotedValue, char **resultString);

// Parse a single operation
int objPathParseSingleOperation(const char *path, size_t length, ObjectPathOpType *op, char ** pathEstr, char ** valueEstr, bool *quotedValue);

// Parse and extract a set of operations to apply.
int objPathParseOperations(ParseTable *table, const char *query, ObjectPathOperation ***operations);
int objPathParseOperations_Fast(ParseTable *table, const char *query, ObjectPathOperation ***pathOperations);
#define objPathParseOperations(table, query, operations) ((!gbUseNewPathFlow)?objPathParseOperations((table), (query), (operations)):objPathParseOperations_Fast((table), (query), (operations)))

// Executes a set of operations on the given object
int objPathApplyOperations(ParseTable *table, void *object, ObjectPathOperation **operations, ObjectPath ***cachedpaths);

// Turn an operation back into text
void objPathWriteSingleOperation(char **resultString, ObjectPathOperation *pOperation);


// Parse and apply a set of path operations
int objPathParseAndApplyOperations(ParseTable *table, void *object, const char *query);

// Given an earray allocated elsewhere, adds to it the names of all fields in a structure
bool objPathGetStructFields(const char* path, ParseTable table[], void* structptr, const char ***earray);

// Useful utility functions used by path operations

// Gets the key value as a string if possible
bool objGetKeyString(ParseTable tpi[], void * structpr, char *str, int str_size);

//Given an xpath string, return true if it is the name of the parsetable's key.
bool objPathIsKey(ParseTable tpi[], const char *str);

// Gets it as an EString if possible
bool objGetKeyEString(ParseTable tpi[], void * structptr, char **str);

// Gets it as an EString if possible
bool objGetEscapedKeyEString(ParseTable tpi[], void * structptr, char **str);

// Gets the key value as an int if possible
bool objGetKeyInt(ParseTable tpi[], void * structpr, int *key);


// These functions are auto commands, but are also useful accessors

// Returns the string value equal to the passed in global path
char *ReadObjectPath(const char *path);

// Sets the global path to the passed in value. DO NOT USE THIS WITH TRANSACTED DATA
char *SetObjectPath(const char *path, const char *value);

// Returns the list of fields of a struct referred to by an object path
char *ListObjectPathFields(const char *path);

// Returns the list of keys in an earray of structs referred to by an object path
char *ListObjectPathKeys(const char *path);

ObjectPathOperation *CreateObjectPathOperation(void);
void DestroyObjectPathOperation(ObjectPathOperation *fieldEntry);


// Named Query support

AUTO_STRUCT;
typedef struct NamedPathQuery
{
	const char *queryName; AST(KEY POOL_STRING)
	char *queryString;
	char *queryDescription;
	char *fileName; AST(CURRENTFILE)
} NamedPathQuery;

// Get the specified query
NamedPathQuery* objGetNamedQuery(const char *name);

// Loads queries in directory
void objLoadNamedQueries(const char *directory, const char *filespec, const char *binFile);

AUTO_STRUCT;
typedef struct NamedPathQueryAndResult
{
	const char *pchKey; AST(KEY STRUCTPARAM REQUIRED POOL_STRING)
	const char *pchObjPath; AST(STRUCTPARAM REQUIRED)
	MultiVal mvValue; AST(NAME(Value) STRUCTPARAM)
} NamedPathQueryAndResult;

AUTO_STRUCT;
typedef struct NamedPathQueriesAndResults
{
	NamedPathQueryAndResult **eaQueries; AST(NAME(Query))
	const char *pchFilename; AST(CURRENTFILE)
} NamedPathQueriesAndResults;

AUTO_STRUCT;
typedef struct WildCardQueryResult
{
	//can't be a poolstring because the key might be a non-string which has to be
	//string-converted
	const char *pKey; AST(KEY)
	const char *pValue; AST(ESTRING)
} WildCardQueryResult;


//does a query with exactly one * inside brackets. So for instance, ".powers[*].type" will return the names of all
//the elements in .powers, and the output of objPathGetEString() on .powers[key].type for each of those
//keys
bool objDoWildCardQuery(char *pQueryString, ParseTable tpi[], void *pStructPtr, WildCardQueryResult ***pppOutList, char **ppResultString);

// Given a string "Root[Key]" (or "Root[Key].Blah.Blah"), extract "Root" and "Path"
// into the passed-in estrings. Returns false if the two fields can't be parsed.
bool objPathParseRootDictionaryAndRefData(const char *pchPath, char **ppchDict, char **ppchRefData);

//NOTE - check textparserutils.h for PARSERRESOLVE_ error messages which you can compare the return
//string from various objPath functions to to see how it failed


AUTO_STRUCT;
typedef struct StringQueryList {
	STRING_EARRAY eaCommandList;
	STRING_EARRAY eaList;
	INT_EARRAY eaTimes;
} StringQueryList;

