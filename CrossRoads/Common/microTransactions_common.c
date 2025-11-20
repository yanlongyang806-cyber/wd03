/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EString.h"
#include "GlobalTypes.h"
#include "microtransactions_common.h"
#include "microtransactions_common_h_ast.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("MicroTransactionPurchase", BUDGET_GameSystems););

// Returns 0 if there was an error.  Otherwise it returns the number of items to grant.
S32 MicroTrans_TokenizeItemID(char *pchItem, char **ppchGameTitle, MicroItemType *eType, char **ppchItemOut)
{
	char *pTok = pchItem;
	char *context = NULL;
	S32 i = 0;
	S32 iCount = 0;
	
	if(ppchGameTitle)
		*ppchGameTitle = NULL;
	if(eType)
		*eType = kMicroItemType_None;
	if(ppchItemOut)
		*ppchItemOut = NULL;
	
	pTok = strtok_s(pTok, ".", &context);
	while(pTok != NULL)
	{
		switch(i)
		{
			//Game Title
			case 0:
			{
				if(ppchGameTitle)
					*ppchGameTitle = pTok;
				break;
			}

			//Transaction Type
			case 1:
			{
				if(eType)
					*eType = StaticDefineIntGetInt(MicroItemTypeEnum,pTok);
				break;
			}

			//Item name
			case 2:
			{
				if(ppchItemOut)
					*ppchItemOut = pTok;
				iCount = 1;
				break;
			}

			//Item Count
			case 3:
			{
				iCount = atoi(pTok);
				break;
			}
			default:
				break;
		}

		++i;
		pTok = strtok_s(NULL, ".", &context);
	}

	return(iCount);
}

void MicroTrans_FormItemEstr(char **ppchItemOut, const char *pchGameTitle, MicroItemType eType, const char *pchItem, S32 iCount)
{
	if(iCount > 1)
	{
		estrPrintf(	ppchItemOut,
					"%s.%s.%s.%d",
					pchGameTitle,
					StaticDefineIntRevLookup(MicroItemTypeEnum, eType),
					pchItem,
					iCount);
	}
	else
	{
		estrPrintf(	ppchItemOut,
					"%s.%s.%s",
					pchGameTitle,
					StaticDefineIntRevLookup(MicroItemTypeEnum, eType),
					pchItem);
	}
}

static void MicroTrans_PopulateGADKeyName(char *pKeyBuffer, size_t iBufferSize, const char *pItemID)
{
	if (!pKeyBuffer[0])
		snprintf_s(pKeyBuffer, iBufferSize, "%s.%s", GetShortProductName(), pItemID);
}

static void MicroTrans_PopulateASKeyName(char *pKeyBuffer, size_t iBufferSize, const char *pItemID)
{
	if (!pKeyBuffer[0])
		snprintf_s(pKeyBuffer, iBufferSize, "$ENV:%s", pItemID);
}

const char *MicroTrans_GetCharSlotsKeyID(void)
{
	return allocAddString("ExtraCharacterSlots");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetCharSlotsGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetCharSlotsKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetCharSlotsASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetCharSlotsKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetSuperPremiumKeyID(void)
{
	return allocAddString("SuperPremium");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetSuperPremiumGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetSuperPremiumKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetSuperPremiumASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetSuperPremiumKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetCSRCharSlotsKeyID(void)
{
	return allocAddString("CSRCharSlots");
}

// The name of the GameAccountData key that Customer Support can use to give additional character slots.
AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetCSRCharSlotsGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetCSRCharSlotsKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetVirtualShardCharSlotsKeyID(void)
{
	return allocAddString("VirtualShard");
}

char *MicroTrans_GetVirtualShardCharSlotsGADKey(ContainerID iVirtualShardID)
{
	static char s_chKeyVal[64] = "";
	if (!s_chKeyVal[0])
	{
		sprintf(s_chKeyVal, "%s.%s_%d.%s", GetShortProductName(), MicroTrans_GetVirtualShardCharSlotsKeyID(), iVirtualShardID, MicroTrans_GetCharSlotsKeyID());
	}

	return s_chKeyVal;
}

const char *MicroTrans_GetCostumeSlotsKeyID(void)
{
	return allocAddString("ExtraCostumeSlots");
}

char *MicroTrans_GetCostumeSlotsGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetCostumeSlotsKeyID());
	return s_chKeyVal;
}

char *MicroTrans_GetCostumeSlotsASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetCostumeSlotsKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetExtraMaxAuctionsKeyID(void)
{
	return allocAddString("ExtraMaxAuctions");
}

char *MicroTrans_GetExtraMaxAuctionsGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetExtraMaxAuctionsKeyID());
	return s_chKeyVal;
}

char *MicroTrans_GetExtraMaxAuctionsASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetExtraMaxAuctionsKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetRespecTokensKeyID(void)
{
	return allocAddString("RespecTokens");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetRespecTokensGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetRespecTokensKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetRespecTokensASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetRespecTokensKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetRenameTokensKeyID(void)
{
	return allocAddString("RenameTokens");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetRenameTokensGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetRenameTokensKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetRenameTokensASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetRenameTokensKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetOfficerSlotsKeyID(void)
{
	return allocAddString("ExtraOfficerSlots");
}

char *MicroTrans_GetOfficerSlotsGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetOfficerSlotsKeyID());
	return s_chKeyVal;
}

char *MicroTrans_GetOfficerSlotsASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetOfficerSlotsKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetFreeCostumeChangeKeyID(void)
{
	return allocAddString("FreeCostumeChange");
}

char *MicroTrans_GetFreeCostumeChangeGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetFreeCostumeChangeKeyID());
	return s_chKeyVal;
}

char *MicroTrans_GetFreeCostumeChangeASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetFreeCostumeChangeKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetFreeShipCostumeChangeKeyID(void)
{
	return allocAddString("FreeShipCostumeChange");
}

char *MicroTrans_GetFreeShipCostumeChangeGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetFreeShipCostumeChangeKeyID());
	return s_chKeyVal;
}

char *MicroTrans_GetFreeShipCostumeChangeASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetFreeShipCostumeChangeKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetRetrainTokensKeyID(void)
{
	return allocAddString("RetrainTokens");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetRetrainTokensGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetRetrainTokensKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetRetrainTokensASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetRetrainTokensKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetItemAssignmentCompleteNowKeyID(void)
{
	return allocAddString("ItemAssignmentCompleteNow");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetItemAssignmentCompleteNowGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetItemAssignmentCompleteNowKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetItemAssignmentCompleteNowASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetItemAssignmentCompleteNowKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetItemAssignmentUnslotTokensKeyID(void)
{
	return allocAddString("ItemAssignmentUnslotToken");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetItemAssignmentUnslotTokensGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetItemAssignmentUnslotTokensKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetItemAssignmentUnslotTokensASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetItemAssignmentUnslotTokensKeyID());
	return s_chKeyVal;
}

const char *MicroTrans_GetSharedBankSlotKeyID(void)
{
	return allocAddString("SharedBankSlot");
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetSharedBankSlotGADKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateGADKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetSharedBankSlotKeyID());
	return s_chKeyVal;
}

AUTO_TRANS_HELPER_SIMPLE;
char *MicroTrans_GetSharedBankSlotASKey(void)
{
	static char s_chKeyVal[64] = "";
	MicroTrans_PopulateASKeyName(SAFESTR(s_chKeyVal), MicroTrans_GetSharedBankSlotKeyID());
	return s_chKeyVal;
}

#include "AutoGen/microtransactions_common_h_ast.c"
