#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct CBox CBox;

void gclDrawStuffOverObjects(void);
void gclWaypoint_UpdateGens(void);

F32 objGetScreenDist(WorldInteractionNode *pNode);
void objGetScreenBoundingBox(WorldInteractionNode *pNode, CBox *pBox, F32 *pfDistance, bool bClipToScreen, bool bIgnoreDimensionsAndUseCenterPoint);