/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"
#include "Guild.h"
#include "aslGuildServer.h"
#include "itemEnums.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/aslGuildWebCommands_c_ast.h"

AUTO_STRUCT;
typedef struct WebGuild
{
	ContainerID iContainerID;
	
	GuildMember **eaMembers;		AST(UNOWNED)
	GuildMember **eaInvites;		AST(UNOWNED)
	GuildCustomRank **eaRanks;		AST(UNOWNED)
	
	GuildBankLogEntry **eaBankLog;	AST(UNOWNED SERVER_ONLY)
	
	const char *pcName;				AST(UNOWNED)
	const char *pcMotD;				AST(UNOWNED)
	const char *pcDescription;		AST(UNOWNED)

	STRING_POOLED pcEmblem;		AST(POOL_STRING)
	U32 iEmblemColor0;
	U32 iEmblemColor1;
	F32 fEmblemRotation; // 0 to 100
	STRING_POOLED pcEmblem2;	AST(POOL_STRING)
	U32 iEmblem2Color0;
	U32 iEmblem2Color1;
	F32 fEmblem2Rotation; // 0 to 100
	F32 fEmblem2X; // -100 to 100
	F32 fEmblem2Y; // -100 to 100
	F32 fEmblem2ScaleX; // 0 to 100
	F32 fEmblem2ScaleY; // 0 to 100
	STRING_POOLED pcEmblem3;	AST(POOL_STRING)

	U32 iColor1;
	U32 iColor2;
} WebGuild;

AUTO_FIXUPFUNC;
TextParserResult aslGuild_fixup_WebGuild(WebGuild *pWebGuild, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType) {
		case FIXUPTYPE_DESTRUCTOR:
			eaDestroy(&pWebGuild->eaMembers);
			eaDestroy(&pWebGuild->eaInvites);
			eaDestroy(&pWebGuild->eaRanks);
			eaDestroy(&pWebGuild->eaBankLog);
	}
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
WebGuild *aslGuild_webcmd_GetGuild(U32 iGuildID)
{
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	Entity *pGuildBank = aslGuild_GetGuildBank(iGuildID);
	
	if (pGuild) {
		WebGuild *pWebGuild = StructCreate(parse_WebGuild);
		pWebGuild->iContainerID = pGuild->iContainerID;
		eaCopy(&pWebGuild->eaMembers, &pGuild->eaMembers);
		eaCopy(&pWebGuild->eaInvites, &pGuild->eaInvites);
		eaCopy(&pWebGuild->eaRanks, &pGuild->eaRanks);
		eaCopy(&pWebGuild->eaBankLog, &pGuild->eaBankLog);
		pWebGuild->pcName = pGuild->pcName;
		pWebGuild->pcMotD = pGuild->pcMotD;
		pWebGuild->pcDescription = pGuild->pcDescription;
		pWebGuild->pcEmblem = pGuild->pcEmblem;
		pWebGuild->iEmblemColor0 = pGuild->iEmblemColor0;
		pWebGuild->iEmblemColor1 = pGuild->iEmblemColor1;
		pWebGuild->fEmblemRotation = pGuild->fEmblemRotation;
		pWebGuild->pcEmblem2 = pGuild->pcEmblem2;
		pWebGuild->iEmblem2Color0 = pGuild->iEmblem2Color0;
		pWebGuild->iEmblem2Color1 = pGuild->iEmblem2Color1;
		pWebGuild->fEmblem2Rotation = pGuild->fEmblem2Rotation;
		pWebGuild->fEmblem2X = pGuild->fEmblem2X;
		pWebGuild->fEmblem2Y = pGuild->fEmblem2Y;
		pWebGuild->fEmblem2ScaleX = pGuild->fEmblem2ScaleX;
		pWebGuild->fEmblem2ScaleY = pGuild->fEmblem2ScaleY;
		pWebGuild->pcEmblem3 = pGuild->pcEmblem3;
		pWebGuild->iColor1 = pGuild->iColor1;
		pWebGuild->iColor2 = pGuild->iColor2;
		return pWebGuild;
	}
	
	return NULL;
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
int aslGuild_webcmd_FindAccountsHighestGuildMember(U32 iGuildID, ACMD_SENTENCE pcAccount)
{
	Guild *pGuild = aslGuild_GetGuild(iGuildID);
	S32 i;
	U32 iEntID = 0;
	S32 iRank = -1;
	
	if (pGuild) {
		for (i = eaSize(&pGuild->eaMembers)-1; i >= 0; i--) {
			if (!strcmp(pGuild->eaMembers[i]->pcAccount, pcAccount) && pGuild->eaMembers[i]->iRank > iRank) {
				iEntID = pGuild->eaMembers[i]->iEntID;
				iRank = pGuild->eaMembers[i]->iRank;
			}
		}
	}
	
	return iEntID;
}

void aslGuild_webcmd_Invite_CB(TransactionReturnVal *pReturn, U32 *pData)
{
	ContainerID iInviteID;
	ContainerID iEntID = pData ? pData[0] : 0;
	ContainerID iGuildID = pData ? pData[1] : 0;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_dbIDFromPlayerReference(pReturn, &iInviteID);
	
	SAFE_FREE(pData);
	if (!iEntID || !iGuildID) {
		return;
	}
	
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS || !iInviteID) {
		return;
	}
	
	RemoteCommand_aslGuild_Invite(GLOBALTYPE_GUILDSERVER, 0, iGuildID, iEntID, iInviteID, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_Invite(U32 iGuildID, U32 iEntID, ACMD_SENTENCE pcOtherPlayer)
{
	Guild *pGuild = aslGuild_GetGuild(iGuildID);

	if (pGuild)
	{
		U32 *pData = malloc(sizeof(U32)*2);
		pData[0] = iEntID;
		pData[1] = iGuildID;
		RemoteCommand_dbIDFromPlayerReference(objCreateManagedReturnVal(aslGuild_webcmd_Invite_CB, pData),
			GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, pcOtherPlayer, pGuild->iVirtualShardID);
	}
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_AcceptInvite(U32 iGuildID, U32 iEntID)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_AcceptInvite(GetAppGlobalType(), 0, iGuildID, iEntID, NULL, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_DeclineInvite(U32 iGuildID, U32 iEntID)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_DeclineInvite(GetAppGlobalType(), 0, iGuildID, iEntID, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_Promote(U32 iGuildID, U32 iEntID, U32 iSubjectID)
{
	if (!iEntID || !iGuildID || !iSubjectID) {
		return;
	}
	RemoteCommand_aslGuild_Promote(GetAppGlobalType(), 0, iGuildID, iEntID, iSubjectID, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_Demote(U32 iGuildID, U32 iEntID, U32 iSubjectID)
{
	if (!iEntID || !iGuildID || !iSubjectID) {
		return;
	}
	RemoteCommand_aslGuild_Demote(GetAppGlobalType(), 0, iGuildID, iEntID, iSubjectID, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_Leave(U32 iGuildID, U32 iEntID)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_Leave(GetAppGlobalType(), 0, iGuildID, iEntID, iEntID, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_Kick(U32 iGuildID, U32 iEntID, U32 iSubjectID)
{
	if (!iEntID || !iGuildID || !iSubjectID) {
		return;
	}
	RemoteCommand_aslGuild_Leave(GetAppGlobalType(), 0, iGuildID, iEntID, iSubjectID, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_Rename(U32 iGuildID, U32 iEntID, ACMD_SENTENCE pcName)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_Rename(GetAppGlobalType(), 0, iGuildID, iEntID, pcName, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_RenameRank(U32 iGuildID, U32 iEntID, S32 iRank, ACMD_SENTENCE pcName)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_RenameRank(GetAppGlobalType(), 0, iGuildID, iEntID, iRank, pcName, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_RenameBankTab(U32 iGuildID, U32 iEntID, S32 iBagID, ACMD_SENTENCE pcName)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_RenameBankTab(GetAppGlobalType(), 0, iGuildID, iEntID, iBagID + InvBagIDs_Bank1, pcName, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetPermission(U32 iGuildID, U32 iEntID, S32 iRank, S32 ePerm, bool bOn)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetPermission(GetAppGlobalType(), 0, iGuildID, iEntID, iRank, ePerm, bOn, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetBankPermission(U32 iGuildID, U32 iEntID, S32 iBagID, S32 iRank, S32 ePerm, bool bOn)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetBankPermission(GetAppGlobalType(), 0, iGuildID, iEntID, iBagID + InvBagIDs_Bank1, iRank, ePerm, bOn, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetBankWithdrawLimit(U32 iGuildID, U32 iEntID, S32 iBagID, S32 iRank, S32 iLimit)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetBankWithdrawLimit(GetAppGlobalType(), 0, iGuildID, iEntID, iBagID + InvBagIDs_Bank1, iRank, iLimit, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetBankItemWithdrawLimit(U32 iGuildID, U32 iEntID, S32 iBagID, S32 iRank, S32 iCount)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetBankItemWithdrawLimit(GetAppGlobalType(), 0, iGuildID, iEntID, iBagID + InvBagIDs_Bank1, iRank, iCount, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetMotD(U32 iGuildID, U32 iEntID, ACMD_SENTENCE pcMotD)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetMotD(GetAppGlobalType(), 0, iGuildID, iEntID, pcMotD, false, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetPublicComment(U32 iGuildID, U32 iEntID, ACMD_SENTENCE pcComment)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetPublicComment(GetAppGlobalType(), 0, iGuildID, iEntID, pcComment, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetDescription(U32 iGuildID, U32 iEntID, ACMD_SENTENCE pcDescription)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetDescription(GetAppGlobalType(), 0, iGuildID, iEntID, pcDescription, false, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetEmblem(U32 iGuildID, U32 iEntID, ACMD_SENTENCE pcEmblem)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetEmblem(GetAppGlobalType(), 0, iGuildID, iEntID, pcEmblem, false, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetAdvancedEmblem(U32 iGuildID, U32 iEntID, char *pcEmblem, U32 iEmblemColor0, U32 iEmblemColor1, F32 fEmblemRotation)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetAdvancedEmblem(GetAppGlobalType(), 0, iGuildID, iEntID, pcEmblem, iEmblemColor0, iEmblemColor1, fEmblemRotation, true, false, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetAdvancedEmblem2(U32 iGuildID, U32 iEntID, char *pcEmblem2, U32 iEmblem2Color0, U32 iEmblem2Color1, F32 fEmblem2Rotation, F32 fEmblem2X, F32 fEmblem2Y, F32 fEmblem2ScaleX, F32 fEmblem2ScaleY)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetAdvancedEmblem2(GetAppGlobalType(), 0, iGuildID, iEntID, pcEmblem2, iEmblem2Color0, iEmblem2Color1, fEmblem2Rotation, fEmblem2X, fEmblem2Y, fEmblem2ScaleX, fEmblem2ScaleY, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetAdvancedEmblem3(U32 iGuildID, U32 iEntID, char *pcEmblem3)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetAdvancedEmblem3(GetAppGlobalType(), 0, iGuildID, iEntID, pcEmblem3, true, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetColor1(U32 iGuildID, U32 iEntID, U32 iColor)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetColors(GetAppGlobalType(), 0, iGuildID, iEntID, iColor, true, false, NULL, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Guild, XMLRPC) ACMD_ACCESSLEVEL(9);
void aslGuild_webcmd_SetColor2(U32 iGuildID, U32 iEntID, U32 iColor)
{
	if (!iEntID || !iGuildID) {
		return;
	}
	RemoteCommand_aslGuild_SetColors(GetAppGlobalType(), 0, iGuildID, iEntID, iColor, false, false, NULL, NULL, NULL);
}

#include "AutoGen/aslGuildWebCommands_c_ast.c"
