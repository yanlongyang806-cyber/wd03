#include "cmdparse.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "textparser.h"

#include "GraphicsLib.h"
#include "GfxConsole.h"
#include "GfxSpriteText.h"

#include "soundLib.h"

#include "GameClientLib.h"
#include "GameStringFormat.h"
#include "NotifyCommon.h"
#include "NotifyCommon_h_ast.h"
#include "chat/gclChatLog.h"
#include "gclChatLog.h"
#include "gclDialogBox.h"
#include "gclEntity.h"
#include "gclHUDOptions.h"
#include "gclNotify.h"
#include "gclUIGen.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "dynFx.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "Player.h"
#include "rand.h"
#include "mission_common.h"
#include "contact_common.h"
#include "uiMission.h"
#include "StringUtil.h"
#include "UITextureAssembly.h"
#include "UIGen_h_ast.h"
#include "NotifyEnum_h_ast.h"
#include "gclNotify_h_ast.h"
#include "gclNotify_h_ast.c"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static NotifyActions s_Actions = {0};
static NotifyQueues s_Queues;
static NotifyAudioEvent** s_eaAudioEventUpdates = NULL;
static NotifyAudioEventList s_AudioEvents;
#define MAX_AUDIO_EVENT_HISTORY 10

// All delayed notifications are pushed into this array
// and flushed at once
static NotifyQueueItem **s_eaDelayedNotifications = NULL;

static NotifyActionFloatToGenItem **s_eaFloatToGens;

static bool s_bNotifyDebug = false;

static ExprContext *s_pNotifyContext = NULL;

static NotifySettingFlags s_NotifySettings[kNotifyType_COUNT];
static NotifySettingsGroupDef** s_eaNotifySettingsGroupDefs = NULL;
static NotifySettingsDef* s_pNotifySettings = NULL;

// Enable/disable notification debugging.
AUTO_CMD_INT(s_bNotifyDebug, NotifyDebug) ACMD_ACCESSLEVEL(9);

static NotifyActionFloatToGenItem *gclNotifyAction_FloatToCreate(NotifyActionFloatToGen *pFloatTo, const char *pchDisplayString, const char *pchTag, S32 iValue, const Vec3 vOrigin, EntityRef erEntity);
static void gclNotifyActionDo(	NotifyAction *pAction, NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture, const ChatData *pChatData, const ContactHeadshotData *pHeadshotData, const char * pchTag,S32 iValue, const Vec3 vOrigin,S64 itemID, EntityRef erEntity);
static void gclNotifyUpdateExpressionContext(NotifyAudioEvent* pNotify, Entity* pEnt, S32 iValue, const char *pchTag, const char*pchDisplayString);

// -----------------------------------------------------------------------------------------------------------------------------
AUTO_FIXUPFUNC;
TextParserResult gclNotifyActionParserFixup(NotifyAction *pAction, enumTextParserFixupType eType, void *pExtraData)
{
	S32 i;
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
	case FIXUPTYPE_POST_BIN_READ:
	case FIXUPTYPE_POST_RELOAD:
		for (i = 0; i < eaSize(&pAction->eaSound); i++)
			if (!sndEventExists(pAction->eaSound[i]))
				ErrorFilenamef(pAction->pchFilename, "NotifyAction %s: Invalid audio event: %s", pAction->pchFilename, pAction->eaSound[i]);
	}
	return PARSERESULT_SUCCESS;
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyKillItem(NotifyQueueItem * pItem)
{
	pItem->fLifetime = 0;
	pItem->bInfinite = false;
}

// -----------------------------------------------------------------------------------------------------------------------------
static NotifyAction* gclNotifyAction_FindAction(NotifyType eType, const char *pchLogicalStringFilter)
{
	if (pchLogicalStringFilter)
		pchLogicalStringFilter = allocFindString(pchLogicalStringFilter);
	
	if (eType >= kNotifyType_Default && eType < kNotifyType_COUNT)
	{
		NotifyAction  *pDefaultAction = NULL;
		NotifyAction **peaActionList = s_Actions.aeaActions[eType];

		FOR_EACH_IN_EARRAY(peaActionList, NotifyAction, pAction)
		{
			if (!pAction->ppchLogicalStringFilters && pAction->bLogicalStringIsTutorial == -1)
			{
				pDefaultAction = pAction;
				continue;
			}

			if (pAction->ppchLogicalStringFilters && pchLogicalStringFilter && 
				eaFind(&pAction->ppchLogicalStringFilters, pchLogicalStringFilter) == -1)
			{
				continue;
			}

			if (pAction->bLogicalStringIsTutorial != -1)
			{
				TutorialScreenRegionInfo *pRegionInfo = pchLogicalStringFilter ? eaIndexedGetUsingString(&g_TutorialScreenRegions.eaRegions, pchLogicalStringFilter) : NULL;
				UIGen *pTargetGen = pRegionInfo ? RefSystem_ReferentFromString("UIGen", pRegionInfo->pchUIGen) : NULL;
				if (pAction->bLogicalStringIsTutorial && !UI_GEN_READY(pTargetGen))
					continue;
				else if (!pAction->bLogicalStringIsTutorial && UI_GEN_READY(pTargetGen))
					continue;
			}

			return pAction;
		}
		FOR_EACH_END

		return pDefaultAction;
	}
	
	return NULL;
}

// -----------------------------------------------------------------------------------------------------------------------------
static S32 gclNotifyAction_ValidateNoDupes(const NotifyAction *pAction)
{
	NotifyAction **peaActionList = s_Actions.aeaActions[pAction->eType];

	// see if there is a dupe
	FOR_EACH_IN_EARRAY(peaActionList, NotifyAction, pOtherAction)
	{
		bool bSameTutorialFlag = false;
		if ((pOtherAction->bLogicalStringIsTutorial != -1
				&& pAction->bLogicalStringIsTutorial != -1
				&& (pOtherAction->bLogicalStringIsTutorial && pAction->bLogicalStringIsTutorial
					|| !pOtherAction->bLogicalStringIsTutorial && !pAction->bLogicalStringIsTutorial)))
		{
			bSameTutorialFlag = true;
		}

		FOR_EACH_IN_EARRAY(pAction->ppchLogicalStringFilters, const char, pchLogicalString)
		{
			if (eaFind(&pOtherAction->ppchLogicalStringFilters, pchLogicalString) != -1)
			{// found a dupe
				if (bSameTutorialFlag)
				{
					return false;
				}
			}
		}
		FOR_EACH_END

		if (!pAction->ppchLogicalStringFilters && !pOtherAction->ppchLogicalStringFilters)
		{
			if (bSameTutorialFlag)
			{
				return false;
			}
		}
	}
	FOR_EACH_END

	return true;
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyAction_FloatToValidate(NotifyAction *pAction, NotifyActionFloatToGen *pFloatTo)
{
	if (pFloatTo->pchIconName && (!pFloatTo->fIconHeight || !pFloatTo->fIconWidth))
	{
		ErrorFilenamef(s_Actions.pchFilename, "NotifyAction (%s) has sprite but IconWidth/IconHeight not defined", 
								StaticDefineIntRevLookup(NotifyTypeEnum, pAction->eType));
	}
	
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyAction_FloatToDestroy(void *p)
{
	NotifyActionFloatToGenItem *pFloatTo = (NotifyActionFloatToGenItem*)p;

	if (pFloatTo)
	{
		if (pFloatTo->hFX)
		{
			dtFxKill(pFloatTo->hFX);
		}

		StructDestroy(parse_NotifyActionFloatToGenItem, pFloatTo);
	}
}

static void gclNotifySettingsValidate(void)
{
	int i, j;
	for (i = eaSize(&s_pNotifySettings->eaCategoryDefs)-1; i >= 0; i--)
	{
		NotifySettingsCategoryDef* pCategory = s_pNotifySettings->eaCategoryDefs[i];

		if (!REF_STRING_FROM_HANDLE(pCategory->msgDisplayName.hMessage))
		{
			Errorf("Notify settings category %d does not have a display name!", i);
		}
		for (j = eaSize(&pCategory->eaGroupDefs)-1; j >= 0; j--)
		{
			NotifySettingsGroupDef* pGroup = pCategory->eaGroupDefs[j];

			if (!pGroup->pchName)
			{
				Errorf("Notify settings group %d does not have a name!", j);
			}
			else if (!REF_STRING_FROM_HANDLE(pGroup->msgDisplayName.hMessage))
			{
				Errorf("Notify settings group %s does not have a display name!", pGroup->pchName);
			}
			if (!eaiSize(&pGroup->peNotifyTypes))
			{
				Errorf("Notify settings group does not specify any notify types!");
			}
		}
	}
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifySettingsLoad(const char *pchPath, S32 iWhen)
{
	int i, j;

	if (s_pNotifySettings)
	{
		StructReset(parse_NotifySettingsDef, s_pNotifySettings);
	}
	else
	{
		s_pNotifySettings = StructCreate(parse_NotifySettingsDef);
	}

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	// Load Notification settings
	ParserLoadFiles(NULL, "defs/config/NotifySettings.def", "NotifySettings.bin", PARSER_OPTIONALFLAG, parse_NotifySettingsDef, s_pNotifySettings);

	// Add all GroupDefs to a single list for faster lookups
	eaClear(&s_eaNotifySettingsGroupDefs);
	eaIndexedEnable(&s_eaNotifySettingsGroupDefs, parse_NotifySettingsGroupDef);

	for (i = eaSize(&s_pNotifySettings->eaCategoryDefs)-1; i >= 0; i--)
	{
		NotifySettingsCategoryDef* pCategory = s_pNotifySettings->eaCategoryDefs[i];
		for (j = eaSize(&pCategory->eaGroupDefs)-1; j >= 0; j--)
		{
			eaPush(&s_eaNotifySettingsGroupDefs, pCategory->eaGroupDefs[j]);
		}
	}

	// If in dev mode, do validation on notification settings
	if (isDevelopmentMode())
	{
		gclNotifySettingsValidate();
	}
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyLoad(const char *pchPath, S32 iWhen)
{
	S32 i;
	S32 j;
	loadstart_printf("Loading notification actions...");
	StructDeInit(parse_NotifyActions, &s_Actions);
	StructDeInit(parse_NotifyQueues, &s_Queues);
	eaClearEx(&s_eaFloatToGens, gclNotifyAction_FloatToDestroy);
	eaIndexedEnable(&s_Actions.eaActions, parse_NotifyAction);
	eaIndexedEnable(&s_Queues.eaQueues, parse_NotifyQueue);
	

	for (i = 0; i < kNotifyType_COUNT; ++i)
	{
		eaClear(&s_Actions.aeaActions[i]);
	}

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	ParserLoadFiles(NULL, "ui/Notify.def", "Notify.bin", PARSER_OPTIONALFLAG, parse_NotifyActions, &s_Actions);

	// Create queues
	for (i = 0; i < eaSize(&s_Actions.eaActions); i++)
	{
		NotifyAction *pAction = s_Actions.eaActions[i];
		for (j = 0; j < eaSize(&pAction->eaQueue); j++)
		{
			NotifyActionEnqueue *pEnqueue = pAction->eaQueue[j];
			NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pEnqueue->pchQueueName);
			if (!pQueue)
			{
				pQueue = StructCreate(parse_NotifyQueue);
				pQueue->pchName = pEnqueue->pchQueueName;
				eaPush(&s_Queues.eaQueues, pQueue);
			}
		}

		for (j = 0; j < eaSize(&pAction->eaTutorialGen); j++)
		{
			NotifyTutorialGen *pTutorial = pAction->eaTutorialGen[j];
			if (pTutorial->ePopupDirection != UINoDirection)
			{
				if ((pTutorial->ePopupDirection & UIHorizontal) && !IS_HANDLE_ACTIVE(pTutorial->hHorizontalPopupTemplate))
					ErrorFilenamef(pAction->pchFilename, "NotifyAction (%s) TutorialGen includes Horizontal popup direction without specifying a HorizontalPopupTemplate", StaticDefineIntRevLookup(NotifyTypeEnum, pAction->eType));
				if ((pTutorial->ePopupDirection & UIVertical) && !IS_HANDLE_ACTIVE(pTutorial->hVerticalPopupTemplate))
					ErrorFilenamef(pAction->pchFilename, "NotifyAction (%s) TutorialGen includes Vertical popup direction without specifying a VerticalPopupTemplate", StaticDefineIntRevLookup(NotifyTypeEnum, pAction->eType));
				if (!IS_HANDLE_ACTIVE(pTutorial->hHorizontalPopupTemplate) && !IS_HANDLE_ACTIVE(pTutorial->hVerticalPopupTemplate))
					ErrorFilenamef(pAction->pchFilename, "NotifyAction (%s) TutorialGen does not specify HorizontalPopupTemplate or VerticalPopupTemplate", StaticDefineIntRevLookup(NotifyTypeEnum, pAction->eType));
			}
		}

		if (pAction->pFloatTo)
		{
			gclNotifyUpdateExpressionContext(NULL, NULL, 0, NULL, NULL);

			if (pAction->pFloatTo->pExprDisplayStringOverride)
			{
				exprGenerate(pAction->pFloatTo->pExprDisplayStringOverride, s_pNotifyContext);
			}
			if (pAction->pFloatTo->pExprShowFloatTo)
			{
				exprGenerate(pAction->pFloatTo->pExprShowFloatTo, s_pNotifyContext);
			}
			if (pAction->pFloatTo->pExprOnExpire)
			{
				exprGenerate(pAction->pFloatTo->pExprOnExpire, s_pNotifyContext);
			}

			gclNotifyAction_FloatToValidate(pAction, pAction->pFloatTo);
		}

		if( pAction->pChat )
		{
			gclNotifyUpdateExpressionContext(NULL, NULL, 0, NULL, NULL);

			if (pAction->pChat->pExprDisplayStringOverride)
			{
				exprGenerate(pAction->pChat->pExprDisplayStringOverride, s_pNotifyContext);
			}
		}

		// add the action to our lookup table 
		if (pAction->eType >= kNotifyType_Default && pAction->eType < kNotifyType_COUNT)
		{
			bool bDupe = false;
			if (!gclNotifyAction_ValidateNoDupes(pAction))
			{	
				ErrorFilenamef(s_Actions.pchFilename, "Duplicate NotifyAction found for type %s", 
					StaticDefineIntRevLookup(NotifyTypeEnum, pAction->eType));
			}
			else
			{
				NotifyAction ***peaActionList = &s_Actions.aeaActions[pAction->eType];
				eaPush(peaActionList, pAction);
			}
		}
	}

	loadend_printf(" Done. (%d actions)", eaSize(&s_Actions.eaActions));
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyInitExpressionContext(void)
{
	if ( s_pNotifyContext==NULL )
	{
		ExprFuncTable* stFuncTable = NULL;
		s_pNotifyContext = exprContextCreate();

		stFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stFuncTable, "uigen");
		exprContextAddFuncsToTableByTag(stFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(stFuncTable, "player");
		exprContextAddFuncsToTableByTag(stFuncTable, "util");
		exprContextSetFuncTable(s_pNotifyContext, stFuncTable);

		exprContextSetAllowRuntimePartition(s_pNotifyContext);
		exprContextSetAllowRuntimeSelfPtr(s_pNotifyContext);
	}
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyUpdateExpressionContext(NotifyAudioEvent* pNotify, Entity* pEnt, S32 iValue, const char* pchTag, const char* pchDisplayString)
{
	static int s_hSelfVar = 0;
	static int s_hPlayerVar = 0;
	static int s_hValueVar = 0;
	static int s_hTagVar = 0;
	static int s_hDisplayStringVar = 0;
	static const char* s_pchSelfVar = NULL;
	static const char* s_pchPlayerVar = NULL;
	static const char* s_pchValueVar = NULL;
	static const char* s_pchTagVar = NULL;
	static const char* s_pchDisplayStringVar = NULL;

	if (!s_pchSelfVar)
		s_pchSelfVar = allocAddString("Self");
	if (!s_pchPlayerVar)
		s_pchPlayerVar = allocAddString("Player");
	if (!s_pchValueVar)
		s_pchValueVar = allocAddString("Value");
	if (!s_pchTagVar)
		s_pchTagVar = allocAddString("Tag");
	if (!s_pchDisplayStringVar)
		s_pchDisplayStringVar = allocAddString("DisplayString");

	exprContextSetPointerVarPooledCached(s_pNotifyContext, s_pchPlayerVar, pEnt, parse_Entity, true, true, &s_hPlayerVar);	
	exprContextSetPointerVarPooledCached(s_pNotifyContext, s_pchSelfVar, pNotify, parse_NotifyAudioEvent, true, true, &s_hSelfVar);
	exprContextSetStringVarPooledCached(s_pNotifyContext, s_pchTagVar, NULL_TO_EMPTY(pchTag), &s_hTagVar);
	exprContextSetIntVarPooledCached(s_pNotifyContext, s_pchValueVar, iValue, &s_hValueVar);
	exprContextSetStringVarPooledCached(s_pNotifyContext, s_pchDisplayStringVar, NULL_TO_EMPTY(pchDisplayString), &s_hDisplayStringVar);
}



// -----------------------------------------------------------------------------------------------------------------------------
static S32 gclNotifyAudioEvents_Generate(void)
{
	S32 i, j, iCount = 0;
	for (i = eaSize(&s_AudioEvents.eaGroups)-1; i >= 0; i--)
	{
		NotifyAudioEventGroup* pGroup = s_AudioEvents.eaGroups[i];

		if (pGroup->pRequiresExpr)
		{
			exprGenerate(pGroup->pRequiresExpr, s_pNotifyContext);
		}
		for (j = eaSize(&pGroup->eaList)-1; j >= 0; j--)
		{
			NotifyAudioEvent* pNotify = pGroup->eaList[j];
			if (pNotify->pUpdateExpr)
			{
				exprGenerate(pNotify->pUpdateExpr, s_pNotifyContext);
			}
			if (pNotify->pActivateExpr)
			{
				exprGenerate(pNotify->pActivateExpr, s_pNotifyContext);
			}
			if (pNotify->pResetExpr)
			{
				exprGenerate(pNotify->pResetExpr, s_pNotifyContext);
			}
			iCount++;
		}
	}
	return iCount;
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyAudioEvents_Load(const char *pchPath, S32 iWhen)
{
	S32 iSize;
	loadstart_printf("Loading notification audio events...");

	StructDeInit(parse_NotifyAudioEventList, &s_AudioEvents);

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	ParserLoadFiles(NULL, "ui/NotifyAudioEvents.def", "NotifyAudioEvents.bin", PARSER_OPTIONALFLAG, parse_NotifyAudioEventList, &s_AudioEvents);

	gclNotifyUpdateExpressionContext(NULL, NULL, 0, NULL, NULL);
	iSize = gclNotifyAudioEvents_Generate();

	if (isDevelopmentMode())
	{
		S32 i, j;
		for (i = eaSize(&s_AudioEvents.eaGroups)-1; i >= 0; i--)
		{
			NotifyAudioEventGroup* pGroup = s_AudioEvents.eaGroups[i];
			for (j = eaSize(&pGroup->eaList)-1; j >= 0; j--)
			{
				NotifyAudioEvent* pNotify = pGroup->eaList[j];
				if (!GET_REF(pNotify->DisplayMsg.hMessage) && REF_STRING_FROM_HANDLE(pNotify->DisplayMsg.hMessage))
				{
					ErrorFilenamef(s_AudioEvents.pchFilename, "NotifyAudioEvent refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pNotify->DisplayMsg.hMessage));
				}
			}
		}
	}
	loadend_printf(" Done. (%d events)", iSize);
}

// -----------------------------------------------------------------------------------------------------------------------------
AUTO_RUN;
void gclNotifyInit(void)
{
	ui_GenInitStaticDefineVars(NotifySettingFlagsEnum, "NotifySettingFlag_");
}

AUTO_STARTUP(Notify) ASTRT_DEPS(UIGen);
void gclNotifyStartup(void)
{
	if(!gbNoGraphics)
	{
		gclNotifyInitExpressionContext();
		gclNotifyLoad(NULL, 0);
		gclNotifySettingsLoad(NULL, 0);
		gclNotifyAudioEvents_Load(NULL, 0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/Notify.def", gclNotifyLoad);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/NotifySettings.def", gclNotifySettingsLoad);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/NotifyAudioEvents.def", gclNotifyAudioEvents_Load);
	}
}

// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyClientController(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound)
{
	SendCommandStringToTestClientf("PushNotify \"%s\" \"%s\" \"%s\" %s", StaticDefineIntRevLookup(NotifyTypeEnum, eType), pchLogicalString, pchSound, pchDisplayString);
}



// -----------------------------------------------------------------------------------------------------------------------------
static void gclNotifyReceiveInternal(	NotifyType eType, 
										const char *pchDisplayString, 
										const char *pchLogicalString, 
										const char *pchSound, 
										const char *pchTexture, 
										const ChatData *pChatData, 
										const ContactHeadshotData *pHeadshotData, 
										const char * pchTag,
										S32 iValue,
										const Vec3 vOrigin,
										S64 itemID,
										EntityRef erEntity)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	ContactDialog *pContactDialog = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, pContactDialog);

	if (gConf.bDelayNotificationsDuringContactDialog && 
		pPlayerEnt && pContactDialog &&
		pChatData == NULL && pHeadshotData == NULL)
	{
		// Add this notification to the delayed notifications array
		NotifyQueueItem *pItem = StructCreate(parse_NotifyQueueItem);

		pItem->eType = eType;
		pItem->pchDisplayString = StructAllocString(pchDisplayString);
		pItem->pchLogicalString = allocAddString(pchLogicalString);
		pItem->pchSound = allocAddString(pchSound);
		pItem->pchTexture = allocAddString(pchTexture);
		pItem->pchTag = allocAddString(pchTag);
		pItem->iValue = iValue;
		copyVec3(vOrigin, pItem->vOrigin);
		pItem->itemID = itemID;
		pItem->erEntity = erEntity;

		eaPush(&s_eaDelayedNotifications, pItem);
	}
	else
	{
		NotifyAction *pAction = gclNotifyAction_FindAction(eType, pchLogicalString);
			
		if(gclGetLinkToTestClient())
		{
			gclNotifyClientController(eType, pchDisplayString, pchLogicalString, pchSound);
		}

		if (!pAction)
		{
			pAction = gclNotifyAction_FindAction(kNotifyType_Default, NULL);
		}

		if (pAction)
		{
			gclNotifyActionDo(	pAction, 
								eType, 
								pchDisplayString, 
								pchLogicalString, 
								pchSound, 
								pchTexture, 
								pChatData, 
								pHeadshotData, 
								pchTag,
								iValue, 
								vOrigin, 
								itemID,
								erEntity);
			if (s_bNotifyDebug)
			{
				conPrintf("Notify Received: %s (processed as %s). Display: %s; Logical: %s; Audio: %s; Texture: %s.",
					StaticDefineIntRevLookup(NotifyTypeEnum, eType),
					StaticDefineIntRevLookup(NotifyTypeEnum, pAction->eType),
					pchDisplayString, pchLogicalString, pchSound, pchTexture);
			}
		}
		else if (s_bNotifyDebug)
		{
			conPrintf("Notify Received: %s (Ignored). Display: %s; Logical: %s; Audio: %s; Texture: %s.",
				StaticDefineIntRevLookup(NotifyTypeEnum, eType),
				pchDisplayString, pchLogicalString, pchSound, pchTexture);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifyChatSend) ACMD_GENERICCLIENTCMD ACMD_HIDE ACMD_PRIVATE;
void gclNotifyChatRelayReceive(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, 
	const char *pchTexture, const ChatData *pChatData, const char * pchTag)
{
	gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, pchSound, pchTexture, pChatData, NULL, pchTag, 0, NULL, 0, 0);
}

// Process a notification with chat data
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendWithData) ACMD_CLIENTCMD ACMD_PRIVATE;
void gclNotifyReceiveWithData(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture, const ChatData *pChatData)
{
	gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, pchSound, pchTexture, pChatData, NULL, NULL, 0, NULL, 0, 0);
}

// Process a notification with headshot data
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendWithHeadshot) ACMD_CLIENTCMD ACMD_PRIVATE;
void gclNotifyReceiveWithHeadshot(	NotifyType eType, 
									const char *pchDisplayString, 
									const char *pchLogicalString, 
									const char *pchSound, 
									const ContactHeadshotData *pHeadshotData)
{
	gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, pchSound, NULL, NULL, pHeadshotData, NULL, 0, NULL, 0, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySend) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyReceive(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture)
{
	gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, NULL, pchTexture, NULL, NULL, NULL, 0, NULL, 0, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendStruct) ACMD_CLIENTCMD ACMD_HIDE ACMD_PRIVATE;
void gclNotifyReceiveMessageStruct(NotifyType eType, MessageStruct *pFmt)
{
	Language eLang = langGetCurrent();
	if (pFmt->eLangSendRestriction == LANGUAGE_DEFAULT || 
		pFmt->eLangSendRestriction == eLang ||
		(eLang == LANGUAGE_DEFAULT && pFmt->eLangSendRestriction == LANGUAGE_ENGLISH))
	{
		char *pMessage = NULL;
		estrStackCreate(&pMessage);
		langFormatMessageStructDefault(eLang, &pMessage, pFmt, pFmt->pchKey);
		gclNotifyReceiveInternal(eType, pMessage, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL, 0, 0);
		estrDestroy(&pMessage);
	}
}

// Process a notification with a tag
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendWithTag) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyReceiveWithTag(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture, const char *pchTag)
{
	gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, NULL, pchTexture, NULL, NULL, pchTag, 0, NULL, 0, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendSplatFX) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyReceiveSplatFX(NotifyType eType, const char *pchDisplayString, const char *pchSplatFX)
{
	//Don't worry about all the notify, just play the FX
	playFXSplat(pchSplatFX,pchDisplayString);
}

// Process a notification.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendAudio) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyReceiveAudio(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture)
{
	gclNotifyReceiveWithData(eType, pchDisplayString, pchLogicalString, pchSound, pchTexture, NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendWithOrigin) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyReceiveWithOrigin(NotifyType eType, 
								const char *pchDisplayString, 
								const char *pchLogicalString, 
								const char *pchTag, 
								S32 iValue,
								const Vec3 vOrigin)
{
	gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, NULL, NULL, NULL, NULL, pchTag, iValue, vOrigin, 0, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendWithItemID) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyReceiveWithItemID(NotifyType eType, 
								const char *pchDisplayString, 
								const char *pchLogicalString, 
								const char *pchTexture,
								U64 itemID,
								S32 iCount)
{
	gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, NULL, pchTexture, NULL, NULL, NULL, iCount, NULL, itemID, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendWithEntityRef) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyReceiveWithEntityRef(	NotifyType eType, 
									const char *pcDisplayString, 
									const char *pchLogicalString, 
									S32 iValue,
									EntityRef erEntity)
{
	gclNotifyReceiveInternal(eType, pcDisplayString, pchLogicalString, NULL, 0, NULL, NULL, NULL, iValue, NULL, 0, erEntity);

}

// Process a notification (specifying type by name).
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendName) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifySendName(ACMD_NAMELIST(NotifyTypeEnum, STATICDEFINE) const char *pchType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture)
{
	gclNotifyReceive(StaticDefineIntGetInt(NotifyTypeEnum, pchType), pchDisplayString, pchLogicalString, pchTexture);
}

// Process a notification (specifying type by name).
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifySendAudioName) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifySendAudioName(ACMD_NAMELIST(NotifyTypeEnum, STATICDEFINE) const char *pchType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture)
{
	gclNotifyReceiveAudio(StaticDefineIntGetInt(NotifyTypeEnum, pchType), pchDisplayString, pchLogicalString, pchSound, pchTexture);
}

// -----------------------------------------------------------------------------------------------------------------------------
bool gclNotifyIsHandled(NotifyType eType, SA_PARAM_OP_STR const char *pchLogicalString)
{
	return gclNotifyAction_FindAction(eType, pchLogicalString) != NULL;
}

// -----------------------------------------------------------------------------------------------------------------------------
bool gclNotifySendIfHandled(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture)
{
	NotifyAction *pAction = gclNotifyAction_FindAction(eType, pchLogicalString);
	if (pAction)
	{
		gclNotifyReceiveInternal(eType, pchDisplayString, pchLogicalString, NULL, pchTexture, NULL, NULL, NULL, 0, NULL, 0, 0);
		return true;
	}
	return false;
}

static void gclNotifySettingsFillFromArray(NotifySetting** eaSettings)
{
	int i, j;

	// reset to defaults
	for (i = eaSize(&s_eaNotifySettingsGroupDefs)-1; i >= 0; i--)
	{
		NotifySettingsGroupDef* pGroup = s_eaNotifySettingsGroupDefs[i];

		if (pGroup && pGroup->eFlags)
		{
			for (j = eaiSize(&pGroup->peNotifyTypes) - 1; i >= 0; i--)
			{
				NotifyType eType = pGroup->peNotifyTypes[j];

				if (eType >= kNotifyType_Default && eType < kNotifyType_COUNT)
				{
					s_NotifySettings[eType] = pGroup->eFlags;
				}
			}
		}
	}

	for (i = eaSize(&eaSettings)-1; i >= 0; i--)
	{
		NotifySetting* pSetting = eaSettings[i];

		NotifySettingsGroupDef* pGroup = eaIndexedGetUsingString(&s_eaNotifySettingsGroupDefs, pSetting->pchNotifyGroupName);

		if (pGroup)
		{
			for (j = eaiSize(&pGroup->peNotifyTypes)-1; j >= 0; j--)
			{
				NotifyType eType = pGroup->peNotifyTypes[j];

				if (eType >= kNotifyType_Default && eType < kNotifyType_COUNT)
				{
					s_NotifySettings[eType] = pSetting->eFlags;
				}
			}
		}
	}
}

void gclNotifySettingsFillFromEntity(Entity* pEnt)
{
	memset(s_NotifySettings, 0, kNotifyType_COUNT);

	if (s_pNotifySettings)
	{
		if (pEnt)
		{
			PlayerUI* pUI = SAFE_MEMBER(pEnt->pPlayer, pUI);
			if (pUI)
			{
				gclNotifySettingsFillFromArray(pUI->eaNotifySettings);
			}
		}
	}
}

bool gclNotify_CheckSettingFlags(NotifyType eType, NotifySettingFlags eFlags)
{
	if (eType >= kNotifyType_Default && eType < kNotifyType_COUNT)
	{
		if (s_NotifySettings[eType] & eFlags)
		{
			return true;
		}
	}
	return false;
}

void gclNotify_HandleUpdate(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		static int s_iLastVersion = 0;
		if (pEnt->pPlayer->pUI->iNotifySettingVersion != s_iLastVersion)
		{
			gclNotifySettingsFillFromEntity(pEnt);

			g_ChatLogVersion++;

			s_iLastVersion = pEnt->pPlayer->pUI->iNotifySettingVersion;
		}
	}
}


// -----------------------------------------------------------------------------------------------------------------------

static bool gclNotifyAction_ChatEvaluateStringExpression(Expression* pExpr, 
	const char *pchTag, 
	S32 iValue, 
	const char *pchDisplayString,
	SA_PARAM_NN_VALID char **pestrOutput)
{
	if (pExpr)
	{
		MultiVal mVal;
		Entity *pPlayer = entActivePlayerPtr();

		exprContextSetSelfPtr(s_pNotifyContext, pPlayer);
		exprContextSetPartition(s_pNotifyContext, entGetPartitionIdx(pPlayer));

		gclNotifyUpdateExpressionContext(NULL, pPlayer, iValue, pchTag, pchDisplayString);

		exprEvaluate(pExpr, s_pNotifyContext, &mVal);

		if(MultiValIsString(&mVal))
		{
			if (mVal.str)
				estrCopy2(pestrOutput, mVal.str);
			return true;
		}
	}
	return true;
}



static void gclNotify_GroupHasChatOrQueueAction(NotifySettingsGroupDef* pGroup, S32* pbHasChat, S32* pbHasQueue)
{
	int i;
	for (i = eaiSize(&pGroup->peNotifyTypes)-1; i >= 0; i--)
	{
		NotifyAction *pAction = gclNotifyAction_FindAction(pGroup->peNotifyTypes[i], NULL);

		if (!pAction)
		{
			pAction = gclNotifyAction_FindAction(kNotifyType_Default, NULL);
		}
		if (pAction)
		{
			if (pAction->pChat)
			{
				(*pbHasChat) = true;
			}
			if (eaSize(&pAction->eaQueue))
			{
				(*pbHasQueue) = true;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyGetSettingsList);
void gclExprNotifyGetSettingsList(SA_PARAM_NN_VALID UIGen* pGen)
{
	NotifySettingUI*** peaData = ui_GenGetManagedListSafe(pGen, NotifySettingUI);
	int i, j, iCount = 0;

	if (s_pNotifySettings)
	{
		Entity* pEnt = entActivePlayerPtr();
		PlayerUI* pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);
		for (i = 0; i < eaSize(&s_pNotifySettings->eaCategoryDefs); i++)
		{
			NotifySettingsCategoryDef* pCategory = s_pNotifySettings->eaCategoryDefs[i];
			NotifySettingUI* pCategoryHeader = eaGetStruct(peaData, parse_NotifySettingUI, iCount++);
			pCategoryHeader->pchGroupName = NULL;
			pCategoryHeader->pchDisplayName = TranslateDisplayMessage(pCategory->msgDisplayName);
			pCategoryHeader->pchDescription = NULL;
			pCategoryHeader->eFlags = kNotifySettingFlags_None;
			pCategoryHeader->bIsHeader = true;
			pCategoryHeader->bHasChat = false;
			pCategoryHeader->bHasQueue = false;

			for (j = 0; j < eaSize(&pCategory->eaGroupDefs); j++)
			{
				NotifySettingsGroupDef* pGroupDef = pCategory->eaGroupDefs[j];
				S32 bHasChat = false;
				S32 bHasQueue = false;

				gclNotify_GroupHasChatOrQueueAction(pGroupDef, &bHasChat, &bHasQueue);
				if (bHasChat || bHasQueue)
				{
					NotifySettingUI* pGroupUI = eaGetStruct(peaData, parse_NotifySettingUI, iCount++);
					NotifySetting* pSetting = NULL;

					if (pUI)
					{
						pSetting = eaIndexedGetUsingString(&pUI->eaNotifySettings, pGroupDef->pchName);
					}
					pGroupUI->pchGroupName = pGroupDef->pchName;
					pGroupUI->pchDisplayName = TranslateDisplayMessage(pGroupDef->msgDisplayName);
					pGroupUI->pchDescription = TranslateDisplayMessage(pGroupDef->msgDescription);
					pGroupUI->eFlags = pSetting ? pSetting->eFlags : pGroupDef->eFlags;
					pGroupUI->bIsHeader = false;
					pGroupUI->bHasChat = !!bHasChat;
					pGroupUI->bHasQueue = !!bHasQueue;
				}
			}
		}
	}

	eaSetSizeStruct(peaData, parse_NotifySettingUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, NotifySettingUI, true);
}

// -----------------------------------------------------------------------------------------------------------------------------
static NotifyQueueItem *gclNotifyEnqueue(	NotifyActionEnqueue *pEnqueue, 
											NotifyType eType, 
											const char *pchDisplayString, 
											const char *pchLogicalString, 
											const char *pchSound, 
											const char *pchTexture, 
											const ContactHeadshotData *pHeadshotData, 
											const char * pchTag,
											S32 iValue, 
											S64 itemID)
{
	NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pEnqueue->pchQueueName);
	NotifyQueueItem *pItem = NULL;
	assertmsgf(pQueue, "Queue %s not found, it should've been created at load time.", pEnqueue->pchQueueName);

	if (pEnqueue->iBatchLimit > 1)
	{
		S32 i;
		for (i = 0; i < eaSize(&pQueue->eaItems); i++)
		{
			if (pQueue->eaItems[i]->eType == eType
				&& !stricmp_safe(pQueue->eaItems[i]->pchLogicalString, pchLogicalString)
				&& pQueue->eaItems[i]->iCount < pEnqueue->iBatchLimit)
			{
				// Reuse existing item
				pItem = eaRemove(&pQueue->eaItems, i);

				// Sum up the values
				iValue += pItem->iValue;
				break;
			}
		}
	}

	if (!pItem)
		pItem = StructCreate(parse_NotifyQueueItem);

	pItem->eType = eType;
	pItem->fLifetime = pEnqueue->fLifetime;
	pItem->fLifetimeMax = pEnqueue->fLifetime;
	pItem->fDelay = pEnqueue->fDelay;
	pItem->bInfinite = pEnqueue->bInfinite;
	COPY_HANDLE(pItem->hFont, pEnqueue->hFont);
	if (pItem->pchDisplayString)
		StructFreeString((char *)pItem->pchDisplayString);
	pItem->pchDisplayString = StructAllocString(pchDisplayString);
	pItem->pchLogicalString = allocAddString(pchLogicalString);
	//pItem->pchSound = allocAddString(pchLogicalString); //Already played
	pItem->pchTexture = allocAddString(pchTexture);
	if (pItem->pHeadshotData)
		StructDestroy(parse_ContactHeadshotData, pItem->pHeadshotData);
	pItem->pHeadshotData = StructClone(parse_ContactHeadshotData, pHeadshotData);
	pItem->uiColor = pEnqueue->uiColor;
	pItem->pchTag = allocAddString(pchTag);
	pItem->pchQueue = pEnqueue->pchQueueName;
	pItem->iValue = iValue;
	pItem->itemID = itemID;
	++pItem->iCount;

	sprintf(pItem->achColorString, "#%08x", pItem->uiColor ? pItem->uiColor : 0xFFFFFFFF);
	eaPush(&pQueue->eaItems, pItem);
	return pItem;
}

static void gclNotifyTutorial(NotifyTutorialGen *pTutorial,
									NotifyType eType,
									const char *pchLogicalString,
									TutorialScreenRegionInfo *pTutorialInfo)
{
	UIGen *pTutorialGen = pTutorialInfo ? ui_GenFind(pTutorialInfo->pchUIGen, kUIGenTypeNone) : NULL;
	UIGen *pTemplate = NULL;
	if (!pTutorialGen)
		pTutorialGen = GET_REF(pTutorial->hGen);
	if (!pTutorialGen || !GET_REF(pTutorial->hHorizontalPopupTemplate) || !GET_REF(pTutorial->hVerticalPopupTemplate))
		return;

	if (pTutorialInfo && pTutorialInfo->bHorizontalAlignment != pTutorialInfo->bVerticalAlignment)
	{
		// Use preference from tutorial info
		if (pTutorialInfo->bHorizontalAlignment)
			pTemplate = GET_REF(pTutorial->hHorizontalPopupTemplate);
		else if (pTutorialInfo->bVerticalAlignment)
			pTemplate = GET_REF(pTutorial->hVerticalPopupTemplate);
	}

	if (!pTemplate && (pTutorial->ePopupDirection & UIAnyDirection) != 0)
	{
		// Use preference on action
		if ((pTutorial->ePopupDirection & UIHorizontal) && !(pTutorial->ePopupDirection & UIVertical))
			pTemplate = GET_REF(pTutorial->hHorizontalPopupTemplate);
		else if (!(pTutorial->ePopupDirection & UIHorizontal) && (pTutorial->ePopupDirection & UIVertical))
			pTemplate = GET_REF(pTutorial->hVerticalPopupTemplate);
	}

	if (!pTemplate && UI_GEN_READY(pTutorialGen)
		&& GET_REF(pTutorial->hHorizontalPopupTemplate)
		&& GET_REF(pTutorial->hVerticalPopupTemplate))
	{
		// Determine best location based on where the UIGen is on screen
		UIDirection eEdges = UINoDirection;
		UIDirection eSides = UINoDirection;

		// Setup flags
		if (ui_GenInState(pTutorialGen, kUIGenStateLeftEdge))
			eEdges |= UILeft;
		if (ui_GenInState(pTutorialGen, kUIGenStateRightEdge))
			eEdges |= UIRight;
		if (ui_GenInState(pTutorialGen, kUIGenStateTopEdge))
			eEdges |= UITop;
		if (ui_GenInState(pTutorialGen, kUIGenStateBottomEdge))
			eEdges |= UIBottom;
		if (ui_GenInState(pTutorialGen, kUIGenStateLeftSide))
			eSides |= UILeft;
		if (ui_GenInState(pTutorialGen, kUIGenStateRightSide))
			eSides |= UIRight;
		if (ui_GenInState(pTutorialGen, kUIGenStateTopSide))
			eSides |= UITop;
		if (ui_GenInState(pTutorialGen, kUIGenStateBottomSide))
			eSides |= UIBottom;

		if (eEdges != UIAnyDirection && eEdges != UINoDirection)
		{
			// There's at least one edge touching the side of the
			// screen. Prefer floating away from the edge of
			// the screen.
			if ((eEdges & UIHorizontal) != UIHorizontal && ((eEdges & UIVertical) == 0 || (eEdges & UIVertical) == UIVertical))
				pTemplate = GET_REF(pTutorial->hHorizontalPopupTemplate);
			else if (((eEdges & UIHorizontal) == 0 || (eEdges & UIHorizontal) == UIHorizontal) && (eEdges & UIVertical) != UIVertical)
				pTemplate = GET_REF(pTutorial->hVerticalPopupTemplate);
			else if ((eEdges & UIHorizontal) != UIHorizontal)
				pTemplate = GET_REF(pTutorial->hHorizontalPopupTemplate);
			else if ((eEdges & UIVertical) != UIVertical)
				pTemplate = GET_REF(pTutorial->hVerticalPopupTemplate);
		}

		if (!pTemplate && eSides != UIAnyDirection && eSides != UINoDirection)
		{
			// The gen is floating somewhere on screen, but is near
			// a side of the screen. Prefer floating towards the center
			// of the screen.
			if ((eSides & UIHorizontal) != UIHorizontal && ((eSides & UIVertical) == 0 || (eSides & UIVertical) == UIVertical))
				pTemplate = GET_REF(pTutorial->hHorizontalPopupTemplate);
			else if (((eSides & UIHorizontal) == 0 || (eSides & UIHorizontal) == UIHorizontal) && (eSides & UIVertical) != UIVertical)
				pTemplate = GET_REF(pTutorial->hVerticalPopupTemplate);
			else if ((eSides & UIHorizontal) != UIHorizontal)
				pTemplate = GET_REF(pTutorial->hHorizontalPopupTemplate);
			else if ((eSides & UIVertical) != UIVertical)
				pTemplate = GET_REF(pTutorial->hVerticalPopupTemplate);
		}
	}

	// Pick a template, any template
	if (!pTemplate)
		pTemplate = GET_REF(pTutorial->hHorizontalPopupTemplate);
	if (!pTemplate)
		pTemplate = GET_REF(pTutorial->hVerticalPopupTemplate);

	// Remove existing popup templates
	if (GET_REF(pTutorial->hHorizontalPopupTemplate) != pTemplate)
		ui_GenRemoveEarlyOverrideChildTemplate(pTutorialGen, GET_REF(pTutorial->hHorizontalPopupTemplate));
	if (GET_REF(pTutorial->hVerticalPopupTemplate) != pTemplate)
		ui_GenRemoveEarlyOverrideChildTemplate(pTutorialGen, GET_REF(pTutorial->hVerticalPopupTemplate));

	// Add new popup template
	ui_GenAddEarlyOverrideChildTemplate(pTutorialGen, pTemplate);
}

// -----------------------------------------------------------------------------------------------------------------------------
// TODO(RP): these parameters should become a struct
static void gclNotifyActionDo(	NotifyAction *pAction, 
								NotifyType eType, 
								const char *pchDisplayString, 
								const char *pchLogicalString, 
								const char *pchSound, 
								const char *pchTexture, 
								const ChatData *pChatData, 
								const ContactHeadshotData *pHeadshotData, 
								const char * pchTag,
								S32 iValue, 
								const Vec3 vOrigin,
								S64 itemID,
								EntityRef erEntity)
{
	char *pch = NULL;
	S32 i;
	S32 j;
	if (!pAction)
		return;
	estrStackCreate(&pch);

	if (pchDisplayString && !pchDisplayString[0])
		pchDisplayString = NULL;

	if (pchDisplayString)
	{
		for (i = 0; i < eaSize(&pAction->eaCommands); i++)
		{
			estrClear(&pch);
			FormatGameString(&pch, pAction->eaCommands[i],
				STRFMT_STRING("DisplayString", pchDisplayString),
				STRFMT_STRING("LogicalString", pchLogicalString),
				STRFMT_STRING("Sound", pchSound),
				STRFMT_STRING("Texture", pchTexture),
				STRFMT_STRUCT("Action", pAction, parse_NotifyAction),
				STRFMT_END);
			globCmdParse(pch);
		}
	}

	for (i = 0; i < eaiSize(&pAction->eaiGenEnterGlobalState); i++)
		ui_GenSetGlobalState(pAction->eaiGenEnterGlobalState[i], true);
	for (i = 0; i < eaiSize(&pAction->eaiGenExitGlobalState); i++)
		ui_GenSetGlobalState(pAction->eaiGenExitGlobalState[i], false);

	for (i = 0; i < eaSize(&pAction->eaGenEnterState); i++)
	{
		UIGen *pGen = GET_REF(pAction->eaGenEnterState[i]->hGen);
		if (pGen)
			for (j = 0; j < eaiSize(&pAction->eaGenEnterState[i]->eaiStates); j++)
				ui_GenState(pGen, pAction->eaGenEnterState[i]->eaiStates[j], true);
	}

	for (i = 0; i < eaSize(&pAction->eaGenExitState); i++)
	{
		UIGen *pGen = GET_REF(pAction->eaGenExitState[i]->hGen);
		if (pGen)
			for (j = 0; j < eaiSize(&pAction->eaGenExitState[i]->eaiStates); j++)
				ui_GenState(pGen, pAction->eaGenExitState[i]->eaiStates[j], false);
	}

	for (i = 0; i < eaSize(&pAction->eaGenMessage); i++)
	{
		UIGen *pGen = GET_REF(pAction->eaGenMessage[i]->hGen);
		if (pGen)
			ui_GenSendMessage(pGen, pAction->eaGenMessage[i]->pchMessage);
	}

	if (pchLogicalString && !gclNotify_CheckSettingFlags(eType, kNotifySettingFlags_DisableTutorial))
	{
		TutorialScreenRegionInfo *pTutorialInfo = eaIndexedGetUsingString(&g_TutorialScreenRegions.eaRegions, pchLogicalString);
		for (i = 0; i < eaSize(&pAction->eaTutorialGen); i++)
		{
			gclNotifyTutorial(pAction->eaTutorialGen[i],
							  eType,
							  pchLogicalString,
							  pTutorialInfo);
		}
	}

	if (pchDisplayString && !gclNotify_CheckSettingFlags(eType, kNotifySettingFlags_DisableQueue))
	{
		for (i = 0; i < eaSize(&pAction->eaQueue); i++)
		{
			gclNotifyEnqueue(pAction->eaQueue[i], 
							 eType, 
							 pchDisplayString, 
							 pchLogicalString, 
							 pchSound, 
							 pchTexture, 
							 pHeadshotData, 
							 pchTag, 
							 iValue, 
							 itemID);
		}
	}
	if (pchSound && *pchSound)
	{
		sndPlayUIAudio(pchSound, s_Actions.pchFilename);
	}

	for (i = 0; i < eaSize(&pAction->eaSound); i++)
	{
		if (pAction->eaSound[i] && *(pAction->eaSound[i]))
			sndPlayUIAudio(pAction->eaSound[i], s_Actions.pchFilename);
	}

	if (pAction->pPopUpDialog && pchDisplayString && !gbNoGraphics)
	{
		if (!pAction->pPopUpDialog->bOnlyInUGC || g_ui_State.bInUGCEditor) 
		{
			for (i = 0; i < eaiSize(&pAction->pPopUpDialog->eaiClose); i++)
			{
				GameDialogClearType(pAction->pPopUpDialog->eaiClose[i]);
			}

			GameDialogTyped(eType, TranslateMessageRef(pAction->pPopUpDialog->hTitle), pchDisplayString);
		}
	}

	if (pAction->pChat && pchDisplayString) {
		char *estrDisplayString_SMFStripped = NULL;
		char *estrDisplayString_Expression = NULL;
		const char *pchChatDisplayString = pchDisplayString;

		if (pAction->pChat->pExprDisplayStringOverride)
		{
			estrStackCreate(&estrDisplayString_Expression);
			gclNotifyAction_ChatEvaluateStringExpression(pAction->pChat->pExprDisplayStringOverride, pchTag, iValue, pchChatDisplayString, &estrDisplayString_Expression);
			pchChatDisplayString = estrDisplayString_Expression;
		}

		if (pAction->pChat->bStripSMF) 
		{
			estrStackCreate(&estrDisplayString_SMFStripped);
			if (!StringStripTagsPrettyPrintEx(pchChatDisplayString, &estrDisplayString_SMFStripped, false))
			{
				estrDestroy(&estrDisplayString_SMFStripped);
			}
			else
			{
				pchChatDisplayString = estrDisplayString_SMFStripped;
			}
		}

		if (pAction->pChat->eType == kChatLogEntryType_System) 
		{
			// We use the notify type as the system message tag so
			// we can easily track down where the message came from.
			// This is only visible in the chat log if you have 
			// 'show message types' turned on in the chat settings.
			ChatLog_AddSystemMessageWithData(	pchChatDisplayString, 
												StaticDefineIntRevLookup(NotifyTypeEnum, eType), 
												pChatData);
		} 
		else if (!gclNotify_CheckSettingFlags(eType, kNotifySettingFlags_DisableChat))
		{
			ChatLog_AddChatMessage(	pAction->pChat->eType, 
									pchChatDisplayString, 
									pChatData);
		}

		estrDestroy(&estrDisplayString_SMFStripped);
		estrDestroy(&estrDisplayString_Expression);
	}

	for (i = 0; i < eaiSize(&pAction->eaiChainNotify); i++)
	{
		NotifyAction *pChain = gclNotifyAction_FindAction(pAction->eaiChainNotify[i], pchLogicalString);
		if (pChain)
		{
			gclNotifyActionDo(pChain, eType, pchDisplayString, pchLogicalString, pchSound, 
								pchTexture, pChatData, pHeadshotData, pchTag, iValue, vOrigin, itemID, erEntity);
		}
	}

	if (pAction->pFloatTo && pchDisplayString)
	{
		NotifyActionFloatToGenItem *pItem = gclNotifyAction_FloatToCreate(	pAction->pFloatTo, 
																			pchDisplayString, 
																			pchTag,
																			iValue, 
																			vOrigin, 
																			erEntity);
		if (pItem)
			eaPush(&s_eaFloatToGens, pItem);
	}

	estrDestroy(&pch);
}

// Flushes the delayed notifications when the player is not in a dialog
static void gclNotifyFlushDelayedNotifications(void)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	ContactDialog *pContactDialog = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, pContactDialog);

	if (pPlayerEnt && 
		pContactDialog == NULL &&
		eaSize(&s_eaDelayedNotifications) > 0)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(s_eaDelayedNotifications, NotifyQueueItem, pQueueItem)
		{
			if (pQueueItem)
			{
				gclNotifyReceiveInternal(pQueueItem->eType, 
										pQueueItem->pchDisplayString, 
										pQueueItem->pchLogicalString, 
										pQueueItem->pchSound, 
										pQueueItem->pchTexture,
										NULL,
										NULL,
										pQueueItem->pchTag,
										pQueueItem->iValue,
										pQueueItem->vOrigin,
										pQueueItem->itemID,
										pQueueItem->erEntity);
			}
		}
		FOR_EACH_END

		eaClearStruct(&s_eaDelayedNotifications, parse_NotifyQueueItem);
	}
}

// -----------------------------------------------------------------------------------------------------------------------

static bool gclNotifyAction_FloatToEvaluateStringExpression(Expression* pExpr, 
															const char *pchTag, 
															S32 iValue, 
															NotifyActionFloatToGenItem *pItem)
{
	if (pExpr)
	{
		MultiVal mVal;
		Entity *pPlayer = entActivePlayerPtr();

		exprContextSetSelfPtr(s_pNotifyContext, pPlayer);
		exprContextSetPartition(s_pNotifyContext, entGetPartitionIdx(pPlayer));
				
		gclNotifyUpdateExpressionContext(NULL, pPlayer, iValue, pchTag, NULL);

		exprEvaluate(pExpr, s_pNotifyContext, &mVal);

		if(MultiValIsString(&mVal))
		{
			if (mVal.str)
				pItem->pchString = StructAllocString(mVal.str);
			return true;
		}
	}
	return true;
}

// -----------------------------------------------------------------------------------------------------------------------
static bool gclNotifyAction_FloatToEvaluateBoolExpression(	Expression* pExpr, 
															const char *pchTag, 
															S32 iValue)
{
	if (pExpr)
	{
		MultiVal mVal;
		Entity *pPlayer = entActivePlayerPtr();

		exprContextSetSelfPtr(s_pNotifyContext, pPlayer);
		exprContextSetPartition(s_pNotifyContext, entGetPartitionIdx(pPlayer));
		gclNotifyUpdateExpressionContext(NULL, pPlayer, iValue, pchTag, NULL);

		exprEvaluate(pExpr, s_pNotifyContext, &mVal);

		return MultiValToBool(&mVal);
	}
	return true;
}

// -----------------------------------------------------------------------------------------------------------------------
static F32 scaleForCurrentResolution(F32 fScreenWidth, F32 fScreenHeight)
{
#define TARGET_RESOLUTION_Y		1050.f
	return fScreenHeight / TARGET_RESOLUTION_Y;
}

// -----------------------------------------------------------------------------------------------------------------------
static bool gclNotifyAction_FloatToCreateControlPoint(	const Vec2 vBasePoint, 
														const Vec2 vGoalPoint, 
														F32 fAngleOffset,
														F32 fAngleOffsetMax,
														F32 fDistanceMin,
														F32 fDistanceMax,
														bool bAllowMirror,
														F32 fResolutionScale,
														Vec2 vOutPoint)
{
	F32 fAngleOff, fAngle, fYawToGoal;
	F32 fDistance;
	Vec2 vControlOffset;
	Vec2 vBaseToGoal;
	bool bMirrored = false;
	
	subVec2(vGoalPoint, vBasePoint, vBaseToGoal);

	fYawToGoal = getVec2Yaw(vBaseToGoal);
	fAngleOff = RAD(fAngleOffset);
	
	if (fAngleOffsetMax)
		fAngleOff = addAngle(fAngleOff, RAD(fAngleOffsetMax) * randomF32());

	if (bAllowMirror && randomBool())
	{
		fAngleOff = fixAngle(-fAngleOff);
		bMirrored = true;
	}

	fAngle = addAngle(fAngleOff, fYawToGoal);

	setVec2FromYaw(vControlOffset, fAngle);

	fDistance = fDistanceMin + (fDistanceMax - fDistanceMin) * randomPositiveF32();
	scaleAddVec2(vControlOffset, fDistance * fResolutionScale, vBasePoint, vOutPoint);

	return bMirrored;
}

// -----------------------------------------------------------------------------------------------------------------------
static dtFxManager gclNotifyAction_GetFxManager()
{
	// note: the UI 2d FX manager is not working for some reason- so we are using the global FX manager for now
	//DynFxManager *pManager = dynFxGetUiManager(false);

	Entity *e = entActivePlayerPtr();
	if (e)
	{
		return e->dyn.guidFxMan;
	}
	else
	{
		DynFxManager *pMan = dynFxGetGlobalFxManager(zerovec3);
		return pMan ? pMan->guid : 0;
	}
}

// -----------------------------------------------------------------------------------------------------------------------
static void gclNotifyAction_FloatToUpdateFXPos(dtFx hFX, const Vec2 vScreen)
{
	S32 iScreenWidth, iScreenHeight;
	Vec3 vScreenNorm = {0};
	DynFx *pFX;
	DynNode *pNode;

	gfxGetActiveDeviceSize(&iScreenWidth, &iScreenHeight);

	// just in case the screen isn't initialized properly, so don't divide by zero 
	if (!iScreenWidth) iScreenWidth = 1;
	if (!iScreenHeight) iScreenHeight = 1;

	vScreenNorm[0] = vScreen[0] / iScreenWidth;
	vScreenNorm[1] = 1.f - (vScreen[1] / iScreenHeight);

	pFX = dynFxFromGuid(hFX);
	pNode = pFX ? dynFxGetNode(pFX) : NULL;
	if (pNode)
	{
		dynNodeSetPos(pNode, vScreenNorm);
	}
}

// -----------------------------------------------------------------------------------------------------------------------
static NotifyActionFloatToGenItem *gclNotifyAction_FloatToCreate(NotifyActionFloatToGen *pFloatTo, 
																const char *pchDisplayString, 
																const char *pchTag,
																S32 iValue,
																const Vec3 vOrigin,
																EntityRef erEntity)
{
	UIGen *pPrimary = GET_REF(pFloatTo->hGen);
	UIGen *pFallback = GET_REF(pFloatTo->hFallback);
	Vec2 vDestination = {0};
	bool bDestIsOffset = false;
	bool bAnchored = false;
	S32 iScreenWidth, iScreenHeight;
	F32 fResolutionScale;
	
	UIGen *pGen = NULL;

	if (pFloatTo->pExprShowFloatTo)
	{	// check if the floatTo should be shown or not
		if (!gclNotifyAction_FloatToEvaluateBoolExpression(pFloatTo->pExprShowFloatTo, pchTag, iValue))
			return NULL;
	}

	gfxGetActiveDeviceSize(&iScreenWidth, &iScreenHeight);
	fResolutionScale = scaleForCurrentResolution((F32)iScreenWidth, (F32)iScreenHeight);

	if (pPrimary && ui_GenInState(pPrimary, kUIGenStateVisible))
		pGen = pPrimary;
	else if (pFallback && ui_GenInState(pFallback, kUIGenStateVisible))
		pGen = pFallback;

	if (pGen)
	{
		CBoxGetCenter(&pGen->UnpaddedScreenBox, vDestination, vDestination+1);
	}
	else
	{
		bDestIsOffset = true;
	}

	if (pFloatTo->fOffsetMagnitude)
	{
		Vec2 vDir;
		
		setVec2FromYaw(vDir, -RAD(pFloatTo->fOffsetYaw));
		scaleVec2(vDir, pFloatTo->fOffsetMagnitude * fResolutionScale, vDir);

		addVec2(vDestination, vDir, vDestination);
	}

	{
		NotifyActionFloatToGenItem *pItem = StructCreate(parse_NotifyActionFloatToGenItem);
		Entity *pEnt = NULL;
		pItem->pDef = pFloatTo;
		pItem->fLifetime = 0;
		pItem->fScreenScale = fResolutionScale;
				
		if (GET_REF(pFloatTo->hFont))
		{
			if (pFloatTo->pExprDisplayStringOverride)
			{
				gclNotifyAction_FloatToEvaluateStringExpression(pFloatTo->pExprDisplayStringOverride, pchTag, iValue, pItem);
			}
			else
			{
				pItem->pchString = StructAllocString(pchDisplayString);
			}
		}

		if(pFloatTo->pchIconName)
			pItem->pSprite = atlasLoadTexture(pFloatTo->pchIconName);
				
		// Get the starting location
		if (erEntity && (pEnt = entFromEntityRefAnyPartition(erEntity)))
		{	
			if (pEnt->pEntUI && pEnt->pEntUI->pGen)
			{	// for now come out of the UI screenbox if we have it
				pItem->vPoint1[0] = interpF32(0.5f, pEnt->pEntUI->pGen->ScreenBox.left, pEnt->pEntUI->pGen->ScreenBox.right);
				pItem->vPoint1[1] = pEnt->pEntUI->pGen->ScreenBox.top;
			}
			else
			{	
				Vec3 vEntityPos;
				GfxCameraView *pView = gfxGetActiveCameraView();
				entGetPos(pEnt, vEntityPos);

				// project this position onto our screen
				gfxWorldToScreenSpaceVector(pView, vEntityPos, pItem->vPoint1, true);
				if (pFloatTo->bAnchorToWorld)
				{	
					bAnchored = true;
					copyVec3(vEntityPos, pItem->vWorldOrigin);
				}
			}
		}
		else if (vOrigin && !vec3IsZero(vOrigin))
		{
			GfxCameraView *pView = gfxGetActiveCameraView();
			
			// project this position onto our screen
			gfxWorldToScreenSpaceVector(pView, vOrigin, pItem->vPoint1, true);
			
			if (pFloatTo->bAnchorToWorld)
			{	
				bAnchored = true;
				copyVec3(vOrigin, pItem->vWorldOrigin);
			}
		}
		else
		{
			// use fStartX,Y to scale to the screen size
			setVec2(pItem->vPoint1, pFloatTo->fStartX * iScreenWidth, pFloatTo->fStartY * iScreenHeight);
		}
		
		if (bDestIsOffset)
		{
			addVec2(vDestination, pItem->vPoint1, vDestination);
			bDestIsOffset = false;
		}

		// set the destination and offset information
		switch (pFloatTo->eInterpType)
		{
			xcase ENotifyFloatToInterp_LINEAR:
			{
				copyVec2(vDestination, pItem->vPoint2);
				
			}
			xcase ENotifyFloatToInterp_SPLINE:
			{
				NotifyFloatToSplineDef *pSplineDef = pFloatTo->pSplineInfo;
				bool bMirrored;
				static NotifyFloatToSplineDef s_defaultSplineDef = 
				{
					20.f,	// fCtrl1_AngleOffset;
					25.f,	// fCtrl1_AngleOffsetMax;
					100.f,	// fCtrl1_DistanceMin;
					300.f,	// fCtrl1_DistanceMax;
					20.f,	// fCtrl2_AngleOffset;
					25.f,	// fCtrl2_AngleOffsetMax;
					100.f,	// fCtrl2_DistanceMin;
					300.f,	// fCtrl2_DistanceMax;
					true,	// bCtrl1_AllowMirror;
					false	// bCtrl2_AllowMirror;
				};

				if (!pSplineDef)
					pSplineDef = &s_defaultSplineDef;
				
				// get the control points and the last position
				copyVec2(vDestination, pItem->vPoint4);

				// compute the first control point
				bMirrored = gclNotifyAction_FloatToCreateControlPoint(	pItem->vPoint1, pItem->vPoint4, 
																		pSplineDef->fCtrl1_AngleOffset, 
																		pSplineDef->fCtrl1_AngleOffsetMax,
																		pSplineDef->fCtrl1_DistanceMin,
																		pSplineDef->fCtrl1_DistanceMax,
																		pSplineDef->bCtrl1_AllowMirror,
																		fResolutionScale,
																		pItem->vPoint2);

				gclNotifyAction_FloatToCreateControlPoint(	pItem->vPoint2, pItem->vPoint4, 
															bMirrored ? fixAngle(-pSplineDef->fCtrl2_AngleOffset) : 
																		pSplineDef->fCtrl2_AngleOffset, 
															pSplineDef->fCtrl2_AngleOffsetMax,
															pSplineDef->fCtrl2_DistanceMin,
															pSplineDef->fCtrl2_DistanceMax,
															pSplineDef->bCtrl2_AllowMirror,
															fResolutionScale,
															pItem->vPoint3);
			}
		}

		if (pFloatTo->bAnchorToWorld && bAnchored)
		{	// if we're anchoring the notify to the world
			subVec2(pItem->vPoint2, pItem->vPoint1, pItem->vPoint2);
			subVec2(pItem->vPoint3, pItem->vPoint1, pItem->vPoint3);
			subVec2(pItem->vPoint4, pItem->vPoint1, pItem->vPoint4);
		}


		if (pFloatTo->pchAttachedFX)
		{
			dtFxManager guidFxMan = gclNotifyAction_GetFxManager();

			if (guidFxMan)
			{
				pItem->hFX = dtAddFx(	guidFxMan, pFloatTo->pchAttachedFX, 
										NULL, 0, 0, 1.f, 
										0, NULL, eDynFxSource_UI, NULL, NULL);

				if (pItem->hFX)
				{
					gclNotifyAction_FloatToUpdateFXPos(pItem->hFX, pItem->vPoint1);
				}
			}
		}


		return pItem;
	}

	
	return NULL;
}


// -----------------------------------------------------------------------------------------------------------------------
// returns false when the FloatTo is finished and should be destroyed
static S32 gclNotifyAction_FloatToUpdate(NotifyActionFloatToGenItem *pFloatTo, F32 fDTime)
{
	F32 fScale = 1.f;
	F32 fAlpha = 1.f;
	S32 iAlpha = 255;
	F32 fLifetimeScale = 0.f;
	Vec2 vCurPos = {0};
	F32 fStringSize = 0.f;
	NotifyActionFloatToGen *pDef = pFloatTo->pDef;

	pFloatTo->fLifetime += fDTime;

	if (pFloatTo->fLifetime >= pDef->fLifetime)
	{
		if (pDef->pExprOnExpire)
		{
			gclNotifyAction_FloatToEvaluateBoolExpression(pDef->pExprOnExpire, NULL, 0);
		}

		gclNotifyAction_FloatToDestroy(pFloatTo);
		return false;
	}

	
	fLifetimeScale = pFloatTo->fLifetime / pDef->fLifetime;

	switch (pDef->eInterpType)
	{
		xcase ENotifyFloatToInterp_LINEAR:
		{
			if (pDef->bAnchorToWorld && !vec3IsZero(pFloatTo->vWorldOrigin))
			{
				GfxCameraView *pView = gfxGetActiveCameraView();
				Vec2 vPoint1, vPoint2;
				// project this position onto our sCreateden
				gfxWorldToScreenSpaceVector(pView, pFloatTo->vWorldOrigin, vPoint1, true);

				addVec2(vPoint1, pFloatTo->vPoint2, vPoint2);

				interpVec2(fLifetimeScale, vPoint1, vPoint2, vCurPos);
			}
			else
			{
				interpVec2(fLifetimeScale, pFloatTo->vPoint1, pFloatTo->vPoint2, vCurPos);
			}
		}

		xcase ENotifyFloatToInterp_SPLINE:
		{
			if (pDef->bAnchorToWorld && !vec3IsZero(pFloatTo->vWorldOrigin))
			{
				GfxCameraView *pView = gfxGetActiveCameraView();
				Vec2 vPoints[4];

				// project this position onto our screen
				gfxWorldToScreenSpaceVector(pView, pFloatTo->vWorldOrigin, vPoints[0], true);
				addVec2(vPoints[0], pFloatTo->vPoint2, vPoints[1]);
				addVec2(vPoints[0], pFloatTo->vPoint3, vPoints[2]);
				addVec2(vPoints[0], pFloatTo->vPoint4, vPoints[3]);
				
				bezierGetPoint(vPoints, fLifetimeScale, vCurPos); 
			}
			else
			{
				bezierGetPoint((Vec2*)pFloatTo->vPoint1, fLifetimeScale, vCurPos); 
			}
		}
	}

	// see if and how we want to adjust the scale
	fScale = 1.f;

	// adjust the scale and alpha if it's the right time
	
	if (pDef->fScaleOutNormTime && fLifetimeScale >= (1.f - pDef->fScaleOutNormTime))
	{
		F32 t = 1.f - (1.0f - fLifetimeScale)/pDef->fScaleOutNormTime;
		fScale = lerp(1.f, 0.01f, t);
	}
	else if (pDef->fScaleInNormTime && fLifetimeScale <= pDef->fScaleInNormTime)
	{
		F32 t = fLifetimeScale/pDef->fScaleInNormTime;
		fScale = lerp(0.01f, 1.f, t);
	}

	if (pDef->fFadeOutNormTime && fLifetimeScale >= (1.f - pDef->fFadeOutNormTime))
	{
		F32 t = 1.f - (1.0f - fLifetimeScale)/pDef->fFadeOutNormTime;
		fAlpha = lerp(1.f, 0.f, t);
	}
	else if (pDef->fFadeInNormTime && fLifetimeScale <= pDef->fFadeInNormTime)
	{
		F32 t = fLifetimeScale/pDef->fFadeInNormTime;
		fAlpha = lerp(0.f, 1.f, t);
	}
	
	iAlpha = fAlpha * 255;
	iAlpha = CLAMP(iAlpha, 0, 255);

	if (pFloatTo->pchString)
	{
		int colors[4];
		int clr = 0xFFFFFF00 | iAlpha;
		UIStyleFont *pFont = GET_REF(pDef->hFont);
				
		setVec4same(colors, clr);
		
		ui_StyleFontUse(pFont, false, kWidgetModifier_None);
		gfxfont_SetAlpha(iAlpha);
		gfxfont_Print(vCurPos[0], vCurPos[1], UI_INFINITE_Z, 
						pDef->fTextScale * fScale * pFloatTo->fScreenScale, 
						pDef->fTextScale * fScale * pFloatTo->fScreenScale, 
						CENTER_XY, pFloatTo->pchString);

		if (pFloatTo->pSprite)
			fStringSize = ui_StyleFontWidth(pFont, pDef->fTextScale * fScale * pFloatTo->fScreenScale, pFloatTo->pchString);
	}

		
	if (pFloatTo->pSprite)
	{
		CBox box = {0};
		S32 alpha = fAlpha * 255;
		alpha = CLAMP(alpha, 0, 255);
		
		if (fStringSize)
		{
			vCurPos[0] -= fStringSize * 0.5f + (fScale * pFloatTo->fScreenScale * pDef->fIconWidth * 0.5f);
		}
		BuildCBoxFromCenter(&box, vCurPos[0], vCurPos[1], 
							fScale * pDef->fIconWidth * pFloatTo->fScreenScale, 
							fScale * pDef->fIconHeight * pFloatTo->fScreenScale);

		display_sprite_box(pFloatTo->pSprite, &box, UI_INFINITE_Z, 0xFFFFFF00|alpha);
	}

	if (pFloatTo->hFX)
	{
		gclNotifyAction_FloatToUpdateFXPos(pFloatTo->hFX, vCurPos);
	}
	return true;
}

// -----------------------------------------------------------------------------------------------------------------------
static S32 gclNotifyProcessQueues(F32 fElapsed)
{
	S32 i;
	S32 j;
	S32 iRemoved = 0;
	S32 iScreenWidth;
	S32 iScreenHeight;

	// Flush the delayed notifications when possible
	gclNotifyFlushDelayedNotifications();

	gfxGetActiveDeviceSize(&iScreenWidth, &iScreenHeight);

	for (i = eaSize(&s_Queues.eaQueues) - 1; i >= 0; i--)
	{
		NotifyQueue *pQueue = s_Queues.eaQueues[i];
		for (j = eaSize(&pQueue->eaItems) - 1; j >= 0; j--)
		{
			NotifyQueueItem *pItem = pQueue->eaItems[j];

			if (pItem->fDelay >= fElapsed)
			{
				pItem->fDelay -= fElapsed;
			}
			else if (!pItem->bInfinite)
			{
				F32 fCalcElapsed = fElapsed;

				if (pItem->fDelay > 0.0f)
				{
					fCalcElapsed = fElapsed - pItem->fDelay;
					pItem->fDelay = 0.0f;
				}
				pItem->fLifetime -= fCalcElapsed;
				if (pItem->fLifetime <= 0 || j >= NOTIFY_QUEUE_MAX)
				{
					StructDestroy(parse_NotifyQueueItem, pItem);
					eaRemove(&pQueue->eaItems, j);
					iRemoved++;
				}
			}
		}
	}

	for (i = eaSize(&s_eaFloatToGens) - 1; i >= 0; i--)
	{
		if (!gclNotifyAction_FloatToUpdate(s_eaFloatToGens[i], fElapsed))
		{
			eaRemove(&s_eaFloatToGens, i);
			iRemoved++;
		}
	}

	return iRemoved;
}

bool gclNotifyAudioHasAnyEvents(void)
{
	return eaSize(&s_AudioEvents.eaGroups) > 0;
}

static bool gclNotifyAudioEvent_EvaluateExpression(Expression* pExpr)
{
	if (pExpr)
	{
		MultiVal mVal;
		Entity *pPlayer = entActivePlayerPtr();

		exprContextSetSelfPtr(s_pNotifyContext, pPlayer);
		exprContextSetPartition(s_pNotifyContext, entGetPartitionIdx(pPlayer));

		exprEvaluate(pExpr, s_pNotifyContext, &mVal);

		if(MultiValGetInt(&mVal,NULL) <= 0)
		{
			return false;
		}
	}
	return true;
}

static void gclNotifyAudioEvent_Activate(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID NotifyAudioEventGroup* pGroup, SA_PARAM_NN_VALID NotifyAudioEvent* pNotify)
{
	NotifyAudioEventHistory* pHistory;
	F32 fCurTime = gGCLState.totalElapsedTimeMs / 1000.0;
	NotifyType eType = pNotify->eType;
	const char* pchDisplayString = entTranslateMessage(pEnt,GET_REF(pNotify->DisplayMsg.hMessage));
	const char* pchTexture = pNotify->pchTexture;
	const char* pchSound;
	bool bIsSuggestion = false;
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);

	if (!pHUDOptions)
	{
		return;
	}
	devassert(pEnt->pPlayer);

	if (pHUDOptions->eNotifyAudioMode == PlayerNotifyAudioMode_Standard ||
		!pNotify->pchSuggestionSound || !pNotify->pchSuggestionSound[0])
	{
		pchSound = pNotify->pchSound;
	}
	else
	{
		pchSound = pNotify->pchSuggestionSound;
		bIsSuggestion = true;
	}
	
	gclNotifyReceiveAudio(eType, pchDisplayString, NULL, pchSound, pchTexture);
	
	pNotify->bActivateHold = false;
	pNotify->bActivated = true;
	pNotify->fActivateTime = fCurTime;
	pNotify->iActivateCount++;

	pHistory = StructCreate(parse_NotifyAudioEventHistory);
	pHistory->pEvent = pNotify;
	pHistory->fActivateTime = fCurTime;
	pHistory->bIsSuggestion = bIsSuggestion;
	eaPush(&pGroup->eaHistory, pHistory);

	while (eaSize(&pGroup->eaHistory) > MAX_AUDIO_EVENT_HISTORY)
	{
		StructDestroy(parse_NotifyAudioEventHistory, eaRemove(&pGroup->eaHistory, 0));
	}
}

static bool gclNotifyAudioEvent_CanActivate( SA_PARAM_NN_VALID NotifyAudioEvent* pNotify )
{
	if (pNotify->bCanActivate)
	{
		return gclNotifyAudioEvent_EvaluateExpression( pNotify->pActivateExpr );
	}
	return false;
}

static bool gclNotifyAudioEvent_CanActivateAny(SA_PARAM_NN_VALID NotifyAudioEventGroup* pGroup)
{
	F32 fCurTime = gGCLState.totalElapsedTimeMs / 1000.0;
	NotifyAudioEventHistory* pHistory = eaGetLast(&pGroup->eaHistory);

	if (pHistory && pHistory->pEvent)
	{
		F32 fDuration = pHistory->bIsSuggestion ? pHistory->pEvent->fSuggestDuration : pHistory->pEvent->fDuration;

		if (pHistory->fActivateTime > fCurTime - fDuration)
		{
			return false;
		}
	}
	return true;
}

static void gclNotifyProcessAudioEvents(F32 fElapsed)
{
	Entity* pEnt = entActivePlayerPtr();
	S32 i, j, iGroupSize = eaSize(&s_AudioEvents.eaGroups);
	F32 fCurTime = gGCLState.totalElapsedTimeMs / 1000.0;
	bool bCanActivateAny;
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);

	if (!pHUDOptions || pHUDOptions->eNotifyAudioMode == PlayerNotifyAudioMode_Off)
		return;

	devassert(pEnt && pEnt->pPlayer);

	for (i = 0; i < iGroupSize; i++)
	{
		NotifyAudioEventGroup* pGroup = s_AudioEvents.eaGroups[i];
		S32 iEventSize = eaSize(&pGroup->eaList);

		if (pGroup->pRequiresExpr)
		{
			gclNotifyUpdateExpressionContext(NULL, pEnt, 0, NULL, NULL);
			if (!gclNotifyAudioEvent_EvaluateExpression(pGroup->pRequiresExpr))
			{
				if (pGroup->bUpdatedLastFrame)
				{
					for (j = 0; j < iEventSize; j++)
					{
						NotifyAudioEvent* pNotify = pGroup->eaList[j];
						pNotify->bCanActivate = true;
						pNotify->bActivated = false;
						pNotify->bActivateHold = false;
						if (pNotify->pData)
						{
							StructDestroySafe(parse_NotifyAudioEventData, &pNotify->pData);
						}
					}
					pGroup->bUpdatedLastFrame = false;
				}
				continue;
			}
		}

		pGroup->bUpdatedLastFrame = true;
		bCanActivateAny = gclNotifyAudioEvent_CanActivateAny(pGroup);

		for (j = 0; j < iEventSize; j++)
		{
			NotifyAudioEvent* pNotify = pGroup->eaList[j];

			gclNotifyUpdateExpressionContext(pNotify, pEnt, 0, NULL, NULL);

			if (pNotify->pUpdateExpr)
			{
				gclNotifyAudioEvent_EvaluateExpression( pNotify->pUpdateExpr );
			}
			if (pNotify->bActivated)
			{
				if (pNotify->fActivateTime < fCurTime - pNotify->fResetTime)
				{
					if (pNotify->pResetExpr && gclNotifyAudioEvent_EvaluateExpression(pNotify->pResetExpr))
					{
						pNotify->bActivated = false;
					}
				}
			}
			else if (pNotify->fActivateHoldTime > 0.001f)
			{
				if (gclNotifyAudioEvent_CanActivate(pNotify))
				{
					if (!pNotify->bActivateHold)
					{
						pNotify->fActivateTime = fCurTime;
						pNotify->bActivateHold = true;
					}
					else if (bCanActivateAny && pNotify->fActivateTime < fCurTime - pNotify->fActivateHoldTime)
					{
						gclNotifyAudioEvent_Activate(pEnt, pGroup, pNotify);
						bCanActivateAny = false;
					}
				}
				else
				{
					pNotify->fActivateTime = 0.0f;
					pNotify->bActivateHold = false;
				}
			}
			else if (bCanActivateAny && gclNotifyAudioEvent_CanActivate(pNotify))
			{
				gclNotifyAudioEvent_Activate(pEnt, pGroup, pNotify);
				bCanActivateAny = false;
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(NotifyAudioEventsResetAll);
void gclCmdNotifyAudioEventsResetAll(void)
{
	S32 i, j;
	for (i = eaSize(&s_AudioEvents.eaGroups)-1; i >= 0; i--)
	{
		NotifyAudioEventGroup* pGroup = s_AudioEvents.eaGroups[i];
		for (j = eaSize(&pGroup->eaList)-1; j >= 0; j--)
		{
			NotifyAudioEvent* pNotify = pGroup->eaList[j];
			pNotify->iActivateCount = 0;
			pNotify->fActivateTime = 0.0f;
			pNotify->bActivated = false;
			pNotify->bCanActivate = true;
			if (pNotify->pData)
			{
				StructDestroySafe(parse_NotifyAudioEventData, &pNotify->pData);
			}
		}
		eaDestroyStruct(&pGroup->eaHistory, parse_NotifyAudioEventHistory);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyAudioEventSetCanActivate);
void gclExprNotifyAudioEventSetCanActivate( SA_PARAM_OP_VALID NotifyAudioEvent* pNotify, bool bValue )
{
	if (pNotify)
	{
		pNotify->bCanActivate = bValue;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyAudioEventSetIntData);
void gclExprNotifyAudioEventSetIntData( SA_PARAM_OP_VALID NotifyAudioEvent* pNotify, S32 iValue )
{
	if (pNotify)
	{
		if (!pNotify->pData)
		{
			pNotify->pData = StructCreate(parse_NotifyAudioEventData);
		}
		pNotify->pData->iInt = iValue;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyAudioEventGetIntData);
S32 gclExprNotifyAudioEventGetIntData( SA_PARAM_OP_VALID NotifyAudioEvent* pNotify )
{
	return pNotify && pNotify->pData ? pNotify->pData->iInt : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyAudioEventSetFloatData);
void gclExprNotifyAudioEventSetFloatData( SA_PARAM_OP_VALID NotifyAudioEvent* pNotify, F32 fValue )
{
	if (pNotify)
	{
		if (!pNotify->pData)
		{
			pNotify->pData = StructCreate(parse_NotifyAudioEventData);
		}
		pNotify->pData->fFloat = fValue;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyAudioEventGetFloatData);
F32 gclExprNotifyAudioEventGetFloatData( SA_PARAM_OP_VALID NotifyAudioEvent* pNotify )
{
	return pNotify && pNotify->pData ? pNotify->pData->fFloat : 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyAudioEventSetStringData);
void gclExprNotifyAudioEventSetStringData( SA_PARAM_OP_VALID NotifyAudioEvent* pNotify, const char* pchString )
{
	if (pNotify)
	{
		if (!pNotify->pData)
		{
			pNotify->pData = StructCreate(parse_NotifyAudioEventData);
		}
		if (pNotify->pData->pchString)
		{
			StructFreeString(pNotify->pData->pchString);
		}
		StructCopyString(&pNotify->pData->pchString, pchString);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyAudioEventGetStringData);
const char* gclExprNotifyAudioEventGetStringData( SA_PARAM_OP_VALID NotifyAudioEvent* pNotify )
{
	return pNotify && pNotify->pData ? pNotify->pData->pchString : "";
}

void gclNotifyUpdate(F32 fElapsed)
{
	gclNotifyProcessQueues(fElapsed);
	gclNotifyProcessAudioEvents(fElapsed);
}

S32 gclGenGetNotifyQueues(ExprContext *pContext, NotifyQueueItem ***peaItems, UIGen *pGen, const char *pchQueue, S32 maxNotifications)
{
	char *pchQueueCopy;
	char *pchContext;
	char *pchStart;

	strdup_alloca(pchQueueCopy, pchQueue);
	pchStart = strtok_r(pchQueueCopy, " ", &pchContext);
	eaClearFast(peaItems);
	do 
	{
		NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pchStart);
		if (pQueue)
		{
			S32 i = 0;
			FOR_EACH_IN_EARRAY(pQueue->eaItems, NotifyQueueItem, pItem) 
			{
				if (pItem->fDelay <= 0.0f)
				{
					eaPush(peaItems, pItem);
					if (maxNotifications > 0 && ++i >= maxNotifications)
						break;
				}
			}
			FOR_EACH_END
		}
		else
			ErrorFilenamef(exprContextGetBlameFile(pContext), "%s: Invalid notify queue %s.", pGen->pchName, pchStart);
	} while (pchStart = strtok_r(NULL, " ", &pchContext));

	return eaSize(peaItems);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetNotifyQueueSize);
S32 gclGenExprGetNotifyQueueSize(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchQueue)
{
	static NotifyQueueItem **s_eaItems = NULL;
	return gclGenGetNotifyQueues(pContext, &s_eaItems, pGen, pchQueue, -1);
}

//Removes the oldest members of the specified queue until it is reduced to the proper size.
//Return the number of elements removed.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenReduceNotifyQueueToSize);
S32 gclGenReduceNotifyQueueToSize(ExprContext *pContext, const char *pchQueue, int size)
{
	int numRemoved = 0;

	NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pchQueue);
	if (pQueue && pQueue->eaItems)
	{
		for (numRemoved = 0; numRemoved < eaSize(&pQueue->eaItems)-size; numRemoved++)
		{
			gclNotifyKillItem(pQueue->eaItems[numRemoved]);
		}
		return numRemoved;
	}

	return 0;
}

// Get a list of all active NotifyQueueItems in the given queues. Queue names are not
// statically checked (since gens need to be loaded before notifications), but are
// checked at runtime.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNotifyQueues);
S32 gclGenExprNotifyQueues(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchQueue)
{
	static NotifyQueueItem **s_eaItems = NULL;
	gclGenGetNotifyQueues(pContext, &s_eaItems, pGen, pchQueue, -1);
	ui_GenSetListSafe(pGen, &s_eaItems, NotifyQueueItem);
	return eaSize(&s_eaItems);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNotifyQueuesMaxNotifications);
S32 gclGenExprNotifyQueuesMaxNotifications(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchQueue, S32 maxNotifications)
{
	static NotifyQueueItem **s_eaItems = NULL;
	gclGenGetNotifyQueues(pContext, &s_eaItems, pGen, pchQueue, maxNotifications);
	ui_GenSetListSafe(pGen, &s_eaItems, NotifyQueueItem);
	return eaSize(&s_eaItems);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyQueuesGetNotify);
SA_RET_OP_VALID NotifyQueueItem *gclGenExprNotifyQueuesGetNotify(ExprContext *pContext, const char *pchQueue, S32 iNotify)
{
	static NotifyQueueItem **s_eaItems = NULL;
	static const char *s_pchQueue = NULL;
	static U32 s_uFrame;

	if (gGCLState.totalElapsedTimeMs != s_uFrame || s_pchQueue != pchQueue)
	{
		UIGen *pSelf = (UIGen *)exprContextGetUserPtr(pContext, parse_UIGen);
		if (pSelf)
			gclGenGetNotifyQueues(pContext, &s_eaItems, NULL, pchQueue, -1);
		else
			eaClearFast(&s_eaItems);
	}

	return eaGet(&s_eaItems, iNotify);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNotifyQueuesForItems);
S32 gclGenExprNotifyQueuesForItems(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_STR const char *pchQueue, bool bUseItemTypesAsExclusionList, SA_PARAM_OP_STR const char *pchItemTypes)
{
	static NotifyQueueItem **eaAllQueueItems = NULL;
	static char **eaItemTypeNames = NULL;
	S32 iItemCount = 0;
	char *pchItemTypesCopy = NULL;

	// Get the array that the gen manages
	NotifyQueueItemWithItemDef ***peaItems = ui_GenGetManagedListSafe(pGen, NotifyQueueItemWithItemDef);

	// Clear the list of item type names
	eaClear(&eaItemTypeNames);

	// Tokenize item type names
	if (pchItemTypes && pchItemTypes[0])
	{
		char *pchItemType = NULL;
		char *pchStrTokContext = NULL;
		pchItemTypesCopy = strdup(pchItemTypes);
		pchItemType = strtok_s(pchItemTypesCopy, " ,", &pchStrTokContext);

		while (pchItemType != NULL)
		{
			eaPush(&eaItemTypeNames, pchItemType);
			pchItemType = strtok_s(NULL, " ,", &pchStrTokContext);
		}
	}

	// Get all queue items from the queue
	gclGenGetNotifyQueues(pContext, &eaAllQueueItems, pGen, pchQueue, -1);

	FOR_EACH_IN_EARRAY_FORWARDS(eaAllQueueItems, NotifyQueueItem, pQueueItem)
	{
		ItemDef *pItemDef = NULL;
		if (pQueueItem && 
			(pItemDef = item_DefFromName(pQueueItem->pchLogicalString)) != NULL)
		{
			bool bInclude;
			if (eaSize(&eaItemTypeNames) > 0)
			{
				// Item type of the current item as a string
				const char *pchItemType = StaticDefineIntRevLookup(ItemTypeEnum, pItemDef->eType);

				S32 itItemTypeNames;

				// Include by default for exclusion lists, and exclude by default for inclusion lists
				bInclude = bUseItemTypesAsExclusionList;
				for (itItemTypeNames = 0; itItemTypeNames < eaSize(&eaItemTypeNames); itItemTypeNames++)
				{					
					if (stricmp(pchItemType, eaItemTypeNames[itItemTypeNames]) == 0)
					{
						bInclude = !bUseItemTypesAsExclusionList;
						break;
					}
				}
			}
			else
			{
				bInclude = true;
			}
			if (bInclude)
			{
				NotifyQueueItemWithItemDef *pQueueItemWithItemDef = eaGetStruct(peaItems, parse_NotifyQueueItemWithItemDef, iItemCount++);
				pQueueItemWithItemDef->pItemDef = pItemDef;
				pQueueItemWithItemDef->pQueueItem = pQueueItem;
			}
		}
	}
	FOR_EACH_END


	// Clean up
	eaClear(&eaItemTypeNames);
	if (pchItemTypesCopy)
		free(pchItemTypesCopy);

	// Trim unused elements from the list
	eaSetSizeStruct(peaItems, parse_NotifyQueueItemWithItemDef, iItemCount);

	// Set the list on the gen
	ui_GenSetManagedListSafe(pGen, peaItems, NotifyQueueItemWithItemDef, true);

	return eaSize(peaItems);
}


// Get a list of all active NotifyQueueItems in the given queues in reverse order. Queues names are not
// statically checked (since gens need to be loaded before notifications), but are
// checked at runtime.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNotifyQueuesReverse);
S32 gclGenExprNotifyQueuesReverse(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchQueue)
{
	static NotifyQueueItem **s_eaItems = NULL;
	gclGenGetNotifyQueues(pContext, &s_eaItems, pGen, pchQueue, -1);
	eaReverse(&s_eaItems);
	ui_GenSetListSafe(pGen, &s_eaItems, NotifyQueueItem);
	return eaSize(&s_eaItems);
}

// Find a NotifyQueueItem in a given queue by logical name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NotifyQueueItem);
SA_RET_OP_VALID NotifyQueueItem *gclGenExprNotifyQueueItem(ExprContext *pContext, const char *pchQueue, const char *pchLogicalString)
{
	NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pchQueue);
	S32 i;
	pchLogicalString = allocFindString(pchLogicalString);
	if (pQueue && pchLogicalString)
	{
		for (i = 0; i < eaSize(&pQueue->eaItems); i++)
			if (pQueue->eaItems[i]->pchLogicalString == pchLogicalString)
				return pQueue->eaItems[i];
	}
	return NULL;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(NotifyQueueClearTag) ACMD_CLIENTCMD ACMD_HIDE;
void gclNotifyQueueClearTag(const char *pchQueue, const char * pchTag)
{
	char *pchQueueCopy;
	char *pchContext;
	char *pchStart;

	strdup_alloca(pchQueueCopy, pchQueue);
	pchStart = strtok_r(pchQueueCopy, " ", &pchContext);
	do 
	{
		NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pchStart);
		if (pQueue)
		{
			int i;
			for (i = eaSize(&pQueue->eaItems)-1; i >= 0; --i)
			{
				if (pQueue->eaItems[i]->pchTag && strcmp(pQueue->eaItems[i]->pchTag,pchTag) == 0)
				{
					gclNotifyKillItem(pQueue->eaItems[i]);
				}
			}
		}
	} while (pchStart = strtok_r(NULL, " ", &pchContext));
}

// Deprecated, this function is misspelled. Use GenNotifyQueueRemoveItem.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetNotifyQueueRemoveItem);
void gclGenExprNotifyQueueRemoveItem(ExprContext *pContext, const char *pchQueue, SA_PARAM_OP_VALID NotifyQueueItem *pItem);

// Set the lifetime of this queue item to 0, which will cause it to be removed next tick.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNotifyQueueRemoveItem);
void gclGenExprNotifyQueueRemoveItem(ExprContext *pContext, const char *pchQueue, SA_PARAM_OP_VALID NotifyQueueItem *pItem)
{
	char *pchQueueCopy;
	char *pchContext;
	char *pchStart;
	int i;

	if (!pItem) {
		return;
	}

	strdup_alloca(pchQueueCopy, pchQueue);
	pchStart = strtok_r(pchQueueCopy, " ", &pchContext);
	do 
	{
		NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pchStart);
		if (pQueue)
		{
			for (i = eaSize(&pQueue->eaItems)-1; i >= 0; --i)
			{
				if (pQueue->eaItems[i] == pItem)
				{
					gclNotifyKillItem(pItem);
				}
			}
		}
	} while (pchStart = strtok_r(NULL, " ", &pchContext));
}

// Deprecated, this function is misspelled. Use GenNotifyQueueRemoveAll.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetNotifyQueueRemoveAll);
void gclGenExprNotifyQueueRemoveAll(ExprContext *pContext, const char *pchQueue);

// Remove all items from a notification queue.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNotifyQueueRemoveAll);
void gclGenExprNotifyQueueRemoveAll(ExprContext *pContext, const char *pchQueue)
{
	char *pchQueueCopy;
	char *pchContext;
	char *pchStart;
	int i;

	strdup_alloca(pchQueueCopy, pchQueue);
	pchStart = strtok_r(pchQueueCopy, " ", &pchContext);
	do 
	{
		NotifyQueue *pQueue = eaIndexedGetUsingString(&s_Queues.eaQueues, pchStart);
		if (pQueue)
		{
			for (i = eaSize(&pQueue->eaItems)-1; i >= 0; --i)
			{
				gclNotifyKillItem(pQueue->eaItems[i]);
			}
		}
	} while (pchStart = strtok_r(NULL, " ", &pchContext));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetNotifyNameFromType);
const char* gclGenExprGetNotifyNameFromType( S32 eType )
{
	return StaticDefineIntRevLookup( NotifyTypeEnum, eType );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetHeadshotFromNotifyItem);
SA_RET_OP_VALID BasicTexture* gclGenExprGetHeadshotFromNotifyItem( SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID NotifyQueueItem *pNotifyItem, SA_PARAM_OP_VALID BasicTexture *pTexture, F32 fWidth, F32 fHeight)
{
	return NULL;
}

static bool gclNotify_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

void gclNotify_NotifyAction_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	*ppcType = strdup("NotifyAction");

	FOR_EACH_IN_EARRAY(s_Actions.eaActions, NotifyAction, pNotifyAction)
	{
		bool bResourceHasAudio = false;

		FOR_EACH_IN_EARRAY(pNotifyAction->eaSound, const char, pcSound) {
			bResourceHasAudio |= gclNotify_GetAudioAssets_HandleString(pcSound, peaStrings);
		} FOR_EACH_END;

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	FOR_EACH_END;
}

void gclNotify_NotifyAudioEvent_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	*ppcType = strdup("NotifyAudioEvent");

	FOR_EACH_IN_EARRAY(s_AudioEvents.eaGroups, NotifyAudioEventGroup, pNotifyGroup) {
		FOR_EACH_IN_EARRAY(pNotifyGroup->eaList, NotifyAudioEvent, pNotifyEvent)
		{
			bool bResourceHasAudio = false;

			bResourceHasAudio |= gclNotify_GetAudioAssets_HandleString(pNotifyEvent->pchSound,			peaStrings);
			bResourceHasAudio |= gclNotify_GetAudioAssets_HandleString(pNotifyEvent->pchSuggestionSound,peaStrings);

			*puiNumData = *puiNumData + 1;
			if (bResourceHasAudio) {
				*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
			}
		}
		FOR_EACH_END;
	} FOR_EACH_END;
}