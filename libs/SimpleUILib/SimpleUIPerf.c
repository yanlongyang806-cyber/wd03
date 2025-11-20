
#if 0
#include "SimpleUI.h"
#include "SimpleUIPerf.h"
#include "SimpleUIScroller.h"
#include "stdtypes.h"
#include "wininclude.h"
#include "earray.h"
#include "timing.h"
#include "StashTable.h"
#include "MemoryPool.h"
#include "strings_opt.h"
#include "utils.h"

// PerfWindow

typedef struct PerfWindow {
	SUIWindow*			wScroller;
	U32					redrawTime;
	AutoTimerReader*	atReader;
	S32					pos;
} PerfWindow;

#if 0
typedef struct PerfInfoHistoryEntry {
	S64							cycles;
	U32							count;	
} PerfInfoHistoryEntry;

typedef struct PerfInfoUserData {
	PerformanceInfo*			perfInfo;
	PerfInfoUserData*			child;
	PerfInfoUserData*			sibling;
	
	PerfInfoHistoryEntry		history[200];

	U32							open		: 1;
	U32							hidden		: 1;
	U32							inited		: 1;
} PerfInfoUserData;

MP_DEFINE(PerfInfoUserData);

void destroyPerfInfoUserData(PerformanceInfo* info){
	MP_FREE(PerfInfoUserData, info->userData.data);
	info->userData.destructor = NULL;
}

PerfInfoUserData* getPerfInfoUserData(PerformanceInfo* info){
	PerfInfoUserData* userData;
	
	if(!info->userData.data){
		S32 i;
		
		MP_CREATE(PerfInfoUserData, 100);
	
		userData = MP_ALLOC(PerfInfoUserData);
		info->userData.destructor = destroyPerfInfoUserData;
		
		for(i = 0; i < ARRAY_SIZE(userData->history); i++){
			userData->history[i].count = ~0;
		}
		
		if(!info->parent && info->locName && !strnicmp(info->locName, "Sleep(", 6)){
			userData->hidden = 1;
		}
	}
	
	return info->userData.data;
}

static S32 timerPaused;
static S32 historyPos;

static void entDebugAdvanceTimerHistoryHelper(PerformanceInfo* cur){
	for(; cur; cur = cur->nextSibling){
		if(!timerPaused){
			PerfInfoUserData* infoData = getPerfInfoUserData(cur);
			S64 cycles64 = cur->curTick.cycles64;
			S32 count = cur->curTick.count;
			infoData->history[historyPos].cycles = cycles64;
			infoData->history[historyPos].count = count;
			cur->totalTime += cycles64;
			cur->opCountInt += count;
		}
			
		entDebugAdvanceTimerHistoryHelper(cur->child.head);
	}
}

static void entDebugAdvanceSystemTimer(PerformanceInfo* info){
	PerfInfoUserData* infoData = getPerfInfoUserData(info);
	infoData->history[historyPos].cycles = info->curTick.cycles64;
	infoData->history[historyPos].count = 0;
}

static void entDebugAdvanceSystemTimerTree(PerformanceInfo* info){
	entDebugAdvanceSystemTimer(info);
	
	for(info = info->child.head; info; info = info->nextSibling){
		entDebugAdvanceSystemTimerTree(info);
	}
}

void entDebugAdvanceTimerHistory(void){
	if(PERFINFO_RUN_CONDITIONS){
		if(!timerPaused){
			PerfInfoIterator iter = {0};
		
			PERFINFO_AUTO_START("entDebugAdvanceTimerHistory", 1);

				// Advance the history position.
				
				historyPos = (historyPos + 1) % TYPE_ARRAY_SIZE(PerfInfoUserData, history);

				entDebugAdvanceSystemTimer(autoTimerGetPageFaults());
				entDebugAdvanceSystemTimer(autoTimerGetProcessTimeUser());
				entDebugAdvanceSystemTimer(autoTimerGetProcessTimeKernel());
				entDebugAdvanceSystemTimerTree(autoTimerGetOtherProcesses());

				while(autoTimerIterateThreadRoots(&iter)){
					autoTimerDestroyLockAcquireByThreadID(iter.curThreadID);
					
					PERFINFO_AUTO_START("advance", 1);
						entDebugAdvanceSystemTimer(&iter.publicData->systemCounters.userMode);
						entDebugAdvanceSystemTimer(&iter.publicData->systemCounters.kernelMode);

						entDebugAdvanceTimerHistoryHelper(iter.publicData->timerRoot);
					PERFINFO_AUTO_STOP();

					autoTimerDestroyLockReleaseByThreadID(iter.curThreadID);
				}

			PERFINFO_AUTO_STOP();
		}
	}
}

typedef struct PerfInfoThreadRootTimers
{
	PerformanceInfo		threadParent;
} PerfInfoThreadRootTimers;

static StashTable threadRootTable;

static PerfInfoThreadRootTimers* entDebugGetThreadRootTimers(U32 threadID){
	PerfInfoThreadRootTimers* timers;

	if(!threadRootTable){
		threadRootTable = stashTableCreateInt(100);
	}
	
	if(!stashIntFindPointer(threadRootTable, threadID, &timers)){
		timers = calloc(1, sizeof(PerfInfoThreadRootTimers));
		
		stashIntAddPointer(threadRootTable, threadID, timers, 0);
	}
	
	return timers;
}

static StashTable stateTable;

static StashElement perfInfoGetElement(const char* source, PerformanceInfo* info, S32 create){
	char buffer[10000];
	PerformanceInfo* curInfo = info;
	StashElement element;

	if(!stateTable){
		stateTable = stashTableCreateWithStringKeys(100, StashDeepCopyKeys);
	}

	STR_COMBINE_BEGIN(buffer);


	STR_COMBINE_CAT(source);
	STR_COMBINE_CAT(":");

	while(curInfo){
		if(info != curInfo){
			STR_COMBINE_CAT(":");
		}
		
		if((intptr_t)curInfo->staticData){
			STR_COMBINE_CAT_D((intptr_t)curInfo->staticData);
		}else{
			STR_COMBINE_CAT(curInfo->locName);
		}
		
		curInfo = curInfo->parent;		 
	}
	
	STR_COMBINE_END();

	if(!stashFindElement(stateTable, buffer, &element)){
		if(create){
			stashAddIntAndGetElement(stateTable, buffer, 0, 0, &element);
		}else{
			return NULL;
		}
	}
	
	return element;
}

static void perfInfoSetState(	const char* source,
								PerformanceInfo* info,
								S32 flag,
								S32 state)
{
	if(source){
		StashElement element = perfInfoGetElement(source, info, 1);

		if(element){
			S32 value = stashElementGetInt(element);

			if(state){
				value |= flag;
			}else{
				value &= ~flag;
			}

			stashElementSetInt(element, value);
		}
	}
}

static void perfInfoSetOpen(const char* source,
							PerformanceInfo* info,
							S32 set)
{
	PerfInfoUserData* infoData = getPerfInfoUserData(info);
	
	infoData->open = set;

	perfInfoSetState(source, info, 1, set);
}

static void perfInfoSetHidden(	const char* source,
								PerformanceInfo* info,
								S32 set)
{
	if(source){
		PerfInfoUserData* infoData = getPerfInfoUserData(info);

		infoData->hidden = set;

		perfInfoSetState(source, info, 2, set);
	}
}

static void perfInfoInitState(const char* source, PerformanceInfo* info){
	PerfInfoUserData* infoData = getPerfInfoUserData(info);
	StashElement element;

	if(!source || infoData->inited){
		return;
	}
	
	infoData->inited = 1;

	element = perfInfoGetElement(source, info, 0);

	if(element){
		infoData->open = stashElementGetInt(element) & 1 ? 1 : 0;
		infoData->hidden = stashElementGetInt(element) & 2 ? 1 : 0;
	}
}

static void entDebugAccumulateChildHistory(	PerformanceInfo* threadParent,
											PerfInfoPublicThreadData* publicData)
{
	PerfInfoUserData* infoThread = getPerfInfoUserData(threadParent);
	PerformanceInfo* curRoot;
	
	ZeroArray(infoThread->history);
	
	threadParent->opCountInt = 0;
	
	for(curRoot = publicData->timerRoot; curRoot; curRoot = curRoot->nextSibling){
		PerfInfoUserData* infoRoot = getPerfInfoUserData(curRoot);
		
		if(!infoRoot->hidden){
			S32 i;
			
			for(i = 0; i < ARRAY_SIZE(infoThread->history); i++){
				infoThread->history[i].cycles += infoRoot->history[i].cycles;
				infoThread->history[i].count += infoRoot->history[i].count;
			}
			
			threadParent->opCountInt += curRoot->opCountInt;
		}
	}
}

static struct {
	S32 y;
	S32 selectedY;
	S32 maxY;
	S32 minY;
} perfDraw;

static const char* getPercentColor(float percent){
	if(percent >= 20)
		return "^1";
	else if(percent >= 10)
		return "^4";
	else if(percent >= 5)
		return "^7";
	else if(percent >= 1)
		return "^2";
	else
		return "^5";
}

static const char* getPercentString(float percent){
	static char buffer[15];
	
	STR_COMBINE_BEGIN(buffer);
	if(percent < 1){
		STR_COMBINE_CAT("^s");
	}
	STR_COMBINE_CAT(getPercentColor(percent));
	STR_COMBINE_CAT(getPercentColor(percent));
	if(percent >= 100){
		STR_COMBINE_CAT_D(100);
	}
	else if(percent >= 10){
		STR_COMBINE_CAT_D((S32)percent);
		STR_COMBINE_CAT(".");
		STR_COMBINE_CAT_D((S32)(percent * 10) % 10);
	}
	else{
		STR_COMBINE_CAT_D((S32)percent);
		STR_COMBINE_CAT(".");
		STR_COMBINE_CAT_D((S32)(percent * 10) % 10);
		STR_COMBINE_CAT_D((S32)(percent * 100) % 10);
	}
	if(percent < 1){
		STR_COMBINE_CAT("^n");
	}
	STR_COMBINE_END();

	return buffer;
}

static S32 getPerfInfoDepth(PerformanceInfo* info){
	S32 childDepthMax = 0;
	
	for(info = info->child.head; info; info = info->nextSibling){
		S32 childDepth = getPerfInfoDepth(info);
		
		childDepthMax = max(childDepth, childDepthMax);
	}
	
	return 1 + childDepthMax;
}

static S64 getHighestChildCPUTotal(PerformanceInfo* info, S32 historyPos, S64* highest){
	S64 total = 0;
	PerformanceInfo* cur;
	
	if(!info){
		return 0;
	}
	
	if(highest){
		*highest = 0;
	}
	
	for(cur = info->child.head; cur; cur = cur->nextSibling){
		S64 curCPU;
		
		if(cur->infoType == PERFINFO_TYPE_CPU){
			PerfInfoUserData* curData = getPerfInfoUserData(cur);
			
			curCPU = curData->history[historyPos].cycles;
		}else{
			curCPU = getHighestChildCPUTotal(cur, historyPos, NULL);
		}
		
		total += curCPU;
		
		if(highest && curCPU > *highest){
			*highest = curCPU;
		}
	}
	
	return total;
}

static S64 getSelfOrChildrenHighestCPU(PerformanceInfo* info, S32 historyPos){
	if(info->infoType == PERFINFO_TYPE_CPU){
		PerfInfoUserData* infoData = getPerfInfoUserData(info);
		
		return infoData->history[historyPos].cycles;
	}else{
		return getHighestChildCPUTotal(info, historyPos, NULL);
	}
}

static U32 getPerfInfoCount(PerformanceInfo* info){
	U32 total = 1;
	PerformanceInfo* child;
	
	for(child = info->child.head; child; child = child->nextSibling){
		total += getPerfInfoCount(child);
	}

	return total;
}

typedef struct DrawPerfInfoData {
	S32 		windowSizeX;
	S32 		windowSizeY;
	S64 		totalTime;
	S64 		cpuSpeed;
	const char* prefix;
} DrawPerfInfoData;

static void displayPerformanceInfoHelper(	const SUIDrawContext* dc,
											const DrawPerfInfoData* data,
											PerformanceInfo* info,
											S32* y,
											S32 level)
{
	static char* buffer;
	
	S32 index = info->uid;
	S32 hiddenChild = 0;
	S32 draw_y = *y;
	S32 level_color = max(0, 0x50 - 8 * level);
	S32 boxColor = !level ? 0x003030 : info->breakMe ? 0xffff00 : (level_color | level_color << 8 | level_color << 16);
	PerfInfoUserData* infoData = getPerfInfoUserData(info);
	S32 size = ARRAY_SIZE(infoData->history);
	float scale = 4.0f;//debug_state.perfInfo.scaleF32;
	S32 selected = 0;
	S32 lineCount = 0;
	S32 showGraphHilight = 0;
	S32 mouse_over = 0;
	S32 get_zoomed_data = 0;

	PerformanceInfo* child;
	S32 alpha;
	S32 j, k;
	
	if(!buffer){
		buffer = malloc(1000);
	}
	
	perfInfoInitState(data->prefix, info);

	if(infoData->hidden){
		*y -= 15;
		return;
	}

	// Draw children first, in order to collect info about them.

	if(	0 &&
		!infoData->open &&
		level <= 20)
	{
		//printf("level: %d\n", level);
		perfInfoSetOpen(data->prefix, info, 1);
	}
	
	if(infoData->open){
		for(child = info->child.head; child; child = child->nextSibling){
			S32 oldy = *y;

			*y += 15;

			displayPerformanceInfoHelper(dc, data, child, y, level + 1);

			if(*y == oldy){
				hiddenChild = 1;
			}
		}
	}
	else if(!level){
		for(child = info->child.head; child; child = child->nextSibling){
			PerfInfoUserData* childData = getPerfInfoUserData(child);

			if(childData->hidden){
				hiddenChild = 1;
			}
		}		
	}
	
	if(draw_y > perfDraw.maxY){
		perfDraw.maxY = draw_y;
	}
	
	if(draw_y < perfDraw.minY){
		perfDraw.minY = draw_y;
	}

	if(draw_y < 0 || draw_y > data->windowSizeY){
		return;
	}

	PERFINFO_AUTO_START("top", 1);
	
	// Draw self after children.

	//if(info->child.head && !info->open){
	//	boxColor = info->breakMe ? 0xffff00 : 0x112211;
	//}

	if(1){//!inpIsMouseLocked()){
		alpha = 355 - 0;//mousex
		alpha = min(255, max(0, alpha));
	}else{
		alpha = 0;
	}

	if(0){//if(/*!inpIsMouseLocked() &&*/ g.mx >= 0 && g.mx < 650 && g.my >= draw_y && g.my < draw_y + 15){
		if(1){//!inpIsMouseLocked()){
			//debug_state.mouseOverUI = 1;
			selected = 1;
			perfDraw.selectedY = draw_y;
			alpha = max(alpha, 192);
			//boxColor =	(info->breakMe ? 0xffff00 : level ? 0x000000 : 0xff8080) |
			//			(127 + abs(128 - ((global_state.global_frame_count) % 257))) << 16 |
			//			(abs(255 - (((global_state.global_frame_count * 2) + 255)) % 510) / 2);
		}

		//if(inpEdge(INP_LBUTTON)){
		//	eatEdge(INP_LBUTTON);
		//	if(g.mx >= 0 && g.mx < 12){
		//		if(prefix){
		//			if(debug_state.perfInfo.breaks){
		//				if(!stricmp(prefix, "s")){
		//					info->breakMe ^= 1;
		//					sprintf(buffer, "perfinfosetbreak_server %d %d %d", debug_state.serverTimersLayoutID, info->uid, info->breakMe);
		//					cmdParse(buffer);
		//				}
		//				else if(!stricmp(prefix, "c")){
		//					info->breakMe ^= 1;
		//				}
		//			}else{
		//				perfInfoSetHidden(prefix, info, 1);
		//			}
		//		}
		//	}
		//	else if(hiddenChild && g.mx >= 14 && g.mx < 26){
		//		for(child = info->child.head; child; child = child->nextSibling){
		//			perfInfoSetHidden(prefix, child, 0);
		//		}
		//	}
		//	else if(info->child.head){
		//		perfInfoSetOpen(prefix, info, !infoData->open);
		//	}
		//}
	}

	if(	selected &&
		//!inpIsMouseLocked() && 
		1
		//g.mx >= 0 &&
		//g.mx < 252
		)
	{
		get_zoomed_data = 1;
		//debug_state.perfInfo.zoomedInfo = info;
		//debug_state.perfInfo.zoomedAlpha = max(0, min(255, 255 - g.mx));
	}
 	
 	if(	//!inpIsMouseLocked() && 
 		1)
 		//g.mx >= 252 &&
 		//g.mx < 252 + ARRAY_SIZE(infoData->history))
 	{
 		alpha = 100;
 		
		//debug_state.perfInfo.zoomedHilightLine = ARRAY_SIZE(infoData->history) - (g.mx - 252);
		showGraphHilight = 1;

 		if(selected){
 			get_zoomed_data = 1;
			//debug_state.perfInfo.zoomedInfo = info;
			//debug_state.perfInfo.zoomedAlpha = 128;
			
			//if(!debug_state.perfInfo.lockMouseX && inpLevel(INP_CONTROL)){
			//	debug_state.perfInfo.lockMouseX = g.mx;
			//}
		}
	}

	if(get_zoomed_data){
		//gatherZoomedData(info);
	}

	alpha <<= 24;

	if(alpha){
		mouse_over = 0;//selected ? g.mx >= 0 ? g.mx < 12 ? 1 : g.mx < 26 ? 2 : 0 : 0 : 0;

		if(mouse_over){
			//debug_state.mouseOverUI = 1;
		}

		suiSetClipRect(dc, 0, draw_y + 1, data->windowSizeX, 14);
		suiDrawFilledRect(dc, 0, draw_y + 1, data->windowSizeX, 14, boxColor | alpha);
		//suiDrawFilledRect(dc, 20 * level, draw_y + 13, 650, draw_y + 15, 0xffff0000);
		//drawFilledBox4(	0,
		//				g.h - 15 - draw_y,
		//				650,
		//				g.h - draw_y,
		//				0x111111 | alpha, 0x111111 | alpha,
		//				boxColor | alpha, boxColor | alpha);
	}
	
	if(	1 || //!debug_state.perfInfo.hideText ||
		selected)
	{
		F32 percent = (F32)info->totalTime * 100.0f / (data->totalTime ? data->totalTime : 1);

		//if(0){
		//	STR_COMBINE_BEGIN(buffer);
		//	STR_COMBINE_CAT(getPercentString(percent));
		//	STR_COMBINE_CAT("^t30t\t^0");
		//	if(info->child.head){
		//		STR_COMBINE_CAT(infoData->open ? "[^1--^4" : "[^2+^4");
		//		STR_COMBINE_CAT_D(getPerfInfoDepth(info) - 1);
		//		STR_COMBINE_CAT("^d]^");
		//	}else{
		//		STR_COMBINE_CAT("     ^d^");
		//	}
		//	STR_COMBINE_CAT_D(level % 10);
		//	switch(info->infoType){
		//		case PERFINFO_TYPE_BITS:
		//			STR_COMBINE_CAT("b:");
		//			break;
		//		case PERFINFO_TYPE_MISC:
		//			STR_COMBINE_CAT("c:");
		//			break;
		//	}
		//	STR_COMBINE_CAT(info->locName);
		//	if(selected && hiddenChild){
		//		STR_COMBINE_CAT(" ^2[*** HIDDEN CHILD ***]");
		//	}
		//	STR_COMBINE_END();
		//}
		
		suiPrintText(dc, 25 + level * 10, draw_y + 1, info->locName, -1, 0);
		suiPrintText(dc, 25 + level * 10, draw_y, info->locName, -1, 0xff555555 | (0x99  << ((level % 3) * 8)));
		//printDebugString(buffer, 15 + level * 10 + debug_state.perfInfo.leftX, g.h - 15 - draw_y + 3, 1, 11, -1, 0, 255, NULL);

		if(data->totalTime){
			STR_COMBINE_BEGIN_S(buffer, 1000);
			STR_COMBINE_CAT("^4");
			STR_COMBINE_CAT(getCommaSeparatedInt(info->opCountInt));
			STR_COMBINE_CAT("^t100t\t");
			STR_COMBINE_CAT(getCommaSeparatedInt(info->opCountInt ? info->totalTime / info->opCountInt : 0));
			//if(debug_state.perfInfo.showMaxCPUCycles || selected){
			//	STR_COMBINE_CAT("^0(^s^4");
			//	STR_COMBINE_CAT(getCommaSeparatedInt(info->maxTime));
			//	STR_COMBINE_CAT("^0^n)");
			//}
			STR_COMBINE_END();
			
			suiPrintText(dc, data->windowSizeX - 192, draw_y, getCommaSeparatedInt(info->opCountInt), -1, 0xffaaaaaa);
			suiPrintText(dc, data->windowSizeX - 92, draw_y, getCommaSeparatedInt(info->opCountInt ? info->totalTime / info->opCountInt : 0), -1, 0xffaaaaaa);
			//printDebugString(buffer, 460, g.h - 15 - draw_y + 3, 1, 15, -1, 0, 255, NULL);
		}
	}

	//if(alpha){
	//	U32 color = alpha;
	//	//S32 i = (historyPos + debug_state.perfInfo.zoomedHilightLine) % size;

	//	// Draw those two buttons on the left.
	//	
	//	suiDrawFilledRect(dc, 1, draw_y + 14, 12, 
	//	
	//	drawFilledBox4(	1,
	//					g.h - 14 - draw_y,
	//					12,
	//					g.h - draw_y - 1,
	//					alpha | 0xcccccc,
	//					alpha | 0xdddddd,
	//					alpha | 0xaaaaaa,
	//					alpha | 0xbbbbbb);

	//	drawFilledBox4(	2,
	//					g.h - 13 - draw_y,
	//					11,
	//					g.h - draw_y - 2,
	//					alpha | (mouse_over == 1 ? rand() : hiddenChild ? 0x880000 : 0x009900),
	//					alpha | (mouse_over == 1 ? rand() : hiddenChild ? 0x770000 : 0x00aa00),
	//					alpha | (mouse_over == 1 ? rand() : hiddenChild ? 0x440000 : 0x007700),
	//					alpha | (mouse_over == 1 ? rand() : hiddenChild ? 0x990000 : 0x008800));

	//	if(selected && hiddenChild){
	//		drawFilledBox4(	14,
	//						g.h - 14 - draw_y,
	//						25,
	//						g.h - draw_y - 1,
	//						alpha | 0xcccccc,
	//						alpha | 0xdddddd,
	//						alpha | 0xaaaaaa,
	//						alpha | 0xbbbbbb);

	//		drawFilledBox4(	15,
	//						g.h - 13 - draw_y,
	//						24,
	//						g.h - draw_y - 2,
	//						alpha | (mouse_over == 2 ? rand() : 0x009900),
	//						alpha | (mouse_over == 2 ? rand() : 0x00aa00),
	//						alpha | (mouse_over == 2 ? rand() : 0x007700),
	//						alpha | (mouse_over == 2 ? rand() : 0x008800));
	//	}

	//	// Draw the meter to the right of the graph.
	//	
	//	//if(	showGraphHilight &&
	//	//	infoData->history[i].count &&
	//	//	infoData->history[i].count != (U32)~0)
	//	//{
	//	//	color = alpha | 0x505000;
	//	//	
	//	//	if(info->parent){
	//	//		S64 parentCPU = getSelfOrChildrenHighestCPU(info->parent, i);
	//	//		S64 selfCPU = getSelfOrChildrenHighestCPU(info, i);
	//	//		
	//	//		if(parentCPU){
	//	//			//sprintf(buffer, "^%d^b%d/100b", level % 10, (S32)((F32)selfCPU * 100.0 / parentCPU));
	//	//			STR_COMBINE_BEGIN(buffer);
	//	//			STR_COMBINE_CAT("^");
	//	//			STR_COMBINE_CAT_D(level % 10);
	//	//			STR_COMBINE_CAT("^b");
	//	//			STR_COMBINE_CAT_D((S32)((F32)selfCPU * 100.0 / parentCPU));
	//	//			STR_COMBINE_CAT("/100b");
	//	//			STR_COMBINE_END();
	//	//			
	//	//			printDebugString(buffer, 452, g.h - draw_y - 12, 1, 14, -1, -1, 255, NULL);
	//	//		}
	//	//	}
	//	//}

	//	if(!level){
	//		color = alpha | 0x002020;
	//	}
	//	
	//	drawFilledBox(250, g.h - draw_y - 15, 450, g.h - draw_y, color);
	//}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("drawSmallGraph", 1);
	if(0){
		S32 xRight = data->windowSizeX;
		
		for(j = (historyPos + 1) % size, k = 0;
			k < size;
			j = (j + 1) % size, k++)
		{
			F32 cycles = infoData->history[j].cycles;
			F32 percent = (F32)cycles * 100 / (F32)data->cpuSpeed;
			S32 color;
			S32 height = scale * percent * 2 * 100;
			S32 true_color;
			S32 base_color = 0xff00ffff;
			S32 base_alpha = 0xff000000;

			percent = min(percent, 100);

			color = 255 * percent / 10;
			color = max(0, min(255, color));
			true_color = color << 16 | (max(min(64 + 255 - color, 255), 64) << 8) | (255 - color);
			
			//if(showGraphHilight && debug_state.perfInfo.zoomedHilightLine - 1 == k){
			//	base_color = true_color = debug_state.perfInfo.smallGraphHilightColor;
			//	
			//	drawLine2(	449 - k,
			//				g.h - draw_y - 15,
			//				449 - k,
			//				g.h - draw_y,
			//				0x40ff0000,
			//				0x40ff0000);
			//}
			
			//if(infoData->history[j].count == (U32)~0){
			//	if(k == size - 1 || infoData->history[(j+1)%size].count != (U32)~0){
			//		height = 1500;
			//		
			//		if(base_color != debug_state.perfInfo.smallGraphHilightColor){
			//			base_color = true_color = 0x00ff0000;
			//			base_alpha = 0x80000000;
			//		}
			//	}
			//}

			if(height > 0){
				S32 dy = height / 100 + ((height % 100) ? 1 : 0);
				
				if(dy > 0){
					//suiDrawLine(dc, xRight - 200 - k, draw_y + 14, xRight - 200 - k, draw_y + 14 - (height / 100), 0xffffffff);
					suiDrawFilledRect(dc, xRight - 200 - k, draw_y + 14 - dy, 1, dy, 0xff22ff22);
					//drawLine2(	449 - k,
					//			g.h - draw_y - 15,
					//			449 - k,
					//			g.h - draw_y - 15 + (height / 100),
					//			base_alpha | base_color,
					//			base_alpha | true_color);
				}

				if(0)if(height % 100){
					S32 alpha = (64 + (191 * (height % 100)) / 99) << 24;

					suiDrawFilledRect(dc, xRight - 200 - k, draw_y + 14 - dy, 1, 1, 0xff220022 | (alpha << 8));

					//drawLine2(	449 - k,
					//			g.h - draw_y - 15 + (height / 100),
					//			449 - k,
					//			g.h - draw_y - 15 + (height / 100) + 1,
					//			alpha | true_color,
					//			alpha | true_color);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}
#endif

static S32 suiPerfWindowMsgHandler(SUIWindow* w, const SUIWindowMsg* msg);

#if 0
static void drawPerfInfo(	SUIWindow* w,
							PerfWindow* wd,
							const SUIWindowMsg* msg)
{
	const SUIDrawContext*	dc = msg->msgData;
	PerfInfoIterator		iter = {0};
	char					buffer[1000];
	S32						y = 0;
	DrawPerfInfoData		drawData = {0};;
	
	suiWindowGetSize(w, &drawData.windowSizeX, &drawData.windowSizeY);
	drawData.cpuSpeed = getRegistryMhz();
	drawData.prefix = NULL;
	drawData.totalTime = 1;
	
	if(1){
		displayPerformanceInfoHelper(dc, &drawData, autoTimerGetPageFaults(), &y, 0);
		y += 15;
	}
	
	if(1){
		displayPerformanceInfoHelper(dc, &drawData, autoTimerGetProcessTimeUser(), &y, 0);
		y += 15;
	}
	
	if(1){
		displayPerformanceInfoHelper(dc, &drawData, autoTimerGetProcessTimeKernel(), &y, 0);
		y += 15;
	}

	if(1){
		displayPerformanceInfoHelper(dc, &drawData, autoTimerGetOtherProcesses(), &y, 0);
		y += 15;
	}

	while(autoTimerIterateThreadRoots(&iter)){
		PerfInfoThreadRootTimers* rootTimers = entDebugGetThreadRootTimers(iter.curThreadID);
		PerformanceInfo* threadParent = &rootTimers->threadParent;
		PerfInfoUserData* infoThread;
		
		PERFINFO_AUTO_START("acquireLock", 1);
			autoTimerDestroyLockAcquireByThreadID(iter.curThreadID);
		PERFINFO_AUTO_STOP();
		
		//sprintf(buffer, "Thread ^4%d ^0(pri ^4%d^0)", perfRoot->threadID, GetThreadPriority(threadHandle));
		STR_COMBINE_BEGIN(buffer);
		if(!iter.publicData->timerRoot){
			STR_COMBINE_CAT("^#(55,55,55)");
		}
		STR_COMBINE_CAT("Thread ^4");
		if(!iter.publicData->timerRoot){
			STR_COMBINE_CAT("^#(95,55,55)");
		}
		STR_COMBINE_CAT_D(iter.curThreadID);
		STR_COMBINE_CAT(" ^0(pri ^4");
		STR_COMBINE_CAT_D(GetThreadPriority(iter.publicData->threadHandle));
		STR_COMBINE_CAT("^0)");
		STR_COMBINE_END();
		
		threadParent->locName = buffer;
		threadParent->uid = (intptr_t)threadParent;
		
		threadParent->child.head = threadParent->child.tail = iter.publicData->timerRoot;
		
		#define ADD_TIMER(x)							\
			(x)->nextSibling = threadParent->child.head;\
			threadParent->child.head = (x);

		if(1){
			ADD_TIMER(&iter.publicData->systemCounters.kernelMode);
		}
		
		if(1){
			ADD_TIMER(&iter.publicData->systemCounters.userMode);
		}
		
		while(threadParent->child.tail && threadParent->child.tail->nextSibling){
			threadParent->child.tail = threadParent->child.tail->nextSibling;
		}
		
		infoThread = getPerfInfoUserData(threadParent);
		
		perfInfoInitState("c", threadParent);
		
		PERFINFO_AUTO_START("entDebugAccumulateChildHistory", 1)
			entDebugAccumulateChildHistory(threadParent, iter.publicData);
		PERFINFO_AUTO_STOP();
		
		if(infoThread->hidden){
			if(0){//resetTops){
				perfInfoSetHidden("c", threadParent, 0);
			}
			//showUnhide = 1;
		}else{
			PERFINFO_AUTO_START("drawThreadChildren", 1);
				displayPerformanceInfoHelper(dc, &drawData, threadParent, &y, 0);
			PERFINFO_AUTO_STOP();
			
			y += 15;
		}

		PERFINFO_AUTO_START("releaseLock", 1);
			autoTimerDestroyLockReleaseByThreadID(iter.curThreadID);
		PERFINFO_AUTO_STOP();
	}
}
#endif

static void suiPerfUpdateScroller(	SUIWindow* w,
									PerfWindow* wd)
{
	suiWindowSetSize(wd->wScroller, suiWindowGetSizeX(w), suiWindowGetSizeY(w));
}

static S32 suiPerfWindowHandleCreate(	SUIWindow* w,
										PerfWindow* wd,
										const SUIWindowMsg* msg)
{
	assert(!wd);
	
	callocStruct(wd, PerfWindow);
	
	suiWindowSetUserPointer(w, suiPerfWindowMsgHandler, wd);
	
	autoTimerReaderCreate(&wd->atReader);

	suiWindowSetPosAndSize(w, 200, 200, 500, 400);
	
	suiWindowSetProcessing(w, 1);
	
	suiScrollerCreate(&wd->wScroller, w, w);
						
	suiWindowAddChild(w, wd->wScroller);
						
	suiPerfUpdateScroller(w, wd);
						
	return 1;
}

static S32 suiPerfWindowHandleDestroy(	SUIWindow* w,
										PerfWindow* wd,
										const SUIWindowMsg* msg)
{
	autoTimerReaderDestroy(&wd->atReader);
	SAFE_FREE(wd);
	return 1;
}

static S32 suiPerfWindowHandleDraw(	SUIWindow* w,
									PerfWindow* wd,
									const SUIWindowMsg* msg)
{
	const SUIDrawContext* dc = msg->msgData;
	
	PERFINFO_AUTO_START("suiPerfWindowHandleDraw", 1);
		suiDrawFilledRect(dc, 0, 0, suiWindowGetSizeX(w), suiWindowGetSizeY(w), 0xff000000);
		
		suiDrawFilledRect(dc, 100, wd->pos, 200, 10, 0xffffff00);
		
		PERFINFO_AUTO_START("drawPerfInfo", 1);
			//drawPerfInfo(w, wd, msg);
		PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
	
	return 1;
}

static S32 suiPerfWindowHandleProcess(	SUIWindow* w,
										PerfWindow* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseMove* mm = msg->msgData;
	
	if(!wd->redrawTime){
		wd->redrawTime = timeGetTime();
	}
	
	if((U32)(timeGetTime() - wd->redrawTime) > 100){
		suiWindowInvalidate(w);
		wd->redrawTime = timeGetTime();
	}
	
	return 1;
}

static S32 suiPerfWindowHandleMouseDown(SUIWindow* w,
										PerfWindow* wd,
										const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* mm = msg->msgData;
	
	return 0;
}

static S32 suiPerfWindowHandleMouseDoubleClick(	SUIWindow* w,
												PerfWindow* wd,
												const SUIWindowMsg* msg)
{
	const SUIWindowMsgMouseButton* mm = msg->msgData;
	
	return 0;
}

static S32 suiPerfWindowHandleSizeChanged(	SUIWindow* w,
											PerfWindow* wd,
											const SUIWindowMsg* msg)
{
	suiPerfUpdateScroller(w, wd);
	
	return 0;
}

static S32 suiPerfWindowMsgHandler(SUIWindow* w, const SUIWindowMsg* msg){
	PerfWindow* wd;
	
	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, PerfWindow, wd);
		SUI_WM_HANDLER(SUI_WM_CREATE,				suiPerfWindowHandleCreate);
		SUI_WM_HANDLER(SUI_WM_DESTROY,				suiPerfWindowHandleDestroy);
		SUI_WM_HANDLER(SUI_WM_DRAW,					suiPerfWindowHandleDraw);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOWN,			suiPerfWindowHandleMouseDown);
		SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	suiPerfWindowHandleMouseDoubleClick);
		//SUI_WM_HANDLER(SUI_WM_MOUSE_DOUBLECLICK,	suiPerfWindowHandleMouseDown);
		//SUI_WM_HANDLER(SUI_WM_MOUSE_UP,			suiPerfWindowHandleMouseUp);
		//SUI_WM_HANDLER(SUI_WM_MOUSE_MOVE,		suiPerfWindowHandleMouseMove);
		SUI_WM_HANDLER(SUI_WM_PROCESS,				suiPerfWindowHandleProcess);
		SUI_WM_HANDLER(SUI_WM_SIZE_CHANGED,			suiPerfWindowHandleSizeChanged);
	SUI_WM_HANDLERS_END;
	
	SUI_WM_HANDLERS_BEGIN(w, msg, suiScrollerGetMsgGroupID, PerfWindow, wd);
		SUI_WM_CASE(SUI_SCROLLER_MSG_POSITION_CHANGED){
			const SUIScrollerMsgPositionChanged* md = msg->msgData;
			
			wd->pos = md->pos;
			
			suiWindowInvalidate(w);
		}
	SUI_WM_HANDLERS_END;
	
	return 0;
}

S32 suiPerfWindowCreate(	SUIWindow** wOut,
							SUIWindow* wParent,
							const char* name)
{
	return suiWindowCreate(	wOut,
							wParent,
							suiPerfWindowMsgHandler,
							NULL);
}
#endif