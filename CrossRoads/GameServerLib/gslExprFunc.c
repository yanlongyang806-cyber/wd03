#include "cmdServerCombat.h"
#include "CostumeCommonTailor.h"
#include "Entity.h"
#include "dynFxInfo.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "earray.h"
#include "EntityGrid.h"
#include "EntityIterator.h"
#include "Expression.h"
#include "EString.h"
#include "gslEntity.h"
#include "gslMechanics.h"
#include "gslPartition.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "StringCache.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "../Common/AutoGen/WorldLib_autogen_ClientCmdWrappers.h"
#include "../Common/AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncPlayFxSC(ACMD_EXPR_SELF Entity* e, ExprContext* context, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	if(!stricmp(fxName, MULTI_DUMMY_STRING))
		return ExprFuncReturnFinished;

	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "FX Info didn't exist: %s", fxName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Plays an effect on the current entity for every player within the FX draw distance
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(PlayFx) ACMD_EXPR_STATIC_CHECK(exprFuncPlayFxSC);
ExprFuncReturnVal exprFuncPlayFx(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	static Entity** playersNear = NULL;
	F32 fxDist;
	Vec3 myPos;
	int i, n;
	EntityRef myRef = entGetRef(be);

	if(!fxName[0])
		return ExprFuncReturnError;

	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "FX didn't exist: %s", fxName);
		return ExprFuncReturnError;
	}

	entGetPos(be, myPos);
	fxDist = dynFxGetMaxDrawDistance(fxName);

	entGridProximityLookupExEArray(entGetPartitionIdx(be), myPos, &playersNear, fxDist, ENTITYFLAG_IS_PLAYER, 0, be);

	n = eaSize(&playersNear);
	for(i = 0; i < n; i++)
	{
		ClientCmd_dtAddFxToEntSimple(playersNear[i], myRef, fxName, exprContextGetBlameFile(context));
	}

	return ExprFuncReturnFinished;
}

static ExprFuncReturnVal PlayFxPointToPointHelper(ACMD_EXPR_PARTITION partition, Mat4 posSource, Mat4 posTarget, const char* fxName, ACMD_EXPR_ERRSTRING errString, const char* filename)
{
	static Entity** playersNear = NULL;
	F32 fxDist;
	int i, n;

	if(!fxName[0])
	{
		estrPrintf(errString, "No FX name");
		return ExprFuncReturnError;
	}
	if(!posSource)
	{
		estrPrintf(errString, "No source location for FX");
		return ExprFuncReturnError;
	}
	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "FX Info didn't exist: %s", fxName);
		return ExprFuncReturnError;
	}

	fxDist = dynFxGetMaxDrawDistance(fxName);

	entGridProximityLookupExEArray(partition, posSource[3], &playersNear, fxDist, ENTITYFLAG_IS_PLAYER, 0, NULL);

	n = eaSize(&playersNear);
	for(i = 0; i < n; i++)
	{
		if(posTarget)
			ClientCmd_dynAddFxAtLocationWithTarget(playersNear[i], fxName, posSource, posTarget, filename);
		else
			ClientCmd_dynAddFxAtLocationSimple(playersNear[i], fxName, posSource, filename);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncPlayFxAtPointSC(ExprContext *context, ACMD_EXPR_PARTITION partition, ACMD_EXPR_LOC_MAT4_IN posIn, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	if(!stricmp(fxName, MULTI_DUMMY_STRING))
		return ExprFuncReturnFinished;

	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "FX Info didn't exist: %s", fxName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Plays an effect at the passed in location, for everyone who is within the effect's draw
// distance
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayFxAtPoint) ACMD_EXPR_STATIC_CHECK(exprFuncPlayFxAtPointSC);
ExprFuncReturnVal exprFuncPlayFxAtPoint(ExprContext *context, ACMD_EXPR_PARTITION partition, ACMD_EXPR_LOC_MAT4_IN posIn, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	return PlayFxPointToPointHelper(partition, posIn, NULL, fxName, errString, exprContextGetBlameFile(context));
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncPlayFxPointToPointSC(ExprContext *context, ACMD_EXPR_PARTITION partition, ACMD_EXPR_LOC_MAT4_IN posSource, ACMD_EXPR_LOC_MAT4_IN posTarget, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	if(!stricmp(fxName, MULTI_DUMMY_STRING))
		return ExprFuncReturnFinished;

	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "FX Info didn't exist: %s", fxName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Plays an effect that starts at the first point and targets the second point, for everyone who is within
// the effect's draw distance
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayFxPointToPoint) ACMD_EXPR_STATIC_CHECK(exprFuncPlayFxPointToPointSC);
ExprFuncReturnVal exprFuncPlayFxPointToPoint(ExprContext *context, ACMD_EXPR_PARTITION partition, ACMD_EXPR_LOC_MAT4_IN posSource, ACMD_EXPR_LOC_MAT4_IN posTarget, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	if(!posTarget)
	{
		estrPrintf(errString, "No target location for FX");
		return ExprFuncReturnError;
	}

	return PlayFxPointToPointHelper(partition, posSource, posTarget, fxName, errString, exprContextGetBlameFile(context));
}

static ExprFuncReturnVal PlayFxPointToPointWholeMapHelper(int iPartitionIdx, Mat4 posSource, Mat4 posTarget, const char* fxName, ACMD_EXPR_ERRSTRING errString, const char* filename)
{
	EntityIterator* iter;
	Entity* e;

	if(!fxName[0])
	{
		estrPrintf(errString, "No FX name");
		return ExprFuncReturnError;
	}
	if(!posSource)
	{
		estrPrintf(errString, "No source location for FX");
		return ExprFuncReturnError;
	}

	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "Invalid FX name: %s", fxName);
		return ExprFuncReturnError;
	}

	iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

	while(e = EntityIteratorGetNext(iter))
	{
		if(posTarget)
			ClientCmd_dynAddFxAtLocationWithTarget(e, fxName, posSource, posTarget, filename);
		else
			ClientCmd_dynAddFxAtLocationSimple(e, fxName, posSource, filename);
	}
	EntityIteratorRelease(iter);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncPlayFxAtPointForWholeMapSC(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN posIn, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	if(!stricmp(fxName, MULTI_DUMMY_STRING))
		return ExprFuncReturnFinished;

	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "FX Info didn't exist: %s", fxName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Plays an effect at the passed in location, for everyone on the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayFxAtPointForWholeMap) ACMD_EXPR_STATIC_CHECK(exprFuncPlayFxAtPointForWholeMapSC);
ExprFuncReturnVal exprFuncPlayFxAtPointForWholeMap(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN posIn, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	return PlayFxPointToPointWholeMapHelper(iPartitionIdx, posIn, NULL, fxName, errString, exprContextGetBlameFile(context));
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncPlayFxPointToPointForWholeMapSC(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN posSource, ACMD_EXPR_LOC_MAT4_IN posTarget, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	if(!stricmp(fxName, MULTI_DUMMY_STRING))
		return ExprFuncReturnFinished;

	if(!dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "FX Info didn't exist: %s", fxName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Plays an effect at the passed in location to the target location, for everyone on the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayFxPointToPointForWholeMap) ACMD_EXPR_STATIC_CHECK(exprFuncPlayFxPointToPointForWholeMapSC);
ExprFuncReturnVal exprFuncPlayFxPointToPointForWholeMap(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN posSource, ACMD_EXPR_LOC_MAT4_IN posTarget, const char* fxName, ACMD_EXPR_ERRSTRING errString)
{
	if(!posTarget)
	{
		estrPrintf(errString, "No target location for FX");
		return ExprFuncReturnError;
	}
	return PlayFxPointToPointWholeMapHelper(iPartitionIdx, posSource, posTarget, fxName, errString, exprContextGetBlameFile(context));
}

static int exprFXId = 0;

int exprFuncGetFxId(void)
{
	exprFXId++;
	if(exprFXId==0)
		exprFXId++;
	return exprFXId;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(AddFxPointToPoint);
ExprFuncReturnVal exprFuncAddFxPointToPoint(ACMD_EXPR_PARTITION partition, ACMD_EXPR_INT_OUT idOut, ACMD_EXPR_LOC_MAT4_IN posSource, ACMD_EXPR_LOC_MAT4_IN posTarget, const char *fxName, F32 hue, ACMD_EXPR_ERRSTRING errString)
{
	int id;
	static const char **fxNames = NULL;

	if(!fxName || !dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "Invalid FX name: %s", fxName);
		return ExprFuncReturnError;
	}

	id = exprFuncGetFxId();
	eaClearFast(&fxNames);
	eaPush(&fxNames, allocAddString(fxName));
	location_StickyFXOn(posSource[3], partition, id, 1, kPowerAnimFXType_Expr, 
						NULL, posTarget[3], NULL, NULL, fxNames, NULL, hue, pmTimestamp(0), 0);

	*idOut = id;

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(AddFxPointToTarget);
ExprFuncReturnVal exprFuncAddFxPointToTarget(ACMD_EXPR_PARTITION partition, ACMD_EXPR_INT_OUT idOut, ACMD_EXPR_LOC_MAT4_IN posSource, ACMD_EXPR_ENTARRAY_IN entIn, const char *fxName, F32 hue, ACMD_EXPR_ERRSTRING errString)
{
	int id;
	static const char **fxNames = NULL;
	if(eaSize(entIn)==0)
	{
		estrPrintf(errString, "Cannot create FX to no-target");
		return ExprFuncReturnError;
	}

	if(eaSize(entIn)>1)
	{
		estrPrintf(errString, "Cannot create FX to more than one target at a time");
		return ExprFuncReturnError;
	}

	if(!(*entIn)[0]->pChar)
	{
		estrPrintf(errString, "Unable to add FX to non-combat target at present.  Bug Adam if you really want to fix this");
		return ExprFuncReturnError;
	}

	if(!fxName || !dynFxInfoExists(fxName))
	{
		estrPrintf(errString, "Invalid FX name: %s", fxName);
		return ExprFuncReturnError;
	}

	id = exprFuncGetFxId();
	eaClearFast(&fxNames);
	eaPush(&fxNames, allocAddString(fxName));
	location_StickyFXOn(posSource[3], partition, id, 1, kPowerAnimFXType_Expr,
					(*entIn)[0]->pChar, NULL, NULL, NULL, fxNames, NULL, hue, pmTimestamp(0), 0);

	*idOut = id;

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(RemoveFx);
ExprFuncReturnVal exprFuncRemoveFx(ACMD_EXPR_PARTITION partition, int fxId, ACMD_EXPR_ERRSTRING errString)
{
	if(!fxId)
	{
		estrPrintf(errString, "Invalid FX id: %d", fxId);
		return ExprFuncReturnError;
	}

	location_StickyFXOff(partition, fxId, 1, kPowerAnimFXType_Expr, NULL, pmTimestamp(0));

	return ExprFuncReturnFinished;
}

// TODO: this should really be in an "entity_server" category or something

// Removes everything EXCEPT the map's owner from the array
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntCropMapOwner);
void exprFuncEntCropMapOwner(ACMD_EXPR_PARTITION partition, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, n = eaSize(entsInOut);
	Entity* mapOwner = partition_GetPlayerMapOwner(partition);

	if(!mapOwner)
		eaClearFast(entsInOut);
	else
	{
		for(i = n-1; i >= 0; i--)
		{
			Entity* target = (*entsInOut)[i];

			if(target->myContainerID != mapOwner->myContainerID)
				eaRemoveFast(entsInOut, i);
		}
	}
}


AUTO_EXPR_FUNC(ai) ACMD_NAME("RespawnPlayer");
void exprPlayerRespawnPlayer(ACMD_EXPR_SELF Entity* e)
{
	gslPlayerRespawn(e, false, false);
}
