#ifndef _POWERS_UI_H_
#define _POWERS_UI_H_
GCC_SYSTEM

#include "ResourceManager.h"
#include "stdtypes.h"
#include "Powers.h"
#include "PowerTree.h"

typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PowerTreeDefRef PowerTreeDefRef;
typedef struct PowerListNode PowerListNode;
typedef struct ExprContext ExprContext;
typedef struct PowerTreeDef PowerTreeDef;
typedef struct PowerTreeDefRef PowerTreeDefRef;
typedef struct PTGroupDef PTGroupDef;
typedef struct PTNodeDef PTNodeDef;

AUTO_STRUCT;
typedef struct PowersUIPurposeNode
{
	const char *pchName;				AST(POOL_STRING)
	PowerPurpose ePurpose;
	PowerListNode **eaPowerListNodes;
	S32 iSize;
} PowersUIPurposeNode;

AUTO_STRUCT;
typedef struct PowersUITreeNode
{
	const char *pchTreeName;			AST(POOL_STRING)
	REF_TO(PowerTreeDef) hTreeDef;
	bool bSelected;
	bool bShow;
} PowersUITreeNode;

AUTO_STRUCT;
typedef struct PowersUICategoryNode
{
	const char *pchName;				AST(POOL_STRING)
	PowerTreeUICategory eCategory;
	PowersUITreeNode **eaTreeNodes;
	bool bExpand;
} PowersUICategoryNode;

AUTO_STRUCT;
typedef struct PTGroupDefRef
{	
	REF_TO(PTGroupDef) hGroupRef;
	REF_TO(PowerTreeDef) hTreeRef;
} PTGroupDefRef;

AUTO_STRUCT;
typedef struct PowersUIDependentTreeNode
{
	REF_TO(PowerTreeDef) hParent;
		// If the parent tree is selected, then the dependent tree will 
		// also be selected. 
	
	REF_TO(PowerTreeDef) hDependant;
		// According to the dictionary, dependent (with an e) is a adj
		// and dependant (with an a) is a noun. 
} PowersUIDependentTreeNode;

// The textures laid out on a talent tree node
AUTO_ENUM;
typedef enum TalentsUITextureBits
{
	TalentsUITextureBits_None				= 0,
	TalentsUITextureBits_Vertical			= 1,
	TalentsUITextureBits_Horizontal			= 1 << 1,
	TalentsUITextureBits_ArrowOnTop			= 1 << 2,
	TalentsUITextureBits_ArrowOnBottom		= 1 << 3,
	TalentsUITextureBits_ArrowOnLeft		= 1 << 4,
	TalentsUITextureBits_ArrowOnRight		= 1 << 5,
	TalentsUITextureBits_ConnectorOnTop		= 1 << 6,
	TalentsUITextureBits_ConnectorOnBottom	= 1 << 7,
	TalentsUITextureBits_ConnectorOnLeft	= 1 << 8,
	TalentsUITextureBits_ConnectorOnRight	= 1 << 9,
	TalentsUITextureBits_LShapeTopLeft		= 1 << 10,
	TalentsUITextureBits_LShapeTopRight		= 1 << 11,
	TalentsUITextureBits_LShapeBottomLeft	= 1 << 12,
	TalentsUITextureBits_LShapeBottomRight	= 1 << 13,
} TalentsUITextureBits;

// Represents a single node in a talent tree
AUTO_STRUCT; 
typedef struct TalentsUITreeNode
{
	bool bHasPowerNode;

	PowerListNode *pPowerListNode;		AST(UNOWNED)
		// The power node assigned to this node (may be NULL)
	
	S32 iTextureBits;
		// Bitflag which indicates what textures are shown for this node

	S32 iTextureBitsPathMode;
		// Bitflag which indicates if the texture is on an open or closed path

	S8 iRow;
		// The row index

	S8 iColumn;
		// The column index
} TalentsUITreeNode;

// Represents a single talent tree
AUTO_STRUCT; 
typedef struct TalentsUITree
{
	const char *pchTreeName;			AST(POOL_STRING)
	REF_TO(PowerTreeDef) hTreeDef;

	TalentsUITreeNode **eaTalentNodes;
		// The talents that exists in this talent tree

	S8 iNumRows;
		// The number of rows in the talent tree

	S8 iNumCols;
		// The number of columns in the talent tree

	S8 iLowX;
		// The lowest x value for any node in the talent tree
	
	S8 iHighX;
		// The highest x value for any node in the talent tree

	S8 iLowY;
		// The lowest y value for any node in the talent tree

	S8 iHighY;
		// The highest y value for any node in the talent tree

	S32 *eaTextureBits;
		// This array contains the texture bit flags for each position

	S32 *eaTextureBitsPathMode;
		// This array contains the bits to indicate if the texture is drawn for an open or closed path
	
} TalentsUITree;

AUTO_ENUM;
typedef enum PurposeListPowerSortingMethod
{
	PurposeListPowerSortingMethod_Default,
	PurposeListPowerSortingMethod_Level,
} PurposeListPowerSortingMethod;

// The master data structure for the Powers UI. 
AUTO_STRUCT;
typedef struct PowersUIState
{
	U32 uiFrameLastUpdate;

	PowersUICategoryNode **eaUICategories;
		// Category nodes are the super tabs that contain multiple 
		// power trees within them when clicked

	PowersUICategoryNode *pHiddenCategoryNode;
		// This is used to store the dependent tree nodes that don't
		// appear in the UI. 

	PowersUIPurposeNode **eaPurposeListNodes;
		// Power purposes are things like "offense" and "defense"
		// that the powers are sorted into. 

	PowersUIDependentTreeNode **eaDependantTreeNodes;
		// Powers that only appear if the parent tree is also visible

	TalentsUITree **eaTalentTrees;
		// The list of talent trees that would be displayed in UI

	PowerListNode *pSelectedPower;			AST(UNOWNED)
	PTNodeDef *pSelectedPowerNode;			AST(UNOWNED)
		// The power that's currently selected. Mostly a convenience 
		// to keep the gen from going out of it's way to try to 
		// keep track of it.

	PowerTreeDefRef **eaTreeDefRefs;
		// This array just holds references to all the power trees
		// to force them to load on the client. 

	PTGroupDefRef **eaPTGroupDefRefs;
		// This array just holds references to all the individual PTGroups
		// that we want to load independently of the trees

	const char* pchFilterText;				AST(UNOWNED)
	const char* pchFilterModList;			AST(UNOWNED)
	bool bShowOwned;
	bool bShowAvailable;
	bool bShowUnavailable;

	// A comparison entity must be passed to the PowersUI_Update function for this flag to work.
	// This filter is useful for the cases where you use a power cart and want to compare
	// the fake entity to the actual entity. Comparison entity is the actual entity without
	// the powers added to the cart.
	// If this is set to true, any power owned by the comparison entity will be filtered.
	bool bFilterOwnedByComparisonEntity;

	// A comparison entity must be passed to the PowersUI_Update function for this flag to work.
	// This filter is useful for the cases where you use a power cart and want to compare
	// the fake entity to the actual entity. Comparison entity is the actual entity without
	// the powers added to the cart.
	// If this is set to true, any power available to the comparison entity will be added
	// to the list of powers.
	bool bAddAvailableForComparisonEntity;
		// These are filters that decide which powers are going show
		// up in the UI. 

	U32 uiCategoryUpdateNum;
		// Whenever a new power  or tree is loaded a static U32 is
		// incremented. Whenever this is less than that value 
		// it the power trees need to be reloaded.

	bool bExpandOwnedOnLoad;
		// If this is set, when a power tree is loaded from the server
		// and placed in the UI it will automatically expand it if it
		// is owned. 

	S32 iNumPurposes;

	S32 iMaxPowerLevelInPurposeNodes;

	PurposeListPowerSortingMethod ePurposeListPowerSortingMethod;
} PowersUIState;

S32 PowerGetStackCount(Entity* e, SA_PARAM_OP_VALID Power* pPow, S32* piLastStackCount, U32* puiNextUpdate, GameAccountDataExtract *pExtract);

#endif 