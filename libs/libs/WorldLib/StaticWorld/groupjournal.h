#ifndef _GROUPJOURNAL_H
#define _GROUPJOURNAL_H
GCC_SYSTEM

typedef struct GroupTracker GroupTracker;
typedef struct GroupDef GroupDef;
typedef struct GroupDefBak GroupDefBak;
typedef struct JournalEntry JournalEntry;

void journalBeginEntry();				// call this when you are about to start journaling things
void journalDef(GroupDef * def);		// call this before you modify a GroupDef
JournalEntry* journalEndEntry();		// call this when you are done journaling
void journalEntryDestroy(JournalEntry * je);

void journalUndo(JournalEntry *entry);	// undoes or redoes one journal entry, atomically

GroupDefBak *groupDefBakCreate(GroupDef *def);
GroupDef *groupDefBakGetDef(GroupDefBak *bak);
int groupDefBakGetOldUID(GroupDefBak *bak);
GroupDef *groupDefBakRestore(GroupDefBak *bak);
void groupDefBakFree(GroupDefBak *gdbak);

#endif
