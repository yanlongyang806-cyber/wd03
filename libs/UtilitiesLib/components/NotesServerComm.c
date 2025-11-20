#include "NotesServerComm.h"
#include "net.h"
#include "StashTable.h"
#include "NotesServerComm_c_ast.h"
#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "ResourceInfo.h"
#include "HttpXpathSupport.h"
#include "../../Utilities/NotesServer/NotesServer_pub.h"
#include "utilitiesLib.h"
#include "url.h"
#include "file.h"
#include "NotesServer_pub_h_ast.h"
#include "StringUtil.h"
#include "structNet.h"
#include "cmdParse.h"
#include "sock.h"
#include "Alerts.h"
#include "continuousBuildeRSupport.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

//Alert if we try to set a note on the note server but fail for this many seconds
int siNoteSettingFailureAlertTime = 60;
AUTO_CMD_INT(siNoteSettingFailureAlertTime, NoteSettingFailureAlertTime);

//if non-NULL, then we are "active"
NotesRegisterStruct *spRegisterStruct = NULL;


static CommConnectFSM *spNotesServerConnectFSM = NULL;
static NetLink *spNotesServerNetLink = NULL;

static NotesServerInfoStruct *spServerInfoStruct = NULL;

static bool sbNotesServerConnectedLastTick = false;
static U32 siLastNoteServerConnectTime = 0;

char sNotesServerName[128] = "NotesServer";
AUTO_CMD_STRING(sNotesServerName, NotesServerName) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;

AUTO_STRUCT;
typedef struct CachedNoteToSet
{
	U32 iTime;
	bool bAlerted;

	SingleNote *pNote;

	char *pSettingComment;
} CachedNoteToSet;

CachedNoteToSet **sppCachedNotesToSet = NULL;


StashTable gSingleNotesByName = NULL;

static void LazyCreateStashTable(void)
{
	if (!gSingleNotesByName)
	{
		gSingleNotesByName = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("Notes",  RESCATEGORY_OTHER, 0, gSingleNotesByName, parse_SingleNote);
	}
}

char *NotesServer_GetConnectionStatusString(void)
{
	static char *spRetVal = NULL;
	estrClear(&spRetVal);

	if (!spRegisterStruct)
	{
		return "NotesServerComm never initted";
	}

	if (!sNotesServerName[0])
	{
		return "No notes server name set";
	}

	if (sbNotesServerConnectedLastTick)
	{
		return NULL;
	}

	if (siLastNoteServerConnectTime)
	{
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - siLastNoteServerConnectTime, &spRetVal);
		estrInsertf(&spRetVal, 0, "Currently disconnected from Notes Server %s, last contact: ", sNotesServerName);
		estrConcatf(&spRetVal, " ago");
		return spRetVal;
	}

	estrPrintf(&spRetVal, "Never connected to Notes Server %s", sNotesServerName);
	return spRetVal;
}

void NotesServer_InitSystemAndConnect(char *pDomainName, char *pSystemName)
{
	if (spRegisterStruct)
	{
		return;
	}

	LazyCreateStashTable();

	spRegisterStruct = StructCreate(parse_NotesRegisterStruct);
	spRegisterStruct->pDomainName = strdup(pDomainName);
	spRegisterStruct->pSystemName = strdup(pSystemName);
	spRegisterStruct->pProductName = strdup(GetProductName());
	
}

static void AddOrUpdateSingleNote(SingleNote *pInNote)
{
	SingleNote *pNote;
	NoteScopeType eScope;


	LazyCreateStashTable();

	if (stashFindPointer(gSingleNotesByName, pInNote->pNoteName, &pNote))
	{
		//do nothing
	}
	else
	{
		pNote = StructCreate(parse_SingleNote);
		estrCopy(&pNote->pNoteName, &pInNote->pNoteName);
		stashAddPointer(gSingleNotesByName, pNote->pNoteName, pNote, false);
	}

	for (eScope = 0; eScope < NOTESCOPE_COUNT; eScope++)
	{
		if (stricmp_safe(pInNote->comments[eScope].pNormal, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			estrCopy(&pNote->comments[eScope].pNormal, &pInNote->comments[eScope].pNormal);
		}

		if (stricmp_safe(pInNote->comments[eScope].pCritical, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			estrCopy(&pNote->comments[eScope].pCritical, &pInNote->comments[eScope].pCritical);
		}
	}

	SingleNoteWasUpdated(pNote);
}

void NotesServer_SetSingleNote(SingleNote *pNote)
{
	AddOrUpdateSingleNote(pNote);
}


SingleNote *CreateMostlyLeaveAsNote(char *pNoteName, NoteScopeType eInScope, bool bCritical, char *pCommentText)
{
	SingleNote *pRetVal = StructCreate(parse_SingleNote);
	NoteScopeType eScope;

	estrCopy2(&pRetVal->pNoteName, pNoteName);

	for (eScope = 0; eScope < NOTESCOPE_COUNT; eScope++)
	{
		if (eScope == eInScope)
		{
			if (bCritical)
			{
				estrCopy2(&pRetVal->comments[eScope].pCritical, pCommentText);
				estrCopy2(&pRetVal->comments[eScope].pNormal, NOTE_SETCOMMENT_LEAVE_AS_IS);
			}
			else
			{
				estrCopy2(&pRetVal->comments[eScope].pCritical, NOTE_SETCOMMENT_LEAVE_AS_IS);
				estrCopy2(&pRetVal->comments[eScope].pNormal, pCommentText);
			}
		}
		else
		{
			estrCopy2(&pRetVal->comments[eScope].pCritical, NOTE_SETCOMMENT_LEAVE_AS_IS);
			estrCopy2(&pRetVal->comments[eScope].pNormal, NOTE_SETCOMMENT_LEAVE_AS_IS);
		}
	}

	return pRetVal;
}


void NotesServerMessageCB(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	switch (cmd)
	{
	xcase FROM_NOTESSERVER_HERE_IS_SERVER_INFO:
		StructDestroySafe(parse_NotesServerInfoStruct, &spServerInfoStruct);
		spServerInfoStruct = StructCreate(parse_NotesServerInfoStruct);
		ParserRecvStructSafe(parse_NotesServerInfoStruct, pak, spServerInfoStruct);

	xcase FROM_NOTESSERVER_HERE_ARE_NOTES:
		{
			SingleNoteList *pList = StructCreate(parse_SingleNoteList);
			ParserRecvStructSafe(parse_SingleNoteList, pak, pList);
			FOR_EACH_IN_EARRAY(pList->ppNotes, SingleNote, pNote)
			{
				AddOrUpdateSingleNote(pNote);
			}
			FOR_EACH_END;
			StructDestroy(parse_SingleNoteList, pList);
		}


		break;
	}

}

char *GetDescriptionOfCachedNote(CachedNoteToSet *pCache)
{
	static char *spRetVal = NULL;
	int iScope;

	if (!pCache->pNote)
	{
		return NULL;
	}

	estrPrintf(&spRetVal, "Trying to set note %s because (%s). ", pCache->pNote->pNoteName, pCache->pSettingComment);

	for (iScope = 0; iScope < NOTESCOPE_COUNT; iScope++)
	{
		char *pNormal = pCache->pNote->comments[iScope].pNormal;
		char *pCritical = pCache->pNote->comments[iScope].pCritical;
		
		if (stricmp_safe(pNormal, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			estrConcatf(&spRetVal, "Setting non-critical %s comment to: %s", StaticDefineIntRevLookup(NoteScopeTypeEnum, iScope), pNormal);
		}

		if (stricmp_safe(pCritical, NOTE_SETCOMMENT_LEAVE_AS_IS) != 0)
		{
			estrConcatf(&spRetVal, "Setting critical %s comment to: %s", StaticDefineIntRevLookup(NoteScopeTypeEnum, iScope), pCritical);
		}
	}

	return spRetVal;
}


void NotesServer_Tick(void)
{

	if (!spRegisterStruct || !sNotesServerName[0] || g_isContinuousBuilder)
	{
		return;
	}

	if (commConnectFSMForTickFunctionWithRetrying(&spNotesServerConnectFSM, &spNotesServerNetLink, 
		"link to Notes Server",
			2.0f, commDefault(), LINKTYPE_SHARD_CRITICAL_20MEG, LINK_FORCE_FLUSH,
			sNotesServerName, DEFAULT_NOTESSERVER_PORT ,NotesServerMessageCB,0,0,0, NULL, 0, NULL, 0))
	{
		if (!sbNotesServerConnectedLastTick)
		{
			Packet *pOutPack = pktCreate(spNotesServerNetLink, TO_NOTESSERVER_REQUESTING_NOTES);
			ParserSendStructSafe(parse_NotesRegisterStruct, pOutPack, spRegisterStruct);
			pktSend(&pOutPack);
		}
		
		if (eaSize(&sppCachedNotesToSet))
		{
			FOR_EACH_IN_EARRAY(sppCachedNotesToSet, CachedNoteToSet, pCachedNote)
			{
				Packet *pOutPack = pktCreate(spNotesServerNetLink, TO_NOTESSERVER_SETTING_NOTE);
				NoteSettingStruct *pSettingStruct = StructCreate(parse_NoteSettingStruct);
				pSettingStruct->pRegisterStruct = spRegisterStruct;
				pSettingStruct->pSettingComment = strdup(pCachedNote->pSettingComment);

				pSettingStruct->pNote = pCachedNote->pNote;
				pCachedNote->pNote = NULL;
			
				ParserSendStructSafe(parse_NoteSettingStruct, pOutPack, pSettingStruct);
				pktSend(&pOutPack);

				pSettingStruct->pRegisterStruct = NULL;
				StructDestroy(parse_NoteSettingStruct, pSettingStruct);
			}

			FOR_EACH_END;

			eaDestroyStruct(&sppCachedNotesToSet, parse_CachedNoteToSet);
		}

		sbNotesServerConnectedLastTick = true;
		siLastNoteServerConnectTime = timeSecondsSince2000();

	}
	else
	{
		sbNotesServerConnectedLastTick = false;

		if (eaSize(&sppCachedNotesToSet))
		{
			FOR_EACH_IN_EARRAY(sppCachedNotesToSet, CachedNoteToSet, pCachedNote)
			{
				if (!pCachedNote->bAlerted && pCachedNote->iTime < timeSecondsSince2000() - siNoteSettingFailureAlertTime)
				{
					
					char *pDurationString = NULL;

					pCachedNote->bAlerted = true;

					timeSecondsDurationToPrettyEString(siNoteSettingFailureAlertTime, &pDurationString);
					WARNING_NETOPS_ALERT("CANT_SET_NOTE", "Have been trying <<%s>> for %s, can't contact notes server at %s. Will keep trying until we succeed",
						GetDescriptionOfCachedNote(pCachedNote), pDurationString, sNotesServerName);

					estrDestroy(&pDurationString);
				}
			}
			FOR_EACH_END;
		}
	}
}



AUTO_COMMAND ACMD_ALLOW_JSONRPC;
char *NotesServer_SetNote(CmdContext *pContext, char *pNoteName, char *pScopeName, bool bCritical, ACMD_SENTENCE pNoteText)
{
	NoteScopeType eScopeType = StaticDefineIntGetInt(NoteScopeTypeEnum, pScopeName);
	if (eScopeType == -1)
	{
		return "Unknown Scope type";
	}

	if (pNoteName && pNoteName[0])
	{
		CachedNoteToSet *pCachedNote = StructCreate(parse_CachedNoteToSet);

		
		pCachedNote->iTime = timeSecondsSince2000();
		pCachedNote->pSettingComment = strdupf("Set via %s by %s", GetContextHowString(pContext), pContext->pAuthNameAndIP);
		pCachedNote->pNote = CreateMostlyLeaveAsNote(pNoteName, eScopeType, bCritical, pNoteText);
		
		eaPush(&sppCachedNotesToSet, pCachedNote);


		AddOrUpdateSingleNote(pCachedNote->pNote);
		return "Note has been set";

	}

	return "No note name specified";
}	

AUTO_STRUCT;
typedef struct SingleNoteOverview
{
	char *pNoteServerStatusString; AST(FORMATSTRING(HTML=1,HTML_NO_HEADER=1, HTML_CLASS = "divWarning2"))
	char *pTitle;
	char *pNoteName; AST(FORMATSTRING(HTML_SKIP=1))
	SingleNote *pNote;
	char *pLinkOnNotesServer; AST(ESTRING FORMATSTRING(HTML=1,HTML_NO_HEADER=1))
	
	AST_COMMAND("Set", "NotesServer_SetNote $FIELD(NoteName) $SELECT(Scope|Global,Product,System) $INT(Critical?) $STRING(Type new note here)")
} SingleNoteOverview;

AUTO_STRUCT;
typedef struct NoteNameAndLink
{
	char *pName; AST(KEY, POOL_STRING)
	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1))
} NoteNameAndLink;

AUTO_STRUCT;
typedef struct NoteList
{
	NoteNameAndLink **ppNotes;
} NoteList;

static void SetNotesServerLink(SingleNoteOverview *pOverview)
{
	if (spServerInfoStruct && spRegisterStruct)
	{
		estrPrintf(&pOverview->pLinkOnNotesServer, "<a href=\"http://%s:%u/viewxpath?xpath=NotesServer[0].globObj.Notes[%s.%s]\">Link on notes server</a>",
			makeIpStr(spServerInfoStruct->iIP), spServerInfoStruct->iPort, spRegisterStruct->pDomainName, pOverview->pNoteName);
	}

}

static SingleNoteOverview *GetSingleNoteOverview(SingleNote *pNote, UrlArgumentList *pArgList)
{
	SingleNoteOverview *pRetVal = StructCreate(parse_SingleNoteOverview);
	const char *pTitle = urlFindValue(pArgList, "svrTitle");
	char *pStatusString = NotesServer_GetConnectionStatusString();

	pRetVal->pNoteName = strdup(pNote->pNoteName);
	pRetVal->pNote = StructClone(parse_SingleNote, pNote);

	if (pTitle && pTitle[0])
	{
		pRetVal->pTitle = strdup(pTitle);
	}
	else
	{
		pRetVal->pTitle = strdupf("Setting for note %s", pRetVal->pNoteName);
	}

	SetNotesServerLink(pRetVal);

	if (pStatusString)
	{
		pRetVal->pNoteServerStatusString = strdup(pStatusString);
	}


	return pRetVal;
}


static SingleNoteOverview *GetSingleNoteOverviewFromNoteName(char *pNoteName, UrlArgumentList *pArgList)
{
	SingleNoteOverview *pRetVal = StructCreate(parse_SingleNoteOverview);
	const char *pTitle = urlFindValue(pArgList, "svrTitle");
	char *pStatusString = NotesServer_GetConnectionStatusString();

	pRetVal->pNoteName = strdup(pNoteName);

	if (pTitle && pTitle[0])
	{
		pRetVal->pTitle = strdup(pTitle);
	}
	else
	{
		pRetVal->pTitle = strdupf("Setting for note %s", pNoteName);
	}

	SetNotesServerLink(pRetVal);
	
	if (pStatusString)
	{
		pRetVal->pNoteServerStatusString = strdup(pStatusString);
	}

	return pRetVal;
	
}



bool ProcessNoteIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pFirstBracket;
	SingleNote *pSingleNote;
	bool bRetVal;

	if (pLocalXPath[0] != '[')
	{
		static NoteList *pList = NULL;

		if (pList)
		{
			StructReset(parse_NoteList, pList);
		}
		else
		{
			pList = StructCreate(parse_NoteList);
		}

		FOR_EACH_IN_STASHTABLE2(gSingleNotesByName, pElem)
		{
			NoteNameAndLink *pLink = StructCreate(parse_NoteNameAndLink);
			pLink->pName = stashElementGetStringKey(pElem);
			estrPrintf(&pLink->pLink, "<a href=\"%s.Notes[%s]\">Link</a>", 
						LinkToThisServer(), pLink->pName);
			eaPush(&pList->ppNotes, pLink);
		}
		FOR_EACH_END;

	
		return ProcessStructIntoStructInfoForHttp("", pArgList,
			pList, parse_NoteList, iAccessLevel, 0, pStructInfo, eFlags);
	}

	pFirstBracket = strchr(pLocalXPath, ']');
	if (!pFirstBracket)
	{
		GetMessageForHttpXpath("Error - expected ] (format should be .Notes[tablename])", pStructInfo, true);
		return true;
	}

	*pFirstBracket = 0;

	if (stashFindPointer(gSingleNotesByName, pLocalXPath + 1, &pSingleNote))
	{
		SingleNoteOverview *pSingleNoteOverview = GetSingleNoteOverview(pSingleNote, pArgList);
		bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList, pSingleNoteOverview, parse_SingleNoteOverview,
			iAccessLevel, 0, pStructInfo, eFlags);
		StructDestroy(parse_SingleNoteOverview, pSingleNoteOverview);
		return bRetVal;
	}
	else
	{
		SingleNoteOverview *pSingleNoteOverview = GetSingleNoteOverviewFromNoteName(pLocalXPath + 1, pArgList);
		bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList, pSingleNoteOverview, parse_SingleNoteOverview,
			iAccessLevel, 0, pStructInfo, eFlags);
		StructDestroy(parse_SingleNoteOverview, pSingleNoteOverview);
		return bRetVal;
	}
}





AUTO_RUN;
void NoteServerHttpInit(void)
{
	RegisterCustomXPathDomain(".Notes", ProcessNoteIntoStructInfoForHttp, NULL);
}

static bool NoteHasCommentsSet(SingleNote *pNote, bool bCritical)
{
	int iScope;

	for (iScope = 0; iScope < NOTESCOPE_COUNT; iScope++)
	{
		char *pTemp = bCritical ? pNote->comments[iScope].pCritical : pNote->comments[iScope].pNormal;
		if (pTemp && pTemp[0])
		{
			return true;
		}
	}

	return false;
}

char *GetScopeComment(NoteScopeType eScope)
{
	static char *spComments[NOTESCOPE_COUNT] = {0};

	if (!spComments[eScope])
	{
		switch (eScope)
		{
		xcase NOTESCOPE_GLOBAL:
			spComments[eScope] = "Global";
		xcase NOTESCOPE_PRODUCT:
			spComments[eScope] = strdupf("%s-specific", GetProductName());
		xcase NOTESCOPE_SYSTEM:
			spComments[eScope] = strdupf("%s-specific", GetNotesSystemName());
		}
	}
	return spComments[eScope];
}

char *NotesServer_GetLinkToNoteServerMonPage(char *pNoteName, char *pDescriptiveTitle, bool bPopLeft, bool bDesigner)
{
	
	static char *spTemplate_Set = NULL;
	static char *spTemplate_UnSet = NULL;
	static char *spRetVal = NULL;
	char *pTitleString=NULL;
	char *pIconString = NULL;
	char *pLinkString = NULL;
	char *pBody = NULL;
	char *pTemp = NULL;

	bool bNotesExist = false;

	SingleNote *pNote;

	if (!spTemplate_Set)
	{
		spTemplate_Set = fileAlloc("server/MCP/templates/NotesLinkTemplate_Set.txt", NULL);
		if (!spTemplate_Set)
		{
			return "(Notes links are broken)";
		}
	}
	if (!spTemplate_UnSet)
	{
		spTemplate_UnSet = fileAlloc("server/MCP/templates/NotesLinkTemplate_UnSet.txt", NULL);
		if (!spTemplate_UnSet)
		{
			return "(Notes links are broken)";
		}
	}


	if (pDescriptiveTitle && pDescriptiveTitle[0])
	{
		estrCopyWithURIEscaping(&pTitleString, pDescriptiveTitle);
	}

	estrCopy2(&pIconString, "static/images/notesempty.jpg");


	if (stashFindPointer(gSingleNotesByName, pNoteName, &pNote))
	{
		if (NoteHasCommentsSet(pNote, true))
		{
			estrCopy2(&pIconString, "static/images/notesred.jpg");
			bNotesExist = true;
		}
		else if (NoteHasCommentsSet(pNote, false))
		{
			estrCopy2(&pIconString, "static/images/notesgreen.jpg");
			bNotesExist = true;
		}

		if (bNotesExist)
		{
			int iScope;

			for (iScope = 0; iScope < NOTESCOPE_COUNT; iScope++)
			{
				char *pNormal = pNote->comments[iScope].pNormal;
				char *pCritical = pNote->comments[iScope].pCritical;

				if (pCritical && pCritical[0])
				{
					estrClear(&pTemp);
					estrCopyWithHTMLEscaping(&pTemp, pCritical, false);
					estrConcatf(&pBody, "<p class=\"Critical\">%s<sub>(%s)</sub></p>\n", pTemp, GetScopeComment(iScope));
				}

				if (pNormal && pNormal[0])
				{
					estrClear(&pTemp);
					estrCopyWithHTMLEscaping(&pTemp, pNormal, false);
					estrConcatf(&pBody, "<p>%s<sub>(%s)</sub></p>\n", pTemp, GetScopeComment(iScope));
				}
			}
		}
	}



	if (ShardInfoStringWasSet() || GetAppGlobalType() == GLOBALTYPE_MASTERCONTROLPROGRAM)
	{
		estrPrintf(&pLinkString, "/viewxpath?xpath=CONTROLLER[0].notes[%s]", pNoteName);
	}
	else
	{
		estrPrintf(&pLinkString, "%s.notes[%s]", LinkToThisServer(), pNoteName);
	}

	estrConcatf(&pLinkString, "&pretty=1&system=%s&product=%s", 
		GetNotesSystemName(),
		GetProductName());

	if (bDesigner)
	{
		estrConcatf(&pLinkString, "&designer=1");
	}


	if (bNotesExist)
	{
		estrCopy2(&spRetVal, spTemplate_Set);
	}
	else
	{
		estrCopy2(&spRetVal, spTemplate_UnSet);
	}


	estrReplaceOccurrences(&spRetVal, "$ICON", pIconString);
	estrReplaceOccurrences(&spRetVal, "$NOTE", pNoteName);
	estrReplaceOccurrences(&spRetVal, "$TITLE", pTitleString ? pTitleString : "");
	estrReplaceOccurrences(&spRetVal, "$LINK", pLinkString);
	estrReplaceOccurrences(&spRetVal, "$BODY", pBody);

	if (bPopLeft)
	{
		estrReplaceOccurrences(&spRetVal, "$POP_CLASS", "pop_left");
	}
	else
	{
		estrReplaceOccurrences(&spRetVal, "$POP_CLASS", "pop_right");
	}

	estrDestroy(&pTitleString);
	estrDestroy(&pLinkString);
	estrDestroy(&pIconString);

	estrDestroy(&pBody);
	estrDestroy(&pTemp);

	if (isDevelopmentMode())
	{
		SAFE_FREE(spTemplate_Set);
		SAFE_FREE(spTemplate_UnSet);
	}

	return spRetVal;
}


/*
	static char *spRetVal = NULL;
	char *pTitleString = NULL;
	char *pUniqueTag = NULL;
	char *pMessageUniqueTag = NULL;


	if (pDescriptiveTitle && pDescriptiveTitle[0])
	{
		estrCopyWithURIEscaping(&pTitleString, pDescriptiveTitle);
	}

	estrPrintf(&spRetVal, "<a href=\"");

	if (ShardInfoStringWasSet())
	{
		estrConcatf(&spRetVal, "/viewxpath?xpath=CONTROLLER[0].notes[%s]", pNoteName);
	}
	else
	{
		estrConcatf(&spRetVal, "%s.notes[%s]", LinkToThisServer(), pNoteName);
	}

	if (estrLength(&pTitleString))
	{
		estrConcatf(&spRetVal, "&svrTitle=%s", pTitleString);
		estrDestroy(&pTitleString);
	}

	estrConcatf(&spRetVal, "\" target=\"_blank\"><img src=\"static/images/notesgreen.jpg\"></a>"); 
	
	*/

//<a class="rollover" id="rollover_unique" href="/viewxpath?xpath=CONTROLLER[0].notes[controller.custom.Databases]&svrTitle=The+Databases+link+on+the+serverMon+front+page" target="_blank"><img src="static/images/notesgreen.jpg"></a>

/*
<style>
.rollover_unique .rollover_message_unique {
	display: none;
}
.rollover_unique:hover .rollover_message_unique {
	display: block;
}
</style>

  <span id="rollover_message_unique" class="rollover_message">
                   Here is some text
                </span>
				*/


void DEFAULT_LATELINK_SingleNoteWasUpdated(SingleNote *pNote)
{

}

char *DEFAULT_LATELINK_GetNotesSystemName(void)
{
	if (spRegisterStruct)
	{
		return spRegisterStruct->pSystemName;
	}
	else
	{
		return "Unknown... someone needs to do some LATELINKing of GetNotesSystemName()";
	}
}

bool NotesServer_NoteIsSet(char *pNoteName)
{
	SingleNote *pNote;

	if (!stashFindPointer(gSingleNotesByName, pNoteName, &pNote))
	{
		return false;
	}

	return (NoteHasCommentsSet(pNote, true) || NoteHasCommentsSet(pNote, false));
}


#include "NotesServerComm_c_ast.c"
#include "NotesServer_pub_h_ast.c"
