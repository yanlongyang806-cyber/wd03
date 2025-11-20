#pragma once
GCC_SYSTEM
//
// ItemPowerEditor.h
//


#ifndef NO_EDITORS

typedef struct MEWindow MEWindow;
typedef struct MultiEditEMDoc MultiEditEMDoc;


typedef struct newItemPowerData 
{
	char* pchName;
	const char* pScope;
	PowerDef* pPowerDef;
} newItemPowerData;


// Starts up the item power editor and displays the main window
MEWindow *itemPowerEditor_init(MultiEditEMDoc *pEditorDoc);

// Creates an item power for editing
void itemPowerEditor_createItemPower(char *pcName);
void itemPowerEditor_createItemPowerFromPowerDef(char *pcName, PowerDef* pDef, const char* pScope);

// EM wrapper, didn't want to create a new .h file for one measly function.
void itemPowerEditorEMNewDocFromPowerDef(const char *pcType, PowerDef* pPowerDef, char* pName, const char* pScope);


#endif