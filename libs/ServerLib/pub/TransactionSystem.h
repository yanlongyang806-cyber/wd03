/***************************************************************************



***************************************************************************/

#ifndef TRANSACTIONSYSTEM_H_
#define TRANSACTIONSYSTEM_H_

#include "stdtypes.h"
#include "GlobalTypeEnum.h"
#include "GlobalTypes.h"
#include "net/net.h"
#include "transactionoutcomes.h"


//this file includes global defines and typedefs that are used by all parts of the transaction system. For more info
//on the transaction system in general, talk to Alex or Ben, or look at the wiki page

//every transaction has a unique ID. 0 is invalid.
typedef unsigned int TransactionID;

//if this bit is set in a transaction ID, it means that transaction is a local transaction. The transaction server itself
//never knows about local transactions.
#define TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION 0x80000000


//each transaction consists of one or more "base transactions". Each base transaction is a command send to a specifically
//identified recipient. The command itself is a string which the transaction system generally knows nothing about
#define MAX_BASE_TRANSACTIONS_PER_TRANSACTION 350


//these are the basic types of transactions. In different types of transactions, the base transactions
//relate to each other differently, or not at all. The classic "transaction" as initially described by Mark
//and Bruce is TRANS_TYPE_SEQUENTIAL_ATOMIC.

AUTO_ENUM;
typedef enum
{
	TRANS_TYPE_NONE,

	TRANS_TYPE_SIMULTANEOUS,
	//sends all its base transactions at once, waits til they all respond, if any are blocked, retries them,
	//continues until all have succeeded or failed. Returns failure if at least one failed. You would use this
	//for "give $100 to everyone on my team", where if one of the guys on the team doesn't get his $100 for some
	//reason, that doesn't interfere with the rest of the people getting the $100.

	TRANS_TYPE_SIMULTANEOUS_ATOMIC,
	//sends all its base transactions at once, with wait-to-confirm flag set. If all respond succeeded, sends
	//confirm to all, returns true. If at least one fails, cancel all and return failure.
	//If at least one is blocked, cancel all, and retry.

	TRANS_TYPE_SEQUENTIAL,
	//sends 1st base transaction, retrying until no blocking. Then sends 2nd base transaction. Etc. Returns failure if
	//at least one failed

	TRANS_TYPE_SEQUENTIAL_STOPONFAIL,
	//sends 1st base transaction, retrying until no blocking. If it fails, return failure. Otherwise, send 2nd base
	//transaction, retrying until no blocking. Etc.

	TRANS_TYPE_SEQUENTIAL_ATOMIC,
	//sends nth base transaction with wait-to-confirm flag set. If it fails, cancel 1 through n-1, return failure. If it
	//succeeds, move on to n+1. If it blocks, cancel 1 through n-1 and retry. If all succeed, confirm them all (simultaneously)
	//and when all are confirmed, return success

	TRANS_TYPE_CORRUPT,
	//we called TransactionAssert on this transaction, something went very wrong, so we're going to just quarantine it and 
	//ignore it forever

	TRANS_TYPE_COUNT,
	//end-of-list placeholder

} enumTransactionType;


extern ParseTable parse_BaseTransaction[];
#define TYPE_parse_BaseTransaction BaseTransaction
extern ParseTable parse_TransactionRequest[];
#define TYPE_parse_TransactionRequest TransactionRequest

//a base transaction consists of a recipient, a transaction string, and a space-separated string containing the names
//of transaction variables this base transaction might be interested in
AUTO_STRUCT;
typedef struct BaseTransaction
{
	ContainerRef recipient;
	char *pData; //standard null-terminatd string
	char *pRequestedTransVariableNames;
} BaseTransaction;

// Container for multiple base Transactions, useful for queries
AUTO_STRUCT;
typedef struct TransactionRequest
{
	BaseTransaction **ppBaseTransactions;
} TransactionRequest;


//helper functions for putting transaction IDs into packets and taking them back out again, in case we
//want to do some optimization at some point in the future
_inline TransactionID GetTransactionIDFromPacket(Packet *pPacket)
{
	return pktGetBits(pPacket, 32);
}

_inline void PutTransactionIDIntoPacket(Packet *pPacket, TransactionID iID)
{
	pktSendBits(pPacket, 32, iID);
}

/*
examples of legal transaction server commands
online entity 17 mapserver 15
offline entity 23 mapserver 11
move entity 14 mapserver 6 mapserver 12
register entity 16 persisted dbserver 1
*/
#define TRANSACTIONSERVER_COMMAND_ONLINE "online"
#define TRANSACTIONSERVER_COMMAND_OFFLINE "offline"
#define TRANSACTIONSERVER_COMMAND_REGISTER "register"
#define TRANSACTIONSERVER_COMMAND_MOVE "move"
#define TRANSACTIONSERVER_COMMAND_ALL "all"


//return values when a local transaction manager is created and attempts to connect to the transaction server
AUTO_ENUM;
typedef enum enumTransServerConnectResult
{
	TRANS_SERVER_CONNECT_RESULT_NONE,
	TRANS_SERVER_CONNECT_RESULT_SUCCESS,
	TRANS_SERVER_CONNECT_RESULT_FAILURE_OBJECTDBALREADYCONNECTED,
	TRANS_SERVER_CONNECT_RESULT_FAILURE_SERVERIDNOTUNIQUE,
	TRANS_SERVER_CONNECT_RESULT_FAILURE_ANTIZOMBIFICATIONCOOKIE_MISMATCH,
	TRANS_SERVER_CONNECT_RESULT_FAILURE_TOOMANYMULTIPLEXERS,
	TRANS_SERVER_CONNECT_RESULT_FAILURE_MULTIPLEXIDINUSE,
	TRANS_SERVER_CONNECT_RESULT_HANDSHAKE_FAILED,
	TRANS_SERVER_CONNECT_RESULT_TOO_MANY_CONNECTIONS,
	TRANS_SERVER_CONNECT_RESULT_FAILURE_VERSION_MISMATCH,
} enumTransServerConnectResult;


//the structure that database returns are put into... it contains two estrings 
//(the second one used for dbUpdateContainerOwner only) and a
//variably sized raw data pointer. 
typedef struct TransDataBlock
{
	//NOTE NOTE NOTE these used to be guaranteed EStrings, now you shoudl construct them as EStrings when
	//building a TransDataBlock, but when handed a TransDataBlock, you can not treat them as EStrings because
	//the might be temp strings
	char *pString1;
	char *pString2;

	bool bTempStrings;
	void *pData;
	int iDataSize;
} TransDataBlock;

//all of these are safe for NULL pBlock
void PutTransDataBlockIntoPacket(Packet *pPack, TransDataBlock *pBlock);
bool TransDataBlockIsEmpty(TransDataBlock *pBlock);

//memsets to zero, does NOT free anything already there
void TransDataBlockInit(TransDataBlock *pBlock);

//estrDestroy and the frees the pData, also resets everything to NULL, does NOT 
//free the pBlock itself
void TransDataBlockClear(TransDataBlock *pBlock);

//same as TransDataBlockClear, then frees the pBlock
void TransDataBlockDestroy(TransDataBlock *pBlock);

//returns either NULL or a malloced non-empty data block
TransDataBlock *GetTransDataBlockFromPacket(Packet *pPack);

//returns either NULL or a pktGetStringTemp, which MUST be destroyed before
//the packet is releasednon-empty data block
TransDataBlock *GetTransDataBlockFromPacket_Temp(Packet *pPack);


//like the above, but gets into an already-existing data block
void SetTransDataBlockFromPacket(Packet *pPack, TransDataBlock *pBlock);

//copies the estring and duplicates and re-mallocs the data
void TransDataBlockCopy(TransDataBlock *pDest, TransDataBlock *pSrc);


void TransactionReturnVal_VerboseDump(TransactionReturnVal *pRetVal, char **ppOutEString);

#endif