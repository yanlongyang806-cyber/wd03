#define GENESIS_ALLOW_OLD_HEADERS
#ifndef NO_EDITORS

#include"GenesisEpisodeEditor.h"

#include"Color.h"
#include"EditLibUIUtil.h"
#include"EditorPrefs.h"
#include"Genesis.h"
#include"GfxDebug.h"
#include"MultiEditField.h"
#include"StringCache.h"
#include"UIGimmeButton.h"
#include"UIWindow.h"
#include"WorldEditorUI.h"
#include"error.h"
#include"file.h"
#include"fileutil2.h"
#include"gimmeDLLWrapper.h"
#include "partition_enums.h"
#include"wlGenesis.h"
#include"wlGenesisExterior.h"
#include"wlGenesisInterior.h"
#include"wlGenesisMissions.h"
#include"wlGenesisRoom.h"
#include"wlGenesisSolarSystem.h"
#include"gimmeDLLWrapper.h"
#include"tokenstore.h"
#include"gameeditorshared.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/// Function prototypes and type definitions
#define X_OFFSET_BASE		4
#define X_OFFSET_INDENT		15
#define X_OFFSET_CONTROL	125

#define STANDARD_ROW_HEIGHT 26
#define SEPARATOR_HEIGHT    11

typedef struct GEPUIMakeEpisodesWindow {
	UIWindow *win;
	UITextEntry *pCountEntry;
	UITextEntry *pStartNumEntry;
	GenesisEpisode *pEpisode;
} GEPUIMakeEpisodesWindow;

static EpisodeEditDoc *GEPInitDoc(GenesisEpisode *pEpisode, bool bCreated);
static UIWindow *GEPInitMainWindow(EpisodeEditDoc *pDoc);
static void GEPInitDisplay(EMEditor *pEditor, EpisodeEditDoc *pDoc);
static void GEPUpdateDisplay(EpisodeEditDoc *pDoc);
static void GEPUpdateInfrastructure(EpisodeEditDoc *pDoc);
static void GEPEpisodePostOpenFixup(GenesisEpisode *pEpisode);
static void GEPEpisodePreSaveFixup(GenesisEpisode *pEpisode);
static void GEPInitToolbarsAndMenus(EMEditor *pEditor);
static void GEPContentDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData);
static void GEPRefreshTagLists(void);
static void GEPAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EpisodeEditDoc *pDoc);
static bool GEPFieldPreChangeCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc);
static void GEPFieldChangedCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc);
static void GEPSetNameCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc);
static void GEPSetScopeCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc);
static void GEPRefreshData(EpisodeEditDoc *pDoc);
static void GEPEpisodeChanged(EpisodeEditDoc *pDoc, bool bUndoable);
static void GEPEpisodeUndoCB(EpisodeEditDoc *pDoc, GEPUndoData *pData);
static void GEPEpisodeRedoCB(EpisodeEditDoc *pDoc, GEPUndoData *pData);
static void GEPEpisodeUndoFreeCB(EpisodeEditDoc *pDoc, GEPUndoData *pData);

static void GEPMakeEpisodes(UIButton *pButton, EpisodeEditDoc* pDoc);
static void GEPMakeEpisodesWindowOk( UIButton* pButton, GEPUIMakeEpisodesWindow* ui );
static bool GEPMakeEpisodesWindowDestroy( UIButton* pButton, GEPUIMakeEpisodesWindow* ui );
static void GEPMakeEpisodesBrowserOk( const char* dir, const char* name, GEPUIMakeEpisodesWindow* ui );
static void GEPMakeEpisodesBrowserDestroy( GEPUIMakeEpisodesWindow* ui );

static UIExpander* GEPCreateExpander(UIExpanderGroup *pGroup, const char* pcName, int index);
static void GEPRefreshLabel(UILabel** ppLabel, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent);
static void GEPRefreshButton(UIButton** ppButton, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent,
							 UIActivationFunc clickedFn, UserData clickedData );
static int GEPRefreshAddRemoveButtons(UIButton*** peaButtons, int numButtons, int y,
									  UIWidget* pParent, UIActivationFunc addFunc, UIActivationFunc removeFunc, void* data );
static void GEPRefreshFieldSimple( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
								   int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
								   ParseTable* pTable, const char* pchField );
static int GMDRefreshEArrayFieldSimple(
		MEField*** peaField, MEFieldType fieldType, void* pOld, void* pNew,
		int x, int y, F32 w, int pad, UIWidget* pParent, EpisodeEditDoc* pDoc,
		ParseTable* pTable, const char* pcField);
static void GEPRefreshFieldSimpleEnum( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
									   int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
									   ParseTable* pTable, const char* pchField, StaticDefineInt* pEnum );
static void GEPRefreshFieldSimpleGlobalDictionary( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
												   int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
												   ParseTable* pTable, const char* pchField,
												   const char* pchDictName );
static void GEPRefreshFieldSimpleDataProvided( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
											   int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
											   ParseTable* pTable, const char* pchField, const char*** pModel );

static void GEPAddPart(UIButton* pButton, EpisodeEditDoc *pDoc);
static void GEPRemovePart(UIButton* pButton, void *ignored);
static void GEPContinuePromptBodyTextAdd(UIButton* pButton, GEPEpisodePartGroup *pGroup);
static void GEPContinuePromptBodyTextRemove(UIButton* pButton, GEPEpisodePartGroup *pGroup);
static void GEPFreeEpisodePartGroup(GEPEpisodePartGroup *pGroup);
static void GEPRefreshPartInfrastructure( EpisodeEditDoc *pDoc, GEPEpisodePartGroup *pGroup, int index, GenesisEpisodePart *part );
static void GEPRefreshPart(EpisodeEditDoc *pDoc, GEPEpisodePartGroup *pGroup, int index, GenesisEpisodePart *origPart, GenesisEpisodePart *part,
						   GenesisEpisodePart *origNextPart, GenesisEpisodePart *nextPart);
static void GEPEpisodePartMissionChanged(MEField *pField, bool finished, GEPEpisodePartGroup *pGroup);
static void GEPPartEditMapDesc(UIButton* button, GEPEpisodePartGroup* pGroup);

static void GEPFreeMissionInfoGroup(GEPMissionInfoGroup *pGroup);
static void GEPRefreshMissionInfo(EpisodeEditDoc *pDoc, GEPMissionInfoGroup *pGroup, GenesisEpisode *orig, GenesisEpisode *new);

static void GEPFreeMissionStartGroup(GEPMissionStartGroup *pGroup);
static void GEPRefreshMissionStart(EpisodeEditDoc *pDoc, GEPMissionStartGroup *pGroup, GenesisEpisodeGrantDescription *orig, GenesisEpisodeGrantDescription *new);
static bool GEPQueryIfNotEpisodeDirs( char** dirs, char** names );

/// Global Data
static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;

static char **geaScopes = NULL;
static const char **geaOptionalActionCategories = NULL;

extern EMEditor s_EpisodeEditor;

static UISkin *gBoldExpanderSkin;

static int GEPStringCompare( const char** str1, const char** str2 )
{
	return stricmp(*str1, *str2);
}

static void GEPCheckNameList( char*** peaCurrentList, char*** peaNewList )
{
	bool bChanged = false;
	int i;

	if (eaSize(peaCurrentList) == eaSize(peaNewList)) {
		for(i=eaSize(peaCurrentList)-1; i>=0; --i) {
			if (stricmp((*peaCurrentList)[i], (*peaNewList)[i]) != 0) {
				bChanged = true;
				break;
			}
		}
	} else {
		bChanged = true;
	}
	if (bChanged) {
		eaDestroyEx(peaCurrentList, NULL);
		*peaCurrentList = *peaNewList;
	} else {
		eaDestroyEx(peaNewList, NULL);
	}
}

/// EM functionality
EpisodeEditDoc *GEPOpenEpisode(EMEditor *pEditor, char *pcName)
{
	EpisodeEditDoc *pDoc = NULL;
	GenesisEpisode *pEpisode = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_EpisodeDictionary, pcName)) {
		// Simply open the object since it is in the dictionary
		pEpisode = RefSystem_ReferentFromString(g_EpisodeDictionary, pcName);
	} else if (pcName) {
		// Wait for the object to show up so we can open it
		resSetDictionaryEditMode(g_EpisodeDictionary, true);
		resSetDictionaryEditMode("PetContactList", true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_EpisodeDictionary, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pEpisode || bCreated) {
		pDoc = GEPInitDoc(pEpisode, bCreated);
		GEPInitDisplay(pEditor, pDoc);
		resFixFilename(g_EpisodeDictionary, pDoc->pEpisode->name, pDoc->pEpisode);
	}
	
	return pDoc;
}

void GEPRevertEpisode(EpisodeEditDoc *pDoc)
{
	GenesisEpisode *pEpisode;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pEpisode = RefSystem_ReferentFromString(g_EpisodeDictionary, pDoc->emDoc.orig_doc_name);
	if (pEpisode) {
		// Revert the episode
		StructDestroy(parse_GenesisEpisode, pDoc->pEpisode);
		StructDestroy(parse_GenesisEpisode, pDoc->pOrigEpisode);
		pDoc->pEpisode = StructClone(parse_GenesisEpisode, pEpisode);
		GEPEpisodePostOpenFixup(pDoc->pEpisode);
		pDoc->pOrigEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_GenesisEpisode, pDoc->pNextUndoEpisode);
		pDoc->pNextUndoEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		GEPUpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	}
}

void GEPCloseEpisode(EpisodeEditDoc *pDoc)
{
	// Free the objects
	StructDestroy(parse_GenesisEpisode, pDoc->pEpisode);
	if (pDoc->pOrigEpisode) {
		StructDestroy(parse_GenesisEpisode, pDoc->pOrigEpisode);
	}
	StructDestroy(parse_GenesisEpisode, pDoc->pNextUndoEpisode);

	// Close the window
	ui_WindowHide(pDoc->pMainWindow);
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
}

EMTaskStatus GEPSaveEpisode(EpisodeEditDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	char *pcName;
	GenesisEpisode *pEpisodeCopy;

	// Deal with state changes
	pcName = pDoc->pEpisode->name;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pEpisodeCopy = StructClone(parse_GenesisEpisode, pDoc->pEpisode);
	GEPEpisodePreSaveFixup(pEpisodeCopy);

	// Perform validation
	//if (!episode_Validate(pEpisodeCopy)) {
	//	StructDestroy(parse_GenesisEpisode, pEpisodeCopy);
	//	return EM_TASK_FAILED;
	//}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pEpisodeCopy, pDoc->pOrigEpisode, bSaveAsNew);

	return status;
}

void GEPInitData(EMEditor *pEditor)
{
	if (pEditor && !gInitializedEditor) {
		GEPInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "Episode", true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(g_EpisodeDictionary, &geaScopes);
		eaClear( &geaOptionalActionCategories );
		{
			int it;
			for( it = 0; it != eaSize( &g_eaOptionalActionCategoryDefs ); ++it ) {
				eaPush( &geaOptionalActionCategories, g_eaOptionalActionCategoryDefs[it]->pcName );
			}
			eaPush( &geaOptionalActionCategories, allocAddString( "None" ));
		}
		
		gInitializedEditor = true;
	}

	if (!gInitializedEditorData) {
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		// Make sure genesis dictionaries are loaded for use by the editor
		//TOOD: need this -- genesisLoadMapDescLibrary();

		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(g_EpisodeDictionary, GEPContentDictChanged, NULL);
		resDictRegisterEventCallback(g_MapDescDictionary, GEPContentDictChanged, NULL);

		gInitializedEditorData = true;
	}
}

EpisodeEditDoc *GEPInitDoc(GenesisEpisode *pEpisode, bool bCreated)
{
	EpisodeEditDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (EpisodeEditDoc*)calloc(1,sizeof(EpisodeEditDoc));

	// Fill in the map description data
	if (bCreated) {
		pDoc->pEpisode = StructCreate(parse_GenesisEpisode);
		assert(pDoc->pEpisode);
		emMakeUniqueDocName(&pDoc->emDoc, "New_Episode", "Episode", "Episode");
		pDoc->pEpisode->name = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "genesis/episodes/%s.episode", pDoc->pEpisode->name);
		pDoc->pEpisode->filename = allocAddFilename(nameBuf);
		GEPEpisodePostOpenFixup(pDoc->pEpisode);
	} else {
		pDoc->pEpisode = StructClone(parse_GenesisEpisode, pEpisode);
		assert(pDoc->pEpisode);
		GEPEpisodePostOpenFixup(pDoc->pEpisode);
		pDoc->pOrigEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);

	return pDoc;
}

UIWindow *GEPInitMainWindow(EpisodeEditDoc *pDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;

	// Create the window
	pWin = ui_WindowCreate(pDoc->pEpisode->name, 15, 50, 950, 600);
	pWin->minW = 950;
	pWin->minH = 400;
	EditorPrefGetWindowPosition(EPISODE_EDITOR, "Window Position", "Main", pWin);

	// Make Episode functionality
	{
		UIButton* pButton = ui_ButtonCreate("Make Episodes", 0, 0, GEPMakeEpisodes, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 5, 0, 0, 0, UITopRight);
		ui_WindowAddChild(pWin, pButton);
	}

	// Name
	pLabel = ui_LabelCreate("Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigEpisode, pDoc->pEpisode, parse_GenesisEpisode, "Name");
	GEPAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, GEPSetNameCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigEpisode, pDoc->pEpisode, parse_GenesisEpisode, "Scope", NULL, &geaScopes, NULL);
	GEPAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, GEPSetScopeCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);
	
	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "Episode", pDoc->pEpisode->name, pDoc->pEpisode);
	ui_WindowAddChild(pWin, pDoc->pFileButton);
	pLabel = ui_LabelCreate(pDoc->pEpisode->filename, X_OFFSET_CONTROL+20, y);
	ui_WindowAddChild(pWin, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 0.4, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	y += STANDARD_ROW_HEIGHT;

	// Comments
	pLabel = ui_LabelCreate("Comments", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_MultiText, pDoc->pOrigEpisode, pDoc->pEpisode, parse_GenesisEpisode, "Comments");
	GEPAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	fTopY = MAX( fTopY, y + SEPARATOR_HEIGHT );

	/// LAYOUT DEFINITION GOES HERE
	pDoc->pPartArea = ui_ScrollAreaCreate( 0, fTopY, 0, 0, 1.0, 1.0, true, true );
	pDoc->pPartArea->autosize = true;
	ui_WidgetSetDimensionsEx( UI_WIDGET( pDoc->pPartArea ), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage );
	ui_WindowAddChild( pWin, pDoc->pPartArea );

	pDoc->pMissionExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetDimensionsEx( UI_WIDGET( pDoc->pMissionExpanderGroup ), 350, 1.0, UIUnitFixed, UIUnitPercentage );
	UI_SET_STYLE_BORDER_NAME(pDoc->pMissionExpanderGroup->hBorder, "Default_MiniFrame_Empty");
	ui_ScrollAreaAddChild( pDoc->pPartArea, pDoc->pMissionExpanderGroup );
	
	pDoc->pAddPartButton = ui_ButtonCreate( "+", 0, 0, GEPAddPart, pDoc);
	ui_WidgetSetDimensions( UI_WIDGET( pDoc->pAddPartButton ), 32, 32 );
	ui_ScrollAreaAddChild(pDoc->pPartArea, pDoc->pAddPartButton);
	
	return pWin;
}

void GEPInitDisplay(EMEditor *pEditor, EpisodeEditDoc *pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pMainWindow = GEPInitMainWindow(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	// Show the window
	ui_WindowPresent(pDoc->pMainWindow);

	// Editor Manager needs to be told about the windows used
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);

	// Update the rest of the UI
	GEPUpdateDisplay(pDoc);
}

void GEPInitToolbarsAndMenus(EMEditor *pEditor)
{
	EMToolbar *pToolbar;

	// Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// File menu
	emMenuItemCreate(pEditor, "gep_revertepisode", "Revert", NULL, NULL, "GEP_RevertEpisode");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "gep_revertepisode", NULL));
}

void GEPUpdateDisplay(EpisodeEditDoc *pDoc)
{
	int i;

	// Ignore changes while UI refreshes
	pDoc->bIgnoreFieldChanges = true;

	// Refresh data
	GEPRefreshData(pDoc);

	// Refresh doc-level fields
	for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigEpisode, pDoc->pEpisode);
	}
	
	ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pEpisode->name);
	ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pEpisode);
	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pEpisode->filename);

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigEpisode && (StructCompare(parse_GenesisEpisode, pDoc->pOrigEpisode, pDoc->pEpisode, 0, 0, 0) == 0);

	// Refresh the mission data
	if (!pDoc->pMissionInfoGroup) {
		GEPMissionInfoGroup *missionInfoGroup = calloc( 1, sizeof( GEPMissionInfoGroup ));
		missionInfoGroup->pExpander = GEPCreateExpander( pDoc->pMissionExpanderGroup, "Mission Info", 0 );
		missionInfoGroup->pDoc = pDoc;
		pDoc->pMissionInfoGroup = missionInfoGroup;
	}
	GEPRefreshMissionInfo(pDoc, pDoc->pMissionInfoGroup, pDoc->pOrigEpisode, pDoc->pEpisode);
	
	if( !pDoc->pMissionStartGroup ) {
		GEPMissionStartGroup *missionStartGroup = calloc( 1, sizeof( GEPMissionStartGroup ));
		missionStartGroup->pExpander = GEPCreateExpander( pDoc->pMissionExpanderGroup, "Mission Turn In", 1 );
		missionStartGroup->pDoc = pDoc;
		pDoc->pMissionStartGroup = missionStartGroup;
	}
	GEPRefreshMissionStart(pDoc, pDoc->pMissionStartGroup, SAFE_MEMBER_ADDR(pDoc->pOrigEpisode, grantDescription), &pDoc->pEpisode->grantDescription);

	// Refresh the parts
	{
		int iNumParts = eaSize( &pDoc->pEpisode->parts);
		int it;
		for( it = eaSize(&pDoc->eaEpisodePartGroups)-1; it>=iNumParts; --it) {
			GEPFreeEpisodePartGroup(pDoc->eaEpisodePartGroups[it]);
			eaRemove(&pDoc->eaEpisodePartGroups, it);
		}
		
		for( it = 0; it != iNumParts; ++it ) {
			GenesisEpisodePart *part = pDoc->pEpisode->parts[it];
			GenesisEpisodePart *origPart = NULL;
			GenesisEpisodePart *nextPart = NULL;
			GenesisEpisodePart *origNextPart = NULL;

			if(eaSize(&pDoc->eaEpisodePartGroups) <= it) {
				GEPEpisodePartGroup *partGroup = calloc( 1, sizeof( GEPEpisodePartGroup ));

				partGroup->pExpanderGroup = ui_ExpanderGroupCreate();
				UI_SET_STYLE_BORDER_NAME(partGroup->pExpanderGroup->hBorder, "Default_MiniFrame_Empty");
				partGroup->pExpander = GEPCreateExpander( partGroup->pExpanderGroup, "Part", 0 );
				partGroup->pTransitionExpander = GEPCreateExpander( partGroup->pExpanderGroup, "Transition Overrides", 1 );
				ui_ExpanderSetOpened( partGroup->pTransitionExpander,
									  (IS_HANDLE_ACTIVE( part->hStartTransitionOverride )
									   || IS_HANDLE_ACTIVE( part->continuePromptCostume.hCostume )
									   || IS_HANDLE_ACTIVE( part->continuePromptCostume.hPetCostume )));
				ui_WidgetSetDimensionsEx( UI_WIDGET( partGroup->pExpanderGroup ), 350, 1.0, UIUnitFixed, UIUnitPercentage );
				partGroup->pDoc = pDoc;

				partGroup->pDelButton = ui_ButtonCreate( "X", 0, 0, NULL, NULL );
				partGroup->pDelButton->widget.priority = 100;
				ui_WidgetSetDimensions( UI_WIDGET( partGroup->pDelButton ), 16, 16 );
				ui_WidgetSetPositionEx( UI_WIDGET( partGroup->pDelButton ), 2, 4, 0, 0, UITopRight );
				ui_WidgetAddChild( UI_WIDGET( partGroup->pExpanderGroup ), UI_WIDGET( partGroup->pDelButton ));

				ui_ScrollAreaAddChild( pDoc->pPartArea, partGroup->pExpanderGroup );
				eaPush( &pDoc->eaEpisodePartGroups, partGroup );
			}

			if (pDoc->pOrigEpisode && eaSize(&pDoc->pOrigEpisode->parts) > it) {
				origPart = pDoc->pOrigEpisode->parts[it];
			}
			if (it + 1 < iNumParts) {
				nextPart = pDoc->pEpisode->parts[it + 1];
			}
			if (pDoc->pOrigEpisode && eaSize(&pDoc->pOrigEpisode->parts) > it + 1) {
				origNextPart = pDoc->pOrigEpisode->parts[it + 1];
			}

			ui_WidgetSetPosition(UI_WIDGET(pDoc->eaEpisodePartGroups[it]->pExpanderGroup), 354 * (it + 1), ui_WidgetGetY(UI_WIDGET(pDoc->eaEpisodePartGroups[it]->pExpanderGroup)));
			GEPRefreshPart(pDoc, pDoc->eaEpisodePartGroups[it], it, origPart, part, origNextPart, nextPart);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pAddPartButton), 354 * (it + 1), ui_WidgetGetY(UI_WIDGET(pDoc->pAddPartButton)));
	}

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = false;
}

void GEPUpdateInfrastructure(EpisodeEditDoc *pDoc)
{
	int numPartGroups = eaSize(&pDoc->eaEpisodePartGroups);
	int numParts = eaSize(&pDoc->pEpisode->parts);

	int it;
	for( it = 0; it != MIN(numPartGroups, numParts); ++it ) {
		GenesisEpisodePart *part = pDoc->pEpisode->parts[it];
		GenesisEpisodePart *origPart = NULL;

		GEPRefreshPartInfrastructure(pDoc, pDoc->eaEpisodePartGroups[it], it, part);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pAddPartButton), 354 * (it + 1), ui_WidgetGetY(UI_WIDGET(pDoc->pAddPartButton)));
}

void GEPEpisodePostOpenFixup(GenesisEpisode *pEpisode)
{
}

void GEPEpisodePreSaveFixup(GenesisEpisode *pEpisode)
{
	// make sure there are no transition overrides that couldn't be exposed
	if (eaSize(&pEpisode->parts) > 0) {
		REMOVE_HANDLE(pEpisode->parts[0]->hStartTransitionOverride);
		REMOVE_HANDLE(pEpisode->parts[ eaSize(&pEpisode->parts) - 1 ]->hContinueTransitionOverride);
	}

	GEMissionLevelDefPreSaveFixup( &pEpisode->levelDef );

	{
		int it;
		for( it = 0; it != eaSize(&pEpisode->parts); ++it ) {
			GenesisEpisodePart* part = pEpisode->parts[it];

			if( part->continuePromptCostume.eCostumeType == GenesisMissionCostumeType_PetCostume ) {
				REMOVE_HANDLE(part->continuePromptCostume.hCostume);
			} else {
				REMOVE_HANDLE(part->continuePromptCostume.hPetCostume);
			}
		}
	}
}

static void GEPIndexChangedCB(void *unused)
{
	gIndexChanged = false;
	GEPRefreshTagLists();

	{
		int it;
		for( it = 0; it != eaSize( &s_EpisodeEditor.open_docs ); ++it ) {
			GEPUpdateInfrastructure( (EpisodeEditDoc*)s_EpisodeEditor.open_docs[ it ]);
		}
	}
}

void GEPContentDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if (eType != RESEVENT_NO_REFERENCES && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(GEPIndexChangedCB, NULL);
	}
}

void GEPRefreshTagLists(void)
{
}

void GEPAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EpisodeEditDoc *pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, GEPFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, GEPFieldPreChangeCB, pDoc);
}

// This is called by MEField prior to allowing an edit
bool GEPFieldPreChangeCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	return emDocIsEditable(&pDoc->emDoc, true);
}


// This is called when an MEField is changed
void GEPFieldChangedCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc)
{
	GEPEpisodeChanged(pDoc, bFinished);
}

void GEPSetNameCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc)
{
	// When the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pEpisode->name);

	// Make sure the browser picks up the new mapdesc name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pEpisode->name);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pEpisode->name);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	GEPSetScopeCB(pField, bFinished, pDoc);
}

void GEPSetScopeCB(MEField *pField, bool bFinished, EpisodeEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges) {
		// Update the filename appropriately
		resFixFilename(g_EpisodeDictionary, pDoc->pEpisode->name, pDoc->pEpisode);
	}

	// Call on to do regular updates
	GEPFieldChangedCB(pField, bFinished, pDoc);
}

void GEPRefreshData(EpisodeEditDoc *pDoc)
{
}

// This is called whenever any episode data changes to do cleanup
void GEPEpisodeChanged(EpisodeEditDoc *pDoc, bool bUndoable)
{
	if (!pDoc->bIgnoreFieldChanges) {
		GEPUpdateDisplay(pDoc);

		if (bUndoable) {
			GEPUndoData *pData = calloc(1, sizeof(GEPUndoData));
			pData->pPreEpisode = pDoc->pNextUndoEpisode;
			pData->pPostEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, GEPEpisodeUndoCB, GEPEpisodeRedoCB, GEPEpisodeUndoFreeCB, pData);
			pDoc->pNextUndoEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);
		}
	}
}

void GEPEpisodeUndoCB(EpisodeEditDoc *pDoc, GEPUndoData *pData)
{
	// Put the undo episode into the editor
	StructDestroy(parse_GenesisEpisode, pDoc->pEpisode);
	pDoc->pEpisode = StructClone(parse_GenesisEpisode, pData->pPreEpisode);
	if (pDoc->pNextUndoEpisode) {
		StructDestroy(parse_GenesisEpisode, pDoc->pNextUndoEpisode);
	}
	pDoc->pNextUndoEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);

	// Update the UI
	GEPEpisodeChanged(pDoc, false);
}

void GEPEpisodeRedoCB(EpisodeEditDoc *pDoc, GEPUndoData *pData)
{
	// Put the undo episode into the editor
	StructDestroy(parse_GenesisEpisode, pDoc->pEpisode);
	pDoc->pEpisode = StructClone(parse_GenesisEpisode, pData->pPostEpisode);
	if (pDoc->pNextUndoEpisode) {
		StructDestroy(parse_GenesisEpisode, pDoc->pNextUndoEpisode);
	}
	pDoc->pNextUndoEpisode = StructClone(parse_GenesisEpisode, pDoc->pEpisode);

	// Update the UI
	GEPEpisodeChanged(pDoc, false);
}

void GEPEpisodeUndoFreeCB(EpisodeEditDoc *pDoc, GEPUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_GenesisEpisode, pData->pPreEpisode);
	StructDestroy(parse_GenesisEpisode, pData->pPostEpisode);
	free(pData);
}

void GEPMakeEpisodes(UIButton *pButton, EpisodeEditDoc* pDoc)
{
	GEPUIMakeEpisodesWindow *ui;
	int y;
	
	if( !pDoc->emDoc.saved ) {
		Alertf( "You must save before making maps." );
		return;
	}
	
	ui = calloc( 1, sizeof( *ui ));
	ui->win = ui_WindowCreate( "Make Episodes", 100, 100, 225, 90 );
	ui->pEpisode = pDoc->pEpisode;

	y = 0;
	
	{
		UILabel* label = ui_LabelCreate( "Number to make:", X_OFFSET_BASE, y );
		ui_WindowAddChild( ui->win, label );
	}
	{
		int count = EditorPrefGetInt( EPISODE_EDITOR, "MakeEpisodes", "MakeCount", 1 );
		char buf[ 25 ];

		sprintf( buf, "%d", count );
		ui->pCountEntry = ui_TextEntryCreate( buf, X_OFFSET_CONTROL, y );
		ui_TextEntrySetIntegerOnly( ui->pCountEntry );
		ui_WidgetSetWidthEx( UI_WIDGET( ui->pCountEntry ), 1, UIUnitPercentage );
		ui_WindowAddChild( ui->win, ui->pCountEntry );
	}
	y += STANDARD_ROW_HEIGHT;

	{
		UILabel* label = ui_LabelCreate( "Starting Number:", X_OFFSET_BASE, y );
		ui_WindowAddChild( ui->win, label );
	}
	{
		ui->pStartNumEntry = ui_TextEntryCreate( "1", X_OFFSET_CONTROL, y );
		ui_TextEntrySetIntegerOnly( ui->pStartNumEntry );
		ui_WidgetSetWidthEx( UI_WIDGET( ui->pStartNumEntry ), 1, UIUnitPercentage );
		ui_WindowAddChild( ui->win, ui->pStartNumEntry );
	}
	y += STANDARD_ROW_HEIGHT;
	
	y += 5;

	elUIAddCancelOkButtons( ui->win, GEPMakeEpisodesWindowDestroy, ui, GEPMakeEpisodesWindowOk, ui );
	ui_WindowSetCloseCallback( ui->win, GEPMakeEpisodesWindowDestroy, ui );

	y += STANDARD_ROW_HEIGHT;

	ui_WidgetSetHeight( UI_WIDGET( ui->win ), y );
	elUICenterWindow( ui->win );
	ui_WindowSetModal( ui->win, true );
	ui_WindowShow( ui->win );
}

void GEPMakeEpisodesWindowOk( UIButton* pButton, GEPUIMakeEpisodesWindow* ui )
{
	int count = atoi( ui_TextEntryGetText( ui->pCountEntry ));
	if( count > 0 ) {
		const char* startDir = EditorPrefGetString( EPISODE_EDITOR, "MakeEpisodes", "StartDir", "maps" );
		const char* startText = EditorPrefGetString( EPISODE_EDITOR, "MakeEpisodes", "StartText", "" );
		UIWindow* browser = ui_FileBrowserCreate( "Make Episodes", "Save", UIBrowseNew, UIBrowseFiles, true,
												  "maps", startDir, startText, NULL, GEPMakeEpisodesBrowserDestroy, ui,
												  GEPMakeEpisodesBrowserOk, ui );
		
		EditorPrefStoreInt( EPISODE_EDITOR, "MakeMaps", "MakeCount", count );
		elUICenterWindow( browser );
		ui_WindowShow( browser );
		ui_WindowHide( ui->win );
	}
}

bool GEPMakeEpisodesWindowDestroy( UIButton* pButton, GEPUIMakeEpisodesWindow* ui )
{
	ui_WindowHide( ui->win );
	ui_WidgetQueueFreeAndNull( &ui->win );
	free( ui );
	return true;
}

void GEPMakeEpisodesBrowserOk( const char* dir, const char* name, GEPUIMakeEpisodesWindow* ui )
{
	static U32 genesis_last_make_episode_time = 0;
	if( wlGetFrameCount() <= genesis_last_make_episode_time+5)
	{
		return;
	}
	else
	{
		int count = atoi( ui_TextEntryGetText( ui->pCountEntry ));
		int start_num = atoi( ui_TextEntryGetText( ui->pStartNumEntry ));
		genesis_last_make_episode_time = wlGetFrameCount();
	
		EditorPrefStoreString( EPISODE_EDITOR, "MakeEpisodes", "StartDir", dir );
		EditorPrefStoreString( EPISODE_EDITOR, "MakeEpisodes", "StartText", name );
		if( count > 0 ) {
			char** episodeNames = NULL;
			char** episodeRoots = NULL;
			bool displayedWarnings = false;


			// Build a list of directories that are going to be
			// generated in so they can all be checked out first.
			{
				int it;
				for( it = 0; it != count; ++it ) {
					char episodeRoot[ MAX_PATH ];
					char episodeName[ 256 ];
				
					sprintf( episodeName, "%s_%03d", name, it + start_num );
					sprintf( episodeRoot, "%s/%s", dir, episodeName );

					eaPush( &episodeNames, strdup( episodeName ));
					eaPush( &episodeRoots, strdup( episodeRoot ));
				}
			}

			if( GEPQueryIfNotEpisodeDirs( episodeRoots, episodeNames )) {
				GimmeErrorValue err;

				err = gimmeDLLDoOperationsDirs( episodeRoots, GIMME_CHECKOUT, 0 );
				if( err != GIMME_NO_ERROR && err != GIMME_ERROR_NOT_IN_DB && err != GIMME_ERROR_NO_DLL ) {
					Alertf( "Error checking out all files (see console for details)." );
					gfxStatusPrintf( "Check out FAILED" );
				} else {
					GenesisRuntimeStatus* genStatus = StructCreate( parse_GenesisRuntimeStatus );
					
					int it;
					for( it = 0; it != count; ++it ) {
						U32 seed = rand();
						GenesisRuntimeStatus* mapStatus;

						{
							GenesisEpisode* episodeFixed = StructClone( parse_GenesisEpisode, ui->pEpisode );
							assert( episodeFixed );
							StructFreeString( episodeFixed->name );
							episodeFixed->name = StructAllocString( episodeNames[ it ]);
							mapStatus = genesisCreateEpisode( PARTITION_CLIENT, episodeFixed, episodeRoots[ it ], seed, seed );
							StructDestroy( parse_GenesisEpisode, episodeFixed );
						}

						eaPushEArray( &genStatus->stages, &mapStatus->stages );
						eaClear( &mapStatus->stages );
						StructDestroy( parse_GenesisRuntimeStatus, mapStatus );
					}
					
					wleGenesisDisplayErrorDialog( genStatus );
					StructDestroy( parse_GenesisRuntimeStatus, genStatus );
				}
			}
			
			eaDestroyEx( &episodeNames, NULL );
			eaDestroyEx( &episodeRoots, NULL );
		}

		GEPMakeEpisodesBrowserDestroy( ui );
	}
}

void GEPMakeEpisodesBrowserDestroy( GEPUIMakeEpisodesWindow* ui )
{
	ui_WidgetQueueFreeAndNull( &ui->win );
	free( ui );
}

UIExpander* GEPCreateExpander(UIExpanderGroup *pGroup, const char* pcName, int index)
{
	UIExpander* accum = ui_ExpanderCreate( pcName, 0 );
	ui_WidgetSkin( UI_WIDGET( accum ), gBoldExpanderSkin );
	ui_ExpanderGroupInsertExpander( pGroup, accum, index );
	ui_ExpanderSetOpened( accum, true );

	return accum;
}

void GEPRefreshLabel(UILabel** ppLabel, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent)
{
	if( !*ppLabel ) {
		*ppLabel = ui_LabelCreate(text, x, y);
		ui_WidgetAddChild(pParent, UI_WIDGET(*ppLabel));
	}
	ui_LabelSetText(*ppLabel, text);
	ui_WidgetSetPosition(UI_WIDGET(*ppLabel), x, y);
	ui_WidgetSetTooltipString(UI_WIDGET(*ppLabel), tooltip);
	ui_LabelEnableTooltips(*ppLabel);
}

void GEPRefreshButton(UIButton** ppButton, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent,
					  UIActivationFunc clickedFn, UserData clickedData )
{
	if( !*ppButton ) {
		*ppButton = ui_ButtonCreate( text, x, y, NULL, NULL );
		ui_WidgetAddChild(pParent, UI_WIDGET(*ppButton));
	}
	ui_ButtonSetText( *ppButton, text );
	ui_WidgetSetPosition( UI_WIDGET(*ppButton), x, y );
	ui_ButtonSetTooltip( *ppButton, tooltip );
	ui_ButtonSetCallback( *ppButton, clickedFn, clickedData );
}

int GEPRefreshAddRemoveButtons(UIButton*** peaButtons, int numButtons, int y,
							   UIWidget* pParent, UIActivationFunc addFunc, UIActivationFunc removeFunc, void* data)
{
	int i;
	
	for( i = eaSize( peaButtons ) - 1; i >= numButtons; --i ) {
		assert( *peaButtons );
		ui_WidgetQueueFreeAndNull( &(*peaButtons)[ i ]);
	}
	eaSetSize( peaButtons, numButtons );

	for( i = 0; i != numButtons; ++i ) {
		UIButton** pButton = &(*peaButtons)[i];

		if( !*pButton ) {
			*pButton = ui_ButtonCreate( "", 0, 0, NULL, NULL );
			ui_WidgetAddChild( pParent, UI_WIDGET(*pButton) );
		}

		ui_WidgetSetPositionEx( UI_WIDGET(*pButton), 5, y, 0, 0, UITopRight );
		ui_WidgetSetWidth( UI_WIDGET(*pButton), 16 );

		if( i == 0 ) {
			ui_ButtonSetText( *pButton, "+" );
			ui_ButtonSetTooltip( *pButton, "Add another page to the prompt." );
			ui_ButtonSetCallback( *pButton, addFunc, data );
		} else {
			ui_ButtonSetText( *pButton, "X" );
			ui_ButtonSetTooltip( *pButton, "Remove this page from the prompt." );
			ui_ButtonSetCallback( *pButton, removeFunc, data );
		}

		y += STANDARD_ROW_HEIGHT;
	}

	return y;
}

void GEPRefreshFieldSimple( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
							int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
							ParseTable* pTable, const char* pchField )
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimple( eType, pOld, pNew, pTable, pchField );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidth( (*ppField)->pUIWidget, 200 );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, GEPFieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, GEPFieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

int GEPRefreshEArrayFieldSimple(
		MEField*** peaField, MEFieldType fieldType, void* pOld, void* pNew,
		int x, int y, F32 w, int pad, UIWidget* pParent, EpisodeEditDoc* pDoc,
		ParseTable* pTable, const char* pcField)
{
	int fieldColumn;

	assert( peaField );
	if( ParserFindColumn( pTable, pcField, &fieldColumn )) {
		void*** pArray = TokenStoreGetEArray(pTable, fieldColumn, pNew, NULL);
		int numElem = eaSize( pArray );
		int i;
	
		for( i = eaSize( peaField ) - 1; i >= numElem; --i ) {
			assert( *peaField );
			MEFieldSafeDestroy( &(*peaField)[i] );
		}
		eaSetSize( peaField, numElem );
			
		for( i = 0; i != numElem; ++i ) {
			MEField** ppField = &(*peaField)[i];
					
			if (!*ppField) {
				*ppField = MEFieldCreate(fieldType, pOld, pNew, pTable, pcField, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
										 NULL, NULL, NULL, NULL, false , NULL, NULL, NULL, NULL, i, 0, 0, 0, NULL);
				GEPAddFieldToParent(*ppField, pParent, x, y, 0, w,
									(w <= 1.0 ? UIUnitPercentage : UIUnitFixed),
									pad, pDoc);
			} else {
				ui_WidgetSetPosition((*ppField)->pUIWidget, x, y);
				MEFieldSetAndRefreshFromData(*ppField, pOld, pNew);
			}
			
			y += STANDARD_ROW_HEIGHT;
		}
	}

	return y;
}


void GEPRefreshFieldSimpleEnum( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
								int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
								ParseTable* pTable, const char* pchField, StaticDefineInt* pEnum )
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimpleEnum( eType, pOld, pNew, pTable, pchField, pEnum );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidth( (*ppField)->pUIWidget, 200 );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, GEPFieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, GEPFieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

void GEPRefreshFieldSimpleGlobalDictionary( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
											int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
											ParseTable* pTable, const char* pchField,
											const char* pchDictName)
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimpleGlobalDictionary( eType, pOld, pNew, pTable, pchField, pchDictName, "ResourceName" );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidth( (*ppField)->pUIWidget, 200 );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, GEPFieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, GEPFieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

void GEPRefreshFieldSimpleDataProvided( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
										int x, int y, UIWidget* pParent, EpisodeEditDoc* pDoc,
										ParseTable* pTable, const char* pchField, const char*** pModel )
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimpleDataProvided( eType, pOld, pNew, pTable, pchField, NULL, pModel, NULL );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidth( (*ppField)->pUIWidget, 200 );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, GEPFieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, GEPFieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

void GEPAddPart(UIButton* pButton, EpisodeEditDoc *pDoc)
{
	GenesisEpisodePart *partAccum;
	
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	partAccum = StructCreate( parse_GenesisEpisodePart );

	eaPush( &pDoc->pEpisode->parts, partAccum );

	// Refresh the UI
	GEPEpisodeChanged(pDoc, true);
}

void GEPRemovePart(UIButton* pButton, GEPEpisodePartGroup *pGroup)
{
	EpisodeEditDoc *pDoc = pGroup->pDoc;

	if( !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	eaRemove( &pDoc->pEpisode->parts, pGroup->index );

	// Refresh the UI
	GEPEpisodeChanged( pDoc, true );
}

void GEPContinuePromptBodyTextAdd(UIButton* pButton, GEPEpisodePartGroup *pGroup)
{
	GenesisEpisodePart *pPart;
	EpisodeEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPart = pDoc->pEpisode->parts[ pGroup->index ];

	// Perform the operation
	eaPush( &pPart->eaContinuePromptBodyText, StructAllocString( "" ));

	// Refresh the UI
	GEPEpisodeChanged( pDoc, true );
}

void GEPContinuePromptBodyTextRemove(UIButton* pButton, GEPEpisodePartGroup *pGroup)
{
	GenesisEpisodePart *pPart;
	EpisodeEditDoc* pDoc;
	int index;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPart = pDoc->pEpisode->parts[ pGroup->index ];

	for (index = eaSize( &pGroup->eaContinuePromptBodyTextAddRemoveButtons ) - 1; index >= 0; --index ) {
		if( pGroup->eaContinuePromptBodyTextAddRemoveButtons[ index ] == pButton ) {
			break;
		}
	}
	if( index == -1 ) {
		return;
	}

	// Perform the operation
	StructFreeString( pPart->eaContinuePromptBodyText[ index ]);
	eaRemove( &pPart->eaContinuePromptBodyText, index );

	// Refresh the UI
	GEPEpisodeChanged( pDoc, true );
}

void GEPFreeEpisodePartGroup(GEPEpisodePartGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pMapDescField);
	MEFieldSafeDestroy(&pGroup->pMissionField);
	MEFieldSafeDestroy(&pGroup->pContinueFromField);
	MEFieldSafeDestroy(&pGroup->pContinueRoomField);
	MEFieldSafeDestroy(&pGroup->pContinueChallengeField);
	MEFieldSafeDestroy(&pGroup->pContinueMissionTextField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptUsePetCostumeField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptCostumeField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptPetCostumeField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptButtonTextField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptCategoryField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptPriorityField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptTitleTextField);
	eaDestroyEx(&pGroup->eaContinuePromptBodyTextField, MEFieldDestroy);

	ui_WidgetQueueFreeAndNull(&pGroup->pExpanderGroup);

	eaDestroyEx( &pGroup->eaMissionNames, NULL );
	eaDestroyEx( &pGroup->eaRoomNames, NULL );
	eaDestroyEx( &pGroup->eaChallengeNames, NULL );
	free(pGroup);
}

void GEPRefreshPartInfrastructure( EpisodeEditDoc *pDoc, GEPEpisodePartGroup *pGroup, int index, GenesisEpisodePart *part )
{
	pGroup->index = index;
	ui_ButtonSetCallback( pGroup->pDelButton, GEPRemovePart, pGroup );

	{
		char buffer[ 256 ];
		if( part->name ) {
			sprintf( buffer, "Part %d: %s", index + 1, part->name );
		} else {
			sprintf( buffer, "Part %d", index + 1 );
		}
		ui_WidgetSetTextString( UI_WIDGET( pGroup->pExpander ), buffer );
	}

	// Ensure the transition overrides are visible only if there is a transition to do.
	if( index < eaSize( &pDoc->pEpisode->parts ) - 1 ) {
		if (!pGroup->pTransitionExpander->group) {
			ui_ExpanderGroupInsertExpander(pGroup->pExpanderGroup, pGroup->pTransitionExpander, 1);
		}
	} else {
		if (pGroup->pTransitionExpander->group) {
			ui_ExpanderGroupRemoveExpander(pGroup->pExpanderGroup, pGroup->pTransitionExpander);
		}
	}

	{
		char** missionNames = NULL;
		char** roomNames = NULL;
		char** challengeNames = NULL;
		GenesisMapDescription* mapDesc = GET_REF(part->map_desc);

		if( mapDesc ) {
			int missionIt;
			for( missionIt = 0; missionIt != eaSize( &mapDesc->missions ); ++missionIt ) {
				GenesisMissionDescription* mission = mapDesc->missions[ missionIt ];
				eaPush( &missionNames, strdup( mission->zoneDesc.pcName ));

				if(   mission->zoneDesc.pcName && part->mission_name
					  && stricmp( mission->zoneDesc.pcName, part->mission_name ) == 0 ) {
					int challengeIt;
					for( challengeIt = 0; challengeIt != eaSize( &mission->eaChallenges ); ++challengeIt ) {
						eaPush( &challengeNames, strdup( mission->eaChallenges[ challengeIt ]->pcName ));
					}
				}
			}
			{
				int challengeIt;
				for( challengeIt = 0; challengeIt != eaSize( &mapDesc->shared_challenges ); ++challengeIt ) {
					eaPush( &challengeNames, strdup( mapDesc->shared_challenges[ challengeIt ]->pcName ));
				}
			}

			genesisMakeRoomNamesList(mapDesc, &roomNames);
		}

		eaQSort( missionNames, GEPStringCompare );
		eaQSort( roomNames, GEPStringCompare );
		eaQSort( challengeNames, GEPStringCompare );
		
		GEPCheckNameList( &pGroup->eaMissionNames, &missionNames );
		GEPCheckNameList( &pGroup->eaRoomNames, &roomNames );
		GEPCheckNameList( &pGroup->eaChallengeNames, &challengeNames );
	}
}

void GEPRefreshPart(EpisodeEditDoc *pDoc, GEPEpisodePartGroup *pGroup, int index, GenesisEpisodePart *origPart, GenesisEpisodePart *part,
					GenesisEpisodePart *origNextPart, GenesisEpisodePart *nextPart)
{
	F32 y = 0;

	// Update infrastructure
	GEPRefreshPartInfrastructure( pDoc, pGroup, index, part );

	// Update the main expander
	y = 0;
	{
		// Update the name
		GEPRefreshLabel(&pGroup->pNameLabel, "Name", "The name of this part.  Not user visible.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimple( &pGroup->pNameField, kMEFieldType_TextEntry, origPart, part,
							   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
							   parse_GenesisEpisodePart, "Name");
		y += STANDARD_ROW_HEIGHT;

		// Update the mapdesc
		GEPRefreshLabel(&pGroup->pMapDescLabel, "MapDesc", "The mapdesc for this part", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimpleGlobalDictionary( &pGroup->pMapDescField, kMEFieldType_TextEntry, origPart, part,
											   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
											   parse_GenesisEpisodePart, "MapDesc",  GENESIS_MAPDESC_DICTIONARY );
		ui_WidgetSetWidth( pGroup->pMapDescField->pUIWidget, 160 );
		GEPRefreshButton( &pGroup->pMapDescEditButton, "Edit", "Edit this map desc", X_OFFSET_CONTROL + 160, y, UI_WIDGET(pGroup->pExpander),
						  GEPPartEditMapDesc, pGroup );
		ui_WidgetSetWidth( UI_WIDGET( pGroup->pMapDescEditButton ), 40 );
		y += STANDARD_ROW_HEIGHT;

		// Update the mission
		GEPRefreshLabel(&pGroup->pMissionLabel, "Mission", "Mission for this part", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimpleDataProvided( &pGroup->pMissionField, kMEFieldType_TextEntry, origPart, part,
										   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
										   parse_GenesisEpisodePart, "MissionName",
										   &pGroup->eaMissionNames );
		MEFieldSetChangeCallback( pGroup->pMissionField, GEPEpisodePartMissionChanged, pGroup );
		y += STANDARD_ROW_HEIGHT;
	
		// Update the continue from field
		GEPRefreshLabel(&pGroup->pContinueFromLabel, "ContinueFrom", "", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimpleEnum( &pGroup->pContinueFromField, kMEFieldType_Combo, origPart, part,
								   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
								   parse_GenesisEpisodePart, "ContinueFrom", GenesisMissionExitFromEnum );
		y += STANDARD_ROW_HEIGHT;

		if (part->eContinueFrom == GenesisMissionExitFrom_DoorInRoom) {
			GEPRefreshLabel(&pGroup->pContinueRoomLabel, "Room", "", X_OFFSET_BASE + X_OFFSET_INDENT, y, UI_WIDGET(pGroup->pExpander));
			GEPRefreshFieldSimpleDataProvided( &pGroup->pContinueRoomField, kMEFieldType_TextEntry, origPart, part,
											   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
											   parse_GenesisEpisodePart, "ContinueRoom",
											   &pGroup->eaRoomNames );
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContinueRoomLabel);
			MEFieldSafeDestroy( &pGroup->pContinueRoomField );
		}

		if (part->eContinueFrom == GenesisMissionExitFrom_Challenge) {
			GEPRefreshLabel(&pGroup->pContinueChallengeLabel, "Challenge", "", X_OFFSET_BASE + X_OFFSET_INDENT, y, UI_WIDGET(pGroup->pExpander));
			GEPRefreshFieldSimpleDataProvided( &pGroup->pContinueChallengeField, kMEFieldType_TextEntry, origPart, part,
											   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
											   parse_GenesisEpisodePart, "ContinueChallenge",
											   &pGroup->eaChallengeNames );
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContinueChallengeLabel);
			MEFieldSafeDestroy( &pGroup->pContinueChallengeField );
		}

		GEPRefreshLabel(&pGroup->pContinueMissionTextLabel, "Return Text", "The mission return text used when this part is completed.", X_OFFSET_BASE + X_OFFSET_INDENT, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimple( &pGroup->pContinueMissionTextField, kMEFieldType_TextEntry, origPart, part,
							   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
							   parse_GenesisEpisodePart, "ContinueMissionText" );
		y += STANDARD_ROW_HEIGHT;

		GEPRefreshLabel(&pGroup->pContinuePromptUsePetCostumeLabel, "Use Pet Costume", "If true, specify which pet to use, otherwise specify the costume to use.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimple( &pGroup->pContinuePromptUsePetCostumeField, kMEFieldType_BooleanCombo, SAFE_MEMBER_ADDR(origPart, continuePromptCostume), &part->continuePromptCostume,
							   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
							   parse_GenesisMissionCostume, "UsePetCostume" );
		y += STANDARD_ROW_HEIGHT;
		
		if (part->continuePromptCostume.eCostumeType == GenesisMissionCostumeType_PetCostume) {
			// update the continue costume field
			GEPRefreshLabel(&pGroup->pContinuePromptPetCostumeLabel, "Prompt Pet Costume", "If a prompt is used to continue, the pet used for the prompt's costume.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
			GEPRefreshFieldSimpleGlobalDictionary( &pGroup->pContinuePromptPetCostumeField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(origPart, continuePromptCostume), &part->continuePromptCostume,
												   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
												   parse_GenesisMissionCostume, "PetCostume", "PetContactList" );
			y += STANDARD_ROW_HEIGHT;

			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptCostumeLabel);
			MEFieldSafeDestroy(&pGroup->pContinuePromptCostumeField);
		} else {
			// update the continue costume field
			GEPRefreshLabel(&pGroup->pContinuePromptCostumeLabel, "Prompt Costume", "If a prompt is used to continue, the prompt's costume.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
			GEPRefreshFieldSimpleGlobalDictionary( &pGroup->pContinuePromptCostumeField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(origPart, continuePromptCostume), &part->continuePromptCostume,
												   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
												   parse_GenesisMissionCostume, "Costume", "PlayerCostume" );
			y += STANDARD_ROW_HEIGHT;

			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptPetCostumeLabel);
			MEFieldSafeDestroy(&pGroup->pContinuePromptPetCostumeField);
		}

		// update the continue button text field
		GEPRefreshLabel(&pGroup->pContinuePromptButtonTextLabel, "Prompt Button Text", "If a prompt is used to continue, the prompt button's text.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimple( &pGroup->pContinuePromptButtonTextField, kMEFieldType_TextEntry, origPart, part,
							   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
							   parse_GenesisEpisodePart, "ContinuePromptButtonText" );
		y += STANDARD_ROW_HEIGHT;

		// update the continue category
		GEPRefreshLabel(&pGroup->pContinuePromptCategoryLabel, "Prompt Category", "If a prompt is used to continue, the prompt's category.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimpleDataProvided( &pGroup->pContinuePromptCategoryField, kMEFieldType_Combo, origPart, part,
										   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
										   parse_GenesisEpisodePart, "ContinuePromptCategoryName", &geaOptionalActionCategories );
		y += STANDARD_ROW_HEIGHT;

		// update the continue priority
		GEPRefreshLabel(&pGroup->pContinuePromptPriorityLabel, "Prompt Priority", "If a prompt is used to continue, the prompt's priority.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimpleEnum( &pGroup->pContinuePromptPriorityField, kMEFieldType_Combo, origPart, part,
								   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
								   parse_GenesisEpisodePart, "ContinuePromptPriority", WorldOptionalActionPriorityEnum );
		y += STANDARD_ROW_HEIGHT;

		// update the continue title text field
		GEPRefreshLabel(&pGroup->pContinuePromptTitleTextLabel, "Prompt Title", "If a prompt is used to continue, the prompt's title text.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimple( &pGroup->pContinuePromptTitleTextField, kMEFieldType_TextEntry, origPart, part,
							   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
							   parse_GenesisEpisodePart, "ContinuePromptTitleText" );
		y += STANDARD_ROW_HEIGHT;

		// update the continue body text field
		GEPRefreshLabel(&pGroup->pContinuePromptBodyTextLabel, "Prompt Body", "If a prompt is used to continue, the prompt's body text.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
		if( eaSize( &part->eaContinuePromptBodyText ) == 0 ) {
			eaPush( &part->eaContinuePromptBodyText, NULL );
		}
		GEPRefreshAddRemoveButtons(&pGroup->eaContinuePromptBodyTextAddRemoveButtons, eaSize(&part->eaContinuePromptBodyText), y,
								   UI_WIDGET(pGroup->pExpander), GEPContinuePromptBodyTextAdd, GEPContinuePromptBodyTextRemove, pGroup );
		y = GEPRefreshEArrayFieldSimple(&pGroup->eaContinuePromptBodyTextField, kMEFieldType_SMFTextEntry, origPart, part,
										X_OFFSET_CONTROL, y, 1.0, 5 + 16, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
										parse_GenesisEpisodePart, "ContinuePromptBodyText");
		
		ui_ExpanderSetHeight( pGroup->pExpander, y );
	}

	// Update the transition expander
	y = 0;
	{
		GEPRefreshLabel( &pGroup->pContinueTransitionOverrideLabel, "Continue Override", "Specifies an override to the default region rules Transition.  If left blank, the default Transition as determined by the region rules will be seen.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pTransitionExpander));
		GEPRefreshFieldSimpleGlobalDictionary( &pGroup->pContinueTransitionOverrideField, kMEFieldType_TextEntry, origPart, part,
											   X_OFFSET_CONTROL, y, UI_WIDGET( pGroup->pTransitionExpander ), pGroup->pDoc,
											   parse_GenesisEpisodePart, "ContinueTransitionOverride", "DoorTransitionSequenceDef" );
		y += STANDARD_ROW_HEIGHT;

		if (nextPart) {
			GEPRefreshLabel( &pGroup->pNextStartTransitionOverrideLabel, "Next Start Override", "Specifies an override to the default region rules Transition.  If left blank, the default Transition as determined by the region rules will be seen.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pTransitionExpander));
			GEPRefreshFieldSimpleGlobalDictionary( &pGroup->pNextStartTransitionOverrideField, kMEFieldType_TextEntry, origNextPart, nextPart,
												   X_OFFSET_CONTROL, y, UI_WIDGET( pGroup->pTransitionExpander ), pGroup->pDoc,
												   parse_GenesisEpisodePart, "ContinueTransitionOverride", "DoorTransitionSequenceDef" );
			y += STANDARD_ROW_HEIGHT;
		}
		
		ui_ExpanderSetHeight( pGroup->pTransitionExpander, y );
	}
}

void GEPEpisodePartMissionChanged(MEField *pField, bool finished, GEPEpisodePartGroup *pGroup)
{
	EpisodeEditDoc* pDoc = pGroup->pDoc;
	GenesisEpisodePart* part = pDoc->pEpisode->parts[ pGroup->index ];
	GenesisMapDescription* mapDesc = GET_REF( part->map_desc );
	int missionIndex = genesisFindMission( mapDesc, part->mission_name );

	if( pDoc->bIgnoreFieldChanges ) {
		return;
	}

	if( finished && mapDesc && missionIndex >= 0 ) {
		// update default continue fields
		GenesisMissionStartDescription* startDesc = &mapDesc->missions[ missionIndex ]->zoneDesc.startDescription;
		
		part->eContinueFrom = startDesc->eContinueFrom;
		free( part->pcContinueRoom );
		part->pcContinueRoom = strdup( startDesc->pcContinueRoom );
		free( part->pcContinueChallenge );
		part->pcContinueChallenge = strdup( startDesc->pcContinueChallenge );
		StructCopyAll( parse_GenesisMissionCostume, &startDesc->continuePromptCostume, &part->continuePromptCostume );
	}

	GEPFieldChangedCB( pField, finished, pDoc );
}

void GEPPartEditMapDesc(UIButton* button, GEPEpisodePartGroup* pGroup)
{
	EpisodeEditDoc *pDoc = pGroup->pDoc;
	const char* mapDescName = REF_STRING_FROM_HANDLE( pDoc->pEpisode->parts[ pGroup->index ]->map_desc );

	if( mapDescName ) {
		emOpenFileEx( mapDescName, "MapDescription" );
	}
}

void GEPFreeMissionInfoGroup(GEPMissionInfoGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pDisplayNameField);
	MEFieldSafeDestroy(&pGroup->pShortTextField);
	MEFieldSafeDestroy(&pGroup->pDescriptionTextField);
	MEFieldSafeDestroy(&pGroup->pSummaryTextField);
	MEFieldSafeDestroy(&pGroup->pCategoryField);
	MEFieldSafeDestroy(&pGroup->pRequirementsField);
	MEFieldSafeDestroy(&pGroup->pRewardField);
	MEFieldSafeDestroy(&pGroup->pRewardScaleField);
	GEFreeMissionLevelDefGroupSafe( &pGroup->pLevelDefGroup );

	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);
	free(pGroup);
}

void GEPRefreshMissionInfo(EpisodeEditDoc *pDoc, GEPMissionInfoGroup *pGroup, GenesisEpisode *orig, GenesisEpisode *new)
{
	F32 y = 0;

	// Update infrastructure
	;
	
	// Update the display name
	GEPRefreshLabel(&pGroup->pDisplayNameLabel, "Display Name", "The display name of the episode mission for the Journal and UI.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimple( &pGroup->pDisplayNameField, kMEFieldType_TextEntry, orig, new,
						   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
						   parse_GenesisEpisode, "DisplayName" );
	y += STANDARD_ROW_HEIGHT;

	// Update the short text
	GEPRefreshLabel(&pGroup->pShortTextLabel, "UI String", "The text for the episode mission to show on the HUD.  If not set, then only the mission name will be displayed.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimple( &pGroup->pShortTextField, kMEFieldType_TextEntry, orig, new,
						   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
						   parse_GenesisEpisode, "UIString" );
	y += STANDARD_ROW_HEIGHT;

	// Update description
	GEPRefreshLabel(&pGroup->pDescriptionTextLabel, "Journal Description", "The long text for the episode mission that displays in the mission journal.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimple( &pGroup->pDescriptionTextField, kMEFieldType_SMFTextEntry, orig, new,
						   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
						   parse_GenesisEpisode, "DescriptionText" );
	y += STANDARD_ROW_HEIGHT;
	
	// Update summary
	GEPRefreshLabel(&pGroup->pSummaryTextLabel, "Journal Summary", "The summary for the episode mission that displays in the mission journal.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimple( &pGroup->pSummaryTextField, kMEFieldType_TextEntry, orig, new,
						   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
						   parse_GenesisEpisode, "Summary" );
	y += STANDARD_ROW_HEIGHT;
	
	// Update category
	GEPRefreshLabel(&pGroup->pCategoryLabel, "Journal Category", "The mission journal category for the episode mission.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimpleGlobalDictionary( &pGroup->pCategoryField, kMEFieldType_TextEntry, orig, new,
										   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
										   parse_GenesisEpisode, "Category", "MissionCategory" );
	y += STANDARD_ROW_HEIGHT;

	if (!pGroup->pLevelDefGroup) {
		pGroup->pLevelDefGroup = calloc( 1, sizeof( *pGroup->pLevelDefGroup ));
	}
	y = GEUpdateMissionLevelDefGroup( pGroup->pLevelDefGroup, pGroup->pExpander, &new->levelDef, SAFE_MEMBER_ADDR(orig, levelDef),
									  X_OFFSET_BASE, X_OFFSET_CONTROL, y,
									  GEPFieldChangedCB, GEPFieldPreChangeCB, pGroup->pDoc );

	// Update the requirements expr
	GEPRefreshLabel(&pGroup->pRequirementsLabel, "Requires", "What must be true before the episode mission can be given to the player.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimple( &pGroup->pRequirementsField, kMEFieldTypeEx_Expression, orig, new,
						   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
						   parse_GenesisEpisode, "RequiresBlock");
	y += STANDARD_ROW_HEIGHT;

	// Update rewards
	GEPRefreshLabel(&pGroup->pRewardLabel, "Reward", "Reward given when the mission is completed.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimpleGlobalDictionary( &pGroup->pRewardField, kMEFieldType_TextEntry, orig, new,
										   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
										   parse_GenesisEpisode, "Reward", "RewardTable" );
	y += STANDARD_ROW_HEIGHT;

	// Update the reward scale
	GEPRefreshLabel(&pGroup->pRewardScaleLabel, "Reward Scale", "The multiplier on rewards for the episode mission.  1.0 is normal", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimple( &pGroup->pRewardScaleField, kMEFieldType_TextEntry, orig, new,
						   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
						   parse_GenesisEpisode, "RewardScale" );
	ui_WidgetSetWidth( pGroup->pRewardScaleField->pUIWidget, 60 );
	y += STANDARD_ROW_HEIGHT;

	ui_ExpanderSetHeight( pGroup->pExpander, y );
}

void GEPFreeMissionStartGroup(GEPMissionStartGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNeedsReturnField);
	MEFieldSafeDestroy(&pGroup->pReturnTextField);

	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);
	free(pGroup);
}

void GEPRefreshMissionStart(EpisodeEditDoc *pDoc, GEPMissionStartGroup *pGroup, GenesisEpisodeGrantDescription *orig, GenesisEpisodeGrantDescription *new)
{
	F32 y = 0;

	// Update infrastructure
	;
	
	
	// Update needs return
	GEPRefreshLabel(&pGroup->pNeedsReturnLabel, "Needs Return", "If the episode mission needs to be returned to be completed.", X_OFFSET_BASE, y, UI_WIDGET(pGroup->pExpander));
	GEPRefreshFieldSimple( &pGroup->pNeedsReturnField, kMEFieldType_BooleanCombo, orig, new,
						   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
						   parse_GenesisEpisodeGrantDescription, "NeedsReturn" );
	y += STANDARD_ROW_HEIGHT;
	
	// Update return text
	if( new->bNeedsReturn ) {
		GEPRefreshLabel(&pGroup->pReturnTextLabel, "Return Text", "The mission return text for the episode.", X_OFFSET_BASE + X_OFFSET_INDENT, y, UI_WIDGET(pGroup->pExpander));
		GEPRefreshFieldSimple( &pGroup->pReturnTextField, kMEFieldType_TextEntry, orig, new,
							   X_OFFSET_CONTROL, y, UI_WIDGET(pGroup->pExpander), pGroup->pDoc,
							   parse_GenesisEpisodeGrantDescription, "ReturnText" );
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pReturnTextLabel );
		MEFieldSafeDestroy( &pGroup->pReturnTextField );
	}

	ui_ExpanderSetHeight( pGroup->pExpander, y );
}

/// Pops up a modal dialog if any of DIRS do not appear to be episode
/// dirs.
bool GEPQueryIfNotEpisodeDirs( char** dirs, char** epNames )
{
	char** invalidEpisodeDirs = NULL;

	{
		int it;
		for( it = 0; it != eaSize( &dirs ); ++it ) {
			if( dirExists( dirs[ it ])) {
				char epMission[ MAX_PATH ];
				char epBackup[ MAX_PATH ];

				sprintf( epMission, "%s/%s.mission", dirs[ it ], epNames[ it ]);
				sprintf( epBackup, "%s/backup_%s.episode", dirs[ it ], epNames[ it ]);

				if( !fileExists( epMission ) || !fileExists( epBackup )) {
					eaPush( &invalidEpisodeDirs, dirs[ it ]);
				}
			}
		}
	}

	if( invalidEpisodeDirs ) {
		char* errStr = NULL;
		estrConcatStatic( &errStr, "Are you sure you want to overwrite the following directories?  They have non-episodes in them.\n" );
		{
			int it;
			for( it = 0; it != eaSize( &invalidEpisodeDirs ); ++it ) {
				estrConcatf( &errStr, "\n%s", invalidEpisodeDirs[ it ]);
			}
		}

		{
			UIDialogButtons result = ui_ModalDialog( "Are you sure?", errStr, ColorBlack, UIYes | UINo );
			estrDestroy( &errStr );
			eaDestroy( &invalidEpisodeDirs );
			return result == UIYes;
		}
	} else {
		return true;
	}
}

#endif
