#pragma once
GCC_SYSTEM

#include "HybridObj.h"

/*this module is used by the server monitor to query arbitrary lists of things with arbitrary expressions*/



typedef void GenericIterator_Begin(void *pUserData, void **ppCounter);
typedef void *GenericIterator_GetNext(void *pUserData, void **ppCounter, char **ppName);

typedef struct FilteredList
{
	void *pObject;
	ParseTable *pTPI;
	U32 iFlags;
} FilteredList;


//the TPI and object that are created use the HybridObj system, as the "out" object is constructed
//out of arbitrary fields from the parent object. If you pass in a non-NULL "link" string, then 
//each object's first field will be a field named "link" containing the link string
//with the object name sprintf'd into it.
//
//if pppListOfNames is non-NULL, it is filled in with a list of the names of all the filtered objects found.
//The names will be strduped, so will need to be freed when no longer needed.
FilteredList *GetFilteredListOfObjects(char *pTitle, const char *pFilteringExpression, char **ppFieldsToReturn,
	ParseTable *pObjTPI, GenericIterator_Begin *pIterBeginCB, GenericIterator_GetNext *pIterGetNextCB, void *pUserData,
	char *pLinkString, char ***pppListOfNames, int limit, int offset, bool bForServerMonitoring);

void DestroyFilteredList(FilteredList *pList);



//some useful generic iterators
void GenericIteratorBegin_GlobObj(void *pUserData, void **ppCounter);
void *GenericIteratorGetNext_GlobObj(void *pUserData, void **ppCounter, char **ppName);