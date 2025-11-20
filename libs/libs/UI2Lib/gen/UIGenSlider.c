#include "MultiVal.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenSlider.h"
#include "UITextureAssembly.h"
#include "UIGenPrivate.h"
#include "inputMouse.h"
#include "UIStyle_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static bool ui_GenValidateSlider(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	UIGenSlider *pSlider = (UIGenSlider*)pInt;
	if(pSlider)
	{
		/*UI_GEN_FAIL_IF_RETURN(pGen, 
			!ui_GenBundleStyleBarGetStyleBar(pGen, &pSlider->StyleBarBundle),
			false, "You must provide a Bar or a BarExpr for the BarStyle.");*/

		if(pSlider->pSliderTween)
		{
			UI_GEN_FAIL_IF_RETURN(pGen, 
				pSlider->pSliderTween->eType != kUITweenInstant
				&& pSlider->pSliderTween->eType != kUITweenLinear
				&& pSlider->pSliderTween->eType != kUITweenEaseIn, 
				false, "You are using a nonsensical slider tween type. Valid types are Linear, EaseIn and Instant.");
	
		}
	}
	return true;
}

static F32 ui_GetSliderPosition(UIGen *pGen, UIGenSliderState *pState, UIGenSlider *pSlider)
{
	F32 fP;
	UIStyleBar *pBar = GET_REF(pState->hStyleBar);
	UIDirection eFillFrom = SAFE_MEMBER(pBar, eFillFrom);
	UITextureAssembly* pEmpty = pBar ? GET_REF(pBar->hEmpty) : NULL;
	int iMouseX, iMouseY;
	transformMousePos(g_ui_State.mouseX, g_ui_State.mouseY, &iMouseX, &iMouseY);

	if (pSlider->bDragMoveMode)
	{
		if (eFillFrom & UIHorizontal)
		{
			F32 fMouseDelta = (iMouseX-pState->iLastMousePosition)/(CBoxWidth(&pGen->ScreenBox) + ui_TextureAssemblyWidth(pEmpty));
			fP = pSlider->bValueInteractive ? pState->fValue : pState->fNotch;
			fP = CLAMPF32(fP + fMouseDelta, 0.0f, 1.0f); 
			pState->iLastMousePosition = iMouseX;
		}
		else
		{
			F32 fMouseDelta = (iMouseY-pState->iLastMousePosition)/(CBoxHeight(&pGen->ScreenBox) + ui_TextureAssemblyHeight(pEmpty));
			fP = pSlider->bValueInteractive ? pState->fValue : pState->fNotch;
			fP = CLAMPF32(fP - fMouseDelta, 0.0f, 1.0f);
			pState->iLastMousePosition = iMouseY;
		}
		return fP;
	}
	else
	{
		F32 fA = 0.f;
		F32 fB = 0.f;
		if (eFillFrom & UIHorizontal)
		{
			F32 fLow = pEmpty ? pGen->ScreenBox.lx + pEmpty->iPaddingLeft : pGen->ScreenBox.lx;
			F32 fHigh = pEmpty ? pGen->ScreenBox.hx - pEmpty->iPaddingRight : pGen->ScreenBox.hx;
			if ((eFillFrom & UIHorizontal) == UIHorizontal)
			{
				F32 fX = ABS(iMouseX - (fLow + fHigh)/2);
				fA = (fX / ((fHigh - fLow)/2));
			}
			else if (eFillFrom & UILeft)
			{
				F32 fX = CLAMP(iMouseX, fLow, fHigh) - fLow;
				fA = (fX / (fHigh - fLow));
			}
			else
			{
				F32 fX = fHigh - CLAMP(iMouseX, fLow, fHigh);
				fA = (fX / (fHigh - fLow));
			}
		}
		if (eFillFrom & UIVertical)
		{
			F32 fLow = pEmpty ? pGen->ScreenBox.ly + pEmpty->iPaddingTop : pGen->ScreenBox.ly;
			F32 fHigh = pEmpty ? pGen->ScreenBox.hy - pEmpty->iPaddingBottom : pGen->ScreenBox.hy;
			if ((eFillFrom & UIVertical) == UIVertical)
			{
				F32 fY = ABS((fLow + fHigh)/2 - iMouseY);
				fB = (fY / ((fHigh - fLow)/2));
			}
			else if (eFillFrom & UITop)
			{
				F32 fY = CLAMP(iMouseY, fLow, fHigh) - fLow;
				fB = (fY / (fHigh - fLow));
			}
			else
			{
				F32 fY = fHigh - CLAMP(iMouseY, fLow, fHigh);
				fB = (fY / (fHigh - fLow));
			}
		}
		return MAX(fA, fB);
	}
}

bool ui_IsMouseNearSliderPosition(UIGen *pGen, UIGenSliderState *pState, UIGenSlider *pSlider)
{
	const F32 fNearScreenPercent = 0.015f;
	F32 fP = pSlider->bValueInteractive ? pState->fValue : pState->fNotch;
	UIStyleBar *pBar = GET_REF(pState->hStyleBar);
	UIDirection eFillFrom = SAFE_MEMBER(pBar, eFillFrom);
	UITextureAssembly* pEmpty = pBar ? GET_REF(pBar->hEmpty) : NULL;
	int iMouseX, iMouseY;
	transformMousePos(g_ui_State.mouseX, g_ui_State.mouseY, &iMouseX, &iMouseY);

	if (eFillFrom & UIHorizontal)
	{
		F32 fLow = pEmpty ? pGen->ScreenBox.lx + pEmpty->iPaddingLeft : pGen->ScreenBox.lx;
		F32 fHigh = pEmpty ? pGen->ScreenBox.hx - pEmpty->iPaddingRight : pGen->ScreenBox.hx;
		float fX;
		if (eFillFrom == UILeft)
			fX = fLow + fP * (fHigh - fLow);
		else
			fX = fHigh - fP * (fHigh - fLow);
		return nearSameF32Tol(fX, iMouseX, g_ui_State.screenWidth*fNearScreenPercent);
	}
	else
	{
		F32 fLow = pEmpty ? pGen->ScreenBox.ly + pEmpty->iPaddingTop : pGen->ScreenBox.ly;
		F32 fHigh = pEmpty ? pGen->ScreenBox.hy - pEmpty->iPaddingBottom : pGen->ScreenBox.hy;
		float fY;
		if (eFillFrom == UITop)
			fY = fLow + fP * (fHigh - fLow);
		else
			fY = fHigh - fP * (fHigh - fLow);
		return nearSameF32Tol(fY, iMouseY, g_ui_State.screenHeight*fNearScreenPercent);
	}
}

void ui_GenBundleStyleBarGetStyleBar(UIGen *pGen, UIGenSliderState *pState, SA_PARAM_NN_VALID const UIGenBundleStyleBar *pBundle)
{
	if(pBundle->pBarExpr)
	{
		MultiVal mv = {0};
		char *result = NULL;
		ui_GenEvaluate(pGen, pBundle->pBarExpr, &mv);
		MultiValToEString(&mv, &result);
		REF_HANDLE_SET_FROM_STRING(g_ui_BarDict, result, pState->hStyleBar);
		estrDestroy(&result);
	}
	else
	{
		COPY_HANDLE(pState->hStyleBar, pBundle->hBar);
	}
}

void ui_GenUpdateSlider(UIGen *pGen)
{
	UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
	UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
	UIStyleBar *pBar;
	MultiVal mv;
	ui_GenBundleStyleBarGetStyleBar(pGen, pState, &pSlider->StyleBarBundle);
	pBar = GET_REF(pState->hStyleBar);

	if (pSlider->pMax)
	{
		ui_GenEvaluate(pGen, pSlider->pMax, &mv);
		pState->fMax = MultiValGetFloat(&mv, NULL);
		if (pState->fMax <= 0)
		{
			ErrorDetailsf("%f", pState->fMax);
			ErrorFilenamef(pGen->pchFilename, "%s: The Max expression of UIGenSlider should return a value greater than zero.", pGen->pchName);
			pState->fMax = UIGEN_SLIDER_DEFAULT_MAX;
		}
	}
	else
	{
		pState->fMax = UIGEN_SLIDER_DEFAULT_MAX;
	}
	if (pSlider->pValue)
	{
		ui_GenEvaluate(pGen, pSlider->pValue, &mv);
		pState->fValue = MultiValGetFloat(&mv, NULL);
	}
	if (pSlider->pNotch)
	{
		ui_GenEvaluate(pGen, pSlider->pNotch, &mv);
		pState->fNotch = MultiValGetFloat(&mv, NULL);
	}
	if (pSlider->pTickCountExpression)
	{
		ui_GenEvaluate(pGen, pSlider->pTickCountExpression, &mv);
		pState->iTickCount = MultiValGetInt(&mv, NULL);
	} 
	else
	{
		pState->iTickCount = pSlider->iTickCount;
	}
	if (ui_GenInState(pGen, kUIGenStateDragging))
	{
		if (!ui_GenInState(pGen, kUIGenStateLeftMouseDownAnywhere))
		{
			if (pSlider->pOnStoppedDragging)
				ui_GenRunAction(pGen, pSlider->pOnStoppedDragging);
			ui_GenUnsetState(pGen, kUIGenStateDragging);
		}
		else
		{
			F32 fP = ui_GetSliderPosition(pGen, pState, pSlider);
			if (pSlider->bValueInteractive)
				pState->fValue = fP * pState->fMax;
			if (pSlider->bNotchInteractive)
				pState->fNotch = fP * pState->fMax;
			if (pSlider->pOnChanged)
			{
				ui_GenRunAction(pGen, pSlider->pOnChanged);
			}
			ui_SoftwareCursorThisFrame();
		}
	}
	if (pSlider->bSnap)
	{
		pState->fNotch = round(pState->fNotch);
		pState->fValue = round(pState->fValue);
	}
	MIN1(pState->fValue, pState->fMax);
	MIN1(pState->fNotch, pState->fMax);

	ui_GenStates(pGen,
		kUIGenStateEmpty, pState->fValue == 0,
		kUIGenStateFilled, nearf(pState->fValue, pState->fMax),
		0);

	if (!pState->bInitialized)
	{
		pState->fDisplayValue = pState->fValue;
		pState->bInitialized = true;
	}

	if (pSlider->pSliderTween)
	{
		F32 fDelta = pState->fValue - pState->fDisplayValue;

		// Decrement the delay timer and set the delay flag
		if (pState->fDelay > 0)
		{
			pState->fDelay -= pGen->fTimeDelta;
		}
		else 
		{
			pState->fDelay = 0;
		}

		// Only apply the slider tween in the chosen direction
		if ((((pSlider->pSliderTween->eDirection & kUIStyleBarPositive) && (fDelta > 0.f))
			|| ((pSlider->pSliderTween->eDirection & kUIStyleBarNegative) && (fDelta < -0.f))))
		{
			// If no delay left, start the slider tween
			if (pState->fDelay <= 0)
			{
				if (pSlider->pSliderTween->eType == kUITweenLinear)
				{
					if (pState->fValue > pState->fDisplayValue)
						pState->fDisplayValue = MIN(pState->fDisplayValue + (pSlider->pSliderTween->fTweenSpeed * pGen->fTimeDelta * pState->fMax), pState->fValue);
					else
						pState->fDisplayValue = MAX(pState->fDisplayValue - (pSlider->pSliderTween->fTweenSpeed * pGen->fTimeDelta * pState->fMax), pState->fValue);
				}
				else if (pSlider->pSliderTween->eType == kUITweenEaseIn)
				{
					if (fDelta > 0.0)
						pState->fDisplayValue = MIN(pState->fDisplayValue + (fDelta * pSlider->pSliderTween->fTweenSpeed * pGen->fTimeDelta * pState->fMax), pState->fValue);
					else
						pState->fDisplayValue = MAX(pState->fDisplayValue + (fDelta * pSlider->pSliderTween->fTweenSpeed * pGen->fTimeDelta * pState->fMax), pState->fValue);
				}
				else
				{
					pState->fDisplayValue = pState->fValue;
				}
			}
		}
		else
		{
			pState->fDisplayValue = pState->fValue;
		}

		// If the change is zero and the delay timer is depleted reset it
		if (nearf(fDelta, 0) && (pState->fDelay <= 0) && pSlider->pSliderTween)
		{
			pState->fDelay = pSlider->pSliderTween->fDelay;
		}

		// Decide the alpha of the moving overlay based on the direction of the delta
		if (pBar && GET_REF(pBar->hDynamicOverlay))
		{
			if ((pBar->eMovingOverlayDirection & kUIStyleBarPositive) && (fDelta > 0.001f)
				|| (pBar->eMovingOverlayDirection & kUIStyleBarNegative) && (fDelta < -0.001f))
			{
				if(pBar->fMovingOverlayFadeIn){
					pState->fMovingOverlayAlpha += (pGen->fTimeDelta / pBar->fMovingOverlayFadeIn);
				}
			}
			else if(pBar->fMovingOverlayFadeOut)
			{
				pState->fMovingOverlayAlpha -= (pGen->fTimeDelta / pBar->fMovingOverlayFadeOut);
			}
			pState->fMovingOverlayAlpha = CLAMPF32(pState->fMovingOverlayAlpha, 0.0f, 1.0f);
		}
	}
	else
	{
		pState->fDisplayValue = pState->fValue;
	}
	pState->fDisplayValue = CLAMP(pState->fDisplayValue, 0, pState->fMax);
}

void ui_GenTickEarlySlider(UIGen *pGen)
{
	UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
	UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
	UIStyleBar *pBar = GET_REF(pState->hStyleBar);
	bool bChanged = false;

	if (!ui_GenInState(pGen, kUIGenStateDragging))
	{
		if (ui_GenInState(pGen, kUIGenStateLeftMouseDown))
		{
			if (!pSlider->bDragMoveMode || 
				(!pState->bHandledMouseDown && ui_IsMouseNearSliderPosition(pGen, pState, pSlider)))
			{
				int iMouseX, iMouseY;
				transformMousePos(g_ui_State.mouseX, g_ui_State.mouseY, &iMouseX, &iMouseY);
				ui_GenSetState(pGen, kUIGenStateDragging);

				if (pSlider->bDragMoveMode)
				{
					pState->iLastMousePosition = (pBar->eFillFrom & UIHorizontal) ? iMouseX : iMouseY;
					pState->bHandledMouseDown = true;
				}
			}
		}
		else
		{
			pState->bHandledMouseDown = false;
		}
	}
}

void ui_GenDrawEarlySlider(UIGen *pGen)
{
	UIGenSlider *pSlider = UI_GEN_RESULT(pGen, Slider);
	UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
	UIStyleBar *pBar = pSlider->pInlineBar ? pSlider->pInlineBar : GET_REF(pState->hStyleBar);
	F32 fWidth = CBoxWidth(&pGen->ScreenBox) - (pSlider->chRows * pSlider->fRowHorizontalOffset * pGen->fScale);
	F32 fHeight = CBoxHeight(&pGen->ScreenBox) / pSlider->chRows;
	F32 fValuePer = pState->fMax / pSlider->chRows;
	S32 i;
	
	for (i = 0; i < pSlider->chRows; i++)
	{
		CBox BarBox;
		F32 fBase = (pSlider->chRows - (i + 1)) * fValuePer;
		F32 fValue = (pState->fDisplayValue - fBase) / fValuePer;
		F32 fNotch = 0;
		BuildCBox(&BarBox,
			pGen->ScreenBox.lx + i * fWidth,
			pGen->ScreenBox.ly + pSlider->fRowHorizontalOffset * pGen->fScale * (pSlider->chRows-i-1),
			fWidth,
			fHeight);

		if (pState->fNotch > fBase && pState->fNotch <= fBase + fValuePer)
			fNotch = (pState->fNotch - fBase) / fValuePer;
		fValue = CLAMP(fValue, 0, 1);
		ui_StyleBarDraw(pBar, &BarBox, fValue, fNotch, pState->iTickCount, pState->fMovingOverlayAlpha, NULL, UI_GET_Z(), pGen->chAlpha, true, pGen->fScale, GenSpritePropGetCurrent());
	}
}

void ui_GenFitContentsSizeSlider(UIGen *pGen, UIGenSlider *pSlider, CBox *pOut)
{
	UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
	UIStyleBar *pBar = pSlider->pInlineBar ? pSlider->pInlineBar : GET_REF(pState->hStyleBar);
	BuildCBox(pOut, 0, 0, ui_StyleBarGetLeftPad(pBar) + ui_StyleBarGetRightPad(pBar), ui_StyleBarGetHeight(pBar) * pSlider->chRows);
}

void ui_GenHideSlider(UIGen *pGen)
{
	UIGenSliderState *pState = UI_GEN_STATE(pGen, Slider);
	if (pState)
	{
		pState->bInitialized = false;
	}
}

AUTO_RUN;
void ui_GenRegisterSlider(void)
{
	ui_GenRegisterType(kUIGenTypeSlider, 
		ui_GenValidateSlider,
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateSlider, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlySlider, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlySlider,
		ui_GenFitContentsSizeSlider, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideSlider, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenSlider_h_ast.c"
