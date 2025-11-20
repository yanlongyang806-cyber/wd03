#include "../../core/common/autogen/controller_autogen_remotefuncs.h"
#include "httpJpegLibrary.h"
#include "HttpXpathSupport.h"
#include "TextParser.h"
#include "GenericFileServing.h"


void GetJpegForServerMonitoringRemoteCommandReturnCB(char *pData, int iDataSize, int iLifeSpan, char *pMessage, GetJpegCache *pUserData)
{	
	TextParserBinaryBlock *pBlock;

	pBlock = TextParserBinaryBlock_CreateFromMemory(pData, iDataSize, false);

	RemoteCommand_ReturnJpegForServerMonitoring(GLOBALTYPE_CONTROLLER, 0, pUserData->iRequestID, pUserData->iMCPID, 
		pBlock, iLifeSpan, pMessage);

	free(pUserData);
	StructDestroy(parse_TextParserBinaryBlock, pBlock);
}



AUTO_COMMAND_REMOTE;
void GetJpegForServerMonitoring(int iRequestID, UrlArgumentList *pArgList, ContainerID iMCPID, char *pJpegName)
{
	GetJpegCache *pCache = malloc(sizeof(GetJpegCache));
	pCache->iMCPID = iMCPID;
	pCache->iRequestID = iRequestID;

	JpegLibrary_GetJpeg(pJpegName, pArgList, GetJpegForServerMonitoringRemoteCommandReturnCB, pCache);

}

void FileServingRemoteCommandFulfill(int iRequestID, char *pErrorString,
	 U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, U8 *pCurData)
{
	char *pEStr = NULL;

	if (iCurNumBytes)
	{
		estrBase64Encode(&pEStr, pCurData, iCurNumBytes);
		free(pCurData);
	}

	RemoteCommand_FileServingReturn(GLOBALTYPE_CONTROLLER, 0, iRequestID, pErrorString, iTotalSize, iCurBeginByteOffset, iCurNumBytes, pEStr, estrLength(&pEStr));
	estrDestroy(&pEStr);

}

AUTO_COMMAND_REMOTE;
void FileServingRequestForServerMonitoring(char *pFileName, int iControllerReqID, enumFileServingCommand eCommand, S64 iBytesRequested)
{
	GenericFileServing_CommandCallBack(pFileName, iControllerReqID, eCommand, iBytesRequested, FileServingRemoteCommandFulfill);





}
