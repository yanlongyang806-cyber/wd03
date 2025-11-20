#include "chatCommonStructs.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntityResolver.h"
#include "EString.h"
#include "GameStringFormat.h"
#include "Guild.h"
#include "MemoryBudget.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "StringUtil.h"
#include "Team.h"

#ifdef GAMECLIENT
#include "chat/gclChatLog.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define LOCAL_RESOLVE_DIST 100.0f

#define STR_EXISTS(str) ((str) && (*str))
#define STR_IS_EMPTY(str) (!STR_EXISTS(str))

static void handleResult(Entity *pRequestor, EntityResolveReturn result, const char *pchLookupName) {
	char *pchError = NULL;
	switch (result) {
		case kEntityResolve_Ambiguous: 
			entFormatGameMessageKey(pRequestor, &pchError, "EntityResolve_Ambiguous", 
				STRFMT_STRING("CharacterName", pchLookupName),
				STRFMT_END);
			notify_NotifySend(pRequestor, kNotifyType_EntityResolve_Ambiguous, pchError, NULL, NULL); 
			break;
		case kEntityResolve_NotFound:
			entFormatGameMessageKey(pRequestor, &pchError, "EntityResolve_NotFound", 
				STRFMT_STRING("FullName", pchLookupName),
				STRFMT_END);
			notify_NotifySend(pRequestor, kNotifyType_EntityResolve_NotFound, pchError, NULL, NULL);
			break;
		default:
			// Do nothing
			break;
	}
}

static void constructFullName(SA_PRE_NN_NN_VALID char **ppchFullName, SA_PARAM_OP_VALID const char *pchCharacterName, SA_PARAM_OP_VALID const char *pchAccountName, bool bAddQuotesIfNeeded, bool bAddCommaIfNeeded, bool bAddTrailingSpace) {
	bool bAddComma = bAddCommaIfNeeded && pchAccountName && *pchAccountName;
	bool bAddQuotes = bAddQuotesIfNeeded && pchCharacterName && strchr(pchCharacterName, ' ');

	estrClear(ppchFullName);
	if (bAddQuotes && !bAddComma) {
		estrAppend2(ppchFullName, "\"");
	}

	if (pchCharacterName && *pchCharacterName) {
		estrAppend2(ppchFullName, pchCharacterName);
	}

	if (pchAccountName && *pchAccountName) {
		estrAppend2(ppchFullName, "@");
		estrAppend2(ppchFullName, pchAccountName);

		if (bAddQuotes && !bAddComma) {
			estrAppend2(ppchFullName, "\"");
		} else if (bAddComma) {
			estrAppend2(ppchFullName, ",");
		}

		// Note: We only add trailing spaces to the handle
		// portion because the user might want to type an '@'
		// and restart auto-completion.  This case only happens
		// (at this time) when users to chat auto completion
		// using text that doesn't include a command. (i.e. they
		// are typing a message to a channel using the current channel
		// selection).
		if (bAddTrailingSpace) {
			estrAppend2(ppchFullName, " ");
		}
	} else if (bAddQuotes && !bAddComma) {
		estrAppend2(ppchFullName, "\"");
	}
}

static bool entitySuggestionMatches(SA_PARAM_NN_STR const char *pchMatchCharacterName, SA_PARAM_NN_STR const char *pchMatchAccountName, SA_PARAM_OP_STR const char *pchExistingCharacterName, SA_PARAM_OP_STR const char *pchExistingAccountName, char **ppchResolvedFullName, char **ppchResolvedAccountName)
{
	// Account names take precedence over character names because they
	// are unique, whereas character names are not.

	if (STR_EXISTS(pchMatchAccountName) && STR_EXISTS(pchExistingAccountName)) {
		bool result = stricmp(pchMatchAccountName, pchExistingAccountName) == 0;
		if (result) {
			// Account name matched.  If a character name was also provided, then 
			// match that as well to get an exact match.
			if (STR_EXISTS(pchMatchCharacterName)) {
				if (STR_EXISTS(pchMatchCharacterName) && STR_EXISTS(pchExistingCharacterName)) {
					result = stricmp(pchMatchCharacterName, pchExistingCharacterName) == 0;
				} else {
					// If a character name was provided to match against, then we ONLY want
					// to match if we know for sure the returned entity will also match.
					result = false;
				}
			}

			if (result) {
				if (ppchResolvedFullName) {
					constructFullName(ppchResolvedFullName, pchExistingCharacterName, pchExistingAccountName, false, false, false);
				}
				if (ppchResolvedAccountName) {
					estrCopy2(ppchResolvedAccountName, pchExistingAccountName);
				}
			}
		}
		return result;
	}

	if (STR_EXISTS(pchMatchCharacterName) && STR_EXISTS(pchExistingCharacterName)) {
		bool result = stricmp(pchMatchCharacterName, pchExistingCharacterName) == 0;
		if (result) {
			if (ppchResolvedFullName) {
				constructFullName(ppchResolvedFullName, pchExistingCharacterName, pchExistingAccountName, false, false, false);
			}
			if (ppchResolvedAccountName) {
				estrCopy2(ppchResolvedAccountName, pchExistingAccountName);
			}
		}
		return result;
	}

	return false;
}

// Returns true if we should stop looking for a match
static bool analyzeKnownAccountIDResult(U32 iEntId, U32 iAccountId, U32 uiLoginServerID, const char *pchAccount, SA_PARAM_NN_VALID U32 *pResolvedEntId, SA_PARAM_NN_VALID U32 *pResolvedAccountId, SA_PARAM_NN_VALID U32 *puiResolvedLoginServerID, char **ppchResolvedFullName, char **ppchResolvedAccountName, SA_PARAM_NN_VALID EntityResolveReturn *pResult) {
	// If pchAccount is set, then we're only looking for account matches.
	// If we had success using an account, then we have a hit.
	// Otherwise, we're searching for a potentially ambiguous character
	// name.  If it's ambiguous (meaning we've found more than one
	// with the same name), then return that fact.  If not, then 
	// update the entID & accountID and continue searching for 
	// potentially ambiguous character name matches.
	if (STR_EXISTS(pchAccount)) {
		*pResolvedEntId = iEntId;
		*pResolvedAccountId = iAccountId;
		*puiResolvedLoginServerID = uiLoginServerID;
		*pResult = kEntityResolve_Success;
		return true;
	} else {
		if (pResolvedEntId && *pResolvedEntId && *pResolvedEntId != iEntId) {
			*pResolvedEntId = 0;
			*pResolvedAccountId = 0;
			*puiResolvedLoginServerID = 0;
			estrClear(ppchResolvedFullName);
			estrClear(ppchResolvedAccountName);
			*pResult = kEntityResolve_Ambiguous;
			return true;
		}
		*pResolvedEntId = iEntId;
		*pResolvedAccountId = iAccountId;
		*puiResolvedLoginServerID = uiLoginServerID;
	}

	return false;
}

#if defined(GAMECLIENT) && defined(USE_CHATRELAY)
extern ChatState g_ChatState;
#endif

EntityResolveReturn ResolveKnownAccountID(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId, U32 *pResolvedAccountID, U32 *puiLoginServerID)
{
	Entity *pEnt;
	Team *pTeam;
	static char *pchCharacter = NULL;
	const char *pchAccount;
	EntityResolveReturn result;
	U32 uEntID, uAccountID, uiLoginServerID;

#if !defined(GAMECLIENT) || !defined(USE_CHATRELAY)
	// Valid for GameClient to pass in no Entity (entActivePlayerPtr) here
	if (!pRequestor)
		return kEntityResolve_NotFound;
#endif
	if (!pchLookupName || !*pchLookupName)
		return kEntityResolve_NotFound;

	// Initialize the results
	if (!pResolvedEntId)
		pResolvedEntId = &uEntID;
	if (!pResolvedAccountID)
		pResolvedAccountID = &uAccountID;
	if (!puiLoginServerID)
	{
		puiLoginServerID = &uiLoginServerID;
	}

	*pResolvedEntId = 0;
	*pResolvedAccountID = 0;
	*puiLoginServerID = 0;
	if (ppchResolvedFullName)
		estrClear(ppchResolvedFullName);
	if (ppchResolvedAccountName)
		estrClear(ppchResolvedAccountName);
	estrClear(&pchCharacter);

	pchAccount = strchr(pchLookupName, '@');
	if (pchAccount == pchLookupName) {
		pchAccount++; // Don't include @
	} else if (pchAccount) {
		estrConcat(&pchCharacter, pchLookupName, pchAccount-pchLookupName);
		pchAccount++; // Don't include @
	} else {
		estrCopy2(&pchCharacter, pchLookupName);
	}

	if (STR_IS_EMPTY(pchCharacter) && STR_IS_EMPTY(pchAccount)) {
		return kEntityResolve_NotFound;
	}

	// Look at teammates
	if (pRequestor)
	{
		pTeam = pRequestor->pTeam ? GET_REF(pRequestor->pTeam->hTeam) : NULL;
		if (pTeam && pRequestor->pTeam->eState == TeamState_Member)
		{
			EARRAY_FOREACH_BEGIN(pTeam->eaMembers, i);
			{
				TeamMember *pMember = pTeam->eaMembers[i];
				pEnt = GET_REF(pMember->hEnt);
				if (pEnt && pEnt->pPlayer &&
					entitySuggestionMatches(pchCharacter, pchAccount, entGetLocalName(pEnt), pEnt->pPlayer->publicAccountName, ppchResolvedFullName, ppchResolvedAccountName) &&
					analyzeKnownAccountIDResult(entGetContainerID(pEnt), pEnt->pPlayer->accountID, 0, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) 
				{
					return result;
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	// Look at friends
#if defined(GAMECLIENT) && defined(USE_CHATRELAY)
	EARRAY_FOREACH_BEGIN(g_ChatState.eaFriends, i);
	{
		ChatPlayerStruct *pFriend = g_ChatState.eaFriends[i];
		if (pFriend && pFriend->pPlayerInfo.onlineCharacterID &&
			entitySuggestionMatches(pchCharacter, pchAccount, pFriend->pPlayerInfo.onlinePlayerName, pFriend->chatHandle, ppchResolvedFullName, ppchResolvedAccountName) &&
			analyzeKnownAccountIDResult(pFriend->pPlayerInfo.onlineCharacterID, pFriend->accountID, pFriend->pPlayerInfo.uLoginServerID, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) 
		{
			return result;
		}
	}
	EARRAY_FOREACH_END;
#else
	EARRAY_FOREACH_BEGIN(pRequestor->pPlayer->pUI->pChatState->eaFriends, i);
	{
		ChatPlayerStruct *pFriend = pRequestor->pPlayer->pUI->pChatState->eaFriends[i];
		if (pFriend && pFriend->pPlayerInfo.onlineCharacterID &&
			entitySuggestionMatches(pchCharacter, pchAccount, pFriend->pPlayerInfo.onlinePlayerName, pFriend->chatHandle, ppchResolvedFullName, ppchResolvedAccountName) &&
			analyzeKnownAccountIDResult(pFriend->pPlayerInfo.onlineCharacterID, pFriend->accountID, pFriend->pPlayerInfo.uLoginServerID, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) 
		{
			return result;
		}
	}
	EARRAY_FOREACH_END;
#endif

	// Look at guild mates
	if (pRequestor && pRequestor->pPlayer->pGuild)
	{
		Guild *pGuild = GET_REF(pRequestor->pPlayer->pGuild->hGuild);
		if (pGuild)
		{
			EARRAY_FOREACH_BEGIN(pGuild->eaMembers, i);
			{
				GuildMember *pMember = pGuild->eaMembers[i];
				const char *pchMemberAccount = pMember->pcAccount;
				if (pMember && pchMemberAccount && *pchMemberAccount == '@') {
					pchMemberAccount++;
				}

				if (entitySuggestionMatches(pchCharacter, pchAccount, pMember->pcName, pchMemberAccount, ppchResolvedFullName, ppchResolvedAccountName)) {
					if (analyzeKnownAccountIDResult(pMember->iEntID, pMember->iAccountID, 0, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) {
						return result;
					}
				}
			}
			EARRAY_FOREACH_END;
		}
	}

#ifdef GAMECLIENT // List of recent receivers/senders only exists on client
	// Look at recent chat receivers (people you've sent messages to)
	{
		ChatUserInfo ***peaUserInfos = ChatLog_GetRecentChatReceivers();
		EARRAY_FOREACH_BEGIN(*peaUserInfos, i);
		{
			ChatUserInfo *pInfo = (*peaUserInfos)[i];
			if (entitySuggestionMatches(pchCharacter, pchAccount, pInfo->pchName, pInfo->pchHandle, ppchResolvedFullName, ppchResolvedAccountName)) {
				if (analyzeKnownAccountIDResult(pInfo->playerID, pInfo->accountID, 0, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) {
					return result;
				}
			}
		}
		EARRAY_FOREACH_END;

		// Look at recent chat senders (people you've received messages from)
		peaUserInfos = ChatLog_GetRecentChatSenders();
		EARRAY_FOREACH_BEGIN(*peaUserInfos, i);
		{
			ChatUserInfo *pInfo = (*peaUserInfos)[i];
			if (entitySuggestionMatches(pchCharacter, pchAccount, pInfo->pchName, pInfo->pchHandle, ppchResolvedFullName, ppchResolvedAccountName)) {
				if (analyzeKnownAccountIDResult(pInfo->playerID, pInfo->accountID, 0, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) {
					return result;
				}
			}
		}
		EARRAY_FOREACH_END;
	}
#endif //GAMECLIENT

#ifdef GAMECLIENT
	// Look for other local entities (includes self).
	// This includes all entities the client knows about which 
	// includes all local entities, plus recently local entities
	// depending on which entities the client as cached.
	{
		EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		while ((pEnt = EntityIteratorGetNext(pIter)))
		{
			if (pEnt && pEnt->pPlayer) {
				if (entitySuggestionMatches(pchCharacter, pchAccount, entGetLocalName(pEnt), pEnt->pPlayer->publicAccountName, ppchResolvedFullName, ppchResolvedAccountName)) {
					if (analyzeKnownAccountIDResult(entGetContainerID(pEnt), pEnt->pPlayer->accountID, 0, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) {
						EntityIteratorRelease(pIter);
						return result;
					}
				}
			}
		}

		EntityIteratorRelease(pIter);
	}
#else
	// Look for other local entities (includes self).
	// Since we're probably on the game server, we don't want ALL the 
	// entities, so just get those that are close by
	{
		Vec3 vPos;
		static Entity** eaEnts = NULL;
		int i;
		eaClear(&eaEnts);
		entGetPos(pRequestor, vPos);
		entGridProximityLookupExEArray(entGetPartitionIdx(pRequestor), vPos, &eaEnts, LOCAL_RESOLVE_DIST, 0, ENTITYFLAG_IGNORE, pRequestor);

		for(i=eaSize(&eaEnts)-1; i>=0; i--) {
			pEnt = eaEnts[i];
			if (pEnt && pEnt->pPlayer) {
				if (entitySuggestionMatches(pchCharacter, pchAccount, entGetLocalName(pEnt), pEnt->pPlayer->publicAccountName, ppchResolvedFullName, ppchResolvedAccountName)) {
					if (analyzeKnownAccountIDResult(entGetContainerID(pEnt), pEnt->pPlayer->accountID, 0, pchAccount, pResolvedEntId, pResolvedAccountID, puiLoginServerID, ppchResolvedFullName, ppchResolvedAccountName, &result)) {
						return result;
					}
				}
			}
		}
	}
#endif //GAMECLIENT

	return *pResolvedEntId ? kEntityResolve_Success : kEntityResolve_NotFound;
}

EntityResolveReturn ResolveKnownEntityID(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId)
{
	return ResolveKnownAccountID(pRequestor, pchLookupName, ppchResolvedFullName, ppchResolvedAccountName, pResolvedEntId, NULL, NULL);
}

EntityResolveReturn ResolveKnownAccount(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName)
{
	const char *pchAccountWithAt = NULL;
	U32 iEntID;

	if (!pchLookupName || !*pchLookupName) {
		return kEntityResolve_NotFound;
	}

	pchAccountWithAt = strchr(pchLookupName, '@');

	if (pchAccountWithAt) {
		if (ppchResolvedFullName) {
			estrCopy2(ppchResolvedFullName, pchLookupName);
		}
		if (ppchResolvedAccountName) {
			estrCopy2(ppchResolvedAccountName, pchAccountWithAt+1);
		}
		return kEntityResolve_Success;
	}

	return ResolveKnownEntityID(pRequestor, pchAccountWithAt, ppchResolvedFullName, ppchResolvedAccountName, &iEntID);
}

// See comment in header for expected behavior
Entity *ResolveKnownEntity(Entity *pRequestor, const char *pchLookupName) {
	Entity *pResolvedEnt = NULL;
	U32 iResolvedEntId=0;
	EntityResolveReturn result = ResolveKnownEntityID(pRequestor, pchLookupName, NULL, NULL, &iResolvedEntId);

	if (result == kEntityResolve_Success) {
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iResolvedEntId);
		pResolvedEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iResolvedEntId);
	}

	return pResolvedEnt;
}

EntityResolveReturn ResolveKnownAccountIDNotify(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId, U32 *pResolvedAccountID)
{
	EntityResolveReturn result = ResolveKnownAccountID(pRequestor, pchLookupName, ppchResolvedFullName, ppchResolvedAccountName, pResolvedEntId, pResolvedAccountID, NULL);
	handleResult(pRequestor, result, pchLookupName);
	return result;
}

EntityResolveReturn ResolveKnownEntityIDNotify(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId)
{
	EntityResolveReturn result = ResolveKnownEntityID(pRequestor, pchLookupName, ppchResolvedFullName, ppchResolvedAccountName, pResolvedEntId);
	handleResult(pRequestor, result, pchLookupName);
	return result;
}

EntityResolveReturn ResolveKnownAccountNotify(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName)
{
	EntityResolveReturn result = ResolveKnownAccount(pRequestor, pchLookupName, ppchResolvedFullName, ppchResolvedAccountName);
	handleResult(pRequestor, result, pchLookupName);
	return result;
}

Entity *ResolveKnownEntityNotify(Entity *pRequestor, const char *pchLookupName)
{
	Entity *pResolvedEnt = NULL;
	U32 iResolvedEntId=0;
	EntityResolveReturn result = ResolveKnownEntityIDNotify(pRequestor, pchLookupName, NULL, NULL, &iResolvedEntId);

	if (result == kEntityResolve_Success) {
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iResolvedEntId);
		pResolvedEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iResolvedEntId);
	}

	return pResolvedEnt;
}

bool ResolveNameOrAccountIDNotify(Entity *pEnt, const char *pchLookupName, char **ppchAccountName, U32 *piAccountId) {
	// Try to resolve the lookup name into an account ID.  Otherwise, check whether an @ appears in the name
	EntityResolveReturn result = ResolveKnownAccountID(pEnt, pchLookupName, NULL, ppchAccountName, NULL, piAccountId, NULL);
	switch (result) {
		case kEntityResolve_Ambiguous:
			{
				char *pchError = NULL;
				entFormatGameMessageKey(pEnt, &pchError, "EntityResolve_Ambiguous", STRFMT_STRING("CharacterName", pchLookupName), STRFMT_END);
				notify_NotifySend(pEnt, kNotifyType_EntityResolve_Ambiguous, pchError, NULL, NULL); 
				estrDestroy(&pchError);
				return false;
				break;
			}

		case kEntityResolve_NotFound:
			{
				const char *pchAt = pchLookupName ? strchr(pchLookupName, '@') : NULL;
				if (pchAt && *(pchAt+1)) {
					estrCopy2(ppchAccountName, pchAt+1);
					return true;
				} else {
					char *pchError = NULL;
					entFormatGameMessageKey(pEnt, &pchError, "EntityResolve_NotFound", STRFMT_STRING("FullName", pchLookupName), STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_EntityResolve_NotFound, pchError, NULL, NULL);
					estrDestroy(&pchError);
					return false;
				}
				break;
			}

		case kEntityResolve_Success:
			return true;
			break;
	}

	devassertmsgf(0, "Unexpected EntityResolveReturn result: %d", result);
	return false;
}
