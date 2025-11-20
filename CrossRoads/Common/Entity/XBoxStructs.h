/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

// Duplicate of XNADDR defined in <Winsockx.h>
AUTO_STRUCT AST_CONTAINER;
typedef struct CrypticXnAddr
{
	// This is defined as IN_ADDR in XNADDR
	const U32 ina;									AST(PERSIST SUBSCRIBE)
	// This is defined as IN_ADDR in XNADDR
	const U32 inaOnline;							AST(PERSIST SUBSCRIBE)
	const U16 wPortOnline;							AST(PERSIST SUBSCRIBE)
	const U8 abEnet[6];								AST(PERSIST SUBSCRIBE)
	const U8 abOnline[20];							AST(PERSIST SUBSCRIBE)
} CrypticXnAddr;

// XBOX specific data for the player
AUTO_STRUCT AST_CONTAINER;
typedef struct XBoxSpecificData
{
	// The XBOX network address of the player
	CONST_OPTIONAL_STRUCT(CrypticXnAddr) pXnAddr;			AST(PERSIST SUBSCRIBE FORCE_CONTAINER) 

	// Player's XUID
	const U64 xuid;											AST(PERSIST SUBSCRIBE)
} XBoxSpecificData;

AUTO_STRUCT AST_CONTAINER;
typedef struct CrypticXnkId
{
	const U8 xnkid[8];								AST(PERSIST SUBSCRIBE)
} CrypticXnkId;


// Duplicate of XSESSION_INFO defined in <xonline.h> (with the addition of session nonce
AUTO_STRUCT AST_CONTAINER;
typedef struct CrypticXSessionInfo
{
	CONST_OPTIONAL_STRUCT(CrypticXnkId) sessionID;		AST(PERSIST SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(CrypticXnAddr) hostAddress;	AST(PERSIST SUBSCRIBE)
	const U8 keyExchangeKey[16];						AST(PERSIST SUBSCRIBE)
	const U64 sessionNonce;								AST(PERSIST SUBSCRIBE)
} CrypticXSessionInfo;

#if _XBOX

#include "utils.h"
#include <Xtl.h>
#include <xonline.h>

typedef struct NOCONST(CrypticXnAddr) NOCONST(CrypticXnAddr);
typedef struct NOCONST(CrypticXnkId) NOCONST(CrypticXnkId);
typedef struct NOCONST(CrypticXSessionInfo) NOCONST(CrypticXSessionInfo);

C_DECLARATIONS_BEGIN

void xBoxStructConvertToCrypticXnAddr(XNADDR *pSource, NOCONST(CrypticXnAddr) *pDest);

void xBoxStructConvertToXNADDR(SA_PARAM_NN_VALID CrypticXnAddr *pSource, SA_PARAM_NN_VALID XNADDR *pDest);

void xBoxStructConvertToCrypticXnkId(SA_PARAM_NN_VALID XNKID *pSource, SA_PARAM_NN_VALID NOCONST(CrypticXnkId) *pDest);

void xBoxStructConvertToXNKID(SA_PARAM_NN_VALID CrypticXnkId *pSource, SA_PARAM_NN_VALID XNKID *pDest);

void xBoxStructConvertToCrypticXSessionInfo(SA_PARAM_NN_VALID XSESSION_INFO *pSource, SA_PARAM_NN_VALID NOCONST(CrypticXSessionInfo) *pDest, U64 sessionNonce);

void xBoxStructConvertToXSESSION_INFO(SA_PARAM_NN_VALID CrypticXSessionInfo *pSource, SA_PARAM_NN_VALID XSESSION_INFO *pDest);

C_DECLARATIONS_END

#endif