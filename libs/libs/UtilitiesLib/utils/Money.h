#ifndef CRYPTIC_MONEY_H
#define CRYPTIC_MONEY_H

#include "AutoGen\Money_h_ast.h"

/************************************************************************/
/* Money, currency, and prices                                          */
/************************************************************************/

/* General
 * Freshly-allocated money objects must be initialized before being used by calling moneyInitFromStr().
 * If the Money object is created by calling moneyCreate(), this step is unnecessary. */

/* Notes on currency formatting
 * There are four ways to format a currency
 * 1) Raw form, as a C-style decimal number.  This is what estrFromMoneyRaw() does.
 * 2) According to user's locale.  This is what most NLS libraries do, such as the standard C++ library and Windows NLS,
 * and it's terribly wrong unless you happen to be printing the local currency.
 * 3) According to the predominate locale associated with the currency type.  This is what estrFromMoney() tries to do.
 * Widely-used currencies like the euro or regional funds are likely to be formatted in a way that is incorrect for the locale,
 * (such as commas instead of periods, possibly a foreign language currency symbol) but hopefully it should still be recognizable.
 * 4) According to both the currency type and the locale.  This is the correct way, but difficult because NLS library support is difficult
 * to find, and a significant amount of research is necessary to come up with data on the cross product of all currencies with all written
 * languages.
 */

// Prefix for points currencies
// Use !isRealCurrency() to check for points currencies
#define POINT_CURRENCY_MARKER '_'

// THIS IS THE MAGICAL FORMULA OF MAGIC FOR CONVERTING CP TO ZEN
// NEVER DUPLICATE THIS ANYWHERE ELSE, OR DO ANYTHING SIMILAR ANYWHERE
#define ZEN_CONVERT_VALUE(val) (val) = (((val) * 5 + 3) / 4)

// Amount of money in a particular currency.
// Manipulate this structure using the accessor functions below.
// WARNING: Internal members are not a public interface.  Direct manipulation may be unsafe.
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct MoneyContainer
{
	const S64 _internal_SubdividedAmount;							// Amount in cents or similarly subdivided unit
	CONST_STRING_MODIFIABLE _internal_Currency;		AST(ESTRING)	// ISO 4217 currency code or points type
} MoneyContainer;
AST_PREFIX()

// Non-container version of Money.
typedef NOCONST(MoneyContainer) MoneyInternal;
AUTO_STRUCT;
typedef struct Money
{
	MoneyInternal Internal;							AST(STRUCT(parse_MoneyContainer))
} Money;

/************************************************************************/
/* Container conversion                                                 */
/************************************************************************/

// Cast a Money object to a MoneyContainer.
SA_RET_NN_VALID NOCONST(MoneyContainer) *moneyToContainer(SA_PARAM_NN_VALID Money *money);

// Cast a Money object to a MoneyContainer (const version).
SA_RET_NN_VALID const NOCONST(MoneyContainer) *moneyToContainerConst(SA_PARAM_NN_VALID const Money *money);

// Cast a MoneyContainer to a Money object.
SA_RET_NN_VALID Money *moneyContainerToMoney(SA_PARAM_NN_VALID NOCONST(MoneyContainer) *container);

// Cast a MoneyContainer to a Money object (const version).
SA_RET_NN_VALID const Money *moneyContainerToMoneyConst(SA_PARAM_NN_VALID const MoneyContainer *container);

/************************************************************************/
/* Creation and destruction                                             */
/************************************************************************/

// Initialize a money object.
void moneyInit(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money);

// Create a money object.
SA_RET_NN_VALID Money *moneyCreate(SA_PARAM_NN_STR const char *strRaw, SA_PARAM_NN_STR const char *currency);

// Create a money object with invalid amount.
SA_RET_NN_VALID Money *moneyCreateInvalid(SA_PARAM_NN_STR const char *currency);

// Create a money object from a float.
// WARNING: This is only intended to be used as a transitional mechanism to support interoperation with PriceContainer.  It should
// not be used for any other purpose.  In the long run, binary floating point must never be used to represent currency values.
SA_RET_NN_VALID Money *moneyCreateFromFloat(float value, SA_PARAM_NN_STR const char *currency);

// Create a money object from an integer.
SA_RET_NN_VALID Money *moneyCreateFromInt(int value, SA_PARAM_NN_STR const char *currency);

// Initialize a money object from a string.
// This must be moneyDestroy()ed by the caller.
void moneyInitFromStr(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money, SA_PARAM_NN_STR const char *strRaw, SA_PARAM_NN_STR const char *currency);

// Read a string into a Money struct.
// This must be StructDestroy()ed by the caller.
// WARNING: This is only intended to be used as a transitional mechanism to support interoperation with PriceContainer.  It should
// not be used for any other purpose.  In the long run, binary floating point must never be used to represent currency values.
void moneyInitFromFloat(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money, float value, SA_PARAM_NN_STR const char *currency);

// Read an integer into a Money struct.
// This must be StructDestroy()ed by the caller.
void moneyInitFromInt(SA_PRE_NN_FREE SA_POST_NN_VALID Money *money, S64 value, SA_PARAM_NN_STR const char *currency);

// Destroy contents of a money object.
void moneyDeinit(SA_PRE_NN_VALID SA_POST_P_FREE Money *lhs);

// Destroy a money object.
void moneyDestroy(SA_PRE_NN_VALID SA_POST_P_FREE Money *lhs);

/************************************************************************/
/* Operations                                                           */
/************************************************************************/

// Copy a money object.
void moneyAssign(SA_PRE_NN_FREE SA_POST_NN_VALID Money *lhs, SA_PARAM_NN_VALID const Money *rhs);

// Add a Money value to another Money value.
void moneyAdd(SA_PARAM_NN_VALID Money *lhs, SA_PARAM_NN_VALID const Money *rhs);

// Subtract a Money value to another Money value.
void moneySubtract(SA_PARAM_NN_VALID Money *lhs, SA_PARAM_NN_VALID const Money *rhs);

// Multiply a Money value by a scalar.
void moneyMultiply(SA_PARAM_NN_VALID Money *lhs, SA_PARAM_NN_VALID double rhs);

// Convert a Money value to Zen.
void moneyZenConvert(SA_PARAM_NN_VALID Money *lhs);

/************************************************************************/
/* Accessors                                                            */
/************************************************************************/

// Ordering comparison for Money.
bool moneyLess(SA_PARAM_NN_VALID const Money *lhs, SA_PARAM_NN_VALID const Money *rhs);

// Equality comparison for Money.
bool moneyEqual(SA_PARAM_NN_VALID const Money *lhs, SA_PARAM_NN_VALID const Money *rhs);

// Format Money to an EString in a manner appropriate for this currency.
void estrFromMoney(SA_PRE_NN_NN_STR char **str, SA_PARAM_NN_VALID const Money *money);

// Format Money to an EString, with no currency markers, using - for negative and . for decimal.
void estrFromMoneyRaw(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR  char **str, SA_PARAM_NN_VALID const Money *money);

// Get the international currency abbreviation for a Money struct as an EString.
// The EString must be a destroyed by the caller.
void estrCurrency(SA_PRE_NN_NN_STR char **currency, SA_PARAM_NN_VALID const Money *money);

// Get the international currency abbreviation for a Money struct.
// The lifetime of the string is equivalent to the lifetime of the Money object.
SA_RET_NN_STR const char *moneyCurrency(SA_PARAM_NN_VALID const Money *money);

// Get the account key name of a points Money struct.
// The lifetime of the string is equivalent to the lifetime of the Money object.
SA_RET_NN_STR const char *moneyKeyName(SA_PARAM_NN_VALID const Money *money);

// Return true if this Money value has overflowed.
bool moneyInvalid(SA_PARAM_NN_VALID const Money *money);

// Get an English-language description of a currency.
const char *CurrencyDescription(SA_PARAM_NN_STR const char *currency);

// If this currency is a points currency, return the number of points.
// WARNING: Do not use this on a non-points currency.
S64 moneyCountPoints(SA_PARAM_NN_VALID const Money *money);

/************************************************************************/
/* Utility                                                              */
/************************************************************************/

// Determine if a currency is real or not
bool isRealCurrency(SA_PARAM_NN_STR const char *pCurrency);

// Convert a currency to the proper case
SA_RET_NN_STR char *convertCurrencyCase(SA_PARAM_NN_STR char *pCurrency);

const Money *findMoneyFromCurrency(CONST_EARRAY_OF(Money) ppMoney, const char *pCurrency);
const MoneyContainer *findMoneyContainerFromCurrency(CONST_EARRAY_OF(MoneyContainer) ppMoney, const char *pCurrency);

// An overflowed money object.
extern Money moneyInvalidValue;

#endif  // CRYPTIC_MONEY_H
