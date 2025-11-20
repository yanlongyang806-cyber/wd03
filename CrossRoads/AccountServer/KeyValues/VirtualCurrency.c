#include "VirtualCurrency.h"
#include "objContainer.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "stdtypes.h"
#include "StringUtil.h"

#include "VirtualCurrency_h_ast.h"

#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"

void VirtualCurrency_Init(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, parse_VirtualCurrency, NULL, NULL, NULL, NULL, NULL);
}

VirtualCurrency *VirtualCurrency_GetByID(U32 uID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, uID);
	if (!pContainer || !pContainer->containerData) return NULL;
	return pContainer->containerData;
}

VirtualCurrency *VirtualCurrency_GetByName(const char *pName)
{
	if (nullStr(pName)) return NULL;

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, pContainer);
	{
		VirtualCurrency *pCurrency = pContainer->containerData;

		if (!devassert(pCurrency)) continue;
		if (!stricmp(pName, pCurrency->pName)) return pCurrency;
	}
	CONTAINER_FOREACH_END;

	return NULL;
}

bool VirtualCurrency_Exists(const char *pName)
{
	return VirtualCurrency_GetByName(pName) != NULL;
}

bool VirtualCurrency_IsChain(const char *pName)
{
	VirtualCurrency *pCurrency = VirtualCurrency_GetByName(pName);
	return pCurrency && pCurrency->bIsChain;
}

void VirtualCurrency_GetChainParts(const char *pName, char ***eaKeys, bool bUnique)
{
	VirtualCurrency *pCurrency = VirtualCurrency_GetByName(pName);

	if (!pCurrency) return;
	if (!pCurrency->ppChainParts) return;

	EARRAY_FOREACH_BEGIN(pCurrency->ppChainParts, iPart);
	{
		bool bFound = false;
		const VirtualCurrency *pChildCurrency = VirtualCurrency_GetByName(pCurrency->ppChainParts[iPart]);

		if (pChildCurrency && pChildCurrency->bIsChain)
		{
			VirtualCurrency_GetChainParts(pChildCurrency->pName, eaKeys, bUnique);
			continue;
		}

		if (bUnique)
		{
			int i = 0;

			for (i = 0; i < eaSize(eaKeys); ++i)
			{
				if (!stricmp(pCurrency->ppChainParts[iPart], (*eaKeys)[i]))
				{
					bFound = true;
					break;
				}
			}
		}

		if (!bFound)
		{
			eaPush(eaKeys, strdup(pCurrency->ppChainParts[iPart]));
		}
	}
	EARRAY_FOREACH_END;
}

static void createVirtualCurrency(U32 uID, const char *pName, const char *pGame, const char *pEnvironment, U32 uCreatedTime, U32 uReportingID, VirtualCurrencyRevenueType eRevenueType, bool bIsChain, CONST_STRING_EARRAY ppChainParts)
{
	NOCONST(VirtualCurrency) newCurrency = {0};

	newCurrency.uID = uID;
	newCurrency.pName = StructAllocString(pName);
	newCurrency.pGame = StructAllocString(pGame);
	newCurrency.pEnvironment = StructAllocString(pEnvironment);
	newCurrency.uCreatedTime = uCreatedTime ? uCreatedTime : timeSecondsSince2000();
	newCurrency.uReportingID = uReportingID;
	newCurrency.eRevenueType = eRevenueType;
	newCurrency.bIsChain = bIsChain;
	newCurrency.ppChainParts = DECONST(char**, ppChainParts);

	objRequestContainerCreateLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, &newCurrency);
	newCurrency.ppChainParts = NULL;
	StructDeInitNoConst(parse_VirtualCurrency, &newCurrency);
}

void VirtualCurrency_DeleteCurrency(U32 uID)
{
	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, uID);
}

bool VirtualCurrency_UpdateCurrency(const char *pName,
	const char *pGame,
	const char *pEnvironment,
	U32 uCreatedTime,
	U32 uReportingID,
	VirtualCurrencyRevenueType eRevenueType,
	bool bIsChain,
	CONST_STRING_EARRAY ppChainParts)
{
	VirtualCurrency *pExistingCurrency = VirtualCurrency_GetByName(pName);
	U32 uID = 0;

	if (bIsChain)
	{
		EARRAY_FOREACH_BEGIN(ppChainParts, iPart);
		{
			if (!VirtualCurrency_GetByName(ppChainParts[iPart]))
			{
				return false;
			}
		}
		EARRAY_FOREACH_END;
	}

	if (pExistingCurrency)
	{
		uID = pExistingCurrency->uID;
		uCreatedTime = pExistingCurrency->uCreatedTime;
		VirtualCurrency_DeleteCurrency(pExistingCurrency->uID);
	}

	createVirtualCurrency(uID, pName, pGame, pEnvironment, uCreatedTime, uReportingID, eRevenueType, bIsChain, ppChainParts);
	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pCurrency, ".Udeprecatedtime");
enumTransactionOutcome trVirtualCurrency_DeprecateCurrency(ATR_ARGS, NOCONST(VirtualCurrency) *pCurrency, U32 uDeprecatedTime)
{
	pCurrency->uDeprecatedTime = uDeprecatedTime ? uDeprecatedTime : timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}

void VirtualCurrency_DeprecateCurrency(const char *pName, U32 uDeprecatedTime)
{
	VirtualCurrency *pExistingCurrency = VirtualCurrency_GetByName(pName);

	if (!pExistingCurrency) return;

	AutoTrans_trVirtualCurrency_DeprecateCurrency(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, pExistingCurrency->uID, uDeprecatedTime);
}

EARRAY_OF(const VirtualCurrency) VirtualCurrency_GetAll(void)
{
	EARRAY_OF(const VirtualCurrency) eaCurrencies = NULL;

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, pContainer);
	{
		const VirtualCurrency *pCurrency = pContainer->containerData;

		if (devassert(pCurrency)) eaPush(&eaCurrencies, pCurrency);
	}
	CONTAINER_FOREACH_END;

	return eaCurrencies;
}

bool VirtualCurrency_IsKeyInChain(const VirtualCurrency *pCurrency, const char *pKey)
{
	EARRAY_CONST_FOREACH_BEGIN(pCurrency->ppChainParts, iPart, iNumParts);
	{
		const char *pPart = pCurrency->ppChainParts[iPart];

		if (devassert(pPart) && !stricmp(pPart, pKey)) return true;
	}
	EARRAY_FOREACH_END;

	return false;
}

#include "VirtualCurrency_h_ast.c"