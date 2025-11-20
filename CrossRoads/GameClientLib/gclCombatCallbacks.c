/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatCallbacks.h"

#include "Character.h"
#include "cmdparse.h"
#include "gclEntity.h"
#include "soundLib.h"
#include "StringFormat.h"
#include "NotifyCommon.h"

#include "PowerActivation.h"
#include "PowersAutoDesc.h"
#include "CharacterAttribs.h"
#include "WorldGrid.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

static void gclccbHandlePMEvent(Entity *pent, U32 uiEventID, U32 uiUserID)
{
	if(pent && pent==entActivePlayerPtr() && pent->pChar)
	{
		Character *pchar = pent->pChar;
		if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uiUserID)
		{
			PowerActivation *pact = pchar->pPowActQueued;
			U32 uiSeq = 0, uiSeqReset = 0;
			character_GetPowerActSeq(pchar,&uiSeq,&uiSeqReset);
			pact->bCommit = true;
			pact->uiSeedSBLORN = uiSeq;
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pent, "Queue %d: Committed from PMEvent\n",pact->uchID);
			ServerCmd_MarkActCommitted(pact->uchID,uiSeq,uiSeqReset);
		}
		else if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uiUserID)
		{
			// Do nothing, the activation already made it current
		}
		else
		{
			// PowersError("!!!!! Received event notification for unknown activation %d at %d!!!!!!\n",uiUserID,pmTimestamp(0));
		}
	}
}

void gclInitCombatCallbacks(void)
{
	combatcbHandlePMEvent = gclccbHandlePMEvent;
}



// Safety function for the client - if a Power in my general list is getting destroyed,
//  go ahead and remove it from the list.  The server SHOULD be triggering a reset at
//  some point in the near future, this is purely a precaution.
AUTO_FIXUPFUNC;
TextParserResult gclFixup_Power(Power *ppow, enumTextParserFixupType eFixupType, void *pvExtraData)
{
	if(eFixupType==FIXUPTYPE_DESTRUCTOR && ppow && ppow->uiID)
	{
		Character *pchar = characterActivePlayerPtr();
		if(pchar)
		{
			int idx = eaIndexedFindUsingInt(&pchar->ppPowers,(S32)ppow->uiID);
			if(idx>=0 && pchar->ppPowers[idx]==ppow)
			{
				eaRemove(&pchar->ppPowers,idx);
			}
		}
	}

	return PARSERESULT_SUCCESS;
}