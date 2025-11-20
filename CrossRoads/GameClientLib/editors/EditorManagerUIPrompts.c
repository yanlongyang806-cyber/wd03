/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "EditorManagerPrivate.h"
#include "Message.h"
#include "GraphicsLib.h"
#include "smf_render.h"
#include "inputLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/********************
* GLOBALS
********************/

static UIWindow *prompt_window = NULL;


/******
* FORWARD DECLARATIONS
******/
static void emStartPrompt(void);
static void emFinishPrompt(void);

/********************
* Helpers
********************/

static void emPromptEntryDestroy(EMPromptEntry *entry)
{
	StructFreeStringSafe(&entry->custom_message);
	free(entry);
}

/********************
* UI CALLBACKS
********************/

static void emPromptUIDismiss(void)
{
	if (prompt_window) {
		ui_WindowHide(prompt_window);
		ui_WidgetQueueFree(UI_WIDGET(prompt_window));
		prompt_window = NULL;
	}
}


static void emPromptUIRevert(UIButton *button, EMPromptEntry *entry)
{
	// Cause document reload
	if (entry->doc && entry->sub_doc) 
	{
		emReloadSubDoc(entry->doc, entry->sub_doc);
	} 
	else if (entry->doc) 
	{
		emReloadDoc(entry->doc);
	}

	emPromptUIDismiss();
	emFinishPrompt();
}


static void emPromptUICheckout(UIButton *button, EMPromptEntry *entry)
{
	if (entry->doc && entry->dict && entry->res_name) 
	{
		void *obj = RefSystem_ReferentFromString( entry->dict, entry->res_name );

		// Set the state to locking so we monitor for lock completion
		emSetResourceState(entry->doc->editor, entry->res_name, EMRES_STATE_LOCKING);

		// Send off the lock/checkout request
		resSetDictionaryEditMode(entry->dict, true);
		resSetDictionaryEditMode(gMessageDict, true);
		resRequestLockResource(entry->dict,  entry->res_name, obj);
	}
	emPromptUIDismiss();
	emFinishPrompt();
}


static void emPromptUICancel(UIButton *button, EMPromptEntry *entry)
{
	// On cancel, just do cleanup
	emPromptUIDismiss();
	emFinishPrompt();
}


static void emPromptUISaveOverwrite(UIButton *pButton, EMPromptEntry *entry)
{
	if (entry->doc)
	{
		entry->doc->smart_save_rename = false;
		entry->doc->smart_save_overwrite = true;
		emSaveDocAs(entry->doc);
	}
	emPromptUIDismiss();
	emFinishPrompt();
}


static void emPromptUISaveRename(UIButton *pButton, EMPromptEntry *entry)
{
	if (entry->doc)
	{
		entry->doc->smart_save_rename = true;
		entry->doc->smart_save_overwrite = true;
		emSaveDoc(entry->doc);
	}
	emPromptUIDismiss();
	emFinishPrompt();
}


/********************
* PROMPT WINDOWS
********************/

static void emPromptForCheckout(EMPromptEntry *entry)
{
	UILabel *label;
	UIButton *checkout_button, *cancel_button;
	char buf[1024];

	prompt_window = ui_WindowCreate("Check Out Resource ?", 200, 200, 400, 60);

	sprintf(buf, "Check out '%s' for editing?", entry->res_name ? entry->res_name : "UNNAMED RESOURCE");
	label = ui_LabelCreate(buf, 10, 0);
	ui_WindowAddChild(prompt_window, label);

	checkout_button = ui_ButtonCreate("Checkout", 0, 0, emPromptUICheckout, entry);
	ui_WidgetSetWidth(UI_WIDGET(checkout_button), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(checkout_button), -90, 28, 0.5, 0, UITopLeft);
	ui_WindowAddChild(prompt_window, checkout_button);

	cancel_button = ui_ButtonCreate("Cancel", 0, 0, emPromptUICancel, entry);
	ui_WidgetSetWidth(UI_WIDGET(cancel_button), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(cancel_button), 10, 28, 0.5, 0, UITopLeft);
	ui_WindowAddChild(prompt_window, cancel_button);

	prompt_window->widget.width = 20 + label->widget.width;

	ui_WindowSetModal(prompt_window, true);
	ui_WindowSetClosable(prompt_window, false);
	ui_WindowShow(prompt_window);
	ui_SetFocus(checkout_button); // for being able to just press Enter
}


static void emPromptForRevertContinue(EMPromptEntry *entry)
{
	UILabel *label, *label2;
	UIButton *button;
	char buf[1024];

	prompt_window = ui_WindowCreate("File Changed on Disk", 200, 200, 400, 90);

	sprintf(buf, "'%s' changed on disk while you have unsaved edits.", entry->res_name ? entry->res_name : "UNNAMED RESOURCE");
	label = ui_LabelCreate(buf, 10, 0);
	ui_WindowAddChild(prompt_window, label);

	label2 = ui_LabelCreate("Revert to disk version or ignore changes and continue?", 10, 28);
	ui_WindowAddChild(prompt_window, label2);

	button = ui_ButtonCreate("Revert", 0, 0, emPromptUIRevert, entry);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(button), -90, 56, 0.5, 0, UITopLeft);
	ui_WindowAddChild(prompt_window, button);

	button = ui_ButtonCreate("Continue", 0, 0, emPromptUICancel, entry);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 10, 56, 0.5, 0, UITopLeft);
	ui_WindowAddChild(prompt_window, button);

	prompt_window->widget.width = 20 + MAX(label->widget.width, label2->widget.width);

	ui_WindowSetModal(prompt_window, true);
	ui_WindowSetClosable(prompt_window, false);
	ui_WindowShow(prompt_window);
}


static void emPromptForCheckoutRevert(EMPromptEntry *entry)
{
	UILabel *label, *label2;
	UIButton *button;
	char buf[1024];

	prompt_window = ui_WindowCreate("File No Longer Editable", 200, 200, 400, 90);

	sprintf(buf, "'%s' became no longer editable on disk.", entry->res_name ? entry->res_name : "UNNAMED RESOURCE");
	label = ui_LabelCreate(buf, 10, 0);
	ui_WindowAddChild(prompt_window, label);

	label2 = ui_LabelCreate("Checkout file or revert to disk version?", 10, 28);
	ui_WindowAddChild(prompt_window, label2);

	button = ui_ButtonCreate("Checkout", 0, 0, emPromptUICheckout, entry);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(button), -90, 56, 0.5, 0, UITopLeft);
	ui_WindowAddChild(prompt_window, button);

	button = ui_ButtonCreate("Revert", 0, 0, emPromptUIRevert, entry);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 10, 56, 0.5, 0, UITopLeft);
	ui_WindowAddChild(prompt_window, button);

	prompt_window->widget.width = 20 + MAX(label->widget.width, label2->widget.width);

	ui_WindowSetModal(prompt_window, true);
	ui_WindowSetClosable(prompt_window, false);
	ui_WindowShow(prompt_window);
}

static void emPromptForSave(EMPromptEntry *entry)
{
	UILabel *pLabel;
	UIButton *pButton;
	char buf[1024];
	int y = 0;
	int width = 0;
	int x = 0;

	prompt_window = ui_WindowCreate("Confirm Save?", 200, 200, 300, 60);

	if ((entry->prompt_type == EMPROMPT_SAVE_NEW_RENAME_CANCEL) ||
	    (entry->prompt_type == EMPROMPT_SAVE_NEW_RENAME_OVERWRITE_CANCEL)) {
		sprintf(buf, "The %s name was changed to a new name.  Did you want to rename or save as new?", entry->doc->editor->default_type);
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(prompt_window, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if ((entry->prompt_type == EMPROMPT_SAVE_OVERWRITE_CANCEL) ||
	    (entry->prompt_type == EMPROMPT_SAVE_NEW_RENAME_OVERWRITE_CANCEL)) {
		sprintf(buf, "The %s name '%s' is already in use.  Did you want to overwrite it?", entry->doc->editor->default_type, entry->doc->doc_name);
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(prompt_window, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (entry->prompt_type == EMPROMPT_SAVE_OVERWRITE_CANCEL) {
		pButton = ui_ButtonCreate("Overwrite", 0, 0, emPromptUISaveOverwrite, entry);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -105, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(prompt_window, pButton);

		x = 5;
		width = MAX(width, 230);
	} else if (entry->prompt_type == EMPROMPT_SAVE_NEW_RENAME_CANCEL) {
		pButton = ui_ButtonCreate("Save As New", 0, 0, emPromptUISaveOverwrite, entry);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -160, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(prompt_window, pButton);

		pButton = ui_ButtonCreate("Rename", 0, 0, emPromptUISaveRename, entry);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(prompt_window, pButton);

		x = 60;
		width = MAX(width, 340);
	} else if (entry->prompt_type == EMPROMPT_SAVE_NEW_RENAME_OVERWRITE_CANCEL) {
		pButton = ui_ButtonCreate("Save As New AND Overwrite", 0, 28, emPromptUISaveOverwrite, entry);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -260, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(prompt_window, pButton);

		pButton = ui_ButtonCreate("Rename AND Overwrite", 0, 28, emPromptUISaveRename, entry);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(prompt_window, pButton);

		x = 160;
		width = MAX(width, 540);
	}

	pButton = ui_ButtonCreate("Cancel", 0, 0, emPromptUICancel, entry);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(prompt_window, pButton);

	y += 28;

	prompt_window->widget.width = width;
	prompt_window->widget.height = y;

	ui_WindowSetClosable(prompt_window, false);
	ui_WindowSetModal(prompt_window, true);

	ui_WindowPresent(prompt_window);
}


/********************
* PROMPT LOGIC
********************/

/******
 * This function starts the first queued prompt.
 ******/
static void emStartPrompt(void)
{
	EMPromptEntry *entry;

	if (eaSize(&em_data.prompt_state.prompts) == 0) 
	{
		// No more to start
		return;
	}
	entry = em_data.prompt_state.prompts[0];
	em_data.prompt_state.current_prompt = entry;
	eaRemove(&em_data.prompt_state.prompts, 0);
	
	switch(entry->prompt_type)
	{
	case EMPROMPT_CHECKOUT:
			emPromptForCheckout(entry);
			break;
	case EMPROMPT_REVERT_CONTINUE:
			emPromptForRevertContinue(entry);
			break;
	case EMPROMPT_CHECKOUT_REVERT:
			emPromptForCheckoutRevert(entry);
			break;
	case EMPROMPT_SAVE_OVERWRITE_CANCEL:
	case EMPROMPT_SAVE_NEW_RENAME_CANCEL:
	case EMPROMPT_SAVE_NEW_RENAME_OVERWRITE_CANCEL:
			emPromptForSave(entry);
			break;
	}
}


/******
 * This function finishes the first queued prompt and starts the next.
 ******/
static void emFinishPrompt(void)
{
	// Free the current prompt
	if (em_data.prompt_state.current_prompt)
	{
		emPromptEntryDestroy(em_data.prompt_state.current_prompt);
		em_data.prompt_state.current_prompt = NULL;
	}

	if (eaSize(&em_data.prompt_state.prompts) == 0) 
	{
		return;
	}

	// Start the next one
	emStartPrompt();
}


/******
* This function queues up a prompt window for the user of the appropriate prompt type.
* Removes duplicate prompts.
* PARAMS:
*   prompt_type - the type of thing to prompt for
*   doc - The document to use for certain prompts
*   sub_doc - The sub-document to use for certain prompts
*   dict - The dictionary to use for certain prompts
*   res_name - The resource_name to use for certain prompts
******/
void emQueuePrompt(EMPromptType prompt_type, EMEditorDoc *doc, EMEditorSubDoc *sub_doc, DictionaryHandle dict, const char *res_name)
{
	EMPromptEntry *entry;
	int i;

	// Check for duplicate
	if (em_data.prompt_state.current_prompt &&
		(em_data.prompt_state.current_prompt->prompt_type == prompt_type) && 
		(em_data.prompt_state.current_prompt->doc == doc) && (em_data.prompt_state.current_prompt->sub_doc == sub_doc) && 
		(em_data.prompt_state.current_prompt->dict == dict) && 
		((em_data.prompt_state.current_prompt->res_name == res_name) || (em_data.prompt_state.current_prompt->res_name && res_name && stricmp(em_data.prompt_state.current_prompt->res_name, res_name) == 0))) 
	{
		// Ignore attempt to queue a duplicate prompt
		return;
	}
	for(i=eaSize(&em_data.prompt_state.prompts)-1; i>=0; --i) 
	{
		entry = em_data.prompt_state.prompts[i];
		if ((entry->prompt_type == prompt_type) && 
			(entry->doc == doc) && (entry->sub_doc == sub_doc) && 
			(entry->dict == dict) && 
			((entry->res_name == res_name) || (entry->res_name && res_name && stricmp(entry->res_name, res_name) == 0))) 
		{
			// Ignore attempt to queue a duplicate prompt
			return;
		}
	}

	// Queue the new entry
	entry = calloc(1, sizeof(EMPromptEntry));
	entry->prompt_type = prompt_type;
	entry->doc = doc;
	entry->sub_doc = sub_doc;
	entry->dict = dict;
	entry->res_name = res_name;
	eaPush(&em_data.prompt_state.prompts, entry);

	// If no prompt running, start one
	if (!em_data.prompt_state.current_prompt) 
	{
		emStartPrompt();
	}
}

/******
* This function removes all queued up prompts for a given document.
* PARAMS:
*   doc - The document to use
*   sub_doc - The sub-document to use
******/
void emRemovePrompts(EMEditorDoc *doc, EMEditorSubDoc *sub_doc)
{
	EMPromptEntry *entry;
	int i;

	for(i=eaSize(&em_data.prompt_state.prompts)-1; i>=0; --i)
	{
		entry = em_data.prompt_state.prompts[i];
		if ((entry->doc == doc) && (!sub_doc || (entry->sub_doc == sub_doc)))
		{
			emPromptEntryDestroy(entry);
			eaRemove(&em_data.prompt_state.prompts, i);
		}
	}
}

#endif