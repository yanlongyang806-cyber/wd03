/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#include "AutoTransDefs.h"

#include "entity.h"
#include "player.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_TRANSACTION
ATR_LOCKS(e, ".Pplayer.Pgatewayinfo.Bhidden");
enumTransactionOutcome gateway_tr_SetHidden(ATR_ARGS, NOCONST(Entity)* e, U32 bHidden)
{
	if(NONNULL(e) && NONNULL(e->pPlayer) && NONNULL(e->pPlayer->pGatewayInfo))
	{
		e->pPlayer->pGatewayInfo->bHidden = bHidden;
		TRANSACTION_RETURN_LOG_SUCCESS("Hidden changed");
	}

	TRANSACTION_RETURN_LOG_FAILURE("Hidden not changed: entity id %d.", NONNULL(e) ? e->myContainerID : 0);
}

/* End of File */
