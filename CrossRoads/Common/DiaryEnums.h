/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

AUTO_ENUM;
typedef enum DiaryEntryType
{
	DiaryEntryType_None,				// used for queries that don't care about type
	DiaryEntryType_Perk,
	DiaryEntryType_Blog,
	DiaryEntryType_Mission,
	DiaryEntryType_Activity
} DiaryEntryType;