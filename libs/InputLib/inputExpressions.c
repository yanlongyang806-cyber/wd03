#include "Expression.h"
#include "cmdparse.h"
#include "inputLib.h"
#include "inputMouse.h"
#include "inputKeyBind.h"
#include "inputGamepad.h"
#include "inputLibEnums_h_ast.h"
#include "StringCache.h"
#include "Message.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define AXIS_SNAP(a, b) ((fabs(a) < 0.1 || (fabs(b) >= 0.5 && fabs(b) > fabs(a))) ? 0 : (a))

static const char *s_pchMouseX = NULL;
static const char *s_pchMouseY = NULL;
static const char *s_pchMouseZ = NULL;
static const char *s_pchLeftStickX = NULL;
static const char *s_pchLeftStickY = NULL;
static const char *s_pchRightStickX = NULL;
static const char *s_pchRightStickY = NULL;
static const char *s_pchLeftStickSnapX = NULL;
static const char *s_pchLeftStickSnapY = NULL;
static const char *s_pchLeftTrigger = NULL;
static const char *s_pchRightTrigger= NULL;

AUTO_RUN;
void inpExprInitStaticStrings(void)
{
	s_pchMouseX = allocAddStaticString("MouseX");
	s_pchMouseY = allocAddStaticString("MouseY");
	s_pchMouseZ = allocAddStaticString("MouseZ");
	s_pchLeftStickX = allocAddStaticString("LeftStickX");
	s_pchLeftStickY = allocAddStaticString("LeftStickY");
	s_pchRightStickX = allocAddStaticString("RightStickX");
	s_pchRightStickY = allocAddStaticString("RightStickY");
	s_pchLeftStickSnapX = allocAddStaticString("LeftStickSnapX");
	s_pchLeftStickSnapY = allocAddStaticString("LeftStickSnapY");
	s_pchLeftTrigger = allocAddStaticString("LeftTrigger");
	s_pchRightTrigger= allocAddStaticString("RightTrigger");
}

void inpUpdateExpressionContext(ExprContext *pContext, bool bAddConstVariables)
{
	S32 iX = 0, iY = 0;
	S32 iZ = mouseZ();
	F32 fLeftX;
	F32 fLeftY;
	F32 fRightX;
	F32 fRightY;
	F32 fLeftTrigger;
	F32 fRightTrigger;

	mousePos(&iX, &iY);
	gamepadGetLeftStick(&fLeftX, &fLeftY);
	gamepadGetRightStick(&fRightX, &fRightY);
	gamepadGetTriggerValues(&fLeftTrigger, &fRightTrigger);

	exprContextSetIntVarPooledCached(pContext, s_pchMouseX, iX, NULL);
	exprContextSetIntVarPooledCached(pContext, s_pchMouseY, iY, NULL);
	exprContextSetIntVarPooledCached(pContext, s_pchMouseZ, iZ, NULL);
	exprContextSetFloatVarPooledCached(pContext, s_pchLeftStickX, AXIS_SNAP(fLeftX, 0), NULL);
	exprContextSetFloatVarPooledCached(pContext, s_pchLeftStickY, AXIS_SNAP(fLeftY, 0), NULL);
	exprContextSetFloatVarPooledCached(pContext, s_pchRightStickX, AXIS_SNAP(fRightX, 0), NULL);
	exprContextSetFloatVarPooledCached(pContext, s_pchRightStickY, AXIS_SNAP(fRightY, 0), NULL);
	exprContextSetFloatVarPooledCached(pContext, s_pchLeftStickSnapX, AXIS_SNAP(fLeftX, fLeftY), NULL);
	exprContextSetFloatVarPooledCached(pContext, s_pchLeftStickSnapY, AXIS_SNAP(fLeftY, fLeftX), NULL);

	exprContextSetFloatVarPooledCached(pContext, s_pchLeftTrigger, fLeftTrigger, NULL);
	exprContextSetFloatVarPooledCached(pContext, s_pchRightTrigger, fRightTrigger, NULL);

	if (bAddConstVariables)
	{
		S32 i;
		S32 iKeyArraySize = keybind_GetKeyArraySize();
		for (i = 0; i < iKeyArraySize; i++)
		{
			S32 iKey = keybind_GetKeyCodeByIndex(i);
			const char *pchKey = keybind_GetKeyNameByIndex(i);
			if (pchKey)
			{
				char achPrefixedKey[100];
				sprintf(achPrefixedKey, "K_%s", pchKey);
				exprContextSetIntVar(pContext, achPrefixedKey, iKey);
			}
		}
		exprContextAddStaticDefineIntAsVars(pContext, MouseButtonEnum, "");
	}
}

// Returns true if the player is holding down the given key.
// Keys are given as scancode integers, or the K_* variables, e.g. K_A, K_F4, K_DOWNARROW.
//NO LONGER WORKS FOR NORMAL KEYS 1, A, [, etc.
//TODO - Rename Expression
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputKeyState");
bool inpExprKeyState(ExprContext *pContext, S32 iKey)
{
	return inpLevelPeek(iKey);
}

// Returns true if the player pressed the given key this frame.
// Keys are given as scancode integers, or the K_* variables, e.g. K_A, K_F4, K_DOWNARROW.
//NO LONGER WORKS FOR NORMAL KEYS 1, A, [, etc.
//TODO - Rename Expression
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputKeyEdge");
bool inpExprKeyEdge(ExprContext *pContext, S32 iKey)
{
	return inpEdgePeek(iKey);
}

// Returns true if the player has a gamepad plugged in.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputHasGamepad");
bool inpExprHasGamepad(ExprContext *pContext, S32 iKey)
{
	return inpHasGamepad();
}

// Returns true if the player has a keyboard plugged in.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputHasKeyboard");
bool inpExprHasKeyboard(ExprContext *pContext, S32 iKey)
{
	return inpHasKeyboard();
}

// Returns true if the player has a mouse plugged in.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputHasMouse");
bool inpExprHasMouse(ExprContext *pContext, S32 iKey)
{
	return inpHasMouse();
}

// Push a keybind profile onto the stack.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputPushKeybinds");
void inpExprPushKeybinds(ExprContext *pContext, const char *pchKeybind)
{
	keybind_PushProfileName(pchKeybind);
}

// Remove a keybind profile from the stack.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputPopKeybinds");
void inpExprPopKeybinds(ExprContext *pContext, const char *pchKeybind)
{
	keybind_PopProfileName(pchKeybind);
}

// Return the display name of a key string.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TranslateKeyString");
const char *inpExprTranslateKeyString(ExprContext *pContext, const char *pchKeyString, bool bAbbreviate)
{
	return keybind_GetDisplayName(pchKeyString, bAbbreviate);
}


static void ExprShowVirtualKeyboardCB(const char *pchResult, bool bAccepted, char *pchCommandBase)
{
	if (bAccepted)
	{
		char *pchCommand = NULL;
		estrStackCreate(&pchCommand);
		estrPrintf(&pchCommand, "%s %s", pchCommandBase, pchResult);
		globCmdParse(pchCommand);
		estrDestroy(&pchCommand);
	}
	free(pchCommandBase);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputShowVirtualKeyboard");
void inpExprShowVirtualKeyboard(const char *pchTitle, const char *pchPrompt, const char *pchDefault, const char *pchCommand)
{
	inpShowVirtualKeyboard("Default", pchTitle, pchPrompt, ExprShowVirtualKeyboardCB, strdup(pchCommand), pchDefault, 256);
}

// Rumble the gamepad for fDuration seconds at the given intensities.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InputRumbleGamepad");
void inpExprRumbleGamepad(S32 iLeftMotor, S32 iRightMotor, F32 fDuration)
{
	gamepadRumble(iLeftMotor, iRightMotor, fDuration * 1000.f);
}

#include "inputLibEnums_h_ast.c"
