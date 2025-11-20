// Red-Black Tree implemenation
// Original taken from: http://oopweb.com/Algorithms/Documents/Sman/VolumeFront.html
// Author: Thomas Niemann, Portland Oregon
// License: "Source code when part of a software project may be used freely without reference to the author."

#pragma once
GCC_SYSTEM


typedef enum RbtStatus {
    RBT_STATUS_OK,
    RBT_STATUS_DUPLICATE_KEY,
    RBT_STATUS_KEY_NOT_FOUND,
	RBT_STATUS_OVERWROTE
} RbtStatus;

typedef struct RedBlackTreeNode RedBlackTreeNode;
typedef RedBlackTreeNode *RbtIterator;
typedef struct RedBlackTree RedBlackTree;

RedBlackTree *rbtNew_dbg(int(*compare)(void *a, void *b) MEM_DBG_PARMS);
#define rbtNew(compare) rbtNew_dbg(compare MEM_DBG_PARMS_INIT)

// create red-black tree
// parameters:
//     compare  pointer to function that compares keys
//              return 0   if a == b
//              return < 0 if a < b
//              return > 0 if a > b
//         note: key being searched for will always be parameter 'a'
//              and existing elements will be 'b', so if you have
//              a tree of sets/ranges, you return the relation between
//              the value 'a' and the set 'b' in your comparator
// returns:
//     handle   use handle in calls to rbt functions


typedef void (*Destructor)(void* element);
void rbtDelete(RedBlackTree *h, Destructor keyDestructor, Destructor valueDestructor);
// destroy red-black tree

typedef void (*DestructorWithUserData)(void *element, void *pUserData);
void rbtDeleteEx(RedBlackTree *h, DestructorWithUserData keyDestructor, DestructorWithUserData valueDestructor, void *pUserData);
//destroy with userData pointer

RbtStatus rbtInsertEx(RedBlackTree *h, void *key, void *value, bool bOverWrite, RedBlackTreeNode **ppNode, void **ppOverwrittenValue);
#define rbtInsert(h, key, value) rbtInsertEx(h, key, value, false, NULL, NULL)
// insert key/value pair

RbtStatus rbtErase(RedBlackTree *h, RbtIterator i);
// delete node in tree associated with iterator
// this function does not free the key/value pointers

RbtIterator rbtNext(RedBlackTree *h, RbtIterator i);
// return ++i

RbtIterator rbtPrev(RedBlackTree *h, RbtIterator i);
// return --i

RbtIterator rbtBegin(RedBlackTree *h);
// return pointer to first node

RbtIterator rbtEnd(RedBlackTree *h);
// return pointer to one past last node

void rbtKeyValue(RedBlackTree *h, RbtIterator i, void **key, void **value);
// returns key/value pair associated with iterator

RbtIterator rbtFind(RedBlackTree *h, void *key);
// returns iterator associated with key

RbtIterator rbtFindGTE(RedBlackTree *h, void *key);
// returns iterator associated with key, or the closest one greater than it


//From here down is the cryptic-friendly API that feels a lot like StashTables, etc.

typedef enum RedBlackTreeType
{
	REDBLACKTREE_NONE,

	REDBLACKTREE_INT,
	REDBLACKTREE_U32,
	REDBLACKTREE_STRING,
	REDBLACKTREE_STRINGKEYEDSTRUCT,
} RedBlackTreeType;


typedef struct CrypticRedBlackTree
{
	RedBlackTreeType eType;
	int iCount;
	bool bCopyStringKeys;
	ParseTable *pTPI;
	RedBlackTree *pTree;
} CrypticRedBlackTree;


#define KEY_FROM_INT(a) ((void*)((intptr_t)((U32)a)))
#define INT_FROM_KEY(k) (int)((U32)((intptr_t)k))

#define KEY_FROM_U32(a) ((void*)((intptr_t)(a)))
#define U32_FROM_KEY(k) ((U32)((intptr_t)k))



//signed integer keys
CrypticRedBlackTree *CrypticRedBlackTree_Create_IntKeys(void);
bool CrypticRedBlackTree_Int_AddPointer(CrypticRedBlackTree *pTree, int iKey, void *pValue, bool bOverwrite);
bool CrypticRedBlackTree_Int_FindPointer(CrypticRedBlackTree *pTree, int iKey, void **ppFound);
bool CrypticRedBlackTree_Int_RemovePointer(CrypticRedBlackTree *pTree, int iKey, void **ppFound);
void CrypticRedBlackTree_Int_Destroy(CrypticRedBlackTree **ppTree);
void CrypticRedBlackTree_Int_DestroyEx(CrypticRedBlackTree **ppTree, Destructor valDestructor);

#define FOR_EACH_IN_CRYPTICREDBLACKTREE_INT(tree, key, type, p) { int key; RbtIterator i##p##Iter; void *p##Key; type *p; for (i##p##Iter = rbtBegin(tree->pTree); i##p##Iter; i##p##Iter = rbtNext(tree->pTree, i##p##Iter)) { rbtKeyValue(tree->pTree, i##p##Iter, &p##Key, &p); key = INT_FROM_KEY(p##Key); 
#define FOR_EACH_END } }


//U32 keys
CrypticRedBlackTree *CrypticRedBlackTree_Create_U32Keys(void);
bool CrypticRedBlackTree_U32_AddPointer(CrypticRedBlackTree *pTree, U32 iKey, void *pValue, bool bOverwrite);
bool CrypticRedBlackTree_U32_FindPointer(CrypticRedBlackTree *pTree, U32 iKey, void **ppFound);
bool CrypticRedBlackTree_U32_RemovePointer(CrypticRedBlackTree *pTree, U32 iKey, void **ppFound);
void CrypticRedBlackTree_U32_Destroy(CrypticRedBlackTree **ppTree);
void CrypticRedBlackTree_U32_DestroyEx(CrypticRedBlackTree **ppTree, Destructor valDestructor);

#define FOR_EACH_IN_CRYPTICREDBLACKTREE_U32(tree, key, type, p) { U32 key; RbtIterator i##p##Iter; void *p##Key; type *p; for (i##p##Iter = rbtBegin(tree->pTree); i##p##Iter; i##p##Iter = rbtNext(tree->pTree, i##p##Iter)) { rbtKeyValue(tree->pTree, i##p##Iter, &p##Key, &p); key = U32_FROM_KEY(p##Key); 

//string keys
CrypticRedBlackTree *CrypticRedBlackTree_Create_StringKeys(bool bInternallyCopyKeys);
bool CrypticRedBlackTree_String_AddPointer(CrypticRedBlackTree *pTree, char *pKey, void *pValue, bool bOverwrite);
bool CrypticRedBlackTree_String_FindPointer(CrypticRedBlackTree *pTree, char *pKey, void **ppFound);
bool CrypticRedBlackTree_String_RemovePointer(CrypticRedBlackTree *pTree, char *pKey, void **ppFound);
void CrypticRedBlackTree_String_Destroy(CrypticRedBlackTree **ppTree);
void CrypticRedBlackTree_String_DestroyEx(CrypticRedBlackTree **ppTree, Destructor valDestructor);

#define FOR_EACH_IN_CRYPTICREDBLACKTREE_STRING(tree, key, type, p) { char *key; RbtIterator i##p##Iter; type *p; for (i##p##Iter = rbtBegin(tree->pTree); i##p##Iter; i##p##Iter = rbtNext(tree->pTree, i##p##Iter)) { rbtKeyValue(tree->pTree, i##p##Iter, &key, &p);  

//textparser structs with string keys built in
CrypticRedBlackTree *CrypticRedBlackTree_Create_StringKeyedStructs(ParseTable *pTPI);
bool CrypticRedBlackTree_StringKeyedStruct_AddStruct(CrypticRedBlackTree *pTree, void *pStruct, bool bOverwrite, bool bDestroyOnOverwrite);
bool CrypticRedBlackTree_StringKeyedStruct_FindStruct(CrypticRedBlackTree *pTree, char *pKey, void **ppFound);

bool CrypticRedBlackTree_StringKeyedStruct_RemoveStruct(CrypticRedBlackTree *pTree, char *pKey, void **ppFound);
bool CrypticRedBlackTree_StringKeyedStruct_RemoveAndDestroyStruct(CrypticRedBlackTree *pTree, char *pKey);

void CrypticRedBlackTree_StringKeyedStruct_Destroy(CrypticRedBlackTree **ppTree, bool bDestroyStructs);

//use FOR_EACH_IN_CRYPTICREDBLACKTREE_STRING for StringKeyedStructs





