/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef APPSERVERLIB_H_
#define APPSERVERLIB_H_

#include "GlobalComm.h"
#include "GlobalTypeEnum.h"

typedef struct AppServerDef AppServerDef;

typedef int (*AppServerInitCB)(void);
typedef int (*AppServerOncePerFrameCB)(F32 fElapsed);
typedef void (*AppServerAcquiredSingleContainerCB)(ContainerID containerID);
typedef void (*AppServerAcquiredContainersCB)(void);

enum
{
	//this flag saves a lot of RAM for appservers that don't need to load data files
	APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT = 1,
};

typedef struct AppServerDef
{
	GlobalType appType;
	AppServerInitCB appInit;

	bool appRunning;
	AppServerOncePerFrameCB oncePerFrame;

	U32 iFlags; //APPSERVERTYPEFLAG_

} AppServerDef;

extern AppServerDef **gAppServerList;

extern AppServerDef *gAppServer;

// Run this before anything else to initialize server functions
void aslPreMain(const char* pcAppName, int argc, char **argv);

// The main loop
void aslMain(void);

// Run this once per frame
void aslOncePerFrame(F32 fElapsed);

// Individual Apps call this to register themselves
void aslRegisterApp(GlobalType appType, AppServerInitCB appInit, U32 iFlags /*APPSERVERTYPEFLAG_*/);

// Start up an app with the given name, or return false. Should happen AFTER aslPreMain
int  aslStartApp(void);

//Request ownership of all containers of a specific type from the ObjectDB (via a RemoteCommand).
void aslAcquireContainerOwnershipEx(GlobalType containerType, AppServerAcquiredContainersCB fpCallback, AppServerAcquiredSingleContainerCB fpSingleCallback);
#define aslAcquireContainerOwnership(containerType, fpCallback) aslAcquireContainerOwnershipEx(containerType, fpCallback, NULL)


#endif
