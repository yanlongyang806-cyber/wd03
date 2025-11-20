#include "earray.h"
#include "Error.h"
#include "Expression.h"
#include "Message.h"
#include "UIDialog.h"

#include "inputKeyBind.h"
#include "inputGamepad.h"
#include "NotifyCommon.h"

#include "UIGen.h"

#include "NotifyEnum_h_ast.h"
#include "gclDialogBox_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct ClientDialogBox
{
	NotifyType eType;
	char *pchTitle;
	char *pchBody;
	char *pchOK;
	char *pchCancel;
	REF_TO(UIGen) hTarget;
	bool bCancelDialog;
} ClientDialogBox;

static ClientDialogBox **s_eaPendingDialogs;
static bool s_bShowingModalDialog = false;

bool GameDialogPopup(void);


bool GameUIDialogOKCB(UIDialog *pDialog, UIDialogButton eButton, UserData pResponseData)
{
	// Clean up this dialog so the next can show
	ClientDialogBox *pBox = eaRemove(&s_eaPendingDialogs, 0);
	StructDestroy(parse_ClientDialogBox, pBox);
	s_bShowingModalDialog = false;

	// Show next dialog if required
	GameDialogPopup();

	return true;
}


bool GameDialogPopup(void)
{
	if (eaSize(&s_eaPendingDialogs) > 0)
	{
		if (g_ui_State.bInUGCEditor)
		{
			if (!s_bShowingModalDialog)
			{
				// In UGC Editor need such dialogs to show up in UI2Lib, not UIGen
				ClientDialogBox *pBox = s_eaPendingDialogs[0];
				UIDialog *pDialog = ui_DialogCreateEx(pBox->pchTitle, pBox->pchBody, GameUIDialogOKCB, NULL, NULL, "OK", kUIDialogButton_Ok, NULL);
				ui_WindowShow(UI_WINDOW(pDialog));
				s_bShowingModalDialog = true;
			}
		}
		else
		{
			UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, "ModalDialog_Root");
			if (devassertmsg(pGen, "ModalDialog_Root gen not found, unable to display a window!"))
				ui_GenAddModalPopup(pGen);
			return true;
		}
	}
	return false;
}

static ClientDialogBox *CreateClientDialogBox(const char *pchTitle, const char *pchDisplay, const char *pchOK, const char *pchCancel)
{
	ClientDialogBox *pNewBox = StructCreate(parse_ClientDialogBox);

	pNewBox->pchTitle = StructAllocString(pchTitle);
	pNewBox->pchBody = StructAllocString(pchDisplay);
	pNewBox->pchOK = StructAllocString(pchOK);
	pNewBox->pchCancel = StructAllocString(pchCancel);

	return pNewBox;
}

static void DialogBoxAdd(ClientDialogBox *pDialog)
{
	eaPush(&s_eaPendingDialogs, pDialog);
	GameDialogPopup();	
}

// Confirm this dialog and display the next; return false if none remain.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDialogOK);
S32 gclGenDialogOK(void)
{
	ClientDialogBox *pBox = eaRemove(&s_eaPendingDialogs, 0);
	if (pBox && GET_REF(pBox->hTarget))
		ui_GenSendMessage(GET_REF(pBox->hTarget), "DialogOK");
	StructDestroy(parse_ClientDialogBox, pBox);
	return eaSize(&s_eaPendingDialogs);
}

// Cancel this dialog and display the next; return false if none remain.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDialogCancel);
S32 gclGenDialogCancel(void)
{
	ClientDialogBox *pBox = eaRemove(&s_eaPendingDialogs, 0);
	if (pBox && GET_REF(pBox->hTarget))
		ui_GenSendMessage(GET_REF(pBox->hTarget), pBox->bCancelDialog ? "DialogCancel" : "DialogOK");
	StructDestroy(parse_ClientDialogBox, pBox);
	return eaSize(&s_eaPendingDialogs);
}

static S32 GenAddDialog(ExprContext *pContext, 
						const char *pchTitle, const char *pchBody, const char *pchOK, const char *pchCancel, 
						SA_PARAM_OP_VALID UIGen *pTarget, bool bCancel)
{
	ClientDialogBox *pBox = CreateClientDialogBox(pchTitle, pchBody, pchOK, pchCancel);
	pBox->bCancelDialog = bCancel;
	if (!pTarget|| ui_GenInDictionary(pTarget))
		SET_HANDLE_FROM_REFERENT(UI_GEN_DICTIONARY, pTarget, pBox->hTarget);
	else
		ErrorFilenamef(exprContextGetBlameFile(pContext), "%s: Only gens in the dictionary can receive dialog events.", pTarget->pchName);
	DialogBoxAdd(pBox);
	return eaSize(&s_eaPendingDialogs);
}

// Add a new simple dialog. A simple dialog has some text and an OK button,
// and sends an "OK" message to the target gen when closed.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddSimpleDialog);
S32 gclGenAddSimpleDialog(ExprContext *pContext, const char *pchTitle, const char *pchBody, SA_PARAM_OP_VALID UIGen *pTarget)
{
	return GenAddDialog(pContext, pchTitle, pchBody, NULL, NULL, pTarget, false);
}

// Add a new simple dialog. A simple dialog has some text and an OK button,
// and sends an "OK" message to the target gen when closed.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddCustomSimpleDialog);
S32 gclGenAddCustomSimpleDialog(ExprContext *pContext, const char *pchTitle, const char *pchBody, const char *pchOK, SA_PARAM_OP_VALID UIGen *pTarget)
{
	return GenAddDialog(pContext, pchTitle, pchBody, pchOK, NULL, pTarget, false);
}

// Add a new confirmation dialog. A confirmation dialog has an OK button,
// a cancel button, and sends an OK or Cancel message to the target gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddConfirmDialog);
S32 gclGenAddConfirmDialog(ExprContext *pContext, const char *pchTitle, const char *pchBody, SA_PARAM_OP_VALID UIGen *pTarget)
{
	return GenAddDialog(pContext, pchTitle, pchBody, NULL, NULL, pTarget, true);
}

// Add a new confirmation dialog. A confirmation dialog has an OK button,
// a cancel button, and sends an OK or Cancel message to the target gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenAddCustomConfirmDialog);
S32 gclGenAddCustomConfirmDialog(ExprContext *pContext, const char *pchTitle, const char *pchBody, const char *pchOK, const char *pchCancel, SA_PARAM_OP_VALID UIGen *pTarget)
{
	return GenAddDialog(pContext, pchTitle, pchBody, pchOK, pchCancel, pTarget, true);
}

// Return the body for the current simple dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDialogIsConfirm);
bool gclGenDialogIsConfirm(void)
{
	ClientDialogBox *pBox = eaGet(&s_eaPendingDialogs, 0);
	return pBox ? pBox->bCancelDialog : 0;
}

// Return the body for the current simple dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDialogIsCustom);
bool gclGenDialogIsCustom(void)
{
	ClientDialogBox *pBox = eaGet(&s_eaPendingDialogs, 0);
	return pBox ? !!pBox->pchOK : 0;
}

// Return the body for the current simple dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDialogIsPending);
bool gclGenDialogIsPending(void)
{
	return eaSize(&s_eaPendingDialogs) > 0;
}

// Return the body for the current simple dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSimpleDialogBody);
const char *gclGenSimpleDialogBody(void)
{
	ClientDialogBox *pBox = eaGet(&s_eaPendingDialogs, 0);
	return pBox ? pBox->pchBody : "";
}

// Return the title for the current simple dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSimpleDialogTitle);
const char *gclGenSimpleDialogTitle(void)
{
	ClientDialogBox *pBox = eaGet(&s_eaPendingDialogs, 0);
	return pBox ? pBox->pchTitle : "";
}

// Return the body for the current simple dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSimpleDialogOK);
const char *gclGenSimpleDialogOK(void)
{
	ClientDialogBox *pBox = eaGet(&s_eaPendingDialogs, 0);
	return pBox ? pBox->pchOK : "";
}

// Return the title for the current simple dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSimpleDialogCancel);
const char *gclGenSimpleDialogCancel(void)
{
	ClientDialogBox *pBox = eaGet(&s_eaPendingDialogs, 0);
	return pBox ? pBox->pchCancel : "";
}

// Receive a broadcast message from the server.
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void GameDialogGenericMessage(const char *pchTitle, const ACMD_SENTENCE pchBody)
{
	DialogBoxAdd(CreateClientDialogBox(pchTitle, pchBody, NULL, NULL));
}

// Receive a broadcast message from the server.
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void GameDialogTyped(NotifyType eType, const char *pchTitle, const ACMD_SENTENCE pchBody)
{
	ClientDialogBox *pBox = CreateClientDialogBox(pchTitle, pchBody, NULL, NULL);
	pBox->eType = eType;
	DialogBoxAdd(pBox);
}

// Remove all dialog boxes of a given type.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_HIDE;
void GameDialogClearType(NotifyType eType)
{
	S32 i;
	for (i = eaSize(&s_eaPendingDialogs) - 1; i >= 0; i--)
		if (s_eaPendingDialogs[i]->eType == eType)
			StructDestroy(parse_ClientDialogBox, eaRemove(&s_eaPendingDialogs, i));
}

// Test confirm dialogs.
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void GameConfirmDialogTest(const char *pchTitle, const ACMD_SENTENCE pchBody)
{
	ClientDialogBox *pBox = CreateClientDialogBox(pchTitle, pchBody, NULL, NULL);
	pBox->bCancelDialog = true;
	DialogBoxAdd(pBox);
}

#include "gclDialogBox_c_ast.c"
