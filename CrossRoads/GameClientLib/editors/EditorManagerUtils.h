#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef __EDITORMANAGERUTILS_H__
#define __EDITORMANAGERUTILS_H__

#ifndef NO_EDITORS

typedef struct EMFile EMFile;
typedef enum GimmeErrorValue GimmeErrorValue;

int emuGetBranchNumber(void);

// Open and OpenContaining operate on a single file
int emuOpenFile(const char *filename);
void emuOpenContainingDirectory(const char *filename);

// These others also operate on any linked files
int emuCheckoutFile(EMFile *file);
int emuCheckoutFileEx(EMFile *file, const char *name, bool show_dialog);
bool emuHandleGimmeMessage(const char *filename, GimmeErrorValue gimmeMessage, bool doing_checkout, bool show_dialog);

void emuCheckinFile(EMFile *file);
void emuCheckinFileEx(EMFile *file, const char *name, bool show_dialog);

void emuCheckpointFile(EMFile *file);
void emuCheckpointFileEx(EMFile *file, const char *name, bool show_dialog);

void emuUndoCheckout(EMFile *file);
void emuUndoCheckoutEx(EMFile *file, const char *name, bool show_dialog);

void emuGetLatest(EMFile *file);
void emuGetLatestEx(EMFile *file, const char *name, bool show_dialog);

void emuCheckRevisions(EMFile *file);
void emuCheckRevisionsEx(EMFile *file, const char *name, bool show_dialog);

#endif // NO_EDITORS

#endif // __EDITORMANAGERUTILS_H__
