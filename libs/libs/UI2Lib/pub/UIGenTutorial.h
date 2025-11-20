/***************************************************************************



***************************************************************************/

#pragma once

#include "ReferenceSystem.h"

typedef struct UIGen UIGen;
typedef struct UIGenInternal UIGenInternal;
typedef struct UIGenAction UIGenAction;
typedef struct Message Message;
typedef struct Expression Expression;

AUTO_ENUM;
typedef enum UIGenTutorialSelectType
{
	kUIGenTutorialSelectTypeNone = 0,
	kUIGenTutorialSelectTypeChild,
	kUIGenTutorialSelectTypeListRow,
	kUIGenTutorialSelectTypeListColumn,
	kUIGenTutorialSelectTypeInstance,
	kUIGenTutorialSelectTypeTab,
	kUIGenTutorialSelectTypeJail,
} UIGenTutorialSelectType;

AUTO_ENUM;
typedef enum UIGenTutorialEventType
{
	kUIGenTutorialEventTypeNone,
	kUIGenTutorialEventTypeClick,
	kUIGenTutorialEventTypeSelect,
	kUIGenTutorialEventTypeManualNext,
} UIGenTutorialEventType;

AUTO_STRUCT;
typedef struct UIGenTutorialSelect
{
	UIGenTutorialSelectType eType;				AST(POLYPARENTTYPE(0))

	// The current filename
	const char *pchFilename;					AST(CURRENTFILE)

	// The current line number
	int iLineNumber;							AST(LINENUM)
} UIGenTutorialSelect;

AUTO_STRUCT;
typedef struct UIGenTutorialSelectChild
{
	UIGenTutorialSelect polyp;				AST(POLYCHILDTYPE(kUIGenTutorialSelectTypeChild))

	// The name of the child gen (usually InlineChild)
	const char *pchChildName;					AST(NAME(Child) POOL_STRING STRUCTPARAM REQUIRED)
} UIGenTutorialSelectChild;

AUTO_STRUCT;
typedef struct UIGenTutorialSelectListColumn
{
	UIGenTutorialSelect polyp;				AST(POLYCHILDTYPE(kUIGenTutorialSelectTypeListColumn))

	// The name of the list column
	const char *pchColumnName;					AST(NAME(Column) POOL_STRING STRUCTPARAM)
} UIGenTutorialSelectListColumn;

AUTO_STRUCT;
typedef struct UIGenTutorialSelectListRow
{
	UIGenTutorialSelect polyp;				AST(POLYCHILDTYPE(kUIGenTutorialSelectTypeListRow))

	// Select all rows that pass the filter
	Expression *pFilter;						AST(NAME(FilterBlock) REDUNDANT_STRUCT(FilterExpr, parse_Expression_StructParam) LATEBIND STRUCTPARAM)

	// Select a specifically named list row (usually for template children)
	const char **eapchName;						AST(NAME(Name) POOL_STRING)
} UIGenTutorialSelectListRow;

AUTO_STRUCT;
typedef struct UIGenTutorialSelectInstance
{
	UIGenTutorialSelect polyp;				AST(POLYCHILDTYPE(kUIGenTutorialSelectTypeInstance))

	// Select all layout box instances that pass the filter
	Expression *pFilter;						AST(NAME(FilterBlock) REDUNDANT_STRUCT(FilterExpr, parse_Expression_StructParam) LATEBIND STRUCTPARAM)

	// Select a specifically named list row (usually for template children)
	const char **eapchName;						AST(NAME(Name) POOL_STRING)
} UIGenTutorialSelectInstance;

AUTO_STRUCT;
typedef struct UIGenTutorialSelectTab
{
	UIGenTutorialSelect polyp;				AST(POLYCHILDTYPE(kUIGenTutorialSelectTypeTab))

	// Select all tabs that pass the filter
	Expression *pFilter;						AST(NAME(FilterBlock) REDUNDANT_STRUCT(FilterExpr, parse_Expression_StructParam) LATEBIND STRUCTPARAM)

	// Select a specifically named list row (usually for template children)
	const char **eapchName;						AST(NAME(Name) POOL_STRING)
} UIGenTutorialSelectTab;

AUTO_STRUCT;
typedef struct UIGenTutorialSelectJail
{
	UIGenTutorialSelect polyp;				AST(POLYCHILDTYPE(kUIGenTutorialSelectTypeJail))

	// The name of the jail cell
	const char *pchJailName;					AST(NAME(Jail) POOL_STRING STRUCTPARAM REQUIRED)

	// Which jail cells specifically to have this apply
	int iCell;									AST(NAME(Cell) DEFAULT(-1))
} UIGenTutorialSelectJail;

AUTO_STRUCT;
typedef struct UIGenTutorialInfo
{
	// The target UIGen
	REF_TO(UIGen) hTutorialGen;					AST(NAME(TutorialGen) STRUCTPARAM NON_NULL_REF)

	// Refined selection through inline children/list rows
	UIGenTutorialSelect **eaSelectors;			AST(NAME(Select))

	// A specific information messages to give to the selected gens
	REF_TO(Message) hInfo;						AST(NAME(Info) NON_NULL_REF)

	// Overrides to apply to the UIGen (this is applied just before the Last override).
	UIGenInternal *pOverride;					AST(NAME(Override))

	// The OnEnter action run on each selected UIGen of this step
	UIGenAction *pOnEnter;						AST(NAME(OnEnter))

	// The OnExit action run on each selected UIGen of this step
	UIGenAction *pOnExit;						AST(NAME(OnExit))

	// The limit of gens to to select, 0 for no limit
	S32 iSelectLimit;							AST(NAME(SelectLimit))

	// Ignore events
	UIGenTutorialEventType *peListenEvents;		AST(NAME(ListenEvent))

	// The current filename
	const char *pchFilename;					AST(CURRENTFILE)

	// The current line number
	int iLineNumber;							AST(LINENUM)
} UIGenTutorialInfo;

AUTO_STRUCT;
typedef struct UIGenTutorialStep
{
	// The display name of this tutorial step
	REF_TO(Message) hDisplayName;				AST(NAME(DisplayName) NON_NULL_REF)

	// The description of this tutorial step
	REF_TO(Message) hDescription;				AST(NAME(Description) NON_NULL_REF)

	// The OnEnter action run when this step becomes active
	UIGenAction *pOnEnter;						AST(NAME(OnEnter))

	// The OnExit action run when this step becomes inactive
	UIGenAction *pOnExit;						AST(NAME(OnExit))

	// The list of UIGens that gain tutorial information
	UIGenTutorialInfo **eaTutorialGens;			AST(NAME(TutorialGen))
} UIGenTutorialStep;

AUTO_STRUCT;
typedef struct UIGenTutorial
{
	// The name of this tutorial
	const char *pchName;						AST(KEY POOL_STRING REQUIRED STRUCTPARAM)

	// The OnEnter action run when this tutorial is started
	UIGenAction *pOnEnter;						AST(NAME(OnEnter))

	// The OnExit action run when this tutorial ends
	UIGenAction *pOnExit;						AST(NAME(OnExit))

	// The steps in the tutorial
	UIGenTutorialStep **eaSteps;				AST(NAME(Step))

	// The filename for reloading
	const char *pchFilename;					AST(CURRENTFILE)
} UIGenTutorial;

AUTO_STRUCT;
typedef struct UIGenTutorialInfoState
{
	// The UIGen this state belongs to
	UIGen *pGen;								AST(UNOWNED)

	// The tutorial info associated with this gen
	UIGenTutorialInfo *pInfo;					AST(UNOWNED)
} UIGenTutorialInfoState;

AUTO_STRUCT;
typedef struct UIGenTutorialState
{
	// The tutorial
	REF_TO(UIGenTutorial) hTutorial;

	// The info states
	UIGenTutorialInfoState **eaInfoState;

	// The step of the tutorial
	S32 iStep;

	// The data version of this tutorial
	S32 iVersion;

	// There's been a change to this tutorial state
	bool bDirty;
} UIGenTutorialState;

extern DictionaryHandle g_hUIGenTutorialDict;
extern UIGenTutorialState *g_pUIGenActiveTutorial;

extern void ui_GenTutorialOncePerFrameEarly(void);
extern void ui_GenTutorialOncePerFrame(void);
extern bool ui_GenTutorialStart(UIGenTutorialState *pState, UIGenTutorial *pTutorial);
extern bool ui_GenTutorialReset(UIGenTutorialState *pState);
extern bool ui_GenTutorialStop(UIGenTutorialState *pState);
extern bool ui_GenTutorialPrevious(UIGenTutorialState *pState);
extern bool ui_GenTutorialNext(UIGenTutorialState *pState);
extern bool ui_GenTutorialSetStep(UIGenTutorialState *pState, S32 iStep);
extern void ui_GenTutorialEvent(UIGenTutorialState *pState, UIGen *pGen, UIGenTutorialEventType eType);

extern bool ui_GenTutorialAddInfo(UIGenTutorialState *pState, UIGen *pGen, UIGenTutorialInfo *pInfo);
extern bool ui_GenTutorialSetInfo(UIGenTutorialState *pState, UIGen *pGen, UIGenTutorialInfo *pInfo);
extern UIGenTutorialInfo *ui_GenTutorialGetInfo(UIGenTutorialState *pState, UIGen *pGen);
extern void ui_GenTutorialReleaseInfo(UIGenTutorialState *pState, UIGen *pGen);

typedef UIGen *(*UIGenTutorialResolveCB)(S32 *piIndex, UIGen *pGen, void *pSelector, bool bValidate);
extern void ui_GenTutorialAddResolver(UIGenTutorialSelectType eType, UIGenTutorialResolveCB cbResolver);
extern bool ui_GenTutorialResolve(UIGen ***peaResolved, UIGen *pGen, UIGenTutorialSelect **eaSelectors, bool bValidate);

extern void ui_GenTutorialLoad(void);

typedef void (*ui_GenTutorialStartStopCB)(UIGenTutorial *pTutorial, bool bStarted);
extern void ui_GenTutorialSetStartStopHandler(ui_GenTutorialStartStopCB cbStartStopHandler);
