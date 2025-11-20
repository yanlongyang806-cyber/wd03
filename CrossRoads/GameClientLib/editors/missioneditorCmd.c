/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS 

// Note: Attempting to not include "missioneditor.h" to avoid recompile problems
#include "EditorManager.h"


//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

typedef struct MissionDef MissionDef;
typedef struct MissionEditDoc MissionEditDoc;

void MDECloneSelectedMission(MissionEditDoc *pDoc);
void MDECopySelectedMission(MissionEditDoc *pDoc);
MissionDef* MDECreateSubMission(MissionEditDoc* pDoc, MissionDef *pMissionToClone);
void MDEDeleteSelectedMission(MissionEditDoc *pDoc);
void MDEDeleteUnusedSubMissions(MissionEditDoc *pDoc);
void MDEPasteMission(MissionEditDoc *pDoc);
void MDESelectedRemoveTemplate(MissionEditDoc *pDoc);
void MDERevertMission(MissionEditDoc *pDoc);


//---------------------------------------------------------------------------------------------------
// Commands
//---------------------------------------------------------------------------------------------------

#endif
AUTO_COMMAND ACMD_NAME("MDE_CloneMission");
void CmdCloneMission(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDECloneSelectedMission(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MDE_CopyMission");
void CmdCopyMission(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDECopySelectedMission(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MDE_CreateMission");
void CmdCreateSubMission(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDECreateSubMission(pDoc, NULL);
#endif
}

AUTO_COMMAND ACMD_NAME("MDE_DeleteMission");
void CmdDeleteMission(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDEDeleteSelectedMission(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MDE_DeleteUnused");
void CmdDeleteUnusedSubMissions(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDEDeleteUnusedSubMissions(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MDE_PasteMission");
void CmdPasteMission(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDEPasteMission(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MDE_RemoveTemplate");
void CmdMissionRemoveTemplate(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDESelectedRemoveTemplate(pDoc);
#endif
}

AUTO_COMMAND ACMD_NAME("MDE_RevertMission");
void CmdRevertMission(void) 
{
#ifndef NO_EDITORS
	MissionEditDoc *pDoc = (MissionEditDoc*)emGetActiveEditorDoc();

	MDERevertMission(pDoc);
#endif
}

