#pragma once

#include "FileServing.h"

void GenericFileServing_CommandCallBack(char *pFileName, int iRequestID, enumFileServingCommand eCommand,
	U64 iBytesRequested, FileServingRequestFulfilledCallBack *pFulfillCB);
void GenericFileServing_ExposeDirectory(char *pDirName);

void GenericFileServing_Begin(int iBytesPerFilePerTick, int iInactivityTimeout);
void GenericFileServing_Tick(void);

//normally, files look like this:
///mapmanager/7/fileSystem/foo.txt
//
//The simplest way to serve up files-that-don't-yet-exist is to add a callback which creates a temp file
//and then serves it. So logparser/0/downloadGraph/Joe, when linked to, calls a callback which creates a local
//file, then the downloading happens as normal

//given a fileserving name (Joe), returns the absolute path of the created file (c:\temp\joe.zip), or NULL
typedef char *GenericFileServing_SimpleDomainCB(char *pInName, char **ppErrorString);


void GenericFileServing_RegisterSimpleDomain(char *pDomainName, GenericFileServing_SimpleDomainCB *pCB);