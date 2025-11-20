/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "eventlogviewer.h"
#include "eventeditor.h"
#include "GameEvent.h"
#include "GameEvent_h_ast.h"
#include "GameEditorShared.h"
#include "eventlogviewer_c_ast.h"


#include "tokenstore.h"
#include "file.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#endif

// Need to have all AUTO_STRUCTS present even if NO_EDITORS in order to compile

AUTO_STRUCT;
typedef struct EventLogGroupBy{
	char *name;
	int parseTableIndex;
} EventLogGroupBy;

#ifndef NO_EDITORS

typedef struct EventLogGroupNode{
	int depth;			// depth in the tree
	GameEvent *ev;       // This is a dummy event used to store the Key field so that I can easily use TokenCopyField, etc.
	StashTable nodeChildren;  // Children of this GroupNode, index based on childPtiIndex
	GameEvent **eventChildren; // Child Events of this GroupNode.  Only present on the leaves
	
	int count; // Count of the Events under this expander.  Processed at the end.

	bool isOpen;
} EventLogGroupNode;

typedef struct EventLogFilter{
	// These are the Events used as a filter
	GameEvent **startEvents;
	GameEvent **stopEvents;
	GameEvent *countEvent;
	EventLogGroupBy **groupByList;
} EventLogFilter;

typedef struct EventLogViewer{
	// UI Elements
	UIWindow *window;
	UIWindow *fileSelectWindow;
	UIButton *fileSelectBtn;
	UILabel *fileLabel;
	UIButton *generateButton;

	UIExpanderGroup *filterExpanderGroup;
	UIExpander *startEventsExpander;
	UIExpander *stopEventsExpander;
	UIExpander *groupByExpander;
	GEList *startEventsUI;
	GEList *stopEventsUI;
	GEList *groupByUI;
	UIPane *countEventUI;

	// This filter is used to find statistics about the log
	EventLogFilter filter;

	// This is a list of Events from the logfile
	char *path;
	char *filename;
	GameEvent **loggedEvents;

	// This is where the results of the report go
	EventLogGroupNode rootResultsGroup;
	UIExpanderGroup *resultsExpanderGroup;

} EventLogViewer;

typedef struct EventLogViewerCBData{
	EventLogViewer *logviewer;
	GameEvent *ev;
	UIButton *button;
	EventEditor *editor;
} EventLogViewerCBData;

static EventLogGroupBy **s_GameEventFields = NULL;

static void eventlogviewer_GroupNodeDestroy(EventLogGroupNode *node)
{
	eaDestroy(&node->eventChildren);
	stashTableDestroyEx(node->nodeChildren, NULL, eventlogviewer_GroupNodeDestroy);
	StructDestroy(parse_GameEvent, node->ev);
	free(node);
}

static void eventlogviewer_GroupNodeClear(EventLogGroupNode *node)
{
	eaClear(&node->eventChildren);
	stashTableDestroyEx(node->nodeChildren, NULL, eventlogviewer_GroupNodeDestroy);
	node->isOpen = true;
	node->depth = 0;
}

#define EventFieldMatches(a) ((!listeningEvent->a)||((sentEvent->a) && !stricmp((listeningEvent->a),(sentEvent->a))))
#define EventFieldMatchesPooled(a) ((!listeningEvent->a)||(listeningEvent->a == sentEvent->a))
// -1 implies wildcard
#define EventFieldMatchesInt(a) (listeningEvent->a==-1 || listeningEvent->a==sentEvent->a)
static bool eventlogviewer_MatchEvents(GameEvent *listeningEvent, GameEvent *sentEvent)
{
	// BF - This is all so out-of-date that I'm not going to bother maintaining it for now
	// It will take some serious work to make this work again

	/*
	// TODO - this should use the same matching function as EventTracker!
	if (!listeningEvent || !sentEvent) return false;
	if (listeningEvent->type != sentEvent->type) return false;
	
	// Match the Source and Target
	if (!EventFieldMatches(pchSourceActorName)) return false;
	if (!EventFieldMatches(pchSourceCritterName)) return false;
	if (!EventFieldMatches(pchSourceCritterGroupName)) return false;
	if (!EventFieldMatches(pchSourceEncounterName)) return false;
	if (!EventFieldMatches(pchSourceStaticEncName)) return false;
	if (!EventFieldMatches(pchSourceObjectName)) return false;
	if (!EventFieldMatchesInt(eSourceRank)) return false;

	if (!EventFieldMatches(pchTargetActorName)) return false;
	if (!EventFieldMatches(pchTargetCritterName)) return false;
	if (!EventFieldMatches(pchTargetCritterGroupName)) return false;
	if (!EventFieldMatches(pchTargetEncounterName)) return false;
	if (!EventFieldMatches(pchTargetStaticEncName)) return false;
	if (!EventFieldMatches(pchTargetObjectName)) return false;
	if (!EventFieldMatchesInt(eTargetRank)) return false;

	if (!EventFieldMatches(pchFsmStateName)) return false;	
	if (!EventFieldMatches(pchClickableName)) return false;
	if (!EventFieldMatches(pchClickableGroupName)) return false;
	if (!EventFieldMatches(pchContactName)) return false;
	if (!EventFieldMatchesPooled(pchMissionRefString)) return false;
	if (!EventFieldMatches(pchItemName)) return false;
	if (!EventFieldMatches(pchCutsceneName)) return false;
	if (!EventFieldMatches(pchPowerName)) return false;
	if (!EventFieldMatches(pchPowerEventName)) return false;
	
	// Volumes
	if (sentEvent->type == EventType_VolumeEntered || sentEvent->type == EventType_VolumeExited)
	{
		if (!EventFieldMatches(pchVolumeName)) return false;
	}

	if (sentEvent->type == EventType_EncounterState && sentEvent->encState != listeningEvent->encState) return false;
	if (sentEvent->type == EventType_MissionState && sentEvent->missionState != listeningEvent->missionState) return false;
	if (sentEvent->type == EventType_HealthState && listeningEvent->healthState && sentEvent->healthState != listeningEvent->healthState) return false;
	*/
		
	return true;
}


static const void *eventlogviewer_GetKeyForEvent(GameEvent *ev, int parseTableIndex)
{
	ParseTable pt = parse_GameEvent[parseTableIndex];
	TextParserResult result = PARSERESULT_SUCCESS;
	switch (pt.type & TOK_TYPE_MASK)
	{
		case TOK_U8_X:
		case TOK_INT16_X: 
		case TOK_INT_X: 
		//case TOK_INT64_X: // support?
		{
			int val = TokenStoreGetIntAuto(parse_GameEvent, parseTableIndex, ev, 0, &result);
			if (result == PARSERESULT_SUCCESS)
				return S32_TO_PTR(val);
			else
				assertmsg(0, "Error parsing Event field!");
			break;
		}
		case TOK_STRING_X:
		{
			const char *string = TokenStoreGetString(parse_GameEvent, parseTableIndex, ev, 0, &result);
			if (result == PARSERESULT_SUCCESS)
				return string;
			else
				assertmsg(0, "Error parsing Event field!");
			break;
		}
		default:
		{
			assertmsg(0, "Programmer error: Unhandled token type!");
		}
	}

	return NULL;
}

static bool eventlogviewer_StashFindNode(StashTable table, const void *key, EventLogGroupNode** node, int parseTableIndex)
{
	ParseTable pt = parse_GameEvent[parseTableIndex];
	TextParserResult result = PARSERESULT_SUCCESS;
	switch (pt.type & TOK_TYPE_MASK)
	{
		case TOK_U8_X:
		case TOK_INT16_X: 
		case TOK_INT_X: 
		//case TOK_INT64_X: // support?
		{
			return stashIntFindPointer(table, PTR_TO_S32(key), node);
			break;
		}
		case TOK_STRING_X:
		{
			return stashFindPointer(table, key, node);
			break;
		}
		default:
		{
			assertmsg(0, "Programmer error: Unhandled token type!");
		}
	}

	return false;
}

static bool eventlogviewer_StashAddNode(StashTable table, const void *key, EventLogGroupNode* node, int parseTableIndex)
{
	ParseTable pt = parse_GameEvent[parseTableIndex];
	TextParserResult result = PARSERESULT_SUCCESS;
	switch (pt.type & TOK_TYPE_MASK)
	{
		case TOK_U8_X:
		case TOK_INT16_X: 
		case TOK_INT_X: 
		//case TOK_INT64_X: // support?
		{
			return stashIntAddPointer(table, PTR_TO_S32(key), node, false);
			break;
		}
		case TOK_STRING_X:
		{
			return stashAddPointer(table, key, node, false);
			break;
		}
		default:
		{
			assertmsg(0, "Programmer error: Unhandled token type!");
		}
	}

	return false;
}

static EventLogGroupNode *eventlogviewer_FindOpenNodeForEvent(EventLogViewer *viewer, EventLogGroupNode *rootNode, GameEvent *ev)
{
	if (rootNode->depth == eaSize(&viewer->filter.groupByList))
	{
		return rootNode;
	}
	else if (rootNode->depth < eaSize(&viewer->filter.groupByList) && rootNode->depth >= 0)
	{
		EventLogGroupNode *nextChild = NULL;
		int parseTableIndex = viewer->filter.groupByList[rootNode->depth]->parseTableIndex;
		const void *key = eventlogviewer_GetKeyForEvent(ev, parseTableIndex);
		if (key && eventlogviewer_StashFindNode(rootNode->nodeChildren, key, &nextChild, parseTableIndex))
		{
			if (nextChild && nextChild->isOpen)
				return eventlogviewer_FindOpenNodeForEvent(viewer, nextChild, ev);
		}
	}
	return NULL;
}

static StashTable eventlogviewer_CreateStashTableForEventField(int parseTableIndex)
{
	ParseTable pt = parse_GameEvent[parseTableIndex];
	switch (pt.type & TOK_TYPE_MASK)
	{
		case TOK_U8_X:
		case TOK_INT16_X: 
		case TOK_INT_X: 
		//case TOK_INT64_X: // support?
		{
			return stashTableCreateInt(100);
			break;
		}
		case TOK_STRING_X:
		{
			return stashTableCreateWithStringKeys(100, 0);
			break;
		}
		default:
		{
			assertmsg(0, "Programmer error: Unhandled token type!");
		}
	}
}

static EventLogGroupNode *eventlogviewer_CreateNode(EventLogViewer *viewer, int depth, GameEvent *ev, int parseTableIndex)
{
	EventLogGroupNode *newNode = calloc(1, sizeof(EventLogGroupNode));
	newNode->depth = depth;
	if (0 <= depth && depth < eaSize(&viewer->filter.groupByList))
		newNode->nodeChildren = eventlogviewer_CreateStashTableForEventField(viewer->filter.groupByList[depth]->parseTableIndex);
	newNode->isOpen = true;
	newNode->ev = StructCreate(parse_GameEvent);
	TokenCopy(parse_GameEvent, parseTableIndex, newNode->ev, ev, 0);
	return newNode;
}

static EventLogGroupNode *eventlogviewer_OpenNodeForEvent(EventLogViewer *viewer, EventLogGroupNode *rootNode, GameEvent *ev)
{
	if (rootNode && rootNode->depth == eaSize(&viewer->filter.groupByList))
	{
		return rootNode;
	}
	else if (rootNode && rootNode->depth < eaSize(&viewer->filter.groupByList) && rootNode->depth >= 0)
	{
		EventLogGroupNode *nextChild = NULL;
		int parseTableIndex = viewer->filter.groupByList[rootNode->depth]->parseTableIndex;
		const void *key = eventlogviewer_GetKeyForEvent(ev, parseTableIndex);
		if (key)
		{
			eventlogviewer_StashFindNode(rootNode->nodeChildren, key, &nextChild, parseTableIndex);
			if (!nextChild)
			{
				nextChild = eventlogviewer_CreateNode(viewer, rootNode->depth+1, ev, parseTableIndex);
				if (nextChild)
					eventlogviewer_StashAddNode(rootNode->nodeChildren, key, nextChild, parseTableIndex);
			}
			if (nextChild && !nextChild->isOpen)
				nextChild->isOpen = true;
			if (nextChild)
				return eventlogviewer_OpenNodeForEvent(viewer, nextChild, ev);
		}
	}
	return NULL;
}

static void eventlogviewer_CloseNodeForEvent(EventLogViewer *viewer, EventLogGroupNode *rootNode, GameEvent *ev)
{
	if (rootNode->depth == eaSize(&viewer->filter.groupByList))
	{
		rootNode->isOpen = false;
	}
	else if (rootNode->depth < eaSize(&viewer->filter.groupByList) && rootNode->depth >= 0)
	{
		EventLogGroupNode *nextChild = NULL;
		int parseTableIndex = viewer->filter.groupByList[rootNode->depth]->parseTableIndex;
		const void *key = eventlogviewer_GetKeyForEvent(ev, parseTableIndex);
		if (key)
		{
			eventlogviewer_StashFindNode(rootNode->nodeChildren, key, &nextChild, parseTableIndex);
			if (nextChild && nextChild->isOpen)
				eventlogviewer_CloseNodeForEvent(viewer, nextChild, ev);
		}
	}
}

static void eventlogviewer_CreateExpanderForNode(EventLogViewer *viewer, EventLogGroupNode *groupNode, UIExpanderGroup *expanderGroup)
{
	StashTableIterator iterator;
	StashElement element;
	UIExpander *expander = expander = ui_ExpanderCreate("", 0);
	UIExpanderGroup *childGroup = ui_ExpanderGroupCreate();
	int i, n;
	char *estrHeader = NULL;
	char *estrExpanderTitle = NULL;
	estrStackCreate(&estrHeader);
	estrStackCreate(&estrExpanderTitle);

	// Set up name of the expander
	if (groupNode->ev && groupNode->depth > 0)
		TokenWriteText(parse_GameEvent, viewer->filter.groupByList[groupNode->depth-1]->parseTableIndex, groupNode->ev, &estrHeader, true);
	else
		estrCopy2(&estrHeader, StaticDefineIntRevLookup(EventTypeEnum, viewer->filter.countEvent->type));

	groupNode->count = 0;
	
	expander->widget.x = 20;
	ui_ExpanderGroupAddExpander(expanderGroup, expander);
	
	ui_ExpanderAddChild(expander, childGroup);
	ui_ExpanderGroupSetReflowCallback(childGroup, GENestedExpanderReflowCB, expander);
	ui_WidgetSetDimensionsEx((UIWidget*)childGroup, 1.0f, 1.0f, UIUnitPercentage, UIUnitPercentage);
	ui_ExpanderGroupSetGrow(childGroup, true);

	stashGetIterator(groupNode->nodeChildren, &iterator);
	while (stashGetNextElement(&iterator, &element))
	{
		EventLogGroupNode *childNode = stashElementGetPointer(element);
		eventlogviewer_CreateExpanderForNode(viewer, childNode, childGroup);
		groupNode->count += childNode->count;
	}

	n = eaSize(&groupNode->eventChildren);
	groupNode->count += n;
	for (i = 0; i < n; i++)
	{
		UILabel *label = NULL;
		char *eventString = NULL;
		estrStackCreate(&eventString);
		gameevent_WriteEventEscaped(groupNode->eventChildren[i], &eventString);
		label = ui_LabelCreate(eventString, 20, 0);
		ui_ExpanderGroupAddWidget(childGroup, UI_WIDGET(label));
		estrDestroy(&eventString);
	}
	ui_ExpanderGroupReflow(childGroup);
	ui_ExpanderGroupReflow(expanderGroup);

	estrPrintf(&estrExpanderTitle, "%s (%d)", estrHeader, groupNode->count);
	ui_WidgetSetTextString(UI_WIDGET(expander), estrExpanderTitle);
	estrDestroy(&estrHeader);
	estrDestroy(&estrExpanderTitle);
}

static void eventlogviewer_GenerateResults(UIButton *button, EventLogViewer *viewer)
{
	int iEvent, numEvents = eaSize(&viewer->loggedEvents);

	// Clear old results
	eventlogviewer_GroupNodeClear(&viewer->rootResultsGroup);
	if (eaSize(&viewer->filter.groupByList))
		viewer->rootResultsGroup.nodeChildren = eventlogviewer_CreateStashTableForEventField(viewer->filter.groupByList[0]->parseTableIndex);
	viewer->rootResultsGroup.isOpen = true;

	eaClearEx(&viewer->resultsExpanderGroup->widget.children, ui_WidgetQueueFree);
	eaClear(&viewer->resultsExpanderGroup->childrenInOrder);

	for (iEvent = 0; iEvent < numEvents; iEvent++)
	{
		GameEvent *logEvent = viewer->loggedEvents[iEvent];
		EventLogGroupNode *openNode = eventlogviewer_FindOpenNodeForEvent(viewer, &viewer->rootResultsGroup, logEvent);
		if (!openNode)
		{
			int i, n = eaSize(&viewer->filter.startEvents);
			for (i = 0; i < n; i++)
			{
				if(eventlogviewer_MatchEvents(viewer->filter.startEvents[i], logEvent))
				{
					openNode = eventlogviewer_OpenNodeForEvent(viewer, &viewer->rootResultsGroup, logEvent);
					break;
				}
			}
		}
		
		if (openNode || !eaSize(&viewer->filter.startEvents))
		{
			int i, n;
			if (eventlogviewer_MatchEvents(viewer->filter.countEvent, logEvent))
			{
				if (!openNode)
					openNode = eventlogviewer_OpenNodeForEvent(viewer, &viewer->rootResultsGroup, logEvent);
				if (openNode)
					eaPush(&openNode->eventChildren, logEvent);
			}
			
			// TODO - if there's no start Event, this doesn't work right.  
			// There needs to be some way for these to stop future Events.
			// Maybe it makes a node, then closes it.
			n = eaSize(&viewer->filter.stopEvents);
			for (i = 0; i < n; i++)
			{
				if(eventlogviewer_MatchEvents(viewer->filter.stopEvents[i], logEvent))
				{
					eventlogviewer_CloseNodeForEvent(viewer, openNode, logEvent);
					break;
				}
			}
		}
	}

	// Create UI
	eventlogviewer_CreateExpanderForNode(viewer, &viewer->rootResultsGroup, viewer->resultsExpanderGroup);

}

static void eventlogviewer_OpenFileCB(const char *path, const char *filename, EventLogViewer *logviewer)
{
	FILE *file;
	char *fullFilename = NULL;
	char buffer[2048];
	estrStackCreate(&fullFilename);
	logviewer->path = strdup(path);
	logviewer->filename = strdup(filename);
	ui_LabelSetText(logviewer->fileLabel, filename);
	estrPrintf(&fullFilename, "%s\\%s", logviewer->path, logviewer->filename);
	file = fileOpen(fullFilename, "r");

	if(file)
	{
		eaClearStruct(&logviewer->loggedEvents, parse_GameEvent);
		while(fgets(buffer, 2048, file))
		{
			GameEvent *ev = NULL;
			char *eventStr = strstr(buffer, "Event(");
			char *eventEnd = buffer + strlen(buffer);
			if (eventStr)
			{
				eventStr += strlen("Event(");
				while (eventEnd > eventStr && *eventEnd != '}')
					eventEnd--;
				*(eventEnd+1) = '\0';
				ev = gameevent_EventFromString(eventStr);
				ev->pchEventName = 0;
				if (ev)
					eaPush(&logviewer->loggedEvents, ev);
			}
		}
		fileClose(file);
	}

	estrDestroy(&fullFilename);
}

static void eventlogviewer_OpenFileButtonClicked(UIButton *button, EventLogViewer *logviewer)
{
	// free the old window if it's still around
	logviewer->fileSelectWindow = ui_FileBrowserCreate("Select Logfile", "Open", UIBrowseExisting, UIBrowseFiles, false, "logs", "logs", NULL, ".log", NULL, NULL, eventlogviewer_OpenFileCB, logviewer);
	ui_WindowShow(logviewer->fileSelectWindow);
}

static bool eventlogviewer_Close(UIWindow *window, EventLogViewer *logviewer)
{
	eventlogviewer_Destroy(logviewer);
	return true;
}

static GameEvent *eventlogviewer_CreateEvent(void* unused)
{
	return StructCreate(parse_GameEvent);
}

static void eventlogviewer_DestroyEvent(GameEvent *ev, void* unused)
{
	StructDestroy(parse_GameEvent, ev);
}

static void eventlogviewer_HeightChangedCB(GEList *list, F32 newHeight, EventLogViewer *logviewer)
{
	if (list == logviewer->startEventsUI || !logviewer->startEventsUI)
		ui_ExpanderSetHeight(logviewer->startEventsExpander, newHeight);
	else if (list == logviewer->stopEventsUI || !logviewer->stopEventsUI)
		ui_ExpanderSetHeight(logviewer->stopEventsExpander, newHeight);
	else if (list == logviewer->groupByUI || !logviewer->groupByUI)
		ui_ExpanderSetHeight(logviewer->groupByExpander, newHeight);
	ui_ExpanderGroupReflow(logviewer->filterExpanderGroup);
}

static void eventlogviewer_EventEditorCloseCB(EventEditor *editor, EventLogViewerCBData *cbData)
{
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	eventeditor_GetEventStringEscaped(editor, &estrBuffer);
	ui_ButtonSetText(cbData->button, estrBuffer);
	estrDestroy(&estrBuffer);
	eventeditor_Destroy(editor);
	cbData->editor = NULL;
}

static void eventlogviewer_CreateEventButtonCB(UIButton *button, EventLogViewerCBData *cbData)
{
	if (cbData && cbData->ev)
	{
		EventEditor *editor = eventeditor_Create(cbData->ev, eventlogviewer_EventEditorCloseCB, cbData, false);
		eventeditor_Open(editor);
		cbData->editor = editor;
	}
}

static void eventlogviewer_EventButtonFreeCB(UIButton *button)
{
	EventLogViewerCBData *cbData = button->clickedData;
	if (cbData->editor)
	{
		eventeditor_Destroy(cbData->editor);
	}
	free(cbData);
}

static F32 eventlogviewer_CreateEventUI(GEListItem *listItem, UIWidget*** widgetList, GameEvent *ev, F32 x, F32 y, UIAnyWidget* widgetParent, EventLogViewer *logviewer)
{
	F32 currY = y;
	F32 currX = x;
	UIButton *eventEditorButton = NULL;
	EventLogViewerCBData *cbData = calloc(1, sizeof(EventLogViewerCBData));
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	gameevent_WriteEventEscaped(ev, &estrBuffer);

	cbData->ev = ev;
	cbData->logviewer = logviewer;

	eventEditorButton = ui_ButtonCreate(estrBuffer, currX, currY, eventlogviewer_CreateEventButtonCB, cbData);
	ui_WidgetSetWidthEx(UI_WIDGET(eventEditorButton), 1.0f, UIUnitPercentage);
	ui_WidgetSetFreeCallback(UI_WIDGET(eventEditorButton), eventlogviewer_EventButtonFreeCB);
	currY += eventEditorButton->widget.height;
	cbData->button = eventEditorButton;

	if (widgetList)
		eaPush(widgetList, UI_WIDGET(eventEditorButton));

	if (widgetParent)
		ui_WidgetAddChild((UIWidget*)widgetParent, UI_WIDGET(eventEditorButton));

	estrDestroy(&estrBuffer);
	return currY + GE_SPACE;
}

static void eventlogviewer_GroupByComboChanged(UIComboBox *comboBox, EventLogGroupBy *groupBy)
{
	EventLogGroupBy *selectedGroupBy = (EventLogGroupBy*)ui_ComboBoxGetSelectedObject(comboBox);
	if (selectedGroupBy)
	{
		groupBy->name = selectedGroupBy->name;
		groupBy->parseTableIndex = selectedGroupBy->parseTableIndex;
	}
}

static F32 eventlogviewer_CreateGroupByUI(GEListItem *listItem, UIWidget*** widgetList, EventLogGroupBy *groupBy, F32 x, F32 y, UIAnyWidget* widgetParent, EventLogViewer *logviewer)
{
	F32 currY = y;
	F32 currX = x;
	UIComboBox *comboBox = NULL;

	comboBox = ui_ComboBoxCreate(currX, currY, 200, parse_EventLogGroupBy, &s_GameEventFields, "name");
	currY += comboBox->widget.height;
	ui_ComboBoxSetSelectedCallback(comboBox, eventlogviewer_GroupByComboChanged, groupBy);

	if (widgetList)
		eaPush(widgetList, UI_WIDGET(comboBox));

	if (widgetParent)
		ui_WidgetAddChild((UIWidget*)widgetParent, UI_WIDGET(comboBox));

	return currY + GE_SPACE;
}

static EventLogGroupBy* eventlogviewer_CreateGroupBy(void* unused)
{
	return calloc(1, sizeof(EventLogGroupBy));	
}

static void eventlogviewer_DestroyGroupBy(EventLogGroupBy *groupBy, void *unused)
{
	free(groupBy);
}

// Create the EventLogViewer window for the first time
EventLogViewer* eventlogviewer_Create(void)
{
	EventLogViewer *logviewer = calloc(1, sizeof(EventLogViewer));
	int width = 1000;
	int height = 600;
	int currY = GE_SPACE;
	UIWidget *widget;
	UIPane *resultsPane;
	UISeparator *pSeparator = NULL;

	logviewer->window = ui_WindowCreate("Event Log Viewer", 50, 50, width, height);
	logviewer->window->widget.scale = 0.8f;
	ui_WindowSetCloseCallback(logviewer->window, eventlogviewer_Close, logviewer);
	ui_WindowShow(logviewer->window);

	// Set up the Open File button
	logviewer->fileSelectBtn = ui_ButtonCreate("Open File", 0, currY, eventlogviewer_OpenFileButtonClicked, logviewer);
	ui_WindowAddChild(logviewer->window, logviewer->fileSelectBtn);
	logviewer->fileLabel = ui_LabelCreate("", logviewer->fileSelectBtn->widget.x + logviewer->fileSelectBtn->widget.width + GE_SPACE, currY);
	ui_WindowAddChild(logviewer->window, logviewer->fileLabel);
	
	currY += logviewer->fileSelectBtn->widget.height + GE_SPACE;

	// Button
	logviewer->generateButton = ui_ButtonCreate("Generate", 0, currY, eventlogviewer_GenerateResults, logviewer);
	ui_WindowAddChild(logviewer->window, logviewer->generateButton);
	currY += logviewer->generateButton->widget.height + GE_SPACE;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, currY);
	ui_WindowAddChild(logviewer->window, UI_WIDGET(pSeparator));
	currY += GE_SPACE;

	// Set up Filter expander group
	logviewer->filterExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition((UIWidget*)logviewer->filterExpanderGroup, 0, currY);
	ui_WidgetSetDimensionsEx((UIWidget*)logviewer->filterExpanderGroup, 0.3f, 1.0f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(logviewer->window, logviewer->filterExpanderGroup);

	// Count Event
	logviewer->countEventUI = ui_PaneCreate(0, 0, 1.0f, 0, UIUnitPercentage, UIUnitFixed, 0);
	logviewer->countEventUI->invisible = true;
	ui_ExpanderGroupAddWidget(logviewer->filterExpanderGroup, UI_WIDGET(logviewer->countEventUI));
	logviewer->filter.countEvent = StructCreate(parse_GameEvent);
	ui_WidgetAddChild(UI_WIDGET(logviewer->countEventUI), widget = (UIWidget*)ui_LabelCreate("Count This Event", 0, 0));
	currY = widget->height + GE_SPACE;
	currY = eventlogviewer_CreateEventUI(NULL, NULL, logviewer->filter.countEvent, 0, currY, logviewer->countEventUI, logviewer);
	logviewer->countEventUI->widget.height = currY;

	// Start events
	logviewer->startEventsExpander = ui_ExpanderCreate("Begin At", 0);
	ui_ExpanderGroupAddExpander(logviewer->filterExpanderGroup, logviewer->startEventsExpander);
	logviewer->startEventsUI = GEListCreate(&logviewer->filter.startEvents, 0, 0, logviewer->startEventsExpander, logviewer, eventlogviewer_CreateEventUI, eventlogviewer_CreateEvent, eventlogviewer_DestroyEvent, eventlogviewer_HeightChangedCB, false, false);

	// End events
	logviewer->stopEventsExpander = ui_ExpanderCreate("End At", 0);
	ui_ExpanderGroupAddExpander(logviewer->filterExpanderGroup, logviewer->stopEventsExpander);
	logviewer->stopEventsUI = GEListCreate(&logviewer->filter.stopEvents, 0, 0, logviewer->stopEventsExpander, logviewer, eventlogviewer_CreateEventUI, eventlogviewer_CreateEvent, eventlogviewer_DestroyEvent, eventlogviewer_HeightChangedCB, false, false);

	// Group By expander
	logviewer->groupByExpander = ui_ExpanderCreate("Group By", 0);
	ui_ExpanderGroupAddExpander(logviewer->filterExpanderGroup, logviewer->groupByExpander);
	logviewer->groupByUI = GEListCreate(&logviewer->filter.groupByList, 0, 0, logviewer->groupByExpander, logviewer, eventlogviewer_CreateGroupByUI, eventlogviewer_CreateGroupBy, eventlogviewer_DestroyGroupBy, eventlogviewer_HeightChangedCB, true, false);
	
	ui_ExpanderGroupReflow(logviewer->filterExpanderGroup);

	// Results pane
	resultsPane = ui_PaneCreate(0, 0, 0.7f, 1.0f, UIUnitPercentage, UIUnitPercentage, 0);
	resultsPane->widget.xPOffset = 0.3f;
	//resultsPane->invisible = true;
	ui_WindowAddChild(logviewer->window, resultsPane);
	logviewer->resultsExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetAddChild(UI_WIDGET(resultsPane), UI_WIDGET(logviewer->resultsExpanderGroup));
	ui_WidgetSetDimensionsEx((UIWidget*)logviewer->resultsExpanderGroup, 1.0f, 1.0f, UIUnitPercentage, UIUnitPercentage);

	return logviewer;
}

void eventlogviewer_Destroy(EventLogViewer *logviewer)
{
	eaDestroyStruct(&logviewer->loggedEvents, parse_GameEvent);
	eaDestroyStruct(&logviewer->filter.startEvents, parse_GameEvent);
	eaDestroyStruct(&logviewer->filter.stopEvents, parse_GameEvent);
	StructDestroy(parse_GameEvent, logviewer->filter.countEvent);
	ui_WidgetQueueFree(UI_WIDGET(logviewer->window));
	free(logviewer->path);
	free(logviewer->filename);
	free(logviewer);
}

#endif

AUTO_RUN;
void eventlogviewer_InitGameEventFieldList(void)
{
#ifndef NO_EDITORS
	int i;
	FORALL_PARSETABLE(parse_GameEvent, i)
	{
		int tokentype = parse_GameEvent[i].type & TOK_TYPE_MASK; 
		if (tokentype == TOK_U8_X || tokentype == TOK_INT16_X || tokentype == TOK_INT_X || tokentype == TOK_STRING_X)
		{
			EventLogGroupBy *groupBy = StructCreate(parse_EventLogGroupBy);
			groupBy->name = StructAllocString(parse_GameEvent[i].name);
			groupBy->parseTableIndex = i;
			eaPush(&s_GameEventFields, groupBy);
		}
	}
#endif
}

#include "eventlogviewer_c_ast.c"

