/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

AUTO_STRUCT;
typedef struct ActivityLogDisplayEntry
{
	U32 entryID;			AST(KEY)
	U32 time;
	char *text;				AST(ESTRING)
} ActivityLogDisplayEntry;