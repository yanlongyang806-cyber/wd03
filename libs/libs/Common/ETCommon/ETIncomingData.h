#pragma once
typedef struct Packet Packet;
typedef struct NetLink NetLink;
typedef struct StashTableImp* StashTable;
typedef struct NOCONST(DumpData) NOCONST(DumpData);

// Must include to get FileWrapper stuff correct
#include "file.h"

// Types of Incoming Data:
// 
// ERRORDATA:
// * Valid Members: eType, pErrorData
// * Description  : Provides new ErrorData from client
// 
// LINK_DROPPED:
// * Valid Members: eType, dumpID
// * Description  : Notification that [dumpID -> ErrorEntry] mapping is no longer valid
// 
// DUMP_RECEIVED:
// * Valid Members: eType, dumpID, pTempFilename
// * Description  : Notification that pTempFilename is a valid dump for crash mapping to dumpID
//                  If dumpID actually still maps to an ErrorEntry, move the temporary file
//                  into its final resting place, and update ErrorEntry to point to it. 
//                  Ignore the incoming dump if ErrorEntry already has an example.

AUTO_ENUM;
typedef enum IncomingDataType
{
	INCOMINGDATATYPE_UNKNOWN = 0,
	INCOMINGDATATYPE_ERRORDATA,

	INCOMINGDATATYPE_LINK_DROPPED,
	INCOMINGDATATYPE_MINIDUMP_RECEIVED,
	INCOMINGDATATYPE_FULLDUMP_RECEIVED,
	INCOMINGDATATYPE_MEMORYDUMP_RECEIVED,
	INCOMINGDATATYPE_DUMP_DESCRIPTION_RECEIVED,
	INCOMINGDATATYPE_DUMP_CANCELLED,
	INCOMINGDATATYPE_DUMP_DESCRIPTION_UPDATE,
	INCOMINGDATATYPE_XPERF_FILE,

	INCOMINGDATATYPE_MAX
} IncomingDataType;

AUTO_STRUCT;
typedef struct IncomingData
{
	IncomingDataType eType;
	U32 id;
	U32 index;
	ErrorData *pErrorData;
	char *pTempFilename;   AST(ESTRING)
	char *pPermanentFilename; AST(ESTRING) // Used for Generic Files (eg. Xperf)
} IncomingData;

IncomingData * getNextIncomingData(void);

typedef struct IncomingClientState
{
	NetLink *link;
	char *pTempOutputFileName;
	FILE *pTempOutputFile;
	int   iCurrentLocation;
	NOCONST(DumpData) *pDeferredDumpData;
	char *pTempDescription;
	U32   uDumpID;
	U32   uDumpFlags;
	S64   startTime;
	U32 uDeferredDumpEntryID;
	
	//Used on links to slave ErrorTrackers
	bool isSlaveErrorTracker;
	U32 uDumpsToSend;
	U32 uDumpsSinceLastSend;
	U32 uCurrentlySendingDump;
	U32 context;
	StashTable initialIDs;
	StashTable masterIDtoSlaveIDTable;

	// Generic File data
	U32 uTotalBytes; // expected file size
	ErrorFileType eFileType;
} IncomingClientState;

typedef void (*IncomingDataHandler) (IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState);

void ETIncoming_SetDataTypeHandler(IncomingDataType eType, IncomingDataHandler func);
void ETIncoming_ErrorData(Packet *pak, NetLink *link, IncomingClientState *pClientState);
void ProcessIncomingData(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState);

void connectToMasterErrorTracker();
void addDumpID(int context, int ID, bool master, IncomingClientState *slaveState);

bool initIncomingSecureData(void);
bool initIncomingPublicData(void);
bool initIncomingData(void);
void shutdownIncomingData(void);

void dumpSendWait(void);

extern IncomingClientState **gActiveDumps;
void disconnectAllDumpsByID(U32 id);

void AddClientLink (NetLink *link);
void RemoveClientLink (NetLink *link);
NetLink *FindClientLink (U32 uLinkID);
