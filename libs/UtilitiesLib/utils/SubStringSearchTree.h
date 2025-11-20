#pragma once

typedef struct SubStringSearchTree SubStringSearchTree;
typedef struct SubStringSearchTreeInt SubStringSearchTreeInt;

//stores void*
SubStringSearchTree *SSSTree_Create(int iMinStringLength);

//stores int
SubStringSearchTreeInt *SSSTreeInt_Create(int iMinStringLength);

//substring searches match only alphanum characters, and are case insensitive. This function
//turns a string into the internal string format. You should use this when checking minimum legal length
void SSSTree_InternalizeString_dbg(char **ppOutString, const char *pInString, const char *caller_fname, int lineNum );
#define SSSTree_InternalizeString(ppOutString, pInString) SSSTree_InternalizeString_dbg(ppOutString, pInString, __FILE__, __LINE__)


typedef enum SSSTreeSearchType
{
	SSSTREE_SEARCH_PRECISE_STRING,
	SSSTREE_SEARCH_SUBSTRINGS,
	SSSTREE_SEARCH_PREFIXES,
	SSSTREE_SEARCH_SUFFIXES,
} SSSTreeSearchType;



//returns true if the element was added (currently the only failure case is a name that is too short)
bool SSSTree_AddElement(SubStringSearchTree *pTree, const char *pName, void *pData);

//returns true if the element was found
bool SSSTree_RemoveElement(SubStringSearchTree *pTree, const char *pName, void *pData);

//returns true or false depending on whether this element is in the tree)
bool SSSTree_FindPreciseElement(SubStringSearchTree *pTree, const char *pName, void *pData);



void SSSTree_FindElementsByPreciseName(SubStringSearchTree *pTree, void ***pppOutFoundElements, const char *pName);
void SSSTree_FindElementsBySubString(SubStringSearchTree *pTree, void ***pppOutFoundElements, char *pSubString);


typedef bool (SSSTreeCheckFunction)(void *pData, void *pUserData);




//returns true if it wanted to return at least one more than it returned
bool SSSTree_FindElementsWithRestrictions(SubStringSearchTree *pTree, SSSTreeSearchType eSearchType, const char *pSubString, void ***pppOutFoundElements, int iMaxToReturn,
	SSSTreeCheckFunction *pCheckFunction, void *pUserData);




//----------------SSSTrees can also store INTs. Internally they're just pointers, so don't mix the two

//returns true if the element was added (currently the only failure case is a name that is too short)
static __forceinline bool SSSTreeInt_AddElement(SubStringSearchTreeInt *pTree, const char *pName, U32 iData)
{
	return SSSTree_AddElement((SubStringSearchTree*)pTree, pName, (void*)((intptr_t)iData));
}

//returns true if the element was found
static __forceinline bool SSSTreeInt_RemoveElement(SubStringSearchTreeInt *pTree, const char *pName, U32 iData)
{
	return SSSTree_RemoveElement((SubStringSearchTree*)pTree, pName, (void*)((intptr_t)iData));
}
//returns true or false depending on whether this element is in the tree)
static __forceinline bool SSSTreeInt_FindPreciseElement(SubStringSearchTreeInt *pTree, const char *pName, U32 iData)
{
	return SSSTree_FindPreciseElement((SubStringSearchTree*)pTree, pName, (void*)((intptr_t)iData));
}


typedef bool (SSSTreeIntCheckFunction)(U32 iData, void *pUserData);


//returns true if it wanted to return at least one more than it returned
bool SSSTreeInt_FindElementsWithRestrictions(SubStringSearchTreeInt *pTree, SSSTreeSearchType eSearchType, const char *pSubString, U32 **pppOutFoundElements, int iMaxToReturn,
	SSSTreeIntCheckFunction *pCheckFunction, void *pUserData);

