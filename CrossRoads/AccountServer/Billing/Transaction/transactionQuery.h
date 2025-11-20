#ifndef TRANSACTIONQUERY_H
#define TRANSACTIONQUERY_H

#include "transactionQuery_h_ast.h"

AUTO_ENUM;
typedef enum TransactionFilterFlags
{
	TFF_Cancelled				= BIT(0),
	TFF_DirectDebit				= BIT(1),
	TFF_NotValidationProduct	= BIT(2),

	TFF_MAX, EIGNORE
} TransactionFilterFlags;
#define TransactionFilterFlags_NUMBITS 4
STATIC_ASSERT(TFF_MAX == ((1 << (TransactionFilterFlags_NUMBITS-2))+1));

typedef struct BillingTransaction BillingTransaction;
typedef struct PaymentMethod PaymentMethod;
typedef struct AccountAddress AccountAddress;

// Status of a Vindicia transaction
AUTO_STRUCT;
typedef struct AccountTransactionStatus
{
	char *status;							AST(ESTRING)
	S64 timestamp;							AST(ESTRING)
	char *paymentMethodType;				AST(ESTRING)
	char *authCode;							AST(ESTRING)

	// Credit cards
	char *avsCode;							AST(ESTRING)
	char *cvnCode;							AST(ESTRING)
	char *vinAVS;							AST(ESTRING)

	// Boleto
	char *uri;								AST(ESTRING)

	// Paypal
	char *token;							AST(ESTRING)
	char *redirectUrl;						AST(ESTRING)
} AccountTransactionStatus;

// An individual Vindicia transaction and transaction information associated with the requested account.
AUTO_STRUCT;
typedef struct AccountTransactionInfo
{
	// Account Server information
	STRING_EARRAY products;					AST(ESTRING)
	char *accountGUID;						AST(ESTRING)

	// Vindicia information
	char *VID;								AST(ESTRING)
	char *amount;							AST(ESTRING)
	char *currency;							AST(ESTRING)
	char *divisionNumber;					AST(ESTRING)
	char *merchantTransactionId;			AST(ESTRING)
	char *previousMerchantTransactionId;	AST(ESTRING)
	S64 timestamp;
	// Note: Other account fields from Vindicia are omitted.
	char *merchantAccountId;				AST(ESTRING)
	// Note: PaymentMethod does not have all fields that the original response from Vindicia has.
	PaymentMethod *sourcePaymentMethod;
	PaymentMethod *destPaymentMethod;
	char *ecpTransactionType;				AST(ESTRING)
	EARRAY_OF(AccountTransactionStatus)		statusLog;
	char *paymentProcessor;					AST(ESTRING)
	char *sourcePhoneNumber;				AST(ESTRING)
	char *shippingAddressaddress1;			AST(ESTRING)
	char *shippingAddressaddress2;			AST(ESTRING)
	char *shippingAddresscity;				AST(ESTRING)
	char *shippingAddressdistrict;			AST(ESTRING)
	char *shippingAddresspostalCode;		AST(ESTRING)
	char *shippingAddresscountry;			AST(ESTRING)
	char *shippingAddressphone;				AST(ESTRING)
	char *merchantAffiliateId;				AST(ESTRING)
	char *merchantAffiliateSubId;			AST(ESTRING)
	char *userAgent;						AST(ESTRING)
	char *note;								AST(ESTRING)
	char *preferredNotificationLanguage;	AST(ESTRING)
	char *sourceMacAddress;					AST(ESTRING)
	char *sourceIp;							AST(ESTRING)
	char *billingStatementIdentifier;		AST(ESTRING)
	char *verificationCode;					AST(ESTRING)
	// Note: Omitting Vindicia response fields: taxExemptions, salesTaxAddress, nameValues, transactionItems

	const struct vin__Transaction *vindiciaTransaction; NO_AST
} AccountTransactionInfo;

typedef void (*btFetchAccountTransactionsCallback)(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bKnownAccount,
												   SA_PARAM_OP_VALID AccountTransactionInfo **, SA_PARAM_OP_VALID void *userData);
typedef void (*btFetchAccountTransactionCallback)(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bFoundTransaction,
												   SA_PARAM_OP_VALID AccountTransactionInfo *, SA_PARAM_OP_VALID void *userData);

// Get a list of transactions that have been performed by an account from Vindicia.
// callback will be called with an EARRAY of the struct, which it must destroy.
SA_ORET_NN_VALID BillingTransaction * btFetchAccountTransactions(
	U32 uAccountID,
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	SA_PARAM_NN_VALID btFetchAccountTransactionsCallback callback,
	SA_PARAM_OP_VALID void *userData);

// Get transaction information for a VID.
SA_ORET_NN_VALID BillingTransaction * btFetchTransactionByVID(
	SA_PARAM_NN_STR const char *VID,
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	SA_PARAM_NN_VALID btFetchAccountTransactionCallback callback,
	SA_PARAM_OP_VALID void *userData);

// Get transaction information for a merchant account ID.
SA_ORET_NN_VALID BillingTransaction * btFetchTransactionByMTID(
	SA_PARAM_NN_STR const char *MTID,
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	SA_PARAM_NN_VALID btFetchAccountTransactionCallback callback,
	SA_PARAM_OP_VALID void *userData);

// Get a list of transactions that have been changed by Vindicia in a time range
// callback will be called with an EARRAY of the struct, which it must destroy.
SA_ORET_NN_VALID BillingTransaction *
btFetchChangedTransactionsSince(U32 uTimeSS2000,
								U32 uTimeSS2000End,
								TransactionFilterFlags filters,
								SA_PARAM_OP_VALID BillingTransaction *pTrans,
								SA_PARAM_NN_VALID btFetchAccountTransactionsCallback pCallback,
								SA_PARAM_OP_VALID void *pUserData);

#endif  // TRANSACTIONQUERY_H
