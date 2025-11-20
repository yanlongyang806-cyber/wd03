#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_GEN_PRIVATE_H
#define UI_GEN_PRIVATE_H

#include "MultiVal.h"
#include "TextParserEnums.h"
#include "StringCache.h"

#define UI_GEN_KEY_ACTION_COMMAND_PREFIX "Command:"
#define GEN_STACK_MAX_COUNT 8

typedef struct ExprFuncTable ExprFuncTable;
typedef struct NinePatch NinePatch;
typedef struct SpriteProperties SpriteProperties;

AUTO_STRUCT;
typedef struct UIGenCodeInterface
{
	ParseTable *pTable; NO_AST
	void *pStruct; NO_AST
	ParseTable *pListTable; NO_AST
	void **eaList; NO_AST
	S32 iSortColumn; AST(DEFAULT(-1))
	UISortType eSort;

	// Timestamp to see ensure the model has been updated
	U32 uiFrameLastUpdate;

	// Timestamp of last bad memory parameter error
	U32 uiFrameLastBadMemoryError;

	// If true, peaList should be eaDestroyStructed using pListTable when the structure is destroyed.
	bool bManagedStructures;
	// If true, pStruct should be StructDestroy'd using pTable when the structure is destroyed.
	bool bManagedPointer;
} UIGenCodeInterface;

AUTO_STRUCT;
typedef struct UIGenSizePair
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM)
	S32 iValue; AST(STRUCTPARAM)
} UIGenSizePair;

AUTO_STRUCT;
typedef struct UIGenSizes
{
	UIGenSizePair **eaSizes; AST(NAME(Size))
	const char *pchFilename; AST(CURRENTFILE)
} UIGenSizes;

AUTO_STRUCT;
typedef struct UIGenBadPointer
{
	int UseDeclareType;
} UIGenBadPointer;

typedef struct UIGenFuncDefs
{
	bool bIsRegistered;

	// Runs after the main validation step at the gen's load time.
	UIGenValidateFunc cbValidate;

	// Write a better comment here!!!
	UIGenLoopFunc cbPointerUpdate;

	// Write a better comment here!!!
	UIGenLoopFunc cbUpdate;

	// Runs before generic ticking, after normal children. The gen is
	// guaranteed to be ready.
	UIGenLoopFunc cbTickEarly;

	// Runs before updating position info for this frame and updating
	// normal children. The gen is guaranteed to be ready.
	UIGenLoopFunc cbLayoutEarly;

	// Runs after updating position info for this frame, at the same time as
	// laying out children. The gen is guaranteed to be ready.
	UIGenLoopFunc cbLayoutLate;

	// Runs after generic ticking, before drawing children. The gen is
	// guaranteed to be ready.
	UIGenLoopFunc cbDrawEarly;

	// Runs each frame when the fit-to-contents size of a gen is required.
	// The gen is not guaranteed to be ready; use the GenInternal passed in.
	UIGenFitSizeFunc cbFitContentsSize;

	// Runs each frame when the fit-to-parent size of a gen is required.
	// The gen is not guaranteed to be ready; use the GenInternal passed in.
	UIGenFitSizeFunc cbFitParentSize;

	// Runs when this gen is no longer visible. The gen is not guaranteed to be ready.
	UIGenLoopFunc cbHide;

	// Runs when receiving keyboard input, before anything else.
	// The gen is guaranteed to be ready.
	UIGenInputFunc cbInput;

	// Runs when evaluating an expression for the 'pFor' gen, and this gen
	// is in its parent tree. The gen is not guaranteed to be ready.
	UIGenUpdateContextFunc cbUpdateContext;

	// Runs when queueing a gen to be reset. Certain gens, like lists and layoutboxes
	// need special handling when being queued for reset. 
	UIGenLoopFunc cbQueueReset;
} UIGenFuncDefs;

TextParserResult ui_GenInternalParserFixup(UIGenInternal *pInt, enumTextParserFixupType eType, void *pExtraData);

// UIGens have five phases
// - Pointer Update - RowData, GenInstanceData, and other pointers are updated
// - Tick           - Process mouse and keyboard events.
// - Update         - Regenerate pResult, create children, run timers and state transitions, and other things.
// - Layout         - Turn unbaked UIPosition into CBoxes, update internal layout state.
// - Draw           - Actually draw.
//
// When using sprite caches, Draw is replaced with a cached draw, and none of the other phaes run.


typedef void (*UIGenPointerUpdateFunc)(UserData userdata);

void ui_GenSetPointerUpdateCallback(UserData userdata, UIGenPointerUpdateFunc callback);
void ui_GenClearPointerUpdateCallback(UserData userdata);

void ui_GenOncePerFrameEarly(void);
void ui_GenOncePerFramePointerUpdate(void);
void ui_GenOncePerFrameInput(bool bShowing);
void ui_GenOncePerFrameMiddle(bool bShowing);
void ui_GenOncePerFrameLate(bool bShowing);
void ui_GenProcessInput(void);
void ui_GenDebugUpdate(void);

void ui_GenTooltipSet(UIGen *pGen);
void ui_GenTooltipDraw(void);
void ui_GenTooltipClear(void);
void ui_GenTooltipClearGen(UIGen *pGen);

void ui_GenCodeInterfaceFreeManagedList(UIGenCodeInterface *pCode);

bool ui_GenDrawBackground(void);

UIGen *ui_GenGetHighlighted(void);

void ui_GenSetHighlighted(UIGen *pGen);

void ui_GenRunCommandInExprContext(UIGen *pGen, const char *pchCommand);

void ui_GenFillWithTopLevel(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) UIGen ***peaOut);

CONST_EARRAY_OF(UIGen) *ui_GenGetTopLevel(void);

//////////////////////////////////////////////////////////////////////////
// Window handling.

// Return true if the modal layer has any active children in it.
bool ui_GenModalPending(void);

// Show the modal layer.
void ui_GenModalShow(void);

// Hide the modal layer
void ui_GenModalHide(void);

// When a Modal Layer is deleted, call this.
void ui_GenModalDeleted(void);

// Pop up pGen in the modal layer; show the modal layer if necessary.
// Return true if succeeded or if pGen was already there.
bool ui_GenAddModalPopup(UIGen *pGen);

// Initialize all layers.
void ui_GenLayersInitialize(void);

void ui_GenListSort(UIGen *pGen);

// Uses void* instead of char* to emphasize the fact the string passed in *MUST* be pooled.
UIGen *ui_GenGetSiblingEx(UIGen *pGen, const void *pchName, bool bReverse, bool bMustBeInOrder);
#define ui_GenGetSibling(pGen, pchName) ui_GenGetSiblingEx(pGen, pchName, true, false)
UIGen *ui_GenGetChild(UIGen *pGen, const void *pchName);
UIGen *ui_GenGetBaseChild(UIGen *pGen, const void *pchName);

void ui_GenHandleRelativeOffset(UIGen *pGen, CBox *pBox, UIGenRelative *pRelative);

#define UI_GEN_FAIL_IF_RETURN(pGen, cond, ret, pchFormat, ...)\
	if (cond) {\
	InvalidDataErrorFilenamefInternal(__FILE__, __LINE__, pGen->pchFilename, "%s: " pchFormat, pGen->pchName, ##__VA_ARGS__);\
	return (ret);\
	}

#define UI_GEN_FAIL_IF(pGen, cond, pchFormat, ...) UI_GEN_FAIL_IF_RETURN(pGen, cond, 0, pchFormat, ##__VA_ARGS__)

#define UI_GEN_WARN_IF(pGen, cond, pchFormat, ...)\
	if (cond) {\
	ErrorFilenamefInternal(__FILE__, __LINE__, pGen->pchFilename, "%s: " pchFormat, pGen->pchName, ##__VA_ARGS__);\
	}

bool ui_GenSetEarlyOverrideAngle(UIGen *pGen, const char *pchField, F32 fMagnitude, UIAngleUnitType eUnit);
bool ui_GenSetEarlyOverrideDimension(UIGen *pGen, const char *pchField, F32 fMagnitude, UIUnitType eUnit);
bool ui_GenSetEarlyOverrideField(UIGen *pGen, const char *pchField, MultiVal *pValue);
bool ui_GenClearEarlyOverrideField(UIGen *pGen, const char *pchField);
bool ui_GenSetEarlyOverrideFieldColor(UIGen *pGen, const char *pchField, Color color);
bool ui_GenSetResultField(UIGen *pGen, const char *pchField, MultiVal *pValue);
bool ui_GenClearEarlyOverrideChildren(UIGen *pGen);
bool ui_GenSetEarlyOverrideAssembly(UIGen *pGen, UITextureAssembly *pTexAs, Color4 *pTint);


//////////////////////////////////////////////////////////////////////////

typedef struct GenExprContext
{
	UIGen *pLastGen;
	ExprContext *pContext;
} GenExprContext;

typedef struct UIGenGlobalState
{
	ExprContext *pContext;
	GenExprContext **eaContexts;
	S32 iCurrentContextDepth;

	ExprFuncTable* stGenFuncTable;

	UIGenState *eaiGlobalOnStates;
	UIGenState *eaiGlobalOffStates;

	UIGenGetPosition cbPositionGet;
	UIGenSetPosition cbPositionSet;
	UIGenSetPosition cbPositionForget;
	UIGenGetValue cbValueGet;
	UIGenSetValue cbValueSet;
	UIGenGetOpenWindows cbOpenWindowsNames;
	UIGenOpenWindows cbOpenWindowsGet;
	UIGenOpenWindows cbOpenWindowsSet;

	UIGen *pHighlighted;

	UIGen *pFocused;
	UIGen *pTooltipFocused;

	UIGenFuncDefs *aFuncTable;
	S32 iFuncTableSize;

	S32 iMaxStates;

	UIGenBackgroundImage *pBackground;

	DictionaryHandle hGenDict;

	UIGen **eaManagedTopLevel;
	UIGen **eaManagedTopLevelComparitor;

	UIGen **eaFreeQueue;
	UIGen **eaResetQueue;

	S32 iDebugLevel;

	// Some performance metrics
	S32 iActionsRun;
	S32 iExpressionsRun;
	S32 iCSDExpressionsRun;
	S32 aiActionsRunRate[4];
	S32 aiActionsPeakRate[4];
	S32 aiExpressionsRunRate[4];
	S32 aiCSDExpressionsRunRate[4];
	S32 aiExpressionsPeakRate[4];
	S32 aiCSDExpressionsPeakRate[4];
	S32 aiFrameRunRate[4];

	bool bHighlight;
	bool bFocusHighlight;

	S32 iTimingLevel;
	bool bTimingNames;
	bool bTimingFunctions;
	bool bTimingActions;
	bool bTimingExpressions;

	bool bJailUnlocked;
	bool bJailFrames;
	bool bJailNoGens;
	bool bJailCells;
	bool bJailCompress;

	const char* pchDumpExpressions;
	const char* pchDumpCSDExpressions;
	const char** eapchExprs;

	const char** eapchSkinOverrides;
	S32 *eaiSkinStates;

	const char** eapchTextureSkins;

	bool bGenLifeChange;

	F32 fScale;

	StashTable stTimingCommonData;

	struct
	{
		struct 
		{
			SpriteProperties *spriteProps[GEN_STACK_MAX_COUNT];
			int stack_depth;
		} spritePropStack;
	} drawProperties;

} UIGenGlobalState;

#define ui_GenSumRunRateCounter(iCount, aiRunRates) { \
		static S32 s_iLastIndex = -1; \
		S32 iUpdateIndex, iCurrentIndex = g_ui_State.totalTimeInMs / 500 / ARRAY_SIZE_CHECKED(aiRunRates); \
		if (s_iLastIndex != iCurrentIndex) \
		{ \
			if (s_iLastIndex >= 0) \
			{ \
				(aiRunRates)[s_iLastIndex % ARRAY_SIZE(aiRunRates)] = 0; \
			} \
			s_iLastIndex = iCurrentIndex; \
		} \
		for (iUpdateIndex = ARRAY_SIZE(aiRunRates) - 1; iUpdateIndex > 0; iUpdateIndex--) \
		{ \
			(aiRunRates)[(iCurrentIndex + iUpdateIndex) % ARRAY_SIZE(aiRunRates)] += (iCount); \
		} \
	}
#define ui_GenMaxRunRateCounter(iCount, aiRunRates) { \
		static S32 s_iLastIndex = -1; \
		S32 iUpdateIndex, iCurrentIndex = g_ui_State.totalTimeInMs / 500 / ARRAY_SIZE_CHECKED(aiRunRates); \
		if (s_iLastIndex != iCurrentIndex) \
		{ \
			if (s_iLastIndex >= 0) \
			{ \
				(aiRunRates)[s_iLastIndex % ARRAY_SIZE(aiRunRates)] = 0; \
			} \
			s_iLastIndex = iCurrentIndex; \
		} \
		for (iUpdateIndex = ARRAY_SIZE(aiRunRates) - 1; iUpdateIndex > 0; iUpdateIndex--) \
		{ \
			MAX1((aiRunRates)[(iCurrentIndex + iUpdateIndex) % ARRAY_SIZE(aiRunRates)], (iCount)); \
		} \
	}
#define ui_GenGetRunRateCounter(aiRunRates) ((aiRunRates)[(g_ui_State.totalTimeInMs / 500 / ARRAY_SIZE_CHECKED(aiRunRates)) % ARRAY_SIZE(aiRunRates)])

extern UIGenGlobalState g_GenState;

#ifndef _XBOX
#define ui_GenTimeAction(pGen, pAction, pchFormat, ...) do { \
		if ((pAction) && g_GenState.bTimingActions) { \
			char achBuffer[256]; \
			if (g_GenState.bTimingNames) { \
				sprintf(achBuffer, pchFormat, __VA_ARGS__); \
			} else { \
				sprintf(achBuffer, "%s: " pchFormat, (pGen)->pchName, __VA_ARGS__); \
			} \
			PERFINFO_AUTO_START_STATIC(allocAddString(achBuffer), g_GenState.bTimingActions < 0 ? ui_GenGetCommonPerfInfo(achBuffer) : &(pAction)->pPerfInfo, 1); \
		} \
		ui_GenRunAction(pGen, pAction); \
		if ((pAction) && g_GenState.bTimingActions) { \
			PERFINFO_AUTO_STOP(); \
		} \
	} while (false)
#define ui_GenTimeEvaluate(pGen, pExpr, pmv, pchFormat, ...) do { \
		if ((pExpr) && g_GenState.bTimingExpressions) { \
			char achBuffer[256]; \
			if (g_GenState.bTimingNames) { \
				sprintf(achBuffer, pchFormat, __VA_ARGS__); \
			} else { \
				sprintf(achBuffer, "%s: " pchFormat, (pGen)->pchName, __VA_ARGS__); \
			} \
			PERFINFO_AUTO_START_STATIC(allocAddString(achBuffer), ui_GenGetCommonPerfInfo(achBuffer), 1); \
		} \
		ui_GenEvaluate(pGen, pExpr, pmv); \
		if ((pExpr) && g_GenState.bTimingExpressions) { \
			PERFINFO_AUTO_STOP(); \
		} \
	} while (false)

#else
#define ui_GenTimeAction(pGen, pAction, pchFormat, ...) ui_GenRunAction(pGen, pAction)
#define ui_GenTimeEvaluate(pGen, pExpr, pmv, pchFormat, ...) ui_GenEvaluate(pGen, pExpr, pmv)
#endif

#ifndef _XBOX
#define GEN_PRINTF_LEVEL(iLevel, pchFormat, ...) if (g_GenState.iDebugLevel > iLevel) printf("Gen: " pchFormat "\n", __VA_ARGS__)
#else
#define GEN_PRINTF_LEVEL(iLevel, pchFormat, ...) 
#endif
#define GEN_PRINTF(pchFormat, ...) GEN_PRINTF_LEVEL(0, pchFormat, __VA_ARGS__)

#ifndef _XBOX
// PerfInfoStaticData doesn't really exist.  To make this work, we can't pass non-static data as PerfInfoStaticData.  I'm using the name,
// since those are pooled and should behave better than a field hanging off of the gen.
#define GEN_PERFINFO_START(pGen) {\
		if (pGen && ((g_GenState.bTimingNames && autoTimersPublicState.enabled) || g_bUsingSpecialTimer)) { \
			PERFINFO_AUTO_START_STATIC_FORCE(pGen->pchName, g_GenState.bTimingNames < 0 ? ui_GenGetCommonPerfInfo(pGen->pchName) : (PerfInfoStaticData **)pGen->pchName, 1, g_CurrentGenTimerTD); \
		} \
		if (g_GenState.bTimingFunctions) { \
			PERFINFO_AUTO_START_FUNC(); \
		} \
	}

#define GEN_PERFINFO_STOP(pGen) {\
		if (g_GenState.bTimingFunctions) { \
			PERFINFO_AUTO_STOP_FUNC(); \
		} \
		if (pGen && ((g_GenState.bTimingNames && autoTimersPublicState.enabled) || g_bUsingSpecialTimer)) { \
			PERFINFO_AUTO_STOP_CHECKED_FORCE(pGen->pchName,g_CurrentGenTimerTD); \
		} \
	}

#define GEN_PERFINFO_START_DETAIL(Level, pchName, iCount) {\
		if (g_GenState.iTimingLevel >= Level) { \
			PERFINFO_AUTO_START(pchName, iCount); \
		} \
	}

#define GEN_PERFINFO_STOP_DETAIL(Level) {\
		if (g_GenState.iTimingLevel >= Level) { \
			PERFINFO_AUTO_STOP(); \
		} \
	}

#else
#define GEN_PERFINFO_START(pGen)
#define GEN_PERFINFO_STOP(pGen)
#define GEN_PERFINFO_START_DETAIL(Level, pchName, iCount)
#define GEN_PERFINFO_STOP_DETAIL(Level)
#endif

#define GEN_PERFINFO_START_L1(pchName, iCount) GEN_PERFINFO_START_DETAIL(1, pchName, iCount)
#define GEN_PERFINFO_STOP_L1() GEN_PERFINFO_STOP_DETAIL(1)

#define GEN_PERFINFO_START_DEFAULT(pchLocation, iCount) if (!(g_GenState.bTimingFunctions || g_GenState.bTimingNames)) { PERFINFO_AUTO_START_PIX(pchLocation, iCount); }
#define GEN_PERFINFO_STOP_DEFAULT() if (!(g_GenState.bTimingFunctions || g_GenState.bTimingNames)) { PERFINFO_AUTO_STOP_PIX(); }

const char *ui_GenGetStateName(UIGenState eState);
const char *ui_GenGetTypeName(UIGenType eType);

void ui_GenDemandTextures(UIGen *pGen);

const char *ui_GenGetPersistedString(const char *pchKey, const char *pchDefault);
S32 ui_GenGetPersistedInt(const char *pchKey, S32 iDefault);
F32 ui_GenGetPersistedFloat(const char *pchKey, F32 fDefault);

bool ui_GenSetPersistedString(const char *pchKey, const char *pchValue);
bool ui_GenSetPersistedInt(const char *pchKey, S32 iValue);
bool ui_GenSetPersistedFloat(const char *pchKey, F32 fValue);

void ui_GenManageTopLevel(UIGen *pGen);
void ui_GenRaiseTopLevel(UIGen *pGen);

bool ui_GenLayersAddModalPopup(UIGen *pGen);
bool ui_GenLayersAddWindow(UIGen *pGen);
bool ui_GenLayersRemoveWindow(UIGen *pGen, bool bForce);

typedef bool (*UIGenIsMatch)(UIGen *pGen, const void *pvTester);
// Find a child of a gen. Searches pResult.
UIGen *ui_GenFindChild(UIGen *pParent, UIGenIsMatch pfnGenIsMatch, const void *pvTester);
// Callback test functions for the above.
bool ui_GenMatchesByNameCB(UIGen *pGen, const char *pchName);
bool ui_GenMatchesByPointerCB(UIGen *pGen, const UIGen *pTest);

bool ui_GenRunTransitions(UIGen *pGen);

extern struct DisplaySprite **g_sprite_cache_ptr;
extern int *g_sprite_cache_count;
extern int *g_sprite_cache_max;

extern int (*g_SMFNavigateCallback)(const char *);
extern int (*g_SMFHoverCallback)(const char *, UIGen* pGen);

extern PerfInfoStaticData **ui_GenGetCommonPerfInfo(const char *pchName);

void ui_GenDebugRegisterContextInt(const char *pchName);
void ui_GenDebugRegisterContextFloat(const char *pchName);
void ui_GenDebugRegisterContextString(const char *pchName);
void ui_GenDebugRegisterContextPointer(const char *pchName, ParseTable *pTable);
void ui_GenDebugRegisterContextStaticDefine(StaticDefineInt *pDefine, const char *pchPrefix);

#ifndef _XBOX
__forceinline bool ui_GenTimeEvaluateWithContext(UIGen *pGen, Expression *pExpr, MultiVal *pMulti, ExprContext *pContext, const char *pchFormat)
{
	bool bResult;
	if (pExpr && g_GenState.bTimingExpressions) {
		char achBuffer[256];
		if (g_GenState.bTimingNames) {
			strcpy(achBuffer, pchFormat);
		} else {
			sprintf(achBuffer, "%s: %s", pGen->pchName, pchFormat);
		}
		PERFINFO_AUTO_START_STATIC(allocAddString(achBuffer), ui_GenGetCommonPerfInfo(achBuffer), 1);
	}
	bResult = ui_GenEvaluateWithContext(pGen, pExpr, pMulti, pContext);
	if (pExpr && g_GenState.bTimingExpressions) {
		PERFINFO_AUTO_STOP();
	}
	return bResult;
}
#else
#define ui_GenTimeEvaluateWithContext(pGen, pExpr, pMulti, pContext, pchFormat) ui_GenEvaluateWithContext(pGen, pExpr, pMulti, pContext)
#endif

bool ui_GenGenerateExpr(Expression *pExpr, UIGen *pGen, const char *pchPathString, ExprContext *pContext);
void ui_GenInternalResourceFixup(UIGenInternal *pInt);
void ui_GenInternalResourcePostBinning(UIGenInternal *pInt);
void ui_GenInternalResourceValidate(const char *pchName, const char *pchDescriptor, UIGenInternal *pInt);

void ui_GenDrawNinePatchAtlas(const UIGenBundleTexture *pBundle,
							  CBox *pBox,
							  AtlasTex *pTex, const NinePatch *pNinePatch,
							  AtlasTex *pRender, AtlasTex *pMask,
							  F32 fZ,
							  F32 fScaleX, F32 fScaleY,
							  U32 uTopLeftColor, U32 uTopRightColor,
							  U32 uBottomRightColor, U32 uBottomLeftColor);

int g_ui_iGenData;
const char *g_ui_pchGenData;

#endif
