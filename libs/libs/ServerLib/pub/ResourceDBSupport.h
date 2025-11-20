#pragma once
#include "ReferenceSystem.h"

typedef enum eResourceDBResourceType eResourceDBResourceType;

//set via shardlauncher
bool UseResourceDB(void);

void SetUseResourceDB(bool bSet);

void resourceDBHandleRequest(DictionaryHandleOrName dictHandle, int command, const char *pResourceName, void * pResource, const char* reason);

//returns true if the object was added. If false, it was not added, make sure to clean it up
bool ResourceDBHandleGetObject(DictionaryHandleOrName hDict, char *pName, void *pObject, char *pComment);

//if you create a handle to this resource, will a request be dispatched to the ResourceDB?
bool ResDbWouldTryToProvideResource(eResourceDBResourceType eType, const char *pResourceName);

void ProcessDeferredResDebRequests(void);

//checks if the resource name is in a namespace, specifically one that the resource DB will be able to patch
bool ResourceNameIsSupportedByResourceDB(const char *pNameSpace);