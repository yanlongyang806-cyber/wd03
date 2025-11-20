#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "gclMicroTransactions.h"
#include "GameClientLib.h"

AUTO_EXPR_FUNC(entityutil);
bool GamePermissions_CanAccessBag(SA_PARAM_NN_VALID Entity *pEntity, U32 eBagId)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);

	bool bResult = GamePermissions_trh_CanAccessBag(CONTAINER_NOCONST(Entity, pEntity), eBagId, pExtract);

	return bResult;
}

// Get the MicroTransaction Product ID to purchase access to the bag, or 0 if there is none.
AUTO_EXPR_FUNC(entityutil);
U32 GamePermissions_GetBagProduct(SA_PARAM_NN_VALID Entity *pEntity, U32 eBagId)
{
	static U32 s_eLastBagID = -1;
	static U32 s_uLastID = -1;
	static U32 s_iFrame;
	const char *pchToken;

	if (s_iFrame == gGCLState.totalElapsedTimeMs && eBagId == s_eLastBagID)
	{
		return s_uLastID;
	}

	pchToken = pEntity ? GamePermissions_GetBagPermission(eBagId) : NULL;
	if (pchToken)
	{
		MicroTransactionProduct *pDef = gclMicroTrans_FindProductForPermission(pchToken);
		if (pDef)
		{
			s_eLastBagID = eBagId;
			s_uLastID = pDef->uID;
			s_iFrame = gGCLState.totalElapsedTimeMs;
			return pDef->uID;
		}
	}

	return 0;
}
