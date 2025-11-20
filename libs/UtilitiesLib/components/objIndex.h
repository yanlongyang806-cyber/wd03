#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

//This file provides a means to generate and use a b+tree index on a collection of objects.
// KNN 20081020
// tags: btree b-tree b+tree bplustree search index compiledobjectpaths sorting iteration ranged fast

//for keys and subnodes. 
// *EArrays are suboptimal for storing keys because of all the memmoves.
// *If you have time, feel free to swap in a better datastructure.
#include "earray.h"

//For locks
#include "windefinclude.h"
#include "Semaphore.h"

//For sortcolumn affectedness
#include "objpath.h"

//To make ObjectHeaderOrData more debuggable
#include "objContainer.h"

//Turn this on to enable test methods.
//#define TEST_OBJECT_INDEX

typedef struct StashTableImp* StashTable;

#define OBJINDEX_DEFAULT_ORDER 7

// IMPORTANT: when adding any indexed fields, do a find in files on one of the previous
// enums and add an entry for your field every place they are used or in a comment
// (you'll have to add a field to ObjectIndexHeader and ObjectIndexFileHeader too)
// FURTHER NOTE: Make sure you actually need to add your field to the header before just
// doing it.  The comment above isn't meant to help people unfamiliar with the code to add
// fields to the header. If you don't know what you're doing, talk to Raoul and/or Jon P.
typedef enum ObjectIndexHeaderField
{
	OBJ_HEADER_CONTAINERID,
	OBJ_HEADER_ACCOUNTID,
	OBJ_HEADER_CREATEDTIME,
	OBJ_HEADER_LEVEL,
	OBJ_HEADER_FIXUP_VERSION,
	OBJ_HEADER_LAST_PLAYED_TIME,
	OBJ_HEADER_PUB_ACCOUNTNAME,
	OBJ_HEADER_PRIV_ACCOUNTNAME,
	OBJ_HEADER_SAVEDNAME,
	OBJ_HEADER_VIRTUAL_SHARD_ID,
	OBJ_HEADER_EXTRA_DATA_1,
	OBJ_HEADER_EXTRA_DATA_2,
	OBJ_HEADER_EXTRA_DATA_3,
	OBJ_HEADER_EXTRA_DATA_4,
	OBJ_HEADER_EXTRA_DATA_5,

	OBJ_HEADER_COUNT
}ObjectIndexHeaderField;

const char *objIndexGetExtraDataPath(ObjectIndexHeaderField field);

typedef struct OriginalObjectIndexFileHeader
{
	U32 headerSize;
	U32 crc;
	U32 containerId;
	U32 accountId;
	U32 createdTime;
	U32 level;
	U32 fixupVersion;
	U32 lastPlayedTime;

	U32 pubAccountNameLen;
	U32 privAccountNameLen;
	U32 savedNameLen;

} OriginalObjectIndexFileHeader;

typedef struct ObjectIndexFileHeaderTwo
{
	U32 crc;
	U32 containerId;
	U32 accountId;
	U32 createdTime;
	U32 level;
	U32 fixupVersion;
	U32 lastPlayedTime;

	U32 pubAccountNameLen;
	U32 privAccountNameLen;
	U32 savedNameLen;

	U32 virtualShardId;
} ObjectIndexFileHeaderTwo;

typedef struct ObjectIndexFileHeader
{
	U32 crc;
	U32 containerId;
	U32 accountId;
	U32 createdTime;
	U32 level;
	U32 fixupVersion;
	U32 lastPlayedTime;

	U32 pubAccountNameLen;
	U32 privAccountNameLen;
	U32 savedNameLen;

	U32 virtualShardId;
	
	U32 extraDataCrc;
	U32 extraData1Len;
	U32 extraData2Len;
	U32 extraData3Len;
	U32 extraData4Len;
	U32 extraData5Len;
} ObjectIndexFileHeader;

typedef struct ContainerFileHeader {
	U32 headerSize;
	U32 headerVersion;
	U32 deletedTime;

	ObjectIndexHeaderType objextIndexHeaderType;
} ContainerFileHeader;

#define CONTAINER_FILE_HEADER_ORIGINAL_VERSION 810153323 // The original header crc
#define OBJECT_FILE_HEADER_CRC_TWO 2890482693 // The Object file header crc before the addition of extra data
#define CONTAINER_FILE_HEADER_CURRENT_VERSION 1

typedef struct ContainerHeaderInfo {
	U32 deletedTime;
	ObjectIndexHeaderType objectIndexHeaderType;
	ObjectIndexHeader *objectHeader;
} ContainerHeaderInfo;

typedef struct ObjectIndexHeader {
	U32 containerId;
	U32 accountId;
	U32 createdTime;
	U32 level;
	U32 fixupVersion;
	U32 lastPlayedTime;
	U32 virtualShardId;

	const char* pubAccountName;
	const char* privAccountName;
	const char* savedName;
	const char* extraData1;
	const char* extraData2;
	const char* extraData3;
	const char* extraData4;
	const char* extraData5;
	U32 extraDataOutOfDate : 1;
} ObjectIndexHeader;

typedef struct ObjectIndexHeader_NoConst {
	U32 containerId;
	U32 accountId;
	U32 createdTime;
	U32 level;
	U32 fixupVersion;
	U32 lastPlayedTime;
	U32 virtualShardId;

	char* pubAccountName;
	char* privAccountName;
	char* savedName;
	char* extraData1;
	char* extraData2;
	char* extraData3;
	char* extraData4;
	char* extraData5;
	U32 extraDataOutOfDate : 1;
} ObjectIndexHeader_NoConst;

typedef struct ObjectBTKV
{
	union{
		void *key;
		ObjectIndexHeader *header;
	};
	struct ObjectBTNode *child;
} ObjectBTKV;

typedef struct ObjectBTNode
{
	struct ObjectBTNode *prev;
	struct ObjectBTNode *next;

	ObjectBTKV **kvs;	//earray
	S64 count;			//sum of all leaf descendants
} ObjectBTNode;

typedef struct ObjectSortCol
{
	ParserSortData context;
	ObjectPath *colPath;
	ParserCompareFieldFunction comp;
	ObjectIndexHeaderField headerField;
} ObjectSortCol;

typedef struct ObjectIndex
{
	ObjectSortCol **columns;	//earray of columns by priority.
	
	U32 opts;

	//b+tree
	S64 count;
	ObjectBTNode *root;
	U16 order;
	U16 depth;
	int lastinsert;
	S64 currentIndex; //The current index while traversing the tree. Only valid during traversal.

	//for direct iteration.
	ObjectBTNode *first;
	ObjectBTNode *last;
	
	//Read/Write locks
	int					read_count;
	CrypticSemaphore	read_semaphore;
	CRITICAL_SECTION	write_lock;

	StashTable headerPathTable;
	U32 useHeaders : 1;
} ObjectIndex;


typedef struct ObjectIndexKey
{
	MultiVal val;
	void *str;
} ObjectIndexKey;

typedef struct ObjectHeaderOrData
{
	union {
		ObjectIndexHeader header;
		Container con;
#ifdef TEST_OBJECT_INDEX
		OITestElement test;
#endif
	};
}ObjectHeaderOrData;

//Create an object index with an earray of paths.
// THREAD SAFETY NOTE: If you are using this index in multiple threads, see important comments in objIndex.c
ObjectIndex* objIndexCreate(S16 order, U32 options, ObjectPath **paths, const char **origPaths, StashTable headerPathTable);

//This function creates sort columns for each path provided and creates an index.
// The list of paths must be NULL terminated.
// THREAD SAFETY NOTE: If you are using this index in multiple threads, see important comments in objIndex.c
ObjectIndex* objIndexCreateWithStringPaths(S16 order, U32 options, ParseTable *tpi, char *paths, ...);

//Convenience for single paths.
// THREAD SAFETY NOTE: If you are using this index in multiple threads, see important comments in objIndex.c
#define objIndexCreateWithStringPath(order, options, tpi, path) objIndexCreateWithStringPaths(order, options, tpi, path, 0)

//Destroy an object index. Free all parts.
// *not thread safe. You'll need to ensure that all references to the index are not in use before destroying.
void objIndexDestroy(ObjectIndex **oi);

//Returns true if a specific path affects the sorting in this index.
bool objIndexPathAffected(ObjectIndex *oi, ObjectPath *path);
//Returns true if any of the paths affect the sorting in this index.
bool objIndexPathsAffected(ObjectIndex *oi, EARRAY_OF(ObjectPath) paths);

//ObjectIndexes do not own indexed data; they must be updated when objects are destroyed.
// *Insertion ASSUMES that the parsetable for this index is correct for strptr!
// *Returns true on success, false if key could not be resolved.
// *Thread safety requirement: write lock required
bool objIndexInsert(ObjectIndex *oi, void *strptr);

//Searches for the object (by pointer equality) and, if found, conditionally removes it from the index.
// *Returns true if an object was found/removed.
// *If you need to UPDATE an object's KEY FIELD, you MUST remove it from the index first!
// *Thread safety requirement: if remove == true, write lock required; else, read lock required
bool objIndexFind(ObjectIndex *oi, void *structptr, bool remove);

//Searches for the exact object and returns true if found.
// *Thread safety requirement: read lock required
#define objIndexContains(oi, strptr) objIndexFind(oi, strptr, false)

//Searches for an object and removes it.
// *Returns true if found and removed.
// *Thread safety requirement: write lock required
#define objIndexRemove(oi, strptr) objIndexFind(oi, strptr, true)

//Searches for an object with the specified key and removes the first occurrence.
// *Returns true if an object was removed.
// *Thread safety requirement: write lock required
#define objIndexRemoveByKey(oi, key) objIndexFindExactMatch(oi, key, 0, true)

//Searches for an object with a template object and removes the first occurrence.
// *Returns true if an object was removed.
// *Thread safety requirement: write lock required
#define objIndexRemoveByTemplate(oi, strptr) objIndexFindExactMatch(oi, 0, strptr, true)

//Return the MultiValType of the key field.
MultiValType objIndexGetKeyType(ObjectIndex *oi);

//Use a struct template (or legit element) to search.
// *Returns true if an index column match is found.
// *Passes back the first match in strfound.
// *Thread safety requirement: read lock required
// !!!This function may return a different element with a matching value in the index column!!!
#define objIndexSearchStruct(oi, key, strfound) objIndexGet(oi, key, 0, strfound)

//If you absolutely, positively NEED a heap-allocated ObjectIndexKey, use this
// But seriously, just make one on the stack and pass its address to any objIndexInitKey_* function
ObjectIndexKey *objIndexCreateKey(void);

//For allocated keys.
// Don't use this, use the specific destructors listed below.
void objIndexDeinitKey(ObjectIndex *oi, ObjectIndexKey *key, bool destroyData);
void objIndexDestroyKey(ObjectIndex *oi, ObjectIndexKey **key, bool destroyData);

//Init a search key containing an allocated struct with filled in search column fields.
// *key.str will point to a StructCreate()'d struct.
// *for each non-null var_arg, this function will copy the value into the appropriate search column field in order.
// *You must provide the same number of column parameters as there are search columns on the index.
// This is not reentrant. DO NOT NEST WALKS OF INDEXES USING objIndexInitKey_Template.
// This is thread safe.
void objIndexInitKey_Template(ObjectIndex *oi, ObjectIndexKey *key, ...);
#define objIndexDeinitKey_Template(oi, key) objIndexDeinitKey(oi, key, true)
#define objIndexDestroyKey_Template(oi, key) objIndexDestroyKey(oi, key, true)

//Init a search key with a struct.
// *strptr is assumed to be the type of the index element.
void objIndexInitKey_Struct(ObjectIndex *oi, ObjectIndexKey *key, void *strptr);
//Unowned key structs should never be destroyed like templates.
#define objIndexDeinitKey_Struct(oi, key) objIndexDeinitKey(oi, key, false)
#define objIndexDestroyKey_Struct(oi, key) objIndexDestroyKey(oi, key, false)

//Init a search key with an int.
// *asserts if the index field is not int compatible.
void objIndexInitKey_Int(ObjectIndex *oi, ObjectIndexKey *key, int value);
#define objIndexDeinitKey_Int(oi, key) objIndexDeinitKey(oi, key, true)
#define objIndexDestroyKey_Int(oi, key) objIndexDestroyKey(oi, key, true)

//Init a search key with an int 64.
// *asserts if the index field is not int compatible.
void objIndexInitKey_Int64(ObjectIndex *oi, ObjectIndexKey *key, S64 value);
#define objIndexDeinitKey_Int64(oi, key) objIndexDeinitKey(oi, key, true)
#define objIndexDestroyKey_Int64(oi, key) objIndexDestroyKey(oi, key, true)

//Init a search key with an int.
// *asserts if the index field is not int compatible.
void objIndexInitKey_U8(ObjectIndex *oi, ObjectIndexKey *key, U8 value);
#define objIndexDeinitKey_U8(oi, key) objIndexDeinitKey(oi, key, true)
#define objIndexDestroyKey_U8(oi, key) objIndexDestroyKey(oi, key, true)

//Init a search key with a string.
// *asserts if the index field is not int compatible.
void objIndexInitKey_F32(ObjectIndex *oi, ObjectIndexKey *key, F32 value);
#define objIndexDeinitKey_F32(oi, key) objIndexDeinitKey(oi, key, true)
#define objIndexDestroyKey_F32(oi,key) objIndexDestroyKey(oi, key, true)

//Init a search key with a string.
// *asserts if the index field is not int compatible.
void objIndexInitKey_String(ObjectIndex *oi, ObjectIndexKey *key, const char *value);
#define objIndexDeinitKey_String(oi, key) objIndexDeinitKey(oi, key, true)
#define objIndexDestroyKey_String(oi,key) objIndexDestroyKey(oi, key, true)

//Get an object by indexed order.
// *if key is NULL, get the n'th element in the index.
// *if key is not NULL, get the n'th match.
// *returns true if a match is found and passed back in strfound.
// *Thread safety requirement: read lock required
bool objIndexGet(ObjectIndex *oi, ObjectIndexKey *key, S64 n, ObjectHeaderOrData **strfound);

//Get the element with the minimum key.
// *This is fast.
// *Thread safety requirement: read lock required
ObjectHeaderOrData *objIndexGetFirst(ObjectIndex *oi);

//Get the element with the maximum key.
// *This is fast.
// *Thread safety requirement: read lock required
ObjectHeaderOrData *objIndexGetLast(ObjectIndex *oi);

//Does not lock.
S64 objIndexCount(ObjectIndex *oi);

//Will block calling thread until a lock can be obtained.
// *will block other writers while lock is maintained.
bool objIndexObtainReadLock(ObjectIndex *oi);

//*Releases the read lock.
// *returns NULL for convenience.
void* objIndexReleaseReadLock(ObjectIndex *oi);

//Will block calling thread until a lock can be obtained.
// *will block other readers and writers while lock is maintained.
bool objIndexObtainWriteLock(ObjectIndex *oi);

//Releases the write lock.
// *returns NULL for convenience.
void* objIndexReleaseWriteLock(ObjectIndex *oi);

//Do not use this.
// *This function will traverse the tree and locate an exact matching element and then remove it.
// *If both key and strptr are not NULL, this function will match pointer equality (exact match).
// *If key is NULL, strptr will not be checked for pointer equality. (strptr is a template)
// *If strptr is NULL, this function will match the first element with key equality.
// *Use objIndexRemove() if you need to remove something.
// *Thread safety requirement: if remove == true, write lock required; else, read lock required
bool objIndexFindExactMatch(ObjectIndex *oi, ObjectIndexKey *key, void *strptr, bool remove);


// ITERATE_REVERSE does not work correctly with template keys without all fields specified.
// To do a template search with one field specified, use the appropriate type specific creation function, like objIndexCreateKey_Int.
// Template searches with x of y fields specified (x > 1 && x < y) currently have no work around for ITERATE_REVERSE

#define SEARCHBUCKET_FINDNEXT 1 << 2

typedef enum {
	ITERATE_FORWARD = 0,
	ITERATE_REVERSE = SEARCHBUCKET_FINDNEXT
} IteratorDirection;

typedef struct ObjectIndexIterator {
	ObjectIndex *oi;
	ObjectBTNode *leafNode;
	int nodeIndex;
	IteratorDirection dir;
} ObjectIndexIterator;

//Returns the number of elements matching the given key.
// *Thread safety requirement: read lock required
S64 objIndexCountKey(ObjectIndex *oi, ObjectIndexKey *key);

//Returns the number of elements between the keys.
// *Pass NULL for from or to for an unbounded end.
// *Thread safety requirement: read lock required
S64 objIndexCountRange(ObjectIndex *oi, ObjectIndexKey *from, ObjectIndexKey *to);

//Copies elements into the earray within the range.
// *Thread safety requirement: read lock required
S64 objIndexCopyEArrayRange(ObjectIndex *oi, cEArrayHandle * handle, S64 start, S64 count);

//Copies elements [count] into the earray beginning with [n]th item after [key]
// *Thread safety requirement: read lock required
S64 objIndexCopyEArrayRangeFrom(ObjectIndex *oi, cEArrayHandle *handle, ObjectIndexKey *key, S64 n, IteratorDirection dir, S64 count);

//Copies elements into the earray from [key] to (end).
// *Thread safety requirement: read lock required
S64 objIndexCopyEArrayRangeFromTo(ObjectIndex *oi, cEArrayHandle *handle, ObjectIndexKey *key, S64 n, ObjectIndexKey *end);

//Copies all elements into the earray matching key.
// *set reverse to true to reverse the copy order.
// *Thread safety requirement: read lock required
S64 objIndexCopyEArrayOfKey(ObjectIndex *oi, cEArrayHandle *handle, ObjectIndexKey *key, bool reverse);


//***If you are running multiple threads, you MUST ReadLock your index while your iterators are active.***//
//These iterator functions will not ReadLock for you.

//Get an object iterator for the index.
// *Iterator starts at first or last element for ITERATE_FORWARD and ITERATE_REVERSE, respectively.
// *Returns false if there are not elements to iterate.
// *DOES NOT LOCK
bool objIndexGetIterator(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir);

//Start the iterator from a simple key offset.
// *node searching rules apply the same as objIndexGet.
// *ITERATE_REVERSE will cause the iterator to go to the last element with the key.
// *Returns true if a matching element was found.
// *Thread safety requirement: read lock required
bool objIndexGetIteratorFrom(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir, ObjectIndexKey *key, S64 n);

//Finds the first item with the key (forward) or the last item with the key (reverse)
// *Returns true if an element was found.
// *Thread safety requirement: read lock required
bool objIndexFindLimit(ObjectIndex *oi, IteratorDirection dir, ObjectIndexKey *key, ObjectHeaderOrData **strfound);

//Start the iterator at the first simple key offset greater/less than your key.
// *This basically just makes the iterator backwards and then gets next until we're at the correct index.
// *Thread safety requirement: read lock required
bool objIndexGetIteratorPast(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir, ObjectIndexKey *key, S64 n);

//Start the iterator at a specific node.
// *strptr will be treated as a template (pointer equality need not match).
// *Returns true if a matching element was found.
// *Thread safety requirement: read lock required
bool objIndexGetIteratorAt(ObjectIndex *oi, ObjectIndexIterator *iter, IteratorDirection dir, void *strptr);

//Change the direction of the iterator.
__forceinline void objIndexReverseIterator(ObjectIndexIterator *iter); 

//Get the next item from the iterator.
// *Advances the node index and node.
// *Thread safety requirement: read lock required
ObjectHeaderOrData *objIndexGetNext(ObjectIndexIterator *iter);

//Peek the next item from the iterator.
// *Does not advance the node index and node.
// *Thread safety requirement: read lock required
ObjectHeaderOrData *objIndexPeekNext(ObjectIndexIterator *iter);

typedef enum {
	OIM_LTE = -2,
	OIM_LT = -1,
	OIM_EQ = 0,
	OIM_GT = 1,
	OIM_GTE = 2,
} ObjectIndexMatch;

// *Thread safety requirement: read lock required
ObjectHeaderOrData *objIndexGetNextMatch(ObjectIndexIterator *iter, ObjectIndexKey *key, ObjectIndexMatch match);

//Copies count elements into the earray from the iterator.
// *Thread safety requirement: read lock required
S64 objIndexGetEArrayCount(ObjectIndex *oi_UNUSED_, ObjectIndexIterator *iter, cEArrayHandle * handle, S64 count);

//Copies elements into the earray while < key.
// *Does not include key.
// *Thread safety requirement: read lock required
S64 objIndexGetEArrayToKey(ObjectIndex *oi_UNUSED_, ObjectIndexIterator *iter, cEArrayHandle * handle, ObjectIndexKey *key);

//Copies elements into the earray while == key.
// *Thread safety requirement: read lock required
S64 objIndexGetEArrayWhileKey(ObjectIndex *oi_UNUSED_, ObjectIndexIterator *iter, cEArrayHandle * handle, ObjectIndexKey *key);

//Test utils
char* printObjectIndexElement(ObjectIndex *oi, S64 n);

void printObjectIndexKey(ObjectIndex *oi, void *strptr, char **estr);
void printObjectIndexNode(ObjectIndex *oi, ObjectBTNode *node, int depth, char **estr);
void printObjectIndexStructure(ObjectIndex *oi, char **estr);

// ObjectIndexHeader manipulation functions

void DeinitObjectIndexHeader(ObjectIndexHeader *header);
void DestroyObjectIndexHeader(ObjectIndexHeader **header);
U32 objGetHeaderFieldIntVal(const ObjectIndexHeader *header, ObjectIndexHeaderField headerField);
const char *objGetHeaderFieldStringVal(const ObjectIndexHeader *header, ObjectIndexHeaderField headerField);

void LoadAndCheckExtraHeaderDataConfig(void);
