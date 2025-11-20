#include "GlobalStateMachine.h"
#include "Combat/ClientTargeting.h"
#include "gclBaseStates.h"
#include "gclEntity.h"
#include "Character.h"
#include "UICore.h"
#include "GameClientLib.h"
#include "estring.h"
#include "GameStringFormat.h"
#include "CombatConfig.h"
#include "gclCamera.h"
#include "gclControlScheme.h"
#include "gclCursorMode.h"
#include "NotifyCommon.h"
#include "gclPlayerControl.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define GCL_MENU_DELAY 0.75

// If something is targeted, clear the target.
// If nothing is targeted, bring up the main menu.
void ClearTargetOrBringUpMenuHelper(bool bCheckMouseLook)
{
	Entity *pEnt = entActivePlayerPtr();
	static S64 s_ulLocalAbsTime = 0;
	bool bDidSomething = true;
	
	//Don't do anything without an active entity pointer
	if (!(pEnt && GSM_IsStateActive(GCL_GAMEPLAY)))
		return;
	else if (!gclCursorMode_IsDefault())
	{
		gclCursorMode_ChangeToDefault();
	}
	else if (!g_CurrentScheme.bMouseLookHardTarget &&
		(pEnt->pChar->currentTargetRef || IS_HANDLE_ACTIVE(pEnt->pChar->currentTargetHandle) || pEnt->pChar->erTargetDual))
	{
		clientTarget_Clear();
	}
	else if (gclPlayerControl_IsMouseLooking() && g_CurrentScheme.bShowMouseLookReticle && bCheckMouseLook)
	{
		gclPlayerControl_UpdateMouseInput(false, false);
		gclCamera_OnMouseLook(false);
	}
	//The fall through, toggle on/off the menu
	else
	{
		// I didn't do nothing, don't update the timestamp (s_ulLocalAbsTime)
		bDidSomething = false;
		if (GSM_IsStateActive(GCL_GAME_MENU))
		{
			GSM_SwitchToState_Complex(GCL_GAMEPLAY);
		}
		else if(ABS_TIME_TO_SEC(ABS_TIME_SINCE(s_ulLocalAbsTime)) > GCL_MENU_DELAY )
		{
			if (bCheckMouseLook)
				gclCamera_DisableMouseLook();
			GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_GAME_MENU);
		}
	}

	//If I did something (untargeted an enemy, etc) then update the local timestamp
	if(bDidSomething)
		s_ulLocalAbsTime = ulAbsTime;
}

// If something is targeted, clear the target, If nothing is targeted, bring up the main menu.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void ClearTargetOrBringUpMenu(void)
{
	ClearTargetOrBringUpMenuHelper(1);	
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void ClearTargetOrBringUpMenuIgnoreMouseLook(void)
{
	ClearTargetOrBringUpMenuHelper(0);	
}

// Opens the game pause menu
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void GameMenu(bool bShow)
{
	//for instance, we click "log out" on the menu, then try to close the menu. Closing the menu
	//will overwrite the logging out.
	if (GSM_AreAnyStateChangesRequested())
	{
		return;
	}

	if (!(entActivePlayerPtr() && GSM_IsStateActive(GCL_GAMEPLAY)))
		return;
	else if (!bShow && GSM_IsStateActive(GCL_GAME_MENU))
	{
		GSM_SwitchToState_Complex(GCL_GAMEPLAY);
	}
	else if (bShow && !GSM_IsStateActive(GCL_GAME_MENU))
	{
		GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_GAME_MENU);
	}
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void ui_FindUnusedAssets(const char *pchDictionary)
{
	RefDictIterator iter;
	char *pchRefData;
	RefSystem_InitRefDictIterator(pchDictionary, &iter);
	while (pchRefData = RefSystem_GetNextReferenceDataFromIterator(&iter))
	{
		void *pReferent = RefSystem_ReferentFromString(pchDictionary, pchRefData);
		if (pReferent && RefSystem_GetReferenceCountForReferent(pReferent) == 0)
			printf("%s: %s\n", pchDictionary, pchRefData);
	}

}

// Respond "OK" to an open dialog box; may not work in all dialogs.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void uiOK(void)
{
	// This doesn't actually do anything - gens listen for
	// KeyAction:UIOK which the user has bound, and running
	// the command doesn't really do anything.
}

// Respond "Cancel" to an open dialog box; may not work in all dialogs.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void uiCancel(void)
{
	// This doesn't actually do anything - gens listen for
	// KeyAction:UICancel which the user has bound, and running
	// the command doesn't really do anything.
}
