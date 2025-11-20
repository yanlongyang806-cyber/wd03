/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "encounter_common.h"
#include "gameeditorshared.h"
#include "aijobeditor.h"
#include "StateMachine.h"
#include "AutoGen/StateMachine_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define JE_LABEL_WIDTH 75

typedef struct JobEditorJobUI JobEditorJobUI;

static void jobeditor_DestroyJobUI(JobEditorJobUI *jobUI);

typedef struct JobEditor{
	// UI Elements
	UIWindow *window;
	UIScrollArea *scrollArea;
	UIButton *saveButton;
	UIButton *cancelButton;
	JobEditorJobUI **jobUIList;
	
	// Data
	AIJobDesc *jobDesc;  // Actual job description being edited
	const AIJobDesc *origJobDesc;  // Pointer to the original job description

	// Callbacks
	JobEditorChangeFunc changeFunc;
	void *userData;
} JobEditor;

// This struct is for the subwindow containing the actual job
typedef struct JobEditorJobUI{
	JobEditor *parentEditor;
	JobEditorJobUI *parent;

	UIWindow *window;
	UITextEntry *nameEntry;
	UITextEntry *priorityEntry;
	UIFilteredList *fsmEntry;
	UIButton *jobRequiresEntry;
	UIButton *jobRatingEntry;
	
	AIJobDesc *jobDesc;
} JobEditorJobUI;

// This keeps track of existing job editors so that you don't accidentally open two editors for
// the same job.
StashTable s_ExistingJobEditors = NULL;

static bool jobeditor_CloseWindow(UIWindow *window, JobEditor *editor)
{
	jobeditor_Destroy(editor);
	return true;
}

static void jobeditor_Save(void *unused, JobEditor *editor)
{
	if (editor->changeFunc)
		editor->changeFunc(editor, (AIJobDesc*)editor->origJobDesc, editor->jobDesc, editor->userData);
	jobeditor_Destroy(editor);
}

static void jobeditor_Cancel(void *unused, JobEditor *editor)
{
	jobeditor_Destroy(editor);
}

static bool jobeditor_CloseSubWindow(UIWindow *window, JobEditorJobUI *jobUI)
{
	JobEditor *editor = jobUI->parentEditor;
	eaFindAndRemove(&editor->jobUIList, jobUI);
	jobeditor_DestroyJobUI(jobUI);
	return true;
}

static void jobeditor_UpdateName(UITextEntry *textEntry, JobEditorJobUI *jobUI)
{
	const char *name = ui_TextEntryGetText(textEntry);
	jobUI->jobDesc->jobName = StructAllocString(name);
	ui_WindowSetTitle(jobUI->window, name);
	if (jobUI->jobDesc == jobUI->parentEditor->jobDesc)
		ui_WindowSetTitle(jobUI->parentEditor->window, name);
}

static void jobeditor_UpdateFSM(UITextEntry *textEntry, JobEditorJobUI *jobUI)
{
	const char *fsmName = ui_TextEntryGetText(textEntry);
	jobUI->jobDesc->fsmName = StructAllocString(fsmName);
}

static void jobeditor_UpdatePriority(UITextEntry *textEntry, JobEditorJobUI *jobUI)
{
	F32 priority = atof(ui_TextEntryGetText(textEntry));
	jobUI->jobDesc->priority = priority;
}

static void jobeditor_UpdateRequiresExpr(Expression *expression, JobEditorJobUI *jobUI)
{
	//char* exprString = exprGetCompleteString(expression);
	GEUpdateExpressionFromExpression(&jobUI->jobDesc->jobRequires, expression);
	//ui_ButtonSetText(jobUI->jobRequiresEntry, exprString);
}

static void jobeditor_UpdateRatingExpr(Expression *expression, JobEditorJobUI *jobUI)
{
	//char* exprString = exprGetCompleteString(expression);
	GEUpdateExpressionFromExpression(&jobUI->jobDesc->jobRating, expression);
	//ui_ButtonSetText(jobUI->jobRequiresEntry, exprString);
}

static JobEditorJobUI *jobeditor_CreateJobUI(JobEditor *editor, AIJobDesc *job, JobEditorJobUI *parentUI)
{
	F32 currY = 0;
	char buffer[128];
	JobEditorJobUI *jobUI = calloc(1, sizeof(JobEditorJobUI));
	jobUI->jobDesc = job;
	jobUI->parent = parentUI;
	jobUI->parentEditor = editor;
	
	// Setup the window
	jobUI->window = ui_WindowCreate(job->jobName, 20, 20, 250, 250);
	ui_ScrollAreaAddChild(editor->scrollArea, (UIWidget*)jobUI->window);
	ui_WindowSetCloseCallback(jobUI->window, jobeditor_CloseSubWindow, jobUI);
	if (!parentUI)
		ui_WindowSetClosable(jobUI->window, false);

	// Set up the UI elements
	currY = GETextEntryCreate(&jobUI->nameEntry, "Name", job->jobName, 0, currY, 200, 0, JE_LABEL_WIDTH, GE_VALIDFUNC_NOSPACE, jobeditor_UpdateName, jobUI, jobUI->window);
	ui_WidgetSetWidthEx(UI_WIDGET(jobUI->nameEntry), 1.0f, UIUnitPercentage);
	
	sprintf(buffer, "%.2f", job->priority);
	currY = GETextEntryCreate(&jobUI->priorityEntry, "Priority", buffer, 0, currY, 50, 0, JE_LABEL_WIDTH, GE_VALIDFUNC_FLOAT, jobeditor_UpdatePriority, jobUI, jobUI->window) + GE_SPACE;
	
	currY = GETextEntryCreateEx(&jobUI->fsmEntry, NULL, "FSM", jobUI->jobDesc->fsmName, NULL, gFSMDict, NULL, parse_FSM, "Name:", 0, currY, 200, 0, JE_LABEL_WIDTH, 0, jobeditor_UpdateFSM, jobUI, jobUI->window) + GE_SPACE;
	ui_WidgetSetWidthEx(UI_WIDGET(jobUI->fsmEntry), 1.0f, UIUnitPercentage);
	
	currY = GEExpressionEditButtonCreate(&jobUI->jobRequiresEntry, NULL, "Requires", jobUI->jobDesc->jobRequires, 0, currY, 200, JE_LABEL_WIDTH, jobeditor_UpdateRequiresExpr, jobUI, jobUI->window) + GE_SPACE;
	ui_WidgetSetWidthEx(UI_WIDGET(jobUI->jobRequiresEntry), 1.0f, UIUnitPercentage);
	
	currY = GEExpressionEditButtonCreate(&jobUI->jobRatingEntry, NULL, "Rating", jobUI->jobDesc->jobRating, 0, currY, 200, JE_LABEL_WIDTH, jobeditor_UpdateRatingExpr, jobUI, jobUI->window) + GE_SPACE;
	ui_WidgetSetWidthEx(UI_WIDGET(jobUI->jobRatingEntry), 1.0f, UIUnitPercentage);
	
	ui_ScrollAreaAddChild(editor->scrollArea, UI_WIDGET(jobUI->window));
	eaPush(&editor->jobUIList, jobUI);
	return jobUI;
}

static void jobeditor_DestroyJobUI(JobEditorJobUI *jobUI)
{
	ui_WidgetQueueFree(UI_WIDGET(jobUI->window));
	free(jobUI);
}

JobEditor *jobeditor_Create(const AIJobDesc *job, JobEditorChangeFunc changeFunc, void* userData)
{
	JobEditor *editor = NULL;
	if (!(s_ExistingJobEditors && stashFindPointer(s_ExistingJobEditors, job, &editor)))
	{
		editor = calloc(1, sizeof(JobEditor));
		editor->origJobDesc = job;
		editor->jobDesc = StructClone(parse_AIJobDesc, (void*)job);
		editor->changeFunc = changeFunc;
		editor->userData = userData;

		editor->window = ui_WindowCreate(job->jobName, 100, 100, 500, 500); // TODO - user prefs
		ui_WindowSetCloseCallback(editor->window, jobeditor_CloseWindow, editor);

		// Set-up UI elements
		editor->cancelButton = ui_ButtonCreate("Cancel", 0, 0, jobeditor_Cancel, editor);
		ui_WidgetSetPositionEx(UI_WIDGET(editor->cancelButton), GE_SPACE, GE_SPACE, 0, 0, UIBottomRight);
		ui_WindowAddChild(editor->window, editor->cancelButton);
		editor->saveButton = ui_ButtonCreate("Save", 0, 0, jobeditor_Save, editor);
		ui_WidgetSetPositionEx(UI_WIDGET(editor->saveButton), GE_SPACE*2+editor->cancelButton->widget.width, GE_SPACE, 0, 0, UIBottomRight);
		ui_WindowAddChild(editor->window, editor->saveButton);

		editor->scrollArea = ui_ScrollAreaCreate(0, 0, 0, 0, 4000, 4000, true, true);
		ui_WidgetSetPositionEx(UI_WIDGET(editor->scrollArea), 0, GE_SPACE*2+editor->cancelButton->widget.height, 0, 0, UIBottomRight);
		ui_WidgetSetDimensionsEx((UIWidget*)editor->scrollArea, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
		ui_WindowAddChild(editor->window, editor->scrollArea);

		// Set-up subwindow for the root Job
		jobeditor_CreateJobUI(editor, editor->jobDesc, NULL);

		// Add job editor to the stashtable
		if (!s_ExistingJobEditors)
			s_ExistingJobEditors = stashTableCreateAddress(10);
		stashAddPointer(s_ExistingJobEditors, job, editor, false);
	}
	ui_WindowShow(editor->window);
	return editor;
}

void jobeditor_Destroy(JobEditor* editor)
{
	eaDestroyEx(&editor->jobUIList, jobeditor_DestroyJobUI);
	stashRemovePointer(s_ExistingJobEditors, editor->origJobDesc, NULL);
	ui_WidgetQueueFree(UI_WIDGET(editor->window));
	free(editor);
}

void jobeditor_DestroyForJob(const AIJobDesc *job)
{
	JobEditor *editor = NULL;
	if (s_ExistingJobEditors && stashFindPointer(s_ExistingJobEditors, job, &editor))
		jobeditor_Destroy(editor);
}

UIWindow* jobeditor_GetWindow(JobEditor *editor)
{
	return editor->window;
}

#endif