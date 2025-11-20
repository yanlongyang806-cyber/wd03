//
// CostumeEditorCmd.c
//

#ifndef NO_EDITORS

#include "CostumeEditor.h"


//---------------------------------------------------------------------------------------------------
// Keybinds & Commands
//---------------------------------------------------------------------------------------------------

#endif

AUTO_COMMAND ACMD_NAME("CostumeEditor.SelectBone");
void CmdSelectBone(void) 
{
#ifndef NO_EDITORS
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	if (pDoc && pDoc->pGraphics) {
		PCBoneDef *pBone = costumeView_GetSelectedBone(pDoc->pGraphics, pDoc->pCostume);
		if (pBone) {
			costumeEdit_SelectBone(pDoc, pBone);
		}
	}
#endif
}

AUTO_COMMAND ACMD_NAME("COE_CloneCostume");
void CmdCloneCostume(void) 
{
#ifndef NO_EDITORS
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();

	costumeEdit_UICostumeClone(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("COE_RevertCostume");
void CmdRevertCostume(void) 
{
#ifndef NO_EDITORS
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();

	costumeEdit_CostumeRevert(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("CostumeEditor.CenterCamera");
void CmdCostumeCenterCamera(void) 
{
#ifndef NO_EDITORS
	EMEditor *editor = costumeEditorEMGetEditor();
	costumeEdit_UICenterCamera(NULL, editor);
#endif
}