/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

//#include "Message.h"
//#include "GlobalTypeEnum.h"

#include "Entity.h"
#include "Player.h"
#include "progression_common.h"
#include "UIGen.h"
#include "StringUtil.h"
#include "qsortG.h"
#include "gclEntity.h"
#include "progression_transact.h"
#include "SuggestedContentCommon.h"
#include "queue_common.h"

#include "AutoGen/progression_common_h_ast.h"
#include "AutoGen/SuggestedContentCommon_h_ast.h"
#include "AutoGen/gclSuggestedContent_c_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct SuggestedContentClientData
{
	// The tag/list name being requested
	const char* pListTagName;
	
	// The time this list was requested
	U32 uDataRequestTimestamp;
	
	// The time this list was received
	U32 uDataReceiveTimestamp;

	SuggestedContentInfo *pSuggestedContentInfo;	// The info passed from the server
} SuggestedContentClientData;

static SuggestedContentClientData** s_SuggestedContentClientDatas = NULL;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SuggestedContentClientData* gclSuggestedContent_FindClientData(const char* pchListName)
{
	int i;
	for (i=0;i<eaSize(&s_SuggestedContentClientDatas);i++)
	{
		if (s_SuggestedContentClientDatas[i]->pListTagName!=NULL && stricmp(s_SuggestedContentClientDatas[i]->pListTagName,pchListName)==0)
		{
			return(s_SuggestedContentClientDatas[i]);
		}
	}
	return(NULL);
}

void gclSuggestedContent_RemoveClientData(const char* pchListName)
{
	int i;
	for (i=0;i<eaSize(&s_SuggestedContentClientDatas);i++)
	{
		if (s_SuggestedContentClientDatas[i]->pSuggestedContentInfo!=NULL && stricmp(s_SuggestedContentClientDatas[i]->pSuggestedContentInfo->strListName,pchListName)==0)
		{
			StructDestroy(parse_SuggestedContentClientData,s_SuggestedContentClientDatas[i]);
			eaRemove(&s_SuggestedContentClientDatas,i);
			return;
		}
	}
}

void gclSuggestedContent_AddClientData(const char* pchListName, SuggestedContentClientData* pData)
{
	eaPush(&s_SuggestedContentClientDatas,pData);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
															  
// At some point we should probably create the ability to request individual content blocks.
// For now we'll keep the old mechanism of getting them as a chunk in the ui


// Get all content and pass it to the uigen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SuggestedContent_GetAllContent);
void gclSuggestedContent_GetAllContent(SA_PARAM_NN_VALID UIGen *pGen)
{
	SuggestedContentInfo ***peaContentList = ui_GenGetManagedListSafe(pGen, SuggestedContentInfo);

	S32 iCount = 0;

	// Do the pass for all results
	FOR_EACH_IN_EARRAY_FORWARDS(s_SuggestedContentClientDatas, SuggestedContentClientData, pClientData)
	{
		if (pClientData->uDataReceiveTimestamp > 0 && pClientData->pSuggestedContentInfo!=NULL)
		{
			SuggestedContentInfo *pStorageSlot = eaGetStruct(peaContentList, parse_SuggestedContentInfo, iCount++);
			StructCopyAll(parse_SuggestedContentInfo, pClientData->pSuggestedContentInfo, pStorageSlot);
		}
	}
	FOR_EACH_END

	eaSetSizeStruct(peaContentList, parse_SuggestedContentInfo, iCount);

	ui_GenSetManagedListSafe(pGen, peaContentList, SuggestedContentInfo, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SuggestedContent_IsContentLoading);
 bool gclSuggestedContent_IsContentLoading(const char *pchContentListName)
{
	SuggestedContentClientData* pCurrentData = gclSuggestedContent_FindClientData(pchContentListName);

	return(pCurrentData!=NULL && pCurrentData->uDataReceiveTimestamp == 0);
}

// Request content from a named list. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SuggestedContent_Request);
void gclSuggestedContent_RequestSuggestedContent(const char *pchContentListName)
{
	SuggestedContentClientData* pCurrentData = gclSuggestedContent_FindClientData(pchContentListName);

	if (pCurrentData==NULL)
	{
		pCurrentData=StructCreate(parse_SuggestedContentClientData);

		pCurrentData->pListTagName = StructAllocString(pchContentListName);

		// Add a dummy empty data so we can track times
		// Timestamps should be zero
		gclSuggestedContent_AddClientData(pchContentListName, pCurrentData);
	}

	if (pCurrentData!=NULL)
	{
		// Check for last time we asked or received to throttle things
		if ((pCurrentData->uDataReceiveTimestamp==0 && timeSecondsSince2000() - pCurrentData->uDataRequestTimestamp > 5) || 
			(pCurrentData->uDataReceiveTimestamp>0 && timeSecondsSince2000() - pCurrentData->uDataReceiveTimestamp > 5))
		{
			
			// We did not receive home page content in the last 5 seconds		
			pCurrentData->uDataReceiveTimestamp=0; // Invalidate the old data
			pCurrentData->uDataRequestTimestamp=timeSecondsSince2000();	// Note our request time
			
			// Ask the game server for the home page content
			ServerCmd_gslSuggestedContent_GetContent(pchContentListName);

			// Note that's it's possible that we made a request and never received anything (perhaps there was nothing valid in the list)
		}
	}
}


AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclSuggestedContentReceiveInfo(SuggestedContentInfo* pContent)
{
	SuggestedContentClientData* pCurrentData = gclSuggestedContent_FindClientData(pContent->strListName);

	// If we don't find a data we didn't request one. 
	if (pCurrentData!=NULL)
	{
		pCurrentData->uDataReceiveTimestamp = timeSecondsSince2000();
		if (pCurrentData->pSuggestedContentInfo)
		{
			StructDestroy(parse_SuggestedContentInfo, pCurrentData->pSuggestedContentInfo);
		}
		pCurrentData->pSuggestedContentInfo = StructClone(parse_SuggestedContentInfo, pContent);
	}
}



AUTO_RUN;
void gclSuggestedContentAutoRegister(void)
{
	ui_GenInitStaticDefineVars(SuggestedContentTypeEnum, "SuggestedContentType_");
}

#include "AutoGen/gclSuggestedContent_c_ast.c"

