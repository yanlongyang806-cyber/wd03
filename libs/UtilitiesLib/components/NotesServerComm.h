#pragma once
typedef struct StashTableImp*			StashTable;
typedef struct SingleNote SingleNote;





void NotesServer_InitSystemAndConnect(char *pDomainName, char *pSystemName);

void NotesServer_Tick(void);

//returns true if we've ever connected to the note server, doesn't matter if we're currently connected
bool NotesServer_Connected(void);

//returns an html link to the appropriate notes page (note that when called on a server in a shard, this returns 
//a link to a page on the controller

//Usually if you want to use this on a server, do AST(FORMATSTRING(HTML_NOTENAME=1)), and then put
//"notename, title, 1/0, 1/0" in the string, because only the controller and the mcp actually have access to the notes information
char *NotesServer_GetLinkToNoteServerMonPage(char *pNoteName, char *pDescriptiveTitle, bool bPopLeft, bool bDesigner);

//returns true if the note exists and has at least one comment set
bool NotesServer_NoteIsSet(char *pNoteName);


//the controller has to always send all local notes down to the monitoring MCP
LATELINK;
void SingleNoteWasUpdated(SingleNote *pSingleNote);


void NotesServer_SetSingleNote(SingleNote *pSingleNote);

LATELINK;
char *GetNotesSystemName(void);

extern StashTable gSingleNotesByName;

//returns NULL if everything is connected nicely, a descriptive string of some sort otherwise (so getting back a string
//always means there's a problem)
char *NotesServer_GetConnectionStatusString(void);