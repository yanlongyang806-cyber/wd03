#include "cmdparse.h"
#include "earray.h"
#include "Expression.h"
#include "ResourceManager.h"
#include "timing.h"
#include "StringCache.h"
#include "Message.h"
#include "FolderCache.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputKeyBind.h"

#include "RdrDevice.h"
#include "GfxConsole.h"
#include "GfxSpriteText.h"

#include "UIGen.h"
#include "UIGenJail.h"
#include "UIGenPrivate.h"
#include "UITextureAssembly.h"

#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenJail_h_ast.h"
#include "UIGenJail_h_ast.c"
#include "UIGenBox_h_ast.h"

/////////////////////////////////////////////////////////////////////////////////////

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void GenJailRunAction(UIGenJail *pJail, UIGenAction *pAction);

static DictionaryHandle s_hJailDict;

UIGenJailKeeper *g_ui_pDefaultKeeper;
static KeyBindProfile s_JailProfile = { "Gen Cell Unlocking", __FILE__, KeyboardLocale_EnglishUS, NULL, true, true, NULL, NULL, InputBindPriorityConsole };

UIGenPrisonDefLoading g_ui_GenPrisonDefs;

static UIGenJail **s_eaQueueFree = NULL;

// Unlock jail cells and allow them to move around and resize.
AUTO_CMD_INT(g_GenState.bJailUnlocked, GenJailUnlock) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// Draw jail cell frames.
AUTO_CMD_INT(g_GenState.bJailFrames, GenJailFrames) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// Hide (but don't reset?) gens inside cells.
AUTO_CMD_INT(g_GenState.bJailNoGens, GenJailNoGens) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// Compress cells as windows close (e.g. close window 1, window 2 goes to slot 1).
AUTO_CMD_INT(g_GenState.bJailCompress, GenJailCompress) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// Toggling GenJailCells toggles all three of the above.
void ui_GenJailCells(void)
{
	g_GenState.bJailUnlocked = g_GenState.bJailCells;
	g_GenState.bJailFrames = g_GenState.bJailCells;
	g_GenState.bJailNoGens = g_GenState.bJailCells;
	if (g_GenState.bJailCells)
		keybind_PushProfile(&s_JailProfile);
	else
		keybind_PopProfile(&s_JailProfile);
}

// Show and move cells, rather than normal gen contents.
AUTO_CMD_INT(g_GenState.bJailCells, GenJailCells) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(ui_GenJailCells) ACMD_HIDE;

//////////////////////////////////////////////////////////////////////////
// Loading and fixup functions

AUTO_FIXUPFUNC;
TextParserResult ui_GenJailCellParserFixup(UIGenJailCell *pCell, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_CONSTRUCTOR)
	{
		pCell->Parent.eType = kUIGenTypeBox;
		pCell->Parent.pResult = StructCreateVoid(parse_UIGenBox);
		pCell->Parent.chLayer = kUIGenLayerWindow;
		pCell->Parent.bIsRoot = true;
	}
	else if (eType == FIXUPTYPE_DESTRUCTOR)
	{
		UIGen *pGen = GET_REF(pCell->hGen);
		if (pGen)
		{
			pGen->pParent = NULL;
			ui_GenQueueReset(pGen);
		}
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenJailCellBlockParserFixup(UIGenJailCellBlock *pCellBlock, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_CONSTRUCTOR)
	{
		SET_HANDLE_FROM_STRING("UITextureAssembly", "JailCell", pCellBlock->hAssembly);
		SET_HANDLE_FROM_STRING("UITextureAssembly", "JailCellHover", pCellBlock->hHoverAssembly);
		SET_HANDLE_FROM_STRING("UITextureAssembly", "JailCellGhost", pCellBlock->hGhostAssembly);
		SET_HANDLE_FROM_STRING("UITextureAssembly", "JailCellGhostHover", pCellBlock->hHoverGhostAssembly);
		SET_HANDLE_FROM_STRING("UITextureAssembly", "Button_Active", pCellBlock->hButtonAssembly);
		// "Shared core UI" my ass.
		if (!GET_REF(pCellBlock->hButtonAssembly))
			SET_HANDLE_FROM_STRING("UITextureAssembly", "Button", pCellBlock->hButtonAssembly);
		SET_HANDLE_FROM_STRING("UIStyleFont", "Game_HUD", pCellBlock->hFont);
		SET_HANDLE_FROM_STRING("UIStyleFont", "Game_Highlight", pCellBlock->hHoverFont);
	}
	return PARSERESULT_SUCCESS;
}

static bool GenJailDefActionResValidatePostTextRead(UIGenJailDef *pJail, UIGenAction *pAction)
{
	if (!pAction)
		return true;

	UI_GEN_FAIL_IF(pJail,
		pAction->pExpression
		|| pAction->eaiEnterState
		|| pAction->eaiExitState
		|| pAction->eaiToggleState
		|| pAction->eaMutate
		|| pAction->eaSetter,
		"Jail actions only support messages, commands, and sounds.");

	return true;
}


static bool GenJailDefResValidatePostTextRead(UIGenJailDef *pJail)
{
	S32 i, j;
	ExprContext *pContext = ui_GenGetContext(NULL);
	if (!GET_REF(pJail->hFont))
		SET_HANDLE_FROM_STRING("UIStyleFont", "Game_HUD", pJail->hFont);

	if (!GenJailDefActionResValidatePostTextRead(pJail, pJail->pBeforeCreate))
		return false;

	if (!GenJailDefActionResValidatePostTextRead(pJail, pJail->pBeforeHide))
		return false;

	for (i = 0; i < eaSize(&pJail->eaCellBlock); i++)
	{
		UIGenJailCellBlock *pBlock = pJail->eaCellBlock[i];
		if (pBlock->iMaxCells < pBlock->iDefaultCells)
			pBlock->iMaxCells = pBlock->iDefaultCells;
		UI_GEN_FAIL_IF(pJail,
			eaSize(&pBlock->eaPosition) < pBlock->iDefaultCells,
			"%s: Has more default cells than cell positions.", pBlock->pchName);
		if (pBlock->AspectRatio.iWidth)
		{
			for (j = 0; j < eaSize(&pBlock->eaPosition); j++)
			{
				UIPosition *pPosition = pBlock->eaPosition[j];
				UI_GEN_FAIL_IF(pJail,
					pPosition->Width.eUnit == UIUnitPercentage ||
					pPosition->Height.eUnit == UIUnitPercentage ||
					pPosition->MinimumWidth.eUnit == UIUnitPercentage ||
					pPosition->MinimumHeight.eUnit == UIUnitPercentage ||
					pPosition->MaximumWidth.eUnit == UIUnitPercentage ||
					pPosition->MaximumHeight.eUnit == UIUnitPercentage,
					"%s: Using fixed aspect ratio with Percentage size.", pBlock->pchName);
			}
		}
	}

	for (i = 0; i < eaSize(&pJail->eaStateDef); i++)
	{
		UI_GEN_FAIL_IF(pJail, pJail->eaStateDef[i]->pOverride,
			"Jails may not have overrides, only OnEnter/OnExit.");
		if (!GenJailDefActionResValidatePostTextRead(pJail, pJail->eaStateDef[i]->pOnEnter))
			return false;
		if (!GenJailDefActionResValidatePostTextRead(pJail, pJail->eaStateDef[i]->pOnExit))
			return false;
	}

	for (i = 0; i < eaSize(&pJail->eaComplexStateDef); i++)
	{
		UI_GEN_FAIL_IF(pJail, pJail->eaComplexStateDef[i]->pOverride,
			"Jails may not have overrides, only OnEnter/OnExit.");
		UI_GEN_FAIL_IF(pJail,
			!exprGenerate(pJail->eaComplexStateDef[i]->pCondition, pContext),
			"Invalid expression.");
		if (!GenJailDefActionResValidatePostTextRead(pJail, pJail->eaComplexStateDef[i]->pOnEnter))
			return false;
		if (!GenJailDefActionResValidatePostTextRead(pJail, pJail->eaComplexStateDef[i]->pOnExit))
			return false;
	}

	UI_GEN_FAIL_IF(pJail, eaSize(&pJail->eaComplexStateDef) > sizeof(((UIGenJail *)NULL)->bfComplexStates) * 8,
		"Too many complex state defs.");

	return true;
}

static int GenJailDefResValidate(enumResourceEventType eType, const char *pDictName, const char *pchJail, UIGenJailDef *pJailDef, U32 iUserID)
{
	if (eType == RESVALIDATE_POST_TEXT_READING)
	{
		GenJailDefResValidatePostTextRead(pJailDef);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static void GenJailDefResEvent(enumResourceEventType eType, const char *pDictName, const char *pchJail, UIGenJailDef *pJailDef, UIGenJailKeeper *pKeeper)
{
	if (eType == RESEVENT_RESOURCE_MODIFIED)
	{
		UIGenJail *pJail = ui_GenJailHide(NULL, pJailDef);
		if (pJail)
		{
			ui_GenJailDestroy(pJail);
			ui_GenJailShow(NULL, pJailDef);
		}
	}
}

AUTO_RUN;
void ui_GenJailRegister(void)
{
	s_hJailDict = RefSystem_RegisterSelfDefiningDictionary("UIGenJailDef", false, parse_UIGenJailDef, true, true, NULL);
	g_ui_pDefaultKeeper = ui_GenJailKeeperCreate();
	ui_WidgetSetName(UI_WIDGET(g_ui_pDefaultKeeper), "Default_Gen_JailKeeper");
	keybind_BindKeyInProfile(&s_JailProfile, "Escape", "-GenJailCells");
	keybind_BindKeyInProfile(&s_JailProfile, "Shift", "");
	keybind_BindKeyInProfile(&s_JailProfile, "Shift+Escape", "GenJailReset");
	keybind_BindKeyInProfile(&s_JailProfile, "Tab", "GenJailSink");

	if( gConf.bHUDRearrangeModeEatsKeys )
	{
		s_JailProfile.bTrickleKeys = false;
	}
}

void GenReloadPrisonFiles(const char *path, int when)
{
	StructReset(parse_UIGenPrisonDefLoading, &g_ui_GenPrisonDefs);
	eaIndexedEnable(&g_ui_GenPrisonDefs.eaPrisons, parse_UIGenPrisonDef);

	loadstart_printf("Loading UIGenPrisonDef...");
	ParserLoadFiles("ui/gens/jails", ".prison", "UIGenPrison.bin", PARSER_OPTIONALFLAG, parse_UIGenPrisonDefLoading, &g_ui_GenPrisonDefs);
	loadend_printf("done. (%d UIGenPrisonDef)", eaSize(&g_ui_GenPrisonDefs.eaPrisons));
}

void ui_GenJailLoad(void)
{
	resDictManageValidation(s_hJailDict, GenJailDefResValidate);
	resDictRegisterEventCallback(s_hJailDict, GenJailDefResEvent, g_ui_pDefaultKeeper);
	resLoadResourcesFromDisk(s_hJailDict, "ui/gens/jails", ".jail", NULL, 0);

	// Load prison files
	GenReloadPrisonFiles(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/gens/jails/*.prison", GenReloadPrisonFiles);
}

static void GenJailCellPositionGet(UIGenJailCell *pCell)
{
	UIGenJailDef *pDef = GET_REF(pCell->hJail);
	if (pDef && g_GenState.cbPositionGet)
	{
		const char *pchContent = NULL;
		g_GenState.cbPositionGet(pCell->Parent.pchName, &pCell->Parent.pResult->pos, pDef->iVersion, 0, &pCell->Parent.chPriority, &pchContent);
		if (pCell->pBlock->bRememberContents)
			SET_HANDLE_FROM_STRING("UIGen", pchContent, pCell->hGen);
	}
}

static void GenJailCellPositionSave(UIGenJailCell *pCell)
{
  	UIGenJailDef *pDef = GET_REF(pCell->hJail);
	UIGen *pGen = GET_REF(pCell->hGen);
	if (pDef && g_GenState.cbPositionSet)
	{
		g_GenState.cbPositionSet(pCell->Parent.pchName, &pCell->Parent.pResult->pos, pDef->iVersion, pGen ? pGen->chClone : 0, pCell->Parent.chPriority, pCell->pBlock->bRememberContents ? REF_STRING_FROM_HANDLE(pCell->hGen) : NULL);
	}
}

static void GenJailCellPositionForget(UIGenJailCell *pCell)
{
	UIGenJailDef *pDef = GET_REF(pCell->hJail);
	UIGen *pGen = GET_REF(pCell->hGen);
	if (pDef && g_GenState.cbPositionForget)
	{
		g_GenState.cbPositionForget(pCell->Parent.pchName, &pCell->Parent.pResult->pos, pDef->iVersion, pGen ? pGen->chClone : 0, pCell->Parent.chPriority, pCell->pBlock->bRememberContents ? REF_STRING_FROM_HANDLE(pCell->hGen) : NULL);
	}
}

static void GenJailCellFixName(UIGenJailCell *pCell)
{
	UIGenJailDef *pDef = GET_REF(pCell->hJail);
	char achName[1000];
	assert(pDef);
	sprintf(achName, "_Cell_%s_%d_Jail_%s", pCell->pchName, max(0, pCell->iIndex), pDef->pchPrisonName && *pDef->pchPrisonName ? pDef->pchPrisonName : pDef->pchName);
	pCell->Parent.pchName = allocAddString(achName);
}

static void GenJailCellSetToAspectRatio(UISizeSpec *pWidth, UISizeSpec *pHeight, UIDirection eFixed, F32 fAspectRatio)
{
	F32 fNewWidth, fNewHeight;

	if (!(eFixed & (UILeft|UIRight)))
	{
		// Unconstrained width
		if (pWidth->eUnit == pHeight->eUnit)
		{
			fNewWidth = pHeight->fMagnitude * fAspectRatio;
			pWidth->fMagnitude = fNewWidth;
		}
	}
	else if (!(eFixed & (UITop|UIBottom)))
	{
		// Unconstrained height
		if (pWidth->eUnit == pHeight->eUnit)
		{
			fNewHeight = pWidth->fMagnitude / fAspectRatio;
			pHeight->fMagnitude = fNewHeight;
		}
	}
	else
	{
		// Corner based or completely unconstrained, only expand
		if (pWidth->eUnit == pHeight->eUnit)
		{
			fNewWidth = pHeight->fMagnitude * fAspectRatio;
			fNewHeight = pWidth->fMagnitude / fAspectRatio;

			if (fNewWidth > pWidth->fMagnitude)
			{
				pWidth->fMagnitude = fNewWidth;
			}
			else if (fNewHeight > pHeight->fMagnitude)
			{
				pHeight->fMagnitude = fNewHeight;
			}
		}
	}
}

static void GenJailCellFixAspectRatio(UIGenJail *pJail, UIGenJailCell *pCell, UIDirection eFixed)
{
	UIGenJailDef *pDef = GET_REF(pJail->hJail);
	UIGenJailCellBlock *pBlock = pCell->pBlock;
	UIGen *pParent = &pCell->Parent;
	UIPosition *pPos = &pParent->pResult->pos;
	CBox Screen = {0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight};
	CBox Size = {0, 0, 0, 0};
	CBox Dummy = {0, 0, 0, 0};

	if (!pBlock || !pBlock->AspectRatio.iWidth)
	{
		return;
	}

	if (!pCell->fAspectRatio)
	{
		if (!pBlock->AspectRatio.iHeight)
		{
			// Auto-compute from base size
			ui_GenPositionToCBox(pBlock->eaPosition[pCell->iIndex % eaSize(&pBlock->eaPosition)], &Screen, 1, 1, &Size, &Dummy, &Dummy, NULL);
			pCell->fAspectRatio = CBoxWidth(&Size) / CBoxHeight(&Size);
		}
		else
		{
			pCell->fAspectRatio = (float)pBlock->AspectRatio.iWidth / (float)pBlock->AspectRatio.iHeight;
		}
	}

	GenJailCellSetToAspectRatio(&pPos->Width, &pPos->Height, eFixed, pCell->fAspectRatio);
	GenJailCellSetToAspectRatio(&pPos->MinimumWidth, &pPos->MinimumHeight, eFixed, pCell->fAspectRatio);
	GenJailCellSetToAspectRatio(&pPos->MaximumWidth, &pPos->MaximumHeight, eFixed, pCell->fAspectRatio);
}

//////////////////////////////////////////////////////////////////////////
// UIGenJail instance management

static void GenJailRunAction(UIGenJail *pJail, UIGenAction *pAction)
{
	UIGenJailDef *pDef = GET_REF(pJail->hJail);
	S32 i;
	if (!(pAction && pDef))
		return;

	for (i = 0; i < eaSize(&pAction->eaMessage); i++)
		ui_GenSendMessage(GET_REF(pAction->eaMessage[i]->hGen), pAction->eaMessage[i]->pchMessageName);

	if (g_ui_State.cbPlayAudio)
		for (i = 0; i < eaSize(&pAction->eachSounds); i++)
			g_ui_State.cbPlayAudio(pAction->eachSounds[i], pDef->pchFilename);

	for (i = 0; i < eaSize(&pAction->eachCommands); i++)
		globCmdParse(pAction->eachCommands[i]);

// Not enabled until we need support for it.
// 	if (pAction->eaExpressions)
// 	{
// 		ExprContext *pContext = ui_GenGetContext(NULL);
// 		for (i = 0; i < eaSize(&pAction->eaExpressions); i++)
// 		{
// 			MultiVal mv = {0};
// 			exprEvaluate(pAction->eaExpressions[i], pContext, &mv);
// 		}
// 	}
}

UIGenJailCell *ui_GenJailCellCreate(UIGenJail *pJail, UIGenJailCellBlock *pBlock, S32 iIndex)
{
	UIGenJailCell *pCell = StructCreate(parse_UIGenJailCell);
	UIGenJailDefault *pDefaultGen = eaGet(&pBlock->eaDefaultGens, iIndex);
	UISizeSpec MinimumWidth, MinimumHeight;
	UISizeSpec MaximumWidth, MaximumHeight;
	COPY_HANDLE(pCell->hJail, pJail->hJail);
	pCell->pBlock = pBlock;
	pCell->pJail = pJail;
	pCell->pchName = pBlock->pchName;
	pCell->Parent.pResult->pos = *pBlock->eaPosition[iIndex % eaSize(&pBlock->eaPosition)];
	if (pCell->Parent.pResult->pos.MinimumHeight.fMagnitude <= 0)
	{
		pCell->Parent.pResult->pos.MinimumHeight.fMagnitude = pBlock->iResizeBorder * 3;
		pCell->Parent.pResult->pos.MinimumHeight.eUnit = UIUnitFixed;
	}
	if (pCell->Parent.pResult->pos.MinimumWidth.fMagnitude <= 0)
	{
		pCell->Parent.pResult->pos.MinimumWidth.fMagnitude = pBlock->iResizeBorder * 3;
		pCell->Parent.pResult->pos.MinimumWidth.eUnit = UIUnitFixed;
	}
	pCell->iIndex = iIndex;
	GenJailCellFixName(pCell);

	// Save the minimum/maximum size from the Jail
	MinimumWidth = pCell->Parent.pResult->pos.MinimumWidth;
	MinimumHeight = pCell->Parent.pResult->pos.MinimumHeight;
	MaximumWidth = pCell->Parent.pResult->pos.MaximumWidth;
	MaximumHeight = pCell->Parent.pResult->pos.MaximumHeight;

	GenJailCellPositionGet(pCell);

	// Ensure that that minimum/maximum size are updated when changed
	pCell->Parent.pResult->pos.MinimumWidth = MinimumWidth;
	pCell->Parent.pResult->pos.MinimumHeight = MinimumHeight;
	pCell->Parent.pResult->pos.MaximumWidth = MaximumWidth;
	pCell->Parent.pResult->pos.MaximumHeight = MaximumHeight;

	// Ensure the default gen is set
	if (!GET_REF(pCell->hGen) && pDefaultGen)
		COPY_HANDLE(pCell->hGen, pDefaultGen->hDefaultGen);

	GenJailCellFixAspectRatio(pJail, pCell, 0);
	eaPush(&pJail->eaCells, pCell);
	return pCell;
}

void ui_GenJailPointerUpdate(UIGenJail* pJail)
{
	int i;
	for (i = eaSize(&pJail->eaCells) - 1; i >= 0; i--)
	{
		UIGenJailCell *pCell = pJail->eaCells[i];
		UIGen *pGen = GET_REF(pCell->hGen);
		if (pGen)
			ui_GenPointerUpdateCB(pGen, &pCell->Parent);
	}
}

UIGenJail *ui_GenJailCreate(UIGenJailDef *pDef)
{
	if (pDef)
	{
		UIGenJail *pJail = StructCreate(parse_UIGenJail);
		S32 i;
		S32 j;
		SET_HANDLE_FROM_REFERENT(s_hJailDict, pDef, pJail->hJail);
		for (i = 0; i < eaSize(&pDef->eaCellBlock); i++)
		{
			char achCellName[1024];
			S32 iCellCount;
			sprintf(achCellName, "Jail_Cell_Count_%s_%s", pDef->pchPrisonName && *pDef->pchPrisonName ? pDef->pchPrisonName : pDef->pchName, pDef->eaCellBlock[i]->pchName);
			iCellCount = ui_GenGetPersistedInt(achCellName, pDef->eaCellBlock[i]->iDefaultCells);
			MIN1(iCellCount, pDef->eaCellBlock[i]->iMaxCells);
			if (iCellCount > 0)
			{
				for (j = 0; j < iCellCount; j++)
				{
					UIGenJailCell *pCell = ui_GenJailCellCreate(pJail, pDef->eaCellBlock[i], j);
				}
			}
			else
			{
				UIGenJailCell *pCell = ui_GenJailCellCreate(pJail, pDef->eaCellBlock[i], 0);
				pCell->iIndex = -1;
			}
		}
		ui_GenSetPointerUpdateCallback(pJail, ui_GenJailPointerUpdate);
		return pJail;
	}
	else
		return NULL;
}

UIGenJail *ui_GenJailShow(UIGenJailKeeper *pKeeper, UIGenJailDef *pDef)
{
	S32 i;
	if (!pKeeper)
		pKeeper = g_ui_pDefaultKeeper;
	for (i = 0; i < eaSize(&pKeeper->eaJail); i++)
	{
		if (GET_REF(pKeeper->eaJail[i]->hJail) == pDef)
			return pKeeper->eaJail[i];
	}
	if (pDef)
	{
		UIGenJail *pJail = ui_GenJailCreate(pDef);
		eaPush(&pKeeper->eaJail, pJail);
		GenJailRunAction(pJail, pDef->pBeforeCreate);
		return pJail;
	}
	return NULL;
}

UIGenJail *ui_GenJailHide(UIGenJailKeeper *pKeeper, UIGenJailDef *pDef)
{
	S32 i;
	if (!pKeeper)
		pKeeper = g_ui_pDefaultKeeper;
	for (i = 0; i < eaSize(&pKeeper->eaJail); i++)
	{
		if (GET_REF(pKeeper->eaJail[i]->hJail) == pDef)
		{
			// If hiding an entire jail, exit rearrange mode.
			globCmdParse("-GenJailCells");
			GenJailRunAction(pKeeper->eaJail[i], pDef->pBeforeHide);
			ui_GenClearPointerUpdateCallback(pKeeper->eaJail[i]);
			return eaRemove(&pKeeper->eaJail, i);
		}
	}
	return NULL;
}

static void GenJailCellSwap(UIGenJailCell *pA, UIGenJailCell *pB)
{
	UIGen *pGenA = GET_REF(pA->hGen);
	COPY_HANDLE(pA->hGen, pB->hGen);
	SET_HANDLE_FROM_REFERENT("UIGen", pGenA, pB->hGen);
}

void ui_GenJailCellSetGen(UIGenJailCell *pCell, UIGen *pGen)
{
	UIGen *pOld = GET_REF(pCell->hGen);
	assert(!pGen || ui_GenInDictionary(pGen));
	if (pOld == pGen)
		return;
	if (pOld)
	{
		pOld->pParent = NULL;
		ui_GenQueueReset(pOld);
	}
	SET_HANDLE_FROM_REFERENT("UIGen", pGen, pCell->hGen);
	pCell->iLastInteractTime = pGen ? timerCpuMs() : 0;
	if (pGen)
		ui_GenSetState(pGen, kUIGenStateJailed);
}

static S32 GenJailCellCount(UIGenJailCell *pCell)
{
	S32 iCount = 0;
	S32 i;
	for (i = 0; i < eaSize(&pCell->pJail->eaCells); i++)
		if (pCell->pJail->eaCells[i]->pBlock == pCell->pBlock && pCell->iIndex >= 0)
			iCount++;
	return iCount;
}

static bool GenJailCellMoveResizeCheck(UIGenJailKeeper *pKeeper, UIGenJailCell *pCell, F32 fScale, const CBox *pParent)
{
	UIGenJailCellBlock *pBlock = pCell->pBlock;
	S32 iMouseX = g_ui_State.mouseX;
	S32 iMouseY = g_ui_State.mouseY;
	S32 iDownX = INT_MIN;
	S32 iDownY = INT_MIN;
	bool bIsDown = mouseIsDown(MS_LEFT);
	CBox *pBox = &pCell->Parent.ScreenBox;
	UIDirection eCursor = UINoDirection;
	UIDirection eChanged = UINoDirection;
	if (!inpCheckHandled())
		mouseDownPos(MS_LEFT, &iDownX, &iDownY);
	if (point_cbox_clsn(iMouseX, iMouseY, pBox))
		pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
	if (!bIsDown)
	{
		if (pCell->iGrabbedX >= 0 || pCell->eResize)
		{
			GenJailCellPositionSave(pCell);
		}
		pCell->iGrabbedX = -1;
		pCell->iGrabbedY = -1;
		pCell->eResize = UINoDirection;
		iDownX = INT_MIN;
		iDownY = INT_MIN;
	}
	if (pCell->iGrabbedX < 0 && !pCell->eResize && pBlock->eResizable && pBlock->iResizeBorder)
	{
		F32 fResizeBorder = pBlock->iResizeBorder;
		CBox Check;
		
		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->lx - fResizeBorder / 2, pBox->hy - fResizeBorder / 2, fResizeBorder, fResizeBorder);
			if (bIsDown && point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UIBottomLeft;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UIBottomLeft;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->hx - fResizeBorder / 2, pBox->hy - fResizeBorder / 2, fResizeBorder, fResizeBorder);
			if (bIsDown && point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UIBottomRight;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UIBottomRight;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->lx, pBox->hy - fResizeBorder / 2, CBoxWidth(pBox), fResizeBorder);
			if (point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UIBottom;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UIBottom;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->lx - fResizeBorder / 2, pBox->ly - fResizeBorder / 2, fResizeBorder, fResizeBorder);
			if (bIsDown && point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UITopLeft;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UITopLeft;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->hx - fResizeBorder / 2, pBox->ly - fResizeBorder / 2, fResizeBorder, fResizeBorder);
			if (bIsDown && point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UITopRight;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UITopRight;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->lx, pBox->ly - fResizeBorder / 2, CBoxWidth(pBox), fResizeBorder);
			if (bIsDown && point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UITop;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UITop;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->lx - fResizeBorder / 2, pBox->ly, fResizeBorder, CBoxHeight(pBox));
			if (bIsDown && point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UILeft;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UILeft;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (!pCell->eResize)
		{
			BuildCBox(&Check, pBox->hx - fResizeBorder / 2, pBox->ly, fResizeBorder, CBoxHeight(pBox));
			if (bIsDown && point_cbox_clsn(iDownX, iDownY, &Check))
			{
				pCell->eResize = UIRight;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
			else if (!eCursor && point_cbox_clsn(iMouseX, iMouseY, &Check))
			{
				eCursor = UIRight;
				pKeeper->pHovered = FIRST_IF_SET(pKeeper->pHovered, pCell);
			}
		}

		if (pCell->eResize)
		{
			pCell->iGrabbedX = (pCell->eResize & UILeft) ? iDownX - pCell->Parent.ScreenBox.lx : pCell->Parent.ScreenBox.hx - iMouseX;
			pCell->iGrabbedY = (pCell->eResize & UITop) ? iDownY - pCell->Parent.ScreenBox.ly : pCell->Parent.ScreenBox.hy - iMouseY;
		}
	}
	if (!pCell->eResize && pCell->iGrabbedX < 0 && mouseDownHit(MS_LEFT, &pCell->Parent.ScreenBox))
	{
		pCell->iGrabbedX = iDownX - pBox->lx;
		pCell->iGrabbedY = iDownY - pBox->ly;
	}

	if (pCell->eResize)
	{
		if (pCell->eResize & UILeft)
			pBox->lx = max(iMouseX - pCell->iGrabbedX, pParent->lx);
		else if (pCell->eResize & UIRight)
			pBox->hx = min(iMouseX + pCell->iGrabbedX, pParent->hx);
		if (pCell->eResize & UITop)
			pBox->ly = max(iMouseY - pCell->iGrabbedY, pParent->ly);
		else if (pCell->eResize & UIBottom)
			pBox->hy = min(iMouseY + pCell->iGrabbedY, pParent->hy);

		// Try to maintain aspect ratio
		if (pCell->fAspectRatio)
		{
			F32 fNewWidth, fNewHeight;

			fNewWidth = round(CBoxHeight(pBox) * pCell->fAspectRatio);
			fNewHeight = round(CBoxWidth(pBox) / pCell->fAspectRatio);

			if (!(pCell->eResize & (UILeft|UIRight)))
				fNewHeight = CBoxHeight(pBox);
			else if (!(pCell->eResize & (UITop|UIBottom)))
				fNewWidth = CBoxWidth(pBox);
			else
			{
				if (fNewWidth <= CBoxWidth(pBox))
					fNewWidth = CBoxWidth(pBox);
				if (fNewHeight <= CBoxHeight(pBox))
					fNewHeight = CBoxHeight(pBox);
			}

			// Adjust the height
			if (fNewHeight != CBoxHeight(pBox))
			{
				if (pCell->eResize & UITop)
					pBox->ly -= fNewHeight - CBoxHeight(pBox);
				else if (pCell->eResize & UIBottom)
					pBox->hy += fNewHeight - CBoxHeight(pBox);
				else
				{
					fNewHeight -= CBoxHeight(pBox);
					pBox->ly -= round(fNewHeight / 2);
					pBox->hy += fNewHeight - round(fNewHeight / 2);
					eChanged |= UITop | UIBottom;
				}
			}

			// Adjust the width
			if (fNewWidth != CBoxWidth(pBox))
			{
				if (pCell->eResize & UILeft)
					pBox->lx -= fNewWidth - CBoxWidth(pBox);
				else if (pCell->eResize & UIRight)
					pBox->hx += fNewWidth - CBoxWidth(pBox);
				else
				{
					fNewWidth -= CBoxWidth(pBox);
					pBox->lx -= round(fNewWidth / 2);
					pBox->hx += fNewWidth - round(fNewWidth / 2);
					eChanged |= UILeft | UIRight;
				}
			}

			// Clamp to screen
			if (pBox->lx < 0)
			{
				CBoxMoveX(pBox, -pBox->lx);
				eChanged |= UILeft | UIRight;
			}
			else if (pBox->hx > g_ui_State.screenWidth)
			{
				CBoxMoveX(pBox, pBox->lx + g_ui_State.screenWidth - pBox->hx);
				eChanged |= UILeft | UIRight;
			}
			if (pBox->ly < 0)
			{
				CBoxMoveY(pBox, -pBox->ly);
				eChanged |= UITop | UIBottom;
			}
			else if (pBox->hy > g_ui_State.screenHeight)
			{
				CBoxMoveY(pBox, pBox->ly + g_ui_State.screenHeight - pBox->hy);
				eChanged |= UITop | UIBottom;
			}
		}
	}
	else if (pCell->iGrabbedX >= 0)
	{
		CBoxMoveX(pBox, iMouseX - pCell->iGrabbedX);
		CBoxMoveY(pBox, iMouseY - pCell->iGrabbedY);
	}

	pCell->eResize &= pBlock->eResizable;
	eCursor &= pBlock->eResizable;
	if (pCell->eResize)
		eCursor = pCell->eResize;
	if (eCursor && !inpCheckHandled())
		ui_SetCursorForDirection(eCursor);

	if ((pCell->eResize || pCell->iGrabbedX >= 0)
		&& !ui_GenCBoxToPosition(&pCell->Parent.pResult->pos, &pCell->Parent.ScreenBox, pParent, pCell->eResize | eChanged, pCell->Parent.fScale))
	{
		UIGenJailDef *pDef = GET_REF(pCell->hJail);
		const char *pchFilename = pDef ? pDef->pchFilename : NULL;
		ErrorFilenamef(pchFilename, "%s: Jails must be sized using Fixed or Percentage units.", pCell->pchName);
	}

	pCell->Parent.UnpaddedScreenBox = *pBox;

	if (pCell->iGrabbedX >= 0 || pCell->eResize)
	{
		ui_SoftwareCursorThisFrame();
		pKeeper->pHovered = pCell;
	}

	return pCell->iGrabbedX >= 0 || pCell->eResize || point_cbox_clsn(iMouseX, iMouseY, &pCell->Parent.ScreenBox);
}

//////////////////////////////////////////////////////////////////////////
// Jail Keeper widget implementation

UIGenJailKeeper *ui_GenJailKeeperCreate(void)
{
	UIGenJailKeeper *pKeeper = calloc(1, sizeof(UIGenJailKeeper));
	ui_WidgetInitialize(UI_WIDGET(pKeeper), ui_GenJailKeeperTick, ui_GenJailKeeperDraw, ui_GenJailKeeperFree, NULL, NULL);
	pKeeper->widget.uUIGenWidget = 1;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pKeeper), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	return pKeeper;
}

void ui_GenJailKeeperFree(UIGenJailKeeper *pKeeper)
{
	eaDestroy(&pKeeper->eaGens);
	eaDestroy(&pKeeper->eaCells);
	eaDestroy(&pKeeper->eaPrisons);
	eaDestroyStruct(&pKeeper->eaJail, parse_UIGenJail);
}

void ui_GenJailKeeperRemoveCell(UIGenJailKeeper *pKeeper, UIGenJail *pJail, UIGenJailCell *pCell)
{
	UIGenJailDef *pJailDef = GET_REF(pJail->hJail);
	S32 iCount = GenJailCellCount(pCell);
	S32 iAt = eaFind(&pJail->eaCells, pCell);

	// Don't allow removing the last cell of a type.
	if (iCount <= pCell->pBlock->iMinCells)
		return;

	if (iAt >= 0 && pJailDef)
	{
		char achCellName[1024];
		S32 i;
		sprintf(achCellName, "Jail_Cell_Count_%s_%s", pJailDef->pchPrisonName && *pJailDef->pchPrisonName ? pJailDef->pchPrisonName : pJailDef->pchName, pCell->pBlock->pchName);
		if (iCount == 1)
		{
			pCell->iIndex = -1;
		}
		else
		{
			for (i = eaSize(&pKeeper->eaCells) - 1; i >= 0; i--)
			{
				if (pKeeper->eaCells[i]->iIndex > pCell->iIndex && pKeeper->eaCells[i]->pBlock == pCell->pBlock)
				{
					pKeeper->eaCells[i]->iIndex--;
					GenJailCellFixName(pKeeper->eaCells[i]);
				}
			}

			if (eaFindAndRemove(&pKeeper->eaCells, pCell) >= 0)
			{
				GenJailCellPositionForget(pCell);
			}

			eaFindAndRemove(&pKeeper->eaGens, GET_REF(pCell->hGen));
			eaRemove(&pJail->eaCells, iAt);
			StructDestroySafe(parse_UIGenJailCell, &pCell);
		}
		ui_GenSetPersistedInt(achCellName, iCount - 1);
	}
}

UIGenJailCell *ui_GenJailKeeperAddCell(UIGenJailKeeper *pKeeper, UIGenJail *pJail, UIGenJailCell *pCell)
{
	UIGenJailDef *pJailDef = GET_REF(pJail->hJail);
	UIGenJailCellBlock *pBlock = pCell->pBlock;
	UIGenJailCell *pNewCell = NULL;
	S32 iCount = GenJailCellCount(pCell);

	if (pJailDef && iCount < pBlock->iMaxCells)
	{
		char achCellName[1024];
		pNewCell = ui_GenJailCellCreate(pJail, pBlock, iCount);
		if (pNewCell->iIndex >= eaSize(&pBlock->eaPosition))
		{
			// Copy the existing cell's position, offset slightly.
			pNewCell->Parent.pResult->pos = pCell->Parent.pResult->pos;
			if (pNewCell->Parent.pResult->pos.eOffsetFrom & UILeft)
				pNewCell->Parent.pResult->pos.fPercentX += 0.1;
			else if (pNewCell->Parent.pResult->pos.eOffsetFrom & UIRight)
				pNewCell->Parent.pResult->pos.fPercentX -= 0.1;
			if (pNewCell->Parent.pResult->pos.eOffsetFrom & UITop)
				pNewCell->Parent.pResult->pos.fPercentY += 0.1;
			else if (pNewCell->Parent.pResult->pos.eOffsetFrom & UIBottom)
				pNewCell->Parent.pResult->pos.fPercentY -= 0.1;
		}
		pNewCell->Parent.chPriority = pCell->Parent.chPriority + 1;
		sprintf(achCellName, "Jail_Cell_Count_%s_%s", pJailDef->pchPrisonName && *pJailDef->pchPrisonName ? pJailDef->pchPrisonName : pJailDef->pchName, pBlock->pchName);
		ui_GenSetPersistedInt(achCellName, iCount + 1);
	}
	return pNewCell;
}

static int GenJailCellSortPriority(const UIGenJailCell **ppA, const UIGenJailCell **ppB)
{
	int iRet = (*ppA)->Parent.chPriority - (*ppB)->Parent.chPriority;
	if (iRet)
		return iRet;
	iRet = stricmp((*ppA)->pchName, (*ppB)->pchName);
	if (iRet)
		return iRet;
	return (*ppA)->iIndex - (*ppB)->iIndex;
}

static void GenJailUpdate(UIGenJail *pJail, UIGenJail ***peaJails, S32 iIndex)
{
	UIGenJailDef *pDef = GET_REF(pJail->hJail);
	S32 i;

	if (!pDef)
		return;

	pJail->fScale = g_GenState.fScale;
	if (pDef->ausScaleAsIf[0] && pDef->ausScaleAsIf[1])
	{
		F32 fScaleX = g_ui_State.screenWidth / (F32)pDef->ausScaleAsIf[0];
		F32 fScaleY = g_ui_State.screenHeight / (F32)pDef->ausScaleAsIf[1];
		F32 fScale = min(fScaleX, fScaleY);
		if (pDef->bScaleNoGrow && fScale > 1) 
			fScale = 1;
		if (pDef->bScaleNoShrink && fScale < 1) 
			fScale = 1;
		pJail->fScale *= fScale;
	}

	for (i = 0; i < eaSize(&pDef->eaStateDef) && eaGet(peaJails, iIndex) == pJail; i++)
	{
		bool bOn = eaiFind(&g_GenState.eaiGlobalOnStates, pDef->eaStateDef[i]->eState) >= 0;
		if (bOn && !TSTB(pJail->bfStates, i))
		{
			SETB(pJail->bfStates, i);
			GenJailRunAction(pJail, pDef->eaStateDef[i]->pOnEnter);
		}
		else if (!bOn && TSTB(pJail->bfStates, i))
		{
			CLRB(pJail->bfStates, i);
			GenJailRunAction(pJail, pDef->eaStateDef[i]->pOnExit);
		}
	}

	if (pDef->eaComplexStateDef && eaGet(peaJails, iIndex) == pJail)
	{
		ExprContext *pContext = ui_GenGetContext(NULL);
		for (i = 0; i < eaSize(&pDef->eaComplexStateDef) && eaGet(peaJails, iIndex) == pJail; i++)
		{
			MultiVal mv = {0};
			bool bOn;
			exprEvaluate(pDef->eaComplexStateDef[i]->pCondition, pContext, &mv);
			bOn = MultiValToBool(&mv);
			if (bOn && !TSTB(pJail->bfComplexStates, i))
			{
				SETB(pJail->bfComplexStates, i);
				GenJailRunAction(pJail, pDef->eaComplexStateDef[i]->pOnEnter);
			}
			else if (!bOn && TSTB(pJail->bfComplexStates, i))
			{
				CLRB(pJail->bfComplexStates, i);
				GenJailRunAction(pJail, pDef->eaComplexStateDef[i]->pOnExit);
			}
		}
	}
}

void ui_GenJailDestroy(UIGenJail *pJail)
{
	if (pJail)
	{
		eaPush(&s_eaQueueFree, pJail);
	}
}

void ui_GenJailOncePerFrameInput()
{
	//ui_GenJailKeeperTick(g_ui_pDefaultKeeper, g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);
	UIGenJailKeeper *pKeeper = g_ui_pDefaultKeeper;
	//UI_GET_COORDINATES(pKeeper);
	CBox Screen = { g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0], g_ui_State.viewportMax[1] };
	CBox Dummy = {0, 0, 0, 0};
	S32 i;
	S32 j;

	eaClearFast(&pKeeper->eaGens);
	eaClearFast(&pKeeper->eaCells);
	pKeeper->pHovered = NULL;

	for (i = eaSize(&pKeeper->eaJail) - 1; i >= 0; i--)
		GenJailUpdate(pKeeper->eaJail[i], &pKeeper->eaJail, i);

	if (s_eaQueueFree)
		eaDestroyStruct(&s_eaQueueFree, parse_UIGenJail);

	for (i = 0; i < eaSize(&pKeeper->eaJail); i++)
	{
		UIGenJail *pJail = pKeeper->eaJail[i];
		for (j = 0; j < eaSize(&pJail->eaCells); j++)
		{
			UIGenJailCell *pCell = pJail->eaCells[j];
			if (!g_GenState.bJailUnlocked && (pCell->iGrabbedX >= 0 || pCell->iGrabbedY >= 0 || pCell->eResize))
			{
				pCell->iGrabbedX = -1;
				pCell->iGrabbedY = -1;
				GenJailCellPositionSave(pCell);
			}

			ui_GenPositionToCBox(&pCell->Parent.pResult->pos, &Screen, pJail->fScale, pCell->Parent.pResult->pos.fScale, &pCell->Parent.ScreenBox, &Dummy, &Dummy, NULL);
			pCell->Parent.fScale = pCell->Parent.pResult->pos.fScale * pJail->fScale;
			pCell->Parent.UnpaddedScreenBox = pCell->Parent.ScreenBox;
			eaPush(&pKeeper->eaCells, pCell);
		}
	}

	if (g_GenState.bJailNoGens)
		ui_GenSetTooltipFocus(NULL);

	if (g_GenState.bJailUnlocked)
	{
		eaQSort(pKeeper->eaCells, GenJailCellSortPriority);

		for (j = eaSize(&pKeeper->eaCells) - 1; j >= 0; j--)
		{
			UIGenJailCell *pCell = pKeeper->eaCells[j];
			F32 fTextScale = max(0.75, pCell->Parent.fScale);
			S32 iResizeBorder = pCell->pBlock->iResizeBorder;
			F32 fButtonHeight = ui_StyleFontLineHeight(GET_REF(pCell->pBlock->hFont), fTextScale)
				+ ui_TextureAssemblyHeight(GET_REF(pCell->pBlock->hButtonAssembly)) * pCell->Parent.fScale;
			F32 fButtonWidth = max(60, CBoxWidth(&pCell->Parent.ScreenBox) / 3 - iResizeBorder * 2);
			S32 iCount = GenJailCellCount(pCell);
			bool bAddAllowed = iCount < pCell->pBlock->iMaxCells;
			bool bCloseAllowed = iCount > pCell->pBlock->iMinCells;
			CBox Button;

			if (bAddAllowed)
			{
				BuildCBox(
					&Button,
					pCell->Parent.ScreenBox.lx + iResizeBorder,
					pCell->Parent.ScreenBox.ly + iResizeBorder,
					fButtonWidth,
					fButtonHeight);
				if (mouseClickHit(MS_LEFT, &Button))
				{
					if (pCell->iIndex < 0)
					{
						UIGenJailDef *pJailDef = GET_REF(pCell->pJail->hJail);
						char achCellName[1024];
						pCell->iIndex = 0;
						if (pJailDef)
						{
							sprintf(achCellName, "Jail_Cell_Count_%s_%s", pJailDef->pchPrisonName && *pJailDef->pchPrisonName ? pJailDef->pchPrisonName : pJailDef->pchName, pCell->pBlock->pchName);
							ui_GenSetPersistedInt(achCellName, 1);
						}
						GenJailCellFixName(pCell);
						GenJailCellPositionSave(pCell);
					}
					else
						ui_GenJailKeeperAddCell(pKeeper, pCell->pJail, pCell);
					inpHandled();
					continue;
				}
			}

			if (bCloseAllowed)
			{
				BuildCBox(
					&Button,
					pCell->Parent.ScreenBox.hx - (fButtonWidth + iResizeBorder),
					pCell->Parent.ScreenBox.ly + iResizeBorder,
					fButtonWidth,
					fButtonHeight);
				if (mouseClickHit(MS_LEFT, &Button))
				{
					ui_GenJailKeeperRemoveCell(pKeeper, pCell->pJail, pCell);
					inpHandled();
					continue;
				}
			}

			if ((GET_REF(pCell->hGen) || g_GenState.bJailFrames)
				&& GenJailCellMoveResizeCheck(pKeeper, pCell, pCell->Parent.fScale, &Screen))
			{
				ui_GenPositionToCBox(&pCell->Parent.pResult->pos, &Screen, pCell->pJail->fScale, pCell->Parent.pResult->pos.fScale, &pKeeper->eaCells[j]->Parent.ScreenBox, &Dummy, &Dummy, NULL);
				if (!inpLevelPeek(INP_SHIFT))
				{
					inpHandled();
					if ((pCell->eResize || pCell->iGrabbedX >= 0))
						pCell->Parent.chPriority = eaSize(&pKeeper->eaCells) + 1;
				}
			}
		}

		if (pKeeper->bReprioritize && pKeeper->pHovered)
		{
			pKeeper->pHovered->Parent.chPriority = eaSize(&pKeeper->eaCells) + 1;
			pKeeper->bReprioritize = false;
			GenJailCellPositionSave(pKeeper->pHovered);
		}
	}
}

void ui_GenJailKeeperTick(UIGenJailKeeper *pKeeper, UI_PARENT_ARGS)
{
	S32 i;
	S32 j;
	eaQSort(pKeeper->eaCells, GenJailCellSortPriority);

	for (j = eaSize(&pKeeper->eaCells) - 1; j >= 0; j--)
	{
		UIGenJailCell *pCell = pKeeper->eaCells[j];
		UIGenJail *pJail = pCell->pJail;
		UIGen *pGen = GET_REF(pCell->hGen);
		pCell->Parent.chPriority = j;

		if (pCell->iIndex < 0)
			continue;

		if (pGen)
		{
			if (mouseDownAnyHit(MS_LEFT, &pCell->Parent.ScreenBox)
				|| mouseDownAnyHit(MS_RIGHT, &pCell->Parent.ScreenBox))
			{
				pCell->iLastInteractTime = timerCpuMs();
				// Raising the actual gen is going to be handled by the top-level
				// management code, but we need to raise the cell as well.
				// Don't save it, because there's too much database load.
				if (j != eaSize(&pKeeper->eaCells) - 1)
					pCell->Parent.chPriority = eaSize(&pKeeper->eaCells) + 1;
			}

			ui_GenState(pGen, kUIGenStateJailed, true);
			ui_GenUpdateCB(pGen, &pCell->Parent);

			// Only run Layout if the gen is still in this cell.
			if (pGen == GET_REF(pCell->hGen))
				ui_GenLayoutCB(pGen, &pCell->Parent);

			// Only queue for tick/draw if the gen is still in this cell.
			if (pGen == GET_REF(pCell->hGen) && !g_GenState.bJailNoGens)
				eaPush(&pKeeper->eaGens, pGen);
		}

		// Reset, need to save everything.
		if (pKeeper->bSaveAllCells || pCell->bSave)
			GenJailCellPositionSave(pCell);

		pCell->bSave = false;
	}

	pKeeper->bSaveAllCells = false;

	eaReverse(&pKeeper->eaGens);

	if (!g_GenState.bJailNoGens)
		for (i = eaSize(&pKeeper->eaGens) - 1; i >= 0; i--)
			ui_GenManageTopLevel(pKeeper->eaGens[i]);

}

void ui_GenJailKeeperDraw(UIGenJailKeeper *pKeeper, UI_PARENT_ARGS)
{
	static REF_TO(Message) s_hExit;
	if (!GET_REF(s_hExit))
		SET_HANDLE_FROM_STRING("Message", "Jail_Exit", s_hExit);

	if (g_GenState.bJailFrames)
	{
		UI_GET_COORDINATES(pKeeper);
		UIGenJail *pJail = eaGet(&pKeeper->eaJail, 0);
		UIGenJailDef *pDef = pJail ? GET_REF(pJail->hJail) : NULL;
		S32 i;

		if (pDef)
			ui_StyleFontUse(GET_REF(pDef->hFont), false, kWidgetModifier_None);
		gfxfont_PrintWrapped(g_ui_State.screenWidth / 2,
			g_ui_State.screenHeight / 3,
			UI_INFINITE_Z, g_ui_State.screenWidth * 0.8,
			g_ui_State.scale * 2, g_ui_State.scale * 2, CENTER_XY, true,
			TranslateMessageRefDefault(s_hExit, "Press Escape to exit arrange mode"));
		for (i = 0; i < eaSize(&pKeeper->eaCells); i++)
		{
			UIGenJailCell *pCell = pKeeper->eaCells[i];
			UIGenJailCellBlock *pBlock = pCell->pBlock;
			F32 fTextScale = max(0.75, pCell->Parent.fScale);
			S32 iResizeBorder = pCell->pBlock->iResizeBorder;
			F32 fButtonHeight = ui_StyleFontLineHeight(GET_REF(pCell->pBlock->hFont), fTextScale)
				+ ui_TextureAssemblyHeight(GET_REF(pCell->pBlock->hButtonAssembly)) * pCell->Parent.fScale;
			CBox Dummy = {0, 0, 0, 0};
			F32 fButtonWidth = max(60, CBoxWidth(&pCell->Parent.ScreenBox) / 3 - iResizeBorder * 2);
			F32 fCenterX;
			F32 fCenterY;
			F32 fY;
			F32 fZ = UI_GET_Z();
			CBox Button;
			S32 iCount = GenJailCellCount(pCell);
			bool bAddAllowed = iCount < pCell->pBlock->iMaxCells;
			bool bCloseAllowed = iCount > pCell->pBlock->iMinCells && (!pCell->pBlock->bKeepDefaultCells || pCell->iIndex >= pCell->pBlock->iDefaultCells);
			F32 fWidth = CBoxWidth(&pCell->Parent.ScreenBox);

			ui_StyleFontUse(GET_REF(pBlock->hFont), false, kWidgetModifier_None);

			if (bAddAllowed)
			{
				const char *pchText;

				if (pCell->iIndex < 0)
					pchText = TranslateMessageKeyDefault("Jail_Show", "Show");
				else
					pchText = TranslateMessageKeyDefault("Jail_New", "New");
				BuildCBox(
					&Button,
					pCell->Parent.ScreenBox.lx + iResizeBorder,
					pCell->Parent.ScreenBox.ly + iResizeBorder,
					fButtonWidth,
					fButtonHeight);
				ui_TextureAssemblyDraw(GET_REF(pCell->pBlock->hButtonAssembly), &Button, NULL,
					pCell->Parent.fScale, fZ + 0.5, fZ + 0.9, 255, NULL);
				gfxfont_Print((Button.lx + Button.hx) / 2, (Button.ly + Button.hy) / 2,
					fZ + 0.9, fTextScale, fTextScale,
					CENTER_XY, pchText);
			}

			if (bCloseAllowed)
			{
				const char *pchText;

				if (iCount == 1)
					pchText = TranslateMessageKeyDefault("Jail_Hide", "Hide");
				else
					pchText = TranslateMessageKeyDefault("Jail_Delete", "Delete");
				BuildCBox(
					&Button,
					pCell->Parent.ScreenBox.hx - (fButtonWidth + iResizeBorder),
					pCell->Parent.ScreenBox.ly + iResizeBorder,
					fButtonWidth,
					fButtonHeight);
				ui_TextureAssemblyDraw(GET_REF(pCell->pBlock->hButtonAssembly), &Button, NULL,
					pCell->Parent.fScale, fZ + 0.5, fZ + 0.9, 255, NULL);
				gfxfont_Print((Button.lx + Button.hx) / 2, (Button.ly + Button.hy) / 2,
					fZ + 0.9, fTextScale, fTextScale,
					CENTER_XY, pchText);
			}

			if (pCell == pKeeper->pHovered && GET_REF(pBlock->hHoverFont))
				ui_StyleFontUse(GET_REF(pBlock->hHoverFont), false, kWidgetModifier_None);

			if (pCell == pKeeper->pHovered && pCell->iIndex >= 0 && GET_REF(pBlock->hHoverAssembly))
				ui_TextureAssemblyDraw(GET_REF(pBlock->hHoverAssembly), &pCell->Parent.ScreenBox, NULL, pCell->Parent.fScale, fZ, fZ + 0.5, 255, NULL);
			else if (pCell == pKeeper->pHovered && pCell->iIndex < 0 && GET_REF(pBlock->hHoverGhostAssembly))
				ui_TextureAssemblyDraw(GET_REF(pBlock->hHoverGhostAssembly), &pCell->Parent.ScreenBox, NULL, pCell->Parent.fScale, fZ, fZ + 0.5, 255, NULL);
			else if (pCell->iIndex >= 0 && GET_REF(pBlock->hAssembly))
				ui_TextureAssemblyDraw(GET_REF(pBlock->hAssembly), &pCell->Parent.ScreenBox, NULL, pCell->Parent.fScale, fZ, fZ + 0.5, 255, NULL);
			else if (pCell->iIndex < 0 && GET_REF(pBlock->hGhostAssembly))
				ui_TextureAssemblyDraw(GET_REF(pBlock->hGhostAssembly), &pCell->Parent.ScreenBox, NULL, pCell->Parent.fScale, fZ, fZ + 0.5, 255, NULL);
			else
				ui_DrawCapsule(&pCell->Parent.ScreenBox, fZ, ColorWhite, pCell->Parent.fScale);
			fCenterX = (pCell->Parent.ScreenBox.lx + pCell->Parent.ScreenBox.hx) / 2;
			fCenterY = (pCell->Parent.ScreenBox.ly + pCell->Parent.ScreenBox.hy) / 2;
			fY = pCell->Parent.ScreenBox.ly
				+ fButtonHeight
				+ ui_StyleFontLineHeight(GET_REF(pBlock->hFont), pCell->Parent.fScale)
				+ pCell->Parent.fScale * pCell->pBlock->iResizeBorder;

			if (pCell->iIndex < 0)
				gfxfont_PrintfWrapped(fCenterX, fCenterY, fZ + 0.9, fWidth * 0.8, fTextScale, fTextScale,
					CENTER_X, true, "%s%s", TranslateMessageRef(pBlock->hDisplayName), TranslateMessageKeyDefault("Jail_Hidden", "This cell is hidden."));
			else if (pCell->pBlock->iMaxCells == 1)
				gfxfont_PrintWrapped(fCenterX, fCenterY, fZ + 0.9, fWidth * 0.8, fTextScale, fTextScale,
					CENTER_X, true, TranslateMessageRef(pBlock->hDisplayName));
			else
				gfxfont_PrintfWrapped(fCenterX, fCenterY, fZ + 0.9, fWidth * 0.8, fTextScale, fTextScale,
				CENTER_X, true, "%s\n#%d", TranslateMessageRef(pBlock->hDisplayName), pCell->iIndex + 1);
		}
	}
}

UIGenJailCell *ui_GenJailKeeperAdd(UIGenJailKeeper *pKeeper, UIGen *pGen)
{
	S32 i;
	S32 j;
	const char *pchCell = pGen ? pGen->pchJailCell : NULL;

	UIGenJailCell *pBest = NULL;
	if (!pchCell)
		return NULL;
	if (!pKeeper)
		pKeeper = g_ui_pDefaultKeeper;
	for (i = 0; i < eaSize(&pKeeper->eaJail); i++)
	{
		UIGenJail *pJail = pKeeper->eaJail[i];

		for (j = eaSize(&pJail->eaCells) - 1; j >= 0; j--)
		{
			UIGenJailCell *pCell = pJail->eaCells[j];
			if (pchCell == pCell->pchName && pCell->iIndex >= 0)
			{
				U32 currTime = timerCpuMs();
				if (GET_REF(pCell->hGen) == pGen)
				{
					pCell->Parent.chPriority = 255;
					pCell->iLastInteractTime = currTime;
					return pCell;
				}
				else if (!pBest)
					pBest = pCell;
				else if (!GET_REF(pCell->hGen))
					pBest = pCell;
				else if ((currTime - pBest->iLastInteractTime) < (currTime - pCell->iLastInteractTime)) // Can't just compare, these numbers wrap around
					pBest = pCell;
			}
		}
	}

	if (pBest)
	{
		ui_GenJailCellSetGen(pBest, pGen);
		pBest->Parent.chPriority = 255;
	}
	return pBest;
}

bool ui_GenJailKeeperRemove(UIGenJailKeeper *pKeeper, UIGen *pGen)
{
	S32 i;
	if (!pKeeper)
		pKeeper = g_ui_pDefaultKeeper;
	for (i = 0; i < eaSize(&pKeeper->eaJail); i++)
	{
		UIGenJail *pJail = pKeeper->eaJail[i];
		S32 j;
		for (j = 0; j < eaSize(&pJail->eaCells); j++)
		{
			UIGenJailCell *pCell = pJail->eaCells[j];
			if (GET_REF(pCell->hGen) == pGen)
			{
				ui_GenJailCellSetGen(pCell, NULL);
				if (g_GenState.bJailCompress)
				{
					UIGenJailCell *pHighest = NULL;
					S32 k;
					for (k = 0; k < eaSize(&pJail->eaCells); k++)
					{
						if (pJail->eaCells[k]->pchName == pCell->pchName
							&& (!pHighest || pHighest->iIndex < pJail->eaCells[k]->iIndex))
							pHighest = pJail->eaCells[k];
					}
					if (pHighest && pHighest != pCell)
						GenJailCellSwap(pCell, pHighest);
				}
				return true;
			}
		}

	}
	return false;
}

bool ui_GenJailKeeperRemoveAll(UIGenJailKeeper *pKeeper, const char *pchCellBlock)
{
	bool bRemoved = false;
	S32 i;
	if (!pKeeper)
		pKeeper = g_ui_pDefaultKeeper;
	pchCellBlock = allocFindString(pchCellBlock);
	for (i = 0; i < eaSize(&pKeeper->eaJail); i++)
	{
		UIGenJail *pJail = pKeeper->eaJail[i];
		S32 j;
		for (j = 0; j < eaSize(&pJail->eaCells); j++)
		{
			UIGenJailCell *pCell = pJail->eaCells[j];
			// No need to compress because we'll be removing all of them.
			if (pCell->pchName == pchCellBlock && GET_REF(pCell->hGen))
			{
				bRemoved = true;
				ui_GenJailCellSetGen(pCell, NULL);
			}
		}

	}
	return bRemoved;
}

// Print a list of all jail cells currently managed by the default jail keeper.
AUTO_COMMAND ACMD_NAME(GenJailPrintActive) ACMD_ACCESSLEVEL(9);
void ui_GenJailPrintActive(void)
{
	S32 i;
	S32 j;
	conPrintf("Currently Active Jails:\n");
	for (i = 0; i < eaSize(&g_ui_pDefaultKeeper->eaJail); i++)
	{
		UIGenJail *pJail = g_ui_pDefaultKeeper->eaJail[i];
		conPrintf("  Jail: %s\n", REF_STRING_FROM_HANDLE(pJail->hJail));
		for (j = 0; j < eaSize(&pJail->eaCells); j++)
		{
			UIGenJailCell *pCell = pJail->eaCells[j];
			conPrintf("        Cell %s #%d   (%d, %d) to (%d, %d) (%dx%d)\n",
				pCell->pchName, pCell->iIndex,
				(S32)pCell->Parent.ScreenBox.lx,
				(S32)pCell->Parent.ScreenBox.ly,
				(S32)pCell->Parent.ScreenBox.hx,
				(S32)pCell->Parent.ScreenBox.hy,
				(S32)CBoxWidth(&pCell->Parent.ScreenBox),
				(S32)CBoxHeight(&pCell->Parent.ScreenBox)
				);

			if (REF_STRING_FROM_HANDLE(pCell->hGen))
			{
				conPrintf("             Holding Gen: %s\n", REF_STRING_FROM_HANDLE(pCell->hGen));
			}
		}
	}
}

// Reset all cells to their default sizes and positions.
AUTO_COMMAND ACMD_NAME(GenJailReset) ACMD_ACCESSLEVEL(0);
void ui_GenJailCmdReset(void)
{
	S32 i;
	for (i = 0; i < eaSize(&g_ui_pDefaultKeeper->eaJail); i++)
	{
		UIGenJail *pJail = g_ui_pDefaultKeeper->eaJail[i];
		UIGenJailDef *pDef = GET_REF(pJail->hJail);
		S32 j;
		for (j = eaSize(&pJail->eaCells) - 1; j >= 0; j--)
		{
			UIGenJailCell *pCell = pJail->eaCells[j];
			if (pCell->iIndex >= eaSize(&pCell->pBlock->eaPosition))
			{
				ui_GenJailKeeperRemoveCell(g_ui_pDefaultKeeper, pJail, pCell);
			}
			else if (pCell->iIndex >= 0)
			{
				pCell->Parent.pResult->pos = *pCell->pBlock->eaPosition[pCell->iIndex];
				GenJailCellPositionForget(pCell);
			}
		}
	}
}

// Send the hovered jail to the bottom of the stack.
AUTO_COMMAND ACMD_NAME(GenJailSink) ACMD_ACCESSLEVEL(0);
void ui_GenJailCmdSink(void)
{
	S32 i, iPos;
	if ((iPos = eaFind(&g_ui_pDefaultKeeper->eaCells, g_ui_pDefaultKeeper->pHovered)) >= 0)
	{
		for (i = 0; i < eaSize(&g_ui_pDefaultKeeper->eaCells); i++)
		{
			UIGenJailCell *pCell = g_ui_pDefaultKeeper->eaCells[i];
			if (pCell->Parent.chPriority < g_ui_pDefaultKeeper->pHovered->Parent.chPriority)
				pCell->Parent.chPriority++;
		}
		g_ui_pDefaultKeeper->pHovered->Parent.chPriority = 0;
		g_ui_pDefaultKeeper->bReprioritize = true;
	}
}

// Show a set of jails in a prison
AUTO_COMMAND ACMD_NAME(GenPrisonShow) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(UI);
bool ui_GenPrisonCmdShow(const char *pchPrisonName, const char *pchJailSet)
{
	UIGenPrisonDef *pPrisonDef = eaIndexedGetUsingString(&g_ui_GenPrisonDefs.eaPrisons, pchPrisonName);
	UIGenPrison *pPrison = NULL;
	bool bAddingPrison = false;
	S32 i;

	pchPrisonName = allocFindString(pchPrisonName);
	if (!pchPrisonName || !*pchPrisonName)
		return false;

	for (i = eaSize(&g_ui_pDefaultKeeper->eaPrisons) - 1; i >= 0; i--)
	{
		if (g_ui_pDefaultKeeper->eaPrisons[i]->pchPrisonName == pchPrisonName)
		{
			pPrison = g_ui_pDefaultKeeper->eaPrisons[i];
			break;
		}
	}

	if (i < 0)
	{
		pPrison = StructCreate(parse_UIGenPrison);
		pPrison->pchPrisonName = pchPrisonName;
		eaPush(&g_ui_pDefaultKeeper->eaPrisons, pPrison);
		bAddingPrison = true;
	}

	if (pPrisonDef)
	{
		UIGenPrisonJailSet *pCurrentSet = eaIndexedGetUsingString(&pPrisonDef->eaJailSets, pPrison->pchCurrentPrison);
		UIGenPrisonJailSet *pNewSet = eaIndexedGetUsingString(&pPrisonDef->eaJailSets, pchJailSet);

		if (pCurrentSet != pNewSet || bAddingPrison)
		{
			if (pCurrentSet || pPrisonDef->pDefaultJailSet)
			{
				UIGenJailDefRef **eaJails = pCurrentSet ? pCurrentSet->eaJails : pPrisonDef->pDefaultJailSet->eaJails;

				// Turn off current set
				for (i = eaSize(&eaJails) - 1; i >= 0; i--)
				{
					UIGenJail *pJail = ui_GenJailHide(g_ui_pDefaultKeeper, GET_REF(eaJails[i]->hDef));
					if (pJail)
						ui_GenJailDestroy(pJail);
				}
			}

			if (pNewSet || pPrisonDef->pDefaultJailSet)
			{
				UIGenJailDefRef **eaJails = pNewSet ? pNewSet->eaJails : pPrisonDef->pDefaultJailSet->eaJails;

				// Turn on new set
				for (i = eaSize(&eaJails) - 1; i >= 0; i--)
					ui_GenJailShow(g_ui_pDefaultKeeper, GET_REF(eaJails[i]->hDef));
			}

			StructCopyString(&pPrison->pchCurrentPrison, pchJailSet);
			return true;
		}
	}

	return false;
}

// Show a set of jails in a prison
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenPrisonShow);
bool ui_GenPrisonExprShow(const char *pchPrisonName, const char *pchJailSet)
{
	return ui_GenPrisonCmdShow(pchPrisonName, pchJailSet);
}

// Hide a the visible jails in the prison
AUTO_COMMAND ACMD_NAME(GenPrisonHide) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(UI);
bool ui_GenPrisonCmdHide(const char *pchPrisonName)
{
	UIGenPrisonDef *pPrisonDef = eaIndexedGetUsingString(&g_ui_GenPrisonDefs.eaPrisons, pchPrisonName);
	UIGenPrison *pPrison = NULL;
	S32 i;

	pchPrisonName = allocFindString(pchPrisonName);
	if (!pchPrisonName || !*pchPrisonName)
		return false;

	for (i = eaSize(&g_ui_pDefaultKeeper->eaPrisons) - 1; i >= 0; i--)
	{
		if (g_ui_pDefaultKeeper->eaPrisons[i]->pchPrisonName == pchPrisonName)
		{
			pPrison = g_ui_pDefaultKeeper->eaPrisons[i];
			break;
		}
	}

	if (i < 0)
		return false;

	if (pPrisonDef)
	{
		UIGenPrisonJailSet *pCurrentSet = eaIndexedGetUsingString(&pPrisonDef->eaJailSets, pPrison->pchCurrentPrison);
		if (pCurrentSet || pPrisonDef->pDefaultJailSet)
		{
			UIGenJailDefRef **eaJails = pCurrentSet ? pCurrentSet->eaJails : pPrisonDef->pDefaultJailSet->eaJails;

			// Turn off current set
			for (i = eaSize(&eaJails) - 1; i >= 0; i--)
			{
				UIGenJail *pJail = ui_GenJailHide(g_ui_pDefaultKeeper, GET_REF(eaJails[i]->hDef));
				if (pJail)
					ui_GenJailDestroy(pJail);
			}
		}
	}

	eaFindAndRemove(&g_ui_pDefaultKeeper->eaPrisons, pPrison);
	StructDestroy(parse_UIGenPrison, pPrison);

	return true;
}

// Hide a the visible jails in the prison
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenPrisonHide);
bool ui_GenPrisonExprHide(const char *pchPrisonName)
{
	return ui_GenPrisonCmdHide(pchPrisonName);
}
