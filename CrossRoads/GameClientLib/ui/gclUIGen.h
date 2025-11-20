#ifndef GCL_UI_GEN_H
#define GCL_UI_GEN_H
GCC_SYSTEM

#include "UIGen.h"
#include "referencesystem.h"
#include "SavedPetCommon.h"

typedef U32 ContainerID;
typedef U32 EntityRef;
typedef struct AlwaysPropSlotClassRestrictDef AlwaysPropSlotClassRestrictDef;
typedef struct AlwaysPropSlotDef AlwaysPropSlotDef;
typedef struct BasicTexture BasicTexture;
typedef struct CBox CBox;
typedef struct Character Character;
typedef struct DynFx DynFx;
typedef struct Entity Entity;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct GfxCameraView GfxCameraView;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct HeadshotStyleDef HeadshotStyleDef;
typedef struct MinimapWaypoint MinimapWaypoint;
typedef struct PlayerCostume PlayerCostume;
typedef struct PTEnhTypeDef PTEnhTypeDef;
typedef struct PTGroupDef PTGroupDef;
typedef struct PTNodeDef PTNodeDef;
typedef struct PTNodeEnhancementDef PTNodeEnhancementDef;
typedef struct PTPurchaseRequirements PTPurchaseRequirements;
typedef struct PowerDef PowerDef;
typedef struct PowerSubtargetCategory PowerSubtargetCategory;
typedef struct PowerTree PowerTree;
typedef struct PowerTreeDef PowerTreeDef;
typedef struct UIGen UIGen;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct AttribModNet AttribModNet;
typedef struct Item Item;
typedef struct ItemDef ItemDef;

typedef void (*HeadshotNotifyBytesF)( UserData userData, U8* bytes, int width, int height, char **ppExtraInfo );

// Entities/Objects must be at least this far in front of the camera to be considered onscreen.
#define ENTUI_MIN_FEET_FROM_CAMERA 0.1f

// Entities/Objects must be no farther than this to be considered to be considered onscreen.
#define ENTUI_MAX_FEET_FROM_CAMERA 300.f

// Entities/Objects within this many pixels of screen edge are still considered on-screen.
#define ENTUI_SCREEN_EDGE_FUZZINESS 100

// The minimum size an auto-complete prefix should be before we provide suggestions
#define MIN_COMPLETE_PREFIX_LENGTH 3

#define UI_SETTINGS_DEFAULT_FILENAME "ui_settings.txt"

AUTO_ENUM AEN_APPEND_TO(UIGenType);
typedef enum GCLGenTypes
{
	// Deprecated
	kUIGenTypeMiniMap_Old = kUIGenType_MAX,

	// This gen is a special type that is instanced for each entity,
	// used for targeting info, etc.
	kUIGenTypeEntity,

	kUIGenTypeObject,

	// UIGenChatLog is a specialty widget for displaying the user's chat log.
	kUIGenTypeChatLog,

	// A compass with entity / waypoint annotations on it.
	kUIGenTypeCompass,

	// An improved map type.
	kUIGenTypeMap,

	// A special type used for waypoint HUD icons
	kUIGenTypeWaypoint,

	// UIGenPaperdoll is a gen designed to generate headshots
	kUIGenTypePaperdoll,

	kUIGenType_GCLMAX, EIGNORE
} GCLGenTypes;

//this is redundant as the powers system has an enum that is almost identical
AUTO_ENUM;
typedef enum UIGen3DFxParamType
{
	UIGen3DFxParamType_STR,
	UIGen3DFxParamType_FLT,
	UIGen3DFxParamType_INT,
	UIGen3DFxParamType_VEC,
} UIGen3DFxParamType;

extern StaticDefineInt UIGen3DFxParamTypeEnum[];

AUTO_STRUCT;
typedef struct UIGen3DFxParam
{
	const char*			pchName;	AST(POOL_STRING STRUCTPARAM)
	UIGen3DFxParamType	eType;		AST(NAME(Type) STRUCTPARAM)
	const char**		ppchVals;	AST(POOL_STRING STRUCTPARAM)
} UIGen3DFxParam;

AUTO_STRUCT;
typedef struct UIGen3DFx
{
	const char *pchFxName;		AST(POOL_STRING)
	UIGen3DFxParam** eaParams;	AST(NAME("Param"))
} UIGen3DFx;

AUTO_STRUCT;
typedef struct UIGenEntity
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeEntity))
	UIGen3DFx **ppEntityFx;
} UIGenEntity;

AUTO_STRUCT;
typedef struct UIGenObject
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeObject))

	//If the object is now an ent, this is the ref to that ent
	EntityRef eObjectEnt;
	UIGen3DFx **ppObjectFx;
} UIGenObject;

AUTO_STRUCT;
typedef struct UIGenWaypoint
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeWaypoint))
} UIGenWaypoint;

AUTO_STRUCT;
typedef struct UIGen3DFxState
{
	const char *pchFxName; AST(POOL_STRING)
	REF_TO(DynFx) hFx;
} UIGen3DFxState;

AUTO_STRUCT;
typedef struct UIGenEntityState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeEntity))
	EntityRef hEntity; AST(NAME(EntityRef))

	UIGen3DFxState **ppEntityFxState;

	F32 fScreenDist;
} UIGenEntityState;

AUTO_STRUCT;
typedef struct UIGenObjectState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeObject))
	REF_TO(WorldInteractionNode) hKey;		AST(NAME(Key))
	EntityRef hEntity;

	UIGen3DFxState *pObjectFxState;

	F32 fScreenDist;
} UIGenObjectState;

AUTO_STRUCT;
typedef struct UIGenWaypointState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeWaypoint))
	
	MinimapWaypoint *pWaypoint; NO_AST
} UIGenWaypointState;

// Tracks AttribMod StackGroup data for the UI
AUTO_STRUCT;
typedef struct UIStackGroupData
{
	U32 offAttrib;
	U32 offAspect;
	U32 eStackGroup;
	S32 iModNetIdxBest;
	S32 iMagnitudeBest;
	S32* piModNetIndices;
} UIStackGroupData;

AUTO_STRUCT;
typedef struct UIStackGroupEntityData
{
	ContainerID uEntID; AST(KEY)
	U32 uLastUpdateFrame;
	UIStackGroupData** eaStackGroupData;
} UIStackGroupEntityData;

// Keeps track of how many times a buff has been stacked. Used by the UI only. 
AUTO_STRUCT;
typedef struct AttribModStack
{
	AttribModNet *pModNet;			AST(UNOWNED)
	UIStackGroupData *pStackGroup;	AST(UNOWNED)
	U32 uiStack;
	S32 iUpdateIndex; 
	// Used to decide if this has been updated this frame. 
	// This is just used as an optimization to avoid per frame allocations
} AttribModStack;

AUTO_STRUCT;
typedef struct PowerDefBuffList
{
	AttribModStack **eaModStack;

	S32 iSize;
	// Keeps track of the size of eaModStack. This is done so that I can 
	// simply overwrite whatever is there and manually adjust the size to 
	// match, rather than clear it and allocate new memory every frame. 
} PowerDefBuffList;

AUTO_STRUCT;
typedef struct AttribModBuffData
{
	U32* eaModNetIdx;
	UIStackGroupData *pStackGroup;	AST(UNOWNED)
} AttribModBuffData;

AUTO_ENUM;
typedef enum EntityBuffType
{
	kEntityBuffType_PowerDef,
	kEntityBuffType_AttribMod,
} EntityBuffType;

AUTO_STRUCT;
typedef struct EntityBuffData
{
	EntityBuffType eType;
	PowerDefBuffList PowerDefData;
	AttribModBuffData AttribModData;
	S32 iUpdateIndex;
} EntityBuffData;

// Tracks the data used by the UI to describe a buff indicator of some sort on an Entity
AUTO_STRUCT;
typedef struct UIGenEntityBuff
{
	const char *pchIcon;		AST(POOL_STRING)
		// The name of the texture for the icon

	const char *pchDescShort;	AST(UNOWNED)
		// The localized display name of the buff

	const char *pchDescLong;	AST(UNOWNED)
		// The localized short description of the buff

	const char *pchDescVeryLong;	AST(UNOWNED)
		// The localized long description of the buff

	const char *pchNameInternal;	AST(UNOWNED)
		// The internal name of the buff (only set if you're over AL 0)

	char *pchAutoDesc;			AST(ESTRING)
		// The AutoDesc of the effects of the buff

	S32 ePowerTag;				AST(NAME(PowerTag))
		// The power tag of this buff

	U32 uiDuration;				AST(NAME(Duration))
		// The remaining duration of the buff

	U32 uiDurationOriginal;		AST(NAME(DurationOriginal))
		// The original duration of the buff

	U32 uiStack;				AST(NAME(Stack))
		// The number of times this buff has been applied

	S32 iLifetimeRemaining;		AST(NAME(LifetimeRemaining, LifetimeUsageLeft))

	F32 fResist;				AST(NAME(Resist))

	U8 bHasLifetimeTimer : 1;	AST(NAME(HasLifetimeTimer, HasLifetimeUsage))

	REF_TO(PowerDef) hPowerDef;
	
	U8 bNoDuration: 1;			NO_AST

	EntityBuffData *pData;		NO_AST
} UIGenEntityBuff;

// Tracks the data used by the UI to describe a buff indicator for a subtarget of some sort on an Entity
AUTO_STRUCT;
typedef struct UIGenEntitySubTargetBuff
{
	PowerSubtargetCategory* pCategory;	AST(UNOWNED)
	
	const char *pchIcon;				AST(POOL_STRING)
	// The name of the texture for the icon

	const char *pchDescShort;			AST(UNOWNED)
	// The localized display name of the buff

	const char *pchDescLong;			AST(UNOWNED)
	// The localized short description of the buff

	const char *pchDescVeryLong;		AST(UNOWNED)
	// The localized long description of the buff

	F32 fPercentHealth;
	// [0-1] Health percentage

	S32	iCount;
	// Number of SubTargets 
} UIGenEntitySubTargetBuff;

AUTO_STRUCT;
typedef struct PTNodeUpgrade
{
	REF_TO(PowerDef) hPowerDef;		AST(NAME(Power))
	REF_TO(PTNodeDef) hNode;		AST(REFDICT(PowerTreeNodeDef) NAME(Node))
	int iCost;						AST(DEFAULT(1))

	REF_TO(PTEnhTypeDef) hEnhType;	AST(NAME(EnhType) REFDICT(PTEnhTypeDef))
	const char *pchCostTable;		AST(NAME(CostTable) POOL_STRING) 
	PTPurchaseRequirements *pRequires;	AST(STRUCT(parse_PTPurchaseRequirements) UNOWNED)

	S32 iRank;
	bool bIsRank;

} PTNodeUpgrade;

AUTO_STRUCT;
typedef struct ClientMapNameRequestInfo
{
	REF_TO(Message) hMessage;
	U32 uLastRequestTime;
} ClientMapNameRequestInfo;

AUTO_STRUCT;
typedef struct AlwaysPropSlotData
{
	// The def of this prop slot
	REF_TO(AlwaysPropSlotDef) hDef; AST(REFDICT(AlwaysPropSlotDef))

	// The type of the class restrictions
	AlwaysPropSlotClassRestrictType eRestrictType;

	// The class restrictions of this prop slot
	AlwaysPropSlotClassRestrictDef* pRestrictDef; AST(UNOWNED)

	// Name of the prop
	char *pchName;

	// Class of the prop
	const char *pchClassDisplayName; AST(UNOWNED)

	// The puppet this slot belongs to
	U32 uiPuppetID;

	// The prop slot
	S32 iSlotID;

	// The pet id of the pet in the prop slot (not ContainerID)
	U32 uiPetID;

	// The container id of the pet in the prop slot
	ContainerID	iID;

	// If this slot data represents an item, this is the item
	Item *pPropItem; AST(UNOWNED)

	// If this slot data represents an item, this is the def
	REF_TO(ItemDef) hPropItemDef;

	// The inventory key of the prop item
	char *pchPropItemKey;
} AlwaysPropSlotData;

// Used for UIGenMovableBox to remember positions.
bool gclui_GenGetPosition(const char *pchName, UIPosition *pPosition, S32 iVersion, U8 chClone, U8 *pchPriority, const char **ppchContents);
bool gclui_GenSetPosition(const char *pchName, const UIPosition *pPosition, S32 iVersion, U8 chClone, U8 chPriority, const char *pchContents);

char *ConvertNameToFit(const char* longName, int useLastName, int maxFitLen);
char *ConvertTextToFit(const char* longText, int maxFitLen);

void CreateScreenBoxFromScreenPosition(CBox *pBox, CBox *pScreen, Vec2 vScreenPos, F32 width, F32 height);
bool ProjectPointOnScreen(Vec3 vPos, GfxCameraView *pView, CBox *pBox, CBox *pScreen, Vec2 vScreenPos);
bool ProjectCBoxOnScreen(Vec3 vPos, GfxCameraView *pView, CBox *pBox, CBox *pScreen, F32 width, F32 height);
bool IsScreenPositionValid(Vec2 vScreenPos, S32 iSlop);

AUTO_STRUCT;
typedef struct UIGenExpressionContainer
{
	const char *pchName; AST(POOL_STRING KEY REQUIRED)
	Expression *pExpression; AST(NAME(ExpressionBlock) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
	S32 iForceRebuild;
} UIGenExpressionContainer;

extern UIGenExpressionContainer g_ShowPayExpression;

// Expression to decide which EntityGen, ObjectGen, or WaypointGen to use, if any.
extern UIGenExpressionContainer g_EntityGenExpression;
extern UIGenExpressionContainer g_ObjectGenExpression;
extern UIGenExpressionContainer g_WaypointGenExpression;
extern UIGenExpressionContainer g_EntityGenOffscreenExpression;
extern UIGenExpressionContainer g_ObjectGenOffscreenExpression;
extern UIGenExpressionContainer g_WaypointGenOffscreenExpression;

extern void gclGenFillEntityNameSuggestions(UIGenTextEntryCompletion ***peaCompletion, const char *pchPrefix, S32 iPrefixReplaceFrom, S32 iPrefixReplaceTo, bool bAllowPartialReplace, bool bDoMinimalExpansion, bool bAddQuotesIfNeeded, bool bAddCommaIfNeeded, bool bAddTrailingSpace, bool bAppendRecentChatReceivers);

F32 AttribModMagnitudePct(SA_PARAM_OP_VALID Entity *pent, const char* attribName, S32 index);
S32 AttribModMagnitudeEx(Entity *pent, const char* attribName, S32 index, S32 *piTags);
F32 ShieldPctOriented(SA_PARAM_OP_VALID Entity *pEnt, F32 fAngle);

const char* gclGetSecondsAsHMS(S32 iSeconds, bool bShorten, bool bHideSeconds);
void gclFormatSecondsAsHMS(char** pestrResult, S32 iSeconds, S32 iShortenOutputOverSeconds, const char* pchFormatMessageKey);
const char* gclGetBestIconName(const char* pchIconName, const char* pchDefaultIcon);
const char* gclGetBestPowerIcon(const char* pchIcon, const char* pchLastIcon);

SA_RET_OP_VALID BasicTexture* gclHeadshotFromCostume(	SA_PARAM_OP_VALID const char* pchHeadshotStyleDef,
													SA_PARAM_OP_VALID const PlayerCostume *pHeadshotCostume,	// Costume pointer
													SA_PARAM_OP_VALID const char* pchCostume,				// Name of costume reference (used if costume ptr is NULL)
													F32 fWidth, F32 fHeight,
													HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData );
const char* gclRequestMapDisplayName(const char* pchMapNameMsgKey);

S32 gclGetMaxBuyablePowerTreeNodesInGroup(Entity* pEnt, PowerTree* pTree, PTGroupDef* pGroupDef, bool bCountAvailable, PTNodeDef*** pppAvailableNodes);


#endif
