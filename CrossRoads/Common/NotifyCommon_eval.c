/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "error.h"
#include "ExpressionPrivate.h"
#include "NotifyCommon.h"

#ifdef GAMECLIENT
#include "gclEntity.h"
#include "gclNotify.h"
#endif

#ifdef GAMESERVER
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "contact_common.h"
#include "CostumeCommonLoad.h"
#include "gslContact.h"
#include "Player.h"
#endif

#include "NotifyEnum_h_ast.h"

// ----------------------------------------------------------------------------------
// Notify Expression Functions
// ----------------------------------------------------------------------------------

// Send a notification.
AUTO_EXPR_FUNC(UIGen, player) ACMD_NAME(NotifySend);
void notify_FuncNotifySend(ExprContext *pContext, ACMD_NAMELIST(NotifyTypeEnum, STATICDEFINE) const char *pcType, const char *pcDisplayString, const char *pcLogicalString, const char *pcTexture)
{
#if defined(GAMESERVER)
	if (pContext->selfPtr == NULL || pContext->selfPtr->pPlayer == NULL) {
		Errorf("NotifySend failed: No player available in this context");
	}
#endif

	notify_NotifySend(pContext->selfPtr, StaticDefineIntGetInt(NotifyTypeEnum, pcType), pcDisplayString, pcLogicalString, pcTexture);
}


// Send a notification with a tag.
AUTO_EXPR_FUNC(UIGen, player) ACMD_NAME(NotifySendWithTag);
void notify__FuncSendWithTag(ExprContext *pContext, ACMD_NAMELIST(NotifyTypeEnum, STATICDEFINE) const char *pcType, const char *pcDisplayString, const char *pcLogicalString, const char *pcTexture, const char *pcTag)
{
#if defined(GAMESERVER)
	if (pContext->selfPtr == NULL || pContext->selfPtr->pPlayer == NULL) {
		Errorf("NotifySendWithTag failed: No player available in this context");
	}
#endif

	notify_NotifySendWithTag(pContext->selfPtr, StaticDefineIntGetInt(NotifyTypeEnum, pcType), pcDisplayString, pcLogicalString, pcTexture, pcTag);
}


// Send a notification.
AUTO_EXPR_FUNC(UIGen, player) ACMD_NAME(NotifySendAudio);
void notify_FuncSendAudio(ExprContext *pContext, ACMD_NAMELIST(NotifyTypeEnum, STATICDEFINE) const char *pcType, const char *pcDisplayString, const char *pcLogicalString, const char *pcSound, const char *pcTexture)
{
#if defined(GAMESERVER)
	if (pContext->selfPtr == NULL || pContext->selfPtr->pPlayer == NULL) {
		Errorf("NotifySendWithTag failed: No player available in this context");
	}
#endif

	notify_NotifySendAudio(NULL, StaticDefineIntGetInt(NotifyTypeEnum, pcType), pcDisplayString, pcLogicalString, pcSound, pcTexture);
}


AUTO_EXPR_FUNC(UIGen, player) ACMD_NAME(NotifyQueueClearTag);
void notify_FuncQueueClearTag(ExprContext *pContext, const char *pcQueue,const char *pcTag)
{
#if defined(GAMESERVER)
	if (pContext->selfPtr && pContext->selfPtr->pPlayer) {
		ClientCmd_NotifyQueueClearTag(pContext->selfPtr, pcQueue, pcTag);
	} else {
		Errorf("NotifyQueueClearTag failed: No player available in this context");
	}
#else
	assertmsg(!pContext->selfPtr || pContext->selfPtr == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyQueueClearTag(pcQueue, pcTag);
#endif
}

// Send a mini contact notification
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMiniContactSpecifiedCritter);
void SendMiniContactSpecifiedCritter(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchSound,
									 const char* pchHeadshotStyle, ACMD_EXPR_DICT(PlayerCostume) const char *pchCostumeName)
{
#if defined(GAMESERVER)
	int i;
	ContactCostume *pContactCostume = StructCreate(parse_ContactCostume);

	pContactCostume->eCostumeType = ContactCostumeType_Specified;
	SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pchCostumeName, pContactCostume->costumeOverride);
	pContactCostume->eCostumeCritterGroupType = ContactMapVarOverrideType_Specified;

	for (i = 0; i < eaSize(entsIn); i++)
	{
		Entity *pEnt = (*entsIn)[i];
		if (pEnt && pEnt->pPlayer)
		{
			int eLang = entGetLanguage(pEnt);
			const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
			ContactHeadshotData *pHeadshotData = NULL;

			if (!pchTranslation)
			{
				continue;
			}

			pHeadshotData = StructCreate(parse_ContactHeadshotData);
			contact_CostumeToHeadshotData(pEnt, pContactCostume, &pHeadshotData);
			pHeadshotData->pchHeadshotStyleDef = allocAddString(pchHeadshotStyle);

			notify_NotifySendWithHeadshot(pEnt, kNotifyType_MiniContact, pchTranslation, NULL, pchSound, pHeadshotData);

			StructDestroy(parse_ContactHeadshotData, pHeadshotData);
		}
	}

	StructDestroy(parse_ContactCostume, pContactCostume);
#endif
}

// Send a mini contact notification
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMiniContactPetContact);
void SendMiniContactPetContact(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchSound,
							   const char* pchHeadshotStyle, const char *pchPetContactListName)
{
#if defined(GAMESERVER)
	int i;
	ContactCostume *pContactCostume = StructCreate(parse_ContactCostume);

	pContactCostume->eCostumeType = ContactCostumeType_PetContactList;
	SET_HANDLE_FROM_STRING("PetContactList", pchPetContactListName, pContactCostume->hPetOverride);
	pContactCostume->eCostumeCritterGroupType = ContactMapVarOverrideType_Specified;

	for (i = 0; i < eaSize(entsIn); i++)
	{
		Entity *pEnt = (*entsIn)[i];
		if (pEnt && pEnt->pPlayer)
		{
			int eLang = entGetLanguage(pEnt);
			const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
			ContactHeadshotData *pHeadshotData = NULL;

			if (!pchTranslation)
			{
				continue;
			}

			pHeadshotData = StructCreate(parse_ContactHeadshotData);
			contact_CostumeToHeadshotData(pEnt, pContactCostume, &pHeadshotData);
			pHeadshotData->pchHeadshotStyleDef = allocAddString(pchHeadshotStyle);

			notify_NotifySendWithHeadshot(pEnt, kNotifyType_MiniContact, pchTranslation, NULL, pchSound, pHeadshotData);

			StructDestroy(parse_ContactHeadshotData, pHeadshotData);
		}
	}

	StructDestroy(parse_ContactCostume, pContactCostume);
#endif
}

// Send a mini contact notification
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMiniContactCritterGroup);
void SendMiniContactCritterGroup(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchSound,
								 const char* pchHeadshotStyle, ACMD_EXPR_DICT(CritterGroup) const char *pchCritterGroupName, const char *pchContactName)
{
#if defined(GAMESERVER)
	int i;
	ContactCostume *pContactCostume = StructCreate(parse_ContactCostume);

	pContactCostume->eCostumeType = ContactCostumeType_CritterGroup;
	SET_HANDLE_FROM_STRING(g_hCritterGroupDict, pchCritterGroupName, pContactCostume->hCostumeCritterGroup);
	pContactCostume->eCostumeCritterGroupType = ContactMapVarOverrideType_Specified;

	if (pchContactName)
		pContactCostume->pchCostumeIdentifier = allocAddString(pchContactName);

	for (i = 0; i < eaSize(entsIn); i++)
	{
		Entity *pEnt = (*entsIn)[i];
		if (pEnt && pEnt->pPlayer)
		{
			int eLang = entGetLanguage(pEnt);
			const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
			ContactHeadshotData *pHeadshotData = NULL;

			if (!pchTranslation)
			{
				continue;
			}

			pHeadshotData = StructCreate(parse_ContactHeadshotData);
			contact_CostumeToHeadshotData(pEnt, pContactCostume, &pHeadshotData);
			pHeadshotData->pchHeadshotStyleDef = allocAddString(pchHeadshotStyle);

			notify_NotifySendWithHeadshot(pEnt, kNotifyType_MiniContact, pchTranslation, NULL, pchSound, pHeadshotData);

			StructDestroy(parse_ContactHeadshotData, pHeadshotData);
		}
	}

	StructDestroy(parse_ContactCostume, pContactCostume);
#endif
}

// Send a mini contact notification
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMiniContactMapVarCritterGroup);
void SendMiniContactMapVarCritterGroup(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchSound,
									   const char* pchHeadshotStyle, const char *pchMapVariableName, const char *pchContactName)
{
#if defined(GAMESERVER)
	int i;
	ContactCostume *pContactCostume = StructCreate(parse_ContactCostume);

	pContactCostume->eCostumeType = ContactCostumeType_CritterGroup;
	pContactCostume->eCostumeCritterGroupType = ContactMapVarOverrideType_MapVar;

	if (pchMapVariableName)
		pContactCostume->pchCostumeMapVar = allocAddString(pchMapVariableName);
	if (pchContactName)
		pContactCostume->pchCostumeIdentifier = allocAddString(pchContactName);

	for (i = 0; i < eaSize(entsIn); i++)
	{
		Entity *pEnt = (*entsIn)[i];
		if (pEnt && pEnt->pPlayer)
		{
			int eLang = entGetLanguage(pEnt);
			const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
			ContactHeadshotData *pHeadshotData = NULL;

			if (!pchTranslation)
			{
				continue;
			}

			pHeadshotData = StructCreate(parse_ContactHeadshotData);
			contact_CostumeToHeadshotData(pEnt, pContactCostume, &pHeadshotData);
			pHeadshotData->pchHeadshotStyleDef = allocAddString(pchHeadshotStyle);

			notify_NotifySendWithHeadshot(pEnt, kNotifyType_MiniContact, pchTranslation, NULL, pchSound, pHeadshotData);

			StructDestroy(parse_ContactHeadshotData, pHeadshotData);
		}
	}

	StructDestroy(parse_ContactCostume, pContactCostume);
#endif
}

// Send a mini contact notification
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMiniContactPlayer);
void SendMiniContactPlayer(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchSound,
						   const char* pchHeadshotStyle)
{
#if defined(GAMESERVER)
	int i;
	ContactCostume *pContactCostume = StructCreate(parse_ContactCostume);

	pContactCostume->eCostumeType = ContactCostumeType_Player;

	for (i = 0; i < eaSize(entsIn); i++)
	{
		Entity *pEnt = (*entsIn)[i];
		if (pEnt && pEnt->pPlayer)
		{
			int eLang = entGetLanguage(pEnt);
			const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
			ContactHeadshotData *pHeadshotData = NULL;

			if (!pchTranslation)
			{
				continue;
			}

			pHeadshotData = StructCreate(parse_ContactHeadshotData);
			contact_CostumeToHeadshotData(pEnt, pContactCostume, &pHeadshotData);
			pHeadshotData->pchHeadshotStyleDef = allocAddString(pchHeadshotStyle);

			notify_NotifySendWithHeadshot(pEnt, kNotifyType_MiniContact, pchTranslation, NULL, pchSound, pHeadshotData);

			StructDestroy(parse_ContactHeadshotData, pHeadshotData);
		}
	}

	StructDestroy(parse_ContactCostume, pContactCostume);
#endif
}