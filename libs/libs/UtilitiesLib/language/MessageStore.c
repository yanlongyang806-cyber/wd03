#include "MessageStore.h"
#include "fileutil.h"
#include "utils.h"
#include "strings_opt.h"
#include "error.h"
#include "stringutil.h"
#include "scriptvars.h"
#include "gimmeDLLWrapper.h"
#include "AppLocale.h"
#include "earray.h"
#include "StashTable.h"
#include "StringTable.h"
#include "structdefines.h"
#include "wininclude.h"
#include "EString.h"
#include "ScratchStack.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct{
	const char*	variableName;
	const char*	variableType;
	U8			variableTypeChar;

	// temp value - filled in during msPrintf and only valid then
	void*		variablePointer;	// Where is this thing stored on the stack?
} NamedVariableDef;

typedef struct{
	const char*		messageID;				// The message ID associated with the message.  For debugging convenience.
	int				messageIndex;			// The message index into a string table,  to be printed as double-byte strings.

	Array*			variableDefNameIndices; // An earray of indices into the namedVariableDefPool string table
											// note that the type index is always this +1
} TextMessage;

typedef struct ExtendedTextMessage {
	char			*commentLine;
	int				originalIndex; // for keeping track of changed messages so they're saved in their original order
	U32				newMessage:1;
	U32				modified:1;
} ExtendedTextMessage;


int						msPrintfError;	// True when the messageID is unknown.
static U8				UTF8BOM[] = {0xEF, 0xBB, 0xBF};
static int				hideTranslationErrors;

typedef struct MessageStore {
	int								localeID;					// locale ID corresponding to those in AppLocale.h
	StashTable 						messageIDStash;				// msg ID -> msg map.

	MemoryPool						textMessagePool;			// yep, might be overkill.
	StringTable						messages;					// actual message storage. Stored as single-byte strings.
	StringTable						variableStringTable;		// Stores variable name strings and varable type strings.  Stored as single-byte strings.

	char*							fileLoadedFrom;				// The file this store was loaded from (for saving changes)
	bool							bLocked;
	
	SharedHeapHandle*				pHandle;					// if it's in shared heap, this will be the handle to that

	MessageStoreFormatHandler		formatHandler[26][2];		// One each for upper and lower case letters.
	
	StaticDefineInt*				attributes;
	StaticDefineInt**				attribToValue;
	S32*							valueOffsetInUserData;
	
	U32								useExtendedTextMessage : 1;	// Enables extra info that's used by texWords.
} MessageStore;

//static int ParseAttrCond(MessageStore* store, char* outputBuffer, char* srcstr, void* param, char paramType, char* wholemsg, char* filename, int linecount);

MessageStore* createMessageStore(int useExtendedTextMessage){
	MessageStore* store = calloc(1, sizeof(*store));
	
	store->useExtendedTextMessage = !!useExtendedTextMessage;
	
	return store;
}

// How fitting that we should use a string table to keep track of
// which string tables we have already shared
StringTable locallySharedStringTables;


// Once we share a string table, we add it to a list and check it in the future
// so that we can never try to share the same table twice locally...
//
// this is so that if you reload a message store, you keep your local copy, to prevent
// confusion across processes when there is a change in the data
//
// should ALWAYS return false when in production mode
static bool alreadySharedLocally(const char* pcSharedStringTableHashKey)
{
	if ( locallySharedStringTables )
	{
		int i;
		for (i=0; i<strTableGetStringCount(locallySharedStringTables); ++i)
		{
			if ( stricmp( pcSharedStringTableHashKey, strTableGetConstString(locallySharedStringTables, i ) ) == 0 )
			{
				// we are already in the table, return true!

				if ( isProductionMode() )
					assert( "Should never reload messagestore in productionmode" == 0);
				return true;
			}
		}

	}
	else // init the table
	{
		locallySharedStringTables = strTableCreate(Indexable, 128);
	}

	// We got here, meaning that we are not in the table.. add us and return false
	assert( locallySharedStringTables );
	strTableAddString(locallySharedStringTables, pcSharedStringTableHashKey);

	return false;
}

void destroyTextMessage(TextMessage* textMessage){
	destroyArray(textMessage->variableDefNameIndices);
	textMessage->variableDefNameIndices = NULL;
}

void destroyMessageStore(MessageStore* store)
{
	if(!store){
		return;
	}
	
	if ( store->pHandle ) // otherwise, its in shared memory
	{
		sharedHeapMemoryManagerLock();
		sharedHeapRelease(store->pHandle);
		sharedHeapMemoryManagerUnlock();
		store->pHandle = NULL;
	}
	else
	{
		if(store->messageIDStash)
			stashTableDestroyEx(store->messageIDStash, NULL, destroyTextMessage);
		if(store->textMessagePool)
			destroyMemoryPool(store->textMessagePool);
		if(store->messages)
			destroyStringTable(store->messages);
		if(store->variableStringTable)
			destroyStringTable(store->variableStringTable);
	}

	SAFE_FREE(store->fileLoadedFrom);

	SAFE_FREE(store);
}


/* Function readEnclosedString()
 *	Extract a string that is enclosed by the specified characters.
 *	This function destroys the given string.
 */
char* readEnclosedString(char** input, char openDelim, char closeDelim){
	char openDelimStr[2];
	char closeDelimStr[2];
	char* beginEnclosed;

	openDelimStr[0] = openDelim;
	openDelimStr[1] = '\0';

	closeDelimStr[0] = closeDelim;
	closeDelimStr[1] = '\0';

	if(beginEnclosed = strsep(input, openDelimStr)){
		char* token;
		token = strsep(input, closeDelimStr);
		return token;
	}

	return NULL;
}

char* readQuotedString(char** input){
	return readEnclosedString(input, '\"', '\"');
}

void eatWhitespace(char** str){
	while(**str == ' ')
		(*str)++;
}


static void setStorePointersToSharedHeap(MessageStore* store)
{
	void** pTargetAddress = store->pHandle->data;

	void* pVariableStrTableAddress = pTargetAddress[0];
	void* pIdStashTableAddress = pTargetAddress[1];
	void* pMessageStrTableAddress = &pTargetAddress[2]; // just past the header (size 2*sizeof(void*))

	// set the old elements
	store->messageIDStash = pIdStashTableAddress;
	store->messages = pMessageStrTableAddress;
	store->variableStringTable = pVariableStrTableAddress;
}

SharedHeapAcquireResult initMessageStore(MessageStore* store, int locid, const char* pcSharedMemoryName )
{
	S32 textMessageSize =	sizeof(TextMessage) +
							(store->useExtendedTextMessage ? sizeof(ExtendedTextMessage) : 0);
	
	// Initialize message store internal variables.
	#define EXPECTED_MESSAGE_COUNT 100

	// Do we try to place this in shared memory?
	bool bAttemptToShare = (pcSharedMemoryName != NULL);
	SharedHeapAcquireResult ret = SHAR_Error;

	// Create the TextMessage pool.
	
	store->textMessagePool = createMemoryPool();
	initMemoryPool(	store->textMessagePool, textMessageSize, EXPECTED_MESSAGE_COUNT);
	mpSetMode(store->textMessagePool, ZERO_MEMORY_BIT);

	if ( bAttemptToShare )
	{
		// First, get the name right
		char cSharedName[128];
		STR_COMBINE_SSD(cSharedName, pcSharedMemoryName, "-", locid);

		// We don't want to try to share something we already shared, and are reloading
		if ( !alreadySharedLocally(cSharedName))
			ret = sharedHeapAcquire(&store->pHandle, cSharedName);

		if ( ret == SHAR_DataAcquired )
		{
			setStorePointersToSharedHeap(store);
		}
		else
		{
			// either way, we need to create a local copy first
			store->messageIDStash = stashTableCreateWithStringKeys(EXPECTED_MESSAGE_COUNT, StashDeepCopyKeys_NeverRelease);
			store->messages = strTableCreate(Indexable, 128);
			store->variableStringTable = strTableCreate(Indexable, 128);
		}
	}
	else
	{
		store->messageIDStash = stashTableCreateWithStringKeys(EXPECTED_MESSAGE_COUNT, StashDeepCopyKeys_NeverRelease);
		store->messages = strTableCreate(Indexable, 128);
		store->variableStringTable = strTableCreate(Indexable, 128);
	}


	#undef EXPECTED_MESSAGE_COUNT

	store->localeID = locid;
	
	return ret;
}

// returns 1 if message doesn't contain any weird upper ASCII characters
int verifyPrintable(char* message, const char* messageFilename, int lineCount)
{
	U8* str = message;
	if (isProductionMode()) return 1; // don't do in depth searching for production servers
	while (*str)
	{
		U8 c = *str;
		
		if (c <= 127)
		{
			if ((c < 'a' || c > 'z')
				&&
				(c < 'A' || c > 'Z')
				&&
				(c < '0' || c > '9')
				&&
				!strchr("!@#$%^&*()-_=+[]{|};:',<.>/?\" ~\r\n\t\\", c))
			{
				ErrorFilenamef(messageFilename, "Bad character '%c' (%i) in \"%s\", line %i in file %s\n", c, (str-(U8*)message)+1, message, lineCount, messageFilename);
				return 0;
			}
		}
		str = UTF8GetNextCodepoint(str);
	}
	return 1;
}

ExtendedTextMessage* getExtendedMessage(MessageStore* store, const TextMessage* msg){
	return store->useExtendedTextMessage ? (ExtendedTextMessage* )(msg + 1) : NULL;
}

static void msSetMessageIsModified(MessageStore* store, TextMessage* msg, int modified){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);

	if(extMsg){
		extMsg->modified = !!modified;
	}
}

static int msGetMessageIsModified(MessageStore* store, const TextMessage* msg){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);

	return SAFE_MEMBER(extMsg, modified);
}

static void msSetMessageOriginalIndex(MessageStore* store, TextMessage* msg, int originalIndex){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);

	if(extMsg){
		extMsg->originalIndex = originalIndex;
	}
}

static int msGetMessageOriginalIndex(MessageStore *store, const TextMessage *msg){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);

	if (extMsg) {
		return extMsg->originalIndex;
	}
	return msg->messageIndex;
}

static void msSetMessageIsNew(MessageStore* store, TextMessage* msg, int newMessage){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);

	if(extMsg){
		extMsg->newMessage = !!newMessage;
	}
}

static int msGetMessageIsNew(MessageStore* store, const TextMessage* msg){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);

	return SAFE_MEMBER(extMsg, newMessage);
}


static TextMessage *msAddMessage(MessageStore *store, const char *messageID, const char *messageText)
{
	TextMessage* textMessage;
	bool bSuccess;

	assert(!store->bLocked); // this message store is locked, perhaps because it was put in shared memory. you are adding to this too late

	// Create a text message to hold text message related stuff.
	textMessage = mpAlloc(store->textMessagePool);

	// Add this message to the messageID hash table so we can look this message up by ID later.
	if (!stashAddPointer(store->messageIDStash, messageID, textMessage, false))
		return NULL;

	// Store a pointer back to the messageID in the text message structure so we know what the
	// ID is when debugging.
	bSuccess = stashGetKey(store->messageIDStash, messageID, &textMessage->messageID);
	assert(bSuccess); // we just added it, it better be there

	// Convert the message into a wide character format, place it in the message store's message
	// string table, then store the message itself into the text message structure.
	//textMessage->message = strTableAddString(store->messages, (void*)UTF8ToWC(message));
	textMessage->messageIndex = strTableAddStringGetIndex(store->messages, messageText);
	msSetMessageOriginalIndex(store, textMessage, textMessage->messageIndex);

	return textMessage;
}

static MessageStoreFormatHandler getFormatHandler(MessageStore* store, char character)
{
	U32 isUpper = character >= 'A' && character <= 'Z';
	U32 index = character - (isUpper ? 'A' : 'a');
	
	if(store && index <= 'Z' - 'A'){
		return store->formatHandler[index][isUpper];
	}
	
	return NULL;
}

// Parses ContactDef/Entity/DB_ID based info and everything that has attribs or conditionals
// returns 1 if parsing succeeded, 0 if it failed
// Note: srcstr is not conserved
// if outputBuffer is NULL, just parses the srcstr for correctness
static int ParseAttrCond(	MessageStore*	store,
							char*			outputBuffer,
							int				bufferLength,
							char*			srcstr,
							void*			param,
							char			paramType,
							char* 			wholemsg,
							const char* 	filename,
							int				linecount)
{
	MessageStoreFormatHandler	formatHandler;
	S32							foundAttr=0;
	U8							userDataBuffer[1000];

	char *attribstr = 0, *valueToCompareTo, *outputIfTrue, *outputIfFalse;
	char *parse=srcstr, *temp;
	
	if(outputIfFalse = strchr(srcstr, '|'))	// be at least somewhat flexible with spaces around the | else marker
	{
		temp = outputIfFalse;
		do {
			*temp = '\0';
		} while(*(--temp) == ' ');
		do {
			outputIfFalse++;
		} while(*outputIfFalse == ' ');
	}
	if(valueToCompareTo = strchr(srcstr, '='))
	{
		temp = valueToCompareTo;
		do{ *temp = '\0'; }while(*(--temp) == ' ');
		do{ valueToCompareTo++; }while(*valueToCompareTo == ' ');
		parse = valueToCompareTo;
	}
	if(outputIfTrue = strchr(parse, ' '))
	{
		*outputIfTrue++ = '\0';
		parse = outputIfTrue;
	}
	if((paramType != 's') && (attribstr = strchr(srcstr, '.')))	//check for attributes
	{
		*attribstr++ = '\0';
		parse = attribstr;
	}

	if(!outputBuffer && filename)
	{
		if(valueToCompareTo && !outputIfTrue)
		{
			ErrorFilenamef(filename, "Found conditional but no clause to print based on the outcome, file %s, message %s, line %i", filename, wholemsg, linecount);
			if(outputBuffer)
				outputBuffer[0] = '\0';
			return 0;
		}

		if(outputIfFalse && !(outputIfTrue && valueToCompareTo))
		{
			ErrorFilenamef(filename, "Found | but no text if conditional is true, or no conditional test, file %s, message %s, line %i", filename, wholemsg, linecount);
			if(outputBuffer)
				outputBuffer[0] = '\0';
			return 0;
		}

		if (attribstr)
		{
			ANALYSIS_ASSUME(attribstr);
			if (strchr(attribstr, ' '))
			{
				ErrorFilenamef(filename, "Found an illegal space, file %s, message %s, line %i", filename, wholemsg, linecount);
				if(outputBuffer)
					outputBuffer[0] = '\0';
				return 0;
			}
		}
	}
	
	formatHandler = getFormatHandler(store, paramType);
	
	if(formatHandler){
		MessageStoreFormatHandlerParams params = {0};
		
		params.fileName = filename;
		params.lineCount = linecount;
		
		params.userData = userDataBuffer;
		params.param = param;
		
		params.srcstr = srcstr;
		params.wholemsg = wholemsg;
		params.attribstr = attribstr;
		params.outputBuffer = outputBuffer;
		params.bufferLength = bufferLength;
		
		formatHandler(&params);
	}else{
		switch(paramType)
		{
			xcase 's':
				if(outputBuffer)
					strcpy_s(outputBuffer, bufferLength, (char*)param);
			xdefault:
				if(filename)
					ErrorFilenamef(filename, "Unknown paramType in ParseAttrCond while parsing %s, file %s, message %s, line %i", srcstr, filename, wholemsg, linecount);
				else
					Errorf("Unknown paramType in ParseAttrCond while parsing %s", srcstr);
				if(outputBuffer)
					outputBuffer[0] = '\0';
				return 0;
		}
	}

	if(attribstr)
	{
		foundAttr = StaticDefineIntGetInt(store->attributes, attribstr);
		
		if(foundAttr >= 0){
			if(outputBuffer){
				S32 value = *(S32*)(userDataBuffer + store->valueOffsetInUserData[foundAttr]);
				
				strcpy_s(outputBuffer, bufferLength, StaticDefineIntRevLookup(store->attribToValue[foundAttr], value));
			}
		}else{
			if(filename)
				ErrorFilenamef(filename, "Unsupported attribute value %s, file %s, message %s, line %i", attribstr, filename, wholemsg, linecount);
			else
				Errorf("Unsupported attribute value %s", attribstr);
			if(outputBuffer)
				outputBuffer[0] = '\0';
			return 0;
		}
	}

	// TODO: conditionals for %s etc
	if(valueToCompareTo)
	{
		if(attribstr && (StaticDefineInt_FastStringToInt(store->attribToValue[foundAttr], valueToCompareTo, INT_MIN) == INT_MIN))
		{
			if(filename)
				ErrorFilenamef(filename, "Unsupported conditional value %s, file %s, message %s, line %i", valueToCompareTo, filename, wholemsg, linecount);
			else
				Errorf("Unsupported conditional value %s", valueToCompareTo);
			if(outputBuffer)
				outputBuffer[0] = '\0';
			return 0;
		}
		if(!outputIfTrue)
		{
			if(filename)
				ErrorFilenamef(filename, "Found conditional but no rest; String: %s, Conditional: %s, file %s, message %s, line %i", srcstr, valueToCompareTo, filename, wholemsg, linecount);
			else
				Errorf("Found conditional but no rest; String: %s", valueToCompareTo);
			if(outputBuffer)
				outputBuffer[0] = '\0';
			return 0;
		}

		if(outputBuffer){
			const char* output = "";
			
			if(!stricmp(outputBuffer, valueToCompareTo)){
				output = outputIfTrue;
			}
			else if(outputIfFalse){
				output = outputIfFalse;
			}
			
			strcpy_s(outputBuffer, bufferLength, output);
		}
	}

	if(outputBuffer){
		for(parse = outputBuffer + strlen(outputBuffer) - 1;
			parse >= outputBuffer && *parse == ' ';
			parse--);
		
		if(parse[1] == ' '){
			parse[1] = 0;
		}
	}

	return 1;
}


//TODO: clean up this mess
static int loadMessageData(MessageStore* store,char *messageFileContents, const char *messageFilename)
{
	int lineCount = 1;
	const char *pchCur;
	char *cur, *varstart, *varend;
	bool verifyCharacters=!store->localeID && (!messageFilename || !strstriConst(messageFilename, "texture")); // Some textures have spanish text, etc

	char *remain;
	int len;
	static char *buf = 0;

	estrCreate(&buf);	// buffer for validity checking

	SAFE_FREE(store->fileLoadedFrom);
	store->fileLoadedFrom = strdup(messageFilename);

	if(!messageFileContents)
		return 0;

	pchCur = messageFileContents;

	// Are we really reading an UTF8 file?
	// If not, do not initialize the message store.
	if(0 != memcmp(pchCur, UTF8BOM, 3))
	{
		// it's ok not to have UTF8 files...
//		ErrorFilenamef(messageFilename, "Unable to read file \"%s\" because it is not a UTF8 file!\n", messageFilename);
//		return 0;
	} else {
		// Read and remove the UTF8 byte order marker.
		pchCur += 3;
	}

	//
	// Message files are made up of MessageID-Message pairs.
	// The MessageID appears first on the line and must start on the
	//   first column of the line. It must have double-quotes (") around
	//   it and cannot contain double-quotes.
	// The Message follows the MessageID and can either be double-quoted
	//   or quoted with << >>.
	// If double-quoted, the entire message must be on the same line; it
	//   cannot contain \r or \n. It may contain "s. The last double-quote
	//   found on the line is used to delimit  the string.
	// Messages quoted with << and >> may span multiple lines and may
	//   also contain "s.
	//
	// Blank lines and lines starting with // and # are ignored.
	//
	// Example (ignore the comment prefix :-)
	//
	// # Message file
	// "str1" "regular"
	// "str2" "has "embedded" double-quotes(")"
	// "str3" <<Uses special "s on one line>>
	// # I am a comment!
	// "str4" <<Uses
	//   multiple
	//   lines just because it's fun>>
	// "str5" "<<has the left and right chevrons in the messages>>"
	// # End fo message file
	//

	while(*pchCur != 0)
	{
		static StuffBuff message;
		static StuffBuff messageID;
		static StuffBuff helpMessage;
		char *pchNextLine = NULL;
		char *pchEOL;

		// Init the message and messageID scratchpads.
		if(!message.buff)
		{
			initStuffBuff(&message, 512);
			initStuffBuff(&messageID, 512);
			initStuffBuff(&helpMessage, 512);
		}
		else
		{
			clearStuffBuff(&message);
			clearStuffBuff(&messageID);
			clearStuffBuff(&helpMessage);
		}

		// Look for the end of this line. EOLs must contain an \r. \n
		// is optional.
		pchEOL = strchr(pchCur, '\r');
		if(pchEOL)
		{
			pchNextLine = pchEOL+1;
			while(*pchNextLine=='\r' || *pchNextLine=='\n')
			{
				pchNextLine++;
				if(*pchNextLine=='\r')
					lineCount++;
			}
		}
		else
		{
			pchEOL = pchNextLine = (char*)pchCur+strlen((char*)pchCur);
		}

		if(pchCur[0]=='/' && pchCur[1]=='/')
		{
			// Skip comment lines
			goto nextline;
		}
		else if(pchCur[0]=='#')
		{
			// Skip comment lines
			goto nextline;
		}
		else if(pchCur[0]=='\r')
		{
			// Blank line
			goto nextline;
		}
		else if(pchCur[0]!='"')
		{
			if (!hideTranslationErrors)
				ErrorFilenamef(messageFilename, "Unable to find MessageID on line %d in file %s\n", lineCount, messageFilename);
			goto nextline;
		}
		else
		{
			// OK, We got a ", get the MessageID
			char *pchEnd;

			// Skip to the next "
			pchCur++;
			pchEnd = strchr(pchCur, '"');
			if(!pchEnd || pchEnd>pchEOL)
			{
				if (!hideTranslationErrors)
					ErrorFilenamef(messageFilename, "Unable to find MessageID terminator on line %d in file %s\n", lineCount, messageFilename);
				goto nextline;
			}

			// Got the messageID.
			addBinaryDataToStuffBuff(&messageID, pchCur, pchEnd-pchCur);
			addBinaryDataToStuffBuff(&messageID, "\0", 1);

			// Skip forward to the next " or <
			pchCur = pchEnd+1;
			while(pchCur[0]!='\"' && (pchCur[0]!='<' || pchCur[1]!='<') && pchCur<pchEOL)
				pchCur++;

		if(pchCur[0]=='\"')
			{
				// Scan to the end of the line and truncate the string
				// after the last "
				char *pchLastQuote = NULL;
				int iCntExtra=0;
				
				pchCur++;
				pchEnd = (char*)pchCur;
				while(pchEnd<pchEOL)
				{
					if(*pchEnd=='\"')
					{
						pchLastQuote = pchEnd;
						iCntExtra = 0;
					}
					else if(*pchEnd!=' ' && *pchEnd!='\t' && *pchEnd!='\r' && *pchEnd!='\n')
					{
						iCntExtra++;
					}
					pchEnd++;
				}
				
				if(pchLastQuote==NULL)
				{
					if (!hideTranslationErrors)
						ErrorFilenamef(messageFilename, "No terminator for MessageID: \"%s\" found on line %i in file %s\n", messageID.buff, lineCount, messageFilename);
					goto nextline;
				}
				else if(iCntExtra>0)
				{
					if (!hideTranslationErrors)
						ErrorFilenamef(messageFilename, "Warning: Extra characters after message for MessageID: \"%s\" found on line %i in file %s\n", messageID.buff, lineCount, messageFilename);
				}
				
				// Got the message
				addBinaryDataToStuffBuff(&message, pchCur, pchLastQuote-pchCur);
				addBinaryDataToStuffBuff(&message, "\0", 1);
			}
			else if(pchCur[0]=='<' && pchCur[1]=='<')
			{
				pchCur+=2;
				
				// Scan forward for the >>
				// Strings within << >> delimiters can cross line boundaries.
				pchEnd = strchr(pchCur, '>');
				while(pchEnd && pchEnd[1]!='>')
				{
					pchEnd = strchr(pchEnd + 1, '>');
				}
				if(!pchEnd)
				{
					ErrorFilenamef(messageFilename, "Unable to find >> terminator for MessageID \"%s\" on line %d in file %s\n", messageID.buff, lineCount, messageFilename);
					goto nextline;
				}
				
				// Got the message
				addBinaryDataToStuffBuff(&message, pchCur, pchEnd-pchCur);
				addBinaryDataToStuffBuff(&message, "\0", 1);
				
				// Fix up the line count
				while(pchCur<pchEnd)
				{
					if(*pchCur=='\r')
						lineCount++;
					pchCur++;
				}
				
				// Skip to the end of the line
				pchNextLine = NULL;
				pchEOL = strchr(pchCur, '\r');
				if(pchEOL)
				{
					pchNextLine = pchEOL;
					do {
						pchNextLine++;
					} while(*pchNextLine=='\r' || *pchNextLine=='\n');
				}
			}
			else
			{
				if (!hideTranslationErrors)
					ErrorFilenamef(messageFilename, "Unable to find message for MessageID \"%s\" on line %d in file %s\n", messageID.buff, lineCount, messageFilename);
				goto nextline;
			}
			
			// If we get here, we have a messageID and a message.
			
			// Check if braces match
			for(cur = message.buff; 1; cur = varend + 1)
			{
				varstart = strchr(cur, '{');
				varend = strchr(cur, '}');
				
				if(!varstart && !varend)
					break;
				
				if((varstart && !varend) || (!varstart && varend) || (varstart > varend) || ((varstart = strchr(varstart+1, '{')) && varstart < varend))
				{
					if (!hideTranslationErrors)
						ErrorFilenamef(messageFilename, "Found a {} mismatch in messageID %s in file %s, line %i", messageID.buff, messageFilename, lineCount);
					goto nextline;
				}
			}
			
			// Check for valid attributes/conditionals
			for(remain = message.buff; varstart = strchr(remain, '{'); remain = varend + 1)
			{
				varend = strchr(varstart, '}');
				len = varend - varstart - 1;
				estrClear(&buf);
				estrConcat(&buf, varstart + 1, len);
				if(!ParseAttrCond(store, NULL, 0, buf, buf, 's', messageID.buff, messageFilename, lineCount))
					ErrorFilenamef(messageFilename, "The previous error occurred in messageID %s in file %s, line %i", messageID.buff, messageFilename, lineCount);
			}
			
			// Add the message to the list.
			{
				const TextMessage* textMessage;
				
				// Make sure the message ID does not already exist.
				if(stashFindPointerConst(store->messageIDStash, messageID.buff, &textMessage))
				{
					if(0 != strcmp(strTableGetConstString(store->messages, textMessage->messageIndex), message.buff))
					{

						// If the message ID already exists, print out an error message and ignore the new message.
						if (!hideTranslationErrors)
							ErrorFilenamef(messageFilename, "Duplicate MessageID: \"%s\" found on line %i in file %s\n", messageID.buff, lineCount, messageFilename);
					}
					goto nextline;
				}
				
				// make sure string doesn't include unprintable ASCII characters
//				if (!verifyPrintable(message.buff, messageFilename, lineCount))
//					goto nextline;
				if(verifyCharacters)	// using the default (english) locale so check for weird chars
					verifyPrintable(message.buff, messageFilename, lineCount);
				
				msAddMessage(store, messageID.buff, message.buff);
			}
		}
		
	nextline:
		pchCur = pchNextLine;
		lineCount++;
	}
	return 1;
}

static void addTextMessageDefNameIndex(TextMessage* textMessage, int index)
{
	if(textMessage)
	{
		if(!textMessage->variableDefNameIndices)
		{
			textMessage->variableDefNameIndices = createArray();
		}
		
		arrayPushBack(textMessage->variableDefNameIndices, (void*)(intptr_t)index);
	}
}

static int loadMessageTypeData(MessageStore* store,char* messageTypeData, const char *messageTypeFilename)
{
	char lineBuffer[1024];
	char* parseCursor;
	int lineCount = 0;
	intptr_t len;
	char* messageID;
	char* variableName;
	char* variableType;
	char* curr;
	TextMessage* textMessage, *v_textMessage;
	char * end;
	
	if(!messageTypeData)
		return 0;
	
	end=messageTypeData+strlen(messageTypeData);
	for(curr = messageTypeData;(curr<end) && (len = strcspn(curr,"\n"));curr += len+1)
	{
		char v_messageID[1024];
		assert(len < ARRAY_SIZE(lineBuffer));
		memcpy(lineBuffer, curr, len);
		lineBuffer[len] = 0;
		if (lineBuffer[len-1] == '\r')
			lineBuffer[len-1] = 0;
		lineCount++;
		parseCursor = lineBuffer;
		
		if(lineBuffer[0] == '#') // at least TRY to get rid of commented out strings
			continue;
		
		// Assume that every line always starts with a quoted string.
		// Extract the quoted string.  This is the message ID.
		messageID = readQuotedString(&parseCursor);
		if(!messageID)
			continue;

		STR_COMBINE_SS(v_messageID, "v_", messageID);

		if (!stashFindPointer(store->messageIDStash, messageID, &textMessage))
			textMessage = NULL;
		if (!stashFindPointer(store->messageIDStash, v_messageID, &v_textMessage))
			v_textMessage = NULL;

		if( textMessage || v_textMessage )// Make sure the message ID exists.
		{
			// For each pair of strings enclosed in { and }...
			while(variableType = readEnclosedString(&parseCursor, '{', '}'))
			{
				int iVariableNameIndex, iVariableTypeIndex;
				
				// Get the variable name and the expected type.
				variableName = strsep(&variableType, ",");
				
				eatWhitespace(&variableName);
				eatWhitespace(&variableType);
				
				iVariableNameIndex = strTableAddStringGetIndex(store->variableStringTable, variableName);
				iVariableTypeIndex = strTableAddStringGetIndex(store->variableStringTable, variableType);

				assert( iVariableTypeIndex == iVariableNameIndex + 1 );
				

				if(v_textMessage && !v_textMessage->variableDefNameIndices){
					v_textMessage->variableDefNameIndices = createArray();
				}
				
				addTextMessageDefNameIndex(textMessage, iVariableNameIndex);
				addTextMessageDefNameIndex(v_textMessage, iVariableNameIndex);
			}
		}
		else if( !textMessage )
		{
			// If the message ID does not exist, print out an error message and proceed to the next message.
			if (!hideTranslationErrors)
				ErrorFilenamef(messageTypeFilename, "Unknown MessageID: \"%s\" found on line %i in file %s\n", messageID, lineCount, messageTypeFilename);
		}
	}
	return 1;
}


int msAddMessages(MessageStore* store, const char* messageFilename, const char* messageTypeFilename)
{
	char	*messageData,*messageTypes;
	int		ret;
	
	messageData = fileAlloc(messageFilename, 0);
	ret = loadMessageData(store,messageData,messageFilename);
	free(messageData);
	if (!ret)
		return 0;
	
	messageTypes = fileAlloc(messageTypeFilename, 0);
	if (messageTypes) {
		ret = loadMessageTypeData(store,messageTypes,messageTypeFilename);
		free(messageTypes);
	}
	return ret;
}

/* Function msPrintf()
 *	Parameters:
 *		store - an initialized message store where the specified message can be found.
 *		outputBuffer - a valid string buffer where the produced string will be printed.
 *		messageID - a UTF-8 string that will identify a single string in the given store.
 *		... - parameters required by printf call.
 *
 *	Returns:
 *		-1 - The message cannot be printed because the messageID is unknown to the given store.
 *		valid number - the number of characters printed into the output buffer.
 *		or
 *		valid number -	the number of bytes required to store the result, excluding the NULL terminator
 *						if the outputBuffer is NULL.
 */
int msPrintf(MessageStore* store, char* outputBuffer, int bufferLength, const char* messageID, ...){
	int result;
	
	VA_START(arg, messageID);
	result = msvaPrintfInternal(store, outputBuffer, bufferLength, messageID, NULL, NULL, arg);
	VA_END();
	
	return result;
}

int msPrintfVars(MessageStore* store, char* outputBuffer, int bufferLength, const char* messageID, ScriptVarsTable* vars, ...){
	int result;
	
	VA_START(arg, vars);
	result = msvaPrintfInternal(store, outputBuffer, bufferLength, messageID, NULL, vars, arg);
	VA_END();
	
	return result;
}

// va versions
int msvaPrintf(MessageStore* store, char* outputBuffer, int bufferLength, const char* messageID, va_list arg){
	return msvaPrintfInternal(store, outputBuffer, bufferLength, messageID, NULL, NULL, arg);
}

static int vsprintfInternal(char* outputBuffer, int bufferLength, const char* format, va_list arg)
{
	if(!outputBuffer)
	{
		return _vscprintf(format, arg);
	}
	else
	{
		return vsprintf_s(outputBuffer, bufferLength, format, arg);
	}
}

// get string from message store without any formatting
char* msGetUnformattedMessage(	MessageStore* store,
								char* outputBuffer,
								int bufferLength,
								char* messageID)
{
	const TextMessage* textMessage;
	
	if(!store)
	{
		msPrintfError = 1;
		strcpy_s(outputBuffer, bufferLength, messageID);
		return messageID;
	}
	
	// Look up the message to be printed.
	if (!stashFindPointerConst(store->messageIDStash, messageID, &textMessage))
		textMessage = NULL;
	
	// If the message cannot be found, print the messageID itself.
	if(!textMessage){
		msPrintfError = 1;
		strcpy_s(outputBuffer, bufferLength, messageID);
		return messageID;
	}

	// found it
	strcpy_s(outputBuffer, bufferLength, strTableGetConstString(store->messages, textMessage->messageIndex));
	
	return outputBuffer;
}

// put a temp pointer in with each type definition
static void msGetTypeDefPointers(Array* messageTypeDef, va_list arg)
{
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	// FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME
	//
	// THIS FUNCTION IS AN INCREDIBLE HACK AND DOES NOT REALLY WORK.
	// THE ONLY REASON I AM NOT REWRITING IT IS BECAUSE WE NEED IT TO WORK
	// FOR FC PPM7 AND BECAUSE WE ARE REMOVING THIS ENTIRE MESSAGE STORE
	// SYSTEM SOON. IF YOU USE THIS AND IT BREAKS, YOU'RE ON YOUR OWN.
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
#ifdef _XBOX
	#define va_advance(ap, byteCount) (ap += 8)
#else
	#define va_advance(ap, byteCount) (ap += byteCount)
#endif
	int i;

	if (!messageTypeDef) return; // ok

	for(i = 0; i < messageTypeDef->size; i++)
	{
		NamedVariableDef* def;
		int variableTypeSize = sizeof(int);
		def = messageTypeDef->storage[i];

		// Different variable types occupy different amount of stack space.
		// How much space does this kind of variable take up?
		
		switch(def->variableTypeChar){
			// Doubles are 8 bytes long.
			case 'f':
			case 'g':
			case 'G':
				variableTypeSize = sizeof(double);
				break;
		}
		def->variablePointer = arg;
		va_advance(arg, variableTypeSize);
	}

	#undef va_advance
}

static void getAttribSeparatorAndCompareValue(	char* param,
												char** attribSeparatorPtr,
												char** equalSignPtr)
{
	char* attribSeparator = 0;
	char* equalSign = 0;

	if(attribSeparator = strchr(param, '.'))
	{
		param = attribSeparator + 1;
	}
	if(equalSign = strchr(param, '='))
	{
		param = equalSign;
		do{ *param = '\0'; }while(*--param == ' ');
	}
	
	*attribSeparatorPtr = attribSeparator;
	*equalSignPtr = equalSign;
}

// replace a particular {Param} found in string
static void msPrintParam(	const char* messageID,
							char* outputBuffer,
							int bufferLength,
							char* param,
							MessageStore* store,
							Array* messageTypeDef,
							ScriptVarsTable* vars,
							va_list arg)
{
	int i;
	char* attribSeparator;
	char* equalSign;
	
	getAttribSeparatorAndCompareValue(param, &attribSeparator, &equalSign);
	
	// look for the param in the message types first
	for(i = 0; i < messageTypeDef->size; i++){
		NamedVariableDef*			def = messageTypeDef->storage[i];
		MessageStoreFormatHandler	formatHandler = getFormatHandler(store, def->variableTypeChar);
		const char*					type;

		if(attribSeparator){
			if(formatHandler){
				*attribSeparator = '\0';
			}else{
				*attribSeparator = '.';
			}
		}
		
		// Not a match
		if (stricmp(def->variableName, param))
		{
			continue;
		}
		
		type = def->variableType;
		
		// reference parameters need another lookup through the message store
		if(def->variableTypeChar == 'r')
		{
			char*	keyString = *(char**)def->variablePointer;
			int		validString = 1;
			
			assert(!formatHandler);
			
			// MS: Exception wrapper to prevent a bad string parameter from causing an access violation.
#if _PS3
#else
			__try
			{
				int len = (int)strlen(keyString);
				UNUSED(len);
			}
#pragma warning(suppress:6320)		//Exception-filter is the constant...
			__except(validString = 0, EXCEPTION_EXECUTE_HANDLER)
#pragma warning(suppress:6322)		//Empty _except block...
			{

			}
#endif

			if(validString)
			{
				const TextMessage* referencedMessage;
				
				if(!stashFindPointerConst(store->messageIDStash, keyString, &referencedMessage)){
					referencedMessage = NULL;
				}

				if(referencedMessage)
				{
					*(const char**)def->variablePointer = strTableGetConstString(store->messages, referencedMessage->messageIndex);
				}
			}
			else
			{
				*(char**)def->variablePointer = "Invalid String Parameter";
				
				Errorf("Bad parameter for: %s:%s", messageID, def->variableName);
			}
			
			type = "%s";
		}

		if(attribSeparator)
			*attribSeparator = '.';	// put the period back to let ParseAttrCond() parse the string itself
		if(equalSign)
			*equalSign = '=';

		if(formatHandler){
			ParseAttrCond(store, outputBuffer, bufferLength, param, *(void**)def->variablePointer, def->variableTypeChar, NULL, NULL, 0);
		}else{
			vsprintf_s(outputBuffer, bufferLength, type, def->variablePointer);
			if(equalSign)
				ParseAttrCond(store, outputBuffer, bufferLength, outputBuffer, outputBuffer, 's', NULL, NULL, 0);
		}

		return;
		// complete
	}

	// otherwise, try a variable lookup through the vars table
	if (vars)
	{
		char* replace;
		char vartype;

		replace = ScriptVarsTableLookupTyped(vars, param, &vartype);

		if(attribSeparator && replace == param)
		{
			*attribSeparator = '\0';
			replace = ScriptVarsTableLookupTyped(vars, param, &vartype);
		}

		if (replace != param) // script vars need a recursive call through msPrintf for further replacement
		{
			if(equalSign)
				*equalSign = '=';
			if(attribSeparator)
				*attribSeparator = '.';

			switch(vartype)
			{
				xcase 's':
					msPrintfVars(store, outputBuffer, bufferLength, replace, vars);
					msPrintfError = 0;
					if(equalSign)
						ParseAttrCond(store, outputBuffer, bufferLength, outputBuffer, outputBuffer, 's', NULL, NULL, 0);

				xdefault:
					ParseAttrCond(store, outputBuffer, bufferLength, param, (void*)replace, vartype, NULL, NULL, 0);
			}
			return;
		}
	}

	// just print bad param if we failed
    strcpy_s(outputBuffer, bufferLength, param);
}

int msvaPrintfInternal(MessageStore* store, char* outputBuffer, int bufferLength, const char* messageID, Array* messageTypeDef, ScriptVarsTable* vars, va_list arg)
{
	return msvaPrintfInternalEx(store, outputBuffer, bufferLength, messageID, messageTypeDef, vars, 0, arg);
}

// ------------------------------------------------------------
// Special Korean handling

// http://code.cside.com/3rdpage/us/unicode/converter.html - used to convert

// Hangul syllables
#define HANGUL_PRECOMP_BASE      0xAC00
#define HANGUL_PRECOMP_MAX       0xD7A3
#define NUM_JONGSEONG            28  // (T) Trailing Consonants
 
// return true if the given char is a precomposed Hangul syllable
bool CharIsHangulSyllable(wchar_t wch) 
{
    return (wch >= HANGUL_PRECOMP_BASE && wch <= HANGUL_PRECOMP_MAX) ? true : false;
}

// prev char is previous char of this fixup
// if (CharIsHangulSyllable(prevChar)) {
//     koreanVowelSound = !((prevChar - HANGUL_PRECOMP_BASE) % NUM_JONGSEONG);
//     buffer->Add(fixup[koreanVowelSound ? 1 : 0]);
// }

// Special Korean handling
// ------------------------------------------------------------

static bool s_CharIsPostPositionable(wchar_t prevChar)
{
	return CharIsHangulSyllable( prevChar ) || 
		(prevChar <= '9' && prevChar >= '0') ||
		(prevChar <= 'z' && prevChar >= 'A'); 
}

static bool s_ChoosePostposition(wchar_t prevChar,int *pIsKoreanVowelSound, char *postposVars)
{	
	bool res = false;
	
	if( s_CharIsPostPositionable( prevChar ) )
	{
		int koreanVowelSound = !((prevChar - HANGUL_PRECOMP_BASE) % NUM_JONGSEONG);
// 		char *copyPos = variableName;
// 		int varlength = UTF8GetLength(variableName);
		
		if (prevChar <= '9' && prevChar >= '0') {
			koreanVowelSound = (prevChar == '2' || prevChar == '4' || prevChar == '5' || prevChar == '9');
		} else if (prevChar <= 'z' && prevChar >= 'A') {
			koreanVowelSound = (prevChar == 'a' || prevChar == 'e' || prevChar == 'i' || prevChar == 'o' ||
								prevChar == 'u' || prevChar == 'y' || prevChar == 'A' || prevChar == 'E' ||
								prevChar == 'I' || prevChar == 'O' || prevChar == 'U' || prevChar == 'Y');
		}
		
		// --------------------
		// special case: see if previous character has 'f' in it
		if( !koreanVowelSound )
		{
			wchar_t special[] =  
				{
					0xc73c, 0xb85c, 0xb85c, 0
				};
			wchar_t var[ARRAY_SIZE(special)];

			UTF8ToWideStrConvert(postposVars, var, ARRAY_SIZE( var ));

			
			if( 0 == wcsncmp(special, var, ARRAY_SIZE( special )) )
			{
				// ( ( ( alpha * 0x15 ) + beta ) * 0x1C ) + (gamma + 0x1) + 0xAC00
				// To extract the consonants or vowels, the following formulas can be used as fundamental,
				// Final Consonant: 0x11A8 + ( ( The value of a hangul syllable - 0xAC00 ) % 0x1C - 1)
				// Vowel: 0x1161 + int ( ( ( ( The value of hangul syllable - 0xAC00 ) / 0x1C ) ) % 0x15 )
				// First Consonant:  0x1100 + int ( ( ( ( The value of hangul syllable - 0xAC00 ) / 0x1C ) ) / 0x15 )
				wchar_t finalConsonantOffset = (( prevChar - 0xAC00 ) % 0x1C - 1);
//				wchar_t finalConsonant = 0x11A8 + finalConsonantOffset;
// 		wchar_t vowel = 0x1161 + (((( prevChar - 0xAC00 ) / 0x1C ) ) % 0x15 );
// 		wchar_t firstConsonant = 0x1100 + int (((( prevChar - 0xAC00 ) / 0x1C ) ) / 0x15 );
				
				if( finalConsonantOffset == 7 )
				{
					koreanVowelSound = true;
				}
			}
		}
		

		// ----------
		// if we get here, use postpos 

		if( pIsKoreanVowelSound )
		{
			*pIsKoreanVowelSound = koreanVowelSound;
		}
		res = true;
	}

	// ----------
	// finally

	return res;
}

static void initVariableNameAndType(NamedVariableDef* def,
									const char* variableName,
									const char* variableType)
{
	int i;
	
	def->variableName = variableName;
	def->variableType = variableType;

	for(i = 0, def->variableTypeChar = 0; def->variableType[i]; i++){
		char c = tolower(def->variableType[i]);

		if(c >= 'a' && c <= 'z'){
			devassert(!def->variableTypeChar);
			def->variableTypeChar = def->variableType[i];
		}
	}

	devassert(def->variableTypeChar);
}

static char *resizeTempBuffer(char *buffer, int *size, char **cursor, char **bufEnd)
{
	char *newMessageResize;
	int offset = *cursor - buffer;
	int origSize = *size;

	*size = *size * 2; 
	newMessageResize = (char *) malloc(*size);
	strncpy_s(newMessageResize, *size, buffer, origSize);
	free(buffer);
	*bufEnd = newMessageResize + *size;
	*cursor = newMessageResize + offset;
	return newMessageResize;
}


// MAK - <rant>I'd really like to take the sprintf compatibility out.  I don't know why
// callers use both msPrintf and sprintf interchangeably.  You can't call
// msPrintf with a string containing {Param} and get the results you expect.</rant>
int msvaPrintfInternalEx(	MessageStore* store,
							char* outputBuffer,
							int bufferLength,
							const char* messageID,
							Array* messageTypeDef,
							ScriptVarsTable* vars,
							int flags,
							va_list arg)
{
	#define BUFFER_SIZE 16 * 1024
	const TextMessage* textMessage = NULL;
	int	 newMessageSize = 0;
	char *newMessage = NULL;
	char *newMessageEnd = NULL;
//	char newMessage[BUFFER_SIZE];
//	char paramBuf[BUFFER_SIZE];
	const char* oldMessageCursor;
	char* newMessageCursor;
	const char* varEnd = NULL; // where the start of a var from '{}' is

	//ZeroArray(newMessage);
	msPrintfError = 0;
	if(!store)
	{
		msPrintfError = 1;
		return vsprintfInternal(outputBuffer, bufferLength, messageID, arg);
	}

	// Look up the message to be printed.
	if (!stashFindPointerConst(store->messageIDStash, messageID, &textMessage))
		textMessage = NULL;

	// If the message cannot be found, print the messageID itself.
	if(!textMessage){
		msPrintfError = 1;
		return vsprintfInternal(outputBuffer, bufferLength, messageID, arg);
	}

	// put a temp pointer in with each type definition
	if(!messageTypeDef) // caller can override the typedef
	{
		// construct a local stack typedef array from the information in the textMessage->variableDefNameIndices and the string table
		U32 uiNumVariableDefs = SAFE_MEMBER(textMessage->variableDefNameIndices, size);
		U32 uiIndex;
		messageTypeDef = _alloca(sizeof(Array));
		messageTypeDef->size = messageTypeDef->maxSize = uiNumVariableDefs;
		if ( uiNumVariableDefs > 0 )
			messageTypeDef->storage = _alloca(sizeof(*messageTypeDef->storage) * uiNumVariableDefs);
		for (uiIndex=0; uiIndex < uiNumVariableDefs; ++uiIndex)
		{
			NamedVariableDef*	pDef = _alloca(sizeof(*pDef));
			int*				indices = (int*)textMessage->variableDefNameIndices->storage;
			
			initVariableNameAndType(pDef,
									strTableGetConstString(store->variableStringTable, indices[uiIndex]),
									strTableGetConstString(store->variableStringTable, indices[uiIndex] + 1));

			pDef->variablePointer = NULL;
			messageTypeDef->storage[uiIndex] = pDef;
		}
	}
	msGetTypeDefPointers(messageTypeDef, arg);

	// MAK - modified so string is generated in one pass
	oldMessageCursor = strTableGetConstString(store->messages, textMessage->messageIndex);

	newMessageSize = ((int) strlen(oldMessageCursor)) / 16 + 1; 
//	newMessageSize = ((int) strlen(oldMessageCursor)) * 2; 
	newMessage = (char *) malloc(newMessageSize);
	newMessageCursor = newMessage;
	newMessageEnd = newMessage + newMessageSize;

	if (oldMessageCursor)
	{
		while(*oldMessageCursor){
			switch(*oldMessageCursor){
				// If the \{ or \} characters are encountered, turn them into { and } characters, respectively.
				xcase '\\':
				{
					while (newMessageCursor + 1 >= newMessageEnd) 
					{
						newMessage = resizeTempBuffer(newMessage, &newMessageSize, &newMessageCursor, &newMessageEnd);
					}

					switch(oldMessageCursor[1])
					{
						xcase '{':
						case '}':
							*(newMessageCursor++) = oldMessageCursor[1];
							oldMessageCursor += 2;
						xcase 'n':
							*(newMessageCursor++) = '\n';
							oldMessageCursor += 2;
						xdefault:
							// If a "\{" or "\}" was not found, just copy the backslash character.
							*(newMessageCursor++) = *(oldMessageCursor++);
					}
				}
				
				xcase '{':
				{
					// If we find the open brace by itself, we've found the beginning of a variable name.
					char *endvar;
					char variableName[1024];
					char *paramBuf;

					// MAK - need to avoid mangling oldMessage because it may be in shared memory
					oldMessageCursor++;
					endvar = strchr(oldMessageCursor, '}');

					if (!endvar || endvar > oldMessageCursor + 1023)
					{
						Errorf("Error processing clause in Message %s, clause is too long", messageID);
						break; // no good way to recover here, log it?
					}

					strncpyt(variableName, oldMessageCursor, endvar - oldMessageCursor + 1);

					paramBuf = ScratchAlloc(BUFFER_SIZE);
					msPrintParam(messageID, paramBuf, BUFFER_SIZE, variableName, store, messageTypeDef, vars, arg);

					while (strlen(paramBuf) + newMessageCursor >= newMessageEnd) 
					{
						newMessage = resizeTempBuffer(newMessage, &newMessageSize, &newMessageCursor, &newMessageEnd);
					}

					strcpy_s(newMessageCursor, newMessage + newMessageSize - newMessageCursor, paramBuf);

					ScratchFree(paramBuf);

					oldMessageCursor = endvar + 1;
					newMessageCursor += strlen(newMessageCursor);
					varEnd = newMessageCursor; // track the end of the variable explicitly
				}

				xcase '[':
				{
				// Korean postposition thing
					if( getCurrentLocale() == 3 // its korean
						&& varEnd ) // we have a variable that was substituted
					{
						char *endvar;
						char variableName[1024];
						wchar_t prevChar = 0;
						int koreanVowelSound = 0;
						
						// MAK - need to avoid mangling oldMessage because it may be in shared memory
						oldMessageCursor++;
						endvar = strchr(oldMessageCursor, ']');
						
						if (!endvar || endvar > oldMessageCursor + 1023)
						{
							Errorf("Error processing clause in Message %s, clause is too long", messageID);
							break; // no good way to recover here, log it?
						}
						
						strncpy(variableName, oldMessageCursor, endvar - oldMessageCursor);
						variableName[endvar - oldMessageCursor] = '\0';
						
						// get the last korean character in the converted message
						{
							wchar_t var[1024];
							int lenVar = 0;
							char *tmp = newMessage;
							int i;
							
							while (*tmp && tmp < varEnd)
							{
								tmp = UTF8GetNextCodepoint( tmp );
								lenVar++;
							}
							
							i = UTF8ToWideStrConvert(newMessage, var, ARRAY_SIZE( var ));
							for(;i >= 0; --i)
							{
								if(s_CharIsPostPositionable(var[i]))
									break;
							}
							
							if(i >= 0)
								prevChar = var[i];
						}
						
						// perform the conversion (if applicable)
						if ( prevChar && s_ChoosePostposition(prevChar, &koreanVowelSound, variableName) )
						{
							char *copyPos = variableName;
							int varlength = UTF8GetLength(variableName);
							
							if (koreanVowelSound) 
							{
								// skip first or second
								copyPos += UTF8GetCodepointLength(copyPos);
								if (varlength > 2)
									copyPos += UTF8GetCodepointLength(copyPos);
								
								// use rest
								while (*copyPos) 
								{
									while (newMessageCursor + 1 >= newMessageEnd) 
									{
										newMessage = resizeTempBuffer(newMessage, &newMessageSize, &newMessageCursor, &newMessageEnd);
									}

									*newMessageCursor++ = *copyPos++;
								}
								
							} else {
								int copySize = UTF8GetCodepointLength(variableName);
								if (varlength > 2)
								{
									char *nextChar = UTF8GetNextCodepoint(variableName);
									copySize += UTF8GetCodepointLength(nextChar);
								}

								while (newMessageCursor + copySize >= newMessageEnd) 
								{
									newMessage = resizeTempBuffer(newMessage, &newMessageSize, &newMessageCursor, &newMessageEnd);
								}

								// use first or first and second (adverb case)
								strncpy_s(newMessageCursor, newMessageEnd - newMessageCursor - 1, 
									variableName, copySize);
								newMessageCursor += copySize;
							}
							oldMessageCursor = endvar + 1; 
						} 
						else 
						{
							// didn't find anything, just append the variable data and be done
							while (newMessageCursor + (int) (endvar - oldMessageCursor + 2) >= newMessageEnd) 
							{
								newMessage = resizeTempBuffer(newMessage, &newMessageSize, &newMessageCursor, &newMessageEnd);
							}

							strncpy_s(newMessageCursor, endvar - oldMessageCursor + 1,
								oldMessageCursor - 1, endvar - oldMessageCursor + 2);
							
							newMessageCursor += endvar - oldMessageCursor + 2;
							oldMessageCursor = endvar + 1;
						} 					
					} else {
						while (newMessageCursor + 1 >= newMessageEnd) 
						{
							newMessage = resizeTempBuffer(newMessage, &newMessageSize, &newMessageCursor, &newMessageEnd);
						}
						*(newMessageCursor++) = *(oldMessageCursor++);
					}
				}
				
				xdefault:
				{
					while (newMessageCursor + 1 >= newMessageEnd) 
					{
						newMessage = resizeTempBuffer(newMessage, &newMessageSize, &newMessageCursor, &newMessageEnd);
					}
					*(newMessageCursor++) = *(oldMessageCursor++);
				}
			}
		}
	}

	*newMessageCursor = '\0';
	if(outputBuffer) 
	{
		strcpy_s(outputBuffer, bufferLength, newMessage);
	}

	newMessageSize = newMessageCursor - newMessage;
	if (newMessage) {
		free(newMessage);
		newMessage = NULL;
	}
	return newMessageSize;
	
	#undef BUFFER_SIZE
}

static MessageStore* msAddMessageDirStore;
static FileScanAction msAddMessageDirHelper(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char filename[CRYPTIC_MAX_PATH];
	char typefilename[CRYPTIC_MAX_PATH];
	MessageStore* store = msAddMessageDirStore;
	FileScanAction retval = FSA_EXPLORE_DIRECTORY;
	char *s;

	// Explore all subdirectories
	if(data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;

	// Only load message store files
	if (!strEndsWith(data->name, ".ms") && !strEndsWith(data->name, ".txt"))
		return retval;

	// Grab the full filename.
	sprintf(filename, "%s/%s", dir, data->name);

	// Generate a possible type file name (Always in English/)
	s = strchr(filename, '/');
	assert(s);
	s = strchr(s+1, '/');
	assert(s);
	sprintf(typefilename, "texts/English%s", s);
	changeFileExt(typefilename, ".types", typefilename);

	// Load messages and type file
	msAddMessages(store, filename, typefilename);

	return retval;
}

void msAddMessageDirectory(MessageStore* store, char* dirname)
{
	msAddMessageDirStore = store;

	// Get a list of filenames in the directory.
	fileScanAllDataDirs(dirname, msAddMessageDirHelper, NULL);
	SAFE_FREE(store->fileLoadedFrom);
}

int msContainsKey(MessageStore* store, const char *messageID)
{
	return stashFindPointerConst(store->messageIDStash, messageID, NULL);
}

void msSetMessageComment(MessageStore* store, TextMessage* msg, SA_PARAM_OP_STR const char* comment){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);
	
	if(extMsg){
		extMsg->commentLine = comment?strTableAddString(store->messages, comment):NULL;
	}
}

const char* msGetMessageComment(MessageStore* store, TextMessage* msg){
	ExtendedTextMessage* extMsg = getExtendedMessage(store, msg);
	
	return SAFE_MEMBER(extMsg, commentLine);
}

void msUpdateMessage(MessageStore *store, const char *messageID, const char *currentLocaleText, const char *comment)
{
	TextMessage *oldTextMessage = NULL;
	
	stashFindPointer(store->messageIDStash, messageID, &oldTextMessage);

	if (!oldTextMessage) {
		// New message!
		TextMessage *newTextMessage = msAddMessage(store, messageID, currentLocaleText);
		assert(newTextMessage);
		msSetMessageIsNew(store, newTextMessage, 1);
		msSetMessageComment(store, newTextMessage, comment);
	} else {
		// Modifying an old message
		int oldIndex = oldTextMessage->messageIndex;
		oldTextMessage->messageIndex = strTableAddStringGetIndex(store->messages, currentLocaleText);
	
		msSetMessageIsModified(store, oldTextMessage, 1);
		msSetMessageComment(store, oldTextMessage, comment);
		msSetMessageOriginalIndex(store, oldTextMessage, oldIndex);
	}
}

void msSaveMessageStore(MessageStore *store)
{
	StashTableIterator iter;
	StashElement elem;
	char *messageData, *cursor;
	int argc;
	char *args[10];
	FILE *fout;
	const char *lockee;
	bool changed=false;
	bool checkedOut;
	int trysLeft=5;
	char filename[512];
	
	assert(store->useExtendedTextMessage);

	// Check for anything changed
	stashGetIterator(store->messageIDStash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		TextMessage *textMessage = stashElementGetPointer(elem);
		if(	msGetMessageIsModified(store, textMessage) ||
			msGetMessageIsNew(store, textMessage))
		{
			changed = true;
			break;
		}
	}
	if (!changed)
		return;

	// Checkout *before* editing!
	do {
		int ret;
		ret = gimmeDLLDoOperation( store->fileLoadedFrom, GIMME_CHECKOUT, 0 );
		if (GIMME_NO_ERROR!=ret && ret!=GIMME_ERROR_NO_SC && ret!=GIMME_ERROR_NO_DLL) {
			checkedOut = false;
			Sleep(250);
			trysLeft--;
		} else {
			// Either we don't have gimme access or it checked it out
			checkedOut = true;
		}
	} while (!checkedOut && trysLeft);
	if (!checkedOut) {
		lockee = gimmeDLLQueryIsFileLocked(store->fileLoadedFrom);
		Errorf("Unable to check out %s for editing, please have %s check it back in and then re-run the Save command, otherwise the text will not be saved!", store->fileLoadedFrom, lockee?lockee:"UNKNOWN");
		return;
	}

	// MAKE SURE we've got the latest
	gimmeDLLDoOperation( store->fileLoadedFrom, GIMME_GLV, 0 );

	// Read message store, keeping comments, changing modified versions
	fileLocateWrite(store->fileLoadedFrom, filename);
	mkdirtree(filename);
	messageData = fileAlloc(store->fileLoadedFrom, 0);
	if (!messageData) {
		Errorf("Error reading in old %s", store->fileLoadedFrom);
		return;
	}
	if(0 != memcmp(messageData, UTF8BOM, 3))
	{
		printf("Unable to read file \"%s\" because it is not a UTF8 file!\n", store->fileLoadedFrom);
		free(messageData);
		return;
	}

	fout = fileOpen(store->fileLoadedFrom, "wb");
	if (!fout) {
		Errorf("Error opening %s for writing", store->fileLoadedFrom);
		free(messageData);
		return;
	}
	fwrite(UTF8BOM, 1, ARRAY_SIZE(UTF8BOM), fout);
	cursor = messageData;
	cursor+=ARRAY_SIZE(UTF8BOM);
	while (cursor && *cursor)
	{
		int len;
		char *end = strchr(cursor, '\r');
		if (!end) {
			end = cursor + strlen(cursor)-1;
		} else {
			if (end[1]=='\n') {
				end++;
			}
		}
		len = end - cursor + 1;
//		while (strchr("\r \t\n", *cursor)) // Skip these characters
//			cursor++;
		if (cursor[0]=='\r' || cursor[0]=='#') {
			// Comment or empty line, pass-through
			fwrite(cursor, 1, len, fout);
			if (cursor[len-1]!='\n') {
				char c='\n';
				fwrite(&c, 1, 1, fout);
			}
			cursor+=len;
		} else {
			// Data line
			char *line = calloc(len+1, 1);
			strncpy_s(line, len+1, cursor, len);
			argc = tokenize_line_safe(cursor, args, ARRAY_SIZE(args), &cursor);
			if (argc<2) {				
				ErrorFilenamef(store->fileLoadedFrom, "Invalid format: found %d tokens, expected 2 or more", argc);
				fwrite(line, 1, len, fout);
			} else {
				TextMessage *textMessage;
				if (!stashFindPointer(store->messageIDStash, args[0], &textMessage))
					textMessage = NULL;

				if(	textMessage &&
					(	msGetMessageIsModified(store, textMessage) ||
						msGetMessageIsNew(store, textMessage)))
				{
					if (strchr(strTableGetConstString(store->messages,textMessage->messageIndex), '\"')) {
						fprintf(fout, "\"%s\", <<%s>>\r\n", textMessage->messageID, strTableGetConstString(store->messages,textMessage->messageIndex));
					} else {
						fprintf(fout, "\"%s\", \"%s\"\r\n", textMessage->messageID, strTableGetConstString(store->messages,textMessage->messageIndex));
					}
					msSetMessageIsModified(store, textMessage, 0);
					msSetMessageIsNew(store, textMessage, 0);
				} else {
					fwrite(line, 1, len, fout);
				}
			}
			free(line);
		}
	}
	// Write all new messages
	stashGetIterator(store->messageIDStash, &iter);
	while (stashGetNextElement(&iter, &elem)) {
		TextMessage *textMessage = stashElementGetPointer(elem);
		if (msGetMessageIsNew(store, textMessage)) {
			// Append
			fprintf(fout, "%s\r\n", msGetMessageComment(store, textMessage));

			if (strchr(strTableGetConstString(store->messages, textMessage->messageIndex), '\"')) {
				fprintf(fout, "\"%s\", <<%s>>\r\n", textMessage->messageID, strTableGetConstString(store->messages, textMessage->messageIndex));
			} else {
				fprintf(fout, "\"%s\", \"%s\"\r\n", textMessage->messageID, strTableGetConstString(store->messages, textMessage->messageIndex));
			}
			msSetMessageIsNew(store, textMessage, 0);
		}
	}
	fclose(fout);
	gimmeDLLDoOperation( store->fileLoadedFrom, GIMME_CHECKIN, 0 );
	free(messageData);
}

static int cmpOrigIndex(MessageStore *store, const TextMessage **msg1, const TextMessage **msg2)
{
	int index1 = msGetMessageOriginalIndex(store, *msg1);
	int index2 = msGetMessageOriginalIndex(store, *msg2);
	return index1 - index2;
}

void msSaveMessageStoreFresh(MessageStore *store)
{
	StashTableIterator iter;
	StashElement elem;
	FILE *fout;
	char filename[512];
	int i;
	TextMessage **messages=NULL;
	
	// Write all new messages
	stashGetIterator(store->messageIDStash, &iter);
	while (stashGetNextElement(&iter, &elem)) {
		eaPush(&messages, stashElementGetPointer(elem));
	}

	if (messages) {
		eaQSort_s(messages, cmpOrigIndex, store);
	}

	fileLocateWrite(store->fileLoadedFrom, filename);
	mkdirtree(filename);
	fout = fileOpen(store->fileLoadedFrom, "wb");
	if (!fout) {
		Errorf("Error opening %s for writing", store->fileLoadedFrom);
		return;
	}
	fwrite(UTF8BOM, 1, ARRAY_SIZE(UTF8BOM), fout);
	for (i=0; i<eaSize(&messages); i++) {
		TextMessage *textMessage = messages[i];
		if (strchr(strTableGetConstString(store->messages, textMessage->messageIndex), '\"')) {
			fprintf(fout, "\"%s\", <<%s>>\r\n", textMessage->messageID, strTableGetConstString(store->messages, textMessage->messageIndex));
		} else {
			fprintf(fout, "\"%s\", \"%s\"\r\n", textMessage->messageID, strTableGetConstString(store->messages, textMessage->messageIndex));
		}
	}
	fclose(fout);
	eaDestroy(&messages);
}

void msSetFilename(MessageStore *store, const char *messageFilename)
{
	SAFE_FREE(store->fileLoadedFrom);
	store->fileLoadedFrom = strdup(messageFilename);
}
