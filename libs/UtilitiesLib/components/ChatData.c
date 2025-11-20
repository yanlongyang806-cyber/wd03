#include "chatData.h"
#include "EString.h"
#include "GlobalTypes.h"
#include "StringUtil.h"
#include "StringCache.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

SA_RET_NN_VALID extern ChatLink *ChatData_CreateLink(ChatLinkType eType, const char *pchKey, const char *pchDisplayText) {
	ChatLink *pLink = StructCreate(parse_ChatLink);
	char *pchDisplayTextCopy = strdup(pchDisplayText);
	removeLeadingAndFollowingSpaces(pchDisplayTextCopy);

	pLink->eType = eType;
	pLink->pchGame = allocAddString(GetShortProductName());
	estrCopy2(&pLink->pchKey, pchKey);
	estrPrintf(&pLink->pchText, "[%s]", pchDisplayTextCopy);

	free(pchDisplayTextCopy);

	return pLink;
}

SA_RET_NN_VALID extern ChatLink *ChatData_CreatePlayerLink(SA_PARAM_NN_STR const char *pchNameAndHandle, SA_PARAM_OP_STR const char *pchDisplayText, bool bIsGM, bool bIsDev, SA_PARAM_OP_STR const char *pchFullMsg, U32 iEntContainerID) 
{
	ChatLink *pLink = StructCreate(parse_ChatLink);
	char *pchDisplayTextCopy = pchDisplayText ? strdup(pchDisplayText) : strdup(pchNameAndHandle);
	removeLeadingAndFollowingSpaces(pchDisplayTextCopy);

	pLink->eType = kChatLinkType_PlayerHandle;
	pLink->pchGame = allocAddString(GetShortProductName());
	pLink->bIsGM = bIsGM;
	pLink->bIsDev = bIsDev;
	estrCopy2(&pLink->pchKey, pchNameAndHandle);
	estrCopy2(&pLink->pchText, pchDisplayTextCopy);
	estrCopy2(&pLink->pchText2, pchFullMsg);
	pLink->iEntContainerID = iEntContainerID;

	free(pchDisplayTextCopy);

	return pLink;
}

// Search for a reference to pchNameAndHandle within the given message
// and generate a ChatLinkInfo that points to it.  If the pchNameAndHandle
// can't be found this returns NULL.
//
// The '@' character must be present in the pchNameAndHandle.
// The name part of pchNameAndHandle is optional, but the handle is not.  
// This is because links to player handles only make sense if the handle
// is present.  The name may provide additional context for link operations
// that require it (such as team & guild commands).
SA_RET_OP_VALID extern ChatLinkInfo *ChatData_CreatePlayerHandleLinkInfoFromMessage(const char *pchMessage, const char *pchNameAndHandle, bool bIsGM, bool bIsDev) {
	ChatLinkInfo *pLinkInfo = NULL;

	if (pchMessage && *pchMessage && pchNameAndHandle && *pchNameAndHandle && strchr(pchNameAndHandle, '@')) {
		const char *pchIndex = strstr(pchMessage, pchNameAndHandle);

		if (pchIndex) {
			ChatLink *pLink = ChatData_CreatePlayerLink(pchNameAndHandle, NULL, bIsGM, bIsDev, NULL, 0);
			pLinkInfo = StructCreate(parse_ChatLinkInfo);
			pLinkInfo->pLink = pLink;
			pLinkInfo->iStart = pchIndex - pchMessage;
			pLinkInfo->iLength = (S32) strlen(pchNameAndHandle);
		}
	}

	return pLinkInfo;
}

// Search for a reference to pchNameAndHandle within the given message
// and generate a ChatData that contains a ChatLinkInfo that points to it.  
// If the pchNameAndHandle can't be found this returns NULL.
SA_RET_OP_VALID extern ChatData *ChatData_CreatePlayerHandleDataFromMessage(const char *pchMessage, const char *pchNameAndHandle, bool bIsGM, bool bIsDev) {
	ChatData *pData = NULL;
	ChatLinkInfo *pLinkInfo = ChatData_CreatePlayerHandleLinkInfoFromMessage(pchMessage, pchNameAndHandle, bIsGM, bIsDev);
	if (pLinkInfo) {
		pData = StructCreate(parse_ChatData);
		eaPush(&pData->eaLinkInfos, pLinkInfo);
	}

	return pData;
}

extern bool ChatData_Encode(SA_PARAM_OP_VALID char **ppchEncodedData, SA_PARAM_OP_VALID ChatData *pData) {
	if (ppchEncodedData) {
		estrClear(ppchEncodedData);

		if (pData) {
			return ParserWriteText(ppchEncodedData, parse_ChatData, pData, 0, 0, 0);
		}
	}

	return false;
}

extern bool ChatData_Decode(SA_PARAM_OP_VALID const char *pchEncodedData, SA_PARAM_OP_VALID ChatData *pData) {
	if (pchEncodedData && *pchEncodedData && pData) {
		return ParserReadText(pchEncodedData, parse_ChatData, pData, 0);
	}

	return false;
}

extern bool ChatData_EncodeLink(SA_PARAM_OP_VALID char **ppchEncodedLink, SA_PARAM_OP_VALID ChatLink *pLink) {
	if (ppchEncodedLink) {
		estrClear(ppchEncodedLink);

		if (pLink) {
			return ParserWriteText(ppchEncodedLink, parse_ChatLink, pLink, 0, 0, 0);
		}
	}

	return false;
}

extern bool ChatData_DecodeLink(SA_PARAM_OP_VALID const char *pchEncodedLink, SA_PARAM_OP_VALID ChatLink *pLink) {
	if (pchEncodedLink && *pchEncodedLink && pLink) {
		return ParserReadText(pchEncodedLink, parse_ChatLink, pLink, 0);
	}

	return false;
}

#include "AutoGen/chatData_h_ast.c"