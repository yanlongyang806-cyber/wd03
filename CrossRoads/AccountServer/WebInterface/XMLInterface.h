#ifndef XMLRPC_H
#define XMLRPC_H

/************************************************************************/
/* Flags                                                                */
/************************************************************************/

// Used for UserInfo response bits
#define USERINFO_PERMISSIONS			BIT(0) // Include permissions
#define USERINFO_KEYVALUES				BIT(1) // Include key values
#define USERINFO_PRODUCTSOWNED			BIT(2) // Include products owned
#define USERINFO_QUESTIONS				BIT(3) // Include secret questions
#define USERINFO_PRODUCTKEYS			BIT(4) // Include product keys
#define USERINFO_DISTRIBUTEDPRODUCTKEYS BIT(5) // Include distributed product keys
#define USERINFO_INTERNALSUBS			BIT(6) // Include internal subscriptions
#define USERINFO_ACTIVITYLOG			BIT(7) // Include activity log
#define USERINFO_SUBHISTORY				BIT(8) // Include sub history
#define USERINFO_SUBSTATS				BIT(9) // Include sub stats
#define USERINFO_RECRUITS				BIT(10) // Include recruits
#define USERINFO_RECRUITERS				BIT(11) // Include recruiters
#define USERINFO_RECRUITSLOTS			BIT(12) // Include number of recruitment slots available
#define USERINFO_PLAYTIME				BIT(13) // Playtime information
#define USERINFO_PROFILES				BIT(14) // Profile information (used by Eden stuff)
#define USERINFO_SPENDINGCAP			BIT(15) // Profile information (used by Eden stuff)
#define USERINFO_PERFECTWORLDACCOUNT    BIT(16) // Linked Perfect World account info
#define USERINFO_SAVEDMACHINES          BIT(17) // Machine Locking info

// Used for account creation
//#define ACCOUNTCREATE_SKIP_PREFIX_CHECK		BIT(0) (deprecated)
#define ACCOUNTCREATE_INTERNAL_LOGIN_ONLY	BIT(1)

/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize XML interface.
void XmlInterfaceInit(void);

/************************************************************************/
/* XML-RPC                                                              */
/************************************************************************/

// Used to represent prices to the web
AUTO_STRUCT;
typedef struct XMLRPCPrice
{
	char *pPrice;							AST(ESTRING)  // Localized price
	char *pPriceRaw;						AST(ESTRING)  // Price as a decimal number
	char *pCurrency;						AST(ESTRING)  // ISO 4217 currency code
} XMLRPCPrice;

// Short XML-RPC logs.
void LogXmlrpcf(FORMAT_STR const char *pFormat, ...);
#define LogXmlrpcf(pFormat, ...) LogXmlrpcf(FORMAT_STRING_CHECKED(pFormat), __VA_ARGS__)

#endif
