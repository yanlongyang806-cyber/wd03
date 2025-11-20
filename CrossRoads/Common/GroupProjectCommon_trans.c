/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "GroupProjectCommon.h"
#include "stdtypes.h"
#include "itemCommon.h"
#include "earray.h"
#include "GlobalTypeEnum.h"
#include "AutoTransDefs.h"
#include "ReferenceSystem.h"
#include "timing.h"
#include "logging.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"

AUTO_TRANS_HELPER;
bool
GroupProject_trh_GetProjectConstant(ATR_ARGS, ATH_ARG NOCONST(GroupProjectState) *projectState, const char *constantName, S32 *valueOut)
{
    GroupProjectConstant *constant;
    GroupProjectDef *projectDef;
	int constantIndex;

    if ( ISNULL(valueOut) )
    {
        return false;
    }

    if ( ISNULL(projectState) || ISNULL(constantName) )
    {
        *valueOut = 0;
        return false;
    }

    projectDef = GET_REF(projectState->projectDef);
    if ( ISNULL(projectDef) )
    {
        *valueOut = 0;
        return false;
    }

	constantIndex = GroupProject_FindConstant(projectDef, constantName);
	constant = eaGet(&projectDef->constants, constantIndex);
    if ( ISNULL(constant) )
    {
        *valueOut = 0;
        return false;
    }

    *valueOut = constant->value;
    return true;
}

AUTO_TRANS_HELPER;
void
GroupProject_trh_FinalizeTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *taskDef, const char *projectName)
{
    taskSlot->state = DonationTaskState_Finalized;
    taskSlot->finalizedTime = timeSecondsSince2000();
    taskSlot->completionTime = taskSlot->finalizedTime + taskDef->secondsToComplete;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "FinalizeTask", 
        "ProjectName %s TaskSlot %d TaskName %s", projectName, taskSlot->taskSlotNum, taskDef->name);
    return;
}

AUTO_TRANS_HELPER;
void
GroupProject_trh_CancelTask(ATR_ARGS, ATH_ARG NOCONST(DonationTaskSlot) *taskSlot, DonationTaskDef *taskDef, const char *projectName)
{
    taskSlot->state = DonationTaskState_Canceled;
    taskSlot->finalizedTime = timeSecondsSince2000();
    taskSlot->completionTime = taskSlot->finalizedTime;

    TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GROUPPROJECT, "CancelTask",
        "ProjectName %s TaskSlot %d TaskName %s", projectName, taskSlot->taskSlotNum, taskDef->name);
}