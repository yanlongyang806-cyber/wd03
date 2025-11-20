#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct PowerDef	PowerDef;
typedef struct CharacterClass CharacterClass;
typedef struct Entity Entity;

bool im_IsNotDestructibleOrCanThrowObject(Entity* pEnt, WorldInteractionNode *pNode, UserData *pData);

bool im_EntityCanThrowObject(Entity* pEnt, WorldInteractionNode *pNode, F32 fOverrrideStrength);

bool im_IsNotDestructible(WorldInteractionNode *pNode);

S32 im_GetDeathPowerDefs(WorldInteractionNode *pNode, PowerDef ***pppDefs);

F32 im_GetMass(WorldInteractionNode *pNode);

CharacterClass *im_GetCharacterClass(WorldInteractionNode *pNode);

S32 im_GetLevel(WorldInteractionNode *pNode);

void im_MapUnload(void);

