#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "interaction_common.h"

typedef struct Entity Entity;
typedef struct InteractOption InteractOption;
typedef struct TargetableNode TargetableNode;
typedef struct TooltipNode TooltipNode;
typedef struct WorldInteractionNode WorldInteractionNode;

AUTO_ENUM;
typedef enum UIInteractType
{
	UIInteractType_None = 0, // Do not change the value of UIInteractType_None
	UIInteractType_Contact,
	UIInteractType_Loot,
	UIInteractType_Clicky,
	UIInteractType_NamedPoint,
	UIInteractType_Door
} UIInteractType;

AUTO_STRUCT;
typedef struct InteractString
{
	char*	string;						AST(NAME(String) ESTRING)
} InteractString;

AUTO_STRUCT;
typedef struct MouseoverInteractableInfo
{
	const char* pcName;					AST(UNOWNED)
	const char* pcString;				AST(UNOWNED)
	const char* pchRequirementString;	AST(UNOWNED)		// The skill required by an interact (parsed from the Attemptable/Usable expression)
	const char* pchUsabilityString;		AST(UNOWNED)		// A full string representing the usablity of the interact. May override the Requirement String.
	InteractValidity eValid;
	UIInteractType eType;
	InventoryBag** eaLootBags;			AST(UNOWNED)
	const char *pchCursorTexture;		AST(NAME(CursorTexture) POOL_STRING)
	const char *pchCursorName;			AST(NAME(CursorName) POOL_STRING)
	S16 siCursorHotSpotX;				AST(NAME(CursorHotSpotX))
	S16 siCursorHotSpotY;				AST(NAME(CursorHotSpotY))
} MouseoverInteractableInfo;


S32 interaction_findMouseoverInteractableEx(Entity **pInteractableEntity, MouseoverInteractableInfo* pInfoOut);
WorldInteractionNode* interaction_getTooltipNode();
void interaction_setTooltipFromTooltipNode(TooltipNode* pNode, MouseoverInteractableInfo* pInfo);
void interaction_setTooltipFromTargetableNode(TargetableNode* pNode, MouseoverInteractableInfo* pInfo);
bool interaction_InteractWithOption(SA_PARAM_NN_VALID Entity *pEnt, InteractOption *pOption, bool bClientValidation);
InteractOption* interaction_FindNodeInInteractOptions(WorldInteractionNode* pNode);
InteractOption* interaction_FindEntityInInteractOptions(EntityRef erEnt);
void gclInteract_FindBest(Entity *pEnt);

bool gclInteract_FindRecentQueueInteract(Entity *pEnt, const char *pchQueueName);

bool interactOverrideCursor(void);
bool unifiedInteractAtCursor(void);
bool interactSetOverrideAtCursor(Entity *e, 
	bool bDoInteract,
	Entity **ppEntAtCursorOut, 
	WorldInteractionNode **ppNodeAtCursorOut);
