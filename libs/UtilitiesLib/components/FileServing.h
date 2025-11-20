#pragma once
GCC_SYSTEM
#include "globaltypes.h"

//"file serving" in this context means converting a filename into a binary chunk of data and asynchronously feeding that
//data back to whoever requested it. This is used by the server monitoring to allow download of files from
//other servers on other machines in the shard
//
//filenames look like this: /mapmanager/7/fileSystem/foo.txt
//that's a file on mapmanager id 7, category "fileSystem", actual name "foo.txt"

AUTO_ENUM;
typedef enum
{
	FILESERVING_BEGIN,
	FILESERVING_PUMP,
	FILESERVING_CANCEL
} enumFileServingCommand;

//if pErrorString is NULL, then there was no error.
//
//pCurData was malloced, ownership is being passed
typedef void FileServingRequestFulfilledCallBack(int iRequestID, char *pErrorString,
	 U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, U8 *pCurData);

typedef void FileServingCommandCallBack(char *pFileName, int iRequestID, enumFileServingCommand eCommand,
	U64 iBytesRequested, FileServingRequestFulfilledCallBack *pFulfillCB);


bool DeconstructFileServingName(char *pInName, GlobalType *pOutContainerType, ContainerID *pOutContainerID,
	char **ppTypeString, char **ppInnerName);