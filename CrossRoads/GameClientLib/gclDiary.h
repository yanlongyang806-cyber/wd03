/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "DiaryEnums.h"

AUTO_STRUCT;
typedef struct TagListRow
{
	int bitNum;
	bool permanent;
	STRING_POOLED String;				AST(POOL_STRING)
} TagListRow;

AUTO_STRUCT;
typedef struct DiaryEntryTypeItem
{
	const char *message;
	int value;
} DiaryEntryTypeItem;