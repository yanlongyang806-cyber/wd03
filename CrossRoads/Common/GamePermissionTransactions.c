
#include "GameAccountData\GameAccountData.h"
#include "GameAccountData_h_ast.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionsCommon_h_ast.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "GameAccountDataCommon.h"
#include "GlobalTypes.h"
#include "GamePermissionTransactions.h"
#include "Alerts.h"

#include "AutoTransDefs.h"

AUTO_TRANS_HELPER;
static void GamePermissions_trh_AddPermission(ATH_ARG NOCONST(GameAccountData) *gameAccountData, NON_CONTAINER GamePermissionDef *gamePermissionDef)
{
    int i;

    gamePermission_trh_GetTokens(gamePermissionDef, &gameAccountData->eaTokens);
    for(i = eaSize(&gamePermissionDef->eaLevelRestrictedTokens) - 1; i >= 0; i-- )
    {
        LevelRestrictedTokens *levelRestrictedTokens = gamePermissionDef->eaLevelRestrictedTokens[i];
        if ( ( gameAccountData->uMaxCharacterLevelCached >= levelRestrictedTokens->uMinAccountLevel ) || 
            ( levelRestrictedTokens->allowIfBilled && gameAccountData->bBilled ) )
        {
            // If the player has reached high enough level, add the level restricted tokens.  
            // Also add them if the level restriction is flagged as allowIfBilled and the player has been billed.
            gamePermission_trh_GetTokensEx(levelRestrictedTokens->eaTextTokens, &gameAccountData->eaTokens);
        }
    }
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eapermissions, .Eatokens, .Umaxcharacterlevelcached, .Bbilled");
static bool GamePermissions_trh_CreateTokens_Internal(ATH_ARG NOCONST(GameAccountData) *pData, NON_CONTAINER GamePermissionDefs *pShadowPermissions)
{
    S32 i;
	GamePermissionDef *gamePermissionDef;

	//Add the tokens from the shadow permissions (gold/silver account status)
	if(pShadowPermissions)
	{
		for(i=eaSize(&pShadowPermissions->eaPermissions)-1; i>=0; i--)
		{
            gamePermissionDef = pShadowPermissions->eaPermissions[i];
            GamePermissions_trh_AddPermission(pData, gamePermissionDef);
		}
	}
	for(i=eaSize(&pData->eaPermissions)-1; i>=0; i--)
	{
		gamePermissionDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pData->eaPermissions[i]->pchName);
		if(gamePermissionDef)
		{
            GamePermissions_trh_AddPermission(pData, gamePermissionDef);
		}
	}
	// Look for any permissions that have the type kGamePermission_NotPresent.  Add their tokens to the player if the
	//  player doesn't have the permission.
	for(i=eaSize(&g_GamePermissions.eaPermissions)-1; i>=0; i--)
	{
		gamePermissionDef = g_GamePermissions.eaPermissions[i];
		if ( gamePermissionDef && ( eaSize(&gamePermissionDef->eaNotPresentTokens) > 0 ) )
		{
			// If the permission is not present on the GameAccountData, then add the "not present" tokens
			if ( eaIndexedGetUsingString(&pData->eaPermissions, gamePermissionDef->pchName) == NULL )
			{
				gamePermission_trh_GetTokensEx(gamePermissionDef->eaNotPresentTokens, &pData->eaTokens);
			}
		}
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eapermissions, .Eatokens, .Umaxcharacterlevelcached, .Bbilled");
bool GamePermissions_trh_CreateTokens(ATH_ARG NOCONST(GameAccountData) *pData, NON_CONTAINER GamePermissionDefs *pShadowPermissions)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);
	return GamePermissions_trh_CreateTokens_Internal(pData, pShadowPermissions);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eapermissions, .Eatokens, .Umaxcharacterlevelcached, .Bbilled");
bool GamePermissions_trh_CreateTokens_Force(ATH_ARG NOCONST(GameAccountData) *pData, NON_CONTAINER GamePermissionDefs *pShadowPermissions)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return GamePermissions_trh_CreateTokens_Internal(pData, pShadowPermissions);
}

AUTO_TRANSACTION ATR_LOCKS(pData, ".eaTokens, .eaPermissions, .Umaxcharacterlevelcached, .Bbilled");
enumTransactionOutcome GamePermissions_CreateTokens(ATR_ARGS, NOCONST(GameAccountData) *pData, NON_CONTAINER GamePermissionDefs *pTempPermissions, U32 bAddTokens)
{
	enumTransactionOutcome eOutcome = TRANSACTION_OUTCOME_FAILURE;

	if(!bAddTokens)
	{
		//Clear out the tokens
		eaClearStructNoConst(&pData->eaTokens, parse_GameToken);
	}
	
	//Then create them
	if(GamePermissions_trh_CreateTokens_Force(pData, pTempPermissions))
	{
		eOutcome = TRANSACTION_OUTCOME_SUCCESS;
	}

	return eOutcome;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, ".Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics")
ATR_LOCKS(pData, ".Eatokens");
bool GamePermissions_trh_UpdateNumerics(ATH_ARG NOCONST(Entity) *pEntity, ATH_ARG NOCONST(GameAccountData) *pData, bool bCheckForUpdate, bool bDoUpdate)
{
	bool bRequiresUpdate = false;
	NOCONST(GamePermissionNumerics) **eaFoundNumerics = NULL;
	S32 i;
	
	if(ISNULL(pData) || ISNULL(pEntity) || ISNULL(pEntity->pPlayer))
	{
		return false;	
	}
	
	if(bDoUpdate)
	{
		eaDestroyStructNoConst(&pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics, parse_GamePermissionNumerics);
	}
	
	for(i = 0; i < eaSize(&pData->eaTokens); ++i)
	{
		NOCONST(GameToken) *pToken = pData->eaTokens[i];
		if(pToken)
		{
			char *esName = NULL;
			char *esKey = NULL;
			S32 iValue;

			bool bNumeric = gamePermissions_GetNameKeyValue((GameToken *)pToken, &esName, &esKey, &iValue);
			if(bNumeric)
			{
				// find the numeric
				NOCONST(GamePermissionNumerics) *pNumeric = eaIndexedGetUsingString(&pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics, esKey);
				if(bDoUpdate)
				{
					if(pNumeric)
					{
						if(iValue > pNumeric->iValue)
						{	
							pNumeric->iValue = iValue;
						}						
					}
					else
					{
						NOCONST(GamePermissionNumerics) *pNewNumeric = StructCreateNoConst(parse_GamePermissionNumerics);

						pNewNumeric->iValue = iValue;
						estrPrintf(&pNewNumeric->pchKey, "%s", esKey);
						if(stricmp(esName, GAME_PERMISSION_NUMERIC) == 0 )
						{
							pNewNumeric->bIsNumeric = true;
						}

						if(!pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics)
						{
							eaIndexedEnableNoConst(&pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics, parse_GamePermissionNumerics);
						}

						eaIndexedAdd(&pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics, pNewNumeric);
					}
				}
				else
				{
					if(!eaFoundNumerics)
					{
						eaIndexedEnableNoConst(&eaFoundNumerics, parse_GamePermissionNumerics);
					}
					
					if(pNumeric)
					{
						NOCONST(GamePermissionNumerics) *pNewNumeric = eaIndexedGetUsingString(&eaFoundNumerics, pNumeric->pchKey);
						if(pNewNumeric)
						{
							if(iValue > pNewNumeric->iValue)
							{
								pNewNumeric->iValue = iValue;	// set the new numeric to the new high value
							}
						}
						else
						{
							NOCONST(GamePermissionNumerics) *pCloneNumeric = StructCreateNoConst(parse_GamePermissionNumerics);
							pCloneNumeric->iValue = iValue;
							estrPrintf(&pCloneNumeric->pchKey, "%s", esKey);
							eaIndexedAdd(&eaFoundNumerics, pCloneNumeric);
						}
					}
					else
					{
						bRequiresUpdate = true;
						break;
					}
				}
			}

			estrDestroy(&esName);
			estrDestroy(&esKey);
		}
	}

	if(!bDoUpdate && !bRequiresUpdate)
	{
		for(i = 0; i < eaSize(&pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics); ++i)
		{
			NOCONST(GamePermissionNumerics) *pNumeric = pEntity->pPlayer->pPlayerAccountData->eaGamePermissionMaxValueNumerics[i];
			// check to see if we have a numeric of this sort saved,
			// if we don't then this numeric is no longer there
			// also check for difference, if it is then an update is required
			NOCONST(GamePermissionNumerics) *pNewNumeric = eaIndexedGetUsingString(&eaFoundNumerics, pNumeric->pchKey);
			if(!pNewNumeric || pNewNumeric->iValue != pNumeric->iValue)
			{
				bRequiresUpdate = true;
				break;			
			}
		}
	}

	// cleanup
	eaDestroyStructNoConst(&eaFoundNumerics, parse_GamePermissionNumerics);

	// update required	
	if(!bDoUpdate && bRequiresUpdate && bCheckForUpdate)
	{
		GamePermissions_trh_UpdateNumerics(pEntity, pData, true, true);
	}
	
	return bRequiresUpdate;
}

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics")
ATR_LOCKS(pData, ".Eatokens");
enumTransactionOutcome GamePermissions_tr_UpdateNumerics(ATR_ARGS, NOCONST(Entity) *pEntity, NOCONST(GameAccountData) *pData)
{
	// check to see if numerics need to be changed and if so do so
	GamePermissions_trh_UpdateNumerics(pEntity, pData, true, false);

	return TRANSACTION_OUTCOME_SUCCESS;
}
