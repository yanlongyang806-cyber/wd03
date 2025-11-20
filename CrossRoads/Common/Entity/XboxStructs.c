/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "XboxStructs.h"
#include "XBoxStructs_h_ast.h"

#if _XBOX

void xBoxStructConvertToCrypticXnAddr(SA_PARAM_NN_VALID XNADDR *pSource, SA_PARAM_NN_VALID NOCONST(CrypticXnAddr) *pDest)
{
	assert(pSource);
	assert(pDest);

	pDest->ina = pSource->ina.s_addr;
	pDest->inaOnline = pSource->inaOnline.s_addr;
	pDest->wPortOnline = pSource->wPortOnline;
	memcpy(pDest->abEnet, pSource->abEnet, sizeof(pSource->abEnet));
	memcpy(pDest->abOnline, pSource->abOnline, sizeof(pSource->abOnline));
}

void xBoxStructConvertToXNADDR(SA_PARAM_NN_VALID CrypticXnAddr *pSource, SA_PARAM_NN_VALID XNADDR *pDest)
{
	assert(pSource);
	assert(pDest);

	pDest->ina.s_addr = pSource->ina;
	pDest->inaOnline.s_addr = pSource->inaOnline;
	pDest->wPortOnline = pSource->wPortOnline;
	memcpy(pDest->abEnet, pSource->abEnet, sizeof(pSource->abEnet));
	memcpy(pDest->abOnline, pSource->abOnline, sizeof(pSource->abOnline));
}

void xBoxStructConvertToCrypticXnkId(SA_PARAM_NN_VALID XNKID *pSource, SA_PARAM_NN_VALID NOCONST(CrypticXnkId) *pDest)
{
	assert(pSource);
	assert(pDest);

	memcpy(pDest->xnkid, pSource->ab, sizeof(pSource->ab));
}

void xBoxStructConvertToXNKID(SA_PARAM_NN_VALID CrypticXnkId *pSource, SA_PARAM_NN_VALID XNKID *pDest)
{
	assert(pSource);
	assert(pDest);

	memcpy(pDest->ab, pSource->xnkid, sizeof(pSource->xnkid));
}

void xBoxStructConvertToCrypticXSessionInfo(SA_PARAM_NN_VALID XSESSION_INFO *pSource, SA_PARAM_NN_VALID NOCONST(CrypticXSessionInfo) *pDest, U64 sessionNonce)
{
	assert(pSource);
	assert(pDest);

	// Session Nonce
	pDest->sessionNonce = sessionNonce;

	// Session ID
	if (pDest->sessionID == NULL)
	{
		pDest->sessionID = StructCreate(parse_CrypticXnkId);
	}
	xBoxStructConvertToCrypticXnkId(&pSource->sessionID, pDest->sessionID);

	// Host Address
	if (pDest->hostAddress == NULL)
	{
		pDest->hostAddress = StructCreate(parse_CrypticXnAddr);
	}
	xBoxStructConvertToCrypticXnAddr(&pSource->hostAddress, pDest->hostAddress);

	// Key exchange key
	memcpy(pDest->keyExchangeKey, pSource->keyExchangeKey.ab, sizeof(pSource->keyExchangeKey.ab));
}

void xBoxStructConvertToXSESSION_INFO(SA_PARAM_NN_VALID CrypticXSessionInfo *pSource, SA_PARAM_NN_VALID XSESSION_INFO *pDest)
{
	assert(pSource);
	assert(pDest);

	// Session ID
	xBoxStructConvertToXNKID(pSource->sessionID, &pDest->sessionID);

	// Host Address
	xBoxStructConvertToXNADDR(pSource->hostAddress, &pDest->hostAddress);

	// Key exchange key
	memcpy(pDest->keyExchangeKey.ab, pSource->keyExchangeKey, sizeof(pDest->keyExchangeKey.ab));
}

#endif

#include "AutoGen/XboxStructs_h_ast.c"