//
// PowerTreeEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "estring.h"
#include "file.h"
#include "powers.h"
#include "PowerTreeEditor.h"
#include "StringCache.h"


#include "AutoGen/PowerTree_h_ast.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMEditor gPowerTreeEditor;

static EMPicker gPowerTreePicker;

static PowerTreeDef **s_ppNewPowerTrees = NULL;

static bool gInitializedEditor = false;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static PowerTreeDef *PTDefFind(char *pchName)
{
	PowerTreeDef *pdef = powertreedef_Find(pchName);
	if(!pdef)
	{
		int i;
		for(i=eaSize(&s_ppNewPowerTrees)-1; i>=0; i--)
		{
			if(!stricmp(s_ppNewPowerTrees[i]->pchName,pchName))
			{
				pdef = s_ppNewPowerTrees[i];
				break;
			}
		}
	}
	return pdef;
}

bool powerTreeEditorOnDocumentTypeSelected(UIDialog *pDialog, UIDialogButton eButton, PTEditDoc *pDoc)
{
	bool bIsTalentTree = eButton != kUIDialogButton_Ok;

	PowerTreeDef *pPowerTreeDef;

	devassert(pDoc);
	if (pDoc == NULL)
		return false;
	
	pPowerTreeDef = StructCreate(parse_PowerTreeDef);
	StructInit(parse_PowerTreeDef, pPowerTreeDef);
	pPowerTreeDef->bIsTalentTree = bIsTalentTree;
	if (bIsTalentTree)
	{
		pPowerTreeDef->pchName = StructAllocString("_New_Talent_Tree");
		pPowerTreeDef->pchFile = (char*)allocAddString("defs/powertrees/_NewTalentTree.powertree");
	}
	else
	{
		pPowerTreeDef->pchName = StructAllocString("_New Power Tree");
		pPowerTreeDef->pchFile = (char*)allocAddString("defs/powertrees/_NewPowerTree.powertree");
	}
	
	eaPush(&s_ppNewPowerTrees, pPowerTreeDef);

	// Set the original def for the doc
	pDoc->pDefOrig = pPowerTreeDef;

	langMakeEditorCopy(parse_PowerTreeDef, pDoc->pDefOrig, true);

	pDoc->pDefNew = StructClone(parse_PowerTreeDef, pDoc->pDefOrig);	

	if(bIsTalentTree) // Talent tree
	{
		PTSetupTalentTreeUI(pDoc);
	}
	else // Power tree
	{
		PTSetupPowerTreeUI(pDoc);		
	}

	strcpy(pDoc->emDoc.doc_display_name, pPowerTreeDef->pchName);

	return true;
}

// Asks the user what kind of power tree document they want to create
static void powerTreeEditorAskForPowerTreeType(PTEditDoc *pDoc)
{
	ui_WindowShow(UI_WINDOW(ui_DialogCreateEx("Choose tree type", "What kind of tree do you want to create?", powerTreeEditorOnDocumentTypeSelected, pDoc, NULL, 
		"Power Tree", kUIDialogButton_Ok, "Talent Tree", kUIDialogButton_Cancel, NULL)));
}

static EMEditorDoc *powerTreeEditorEMLoadDoc(const char *name, const char *type)
{
	PTEditDoc *pdoc;
	PowerTreeDef *pdef;
	char achPTName[MAX_POWER_NAME_LEN] = {0};
	char achFileName[MAX_PATH] = {0};

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	if (!gInitializedEditor)
	{
		gInitializedEditor = true;

		emAddDictionaryStateChangeHandler(&gPowerTreeEditor, "powertreeDef", NULL, NULL, NULL, NULL, NULL);
	}

	pdoc = calloc(sizeof(*pdoc),1);
	pdoc->bPermaDirty = false;
	pdoc->bTalentTreeGridCreated = false;

	if (name) strcpy(achPTName,name);
	
	if(!achPTName[0]) //Create new
	{
		// Ask what type of power tree they want to create
		powerTreeEditorAskForPowerTreeType(pdoc);
		return &pdoc->emDoc;
	}
	else if (!resIsEditingVersionAvailable(g_hPowerTreeDefDict, achPTName))
	{
		// Requested power tree is not loaded on the client, so request it from the server
		resSetDictionaryEditMode(g_hPowerTreeDefDict, true);
		resSetDictionaryEditMode(gMessageDict, true);
		emSetResourceState(&gPowerTreeEditor, achPTName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_hPowerTreeDefDict, achPTName);
		return NULL;
	}

	pdef = PTDefFind(achPTName);
	if(!pdef)
	{
		pdef = StructCreate(parse_PowerTreeDef);
		StructInit(parse_PowerTreeDef,pdef);
		pdef->pchName = StructAllocString(achPTName);
		//pdef->pchDisplayName = StructAllocString(achPTName);
		sprintf(achFileName,"defs/powertrees/_NewPowerTree.powertree");
		pdef->pchFile = (char*)allocAddString(achFileName);
		eaPush(&s_ppNewPowerTrees,pdef);

		pdoc->pDefOrig = pdef;
	}else{
		emDocAssocFile(&pdoc->emDoc, pdef->pchFile);
		pdoc->pDefOrig = StructClone(parse_PowerTreeDef,pdef);
	}
	
	langMakeEditorCopy(parse_PowerTreeDef,pdoc->pDefOrig,true);

	pdoc->pDefNew = StructClone(parse_PowerTreeDef, pdoc->pDefOrig);

	strcpy(pdoc->emDoc.doc_display_name,achPTName);

	if (pdoc->pDefOrig->bIsTalentTree)
	{
		PTSetupTalentTreeUI(pdoc);
	}
	else
	{
		PTSetupPowerTreeUI(pdoc);
	}


	//if(IS_HANDLE_ACTIVE(pdoc->hDefCheckPoint))
	//	REMOVE_HANDLE(pdoc->hDefCheckPoint);

	//SET_HANDLE_FROM_STRING("PowerTreeDef",pdoc->pDefOrig ? pdoc->pDefOrig->pchName : pdoc->pchOrigName,pdoc->hDefCheckPoint);

	return &pdoc->emDoc;
}


static EMEditorDoc *powerTreeEditorEMNewDoc(const char *pcType, void *data)
{
	return powerTreeEditorEMLoadDoc(NULL, pcType);
}


static void powerTreeEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	PTEditDoc *pTreeDoc = (PTEditDoc *)pDoc;
	if(pTreeDoc->pDefOrig)
	{
		StructDestroy(parse_PowerTreeDef, pTreeDoc->pDefOrig);
	}
	if(pTreeDoc->pDefNew)
	{
		eaFindAndRemoveFast(&s_ppNewPowerTrees, pTreeDoc->pDefNew);
		StructDestroy(parse_PowerTreeDef, pTreeDoc->pDefNew);
	}
//	if(IS_HANDLE_ACTIVE(pTreeDoc->hDefCheckPoint))
//		REMOVE_HANDLE(pTreeDoc->hDefCheckPoint);

	SAFE_FREE(pDoc);
}

static void powerTreeEditorEMReloadDoc(EMEditorDoc *pDoc)
{
	PTEditDoc *pTreeDoc = (PTEditDoc *)pDoc;
	if(!pTreeDoc->bDirty)
	{
		char *pchName = strdup(((PTEditDoc*)pDoc)->pchNewName);

		emForceCloseDoc(pDoc);
		emOpenFileEx(pchName,"PowerTreeDef");
	
		free(pchName);
	}
	else
	{
		emQueueFunctionCallEx(powerTreeEditorEMReloadDoc,pDoc,15);
	}
}


static EMTaskStatus powerTreeEditorEMSave(EMEditorDoc *pDoc)
{
	return PTEditorSaveDoc(pDoc);
}


static EMTaskStatus powerTreeEditorEMSaveAs(EMEditorDoc *pDoc)
{
	// TODO
	return EM_TASK_FAILED;
}


static void powerTreeEditorEMDraw(EMEditorDoc *pDoc)
{
	PTEditDoc *pTreeDoc = (PTEditDoc *)pDoc;

 	//if(IS_HANDLE_ACTIVE(pTreeDoc->hDefCheckPoint) && GET_REF(pTreeDoc->hDefCheckPoint) != NULL && GET_REF(pTreeDoc->hDefCheckPoint) != pTreeDoc->pDefOrig)
 	//{
 	//	REMOVE_HANDLE(pTreeDoc->hDefCheckPoint);
 	//	emQueueFunctionCallEx(powerTreeEditorEMReloadDoc,pDoc,1);
 	//}

	PTDraw(pDoc);
}

static void powerTreeEditorEMCopy(EMEditorDoc *pAEdoc, bool bCut)
{
	PTEditDoc* pdoc = (PTEditDoc *)pAEdoc;
	PTGroupCopy(pdoc->pFocusedGrp,bCut,true,false);
}

static void powerTreeEditorEMPaste(EMEditorDoc *pAEdoc, ParseTable *pti, const char *custom_type, PTClipboard *structptr)
{
	PTEditDoc* pdoc = (PTEditDoc *)pAEdoc;
	if(structptr && pdoc->pFocusedGrp)
	{
		PTGroupPaste(pdoc,pdoc->pFocusedGrp,structptr);
	}
}

void powerTreeDictEventCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_REMOVED || eType == RESEVENT_RESOURCE_MODIFIED) {
		Alertf(		"Warning: Power Tree \"%s\" has either changed on disk or you did something in an editor that "
					"caused a new version to be sent, such as opening or saving a Power Tree.  If however "
					"you just got latest, then this can mean you have lost data.  Yes, we know this error is vague, "
					"and this error will show up on pretty much any simple activity you do, but we're erring on the side"
					" of alerting you in all cases instead of accidentally missing an important one.  This error will "
					"continue being overly annoying until the Power Tree Editor gets rewritten.", pResourceName);
	}
}

static void powerTreeEditorEMInit(EMEditor *pEditor)
{
	emMenuItemCreate(pEditor, "pe_levelviewtoggle", "Toggle Level View", NULL, NULL, "PT_LevelViewToggle");
	emMenuItemCreate(pEditor, "pe_levelviewranks", "View Ranks",NULL,NULL,"PT_ExplodeRanks");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "Edit", "me_editfield", "pe_levelviewtoggle", "pe_levelviewranks", NULL));
	resDictRegisterEventCallback("PowerTreeDef", powerTreeDictEventCB, NULL);
}

static void powerTreeEditorEMEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_hPowerTreeDefDict, true);
	resSetDictionaryEditMode(gMessageDict, true);
}

static void powerTreeEditorEMExit(EMEditor *pEditor)
{
}

#endif

AUTO_RUN_LATE;
int powerTreeEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gPowerTreeEditor.editor_name, "Power/Talent Tree Editor");
	gPowerTreeEditor.type = EM_TYPE_SINGLEDOC;
	gPowerTreeEditor.hide_world = 1;
	gPowerTreeEditor.allow_save = 1;
	gPowerTreeEditor.allow_multiple_docs = 1;
	gPowerTreeEditor.disable_auto_checkout = 1;
	gPowerTreeEditor.do_not_reload = 0;
	gPowerTreeEditor.reload_func = powerTreeEditorEMReloadDoc;
	gPowerTreeEditor.default_type = "PowerTreeDef";
	strcpy(gPowerTreeEditor.default_workspace, "Game Design Editors");

	gPowerTreeEditor.init_func = powerTreeEditorEMInit;
	gPowerTreeEditor.enter_editor_func = powerTreeEditorEMEnter;
	gPowerTreeEditor.exit_func = powerTreeEditorEMExit;
	gPowerTreeEditor.new_func = powerTreeEditorEMNewDoc;
	gPowerTreeEditor.load_func = powerTreeEditorEMLoadDoc;
	gPowerTreeEditor.close_func = powerTreeEditorEMCloseDoc;
	gPowerTreeEditor.save_func = powerTreeEditorEMSave;
	gPowerTreeEditor.save_as_func = powerTreeEditorEMSaveAs;
	gPowerTreeEditor.draw_func = powerTreeEditorEMDraw;
	gPowerTreeEditor.copy_func = powerTreeEditorEMCopy;
	gPowerTreeEditor.paste_func = powerTreeEditorEMPaste;

	// Register the picker
	gPowerTreePicker.allow_outsource = 1;
	strcpy(gPowerTreePicker.picker_name, "Power/Talent Tree Library");
	strcpy(gPowerTreePicker.default_type, gPowerTreeEditor.default_type);
	emPickerManage(&gPowerTreePicker);
	eaPush(&gPowerTreeEditor.pickers, &gPowerTreePicker);

	emRegisterEditor(&gPowerTreeEditor);
	emRegisterFileType(gPowerTreeEditor.default_type, "Power/Talent Tree", gPowerTreeEditor.editor_name);
#endif

	return 0;
}

AUTO_COMMAND;
void PT_ExplodeRanks(void)
{
#ifndef NO_EDITORS
	PTEditDoc *pdoc = (PTEditDoc*)emGetActiveEditorDoc();

	if(pdoc)
	{
		pdoc->bExplodeRanks = pdoc->bLevelView && !pdoc->bExplodeRanks;

		PTResizeGroupWin_All(pdoc);
	}
#endif
}

AUTO_COMMAND;
void PT_LevelViewToggle(void)
{
#ifndef NO_EDITORS
	PTEditDoc *pdoc = (PTEditDoc*)emGetActiveEditorDoc();

	if(pdoc)
	{
		pdoc->bLevelView = !pdoc->bLevelView;

		PT_ExplodeRanks();

		PTResizeGroupWin_All(pdoc);
	}
#endif
}
AUTO_COMMAND;
void PT_LevelReset(void)
{
#ifndef NO_EDITORS
	PTEditDoc *pdoc = (PTEditDoc*)emGetActiveEditorDoc();

	if(pdoc)
	{
		int g,n,r;

		for(g=0;g<eaSize(&pdoc->ppGroupWindows);g++)
		{
			for(n=0;n<eaSize(&pdoc->ppGroupWindows[g]->ppNodeEdits);n++)
			{
				if(IS_HANDLE_ACTIVE(pdoc->ppGroupWindows[g]->ppNodeEdits[n]->pDefNodeNew->hNodeRequire))
					continue;

				for(r=0;r<eaSize(&pdoc->ppGroupWindows[g]->ppNodeEdits[n]->ppRankEdits);r++)
				{
					PTRankEdit *pRank = pdoc->ppGroupWindows[g]->ppNodeEdits[n]->ppRankEdits[r];
					
					if(r>0)
					{
						pRank->pDefRankNew->pRequires->iTableLevel = 0;
					}
				}
			}
		}

		for(r=0;r<eaSize(&pdoc->ppFields);r++)
		{
			if(pdoc->ppFields[r]->pUIText && pdoc->ppFields[r]->ptable == parse_PTPurchaseRequirements)
			{
				if(TOK_GET_TYPE(pdoc->ppFields[r]->type) == TOK_INT_X)
				{
					int *piTarget = (int*)((char*)(pdoc->ppFields[r]->pNew) + pdoc->ppFields[r]->offset);
					char *pchI;

					estrStackCreate(&pchI);
					estrPrintf(&pchI,"%d",*piTarget);
					ui_TextEntrySetText(pdoc->ppFields[r]->pUIText,pchI);
					estrDestroy(&pchI);
				}
			}
		}
	}
#endif
}

