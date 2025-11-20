/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if _XBOX

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#include "XStore.h"
#include "XUtil.h"
#include "windefinclude.h"
#include <xam.h>
#include <xonline.h>
#include "GlobalTypes.h"
#include "earray.h"
#include "EString.h"
#include "accountnet.h"
#include "gclEntity.h"
#include "gclAccountProxy.h"
#include "gclSendToServer.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#define MAX_ROOT_NAME_SIZE 22

// The struct holding the content information
typedef struct XContentInfo
{
	XCONTENT_DATA contentData;
	bool bMounted;
	DWORD dwLicenseMask;
	char rootName[MAX_ROOT_NAME_SIZE];
} XContentInfo;

// Notification areas we are interested in
static ULONGLONG	qwNotificationAreas				= XNOTIFY_SYSTEM | XNOTIFY_LIVE;

// Messages we are interested in
static DWORD		dwMsgStoreDevicesChanged		= XN_SYS_STORAGEDEVICESCHANGED;
static DWORD		dwMsgSignInChanged				= XN_SYS_SIGNINCHANGED;

// XOVERLAPPED struct for async operations related to content
static XOVERLAPPED	overlappedEnumContent;

// Content mounting related variables
static XContentInfo **eaContentInfo					= NULL;
static bool			bMountContent					= false;
static XOVERLAPPED	overlappedMountContent;

// Offer enumeration related variables
static XMARKETPLACE_CONTENTOFFER_INFO **eaOffers	= NULL;
static XOVERLAPPED	overlappedEnumOffers;

// XUID for the player whom we're displaying the offers for
static XUID			xuidPlayerForOffers				= 0;

// If this set to true, the offers are reenumerated
static bool			bReenumerateOffers				= false;
static bool			bAllOffersEnumerated			= false;

// Indicates if we are currently notifying game server about the purchases
static bool			bPurchaseNotificationInProgress	= false;

// Forward declarations of methods
static void xStore_MountContent(void);
static void xStore_HandleNotifications(void);
static void xStore_EnumerateContent(void);
static void xStore_EnumerateOffers(void);
static XContentInfo * xStore_GetXContentInfo(SA_PARAM_NN_VALID const XCONTENT_DATA *pXContentData, S32 iIndex);
static void xStore_SendPurchaseNotifications(void);
static XMARKETPLACE_CONTENTOFFER_INFO * xStore_GetOfferByProduct(SA_PARAM_NN_VALID AccountProxyProduct *pProduct);

// Starts the process that notifies the server about the purchases
void xStore_BeginPurchaseNotification(void)
{
	bPurchaseNotificationInProgress = true;
}

// Sends the purchase notifications to the game server
static void xStore_SendPurchaseNotifications(void)
{
	// The current player
	Entity *pEntity = entActivePlayerPtr();

	// Category names
	static char availableCategoryName[20]	= { 0 };
	static char specialCategoryName[20]		= { 0 };
	static char *pCurrency					= "_xboxpoints";

	// The product related stuff
	bool	bAvailableProductsLoaded	= false;
	bool	bSpecialProductsLoaded		= false;
	EARRAY_OF(AccountProxyProduct) availableProducts	= NULL;
	EARRAY_OF(AccountProxyProduct) specialProducts		= NULL;
	EARRAY_OF(AccountProxyProduct) currentProductList			= NULL;

	// Product iteration related stuff
	AccountProxyProduct *pProduct = NULL;
	XMARKETPLACE_CONTENTOFFER_INFO *pOfferInfo = NULL;
	S32 iProductCount = 0;
	S32 i, j;

	if (pEntity == NULL || // No player
		!bAllOffersEnumerated || // All offers are not enumerated yet
		!bPurchaseNotificationInProgress || // All the purchase notifications are sent
		!gclServerIsConnected()) // We are not connected to the game server yet
		return;

	// VAS 112811
	// I would just like to comment that there is LITERALLY NO WAY ANY OF THIS WILL WORK ANYMORE
	// The whole codepath being used here is deprecated. If you need to find some way to resurrect this, talk to Vinay (if he still works here).

	// Set the category names
	if (availableCategoryName[0] == 0)
	{
		sprintf(availableCategoryName, "%s.Units", GetShortProductName());
	}
	if (specialCategoryName[0] == 0)
	{
		sprintf(specialCategoryName, "%s.SpecialItems", GetShortProductName());
	}

	// Get available products
	bAvailableProductsLoaded = gclAPGetProductList(availableCategoryName, &availableProducts);

	// Get special products
	bSpecialProductsLoaded = gclAPGetProductList(specialCategoryName, &specialProducts);

	// Did we get all the products
	if (bAvailableProductsLoaded && bSpecialProductsLoaded)
	{
		// Iterate through all lists and send notifications for the ones that is purchased
		for (j = 0; j < 2; j++)
		{
			currentProductList = j == 0 ? availableProducts : specialProducts;
			iProductCount = eaSize(&currentProductList);
			for (i = 0; i < iProductCount; i++)
			{
				pProduct = currentProductList[i];
				if (pProduct)
				{
					// Get the offer related to this product
					pOfferInfo = xStore_GetOfferByProduct(pProduct);
					if (pOfferInfo && 
						pOfferInfo->fUserHasPurchased &&
						gclAPPrerequisitesMet(pProduct))
					{
						// This offer is purchased, let the game server know
						ServerCmd_gslAPCmdPurchaseProduct(availableCategoryName, pProduct->uID, pCurrency, NULL);
					}
				}
			}
		}

		// Notification is complete
		bPurchaseNotificationInProgress = false;
	}
}

bool xStore_IsProductAvailableInMarketPlace(SA_PARAM_NN_VALID AccountProxyProduct *pProduct)
{
	assert(pProduct);

	return xStore_GetOfferByProduct(pProduct) != NULL;
}

bool xStore_OpenMarketPlacePurchaseDialog(U64 iOfferId)
{
	DWORD dwResult;
	DWORD dwPlayerIndex;
	XMARKETPLACE_CONTENTOFFER_INFO *pOffer = NULL;

	// Get the offer
	dwPlayerIndex = xUtil_GetCurrentPlayerIndex();

	if (iOfferId == 0 || dwPlayerIndex == XUSER_INDEX_NONE)
	{
		return false;
	}

	// Show XBOX UI
	dwResult = XShowMarketplaceUI(dwPlayerIndex, XSHOWMARKETPLACEUI_ENTRYPOINT_CONTENTITEM, 
		iOfferId, -1);

	return ERROR_SUCCESS == dwResult;
}

static XMARKETPLACE_CONTENTOFFER_INFO * xStore_GetOfferByProduct(SA_PARAM_NN_VALID AccountProxyProduct *pProduct)
{
	S32 i;
	S32 iOfferCount = eaSize(&eaOffers);
	XMARKETPLACE_CONTENTOFFER_INFO *pOffer = NULL;

	assert(pProduct);

	if (iOfferCount <= 0 || pProduct == NULL)
	{
		return NULL;
	}

	// Iterate thru all offers
	for (i = 0; i < iOfferCount; i++)
	{
		// Get the offer
		pOffer = eaOffers[i];

		// See if the product and offer match
		if (pOffer->qwOfferID == pProduct->qwOfferID &&
			memcmp(pOffer->contentId, pProduct->contentId, sizeof(pOffer->contentId)) == 0)
		{
			return pOffer;
		}
	}

	return NULL;
}

// Handles the initialization for store related tasks
void xStore_Init(void)
{
	// Create the events for the XOVERLAPPED structs
	ZeroMemory(&overlappedEnumContent, sizeof(overlappedEnumContent));
	overlappedEnumContent.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	ZeroMemory(&overlappedMountContent, sizeof(overlappedMountContent));
	overlappedMountContent.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );	
	ZeroMemory(&overlappedEnumOffers, sizeof(overlappedEnumOffers));
	overlappedEnumOffers.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
}

// Per frame store action handler
void xStore_Tick(void)
{
	// Handle notifications
	xStore_HandleNotifications();

	// Enumerate all offers
	xStore_EnumerateOffers();

	// Send purchase notifications
	xStore_SendPurchaseNotifications();
}

// Handles XBOX notifications important to the store
static void xStore_HandleNotifications(void)
{
	// The listener
	static HANDLE hStoreNotificationListener = NULL;
	DWORD dwMsg = 0;
	DWORD dwNewPlayerIndex = xUtil_GetCurrentPlayerIndex();
	XUID xuidNewPlayer = 0;

	if (hStoreNotificationListener == NULL)
	{
		// Listener is not created successfully
		// Try to create a new one
		hStoreNotificationListener = XNotifyCreateListener(qwNotificationAreas);
	}

	if (hStoreNotificationListener == NULL)
	{
		// Bail out for this frame
		return;
	}

	// Do we have a sign in change?
	if (XNotifyGetNext(hStoreNotificationListener, XN_SYS_SIGNINCHANGED, &dwMsg, NULL) &&
		dwNewPlayerIndex != XUSER_INDEX_NONE)
	{
		// Is this a different player
		if (xUtil_GetCurrentPlayerXuid() != xuidPlayerForOffers)
		{
			// Re-enumerate offers
			bReenumerateOffers = true;
		}
	}

	// Is new content installed
	if (XNotifyGetNext(hStoreNotificationListener, XN_LIVE_CONTENT_INSTALLED, &dwMsg, NULL) &&
		dwNewPlayerIndex != XUSER_INDEX_NONE)
	{
		// Re-enumerate offers
		bReenumerateOffers = true;
		// Notify the server about the purchases
		xStore_BeginPurchaseNotification();
	}
}

// Enumerates all offers
static void xStore_EnumerateOffers(void)
{	
	static HANDLE hOfferEnumerator = INVALID_HANDLE_VALUE;
	static bool	bEnumerateNextOffer = false;
	static bool	bEnumerationActive = false;
	static DWORD cbOfferBufferSize = 0;
	static XMARKETPLACE_CONTENTOFFER_INFO *pOfferBuffer = NULL;

	DWORD dwResult = 0;
	DWORD dwStatus = 0;
	DWORD dwCurrentPlayerIndex = xUtil_GetCurrentPlayerIndex();

	// Do we want to reenumerate offers?
	if (bReenumerateOffers)
	{
		// We only want to reenumerate once
		bReenumerateOffers = false;

		// Reset the state variables
		bAllOffersEnumerated = false;
		bEnumerateNextOffer = true;
		bEnumerationActive = true;
		cbOfferBufferSize = 0;

		// Kill any active operation if necessary
		if (!XHasOverlappedIoCompleted(&overlappedEnumOffers))
		{
			XCancelOverlapped(&overlappedEnumOffers);
		}

		// Destroy the elements and the array holding the offers
		eaDestroyEx(&eaOffers, NULL);

		// Clean up the buffer if necessary
		if (pOfferBuffer)
		{
			free(pOfferBuffer);
			pOfferBuffer = NULL;
		}

		// Free the handle
		if (hOfferEnumerator != INVALID_HANDLE_VALUE)
		{
			XCloseHandle(hOfferEnumerator);
			hOfferEnumerator = INVALID_HANDLE_VALUE;
		}		
	}

	// Create the offer enumerator if necessary
	if (hOfferEnumerator == INVALID_HANDLE_VALUE)
	{
		if (dwCurrentPlayerIndex != XUSER_INDEX_NONE)
		{			
			if (ERROR_SUCCESS == XMarketplaceCreateOfferEnumerator(
				dwCurrentPlayerIndex,						// Get the offer data for this player
				XMARKETPLACE_OFFERING_TYPE_CONTENT, // Marketplace content
				0xffffffff,							// Retrieve all content categories
				1,									// Number of results per call to XEnumerate
				&cbOfferBufferSize,					// Size of buffer needed for results
				&hOfferEnumerator))					// Enumeration handle
			{
				// We want to start enumerating all offers
				bEnumerateNextOffer = true;
				bEnumerationActive = true;
				bAllOffersEnumerated = false;
				// Get the XUID for the offers
				xuidPlayerForOffers = xUtil_GetCurrentPlayerXuid();
			}
		}
	}

	if (hOfferEnumerator == INVALID_HANDLE_VALUE)
	{
		// No enumerator for us
		return;
	}

	// Once we have called XEnumerate then next batch of results is ready 
	// when the overlapped operation is complete.
	if (bEnumerationActive && !bEnumerateNextOffer && XHasOverlappedIoCompleted(&overlappedEnumOffers))
	{
		dwResult = 0;
		dwStatus = XGetOverlappedResult(&overlappedEnumOffers, &dwResult, FALSE);

		if (ERROR_SUCCESS == dwStatus)
		{			
			// Add the result to the array
			if (pOfferBuffer)
			{
				eaPush(&eaOffers, pOfferBuffer);
				pOfferBuffer = NULL;
			}

			bEnumerateNextOffer = true;
		}
		else
		{
			// Free the buffer
			if (pOfferBuffer)
			{
				free(pOfferBuffer);
				pOfferBuffer = NULL;
			}

			// We are done
			bAllOffersEnumerated = true;
			bEnumerationActive = false;
		}
	}

	// Repeatedly call XEnumerate until we get ERROR_NO_MORE_FILES. bEnumerateNextOffer
	// indicates if we still need to call XEnumerate
	if (bEnumerationActive && bEnumerateNextOffer)
	{
		// Allocate the buffer required for enumeration
		pOfferBuffer = calloc(1, cbOfferBufferSize);

		dwStatus = XEnumerate(hOfferEnumerator, pOfferBuffer, cbOfferBufferSize, NULL, &overlappedEnumOffers);

		if (ERROR_IO_PENDING == dwStatus)
		{
			bEnumerateNextOffer = false;
		}		
		else
		{
			bEnumerationActive = false;
		}
	}
}

#endif