/* File MessageStore.h
 * This system is deprecated in favor of the one found in Message.h.
 */

#ifndef MESSAGESTORE_H
#define	MESSAGESTORE_H
#pragma once
GCC_SYSTEM

#include <stdio.h>
#include <stdarg.h>
#include "Array.h"
#include "MemoryPool.h"
#include "SharedHeap.h"

typedef struct MessageStore		MessageStore;
typedef struct ScriptVarsTable	ScriptVarsTable;

typedef struct MessageStoreFormatHandlerParams {
	const char* fileName;
	int			lineCount;
	
	void*		userData;
	void*		param;
	
	const char*	srcstr;
	const char*	wholemsg;
	const char*	attribstr;
	char*		outputBuffer;
	int			bufferLength;
} MessageStoreFormatHandlerParams;

typedef int (*MessageStoreFormatHandler)(MessageStoreFormatHandlerParams* params);

MessageStore* createMessageStore(int useExtendedTextMessage);
void destroyMessageStore(MessageStore* store);
SharedHeapAcquireResult initMessageStore(MessageStore* store, int locid, const char* pcSharedMemoryName );
int msAddMessages(MessageStore* store, const char* messageFilename, const char* messageTypeFilename);
void msAddMessageDirectory(MessageStore* store, char* dirname);

char* msGetUnformattedMessage(MessageStore* store, char* outputBuffer, int bufferLength, char* messageID);
	// get string from message store without any formatting

extern int msPrintfError;	// True when the messageID is unknown.
int msPrintf(MessageStore* store, char* outputBuffer, int bufferLength, const char* messageID, ...);
int msvaPrintf(MessageStore* store, char* outputBuffer, int bufferLength, const char* messageID, va_list arg);

int msPrintfVars(MessageStore* store, char* outputBuffer, int bufferLength, const char* messageID, ScriptVarsTable* vars, ...);

int msvaPrintfInternal(	MessageStore* store,
						char* outputBuffer,
						int bufferLength,
						const char* messageID, 
						Array* messageTypeDef,
						ScriptVarsTable* vars,
						va_list arg);

int msvaPrintfInternalEx(	MessageStore* store,
							char* outputBuffer,
							int bufferLength,
							const char* messageID, 
							Array* messageTypeDef,
							ScriptVarsTable* vars,
							int flags,
							va_list arg);

// Utility functions for modifying message stores at run time
int msContainsKey(MessageStore* store, const char *messageID);
void msUpdateMessage(SA_PARAM_NN_VALID MessageStore *store, SA_PARAM_NN_STR const char *messageID, SA_PARAM_NN_STR const char *currentLocaleText, SA_PARAM_OP_STR const char *comment);

// Incrementally changes a message store, by checking it out, applying any in-memory changes,
//  and then checking it back in (used for multiple people editing (appending to) the same
//  message store).
// Preserves comments
void msSaveMessageStore(MessageStore *store);

// Saves a new message store or completely overwrites an old message store.
//  No gimme operations done.
//  Prunes comments.
void msSaveMessageStoreFresh(MessageStore *store);

void msSetFilename(MessageStore *store, const char *messageFilename);

int verifyPrintable(char* message, const char* messageFilename, int lineCount);

#endif
