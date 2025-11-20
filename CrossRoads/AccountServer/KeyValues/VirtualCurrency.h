#pragma once

AUTO_ENUM;
typedef enum VirtualCurrencyRevenueType
{
	VCRT_Promotional,
	VCRT_Paid,
	VCRT_Ambiguous,
} VirtualCurrencyRevenueType;

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct VirtualCurrency
{
	const U32 uID;									AST(KEY)
	CONST_STRING_MODIFIABLE pName;

	CONST_STRING_MODIFIABLE pGame;
	CONST_STRING_MODIFIABLE pEnvironment;
	const U32 uCreatedTime;
	const U32 uDeprecatedTime;

	const U32 uReportingID;
	const VirtualCurrencyRevenueType eRevenueType;

	const bool bIsChain;
	CONST_STRING_EARRAY ppChainParts;
} VirtualCurrency;
AST_PREFIX()

void VirtualCurrency_Init(void);
EARRAY_OF(const VirtualCurrency) VirtualCurrency_GetAll(void);
VirtualCurrency *VirtualCurrency_GetByName(const char *pName);
VirtualCurrency *VirtualCurrency_GetByID(U32 uID);

bool VirtualCurrency_Exists(const char *pName);
bool VirtualCurrency_IsChain(const char *pName);
void VirtualCurrency_GetChainParts(const char *pName, char ***eaKeys, bool bUnique);
#define VirtualCurrency_GetUniqueChainParts(pName, eaKeys) VirtualCurrency_GetChainParts(pName, eaKeys, true)

bool VirtualCurrency_UpdateCurrency(
	const char *pName,
	const char *pGame,
	const char *pEnvironment,
	U32 uCreatedTime,
	U32 uReportingID,
	VirtualCurrencyRevenueType eRevenueType,
	bool bIsChain,
	CONST_STRING_EARRAY ppChainParts);
void VirtualCurrency_DeprecateCurrency(const char *pName, U32 uDeprecatedTime);
void VirtualCurrency_DeleteCurrency(U32 uID);
bool VirtualCurrency_IsKeyInChain(SA_PARAM_NN_VALID const VirtualCurrency *pCurrency, SA_PARAM_NN_STR const char *pKey);

#define KeyIsCurrency(pKey) VirtualCurrency_Exists(pKey)
#define CurrencyIsChain(pCurrency) VirtualCurrency_IsChain(pCurrency)
#define CurrencyGetChainParts(pCurrency, eaKeys) VirtualCurrency_GetUniqueChainParts(pCurrency, eaKeys)

#define CurrencyPopulateKeyList(pCurrency, eaKeys) (CurrencyIsChain(pCurrency) ? (CurrencyGetChainParts(pCurrency, eaKeys),1) : (eaPush(eaKeys, strdup(pCurrency)),1))