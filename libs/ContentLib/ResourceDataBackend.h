#ifndef RESOURCEDATABACKEND_H_
#define RESOURCEDATABACKEND_H_

// This defines the interface between the resource system and a data backend (gimme or assetdb)
#include "referencesystem.h"
#include "textparserUtils.h"

// Gimme data backend, used in development

// Attempt to check out a resource
bool resGimmeCheckoutResource(ResourceDictionary *pDictionary, void * pResource, const char *pResourceName, char **estrOut);

// Attempt to undo a checkout of a resource
bool resGimmeUndoCheckoutResource(ResourceDictionary *pDictionary, void * pResource, const char *pResourceName, char **estrOut);

// Figure out if a resource is writable. Also, register callback to wait for changes to writable
void resGimmeCheckWritable(ResourceDictionary *pDictionary, ResourceInfo *resInfo, bool bNoLocationFile, bool bForReload);

// Figure out checked out status of file
void resGimmeCheckRepositoryInfo(ResourceDictionary *pDictionary, ResourceInfo *resInfo, bool bNoLocationFile);

// Apply a list of resource actions
void resGimmeApplyResourceActions(ResourceActionList *pHolder);

// Initialize data backend
void resGimmeInitializeDataBackend(void);



#endif