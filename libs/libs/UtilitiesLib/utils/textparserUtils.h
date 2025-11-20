#ifndef TEXT_PARSER_UTILS_H
#define TEXT_PARSER_UTILS_H
GCC_SYSTEM

#include "TextParserEnums.h"

C_DECLARATIONS_BEGIN

typedef struct MultiVal MultiVal;
typedef struct Packet Packet;
typedef struct DefineContext DefineContext;
typedef struct PerformanceInfo PerformanceInfo;
typedef struct ResourceInfo ResourceInfo;
typedef struct StructCopyInformation StructCopyInformation;
typedef struct StashTableImp *StashTable;
typedef struct SimpleBuffer *SimpleBufHandle;
typedef struct FileWrapper FileWrapper;
typedef struct ParseTableInfo ParseTableInfo;
typedef struct MemoryPoolImp *MemoryPool;
typedef U64 StructTypeField;
typedef struct ThreadSafeMemoryPool ThreadSafeMemoryPool;


/********************************* ParseTable Operations *********************************/

// These are a variety of useful functions that are run on a ParseTable itself, to acquire
// or set information about it

// Returns the parsetable that corresponds to a named struct, optionally sets the size
ParseTable *ParserGetTableFromStructName(const char *pName);

// Gets the name of the structure that is defined by the passed in ParseTable
const char *ParserGetTableName(ParseTable table[]);

/*
logically, this should be here too, but limitations of the latelink system prevent that, so
the prototype is in utilitieslib.h

LATELINK;
const char *ParserGetTableName_WithEntityFixup(ParseTable *table, void *pObject);
*/

// Gets the memory size of the structure defined by the passed in ParseTable
int ParserGetTableSize(ParseTable table[]);

// Gets the total number of columns
int ParserGetTableNumColumns(ParseTable table[]);

// Returns the column that is a key, or -1 if none
int ParserGetTableKeyColumn(ParseTable table[]);

//gets the key of a struct as a string, appends it onto the end of an estring. Will usually run in "fast" mode,
//unless your key is something oddball like a struct or a formatted string
bool ParserGetKeyStringDbg(ParseTable table[], void *pStruct, char **ppOutEString, const char *caller_fname, int iLine);
#define ParserGetKeyString(table, pStruct, ppOutEString) ParserGetKeyStringDbg(table, pStruct, ppOutEString, __FILE__, __LINE__)

// Returns the CRC of this parse table and it's dependencies
int ParseTableCRC(ParseTable pti[], DefineContext* defines, U32 iFlags);

// Finds a column corresponding to given name
bool ParserFindColumn(ParseTable table[], const char* colname, int* column); // lookup column by name

//returns true if an IGNORE column exists in this tpi with this name
bool ParserFindIgnoreColumn(ParseTable table[], const char *colname);

// Find a column corresponding to a given storage offset in the structure.
int ParserFindColumnFromOffset(ParseTable table[], size_t offset);

// Returns true if it finds a column with given token type. returns the first one found
bool ParserFindColumnWithType(ParseTable table[], StructTypeField type, int *column);

//find a column in the TPI which matches at least one of the flag bits in flag, returns true or false,
//sets column number
bool ParserFindColumnWithFlag(ParseTable table[], StructTypeField flag, int *column);


// Returns true if passed in field is an indexed earray
bool ParserColumnIsIndexedEArray(ParseTable tpi[], int column, int *keyColumn);

// If there is a global dictionary associated with this token, return it
const char *ParserColumnGetDictionaryName(ParseTable tpi[], int column);

// Returns the staticDefine for a given tpi and column, or NULL if none
StaticDefine* ParserColumnGetStaticDefineList(ParseTable *tpi, int column);

//returns true if this is a temporary table, ie one created for server monitoring or as a hybrid object or somethign
bool ParserTableIsTemporary(ParseTable *tpi);

//get set the "Creation comment offset", which is the offset in the struct of a NO_AST string field
//which should get automatically filled in with a comment describing how the field was created
void SetCreationCommentOffsetInTPIInfoColumn(ParseTable table[], int iOffset);
int ParserGetCreationCommentOffset(ParseTable table[]);

/********************************* Data Accessors *********************************/

// These are a set of functions that get or set data from ParseTable-created objects
// in a variety of ways

// In general TokenXxx functions deal with an entire array, while FieldXxx functions let you specify an index


// These functions write out a text representation of a ParseTable column
// If WRITETEXTFLAG_PRETTYPRINT is set, there is no guarantee that you can later execute TokenFromSimpleString on it

// Output a token (arrays are a single token) to an eString
bool TokenWriteText_dbg(ParseTable tpi[], int column, SA_PARAM_NN_VALID const void* structptr, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, SA_PARAM_NN_STR const char *caller_fname, int line);
#define TokenWriteText(tpi, column, structptr, estr, iWriteTextFlags) TokenWriteText_dbg(tpi, column, structptr, estr, iWriteTextFlags, 0, 0, __FILE__, __LINE__)
#define TokenWriteTextWFlags(tpi, column, structptr, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) FieldWriteText_dbg(tpi, column, structptr, index, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, __FILE__, __LINE__)

// Output a field (individual element of a field) to an eString
bool FieldWriteText_dbg(ParseTable tpi[], int column, SA_PARAM_NN_VALID const void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, SA_PARAM_NN_STR const char *caller_fname, int line);
#define FieldWriteText(tpi, column, structptr, index, estr, iWriteTextFlags) FieldWriteText_dbg(tpi, column, structptr, index, estr, iWriteTextFlags, 0, 0, __FILE__, __LINE__)
#define FieldWriteTextWFlags(tpi, column, structptr, index, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude) FieldWriteText_dbg(tpi, column, structptr, index, estr, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, __FILE__, __LINE__)

// Versions of above functions that output to a static string. 
// These use an intermediate eString and are slower
SA_RET_VALID bool TokenToSimpleString(ParseTable tpi[], int column, SA_PARAM_NN_VALID const void* structptr, SA_PRE_NN_ELEMS_VAR(str_size) SA_POST_OP_VALID char* str, int str_size, WriteTextFlags iWriteTextFlags);
SA_RET_VALID bool FieldToSimpleString(ParseTable tpi[], int column, SA_PARAM_NN_VALID const void* structptr, int index, SA_PRE_NN_ELEMS_VAR(str_size) SA_POST_OP_VALID char* str, int str_size, WriteTextFlags iWriteTextFlags);

// Sets the value of an entire token from a string
bool TokenFromSimpleString(ParseTable tpi[], int column, SA_PARAM_NN_VALID void* structptr, SA_PARAM_NN_STR const char* str);
#define TokenReadText(tpi,column, structptr, str) TokenFromSimpleString(tpi,column,structptr,str)

// Sets the value of a individual element from a string
bool FieldFromSimpleString(ParseTable tpi[], int column, SA_PARAM_NN_VALID void* structptr, int index, SA_PARAM_NN_STR const char* str);
#define FieldReadText(tpi,column, structptr, index, str) FieldFromSimpleString(tpi,column,structptr,index,str)


// Similar to WriteText, but puts/gets from a MultiVal
// Doesn't support complex types like structs.

// Outputs an entire token to a MultiVal
SA_RET_VALID bool TokenToMultiVal(ParseTable tpi[], int column, SA_PARAM_NN_VALID const void* structptr, SA_PRE_NN_FREE SA_POST_NN_VALID MultiVal* result);

// Outputs a single field to a MultiVal
// If bDuplicateData is true, then strings and pointer data like vectors will be copied instead of referenced
SA_RET_VALID bool FieldToMultiVal(ParseTable tpi[], int column, SA_PARAM_NN_VALID const void* structptr, int index, SA_PRE_NN_FREE SA_POST_NN_VALID MultiVal* result, bool bDuplicateData, bool dummyOnNULL);

// Read entire token from a MultiVal
bool TokenFromMultiVal(ParseTable tpi[], int column, SA_PARAM_NN_VALID void* structptr, SA_PARAM_NN_VALID const MultiVal* value);

// Read single element from a MultiVal
bool FieldFromMultiVal(ParseTable tpi[], int column, SA_PARAM_NN_VALID void* structptr, int index, SA_PARAM_NN_VALID const MultiVal* value);


// Generally useful accessors for a specific object

// get a subtable based on column and index, optionally get the subtable and size fields for the subtable
// - this encapsulates the operations necessary for dealing with a polymorphic subtable
SA_ORET_OP_VALID void* StructGetSubtable(ParseTable tpi[], int column, void* structptr, int index, ParseTable** subtable, size_t* subsize);

// Returns true of the specified Field is it's default value
// Only for primitive types (string, int, float)
bool FieldIsDefault(ParseTable tpi[], int column, void* structptr, int index);

// Returns the filename of an object, if it has one
const char *ParserGetFilename(ParseTable *pti, void *structptr);

// Set the filename of an object, if it has one (it should)
void ParserSetFilename(ParseTable *pti, void *structptr, const char* filename);

//if there is a KEY field, use it for the name. Otherwise, return "unknown"
const char *ParserGetStructName(ParseTable table[], void *pStruct);

// Copy a single field/token between structures that share a ParseTable
void FieldCopy(ParseTable tpi[], int column, void *dstStruct, void *srcStruct, int index, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
void TokenCopy(ParseTable tpi[], int column, void *dstStruct, void *srcStruct, bool bCopyAll);

// Set the value of a Field/Token back to its default
void FieldClear(ParseTable tpi[], int column, void *dstStruct,int index);
void TokenClear(ParseTable tpi[], int column, void *dstStruct);

// Copy all substructs in srcStruct (in this column) to dstStruct
// copyAll specifies rather to copy non-ParseTable data
void TokenAddSubStruct(ParseTable tpi[], int column, void *dstStruct, void *srcStruct, bool copyAll);

// looks at keys to merge lists of substructs.  if key is in src, but not dst, adds to end of list
// if overwriteSame is set, then any structs that exist in both dst & src will be overwritten in dst with copies from src
void TokenSubStructKeyMerge(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, bool overwriteSame, bool copyAll);

// Runs a callback for every substruct of type pTargetTable
// If a callback is provided, returns TRUE if at least one callback was executed and returned TRUE.
// Otherwise, returns TRUE if at least one matching substruct was found.
// Ignores UNOWNED structs by default
typedef bool (*ParserScanForSubstructCB) (void *pStruct, void *pRootStruct, const char *pcPathString, void *pCBData);
bool ParserScanForSubstruct(ParseTable pti[], void *pStruct, ParseTable *pTargetTable, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, ParserScanForSubstructCB callback, void *pCBData);

// Walks the given ParseTable and runs a callback for every column.
// Ignores UNOWNED structs by default
typedef bool (*ParserTraverseParseTableCB) (ParseTable pti[], void *pStruct, int column, int index, void *pCBData);
typedef bool (*ParserTraverseParseTableColumnCB) (ParseTable pti[], void *pStruct, int column, void *pCBData);
bool ParserTraverseParseTable(ParseTable pti[], void *pStruct, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, ParserTraverseParseTableCB callback, ParserTraverseParseTableColumnCB preColumnCallback, ParserTraverseParseTableColumnCB postColumnCallback, void *pCBData);

// Pass in an earray of GlobalObjects, which will be added to
bool ParserFindDependencies(ParseTable table[], void *structptr, ResourceInfo *parentObject, char *pathPrefix, bool bOnlyImportantRefs, bool bIncludePaths);


/********************************* Struct Defaults *********************************/

// Finds the used bit field, -1 if not found
int ParserGetUsedBitFieldIndex(ParseTable pti[]);

// Checks the used bit field to see if a column has been specified
bool TokenIsSpecifiedEx(ParseTable tpi[], int column, void* srcStruct, int iBitFieldIndex, bool bReturnThisIfNoUsedFieldExists);
#define TokenIsSpecified(tpi, column, srcStruct, iBitFieldIndex) TokenIsSpecifiedEx(tpi, column, srcStruct, iBitFieldIndex, false)

int TokenIsSpecifiedByName(ParseTable pti[], void* srcStruct, const char *pchField);

// Set the used bit field for a given column, to show it has been specified
void TokenSetSpecified(ParseTable tpi[], int column, void* srcStruct, int iBitFieldIndex, bool bOn);

//same as the previous, but OK if there is no used bitfield
void TokenSetSpecifiedIfPossible(ParseTable tpi[], int column, void* srcStruct, bool bOn);

// nice way of implementing inheritance for structs, any fields specified in srcStruct will override those
// in dstStruct.  If addSubStructs is set, substructures get added to the end of the list instead of overridding.
// If keyMerge is set, then substructs are added or merged based on their key values
void StructOverride(ParseTable pti[], void *dstStruct, void *srcStruct, int addSubStructs, bool keyMerge, bool copyAll);

// Reversed StructOverride - more appropriate for a data-defined "default" struct.  Any fields not specified in dst are filled with default values.
// addSubStructs - substructs in defaultStruct will be ADDED to the list of structs in dest
// applySubStructFields - function will recurse so that individual fields in substructures will also use defaults, instead of only applying on a whole-struct level
// If keyMerge is set, then substructs are added or merged based on their key values
void StructApplyDefaults(ParseTable pti[], void *dstStruct, void *defaultStruct, int addSubStructs, int applySubStructFields, bool keyMerge);


/********************************* Dirty Bit Support *********************************/

bool ParserTableHasDirtyBit(ParseTable table[]);
bool ParserTableHasDirtyBitAndGetIt(ParseTable table[], const void *pStruct, bool *pOutBit);
bool ParserChildrenHaveDirtyBit(ParseTable table[]);

//when calling SetDirtyBit you must report whether substructs of the current struct changed (as opposed to if
//there were just data changes in the current struct)
int ParserSetDirtyBit_sekret(ParseTable table[], void *pStruct, bool bSubStructsChanged MEM_DBG_PARMS);

// DOES NOT RECURSE. Don't call this without a good purpose
void ParserClearDirtyBit(ParseTable table[], void *pStruct);


/********************************* Polymorphic Types *********************************/

// These functions deal with TOK_POLYMORPH objects

// Returns column of the object type
int ParserGetTableObjectTypeColumn(ParseTable table[]);

// for a TOK_POLYMORPH field, you can use FieldDeterminePolyType to determine which polymorphic type
// this field is pointing to.  The result is an index into the polytable (tpi[column].subtable)
bool FieldDeterminePolyType(ParseTable tpi[], int column, void* structptr, int index, int* polycol);
__forceinline static bool TokenDeterminePolyType(ParseTable tpi[], int column, void* structptr, int* polycol)
	{ return FieldDeterminePolyType(tpi, column, structptr, 0, polycol); }

// if you are just pointing to a random piece of memory, you can use StructDeterminePolyType to determine
// what type of polymorphic memory you are pointing to.  You have to provide the polytable directly - this
// should be a parse table that has only STRUCT members that represent possible poly types
bool StructDeterminePolyType(ParseTable* polytable, void* structptr, int* polycol);

// return the ParseTable for the given structure, which must be one of the types specified in polytable.
ParseTable* PolyStructDetermineParseTable(ParseTable* polytable, void* structptr);

// Return true if the given polytable contains the given ParseTable, false if not, assert if
// polytable doesn't look like a real polytable.
bool PolyTableContains(ParseTable* polytable, ParseTable *table);


/********************************* Math Operations *********************************/

// These functions allow various calculations to be done on a ParseTable

void TokenInterpolate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, F32 interpParam);
void StructInterpolate(ParseTable pti[], void* structA, void* structB, void* destStruct, F32 interpParam);

void TokenCalcRate(ParseTable tpi[], int column, void* srcStoreA, void* srcStoreB, void* destStore, F32 deltaTime );
void StructCalcRate(ParseTable pti[], void* structA, void* structB, void* destStruct, F32 deltaTime );

void TokenIntegrate(ParseTable tpi[], int column, void* srcValue, void* srcRate, void* destStore, F32 deltaTime );
void StructIntegrate(ParseTable pti[], void* valueStruct, void* rateStruct, void* destStruct, F32 deltaTime );

void TokenCalcCyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, F32 fStartTime, F32 deltaTime );
void StructCalcCyclic(ParseTable pti[], void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, F32 fStartTime, F32 deltaTime );

typedef enum DynOpType
{
	DynOpType_None = 0,
	DynOpType_Jitter,
	DynOpType_SphereJitter,
	DynOpType_SphereShellJitter,
	DynOpType_Inherit,
	DynOpType_Add,
	DynOpType_Multiply,
	DynOpType_Min,
	DynOpType_Max,
} DynOpType;

void FieldApplyDynOp(ParseTable tpi[], int column, int index, DynOpType optype, F32* values, U8 uiValuesSpecd, void* dstStruct, void* srcStruct, U32* seed);
__forceinline static void TokenApplyDynOp(ParseTable tpi[], int column, DynOpType optype, F32* values, U8 uiValuesSpecd, void* dstStruct, void* srcStruct, U32* seed)
	{ FieldApplyDynOp(tpi, column, 0, optype, values, uiValuesSpecd, dstStruct, srcStruct, seed); }
void StructApplyDynOp(ParseTable pti[], DynOpType optype, const F32* values, U8 uiValuesSpecd, void* dstStruct, void* srcStruct, U32* seed);


/********************************* TextParserBinaryBlock *********************************/

//TextParserBinaryBlock, a simple struct into which arbitrary binary data can be shoved, but which is AUTO_STRUCTable, 
//so that you can easily send/save/whatever it. (Not hugely high performance, but not disastrous either.)

extern ParseTable parse_TextParserBinaryBlock[];
#define TYPE_parse_TextParserBinaryBlock TextParserBinaryBlock

#define TEXTPARSERBINARYBLOCK_MAXSIZE_WORDS 3000000
#define TEXTPARSERBINARYBLOCK_MAXSIZE_BYTES (4 * TEXTPARSERBINARYBLOCK_MAXSIZE_WORDS)

AUTO_STRUCT AST_NOMEMTRACKING;
typedef struct TextParserBinaryBlock
{
	//must be in sync with TEXTPARSERBINARYBLOCK_MAXSIZE_WORDS
	U32 *pInts; AST(FORMATSTRING(MAX_ARRAY_SIZE = 3000000))
	
	U32 iActualByteSize;
	U32 iSizeBeforeZipping; //if 0, then it is not zipped
} TextParserBinaryBlock;

int TextParserBinaryBlock_GetSize(TextParserBinaryBlock *pBlock);

#define TextParserBinaryBlock_CreateFromFile(pFileName, bZip) TextParserBinaryBlock_CreateFromFile_dbg(pFileName, bZip MEM_DBG_PARMS_INIT)
TextParserBinaryBlock *TextParserBinaryBlock_CreateFromFile_dbg(char *pFileName, bool bZip MEM_DBG_PARMS);

#define TextParserBinaryBlock_CreateFromMemory(pBuf, iBufSize, bZip) TextParserBinaryBlock_CreateFromMemory_dbg(pBuf, iBufSize, bZip MEM_DBG_PARMS_INIT)
TextParserBinaryBlock *TextParserBinaryBlock_CreateFromMemory_dbg(char *pBuf, int iBufSize, bool bZip MEM_DBG_PARMS);

bool TextParserBinaryBlock_PutIntoFile(TextParserBinaryBlock *pBlock, char *pOutFileName);

#define TextParserBinaryBlock_PutIntoMallocedBuffer(pBlock, piOutSize) TextParserBinaryBlock_PutIntoMallocedBuffer_dbg(pBlock, piOutSize MEM_DBG_PARMS_INIT)
void *TextParserBinaryBlock_PutIntoMallocedBuffer_dbg(TextParserBinaryBlock *pBlock, int *piOutSize MEM_DBG_PARMS);

TextParserBinaryBlock *TextParserBinaryBlock_AssignMemory(TextParserBinaryBlock *pBlock, const U8 *pBuf, int iBufSize, bool bZip);

/********************************* ObjectPath support *********************************/

// Textparser path functions allow for XPath-style addressing of textparser objects

typedef void * TPIPointer;

AUTO_STRUCT;
typedef struct ObjectPathSegment {
	U16 fieldOffset;			//the index of the objectpath where the field starts
	U16 fieldSize;				//the string length of the field
	int column;					//the index of the ParseTable e.g. tpi[column]
	size_t offset;				//the pointer offset within the struct
	int index;					//the index of array elements (usually -1 for non-arrays)
	const char *key;			AST( POOL_STRING )
	bool descend;				//true if not leaf-node
	bool isPoly;
} ObjectPathSegment;

AUTO_STRUCT;
typedef struct ObjectPathKey {
	// All fields are STRUCTPARAM because this is a requirement of it being the AST(KEY)
	// of ObjectPath below
	char *lookupString;				AST(ESTRING STRUCTPARAM)
	const char *pathString;			AST(UNOWNED, STRUCTPARAM) // points into lookupString
	TPIPointer rootTpi;				AST(INT, STRUCTPARAM)	//ParseTable
	char lookupStringBuffer[300];	NO_AST
} ObjectPathKey;

AUTO_STRUCT;
typedef struct ObjectPath {
	ObjectPathKey *key;			AST(KEY)	
	ObjectPathSegment **ppPath;
	U32	hitcount;				AST(INDEX_DEFINE)
	int refcount;				AST(NAME(rc))
} ObjectPath;

#define INVALID_COLUMN -1
#define LOOKUP_COLUMN -2
#define MAX_OBJECT_PATH 1024

typedef enum enumReadObjectPathIdentifierResult
{
	READOBJPATHIDENT_OK,
	READOBJPATHIDENT_EMPTYKEY,
	READOBJPATHIDENT_PARSEFAIL,
} enumReadObjectPathIdentifierResult;

// Tries to read out a path identifier, which may be quoted and escaped
enumReadObjectPathIdentifierResult ReadObjectPathIdentifier(char **estrOut, const char** path, bool *quoted);

// main function for resolving a textparser path - result is suitable for use with TokenStore functions
// - table_in and structptr_in are not required if you are referencing well-known root path (Entities, etc.)
//
// as is obvious from the naming, the things returned are table_out, column_out, etc., which give
// a textparser-world description of the piece of data requested.
//
// Then you would say "TokenStoreGetInt(table, column, structptr, index)", and get the
// actual int (or what have you).
// If bTraverseUnowned is set, then it will traverse down into references and unowned structs
#define ParserResolvePath(path_in, table_in, structptr_in,table_out, column_out, structptr_out, index_out, resultString, correctedString, iObjPathFlags) ParserResolvePathEx(path_in, table_in, structptr_in,table_out, column_out, structptr_out, index_out, NULL, resultString, correctedString, NULL, iObjPathFlags)

//Optionally resolves root paths.
bool ParserResolvePathEx(const char* path_in, ParseTable table_in[], void* structptr_in, 
	   ParseTable** table_out, int* column_out, void** structptr_out, int* index_out, ObjectPath **objpath_out,
	   char **resultString, char **correctedString, char **createString, U32 iObjPathFlags);

//Descend a parsetable given a single path segment.
// *no polys
bool ParserResolvePathSegment(ObjectPathSegment *pSeg, ParseTable *tpi, const void *structPtr, ParseTable **tpiOut, void **struct_out, int *column_out,  int *index_out, char **resultString, U32 iObjPathFlags);

//Get a ref handle with a compiled objectpath.
bool ParserResolvePathComp(ObjectPath *objPath, const void *structPtr, ParseTable **tpiOut, int *column_out, void **struct_out, int *index_out, char **resultString, U32 iObjPathFlags);

//Find a matching string from the objectpath cache strings.
const char* findObjectPathString(const char *path);

//Creates a new ObjectPath for a specified field and tpi.
// Passing -1 to column will search the tpi for the field-named column instead of just setting it.
// index should be -1 for default (non-arrays or keyed arrays)
// key is optional.
ObjectPath *ObjectPathCreate(ParseTable *tpi, const char *field, int column, int index, const char *key);

//Creates a copy of a compiled object path.
// *Reuses stringrefs from the objectpath cache strings.
// *Reuses the ParseTable reference.
// *Copies path segments.
ObjectPath *ObjectPathCopy(ObjectPath* path);

//Creates a copy of a compiled path by appending a path segment.
// *Creates new stringref for objectpath and adds to the cache strings.
// *Reuses the ParseTable reference.
// *Passing LOOKUP_COLUMN for column does the TPI lookup.
// *Copies path segements.
ObjectPath *ObjectPathCopyAndAppend(ObjectPath* path, const char *field, int column, int index, const char *key);

//Modifies a compiled object path by appending a path segment.
// *Creates a new string ref for objectpath and adds to the cache strings.
// *Passing LOOKUP_COLUMN for column does the TPI lookup. 
// *This is a destructive mutator!
ObjectPath* ObjectPathAppend(ObjectPath* path, const char *field, int column, int index, const char *key);

//Modifies a compiled object path by removing the last path segment.
// *Creates a new string ref for objectpath and adds to the cache strings.
// *This is a destructive mutator!
ObjectPath *ObjectPathChopSegment(ObjectPath *path);

//Creates a copy of a compiled path by adding an index or key to the last path segment.
// *Creates a new stringref for objectpath and adds to the cache strings.
// *Reuses the ParseTable reference.
// *Copies path segments.
ObjectPath *ObjectPathCopyAndAppendIndex(ObjectPath* path, int index, const char *key);

//Modifies a compiled path by adding an index or key to the last path segment.
// *Creates a new stringref for objectpath and adds to the cache strings.
// *Reuses the ParseTable reference.
// *This is a destructive mutator!
ObjectPath *ObjectPathAppendIndex(ObjectPath* path, int index, const char *key);

//Modifies a compiled object path by removing the index part of the path.
// *Creates a new string ref for objectpath and adds to the cache strings.
// *This is a destructive mutator!
ObjectPath *ObjectPathChopSegmentIndex(ObjectPath *path);

void ObjectPathSetTailDescend(ObjectPath *path, bool flag);

ObjectPathSegment* ObjectPathTail(ObjectPath *path);

//Gets the last path segment string.
const char * ObjectPathTailString(ObjectPath *path);

bool ObjectPathGetParseTables(ObjectPath *objPath, const void *structPtr, ParseTable ***pppTables, char **resultString, U32 iObjPathFlags);

//Returns true if the beginning segments of the longpath are equivalent to the shortpath.
// *Use this to figure out if longpath is a descendant of shortpath.
// *This function assumes the ObjectPaths have the same rootTPI.
bool ObjectPathIsDescendant(ObjectPath *longpath, ObjectPath *shortpath);

//Safely free a compiled objectpath.
void ObjectPathDestroy(ObjectPath *path);

//Remove all objectpaths in the cache that have the tpi in their key.
void ObjectPathCleanTPI(ParseTable *tpi);

// Assuming that table[column] is an array, returns the index of the given key name
bool ParserResolveKey(const char* key, ParseTable table[], int column, void* structptr, int* index, U32 iObjPathFlags, char** resultString, ObjectPathSegment *top); 

// Append relative path, which is useful when building object paths dynamically
bool ParserAppendPath(char **estrPath, ParseTable tpi[], int column, SA_PARAM_NN_VALID void* structptr, int index);

// this does a root path lookup on the path with the correct tokenizing for ParserResolvePath (i.e. supports
// <container><index> and everything else ParserResolvePath should)
bool ParserResolveRootPath(const char* path_in, char** path_out, ParseTable** table_out,
						   int* column_out, void** structptr_out, char** resultString, U32 iObjPathFlags);

//these are error messages that can be returned from ParserResolvePath. Because they include
//%s and so forth, you should do a strStartsWith with the shorter versions

#define PARSERRESOLVE_INDEX_BEYOND_BOUNDS_SHORT "Integer index was outside the range of elements:"
#define PARSERRESOLVE_INDEX_BEYOND_BOUNDS_FULL PARSERRESOLVE_INDEX_BEYOND_BOUNDS_SHORT " %d (%d)"
#define PARSERRESOLVE_NO_KEY_OR_INDEX "Did not parse a key or index to resolve."
#define PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT "Could not find earray index for key:"
#define PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_FULL PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT " %s"
#define PARSERRESOLVE_TRAVERSE_NULL_SHORT "Attempted to traverse null substruct:"
#define PARSERRESOLVE_TRAVERSE_NULL_FULL PARSERRESOLVE_TRAVERSE_NULL_SHORT " %s"
#define PARSERRESOLVE_EMPTY_KEY "Found an empty earray key"

#define PARSERRESOLVE_TRAVERSE_IGNORE_SHORT "Attempted to traverse IGNORE field:"
#define PARSERRESOLVE_TRAVERSE_IGNORE_FULL PARSERRESOLVE_TRAVERSE_IGNORE_SHORT " %s"

// one or more root path providers can be registered to handle paths that begin with an identifier.
// providers should set *column to -1 if field is required next: (Mission.Success.Field)
// or to the correct field number if indexing is required next: (Entities[300].Field)
typedef enum RootPathLookupResult
{
	ROOTPATH_UNHANDLED = 0, // We know nothing about this, try again
	ROOTPATH_NOTFOUND = 1, // We know about this, and didn't find the specified object
	ROOTPATH_FOUND = 2, // We found it
} RootPathLookupResult;

typedef int (*RootPathLookupFunc)(const char* name, const char *key, ParseTable** table, void** structptr, int* column);
void ParserRegisterRootPathFunc(RootPathLookupFunc function);
bool ParserRootPathLookup(const char* name, const char *key, ParseTable** table, void** structptr, int* column); // just calls registered functions


/********************************* Indexed Earrays *********************************/

// These are sort functions for sorting earrays of structures

typedef struct ParserSortData
{
	ParseTable *tpi;
	int column;
} ParserSortData;

typedef int (*ParserCompareFieldFunction)(const ParserSortData *context, const void ** a, const void** b);
typedef int (*ParserSearchStringFunction)(ParserSortData *context, const char * key, const void** b);
typedef int (*ParserSearchIntFunction)(ParserSortData *context, const S64 * key, const void** b);

// Gets the appropriate field compare function for the given field
ParserCompareFieldFunction ParserGetCompareFunction(ParseTable tpi[], int column);
ParserSearchStringFunction ParserGetStringSearchFunction(ParseTable tpi[], int column);
ParserSearchIntFunction ParserGetIntSearchFunction(ParseTable tpi[], int column);


/********************************* ParseTable Meta Information *********************************/

// Support functions for fixups and other error results

// Function to avoid double evaluation

__forceinline static TextParserResult MIN_RESULT(TextParserResult x, TextParserResult y)
{
	return MIN(MIN(x,y),PARSERESULT_SUCCESS);
}

#define SET_MIN_RESULT(x,y) ((x) = MIN_RESULT(x,y))
#define SET_INVALID_RESULT(x) MIN1((x),PARSERESULT_INVALID)
#define SET_ERROR_RESULT(x) MIN1((x),PARSERESULT_ERROR)

#define RESULT_GOOD(x) ((x) == PARSERESULT_SUCCESS)

//this is the ParseTable-specific callback that does various types of fixup, and is associated with a specific
//ParseTable by AUTO_STRUCT
//
//pExtraData's type depends on eFixupType. see the comments in enumTextParserFixupType definition
typedef TextParserResult TextParserAutoFixupCB(void *pStruct, enumTextParserFixupType eFixupType, void *pExtraData);

// Recursively fixes up a structure
TextParserResult FixupStructLeafFirst(ParseTable pti[], void *structptr, enumTextParserFixupType eFixupType, void *pExtraData);

// Returns the fixup function for a given ParseTable
TextParserAutoFixupCB *ParserGetTableFixupFunc(ParseTable table[]);

// Manually sets a parse table fixup function
void ParserSetTableFixupFunc(ParseTable pti[], TextParserAutoFixupCB *pFixupCB);

typedef enum SetTableInfoFlags
{
	SETTABLEINFO_NAME_STATIC = 1 << 0,
	SETTABLEINFO_TABLE_IS_TEMPORARY = 1 << 1,
	SETTABLEINFO_ALLOW_CRC_CACHING = 1 << 2,
	SETTABLEINFO_SAVE_ORIGINAL_CASE_FIELD_NAMES = 1 << 3,
} SetTableInfoFlags;

// normally the table info will be inferred correctly, but you can call this function directly before
// other textparser functions in order to set the name and size you want
// YOU MUST call this function if you are defining manual Parse Tables
#define ParserSetTableInfo(table, size, name, pFixupCB, pSourceFileName, eFlags) ParserSetTableInfoEx(table, size, name, pFixupCB, pSourceFileName, eFlags | ((name "Not A Static String Test") ? SETTABLEINFO_NAME_STATIC : 0))
bool ParserSetTableInfoEx(ParseTable table[], int size, const char* name, TextParserAutoFixupCB *pFixupCB, const char *pSourceFileName, SetTableInfoFlags eFlags ); 

// as above, but recurse through subtables
bool ParserSetTableInfoRecurse(ParseTable table[], int size, const char* name, ParseTable* parent, const char *pSourceFileName, StashTable noRecurseTable, SetTableInfoFlags eFlags); 

// throw away table info if any - should only be used for temp loaded tables
bool ParserClearTableInfo(ParseTable table[]); 

//sets a flag so that this tpi can not generally be used for text writing, and if text writing is 
//attempted, the specified string error message will be generated.
void ParserSetTableNotLegalForWriting(ParseTable table[], char *pErrorString);

//used during text writing to check the above flag and maybe generate an error
void CheckForIllegalTableWriting_internal(ParseTable parentTable[], const char *pNameInParent, ParseTable childTable[]);
#define CheckForIllegalTableWriting(parentTable, pNameInParent, childTable) { if (isDevelopmentMode()) CheckForIllegalTableWriting_internal(parentTable, pNameInParent, childTable); }

//appends a given string to a TPI's FormatString, creating it if it doesn't exist.
//
//Note that this generally only should be done to TPIs that have been created by parse table send/receive stuff,
//as it's assuming the formatstring is locally owned
void AppendFormatString(ParseTable *tpi, char *pString);

/*
The first line of auto-generated parsetables contains info about the parsetable. Its format is:
typedef struct ParseTable
{
char* name; //name of the parsetable
StructTypeField type; //TOK_IGNORE | TOK_PARSETABLE_INFO + numColumns (10 bits) << 8 + creationCommentIndex (8 bits) << 18
size_t storeoffset; //struct size
intptr_t param;	//pointer to fixup func		 
void* subtable; //pointer to TextParserInfo struct
StructFormatField format; // keyColumn(16 bits) | typeColumn(15 bits) << 16 | can-do-memcpy(bit) << 31 
} ParseTable;*/

// Returns if a TPI has a fast access info column
#define TPIHasTPIInfoColumn(table) ((table)[0].type & TOK_PARSETABLE_INFO)

// Returns the actual ParseTableInfo
#define ParserGetTableInfo(table)	\
	(TPIHasTPIInfoColumn(table) && table[0].subtable) ? table[0].subtable : ParserGetTableInfoInternal(table)
ParseTableInfo *ParserGetTableInfoInternal(ParseTable table[]);

// Tells a parse table to associate with a given memory pool

//note that we are setting a pointer to a pointer here, effectively a handle,
//so we can set that a TPI uses a memory pool before that memory pool has been created
void ParserSetTPIUsesSingleThreadedMemPool(ParseTable table[], MemoryPool *pPool);
MemoryPool ParserGetTPISingleThreadedMemPool(ParseTable *pTable);
void ParserSetTPIUsesThreadSafeMemPool(ParseTable table[], ThreadSafeMemoryPool *pPool);
ThreadSafeMemoryPool *ParserGetTPIThreadSafeMemPool(ParseTable *pTable);

// Disables memory tracking for a parsetable
void ParserSetTPINoMemTracking(ParseTable table[]);

//goes through a parse table for a made-up struct type and comes up with reasonable offset values, returns
//total size. Don't use this unless you know what you're doing
int ParseInfoCalcOffsets(ParseTable table[], bool bNoRecurse);

// If it can be copied fast, return the size
int ParserCanTpiBeCopiedFast(ParseTable table[]);

// Returns the source file the ParseTable is from
const char *ParserGetTableSourceFileName(ParseTable table[]);

// Type of a text reformat callback
typedef void TextParserReformattingCallback(char *pInString, char **ppOutString, const char *pFileName);

//returns the text reformatting callback (used to reformat text immediately after it's loaded
//from a text file right before it's parsed)
TextParserReformattingCallback *ParserGetReformattingCallback(ParseTable *tpi);

// Sets the reformatting callback for a given parse table
void ParserSetReformattingCallback(ParseTable table[], TextParserReformattingCallback *pReformattingCB);

//gets a fairly-human-readable name for a field's type (ie, "U8"). Pass in NULL for modifiedType
//unless you really really know what you're doing. Which I pretty much guarantee you don't.
char* ParseInfoGetType(ParseTable tpi[], int column, StructTypeField* modifiedtype);

/********************************* ParseTable IO *********************************/

// These functions handle the saving/loading of ParseTables themselves

//there are different "send types" for ParseTableXxx functions, each with different potential behaviors
typedef enum enumParseTableSendType
{
	//assert on things that won't work for schemas
	PARSETABLESENDFLAG_FOR_SCHEMAS = 1 << 0,

	//the parsetable is going to be used to HTTP display
	PARSETABLESENDFLAG_FOR_HTTP_INTERNAL = 1 << 1,

	//the parsetable is going to be printed as an API member
	PARSETABLESENDFLAG_FOR_API = 1 << 2,

	PARSETABLESENDFLAG_FOR_SQL = 1 << 3,

	//back in the old days, TOK_BIT was always converted to U8. That's still the default behavior to
	//avoid breaking things. But we now support bitfields of size other than 1 bit. So we provide two options:
	//
	//bitfields maintain their original size, but get spread out and take up more space. Use this if you're
	//going to be using your TPI to parserSend and parserRecv
	PARSETABLESENDFLAG_MAINTAIN_BITFIELDS = 1 << 4,
	
	//bitfields get converted to ints that are large enough to store any possible value (so 12 bit
	//bitfields get converted to U16s, etc). Use this if you're going to be reading/writing and might
	//have larget bitfields
	PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS = 1 << 5,
} enumParseTableSendType;

#define PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL  (PARSETABLESENDFLAG_FOR_HTTP_INTERNAL | PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS)

void ParseTableFree(ParseTable*** eapti);

bool ParseTableWriteTextFile(const char* filename, ParseTable pti[], const char* name, enumParseTableSendType eSendType);		// returns success

// returns an erray of parse tables and size of root struct (root struct is the first TPI in the array, ie, the
// one that was passed to WriteTextFile.)
bool ParseTableReadTextFile(const char* filename, ParseTable*** eapti, int* size, const char** name, enumParseTableSendType eSendType); 

bool ParseTableReadText(const char *pText, ParseTable*** eapti, int* size, const char** name, enumParseTableSendType eSendType);
bool ParseTableWriteText_dbg(char **ppEString, ParseTable pti[], const char* name, enumParseTableSendType eSendType, const char *caller_fname, int line);
#define ParseTableWriteText(ppEString, pti, name, eSendType) ParseTableWriteText_dbg(ppEString, pti, name, eSendType, __FILE__, __LINE__)

bool ParseTableSend(Packet* pak, ParseTable pti[], const char* name, enumParseTableSendType eSendType);
bool ParseTableRecv(Packet* pak, ParseTable*** eapti, int* size, const char** name, enumParseTableSendType eSendType);

#define PARSER_WRITE_SQL_SUBSTRUCT 1 << 2

char * ParserWriteSQL_dbg(char **ppEString, ParseTable pti[], void *strptr, U64 *ids, U32 opts, const char *caller_fname, int line);
#define ParserWriteSQL(ppEString, pti, strptr, ids, opts) ParserWriteSQL_dbg(ppEString, pti, strptr, ids, opts, __FILE__, __LINE__)

bool ParseTableWriteSQL_dbg(char **ppEString, ParseTable pti[], const char* name, const char *caller_fname, int line);
#define ParseTableWriteSQL(ppEString, pti, name) ParseTableWriteSQL_dbg(ppEString, pti, name, __FILE__, __LINE__)
/********************************* ParseTableGraph *********************************/

/*A parseTableGraph is a hierarchical, and potentially recursive, graph of a parse table and all the other
parsetables that it includes, which could potentially include it. It's useful for things where you need to 
process a parsetable and all its descendents without worrying about recursion. (For instance, if parse table
A has parse table B as a substruct and vice versa, things can get tricky */

typedef struct ParseTableGraphHandle ParseTableGraphHandle;

typedef struct ParseTableGraphHandle
{
	ParseTable *pTable;
	bool bIsPolyTable;
	ParseTableGraphHandle **ppParents;
	ParseTableGraphHandle **ppChildren;
	void *pUserData;
	int iUserData;
} ParseTableGraphHandle;

typedef struct ParseTableGraph
{
	ParseTableGraphHandle **ppHandles; //ppHandles[0] will always be pRoot
	StashTable handleTable; //if look up parsetables to find the associated ParseTableGraphHandle;
} ParseTableGraph;


ParseTableGraph *MakeParseTableGraph(ParseTable *pRoot, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
void DestroyParseTableGraph(ParseTableGraph *pGraph);


/********************************* Optimized ParseTables *********************************/

//takes a parsetable and makes an "optimized" version of it, one which has all non-relevant fields stripped out
//
//This is useful for optimizing repeated big diffs or sends because all nonnecessary stepping over the parsetable
//will be eliminated. BE VERY CAREFUL USING THIS FUNCTION. TALK TO ALEX.

#define FILE FileWrapper

typedef enum
{
	PTOPT_FLAG_NO_START_AND_END = 1,
} enumParseTableOptimizationFlags;

ParseTable *ParserMakeOptimizedParseTable(ParseTable pti[], StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
	enumParseTableOptimizationFlags eOptionFlags);

//while doing a .bin write, we can optionally create a "layout file", with information about what is in the .bin file and where,
//useful for tracking down various .bin issues. This helper functions writes a standardized tag into the layout file
void TagLayoutFile_internal(FILE *pLayoutFile, SimpleBufHandle outFile, char *fmt, ...);
#define TagLayoutFile(pLayoutFile, outFile, fmt, ...) { if (pLayoutFile) TagLayoutFile_internal(pLayoutFile, outFile, fmt, ##__VA_ARGS__); }

void LayoutFile_BeginBlock(FILE *pLayoutFile);
void LayoutFile_EndBlock(FILE *pLayoutFile);

//forces use of a particular offset
void TagLayoutFileOffset_internal(FILE *pLayoutFile, SimpleBufHandle outFile, int iOffset, char *fmt, ...);
#define TagLayoutFileOffset(pLayoutFile, outFile, iOffset, fmt, ...) { if (pLayoutFile) TagLayoutFileOffset_internal(pLayoutFile, outFile, iOffset, fmt, ##__VA_ARGS__); }

#define TagLayoutFileBegin(pLayoutFile, outFile, fmt, ...) { if (pLayoutFile) { TagLayoutFile_internal(pLayoutFile, outFile, fmt, ##__VA_ARGS__); LayoutFile_BeginBlock(pLayoutFile); } }
#define TagLayoutFileEnd(pLayoutFile, outFile, fmt, ...) { if (pLayoutFile) { LayoutFile_EndBlock(pLayoutFile); TagLayoutFile_internal(pLayoutFile, outFile, fmt, ##__VA_ARGS__); } }


/********************************* Misc *********************************/


//for a struct in shared memory, verifies that all pointers inside that struct are pointers to shared memory,
//recurses
int CheckSharedMemory(ParseTable pti[], void *structptr);

//given a redundant field, find the main field that it's a redundant copy of
int ParseInfoFindAliasedField(ParseTable pti[], int column);

//memory allocated for "temporary TPIs", which is generally hybrid objects or 
//TPIs sent via network, get assigned to this memorypool
#define TEMPORARYTPI_MEMORY_NAME "TemporaryTPI"

//because we want no textparser mallocs to have textparser-y filenames, we need some fake filenames
//to represent different systems
#define FAKEFILENAME_PARSETABLEINFOS "ff_ParseTableInfos"
#define FAKEFILENAME_COMPILEDOBJPATHS "ff_CompiledObjectPaths"
#define FAKEFILENAME_TESTTEXTPARSER "ff_TestTextParser"


__forceinline static const char* strchr_fast(const char* str, char c)
{
	while(*str && *str != c)
		str++;
	if(*str == c)
		return str;
	return NULL;
}

__forceinline static char* strchr_fast_nonconst(char* str, char c)
{
	while(*str && *str != c)
		str++;
	if(*str == c)
		return str;
	return NULL;
}

ParseTable *RecursivelyFindNamedChildTable(ParseTable pti[], char *pChildName);

//Checks if any field in this ParseTable or any child tables contains any references
bool ParseTableContainsRefs(ParseTable pti[], char*** eaFieldNames);
//This will clear and destroy and refs contained in the struct. Call this in the main thread
//before you free a struct from another thread
void ParserClearAndDestroyRefs(ParseTable pti[], void* structptr);

extern StashTable gParseTablesByName;


//stuff relating to FIXUPTYPE_HERE_IS_IGNORED_FIELD

//passed in as data to FIXUPTYPE_HERE_IS_IGNORED_FIELD
AUTO_STRUCT;
typedef struct SpecialIgnoredField
{
	const char *pFieldName; AST(POOL_STRING)
	char *pString; AST(ESTRING)
} SpecialIgnoredField;

//calling this on a TPI before text reading (but only text reading) will say "for a given AST_IGNORE field, I want you to
//internally remember the string you ignore (if any) and then, after POST_TEXT_READ_FIXUP, pass it in as a FIXUPTYPE_HERE_IS_IGNORED_FIELD
void ParserSetWantSpecialIgnoredFieldCallbacks(ParseTable pti[], const char *pFieldName);


int ParserGetDefaultFieldColumn(ParseTable tpi[]);

// Switch from having one compiledObjectPath cache to having one per thread. Only used by the ObjectDB.
void EnableTLSCompiledObjectPaths(bool enable);


//if the parse table has AST_SAVE_ORIGINAL_CASE_OF_FIELD_NAMES, then there's an earray in its ParseTableInfo of the
//names of the fields with their original case. This returns that earray, or NULL if there isn't one
char **ParserGetOriginalCaseFieldNames(ParseTable tpi[]);


C_DECLARATIONS_END

#endif //TEXT_PARSER_UTILS_H
