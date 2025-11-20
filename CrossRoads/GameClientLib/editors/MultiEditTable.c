//
// MultiEditTable.c
//

#ifndef NO_EDITORS

#include "cmdparse.h"
#include "EditorManagerUtils.h"
#include "EditorPrefs.h"
#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "MultiEditTable.h"
#include "Powers.h"
#include "TextParserInheritance.h"
#include "tokenstore.h"
#include "objPath.h"
#include "StringCache.h"
#include "cmdClient.h"
#include "inputLib.h"

#include "AutoGen/MultiEditTable_h_ast.h"

#define MET_GROUP_MENU            NULL


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define MET_CLICK_LEFT    1
#define MET_CLICK_RIGHT   2

#define MENU_MOVE_UP_INDEX         1
#define MENU_MOVE_DOWN_INDEX       2
#define MENU_MOVE_TOP_INDEX        3
#define MENU_MOVE_BOTTOM_INDEX     4
#define MENU_VALIDATE_INDEX        7
#define MENU_SAVE_INDEX            8
#define MENU_REVERT_INDEX          10
#define MENU_DELETE_INDEX          11
#define MENU_FIND_USAGE_INDEX      12
#define MENU_LIST_REFERENCES_INDEX 13
#define MENU_CHECK_OUT_INDEX       15
#define MENU_UNDO_CHECK_OUT_INDEX  16
#define MENU_OPEN_FILE_INDEX       17
#define MENU_OPEN_FOLDER_INDEX     18
#define MENU_CREATE_CHILD_INDEX    19

#define SUBMENU_MOVE_UP_INDEX      1
#define SUBMENU_MOVE_DOWN_INDEX    2
#define SUBMENU_MOVE_TOP_INDEX     3
#define SUBMENU_MOVE_BOTTOM_INDEX  4
#define SUBMENU_EDIT_ROW_INDEX	   6
#define SUBMENU_REVERT_INDEX       7
#define SUBMENU_DELETE_INDEX       8
#define SUBMENU_NEW_INDEX          10
#define SUBMENU_CLONE_INDEX        11
#define SUBMENU_COPY_INDEX         12
#define SUBMENU_PASTE_INDEX        13

#define EDITMENU_EDIT_INDEX        0
#define EDITMENU_REVERT_INDEX      1
#define EDITMENU_INHERIT_INDEX     2
#define EDITMENU_NO_INHERIT_INDEX  3

#define PROMPT_FLAG_CLOSE          0x01
#define PROMPT_FLAG_SAVE           0x02
#define PROMPT_FLAG_DELETE         0x04

#define GIMME_ICON_OFFSET          12

#define Z_DIST_BEFORE_SELECTION   (-0.15)

#define PREF_COLUMN_WIDTH         "ColumnWidth"

static void met_addRow(METable *pTable, void *pOrigData, void *pNewData, bool bTop, bool bScrollTo);
static void met_revertRow(METable *pTable, int iRow, bool bForce);


//---------------------------------------------------------------------------------------------------
// Global Data
//---------------------------------------------------------------------------------------------------

static Color gColorMainBackground     = { 255, 255, 255, 255 };
static Color gColorSublistBackground  = { 232, 232, 232, 255 };
static Color gColorSublist2Background = { 216, 216, 216, 216 };
static Color gColorMenuIcon           = { 192, 192, 192, 255 };
static Color gColorMenuInner          = { 246, 246, 246, 255 };

// The static editing window
static UIWindow *pGlobalWindow = NULL;

// This is an annoying hack to allow a static handle to be declared
typedef union StyleBorderHandle { UIStyleBorder * const __pData_INTERNAL; ReferenceHandle __handle_INTERNAL;} StyleBorderHandle;
static StyleBorderHandle g_hBorder;

#define COLOR_INT_MENU_ICON          0xbfbfbfff
#define COLOR_GIMME_ICON             0x69f8219f


//---------------------------------------------------------------------------------------------------
// Helper Procedures
//---------------------------------------------------------------------------------------------------

// Gets the name of an object by row number using the parse table for access
const char* met_getObjectName(METable *pTable, int iRow)
{
	devassert(iRow < eaSize(&pTable->eaRows));
	return TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pTable->eaRows[iRow]->pObject, 0, NULL);
}


// Gets the name of an object by row number using the parse table for access
static const char* met_getOrigObjectName(METable *pTable, int iRow)
{
	if (pTable->eaRows[iRow]->pOrigObject) {
		return TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pTable->eaRows[iRow]->pOrigObject, 0, NULL);
	} else {
		return NULL;
	}
}


static const char* met_getObjectFileName(METable *pTable, int iRow)
{
	return TokenStoreGetString(pTable->pParseTable, pTable->iFileIndex, pTable->eaRows[iRow]->pObject, 0, NULL);
}


static int met_compareStrings(const char** left, const char** right, const void* context)
{
	return stricmp(*left,*right);
}


static int met_fieldSortComparator(const METableRow **left, const METableRow **right, const void* context)
{
	char *estrLeft = NULL;
	char *estrRight = NULL;
	int iResult;

	// Note: This is coded to only work on the main table, not on subtables

	estrStackCreate(&estrLeft);
	estrStackCreate(&estrRight);
	MEFieldGetString((*left)->eaFields[(*left)->pTable->iMenuTableCol], &estrLeft);
	MEFieldGetString((*right)->eaFields[(*right)->pTable->iMenuTableCol], &estrRight);

	// Sort dir can invert the result
	if ((strspn(estrLeft,"0123456789.-") == strlen(estrLeft)) &&
		(strspn(estrRight,"0123456789.-") == strlen(estrRight))) {
		// both strings appear to be numeric
		F32 fLeft = atof(estrLeft);
		F32 fRight = atof(estrRight);
		if (fLeft < fRight) {
			iResult = -1;
		} else if (fLeft > fRight) {
			iResult = 1;
		} else {
			iResult = 0;
		}
	} else {
		iResult = stricmp(estrLeft,estrRight);
	}

	estrDestroy(&estrLeft);
	estrDestroy(&estrRight);

	return iResult * (*left)->pTable->iSortDir;
}

static void met_clearEditWidget(METable *pTable)
{
	if (pTable->pEditWidget) {
		MEFieldDismiss(pTable->pEditField,(UIWidget*)pTable->pList);
		pTable->pEditWidget = NULL;
		pTable->pEditField = NULL;
	}
	MEFieldCloseEditor();
	METableCloseEditRow(pTable);
}


static void met_destroyFieldRow(MEField ***peaFields)
{
	int i;

	// Destroy each field
	for(i=eaSize(peaFields)-1; i>=0; --i) {
		if ((*peaFields)[i]) {
			MEFieldDestroy((*peaFields)[i]);
		}
	}

	// Destroy the row array itself
	eaDestroy(peaFields);
}


static void met_freeRow(METable *pTable, METableRow *pRow)
{
	int i, j;

	// Destroy the data in the row
	StructDestroyVoid(pTable->pParseTable, pRow->pObject);
	StructDestroyVoid(pTable->pParseTable, pRow->pOrigObject);
	StructDestroyVoid(pTable->pParseTable, pRow->pParentObject);
	met_destroyFieldRow(&pRow->eaFields);

	// Destroy each sub-row
	for(i=eaSize(&pRow->eaSubData)-1; i>=0; --i) {
		for(j=eaSize(&pRow->eaSubData[i]->eaRows)-1; j>=0; --j) {
			met_destroyFieldRow(&pRow->eaSubData[i]->eaRows[j]->eaFields);
			eaDestroy(&pRow->eaSubData[i]->eaFakeModel);
			free(pRow->eaSubData[i]->eaRows[j]);
		}
		eaDestroy(&pRow->eaSubData[i]->eaRows);
		eaDestroy(&pRow->eaSubData[i]->eaObjects);
		free(pRow->eaSubData[i]);
	}
	eaDestroy(&pRow->eaSubData);

	// Free the editor sub-doc
	free(pRow->pEditorSubDoc);

	// Free the row itself
	free(pRow);
}

static void met_closeRow(METable *pTable, int iRow, bool bOnNextTick)
{
	METableRow *pRow;
	const char *pcObjectName;

	pcObjectName = met_getObjectName(pTable,iRow);

	// Remove any edit widget so it won't collide with the close
	met_clearEditWidget(pTable);

	// Clear any selection
	ui_ListClearEverySelection(pTable->pList);

	// Remove from objects lists
	pRow = pTable->eaRows[iRow];
	eaRemove(&pTable->eaRows,iRow);

	// Remove from the editor doc
	eaFindAndRemove(&pTable->pEditorDoc->emDoc.sub_docs, (EMEditorSubDoc*)pRow->pEditorSubDoc);

	// Free the memory
	if (bOnNextTick) {
		eaPush(&pTable->eaRowsToFreeOnNextTick, pRow);
	} else {
		met_freeRow(pTable, pRow);
	}
	
	// Handle Editor Manager issue
	pTable->pEditorDoc->emDoc.saved = true;

	pTable->bCheckSmartHidden = true;
}


static void met_freeRowsOnTick(METable *pTable)
{
	int i;

	for(i=eaSize(&pTable->eaRowsToFreeOnNextTick)-1; i>=0; --i) {
		met_freeRow(pTable, pTable->eaRowsToFreeOnNextTick[i]);
	}
	eaClear(&pTable->eaRowsToFreeOnNextTick);
}


static void met_deleteObjectFromDictionary(METable *pTable, void *pObject)
{
	const void *pcObjName;
	const void *pcFileName;
	void *pRef;

	if (pObject) {
		pcObjName = TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pObject, 0, NULL);
		if (pcObjName) {
			pRef = RefSystem_ReferentFromString(pTable->hDict, pcObjName);
			if (pRef) {
				EMFile *pEMFile;

				// Check out file if not already checked out
				pcFileName = TokenStoreGetString(pTable->pParseTable, pTable->iFileIndex, pRef, 0, NULL);
				pEMFile = emGetFile(pcFileName, true);
				if (pEMFile && pEMFile->read_only) {
					if (!emuCheckoutFileEx(pEMFile, pcObjName, true)) {
						// Gimme code will show error dialog
						return;
					}
				}

				// Actually delete it
				emStatusPrintf("Permanently deleted %s \"%s\"", pTable->pcDisplayName, (const char*)pcObjName);
				RefSystem_RemoveReferent(pRef, true);
				ParserWriteTextFileFromDictionary(pcFileName, pTable->hDict, 0, 0);

				// Clean up messages
				langMakeEditorCopy(pTable->pParseTable, pRef, false);
				langDeleteMessages(pTable->pParseTable, pRef);
				
				// Free the structure
				StructDestroyVoid(pTable->pParseTable, pRef);
			}
		}
	}
}


bool met_deleteRowContinue(EMEditor *pEditor, const char *pcObjName, void *pObject, EMResourceState eState, METable *pTable, bool bSuccess)
{
	if (bSuccess && (eState == EMRES_STATE_LOCK_SUCCEEDED)) {
		// Start the delete by saving with NULL pointer
		emSetResourceStateWithData(pEditor, pcObjName, EMRES_STATE_DELETING, pObject);
		resRequestSaveResource(pTable->hDict, pcObjName, NULL);
		//printf("Delete saving %s\n", pcObjName); // DEBUG
		return false;

	} else if (bSuccess & (eState == EMRES_STATE_DELETE_SUCCEEDED)) {
		int i;

		// Close the row (if there is one)
		for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
			const char *pcName = met_getObjectName(pTable, i);
			if (pcName && (stricmp(pcName, pcObjName) == 0)) {
				met_closeRow(pTable, i, false);
				break;
			}
		}	

		// There won't be an object registered on a delete-for-rename
		if (pObject) {
			//printf("Deleting messages %s\n", pcObjName); // DEBUG

			// Delete messages
			langMakeEditorCopy(pTable->pParseTable, pObject, false);
			langDeleteMessages(pTable->pParseTable, pObject);
			
			// Free the structure
			StructDestroyVoid(pTable->pParseTable, pObject);
		}
	}

	return true;
}


static void met_deleteRowStart(METable *pTable, const char *pcObjName, void *pObject, bool bRename)
{
	void *pObjectCopy = NULL;

	if (!bRename) {
		pObjectCopy = StructCloneVoid(pTable->pParseTable, pObject);
	}

	resSetDictionaryEditMode(pTable->hDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Go into lock state if we don't already have the lock
	if (!resGetLockOwner(pTable->hDict, pcObjName))
	{
		emSetResourceStateWithData(pTable->pEditorDoc->emDoc.editor, pcObjName, EMRES_STATE_LOCKING_FOR_DELETE, pObjectCopy);
		//printf("Locking %s\n", pcObjName); // DEBUG
		resRequestLockResource(pTable->hDict, pcObjName, pObject);
		return;
	}

	// Otherwise continue the delete
	met_deleteRowContinue(pTable->pEditorDoc->emDoc.editor, pcObjName, pObjectCopy, EMRES_STATE_LOCK_SUCCEEDED, pTable, true);
}


static void met_deleteRow(METable *pTable, int iRow)
{
	void *pOrigObject;

	// Delete the original object
	pOrigObject = pTable->eaRows[iRow]->pOrigObject;
	if (pOrigObject) {
		const char *pcObjName = TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pOrigObject, 0, NULL);
		met_deleteRowStart(pTable, pcObjName, pOrigObject, false);
		return;
	}

	// Close the row in the editor after deleting from dictionary (if required)
	met_closeRow(pTable, iRow, 0);
}


static int met_isNameCollision(METable *pTable, int iRow)
{
	const char *pcName = met_getObjectName(pTable,iRow);
	const char *pcOrigName = met_getOrigObjectName(pTable,iRow);

	// If no original name, or original name not matching current name,
	// then check for collision
	if (!pcOrigName || (pcOrigName && (stricmp(pcOrigName,pcName) != 0))) {
		if (RefSystem_ReferentFromString(pTable->hDict, pcName)) {
			return 1;
		}
	}
	return 0;
}


static void ***met_getSubTableRows(METable *pTable, int iSubTableId, void *pObject)
{
	MESubTable *pSubTable = pTable->eaSubTables[iSubTableId];
	int iResultCol;

	if (pSubTable->pcSubPTName[0] != '.') {
		if (!ParserFindColumn(pTable->pParseTable, pSubTable->pcSubPTName, &iResultCol)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pSubTable->pcSubPTName);
			assertmsg(0, buf);
			return NULL;
		}
		return TokenStoreGetEArray(pTable->pParseTable, iResultCol, pObject, NULL);
	} else {
		ParseTable *pResultParseTable;
		void *pResultData;
		int index;

		if (!objPathResolveField(pSubTable->pcSubPTName,pTable->pParseTable,pObject,&pResultParseTable,&iResultCol,&pResultData,&index,OBJPATHFLAG_CREATESTRUCTS)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pSubTable->pcSubPTName);
			assertmsg(0, buf);
			return NULL;
		}
		return TokenStoreGetEArray(pResultParseTable, iResultCol, pResultData, NULL);
	}
}


int met_getFieldData(ParseTable *pParseTable, char *pcField, void *pData, ParseTable **ppResultParseTable, int *piResultCol, void **ppResultData)
{
	if (pcField[0] == '.') {
		int index;
		if (!objPathResolveField(pcField,pParseTable,pData,ppResultParseTable,piResultCol,ppResultData,&index,OBJPATHFLAG_CREATESTRUCTS)) {
			char buf[1024];
			sprintf(buf, "Field is missing and could not be created: %s", pcField);
			assertmsg(0, buf);
			return 0;
		}
		return 1;
	} else if (pcField[0] == '@') {
		// This is used for polymorphic fields, expecting format "@base.path@inside.poly.path"
		char buf[260];
		char *pos;
		int index;

		strcpy(buf, pcField);
		buf[0] = '.';
		pos = strchr(buf,'@');
		if (pos) {
			*pos = '\0';
		}
		if (!objPathResolveField(buf,pParseTable,pData,ppResultParseTable,piResultCol,ppResultData,&index,OBJPATHFLAG_CREATESTRUCTS)) {
			char buf2[1024];
			sprintf(buf2, "Field is missing and could not be created: %s", pcField);
			assertmsg(0, buf2);
			return 0;
		}
		if (*ppResultData) {
			// Get here if poly field exists and is non-null, now test if whole field is valid
			if (pos) {
				*pos = '.';
			}
			if (objPathResolveField(buf,pParseTable,pData,ppResultParseTable,piResultCol,ppResultData,&index,OBJPATHFLAG_CREATESTRUCTS)) {
				// Subfield exists, so we have valid data
				return 1;
			}
			// Subfield does not exist in this poly form so fall through to return failure
		}
		*ppResultParseTable = NULL;
		*piResultCol = -1;
		*ppResultData = NULL;
		return 0;
	} else {
		if (!ParserFindColumn(pParseTable, pcField, piResultCol)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pcField);
			assertmsg(0, buf);
			return 0;
		}
		*ppResultParseTable = pParseTable;
		*ppResultData = pData;
		return 1;
	}
}



// Checks if a given row has a dirty field
static int met_isRowDirty(METable *pTable, int iRow)
{
	MEField **eaFields;
	int i,j,k;

	// Scan the main rows
	eaFields = pTable->eaRows[iRow]->eaFields;
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i] && eaFields[i]->bDirty) {
			return 1;
		}
	}

	// Scan the sub-rows
	for(i=eaSize(&pTable->eaRows[iRow]->eaSubData)-1; i>=0; --i) {
		if (pTable->eaRows[iRow]->pOrigObject) {
			// If number of sub-rows changes (for example a row is deleted), then it's dirty
			void ***peaOrigSubRows = met_getSubTableRows(pTable, i, pTable->eaRows[iRow]->pOrigObject);
			if (eaSize(&pTable->eaRows[iRow]->eaSubData[i]->eaRows) != eaSize(peaOrigSubRows)) {
				return 1;
			}
		}

		for(j=eaSize(&pTable->eaRows[iRow]->eaSubData[i]->eaRows)-1; j>=0; --j) {
			eaFields = pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->eaFields;
			for(k=eaSize(&eaFields)-1; k>=0; --k) {
				if (eaFields[k] && eaFields[k]->bDirty) {
					return 1;
				}
			}
		}
	}

	return 0;
}


static int met_isValidName(METable *pTable, int iRow)
{
	const char *pcName = met_getObjectName(pTable, iRow);

	// Not valid if no name
	if (!pcName || !pcName[0] || (pcName[1] == '_')) {
		return 0;
	}

	return 1;
}


static int met_isValidFileName(METable *pTable, int iRow)
{
	const char *pcFileName = met_getObjectFileName(pTable, iRow);

	// Not valid if no file or no AMFile
	if (!pcFileName || !pcFileName[0] || !pTable->eaRows[iRow]->pEMFile) {
		return 0;
	}

	return 1;
}


static void met_writeBackupObject(METable *pTable, int iRow)
{
	//char *pcBakFileName = NULL;
	//const char *pcFileName;
	// Create the backup file
	//pcFileName = met_getObjectFileName(pTable,iRow);
	//estrStackCreate(&pcBakFileName);
	//estrPrintf(&pcBakFileName,"%s.bak",pcFileName);
	//ParserWriteTextFileFromSingleDictionaryStruct( pcBakFileName, pTable->hDict, pTable->eaRows[iRow]->pObject, 0, 0 );
	//emStatusPrintf("Saved %s \"%s\" to backup file \"%s\"", pTable->pcDisplayName, met_getObjectName(pTable, iRow), pcBakFileName);
	//estrDestroy(&pcBakFileName);
}


static void met_fixInheritance(METable *pTable, int iRow, bool bClean)
{
	void *pObject;
	char *pcParentName;
	int i,j,k,iInhCol;
	InheritanceData *pInhData;

	pObject = pTable->eaRows[iRow]->pObject;
	pcParentName = StructInherit_GetParentName(pTable->pParseTable, pObject);
	
	// Return if has no parent
	if (!pcParentName) {
		return;
	}

	// In clean mode, we wipe the inheritance info rather than just acting on top of it
	if (bClean) {
		// Duplicate the name so it survives the re-create
		char *pcTempName = strdup(pcParentName);

		// Recreate the inheritance data
		StructInherit_StopInheriting(pTable->pParseTable, pObject);
		StructInherit_StartInheriting(pTable->pParseTable, pObject, pcTempName, ParserGetFilename(pTable->pParseTable, pObject));

		free(pcTempName);
	}

	// Iterate the fields to update inheritance struct
	for(i=eaSize(&pTable->eaRows[iRow]->eaFields)-1; i>=0; --i) {
		if (pTable->eaRows[iRow]->eaFields[i]) {
			MEFieldValidateInheritance(pTable->eaRows[iRow]->eaFields[i]);
		}
	}

	// Iterate the sub-fields to update inheritance struct
	for(i=eaSize(&pTable->eaRows[iRow]->eaSubData)-1; i>=0; --i) {
		for(j=eaSize(&pTable->eaRows[iRow]->eaSubData[i]->eaRows)-1; j>=0; --j) {
			MEField **eaFields;
			bool bInherits = true;

			if (!pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->bInherited) {
				// If the row is not inherited, then just one inheritance entry for the row
				int iKey;
				char buf[1024];
				
				iKey = TokenStoreGetInt(pTable->eaSubTables[i]->pParseTable,pTable->eaSubTables[i]->iKeyIndex,pTable->eaRows[iRow]->eaSubData[i]->eaObjects[j],0,NULL);
				sprintf(buf, ".%s[\"%d\"]", pTable->eaSubTables[i]->pcSubPTName, iKey);
				if (!pTable->eaRows[iRow]->pParentObject ||
					!objPathResolveField(buf, pTable->pParseTable, pTable->eaRows[iRow]->pParentObject, NULL, NULL, NULL, NULL, 0)) {
					// Not actually inherited, so add struct
					StructInherit_CreateAddStructOverride(pTable->pParseTable, pTable->eaRows[iRow]->pObject, buf);
					bInherits = false;
				}
			}
			if (bInherits) {
				// If the row is inherited, iterate the fields looking for overrides
				eaFields = pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->eaFields;

				// Need three passes... first on not applicable, then on applicable and parented,
				// then on applicable and not parented
				// Because polymorphic structures can override names and this avoids collision
				// And optional structs may be only partially parented and this avoids collision
				for(k=eaSize(&eaFields)-1; k>=0; --k) {
					if (eaFields[k] && eaFields[k]->bNotApplicable) {
						MEFieldValidateInheritance(eaFields[k]);
					}
				}
				for(k=eaSize(&eaFields)-1; k>=0; --k) {
					if (eaFields[k] && !eaFields[k]->bNotApplicable && eaFields[k]->bParented) {
						MEFieldValidateInheritance(eaFields[k]);
					}
				}
				for(k=eaSize(&eaFields)-1; k>=0; --k) {
					if (eaFields[k] && !eaFields[k]->bNotApplicable && !eaFields[k]->bParented) {
						MEFieldValidateInheritance(eaFields[k]);
					}
				}
			}
		}
	}

	// Scan inheritance data for ADD rows and make sure the row still exists and wasn't
	// deleted while editing.  If deleted, remove them from inheritance struct.
	iInhCol = StructInherit_GetInheritanceDataColumn(pTable->pParseTable);
	pInhData = TokenStoreGetPointer(pTable->pParseTable, iInhCol, pObject, 0, NULL);
	if (pInhData) {
		for(i=eaSize(&pInhData->ppFieldData)-1; i>=0; --i) {
			if (pInhData->ppFieldData[i]->eType == OVERRIDE_ADD) {
				if (!objPathResolveField(pInhData->ppFieldData[i]->pFieldName, pTable->pParseTable, pObject, 
					NULL, NULL, NULL, NULL, 0)) {
					StructInherit_DestroyOverride(pTable->pParseTable, pObject, pInhData->ppFieldData[i]->pFieldName);
				}
			}
		}
	}

	if (pTable->cbFixInheritance) {
		pTable->cbFixInheritance(pTable, pObject);
	}

	// Force update of inheritance data values from the struct after
	// making sure inheritance data tracking info is correct
	StructInherit_UpdateFromStruct(pTable->pParseTable, pObject, true);

	// Also set file
	StructInherit_SetFileName(pTable->pParseTable, pObject, ParserGetFilename(pTable->pParseTable, pObject));
}


// Returns 1 on success and zero on failure
static int met_validateObject(METable *pTable, void *pObject)
{
	bool bRet = 1;

	// Call validation callback if available
	if (pTable->cbValidate) {
		if (!pTable->cbValidate(pTable, pObject, pTable->pValidateUserData)) {
			return 0;
		}
	}

	return 1;
}

static void met_UIDismissEditor(UIWidget *pWidget, METable *pTable)
{
	int i;

	if (!pTable->pEditWindow) {
		return;
	}
	
	// Save the position
	EditorPrefStoreWindowPosition("METable", "Edit Window", "Position", pTable->pEditWindow);

	// Hide the window
	ui_WindowHide(pTable->pEditWindow);

	// Clear the data
	for(i=eaSize(&pTable->eaEditLabels)-1; i>=0; --i) {
		pTable->eaEditLabels[i] = NULL;
		pTable->eaEditWidgets[i] = NULL;
	}

	// Destroy the window
	ui_WidgetQueueFree(UI_WIDGET(pTable->pEditWindow));
	pTable->pEditWindow = NULL;
}


void METableCloseEditRow(METable *pTable)
{
	met_UIDismissEditor(NULL, pTable);
}

static void met_UICustomMenuItem(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		pTable->cbCustomMenu(pTable, pTable->eaRows[((*eaiRows)[i])]->pObject, pTable->pCustomMenuUserData);
	}
}

void METableAddCustomAction(METable *pTable, const char *pcText, METableCustomMenuCallback cbCustomMenu, void *pCustomMenuUserData)
{
	assert(cbCustomMenu && !pTable->cbCustomMenu);

	pTable->cbCustomMenu = cbCustomMenu;
	pTable->pCustomMenuUserData = pCustomMenuUserData;

	ui_MenuAppendItems(pTable->pTableMenu,
		ui_MenuItemCreate("", UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate(pcText, UIMenuCallback, met_UICustomMenuItem, pTable, NULL),
		NULL);

}	

static void met_editRowExpandOrCollapseAll(UIWidget *pWidget, METable *pTable)
{
	int i;
	bool expanding = true;

	if (!stricmp(ui_WidgetGetText(pWidget), "Collapse All")) {
		expanding = false;
		ui_WidgetSetTextString(pWidget, "Expand All");
	}
	else {
		ui_WidgetSetTextString(pWidget, "Collapse All");
	}

	for (i = 0; i < eaSize(&pTable->pEditExpanderGroup->childrenInOrder); i++) {
		ui_ExpanderSetOpened((UIExpander*)pTable->pEditExpanderGroup->childrenInOrder[i], expanding);
	}
}

static void met_refreshEditWindow(METable *pTable)
{
	MEField **eaFields;
	UILabel *pLabel;
	int i,j;
	F32 fLongestLabel = 0.0;
	UIExpander **eaUsedExpanders = NULL;
	bool bAllExpanded = true;
	char *pcFirstColGroup = NULL;

	if (pTable->bEditSubRow) {
		eaFields = pTable->eaRows[pTable->iEditRow]->eaSubData[pTable->iEditSubTable]->eaRows[pTable->iEditSubRow]->eaFields;
	} else {
		eaFields = pTable->eaRows[pTable->iEditRow]->eaFields;
	}

	// First loop through and find the longest label
	pLabel = ui_LabelCreate("", 0, 0);
	for(i=0; i<eaSize(&eaFields); ++i) {
		MEField *pField = eaFields[i];
		if (pField && pField->column >= 0 && pField->column < eaSize(&pTable->eaCols)) {
			ui_LabelSetText(pLabel, pField->pchLabel);
			if (pLabel->widget.width > fLongestLabel) {
				fLongestLabel = pLabel->widget.width;
			}
		}
	}
	ui_LabelFreeInternal(pLabel);

	// Loop through and find the first column group
	if (pTable->bEditSubRow) {
		for(i=0; i<eaSize(&pTable->eaSubTables[pTable->iEditSubTable]->eaCols); ++i) {
			if (pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->pcGroup) {
				pcFirstColGroup = pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->pcGroup;
				break;
			}
		}
	} else {
		for(i=0; i<eaSize(&pTable->eaCols); ++i) {
			if (pTable->eaCols[i]->pcGroup) {
				pcFirstColGroup = pTable->eaCols[i]->pcGroup;
				break;
			}
		}
	}

	// Make sure label and widget arrays are the right size
	while(eaSize(&pTable->eaEditLabels) < eaSize(&eaFields)) {
		eaPush(&pTable->eaEditLabels, NULL);
		eaPush(&pTable->eaEditWidgets, NULL);
	}
	while(eaSize(&pTable->eaEditLabels) > eaSize(&eaFields)) {
		eaRemove(&pTable->eaEditLabels, eaSize(&eaFields));
		eaRemove(&pTable->eaEditWidgets, eaSize(&eaFields));
	}
	assert(pTable->eaEditLabels);
	assert(pTable->eaEditWidgets);

	// Next loop through adding or updating fields
	for(i=0; i<eaSize(&eaFields); ++i) {
		MEField *pField = eaFields[i];
		char *pcColGroup;
		UIExpander *pExpander;
		UIWidget *pWidget;
		F32 y;
		bool bOpened;

		// Skip this field if not present or not applicable
		if (!pField || pField->bNotApplicable) {
			ui_WidgetQueueFreeAndNull(&pTable->eaEditLabels[i]);
			ui_WidgetQueueFreeAndNull(&(UILabel*)pTable->eaEditWidgets[i]);
			continue;
		}
		
		// Figure out the column group name, which is the expander name
		if (pTable->bEditSubRow) {
			pcColGroup = pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->pcGroup;
		} else {
			pcColGroup = pTable->eaCols[i]->pcGroup;
		}
		if (!pcColGroup) {
			pcColGroup = pcFirstColGroup;
		}

		// find the expander that matches this column
		pExpander = NULL;
		for(j=0; j < eaSize(&pTable->pEditExpanderGroup->childrenInOrder); ++j) {
			const char* widgetText = ui_WidgetGetText(pTable->pEditExpanderGroup->childrenInOrder[j]);
			if (widgetText && pcColGroup &&
				!stricmp(pcColGroup, widgetText)) {
				pExpander = (UIExpander*)pTable->pEditExpanderGroup->childrenInOrder[j];
			}
		}
		if (pExpander) {
			// If found for first time on this refresh, set height to zero
			if (eaFind(&eaUsedExpanders,pExpander) == -1) {
				pExpander->openedHeight = 0;
			}
		} else {
			// If no match, create one	
			pExpander = ui_ExpanderCreate(pcColGroup, 0);
			ui_ExpanderGroupAddExpander(pTable->pEditExpanderGroup, pExpander);
		}
		eaPushUnique(&eaUsedExpanders, pExpander);

		// Set expander open status when it is created or first found
		if (pExpander->bFirstRefresh)
		{
			if (pTable->bEditSubRow)
			{
				bOpened = !pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->pListColumn->bHidden;
			}
			else
			{
				bOpened = !pTable->eaCols[i]->pListColumn->bHidden;
			}
			ui_ExpanderSetOpened(pExpander, bOpened);
			bAllExpanded &= bOpened;
		}

		// Position label
		y = pExpander->openedHeight;
		if (!pTable->eaEditLabels[i]) {
			pTable->eaEditLabels[i] = ui_LabelCreate(pField->pchLabel ? pField->pchLabel : "Field Value", 25, y);
			ui_ExpanderAddChild(pExpander, pTable->eaEditLabels[i]);
		} else {
			ui_LabelSetText(pTable->eaEditLabels[i], pField->pchLabel ? pField->pchLabel : "Field Value");
			ui_WidgetSetPosition(UI_WIDGET(pTable->eaEditLabels[i]), 25, y);
		}

		// Position widget
		pWidget = pTable->eaEditWidgets[i];
		if (!pWidget) {
			pTable->eaEditWidgets[i] = pWidget = MEFieldCreateEditWidget(pField);
			ui_ExpanderAddChild(pExpander, pWidget);
		}
		ui_WidgetSetPosition(pWidget, fLongestLabel+30, y);
		ui_WidgetSetWidthEx(pWidget, 1.0f, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pWidget, 0, 5, 0, 0);

		// If this is a data-provided column, reload the names array before displaying it
		if (!pTable->bEditSubRow) {
			if (pTable->eaCols[i]->cbDataFunc) {
				for(j=eaSize(&pTable->eaCols[i]->eaDictNames)-1; j>=0; --j) {
					free(pTable->eaCols[i]->eaDictNames[j]);
				}
				eaClear(&pTable->eaCols[i]->eaDictNames);
				pTable->eaCols[i]->eaDictNames = pTable->eaCols[i]->cbDataFunc(pTable, pTable->eaRows[pTable->iEditRow]->pObject);
				eaStableSort(pTable->eaCols[i]->eaDictNames, NULL, met_compareStrings);
			}
		} else {
			if (pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->cbDataFunc) {
				for(j=eaSize(&pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->eaDictNames)-1; j>=0; --j) {
					free(pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->eaDictNames[j]);
				}
				eaClear(&pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->eaDictNames);
				pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->eaDictNames = pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->cbDataFunc(pTable, pTable->eaRows[pTable->iEditRow]->eaSubData[pTable->iEditSubTable]->eaObjects[pTable->iEditSubRow]);
				eaStableSort(pTable->eaSubTables[pTable->iEditSubTable]->eaCols[i]->eaDictNames, NULL, met_compareStrings);
			}
		}

		// Set up expander
		y += pWidget->height + 5;
		ui_ExpanderSetHeight(pExpander, y);
	}

	// Remove unused expanders
	for(i=eaSize(&pTable->pEditExpanderGroup->childrenInOrder)-1; i>=0; --i) {
		if (eaFind(&eaUsedExpanders, (UIExpander*)pTable->pEditExpanderGroup->childrenInOrder[i]) == -1) {
			ui_ExpanderGroupRemoveExpander(pTable->pEditExpanderGroup, (UIExpander*)pTable->pEditExpanderGroup->childrenInOrder[i]);
		}
	}
}

static void met_checkEditWindow(METable *pTable)
{
	if (pTable->bRefreshEditWindow) {
		if (pTable->pEditWindow) {
			met_refreshEditWindow(pTable);
		}
		pTable->bRefreshEditWindow = false;
	}
}

static void met_editRow(METable *pTable, int iRow, int iSubTable, int iSubTableRow, bool bSubRow)
{
	UISeparator *pSeparator = NULL;
	UIButton *pButton = NULL;
	char windowTitle[1024] = {0};

	pTable->iEditRow = iRow;
	pTable->iEditSubTable = iSubTable;
	pTable->iEditSubRow = iSubTableRow;
	pTable->bEditSubRow = bSubRow;

	if (bSubRow)
		sprintf(windowTitle, "Editing %s #%d on %s \"%s\"", pTable->eaSubTables[iSubTable]->pcDisplayName, iSubTableRow + 1, pTable->pcDisplayName, met_getObjectName(pTable, iRow)); 
	else
		sprintf(windowTitle, "Editing %s \"%s\"", pTable->pcDisplayName, met_getObjectName(pTable, iRow)); 

	pTable->pEditWindow = ui_WindowCreate(windowTitle,100,100,400,400);
	pTable->pEditWindow->widget.scale = emGetEditorScale(pTable->pEditorDoc->emDoc.editor);
	EditorPrefGetWindowPosition("METable", "Edit Window", "Position", pTable->pEditWindow);

	pTable->pEditExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPositionEx(UI_WIDGET(pTable->pEditExpanderGroup), 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTable->pEditExpanderGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pTable->pEditExpanderGroup), 0, 0, 0, 38);
	ui_WindowAddChild(pTable->pEditWindow, pTable->pEditExpanderGroup);

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPositionEx(UI_WIDGET(pSeparator), 0, 36, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pTable->pEditWindow, pSeparator);

	pButton = ui_ButtonCreate("OK", 0, 0, met_UIDismissEditor, pTable);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	pButton->widget.width = 50;
	ui_WindowAddChild(pTable->pEditWindow, pButton);

	// Create last button down here after we know expand state
	pTable->pEditToggleButton = ui_ButtonCreate("Collapse All", 0, 0, met_editRowExpandOrCollapseAll, pTable);
	ui_WidgetSetPositionEx(UI_WIDGET(pTable->pEditToggleButton), 0, 0, 0, 0, UIBottomLeft);
	pTable->pEditToggleButton->widget.width = 100;
	ui_WindowAddChild(pTable->pEditWindow, pTable->pEditToggleButton);

	met_refreshEditWindow(pTable);

	ui_WindowPresent(pTable->pEditWindow);
}


static void met_UIDismissWindow(UIButton *pButton, METable *pTable)
{
	EditorPrefStoreWindowPosition("METable", "Window Position", "Save Confirm", pGlobalWindow);

	// Free the window
	ui_WindowHide(pGlobalWindow);
	ui_WidgetQueueFree(UI_WIDGET(pGlobalWindow));
	pGlobalWindow = NULL;

	// Clear window flags
	pTable->eaRows[pTable->iMenuTableRow]->bSaveOverwrite = false;
	pTable->eaRows[pTable->iMenuTableRow]->bSaveRename = false;
}


static void met_UISaveOverwrite(UIButton *pButton, METable *pTable)
{
	met_UIDismissWindow(pButton, pTable);

	pTable->eaRows[pTable->iMenuTableRow]->bSaveOverwrite = true;
	emSaveSubDoc(&pTable->pEditorDoc->emDoc, &pTable->eaRows[pTable->iMenuTableRow]->pEditorSubDoc->emSubDoc);
}


static void met_UISaveRename(UIButton *pButton, METable *pTable)
{
	met_UIDismissWindow(pButton, pTable);

	pTable->eaRows[pTable->iMenuTableRow]->bSaveRename = true;
	emSaveSubDoc(&pTable->pEditorDoc->emDoc, &pTable->eaRows[pTable->iMenuTableRow]->pEditorSubDoc->emSubDoc);
}


static void met_promptForSave(METable *pTable, int iRow, bool bNameCollision, bool bNameChanged)
{
	UILabel *pLabel;
	UIButton *pButton;
	char buf[1024];
	int y = 0;
	int width = 0;
	int x = 0;

	// Save the row
	pTable->iMenuTableRow = iRow;

	pGlobalWindow = ui_WindowCreate("Confirm Save?", 200, 200, 300, 60);

	EditorPrefGetWindowPosition("METable", "Window Position", "Save Confirm", pGlobalWindow);

	if (bNameChanged) {
		sprintf(buf, "The name was changed to a new name.  Did you want to rename or save as new?");
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameCollision) {
		sprintf(buf, "The name '%s' is already in use.  Did you want to overwrite it?", met_getObjectName(pTable, iRow));
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameChanged) {
		if (bNameCollision) {
			pButton = ui_ButtonCreate("Save As New AND Overwrite", 0, 28, met_UISaveOverwrite, pTable);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -260, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			pButton = ui_ButtonCreate("Rename AND Overwrite", 0, 28, met_UISaveRename, pTable);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			x = 160;
			width = MAX(width, 540);
		} else {
			pButton = ui_ButtonCreate("Save As New", 0, 0, met_UISaveOverwrite, pTable);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -160, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			pButton = ui_ButtonCreate("Rename", 0, 0, met_UISaveRename, pTable);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			x = 60;
			width = MAX(width, 340);
		}
	} else {
		pButton = ui_ButtonCreate("Overwrite", 0, 0, met_UISaveOverwrite, pTable);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -105, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(pGlobalWindow, pButton);

		x = 5;
		width = MAX(width, 230);
	}

	pButton = ui_ButtonCreate("Cancel", 0, 0, met_UIDismissWindow, pTable);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}


// Finalizes save for a client-server saved object
bool met_saveRowContinue(EMEditor *pEditor, const char *pcName, void *pObject, EMResourceState eState, METable *pTable, bool bSuccess)
{
	if (bSuccess && (eState == EMRES_STATE_SAVE_SUCCEEDED)) {
		const char *pcObjName;
		int i;
		for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
			pcObjName = met_getObjectName(pTable,i);
			if (pcObjName && stricmp(pcObjName, pcName) == 0) {
				// Mark it as saved so it will revert due to the dictionary change callback.
				pTable->eaRows[i]->bSaved = 1;

				emStatusPrintf("Saved %s \"%s\"", pTable->pcDisplayName, pcName);

				// Save prefs on an interesting event
				METableSavePrefs(pTable);
			}
		}
	}
	return true;
}


// EMTaskStatus to indicate success, failure, or in progress
static EMTaskStatus met_saveRow(METable *pTable, int iRow, bool bShowErrorDialog)
{
	void *pObject;
	const char *pcObjectName, *pcOrigName, *pcFileName, *pcOtherFileName = NULL;
	EMTaskStatus status;

	pcObjectName = met_getObjectName(pTable,iRow);

	// Process status changes
	if (emHandleSaveResourceState(pTable->pEditorDoc->emDoc.editor, pcObjectName, &status)) {
		if (status != EM_TASK_INPROGRESS) {
			pTable->eaRows[iRow]->bSaveOverwrite = false;
			pTable->eaRows[iRow]->bSaveRename = false;
		}
		if (status == EM_TASK_SUCCEEDED)
		{
			pTable->eaRows[iRow]->bDirty = false;
		}
		return status;
	}

	// Save only if dirty
	if (!pTable->eaRows[iRow]->bDirty) {
		pTable->eaRows[iRow]->bSaveOverwrite = false;
		pTable->eaRows[iRow]->bSaveRename = false;
		return EM_TASK_SUCCEEDED;
	}

	// Make sure inheritance structure is correct
	met_fixInheritance(pTable, iRow, true);

	// Make a copy of the object we're saving
	pObject = StructCloneVoid(pTable->pParseTable, pTable->eaRows[iRow]->pObject);

	// Validate the object
	if (!met_validateObject(pTable, pObject)) {
		// Disabled backup because it generally fails to write the file as often as not
		//met_writeBackupObject(pTable, iRow);

		StructDestroyVoid(pTable->pParseTable, pObject);
		pTable->eaRows[iRow]->bSaveOverwrite = false;
		pTable->eaRows[iRow]->bSaveRename = false;
		return EM_TASK_FAILED;
	}

	// Re-update from struct after validation in case it changed
	if (StructInherit_GetParentName(pTable->pParseTable, pObject)) {
		StructInherit_UpdateFromStruct(pTable->pParseTable, pObject, false);
	}

	// CONFIRM NAME CHANGE
	pcOrigName = met_getOrigObjectName(pTable,iRow);
	if (!pTable->eaRows[iRow]->bSaveOverwrite && !pcOrigName && resGetInfo(pTable->hDict, pcObjectName)) {
		met_promptForSave(pTable, iRow, true, false);

		StructDestroyVoid(pTable->pParseTable, pObject);
		pTable->eaRows[iRow]->bSaveOverwrite = false;
		pTable->eaRows[iRow]->bSaveRename = false;
		return EM_TASK_FAILED;
	} else if (!pTable->eaRows[iRow]->bSaveOverwrite && !pTable->eaRows[iRow]->bSaveRename &&
			   pcOrigName && (stricmp(pcOrigName, pcObjectName) != 0)) {
		met_promptForSave(pTable, iRow, (resGetInfo(pTable->hDict, pcObjectName) != NULL), true);

		StructDestroyVoid(pTable->pParseTable, pObject);
		pTable->eaRows[iRow]->bSaveOverwrite = false;
		pTable->eaRows[iRow]->bSaveRename = false;
		return EM_TASK_FAILED;
	}

	resSetDictionaryEditMode(pTable->hDict, true);
	if (pTable->hDict != gMessageDict) {
		resSetDictionaryEditMode(gMessageDict, true);
	}

	// LOCKING
	if (!resGetLockOwner(pTable->hDict, pcObjectName)) {
		emSetResourceState(pTable->pEditorDoc->emDoc.editor, pcObjectName, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(pTable->hDict, pcObjectName, pTable->eaRows[iRow]->pObject);

		StructDestroyVoid(pTable->pParseTable, pObject);
		return EM_TASK_INPROGRESS;
	}

	// ACTUAL SAVE LOGIC

	// Do pre-save function on the copy
	if (pTable->cbPreSave) {
		pTable->cbPreSave(pTable, pObject);
	}

	// Get the name and filename
	pcObjectName = met_getObjectName(pTable,iRow);
	pcFileName = met_getObjectFileName(pTable,iRow);

	// If there is an original object and the name changed, 
	// need to explicitly delete the old object when saving
	if (pTable->eaRows[iRow]->pOrigObject && (stricmp(pcObjectName,pcOrigName) != 0)) {
		if (!pTable->eaRows[iRow]->bSaveOverwrite) {
			// Delete original
			// Use NULL as orig object so it doesn't close row when done
			met_deleteRowStart(pTable, pcOrigName, pTable->eaRows[iRow]->pOrigObject, true);
		}

		// Need to alter name on orig object in order to catch dictionary change update
		TokenStoreSetString(pTable->pParseTable, pTable->iNameIndex, pTable->eaRows[iRow]->pOrigObject, 0, pcObjectName, NULL, NULL, NULL, NULL);
	}

	pTable->eaRows[iRow]->bSaveOverwrite = false;
	pTable->eaRows[iRow]->bSaveRename = false;

	emSetResourceState(pTable->pEditorDoc->emDoc.editor, pcObjectName, EMRES_STATE_SAVING);
	resRequestSaveResource(pTable->hDict, pcObjectName, pObject);
	return EM_TASK_INPROGRESS;
}


static void ***met_getFakeSubTableModel(METable *pTable, int iRow, int iSubId)
{
	if (!pTable->eaRows[iRow]->eaSubData[iSubId]->eaFakeModel) {
		eaPush(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaFakeModel, NULL);
	}
	return &pTable->eaRows[iRow]->eaSubData[iSubId]->eaFakeModel;
}


static void met_revertSubRow(METable *pTable, int iRow, int iSubId, int iSubRow)
{
	MEField **eaFields;
	MESubTable *pSubTable;
	METableSubTableData *pSubData;
	int i;

	if (!pTable->eaRows[iRow]->pOrigObject) {
		// This must be a new and unsaved object, so revert does nothing
		return;
	}

	pSubTable = pTable->eaSubTables[iSubId];
	pSubData = pTable->eaRows[iRow]->eaSubData[iSubId];

	// See if this row existed in the original
	if (pSubTable->iKeyIndex >= 0) {
		// Currently assumes the key is an "int".  Might need to add string key support at some point.
		int iKey = TokenStoreGetInt(pSubTable->pParseTable,pSubTable->iKeyIndex,pSubData->eaObjects[iSubRow],0,NULL);
		void ***peaOrigSubObjects = met_getSubTableRows(pTable, iSubId, pTable->eaRows[iRow]->pOrigObject);

		if (!eaIndexedGetUsingInt(peaOrigSubObjects, iKey)) {
			// This row does not exist in the original, so we should remove it
			ui_DialogPopup("Notice", "A newly added row cannot be reverted.  Use delete if you want to remove it.");
			return;
		}
		// We get here if the row existed in the original so it falls through to a normal field revert
	} else {
		void ***peaOrigSubObjects = met_getSubTableRows(pTable, iSubId, pTable->eaRows[iRow]->pOrigObject);
		if (eaSize(peaOrigSubObjects) <= iSubRow) {
			ui_DialogPopup("Notice", "A newly added row cannot be reverted.  Use delete if you want to remove it.");
			return;
		}
	}

	// See if any fields are modified and need reverting
	eaFields = pSubData->eaRows[iSubRow]->eaFields;
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i] && eaFields[i]->bDirty && ((pTable->eaSubTables[pTable->iMenuSubTableId]->eaCols[i]->flags & ME_STATE_NOT_REVERTABLE) == 0)) {
			MEFieldRevert(eaFields[i]);
		}
	}
}


static void met_revertRow(METable *pTable, int iRow, bool bForce)
{
	const char *pcOrigName;
	char *pcName;

	if (!bForce && !pTable->eaRows[iRow]->pOrigObject) {
		// This must be a new and unsaved object, so revert does nothing
		return;
	}

	// Get the object's name.  Copy name so we can use it after closing.
	pcOrigName = met_getOrigObjectName(pTable, iRow);
	if (pcOrigName == NULL) {
		pcOrigName = met_getObjectName(pTable, iRow);
	}
	pcName = strdup(pcOrigName);

	if (!RefSystem_ReferentFromString(pTable->hDict, pcName)) {
		// Get here if the object no longer is in the dictionary so it is
		// effectively a new object instead of an edited one, so can't revert
		free(pcName);
		return;
	}

	// Close the object
	met_closeRow(pTable, iRow, 0);

	// Re-open the object
	METableAddRow(pTable, pcName, 0, 0);
    free(pcName);

	// Move object back at proper position
	eaMove(&pTable->eaRows, iRow, eaSize(&pTable->eaRows)-1);
}


static void met_refreshFieldsFromData(METable *pTable, MEField **eaFields)
{
	int i;

	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i]) {
			MEFieldRefreshFromData(eaFields[i]);
		}
	}
}


static MEField* met_getFieldFromSelection(METable *pTable, UIListSelectionObject *pSelection)
{
	int i, j;

	// Figure out which column this is
	if (pSelection->pList == pTable->pList) {
		for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
			if (pSelection->pModel && (eaSize(pSelection->pModel) > pSelection->iRow) && 
				(pTable->eaCols[i]->pListColumn == pSelection->pColumn)) {
				return ((METableRow**)*pSelection->pModel)[pSelection->iRow]->eaFields[i];
			}
		}
	} else {
		for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
			if (pSelection->pList == pTable->eaSubTables[i]->pList) {
				for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
					if (pTable->eaSubTables[i]->eaCols[j]->pListColumn == pSelection->pColumn) {
						if (pSelection->pModel && (eaSize(pSelection->pModel) > pSelection->iRow) && 
							((METableSubRow**)*pSelection->pModel)[pSelection->iRow]) {
							return ((METableSubRow**)*pSelection->pModel)[pSelection->iRow]->eaFields[j];
						} else {
							// This happens for the fake model row
							return NULL;
						}
					}
				}
			}
		}
	}
	return NULL;
}


static void met_getSelectedFields(METable *pTable, MEField ***peaFields)
{
	UIListSelectionObject **eaSelectedCells = NULL;
	int i;

	// Get the current set of selected fields from the main list (not the list passed to this function)
	ui_ListGetSelectedCells(pTable->pList, &eaSelectedCells);

	// Scan the selection
	for(i=eaSize(&eaSelectedCells)-1; i>=0; --i) {
		MEField *pField;

		pField = met_getFieldFromSelection(pTable, eaSelectedCells[i]);
		eaPush(peaFields, pField);
	}

	// Need to destroy the selection array
	eaDestroy(&eaSelectedCells);
}

static MEField* met_getObjectFromSelection(METable *pTable, UIListSelectionObject *pSelection)
{
	int i, j;

	// Figure out which column this is
	if (pSelection->pList == pTable->pList) {
		for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
			if (pSelection->pModel && (eaSize(pSelection->pModel) > pSelection->iRow) && 
				(pTable->eaCols[i]->pListColumn == pSelection->pColumn)) {
					return pTable->eaRows[pSelection->iRow]->pObject;
			}
		}
	} else {
		for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
			if (pSelection->pList == pTable->eaSubTables[i]->pList) {
				for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
					if (pTable->eaSubTables[i]->eaCols[j]->pListColumn == pSelection->pColumn) {
						if (pSelection->pModel && (eaSize(pSelection->pModel) > pSelection->iRow) && 
							((METableSubRow**)*pSelection->pModel)[pSelection->iRow]) {
								return pTable->eaRows[pSelection->iRow]->pObject;
						} else {
							// This happens for the fake model row
							return NULL;
						}
					}
				}
			}
		}
	}
	return NULL;
}


static void met_getSelectedObjects(METable *pTable, void ***peaObjects)
{
	UIListSelectionObject **eaSelectedCells = NULL;
	int i;

	// Get the current set of selected fields from the main list (not the list passed to this function)
	ui_ListGetSelectedCells(pTable->pList, &eaSelectedCells);

	// Scan the selection
	for(i=eaSize(&eaSelectedCells)-1; i>=0; --i) {
		void *pObject;

		pObject = met_getObjectFromSelection(pTable, eaSelectedCells[i]);
		eaPush(peaObjects, pObject);
	}

	// Need to destroy the selection array
	eaDestroy(&eaSelectedCells);
}


static int met_findField(METable *pTable, MEField *pField, int *iRow, int *iCol, int *iSubId, int *iSubRow)
{
	int i, j, k, n;

	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		// Search main columns
		for(j=eaSize(&pTable->eaRows[i]->eaFields)-1; j>=0; --j) {
			if (pTable->eaRows[i]->eaFields[j] == pField) {
				*iRow = i;
				*iCol = j;
				*iSubId = -1;
				*iSubRow = -1;
				return 1;
			}
		}

		// Search sub-columns
		for(j=eaSize(&pTable->eaRows[i]->eaSubData)-1; j>=0; --j) {
			for(k=eaSize(&pTable->eaRows[i]->eaSubData[j]->eaRows)-1; k>=0; --k) {
				for(n=eaSize(&pTable->eaRows[i]->eaSubData[j]->eaRows[k]->eaFields)-1; n>=0; --n) {
					if (pTable->eaRows[i]->eaSubData[j]->eaRows[k]->eaFields[n] == pField) {
						*iRow = i;
						*iCol = n;
						*iSubId = j;
						*iSubRow = k;
						return 1;
					}
				}
			}
		}
	}
	return 0;
}


static void met_createChildObject(METable *pTable, void *pParentObject, int pos)
{
	void *pObject;
	int i;
	
	// Create the object
	pObject = pTable->cbCreateObject(pTable, pParentObject, true);

	// Add it
	if (!METableAddRowByObject(pTable, pObject, false, false)) {
		return; // Row add failed!
	}

	// Move it to be at the requested position
	eaMove(&pTable->eaRows, pos, eaSize(&pTable->eaRows) - 1);

	// Find the parent field
	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (pTable->eaCols[i]->bParentCol) {
			int j, k, n;
			METableRow *pRow = pTable->eaRows[pos];

			// Found it, so change the parent
			MEField *pField = pRow->eaFields[i];
			const char *pcParentName = 	TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pParentObject, 0, NULL);

			StructFreeString(((MEParentName*)pField->pNew)->pcParentName);
			((MEParentName*)pField->pNew)->pcParentName = StructAllocString(pcParentName);

			// Need to tag all parentable fields as parented
			for(j=eaSize(&pRow->eaFields)-1; j>=0; --j) {
				if (pRow->eaFields[j] && !pRow->eaFields[j]->bNotParentable) {
					pRow->eaFields[j]->bParented = true;
				}
			}

			for(j=eaSize(&pRow->eaSubData)-1; j>=0; --j) {
				for(k=eaSize(&pRow->eaSubData[j]->eaRows)-1; k>=0; --k) {
					METableSubRow *pSubRow = pRow->eaSubData[j]->eaRows[k];

					// Need to tag all sub-rows as inherited
					pSubRow->bInherited = true;

					// Need to tag all parentable fields as parented
					for(n=eaSize(&pSubRow->eaFields)-1; n>=0; --n) {
						if (pSubRow->eaFields[n] && !pSubRow->eaFields[n]->bNotParentable) {
							pSubRow->eaFields[n]->bParented = true;
						}
					}
				}
			}

			// Refresh, which should trigger change event
			MEFieldRefreshFromData(pField);
			break;
		}
	}
}


static void met_fieldSimpleChangeCallback(MEField *pField, bool bFinished, METable *pTable)
{
	int iRow, iCol, iSubId, iSubRow;

	if (pTable->bIgnoreChanges) {
		return;
	}

	// Find the appropriate row and column
	if (met_findField(pTable, pField, &iRow, &iCol, &iSubId, &iSubRow)) {
		if (iSubId < 0) {
			if (pTable->eaCols[iCol]->cbChange) {
				pTable->eaCols[iCol]->cbChange(pTable, pTable->eaRows[iRow]->pObject, pTable->eaCols[iCol]->pChangeUserData, false);
			}
		} else {
			if (pTable->eaSubTables[iSubId]->eaCols[iCol]->cbChange) {
				pTable->eaSubTables[iSubId]->eaCols[iCol]->cbChange(pTable, pTable->eaRows[iRow]->pObject, pTable->eaRows[iRow]->eaSubData[iSubId]->eaObjects[iSubRow], pTable->eaSubTables[iSubId]->eaCols[iCol]->pChangeUserData, false);
			}
		}

		// Test for dirty on next paint
		pTable->eaRows[iRow]->bCheckDirty = 1;
		pTable->bRefreshEditWindow = true;
	}

	return;
}


static bool met_fieldPreChangeCallback(MEField *pField, bool bFinished, METable *pTable)
{
	int iRow, iCol, iSubId, iSubRow;

	if (pTable->bIgnoreChanges) {
		return true;
	}

	// Find the appropriate row and column
	if (met_findField(pTable, pField, &iRow, &iCol, &iSubId, &iSubRow)) {
		if (!pTable->eaRows[iRow]->pOrigObject) {
			// Can always modify something that is new
			return true;
		} else {
			const char *pcName = met_getObjectName(pTable, iRow);
			ResourceInfo *resInfo = resGetInfo(pTable->hDict, pcName);
			if (resInfo && !resIsWritable(resInfo->resourceDict, resInfo->resourceName)) {
				emQueuePrompt(EMPROMPT_CHECKOUT, &pTable->pEditorDoc->emDoc, &pTable->eaRows[iRow]->pEditorSubDoc->emSubDoc, pTable->hDict, pcName);
				return false;
			}
			return true;
		}
	}
	return false;
}


static bool met_dataPreChangeTest(METable *pTable, int iRow)
{
	if (pTable->bIgnoreChanges) {
		return true;
	}

	if (!pTable->eaRows[iRow]->pOrigObject) {
		// Can always modify something that is new
		return true;
	} else {
		const char *pcName = met_getObjectName(pTable, iRow);
		ResourceInfo *resInfo = resGetInfo(pTable->hDict, pcName);
		if (resInfo && !resIsWritable(resInfo->resourceDict, resInfo->resourceName)) {
			emQueuePrompt(EMPROMPT_CHECKOUT, &pTable->pEditorDoc->emDoc, &pTable->eaRows[iRow]->pEditorSubDoc->emSubDoc, pTable->hDict, pcName);
			return false;
		}
		return true;
	}
}

static void met_dataNoLongerWritable(METable *pTable, int iRow)
{
	// This only applies to edited objects... not to newly created ones
	if (pTable->eaRows[iRow]->pOrigObject) {
		const char *pcName = met_getObjectName(pTable, iRow);
		if (emGetResourceState(pTable->pEditorDoc->emDoc.editor, pcName) == EMRES_STATE_NONE) {
			emQueuePrompt(EMPROMPT_CHECKOUT_REVERT, &pTable->pEditorDoc->emDoc, &pTable->eaRows[iRow]->pEditorSubDoc->emSubDoc, pTable->hDict, pcName);
		}
	}
}


static void met_deleteSubRow(METable *pTable, int iRow, int iSubId, int iSubRow)
{
	void ***peaData;
	void *pSubData;
	int i;

	// Clear the edit widget and selection
	met_clearEditWidget(pTable);
	ui_ListClearEverySelection(pTable->pList);

	// Save the pointer to the data
	pSubData = pTable->eaRows[iRow]->eaSubData[iSubId]->eaObjects[iSubRow];

	// Free the resources for the row
	met_destroyFieldRow(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaRows[iSubRow]->eaFields);
	eaDestroy(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaRows[iSubRow]->eaFields);
	free(pTable->eaRows[iRow]->eaSubData[iSubId]->eaRows[iSubRow]);

	// Remove the row from the table
	eaRemove(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaRows, iSubRow);
	eaRemove(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaObjects, iSubRow);

	// Remove the row from the data itself
	peaData = met_getSubTableRows(pTable, iSubId, pTable->eaRows[iRow]->pObject);
	for(i=eaSize(peaData)-1; i>=0; --i) {
		if ((*peaData)[i] == pSubData) {
			eaRemove(peaData, i);
		}
	}
}


static void met_regenerateRow(METable *pTable, int iRow) 
{
	void *pNewObject;
	void *pNewOrig = NULL;
	void *pNewParent = NULL;
	const char *pcParentName;
	int i,j;

	// Get Parent (if any)
	pcParentName = StructInherit_GetParentName(pTable->pParseTable, pTable->eaRows[iRow]->pObject);
	if (pcParentName) {
		pNewParent = RefSystem_ReferentFromString(pTable->hDict, pcParentName);
	}
			
	// Disable callbacks while modifying the data
	pTable->bIgnoreChanges = 1;

	// Delete inherited subrows if the parent doesn't have them any more
	// to avoid putting bogus override information into the inheritance
	for(i=eaSize(&pTable->eaRows[iRow]->eaSubData)-1; i>=0; --i) {
		MESubTable *pSubTable = pTable->eaSubTables[i];
		METableSubTableData *pSubData = pTable->eaRows[iRow]->eaSubData[i];

		for(j=eaSize(&pTable->eaRows[iRow]->eaSubData[i]->eaRows)-1; j>=0; --j) {
			if (pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->bInherited) {
				void *pNewSubParent = NULL;

				if (pNewParent) {
					void ***peaParentSubRows;
					int iKey, pos;

					// Get the index of this sub-row
					iKey = TokenStoreGetInt(pSubTable->pParseTable,pSubTable->iKeyIndex,pSubData->eaObjects[j],0,NULL);
					peaParentSubRows = met_getSubTableRows(pTable, i, pNewParent);

					// Find if there is a parent for this sub-row
					pos = eaIndexedFindUsingInt(peaParentSubRows, iKey);
					if (pos >= 0) {
						pNewSubParent = (*peaParentSubRows)[pos];
					}
				}

				if (!pNewSubParent) {
					// If no parent row matching, delete the sub-row so it doesn't show in inheritance data
					met_deleteSubRow(pTable, iRow, i, j);
				}
			}
		}
	}

	// Re-enable callbacks
	pTable->bIgnoreChanges = 0;

	// Put in overrides from the current data
	met_fixInheritance(pTable, iRow, false);

	// Clone the data
	pNewObject = StructCloneVoid(pTable->pParseTable, pTable->eaRows[iRow]->pObject);
	if (pTable->eaRows[iRow]->pOrigObject) {
		pNewOrig = StructCloneVoid(pTable->pParseTable, pTable->eaRows[iRow]->pOrigObject);
	}

	// Set the row to close on the next tick
	met_closeRow(pTable, iRow, true);

	// Apply the parent to the data
	if (pNewParent) {
		// Need to apply parent when in editor mode because the inheritance data from
		// the "met_fixInheritance" is based on editor mode
		void *pParentCopy = StructCloneVoid(pTable->pParseTable, pNewParent);
		langMakeEditorCopy(pTable->pParseTable, pParentCopy, true);
		StructInherit_ApplyToStruct(pTable->pParseTable, pNewObject, pParentCopy);
		StructDestroyVoid(pTable->pParseTable, pParentCopy);
	}

	// Re-Add the row
	if(pNewOrig) {
		langMakeEditorCopy(pTable->pParseTable, pNewOrig, true);
	}
	langMakeEditorCopy(pTable->pParseTable, pNewObject, true);
	met_addRow(pTable, pNewOrig, pNewObject, 0, 0);

	// Move it back to the correct location
	eaMove(&pTable->eaRows, iRow, eaSize(&pTable->eaRows)-1);
}


static void met_changeParent(METable *pTable, int iRow)
{
	const char *pcNewParentName;
	void *pNewParent = NULL;
	int i, j, k;

	pcNewParentName = StructInherit_GetParentName(pTable->pParseTable, pTable->eaRows[iRow]->pObject);
	if (pcNewParentName) {
		pNewParent = RefSystem_ReferentFromString(pTable->hDict, pcNewParentName);
		if (!pNewParent) {
			Alertf("Parent '%s' is missing for '%s'", pcNewParentName, met_getObjectName(pTable, iRow));
			return;
		}
		pNewParent = StructCloneVoid(pTable->pParseTable, pNewParent);
		langMakeEditorCopy(pTable->pParseTable, pNewParent, true);
		if (pTable->cbPostOpen) {
			pTable->cbPostOpen(pTable, pNewParent, NULL);
		}
	}

	// Ignore changes during heavy mods to avoid inappropriate callbacks
	pTable->bIgnoreChanges = true;

	// Set parent on all fields to the new parent to detect overrides (pNewParent may be NULL)
	for(i=eaSize(&pTable->eaRows[iRow]->eaFields)-1; i>=0; --i) {
		if (pTable->eaRows[iRow]->eaFields[i] && pTable->eaCols[i]->pcPTName) {
			ParseTable *pFieldParseTable;
			void *pFieldParentData;
			int iFieldCol;
			met_getFieldData(pTable->pParseTable, pTable->eaCols[i]->pcPTName, pNewParent, &pFieldParseTable, &iFieldCol, &pFieldParentData);
			MEFieldSetParent(pTable->eaRows[iRow]->eaFields[i], pFieldParentData);
		}
	}

	// If we have a parent, need to update inheritance info on sub-rows
	// If we have no parent, regenerate will cleanup
	if (pNewParent) {
		for(i=eaSize(&pTable->eaRows[iRow]->eaSubData)-1; i>=0; --i) {
			MESubTable *pSubTable = pTable->eaSubTables[i];
			METableSubTableData *pSubData = pTable->eaRows[iRow]->eaSubData[i];

			for(j=eaSize(&pTable->eaRows[iRow]->eaSubData[i]->eaRows)-1; j>=0; --j) {
				if (pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->bInherited) {
					void *pNewSubParent = NULL;

					void ***peaParentSubRows;
					int iKey, pos;

					// Get the index of this sub-row
					iKey = TokenStoreGetInt(pSubTable->pParseTable,pSubTable->iKeyIndex,pSubData->eaObjects[j],0,NULL);
					peaParentSubRows = met_getSubTableRows(pTable, i, pNewParent);

					// Find if there is a parent for this sub-row
					pos = eaIndexedFindUsingInt(peaParentSubRows, iKey);
					if (pos >= 0) {
						pNewSubParent = (*peaParentSubRows)[pos];
					}

					if (pNewSubParent) {
						// If there is a parent, reset the parent to collect inheritance data
						// If there is no parent, regenerate will clean up
						for(k=eaSize(&pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->eaFields)-1; k>=0; --k) {
							if (pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->eaFields[k] && pTable->eaSubTables[i]->eaCols[k]->pcPTName) {
								ParseTable *pFieldParseTable;
								void *pFieldParentData;
								int iFieldCol;
								met_getFieldData(pTable->eaSubTables[i]->pParseTable, pTable->eaSubTables[i]->eaCols[k]->pcPTName, pNewSubParent, &pFieldParseTable, &iFieldCol, &pFieldParentData);
								MEFieldSetParent(pTable->eaRows[iRow]->eaSubData[i]->eaRows[j]->eaFields[k], pFieldParentData);
							}
						}
					}
				}
			}
		}
	}

	StructDestroyVoid(pTable->pParseTable, pNewParent);

	// Re-enable callbacks
	pTable->bIgnoreChanges = false;

	// Regenerate the row
	met_regenerateRow(pTable, iRow);
}


static void met_fieldParentChangeCallback(MEField *pField, bool bFinished, METable *pTable)
{
	if (pTable->bIgnoreChanges) {
		return;
	}

	if (bFinished) {
		int iRow, iCol, iSubId, iSubRow;

		// Find the appropriate row and column
		if (met_findField(pTable, pField, &iRow, &iCol, &iSubId, &iSubRow)) {
			const char *pcParentName;
			char *pcNewParentName;
			void *pNewParent = NULL;
			bool bParentAdded = false;
			bool bParentRemoved = false;
			bool bParentPending = false;

			// Get old and new parent names
			pcParentName = StructInherit_GetParentName(pTable->pParseTable, pTable->eaRows[iRow]->pObject);
			pcNewParentName = ((MEParentName*)pField->pNew)->pcParentName;
			if (pcNewParentName && (pcNewParentName[0] == '\0')) {
				pcNewParentName = NULL;
			}

			if (pcNewParentName) {
				ResourceInfo *pNewParentInfo = resGetInfo(RefSystem_GetDictionaryNameFromNameOrHandle(pTable->hDict), pcNewParentName);
				pNewParent = RefSystem_ReferentFromString(pTable->hDict, pcNewParentName);
				if (!pNewParent) {
					char buf[1024];

					if (!pNewParentInfo)
					{					
						// Alert message
						sprintf(buf, "Unable to find %s \"%s\" to use as parent.  Reverting to prior parent name.", pTable->pcDisplayName, pcNewParentName);
						ui_DialogPopup("Not Found", buf);

						pNewParent = RefSystem_ReferentFromString(pTable->hDict, pcParentName);

						// Reset the name
						((MEParentName*)pField->pNew)->pcParentName = StructAllocString(pcParentName);
						StructFreeString(pcNewParentName);
						if (pNewParent)
						{					
							MEFieldRefreshFromData(pField);
						}
						return;
					}
					else
					{
						// Set state to watch for parent to arrive
						emSetResourceStateWithData(pTable->pEditorDoc->emDoc.editor, pcNewParentName, EMRES_STATE_PARENT_CHANGE, strdup(met_getObjectName(pTable,iRow)));
						resRequestOpenResource(pTable->hDict, pcNewParentName);
						bParentPending = true;
					}					
				}
			}

			if (pcParentName && pcNewParentName && (stricmp(pcParentName, pcNewParentName) != 0)) {
				// Changed from one parent to another
				bParentAdded = true;
				bParentRemoved = true;
			} else if (!pcParentName && pcNewParentName) {
				// Changed from no parent to having a parent
				bParentAdded = true;
			} else if (pcParentName && !pcNewParentName) {
				bParentRemoved = true;
			}

			if (!bParentAdded && !bParentRemoved) {
				// No change, so return
				return;
			}
			
			if (bParentRemoved) {
				StructInherit_StopInheriting(pTable->pParseTable, pTable->eaRows[iRow]->pObject);
			}
			if (bParentAdded) {
				StructInherit_StartInheriting(pTable->pParseTable, pTable->eaRows[iRow]->pObject, pcNewParentName, ParserGetFilename(pTable->pParseTable, pTable->eaRows[iRow]->pObject));
			}

			if (!bParentPending) {
				met_changeParent(pTable, iRow);
			}
			// Set pField to the new field before calling other callback function
			pField = pTable->eaRows[iRow]->eaFields[iCol];
		}
	}

	// Fire callback for the change (if any)
	met_fieldSimpleChangeCallback(pField, 1, pTable);
}


static void met_fieldFileChangeCallback(MEField *pField, bool bFinished, METable *pTable)
{
	if (pTable->bIgnoreChanges) {
		return;
	}

	// Now go check for callbacks
	met_fieldSimpleChangeCallback(pField, bFinished, pTable);
}


static void met_fieldNameChangeCallback(MEField *pField, bool bFinished, METable *pTable)
{
	int iRow, iCol, iSubId, iSubRow, i;

	if (pTable->bIgnoreChanges) {
		return;
	}
	
	if (met_findField(pTable, pField, &iRow, &iCol, &iSubId, &iSubRow)) {
		const char *pcName;

		pcName = TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pTable->eaRows[iRow]->pObject, 0, NULL);

		if (pcName) {
			ANALYSIS_ASSUME(pcName != NULL);
			if (strchr(pcName, ' ')) {
				char pcNewName[256];
				strcpy(pcNewName, pcName);
				for ( i=0; i < 256 && pcNewName[i]; i++ ) {
					if(pcNewName[i] == ' ')
						pcNewName[i] = '_';
				}
				pcName = TokenStoreSetString(pTable->pParseTable, pTable->iNameIndex, pTable->eaRows[iRow]->pObject, 0, pcNewName, NULL, NULL, NULL, NULL);
				MEFieldRefreshFromData(pField);
			}
		}
		
		if(bFinished)
		{
			// Fix up filename
			if (resFixFilename(pTable->hDict, met_getObjectName(pTable, iRow), pTable->eaRows[iRow]->pObject)) {
				// Find the file field to refresh it
				for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
					if (pTable->eaCols[i]->pcPTName && stricmp(pTable->eaCols[i]->pcPTName, pTable->pcFilePTName) == 0) {
						MEFieldRefreshFromData(pTable->eaRows[iRow]->eaFields[i]);
						break;
					}
				}
			}

			// Update sub-doc for the name change
			strcpy(pTable->eaRows[iRow]->pEditorSubDoc->emSubDoc.doc_name, pcName ? pcName : "");
			//pTable->eaRows[iRow]->pEditorSubDoc->emSubDoc.name_changed = 1;
		}
	}

	// Now go check for callbacks
	met_fieldSimpleChangeCallback(pField, bFinished, pTable);
}


static void met_fieldScopeChangeCallback(MEField *pField, bool bFinished, METable *pTable)
{
	int iRow, iCol, iSubId, iSubRow, i;

	if (pTable->bIgnoreChanges) {
		return;
	}
	
	if (met_findField(pTable, pField, &iRow, &iCol, &iSubId, &iSubRow)) {
		const char *pcScope;

		pcScope = TokenStoreGetString(pTable->pParseTable, pTable->iScopeIndex, pTable->eaRows[iRow]->pObject, 0, NULL);

		// Fix up filename
		if (resFixFilename(pTable->hDict, met_getObjectName(pTable, iRow), pTable->eaRows[iRow]->pObject)) {
			// Find the file field to refresh it
			for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
				if (pTable->eaCols[i]->pcPTName && stricmp(pTable->eaCols[i]->pcPTName, pTable->pcFilePTName) == 0) {
					MEFieldRefreshFromData(pTable->eaRows[iRow]->eaFields[i]);
					break;
				}
			}
		}
	}

	// Now go check for callbacks
	met_fieldSimpleChangeCallback(pField, bFinished, pTable);
}


static char **met_getScopeNames(METable *pTable, void *pUnused)
{
	char **eaScopeNames = NULL;
	resGetUniqueScopes(pTable->hDict, &eaScopeNames);
	return eaScopeNames;
}


static void met_scanForChangedObjects(METable *pTable)
{
	if (pTable->eaChangedObjectNames) {
		int i,j;
		const char *pcParentName, *pcName;
		const char *pcObjName, *pcObjOrigName;

		// Look for parent change requests first and process those
		for(i=eaSize(&pTable->eaChangedObjectNames)-1; i>=0; --i) {
			pcParentName = pTable->eaChangedObjectNames[i];
			if (emGetResourceState(pTable->pEditorDoc->emDoc.editor, pcParentName) == EMRES_STATE_PARENT_CHANGE) {
				char *pcChildName = emGetResourceStateData(pTable->pEditorDoc->emDoc.editor, pcParentName);
				for (j=eaSize(&pTable->eaRows)-1; j>=0; --j) {
					const char *pcRowName = met_getObjectName(pTable, j);
					if (stricmp(pcRowName, pcChildName) == 0) {
						met_changeParent(pTable, j);
					}
				}
				free(pcChildName);
				emSetResourceState(pTable->pEditorDoc->emDoc.editor, pcParentName, EMRES_STATE_NONE);
			}
		}

		// Then process all other change requests
		for(i=eaSize(&pTable->eaChangedObjectNames)-1; i>=0; --i) {
			bool bRowFound = false;
			pcName = pTable->eaChangedObjectNames[i];

			// Clear working copy (if any) for this object
			if (resEditGetWorkingCopy(pTable->hDict, pcName)) {
				resEditRevertAllModifications(pTable->hDict);
			}

			// See if the changed object is one that is currently open for editing
			// Or is a parent of one that is open for editing
			for(j=eaSize(&pTable->eaRows)-1; j>=0; --j) {
				pcObjName = met_getObjectName(pTable,j);
				pcObjOrigName = met_getOrigObjectName(pTable,j);
				pcParentName = allocAddString(StructInherit_GetParentName(pTable->pParseTable, pTable->eaRows[j]->pObject));

				if ((pcObjOrigName && stricmp(pcName, pcObjOrigName) == 0) ||
					(!pcObjOrigName && stricmp(pcName, pcObjName) == 0)) {
					// An open object was modified in the dictionary
					// Test orig name to avoid messing with newly created objects
					// Renamed objects will get found but turn up dirty
					if (pTable->eaRows[j]->bSaved || !met_isRowDirty(pTable, j)) {
						// If it is saved or not dirty, then reload it from the dictionary
						met_revertRow(pTable, j, 1);
						bRowFound = true;
					} else {
						// If it is unsaved and dirty, check if it actually changed
						// Get the new version from the dictionary and fix it up
						void *pUpdatedObject = RefSystem_ReferentFromString(pTable->hDict, pcObjOrigName);
						void *pUpdatedClone = StructCloneVoid(pTable->pParseTable, pUpdatedObject);
						if (pUpdatedObject && pUpdatedClone) {
							langMakeEditorCopy(pTable->pParseTable, pUpdatedClone, true);
							if (pTable->cbPostOpen) {
								pTable->cbPostOpen(pTable, pUpdatedClone, NULL);
							}

							// If it is different, prompt for revert or continue
							if ((StructCompare(pTable->pParseTable, pTable->eaRows[j]->pOrigObject, pUpdatedClone, 0, 0, 0) != 0) &&
								(StructCompare(pTable->pParseTable, pTable->eaRows[j]->pObject, pUpdatedClone, 0, 0, 0) != 0)) {
								emQueuePrompt(EMPROMPT_REVERT_CONTINUE, &pTable->pEditorDoc->emDoc, &pTable->eaRows[j]->pEditorSubDoc->emSubDoc, pTable->hDict, pcObjName);
							}

							// Clean up
							StructDestroyVoid(pTable->pParseTable, pUpdatedClone);
						}
					}
				}

				if (pcParentName && stricmp(pcName, pcParentName) == 0) {
					// The parent of an open object was modified in the dictionary
					if (pTable->eaRows[j]->bSaved || !met_isRowDirty(pTable, j)) {
						// If it is not dirty, then reload it from the dictionary
						met_revertRow(pTable, j, 1);
					} else {
						// If it is modified, then regenerate it
						met_regenerateRow(pTable, j);
					}
				}
			}
		}

		eaDestroy(&pTable->eaChangedObjectNames);
		pTable->eaChangedObjectNames = NULL;
	}

	// This is used for indirect
	if (pTable->eaMessageChangedObjectNames) {
		int i,j;
		const char *pcParentName, *pcName;
		const char *pcObjName, *pcObjOrigName;

		// Look for ones we can refresh the message for
		for(i=eaSize(&pTable->eaMessageChangedObjectNames)-1; i>=0; --i) {
			bool bRowFound = false;
			pcName = pTable->eaMessageChangedObjectNames[i];
			if (!pcName) {
				continue;
			}

			// Clear working copy (if any) for this object
			if (resEditGetWorkingCopy(pTable->hDict, pcName)) {
				resEditRevertAllModifications(pTable->hDict);
			}

			// See if the changed object is one that is currently open for editing
			// Or is a parent of one that is open for editing
			for(j=eaSize(&pTable->eaRows)-1; j>=0; --j) {
				pcObjName = met_getObjectName(pTable,j);
				pcObjOrigName = met_getOrigObjectName(pTable,j);
				pcParentName = allocAddString(StructInherit_GetParentName(pTable->pParseTable, pTable->eaRows[j]->pObject));

				if ((pcObjOrigName && stricmp(pcName, pcObjOrigName) == 0) ||
					(!pcObjOrigName && stricmp(pcName, pcObjName) == 0)) {
					// An open object was modified in the dictionary
					// Test orig name to avoid messing with newly created objects
					// Renamed objects will get found but turn up dirty
					if (pTable->eaRows[j]->bSaved || !met_isRowDirty(pTable, j)) {
						// If it is saved or not dirty, then reload it from the dictionary
						met_revertRow(pTable, j, 1);
						bRowFound = true;
					} else {
						// If it is unsaved and dirty, check if it actually changed
						// Get the new version from the dictionary and fix it up
						void *pUpdatedObject = RefSystem_ReferentFromString(pTable->hDict, pcObjOrigName);
						void *pUpdatedClone = StructCloneVoid(pTable->pParseTable, pUpdatedObject);
						if (pUpdatedObject && pUpdatedClone) {
							langMakeEditorCopy(pTable->pParseTable, pUpdatedClone, true);
							if (pTable->cbPostOpen) {
								pTable->cbPostOpen(pTable, pUpdatedClone, NULL);
							}

							// Clean up
							StructDestroyVoid(pTable->pParseTable, pUpdatedClone);
						}
					}
				}

				if (pcParentName && stricmp(pcName, pcParentName) == 0) {
					// The parent of an open object was modified in the dictionary
					if (pTable->eaRows[j]->bSaved || !met_isRowDirty(pTable, j)) {
						// If it is not dirty, then reload it from the dictionary
						met_revertRow(pTable, j, 1);
					} else {
						// If it is modified, then regenerate it
						met_regenerateRow(pTable, j);
					}
				}
			}
		}

		eaDestroy(&pTable->eaMessageChangedObjectNames);
		pTable->eaMessageChangedObjectNames = NULL;
	}
}


static void met_checkSmartHidden(METable *pTable)
{
	int i, j, k, n;
	bool bClear = false;

	if (!pTable->bCheckSmartHidden) {
		return;
	}
	pTable->bCheckSmartHidden = false;

	// Check all main columns
	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		MEColData *pCol = pTable->eaCols[i];
		bool bHide = (pCol->pcGroup != NULL);
		for(j=eaSize(&pTable->eaRows)-1; j>=0 && bHide; --j) {
			if ((eaSize(&pTable->eaRows[j]->eaFields) > i) &&
				(pTable->eaRows[j]->eaFields[i]) &&
				(!pTable->eaRows[j]->eaFields[i]->bNotApplicable)) {
				bHide = false;
				break;
			}
		}

		pCol->bSmartHidden = bHide && pTable->bHideUnused;
		if (IS_HIDDEN(pCol) != !!pCol->pListColumn->bHidden) { // The !! is to deal with bitfield boolean
			pCol->pListColumn->bHidden = IS_HIDDEN(pCol);
			bClear = true;
		}
	}

	// Check sub-columns
	for(n=eaSize(&pTable->eaSubTables)-1; n>=0; --n) {
		MESubTable *pSubTable = pTable->eaSubTables[n];
		for(i=eaSize(&pSubTable->eaCols)-1; i>=0; --i) {
			MESubColData *pCol = pSubTable->eaCols[i];
			bool bHide = (pCol->pcGroup != NULL);
			for(j=eaSize(&pTable->eaRows)-1; j>=0 && bHide; --j) {
				if (eaSize(&pTable->eaRows[j]->eaSubData) > n) {
					for(k=eaSize(&pTable->eaRows[j]->eaSubData[n]->eaRows)-1; k>=0; --k) {
						if ((eaSize(&pTable->eaRows[j]->eaSubData[n]->eaRows[k]->eaFields) > i) &&
							(pTable->eaRows[j]->eaSubData[n]->eaRows[k]->eaFields[i]) &&
							(!pTable->eaRows[j]->eaSubData[n]->eaRows[k]->eaFields[i]->bNotApplicable)) {
							bHide = false;
							break;
						}
					}
				}
			}
			pCol->bSmartHidden = bHide && pTable->bHideUnused;
			if (IS_HIDDEN(pCol) != !!pCol->pListColumn->bHidden) { // The !! is to deal with bitfield boolean
				pCol->pListColumn->bHidden = IS_HIDDEN(pCol);
				bClear = true;
			}
		}
	}

	if (bClear) {
		met_clearEditWidget(pTable);
		ui_ListClearEverySelection(pTable->pList);
	}
}


static void met_checkWritable(METable *pTable)
{
	ResourceInfo *pResInfo;
	int i;

	// Check all rows
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		// Check if row is dirty
		if (pTable->eaRows[i]->bCheckDirty) {
			pTable->eaRows[i]->bDirty = met_isRowDirty(pTable, i);
			pTable->eaRows[i]->bCheckDirty = 0;
			pTable->eaRows[i]->pEditorSubDoc->emSubDoc.saved = !pTable->eaRows[i]->bDirty;
		}

		// Check if row needs writable alert
		pResInfo = resGetInfo(pTable->hDict, met_getObjectName(pTable, i));
		if (pResInfo && !resIsWritable(pResInfo->resourceDict, pResInfo->resourceName) && pTable->eaRows[i]->bDirty) {
			met_dataNoLongerWritable(pTable, i);
		}
	}
}


//---------------------------------------------------------------------------------------------------
// UI Callbacks
//---------------------------------------------------------------------------------------------------


static void met_UISubModelChange(UIList *pList, UIList *pSubList, S32 row, METable *pTable)
{
	int i;

	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		if (pTable->eaSubTables[i]->pList == pSubList) {
			if ((pTable->eaRows[row]->eExpand == ME_EXPAND) &&
			    !pTable->eaRows[row]->eaSubData[i]->bHidden &&
				!pTable->eaSubTables[i]->bHidden) {
				// Expanded
				if (eaSize(&pTable->eaRows[row]->eaSubData[i]->eaRows) == 0) {
					// We have no real rows, so use the fake subtable row
					ui_ListSetModel(pSubList, NULL, met_getFakeSubTableModel(pTable, row, i));
				} else {
					// We have real rows, so use them
					ui_ListSetModel(pSubList, NULL, &pTable->eaRows[row]->eaSubData[i]->eaRows);
				}
				return;
			} else {
				// Collapsed or hidden
				ui_ListSetModel(pSubList,NULL,NULL);
				return;
			}
		}
	}

	// No matching sublist found
	ui_ListSetModel(pSubList,NULL,NULL);
}


static void met_drawGimmeIcon(int bReadOnly, F32 x, F32 y, F32 z, F32 scale)
{
	AtlasTex *pTex = atlasLoadTexture(bReadOnly?"eui_gimme_readonly":"eui_gimme_ok");
	display_sprite(pTex, x, y, z, scale, scale, COLOR_GIMME_ICON);
}


static void met_makeMenuBox(CBox *pOuterBox, AtlasTex *pTex, float scale, CBox *pIconBox)
{
	pIconBox->left  = (pOuterBox->left + pOuterBox->right) / 2 - (7 * scale);
	pIconBox->right = (pOuterBox->left + pOuterBox->right) / 2 + (7 * scale);

	pIconBox->top    = (pOuterBox->top + pOuterBox->bottom) / 2 - (8 * scale);
	pIconBox->bottom = (pOuterBox->top + pOuterBox->bottom) / 2 + (7 * scale);
}


static void met_drawMenuBox(F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale)
{
	CBox box, menuBox;
	AtlasTex *pTex;
	UIStyleBorder *pBorder = GET_REF(g_hBorder);

	BuildCBox(&box, x, y, w, h);
	pTex = (g_ui_Tex.arrowDropDown);
	met_makeMenuBox(&box, pTex, scale, &menuBox);

	pBorder = GET_REF(g_hBorder);
	ui_StyleBorderDraw(pBorder, &menuBox, RGBAFromColor(gColorMenuInner), RGBAFromColor(gColorMenuInner), z, scale, 255);

	menuBox.left += 3;
	menuBox.top += 4;
	menuBox.right -= 3;
	menuBox.bottom -= 3;

	display_sprite_box(pTex, &menuBox, z+.01, COLOR_INT_MENU_ICON);
}



static void met_UIDrawIcon(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, METable *pTable)
{
	ResourceInfo *resInfo;

	// Display dirty status if required
	if (pTable->eaRows[index]->bDirty && UI_GET_SKIN(pList)) {
		CBox box;
		Color c = ColorLerp(gColorMainBackground, UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	// Place Gimme icon in the main column
	resInfo = resGetInfo(pTable->hDict, met_getObjectName(pTable, index));
	met_drawGimmeIcon(resInfo && !resIsWritable(resInfo->resourceDict, resInfo->resourceName), x+(5.0*scale), y+(4.0*scale), z, scale);

	x += GIMME_ICON_OFFSET * scale; w -= GIMME_ICON_OFFSET * scale;

	// Place menu button image
	met_drawMenuBox(x, y, w, h, z, scale);
}

static void met_UIDrawSubIcon(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, METable *pTable)
{
	met_drawMenuBox(x, y, w, h, z, scale);
}

F32 fAltTint = 1;
AUTO_CMD_FLOAT(fAltTint,MultiEditAltTint) ACMD_HIDE;

static void met_UIDrawField(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, MEColData *pColData)
{
	MEField *pField;
	
	pField = pColData->pTable->eaRows[index]->eaFields[pColData->iColNum];

	if (!pField || pField->bNotApplicable) {
		if (!pField && (pColData->flags & ME_STATE_LABEL)) {
			CBox box;
			w -= 3;
			BuildCBox(&box, x, y, w, h);
			clipperPushRestrict(&box);
			x += 3; w -=3;
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", ui_ListColumnGetTitle(pColData->pListColumn));
			clipperPop();
		} else {
			gfxDrawLine(x, y, z, x+w, y+h-1, gColorMenuIcon);
			gfxDrawLine(x, y+h-1, z, x+w, y, gColorMenuIcon);
		}
	} else if (pField == pColData->pTable->pEditField) {
		// Set the widget location if it is editable in case of resize
		ui_WidgetSetCBox(pColData->pTable->pEditField->pUIWidget,pBox);
	} else {
		// Draw the field if it isn't editable
		if(pColData->bGroupAltTint) {
			Color c = ColorDarkenPercent(gColorMainBackground, fAltTint);
			MEDrawField(pField,x,y,w,h,scale,z,&c,UI_GET_SKIN(pList),true);
		}
		else {
			MEDrawField(pField,x,y,w,h,scale,z,&gColorMainBackground,UI_GET_SKIN(pList),false);
		}
	}
}


static void met_UIDrawSubField(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, MESubColData *pSubColData)
{
	int parentIndex;
	MEField *pField = NULL;

	parentIndex = ui_ListGetActiveParentRow(pList);

	if (index < eaSize(&pSubColData->pTable->eaRows[parentIndex]->eaSubData[pSubColData->iSubTableId]->eaRows)) {
		pField = pSubColData->pTable->eaRows[parentIndex]->eaSubData[pSubColData->iSubTableId]->eaRows[index]->eaFields[pSubColData->iColNum];
	}

	if (!pField || pField->bNotApplicable) {
		if (!pField && (pSubColData->flags & ME_STATE_LABEL)) {
			CBox box;
			w -= 3;
			BuildCBox(&box, x, y, w, h);
			clipperPushRestrict(&box);
			x += 3; w -=3;
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", ui_ListColumnGetTitle(pSubColData->pListColumn));
			clipperPop();
		} else {
			gfxDrawLine(x, y, z, x+w, y+h-1, gColorMenuIcon);
			gfxDrawLine(x, y+h-1, z, x+w, y-1, gColorMenuIcon);
		}
	} else if (pField == pSubColData->pTable->pEditField) {
		// Set the widget location if it is editable in case of resize
		ui_WidgetSetCBox(pSubColData->pTable->pEditField->pUIWidget,pBox);
	} else {
		// Draw the field if it isn't editable
		if(pSubColData->bGroupAltTint) {
			Color c = ColorDarkenPercent(gColorSublistBackground, fAltTint);
			MEDrawField(pField,x,y,w,h,scale,z,&c,UI_GET_SKIN(pList),true);
		}
		else {
			MEDrawField(pField,x,y,w,h,scale,z,&gColorSublistBackground,UI_GET_SKIN(pList),false);
		}
	}
}


static void met_updateEditControl(METable *pTable, int iClickType, UIList *pList, MEField *pClickedField, int iRow, int iParentRow, int iSubTableId, int iColumn, CBox *pBox)
{
	MEField **eaFields = NULL;

	// Get the current set of selected fields from the main list (not the list passed to this function)
	met_getSelectedFields(pTable, &eaFields);

	if (eaSize(&eaFields) != 1) {
		// If more than one field selected or zero selected, none of them are editable
		if (pTable->pEditWidget) {
			met_clearEditWidget(pTable);
		}

		// On right click pull up context menu
		if ((iClickType == MET_CLICK_RIGHT) && (eaSize(&eaFields) > 1)) {
			MEFieldShowMultiFieldMenu(eaFields);
		}
	} else if (!pClickedField) {
		// Selection is not due to a click, so clear editing
		if (pTable->pEditWidget) {
			met_clearEditWidget(pTable);
		}
	} else {
		MEField *pSelectedField;

		// Get the field that is currently selected
		pSelectedField = eaFields[0];

		// If clicked and selected are different, then it was a deselect that caused the single
		// selection, so do nothing at this time
		if (pClickedField == pSelectedField) {
			// Exactly one selected so make it editable if not already
			if (pTable->pEditField != pSelectedField) {
				// Switch to new field
				met_clearEditWidget(pTable);

				// Display the edit widget if the field is editable
				if (pSelectedField && pSelectedField->bEditable && !pSelectedField->bNotApplicable) {
					// If this is a data-provided column, reload the names array before displaying it
					if (pList == pTable->pList) {
						if (pTable->eaCols[iColumn]->cbDataFunc) {
							int i;
							for(i=eaSize(&pTable->eaCols[iColumn]->eaDictNames)-1; i>=0; --i) {
								free(pTable->eaCols[iColumn]->eaDictNames[i]);
							}
							eaClear(&pTable->eaCols[iColumn]->eaDictNames);
							pTable->eaCols[iColumn]->eaDictNames = pTable->eaCols[iColumn]->cbDataFunc(pTable, pTable->eaRows[iRow]->pObject);
							eaStableSort(pTable->eaCols[iColumn]->eaDictNames, NULL, met_compareStrings);
						}
					} else if (iSubTableId >=0) {
						if (pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->cbDataFunc) {
							int i;
							for(i=eaSize(&pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->eaDictNames)-1; i>=0; --i) {
								free(pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->eaDictNames[i]);
							}
							eaClear(&pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->eaDictNames);
							pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->eaDictNames = pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->cbDataFunc(pTable, pTable->eaRows[iParentRow]->eaSubData[iSubTableId]->eaObjects[iRow]);
							eaStableSort(pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->eaDictNames, NULL, met_compareStrings);
						}
					}

					MEFieldDisplay(pSelectedField,(UIWidget*)pTable->pList,pBox);
					pTable->pEditWidget = pSelectedField->pUIWidget;
					pTable->pEditField = pSelectedField;
					if (iSubTableId >= 0) {
						MESubColData *pCol = pTable->eaSubTables[iSubTableId]->eaCols[iColumn];
						emStatusPrintf("Editing %s \"%s\", %s #%d, field \"%s\"", pTable->pcDisplayName, met_getObjectName(pTable, iParentRow), pTable->eaSubTables[iSubTableId]->pcDisplayName, iRow+1, ui_ListColumnGetTitle(pTable->eaSubTables[iSubTableId]->eaCols[iColumn]->pListColumn));
						if (pCol->pFileData) {
							ui_FileNameEntrySetBrowseValues(pTable->pEditField->pUIFileName, pCol->pFileData->pcBrowseTitle, pCol->pFileData->pcTopDir, pCol->pFileData->pcStartDir, pCol->pFileData->pcExtension, pCol->pFileData->eMode);
						}
					} else {
						MEColData *pCol = pTable->eaCols[iColumn];
						emStatusPrintf("Editing %s \"%s\" field \"%s\"", pTable->pcDisplayName, met_getObjectName(pTable, iRow), ui_ListColumnGetTitle(pTable->eaCols[iColumn]->pListColumn));
						if (pCol->pFileData) {
							ui_FileNameEntrySetBrowseValues(pTable->pEditField->pUIFileName, pCol->pFileData->pcBrowseTitle, pCol->pFileData->pcTopDir, pCol->pFileData->pcStartDir, pCol->pFileData->pcExtension, pCol->pFileData->eMode);
						}
					}
				}
			}

			// On right click pull up context menu
			if (iClickType == MET_CLICK_RIGHT) {
				MEFieldShowMenu(pSelectedField);
			}
		}
	}

	// Need to destroy the fields array
	eaDestroy(&eaFields);
}


static void met_cellClickAction(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, METable *pTable, int iClickType)
{
	MEField *pClickedField = NULL;
	int iParentRow = -1, iSubTableId = -1;
	int i;
			
	// Capture parent row in case we need it
	// We can't get it later since call-ins destroy this information
	if (pList != pTable->pList) {
		iParentRow = ui_ListGetActiveParentRow(pList);
	}

	// Get the field that was just clicked on in case they are different
	if (pList == pTable->pList) {
		pClickedField = pTable->eaRows[iRow]->eaFields[iColumn];
	} else {
		for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
			if (pTable->eaSubTables[i]->pList == pList) {
				if (iRow < eaSize(&pTable->eaRows[iParentRow]->eaSubData[i]->eaRows)) {
					pClickedField = pTable->eaRows[iParentRow]->eaSubData[i]->eaRows[iRow]->eaFields[iColumn];
				} else {
					pClickedField = NULL; // Happens for fake model
				}
				iSubTableId = i;
			}
		}
	}

	if (iClickType == MET_CLICK_RIGHT) {
		MEField **eaFields = NULL;

		// Get the current set of selected fields from the main list (not the list passed to this function)
		met_getSelectedFields(pTable, &eaFields);

		// Check if right clicked on an already selected field
		for(i=eaSize(&eaFields)-1; i>=0; --i) {
			if (eaFields[i] == pClickedField) {
				// Pull up rclick menu instead of treating as a click or modifying edit control
				MEFieldShowMultiFieldMenu(eaFields);
				return;
			}
		}

		if (iParentRow >= 0) {
			// Reset the submodel since it was modified by getting the selected fields
			met_UISubModelChange(pTable->pList, pList, iParentRow, pTable);
		}
	} 

	// Pass back to the list to have it modify the current selection
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pTable);

	// Update the edit control
	met_updateEditControl(pTable, iClickType, pList, pClickedField, iRow, iParentRow, iSubTableId, iColumn, pBox);
}


static void met_UICellLClickCallback(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, METable *pTable)
{
	// Don't select cells in the menu column
	if (iColumn > 0)
	{
		if (pTable->pLastListClicked != NULL)
		{
			ui_ListClearSelectedRows(pTable->pLastListClicked);
			ui_ListSelectionStateClear(ui_ListGetSelectionState(pTable->pLastListClicked));
		}
		pTable->pLastListClicked = NULL;
		met_cellClickAction(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pTable, MET_CLICK_LEFT);
	}
	else if (iColumn == 0)
	{
		CBox iconBox, boundBox;
		AtlasTex *pTex;

		//Row selection
		if (pTable->pLastListClicked != pList)
		{
			if (pTable->pLastListClicked != NULL)
			{
				ui_ListClearSelectedRows(pTable->pLastListClicked);
				ui_ListSelectionStateClear(ui_ListGetSelectionState(pTable->pLastListClicked));
			}
			pTable->pLastListClicked = pList;
		}
		ui_ListSelectOrDeselectRowEx(pList, iRow, inpLevelPeek(INP_SHIFT), inpLevelPeek(INP_CONTROL));

		// Check for click on the menu icon
		boundBox = *pBox;
		if (pList == pTable->pList)
		{
			boundBox.left += GIMME_ICON_OFFSET;
		}
		pTex = (g_ui_Tex.arrowDropDown);
		met_makeMenuBox(&boundBox,pTex,1.0,&iconBox);

		if ((fMouseX >= iconBox.left - pBox->left) && (fMouseX <= iconBox.right - pBox->left) &&
			(fMouseY >= iconBox.top - pBox->top) && (fMouseY <= iconBox.bottom - pBox->top))
		{
			int i;
			bool bMultiSelect;
			ui_ListSelectRow(pList, iRow);	//Sometimes this row winds up not selected. Not sure why. This ensures that it is.
			bMultiSelect = ((ea32Size(ui_ListGetSelectedSubRowsSorted(pList))) > 1);

			// Clicked in the menu icon, so pull up the appropriate menu
			if (pList == pTable->pList)
			{
				bool bWritable = false;

				pTable->iMenuTableRow = iRow;
				pTable->iMenuSubTableRow = -1;
				pTable->iMenuSubTableId = -1;

				if (pTable->eaRows[iRow]->pOrigObject)
				{
					ResourceInfo *resInfo = resGetInfo(pTable->hDict, met_getObjectName(pTable, iRow));
					if (resInfo)
					{
						bWritable = resIsWritable(resInfo->resourceDict, resInfo->resourceName);
					}
					else
					{
						resInfo = resGetInfo(pTable->hDict, met_getOrigObjectName(pTable, iRow));
						if (resInfo)
						{
							bWritable = resIsWritable(resInfo->resourceDict, resInfo->resourceName);
						}
						else
						{
							bWritable = true;
						}
					}
				}

				// Disable options as appropriate
				if (bMultiSelect)
				{
					const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
					int numSelectedRows = eaiSize(eaiRows);
					bool allSelected = ((eaSize(&pTable->eaRows)) == numSelectedRows);

					pTable->pTableMenu->items[MENU_MOVE_UP_INDEX]->active = ((!allSelected) && (((*eaiRows)[numSelectedRows-1]) > (numSelectedRows-1)));
					pTable->pTableMenu->items[MENU_MOVE_DOWN_INDEX]->active = ((!allSelected) && ((((*eaiRows)[0]) + numSelectedRows) < (eaSize(&pTable->eaRows))));
					pTable->pTableMenu->items[MENU_MOVE_TOP_INDEX]->active = ((!allSelected) && (((*eaiRows)[numSelectedRows-1]) > (numSelectedRows-1)));
					pTable->pTableMenu->items[MENU_MOVE_BOTTOM_INDEX]->active = ((!allSelected) && ((((*eaiRows)[0]) + numSelectedRows) < (eaSize(&pTable->eaRows))));
					pTable->pTableMenu->items[MENU_REVERT_INDEX]->active = true;							//Checks in loop below
					pTable->pTableMenu->items[MENU_FIND_USAGE_INDEX]->active = false;
					pTable->pTableMenu->items[MENU_LIST_REFERENCES_INDEX]->active = false;
					pTable->pTableMenu->items[MENU_CHECK_OUT_INDEX]->active = (!bWritable);					//Checks in loop below
					pTable->pTableMenu->items[MENU_UNDO_CHECK_OUT_INDEX]->active = (bWritable);				//Checks in loop below
					pTable->pTableMenu->items[MENU_VALIDATE_INDEX]->active = (pTable->cbValidate != NULL);
					pTable->pTableMenu->items[MENU_CREATE_CHILD_INDEX]->active = (pTable->bHasParentCol);	//Checks in loop below
					pTable->pTableMenu->items[MENU_SAVE_INDEX]->active = bWritable;							//Checks in loop below
					pTable->pTableMenu->items[MENU_OPEN_FILE_INDEX]->active = false;
					pTable->pTableMenu->items[MENU_OPEN_FOLDER_INDEX]->active = false;

					for (i = 0; i < eaiSize(eaiRows); i++)
					{
						if (pTable->eaRows[(*eaiRows)[i]]->pOrigObject == NULL)
						{
							pTable->pTableMenu->items[MENU_REVERT_INDEX]->active = false;
							pTable->pTableMenu->items[MENU_CHECK_OUT_INDEX]->active = false;
							pTable->pTableMenu->items[MENU_UNDO_CHECK_OUT_INDEX]->active = false;
							pTable->pTableMenu->items[MENU_CREATE_CHILD_INDEX]->active = false;
							pTable->pTableMenu->items[MENU_SAVE_INDEX]->active = true;
						}
					}
				}
				else
				{
					pTable->pTableMenu->items[MENU_MOVE_UP_INDEX]->active = (iRow > 0);
					pTable->pTableMenu->items[MENU_MOVE_DOWN_INDEX]->active = (iRow < eaSize(&pTable->eaRows)-1);
					pTable->pTableMenu->items[MENU_MOVE_TOP_INDEX]->active = (iRow > 0);
					pTable->pTableMenu->items[MENU_MOVE_BOTTOM_INDEX]->active = (iRow < eaSize(&pTable->eaRows)-1);
					pTable->pTableMenu->items[MENU_REVERT_INDEX]->active = (pTable->eaRows[iRow]->pOrigObject != NULL);
					pTable->pTableMenu->items[MENU_FIND_USAGE_INDEX]->active = (pTable->hDict) && (pTable->eaRows[iRow]->pOrigObject != NULL);
					pTable->pTableMenu->items[MENU_LIST_REFERENCES_INDEX]->active = (pTable->hDict) && (pTable->eaRows[iRow]->pOrigObject != NULL);
					pTable->pTableMenu->items[MENU_CHECK_OUT_INDEX]->active = (pTable->eaRows[iRow]->pOrigObject) && (!bWritable);
					pTable->pTableMenu->items[MENU_UNDO_CHECK_OUT_INDEX]->active = (pTable->eaRows[iRow]->pOrigObject) && (bWritable);
					pTable->pTableMenu->items[MENU_VALIDATE_INDEX]->active = (pTable->cbValidate != NULL);
					pTable->pTableMenu->items[MENU_CREATE_CHILD_INDEX]->active = (pTable->bHasParentCol) && (pTable->eaRows[iRow]->pOrigObject != NULL);
					pTable->pTableMenu->items[MENU_SAVE_INDEX]->active = ((pTable->eaRows[iRow]->pOrigObject == NULL) || bWritable);
				}

				pTable->pTableMenu->widget.scale = emGetEditorScale(pTable->pEditorDoc->emDoc.editor);
				ui_MenuPopupAtCursor(pTable->pTableMenu);
			}
			else
			{
				char buf[256];
				bool bIsIndexed;

				pTable->iMenuTableRow = ui_ListGetActiveParentRow(pList);
				pTable->iMenuSubTableRow = iRow;
				for (i = eaSize(&pTable->eaSubTables) - 1; i >= 0; --i)
				{
					if (pTable->eaSubTables[i]->pList == pList)
					{
						pTable->iMenuSubTableId = i;
						break;
					}
				}

				// Disable options as appropriate
				bIsIndexed = (eaIndexedGetTable(met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject)) != NULL);

				if (bMultiSelect)
				{
					const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
					int numSelectedRows = eaiSize(eaiRows);
					bool allSelected = ((eaSize(met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject))) == numSelectedRows);

					pTable->pSubTableMenu->items[SUBMENU_MOVE_UP_INDEX]->active = ((!allSelected) && (((*eaiRows)[numSelectedRows-1]) > (numSelectedRows-1)));
					pTable->pSubTableMenu->items[SUBMENU_MOVE_DOWN_INDEX]->active = ((!allSelected) && ((((*eaiRows)[0]) + numSelectedRows) <
						(eaSize(met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject)))));
					pTable->pSubTableMenu->items[SUBMENU_MOVE_TOP_INDEX]->active = ((!allSelected) && (((*eaiRows)[numSelectedRows-1]) > (numSelectedRows-1)));
					pTable->pSubTableMenu->items[SUBMENU_MOVE_BOTTOM_INDEX]->active = ((!allSelected) && ((((*eaiRows)[0]) + numSelectedRows) <
						(eaSize(met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject)))));
					pTable->pSubTableMenu->items[SUBMENU_EDIT_ROW_INDEX]->active = false;		//Feature not yet added
					pTable->pSubTableMenu->items[SUBMENU_REVERT_INDEX]->active = true;			//Checks in loop below
					pTable->pSubTableMenu->items[SUBMENU_DELETE_INDEX]->active = true;			//Checks in loop below
					pTable->pSubTableMenu->items[SUBMENU_NEW_INDEX]->active = (pTable->eaSubTables[pTable->iMenuSubTableId]->cbSubCreate != NULL);
					pTable->pSubTableMenu->items[SUBMENU_CLONE_INDEX]->active = (pTable->eaSubTables[pTable->iMenuSubTableId]->cbSubCreate != NULL);	//Checks in loop below
					pTable->pSubTableMenu->items[SUBMENU_COPY_INDEX]->active = (pTable->eaSubTables[pTable->iMenuSubTableId]->cbSubCreate != NULL);		//Checks in loop below
					pTable->pSubTableMenu->items[SUBMENU_PASTE_INDEX]->active = (eaSize(&pTable->pPasteObject) > 0) && (pTable->iPasteSubTableId == pTable->iMenuSubTableId);

					for (i = 0; i < eaiSize(eaiRows); i++)
					{
						if (((*eaiRows)[i]) >= eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows))
						{
							pTable->pSubTableMenu->items[SUBMENU_DELETE_INDEX]->active = false;
							pTable->pSubTableMenu->items[SUBMENU_CLONE_INDEX]->active = false;
							pTable->pSubTableMenu->items[SUBMENU_COPY_INDEX]->active = false;
							pTable->pSubTableMenu->items[SUBMENU_REVERT_INDEX]->active = false;
						}
						else if (pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows[(*eaiRows)[i]]->bInherited)
						{
							pTable->pSubTableMenu->items[SUBMENU_DELETE_INDEX]->active = false;
						}
					}
				}
				else
				{
					pTable->pSubTableMenu->items[SUBMENU_MOVE_UP_INDEX]->active = (iRow > 0) &&
						(pTable->eaSubTables[pTable->iMenuSubTableId]->cbReorder || !bIsIndexed);
					pTable->pSubTableMenu->items[SUBMENU_MOVE_DOWN_INDEX]->active = (iRow < eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows)-1) &&
						(pTable->eaSubTables[pTable->iMenuSubTableId]->cbReorder || !bIsIndexed);
					pTable->pSubTableMenu->items[SUBMENU_MOVE_TOP_INDEX]->active = (iRow > 0) &&
						(pTable->eaSubTables[pTable->iMenuSubTableId]->cbReorder || !bIsIndexed);
					pTable->pSubTableMenu->items[SUBMENU_MOVE_BOTTOM_INDEX]->active = (iRow < eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows)-1) &&
						(pTable->eaSubTables[pTable->iMenuSubTableId]->cbReorder || !bIsIndexed);
					pTable->pSubTableMenu->items[SUBMENU_EDIT_ROW_INDEX]->active = (iRow < eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows)) &&
						(pTable->eaSubTables[pTable->iMenuSubTableId]->cbSubCreate != NULL);
					pTable->pSubTableMenu->items[SUBMENU_REVERT_INDEX]->active = (iRow < eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows));
					pTable->pSubTableMenu->items[SUBMENU_DELETE_INDEX]->active = (iRow < eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows)) && 
						(!pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows[pTable->iMenuSubTableRow]->bInherited);
					pTable->pSubTableMenu->items[SUBMENU_NEW_INDEX]->active = (pTable->eaSubTables[pTable->iMenuSubTableId]->cbSubCreate != NULL);
					pTable->pSubTableMenu->items[SUBMENU_CLONE_INDEX]->active = (iRow < eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows)) &&
						(pTable->eaSubTables[pTable->iMenuSubTableId]->cbSubCreate != NULL);
					pTable->pSubTableMenu->items[SUBMENU_COPY_INDEX]->active = (iRow < eaSize(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows)) &&
						(pTable->eaSubTables[pTable->iMenuSubTableId]->cbSubCreate != NULL);
					pTable->pSubTableMenu->items[SUBMENU_PASTE_INDEX]->active = (eaSize(&pTable->pPasteObject) > 0) && (pTable->iPasteSubTableId == pTable->iMenuSubTableId);
				}
				
				sprintf(buf, "New %s", pTable->eaSubTables[pTable->iMenuSubTableId]->pcDisplayName);
				ui_MenuItemSetTextString( pTable->pSubTableMenu->items[SUBMENU_NEW_INDEX], buf );

				if (eaSize(&pTable->pPasteObject) > 0)
				{
					sprintf(buf, "Paste %s", pTable->eaSubTables[pTable->iPasteSubTableId]->pcDisplayName);
					ui_MenuItemSetTextString( pTable->pSubTableMenu->items[SUBMENU_PASTE_INDEX], buf );
				}
				else
				{
					ui_MenuItemSetTextString( pTable->pSubTableMenu->items[SUBMENU_PASTE_INDEX], "Paste" );
				}

				ui_MenuCalculateWidth(pTable->pSubTableMenu);

				pTable->pSubTableMenu->widget.scale = emGetEditorScale(pTable->pEditorDoc->emDoc.editor);
				ui_MenuPopupAtCursor(pTable->pSubTableMenu);
			}
		}
	}
}


static void met_UICellRClickCallback(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, METable *pTable)
{
	if (iColumn > 0) {
		met_cellClickAction(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pTable, MET_CLICK_RIGHT);
	}
}


static void met_UISelectionChangeCallback(UIList *pList, METable *pTable)
{
	// Clear the edit control if required
	met_updateEditControl(pTable,0,NULL, NULL, 0, 0, 0, 0, NULL);

	// Close any popup editor
	MEFieldCloseEditor();
}

//Moves all selected rows to be in a contiguous group that starts one row higher than the top selected row starts at
static void met_UIMoveRowUp(UIMenu *pMenu, METable *pTable)
{
	int i, j, firstRow;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
	S32 *newSelectedRows = NULL;

	eaiCreate(&newSelectedRows);
	firstRow = (*eaiRows)[0] - 1;

	for (i = 0; i < eaiSize(eaiRows); i++)
	{
		for (j = (*eaiRows)[i]; j > (firstRow + i); j--)
		{
			// Swap the object defs
			eaSwap(&pTable->eaRows, j, j-1);
		}
		eaiPush(&newSelectedRows, firstRow + i);
	}

	ui_ListSetSelectedRows(pTable->pList, &newSelectedRows);
	eaiDestroy(&newSelectedRows);
}

//Moves all selected rows to be in a contiguous group that starts one row lower than the bottom selected row starts at
static void met_UIMoveRowDown(UIMenu *pMenu, METable *pTable)
{
	int i, j, k, lastRow;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
	S32 *newSelectedRows = NULL;

	eaiCreate(&newSelectedRows);
	lastRow = (*eaiRows)[eaiSize(eaiRows)-1] + 1;
	k = 0;

	for (i = eaiSize(eaiRows) - 1; i >= 0; i--)
	{
		for (j = (*eaiRows)[i]; j < (lastRow - k); j++)
		{
			// Swap the object defs
			eaSwap(&pTable->eaRows, j, j+1);
		}
		eaiPush(&newSelectedRows, lastRow - k);
		k++;
	}

	ui_ListSetSelectedRows(pTable->pList, &newSelectedRows);
	eaiDestroy(&newSelectedRows);
}

//Moves all selected rows to be in a contiguous group that starts at the top
static void met_UIMoveRowToTop(UIMenu *pMenu, METable *pTable)
{
	int i, j;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
	S32 *newSelectedRows = NULL;

	eaiCreate(&newSelectedRows);

	for (i = 0; i < eaiSize(eaiRows); i++)
	{
		for (j = (*eaiRows)[i]; j > i; j--)
		{
			// Swap the object defs
			eaSwap(&pTable->eaRows, j, j-1);
		}
		eaiPush(&newSelectedRows, i);
	}

	ui_ListSetSelectedRows(pTable->pList, &newSelectedRows);
	eaiDestroy(&newSelectedRows);
}

//Moves all selected rows to be in a contiguous group that ends at the bottom
static void met_UIMoveRowToBottom(UIMenu *pMenu, METable *pTable)
{
	int i, j, k, lastRow;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
	S32 *newSelectedRows = NULL;

	eaiCreate(&newSelectedRows);
	lastRow = eaSize(&pTable->eaRows) - 1;
	k = 0;

	for (i = eaiSize(eaiRows) - 1; i >= 0; i--)
	{
		for (j = (*eaiRows)[i]; j < (lastRow - k); j++)
		{
			// Swap the object defs
			eaSwap(&pTable->eaRows, j, j+1);
		}
		eaiPush(&newSelectedRows, lastRow - k);
		k++;
	}

	ui_ListSetSelectedRows(pTable->pList, &newSelectedRows);
	eaiDestroy(&newSelectedRows);
}

static void met_UIMoveSubRowUp(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		int i, j, firstRow;
		const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
		S32 *newSelectedRows = NULL;
		MESubTable *pSubTable = pTable->eaSubTables[pTable->iMenuSubTableId];
		METableSubTableData *pSubData = pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId];
		void ***peaSubRows = met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject);

		eaiCreate(&newSelectedRows);
		firstRow = (*eaiRows)[0] - 1;

		for (i = 0; i < eaiSize(eaiRows); i++)
		{
			for (j = (*eaiRows)[i]; j > (firstRow + i); j--)
			{
				// Swap the object defs
				if (pSubTable->cbReorder)
				{
					if (!pSubTable->cbReorder(pTable,pTable->eaRows[pTable->iMenuTableRow]->pObject, &pSubData->eaObjects, j, j - 1))
					{
						// If reorder fails, return without reordering
						return;
					}
				}
				else
				{
					// Swap the rows in the real data
					eaSwap(peaSubRows, j, j - 1);
					eaSwap(&pSubData->eaObjects, j, j - 1);
				}

				met_refreshFieldsFromData(pTable, pSubData->eaRows[j]->eaFields);
				met_refreshFieldsFromData(pTable, pSubData->eaRows[j - 1]->eaFields);

				// Swap two sub-rows
				eaSwap(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows, j, j - 1);
			}
			eaiPush(&newSelectedRows, firstRow + i);
		}

		ui_ListSetSelectedRows(pTable->pLastListClicked, &newSelectedRows);
		eaiDestroy(&newSelectedRows);
	}
}

static void met_UIMoveSubRowDown(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		int i, j, k, lastRow;
		const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
		S32 *newSelectedRows = NULL;
		MESubTable *pSubTable = pTable->eaSubTables[pTable->iMenuSubTableId];
		METableSubTableData *pSubData = pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId];
		void ***peaSubRows = met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject);

		eaiCreate(&newSelectedRows);
		lastRow = (*eaiRows)[eaiSize(eaiRows)-1] + 1;
		k = 0;

		for (i = eaiSize(eaiRows) - 1; i >= 0; i--)
		{
			for (j = (*eaiRows)[i]; j < (lastRow - k); j++)
			{
				// Swap the object defs
				if (pSubTable->cbReorder)
				{
					if (!pSubTable->cbReorder(pTable,pTable->eaRows[pTable->iMenuTableRow]->pObject, &pSubData->eaObjects, j, j + 1))
					{
						// If reorder fails, return without reordering
						return;
					}
				}
				else
				{
					// Swap the rows in the real data
					eaSwap(peaSubRows, j, j + 1);
					eaSwap(&pSubData->eaObjects, j, j + 1);
				}
		
				met_refreshFieldsFromData(pTable, pSubData->eaRows[j]->eaFields);
				met_refreshFieldsFromData(pTable, pSubData->eaRows[j + 1]->eaFields);

				// Swap two sub-rows
				eaSwap(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows, j, j + 1);
			}
			eaiPush(&newSelectedRows, lastRow - k);
			k++;
		}

		ui_ListSetSelectedRows(pTable->pLastListClicked, &newSelectedRows);
		eaiDestroy(&newSelectedRows);
	}
}

static void met_UIMoveSubRowToTop(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		int i, j;
		const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
		S32 *newSelectedRows = NULL;
		MESubTable *pSubTable = pTable->eaSubTables[pTable->iMenuSubTableId];
		METableSubTableData *pSubData = pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId];
		void ***peaSubRows = met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject);

		eaiCreate(&newSelectedRows);

		for (i = 0; i < eaiSize(eaiRows); i++)
		{
			for (j = (*eaiRows)[i]; j > i; j--)
			{
				// Swap the object defs
				if (pSubTable->cbReorder)
				{
					if (!pSubTable->cbReorder(pTable,pTable->eaRows[pTable->iMenuTableRow]->pObject, &pSubData->eaObjects, j, j - 1))
					{
						// If reorder fails, stop here
						break;
					}
				}
				else
				{
					// Swap the rows in the real data
					eaSwap(peaSubRows, j, j - 1);
					eaSwap(&pSubData->eaObjects, j, j - 1);
				}
				// Swap two sub-rows
				eaSwap(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows, j, j - 1);
				met_refreshFieldsFromData(pTable, pSubData->eaRows[j]->eaFields);
			}
			eaiPush(&newSelectedRows, i);
		}

		ui_ListSetSelectedRows(pTable->pLastListClicked, &newSelectedRows);
		eaiDestroy(&newSelectedRows);
	}
}


static void met_UIMoveSubRowToBottom(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		int i, j, k, lastRow;
		const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
		S32 *newSelectedRows = NULL;
		MESubTable *pSubTable = pTable->eaSubTables[pTable->iMenuSubTableId];
		METableSubTableData *pSubData = pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId];

		eaiCreate(&newSelectedRows);
		lastRow = eaSize(met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject)) - 1;
		k = 0;

		for (i = eaiSize(eaiRows) - 1; i >= 0; i--)
		{
			for (j = (*eaiRows)[i]; j < (lastRow - k); j++)
			{
				// Swap the object defs
				if (pSubTable->cbReorder)
				{
					if (!pSubTable->cbReorder(pTable,pTable->eaRows[pTable->iMenuTableRow]->pObject, &pSubData->eaObjects, j, j + 1))
					{
						// If reorder fails, stop here
						break;
					}
				}
				else
				{
					// Swap the rows in the real data
					void ***peaSubRows = met_getSubTableRows(pTable, pTable->iMenuSubTableId, pTable->eaRows[pTable->iMenuTableRow]->pObject);
					eaSwap(peaSubRows, j, j + 1);
					eaSwap(&pSubData->eaObjects, j, j + 1);
				}
				// Swap two sub-rows
				eaSwap(&pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows, j, j + 1);
				met_refreshFieldsFromData(pTable, pSubData->eaRows[j]->eaFields);
			}
			eaiPush(&newSelectedRows, lastRow - k);
			k++;
		}

		ui_ListSetSelectedRows(pTable->pLastListClicked, &newSelectedRows);
		eaiDestroy(&newSelectedRows);
	}
}

static void met_UIFindUsage(UIMenu *pMenu, METable *pTable)
{
	RequestUsageSearch(pTable->hDict, met_getObjectName(pTable, pTable->iMenuTableRow));
}

static void met_UIListReferences(UIMenu *pMenu, METable *pTable)
{
	RequestReferencesSearch(pTable->hDict, met_getObjectName(pTable, pTable->iMenuTableRow));
}

static void met_UICheckOut(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		resSetDictionaryEditMode(pTable->hDict, true);
		resSetDictionaryEditMode(gMessageDict, true);
		resRequestLockResource(pTable->hDict,  met_getObjectName(pTable, (*eaiRows)[i]), pTable->eaRows[(*eaiRows)[i]]->pObject);
	}
}

static void met_UIUndoCheckOut(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		met_revertRow(pTable, (*eaiRows)[i], 0);
		resSetDictionaryEditMode(pTable->hDict, true);
		resSetDictionaryEditMode(gMessageDict, true);
		resRequestUnlockResource(pTable->hDict,  met_getObjectName(pTable, (*eaiRows)[i]), pTable->eaRows[(*eaiRows)[i]]->pObject);
	}
}

static void met_UIOpenFolder(UIMenu *pMenu, METable *pTable)
{
	emuOpenContainingDirectory(met_getObjectFileName(pTable, pTable->iMenuTableRow));
}

static void met_UIOpenFile(UIMenu *pMenu, METable *pTable)
{
	emuOpenFile(met_getObjectFileName(pTable, pTable->iMenuTableRow));
}

static void met_UICloseObject(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
	EMEditorSubDoc **subDocs = NULL;

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		eaPush(&subDocs, (EMEditorSubDoc*)pTable->eaRows[(*eaiRows)[i]]->pEditorSubDoc);
	}
	emCloseSubDocs((EMEditorDoc*)pTable->pEditorDoc, &subDocs);
}


static void met_UIDeleteDismiss(UIButton *pButton, METable *pTable)
{
	EditorPrefStoreWindowPosition("METable", "Window Position", "Delete Confirm", pGlobalWindow);

	// Free the window
	ui_WindowHide(pGlobalWindow);
	ui_WidgetQueueFree(UI_WIDGET(pGlobalWindow));
	pGlobalWindow = NULL;
}


static void met_UIDeleteConfirmed(UIButton *pButton, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

	met_UIDeleteDismiss(pButton, pTable);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		met_deleteRow(pTable, (*eaiRows)[i]);
	}
}


static void met_UIDeleteObject(UIMenu *pMenu, METable *pTable)
{
	int i;
	UILabel *pLabel;
	UIButton *pButton;
	char *buf = NULL;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

	estrCreate(&buf);
	pGlobalWindow = ui_WindowCreate("Confirm Deletion?", 200, 200, 300, 60);

	EditorPrefGetWindowPosition("METable", "Window Position", "Delete Confirm", pGlobalWindow);

	estrCopy2(&buf, "Permanently delete ");
	estrConcatString(&buf, pTable->pcDisplayName, strlen(pTable->pcDisplayName));

	if ((eaiSize(eaiRows)) > 1)
	{
		estrConcatString(&buf, "s", 1);
	}

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		if (i > 0)
		{
			estrConcatString(&buf, ",", 1);
		}
		estrConcatString(&buf, " '", 2);
		estrConcatString(&buf, met_getObjectName(pTable, (*eaiRows)[i]), strlen(met_getObjectName(pTable, (*eaiRows)[i])));
		estrConcatString(&buf, "'", 1);
	}
	estrConcatString(&buf, "?", 1);
	pLabel = ui_LabelCreate(buf, 0, 0);
	ui_WindowAddChild(pGlobalWindow, pLabel);

	pGlobalWindow->widget.width = pLabel->widget.width + 20;
	pGlobalWindow->widget.height = 60;

	pButton = ui_ButtonCreate("Delete", 0, 28, met_UIDeleteConfirmed, pTable);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -85, 28, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	pButton = ui_ButtonCreate("Cancel", 0, 28, met_UIDeleteDismiss, pTable);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 5, 28, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
	estrDestroy(&buf);
}


static void met_UIRevertRow(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

	if ((eaiSize(eaiRows)) == 1)
	{
		if (UIYes != ui_ModalDialog("Revert?", "Are you sure you want to revert this row?", ColorBlack, UIYes | UINo))
		{
			return;
		}
	}
	else
	{
		if (UIYes != ui_ModalDialog("Revert?", "Are you sure you want to revert these rows?", ColorBlack, UIYes | UINo))
		{
			return;
		}
	}

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		met_revertRow(pTable, (*eaiRows)[i], 0);
		emStatusPrintf("Reverted %s \"%s\"", pTable->pcDisplayName, met_getObjectName(pTable, (*eaiRows)[i]));
	}
}


static void met_UISaveObject(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		emSaveSubDoc(&pTable->pEditorDoc->emDoc, &pTable->eaRows[(*eaiRows)[i]]->pEditorSubDoc->emSubDoc);
	}
}


static void met_UIRevertSubRow(UIMenu *pMenu, METable *pTable)
{
	const S32 *const *eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

	if ((eaiSize(eaiRows)) == 1)
	{
		if (UIYes != ui_ModalDialog("Revert?", "Are you sure you want to revert this sub row?", ColorBlack, UIYes | UINo))
		{
			return;
		}
	}
	else
	{
		if (UIYes != ui_ModalDialog("Revert?", "Are you sure you want to revert these sub rows?", ColorBlack, UIYes | UINo))
		{
			return;
		}
	}

	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		int i;
		for (i = 0; i < (eaiSize(eaiRows)); i++)
		{
			met_revertSubRow(pTable, pTable->iMenuTableRow, pTable->iMenuSubTableId, (*eaiRows)[i]);
		}
	}
}


static void met_UIDeleteSubRow(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		const S32 *const *eaiRows;
		eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

		while (eaiSize(eaiRows) > 0)
		{
			if (pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaRows[(*eaiRows)[0]]->bInherited)
			{
				ui_DialogPopup("Notice", "An inherited row cannot be deleted.");
				return;
			}
		
			met_deleteSubRow(pTable, pTable->iMenuTableRow, pTable->iMenuSubTableId, (*eaiRows)[0]);
			ui_ListNotifySelectionOfRowDeletion(pTable->pLastListClicked, (*eaiRows)[0]);
		}
		
		// Mark to check for dirty state again
		pTable->eaRows[pTable->iMenuTableRow]->bCheckDirty = 1;
	}
}


static void met_createSubRow(METable *pTable, int iRow, int iSubId, int iSubRow, void *pObjectToClone)
{
	if (pTable->eaSubTables[iSubId]->cbSubCreate) {
		void *pBeforeObject = NULL, *pAfterObject = NULL;
		void *pNewObject;

		// Determine if there is a before, after, or clone object
		if (eaSize(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaRows) > 0) {
			pBeforeObject = pTable->eaRows[iRow]->eaSubData[iSubId]->eaObjects[iSubRow];
		}
		if (iSubRow + 1 < eaSize(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaRows)) {
			pAfterObject = pTable->eaRows[iRow]->eaSubData[iSubId]->eaObjects[iSubRow + 1];
		}

		pNewObject = pTable->eaSubTables[iSubId]->cbSubCreate(pTable, pTable->eaRows[iRow]->pObject, pObjectToClone, pBeforeObject, pAfterObject);
		if (pNewObject) {
			METableSubRow *pSubRow;

			// Add the new object to the real object
			void ***peaSubObjects = met_getSubTableRows(pTable, iSubId, pTable->eaRows[iRow]->pObject);
			if (pTable->eaSubTables[iSubId]->pcKeyPTName) {
				eaIndexedAdd(peaSubObjects, pNewObject);
			} else if (!peaSubObjects || !eaSize(peaSubObjects)) {
				eaPush(peaSubObjects, pNewObject);
			} else {
				eaInsert(peaSubObjects, pNewObject, iSubRow + 1);
			}

			// Add half-baked data before regeneration so inheritance data gets picked up
			pSubRow = (METableSubRow*)calloc(1,sizeof(METableSubRow));
			pSubRow->bInherited = 0;
			eaPush(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaRows, pSubRow);
			eaPush(&pTable->eaRows[iRow]->eaSubData[iSubId]->eaObjects, pNewObject);

			// Regenerate the row
			met_regenerateRow(pTable, iRow);
		}
	}
}


static void met_UINewSubRow(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		met_createSubRow(pTable, pTable->iMenuTableRow, pTable->iMenuSubTableId, pTable->iMenuSubTableRow, NULL);
	}
}


static void met_UICloneSubRow(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		int i;
		const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);

		for (i = (eaiSize(eaiRows)) - 1; i >= 0; i--)
		{
			met_createSubRow(pTable, pTable->iMenuTableRow, pTable->iMenuSubTableId, (*eaiRows)[i], 
				pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaObjects[(*eaiRows)[i]]);
		}
	}
}


static void met_UICopySubRow(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);

	for (i = 0; i < (eaSize(&pTable->pPasteObject)); i++)
	{
		StructDestroyVoid(pTable->eaSubTables[pTable->iPasteSubTableId]->pParseTable, pTable->pPasteObject[i]);
	}
	eaDestroy(&pTable->pPasteObject);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		eaPush(&pTable->pPasteObject, StructCloneVoid(pTable->eaSubTables[pTable->iMenuSubTableId]->pParseTable, pTable->eaRows[pTable->iMenuTableRow]->eaSubData[pTable->iMenuSubTableId]->eaObjects[(*eaiRows)[i]]));
	}
	pTable->iPasteSubTableId = pTable->iMenuSubTableId;
}


static void met_UIPasteSubRow(UIMenu *pMenu, METable *pTable)
{
	if (met_dataPreChangeTest(pTable, pTable->iMenuTableRow))
	{
		const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);
		int i, prevLoc = (*eaiRows)[eaiSize(eaiRows)-1];
		for (i = 0; i < (eaSize(&pTable->pPasteObject)); i++)
		{
			met_createSubRow(pTable, pTable->iMenuTableRow, pTable->iMenuSubTableId, prevLoc, pTable->pPasteObject[i]);
			prevLoc++;
		}
	}
}


static void met_UIValidateObject(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		// Perform validation on a clone
		void *pObject = StructCloneVoid(pTable->pParseTable, pTable->eaRows[(*eaiRows)[i]]->pObject);
		met_validateObject(pTable, pObject);
		StructDestroyVoid(pTable->pParseTable, pObject);
	}
}

static void met_UIEditRow(UIMenu *pMenu, METable *pTable)
{
	met_clearEditWidget(pTable);
	met_editRow(pTable, pTable->iMenuTableRow, 0, 0, false);
}

static void met_UIEditSubRow(UIMenu *pMenu, METable *pTable)
{
	met_clearEditWidget(pTable);
	met_editRow(pTable, pTable->iMenuTableRow, pTable->iMenuSubTableId, pTable->iMenuSubTableRow, true);
}


static void met_UICloneObject(UIMenu *pMenu, METable *pTable)
{
	int i;
	void *pObject;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRowsSorted(pTable->pLastListClicked);

	for (i = (eaiSize(eaiRows)) - 1; i >= 0; i--)
	{
		// Create the object
		pObject = pTable->cbCreateObject(pTable, pTable->eaRows[(*eaiRows)[i]]->pObject, false);

		// Add it
		if (METableAddRowByObject(pTable, pObject, 0, 0))
		{
			// Move it to be directly following the object we're cloning
			eaMove(&pTable->eaRows, (*eaiRows)[i] + 1, eaSize(&pTable->eaRows) - 1);
		}
	}
}


static void met_UICreateChildObject(UIMenu *pMenu, METable *pTable)
{
	int i;
	const S32 *const *eaiRows = ui_ListGetSelectedSubRows(pTable->pLastListClicked);

	for (i = 0; i < (eaiSize(eaiRows)); i++)
	{
		met_createChildObject(pTable, pTable->eaRows[(*eaiRows)[i]]->pObject, (*eaiRows)[i] + 1);
	}
}


static void met_UIEditFields(UIMenu *pMenu, METable *pTable)
{
	METableEditFields(pTable);
}


static void met_UIRevertFields(UIMenu *pMenu, METable *pTable)
{
	METableRevertFields(pTable);
}


static void met_UIInheritFields(UIMenu *pMenu, METable *pTable)
{
	METableInheritFields(pTable);
}


static void met_UINoInheritFields(UIMenu *pMenu, METable *pTable)
{
	METableNoInheritFields(pTable);
}


static void met_UISelectColumn(UIWidget *pWidget, METable *pTable)
{
	// Select the cells in this column
	if (pTable->iMenuSubTableId < 0) {
		ui_ListColumnSelectCallback(pTable->eaCols[pTable->iMenuTableCol]->pListColumn, pTable->pList);
	} else {
		ui_ListColumnSelectCallback(pTable->eaSubTables[pTable->iMenuSubTableId]->eaCols[pTable->iMenuTableCol]->pListColumn, pTable->pList);
	}
}


static void met_UISortAscending(UIWidget *pWidget, METable *pTable)
{
	// Clear all selections before sorting
	met_clearEditWidget(pTable);
	ui_ListClearEverySelection(pTable->pList);
	
	// Perform the sort
	pTable->iSortDir = 1;
	eaStableSort(pTable->eaRows, NULL, met_fieldSortComparator);
}


static void met_UISortDescending(UIWidget *pWidget, METable *pTable)
{
	// Clear all selections before sorting
	met_clearEditWidget(pTable);
	ui_ListClearEverySelection(pTable->pList);
	
	// Perform the sort
	pTable->iSortDir = -1;
	eaStableSort(pTable->eaRows, NULL, met_fieldSortComparator);
}


static void met_UIRClickHeader(UIListColumn *pListColumn, METable *pTable)
{
	int i,j;

	if (!pTable->pHeaderMenu) {
		pTable->pHeaderMenu = ui_MenuCreate(NULL);
		ui_MenuAppendItems(pTable->pHeaderMenu,
			ui_MenuItemCreate("Select Column",UIMenuCallback, met_UISelectColumn, pTable, NULL),
			ui_MenuItemCreate("Sort Ascending",UIMenuCallback, met_UISortAscending, pTable, NULL),
			ui_MenuItemCreate("Sort Descending",UIMenuCallback, met_UISortDescending, pTable, NULL),
			NULL);
	}

	// Look on main columns
	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (pTable->eaCols[i]->pListColumn == pListColumn) {
			pTable->iMenuSubTableId = -1;
			pTable->iMenuTableCol = i;
			pTable->pHeaderMenu->items[1]->active = true;
			pTable->pHeaderMenu->items[2]->active = true;
			pTable->pHeaderMenu->widget.scale = emGetEditorScale(pTable->pEditorDoc->emDoc.editor);
			ui_MenuPopupAtCursor(pTable->pHeaderMenu);
			break;
		}
	}
	// Look on sub columns
	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
			if (pTable->eaSubTables[i]->eaCols[j]->pListColumn == pListColumn) {
				pTable->iMenuSubTableId = i;
				pTable->iMenuTableCol = j;
				pTable->pHeaderMenu->items[1]->active = false;
				pTable->pHeaderMenu->items[2]->active = false;
				pTable->pHeaderMenu->widget.scale = emGetEditorScale(pTable->pEditorDoc->emDoc.editor);
				ui_MenuPopupAtCursor(pTable->pHeaderMenu);
				break;
			}
		}
	}
}


static void met_UIPlaceholderCallback(UIWidget *pWidget, METable *pTable)
{
}


//---------------------------------------------------------------------------------------------------
// Internal Work Procedures
//---------------------------------------------------------------------------------------------------


static MEColData *met_addColData(METable *pTable, UIListColumn *pListColumn, F32 fWidth, char *pcGroupName, char *pcPTName, 
								 MEFieldType eType, StaticDefineInt *pEnum, ExprContext *pExprContext,
								 const char *pcDictName, ParseTable *pDictParseTable, char *pcDictNamePTName, const char *pcGlobalDictName,
								 METableDataFunc cbDataFunc)
{
	// Create column information
	MEColData *pColData = (MEColData*)calloc(1,sizeof(MEColData));
	pColData->pTable = pTable;
	pColData->pListColumn = pListColumn;
	pColData->fDefaultWidth = fWidth;
	pColData->pcGroup = pcGroupName;
	pColData->pcPTName = pcPTName;
	pColData->iColNum = eaSize(&pTable->eaCols);
	pColData->eType = eType;
	pColData->pEnum = pEnum;
	pColData->pExprContext = pExprContext;
	pColData->pcDictName = pcDictName;
	pColData->pcGlobalDictName = pcGlobalDictName;
	pColData->pDictParseTable = pDictParseTable;
	pColData->pcDictField = pcDictNamePTName;
	pColData->cbDataFunc = cbDataFunc;

	if (pcDictName || cbDataFunc || pcGlobalDictName) {
		// Need to pre-create the eArray
		pColData->eaDictNames = NULL;
		eaCreate(&pColData->eaDictNames);
	}

	// Add column information
	eaPush(&pTable->eaCols,pColData);

	return pColData;
}

static MESubColData *met_addSubColData(METable *pTable, MESubTable *pSubTable, 
									   UIListColumn *pListColumn, F32 fWidth, char *pcGroupName, char *pcPTName, 
									   ParseTable *pColParseTable, 
									   MEFieldType eType, StaticDefineInt *pEnum, ExprContext *pExprContext,
									   const char *pcDictName, ParseTable *pDictParseTable, char *pcDictNamePTName, const char *pcGlobalDictName,
									   METableDataFunc cbDataFunc)
{
	MESubColData *pSubColData;

	// Create column information
	pSubColData = (MESubColData*)calloc(1,sizeof(MESubColData));
	pSubColData->pTable = pTable;
	pSubColData->iSubTableId = pSubTable->id;
	pSubColData->pListColumn = pListColumn;
	pSubColData->fDefaultWidth = fWidth;
	pSubColData->pcGroup = pcGroupName;
	pSubColData->pcPTName = pcPTName;
	pSubColData->pColParseTable = pColParseTable;
	pSubColData->iColNum = eaSize(&pSubTable->eaCols);
	pSubColData->eType = eType;
	pSubColData->pEnum = pEnum;
	pSubColData->pExprContext = pExprContext;
	pSubColData->pcDictName = pcDictName;
	pSubColData->pcGlobalDictName = pcGlobalDictName;
	pSubColData->pDictParseTable = pDictParseTable;
	pSubColData->pcDictField = pcDictNamePTName;
	pSubColData->cbDataFunc = cbDataFunc;

	if (pcDictName || cbDataFunc || pcGlobalDictName) {
		// Need to pre-create the eArray
		pSubColData->eaDictNames = NULL;
		eaCreate(&pSubColData->eaDictNames);
	}

	// Add column information
	eaPush(&pSubTable->eaCols,pSubColData);

	return pSubColData;
}


static void met_initTableMenu(METable *pTable)
{
	pTable->pTableMenu = ui_MenuCreate(NULL);

	ui_MenuAppendItems(pTable->pTableMenu,
		ui_MenuItemCreate("", UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Move Up", UIMenuCallback, met_UIMoveRowUp, pTable, NULL),
		ui_MenuItemCreate("Move Down", UIMenuCallback, met_UIMoveRowDown, pTable, NULL),
		ui_MenuItemCreate("Move To Top", UIMenuCallback, met_UIMoveRowToTop, pTable, NULL),
		ui_MenuItemCreate("Move To Bottom", UIMenuCallback, met_UIMoveRowToBottom, pTable, NULL),
		ui_MenuItemCreate("", UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Edit Row", UIMenuCallback, met_UIEditRow, pTable, NULL),
		ui_MenuItemCreate("Validate", UIMenuCallback, met_UIValidateObject, pTable, NULL),
		ui_MenuItemCreate("Save", UIMenuCallback, met_UISaveObject, pTable, NULL),
		ui_MenuItemCreate("Close", UIMenuCallback, met_UICloseObject, pTable, NULL),
		ui_MenuItemCreate("Revert", UIMenuCallback, met_UIRevertRow, pTable, NULL),
		ui_MenuItemCreate("Delete", UIMenuCallback, met_UIDeleteObject, pTable, NULL),
		ui_MenuItemCreate("Find Usage", UIMenuCallback, met_UIFindUsage, pTable, NULL),
		ui_MenuItemCreate("List References", UIMenuCallback, met_UIListReferences, pTable, NULL),
		ui_MenuItemCreate("", UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Check Out", UIMenuCallback, met_UICheckOut, pTable, NULL),
		ui_MenuItemCreate("Undo Check Out", UIMenuCallback, met_UIUndoCheckOut, pTable, NULL),
		ui_MenuItemCreate("Open File", UIMenuCallback, met_UIOpenFile, pTable, NULL),
		ui_MenuItemCreate("Open Folder", UIMenuCallback, met_UIOpenFolder, pTable, NULL),
		ui_MenuItemCreate("", UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Create Clone", UIMenuCallback, met_UICloneObject, pTable, NULL),
		ui_MenuItemCreate("Create Child", UIMenuCallback, met_UICreateChildObject, pTable, NULL),
		NULL);
}


static void met_initSubTableMenu(METable *pTable)
{
	pTable->pSubTableMenu = ui_MenuCreate(NULL);

	ui_MenuAppendItems(pTable->pSubTableMenu,
		ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Move Up",UIMenuCallback,met_UIMoveSubRowUp,pTable,NULL),
		ui_MenuItemCreate("Move Down",UIMenuCallback,met_UIMoveSubRowDown,pTable,NULL),
		ui_MenuItemCreate("Move To Top",UIMenuCallback,met_UIMoveSubRowToTop,pTable,NULL),
		ui_MenuItemCreate("Move To Bottom",UIMenuCallback,met_UIMoveSubRowToBottom,pTable,NULL),
		ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Edit Row", UIMenuCallback, met_UIEditSubRow, pTable, NULL),
		ui_MenuItemCreate("Revert",UIMenuCallback,met_UIRevertSubRow,pTable,NULL),
		ui_MenuItemCreate("Delete",UIMenuCallback,met_UIDeleteSubRow,pTable,NULL),
		ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Add New",UIMenuCallback,met_UINewSubRow,pTable,NULL),
		ui_MenuItemCreate("Create Clone",UIMenuCallback,met_UICloneSubRow,pTable,NULL),
		ui_MenuItemCreate("Copy",UIMenuCallback,met_UICopySubRow,pTable,NULL),
		ui_MenuItemCreate("Paste",UIMenuCallback,met_UIPasteSubRow,pTable,NULL),
		NULL);
}


static void met_initList(METable *pTable)
{
	UIList *pList;
	UIListColumn *pCol;
	MEColData *pColData;

	// Create the list
	pList = pTable->pList = ui_ListCreate(NULL,&pTable->eaRows,20);

	// Define callbacks
	ui_ListSetCellClickedCallback(pList, met_UICellLClickCallback, pTable);
	ui_ListSetCellContextCallback(pList, met_UICellRClickCallback, pTable);
	ui_ListSetSelectedCallback(pList, met_UISelectionChangeCallback, pTable);

	// Define colors and other list behaviors
	pList->bUseBackgroundColor = 1;
	pList->backgroundColor = gColorMainBackground;
	//pList->widget.color[1] = COLOR_SELECTION;
	pList->bDrawGrid = 1;
	pList->bMultiSelect = 1;
	pList->bColumnSelect = 1;

	// Add the menu column
	pCol = ui_ListColumnCreateCallback("", met_UIDrawIcon, pTable);
	pCol->fWidth = 40;
	pCol->bResizable = 0;
	ui_ListAppendColumn(pList, pCol);
	pColData = met_addColData(pTable, pCol, 40, MET_GROUP_MENU, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}


static void met_initSubList(METable *pTable, MESubTable *pSubTable)
{
	UIList *pList;
	UIListColumn *pCol;
	MESubColData *pSubColData;

	// Create the list
	pList = pSubTable->pList = ui_ListCreate(NULL,NULL,20);

	// Define callbacks
	ui_ListSetSubListModelCallback(pTable->pList, met_UISubModelChange, pTable);
	ui_ListSetCellClickedCallback(pList, met_UICellLClickCallback, pTable);
	ui_ListSetCellContextCallback(pList, met_UICellRClickCallback, pTable);

	// Define colors and other list behaviors
	pList->bUseBackgroundColor = 1;
	pList->backgroundColor = ColorDarken(gColorSublistBackground, 16*pSubTable->id);
	pList->bDrawGrid = 1;
	pList->bMultiSelect = 1;
	pList->bColumnSelect = 1;

	// Add the menu column
	pCol = ui_ListColumnCreateCallback("", met_UIDrawSubIcon, pTable);
	pCol->fWidth = 40;
	pCol->bResizable = 0;
	ui_ListAppendColumn(pList, pCol);
	pSubColData = met_addSubColData(pTable, pSubTable, pCol, 40, MET_GROUP_MENU, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	// Add the sub-list to the parent list
	ui_ListAddSubList(pTable->pList,pList,50,NULL);
}


//---------------------------------------------------------------------------------------------------
// General API Calls
//---------------------------------------------------------------------------------------------------


void METableTick(TimedCallback *callback, F32 timeSinceLastCallback, METable *pTable)
{
	if (emIsEditorActive() && (emGetActiveEditorDoc() == &pTable->pEditorDoc->emDoc)) {
		PERFINFO_AUTO_START_FUNC();
		met_scanForChangedObjects(pTable);
		met_freeRowsOnTick(pTable);
		met_checkSmartHidden(pTable);
		met_checkWritable(pTable);
		met_checkEditWindow(pTable);
		PERFINFO_AUTO_STOP();
	}
}

METable *METableCreate(char *pcDisplayName, char *pcTypeName, DictionaryHandle hDict, ParseTable *pParseTable, char *pcNamePTName,  char *pcFilePTName, char *pcScopePTName, MultiEditEMDoc *pEditorDoc)
{
	METable *pTable;

	// Initialize the structure
	pTable = (METable*)calloc(sizeof(METable),1);
	pTable->pEditorDoc = pEditorDoc;
	pTable->pcDisplayName = pcDisplayName;
	pTable->pcTypeName = pcTypeName;
	pTable->hDict = hDict;
	pTable->pParseTable = pParseTable;
	pTable->pcNamePTName = pcNamePTName;
	pTable->iNameIndex = -1;
	pTable->pcFilePTName = pcFilePTName;
	pTable->iFileIndex = -1;
	pTable->pcScopePTName = pcScopePTName;
	pTable->iScopeIndex = -1;

	pTable->eaCols = NULL;
	pTable->eaSubTables = NULL;

	// Initialize rows eArray properly (required)
	pTable->eaRows = NULL;
	eaCreate(&pTable->eaRows);

	// Figure out parse table index for the name column
	if (pTable->pcNamePTName) {
		if (!ParserFindColumn(pTable->pParseTable,pTable->pcNamePTName,&pTable->iNameIndex)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pTable->pcNamePTName);
			assertmsg(0, buf);
		}
	}

	// Figure out parse table index for the file column
	if (pTable->pcFilePTName) {
		if (!ParserFindColumn(pTable->pParseTable,pTable->pcFilePTName,&pTable->iFileIndex)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pTable->pcFilePTName);
			assertmsg(0, buf);
		}
	}

	// Figure out parse table index for the scope column
	if (pTable->pcFilePTName) {
		if (!ParserFindColumn(pTable->pParseTable,pTable->pcScopePTName,&pTable->iScopeIndex)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pTable->pcScopePTName);
			assertmsg(0, buf);
		}
	}

	// Initialize the root list
	met_initList(pTable);
	met_initTableMenu(pTable);

	if (!IS_HANDLE_ACTIVE(g_hBorder)) {
		SET_HANDLE_FROM_STRING(g_ui_BorderDict, "Default_Capsule_Filled", g_hBorder);
	}

	pTable->pTickCallback = TimedCallback_Add(METableTick, pTable, 0.0);

	return pTable;
}


void METableDestroy(METable *pTable)
{
	int i,j,k;

	TimedCallback_Remove(pTable->pTickCallback);

	// Close all the rows to free the main data
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		met_closeRow(pTable, i, 0);
	}
	eaDestroy(&pTable->eaRows);

	// Free the UI list (also gets sub-list and columns)
	if (pTable->pList) {
		ui_WidgetQueueFree((UIWidget*)pTable->pList);
		pTable->pList = NULL;
	}
	if (pTable->pHeaderMenu) {
		ui_WidgetQueueFree((UIWidget*)pTable->pHeaderMenu);
		pTable->pHeaderMenu = NULL;
	}

	// Free the column tracking data
	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (pTable->eaCols[i]->cbDataFunc) {
			for(j=eaSize(&pTable->eaCols[i]->eaDictNames)-1; j>=0; --j) {
				free(pTable->eaCols[i]->eaDictNames[j]);
			}
		}
		eaDestroy(&pTable->eaCols[i]->eaDictNames);
		free(pTable->eaCols[i]);
	}
	eaDestroy(&pTable->eaCols);

	// Free the sub-column tracking data
	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
			if (pTable->eaSubTables[i]->eaCols[j]->cbDataFunc) {
				for(k=eaSize(&pTable->eaSubTables[i]->eaCols[j]->eaDictNames)-1; k>=0; --k) {
					free(pTable->eaSubTables[i]->eaCols[j]->eaDictNames[k]);
				}
			}
			eaDestroy(&pTable->eaSubTables[i]->eaCols[j]->eaDictNames);
			free(pTable->eaSubTables[i]->eaCols[j]);
		}
		eaDestroy(&pTable->eaSubTables[i]->eaCols);
		free(pTable->eaSubTables[i]);
	}
	eaDestroy(&pTable->eaSubTables);

	// Release this object's memory
	free(pTable);
}


UIWidget* METableGetWidget(METable *pTable)
{
	return (UIWidget*)pTable->pList;
}


int METableCreateSubTable(METable *pTable, char *pcDisplayName,
						  char *pcSubPTName, ParseTable *pSubParseTable, char *pcSubKeyPTName,
						  MEOrderFunc cbOrder, MEReorderFunc cbReorder, MESubCreateFunc cbSubCreate)
{
	MESubTable *pSubTable;

	pSubTable = (MESubTable*)calloc(sizeof(MESubTable),1);
	pSubTable->id = eaSize(&pTable->eaSubTables);
	pSubTable->pcDisplayName = pcDisplayName;
	pSubTable->pcSubPTName = pcSubPTName;

	if (pTable->bHasParentCol && (!pSubParseTable || !pcSubKeyPTName)) {
		assertmsg(0, "A table with inheritance must have a valid key on all sub-tables");
	}

	pSubTable->pParseTable = pSubParseTable;
	pSubTable->pcKeyPTName = pcSubKeyPTName;
	pSubTable->iKeyIndex = -1;
	pSubTable->eaCols = NULL;
	pSubTable->pList = NULL;

	pSubTable->cbOrder = cbOrder;
	pSubTable->cbReorder = cbReorder;
	pSubTable->cbSubCreate = cbSubCreate;

	// Figure out parse table index for the key column of the sub table
	if (pSubTable->pcKeyPTName) {
		if (!ParserFindColumn(pSubTable->pParseTable,pSubTable->pcKeyPTName,&pSubTable->iKeyIndex)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pSubTable->pcKeyPTName);
			assertmsg(0, buf);
		}
	}

	met_initSubList(pTable, pSubTable);

	if (eaSize(&pTable->eaSubTables) == 0) {
		// First sublist, so make the menu
		met_initSubTableMenu(pTable);
	}

	// Add the sub list to the table
	eaPush(&pTable->eaSubTables, pSubTable);

	// Return the ID
	return pSubTable->id;
}


void METableGetSubTableInfo(METable *pTable, int **peaiIds, char ***peaTableNames)
{
	int i;
	for (i=0; i<eaSize(&pTable->eaSubTables); ++i) {
		eaiPush(peaiIds, pTable->eaSubTables[i]->id);
		eaPush(peaTableNames, pTable->eaSubTables[i]->pcDisplayName);
	}
}


void METableAddColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
					  MEFieldType eType, StaticDefineInt *pEnum, ExprContext *pExprContext,
					  const char *pcDictName, ParseTable *pDictParseTable, char *pcDictNamePTName, const char *pcGlobalDictName,
					  METableDataFunc cbDataFunc)
{
	UIListColumn *pCol;
	MEColData *pColData;

	// Create the column tracking data
	pColData = met_addColData(pTable,NULL,fColWidth,pcColGroupName,pcColPTName,eType,pEnum,pExprContext,pcDictName, pDictParseTable, pcDictNamePTName, pcGlobalDictName, cbDataFunc);

	// Create the UI list column
	pCol = ui_ListColumnCreateCallback(pcColLabel, met_UIDrawField, pColData);
	pCol->fWidth = EditorPrefGetFloat(pTable->pcDisplayName, PREF_COLUMN_WIDTH, pcColLabel, fColWidth);
	ui_ListColumnSetClickedCallback(pCol, ui_ListColumnSelectCallback, pTable->pList);
	ui_ListColumnSetContextCallback(pCol, met_UIRClickHeader, pTable);
	ui_ListAppendColumn(pTable->pList, pCol);
	pColData->pListColumn = pCol;
}


void METableAddSimpleColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
							char *pcColGroupName, MEFieldType eType)
{
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, eType,
					 NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}


void METableAddDictColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
						  MEFieldType eType, const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName)
{
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, eType,
					 NULL, NULL, pDictName, pDictParseTable, pcDictNamePTName, NULL, NULL);
}

void METableAddGlobalDictColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
								MEFieldType eType, const char *pDictName, char *pcDictNamePTName)
{
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, eType,
		NULL, NULL, NULL, parse_ResourceInfo, pcDictNamePTName, pDictName, NULL);
}

void METableAddEnumColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
						  char *pcColGroupName, MEFieldType eType, StaticDefineInt *pEnum)
{
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, eType,
					 pEnum, NULL, NULL, NULL, NULL, NULL, NULL);
}


void METableAddExprColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
						  char *pcColGroupName, ExprContext *pExprContext)
{
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, kMEFieldTypeEx_Expression,
					 NULL, pExprContext, NULL, NULL, NULL, NULL, NULL);
}

void METableAddGameActionBlockColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
						  char *pcColGroupName, ExprContext *pExprContext)
{
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, kMEFieldTypeEx_GameActionBlock,
					 NULL, pExprContext, NULL, NULL, NULL, NULL, NULL);
}

void METableAddGameEventColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
									 char *pcColGroupName, ExprContext *pExprContext)
{
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, kMEFieldTypeEx_GameEvent,
		NULL, pExprContext, NULL, NULL, NULL, NULL, NULL);
}


void METableAddFileNameColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
							char *pcColGroupName, char *pcBrowseTitle, char *pcTopDir, char *pcStartDir, char *pcExtension, UIBrowserMode eMode)
{
	MEFileBrowseData *pFileData;

	// Create the column
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, kMEFieldType_FileName,
					 NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	METableSetColumnState(pTable, pcColLabel, ME_STATE_NOT_EDITABLE | ME_STATE_NOT_PARENTABLE | ME_STATE_NOT_REVERTABLE);

	// Add the file data
	pFileData = (MEFileBrowseData*)calloc(1,sizeof(MEFileBrowseData));
	pFileData->pcBrowseTitle = pcBrowseTitle;
	pFileData->pcTopDir = pcTopDir;
	pFileData->pcStartDir = pcStartDir;
	pFileData->pcExtension = pcExtension;
	pFileData->eMode = eMode;
	pTable->eaCols[eaSize(&pTable->eaCols)-1]->pFileData = pFileData;
}


void METableAddScopeColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, MEFieldType eType)
{
	// Create the column
	METableAddColumn(pTable, pcColLabel, pcColPTName, fColWidth, pcColGroupName, eType,
					 NULL, NULL, NULL, NULL, NULL, NULL, met_getScopeNames);
	METableSetColumnState(pTable, pcColLabel, ME_STATE_NOT_PARENTABLE);
}


void METableAddParentColumn(METable *pTable, char *pcColLabel, float fColWidth, char *pcColGroupName, bool bGlobalDictionary)
{
	UIListColumn *pCol;
	MEColData *pColData;
	const char *pcDictName;

	pcDictName = RefSystem_GetDictionaryNameFromNameOrHandle(pTable->hDict);

	pTable->bHasParentCol = true;

	// Create the column tracking data
	if (bGlobalDictionary)
		pColData = met_addColData(pTable,NULL,fColWidth,pcColGroupName,NULL,kMEFieldType_TextEntry,NULL,NULL,NULL,NULL,"resourceName",pcDictName, NULL);
	else
		pColData = met_addColData(pTable,NULL,fColWidth,pcColGroupName,NULL,kMEFieldType_TextEntry,NULL,NULL,pcDictName,pTable->pParseTable,pTable->pcNamePTName, NULL,NULL);

	pColData->bParentCol = 1;
	pColData->flags = ME_STATE_NOT_PARENTABLE | ME_STATE_NOT_GROUP_EDITABLE;

	// Create the UI list column
	pCol = ui_ListColumnCreateCallback(pcColLabel, met_UIDrawField, pColData);
	pCol->fWidth = fColWidth;
	ui_ListColumnSetClickedCallback(pCol, ui_ListColumnSelectCallback, pTable->pList);
	ui_ListColumnSetContextCallback(pCol, met_UIRClickHeader, pTable);
	ui_ListAppendColumn(pTable->pList, pCol);
	pColData->pListColumn = pCol;
}


void METableAddSubColumn(METable *pTable, int iSubTableId,
						 char *pcColLabel, char *pcColPTName, ParseTable *pColParseTable, float fColWidth, char *pcColGroupName,
						 MEFieldType eType, StaticDefineInt *pEnum, ExprContext *pExprContext,
						 const char *pcDictName, ParseTable *pDictParseTable, char *pcDictNamePTName, const char *pcGlobalDictName,
						 METableDataFunc cbDataFunc)
{
	UIListColumn *pCol;
	MESubColData *pSubColData;
	char buf[260];

	// Create the sub-list column tracking data
	pSubColData = met_addSubColData(pTable, pTable->eaSubTables[iSubTableId],
									NULL,fColWidth,pcColGroupName,pcColPTName,pColParseTable,eType,pEnum,pExprContext,
									pcDictName, pDictParseTable, pcDictNamePTName, pcGlobalDictName, cbDataFunc);

	// Create the UI sub-list column
	pCol = ui_ListColumnCreateCallback(pcColLabel, met_UIDrawSubField, pSubColData);
	sprintf(buf, "%s_%d", PREF_COLUMN_WIDTH, iSubTableId);
	pCol->fWidth = EditorPrefGetFloat(pTable->pcDisplayName, buf, pcColLabel, fColWidth);
	ui_ListColumnSetClickedCallback(pCol, ui_ListColumnSelectCallback, pTable->eaSubTables[iSubTableId]->pList);
	ui_ListColumnSetContextCallback(pCol, met_UIRClickHeader, pTable);
	ui_ListAppendColumn(pTable->eaSubTables[iSubTableId]->pList, pCol);
	pSubColData->pListColumn = pCol;
}


void METableAddSimpleSubColumn(METable *pTable, int iSubTableId,
						 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
						 MEFieldType eType)
{
	METableAddSubColumn(pTable, iSubTableId, pcColLabel, pcColPTName, NULL, fColWidth, pcColGroupName, eType,
						NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}


void METableAddDictSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, MEFieldType eType,
							 const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName)
{
	METableAddSubColumn(pTable, iSubTableId, pcColLabel, pcColPTName, NULL, fColWidth, pcColGroupName, eType,
						NULL, NULL, pDictName, pDictParseTable, pcDictNamePTName, NULL, NULL);
}

void METableAddGlobalDictSubColumn(METable *pTable, int iSubTableId, 
								   char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, MEFieldType eType,
								   const char *pDictName, char *pcDictNamePTName)
{
	METableAddSubColumn(pTable, iSubTableId, pcColLabel, pcColPTName, NULL, fColWidth, pcColGroupName, eType,
		NULL, NULL, NULL, parse_ResourceInfo, pcDictNamePTName, pDictName, NULL);
}

void METableAddEnumSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
							 MEFieldType eType, StaticDefineInt *pEnum)
{
	METableAddSubColumn(pTable, iSubTableId, pcColLabel, pcColPTName, NULL, fColWidth, pcColGroupName, eType,
						pEnum, NULL, NULL, NULL, NULL, NULL, NULL);
}


void METableAddExprSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
							 ExprContext *pExprContext)
{
	METableAddSubColumn(pTable, iSubTableId, pcColLabel, pcColPTName, NULL, fColWidth, pcColGroupName, kMEFieldTypeEx_Expression,
						NULL, pExprContext, NULL, NULL, NULL, NULL, NULL);
}

void METableAddGameActionBlockSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
							 ExprContext *pExprContext)
{
	METableAddSubColumn(pTable, iSubTableId, pcColLabel, pcColPTName, NULL, fColWidth, pcColGroupName, kMEFieldTypeEx_GameActionBlock,
						NULL, pExprContext, NULL, NULL, NULL, NULL, NULL);
}

void METableAddGameEventSubColumn(METable *pTable, int iSubTableId, 
										char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
										ExprContext *pExprContext)
{
	METableAddSubColumn(pTable, iSubTableId, pcColLabel, pcColPTName, NULL, fColWidth, pcColGroupName, kMEFieldTypeEx_GameEvent,
		NULL, pExprContext, NULL, NULL, NULL, NULL, NULL);
}


void METableFinishColumns(METable *pTable)
{
	int i;

	// Add empty column at the end of each table
	METableAddColumn(pTable, "", NULL, 10, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		METableAddSubColumn(pTable, pTable->eaSubTables[i]->id, "", NULL, NULL, 10, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	}
}

void METableSetNumLockedColumns(METable *pTable, int iCount)
{
	ui_ListSetNumLockedColumns(pTable->pList, iCount);
}


void METableSetNumLockedSubColumns(METable *pTable, int id, int iCount)
{
	ui_ListSetNumLockedColumns(pTable->eaSubTables[id]->pList, iCount);
}


void METableGetColGroupNames(METable *pTable, char ***peaColGroups)
{
	int i,j;
	for (i=0; i<eaSize(&pTable->eaCols); ++i) {
		if (pTable->eaCols[i]->pcGroup) {
			bool bFound = false;
			for(j=eaSize(peaColGroups)-1; j>=0; --j) {
				if (stricmp(pTable->eaCols[i]->pcGroup, (*peaColGroups)[j]) == 0) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				eaPush(peaColGroups, pTable->eaCols[i]->pcGroup);
			}
		}
	}
}


void METableGetSubColGroupNames(METable *pTable, int id, char ***peaColGroups)
{
	int i,j;
	for (i=0; i<eaSize(&pTable->eaSubTables[id]->eaCols); ++i) {
		if (pTable->eaSubTables[id]->eaCols[i]->pcGroup) {
			bool bFound = false;
			for(j=eaSize(peaColGroups)-1; j>=0; --j) {
				if (stricmp(pTable->eaSubTables[id]->eaCols[i]->pcGroup, (*peaColGroups)[j]) == 0) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				eaPush(peaColGroups, pTable->eaSubTables[id]->eaCols[i]->pcGroup);
			}
		}
	}
}


void METableSetColumnState(METable *pTable, char *pcColName, int columnStateFlags)
{
	int i;

	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (stricmp(ui_ListColumnGetTitle(pTable->eaCols[i]->pListColumn), pcColName) == 0) {
			pTable->eaCols[i]->flags = columnStateFlags;
			ui_ListColumnSetHidden(pTable->eaCols[i]->pListColumn, (columnStateFlags & ME_STATE_HIDDEN) != 0);
			break;
		}
	}
}


void METableSetSubColumnState(METable *pTable, int id, char *pcColName, int columnStateFlags)
{
	int i,j;

	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		if (pTable->eaSubTables[i]->id == id) {
			for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
				if (stricmp(ui_ListColumnGetTitle(pTable->eaSubTables[i]->eaCols[j]->pListColumn), pcColName) == 0) {
					pTable->eaSubTables[i]->eaCols[j]->flags = columnStateFlags;
					ui_ListColumnSetHidden(pTable->eaSubTables[i]->eaCols[j]->pListColumn, (columnStateFlags & ME_STATE_HIDDEN) != 0);
					break;
				}
			}
		}
	}
}


// Add an alternate object path for the column
void METableAddColumnAlternatePath(METable *pTable, char *pcColName, char *pcAltPath)
{
	int i;

	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (stricmp(ui_ListColumnGetTitle(pTable->eaCols[i]->pListColumn), pcColName) == 0) {
			eaPush(&pTable->eaCols[i]->eaExtraNames, pcAltPath);
			break;
		}
	}
}


// Add an alternate object path for the sub-column
void METableAddSubColumnAlternatePath(METable *pTable, int id, char *pcColName, char *pcAltPath)
{
	int i,j;

	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		if (pTable->eaSubTables[i]->id == id) {
			for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
				if (stricmp(ui_ListColumnGetTitle(pTable->eaSubTables[i]->eaCols[j]->pListColumn), pcColName) == 0) {
					eaPush(&pTable->eaSubTables[i]->eaCols[j]->eaExtraNames, pcAltPath);
					break;
				}
			}
		}
	}
}


static void met_addRow(METable *pTable, void *pOrigData, void *pNewData, bool bTop, bool bScrollTo)
{
	int i,j,k,n,numRows,numCols,numSubTables;
	METableRow *pRow;
	MEField *pField;
	MEField **eaFields = NULL;
	void *pParent = NULL;
	char *pcParentName, *pcOrigParentName = NULL;
	const char *pcName;
	const char *pcFileName = NULL;

	// Remove any edit widget and clear any selection so it won't get confused with the newly added row
	met_clearEditWidget(pTable);
	ui_ListClearEverySelection(pTable->pList);
	pTable->bCheckSmartHidden = true;

	// Find Parent reference for the object
	pcParentName = StructInherit_GetParentName(pTable->pParseTable, pNewData);
	if (pOrigData) {
		pcOrigParentName = StructInherit_GetParentName(pTable->pParseTable, pOrigData);
	}
	if (pcParentName) {
		void *pDictParent = RefSystem_ReferentFromString(pTable->hDict, pcParentName);
		if (pDictParent && resIsEditingVersionAvailable(pTable->hDict, pcParentName)) {
			pParent = StructCloneVoid(pTable->pParseTable, pDictParent);
			langMakeEditorCopy(pTable->pParseTable, pParent, true);
		} else {
			resRequestOpenResource(pTable->hDict, pcParentName);
		}
	}

	// Allocate the row
	pRow = (METableRow*)calloc(1,sizeof(METableRow));
	pRow->pTable = pTable;
	pRow->pObject = pNewData;
	pRow->pOrigObject = pOrigData;
	pRow->pParentObject = pParent;
	pRow->eExpand = ME_EXPAND;
	pRow->bCheckDirty = 1;

	// Set up file tracker
	if (pTable->pcFilePTName) {
		pcFileName = TokenStoreGetString(pTable->pParseTable, pTable->iFileIndex, pNewData, 0, NULL);
		pRow->pEMFile = emGetFile(pcFileName, true);
	}

	// Initialize subrows data properly
	numSubTables = eaSize(&pTable->eaSubTables);
	pRow->eaSubData = NULL;
	for(i=0; i<numSubTables; ++i) {
		METableSubTableData *pSubData = (METableSubTableData*)calloc(sizeof(METableSubTableData),1);
		eaCreate(&pSubData->eaRows);
		eaPush(&pRow->eaSubData,pSubData);
	}

	// Set up the editor sub-doc
	pcName = TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pRow->pObject, 0, NULL);
	pRow->pEditorSubDoc = calloc(1, sizeof(MultiEditEMSubDoc));
	pRow->pEditorSubDoc->pObject = pRow->pObject;
	pRow->pEditorSubDoc->emSubDoc.saved = (pOrigData != NULL);
	strcpy(pRow->pEditorSubDoc->emSubDoc.doc_name, pcName ? pcName : "");
	eaPush(&pTable->pEditorDoc->emDoc.sub_docs, (EMEditorSubDoc*)pRow->pEditorSubDoc);

	// Add the object for the row
	eaPush(&pTable->eaRows, pRow);

	// Create MEField objects for all columns in the main list
	numCols = eaSize(&pTable->eaCols);
	for(i=0; i<numCols; ++i) {
		MEColData *pCol = pTable->eaCols[i];
		char ***peaComboStrings = NULL;

		if (pCol->cbDataFunc) {
			peaComboStrings = &pCol->eaDictNames;
		}

		if (pCol->pcPTName) {
			ParseTable *pFieldParseTable = NULL;
			int iFieldCol = -1;
			void *pFieldData = NULL, *pFieldOrigData = NULL, *pFieldParentData = NULL;
			char *pcInheritName = NULL;
			char buf[1024];

			// Put the inheritance name in the
			if (pCol->pcPTName[0] == '.') {
				pcInheritName = pCol->pcPTName;
			} else if (pCol->pcPTName[0] == '@') {
				char *pos;
				strcpy(buf, pCol->pcPTName);
				buf[0] = '.';
				pos = strchr(buf,'@');
				if (pos) {
					*pos = '.';
				}
				pcInheritName = buf;
			} else {
				sprintf(buf,".%s",pCol->pcPTName);
				pcInheritName = buf;
			}

			// Get the field's original, parent, and real data
			met_getFieldData(pTable->pParseTable, pCol->pcPTName, pOrigData, &pFieldParseTable, &iFieldCol, &pFieldOrigData);
			if (pParent) {
				met_getFieldData(pTable->pParseTable, pCol->pcPTName, pParent, &pFieldParseTable, &iFieldCol, &pFieldParentData);
			}
			if (met_getFieldData(pTable->pParseTable, pCol->pcPTName, pNewData, &pFieldParseTable, &iFieldCol, &pFieldData)) {
				pField = MEFieldCreate(pCol->eType,
						pFieldOrigData,pFieldData,pFieldParseTable,pFieldParseTable[iFieldCol].name,pFieldParentData,
						pOrigData,pNewData,pTable->pParseTable,pcInheritName,
						pCol->pcDictName,pCol->pDictParseTable,pCol->pcDictField,NULL,pCol->pcGlobalDictName,ui_ListColumnGetTitle(pCol->pListColumn),
						0,peaComboStrings,NULL,pTable->eaCols[i]->pEnum,pTable->eaCols[i]->pExprContext,
						-1,0,0,0,NULL);
			} else {
				// Unable to find the actual field
				pField = NULL;
				Errorf("Unable to find editor field data for \"%s\"\n", pCol->pcPTName);
			}
		} else if (pCol->bParentCol) {
			// This is a parent column so make the fake structs to hold the parent name
			MEParentName *pOrigName = NULL, *pNewName;

			pNewName = StructCreate(parse_MEParentName);
			if (pcParentName) {
				pNewName->pcParentName = StructAllocString(pcParentName);
			}
			if (pOrigData) {
				pOrigName = StructCreate(parse_MEParentName);
				if (pcOrigParentName) {
					pOrigName->pcParentName = StructAllocString(pcOrigParentName);
				}
			}

			pField = MEFieldCreate(pCol->eType,
					pOrigName,pNewName,parse_MEParentName,"parentName",NULL,
					NULL,NULL,NULL,NULL,
					pCol->pcDictName,pCol->pDictParseTable,pCol->pcDictField,NULL,pCol->pcGlobalDictName,ui_ListColumnGetTitle(pCol->pListColumn),
					0,NULL,NULL,NULL,NULL,
					-1,0,0,0,NULL);
		} else {
			pField = NULL;
		}

		if (pField) {
			const char *dictName = RefSystem_GetDictionaryNameFromNameOrHandle(pTable->hDict);
			if (pTable->eaCols[i]->flags & ME_STATE_NOT_PARENTABLE) {
				pField->bNotParentable = true;
				MEUpdateFieldParented(pField, false);
			}
			if (pTable->eaCols[i]->flags & ME_STATE_NOT_EDITABLE) {
				pField->bEditable = false;
			}
			if (pTable->eaCols[i]->flags & ME_STATE_NOT_REVERTABLE) {
				pField->bNotRevertable = true;
			}
			if (pTable->eaCols[i]->flags & ME_STATE_NOT_GROUP_EDITABLE) {
				pField->bNotGroupEditable = true;
			}
			
			for(n=eaSize(&pCol->eaExtraNames)-1; n>=0; --n) {
				MEFieldAddAlternatePath(pField, pCol->eaExtraNames[n]);
			}

			if (pTable->eaCols[i]->bParentCol) {
				MEFieldSetChangeCallback(pField, met_fieldParentChangeCallback, pTable);
			} else if (stricmp(pTable->eaCols[i]->pcPTName, pTable->pcFilePTName) == 0) {
				MEFieldSetChangeCallback(pField, met_fieldFileChangeCallback, pTable);
			} else if (stricmp(pTable->eaCols[i]->pcPTName, pTable->pcNamePTName) == 0) {
				MEFieldSetChangeCallback(pField, met_fieldNameChangeCallback, pTable);
			} else if (stricmp(pTable->eaCols[i]->pcPTName, pTable->pcScopePTName) == 0) {
				MEFieldSetChangeCallback(pField, met_fieldScopeChangeCallback, pTable);
			} else {
				MEFieldSetChangeCallback(pField, met_fieldSimpleChangeCallback, pTable);
			}
			MEFieldSetPreChangeCallback(pField, met_fieldPreChangeCallback, pTable);

			if (dictName)
			{
				MEFieldSetRootDictName(pField, dictName);
			}
		}

		// Add column to the row
		eaPush(&eaFields,pField);
	}
	// Put the fields on the row
	pRow->eaFields = eaFields;

	// Create MEField objects for all columns in the sub-tables
	for(i=0; i<numSubTables; ++i) {
		if (pTable->eaSubTables[i]->pcSubPTName) {
			void ***peaOldSubRows = NULL;
			void ***peaNewSubRows = NULL, **eaOrderedNewSubRows = NULL;
			void ***peaParentSubRows = NULL;
			METableSubRow *pSubRow;
			MESubTable *pSubTable;

			pSubTable = pTable->eaSubTables[i];
			peaNewSubRows = met_getSubTableRows(pTable, i, pNewData);
			if (pOrigData) {
				peaOldSubRows = met_getSubTableRows(pTable, i, pOrigData);
			}
			if (pParent) {
				peaParentSubRows = met_getSubTableRows(pTable, i, pParent);
			}

			if (pSubTable->cbOrder) {
				// If sublist can be ordered, ask callback to order it for us
				pSubTable->cbOrder(pTable, peaNewSubRows, &eaOrderedNewSubRows);
			} else {
				// If can't be ordered, just use the natural ordering
				for(j=0; j<eaSize(peaNewSubRows); ++j) {
					eaPush(&eaOrderedNewSubRows, (*peaNewSubRows)[j]);
				}
			}

			// Save the ordered sub-rows 
			pRow->eaSubData[i]->eaObjects = eaOrderedNewSubRows;

			// Loop on data to create fields
			numCols = eaSize(&pSubTable->eaCols);
			numRows = eaSize(&eaOrderedNewSubRows);
			for(j=0; j<numRows; ++j) {
				void *pParentEntry = NULL;
				void *pOldSubRow = NULL;
				int iKey = 0;

				eaFields = NULL;

				// Get the index of this sub-row
				if (pSubTable->pcKeyPTName) {
					iKey = TokenStoreGetInt(pSubTable->pParseTable,pSubTable->iKeyIndex,eaOrderedNewSubRows[j],0,NULL);

					if (peaOldSubRows) {
						int pos = eaIndexedFindUsingInt(peaOldSubRows, iKey);
						if (pos >= 0) {
							pOldSubRow = (*peaOldSubRows)[pos];
						}
					}
					if (pParent && peaParentSubRows) {
						int pos = eaIndexedFindUsingInt(peaParentSubRows, iKey);
						if (pos >= 0) {
							pParentEntry = (*peaParentSubRows)[pos];
						}
					}
				} else if (peaOldSubRows && eaSize(peaOldSubRows) > j) {
					pOldSubRow = (*peaOldSubRows)[j];
				}

				for(k=0; k<numCols; ++k) {
					char ***peaComboStrings = NULL;
					MESubColData *pSubCol = pSubTable->eaCols[k];

					if (pSubCol->cbDataFunc) {
						peaComboStrings = &pSubCol->eaDictNames;
					}

					if (pSubCol->pcPTName) {
						ParseTable *pFieldParseTable = NULL;
						int iFieldCol = -1;
						void *pFieldData = NULL, *pFieldOrigData = NULL, *pFieldParentData = NULL;
						char *pcInheritName = NULL;
						char buf[1024];

						// Put the inheritance name in
						if (pSubTable->pcKeyPTName) {
							char nameBuf[260];
							char subNameBuf[260];
							strcpy(nameBuf, pSubCol->pcPTName);
							if (nameBuf[0] == '@') {
								char *pos;
								nameBuf[0] = '.';
								pos = strchr(nameBuf,'@');
								if (pos) {
									*pos = '\0';
								}
							}
							strcpy(subNameBuf, pSubTable->pcSubPTName);
							if (subNameBuf[0] == '@') {
								char *pos;
								subNameBuf[0] = '.';
								pos = strchr(subNameBuf,'@');
								if (pos) {
									*pos = '.';
								}
							}

							if ((subNameBuf[0] == '.') && (nameBuf[0] == '.')) {
								sprintf(buf,"%s[\"%d\"]%s",subNameBuf,iKey,nameBuf);
							} else if (subNameBuf[0] == '.') {
								sprintf(buf,"%s[\"%d\"].%s",subNameBuf,iKey,nameBuf);
							} else if (nameBuf[0] == '.') {
								sprintf(buf,".%s[\"%d\"]%s",subNameBuf,iKey,nameBuf);
							} else {
								sprintf(buf,".%s[\"%d\"].%s",subNameBuf,iKey,nameBuf);
							}
							pcInheritName = buf;
						}

						// Get the field's original, parent, and real data
						met_getFieldData(pSubTable->pParseTable, pSubCol->pcPTName, pOldSubRow, &pFieldParseTable, &iFieldCol, &pFieldOrigData);
						if (pParent) {
							met_getFieldData(pSubTable->pParseTable, pSubCol->pcPTName, pParentEntry, &pFieldParseTable, &iFieldCol, &pFieldParentData);
						}
						

						if (met_getFieldData(pSubTable->pParseTable, pSubCol->pcPTName, eaOrderedNewSubRows[j], &pFieldParseTable, &iFieldCol, &pFieldData) 
							&& (!pSubCol->pColParseTable || pSubCol->pColParseTable == pFieldParseTable)) 
						{
							pField = MEFieldCreate(pSubCol->eType,
									pFieldOrigData,pFieldData,pFieldParseTable,pFieldParseTable[iFieldCol].name,pFieldParentData,
									pOrigData,pNewData,pTable->pParseTable,pcInheritName,
									pSubCol->pcDictName,pSubCol->pDictParseTable,pSubCol->pcDictField,NULL,pSubCol->pcGlobalDictName,ui_ListColumnGetTitle(pSubCol->pListColumn),
									0,peaComboStrings,NULL,pSubCol->pEnum,pSubCol->pExprContext,
									-1,0,0,0,NULL);
						} else {
							// Unable to find the actual field
							pField = NULL;
						}
					} else {
						pField = NULL;
					}

					if (pField) {
						const char *dictName = RefSystem_GetDictionaryNameFromNameOrHandle(pTable->hDict);
						if (pSubCol->flags & ME_STATE_NOT_PARENTABLE) {
							pField->bNotParentable = true;
						}
						if (pSubCol->flags & ME_STATE_NOT_EDITABLE) {
							pField->bEditable = false;
						}
						if (pSubCol->flags & ME_STATE_NOT_REVERTABLE) {
							pField->bNotRevertable = true;
						}

						for(n=eaSize(&pSubCol->eaExtraNames)-1; n>=0; --n) {
							MEFieldAddAlternatePath(pField, pSubCol->eaExtraNames[n]);
						}

						MEFieldSetChangeCallback(pField, met_fieldSimpleChangeCallback, pTable);
						MEFieldSetPreChangeCallback(pField, met_fieldPreChangeCallback, pTable);

						if (dictName)
						{
							MEFieldSetRootDictName(pField, dictName);
						}
					}

					// Add column to the row
					eaPush(&eaFields,pField);
				}

				// Allocate the sub-row
				pSubRow = (METableSubRow*)calloc(1,sizeof(METableSubRow));
				pSubRow->eaFields = eaFields;
				if (pParentEntry) {
					pSubRow->bInherited = 1;
				}

				// Add sub-row
				eaPush(&pRow->eaSubData[i]->eaRows,pSubRow);
			}
		}
	}

	// Call post-open callback (if any)
	if (pTable->cbPostOpen) {
		pTable->cbPostOpen(pTable, pNewData, pOrigData);
	}

	// Do change callback on every field
	for(i=eaSize(&pRow->eaFields)-1; i>=0; --i) {
		if (pTable->eaCols[i]->cbChange) {
			pTable->eaCols[i]->cbChange(pTable, pRow->pObject, pTable->eaCols[i]->pChangeUserData, true);
		}
	}
	for(i=eaSize(&pRow->eaSubData)-1; i>=0; --i) {
		for(j=eaSize(&pRow->eaSubData[i]->eaRows)-1; j>=0; --j) {
			for(k=eaSize(&pRow->eaSubData[i]->eaRows[j]->eaFields)-1; k>=0; --k) {
				if (pTable->eaSubTables[i]->eaCols[k]->cbChange) {
					pTable->eaSubTables[i]->eaCols[k]->cbChange(pTable, pRow->pObject, pRow->eaSubData[i]->eaObjects[j], pTable->eaSubTables[i]->eaCols[k]->pChangeUserData, true);
				}
			}
		}
	}

	if (bTop) {
		eaMove(&pTable->eaRows, 0, eaSize(&pTable->eaRows)-1);
		if (bScrollTo) {
			ui_ListScrollToRow(pTable->pList,0);
		}
	} else if (bScrollTo) {
		ui_ListScrollToRow(pTable->pList,eaSize(&pTable->eaRows)-1);
	}

	pTable->bCheckSmartHidden = true;
}


void METableAddRow(METable *pTable, char *pcObjName, bool bTop, bool bScrollTo)
{	
	int i;
	void *pDictObject, *pOrigData, *pNewData;

	// Check if it is already open based on original name
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pOrigObject && stricmp(pcObjName,met_getOrigObjectName(pTable,i)) == 0) {
			// This object is already open, so scroll to it
			ui_ListScrollToRow(pTable->pList, i);
			return;
		}
	}

	resSetDictionaryEditMode(pTable->hDict, true);
	resSetDictionaryEditMode(gMessageDict, true);
	
	// Get the object by name from the dictionary
	pDictObject = RefSystem_ReferentFromString(pTable->hDict, pcObjName);

	// Clone it twice
	pOrigData = StructCloneVoid(pTable->pParseTable,pDictObject);
	pNewData = StructCloneVoid(pTable->pParseTable,pDictObject);

	// Set up for editing
	langMakeEditorCopy(pTable->pParseTable, pOrigData, true);
	langMakeEditorCopy(pTable->pParseTable, pNewData, true);

	// Now add it
	met_addRow(pTable, pOrigData, pNewData, bTop, bScrollTo);
}


bool METableAddRowByObject(METable *pTable, void *pObject, bool bTop, bool bScrollTo)
{	
	const char *pcObjName;
	int i;

	// Check if this name is already open
	pcObjName = TokenStoreGetString(pTable->pParseTable, pTable->iNameIndex, pObject, 0, NULL);

	if (resGetInfo(RefSystem_GetDictionaryNameFromNameOrHandle(pTable->hDict), pcObjName)) {
		ui_DialogPopup("Error", "Cannot create an object with the same name as an existing object");
		return false;
	}

	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (stricmp(pcObjName,met_getObjectName(pTable,i)) == 0) {
			// This object is already open so error
			ui_DialogPopup("Error", "Cannot create an object with the same name as an open and unsaved object");
			return false;
		}
	}

	resSetDictionaryEditMode(pTable->hDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Set up for editing
	langMakeEditorCopy(pTable->pParseTable, pObject, true);

	// Now add it
	met_addRow(pTable, NULL, pObject, bTop, bScrollTo);

	return true;
}


void METableAddChildOfObject(METable *pTable, void *pObject, bool bTop, bool bScrollTo)
{	
	// Create the child
	met_createChildObject(pTable, pObject, 0);

	// Scroll to it
	ui_ListScrollToRow(pTable->pList,0);
}


// Returns true if the original name or new name is in use
bool METableIsObjectOpen(METable *pTable, char *pcObjName)
{
	int i;

	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		const char *pcOrigName = met_getOrigObjectName(pTable,i);
		if ((stricmp(pcObjName,met_getObjectName(pTable,i)) == 0) ||
			(pcOrigName && stricmp(pcObjName,pcOrigName) == 0)) {
			return true;
		}
	}
	return false;
}


void METableCloseAll(METable *pTable)
{
	int i;

	// Close the rows
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		met_closeRow(pTable,i,false);
	}
}


void METableCloseObject(METable *pTable, void *pObject)
{
	int i;

	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Close the one row
			met_closeRow(pTable,i,false);
			break;
		}
	}
}


void METableRevertAll(METable *pTable)
{
	int i;

	// Revert the rows
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		met_revertRow(pTable, i, false);
	}
}


void METableRevertObject(METable *pTable, void *pObject)
{
	int i;

	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Revert the one row
			met_revertRow(pTable, i, false);
			break;
		}
	}
}


EMTaskStatus METableSaveAll(METable *pTable)
{
	int i;
	EMTaskStatus status = EM_TASK_SUCCEEDED;
	
	met_clearEditWidget(pTable);

	// Save the rows
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		EMTaskStatus tempStatus = met_saveRow(pTable, i, status != EM_TASK_FAILED);
		if (tempStatus == EM_TASK_FAILED)
		{
			status = tempStatus;			
		}
		else if (tempStatus == EM_TASK_INPROGRESS && status == EM_TASK_SUCCEEDED)
		{
			status = tempStatus;
		}
	}

	return status;
}


EMTaskStatus METableSaveObject(METable *pTable, void *pObject)
{
	int i;

	met_clearEditWidget(pTable);
	
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Save the one row
			return met_saveRow(pTable, i, true);
		}
	}

	return EM_TASK_SUCCEEDED;
}

void METableCheckOutAll(METable *pTable)
{
	int i;

	resSetDictionaryEditMode(pTable->hDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Revert the rows
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		resRequestLockResource(pTable->hDict,  met_getObjectName(pTable, i), pTable->eaRows[i]->pObject);
	}
}


void METableUndoCheckOutAll(METable *pTable)
{
	int i;

	resSetDictionaryEditMode(pTable->hDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Revert the rows
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		met_revertRow(pTable, i, 0);
		resRequestUnlockResource(pTable->hDict,  met_getObjectName(pTable, i), pTable->eaRows[i]->pObject);
	}
}


void METableExpandAllRows(METable *pTable, int eExpand)
{
	int i;

	// Set expand state on all rows
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		pTable->eaRows[i]->eExpand = eExpand;
	}
}


void METableRefreshRow(METable *pTable, void *pObject)
{
	int i, j, k;

	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Refresh the main fields
			met_refreshFieldsFromData(pTable, pTable->eaRows[i]->eaFields);

			// Refresh the subtables
			for(j=eaSize(&pTable->eaRows[i]->eaSubData)-1; j>=0; --j) {
				for(k=eaSize(&pTable->eaRows[i]->eaSubData[j]->eaRows)-1; k>=0; --k) {
					met_refreshFieldsFromData(pTable, pTable->eaRows[i]->eaSubData[j]->eaRows[k]->eaFields);
				}
			}
			return;
		}
	}
}


void *METableRegenerateRow(METable *pTable, void *pObject)
{
	int i;

	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Regenerate the row
			met_regenerateRow(pTable, i);

			// Return the new object
			return pTable->eaRows[i]->pObject;
		}
	}
	assertmsg(0,"Object not found in the table");
}


void METableDeleteSubRow(METable *pTable, void *pObject, int id, void *pSubObject)
{
	int i, j;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Now find the sub-row
			for(j=eaSize(&pTable->eaRows[i]->eaSubData[id]->eaRows)-1; j>=0; --j) {
				if (pTable->eaRows[i]->eaSubData[id]->eaObjects[j] == pSubObject) {
					met_deleteSubRow(pTable, i, id, j);
					return;
				}
			}
		}
	}
}


void METableHideSubTable(METable *pTable, void *pObject, int id, bool bHide)
{
	int i;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			if (pTable->eaRows[i]->eaSubData[id]->bHidden != bHide) {
				pTable->eaRows[i]->eaSubData[id]->bHidden = bHide;
				met_clearEditWidget(pTable);
				ui_ListClearEverySelection(pTable->pList);
			}
			return;
		}
	}
}


void METableHideColGroup(METable *pTable, char *pcGroupName, bool bHide, bool bAltTint)
{
	int i;
	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (pTable->eaCols[i]->pcGroup && (stricmp(pTable->eaCols[i]->pcGroup, pcGroupName) == 0)) {
			pTable->eaCols[i]->bGroupAltTint = bAltTint;
			pTable->eaCols[i]->bGroupHidden = bHide;
			pTable->eaCols[i]->pListColumn->bHidden = IS_HIDDEN(pTable->eaCols[i]);
		}
	}
	met_clearEditWidget(pTable);
	ui_ListClearEverySelection(pTable->pList);
}


void METableHideSubColGroup(METable *pTable, int id, char *pcGroupName, bool bHide, bool bAltTint)
{
	int i;
	bool bAllHidden = true;

	for(i=eaSize(&pTable->eaSubTables[id]->eaCols)-1; i>=0; --i) {
		if (pTable->eaSubTables[id]->eaCols[i]->pcGroup) {
			if (stricmp(pTable->eaSubTables[id]->eaCols[i]->pcGroup, pcGroupName) == 0) {
				pTable->eaSubTables[id]->eaCols[i]->bGroupAltTint = bAltTint;
				pTable->eaSubTables[id]->eaCols[i]->bGroupHidden = bHide;
				pTable->eaSubTables[id]->eaCols[i]->pListColumn->bHidden = IS_HIDDEN(pTable->eaSubTables[id]->eaCols[i]);
			}
			// Don't hide table if any group not group hidden or not hidden by mask
			// Keep table even if smart hidden
			if (!pTable->eaSubTables[id]->eaCols[i]->bGroupHidden && !(pTable->eaSubTables[id]->eaCols[i]->flags & ME_STATE_HIDDEN)) {
				bAllHidden = false;
			}
		}
	}

	// Hide table if all grouped columns hidden
	pTable->eaSubTables[id]->bHidden = bAllHidden;
	pTable->eaSubTables[id]->pList->fHeaderHeight = bAllHidden ? 0 : 20;
	met_clearEditWidget(pTable);
	ui_ListClearEverySelection(pTable->pList);
}

void METableSetFieldScale(METable *pTable, void *pObject, char *pcColName, F32 fScale)
{
	int i, j;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Now find the column
			for(j=eaSize(&pTable->eaCols)-1; j>=0; --j) {
				if (stricmp(pcColName, ui_ListColumnGetTitle(pTable->eaCols[j]->pListColumn)) == 0) {
					// Now set the field scale
					if (pTable->eaRows[i]->eaFields[j]) {
						pTable->eaRows[i]->eaFields[j]->fScale = fScale;
					}
					pTable->bRefreshEditWindow = true;
					return;
				}
			}
		}
	}
}

void METableSetFieldNotApplicable(METable *pTable, void *pObject, char *pcColName, bool bNotApplicable)
{
	int i, j;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Now find the column
			for(j=eaSize(&pTable->eaCols)-1; j>=0; --j) {
				if (stricmp(pcColName, ui_ListColumnGetTitle(pTable->eaCols[j]->pListColumn)) == 0) {
					// Now set it not applicable
					if (pTable->eaRows[i]->eaFields[j]) {
						pTable->eaRows[i]->eaFields[j]->bNotApplicable = bNotApplicable;
					}
					pTable->bCheckSmartHidden = true;
					pTable->bRefreshEditWindow = true;
					return;
				}
			}
		}
	}

	Errorf("METableSetFieldNotApplicable: Invalid field \"%s\" referenced\n",pcColName);
}


void METableSetGroupNotApplicable(METable *pTable, void *pObject, char *pcGroupName, bool bNotApplicable)
{
	int i, j;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Now find the column
			for(j=eaSize(&pTable->eaCols)-1; j>=0; --j) {
				if (pTable->eaCols[j]->pcGroup && (stricmp(pcGroupName, pTable->eaCols[j]->pcGroup) == 0)) {
					// Now set it not applicable
					if (pTable->eaRows[i]->eaFields[j]) {
						pTable->eaRows[i]->eaFields[j]->bNotApplicable = bNotApplicable;
					}
				}
			}
			pTable->bCheckSmartHidden = true;
			pTable->bRefreshEditWindow = true;
			return;
		}
	}
}

void METableSetSubFieldNotApplicable(METable *pTable, void *pObject, int id, void *pSubObject, const char *pcColName, bool bNotApplicable)
{
	int i, j, k;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Now find the sub-row
			if(eaSize(&pTable->eaRows[i]->eaSubData)<=id)
				return;
			ANALYSIS_ASSUME(pTable->eaRows[i]->eaSubData);
			for(j=eaSize(&pTable->eaRows[i]->eaSubData[id]->eaRows)-1; j>=0; --j) {
				if (pTable->eaRows[i]->eaSubData[id]->eaObjects[j] == pSubObject) {
					// Now find the columns in the group
					for(k=eaSize(&pTable->eaSubTables[id]->eaCols)-1; k>=0; --k) {
						if (stricmp(pcColName, ui_ListColumnGetTitle(pTable->eaSubTables[id]->eaCols[k]->pListColumn)) == 0) {
							// Now set the field not applicable
							if (pTable->eaRows[i]->eaSubData[id]->eaRows[j]->eaFields[k]) {
								pTable->eaRows[i]->eaSubData[id]->eaRows[j]->eaFields[k]->bNotApplicable = bNotApplicable;
							}
							break;
						}
					}
					pTable->bCheckSmartHidden = true;
					pTable->bRefreshEditWindow = true;
					return;
				}
			}
		}
	}
}

void METableSetSubFieldScale(METable *pTable, void *pObject, int id, void *pSubObject, char *pcColName, F32 fScale)
{
	int i, j, k;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Now find the sub-row
			if(eaSize(&pTable->eaRows[i]->eaSubData)<=id)
				return;
			ANALYSIS_ASSUME(pTable->eaRows[i]->eaSubData);
			for(j=eaSize(&pTable->eaRows[i]->eaSubData[id]->eaRows)-1; j>=0; --j) {
				if (pTable->eaRows[i]->eaSubData[id]->eaObjects[j] == pSubObject) {
					// Now find the columns in the group
					for(k=eaSize(&pTable->eaSubTables[id]->eaCols)-1; k>=0; --k) {
						if (stricmp(pcColName, ui_ListColumnGetTitle(pTable->eaSubTables[id]->eaCols[k]->pListColumn)) == 0) {
							// Now set the field scale
							if (pTable->eaRows[i]->eaSubData[id]->eaRows[j]->eaFields[k]) {
								pTable->eaRows[i]->eaSubData[id]->eaRows[j]->eaFields[k]->fScale = fScale;
							}
							break;
						}
					}
					pTable->bRefreshEditWindow = true;
					return;
				}
			}
		}
	}
}

void METableSetSubGroupNotApplicable(METable *pTable, void *pObject, int id, void *pSubObject, char *pcGroupName, bool bNotApplicable)
{
	int i, j, k;

	// find the row
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		if (pTable->eaRows[i]->pObject == pObject) {
			// Now find the sub-row
			for(j=eaSize(&pTable->eaRows[i]->eaSubData[id]->eaRows)-1; j>=0; --j) {
				if (pTable->eaRows[i]->eaSubData[id]->eaObjects[j] == pSubObject) {
					// Now find the columns in the group
					for(k=eaSize(&pTable->eaSubTables[id]->eaCols)-1; k>=0; --k) {
						if (pTable->eaSubTables[id]->eaCols[k]->pcGroup && (stricmp(pcGroupName, pTable->eaSubTables[id]->eaCols[k]->pcGroup) == 0)) {
							// Now set the field not applicable
							if (pTable->eaRows[i]->eaSubData[id]->eaRows[j]->eaFields[k]) {
								pTable->eaRows[i]->eaSubData[id]->eaRows[j]->eaFields[k]->bNotApplicable = bNotApplicable;
							}
						}
					}
					pTable->bCheckSmartHidden = true;
					pTable->bRefreshEditWindow = true;
					return;
				}
			}
		}
	}
}


// Set whether or not to hide unused columns
void METableSetHideUnused(METable *pTable, bool bHide)
{
	pTable->bHideUnused = bHide;
	pTable->bCheckSmartHidden = true;
	pTable->bRefreshEditWindow = true;
}


char *METableMakeNewName(METable *pTable, const char *pcBaseName)
{
	int count = 0;
	char nameBuf[260];
	char resultBuf[260];

	// Strip off trailing digits and underbar
	strcpy(nameBuf, pcBaseName);
	while(nameBuf[0] && (nameBuf[strlen(nameBuf)-1] >= '0') && (nameBuf[strlen(nameBuf)-1] <= '9')) {
		nameBuf[strlen(nameBuf)-1] = '\0';
	}
	if (nameBuf[0] && nameBuf[strlen(nameBuf)-1] == '_') {
		nameBuf[strlen(nameBuf)-1] = '\0';
	}

	// Generate new name
	for(count=1;;count++)
	{
		if (count==1)
			sprintf(resultBuf,"%s",nameBuf);
		else
			sprintf(resultBuf,"%s_%d",nameBuf,count);

		if ( !METableIsObjectOpen(pTable, resultBuf) &&
			 !resGetInfo(pTable->hDict,resultBuf) )
			break;
	}

	return StructAllocString(resultBuf);
}

const char *METableMakeNewNameShared(METable *pTable, const char *pcBaseName, bool stripTrailingDigits)
{
	int count = 0;
	char nameBuf[260];
	char resultBuf[260];

	strcpy(nameBuf, pcBaseName);
	if (stripTrailingDigits)
	{
		// Strip off trailing digits and underbar
		while(nameBuf[0] && (nameBuf[strlen(nameBuf)-1] >= '0') && (nameBuf[strlen(nameBuf)-1] <= '9')) {
			nameBuf[strlen(nameBuf)-1] = '\0';
		}
		if (nameBuf[0] && nameBuf[strlen(nameBuf)-1] == '_') {
			nameBuf[strlen(nameBuf)-1] = '\0';
		}
	}

	// Generate new name
	for(count=1;;count++)
	{
		if (count==1)
			sprintf(resultBuf,"%s",nameBuf);
		else
			sprintf(resultBuf,"%s_%d",nameBuf,count);

		if ( !METableIsObjectOpen(pTable, resultBuf) &&
			!resGetInfo(pTable->hDict,resultBuf) )
			break;
	}

	return allocAddString(resultBuf);
}


void METableDictChanged(METable *pTable, enumResourceEventType eType, Referent pReferent, const char *pResourceName)
{
	const char *pcName = allocAddString(pResourceName);
	EMEditor *pEditor = pTable->pEditorDoc->emDoc.editor;
	
	// If an object is modified, removed, or added, need to scan for updates to the UI
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {
		eaPushUnique(&pTable->eaChangedObjectNames,pcName);
	}	
}


void METableMessageChangedRefresh(METable *pTable, const char *pcMessageKey)
{
	int i;

	// Consider a row as needing update if its name appears in the message key somewhere
	for(i=eaSize(&pTable->eaRows)-1; i>=0; --i) {
		const char *pcName = allocAddString(met_getObjectName(pTable, i));
		eaPushUnique(&pTable->eaMessageChangedObjectNames,pcName);
	}
}


void METableSetValidateCallback(METable *pTable, MEValidateFunc cbValidate, void *pUserData)
{
	pTable->cbValidate = cbValidate;
	pTable->pValidateUserData = pUserData;
}


void METableSetCreateCallback(METable *pTable, MECreateObjectFunc cbCreateObject)
{
	pTable->cbCreateObject = cbCreateObject;
}


void METableSetPostOpenCallback(METable *pTable, MEPostOpenFunc cbPostOpen)
{
	pTable->cbPostOpen = cbPostOpen;
}


void METableSetPreSaveCallback(METable *pTable, MEPreSaveFunc cbPreSave)
{
	pTable->cbPreSave = cbPreSave;
}


void METableSetInheritanceFixCallback(METable *pTable, MEInheritanceFixFunc cbFixInheritance)
{
	pTable->cbFixInheritance = cbFixInheritance;
}

void METableSetColumnChangeCallback(METable *pTable, char *pcColName, METableChangeFunc cbChange, void *pUserData)
{
	int i;

	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (stricmp(ui_ListColumnGetTitle(pTable->eaCols[i]->pListColumn), pcColName) == 0) {
			pTable->eaCols[i]->cbChange = cbChange;
			pTable->eaCols[i]->pChangeUserData = pUserData;
			break;
		}
	}
}


void METableSetSubColumnChangeCallback(METable *pTable, int id, char *pcColName, METableSubChangeFunc cbChange, void *pUserData)
{
	int i,j;

	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		if (pTable->eaSubTables[i]->id == id) {
			for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
				if (stricmp(ui_ListColumnGetTitle(pTable->eaSubTables[i]->eaCols[j]->pListColumn), pcColName) == 0) {
					pTable->eaSubTables[i]->eaCols[j]->cbChange = cbChange;
					pTable->eaSubTables[i]->eaCols[j]->pChangeUserData = pUserData;
					break;
				}
			}
		}
	}
}


bool METableCanEditFields(METable *pTable)
{
	MEField **eaFields = NULL;
	bool bHasEditable = false, bHasTypeMismatch = false;
	int i;
	MEField *pEditField = NULL;

	// Scan the selection
	met_getSelectedFields(pTable, &eaFields);
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i]) {
			if (eaFields[i]->bEditable) {
				bHasEditable = true;
			}
			if (!pEditField) {
				pEditField = eaFields[i];
			} else if (!MEFieldHasCompatibleTypes(eaFields[i],pEditField)) {
				bHasTypeMismatch = true;
			}
			if (eaFields[i]->bNotGroupEditable) {
				bHasTypeMismatch = true;
			}
		}
	}
	eaDestroy(&eaFields);

	return bHasEditable && !bHasTypeMismatch;
}


bool METableCanRevertFields(METable *pTable)
{
	MEField **eaFields = NULL;
	bool bHasDirty = false;
	int i;

	// Scan the selection
	met_getSelectedFields(pTable, &eaFields);
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i]) {
			if (eaFields[i]->bDirty) {
				bHasDirty = true;
				break;
			}
		}
	}
	eaDestroy(&eaFields);

	return bHasDirty;
}


bool METableCanInheritFields(METable *pTable)
{
	MEField **eaFields = NULL;
	bool bHasUnparented = false;
	int i;

	// Scan the selection
	met_getSelectedFields(pTable, &eaFields);
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i]) {
			if (eaFields[i]->pParent && !eaFields[i]->bNotParentable && !eaFields[i]->bParented) {
				bHasUnparented = true;
				break;
			}
		}
	}
	eaDestroy(&eaFields);

	return bHasUnparented;
}


bool METableCanNoInheritFields(METable *pTable)
{
	MEField **eaFields = NULL;
	bool bHasParented = false;
	int i;

	// Scan the selection
	met_getSelectedFields(pTable, &eaFields);
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i]) {
			if (eaFields[i]->bParented) {
				bHasParented = true;
				break;
			}
		}
	}
	eaDestroy(&eaFields);

	return bHasParented;
}


bool METableCanOpenFieldsInEditor(METable *pTable)
{
	MEField **eaFields = NULL;
	bool bHasEditable = false, bHasTypeMismatch = false, bCanOpen = true;
	int i;
	MEField *pEditField = NULL;

	// Scan the selection
	met_getSelectedFields(pTable, &eaFields);
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i]) {
			if (eaFields[i]->bEditable) {
				bHasEditable = true;
			}
			if (!pEditField) {
				pEditField = eaFields[i];
			} else if (!MEFieldHasCompatibleTypes(eaFields[i],pEditField)) {
				bHasTypeMismatch = true;
			}
			if (bCanOpen) {
				bCanOpen = MEFieldCanOpenEMEditor(pEditField);
			}
		}
	}
	eaDestroy(&eaFields);

	return bHasEditable && !bHasTypeMismatch && bCanOpen;
}


void METableEditFields(METable *pTable)
{
	MEField **eaFields = NULL;
	UIWindow *pWin = NULL;

	met_getSelectedFields(pTable, &eaFields);

	if (eaSize(&eaFields) == 1) {
		pWin = MEFieldOpenEditor(eaFields[0], NULL);
		eaDestroy(&eaFields);
	} else if (eaSize(&eaFields) > 1) {
		pWin = MEFieldOpenMultiEditor(eaFields, NULL);
	}

	if (pWin) {
		pWin->widget.scale = emGetEditorScale(pTable->pEditorDoc->emDoc.editor);
	}
}


void METableRevertFields(METable *pTable)
{
	MEField **eaFields = NULL;
	int i;

	met_getSelectedFields(pTable, &eaFields);

	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i] && eaFields[i]->bDirty) {
			MEFieldRevert(eaFields[i]);
		}
	}

	eaDestroy(&eaFields);
}


void METableInheritFields(METable *pTable)
{
	MEField **eaFields = NULL;
	int i;

	met_getSelectedFields(pTable, &eaFields);

	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i] && !eaFields[i]->bParented && !eaFields[i]->bNotParentable) {
			MEUpdateFieldParented(eaFields[i],1);
		}
	}

	eaDestroy(&eaFields);
}


void METableNoInheritFields(METable *pTable)
{
	MEField **eaFields = NULL;
	int i;

	met_getSelectedFields(pTable, &eaFields);

	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i] && eaFields[i]->bParented) {
			MEUpdateFieldParented(eaFields[i],0);
		}
	}

	eaDestroy(&eaFields);
}


void METableOpenFieldsInEditor(METable *pTable)
{
	MEField **eaFields = NULL;
	int i;

	met_getSelectedFields(pTable, &eaFields);

	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (MEFieldCanOpenEMEditor(eaFields[i])) {
			MEFieldOpenEMEditor(eaFields[i]);
		}
	}

	eaDestroy(&eaFields);
}

void METableGetAllObjectsWithSelectedFields(METable *pTable, void*** peaObjects)
{
	met_getSelectedObjects(pTable, peaObjects);
}


void METableSavePrefs(METable *pTable)
{
	int i,j;

	// Save column width prefs
	for(i=eaSize(&pTable->eaCols)-1; i>=0; --i) {
		if (pTable->eaCols[i]->pListColumn->fWidth != pTable->eaCols[i]->fDefaultWidth) {
			EditorPrefStoreFloat(pTable->pcDisplayName, PREF_COLUMN_WIDTH, ui_ListColumnGetTitle(pTable->eaCols[i]->pListColumn), pTable->eaCols[i]->pListColumn->fWidth);
		} else {
			EditorPrefClear(pTable->pcDisplayName, PREF_COLUMN_WIDTH, ui_ListColumnGetTitle(pTable->eaCols[i]->pListColumn));
		}
	}

	// Save sub column width prefs
	for(i=eaSize(&pTable->eaSubTables)-1; i>=0; --i) {
		char buf[260];
		MESubTable *pSubTable = pTable->eaSubTables[i];
		sprintf(buf, "%s_%d", PREF_COLUMN_WIDTH, i);
		for(j=eaSize(&pTable->eaSubTables[i]->eaCols)-1; j>=0; --j) {
			if (pSubTable->eaCols[j]->pListColumn->fWidth != pSubTable->eaCols[j]->fDefaultWidth) {
				EditorPrefStoreFloat(pTable->pcDisplayName, buf, ui_ListColumnGetTitle(pSubTable->eaCols[j]->pListColumn), pSubTable->eaCols[j]->pListColumn->fWidth);
			} else {
				EditorPrefClear(pTable->pcDisplayName, PREF_COLUMN_WIDTH, ui_ListColumnGetTitle(pSubTable->eaCols[j]->pListColumn));
			}
		}
	}
}


#endif

//
// Include the auto-generated code so it gets compiled
//
#include "MultiEditTable.h"
#include "AutoGen/MultiEditTable_h_ast.c"
