/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "CharacterAttribs.h"
#include "CombatSensitivity.h"
#include "Color.h"
#include "estring.h"
#include "StringCache.h"
#include "Expression.h"
#include "GfxTexAtlas.h"
#include "InputMouse.h"
#include "Powers.h"
#include "PowerTreeEditor.h"
#include "PowerReplace.h"
#include "StringUtil.h"
#include "timing.h"
#include "inventoryCommon.h"
#include "PowerVars.h"
#include "qsortG.h"
#include "GfxPrimitive.h"
#include "GfxClipper.h"
#include "gimmeDLLWrapper.h"

#include "AutoGen/PowersEnums_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/PowerReplace_h_ast.h"
#include "AutoGen/PowerTreeEditor_h_ast.h"
#include "AutoGen/PowerVars_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define UI_DND_NODE "nodeDef"
#define UI_DND_GROUP "groupDef"
#define UI_DND_TALENT_TREE_NODE "talentTreeNodeDrag"

#define TALENT_TREE_BUTTON_WIDTH 60
#define TALENT_TREE_BUTTON_HEIGHT 60
#define TALENT_TREE_GRID_HSPACING 20
#define TALENT_TREE_GRID_VSPACING 20
#define TALENT_TREE_BUTTON_START_X_POS 50
#define TALENT_TREE_BUTTON_START_Y_POS 0
#define TALENT_TREE_ARROW_DISTANCE 10
#define TALENT_TREE_NODE_LINE_Z 1000.f

static const char *s_pTalentTreeValidationErrorString = "Talent tree validation error";

static UIMenu *s_pNodeContextMenu = NULL;
static bool s_bRedrawGroups = false;

// Talent tree node data (used for buttons)
typedef struct TalentTreeNodeData
{
	PTEditDoc *pDoc;

	PTNodeEdit *pNodeEdit;

	// The row index
	S8 iRow;

	// The column index
	S8 iCol;

	// The screen coordinate for the center of the button
	F32 fScreenCenterX;
	F32 fScreenCenterY;
} TalentTreeNodeData;

// Data for the row edit button
typedef struct TalentTreeRowEditButtonData
{
	PTEditDoc *pDoc;

	// Indicates if the button is trying to add or remove a row
	bool bAdd;

	// The row index
	S8 iRow;
} TalentTreeRowEditButtonData;

static PTEditDoc **s_ppDocs = NULL;
static PTNodeEnhHelper **s_ppEnhHelpers = NULL;

static ResourceInfo **s_ppEnhancements = NULL;

static void PTDrawGroupConnections(PTGroupEdit **ppGroups);
static void PTDeleteRank(PTRankEdit *pRank);
static void PTCreateRankExp(PTNodeRankDef *pNew, PTNodeRankDef *pOld, PTNodeEdit *pNode);
static void PTCreateEnhancementExp(PTNodeEnhancementDef *pNew, PTNodeEnhancementDef *pOrig, PTNodeEdit *pNode);
static void PTDeleteEnhancement(PTEnhancementEdit *pEnhancement);
static bool PTTalentTreeDrawArrowBetweenNodes(PTEditDoc *pDoc, S8 iRowFrom, S8 iColFrom, S8 iRowTo, S8 iColTo);
static const char * PTGetTalentTreeButtonName(S8 iRow, S8 iCol);
// Creates a talent tree button
static UIButton * PTCreateTalentTreeButton(const char *pchLabel, F32 fX, F32 fY, F32 fWidth, F32 fHeight,
										   UIActivationFunc pClickCallback, TalentTreeNodeData *pTalentTreeData);
// Returns the icon name for the given power tree node def
static const char * PTTalentTreeGetNodeIconName(PTNodeDef *pNodeDef);
// Returns the name for the given power tree node def
static const char * PTTalentTreeGetNodeName(PTNodeDef *pNodeDef);
// Updates the icon for the talent tree button
void PTTalentTreeButtonUpdateIcon(PTEditDoc *pDoc, PTNodeDef *pNode, const char * pchIconName);


static bool PTCanEdit(PTEditDoc *doc)
{
	ResourceInfo *info;
	
	// Can always edit if this is a new doc
	if (!doc->pDefOrig)
	{
		return true;
	}

	// Cannot edit if not writable
	info = resGetInfo(g_hPowerTreeDefDict, doc->pDefOrig->pchName);
	if (info && !resIsWritable(info->resourceDict, info->resourceName))
	{
		if(!gimmeDLLQueryIsFileLatest(info->resourceLocation)) {
			Alertf("File is not latest and the power tree editor does not support refreshing.  You must close down the client and and get latest before editing this file. (%s)", info->resourceLocation);
		} else {
			emQueuePrompt(EMPROMPT_CHECKOUT, &doc->emDoc, NULL, g_hPowerTreeDefDict, doc->pDefOrig->pchName);
		}
		return false;
	}

	return true;
}

static void PTGetDefaultEnhHelperTypes(SA_PARAM_NN_VALID PowerDef *pdef, PTNodeEnhHelperType **ppeTypes, AttribType **ppeAttribs)
{
	int i;

	if(pdef->fTimeRecharge) eaiPushUnique(ppeTypes,kPTNodeEnhType_Recharge);
	if(pdef->pExprCost) eaiPushUnique(ppeTypes,kPTNodeEnhType_Cost);
	if(pdef->pExprRadius) eaiPushUnique(ppeTypes,kPTNodeEnhType_Radius);
	if(pdef->pExprArc) eaiPushUnique(ppeTypes,kPTNodeEnhType_Arc);

	for(i=eaSize(&pdef->ppOrderedMods)-1; i>=0; i--)
	{
		AttribModDef *pmod = pdef->ppOrderedMods[i];
		// Ignore anything except basic mods
		if(!IS_BASIC_ASPECT(pmod->offAspect))
		{
			continue;
		}

		// Ignore mods that are insensitive to strength
		if(moddef_GetSensitivity(pmod,kSensitivityType_Strength)<=0)
			continue;

		if(IS_DAMAGE_ATTRIBASPECT(pmod->offAttrib,pmod->offAspect))
		{
			if(pmod->pExprDuration)
			{
				eaiPushUnique(ppeTypes,kPTNodeEnhType_DamageOverTime);
			}
			else
			{
				eaiPushUnique(ppeTypes,kPTNodeEnhType_DamageDirect);
			}
		}
		else
		{
			eaiPushUnique(ppeTypes,kPTNodeEnhType_Attribs);
			eaiPushUnique(ppeAttribs,pmod->offAttrib);
		}
	}
}

// Analyzes the PowerDef and returns an earray of reasonable PTNodeEnhancementDefs
static PTNodeEnhancementDef **PTGetDefaultEnhancements(SA_PARAM_NN_VALID PowerDef *pdef)
{
	int i;
	PTNodeEnhHelperType *peTypes = NULL;
	AttribType *peAttribs = NULL;
	PTNodeEnhancementDef **ppEnhDefs = NULL;
	
	if(!s_ppEnhHelpers)
	{
		ParserLoadFiles("defs/powertrees","autoenhance.def",NULL,0,parse_PTNodeEnhHelpers,&s_ppEnhHelpers);
	}

	eaiPush(&peTypes,kPTNodeEnhType_All);
	PTGetDefaultEnhHelperTypes(pdef,&peTypes,&peAttribs);
	for(i=eaSize(&pdef->ppCombos)-1; i>=0; i--)
	{
		PowerDef *pdefCombo = GET_REF(pdef->ppCombos[i]->hPower);
		if(pdefCombo) PTGetDefaultEnhHelperTypes(pdefCombo,&peTypes,&peAttribs);
	}

	for(i=0; i<eaSize(&s_ppEnhHelpers); i++)
	{
		if(-1!=eaiFind(&peTypes,s_ppEnhHelpers[i]->eType))
		{
			int bMatch = (s_ppEnhHelpers[i]->eType!=kPTNodeEnhType_Attribs);
			if(!bMatch)
			{
				int j;
				for(j=eaiSize(&s_ppEnhHelpers[i]->peAttribs)-1; !bMatch && j>=0; j--)
				{
					AttribType offAttrib = s_ppEnhHelpers[i]->peAttribs[j];
					bMatch = (-1!=eaiFind(&peAttribs,offAttrib));
					if(!bMatch)
					{
						AttribType *peUnroll = attrib_Unroll(offAttrib);
						if(peUnroll)
						{
							int k;
							for(k=eaiSize(&peUnroll)-1; !bMatch && k>=0; k--)
							{
								bMatch = (-1!=eaiFind(&peAttribs,peUnroll[k]));
							}
						}
					}
				}
			}

			if(bMatch)
			{
				eaPush(&ppEnhDefs,s_ppEnhHelpers[i]->pDef);
			}
		}
	}

	eaiDestroy(&peTypes);
	eaiDestroy(&peAttribs);

	return ppEnhDefs;
}




static void PTCreateGroupList(PTEditDoc *pdoc)
{
	int i;
	char ***pppchGroupNames = &pdoc->ppchGroupNames;

	if(!pppchGroupNames)
		return;

	if(*pppchGroupNames)
		eaClear(pppchGroupNames);
	else
		eaCreate(pppchGroupNames);
	
	for(i=0;i<eaSize(&pdoc->ppGroupWindows);i++)
	{
		eaPush(pppchGroupNames,pdoc->ppGroupWindows[i]->pDefGroupNew->pchNameFull);
		eaQSort(*pppchGroupNames,strCmp);
	}
}

static void PTCreateNodeList(PTEditDoc *pdoc)
{
	int i,c;
	const char ***pppchNodeNames = &pdoc->ppchNodeNames;

	if(!pppchNodeNames)
		return;

	if(*pppchNodeNames)
		eaClear(pppchNodeNames);
	else
		eaCreate(pppchNodeNames);

	for(i=0;i<eaSize(&pdoc->ppGroupWindows);i++)
	{
		for(c=0;c<eaSize(&pdoc->ppGroupWindows[i]->ppNodeEdits);c++)
		{
			eaPush(pppchNodeNames,pdoc->ppGroupWindows[i]->ppNodeEdits[c]->pDefNodeNew->pchNameFull);
			eaQSort(*pppchNodeNames,strCmp);
		}
	}
}
static void PTGroupWinFocus(UIWidget *pWidget, PTEditDoc *pdoc)
{
	UIWindow *pWin = (UIWindow *)pWidget;
	int i;

	if(pdoc->pFocusedGrp)
	{
		if (pdoc->pFocusedGrp->pWin == pWin)
			return;
		//pdoc->pFocusedGrp->pWin->widget.scale = 0.6;
		ui_ExpanderGroupRemoveExpander(pdoc->pExpGrp,pdoc->pFocusedGrp->pExp);
		for(i=0;i<3;i++)
		{
			pdoc->pGroupBtns[i]->clickedData = NULL;
			ui_SetActive(UI_WIDGET(pdoc->pGroupBtns[i]), false);
		}
	}
	pdoc->pFocusedGrp = NULL;
	for(i=0;i<eaSize(&pdoc->ppGroupWindows);i++)
	{
		if(pdoc->ppGroupWindows[i]->pWin == pWin)
			pdoc->pFocusedGrp = pdoc->ppGroupWindows[i];
	}

	if(!pdoc->pFocusedGrp)
		return;
	pdoc->pFocusedGrp->pWin = pWin;
	for(i=0;i<3;i++)
	{
		pdoc->pGroupBtns[i]->clickedData = pdoc->pFocusedGrp;
		ui_SetActive(UI_WIDGET(pdoc->pGroupBtns[i]), true);
	}
	//pdoc->pFocusedGrp->pWin->widget.scale = 1.0;
	ui_ExpanderGroupAddExpander(pdoc->pExpGrp,pdoc->pFocusedGrp->pExp);
}
static PTNodeEdit *PTFindNode(PTEditDoc *pdoc,const char *pchNodeName)
{
	int i,c;

	for(i=0;i<eaSize(&pdoc->ppGroupWindows);i++)
	{
		for(c=0;c<eaSize(&pdoc->ppGroupWindows[i]->ppNodeEdits);c++)
		{
			if(stricmp(pdoc->ppGroupWindows[i]->ppNodeEdits[c]->pDefNodeNew->pchName,pchNodeName) == 0 || stricmp(pdoc->ppGroupWindows[i]->ppNodeEdits[c]->pDefNodeNew->pchNameFull,pchNodeName) == 0)
				return pdoc->ppGroupWindows[i]->ppNodeEdits[c];
		}
	}
	return NULL;
}
static PTGroupEdit *PTFindGroupWindow(PTGroupEdit **ppGroups,const char *pGroupName)
{
	int c;

	if(!pGroupName)
		return NULL;
	
	for (c=0;c<eaSize(&ppGroups);c++)
	{
		if(strcmp(ppGroups[c]->pDefGroupNew->pchGroup,pGroupName) == 0 || strcmp(ppGroups[c]->pDefGroupNew->pchNameFull,pGroupName) == 0)
			return ppGroups[c];
	}
	return NULL;
}

static void PTGroupResize(PTGroupEdit *pGroup)
{
	pGroup->pExp->openedHeight = pGroup->pExpNodes->widget.y + pGroup->pExpNodes->totalHeight + 5; 

	ui_ExpanderReflow(pGroup->pExp);
}

void PTDraw(EMEditorDoc *pEMDoc)
{
	int c;
	PTEditDoc *pdoc = (PTEditDoc *)pEMDoc;

	PTGroupEdit **ppGroups = pdoc->ppGroupWindows;

	if (s_bRedrawGroups)
	{
		PTDrawGroupConnections(pdoc->ppGroupWindows);
		s_bRedrawGroups = false;
	}
	for (c=0;c<eaSize(&ppGroups);c++)
	{
		int i,j;
		for(i=0;i<eaSize(&ppGroups[c]->ppConnections);i++)
		{
			PTGroupConnection *pConnection = ppGroups[c]->ppConnections[i];
			
			//place connections with buttons
			if(pConnection->pNodeDest)
			{
				pConnection->pPairedDest->widget.x = pConnection->pNodeDest->pBtn->widget.x + pConnection->pNodeDest->pBtn->widget.width / 2;
				pConnection->pPairedDest->widget.y = pConnection->pNodeDest->pBtn->widget.y + pConnection->pNodeDest->pBtn->widget.height;
			}
		}

		for(i=0;i<eaSize(&ppGroups[c]->ppPairedIn);i++)
		{
			for(j=i+1;j<eaSize(&ppGroups[c]->ppPairedIn);j++)
			{
				if(ppGroups[c]->ppPairedIn[i]->otherBox->lastX < ppGroups[c]->ppPairedIn[j]->otherBox->lastX)
				{
					eaSwap(&ppGroups[c]->ppPairedIn, i, j);
					ppGroups[c]->ppPairedIn[i]->widget.x = ppGroups[c]->pWin->widget.width - (i * 20) - 10;
					ppGroups[c]->ppPairedIn[j]->widget.x = ppGroups[c]->pWin->widget.width - (j * 20) - 10;
				}
			}
		}

		for(i=0;i<eaSize(&ppGroups[c]->ppPairedOut);i++)
		{
			for(j=i+1;j<eaSize(&ppGroups[c]->ppPairedOut);j++)
			{
				if(ppGroups[c]->ppPairedOut[i]->otherBox->lastX > ppGroups[c]->ppPairedOut[j]->otherBox->lastX)
				{
					eaSwap(&ppGroups[c]->ppPairedOut, i, j);
					ppGroups[c]->ppPairedOut[i]->widget.x = (i * 20) + 20;
					ppGroups[c]->ppPairedOut[j]->widget.x = (j * 20) + 20;
				}
			}
		}
		for(i=0;i<eaSize(&ppGroups[c]->ppConnections);i++)
		{
			//Relocate all the labels and text boxes
			int x1,y1,x2,y2;
			PTGroupConnection *pConnection = ppGroups[c]->ppConnections[i];
			
			if(pConnection->pGroupDest)
			{
				x1 = pConnection->pPairedDest->widget.x + pConnection->pGroupDest->pWin->widget.x;
				y1 = pConnection->pPairedDest->widget.y + pConnection->pGroupDest->pWin->widget.y;
			}
			else
			{
				x1 = pConnection->pPairedDest->widget.x + pConnection->pNodeDest->pGroup->pWin->widget.x;
				y1 = pConnection->pPairedDest->widget.y + pConnection->pNodeDest->pGroup->pWin->widget.y;
			}

			if(pConnection->pGroupSrc)
			{
				x2 = pConnection->pPairedSrc->widget.x + pConnection->pGroupSrc->pWin->widget.x;
				y2 = pConnection->pPairedSrc->widget.y + pConnection->pGroupSrc->pWin->widget.y;
			}
			else
			{
				x2 = pConnection->pPairedSrc->widget.x + pConnection->pNodeSrc->pGroup->pWin->widget.x;
				y2 = pConnection->pPairedSrc->widget.y + pConnection->pNodeSrc->pGroup->pWin->widget.y;
			}

			x1 = x1 - ((x1 - x2) / 2);
			y1 = y1 - ((y1 - y2) / 2);

			if(pConnection->pUIText)
			{
				ui_WidgetSetPosition((UIWidget *)pConnection->pUIText,x1,y1);
				ui_WidgetSetPosition((UIWidget *)pConnection->pUILabel,x1,y1);
			}
		}
	}

	//Resize the group
	if(pdoc->pFocusedGrp)
		PTGroupResize(pdoc->pFocusedGrp);
}

static void PTPreSaveNode(PTNodeDef *pDef)
{
	char buf[1024];

	sprintf(buf, "PowerTree.%s", pDef->pchNameFull);
	if(!pDef->pDisplayMessage.pEditorCopy->pcMessageKey ||
		strcmp(buf,pDef->pDisplayMessage.pEditorCopy->pcMessageKey) != 0)
	{
		pDef->pDisplayMessage.pEditorCopy->pcMessageKey = allocAddString(buf);
	}

	if(!pDef->pDisplayMessage.pEditorCopy->pcDescription ||
		pDef->pDisplayMessage.pEditorCopy->pcMessageKey[0])
	{
		StructFreeString(pDef->pDisplayMessage.pEditorCopy->pcDescription);
		pDef->pDisplayMessage.pEditorCopy->pcDescription = StructAllocString("The Display Name for the power node");
	}
	if(!pDef->pDisplayMessage.pEditorCopy->pcScope ||
		pDef->pDisplayMessage.pEditorCopy->pcScope[0])
	{
		pDef->pDisplayMessage.pEditorCopy->pcScope = allocAddString("PowerTreeDef");
	}


	// Fix up the Requirements message
	sprintf(buf, "PowerTree.%s.Requirements", pDef->pchNameFull);
	if(!pDef->msgRequirements.pEditorCopy->pcMessageKey ||
		strcmp(buf,pDef->msgRequirements.pEditorCopy->pcMessageKey) != 0)
	{
		pDef->msgRequirements.pEditorCopy->pcMessageKey = allocAddString(buf);
	}

	if(!pDef->msgRequirements.pEditorCopy->pcDescription ||
		pDef->msgRequirements.pEditorCopy->pcMessageKey[0])
	{
		StructFreeString(pDef->msgRequirements.pEditorCopy->pcDescription);
		pDef->msgRequirements.pEditorCopy->pcDescription = StructAllocString("The purchase requirements for the power node");
	}
	if(!pDef->msgRequirements.pEditorCopy->pcScope ||
		pDef->msgRequirements.pEditorCopy->pcScope[0])
	{
		pDef->msgRequirements.pEditorCopy->pcScope = allocAddString("PowerTreeDef");
	}


	if(pDef->eAttrib==kAttribType_Null)
	{
		pDef->eAttrib = -1;
	}

	if(pDef->eAttrib==-1 || (pDef->pchAttribPowerTable && !pDef->pchAttribPowerTable[0]))
	{
		pDef->pchAttribPowerTable = NULL;
	}
}
static void PTPreSaveGroup(PTGroupDef *pDef)
{
	char buf[1024];
	int i;

	sprintf(buf, "PowerTree.%s", pDef->pchNameFull);
	if(!pDef->pDisplayMessage.pEditorCopy->pcMessageKey ||
		strcmp(buf,pDef->pDisplayMessage.pEditorCopy->pcMessageKey) != 0)
	{
		pDef->pDisplayMessage.pEditorCopy->pcMessageKey = allocAddString(buf);
	}

	if(!pDef->pDisplayMessage.pEditorCopy->pcDescription ||
		pDef->pDisplayMessage.pEditorCopy->pcMessageKey[0])
	{
		StructFreeString(pDef->pDisplayMessage.pEditorCopy->pcDescription);
		pDef->pDisplayMessage.pEditorCopy->pcDescription = StructAllocString("The Display Name for the power group");
	}
	if(!pDef->pDisplayMessage.pEditorCopy->pcScope ||
		pDef->pDisplayMessage.pEditorCopy->pcScope[0])
	{
		pDef->pDisplayMessage.pEditorCopy->pcScope = allocAddString("PowerTreeDef");
	}

	sprintf(buf, "PowerTreeDescription.%s", pDef->pchNameFull);
	if(!pDef->pDisplayDescription.pEditorCopy->pcMessageKey ||
		strcmp(buf,pDef->pDisplayDescription.pEditorCopy->pcMessageKey) != 0)
	{
		pDef->pDisplayDescription.pEditorCopy->pcMessageKey = allocAddString(buf);
	}

	if(!pDef->pDisplayDescription.pEditorCopy->pcDescription ||
		pDef->pDisplayDescription.pEditorCopy->pcMessageKey[0])
	{
		StructFreeString(pDef->pDisplayDescription.pEditorCopy->pcDescription);
		pDef->pDisplayDescription.pEditorCopy->pcDescription = StructAllocString("The Display Name for the power group");
	}
	if(!pDef->pDisplayDescription.pEditorCopy->pcScope ||
		pDef->pDisplayDescription.pEditorCopy->pcScope[0])
	{
		pDef->pDisplayDescription.pEditorCopy->pcScope = allocAddString("PowerTreeDef");
	}

	for(i=0;i<eaSize(&pDef->ppNodes);i++)
	{
		PTPreSaveNode(pDef->ppNodes[i]);
	}

}
static void PTPreSaveTree(PowerTreeDef *pDef)
{
	int i;
	char buf[1024];

	sprintf(buf, "PowerTree.%s", pDef->pchName);
	if(!pDef->pDisplayMessage.pEditorCopy->pcMessageKey ||
		strcmp(buf,pDef->pDisplayMessage.pEditorCopy->pcMessageKey) != 0)
	{
		pDef->pDisplayMessage.pEditorCopy->pcMessageKey = allocAddString(buf);
	}

	if(!pDef->pDisplayMessage.pEditorCopy->pcDescription ||
		pDef->pDisplayMessage.pEditorCopy->pcMessageKey[0])
	{
		StructFreeString(pDef->pDisplayMessage.pEditorCopy->pcDescription);
		pDef->pDisplayMessage.pEditorCopy->pcDescription = StructAllocString("The Display Name for the Power Tree");
	}

	if(!pDef->pDisplayMessage.pEditorCopy->pcScope ||
		pDef->pDisplayMessage.pEditorCopy->pcScope[0])
	{
		pDef->pDisplayMessage.pEditorCopy->pcScope = allocAddString("PowerTreeDef");
	}

	sprintf(buf, "PowerTreeDescription.%s", pDef->pchName);
	if(!pDef->pDescriptionMessage.pEditorCopy->pcMessageKey ||
		strcmp(buf,pDef->pDescriptionMessage.pEditorCopy->pcMessageKey) != 0)
	{
		pDef->pDescriptionMessage.pEditorCopy->pcMessageKey = allocAddString(buf);
	}

	if(!pDef->pDescriptionMessage.pEditorCopy->pcDescription ||
		pDef->pDescriptionMessage.pEditorCopy->pcMessageKey[0])
	{
		StructFreeString(pDef->pDescriptionMessage.pEditorCopy->pcDescription);
		pDef->pDescriptionMessage.pEditorCopy->pcDescription = StructAllocString("The Display Name for the power group");
	}
	if(!pDef->pDescriptionMessage.pEditorCopy->pcScope ||
		pDef->pDescriptionMessage.pEditorCopy->pcScope[0])
	{
		pDef->pDescriptionMessage.pEditorCopy->pcScope = allocAddString("PowerTreeDef");
	}

	sprintf(buf, "PowerTreeGrantMessage.%s", pDef->pchName);
	if(!pDef->pGrantMessage.pEditorCopy->pcMessageKey ||
		strcmp(buf,pDef->pGrantMessage.pEditorCopy->pcMessageKey) != 0)
	{
		pDef->pGrantMessage.pEditorCopy->pcMessageKey = allocAddString(buf);
	}

	if(!pDef->pGrantMessage.pEditorCopy->pcDescription ||
		pDef->pGrantMessage.pEditorCopy->pcMessageKey[0])
	{
		StructFreeString(pDef->pGrantMessage.pEditorCopy->pcDescription);
		pDef->pGrantMessage.pEditorCopy->pcDescription = StructAllocString("The notification text displayed to the players when they are granted a power tree node from this tree");
	}
	if(!pDef->pGrantMessage.pEditorCopy->pcScope ||
		pDef->pGrantMessage.pEditorCopy->pcScope[0])
	{
		pDef->pGrantMessage.pEditorCopy->pcScope = allocAddString("PowerTreeDef");
	}

	for(i=0;i<eaSize(&pDef->ppGroups);i++)
	{
		PTPreSaveGroup(pDef->ppGroups[i]);
	}
}
static void PTPreSave(PTEditDoc* pdoc)
{
	PTPreSaveTree(pdoc->pDefNew);
}
static EMTaskStatus PTSaveDef(PTEditDoc* pdoc)
{
	PowerTreeDefs defs = {0};
	int i,c;
	char achName[MAX_POWER_NAME_LEN];
	char achGroup[MAX_POWER_NAME_LEN * 2];
	char achNode[MAX_POWER_NAME_LEN * 3];

	PowerTreeDef* pOrig = pdoc->pDefOrig;
	PowerTreeDef* pNew = pdoc->pDefNew;

	sprintf(achName,"%s",pNew->pchName);

	eaPush(&defs.ppPowerTrees,pNew);
	

	for(i=0;i<eaSize(&pdoc->ppGroupWindows);i++)
	{
		if (pdoc->ppGroupWindows[i]->pWin)
		{
			pdoc->ppGroupWindows[i]->pDefGroupNew->x = pdoc->ppGroupWindows[i]->pWin->widget.x;
			pdoc->ppGroupWindows[i]->pDefGroupNew->y = pdoc->ppGroupWindows[i]->pWin->widget.y;
		}
	}
	//Fill in the full names
	for(i=0;i<eaSize(&pNew->ppGroups);i++)
	{

		sprintf(achGroup,"%s.%s",achName,pNew->ppGroups[i]->pchGroup);
		if(pNew->ppGroups[i]->pchNameFull)
			free(pNew->ppGroups[i]->pchNameFull);

		pNew->ppGroups[i]->pchNameFull = strdup(achGroup);

		for(c=0;c<eaSize(&pNew->ppGroups[i]->ppNodes);c++)
		{
			sprintf(achNode,"%s.%s",achGroup,pNew->ppGroups[i]->ppNodes[c]->pchName);

			pNew->ppGroups[i]->ppNodes[c]->pchNameFull = allocAddString(achNode);
		}
	}

	PTPreSave(pdoc);

	if(!powertrees_Load_TreeValidate(pdoc->pDefNew))
	{
		char *pchFileName = NULL;
		
		estrCreate(&pchFileName);
		estrPrintf(&pchFileName,"%s.bak",pNew->pchFile);

		if(ParserWriteTextFile(pchFileName,parse_PowerTreeDefs,&defs,0,0))
		{
			ErrorFilenamef(pdoc->pDefNew->pchFile,"Pre-Save Validation Failed\nPower Tree %s NOT SAVED!!!\n\nBackup saved at: %s",pNew->pchName,pchFileName);
		}
		else
		{
			ErrorFilenamef(pdoc->pDefNew->pchFile,"Pre-Save Validation Failed\nPower Tree %s NOT SAVED!!!\n\nUNABLE TO SAVE BACKUP: %s",pNew->pchName,pchFileName);
		}
		return EM_TASK_FAILED;
	}
	else
	{
		//Clear out data in the cloned nodes
		for(i=0;i<eaSize(&pNew->ppGroups);i++)
		{
			for(c=0;c<eaSize(&pNew->ppGroups[i]->ppNodes);c++)
			{
				if(IS_HANDLE_ACTIVE(pNew->ppGroups[i]->ppNodes[c]->hNodeClone))
				{
					int j;

					for(j=eaSize(&pNew->ppGroups[i]->ppNodes[c]->ppRanks)-1;j>=0;j--)
					{
						PTNodeRankDef *pRank = pNew->ppGroups[i]->ppNodes[c]->ppRanks[j];
						REMOVE_HANDLE(pRank->hPowerDef);
						eaRemove(&pNew->ppGroups[i]->ppNodes[c]->ppRanks,j);
						StructDestroy(parse_PTNodeRankDef,pRank);
					}

					for(j=eaSize(&pNew->ppGroups[i]->ppNodes[c]->ppEnhancements)-1;j>=0;j--)
					{
						PTNodeEnhancementDef *pEnh = pNew->ppGroups[i]->ppNodes[c]->ppEnhancements[j];
						REMOVE_HANDLE(pEnh->hPowerDef);
						eaRemove(&pNew->ppGroups[i]->ppNodes[c]->ppEnhancements,j);
						StructDestroy(parse_PTNodeEnhancementDef,pEnh);
					}
				}
			}

		}
	}

	// Send save to server
	resSetDictionaryEditMode(g_hPowerTreeDefDict, true);
	resSetDictionaryEditMode(gMessageDict, true);
	emSetResourceStateWithData(pdoc->emDoc.editor, pdoc->pDefNew->pchName, EMRES_STATE_SAVING, &pdoc->emDoc);
	resRequestSaveResource(g_hPowerTreeDefDict, pdoc->pDefNew->pchName, pdoc->pDefNew);
	return EM_TASK_INPROGRESS;
}

static bool PTUpdateDocDirty(PTEditDoc *pdoc)
{
	pdoc->bDirty = (eaSize(&pdoc->ppDirty) > 0) || pdoc->bPermaDirty;

	pdoc->emDoc.saved = !pdoc->bDirty;

	return pdoc->bDirty;
}

static void PTPermaDirty(PTEditDoc *pdoc)
{
	pdoc->bPermaDirty = true;

	PTUpdateDocDirty(pdoc);
}

EMTaskStatus PTEditorSaveDoc(EMEditorDoc *pAEdoc)
{
	PTEditDoc* pdoc = (PTEditDoc *)pAEdoc;
	char achFileName[MAX_PATH];
	EMTaskStatus status;

	resSetDictionaryEditMode(g_hPowerTreeDefDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Required at start of save to handle server saving logic
	if (!emHandleSaveResourceState(pAEdoc->editor, pdoc->pDefNew->pchName, &status)) {
		// Get in here if save state not handled by EM common code

		//set filename to be used
		sprintf(achFileName,"defs/powertrees/%s.powertree",pdoc->pDefNew->pchName);
		pdoc->pDefNew->pchFile = (char*)allocAddString(achFileName);

		// Perform locking
		if (!resGetLockOwner(g_hPowerTreeDefDict, pdoc->pDefNew->pchName)) {
			// Don't have lock, so ask server to lock and go into locking state
			emSetResourceState(pAEdoc->editor, pdoc->pDefNew->pchName, EMRES_STATE_LOCKING_FOR_SAVE);
			resRequestLockResource(g_hPowerTreeDefDict, pdoc->pDefNew->pchName, pdoc->pDefNew);
			return EM_TASK_INPROGRESS;
		}
		// Get here if have the lock

		status = PTSaveDef(pdoc);
	}

	if(status == EM_TASK_SUCCEEDED)
	{
		int i;
		for(i=eaSize(&pdoc->ppDirty)-1; i>=0; i--)
		{
			pdoc->ppDirty[i]->bDirty = false;
			if(pdoc->ppDirty[i]->pUIText)
				ui_SetChanged(UI_WIDGET(pdoc->ppDirty[i]->pUIText), false);
			if(pdoc->ppDirty[i]->pUICombo)
				ui_SetChanged(UI_WIDGET(pdoc->ppDirty[i]->pUICombo), false);
			if(pdoc->ppDirty[i]->pUITextArea)
				ui_SetChanged(UI_WIDGET(pdoc->ppDirty[i]->pUITextArea), false);
		}
		eaDestroy(&pdoc->ppDirty);

		//SET_HANDLE_FROM_STRING("PowerTreeDef",pdoc->pchNewName,pdoc->hDefCheckPoint);

		emDocAssocFile(pAEdoc,pdoc->pDefNew->pchFile);

		StructCopyAll(parse_PowerTreeDef, pdoc->pDefNew, pdoc->pDefOrig);
	}

	PTUpdateDocDirty(pdoc);
	return status;
}

static void PTGroupTextChanged(UITextEntry *pEntry, PTGroupConnection *pConnection)
{
	int iChange=atoi(ui_TextEntryGetText(pEntry));
	PTGroupEdit *pGroupSource  = pConnection->pGroupSrc;
	
	if(IS_HANDLE_ACTIVE(pGroupSource->pDefGroupNew->pRequires->hGroup))
	{
		ui_TextEntrySetTextAndCallback(pGroupSource->pGroupNum,ui_TextEntryGetText(pEntry));
	}
}
static void PTGroupTextUnfocus(UITextEntry *pEntry, UIAnyWidget *pTarget)
{
	ui_SetActive(UI_WIDGET(pEntry), false);
}
static void PTGroupTextTick(UITextEntry *pEntry, UI_PARENT_ARGS)
{
	PTGroupConnection *pGroup = (PTGroupConnection *)pEntry->widget.onFocusData;

	UI_GET_COORDINATES(pEntry);

	CBoxClipTo(&pBox,&box);

	if(!ui_IsActive(UI_WIDGET(pEntry)) || (!mouseCollision(&box) && !ui_IsFocused(pEntry)))
	{
		ui_SetActive(UI_WIDGET(pEntry), false);
		if(pGroup->pGroupDest)
			ui_ScrollAreaRemoveChild(pGroup->pGroupDest->pdoc->pScrollArea,(UIWidget *)pGroup->pUIText);
		else
			ui_ScrollAreaRemoveChild(pGroup->pNodeDest->pdoc->pScrollArea,(UIWidget *)pGroup->pUIText);
		PTGroupTextChanged(pEntry,pGroup);
	}
	ui_TextEntryTick(pEntry,pX,pY,pW,pH,pScale);
}
static void PTGroupLabelTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	PTGroupConnection *pGroup = (PTGroupConnection *)pLabel->widget.onFocusData;

	UI_GET_COORDINATES(pLabel);

	CBoxClipTo(&pBox, &box);

	if (!ui_IsActive(UI_WIDGET(pLabel)))
		return;

	if(mouseCollision(&box) && !ui_IsActive(UI_WIDGET(pGroup->pUIText)))
	{
		if(pGroup->pGroupDest)
			ui_ScrollAreaAddChild(pGroup->pGroupDest->pdoc->pScrollArea,(UIWidget *)pGroup->pUIText);
		else
			ui_ScrollAreaAddChild(pGroup->pNodeDest->pdoc->pScrollArea,(UIWidget *)pGroup->pUIText);
		ui_SetActive(UI_WIDGET(pGroup->pUIText), true);
	}

}

static void PTDrawNodeConnections(PTNodeEdit *pNode, PTGroupEdit *pGroup)
{
	PTNodeEdit *pNodeFind;
	int i;

	for(i=eaSize(&pNode->ppConnections)-1;i>=0;i--)
	{
		SAFE_FREE(pNode->ppConnections[i]);
	}
	eaDestroy(&pNode->ppConnections);
	eaDestroyEx(&pNode->ppPairedPower,ui_WidgetQueueFree);

	if(!IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeRequire))
		return;

	pNodeFind = PTFindNode(pGroup->pdoc,REF_STRING_FROM_HANDLE(pNode->pDefNodeNew->hNodeRequire));

	if(pNodeFind != NULL)
	{
		UIPairedBox *p1;
		UIPairedBox *p2;
		PTGroupConnection *pConnection;
		ui_PairedBoxCreatePair(&p1,&p2,ColorOrange,pGroup->pdoc->pScrollArea);
		p1->widget.scale = 0.5;
		p2->widget.scale = 0.5;
		p1->widget.x = pNode->pBtn->widget.x + pNode->pBtn->widget.width / 2;
		p1->widget.y = pNode->pBtn->widget.y - p1->widget.height * p1->widget.scale;
		p2->widget.x = pNodeFind->pBtn->widget.x + pNodeFind->pBtn->widget.width / 2;
		p2->widget.y = pNodeFind->pBtn->widget.y + pNodeFind->pBtn->widget.height;

		p1->bVertical = true;
		p2->bVertical = true;
		ui_WindowAddChild(pGroup->pWin,p1);
		ui_WindowAddChild(pGroup->pWin,p2);
		eaPush(&pNode->ppPairedPower,p1);
		eaPush(&pNode->ppPairedPower,p2);

		//Create group connection
		pConnection = calloc(sizeof(PTGroupConnection),1);

		pConnection->pPairedSrc = p1;
		pConnection->pPairedDest = p2;
		pConnection->pchNameCompare = pNodeFind->pDefNodeNew->pchName;
		pConnection->pNodeDest = pNodeFind;
		pConnection->pNodeSrc = pNode;

		pConnection->pUILabel = NULL;
		pConnection->pUIText = NULL;

		eaPush(&pNode->ppConnections,pConnection);
	}
}
static void PTDrawGroupLayers(PTGroupEdit *pGroup)
{
	int iLayerCount = 0;
	int i,c;
	F32 fWidth = 0;

	if(pGroup->pdoc->bLevelView)
		return;

	for(i=0;i<eaSize(&pGroup->ppNodeEdits);i++)
	{
		int iCount=0;
		PTNodeEdit *pCurNode = pGroup->ppNodeEdits[i];
		while(pCurNode && IS_HANDLE_ACTIVE(pCurNode->pDefNodeNew->hNodeRequire))
		{
			iCount++;
			pCurNode = PTFindNode(pGroup->pdoc,REF_STRING_FROM_HANDLE(pCurNode->pDefNodeNew->hNodeRequire));
			if (pCurNode == pGroup->ppNodeEdits[i])
			{
				Alertf("Infinite Loop for requires on node %s",pCurNode->pDefNodeNew->pchNameFull);
				pCurNode = NULL;
			}
		}

		iLayerCount = max(iLayerCount,iCount);
		pGroup->ppNodeEdits[i]->iLayer = iCount;
	}

	pGroup->pWin->widget.height = 80 * (iLayerCount + 1);

	for(i=0;i<=iLayerCount;i++)
	{
		PTNodeEdit **ppNodes = NULL;
		F32 fTempWidth = 0;
		int x=5;
		for(c=0;c<eaSize(&pGroup->ppNodeEdits);c++)
		{
			if(pGroup->ppNodeEdits[c]->iLayer == i)
			{
				eaPush(&ppNodes,pGroup->ppNodeEdits[c]);
				fTempWidth += 5 + pGroup->ppNodeEdits[c]->pBtn->widget.width;
			}
		}

		for(c=0;c<eaSize(&ppNodes);c++)
		{
			ppNodes[c]->pBtn->widget.x = x;
			ppNodes[c]->pBtn->widget.y = i * 80 + 20;
			x += 5 + ppNodes[c]->pBtn->widget.width;
		}

		fWidth=max(fWidth,fTempWidth);
	}

	pGroup->pWin->widget.width = fWidth;
}

static void PTDrawGroupConnections(PTGroupEdit **ppGroups)
{
	int c;
	int i;

	// Bail out if this is a talent tree
	if (eaSize(&ppGroups) > 0 && ppGroups[0]->pDefGroupNew->iUIGridRow > 0)
	{
		return;
	}

	for (c=0;c<eaSize(&ppGroups);c++)
	{
		
		for(i=eaSize(&ppGroups[c]->ppConnections)-1;i>=0;i--)
		{
			if(ppGroups[c]->ppConnections[i]->pUIText)
			{
			ui_WidgetQueueFreeAndNull(&ppGroups[c]->ppConnections[i]->pUIText);
			ui_WidgetQueueFreeAndNull(&ppGroups[c]->ppConnections[i]->pUILabel);
			}
			SAFE_FREE(ppGroups[c]->ppConnections[i]);
		}
		eaDestroy(&ppGroups[c]->ppConnections);
		eaDestroyEx(&ppGroups[c]->ppPairedIn,ui_WidgetQueueFree);
		eaDestroyEx(&ppGroups[c]->ppPairedOut,ui_WidgetQueueFree);
		eaDestroyEx(&ppGroups[c]->ppPairedPower,ui_WidgetQueueFree);
	}
	for (c=0;c<eaSize(&ppGroups);c++)
	{
		// The new code
		if(IS_HANDLE_ACTIVE(ppGroups[c]->pDefGroupNew->pRequires->hGroup))
		{
			PTGroupEdit *pGroupFind = PTFindGroupWindow(ppGroups,REF_STRING_FROM_HANDLE(ppGroups[c]->pDefGroupNew->pRequires->hGroup));

			if(pGroupFind)
			{
				UIPairedBox *p1;
				UIPairedBox *p2;
				PTGroupConnection *pconnection;
				char *pchValue = NULL;
				char achCatch1[250];
				char achCatch2[250];
				achCatch1[0] = 0;
				achCatch2[0] = 0;
				ui_PairedBoxCreatePair(&p1,&p2,ColorBlue,pGroupFind->pdoc->pScrollArea);
				p1->widget.scale = 0.5;
				p2->widget.scale = 0.5;
				p1->widget.x = eaSize(&ppGroups[c]->ppPairedOut) * 20;
				p1->widget.y = 0;
				p2->widget.x = pGroupFind->pWin->widget.width - (eaSize(&pGroupFind->ppPairedIn) * 20) - 10;
				p2->widget.y = pGroupFind->pWin->widget.height - (p2->widget.height * p2->widget.scale);

				p1->bVertical = true;
				p2->bVertical = true;
				ui_WindowAddChild(ppGroups[c]->pWin,p1);
				ui_WindowAddChild(pGroupFind->pWin,p2);
				eaPush(&ppGroups[c]->ppPairedOut,p1);
				eaPush(&pGroupFind->ppPairedIn,p2);

				//Create group connection
				pconnection = calloc(sizeof(PTGroupConnection),1);

				pconnection->pPairedSrc = p1;
				pconnection->pPairedDest = p2;
				pconnection->pchNameCompare = pGroupFind->pDefGroupNew->pchGroup;
				pconnection->pGroupDest = pGroupFind;
				pconnection->pGroupSrc = ppGroups[c];

				estrStackCreate(&pchValue);
				estrPrintf(&pchValue,"%d",ppGroups[c]->pDefGroupNew->pRequires->iGroupRequired);

				pconnection->pUILabel = ui_LabelCreate(pchValue,0,0);
				pconnection->pUIText = ui_TextEntryCreate(pchValue,0,0);
				pconnection->pUIText->widget.width = 25;
				ui_SetActive(UI_WIDGET(pconnection->pUIText), false);

				estrDestroy(&pchValue);
				pconnection->pUILabel->widget.tickF = PTGroupLabelTick;
				pconnection->pUILabel->widget.onFocusData = pconnection;
				pconnection->pUIText->widget.tickF = PTGroupTextTick;
				pconnection->pUIText->widget.onFocusData = pconnection;
				pconnection->pUIText->widget.unfocusF = PTGroupTextUnfocus;

				//ui_TextEntrySetChangedCallback(pconnection->pUIText,PTGroupTextChanged,pconnection);

				ui_ScrollAreaAddChild(ppGroups[c]->pdoc->pScrollArea,(UIWidget*)pconnection->pUILabel);

				eaPush(&ppGroups[c]->ppConnections,pconnection);
			}
			
		}
		PTDrawGroupLayers(ppGroups[c]);

		for(i=0;i<eaSize(&ppGroups[c]->ppNodeEdits);i++)
		{
			PTDrawNodeConnections(ppGroups[c]->ppNodeEdits[i],ppGroups[c]);
		}
	}
}

static UIWidget *PTGetFieldWidget(PTField *pfield)
{
	if(pfield->pUIText) return (UIWidget*)pfield->pUIText;
	return NULL;
}
static void PTRemoveField(PTField *pfield)
{
	UIWidget* pWidget = PTGetFieldWidget(pfield);
	ui_WindowRemoveChild(pfield->pdoc->pWin,pWidget);

	if(pfield->pUIText)
		ui_WidgetQueueFree(UI_WIDGET(pfield->pUIText));
	if(pfield->pUILabel)
		ui_WidgetQueueFree(UI_WIDGET(pfield->pUILabel));

}
static void PTDisplayMenu(UIMenu *pMenu)
{
	if(pMenu)
		ui_MenuPopupAtCursor(pMenu);
}

static void PTMenuRankOpen(UIMenuItem *pMenuItem,PTRankEdit *pRank);
static void PTMenuRankDelete(UIMenuItem *pMenuItem,PTRankEdit *pRank);
static void PTMenuRankUp(UIMenuItem *pMenuItem, PTRankEdit *pRank);
static void PTMenuRankDown(UIMenuItem *pMenuItem, PTRankEdit *pRank);
static void PTMenuRankRevert(UIMenuItem *pMenuItem,PTRankEdit *pRank);

static void PTMenuEnhancementDelete(UIMenuItem *pmenuItem,PTEnhancementEdit *pEnhancement);

static void PTExpandGroupRClick(UIExpander *expand, PTGroupEdit *pGroup)
{
	ui_MenuItemSetTextString( pGroup->pMenu->items[0], pGroup->pDefGroupNew->pchGroup );
	PTDisplayMenu(pGroup->pMenu);
}

static void PTExpandNodeRClick(UIExpander *expand,PTNodeEdit *pNode)
{
	char *pchName = NULL;
	int i;
	PTRankEdit *pRank = NULL;
	PTEnhancementEdit *pEnh = NULL;
	estrStackCreate(&pchName);
	if(expand != pNode->pExp)
	{
		for(i=0;i<eaSize(&pNode->pDefNodeNew->ppRanks);i++)
		{
			if(expand == pNode->ppRankEdits[i]->pExp)
				break;
		}
		if(i<eaSize(&pNode->pDefNodeNew->ppRanks))
			pRank = pNode->ppRankEdits[i];
		else
		{
			for(i=0;i<eaSize(&pNode->pDefNodeNew->ppEnhancements);i++)
			{
				if(expand == pNode->ppEnhancementEdits[i]->pExp)
				{
					pEnh = pNode->ppEnhancementEdits[i];
					break;
				}
			}
		}
		if(pRank)
		{
			UIMenu *pMenu;

			estrPrintf(&pchName,"%s",REF_STRING_FROM_HANDLE(pRank->pDefRankNew->hPowerDef));
			pMenu = ui_MenuCreateWithItems("Rank Option Menu",
				ui_MenuItemCreate(strdup(pchName),UIMenuCallback,NULL,NULL,NULL),
				ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
				ui_MenuItemCreate("Open Power",UIMenuCallback,PTMenuRankOpen,pRank,NULL),
				ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
				ui_MenuItemCreate("Delete Rank",UIMenuCallback,PTMenuRankDelete,pRank,NULL),
				ui_MenuItemCreate("Revert Rank",UIMenuCallback,PTMenuRankRevert,pRank,NULL),
				ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
				ui_MenuItemCreate("Move Rank Up",UIMenuCallback,PTMenuRankUp,pRank,NULL),
				ui_MenuItemCreate("Move Rank Down",UIMenuCallback,PTMenuRankDown,pRank,NULL),
				NULL);
			pMenu->items[0]->active = false;
			if(i == 0)
				pMenu->items[4]->active = false;
			if(i == eaSize(&pNode->pDefNodeNew->ppRanks))
				pMenu->items[4]->active = false;

			
			pNode->pMenu->opened = false; 
			if (pNode->pMenu->type == UIMenuTransient)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pNode->pMenu));//Close the nodes menu

			if(pRank->pMenu)
				ui_WidgetQueueFreeAndNull(&pRank->pMenu);
			pRank->pMenu = pMenu;
			PTDisplayMenu(pMenu); //Open the rank menu
		}
		else if(pEnh)
		{
			UIMenu *pMenu;

			estrPrintf(&pchName,"%s",REF_STRING_FROM_HANDLE(pEnh->pDefNew->hPowerDef));
			pMenu = ui_MenuCreateWithItems("Enhancement Option menu",
				ui_MenuItemCreate(strdup(pchName),UIMenuCallback,NULL,NULL,NULL),
				ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
				ui_MenuItemCreate("Delete Enhancement",UIMenuCallback,PTMenuEnhancementDelete,pEnh,NULL),
				ui_MenuItemCreate("Revert Enhancement",UIMenuCallback,NULL,pEnh,NULL),
				NULL);

			pMenu->items[0]->active = false;
			
			pNode->pMenu->opened = false;
			if(pNode->pMenu->type == UIMenuTransient)
				ui_WidgetRemoveFromGroup(UI_WIDGET(pNode->pMenu));

			if(pEnh->pMenu)
				ui_WidgetQueueFreeAndNull(&pEnh->pMenu);

			pEnh->pMenu = pMenu;
			PTDisplayMenu(pMenu);
		}
	}
	else
	{
		ui_MenuItemSetTextString(pNode->pMenu->items[0], pNode->pDefNodeNew->pchName);
		PTDisplayMenu(pNode->pMenu);
		pNode->pGroup->pMenu->opened = false; 
		if (pNode->pGroup->pMenu->type == UIMenuTransient)
			ui_WidgetRemoveFromGroup(UI_WIDGET(pNode->pGroup->pMenu));//Closes the group menu
	}

	estrDestroy(&pchName);

}
static void PTButtonNodeRClick(UIButton *pbutton, PTNodeEdit *pNode)
{
	PTExpandNodeRClick(pNode->pExp,pNode);
}
static void PTTextFieldTick(UITextEntry *entry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(entry);

	CBoxClipTo(&pBox, &box);

	if (!ui_IsActive(UI_WIDGET(entry)))
		return;

	if (mouseClickHit(MS_RIGHT, &box))
	{
		PTField *pfield = (PTField*)UI_EDITABLE(entry)->changedData;
		PTDisplayMenu(pfield->pMenu);
		//PEDisplayAttribMenu(pfield->pattrib,pfield);
		inpHandled();
	}

	ui_TextEntryTick(entry,pX,pY,pW,pH,pScale);
}
static PTField *PTCreateBasicField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
								   ParseTable *ptable, char *pchField, const char *pchDictName, char *pchAlias, bool bLabel, UIButton *pBtn,
								   int *px, int *py, int width, bool bVertical, bool bisTitle, UIMenu *pMenu)
{
	int i;
	PTField *pfield;
	UILabel *plabel;
	size_t offset = -1;

	FORALL_PARSETABLE(ptable, i)
	{
		if(!stricmp(pchField,ptable[i].name))
		{
			offset = ptable[i].storeoffset;
			break;
		}
	}

	if(offset==-1)
		return NULL;

	pfield = calloc(sizeof(PTField),1);
	pfield->pdoc = pdoc;
	pfield->pOld = pOld;
	pfield->pNew = pNew;
	pfield->ptable = ptable;
	pfield->pchDictName = pchDictName;
	pfield->offset = offset;
	pfield->type = ptable[i].type;
	pfield->bVertical = bVertical;
	pfield->bisTitle = bisTitle;
	pfield->pexp = pexp;
	pfield->pbtn = pBtn;
	pfield->pMenu = pMenu;
	eaPush(&pdoc->ppFields,pfield);

	// Creates the label and moves over
	if(bLabel)
	{
		plabel = ui_LabelCreate(pchAlias?pchAlias:pchField,*px,*py);
		plabel->widget.width = width;
		if(pwin)
			ui_PaneAddChild(pwin,plabel);
		if(pexp)
			ui_ExpanderAddChild(pexp,(UIWidget *)plabel);
		pfield->pUILabel = plabel;
		if(bVertical)
			*py += plabel->widget.height + 5;
		else
			*px += plabel->widget.width + 5;
	}

	return pfield;
}

typedef union FakeRefHandle { void *__pData_INTERNAL; ReferenceHandle __handle_INTERNAL; } FakeRefHandle;

static bool PTMessageFieldUpdate(PTField *pField, const Message *pMessage)
{
	DisplayMessage *pTarget = (DisplayMessage*)((char*)pField->pNew + pField->offset);
	DisplayMessage *pOrig = (DisplayMessage*)((char*)pField->pOld + pField->offset);
	const Message *pMessageOrig = pField->pOld ? (pOrig->pEditorCopy ? pOrig->pEditorCopy : GET_REF(pOrig->hMessage)) : NULL;

	if (!pField->bRevert &&
		(!pMessage || (StructCompare(parse_Message, pMessage, pTarget->pEditorCopy, 0,0,0) != 0)) &&
		!PTCanEdit(pField->pdoc))
		pField->bRevert = true;

	if(pField->bRevert)
	{
		pMessage = pField->pOld ? pMessageOrig : pMessage;
		pField->bRevert = false;
	}

	if (pMessage)
		StructCopyAll(parse_Message,pMessage,pTarget->pEditorCopy);
	else
	{
		StructFreeString(pTarget->pEditorCopy->pcDefaultString);
		pTarget->pEditorCopy->pcDefaultString = NULL;
	}

	if (pField->pUIMessage)
		ui_MessageEntrySetMessage(pField->pUIMessage, pTarget->pEditorCopy);

	if(!pField->pOld)
		return true;

	if((!pMessageOrig || !pMessageOrig->pcDefaultString) && pTarget->pEditorCopy)
		return true;

	if(pMessageOrig && (!pTarget->pEditorCopy || !pTarget->pEditorCopy->pcDefaultString))
		return true;

	if(strcmp(pMessageOrig->pcDefaultString,pTarget->pEditorCopy->pcDefaultString) != 0)
		return true;

	return false;
}

static bool PTTextFieldUpdateString(SA_PARAM_NN_VALID PTField *pfield, SA_PARAM_NN_STR const char* pchText)
{
	char **ppchTarget = (char**)((char*)(pfield->pNew) + pfield->offset);
	char **ppchOrig = (char**)((char*)(pfield->pOld) + pfield->offset);
	int i;

	if (!pfield->bRevert &&
		((!*ppchTarget && (pchText[0] != '\0')) || (*ppchTarget && (strcmp(*ppchTarget,pchText) != 0))) &&
		!PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		pchText = pfield->pOld ? (*ppchOrig ? *ppchOrig : "") : (*ppchTarget ? *ppchTarget : "");

		if(pfield->pUIText)
			ui_TextEntrySetText(pfield->pUIText, pchText);

		if(pfield->type & TOK_POOL_STRING)
			pfield->bPoolString = true;

		pfield->bRevert = false;
	}

	if(*ppchTarget!=pchText)
	{
		if(pfield->bPoolString)
		{
			*ppchTarget = NULL;
			pfield->bPoolString = false;
		}
		else
		{
			StructFreeString(*ppchTarget);
			
		}
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory '*pchText'"
		*ppchTarget = StructAllocString(pchText);
	}

	// Hack to detect changes to the name field of the power and update the doc
	if(pfield->pdoc->pDefNew==pfield->pNew && pfield->offset==0)
	{
		free(pfield->pdoc->pchNewName);
		pfield->pdoc->pchNewName = strdup(*ppchTarget);
		sprintf(pfield->pdoc->emDoc.doc_display_name,"%s",pfield->pdoc->pchNewName);
		pfield->pdoc->emDoc.name_changed = (U32)true;
	}

	// Hack to detect changes to the name field of a group, update the window
	for(i=0;i<eaSize(&pfield->pdoc->ppGroupWindows);i++)
	{
		if(pfield->pdoc->ppGroupWindows[i]->pDefGroupNew == pfield->pNew && pfield->offset==0)
		{
			if(pfield->pdoc->ppGroupWindows[i]->pWin)
				ui_WindowSetTitle(pfield->pdoc->ppGroupWindows[i]->pWin,*ppchTarget);
			ui_WidgetSetTextString(UI_WIDGET(pfield->pdoc->ppGroupWindows[i]->pExp), ppchTarget ? *ppchTarget : NULL);
			s_bRedrawGroups = true;
		}
	}

	if (!pfield->pOld)
	{

		return true;
	}

	ANALYSIS_ASSUME(*ppchTarget != NULL);
#pragma warning(suppress:6001) // /analyze flags "Using uninitialized memory '**ppchTarget'"
	if (!*ppchOrig && strlen(*ppchTarget)>0)
	{
		return true;
	}

	if (*ppchOrig && strcmp(*ppchTarget,*ppchOrig))
	{
		return true;
	}

	return false;
}

static bool PTTextFieldUpdateReference(SA_PARAM_NN_VALID PTField *pfield, SA_PARAM_NN_STR const char *pchText)
{
	bool bDirty = false;
	const char *pchTarget = NULL;
	const char *pchOrig = NULL;
	char *pchTemp = NULL;
	FakeRefHandle* phTarget = (FakeRefHandle*)((char*)(pfield->pNew) + pfield->offset);
	FakeRefHandle* phOrig = (FakeRefHandle*)((char*)(pfield->pOld) + pfield->offset);

	if(pfield->pNew != NULL && IS_HANDLE_ACTIVE(*phTarget))
	{
		pchTarget = REF_STRING_FROM_HANDLE(*phTarget);
	}
	if(pfield->pOld != NULL && IS_HANDLE_ACTIVE(*phOrig))
	{
		pchOrig = REF_STRING_FROM_HANDLE(*phOrig);
	}

	if (!pfield->bRevert &&
		((!pchTarget && (pchText[0] != '\0')) || (pchTarget && (strcmp(pchTarget,pchText) != 0))) &&
		!PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		pchText = pchOrig ? pchOrig : (pchTarget ? pchTarget : "");
		ui_TextEntrySetText(pfield->pUIText, pchText);
		pfield->bRevert = false;
	}

	if(strlen(pchText)==0)
	{
		if(pchTarget)
		{
			REMOVE_HANDLE((*phTarget));
		}
		// Dirty if there wasn't an old attrib, or there was an old ref
		bDirty = (!pfield->pOld || (pchOrig && strlen(pchOrig)>0));
	}
	else
	{
		if(pchTarget)
		{
			REMOVE_HANDLE((*phTarget));
		}

		pchTemp = strchr(pchText,'\\');
		while (pchTemp)
		{
			pchTemp[0] = '/';
			pchTemp = strchr(pchTemp,'\\');
		}
		SET_HANDLE_FROM_STRING(pfield->pchDictName,pchText,*phTarget);

		// Dirty if there wasn't an old attrib, or there wasn't an old ref, 
		//  or the old and new refs have different strings
		bDirty = (!pfield->pOld || !pchOrig || strlen(pchOrig)==0 || strcmp(pchOrig,pchText));
	}

	return bDirty;
}

static bool PTTextFieldUpdateExpression(SA_PARAM_NN_VALID PTField *pfield, SA_PARAM_NN_STR const char *pchText)
{
	const char *pchConstText = (const char*)pchText;
	size_t iLen;
	Expression **ppExprTarget = (Expression**)((char*)(pfield->pNew) + pfield->offset);
	Expression **ppExprOrig = (Expression**)((char*)(pfield->pOld) + pfield->offset);

	if (!pfield->bRevert)
	{
		const char *pchTarget = *ppExprTarget ? exprGetCompleteString(*ppExprTarget) : "";
		if (!PTCanEdit(pfield->pdoc))
		{
			if (!pchTarget && (pchText[0] != '\0'))
			{
				pfield->bRevert = true;
			}
			else if (pchTarget)
			{
				ANALYSIS_ASSUME(pchTarget != NULL);
				if (strcmp(pchTarget,pchText) != 0)
				{
					pfield->bRevert = true;
				}
			}
		}
	}

	if(pfield->bRevert)
	{
		pchConstText = pfield->pOld ? (*ppExprOrig ? exprGetCompleteString(*ppExprOrig) : "") : (*ppExprTarget ? exprGetCompleteString(*ppExprTarget) : "");
		ui_TextEntrySetText(pfield->pUIText, pchConstText);
		pfield->bRevert = false;
	}

	iLen = strlen(pchConstText);

	if(iLen==0)
	{
		if(*ppExprTarget)
		{
			exprDestroy(*ppExprTarget);
			*ppExprTarget = NULL;
		}
		// Dirty if there wasn't an old attrib, or there was an old expression
		return (!pfield->pOld || *ppExprOrig);
	}
	else
	{
		char* pchExprTargetString = NULL;
		char* pchExprOrigString = NULL;
		int iRet;

		estrStackCreate(&pchExprTargetString);
		estrStackCreate(&pchExprOrigString);

		if(!*ppExprTarget)
		{
			*ppExprTarget = exprCreate();
		}

		exprGetCompleteStringEstr(*ppExprTarget, &pchExprTargetString);

		if(exprIsEmpty(*ppExprTarget) || !pchExprTargetString || stricmp(pchExprTargetString, pchConstText))
		{
			exprSetOrigStr(*ppExprTarget, pchConstText, NULL);
			exprGetCompleteStringEstr(*ppExprTarget, &pchExprTargetString);
		}

		if(pfield->pOld && *ppExprOrig)
			exprGetCompleteStringEstr(*ppExprOrig, &pchExprOrigString);
		// Dirty if there wasn't an old attrib, or there wasn't an old expression, 
		//  or the old and new expressions have different strings
		iRet = (!pfield->pOld || !*ppExprOrig || strcmp(pchExprTargetString,pchExprOrigString));
		estrDestroy(&pchExprTargetString);
		estrDestroy(&pchExprOrigString);

		return iRet;
	}
}

static void PTUpdateFieldDirty(SA_PARAM_NN_VALID PTField *pfield,bool bDirty)
{
	// Update color and dirty tracking
	if(bDirty!=pfield->bDirty)
	{
		pfield->bDirty = bDirty;
		if(pfield->pUIText)
			ui_SetChanged(UI_WIDGET(pfield->pUIText), bDirty);
		if(pfield->pUICombo)
			ui_SetChanged(UI_WIDGET(pfield->pUICombo), bDirty);
		if(pfield->pUIMessage)
			ui_SetChanged(UI_WIDGET(pfield->pUIMessage),bDirty);
	}

	if(bDirty)
	{
		eaPushUnique(&pfield->pdoc->ppDirty,pfield);
	}
	else
	{
		eaFindAndRemove(&pfield->pdoc->ppDirty,pfield);
	}

	// Flag doc for saving if it needs
	PTUpdateDocDirty(pfield->pdoc);
}
static bool PTTextFieldUpdateInt(SA_PARAM_NN_VALID PTField *pfield, int i)
{
	int *piTarget = (int*)((char*)(pfield->pNew) + pfield->offset);
	int *piOrig = (int*)((char*)(pfield->pOld) + pfield->offset);

	if (!pfield->bRevert && (*piTarget != i) && !PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		char *pchI = NULL;
		i = pfield->pOld ? *piOrig : *piTarget;
		estrStackCreate(&pchI);
		if (pfield->pEnum) {
			estrPrintf(&pchI,"%s",StaticDefineIntRevLookup(pfield->pEnum, i));
		} else {
			estrPrintf(&pchI,"%d",i);
		}
		ui_TextEntrySetText(pfield->pUIText, pchI);
		estrDestroy(&pchI);
		pfield->bRevert = false;
	}

	*piTarget = i;
	return !pfield->pOld || (*piTarget != *piOrig);
}
static bool PTTextFieldUpdateF32(SA_PARAM_NN_VALID PTField *pfield, F32 f)
{
	F32 *pfTarget = (F32*)((char*)(pfield->pNew) + pfield->offset);
	F32 *pfOrig = (F32*)((char*)(pfield->pOld) + pfield->offset);

	if (!pfield->bRevert && (*pfTarget != f) && !PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		char *pchF = NULL;
		f = pfield->pOld ? *pfOrig : *pfTarget;
		estrStackCreate(&pchF);
		estrPrintf(&pchF,"%g",f);
		ui_TextEntrySetText(pfield->pUIText, pchF);
		estrDestroy(&pchF);
		pfield->bRevert = false;
	}

	*pfTarget = f;
	return !pfield->pOld || (*pfTarget != *pfOrig);
}

static void PTCheckFieldChanged(SA_PARAM_NN_VALID UICheckButton *pcheck, SA_PARAM_NN_VALID PTField *pfield)
{
	bool bDirty;
	bool b = pcheck->state;
	bool *pbTarget = (bool*)((char*)(pfield->pNew) + pfield->offset);
	bool *pbOrig = (bool*)((char*)(pfield->pOld) + pfield->offset);

	if (!pfield->bRevert && ((b && !*pbTarget) || (!b && *pbTarget)) && !PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		b = pfield->pOld ? *pbOrig : *pbTarget;
		pcheck->state = b;
		pfield->bRevert = false;
	}

	*pbTarget = b;

	bDirty = !pfield->pOld || (*pbTarget != *pbOrig);
	PTUpdateFieldDirty(pfield,bDirty);
}

static void PTFlagFieldChanged(SA_PARAM_NN_VALID UIComboBox *pcombo, SA_PARAM_NN_VALID PTField *pfield)
{
	bool bDirty;
	S32 i = pfield->pUIFlagBox->iSelected;
	S32 *pTarget = (S32*)((char*)(pfield->pNew)+pfield->offset);
	S32 *pOrig = (S32*)((char*)(pfield->pOld)+pfield->offset);

	if (!pfield->bRevert && !PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		i = pfield->pOld ? *pOrig : *pTarget;

		ui_ComboBoxSetSelected(pcombo, i);
		pfield->bRevert = false;
	}

	*pTarget = i;

	bDirty = !pfield->pOld || (*pTarget != *pOrig);
	PTUpdateFieldDirty(pfield,bDirty);
}

static void PTUpdateNodeRefs(SA_PARAM_NN_VALID PTEditDoc *pdoc, SA_PARAM_NN_STR const char *pchOld, SA_PARAM_OP_STR const char *pchGroupName, SA_PARAM_NN_VALID PTNodeDef *pNewDef)
{
	int i, j;
	size_t offset = 0;
	char *pchNewName = NULL;
	char *pchOldName = strdup(pchOld);
	PTNodeDef *pNodeDef;

	if(!pchGroupName)
	{
		int n;

		for(n=0;n<eaSize(&pdoc->pDefNew->ppGroups);n++)
		{
			for(i=0;i<eaSize(&pdoc->pDefNew->ppGroups[n]->ppNodes);i++)
			{
				if(pdoc->pDefNew->ppGroups[n]->ppNodes[i] == pNewDef)
				{
					pchGroupName = pdoc->pDefNew->ppGroups[n]->pchGroup;
					break;
				}
			}
			if(pchGroupName)
				break;
		}
	}

	//Change internal name
	estrCreate(&pchNewName);
	estrPrintf(&pchNewName,"%s.%s.%s",pdoc->pDefNew->pchName,pchGroupName,pNewDef->pchName);
	pNewDef->pchNameFull = allocAddString(pchNewName);
	estrDestroy(&pchNewName);

	FORALL_PARSETABLE(parse_PTPurchaseRequirements, i)
	{
		if(!stricmp("hNode",parse_PTPurchaseRequirements[i].name))
		{
			offset = parse_PTPurchaseRequirements[i].storeoffset;
			break;
		}
	}

	for(i=0;i<eaSize(&pdoc->ppFields);i++)
	{
		if(pdoc->ppFields[i]->ptable == parse_PTPurchaseRequirements)
		{
			if(pdoc->ppFields[i]->offset == offset)
			{
				if(stricmp(pchOldName,ui_TextEntryGetText(pdoc->ppFields[i]->pUIText))==0)
				{
					ui_TextEntrySetTextAndCallback(pdoc->ppFields[i]->pUIText,pNewDef->pchNameFull);
				}  
			}
		}
	}

	if (pdoc->pDefNew->bIsTalentTree)
	{
		UIButton *pButton;
		// Update all node requirements
		for(i = 0; i < eaSize(&pdoc->pDefNew->ppGroups); i++)
		{
			for (j = 0; j < eaSize(&pdoc->pDefNew->ppGroups[i]->ppNodes); j++)
			{
				pNodeDef = pdoc->pDefNew->ppGroups[i]->ppNodes[j];
				if (IS_HANDLE_ACTIVE(pNodeDef->hNodeRequire) && 
					stricmp(REF_STRING_FROM_HANDLE(pNodeDef->hNodeRequire), pchOldName) == 0)
				{
					REMOVE_HANDLE(pNodeDef->hNodeRequire);
					SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict, pNewDef->pchNameFull, pNodeDef->hNodeRequire);
				}
			}
		}

		// Find the button and update the tooltip
		if (pdoc->bTalentTreeGridCreated)
		{
			pButton = (UIButton *)ui_WidgetFindChild(UI_WIDGET(pdoc->pScrollArea), PTGetTalentTreeButtonName(pNewDef->iUIGridRow, pNewDef->iUIGridColumn));
			devassert(pButton);
			if (pButton)
			{
				ui_WidgetSetTooltipString(UI_WIDGET(pButton), PTTalentTreeGetNodeName(pNewDef));
			}
		}
	}

	free(pchOldName);
}

static void PTUpdateGroupRefs(SA_PARAM_NN_VALID PTEditDoc *pdoc, SA_PARAM_NN_STR const char *pchOld, SA_PARAM_NN_VALID PTGroupDef *pNewDef)
{
	int i;
	size_t offset = 0;
	char *pchNewName = NULL;
	char *pchOldName = strdup(pchOld);

	//Change internal name
	StructFreeString(pNewDef->pchNameFull);

	estrCreate(&pchNewName);
	estrPrintf(&pchNewName,"%s.%s",pdoc->pDefNew->pchName,pNewDef->pchGroup);
	pNewDef->pchNameFull = StructAllocString(pchNewName);
	estrDestroy(&pchNewName);

	FORALL_PARSETABLE(parse_PTPurchaseRequirements, i)
	{
		if(!stricmp("hGroup",parse_PTPurchaseRequirements[i].name))
		{
			offset = parse_PTPurchaseRequirements[i].storeoffset;
			break;
		}
	}

	for(i=0;i<eaSize(&pdoc->ppFields);i++)
	{
		if(pdoc->ppFields[i]->ptable == parse_PTPurchaseRequirements)
		{
			if(pdoc->ppFields[i]->offset == offset)
			{
				if(stricmp(pchOldName,ui_TextEntryGetText(pdoc->ppFields[i]->pUIText))==0)
				{
					ui_TextEntrySetTextAndCallback(pdoc->ppFields[i]->pUIText,pNewDef->pchNameFull);
				}
			}
		}
	}

	for(i=0;i<eaSize(&pNewDef->ppNodes);i++)
	{
		PTUpdateNodeRefs(pdoc,pNewDef->ppNodes[i]->pchNameFull,pNewDef->pchGroup,pNewDef->ppNodes[i]);
	}

	free(pchOldName);
}

static void PTUpdateTreeRefs(SA_PARAM_NN_VALID PTEditDoc *pdoc)
{
	int i;

	for(i=0;i<eaSize(&pdoc->pDefNew->ppGroups);i++)
	{
		PTUpdateGroupRefs(pdoc,pdoc->pDefNew->ppGroups[i]->pchNameFull,pdoc->pDefNew->ppGroups[i]);
	}
}

static bool PTMessageFieldPreChanged(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, SA_PARAM_NN_VALID PTField *pField)
{
	bool bDirty = false;
	const Message *pMessage = ui_MessageEntryGetMessage(pMsgEntry);

	bDirty = PTMessageFieldUpdate(pField,pMessage);

	PTUpdateFieldDirty(pField,bDirty);
	return bDirty;
}

static void PTMessageFieldChanged(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, SA_PARAM_NN_VALID PTField *pField)
{
	bool bDirty = false;
	const Message *pMessage = ui_MessageEntryGetMessage(pMsgEntry);

	bDirty = PTMessageFieldUpdate(pField,pMessage);

	PTUpdateFieldDirty(pField,bDirty);
}

static void PTCloneFieldChange(SA_PARAM_NN_VALID PTField *pField, SA_PARAM_NN_VALID PTNodeEdit *pNodeEdit)
{
	//Find a clone
	PTNodeDef *pNodeDef = (PTNodeDef*)pField->pNew;

	if(IS_HANDLE_ACTIVE(pNodeDef->hNodeClone))
	{
		int i;
		PTNodeDef *pCloneDef = GET_REF(pNodeDef->hNodeClone);
		
		for(i=eaSize(&pNodeEdit->ppRankEdits)-1;i>=0;i--)
		{
			PTDeleteRank(pNodeEdit->ppRankEdits[i]);
		}

		for(i=eaSize(&pNodeEdit->ppEnhancementEdits)-1;i>=0;i--)
		{
			PTDeleteEnhancement(pNodeEdit->ppEnhancementEdits[i]);
		}

		if(pCloneDef)
		{
			for(i=0;i<eaSize(&pCloneDef->ppRanks);i++)
			{
				PTNodeRankDef *pNewRank = StructCreate(parse_PTNodeRankDef);

				StructCopyAll(parse_PTNodeRankDef,pCloneDef->ppRanks[i],pNewRank);
				eaPush(&pNodeDef->ppRanks,pNewRank);

				PTCreateRankExp(pNewRank,NULL,pNodeEdit);
			}

			for(i=0;i<eaSize(&pCloneDef->ppEnhancements);i++)
			{
				PTNodeEnhancementDef *pNewEnh = StructCreate(parse_PTNodeEnhancementDef);

				StructCopyAll(parse_PTNodeEnhancementDef,pCloneDef->ppEnhancements[i],pNewEnh);
				eaPush(&pNodeDef->ppEnhancements,pNewEnh);

				PTCreateEnhancementExp(pNewEnh,NULL,pNodeEdit);
			}
		}

		pNodeEdit->pNewRankBtn->widget.state = kWidgetModifier_Inactive;
		pNodeEdit->pNewEnhancmentButton->widget.state = kWidgetModifier_Inactive;
		pNodeEdit->pDefaultEnhButton->widget.state = kWidgetModifier_Inactive;
	}
	else
	{
		pNodeEdit->pNewRankBtn->widget.state = kWidgetModifier_None;
		pNodeEdit->pNewEnhancmentButton->widget.state = kWidgetModifier_None;
		pNodeEdit->pDefaultEnhButton->widget.state = kWidgetModifier_None;
	}

	PTResizeGroupWin_All(pNodeEdit->pdoc);
}
static void PTTextFieldChanged(SA_PARAM_NN_VALID UITextEntry *ptext, SA_PARAM_NN_VALID PTField *pfield)
{
	bool bDirty = false;
	char *pchOld = NULL;

	if(pfield->bisName)
	{
		const char *val = ui_TextEntryGetText(ptext);
		if(val && strchr(val, ' ')) {
			int i;
			char pcNewName[256];
			strcpy(pcNewName, val);
			for ( i=0; i < 256 && pcNewName[i]; i++ ) {
				if(pcNewName[i] == ' ')
					pcNewName[i] = '_';
			}
			ui_TextEntrySetText(ptext, pcNewName);
		}
	}

	if(pfield->bisTitle)
	{
		if(pfield->ptable == parse_PTNodeDef)
			pchOld = strdup(((PTNodeDef*)pfield->pNew)->pchNameFull);
		else if(pfield->ptable == parse_PTGroupDef)
			pchOld = strdup(((PTGroupDef *)pfield->pNew)->pchNameFull);
		else if(pfield->ptable == parse_PowerTreeDef)
			pchOld = strdup(((PowerTreeDef *)pfield->pNew)->pchName);
	}
	if(TOK_GET_TYPE(pfield->type) == TOK_STRING_X)
	{
		bDirty = PTTextFieldUpdateString(pfield, ui_TextEntryGetText(ptext));
	}
	else if(TOK_GET_TYPE(pfield->type) == TOK_STRUCT_X)
	{
		bDirty = PTTextFieldUpdateExpression(pfield,ui_TextEntryGetText(ptext));
	}
	else if(TOK_GET_TYPE(pfield->type) == TOK_F32_X)
	{
		bDirty = PTTextFieldUpdateF32(pfield,atof(ui_TextEntryGetText(ptext)));
	}
	else if(TOK_GET_TYPE(pfield->type) == TOK_INT_X)
	{
		int iValue;
		if (pfield->pEnum)
		{
			iValue = StaticDefineIntGetInt(pfield->pEnum, ui_TextEntryGetText(ptext));
		}
		else
		{
			iValue = atoi(ui_TextEntryGetText(ptext));
		}
		bDirty = PTTextFieldUpdateInt(pfield,iValue);
		s_bRedrawGroups = true;
	}
	else if(TOK_GET_TYPE(pfield->type) == TOK_REFERENCE_X)
	{
		bDirty = PTTextFieldUpdateReference(pfield,ui_TextEntryGetText(ptext));
		s_bRedrawGroups = true;
	}
	else //Type not found! Error Message!
	{
		Errorf("Error in PowerTreeEditor.c\n\nTOK_GET_TYPE not found!\n\n(%s)",ui_TextEntryGetText(ptext));	
	}

	PTUpdateFieldDirty(pfield,bDirty);

	if(pfield->fChangeFunc)
		pfield->fChangeFunc(pfield,pfield->pChangeFuncData);

	if(pfield->bisTitle)
	{
		if(pfield->pexp)
		{
			if(pfield->ptable == parse_PTNodeEnhancementDef)
			{
				char *pchDisplay;
				estrCreate(&pchDisplay);
				estrPrintf(&pchDisplay,"(E) - %s",ui_TextEntryGetText(ptext));
				ui_WidgetSetTextString(UI_WIDGET(pfield->pexp), pchDisplay);
				estrDestroy(&pchDisplay);
			}
			else if(pfield->ptable == parse_PTNodeRankDef)
			{
				char *pchDisplay;
				const char *cpchEntry = ui_TextEntryGetText(ptext);
				estrCreate(&pchDisplay);
				estrPrintf(&pchDisplay,"(R) - %s",cpchEntry && *cpchEntry ? cpchEntry : "Empty");
				ui_WidgetSetTextString(UI_WIDGET(pfield->pexp), pchDisplay);
				estrDestroy(&pchDisplay);
			}
			else
				ui_WidgetSetTextString(UI_WIDGET(pfield->pexp), ui_TextEntryGetText(ptext));

			if(pfield->ptable == parse_PTNodeDef && IS_HANDLE_ACTIVE(((PTNodeDef *)pfield->pNew)->hNodeClone))
			{
				char *pchName;
				estrStackCreate(&pchName);
				estrPrintf(&pchName,"%s (%s)",ui_TextEntryGetText(ptext),REF_STRING_FROM_HANDLE(((PTNodeDef *)pfield->pNew)->hNodeClone));
				ui_WidgetSetTextString(UI_WIDGET(pfield->pexp), pchName);
				estrDestroy(&pchName);
			}

		}

		if(pfield->pbtn)
		{
			int height;
			height = pfield->pbtn->widget.height;
			ui_ButtonSetTextAndResize(pfield->pbtn, ui_TextEntryGetText(ptext));
			pfield->pbtn->widget.width += 5;
			pfield->pbtn->widget.height = height;
		}

		if(pchOld)
		{
			if(pfield->ptable == parse_PTNodeDef)
			{
				PTUpdateNodeRefs(pfield->pdoc,pchOld,NULL,pfield->pNew);
			}
			else if(pfield->ptable == parse_PTGroupDef)
			{
				PTUpdateGroupRefs(pfield->pdoc,pchOld,pfield->pNew);
			}
			else if(pfield->ptable == parse_PowerTreeDef)
			{
				PTUpdateTreeRefs(pfield->pdoc);
			}

			free(pchOld);
		}
	}

	ui_WidgetSetTooltipString((UIWidget*)pfield->pUIText, ui_TextEntryGetText(pfield->pUIText));
}
static void PTTextAreaFieldChanged(SA_PARAM_NN_VALID UITextArea* ptext, SA_PARAM_NN_VALID PTField *pfield)
{
	bool bDirty;

	bDirty = PTTextFieldUpdateString(pfield, ui_TextAreaGetText(ptext));

	PTUpdateFieldDirty(pfield,bDirty);
}
static void PTComboEnumFieldChanged(SA_PARAM_NN_VALID UIComboBox *pcombo, int i, SA_PARAM_NN_VALID PTField *pfield)
{
	static bool bRecursion = false; // calls to set selected call the changed func (this)

	bool bDirty;
	int *piTarget = (int*)((char*)(pfield->pNew) + pfield->offset);
	int *piOrig = (int*)((char*)(pfield->pOld) + pfield->offset);

	if(bRecursion)
		return;

	if (!pfield->bRevert && !PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		i = pfield->pOld ? *piOrig : *piTarget;
		bRecursion = true;
		ui_ComboBoxSetSelectedEnumAndCallback(pcombo,i);
		bRecursion = false;
		pfield->bRevert = false;
	}

	*piTarget = i;

	bDirty = !pfield->pOld || (*piTarget != *piOrig);
	PTUpdateFieldDirty(pfield,bDirty);
}
static void PTComboFieldChanged(UIComboBox *pcombo, PTField *pfield)
{
	bool bDirty;
	int *piTarget = (int*)((char*)(pfield->pNew) + pfield->offset);
	int *piOrig = (int*)((char*)(pfield->pOld) + pfield->offset);

	if (!pfield->bRevert && !PTCanEdit(pfield->pdoc))
		pfield->bRevert = true;

	if(pfield->bRevert)
	{
		pfield->pUICombo->iSelected = pfield->pOld ? *piOrig : *piTarget;
		pfield->bRevert = false;
	}

	*piTarget = pfield->pUICombo->iSelected;

	bDirty = !pfield->pOld || (*piTarget != *piOrig);
	PTUpdateFieldDirty(pfield,bDirty);
}
static void PTUpdateField(PTField *pfield,bool brevert)
{
	if(brevert) pfield->bRevert = true;
	if(pfield->pUIText)
		PTTextFieldChanged(pfield->pUIText,pfield);
	if(pfield->pUICombo)
	{
		if(pfield->bEnumCombo)
		{
			PTComboEnumFieldChanged(pfield->pUICombo,ui_ComboBoxGetSelectedEnum(pfield->pUICombo),pfield);
		}
		else
		{
			PTComboFieldChanged(pfield->pUICombo,pfield);
		}
	}
	if(pfield->pUICheck)
		PTCheckFieldChanged(pfield->pUICheck,pfield);
	if(pfield->pUIFlagBox)
		PTFlagFieldChanged(pfield->pUIFlagBox,pfield);
}
static void PTUpdateAllFields(PTEditDoc *pdoc)
{
	int i;
	char **ppText = NULL;
	for (i=0;i<eaSize(&pdoc->ppFields);i++)
	{
		if(pdoc->ppFields[i]->pUIText && TOK_GET_TYPE(pdoc->ppFields[i]->type) == TOK_STRING_X)
		{
			ppText = (char **)((char*)(pdoc->ppFields[i]->pNew) + pdoc->ppFields[i]->offset);
			ui_TextEntrySetTextAndCallback(pdoc->ppFields[i]->pUIText,*ppText);
		}
			PTUpdateField(pdoc->ppFields[i],false);
	}
}


static void PTField_AddFieldChangeCallback(PTField *pField, PTFieldChangeFunction fieldchancefunc, UserData pData)
{
	pField->fChangeFunc = fieldchancefunc;
	pField->pChangeFuncData = pData;
}

static PTField *PTAddEnumComboField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
									ParseTable *ptable, char *pchField, char *pchAlias, bool bLabel, StaticDefineInt *pEnum, 
									int x, int y, int width, bool bVertical, UIMenu *pMenu)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,NULL,pchAlias,bLabel,NULL,&x,&y,width,bVertical,false,pMenu);

	if(!pfield)
		return NULL;

	// Creates the combo box, sets up its callback, and then uses the callback to init
	pfield->pUICombo = ui_ComboBoxCreate(x,y,width,NULL,NULL,NULL);
	ui_ComboBoxSetEnum(pfield->pUICombo,pEnum,PTComboEnumFieldChanged,pfield);

	pfield->bEnumCombo = true;

	pfield->bRevert = true;
	PTUpdateField(pfield,true);


	if(pwin)
		ui_PaneAddChild(pwin,pfield->pUICombo);
	else if(pexp)
		ui_ExpanderAddChild(pexp,&pfield->pUICombo->widget);

	return pfield;
}
static PTField *PTAddComboField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
								ParseTable *ptable, char *pchField, char *pchAlias, bool bLabel, SA_PARAM_OP_VALID UIModel uiModel, 
								int x, int y, int width, bool bVertical, UIMenu *pMenu)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,NULL,pchAlias,bLabel,NULL,&x,&y,width,bVertical,false,pMenu);

	if(!pfield)
		return NULL;

	// Creates the combo box, sets up its callback, and then uses the callback to init
	pfield->pUICombo = ui_ComboBoxCreate(x,y,width,NULL,uiModel,NULL);
	ui_ComboBoxSetSelectedCallback(pfield->pUICombo,PTComboFieldChanged,pfield);
	
	
	pfield->bRevert = true;
	PTUpdateField(pfield,true);


	if(pwin)
		ui_PaneAddChild(pwin,pfield->pUICombo);
	else if(pexp)
		ui_ExpanderAddChild(pexp,&pfield->pUICombo->widget);

	return pfield;
}

static PTField *PTAddCheckField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
								ParseTable *ptable, char *pchField, char *pchAlias, bool bLabel, 
								int x, int y, int width, bool bVertical, UIMenu *pMenu, char *pchTooltip)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,NULL,pchAlias,bLabel,NULL,&x,&y,width,bVertical,false,pMenu);

	if(!pfield)
		return NULL;

	// Creates the check button, sets up its callback, and then uses the callback to init
	pfield->pUICheck = ui_CheckButtonCreate(x,y," ",false);
	pfield->pUICheck->widget.width = width;
	ui_CheckButtonSetToggledCallback(pfield->pUICheck,PTCheckFieldChanged,pfield);

	// Set the tooltip
	if (pchTooltip)
		ui_WidgetSetTooltipString((UIWidget*)pfield->pUICheck, pchTooltip);

	PTUpdateField(pfield,true);

	if(pwin)
		ui_PaneAddChild(pwin,pfield->pUICheck);
	else if(pexp)
		ui_ExpanderAddChild(pexp,&pfield->pUICheck->widget);

	return pfield;
}
static PTField *PTAddFlagField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
							   ParseTable *ptable, char *pchField, char *pchAlias, bool bLabel, StaticDefineInt *pInt,
							   int x, int y, int width, bool bVertical, UIMenu *pMenu)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,NULL,pchAlias,bLabel,NULL,&x,&y,width,bVertical,0,pMenu);

	if(!pfield)
		return NULL;

	pfield->pUIFlagBox = ui_FlagComboBoxCreate(pInt);
	pfield->pUIFlagBox->widget.width = width;
	ui_ComboBoxSetSelectedCallback(pfield->pUIFlagBox,PTFlagFieldChanged,pfield);
	ui_WidgetSetPosition((UIWidget *)pfield->pUIFlagBox,x,y);

	PTUpdateField(pfield,true);

	if(pwin)
		ui_PaneAddChild(pwin,pfield->pUIFlagBox);
	else if(pexp)
		ui_ExpanderAddChild(pexp,&pfield->pUIFlagBox->widget);

	return pfield;
}

static PTField *PTAddMessageField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew,
								  ParseTable *ptable, char *pchField, char *pchAlias, bool bLabel,
								  int x, int y, int width, bool brevert, bool bVertical, bool bisTitle, UIMenu *pMenu)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,NULL,pchAlias,bLabel,NULL,&x,&y,width,bVertical,bisTitle,pMenu);
	DisplayMessage *pMessage;

	if(!pfield)
		return NULL;

	pMessage = (DisplayMessage *)((char *)pNew + pfield->offset);


	pfield->pUIMessage = ui_MessageEntryCreate(pMessage->pEditorCopy,x,y,width);
	ui_MessageEntrySetPreChangedCallback(pfield->pUIMessage,PTMessageFieldPreChanged,pfield);
	ui_MessageEntrySetChangedCallback(pfield->pUIMessage,PTMessageFieldChanged,pfield);

	if(pwin)
		ui_PaneAddChild(pwin,UI_WIDGET(pfield->pUIMessage));
	if(pexp)
		ui_ExpanderAddChild(pexp,UI_WIDGET(pfield->pUIMessage));

	return pfield;
}

static PTField *PTAddTextFieldEnumCombo(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
									    ParseTable *ptable, char *pchField, char *pchAlias, bool bLabel, StaticDefineInt *pEnum, UIButton *pBtn,
									    int x, int y, int width, bool bVertical, bool bisTitle, bool bSort, UIMenu *pMenu)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,NULL,pchAlias,bLabel,pBtn,&x,&y,width,bVertical,bisTitle,pMenu);
	UIComboBox *pSubCombo;

	if(!pfield)
		return NULL;

	if(TOK_GET_TYPE(pfield->type) == TOK_INT_X)
	{
		char achEnum[100];
		int *pData = (int*)((char *)pfield->pNew + pfield->offset);
		sprintf(achEnum,"%s",StaticDefineIntRevLookup(pEnum, *pData));
		pfield->pUIText = ui_TextEntryCreate(achEnum,x,y);
	}
	else
	{
		Errorf("Format Not Recognized for PEAddTextFieldEnumCombo");
		pfield->pUIText = ui_TextEntryCreate("",x,y);
	}

	((UIWidget *)pfield->pUIText)->tickF = PTTextFieldTick;

	pfield->pEnum = pEnum;
	pfield->pUIText->widget.width = width;
	ui_TextEntrySetChangedCallback(pfield->pUIText,PTTextFieldChanged,pfield);

	pSubCombo = ui_ComboBoxCreate(0,0,100,NULL,NULL,NULL);
	DefineFillAllKeysAndValues(pEnum, &pfield->ppchModel, NULL);
	ui_ComboBoxSetModel(pSubCombo, NULL, &pfield->ppchModel);
	pSubCombo->openedWidth = 400;
	ui_TextEntrySetComboBox(pfield->pUIText,pSubCombo);
	pfield->pUIText->autoComplete = true;
	PTUpdateField(pfield,false);

	if (bSort)
		eaQSort(pfield->ppchModel, strCmp);

	if(pwin)
		ui_PaneAddChild(pwin,pfield->pUIText);
	if(pexp)
		ui_ExpanderAddChild(pexp,(UIWidget *)pfield->pUIText);
		
	return pfield;
}

static PTField *PTAddTextField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
							   ParseTable *ptable, char *pchField, const char *pchDictName, char *pchAlias, bool bLabel, UIComboBox *pSubCombo, UIButton *pBtn,
							   int x, int y, int width, bool brevert, bool bVertical, bool bisTitle, UIMenu *pMenu)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,pchDictName,pchAlias,bLabel,pBtn,&x,&y,width,bVertical,bisTitle,pMenu);

	if(!pfield)
		return NULL;

	// Creates the text field, sets up its callback, and then uses the callback to init
	if(brevert)
		pfield->pUIText = ui_TextEntryCreate("",x,y);
	else
		if(TOK_GET_TYPE(pfield->type) == TOK_STRING_X)
		{
			pfield->pUIText = ui_TextEntryCreate(*(char**)((char*)(pfield->pNew) + pfield->offset),x,y);
		}
		else if(TOK_GET_TYPE(pfield->type) == TOK_STRUCT_X)
		{
			Expression **ppExprTarget = (Expression**)((char*)(pfield->pNew) + pfield->offset);
			if (!exprIsEmpty(*ppExprTarget))
				pfield->pUIText = ui_TextEntryCreate(exprGetCompleteString(*ppExprTarget),x,y);
			else
				pfield->pUIText = ui_TextEntryCreate("",x,y);
		}
		else if(TOK_GET_TYPE(pfield->type) == TOK_INT_X)
		{
			char achNum[100];
			int *pData = (int*)((char *)pfield->pNew + pfield->offset);
			sprintf(achNum,"%d",*pData);

			pfield->pUIText = ui_TextEntryCreate(achNum,x,y);
		}
		else if(TOK_GET_TYPE(pfield->type) == TOK_F32_X)
		{
			char achNum[100];
			F32 *pData = (F32*)((char *)pfield->pNew + pfield->offset);
			sprintf(achNum,"%g",*pData);

			pfield->pUIText = ui_TextEntryCreate(achNum,x,y);
		}
		else if(TOK_GET_TYPE(pfield->type) == TOK_REFERENCE_X)
		{
			FakeRefHandle* ph = (FakeRefHandle*)((char*)(pfield->pNew) + pfield->offset);
			if(pfield->pNew != NULL && IS_HANDLE_ACTIVE(*ph))
			{
				pfield->pUIText = ui_TextEntryCreate(REF_STRING_FROM_HANDLE(*ph),x,y);
			}
			else
			{
				pfield->pUIText = ui_TextEntryCreate("",x,y);
			}
		}
		else
		{
			Errorf("Format Not Recognized for PEAddTextField");
			pfield->pUIText = ui_TextEntryCreate("",x,y);
		}
		((UIWidget *)pfield->pUIText)->tickF = PTTextFieldTick;


		pfield->pUIText->widget.width = width;
		ui_TextEntrySetChangedCallback(pfield->pUIText,PTTextFieldChanged,pfield);

		//((UIWidget*)pfield->pUIText)->tickF = PETextFieldTick;
		if(pSubCombo) 
		{
			ui_TextEntrySetComboBox(pfield->pUIText,pSubCombo);
			pfield->pUIText->autoComplete = true;
		}
		PTUpdateField(pfield,brevert);

		if(pwin)
			ui_PaneAddChild(pwin,pfield->pUIText);
		if(pexp)
			ui_ExpanderAddChild(pexp,(UIWidget *)pfield->pUIText);
		
		return pfield;
}
static PTField *PTAddTextAreaField(UIPane *pwin, UIExpander *pexp, PTEditDoc *pdoc, void *pOld, void *pNew, 
								   ParseTable *ptable, char *pchField, char *pchAlias, bool bLabel,
								   int x, int y, int width, bool bVertical, bool bisTitle, UIMenu *pMenu)
{
	PTField *pfield = PTCreateBasicField(pwin,pexp,pdoc,pOld,pNew,ptable,pchField,NULL,pchAlias,bLabel,NULL,&x,&y,width,bVertical,bisTitle,pMenu);

	if(!pfield)
		return NULL;

	// Creates the text area, sets up its callback, and then uses the callback to init
	pfield->pUITextArea = ui_TextAreaCreate("");
	ui_WidgetSetPositionEx((UIWidget*)pfield->pUITextArea,x,y,0,0,UITopLeft);
	ui_WidgetSetDimensionsEx((UIWidget *)pfield->pUITextArea, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_TextAreaSetChangedCallback(pfield->pUITextArea,PTTextAreaFieldChanged,pfield);
	//((UIWidget*)pfield->pUIText)->tickF = PETextFieldTick;
	PTUpdateField(pfield,true);

	if(pwin)
		ui_PaneAddChild(pwin,pfield->pUITextArea);
	else if(pexp)
		ui_ExpanderAddChild(pexp,&pfield->pUITextArea->widget);

	return pfield;
}
static void PTRankResize(UIExpander *expand, PTNodeEdit *pNode)
{
	F32 aHeight;

	aHeight = pNode->pExpRanks->totalHeight;
	aHeight += pNode->pNewRankBtn->widget.y+pNode->pNewRankBtn->widget.height + 5;
	ui_WidgetSetPositionEx((UIWidget*)pNode->pExpRanks,10,pNode->pNewRankBtn->widget.y+pNode->pNewRankBtn->widget.height + 5,0,0,UITopLeft);
	ui_ExpanderSetHeight(pNode->pExp,aHeight);
	ui_ExpanderReflow(pNode->pExp);
	PTGroupResize(pNode->pGroup);
}

static void PTExpanderSetHighlight(UIExpander *expand)
{
	expand->widget.highlightPercent = 1.f;
}

static void PTExpanderDraw(UIExpander *expand, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(expand);
	ui_ExpanderDraw(expand, UI_PARENT_VALUES);
}


static void PTExpanderTick(UIExpander *expand, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(expand);

	if (expand->widget.highlightPercent >= 1.0)
	{
		ui_ScrollbarParentScrollTo(x+w,y+h);
		ui_ScrollbarParentScrollTo(x,y);
	}

	ui_ExpanderTick(expand, UI_PARENT_VALUES);
}

static void PTCreateEnhancementExp(PTNodeEnhancementDef *pNew, PTNodeEnhancementDef *pOrig, PTNodeEdit *pNode)
{
	PTField *pTempField;
	PTEnhancementEdit *pNewEnhancement = calloc(sizeof(*pNewEnhancement),1);
	ResourceDictionaryInfo *pIndex = resDictGetInfo("PowerDef");
	UIComboBox *pCombo;
	PowerDef *pPower = GET_REF(pNew->hPowerDef);
	int i;

	for(i=0; i<eaSize(&pIndex->ppInfos); ++i) {
		ResourceInfo *pInfo = pIndex->ppInfos[i];
		if (pInfo->resourceTags && strstri(pInfo->resourceTags, "Enhancement")) {
			eaPushUnique(&s_ppEnhancements,pInfo);
		}
	}

	pNewEnhancement->pDefNew = pNew;
	pNewEnhancement->pDefOrig = pOrig;
	pNewEnhancement->pExp = ui_ExpanderCreate(pPower ? pPower->pchName : "No Enhancement",95);
	pNewEnhancement->pExp->autoScroll = true;
	
	ui_WidgetSetContextCallback((UIWidget*)pNewEnhancement->pExp,PTExpandNodeRClick,pNode);

	pCombo = ui_ComboBoxCreateWithDictionary(0,0,100,"PTEnhTypeDef",parse_PTEnhTypeDef,"Name");
	pCombo->openedWidth = 400;

	pTempField = PTAddTextField(NULL,pNewEnhancement->pExp,pNode->pdoc,pOrig,pNew,parse_PTNodeEnhancementDef,"EnhType","PTEnhTypeDef","Enhancement Type",true,pCombo,NULL,10,5,150,false,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	pCombo = ui_ComboBoxCreate(0,0,100,parse_ResourceInfo,&s_ppEnhancements,"ResourceName");
	pCombo->openedWidth = 400;

	pTempField = PTAddTextField(NULL,pNewEnhancement->pExp,pNode->pdoc,pOrig,pNew,parse_PTNodeEnhancementDef,"Power","PowerDef","Enhancement",true,pCombo,NULL,10,25,150,false,false,true,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	
	pTempField = PTAddTextField(NULL,pNewEnhancement->pExp,pNode->pdoc,pOrig,pNew,parse_PTNodeEnhancementDef,"LevelMax",NULL,"Max Level",true,NULL,NULL,10,45,150,false,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	pTempField = PTAddTextField(NULL,pNewEnhancement->pExp,pNode->pdoc,pOrig,pNew,parse_PTNodeEnhancementDef,"Cost",NULL,NULL,true,NULL,NULL,10,65,150,false,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	ui_ExpanderGroupAddExpander(pNode->pExpRanks,pNewEnhancement->pExp);
	ui_ExpanderSetExpandCallback(pNewEnhancement->pExp,PTRankResize,pNode);

	pNewEnhancement->pNode = pNode;
	eaPush(&pNode->ppEnhancementEdits,pNewEnhancement);

	PTRankResize(pNewEnhancement->pExp,pNode); 
}


static void PTLevelFieldChangeCallback(PTField *pField, PTEditDoc *pdoc);

// Called whenever a powerdef is changed for a rank
static void PTOnRankPowerDefChange(PTField *pField, PTRankEdit *pRankEdit)
{
	PTEditDoc *pDoc;
	PTNodeEdit *pNodeEdit;
	devassert(pRankEdit);
	devassert(pRankEdit->pNode);

	pNodeEdit = pRankEdit->pNode;
	pDoc = pNodeEdit->pdoc;

	if (pDoc->pDefNew->bIsTalentTree &&
		eaSize(&pNodeEdit->ppRankEdits) > 0 &&
		pNodeEdit->ppRankEdits[0] == pRankEdit) // Only respect the first rank
	{
		// Update the icon for the talent tree button
		PTTalentTreeButtonUpdateIcon(pDoc, pNodeEdit->pDefNodeNew, PTTalentTreeGetNodeIconName(pNodeEdit->pDefNodeNew));
	}
}

#define PTEditorIsInPowerTreeMode(nodeorgroup) ((nodeorgroup)->iUIGridRow < 1)

static void PTCreateRankExp(PTNodeRankDef *pNew, PTNodeRankDef *pOld, PTNodeEdit *pNode)
{
	PTField *pTempField;
	PTRankEdit *pNewRank = calloc(sizeof(*pNewRank),1);
	UIComboBox *pCombo;
	PowerDef *pPower = GET_REF(pNew->hPowerDef);
	const char **ppch_PowerTableNames = NULL;
	S32 y;
	
	pCombo = ui_ComboBoxCreateWithGlobalDictionary(0,0,100,g_hPowerDefDict,"resourceName");
	pCombo->openedWidth = 400;
	 
	pNewRank->pDefRankNew = pNew;
	pNewRank->pDefRankOrig = pOld;
	pNewRank->pExp = ui_ExpanderCreate(pPower ? pPower->pchName : "No Power",PTEditorIsInPowerTreeMode(pNode->pDefNodeNew) ? 280 : 210);
	pNewRank->pExp->autoScroll = true;
	ui_WidgetSetContextCallback(UI_WIDGET(pNewRank->pExp),PTExpandNodeRClick,pNode);

	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld,pNew,parse_PTNodeRankDef,"Power","PowerDef","Power Def",true,pCombo,NULL,10,5,150,true,false,true,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	PTField_AddFieldChangeCallback(pTempField, PTOnRankPowerDefChange, pNewRank);

	pTempField = PTAddCheckField(NULL,pNewRank->pExp,pNode->pdoc,pOld,pNew,parse_PTNodeRankDef,"Empty","Empty",true,10,25,150,false,NULL,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);

	y = 45;

	pTempField = PTAddCheckField(NULL,pNewRank->pExp,pNode->pdoc,pOld,pNew,parse_PTNodeRankDef,"ForcedAutoBuy","Force AutoBuy",true,10,y,150,false,NULL,
		"AutoBuy this Rank even if it has a cost associated with it. Requires that the Node be AutoBuy as well.");
	eaPush(&pNode->pdoc->ppFields,pTempField);

	y+= 20;

	if (PTEditorIsInPowerTreeMode(pNode->pDefNodeNew))
	{
		pCombo = ui_ComboBoxCreate(0,0,100,NULL,&pNode->pdoc->ppchGroupNames,NULL);
		pCombo->openedWidth = 400;
		pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"hGroup",g_hPowerTreeGroupDefDict,"Group",true,pCombo,NULL,10,y,150,true,false,false,NULL);
		eaPush(&pNode->pdoc->ppFields,pTempField);
		if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
		pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"GroupRequired",NULL,NULL,false,NULL,NULL,315,y,25,true,false,false,NULL);
		eaPush(&pNode->pdoc->ppFields,pTempField);
		if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

		y += 20;

		pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"TableLevel",NULL,"Level",true,NULL,NULL,10,y,150,true,false,false,NULL);
		PTField_AddFieldChangeCallback(pTempField,PTLevelFieldChangeCallback,pNode->pdoc);
		eaPush(&pNode->pdoc->ppFields,pTempField);
		if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

		y += 20;

		pCombo = ui_ComboBoxCreate(0,0,200,parse_PowerTable,&g_PowerTables.ppPowerTables,"Name");
		pCombo->openedWidth = 400;
		pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"TableName","PowerTable","LevelTable",true,pCombo,NULL,10,y,150,true,false,false,NULL);
		eaPush(&pNode->pdoc->ppFields,pTempField);
		if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

		y += 20;
	}

	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"MinPointsSpentInThisTree",NULL,"Min Points Spent In Tree",true,NULL,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"MaxPointsSpentInThisTree",NULL,"Max Points Spent In Tree",true,NULL,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"MinPointsSpentInAnyTree",NULL,"Min Points Spent Any Tree",true,NULL,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"MaxPointsSpentInAnyTree",NULL,"Max Points Spent Any Tree",true,NULL,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld? pOld->pRequires : NULL,pNew->pRequires,parse_PTPurchaseRequirements,"PurchaseExpression",NULL,"Requirements",true,NULL,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld,pNew,parse_PTNodeRankDef,"Cost",NULL,NULL,true,NULL,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
		if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pCombo = ui_ComboBoxCreate(0,0,200,parse_PowerVar,&g_PowerVars.ppPowerVars,"Name");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld,pNew,parse_PTNodeRankDef,"CostVar","PowerVar","Cost Var",true,pCombo,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pCombo = ui_ComboBoxCreate(0,0,200,parse_PowerTable,&g_PowerTables.ppPowerTables,"Name");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld,pNew,parse_PTNodeRankDef,"CostTable","PowerTable","Cost Pool",true,pCombo,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	y += 20;

	pCombo = ui_ComboBoxCreateWithDictionary(0,0,100,g_hPowerTreeNodeDefDict,parse_PTNodeDef,"NameFull");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewRank->pExp,pNode->pdoc,pOld,pNew,parse_PTNodeRankDef,"TrainerUnlockNode","PowerTreeNodeDef","Trainer Unlock Node",true,pCombo,NULL,10,y,150,true,false,false,NULL);
	eaPush(&pNode->pdoc->ppFields,pTempField);
	if(IS_HANDLE_ACTIVE(pNode->pDefNodeNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;

	ui_ExpanderGroupAddExpander(pNode->pExpRanks,pNewRank->pExp);
	ui_ExpanderSetExpandCallback(pNewRank->pExp,PTRankResize,pNode);
	
	pNewRank->pNode = pNode;
	eaPush(&pNode->ppRankEdits,pNewRank);
	
	PTRankResize(pNewRank->pExp,pNode);
}

static void PTNewRank(UIButton *pButton,PTNodeEdit *pNode)
{
	PTNodeRankDef *pNewRank;

	if (!PTCanEdit(pNode->pdoc))
		return;

	pNewRank = StructCreate(parse_PTNodeRankDef);
	SET_HANDLE_FROM_STRING(g_hPowerDefDict,"",pNewRank->hPowerDef);
	pNewRank->iCost = 1;

	eaPush(&pNode->pDefNodeNew->ppRanks,pNewRank);
	
	PTCreateRankExp(pNewRank,NULL,pNode);
}

static void PTDeleteEnhancement(PTEnhancementEdit *pEnhancement);

static void PTDefEnhancement(PTNodeEdit *pNode)
{
	int i;
	PTNodeEnhancementDef **ppDefs = NULL;
	
	//Remove all old enhancements
	for(i=eaSize(&pNode->ppEnhancementEdits)-1;i>=0;i--)
	{
		PTDeleteEnhancement(pNode->ppEnhancementEdits[i]);
	}

	for(i=0;i<eaSize(&pNode->ppRankEdits);i++)
	{
		PTNodeEnhancementDef **ppTempDefs = NULL;
		int n;
		PowerDef *pDef = GET_REF(pNode->ppRankEdits[i]->pDefRankNew->hPowerDef);

		if(pDef)
			ppTempDefs = PTGetDefaultEnhancements(pDef);

		for(n=0;n<eaSize(&ppTempDefs);n++)
		{
			eaPushUnique(&ppDefs,ppTempDefs[n]);
		}
	}

	for(i=0;i<eaSize(&ppDefs);i++)
	{
		eaPush(&pNode->pDefNodeNew->ppEnhancements,ppDefs[i]);
		PTCreateEnhancementExp(ppDefs[i],NULL,pNode);
	}
	
}

bool PTDefEnhancementDialog(UIDialog *pDialog, UIDialogButton eButton, PTNodeEdit *pNode)
{
	if(eButton == kUIDialogButton_Ok)
		PTDefEnhancement(pNode);

	return true;
}

static void PTButtonDefEnhancement(UIButton *pButton,PTNodeEdit *pNode)
{
	if (!PTCanEdit(pNode->pdoc))
		return;

	if(eaSize(&pNode->ppEnhancementEdits))
		ui_WindowShow(UI_WINDOW(ui_DialogCreateEx("Get Default Enhancements","Getting Default Enhancements will remove all current enhancements for this node!!!",PTDefEnhancementDialog,pNode, NULL, 
		"Continue", kUIDialogButton_Ok, "Cancel", kUIDialogButton_Cancel, NULL)));
	else
		PTDefEnhancement(pNode);
}

static void PTNewEnhancement(UIButton *pButton,PTNodeEdit *pNode)
{
	PTNodeEnhancementDef *pNewEnhancement;

	if (!PTCanEdit(pNode->pdoc))
		return;

	pNewEnhancement = StructCreate(parse_PTNodeEnhancementDef);
	SET_HANDLE_FROM_STRING(g_hPowerDefDict,"",pNewEnhancement->hPowerDef);
	pNewEnhancement->iLevelMax = 1;

	eaPush(&pNode->pDefNodeNew->ppEnhancements,pNewEnhancement);

	PTCreateEnhancementExp(pNewEnhancement,NULL,pNode);
}

static void PTFindLevels(PTEditDoc *pEditDoc, int **ppiLevelsOut)
{
	powertree_FindLevelRequirements(pEditDoc->pDefNew, ppiLevelsOut);
}

static S32 CompareInts(const S32 *i, const S32 *j) { return *i - *j; }

static bool PTFindNodeInChain(PTNodeEdit *pNode)
{
	int i;

	for(i=0;i<eaSize(&pNode->pGroup->ppChains);i++)
	{
		PTDependencyChain *pChain = pNode->pGroup->ppChains[i];
		int n;

		for(n=0;n<eaSize(&pChain->ppNodes);n++)
		{
			if(pNode == pChain->ppNodes[n])
			{
				return true;
			}
		}
	}

	return false;
}

static int PTFindNodeInRankChain(PTNodeEdit *pNode)
{
	int i;

	for(i=0;i<eaSize(&pNode->pGroup->ppRankChains);i++)
	{
		PTNodeRankChain *pChain = pNode->pGroup->ppRankChains[i];
		
		if(pChain->pNode == pNode)
			return i;
	}

	return -1;
}

static void PTFillChainRecurse(PTGroupEdit *pGroup, PTDependencyChain *pChain, PTNodeEdit *pNode)
{
	int i;
	eaPush(&pChain->ppNodes,pNode);

	pChain->iWidth = max(pChain->iWidth,pNode->pBtn->widget.width);

	for(i=0;i<eaSize(&pGroup->ppNodeEdits);i++)
	{
		if(IS_HANDLE_ACTIVE(pGroup->ppNodeEdits[i]->pDefNodeNew->hNodeRequire))
		{
			if(pNode == PTFindNode(pGroup->pdoc,REF_STRING_FROM_HANDLE(pGroup->ppNodeEdits[i]->pDefNodeNew->hNodeRequire)))
			{
				if(PTFindNodeInChain(pGroup->ppNodeEdits[i])) //Solve infinite loop problem
					return;
				PTFillChainRecurse(pGroup,pChain,pGroup->ppNodeEdits[i]);
			}
		}
	}
}

static void PTFillDependancyChain(PTGroupEdit *pGroup)
{
	int i;

	eaDestroy(&pGroup->ppChains);
	pGroup->ppChains = NULL;


	for(i=0;i<eaSize(&pGroup->ppNodeEdits);i++)
	{
		if(IS_HANDLE_ACTIVE(pGroup->ppNodeEdits[i]->pDefNodeNew->hNodeRequire))
		{
			PTNodeEdit *pNode = PTFindNode(pGroup->pdoc,REF_STRING_FROM_HANDLE(pGroup->ppNodeEdits[i]->pDefNodeNew->hNodeRequire));

			if(pNode)
			{
				PTDependencyChain *pChain = malloc(sizeof(PTDependencyChain));
				pChain->ppNodes = NULL;
				pChain->iWidth = 0;

				eaPush(&pGroup->ppChains,pChain);

				PTFillChainRecurse(pGroup,pChain,pNode);
			}
		}
	}
}

static void PTRemoveRankChain(PTNodeRankChain *pChain)
{
	int i;

	for(i=0;i<eaSize(&pChain->ppRankLabels);i++)
	{
		ui_WindowRemoveChild(pChain->pNode->pGroup->pWin,pChain->ppRankLabels[i]);
	}
	eaDestroyEx(&pChain->ppRankLabels,ui_WidgetQueueFree);
}

static bool PTNodeRankChainFindLevel(PTNodeRankChain *pChain, int iLevel)
{
	if(ea32Find(&pChain->piLevels,iLevel) != -1)
		return true;
	
	if(pChain->pNode->ppRankEdits && pChain->pNode->ppRankEdits[0])
	{
		if(pChain->pNode->ppRankEdits[0]->pDefRankNew->pRequires->iTableLevel >= iLevel)
			return true;
	}
	return false;
}

static void PTFillNodeRankChain(PTGroupEdit *pGroup)
{
	int i;

	//Clear old rank chains
	eaDestroyEx(&pGroup->ppRankChains,PTRemoveRankChain);

	for(i=0;i<eaSize(&pGroup->ppNodeEdits);i++)
	{
		int r;
		PTNodeRankChain *pNewChain = NULL;

		if(eaSize(&pGroup->ppNodeEdits[i]->ppRankEdits) <= 1)
			continue;

		pNewChain = malloc(sizeof(PTNodeRankChain));
		pNewChain->ppRankLabels = NULL;
		pNewChain->piLevels = NULL;
		pNewChain->pNode = pGroup->ppNodeEdits[i];

		for(r=1;r<eaSize(&pGroup->ppNodeEdits[i]->ppRankEdits);r++)
		{
			char *estr;

			if(!PTNodeRankChainFindLevel(pNewChain,pGroup->ppNodeEdits[i]->ppRankEdits[r]->pDefRankNew->pRequires->iTableLevel))
			{
				UILabel *pNewLabel;
				estrCreate(&estr);
				estrPrintf(&estr,"%s (R:%d)",pGroup->ppNodeEdits[i]->pDefNodeNew->pchName,r+1);
				
				pNewLabel = ui_LabelCreate(estr,0,0); // Will position later
				eaPush(&pNewChain->ppRankLabels,pNewLabel);
				ea32PushUnique(&pNewChain->piLevels,pGroup->ppNodeEdits[i]->ppRankEdits[r]->pDefRankNew->pRequires->iTableLevel);

				ui_WindowAddChild(pGroup->pWin,pNewLabel);

				estrDestroy(&estr);
			}
		}

		if(eaSize(&pNewChain->ppRankLabels) > 0)
			eaPush(&pGroup->ppRankChains,pNewChain);
		else
			SAFE_FREE(pNewChain);
	}
}

static void PTResizeGroupWin(PTGroupEdit *pGroup)
{

	if(pGroup->ppLevelLables)
	{
		eaDestroyEx(&pGroup->ppLevelLables,ui_WidgetQueueFree);
		pGroup->ppLevelLables = NULL;
	}

	if(!pGroup->pdoc->bLevelView)
	{
		int x,y,c;

		x=5;
		for(c=0;c<eaSize(&pGroup->ppNodeEdits);c++)
		{
			if(pGroup->ppNodeEdits[c]->pBtn)
			{
				pGroup->ppNodeEdits[c]->pBtn->widget.x = x;
				x += pGroup->ppNodeEdits[c]->pBtn->widget.width + 5;
			}
		}
		x += 5;
		y = 75;

		if(pGroup->pWin)
		{
			pGroup->pWin->widget.width = x;
			pGroup->pWin->widget.height = y;
		}


		s_bRedrawGroups = true;
	}
	else
	{
		int i = 0;
		int *piLevels = NULL;
		int igroupWidth = 60;
		int iWidthChain=20;

		//Find height
		PTFindLevels(pGroup->pdoc,&piLevels);

		PTFillDependancyChain(pGroup);
		
		if(pGroup->pdoc->bExplodeRanks)
			PTFillNodeRankChain(pGroup);
		else if(pGroup->ppRankChains)
			eaDestroyEx(&pGroup->ppRankChains,PTRemoveRankChain);

		ea32QSort(piLevels,CompareInts);

		pGroup->pWin->widget.height = max(45,ea32Size(&piLevels) * 37 + 35);

		//Place chain nodes
		for(i=0;i<eaSize(&pGroup->ppChains);i++)
		{
			int n;
			for(n=0;n<eaSize(&pGroup->ppChains[i]->ppNodes);n++)
			{
				PTNodeEdit *pNode = pGroup->ppChains[i]->ppNodes[n];
				int iRankChain = PTFindNodeInRankChain(pNode);
				int iChain =0;

				//Check to see if this level exists already
				for(iChain=0;iChain<n;iChain++)
				{
					if(!pGroup->ppChains[i]->ppNodes[iChain]->pDefNodeNew->ppRanks && !pNode->pDefNodeNew->ppRanks)
					{
						iWidthChain += pGroup->ppChains[i]->ppNodes[iChain]->pBtn->widget.width;
						continue;
					}

					if(!pGroup->ppChains[i]->ppNodes[iChain]->pDefNodeNew->ppRanks || !pNode->pDefNodeNew->ppRanks)
						continue; 

					if(pGroup->ppChains[i]->ppNodes[iChain]->pDefNodeNew->ppRanks[0]->pRequires->iTableLevel == pNode->pDefNodeNew->ppRanks[0]->pRequires->iTableLevel)
						iWidthChain += pGroup->ppChains[i]->ppNodes[iChain]->pBtn->widget.width;
				}

				pNode->pBtn->widget.x = iWidthChain;

				if(pNode->pDefNodeNew->ppRanks)
				{
					int ilevel;
					for(ilevel = 0;ilevel<ea32Size(&piLevels);ilevel++)
					{
						if(pNode->pDefNodeNew->ppRanks[0]->pRequires->iTableLevel == piLevels[ilevel])
							pNode->pBtn->widget.y = ilevel * 37 + 20;
					}
				}
				else
					pNode->pBtn->widget.y = 20;

				if(iRankChain >= 0)
				{
					PTNodeRankChain *pChain = pGroup->ppRankChains[iRankChain];
					int r;
					for(r=0;r<eaSize(&pChain->ppRankLabels);r++)
					{
						int ilevel;
						pChain->ppRankLabels[r]->bWrap = true;
						pChain->ppRankLabels[r]->widget.width = pChain->pNode->pBtn->widget.width;

						pChain->ppRankLabels[r]->widget.x = iWidthChain;

						for(ilevel = 0;ilevel<ea32Size(&piLevels);ilevel++)
						{
							if(pChain->piLevels[r] == piLevels[ilevel])
								pChain->ppRankLabels[r]->widget.y = ilevel * 37 + 20;
						}

					}
				}
			}

			iWidthChain += pGroup->ppChains[i]->iWidth;
			igroupWidth = max(igroupWidth,iWidthChain);
		}

		//Place Rank Chain Nodes
		for(i=0;i<eaSize(&pGroup->ppRankChains);i++)
		{
			int r;
			int ilevel;
			PTNodeRankChain *pChain = pGroup->ppRankChains[i];

			if(PTFindNodeInChain(pChain->pNode))
				continue;

			for(ilevel = 0;ilevel<ea32Size(&piLevels);ilevel++)
			{
				if(pChain->pNode->ppRankEdits[0]->pDefRankNew->pRequires->iTableLevel == piLevels[ilevel])
					pChain->pNode->pBtn->widget.y = ilevel * 37 + 20;
			}

			pChain->pNode->pBtn->widget.x = iWidthChain;

			for(r=0;r<eaSize(&pChain->ppRankLabels);r++)
			{
				
				pChain->ppRankLabels[r]->bWrap = true;
				pChain->ppRankLabels[r]->widget.width = pChain->pNode->pBtn->widget.width;

				pChain->ppRankLabels[r]->widget.x = iWidthChain;

				for(ilevel = 0;ilevel<ea32Size(&piLevels);ilevel++)
				{
					if(pChain->piLevels[r] == piLevels[ilevel])
						pChain->ppRankLabels[r]->widget.y = ilevel * 37 + 20;
				}

			}

			iWidthChain += pChain->pNode->pBtn->widget.width;
			igroupWidth = max(igroupWidth,iWidthChain);
		}
		
		//Place nodes
		for(i=0;i<ea32Size(&piLevels);i++)
		{
			int n;
			char achLevel[10];
			UILabel *pLabel = NULL;
			int iWidth = iWidthChain;
			
			sprintf(achLevel,"%d",piLevels[i]);
			pLabel = ui_LabelCreate(achLevel,0,i*37+20);

			eaPush(&pGroup->ppLevelLables,pLabel);
			ui_WindowAddChild(pGroup->pWin,pLabel);

			for(n=0;n<eaSize(&pGroup->ppNodeEdits);n++)
			{

				if(PTFindNodeInChain(pGroup->ppNodeEdits[n]) || PTFindNodeInRankChain(pGroup->ppNodeEdits[n]) > -1)
					continue;

				if(eaSize(&pGroup->ppNodeEdits[n]->pDefNodeNew->ppRanks))
				{
					if(pGroup->ppNodeEdits[n]->pDefNodeNew->ppRanks[0]->pRequires->iTableLevel == piLevels[i])
					{
						pGroup->ppNodeEdits[n]->pBtn->widget.y = i * 37 + 20;
						pGroup->ppNodeEdits[n]->pBtn->widget.x = iWidth;
						iWidth += pGroup->ppNodeEdits[n]->pBtn->widget.width;
						igroupWidth = max(igroupWidth,iWidth);
					}
				}
				else if(piLevels[i] == 0)
				{
					pGroup->ppNodeEdits[n]->pBtn->widget.y = i * 35 + 20;
					pGroup->ppNodeEdits[n]->pBtn->widget.x = iWidth;
					iWidth += pGroup->ppNodeEdits[n]->pBtn->widget.width;
					igroupWidth = max(igroupWidth,iWidth);
				}
			}
		}
		
		pGroup->pWin->widget.width = igroupWidth;

		for(i=0;i<eaSize(&pGroup->ppNodeEdits);i++)
		{
			PTDrawNodeConnections(pGroup->ppNodeEdits[i],pGroup);
		}
	}
}

void PTResizeGroupWin_All(PTEditDoc *pEditDoc)
{
	int i=0;

	for(i=0;i<eaSize(&pEditDoc->ppGroupWindows);i++)
	{
		PTResizeGroupWin(pEditDoc->ppGroupWindows[i]);
	}
}

static void PTLevelFieldChangeCallback(PTField *pField, PTEditDoc *pdoc)
{
	if(pdoc->bLevelView)
	{
		PTResizeGroupWin_All(pdoc);
	}
}

static void PTNodeClick(UIButton *pButton, PTNodeEdit *pNode)
{
	if (pNode->pGroup->pWin)
	{
		PTGroupWinFocus(UI_WIDGET(pNode->pGroup->pWin), pNode->pGroup->pdoc);
	}

	ui_ExpanderSetOpened(pNode->pExp, true);
	PTExpanderSetHighlight(pNode->pExp);
	PTRankResize(pNode->pExp,pNode);
}

static void PTCreateNodeExp(PTNodeDef *pNew,PTNodeDef *pOld, PTGroupEdit *pGroup);

static void PTGroupDrag(UIButton *pbutton, UserData data)
{
	ui_DragStart((UIWidget *)pbutton,UI_DND_GROUP,data,atlasLoadTexture("B_Button_38.tga"));
}
static void PTNodeDrag(UIButton *button, UserData data)
{
	ui_DragStart((UIWidget *)button, UI_DND_NODE, data, atlasLoadTexture("A_Button_38.tga"));
}

static void PTRequireGroup(UIMenuItem *pItem, PTGroupEdit *pSourceGroup)
{
	PTGroupEdit *pDestGroup = (PTGroupEdit *)pItem->data.voidPtr;

	if (!PTCanEdit(pSourceGroup->pdoc))
		return;

	ui_TextEntrySetTextAndCallback(pSourceGroup->pGroupText,pDestGroup->pDefGroupNew->pchNameFull);
}

static void PTRequireNode(UIMenuItem *pItem, PTGroupEdit *pSourceGroup)
{
	PTNodeEdit *pDestNode = (PTNodeEdit *)pItem->data.voidPtr;

	if (!PTCanEdit(pSourceGroup->pdoc))
		return;

	ui_TextEntrySetTextAndCallback(pSourceGroup->pGroupText,"");
}

static void PTGroupDrop(UIButton *psource, UIWindow *pdest, UIDnDPayload *pPayload, char *dummy)
{
	PTGroupEdit *pSourceGroup = (PTGroupEdit *)pPayload->payload;
	PTGroupEdit *pDestGroup = PTFindGroupWindow(pSourceGroup->pdoc->ppGroupWindows,ui_WidgetGetText(UI_WIDGET(pdest)));
	UIMenu *pMenu = pSourceGroup->pdoc->pGroupMenu;
	UIMenuItem *pItem;
	char *pchText = NULL;
	int i;

	if(pMenu)
		ui_WidgetQueueFree(UI_WIDGET(pMenu));

	pMenu = ui_MenuCreate("Drop Options");

	estrStackCreate(&pchText);

	estrPrintf(&pchText,"Require: Group %s",pDestGroup->pDefGroupNew->pchGroup);
	
	pItem = ui_MenuItemCreate(pchText,UIMenuCallback,PTRequireGroup,pSourceGroup,pDestGroup);

	ui_MenuAppendItem(pMenu,pItem);

	if(eaSize(&pDestGroup->ppNodeEdits) > 0) ui_MenuAppendItem(pMenu,ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL));
	for(i=0;i<eaSize(&pDestGroup->ppNodeEdits);i++)
	{
		estrPrintf(&pchText,"Require: Node %s",pDestGroup->ppNodeEdits[i]->pDefNodeNew->pchName);

		pItem = ui_MenuItemCreate(pchText,UIMenuCallback,PTRequireNode,pSourceGroup,pDestGroup->ppNodeEdits[i]);
		ui_MenuAppendItem(pMenu,pItem);
	}

	pSourceGroup->pdoc->pGroupMenu = pMenu;

	ui_MenuPopupAtCursor(pMenu);

	estrDestroy(&pchText);
}
static void PTCloneNode(PTGroupEdit *pGroup, PTNodeEdit *pNode)
{
	PTNodeDef *pCloneNode;

	if (!PTCanEdit(pNode->pdoc))
		return;

	pCloneNode = StructCreate(parse_PTNodeDef);

	StructCopyAll(parse_PTNodeDef,pNode->pDefNodeNew,pCloneNode);

	SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pNode->pDefNodeNew->pchNameFull,pCloneNode->hNodeClone);

	eaPush(&pGroup->pDefGroupNew->ppNodes,pCloneNode);

	PTCreateNodeExp(pCloneNode,NULL,pGroup);
}
static void PTCloneNodeMenu(UIMenuItem *pItem, PTNodeEdit *pNode)
{
	PTGroupEdit *pNewGroup = (PTGroupEdit *)pItem->data.voidPtr;

	PTCloneNode(pNewGroup,pNode);
}
static void PTMoveNode(UIMenuItem *pItem, PTNodeEdit *pNode)
{
	PTGroupEdit *pNewGroup = (PTGroupEdit *)pItem->data.voidPtr;
	PTGroupEdit *pOrigGroup = pNode->pGroup;

	if (!PTCanEdit(pOrigGroup->pdoc))
		return;

	eaFindAndRemove(&pOrigGroup->pDefGroupNew->ppNodes,pNode->pDefNodeNew);
	eaFindAndRemove(&pOrigGroup->ppNodeEdits,pNode);
	eaRemove(&pOrigGroup->ppchiMax,eaSize(&pOrigGroup->ppchiMax) - 1);
	eaPush(&pNewGroup->pDefGroupNew->ppNodes,pNode->pDefNodeNew);

	ui_ExpanderGroupRemoveExpander(pOrigGroup->pExpNodes,pNode->pExp);
	ui_WindowRemoveChild(pOrigGroup->pWin,pNode->pBtn);

	PTCreateNodeExp(pNode->pDefNodeNew,NULL,pNewGroup);
	PTResizeGroupWin(pOrigGroup);
}
static void PTNodeDrop(UIButton *source, UIWindow *dest, UIDnDPayload *payload, char *dummy)
{
	PTNodeEdit *pNode = (PTNodeEdit *)payload->payload;
	PTGroupEdit *pGroup = PTFindGroupWindow(pNode->pdoc->ppGroupWindows,ui_WidgetGetText(UI_WIDGET(dest)));
	UIMenu *pMenu = pNode->pdoc->pGroupMenu;
	UIMenuItem *pItem;
	char *pchText = NULL;

	if(pMenu)
		ui_WidgetQueueFree(UI_WIDGET(pMenu));

	pMenu = ui_MenuCreate("Drop Options");

	estrCreate(&pchText);

	estrPrintf(&pchText,"Move node to group");

	pItem = ui_MenuItemCreate(pchText,UIMenuCallback,PTMoveNode,pNode,pGroup);
	ui_MenuAppendItem(pMenu,pItem);

	estrPrintf(&pchText,"Clone node into group");
	pItem = ui_MenuItemCreate(pchText,UIMenuCallback,PTCloneNodeMenu,pNode,pGroup);
	ui_MenuAppendItem(pMenu,pItem);

	ui_MenuPopupAtCursor(pMenu);

	/*

	pItem = ui_MenuItemCreate(NULL,UIMenuSeparator,NULL,NULL,NULL);
	ui_MenuAppendItem(pMenu,pItem);

	estrPrintf(&pchText,"Require: Group %s",pGroup->pDefGroupNew->pchGroup);
	pItem = ui_MenuItemCreate(pchText,UIMenuCallback,PTNodeRequireGroup,pNode,pGroup);

	for(i=0;i<eaSize(&pGroup->ppNodeEdits);i++)
	{
		estrPrintf(&pchText,"Require: Node %s",pGroup->ppNodeEdits[i]->pDefNodeNew->pchName);

		pItem = ui_MenuItemCreate(pchText,UIMenuCallback,PTNodeRequireNode,pNode,pGroup->ppNodeEdits[i]);
		ui_MenuAppendItem(pMenu,pItem);
	}
	*/
}

static void PTWinDrop(UIButton *pSource, UIWindow *pDest, UIDnDPayload *pPayload, char *pchDummy)
{
	if (pPayload->type == UI_DND_NODE)
		PTNodeDrop(pSource,pDest,pPayload,pchDummy);
	else if(pPayload->type == UI_DND_GROUP)
		PTGroupDrop(pSource,pDest,pPayload,pchDummy);
	else
		Errorf("Power Tree Editor:\n\n\nUnknown drag and drop type!");
}

static void PTNodeAccept(UIButton *source, UIWindow *dest, UIDnDPayload *payload, char *dummy)
{
	
}

static void PTRankMoveUp(PTRankEdit *pRank)
{
	PTNodeEdit *pNode = pRank->pNode;
	PTNodeRankDef *pTemp = malloc(sizeof(PTNodeRankDef));
	int i;

	if (!PTCanEdit(pNode->pdoc))
		return;

	for(i=0;i<eaSize(&pNode->ppRankEdits);i++)
	{
		if(pNode->pDefNodeNew->ppRanks[i] == pRank->pDefRankNew)
			break;
	}

	if(i == 0) //Safe check, this should never be true
		return;

	memcpy(pTemp,pNode->pDefNodeNew->ppRanks[i],sizeof(PTNodeRankDef));
	memcpy(pNode->pDefNodeNew->ppRanks[i],pNode->pDefNodeNew->ppRanks[i-1],sizeof(PTNodeRankDef));
	memcpy(pNode->pDefNodeNew->ppRanks[i-1], pTemp,sizeof(PTNodeRankDef));

	PTUpdateAllFields(pRank->pNode->pGroup->pdoc);
}
static void PTRankMoveDown(PTRankEdit *pRank)
{
	PTNodeEdit *pNode = pRank->pNode;
	PTNodeRankDef *pTemp = malloc(sizeof(PTNodeRankDef));
	int i;

	if (!PTCanEdit(pNode->pdoc))
		return;

	for(i=0;i<eaSize(&pNode->ppRankEdits);i++)
	{
		if(pNode->pDefNodeNew->ppRanks[i] == pRank->pDefRankNew)
			break;
	}

	if(i >= eaSize(&pNode->ppRankEdits) - 1) //Safe check, this should never be true
		return;

	memcpy(pTemp,pNode->pDefNodeNew->ppRanks[i],sizeof(PTNodeRankDef));
	memcpy(pNode->pDefNodeNew->ppRanks[i],pNode->pDefNodeNew->ppRanks[i+1],sizeof(PTNodeRankDef));
	memcpy(pNode->pDefNodeNew->ppRanks[i+1], pTemp,sizeof(PTNodeRankDef));

	PTUpdateAllFields(pRank->pNode->pGroup->pdoc);
}
static void PTDeleteRank(PTRankEdit *pRank)
{
	PTNodeEdit *pOrigNode = pRank->pNode;

	if (!PTCanEdit(pOrigNode->pdoc))
		return;

	eaFindAndRemove(&pOrigNode->pDefNodeNew->ppRanks,pRank->pDefRankNew);
	eaFindAndRemove(&pOrigNode->ppRankEdits,pRank);
	ui_ExpanderGroupRemoveExpander(pOrigNode->pExpRanks,pRank->pExp);
	
	PTResizeGroupWin(pOrigNode->pGroup);

	PTPermaDirty(pOrigNode->pGroup->pdoc);
}

static void PTDeleteEnhancement(PTEnhancementEdit *pEnhancement)
{
	PTNodeEdit *pNode = pEnhancement->pNode;

	if (!PTCanEdit(pNode->pdoc))
		return;

	eaFindAndRemove(&pNode->pDefNodeNew->ppEnhancements,pEnhancement->pDefNew);
	eaFindAndRemove(&pNode->ppEnhancementEdits,pEnhancement);

	ui_ExpanderGroupRemoveExpander(pNode->pExpRanks,pEnhancement->pExp);

	PTResizeGroupWin(pNode->pGroup);

	PTPermaDirty(pNode->pGroup->pdoc);
}

static void PTNodeCopyAs(PTNodeEdit *pNode)
{
	char *pchName = NULL;
	PTGroupEdit *pOrigGroup = pNode->pGroup;
	PTNodeDef *pNewNode;

	if (!PTCanEdit(pNode->pdoc))
		return;

	pNewNode = StructCreate(parse_PTNodeDef);
	estrStackCreate(&pchName);
	estrPrintf(&pchName,"Copy of %s",pNode->pDefNodeNew->pchName);

	StructCopyFields(parse_PTNodeDef,pNode->pDefNodeNew,pNewNode,0,0);
	StructFreeString(pNewNode->pchName);
	pNewNode->pchName = StructAllocString(pchName);
	estrDestroy(&pchName);

	eaPush(&pOrigGroup->pDefGroupNew->ppNodes,pNewNode);
	
	PTCreateNodeExp(pNewNode,NULL,pOrigGroup);
}

void PTDeleteNodeDependencies(PTEditDoc *pDoc, PTNodeDef *pNodeDef)
{
	S32 i, j;

	for(i = 0; i < eaSize(&pDoc->ppGroupWindows);i++)
	{
		for(j = 0; j < eaSize(&pDoc->ppGroupWindows[i]->ppNodeEdits); j++)
		{
			if (IS_HANDLE_ACTIVE(pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew->hNodeRequire) &&
				stricmp(REF_STRING_FROM_HANDLE(pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew->hNodeRequire), pNodeDef->pchNameFull) == 0)
			{
				REMOVE_HANDLE(pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew->hNodeRequire);
			}
		}
	}
}

static void PTDeleteNode(PTNodeEdit *pNode)
{
	PTGroupEdit *pOrigGroup = pNode->pGroup;	

	if (!PTCanEdit(pNode->pdoc))
		return;

	// Destroy any dependencies to this node
	PTDeleteNodeDependencies(pNode->pdoc, pNode->pDefNodeNew);	

	eaFindAndRemove(&pOrigGroup->pDefGroupNew->ppNodes,pNode->pDefNodeNew);
	eaFindAndRemove(&pOrigGroup->ppNodeEdits,pNode);
	eaRemove(&pOrigGroup->ppchiMax,eaSize(&pOrigGroup->ppchiMax) - 1);
	ui_ExpanderGroupRemoveExpander(pOrigGroup->pExpNodes,pNode->pExp);
	if (PTEditorIsInPowerTreeMode(pNode->pDefNodeNew))
	{
		ui_WindowRemoveChild(pOrigGroup->pWin,pNode->pBtn);
		PTResizeGroupWin(pOrigGroup);
	}

	PTCreateNodeList(pNode->pdoc);

	PTPermaDirty(pOrigGroup->pdoc);
}
static void PTMenuRankUp(UIMenuItem *pMenuItem, PTRankEdit *pRank)
{
	PTRankMoveUp(pRank);
}
static void PTMenuRankDown(UIMenuItem *pMenuItem, PTRankEdit *pRank)
{
	PTRankMoveDown(pRank);
}
static void PTRevertRank(PTRankEdit *pRank);
static void PTMenuRankRevert(UIMenuItem *pMenuItem,PTRankEdit *pRank)
{
	PTRevertRank(pRank);
}
static void PTMenuRankDelete(UIMenuItem *pMenuItem,PTRankEdit *pRank)
{
	PTDeleteRank(pRank);
}
static void PTMenuRankOpen(UIMenuItem *pMenuItem, PTRankEdit *pRank)
{
	if(pRank->pDefRankNew &&
		!pRank->pDefRankNew->bEmpty &&
		REF_STRING_FROM_HANDLE(pRank->pDefRankNew->hPowerDef))
	{
		emOpenFileEx(REF_STRING_FROM_HANDLE(pRank->pDefRankNew->hPowerDef), "PowerDef");
	}
	else if(pRank->pDefRankOrig &&
		!pRank->pDefRankOrig->bEmpty && 
		REF_STRING_FROM_HANDLE(pRank->pDefRankOrig->hPowerDef))
	{
		emOpenFileEx(REF_STRING_FROM_HANDLE(pRank->pDefRankOrig->hPowerDef), "PowerDef");
	}
}
static void PTMenuEnhancementDelete(UIMenuItem *pmenuItem,PTEnhancementEdit *pEnhancement)
{
	PTDeleteEnhancement(pEnhancement);
}

static void PTMenuNodeOpenPowers(UIMenuItem *pMenuItem, PTNodeEdit *pNode)
{
	S32 iRankIdx;
	for(iRankIdx = eaSize(&pNode->ppRankEdits)-1; iRankIdx >= 0; iRankIdx--)
	{
		PTRankEdit *pRank = pNode->ppRankEdits[iRankIdx];
		if(pRank)
		{
			PTMenuRankOpen(pMenuItem, pRank);
		}
	}
}

static void PTValidateNode(PTNodeEdit *pNode);
static void PTMenuNodeValidate(UIMenuItem *pMenuItem,PTNodeEdit *pNode)
{
	PTValidateNode(pNode);
}
static void PTRevertNode(PTNodeEdit *pNode);
static void PTMenuNodeRevert(UIMenuItem *pItem, PTNodeEdit *pNode)
{
	PTRevertNode(pNode);
}
static void PTMenuNodeDelete(UIMenuItem *pItem, PTNodeEdit *pNode)
{
	PTDeleteNode(pNode);
}
static void PTMenuNodeCopyAs(UIMenuItem *pItem, PTNodeEdit *pNode)
{
	PTNodeCopyAs(pNode);
}
static void PTMenuCloneNode(UIMenuItem *pItem, PTNodeEdit *pNode)
{
	PTCloneNode(pNode->pGroup,pNode);
}
static void PTDeleteGroup(PTGroupEdit *pGroup, bool bForce);
static void PTMenuGroupDelete(UIMenuItem *pItem, PTGroupEdit *pGroup)
{
	PTDeleteGroup(pGroup, false);
}
static void PTRevertGroup(PTGroupEdit *pGroup);
static void PTMenuGroupRevert(UIMenuItem *pItem, PTGroupEdit *pGroup)
{
	PTRevertGroup(pGroup);
}

//void PTGroupCopy(PTGroupEdit *pGroup, bool bCut, bool bAffectChildren, bool bClone);
void PTNodeClone(PTNodeEdit *pNode);

static void PTMenuGroupOpenPowers(UIMenuItem *pMenuItem, PTGroupEdit *pGroup)
{
	S32 iGroupIdx;
	for(iGroupIdx = eaSize(&pGroup->ppNodeEdits)-1; iGroupIdx >= 0; iGroupIdx--)
	{
		PTNodeEdit *pNode = pGroup->ppNodeEdits[iGroupIdx];
		if(pNode)
			PTMenuNodeOpenPowers(pMenuItem, pNode);
	}
}

static void PTMenuGroupClone(UIMenuItem *pMenuItem, PTGroupEdit *pGroup)
{
	PTGroupCopy(pGroup,false,false,true);
}
static void PTMenuGroupCloneAll(UIMenuItem *pMenuItem, PTGroupEdit *pGroup)
{
	PTGroupCopy(pGroup,false,true,true);
}
static void PTMenuGroupCopy(UIMenuItem *pMenuItem, PTGroupEdit *pGroup)
{
	PTGroupCopy(pGroup,false,false,false);
}

static void PTMenuGroupCopyAll(UIMenuItem *pMenuItem, PTGroupEdit *pGroup)
{
	PTGroupCopy(pGroup,false,true,false);
}

static void PTMenuGroupCut(UIMenuItem *pMenuItem, PTGroupEdit *pGroup)
{
	PTGroupCopy(pGroup,true,false,false);
}

static void PTMenuGroupCutAll(UIMenuItem *pMenuItem, PTGroupEdit *pGroup)
{
	PTGroupCopy(pGroup,true,true,false);
}

static void PTMenuNodeClone(UIMenuItem *pMenuItem, PTNodeEdit *pNode)
{
	PTNodeClone(pNode);
}
bool PTFilterFunc(UIFilteredList *pFList, const char *pchValue, const char *pchMatch)
{
	if (pchMatch && pchValue && strstr(pchValue,pchMatch))
		return true;

	return false;
}

static void PTCreateNodeExp(PTNodeDef *pNew,PTNodeDef *pOld, PTGroupEdit *pGroup)
{
	PTNodeEdit *pNewNode = calloc(sizeof(*pNewNode),1);
	UIButton *pButton;
	UIComboBox *pCombo;
	int i,x,y;
	char achNum[3];
	UIMenu *pMenu;
	PTField *pTempField;

	pMenu = ui_MenuCreateWithItems("Node Options",
		ui_MenuItemCreate("Name Goes Here",UIMenuCallback,NULL,NULL,NULL),
		ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Open All Powers",UIMenuCallback,PTMenuNodeOpenPowers,pNewNode,NULL),
		ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Delete Node",UIMenuCallback,PTMenuNodeDelete,pNewNode,NULL),
		ui_MenuItemCreate("Revert Node",UIMenuCallback,PTMenuNodeRevert,pNewNode,NULL),
		ui_MenuItemCreate("Validate Node",UIMenuCallback,PTMenuNodeValidate,pNewNode,NULL),
		ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Copy as New Node",UIMenuCallback,PTMenuNodeCopyAs,pNewNode,NULL),
		ui_MenuItemCreate("Copy Node As Clone",UIMenuCallback,PTMenuNodeClone,pNewNode,NULL),
		NULL);
	pMenu->items[0]->active = false;
	pNewNode->pMenu = pMenu;

	pNewNode->pDefNodeNew = pNew;
	pNewNode->pDefNodeOrig = pOld;
	pNewNode->pExp = ui_ExpanderCreate(pNew->pDisplayMessage.pEditorCopy->pcDefaultString,205);
	pNewNode->pExp->autoScroll = true;
	ui_WidgetSetContextCallback(UI_WIDGET(pNewNode->pExp),PTExpandNodeRClick,pNewNode);
	ui_ExpanderGroupAddExpander(pGroup->pExpNodes,pNewNode->pExp);

	if (PTEditorIsInPowerTreeMode(pNewNode->pDefNodeNew))
	{
		//Add button to the group window
		y= 20;
		x= 75 * (eaSize(&pGroup->ppchiMax) - 1) + 5;

		pNewNode->pBtn = ui_ButtonCreate(pNewNode->pDefNodeNew->pchName,x,y,PTNodeClick,pNewNode);
		pNewNode->pBtn->widget.height = 35;

		if(IS_HANDLE_ACTIVE(pNew->hNodeClone))
			ui_WidgetUnskin((UIWidget*)pNewNode->pBtn,ColorLightBlue,ColorBlack,ColorBlack,ColorBlack);
		ui_WindowAddChild(pGroup->pWin,pNewNode->pBtn);
		ui_WidgetSetContextCallback(UI_WIDGET(pNewNode->pBtn),PTButtonNodeRClick,pNewNode);
		ui_WidgetSetDragCallback((UIWidget *)pNewNode->pBtn,PTNodeDrag,pNewNode);
		ui_WidgetSetAcceptCallback((UIWidget *)pNewNode->pBtn,PTNodeAccept,NULL);
	}

	// Fill in Node Fields

	y = 5;

	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"Name",NULL,NULL,true,NULL,pNewNode->pBtn,10,y,100,true,false,true,pMenu);
	pTempField->pGroup = pGroup;
	eaPush(&pGroup->pdoc->ppFields,pTempField);
	PTField_AddFieldChangeCallback(pTempField, PTLevelFieldChangeCallback, pGroup->pdoc);

	pTempField = PTAddFlagField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"Flags",NULL,1,PTNodeFlagEnum,215,y,100,false,pMenu);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUIFlagBox->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);


	y += 25;

	pTempField = PTAddMessageField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"DisplayMessage","Display Name",true,10,y,100,false,false,false,pMenu);
	eaPush(&pGroup->pdoc->ppFields,pTempField);
 
	pCombo = ui_ComboBoxCreateWithDictionary(0,0,100,g_hPowerTreeNodeDefDict,parse_PTNodeDef,"NameFull");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"Clone","PowerTreeNodeDef","Clone Existing",true,pCombo,NULL,215,y,100,true,false,false,NULL);
	PTField_AddFieldChangeCallback(pTempField,PTCloneFieldChange,pNewNode);
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	y += 25;

	pCombo = ui_ComboBoxCreateWithDictionary(0,0,100,"PTNodeTypeDef",parse_PTNodeTypeDef,"Name");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"NodeType","PTNodeTypeDef","Node Type",true,pCombo,NULL,10,y,100,true,false,false,NULL);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	pCombo = ui_ComboBoxCreateWithDictionary(0,0,100,g_hPowerReplaceDefDict,parse_PowerReplaceDef,"Name");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"GrantSlot","PowerReplaceDef","Grant Replace",true,pCombo,NULL,215,y,100,true,false,false,pMenu);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	y += 25;
		
	pCombo = ui_FilteredComboBoxCreateWithDictionary(0,0,100,g_hPowerTreeNodeDefDict,parse_PTNodeDef,"NameFull");
	ui_FilteredComboBoxSetFilterCallback(pCombo,PTFilterFunc,pGroup->pDefGroupNew->pchNameFull);
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"hNodeRequire","PowerTreeNodeDef","Node Required",true,pCombo,NULL,10,y,87,true,false,false,pMenu);
	PTField_AddFieldChangeCallback(pTempField,PTLevelFieldChangeCallback,pTempField->pdoc);
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"Required",NULL,NULL,false,NULL,NULL,190,y,25,true,false,false,pMenu);
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	pCombo = ui_ComboBoxCreateWithDictionary(0,0,100,g_hPowerTreeNodeDefDict,parse_PTNodeDef,"NameFull");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"NodePowerSlot","PowerTreeNodeDef","Node PowerSlot",true,pCombo,NULL,215,y,100,true,false,false,NULL);
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	y += 25;

	pTempField = PTAddMessageField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"msgRequirements","Require Msg",true,10,y,100,false,false,false,pMenu);
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"CostMaxEnhancement",NULL,"Max Enh Cost",true,NULL,NULL,215,y,100,true,false,false,pMenu);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	y += 25;

	pTempField = PTAddTextFieldEnumCombo(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"Attrib","Attrib",true,AttribTypeEnum,NULL,10,y,100,false,false,true,NULL);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	pCombo = ui_ComboBoxCreate(0,0,200,parse_PowerTable,&g_PowerTables.ppPowerTables,"Name");
	pCombo->openedWidth = 400;
	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"AttribPowerTable","PowerTable","Attrib Table",true,pCombo,NULL,215,y,100,true,false,false,NULL);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	y += 25;

	pTempField = PTAddEnumComboField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"Purpose","Purpose",true,PowerPurposeEnum,10,y,100,false,pMenu);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUICombo->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	pTempField = PTAddTextField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"IconName",NULL,"Icon",true,NULL,NULL,215,y,100,true,false,false,pMenu);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUIText->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	y += 25;

	pTempField = PTAddEnumComboField(NULL,pNewNode->pExp,pGroup->pdoc,pOld,pNew,parse_PTNodeDef,"UICategory","UI Category",true,PTNodeUICategoryEnum,10,y,100,false,pMenu);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pTempField->pUICombo->widget.state = kWidgetModifier_Inactive;
	eaPush(&pGroup->pdoc->ppFields,pTempField);

	y += 30;

	pButton = ui_ButtonCreate("New Rank",10,y,PTNewRank,pNewNode);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pButton->widget.state = kWidgetModifier_Inactive;
	pNewNode->pNewRankBtn = pButton;
	ui_ExpanderAddChild(pNewNode->pExp,(UIWidget *)pButton);

	pButton = ui_ButtonCreate("New Enhancement",pButton->widget.x + pButton->widget.width + 10,y,PTNewEnhancement,pNewNode);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pButton->widget.state = kWidgetModifier_Inactive;
	pNewNode->pNewEnhancmentButton = pButton;
	ui_ExpanderAddChild(pNewNode->pExp,(UIWidget *)pButton);

	pButton = ui_ButtonCreate("Default Enhancements",pButton->widget.x + pButton->widget.width + 30,y,PTButtonDefEnhancement,pNewNode);
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pButton->widget.state = kWidgetModifier_Inactive;
	pNewNode->pDefaultEnhButton = pButton;
	ui_ExpanderAddChild(pNewNode->pExp,(UIWidget *)pButton);

	pNewNode->pExpRanks = ui_ExpanderGroupCreate();

	pNewNode->pExpRanks->widget.sb->scrollY = false;
	if(IS_HANDLE_ACTIVE(pNew->hNodeClone)) pNewNode->pExpRanks->widget.state = kWidgetModifier_Inactive;
	ui_WidgetSetDimensionsEx(&pNewNode->pExpRanks->widget,1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(&pNewNode->pExpRanks->widget,15,205,0,0,UITopLeft);

	ui_ExpanderAddChild(pNewNode->pExp,(UIWidget *)pNewNode->pExpRanks); 


	pNewNode->pExp->widget.drawF = PTExpanderDraw;
	pNewNode->pExp->widget.tickF = PTExpanderTick;

	pNewNode->pdoc = pGroup->pdoc;
	pNewNode->pGroup = pGroup;

	
	sprintf(achNum,"%d",eaSize(&pGroup->ppchiMax));
	eaPush(&pGroup->ppchiMax,strdup(achNum));
	eaPush(&pGroup->ppNodeEdits,pNewNode);

	for(i=0;i<eaSize(&pNew->ppRanks);i++)
	{
		if(pOld)
			PTCreateRankExp(pNew->ppRanks[i],pOld->ppRanks[i],pNewNode);
		else
			PTCreateRankExp(pNew->ppRanks[i],NULL,pNewNode);
	}

	for(i=0;i<eaSize(&pNew->ppEnhancements);i++)
	{
		PTCreateEnhancementExp(pNew->ppEnhancements[i],pOld ? pOld->ppEnhancements[i] : NULL,pNewNode);
	}

	if (PTEditorIsInPowerTreeMode(pNewNode->pDefNodeNew))
	{
		if(pGroup->pdoc->bLevelView)
			PTResizeGroupWin_All(pGroup->pdoc);
		else
			PTResizeGroupWin(pGroup);
	}

	PTCreateNodeList(pGroup->pdoc);
}
static void PTNewNode(PTGroupEdit *pGroup, S8 iRow, S8 iCol)
{
	PTNodeDef *pNewNode;
	char achName[MAX_POWER_NAME_LEN];
	char *pchNameFull = NULL;

	if (!PTCanEdit(pGroup->pdoc))
		return;

	pNewNode = StructCreate(parse_PTNodeDef);
	langMakeEditorCopy(parse_PTNodeDef,pNewNode,true);
	if (iCol > 0)
		sprintf(achName,"_Node %d", iCol);
	else
		sprintf(achName,"_Node %d",eaSize(&pGroup->pDefGroupNew->ppNodes));
	//pNewNode->pchDisplayName = StructAllocString(achName);
	pNewNode->pDisplayMessage.pEditorCopy->pcDefaultString = StructAllocString(achName);
	pNewNode->pchName = StructAllocString(achName);
	pNewNode->iUIGridRow = iRow;
	pNewNode->iUIGridColumn = iCol;

	estrStackCreate(&pchNameFull);
	estrPrintf(&pchNameFull,"%s.%s",pGroup->pDefGroupNew->pchNameFull,achName);
	pNewNode->pchNameFull = allocAddString(pchNameFull);
	estrDestroy(&pchNameFull);

	eaPush(&pGroup->pDefGroupNew->ppNodes,pNewNode);

	PTCreateNodeExp(pNewNode,NULL,pGroup);

	{
		PTNodeEdit *pNewEdit = PTFindNode(pGroup->pdoc,achName);

		if(pNewEdit)
		{
			PTNodeClick(pNewEdit->pBtn,pNewEdit);
		}
	}
}
static void PTButtonNewNode(UIButton *pButton, PTGroupEdit *pGroup)
{
	PTNewNode(pGroup, 0, 0);
}
static void PTCreateGroupWindow(PTGroupDef *pgroupNewDef, PTGroupDef *pgroupOrigDef, PTEditDoc *pdoc)
{
	UIWindow *pWin = NULL;
	UIButton *pButton;
	PTGroupEdit *pgroupEdit;
	int i;
	PTField *pTempField;
	UIMenu *pMenu;
	UIComboBox *pCombo;
	F32 y;

	pgroupEdit = calloc(sizeof(PTGroupEdit),1);
	pgroupEdit->pDefGroupNew = pgroupNewDef;
	pgroupEdit->pDefGroupOrig = pgroupOrigDef;
	pgroupEdit->pExp = ui_ExpanderCreate(pgroupNewDef->pchGroup,550);
	ui_WidgetSetContextCallback(UI_WIDGET(pgroupEdit->pExp),PTExpandGroupRClick,pgroupEdit);
	pgroupEdit->pExpNodes = ui_ExpanderGroupCreate();
	eaPush(&pgroupEdit->ppchiMax,"0");

	if (PTEditorIsInPowerTreeMode(pgroupNewDef))
	{
		pWin = ui_WindowCreate(pgroupNewDef->pchGroup,pgroupNewDef->x,pgroupNewDef->y,100,160);
		pWin->widget.scale = 0.8;
		ui_WidgetSetFocusCallback((UIWidget *)pWin,PTGroupWinFocus,pdoc);
		ui_WindowSetClosable(pWin, false);
		pWin->resizable = false;
		ui_WidgetSetDropCallback((UIWidget *)pWin,PTWinDrop,NULL);
		ui_ScrollAreaAddChild(pdoc->pScrollArea,(UIWidget *)pWin);			
		pgroupEdit->pWin = pWin;
		ui_WidgetSetContextCallback(UI_WIDGET(pgroupEdit->pWin),PTExpandGroupRClick,pgroupEdit);

		pMenu = ui_MenuCreateWithItems("Group Options",
			ui_MenuItemCreate("Name Goes Here",UIMenuCallback,NULL,NULL,NULL),
			ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
			ui_MenuItemCreate("Open All Powers",UIMenuCallback, PTMenuGroupOpenPowers, pgroupEdit, NULL),
			ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
			ui_MenuItemCreate("Copy Group",UIMenuCallback,PTMenuGroupCopy,pgroupEdit,NULL),
			ui_MenuItemCreate("Copy Group w/ Children",UIMenuCallback,PTMenuGroupCopyAll,pgroupEdit,NULL),
			ui_MenuItemCreate("Cut Group",UIMenuCallback,PTMenuGroupCut,pgroupEdit,NULL),
			ui_MenuItemCreate("Cut Group w/ Children",UIMenuCallback,PTMenuGroupCutAll,pgroupEdit,NULL),
			ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
			ui_MenuItemCreate("Clone Group",UIMenuCallback,PTMenuGroupClone,pgroupEdit,NULL),
			ui_MenuItemCreate("Clone Group w/ Children",UIMenuCallback,PTMenuGroupCloneAll,pgroupEdit,NULL),
			ui_MenuItemCreate("",UIMenuSeparator,NULL,NULL,NULL),
			ui_MenuItemCreate("Delete Group",UIMenuCallback,PTMenuGroupDelete,pgroupEdit,NULL),
			ui_MenuItemCreate("Revert Group",UIMenuCallback,PTMenuGroupRevert,pgroupEdit,NULL),
			ui_MenuItemCreate("Validate Group",UIMenuCallback,NULL,NULL,NULL),
			NULL);
		pMenu->items[0]->active = false;

		pgroupEdit->pMenu = pMenu;

		pgroupEdit->pButtonDrag = ui_ButtonCreate(" + ",0,0,NULL,NULL);
		ui_WidgetSetDragCallback((UIWidget *)pgroupEdit->pButtonDrag,PTGroupDrag,pgroupEdit);
		pgroupEdit->pButtonDrag->widget.scale = 0.8f;
		ui_WindowAddChild(pgroupEdit->pWin,pgroupEdit->pButtonDrag);
	}

	y = 5;

	eaPush(&pdoc->ppFields,PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef,pgroupNewDef,parse_PTGroupDef,"Group",NULL,"Group Name",1,NULL,NULL,5,y,150,true,false,true,NULL));
	
	y += 25;

	pTempField = PTAddMessageField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef,pgroupNewDef,parse_PTGroupDef,"DisplayMessage","Display Name",true,5,y,150,true,false,false,NULL);
	eaPush(&pdoc->ppFields,pTempField);

	y += 25;

	if (PTEditorIsInPowerTreeMode(pgroupNewDef))
	{
		pCombo = ui_ComboBoxCreate(0,0,100,NULL,&pdoc->ppchGroupNames,NULL);
		pCombo->openedWidth = 400;
		pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef? pgroupOrigDef->pRequires : NULL,pgroupNewDef->pRequires,parse_PTPurchaseRequirements,"hGroup",g_hPowerTreeGroupDefDict,"Group Required",true,pCombo,NULL,5,y,150,true,false,false,NULL);
		pgroupEdit->pGroupText = pTempField->pUIText;
		eaPush(&pdoc->ppFields,pTempField);

		pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef? pgroupOrigDef->pRequires : NULL,pgroupNewDef->pRequires,parse_PTPurchaseRequirements,"GroupRequired",NULL,NULL,false,NULL,NULL,310,y,25,true,false,false,NULL);
		pgroupEdit->pGroupNum = pTempField->pUIText;
		eaPush(&pdoc->ppFields,pTempField);

		y += 25;
	}

	pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef? pgroupOrigDef->pRequires : NULL,pgroupNewDef->pRequires,parse_PTPurchaseRequirements,"TableLevel",NULL, PTEditorIsInPowerTreeMode(pgroupNewDef) ? "Level" : "Points",true,NULL,NULL,5,y,150,true,false,false,NULL);
	eaPush(&pdoc->ppFields,pTempField);

	y += 25;

	pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef? pgroupOrigDef->pRequires : NULL,pgroupNewDef->pRequires,parse_PTPurchaseRequirements,"PointsSpentInThisTree",NULL, "Points Spent In This Tree",true,NULL,NULL,5,y,150,true,false,false,NULL);
	eaPush(&pdoc->ppFields,pTempField);

	y += 25;

	if (PTEditorIsInPowerTreeMode(pgroupNewDef))
	{
		pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef,pgroupNewDef,parse_PTGroupDef,"Order",NULL,"UI Sort Order",true,NULL,NULL,5,y,150,true,false,false,NULL);
		eaPush(&pdoc->ppFields,pTempField);

		y += 25;

		pCombo = ui_ComboBoxCreate(0,0,200,parse_PowerTable,&g_PowerTables.ppPowerTables,"Name");
		pCombo->openedWidth = 400;
		pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef? pgroupOrigDef->pRequires : NULL,pgroupNewDef->pRequires,parse_PTPurchaseRequirements,"TableName","PowerTable","LevelTable",true,pCombo,NULL,5,y,150,true,false,false,NULL);
		eaPush(&pdoc->ppFields,pTempField);

		y += 25;

		pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef? pgroupOrigDef->pRequires : NULL,pgroupNewDef->pRequires,parse_PTPurchaseRequirements,"PurchaseExpression",NULL,"Requirements",true,NULL,NULL,5,y,150,true,false,false,NULL);
		eaPush(&pdoc->ppFields,pTempField);

		y += 25;

		pTempField = PTAddComboField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef,pgroupNewDef,parse_PTGroupDef,"Max","Max Nodes",true,&pgroupEdit->ppchiMax,5,y,150,false,NULL);
		eaPush(&pdoc->ppFields,pTempField);
		
		y += 30;

		pButton = ui_ButtonCreate("New Node",5,y,PTButtonNewNode,pgroupEdit);
		ui_ExpanderAddChild(pgroupEdit->pExp,(UIWidget *)pButton);

		y+= 25;
	}
	else
	{
		pTempField = PTAddTextField(NULL,pgroupEdit->pExp,pdoc,pgroupOrigDef? pgroupOrigDef->pRequires : NULL,pgroupNewDef->pRequires,parse_PTPurchaseRequirements,"PurchaseExpression",NULL,"Requirements",true,NULL,NULL,5,y,150,true,false,false,NULL);
		eaPush(&pdoc->ppFields,pTempField);

		y += 25;
	}

	eaPush(&pdoc->ppGroupWindows,pgroupEdit);

	pgroupEdit->pdoc = pdoc;

	ui_WidgetSetDimensionsEx(&pgroupEdit->pExpNodes->widget,1.f,50, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(&pgroupEdit->pExpNodes->widget,0,y,0,0,UITopLeft);
	ui_ExpanderGroupSetGrow(pgroupEdit->pExpNodes, true);
	ui_ExpanderAddChild(pgroupEdit->pExp,(UIWidget *)pgroupEdit->pExpNodes);

	ui_ExpanderSetOpened(pgroupEdit->pExp, true);
	ui_ExpanderReflow(pgroupEdit->pExp);

	for(i=0;i<eaSize(&pgroupNewDef->ppNodes);i++)
	{
		PTCreateNodeExp(pgroupNewDef->ppNodes[i],pgroupOrigDef ? pgroupOrigDef->ppNodes[i] : NULL,pgroupEdit);
	}

	pTempField->bRevert = true;
	PTUpdateField(pTempField,true);
	
	if (PTEditorIsInPowerTreeMode(pgroupNewDef))
	{
		ui_SetFocus(pWin);
	}	

	PTCreateGroupList(pdoc);
}
static void PTRevertRank(PTRankEdit *pRank)
{
	PTNodeEdit *pOrigNode = pRank->pNode;
	PTNodeRankDef *pNew;
	int pos;
	UIExpander *pExp;

	if (!PTCanEdit(pOrigNode->pdoc))
		return;

	pos = eaFindAndRemove(&pOrigNode->pDefNodeNew->ppRanks,pRank->pDefRankNew);

	StructDestroy(parse_PTNodeRankDef,pRank->pDefRankNew);
	eaFindAndRemove(&pOrigNode->ppRankEdits,pRank);
	ui_ExpanderGroupRemoveExpander(pOrigNode->pExpRanks,pRank->pExp);
	
	pNew = StructCreate(parse_PTNodeRankDef);

	StructCopyFields(parse_PTNodeRankDef,pRank->pDefRankOrig,pNew,0,0);

	pRank->pDefRankNew = pNew;

	eaInsert(&pOrigNode->pDefNodeNew->ppRanks,pRank->pDefRankNew,pos);

	PTCreateRankExp(pRank->pDefRankNew,pRank->pDefRankOrig,pOrigNode);

	pExp = (UIExpander *)pOrigNode->pExpRanks->childrenInOrder[eaSize(&pOrigNode->pExpRanks->childrenInOrder) - 1];
	if (pOrigNode->pExpRanks->childrenInOrder)
	{
		ui_ExpanderGroupRemoveExpander(pOrigNode->pExpRanks,pExp);
		ui_ExpanderGroupInsertExpander(pOrigNode->pExpRanks,pExp,pos);
	}
}
static void PTValidateNode(PTNodeEdit *pNode)
{
	PTGroupEdit *pGroup = pNode->pGroup;
	int i;

	if (!PTCanEdit(pNode->pdoc))
		return;

	for(i=0;i<eaSize(&pGroup->ppNodeEdits);i++)
	{
		if(pGroup->ppNodeEdits[i] == pNode)
			break;
	}
	
	if(!powertrees_Load_NodeValidate(pNode->pDefNodeNew,i,pGroup->pDefGroupNew,pNode->pdoc->pDefNew))
		ErrorFilenamef(pGroup->pdoc->pDefNew->pchFile,"Node #%d DID NOT PASS Validation",i);
	else
		Errorf("Node #%d passed Validation",i);
}
static void PTRevertNode(PTNodeEdit *pNode)
{
	PTGroupEdit *pOrigGroup = pNode->pGroup;
	PTNodeDef *pNew;
	int pos;
	UIExpander *pExp;

	if (!PTCanEdit(pNode->pdoc))
		return;

	pos = eaFindAndRemove(&pOrigGroup->pDefGroupNew->ppNodes,pNode->pDefNodeNew);

	StructDestroy(parse_PTNodeDef,pNode->pDefNodeNew);
	eaFindAndRemove(&pOrigGroup->ppNodeEdits,pNode);
	ui_ExpanderGroupRemoveExpander(pOrigGroup->pExpNodes,pNode->pExp);
	ui_WindowRemoveChild(pOrigGroup->pWin,(UIWidget *)pNode->pBtn);
	eaRemove(&pOrigGroup->ppchiMax,eaSize(&pOrigGroup->ppchiMax)-1);
	
	pNew = StructCreate(parse_PTNodeDef);

	StructCopyFields(parse_PTNodeDef,pNode->pDefNodeOrig,pNew,0,0);

	pNode->pDefNodeNew = pNew;

	eaInsert(&pOrigGroup->pDefGroupNew->ppNodes,pNode->pDefNodeNew,pos);

	PTCreateNodeExp(pNode->pDefNodeNew,pNode->pDefNodeOrig,pOrigGroup);

	pExp = (UIExpander *)pOrigGroup->pExpNodes->childrenInOrder[eaSize(&pOrigGroup->pExpNodes->childrenInOrder) - 1];
	if(pOrigGroup->pExpNodes->childrenInOrder)
	{
		ui_ExpanderGroupRemoveExpander(pOrigGroup->pExpNodes,pExp);
		ui_ExpanderGroupInsertExpander(pOrigGroup->pExpNodes,pExp,pos);
	}
}
static void PTValidateGroup(PTGroupEdit *pGroup)
{
	PTEditDoc *pdoc;
	int i;

	if(!pGroup || !pGroup->pDefGroupNew)
		return;
	pdoc = pGroup->pdoc;
	for(i=0;i<eaSize(&pdoc->ppGroupWindows);i++)
	{
		if(pdoc->ppGroupWindows[i] == pGroup)
			break;
	}
	if(!powertrees_Load_GroupValidate(pGroup->pDefGroupNew,i,pGroup->pdoc->pDefNew))
		ErrorFilenamef(pdoc->pDefNew->pchFile,"Power Group #%d DID NOT PASS validation",i);
	else
		Errorf("Power Group #%d passed validation",i);
}
static void PTRevertGroup(PTGroupEdit *pGroup)
{
	PTEditDoc *pdoc;
	PTGroupDef *pNew;

	if(!pGroup || !pGroup->pDefGroupNew)
		return;
	pdoc = pGroup->pdoc;
	if (!PTCanEdit(pGroup->pdoc))
		return;

	eaFindAndRemove(&pdoc->pDefNew->ppGroups,pGroup->pDefGroupNew);

	StructDestroy(parse_PTGroupDef,pGroup->pDefGroupNew);
	eaFindAndRemove(&pdoc->ppGroupWindows,pGroup);
	ui_ScrollAreaRemoveChild(pdoc->pScrollArea,(UIWidget *)pGroup->pWin);

	pNew = StructCreate(parse_PTGroupDef);

	StructCopyFields(parse_PTGroupDef,pGroup->pDefGroupOrig,pNew,0,0);

	pGroup->pDefGroupNew = pNew;

	eaPush(&pdoc->pDefNew->ppGroups,pGroup->pDefGroupNew);

	PTResizeGroupWin(pGroup);

	PTCreateGroupWindow(pGroup->pDefGroupNew,pGroup->pDefGroupOrig,pdoc);	
}
static void PTDeleteGroup(PTGroupEdit *pGroup, bool bForce)
{
	bool bIsTalentTree;
	PTEditDoc *pdoc;
	int i;

	if(!pGroup || !pGroup->pDefGroupNew)
		return;
	bIsTalentTree = pGroup->pDefGroupNew->iUIGridRow > 0;
	pdoc = pGroup->pdoc;

	if (!bForce && !PTCanEdit(pGroup->pdoc))
		return;	

	eaFindAndRemove(&pdoc->ppGroupWindows,pGroup);
	eaFindAndRemove(&pdoc->pDefNew->ppGroups,pGroup->pDefGroupNew);

	if (!bIsTalentTree)
	{
		ui_ScrollAreaRemoveChild(pdoc->pScrollArea,(UIWidget *)pGroup->pWin);

		for(i=0;i<eaSize(&pGroup->ppConnections);i++)
		{
			if(pGroup->ppConnections[i]->pUILabel)
			{
				ui_ScrollAreaRemoveChild(pdoc->pScrollArea,(UIWidget *)pGroup->ppConnections[i]->pUILabel);
				ui_WidgetQueueFreeAndNull(&pGroup->ppConnections[i]->pUILabel);
			}
		}

		ui_WidgetQueueFreeAndNull(&pGroup->pWin);
	}

	StructDestroySafe(parse_PTGroupDef,&pGroup->pDefGroupNew);
	if(pdoc->pFocusedGrp == pGroup)
	{
		ui_ExpanderGroupRemoveExpander(pdoc->pExpGrp,pGroup->pExp);
		if (!bIsTalentTree)
			PTGroupWinFocus(NULL,pdoc);
	}
	
	s_bRedrawGroups = true;

	PTCreateGroupList(pdoc);

	PTPermaDirty(pdoc);
}

// If the iRowIndex is a value greater than 0, this group is a talent tree group.
// Otherwise it's a regular group
static void PTCreateNewGroup(PTEditDoc *pdoc, S8 iRowIndex)
{
	char achName[MAX_POWER_NAME_LEN];
	PTGroupDef *pnewDef = StructCreate(parse_PTGroupDef);
	PTGroupDef *porigDef = NULL;
	char *pchNameFull = NULL;
	StructInit(parse_PTGroupDef,pnewDef);

	if (!PTCanEdit(pdoc))
		return;

	langMakeEditorCopy(parse_PTGroupDef,pnewDef,true);
	sprintf(achName,"Group%d",iRowIndex > 0 ? iRowIndex : eaSize(&pdoc->pDefNew->ppGroups));
	//pnewDef->pchDisplayName = StructAllocString(achName);
	pnewDef->pDisplayMessage.pEditorCopy->pcDefaultString = StructAllocString(achName);
	pnewDef->pchGroup = StructAllocString(achName);
	if (iRowIndex > 0)
	{
		// Get selected power table name and assign it to the group
		const char *pchSelectedPointTableForTree = ui_TextEntryGetText(pdoc->pTalentTreePowerTableTextEntry);
		if (pchSelectedPointTableForTree == NULL)
			pchSelectedPointTableForTree = "";

		// Set the table name
		pnewDef->pRequires->pchTableName = strdup(pchSelectedPointTableForTree);

		// Set the row index
		pnewDef->iUIGridRow = iRowIndex;		
		
	}
	else
	{
		pnewDef->x = pdoc->pScrollArea->widget.sb->xpos + 600;
		pnewDef->y = pdoc->pScrollArea->widget.sb->ypos + 600;
	}

	estrStackCreate(&pchNameFull);
	estrPrintf(&pchNameFull,"%s.%s",pdoc->pDefNew->pchName,achName);
	pnewDef->pchNameFull = StructAllocString(pchNameFull);
	estrDestroy(&pchNameFull);

	eaPush(&pdoc->pDefNew->ppGroups,pnewDef);

	PTCreateGroupWindow(pnewDef, porigDef, pdoc);
} 
static void PTValidateTree(PTEditDoc *pdoc)
{
	if(!powertrees_Load_TreeValidate(pdoc->pDefNew))
		ErrorFilenamef(pdoc->pDefNew->pchFile,"Power Tree DID NOT PASS Validation");
	else
		Errorf("Power Tree passed Validation");
}
static void PTRevertTree(PTEditDoc *pdoc)
{
	int i;

	for(i=0;i<eaSize(&pdoc->ppTreeFields);i++)
	{
		PTUpdateField(pdoc->ppTreeFields[i],true);
	}
	
	for(i=eaSize(&pdoc->ppGroupWindows)-1;i>=0;i--)
	{
		PTDeleteGroup(pdoc->ppGroupWindows[i], true);
	}

	for(i=eaSize(&pdoc->pDefOrig->ppGroups)-1;i>=0;i--)
	{
		PTGroupDef *pNew;

		pNew = StructCreate(parse_PTGroupDef);
		StructCopyFields(parse_PTGroupDef,pdoc->pDefOrig->ppGroups[i],pNew,0,0);

		PTCreateGroupWindow(pNew,pdoc->pDefOrig->ppGroups[i],pdoc);
	}

}
static void PTButtonValidateGroup(UIButton *pButton, PTGroupEdit *pGroup)
{
	if(pGroup)
		PTValidateGroup(pGroup);
}
static void PTButtonRevertGroup(UIButton *pButton, PTGroupEdit *pGroup)
{
	if(pGroup)
		PTRevertGroup(pGroup);
}
static void PTButtonDeleteGroup(UIButton *pButton, PTGroupEdit *pGroup)
{
	if(pGroup)
		PTDeleteGroup(pGroup, false);
}
static void PTButtonNewGroup(UIButton *pButton, PTEditDoc *pdoc)
{
	PTCreateNewGroup(pdoc, 0);
}

static void PTButtonSave(UIButton *pbutton, PTEditDoc *pdoc)
{
	emSaveDoc((EMEditorDoc*)pdoc);
}
static void PTButtonValidate(UIButton *pbutton, PTEditDoc *pdoc)
{
	PTValidateTree(pdoc);
}
static void PTButtonRevert(UIButton *pbutton, PTEditDoc *pdoc)
{
	PTRevertTree(pdoc);
}
static void PTButtonCheckOut(UIButton *pbutton, PTEditDoc *pdoc)
{
	if (pdoc->pDefOrig)
	{
		ResourceInfo *info = resGetInfo(g_hPowerTreeDefDict, pdoc->pDefOrig->pchName);
		if (info && !resIsWritable(info->resourceDict, info->resourceName))
		{
			emQueuePrompt(EMPROMPT_CHECKOUT, &pdoc->emDoc, NULL, g_hPowerTreeDefDict, pdoc->pDefOrig->pchName);
		}
	}
}

// Returns the icon name for the given power tree node def
static const char * PTTalentTreeGetNodeIconName(PTNodeDef *pNodeDef)
{
	PowerDef *pPowerDef;
	if (pNodeDef == NULL || eaSize(&pNodeDef->ppRanks) == 0 || pNodeDef->ppRanks[0] == NULL)
		return NULL;

	pPowerDef = GET_REF(pNodeDef->ppRanks[0]->hPowerDef);

	if (pPowerDef == NULL)
		return NULL;

	return pPowerDef->pchIconName ? pPowerDef->pchIconName : NULL;
}

// Returns the name for the given power tree node def
static const char * PTTalentTreeGetNodeName(PTNodeDef *pNodeDef)
{
	return pNodeDef->pchNameFull;
}

// Finds the node with the given row and column in the talent tree
static PTNodeDef *PTTalentTreeFindNodeByRowAndColumn(PTEditDoc *pDoc, S8 iRow, S8 iCol)
{
	S32 i, j;

	// Validate
	devassert(pDoc);
	devassert(iRow >= 0 && iCol >= 0);

	for (i = 0; i < eaSize(&pDoc->ppGroupWindows); i++)
	{
		for(j = 0; j < eaSize(&pDoc->ppGroupWindows[i]->ppNodeEdits); j++)
		{
			PTNodeDef *pNodeDef = pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew;

			if(pNodeDef && pNodeDef->iUIGridRow == iRow && pNodeDef->iUIGridColumn == iCol)
				return pNodeDef;
		}
	}

	return NULL;
}

// Finds the node edit with the given row and column in the talent tree
static PTNodeEdit *PTTalentTreeFindNodeEditByRowAndColumn(PTEditDoc *pDoc, S8 iRow, S8 iCol)
{
	S32 i, j;

	// Validate
	devassert(pDoc);
	devassert(iRow >= 0 && iCol >= 0);

	for (i = 0; i < eaSize(&pDoc->ppGroupWindows); i++)
	{
		for(j = 0; j < eaSize(&pDoc->ppGroupWindows[i]->ppNodeEdits); j++)
		{
			PTNodeDef *pNodeDef = pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew;

			if(pNodeDef && pNodeDef->iUIGridRow == iRow && pNodeDef->iUIGridColumn == iCol)
				return pDoc->ppGroupWindows[i]->ppNodeEdits[j];
		}
	}

	return NULL;
}

// Returns the number of nodes associated with the given node
static S8 PTTalentTreeGetNodeCountByRow(PTEditDoc *pDoc, S8 iRow)
{
	S32 i, j, iNodeCount = 0;

	// Validate
	devassert(pDoc);
	devassert(iRow >= 0);

	for (i = 0; i < eaSize(&pDoc->ppGroupWindows); i++)
	{
		for(j = 0; j < eaSize(&pDoc->ppGroupWindows[i]->ppNodeEdits); j++)
		{
			PTNodeDef *pNodeDef = pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew;

			if(pNodeDef && pNodeDef->iUIGridRow == iRow)
			{
				iNodeCount++;
			}
		}
	}

	return iNodeCount;
}

// Finds the group with the given row in the talent tree
static PTGroupDef *PTTalentTreeFindGroupByRow(PTEditDoc *pDoc, S8 iRow)
{
	S32 i;

	// Validate
	devassert(pDoc);
	devassert(iRow >= 0);

	for (i = 0; i < eaSize(&pDoc->ppGroupWindows); i++)
	{
		if (pDoc->ppGroupWindows[i]->pDefGroupNew->iUIGridRow == iRow)
			return pDoc->ppGroupWindows[i]->pDefGroupNew;
	}

	return NULL;
}

// Finds the group edit with the given row in the talent tree
static PTGroupEdit *PTTalentTreeFindGroupEditByRow(PTEditDoc *pDoc, S8 iRow)
{
	S32 i;

	// Validate
	devassert(pDoc);
	devassert(iRow >= 0);

	for (i = 0; i < eaSize(&pDoc->ppGroupWindows); i++)
	{
		if (pDoc->ppGroupWindows[i]->pDefGroupNew->iUIGridRow == iRow)
			return pDoc->ppGroupWindows[i];
	}

	return NULL;
}

// Displays the row details
static void PTTalentTreeFocusRow(PTEditDoc *pDoc, S8 iRow)
{
	int i;

	devassert(pDoc);

	if (iRow < 1)
	{
		pDoc->pFocusedGrp = NULL;

		// Disable all buttons
		for(i=0; i < 3; i++)
		{
			if (pDoc->pGroupBtns[i])
			{
				pDoc->pGroupBtns[i]->clickedData = NULL;
				ui_SetActive(UI_WIDGET(pDoc->pGroupBtns[i]), false);
			}
		}
		return;
	}

	if(pDoc->pFocusedGrp)
	{
		// Already focused
		if (pDoc->pFocusedGrp->pDefGroupNew->iUIGridRow == iRow)
			return;

		// Un-focus the previously focused one
		ui_ExpanderGroupRemoveExpander(pDoc->pExpGrp, pDoc->pFocusedGrp->pExp);

		// Disable all buttons
		for(i=0; i < 3; i++)
		{
			if (pDoc->pGroupBtns[i])
			{
				pDoc->pGroupBtns[i]->clickedData = NULL;
				ui_SetActive(UI_WIDGET(pDoc->pGroupBtns[i]), false);
			}
		}
	}

	// Set the focused group
	pDoc->pFocusedGrp = PTTalentTreeFindGroupEditByRow(pDoc, iRow);

	if(!pDoc->pFocusedGrp)
		return;

	// Enable all buttons
	for(i=0; i<3; i++)
	{
		if (pDoc->pGroupBtns[i])
		{
			pDoc->pGroupBtns[i]->clickedData = pDoc->pFocusedGrp;
			ui_SetActive(UI_WIDGET(pDoc->pGroupBtns[i]), true);
		}
	}

	ui_ExpanderGroupAddExpander(pDoc->pExpGrp, pDoc->pFocusedGrp->pExp);
}

// Frees up the user data used in talent tree button
static void PTFreeTalentTreeButtonData(UIButton* pButton)
{
	if (pButton && pButton->clickedData)
	{
		free(pButton->clickedData);
	}
}

// Updates the icon for the talent tree button
void PTTalentTreeButtonUpdateIcon(PTEditDoc *pDoc, PTNodeDef *pNode, const char * pchIconName)
{
	UIButton *pButton;

	devassert(pDoc);
	devassert(pNode);

	// Find the button
	pButton = (UIButton *)ui_WidgetFindChild(UI_WIDGET(pDoc->pScrollArea), PTGetTalentTreeButtonName(pNode->iUIGridRow, pNode->iUIGridColumn));

	if (pButton)
	{
		if (pchIconName == NULL || pchIconName[0] == 0)
		{
			ui_ButtonClearImage(pButton);
		}
		else
		{
			ui_ButtonSetImage(pButton, pchIconName);
			ui_ButtonSetImageStretch(pButton, true);
			pButton->spriteInheritsColor = false;
		}		
	}
}

static void PTTalentTreeButtonSetColor(UIButton *pButton, bool bAssigned)
{
	devassert(pButton);
	UI_WIDGET(pButton)->pOverrideSkin = NULL;

	if (bAssigned)
	{		
		UI_WIDGET(pButton)->color[0].r = 255;
		UI_WIDGET(pButton)->color[0].g = 0;
		UI_WIDGET(pButton)->color[0].b = 0;
	}
}

// Creates a node on the selected grid position
static void PTOnNodeContextMenuNewNode(UIMenuItem *pMenuItem, UIButton *pTalentTreeButton)
{
	TalentTreeNodeData *pTalentTreeData;

	devassert(pTalentTreeButton);
	devassert(pTalentTreeButton->clickedData);

	pTalentTreeData = (TalentTreeNodeData *)pTalentTreeButton->clickedData;

	if (pTalentTreeData->pNodeEdit == NULL) // Not associated with a node
	{
		PTGroupEdit *pGroupEdit = PTTalentTreeFindGroupEditByRow(pTalentTreeData->pDoc, pTalentTreeData->iRow);		
		if (pGroupEdit)
		{
			PTNodeEdit *pNodeEdit;

			// Create a new node
			PTNewNode(pGroupEdit, pTalentTreeData->iRow, pTalentTreeData->iCol);

			pNodeEdit = PTTalentTreeFindNodeEditByRowAndColumn(pTalentTreeData->pDoc, pTalentTreeData->iRow, pTalentTreeData->iCol);

			if (pNodeEdit)
			{
				// Change the color of the button
				PTTalentTreeButtonSetColor(pTalentTreeButton, true);

				// Set the tooltip for the button
				if (PTTalentTreeGetNodeName(pNodeEdit->pDefNodeNew))
				{
					ui_WidgetSetTooltipString(UI_WIDGET(pTalentTreeButton), PTTalentTreeGetNodeName(pNodeEdit->pDefNodeNew));
				}

				pTalentTreeData->pNodeEdit = pNodeEdit;
			}
		}

		// Set the doc as dirty
		PTPermaDirty(pTalentTreeData->pDoc);
	}
}

// Removes the dependency on the node
static void PTOnNodeContextMenuRemoveDependency(UIMenuItem *pMenuItem, UIButton *pTalentTreeButton)
{
	TalentTreeNodeData *pTalentTreeData;

	devassert(pTalentTreeButton);
	devassert(pTalentTreeButton->clickedData);

	pTalentTreeData = (TalentTreeNodeData *)pTalentTreeButton->clickedData;

	if (pTalentTreeData->pNodeEdit)
	{
		REMOVE_HANDLE(pTalentTreeData->pNodeEdit->pDefNodeNew->hNodeRequire);

		// Set the doc as dirty
		PTPermaDirty(pTalentTreeData->pDoc);
	}
}

// Removes the selected node
static void PTOnNodeContextMenuDeleteNode(UIMenuItem *pMenuItem, UIButton *pTalentTreeButton)
{
	TalentTreeNodeData *pTalentTreeData;

	devassert(pTalentTreeButton);
	devassert(pTalentTreeButton->clickedData);

	pTalentTreeData = (TalentTreeNodeData *)pTalentTreeButton->clickedData;

	if (pTalentTreeData->pNodeEdit != NULL) // Button is associated with a node
	{
		PTNodeEdit *pNodeEdit;

		// Delete the node
		PTDeleteNode(pTalentTreeData->pNodeEdit);

		pNodeEdit = PTTalentTreeFindNodeEditByRowAndColumn(pTalentTreeData->pDoc, pTalentTreeData->iRow, pTalentTreeData->iCol);

		if (pNodeEdit == NULL)
		{
			// Change the color of the button
			PTTalentTreeButtonSetColor(pTalentTreeButton, false);

			// Clear the image
			ui_ButtonClearImage(pTalentTreeButton);

			pTalentTreeData->pNodeEdit = NULL;
		}
	}
}

// Called for context menu action on the talent tree button
static void PTOnTalentTreeButtonContext(UIButton *pTalentTreeButton, TalentTreeNodeData *pTalentTreeData)
{
	devassert(pTalentTreeData);

	if (s_pNodeContextMenu == NULL)
	{
		s_pNodeContextMenu = ui_MenuCreateWithItems("Node Menu",
			ui_MenuItemCreate("Create new node", UIMenuCallback, NULL, NULL, NULL),
			ui_MenuItemCreate("Remove dependency", UIMenuCallback, PTOnNodeContextMenuRemoveDependency, NULL, NULL),
			NULL);
	}

	s_pNodeContextMenu->items[0]->clickedData = pTalentTreeButton;
	s_pNodeContextMenu->items[1]->clickedData = pTalentTreeButton;

	if (pTalentTreeData->pNodeEdit == NULL)
	{

		s_pNodeContextMenu->items[0]->clickedF = PTOnNodeContextMenuNewNode;
		ui_MenuItemSetTextString( s_pNodeContextMenu->items[0], "Create new node" );
		s_pNodeContextMenu->items[0]->active = pTalentTreeData->pDoc->talentTreeMatrix[((pTalentTreeData->iRow - 1) * MAX_TALENT_TREE_COLS) + (pTalentTreeData->iCol - 1)];

		s_pNodeContextMenu->items[1]->active = false;
	}
	else
	{
		s_pNodeContextMenu->items[0]->clickedF = PTOnNodeContextMenuDeleteNode;
		ui_MenuItemSetTextString( s_pNodeContextMenu->items[0], "Delete this node" );
		s_pNodeContextMenu->items[0]->active = true;

		s_pNodeContextMenu->items[1]->active = IS_HANDLE_ACTIVE(pTalentTreeData->pNodeEdit->pDefNodeNew->hNodeRequire);
	}

	PTDisplayMenu(s_pNodeContextMenu);
}

// Called when a talent tree button is clicked
static void PTOnAssignedTalentTreeButtonClick(UIButton *pTalentTreeButton, TalentTreeNodeData *pTalentTreeData)
{
	PTNodeEdit *pNodeEdit;

	devassert(pTalentTreeData);

	// Select the row
	PTTalentTreeFocusRow(pTalentTreeData->pDoc, pTalentTreeData->iRow);

	pNodeEdit = PTTalentTreeFindNodeEditByRowAndColumn(pTalentTreeData->pDoc, pTalentTreeData->iRow, pTalentTreeData->iCol);

	if (pNodeEdit)
	{
		// Select the node clicked
		ui_ExpanderSetOpened(pNodeEdit->pExp, true);
		PTExpanderSetHighlight(pNodeEdit->pExp);
		PTRankResize(pNodeEdit->pExp, pNodeEdit);	
	}
}

// Handles dragging a node button
static void PTTalentTreeDrag(UIButton *pButton, UserData data)
{
	TalentTreeNodeData *pTalentTreeData;
	devassert(pButton);
	devassert(pButton->clickedData);

	pTalentTreeData = (TalentTreeNodeData *)pButton->clickedData;

	// Only allow the nodes assigned to be dragged
	if (pTalentTreeData->pNodeEdit)
		ui_DragStart(UI_WIDGET(pButton), UI_DND_TALENT_TREE_NODE, pTalentTreeData->pNodeEdit, atlasLoadTexture("A_Button_38.tga"));
}

// Handles drops onto a talent tree button
static void PTTalentTreeDrop(UIButton *pSourceTalentTreeButton, UIButton *pDestinationTalentTreeButton, UIDnDPayload *pPayload, char *pchDummy)
{
	// Handle only node drops
	if (pPayload->type == UI_DND_TALENT_TREE_NODE)
	{
		S8 iRowIt, iRowStep = 0, iColIt, iColStep = 0;
		TalentTreeNodeData *pSourceTalentTreeData = (TalentTreeNodeData *)pSourceTalentTreeButton->clickedData;
		TalentTreeNodeData *pDestinationTalentTreeData = (TalentTreeNodeData *)pDestinationTalentTreeButton->clickedData;
		PTNodeDef *pSourceNodeDef;
		PTNodeDef *pDestinationNodeDef;

		// Validation: Drop on self
		if (pSourceTalentTreeButton == pDestinationTalentTreeButton)
		{
			return;
		}

		// Validation: Source and destination must have nodes assigned
		if (pSourceTalentTreeData->pNodeEdit == NULL || pDestinationTalentTreeData->pNodeEdit == NULL)
		{
			return;
		}

		pSourceNodeDef = pSourceTalentTreeData->pNodeEdit->pDefNodeNew;
		pDestinationNodeDef = pDestinationTalentTreeData->pNodeEdit->pDefNodeNew;

		// Validation: Make sure destination node has no dependency
		if (IS_HANDLE_ACTIVE(pDestinationNodeDef->hNodeRequire))
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "Destination node must not have any dependencies.");
			return;
		}

		// Validation: Circular dependency
		if (IS_HANDLE_ACTIVE(pSourceNodeDef->hNodeRequire) && stricmp(REF_STRING_FROM_HANDLE(pSourceNodeDef->hNodeRequire), pDestinationNodeDef->pchNameFull) == 0)
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "Source node already depends on the destination node. You cannot create this dependency.");
			return;
		}

		// Validation: Check if there is any node in the path that would prevent this connection
		// Move horizontally first
		if (pSourceTalentTreeData->iCol != pDestinationTalentTreeData->iCol)
		{
			iColStep = pSourceTalentTreeData->iCol > pDestinationTalentTreeData->iCol ? -1 : 1;
			for (iColIt = pSourceTalentTreeData->iCol + iColStep; 
				(iColStep > 0 && iColIt <= pDestinationTalentTreeData->iCol) || (iColStep < 0  && iColIt >= pDestinationTalentTreeData->iCol); 
				iColIt += iColStep)
			{
				if ((
					// Test all nodes until the column right before the destination
					iColIt != pDestinationTalentTreeData->iCol ||
					// Test the last node if it's not the destination
					(iColIt == pDestinationTalentTreeData->iCol && pSourceTalentTreeData->iRow != pDestinationTalentTreeData->iRow)
					) &&
					PTTalentTreeFindNodeEditByRowAndColumn(pSourceTalentTreeData->pDoc, pSourceTalentTreeData->iRow, iColIt))
				{
					ui_DialogPopup(s_pTalentTreeValidationErrorString, "Cannot create dependency while there are nodes in the path");
					return;
				}
			}
		}

		// Now move vertically
		if (pSourceTalentTreeData->iRow != pDestinationTalentTreeData->iRow)
		{
			iRowStep = pSourceTalentTreeData->iRow > pDestinationTalentTreeData->iRow ? -1 : 1;
			for (iRowIt = pSourceTalentTreeData->iRow + iRowStep; 
				(iRowStep > 0 && iRowIt < pDestinationTalentTreeData->iRow) || (iRowStep < 0  && iRowIt > pDestinationTalentTreeData->iRow); 
				iRowIt += iRowStep)
			{
				if (PTTalentTreeFindNodeEditByRowAndColumn(pSourceTalentTreeData->pDoc, iRowIt, pDestinationTalentTreeData->iCol))
				{
					ui_DialogPopup(s_pTalentTreeValidationErrorString, "Cannot create dependency while there are nodes in the path");
					return;
				}
			}
		}

		// Everything is valid set dependency
		REMOVE_HANDLE(pDestinationNodeDef->hNodeRequire);
		SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pSourceNodeDef->pchNameFull, pDestinationNodeDef->hNodeRequire);

		PTPermaDirty(pSourceTalentTreeData->pDoc);
	}
}

static const char * PTGetTalentTreeButtonName(S8 iRow, S8 iCol)
{
	static char talentTreeButtonName[25];

	devassert(iRow > 0 && iCol > 0);
	
	sprintf(talentTreeButtonName, "TalentTreeButton_%d_%d", iRow, iCol);

	return talentTreeButtonName;
}

static const char * PTGetTalentTreeRowEditButtonName(S8 iRow)
{
	static char buttonName[25];

	devassert(iRow > 0);

	sprintf(buttonName, "TalentTreeRowEditButton_%d", iRow);

	return buttonName;
}

static bool PTTalentTreeGetNodeCenterPoint(PTEditDoc *pDoc, S8 iRow, S8 iCol, S32 *pX, S32 *pY)
{
	UIButton *pButton;
	TalentTreeNodeData *pTalentTreeData;
	devassert(iRow > 0 && iCol > 0);

	if (pDoc->pScrollArea == NULL)
		return false;

	pButton = (UIButton *)ui_WidgetFindChild(UI_WIDGET(pDoc->pScrollArea), PTGetTalentTreeButtonName(iRow, iCol));

	if (pButton == NULL)
		return false;

	devassert(pButton->clickedData);

	pTalentTreeData = (TalentTreeNodeData *)pButton->clickedData;

	*pX = pTalentTreeData->fScreenCenterX;
	*pY = pTalentTreeData->fScreenCenterY;

	return true;
}

static void PTTalentTreeDrawArrowEndCap(S32 iXFrom, S32 iYFrom, S32 iXTo, S32 iYTo)
{
	Color lineColor = { 0, 0, 0, 255 };
	Vec2 vecFrom = { iXFrom, iYFrom };
	Vec2 vecTo = { iXTo, iYTo };
	Vec2 vecDiff;
	Vec2 vecEndPoint1, vecEndPoint2;

	// Calculate the difference
	subVec2(vecTo, vecFrom, vecDiff);

	// Rotate 30 degrees
	vecEndPoint1[0] = vecDiff[0] * cos(RAD(30.f)) - vecDiff[1] * sin(RAD(30.f));
	vecEndPoint1[1] = vecDiff[0] * sin(RAD(30.f)) + vecDiff[1] * cos(RAD(30.f));
	addVec2(vecTo, vecEndPoint1, vecEndPoint1);

	// Rotate 330 degrees
	vecEndPoint2[0] = vecDiff[0] * cos(RAD(330.f)) - vecDiff[1] * sin(RAD(330.f));
	vecEndPoint2[1] = vecDiff[0] * sin(RAD(330.f)) + vecDiff[1] * cos(RAD(330.f));
	addVec2(vecTo, vecEndPoint2, vecEndPoint2);

	// Draw the lines
	gfxDrawLine(vecEndPoint1[0], vecEndPoint1[1], TALENT_TREE_NODE_LINE_Z, iXTo, iYTo, lineColor);
	gfxDrawLine(vecEndPoint2[0], vecEndPoint2[1], TALENT_TREE_NODE_LINE_Z, iXTo, iYTo, lineColor);
}

static bool PTTalentTreeDrawArrowBetweenNodes(PTEditDoc *pDoc, S8 iRowFrom, S8 iColFrom, S8 iRowTo, S8 iColTo)
{
	bool bDrawArrowEndPoint = false;
	bool bDidDrawHorizontalLine = false;
	S32 iXPosFrom, iYPosFrom, iXPosTo, iYPosTo;
	Color lineColor = { 0, 0, 0, 255 }; // Black lines

	iXPosFrom = iYPosFrom = iXPosTo = iYPosTo = 0;

	devassert(iRowFrom > 0 && iColFrom > 0 && iRowTo > 0 && iColTo > 0);
	devassert(iRowFrom <= MAX_TALENT_TREE_ROWS && iColFrom <= MAX_TALENT_TREE_COLS && iRowTo <= MAX_TALENT_TREE_ROWS && iColTo <= MAX_TALENT_TREE_COLS);

	// Do we need to go left or right (We always go left or right first)
	if (iColFrom != iColTo)
	{
		// Are we drawing a line towards right side of the grid
		bool bLineTowardsRight = iColFrom < iColTo;

		// Is this the last line we are drawing
		bDrawArrowEndPoint = iRowFrom == iRowTo;

		// Get the button positions
		if (!PTTalentTreeGetNodeCenterPoint(pDoc, iRowFrom, iColFrom, &iXPosFrom, &iYPosFrom))
			return false;
		if (!PTTalentTreeGetNodeCenterPoint(pDoc, iRowFrom, iColTo, &iXPosTo, &iYPosTo))
			return false;
		if (bLineTowardsRight)
		{
			iXPosFrom += (TALENT_TREE_BUTTON_WIDTH / 2);
			if (bDrawArrowEndPoint)
				iXPosTo -= (TALENT_TREE_BUTTON_WIDTH / 2);
		}
		else
		{
			iXPosFrom -= (TALENT_TREE_BUTTON_WIDTH / 2);
			if (bDrawArrowEndPoint)
				iXPosTo += (TALENT_TREE_BUTTON_WIDTH / 2);
		}

		// Draw the line
		gfxDrawLine(iXPosFrom, iYPosFrom, TALENT_TREE_NODE_LINE_Z, iXPosTo, iYPosTo, lineColor);

		bDidDrawHorizontalLine = true;

		// Update iColFrom
		iColFrom = iColTo;

		// Draw the end point
		if (bDrawArrowEndPoint)
		{
			PTTalentTreeDrawArrowEndCap(bLineTowardsRight ? iXPosTo + TALENT_TREE_ARROW_DISTANCE : iXPosTo - TALENT_TREE_ARROW_DISTANCE, iYPosFrom, iXPosTo, iYPosTo);
		}
	}

	// Draw vertical line if necessary
	if (iRowFrom != iRowTo)
	{
		// Are we drawing a line towards the top side of the grid
		bool bLineTowardsUp = iRowFrom > iRowTo;

		// Get the next destination position
		if (!PTTalentTreeGetNodeCenterPoint(pDoc, iRowFrom, iColFrom, &iXPosFrom, &iYPosFrom))
			return false;
		if (!PTTalentTreeGetNodeCenterPoint(pDoc, iRowTo, iColTo, &iXPosTo, &iYPosTo))
			return false;

		if (bLineTowardsUp)
		{
			if (!bDidDrawHorizontalLine)
				iYPosFrom -= (TALENT_TREE_BUTTON_HEIGHT / 2);
			iYPosTo += (TALENT_TREE_BUTTON_HEIGHT / 2);
		}
		else
		{
			if (!bDidDrawHorizontalLine)
				iYPosFrom += (TALENT_TREE_BUTTON_HEIGHT / 2);
			iYPosTo -= (TALENT_TREE_BUTTON_HEIGHT / 2);
		}

		// Draw the line
		gfxDrawLine(iXPosFrom, iYPosFrom, TALENT_TREE_NODE_LINE_Z, iXPosTo, iYPosTo, lineColor);

		// Draw the end point
		PTTalentTreeDrawArrowEndCap(iXPosTo, bLineTowardsUp ? iYPosTo - TALENT_TREE_ARROW_DISTANCE : iYPosTo + TALENT_TREE_ARROW_DISTANCE, iXPosTo, iYPosTo);
	}

	return true;
}

static void PTTalentTreeButtonTick(UIButton *pButton, UI_PARENT_ARGS)
{
	TalentTreeNodeData *pTalentTreeData = (TalentTreeNodeData *)pButton->clickedData;
	UI_GET_COORDINATES(pButton);
	devassert(pTalentTreeData);
	pTalentTreeData->fScreenCenterX = x + (w / 2);
	pTalentTreeData->fScreenCenterY = y + (h / 2);

	if (pTalentTreeData->pNodeEdit)
	{
		const char *pchIconName = PTTalentTreeGetNodeIconName(pTalentTreeData->pNodeEdit->pDefNodeNew);
		if (pchIconName && pchIconName[0])
		{
			PTTalentTreeButtonUpdateIcon(pTalentTreeData->pDoc, pTalentTreeData->pNodeEdit->pDefNodeNew, pchIconName);
		}
	}

	// Call the original tick function
	ui_ButtonTick(pButton, UI_PARENT_VALUES);
}

// Creates a talent tree button
static UIButton * PTCreateTalentTreeButton(const char *pchLabel, F32 fX, F32 fY, F32 fWidth, F32 fHeight,
									 UIActivationFunc pClickCallback, TalentTreeNodeData *pTalentTreeData)
{
	UIButton *pButton = NULL;
	const char *pchIconName = NULL;
	const char *pchTooltip = NULL;

	devassert(pTalentTreeData);
	devassert(pchLabel && pchLabel[0]);

	if (pTalentTreeData->pNodeEdit)
	{
		pchIconName = PTTalentTreeGetNodeIconName(pTalentTreeData->pNodeEdit->pDefNodeNew);
		pchTooltip = PTTalentTreeGetNodeName(pTalentTreeData->pNodeEdit->pDefNodeNew);
	}

	if (pchIconName)
	{
		pButton = ui_ButtonCreateImageOnly(pchIconName, fX, fY, pClickCallback, pTalentTreeData);
		pButton->spriteInheritsColor = false;
		ui_ButtonSetImageStretch( pButton, true );		
	}
	else
	{
		pButton = ui_ButtonCreate(pchLabel, fX, fY, pClickCallback, pTalentTreeData);
	}

	if (pTalentTreeData->pNodeEdit)
	{
		// Change the color of the button
		PTTalentTreeButtonSetColor(pButton, true);
	}

	// Set dimensions
	ui_WidgetSetDimensions(UI_WIDGET(pButton), fWidth, fHeight);

	// Set the callback for freeing the widget
	ui_WidgetSetFreeCallback(UI_WIDGET(pButton), PTFreeTalentTreeButtonData);

	// Set the callback for dropping data on this widget
	ui_WidgetSetDropCallback(UI_WIDGET(pButton), PTTalentTreeDrop, NULL);

	// Set the callback for dragging this node
	ui_WidgetSetDragCallback(UI_WIDGET(pButton), PTTalentTreeDrag, NULL);

	// Set the tooltip
	if (pchTooltip)
		ui_WidgetSetTooltipString(UI_WIDGET(pButton), pchTooltip);

	// Set a name for this button for easy access
	ui_WidgetSetName(UI_WIDGET(pButton), PTGetTalentTreeButtonName(pTalentTreeData->iRow, pTalentTreeData->iCol));

	// Set the callback for context menu
	ui_WidgetSetContextCallback(UI_WIDGET(pButton), PTOnTalentTreeButtonContext, pTalentTreeData);

	// Set the tick function
	UI_WIDGET(pButton)->tickF = PTTalentTreeButtonTick;

	return pButton;
}

// Sets the active state of the buttons in a row
static void PTToggleRowButtons(PTEditDoc *pDoc, S8 iRow, bool bActive)
{
	S8 i;
	UIButton *pButton;

	for (i = 1; i <= MAX_TALENT_TREE_COLS; i++)
	{
		pButton = (UIButton *)ui_WidgetFindChild(UI_WIDGET(pDoc->pScrollArea), PTGetTalentTreeButtonName(iRow, i));
		
		devassert(pButton);

		if (pButton)
		{
			ui_SetActive(UI_WIDGET(pButton), bActive);
		}
	}	
}

// Called when the + or - button next to the row is freed
static void PTOnFreeRowEditButton(UIButton* pButton)
{
	if (pButton && pButton->clickedData)
		free(pButton->clickedData);
}

// Called when the + or - button next to the row is clicked
static void PTOnRowEditClick(UIButton* button, TalentTreeRowEditButtonData *pRowEditButtonData)
{
	devassert(pRowEditButtonData);
	devassert(pRowEditButtonData->iRow > 0);

	if (pRowEditButtonData->bAdd) // Add a new row
	{
		// Validation: Row should not be already activated
		if (PTTalentTreeFindGroupByRow(pRowEditButtonData->pDoc, pRowEditButtonData->iRow))
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "This row is already activated");
			return;
		}

		// Validation: Row above this must be already created
		if (pRowEditButtonData->iRow > 1 && 
			PTTalentTreeFindGroupByRow(pRowEditButtonData->pDoc, pRowEditButtonData->iRow - 1) == NULL)
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "You cannot activate a row unless the row above it is activated.");
			return;
		}

		// Validation: Is the document editable
		if (!PTCanEdit(pRowEditButtonData->pDoc))
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "Current document is not editable.");
			return;
		}

		// Create the group
		PTCreateNewGroup(pRowEditButtonData->pDoc, pRowEditButtonData->iRow);
		
		// Activate the row
		PTToggleRowButtons(pRowEditButtonData->pDoc, pRowEditButtonData->iRow, true);

		// Change the button mode
		pRowEditButtonData->bAdd = false;

		// Set the button text
		ui_ButtonSetText(button, "-");
	}
	else // Delete a row
	{
		// Validation: Is the document editable
		if (!PTCanEdit(pRowEditButtonData->pDoc))
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "Current document is not editable.");
			return;
		}

		// Validation: Row should already be activated
		if (PTTalentTreeFindGroupByRow(pRowEditButtonData->pDoc, pRowEditButtonData->iRow) == NULL)
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "This row is not activated.");
			return;
		}

		// Validation: There should not be any nodes assigned to this row
		if (PTTalentTreeGetNodeCountByRow(pRowEditButtonData->pDoc, pRowEditButtonData->iRow) > 0)
		{
			ui_DialogPopup(s_pTalentTreeValidationErrorString, "You cannot deactivate a row while there are nodes associated to it.");
			return;
		}

		// Validation: Any row below this row should not be activated
		if (pRowEditButtonData->iRow < MAX_TALENT_TREE_ROWS)
		{
			S8 i = pRowEditButtonData->iRow + 1;
			for (; i <= MAX_TALENT_TREE_ROWS; i++)
			{
				if (PTTalentTreeFindGroupByRow(pRowEditButtonData->pDoc, i))
				{
					ui_DialogPopup(s_pTalentTreeValidationErrorString, "You cannot deactivate a row until you deactivate all rows below it.");
					return;
				}
			}
		}

		PTDeleteGroup(PTTalentTreeFindGroupEditByRow(pRowEditButtonData->pDoc, pRowEditButtonData->iRow), false);

		// Deactivate the row
		PTToggleRowButtons(pRowEditButtonData->pDoc, pRowEditButtonData->iRow, false);

		// Change the button mode
		pRowEditButtonData->bAdd = true;

		// Set the button text
		ui_ButtonSetText(button, "+");

		// Do not focus on any group
		PTTalentTreeFocusRow(pRowEditButtonData->pDoc, 0);
	}
}

// Creates the talent tree window
static void PTCreateTalentTreeWindow(PTEditDoc *pEditDoc, S8 uiNumRows, S8 uiNumCols,
										  U32 uiButtonWidth, U32 uiButtonHeight,
										  U32 uiSpacingHorizontal, U32 uiSpacingVertical)
{
	UIButton *pButton;
	S8 i, j;
	F32 fWinWidth, fWinHeight, fXPos, fYPos;
	static char pchButtonText[11];
	PTNodeDef *pAssignedNodeForCurrentPosition;
	PTGroupDef *pGroupDefForCurrentRow;
	TalentTreeRowEditButtonData *pRowEditButtonData;

	// Validate
	devassert(uiNumRows > 0 && uiNumCols > 0 && uiButtonWidth > 0 && uiButtonHeight > 0);

	// Calculate the width and height of the window
	fWinWidth = (uiNumCols * uiButtonWidth) + ((uiNumCols + 1) * uiSpacingHorizontal);
	fWinHeight = (uiNumRows * uiButtonHeight) + ((uiNumRows + 1) * uiSpacingVertical);

	// Place all grid buttons
	for (i = 1; i <= uiNumRows; i++)
	{
		// Reset X position
		fXPos = 0.0f;
		// Move along Y
		fYPos = TALENT_TREE_BUTTON_START_Y_POS + ((i - 1) * (uiButtonHeight + uiSpacingVertical)) + uiSpacingVertical;

		// Get the group for this row
		pGroupDefForCurrentRow = PTTalentTreeFindGroupByRow(pEditDoc, i);

		// Place a button for inserting/deleting a row
		pRowEditButtonData = calloc(sizeof(TalentTreeRowEditButtonData), 1);
		pRowEditButtonData->pDoc = pEditDoc;
		pRowEditButtonData->bAdd = !pGroupDefForCurrentRow;
		pRowEditButtonData->iRow = i;
		pButton = ui_ButtonCreate(pGroupDefForCurrentRow ? "-" : "+", fXPos + 10.f, fYPos + 15.f, PTOnRowEditClick, pRowEditButtonData);
		ui_WidgetSetDimensions(UI_WIDGET(pButton), 30.f, 30.f);
		ui_WidgetSetName(UI_WIDGET(pButton), PTGetTalentTreeRowEditButtonName(i));
		ui_WidgetSetFreeCallback(UI_WIDGET(pButton), PTOnFreeRowEditButton);
		ui_ScrollAreaAddChild(pEditDoc->pScrollArea, pButton);

		// Move to the button start position
		fXPos = TALENT_TREE_BUTTON_START_X_POS;

		for (j = 1; j <= uiNumCols; j++)
		{

			// User data for the button which defines the position of the button
			TalentTreeNodeData *pTalentTreeNodeData = calloc(1, sizeof(TalentTreeNodeData));
			pTalentTreeNodeData->pDoc = pEditDoc;
			pTalentTreeNodeData->iRow = i;
			pTalentTreeNodeData->iCol = j;
			pTalentTreeNodeData->pNodeEdit = PTTalentTreeFindNodeEditByRowAndColumn(pEditDoc, i, j);
			
			// Add the horizontal spacing
			fXPos += uiSpacingHorizontal;

			sprintf(pchButtonText, "(%d, %d)", i, j);

			pAssignedNodeForCurrentPosition = PTTalentTreeFindNodeByRowAndColumn(pEditDoc, i, j);

			if (pAssignedNodeForCurrentPosition)
			{
				pButton = PTCreateTalentTreeButton(pchButtonText, fXPos, fYPos, 
					uiButtonWidth, uiButtonHeight, PTOnAssignedTalentTreeButtonClick, pTalentTreeNodeData);
			}
			else
			{
				pButton = PTCreateTalentTreeButton(pchButtonText, fXPos, fYPos, 
					uiButtonWidth, uiButtonHeight, PTOnAssignedTalentTreeButtonClick, pTalentTreeNodeData);
			}

			// Disable the button
			ui_SetActive(UI_WIDGET(pButton), !pRowEditButtonData->bAdd);

			ui_ScrollAreaAddChild(pEditDoc->pScrollArea, pButton);

			// Move along X
			fXPos += uiButtonWidth;
		}

		if (pGroupDefForCurrentRow)
		{
			// Activate the row
			PTToggleRowButtons(pRowEditButtonData->pDoc, pRowEditButtonData->iRow, true);
		}
	}

	pEditDoc->bTalentTreeGridCreated = true;
}

void PTTalentAreaDrawArrows(PTEditDoc *pDoc)
{
	S32 i, j;

	// Clean the matrix
	memset(pDoc->talentTreeMatrix, true, sizeof(pDoc->talentTreeMatrix));

	for(i = 0; i < eaSize(&pDoc->ppGroupWindows);i++)
	{
		for(j = 0; j < eaSize(&pDoc->ppGroupWindows[i]->ppNodeEdits); j++)
		{
			if (IS_HANDLE_ACTIVE(pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew->hNodeRequire))
			{
				// Found a node with dependency
				PTNodeDef *pChildNodeDef = pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew;
				PTNodeEdit *pParentNodeEdit = PTFindNode(pDoc, REF_STRING_FROM_HANDLE(pDoc->ppGroupWindows[i]->ppNodeEdits[j]->pDefNodeNew->hNodeRequire));
				if (pParentNodeEdit)
				{
					S8 iRowFrom, iColFrom, iRowTo, iColTo, iRowStep, iColStep, iRowIt, iColIt;
					iRowFrom = pParentNodeEdit->pDefNodeNew->iUIGridRow;
					iColFrom = pParentNodeEdit->pDefNodeNew->iUIGridColumn;
					iRowTo = pChildNodeDef->iUIGridRow;
					iColTo = pChildNodeDef->iUIGridColumn;

					// Draw the arrow between nodes
					PTTalentTreeDrawArrowBetweenNodes(pDoc, iRowFrom, iColFrom, iRowTo, iColTo);

					// Mark each grid position as unavailable in the path
					pDoc->talentTreeMatrix[((iRowFrom - 1) * MAX_TALENT_TREE_COLS) + (iColFrom - 1)] = false;
					pDoc->talentTreeMatrix[((iRowTo - 1) * MAX_TALENT_TREE_COLS) + (iColTo - 1)] = false;

					// Move horizontally first
					iColStep = iColFrom < iColTo ? 1 : -1;
					for (iColIt = iColFrom; (iColStep > 0 && iColIt <= iColTo) || (iColStep < 0 && iColIt >= iColTo); iColIt += iColStep)
					{
						pDoc->talentTreeMatrix[((iRowFrom - 1) * MAX_TALENT_TREE_COLS) + (iColIt - 1)] = false;
					}
					// Move vertically now
					iRowStep = iRowFrom < iRowTo ? 1 : -1;
					for (iRowIt = iRowFrom; (iRowStep > 0 && iRowIt <= iRowTo) || (iRowStep < 0 && iRowIt >= iRowTo); iRowIt += iRowStep)
					{
						pDoc->talentTreeMatrix[((iRowIt - 1) * MAX_TALENT_TREE_COLS) + (iColTo - 1)] = false;
					}

				}
			}
		}
	}
}

void PTTalentTreeScrollAreaDraw(UIScrollArea *pScrollArea, UI_PARENT_ARGS)
{
	PTEditDoc *pDoc = (PTEditDoc *)UI_WIDGET(pScrollArea)->userinfo;
	UI_GET_COORDINATES(pScrollArea);

	if (pScrollArea->widget.sb->scrollX)
		h -= ui_ScrollbarHeight(UI_WIDGET(pScrollArea)->sb) * scale;
	if (pScrollArea->widget.sb->scrollY)
		w -= ui_ScrollbarWidth(UI_WIDGET(pScrollArea)->sb) * scale;

	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);
	clipperPushRestrict(&box);

	PTTalentAreaDrawArrows(pDoc);

	clipperPop();

	ui_ScrollAreaDraw(pScrollArea, UI_PARENT_VALUES);
}

const char *PTTalentTreeGetLevelTable(PTEditDoc *pDoc)
{
	devassert(pDoc);

	if (eaSize(&pDoc->pDefNew->ppGroups) <= 0)
		return "";

	return pDoc->pDefNew->ppGroups[0]->pRequires->pchTableName ? pDoc->pDefNew->ppGroups[0]->pRequires->pchTableName : "";
}

void PTOnTalentTreePointTableChange(UITextEntry *pTextEntry, PTEditDoc *pDoc)
{
	S32 i;
	bool bValidPowerTable = false;
	const char *pchTableName = NULL;

	devassert(pTextEntry);
	devassert(pDoc);

	// Get the name
	pchTableName = ui_TextEntryGetText(pTextEntry);
	if (pchTableName == NULL)
		pchTableName = "";

	// Set all group level tables to same value
	for (i = 0; i < eaSize(&pDoc->ppGroupWindows); i++)
	{
		// Clean up
		if (pDoc->ppGroupWindows[i]->pDefGroupNew->pRequires->pchTableName)
		{
			StructFreeString(pDoc->ppGroupWindows[i]->pDefGroupNew->pRequires->pchTableName);
		}
		// Set the new name
		pDoc->ppGroupWindows[i]->pDefGroupNew->pRequires->pchTableName = strdup(pchTableName);
	}

	// Set the doc as dirty
	PTPermaDirty(pDoc);
}

void PTSetupTalentTreeUI(PTEditDoc *pdoc)
{
	int x = 0;
	int y = 0;
	int i;

	PowerTreeDef *pdef = pdoc->pDefOrig;
	PTField *pField;
	UIWindow *pWin;
	UIExpanderGroup *pExpGrp;
	UIButton *pButton;
	UIScrollArea *pScrollArea;
	UIComboBox *pCombo;
	UIPane *pRightPane;	
	UILabel *pLevelTableLabel;

	if(!pdoc->pWin)
	{
		pdoc->pWin = ui_WindowCreate("Talent Tree",25,50,800,800);
		pdoc->emDoc.primary_ui_window = pdoc->pWin;
	}

	pWin = pdoc->pWin;


	pRightPane = ui_PaneCreate(0, 0, 450, 1.0, UIUnitFixed,UIUnitPercentage, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(pRightPane),0,0,0,0,UITopRight);
	ui_WindowAddChild(pWin, pRightPane);

	x = 0;
	y = 0;

	pButton = ui_ButtonCreate("Save Tree",x,y,PTButtonSave,pdoc);
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;
	pButton = ui_ButtonCreate("Check Out Tree",x,y,PTButtonCheckOut,pdoc);
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;
	pButton = ui_ButtonCreate("Revert Tree",x,y,PTButtonRevert,pdoc);
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;
	pButton = ui_ButtonCreate("Validate Tree",x,y,PTButtonValidate,pdoc);
	ui_PaneAddChild(pRightPane,pButton);

	x = 0;
	y += 35;

	pField = PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Name",NULL,"Power Tree Name",1,NULL,NULL,x,y,200,true,false,true,NULL);
	pField->bisName = true;
	eaPush(&pdoc->ppTreeFields, pField);

	y += 20;

	// Power table selection for the power tree
	pLevelTableLabel = ui_LabelCreate("Talent Tree Point Table", x, y);
	UI_WIDGET(pLevelTableLabel)->width = 200;
	ui_PaneAddChild(pRightPane, pLevelTableLabel);

	pCombo = ui_ComboBoxCreate(x + UI_WIDGET(pLevelTableLabel)->width + 5, y, 200, parse_PowerTable, &g_PowerTables.ppPowerTables, "Name");
	pCombo->openedWidth = 400;

	pdoc->pTalentTreePowerTableTextEntry = ui_TextEntryCreate(PTTalentTreeGetLevelTable(pdoc), x + UI_WIDGET(pLevelTableLabel)->width + 5, y);
	ui_TextEntrySetComboBox(pdoc->pTalentTreePowerTableTextEntry, pCombo);
	ui_TextEntrySetChangedCallback(pdoc->pTalentTreePowerTableTextEntry, PTOnTalentTreePointTableChange, pdoc);
	pdoc->pTalentTreePowerTableTextEntry->autoComplete = true;
	UI_WIDGET(pdoc->pTalentTreePowerTableTextEntry)->width = 200;
	ui_PaneAddChild(pRightPane, pdoc->pTalentTreePowerTableTextEntry);

	y += 20;

	eaPush(&pdoc->ppTreeFields,PTAddMessageField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"DisplayMessage","Display Name",true,x,y,200,true,false,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddMessageField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"DescriptionMessage","Description",true,x,y,200,true,false,false,NULL));
	y += 20;
	
	pField = PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"IconName",NULL,"Icon",1,NULL,NULL,x,y,200,true,false,false,NULL);
	eaPush(&pdoc->ppTreeFields, pField);
	y += 20;
	

	pCombo = ui_ComboBoxCreateWithDictionary(0,0,250,"PowerTreeTypeDef", parse_PTTypeDef, "Name");
	ui_ComboBoxCalculateOpenedWidth(pCombo);
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"TreeType","PowerTreeTypeDef","Tree Type",1,pCombo,NULL,x,y,200,true,false,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"ExprBlockRequires",NULL,"Requirements",1,NULL,NULL,x,y,200,true,false,false,NULL));

	y += 20;
	pCombo = ui_ComboBoxCreateWithGlobalDictionary(0,0,250,"CharacterClass", "resourceName");
	ui_ComboBoxCalculateOpenedWidth(pCombo);
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Class","CharacterClass","Character Class",1,pCombo,NULL,x,y,200,true,false,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddEnumComboField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"UICategory","UI Category",true,PowerTreeUICategoryEnum,x,y,200,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddEnumComboField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Respec","Respec Type",true,PowerTreeRespecEnum,x,y,200,false,NULL));

	y += 20;

	pCombo = ui_ComboBoxCreateWithGlobalDictionary(0,0,250,"AIAnimList", "resourceName");
	ui_ComboBoxCalculateOpenedWidth(pCombo);
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"AnimListToPlayOnGrant","AIAnimList","AnimList to play on Grant",1,pCombo,NULL,x,y,200,true,false,false,NULL));

	y += 25;

	eaPush(&pdoc->ppTreeFields,PTAddMessageField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"GrantMessage","Notification to display on grant",true,x,y,200,true,false,false,NULL));

	y += 20;

	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"DefaultPlayingStyle",NULL,"Default Playing Style",1,NULL,NULL,x,y,200,true,false,false,NULL));

	//Mikes Grouping Test

	y+=20;
	eaPush(&pdoc->ppTreeFields,PTAddCheckField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"AutoBuy","AutoBuy",true,x,y,200,false,NULL,NULL));

	y+=20;
	eaPush(&pdoc->ppTreeFields,PTAddCheckField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Temporary","Temporary",true,x,y,200,false,NULL,NULL));

	y+=20;
	eaPush(&pdoc->ppTreeFields,PTAddCheckField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"SendToClient","Send Minimal Info To The Clients",true,x,y,200,false,NULL,NULL));

	for(i=0;i<eaSize(&pdoc->ppTreeFields);i++)
	{
		eaPush(&pdoc->ppFields,pdoc->ppTreeFields[i]);
	}

	y += 30;

	pButton = ui_ButtonCreate("Validate Group",x,y,PTButtonValidateGroup,NULL);
	ui_SetActive(UI_WIDGET(pButton), false);
	pdoc->pGroupBtns[2] = pButton;
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;

	y+= 30;
	pExpGrp = ui_ExpanderGroupCreate();
	ui_WidgetSetDimensionsEx(&pExpGrp->widget, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(&pExpGrp->widget,0,y,0,0,UITopLeft);
	pdoc->pExpGrp = pExpGrp;

	ui_PaneAddChild(pRightPane,pExpGrp);

	pScrollArea = ui_ScrollAreaCreate(0,0,750,800,1150,1150,true,true);
	pScrollArea->autosize = true;
	UI_WIDGET(pScrollArea)->drawF = PTTalentTreeScrollAreaDraw;
	UI_WIDGET(pScrollArea)->userinfo = pdoc;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pScrollArea),1.0f,1.0f,UIUnitPercentage,UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pScrollArea), 0, 450, 0, 0);
	pdoc->pScrollArea = pScrollArea;

	ui_WindowAddChild(pWin,pScrollArea);

	for(i=0;i<eaSize(&pdoc->pDefNew->ppGroups);i++)
	{
		PTCreateGroupWindow(pdoc->pDefNew->ppGroups[i],pdoc->pDefOrig->ppGroups[i],pdoc);
	}

	PTCreateTalentTreeWindow(pdoc, MAX_TALENT_TREE_ROWS, MAX_TALENT_TREE_COLS, TALENT_TREE_BUTTON_WIDTH, TALENT_TREE_BUTTON_HEIGHT, TALENT_TREE_GRID_HSPACING, TALENT_TREE_GRID_VSPACING);

	eaPush(&pdoc->emDoc.ui_windows,pWin);
}

void PTSetupPowerTreeUI(PTEditDoc *pdoc)
{
	int x = 0;
	int y = 0;
	int i;

	PowerTreeDef *pdef = pdoc->pDefOrig;
	PTField *pField;
	UIWindow *pWin;
	UIExpanderGroup *pExpGrp;
	UIButton *pButton;
	UIScrollArea *pScrollArea;
	UIComboBox *pCombo;
	UIPane *pRightPane;

	if(!pdoc->pWin)
	{
		pdoc->pWin = ui_WindowCreate("Power Tree",25,50,800,800);
		pdoc->emDoc.primary_ui_window = pdoc->pWin;
	}

	pWin = pdoc->pWin;


	pRightPane = ui_PaneCreate(0, 0, 450, 1.0, UIUnitFixed,UIUnitPercentage, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(pRightPane),0,0,0,0,UITopRight);
	ui_WindowAddChild(pWin, pRightPane);

	x = 0;
	y = 0;

	pButton = ui_ButtonCreate("Save Tree",x,y,PTButtonSave,pdoc);
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;
	pButton = ui_ButtonCreate("Check Out Tree",x,y,PTButtonCheckOut,pdoc);
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;
	pButton = ui_ButtonCreate("Revert Tree",x,y,PTButtonRevert,pdoc);
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;
	pButton = ui_ButtonCreate("Validate Tree",x,y,PTButtonValidate,pdoc);
	ui_PaneAddChild(pRightPane,pButton);

	x = 0;
	y += 35;

	pField = PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Name",NULL,"Power Tree Name",1,NULL,NULL,x,y,200,true,false,true,NULL);
	pField->bisName = true;
	eaPush(&pdoc->ppTreeFields, pField);

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddMessageField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"DisplayMessage","Display Name",true,x,y,200,true,false,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddMessageField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"DescriptionMessage","Description",true,x,y,200,true,false,false,NULL));

	y += 20;
	pCombo = ui_ComboBoxCreateWithDictionary(0,0,250,"PowerTreeTypeDef", parse_PTTypeDef, "Name");
	ui_ComboBoxCalculateOpenedWidth(pCombo);
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"TreeType","PowerTreeTypeDef","Tree Type",1,pCombo,NULL,x,y,200,true,false,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"ExprBlockRequires",NULL,"Requirements",1,NULL,NULL,x,y,200,true,false,false,NULL));

	y += 20;
	pCombo = ui_ComboBoxCreateWithGlobalDictionary(0,0,250,"CharacterClass", "resourceName");
	ui_ComboBoxCalculateOpenedWidth(pCombo);
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Class","CharacterClass","Character Class",1,pCombo,NULL,x,y,200,true,false,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddEnumComboField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"UICategory","UI Category",true,PowerTreeUICategoryEnum,x,y,200,false,NULL));

	y += 20;
	eaPush(&pdoc->ppTreeFields,PTAddEnumComboField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Respec","Respec Type",true,PowerTreeRespecEnum,x,y,200,false,NULL));

	y += 20;

	pCombo = ui_ComboBoxCreateWithGlobalDictionary(0,0,250,"AIAnimList", "resourceName");
	ui_ComboBoxCalculateOpenedWidth(pCombo);
	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"AnimListToPlayOnGrant","AIAnimList","AnimList to play on grant",1,pCombo,NULL,x,y,200,true,false,false,NULL));

	y += 25;

	eaPush(&pdoc->ppTreeFields,PTAddMessageField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"GrantMessage","Notification to display on grant",true,x,y,200,true,false,false,NULL));

	y += 20;

	eaPush(&pdoc->ppTreeFields,PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"DefaultPlayingStyle",NULL,"Default Playing Style",1,NULL,NULL,x,y,200,true,false,false,NULL));

	y+=20;
	pCombo = ui_ComboBoxCreate(0,0,200,parse_PowerTable,&g_PowerTables.ppPowerTables,"Name");
	pCombo->openedWidth = 400;
	pField = PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"MaxSpendablePointsCostTable",NULL,"Max Spendable Points Cost Pool",1,pCombo,NULL,x,y,200,true,false,false,NULL);
	eaPush(&pdoc->ppTreeFields,pField);

	y+=20;
	pField = PTAddTextField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"MaxSpendablePoints",NULL,"Max Spent Points Ratio",1,NULL,NULL,x,y,200,true,false,false,NULL);
	eaPush(&pdoc->ppTreeFields,pField);

	//Mikes Grouping Test

	y+=20;
	eaPush(&pdoc->ppTreeFields,PTAddCheckField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"AutoBuy","AutoBuy",true,x,y,200,false,NULL,NULL));

	y+=20;
	eaPush(&pdoc->ppTreeFields,PTAddCheckField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"Temporary","Temporary",true,x,y,200,false,NULL,NULL));

	y+=20;
	eaPush(&pdoc->ppTreeFields,PTAddCheckField(pRightPane,NULL,pdoc,pdoc->pDefOrig,pdoc->pDefNew,parse_PowerTreeDef,"SendToClient","Send Minimal Info To The Clients",true,x,y,200,false,NULL,NULL));

	for(i=0;i<eaSize(&pdoc->ppTreeFields);i++)
	{
		eaPush(&pdoc->ppFields,pdoc->ppTreeFields[i]);
	}

	y += 30;
	pButton = ui_ButtonCreate("New Group",x,y,PTButtonNewGroup,pdoc);
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;

	pButton = ui_ButtonCreate("Delete Group",x,y,PTButtonDeleteGroup,NULL);
	ui_SetActive(UI_WIDGET(pButton), false);
	pdoc->pGroupBtns[0] = pButton;
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;

	pButton = ui_ButtonCreate("Revert Group",x,y,PTButtonRevertGroup,NULL);
	ui_SetActive(UI_WIDGET(pButton), false);
	pdoc->pGroupBtns[1] = pButton;
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;

	pButton = ui_ButtonCreate("Validate Group",x,y,PTButtonValidateGroup,NULL);
	ui_SetActive(UI_WIDGET(pButton), false);
	pdoc->pGroupBtns[2] = pButton;
	ui_PaneAddChild(pRightPane,pButton);
	x += pButton->widget.width + 5;

	y+= 30;
	pExpGrp = ui_ExpanderGroupCreate();
	ui_WidgetSetDimensionsEx(&pExpGrp->widget, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(&pExpGrp->widget,0,y,0,0,UITopLeft);
	pdoc->pExpGrp = pExpGrp;

	ui_PaneAddChild(pRightPane,pExpGrp);

	pScrollArea = ui_ScrollAreaCreate(0,0,750,800,1150,1150,true,true);
	pScrollArea->autosize = true;
	pScrollArea->draggable = true;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pScrollArea),1.0f,1.0f,UIUnitPercentage,UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pScrollArea), 0, 450, 0, 0);
	pdoc->pScrollArea = pScrollArea;

	ui_WindowAddChild(pWin,pScrollArea);

	for(i=0;i<eaSize(&pdoc->pDefNew->ppGroups);i++)
	{
		PTCreateGroupWindow(pdoc->pDefNew->ppGroups[i],pdoc->pDefOrig->ppGroups[i],pdoc);
	}

	s_bRedrawGroups = true;

	eaPush(&pdoc->emDoc.ui_windows,pWin);
}

char *PTFixTreeName(const char* pchName, const char*pchNewTree)
{
	static char s_achName[255] = {0};

	pchName = strstr(pchName,".");

	strcpy(s_achName,pchNewTree);

	strcat(s_achName,pchName);

	return s_achName;
}

void PTFixGroupName(PTGroupDef *pGroup, const char *pchNewTree)
{
	char *pchNewName;
	int i;

	pchNewName = PTFixTreeName(pGroup->pchNameFull,pchNewTree);

	StructFreeString(pGroup->pchNameFull);
	pGroup->pchNameFull = StructAllocString(pchNewName);

	for(i=0;i<eaSize(&pGroup->ppNodes);i++)
	{
		pchNewName = PTFixTreeName(pGroup->ppNodes[i]->pchNameFull,pchNewTree);
		pGroup->ppNodes[i]->pchNameFull = allocAddString(pchNewName);
	}

}

void PTMoveGroups(PTEditDoc *pdoc, int x, int y)
{
	int i;

	if(x == 0 && y == 0)
		return;

	for(i=0;i<eaSize(&pdoc->ppGroupWindows);i++)
	{
		ui_WidgetSetPosition((UIWidget*)pdoc->ppGroupWindows[i]->pWin,pdoc->ppGroupWindows[i]->pWin->widget.x + x,pdoc->ppGroupWindows[i]->pWin->widget.y + y); 
	}
}

void PTFixGroupRefName(PTGroupDef *pGroup, const char* pchNewTree)
{
	const char *pchLink = REF_STRING_FROM_HANDLE(pGroup->pRequires->hGroup);
	bool bGroupLink = true;
	char *pchNewName = NULL;

	if(!pchLink)
		return;

	estrCreate(&pchNewName);

	pchLink = strstr(pchLink,".");
	
	estrPrintf(&pchNewName,"%s%s",pchNewTree,pchLink);

	REMOVE_HANDLE(pGroup->pRequires->hGroup);

	if(bGroupLink)
		SET_HANDLE_FROM_STRING(g_hPowerTreeGroupDefDict,pchNewName,pGroup->pRequires->hGroup);

	estrDestroy(&pchNewName);
}
void PTGroupPaste(PTEditDoc *pDoc, PTGroupEdit *pGroup, PTClipboard *pData)
{
	switch(pData->eClipboardType)
	{
	case kClipboard_CloneNode:
		{
			PTNodeDef *pNodeClone = StructCreate(parse_PTNodeDef);

			StructCopyAll(parse_PTNodeDef,pData->pNode,pNodeClone);

			SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pData->pNode->pchNameFull,pNodeClone->hNodeClone);

			eaPush(&pGroup->pDefGroupNew->ppNodes,pNodeClone);

			PTCreateNodeExp(pNodeClone,NULL,pGroup);
		}
		break;
	case kClipboard_SingleGroup:
	case kClipboard_CloneGroup:
		{
			PTGroupDef *pGroupCopy = StructCreate(parse_PTGroupDef);

			StructCopyAll(parse_PTGroupDef,pData->ppGroups[0],pGroupCopy);

			while(PTFindGroupWindow(pDoc->ppGroupWindows,pGroupCopy->pchNameFull))
			{
				char achName[1024];

				sprintf(achName,"%s_copy",pGroupCopy->pchNameFull);
				StructFreeString(pGroupCopy->pchNameFull);
				pGroupCopy->pchNameFull = StructAllocString(achName);
				sprintf(achName,"%s_copy",pGroupCopy->pchGroup);
				StructFreeString(pGroupCopy->pchGroup);
				pGroupCopy->pchGroup = StructAllocString(achName);
			}
			REMOVE_HANDLE(pGroupCopy->pRequires->hGroup);
			if(pGroup && pGroup->pDefGroupNew)
			{
				SET_HANDLE_FROM_STRING(g_hPowerTreeGroupDefDict,pGroup->pDefGroupNew->pchNameFull,pGroupCopy->pRequires->hGroup);
				pGroupCopy->x = pGroup->pDefGroupNew->x + pData->ioffX;
				pGroupCopy->y = pGroup->pDefGroupNew->y + pData->ioffX;
			}
			else
			{
				pGroupCopy->x = pData->ioffX;
				pGroupCopy->y = pData->ioffY;
			}

			if(pData->eClipboardType == kClipboard_CloneGroup)
			{
				int i;

				for(i=0;i<eaSize(&pData->ppGroups[0]->ppNodes);i++)
				{
					PTNodeDef *pNode = pData->ppGroups[0]->ppNodes[i];
					PTNodeDef *pCloneNode = pGroupCopy->ppNodes[i];

					REMOVE_HANDLE(pCloneNode->hNodeClone);
					SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pNode->pchNameFull,pCloneNode->hNodeClone);
				}
			}

			eaPush(&pDoc->pDefNew->ppGroups,pGroupCopy);
			PTCreateGroupWindow(pGroupCopy,NULL,pDoc);
		}
		break;
	case kClipboard_Groups:
	case kClipboard_CloneGroups:
		{
			//First group is in 0
			PTGroupDef *pNewGroup = StructCreate(parse_PTGroupDef);
			int i;
			int ioffsetX;
			int ioffsetY;

			StructCopyAll(parse_PTGroupDef,pData->ppGroups[0],pNewGroup);

			while(PTFindGroupWindow(pDoc->ppGroupWindows,pNewGroup->pchNameFull))
			{
				char achName[1024];

				sprintf(achName,"%s_copy",pNewGroup->pchNameFull);
				StructFreeString(pNewGroup->pchNameFull);
				pNewGroup->pchNameFull = StructAllocString(achName);
				sprintf(achName,"%s_copy",pNewGroup->pchGroup);
				StructFreeString(pNewGroup->pchGroup);
				pNewGroup->pchGroup = StructAllocString(achName);
			}

			REMOVE_HANDLE(pNewGroup->pRequires->hGroup);
			PTFixGroupName(pNewGroup,pDoc->pDefNew->pchName);
			if(pGroup && pGroup->pDefGroupNew)
			{
				SET_HANDLE_FROM_STRING(g_hPowerTreeGroupDefDict,pGroup->pDefGroupNew->pchNameFull,pNewGroup->pRequires->hGroup);
				ioffsetX = pGroup->pDefGroupNew->x + pData->ioffX - pNewGroup->x;
				ioffsetY = pGroup->pDefGroupNew->y + pData->ioffY - pNewGroup->y;
				pNewGroup->x = pGroup->pDefGroupNew->x + pData->ioffX;
				pNewGroup->y = pGroup->pDefGroupNew->y + pData->ioffY;
			}
			else
			{
				ioffsetX = pData->ioffX - pNewGroup->x;
				ioffsetY = pData->ioffY - pNewGroup->y;
				pNewGroup->x = pData->ioffX;
				pNewGroup->y = pData->ioffY;
			}

			if(pData->eClipboardType == kClipboard_CloneGroups)
			{
				int n;

				for(n=0;n<eaSize(&pData->ppGroups[0]->ppNodes);n++)
				{
					PTNodeDef *pNode = pData->ppGroups[0]->ppNodes[n];
					PTNodeDef *pCloneNode = pNewGroup->ppNodes[n];

					SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pNode->pchNameFull,pCloneNode->hNodeClone);
				}
			}

			eaPush(&pDoc->pDefNew->ppGroups,pNewGroup);
			PTCreateGroupWindow(pNewGroup,NULL,pDoc);
			
			for(i=1;i<eaSize(&pData->ppGroups);i++)
			{
				pNewGroup = StructCreate(parse_PTGroupDef);

				StructCopyAll(parse_PTGroupDef,pData->ppGroups[i],pNewGroup);

				while(PTFindGroupWindow(pDoc->ppGroupWindows,pNewGroup->pchNameFull))
				{
					char achName[1024];

					sprintf(achName,"%s_copy",pNewGroup->pchNameFull);
					StructFreeString(pNewGroup->pchNameFull);
					pNewGroup->pchNameFull = StructAllocString(achName);
					sprintf(achName,"%s_copy",pNewGroup->pchGroup);
					StructFreeString(pNewGroup->pchGroup);
					pNewGroup->pchGroup = StructAllocString(achName);
				}

				PTFixGroupName(pNewGroup,pDoc->pDefNew->pchName);
				PTFixGroupRefName(pNewGroup,pDoc->pDefNew->pchName);

				pNewGroup->x += ioffsetX;
				pNewGroup->y += ioffsetY;

				PTMoveGroups(pDoc,pNewGroup->x < 0 ? ABS(pNewGroup->x) : 0,pNewGroup->y < 0 ? ABS(pNewGroup->y) : 0);
				
				if(pNewGroup->x < 0)
				{
					ioffsetX += ABS(pNewGroup->x);
					pNewGroup->x = 0;
				}
				if(pNewGroup->y < 0)
				{
					ioffsetY += ABS(pNewGroup->y);
					pNewGroup->y = 0;
				}

				if(pData->eClipboardType == kClipboard_CloneGroups)
				{
					int n;

					for(n=0;n<eaSize(&pData->ppGroups[i]->ppNodes);n++)
					{
						PTNodeDef *pNode = pData->ppGroups[i]->ppNodes[n];
						PTNodeDef *pCloneNode = pNewGroup->ppNodes[n];

						SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pNode->pchNameFull,pCloneNode->hNodeClone);
					}
				}

				eaPush(&pDoc->pDefNew->ppGroups,pNewGroup);
				PTCreateGroupWindow(pNewGroup,NULL,pDoc);
			}
		}
	}
}
void PTFillChildTree(PTEditDoc* pdoc, PTGroupEdit *pGroup, PTGroupDef ***pppOutList)
{
	int i;

	for(i=eaSize(&pdoc->ppGroupWindows)-1;i>=0;i--)
	{
		const char *pchLinkName = REF_STRING_FROM_HANDLE(pdoc->ppGroupWindows[i]->pDefGroupNew->pRequires->hGroup); 
		if(pchLinkName && stricmp(pchLinkName,pGroup->pDefGroupNew->pchNameFull) == 0)
		{
			eaPush(pppOutList,pdoc->ppGroupWindows[i]->pDefGroupNew);
			PTFillChildTree(pdoc,pdoc->ppGroupWindows[i],pppOutList);
			continue;
		}
	}
}
void PTNodeClone(PTNodeEdit *pNode)
{
	PTClipboard *pCopyData = StructCreate(parse_PTClipboard);

	if(pNode)
	{
		pCopyData->eClipboardType = kClipboard_CloneNode;
		pCopyData->pNode = pNode->pDefNodeNew;

		emAddToClipboardParsed(parse_PTClipboard,pCopyData);
	}

	pCopyData->pNode = NULL;
	StructDestroy(parse_PTClipboard,pCopyData);
}
void PTGroupCopy(PTGroupEdit *pGroup, bool bCut, bool bAffectChildren, bool bClone)
{
	PTClipboard *pCopyData = StructCreate(parse_PTClipboard);

	//Copy first data 
	if(pGroup && pGroup->pDefGroupNew)
	{
		pCopyData->eClipboardType = bClone ? kClipboard_CloneGroup : kClipboard_SingleGroup;
		eaPush(&pCopyData->ppGroups,pGroup->pDefGroupNew);
		//Find offsets
		if(IS_HANDLE_ACTIVE(pGroup->pDefGroupNew->pRequires->hGroup))
		{
			PTGroupEdit *pGroupFind = PTFindGroupWindow(pGroup->pdoc->ppGroupWindows,REF_STRING_FROM_HANDLE(pGroup->pDefGroupNew->pRequires->hGroup));

			if(pGroupFind)
			{
				pCopyData->ioffX = pGroup->pDefGroupNew->x - pGroupFind->pDefGroupNew->x;
				pCopyData->ioffY = pGroup->pDefGroupNew->y - pGroupFind->pDefGroupNew->y;
			}
		}
		if(bAffectChildren)
		{
			pCopyData->eClipboardType = bClone ? kClipboard_CloneGroups : kClipboard_Groups;

			PTFillChildTree(pGroup->pdoc,pGroup,&pCopyData->ppGroups);

		}
		emAddToClipboardParsed(parse_PTClipboard,pCopyData);

		if(bCut)
		{
			if(bAffectChildren)
			{
				int i;
				for(i=eaSize(&pCopyData->ppGroups)-1;i>=0;i--)
				{
					PTDeleteGroup(PTFindGroupWindow(pGroup->pdoc->ppGroupWindows,pCopyData->ppGroups[i]->pchGroup), false);
				}
			}
			else
				PTDeleteGroup(pGroup, false);
		}	
	}

	eaDestroy(&pCopyData->ppGroups);
	pCopyData->pNode = NULL;
	StructDestroy(parse_PTClipboard,pCopyData);
}

#endif

#include "PowerTreeEditor.h"
#include "CharacterAttribs.h"
#include "AutoGen/PowerTreeEditor_h_ast.h"
#include "AutoGen/PowerTreeEditor_h_ast.c"
