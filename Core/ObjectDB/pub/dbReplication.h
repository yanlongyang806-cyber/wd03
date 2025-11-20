/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef DBREPLICATION_H_
#define DBREPLICATION_H_

// Code to handle replicating information to the clone server

// Logs a transaction to the clone server
void dbLogTransaction(const char* cmd, U64 trSeq, U32 timeStamp);

// Pause at the end of a transaction to make sure the logfiles are flushed
void dbLogFlushTransactions(void);

// Updates the connection to the clone server
void dbUpdateCloneConnection(void);

// Flush stuff
int dbMasterFlush(void);

// Callback for rotating logs
void dbLogRotateCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog);
void dbLogCloseCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog);

bool dbConnectToClone(void);
bool dbConnectedToClone(void);

enum
{
	DBTOMASTER_ACK_HANDSHAKE = COMM_MAX_CMD,
	DBTOMASTER_STATUS_UPDATE,
};

extern int giCompressReplicationLink;

#endif