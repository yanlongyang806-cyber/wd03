/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_StateMachine.h"
#include "stdtypes.h"
#include "TransactionOutcomes.h"
#include "timing.h"
#include "Entity.h"
#include "Player.h"
#include "AutoTransDefs.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"

AUTO_TRANSACTION
ATR_LOCKS(playerEnt, ".Pplayer.Addictionplaysessionendtime");
enumTransactionOutcome 
aslLogin2_tr_SetAddictionPlaySessionEndTime(ATR_ARGS, NOCONST(Entity) *playerEnt, U32 playSessionEndTime)
{
    if ( NONNULL(playerEnt) && NONNULL(playerEnt->pPlayer) )
    {
        playerEnt->pPlayer->addictionPlaySessionEndTime = playSessionEndTime;
    }

    return TRANSACTION_OUTCOME_SUCCESS;
}
