#pragma once
GCC_SYSTEM
/***************************************************************************
***************************************************************************/
#ifndef _STRUCTNET_H
#define _STRUCTNET_H

#include "stdtypes.h"
#include "textparser.h" // need FloatAccuracy enum from here

// MAK - these functions now work correctly with the full range of textparser structs

typedef struct Packet Packet;

// lossy way of sending & receiving floats
int ParserPackFloat(float f, FloatAccuracy fa);
float ParserUnpackFloat(int i, FloatAccuracy fa);
int ParserFloatMinBits(FloatAccuracy fa);

// better packing for small deltas - promote to network lib?
void packDelta(Packet *pak, int delta);
int unpackDelta(Packet *pak);

//use this if you just want to put a packet in a struct with no fancy options
#define ParserSendStruct(tpi, pak, pStruct) ParserSend(tpi, pak, NULL, pStruct, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL)

//returns true if at least one piece of "real" data was sent (the return value is only useful in the diffing case
bool ParserSend(ParseTable* def, Packet* pak, const void* oldVersion, const void* newVersion, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude,
				StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB);

void ParserSendEmptyDiff(ParseTable* sd, Packet* pak);

//RECVDIFF_FLAG_ABS_VALUES is ignored here, as it's always sent by the sender
//
//if RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE is set, then returns true on success, false on failure
//otherwise always asserts on failure
bool ParserRecv(ParseTable* sd, Packet* pak, void* data, enumRecvDiffFlags eFlags);

//"safe" send and receive send the tpi along with the struct, and ensure maximum compatibility between older and newer versions
//of code
//

/*BIG IMPORTANT WARNING: the way these work is that the first time a struct of a particular type is sent
over a particular link, the TPI is sent. All the other times, the already-sent TPI is used. This means
that you CAN NOT have code which receives the packet and then only maybe calls ParserRecvSTructSafe.

do NOT do this:

void handleFoo(Packet *pPack, MyStruct *pStruct)
{
	FooStruct *pFoo;

	if (pStruct->noFooRightNow)
	{
		return;
	}

	pFoo = StructCreate(parse_FooStruct);
	ParserRecvSafe(parse_FooStruct, pPack, pFoo);

}

instead, do this:

void handleFoo(Packet *pPack, MyStruct *pStruct)
{
	FooStruct *pFoo;

	pFoo = StructCreate(parse_FooStruct);
	ParserRecvSafe(parse_FooStruct, pPack, pFoo);

	if (pStruct->noFooRightNow)
	{
		StructDestroy(parse_FooStruct, pFoo);
		return;
	}


}
*/


void ParserSendStructSafe(ParseTable *pTPI, Packet *pPack, void *pStruct);

//true on success, false on failure (only possible if packet is from an untrustworthy source, otherwise it will assert)
bool ParserRecvStructSafe(ParseTable *pTPI, Packet *pPack, void *pStruct);

//same as above, but does StructCreate for you
static __forceinline void *ParserRecvStructSafe_Create(ParseTable *pTPI, Packet *pPack)
{
	void *pRetVal = StructCreateVoid(pTPI);
	ParserRecvStructSafe(pTPI, pPack, pRetVal);
	return pRetVal;
}


// MAKTODO - replace these
void sdPackParseInfo(ParseTable* sd, Packet *pak);
ParseTable *sdUnpackParseInfo(ParseTable *supported, Packet *pak);
void sdFreeParseInfo(ParseTable* fieldDefs);




//if you ParserSend a struct with a tpi on one server (ie, with the schema on the object DB), you can receive
//it with another tpi on another server, as long as you have the first tpi around. To do so, just call
//ParserSendStruct on the sending server as normal, then this. This can only be done for
//a full struct send, not a diff
int ParserRecv2tpis(Packet *pak, ParseTable *pSrcTable, ParseTable *pTargetTable, void *data);

//helper stuff for recv2tpi
typedef enum
{
	RECV2TPITYPE_DONTCARE, //this is an ignore or start or end or redundantname or something
	RECV2TPITYPE_NORMALRECV, //the two columns are identical, you can use structRecv directly
	RECV2TPITYPE_STRINGRECV, //the two columns are different, but you can just receive a string and then call a string-token-setting
							//function
	RECV2TPITYPE_STRUCT,	//this is a substruct
	RECV2TPITYPE_U8TOBIT,	//a bitfield was converted to a U8, need to convert it back

	RECV2TPITYPE_UNKNOWN, //something bad is going on
} enumRecv2TpiColumnType;

typedef struct
{
	//-1 if not found
	int iTargetColumn;

	enumRecv2TpiColumnType eType;
} Recv2TpiColumnInfo;

typedef struct Recv2TpiCachedInfo
{
	ParseTable *pSrcTable;
	Recv2TpiColumnInfo *pColumns; //allocated as a block, one per column in pSrcTable;
} Recv2TpiCachedInfo;

//super-cautious protocol for sending structs around while maintaining forward- and backward- compatibility. Super-carefully makes
//sure that nothing will crash on bad data. Only supports
//direct embedded strings and 32-bit integers
void ParserSendStructAsCheckedNameValuePairs(Packet *pPak, ParseTable *pTPI, void *pStruct);

//returns true on success, false on failure
bool ParserReceiveStructAsCheckedNameValuePairs(Packet *pPak, ParseTable *pTPI, void *pStruct);

//any time an "untrustworthy" source sends us an array size, check if it's bigger than this. If it is, assume that it's an error of some sort
//This can be overridden on a field-by-field basis using FORMATSTRING(MAX_ARRAY_SIZE)
#define MAX_UNTRUSTWORTHY_ARRAY_SIZE 8192
#endif

// AMA - for debugging
// This should be removed
extern char *g_pchCommentString;