/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef AUCTIONSERVER_H
#define AUCTIONSERVER_H

#include "GlobalTypeEnum.h"
#include "StashTable.h"


AUTO_STRUCT;
typedef struct SearchTermTable
{
	const char *term;		AST(KEY)
	StashTable tokens;
	U32 hitcount;			AST(NAME(stthc))
} SearchTermTable;


#endif //AUCTIONSERVER_H