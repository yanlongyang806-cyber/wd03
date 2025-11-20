/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

// Note: Attempting to not include "contacteditor.h" to avoid recompile problems
#include "EditorManager.h"


//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

typedef struct ContactEditDoc ContactEditDoc;

void CERevertContact(ContactEditDoc *pDoc);


//---------------------------------------------------------------------------------------------------
// Commands
//---------------------------------------------------------------------------------------------------

#endif

AUTO_COMMAND ACMD_NAME("CE_RevertContact");
void CmdRevertContact(void) 
{
#ifndef NO_EDITORS
	ContactEditDoc *pDoc = (ContactEditDoc*)emGetActiveEditorDoc();

	CERevertContact(pDoc);
#endif
}


