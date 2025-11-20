#include "Entity.h"
#include "EntityLib.h"
#include "GameStringFormat.h"
#include "LoggedTransactions.h"
#include "gslMapState.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"

#include "gslPlayerDifficulty_cmd_c_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_STRUCT;
typedef struct PlayerDifficultyCBData
{
	U32 uiEntRef;
	int iDifficulty;
} PlayerDifficultyCBData;

static void PlayerChangeDifficultyCB(TransactionReturnVal *pReturnVal, PlayerDifficultyCBData *pData)
{
	Entity *pEnt = pData ? entFromEntityRefAnyPartition(pData->uiEntRef) : NULL;
	char *peasMessage = NULL;

	if (!pEnt)
		return;

	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		PlayerDifficulty *pDifficulty = pData ? pd_GetDifficulty(pData->iDifficulty) : NULL;

		if (pDifficulty)
		{
			entFormatGameMessageKey(pEnt, &peasMessage, "Entity_DifficultyChanged", STRFMT_STRING("Difficulty", TranslateMessageRef(pDifficulty->hName)), STRFMT_END);
			ClientCmd_NotifySend(pEnt, kNotifyType_Default, peasMessage, NULL, NULL);
		}
	}
	else
	{
		entFormatGameMessageKey(pEnt, &peasMessage, "Entity_DifficultyChangeFailed", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_Default, peasMessage, NULL, NULL);
	}

	estrDestroy(&peasMessage);
	StructDestroy(parse_PlayerDifficultyCBData, pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void PlayerChangeDifficulty(Entity *pPlayerEnt, int iDifficulty)
{
	PlayerDifficultyCBData *pData = NULL;
	TransactionReturnVal *pReturn;

	// make sure player exists
	if (!pPlayerEnt || !pPlayerEnt->pPlayer)
		return;

	// make sure difficulty target exists
	if (!pd_GetDifficulty(iDifficulty))
		return;

	// make sure the player is not in an instance
	if (pd_MapDifficultyApplied())
		return;

	pData = StructCreate(parse_PlayerDifficultyCBData);
	pData->uiEntRef = pPlayerEnt->myRef;
	pData->iDifficulty = iDifficulty;
	pReturn = LoggedTransactions_CreateManagedReturnValEnt("PlayerChangeDifficulty", pPlayerEnt, PlayerChangeDifficultyCB, pData);
	AutoTrans_pd_tr_ChangeDifficulty(pReturn, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iDifficulty);
}

// A debug command for setting the map's difficulty.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME("SetMapDifficulty");
void pd_SetMapDifficultyCmd(Entity *e, int iDifficulty)
{
	mapState_SetDifficulty(entGetPartitionIdx(e), iDifficulty);
}

#include "gslPlayerDifficulty_cmd_c_ast.c"