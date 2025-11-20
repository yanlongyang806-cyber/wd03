#include "wiProducts.h"
#include "wiCommon.h"
#include "timing.h"
#include "StringUtil.h"
#include "Product.h"
#include "EString.h"
#include "wiProducts_c_ast.h"
#include "url.h"
#include "ImportExport.h"

/************************************************************************/
/* Product import/export support                                        */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWIImportExportSession
{
	INT_EARRAY eaProductIDs;
} ASWIImportExportSession;

SA_RET_OP_VALID static ASWIImportExportSession * wiGetImportExportSession(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return NULL;

	return wiGetSessionStruct(pWebRequest, "ImportExportSession", parse_ASWIImportExportSession);
}

static bool wiProductSetToExport(SA_PARAM_NN_VALID ASWebRequest *pWebRequest, U32 uProductID)
{
	ASWIImportExportSession * pSession = NULL;
	bool bFound = false;

	if (!verify(pWebRequest)) return false;
	if (!verify(uProductID)) return false;

	PERFINFO_AUTO_START_FUNC();

	pSession = wiGetImportExportSession(pWebRequest);
	if (devassert(pSession))
	{
		if (eaiFind(&pSession->eaProductIDs, uProductID) > -1)
		{
			bFound = true;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return bFound;
}

static void wiAddProductToExport(SA_PARAM_NN_VALID ASWebRequest *pWebRequest, U32 uProductID)
{
	ASWIImportExportSession * pSession = NULL;

	if (!verify(pWebRequest)) return;
	if (!verify(uProductID)) return;

	PERFINFO_AUTO_START_FUNC();

	pSession = wiGetImportExportSession(pWebRequest);
	if (devassert(pSession))
	{
		eaiPushUnique(&pSession->eaProductIDs, uProductID);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void wiRemoveProductFromExport(SA_PARAM_NN_VALID ASWebRequest *pWebRequest, U32 uProductID)
{
	ASWIImportExportSession * pSession = NULL;

	if (!verify(pWebRequest)) return;
	if (!verify(uProductID)) return;

	PERFINFO_AUTO_START_FUNC();

	pSession = wiGetImportExportSession(pWebRequest);
	if (devassert(pSession))
	{
		eaiFindAndRemove(&pSession->eaProductIDs, uProductID);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

SA_RET_OP_VALID static INT_EARRAY wiGetProductExportList(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	ASWIImportExportSession * pSession = NULL;

	if (!verify(pWebRequest)) return NULL;

	pSession = wiGetImportExportSession(pWebRequest);
	if (devassert(pSession))
	{
		return pSession->eaProductIDs;
	}

	return NULL;
}

static void wiClearProductsFromExport(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	ASWIImportExportSession * pSession = NULL;

	if (!verify(pWebRequest)) return;

	pSession = wiGetImportExportSession(pWebRequest);
	if (devassert(pSession))
	{
		eaiDestroy(&pSession->eaProductIDs);
	}

	return;
}


/************************************************************************/
/* Product list support                                                 */
/************************************************************************/

AUTO_STRUCT;
typedef struct ASWIProductsListEntry
{
	const char *pName;			AST(UNOWNED)
	const char *pCategories;	AST(UNOWNED)
	const char *pInternalName;	AST(UNOWNED)
	char *pShards;				AST(ESTRING)
	char *pPermissions;			AST(ESTRING)
	char *pRequiredSubs;		AST(ESTRING)
	bool bSync;
	bool bSetToExport;
} ASWIProductsListEntry;

AUTO_STRUCT;
typedef struct ASWIProductsList
{
	const char *pViewPage; AST(UNOWNED)
	const char *pSelf; AST(UNOWNED)
	EARRAY_OF(ASWIProductsListEntry) eaEntries;
} ASWIProductsList;

static void wiAddProductToList(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
							   ASWIProductsList *pProductsList,
							   SA_PARAM_NN_VALID const ProductContainer *pProduct)
{
	ASWIProductsListEntry *pEntry = NULL;

	if (!verify(pWebRequest)) return;
	if (!verify(pProductsList)) return;
	if (!verify(pProduct)) return;

	PERFINFO_AUTO_START_FUNC();

	pEntry = StructCreate(parse_ASWIProductsListEntry);

	pEntry->pName = pProduct->pName;
	pEntry->pCategories = pProduct->pCategoriesString;
	pEntry->pInternalName = pProduct->pInternalName;
	pEntry->pShards = NULL;
	pEntry->pPermissions = NULL;
	pEntry->bSync = pProduct->uFlags & PRODUCT_BILLING_SYNC ? true : false;
	pEntry->bSetToExport = wiProductSetToExport(pWebRequest, pProduct->uID);

	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppShards, iCurShard, iNumShards);
	{
		estrConcatf(&pEntry->pShards, "%s%s", iCurShard ? "," : "", pProduct->ppShards[iCurShard]);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppRequiredSubscriptions, iCurRequiredSub, iNumRequiredSubs);
	{
		estrConcatf(&pEntry->pRequiredSubs, "%s%s", iCurRequiredSub ? "," : "", pProduct->ppRequiredSubscriptions[iCurRequiredSub]);
	}
	EARRAY_FOREACH_END;

	concatPermissionString(pProduct->ppPermissions, &pEntry->pPermissions);

	eaPush(&pProductsList->eaEntries, pEntry);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Index                                                                */
/************************************************************************/

static void wiHandleProductsIndex(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* View                                                                 */
/************************************************************************/

static void wiHandleProductsView(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* List                                                                 */
/************************************************************************/

static void wiHandleProductsList(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	const char *pSelf = "list" WI_EXTENSION;
	const char *pViewPage = "view" WI_EXTENSION;
	EARRAY_OF(ProductContainer) eaProducts = NULL;
	ASWIProductsList aswiProductsList = {0};

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_ASWIProductsList, &aswiProductsList);

	aswiProductsList.pViewPage = pViewPage;
	aswiProductsList.pSelf = pSelf;

	if (wiSubmitted(pWebRequest, "export"))
	{
		const char *pProductName = wiGetString(pWebRequest, "product");
		const ProductContainer *pProduct = NULL;

		if (pProductName && *pProductName)
		{
			pProduct = findProductByName(pProductName);
		}

		if (pProduct)
		{
			wiAddProductToExport(pWebRequest, pProduct->uID);
			wiAppendMessageBox(pWebRequest, "Success", STACK_SPRINTF("%s added to export list.", pProduct->pName), 0);
		}
		else
		{
			wiAppendMessageBox(pWebRequest, "Invalid", "Invalid product specified for export.", WMBF_Error);
		}
	}

	if (wiSubmitted(pWebRequest, "unexport"))
	{
		const char *pProductName = wiGetString(pWebRequest, "product");
		const ProductContainer *pProduct = NULL;

		if (pProductName && *pProductName)
		{
			pProduct = findProductByName(pProductName);
		}

		if (pProduct)
		{
			wiRemoveProductFromExport(pWebRequest, pProduct->uID);
			wiAppendMessageBox(pWebRequest, "Success", STACK_SPRINTF("%s removed from export list.", pProduct->pName), 0);
		}
		else
		{
			wiAppendMessageBox(pWebRequest, "Invalid", "Invalid product specified for export removal.", WMBF_Error);
		}
	}

	eaProducts = getProductList(PRODUCTS_ALL);
	EARRAY_CONST_FOREACH_BEGIN(eaProducts, iCurProduct, iNumProducts);
	{
		const ProductContainer *pProduct = eaProducts[iCurProduct];
		wiAddProductToList(pWebRequest, &aswiProductsList, pProduct);
	}
	EARRAY_FOREACH_END;

	wiAppendStruct(pWebRequest, "ProductsList.cs", parse_ASWIProductsList, &aswiProductsList);

	StructDeInit(parse_ASWIProductsList, &aswiProductsList);

	eaDestroy(&eaProducts); // DO NOT FREE CONTENTS

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Create                                                               */
/************************************************************************/

static void wiHandleProductsCreate(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "soon.html");

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Export                                                               */
/************************************************************************/

static void wiHandleProductsExport(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	const char *pSelf = "export" WI_EXTENSION;
	const char *pViewPage = "view" WI_EXTENSION;
	INT_EARRAY eaProductIDs = NULL;

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	if (wiSubmitted(pWebRequest, "unexport"))
	{
		const char *pProductName = wiGetString(pWebRequest, "product");
		const ProductContainer *pProduct = NULL;

		if (pProductName && *pProductName)
		{
			pProduct = findProductByName(pProductName);
		}

		if (pProduct)
		{
			wiRemoveProductFromExport(pWebRequest, pProduct->uID);
			wiAppendMessageBox(pWebRequest, "Success", STACK_SPRINTF("%s removed from export list.", pProduct->pName), 0);
		}
		else
		{
			wiAppendMessageBox(pWebRequest, "Invalid", "Invalid product specified for export removal.", WMBF_Error);
		}
	}

	eaProductIDs = wiGetProductExportList(pWebRequest);

	if (eaProductIDs && wiGetInt(pWebRequest, "export", 0))
	{
		Export *pExport = CreateExport();
		int iCurProductID = 0;
		char *pSerialized = NULL;

		for (iCurProductID = 0; iCurProductID < eaiSize(&eaProductIDs); iCurProductID++)
		{
			const ProductContainer *pProduct = findProductByID(eaProductIDs[iCurProductID]);
			AddExportEntry(pExport, parse_ProductContainer, pProduct);
		}

		pSerialized = SerializeExport(pExport, wiGetUsername(pWebRequest), wiGetString(pWebRequest, "desc"), PRODUCT_EXPORT_VERSION);

		DestroyExport(&pExport);

		if (devassert(pSerialized))
		{
			wiSetAttachmentMode(pWebRequest, "ProductExport.axp");
			wiAppendStringf(pWebRequest, "%s", pSerialized);

			estrDestroy(&pSerialized);

			wiClearProductsFromExport(pWebRequest);
		}
	}
	else if (eaProductIDs)
	{
		ASWIProductsList aswiProductsList = {0};
		int iCurProductID = 0;

		StructInit(parse_ASWIProductsList, &aswiProductsList);

		aswiProductsList.pViewPage = pViewPage;
		aswiProductsList.pSelf = pSelf;

		for (iCurProductID = 0; iCurProductID < eaiSize(&eaProductIDs); iCurProductID++)
		{
			const ProductContainer *pProduct = findProductByID(eaProductIDs[iCurProductID]);
			wiAddProductToList(pWebRequest, &aswiProductsList, pProduct);
		}

		wiAppendStruct(pWebRequest, "Export.cs", parse_ASWIProductsList, &aswiProductsList);

		StructDeInit(parse_ASWIProductsList, &aswiProductsList);
	}
	else
	{
		wiAppendMessageBox(pWebRequest, "No Products", "You have no products set to export.  Please add some from the list page.", 0);
	}

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Import                                                               */
/************************************************************************/

static void wiHandleProductsImport(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	const char *pSelf = "import" WI_EXTENSION;
	const char *pViewPage = "view" WI_EXTENSION;
	ASWIProductsList aswiProductsList = {0};

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	if (wiSubmitted(pWebRequest, "import"))
	{
		const char *pImportFile = wiGetString(pWebRequest, "importFile");
		Export *pExport = NULL;
		int iNumEntries = 0;

		if (pImportFile)
		{
			pExport = UnserializeExport(pImportFile, PRODUCT_EXPORT_VERSION);
		}

		if (pExport)
		{
			iNumEntries = GetNumExportEntries(pExport);
		}

		if (iNumEntries > 0)
		{
			int iCurEntry = 0;

			for (iCurEntry = 0; iCurEntry < iNumEntries; iCurEntry++)
			{
				ProductContainer *pProduct = GetExportEntry(pExport, parse_ProductContainer, iCurEntry);

				if (devassert(pProduct))
				{
					productReplace(pProduct); // Takes ownership
				}
			}

			DestroyExport(&pExport);

			wiAppendMessageBox(pWebRequest, "Imported", "Products have been imported.", 0);
		}
		else
		{
			wiAppendMessageBox(pWebRequest, "Error", "Product import file invalid.", WMBF_Error|WMBF_BackButton);
		}
	}
	else
	{
		StructInit(parse_ASWIProductsList, &aswiProductsList);

		aswiProductsList.pViewPage = pViewPage;
		aswiProductsList.pSelf = pSelf;

		wiAppendStruct(pWebRequest, "Import.cs", parse_ASWIProductsList, &aswiProductsList);

		StructDeInit(parse_ASWIProductsList, &aswiProductsList);
	}

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Handler                                                              */
/************************************************************************/

bool wiHandleProducts(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	bool bHandled = false;

	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

#define WI_PRODUCT_PAGE(page) \
	if (!stricmp_safe(wiGetPath(pWebRequest), WI_PRODUCTS_DIR #page WI_EXTENSION)) \
	{ \
		wiHandleProducts##page(pWebRequest); \
		bHandled = true; \
	}

	WI_PRODUCT_PAGE(Index);
	WI_PRODUCT_PAGE(View);
	WI_PRODUCT_PAGE(List);
	WI_PRODUCT_PAGE(Create);
	WI_PRODUCT_PAGE(Export);
	WI_PRODUCT_PAGE(Import);

#undef WI_PRODUCT_PAGE

	PERFINFO_AUTO_STOP_FUNC();

	return bHandled;
}

#include "wiProducts_c_ast.c"