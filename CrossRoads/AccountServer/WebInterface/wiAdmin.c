#include "wiAdmin.h"
#include "wiAdmin_c_ast.h"
#include "wiCommon.h"

#include "AccountServer.h"
#include "Discount.h"
#include "EString.h"
#include "KeyValues/KeyValueChain.h"
#include "KeyValues/VirtualCurrency.h"
#include "Money.h"
#include "rpn.h"
#include "rpn_h_ast.h"
#include "StringUtil.h"
#include "timing.h"
#include "url.h"

#include "VirtualCurrency_h_ast.h"

/************************************************************************/
/* Index                                                                */
/************************************************************************/

static void wiHandleAdminIndex(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Stats                                                                */
/************************************************************************/

static void wiHandleAdminStats(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Debug                                                                */
/************************************************************************/

static void wiHandleAdminDebug(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Discounts                                                            */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWIDiscountsForm {
	const char *pName; AST(ESTRING)
	const char *pProductInternalName; AST(ESTRING)
	const char *pProducts; AST(ESTRING)
	bool bBlacklistProducts;
	const char *pCategories; AST(ESTRING)
	bool bBlacklistCategories;
	const char *pStartTime; AST(ESTRING)
	const char *pEndTime; AST(ESTRING)
	const char *pCurrency; AST(ESTRING)
	const char *pPercentageDiscount; AST(ESTRING)
	const char *pKeyValuePrereqs; AST(ESTRING)
} ASWIDiscountsForm;

AUTO_STRUCT;
typedef struct ASWIDiscounts
{
	const char *pSelf; AST(UNOWNED)
	ASWIDiscountsForm form;
	EARRAY_OF(DiscountContainer) eaActiveDiscounts; AST(UNOWNED)
	EARRAY_OF(DiscountContainer) eaScheduledDiscounts; AST(UNOWNED)
	EARRAY_OF(DiscountContainer) eaInactiveDiscounts; AST(UNOWNED)
} ASWIDiscounts;

static void wiHandleAdminDiscounts_SaveDiscount(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
												SA_PARAM_NN_VALID ASWIDiscounts *paswiDiscounts)
{
	const char *pError = NULL;
	const char *pCurrency = NULL;
	const char *pProductInternalName = NULL;
	const char *pProductString = NULL;
	const char *pCategoryString = NULL;
	const char *pStartTimeString = NULL;
	const char *pEndTimeString = NULL;
	const char *pKeyValuePrereqs = NULL;
	const char *pName = NULL;
	char *pPercentageDiscount = NULL;
	U32 uDiscount = 0;
	U32 uStartSS2000 = 0;
	U32 uEndSS2000 = 0;
	bool bBlacklistProducts = false;
	bool bBlacklistCategories = false;

	if (!verify(pWebRequest)) return;
	if (!verify(paswiDiscounts)) return;

	PERFINFO_AUTO_START_FUNC();

	pCurrency = wiGetString(pWebRequest, "currency");
	pProductInternalName = wiGetString(pWebRequest, "productInternalName");
	pPercentageDiscount = strdup(wiGetString(pWebRequest, "percentageDiscount"));
	pKeyValuePrereqs = wiGetString(pWebRequest, "keyValuePrereqs");
	pName = wiGetString(pWebRequest, "name");
	pProductString = wiGetString(pWebRequest, "products");
	bBlacklistProducts = wiGetBool(pWebRequest, "blacklistProducts");
	pCategoryString = wiGetString(pWebRequest, "categories");
	bBlacklistCategories = wiGetBool(pWebRequest, "blacklistCategories");
	pStartTimeString = wiGetString(pWebRequest, "startTime");
	pEndTimeString = wiGetString(pWebRequest, "endTime");

	if (!pError && !*pCurrency)
	{
		pError = "Currency required.";
	}

	if (!pError && isRealCurrency(pCurrency))
	{
		pError = "Non-point currencies unsupported.";
	}

	if (!pError && !*pPercentageDiscount)
	{
		pError = "Percentage discount required.";
	}

	if (!pError && !*pKeyValuePrereqs)
	{
		pError = "Key-value prerequisites required.";
	}

	if (!pError)
	{
		unsigned int iCurChar = 0;
		int iPlacesAfterDecimal = 0;
		int iPlacesBeforeDecimal = 0;
		bool bFoundDecimal = false;

		// Strip percentage sign if possible
		if (pPercentageDiscount[strlen(pPercentageDiscount) - 1] == '%')
		{
			pPercentageDiscount[strlen(pPercentageDiscount) - 1] = '\0';
		}

		for (iCurChar = 0; iCurChar < strlen(pPercentageDiscount); iCurChar++)
		{
			char curChar = pPercentageDiscount[iCurChar];

			if (isdigit(curChar))
			{
				if (bFoundDecimal)
				{
					iPlacesAfterDecimal++;

					if (iPlacesAfterDecimal > 2)
					{
						pError = "Maximum 2 decimal places for percentage discount.";
						break;
					}
				}
				else
				{
					iPlacesBeforeDecimal++;

					if (iPlacesBeforeDecimal > 3)
					{
						pError = "Percentage discount too large.";
						break;
					}
				}
			}
			else if (curChar == '.' && !bFoundDecimal)
			{
				bFoundDecimal = true;
			}
			else
			{
				pError = "Invalid percentage discount.";
				break;
			}
		}

		if (!pError && bFoundDecimal && !iPlacesAfterDecimal)
		{
			pError = "Invalid percentage discount.";
		}

		if (!pError)
		{
			pPercentageDiscount[iPlacesBeforeDecimal] = '\0';

			uDiscount = atoi(pPercentageDiscount) * 100;

			if (bFoundDecimal)
			{
				U32 uDecimal = atoi(pPercentageDiscount + iPlacesBeforeDecimal + 1);

				if (iPlacesAfterDecimal == 1)
				{
					uDecimal *= 10;
				}

				uDiscount += uDecimal;
			}

			if (!uDiscount || uDiscount > 10000)
			{
				pError = "Invalid percentage discount.";
			}
		}
	}

	free(pPercentageDiscount);

	if (!pError)
	{
		STRING_EARRAY eaRPNStack = NULL;
		int iErrorIndex = 0;
		RPNParseResult eParseResult = infixToRPN(pKeyValuePrereqs, &eaRPNStack, &iErrorIndex);
		eaDestroyEString(&eaRPNStack);

		if (eParseResult != RPNPR_Success)
		{
			static char *pRPNError = NULL;

			estrPrintf(&pRPNError, "Prerequisites invalid: %s", StaticDefineIntRevLookup(RPNParseResultEnum, eParseResult));

			if (iErrorIndex != -1)
			{
				estrConcatf(&pRPNError, " (location: %i)", iErrorIndex);
			}

			pError = pRPNError;
		}
	}

	if (!pError && pStartTimeString && pStartTimeString[0] && !(uStartSS2000 = timeGetSecondsSince2000FromLocalDateString(pStartTimeString)))
	{
		pError = "Start time was not in YYYY-MM-DD HH:MM:SS format.";
	}

	if (!pError && pEndTimeString && pEndTimeString[0] && !(uEndSS2000 = timeGetSecondsSince2000FromLocalDateString(pEndTimeString)))
	{
		pError = "End time was not in YYYY-MM-DD HH:MM:SS format.";
	}

	if (!pError && !saveDiscount(pName, pCurrency, pProductInternalName, pKeyValuePrereqs, pProductString, bBlacklistProducts,
								 pCategoryString, bBlacklistCategories, uStartSS2000, uEndSS2000, uDiscount, wiGetUsername(pWebRequest)))
	{
		pError = "Could not save discount.";
	}

	if (pError)
	{
		paswiDiscounts->form.pCurrency = wiGetEscapedString(pWebRequest, "currency");
		paswiDiscounts->form.pProductInternalName = wiGetEscapedString(pWebRequest, "productInternalName");
		paswiDiscounts->form.pProducts = wiGetEscapedString(pWebRequest, "products");
		paswiDiscounts->form.bBlacklistProducts = wiGetBool(pWebRequest, "blacklistProducts");
		paswiDiscounts->form.pCategories = wiGetEscapedString(pWebRequest, "categories");
		paswiDiscounts->form.bBlacklistCategories = wiGetBool(pWebRequest, "blacklistCategories");
		paswiDiscounts->form.pStartTime = wiGetEscapedString(pWebRequest, "startTime");
		paswiDiscounts->form.pEndTime = wiGetEscapedString(pWebRequest, "endTime");
		paswiDiscounts->form.pPercentageDiscount = wiGetEscapedString(pWebRequest, "percentageDiscount");
		paswiDiscounts->form.pKeyValuePrereqs = wiGetEscapedString(pWebRequest, "keyValuePrereqs");
		paswiDiscounts->form.pName = wiGetEscapedString(pWebRequest, "name");

		wiAppendMessageBox(pWebRequest, "Error", pError, WMBF_Error);
	}
	else
	{
		wiAppendMessageBox(pWebRequest, "Success", "Discount saved.", 0);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void wiHandleAdminDiscounts(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	const char *pSelf = "discounts" WI_EXTENSION;
	ASWIDiscounts aswiDiscounts = {0};

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_ASWIDiscounts, &aswiDiscounts);

	aswiDiscounts.pSelf = pSelf;

	if (wiSubmitted(pWebRequest, "saveDiscount"))
	{
		wiHandleAdminDiscounts_SaveDiscount(pWebRequest, &aswiDiscounts);
	}
	else if (wiSubmitted(pWebRequest, "setEnabled"))
	{
		U32 uDiscountID = wiGetInt(pWebRequest, "id", 0);

		if (uDiscountID > 0)
		{
			setDiscountEnabled(uDiscountID, !wiGetBool(pWebRequest, "disable"));
			wiAppendMessageBox(pWebRequest, "Success", wiGetBool(pWebRequest, "disable") ? "Discount disabled." : "Discount enabled.", 0);
		}
	}
	else if (wiSubmitted(pWebRequest, "delete"))
	{
		U32 uDiscountID = wiGetInt(pWebRequest, "id", 0);

		if (uDiscountID > 0)
		{
			deleteDiscount(uDiscountID);
			wiAppendMessageBox(pWebRequest, "Success", "Discount deleted.", 0);
		}
	}

	aswiDiscounts.eaActiveDiscounts = getActiveDiscounts();
	aswiDiscounts.eaScheduledDiscounts = getScheduledDiscounts();
	aswiDiscounts.eaInactiveDiscounts = getInactiveDiscounts();

	wiAppendStruct(pWebRequest, "Discounts.cs", parse_ASWIDiscounts, &aswiDiscounts);

	freeDiscountsArray(&aswiDiscounts.eaActiveDiscounts);
	freeDiscountsArray(&aswiDiscounts.eaScheduledDiscounts);
	freeDiscountsArray(&aswiDiscounts.eaInactiveDiscounts);

	StructDeInit(parse_ASWIDiscounts, &aswiDiscounts);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Virtual currencies                                                   */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWICurrenciesForm
{
	const char *pName; AST(UNOWNED)
	const char *pGame; AST(UNOWNED)
	const char *pEnvironment; AST(UNOWNED)
	const char *pCreatedTime; AST(UNOWNED)
	const char *pDeprecatedTime; AST(UNOWNED)
	U32 uReportingID;
	VirtualCurrencyRevenueType eRevenueType;
	bool bIsChain;
	char *pChainParts; AST(ESTRING)
} ASWICurrenciesForm;

AUTO_STRUCT;
typedef struct ASWICurrencies
{
	const char *pSelf; AST(UNOWNED)
	ASWICurrenciesForm form;
	EARRAY_OF(const VirtualCurrency) eaCurrencies; AST(UNOWNED)
} ASWICurrencies;

static void wiHandleAdminCurrency_SaveCurrency(ASWebRequest *pWebRequest, ASWICurrencies *paswiCurrencies)
{
	const char *pName = wiGetString(pWebRequest, "name");
	const char *pGame = wiGetString(pWebRequest, "game");
	const char *pEnvironment = wiGetString(pWebRequest, "environment");
	const char *pCreated = wiGetString(pWebRequest, "created");
	const char *pDeprecated = wiGetString(pWebRequest, "deprecated");
	U32 uReportingID = wiGetInt(pWebRequest, "reportingid", 0);
	const char *pRevenueType = wiGetString(pWebRequest, "revenuetype");
	bool bIsChain = wiGetBool(pWebRequest, "ischain");
	const char *pChainParts = wiGetString(pWebRequest, "parts");

	U32 uCreatedTime = 0;
	U32 uDeprecatedTime = 0;
	char **ppChainParts = NULL;
	VirtualCurrencyRevenueType eRevenueType = VCRT_Promotional;

	const char *pError = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!pError && bIsChain)
	{
		if (nullStr(pChainParts))
		{
			pError = "Failed to specify parts for a chain currency.";
		}
		else
		{
			DivideString(pChainParts, ",", &ppChainParts, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_ESTRINGS);
		}
	}

	if (!pError && !nullStr(pCreated) && !(uCreatedTime = timeGetSecondsSince2000FromLocalDateString(pCreated)))
	{
		pError = "Creation time was not in YYYY-MM-DD HH:MM:SS format.";
	}

	if (!pError && !nullStr(pDeprecated) && !(uDeprecatedTime = timeGetSecondsSince2000FromLocalDateString(pDeprecated)))
	{
		pError = "Deprecation time was not in YYYY-MM-DD HH:MM:SS format.";
	}

	if (!pError)
	{
		if (nullStr(pRevenueType))
		{
			pError = "Somehow didn't specify a revenue type.";
		}
		else
		{
			eRevenueType = StaticDefineInt_FastStringToInt(VirtualCurrencyRevenueTypeEnum, pRevenueType, VCRT_Promotional);
		}
	}

	if (!pError)
	{
		if (!VirtualCurrency_UpdateCurrency(pName, pGame, pEnvironment, uCreatedTime, uReportingID, eRevenueType, bIsChain, ppChainParts))
		{
			pError = "Invalid currency details (most likely a chain referencing non-currency keys)";
		}
	}

	if (pError)
	{
		wiAppendMessageBox(pWebRequest, "Error", pError, WMBF_Error);

		paswiCurrencies->form.pName = pName;
		paswiCurrencies->form.pGame = pGame;
		paswiCurrencies->form.pEnvironment = pEnvironment;
		paswiCurrencies->form.pCreatedTime = pCreated;
		paswiCurrencies->form.pDeprecatedTime = pDeprecated;
		paswiCurrencies->form.uReportingID = uReportingID;
		paswiCurrencies->form.eRevenueType = eRevenueType;
		paswiCurrencies->form.bIsChain = bIsChain;
		paswiCurrencies->form.pChainParts = estrDup(pChainParts);
	}
	else
	{
		if (uDeprecatedTime) VirtualCurrency_DeprecateCurrency(pName, uDeprecatedTime);
		wiAppendMessageBox(pWebRequest, "Success", "Currency saved.", 0);
	}

	PERFINFO_AUTO_STOP();
}

static void wiHandleAdminCurrency_EditCurrency(ASWebRequest *pWebRequest, ASWICurrencies *paswiCurrencies)
{
	U32 uCurrencyID = wiGetInt(pWebRequest, "id", 0);
	VirtualCurrency *pCurrency = NULL;

	if (!uCurrencyID) return;

	pCurrency = VirtualCurrency_GetByID(uCurrencyID);

	if (!pCurrency) return;

	paswiCurrencies->form.pName = pCurrency->pName;
	paswiCurrencies->form.pGame = pCurrency->pGame;
	paswiCurrencies->form.pEnvironment = pCurrency->pEnvironment;
	paswiCurrencies->form.pCreatedTime = "";
	paswiCurrencies->form.pDeprecatedTime = "";
	paswiCurrencies->form.uReportingID = pCurrency->uReportingID;
	paswiCurrencies->form.eRevenueType = pCurrency->eRevenueType;
	paswiCurrencies->form.bIsChain = pCurrency->bIsChain;

	EARRAY_FOREACH_BEGIN(pCurrency->ppChainParts, iPart);
	{
		estrAppend2(&paswiCurrencies->form.pChainParts, pCurrency->ppChainParts[iPart]);

		if (iPart < eaSize(&pCurrency->ppChainParts) - 1)
		{
			estrAppend2(&paswiCurrencies->form.pChainParts, ", ");
		}
	}
	EARRAY_FOREACH_END;
}

static void wiHandleAdminCurrency(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	const char *pSelf = "currency" WI_EXTENSION;
	const char *pError = NULL;
	ASWICurrencies aswiCurrencies = {0};

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();
	if (wiSubmitted(pWebRequest, "saveCurrency"))
	{
		wiHandleAdminCurrency_SaveCurrency(pWebRequest, &aswiCurrencies);
	}
	else if (wiSubmitted(pWebRequest, "edit"))
	{
		wiHandleAdminCurrency_EditCurrency(pWebRequest, &aswiCurrencies);
	}
	else if (wiSubmitted(pWebRequest, "delete"))
	{
		U32 uCurrencyID = wiGetInt(pWebRequest, "id", 0);

		if (uCurrencyID)
		{
			VirtualCurrency_DeleteCurrency(uCurrencyID);
			wiAppendMessageBox(pWebRequest, "Success", "Currency deleted.", 0);
		}
	}

	aswiCurrencies.pSelf = pSelf;
	aswiCurrencies.eaCurrencies = VirtualCurrency_GetAll();
	wiAppendStruct(pWebRequest, "Currency.cs", parse_ASWICurrencies, &aswiCurrencies);
	eaDestroy(&aswiCurrencies.eaCurrencies);
	StructDeInit(parse_ASWICurrencies, &aswiCurrencies);
	PERFINFO_AUTO_STOP_FUNC();
}

/************************************************************************/
/* Blocked IPs                                                          */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWIIPRateLimitEntry
{
	U32 uBlockedUntilSS2000;
	const char * pIPAddress; AST(UNOWNED)
} ASWIIPRateLimitEntry;

AUTO_STRUCT;
typedef struct ASWIIPRateLimit
{
	EARRAY_OF(ASWIIPRateLimitEntry) eaIPRateLimit;
} ASWIIPRateLimit;

static void wiHandleAdminIPRateLimit(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	ASWIIPRateLimit aswiIPRateLimit = {0};
	RateLimitBlockedIter * pIter = NULL;

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_ASWIIPRateLimit, &aswiIPRateLimit);

	pIter = IPBlockedIterCreate();
	if (pIter)
	{
		const char * pIPAddress = NULL;
		while ((pIPAddress = IPBlockedIterNext(pIter)))
		{
			U32 uBlockedUntil = IPBlockedUntil(pIPAddress);
			if (uBlockedUntil)
			{
				ASWIIPRateLimitEntry * pEntry = StructCreate(parse_ASWIIPRateLimitEntry);
				pEntry->pIPAddress = pIPAddress;
				pEntry->uBlockedUntilSS2000 = uBlockedUntil;
				eaPush(&aswiIPRateLimit.eaIPRateLimit, pEntry);
			}
		}
		IPBlockedIterDestroy(pIter);
	}

	wiAppendStruct(pWebRequest, "IPRateLimit.cs", parse_ASWIIPRateLimit, &aswiIPRateLimit);

	StructDeInit(parse_ASWIIPRateLimit, &aswiIPRateLimit);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleAdmin(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	bool bHandled = false;

	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

#define WI_ADMIN_PAGE(page) \
	if (!stricmp_safe(wiGetPath(pWebRequest), WI_ADMIN_DIR #page WI_EXTENSION)) \
	{ \
		wiHandleAdmin##page(pWebRequest); \
		bHandled = true; \
	}

	WI_ADMIN_PAGE(Index);
	WI_ADMIN_PAGE(Stats);
	WI_ADMIN_PAGE(Debug);
	WI_ADMIN_PAGE(Discounts);
	WI_ADMIN_PAGE(Currency);
	WI_ADMIN_PAGE(IPRateLimit);

#undef WI_ADMIN_PAGE

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}

#include "wiAdmin_c_ast.c"