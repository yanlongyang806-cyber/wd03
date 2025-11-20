//
// PowerTreeEditor.h
//

#pragma once
GCC_SYSTEM

#include "PowerTree.h"
#include "EditorManager.h"
#include "referencesystem.h"

typedef enum AttribType AttribType;
typedef struct PTGroupEdit;
typedef struct PTNodeEdit;
typedef struct PTRankEdit;
typedef struct PTField PTField;
typedef struct UIExpander		UIExpander;
typedef struct UIScrollArea		UIScrollArea;
typedef struct UIWindow			UIWindow;
typedef struct DisplayMessage	DisplayMessage;

typedef void (*PTFieldChangeFunction) (PTField *, UserData);

AUTO_ENUM;
typedef enum PTClipBoardOption
{
	kClipboard_Groups,
	kClipboard_SingleNode,
	kClipboard_SingleGroup,
	kClipboard_CloneNode,
	kClipboard_CloneGroup,
	kClipboard_CloneGroups,
}PTClipBoardOption;
#ifndef NO_EDITORS

#define MAX_TALENT_TREE_ROWS 10
#define MAX_TALENT_TREE_COLS 10

typedef struct PTEditDoc
{
	//NOTE: This must be first
	EMEditorDoc emDoc;

	char *pchOrigName;
	char *pchNewName;

	PowerTreeDef *pDefOrig;
	PowerTreeDef *pDefNew;

	UIWindow *pWin;
	UIExpanderGroup *pExpGrp;
	UIScrollArea *pScrollArea;
	struct PTField **ppFields;
	struct PTField **ppTreeFields;
	
	struct PTField **ppDirty; //list of dirty fields
	bool bPermaDirty;		// Used for a big change like a delete
	bool bDirty;

	bool bLevelView;		//If the groups should be sorted in a level view
	bool bExplodeRanks;

	struct PTGroupEdit **ppGroupWindows;

	struct PTGroupEdit *pFocusedGrp; //The current group taking focus
	UIButton *pGroupBtns[3];

	//Character arrays used for menu drop downs
	char **ppchGroupNames;
	char **ppchNodeNames;

	UIMenu *pGroupMenu;

	REF_TO(PowerTreeDef) hDefCheckPoint;

	// Indicates each grid position's availability for creating a new node
	bool talentTreeMatrix[MAX_TALENT_TREE_ROWS * MAX_TALENT_TREE_COLS];

	// UITextEntry that indicates the power table for the whole talent tree
	UITextEntry *pTalentTreePowerTableTextEntry;

	// Indicates if the talent tree grid is created (UI only)
	bool bTalentTreeGridCreated;
}PTEditDoc;
typedef struct PTNodeEdit PTNodeEdit;
typedef struct PTRankEdit PTRankEdit;
typedef struct PTEnhancementEdit PTEnhancementEdit;

typedef struct PTDependencyChain
{
	PTNodeEdit **ppNodes;
	int iWidth;
}PTDependencyChain;

typedef struct PTNodeRankChain
{
	PTNodeEdit *pNode;
	UILabel **ppRankLabels;
	int *piLevels;
	int iWidth;
}PTNodeRankChain;

typedef struct PTGroupEdit
{
	PTEditDoc *pdoc;

	PTGroupDef *pDefGroupOrig;
	PTGroupDef *pDefGroupNew;

	PTNodeEdit **ppNodeEdits;

	UIWindow *pWin;
	UIExpander *pExp;
	UIExpanderGroup *pExpNodes; //Expander used for viewing Nodes
	char **ppchiMax;

	UIPairedBox **ppPairedOut;
	UIPairedBox **ppPairedIn;
	UIPairedBox **ppPairedPower;
	struct PTGroupConnection **ppConnections;

	UITextEntry **ppTextMax;
	UIButton *pButtonDrag;

	UITextEntry *pGroupText;
	UITextEntry *pGroupNum;

	UIMenu *pMenu;

	UILabel **ppLevelLables;

	PTDependencyChain **ppChains;
	PTNodeRankChain **ppRankChains;
}PTGroupEdit;

typedef struct PTGroupConnection
{
	struct PTGroupEdit *pGroupSrc;
	struct PTGroupEdit *pGroupDest;
	struct PTNodeEdit *pNodeDest;
	struct PTNodeEdit *pNodeSrc;

	UILabel *pUILabel;
	UITextEntry *pUIText;
	
	UIPairedBox *pPairedSrc;
	UIPairedBox *pPairedDest;
	char *pchNameCompare;
}PTGroupConnection;

typedef struct PTNodeEdit
{
	PTEditDoc *pdoc;
	PTGroupEdit *pGroup;

	PTNodeDef *pDefNodeOrig;
	PTNodeDef *pDefNodeNew;
	
	UIExpander *pExp;
	UIButton *pBtn;
	UIExpanderGroup *pExpRanks;
	PTRankEdit **ppRankEdits;
	PTEnhancementEdit **ppEnhancementEdits;
	UIMenu *pMenu;
	UIPairedBox **ppPaired;
	int iLayer;

	UIButton *pNewRankBtn;
	UIButton *pNewEnhancmentButton;
	UIButton *pDefaultEnhButton;

	PTGroupConnection **ppConnections;
	UIPairedBox **ppPairedPower;
}PTNodeEdit;

typedef struct PTRankEdit
{
	PTNodeEdit *pNode;
	
	PTNodeRankDef *pDefRankOrig;
	PTNodeRankDef *pDefRankNew;

	UIExpander *pExp;
	UIMenu *pMenu;
}PTRankEdit;

typedef struct PTEnhancementEdit
{
	PTNodeEdit *pNode;

	PTNodeEnhancementDef *pDefOrig;
	PTNodeEnhancementDef *pDefNew;

	UIExpander *pExp;
	UIMenu *pMenu;
}PTEnhancementEdit;

typedef struct PTField
{
	void *pOld, *pNew;		// pointers to the old and new data (could power PowerDef, AttribModDef, etc)

	//	UITab *pTab;			// pointer to tab for coloring and cleanup, if it's in a tab
	struct PTEditDoc *pdoc;		// So we know what doc this is in

	ParseTable *ptable;		// Keep track of what table this is for
	size_t offset;			// So we know where to go looking for the data (assumes PowerDef)
	StructTypeField type;	// So we know what type it is
	const char *pchDictName;		// So we know what dictionary it uses, if it's a reference
	StaticDefineInt *pEnum;	// The StaticDefineInt for an enum
	const char** ppchModel; // If this is a TextEnumCombo, this is the model

	bool bDirty;			// If this field has been changed from the original
	bool bRevert;			// True if this field wants to be reverted
	bool bVertical;			// True if this field should be displayed vertically
	bool bisTitle;			// True if this field operates a title (Expander)
	bool bEnumCombo;		// True if this is a combo box on top of an enum
	bool bisName;			// True if this field is a name and ' ' is replaces with '_'

	UILabel *pUILabel;		// Save the label so we can delete it if we want
	
	UITextEntry *pUIText;	// So we have the handle to the UI widget
	UITextArea *pUITextArea;//...
	UIComboBox *pUICombo;	//...
	UICheckButton *pUICheck; //...
	UIFlagComboBox *pUIFlagBox; //...
	UIMessageEntry *pUIMessage; //...

	UIExpander *pexp;		// The relating expander
	UIButton *pbtn;			// The relating button
	struct PTGroupEdit *pGroup;//The relating group
	UIMenu *pMenu;

	PTFieldChangeFunction fChangeFunc;
	UserData pChangeFuncData;

	bool bPoolString;
} PTField;

#endif
AUTO_STRUCT;
typedef struct PTClipboard
{
	PTClipBoardOption eClipboardType;
	PTGroupDef **ppGroups;				AST(NO_INDEX)
	PTNodeDef *pNode;
	int ioffX;							AST(DEFAULT(0))
	int ioffY;							AST(DEFAULT(0))
}PTClipboard;
#ifndef NO_EDITORS

void PTSetupPowerTreeUI(PTEditDoc *pdoc);
void PTSetupTalentTreeUI(PTEditDoc *pdoc);
void PTUpdateFileSave(EMFile *pfile);
void PTDraw(EMEditorDoc *pEMDoc);
EMTaskStatus PTEditorSaveDoc(EMEditorDoc *pdoc);

void PTResizeGroupWin_All(PTEditDoc *pEditDoc);

//Clipboard functions
void PTGroupCopy(PTGroupEdit *pGroup, bool bCut, bool bAffectChildren, bool bClone);
void PTGroupPaste(PTEditDoc *pDoc, PTGroupEdit *pGroup, PTClipboard *pData);


// Automatic tree enhancement code
#endif
AUTO_ENUM;
typedef enum PTNodeEnhHelperType
{
	kPTNodeEnhType_All = 1,

	// Based on power
	kPTNodeEnhType_Recharge,
	kPTNodeEnhType_Cost,
	kPTNodeEnhType_Radius,
	kPTNodeEnhType_Arc,

	// Based on smart analysis of attribmod
	kPTNodeEnhType_DamageDirect,
	kPTNodeEnhType_DamageOverTime,

	// Plain old attrib match
	kPTNodeEnhType_Attribs,

} PTNodeEnhHelperType;

AUTO_STRUCT;
typedef struct PTNodeEnhHelper
{
	PTNodeEnhHelperType eType;
		// The type of enhancement def

	AttribType *peAttribs;	AST(INT, NAME(Attribs), SUBTABLE(AttribTypeEnum))
		// For use with the Attrib type

	PTNodeEnhancementDef *pDef;
		// The actual enhancement def

} PTNodeEnhHelper;

AUTO_STRUCT;
typedef struct PTNodeEnhHelpers
{
	PTNodeEnhHelper **ppHelpers;	AST(NAME(AutoEnhancement))
} PTNodeEnhHelpers;