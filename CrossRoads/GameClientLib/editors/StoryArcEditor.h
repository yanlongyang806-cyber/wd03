/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once
GCC_SYSTEM
//
// StoryArcEditor.h
//

#ifndef NO_EDITORS

typedef struct GameProgressionNodeDef GameProgressionNodeDef;
typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

typedef struct StoryArcEditDoc
{
	GameProgressionNodeDef *pNodeDef;
	GameProgressionNodeDef *pOrigNodeDef;
	GameProgressionNodeDef *pOrigNodeDefUntouched;
} StoryArcEditDoc;

MEWindow *storyArcEditor_Init(MultiEditEMDoc *pEditorDoc) ;
void storyArcEditor_Close(StoryArcEditDoc *pDoc);

void storyArcEditor_CreateStoryArc(char *pcName);

#endif