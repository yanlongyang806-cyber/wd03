/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef POWERTREE_H__
#define POWERTREE_H__
GCC_SYSTEM

#include "referencesystem.h"
#include "structDefines.h"	// For StaticDefineInt
#include "Message.h"

#include "CharacterAttribsMinimal.h"
#include "PowersEnums.h"


// Forward declarations
typedef struct Character		Character;
typedef struct CharacterClass	CharacterClass;
typedef struct Expression		Expression;
typedef struct ExprContext		ExprContext;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct Power			Power;
typedef struct PowerDef			PowerDef;
typedef struct PTNodeDef		PTNodeDef;
typedef struct PTGroupDef		PTGroupDef;
typedef struct PowerTreeDef		PowerTreeDef;
typedef struct AllegianceDef	AllegianceDef;

typedef struct PTGroupTopDown	PTGroupTopDown;
typedef struct PTNodeTopDown	PTNodeTopDown;
typedef struct PowerTreeTopDown PowerTreeTopDown;

typedef struct PowerReplace		PowerReplace;
typedef struct PowerReplaceDef	PowerReplaceDef;
typedef struct PowerTable		PowerTable;
typedef struct AIAnimList		AIAnimList;

typedef struct InventorySlotIDDef InventorySlotIDDef;
typedef struct ItemDef ItemDef;

typedef struct PTNodeTypeDef PTNodeTypeDef;

typedef struct NOCONST(PowerTree)	NOCONST(PowerTree);
typedef struct NOCONST(Character)	NOCONST(Character);
typedef struct NOCONST(PTNode)		NOCONST(PTNode);
typedef struct NOCONST(Power)		NOCONST(Power);
typedef struct NOCONST(Entity)		NOCONST(Entity);

extern bool g_bDebugPowerTree;

extern DictionaryHandle g_hPowerTreeTypeDict;
extern DictionaryHandle g_hPowerTreeNodeTypeDict;
extern DictionaryHandle g_hPowerTreeEnhTypeDict;
extern DictionaryHandle g_hPowerTreeDefDict;
extern DictionaryHandle g_hPowerTreeGroupDefDict;
extern DictionaryHandle g_hPowerTreeNodeDefDict;

#define DBGPOWERTREE_printf(format, ...) if(g_bDebugPowerTree) printf(format, ##__VA_ARGS__)

// To turn on purging, uncomment this define, make the rank and enhancement PowerDef references server-only, and
//  uncomment the name and purge timestamp fields
//#define PURGE_POWER_TREE_POWER_DEFS

/***** ENUMS *****/

AUTO_ENUM;
typedef enum PTNodeFlag
{
	// These are deprecated, so setting them to 0 will clear them from the flags field
	kNodeFlag_Deprecated		= 0, ENAMES(SneakPower StrugglePower TravelPower EndurancePower BlockPower PickUpPower)
	kNodeFlag_AutoBuy			= 1 << 0,	//This power should be auto purchased when allowed
	kNodeFlag_AutoAttack		= 1 << 1,	//Allow auto-attack
	kNodeFlag_HideNode			= 1 << 5,	//The UI system will not draw this node	
	kNodeFlag_AutoSlot			= 1 << 7,	//This power should be auto slotted when auto purchased	
	kNodeFlag_MasterNode		= 1 << 9,	//This node forces all other nodes in the group to match its rank, with all the other implied side-effects
	kNodeFlag_RequireTraining	= 1 << 10,	//This node cannot be purchased normally and must be trained
}PTNodeFlag;

extern DefineContext *g_PTRespecGroupType;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_PTRespecGroupType);
typedef enum PTRespecGroupType
{
	kPTRespecGroup_ALL = 0,						// This will respec all groups regardless of their type
	kPTRespecGroup_FirstGameSpecific,			EIGNORE

}PTRespecGroupType;

/***** END ENUMS *****/

AUTO_STRUCT;
typedef struct PTRespecGroupTypeName
{
	const char *pcName;

}PTRespecGroupTypeName;

AUTO_STRUCT;
typedef struct PTRespecGroupTypeNames
{
	EARRAY_OF(PTRespecGroupTypeName) eaNames;

}PTRespecGroupTypeNames;

AUTO_STRUCT AST_IGNORE(FirstGroupMax);
typedef struct PTTypeDef
{
	char *pchTreeType;					AST(NAME(Name) KEY STRUCTPARAM)
		// Name of the type

	Expression *pExprPurchase;			AST(NAME(PurchaseExpression) LATEBIND)
		// Expression that must return true to allow a tree with this type to be purchased

	F32 fTableScale;					AST(NAME(TableScale) DEFAULT(1.0))
		// Scale applied to all Power granted by a tree with this type

	F32 *pfCostScale;					AST(NAME(CostScale))
		// If this exists, it causes a scale to be applied to the cost of every node in a tree of this type.  Which
		//  index into this array is used is based on the tree instance.  The first tree purchased of this type uses
		//  the first index, the second tree purchased of this type uses the second index, and so on.  Cost is
		//  still rounded to the nearest S32.

	int iOrder;							AST(NAME(Order))
		// UI display order, relative to other tree types

	INT_EARRAY eaiRespecGroupType;	AST(NAME(RespecGroup) SUBTABLE(PTRespecGroupTypeEnum))
		// The type that this is respeced with.

	U32 bNonRefundable : 1;				AST(NAME(NonRefundable))
		// If true, trees of this type can not be entirely un-purchased (though they can lose all their nodes)

	U32 bSpentPointsNonDynamic : 1;		AST(NAME(SpentPointsNonDynamic))

	U32 bOnlyRespecUsingFull : 1;		AST(NAME(OnlyRespecUsingFull))
		// This tree can only be respeced from a full respec, not a tree respec

	U32 bHasAutoBuyTrees : 1;			AST(NAME(HasAutoBuyTrees))
		// This has required autobuy trees associated with this type

	char* pchSpentPointsNumeric;		AST(NAME(SpentPointsNumeric))

	bool bDisregardPath	: 1;			AST(NAME(DisregardPath))


}PTTypeDef;

AUTO_STRUCT;
typedef struct PTNodeTypeDef
{
	char *pchNodeType;		AST(NAME(Name) KEY STRUCTPARAM)
	Expression *pExpr;		AST(NAME(PurchaseExpression) LATEBIND)
	char **ppchSubTypes;	AST(NAME(SubTypes))
//	PTNodeTypeDef **ppSubTypes; NO_AST
}PTNodeTypeDef;

AUTO_STRUCT;
typedef struct PTEnhTypeDef
{
	char *pchEnhType;		AST(NAME(Name) KEY STRUCTPARAM)
	Expression *pExpr;		AST(NAME(PurchaseExpression) LATEBIND)
}PTEnhTypeDef;

// TOK_USEROPTIONBIT_1 prevents the fields from being included in the StructCopy for Node references
AUTO_STRUCT;
typedef struct PTPurchaseRequirements
{
	REF_TO(PTGroupDef) hGroup;	AST(REFDICT(PowerTreeGroupDef) USERFLAG(TOK_USEROPTIONBIT_1))
	int iGroupRequired;			AST(DEFAULT(1) USERFLAG(TOK_USEROPTIONBIT_1))

	int iTableLevel;			AST(DEFAULT(0) NAME(TableLevel, CharacterLevel) USERFLAG(TOK_USEROPTIONBIT_1))
	int iDerivedTableLevel;		AST(NO_TEXT_SAVE)

	// Required points spent in this tree
	int iMinPointsSpentInThisTree;	AST(NAME(PointsSpentInThisTree, MinPointsSpentInThisTree))

	// Maximum points spent in this tree
	int iMaxPointsSpentInThisTree;	AST(NAME(MaxPointsSpentInThisTree))

	// Required points spent in ANY tree
	int iMinPointsSpentInAnyTree;	AST(NAME(PointsSpentInAnyTree, MinPointsSpentInAnyTree))

	// Maximum points spent in ANY tree
	int iMaxPointsSpentInAnyTree;	AST(NAME(MaxPointsSpentInAnyTree))
	
	char *pchTableName;			AST(DEFAULT("TreePoints") USERFLAG(TOK_USEROPTIONBIT_1))

	Expression *pExprPurchase;	AST(NAME(PurchaseExpression) USERFLAG(TOK_USEROPTIONBIT_1) LATEBIND)
		// Evaluated to figure out if the character can purchase this (group/node)
}PTPurchaseRequirements;

AUTO_STRUCT AST_IGNORE(Scale);
typedef struct PTNodeRankDef
{
	REF_TO(PowerDef) hPowerDef;		AST(NAME(Power))
		// Ref to the power granted by this rank

	//const char *pchPowerDefName;	// AST(NAME(powerDefName) NO_TEXT_SAVE, POOL_STRING)
		// String of the PowerDef ref, used by client to fill in the ref when it actually needs the PowerDef

	PTPurchaseRequirements *pRequires;	AST(STRUCT(parse_PTPurchaseRequirements) NAME(Requires) ALWAYS_ALLOC)
		//Contains all the required information for this node to be purchased

	REF_TO(PTNodeDef) hTrainerUnlockNode;	AST(NAME(TrainerUnlockNode))
		//By acquiring this rank, this node will be unlocked to be trained by pets

	int iCost;						AST(DEFAULT(1))
		// Cost to purchase this rank.  Can be modified by the tree type's cost scale, if there is one.

	int iCostScaled;				AST(NO_TEXT_SAVE)
		// Derived.  This is either a simple copy of the iCost, or a copy of the iCost
		//  automatically scaled by the PowerTreeType's cost scaling, if that cost scaling
		//  only has one entry (and is therefore a static scale applied at all times). This
		//  field is only used internally for figuring out the cost of purchasing a rank
		//  without having to jump through the hoops to perform a single static multiply.

	char *pchCostVar;				AST(NAME(CostVar))
		// If set, overrides iCost. The value is cached in iCostScaled.

	char *pchCostTable;				AST(NAME(CostTable) DEFAULT("TreePoints")) 
		// The points table to look up

	bool bEmpty;
		// Can be set to true to allow a rank to not do anything at all.  Generally only
		//  useful for slave nodes, but this is a guideline, not a rule.

	bool bForcedAutoBuy;
		// If set, AutoBuy this rank even if it has a cost.

	U32 bIgnoreRequires : 1;		AST(NO_TEXT_SAVE)
		// Derived.  Set to true when the rank's requires is effectively a duplicate of the
		//  previous rank's requires, and so it's not necessary to evaluate it.

	U32 bVariableCost : 1;			AST(NO_TEXT_SAVE)
		// Derived.  Set to true when the cost varies due to the tree having a cost scale table.
		//  This really should just be on the PTNodeDef.

} PTNodeRankDef;

AUTO_STRUCT;
typedef struct PTNodeEnhancementDef
{
	REF_TO(PowerDef) hPowerDef;		AST(NAME(Power))
		// Ref to the power granted, should always be an Enhancement

	//const char *pchPowerDefName;	AST(NAME(powerDefName) NO_TEXT_SAVE, POOL_STRING)
		// String of the PowerDef ref, used by client to fill in the ref when it actually needs the PowerDef

	int iLevelMax;
		// Max level allowed for the Enhancement.  0 means it can not be purchased at all.

	int iCost;						AST(DEFAULT(1.0))
		// Cost of this Enhancement

	REF_TO(PTEnhTypeDef) hEnhType;	AST(NAME(EnhType) REFDICT(PTEnhTypeDef))
		// Ref to the enhancement type for purchase rules

	char *pchCostTable;				AST(NAME(CostTable) DEFAULT("EnhPoints")) 
		// The points table to look up
} PTNodeEnhancementDef;

AUTO_STRUCT;
typedef struct PTNodeUICategories
{
	const char **pchNames; AST(NAME(UICategoryName))
} PTNodeUICategories;

extern int g_iNumOfPowerNodeUICategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pNodeUICategories);
typedef enum PTNodeUICategory
{
    kPTNodeUICategory_None = 0, ENAMES(None)
} PTNodeUICategory;


// TOK_USEROPTIONBIT_1 prevents the fields from being included in the StructCopy for Node references
AUTO_STRUCT AST_IGNORE(KeyBind) AST_IGNORE_STRUCT(Templates) AST_IGNORE(GrantInvSlot);
typedef struct PTNodeDef
{
	char *pchName;								AST(USERFLAG(TOK_USEROPTIONBIT_1))
		// Internal name of this node

	const char *pchNameFull;					AST( STRUCTPARAM KEY POOL_STRING USERFLAG(TOK_USEROPTIONBIT_1))
		// Full internal name of this node (TreeName.GroupName.Name)

	DisplayMessage pDisplayMessage;				AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1))
		// Display name of this node

	DisplayMessage msgRequirements;				AST(STRUCT(parse_DisplayMessage) USERFLAG(TOK_USEROPTIONBIT_1))
		// Display message describing any purchase requirements


	REF_TO(PTNodeDef) hNodeRequire;				AST(REFDICT(PowerTreeNodeDef) USERFLAG(TOK_USEROPTIONBIT_1))	
		// The node required to purchase before this node

	int iRequired;								AST(DEFAULT(1) USERFLAG(TOK_USEROPTIONBIT_1))
		// !! One-Based !! - The rank required of the required node


	REF_TO(PTNodeDef) hNodeClone;				AST(NAME(Clone) USERFLAG(TOK_USEROPTIONBIT_1))
		// The full name of the node that this is a clone of

	REF_TO(PTNodeDef) hNodePowerSlot;			AST(NAME(NodePowerSlot))
		// If specified, any Power from this node will not be slottable in a PowerSlot.  Instead it
		//  will mirror the slotted state of the Power from the hNodePowerSlot node.

	REF_TO(PowerTreeDef) hTreeClone;			AST(NAME("") USERFLAG(TOK_USEROPTIONBIT_1))
		// The tree that this node is a clone from. Not written out to the file, but derived at load time

	REF_TO(PowerReplaceDef) hGrantSlot;			AST(NAME(GrantSlot) REFDICT(PowerReplaceDef))

	REF_TO(PTNodeTypeDef) hNodeType;			AST(NAME(NodeType) REFDICT(PTNodeTypeDef))

	PTNodeRankDef **ppRanks;					AST(NAME(Rank) NO_INDEX)
		// Ranks in this node

	PTNodeEnhancementDef **ppEnhancements;		AST(NAME(Enhancement))
		// Enhancements available in this node

	S32 iCostMaxEnhancement;
		// If non-zero, the maximum cost that can be spent on Enhancements

	
	AttribType eAttrib;							AST(NAME(Attrib) DEFAULT(-1))
		// Attribute awarded by purchasing this PTNode.  You get 1 per rank, unless there is an AttribPowerTable
		//  to scale it.

	const char *pchAttribPowerTable;			AST(NAME(AttribPowerTable) POOL_STRING)
		// If this is a valid PowerTable, the amount of the Attribute this PTNode gives per rank is from the
		//  PowerTable value at the current rank.

	PTNodeFlag eFlag;							AST(NAME(Flags) SUBTABLE(PTNodeFlagEnum) FLAGS)
		// The flags that define this node

	PowerPurpose ePurpose;						AST(SUBTABLE(PowerPurposeEnum))
		// Purpose used for the node if the node doesn't grant any powers

	const char *pchIconName;					AST(POOL_STRING)
		// Icon used for the node if the node doesn't grant any powers

	//U32 uiTimestampPurge;						NO_AST
		// Timestamp when it is ok to purge all the PowerDefs referenced by this node

	U32 bCloneSystem : 1;						AST(NO_TEXT_SAVE)
		// Derived.  Indicates that this NodeDef is a "clone" of another NodeDef, or another NodeDef
		//  is a "clone" of this.

	U32 bHasCosts : 1;							AST(NO_TEXT_SAVE)
		// Derived. Indicates that some ranks have a non-zero cost

	U32 bRankCostTablesVary : 1;				AST(NO_TEXT_SAVE)
		// Derived. Indicates that not all ranks have the same pchCostTable, which makes calculating
		//  points spent of a particular type slower.

	U32 bSlave : 1;								AST(NO_TEXT_SAVE)
		// Set to true if this node is a in a group with a master node, but is not the master node.  Set
		//  during group validate.

	U32 bForcedAutoBuy : 1;						AST(NO_TEXT_SAVE)
		// Derived. Set to true if any ranks in this node are set as ForcedAutoBuy.

	PTNodeUICategory eUICategory;				AST(NAME(UICategory))

	S8 iUIGridRow;
		// The row index in the UI grid. This field is only for UI representation e.g. a talent tree

	S8 iUIGridColumn;
		// The column index in the UI grid. This field is only for UI representation e.g. a talent tree
} PTNodeDef;


AUTO_STRUCT;
typedef struct PTNodeDefRef
{
	REF_TO(PTNodeDef) hNodeDef;	AST(NAME(NodeDef) STRUCTPARAM)
} PTNodeDefRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct PTNodeDefRefCont
{
	CONST_REF_TO(PTNodeDef)	hNodeDef; AST(NAME(NodeDef) REFDICT(PowerTreeNodeDef) STRUCTPARAM PERSIST SUBSCRIBE)
} PTNodeDefRefCont;

AUTO_STRUCT AST_CONTAINER;
typedef struct PowerPurchaseTracker
{
	const U32 uiTimeCreated;					AST(PERSIST SUBSCRIBE)
	const U32 uiOrderCreated;					AST(PERSIST SUBSCRIBE)

	const U32 bStepIsLocked : 1;				AST(PERSIST SUBSCRIBE)
		// If true, this is locked in and permanent. Used for powerhouse power locking in Champions

} PowerPurchaseTracker;

// Tracks the necessary purchase data for an Enhancement on a PTNode
AUTO_STRUCT AST_CONTAINER;
typedef struct PTNodeEnhancementTracker
{
	CONST_REF_TO(PowerDef)	hDef;								AST(PERSIST SUBSCRIBE)
		// The PowerDef of the Enhancement this is tracking

	CONST_EARRAY_OF(PowerPurchaseTracker) ppPurchases;			AST(PERSIST SUBSCRIBE)
		// PurchaseTracker for each purchased level of the enhancement

} PTNodeEnhancementTracker;

AUTO_STRUCT AST_CONTAINER;
typedef struct PTNode
{
	CONST_REF_TO(PTNodeDef)	hDef;								AST(PERSIST SUBSCRIBE REFDICT(PowerTreeNodeDef) KEY)
		// Ref to the node's def

	const int iRank;											AST(PERSIST SUBSCRIBE)
		// Current rank of this node (0-based)

	const bool bEscrow;											AST(PERSIST SUBSCRIBE)
		//In this words of the great JW: "something you have the right to purchase, and in some sense own, but don't have access to"

	U32 uiPowerReplaceID;
		// Derived - The powerslot granted by this node

	CONST_EARRAY_OF(PowerPurchaseTracker) ppPurchaseTracker;	AST(PERSIST SUBSCRIBE NO_INDEX)

	CONST_EARRAY_OF(Power) ppPowers;							AST(PERSIST SUBSCRIBE NO_INDEX FORCE_CONTAINER)
		// Actual Powers owned by this node

	CONST_EARRAY_OF(Power) ppEnhancements;						AST(PERSIST SUBSCRIBE NO_INDEX FORCE_CONTAINER)
		// Non-indexed array of Enhancement powers owned by this node

	CONST_EARRAY_OF(PTNodeEnhancementTracker) ppEnhancementTrackers;	AST(PERSIST SUBSCRIBE)
		// Array of Enhancement trackers, which keep purchase data.
		//  Should be 1-to-1 with the array of Enhancement powers after load fixup.

} PTNode;

AUTO_STRUCT AST_IGNORE(isFirst);
typedef struct PTGroupDef
{
	char *pchGroup;
	// Internal name of the power group

	char *pchNameFull;				AST(STRUCTPARAM KEY)		
		// Full Internal name of this power group (TreeName.GroupName)

	DisplayMessage pDisplayMessage;		AST(STRUCT(parse_DisplayMessage))
		// Display name of the power group

	DisplayMessage pDisplayDescription;	AST(STRUCT(parse_DisplayMessage))
		// Display message of the power group

	PTPurchaseRequirements *pRequires;	AST(STRUCT(parse_PTPurchaseRequirements) NAME(Requires) ALWAYS_ALLOC)
	//Contains all the required information for this group to be purchased

	int iMax; 
	//Max picks for the group

	int x;
	//position in the editor for the group window 
	
	int y;
	//position in the editor for the group window

	int iOrder;
		// iOrder that this should appear in the UI

	PTNodeDef **ppNodes;			AST(NAME(Node) NO_INDEX)
		//earray of the nodes in the group

	S8 iUIGridRow;
		// The row index in the UI grid. This field is only for UI representation e.g. a talent tree

	U32 bHasMasterNode : 1;			AST(NO_TEXT_SAVE)
		// Derived, set to true if any nodes in this group are marked as MasterNode

} PTGroupDef;

AUTO_ENUM;
typedef enum PowerTreeRelationship
{
	kPowerTreeRelationship_Unknown = 0,

	// This power tree depends on the linked one.
	kPowerTreeRelationship_DependsOn,

	// This power tree is a dependency of the linked one.
	kPowerTreeRelationship_DependencyOf,

	kPowerTreeRelationship_MAX, EIGNORE
} PowerTreeRelationship;

// Used to let power trees link to other power trees
AUTO_STRUCT;
typedef struct PowerTreeLink
{
	REF_TO(PowerTreeDef) hTree;
	PowerTreeRelationship eType;
} PowerTreeLink;

AUTO_STRUCT;
typedef struct PowerTreeUICategories
{
	const char **pchNames; AST(NAME(UICategoryName))
} PowerTreeUICategories;

extern int g_iNumOfPowerUICategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pUICategories);
typedef enum PowerTreeUICategory
{
    kPowerTreeUICategory_MAX, EIGNORE
} PowerTreeUICategory;

AUTO_ENUM;
typedef enum PowerTreeRespec
{
	kPowerTreeRespec_None,
		// Unable to respec this tree
	kPowerTreeRespec_Remove,
		// Remove this tree from exising on the character
	kPowerTreeRespec_Reset,
		// Keep this tree, but remove all the nodes and enhancements
} PowerTreeRespec;

AUTO_ENUM;
typedef enum Respec_Type
{
	kRespecType_All = 0,
	kRespecType_PowersAndTrees,	

} Respec_Type;

AUTO_STRUCT;
typedef struct RespecPowerTreePlayer
{
	PowerTreeDef *pPowerTreeDef;		AST(UNOWNED)
		//This is a powertree def returned by a GET_REF
	U32 iPurchaseTime;
} RespecPowerTreePlayer;

AUTO_STRUCT;
typedef struct RespecPowerPlayer
{
	PowerDef *pPowerDef;				AST(UNOWNED)
		//This is a power def returned by a GET_REF
	U32 iPurchaseTime;
} RespecPowerPlayer;

AUTO_STRUCT;
typedef struct RespecPowerTree
{
	CONST_REF_TO(PowerTreeDef) respecTreeDef;				AST(REFDICT(PowerTreeDef))
} RespecPowerTree;

AUTO_STRUCT;
typedef struct RespecPower
{
	CONST_REF_TO(PowerDef) respecPowerDef;					AST(REFDICT(PowerDef))
} RespecPower;


AUTO_STRUCT;
typedef struct Respec
{
	char *pchRespecTimeString;				AST(ADDNAMES(Date))
		//The string representation of the respec time in UTC

	U32 uiDerivedRespecTime;				AST(NO_TEXT_SAVE)
	//Derived from RespecTimeString

	Respec_Type eRespecType;				AST(SUBTABLE(Respec_TypeEnum))
	// The type of respec this is, default is all

	EARRAY_OF(RespecPower) eaPowers;		AST(NAME(RespecPowers))
	// earray of power names

	EARRAY_OF(RespecPowerTree) eaTrees;		AST(NAME(RespecPowerTrees))	
	// earray of tree names

	U32 bIsForcedRespec : 1;
	// when the character logs in, they will have a forced full respec on their character if they satisfy the eRespecType. 

} Respec;

AUTO_STRUCT;
typedef struct Respecs
{
	Respec **eaRespecs;
} Respecs;

AUTO_STRUCT;
typedef struct PowerTreeClientInfo
{
	// Internal name of the power tree
	const char *pchName;				AST(POOL_STRING SUBSCRIBE)

	// The display message
	REF_TO(Message) hDisplayMessage;	AST(SUBSCRIBE REFDICT(Message) VITAL_REF)

} PowerTreeClientInfo;

AUTO_STRUCT;
typedef struct PowerTreeClientInfoList
{
	// The list of power trees
	PowerTreeClientInfo **ppTrees;		AST(SUBSCRIBE)

	DirtyBit dirtyBit;					AST(NO_NETSEND)
} PowerTreeClientInfoList;

// Basic definition of a power tree
AUTO_STRUCT;
typedef struct PowerTreeDef
{
	char *pchName;								AST( STRUCTPARAM KEY POOL_STRING)
		// Internal name of the power tree

	PowerTreeRespec eRespec;					AST(DEFAULT(kPowerTreeRespec_Remove))
		// Do not allow the player to respect this tree

	DisplayMessage pDisplayMessage;				AST(STRUCT(parse_DisplayMessage) NAME(DisplayMessage) NAME(DisplayName))
		// Translated version of the display name

	DisplayMessage pDescriptionMessage;			AST(STRUCT(parse_DisplayMessage))
		// Description of the power tree
			
	const char* pchIconName;					AST(NAME(IconName) POOL_STRING)
		// optional icon for the tree

	REF_TO(PTTypeDef) hTreeType;				AST(NAME(TreeType) REFDICT(PowerTreeTypeDef))
		// The Type and Rules for this power tree

	Expression *pExprRequires;					AST(NAME(ExprBlockRequires, RequiresBlock), REDUNDANT_STRUCT(Requires,parse_Expression_StructParam),LATEBIND)
		// The Expression 

	PTGroupDef **ppGroups;						AST(NAME(Group) NO_INDEX)
		// Groups in this tree

	const char *pchFile;						AST(CURRENTFILE)
		// Current file (required for reloading)

	REF_TO(CharacterClass) hClass;				AST(NAME(Class) REFDICT(CharacterClass))
		// Reference to the class

	PowerTreeTopDown *pTopDown;					AST(CLIENT_ONLY NO_WRITE)
		// A top-down version of the tree, cached for UI purposes.

	PowerTreeLink **ppLinks;					AST(NO_TEXT_SAVE)

	PowerTreeUICategory eUICategory;			AST(NAME(UICategory))

	const char *pchDefaultPlayingStyle;			AST(POOL_STRING NAME(DefaultPlayingStyle))

	char *pchMaxSpendablePointsCostTable;		AST(NAME(MaxSpendablePointsCostTable)) 
		// The cost table to look up. Overrides all rank cost tables.
		
	F32 fMaxSpendablePoints;					AST(NAME(MaxSpendablePoints))
		// Ratio of spendable points from the specified cost table: (spent / earned)

	S32 iMinCost;								AST(NO_TEXT_SAVE)
		// The min cost for all ranks/enhancements of this tree. Used for calculating max spendable points
		// Derived at load time

	bool bAutoBuy;
		// Auto give this tree to characters

	bool bTemporary;
		// If the Tree is Temporary (available Powers granted as Temporary upon login)
		//  No, this isn't a bitfield, because the PTEditor doesn't handle bitfields

	bool bSendToClient;
		// If set, a minimal power tree information is sent to all clients for the entities which own this power tree

	U32 bIsTalentTree;
		// Determines if the tree is setup as a talent tree.
		// This is purely for editor and UI purposes. 
		// Talent trees and regular power trees work in exact same way in terms of functionality.

	REF_TO(AIAnimList) hAnimListToPlayOnGrant;	AST(NAME("AnimListToPlayOnGrant") REFDICT(AIAnimList))
		// This is the anim list to be played whenever the player gains a power tree node from this tree

	DisplayMessage pGrantMessage;				AST(STRUCT(parse_DisplayMessage))
		// The notification text sent to the client whenever the player gains a power tree node from this tree

} PowerTreeDef;

AUTO_STRUCT;
typedef struct PowerTreeDefRef
{
	REF_TO(PowerTreeDef) hRef;	AST(STRUCTPARAM)
} PowerTreeDefRef;

AUTO_STRUCT;
typedef struct PTGroupTopDown
{
	REF_TO(PTGroupDef) hGroup; AST(REFDICT(PowerTreeGroupDef) NAME(Group))

	PTGroupTopDown **ppGroups;
	
	PTNodeTopDown **ppOwnedNodes;
	S32 iDepth;
	S32 iCount;
}PTGroupTopDown;

AUTO_STRUCT;
typedef struct PTNodeTopDown
{
	REF_TO(PTNodeDef) hNode; AST(REFDICT(PowerTreeNodeDef) NAME(Node))

	PTNodeTopDown **ppNodes;
	S32 iDepth;
	S32 iCount;
}PTNodeTopDown;

AUTO_STRUCT;
typedef struct PowerTreeTopDown
{
	REF_TO(PowerTreeDef) hTree; AST(NAME(Tree))
	
	PTGroupTopDown **ppGroups;
	int iWidth;
	int iHeight;
}PowerTreeTopDown;

AUTO_STRUCT AST_CONTAINER;
typedef struct PowerTree
{
	DirtyBit dirtyBit;							AST(NO_NETSEND)

	CONST_REF_TO(PowerTreeDef) hDef;			AST(PERSIST SUBSCRIBE REFDICT(PowerTreeDef) KEY)
		// Def of tree

	CONST_EARRAY_OF(PTNode) ppNodes;			AST(PERSIST SUBSCRIBE)
		// Nodes owned in the tree

	const U32 uiTimeCreated;					AST(PERSIST SUBSCRIBE)
		// Time in SecondsSince2000 that this was originally purchased.  If unset, is derived upon
		//  login from the earliest time we can find in the Powers of its nodes.

	const U32 bStepIsLocked : 1;				AST(PERSIST SUBSCRIBE)
		// If true, this is locked in and permanent. Used for powerhouse power locking in Champions

} PowerTree;


// Wrapper for earray of power trees
AUTO_STRUCT;
typedef struct PowerTreeDefs
{
	PowerTreeDef **ppPowerTrees; AST( NAME(PowerTree) )
		// EArray of all trees
} PowerTreeDefs;

extern PowerTreeDefs g_PowerTreeDefs;


// Structure that describes a granular step of purchasing a part of a PowerTree.  Used for
//  purchasing, validation and respec.  Currently those usages are not mingled, which means
//  it's not necessary to indicate on each step whether it's an add or a remove.
AUTO_STRUCT;
typedef struct PowerTreeStep
{
	const char *pchTree;			AST(POOL_STRING)
		// Tree for this step, should never be NULL

	const char *pchNode;			AST(POOL_STRING)
		// Node of the tree - if NULL, the step doesn't involve a node

	const char *pchEnhancement;		AST(POOL_STRING)
		// Enhancement of the node - if NULL, the step doesn't involve a node enhancement

	S32 iRank;
		// The rank of this step (rank of Node or Enhancement)

	U32 uiTimestamp;
		// The secondsSince2000 timestamp of this step (when the Tree, Node or Enhancement was purchased)

	U32 uiOrderIndex;
		// Optional ordering indices if this PowerTreeStep shares a timestamp with a neighboring PowerTreeStep

	S32 iCostRespec;
		// Optional generic cost to respec out of this PowerTreeStep

	S32 iStepsImplied;				AST(CLIENT_ONLY)
		// Optional additional implied PowerTreeStep in this PowerTreeStep
		//  Used by client to hide certain kinds of steps in the respec UI, but still know to step
		//  out of them when sending the respec request to the server.

	U32 bEscrow : 1;
		// If true, this is an attempt to place the Node into escrow, or an attempt to remove a Node
		//  that is in escrow

	U32 bStepIsLocked : 1;
		// If true, this is locked in and permanent. Used for powerhouse power locking in Champions

} PowerTreeStep;

// Set of PowerTreeSteps plus additional validation data, used for transactions/helpers
AUTO_STRUCT;
typedef struct PowerTreeSteps
{
	PowerTreeStep **ppSteps;
		// Steps - in order

	U32 uiPowerTreeModCount;
		// The Character's uiPowerTreeModCount when this set of steps was created, if relevant

	S32 iRespecSkillpointSpentMin;
		// Ugly STO requirement for the minimum number of the "Skillpoint" numeric that must
		//  be spent after a respec

	U32 bUpdatePointsSpent : 1;
		// Option to force a points spent update after processing the Steps, used for full free
		//  respecs.

	U32 bIsTraining : 1;
		// Allow purchasing of nodes flagged as 'RequireTraining'

	U32 bTransaction : 1;		NO_AST
		// Set to true by the VERY SPECIFIC transaction functions that are allowed to actually
		//  modify the PowerTrees.  Do NOT set this to true just because it fails if you don't.
		//  If this isn't set right, it means you're doing it wrong.

	U32 bOnlyLowerPoints : 1;
		// When updating points only lower them, do not raise them, this is used during targeted (group) respecs

} PowerTreeSteps;

AUTO_STRUCT;
typedef struct PowerTreeRespecName
{
	PTRespecGroupType eRespecGroup;

	const char *pchName;						AST(POOL_STRING)

	// The power table used for numeric respecs
	const char *pchTableName;						AST(POOL_STRING)
}PowerTreeRespecName;

// Struct defining the rules for PowerTree respec.  In theory this should be integrated into a general
//  RespecConfig, which would then probably be integrated into a general Config for character development,
//  but I'm being lazy for now.
AUTO_STRUCT;
typedef struct PowerTreeRespecConfig
{
	Expression *pExprCostStep;			AST(NAME(ExprCostStep) LATEBIND)
		// Expression evaluated on each PowerTreeStep to determine the iCostRespec

	Expression *pExprCostBase;			AST(NAME(ExprCostBase) LATEBIND)
		// Expression evaluated for each respec. 

	Expression *pExprRequiredPointsSpent;	AST(NAME(ExprRequiredPointsSpent) LATEBIND)
		// Expression evaluating how many points the player must have spent after respec
		// Currently only used when used in trPowerCart_BuyCartItemsWithRespec

	REF_TO(ItemDef) hNumeric;			AST(NAME(Numeric))
		// Numeric to be used to cost

	bool bForceUseFreeRespec;			AST(NAME(ForceUseFreeRespec))
		// If we should always use free respec numerics if available

	const char *pchFile;				AST(CURRENTFILE)
		// Current file (required for reloading)

	EARRAY_OF(PowerTreeRespecName) eaPTItemRespecNames;		AST(NAME(PTItemRespecNames))
		// Names for the numerics for respecing by category

	EARRAY_OF(PowerTreeRespecName) eaPTGadRespecNames;		AST(NAME(PTGadRespecNames))
		// Names for the GAD for respecing by category

	EARRAY_OF(PowerTreeRespecName) eaPTNumericRespecNames;		AST(NAME(PTNumericRespecNames))
		// Table Names for the Nunmeric for respecing by category

} PowerTreeRespecConfig;

// Globally accessible PowerTreeRespecConfig
extern PowerTreeRespecConfig g_PowerTreeRespecConfig;


AUTO_STRUCT AST_CONTAINER;
typedef struct SavedCartPower
{
	REF_TO(PTNodeDef) hNodeDef; AST(NAME(NodeDef) REFDICT(PowerTreeNodeDef), PERSIST, NO_TRANSACT)

	S32 iRank;

} SavedCartPower;

AUTO_STRUCT;
typedef struct SavedCartPowerList
{
	SavedCartPower** ppNodes;

} SavedCartPowerList;

extern Respec **g_eaRespecs;

// Basic def lookup functions
PowerTreeDef *powertreedef_Find(const char *pchName);
PTNodeDef *powertreenodedef_Find(const char *pchName);
PTGroupDef *powertreegroupdef_Find(const char *pchName);

// Creates a PowerTree (and initialized the timestamp)
NOCONST(PowerTree)* powertree_Create(PowerTreeDef *pTreeDef);


// Derives the scale and executed power of each node in the tree
void powertree_FinalizeNodes(NOCONST(PowerTree) *pConstTree);

NOCONST(PTNode) *character_FindPowerTreeNodeHelper(ATH_ARG NOCONST(Character)* pChar, ATH_ARG NOCONST(PowerTree) **ppTreeOut, const char *pchNode);
SA_RET_OP_VALID PTNode *powertree_FindNode(Character *pChar, PowerTree **pTreeOut, const char *pchNode);

void powertrees_Load(void);

PTNode *powertreenode_create(PTNodeDef *pNode);

bool powertrees_Load_RankValidate(PTNodeRankDef *pRank, int iRank, PTNodeDef *pNode, PTGroupDef *pGroup, PowerTreeDef *pTree);
bool powertrees_Load_NodeValidate(PTNodeDef *pNode, int iNode, PTGroupDef *pGroup, PowerTreeDef *pTree);
bool powertrees_Load_GroupValidate(PTGroupDef *pGroup, int iGroup, PowerTreeDef *pTree);
bool powertrees_Load_TreeValidate(PowerTreeDef *pTree);

// Returns the highest activatable Power from the node
SA_RET_OP_VALID Power *powertreenode_GetActivatablePower(SA_PARAM_NN_VALID PTNode *pNode);

ExprContext* powertree_GetContext(void);
ExprContext* powertreetypes_GetContext(void);
ExprContext* powertreeenhtypes_GetContext(void);
ExprContext* ptpurchaserequirements_GetContext(void);

// Return the power that this node will let you buy, or the most recent
// power it did buy.
PowerDef *powertree_PowerDefFromNode(PTNodeDef *pDef, S32 iRank);
const char *powertree_PowerDefNameFromNode(PTNodeDef *pDef, S32 iRank);

// Returns the node if the character owns it, or null
PTNode *character_GetNode(Character *p, const char *pchNameFull);

const char *powertreenodedef_GetDisplayName(PTNodeDef *pDef);

#ifdef PURGE_POWER_TREE_POWER_DEFS

// Requests that the node fill in all its PowerDef references
void powertreenodedef_RequestPowerDefs(SA_PARAM_NN_VALID PTNodeDef *pdef);

// Purges old requested PowerDef references from all PowerTreeDefs in the dictionary.
//  If a Character is provided, it will make sure PowerDef references relevant to that Character are kept around.
void powertrees_CleanPowerDefs(SA_PARAM_OP_VALID Character *pchar);

#endif


void powertreedefs_RemoveRefDict(PowerTreeDef *pTree);

SA_RET_OP_VALID PowerTreeTopDown *powertree_GetTopDown(SA_PARAM_OP_VALID PowerTreeDef *pTree);
void powertree_DestroyTopDown(SA_PRE_OP_VALID SA_POST_P_FREE PowerTreeTopDown *pTree);

// Returns a PowerTree in the Character's PowerTrees by the specified tree name
SA_RET_OP_VALID PowerTree *character_FindTreeByDefName(SA_PARAM_OP_VALID const Character *pchar,
													SA_PARAM_OP_STR const char* pchTreeName);

// Returns a Power owned by the Character's PowerTrees, otherwise returns NULL.
SA_ORET_OP_VALID Power *character_FindPowerByIDTree(SA_PARAM_NN_VALID const Character *pchar,
												U32 uiID,
												SA_PARAM_OP_VALID PowerTree **ppTreeOut,
												SA_PARAM_OP_VALID PTNode **ppNodeOut);

// Returns a Power owned by the Character's PowerTrees, otherwise returns NULL.
SA_RET_OP_VALID Power *character_FindPowerByDefTree(SA_PARAM_NN_VALID const Character *pchar,
												 SA_PARAM_NN_VALID PowerDef *pdef,
												 SA_PARAM_OP_VALID PowerTree **ppTreeOut,
												 SA_PARAM_OP_VALID PTNode **ppNodeOut);

// Returns true if the Character is allowed to add or remove an Enhancement level
int character_CanEnhanceNode(int iPartitionIdx,
							 SA_PARAM_NN_VALID Character *pchar,
							 SA_PARAM_NN_STR const char *pchTree,
							 SA_PARAM_NN_STR const char *pchNode,
							 SA_PARAM_NN_STR const char *pchEnhancement,
							 int bAdd);

// Makes sure all Powers in the Character's PowerTrees are properly ranked and scaled
void character_PowerTreesFixup(SA_PARAM_NN_VALID Character *pchar);

// Appends Enhancements from the Character's PowerTrees to the earray of Enhancements attached to the Power
void power_GetEnhancementsTree(SA_PARAM_NN_VALID const Character *pchar,
							   SA_PARAM_NN_VALID Power *ppow,
							   SA_PARAM_NN_VALID Power ***pppEnhancements);

// Walks the Character's PowerTrees and adds all the available Powers to the general Powers list
bool character_AddPowersFromPowerTrees(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

void powertree_PrintTopDown(PowerTreeTopDown *pTree);
S32 character_CountOwnedInGroup(Character *pChar, PTGroupDef *pGroup);
bool powertree_CountOwnedInGroupHelper(ATH_ARG NOCONST(PowerTree)* pTree, PTGroupDef* pGroup);

bool PowerTree_GetAllAvailableNodes(int iPartitionIdx, Character *pChar, PTNodeDef ***pppNodesOut);
bool powertree_NodeHasPropagationPowers(PTNodeDef* pNodeDef);
bool powertree_CharacterHasTrainerUnlockNode(Character* pChar, PTNodeDef* pFindNodeDef);
void powertree_CharacterGetTrainerUnlockNodes(Character* pChar, PTNodeDef ***pppNodesOut);

void powertree_FindLevelRequirements(PowerTreeDef *pDef, S32 **peaiLevels);

bool powertree_GroupNameFromNode(SA_PARAM_OP_VALID PTNode *pNode, SA_PRE_NN_NN_STR char **ppchGroup);
bool powertree_GroupNameFromNodeDef(SA_PARAM_OP_VALID PTNodeDef *pNodeDef, SA_PRE_NN_NN_STR char **ppchGroup);
SA_RET_OP_VALID PTGroupDef *powertree_GroupDefFromNode(SA_PARAM_OP_VALID PTNode *pNode);
SA_RET_OP_VALID PTGroupDef *powertree_GroupDefFromNodeDef(SA_PARAM_OP_VALID PTNodeDef *pNodeDef);
bool powertree_TreeNameFromNodeDefName(const char *pchNodeName, char **ppchTree);
bool powertree_TreeNameFromNodeDef(SA_PARAM_OP_VALID PTNodeDef *pNodeDef, SA_PRE_NN_NN_STR char **ppchTree);
SA_RET_OP_VALID PowerTreeDef *powertree_TreeDefFromNodeDef(SA_PARAM_OP_VALID PTNodeDef *pNodeDef);
bool powertree_TreeNameFromGroupDef(PTGroupDef *pGroupDef, char **ppchTree);
SA_RET_OP_VALID PowerTreeDef *powertree_TreeDefFromGroupDef(SA_PARAM_OP_VALID PTGroupDef *pGroupDef);

// Compares two PTNodes by their current rank's power purpose
S32 ComparePTNodesByPurpose(const PTNode** a, const PTNode** b);

// Returns the cost table of the first rank in the node definition
SA_RET_OP_VALID const char * powertree_CostTableOfFirstRankFromNodeDef(SA_PARAM_NN_VALID const PTNodeDef *pNodeDef);

// Return pointer to the numeric respec group
SA_RET_OP_VALID const PowerTreeRespecName *PowerTree_GetRespecGroupNumeric(PTRespecGroupType eRespecType);

// return true if there is a valid table for this respec group
bool PowerTree_CanRespecGroupWithNumeric(PTRespecGroupType eRespecType);

#endif
