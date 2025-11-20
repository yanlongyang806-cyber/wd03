#pragma once
GCC_SYSTEM
//
// RewardTableEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;

typedef struct rewardTableEditor_InitData
{
	char *TableName;
	char *scope;

}rewardTableEditor_InitData;


// Starts up the reward table editor and displays the main window
MEWindow *rewardTableEditor_init(MultiEditEMDoc *pEditorDoc);

// Create a reward table for editing
void rewardTableEditor_createRewardTable(rewardTableEditor_InitData *pInitData);

#endif