#include "ResourceManager.h"
#include "Message.h"
#include "earray.h"
#include "cmdparse.h"
#include "Expression.h"

#include "gslEntity.h"
#include "Entity.h"
#include "ChatData.h"
#include "gslChat.h"
#include "EntityMovementManager.h"
#include "EntityMovementEmote.h"
#include "PowersMovement.h"
#include "PowerAnimFx.h"
#include "GameStringFormat.h"
#include "dynBitField.h"
#include "gslEventSend.h"
#include "AnimList_Common.h"
#include "aiAnimList.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "mission_common.h"
#include "NameList.h"
#include "NotifyCommon.h"
#include "EmoteCommon.h"
#include "Player.h"
#include "Character.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "CombatConfig.h"

#include "gslEmote_c_ast.h"

#include "AutoGen/EmoteCommon_h_ast.h"
#include "AutoGen/ChatData_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

typedef struct EmoteDef EmoteDef;

static ExprContext *s_pEmoteContext;
NameList* g_pEmoteNameList = NULL;
static RefDictIterator s_EmoteDictIterator = {0};

// Do not send chat text for the same emote used in a row until
// this many seconds without using it have passed.
#define EMOTE_CHAT_RATE_LIMIT 30

#define EMOTE_UNKNOWN_MSG "Emote_Unknown"
#define EMOTE_DEFAULT_CANNOT_USE_MSG "Emote_CannotUse_Default"

AUTO_ENUM;
typedef enum EmoteMessageArgType
{	
	EmoteMessageArgType_None	= 0,
	EmoteMessageArgType_Int		= 1,
	EmoteMessageArgType_Float	= 2,
	EmoteMessageArgType_String  = 3,
} EmoteMessageArgType;

AUTO_STRUCT;
typedef struct EmoteMessage
{
	REF_TO(Message) hMessage;		AST(STRUCTPARAM REQUIRED)
	S32 iChance;					AST(STRUCTPARAM DEFAULT(1))
	EmoteMessageArgType eArgType;	AST(NAME(ArgumentType) DEFAULT(EmoteMessageArgType_None))
	Expression* pExprMsgArg;		AST(NAME(ArgumentExpr) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
} EmoteMessage;

AUTO_STRUCT;
typedef struct EmoteAnimList
{
	REF_TO(AIAnimList) hAnimList; AST(STRUCTPARAM REQUIRED)
	S32 iChance; AST(STRUCTPARAM DEFAULT(1))
} EmoteAnimList;

AUTO_STRUCT;
typedef struct EmoteSubemote
{
	REF_TO(EmoteDef) hEmote; AST(STRUCTPARAM REQUIRED)
	S32 iChance; AST(STRUCTPARAM DEFAULT(1))
} EmoteSubemote;

AUTO_STRUCT;
typedef struct EmoteAnimBits
{
	const char **eaAnimBits; AST(POOL_STRING STRUCTPARAM REQUIRED)
	S32 iChance; AST(DEFAULT(1))
} EmoteAnimBits;

// Return the number of this item the player has.
AUTO_EXPR_FUNC(Emote) ACMD_NAME(Item);
S32 emoteExprItem(ACMD_EXPR_SELF Entity *pEnt, ACMD_EXPR_RES_DICT(ItemDef) const char *pchItem)
{
	return pEnt ? inv_ent_AllBagsCountItems(pEnt, pchItem) : 0;
}

// Return the value of this numeric item the player has.
AUTO_EXPR_FUNC(Emote) ACMD_NAME(NumericItem);
S32 emoteExprNumericItem(ACMD_EXPR_SELF Entity *pEnt, ACMD_EXPR_RES_DICT(ItemDef) const char *pchItem)
{
	return pEnt ? inv_GetNumericItemValue(pEnt, pchItem) : 0;
}

// Check the player's access level.
AUTO_EXPR_FUNC(Emote) ACMD_NAME(AccessLevel);
S32 emoteExprAccessLevel(ACMD_EXPR_SELF Entity *pEnt)
{
	return pEnt ? entGetAccessLevel(pEnt) : -1;
}

AUTO_EXPR_FUNC(Emote) ACMD_NAME(GAD_AttribValue);
S32 emoteExprGAD_AttribValue(ACMD_EXPR_SELF Entity *pEnt, const char *pchAttrib)
{
	if(pEnt && pEnt->pPlayer)
	{
		GameAccountData *pData = entity_GetGameAccount(pEnt);
		return ( gad_GetAttribInt(pData, pchAttrib) );
	}
	return 0;
}

AUTO_STRUCT;
typedef struct EmoteDef
{
	const char *pchName; AST(POOL_STRING KEY STRUCTPARAM)
	REF_TO(Message) hDisplayName; AST(NAME(DisplayName))
	REF_TO(Message) hDescription; AST(NAME(Description))
	REF_TO(Message) hFailsRequirements;	AST(NAME(FailsRequirements))
	EmoteAnimList **eaAnimList; AST(NAME(AnimList) NAME(AnimLists))
	EmoteMessage **eaSay; AST(NAME(Say))
	EmoteMessage **eaTargetSay; AST(NAME(TargetSay))
	EmoteMessage **eaAction; AST(NAME(Action))
	EmoteMessage **eaTargetAction; AST(NAME(TargetAction))
	EmoteSubemote **eaEmotes; AST(NAME(Emote) NAME(Emotes))
	EmoteAnimBits **eaHeldBits; AST(NAME(HeldBit) NAME(HeldBits) NAME(HeldAnimBit) NAME(HeldAnimBits))
	EmoteAnimBits **eaFlashBits; AST(NAME(FlashBit) NAME(FlashBits) NAME(FlashAnimBit) NAME(FlashAnimBits))
	Expression *pRequires; AST(NAME(RequiresBlock) REDUNDANT_STRUCT(Requires, parse_Expression_StructParam) LATEBIND)
	U8 bDebug : 1; AST(NAME(Debug))
	const char *pchFilename; AST(CURRENTFILE)
} EmoteDef;

static DictionaryHandle s_EmoteDict;

static int EmoteValidate(enumResourceEventType eType, const char *pDictName, const char *pchEmote, EmoteDef *pEmote, U32 iUserID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		{
			S32 i;
			if (pEmote->eaEmotes && (pEmote->eaSay || pEmote->eaFlashBits || pEmote->eaAnimList))
				InvalidDataErrorf("%s: Emotes must have either subemotes or a message/animation, not both.", pEmote->pchName);
			else if (pEmote->eaFlashBits && pEmote->eaAnimList)
				InvalidDataErrorf("%s: Emotes must have either a bit list or an anim list, not both.", pEmote->pchName);

			if (IS_HANDLE_ACTIVE(pEmote->hDisplayName) && !GET_REF(pEmote->hDisplayName))
				InvalidDataErrorf("%s: Invalid display name %s.", pEmote->pchName, REF_STRING_FROM_HANDLE(pEmote->hDisplayName));
			if (IS_HANDLE_ACTIVE(pEmote->hDescription) && !GET_REF(pEmote->hDescription))
				InvalidDataErrorf("%s: Invalid description %s.", pEmote->pchName, REF_STRING_FROM_HANDLE(pEmote->hDisplayName));
			if (IS_HANDLE_ACTIVE(pEmote->hFailsRequirements) && !GET_REF(pEmote->hFailsRequirements))
				InvalidDataErrorf("%s: Invalid fails requirements message %s.", pEmote->pchName, REF_STRING_FROM_HANDLE(pEmote->hFailsRequirements));

			for (i = 0; i < eaSize(&pEmote->eaSay); i++)
			{
				EmoteMessage* pSay = pEmote->eaSay[i];
				if (!GET_REF(pSay->hMessage))
					InvalidDataErrorf("%s: Invalid Say message %s.", pEmote->pchName, REF_STRING_FROM_HANDLE(pSay->hMessage));
				if (pSay->eArgType != EmoteMessageArgType_None && !pSay->pExprMsgArg)
				{
					InvalidDataErrorf("%s: Invalid Say message %s. A message argument type was specified (%s) without an argument expression.", 
						pEmote->pchName, REF_STRING_FROM_HANDLE(pSay->hMessage), StaticDefineIntRevLookup(EmoteMessageArgTypeEnum,pSay->eArgType));
				}
				else if (pSay->pExprMsgArg && !exprGenerate(pSay->pExprMsgArg, s_pEmoteContext))
				{
					InvalidDataErrorf("%s: Error compiling message argument expression for %s.", pEmote->pchName, REF_STRING_FROM_HANDLE(pSay->hMessage));
				}
			}
			for (i = 0; i < eaSize(&pEmote->eaEmotes); i++)
				if (!GET_REF(pEmote->eaEmotes[i]->hEmote))
					InvalidDataErrorf("%s: Subemote %s is not valid.", pEmote->pchName, REF_STRING_FROM_HANDLE(pEmote->eaEmotes[i]->hEmote));

			for (i = 0; i < eaSize(&pEmote->eaAnimList); i++)
				if (!GET_REF(pEmote->eaAnimList[i]->hAnimList))
					InvalidDataErrorf("%s: Animlist %s is not valid.", pEmote->pchName, REF_STRING_FROM_HANDLE(pEmote->eaAnimList[i]->hAnimList));

			for (i = 0; i < eaSize(&pEmote->eaFlashBits); i++)
			{
				const char *pchLast = eaTail(&pEmote->eaFlashBits[i]->eaAnimBits);
				S32 j;
				ANALYSIS_ASSUME(pchLast != NULL); // I think this is correct.
				if (atoi(pchLast) > 0)
				{
					pEmote->eaFlashBits[i]->iChance = atoi(pchLast);
					eaPop(&pEmote->eaFlashBits[i]->eaAnimBits);
				}
				for (j = 0; j < eaSize(&pEmote->eaFlashBits[i]->eaAnimBits); j++)
				{
					if (!dynBitIsValidName(pEmote->eaFlashBits[i]->eaAnimBits[j]))
					{
						InvalidDataErrorf("%s: Bit name %s is not valid.", pEmote->pchName, pEmote->eaFlashBits[i]->eaAnimBits[j]);
					}
				}
			}
		}

		if (pEmote->pRequires && !exprGenerate(pEmote->pRequires, s_pEmoteContext))
			InvalidDataErrorf("%s: Error compiling requires expression.", pEmote->pchName);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

static void EmoteGetFailsRequirementsString(Entity *pEnt, EmoteDef *pDef, char** pestrMsg)
{
	if (pEnt && pDef)
	{
		Message* pMessage;
		Message* pDisplayMsg = GET_REF(pDef->hDisplayName);
		Language eLangID = entGetLanguage(pEnt);
		const char* pchDisplayName = langTranslateMessageDefault(eLangID, pDisplayMsg, pDef->pchName);

		// Get the emote-specific fails requirements message
		pMessage = GET_REF(pDef->hFailsRequirements);
		if (!pMessage)
		{
			// If there is no specific message for this emote, get the default fails requirements message
			pMessage = RefSystem_ReferentFromString("Message", EMOTE_DEFAULT_CANNOT_USE_MSG);
		}
		
		if (pMessage)
		{
			entFormatGameMessage(pEnt, pestrMsg, pMessage, STRFMT_STRING("Name", pchDisplayName), STRFMT_END);
		}
	}
}

static void EmoteNotifyCannotUse(Entity *pEnt, EmoteDef *pDef)
{
	char* estrMsg = NULL;

	estrStackCreate(&estrMsg);
	EmoteGetFailsRequirementsString(pEnt, pDef, &estrMsg);

	if (estrMsg && estrMsg[0])
	{
		notify_NotifySend(pEnt, kNotifyType_CannotUseEmote, estrMsg, NULL, NULL);
	}
	estrDestroy(&estrMsg);
}

static bool EmoteCheckDebug(Entity *pEnt, EmoteDef *pDef)
{
	if (pDef->bDebug)
	{
		return entGetAccessLevel(pEnt) >= ACCESS_DEBUG;
	}
	return true;
}

static bool EmoteCheckRequires(Entity *pEnt, EmoteDef *pDef)
{
	if (pDef->pRequires)
	{
		MultiVal mv;
		exprContextSetSelfPtr(s_pEmoteContext, pEnt);
		exprEvaluate(pDef->pRequires, s_pEmoteContext, &mv);
		return !!mv.intval;
	}
	return true;
}

static EmoteDef *EmoteCanUse(Entity *pEnt, EmoteDef *pDef)
{
	if (!pEnt || !pDef)
	{
		return NULL;
	}
	if (!EmoteCheckDebug(pEnt, pDef) || !EmoteCheckRequires(pEnt, pDef))
	{
		return NULL;
	}
	return pDef;
}

static const char* EmoteNameList_GetNextCB(NameList* pNameList, void* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pNameList->erClientEntity);
	EmoteDef* pEmote = (EmoteDef*)RefSystem_GetNextReferentFromIterator(&s_EmoteDictIterator);
	if (pEmote)
	{
		if (EmoteCanUse(pEnt, pEmote))
		{
			return pEmote->pchName;
		}
		return EmoteNameList_GetNextCB(pNameList, pData);
	}
	return NULL;
}

static void EmoteNameList_ResetCB(NameList* pNameList, void* pData)
{
	RefSystem_InitRefDictIterator(s_EmoteDict, &s_EmoteDictIterator);
}

AUTO_STARTUP(Emotes) ASTRT_DEPS(AS_Messages, AnimLists, Items, Powers, Missions);
void gslEmotesLoad(void)
{
	ExprFuncTable* stFuncs = exprContextCreateFunctionTable();
	s_pEmoteContext = exprContextCreate();
	exprContextAddFuncsToTableByTag(stFuncs, "Emote");
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextSetFuncTable(s_pEmoteContext, stFuncs);
	exprContextSetAllowRuntimeSelfPtr(s_pEmoteContext);

	s_EmoteDict = RefSystem_RegisterSelfDefiningDictionary("EmoteDef", false, parse_EmoteDef, true, true, NULL);
	resDictManageValidation(s_EmoteDict, EmoteValidate);
	resLoadResourcesFromDisk(s_EmoteDict, "defs/emotes", ".emote", NULL, PARSER_SERVERSIDE | PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	RefSystem_InitRefDictIterator(s_EmoteDict, &s_EmoteDictIterator);
	g_pEmoteNameList = CreateNameList_Callbacks(EmoteNameList_GetNextCB, EmoteNameList_ResetCB, NULL, NULL);
	g_pEmoteNameList->bUpdateClientDataPerRequest = true;
}

static const char* EntityEmote_FormatMessage(Entity* pEnt, Entity* pTarget, const EmoteMessage* pEmoteMessage)
{
	static char* s_pchSay = NULL;
	estrClear(&s_pchSay);
	
	if (pEmoteMessage)
	{
		Message* pMessage = GET_REF(pEmoteMessage->hMessage);
		if (pEmoteMessage->eArgType == EmoteMessageArgType_None)
		{
			entFormatGameMessage(pEnt, &s_pchSay, pMessage, 
				STRFMT_PLAYER(pEnt), 
				STRFMT_TARGET(pTarget), 
				STRFMT_END);
		}
		else if (pEmoteMessage->pExprMsgArg)
		{
			MultiVal mv;
			exprContextSetSelfPtr(s_pEmoteContext, pEnt);
			exprEvaluate(pEmoteMessage->pExprMsgArg, s_pEmoteContext, &mv);
			switch (pEmoteMessage->eArgType)
			{
				xcase EmoteMessageArgType_Int:
					entFormatGameMessage(pEnt, &s_pchSay, pMessage, 
						STRFMT_PLAYER(pEnt), 
						STRFMT_TARGET(pTarget), 
						STRFMT_INT("Value", MultiValGetInt(&mv,NULL)),
						STRFMT_END);
				xcase EmoteMessageArgType_Float:
					entFormatGameMessage(pEnt, &s_pchSay, pMessage, 
						STRFMT_PLAYER(pEnt), 
						STRFMT_TARGET(pTarget), 
						STRFMT_FLOAT("Value", MultiValGetFloat(&mv,NULL)),
						STRFMT_END);
				xcase EmoteMessageArgType_String:
					entFormatGameMessage(pEnt, &s_pchSay, pMessage, 
						STRFMT_PLAYER(pEnt), 
						STRFMT_TARGET(pTarget), 
						STRFMT_STRING("String", MultiValGetString(&mv,NULL)),
						STRFMT_END);
			}
		}
	}
	return s_pchSay;
}

static const EmoteAnimBits* pickRandomBits(const EmoteAnimBits*const* ea){
	const EmoteAnimBits*	b = NULL;
	S32						iSum;
	S32						i;
	S32						iChoice;
	
	for (iSum = 0, i = 0; i < eaSize(&ea); i++)
		iSum += ea[i]->iChance;
	iChoice = randInt(iSum);
	for (iSum = 0, i = 0; iSum <= iChoice && i < eaSize(&ea); i++)
	{
		b = ea[i];
		iSum += b->iChance;
	}

	return b;
}

static void EntityEmote(Entity *pEnt, EmoteDef *pDef, bool bShowText)
{
	Entity *pTarget = entity_GetTarget(pEnt);
	EntityRef hTarget = pTarget ? entGetRef(pTarget) : 0;
	bool bUpdateTime = false;
	S32 iSum = 0;
	S32 iChoice;
	S32 i;
	S32 j;

	if (!(pEnt && pDef))
		return;
	// Randomly choose a subemote
	else if (eaSize(&pDef->eaEmotes))
	{
		EmoteSubemote *pSub = NULL;
		for (iSum = 0, i = 0; i < eaSize(&pDef->eaEmotes); i++)
			iSum += pDef->eaEmotes[i]->iChance;
		iChoice = randInt(iSum);
		for (iSum = 0, j = 0; iSum <= iChoice && j < eaSize(&pDef->eaEmotes); j++)
		{
			pSub = pDef->eaEmotes[j];
			iSum += pSub->iChance;
		}
		if (pSub)
			EntityEmote(pEnt, GET_REF(pSub->hEmote), bShowText);
	}
	else
	{
		const EmoteMessage *pEmoteMessage = NULL;
		const EmoteMessage *pEmoteAction = NULL;
		const EmoteMessage *const*eaAction = NULL;
		const EmoteMessage *const*eaSay = NULL;
		U32 uiTimeNow = timeSecondsSince2000();

		if (pDef->eaFlashBits ||
			pDef->eaHeldBits)
		{
			const EmoteAnimBits* flashBits = pickRandomBits(pDef->eaFlashBits);
			const EmoteAnimBits* heldBits = pickRandomBits(pDef->eaHeldBits);
			
			if(	flashBits ||
				heldBits)
			{
				if(!g_CombatConfig.bEmotesUseRequester)
				{
					if(flashBits){
						character_FlashBitsOn(	pEnt->pChar,
												-1,
												0,
												kPowerAnimFXType_None,
												entGetRef(pEnt),
												flashBits->eaAnimBits,
												pmTimestamp(0),
												false,
												false,
												false,
												false);
					}
				}
				else
				{
					MREmoteSet* set = StructCreate(parse_MREmoteSet);

					set->flags.destroyOnMovement = 1;
					
					if(!pEnt->mm.mrEmote){
						gslEntMovementCreateEmoteRequester(pEnt);
					}
					
					if(heldBits){
						EARRAY_CONST_FOREACH_BEGIN(heldBits->eaAnimBits, k, ksize);
						{
							eaiPush(&set->flashAnimBitHandles,
									mmGetAnimBitHandleByName(heldBits->eaAnimBits[k], 0));
						}
						EARRAY_FOREACH_END;
					}
					
					if(flashBits){
						EARRAY_CONST_FOREACH_BEGIN(flashBits->eaAnimBits, k, ksize);
						{
							eaiPush(&set->flashAnimBitHandles,
									mmGetAnimBitHandleByName(flashBits->eaAnimBits[k], 0));
						}
						EARRAY_FOREACH_END;
					}

					mrEmoteSetDestroy(pEnt->mm.mrEmote, &pEnt->mm.mrEmoteSetHandle);
					mrEmoteSetCreate(pEnt->mm.mrEmote, &set, &pEnt->mm.mrEmoteSetHandle);
				}
			}
		}
		else if (pDef->eaAnimList)
		{
			// Randomly choose an animlist
			EmoteAnimList *pAnimList = NULL;
			for (iSum = 0, i = 0; i < eaSize(&pDef->eaAnimList); i++)
				iSum += pDef->eaAnimList[i]->iChance;
			iChoice = randInt(iSum);
			for (iSum = 0, j = 0; iSum <= iChoice && j < eaSize(&pDef->eaAnimList); j++)
			{
				pAnimList = pDef->eaAnimList[j];
				iSum += pAnimList->iChance;
			}
			if (pAnimList && GET_REF(pAnimList->hAnimList))
			{
				const AIAnimList* al = GET_REF(pAnimList->hAnimList);

				if(!g_CombatConfig.bEmotesUseRequester)
				{
					aiAnimListSetOneTickEx(pEnt, al, true);
				}
				else
				{
					MREmoteSet* set = StructAlloc(parse_MREmoteSet);
					
					set->flags.destroyOnMovement = 1;

					if(!pEnt->mm.mrEmote){
						gslEntMovementCreateEmoteRequester(pEnt);
					}

					if(gConf.bNewAnimationSystem){
						if(al->animKeyword){
							eaiPush(&set->flashAnimBitHandles, mmGetAnimBitHandleByName(al->animKeyword, 1));
						}
					}else{
						EARRAY_CONST_FOREACH_BEGIN(al->bits, k, ksize);
						{
							eaiPush(&set->flashAnimBitHandles, mmGetAnimBitHandleByName(al->bits[k], 1));
						}
						EARRAY_FOREACH_END;
					}

					EARRAY_CONST_FOREACH_BEGIN(al->FX, k, ksize);
					{
						MREmoteFX* fx = StructCreate(parse_MREmoteFX);

						fx->name = al->FX[k];
						fx->isMaintained = 1;
						eaPush(&set->fx, fx);
					}
					EARRAY_FOREACH_END;
					
					EARRAY_CONST_FOREACH_BEGIN(al->FlashFX, k, ksize);
					{
						MREmoteFX* fx = StructCreate(parse_MREmoteFX);

						fx->name = al->FlashFX[k];
						eaPush(&set->fx, fx);
					}
					EARRAY_FOREACH_END;

					mrEmoteSetDestroy(pEnt->mm.mrEmote, &pEnt->mm.mrEmoteSetHandle);
					mrEmoteSetCreate(pEnt->mm.mrEmote, &set, &pEnt->mm.mrEmoteSetHandle);
				}
			}
		}

		if (pTarget)
		{
			eaSay = pDef->eaTargetSay;
			eaAction = pDef->eaTargetAction;
		}
		if (!eaSay)
			eaSay = pDef->eaSay;
		if (!eaAction)
			eaAction = pDef->eaAction;

		// Randomly (independently of animation) choose a message
		for (iSum = 0, i = 0; i < eaSize(&eaSay); i++)
			iSum += eaSay[i]->iChance;
		iChoice = randInt(iSum + 1);
		for (iSum = 0, j = 0; iSum <= iChoice && j < eaSize(&eaSay); j++)
		{
			pEmoteMessage = eaSay[j];
			iSum += pEmoteMessage->iChance;
		}

		// Randomly (independently of animation) choose an action
		for (iSum = 0, i = 0; i < eaSize(&eaAction); i++)
			iSum += eaAction[i]->iChance;
		iChoice = randInt(iSum + 1);
		for (iSum = 0, j = 0; iSum <= iChoice && j < eaSize(&eaAction); j++)
		{
			pEmoteAction = eaAction[j];
			iSum += pEmoteAction->iChance;
		}

		if (!pTarget)
			pTarget = pEnt;

		if (pEmoteMessage
			&& bShowText
			&& pEnt->pPlayer->pUI
			&& (pDef->pchName != pEnt->pPlayer->pUI->pchLastEmote
				|| hTarget != pEnt->pPlayer->pUI->hLastEmoteTarget
				|| uiTimeNow - pEnt->pPlayer->pUI->uiLastEmoteTime > EMOTE_CHAT_RATE_LIMIT))
		{
			const char* pchString = EntityEmote_FormatMessage(pEnt, pTarget, pEmoteMessage);

			if (pchString && *pchString) {
				ChatData *pData = StructCreate(parse_ChatData);
				ServerChat_SendEmoteChatMsg(pEnt, pchString, pData);
				StructDestroy(parse_ChatData, pData);
				bUpdateTime = true;
			}
		}

		if (pEmoteAction
			&& bShowText
			&& pEnt->pPlayer->pUI
			&& (pDef->pchName != pEnt->pPlayer->pUI->pchLastEmote
				|| hTarget != pEnt->pPlayer->pUI->hLastEmoteTarget
				|| uiTimeNow - pEnt->pPlayer->pUI->uiLastEmoteTime > EMOTE_CHAT_RATE_LIMIT))
		{
			const char* pchString = EntityEmote_FormatMessage(pEnt, pTarget, pEmoteAction);

			if (pchString && *pchString) {
				ChatData *pData = StructCreate(parse_ChatData);
				pData->bEmote = true;
				ServerChat_SendEmoteChatMsg(pEnt, pchString, pData);
				StructDestroy(parse_ChatData, pData);
				bUpdateTime = true;
			}
		}

		if (pEnt->pPlayer && pEnt->pPlayer->pUI && bUpdateTime)
		{
			pEnt->pPlayer->pUI->pchLastEmote = pDef->pchName;
			pEnt->pPlayer->pUI->hLastEmoteTarget = hTarget;
			pEnt->pPlayer->pUI->uiLastEmoteTime = timeSecondsSince2000();
		}

		eventsend_Emote(pEnt, pDef->pchName);
	}
}

static EmoteDef *EmoteFindTranslated(Language eLang, const char *pchEmote)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(s_EmoteDict);
	S32 i;
	for (i = 0; i < eaSize(&pStruct->ppReferents); i++)
	{
		EmoteDef *pDef = pStruct->ppReferents[i];
		Message *pMessage;
		if (pDef && (pMessage = GET_REF(pDef->hDisplayName)))
		{
			const char *pchString = langTranslateMessage(eLang, pMessage);
			if (pchString && !stricmp(pchEmote, pchString))
				return pDef;
		}
	}
	return NULL;
}

static void EmoteFindSuggestions(CmdContext *pContext, Entity *pEnt, const char *pchName)
{
	EmoteDef *pEmote = NULL;
	const char **ppchOut = NULL;
	RefSystem_GetSimilarNames(s_EmoteDict, pchName, 10, &ppchOut);

	while (!pEmote && eaSize(&ppchOut))
		pEmote = EmoteCanUse(pEnt, RefSystem_ReferentFromString(s_EmoteDict, eaRemove(&ppchOut, 0)));

	if (pEmote)
		entFormatGameMessageKey(pEnt, pContext->output_msg, EMOTE_UNKNOWN_MSG, STRFMT_STRING("alternative", pEmote->pchName), STRFMT_STRING("emote", pchName), STRFMT_END);
	else
	{
		// if we couldn't find anything we could execute in the first 10 possible corrections, just grab something.
		// otherwise people can tell by the error message if there's a secret emote.
		DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(s_EmoteDict);
		S32 i;
		for (i = 0; !pEmote && i < eaSize(&pStruct->ppReferents); i++)
			pEmote = EmoteCanUse(pEnt, pStruct->ppReferents[i]);
		if (pEmote)
			entFormatGameMessageKey(pEnt, pContext->output_msg, "Emote_Unknown", STRFMT_STRING("alternative", pEmote->pchName), STRFMT_STRING("emote", pchName), STRFMT_END);
		// if they can't execute *ANY* emotes, silently fail, this should never happen.
	}
	eaDestroy(&ppchOut);
}


// Emote, failing if a preset emote is not found.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_NAME(emote);
void emote_preset(CmdContext *pContext, Entity *pEnt, ACMD_NAMELIST(g_pEmoteNameList) const char *pchName)
{
	EmoteDef *pEmote = RefSystem_ReferentFromString(s_EmoteDict, pchName);
	if (!(pEnt && pEnt->pChar))
		return;

	if (!pEmote)
		pEmote = EmoteFindTranslated(entGetLanguage(pEnt), pchName);

	if (pEmote && !EmoteCanUse(pEnt, pEmote))
	{
		if (EmoteCheckDebug(pEnt, pEmote))
		{
			EmoteNotifyCannotUse(pEnt, pEmote);
		}
		return;
	}
	if (pEmote)
		EntityEmote(pEnt, pEmote, true);
	else
		EmoteFindSuggestions(pContext, pEnt, pchName);
}

// Emote, using a plain text string if the emote is not found.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_NAME(em, me, e);
void emote_raw(CmdContext *pContext, Entity *pEnt, ACMD_NAMELIST(g_pEmoteNameList) ACMD_SENTENCE pchName)
{
	EmoteDef *pEmote;
	
	// ACMD_SENTENCE doesn't strip whitespace by default.
	removeLeadingAndFollowingSpaces(pchName);
	pEmote = RefSystem_ReferentFromString(s_EmoteDict, pchName);
	if (!(pEnt && pEnt->pChar))
		return;

	if (!pEmote)
		pEmote = EmoteFindTranslated(entGetLanguage(pEnt), pchName);

	if (pEmote && !EmoteCanUse(pEnt, pEmote))
	{
		if (EmoteCheckDebug(pEnt, pEmote))
		{
			EmoteNotifyCannotUse(pEnt, pEmote);
		}
		return;
	}
	if (pEmote)
		EntityEmote(pEnt, pEmote, true);
	else
	{
		ChatData *pData = StructCreate(parse_ChatData);
		pData->bEmote = true;
		ServerChat_SendEmoteChatMsg(pEnt, pchName, pData);
		StructDestroy(parse_ChatData, pData);
	}
}

// Emote, but without text.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void emote_notext(CmdContext *pContext, Entity *pEnt, ACMD_NAMELIST(g_pEmoteNameList) const char *pchName)
{
	EmoteDef *pEmote = RefSystem_ReferentFromString(s_EmoteDict, pchName);
	if (!(pEnt && pEnt->pChar))
		return;

	if (!pEmote)
		pEmote = EmoteFindTranslated(entGetLanguage(pEnt), pchName);

	if (pEmote && !EmoteCanUse(pEnt, pEmote))
	{
		if (EmoteCheckDebug(pEnt, pEmote))
		{
			EmoteNotifyCannotUse(pEnt, pEmote);
		}
		return;
	}
	if (pEmote)
		EntityEmote(pEnt, pEmote, false);
	else
		EmoteFindSuggestions(pContext, pEnt, pchName);
}

// Get the list of emotes that the player may use
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void emote_GetValidEmotes(CmdContext *pContext, Entity *pEnt)
{
	EmoteList *pEmotes = StructCreate(parse_EmoteList);
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(s_EmoteDict);
	S32 i;
	for (i = 0; i < eaSize(&pStruct->ppReferents); i++)
	{
		EmoteDef *pEmoteDef = pStruct->ppReferents[i];
		if (EmoteCanUse(pEnt, pEmoteDef))
		{
			Emote *pEmote = StructCreate(parse_Emote);
			pEmote->bCanUse = true;
			estrCopy2(&pEmote->estrName, TranslateMessageRef(pEmoteDef->hDisplayName));
			estrCopy2(&pEmote->estrEmoteKey, pEmoteDef->pchName);
			estrCopy2(&pEmote->estrDescription, TranslateMessageRef(pEmoteDef->hDescription));
			eaPush(&pEmotes->eaEmotes, pEmote);
		}
	}
	ClientCmd_emote_ReceiveEmotes(pEnt, pEmotes);
	StructDestroySafe(parse_EmoteList, &pEmotes);
}

// Get a list of all emotes (doesn't include debug emotes if the player's AL is < 9)
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void emote_GetAllEmotes(CmdContext *pContext, Entity *pEnt)
{
	EmoteList *pEmotes = StructCreate(parse_EmoteList);
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(s_EmoteDict);
	S32 i;
	for (i = 0; i < eaSize(&pStruct->ppReferents); i++)
	{
		EmoteDef *pEmoteDef = pStruct->ppReferents[i];
		if (EmoteCheckDebug(pEnt, pEmoteDef))
		{
			Emote *pEmote = StructCreate(parse_Emote);
			pEmote->bCanUse = !!EmoteCanUse(pEnt, pEmoteDef);
			if (!pEmote->bCanUse)
			{
				char* estrMsg = NULL;
				estrStackCreate(&estrMsg);
				EmoteGetFailsRequirementsString(pEnt, pEmoteDef, &estrMsg);
				estrCopy2(&pEmote->estrFailsRequirements, estrMsg);
				estrDestroy(&estrMsg);
			}
			estrCopy2(&pEmote->estrName, TranslateMessageRef(pEmoteDef->hDisplayName));
			estrCopy2(&pEmote->estrEmoteKey, pEmoteDef->pchName);
			estrCopy2(&pEmote->estrDescription, TranslateMessageRef(pEmoteDef->hDescription));
			eaPush(&pEmotes->eaEmotes, pEmote);
		}
	}
	ClientCmd_emote_ReceiveEmotes(pEnt, pEmotes);
	StructDestroySafe(parse_EmoteList, &pEmotes);
}

#include "gslEmote_c_ast.c"
#include "EmoteCommon_h_ast.c"
