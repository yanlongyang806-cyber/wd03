#ifndef __EDITORSERVERMAIN_H__
#define __EDITORSERVERMAIN_H__
GCC_SYSTEM

typedef struct Packet Packet;
typedef struct NetLink NetLink;
typedef struct ResourceCache ResourceCache;

// these are the command types that can be sent to the server for processing
typedef enum
{
	CommandOpenMap,							// load an existing ZoneMap
	CommandLock,							// lock files
	CommandUnlock,							// unlock files
	CommandDebugListLocks,					// tells the server to print lock status of all of its locks
	CommandReloadFromSource,				// tells server to reload the map from source files, deleting bins
	CommandSaveDummyEncounters,				// save the dummy encounters (TomY ENCOUNTER_HACK)
	CommandGenesisGenerateMissions,			// generate just the missions for a genesis map
	CommandGenesisGenerateEpisodeMission,	// generate the episode mission
	CommandUGCPublish,						// Initiate a UGC publish operation
} ServerCommand;

void editorServerInit(void);
void editorHandleGameMsg(Packet * pak, NetLink *link, ResourceCache *pCache);
void editorServerSendQueuedReplies(void);

// Editor server callbacks
typedef enum
{
	ESCT_RELOADED_FROM_SOURCE,
	ESCT_PRE_PUBLISH,
	ESCT_PUBLISH,
} EditorServerCallbackType;
typedef void (*EditorServerCallback)(EditorServerCallbackType eType, const char *pcMapName, void *pUserdata);
void editorServerRegisterCallback(EditorServerCallback pCallback, void *pUserdata);

#endif // __EDITORSERVERMAIN_H__
