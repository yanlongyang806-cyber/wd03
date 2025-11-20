#include "billingProduct.h"
#include "billing.h"
#include "vindicia.h"
#include "estring.h"
#include "Product.h"
#include "error.h"

// -------------------------------------------------------------------------------------------
// Update Product

typedef struct UpdateProductData
{
	char *pProductName;
} UpdateProductData;

// -------------------------------------------------------------------------------------------

static void btProductPush_Complete(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdateProductData *pData = pTrans->userData;
	
	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(prd, updateResponse));
	if(pResult)
	{
		struct prd__updateResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("prd__update", pResponse);
		btFreeObjResult(pTrans, pResult);
	}
	PERFINFO_AUTO_STOP();
}

static void btProductPush_FetchComplete(BillingTransaction *pTrans)
{
	bool bContinuing = false;
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdateProductData *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(prd, fetchByMerchantProductIdResponse));

	if(pResult)
	{
		const ProductContainer *pProduct = findProductByName(pData->pProductName);
		struct prd__fetchByMerchantProductIdResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("prd__fetchByMerchantProductId", pResponse);
		btFreeObjResult(pTrans, pResult);

		if(pProduct)
		{
			struct prd__update *pPrdUpdate = btAlloc(pTrans, pPrdUpdate, struct prd__update);
			pPrdUpdate->_auth = getVindiciaAuth();
			pPrdUpdate->_duplicateBehavior = vin__DuplicateBehavior__SucceedIgnore;

			if(pResponse->_product->VID && *pResponse->_product->VID)
			{
				BILLING_DEBUG("Found product '%s', updating...\n", pData->pProductName);
				pPrdUpdate->_product = pResponse->_product;
			}
			else
			{
				BILLING_DEBUG("No match for product '%s', creating...\n", pData->pProductName);
				pPrdUpdate->_product = btAlloc(pTrans, pPrdUpdate->_product, struct vin__Product);
			}

			{
				char *xml = NULL;
				struct vin__Product *pVinProduct = pPrdUpdate->_product;
				enum vin__TaxClassification *taxClass = btAlloc(pTrans, taxClass, enum vin__TaxClassification);
				*taxClass = btConvertTaxClassification(pProduct->eTaxClassification);

				pVinProduct->merchantProductId          = btStrdupWithPrefix(pTrans, pData->pProductName);
				pVinProduct->description                = (char *)NULL_TO_EMPTY(pProduct->pDescription);
				pVinProduct->billingStatementIdentifier = (char *)NULL_TO_EMPTY(pProduct->pBillingStatementIdentifier);
				pVinProduct->taxClassification			= taxClass;

				if(vindiciaObjtoXML(&xml, pPrdUpdate, VO2X_OBJ(prd, update)))
				{
					btContinue(pTrans, "prd:update", xml, btProductPush_Complete, pData);
					bContinuing = true;
				}
				estrDestroy(&xml);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

BillingTransaction * btProductPush(const ProductContainer *pProduct)
{
	struct BillingTransaction *pTrans = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(pProduct->uFlags & PRODUCT_BILLING_SYNC)
	{
		struct UpdateProductData  *pData  = NULL;
		char *xml = NULL;
		struct prd__fetchByMerchantProductId *p;

		if (pProduct->eTaxClassification == TCNotApplicable)
		{
			ErrorOrAlert("ACCOUNTSERVER_PRODUCTPUSH_TAX", "Please provide a tax classification for %s other than %s!", pProduct->pName, convertTaxClassificationToString(pProduct->eTaxClassification));
			PERFINFO_AUTO_STOP();
			return pTrans;
		}

		p = callocStruct(struct prd__fetchByMerchantProductId);

		BILLING_DEBUG_START;

		p->_auth = getVindiciaAuth();
		estrPrintf(&p->_merchantProductId, "%s%s", billingGetPrefix(), pProduct->pName);

		if(vindiciaObjtoXML(&xml, p, VO2X_OBJ(prd, fetchByMerchantProductId)))
		{
			pTrans = btCreate("prd:fetchByMerchantProductId", xml, btProductPush_FetchComplete, NULL, false);
			pData = btAllocUserData(pTrans, sizeof(struct UpdateProductData));
			pData->pProductName = btStrdup(pTrans, pProduct->pName);
		}

		estrDestroy(&xml);
		estrDestroy(&p->_merchantProductId);
		free(p);
	}

	PERFINFO_AUTO_STOP();
	return pTrans;
}
