/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AutoTransDefs.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "XBoxStructs.h"
#include "XBoxStructs_h_ast.h"

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pplayer.Pxboxspecificdata");
// This transaction sets the XNADDR for the player
enumTransactionOutcome gslXBox_trSetXBoxSpecificData(ATR_ARGS, NOCONST(Entity) *pEntity, NON_CONTAINER CrypticXnAddr *pXnAddr, U64 xuid)
{
	NOCONST(XBoxSpecificData) *pXBoxSpecificData = NULL;

	if (ISNULL(pEntity) || ISNULL(pEntity->pPlayer))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Create XBOX specific data if it does not exist
	if (pEntity->pPlayer->pXBoxSpecificData == NULL)
	{
		pEntity->pPlayer->pXBoxSpecificData = StructCreateNoConst(parse_XBoxSpecificData);
	}

	pXBoxSpecificData = pEntity->pPlayer->pXBoxSpecificData;

	// Set the XUID
	pXBoxSpecificData->xuid = xuid;

	// Assign the XN address
	if (pXBoxSpecificData->pXnAddr == NULL)
	{
		pXBoxSpecificData->pXnAddr = StructCloneDeConst(parse_CrypticXnAddr, pXnAddr);
	}
	else
	{
		StructCopyDeConst(parse_CrypticXnAddr, pXnAddr, pXBoxSpecificData->pXnAddr, 0, 0, 0);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pplayer.Pxboxspecificdata");
// This transaction sets the XNADDR for the player
enumTransactionOutcome gslXBox_trDeleteXBoxSpecificData(ATR_ARGS, NOCONST(Entity) *pEntity)
{
	if (ISNULL(pEntity) || ISNULL(pEntity->pPlayer))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Delete the XBOX specific data if necessary
	if (pEntity->pPlayer->pXBoxSpecificData != NULL)
	{
		StructDestroyNoConst(parse_XBoxSpecificData, pEntity->pPlayer->pXBoxSpecificData);
		pEntity->pPlayer->pXBoxSpecificData = NULL;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}