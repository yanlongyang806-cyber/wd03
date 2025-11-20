//
// MessageStoreEditor.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "Message.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "StringCache.h"



#define MSE_GROUP_FILE          "File"
#define MSE_GROUP_SCOPE         "Scope"
#define MSE_GROUP_DESCRIPTION   "Description"
#define MSE_GROUP_STRING        "String"
#define MSE_GROUP_FLAGS         "Flags"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *mseWindow = NULL;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int mse_validateCallback(METable *pTable, Message *pMessage, void *pUserData)
{
	if (!pMessage->pcFilename || !pMessage->pcFilename[0]) {
		Alertf("Message is not valid.  It must have a file name.");
		return false;
	}
	return resValidateClientSave(gMessageDict, pMessage->pcMessageKey, pMessage);
}


static void *mse_createObject(METable *pTable, Message *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	Message *pNewMessage;
	char buf[128];
	const char *pcBaseName;
	
	// Create the object
	if (pObjectToClone) {
		pNewMessage = StructClone(parse_Message, pObjectToClone);
		pcBaseName = pObjectToClone->pcMessageKey;
	} else {
		pNewMessage = StructCreate(parse_Message);
		pcBaseName = "New_Message";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewMessage, "Failed to create message");

	// Assign a new name
	pNewMessage->pcMessageKey = METableMakeNewNameShared(pTable,pcBaseName, true);

	// Assign a file
	sprintf(buf, "messages/%s.ms", pNewMessage->pcMessageKey);
	pNewMessage->pcFilename = allocAddFilename(buf);

	return pNewMessage;
}


static void *mse_tableCreateCallback(METable *pTable, Message *pObjectToClone, bool bCloneKeepsKeys)
{
	return mse_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *mse_windowCreateCallback(MEWindow *pWindow, Message *pObjectToClone)
{
	return mse_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void mse_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void mse_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, mse_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, mse_validateCallback, pTable);
	METableSetCreateCallback(pTable, mse_tableCreateCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(gMessageDict, mse_dictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void mse_initMessageColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Message Key", "messageKey", 150, NULL, kMEFieldType_TextEntry);

	// Lock in the message key column
	METableSetNumLockedColumns(pTable, 2);

	METableAddScopeColumn(pTable,    "Scope",          "Scope",            160, MSE_GROUP_SCOPE, kMEFieldType_TextEntry);
	METableAddFileNameColumn(pTable, "File Name",      "filename",         210, MSE_GROUP_FILE, NULL, "", "defs/messages", ".ms", UIBrowseNewOrExisting);
	METableSetColumnState(pTable, "File Name", ME_STATE_NOT_PARENTABLE); // Override normal file column behavior to make it editable

	METableAddSimpleColumn(pTable,   "Description",    "description",      210, MSE_GROUP_DESCRIPTION, kMEFieldType_MultiText);

	METableAddSimpleColumn(pTable,   "Default String", "defaultString",    210, MSE_GROUP_STRING, kMEFieldType_MultiText);

	METableAddSimpleColumn(pTable,   "Final",          "Final",            100, MSE_GROUP_FLAGS, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable,   "Do Not Translate", "DoNotTranslate", 160, MSE_GROUP_FLAGS, kMEFieldType_BooleanCombo);
}


static void mse_init(MultiEditEMDoc *pEditorDoc)
{
	if (!mseWindow) {
		// Create the editor window
		mseWindow = MEWindowCreate("Message Store Editor", "Message", "Messages", SEARCH_TYPE_MESSAGE, gMessageDict, parse_Message, "MessageKey", "filename", "scope", pEditorDoc);

		// Add message-specific columns
		mse_initMessageColumns(mseWindow->pTable);
		METableFinishColumns(mseWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(mseWindow);

		// Set the callbacks
		mse_initCallbacks(mseWindow, mseWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(mseWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *messageStoreEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	mse_init(pEditorDoc);

	return mseWindow;
}


void messageStoreEditor_createMessage(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = mse_createObject(mseWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(mseWindow->pTable, pObject, 1, 1);
}

#endif