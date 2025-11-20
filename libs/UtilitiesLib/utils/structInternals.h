#ifndef STRUCTINTERNALS_H
#define STRUCTINTERNALS_H
#pragma once
GCC_SYSTEM

#include "file.h" // need weird FILE defines
#include "textparserUtils.h" // need DynOp enum, but can't forward declare enums
#include "textparser.h" // NUM_TOK_TYPE_TOKENS constant
#include "multival.h" // MultiValType
#include "StructCopy.h"

typedef struct Packet Packet;
typedef struct MultiVal MultiVal;
typedef struct PackedStructStream PackedStructStream;
typedef struct ThreadSafeMemoryPool ThreadSafeMemoryPool;
typedef struct Recv2TpiCachedInfo Recv2TpiCachedInfo;
typedef struct StructInitInfo StructInitInfo;

typedef enum WriteJsonFlags WriteJsonFlags;

/////////////////////////////////////////////////////////////////////////////////////////
// ParseTableInfo - meta information about parse tables

typedef enum
{
	TABLECHILDTYPE_UNKNOWN,
	TABLECHILDTYPE_PARENT,
	TABLECHILDTYPE_SIMPLECHILD, //child of only a single parent, parent is PARENT or SIMPLECHILD
	TABLECHILDTYPE_COMPLEXCHILD, //child of more than one parent, recursively
} enumParseTableChildType;

//recursively calculatd for each parse table and all of its children
typedef enum
{
	TABLEHASDIRTYBIT_UNKNOWN,
	TABLEHASDIRTYBIT_NO,
	TABLEHASDIRTYBIT_YES,
} enumParseTableHasDirtyBit;

//to optimize eaIndexed operations, we cache information about how to do indexed compares
//by saving the compare type and an offset. This also allows us to do nifty tricks like
//treating ContainerRefs as S64s which is a huge win


typedef enum enumIndexCompareType
{
	INDEXCOMPARETYPE_NONE,
	INDEXCOMPARETYPE_INT8,
	INDEXCOMPARETYPE_INT16,
	INDEXCOMPARETYPE_INT32,
	INDEXCOMPARETYPE_INT64,

	INDEXCOMPARETYPE_STRING_EMBEDDED,
	INDEXCOMPARETYPE_STRING_POINTER,
	INDEXCOMPARETYPE_REF,

	INDEXCOMPARETYPE_STRUCT_EMBEDDED,
	INDEXCOMPARETYPE_STRUCT_POINTER,
} enumIndexCompareType;

#define INDEXCOMPARETYPE_IS_SET_AND_NOT_INT(eType) (eType > INDEXCOMPARETYPE_INT64)
#define INDEXCOMPARETYPE_IS_STRING(eType) (eType >= INDEXCOMPARETYPE_STRING_EMBEDDED && eType <= INDEXCOMPARETYPE_REF)



typedef struct ParseTableInfo_IndexedCompareCache
{
	enumIndexCompareType eCompareType; //if anything other than INDEXCOMPARETYPE_NONE, we can use our cached info for optimized
		//searching

	int storeoffset; //offset into the struct where we find the indexing data
	ParseTable *pTPIForStructComparing; //for struct comparing we need a tpi also

	//we also use this cache for quick string-key-extraction
	bool bCanGetStringKeysFast;
	StaticDefineInt *pStaticDefineForEnums;
} ParseTableInfo_IndexedCompareCache;

typedef struct ParseTableInfo 
{
	// basic info about the table
	ParseTable* table;
	int size;					// may be empty if table info wasn't set
	const char* name;			// may be empty if table info wasn't set.  Static or allocAddString()'d.

	// some cached information
	int numcolumns;
	int keycolumn;
	int typecolumn;
	int iDefaultFieldColumn;

	U32 bHasDirtyBit:1;
	U32 bTPIIsTemporary:1;
	U32 bNoMemoryTracking;
	int iDirtyBitOffset;

	ParseTable** tableinfo_parents;		// earray of parents
	enumParseTableChildType eMyChildType; //what type of child I am, to determine how I should be visually graphed

	TextParserAutoFixupCB *pFixupCB; //my fixup function

	const char *pSourceFileName; //file in which the struct is defined... NOT freed when the PTI is released because it's only 
	//used for auto-fixuped TPIs so is a static string

	PERFINFO_TYPE*		piCreate;
	PERFINFO_TYPE*		piSend;
	PERFINFO_TYPE*		piRecv;
	PERFINFO_TYPE*		piCopy;
	PERFINFO_TYPE*		piDestroy;
	PERFINFO_TYPE**		piChildRecv;
	PERFINFO_TYPE**		piChildSend;
	PERFINFO_TYPE*		piStructCompare;
	PERFINFO_TYPE*		piReadTokenizer;
#ifdef TOKENSTORE_DETAILED_TIMERS
	PERFINFO_TYPE*		piEADestroy;
	PERFINFO_TYPE*		piStructDestroy;
	PERFINFO_TYPE*		piRefClear;
#endif

	//each ParseTableInformation tracks how its parsetable should get copied. The two most common types of
	//copying are copy-with-NOASTs and copy-without-NOASTs. All other possibilities (ie, other sets of copyflags
	//and include-exclude flags) are thus handled with a stash table
	StructCopyInformation *pCopyWithNOASTs;
	StructCopyInformation *pCopyWithoutNOASTs;
	StashTable copyWithFlagsTable;
	enumParseTableHasDirtyBit eChildrenHaveDirtyBit;

	//if this is set, then all creations/destructions of objects of this type will use this memory pool
	//(note that this is a pointer to a pointer)
	MemoryPool *pSingleThreadedMemPool;
	ThreadSafeMemoryPool *pThreadSafeMemPool;

	//extra mallocs associated with this tpi, should be freed when/if this is freed
	void **ppExtraAllocations;

	//if set, this struct can not be legally used for string writing, and this error message should be displayed
	char *pNoStringWritingError;

	//when reading from text files, first load the entire file in, then call this callback on it
	TextParserReformattingCallback *pReformattingCB;

	//cached info for receiving parse tables sent with other TPIs (typically send from the object DB with
	//the schema rather than the actual TPI)
	Recv2TpiCachedInfo **ppRecv2TpiCachedInfos;

	//for optimized structInit
	StructInitInfo *pStructInitInfo;
	StructInitInfo *pStructDeInitInfo;

	int iResourceDBResType;

	int iCRCValidationCookie; //the global cookie when this crc was set, if this has changed
		//the cached crcs are all invalid

	StashTable sCachedCRCsByFlags;
	

	//if true, then CheckIfTableIsValidForWriting has already been called for this tpi
	U32 bDidWritingValidityCheck : 1;
	U32 bMyParentsAreInvalidForWriting : 1;
	U32 bMyParentsAreInvalidForWriting_HasBeenSet : 1;
	U32 bAllowCRCCaching : 1;

	int iLongestColumnNameLength;
	int *piColumnNameLengths; //ea32

	//if AST_SAVE_ORIGINAL_CASE_OF_FIELD_NAMES is set, this is an earray of the original field names for the
	//struct, before they got allocAddStringed and canonical cased, etc.
	char **ppOriginalCaseFieldNames;

	ParseTableInfo_IndexedCompareCache IndexedCompareCache;
} ParseTableInfo;



// these are for getting info on fields in ParseTable that change depending on
// token type.  See interpretfield_f().
typedef enum ParseInfoField {
	ParamField,
	SubtableField,
} ParseInfoField;

typedef enum ParseInfoFieldUsage {
	// correct response for either field
	NoFieldUsage = 0,

	// param field
	SizeOfSubstruct,
	NumberOfElements,
	DefaultValue,
	EmbeddedStringLength,
	PointerToDefaultString,
	OffsetOfSizeField,
	SizeOfRawField,
	BitOffset,
	PointerToCommandString,
	
	// subtable field
	StaticDefineList,
	PointerToSubtable,
	PointerToDictionaryName,
} ParseInfoFieldUsage;
// IF ADDING ANYTHING HERE, YOU MUST MODIFY ParseInfoToTable and ParseInfoFromTable TO MATCH


////////////////////////////////////////////////////////// Function prototypes for array and token handlers



typedef void (*preparse_f)(ParseTable tpi[], int column, void* structptr, TokenizerHandle tok);

AUTO_TP_FUNC_OPT;
typedef int (*parse_f)(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult);

//foo_WriteText returns true if there was something meaningful there to write, false otherwise. This is generally only useful when
//showname is false for simple fields, because then something is always written out, even if it's the default, and the caller
//might want to know whether something "Real" was written or not. This is used by the structparam writing code, which writes
//foo 0 0 0 0 1
//or something like that, and doesn't want to be writing trailing zeros, but as it writes each zero, doesn't know whether it's needed 
//or not, and is eventually going to unspool.
AUTO_TP_FUNC_OPT;
typedef bool (*writetext_f)(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
AUTO_TP_FUNC_OPT;
typedef TextParserResult (*writebin_f)(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
AUTO_TP_FUNC_OPT;
typedef TextParserResult (*readbin_f)(SimpleBufHandle file, FileList *pFile, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

//initStruct returns TRUE if it must always be called on all structs, FALSE if a memcpy accomplishes the same thing
AUTO_TP_FUNC_OPT;
typedef bool (*initstruct_f)(ParseTable tpi[], int column, void* structptr, int index);

//destroystruct returns one of these so that the StructDeInit caching can
//optimally skip fields that don't actually need to be destroyed
enum
{
	DESTROY_NEVER,
	DESTROY_ALWAYS,
	DESTROY_IF_NON_NULL,
};

AUTO_TP_FUNC_OPT;
typedef int (*destroystruct_f)(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index);
AUTO_TP_FUNC_OPT;
typedef void (*updatecrc_f)(ParseTable tpi[], int column, void* structptr, int index);
AUTO_TP_FUNC_OPT;
typedef int (*compare_f)(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
AUTO_TP_FUNC_OPT;
typedef size_t (*memusage_f)(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage);
AUTO_TP_FUNC_OPT;
typedef void (*copystruct_f)(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData);
AUTO_TP_FUNC_OPT;
typedef void (*copyfield_f)(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
AUTO_TP_FUNC_OPT;
typedef enumCopy2TpiResult (*copyfield2tpis_f)(ParseTable tpi[], int column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest,  CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString);
AUTO_TP_FUNC_OPT;
typedef void (*writehdiff_f)(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char *prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line);


//what does invertExcludeFlags do you might wonder? Look for TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT
//and see the comment there
AUTO_TP_FUNC_OPT;
typedef void (*writebdiff_f)(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line);

//if SENDDIFF_FLAG_FASTCOMPARES and SENDDIFF_FLAG_ALLOWDIFFS are both set, then the return value indicates whether anything was sent
AUTO_TP_FUNC_OPT;
typedef bool (*senddiff_f)(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB);
AUTO_TP_FUNC_OPT;
typedef bool (*recvdiff_f)(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags); // must deal with structptr == NULL by ignoring data
AUTO_TP_FUNC_OPT;
typedef bool (*bitpack_f)(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack);
AUTO_TP_FUNC_OPT;
typedef void (*unbitpack_f)(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack);
AUTO_TP_FUNC_OPT;
typedef void (*endianswap_f)(ParseTable tpi[], int column, void* structptr, int index);
AUTO_TP_FUNC_OPT;
typedef void (*interp_f)(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam);
AUTO_TP_FUNC_OPT;
typedef void (*calcrate_f)(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime);
AUTO_TP_FUNC_OPT;
typedef void (*integrate_f)(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime);
AUTO_TP_FUNC_OPT;
typedef void (*calccyclic_f)(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime);
AUTO_TP_FUNC_OPT;
typedef void (*applydynop_f)(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed);
AUTO_TP_FUNC_OPT;
typedef TextParserResult (*writestring_f)(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line);
AUTO_TP_FUNC_OPT;
typedef TextParserResult (*readstring_f)(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags);
AUTO_TP_FUNC_OPT;
typedef TextParserResult (*tomulti_f)(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL);
AUTO_TP_FUNC_OPT;
typedef TextParserResult (*frommulti_f)(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value);
AUTO_TP_FUNC_OPT;
typedef void (*calcoffset_f)(ParseTable tpi[], int column, size_t* size);
typedef void (*endtable_f)(F32 f, int i, void* p);
AUTO_TP_FUNC_OPT;
typedef void (*preparesharedmemoryforfixup_f)(ParseTable tpi[], int column, void *structptr, int index, char **ppFixupData);
AUTO_TP_FUNC_OPT;
typedef void (*fixupsharedmemory_f)(ParseTable tpi[], int column, void *structptr, int index, char **ppFixupData);
AUTO_TP_FUNC_OPT;
typedef TextParserResult (*leafFirstFixup_f)(ParseTable tpi[], int column, void *structptr, int index, enumTextParserFixupType eFixupType, void *pExtraData);
AUTO_TP_FUNC_OPT;
typedef void (*reapplypreparse_f)(ParseTable tpi[], int column, void *structptr, int index, char *pCurrentFile, int iTimeStamp, int iLineNum);
AUTO_TP_FUNC_OPT;
typedef int (*checksharedmemory_f)(ParseTable tpi[], int column, void *structptr, int index);

//special function that only array types have which attempts to copy two non-type-matching fields by converting a member to a string and then back
AUTO_TP_FUNC_OPT;
typedef enumCopy2TpiResult (*stringifycopy_f)(ParseTable tpi[], int column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, char **ppResultString);
AUTO_TP_FUNC_OPT;
typedef int (*recvdiff2tpis_f)(Packet *pak, ParseTable tpi[], ParseTable dest_tpi[], int column, int index, Recv2TpiCachedInfo *pCache, void *data);

//new text diff writing scheme, replacing writehdiff. One function does comparing and diff writing, and also
//manages the compare-with-NULL and compare-with-something cases efficiently
AUTO_TP_FUNC_OPT;
typedef int (*textdiffwithnull_f)(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line);



//returns true if it wrote at least one "real" character
AUTO_TP_FUNC_OPT;
typedef bool (*writehtmlfile_f)(ParseTable tpi[], int column, void *structptr, int index, FILE* out, WriteHTMLContext *pContext);

#define TPXML_FORMAT_MASK 0xFF
#define TPXML_FORMAT(flags) (flags & TPXML_FORMAT_MASK)
#define TPXML_FORMAT_DEFAULT	0

//for returning xmlrpc strings.
#define TPXML_FORMAT_NONE		(1 << 0)
#define TPXML_FORMAT_XMLRPC		(1 << 1)

//not implemented.
#define TPXML_FORMAT_JSON		(1 << 2)

//*** Bits 8 through 15 are reserved for internal formatting operations ***

//use tags like <this/>
#define TPXML_USE_SINGLE_ELEMENTS (1 << 16)

//not implemented
#define TPXML_OUTPUT_DTD (1 << 17)

// Suppress pretty-printing of the XML output to make it more compact.
#define TPXML_NO_PRETTY (1 << 18)

//XXXXXX: Don't forget to update the Forward Declares later in this file!

AUTO_TP_FUNC_OPT;
typedef bool (*writexmlfile_f) (ParseTable tpi[], int column, void *structptr, int index, int level, FILE* out, StructFormatField iOptions);
//call back definitions go in structInternals.c

//stuff relating to the new StructCopy code
typedef void (*structCopyQuery_f)(ParseTable *tpi, int column, int iIndex, int iOffsetIntoParentStruct, 
	CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, void *pUserData);
typedef void (*newCopyField_f)(ParseTable tpi[], int column, void* dest, void* src, int index, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
typedef void (*newCopyEarray_f)(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

//stuff relating to the ClearSilver HDF writer
AUTO_TP_FUNC_OPT;
typedef bool (*writehdf_f)(ParseTable tpi[], int column, void *structptr, int index, HDF *hdf, char *name_override);

AUTO_TP_FUNC_OPT;
typedef bool (*writejsonfile_f)(ParseTable tpi[], int column, void *structptr, int index, FILE *out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

typedef ParseInfoFieldUsage (*interpretfield_f)(ParseTable tpi[], int column, ParseInfoField field);


/////////////////////////////////////////////////////////// Token base types
// g_infotable provides handler functions for each type of token,
// generally intended to by used by the array handler functions below
typedef struct TokenTypeInfo 
{
	U32				type;
	U32				storage_compatibility; //what types are legal, ie, TOK_STORAGE_DIRECT_SINGLE
	char*			name_direct_single;			// REQUIRED, rest of names are optional
	char*			name_direct_fixedarray;
	char*			name_direct_earray;
	char*			name_indirect_single;
	char*			name_indirect_fixedarray;
	char*			name_indirect_earray;
	MultiValType	eMultiValType;				// if this type directly corresponds to a Multival type, what type that is
												// (used by objectPathToMultiValType)

	interpretfield_f interpretfield;			// REQUIRED, use ignore_interpretfield if not needed
	initstruct_f	initstruct;
	destroystruct_f destroystruct_func;
	preparse_f		preparse;
	parse_f			parse_func;
	writetext_f		writetext;
	writebin_f		writebin;
	readbin_f		readbin;
	writehdiff_f	writehdiff;
	writebdiff_f	writebdiff;
	senddiff_f		senddiff;
	recvdiff_f		recvdiff;
	bitpack_f		bitpack;
	unbitpack_f		unbitpack;
	writestring_f		writestring;
	readstring_f	readstring;
	tomulti_f		tomulti;
	frommulti_f		frommulti;
	updatecrc_f		updatecrc;
	compare_f		compare;
	memusage_f		memusage;
	calcoffset_f	calcoffset;
	copystruct_f	copystruct_func;
	copyfield_f		copyfield_func;
	copyfield2tpis_f copyfield2tpis;
	endianswap_f	endianswap;
	interp_f		interp;
	calcrate_f		calcrate;
	integrate_f		integrate;
	calccyclic_f	calccyclic;
	applydynop_f	applydynop;
	preparesharedmemoryforfixup_f preparesharedmemoryforfixup;
	fixupsharedmemory_f fixupsharedmemory;
	leafFirstFixup_f leafFirstFixup;
	reapplypreparse_f reapplypreparse;
	checksharedmemory_f checksharedmemory;
	writehtmlfile_f writehtmlfile;
	writexmlfile_f	writexmlfile;
	writejsonfile_f	writejsonfile;
	structCopyQuery_f queryCopyStruct;
	newCopyField_f newFieldCopy;
	newCopyEarray_f earrayCopy;
	writehdf_f writehdf;
	textdiffwithnull_f textdiffwithnull;

	endtable_f		endtable;
} TokenTypeInfo;
extern TokenTypeInfo g_tokentable[NUM_TOK_TYPE_TOKENS];




// there is no guarantee that a particular token type will have a particular handler, generally
// you should use TOKARRAY_INFO instead
#define TYPE_INFO(type) (g_tokentable[TOK_GET_TYPE(type) < NUM_TOK_TYPE_TOKENS ? TOK_GET_TYPE(type) : 0])

// these are some sanity checks on parse tables in case users try to get crazy with token types
void TestParseTable(ParseTable pti[]);
#define TEST_PARSE_TABLE(pti) (isDevelopmentMode()? TestParseTable(pti): (void)0)

////////////////////////////////////////////////////////// FileListobj
// FileList is used by the parser to keep a record of files and correct
// file dates in the .bin files.  They are maintained as a 
// sorted EArray of file names and dates


#define FILELIST_SIG "Files1"
#define FILELIST_CHECKSUM_BIT (0x80000000)
// how could my work be done without creating another file information structure?

AUTO_STRUCT;
typedef struct FileEntry {
	const char *path;								AST(NAME(Path) FILENAME POOL_STRING) // allocAddString
	__time32_t date;								AST(NAME(Date))
	int seen;										NO_AST // just used for compare ops
} FileEntry;
extern ParseTable parse_FileEntry[];
#define TYPE_parse_FileEntry FileEntry

typedef FileEntry** FileList;
typedef void (*FileListCallback)(const char* path, __time32_t date);

#define FileListCreate(list) eaCreate(list)
#define FileListClear(list) eaClearEx(list, NULL)
#define FileListDestroy(list) eaDestroyEx(list, NULL)
#define FileListLength(list) eaSize(list)

void FileListInsertChecksum(FileList* list, const char* path, U32 checksum); // ok to pass 0 for checksum
void FileListInsertInternal(FileList* list, const char* path, U32 date_or_checksum); // ok to pass 0 for date
void FileListInsert(FileList* list, const char* path, __time32_t date); // ok to pass 0 for date
SA_RET_VALID int FileListRead(FileList* list, SimpleBufHandle file); // returns success
int FileListWrite(FileList* list, SimpleBufHandle file, FILE *pLayoutFile, char *pComment); // returns success
int FileListIsBinUpToDate(FileList* binlist, FileList *disklist); // Returns 1 if files in binlist are at least as new as those in disklist.  Returns 0 if files are in one list but not the other.
void FileListForEach(FileList *list, FileListCallback callback);
FileEntry* FileListFind(FileList* list, const char* path);

// doesn't sanitize slashes
FileEntry* FileListFindFast(FileList* list, const char* path);

//for each file in the file list, "touches" it so that a reload will be triggered shortly
void FileListForceReloadAll(FileList *list);

//returns -1 if this filename is not found in the list
int FileListFindIndex(FileList* list, const char* path);

const char *FileListGetFromIndex(FileList *list, int iIndex);

//returns 0 if there are no files in the list
U32 FileListGetMostRecentTimeSS2000(FileList *list);

// Returns true if all files in file list are up to date
bool FileListAllFilesUpToDate(FileList *binlist);

//when writing currentfiles into bin files, we write 0 if it's NULL, 1 if it's not in the filelist, index + 2 if it isin the file list
#define FILELIST_SPECIALINDEX_NULL 0
#define FILELIST_SPECIALINDEX_NOTINLIST 1
#define FILELIST_INDEX_OFFSET 2

////////////////////////////////////////////////////////// DependencyList
// DependencyList is used by the parser to keep a record of non-file dependencies
// for bin files. They consist of a type, a name, and a CRC or integer value

#define DEPENDENCYLIST_SIG "Depen1"

typedef struct DependencyEntry {
	DependencyType type;
	const char *name; // allocAddString
	U32 value;
	int seen;			// just used for compare ops
} DependencyEntry;

typedef DependencyEntry** DependencyList;

#define DependencyListCreate(list) eaCreate(list)
#define DependencyListClear(list) eaClearEx(list, NULL)
#define DependencyListDestroy(list) eaDestroyEx(list, NULL)
#define DependencyListLength(list) eaSize(list)

void DependencyListInsert(DependencyList* list, DependencyType type, const char* path, U32 value);
// returns success
int DependencyListRead(DependencyList* list, SimpleBufHandle file); 
// returns success
int DependencyListWrite(DependencyList* list, SimpleBufHandle file, FILE *pLayoutFile); 
// Returns 1 if dependencies in binlist are the same as the current ones. Extra dependencies in binlist are ignored

DependencyEntry* DependencyListFind(DependencyList* list, DependencyType type, const char* path);

//returns true if every dependency in the second list also exists in the first list, comparing type and
//name only, not value
bool DependencyListIsSuperSetOfOtherList(DependencyList *pLarger, DependencyList *pSmaller);


typedef struct ParseTableInfo ParseTableInfo;

extern ParseTable PARSER_RAW_MEMORY[];

// Functions used by internal textparser code, that should NOT be called publicly
void*	StructAllocRaw_dbg(size_t size, const char* callerName, int line);	// allocates memory in given size
#define StructAllocRaw(size) StructAllocRaw_dbg(size,__FILE__,__LINE__)
void *StructAllocRawCharged_dbg(size_t size, ParseTable *pti, const char* callerName, int line);
#define StructAllocRawCharged(size, tpi) StructAllocRawCharged_dbg(size, tpi, __FILE__, __LINE__);

void	_StructFree_internal(ParseTable tpi[], SA_PRE_OP_VALID SA_POST_P_FREE void* structptr);	// DO NOT CALL THIS

void	StructFreeFunctionCall(StructFunctionCall* callstruct);

// Counts how much memory is used to store all of a structure's members, but NOT the structure itself
size_t StructGetMemoryUsage(ParseTable pti[], const void* pSrcStruct, bool bAbsoluteUsage);

//if bMultiplePasses is set, that means that this struct is being read in multiple passes, presumably to repeatedly
//fill in an EArray or something. Thus, it should not do the FIXUP_POST_TEXT_READ callback. This means that whoever
//called it will need to do that callback.
void ParserReadTokenizer(TokenizerHandle tok, ParseTable pti[], void* structptr, bool bMultiplePasses, TextParserResult *parseResult);

//this function is used by the textparser inheritance stuff so that after a struct has been loaded in and then
//had lots of inheritance stuff done to it, the "preparse" stuff, that is, the stuff that normally is automatically
//filled in from the tokenizer will loading from text files, can be recursively reapplied to it.
void ReApplyPreParseToStruct(ParseTable pti[], void *structptr, char *pCurrentFile, int iTimeStamp, int iLineNum);

// Functions used by textparsercallbacks.c that live in textparser.c
bool InnerWriteTextFile(FILE* out, ParseTable pti[], void* structptr, int level, bool ignoreInherited, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
StructFunctionCall* ParserCompressFunctionCall(StructFunctionCall* src, CustomMemoryAllocator memAllocator, void* customData, ParseTable tpi[]);
size_t ParserGetFunctionCallMemoryUsage(StructFunctionCall* callstruct, bool bAbsoluteUsage);

//special comment just for Tom: As you might suspect from a function which lives in a file called structINTERNALs.h, you
//generally SHOULD NOT CALL THESE FUNCTIONS unless you know what you are doing. Call ParserOpenReadBinaryFile/ParserWriteBinaryFile,
//or else talk to Alex or Jimb.
TextParserResult ParserReadBinaryTable(SimpleBufHandle file, FileList *pFileList, ParseTable pti[], void* structptr, int* sum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); // returns success
TextParserResult ParserReadBinaryTableEx(SimpleBufHandle file, FileList *pFileList, ParseTable pti[], void* structptr, int* sum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); // returns success
bool ParserWriteBinaryTable(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable pti[], void* structptr, int* sum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); // returns success

void ParserUpdateCRCFunctionCall(StructFunctionCall* callstruct);
TextParserResult ReadBinaryFunctionCalls(SimpleBufHandle file, StructFunctionCall*** structarray, int* sum, ParseTable tpi[]);
bool RecvDiffFunctionCalls(Packet* pak, StructFunctionCall*** structarray, ParseTable tpi[], enumRecvDiffFlags eFlags);
void SendDiffFunctionCalls(Packet* pak, StructFunctionCall** structarray);
void StructUpdateCRC(ParseTable pti[], void* structptr);
void StructWriteTextDiffInternal(char **estr, ParseTable tpi[], void *oldp, void *newp, char *prefix, 
								 StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, bool printCreate, bool forceAbsoluteDiff, SA_PARAM_NN_STR const char *caller_fname, int line);
int WriteBinaryFunctionCalls(SimpleBufHandle file, StructFunctionCall** structarray, int* sum);
void WriteFloat(FILE* out, float f, int tabs, int eol, void* subtable);
void WriteInt(FILE* out, int i, int tabs, int eol, void* subtable);
void WriteInt64(FILE* out, S64 i, int tabs, int eol, void* subtable);
void WriteUInt(FILE* out, unsigned int i, int tabs, int eol, void* subtable);
void WriteQuotedString(FILE* out, const char* str, int tabs, int eol);
__inline void WriteString(FILE* out, const char* str, int tabs, int eol);
void WriteFloatHighPrec(FILE* out, float f, int tabs, int eol);

//This define will bypass the WriteNString
//#define WriteNString(out, str, len, tabs, eol) WriteString(out, (str?str:""), tabs, eol)

//This define bypasses the field name length caching.
#define ParseTableColumnNameLen(tpi, col) strlen(tpi[col].name)

//This define uses a cached field name length.
//#define ParseTableColumnNameLen(tpi, col) (tpi[col].namelen?tpi[col].namelen:(tpi[col].namelen = strlen(tpi[col].name)))

void WriteTextFunctionCalls(FILE* out, StructFunctionCall** structarray);

typedef struct ResourceLoaderStruct
{
	void **earrayOfStructs;
} ResourceLoaderStruct;

extern ParseTable parse_ResourceLoaderStruct[];
#define TYPE_parse_ResourceLoaderStruct ResourceLoaderStruct
void SetUpResourceLoaderParse(const char *dictName, int childSize, void *childTable, const char *deprecatedName);

///////////////////////////////////////////////////////// forward declares for handler functions
// this way we can locate the functions in topic-appropriate files, 
// but we still have this one central table in which every function is listed

#define FORWARD_DECLARE(type) \
	void type##_preparse(ParseTable* pti, int i, void* structptr, TokenizerHandle tok); \
	int type##_parse(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index, TextParserResult *parseResult); \
	bool type##_writetext(FILE* out, ParseTable tpi[], int column, void* structptr, int index, bool showname, bool ignoreInherited, int level, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); \
	TextParserResult type##_writebin(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); \
	TextParserResult type##_readbin(SimpleBufHandle file, FileList *pFileList, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); \
	bool type##_initstruct(ParseTable tpi[], int column, void* structptr, int index); \
	int type##_destroystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, int index); \
	void type##_updatecrc(ParseTable tpi[], int column, void* structptr, int index); \
	int type##_compare(ParseTable tpi[], int column, const void* lhs, const void* rhs, int index, enumCompareFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); \
	size_t type##_memusage(ParseTable tpi[], int column, void* structptr, int index, bool bAbsoluteUsage); \
	void type##_copystruct(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData); \
	void type##_copyfield(ParseTable tpi[], ParseTable *ptcc, int column, void* dest, void* src, int index, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); \
	enumCopy2TpiResult type##_copyfield2tpis(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, CustomMemoryAllocator memAllocator, void* customData, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString); \
	void type##_writehdiff(char** estr, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, char * prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line); \
	void type##_writebdiff(StructDiff *diff, ParseTable tpi[], int column, void* oldstruct, void* newstruct, int index, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, const char *caller_fname, int line); \
	bool type##_senddiff(Packet* pak, ParseTable tpi[], int column, const void* oldstruct, const void* newstruct, int index, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB); \
	bool type##_recvdiff(Packet* pak, ParseTable tpi[], int column, void* structptr, int index, enumRecvDiffFlags eFlags); \
	bool type##_bitpack(ParseTable tpi[], int column, const void *structptr, int index, PackedStructStream *pack); \
	void type##_unbitpack(ParseTable tpi[], int column, void *structptr, int index, PackedStructStream *pack); \
	void type##_endianswap(ParseTable tpi[], int column, void* structptr, int index); \
	void type##_interp(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 interpParam); \
	void type##_calcrate(ParseTable tpi[], int column, void* structA, void* structB, void* destStruct, int index, F32 deltaTime); \
	void type##_integrate(ParseTable tpi[], int column, void* valueStruct, void* rateStruct, void* destStruct, int index, F32 deltaTime); \
	void type##_calccyclic(ParseTable tpi[], int column, void* valueStruct, void* ampStruct, void* freqStruct, void* cycleStruct, void* destStruct, int index, F32 fStartTime, F32 deltaTime); \
	void type##_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed); \
	TextParserResult type##_writestring(ParseTable tpi[], int column, void* structptr, int index, char** estr, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, SA_PARAM_NN_STR const char *caller_fname, int line); \
	TextParserResult type##_readstring(ParseTable tpi[], int column, void* structptr, int index, const char* str, ReadStringFlags eFlags); \
	TextParserResult type##_tomulti(ParseTable tpi[], int column, void* structptr, int index, MultiVal* result, bool bDuplicateData, bool dummyOnNULL); \
	TextParserResult type##_frommulti(ParseTable tpi[], int column, void* structptr, int index, const MultiVal* value); \
	void type##_calcoffset(ParseTable tpi[], int column, size_t* size); \
	void type##_preparesharedmemoryforfixup(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData); \
	void type##_fixupsharedmemory(ParseTable tpi[], int column, void* structptr, int index, char **ppFixupData); \
	TextParserResult type##_leafFirstFixup(ParseTable tpi[], int column, void *structptr, int index, enumTextParserFixupType eFixupType, void *pExtraData);\
	void type##_reapplypreparse(ParseTable tpi[], int column, void *structptr, int index, char *pCurrentFile, int iTimeStamp, int iLineNum);\
	int type##_checksharedmemory(ParseTable tpi[], int column, void *structptr, int index);\
	bool type##_writehtmlfile(ParseTable tpi[], int column, void *structptr, int index, FILE *out, WriteHTMLContext *pContext);\
	bool type##_writejsonfile(ParseTable tpi[], int column, void *structptr, int index, FILE *out, WriteJsonFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);\
	bool type##_writexmlfile(ParseTable pti[], int column, void *structptr, int index, int level, FILE *out, StructFormatField iOptions);\
	ParseInfoFieldUsage type##_interpretfield(ParseTable tpi[], int column, ParseInfoField field);\
	enumCopy2TpiResult type##_stringifycopy(ParseTable src_tpi[], int src_column, int src_index, void *src, ParseTable dest_tpi[], int dest_column, int dest_index, void *dest, char **ppResultString);\
	void type##_queryCopyStruct(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, void *pUserData); \
	void type##_newCopyField(ParseTable tpi[], int column, void* dest, void* src, int index, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); \
	void type##_earrayCopy(ParseTable tpi[], int column, void* dest, void* src, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude); \
	bool type##_writehdf(ParseTable pti[], int column, void *structptr, int index, HDF *hdf, char *name_override);\
	int type##_recvdiff2tpis(Packet *pak, ParseTable src_tpi[], ParseTable dest_tpi[], int src_column, int index, Recv2TpiCachedInfo *pCache, void *data);\
	int type##_textdiffwithnull(char** estr, ParseTable tpi[], int column, void* newstruct, int index, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line);\



FORWARD_DECLARE(ignore);
FORWARD_DECLARE(end);
FORWARD_DECLARE(error);
FORWARD_DECLARE(number);
FORWARD_DECLARE(u8);
FORWARD_DECLARE(int16);
FORWARD_DECLARE(int);
FORWARD_DECLARE(int64);
FORWARD_DECLARE(float);
FORWARD_DECLARE(string);
FORWARD_DECLARE(raw);
FORWARD_DECLARE(pointer);
FORWARD_DECLARE(currentfile);
FORWARD_DECLARE(timestamp);
FORWARD_DECLARE(linenum);
FORWARD_DECLARE(usedfield);
FORWARD_DECLARE(bool);
FORWARD_DECLARE(flags);
FORWARD_DECLARE(boolflag);
FORWARD_DECLARE(quatpyr);
FORWARD_DECLARE(matpyr);
FORWARD_DECLARE(filename);
FORWARD_DECLARE(link);
FORWARD_DECLARE(reference);
FORWARD_DECLARE(functioncall);
FORWARD_DECLARE(unparsed);
FORWARD_DECLARE(struct);
FORWARD_DECLARE(poly);
FORWARD_DECLARE(stashtable);
FORWARD_DECLARE(MultiVal);
FORWARD_DECLARE(bit);

//FORWARD_DECLARE(nonarray);
//FORWARD_DECLARE(fixedarray);
//FORWARD_DECLARE(earray);
FORWARD_DECLARE(command);


//a special failure function to call inside recvDiff functions. Asserts if it should assert or fails otherwise
#define RECV_FAIL(fmt, ...) { char *pFullError = NULL; estrStackCreate(&pFullError); estrPrintf(&pFullError, fmt, ##__VA_ARGS__); if (eFlags & RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE) { Errorf("%s%s", GetRecvFailCommentString(), pFullError); estrDestroy(&pFullError); return 0; } else { assertmsgf(0, "%s%s", GetRecvFailCommentString(), pFullError); }}
#define RECV_CHECK_PAK(pak) { if (pak->error_occurred) RECV_FAIL("Packet overrun or float corruption"); }


#endif // STRUCTINTERNALS_H
