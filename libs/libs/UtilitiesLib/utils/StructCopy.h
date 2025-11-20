#pragma once
GCC_SYSTEM

#include "TextParser.h"
#include "ByteBlock.h"



//flags that are passed around internally in the StructCopy system, but never set by normal callers
typedef enum CopyQueryFlags
{
	COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT = 1 << 0, //tells the field being queried that it is being excluded from
		//this copy, and should presumably report a number of bytes not to copy.

	COPYQUERYFLAG_IN_FIXED_ARRAY = 1 << 1, //tells the field being queried that it is in a fixed array
} CopyQueryFlags;

//the copying procedure analyzes a TPI and decides how many times it's actually going to have to call CopyField callbacks
//for that struct. It saves the result of that analysis in a list of CopyCallbackNeeded structs
typedef struct CopyCallbackNeeded
{
	int iColumn;	
	int iIndex;
	bool bIsEarrayCallback;
} CopyCallbackNeeded;

//a struct being copied can have multiple lists of CopyCallbackNeededs, one for the struct itself and one for
//each embedded struct (as embedded structs have their own TPIs)
typedef struct CopyCallbackNeededList
{
	ParseTable *pTPI;
	int iOffsetInParentStruct;
	CopyCallbackNeeded **ppCallbacks;
} CopyCallbackNeededList;

//all the information needed to copy structs described by one TPI
typedef struct StructCopyInformation
{
	//these 4 fields uniquely identify this SCI
	ParseTable *pParentTPI;
	StructCopyFlags eFlags;
	StructTypeField iOptionFlagsToMatch;
	StructTypeField iOptionFlagsToExclude;

	CopyCallbackNeededList **ppCallbackLists;
	ByteCopyGroup bytesToCopy;
	bool bNeedPreCopyCB;
	bool bNeedPostCopyCB;
	bool bNeedPostCopySrcCB;
	bool bNeedPreCopyDestCB;
} StructCopyInformation;

//information needed during SCI Creation
typedef struct SCICreationInfo
{
	StructCopyInformation *pSCI;
	RawByteCopyGroup *pRawBytesToCopy;
	ParseTable *pCurTPI;
	int iCurStartingOffsetIntoParentStruct;

	int iCurColumn;
	int iCurIndex;

} SCICreationInfo;


//five return calls that a tpi field can make when it is queried as to whether it needs to be copied
void StructCopyQueryResult_MarkBytes(bool bMarkToCopy, int iByteOffset, int iNumBytes, void *pUserData);
void StructCopyQueryResult_MarkBit(bool bMarkToCopy, int iByteOffset, int iBitOffset, void *pUserData);
void StructCopyQueryResult_NeedCallback(void *pUserData);

typedef void (*structCopyQuery_f)(ParseTable *pTPI, int iColumn, int iIndex, int iOffsetIntoParentStruct, 
								  CopyQueryFlags eQueryFlags, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, void *pUserData);


void DestroyStructCopyInformation(StructCopyInformation *pSCI);

StructCopyInformation *FindStructCopyInformationForCopy(ParseTable *pTPI, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
void CopyStructWithSCI(StructCopyInformation *pSCI, void *pFrom, void *pTo);






