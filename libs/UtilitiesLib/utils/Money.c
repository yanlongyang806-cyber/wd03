#include "Money.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <wininclude.h>

#include "EString.h"
#include "StashTable.h"
#include "stdtypes.h"
#include "StringUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

/************************************************************************/
/* Money, currency, and prices                                          */
/************************************************************************/

#define INTERNAL_AMOUNT(MONEY) ((MONEY).Internal._internal_SubdividedAmount)
#define INTERNAL_CURRENCY(MONEY) ((MONEY).Internal._internal_Currency)

#ifdef _XBOX
#define LANG_KYRGYZ LANG_NEUTRAL
#define LANG_MONGOLIAN LANG_NEUTRAL
#endif

// Table of information for each ISO 4217 real currency.
// This depends on Windows NLS to actually do the locale-specific part of the currency formatting.
// To remove that dependency, this table should be extended to be something similar to POSIX <langinfo.h>
// and each currency should be individually researched for proper formatting.
struct CurrencyData {
	const char *pAbbreviation;				// ISO 4217 abbreviation
	const char *pDescription;				// English language currency name
	size_t uDigits;							// Number of decimal digits in the subdivided part, if any.
	WORD wLang;								// Windows language ID to format this currency with
	WORD wSublang;							// Windows sublanguage ID to format this currency with
} cdCurrencyTable[] = {
#ifndef _PS3
	{"ADP", "Andorran Peseta", 0, LANG_CATALAN, SUBLANG_NEUTRAL},
	{"AED", "UAE Dirham", 2, LANG_ARABIC, SUBLANG_ARABIC_UAE},
	{"AFA", "Afghani", 2, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"ALL", "Lek", 2, LANG_ALBANIAN, SUBLANG_DEFAULT},
	{"AMD", "Armenian Dram", 2, LANG_ARMENIAN, SUBLANG_DEFAULT},
	{"ANG", "Netherlands Antillian Guilder", 2, LANG_DUTCH, SUBLANG_DEFAULT},
	{"AOA", "Kwanza", 2, LANG_PORTUGUESE, SUBLANG_NEUTRAL},
	{"ARS", "Argentine Peso", 2, LANG_SPANISH, SUBLANG_SPANISH_ARGENTINA},
	{"ATS", "Schilling", 2, LANG_GERMAN, SUBLANG_GERMAN_AUSTRIAN},
	{"AUD", "Australian Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_AUS},
	{"AWG", "Aruban Guilder", 2, LANG_DUTCH, SUBLANG_NEUTRAL},
	{"AZM", "Azerbaijanian Manat", 2, LANG_AZERI, SUBLANG_AZERI_LATIN},
	{"BAM", "Convertible Marks", 2, LANG_SERBIAN, SUBLANG_SERBIAN_LATIN},
	{"BBD", "Barbados Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_CARIBBEAN},
	{"BDT", "Taka", 2, LANG_BENGALI, SUBLANG_DEFAULT},
	{"BEF", "Belgian Franc", 0, LANG_DUTCH, SUBLANG_DUTCH_BELGIAN},
	{"BGL", "Lev", 2, LANG_BULGARIAN, SUBLANG_DEFAULT},
	{"BGN", "Bulgarian Lev", 2, LANG_BULGARIAN, SUBLANG_DEFAULT},
	{"BHD", "Bahraini Dinar", 3, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"BIF", "Burundi Franc", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"BMD", "Bermuda Dollar", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"BND", "Brunei Dollar", 2, LANG_MALAY, SUBLANG_MALAY_BRUNEI_DARUSSALAM},
	{"BOB", "Boliviano", 2, LANG_SPANISH, SUBLANG_SPANISH_BOLIVIA},
	{"BOV", "Mvdol", 2, LANG_SPANISH, SUBLANG_SPANISH_BOLIVIA},
	{"BRL", "Brazilian Real", 2, LANG_PORTUGUESE, SUBLANG_PORTUGUESE_BRAZILIAN},
	{"BSD", "Bahamian Dollar", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"BTN", "Ngultrum", 2, LANG_NEUTRAL, SUBLANG_NEUTRAL},
	{"BWP", "Pula", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"BYB", "Belarussian Ruble", 0, LANG_BELARUSIAN, SUBLANG_DEFAULT},
	{"BYR", "Belarussian Ruble", 0, LANG_BELARUSIAN, SUBLANG_DEFAULT},
	{"BZD", "Belize Dollar", 2, LANG_SPANISH, SUBLANG_ENGLISH_BELIZE},
	{"CAD", "Canadian Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_CAN},
	{"CDF", "Franc Congolais", 2, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"CHF", "Swiss Franc", 2, LANG_GERMAN, SUBLANG_GERMAN_SWISS},
	{"CLF", "Unidades de fomento", 0, LANG_SPANISH, SUBLANG_SPANISH_CHILE},
	{"CLP", "Chilean Peso", 0, LANG_SPANISH, SUBLANG_SPANISH_CHILE},
	{"CNY", "Yuan Renminbi", 2, LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED},
	{"COP", "Colombian Peso", 2, LANG_SPANISH, SUBLANG_SPANISH_COLOMBIA},
	{"CRC", "Costa Rican Colon", 2, LANG_SPANISH, SUBLANG_SPANISH_COSTA_RICA},
	{"CUP", "Cuban Peso", 2, LANG_SPANISH, SUBLANG_NEUTRAL},
	{"CVE", "Cape Verde Escudo", 2, LANG_PORTUGUESE, SUBLANG_NEUTRAL},
	{"CYP", "Cyprus Pound", 2, LANG_GREEK, SUBLANG_NEUTRAL},
	{"CZK", "Czech Koruna", 2, LANG_CZECH, SUBLANG_DEFAULT},
	{"DEM", "Deutsche Mark", 2, LANG_GERMAN, SUBLANG_DEFAULT},
	{"DJF", "Djibouti Franc", 0, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"DKK", "Danish Krone", 2, LANG_DANISH, SUBLANG_DEFAULT},
	{"DOP", "Dominican Peso", 2, LANG_SPANISH, SUBLANG_SPANISH_DOMINICAN_REPUBLIC},
	{"DZD", "Algerian Dinar", 2, LANG_ARABIC, SUBLANG_ARABIC_ALGERIA},
	{"EEK", "Kroon", 2, LANG_ESTONIAN, SUBLANG_DEFAULT},
	{"EGP", "Egyptian Pound", 2, LANG_ARABIC, SUBLANG_ARABIC_EGYPT},
	{"ERN", "Nakfa", 2, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"ESP", "Spanish Peseta", 0, LANG_SPANISH, SUBLANG_SPANISH},
	{"ETB", "Ethiopian Birr", 2, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"EUR", "Euro", 2, LANG_GERMAN, SUBLANG_GERMAN},
	{"FIM", "Markka", 2, LANG_SWEDISH, SUBLANG_SWEDISH_FINLAND},
	{"FJD", "Fiji Dollar", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"FKP", "Falkland Islands Pound", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"FRF", "French Franc", 2, LANG_FRENCH, SUBLANG_FRENCH},
	{"GBP", "Pound Sterling", 2, LANG_ENGLISH, SUBLANG_ENGLISH_UK},
	{"GEL", "Lari", 2, LANG_GEORGIAN, SUBLANG_DEFAULT},
	{"GHC", "Cedi", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"GIP", "Gibraltar Pound", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"GMD", "Dalasi", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"GNF", "Guinea Franc", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"GRD", "Drachma", 0, LANG_GREEK, SUBLANG_DEFAULT},
	{"GTQ", "Quetzal", 2, LANG_SPANISH, SUBLANG_SPANISH_GUATEMALA},
	{"GWP", "Guinea-Bissau Peso", 2, LANG_PORTUGUESE, SUBLANG_NEUTRAL},
	{"GYD", "Guyana Dollar", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"HKD", "Hong Kong Dollar", 2, LANG_CHINESE, SUBLANG_CHINESE_HONGKONG},
	{"HNL", "Lempira", 2, LANG_SPANISH, SUBLANG_SPANISH_HONDURAS},
	{"HRK", "Croatian kuna", 2, LANG_CROATIAN, SUBLANG_DEFAULT},
	{"HTG", "Gourde", 2, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"HUF", "Forint", 2, LANG_HUNGARIAN, SUBLANG_DEFAULT},
	{"IDR", "Rupiah", 2, LANG_INDONESIAN, SUBLANG_DEFAULT},
	{"IEP", "Irish Pound", 2, LANG_ENGLISH, SUBLANG_ENGLISH_EIRE},
	{"ILS", "New Israeli Sheqel", 2, LANG_HEBREW, SUBLANG_DEFAULT},
	{"INR", "Indian Rupee", 2, LANG_HINDI, SUBLANG_DEFAULT},
	{"IQD", "Iraqi Dinar", 3, LANG_ARABIC, SUBLANG_ARABIC_IRAQ},
	{"IRR", "Iranian Rial", 2, LANG_FARSI, SUBLANG_DEFAULT},
	{"ISK", "Iceland Krona", 2, LANG_ICELANDIC, SUBLANG_DEFAULT},
	{"ITL", "Italian Lira", 0, LANG_ITALIAN, SUBLANG_ITALIAN},
	{"JMD", "Jamaican Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_JAMAICA},
	{"JOD", "Jordanian Dinar", 3, LANG_ARABIC, SUBLANG_ARABIC_JORDAN},
	{"JPY", "Yen", 0, LANG_JAPANESE, SUBLANG_DEFAULT},
	{"KES", "Kenyan Shilling", 2, LANG_SWAHILI, SUBLANG_NEUTRAL},
	{"KGS", "Som", 2, LANG_KYRGYZ, SUBLANG_DEFAULT},
	{"KHR", "Riel", 2, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"KMF", "Comoro Franc", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"KPW", "North Korean Won", 2, LANG_KOREAN, SUBLANG_NEUTRAL},
	{"KRW", "Won", 0, LANG_KOREAN, SUBLANG_KOREAN},
	{"KWD", "Kuwaiti Dinar", 3, LANG_ARABIC, SUBLANG_ARABIC_KUWAIT},
	{"KYD", "Cayman Islands Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_CARIBBEAN},
	{"KZT", "Tenge", 2, LANG_KAZAK, SUBLANG_DEFAULT},
	{"LAK", "Kip", 2, LANG_NEUTRAL, SUBLANG_NEUTRAL},
	{"LBP", "Lebanese Pound", 2, LANG_ARABIC, SUBLANG_ARABIC_LEBANON},
	{"LKR", "Sri Lanka Rupee", 2, LANG_TAMIL, SUBLANG_DEFAULT},
	{"LRD", "Liberian Dollar", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"LSL", "Loti", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"LTL", "Lithuanian Litus", 2, LANG_LITHUANIAN, SUBLANG_LITHUANIAN},
	{"LUF", "Luxembourg Franc", 0, LANG_GERMAN, SUBLANG_GERMAN_LUXEMBOURG},
	{"LVL", "Latvian Lats", 2, LANG_LATVIAN, SUBLANG_DEFAULT},
	{"LYD", "Libyan Dinar", 3, LANG_ARABIC, SUBLANG_ARABIC_LIBYA},
	{"MAD", "Moroccan Dirham", 2, LANG_ARABIC, SUBLANG_ARABIC_MOROCCO},
	{"MDL", "Moldovan Leu", 2, LANG_ROMANIAN, SUBLANG_NEUTRAL},
	{"MGF", "Malagasy Franc", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"MKD", "Denar", 2, LANG_MACEDONIAN, SUBLANG_DEFAULT},
	{"MMK", "Kyat", 2, LANG_NEUTRAL, SUBLANG_NEUTRAL},
	{"MNT", "Tugrik", 2, LANG_MONGOLIAN, SUBLANG_DEFAULT},
	{"MOP", "Pataca", 2, LANG_CHINESE, SUBLANG_CHINESE_MACAU},
	{"MRO", "Ouguiya", 2, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"MTL", "Maltese Lira", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"MUR", "Mauritius Rupee", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"MVR", "Rufiyaa", 2, LANG_NEUTRAL, SUBLANG_NEUTRAL},
	{"MWK", "Kwacha", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"MXN", "Mexican Peso", 2, LANG_SPANISH, SUBLANG_SPANISH_MEXICAN},
	{"MXV", "Mexican Unidad de Inversion (UDI)", 2, LANG_SPANISH, SUBLANG_SPANISH_MEXICAN},
	{"MYR", "Malaysian Ringgit", 2, LANG_MALAY, SUBLANG_MALAY_MALAYSIA},
	{"MZM", "Metical", 2, LANG_PORTUGUESE, SUBLANG_NEUTRAL},
	{"NAD", "Namibia Dollar", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"NGN", "Naira", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"NIO", "Cordoba Oro", 2, LANG_SPANISH, SUBLANG_SPANISH_NICARAGUA},
	{"NLG", "Netherlands Guilder", 2, LANG_DUTCH, SUBLANG_DUTCH},
	{"NOK", "Norwegian Krone", 2, LANG_NORWEGIAN, SUBLANG_DEFAULT},
	{"NPR", "Nepalese Rupee", 2, LANG_NEPALI, SUBLANG_DEFAULT},
	{"NZD", "New Zealand Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_NZ},
	{"OMR", "Rial Omani", 3, LANG_ARABIC, SUBLANG_ARABIC_OMAN},
	{"PAB", "Balboa", 2, LANG_SPANISH, SUBLANG_SPANISH_PANAMA},
	{"PEN", "Nuevo Sol", 2, LANG_SPANISH, SUBLANG_SPANISH_PERU},
	{"PGK", "Kina", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"PHP", "Philippine Peso", 2, LANG_ENGLISH, SUBLANG_ENGLISH_PHILIPPINES},
	{"PKR", "Pakistan Rupee", 2, LANG_URDU, SUBLANG_URDU_PAKISTAN},
	{"PLN", "Zloty", 2, LANG_POLISH, SUBLANG_DEFAULT},
	{"PTE", "Portuguese Escudo", 0, LANG_PORTUGUESE, SUBLANG_PORTUGUESE},
	{"PYG", "Guarani", 0, LANG_SPANISH, SUBLANG_SPANISH_PARAGUAY},
	{"QAR", "Qatari Rial", 2, LANG_ARABIC, SUBLANG_ARABIC_QATAR},
	{"ROL", "Leu", 2, LANG_ROMANIAN, SUBLANG_DEFAULT},
	{"RUB", "Russian Ruble", 2, LANG_RUSSIAN, SUBLANG_DEFAULT},
	{"RUR", "Russian Ruble", 2, LANG_RUSSIAN, SUBLANG_DEFAULT},
	{"RWF", "Rwanda Franc", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"SAR", "Saudi Riyal", 2, LANG_ARABIC, SUBLANG_ARABIC_SAUDI_ARABIA},
	{"SBD", "Solomon Islands Dollar", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"SCR", "Seychelles Rupee", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"SDD", "Sudanese Dinar", 2, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"SEK", "Swedish Krona", 2, LANG_SWEDISH, SUBLANG_SWEDISH},
	{"SGD", "Singapore Dollar", 2, LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE},
	{"SHP", "Saint Helena Pound", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"SIT", "Tolar", 2, LANG_SLOVENIAN, SUBLANG_DEFAULT},
	{"SKK", "Slovak Koruna", 2, LANG_SLOVAK, SUBLANG_DEFAULT},
	{"SLL", "Leone", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"SOS", "Somali Shilling", 2, LANG_ARABIC, SUBLANG_NEUTRAL},
	{"SRG", "Suriname Guilder", 2, LANG_DUTCH, SUBLANG_NEUTRAL},
	{"STD", "Dobra", 2, LANG_PORTUGUESE, SUBLANG_NEUTRAL},
	{"SVC", "El Salvador Colon", 2, LANG_SPANISH, SUBLANG_SPANISH_EL_SALVADOR},
	{"SYP", "Syrian Pound", 2, LANG_ARABIC, SUBLANG_ARABIC_SYRIA},
	{"SZL", "Lilangeni", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"THB", "Baht", 2, LANG_THAI, SUBLANG_DEFAULT},
	{"TJR", "Tajik Ruble", 0, LANG_NEUTRAL, SUBLANG_NEUTRAL},
	{"TMM", "Manat", 2, LANG_RUSSIAN, SUBLANG_NEUTRAL},
	{"TND", "Tunisian Dinar", 3, LANG_ARABIC, SUBLANG_ARABIC_TUNISIA},
	{"TOP", "Pa’anga", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"TPE", "Timor Escudo", 0, LANG_PORTUGUESE, SUBLANG_NEUTRAL},
	{"TRL", "Turkish Lira", 0, LANG_TURKISH, SUBLANG_DEFAULT},
	{"TTD", "Trinidad and Tobago Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_TRINIDAD},
	{"TWD", "New Taiwan Dollar", 2, LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL},
	{"TZS", "Tanzanian Shilling", 2, LANG_SWAHILI, SUBLANG_NEUTRAL},
	{"UAH", "Hryvnia", 2, LANG_UKRAINIAN, SUBLANG_DEFAULT},
	{"UGX", "Uganda Shilling", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"USD", "US Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_US},
	{"UYU", "Peso Uruguayo", 2, LANG_SPANISH, SUBLANG_SPANISH_URUGUAY},
	{"UZS", "Uzbekistan Sum", 2, LANG_UZBEK, SUBLANG_UZBEK_LATIN},
	{"VEB", "Bolivar", 2, LANG_SPANISH, SUBLANG_SPANISH_VENEZUELA},
	{"VND", "Dong", 2, LANG_VIETNAMESE, SUBLANG_DEFAULT},
	{"VUV", "Vatu", 0, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"WST", "Tala", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"XAF", "CFA Franc BEAC", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"XCD", "East Caribbean Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_CARIBBEAN},
	{"XOF", "CFA Franc BCEAO", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"XPF", "CFP Franc", 0, LANG_FRENCH, SUBLANG_NEUTRAL},
	{"XXX", "Invalid currency", 0, LANG_NEUTRAL, SUBLANG_NEUTRAL},
	{"YER", "Yemeni Rial", 2, LANG_ARABIC, SUBLANG_ARABIC_YEMEN},
	{"YUM", "Yugoslavian Dinar", 2, LANG_SERBIAN, SUBLANG_SERBIAN_LATIN},
	{"ZAR", "Rand", 2, LANG_ENGLISH, SUBLANG_ENGLISH_SOUTH_AFRICA},
	{"ZMK", "Kwacha", 2, LANG_ENGLISH, SUBLANG_NEUTRAL},
	{"ZWD", "Zimbabwe Dollar", 2, LANG_ENGLISH, SUBLANG_ENGLISH_ZIMBABWE}
#endif  // _PS3
};

StashTable stCurrencyTable;

static void InitTableIfNeeded()
{
	if (!stCurrencyTable)
	{
		size_t i;
		stCurrencyTable = stashTableCreateWithStringKeys(373, StashDefault);
		for (i = 0; i != sizeof(cdCurrencyTable)/sizeof(*cdCurrencyTable); ++i)
			stashAddPointer(stCurrencyTable, cdCurrencyTable[i].pAbbreviation, &cdCurrencyTable[i], true);
	}
}

// Get the currency data table entry for a specific currency.
const struct CurrencyData *GetCurrencyData(const char *pAbbreviation)
{
	void *result;
	InitTableIfNeeded();
	stashFindPointer(stCurrencyTable, pAbbreviation, &result);
	return result;
}

// Use the minimum value as an overflow value.
#define MONEY_INVALID_VALUE LLONG_MIN

// Return true if addition would cause an overflow.
static bool AdditionOverflows(S64 lhs, S64 rhs)
{
	// Check if an operand has already overflowed.
	if (lhs == MONEY_INVALID_VALUE || rhs == MONEY_INVALID_VALUE)
		return true;

	return false;  // TODO FIXME alaframboise
}

// Return true if multiplication would cause an overflow.
static bool MultiplicationOverflows(S64 lhs, double rhs)
{
	// Check if an operand has already overflowed.
	if (lhs == MONEY_INVALID_VALUE)
		return true;

	return false;  // TODO FIXME alaframboise
}

// Assert if the two Money objects do not have the same currency.
static void AssertSameCurrency(SA_PARAM_NN_VALID const Money *lhs, SA_PARAM_NN_VALID const Money *rhs)
{
	devassertmsg(!stricmp_safe(INTERNAL_CURRENCY(*lhs), INTERNAL_CURRENCY(*rhs)), "Performing binary operation on currencies of different types");
}

// Cast a Money object to a MoneyContainer.
SA_RET_NN_VALID NOCONST(MoneyContainer) *moneyToContainer(SA_PARAM_NN_VALID Money *money)
{
	return &money->Internal;
}

const NOCONST(MoneyContainer) *moneyToContainerConst(const Money *money)
{
	return &money->Internal;
}

// Cast a MoneyContainer to a Money object.
SA_RET_NN_VALID Money *moneyContainerToMoney(NOCONST(MoneyContainer) *container)
{
	return (Money *)container;
}

// Cast a MoneyContainer to a Money object (const version).
const Money *moneyContainerToMoneyConst(const MoneyContainer *container)
{
	return (const Money *)container;
}

// Initialize a money object.
void moneyInit(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money)
{
	StructInit(parse_Money, money);
	StructCopy(parse_Money, &moneyInvalidValue, money, 0, 0, 0);
}

// Create a money object.
SA_RET_NN_VALID Money *moneyCreate(SA_PARAM_NN_STR const char *strRaw, SA_PARAM_NN_STR const char *currency)
{
	Money *result = StructCreate(parse_Money);
	moneyInitFromStr(result, strRaw, currency);
	return result;
}

// Create a money object with invalid amount.
SA_RET_NN_VALID Money *moneyCreateInvalid(SA_PARAM_NN_STR const char *currency)
{
	Money *result = StructCreate(parse_Money);
	StructCopy(parse_Money, &moneyInvalidValue, result, 0, 0, 0);
	estrCopy2(&INTERNAL_CURRENCY(*result), currency);
	return result;
}

// Create a money object from a float.
// WARNING: This is only intended to be used as a transitional mechanism to support interoperation with PriceContainer.  It should
// not be used for any other purpose.  In the long run, binary floating point must never be used to represent currency values.
SA_RET_NN_VALID Money *moneyCreateFromFloat(float value, SA_PARAM_NN_STR const char *currency)
{
	Money *result = StructCreate(parse_Money);
	moneyInitFromFloat(result, value, currency);
	return result;
}

// Create a money object from an integer.
SA_RET_NN_VALID Money *moneyCreateFromInt(int value, SA_PARAM_NN_STR const char *currency)
{
	Money *result = StructCreate(parse_Money);
	moneyInitFromInt(result, value, currency);
	return result;
}

// Destroy contents of a money object.
void moneyDeinit(SA_PRE_NN_VALID SA_POST_P_FREE Money *lhs)
{
	StructDeInit(parse_Money, lhs);
}

// Destroy a money object.
void moneyDestroy(SA_PRE_NN_VALID SA_POST_P_FREE Money *lhs)
{
	StructDestroy(parse_Money, lhs);
}

// Copy a money object.
void moneyAssign(Money *lhs, const Money *rhs)
{
	INTERNAL_AMOUNT(*lhs) = INTERNAL_AMOUNT(*rhs);
	devassert(INTERNAL_CURRENCY(*rhs) && *INTERNAL_CURRENCY(*rhs));
	estrDestroy(&INTERNAL_CURRENCY(*lhs));
	INTERNAL_CURRENCY(*lhs) = estrDup(INTERNAL_CURRENCY(*rhs));
}

// Add a Money value to another Money value.
void moneyAdd(SA_PARAM_NN_VALID Money *lhs, SA_PARAM_NN_VALID const Money *rhs)
{

	// Check for problems.
	AssertSameCurrency(lhs, rhs);
	if (AdditionOverflows(INTERNAL_AMOUNT(*lhs), INTERNAL_AMOUNT(*rhs)))
	{
		INTERNAL_AMOUNT(*lhs) = MONEY_INVALID_VALUE;
		return;
	}

	// Perform addition.
	INTERNAL_AMOUNT(*lhs) += INTERNAL_AMOUNT(*rhs);
}

// Subtract a Money value to another Money value.
void moneySubtract(SA_PARAM_NN_VALID Money *lhs, SA_PARAM_NN_VALID const Money *rhs)
{
	// Check for problems.
	AssertSameCurrency(lhs, rhs);
	if (AdditionOverflows(INTERNAL_AMOUNT(*lhs), -INTERNAL_AMOUNT(*rhs)))
	{
		INTERNAL_AMOUNT(*lhs) = MONEY_INVALID_VALUE;
		return;
	}

	// Perform subtraction.
	INTERNAL_AMOUNT(*lhs) -= INTERNAL_AMOUNT(*rhs);
}

// Multiply a Money value by a scalar.
void moneyMultiply(SA_PARAM_NN_VALID Money *lhs, SA_PARAM_NN_VALID double rhs)
{
	// Check for problems.
	if (MultiplicationOverflows(INTERNAL_AMOUNT(*lhs), rhs))
	{
		INTERNAL_AMOUNT(*lhs) = MONEY_INVALID_VALUE;
		return;
	}

	// Perform multiplication.
	INTERNAL_AMOUNT(*lhs) *= rhs;
}

// Ordering comparison for Money.
bool moneyLess(SA_PARAM_NN_VALID const Money *lhs, SA_PARAM_NN_VALID const Money *rhs)
{
	AssertSameCurrency(lhs, rhs);
	if (moneyInvalid(lhs) || moneyInvalid(rhs))
		return false;
	return INTERNAL_AMOUNT(*lhs) < INTERNAL_AMOUNT(*rhs);
}

// Equality comparison for Money.
bool moneyEqual(SA_PARAM_NN_VALID const Money *lhs, SA_PARAM_NN_VALID const Money *rhs)
{
	AssertSameCurrency(lhs, rhs);
	if (moneyInvalid(lhs) || moneyInvalid(rhs))
		return false;
	return INTERNAL_AMOUNT(*lhs) == INTERNAL_AMOUNT(*rhs);
}

void moneyZenConvert(SA_PARAM_NN_VALID Money *lhs)
{
	if (moneyInvalid(lhs) || isRealCurrency(INTERNAL_CURRENCY(*lhs))) return;
	ZEN_CONVERT_VALUE(INTERNAL_AMOUNT(*lhs));
}

// Format Money to an EString.
void estrFromMoney(SA_PRE_NN_NN_STR char **str, SA_PARAM_NN_VALID const Money *money)
{

#if PLATFORM_CONSOLE
	estrFromMoneyRaw(str, money);
#else

	const struct CurrencyData *currency = GetCurrencyData(INTERNAL_CURRENCY(*money));
	char result[256];
	WCHAR wide[sizeof(result)];
	WCHAR format[sizeof(result)];
	int success;

	// Format raw currency.
	estrFromMoneyRaw(str, money);
	if (moneyInvalid(money))
		return;
	if (!isRealCurrency(INTERNAL_CURRENCY(*money)))
		return;

	// Convert UTF-8 to UTF-16.
	success = MultiByteToWideChar(CP_UTF8, 0, *str, -1, wide, sizeof(wide)/sizeof(*wide));
	devassert(success);

	// Do locale-specific formatting using Windows NLS.

	success = GetCurrencyFormatW(MAKELCID(MAKELANGID(currency->wLang, currency->wSublang), SORT_DEFAULT),
		LOCALE_NOUSEROVERRIDE, wide, NULL, format, sizeof(format)/sizeof(*format));
	devassert(success);

	// Convert back to narrow characters.
	success = WideCharToMultiByte(CP_UTF8, 0, format, -1, result, sizeof(result), NULL, NULL);
	devassert(success);
	estrCopy2(str, result);
#endif
}

// Format Money to an EString, with no currency markers, using - for negative and . for decimal.
void estrFromMoneyRaw(char **str, const Money *money)
{
	S64 value = INTERNAL_AMOUNT(*money);
	const char *sign = value < 0 ? "-" : "";
	const struct CurrencyData *currency = GetCurrencyData(INTERNAL_CURRENCY(*money));
	size_t digits = currency ? currency->uDigits : 0;
	devassert(currency || !isRealCurrency(INTERNAL_CURRENCY(*money)));

	// Handle overflow case.
	if (moneyInvalid(money))
	{
		estrCopy2(str, "(overflow)");
		return;
	}

	// Format currency with decimal point.
	if (value < 0)
		value = -value;
	if (digits)
		estrPrintf(str, "%s%"FORM_LL"d.%0*"FORM_LL"d", sign, value / (S64)pow(10, digits), digits, value % (S64)pow(10, digits));
	else
		estrPrintf(str, "%s%"FORM_LL"d", sign, value);
}

// Get the international currency abbreviation for a Money struct as an EString.
// The EString must be a destroyed by the caller.
void estrCurrency(SA_PRE_NN_NN_STR char **currency, SA_PARAM_NN_VALID const Money *money)
{
	estrCopy(currency, (const char **)&INTERNAL_CURRENCY(*money));
}

// Get the international currency abbreviation for a Money struct.
// The lifetime of the string is equivalent to the lifetime of the Money object.
const char *moneyCurrency(const Money *money)
{
	return INTERNAL_CURRENCY(*money);
}

// Get the account key name of a points Money struct.
// The lifetime of the string is equivalent to the lifetime of the Money object.
const char *moneyKeyName(const Money *money)
{
	const char * pCurrency = INTERNAL_CURRENCY(*money);
	if (devassert(!isRealCurrency(pCurrency)))
	{
		return pCurrency + 1;
	}
	return NULL;
}

// Initialize a money object from a string.
// This must be moneyDestroy()ed by the caller.
void moneyInitFromStr(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money, SA_PARAM_NN_STR const char *str, SA_PARAM_NN_STR const char *currency)
{
	const struct CurrencyData *data = GetCurrencyData(currency);
	size_t digits = 0;
	S64 wholepart;
	char subpart[20];
	int n;

	moneyInit(money);

	// Check currency.
	if (!data && isRealCurrency(currency))
	{
		INTERNAL_AMOUNT(*money) = MONEY_INVALID_VALUE;
		estrCopy2(&INTERNAL_CURRENCY(*money), "XXX");
		return;
	}

	// Copy currency.
	estrCopy2(&INTERNAL_CURRENCY(*money), currency);
	if (isRealCurrency(INTERNAL_CURRENCY(*money)))
	{
		size_t i;
		for (i = 0; i != 3; ++i)
			INTERNAL_CURRENCY(*money)[i] = toupper(INTERNAL_CURRENCY(*money)[i]);
	}
	if (data)
		digits = data->uDigits;

	// Parse and convert value.
	if (!strcmp(str, "0"))
		INTERNAL_AMOUNT(*money) = 0;
	else
	{
		n = sscanf_s(str, "%"FORM_LL"d.%19s", &wholepart, subpart, sizeof(subpart));
		if (!n || digits && n != 2 || digits && strlen(subpart) != digits)
			INTERNAL_AMOUNT(*money) = MONEY_INVALID_VALUE;
		else if (digits)
		{
			// Currency with subdivided part
			Money subpartmoney = {0, INTERNAL_CURRENCY(*money)};
			INTERNAL_AMOUNT(*money) = wholepart;
			moneyMultiply(money, pow(10, digits));
			n = sscanf(subpart, "%"FORM_LL"d", &INTERNAL_AMOUNT(subpartmoney));
			if (n == 1 && INTERNAL_AMOUNT(subpartmoney) >= 0)
			{
				INTERNAL_AMOUNT(subpartmoney) *= wholepart < 0 ? -1 : 1;
				moneyAdd(money, &subpartmoney);
			}
			else
				INTERNAL_AMOUNT(*money) = MONEY_INVALID_VALUE;
		}
		else
			// Whole currency with no subdivided parts or points
			INTERNAL_AMOUNT(*money) = wholepart;
	}
}

// Read a string into a Money struct.
// This must be StructDestroy()ed by the caller.
// WARNING: This is only intended to be used as a transitional mechanism to support interoperation with PriceContainer.  It should
// not be used for any other purpose.  In the long run, binary floating point must never be used to represent currency values.
void moneyInitFromFloat(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money, float value, SA_PARAM_NN_STR const char *currency)
{
	const struct CurrencyData *data = GetCurrencyData(currency);
	size_t digits = 0;
	double whole, error, dummy;

	moneyInit(money);

	// Check currency.
	if (!data && isRealCurrency(currency))
	{
		INTERNAL_AMOUNT(*money) = MONEY_INVALID_VALUE;
		estrCopy2(&INTERNAL_CURRENCY(*money), "XXX");
		return;
	}

	// Copy currency.
	estrCopy2(&INTERNAL_CURRENCY(*money), currency);
	if (isRealCurrency(INTERNAL_CURRENCY(*money)))
	{
		size_t i;
		for (i = 0; i != 3; ++i)
			INTERNAL_CURRENCY(*money)[i] = toupper(INTERNAL_CURRENCY(*money)[i]);
	}
	if (data)
		digits = data->uDigits;

	// Convert floating point to fixed point.
	whole = value * pow(10, digits);
	error = modf(whole, &dummy);
	if (error > .5)
		error = 1 - error;
	devassert(error < .01);
	devassert(whole <= LLONG_MAX);
	if (whole > 0)
		whole = floor(whole + .5);
	else
		whole = ceil(whole - .5);
	INTERNAL_AMOUNT(*money) = whole;
}

// Read an integer into a Money struct.
// This must be StructDestroy()ed by the caller.
void moneyInitFromInt(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money, S64 value, SA_PARAM_NN_STR const char *currency)
{
	const struct CurrencyData *data = GetCurrencyData(currency);

	moneyInit(money);

	// Check currency.
	if (!data && isRealCurrency(currency))
	{
		INTERNAL_AMOUNT(*money) = MONEY_INVALID_VALUE;
		estrCopy2(&INTERNAL_CURRENCY(*money), "XXX");
		return;
	}

	// Copy currency.
	estrCopy2(&INTERNAL_CURRENCY(*money), currency);
	if (isRealCurrency(INTERNAL_CURRENCY(*money)))
	{
		size_t i;
		for (i = 0; i != 3; ++i)
			INTERNAL_CURRENCY(*money)[i] = toupper(INTERNAL_CURRENCY(*money)[i]);
	}

	INTERNAL_AMOUNT(*money) = value;
}

// Return true if this Money value has overflowed.
bool moneyInvalid(SA_PARAM_NN_VALID const Money *money)
{
	return INTERNAL_AMOUNT(*money) == MONEY_INVALID_VALUE;
}

// Get an English-language description of a currency.
const char *CurrencyDescription(SA_PARAM_NN_STR const char *currency)
{
	const struct CurrencyData *data = GetCurrencyData(currency);
	if (data)
		return data->pDescription;
	else if (!isRealCurrency(currency))
		return "Points";
	return NULL;
}

// Determine if a currency is for real money or not
AUTO_TRANS_HELPER_SIMPLE;
bool isRealCurrency(SA_PARAM_NN_STR const char *pCurrency)
{
	if (*pCurrency == POINT_CURRENCY_MARKER) return false;
	return true;
}

// If this currency is a points currency, return the number of points.
// WARNING: Do not use this on a non-points currency.
AUTO_TRANS_HELPER_SIMPLE;
S64 moneyCountPoints(SA_PARAM_NN_VALID const Money *money)
{
	devassert(!isRealCurrency(INTERNAL_CURRENCY(*money)));
	return INTERNAL_AMOUNT(*money);
}

// Convert a currency to the proper case
SA_RET_NN_STR char *convertCurrencyCase(SA_PARAM_NN_STR char *pCurrency)
{
	unsigned int ch;
	for (ch = 0; ch < strlen(pCurrency); ch++)
	{
		if (pCurrency[ch] >= 'a' && pCurrency[ch] <= 'z') pCurrency[ch] = pCurrency[ch] - 'a' + 'A';
	}
	return pCurrency;
}

const Money *findMoneyFromCurrency(CONST_EARRAY_OF(Money) ppMoney, const char *pCurrency)
{
	EARRAY_CONST_FOREACH_BEGIN(ppMoney, i, n);
	{
		if (stricmp_safe(moneyCurrency(ppMoney[i]), pCurrency) == 0)
			return ppMoney[i];
	}
	EARRAY_FOREACH_END;
	return NULL;
}

const MoneyContainer *findMoneyContainerFromCurrency(CONST_EARRAY_OF(MoneyContainer) ppMoney, const char *pCurrency)
{
	EARRAY_CONST_FOREACH_BEGIN(ppMoney, i, n);
	{
		if (stricmp_safe(moneyCurrency(moneyContainerToMoneyConst(ppMoney[i])), pCurrency) == 0)
			return ppMoney[i];
	}
	EARRAY_FOREACH_END;
	return NULL;
}

Money moneyInvalidValue = {MONEY_INVALID_VALUE, 0};

#include "Money_h_ast.c"
