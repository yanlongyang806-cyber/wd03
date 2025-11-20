#include "aiAnimList.h"

#include "aiMovement.h"
#include "aiStruct.h"
#include "AnimList_Common.h"
#include "Character.h"
#include "EntityMovementManager.h"
#include "EntityMovementEmote.h"
#include "PowerAnimFX.h"
#include "Powers.h"
#include "TextParserSimpleInheritance.h"
#include "gslEntity.h"

#include "AILib_autogen_QueuedFuncs.h"

AUTO_COMMAND_QUEUED();
void aiAnimListRemoveAnim(ACMD_POINTER Entity* e, const char* animList, int handle)
{
	if(gConf.bNewAnimationSystem){
		mrEmoteSetDestroy(e->mm.mrEmote, &handle);
	}else{
		aiMovementRemoveAnimBitHandle(e, handle);
	}
}

AUTO_COMMAND_QUEUED();
void aiAnimListRemoveFX(ACMD_POINTER Entity* e, const char* animList,
							const char* name, int handle)
{
	aiMovementRemoveFX(e, name, handle);
}

AUTO_COMMAND_QUEUED();
void aiAnimListRestoreDefaultStance(ACMD_POINTER Entity* e, const char* animList,
									const char* stancePowerDefString)
{
	character_SetDefaultStance(entGetPartitionIdx(e), e->pChar, powerdef_Find(stancePowerDefString));
}

int aiAnimListSet_dbg(Entity* e, const AIAnimList* al, CommandQueue** queueForDestroyCmd, bool isEmote,
						const char* fileName, U32 fileLine)
{
	int handle = 0;

	if(!al)
		return false;

	if(queueForDestroyCmd && !*queueForDestroyCmd)
		*queueForDestroyCmd = CommandQueue_Create_Dbg(32, false, fileName, fileLine);
	
	mmLog(	e->mm.movement,
			NULL,
			"[ai.animlist] Starting anim list \"%s\" from %s:%d.",
			al->name,
			fileName,
			fileLine);

	EARRAY_FOREACH_REVERSE_BEGIN(al->FX, i);
	{
		aiMovementAddFX(e, al->FX[i], &handle);
		if(!al->manuallyDestroyedFX && queueForDestroyCmd)
			QueuedCommand_aiAnimListRemoveFX(*queueForDestroyCmd, e, al->name, al->FX[i], handle);
	}
	EARRAY_FOREACH_END;

	EARRAY_FOREACH_REVERSE_BEGIN(al->FlashFX, i);
	{
		aiMovementFlashFX(e, al->FlashFX[i]);
	}
	EARRAY_FOREACH_END;

	if (gConf.bNewAnimationSystem)
	{
		MREmoteSet* set = StructAlloc(parse_MREmoteSet);

		if(al->animKeyword){
			set->animToStart = mmGetAnimBitHandleByName(al->animKeyword, 0);
		}

		if(!e->mm.mrEmote){
			gslEntMovementCreateEmoteRequester(e);
		}

		if (e->mm.mrEmoteSetHandle) {
			mrEmoteSetDestroy(e->mm.mrEmote, &e->mm.mrEmoteSetHandle);
		}

		if(!mrEmoteSetCreate(	e->mm.mrEmote,
								&set,
								queueForDestroyCmd ? &handle : NULL))
		{
			StructDestroySafe(parse_MREmoteSet, &set);
		}
		else if (isEmote) {
			e->mm.mrEmoteSetHandle = handle;
		}
		else if(queueForDestroyCmd){
			e->mm.mrEmoteSetHandle = 0;
			QueuedCommand_aiAnimListRemoveAnim(*queueForDestroyCmd, e, al->name, handle);
		}
	}
	else
	{
		EARRAY_FOREACH_REVERSE_BEGIN(al->bits, i);
		{
			U32 bitHandle = mmGetAnimBitHandleByName(al->bits[i], false);
			aiMovementAddAnimBitHandle(e, bitHandle, &handle);
			if (queueForDestroyCmd)
				QueuedCommand_aiAnimListRemoveAnim(*queueForDestroyCmd, e, al->name, handle);
		}
		EARRAY_FOREACH_END;
	}

	if(!al->enableStance && e->pChar && IS_HANDLE_ACTIVE(e->pChar->hPowerDefStanceDefault))
	{
		const char* stancePower = REF_STRING_FROM_HANDLE(e->pChar->hPowerDefStanceDefault);
		character_SetDefaultStance(entGetPartitionIdx(e), e->pChar, NULL);
		if(queueForDestroyCmd)
			QueuedCommand_aiAnimListRestoreDefaultStance(*queueForDestroyCmd, e, al->name, stancePower);
	}

	return true;
}

void aiAnimListSetHold(Entity *e, AIAnimList* al)
{
	AIVarsBase *aib = e->aibase;
	int i;
	
	for(i=0; i<eaSize(&al->bits); i++)
	{
		U32 bitHandle = mmGetAnimBitHandleByName(al->bits[i], false);

		aiMovementAddHoldBit(e, bitHandle);
	}
}

void aiAnimListClearHold(Entity *e)
{
	aiMovementClearAnimHold(e);
}

void aiAnimListSetOneTickEx(Entity* e, const AIAnimList* al, bool isEmote)
{
	AIVarsBase* aib = e->aibase;

	aiAnimListSetEx(e, al, &aib->nextTickCmdQueue, isEmote);
}

static int aiAnimInheritanceFunc(ParseTable *pti, int column, void *dst, void *src, void *unused)
{
	return 0;
}

static void aiAnimInheritanceApply(AIAnimList *al)
{
	SimpleInheritanceApply(parse_AIAnimList, al, al, aiAnimInheritanceFunc, NULL);
}

AUTO_FIXUPFUNC;
TextParserResult fixupAIAnimList(AIAnimList* al, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
			aiAnimInheritanceApply(al);

		xcase FIXUPTYPE_POST_RELOAD:
			aiAnimInheritanceApply(al);

	}

	return PARSERESULT_SUCCESS;
}
