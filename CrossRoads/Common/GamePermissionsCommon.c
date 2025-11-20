#include "GamePermissionsCommon.h"
#include "GamePermissionsCommon_h_ast.h"

#include "AutoTransDefs.h"
#include "error.h"
#include "Character.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameAccountData_h_ast.h"
#include "logging.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "referencesystem.h"
#include "StringCache.h"
#include "GamePermissionTransactions.h"
#include "inventoryCommon.h"
#include "CharacterClass.h"

#include "itemEnums_h_ast.h"

GamePermissionDefs g_GamePermissions = {0};
GamePermissionDef *g_pPremiumPermission = NULL;
GamePermissionDef *g_pBasePermission = NULL;

bool s_bPlayerConversionActive = false;

int g_bDebugF2P = false;
AUTO_CMD_INT(g_bDebugF2P, DebugF2P) ACMD_CMDLINE ACMD_ACCESSLEVEL(9);

int g_bDebugF2PPremium = false;
AUTO_CMD_INT(g_bDebugF2PPremium, DebugF2PPremium) ACMD_CMDLINE ACMD_ACCESSLEVEL(9);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_TRANS_HELPER_SIMPLE;
void GamePermissions_SetPlayerConversionState(bool bConversionActive)
{
	s_bPlayerConversionActive = bConversionActive;
}

//This function generates the game token key from the provided inputs
void GenerateGameTokenKey(char **estrBuffer, GameTokenType eType, const char *pchKey, const char *pchValue)
{
	estrPrintf(estrBuffer, "%s%s%s%s%s", 
		eType != kGameToken_None ? StaticDefineIntRevLookup(GameTokenTypeEnum, eType) : "",
		eType != kGameToken_None ? "." : "",
		NULL_TO_EMPTY(pchKey),
		pchKey && *pchKey ? "." : "",
		pchValue);
}

// Takes tokens from the given definition, and pushes them into the tokens earray.  Expects the tokens earray to be indexed
AUTO_TRANS_HELPER
ATR_LOCKS(peaTokens, ".*");
void gamePermission_trh_GetTokensEx(EARRAY_OF(GameTokenText) pDefTokens, ATH_ARG NOCONST(GameToken) ***peaTokens)
{
	S32 i;
	char *estrBuffer = NULL;

	estrStackCreate(&estrBuffer);

	for(i=eaSize(&pDefTokens)-1; i>=0; i--)
	{
		NOCONST(GameToken) *pToken = StructCreateNoConst(parse_GameToken);
		GenerateGameTokenKey(&estrBuffer,
			pDefTokens[i]->eType,
			pDefTokens[i]->pchKey,
			pDefTokens[i]->pchValue);
		pToken->pchKey = allocAddString( estrBuffer );
		eaIndexedAdd(peaTokens, pToken);
	}

	estrDestroy(&estrBuffer);
}

// Takes tokens from the given definition, and pushes them into the tokens earray.  Expects the tokens earray to be indexed
AUTO_TRANS_HELPER
ATR_LOCKS(peaTokens, ".*");
void gamePermission_trh_GetTokens(GamePermissionDef *pDef, ATH_ARG NOCONST(GameToken) ***peaTokens)
{
	gamePermission_trh_GetTokensEx(pDef->eaTextTokens, peaTokens);
}

void gamePermission_GetTokenKeys(GamePermissionDef *pDef, const char ***pppchTokens)
{
	S32 i;
	char *estrBuffer = NULL;
	
	estrStackCreate(&estrBuffer);

	for(i=eaSize(&pDef->eaTextTokens)-1; i>=0; i--)
	{
		GenerateGameTokenKey(&estrBuffer,
							 pDef->eaTextTokens[i]->eType,
							 pDef->eaTextTokens[i]->pchKey,
							 pDef->eaTextTokens[i]->pchValue);
		eaPushUnique(pppchTokens, allocAddString(estrBuffer));
	}
	estrDestroy(&estrBuffer);
}

S32 GamePermission_GADHasToken(GameAccountData *pAccountData, const char *pchToken)
{
	if(gamePermission_Enabled())
	{
		if(NONNULL(pAccountData))
		{
			return eaIndexedGetUsingString(&pAccountData->eaTokens, pchToken) != NULL;
		}
		// no game data
		return false;
	}
	return true;
}
// return true if the entity has this token, will return true if game permissions are off
AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(PermToken);
S32 GamePermission_EntHasToken(SA_PARAM_OP_VALID Entity *pEnt, const char *pchToken)
{
	if(gamePermission_Enabled())
	{
		GameAccountData *pData = entity_GetGameAccount(pEnt);
		if(NONNULL(pData))
		{
			return eaIndexedGetUsingString(&pData->eaTokens, pchToken) != NULL;
		}
		
		// no game data
		return false;
	}

	return true;
}

//Does this entity have that token, returns true if game permissions are off
S32 GamePermission_EntHasPermission(SA_PARAM_OP_VALID Entity *pEnt, const char *pchPermission)
{
	if(gamePermission_Enabled())
	{
		GameAccountData *pData = entity_GetGameAccount(pEnt);
		if(NONNULL(pData))
		{
			return eaIndexedGetUsingString(&pData->eaPermissions, pchPermission) != NULL;
		}

		// no game data
		return false;
	}

	return true;
}


// check for game permission token, if game permissions are off then it returns bTrueIfNoGamePermission
S32 GamePermission_ExtractHasToken(GameAccountDataExtract *pExtract, const char *pchToken, bool bTrueIfNoGamePermission)
{
	if(gamePermission_Enabled())
	{
		if(pExtract)
		{
			return eaIndexedGetUsingString(&pExtract->eaTokens, pchToken) != NULL;
		}
		
		// no game data
		return false;
	}

	// return last flag for when permission not enabled, most cases should use bTrueIfNoGamePermission == true
	return bTrueIfNoGamePermission;
}


AUTO_EXPR_FUNC(Player, mission) ACMD_NAME(PermTokenPlayer);
S32 GamePermission_player_EntHasToken(ExprContext *context, const char *pchToken)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	const GameAccountData *pData = entity_GetGameAccount(pEnt);
	if(pData)
	{
		return eaIndexedGetUsingString(&pData->eaTokens, pchToken) != NULL;
	}
	return false;
}

// Returns true or false if this GameAccount has the provided token.
//  Builds the token into estrBuffer in the format "<Type>.<Token>".  E.g. "Zone.Mil" or "Level.15"
//  This function appears to be broken for tokens of the form "<Type>.<Key>.<Value>". Not sure if it should be jswdeprecated.
S32 GamePermission_HasTokenType( GameAccountData *pData, GameTokenType eType, const char *pchToken)
{
	static char *estrBuffer = NULL;

	if(	pData
		&& eType >= kGameToken_None)
	{
		GenerateGameTokenKey(&estrBuffer,
			eType,
			NULL,
			pchToken);

		return eaIndexedGetUsingString(&pData->eaTokens, estrBuffer) != NULL;
	}
	return false;
}

S32 GamePermission_HasTokenKeyType( GameAccountData *pData, GameTokenType eType, const char *pchKey, const char *pchToken)
{
	static char *estrBuffer = NULL;

	if(	pData
		&& eType >= kGameToken_None)
	{
		GenerateGameTokenKey(&estrBuffer,
			eType,
			pchKey,
			pchToken);

		return eaIndexedGetUsingString(&pData->eaTokens, estrBuffer) != NULL;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(PermTokenType);
S32 GamePermission_expr_HasTokenType(SA_PARAM_OP_VALID Entity *pEnt, ACMD_EXPR_ENUM(GameTokenType) const char *pchType, const char *pchToken)
{
	GameAccountData *pData = entity_GetGameAccount(pEnt);
	GameTokenType eType = StaticDefineIntGetInt(GameTokenTypeEnum, pchType);
	return GamePermission_HasTokenType(pData, eType, pchToken);
}

AUTO_EXPR_FUNC(Player, mission) ACMD_NAME(PermTokenTypePlayer);
S32 GamePermission_expr_player_HasTokenType(ExprContext *context, ACMD_EXPR_ENUM(GameTokenType) const char *pchType, const char *pchToken)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	GameAccountData *pData = entity_GetGameAccount(pEnt);
	GameTokenType eType = StaticDefineIntGetInt(GameTokenTypeEnum, pchType);
	return GamePermission_HasTokenType(pData, eType, pchToken);
}

AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(PermTokenKeyType);
S32 GamePermission_expr_HasTokenKeyType(SA_PARAM_OP_VALID Entity *pEnt, ACMD_EXPR_ENUM(GameTokenType) const char *pchType, const char *pchKey, const char *pchToken)
{
	GameAccountData *pData = entity_GetGameAccount(pEnt);
	GameTokenType eType = StaticDefineIntGetInt(GameTokenTypeEnum, pchType);
	return GamePermission_HasTokenKeyType(pData, eType, pchKey, pchToken);
}

AUTO_EXPR_FUNC(Player, mission) ACMD_NAME(PermTokenKeyTypePlayer);
S32 GamePermission_expr_player_HasTokenKeyType(ExprContext *context, ACMD_EXPR_ENUM(GameTokenType) const char *pchType, const char *pchKey, const char *pchToken)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	GameAccountData *pData = entity_GetGameAccount(pEnt);
	GameTokenType eType = StaticDefineIntGetInt(GameTokenTypeEnum, pchType);
	return GamePermission_HasTokenKeyType(pData, eType, pchKey, pchToken);
}

static bool entIsPlayerType(SA_PARAM_OP_VALID Entity *pEnt, const char *pchType)
{
	int eType = StaticDefineIntGetInt(PlayerTypeEnum, pchType);

	return 
		eType >= 0 
		&& pEnt 
		&& pEnt->pPlayer 
		&& pEnt->pPlayer->playerType == eType;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntityIsPlayerType);
S32 entExprIsPlayerType(SA_PARAM_OP_VALID Entity *pEnt, const char *pchType)
{
	return entIsPlayerType(pEnt, pchType);
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(PlayerIsPlayerType);
S32 exprPlayerIsPlayerType(ACMD_EXPR_SELF Entity *pEnt, const char *pchType)
{
	return entIsPlayerType(pEnt, pchType);
}

//Used on the login server to validate a new player for f2p Champions Online
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ValidateNewPlayer);
void entExpr_ValidateNewPlayer(ExprContext *context, ACMD_EXPR_STRING_OUT outString, SA_PARAM_OP_VALID Entity *pEnt)
{	
	if(pEnt && pEnt->pChar && pEnt->pPlayer)
	{
		//No character should have a type of "none"
		if(pEnt->pPlayer->playerType == kPlayerType_None)
		{
			*outString = "New player has no player type!";
		}
		//Standard(f2p) players need to have a character path
		else if(pEnt->pPlayer->playerType == kPlayerType_Standard )
		{
			if(!entity_HasAnyCharacterPath(pEnt))
			{
				*outString = "New player is [Standard] but didn't chose a PowerPath";
			}
		}
	}
}

//Game permissions are considered enabled if we have premium or base (f2p) permissions enabled
 bool gamePermission_Enabled()
 {
	return (g_pBasePermission != NULL || g_pPremiumPermission != NULL);
 }

void gamePermissions_FillNameEArray(char ***peaNames)
{
	FOR_EACH_IN_EARRAY(g_GamePermissions.eaPermissions, GamePermissionDef, pPermission)
	{
		if(pPermission != g_pPremiumPermission && pPermission != g_pBasePermission && pPermission->pchName)
			eaPush(peaNames, strdup(pPermission->pchName));
	} FOR_EACH_END;
}

AUTO_TRANS_HELPER;
bool GamePermissions_trh_GetNameKeyValue(ATH_ARG NOCONST(GameToken) *pToken, char **esName, char **esKey, S32 *piValue)
{
	char *esContext = NULL;
	char *pcToken;
	char *pcKeyValue;
	char *pcValue;
	bool bNumeric = false;
	char *esTokenCopy = NULL;
	
	estrPrintf(&esTokenCopy, "%s", pToken->pchKey);
	
	pcToken = strtok_s(esTokenCopy, ".", &esContext);
	if(pcToken)
	{
		// get name
		estrPrintf(esName, "%s", pcToken);
		pcKeyValue = strtok_s(NULL, ".", &esContext);
		if(pcKeyValue)
		{
			estrPrintf(esKey, "%s", pcKeyValue);
			// key or value ...
			pcValue = strtok_s(NULL, ".", &esContext);
			if(pcValue)
			{
				*piValue = atoi(pcValue);
				bNumeric = true;
			}
		}
		
	}
	
	estrDestroy(&esTokenCopy);
	
	return bNumeric;
}

// return true if a token with this value is found. Return the highest value of the token. The key is 
AUTO_TRANS_HELPER;
bool GamePermissions_trh_GetPermissionValueUncached(ATH_ARG NOCONST(GameAccountData) *pData, const char *pcKey, S32 *piValue)
{
	S32 i, iBestValue = 0, iVal;
	bool bFound = false;
	char *esName = NULL, *esKey = NULL;
	if(ISNULL(pData) || !pcKey || !piValue)
	{
		return false;
	}
	
	// find the key
	for(i = 0; i < eaSize(&pData->eaTokens); ++i)
	{
		NOCONST(GameToken) *pToken = pData->eaTokens[i];
		if(GamePermissions_trh_GetNameKeyValue(pToken, &esName, &esKey, &iVal))
		{
			if(stricmp(esKey, pcKey) == 0 && iVal > iBestValue)
			{
				iBestValue = iVal;
				bFound = true;
			}
		}
	}
	
	estrDestroy(&esName);
	estrDestroy(&esKey);
	
	if(bFound)
	{
		*piValue = iBestValue;
	}
	
	return bFound;
}

// return the numeric limit for this 
AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, "pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
S32 GamePermissions_trh_GetCachedMaxNumericEx(ATH_ARG NOCONST(Entity) *pEntity, const char *pcNumeric, bool bMustBeNumeric, bool *pbFound)
{
	NOCONST(GamePermissionNumerics) *pNumeric;

	if(pbFound)
	{
		*pbFound = false;
	}

	if(!gamePermission_Enabled())
	{
		return NO_NUMERIC_LIMIT;
	}

	if(ISNULL(pEntity))
	{
		return 0;		
	}
	if(ISNULL(pEntity->pPlayer) || ISNULL(pEntity->pPlayer->pPlayerAccountData))
	{
		return NO_NUMERIC_LIMIT;		
	}
	if(ISNULL(pcNumeric))
	{
		return NO_NUMERIC_LIMIT;		
	}

	pNumeric = eaIndexedGetUsingString(&pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics, pcNumeric);	

	if(ISNULL(pNumeric))
	{
		return NO_NUMERIC_LIMIT;
	}
	
	if(bMustBeNumeric && !pNumeric->bIsNumeric)
	{
		devassertmsg(0, "Game permission non-numeric value matches game numeric! Please change non-numeric name.");
		return NO_NUMERIC_LIMIT;
	}

	if(pbFound)
	{
		*pbFound = true;
	}

	return pNumeric->iValue;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, "pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
S32 GamePermissions_trh_GetCachedMaxNumeric(ATH_ARG NOCONST(Entity) *pEntity, const char *pcNumeric, bool bMustBeNumeric)
{
	return GamePermissions_trh_GetCachedMaxNumericEx(pEntity, pcNumeric, bMustBeNumeric, NULL);
}

GameTokenText *gamePermission_TokenStructFromString(const char *pchString)
{
	GameTokenText *pText = NULL;
	if(pchString)
	{
		char *estrCopy = estrCreateFromStr(pchString);
		char *pTok = estrCopy;
		char *context = NULL;
		int i = 0;

		pText = StructCreate(parse_GameTokenText);
		
		pTok = strtok_s(pTok, ".", &context);
		while(pTok != NULL)
		{
			switch(i)
			{
				//Value token
			case 0:
				{
					pText->pchValue = StructAllocString(pTok);
					break;
				}

				//Type and Value
			case 1:
				{
					char *pchTypeString = pText->pchValue;
					pText->eType = StaticDefineIntGetInt(GameTokenTypeEnum, pchTypeString);
					pText->pchValue = StructAllocString(pTok);

					free(pchTypeString);
					break;
				}

				//Key with the type and value
			case 2:
				{
					pText->pchKey = pText->pchValue;
					pText->pchValue = StructAllocString(pTok);
					break;
				}

			default:
				break;
			}

			++i;
			pTok = strtok_s(NULL, ".", &context);
		}

		estrDestroy(&estrCopy);
	}
	return pText;
}

// Can this bag be purchased if locked?
AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, "pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool GamePermissions_trh_CanBuyBag(ATH_ARG NOCONST(Entity) *pEntity, InvBagIDs bagId, GameAccountDataExtract *pExtract)
{
	GamePermissionBagRestriction *pBagRestriction;

	if(ISNULL(pEntity))
	{
		// no entity, can't buy
		return false;
	}

	if(!gamePermission_Enabled())
	{
		// can't buy if restrictions not on
		return false;
	}

	if(GamePermissions_trh_CanAccessBag(pEntity, bagId, pExtract))
	{
		// already own
		return false;
	}

	pBagRestriction = eaIndexedGetUsingInt(&g_GamePermissions.eaInvBagRestrictions, bagId);

	if(!pBagRestriction)
	{
		// no restriction on this bag
		return false;
	}

	return pBagRestriction->bCanBuyBag;

}

// Can this bag be accessed?
AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, "pInventoryV2.ppLiteBags[], .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics[]");
bool GamePermissions_trh_CanAccessBag(ATH_ARG NOCONST(Entity) *pEntity, InvBagIDs bagId, GameAccountDataExtract *pExtract)
{
	GamePermissionBagRestriction *pBagRestriction;
	S32 iExtraInventoryBags;
	S32 iFreeBags;
	bool bFound;

	if(ISNULL(pEntity))
	{
		// no entity involved therefore no restriction
		return true;
	}

	if(!gamePermission_Enabled())
	{
		// not restricted if not on
		return true;
	}

	if(s_bPlayerConversionActive)
	{
		return true;
	}

	pBagRestriction = eaIndexedGetUsingInt(&g_GamePermissions.eaInvBagRestrictions, bagId);

	if(!pBagRestriction)
	{
		// no restriction on this bag
		return true;
	}

	if(!pExtract)
	{
		// not a player
		return true;
	}

	// Test against the numerics that you own
	iExtraInventoryBags = inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pEntity, "ExtraInventoryBags");
	iFreeBags = GamePermissions_trh_GetCachedMaxNumericEx(pEntity, "FreeBags" /* GAME_PERMISSION_FREE_BAGS */, false, &bFound);
	if(!bFound || !pBagRestriction->bCanBuyBag)
	{
		// Is zero, not the max uncached value
		iFreeBags = 0;
	}

	if(iExtraInventoryBags + iFreeBags >= bagId - InvBagIDs_PlayerBags)
	{
		return true;
	}

	return eaIndexedGetUsingString(&pExtract->eaTokens, pBagRestriction->pchValue) != NULL;
}

const char *GamePermissions_GetBagPermission(InvBagIDs bagId)
{
	GamePermissionBagRestriction *pBagRestriction;

	if(!gamePermission_Enabled())
	{
		// not restricted if not on
		return NULL;
	}

	pBagRestriction = eaIndexedGetUsingInt(&g_GamePermissions.eaInvBagRestrictions, bagId);

	if(!pBagRestriction)
	{
		// no restriction on this bag
		return NULL;
	}

	return pBagRestriction->pchValue;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, "pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
S32 GamePermissions_trh_GetNumberOfRestrictedBags(ATH_ARG NOCONST(Entity) *pEntity, GameAccountDataExtract *pExtract)
{
	S32 iNum = 0;
	if(ISNULL(pEntity))
	{
		// no entity involved therefore no restriction
		return 0;
	}

	if(!gamePermission_Enabled())
	{
		// not restricted if not on
		return 0;
	}
		
	iNum = eaSize(&g_GamePermissions.eaInvBagRestrictions);
	if(iNum > 0)		
	{
		// reduce restricted count for this player
		S32 i;
		
		for(i = 0; i < eaSize(&g_GamePermissions.eaInvBagRestrictions); ++i)
		{
			if(GamePermissions_trh_CanAccessBag(pEntity, InvBagIDs_PlayerBag1 + i, pExtract))
			{
				--iNum;
			}
		}
	}
	
	return iNum;
}

bool GamePermissions_PermissionGivesToken(GamePermissionDef *pPermission, const GameTokenText *pSearchToken)
{
	PERFINFO_AUTO_START_FUNC();
	FOR_EACH_IN_EARRAY(pPermission->eaTextTokens, GameTokenText, pToken)
	{
		if(StructCompare(parse_GameTokenText, pToken, pSearchToken, 0, 0, 0)==0)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
	}
	FOR_EACH_END
	PERFINFO_AUTO_STOP();

	return false;
}

bool GamePermissions_PremiumGivesToken(const GameTokenText *pToken)
{
	return GamePermissions_PermissionGivesToken(g_pPremiumPermission, pToken);
}

bool GamePermissions_BaseGivesToken(const GameTokenText *pToken)
{
	return GamePermissions_PermissionGivesToken(g_pBasePermission, pToken);
}

// is the time in a from to block?
// jswdeprecated - this has got to be right up there with the most horribleness per lines of code
bool GamePermissions_InFromToDate(S32 iIndex, U32 uSeconds)
{
	if(iIndex < eaSize(&g_GamePermissions.eaTimedTokenList))
	{
		S32 i;
		for(i = 0; i <eaSize(&g_GamePermissions.eaTimedTokenList[iIndex]->eaFromToDates); ++i)
		{
			if(uSeconds >= g_GamePermissions.eaTimedTokenList[iIndex]->eaFromToDates[i]->uFromTimeSeconds && uSeconds <= g_GamePermissions.eaTimedTokenList[iIndex]->eaFromToDates[i]->uToTimeSeconds)
			{
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(util, UIGen) ACMD_NAME(GamePermissionTimedSecondsLeft);
S32 exprGamePermissionTimedSecondsLeft(SA_PARAM_OP_VALID CharacterPath *pPath)
{
	S32 i, j;
	S32 iSecondsLeft= -1;

	if(pPath)
	{
		for(i = 0; i < eaSize(&g_GamePermissions.eaTimedTokenList); ++i)
		{
			bool bFound = false;
			for (j = 0; j < (eaSize(&g_GamePermissions.eaTimedTokenList[i]->eaPermissions) && !bFound); ++j)
			{
				S32 k;
				for(k = 0; k < eaSize(&g_GamePermissions.eaTimedTokenList[i]->eaPermissions[j]->eaTextTokens); ++k)
				{
					if(stricmp(g_GamePermissions.eaTimedTokenList[i]->eaPermissions[j]->eaTextTokens[k]->pchKey, GAME_PERMISSION_FREE) == 0 &&
						stricmp(g_GamePermissions.eaTimedTokenList[i]->eaPermissions[j]->eaTextTokens[k]->pchValue, pPath->pcGamePermissionValue) == 0)
					{
						bFound = true;
						break;
					}
				}
			}

			if(bFound)
			{
				U32 uCurTime = timeServerSecondsSince2000();
				for(j = 0; j < eaSize(&g_GamePermissions.eaTimedTokenList[i]->eaFromToDates); ++j)
				{
					if(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uFromTimeSeconds > 0 &&
						uCurTime > g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uFromTimeSeconds)
					{
						if(g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uToTimeSeconds > uCurTime)
						{
							S32 iTm = g_GamePermissions.eaTimedTokenList[i]->eaFromToDates[j]->uToTimeSeconds - uCurTime;
							if(iTm > iSecondsLeft)
							{
								iSecondsLeft = iTm;
							}
						}
						if(iSecondsLeft < 0)
						{
							iSecondsLeft = 0;
						}
					}
				}
			}
		}
	}

	return iSecondsLeft;
}

AUTO_EXPR_FUNC(util, UIGen) ACMD_NAME("GamePermissionTimedSecondsLeftByName");
S32 exprGamePermissionTimedSecondsLeftByName(SA_PARAM_OP_VALID const char *pcCharacterPathName)
{
	if(pcCharacterPathName)
	{
		CharacterPath *pCharacterPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pcCharacterPathName);
		return exprGamePermissionTimedSecondsLeft(pCharacterPath);
	}

	return -1;
}

void
GamePermissions_SetBaseAndPremium(void)
{
    S32 i;
    for(i = eaSize(&g_GamePermissions.eaPermissions)-1; i >= 0; i--)
    {
        GamePermissionDef *pDef = eaGet(&g_GamePermissions.eaPermissions, i);
        if(g_GamePermissions.eaPermissions[i]->eType == kGamePermission_Base)
        {
            if(g_pBasePermission)
            {
                Errorf("Base permissions were declared more than once in the definition file.");
                continue;
            }
            g_pBasePermission = pDef;
        }
        else if(g_GamePermissions.eaPermissions[i]->eType == kGamePermission_Premium)
        {
            if(g_pPremiumPermission)
            {
                Errorf("Premium permissions were declared more than once in the definition file.");
                continue;
            }
            g_pPremiumPermission = pDef;
        }
    }
}
#include "GamePermissionsCommon_h_ast.c"