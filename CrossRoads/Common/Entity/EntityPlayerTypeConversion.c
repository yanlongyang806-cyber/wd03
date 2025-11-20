
#include "AutoTransDefs.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterClass.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "GameAccountDataCommon.h"
#include "LoginCommon.h"
#include "microtransactions_common.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "GamePermissionsCommon.h"

////////////////////////////////////////////////
// Player type Conversion functions

//This function assumes that the player only has ONE character path (champions) as opposed to an arbitrary number (neverwinter)
S32 ConversionRequiresRespec(PlayerTypeConversion *pConversion, PlayerType ePlayerTypeOld, CharacterPath *pPathOld, CharacterPath *pPathNew)
{
	bool bRequiresConversion = false;

	if(pConversion->iPlayerTypeNew == kPlayerType_SuperPremium)
	{
		if(pPathOld)
		{
			bRequiresConversion = (pConversion->bConvertToFreeform || pPathNew != pPathOld);
		}
		else
		{
			bRequiresConversion = (pPathNew != NULL);
		}
	}
	else if(pConversion->iPlayerTypeNew == kPlayerType_Premium)
	{
		if(ePlayerTypeOld == kPlayerType_Premium)
		{
			if(pPathOld)
			{
				bRequiresConversion = (pConversion->bConvertToFreeform || pPathNew != pPathOld);
			}
			else
			{
				bRequiresConversion = (pPathNew != NULL);
			}
		}
		//Silver to gold conversion
		else
		{
			if(pConversion->bConvertToFreeform)
				bRequiresConversion = true;
			else if(pPathNew != pPathOld)
				bRequiresConversion = true;
		}
	}
	else if(pConversion->iPlayerTypeNew == kPlayerType_Standard)
	{
		//Converting from gold to silver
		if(ePlayerTypeOld == kPlayerType_Premium)
		{
			if(pPathOld)
			{
				bRequiresConversion = (pPathOld != pPathNew);
			}
			else
				bRequiresConversion = true;
		}
		else if(ePlayerTypeOld == kPlayerType_Standard)
		{
			bRequiresConversion = (pPathOld != pPathNew);
		}
	}

	return bRequiresConversion;
}

AUTO_TRANS_HELPER;
S32 Entity_ConversionRequiresRespec(ATH_ARG NOCONST(Entity) *pEnt, PlayerTypeConversion *pConversion, CharacterPath *pPath)
{
	if(NONNULL(pEnt) && NONNULL(pEnt->pChar) && NONNULL(pEnt->pPlayer))
		return ConversionRequiresRespec(pConversion, pEnt->pPlayer->playerType, entity_trh_GetPrimaryCharacterPath(pEnt), pPath);
	
	return false;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pplayer.Uirespecconversions");
enumTransactionOutcome trEntity_SetRespecConversions(ATR_ARGS, NOCONST(Entity) *pEnt, int iVal)
{
	if(NONNULL(pEnt) && NONNULL(pEnt->pPlayer))
	{
		if(iVal >= 0 && iVal <= U8_MAX)
		{
			pEnt->pPlayer->uiRespecConversions = iVal;
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEntity, ".Pplayer.playerType");
enumTransactionOutcome trEntity_SetPlayerType(ATR_ARGS, NOCONST(Entity) *pEntity, int iPlayerType)
{
	if(NONNULL(pEntity) && NONNULL(pEntity->pPlayer) && iPlayerType >= kPlayerType_Standard && iPlayerType <= kPlayerType_SuperPremium)
	{
		pEntity->pPlayer->playerType = iPlayerType;
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

#define MAX_RESPEC_CONVERSIONS 1

S32 EntityUtil_MaxPlayerRespecConversions()
{
	return MAX_RESPEC_CONVERSIONS;
}

// Returns true if the entity is allowed to do this conversions at this time
//	the optional parameter pbRequiresRespec will return the result of Entity_ConversionRequiresRespec
//	which is called inside this function
// This function assumes the entity in question has exactly one CharPath (as in Champions)
S32 Entity_ValidateConversion(Entity *pEnt, PlayerTypeConversion *pConversion, CharacterPath *pPath, int *pbRequiresRespec)
{
	S32 bUpgradeConversion, bIsRespecConversion = false;
	CharacterPath *pCurrentPath = NULL;
	bool bATToATFree = false;

	if(!pEnt || !pEnt->pPlayer || !pEnt->pChar)
	{
		//Not valid!
		return false;
	}

	if(pEnt->pPlayer->playerType == kPlayerType_SuperPremium && pConversion->iPlayerTypeNew != kPlayerType_SuperPremium)
	{
		// never convert kPlayerType_SuperPremium
		return false;
	}

	if(!pConversion->bConvertToFreeform)
	{
		if(!pPath || !Entity_EvalCharacterPathRequiresExpr(pEnt,pPath))
		{
			// Bad path
			return false;
		}
	}

	pCurrentPath = entity_GetPrimaryCharacterPath(pEnt);

	// The following allows free conversions to timed free character paths
	if(!pConversion->bConvertToFreeform)
	{
		GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
		if
		(	
			pGameAccount && 
			(
				CharacterPath_trh_CanUseEx(CONTAINER_NOCONST(GameAccountData, pGameAccount), pPath, GAME_PERMISSION_FREE) ||
				(pCurrentPath && !CharacterPath_trh_CanUse(CONTAINER_NOCONST(GameAccountData, pGameAccount), pCurrentPath))
			)
		)
		{
			bATToATFree = true;
		}
	}

	if(pConversion->bSideConvert)
	{
		//Cannot do side conversion when types don't match
		if(pConversion->iPlayerTypeNew != pEnt->pPlayer->playerType)
			return false;

		//No conversion possible for a side convert where the paths are the same
		if(!pConversion->bConvertToFreeform &&
			pPath == pCurrentPath)
			return false;

		if(pConversion->bConvertToFreeform && !pCurrentPath)
			return false;
	}

	if(pConversion->iPlayerTypeNew == kPlayerType_Standard)
	{
		if(pConversion->bConvertToFreeform)
			return false;

		if(!pConversion->pchCharacterPath && pCurrentPath)
		{
			//This character needs to end up with a character path and none was specified.
			return false;
		}

		//No conversion
		if(pEnt->pPlayer->playerType == kPlayerType_Standard && pPath == pCurrentPath)
			return false;
	}
	else if(pConversion->iPlayerTypeNew == kPlayerType_Premium)
	{
		if(!pPath && !pConversion->bConvertToFreeform)
		{
			return false;
		}
		if(pPath && pConversion->bConvertToFreeform)
		{
			//Ambiguous
			return false;
		}

		//No conversion
		if(pEnt->pPlayer->playerType == kPlayerType_Premium && pPath == pCurrentPath)
			return false;
	}

	bIsRespecConversion = Entity_ConversionRequiresRespec(CONTAINER_NOCONST(Entity, pEnt), pConversion, pPath);

	if(pbRequiresRespec)
		(*pbRequiresRespec) = bIsRespecConversion;

	bUpgradeConversion = (pConversion->iPlayerTypeNew == kPlayerType_Premium && pEnt->pPlayer->playerType == kPlayerType_Standard);

	return !bIsRespecConversion 
		//If converting from silver to gold, you're allowed to convert
		|| bUpgradeConversion
		|| ( pConversion->bPayWithGADToken && gad_GetAttribInt(entity_GetGameAccount(pEnt), MicroTrans_GetRetrainTokensGADKey()) > 0 )
		|| pEnt->pPlayer->uiRespecConversions < MAX_RESPEC_CONVERSIONS
		|| bATToATFree;
}
