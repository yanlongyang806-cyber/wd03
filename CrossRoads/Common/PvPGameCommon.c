/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "PvPGameCommon.h"

#include "queue_common.h"
#include "queue_common_structs.h"

#include "PvPGameCommon_h_ast.h"

ParseTable *pvpGame_GetGroupParseTable(PVPGameType eType)
{
	switch(eType)
	{
	case kPVPGameType_CaptureTheFlag:
		return parse_CTFGroupParams;
	case kPVPGameType_Domination:
		return parse_DOMGroupParams;
	case kPVPGameType_LastManStanding:
		return parse_LMSGroupParams;
	case kPVPGameType_Deathmatch:
		return parse_DMGroupParams;
	case kPVPGameType_Custom:
		return parse_CUSGroupParams;
	}

	return NULL;
}

#include "PvPGameCommon_h_ast.c"