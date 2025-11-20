/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatCallbacks.h"
#include "cmdparse.h"
#include "Character.h"
#include "NotifyCommon.h"
#include "StringFormat.h"
#include "PowerActivation.h"
#include "PowersAutoDesc.h"
#include "CharacterAttribs.h"
#include "WorldGrid.h"
#include "Entity.h"
#include "Player.h"

#ifdef GAMECLIENT
#include "UIGen.h"
#endif

// General callbacks

CombatCBHandlePMEvent combatcbHandlePMEvent = NULL;
CombatCBCharacterCanPerceive combatcbCharacterCanPerceive = NULL;
CombatCBPredictAttribModDef combatcbPredictAttribModDef = NULL;
CombatCBCharacterPowersChanged combatcbCharacterPowersChanged = NULL;

// Handles the feedbacks for power activation failures
void character_ActivationFailureFeedback(ActivationFailureReason eReason, SA_PARAM_NN_VALID ActivationFailureParams *pParams)
{
	if (!gConf.bHideCombatMessages)
	{
		switch(eReason)
		{
// Add server side feedback below

#ifdef GAMESERVER
		case kActivationFailureReason_Cost:
			{

				Language eLang = locGetLanguage(getCurrentLocale());

				PowerDef * pDef = GET_REF(pParams->pPow->hDef);
				char const * pchAttrName = attrib_AutoDescName(POWERDEF_ATTRIBCOST(pDef),eLang);

				char *pchTemp = NULL;
				estrStackCreate(&pchTemp);
				FormatMessageKey(&pchTemp,"PowersMessage.Float.NotEnoughPower",STRFMT_STRING("Resource",pchAttrName),STRFMT_END);
				notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
				estrDestroy(&pchTemp);
			}
			break;
#endif

// Add client side feedback below

#ifdef GAMECLIENT
		case kActivationFailureReason_Cost:
			{
				Language eLang = entGetLanguage(pParams->pEnt);

				PowerDef * pDef = GET_REF(pParams->pPow->hDef);
				char const * pchAttrName = attrib_AutoDescName(POWERDEF_ATTRIBCOST(pDef),eLang);

				char *pchTemp = NULL;
				estrStackCreate(&pchTemp);
				FormatMessageKey(&pchTemp,"PowersMessage.Float.NotEnoughPower",STRFMT_STRING("Resource",pchAttrName),STRFMT_END);
				notify_NotifySend(pParams->pEnt, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
				estrDestroy(&pchTemp);
			}
			break;
		case kActivationFailureReason_TargetOutOfRange:
		case kActivationFailureReason_TargetOutOfRangeMin:
			{
				if(pParams->pEnt)
				{
					char *pchTemp = NULL;
					estrStackCreate(&pchTemp);
					FormatMessageKey(&pchTemp,"PowersMessage.Float.OutOfRange",STRFMT_STRING("Target",entGetLocalName(pParams->pEnt)),STRFMT_END);
					notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
					estrDestroy(&pchTemp);

#ifdef GAMECLIENT
					if (pParams->pEnt->pEntUI && pParams->pEnt->pEntUI->pGen)
					{
						// Send a message to the entity gen
						ui_GenSendMessage(pParams->pEnt->pEntUI->pGen,"TargetOutOfRange");
					}
#endif
				}
				else if(pParams->pNode)
				{
					WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pParams->pNode);

					if(pEntry)
					{
						char *pchTemp = NULL;
						estrStackCreate(&pchTemp);
						FormatMessageKey(&pchTemp,"PowersMessage.Float.OutOfRange",STRFMT_STRING("Target",TranslateMessagePtr(GET_REF(pEntry->base_interaction_properties->hDisplayNameMsg))),STRFMT_END);
						notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
						estrDestroy(&pchTemp);
					}
				}
			}
			break;
		case kActivationFailureReason_TargetNotInArc:
			{
				char *pchTemp = NULL;
				estrStackCreate(&pchTemp);
				FormatMessageKey(&pchTemp,"PowersMessage.Float.NotInArc",STRFMT_STRING("Target",entGetLocalName(pParams->pEnt)),STRFMT_END);
				notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
				estrDestroy(&pchTemp);

#ifdef GAMECLIENT
				if(pParams->pEnt && pParams->pEnt->pEntUI && pParams->pEnt->pEntUI->pGen)
				{
					// Send a message to the entity gen
					ui_GenSendMessage(pParams->pEnt->pEntUI->pGen,"TargetNotInArc");
				}

			}
#endif
			break;
		case kActivationFailureReason_TargetInvalid:
		case kActivationFailureReason_TargetImperceptible:	// Leave this the same as above since that's what it was
			{
				char *pchTemp = NULL;
				estrStackCreate(&pchTemp);
				if(pParams->pEnt)
					FormatMessageKey(&pchTemp,"PowersMessage.Float.Invalid",STRFMT_STRING("Target",entGetLocalName(pParams->pEnt)),STRFMT_END);
				else
					FormatMessageKey(&pchTemp,"PowersMessage.Float.Missing",STRFMT_END);
				notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
				estrDestroy(&pchTemp);
			}
			break;
		case kActivationFailureReason_TargetLOSFailed:
			{
				char *pchTemp = NULL;
				estrStackCreate(&pchTemp);
				if(pParams->pEnt)
					FormatMessageKey(&pchTemp,"PowersMessage.Float.LoSFailed",STRFMT_STRING("Target",entGetLocalName(pParams->pEnt)),STRFMT_END);
				else
					FormatMessageKey(&pchTemp,"PowersMessage.Float.Missing",STRFMT_END);
				notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
				estrDestroy(&pchTemp);

#ifdef GAMECLIENT
				if (pParams->pEnt && pParams->pEnt->pEntUI && pParams->pEnt->pEntUI->pGen)
				{
					// Send a message to the entity gen
					ui_GenSendMessage(pParams->pEnt->pEntUI->pGen,"TargetLOSFailed");
				}
#endif
			}
			break;
		case kActivationFailureReason_DoesNotHaveRequiredItemEquipped:
			{
				char *pchTemp = NULL;
				estrStackCreate(&pchTemp);
				FormatMessageKey(&pchTemp, "PowersMessage.Float.DoesNotHaveWeaponRequired", STRFMT_STRING("ItemCategory", pParams->pchStringParam), STRFMT_END);
				notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
				estrDestroy(&pchTemp);
			}
			break;
		case kActivationFailureReason_PowerModeDisallowsUsage:
			{
				notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, TranslateMessageKeySafe ("PowersMessage.Float.PowerModeDisallowsUse"), NULL, NULL);
			}
			break;

		case kActivationFailureReason_Other:
			{
				notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, TranslateMessageKeySafe ("PowersMessage.Float.GenericError"), NULL, NULL);
			}
			break;

		case kActivationFailureReason_Disabled:
			{
				const char *pchMessageKey = "PowersMessage.Float.Disabled";
				const char *pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);
				
				if (pchMessageKey != pchTranslatedMessage)
					notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTranslatedMessage, NULL, NULL);
			}
			break;
		case kActivationFailureReason_Knocked:
			{
				const char *pchMessageKey = "PowersMessage.Float.Knocked";
				const char *pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);

				if (pchMessageKey != pchTranslatedMessage)
					notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTranslatedMessage, NULL, NULL);
			}
			break;

		case kActivationFailureReason_NoChargesRemaining:
			{
				const char *pchMessageKey = "PowersMessage.Float.NoChargesRemaining";
				const char *pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);

				if (pchMessageKey != pchTranslatedMessage)
					notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTranslatedMessage, NULL, NULL);
			}
			break;

		case kActivationFailureReason_ReactivePowerDisallow:
			{
				const char *pchMessageKey = "PowersMessage.Float.DisabledInReactiveState";
				const char *pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);

				if (pchMessageKey != pchTranslatedMessage)
					notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTranslatedMessage, NULL, NULL);
			}
			break;

		case kActivationFailureReason_Rooted:
			{
				const char *pchMessageKey = "PowersMessage.Float.Rooted";
				const char *pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);

				if (pchMessageKey != pchTranslatedMessage)
					notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTranslatedMessage, NULL, NULL);
			}
			break;

		case kActivationFailureReason_RequiresQueueExpression:
			{
				const char *pchMessageKey = pParams->pchStringParam;
				const char *pchTranslatedMessage = NULL;
				if (!pchMessageKey)
					pchMessageKey = "PowersMessage.Float.RequiresQueueExpression";

				pchTranslatedMessage = TranslateMessageKey (pchMessageKey);
				if (pchMessageKey != pchTranslatedMessage)
				{
					notify_NotifySend(NULL, kNotifyType_PowerExecutionFailed, pchTranslatedMessage, NULL, NULL);
				}
			} 
			break;
#endif
		}
	}
}
