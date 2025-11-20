/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "contact_common.h"
#include "contacteditor.h"
#include "gameeditorshared.h"
#include "error.h"
#include "file.h"
#include "GameClientLib.h"
#include "AuctionBrokerCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_ContactPicker = {0};

EMEditor s_ContactEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static bool contactEditorEMShouldRevertDoc(EMEditorDoc *pDoc)
{
	ContactEditDoc *pContactDoc = (ContactEditDoc *)pDoc;
	if (pContactDoc->pOrigContactUntouched)
	{
		ContactDef *pNewContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pDoc->orig_doc_name);
		if (StructCompare(parse_ContactDef, pNewContactDef, pContactDoc->pOrigContactUntouched, 0, 0, TOK_USEROPTIONBIT_3) == 0)
		{
			// Only TOK_USEROPTIONBIT_3 fields have changed (overrides)
			// Fix up overrides only
			eaDestroyStruct(&pContactDoc->pOrigContactUntouched->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
			eaCopyStructs(&pNewContactDef->eaSpecialDialogOverrides, &pContactDoc->pOrigContactUntouched->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
			eaDestroyStruct(&pContactDoc->pOrigContactUntouched->eaOfferOverrides, parse_ContactMissionOffer);
			eaCopyStructs(&pNewContactDef->eaOfferOverrides, &pContactDoc->pOrigContactUntouched->eaOfferOverrides, parse_ContactMissionOffer);

			//These could be NULL and therefore require checking
			if (pContactDoc->pOrigContactUntouched->pImageMenuData)
			{
				if (pNewContactDef->pImageMenuData)
				{
					eaDestroyStruct(&pContactDoc->pOrigContactUntouched->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
				}
				eaCopyStructs(&pNewContactDef->pImageMenuData->itemOverrides, &pContactDoc->pOrigContactUntouched->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
			}

			eaDestroyStruct(&pContactDoc->pOrigContact->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
			eaCopyStructs(&pNewContactDef->eaSpecialDialogOverrides, &pContactDoc->pOrigContact->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
			eaDestroyStruct(&pContactDoc->pOrigContact->eaOfferOverrides, parse_ContactMissionOffer);
			eaCopyStructs(&pNewContactDef->eaOfferOverrides, &pContactDoc->pOrigContact->eaOfferOverrides, parse_ContactMissionOffer);

			//These could be NULL and therefore require checking
			if (pNewContactDef->pImageMenuData)
			{
				if (pContactDoc->pOrigContact->pImageMenuData)
				{
					eaDestroyStruct(&pContactDoc->pOrigContact->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
				}
				eaCopyStructs(&pNewContactDef->pImageMenuData->itemOverrides, &pContactDoc->pOrigContact->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
			}

			eaDestroyStruct(&pContactDoc->pContact->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
			eaCopyStructs(&pNewContactDef->eaSpecialDialogOverrides, &pContactDoc->pContact->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
			eaDestroyStruct(&pContactDoc->pContact->eaOfferOverrides, parse_ContactMissionOffer);
			eaCopyStructs(&pNewContactDef->eaOfferOverrides, &pContactDoc->pContact->eaOfferOverrides, parse_ContactMissionOffer);

			//These could be NULL and therefore require checking
			if (pNewContactDef->pImageMenuData)
			{
				if (pContactDoc->pContact->pImageMenuData)
				{
					eaDestroyStruct(&pContactDoc->pContact->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
				}
				eaCopyStructs(&pNewContactDef->pImageMenuData->itemOverrides, &pContactDoc->pContact->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
			}

			if (pContactDoc->pNextUndoContact)
			{
				eaDestroyStruct(&pContactDoc->pNextUndoContact->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
				eaCopyStructs(&pNewContactDef->eaSpecialDialogOverrides, &pContactDoc->pNextUndoContact->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
				eaDestroyStruct(&pContactDoc->pNextUndoContact->eaOfferOverrides, parse_ContactMissionOffer);
				eaCopyStructs(&pNewContactDef->eaOfferOverrides, &pContactDoc->pNextUndoContact->eaOfferOverrides, parse_ContactMissionOffer);

				//These could be NULL and therefore require checking
				if (pNewContactDef->pImageMenuData)
				{
					if (pContactDoc->pNextUndoContact->pImageMenuData)
					{
						eaDestroyStruct(&pContactDoc->pNextUndoContact->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
					}
					eaCopyStructs(&pNewContactDef->pImageMenuData->itemOverrides, &pContactDoc->pNextUndoContact->pImageMenuData->itemOverrides, parse_ContactImageMenuItem);
				}
			}

			CEUpdateDisplay(pContactDoc);

			return false;
		}
	}	

	return true;
}

static EMEditorDoc *contactEditorEMLoadDoc(const char *pcNameToOpen, const char *pcType)
{
	ContactEditDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the contact
	pDoc = CEOpenContact(&s_ContactEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pContact->name;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "contact");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	// Initialize the dialog flow window
	pDoc->pDialogFlowWindowInfo = CEInitDialogFlowWindowWithContactDoc(pDoc);
	CERefreshDialogFlowWindow(pDoc->pDialogFlowWindowInfo);

	return &pDoc->emDoc;
}


static EMEditorDoc *contactEditorEMNewDoc(const char *pcType, void *data)
{
	return contactEditorEMLoadDoc(NULL, pcType);
}


static void contactEditorEMReloadDoc(EMEditorDoc *pDoc)
{
	CERevertContact((ContactEditDoc*)pDoc);
}


static void contactEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	CECloseContact((ContactEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus contactEditorEMSave(EMEditorDoc *pDoc)
{
	return CESaveContact((ContactEditDoc*)pDoc, false);
}


static EMTaskStatus contactEditorEMSaveAs(EMEditorDoc *pDoc)
{
	return CESaveContact((ContactEditDoc*)pDoc, true);
}


static void contactEditorEMEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_ContactDictionary, true);
	resSetDictionaryEditMode(gMessageDict, true);
	resSetDictionaryEditMode("PetContactList", true);
	GERefreshMapNamesList();
}

static void contactEditorEMExit(EMEditor *pEditor)
{

}


static void contactEditorEMInit(EMEditor *pEditor)
{
	CEInitData(pEditor);
}

#endif

AUTO_RUN_LATE;
int contactEditorEMRegister(void)
{
#ifndef NO_EDITORS
	static bool bInited = false;
	if (!areEditorsAllowed() && areEditorsPossible())
	{
		if (!gGCLState.bRunning)
			gclRegisterEditChangeCallback(contactEditorEMRegister);

		return 0;
	}
	
	if (bInited)
	{
		return 0;
	}
	bInited = true;

	// Register the editor
	strcpy(s_ContactEditor.editor_name, CONTACT_EDITOR);
	s_ContactEditor.type = EM_TYPE_MULTIDOC;
	s_ContactEditor.allow_save = 1;
	s_ContactEditor.allow_multiple_docs = 1;
	s_ContactEditor.hide_world = 1;
	s_ContactEditor.disable_auto_checkout = 1;
	s_ContactEditor.default_type = "Contact";
	strcpy(s_ContactEditor.default_workspace, "Game Design Editors");

	s_ContactEditor.init_func = contactEditorEMInit;
	s_ContactEditor.enter_editor_func = contactEditorEMEnter;
	s_ContactEditor.exit_func = contactEditorEMExit;
	s_ContactEditor.new_func = contactEditorEMNewDoc;
	s_ContactEditor.load_func = contactEditorEMLoadDoc;
	s_ContactEditor.reload_func = contactEditorEMReloadDoc;
	s_ContactEditor.close_func = contactEditorEMCloseDoc;
	s_ContactEditor.save_func = contactEditorEMSave;
	s_ContactEditor.save_as_func = contactEditorEMSaveAs;
	s_ContactEditor.should_revert_func = contactEditorEMShouldRevertDoc;

	s_ContactEditor.keybinds_name = "ContactEditor";
	s_ContactEditor.keybind_version = 1;

	// Register the picker
	s_ContactPicker.allow_outsource = 1;
	strcpy(s_ContactPicker.picker_name, "Contact Library");
	strcpy(s_ContactPicker.default_type, s_ContactEditor.default_type);
	emPickerManage(&s_ContactPicker);
	eaPush(&s_ContactEditor.pickers, &s_ContactPicker);
	emRegisterEditor(&s_ContactEditor);

	emRegisterFileType(s_ContactEditor.default_type, "Contact", s_ContactEditor.editor_name);
#endif

	return 0;
}
