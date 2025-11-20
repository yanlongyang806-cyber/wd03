#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynAnimGraph.h"
#include "dynMovement.h"

#define DEFAULT_ANIM_TEMPLATE_NODE_Y 50
#define ANIM_TEMPLATE_EDITED_DICTIONARY "AnimTemplate"

typedef struct DynAnimTemplateNode DynAnimTemplateNode;

extern DictionaryHandle hAnimTemplateDict;

AUTO_ENUM;
typedef enum eAnimTemplateType
{
	eAnimTemplateType_NotSet,
	eAnimTemplateType_Idle,
	eAnimTemplateType_Emote,
	eAnimTemplateType_Power,
	eAnimTemplateType_HitReact,
	eAnimTemplateType_Death,
	eAnimTemplateType_NearDeath,
	eAnimTemplateType_Block,
	eAnimTemplateType_TPose,
	eAnimTemplateType_PowerInterruptingHitReact,
	eAnimTemplateType_Movement,
}
eAnimTemplateType;
extern StaticDefineInt eAnimTemplateTypeEnum[];

AUTO_ENUM;
typedef enum eAnimTemplateNodeType
{
	eAnimTemplateNodeType_Normal,
	eAnimTemplateNodeType_Randomizer,
	eAnimTemplateNodeType_Start,
	eAnimTemplateNodeType_End,
}
eAnimTemplateNodeType;

AUTO_STRUCT;
typedef struct DynAnimTemplateNodeRef
{
	DynAnimTemplateNode* p; NO_AST
	int index; AST(DEF(-1))
}
DynAnimTemplateNodeRef;

AUTO_STRUCT;
typedef struct DynAnimTemplateSwitch
{
	DynAnimTemplateNodeRef next;
	const char* pcFlag;				AST(POOL_STRING)
	DynMovementDirection eDirection;
	bool bInterrupt_Depreciated;	AST(NAME(Interrupt))
}
DynAnimTemplateSwitch;
extern ParseTable parse_DynAnimTemplateSwitch[];
#define TYPE_parse_DynAnimTemplateSwitch DynAnimTemplateSwitch

AUTO_STRUCT;
typedef struct DynAnimTemplateDirectionalData
{
	DynAnimTemplateSwitch** eaSwitch;
}
DynAnimTemplateDirectionalData;
extern ParseTable parse_DynAnimTemplateDirectionalData[];
#define TYPE_parse_DynAnimTemplateDirectionalData DynAnimTemplateDirectionalData

AUTO_STRUCT;
typedef struct DynAnimTemplatePath
{
	DynAnimTemplateNodeRef next;
}
DynAnimTemplatePath;
extern ParseTable parse_DynAnimTemplatePath[];
#define TYPE_parse_DynAnimTemplatePath DynAnimTemplatePath

AUTO_STRUCT;
typedef struct DynAnimTemplateNode
{
	const char* pcName;  AST(POOL_STRING)
	DynAnimTemplateNodeRef defaultNext;
	DynAnimTemplateSwitch** eaSwitch;
	DynAnimTemplateDirectionalData** eaDirectionalData; AST(NO_TEXT_SAVE)
	DynAnimTemplatePath **eaPath;
	eAnimTemplateNodeType eType;
	bool bInterruptible;
	F32 fX;
	F32 fY;
}
DynAnimTemplateNode;
extern ParseTable parse_DynAnimTemplateNode[];
#define TYPE_parse_DynAnimTemplateNode DynAnimTemplateNode

AUTO_STRUCT;
typedef struct DynAnimTemplate
{
	const char* pcName; AST(POOL_STRING KEY)
	const char* pcFilename; AST(POOL_STRING CURRENTFILE)
	const char* pcComments;	AST(SERVER_ONLY)
	const char* pcScope;	AST(POOL_STRING SERVER_ONLY)
	eAnimTemplateType eType; AST(NAME(Type))
	DynAnimGraph *pDefaultsGraph; AST(NAME(DefaultsGraph))
	DynAnimTemplateNode** eaNodes;
	const char** eaFlags; AST(POOL_STRING NAME(Flags))
	bool bPointersFixed; NO_AST

	U32 uiReportCount; NO_AST
}
DynAnimTemplate;
extern ParseTable parse_DynAnimTemplate[];
#define TYPE_parse_DynAnimTemplate DynAnimTemplate

void dynAnimTemplateInit(DynAnimTemplate* pTemplate);
bool dynAnimTemplateNodeConnected(DynAnimTemplate* pTemplate, DynAnimTemplateNode* pNode);
bool dynAnimTemplateVerify(DynAnimTemplate* pTemplate);
void dynAnimTemplateLoadAll(void);

void dynAnimTemplateFreeNode(DynAnimTemplate* pTemplate, DynAnimTemplateNode* pNode);
void dynAnimTemplateFixIndices(DynAnimTemplate* pTemplate);
void dynAnimTemplateFixDefaultsGraph(DynAnimTemplate *pTemplate);
void dynAnimTemplateFixGraphNode(DynAnimTemplate *pTemplate, DynAnimTemplateNode *pTemplateNodeOld, DynAnimTemplateNode *pTemplateNodeNew);

int dynAnimTemplateCompareSwitchDisplayOrder(const void** pa, const void** pb);

int dynAnimTemplateGetSearchStringCount(const DynAnimTemplate *pTemplate, const char *pcSearchText);

bool dynAnimTemplateNodesAttached(	DynAnimTemplate *pTemplate,
									DynAnimTemplateNode *pTemplateNode1,
									DynAnimTemplateNode *pTemplateNode2,
									bool bAllowNormalNodesInbetween);

bool dynAnimTemplateHasMultiFlagStartNode(DynAnimTemplate *pTemplate);