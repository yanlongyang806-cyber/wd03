/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclAIDebugUI.h"

#include "aiDebugShared.h"
#include "cmdparse.h"
#include "Color.h"
#include "file.h"
#include "EntityIterator.h"
#include "gclBaseStates.h"
#include "gclEntity.h"
#include "GfxPrimitive.h"
#include "GlobalStateMachine.h"
#include "inputMouse.h"
#include "Player.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "textparser.h"
#include "tokenstore.h"
#include "wcoll/collcache.h"
#include "WorldLib.h"
#include "WorldGrid.h"

#include "Prefs.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UIComboBox.h"
#include "UIList.h"
#include "UIMenu.h"
#include "UIPane.h"
#include "UISkin.h"
#include "UISlider.h"
#include "UIWindow.h"
#include "CombatConfig.h"
#include "aiStruct.h"
#include "aiConfig.h"

#include "aiDebugShared_h_ast.h"
//#include "gclAIDebugUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("aiDebugShared.h", BUDGET_GameSystems););

void aiDebugView(int bShow);
static void gclAIDebug_FixupStatusColumns(AIDebugAggroTableHeader** eaAggroTableHeaders);


static AIDebug* aidebug = 0;
static U32 s_lastStatusHideFlags = 0;

static UIPane* s_Pane;
bool g_aiDebugShowing = false;
static U32 s_uiHideUntil = 0;

static UIList* basicInfo;
static UIList* statusEntries;
static UIList* statusExternEntries;
static UIList* powBasicInfo;
static UIList* powersInfo;
static UIList* teamBasicInfo;
static UIList* combatTeamBasicInfo;
static UIList* teamInfo;
static UIList* combatTeamInfo;
static UIList* teamAssignmentsInfo;
static UIList* combatTeamAssignmentsInfo;
static UIList* logEntries;
static UIList* varInfo;
static UIList* exVarInfo;
static UIList* msgInfo;
static UIList* externVarInfo;
static UIList* messageInfo;
static UIList* configMods;

static UISkin s_skinPane = {0};
static UISkin s_skinLabelMild = {0};
static UISkin s_skinLabelBold = {0};
static UISkin s_skinToggleGroup = {0};
static UISkin s_skinList = {0};

static UIStyleFont *s_ListFont = NULL;

int g_AggroDraw3D = 0;
AUTO_CMD_INT(g_AggroDraw3D, AggroDraw3D);

int g_AggroDrawDisableNormal = 0;
AUTO_CMD_INT(g_AggroDrawDisableNormal, AggroDrawNoNormal);

int g_AggroDrawDisableSoc = 0;
AUTO_CMD_INT(g_AggroDrawDisableSoc, AggroDrawNoSocial);

static void BIString(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	if(aidebug->basicInfo[row]->str)
		estrConcatf(output, "%s", aidebug->basicInfo[row]->str);
}

// Status Table
static AIDebugStatusTableEntry* getAIDebugStatusTableEntryFromRow(int row, UserData which)
{
	if(!which)
	{
		if(devassert(row >= 0 && row < eaSize(&aidebug->debugStatusEntries)))
			return aidebug->debugStatusEntries[row];
		else
			return NULL;
	}
	else
	{
		if(devassert(row >= 0 && row < eaSize(&aidebug->debugStatusExternEntries)))
			return aidebug->debugStatusExternEntries[row];
		else
			return NULL;
	}
}

static void DSEName(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AIDebugStatusTableEntry* entry = getAIDebugStatusTableEntryFromRow(row, userData);

	if(entry->legalTarget)
		estrConcatf(output, "*");
	if(entry->name)
		estrConcatf(output, "%s", entry->name);
}

static void DSEEntRef(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AIDebugStatusTableEntry* entry = getAIDebugStatusTableEntryFromRow(row, userData);
	if(entry->entRef)
		estrConcatf(output, "%d", entry->entRef);
}

static void DSEEntId(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AIDebugStatusTableEntry* entry = getAIDebugStatusTableEntryFromRow(row, userData);
	if(entry->name)
		estrConcatf(output, "%d", entry->index);
}

static void DSETotal(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AIDebugStatusTableEntry* entry = getAIDebugStatusTableEntryFromRow(row, userData);
	// only fill this in on lines that have the scaled numbers
	if(entry->totalBaseDangerVal >= 0.f)
		estrConcatf(output, "%.3f", entry->totalBaseDangerVal);
}


static void DSEAggroBucket(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AIDebugStatusTableEntry* entry = getAIDebugStatusTableEntryFromRow(row, 0);
	
	if (entry)
	{
		S32 idx = (intptr_t)userData;
		AIDebugAggroBucket *pBucket = eaGet(&entry->eaAggroBuckets, idx);
		
		if (pBucket)
		{
			estrConcatf(output, "%.3f", pBucket->fValue);
		}
	}
}

static void DPIString(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%s", aidebug->powerBasicInfo[row]->str);
}

static void DPIName(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%s", aidebug->powersInfo[row]->powerName);
}

static void DPIRecharge(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%.2f", aidebug->powersInfo[row]->rechargeTime);
}

static void DPICurRating(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%.2f", aidebug->powersInfo[row]->curRating);
}

static void DPIMinRange(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%.2f", aidebug->powersInfo[row]->aiMinRange);
}

static void DPIMaxRange(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%.2f", aidebug->powersInfo[row]->aiMaxRange);
}

static void DPIWeight(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%.2f", aidebug->powersInfo[row]->absWeight);
}

static void DPILastUsed(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	F32 time = ABS_TIME_TO_SEC(aidebug->powersInfo[row]->lastUsed);
	char secStr[200];
	secStr[0] = 0;

	timeMakeOffsetStringFromSeconds(secStr, time);

	estrConcatf(output, "%s.%02d", secStr, (int)((time - (int)time) * 100));
}

static void DPITimesUsed(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%dx", aidebug->powersInfo[row]->timesUsed);
}

static void DPITags(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrConcatf(output, "%s", aidebug->powersInfo[row]->tags);
}

static void DPIAIExpr(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	const char* aiExpr = aidebug->powersInfo[row]->aiExpr;
	estrConcatf(output, "%s", aiExpr ? aiExpr : "");
}

static void TBIString(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%s", ti->teamBasicInfo[row]->str);
}

static void TName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%s", ti->members[row]->critter_name);
}

static void TRVal(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%d", ti->members[row]->ref);
}

static void TJName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%s", ti->members[row]->job_name);
}

static void TJRole(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%s", ti->members[row]->role_name);
}

static void TMPos(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%.2f %.2f %.2f", vecParamsXYZ(ti->members[row]->pos));
}

static void TMTokens(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%.2f", ti->members[row]->combatTokens);
}

static void TMTRateSelf(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%.2f", ti->members[row]->combatTokenRateSelf);
}

static void TMTRateSocial(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	estrConcatf(output, "%.2f", ti->members[row]->combatTokenRateSocial);
}

static void TMATargetName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	if (!ti->healingAssignments)
		return;

	estrConcatf(output, "%s", ti->healingAssignments[row]->targetName);
}

static void TMAType(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	const char *str;
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;

	if (!ti->healingAssignments)
		return;

	str = StaticDefineIntRevLookup(AIDebugTeamAssignmentTypeEnum, ti->healingAssignments[row]->type);
	if (!str) str = "unknown";
	estrConcatf(output, "%s", str);
}

static void TMAAssigneeName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	const char *str;
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	if (!ti->healingAssignments)
		return;

	str = ti->healingAssignments[row]->assigneeName;
	if (!str) str = "none";
	estrConcatf(output, "%s", str);
}

static void TMAPowerName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	const char *str;
	AIDebugTeamInfo *ti = !userdata ? aidebug->teamInfo : aidebug->combatTeamInfo;
	if (!ti->healingAssignments)
		return;

	str = ti->healingAssignments[row]->powerName;
	if (!str) str = "none";
	estrConcatf(output, "%s", str);
}


static void VName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%s", aidebug->varInfo[row]->name);
}

static void VValue(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "<font color='red'>%s</font>", aidebug->varInfo[row]->value);
}

static void XVName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%s", aidebug->exVarInfo[row]->name);
}

static void XVOrigin(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%s", aidebug->exVarInfo[row]->origin);
}

static void XVValue(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "<font color='red'>%s</font>", aidebug->exVarInfo[row]->value);
}

static void MsgName(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%s", aidebug->msgInfo[row]->name);
}

static void MsgTimeSince(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%f", aidebug->msgInfo[row]->timeSince);
}

static void MsgCount(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%d", aidebug->msgInfo[row]->count);
}

static void MsgSources(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%s", aidebug->msgInfo[row]->sources);
}

static void MsgAttachedEnts(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "<font color='red'>%s</font>", aidebug->msgInfo[row]->attachedEnts);
}

static void ConfigModField(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%s", aidebug->configMods[row]->name);
}

static void ConfigModVal(UIList *list, UIListColumn *column, int row, UserData userdata, char **output)
{
	estrConcatf(output, "%s", aidebug->configMods[row]->val);
}

static void LEPrintTime(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	F32 time = aidebug->logEntries[row]->timeInSec;
	char secStr[200];
	secStr[0] = 0;

	timeMakeOffsetStringFromSeconds(secStr, time);

	estrConcatf(output, "%s.%02d", secStr, (int)((time - (int)time) * 100));
}

static void LEPrintStr(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	AIDebugLogEntryClient* entry = aidebug->logEntries[row];

	estrConcatf(output, "%s", entry->str);
}

/*
AUTO_COMMAND ACMD_CLIENTCMD;
void aiDebugUpdateData(AIDebug* newDebug)
{
	StructDestroySafe(parse_AIDebug, &aidebug);
	aidebug = StructAlloc(parse_AIDebug);
	StructCopyFields(parse_AIDebug, newDebug, aidebug, 0, 0);
	aiDebugView(true);
}
*/

// --------------------------------------------------------------------------------------------------------
static void RefreshTeamInfo(AIDebugTeamInfo* ti, UIPane* pane, UIList* bi, UIList* info, UIList* assignmentsInfo, F32 *y)
{
	int i;
	Vec3 start, end;
	int s;
	// need to add one because even though the column headers are NULL it wants to
	// draw them
	F32 dimY = (eaSize(&ti->teamBasicInfo) + 1) * 15;
	if(!ui_IsVisible(UI_WIDGET(bi)))
		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(bi));

	ui_WidgetSetDimensionsEx(UI_WIDGET(bi),1,dimY, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(bi),0,*y,0,0,UITopLeft);
	*y += dimY;
	s = ui_ListGetSelectedRow(bi);
	ui_ListClearSelected(bi); // hack to make this not crash because the model has been blown away
	ui_ListSetModel(bi,parse_AIDebugBasicInfo,&ti->teamBasicInfo);
	ui_ListSetSelectedRow(bi,s);

	// need to add one because even though the column headers are NULL it wants to
	// draw them
	dimY = (eaSize(&ti->members) + 1) * 15;
	if(!ui_IsVisible(UI_WIDGET(info)))
		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(info));

	ui_WidgetSetDimensionsEx(UI_WIDGET(info),1,dimY, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(info),0,*y,0,0,UITopLeft);
	*y += dimY;
	s = ui_ListGetSelectedRow(info);
	ui_ListClearSelected(info); // hack to make this not crash because the model has been blown away
	ui_ListSetModel(info, parse_AIDebugTeamMember,	&ti->members);
	ui_ListSetSelectedRow(info,s);

	dimY = (eaSize(&ti->healingAssignments) + 1) * 15;
	if(!ui_IsVisible(UI_WIDGET(assignmentsInfo)))
		ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(assignmentsInfo));

	ui_WidgetSetDimensionsEx(UI_WIDGET(assignmentsInfo),1,dimY, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(assignmentsInfo),0,*y,0,0,UITopLeft);
	*y += dimY;
	s = ui_ListGetSelectedRow(assignmentsInfo);
	ui_ListClearSelected(assignmentsInfo); // hack to make this not crash because the model has been blown away
	ui_ListSetModel(assignmentsInfo, parse_AIDebugTeamMemberAssignment, &ti->healingAssignments);
	ui_ListSetSelectedRow(assignmentsInfo,s);

	for(i=eaSize(&ti->members)-1; i>=0; i--)
	{
		int j;
		AIDebugTeamMember *member = ti->members[i];
		for(j=i-1; j>=0; j--)
		{
			AIDebugTeamMember *other = ti->members[j];

			if(member!=other)
			{
				int color1 = member->ref==aidebug->settings.debugEntRef ? 0xFF0000FF : 0xFF00FF00;
				int color2 = 0xFF00FF00;

				copyVec3(member->pos, start);
				copyVec3(other->pos, end);
				vecY(start) += 5;
				vecY(end) += 4;
				wlDrawLine3D_2(start, color1, end, color2);
			}
		}
	}
}

static void RefreshData(void)
{
	F32 bx = 10;
	F32 by = 90;
	F32 x = bx;
	F32 y = by;
	F32 ny = 0;
	int fudgeY = 3; // set back to a 1 when UI scale works better
	Entity* e = entActivePlayerPtr();
	PlayerDebug* debug = e ? entGetPlayerDebug(e, false) : NULL;

	if(e && debug)
		aidebug = debug->aiDebugInfo;
	else
		aidebug = NULL;

	if(!aidebug || (!aidebug->settings.debugEntRef && !aidebug->settings.layerFSMName && !aidebug->settings.pfsmName))
	{
#define REMOVE(x) ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET((x)))
		REMOVE(basicInfo);

		REMOVE(statusEntries);
		REMOVE(statusExternEntries);

		REMOVE(powBasicInfo);
		REMOVE(powersInfo);

		REMOVE(teamBasicInfo);
		REMOVE(teamInfo);
		REMOVE(teamAssignmentsInfo);

		REMOVE(combatTeamBasicInfo);
		REMOVE(combatTeamInfo);
		REMOVE(combatTeamAssignmentsInfo);

		REMOVE(varInfo);
		REMOVE(exVarInfo);
		REMOVE(msgInfo);
		REMOVE(configMods);

		REMOVE(logEntries);
#undef REMOVE
		return;
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_BASIC_INFO)
	{
		int s;
		// need to add one because even though the column headers are NULL it wants to
		// draw them
		F32 dimY = (eaSize(&aidebug->basicInfo) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(basicInfo)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(basicInfo));

		ui_WidgetSetDimensionsEx(UI_WIDGET(basicInfo),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(basicInfo),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(basicInfo);
		ui_ListClearSelected(basicInfo); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(basicInfo,parse_AIDebugBasicInfo,&aidebug->basicInfo);
		ui_ListSetSelectedRow(basicInfo,s);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(basicInfo));
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_STATUS_TABLE)
	{
		int s;
		F32 dimY = (eaSize(&aidebug->debugStatusEntries) + fudgeY) * 15;

		// make sure we have the proper columns to represet the status table data
		gclAIDebug_FixupStatusColumns(aidebug->eaAggroTableHeaders);
		// FilterStatusTableColumns(aidebug->statusTableHideFlags);
		
		// make sure it is visible
		if(!ui_IsVisible(UI_WIDGET(statusEntries)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(statusEntries));

		ui_WidgetSetDimensionsEx(UI_WIDGET(statusEntries),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(statusEntries),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(statusEntries);
		ui_ListClearSelected(statusEntries); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(statusEntries,parse_AIDebugStatusTableEntry,&aidebug->debugStatusEntries);
		ui_ListSetSelectedRow(statusEntries,s);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(statusEntries));
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_STATUS_EXTERN)
	{
		int s;
		F32 dimY = (eaSize(&aidebug->debugStatusExternEntries) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(statusExternEntries)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(statusExternEntries));

		ui_WidgetSetDimensionsEx(UI_WIDGET(statusExternEntries),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(statusExternEntries),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(statusExternEntries);
		ui_ListClearSelected(statusExternEntries); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(statusExternEntries,parse_AIDebugStatusTableEntry,&aidebug->debugStatusExternEntries);
		ui_ListSetSelectedRow(statusExternEntries,s);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(statusExternEntries));
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_POWERS)
	{
		int s;
		// need to add one because even though the column headers are NULL it wants to
		// draw them
		F32 dimY = (eaSize(&aidebug->powerBasicInfo) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(powBasicInfo)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(powBasicInfo));

		ui_WidgetSetDimensionsEx(UI_WIDGET(powBasicInfo),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(powBasicInfo),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(powBasicInfo);
		ui_ListClearSelected(powBasicInfo); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(powBasicInfo,parse_AIDebugBasicInfo,&aidebug->powerBasicInfo);
		ui_ListSetSelectedRow(powBasicInfo,s);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(powBasicInfo));
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_POWERS)
	{
		int s;
		// need to add one because even though the column headers are NULL it wants to
		// draw them
		F32 dimY = (eaSize(&aidebug->powersInfo) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(powersInfo)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(powersInfo));

		ui_WidgetSetDimensionsEx(UI_WIDGET(powersInfo),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(powersInfo),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(powersInfo);
		ui_ListClearSelected(powersInfo); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(powersInfo,parse_AIDebugPowersInfo,&aidebug->powersInfo);
		ui_ListSetSelectedRow(powersInfo,s);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(powersInfo));
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_TEAM && aidebug->teamInfo)
	{
		RefreshTeamInfo(aidebug->teamInfo, s_Pane, teamBasicInfo, teamInfo, teamAssignmentsInfo, &y);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(teamBasicInfo));
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(teamInfo));
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(teamAssignmentsInfo));
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_COMBATTEAM && aidebug->combatTeamInfo)
	{
		RefreshTeamInfo(aidebug->combatTeamInfo, s_Pane, combatTeamBasicInfo, combatTeamInfo, combatTeamAssignmentsInfo, &y);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(combatTeamBasicInfo));
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(combatTeamInfo));
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(combatTeamAssignmentsInfo));
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_VARS)
	{
		int s;
		// need to add one because even though the column headers are NULL it wants to
		// draw them
		F32 dimY = (eaSize(&aidebug->varInfo) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(varInfo)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(varInfo));

		ui_WidgetSetDimensionsEx(UI_WIDGET(varInfo),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(varInfo),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(varInfo);
		ui_ListClearSelected(varInfo); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(varInfo,parse_AIDebugVarEntry,&aidebug->varInfo);
		ui_ListSetSelectedRow(varInfo,s);
	}
	else
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(varInfo));

	if(aidebug->settings.flags & AI_DEBUG_FLAG_EXVARS)
	{
		int s;
		// need to add one because even though the column headers are NULL it wants to
		// draw them
		F32 dimY = (eaSize(&aidebug->exVarInfo) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(exVarInfo)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(exVarInfo));

		ui_WidgetSetDimensionsEx(UI_WIDGET(exVarInfo),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(exVarInfo),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(exVarInfo);
		ui_ListClearSelected(exVarInfo); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(exVarInfo,parse_AIDebugVarEntry,&aidebug->exVarInfo);
		ui_ListSetSelectedRow(exVarInfo,s);
	}
	else
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(exVarInfo));

	if(aidebug->settings.flags & AI_DEBUG_FLAG_MSGS)
	{
		int s;
		// need to add one because even though the column headers are NULL it wants to
		// draw them
		F32 dimY = (eaSize(&aidebug->msgInfo) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(msgInfo)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(msgInfo));

		ui_WidgetSetDimensionsEx(UI_WIDGET(msgInfo),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(msgInfo),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(msgInfo);
		ui_ListClearSelected(msgInfo); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(msgInfo,parse_AIDebugMsgEntry,&aidebug->msgInfo);
		ui_ListSetSelectedRow(msgInfo,s);
	}
	else
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(msgInfo));

	if(aidebug->settings.flags & AI_DEBUG_FLAG_MOVEMENT && aidebug->movementInfo)
	{
		int i;
		for(i = eaSize(&aidebug->movementInfo->curPath)-2; i >= 0; i--)
		{
			AIDebugWaypoint* wp = aidebug->movementInfo->curPath[i];
			AIDebugWaypoint* nextWp = aidebug->movementInfo->curPath[i+1];
			int color = 0;
			switch(nextWp->type)
			{
			xcase AI_DEBUG_WP_GROUND:
				color = 0xFFF0000F;
			xcase AI_DEBUG_WP_JUMP:
				color = 0xFF0000FF;
			xcase AI_DEBUG_WP_SHORTCUT:
				color = 0xFF00FF00;
			xcase AI_DEBUG_WP_OTHER:
				color = 0xFFFFFFFF;
			}

			devassert(!vec3IsZero(wp->pos) && !vec3IsZero(nextWp->pos));
			wlDrawLine3D_2(wp->pos, color, nextWp->pos, color);
		}

		if(!sameVec3(aidebug->movementInfo->splineTarget, zerovec3))
		{
			wlDrawLine3D_2(aidebug->movementInfo->curPos, 0xFFFFFF00, aidebug->movementInfo->splineTarget, 0xFFFFFF00);
		}
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_LOC_RATINGS && eaSize(&aidebug->locRatings))
	{
		int i;
		int n = eaSize(&aidebug->locRatings);
		F32 arc = 2*PI/n;
		F32 maxRating = FLT_MIN;
		F32 minRating = FLT_MAX;
		Vec3 pos;
		Entity *attackTarget = entFromEntityRefAnyPartition(aidebug->attackTargetRef);

		if(attackTarget)
		{
			bool bUseCombatPositionSlots = false;
			entGetPos(attackTarget, pos);
			pos[1] += 1;

			for(i=0; i<n; i++)
			{
				int j;
				F32 total = 0;

				if (aidebug->locRatings[i]->combatPosSlotIndex > 0)
				{
					bUseCombatPositionSlots = true;
				}

				if(aidebug->locRatings[i]->rayCollResult)
					continue;

				FORALL_PARSETABLE(parse_AIDebugLocRating, j)
				{
					if(TOK_GET_TYPE(parse_AIDebugLocRating[j].type) == TOK_F32_X)
						total += TokenStoreGetF32(parse_AIDebugLocRating, j, aidebug->locRatings[i], 0, NULL);
				}

				if(total > maxRating)
					maxRating = total;
				if(total < minRating)
					minRating = total;
			}

			if (bUseCombatPositionSlots)
			{
				for (i = 0; i < n; i++)
				{
					Vec3 vecBottom, vecTop;
					AIDebugLocRating *rating = aidebug->locRatings[i];
					F32 ratio;
					F32 total = 0;
					int color;
					int j;

					FORALL_PARSETABLE(parse_AIDebugLocRating, j)
					{
						if(TOK_GET_TYPE(parse_AIDebugLocRating[j].type) == TOK_F32_X)
							total += TokenStoreGetF32(parse_AIDebugLocRating, j, rating, 0, NULL);
					}
					ratio = (total-minRating)/(maxRating-minRating);

					copyVec3(pos, vecBottom);
					
					vecBottom[0] += rating->vCombatPosSlot[0];
					vecBottom[2] += rating->vCombatPosSlot[2];
					copyVec3(vecBottom, vecTop);
					vecTop[1] += 0.5f;

					if(rating->rayCollResult)
						color = 0xFFFF0000;
					else
						color = 0xFF000000 | (((int)(0xFF*ratio)) << 8) | ((int)(0xFF*(1-ratio)));

					gfxDrawCylinder3D(vecBottom, vecTop, 0.75f * ratio, 16, true, ARGBToColor(color), 1);
				}
			}
			else
			{
				for(i=0; i<n; i++)
				{
					F32 angle = i*2*PI/n;
					Vec3 l, r;
					F32 total = 0;
					int color;
					F32 ratio;
					int j;
					AIDebugLocRating *rating = aidebug->locRatings[i];

					setVec3(l, sinf(angle+arc/2), 0, cosf(angle+arc/2));
					setVec3(r, sinf(angle-arc/2), 0, cosf(angle-arc/2));

					FORALL_PARSETABLE(parse_AIDebugLocRating, j)
					{
						if(TOK_GET_TYPE(parse_AIDebugLocRating[j].type) == TOK_F32_X)
							total += TokenStoreGetF32(parse_AIDebugLocRating, j, rating, 0, NULL);
					}

					ratio = (total-minRating)/(maxRating-minRating);
					scaleAddVec3(l, ratio*100, pos, l);
					scaleAddVec3(r, ratio*100, pos, r);

					if(rating->rayCollResult)
						color = 0xFFFF0000;
					else
						color = 0xFF000000 | (((int)(0xFF*ratio)) << 8) | ((int)(0xFF*(1-ratio)));
					gfxDrawTriangle3D_3ARGB(pos, l, r, color, color, color);
				}
			}
		}
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_FORMATION && aidebug->formation)
	{
		int i;
		Vec3 myPos;

		copyVec3(aidebug->formation->formationPos, myPos);

		myPos[1] += 3;

		for(i=0; i<eaSize(&aidebug->formation->positions); i++)
		{
			AIDebugFormationPosition *dfp = aidebug->formation->positions[i];
			Vec3 dfpPos;
			int color;
			Quat q;
			Vec3 pyFace;

			entGetFacePY(e, pyFace);
			yawQuat(-pyFace[1],q);
			quatRotateVec3(q, dfp->offset, dfpPos);
			
			addVec3(dfpPos, myPos, dfpPos);

			if(dfp->blocked)
				color = 0xFFFF0000;
			else if(dfp->assignee)
				color = 0xFF0000FF;
			else
				color = 0xFF00FF00;

			gfxDrawLine3DARGB(myPos, dfpPos, color);

			if(dfp->assignee)
			{
				Entity *assignee = entFromEntityRefAnyPartition(dfp->assignee);

				if(assignee)
				{
					Vec3 assigneePos;

					entGetPos(assignee, assigneePos);

					gfxDrawLine3DARGB(dfpPos, assigneePos, color);
				}
			}
		}
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_CONFIG_MODS)
	{
		int s;
		// need to add one because even though the column headers are NULL it wants to
		// draw them
		F32 dimY = (eaSize(&aidebug->configMods) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(configMods)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(configMods));

		ui_WidgetSetDimensionsEx(UI_WIDGET(configMods),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(configMods),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(configMods);
		ui_ListClearSelected(configMods); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(configMods,parse_AIDebugStringStringEntry,&aidebug->configMods);
		ui_ListSetSelectedRow(configMods,s);
	}
	else
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(configMods));

	if(aidebug->settings.flags & AI_DEBUG_FLAG_AVOID && aidebug->avoidInfo)
	{
		{
			int i;
			Vec3 pos;
			Entity *debugEnt = entFromEntityRefAnyPartition(aidebug->settings.debugEntRef);

			if(debugEnt)
			{
				Vec3 offset = {0.1,0.1,0.1};
				entGetPos(debugEnt, pos);

				for(i=0; i<eaSize(&aidebug->avoidInfo->bcns); i++)
				{
					Vec3 mn, mx;
					AIDebugAvoidBcn *abcn = aidebug->avoidInfo->bcns[i];
					U32 color = abcn->avoid ? 0xFFFF0000 : 0xFF00FF00;

					subVec3(abcn->pos, offset, mn);
					addVec3(abcn->pos, offset, mx);
					wlDrawBox3D(mn, mx, unitmat, color, 1.0);
				}
			}
		}
		
		{
			FOR_EACH_IN_EARRAY(aidebug->avoidInfo->volumes, AIDebugAvoidVolume, pVolume)
			{
				if (pVolume->fRadius)
				{
					Vec3 vMin, vMax;
					Mat4 mtx;
					identityMat4(mtx);
					copyVec3(pVolume->vPos, mtx[3]);
					setVec3same(vMin, -pVolume->fRadius);
					setVec3same(vMax, pVolume->fRadius);
					gfxDrawSphere3DARGB(pVolume->vPos, pVolume->fRadius, 20, 0xFFFF0000, 1.f);
				}
				else
				{
					wlDrawBox3D( pVolume->vBoxMin, pVolume->vBoxMax, pVolume->mtxBox, 0xFFFF0000, 3.f); 
				}
				
			}
			FOR_EACH_END
		}
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_AGGRO && aidebug->aggroInfo)
	{
		Vec3 pos;
		static StashTable set = NULL;
		static Entity **ents = NULL;
		static StashTable entTable = NULL;
		Entity *target;
		Entity *ent;
		int initial;

		entGetPos(e, pos);

		if(!set)
			set = stashTableCreateAddress(20);

		if(!entTable)
			entTable = stashTableCreateInt(20);

		stashTableClear(entTable);
		FOR_EACH_IN_EARRAY(aidebug->entInfo, AIDebugPerEntity, perEnt)
		{
			Vec3 ePos;
			ent = entFromEntityRefAnyPartition(perEnt->myRef);

			if(!ent)
				continue;

			stashIntAddPointer(entTable, perEnt->myRef, perEnt, true);

			entGetPos(ent, ePos);
			ePos[1] += 5;

			if(!g_AggroDrawDisableNormal)
			{
				gfxDrawCircle3DARGB(ePos, upvec, forwardvec, 20, 0xFF00FFFF, perEnt->aggro);
				gfxDrawCircle3DARGB(ePos, upvec, forwardvec, 20, 0xFFFF00FF, perEnt->aware);
			}

			if(aidebug->aggroInfo->socialEnabled && !g_AggroDrawDisableSoc)
			{
				gfxDrawCircle3DARGB(ePos, upvec, forwardvec, 20, 0xFF0000FF, perEnt->socPrim);
				gfxDrawCircle3DARGB(ePos, upvec, forwardvec, 20, 0xFFFF00FF, perEnt->socSec);
			}

			if(g_AggroDraw3D)
			{
				if(!g_AggroDrawDisableNormal)
				{
					gfxDrawCircle3DARGB(ePos, forwardvec, upvec, 20, 0xFF00FFFF, perEnt->aggro);
					gfxDrawCircle3DARGB(ePos, forwardvec, upvec, 20, 0xFFFF00FF, perEnt->aware);

					gfxDrawCircle3DARGB(ePos, sidevec, upvec, 20, 0xFF00FFFF, perEnt->aggro);
					gfxDrawCircle3DARGB(ePos, sidevec, upvec, 20, 0xFFFF00FF, perEnt->aware);
				}

				if(aidebug->aggroInfo->socialEnabled && !g_AggroDrawDisableSoc)
				{
					gfxDrawCircle3DARGB(ePos, forwardvec, upvec, 20, 0xFF0000FF, perEnt->socPrim);
					gfxDrawCircle3DARGB(ePos, forwardvec, upvec, 20, 0xFFFF00FF, perEnt->socSec);

					gfxDrawCircle3DARGB(ePos, sidevec, upvec, 20, 0xFF0000FF, perEnt->socPrim);
					gfxDrawCircle3DARGB(ePos, sidevec, upvec, 20, 0xFFFF00FF, perEnt->socSec);
				}
			}
		}
		FOR_EACH_END;

		eaClear(&ents);
		stashTableClear(set);
		target = entFromEntityRefAnyPartition(aidebug->settings.debugEntRef);

		initial = true;
		eaPush(&ents, target);
		stashAddressAddInt(set, target, 1, true);

		while(ent = eaPop(&ents))
		{
			Vec3 entPos;
			AIDebugPerEntity *perEnt = NULL;

			stashIntFindPointer(entTable, ent->myRef, &perEnt);

			if(!perEnt)
				continue;

			entGetPos(ent, entPos);
			entPos[1] += 5;

			FOR_EACH_IN_EARRAY(aidebug->entInfo, AIDebugPerEntity, testPerEnt)
			{
				Entity *testEnt = entFromEntityRefAnyPartition(testPerEnt->myRef);
				Vec3 testPos;
				F32 dist;
				int color;

				if(!testEnt)
					continue;

				if(stashAddressFindInt(set, testEnt, NULL))
					continue;

				entGetPos(testEnt, testPos);
				testPos[1] += 5;

				if(initial)
				{
					dist = perEnt->socPrim;
					color = 0xFFFF0000;
				}
				else 
				{
					dist = perEnt->socSec;
					color = 0xFFFFAA00;
				}

				if(distance3Squared(entPos, testPos)>SQR(dist))
					continue;

				eaPush(&ents, testEnt);
				stashAddressAddInt(set, testEnt, 1, true);
				gfxDrawLine3DARGB(entPos, testPos, color);
			}
			FOR_EACH_END;

			initial = 0;
		}
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_HEIGHT_CACHE)
	{
		int dx, dz;
		Vec3 pos[21][21];
		F32 castH[21][21];
		Vec3 basePos;
		Entity *debugEnt = entFromEntityRefAnyPartition(aidebug->settings.debugEntRef);

		entGetPos(debugEnt, basePos);

		basePos[0] = floor(basePos[0])+0.5;
		basePos[2] = floor(basePos[2])+0.5;
		basePos[1] += entGetHeight(debugEnt)-1;

		for(dx = 0; dx<=20; dx++)
		{
			for(dz = 0; dz<=20; dz++)
			{
				copyVec3(basePos, pos[dx][dz]);
				pos[dx][dz][0] += dx-10;
				pos[dx][dz][2] += dz-10;

				castH[dx][dz] = pos[dx][dz][1] + worldGetPointFloorDistance(worldGetActiveColl(PARTITION_CLIENT), pos[dx][dz], 0, 20, NULL)+0.1;

				pos[dx][dz][1] = heightCacheGetHeight(worldGetActiveColl(PARTITION_CLIENT), pos[dx][dz])+0.1;
			}
		}
		for(dx = 0; dx<=20; dx++)
		{
			for(dz = 0; dz<=20; dz++)
			{
				int color;
				int hitHC = 0;
				int hitRC = 0;

				if(dx<20)
				{
					ANALYSIS_ASSUME(dx < 20);
#pragma warning(suppress:6201) // /analyze is ignoring the ANALYSIS_ASSUME above
					hitHC = fabs(pos[dx][dz][1]-pos[dx+1][dz][1])>1.42;
#pragma warning(suppress:6201) // /analyze is ignoring the ANALYSIS_ASSUME above
					hitRC = fabs(castH[dx][dz]-castH[dx+1][dz])>1.42;
					if(!hitHC && hitRC)
						color = 0xffff0000;
					else if(hitHC && hitRC)
						color = 0xff0000ff;
					else if(hitHC && !hitRC)
						color = 0xffffff00;
					else
						color = 0xff00ff00;

#pragma warning(suppress:6201) // /analyze is ignoring the ANALYSIS_ASSUME above
					wlDrawLine3D_2(pos[dx][dz], color, pos[dx+1][dz], color);
				}

				if(dz<20)
				{
					hitHC = fabs(pos[dx][dz][1]-pos[dx][dz+1][1])>1.42;
					hitRC = fabs(castH[dx][dz]-castH[dx][dz+1])>1.42;
					if(!hitHC && hitRC)
						color = 0xffff0000;
					else if(hitHC && hitRC)
						color = 0xff0000ff;
					else if(hitHC && !hitRC)
						color = 0xffffff00;
					else
						color = 0xff00ff00;

					wlDrawLine3D_2(pos[dx][dz], color, pos[dx][dz+1], color);
				}
			}
		}
	}

	if(aidebug->settings.flags & AI_DEBUG_FLAG_LOG)
	{
		int s;
		F32 dimY = (eaSize(&aidebug->logEntries) + fudgeY) * 15;
		if(!ui_IsVisible(UI_WIDGET(logEntries)))
			ui_WidgetAddChild(UI_WIDGET(s_Pane), UI_WIDGET(logEntries));

		ui_WidgetSetDimensionsEx(UI_WIDGET(logEntries),1,dimY, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(logEntries),0,y,0,0,UITopLeft);
		y += dimY;
		s = ui_ListGetSelectedRow(logEntries);
		ui_ListClearSelected(logEntries); // hack to make this not crash because the model has been blown away
		ui_ListSetModel(logEntries,parse_AIDebugLogEntryClient,&aidebug->logEntries);
		ui_ListSetSelectedRow(logEntries,s);
	}
	else
	{
		ui_WidgetRemoveChild(UI_WIDGET(s_Pane), UI_WIDGET(logEntries));
	}
}

static int s_aidUIModify = false;
AUTO_CMD_INT(s_aidUIModify, AIDebugUIModify);

// --------------------------------------------------------------------------------------------------------
// Custom tick function to not take input in the pane
static void PaneTick(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pane);

	RefreshData();

	UI_TICK_EARLY(pane, s_aidUIModify, false);
	UI_TICK_LATE(pane);
}

static int g_SetupAIDebug = 1;

int gclAIDebugGetPrefSet(void)
{
	static int prefSet = -1;
	if (prefSet < 0) {
		// Initialize the pref set
		char buf[260];
		sprintf(buf, "%s/aidebugprefs.pref", fileLocalDataDir());
		prefSet = PrefSetGet(buf);
	}
	return prefSet;
}

void gclAIDebugOncePerFrame(void)
{
	if(GSM_IsStateActive(GCL_GAMEPLAY))
	{
		Entity *e = entActivePlayerPtr();
		AIDebug *debug = SAFE_MEMBER3(e, pPlayer, debugInfo, aiDebugInfo);
		static AIDebugSettings s_settings = {0};
		static S32 hadSettings = false;

		if(debug && (!hadSettings || StructCompare(parse_AIDebugSettings, &s_settings, &debug->settings, 0, 0, 0)))
		{
			PrefStoreStruct(gclAIDebugGetPrefSet(), "AIDebugSettings", parse_AIDebugSettings, &debug->settings);

			StructCopyAll(parse_AIDebugSettings, &debug->settings, &s_settings);
			hadSettings = true;
		}
		else if(!debug && hadSettings)
		{
			ZeroStruct(&s_settings);

			PrefStoreStruct(gclAIDebugGetPrefSet(), "AIDebugSettings", parse_AIDebugSettings, &s_settings);
			hadSettings = false;
		}

		if(g_SetupAIDebug)
		{
			AIDebugSettings settings = {0};
			int i;

			g_SetupAIDebug = 0;

			PrefGetStruct(gclAIDebugGetPrefSet(), "AIDebugSettings", parse_AIDebugSettings, &settings);
				
			if(settings.pfsmName)
			{
				if(settings.updateSelected)
				{
					if(settings.updateSelected==1)
						globCmdParsef("aidebugplayerfsm selected %s", settings.pfsmName);
					else
						globCmdParsef("aidebugplayerfsm selected2 %s", settings.pfsmName);
				}
			}
			else if(settings.layerFSMName)
			{
				globCmdParsef("aidebuglayerfsm %s", settings.layerFSMName);
			}
			else if(settings.updateSelected)
			{
				if(settings.updateSelected==1)
					globCmdParse("aidebugent selected");
				else
					globCmdParse("aidebugent selected2");
			}

			if(settings.flags)
				globCmdParsef("aidebugsetflags %d", settings.flags);

			for(i=0; i<ARRAY_SIZE(settings.logSettings); i++)
			{
				if(settings.logSettings[i])
					globCmdParsef("aiDebugLogEnable %d %d", i, countBitsFast(settings.logSettings[i])-1);
			}
		}
	}
}

void gclAIDebugGameplayLeave(void)
{
	g_SetupAIDebug = true;
}

// --------------------------------------------------------------------------------------------------------
bool visitColumn(UIList *pSelect, UIListColumn *pColumn, S32 iColumnIdx, UserData pData)
{
	AIDebugAggroTableHeader** eaAggroTableHeaders = (AIDebugAggroTableHeader**)pData;

	if (iColumnIdx <= 3)
		return false;

	// check to see if this column is represeted in the headers
	FOR_EACH_IN_EARRAY(eaAggroTableHeaders, AIDebugAggroTableHeader, pHeader)
		if (!stricmp(pHeader->pchName, ui_ListColumnGetTitle(pColumn)))
		{
			return false;
		}
	FOR_EACH_END
	
	// was not found, remove
	ui_ListRemoveColumn(statusEntries, pColumn);
	return false;
}


// --------------------------------------------------------------------------------------------------------
static void gclAIDebug_FixupStatusColumns(AIDebugAggroTableHeader** eaAggroTableHeaders)
{
	S32 i, count;

	// if there are aggro buckets that aren't used, remove them
	ui_ListVisitColumns(statusEntries, visitColumn, eaAggroTableHeaders);

	// make sure we have a column for each of the aggro buckets.
	count = eaSize(&eaAggroTableHeaders);
	for(i = 0; i < count; ++i)
	{
		AIDebugAggroTableHeader *pAggroBucket = eaAggroTableHeaders[i];

		UIListColumn *pCol = ui_ListFindColumnByTitleName(statusEntries, pAggroBucket->pchName);
		if (!pCol)
		{	// create a new column for this
			//S32 *p = ;
			pCol = ui_ListColumnCreate(	UIListTextCallback, 
										pAggroBucket->pchName, 
										(intptr_t)DSEAggroBucket, 
										(UserData)(size_t)i );
			pCol->fWidth = 50;
			ui_ListAppendColumn(statusEntries, pCol);
		}
	}	
}

// --------------------------------------------------------------------------------------------------------
static void InitViewer(void)
{
	UIListColumn *pTempColumn = NULL;
	UIMenuItem *pTempMenuItem = NULL;

	UI_WIDGET(s_Pane)->tickF = PaneTick;
	UI_WIDGET(s_Pane)->uClickThrough = true;
	s_skinPane.background[0].a = 0x00;
	ui_WidgetSkin(UI_WIDGET(s_Pane),&s_skinPane);

	s_ListFont = ui_StyleFontCreate("AIDebug_List", NULL, colorFromRGBA(0xFF0000FF), 0, 0, 0);
	ui_StyleFontRegister(s_ListFont);
	
	// Skins
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Bold", s_skinLabelBold.hNormal);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Mild", s_skinLabelMild.hNormal);

	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Bold", s_skinToggleGroup.hNormal);
	ui_SkinSetButton(&s_skinToggleGroup,colorFromRGBA(0x000000A0));

	SET_HANDLE_FROM_STRING(g_ui_FontDict, "AIDebug_List", s_skinList.hNormal);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "CombatDebug_Normal", s_skinList.hWindowTitleFont);
	s_skinList.background[0] = colorFromRGBA(0xFF00FF00);
	s_skinList.background[1] = colorFromRGBA(0x004040C0);

	basicInfo = ui_ListCreate(parse_AIDebugBasicInfo, &aidebug->basicInfo, 15);
	ui_WidgetSkin(UI_WIDGET(basicInfo),&s_skinList);
	pTempColumn = ui_ListColumnCreate(UIListTextCallback,NULL,(intptr_t)BIString,NULL);
	//pTempColumn->fWidth = 800;
	ui_ListAppendColumn(basicInfo,pTempColumn);

	// Status list
	{
		statusEntries = ui_ListCreate(parse_AIDebugStatusTableEntry, NULL,15);
		statusEntries->iPrefSet = gclAIDebugGetPrefSet();
		statusEntries->widget.name = "Status";
		ui_WidgetSkin(UI_WIDGET(statusEntries),&s_skinList);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Name",(intptr_t)DSEName, NULL);
		pTempColumn->fWidth = 200;
		ui_ListAppendColumn(statusEntries,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"EntRef",(intptr_t)DSEEntRef, NULL);
		pTempColumn->fWidth = 60;
		ui_ListAppendColumn(statusEntries,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Id",(intptr_t)DSEEntId, NULL);
		pTempColumn->fWidth = 30;
		ui_ListAppendColumn(statusEntries,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Total",(intptr_t)DSETotal,NULL);
		pTempColumn->fWidth = 50;
		ui_ListAppendColumn(statusEntries,pTempColumn);

		// todo: fix this up
#if 0
		// Status extern entries
		statusExternEntries = ui_ListCreate(parse_AIDebugStatusTableEntry, NULL,15);
		ui_WidgetSkin(UI_WIDGET(statusExternEntries),&s_skinList);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Name",(intptr_t)DSEName,(void*)1);
		pTempColumn->fWidth = 200;
		ui_ListAppendColumn(statusExternEntries,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"EntRef",(intptr_t)DSEEntRef,(void*)1);
		pTempColumn->fWidth = 60;
		ui_ListAppendColumn(statusExternEntries,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Id",(intptr_t)DSEEntId,(void*)1);
		pTempColumn->fWidth = 30;
		ui_ListAppendColumn(statusExternEntries,pTempColumn);
#endif
	}
	


	// AI_DEBUG_FLAG_POWERS
	{
		powBasicInfo = ui_ListCreate(parse_AIDebugBasicInfo, NULL, 15);
		ui_WidgetSkin(UI_WIDGET(powBasicInfo),&s_skinList);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,NULL,(intptr_t)DPIString,NULL);
		//pTempColumn->fWidth = 800;
		ui_ListAppendColumn(powBasicInfo,pTempColumn);

		powersInfo = ui_ListCreate(parse_AIDebugPowersInfo,NULL,15);
		powersInfo->iPrefSet = gclAIDebugGetPrefSet();
		powersInfo->widget.name = "Powers";
		ui_WidgetSkin(UI_WIDGET(powersInfo),&s_skinList);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Name",(intptr_t)DPIName,NULL);
		pTempColumn->fWidth = 150;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Rchrg",(intptr_t)DPIRecharge,NULL);
		pTempColumn->fWidth = 45;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Ratng",(intptr_t)DPICurRating,NULL);
		pTempColumn->fWidth = 45;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"MinRng",(intptr_t)DPIMinRange,NULL);
		pTempColumn->fWidth = 50;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"MaxRng",(intptr_t)DPIMaxRange,NULL);
		pTempColumn->fWidth = 50;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Wght",(intptr_t)DPIWeight,NULL);
		pTempColumn->fWidth = 45;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"LastUsed",(intptr_t)DPILastUsed,NULL);
		pTempColumn->fWidth = 70;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Used",(intptr_t)DPITimesUsed,NULL);
		pTempColumn->fWidth = 35;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Tags",(intptr_t)DPITags,NULL);
		pTempColumn->fWidth = 50;
		ui_ListAppendColumn(powersInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback,"AIExpressions",(intptr_t)DPIAIExpr,NULL);
		pTempColumn->fWidth = 600;
		ui_ListAppendColumn(powersInfo,pTempColumn);
	}
	

	// AI_DEBUG_FLAG_TEAM
	teamBasicInfo = ui_ListCreate(parse_AIDebugBasicInfo, NULL, 15);
	ui_WidgetSkin(UI_WIDGET(teamBasicInfo),&s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,NULL,(intptr_t)TBIString,NULL);
	//pTempColumn->fWidth = 800;
	ui_ListAppendColumn(teamBasicInfo,pTempColumn);

	teamInfo = ui_ListCreate(parse_AIDebugTeamMember, NULL, 15);
	teamInfo->iPrefSet = gclAIDebugGetPrefSet();
	teamInfo->widget.name = "Team";
	ui_WidgetSkin(UI_WIDGET(teamInfo), &s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Name", (intptr_t)TName, (void*)0);
	pTempColumn->fWidth = 200;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Ref", (intptr_t)TRVal, (void*)0);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Job", (intptr_t)TJName, (void*)0);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Role", (intptr_t)TJRole, (void*)0);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Pos", (intptr_t)TMPos, (void*)0);
	pTempColumn->fWidth = 150;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Tokens", (intptr_t)TMTokens, (void*)0);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "TRate_Self", (intptr_t)TMTRateSelf, (void*)0);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "TRate_Soc", (intptr_t)TMTRateSocial, (void*)0);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(teamInfo,pTempColumn);

	{  											  	
		teamAssignmentsInfo = ui_ListCreate(parse_AIDebugTeamMemberAssignment, NULL, 15);
		teamAssignmentsInfo->iPrefSet = gclAIDebugGetPrefSet();
		teamAssignmentsInfo->widget.name = "Team.Assign";
		ui_WidgetSkin(UI_WIDGET(teamAssignmentsInfo), &s_skinList);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Target", (intptr_t)TMATargetName, (void*)0);
		pTempColumn->fWidth = 200;
		ui_ListAppendColumn(teamAssignmentsInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Type", (intptr_t)TMAType, (void*)0);
		pTempColumn->fWidth = 100;
		ui_ListAppendColumn(teamAssignmentsInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Assignee", (intptr_t)TMAAssigneeName, (void*)0);
		pTempColumn->fWidth = 200;
		ui_ListAppendColumn(teamAssignmentsInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Power", (intptr_t)TMAPowerName, (void*)0);
		pTempColumn->fWidth = 150;
		ui_ListAppendColumn(teamAssignmentsInfo,pTempColumn);
	}

	// AI_DEBUG_FLAG_COMBATTEAM

	combatTeamBasicInfo = ui_ListCreate(parse_AIDebugBasicInfo, NULL, 15);
	ui_WidgetSkin(UI_WIDGET(combatTeamBasicInfo),&s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,NULL,(intptr_t)TBIString,(void*)1);
	//pTempColumn->fWidth = 800;
	ui_ListAppendColumn(combatTeamBasicInfo,pTempColumn);

	combatTeamInfo = ui_ListCreate(parse_AIDebugTeamMember, NULL, 15);
	combatTeamInfo->iPrefSet = gclAIDebugGetPrefSet();
	combatTeamInfo->widget.name = "CTeam";
	ui_WidgetSkin(UI_WIDGET(combatTeamInfo), &s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Name", (intptr_t)TName, (void*)1);
	pTempColumn->fWidth = 200;
	ui_ListAppendColumn(combatTeamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Ref", (intptr_t)TRVal, (void*)1);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(combatTeamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Job", (intptr_t)TJName, (void*)1);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(combatTeamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Pos", (intptr_t)TMPos, (void*)1);
	pTempColumn->fWidth = 150;
	ui_ListAppendColumn(combatTeamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Tokens", (intptr_t)TMTokens, (void*)1);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(combatTeamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Tok_Rate_Self", (intptr_t)TMTRateSelf, (void*)1);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(combatTeamInfo,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Tok_Rate_Social", (intptr_t)TMTRateSocial, (void*)1);
	pTempColumn->fWidth = 50;
	ui_ListAppendColumn(combatTeamInfo,pTempColumn);

	{
		combatTeamAssignmentsInfo = ui_ListCreate(parse_AIDebugTeamMemberAssignment, NULL, 15);
		ui_WidgetSkin(UI_WIDGET(combatTeamAssignmentsInfo), &s_skinList);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Target", (intptr_t)TMATargetName, (void*)1);
		pTempColumn->fWidth = 200;
		ui_ListAppendColumn(combatTeamAssignmentsInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Type", (intptr_t)TMAType, (void*)1);
		pTempColumn->fWidth = 100;
		ui_ListAppendColumn(combatTeamAssignmentsInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Assignee", (intptr_t)TMAAssigneeName, (void*)1);
		pTempColumn->fWidth = 200;
		ui_ListAppendColumn(combatTeamAssignmentsInfo,pTempColumn);

		pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Power", (intptr_t)TMAPowerName, (void*)1);
		pTempColumn->fWidth = 150;
		ui_ListAppendColumn(combatTeamAssignmentsInfo,pTempColumn);
	}

	// AI_DEBUG_FLAG_VARS
	varInfo = ui_ListCreate(parse_AIDebugVarEntry, NULL, 15);
	varInfo->iPrefSet = gclAIDebugGetPrefSet();
	varInfo->widget.name = "Vars";
	ui_WidgetSkin(UI_WIDGET(varInfo), &s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Name", (intptr_t)VName, NULL);
	pTempColumn->fWidth = 200;
	ui_ListAppendColumn(varInfo, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListSMFCallback, "Value", (intptr_t)VValue, NULL);
	pTempColumn->fWidth = 800;
	ui_ListAppendColumn(varInfo, pTempColumn);

	// AI_DEBUG_EXFLAG_VARS
	exVarInfo = ui_ListCreate(parse_AIDebugVarEntry, NULL, 15);
	exVarInfo->iPrefSet = gclAIDebugGetPrefSet();
	exVarInfo->widget.name = "XVars";
	ui_WidgetSkin(UI_WIDGET(exVarInfo), &s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Name", (intptr_t)XVName, NULL);
	pTempColumn->fWidth = 200;
	ui_ListAppendColumn(exVarInfo, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Origin", (intptr_t)XVOrigin, NULL);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(exVarInfo, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListSMFCallback, "Value", (intptr_t)XVValue, NULL);
	pTempColumn->fWidth = 700;
	ui_ListAppendColumn(exVarInfo, pTempColumn);

	// AI_DEBUG_MSG_VARS
	msgInfo = ui_ListCreate(parse_AIDebugMsgEntry, NULL, 15);
	msgInfo->iPrefSet = gclAIDebugGetPrefSet();
	msgInfo->widget.name = "Powers";
	ui_WidgetSkin(UI_WIDGET(msgInfo), &s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Name", (intptr_t)MsgName, NULL);
	pTempColumn->fWidth = 200;
	ui_ListAppendColumn(msgInfo, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "TimeSince", (intptr_t)MsgTimeSince, NULL);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(msgInfo, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Count", (intptr_t)MsgCount, NULL);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(msgInfo, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Sources", (intptr_t)MsgSources, NULL);
	pTempColumn->fWidth = 250;
	ui_ListAppendColumn(msgInfo, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListSMFCallback, "AttachedEnts", (intptr_t)MsgAttachedEnts, NULL);
	pTempColumn->fWidth = 500;
	ui_ListAppendColumn(msgInfo, pTempColumn);

	// AI_DEBUG_MSG_CONFIG_MODS
	configMods = ui_ListCreate(parse_AIDebugStringStringEntry, NULL, 15);
	configMods->iPrefSet = gclAIDebugGetPrefSet();
	configMods->widget.name = "ConfigMods";
	ui_WidgetSkin(UI_WIDGET(configMods), &s_skinList);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Field", (intptr_t)ConfigModField, NULL);
	pTempColumn->fWidth = 200;
	ui_ListAppendColumn(configMods, pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)ConfigModVal, NULL);
	pTempColumn->fWidth = 100;
	ui_ListAppendColumn(configMods, pTempColumn);

	// AI_DEBUG_FLAG_LOG
	logEntries = ui_ListCreate(parse_AIDebugLogEntryClient,NULL,15);
	logEntries->iPrefSet = gclAIDebugGetPrefSet();
	logEntries->widget.name = "Log";
	ui_WidgetSkin(UI_WIDGET(logEntries),&s_skinList);
	
	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"Time",(intptr_t)LEPrintTime,NULL);
	pTempColumn->fWidth = 70;
	ui_ListAppendColumn(logEntries,pTempColumn);

	pTempColumn = ui_ListColumnCreate(UIListTextCallback,"LogEntry",(intptr_t)LEPrintStr,NULL);
	pTempColumn->fWidth = 1000;
	ui_ListAppendColumn(logEntries,pTempColumn);
}

void aiDebugViewInternal(int bShow)
{
	if(s_Pane == NULL)
	{
		UISkin *skin = ui_SkinCreate(NULL);
		
		skin->background[0] = colorFromRGBA(0xff00ffff);
		skin->background[1] = colorFromRGBA(0xff00ffff);

		s_Pane = ui_PaneCreate(0,0,1,1,UIUnitPercentage,UIUnitPercentage,0);
		ui_WidgetSkin(UI_WIDGET(s_Pane), skin);
		InitViewer();
	}

	if(bShow)
	{
		if(!g_aiDebugShowing)
		{
			if(s_uiHideUntil <= timeSecondsSince2000())
			{
				g_aiDebugShowing = true;
				ui_WidgetAddToDevice(UI_WIDGET(s_Pane),NULL);
			}
		}
		RefreshData();
	}
	else if(!bShow && g_aiDebugShowing)
	{
		g_aiDebugShowing = false;
		ui_WidgetRemoveFromGroup(UI_WIDGET(s_Pane));
	}
}

typedef struct AnimOption
{
	UICheckButton *enable;
	UIComboBox *anim;
	UISlider *time;
} AnimOption;

struct  
{
	UIWindow *wnd;

	AnimOption anims[AID_MAX_ANIMS];
	UIButton *attach;
	UIButton *untarget;

	const char **animLists;
} g_alTestUI;

void aiAnimListDebugSetOnTarget(UIAnyWidget *widget, UserData data)
{
	Entity *player = entActivePlayerPtr();

	if(player)
	{
		EntityRef ref = 0;
		entGetClientTarget(player, "selected", &ref);

		if(ref)
		{
			int i;

			for(i=0; i<ARRAY_SIZE(g_alTestUI.anims); i++)
			{
				int time = ui_IntSliderGetValue(g_alTestUI.anims[i].time);
				int selected = ui_ComboBoxGetSelected(g_alTestUI.anims[i].anim);
				int enabled = ui_CheckButtonGetState(g_alTestUI.anims[i].enable);
				const char *animList;
				
				if(selected<0 || selected>eaSize(&g_alTestUI.animLists)) selected = 0;
				animList = (const char*)eaGet(&g_alTestUI.animLists, selected);

				globCmdParsef("aiAnimListDebugSetAnim %d %d %d %s %d", ref, i, enabled, animList, time);
			}
			
			globCmdParsef("ec %d SetFSM AnimListTest", ref);
		}
	}
}

static void aiAnimListDebugMakeUntargetable(UIButton* button, UserData unused)
{
	Entity *player = entActivePlayerPtr();

	if(player)
	{
		if(entCheckFlag(player, ENTITYFLAG_UNTARGETABLE))
		{
			globCmdParse("ec me untargetable 0");
			ui_ButtonSetText(button, "Make me untargetable");
		}
		else
		{
			globCmdParse("ec me untargetable 1");
			ui_ButtonSetText(button, "Make me targetable");
		}
	}
}

void aiAnimListDebugSetList(StringListStruct *list)
{
	int i;
	eaClear(&g_alTestUI.animLists);
	for(i=0; i<eaSize(&list->list); i++)
		eaPush(&g_alTestUI.animLists, allocAddString(list->list[i]));
}

static bool aiAnimListDebugCleanupUI(UIAnyWidget *widget, UserData data)
{
	eaDestroy(&g_alTestUI.animLists);
	ZeroStruct(&g_alTestUI);

	return true;
}

static void aiAnimListDebugSetEnabledAnim(UIComboBox *cbox, UserData unused)
{
	int i;

	for(i=0; i<ARRAY_SIZE(g_alTestUI.anims); i++)
	{
		if(g_alTestUI.anims[i].anim==cbox)
		{
			ui_CheckButtonSetState(g_alTestUI.anims[i].enable, 1);
			break;
		}
	}
}

static void aiAnimListDebugSetEnabledTime(UISlider *time, bool bFinished, UserData unused)
{
	int i;

	for(i=0; i<ARRAY_SIZE(g_alTestUI.anims); i++)
	{
		if(g_alTestUI.anims[i].time==time)
		{
			ui_CheckButtonSetState(g_alTestUI.anims[i].enable, 1);
			break;
		}
	}
}

AUTO_COMMAND;
void aiAnimListDebug(Entity *e)
{
	if(g_alTestUI.wnd)
	{
		ui_WindowClose(g_alTestUI.wnd);
	}
	else
	{
		int i;
		F32 curY;
		F32 wndX = GamePrefGetFloat("ALDbg.X", 0);
		F32 wndY = GamePrefGetFloat("ALDbg.Y", 0);
		F32 wndW = GamePrefGetFloat("ALDbg.W", 400);
		F32 wndH = GamePrefGetFloat("ALDbg.H", 300);

		g_alTestUI.wnd = ui_WindowCreate("AnimListDebug", wndX, wndY, wndW, wndH);
		ui_WindowSetCloseCallback(g_alTestUI.wnd, aiAnimListDebugCleanupUI, NULL);

		curY = 0;
		for(i=0; i<ARRAY_SIZE(g_alTestUI.anims); i++)
		{
			g_alTestUI.anims[i].enable = ui_CheckButtonCreate(0, curY, "", 0);
			g_alTestUI.anims[i].anim = ui_FilteredComboBoxCreate(20, curY, 200, NULL, &g_alTestUI.animLists, NULL);
			g_alTestUI.anims[i].time = ui_IntSliderCreate(220+UI_HSTEP, curY, 200, 0, 20, 5);

			ui_ComboBoxSetSelectedCallback(g_alTestUI.anims[i].anim, aiAnimListDebugSetEnabledAnim, NULL);
			ui_SliderSetChangedCallback(g_alTestUI.anims[i].time, aiAnimListDebugSetEnabledTime, NULL);

			curY += 25;

			ui_WindowAddChild(g_alTestUI.wnd, g_alTestUI.anims[i].enable);
			ui_WindowAddChild(g_alTestUI.wnd, g_alTestUI.anims[i].anim);
			ui_WindowAddChild(g_alTestUI.wnd, g_alTestUI.anims[i].time);
		}
		
		g_alTestUI.attach = ui_ButtonCreate("Set AnimLists on Selected", 0, curY, aiAnimListDebugSetOnTarget, NULL);
		ui_WindowAddChild(g_alTestUI.wnd, g_alTestUI.attach);

		g_alTestUI.untarget = ui_ButtonCreate("Make me untargetable", 200, curY, aiAnimListDebugMakeUntargetable, NULL);
		ui_WindowAddChild(g_alTestUI.wnd, g_alTestUI.untarget);

		globCmdParse("aiDebugSendAnimLists");

		ui_WindowShow(g_alTestUI.wnd);
	}
}
