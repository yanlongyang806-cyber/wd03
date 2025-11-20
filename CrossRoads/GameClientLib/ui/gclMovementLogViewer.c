/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UIWindow.h"
#include "UITextEntry.h"
#include "UIButton.h"
#include "UITextArea.h"
#include "UILabel.h"
#include "UISkin.h"
#include "EntityMovementManager.h"
#include "autogen/EntityMovementManager_h_ast.h"
#include "StringCache.h"
#include "GfxPrimitive.h"
#include "GfxDebug.h"
#include "cmdParse.h"
#include "SimpleParser.h"
#include "GraphicsLib.h"
#include "qsortG.h"
#include "EntityIterator.h"
#include "wlCostume.h"
#include "ProjectileEntity.h"
#include "gclEntity.h"
#include "Character.h"
#include "PowerActivation.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "BlockEarray.h"
#include "mapstate_common.h"
#include "SimpleCpuUsage.h"
#include "Player.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

static S32 mmOffsetGraphEnabled;
AUTO_CMD_INT(mmOffsetGraphEnabled, mmOffsetGraph);

static S32 mmDrawServerPosEnabled;
AUTO_CMD_INT(mmDrawServerPosEnabled, mmDrawServerPos);

static S32 mmDrawCapsEnabled;
AUTO_CMD_INT(mmDrawCapsEnabled, mmDrawCaps);

static S32 mmDrawCylsEnabled;
AUTO_CMD_INT(mmDrawCylsEnabled, mmDrawCyls);

static S32 mmDrawCombatEnabled;
AUTO_CMD_INT(mmDrawCombatEnabled, mmDrawCombat);

static S32 mmDrawCoverEnabled;
AUTO_CMD_INT(mmDrawCoverEnabled, mmDrawCover);

static S32 mmDrawRotationsEnabled;
AUTO_CMD_INT(mmDrawRotationsEnabled, mmDrawRotations);

static S32 mmDrawBodiesEnabled;
AUTO_CMD_INT(mmDrawBodiesEnabled, mmDrawBodies);

static S32 mmDrawBodyBoundsEnabled;
AUTO_CMD_INT(mmDrawBodyBoundsEnabled, mmDrawBodyBounds);

static S32 mmDrawResourceDebugEnabled;
AUTO_CMD_INT(mmDrawResourceDebugEnabled, mmDrawResourceDebug);

static S32 mmDrawNetOffsetFromEndEnabled;
AUTO_CMD_INT(mmDrawNetOffsetFromEndEnabled, mmDrawNetOffsetFromEnd);

static S32 mmDrawNetOutputsEnabled;
AUTO_CMD_INT(mmDrawNetOutputsEnabled, mmDrawNetOutputs);

static S32 mmDrawOutputsEnabled;
AUTO_CMD_INT(mmDrawOutputsEnabled, mmDrawOutputs);

static S32 mmDrawCameraEnabled;
AUTO_CMD_INT(mmDrawCameraEnabled, mmDrawCamera);

static S32 netTimingGraphEnabled;
static S32 netTimingGraphAlpha = 0xc0;
AUTO_CMD_INT(netTimingGraphAlpha, netTimingGraphAlpha) ACMD_ACCESSLEVEL(0);

static S32 mmDrawKeyStatesEnabled;
AUTO_CMD_INT(mmDrawKeyStatesEnabled, mmDrawKeyStates);

static S32 mmDrawProjectilesEnabled;
AUTO_CMD_INT(mmDrawProjectilesEnabled, mmDrawProjectiles);


static const char* segListTag = "SegList";
static const char* pointListTag	= "PointList";
static const char* cameraMatTag	= "CameraMat";

extern ParseTable parse_MovementLog[];
#define TYPE_parse_MovementLog MovementLog

typedef struct LogSection {
	S32						lineBegin;
	S32						lineEnd;
	
	U32						processCount;
	U32						localProcessCount;
	
	U32						frameCount;
} LogSection;

typedef struct LogLineTagPaneData {
	UIButton*				button;
	
	struct {
		U32					hasText				: 1;
		U32					has3D				: 1;
		U32					isGroup				: 1;
		U32					existsInCurrentView	: 1;
		U32					exists				: 1;
		U32					disabled			: 1;
	} flags;
} LogLineTagPaneData;

typedef struct LogLineTag {
	char*					name;
	UIButton*				button;
	UILabel*				label;

	LogLineTagPaneData		pane[2];
	
	struct {
		U32					groupOpen	: 1;
	} flags;
} LogLineTag;

typedef enum CompareUISectionType {
	CUIST_MOVEMENT_STEP,
	CUIST_FRAME,
	
	CUIST_COUNT,
} CompareUISectionType;

typedef struct ActiveServerLog {
	EntityRef				er;
	UIButton*				button;
	
	struct {
		U32					exists : 1;
	} flags;
} ActiveServerLog;

typedef struct ActiveClientLog {
	MovementManager*		mm;
	UIButton*				button;

	struct {
		U32					exists : 1;
	} flags;
} ActiveClientLog;

struct {
	UIWindow*				window;
	UISkin*					windowSkin;
	
	UIButton*				toggleOpacity;
	S32						opacityLevel;
	
	UITextEntry*			textEntry[2];
	UIButton*				textEntryButton[2][2];

	UIButton*				sortType;
	
	UITextArea*				textArea[2];
	UIButton*				button[2][6];
	UIButton*				commonButton[2];
	
	UIButton*				wordWrapButton[2];
	S32						wordWrapEnabled[2];
	
	UIButton*				threadFilterButton[2];
	S32						threadFilter[2];
	
	UIButton*				toggle3DButton[2];
	S32						hide3D[2];

	UIButton*				cameraButton[2];
	S32						activeCamera;
	S32						printCameraPos[2];
	
	UIButton*				tagsButton[2];
	S32						tagsVisible[2];

	UIButton*				hideButton[2];
	S32						hideDisabled[2];

	MovementLog*			log[2];
	LogSection**			sections[2];
	LogLineTag**			tags;
	UISkin*					skinButtonRed;
	UISkin*					skinButtonGreen;
	UISkin*					skinButtonYellow;

	U32						sectionIndex[2];
	CompareUISectionType	sectionType;
} compareUI;

typedef struct DebugSegment {
	Vec3					a;
	Vec3					b;
	U32						argb;
} DebugSegment;

struct {
	DebugSegment**			segs;
} debugSegments;

typedef struct AvailableLog {
	EntityRef		er;
	char*			name;
	char*			entName;
	UIButton*		button[4];
	UILabel*		label;
	
	struct {
		U32			exists		: 1;
		U32			hasLocal	: 1;
		U32			hasRemote	: 1;
	} flags;
} AvailableLog;

struct {
	UIWindow*		window;
	UIButton*		targetButton;
	UIButton*		recordButton[3];
	U32				targetType;
	
	AvailableLog**	availableLogs;
	
	ActiveServerLog**	activeServerLogs;
	ActiveClientLog**	activeClientLogs;
} mmDebugOptionUI;

static char* recordButtonText[] = {
	"Start Server",
	"Start Both",
	"Start Client",
	"Stop Server",
	"Stop Both",
	"Stop Client"
};

static void mmCompareRefreshTagButtons(void);
static void mmCmdCompareLogsSetEntInternal( S32 useLocalLog,
											S32 useRightPane,
											const char* module,
											EntityRef erTarget);

AUTO_COMMAND ACMD_NAME("mmAddDebugSegment");
void mmCmdAddDebugSegment(	const Vec3 a,
							const Vec3 b,
							U32 argb)
{
	DebugSegment* seg = callocStruct(DebugSegment);
	
	copyVec3(a, seg->a);
	copyVec3(b, seg->b);
	seg->argb = argb;

	eaPush(&debugSegments.segs, seg);
}

AUTO_COMMAND ACMD_NAME("mmAddDebugSegmentOffset");
void mmCmdAddDebugSegmentOffset(const Vec3 a,
								const Vec3 offset,
								U32 argb)
{
	DebugSegment* seg = callocStruct(DebugSegment);
	
	copyVec3(a, seg->a);
	addVec3(a, offset, seg->b);
	seg->argb = argb;
	
	eaPush(&debugSegments.segs, seg);
}

AUTO_COMMAND ACMD_NAME("mmClearDebugSegments");
void mmCmdClearDebugSegments(void){
	eaDestroyEx(&debugSegments.segs, NULL);
}

AUTO_CMD_INT(netTimingGraphEnabled, netTimingGraph) ACMD_CALLBACK(mmCmdNetTimingGraphChanged) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC;
void mmCmdNetTimingGraphChanged(void){
	mmClientStatsSetFramesEnabled(NULL, netTimingGraphEnabled);
	mmClientStatsSetPacketTimingEnabled(NULL, netTimingGraphEnabled);
}

static S32 compareTagName(	const LogLineTag** tag1,
							const LogLineTag** tag2)
{
	return stricmp(tag1[0]->name, tag2[0]->name);
}

static S32 mmCompareGetLogLineTag(	LogLineTag** tagOut,
									const char* tagName,
									S32 create)
{
	LogLineTag* tag;
	
	EARRAY_CONST_FOREACH_BEGIN(compareUI.tags, i, size);
	{
		tag = compareUI.tags[i];
		
		if(!stricmp(tag->name, tagName)){
			*tagOut = tag;
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	if(!create){
		return 0;
	}
	
	tag = callocStruct(LogLineTag);
	
	tag->name = strdup(tagName);
	
	eaPush(&compareUI.tags, tag);
	
	*tagOut = tag;

	qsortG(	compareUI.tags,
			eaSize(&compareUI.tags),
			sizeof(compareUI.tags[0]),
			compareTagName);

	return 1;
}

static void mmCompareTagsClearExists(S32 useRightPane){
	EARRAY_CONST_FOREACH_BEGIN(compareUI.tags, i, size);
	{
		LogLineTag*			tag = compareUI.tags[i];
		LogLineTagPaneData* pane = tag->pane + useRightPane;
		
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory 'tag->flags'"
		ui_WidgetDestroy(&tag->button);
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory '*tag[8]'"
		ui_WidgetDestroy(&tag->label);
		
		pane->flags.isGroup = 0;
		pane->flags.exists = 0;
		
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory 'pane->button'"
		ui_WidgetDestroy(&pane->button);
	}
	EARRAY_FOREACH_END;
}

static const char* mmCompareUISectionTypeName(CompareUISectionType sectionType){
	switch(sectionType){
		xcase CUIST_MOVEMENT_STEP:
			return "Viewing by: Movement Step";
		xcase CUIST_FRAME:
			return "Viewing by: Frame";
		xdefault:
			return "Unknown";
	}	
}

typedef struct LogLineParsed {
	U32				frameCount;
	const char**	tags;
	char*			lineText;
	
	struct {
		U32		isBG		: 1;
	} flags;
} LogLineParsed;

static void mmLogLineParsedClear(LogLineParsed* parsed){
	estrDestroy(&parsed->lineText);
	
	eaDestroy(&parsed->tags);
	
	ZeroStruct(parsed);
}

static S32 parseFrameCount(	const char* t,
							U32* frameCountOut)
{
	const char*	firstDigit = NULL;
	char		buffer[5];
	
	FOR_BEGIN(i, 4);
		if(*t == ' '){
			if(firstDigit){
				return 0;
			}
		}
		else if(!isdigit(*t)){
			return 0;
		}
		else if(!firstDigit){
			firstDigit = t;
		}

		t++;
	FOR_END;
	
	strncpy(buffer, firstDigit, 4);
	*frameCountOut = atoi(buffer);
	return 1;
}

static void mmCompareParseLogLine(	const char* logLine,
									LogLineParsed* parsedOut)
{
	char buffer[5000];

	if(!parsedOut){
		return;
	}
	
	ZeroStruct(parsedOut);
	
	// Parse the frame count.
	
	if(parseFrameCount(logLine, &parsedOut->frameCount)){
		logLine += 4;
	
		// Parse the fg/bg flag.
		
		if(	strStartsWith(logLine, " bg:") ||
			strStartsWith(logLine, " fg:"))
		{
			strncpy(buffer, logLine, 4);
			parsedOut->flags.isBG = !stricmp(buffer, " bg:");
			logLine += 4;
		
			// Parse the tags.
			
			if(	strStartsWith(logLine, " [") &&
				logLine[2])
			{
				const char* endTags = strstriConst(logLine + 2, "]");
				
				if(endTags){
					logLine += 2;
					
					while(logLine < endTags){
						const char* comma = strstri(logLine, ",");
						char		tag[200];

						tag[0] = 0;
						
						if(	comma &&
							comma < endTags)
						{
							strncpy(tag, logLine, comma - logLine);
							logLine = comma + 1;
						}
						else if(logLine < endTags){
							strncpy(tag, logLine, endTags - logLine);
							logLine = endTags + 1;
						}
						else{
							break;
						}
						
						removeTrailingWhiteSpaces(tag);

						if(tag[0]){
							const char* trimmed = removeLeadingWhiteSpaces(tag);
							
							if(trimmed[0]){
								eaPush(&parsedOut->tags, allocAddCaseSensitiveString(trimmed));
							}
						}
					}
					
					logLine = endTags + 1;
				}
			}
		}
	}
	
	if(!eaSize(&parsedOut->tags)){
		eaPush(&parsedOut->tags, allocAddString("default"));
	}
	
	// The rest is the line text.
	
	estrCopy2(&parsedOut->lineText, removeLeadingWhiteSpaces(logLine));
	estrReplaceOccurrences(&parsedOut->lineText, "\t", "    ");
}

static S32 mmParsedHasEnabledTag(	const LogLineParsed* parsed,
									S32 useRightPane)
{
	if(eaSize(&parsed->tags)){
		S32 hasEnabledTag = 0;
		
		EARRAY_CONST_FOREACH_BEGIN(parsed->tags, i, isize);
		{
			LogLineTag* tag;
			
			if(mmCompareGetLogLineTag(&tag, parsed->tags[i], 0)){
				if(!tag->pane[useRightPane].flags.disabled){
					return 1;
				}
			}
		}
		EARRAY_FOREACH_END;

		return 0;
	}
	
	return 1;
}

static S32 mmParsedThreadIsEnabled(	S32 isBG,
									S32 useRightPane)
{
	return !!((1 << isBG) & (compareUI.threadFilter[useRightPane] + 1));
}

static U32 hexCharToValue(char c){
	if(c >= '0' && c <= '9'){
		return c - '0';
	}
	
	c = tolower(c);
	
	if(c >= 'a' && c <= 'f'){
		return c - 'a' + 10;
	}
	
	return 0;
}

static S32 extractColor(const char** textInOut,
						U32* argbOut)
{
	const char* cur = *textInOut;
	S32			isGoodColor = 1;
	
	switch(*cur++){
		xcase ':':{
			// No color.
		}
		
		xcase '.':{
			if(*cur == '#'){
				Color c;
				
				cur++;
				
				FOR_BEGIN(j, 8);
					if(	!isdigit(cur[j]) &&
						tolower(cur[j]) < 'a' &&
						tolower(cur[j]) > 'f')
					{
						isGoodColor = 0;
						break;
					}
				FOR_END;
				
				if(isGoodColor){
					if(cur[8] != ':'){
						isGoodColor = 0;
					}else{
						c.a = (hexCharToValue(cur[0]) << 4) + hexCharToValue(cur[1]);
						c.r = (hexCharToValue(cur[2]) << 4) + hexCharToValue(cur[3]);
						c.g = (hexCharToValue(cur[4]) << 4) + hexCharToValue(cur[5]);
						c.b = (hexCharToValue(cur[6]) << 4) + hexCharToValue(cur[7]);
						
						if(c.a || c.r || c.g || c.b){
							*argbOut = ARGBFromColor(c);
						}
						
						cur += 9;
					}
				}
			}
		}
	}
	
	*textInOut = cur;
	
	return isGoodColor;
}

static void mmCompareCreateSkins(void){
	if(!compareUI.skinButtonGreen){
		compareUI.skinButtonGreen = ui_SkinCreate(NULL);
		
		ui_SkinSetButton(compareUI.skinButtonGreen, CreateColor(128, 255, 128, 255));
	}
	
	if(!compareUI.skinButtonYellow){
		compareUI.skinButtonYellow = ui_SkinCreate(NULL);
		
		ui_SkinSetButton(compareUI.skinButtonYellow, CreateColor(255, 255, 128, 255));
	}

	if(!compareUI.skinButtonRed){
		compareUI.skinButtonRed = ui_SkinCreate(NULL);
		
		ui_SkinSetButton(compareUI.skinButtonRed, CreateColor(255, 128, 128, 255));
	}

}

static void mmActiveLogButtonPressed(	UIAnyWidget* widget,
										UserData userData)
{
	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeServerLogs, i, isize);
	{
		ActiveServerLog* l = mmDebugOptionUI.activeServerLogs[i];
		
		if(l->button == widget){
			globCmdParsef("ec %d mmDebug 0", l->er);
			break;
		}
	}
	EARRAY_FOREACH_END;		

	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeClientLogs, i, isize);
	{
		ActiveClientLog* l = mmDebugOptionUI.activeClientLogs[i];

		if(l->button == widget){
			mmSetDebugging(l->mm, 0);
			break;
		}
	}
	EARRAY_FOREACH_END;
}

static UIButton* newButton(	SA_PARAM_OP_STR const char *text,
							F32 x,
							F32 y,
							UIActivationFunc clickedF,
							UserData clickedData)
{
	UIButton* b = ui_ButtonCreate(text, x, y, clickedF, clickedData);
	
	b->bNoAutoTruncateText = 1;
	
	return b;
}

static void mmLogUpdateActiveLogLists(void){
	static U32* serverLogList;
	static MovementManager** localLogList;

	char name[100];
	
	mmCopyServerLogList(&serverLogList);
	mmCopyLocalLogList(&localLogList);
	
	mmCompareCreateSkins();
	
	// Update the active server log list.
	
	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeServerLogs, i, isize);
	{
		mmDebugOptionUI.activeServerLogs[i]->flags.exists = 0;
	}
	EARRAY_FOREACH_END;

	EARRAY_INT_CONST_FOREACH_BEGIN(serverLogList, i, isize);
	{
		S32 exists = 0;

		EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeServerLogs, j, jsize);
		{
			if(mmDebugOptionUI.activeServerLogs[j]->er == serverLogList[i]){
				mmDebugOptionUI.activeServerLogs[j]->flags.exists = 1;
				exists = 1;
				break;
			}
		}
		EARRAY_FOREACH_END;
		
		if(!exists){
			ActiveServerLog* l = callocStruct(ActiveServerLog);
			
			eaPush(&mmDebugOptionUI.activeServerLogs, l);
			
			l->er = serverLogList[i];
			l->flags.exists = 1;
			sprintf(name, "0x%x", l->er);
			l->button = newButton("", 0, 0, mmActiveLogButtonPressed, NULL);
			ui_ButtonSetTextAndResize(l->button, name);
			ui_WindowAddChild(mmDebugOptionUI.window, l->button);
			ui_WidgetSkin((UIWidget*)l->button, compareUI.skinButtonRed);
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeServerLogs, i, isize);
	{
		ActiveServerLog* l = mmDebugOptionUI.activeServerLogs[i];
		
		if(!l->flags.exists){
			ui_WidgetDestroy(&l->button);
			eaRemove(&mmDebugOptionUI.activeServerLogs, i);
			i--;
			isize--;
		}else{
			ui_WidgetSetPositionEx(	(UIWidget*)l->button,
									0,
									200 + (i + eaSize(&mmDebugOptionUI.availableLogs)) * 25,
									0,
									0,
									UITopLeft);
		}
	}
	EARRAY_FOREACH_END;

	// Update the active client log list.

	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeClientLogs, i, isize);
	{
		mmDebugOptionUI.activeClientLogs[i]->flags.exists = 0;
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(localLogList, i, isize);
	{
		S32 exists = 0;
		
		EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeClientLogs, j, jsize);
		{
			if(mmDebugOptionUI.activeClientLogs[j]->mm == localLogList[i]){
				mmDebugOptionUI.activeClientLogs[j]->flags.exists = 1;
				exists = 1;
				break;
			}
		}
		EARRAY_FOREACH_END;
		
		if(!exists){
			ActiveClientLog*	l = callocStruct(ActiveClientLog);
			Entity*				e = NULL;
			
			eaPush(&mmDebugOptionUI.activeClientLogs, l);
			
			l->mm = localLogList[i];
			l->flags.exists = 1;
			mmGetUserPointer(l->mm, &e);
			sprintf(name, "0x%x", e ? entGetRef(e) : 0);
			l->button = newButton("", 0, 0, mmActiveLogButtonPressed, NULL);
			ui_ButtonSetTextAndResize(l->button, name);
			ui_WindowAddChild(mmDebugOptionUI.window, l->button);
			ui_WidgetSkin((UIWidget*)l->button, compareUI.skinButtonRed);
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.activeClientLogs, i, isize);
	{
		ActiveClientLog* l = mmDebugOptionUI.activeClientLogs[i];
		
		if(!l->flags.exists){
			ui_WidgetDestroy(&l->button);
			eaRemove(&mmDebugOptionUI.activeClientLogs, i);
			i--;
			isize--;
		}else{
			ui_WidgetSetPositionEx(	(UIWidget*)l->button,
									0,
									200 + (i + eaSize(&mmDebugOptionUI.availableLogs)) * 25,
									0,
									0,
									UITopRight);
		}
	}
	EARRAY_FOREACH_END;
}

static struct {
	Mat4	matCamera;
} mmDrawState;

static void moveCloserToCamera(	const Vec3 p,
								Vec3 pOut)
{
	F32 len;
	
	subVec3(mmDrawState.matCamera[3], p, pOut);
	len = normalVec3(pOut);
	
	if(len > 1.f){
		len = 0.1f;
		scaleVec3(pOut, len, pOut);
		addVec3(pOut, p, pOut);
	}else{
		copyVec3(p, pOut);
	}
}

static void gclMovementDrawLine3D(	const Vec3 p0,
									U32 argb0,
									const Vec3 p1,
									U32 argb1)
{
	Vec3 p0Fixed;
	Vec3 p1Fixed;
	Color c0;
	Color c1;
	
	moveCloserToCamera(p0, p0Fixed);
	moveCloserToCamera(p1, p1Fixed);
	
	setColorFromARGB(&c0, argb0);
	setColorFromARGB(&c1, argb1);
	
	gfxDrawLine3D_2(p0Fixed, p1Fixed, c0, c1);
}

static void gclMovementDrawCapsule3D(	const Vec3 p,
										const Vec3 dir,
										F32 length,
										F32 radius,
										U32 argb)
{
	Color	c;
	Vec3	p1;
	
	setColorFromARGB(&c, argb);
	
	scaleAddVec3(dir, length, p, p1);
	
	gfxDrawCapsule3D(p, p1, radius, 10, c, 1);
}

static void gclMovementDrawTriangle3D(	const Vec3 p0,
										U32 argb0,
										const Vec3 p1,
										U32 argb1,
										const Vec3 p2,
										U32 argb2)
{
}

static void gclMovementDrawBox3D(	const Vec3 xyzSize,
									const Mat4 mat,
									U32 argb)
{
	Color	c;
	Vec3	minCorner;
	Vec3	maxCorner;
	
	setColorFromARGB(&c, argb);
	
	scaleVec3(xyzSize, 0.5, maxCorner);
	scaleVec3(xyzSize, -0.5f, minCorner);
	
	gfxDrawBox3D(minCorner, maxCorner, mat, c, 1);
}

static void mmDebugLogDraw3D(void){
	size_t			segListTagLength;
	size_t			pointListTagLength;
	size_t			cameraMatTagLength;
	S32				useRightPane;
	
	if(	(	!mmDebugOptionUI.window ||
			!ui_WindowIsVisible(mmDebugOptionUI.window))
		&&
		(	!compareUI.window ||
			!ui_WindowIsVisible(compareUI.window)))
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	if(mmDebugOptionUI.window){
		mmLogUpdateActiveLogLists();
	}
	
	segListTagLength = strlen(segListTag);
	pointListTagLength = strlen(pointListTag);
	cameraMatTagLength = strlen(cameraMatTag);

	EARRAY_CONST_FOREACH_BEGIN(debugSegments.segs, i, size);
	{
		const DebugSegment* seg = debugSegments.segs[i];

		gclMovementDrawLine3D(	seg->a,
								0xffffffff,
								seg->b,
								seg->argb);
	}
	EARRAY_FOREACH_END;
	
	for(useRightPane = 0; useRightPane <= 1; useRightPane++){
		S32					sectionIndex = compareUI.sectionIndex[useRightPane];
		LogSection** const	sections = compareUI.sections[useRightPane];
		
		if(EAINRANGE(sectionIndex, sections)){
			LogSection* section = sections[sectionIndex];
			S32			i;
			
			for(i = section->lineBegin; i <= section->lineEnd; i++){
				MovementLogLine*	line = compareUI.log[useRightPane]->lines[i];
				LogLineParsed		parsed = {0};
				
				mmCompareParseLogLine(	line->text,
										&parsed);
										
				if(	mmParsedThreadIsEnabled(parsed.flags.isBG, useRightPane) &&
					mmParsedHasEnabledTag(&parsed, useRightPane))
				{
					if(strStartsWith(parsed.lineText, segListTag)){
						const char*	cur = parsed.lineText + segListTagLength;
						U32			argbTipColor = useRightPane ? 0xffff5555 : 0xff0000ff;
						
						if(compareUI.hide3D[useRightPane]){
							mmLogLineParsedClear(&parsed);
							continue;
						}
						
						if(extractColor(&cur, &argbTipColor)){
							Vec3 a;
							Vec3 b;
							
							while(SAFE_DEREF(cur) == '('){
								a[0] = atof(++cur);
								cur = strstr(cur, ",");
								if(!cur){
									break;
								}
								a[1] = atof(++cur);
								cur = strstr(cur, ",");
								if(!cur){
									break;
								}
								a[2] = atof(++cur);
								cur = strstr(cur, "(");
								if(!cur){
									break;
								}
								b[0] = atof(++cur);
								cur = strstr(cur, ",");
								if(!cur){
									break;
								}
								b[1] = atof(++cur);
								cur = strstr(cur, ",");
								if(!cur){
									break;
								}
								b[2] = atof(++cur);

								gclMovementDrawLine3D(	a,
														0xffffffff,
														b,
														argbTipColor);

								cur = strstr(cur, "(");
								if(!cur){
									break;
								}
							}
						}
					}
					else if(strStartsWith(parsed.lineText, pointListTag)){
						const char*	cur = parsed.lineText + pointListTagLength;
						U32			argb = useRightPane ? 0xffff8888: 0xff0000ff;
						
						if(compareUI.hide3D[useRightPane]){
							mmLogLineParsedClear(&parsed);
							continue;
						}

						if(extractColor(&cur, &argb)){
							Vec3 a;
							
							while(SAFE_DEREF(cur) == '('){
								a[0] = atof(++cur);
								cur = strstr(cur, ",");
								if(!cur){
									break;
								}
								a[1] = atof(++cur);
								cur = strstr(cur, ",");
								if(!cur){
									break;
								}
								a[2] = atof(++cur);

								{
									Vec3 b;
									
									copyVec3(a, b);
									b[0] += 1;
									gclMovementDrawLine3D(	a,
															0xffffffff,
															b,
															argb);

									copyVec3(a, b);
									b[0] -= 1;
									gclMovementDrawLine3D(	a,
															0xffffffff,
															b,
															argb);

									copyVec3(a, b);
									b[1] += 1;
									gclMovementDrawLine3D(	a,
															0xffffffff,
															b,
															argb);

									copyVec3(a, b);
									b[1] -= 1;
									gclMovementDrawLine3D(	a,
															0xffffffff,
															b,
															argb);

									copyVec3(a, b);
									b[2] += 1;
									gclMovementDrawLine3D(	a,
															0xffffffff,
															b,
															argb);

									copyVec3(a, b);
									b[2] -= 1;
									gclMovementDrawLine3D(	a,
															0xffffffff,
															b,
															argb);
								}
								
								cur = strstr(cur, "(");
							}
						}
					}
					else if(strStartsWith(parsed.lineText, cameraMatTag) &&
							compareUI.activeCamera == useRightPane)
					{
						const char*	cur = parsed.lineText + cameraMatTagLength;
						Mat4		matCamera;
						S32			failed = 0;
						
						FOR_BEGIN(k, 4);
							cur = strstr(cur, "(");
							if(!cur){
								failed = 1;
								break;
							}
							matCamera[k][0] = atof(++cur);
							cur = strstr(cur, ",");
							if(!cur){
								failed = 1;
								break;
							}
							matCamera[k][1] = atof(++cur);
							cur = strstr(cur, ",");
							if(!cur){
								failed = 1;
								break;
							}
							matCamera[k][2] = atof(++cur);
						FOR_END;
						
						if(!failed){
							Vec3 pyr;
							
							getMat3YPR(matCamera, pyr);
							
							scaleVec3(pyr, 180/PI, pyr);
							
							globCmdParse("freecam 1");
							globCmdParsef("setcampos %f %f %f", vecParamsXYZ(matCamera[3]));
							globCmdParsef("setcampyr %f %f %f", vecParamsXYZ(pyr));
							
							if(TRUE_THEN_RESET(compareUI.printCameraPos[useRightPane])){
								const char* textOld = ui_TextAreaGetText(compareUI.textArea[useRightPane]);
								char*		textNew = NULL;
								
								estrStackCreate(&textNew);
								
								estrPrintf(	&textNew,
											"Camera: pos(%f, %f, %f)\n%s",
											vecParamsXYZ(matCamera[3]),
											textOld);
								
								ui_TextAreaSetText(	compareUI.textArea[useRightPane],
													textNew);
								
								ui_TextAreaSetCursorPosition(	compareUI.textArea[useRightPane],
																0);

								estrDestroy(&textNew);
							}
						}
					}
				}
				
				mmLogLineParsedClear(&parsed);
			}
		}
	}
	
	PERFINFO_AUTO_STOP();
}

static S32 mmCompareTextMatchesSearch(	const char* searchString,
										const char* findString)
{
	const char* curSearch = searchString;
	const char* curFind = findString;
	
	if(!findString[0]){
		return 0;
	}
	
	while(1){
		const char* star = strstri(curFind, "*");
		
		if(star){
			char subSearch[1000];
			
			strncpy(subSearch, curFind, star - curFind);
			
			if(subSearch[0]){
				const char* found = strstriConst(curSearch, subSearch);
				
				if(!found){
					return 0;
				}
				
				curSearch = found + strlen(subSearch);
			}
			
			curFind = star + 1;
		}else{
			return !curFind[0] || strstriConst(curSearch, curFind);
		}
	}
}

static S32 lineStartsWithSpecialTag(const char* line){
	return	strStartsWith(line, segListTag) ||
			strStartsWith(line, pointListTag) ||
			strStartsWith(line, cameraMatTag);
}

static S32 mmTagGetGroup(	const char* tag,
							char* groupOut,
							S32 groupLen)
{
	const char* dot = strchr(tag, '.');

	if(dot){
		strncpy_s(groupOut, groupLen, tag, dot - tag);
		return 1;
	}

	return 0;
}

static void mmCompareSetSectionIndex(	S32 useRightPane,
										S32 sectionIndex)
{
	char*			buffer = NULL;
	LogSection**	sections;

	estrStackCreate(&buffer);

	useRightPane = !!useRightPane;

	compareUI.printCameraPos[useRightPane] = 1;

	sections = compareUI.sections[useRightPane];

	if(sectionIndex < 0){
		sectionIndex = 0;
	}
	else if(sectionIndex >= eaSize(&sections)){
		sectionIndex = eaSize(&sections) - 1;
	}

	compareUI.sectionIndex[useRightPane] = sectionIndex;
	
	EARRAY_CONST_FOREACH_BEGIN(compareUI.tags, i, isize);
	{
		LogLineTag*			tag = compareUI.tags[i];
		LogLineTagPaneData* pane = tag->pane + useRightPane;
		
		pane->flags.existsInCurrentView = 0;
		pane->flags.has3D = 0;
		pane->flags.hasText = 0;
	}
	EARRAY_FOREACH_END;

	if(EAINRANGE(sectionIndex, sections)){
		S32				sectionCount = eaSize(&sections);
		LogSection* 	first = sections[0];
		LogSection* 	last = sections[sectionCount - 1];
		LogSection* 	section = sections[sectionIndex];
		S32				j;
		char			searchText[1000];
		const char**	hiddenTags = NULL;
		S32				hiddenLineCount = 0;
		
		strcpy(searchText, ui_TextEntryGetText(compareUI.textEntry[useRightPane]));
		
		if(compareUI.sectionType == CUIST_MOVEMENT_STEP){
			estrPrintf(	&buffer,
						"%d : %d(%d) - %d(%d) (section %d/%d)\n",
						section->processCount,
						first->processCount,
						first->localProcessCount,
						last->processCount,
						last->localProcessCount,
						sectionIndex + 1,
						sectionCount);
		}else{
			estrPrintf(	&buffer,
						"%d : %d - %d (section %d/%d)\n",
						section->frameCount,
						first->frameCount,
						last->frameCount,
						sectionIndex + 1,
						sectionCount);
		}

		for(j = section->lineBegin; j <= section->lineEnd; j++){
			MovementLogLine*	line = compareUI.log[useRightPane]->lines[j];
			S32					foundSearchText = 0;
			LogLineParsed		parsed = {0};
			S32					hasSpecialTag;
			
			mmCompareParseLogLine(	line->text,
									&parsed);

			if(!mmParsedThreadIsEnabled(parsed.flags.isBG, useRightPane)){
				mmLogLineParsedClear(&parsed);
				continue;
			}
			
			hasSpecialTag = lineStartsWithSpecialTag(parsed.lineText);
			
			EARRAY_CONST_FOREACH_BEGIN(parsed.tags, i, isize);
			{
				LogLineTag*			tag;
				LogLineTagPaneData* pane;
				char				group[100];
				
				mmCompareGetLogLineTag(&tag, parsed.tags[i], 1);
				
				pane = tag->pane + useRightPane;

				pane->flags.existsInCurrentView = 1;
				
				if(hasSpecialTag){
					pane->flags.has3D = 1;
				}else{
					pane->flags.hasText = 1;
				}

				if(mmTagGetGroup(parsed.tags[i], SAFESTR(group))){
					mmCompareGetLogLineTag(&tag, group, 1);
					
					pane = tag->pane + useRightPane;
					
					pane->flags.existsInCurrentView = 1;

					if(hasSpecialTag){
						pane->flags.has3D = 1;
					}else{
						pane->flags.hasText = 1;
					}
				}
			}
			EARRAY_FOREACH_END;
				
			if(hasSpecialTag){
				mmLogLineParsedClear(&parsed);
				continue;
			}
			
			if(!mmParsedHasEnabledTag(&parsed, useRightPane)){
				hiddenLineCount++;
				
				EARRAY_CONST_FOREACH_BEGIN(parsed.tags, i, isize);
				{
					S32 found = 0;
					
					EARRAY_CONST_FOREACH_BEGIN(hiddenTags, k, ksize);
					{
						if(parsed.tags[i] == hiddenTags[k]){
							found = 1;
							break;
						}
					}
					EARRAY_FOREACH_END;
					
					if(!found){
						eaPush(&hiddenTags, parsed.tags[i]);
					}
				}
				EARRAY_FOREACH_END;
			}else{
				if(	hiddenLineCount &&
					!compareUI.hideDisabled[useRightPane])
				{
					char tagBuffer[1000];

					tagBuffer[0] = 0;
					
					EARRAY_CONST_FOREACH_BEGIN(hiddenTags, i, isize);
					{
						strcatf(tagBuffer,
								"%s%s",
								i ? ", " : "",
								hiddenTags[i]);
					}
					EARRAY_FOREACH_END;
					
					estrConcatf(&buffer,
								"\nHIDDEN:  ******** %u lines: %s ********\n\n",
								hiddenLineCount,
								tagBuffer);
					
					eaDestroy(&hiddenTags);
					hiddenLineCount = 0;
				}
				
				if(mmCompareTextMatchesSearch(parsed.lineText, searchText)){
					foundSearchText = 1;
					estrConcatf(&buffer,
								">\n"
								">>>\n"
								">>>>>>>\n"
								">>>>>>>>>>>>>>>\n"
								">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n"
								"\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/"
								" *** FOUND *** "
								"\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/\n");
				}
				
				estrConcatf(&buffer,
							"%4.4d %s: ",
							parsed.frameCount,
							parsed.flags.isBG ? "BG" : "FG");
				
				if(	compareUI.tagsVisible[useRightPane] &&
					eaSize(&parsed.tags))
				{
					estrConcatf(&buffer, "[");
					
					EARRAY_CONST_FOREACH_BEGIN(parsed.tags, i, isize);
					{
						estrConcatf(&buffer,
									"%s%s",
									i ? ", " : "",
									parsed.tags[i]);
					}
					EARRAY_FOREACH_END;

					estrConcatf(&buffer, "] ");
				}

				{
					const char* tabSpaces = "               ";
					const char* cur = parsed.lineText;
					const char* newLine;
					S32			firstOneLine = 1;
					
					for(newLine = strstr(cur, "\n");
						newLine;
						newLine = strstr(cur, "\n"))
					{
						char oneLine[5000];
						
						strncpy(oneLine, cur, newLine - cur);
						
						estrConcatf(&buffer,
									"%s%s\n",
									firstOneLine ? "" : tabSpaces,
									oneLine);
									
						cur = newLine + strlen("\n");
						
						firstOneLine = 0;
					}
					
					if(*cur){
						estrConcatf(&buffer,
									"%s%s\n",
									firstOneLine ? "" : tabSpaces,
									cur);
					}
				}

				if(foundSearchText){
					estrConcatf(&buffer,
								"/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\"
								" *** FOUND *** "
								"/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\\n"
								">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n"
								">>>>>>>>>>>>>>>\n"
								">>>>>>>\n"
								">>>\n"
								">\n");
				}
			}
			
			mmLogLineParsedClear(&parsed);
		} 

		if(	hiddenLineCount &&
			!compareUI.hideDisabled[useRightPane])
		{
			char tagBuffer[1000];

			tagBuffer[0] = 0;
			
			EARRAY_CONST_FOREACH_BEGIN(hiddenTags, k, ksize);
			{
				strcatf(tagBuffer,
						"%s%s",
						k ? ", " : "",
						hiddenTags[k]);
			}
			EARRAY_FOREACH_END;
			
			estrConcatf(&buffer,
						"\nHIDDEN:  ******** %u lines: %s ********\n\n",
						hiddenLineCount,
						tagBuffer);
			
			eaDestroy(&hiddenTags);
			hiddenLineCount = 0;
		}
	}

	if(	buffer &&
		ui_TextAreaSetText(	compareUI.textArea[useRightPane],
							buffer))
	{
		ui_TextAreaSetCursorPosition(compareUI.textArea[useRightPane], 0);
	}

	estrDestroy(&buffer);

	mmCompareRefreshTagButtons();
}

static void mmCompareFindAndSetText(S32 useRightPane,
									const char* searchText,
									S32 gotoNext)
{
	S32 			sectionIndex = compareUI.sectionIndex[useRightPane] + (gotoNext ? 1 : -1);
	S32 			found = 0;
	LogSection**	sections;

	useRightPane = !!useRightPane;

	sections = compareUI.sections[useRightPane];
	
	while(	!found &&
			sectionIndex >= 0 &&
			sectionIndex < eaSize(&sections))
	{
		LogSection* section = sections[sectionIndex];
		S32			i;
		
		for(i = section->lineBegin;
			!found && i <= section->lineEnd;
			i++)
		{
			LogLineParsed parsed;
			
			mmCompareParseLogLine(	compareUI.log[useRightPane]->lines[i]->text,
									&parsed);
			
			if(	mmParsedThreadIsEnabled(parsed.flags.isBG, useRightPane) &&
				mmParsedHasEnabledTag(&parsed, useRightPane) &&
				mmCompareTextMatchesSearch(parsed.lineText, searchText))
			{
				found = 1;
			}
			
			mmLogLineParsedClear(&parsed);
		}
		
		if(!found){
			sectionIndex += gotoNext ? 1 : -1;
		}
	}
	
	if(found){
		mmCompareSetSectionIndex(useRightPane, sectionIndex);
	}
}

// Sample log format:
// ffff bg: [ai] Something.
// ffff bg: [ai] SegList: (%f, %f, %f)-(%f, %f, %f)

static void buttonSetEnabledColor(	UIButton* b,
									S32 enabled)
{
	ui_WidgetSkin(	(UIWidget*)b,
					enabled ?
						compareUI.skinButtonGreen :
						compareUI.skinButtonRed);
}

static void mmCompareButtonPressed(	UIAnyWidget* widget,
									UserData userData)
{
	ARRAY_FOREACH_BEGIN(compareUI.button, i);
	{
		ARRAY_FOREACH_BEGIN(compareUI.button[0], j);
		{
			if(widget == compareUI.button[i][j]){
				LogSection** sections = compareUI.sections[i];
				
				switch(j){
					xcase 0:{
						mmCompareSetSectionIndex(i, 0);
					}

					xcase 1:{
						mmCompareSetSectionIndex(i, compareUI.sectionIndex[i] - 10);
					}

					xcase 2:{
						mmCompareSetSectionIndex(i, compareUI.sectionIndex[i] - 1);
					}

					xcase 3:{
						mmCompareSetSectionIndex(i, compareUI.sectionIndex[i] + 1);
					}

					xcase 4:{
						mmCompareSetSectionIndex(i, compareUI.sectionIndex[i] + 10);
					}

					xcase 5:{
						mmCompareSetSectionIndex(i, eaSize(&sections) - 1);
					}
				}
			}
		}
		ARRAY_FOREACH_END;
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(compareUI.button, i);
	{
		if(widget == compareUI.commonButton[i]){
			switch(i){
				xcase 0:{
					if(	compareUI.sectionIndex[0] > 0 &&
						compareUI.sectionIndex[1] > 0)
					{
						mmCompareSetSectionIndex(0, compareUI.sectionIndex[0] - 1);
						mmCompareSetSectionIndex(1, compareUI.sectionIndex[1] - 1);
					}
				}

				xcase 1:{
					if(	compareUI.sectionIndex[0] + 1 < eaUSize(&compareUI.sections[0]) &&
						compareUI.sectionIndex[1] + 1 < eaUSize(&compareUI.sections[1]))
					{
						mmCompareSetSectionIndex(0, compareUI.sectionIndex[0] + 1);
						mmCompareSetSectionIndex(1, compareUI.sectionIndex[1] + 1);
					}
				}
			}
		}
	}
	ARRAY_FOREACH_END;
	
	ARRAY_FOREACH_BEGIN(compareUI.wordWrapButton, i);
	{
		if(widget == compareUI.wordWrapButton[i]){
			compareUI.wordWrapEnabled[i] = !compareUI.wordWrapEnabled[i];
			
			ui_TextAreaSetWordWrap(compareUI.textArea[i], compareUI.wordWrapEnabled[i]);
			
			buttonSetEnabledColor(compareUI.wordWrapButton[i], compareUI.wordWrapEnabled[i]);
			
			mmCompareSetSectionIndex(i, compareUI.sectionIndex[i]);
		}
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(compareUI.threadFilterButton, i);
	{
		if(widget == compareUI.threadFilterButton[i]){
			compareUI.threadFilter[i] = (compareUI.threadFilter[i] + 1) % 3;
			
			switch(compareUI.threadFilter[i]){
				xcase 0:
					ui_ButtonSetText(widget, "FG");
				xcase 1:
					ui_ButtonSetText(widget, "BG");
				xcase 2:
					ui_ButtonSetText(widget, "FG+BG");
			}
			
			mmCompareSetSectionIndex(i, compareUI.sectionIndex[i]);
		}
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(compareUI.toggle3DButton, i);
	{
		if(widget == compareUI.toggle3DButton[i]){
			compareUI.hide3D[i] = !compareUI.hide3D[i];
			
			buttonSetEnabledColor(compareUI.toggle3DButton[i], !compareUI.hide3D[i]);
			break;
		}
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(compareUI.cameraButton, i);
	{
		if(widget == compareUI.cameraButton[i]){
			ARRAY_FOREACH_BEGIN(compareUI.cameraButton, j);
			{
				if(j != i){
					buttonSetEnabledColor(compareUI.cameraButton[j], 0);
				}
			}
			ARRAY_FOREACH_END;
			
			if(compareUI.activeCamera == i){
				compareUI.activeCamera = -1;
				buttonSetEnabledColor(compareUI.cameraButton[i], 0);
				globCmdParse("freecam 0");
			}else{
				compareUI.activeCamera = i;
				buttonSetEnabledColor(compareUI.cameraButton[i], 1);
			}
			break;
		}
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(compareUI.tagsButton, i);
	{
		if(widget == compareUI.tagsButton[i]){
			compareUI.tagsVisible[i] = !compareUI.tagsVisible[i];
			
			buttonSetEnabledColor(	compareUI.tagsButton[i],
									compareUI.tagsVisible[i]);

			mmCompareSetSectionIndex(i, compareUI.sectionIndex[i]);
			break;
		}
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(compareUI.hideButton, i);
	{
		if(widget == compareUI.hideButton[i]){
			compareUI.hideDisabled[i] = !compareUI.hideDisabled[i];

			ui_WidgetSkin(	(UIWidget*)compareUI.hideButton[i],
							compareUI.hideDisabled[i] ?
								compareUI.skinButtonRed :
								compareUI.skinButtonGreen);

			mmCompareSetSectionIndex(i, compareUI.sectionIndex[i]);
			break;
		}
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(compareUI.textEntryButton, i);
	{
		ARRAY_FOREACH_BEGIN(compareUI.textEntryButton[i], j);
		{
			if(widget == compareUI.textEntryButton[i][j]){
				char buffer[1000];
				
				strcpy(buffer, ui_TextEntryGetText(compareUI.textEntry[i]));
				
				mmCompareFindAndSetText(i, buffer, j);
				break;
			}
		}
		ARRAY_FOREACH_END;
	}
	ARRAY_FOREACH_END;
}

static void mmCompareTagButtonClicked(UIButton* button, void* unused){
	EARRAY_CONST_FOREACH_BEGIN(compareUI.tags, j, jsize);
	{
		LogLineTag* tag = compareUI.tags[j];
		
		if(tag->button == button){
			tag->flags.groupOpen = !tag->flags.groupOpen;
		}else{
			ARRAY_FOREACH_BEGIN(tag->pane, i);
			{
				LogLineTagPaneData* pane = tag->pane + i;
				
				if(pane->button == button){
					pane->flags.disabled = !pane->flags.disabled;
					
					if(pane->flags.isGroup){
						EARRAY_CONST_FOREACH_BEGIN(compareUI.tags, k, ksize);
						{
							char group[100];
							
							if(	compareUI.tags[k] != tag &&
								mmTagGetGroup(compareUI.tags[k]->name, SAFESTR(group)))
							{
								if(!stricmp(group, tag->name)){
									compareUI.tags[k]->pane[i].flags.disabled = pane->flags.disabled;
								}
							}
						}
						EARRAY_FOREACH_END;
					}
					
					mmCompareSetSectionIndex(i, compareUI.sectionIndex[i]);
					
					break;
				}
			}
			ARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;

	mmCompareRefreshTagButtons();
}

static void mmCompareRefreshTagButtons(void){
	S32 existsCount = 0;
	
	mmCompareCreateSkins();
	
	EARRAY_CONST_FOREACH_BEGIN(compareUI.tags, i, isize);
	{
		LogLineTag* tag = compareUI.tags[i];
		char		group[100];
		S32			isGroup = 0;
		S32			exists = 0;
		S32			hasGroup = 0;

		if(mmTagGetGroup(tag->name, SAFESTR(group))){
			LogLineTag* tagGroup;
			
			hasGroup = 1;
			
			mmCompareGetLogLineTag(&tagGroup, group, 1);
			
			if(!tagGroup->flags.groupOpen){
				ui_WidgetDestroy(&tag->label);
				ui_WidgetDestroy(&tag->button);

				ARRAY_FOREACH_BEGIN(tag->pane, j);
				{
					LogLineTagPaneData* pane = tag->pane + j;
					
					ui_WidgetDestroy(&pane->button);
				}
				ARRAY_FOREACH_END;
				
				continue;
			}
		}

		ARRAY_FOREACH_BEGIN(tag->pane, j);
		{
			LogLineTagPaneData* pane = tag->pane + j;
			
			if(pane->flags.isGroup){
				isGroup = 1;
			}
			
			if(pane->flags.exists){
				exists = 1;
				
				if(!pane->button){
					pane->button = newButton(	"",
												0,
												0,
												mmCompareTagButtonClicked,
												NULL);
					
					ui_WindowAddChild(compareUI.window, pane->button);
				}
				
				ui_ButtonSetTextAndResize(	pane->button,
											pane->flags.disabled ? "+" : "--");
											
				ui_WidgetSetPositionEx(	(UIWidget*)pane->button,
										0,
										240 + existsCount * 25,
										hasGroup ? 0.425f : 0.42f,
										0,
										j ? UITopRight : UITopLeft);
										
				ui_WidgetSkin(	(UIWidget*)pane->button,
								pane->flags.existsInCurrentView ?
									pane->flags.disabled ?
										compareUI.skinButtonYellow :
										compareUI.skinButtonGreen :
									compareUI.skinButtonRed);
			}
		}
		ARRAY_FOREACH_END;
		
		if(!isGroup){
			ARRAY_FOREACH_BEGIN(tag->pane, j);
			{
				LogLineTagPaneData* pane = tag->pane + j;

				if(pane->button){
					F32 w = ui_WidgetGetWidth((UIWidget*)pane->button);
					
					ui_WidgetSetWidth((UIWidget*)pane->button, w - 4);
				}
			}
			ARRAY_FOREACH_END;
		}
				
		if(!exists){
			continue;
		}
		
		if(isGroup){
			char name[100];
			
			ui_WidgetDestroy(&tag->label);
			
			if(!tag->button){
				tag->button = newButton("",
										0,
										0,
										mmCompareTagButtonClicked,
										NULL);

				ui_WindowAddChild(	compareUI.window,
									tag->button);
			}
			
			sprintf(name,
					"(%s%s) %s (%s%s)",
					tag->pane[0].flags.has3D ? "g" : "-",
					tag->pane[0].flags.hasText ? "t" : "-",
					tag->name,
					tag->pane[1].flags.has3D ? "g" : "-",
					tag->pane[1].flags.hasText ? "t" : "-");

			ui_WidgetSkin(	(UIWidget*)tag->button,
							tag->flags.groupOpen ?
								compareUI.skinButtonGreen :
								compareUI.skinButtonRed);

			ui_ButtonSetTextAndResize(	tag->button,
										name);
			
			ui_WidgetSetPositionEx(	(UIWidget*)tag->button,
									0,
									240 + existsCount * 25,
									0,
									0,
									UITop);
		}else{
			ui_WidgetDestroy(&tag->button);

			if(!tag->label){
				tag->label = ui_LabelCreate("",
											10,
											30);

				ui_WindowAddChild(	compareUI.window,
									tag->label);
			}
			
			{
				char		label[100];
				const char* dot = strchr(tag->name, '.');

				sprintf(label,
						"(%s%s) %s (%s%s)",
						tag->pane[0].flags.has3D ? "g" : "-",
						tag->pane[0].flags.hasText ? "t" : "-",
						dot ? dot : tag->name,
						tag->pane[1].flags.has3D ? "g" : "-",
						tag->pane[1].flags.hasText ? "t" : "-");

				ui_LabelSetText(tag->label,
								label);
								
				((UIWidget*)tag->label)->scale = 0.9f;
			}
			
			ui_WidgetSetPositionEx(	(UIWidget*)tag->label,
									0,
									240 + existsCount * 25,
									0,
									0,
									UITop);
		}
		
		existsCount++;
	}
	EARRAY_FOREACH_END;
}

static void mmCompareLogsReparseSections(S32 useRightPane){
	S32				byProcessCount = compareUI.sectionType == CUIST_MOVEMENT_STEP;
	S32				byFrameCount = !byProcessCount;
	LogSection*		section;
	LogSection***	sections = &compareUI.sections[useRightPane];
	MovementLog*	log = compareUI.log[useRightPane];
	char*			buffer = NULL;
	S32				buttonCount = 0;
	
	assert(useRightPane >= 0 && useRightPane <= 1);
	
	eaDestroyEx(&compareUI.sections[useRightPane], NULL);
	
	mmCompareTagsClearExists(useRightPane);

	if(!log){
		return;
	}
	
	section = callocStruct(LogSection);

	eaPush(sections, section);
	
	EARRAY_CONST_FOREACH_BEGIN(log->lines, i, isize);
	{
		MovementLogLine*	line = log->lines[i];
		const char*			beginTag = "*\\/ begin ";
		const char*			localProcessTag = "Local process: ";
		const char*			foundLoc;
		LogLineParsed		parsed;
		
		mmCompareParseLogLine(	line->text,
								&parsed);
								
		EARRAY_CONST_FOREACH_BEGIN(parsed.tags, j, jsize);
		{
			LogLineTag* tag;
			char		group[100];
			
			mmCompareGetLogLineTag(&tag, parsed.tags[j], 1);
			
			tag->pane[useRightPane].flags.exists = 1;
			
			if(mmTagGetGroup(parsed.tags[j], SAFESTR(group))){
				mmCompareGetLogLineTag(&tag, group, 1);
				
				tag->pane[useRightPane].flags.exists = 1;
				tag->pane[useRightPane].flags.isGroup = 1;
			}
		}
		EARRAY_FOREACH_END;

		if(byProcessCount){
			if(foundLoc = strstri(parsed.lineText, beginTag)){
				if(section){
					section->lineEnd = i ? i - 1 : 0;
				}

				section = callocStruct(LogSection);

				section->processCount = atoi(foundLoc + strlen(beginTag));

				if(eaSize(sections) == 1){
					sections[0][0]->processCount = section->processCount - 2;
				}

				eaPush(sections, section);

				section->lineBegin = i;
			}
			else if(foundLoc = strstri(parsed.lineText, localProcessTag)){
				if(section){
					section->localProcessCount = atoi(foundLoc + strlen(localProcessTag));

					if(eaSize(sections) == 2){
						sections[0][0]->localProcessCount = section->localProcessCount - 2;
					}
				}
			}
		}
		else if(byFrameCount){
			if(	!section ||
				parsed.frameCount != section->frameCount)
			{
				if(section){
					section->lineEnd = i - 1;
				}
				
				section = callocStruct(LogSection);
				eaPush(sections, section);
				section->frameCount = parsed.frameCount;
				section->lineBegin = i;
			}
		}
		
		mmLogLineParsedClear(&parsed);
	}
	EARRAY_FOREACH_END;
	
	mmCompareRefreshTagButtons();
	
	if(section){
		section->lineEnd = eaSize(&log->lines) - 1;
	}
}

static void mmSetSectionType(CompareUISectionType sectionType){
	F32 ratio[2];
	
	compareUI.sectionType = ((sectionType % CUIST_COUNT) + CUIST_COUNT) % CUIST_COUNT;
	
	ui_ButtonSetTextAndResize(compareUI.sortType, mmCompareUISectionTypeName(compareUI.sectionType));
	
	ARRAY_FOREACH_BEGIN(ratio, i);
	{
		ratio[i] = eaSize(&compareUI.sections[i]) ?
						(F32)compareUI.sectionIndex[i] /
							(F32)(eaSize(&compareUI.sections[i]) - 1) :
						0;
	}
	ARRAY_FOREACH_END;
	
	mmCompareLogsReparseSections(0);
	mmCompareLogsReparseSections(1);

	ARRAY_FOREACH_BEGIN(ratio, i);
	{
		mmCompareSetSectionIndex(i, ratio[i] * (eaSize(&compareUI.sections[i]) - 1));
	}
	ARRAY_FOREACH_END;
}

static void mmSortTypeButtonPressed(UIButton* button, void* unused){
	mmSetSectionType(compareUI.sectionType + 1);
}

static void mmRunCommandOnTargets(const char* format, ...){
	S32 i;
	
	for(i = 0; i < 2; i++){
		if((mmDebugOptionUI.targetType + 1) & (1 << i)){
			char		buffer[1024];
			const char* target = i ? "selected" : "me";
			
			VA_START(va, format);
				vsprintf(	buffer,
							format,
							va);
			VA_END()
			
			globCmdParsef(FORMAT_OK(buffer), target);
		}
	}	
}

static void mmDebugRecordPressed(UIButton* button, void* idxPtr){
	S32 whichIndex = PTR_TO_U32(idxPtr);
	S32 recordWasEnabled = stricmp(ui_WidgetGetText(UI_WIDGET(button)), recordButtonText[whichIndex]);
	
	if(	whichIndex == 0 ||
		whichIndex == 1)
	{
		mmRunCommandOnTargets("ec %%s mmdebug %d", !recordWasEnabled);
	}
	
	if(	whichIndex == 2 ||
		whichIndex == 1)
	{
		mmRunCommandOnTargets("mmdebug %%s %d", !recordWasEnabled);
	}
	
	ui_ButtonSetText(button, recordButtonText[whichIndex + (!recordWasEnabled * 3)]);
	
	if(whichIndex == 1){
		ui_ButtonSetText(mmDebugOptionUI.recordButton[0], recordButtonText[(!recordWasEnabled * 3)]);
		ui_ButtonSetText(mmDebugOptionUI.recordButton[2], recordButtonText[2 + (!recordWasEnabled * 3)]);
	}
}

static void mmDebugToggleTarget(UIButton* button, void* unused){
	mmDebugOptionUI.targetType = (mmDebugOptionUI.targetType + 1) % 3;
	
	if (!stricmp(ui_WidgetGetText(UI_WIDGET(mmDebugOptionUI.recordButton[0])), recordButtonText[3])){
		mmDebugRecordPressed(mmDebugOptionUI.recordButton[0], S32_TO_PTR(0));
	}

	if (!stricmp(ui_WidgetGetText(UI_WIDGET(mmDebugOptionUI.recordButton[2])), recordButtonText[5])){
		mmDebugRecordPressed(mmDebugOptionUI.recordButton[2], S32_TO_PTR(2));
	}
		
	switch(mmDebugOptionUI.targetType){
		xcase 0:
			ui_ButtonSetTextAndResize(button, "me");
		xcase 1:
			ui_ButtonSetTextAndResize(button, "selected");
		xcase 2:
			ui_ButtonSetTextAndResize(button, "me + selected");
	}

	ARRAY_FOREACH_BEGIN(mmDebugOptionUI.recordButton, i);
	{
		ui_ButtonSetText(mmDebugOptionUI.recordButton[i], recordButtonText[i]);
	}
	ARRAY_FOREACH_END;
}

void mmDebugStopRecording(Entity *eTarget)
{
	if (eTarget && mmIsDebugging(eTarget->mm.movement) && mmDebugOptionUI.window)
	{
		UIButton *pClientButton = mmDebugOptionUI.recordButton[2];
		UIButton *pServerButton = mmDebugOptionUI.recordButton[0];
		UIButton *pBothButton = mmDebugOptionUI.recordButton[1];

		mmSetDebugging(eTarget->mm.movement, 0);

		// see if the client was logging
		if (pClientButton && !stricmp(ui_WidgetGetText(UI_WIDGET(pClientButton)), recordButtonText[5]))
		{
			// reset the button text
			ui_ButtonSetText(pClientButton, recordButtonText[2]);
			mmCmdCompareLogsSetEntInternal(1, 1, "default", entGetRef(eTarget));
		}

		// todo: handle the server logging
	}
	
}

static void mmDebugRequestLog(UIButton* button, void* unused){
	mmRunCommandOnTargets("ec %%s mmSendMeLogs");
}

static void mmDebugSkeletonButtonPressed(UIButton* button, void* unused){
	const char* buttonText = ui_WidgetGetText(UI_WIDGET(button));
	S32 wasEnabled = !stricmp(buttonText, "Stop Skeleton Log");
	ui_ButtonSetTextAndResize(button, wasEnabled ? "Log Skeleton" : "Stop Skeleton Log");
	globCmdParsef("mmLogSkeletons %d", !wasEnabled);
}

static void mmGetAvailableLog(	AvailableLog** availableLogOut,
								EntityRef er,
								const char* name)
{
	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.availableLogs, i, size);
	{
		AvailableLog* availableLog = mmDebugOptionUI.availableLogs[i];
		
		if(	availableLog->er == er &&
			!stricmp(availableLog->name, name))
		{
			*availableLogOut = availableLog;
			return;
		}
	}
	EARRAY_FOREACH_END;
	
	*availableLogOut = callocStruct(AvailableLog);
	
	(*availableLogOut)->er = er;
	(*availableLogOut)->name = strdup(name);
	
	eaPush(&mmDebugOptionUI.availableLogs, (*availableLogOut));
}

static void mmLogsForEachCallback(	EntityRef er,
									const char* name,
									S32 isLocal)
{
	Entity*		be = entFromEntityRefAnyPartition(er);
	AvailableLog*	availableLog;
	
	mmGetAvailableLog(	&availableLog,
						er,
						name);
						
	if(be){
		SAFE_FREE(availableLog->entName);
		
		availableLog->entName = strdup(be->debugName);
	}
						
	availableLog->flags.exists = 1;
						
	if(isLocal){
		availableLog->flags.hasLocal = 1;
	}else{
		availableLog->flags.hasRemote = 1;
	}
}

static void mmDebugPressedAvailableLogButton(UIButton* button, AvailableLog* availableLog){
	ARRAY_FOREACH_BEGIN(availableLog->button, i);
	{
		if(button == availableLog->button[i]){
			switch(i){
				xcase 0:{
					globCmdParsef(	"mmCompareLogsSetEnt 0 0 %s %d",
									availableLog->name,
									availableLog->er);
				}
				xcase 1:{
					globCmdParsef(	"mmCompareLogsSetEnt 1 0 %s %d",
									availableLog->name,
									availableLog->er);
				}
				xcase 2:{
					globCmdParsef(	"mmCompareLogsSetEnt 1 1 %s %d",
									availableLog->name,
									availableLog->er);
				}
				xcase 3:{
					globCmdParsef(	"mmCompareLogsSetEnt 0 1 %s %d",
									availableLog->name,
									availableLog->er);
				}
			}
		}
	}
	ARRAY_FOREACH_END;
}

static void mmDebugRefreshAvailable(UIButton* button, void* unused){
	S32 count = 0;
	
	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.availableLogs, i, size);
	{
		mmDebugOptionUI.availableLogs[i]->flags.exists = 0;
	}
	EARRAY_FOREACH_END;

	mmLogsForEach(mmLogsForEachCallback);

	EARRAY_CONST_FOREACH_BEGIN(mmDebugOptionUI.availableLogs, i, size);
	{
		AvailableLog* availableLog = mmDebugOptionUI.availableLogs[i];
		
		ARRAY_FOREACH_BEGIN(availableLog->button, j);
		{
			if(availableLog->flags.exists){
				if(!availableLog->button[j]){
					if(	(	j == 1 ||
							j == 2) &&
						availableLog->flags.hasLocal
						||
						(	j == 0 ||
							j == 3) &&
						availableLog->flags.hasRemote)
					{
						availableLog->button[j] = newButton(j >= 1 && j <= 2 ? "c" : "s",
															0,
															0,
															mmDebugPressedAvailableLogButton,
															availableLog);
																	
						ui_WindowAddChild(mmDebugOptionUI.window, availableLog->button[j]);
					}
				}
				
				if(availableLog->button[j]){
					ui_WidgetSetPositionEx(	(UIWidget*)availableLog->button[j],
											j >= 1 && j <= 2 ? 17 : 0,
											200 + count * 25,
											0,
											0,
											j < 2 ? UITopLeft : UITopRight);
				}

				if(!availableLog->label){
					S32		isDefault = !stricmp(availableLog->name, "default");
					char	name[100];
					
					sprintf(name,
							"0x%x:%s%s%s%s",
							availableLog->er,
							availableLog->entName,
							isDefault ? "" : " (",
							isDefault ? "" : availableLog->name,
							isDefault ? "" : ")");
							
					availableLog->label = ui_LabelCreate(name, 200, 30);
					
					ui_WindowAddChild(mmDebugOptionUI.window, availableLog->label);
				}
				
				ui_WidgetSetPositionEx(	(UIWidget*)availableLog->label,
										0,
										200 + count * 25,
										0,
										0,
										UITop);

				if(j == 3){
					count++;
				}
			}else{
				ui_WidgetDestroy(&availableLog->button[j]);
				ui_WidgetDestroy(&availableLog->label);
			}
		}
		ARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
	
	mmLogUpdateActiveLogLists();
}

enum {
	MM_SHOW_SERVER,
	MM_SHOW_CLIENT,
	MM_SHOW_REPREDICT,
	MM_TOGGLE_RESOURCE_LOG_SERVER,
	MM_TOGGLE_RESOURCE_LOG_CLIENT,
	MM_LEFT_PREV,
	MM_LEFT_NEXT,
	MM_BOTH_PREV,
	MM_BOTH_NEXT,
	MM_RIGHT_PREV,
	MM_RIGHT_NEXT,
};

static void mmDebugShowLog(UIButton* button, void* showWhatPtr){
	U32 showWhat = PTR_TO_U32(showWhatPtr);

	switch (showWhat){
		xcase MM_SHOW_SERVER:
			mmRunCommandOnTargets("mmCompareLogsSetEnt 0 0 default %%s");
		xcase MM_SHOW_CLIENT:
			mmRunCommandOnTargets("mmCompareLogsSetEnt 1 1 default %%s");
		xcase MM_SHOW_REPREDICT:
			mmRunCommandOnTargets("mmCompareLogsSetEnt 1 0 default_repredict %%s");
		xcase MM_TOGGLE_RESOURCE_LOG_CLIENT:{
			S32 isEnabled = !!stricmp(ui_ButtonGetText(button), "Log Res Client");
			ui_ButtonSetTextAndResize(button, isEnabled ? "Log Res Client" : "No Log Res Client");
			globCmdParsef("mmLogManagedResourcesClient %d", !isEnabled);
			globCmdParsef("mmLogUnmanagedResourcesClient %d", !isEnabled);
		}
		xcase MM_TOGGLE_RESOURCE_LOG_SERVER:{
			S32 isEnabled = !!stricmp(ui_ButtonGetText(button), "Log Res Server");
			ui_ButtonSetTextAndResize(button, isEnabled ? "Log Res Server" : "No Log Res Server");
			globCmdParsef("mmLogManagedResourcesServer %d", !isEnabled);
			globCmdParsef("mmLogUnmanagedResourcesServer %d", !isEnabled);
		}
		xcase MM_LEFT_PREV:
			mmCompareSetSectionIndex(0, compareUI.sectionIndex[0] - 1);
		xcase MM_LEFT_NEXT:
			mmCompareSetSectionIndex(0, compareUI.sectionIndex[0] + 1);
		xcase MM_BOTH_PREV:
			if(	compareUI.sectionIndex[0] > 0 &&
				compareUI.sectionIndex[1] > 0)
			{
				mmCompareSetSectionIndex(0, compareUI.sectionIndex[0] - 1);
				mmCompareSetSectionIndex(1, compareUI.sectionIndex[1] - 1);
			}
		xcase MM_BOTH_NEXT:
			if(	compareUI.sectionIndex[0] + 1 < eaUSize(&compareUI.sections[0]) &&
				compareUI.sectionIndex[1] + 1 < eaUSize(&compareUI.sections[1]))
			{
				mmCompareSetSectionIndex(0, compareUI.sectionIndex[0] + 1);
				mmCompareSetSectionIndex(1, compareUI.sectionIndex[1] + 1);
			}
		xcase MM_RIGHT_PREV:
			mmCompareSetSectionIndex(1, compareUI.sectionIndex[1] - 1);
		xcase MM_RIGHT_NEXT:
			mmCompareSetSectionIndex(1, compareUI.sectionIndex[1] + 1);
	}
}

static bool mmDebugOptionUICloseCallback(	UIAnyWidget* w,
											void* unused)
{
	mmClientSetSendLogListUpdates(0);
	return true;
}

AUTO_COMMAND ACMD_NAME(mmDebugWindow);
void mmDebugShowWindow(void){
	mmClientSetSendLogListUpdates(1);

	mmCompareCreateSkins();

	if (!mmDebugOptionUI.window){
		UILabel* label;
		UIButton* button;

		mmDebugOptionUI.window = ui_WindowCreate("Movement Debug Options", 10000, 100, 250, 500);
		
		ui_WindowSetCloseCallback(mmDebugOptionUI.window, mmDebugOptionUICloseCallback, NULL);
		
		label = ui_LabelCreate("Current Target:", UI_HSTEP, 0);
		
		mmDebugOptionUI.targetButton = newButton(	"me",
													label->widget.x +
														label->widget.width +
														UI_HSTEP,
													0,
													mmDebugToggleTarget,
													NULL);
														
		ui_WindowAddChild(mmDebugOptionUI.window, label);
		ui_WindowAddChild(mmDebugOptionUI.window, mmDebugOptionUI.targetButton);

		ARRAY_FOREACH_BEGIN(mmDebugOptionUI.recordButton, i);
		{
			UIButton*	prevButton = NULL;
			S32			xPos;

			if (i > 0)
			{
				ANALYSIS_ASSUME(i > 0);
#pragma warning(suppress:6200) // /analyze ignores the ANALYSIS_ASSUME above, for whatever reason
				prevButton = mmDebugOptionUI.recordButton[i - 1];
			}
			
			xPos = prevButton ?
						(prevButton->widget.x + prevButton->widget.width + UI_HSTEP) :
						UI_HSTEP;
			
			mmDebugOptionUI.recordButton[i] = newButton(recordButtonText[i],
														xPos,
														25,
														mmDebugRecordPressed,
														S32_TO_PTR(i));
			
			ui_WindowAddChild(mmDebugOptionUI.window, mmDebugOptionUI.recordButton[i]);
		}
		ARRAY_FOREACH_END;

		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	"Show Server Log",
												UI_HSTEP,
												50,
												mmDebugShowLog,
												U32_TO_PTR(MM_SHOW_SERVER)));
														
		ui_WindowAddChild(	mmDebugOptionUI.window,
							newButton(	"Request Server Log",
										button->widget.x + button->widget.width + UI_HSTEP,
										50,
										mmDebugRequestLog,
										NULL));
											
		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	"Show Client Log",
												UI_HSTEP,
												75,
												mmDebugShowLog,
												U32_TO_PTR(MM_SHOW_CLIENT)));
														
		ui_WindowAddChild(	mmDebugOptionUI.window,
							newButton(	"Show Repredict Log",
										button->widget.x + button->widget.width + UI_HSTEP,
										75,
										mmDebugShowLog,
										U32_TO_PTR(MM_SHOW_REPREDICT)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							newButton(	"Log Res Client",
										UI_HSTEP,
										100,
										mmDebugShowLog,
										U32_TO_PTR(MM_TOGGLE_RESOURCE_LOG_CLIENT)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							newButton(	"Log Res Server",
										button->widget.x + button->widget.width + UI_HSTEP + 20,
										100,
										mmDebugShowLog,
										U32_TO_PTR(MM_TOGGLE_RESOURCE_LOG_SERVER)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	"<",
												UI_HSTEP,
												125,
												mmDebugShowLog,
												U32_TO_PTR(MM_LEFT_PREV)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	"<",
												UI_HSTEP,
												125,
												mmDebugShowLog,
												U32_TO_PTR(MM_LEFT_PREV)));
														
		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	">",
												button->widget.x +
													button->widget.width + UI_HSTEP,
												125,
												mmDebugShowLog,
												U32_TO_PTR(MM_LEFT_NEXT)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	"<",
												button->widget.x +
													button->widget.width + UI_HSTEP * 4,
												125,
												mmDebugShowLog,
												U32_TO_PTR(MM_BOTH_PREV)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	">",
												button->widget.x +
													button->widget.width + UI_HSTEP,
												125,
												mmDebugShowLog,
												U32_TO_PTR(MM_BOTH_NEXT)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	"<",
												button->widget.x +
													button->widget.width + UI_HSTEP * 4,
												125,
												mmDebugShowLog,
												U32_TO_PTR(MM_RIGHT_PREV)));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							button = newButton(	">",
												button->widget.x +
													button->widget.width + UI_HSTEP,
												125,
												mmDebugShowLog,
												U32_TO_PTR(MM_RIGHT_NEXT)));

		button = newButton("Toggle Viewer", UI_HSTEP, 150, NULL, NULL);
		ui_ButtonSetCommand(button, "mmCompareLogsToggle");
		ui_WindowAddChild(mmDebugOptionUI.window, button);

		ui_WindowAddChild(	mmDebugOptionUI.window,
							newButton(	"Log Skeleton",
										button->widget.x + button->widget.width + UI_HSTEP,
										150,
										mmDebugSkeletonButtonPressed,
										NULL));

		ui_WindowAddChild(	mmDebugOptionUI.window,
							newButton(	"Refresh Available",
										UI_HSTEP,
										175,
										mmDebugRefreshAvailable,
										NULL));
	}
	else
	{
		ARRAY_FOREACH_BEGIN(mmDebugOptionUI.recordButton, i);
		{
			ui_ButtonSetText(mmDebugOptionUI.recordButton[i], recordButtonText[i]);
		}
		ARRAY_FOREACH_END;
	}

	if (!ui_WindowIsVisible(mmDebugOptionUI.window))
		ui_WindowShow(mmDebugOptionUI.window);
}

static void mmCompareSetOpacity(S32 level){
	S32 alpha;
	
	compareUI.opacityLevel = MINMAX(level, 0, 3);
	
	alpha = 255 - compareUI.opacityLevel * 255 / 3;

	ui_SkinSetBackground(	compareUI.windowSkin,
							CreateColor(148, 148, 148, alpha));

	ui_SkinSetEntry(compareUI.windowSkin,
					CreateColor(178, 178, 178, alpha));
}

static void mmCompareToggleOpacity(	UIButton* button,
									void* unused)
{
	mmCompareSetOpacity((compareUI.opacityLevel + 1) % 4);
}

AUTO_COMMAND ACMD_NAME(mmCompareLogsToggle);
void mmCmdCompareLogsToggle(void){
	mmCompareCreateSkins();

	if(!compareUI.window){
		compareUI.window = ui_WindowCreate("Movement Log Viewer", 0, 0, 1200, 800);
		
		ui_WindowSetDimensions(compareUI.window, 1200, 800, 100, 100);
		
		compareUI.windowSkin = ui_SkinCreate(NULL);
		
		mmCompareSetOpacity(0);

		ui_WidgetSkin(	(UIWidget*)compareUI.window,
						compareUI.windowSkin);
		
		compareUI.toggleOpacity = newButton("",
											0,
											0,
											mmCompareToggleOpacity,
											NULL);
														
		ui_ButtonSetTextAndResize(	compareUI.toggleOpacity,
									"Opacity");
									
		ui_WidgetSetPositionEx(	(UIWidget*)compareUI.toggleOpacity,
								0,
								0,
								0,
								0,
								UITop);
								
		ui_WindowAddChild(	compareUI.window,
							compareUI.toggleOpacity);

		// Create the button to toggle section type.
		
		compareUI.sortType = newButton(	mmCompareUISectionTypeName(compareUI.sectionType),
										0,
										0,
										mmSortTypeButtonPressed,
										NULL);
												
		ui_WidgetSetPositionEx((UIWidget*)compareUI.sortType, 0, 30, 0, 0, UITop);
		ui_WindowAddChild(compareUI.window, compareUI.sortType);

		// Text entries.
		
		ARRAY_FOREACH_BEGIN(compareUI.textEntry, i);
		{
			compareUI.textEntry[i] = ui_TextEntryCreate("", 0, 0);
			
			ui_WidgetSkin(	(UIWidget*)compareUI.textEntry[i],
							compareUI.windowSkin);

			ui_WindowAddChild(compareUI.window, compareUI.textEntry[i]);

			ui_WidgetSetDimensionsEx(	(UIWidget*)compareUI.textEntry[i],
										0.35f, 0.05f,
										UIUnitPercentage, UIUnitPercentage);
										
			ARRAY_FOREACH_BEGIN(compareUI.textEntryButton[i], j);
			{
				compareUI.textEntryButton[i][j] = newButton(j ? "next" : "prev",
															20,
															20,
															mmCompareButtonPressed,
															NULL);

				ui_WindowAddChild(compareUI.window, compareUI.textEntryButton[i][j]);

				ui_WidgetSetPositionEx(	(UIWidget *)compareUI.textEntryButton[i][j],
										((i * -2) + 1) * (130 + j * 90),
										0,
										0,
										0,
										UITop);
			}
			ARRAY_FOREACH_END;
		}
		ARRAY_FOREACH_END;

		ui_WidgetSetPositionEx((UIWidget *)compareUI.textEntry[1], 0, 0, 0, 0, UITopRight);

		// Text areas.

		ARRAY_FOREACH_BEGIN(compareUI.textArea, i);
		{
			compareUI.textArea[i] = ui_TextAreaCreate("");

			ui_WidgetSkin(	(UIWidget*)compareUI.textArea[i],
							compareUI.windowSkin);

			ui_WidgetSetDimensionsEx(	(UIWidget*)compareUI.textArea[i],
										0.42f,
										0.94f,
										UIUnitPercentage,
										UIUnitPercentage);
										
			ui_WindowAddChild(compareUI.window, compareUI.textArea[i]);
		}
		ARRAY_FOREACH_END;

		ui_WidgetSetPositionEx((UIWidget *)compareUI.textArea[0], 0, 0, 0, 0, UIBottomLeft);
		ui_WidgetSetPositionEx((UIWidget *)compareUI.textArea[1], 0, 0, 0, 0, UIBottomRight);

		// Buttons.

		ARRAY_FOREACH_BEGIN(compareUI.button, i);
		{
			compareUI.button[i][0] = newButton("|<", 0, 0, mmCompareButtonPressed, NULL);
			compareUI.button[i][1] = newButton("< 10", 0, 0, mmCompareButtonPressed, NULL);
			compareUI.button[i][2] = newButton("<", 0, 0, mmCompareButtonPressed, NULL);
			compareUI.button[i][3] = newButton(">", 0, 0, mmCompareButtonPressed, NULL);
			compareUI.button[i][4] = newButton("> 10", 0, 0, mmCompareButtonPressed, NULL);
			compareUI.button[i][5] = newButton(">|", 0, 0, mmCompareButtonPressed, NULL);

			ARRAY_FOREACH_BEGIN(compareUI.button[i], j);
			{
				ui_WindowAddChild(compareUI.window, compareUI.button[i][j]);

				ui_WidgetSetDimensionsEx(	(UIWidget*)compareUI.button[i][j],
											0.08f / 2, 20,
											UIUnitPercentage, UIUnitFixed);

				ui_WidgetSetPositionEx(	(UIWidget*)compareUI.button[i][j],
										0, 55 + j * 22,
										((i * -2) + 1) * 0.08f / 2, 0,
										UITop);
			}
			ARRAY_FOREACH_END;
		}
		ARRAY_FOREACH_END;

		ARRAY_FOREACH_BEGIN(compareUI.commonButton, i);
		{
			UIButton* button;
			
			compareUI.commonButton[i] = newButton(	i ? "> both >" : "< both <",
													0,
													0,
													mmCompareButtonPressed,
													NULL);
			
			button = compareUI.commonButton[i];

			ui_WindowAddChild(compareUI.window, button);
			
			ui_WidgetSetDimensionsEx(	(UIWidget*)button,
										0.08f, 20,
										UIUnitPercentage, UIUnitFixed);

			ui_WidgetSetPositionEx(	(UIWidget*)button,
									0, 55 + (i + 6) * 22,
									0, 0,
									UITop);
		}
		ARRAY_FOREACH_END;
		
		ARRAY_FOREACH_BEGIN(compareUI.wordWrapButton, i);
		{
			UIButton* button;
			
			compareUI.wordWrapEnabled[i] = 1;
			
			compareUI.wordWrapButton[i] = newButton("wrap",
													0,
													0,
													mmCompareButtonPressed,
													NULL);
															
			button = compareUI.wordWrapButton[i];

			buttonSetEnabledColor(button, 1);

			ui_WindowAddChild(compareUI.window, button);
						
			ui_WidgetSetDimensionsEx(	(UIWidget*)button,
										0.038f, 20,
										UIUnitPercentage, UIUnitFixed);

			ui_WidgetSetPositionEx(	(UIWidget*)button,
									0, 55,
									0.42f, 0,
									i ? UITopRight : UITopLeft);
		}
		ARRAY_FOREACH_END;

		ARRAY_FOREACH_BEGIN(compareUI.threadFilterButton, i);
		{
			UIButton* button;
			
			compareUI.threadFilter[i] = 2;
			
			compareUI.threadFilterButton[i] = newButton("FG+BG",
														0,
														0,
														mmCompareButtonPressed,
														NULL);
															
			button = compareUI.threadFilterButton[i];

			ui_WindowAddChild(compareUI.window, button);
						
			ui_WidgetSetDimensionsEx(	(UIWidget*)button,
										0.038f, 20,
										UIUnitPercentage, UIUnitFixed);

			ui_WidgetSetPositionEx(	(UIWidget*)button,
									0, 55 + 22,
									0.42f, 0,
									i ? UITopRight : UITopLeft);
		}
		ARRAY_FOREACH_END;

		ARRAY_FOREACH_BEGIN(compareUI.toggle3DButton, i);
		{
			UIButton* button;
			
			compareUI.toggle3DButton[i] = newButton("3D",
													0,
													0,
													mmCompareButtonPressed,
													NULL);
															
			button = compareUI.toggle3DButton[i];

			buttonSetEnabledColor(button, 1);

			ui_WindowAddChild(compareUI.window, button);
						
			ui_WidgetSetDimensionsEx(	(UIWidget*)button,
										0.038f, 20,
										UIUnitPercentage, UIUnitFixed);

			ui_WidgetSetPositionEx(	(UIWidget*)button,
									0, 55 + 22 * 2,
									0.42f, 0,
									i ? UITopRight : UITopLeft);
		}
		ARRAY_FOREACH_END;

		ARRAY_FOREACH_BEGIN(compareUI.cameraButton, i);
		{
			UIButton* button;
			
			compareUI.activeCamera = -1;
			
			compareUI.cameraButton[i] = newButton(	"cam",
													0,
													0,
													mmCompareButtonPressed,
													NULL);
															
			button = compareUI.cameraButton[i];

			buttonSetEnabledColor(button, 0);

			ui_WindowAddChild(compareUI.window, button);
						
			ui_WidgetSetDimensionsEx(	(UIWidget*)button,
										0.038f, 20,
										UIUnitPercentage, UIUnitFixed);

			ui_WidgetSetPositionEx(	(UIWidget*)button,
									0, 55 + 22 * 3,
									0.42f, 0,
									i ? UITopRight : UITopLeft);
		}
		ARRAY_FOREACH_END;

		ARRAY_FOREACH_BEGIN(compareUI.tagsButton, i);
		{
			UIButton* button;
			
			compareUI.tagsButton[i] = newButton("tags",
												0,
												0,
												mmCompareButtonPressed,
												NULL);
															
			button = compareUI.tagsButton[i];

			buttonSetEnabledColor(button, 0);

			ui_WindowAddChild(compareUI.window, button);
						
			ui_WidgetSetDimensionsEx(	(UIWidget*)button,
										0.038f, 20,
										UIUnitPercentage, UIUnitFixed);

			ui_WidgetSetPositionEx(	(UIWidget*)button,
									0, 55 + 22 * 4,
									0.42f, 0,
									i ? UITopRight : UITopLeft);
		}
		ARRAY_FOREACH_END;

		ARRAY_FOREACH_BEGIN(compareUI.hideButton, i);
		{
			UIButton* button;
			
			compareUI.hideButton[i] = newButton("hidden",
												0,
												0,
												mmCompareButtonPressed,
												NULL);
															
			button = compareUI.hideButton[i];

			buttonSetEnabledColor(button, 1);

			ui_WindowAddChild(compareUI.window, button);
						
			ui_WidgetSetDimensionsEx(	(UIWidget*)button,
										0.038f, 20,
										UIUnitPercentage, UIUnitFixed);

			ui_WidgetSetPositionEx(	(UIWidget*)button,
									0, 55 + 22 * 5,
									0.42f, 0,
									i ? UITopRight : UITopLeft);
		}
		ARRAY_FOREACH_END;
	}

	if(ui_WindowIsVisible(compareUI.window)){
		ui_WindowHide(compareUI.window);
	}else{
		ui_WindowShow(compareUI.window);
	}
}

static void mmCmdCompareLogsSetEntInternal( S32 useLocalLog,
											S32 useRightPane,
											const char* module,
											EntityRef erTarget)
{
	useRightPane = !!useRightPane;

	mmCmdCompareLogsToggle();
	ui_WindowShow(compareUI.window);

	mmLogDestroy(&compareUI.log[useRightPane]);

	mmLogGetCopy(	erTarget,
					&compareUI.log[useRightPane],
					useLocalLog,
					module);

	mmCompareLogsReparseSections(useRightPane);

	mmCompareSetSectionIndex(useRightPane, 0);
}

AUTO_COMMAND ACMD_NAME(mmCompareLogsSetEnt);
void mmCmdCompareLogsSetEnt(Entity* e,
							S32 useLocalLog,
							S32 useRightPane,
							const char* module,
							ACMD_SENTENCE target)
{
	EntityRef	erTarget;
	Entity*		eTarget = entGetClientTarget(e, target, &erTarget);
	
	mmCmdCompareLogsSetEntInternal(useLocalLog, useRightPane, module, erTarget);
}

AUTO_COMMAND ACMD_NAME(mmDebugLogSave);
void mmCmdDebugLogSave(	S32 useRightPane,
						const char* fileName)
{
	MovementLog*	log = compareUI.log[useRightPane];
	char			fullPath[CRYPTIC_MAX_PATH];
	
	sprintf(fullPath, "c:\\CrypticSettings\\%s.mmlog", fileName);

	if(!log){
		printf(	"Can't save from %s pane (nothing is there): %s\n",
				useRightPane ? "right" : "left",
				fullPath);
		return;				
	}

	if(ParserWriteTextFile(	fullPath,
							parse_MovementLog,
							log,
							0, 0))
	{
		printf(	"Saved movement log from %s pane: %s\n",
				useRightPane ? "right" : "left",
				fullPath);
	}else{
		printf(	"Failed to save movement log from %s pane: %s\n",
				useRightPane ? "right" : "left",
				fullPath);
	}
}

static void mmAfterLoadedLogFile(S32 useRightPane){
	if(!compareUI.window){
		mmCmdCompareLogsToggle();
	}
	else if(!ui_WindowIsVisible(compareUI.window)){
		ui_WindowShow(compareUI.window);
	}

	mmCompareLogsReparseSections(useRightPane);
	
	mmCompareSetSectionIndex(useRightPane, 0);
}

AUTO_COMMAND ACMD_NAME(mmDebugLogLoad);
void mmCmdDebugLogLoad(	S32 useRightPane,
						const char* fileName)
{
	char		fullPath[CRYPTIC_MAX_PATH];
	const char* fileExt = ".mmlog";
	
	if(fileIsAbsolutePath(fileName)){
		strcpy(fullPath, fileName);
	}else{
		sprintf(fullPath, "c:\\CrypticSettings\\%s", fileName);
	}

	if(!strEndsWith(fullPath, fileExt)){
		strcat(fullPath, fileExt);
	}

	if(!fileExists(fullPath)){
		printf(	"File doesn't exist: %s\n",
				fullPath);
	}else{
		MovementLog** log = &compareUI.log[useRightPane];
		
		if(!*log){
			*log = StructCreate(parse_MovementLog);
		}else{
			StructDeInit(parse_MovementLog, *log);
		}
		
		if(!ParserReadTextFile(	fullPath,
								parse_MovementLog,
								*log,
								0))
		{
			printf(	"Failed to load movement log into %s pane: %s\n",
					useRightPane ? "right" : "left",
					fullPath);
		}else{
			mmAfterLoadedLogFile(useRightPane);

			printf(	"Loaded movement log into %s pane: %s\n",
					useRightPane ? "right" : "left",
					fullPath);
		}
	}
}

AUTO_COMMAND ACMD_NAME(mmDebugLogLoadEscaped);
void mmCmdDebugLogLoadEscaped(	S32 useRightPane,
								const char* fileName)
{
	char	fullPath[CRYPTIC_MAX_PATH];
	char*	fileData;
	U32		fileDataSize;
	
	sprintf(fullPath, "c:\\CrypticSettings\\%s", fileName);

	if(!fileExists(fullPath)){
		printf(	"File doesn't exist: %s\n",
				fullPath);
	}
	else if(!(fileData = fileAlloc(fullPath, &fileDataSize))){
		printf(	"File can't be read: %s\n",
				fullPath);
	}else{
		MovementLog**	log = &compareUI.log[useRightPane];
		char*			estrFile = NULL;
		char*			context = NULL;
		char*			token;
		
		estrAppendUnescapedCount(&estrFile, fileData, fileDataSize);
		SAFE_FREE(fileData);
		
		if(!*log){
			*log = StructCreate(parse_MovementLog);
		}else{
			StructDeInit(parse_MovementLog, *log);
		}
		
		for(token = strtok_s(estrFile, "\n", &context);
			token;
			token = strtok_s(NULL, "\n", &context))
		{
			MovementLogLine* line = StructAlloc(parse_MovementLogLine);
			estrCopy2(&line->text, token);
			eaPush(&(*log)->lines, line);
		}
		
		estrDestroy(&estrFile);

		mmAfterLoadedLogFile(useRightPane);

		printf(	"Loaded escaped movement log into %s pane: %s\n",
				useRightPane ? "right" : "left",
				fullPath);
	}
}

static void mmDrawOffsetArray(	S32* offsets,
								S32 offsetCount,
								S32 findMin,
								S32 xOffset,
								S32 colorShift,
								S32 scale)
{
	S32	minOffset = 0;
	S32	w;
	S32	h;

	gfxGetActiveDeviceSize(&w, &h);

	if(findMin){
		minOffset = S32_MAX;
		
		FOR_BEGIN(i, offsetCount);
			S32 offset = offsets[i];
			
			if(offset){
				MIN1(minOffset, offset);
			}
		FOR_END;
	}

	FOR_BEGIN(i, offsetCount);
		S32 offset = offsets[i];
		
		if(offset){
			S32 height = offset - minOffset + 1;
			
			gfxDrawQuadARGB(w - scale - (i + xOffset) * scale,
							h - height * scale,
							w - (i + xOffset) * scale,
							h,
							2000,
							0x80000000 | ((191 + (i % scale) * 16) << colorShift));
		}
	FOR_END;
}

static void mmDrawOffsetGraph(void){
	S32		offsets[100];
	S32		offsetCount;
	
	mmGetOffsetHistoryClientToServerSync(offsets, ARRAY_SIZE(offsets), &offsetCount);

	mmDrawOffsetArray(offsets, offsetCount, 1, 0, 8, 4);

	mmGetOffsetHistoryServerSyncToServer(offsets, ARRAY_SIZE(offsets), &offsetCount);

	mmDrawOffsetArray(offsets, offsetCount, 0, 100, 16, 4);
}

static void mmDrawClientStatsFrames(void){
	const MovementClientStatsFrames*	frames;

	S32		scale = 2;
	S32		w;
	S32		h;
	S32		wBox;
	S32		hBox = 50 * scale;
	F32		z = 2000.f;
	U32		alpha;
	S32		yBoxTop;
	S32		yBoxBottom;
	S32		yBoxMiddle;
	U32		maxCount;
	U32		shownCount;

	if(!mmGetClientStatsFrames(&frames)){
		return;
	}
	
	gfxGetActiveDeviceSize(&w, &h);
	
	maxCount = (w - 20) / scale;
	shownCount = MIN(maxCount, beaUSize(&frames->frames));

	wBox = shownCount * scale;
	
	netTimingGraphAlpha = MINMAX(netTimingGraphAlpha, 64, 0xff);
	alpha = netTimingGraphAlpha << 24;

	yBoxTop = h - hBox;
	yBoxBottom = yBoxTop + hBox;
	yBoxMiddle = yBoxTop + hBox / 2;
	
	gfxDrawQuadARGB(w / 2 - wBox / 2 - 10,
					yBoxTop,
					w / 2 + wBox / 2 + 10,
					yBoxBottom,
					z,
					alpha);

	FOR_BEGIN(i, (S32)shownCount);
	{
		const MovementClientStatsFrame*	f;
		S32								x = w / 2 - wBox / 2 + i * scale;
		S32								yUp = yBoxMiddle;
		S32								yDown = yUp;
		S32								odd = (i + frames->count) & 1;
		S32								hasCorrection;
		
		f = frames->frames +
			(	(i + frames->count - shownCount + beaSize(&frames->frames)) %
				beaSize(&frames->frames));
				
		hasCorrection = f->forcedSteps ||
						f->skipSteps ||
						f->consolidateStepsEarly ||
						f->consolidateStepsLate;
		
		if(f->flags.isCorrectionFrame){
			U32 argbMiddle = 0xff444444;
			U32 argbTopBottom = argbMiddle & 0xffffff;

			gfxDrawQuad4ARGB(	x,
								yBoxTop + 10,
								x + scale,
								yBoxMiddle,
								z,
								argbTopBottom,
								argbTopBottom,
								argbMiddle,
								argbMiddle);

			if(hasCorrection){
				gfxDrawQuad4ARGB(	x,
									yBoxMiddle,
									x + scale,
									yBoxBottom - 10,
									z,
									argbMiddle,
									argbMiddle,
									argbTopBottom,
									argbTopBottom);
			}
		}
		
		#define DRAW_UP_BAR(y, rgbOdd, rgbEven){					\
			if(y){													\
				S32 yDelta = y * scale;								\
				gfxDrawQuadARGB(x,									\
								yUp - yDelta,						\
								x + scale,							\
								yUp,								\
								z,									\
								alpha | (odd ? rgbOdd : rgbEven));	\
				yUp -= yDelta;										\
			}														\
		}((void)0)
		
		DRAW_UP_BAR(f->serverStepCount, 0x00ff00, 0x00dd00);
		DRAW_UP_BAR(f->usedSteps, 0x3333ff, 0x2222dd);
		DRAW_UP_BAR(f->leftOverSteps, 0xff0000, 0xdd0000);
		
		#undef DRAW_UP_BAR
		
		#define DRAW_DOWN_BAR(y, rgbOdd, rgbEven){					\
			if(y){													\
				S32 yDelta = y * scale;								\
				gfxDrawQuadARGB(x,									\
								yDown,								\
								x + scale,							\
								yDown + yDelta,						\
								z,									\
								alpha | (odd ? rgbOdd : rgbEven));	\
				yDown += yDelta;									\
			}														\
		}

		DRAW_DOWN_BAR(f->behind / 2, 0xffff00, 0xdddd00);
		DRAW_DOWN_BAR(f->forcedSteps, 0xff8000, 0xdd7000);
		DRAW_DOWN_BAR(f->skipSteps, 0xff0000, 0xdd0000);
		DRAW_DOWN_BAR(f->consolidateStepsEarly, 0xff33ff, 0xdd22dd);
		DRAW_DOWN_BAR(f->consolidateStepsLate, 0x33ffff, 0x22dddd);
		
		#undef DRAW_DOWN_BAR
	}
	FOR_END;

	FOR_BEGIN(i, (S32)shownCount + 1);
	{
		S32 x = w / 2 - wBox / 2 + i * scale;
		
		if(!(i % 5)){
			gfxDrawQuadARGB(x,
							yBoxTop + 10,
							x + 1,
							yBoxBottom - 10,
							z,
							0x08ffffff);
		}
	}
	FOR_END;
	
	FOR_BEGIN(i, hBox / (2 * scale * 5));
	{
		S32 yOffset = i * scale * 5;
		
		if(yOffset > hBox / 2 - 10){
			break;
		}
		
		gfxDrawQuadARGB(w / 2 - wBox / 2,
						yBoxMiddle - yOffset,
						w / 2 + wBox / 2,
						yBoxMiddle - yOffset + 1,
						z,
						0x08ffffff);

		if(i){
			gfxDrawQuadARGB(w / 2 - wBox / 2,
							yBoxMiddle + yOffset,
							w / 2 + wBox / 2,
							yBoxMiddle + yOffset + 1,
							z,
							0x08ffffff);
		}
	}
	FOR_END;
}

static void drawStatsPackets(	const MovementClientStatsPacketArray* packets,
								S32 xLeft,
								S32 xWidth,
								S32 yBoxOffsetFromTop,
								S32 scale)
{
	S32	w;
	S32	h;
	S32	hBox = 50 * scale;
	F32	z = 10000.f;
	U32	alpha;
	S32 xRight;
	S32	yBoxTop;
	S32	yBoxBottom;
	S32	yBoxMiddle;
	U32 maxCount;
	U32 shownCount;
	
	if(xWidth <= 0){
		return;
	}

	gfxGetActiveDeviceSize(&w, &h);
	
	maxCount = xWidth / scale;
	shownCount = MIN(maxCount, beaUSize(&packets->packets));

	xWidth += scale - 1;
	xWidth /= scale;
	xWidth *= scale;

	netTimingGraphAlpha = MINMAX(netTimingGraphAlpha, 64, 0xff);
	alpha = netTimingGraphAlpha << 24;
	
	xRight = xLeft + xWidth;
	yBoxTop = yBoxOffsetFromTop;
	yBoxBottom = yBoxTop + hBox;
	yBoxMiddle = yBoxTop + hBox / 2;

	gfxDrawQuadARGB(xLeft - 10,
					yBoxTop,
					xRight + 10,
					yBoxBottom,
					z,
					alpha);

	FOR_BEGIN(i, (S32)shownCount);
	{
		const MovementClientStatsPacket*	p;
		S32									x = xLeft + i * scale;
		S32									yUp = yBoxMiddle;
		S32									yDown = yUp;
		S32									odd = (i + packets->count) & 1;
		
		p = packets->packets +
			(	(i + packets->count - shownCount + beaSize(&packets->packets)) %
				beaSize(&packets->packets));
		
		if(p->size){
			U32 sizeMin = 0;
			U32 scaleMin = 0;
			U32 scaleMax = 1000;
			U32 rgb = (	p->flags.notMovementPacket ?
							(odd ? 0x3333ff : 0x2222dd) :
							(odd ? 0x33ff33 : 0x22dd22));
			
			while(scaleMax > scaleMin){
				U32 size;
				U32 partial;
				
				if(p->size > scaleMax){
					scaleMin = scaleMax;
					scaleMax *= 10;
					sizeMin += scale * 5;
					continue;
				}
				
				size = scale * 5 * (p->size - scaleMin) / (scaleMax - scaleMin);
				
				gfxDrawQuadARGB(x,
								yUp - (sizeMin + size),
								x + scale,
								yUp,
								z,
								alpha | rgb);
									
				yUp -= sizeMin + size;
				
				partial = (p->size - scaleMin) % (scaleMax - scaleMin);
				partial = 255 * partial / (scaleMax - scaleMin);
				
				if(partial){
					gfxDrawQuadARGB(x,
									yUp - 1,
									x + scale,
									yUp,
									z,
									(partial << 24) | rgb);
										
					yUp -= 1;
				}

				break;
			}
		}
		
		// Draw the down part.
		
		if(p->msOffsetFromExpectedTime){
			U32 yDelta = abs(p->msOffsetFromExpectedTime) / 10;

			gfxDrawQuadARGB(x,
							yDown,
							x + scale,
							yDown + yDelta,
							z,
							alpha |
								(	p->msOffsetFromExpectedTime < 0 ?
										(odd ? 0xff3333 : 0xdd2222) :
										(odd ? 0x3333ff : 0x2222dd)));

			yDown += yDelta;
		}

		if(p->msLocalOffsetFromLastPacket){
			U32 msToDraw;
			U32 yDelta;
			
			if(	p->msOffsetFromExpectedTime > 0 &&
				p->msLocalOffsetFromLastPacket > (U32)p->msOffsetFromExpectedTime)
			{
				msToDraw = p->msLocalOffsetFromLastPacket - p->msOffsetFromExpectedTime;
			}else{
				msToDraw = p->msLocalOffsetFromLastPacket;
			}
			
			yDelta = msToDraw / 10;
			
			gfxDrawQuadARGB(x,
							yDown,
							x + scale,
							yDown + yDelta,
							z,
							alpha | (odd ? 0x33ffff : 0x22dddd));

			yDown += yDelta;
		}

		if(p->spcOffset){
			gfxDrawQuadARGB(x,
							yDown,
							x + scale,
							yDown + abs(p->spcOffset),
							z,
							alpha |
								(	p->spcOffset < 0 ?
										(odd ? 0xff33ff : 0xdd22dd) :
										(odd ? 0xffff33 : 0xdddd22)));

			yDown += abs(p->spcOffset);
		}
	}
	FOR_END;

	FOR_BEGIN(i, (S32)shownCount + 1);
	{
		S32 x = xLeft + i * scale;
		
		if(!(i % 5)){
			gfxDrawQuadARGB(x,
							yBoxTop + 10,
							x + 1,
							yBoxBottom - 10,
							z,
							0x08ffffff);
		}
	}
	FOR_END;
	
	FOR_BEGIN(i, hBox / (2 * scale * 5));
	{
		S32 yOffset = i * scale * 5;
		
		if(yOffset > hBox / 2 - 10){
			break;
		}
		
		gfxDrawQuadARGB(xLeft,
						yBoxMiddle - yOffset,
						xRight,
						yBoxMiddle - yOffset + 1,
						z,
						0x08ffffff);

		if(i){
			gfxDrawQuadARGB(xLeft,
							yBoxMiddle + yOffset,
							xRight,
							yBoxMiddle + yOffset + 1,
							z,
							0x08ffffff);
		}
	}
	FOR_END;
}

static void mmDrawClientStatsPackets(void){
	const MovementClientStatsPacketArray* packets;
	S32	w;
	S32	h;

	gfxGetActiveDeviceSize(&w, &h);


	if(mmGetClientStatsPacketsFromClient(&packets)){
		S32 x = w / 2 + 15;
		drawStatsPackets(packets, x, w - x - 15, 0, 2);
	}
	
	if(mmGetClientStatsPacketsFromServer(&packets)){
		drawStatsPackets(packets, 15, w / 2 - 30, 0, 2);
	}
}

static void mmDrawNetOffsetFromEnd(void){
	F32 normal;
	F32 fast;
	F32 lag;
	S32 w;
	S32	h;
	F32 x = 0.f;
	F32 clamp = 1.f;
	F32 xScale = 10.f;
	
	mmGetNetOffsetsFromEnd(&normal, &fast, &lag);

	gfxGetActiveDeviceSize(&w, &h);
	
	FOR_BEGIN(i, 3);
		F32 remain = 0;
		S32 odd = 0;
		
		switch(i){
			xcase 0: remain = lag;
			xcase 1: remain = normal;
			xcase 2: remain = fast;
		}
		
		while(remain > 0.f){
			U32 argb = 0;
			F32 cur;
			
			if(remain > clamp){
				remain -= clamp;
				cur = clamp;
				clamp = 1.f;
			}else{
				cur = remain;
				remain = 0.f;
				clamp -= cur;
			}
			
			switch(i){
				xcase 0: argb = odd ? 0x8044ff44 : 0x8033dd33;
				xcase 1: argb = odd ? 0x804444ff : 0x803333dd;
				xcase 2: argb = odd ? 0x80ff4444 : 0x80dd3333;
			}

			gfxDrawQuadARGB(w - (x + cur) * xScale,
							h * 0.75,
							w - x * xScale,
							h * 0.75 + 50,
							2000,
							argb);

			x += cur;
			odd = !odd;
		}
	FOR_END;	
}

static void mmDrawServerPos(void){
	EntityIterator* iter = entGetIteratorAllTypesAllPartitions(0, 0);
	Entity*			e;

	while(e = EntityIteratorGetNext(iter)){
		Vec3 a;
		
		if(mmGetDebugLatestServerPositionFG(e->mm.movement, a)){
			Vec3 b;
			
			copyVec3(a, b);
			b[1] += 10;
			
			gfxDrawLine3D_2(a,
							b,
							ColorWhite,
							ColorMagenta);
							
			entGetPos(e, b);

			gfxDrawLine3D_2(b,
							a,
							ColorWhite,
							ColorMagenta);
		}
	}

	EntityIteratorRelease(iter);
}

void mmDrawCapsules(void){
	EntityIterator* iter;
	Entity*			e;

	F32 afColorCapsule[] = {1.0f,0.0f,1.0f,0.5f};
	F32 afColorOther[] = {0.5f,0.0f,1.0f,0.5f};

	iter = entGetIteratorAllTypesAllPartitions(0, 0);

	while(e = EntityIteratorGetNext(iter)){		
		Vec3					pos;		
		Quat					rot;
		Vec3 pyr;
		const Capsule*const*	capsules;

		if(!e->mm.movement){
			continue;
		}

		entGetPos(e, pos);

		if (!mmDoesCapsuleOrientationUseRotation(e->mm.movement))
		{
			entGetFacePY(e, pyr);

			// Ignore pitch if not flying
			if (e->pChar && e->pChar->pattrBasic->fFlight <= 0.f)
			{
				pyr[0] = 0.f;
			}		

			// No roll
			pyr[2] = 0.f;

			PYRToQuat(pyr, rot);
		}
		else
		{
			entGetRot(e, rot);
		}
		

		if(mmGetCapsules(e->mm.movement, &capsules))
		{
			EARRAY_CONST_FOREACH_BEGIN(capsules, i, isize);
			{
				const Capsule*	cap = capsules[i];
				Vec3			vPyr;
				Mat4			matrix;
				Quat			localrot;
				Quat			finalrot,temprot;
				Vec3			temp;

				orientYPR(vPyr,cap->vDir);

				PYRToQuat(vPyr,localrot);
				quatMultiply(localrot,rot,temprot);

				//axis[0] = 1.0f;
				//axis[1] = 0.0f;
				//axis[2] = 0.0f;
				//axisAngleToQuat(axis,-0.5f*PI,localrot);

				localrot[0] = -sqrtf(2.0f)*0.5f;
				localrot[1] = 0.0f;
				localrot[2] = 0.0f;
				localrot[3] = -localrot[0];

				quatMultiply(localrot,temprot,finalrot);

				quatVecToMat4(finalrot,pos,matrix);

				quatRotateVec3(rot, cap->vStart, temp);
				addVec3(matrix[3],temp,matrix[3]);

				if(mmIsCollisionEnabled(e->mm.movement))
					gfxQueueCapsule(0,cap->fLength,cap->fRadius, matrix, 1, ROC_EDITOR_ONLY, 0, afColorCapsule, false, 0);
				else
					gfxQueueCapsule(0,cap->fLength,cap->fRadius, matrix, 1, ROC_EDITOR_ONLY, 0, afColorOther, false, 0);
			}
			EARRAY_FOREACH_END;
		}
	}
	
	EntityIteratorRelease(iter);
}

void mmDrawProjectiles(void){
	EntityIterator* iter;
	Entity*			e;

	F32 afColorCapsule[] = {1.0f,0.0f,1.0f,0.5f};
	F32 afColorOther[] = {0.5f,0.0f,1.0f,0.5f};

	iter = entGetIteratorAllTypesAllPartitions(ENTITYFLAG_PROJECTILE, 0);

	if (mmDrawProjectilesEnabled > 3)
		mmDrawProjectilesEnabled = 3;

	while(e = EntityIteratorGetNext(iter)){
		const Capsule*const*	capsules;

		//ProjectileEntity *pProj;
		//ProjectileEntityDef *pDef;
		if(!e->mm.movement){
			continue;
		}
		
		if(mmGetCapsules(e->mm.movement, &capsules))
		{
			Vec3 vPos;
			Mat4 mtx;
			entGetPos(e, vPos);
			identityMat4(mtx);

			EARRAY_CONST_FOREACH_BEGIN(capsules, i, isize);
			{
				const Capsule*	cap = capsules[i];

				if (cap->iType == 0 && mmDrawProjectilesEnabled & 1)
				{
					F32 afColor[] = {0.0f,0.0f,1.0f,0.5f};
					
					gfxQueueSphere(vPos, cap->fRadius, mtx, 1, ROC_EDITOR_ONLY, 0, afColor, 0);
				}
				else if (cap->iType == 1 && mmDrawProjectilesEnabled & 2)
				{
					F32 afColor[] = {0.5f,0.0f,0.0f,0.5f};

					gfxQueueSphere(vPos, cap->fRadius, mtx, 1, ROC_EDITOR_ONLY, 0, afColor, 0);
				}
			}			
			EARRAY_FOREACH_END;

		}
	}
	
	EntityIteratorRelease(iter);

}

void mmDrawCylinders(void){
	EntityIterator* iter;
	Entity*			e;
	Color			colorCylinder;

	setColorFromRGBA(&colorCylinder, 0xff0080ff);

	iter = entGetIteratorAllTypesAllPartitions(0, 0);

	while(e = EntityIteratorGetNext(iter)){
		Vec3 pos, pos2;

		if(!e->mm.movement){
			continue;
		}

		entGetPos(e, pos);
		copyVec3(pos,pos2);
		pos2[1] += 6;

		gfxDrawCylinder3D(pos, pos2, 1, 20, true, colorCylinder, 1);
	}

	EntityIteratorRelease(iter);
}

void mmDrawCombat(void){
	EntityIterator* iter;
	Entity*			e;

	iter = entGetIteratorAllTypesAllPartitions(0, 0);

	while(e = EntityIteratorGetNext(iter)){
		int i;
		Vec3 posCombat;
		entGetCombatPosDir(e,NULL,posCombat,NULL);
		for(i=0; i<=2; i++)
		{
			Vec3 posDraw;
			copyVec3(posCombat,posDraw);
			posDraw[i] += 10;
			gfxDrawLine3D_2(posCombat,posDraw,ColorRed,ColorWhite);
		}
	}

	EntityIteratorRelease(iter);
}

typedef struct CoverData
{
	Vec3 vecStart;
	Vec3 vecDir;
	F32 fRadius;
	F32 fLength;
	S32 iQuant;

	int *pRegionsHit;
	int iRegions;
	U32 bHit : 1;
} CoverData;

void coverHandleTriangles(void* coverData, Vec3 (*tris)[3], U32 triCount)
{
	int x,y,i;

	Color colorVert, colorVertInvalid, colorChunkHit;
	CoverData *data = (CoverData*)coverData;
	F32 fRadSqr = SQR(data->fRadius);
	F32 fLength = data->fLength;

	Mat3 mDir, mDirInv;
	
	int iQuant = 1 + 2 * data->iQuant;
	if(!data->iRegions)
	{
		data->iRegions = iQuant*iQuant;
		data->pRegionsHit = calloc(data->iRegions, sizeof(int));
	}

	// Create a mat in the direction of cover
	orientMat3(mDir,data->vecDir);
	invertMat3Copy(mDir,mDirInv);
	
	setColorFromRGBA(&colorVert, 0xffffff80);
	setColorFromRGBA(&colorVertInvalid, 0x00000080);
	setColorFromRGBA(&colorChunkHit, 0x60000060);

	for (; triCount; --triCount, ++tris)
	{
		int v;
		for (v=0; v<3; v++)
		{
			Vec3 vecTo, vecProj, vecToTrans;
			F32 dotVertex;
			F32 fDistSqr;

			subVec3(tris[0][v],data->vecStart,vecTo);
			dotVertex = dotVec3(vecTo,data->vecDir);
			
			if(dotVertex<0)
			{
				// Vertex is behind the cover root
				if(mmDrawCoverEnabled>0)
				{
					scaleAddVec3(data->vecDir,-dotVertex,tris[0][v],vecProj);
					gfxDrawLine3D(tris[0][v],vecProj,colorVertInvalid);
				}
				continue;
			}
			
			scaleAddVec3(data->vecDir,-dotVertex,tris[0][v],vecProj);
			subVec3(vecProj,data->vecStart,vecTo);
			fDistSqr = lengthVec3Squared(vecTo);
			
			if(fDistSqr > fRadSqr)
			{
				// Vertex is outside the cover radius
				if(mmDrawCoverEnabled>0)
				{
					gfxDrawLine3D(tris[0][v],vecProj,colorVertInvalid);
				}
				continue;
			}

			mulVecMat3(vecTo, mDirInv, vecToTrans);

			// Convert into [-1 .. 1] radius units
			vecToTrans[0] /= data->fRadius;
			vecToTrans[1] /= data->fRadius;

			// Convert into [0 .. 2] radius units
			vecToTrans[0] += 1.f;
			vecToTrans[1] += 1.f;

			// Convert into [0 .. iQuant] units
			vecToTrans[0] *= iQuant/2.f;
			vecToTrans[1] *= iQuant/2.f;

			// Floor
			vecToTrans[0] = floor(vecToTrans[0]);
			vecToTrans[1] = floor(vecToTrans[1]);

			i = vecToTrans[0] + iQuant * vecToTrans[1];
			data->pRegionsHit[i]++;
			
			data->bHit = true;

			if(mmDrawCoverEnabled>0)
			{
				gfxDrawLine3D(tris[0][v],vecProj,colorVert);
			}
		}
	}

	if(mmDrawCoverEnabled>0)
	{
		for(y=0; y<iQuant; y++)
		{
			for(x=0; x<iQuant; x++)
			{
				int ox = x - data->iQuant;
				int oy = y - data->iQuant;
				Vec3 t, a, b, c, d;

				setVec3(a,ox-.5f,oy-.5f,0);
				setVec3(b,ox-.5f,oy+.5f,0);
				setVec3(c,ox+.5f,oy+.5f,0);
				setVec3(d,ox+.5f,oy-.5f,0);

				scaleVec3(a,2.f*data->fRadius/(F32)iQuant,a);
				scaleVec3(b,2.f*data->fRadius/(F32)iQuant,b);
				scaleVec3(c,2.f*data->fRadius/(F32)iQuant,c);
				scaleVec3(d,2.f*data->fRadius/(F32)iQuant,d);

				mulVecMat3(a,mDir,t); addVec3(data->vecStart,t,a);
				mulVecMat3(b,mDir,t); addVec3(data->vecStart,t,b);
				mulVecMat3(c,mDir,t); addVec3(data->vecStart,t,c);
				mulVecMat3(d,mDir,t); addVec3(data->vecStart,t,d);

				gfxDrawQuad3D(a,b,c,d,colorVert,2);

				i = x + y*iQuant;
				if(data->pRegionsHit[i])
				{
					gfxDrawQuad3D(a,b,c,d,colorChunkHit,0);
				}
			}
		}
	}
}


void mmDrawCover(void){
	Color			colorCapsule;
	Entity* ePlayer = entActivePlayerPtr();
	Entity* eTarget = entity_GetTarget(ePlayer);

	if(!mmDrawCoverEnabled || !eTarget || !ePlayer->mm.movement || !eTarget->mm.movement){
		return;
	}
	else
	{
		Vec3 vecPlayerPos, vecTargetPos, vecPlayerCoverPos, vecTargetCoverPos;
		Vec3 vecDir;
		Vec3 vecCapEnd;
		Quat rotPlayer, rotTarget;
		const Capsule*const* capsulesPlayer;
		const Capsule*const* capsulesTarget;
		int iHitPlayer = false, iHitTarget = false;
		CoverData coverPlayer = {0}, coverTarget = {0};
		int iPartitionIdx = entGetPartitionIdx(ePlayer);

		PERFINFO_AUTO_START_FUNC();

#define COVER_RADIUS_DEFAULT 2.7
#define COVER_LENGTH_DEFAULT 6


		// Get location/rotation of player
		entGetPos(ePlayer,vecPlayerPos);
		entGetRot(ePlayer,rotPlayer);
		if(mmGetCapsules(ePlayer->mm.movement,&capsulesPlayer))
		{
			// Cover root and radius based on capsule
			CapsuleMidlinePoint(capsulesPlayer[0],vecPlayerPos,rotPlayer,0.6f,vecPlayerCoverPos);
			coverPlayer.fRadius = capsulesPlayer[0]->fRadius + capsulesPlayer[0]->fLength * .4;
		}
		else
		{
			copyVec3(vecPlayerPos,vecPlayerCoverPos);
			coverPlayer.fRadius = COVER_RADIUS_DEFAULT;
		}

		// Get location/rotation of target
		entGetPos(eTarget,vecTargetPos);
		entGetRot(eTarget,rotTarget);
		if(mmGetCapsules(eTarget->mm.movement,&capsulesTarget))
		{
			// Cover root and radius based on capsule
			CapsuleMidlinePoint(capsulesTarget[0],vecTargetPos,rotTarget,0.6f,vecTargetCoverPos);
			coverTarget.fRadius = capsulesTarget[0]->fRadius + capsulesTarget[0]->fLength * .4;
		}
		else
		{
			copyVec3(vecTargetPos,vecTargetCoverPos);
			coverTarget.fRadius = COVER_RADIUS_DEFAULT;
		}

		// Determine direction between the two
		subVec3(vecPlayerCoverPos,vecTargetCoverPos,vecDir);
		normalVec3(vecDir);


		// Fill target cover struct
		coverTarget.fLength = COVER_LENGTH_DEFAULT;
		coverTarget.iQuant = fabs(mmDrawCoverEnabled) - 1;

		// Root cover position is pushed forward equal to half the radius
		scaleAddVec3(vecDir,coverTarget.fRadius/2.f,vecTargetCoverPos,vecTargetCoverPos);
		scaleAddVec3(vecDir,COVER_LENGTH_DEFAULT,vecTargetCoverPos,vecCapEnd);

		copyVec3(vecTargetCoverPos,coverTarget.vecStart);
		copyVec3(vecDir,coverTarget.vecDir);

		// Do the query
		wcQueryTrianglesInCapsule(worldGetActiveColl(iPartitionIdx),
			WC_QUERY_BITS_TARGETING,
			vecTargetCoverPos,
			vecCapEnd,
			coverTarget.fRadius,
			coverHandleTriangles,
			&coverTarget);

		if(mmDrawCoverEnabled>0)
		{
			setColorFromRGBA(&colorCapsule, coverTarget.bHit ? 0xff000080 : 0x00ff0080);
			gfxDrawCapsule3D(vecTargetCoverPos, vecCapEnd, coverTarget.fRadius, 20, colorCapsule, 1);
		}


		// Invert for player
		scaleVec3(vecDir,-1.f,vecDir);

		// Fill player cover struct
		coverPlayer.fLength = COVER_LENGTH_DEFAULT;
		coverPlayer.iQuant = fabs(mmDrawCoverEnabled) - 1;

		// Root cover position is pushed forward equal to half the radius
		scaleAddVec3(vecDir,coverPlayer.fRadius/2.f,vecPlayerCoverPos,vecPlayerCoverPos);
		scaleAddVec3(vecDir,COVER_LENGTH_DEFAULT,vecPlayerCoverPos,vecCapEnd);

		copyVec3(vecPlayerCoverPos,coverPlayer.vecStart);
		copyVec3(vecDir,coverPlayer.vecDir);

		// Do the query
		wcQueryTrianglesInCapsule(worldGetActiveColl(iPartitionIdx),
			WC_QUERY_BITS_TARGETING,
			vecPlayerCoverPos,
			vecCapEnd,
			coverPlayer.fRadius,
			coverHandleTriangles,
			&coverPlayer);

		if(mmDrawCoverEnabled>0)
		{
			setColorFromRGBA(&colorCapsule, coverPlayer.bHit ? 0xff000080 : 0x00ff0080);
			gfxDrawCapsule3D(vecPlayerCoverPos, vecCapEnd, coverPlayer.fRadius, 20, colorCapsule, 1);
		}



		if(coverPlayer.iRegions)
		{
			int i,c=0;
			for(i=0; i<coverPlayer.iRegions; i++)
			{
				if(coverPlayer.pRegionsHit[i])
					c++;
			}
			gfxXYprintf(0,15,"Player: %d%%",(int)((F32)(100*c)/(F32)coverPlayer.iRegions));
			free(coverPlayer.pRegionsHit);
		}
		else
		{
			gfxXYprintf(0,15,"Player: 0%%");
		}

		if(coverTarget.iRegions)
		{
			int i,c=0;
			for(i=0; i<coverTarget.iRegions; i++)
			{
				if(coverTarget.pRegionsHit[i])
					c++;
			}
			gfxXYprintf(0,16,"Target: %d%%",(int)((F32)(100*c)/(F32)coverTarget.iRegions));
			free(coverTarget.pRegionsHit);
		}
		else
		{
			gfxXYprintf(0,16,"Target: 0%%");
		}

		PERFINFO_AUTO_STOP();
	}
}

void mmDrawRotations(void){
	S32 mmCount;
	
	if(!mmDrawRotationsEnabled){
		return;
	}

	mmCount = mmGetManagerCountFG();
	
	FOR_BEGIN(i, mmCount);
	{
		MovementManager*		mm;
		Vec3					pos;
		Quat					rot;
		Mat3					mat;
		Vec3					pyrFace;
		Mat3					matFace;
		
		if(!mmGetManagerFG(i, &mm)){
			continue;
		}
		
		mmGetPositionFG(mm, pos);
		mmGetRotationFG(mm, rot);
		mmGetFacePitchYawFG(mm, pyrFace);
		pyrFace[2] = 0;
		
		quatToMat(rot, mat);
		createMat3YPR(matFace, pyrFace);
		
		FOR_BEGIN(j, 3);
		{
			Vec3 a;
			Vec3 b;
			
			copyVec3(pos, a);
			if(j != 1){
				addVec3(a, mat[1], a);
			}
			copyVec3(a, b);
			scaleAddVec3(mat[j], 6, a, b);
			
			gfxDrawLine3D_2(a,
							b,
							ColorWhite,
							!j ?
								ColorRed :
								j == 1 ?
									ColorGreen :
									ColorBlue);
		}
		FOR_END;
		
		{
			Vec3 a;
			Vec3 b;
			copyVec3(pos, a);
			scaleAddVec3(mat[1], 6.f, a, a);
			copyVec3(a, b);
			scaleAddVec3(matFace[2], 6, b, b);

			gfxDrawLine3D_2(a,
							b,
							ColorWhite,
							ColorMagenta);
		}
	}
	FOR_END;
}

void gclMovementDebugDraw3D(const Mat4 matCamera){
	MovementDrawFuncs funcs;
	
	PERFINFO_AUTO_START_FUNC();
	
	copyMat4(matCamera, mmDrawState.matCamera);
	
	funcs.drawLine3D = gclMovementDrawLine3D;
	funcs.drawCapsule3D = gclMovementDrawCapsule3D;
	funcs.drawTriangle3D = gclMovementDrawTriangle3D;
	funcs.drawBox3D = gclMovementDrawBox3D;

	mmDebugLogDraw3D();

	if(mmOffsetGraphEnabled){
		mmDrawOffsetGraph();
	}
	
	if(mmDrawServerPosEnabled){
		mmDrawServerPos();
	}
	
	if(netTimingGraphEnabled){
		mmDrawClientStatsFrames();
		mmDrawClientStatsPackets();
	}

	if(mmDrawCapsEnabled){
		mmDrawCapsules();
	}

	if(mmDrawCylsEnabled){
		mmDrawCylinders();
	}

	if(mmDrawCombatEnabled){
		mmDrawCombat();
	}

	if(mmDrawProjectilesEnabled){
		mmDrawProjectiles();
	}

	mmDrawCover();
	
	if(	mmDrawBodiesEnabled ||
		mmDrawBodyBoundsEnabled ||
		mmDrawNetOutputsEnabled ||
		mmDrawOutputsEnabled)
	{
		mmDebugDraw(&funcs,
					matCamera[3],
					mmDrawBodiesEnabled,
					mmDrawBodyBoundsEnabled,
					mmDrawNetOutputsEnabled,
					mmDrawOutputsEnabled);
	}
	
	mmAlwaysDraw(&funcs, matCamera);
	
	if(mmDrawResourceDebugEnabled){
		mmDrawResourceDebug(&funcs, matCamera);
	}
	
	if(mmDrawNetOffsetFromEndEnabled){
		mmDrawNetOffsetFromEnd();
	}
	
	if(mmDrawKeyStatesEnabled){
	}

	mmDrawRotations();
	
	PERFINFO_AUTO_STOP();
}

