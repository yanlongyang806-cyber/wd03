#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameAccountData_h_ast.h"
#include "..\microtransactions_common.h"
#include "AutoGen\microtransactions_common_h_ast.h"
#include "AutoGen\accountnet_h_ast.h"

#include "Alerts.h"
#include "AutoTransDefs.h"
#include "objSchema.h"
#include "chatCommon.h"

#ifndef GAMECLIENT
#include "objTransactions.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_TRANS_HELPER
ATR_LOCKS(pData, "eaKeys[]");
SA_RET_OP_STR const char* gad_trh_GetAttribString(ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttrib)
{
	NOCONST(AttribValuePair) *pPair = NULL;
	if(NONNULL(pData))
		pPair = eaIndexedGetUsingString(&pData->eaKeys, pchAttrib);
	return(NONNULL(pPair) ? pPair->pchValue : NULL);
}

SA_RET_OP_STR const char* gad_GetAttribStringFromExtract(GameAccountDataExtract *pExtract, const char *pchAttrib)
{
	AttribValuePair *pPair = NULL;
	if(pExtract)
		pPair = eaIndexedGetUsingString(&pExtract->eaKeys, pchAttrib);
	return (pPair ? pPair->pchValue : NULL);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, "eaKeys[]");
S32 gad_trh_GetAttribInt(ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttrib)
{
	NOCONST(AttribValuePair) *pPair = NULL;
	if(NONNULL(pData))
		pPair = eaIndexedGetUsingString(&pData->eaKeys, pchAttrib);
	return(NONNULL(pPair) && NONNULL(pPair->pchValue) ? atoi(pPair->pchValue) : 0);
}

S32 gad_GetAttribIntFromExtract(GameAccountDataExtract *pExtract, const char *pchAttrib)
{
	AttribValuePair *pPair = NULL;
	if(pExtract)
		pPair = eaIndexedGetUsingString(&pExtract->eaKeys, pchAttrib);
	return (pPair && pPair->pchValue ? atoi(pPair->pchValue) : 0);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eavanitypets");
const char* gad_trh_GetVanityPet(ATH_ARG NOCONST(GameAccountData) *pData, const char *pchPetToFind)
{
	S32 iPetIdx = -1;
	if(NONNULL(pData))
		iPetIdx = eaFindString(&pData->eaVanityPets, pchPetToFind);
	return(iPetIdx >= 0 ? pData->eaVanityPets[iPetIdx] : NULL );
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, "eaCostumeKeys[]");
const char* gad_trh_GetCostumeRef(ATH_ARG  NOCONST(GameAccountData) *pData, const char *pchCostumeRef)
{
	NOCONST(AttribValuePair) *pPair = NULL;
	if(NONNULL(pData))
		pPair = eaIndexedGetUsingString(&pData->eaCostumeKeys, pchCostumeRef);
	return(NONNULL(pPair) ? pPair->pchValue : NULL);
}

AUTO_TRANS_HELPER_SIMPLE;
static NOCONST(AttribValuePair) *slGAD_trh_MakeAVPair(const char *pchKey, const char *pchValue)
{
	NOCONST(AttribValuePair) *pPair = StructCreateNoConst(parse_AttribValuePair);
	pPair->pchAttribute = StructAllocString(pchKey);
	pPair->pchValue = StructAllocString(pchValue);
	return pPair;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
static enumTransactionOutcome slGAD_trh_SetAttrib_Internal(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, const char *pchValue)
{
	NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&pData->eaKeys, pchKey);
	if (pPair)
	{
		StructFreeString(pPair->pchValue);
		pPair->pchValue = StructAllocString(pchValue);
	}
	else
	{
		pPair = slGAD_trh_MakeAVPair(pchKey, pchValue);
		eaIndexedPushUsingStringIfPossible(&pData->eaKeys, pchKey, pPair);
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Game account data key %s successfully set to %s", pchKey, pchValue);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
enumTransactionOutcome slGAD_trh_SetAttrib(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, const char *pchValue)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	return slGAD_trh_SetAttrib_Internal(ATR_PASS_ARGS, pData, pchKey, pchValue);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
enumTransactionOutcome slGAD_trh_SetAttrib_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, const char *pchValue)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return slGAD_trh_SetAttrib_Internal(ATR_PASS_ARGS, pData, pchKey, pchValue);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
static enumTransactionOutcome slGAD_trh_UnlockCostumeItem_Internal(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeItem)
{
	NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&pData->eaKeys, pchCostumeItem);
	if (!pPair)
	{
		//Push the new costume item ref onto the costume keys stack
		pPair = slGAD_trh_MakeAVPair(pchCostumeItem, "1");
		eaIndexedPushUsingStringIfPossible(&pData->eaKeys, pchCostumeItem, pPair);

		//Increment the version
		pData->iVersion++;

		TRANSACTION_RETURN_LOG_SUCCESS("Costume item %s now unlocked.", pchCostumeItem);
	}
	else
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Costume item %s was already unlocked.", pchCostumeItem);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
enumTransactionOutcome slGAD_trh_UnlockCostumeItem(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeItem)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	return slGAD_trh_UnlockCostumeItem_Internal(ATR_PASS_ARGS, pData, pchCostumeItem);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
enumTransactionOutcome slGAD_trh_UnlockCostumeItem_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeItem)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return slGAD_trh_UnlockCostumeItem_Internal(ATR_PASS_ARGS, pData, pchCostumeItem);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eacostumekeys[]");
static enumTransactionOutcome slGAD_trh_UnlockCostumeRef_Internal(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeRef)
{
	NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&pData->eaCostumeKeys, pchCostumeRef);
	if (!pPair)
	{
		//Push the new costume item ref onto the costume keys stack
		pPair = slGAD_trh_MakeAVPair(pchCostumeRef, "1");
		eaIndexedPushUsingStringIfPossible(&pData->eaCostumeKeys, pchCostumeRef, pPair);

		//Increment the version
		pData->iVersion++;

		TRANSACTION_RETURN_LOG_SUCCESS("Costume ref %s now unlocked.", pchCostumeRef);
	}
	else
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Costume ref %s was already unlocked.", pchCostumeRef);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eacostumekeys[]");
enumTransactionOutcome slGAD_trh_UnlockCostumeRef(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeRef)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	return slGAD_trh_UnlockCostumeRef_Internal(ATR_PASS_ARGS, pData, pchCostumeRef);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eacostumekeys[]");
enumTransactionOutcome slGAD_trh_UnlockCostumeRef_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeRef)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return slGAD_trh_UnlockCostumeRef_Internal(ATR_PASS_ARGS, pData, pchCostumeRef);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
static enumTransactionOutcome slGAD_trh_UnlockSpecies_Internal(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, SpeciesUnlock *pSpecies, const char *pchSpeciesUnlockCode)
{
	NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&pData->eaKeys, pchSpeciesUnlockCode);
	if (!pPair)
	{
		//Push the new costume item ref onto the costume keys stack

		pPair = slGAD_trh_MakeAVPair(pchSpeciesUnlockCode, "1");
		eaIndexedPushUsingStringIfPossible(&pData->eaKeys, pchSpeciesUnlockCode, pPair);

		//Increment the version
		pData->iVersion++;

		TRANSACTION_RETURN_LOG_SUCCESS("Species %s [Code %s] now unlocked.", pSpecies->pchSpeciesName, pSpecies->pchSpeciesUnlockCode);
	}
	else
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Species %s [Code %s] was already unlocked.",  pSpecies->pchSpeciesName, pSpecies->pchSpeciesUnlockCode);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
enumTransactionOutcome slGAD_trh_UnlockSpecies(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, SpeciesUnlock *pSpecies, const char *pchSpeciesUnlockCode)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	return slGAD_trh_UnlockSpecies_Internal(ATR_PASS_ARGS, pData, pSpecies, pchSpeciesUnlockCode);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
enumTransactionOutcome slGAD_trh_UnlockSpecies_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, SpeciesUnlock *pSpecies, const char *pchSpeciesUnlockCode)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return slGAD_trh_UnlockSpecies_Internal(ATR_PASS_ARGS, pData, pSpecies, pchSpeciesUnlockCode);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
static enumTransactionOutcome slGAD_trh_UnlockAttribValue_Internal(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttribValue)
{
	NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&pData->eaKeys, pchAttribValue);
	if (!pPair)
	{
		//Push the new costume item ref onto the costume keys stack

		pPair = slGAD_trh_MakeAVPair(pchAttribValue, "1");
		eaIndexedPushUsingStringIfPossible(&pData->eaKeys, pchAttribValue, pPair);

		//Increment the version
		pData->iVersion++;

		TRANSACTION_RETURN_LOG_SUCCESS("Attrib value %s now unlocked.", pchAttribValue);
	}
	else
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Attrib value %s was already unlocked.", pchAttribValue);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
enumTransactionOutcome slGAD_trh_UnlockAttribValue(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttribValue)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);
	return slGAD_trh_UnlockAttribValue_Internal(ATR_PASS_ARGS, pData, pchAttribValue);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iversion, .Eakeys[]");
enumTransactionOutcome slGAD_trh_UnlockAttribValue_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttribValue)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return slGAD_trh_UnlockAttribValue_Internal(ATR_PASS_ARGS, pData, pchAttribValue);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
static bool slGAD_trh_ChangeAttrib_Internal(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange, S32 iMin, S32 iMax, bool bCheckRange)
{
	NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&pData->eaKeys, pchKey);
	char pchVal[16];
	S32 iValue = 0;
	pchVal[15] = '\0';

	if (pPair && pPair->pchValue)
	{
		iValue = atoi(pPair->pchValue);
	}

	//Make the change
	iValue += iChange;
	if(bCheckRange && (iValue > iMax || iValue < iMin))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Couldn't change the attrib %s, %d by %d.  It breached the min %d or max %d",
			pchKey,
			(pPair && pPair->pchValue) ? atoi(pPair->pchValue) : 0,
			iChange,
			iMin,
			iMax);
		return(false);
	}
	snprintf(pchVal, 15, "%d", iValue);

	if (pPair)
	{
		StructFreeString(pPair->pchValue);
		pPair->pchValue = StructAllocString(pchVal);
	}
	else
	{
		pPair = slGAD_trh_MakeAVPair(pchKey, pchVal);
		eaIndexedPushUsingStringIfPossible(&pData->eaKeys, pchKey, pPair);
	}

	return(true);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
bool slGAD_trh_ChangeAttribClamped(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange, S32 iMin, S32 iMax)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);
	return slGAD_trh_ChangeAttrib_Internal(ATR_PASS_ARGS, pData, pchKey, iChange, iMin, iMax, true);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
bool slGAD_trh_ChangeAttribClamped_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange, S32 iMin, S32 iMax)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return slGAD_trh_ChangeAttrib_Internal(ATR_PASS_ARGS, pData, pchKey, iChange, iMin, iMax, true);
}

bool slGAD_CanChangeAttribClampedExtract(GameAccountDataExtract *pExtract, const char *pchKey, S32 iChange, S32 iMin, S32 iMax)
{
	AttribValuePair *pPair = eaIndexedGetUsingString(&pExtract->eaKeys, pchKey);
	S32 iValue = 0;

	if (pPair && pPair->pchValue)
	{
		iValue = atoi(pPair->pchValue);
	}

	iValue += iChange;
	if (iValue > iMax || iValue < iMin)
	{
		return false;
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
bool slGAD_trh_ChangeAttrib(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(false);
	return slGAD_trh_ChangeAttrib_Internal(ATR_PASS_ARGS, pData, pchKey, iChange, 0, 0, false);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Eakeys[]");
bool slGAD_trh_ChangeAttrib_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange)
{
	// "Force" causes us to ignore gConf.bDontAllowGADModification
	return slGAD_trh_ChangeAttrib_Internal(ATR_PASS_ARGS, pData, pchKey, iChange, 0, 0, false);
}

#include "GameAccountData_h_ast.c"