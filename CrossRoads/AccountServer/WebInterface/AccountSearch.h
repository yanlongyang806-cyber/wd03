#ifndef ACCOUNT_SEARCH_H
#define ACCOUNT_SEARCH_H

#include "objContainer.h"

typedef struct AccountInfo AccountInfo;

typedef enum SortOrder
{
	SORTORDER_ID = 0, 
	SORTORDER_ACCOUNTNAME,
	SORTORDER_DISPLAYNAME,
	SORTORDER_EMAIL,
	SORTORDER_MAX
} SortOrder;

#define SEARCHFLAG_SORT			BIT(0)
#define SEARCHFLAG_ALL			BIT(1)
#define SEARCHFLAG_NAME			BIT(2) // for Account, Display, or Both
#define SEARCHFLAG_EMAIL		BIT(3)
#define SEARCHFLAG_PRODUCTSUB	BIT(4) // search the names of their product/subscription list
#define SEARCHFLAG_FIRSTNAME	BIT(5)
#define SEARCHFLAG_LASTNAME		BIT(6)
#define SEARCHFLAG_PRODUCTKEY	BIT(7)

#define SEARCHNAME_ACCOUNT		BIT(0)
#define SEARCHNAME_DISPLAY		BIT(1)
#define SEARCHNAME_BOTH			(SEARCHNAME_ACCOUNT | SEARCHNAME_DISPLAY)

#define SEARCHPERMISSIONS_PRODUCT		BIT(0)
#define SEARCHPERMISSIONS_SUBSCRIPTION	BIT(1)
#define SEARCHPERMISSIONS_BOTH			(SEARCHPERMISSIONS_PRODUCT | SEARCHPERMISSIONS_SUBSCRIPTION)

typedef struct AccountSearchData
{
	// Core flags, determines what filters are active
	U32 uFlags;
	// SEARCHFLAG_SORT
	SortOrder eSortOrder;
	bool bSortDescending;

	// Basic bools
	char *name;
	int iNameFilter;

	char *firstName;
	char *lastName;
	char *email;

	char *productSub;
	int iProductFilter;
	char *productKey;

	char *pAny;

	// Search state
	AccountInfo **ppSortedEntries;
	int iCount;
	int iNextIndex;
} AccountSearchData;

AccountInfo * searchFirst(AccountSearchData *pData);
AccountInfo * searchNext (AccountSearchData *pData);
void searchEnd(AccountSearchData *pData);

void accountSearch_Begin(const char *pAccountName,
	const char *pDisplayName,
	const char *pEmail,
	const char *pFirstName,
	const char *pLastName,
	const char *pProduct,
	const char *pKey,
	const char *pCardName,
	const char *pCardFirstSix,
	const char *pCardLastFour,
	const char *pStreetAddress,
	const char *pCity,
	const char *pState,
	const char *pZip,
	int iMaxToReturn,
	NonBlockingQueryCB pCompleteCB,
	void *pUserData);

#endif