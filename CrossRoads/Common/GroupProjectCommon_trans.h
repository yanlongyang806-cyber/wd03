/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"

typedef struct NOCONST(GroupProjectState) NOCONST(GroupProjectState);
typedef struct NOCONST(DonationTaskSlot) NOCONST(DonationTaskSlot);
//
// Common transaction helpers.
//
bool GroupProject_trh_GetProjectConstant(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *constantName, S32 *valueOut);
void GroupProject_trh_FinalizeTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *taskDef, const char *projectName);
void GroupProject_trh_CancelTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *taskDef, const char *projectName);
