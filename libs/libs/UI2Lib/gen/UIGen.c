#include "cmdparse.h"
#include "TokenStore.h"
#include "StringCache.h"
#include "Expression.h"
#include "Message.h"
#include "MessageExpressions.h"
#include "StringFormat.h"
#include "MemoryPool.h"
#include "TextFilter.h"
#include "StringUtil.h"
#include "utilitiesLib.h"
#include "ScratchStack.h"
#include "memcheck.h"
#include "GlobalTypes.h"
#include "file.h"

#include "GfxClipper.h"
#include "GfxConsole.h"
#include "GfxHeadshot.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"
#include "GfxSprite.h"
#include "GfxTextures.h"
#include "GfxSpriteList.h"
#include "GfxPrimitive.h"

#include "InputLib.h"
#include "inputMouse.h"
#include "inputText.h"
#include "inputKeybind.h"

#include "UIGen.h"
#include "UIGenPrivate.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenPrivate_h_ast.h"
#include "UIGenWidget.h"
#include "UIGenDnD.h"
#include "UIGenList.h"
#include "UIGenLayoutBox.h"
#include "UIGenTabGroup.h"
#include "UIGenTutorial.h"

#include "UITextureAssembly.h"

#define UI_TARGET_DEPTH_THRESHOLD 5.0f
#define UI_TARGET_DEPTH_DEFAULT 20.0f

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("UIGenPrivate.h", BUDGET_UISystem););

MP_DEFINE(UIGenMessagePacket);
MP_DEFINE(UIGenAction);
MP_DEFINE(UIGenMessage);
MP_DEFINE(UIGenStateDef);
MP_DEFINE(UIGenComplexStateDef);
MP_DEFINE(UIGenBorrowed);
MP_DEFINE(UIGenVarTypeGlob);
MP_DEFINE(UIGenVarTypeGlobAndGen);
MP_DEFINE(UIGen);
MP_DEFINE(UIGenChild);
MP_DEFINE(UIGenKeyAction);
MP_DEFINE(UIGenTextureAssembly);
MP_DEFINE(UIGenRelative);

UIGenGlobalState g_GenState;
UIGenFilterProfanityForPlayerFunc *g_UIGenFilterProfanityForPlayerCB = NULL;
bool g_bUIGenFilterProfanityThisFrame;

static bool s_bGenDisableMouseTick = false;
AUTO_CMD_INT(s_bGenDisableMouseTick, GenDisableMouseTick) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(7);

bool g_ui_bGenLayoutOrder;
AUTO_CMD_INT(g_ui_bGenLayoutOrder, GenSaneLayoutOrder) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

bool g_ui_bGenDetectBadModelFunctions;
AUTO_CMD_INT(g_ui_bGenDetectBadModelFunctions, GenDetectBadModelFunctions) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

bool g_ui_bGenDisallowExprObjPathing;
AUTO_CMD_INT(g_ui_bGenDisallowExprObjPathing, GenDisallowObjectPaths) ACMD_CMDLINE ACMD_ACCESSLEVEL(9);

int (*g_SMFNavigateCallback)(const char *);
int (*g_SMFHoverCallback)(const char *, UIGen* pGen);

AUTO_RUN_EARLY;
void UIGenInitMemPools(void)
{
	MP_CREATE(UIGen, 256);
	MP_CREATE(UIGenAction, 64);
	MP_CREATE(UIGenBorrowed, 64);
	MP_CREATE(UIGenChild, 128);
	MP_CREATE(UIGenComplexStateDef, 64);
	MP_CREATE(UIGenKeyAction, 64);
	MP_CREATE(UIGenMessage, 32);
	MP_CREATE(UIGenMessagePacket, 32);
	MP_CREATE(UIGenStateDef, 64);
	MP_CREATE(UIGenVarTypeGlob, 64);
	MP_CREATE(UIGenVarTypeGlobAndGen, 64);
	MP_CREATE(UIGenKeyAction, 64);
	MP_CREATE(UIGenTextureAssembly, 64);
	MP_CREATE(UIGenRelative, 64);
}

void GenSpritePropPush(SpriteProperties *spriteProp)
{
	assert(g_GenState.drawProperties.spritePropStack.stack_depth < GEN_STACK_MAX_COUNT);
	g_GenState.drawProperties.spritePropStack.spriteProps[g_GenState.drawProperties.spritePropStack.stack_depth] = spriteProp;
	g_GenState.drawProperties.spritePropStack.stack_depth ++;
}

void GenSpritePropPop()
{
	assert(g_GenState.drawProperties.spritePropStack.stack_depth > 0);
	g_GenState.drawProperties.spritePropStack.stack_depth --;
}

SpriteProperties *GenSpritePropGetCurrent()
{
	if (g_GenState.drawProperties.spritePropStack.stack_depth)
		return g_GenState.drawProperties.spritePropStack.spriteProps[g_GenState.drawProperties.spritePropStack.stack_depth - 1];
	return NULL;
}

void GenExprContextInit(void)
{
	if (!g_GenState.pContext)
	{
		GenExprContext *pGenContext = calloc(1, sizeof(*pGenContext));
		ExprContext *pNewContext;

		g_GenState.pContext = exprContextCreateEx(SCRATCHSTACK_DEFAULT_SIZE_LARGE MEM_DBG_PARMS_INIT);

		// Create an empty context as the first context
		pNewContext = exprContextCreateEx(SCRATCHSTACK_DEFAULT_SIZE_LARGE MEM_DBG_PARMS_INIT);
		exprContextSetFuncTable(pNewContext, g_GenState.stGenFuncTable);
		exprContextSetParent(pNewContext, g_GenState.pContext);
		pGenContext->pContext = pNewContext;
		exprContextSetStaticCheck(pNewContext, ExprStaticCheck_AllowTypeChanges);
		eaPush(&g_GenState.eaContexts, pGenContext);
	}
}

typedef struct UIGenPointerUpdateHolder
{
	UIGenPointerUpdateFunc cbPointerUpdate; 
	UserData userdata; 
} UIGenPointerUpdateHolder;

static UIGenPointerUpdateHolder **s_eaPointerUpdates;

void ui_GenSetPointerUpdateCallback(UserData userdata, UIGenPointerUpdateFunc cbCallback)
{
	UIGenPointerUpdateHolder *pHolder = calloc(1, sizeof(*pHolder));
	pHolder->cbPointerUpdate = cbCallback;
	pHolder->userdata = userdata;
	eaPush(&s_eaPointerUpdates, pHolder);
}

void ui_GenClearPointerUpdateCallback(UserData userdata)
{
	int i; 
	for (i = 0; i < eaSize(&s_eaPointerUpdates); i++)
	{
		UIGenPointerUpdateHolder *pHolder = s_eaPointerUpdates[i];
		if (pHolder->userdata == userdata)
		{
			free(pHolder);
			eaRemoveFast(&s_eaPointerUpdates, i);
			return;
		}
	}
}

void ui_GenRegisterType(UIGenType eType,
						UIGenValidateFunc cbValidate,
						UIGenLoopFunc cbPointerUpdate,
						UIGenLoopFunc cbUpdate,
						UIGenLoopFunc cbLayoutEarly,
						UIGenLoopFunc cbLayoutLate,
						UIGenLoopFunc cbTickEarly,
						UIGenLoopFunc cbTickLate,
						UIGenLoopFunc cbDrawEarly,
						UIGenFitSizeFunc cbFitContentsSize,
						UIGenFitSizeFunc cbFitParentSize,
						UIGenLoopFunc cbHide,
						UIGenInputFunc cbInput,
						UIGenUpdateContextFunc cbUpdateContext,
						UIGenLoopFunc cbQueueReset)
{
	if (g_GenState.iFuncTableSize < eType + 1)
	{
		UIGenFuncDefs *aOldTable = g_GenState.aFuncTable;
		size_t sz = sizeof(*aOldTable) * (eType + 1);
		UIGenFuncDefs *aNewTable = calloc(1, sz);
		if (g_GenState.iFuncTableSize)
			memcpy_s(aNewTable, sz, aOldTable, sizeof(*aOldTable) * g_GenState.iFuncTableSize);
		g_GenState.aFuncTable = aNewTable;
		g_GenState.iFuncTableSize = eType + 1;
		free(aOldTable);
	}
	if (devassertmsg(!g_GenState.aFuncTable[eType].bIsRegistered, "Type is already registered"))
	{
		UIGenFuncDefs *pDef = &g_GenState.aFuncTable[eType];
		pDef->bIsRegistered = true;
		pDef->cbValidate = cbValidate;
		pDef->cbUpdate = cbUpdate;
		pDef->cbPointerUpdate = cbPointerUpdate;
		pDef->cbLayoutEarly = cbLayoutEarly;
		pDef->cbLayoutLate = cbLayoutLate;
		pDef->cbTickEarly = cbTickEarly;
		pDef->cbDrawEarly = cbDrawEarly;
		pDef->cbFitContentsSize = cbFitContentsSize;
		pDef->cbFitParentSize = cbFitParentSize;
		pDef->cbInput = cbInput;
		pDef->cbHide = cbHide;
		pDef->cbUpdateContext = cbUpdateContext;
		pDef->cbQueueReset = cbQueueReset;
	}
}

// Turn a global UI state on or off.
void ui_GenSetGlobalState(UIGenState eState, bool bEnable)
{
	if (bEnable)
	{
		eaiFindAndRemove(&g_GenState.eaiGlobalOffStates, eState);
		eaiPushUnique(&g_GenState.eaiGlobalOnStates, eState);
	}
	else
	{
		eaiPushUnique(&g_GenState.eaiGlobalOffStates, eState);
		eaiFindAndRemove(&g_GenState.eaiGlobalOnStates, eState);
	}
}

void ui_GenSetGlobalStateName(const char *pchState, bool bEnable)
{
	UIGenState eState = StaticDefineIntGetInt(UIGenStateEnum, pchState);
	if (eState > 0)
		ui_GenSetGlobalState(eState, bEnable);
	else
		Errorf("Invalid state name given to %s: %s", __FUNCTION__, pchState);
}

bool ui_GenInGlobalState(UIGenState eState)
{
	return eaiFind(&g_GenState.eaiGlobalOnStates, eState) >= 0;
}

bool ui_GenInGlobalStateName(const char *pchState)
{
	UIGenState eState = StaticDefineIntGetInt(UIGenStateEnum, pchState);
	if (eState > 0)
	{
		return ui_GenInGlobalState(eState);
	}
	else
	{
		Errorf("Invalid state name given to %s: %s", __FUNCTION__, pchState);
		return 0;
	}
}

void ui_GenInternalDestroySafe(UIGenInternal **ppGenInternal)
{
	if (*ppGenInternal)
	{
		ParseTable *pTable = ui_GenInternalGetType(*ppGenInternal);
		if (pTable)
			StructDestroySafeVoid(pTable, ppGenInternal);
	}
}

static void ui_GenCreateState(UIGen *pGen)
{
	if (!pGen->pState)
	{
		UIGenPerTypeState fake = {pGen->eType};
		ParseTable *pStateTable = PolyStructDetermineParseTable(polyTable_UIGenPerTypeState, &fake);
		if (pStateTable)
			pGen->pState = StructCreateVoid(pStateTable);
	}
}

static bool ui_GenUserReset(UIGen *pGen, UserData pDummy)
{
	ui_GenReset(pGen);
	return false;
}

bool ui_GenQueueResetChildren(UIGen *pChild, UIGen *pParent)
{
	if (g_GenState.pFocused == pChild)
		ui_GenSetFocus(NULL);
	if (g_GenState.pTooltipFocused == pChild)
		ui_GenSetTooltipFocus(NULL);
	if (UI_GEN_READY(pChild))
		ui_GenInternalForEachChild(pChild->pResult, ui_GenQueueResetChildren, pParent, false);
	if (g_GenState.aFuncTable[pChild->eType].cbQueueReset)
		g_GenState.aFuncTable[pChild->eType].cbQueueReset(pChild);
	
	return false;
}

void ui_GenQueueReset(UIGen *pGen)
{
	if (g_GenState.pFocused == pGen)
		ui_GenSetFocus(NULL);
	if (g_GenState.pTooltipFocused == pGen)
		ui_GenSetTooltipFocus(NULL);
	if (UI_GEN_READY(pGen))
	{
		ui_GenInternalForEachChild(pGen->pResult, ui_GenQueueResetChildren, pGen, false);
		eaPushUnique(&g_GenState.eaResetQueue, pGen);
		g_GenState.bGenLifeChange = true;
	}
	if (g_GenState.aFuncTable[pGen->eType].cbQueueReset)
		g_GenState.aFuncTable[pGen->eType].cbQueueReset(pGen);
}

void ui_GenReset(UIGen *pGen)
{
	S32 i;

	if (!pGen)
		return;

	GEN_PRINTF("%s: Resetting (%p)", pGen->pchName, pGen);

	if (ui_GenDragDropGetSource() == pGen)
		ui_GenDragDropCancel();

	if (!pGen->pState)
		ui_GenCreateState(pGen);

	// Clear the NextFocusOnCreate flag early to permit the BeforeHide action
	// to set the flag again. Of course a UIGen that does that is probably poorly
	// designed and probably wants to use FocusOnCreate in the Base or something.
	pGen->bNextFocusOnCreate = false;

	// Code interface needs to be cleared before anything happens, because it
	// may contain pointers that are now invalid.
	StructDestroySafe(parse_UIGenCodeInterface, &pGen->pCode);

	if (pGen->pResult)
	{
		// Any gens not currently children of pResult were reset already.
		ui_GenInternalForEachChild(pGen->pResult, ui_GenUserReset, NULL, false);
	}

	// Only run the action if something was actually on-screen...
	if (pGen->pResult)
	{
		ui_GenTimeAction(pGen, pGen->pBeforeHide, "BeforeHide");
		for (i = eaSize(&pGen->eaBorrowed)-1; i>=0; i--)
		{
			UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
			if (pBorrowed)
			{
				ui_GenTimeAction(pGen, pBorrowed->pBeforeHide, "BeforeHide(%s)", pBorrowed->pchName);
			}
		}
	}

	// But always run the C callback.
	if (g_GenState.aFuncTable[pGen->eType].cbHide)
		g_GenState.aFuncTable[pGen->eType].cbHide(pGen);

	// Clear these out after all Hide actions, in case a child or a poorly-written
	// expression set focus back to us.
	if (g_GenState.pFocused == pGen)
		ui_GenSetFocus(NULL);
	if (g_GenState.pTooltipFocused == pGen)
		ui_GenSetTooltipFocus(NULL);
	if (g_GenState.pHighlighted == pGen)
		ui_GenSetHighlighted(NULL);
	ui_GenTooltipClearGen(pGen);

	// Result/override inline children are copied, not owned - so clear them here.
	if (pGen->pResult)
		eaClearFast(&pGen->pResult->eaInlineChildren);
	if (pGen->pCodeOverrideEarly)
		eaClearFast(&pGen->pCodeOverrideEarly->eaInlineChildren);

	pGen->pParent = NULL;

	for (i = 0; i < eaSize(&pGen->eaTimers); i++)
		pGen->eaTimers[i]->fCurrent = 0;
	pGen->fTimeDelta = 0.f;
	pGen->uiTimeLastUpdateInMs = 0.f;

	ui_GenInternalDestroySafe(&pGen->pResult);
	ui_GenInternalDestroySafe(&pGen->pCodeOverrideEarly);
	StructDestroySafe(parse_UIGenTweenState, &pGen->pTweenState);
	StructDestroySafe(parse_UIGenTweenBoxState, &pGen->pBoxTweenState);
	eaDestroy(&pGen->eaTransitions);

	// Wipe out state list, everything will re-OnEnter the next time the widget is shown.
	memset(pGen->bfStates, 0, sizeof(pGen->bfStates));
	pGen->uiComplexStates = 0;
	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
		pGen->eaBorrowed[i]->uiComplexStates = 0;

	if (pGen->pSpriteCache)
	{
		if (pGen->pSpriteCache->pSprites)
			gfxDestroySpriteList(pGen->pSpriteCache->pSprites);

		pGen->pSpriteCache->pSprites = NULL;
		pGen->pSpriteCache->iAccumulate = 0;

		eaDestroy(&pGen->pSpriteCache->eaPopupChildren);
	}

	eaFindAndRemoveFast(&g_GenState.eaManagedTopLevel, pGen);
	eaFindAndRemoveFast(&g_GenState.eaResetQueue, pGen);

	if (pGen->pTutorialInfo)
		ui_GenTutorialReleaseInfo(NULL, pGen);

	ui_GenMarkDirty(pGen);
	g_GenState.bGenLifeChange = true;
}

static UIGenKeyAction *ui_GenRunKeyBinds(UIGen *pGen, KeyInput *pKey)
{
	S32 i;
	S32 j;
	UIGenKeyAction *pBest = NULL;

	// Non-edit keys should never result in keyboard navigation.
	if (pKey->type != KIT_EditKey)
		return NULL;

	for (i = eaSize(&pGen->pResult->eaKeyActions) - 1; i >= 0 ; i--)
	{
		UIGenKeyAction *pAction = pGen->pResult->eaKeyActions[i];
		KeyBind FakeBind = {pAction->iKey1, pAction->iKey2};
		KeyBind FakeBest = {pBest ? pBest->iKey1 : 0, pBest ? pBest->iKey2 : 0};

		if ((pAction->iAttribInclude && (pAction->iAttribInclude & pKey->attrib) == 0)
			|| (pAction->iAttribExclude && (pAction->iAttribExclude & pKey->attrib) != 0))
		{
			continue;
		}

		if (strStartsWith(pAction->pchKey, UI_GEN_KEY_ACTION_COMMAND_PREFIX))
		{
			static KeyBind **s_eaBinds;
			eaClearFast(&s_eaBinds);
			keybind_BindsForCommand(pAction->pchKey+(sizeof(UI_GEN_KEY_ACTION_COMMAND_PREFIX)-1), true, true, &s_eaBinds);
			keybind_BindsForCommand(pAction->pchKey+(sizeof(UI_GEN_KEY_ACTION_COMMAND_PREFIX)-1), false, true, &s_eaBinds);
			for (j = 0; j < eaSize(&s_eaBinds); j++)
			{
				if (keybind_BindIsActiveBest(s_eaBinds[j], pKey->scancode, pBest ? &FakeBest : NULL) == s_eaBinds[j])
				{
					pBest = pAction;
					break;
				}
			}
		}
		else if (keybind_BindIsActiveBest(&FakeBind, pKey->scancode, pBest ? &FakeBest : NULL) == &FakeBind)
			pBest = pGen->pResult->eaKeyActions[i];
	}

	if (pBest)
	{
		if(!pBest->bIgnore)
		{
			inpCapture(pBest->iKey1);
			inpCapture(pBest->iKey2);
			ui_GenRunAction(pGen, &pBest->action);
		}
		return pBest;
	}

	return NULL;
}

static const char *s_pchElapsedTime;
static const char *s_pchScreenWidth;
static const char *s_pchScreenHeight;
static const char *s_pchPrev;
static const char *s_pch_Previous;
static const char *s_pchNext;
static const char *s_pchFrame;
static int s_iGenApril1Mode = -1;
const char *g_ui_pchParent;

AUTO_CMD_INT(s_iGenApril1Mode, ui_GenApril1) ACMD_HIDE ACMD_ACCESSLEVEL(7) ACMD_COMMANDLINE;

static bool ui_GenIsApril1(void)
{
	static bool s_bCache;
	static U32 s_uiAge = 0;

	if (isDevelopmentMode() && s_uiAge <= g_ui_State.totalTimeInMs)
	{
		struct tm time;
		S32 iSeconds;
		timeMakeTimeStructFromSecondsSince2000(timeServerSecondsSince2000(), &time);
		iSeconds = time.tm_sec + time.tm_min * 60 + time.tm_hour * 3600;
		s_bCache = time.tm_mday == 1 && time.tm_mon == 3;
		// So it lags by about 5 seconds...
		s_uiAge = g_ui_State.totalTimeInMs + 1000 * (86405 - iSeconds);
	}

	return s_bCache;
}

AUTO_RUN;
void ui_GenInitStaticStrings(void)
{
	s_pchElapsedTime = allocAddStaticString("ElapsedTime");
	s_pchScreenWidth = allocAddStaticString("ScreenWidth");
	s_pchScreenHeight = allocAddStaticString("ScreenHeight");
	s_pchPrev = allocAddStaticString("_Prev");
	s_pch_Previous = allocAddStaticString("_Previous");
	s_pchNext = allocAddStaticString("_Next");
	s_pchFrame = allocAddStaticString("Frame");
	g_ui_pchParent = allocAddStaticString("_Parent");
	ui_GenSetIntVar("true", true);
	ui_GenSetIntVar("false", false);
}

void ui_GenOncePerFrameEarly(void)
{
	bool bLeftDown = mouseIsDown(MS_LEFT);
	bool bRightDown = mouseIsDown(MS_RIGHT);
	static bool s_bFirstTime = true;
	static S32 s_iElapsedTime;
	static S32 s_iScreenWidth;
	static S32 s_iScreenHeight;
	static S32 s_iFrameCount;
	S32 i;

	PERFINFO_AUTO_START_FUNC_PIX();

	ui_GenSetGlobalState(kUIGenStateKeyboard, inpHasKeyboard());
	ui_GenSetGlobalState(kUIGenStateMouse, inpHasKeyboard());
	ui_GenSetGlobalState(kUIGenStateGamepad, inpHasGamepad());
	ui_GenSetGlobalState(kUIGenStateStandardDefinition, g_ui_State.mode == UISD);
	ui_GenSetGlobalState(kUIGenStateHighDefinition, g_ui_State.mode == UIHD);
	ui_GenSetGlobalState(kUIGenStateMouseDownAnywhere, bLeftDown || bRightDown);
	ui_GenSetGlobalState(kUIGenStateLeftMouseDownAnywhere, bLeftDown);
	ui_GenSetGlobalState(kUIGenStateRightMouseDownAnywhere, bRightDown);
	ui_GenSetGlobalState(kUIGenStateApril1, s_iGenApril1Mode == 1 || s_iGenApril1Mode == -1 && ui_GenIsApril1());
	ui_GenSetGlobalState(kUIGenStateProductionEdit, isProductionEditMode() );
	devassertmsg(g_GenState.iCurrentContextDepth == 0, "Context evaluation depth is not zero, the world is ending");
	inpUpdateExpressionContext(g_GenState.pContext, false);
	exprContextSetFloatVarPooledCached(g_GenState.pContext, s_pchElapsedTime, g_ui_State.timestep, &s_iElapsedTime);
	exprContextSetIntVarPooledCached(g_GenState.pContext, s_pchScreenWidth, g_ui_State.screenWidth, &s_iScreenWidth);
	exprContextSetIntVarPooledCached(g_GenState.pContext, s_pchScreenHeight, g_ui_State.screenHeight, &s_iScreenHeight);
	exprContextSetIntVarPooledCached(g_GenState.pContext, s_pchFrame, g_ui_State.uiFrameCount, &s_iFrameCount);

	g_bUIGenFilterProfanityThisFrame = !g_UIGenFilterProfanityForPlayerCB || g_UIGenFilterProfanityForPlayerCB();

	while (eaSize(&g_GenState.eaResetQueue) > 0)
	{
		UIGen *pGen = eaPop(&g_GenState.eaResetQueue);
		pGen->pParent = NULL;
		ui_GenReset(pGen);
	}

	while (eaSize(&g_GenState.eaFreeQueue) > 0)
	{
		UIGen *pGen = eaPop(&g_GenState.eaFreeQueue);
		if (pGen->pParent)
			ui_GenRemoveChild(pGen->pParent, pGen, true);
		StructDestroy(parse_UIGen, pGen);
	}
	eaDestroy(&g_GenState.eaFreeQueue);

	g_GenState.bGenLifeChange = false;
	g_GenState.pBackground = NULL;

	if (!g_GenState.hGenDict)
		return;

	if (s_bFirstTime)
	{
		ui_GenLayersInitialize();
		s_bFirstTime = false;
	}

	if (g_ui_State.chInputDidAnything && (!g_GenState.bHighlight || inpLevelPeek(INP_SHIFT) || inpLevelPeek(INP_CONTROL)) && !s_bGenDisableMouseTick)
		ui_GenSetHighlighted(NULL);

	// Reset gen evaluation contexts so we don't have stale variables in them.
	for (i = 0; i < eaSize(&g_GenState.eaContexts); i++)
		g_GenState.eaContexts[i]->pLastGen = NULL;

	//if (!ui_GenModalPending())
	//	ui_GenModalHide();

	if (!UI_GEN_READY(g_GenState.pFocused))
		ui_GenSetFocus(NULL);

	ui_GenTutorialOncePerFrameEarly();

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void ui_GenProcessInput(void)
{
	KeyInput *pKey;
	// Give each widget in the gen tree the possibility to capture the input.
	if (g_GenState.pFocused && !ui_GenInState(g_GenState.pFocused, kUIGenStateVisible))
	{
		ErrorfForceCallstack(
			"An invisible gen, %s, is focused. Please please please allow the dump to complete and send a report of what you were doing.",
			g_GenState.pFocused->pchName);
		return;
	}
	if (!gfxConsoleVisible() && !g_GenState.bJailNoGens)
	{
		for (pKey = inpGetKeyBuf(); pKey && UI_GEN_READY(g_GenState.pFocused); inpGetNextKey(&pKey))
		{
			UIGen *pFocused = g_GenState.pFocused;
			UIGen *pGen = pFocused;
			bool bCaptured = false;
			
			ui_GenSetIntVar("Key", pKey->scancode);
			while (!bCaptured && UI_GEN_READY(pGen))
			{
				UIGenKeyAction *pRan;
				GEN_PERFINFO_START(pGen);
				pRan = ui_GenRunKeyBinds(pGen, pKey);
				if (pRan && !pRan->bPassThrough && !pRan->bIgnore)
					bCaptured = true;

				// PassThrough allows code actions to run still (and later, keybinds).
				if (!bCaptured)
				{
					UIGenInputFunc cbInput = g_GenState.aFuncTable[pGen->eType].cbInput;
					bCaptured = cbInput && cbInput(pGen, pKey);
				}

				// However if we found a matching KeyAction, don't run the default
				// key action. In particular, if PassThrough was set on pRan, we don't
				// want to eat the input in the default action. This allows all KeyActions
				// to override the default.
				if (!pRan && !bCaptured && pGen->pResult->pDefaultKeyAction && pKey->type == KIT_EditKey)
				{
					ui_GenRunAction(pGen, pGen->pResult->pDefaultKeyAction);
					bCaptured = true;
				}

				GEN_PERFINFO_STOP(pGen);

				pGen = pGen->pParent;
			}
			if (bCaptured)
				inpCapture(pKey->scancode);
		}
	}
}

static int GenTopLevelSort(const UIGen **ppA, const UIGen **ppB)
{
	const UIGen *pA = *ppA;
	const UIGen *pB = *ppB;
	// Layer is the primary key
	if (pA->chLayer != pB->chLayer)
		return pA->chLayer - pB->chLayer;
	// Non-roots above roots.
	else if (pA->bIsRoot && !pB->bIsRoot)
		return -1;
	else if (!pA->bIsRoot && pB->bIsRoot)
		return 1;
	// Popups above non-popups.
	else if (pA->bPopup && !pB->bPopup)
		return 1;
	else if (!pA->bPopup && pB->bPopup)
		return -1;
	else
	{
		S32 iA = eaFind(&g_GenState.eaManagedTopLevelComparitor, pA);
		S32 iB = eaFind(&g_GenState.eaManagedTopLevelComparitor, pB);
		// New windows above old windows.
		if (iA == -1 && iB >= 0)
			return 1;
		else if (iB == -1 && iA >= 0)
			return -1;
		else if (iA != iB)
			return iA - iB;
		// If those criteria fail, just make sure we're stable.
		else if (pA->chPriority != pB->chPriority)
			return pA->chPriority - pB->chPriority;
		else
			return stricmp(pA->pchName, pB->pchName);
	}
}

bool ui_GenRequestsTextInput(UIGen *pGen)
{
	return (pGen && (UI_GEN_IS_TYPE(pGen, kUIGenTypeTextEntry) ||
		UI_GEN_IS_TYPE(pGen, kUIGenTypeWebView) ||
		UI_GEN_IS_TYPE(pGen, kUIGenTypeTextArea) || 
		UI_GEN_IS_TYPE(pGen, kUIGenTypeSMF)));
}

bool ui_FocusGenRequestsTextInput()
{
	return ui_GenRequestsTextInput(g_GenState.pFocused);
}

void ui_GenOncePerFrameInput(bool bShowing)
{
	S32 i;
	GEN_PERFINFO_START_DEFAULT("Gen Sort", 1);
	eaQSort(g_GenState.eaManagedTopLevel, GenTopLevelSort);
	eaClearFast(&g_GenState.eaManagedTopLevelComparitor);
	for (i = 0; i < eaSize(&g_GenState.eaManagedTopLevel); i++)
	{
		eaPush(&g_GenState.eaManagedTopLevelComparitor, g_GenState.eaManagedTopLevel[i]);
		g_GenState.eaManagedTopLevel[i]->chPriority = i;
	}
	GEN_PERFINFO_STOP_DEFAULT();

	if (!s_bGenDisableMouseTick && (g_ui_State.chInputDidAnything || g_GenState.bGenLifeChange))
	{
		for (i = eaSize(&g_GenState.eaManagedTopLevel) - 1; i >= 0; i--)
		{
			UIGen *pGen = g_GenState.eaManagedTopLevel[i];
			UIGen *pParent = pGen->pParent;
			bool bPopup = pGen->bPopup;
			bool bParentQueue = pGen->pParent ? pGen->pParent->bTopLevelChildren : false;
			bool bRaise = false;
			pGen->bPopup = false;
			if (pParent)
				pParent->bTopLevelChildren = false;
			GEN_PERFINFO_START_DEFAULT("GenTickCB", 1);
			if (mouseDownAnyHit(MS_LEFT, &pGen->UnpaddedScreenBox)
				|| mouseDownAnyHit(MS_RIGHT, &pGen->UnpaddedScreenBox))
				bRaise = true;
			ui_GenTickCB(pGen, NULL);
			if (bRaise && !pGen->bIsRoot && inpCheckHandled())
				ui_GenRaiseTopLevel(pGen);
			GEN_PERFINFO_STOP_DEFAULT();
			pGen->bPopup = bPopup;
			if (pParent)
				pParent->bTopLevelChildren = bParentQueue;
		}
	}

	eaClearFast(&g_GenState.eaManagedTopLevel);
}

void ui_GenOncePerFramePointerUpdate(void)
{
	S32 i;
	for (i = eaSize(&s_eaPointerUpdates) - 1; i >= 0; i--)
	{
		UIGenPointerUpdateHolder *pHolder = s_eaPointerUpdates[i];
		GEN_PERFINFO_START_DEFAULT("GenPointerUpdateCB", 1);
		pHolder->cbPointerUpdate(pHolder->userdata);
		GEN_PERFINFO_STOP_DEFAULT();
	}
}

void ui_GenOncePerFrameMiddle(bool bShowing)
{
	S32 i;

	GEN_PERFINFO_START_DEFAULT("Gen Sort", 1);
	eaQSort(g_GenState.eaManagedTopLevel, GenTopLevelSort);
	eaClearFast(&g_GenState.eaManagedTopLevelComparitor);
	for (i = 0; i < eaSize(&g_GenState.eaManagedTopLevel); i++)
	{
		eaPush(&g_GenState.eaManagedTopLevelComparitor, g_GenState.eaManagedTopLevel[i]);
		g_GenState.eaManagedTopLevel[i]->chPriority = i;
	}
	GEN_PERFINFO_STOP_DEFAULT();

	if(bShowing && !s_bGenDisableMouseTick && (g_ui_State.chInputDidAnything || g_GenState.bGenLifeChange))
	{
		ui_GenTooltipClear();
		if (g_GenState.pTooltipFocused)
			ui_GenTooltipSet(g_GenState.pTooltipFocused);
	}

	// If a Gen has TooltipFocus, mark the parent SpriteCache as having tooltip focus
	if (g_GenState.pTooltipFocused)
	{
		UIGen *pParent;
		for (pParent = g_GenState.pTooltipFocused; pParent; pParent = pParent->pParent)
		{
			if (pParent->pSpriteCache)
			{
				pParent->pSpriteCache->bTooltipFocus = true;
				break;
			}
		}
	}

	if (g_ui_State.cbOncePerFrameBeforeMainDraw)
	{
		GEN_PERFINFO_START_DEFAULT("UI Extern Once Per Frame (Probably Entities)", 1);
		g_ui_State.cbOncePerFrameBeforeMainDraw();
		GEN_PERFINFO_STOP_DEFAULT();
	}

	for (i = 0; i < eaSize(&g_GenState.eaManagedTopLevel); i++)
	{
		UIGen *pGen = g_GenState.eaManagedTopLevel[i];
		UIGen *pParent = pGen->pParent;
		bool bPopup = pGen->bPopup;
		bool bParentQueue = pGen->pParent ? pGen->pParent->bTopLevelChildren : false;
		pGen->bPopup = false;
		if (pParent)
		{
			pParent->bTopLevelChildren = false;
			// I don't want fakey parents created for jail cells.  Not sure what the best thing to check is
			if (bParentQueue)
			{
				GEN_PERFINFO_START(pParent); // get the tree right
			}
		}
		GEN_PERFINFO_START_DEFAULT("GenDrawCB", 1);
		ui_GenDrawCB(pGen, NULL);
		GEN_PERFINFO_STOP_DEFAULT();
		pGen->bPopup = bPopup;
		if (pParent)
		{
			if (bParentQueue)
			{
				GEN_PERFINFO_STOP(pParent);
			}
			pParent->bTopLevelChildren = bParentQueue;
		}
	}
}

void ui_GenOncePerFrameLate(bool bShowing)
{
	devassertmsgf(!g_GenState.pFocused || ui_GenInState(g_GenState.pFocused, kUIGenStateVisible),
		"An invisible gen, %s, is focused. Please please please allow the dump to complete and send a report of what you were doing.", g_GenState.pFocused->pchName);

	ui_GenDragDropOncePerFrame();
	ui_GenTutorialOncePerFrame();

	if ((mouseDownAny(MS_LEFT) || mouseDownAny(MS_RIGHT)))
		ui_GenSetFocus(NULL);

	if (bShowing)
		ui_GenDrawBackground();
	g_ui_State.drawZ = UI_INFINITE_Z;
	if(bShowing)
		ui_GenTooltipDraw();

	if (!ui_GenModalPending())
		ui_GenModalHide();

	ui_GenSumRunRateCounter(g_GenState.iActionsRun, g_GenState.aiActionsRunRate);
	ui_GenMaxRunRateCounter(g_GenState.iActionsRun, g_GenState.aiActionsPeakRate);
	ui_GenSumRunRateCounter(g_GenState.iExpressionsRun, g_GenState.aiExpressionsRunRate);
	ui_GenSumRunRateCounter(g_GenState.iCSDExpressionsRun, g_GenState.aiCSDExpressionsRunRate);
	ui_GenMaxRunRateCounter(g_GenState.iExpressionsRun, g_GenState.aiExpressionsPeakRate);
	ui_GenMaxRunRateCounter(g_GenState.iCSDExpressionsRun, g_GenState.aiCSDExpressionsPeakRate);
	ui_GenSumRunRateCounter(1, g_GenState.aiFrameRunRate);
	g_GenState.iActionsRun = 0;
	g_GenState.iExpressionsRun = 0;
	g_GenState.iCSDExpressionsRun = 0;

	if (g_GenState.pchDumpCSDExpressions || g_GenState.pchDumpExpressions)
	{
		char achPath[MAX_PATH];
		const char *pchPath = g_GenState.pchDumpCSDExpressions ? g_GenState.pchDumpCSDExpressions : g_GenState.pchDumpExpressions;
		FILE *pFile;
		int i;
		fileLocateWrite(pchPath, achPath);
		makeDirectoriesForFile(achPath);
		pFile = fopen(achPath, "wb");
		for (i=0; i < eaSize(&g_GenState.eapchExprs); i+=3)
			fprintf(pFile, "\n=== %3d ===\n%s -- %s\n===========\n%s\n", i, g_GenState.eapchExprs[i], g_GenState.eapchExprs[i+1], g_GenState.eapchExprs[i+2]);
		fclose(pFile);
	}
	g_GenState.pchDumpCSDExpressions = NULL;
	g_GenState.pchDumpExpressions = NULL;
	eaClearFast(&g_GenState.eapchExprs);
}

UIGen *ui_GenFind(const char *pchName, UIGenType eType)
{
	UIGen *pGen = pchName ? RefSystem_ReferentFromString(g_GenState.hGenDict, pchName) : NULL;
	if (UI_GEN_NON_NULL(pGen) && (pGen->eType == eType || eType == kUIGenTypeNone))
		return pGen;
	else
		return NULL;
}

void ui_GenInitIntVar(const char *pchName, S32 iValue)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	ui_GenDebugRegisterContextInt(pchName);
	exprContextSetIntVar(g_GenState.pContext, pchName, iValue);
}

void ui_GenInitFloatVar(const char *pchName)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	ui_GenDebugRegisterContextFloat(pchName);
	exprContextSetFloatVar(g_GenState.pContext, pchName, 0.f);
}

void ui_GenInitStringVar(const char *pchName)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	ui_GenDebugRegisterContextString(pchName);
	exprContextSetStringVar(g_GenState.pContext, pchName, "");
}

void ui_GenInitPointerVar(const char *pchName, ParseTable *pTable)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	ui_GenDebugRegisterContextPointer(pchName, pTable);
	exprContextSetPointerVar(g_GenState.pContext, pchName, NULL, pTable, true, true);
}

void ui_GenInitStaticDefineVars(StaticDefineInt *pDefine, const char *pchPrefix)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	ui_GenDebugRegisterContextStaticDefine(pDefine, pchPrefix);
	exprContextAddStaticDefineIntAsVars(g_GenState.pContext, pDefine, pchPrefix);
}

void ui_GenSetPointerVar(const char *pchName, void *pStruct, ParseTable *pTable)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	exprContextSetPointerVar(g_GenState.pContext, pchName, pStruct, pTable, true, true);
}

void ui_GenSetStringVar(const char *pchName, const char *pchValue)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	exprContextSetStringVar(g_GenState.pContext, pchName, pchValue);
}

void ui_GenSetIntVar(const char *pchName, S32 iValue)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	exprContextSetIntVar(g_GenState.pContext, pchName, iValue);
}

void ui_GenSetFloatVar(const char *pchName, F32 fValue)
{
	// Auto-instantiate, so this is safe to call from other AUTO_RUNs.
	GenExprContextInit();
	exprContextSetFloatVar(g_GenState.pContext, pchName, fValue);
}

ParseTable *ui_GenInternalGetType(UIGenInternal *pInt)
{
	ParseTable *pTable = PolyStructDetermineParseTable(polyTable_UIGenInternal, pInt);
	devassertmsg(!(pInt && pInt->eType) || pTable, "Unable to determine polytype, this gen is probably corrupted");
	return pTable ? pTable : parse_UIGenInternal;
}

ParseTable *ui_GenGetType(UIGen *pGen)
{
	UIGenInternal Int = {pGen->eType};
	return pGen->eType ? ui_GenInternalGetType(&Int) : parse_UIGenInternal;
}

static void ui_GenCreateCodeInterface(UIGen *pGen)
{
	if (!pGen->pCode)
		pGen->pCode = StructCreate(parse_UIGenCodeInterface);
}

void *ui_GenGetPointer(UIGen *pGen, ParseTable *pTableExpected, ParseTable **ppTableOut)
{
	devassertmsg(pTableExpected || ppTableOut, "You must either expect a certain table or pay attention to what type is returned");
	if (!pGen->pCode)
	{
		if (ppTableOut)
			*ppTableOut = NULL;
		return NULL;
	}
	if (pGen->pCode->pTable && (!pTableExpected || pTableExpected == pGen->pCode->pTable))
	{
		if (ppTableOut)
			*ppTableOut = pGen->pCode->pTable;
		return pGen->pCode->pStruct;
	}
	else
		return NULL;
}

void *ui_GenGetManagedPointer(UIGen *pGen, ParseTable *pTable)
{
	S32 i;
	UIGenCodeInterface *pCode;

	ui_GenCreateCodeInterface(pGen);
	pCode = pGen->pCode;

	if (pCode->pTable == pTable && pCode->bManagedPointer)
		return pCode->pStruct;

	if (pCode->bManagedPointer && pCode->pStruct)
		StructDestroyVoid(pCode->pTable, pCode->pStruct);

	pCode->pStruct = StructCreateVoid(pTable);
	pCode->pTable = pTable;
	pCode->bManagedPointer = true;

	for (i = 0; i < eaSize(&g_GenState.eaContexts); i++)
	{
		if (exprContextGetCurExpression(g_GenState.eaContexts[i]->pContext) && exprContextGetUserPtr(g_GenState.eaContexts[i]->pContext, parse_UIGen) == pGen)
		{
			exprContextSetPointerVarPooledCached(g_GenState.eaContexts[i]->pContext, g_ui_pchGenData, pCode->pStruct, pTable, true, true, &g_ui_iGenData);
		}
	}

	return pCode->pStruct;
}

void ui_GenSetManagedPointer(UIGen *pGen, void *pStruct, ParseTable *pTable, bool bManageMemory)
{
	S32 i;
	UIGenCodeInterface *pCode;

	ui_GenCreateCodeInterface(pGen);
	pCode = pGen->pCode;

	if (pCode->bManagedPointer)
	{
		// Check to see if the old value should be freed
		if (pCode->pStruct != pStruct)
		{
			StructDestroyVoid(pCode->pTable, pCode->pStruct);
		}
	}

	pCode->pTable = pTable;
	pCode->pStruct = pStruct;
	pCode->bManagedPointer = bManageMemory && pStruct != NULL;

	for (i = 0; i < eaSize(&g_GenState.eaContexts); i++)
	{
		if (exprContextGetCurExpression(g_GenState.eaContexts[i]->pContext) && exprContextGetUserPtr(g_GenState.eaContexts[i]->pContext, parse_UIGen) == pGen)
		{
			exprContextSetPointerVarPooledCached(g_GenState.eaContexts[i]->pContext, g_ui_pchGenData, pStruct, pTable, true, true, &g_ui_iGenData);
		}
	}
}

void ui_GenClearPointer(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (pGen && pGen->pCode)
	{
		if (pGen->pCode->bManagedPointer && pGen->pCode->pStruct)
		{
			StructDestroyVoid(pGen->pCode->pTable, pGen->pCode->pStruct);
		}

		pGen->pCode->bManagedPointer = false;
		pGen->pCode->pStruct = NULL;
		pGen->pCode->pTable = NULL;
	}
}

void ***ui_GenGetManagedList(UIGen *pGen, ParseTable *pTableExpected)
{
	if (!pGen->pCode)
	{
		ui_GenCreateCodeInterface(pGen);
		assertmsg(pGen->pCode, "Could not create code interface");
	}
	devassertmsg(pGen->pCode->pListTable == pTableExpected || !pGen->pCode->pListTable, "Gen has a non-matching list ParseTable");
	if (!pGen->pCode->pListTable)
		pGen->pCode->pListTable = pTableExpected;
	return &pGen->pCode->eaList;
}

bool ui_GenIsListManagingStructures(SA_PARAM_NN_VALID UIGen *pGen)
{
	return SAFE_MEMBER(pGen->pCode, bManagedStructures);
}

void ***ui_GenGetList(UIGen *pGen, ParseTable *pTableExpected, ParseTable **ppTableOut)
{
	static void **s_eaInvalidList = NULL;
	U32 uiFrameLastUpdate;
	if (!pGen->pCode)
	{
		if (ppTableOut)
			*ppTableOut = NULL;
		return &s_eaInvalidList;
	}

	uiFrameLastUpdate = pGen->pCode->uiFrameLastUpdate;

	devassertmsg(pTableExpected || ppTableOut,
		"You must either expect a certain table, or pay attention to what type is returned, or have no table for a list of strings");
	if (uiFrameLastUpdate == g_ui_State.uiFrameCount)
	{
		if (pGen->pCode->pListTable && (!pTableExpected || pTableExpected == pGen->pCode->pListTable))
		{
			if (ppTableOut)
				*ppTableOut = pGen->pCode->pListTable;
			return &pGen->pCode->eaList;
		}
	}
	else if (pGen->pCode->pListTable && eaSize(&pGen->pCode->eaList) > 0)
	{
		if (g_ui_State.uiFrameCount > pGen->pCode->uiFrameLastBadMemoryError + 1)
			Alertf("%s: Accessing list model without updating.", pGen->pchName);
		pGen->pCode->uiFrameLastBadMemoryError = g_ui_State.uiFrameCount;
	}
	if (ppTableOut)
		*ppTableOut = NULL;
	return &s_eaInvalidList;
}

static int ui_GenListComparator(const UIGen *pGen, const void **ppA, const void **ppB)
{
	S32 iRet = (*ppA && *ppB) ? TokenCompare(pGen->pCode->pListTable, pGen->pCode->iSortColumn, *ppA, *ppB, 0, 0) : (*ppA ? 1 : (*ppB ? -1 : 0));
	if (iRet == 0)
	{
		int i;
		// Walk through all other tokens to resolve equality and to have a stable sort
		FORALL_PARSETABLE(pGen->pCode->pListTable, i)
			if (iRet = TokenCompare(pGen->pCode->pListTable, i, *ppA, *ppB, 0, 0))
				break;
	}
	if (iRet == 0)
		iRet = (*ppA < *ppB) ? -1 : (*ppA > *ppB) ? 1 : 0;
	if (pGen->pCode->eSort == UISortDescending)
		iRet = -iRet;
	return iRet;
}

void ui_GenSetManagedListEx(UIGen *pGen, void ***peaList, ParseTable *pTable, bool bManageStructures, const char *pchFunction)
{
	ui_GenCreateCodeInterface(pGen);

	pGen->pCode->uiFrameLastUpdate = g_ui_State.uiFrameCount;

	// Figure out the new column, if any, to be sorting by.
	if (pGen->pCode->pListTable != pTable)
	{
		if (pGen->pCode->iSortColumn < 0 || !pGen->pCode->pListTable || !pTable || !ParserFindColumn(pTable, pGen->pCode->pListTable[pGen->pCode->iSortColumn].name, &pGen->pCode->iSortColumn))
		{
			pGen->pCode->iSortColumn = -1;
			pGen->pCode->eSort = UISortNone;
		}
	}

	bManageStructures = bManageStructures == -1 ? peaList == &pGen->pCode->eaList : bManageStructures;

	if (peaList == &pGen->pCode->eaList)
	{
		if (g_ui_bGenDetectBadModelFunctions && !bManageStructures)
		{
			if (peaList && pTable)
				eaClearFast(peaList);
			if (g_ui_State.uiFrameCount > pGen->pCode->uiFrameLastBadMemoryError + 1)
				Alertf("%s: Using the managed list of type %s without using managed memory.", pchFunction, pTable[0].name);
			pGen->pCode->uiFrameLastBadMemoryError = g_ui_State.uiFrameCount;
		}

		pGen->pCode->pListTable = pTable;
		pGen->pCode->bManagedStructures = bManageStructures;
		ui_GenListSort(pGen);
		return;
	}

	if (pGen->pCode->bManagedStructures)
		eaClearStructVoid(&pGen->pCode->eaList, pGen->pCode->pListTable);
	else
		eaClearFast(&pGen->pCode->eaList);

	if (g_ui_bGenDetectBadModelFunctions && bManageStructures)
	{
		if (peaList && pTable)
			eaClearStructVoid(peaList, pTable);
		if (g_ui_State.uiFrameCount > pGen->pCode->uiFrameLastBadMemoryError + 1)
			Alertf("%s: Using the unmanaged list of type %s with managed memory.", pchFunction, pTable[0].name);
		pGen->pCode->uiFrameLastBadMemoryError = g_ui_State.uiFrameCount;
	}

	pGen->pCode->pListTable = pTable;
	if (peaList && peaList != &pGen->pCode->eaList)
		eaCopy(&pGen->pCode->eaList, peaList);
	pGen->pCode->bManagedStructures = bManageStructures;

	ui_GenListSort(pGen);
}

void ui_GenListSort(UIGen *pGen)
{
	if (!pGen->pCode || pGen->pCode->iSortColumn < 0 || !pGen->pCode->pListTable || pGen->pCode->eSort == UISortNone)
		return;
	if (eaSize(&pGen->pCode->eaList) > 1)
		eaQSort_s(pGen->pCode->eaList, ui_GenListComparator, pGen);
}

void ui_GenListSortList(UIGen *pGen, void **eaList, ParseTable *pTable)
{
	if (!pGen->pCode || pGen->pCode->iSortColumn < 0 || !pGen->pCode->pListTable || pGen->pCode->eSort == UISortNone)
		return;
	if (!devassertmsgf(pTable == pGen->pCode->pListTable, "table mismatch"))
		return;
	if (eaSize(&eaList) > 1)
		eaQSort_s(eaList, ui_GenListComparator, pGen);
}

bool ui_GenSetListSort(UIGen *pGen, const char *pchField, UISortType eSort)
{
	ParseTable *pTable = 0;
	void ***peaList = ui_GenGetList(pGen, NULL, &pTable);
	S32 iColumn;
	if (!pTable || !peaList || !pGen->pCode)
		return false;

	if (!pchField || !*pchField)
	{
		pGen->pCode->iSortColumn = -1;
		pGen->pCode->eSort = UISortNone;
		return false;
	}

	UI_GEN_FAIL_IF(pGen, !ParserFindColumn(pTable, pchField, &iColumn), "Trying to sort a list of %s by an invalid field: %s", ParserGetTableName(pTable), pchField);
	pGen->pCode->iSortColumn = iColumn;
	pGen->pCode->eSort = eSort % UISortMax;

	ui_GenListSort(pGen);
	return true;
}

void ui_GenSetListSortOrder(UIGen *pGen, UISortType eSort)
{
	if (pGen->pCode)
		pGen->pCode->eSort = eSort % UISortMax;
	ui_GenListSort(pGen);
}

void ui_GenSetFocus(UIGen *pGen)
{
	UIGen *pFocused;
	if (pGen == g_GenState.pFocused)
		return;
	else if (pGen && !UI_GEN_READY(pGen))
		return;

	if (g_GenState.pFocused)
		ui_GenUnsetState(g_GenState.pFocused, kUIGenStateFocused);

	pFocused = g_GenState.pFocused;
	if (ui_GenRequestsTextInput(pFocused) != ui_GenRequestsTextInput(pGen))
		ui_SetDeviceKeyboardTextInputEnable(ui_GenRequestsTextInput(pGen));

	while (pFocused && pFocused->pParent && (pFocused = pFocused->pParent)
		&& ui_GenInState(pFocused, kUIGenStateFocusedChild))
		ui_GenUnsetState(pFocused, kUIGenStateFocusedChild);

	g_GenState.pFocused = pGen;
	if (pGen)
	{
		ui_SetFocus(NULL);
		ui_GenSetState(pGen, kUIGenStateFocused);
	}

	pFocused = g_GenState.pFocused;
	while (pFocused && pFocused->pParent && (pFocused = pFocused->pParent)
		&& !ui_GenInState(pFocused, kUIGenStateFocusedChild))
		ui_GenSetState(pFocused, kUIGenStateFocusedChild);
}

void ui_GenSetTooltipFocus(UIGen *pGen)
{
	if (pGen == g_GenState.pTooltipFocused)
		return;
	else if (pGen && !UI_GEN_READY(pGen))
		return;

	if (g_GenState.pTooltipFocused)
	{
		ui_GenUnsetState(g_GenState.pTooltipFocused, kUIGenStateTooltipFocused);
	}

	g_GenState.pTooltipFocused = pGen;
	if (pGen)
	{
		ui_GenSetState(pGen, kUIGenStateTooltipFocused);
	}
}

UIGen *ui_GenGetFocus(void)
{
	return g_GenState.pFocused;
}

static S32 GenGetFieldAndColumn(UIGen *pGen, const char *pchField, ParseTable **ppTable)
{
	ParseTable *pTable;
	S32 iColumn;
	if (!pGen)
		return -1;

	pTable = ui_GenGetType(pGen);
	if (!ParserFindColumn(pTable, pchField, &iColumn))
		return -1;
	if (ppTable)
		*ppTable = pTable;
	return iColumn;
}

bool ui_GenSetEarlyOverrideField(UIGen *pGen, const char *pchField, MultiVal *pValue)
{
	MultiVal mv;
	char ach[MAX_PATH];
	ParseTable *pTable;
	S32 iColumn = GenGetFieldAndColumn(pGen, pchField, &pTable);
	bool bSuccess = iColumn >= 0;
	if (bSuccess && !pGen->pCodeOverrideEarly)
		pGen->pCodeOverrideEarly = StructCreateVoid(pTable);
	if (bSuccess)
	{
		MultiVal oldValue = {0};
		if (FieldToMultiVal(pTable, iColumn, pGen->pCodeOverrideEarly, 0, &oldValue, false, false))
		{
			if (oldValue.type != pValue->type)
				ui_GenMarkDirty(pGen);
			else if (MultiValIsString(&oldValue))
			{
				if (pValue->str && *pValue->str && g_texture_name_fixup && !stricmp(pTable[iColumn].subtable, "Texture"))
				{
					g_texture_name_fixup(pValue->str, SAFESTR(ach));
					mv.type = pValue->type;
					mv.str = ach;
					pValue = &mv;
				}
				if (stricmp_safe(oldValue.str, pValue->str))
					ui_GenMarkDirty(pGen);
			}
			else if (oldValue.intval != pValue->intval)
				ui_GenMarkDirty(pGen);
			else if (!TokenIsSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1))
				ui_GenMarkDirty(pGen);
		}
		else
			return false;
		if (pGen->bNeedsRebuild)
		{
			FieldFromMultiVal(pTable, iColumn, pGen->pCodeOverrideEarly, 0, pValue);
			TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, true);
		}
	}
	return bSuccess;
}

bool ui_GenSetResultField(UIGen *pGen, const char *pchField, MultiVal *pValue)
{
	ParseTable *pTable;
	S32 iColumn = GenGetFieldAndColumn(pGen, pchField, &pTable);
	bool bSuccess = iColumn >= 0;
	if (!pGen->pResult)
		return bSuccess;
	return bSuccess && FieldFromMultiVal(pTable, iColumn, pGen->pResult, 0, pValue);
}

bool ui_GenSetEarlyOverrideDimension(UIGen *pGen, const char *pchField, F32 fMagnitude, UIUnitType eUnit)
{
	ParseTable *pTable;
	S32 iColumn = GenGetFieldAndColumn(pGen, pchField, &pTable);
	bool bSuccess = iColumn >= 0;
	if (bSuccess && !pGen->pCodeOverrideEarly)
		pGen->pCodeOverrideEarly = StructCreateVoid(pTable);
	if (bSuccess && devassertmsgf(pTable[iColumn].subtable == parse_UISizeSpec, "Invalid (non-dimensional) field given to %s", __FUNCTION__))
	{
		UISizeSpec *pSpec = TokenStoreGetPointer(pTable, iColumn, pGen->pCodeOverrideEarly, 0, NULL);
		if (pSpec)
		{
			if (pSpec->fMagnitude != fMagnitude || pSpec->eUnit != eUnit || !TokenIsSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1))
			{
				pSpec->fMagnitude = fMagnitude;
				pSpec->eUnit = eUnit;
				TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, true);
				ui_GenMarkDirty(pGen);
			}
		}
	}
	return bSuccess;
}

bool ui_GenSetEarlyOverrideAngle(UIGen *pGen, const char *pchField, F32 fMagnitude, UIAngleUnitType eUnit)
{
	ParseTable *pTable;
	S32 iColumn = GenGetFieldAndColumn(pGen, pchField, &pTable);
	bool bSuccess = iColumn >= 0;
	if (bSuccess && !pGen->pCodeOverrideEarly)
		pGen->pCodeOverrideEarly = StructCreateVoid(pTable);
	if (bSuccess && devassertmsgf(pTable[iColumn].subtable == parse_UIAngle, "Invalid (non-angular) field given to %s", __FUNCTION__))
	{
		UIAngle *pAngle = TokenStoreGetPointer(pTable, iColumn, pGen->pCodeOverrideEarly, 0, NULL);
		if (pAngle)
		{
			if (pAngle->fAngle != fMagnitude || pAngle->eUnit != eUnit || !TokenIsSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1))
			{
				pAngle->fAngle = fMagnitude;
				pAngle->eUnit = eUnit;
				TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, true);
				ui_GenMarkDirty(pGen);
			}
		}
	}
	return bSuccess;
}

bool ui_GenSetEarlyOverrideAssembly(UIGen *pGen, UITextureAssembly *pTexAs, Color4 *pTint)
{
	ParseTable *pTable;
	S32 iColumn;
	if (!pGen)
		return false;
	if (!pGen->pCodeOverrideEarly)
		pGen->pCodeOverrideEarly = StructCreateVoid(ui_GenGetType(pGen));
	iColumn = GenGetFieldAndColumn(pGen, "Assembly", &pTable);
	if (pTexAs)
	{
		if (!pGen->pCodeOverrideEarly->pAssembly)
			pGen->pCodeOverrideEarly->pAssembly = StructCreate(parse_UIGenTextureAssembly);

		if (pTexAs != GET_REF(pGen->pCodeOverrideEarly->pAssembly->hAssembly))
		{
			SET_HANDLE_FROM_REFERENT("UITextureAssembly", pTexAs, pGen->pCodeOverrideEarly->pAssembly->hAssembly);
			ui_GenMarkDirty(pGen);
		}

		if (pTint 
			&& (pTint->uiTopLeftColor != pGen->pCodeOverrideEarly->pAssembly->Colors.uiTopLeftColor
			    || pTint->uiTopRightColor != pGen->pCodeOverrideEarly->pAssembly->Colors.uiTopRightColor
			    || pTint->uiBottomLeftColor != pGen->pCodeOverrideEarly->pAssembly->Colors.uiBottomLeftColor
			    || pTint->uiBottomRightColor != pGen->pCodeOverrideEarly->pAssembly->Colors.uiBottomRightColor))
		{
			pGen->pCodeOverrideEarly->pAssembly->Colors = *pTint;
			ui_GenMarkDirty(pGen);
		}

		if (pGen->bNeedsRebuild)
		{
			TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, true);
		}
		return true;
	}
	else
	{
		if (pGen->pCodeOverrideEarly->pAssembly)
		{
			StructDestroySafe(parse_UIGenTextureAssembly, &pGen->pCodeOverrideEarly->pAssembly);
			ui_GenMarkDirty(pGen);
			TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, false);
		}
		return false;
	}
}


bool ui_GenAddEarlyOverrideChild(UIGen *pGen, UIGen *pChildGen)
{
	ParseTable *pTable;
	S32 iColumn = GenGetFieldAndColumn(pGen, "Child", &pTable);
	S32 i;
	UIGen *pParent;
	if (!(pGen && pChildGen))
		return false;
	else if (devassertmsg(iColumn > 0, "Child field does not exist"))
	{
		// Since it will result in a stack overflow. Make the error continuable.
		for (pParent = pGen; pParent != NULL; pParent = pParent->pParent)
		{
			if (!devassertmsg(pChildGen != pParent, "Attempted to add a parent gen as an override child."))
				return false;
		}

		if (!pGen->pCodeOverrideEarly)
			pGen->pCodeOverrideEarly = StructCreateVoid(pTable);
		if (ui_GenInDictionary(pChildGen))
		{
			UIGenChild *pChild = NULL;
			for (i = 0; i < eaSize(&pGen->pCodeOverrideEarly->eaChildren); i++)
			{
				if (GET_REF(pGen->pCodeOverrideEarly->eaChildren[i]->hChild) == pChildGen)
					return false;
			}
			pChild = StructCreate(parse_UIGenChild);
			SET_HANDLE_FROM_REFERENT(g_GenState.hGenDict, pChildGen, pChild->hChild);
			eaPush(&pGen->pCodeOverrideEarly->eaChildren, pChild);
			TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, true);
		}
		else
		{
			if (eaFind(&pGen->pCodeOverrideEarly->eaInlineChildren, pChildGen) >= 0)
				return false;
			eaPush(&pGen->pCodeOverrideEarly->eaInlineChildren, pChildGen);
		}
		ui_GenMarkDirty(pGen);
	}
	return true;
}

bool ui_GenClearEarlyOverrideChildren(UIGen *pGen)
{
	ParseTable *pTable;
	S32 iColumn = GenGetFieldAndColumn(pGen, "Child", &pTable);
	if (!pGen)
		return false;
	else if (devassertmsg(iColumn >= 0, "Child field does not exist") && pGen->pCodeOverrideEarly)
	{
		eaClearStruct(&pGen->pCodeOverrideEarly->eaChildren, parse_UIGenChild);
		eaClear(&pGen->pCodeOverrideEarly->eaInlineChildren);
		TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, false);
		ui_GenMarkDirty(pGen);
	}
	return true;
}

bool ui_GenRemoveEarlyOverrideChild(UIGen *pGen, UIGen *pChild)
{
	S32 i;
	if (pGen && pGen->pCodeOverrideEarly)
	{
		for (i = 0; i < eaSize(&pGen->pCodeOverrideEarly->eaChildren); i++)
		{
			if (GET_REF(pGen->pCodeOverrideEarly->eaChildren[i]->hChild) == pChild)
			{
				StructDestroy(parse_UIGenChild, eaRemove(&pGen->pCodeOverrideEarly->eaChildren, i));
				ui_GenMarkDirty(pGen);
				return true;
			}
		}
		if (eaFindAndRemove(&pGen->pCodeOverrideEarly->eaInlineChildren, pChild) >= 0)
		{
			ui_GenMarkDirty(pGen);
			return true;
		}
	}
	return false;
}

bool ui_GenAddEarlyOverrideChildTemplate(UIGen *pGen, UIGen *pChildTemplateGen)
{
	ParseTable *pTable;
	S32 iInlineColumn = GenGetFieldAndColumn(pGen, "InlineChild", &pTable);
	UIGen *pChild;

	if (!pGen || !pChildTemplateGen)
		return false;

	// Remove an existing copy of the child template
	ui_GenRemoveEarlyOverrideChildTemplate(pGen, pChildTemplateGen);

	// Add new template child
	pChild = ui_GenClone(pChildTemplateGen);
	eaIndexedAdd(&pGen->eaBorrowedInlineChildren, pChild);
	return ui_GenAddEarlyOverrideChild(pGen, pChild);
}

bool ui_GenRemoveEarlyOverrideChildTemplate(UIGen *pGen, UIGen *pChildTemplateGen)
{
	UIGen *pChild;

	if (!pGen || !pChildTemplateGen)
		return false;

	// Remove existing template child
	pChild = eaIndexedGetUsingString(&pGen->eaBorrowedInlineChildren, pChildTemplateGen->pchName);
	if (pChild)
	{
		ui_GenRemoveChild(pGen, pChild, true);
		eaFindAndRemove(&pGen->eaBorrowedInlineChildren, pChild);
		StructDestroy(parse_UIGen, pChild);
		return true;
	}
	return false;
}

bool ui_GenClearEarlyOverrideField(UIGen *pGen, const char *pchField)
{
	ParseTable *pTable;
	S32 iColumn = GenGetFieldAndColumn(pGen, pchField, &pTable);
	bool bSuccess = iColumn >= 0;
	if (bSuccess && TokenIsSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1))
	{
		TokenSetSpecified(pTable, iColumn, pGen->pCodeOverrideEarly, -1, false);
		ui_GenMarkDirty(pGen);
	}
	return bSuccess;
}

void ui_GenRunCommandInExprContext(UIGen *pGen, const char *pchCommand)
{
	char *pchFormatted = NULL;
	char *pchReturn = NULL;
	estrStackCreate(&pchReturn);
	if (strchr(pchCommand, STRFMT_TOKEN_START))
	{
		ExprContext *pContext = ui_GenGetContext(pGen);
		estrStackCreate(&pchFormatted);
		exprFormat(&pchFormatted, pchCommand, pContext, pGen->pchFilename);
	}
	GEN_PRINTF("Running: Context=%s, Command=%s", pGen->pchName, pchFormatted ? pchFormatted : pchCommand);
	if (!globCmdParseAndReturn(pchFormatted ? pchFormatted : pchCommand, &pchReturn, 0,
							   (isProductionEditMode() ? 2 : 0),
							   CMD_CONTEXT_HOWCALLED_UIGEN, NULL))
	{
		// Apparently server commands come back as "Unknown commands".
// 		ErrorFilenamef(pGen->pchFilename, "%s:\nGen is trying to run a command\n\t%s\nthat is invalid:\n\t%s", pGen->pchName,
// 			pchFormatted ? pchFormatted : pchCommand, pchReturn);
	}
	estrDestroy(&pchFormatted);
	estrDestroy(&pchReturn);
}

UIGenState ui_GenGetState(const char *pchName)
{
	return StaticDefineIntGetInt(UIGenStateEnum, pchName);
}

const char *ui_GenGetStateName(UIGenState eState)
{
	return StaticDefineIntRevLookup(UIGenStateEnum, eState);
}

const char *ui_GenGetTypeName(UIGenType eType)
{
	return StaticDefineIntRevLookup(UIGenTypeEnum, eType);
}

bool ui_GenDrawBackground(void)
{
	if (g_GenState.pBackground && g_GenState.pBackground->pchImage && *g_GenState.pBackground->pchImage)
	{
		U32 aiColors[] = {
			g_GenState.pBackground->uiTopLeft,
			g_GenState.pBackground->uiTopRight,
			g_GenState.pBackground->uiBottomRight,
			g_GenState.pBackground->uiBottomLeft,
		};
		CBox screenBox = {0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight};
		BasicTexture *pBasicTex = NULL;
		AtlasTex *pAtlasTex = NULL;
		F32 fU = 1;
		F32 fV = 1;
		F32 fScaleX = 1;
		F32 fScaleY = 1;
		if (g_GenState.pBackground->eType == UITextureModeTiled)
			pBasicTex = texLoadBasic(g_GenState.pBackground->pchImage, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
		else
			pAtlasTex = atlasLoadTexture(g_GenState.pBackground->pchImage);

		if (!aiColors[0]) aiColors[0] = 0xFFFFFFFF;
		if (!aiColors[1]) aiColors[1] = aiColors[0];
		if (!aiColors[2]) aiColors[2] = aiColors[0];
		if (!aiColors[3]) aiColors[3] = aiColors[2];

		fScaleX = g_ui_State.screenWidth / (F32)(pBasicTex ? pBasicTex->width : pAtlasTex->width);
		fScaleY = g_ui_State.screenHeight / (F32)(pBasicTex ? pBasicTex->height : pAtlasTex->height);
		if (g_GenState.pBackground->eType == UITextureModeTiled)
		{
			fScaleX = fU;
			fScaleY = fV;
		}

		display_sprite_effect_ex(pAtlasTex, pBasicTex, NULL, NULL,
			0, 0, 500, fScaleX, fScaleY, aiColors[0], aiColors[1], aiColors[2], aiColors[3],
			0, 0, fU, fV, 0, 0, 1, 1, 0, 0, clipperGetCurrent(), 0, 0, SPRITE_3D);

		g_GenState.pBackground = NULL;
		return true;
	}
	return false;
}

UIGen *ui_GenGetHighlighted(void)
{
	return g_GenState.pHighlighted;
}

void ui_GenSetHighlighted(UIGen *pGen)
{
	g_GenState.pHighlighted = pGen;
}

bool ui_GenGetTextFromExprMessage(UIGen *pGen, Expression *pTextExpr, Message *pMessage, const char *pchStaticString, unsigned char **ppchString, bool bFilterProfanity)
{
	const char *pchMessage = TranslateMessagePtr(pMessage);
	static char *s_pchOriginal;
	estrCopy(&s_pchOriginal, ppchString);
	estrClear(ppchString);
	if (pTextExpr)
	{
		MultiVal mv = {0};
		ui_GenTimeEvaluate(pGen, pTextExpr, &mv, "TextExpr");
		MultiValToEString(&mv, ppchString);
	}
	else if (pchMessage)
	{
		if (strchr(pchMessage, STRFMT_TOKEN_START))
			exprFormat(ppchString, pchMessage, ui_GenGetContext(pGen), pGen->pchFilename);
		else
			estrCopy2(ppchString, pchMessage);
	}
	else if (pchStaticString)
	{
		estrCopy2(ppchString, pchStaticString);
	}
	else
		estrCopy2(ppchString, "");

	if (!s_pchOriginal && !*ppchString)
		return false;
	else
		return (
			(s_pchOriginal && !*ppchString)
			|| (!s_pchOriginal && *ppchString)
			|| strcmp(s_pchOriginal, *ppchString)
			);
}

bool ui_GenBundleTextGetText(UIGen *pGen, UIGenBundleText *pBundle, const char *pchStaticString, unsigned char **ppchString)
{
	return ui_GenGetTextFromExprMessage(pGen, pBundle->pTextExpr, GET_REF(pBundle->hText), pchStaticString, ppchString, pBundle->bFilterProfanity);
}

UIStyleFont *ui_GenBundleTextGetFont(UIGenBundleText *pBundle)
{
	// Used to be more complicated.
	return GET_REF(pBundle->hFont);
}

bool ui_GenBundleTruncate(UIGenBundleTruncateState *pState, UIStyleFont *pFont, Message *pTruncateMessage, F32 fWidth, F32 fScale, const unsigned char *pchText, unsigned char **ppchString)
{
	if (pTruncateMessage && pchText && pFont && (ui_StyleFontWidth(pFont, fScale, pchText) > fWidth))
	{
		unsigned int iGlyphs, i, iLen = 0;
		Vec2 v2Area;
		if (pTruncateMessage != pState->pPreviousTruncateMessage)
		{
			pState->pchTruncateString = langTranslateMessage(locGetLanguage(getCurrentLocale()), pTruncateMessage);
			pState->fTruncateWidth = ui_StyleFontWidth(pFont, fScale, pState->pchTruncateString);
			pState->pPreviousTruncateMessage = pTruncateMessage;
		}
		v2Area[0] = fWidth - pState->fTruncateWidth;
		v2Area[1] = 1000000.0f;
		iGlyphs = ui_StyleFontCountGlyphsInArea(pFont, 1.0f, pchText, v2Area);
		// Count how many bytes are in the glyphs
		for (i = 0; i < iGlyphs; i++)
			iLen += UTF8GetCodepointLength(pchText + iLen);
		if (*ppchString != pchText)
			estrCopy2(ppchString, pchText);
		estrReplaceRangeWithString(ppchString, iLen, estrLength(ppchString) - iLen, pState->pchTruncateString);
		return true;
	}
	else
	{
		pState->pPreviousTruncateMessage = NULL;
		return false;
	}
}

void ui_GenGetTransformedCBoxBounds(const CBox* pBoxIn, Mat3 Matrix, CBox* pBoxOut)
{
	int i;
	Vec3 v3out;
	Vec3 v3points[4] =
	{
		{ pBoxIn->lx, g_ui_State.screenHeight - pBoxIn->ly, 1.0f },
		{ pBoxIn->hx, g_ui_State.screenHeight - pBoxIn->ly, 1.0f },
		{ pBoxIn->hx, g_ui_State.screenHeight - pBoxIn->hy, 1.0f },
		{ pBoxIn->lx, g_ui_State.screenHeight - pBoxIn->hy, 1.0f }
	};
	pBoxOut->lx = 1000000.0f;
	pBoxOut->ly = 1000000.0f;
	pBoxOut->hx = -1000000.0f;
	pBoxOut->hy = -1000000.0f;
	for (i = 0; i < 4; i++)
	{
		mulVecMat3(v3points[i], Matrix, v3out);
		MIN1(pBoxOut->lx, v3out[0]);
		MAX1(pBoxOut->hx, v3out[0]);
		MIN1(pBoxOut->ly, g_ui_State.screenHeight - v3out[1]);
		MAX1(pBoxOut->hy, g_ui_State.screenHeight - v3out[1]);
	}
	MAX1(pBoxOut->lx, pBoxIn->lx);
	MAX1(pBoxOut->hx, pBoxIn->lx);
	MAX1(pBoxOut->ly, pBoxIn->ly);
	MAX1(pBoxOut->hy, pBoxIn->ly);
}

void ui_GenFitContentsSize(UIGen *pGen, UIGenInternal *pInt, CBox *pBox)
{
	UITextureAssembly *pAssembly = ui_GenTextureAssemblyGetAssembly(pGen, pInt->pAssembly);

	if (g_GenState.aFuncTable[pGen->eType].cbFitContentsSize)
		g_GenState.aFuncTable[pGen->eType].cbFitContentsSize(pGen, pInt, pBox);
	if (pAssembly)
	{
		pBox->hx += pAssembly->iPaddingLeft + pAssembly->iPaddingRight;
		pBox->hy += pAssembly->iPaddingTop + pAssembly->iPaddingBottom;
	}
	if (pInt->pAssembly)
	{
		pBox->hx += pInt->pAssembly->iPaddingLeft + pInt->pAssembly->iPaddingRight;
		pBox->hy += pInt->pAssembly->iPaddingTop + pInt->pAssembly->iPaddingBottom;
	}

	if (UI_GEN_USE_MATRIX(pInt))
	{
		CBoxMoveX(pBox, pGen->UnpaddedScreenBox.lx);
		CBoxMoveY(pBox, pGen->UnpaddedScreenBox.ly);
		ui_GenGetTransformedCBoxBounds(pBox, pGen->TransformationMatrix, &pGen->BoundingBox);
		CBoxMoveX(pBox, -pGen->UnpaddedScreenBox.lx);
		CBoxMoveY(pBox, -pGen->UnpaddedScreenBox.ly);
	}
	else
	{
		pGen->BoundingBox = *pBox;
	}
}

void ui_GenBundleTextureUpdate(UIGen *pGen, const UIGenBundleTexture *pBundle, UIGenBundleTextureState *pState)
{
	if ( pState==NULL )
		return;

	// Figure out what textures we're actually going to load.
	if (pBundle->pchTexture || pState->pBasicTexture)
	{
		if (!pState->pBasicTexture)
		{
			if (pBundle->pchTexture && strchr(pBundle->pchTexture, STRFMT_TOKEN_START))
			{
				static char *s_pchTexFormatted;
				char ach[MAX_PATH];
				estrClear(&s_pchTexFormatted);
				exprFormat(&s_pchTexFormatted, pBundle->pchTexture, ui_GenGetContext(pGen), pGen->pchFilename);
				if (pBundle->bSkinningOverride)
				{
					const char *pchOverride = ui_GenGetTextureSkinOverride(s_pchTexFormatted);
					UI_GEN_LOAD_TEXTURE(pchOverride, pState->pTexture)
				}
				else if (g_texture_name_fixup && s_pchTexFormatted && *s_pchTexFormatted)
				{
					g_texture_name_fixup(s_pchTexFormatted, SAFESTR(ach));
					UI_GEN_LOAD_TEXTURE(ach, pState->pTexture)
				}
				else
				{
					UI_GEN_LOAD_TEXTURE(s_pchTexFormatted, pState->pTexture)
				}
			}
			else if (pBundle->bSkinningOverride)
			{
				const char *pchOverride = ui_GenGetTextureSkinOverride(pBundle->pchTexture);
				UI_GEN_LOAD_TEXTURE(pchOverride, pState->pTexture)
			}
			else
			{
				UI_GEN_LOAD_TEXTURE(pBundle->pchTexture, pState->pTexture)
			}
		}
		else
			pState->pTexture = NULL;
	}
	else
		pState->pTexture = NULL;

	if ( pBundle->pAnimation )
	{
		if (pState->pAnimState==NULL)
		{
			pState->pAnimState = StructCreate( parse_UIGenBundleTextureAnimationState );
		}

		if ( pBundle->pAnimation->pchArmTexture && pBundle->pAnimation->pchArmTexture[0] )
		{
			pState->pAnimState->pArmTexture = atlasLoadTexture(pBundle->pAnimation->pchArmTexture);
		}
		else
		{
			pState->pAnimState->pArmTexture = NULL;
		}

		// Evaluate the animation progress expression, update the state
		if ( pBundle->pAnimation->pProgress )
		{
			MultiVal mv;
			ui_GenEvaluate(pGen, pBundle->pAnimation->pProgress, &mv);
			pState->pAnimState->fAnimProgress = MultiValGetFloat(&mv, NULL);
			
			if ( pBundle->pAnimation->bRepeat )
			{
				F32 fProgress = pState->pAnimState->fAnimProgress;
				pState->pAnimState->fAnimProgress = fProgress-(S32)(fProgress);
				if ( fProgress < 0.0f )
				{
					pState->pAnimState->fAnimProgress += 1.0f;
				}
			}

			pState->pAnimState->fAnimProgress = CLAMPF32( pState->pAnimState->fAnimProgress, 0.0f, 1.0f );
		}
	}
}

bool ui_GenBundleTextureFitContentsSize(UIGen *pGen, const UIGenBundleTexture *pBundle, CBox *pOut, UIGenBundleTextureState *pState)
{
	if (pState)
	{
		if (pState->pBasicTexture)
		{
			BuildCBox(pOut, 0, 0, texWidth(pState->pBasicTexture), texHeight(pState->pBasicTexture));
			return true;
		}
		if (pState->pTexture)
		{
			BuildCBox(pOut, 0, 0, pState->pTexture->width, pState->pTexture->height);
			return true;
		}
	}
	return false;
}

static void ui_GenBundleTexSweepDrawUpperCornerTri(	const UIGenBundleTexture *pBundle,
													AtlasTex *pTex, BasicTexture *pBasicTex, CBox *pBox,
													F32 fHalfWidth, F32 fHalfHeight, S32 iDir, U32 aiColors[4] )
{
	Vec2 v0, v1, v2, t0, t1, t2;
	U32 c[3];
	setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
	setVec2( v1, v0[0], pBox->ly );
	setVec2( v2, iDir>0?pBox->lx:pBox->hx, pBox->ly );
	setVec2( t0, 0.5f, 0.5f );
	setVec2( t1, 0.5f, 0.0f );
	setVec2( t2, iDir>0?0.0f:1.0f, 0.0f );
	setVec3( c, interpBilinearColor(0.5f,0.5f,aiColors), interpColor(0.5f,aiColors[0],aiColors[1]), iDir>0?aiColors[0]:aiColors[1]);
	display_sprite_triangle( pTex, pBasicTex, v0[0], v0[1], v1[0], v1[1], v2[0], v2[1], UI_GET_Z(), 
							c[0], c[1], c[2], t0[0], t0[1], t1[0], t1[1], t2[0], t2[1], pBundle->bAdditive,
							clipperGetCurrent(), pBundle->eEffect, 1.0f, false );
}

static void ui_GenBundleTexSweepDrawSide1Tri(	const UIGenBundleTexture *pBundle,
												AtlasTex *pTex, BasicTexture *pBasicTex, CBox *pBox,
												F32 fHalfWidth, F32 fHalfHeight, S32 iDir, U32 aiColors[4] )
{
	Vec2 v0, v1, v2, t0, t1, t2;
	U32 c[3];
	setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
	setVec2( v1, iDir>0?pBox->hx:pBox->lx, pBox->ly );
	setVec2( v2, iDir>0?pBox->hx:pBox->lx, pBox->hy );
	setVec2( t0, 0.5f, 0.5f );
	setVec2( t1, iDir>0?1.0f:0.0f, 0.0f );
	setVec2( t2, iDir>0?1.0f:0.0f, 1.0f );
	setVec3( c, interpBilinearColor(0.5f,0.5f,aiColors), iDir>0?aiColors[1]:aiColors[0], iDir>0?aiColors[2]:aiColors[3]);
	display_sprite_triangle( pTex, pBasicTex, v0[0], v0[1], v1[0], v1[1], v2[0], v2[1], UI_GET_Z(), 
							c[0], c[1], c[2], t0[0], t0[1], t1[0], t1[1], t2[0], t2[1], pBundle->bAdditive,
							clipperGetCurrent(), pBundle->eEffect, 1.0f, false );
}

static void ui_GenBundleTexSweepDrawBottomTri(	const UIGenBundleTexture *pBundle,
												AtlasTex *pTex, BasicTexture *pBasicTex, CBox *pBox,
												F32 fHalfWidth, F32 fHalfHeight, S32 iDir, U32 aiColors[4] )
{
	Vec2 v0, v1, v2, t0, t1, t2;
	U32 c[3];
	setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
	setVec2( v1, pBox->hx, pBox->hy );
	setVec2( v2, pBox->lx, pBox->hy );
	setVec2( t0, 0.5f, 0.5f );
	setVec2( t1, 1.0f, 1.0f );
	setVec2( t2, 0.0f, 1.0f );
	setVec3( c, interpBilinearColor(0.5f,0.5f,aiColors), aiColors[2], aiColors[3]);
	display_sprite_triangle( pTex, pBasicTex, v0[0], v0[1], v1[0], v1[1], v2[0], v2[1], UI_GET_Z(), 
							c[0], c[1], c[2], t0[0], t0[1], t1[0], t1[1], t2[0], t2[1], pBundle->bAdditive,
							clipperGetCurrent(), pBundle->eEffect, 1.0f, false );
}

static void ui_GenBundleTexSweepDrawSide2Tri(	const UIGenBundleTexture *pBundle,
												AtlasTex *pTex, BasicTexture *pBasicTex, CBox *pBox,
												F32 fHalfWidth, F32 fHalfHeight, S32 iDir, U32 aiColors[4] )
{
	Vec2 v0, v1, v2, t0, t1, t2;
	U32 c[3];
	setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
	setVec2( v1, iDir>0?pBox->lx:pBox->hx, pBox->ly );
	setVec2( v2, iDir>0?pBox->lx:pBox->hx, pBox->hy );
	setVec2( t0, 0.5f, 0.5f );
	setVec2( t1, iDir>0?0.0f:1.0f, 0.0f );
	setVec2( t2, iDir>0?0.0f:1.0f, 1.0f );
	setVec3( c, interpBilinearColor(0.5f,0.5f,aiColors), iDir>0?aiColors[0]:aiColors[1], iDir>0?aiColors[3]:aiColors[2]);
	display_sprite_triangle( pTex, pBasicTex, v0[0], v0[1], v1[0], v1[1], v2[0], v2[1], UI_GET_Z(), 
							c[0], c[1], c[2], t0[0], t0[1], t1[0], t1[1], t2[0], t2[1], pBundle->bAdditive,
							clipperGetCurrent(), pBundle->eEffect, 1.0f, false );
}

static void ui_GenBundleTextureDrawSweep(const UIGenBundleTexture *pBundle, UIGenBundleTextureState *pState,
										 CBox *pBox, AtlasTex *pTex, BasicTexture *pBasicTex, AtlasTex *pMask,
										 F32 fScaleX, F32 fScaleY, F32 fRepeatX, F32 fRepeatY,
										 U32 aiColors[4])
{
	Vec2 v0, v1, v2, t0, t1, t2, vRepeat, vScale;
	U32 c[3];
	F32 fProgress = pState->pAnimState->fAnimProgress;
	F32 fAngle = fProgress * TWOPI;
	F32 fWidth = CBoxWidth(pBox);
	F32 fHeight = CBoxHeight(pBox);
	F32 fHalfWidth = fWidth * 0.5f; 
	F32 fHalfHeight = fHeight * 0.5f;
	F32 fStartAngle = atan(fHalfWidth/fHalfHeight);
	S32 iDir = pBundle->pAnimation->eStyle == UIGenAnimatedTextureStyleSweepRight ? 1 : -1;

	PERFINFO_AUTO_START_FUNC();

	setVec2( vScale, fScaleX, fScaleY );
	setVec2( vRepeat, fRepeatX, fRepeatY );

	if ( fAngle <= fStartAngle )
	{	
		//draw un-animated triangles
		ui_GenBundleTexSweepDrawSide1Tri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);
		ui_GenBundleTexSweepDrawBottomTri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);
		ui_GenBundleTexSweepDrawSide2Tri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);
		ui_GenBundleTexSweepDrawUpperCornerTri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);

		//compute animated triangle parameters
		setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
		setVec2( v1, iDir > 0 ? pBox->hx : pBox->lx, pBox->ly );
		setVec2( v2, CLAMPF32(v0[0] + iDir * fHalfHeight * tan( fAngle ),pBox->lx,pBox->hx), pBox->ly );
		setVec2( t0, 0.5f, 0.5f );
		setVec2( t1, iDir > 0 ? 1.0f : 0.0f, 0.0f );
		setVec2( t2, (v2[0]-pBox->lx)/fWidth, 0.0f );
		c[0] = interpBilinearColor(0.5,0.5,aiColors);
		c[1] = iDir > 0 ? aiColors[1] : aiColors[0];
		c[2] = interpColor(t2[0],aiColors[0],aiColors[1]);
	}
	else if ( fAngle <= HALFPI + (HALFPI - fStartAngle) )
	{
		//draw static triangles
		ui_GenBundleTexSweepDrawBottomTri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);
		ui_GenBundleTexSweepDrawSide2Tri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);
		ui_GenBundleTexSweepDrawUpperCornerTri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);

		//compute current animated triangle parameters
		setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
		setVec2( v1, iDir > 0 ? pBox->hx : pBox->lx, pBox->hy );
		v2[0] = v1[0];
		if ( fAngle <= HALFPI )
			v2[1] = CLAMPF32(pBox->ly + fHalfHeight - fHalfWidth * tan( HALFPI - fAngle ),pBox->ly,pBox->hy);
		else
			v2[1] = CLAMPF32(pBox->ly + fHalfHeight + fHalfWidth * tan( fAngle - HALFPI ),pBox->ly,pBox->hy);
		setVec2( t0, 0.5f, 0.5f );
		setVec2( t1, iDir > 0 ? 1.0f : 0.0f, 1.0f );
		setVec2( t2, t1[0], (v2[1]-pBox->ly)/fHeight );
		c[0] = interpBilinearColor(0.5,0.5,aiColors);
		c[1] = iDir > 0 ? aiColors[2] : aiColors[3];
		c[2] = iDir > 0 ? interpColor(t2[1],aiColors[1],aiColors[2]) : interpColor(t2[1],aiColors[0],aiColors[3]);
	}
	else if ( fAngle <= PI + fStartAngle )
	{
		//draw static triangles
		ui_GenBundleTexSweepDrawSide2Tri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);
		ui_GenBundleTexSweepDrawUpperCornerTri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);

		//compute current animated triangle parameters
		setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
		setVec2( v1, iDir > 0 ? pBox->lx : pBox->hx, pBox->hy );
		if ( fAngle <= PI )
			v2[0] = CLAMPF32(pBox->lx + fHalfWidth + iDir * fHalfHeight * tan( PI - fAngle ),pBox->lx,pBox->hx);
		else
			v2[0] = CLAMPF32(pBox->lx + fHalfWidth - iDir * fHalfHeight * tan( fAngle - PI ),pBox->lx,pBox->hx);
		v2[1] = pBox->hy;
		setVec2( t0, 0.5f, 0.5f );
		setVec2( t1, iDir > 0 ? 0.0f : 1.0f, 1.0f );
		setVec2( t2, (v2[0]-pBox->lx)/fWidth, 1.0f );
		c[0] = interpBilinearColor(0.5f,0.5f,aiColors);
		c[1] = iDir > 0 ? aiColors[3] : aiColors[2];
		c[2] = interpColor(t2[0],aiColors[2],aiColors[3]);
	}
	else if ( fAngle <= TWOPI - fStartAngle )
	{
		F32 fThreeHalvesPI = PI + HALFPI;

		//draw static triangles
		ui_GenBundleTexSweepDrawUpperCornerTri(pBundle,pTex,pBasicTex,pBox,fHalfWidth,fHalfHeight,iDir,aiColors);

		//compute current animated triangle parameters
		setVec2( v0, pBox->lx+fHalfWidth, pBox->ly+fHalfHeight );
		setVec2( v1, iDir > 0 ? pBox->lx : pBox->hx, pBox->ly );
		v2[0] = v1[0];
		if ( fAngle <= fThreeHalvesPI )
			v2[1] = CLAMPF32(pBox->ly + fHalfHeight + fHalfWidth * tan( fThreeHalvesPI - fAngle ),pBox->ly,pBox->hy);
		else
			v2[1] = CLAMPF32(pBox->ly + fHalfHeight - fHalfWidth * tan( fAngle - fThreeHalvesPI ),pBox->ly,pBox->hy);
		setVec2( t0, 0.5f, 0.5f );
		setVec2( t1, iDir > 0 ? 0.0f : 1.0f, 0.0f );
		setVec2( t2, t1[0], (v2[1]-pBox->ly)/fHeight );
		c[0] = interpBilinearColor(0.5f,0.5f,aiColors);
		c[1] = iDir > 0 ? aiColors[0] : aiColors[1];
		c[2] = iDir > 0 ? interpColor(t2[1],aiColors[0],aiColors[3]) : interpColor(t2[1],aiColors[1],aiColors[2]);
	}
	else
	{
		//compute current animated triangle parameters
		setVec2( v0, pBox->lx + fHalfWidth, pBox->ly + fHalfHeight );
		setVec2( v1, v0[0], pBox->ly ); 
		setVec2( v2, CLAMPF32(v0[0] + iDir * fHalfHeight * tan( fAngle ),pBox->lx,pBox->hx), pBox->ly );
		setVec2( t0, 0.5f, 0.5f );
		setVec2( t1, 0.5f, 0.0f );
		setVec2( t2, (v2[0]-pBox->lx)/fWidth, 0.0f );
		c[0] = interpBilinearColor(0.5f,0.5f,aiColors);
		c[1] = interpColor(0.5f,aiColors[0],aiColors[1]);
		c[2] = interpColor(t2[0],aiColors[0],aiColors[1]);
	}

	//draw the currently animating triangle
	display_sprite_triangle( pTex, pBasicTex, v0[0], v0[1], v1[0], v1[1], v2[0], v2[1], UI_GET_Z(), 
						c[0], c[1], c[2], t0[0], t0[1], t1[0], t1[1], t2[0], t2[1], pBundle->bAdditive,
						clipperGetCurrent(), pBundle->eEffect, 1.0f, false );

	clipperPushRestrict(pBox);
	//draw the arm sprite, if the texture exists
	if ( pState->pAnimState->pArmTexture )
	{
		int rgba = RGBAFromColor(ColorWhite);
		F32 fCos, fSin, fTLx, fTLy;
		F32 fTW = pState->pAnimState->pArmTexture->width;
		F32 fTH = pState->pAnimState->pArmTexture->height;
		F32 fHalfTW = fTW * 0.5f;
		F32 fHalfTH = fTH * 0.5f;
		F32 fAngleTex = iDir * fAngle;
		sincosf( fAngleTex, &fSin, &fCos );
		fTLx = pBox->lx + fHalfWidth - fHalfTW - iDir * fHalfTW * fCos + fHalfTH * fSin;
		fTLy = pBox->ly + fHalfHeight - fHalfTH - iDir * fHalfTW * fSin - fHalfTH * fCos;
		//fTLx = pBox->lx + fHalfWidth - fHalfTW + (iDir * fHalfTW * fCos + fHalfTH * fSin);
		//fTLy = pBox->ly + fHalfHeight - fHalfTH + (iDir * fHalfTW * fSin - fHalfTH * fCos);
		display_sprite_ex(	pState->pAnimState->pArmTexture, NULL, NULL, NULL,
							fTLx, fTLy, UI_GET_Z(), 1, 1, rgba, rgba, rgba, rgba, 0, 0, 1, 1, 0, 0, 1, 1, 
							fAngleTex, 0, clipperGetCurrent() );
	}
	clipperPop();
	PERFINFO_AUTO_STOP_FUNC();
}

static void ui_GenBundleTextureDrawFill(const UIGenBundleTexture *pBundle, UIGenBundleTextureState *pState,
										CBox *pBox, AtlasTex *pTex, BasicTexture *pBasicTex, AtlasTex *pMask,
										F32 fScaleX, F32 fScaleY, F32 fRepeatX, F32 fRepeatY,
										U32 aiColors[4]) 
{
	CBox clipBox;
	Vec2 vArmPoint;
	F32 fArmAngle;
	F32 fProgress = pState->pAnimState->fAnimProgress;
	F32 tw, th, halftw, halfth;
	
	if ( pState->pAnimState->pArmTexture )
	{
		tw = pState->pAnimState->pArmTexture->width;
		th = pState->pAnimState->pArmTexture->height;
		halftw = tw*0.5f;
		halfth = th*0.5f;
	}

	//compute clip region and arm parameters 
	switch (pBundle->pAnimation->eStyle)
	{
		xcase UIGenAnimatedTextureStyleFillUp:
		{
			clipBox.lx = pBox->lx;
			clipBox.ly = pBox->ly + CBoxHeight(pBox) * (1.0f - fProgress);
			clipBox.hx = pBox->hx;
			clipBox.hy = pBox->hy;

			if ( pState->pAnimState->pArmTexture )
			{
				setVec2(vArmPoint, clipBox.lx-(halftw-halfth), clipBox.ly-(halfth-halftw)-1);
				fArmAngle = -HALFPI;
			}
		}
		xcase UIGenAnimatedTextureStyleFillDown:
		{
			clipBox.lx = pBox->lx;
			clipBox.ly = pBox->ly;
			clipBox.hx = pBox->hx;
			clipBox.hy = pBox->ly + CBoxHeight(pBox) * fProgress;

			if ( pState->pAnimState->pArmTexture )
			{
				setVec2(vArmPoint, clipBox.hx-tw+(halftw-halfth), clipBox.hy-th+(halfth-halftw)+1);
				fArmAngle = HALFPI;
			}
		}
		xcase UIGenAnimatedTextureStyleFillLeft:
		{
			clipBox.lx = pBox->lx + CBoxWidth(pBox) * (1.0f - fProgress);
			clipBox.ly = pBox->ly;
			clipBox.hx = pBox->hx;
			clipBox.hy = pBox->hy;

			if ( pState->pAnimState->pArmTexture )
			{
				setVec2(vArmPoint, clipBox.lx-1, clipBox.hy-th);
				fArmAngle = PI;
			}
		}
		xcase UIGenAnimatedTextureStyleFillRight:
		{
			clipBox.lx = pBox->lx;
			clipBox.ly = pBox->ly;
			clipBox.hx = pBox->lx + CBoxWidth(pBox) * fProgress;
			clipBox.hy = pBox->hy;
			
			if ( pState->pAnimState->pArmTexture )
			{
				setVec2(vArmPoint, clipBox.hx-tw+1, clipBox.ly);
				fArmAngle = 0.0f;
			}
		}
		xdefault:
		{
			return;
		}
	}
	clipperPushRestrict(&clipBox);
	display_sprite_effect_ex(pTex, pBasicTex, pMask, NULL, pBox->lx, pBox->ly, UI_GET_Z(),
							fScaleX, fScaleY, aiColors[0], aiColors[1], aiColors[2], aiColors[3], 0, 0,
							fRepeatX, fRepeatY, 0, 0, fRepeatX, fRepeatY, 
							UI_ANGLE_TO_RAD(pBundle->Rotation), pBundle->bAdditive, clipperGetCurrent(), 
							pBundle->eEffect, 1.f,SPRITE_2D);
	clipperPop();
	clipperPushRestrict(pBox);
	//draw the arm sprite, if the texture exists
	if ( pState->pAnimState->pArmTexture )
	{
		int rgba = 0xFFFFFFFF;
		display_sprite_ex(	pState->pAnimState->pArmTexture,NULL,NULL,NULL,
							vArmPoint[0],vArmPoint[1],UI_GET_Z(),
							1,1,rgba,rgba,rgba,rgba,0,0,1,1,0,0,1,1,
							fArmAngle,0,clipperGetCurrent());
	}
	clipperPop();
}

static void ui_GenBundleTextureDrawScroll(	const UIGenBundleTexture *pBundle, UIGenBundleTextureState *pState,
											CBox *pBox, AtlasTex *pTex, BasicTexture *pBasicTex, AtlasTex *pMask,
											F32 fScaleX, F32 fScaleY, F32 fRepeatX, F32 fRepeatY,
											U32 aiColors[4]) 
{
	F32 fProgress = 1.0f-pState->pAnimState->fAnimProgress;
	F32 fU0, fU1, fV0, fV1;

	if ( pBundle->pAnimation->eStyle == UIGenAnimatedTextureStyleScrollHorizontal )
	{
		fU0 = interpF32( fProgress, 0.0f, 1.0f );
		fU1 = fU0 + 1.0f;
		fV0 = 0.0f;
		fV1 = 1.0f;
	}
	else
	{
		fU0 = 0.0f;
		fU1 = 1.0f;
		fV0 = interpF32( fProgress, 0.0f, 1.0f );
		fV1 = fV0 + 1.0f;
	}

	display_sprite_effect_ex(pTex, pBasicTex, pMask, NULL, pBox->lx, pBox->ly, UI_GET_Z(),
							fScaleX, fScaleY, aiColors[0], aiColors[1], aiColors[2], aiColors[3], 
							fU0, fV0, fU1, fV1, fU0, fV0, fU1, fV1, 
							UI_ANGLE_TO_RAD(pBundle->Rotation), pBundle->bAdditive, clipperGetCurrent(), 
							pBundle->eEffect, 1.f, SPRITE_2D);
}

void ui_GenBundleTextureDraw(UIGen *pGen, UIGenInternal *pInt, const UIGenBundleTexture *pBundle, CBox *pBox, F32 fX, F32 fY, bool bCenterX, bool bCenterY, UIGenBundleTextureState *pState, CBox *pOut)
{
	AtlasTex *pTex = pState->pTexture;
	BasicTexture *pBasicTex = pState->pBasicTexture;
	U32 aiColors[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
	F32 fRepeatX = 1;
	F32 fRepeatY = 1;
	F32 fScaleX = pGen->fScale;
	F32 fScaleY = pGen->fScale;
	F32 fWidth = 1;
	F32 fHeight = 1;
	CBox box = {fX, fY, fX, fY};
	AtlasTex *pMask = (pBundle->pchMask && *pBundle->pchMask) ? atlasLoadTexture(pBundle->pchMask) : NULL;

	if (pState->pBasicTexture && !gfxHeadshotRaisePriority(pState->pBasicTexture))
		pBasicTex = NULL;

	if (pTex || pBasicTex)
	{
		if ((pBundle->uiBottomLeftColor && pBundle->uiBottomLeftColor != 0xFFFFFFFF)
			|| (pBundle->uiTopRightColor  && pBundle->uiTopRightColor != 0xFFFFFFFF)
			|| (pBundle->uiBottomRightColor && pBundle->uiBottomRightColor != 0xFFFFFFFF))
		{
			aiColors[0] = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pBundle->uiTopLeftColor), pGen->chAlpha);
			aiColors[1] = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pBundle->uiTopRightColor), pGen->chAlpha);
			aiColors[2] = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pBundle->uiBottomRightColor), pGen->chAlpha);
			aiColors[3] = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pBundle->uiBottomLeftColor), pGen->chAlpha);
		}
		else
		{
			aiColors[0] = aiColors[1] = aiColors[2] = aiColors[3] = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pBundle->uiTopLeftColor), pGen->chAlpha);
		}

		if (pBundle->eMode == UITextureModeTiled && pTex && !pBasicTex)
		{
			pBasicTex = texLoadBasic(pTex->name, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
		}

		if (pTex)
		{
			fWidth = pTex->width;
			fHeight = pTex->height;
		}

		if (pBasicTex)
		{
			fWidth = texWidth(pBasicTex);
			fHeight = texHeight(pBasicTex);
			pTex = NULL;
		}

		if (pBundle->eMode == UITextureModeStretched || pBundle->eMode == UITextureModeNinePatch)
		{
			// Should be unconditionally sized to the ScreenBox
			pBox = &pGen->ScreenBox;
		}

		if (!pBox)
		{
			pBox = &box;
			BuildCBox(pBox, fX - (bCenterX ? fWidth * pGen->fScale / 2 : 0), fY - (bCenterY ? fHeight * pGen->fScale / 2 : 0), fWidth * pGen->fScale, fHeight * pGen->fScale);
		}

		if (pBundle->eMode == UITextureModeTiled)
		{
			fRepeatX = CBoxWidth(pBox) / fWidth;
			fRepeatY = CBoxHeight(pBox) / fHeight;
		}
		if (pBundle->eMode == UITextureModeCentered)
		{
			F32 fCenterX;
			F32 fCenterY;
			CBoxGetCenter(pBox, &fCenterX, &fCenterY);
			BuildCBox(pBox, fCenterX - fWidth * pGen->fScale / 2, fCenterY - fHeight * pGen->fScale / 2, fWidth * pGen->fScale, fHeight * pGen->fScale);
		}
		else if(pBundle->eMode == UITextureModeNinePatch)
		{
			// For NinePatch, we want fScaleX and fScaleY to be pGen->fScale, NOT whatever scaled madness happens in the != UITextureModeNone case below.
			if (pBundle->eAlignment)
			{
				box = *pBox;
				pBox = &box;
				ui_AlignCBox(&pGen->ScreenBox, pBox, pBundle->eAlignment);
			}
		}
		else if (pBundle->eMode != UITextureModeNone)
		{
			if (pBundle->eAlignment)
			{
				box = *pBox;
				pBox = &box;
			}

			fScaleX = CBoxWidth(pBox) / fWidth;
			fScaleY = CBoxHeight(pBox) / fHeight;
			if (pBundle->eMode == UITextureModeScaled)
			{
				F32 fCenterX;
				F32 fCenterY;
				fScaleX = fScaleY = min(fScaleX, fScaleY);
				CBoxGetCenter(pBox, &fCenterX, &fCenterY);
				BuildCBox(pBox, fCenterX - fWidth * fScaleX / 2, fCenterY - fHeight * fScaleY / 2, fWidth * fScaleX, fHeight * fScaleY);
			}
			else if (pBundle->eMode == UITextureModeFilled)
			{
				F32 fCenterX;
				F32 fCenterY;
				fScaleX = fScaleY = max(fScaleX, fScaleY);
				CBoxGetCenter(pBox, &fCenterX, &fCenterY);
				BuildCBox(pBox, fCenterX - fWidth * fScaleX / 2, fCenterY - fHeight * fScaleY / 2, fWidth * fScaleX, fHeight * fScaleY);
			}

			if (pBundle->eAlignment)
			{
				ui_AlignCBox(&pGen->ScreenBox, pBox, pBundle->eAlignment);
			}
		}
		else
		{
			if (pBundle->eAlignment)
			{
				box.lx = 0;
				box.ly = 0;
				box.hx = fWidth * fScaleX;
				box.hy = fHeight * fScaleY;
				pBox = &box;
				ui_AlignCBox(&pGen->ScreenBox, pBox, pBundle->eAlignment);
			}
		}
	}

	if (pBasicTex || pTex)
	{
		const NinePatch *pRend9 = pBundle->eMode == UITextureModeNinePatch ? texGetNinePatch(pBundle->pchTexture) : NULL;
		const NinePatch *pMask9 = pBundle->pchMask && pBundle->eMode == UITextureModeNinePatch ? texGetNinePatch(pBundle->pchMask) : NULL;
		if (pRend9 || pMask9)
		{
			if (pRend9)
				ui_GenDrawNinePatchAtlas(pBundle, pBox, pTex, pRend9, pTex, pMask, UI_GET_Z(), fScaleX, fScaleY, aiColors[0], aiColors[1], aiColors[2], aiColors[3]);
			else
				ui_GenDrawNinePatchAtlas(pBundle, pBox, pMask, pMask9, pTex, pMask, UI_GET_Z(), fScaleX, fScaleY, aiColors[0], aiColors[1], aiColors[2], aiColors[3]);
		}
		else if (pBundle->pAnimation==NULL)
		{
			F32 pfTexU[4] = {0, fRepeatX, 0, fRepeatX};
			F32 pfTexV[4] = {0, fRepeatY, 0, fRepeatY};
			F32 targetDepth = gfxGetTargetEntityDepth();
			bool useTargetDepth = false;
			SpriteProperties *pSpriteProperties = GenSpritePropGetCurrent();

			if (pBundle->eMode == UITextureModeTiled)
			{
				const UIGenTextureCoordinateData* pTexCoordData = &pBundle->TexCoordData;
				const UIGenTextureCoordinateData* pMaskCoordData = &pBundle->MaskCoordData;
				pfTexU[0] = pTexCoordData->fOffsetU;
				pfTexU[1] = pTexCoordData->fOffsetU + fRepeatX * pTexCoordData->fScaleU / pGen->fScale;
				pfTexV[0] = pTexCoordData->fOffsetV;
				pfTexV[1] = pTexCoordData->fOffsetV + fRepeatY * pTexCoordData->fScaleV / pGen->fScale;

				pfTexU[2] = pMaskCoordData->fOffsetU;
				pfTexU[3] = pMaskCoordData->fOffsetU + fRepeatX * pMaskCoordData->fScaleU / pGen->fScale;
				pfTexV[2] = pMaskCoordData->fOffsetV;
				pfTexV[3] = pMaskCoordData->fOffsetV + fRepeatY * pMaskCoordData->fScaleV / pGen->fScale;
			}
			else if (pBundle->eMode == UITextureModeHeadshotScaled && pBasicTex)
			{
				F32 headshotPixels = (float)texWidth(pBasicTex) / texHeight(pBasicTex) * (pBox->hy - pBox->ly);
				F32 boxPixels = (pBox->hx - pBox->lx);
				F32 fClipU = (headshotPixels - boxPixels) / headshotPixels / 2.f;
				pfTexU[0] = fClipU;
				pfTexU[1] = fRepeatX - fClipU;
			}
			
			if ((pBundle->bTargetEntityDepth || (pSpriteProperties && pSpriteProperties->is_3D)) && gfxStereoscopicActive())
			{
				if (targetDepth < UI_TARGET_DEPTH_THRESHOLD)
					targetDepth = UI_TARGET_DEPTH_DEFAULT;
				if (pSpriteProperties && pSpriteProperties->is_3D)
					targetDepth = pSpriteProperties->screen_distance;
				useTargetDepth = true;
			}

			display_sprite_effect_ex(
				pTex, pBasicTex, pMask, NULL, pBox->lx, pBox->ly, useTargetDepth ? targetDepth : UI_GET_Z(),
				fScaleX, fScaleY, aiColors[0], aiColors[1], aiColors[2], aiColors[3],
				pfTexU[0], pfTexV[0], pfTexU[1], pfTexV[1],
				pfTexU[2], pfTexV[2], pfTexU[3], pfTexV[3], 
				UI_ANGLE_TO_RAD(pBundle->Rotation), pBundle->bAdditive, clipperGetCurrent(), 
				pBundle->eEffect, 1.f, useTargetDepth ? (SPRITE_3D | (((!pSpriteProperties) || pSpriteProperties->ignore_depth_test) ? SPRITE_IGNORE_Z_TEST : 0)) : SPRITE_2D);
		}
		else //animation is specified, determine what type to draw
		{
			switch(pBundle->pAnimation->eStyle)
			{
				case UIGenAnimatedTextureStyleSweepRight:
				case UIGenAnimatedTextureStyleSweepLeft:
				{
					if( pBundle->eMode == UITextureModeHeadshotScaled ) {
						Errorf( "Sweeping is not supported with Headshot Scaled gens." );
					}
					ui_GenBundleTextureDrawSweep(	pBundle,pState,pBox,pTex,pBasicTex,pMask,
													fScaleX,fScaleY,fRepeatX,fRepeatY,aiColors );
					break;
				}
				case UIGenAnimatedTextureStyleFillUp:
				case UIGenAnimatedTextureStyleFillDown:
				case UIGenAnimatedTextureStyleFillLeft:
				case UIGenAnimatedTextureStyleFillRight:
				{
					ui_GenBundleTextureDrawFill(	pBundle,pState,pBox,pTex,pBasicTex,pMask,
													fScaleX,fScaleY,fRepeatX,fRepeatY,aiColors );
					break;
				}
				case UIGenAnimatedTextureStyleScrollVertical:
				case UIGenAnimatedTextureStyleScrollHorizontal:
				{
					ui_GenBundleTextureDrawScroll(	pBundle,pState,pBox,pTex,pBasicTex,pMask,
													fScaleX,fScaleY,fRepeatX,fRepeatY,aiColors );
					break;
				}
			}
		}
	}

	if (pOut != pBox && pOut)
	{
		if (pBox)
			*pOut = *pBox;
		else
			*pOut = box;
	}
}

UIGenAction *ui_GenFindMessage(UIGen *pGen, const char *pchMessage)
{
	UIGenInternal *pResult = pGen->pResult;
	S32 i;
	if (!pchMessage)
		return NULL;
	if (pResult)
	{
		for (i = 0; i < eaSize(&pResult->eaMessages); i++)
		{
			if (pResult->eaMessages[i]->pchName == pchMessage)
			{
				GEN_PRINTF("Message Found In Result: Dest=%s, Message=%s.", pGen->pchName, pchMessage);
				return &pResult->eaMessages[i]->Action;
			}
		}
	}
	for (i = 0; i < eaSize(&pGen->eaMessages); i++)
	{
		if (pGen->eaMessages[i]->pchName == pchMessage)
		{
			GEN_PRINTF("Message Found In Gen: Dest=%s, Message=%s.", pGen->pchName, pchMessage);
			return &pGen->eaMessages[i]->Action;
		}
	}

	for (i = eaSize(&pGen->eaBorrowed) - 1; i >= 0; i--)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
		S32 j;
		if (!pBorrowed)
			continue;
		for (j = 0; j < eaSize(&pBorrowed->eaMessages); j++)
		{
			if (pBorrowed->eaMessages[j]->pchName == pchMessage)
			{
				GEN_PRINTF("Message Found In Borrowed: Borrowed=%s, Dest=%s, Message=%s.", pBorrowed->pchName, pGen->pchName, pchMessage);
				return &pBorrowed->eaMessages[j]->Action;
			}
		}
	}

	return NULL;
}

bool ui_GenSendMessage(UIGen *pGen, const char *pchMessage)
{
	static S32 s_iMessageStackDepth;
	UIGenAction *pAction = ui_GenFindMessage(pGen, allocFindString(pchMessage));
	if (s_iMessageStackDepth >= 20)
	{
		ErrorFilenamef(pGen->pchFilename, "Gen message stack has grown to %d, %s is receiving message %s. Stopping before I smash the stack; you're probably stuck in infinite recursion.", s_iMessageStackDepth, pGen->pchName, pchMessage);
		pAction = NULL;
	}
	if (pAction)
	{
		s_iMessageStackDepth++;
		ui_GenTimeAction(pGen, pAction, "Message %s", pchMessage);
		s_iMessageStackDepth--;
	}
	return !!pAction;
}

UIGen *ui_GenCreate(const char *pchName, UIGenType eType)
{
	UIGen *pGen = StructCreate(parse_UIGen);
	UIGenInternal Int = {eType};
	ParseTable *pBaseTable = PolyStructDetermineParseTable(polyTable_UIGenInternal, &Int);
	if (devassertmsg(pBaseTable, "Can't make a new Gen, the type did not resolve!"))
	{
		pGen->pchName = allocAddString(pchName);
		pGen->pchFilename = allocAddFilename("From ui_GenCreate");
		pGen->eType = eType;
		pGen->pBase = StructCreateVoid(pBaseTable);
	}
	return pGen;
}

bool ui_GenInDictionary(UIGen *pGen)
{
	return (pGen && pGen->pchName && ui_GenFind(pGen->pchName, kUIGenTypeNone) == pGen);
}

bool ui_GenMatchesByNameCB(UIGen *pGen, const char *pchName)
{
	return stricmp(pGen->pchName, pchName)==0;
}

bool ui_GenMatchesByPointerCB(UIGen *pGen, const UIGen *pTest)
{
	return pGen==pTest;
}

UIGen *ui_GenFindChild(UIGen *pParent, UIGenIsMatch pfnGenIsMatch, const void *pvTester)
{
	S32 j;
	UIGenInternal *pResult = pParent->pResult;

	if (pResult)
	{
		for (j = eaSize(&pResult->eaChildren) - 1; j >= 0; j--)
		{
			UIGenChild *pChild = pResult->eaChildren[j];
			UIGen *pThis = GET_REF(pChild->hChild);
			if(pThis && pfnGenIsMatch(pThis, pvTester))
			{
				return pThis;
			}
		}

		for (j = eaSize(&pResult->eaInlineChildren) - 1; j >= 0; j--)
		{
			if(pfnGenIsMatch(pResult->eaInlineChildren[j], pvTester))
			{
				return pResult->eaInlineChildren[j];
			}
		}
	}

	return NULL;
}

bool ui_GenRemoveChild(UIGen *pParent, UIGen *pToRemove, bool bFromResult)
{
	UIGenInternal *apToCheck[] = {pParent->pBase, pParent->pCodeOverrideEarly, bFromResult ? pParent->pResult : NULL};
	S32 i;
	S32 j;
	bool bFound = false;
	for (i = 0; i < ARRAY_SIZE(apToCheck); i++)
	{
		UIGenInternal *pOverride = apToCheck[i];
		if (pOverride)
		{
			for (j = eaSize(&pOverride->eaChildren) - 1; j >= 0; j--)
			{
				UIGenChild *pChild = pOverride->eaChildren[j];
				if (GET_REF(pChild->hChild) == pToRemove)
				{
					eaRemove(&pOverride->eaChildren, j);
					StructDestroy(parse_UIGenChild, pChild);
					bFound = true;
				}
			}

			for (j = eaSize(&pOverride->eaInlineChildren) - 1; j >= 0; j--)
			{
				if (pOverride->eaInlineChildren[j] == pToRemove)
				{
					eaRemove(&pOverride->eaInlineChildren, j);
					bFound = true;
				}
			}
		}
	}
	if (bFound && bFromResult && pToRemove)
		pToRemove->pParent = NULL;

	if (bFound)
		ui_GenMarkDirty(pParent);
	return bFound;
}

UIGen *ui_GenClone(UIGen *pGen)
{
	UIGenPerTypeState *pState = pGen->pState;
	UIGenInternal *pResult = pGen->pResult;
	UIGenCodeInterface *pCode = pGen->pCode;
	UIGen **eaBorrowedInlineChildren = NULL;
	UIGen *pNew;
	// Don't copy the state, result, or code of the gen
	pGen->pState = NULL;
	pGen->pResult = NULL;
	pGen->pCode = NULL;
	eaCopy(&eaBorrowedInlineChildren, &pGen->eaBorrowedInlineChildren);
	eaClearFast(&pGen->eaBorrowedInlineChildren);
	pNew = StructClone(parse_UIGen, pGen);
	ui_GenReset(pNew);
	pGen->pState = pState;
	pGen->pResult = pResult;
	pGen->pCode = pCode;
	eaCopy(&pGen->eaBorrowedInlineChildren, &eaBorrowedInlineChildren);
	eaDestroy(&eaBorrowedInlineChildren);
	return pNew;
}

bool ui_GenSetVarEx(UIGen *pGen, const char *pchVar, F64 dValue, const char *pchString, bool create)
{
	UIGenVarTypeGlob *pGlob = NULL;

	//verify params
	if ( !pGen || !pchVar || !pchVar[0] )
		return false;

	pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);

	//create the var if required and desired
	if ( !pGlob && create )
	{
		pGlob = StructCreate(parse_UIGenVarTypeGlob);
		if (pGlob)
		{
			pGlob->pchName = StructAllocString(pchVar);
			eaPush(&pGen->eaVars, pGlob);
		}
	}

	//set the var values/string
	if (pGlob)
	{
		pGlob->fFloat = dValue;
		pGlob->iInt = dValue;

		if (pchString)
		{
			estrCopy2(&pGlob->pchString, pchString);
		}
		return true;
	}

	return false;
}

UIGen *ui_GenGetSiblingEx(UIGen *pGen, const void *pchName, bool bReverse, bool bMustBeInOrder)
{
	UIGen *pParent = pGen->pParent;
	S32 i, iSize;
	S32 iDiff = bReverse ? -1 : 1;
	S32 iRelative = 0;

	if (!pParent)
		return NULL;
	else if (!UI_GEN_READY(pParent))
	{
		ErrorFilenamef(pGen->pchFilename, "%s: Has an invalid parent %s", pGen->pchName, pParent->pchName);
		return NULL;
	}

	if (g_ui_pchParent == pchName)
		return pParent;

	if(s_pchPrev==pchName || s_pch_Previous==pchName)
	{
		iRelative = -iDiff;
	}
	else if(s_pchNext==pchName)
	{
		if(bMustBeInOrder)
			return NULL;

		iRelative = iDiff;
	}

	iSize = eaSize(&pParent->pResult->eaChildren);
	for (i = bReverse ? (iSize - 1) : 0; bReverse ? (i >= 0) : (i < iSize); i += iDiff)
	{
		UIGen *pChild = GET_REF(pParent->pResult->eaChildren[i]->hChild);
		if (pChild == pGen)
		{
			if(iRelative)
			{
				int iRel = i+iRelative;
				if(iRel>=0 && iRel<iSize)
					return GET_REF(pParent->pResult->eaChildren[iRel]->hChild);
				else
					return NULL;
			}
			else if (bMustBeInOrder)
				return NULL;
		}
		if (pChild && pChild->pchName == pchName)
			return pChild;
	}

	iSize = eaSize(&pParent->pResult->eaInlineChildren);
	for (i = bReverse ? (iSize - 1) : 0; bReverse ? (i >= 0) : (i < iSize); i += iDiff)
	{
		UIGen *pChild = pParent->pResult->eaInlineChildren[i];
		if (pChild == pGen)
		{
			if(iRelative)
			{
				int iRel = i+iRelative;
				if(iRel>=0 && iRel<iSize)
					return pParent->pResult->eaInlineChildren[iRel];
				else
					return NULL;
			}
			else if (bMustBeInOrder)
				return NULL;
		}
		if (pChild && pChild->pchName == pchName)
			return pChild;
	}

	return NULL;
}

UIGen *ui_GenInternalGetChild(SA_PARAM_NN_VALID UIGenInternal *pInternal, const void *pchName)
{
	S32 i;
	for (i = 0; i < eaSize(&pInternal->eaChildren); i++)
	{
		UIGen *pChild = GET_REF(pInternal->eaChildren[i]->hChild);
		if (pChild && pChild->pchName == pchName)
			return pChild;
	}
	for (i = 0; i < eaSize(&pInternal->eaInlineChildren); i++)
	{
		if (pInternal->eaInlineChildren[i] && pInternal->eaInlineChildren[i]->pchName == pchName)
			return pInternal->eaInlineChildren[i];
	}
	return NULL;
}

UIGen *ui_GenGetChild(UIGen *pGen, const void *pchName)
{
	if (!pGen->pResult)
		return NULL;
	return ui_GenInternalGetChild(pGen->pResult, pchName);
}

UIGen *ui_GenGetBorrowedBaseChild(UIGen *pGen, const void *pchName)
{
	S32 i, j;
	UIGen *pChild, *pBorrowed, *pInlineChild;

	if (pChild = eaIndexedGetUsingString(&pGen->eaBorrowedInlineChildren, pchName))
		return pChild;

	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
	{
		pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
		if (pBorrowed->pLast)
		{
			for (j = 0; j < eaSize(&pBorrowed->pLast->eaChildren); j++)
			{
				pChild = GET_REF(pBorrowed->pLast->eaChildren[j]->hChild);
				if (pChild && pChild->pchName == pchName)
					return pChild;
			}
			for (j = 0; j < eaSize(&pBorrowed->pLast->eaInlineChildren); j++)
			{
				if (pBorrowed->pLast->eaInlineChildren[j] && pBorrowed->pLast->eaInlineChildren[j]->pchName == pchName)
				{
					pInlineChild = StructClone(parse_UIGen, pBorrowed->pLast->eaInlineChildren[j]);
					eaIndexedAdd(&pGen->eaBorrowedInlineChildren, pInlineChild);
					return pInlineChild;
				}
			}
		}
		if (pBorrowed->pBase)
		{
			for (j = 0; j < eaSize(&pBorrowed->pBase->eaChildren); j++)
			{
				pChild = GET_REF(pBorrowed->pBase->eaChildren[j]->hChild);
				if (pChild && pChild->pchName == pchName)
					return pChild;
			}
			for (j = 0; j < eaSize(&pBorrowed->pBase->eaInlineChildren); j++)
			{
				if (pBorrowed->pBase->eaInlineChildren[j] && pBorrowed->pBase->eaInlineChildren[j]->pchName == pchName)
				{
					pInlineChild = StructClone(parse_UIGen, pBorrowed->pBase->eaInlineChildren[j]);
					eaIndexedAdd(&pGen->eaBorrowedInlineChildren, pInlineChild);
					return pInlineChild;
				}
			}
		}
	}

	return NULL;
}

UIGen *ui_GenGetBaseChild(UIGen *pGen, const void *pchName)
{
	UIGen *pChild;

	if (pGen->pLast && (pChild = ui_GenInternalGetChild(pGen->pLast, pchName)))
		return pChild;
	if (pGen->pBase && (pChild = ui_GenInternalGetChild(pGen->pBase, pchName)))
		return pChild;

	return ui_GenGetBorrowedBaseChild(pGen, pchName);
}

UIGenInternal *ui_GenGetBase(UIGen *pGen, bool bCreate)
{
	if (!pGen->pBase)
	{
		if (bCreate)
		{
			ParseTable *pTable = ui_GenGetType(pGen);
			pGen->pBase = StructCreateVoid(pTable);
		}
		else
		{
			S32 i;
			for (i = eaSize(&pGen->eaBorrowed) - 1; i >= 0; i--)
				if (GET_REF(pGen->eaBorrowed[i]->hGen) && GET_REF(pGen->eaBorrowed[i]->hGen)->pBase)
					return GET_REF(pGen->eaBorrowed[i]->hGen)->pBase;
		}
	}
	return pGen->pBase;
}

void ui_GenSetBaseField(UIGen *pGen, const char *pchField, bool bSet, S32 *piColumn)
{
	S32 iColumn = -1;
	ParseTable *pTable = NULL;

	if (!pGen->pBase)
		return;

	if (!piColumn)
		piColumn = &iColumn;

	if (*piColumn < 0)
		*piColumn = GenGetFieldAndColumn(pGen, pchField, &pTable);
	else
		pTable = ui_GenGetType(pGen);

	if (*piColumn >= 0)
		TokenSetSpecified(pTable, *piColumn, pGen->pBase, -1, bSet);
}

static void ui_GenInternalDemandTextures(UIGenInternal *pInt)
{
	ParseTable *pTable = pInt ? ui_GenInternalGetType(pInt) : NULL;
	if (pTable)
	{
		UITextureAssembly *pAssembly = ui_GenTextureAssemblyGetAssembly(NULL, pInt->pAssembly);
		S32 i;
		FORALL_PARSETABLE(pTable, i)
		{
			if (pTable[i].type & TOK_STRING_X && pTable[i].subtable && !stricmp(pTable[i].subtable, "Texture"))
			{
				const char *pchValue = TokenStoreGetString(pTable, i, pInt, 0, NULL);
				if (pchValue)
				{
					ANALYSIS_ASSUME(pchValue != NULL);
					if (!strchr(pchValue, '{'))
					{
						atlasDemandLoadTextureByName(pchValue);
					}
				}
			}
		}

		if (pAssembly)
		{
			for (i = 0; i < eaSize(&pAssembly->eaTextures); i++)
			{
				if (pAssembly->eaTextures[i]->bIsAtlas && pAssembly->eaTextures[i]->pAtlasTexture)
					atlasDemandLoadTexture(pAssembly->eaTextures[i]->pAtlasTexture);
				else if (!pAssembly->eaTextures[i]->bIsAtlas && pAssembly->eaTextures[i]->pTexture)
					texDemandLoad(pAssembly->eaTextures[i]->pTexture, 0.f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY, invisible_tex);
			}
		}
	}
}

void ui_GenDemandTextures(UIGen *pGen)
{
	S32 i, j;
	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
	{
		UIGen *pBorrow = GET_REF(pGen->eaBorrowed[i]->hGen);
		if (pBorrow)
		{
			ui_GenInternalDemandTextures(pBorrow->pBase);
			for (j = 0; j < eaSize(&pBorrow->eaStates); j++)
				ui_GenInternalDemandTextures(pBorrow->eaStates[j]->pOverride);
			for (j = 0; j < eaSize(&pBorrow->eaComplexStates); j++)
				ui_GenInternalDemandTextures(pBorrow->eaComplexStates[j]->pOverride);
		}
	}
	ui_GenInternalDemandTextures(pGen->pBase);
	for (j = 0; j < eaSize(&pGen->eaStates); j++)
		ui_GenInternalDemandTextures(pGen->eaStates[j]->pOverride);
	for (j = 0; j < eaSize(&pGen->eaComplexStates); j++)
		ui_GenInternalDemandTextures(pGen->eaComplexStates[j]->pOverride);
}

const char *ui_GenGetPersistedString(const char *pchKey, const char *pchDefault)
{
	const char *pchValue = g_GenState.cbValueGet ? g_GenState.cbValueGet(pchKey) : NULL;
	return pchValue ? pchValue : pchDefault;
}

S32 ui_GenGetPersistedInt(const char *pchKey, S32 iDefault)
{
	const char *pchValue = g_GenState.cbValueGet ? g_GenState.cbValueGet(pchKey) : NULL;
	return pchValue ? atoi(pchValue) : iDefault;
}

F32 ui_GenGetPersistedFloat(const char *pchKey, F32 fDefault)
{
	const char *pchValue = g_GenState.cbValueGet ? g_GenState.cbValueGet(pchKey) : NULL;
	return pchValue ? atof(pchValue) : fDefault;

}

bool ui_GenSetPersistedString(const char *pchKey, const char *pchValue)
{
	return g_GenState.cbValueSet ? g_GenState.cbValueSet(pchKey, pchValue) : false;
}

bool ui_GenSetPersistedInt(const char *pchKey, S32 iValue)
{
	char ach[64];
	sprintf(ach, "%d", iValue);
	return g_GenState.cbValueSet ? g_GenState.cbValueSet(pchKey, ach) : false;
}

bool ui_GenSetPersistedFloat(const char *pchKey, F32 fValue)
{
	char ach[64];
	sprintf(ach, "%g", fValue);
	return g_GenState.cbValueSet ? g_GenState.cbValueSet(pchKey, ach) : false;
}

void ui_GenSetPersistedValueCallbacks(UIGenGetValue cbValueGet, UIGenSetValue cbValueSet)
{
	g_GenState.cbValueGet = cbValueGet;
	g_GenState.cbValueSet = cbValueSet;
}

F32 ui_GenGetBaseScale(void)
{
	return g_GenState.fScale;
}

void ui_GenSetBaseScale(F32 fScale)
{
	g_GenState.fScale = CLAMP(fScale, 0.2, 5.0);
}

void ui_GenSetSMFNavigateCallback(int (*callback)(const char *))
{
	g_SMFNavigateCallback = callback;
}

void ui_GenSetSMFHoverCallback(int (*callback)(const char *, UIGen*))
{
	g_SMFHoverCallback = callback;
}

UITextureAssembly *ui_GenTextureAssemblyGetAssembly(UIGen *pGen, UIGenTextureAssembly *pAssembly)
{
	UITextureAssembly *pTexAs;

	if (!pAssembly)
		return NULL;

	pTexAs = GET_REF(pAssembly->hAssembly);

	// Attempt to minimize evaluations of the assembly name.
	// Also, only evaluate when safe.
	if (pGen && pAssembly->pchAssembly
		&& pGen->uiFrameLastUpdate != pAssembly->uiFrameLastUpdate
		&& pGen->uiFrameLastUpdate == g_ui_State.uiFrameCount)
	{
		static char *s_pchTexasFormatted;

		if (strchr(pAssembly->pchAssembly, STRFMT_TOKEN_START))
		{
			if (s_pchTexasFormatted)
			{
				estrClear(&s_pchTexasFormatted);
			}
			else
			{
				estrCreate(&s_pchTexasFormatted);
			}

			exprFormat(&s_pchTexasFormatted, pAssembly->pchAssembly, ui_GenGetContext(pGen), pGen->pchFilename);

			if ((!pTexAs) != (!*s_pchTexasFormatted) || pTexAs && stricmp(pTexAs->pchName, s_pchTexasFormatted))
			{
				if (*s_pchTexasFormatted)
					SET_HANDLE_FROM_STRING("UITextureAssembly", s_pchTexasFormatted, pAssembly->hAssembly);
				else
					REMOVE_HANDLE(pAssembly->hAssembly);
				pTexAs = GET_REF(pAssembly->hAssembly);
			}
		}
		else
		{
			SET_HANDLE_FROM_STRING("UITextureAssembly", pAssembly->pchAssembly, pAssembly->hAssembly);
			pTexAs = GET_REF(pAssembly->hAssembly);
			pAssembly->pchAssembly = NULL;
		}
	}

	return pTexAs;
}

UIGenInternal *ui_GenGetCodeOverrideEarly(UIGen *pGen, bool bCreate)
{
	if (pGen && !pGen->pCodeOverrideEarly && bCreate)
	{
		ParseTable *pTable = ui_GenGetType(pGen);
		pGen->pCodeOverrideEarly = StructCreateVoid(pTable);
	}
	return pGen ? pGen->pCodeOverrideEarly : NULL;
}

void ui_GenSetCodeOverrideEarlyField(UIGen *pGen, const char *pchField, bool bSet, S32 *piColumn)
{
	S32 iColumn = -1;
	ParseTable *pTable = NULL;

	if (!pGen->pCodeOverrideEarly)
		return;

	if (!piColumn)
		piColumn = &iColumn;

	if (*piColumn < 0)
		*piColumn = GenGetFieldAndColumn(pGen, pchField, &pTable);
	else
		pTable = ui_GenGetType(pGen);

	if (*piColumn >= 0)
		TokenSetSpecified(pTable, *piColumn, pGen->pCodeOverrideEarly, -1, bSet);
}

UIGen*** ui_GenGetInstances(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList))
	{
		UIGenListState *pState = UI_GEN_STATE(pGen, List);
		return &pState->eaRows;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		return &pState->eaInstanceGens;
	}
	else if (UI_GEN_IS_TYPE(pGen, kUIGenTypeTabGroup))
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		return &pState->eaTabs;
	}
	return NULL;
}

bool ui_GenAddTextureSkinOverride(const char *pchTextureSuffix)
{
	pchTextureSuffix = allocAddString(pchTextureSuffix);
	if (pchTextureSuffix)
	{
		if (eaFind(&g_GenState.eapchTextureSkins, pchTextureSuffix) < 0)
		{
			eaPush(&g_GenState.eapchTextureSkins, pchTextureSuffix);
			return true;
		}
	}
	return false;
}

bool ui_GenRemoveTextureSkinOverride(const char *pchTextureSuffix)
{
	pchTextureSuffix = allocAddString(pchTextureSuffix);
	if (pchTextureSuffix)
	{
		if (pchTextureSuffix[0] == '*' && pchTextureSuffix[1] == '\0')
		{
			eaDestroy(&g_GenState.eapchTextureSkins);
			return true;
		}
		else if (eaFindAndRemove(&g_GenState.eapchTextureSkins, pchTextureSuffix) >= 0)
		{
			return true;
		}
	}
	return false;
}

const char *ui_GenGetTextureSkinOverride(const char *pchTextureName)
{
	S32 i, l, n = eaiSize(&g_GenState.eaiSkinStates);

	if (pchTextureName && *pchTextureName && n > 0)
	{
		l = strlen(pchTextureName);
		if (!stricmp(pchTextureName + l - 5, ".wtex"))
			l -= 5;

		for (i = 0; i < n; i++)
		{
			if (ui_GenInGlobalState(g_GenState.eaiSkinStates[i]))
			{
				char achName[MAX_PATH];

				if (g_texture_name_fixup)
				{
					char achNameOverride[MAX_PATH];
					sprintf(achNameOverride, "%.*s_%s", l, pchTextureName, g_GenState.eapchSkinOverrides[i]);
					g_texture_name_fixup(achNameOverride, SAFESTR(achName));
				}
				else
				{
					sprintf(achName, "%.*s_%s", l, pchTextureName, g_GenState.eapchSkinOverrides[i]);
				}

				if (texFind(achName, false))
				{
					return allocAddString(achName);
				}
			}
		}

		n = eaSize(&g_GenState.eapchTextureSkins);
		for (i = 0; i < n; i++)
		{
			char achTextureName[MAX_PATH];

			if (g_texture_name_fixup)
			{
				char achTextureNameOverride[MAX_PATH];
				sprintf(achTextureNameOverride, "%.*s_%s", l, pchTextureName, g_GenState.eapchTextureSkins[i]);
				g_texture_name_fixup(achTextureNameOverride, SAFESTR(achTextureName));
			}
			else
			{
				sprintf(achTextureName, "%.*s_%s", l, pchTextureName, g_GenState.eapchTextureSkins[i]);
			}

			if (texFind(achTextureName, false))
			{
				return allocAddString(achTextureName);
			}
		}
	}

	if (g_texture_name_fixup)
	{
		char achFixupName[MAX_PATH];
		g_texture_name_fixup(pchTextureName, SAFESTR(achFixupName));
		return allocAddString(achFixupName);
	}

	return pchTextureName;
}

#include "UIGen_h_ast.c"
#include "UIGenPrivate_h_ast.c"
