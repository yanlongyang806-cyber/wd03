/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

AUTO_STRUCT;
typedef struct CCGPackNameAndCount
{
	STRING_POOLED name;						AST(KEY POOL_STRING)
	U32 count;
} CCGPackNameAndCount;

AUTO_STRUCT;
typedef struct CCGPackRequests
{
	EARRAY_OF(CCGPackNameAndCount) requests;
} CCGPackRequests;

void CCG_AddPackRequest(U32 **packIDsHandle, CCGPackRequests *packRequests, char *packName, U32 count);