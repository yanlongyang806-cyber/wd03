
#include "timing.h"

#include "groupdbmodify.h"
#include "groupjournal.h"
#include "WorldGridPrivate.h"

#include "groupjournal_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// This structure encapsulates all of the necessary data to define a GroupDef backup.
AUTO_STRUCT;
typedef struct GroupDefBak
{
	GroupDef *pre_copy;
	GroupDef *post_copy;
	GroupDef *group_orig;				NO_AST
	bool is_undo;						// Toggle between UNDO and REDO
} GroupDefBak;

// This is the main journal entry struct, which contains an array of GroupDef backups.  Undo
// and redo operations are done on an entry-to-entry basis, allowing multiple GroupDefs to
// be restored simultaneously (for transactions involving more than one entity).
AUTO_STRUCT;
typedef struct JournalEntry
{
	GroupDefBak ** defs;
} JournalEntry;

//JournalEntry ** journal;		// the journal, which consists of an array of entries
JournalEntry *workingEntry;		// the entry "in progress", not yet saved to the journal
GroupDef **tempDefArray;		// stores all of the temporary definitions we've created
int journalAddingEntry;			// true after journalBeginEntry, false after journalEndEntry

JournalEntry * journalEntryCreate()
{
	return StructCreate(parse_JournalEntry);
}

/*
 * This function frees the memory used by a journal entry; it also iterates through
 * the entry's backup GroupDefs and frees their memory as well.
 * PARAMS:
 *   je - the JournalEntry to free
 */
void journalEntryDestroy(JournalEntry * je)
{
	StructDestroy(parse_JournalEntry, je);
}

/*
 * This function creates a new JournalEntry and initializes the state variables as appropriate.
 */
void journalBeginEntry(void)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	assert(!journalAddingEntry);
	workingEntry = journalEntryCreate();
	journalAddingEntry = 1;
	PERFINFO_AUTO_STOP();
}

/*
 * This function creates and returns a backup from a group definition.
 * PARAMS:
 *   def - the GroupDef to backup
 * RETURNS:
 *   a GroupDefBak backup of the passed in GroupDef
 */
GroupDefBak *groupDefBakCreate(GroupDef *def)
{
	GroupDefBak *bak = StructCreate(parse_GroupDefBak);
	groupFixupBeforeWrite(def, false);
	bak->pre_copy = StructClone(parse_GroupDef, def);
	bak->group_orig = def;
	return bak;
}

void groupDefBakFinish(GroupDefBak *bak)
{
	if (groupLibFindGroupDef(bak->group_orig->def_lib, bak->group_orig->name_uid, false) != NULL)
	{
		groupFixupBeforeWrite(bak->group_orig, true);
		bak->post_copy = StructClone(parse_GroupDef, bak->group_orig);
	}
	else
	{
		bak->post_copy = StructCreate(parse_GroupDef); // "Deleted"
	}

	bak->is_undo = true;
}

/*
 * This is the workhorse function for adding a new definition to the working JournalEntry.
 * This function creates a backup of the specified GroupDef and stores the backup in the
 * specified JournalEntry.
 * PARAMS:
 *   def - the GroupDef to backup
 *   je - the JournalEntry to which the backup will be written
 */
void journalDefInEntry(GroupDef * def, JournalEntry * je)
{
	GroupDefBak *defCopy;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	defCopy = groupDefBakCreate(def);

	// store the backup
	eaPush(&je->defs, defCopy);
	PERFINFO_AUTO_STOP();
}

/*
 * This is the public journalling function, which will journal the specified definition in
 * the working JournalEntry.
 * PARAMS:
 *   def - the GroupDef to journal
 */
void journalDef(GroupDef * def)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	if (journalAddingEntry)
	{
		journalDefInEntry(def, workingEntry);
	}
	PERFINFO_AUTO_STOP();
}

/*
 * This function takes care of writing the working journal entry to the journal.  It also
 * maintains the entry stack by removing any entries after the location where the working
 * entry is written.
 */
JournalEntry *journalEndEntry(void)
{
	int i;
	JournalEntry *ret = NULL;
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	if (!journalAddingEntry)
	{
		return NULL;
	}

	for (i = 0; i < eaSize(&workingEntry->defs); i++)
	{
		groupDefBakFinish(workingEntry->defs[i]);
	}

	// now we can add in the new journal entry
	if (eaSize(&workingEntry->defs) > 0)
	{
		ret = workingEntry;
	}
	else
	{
		free(workingEntry);
	}
	journalAddingEntry = 0;
	objectLibraryConsistencyCheck(true);
	PERFINFO_AUTO_STOP();
	return ret;
}

/******
* This function returns a restored group definition given its backup.
* PARAMS:
*   bak - the GroupDefBak to restore
* RETURNS:
*   the restored GroupDef
******/
GroupDef *groupDefBakRestore(GroupDefBak *bak)
{
	GroupDef *src_def = NULL;

	if (bak->is_undo)
		src_def = bak->pre_copy;
	else
		src_def = bak->post_copy;

	assert(bak->group_orig->def_lib != objectLibraryGetDefLib());

	if (bak->group_orig->name_uid != 0)
	{
		// remove group_orig pointer from various places
		stashIntRemovePointer(bak->group_orig->def_lib->defs, bak->group_orig->name_uid, NULL);
		if (bak->group_orig->name_str && bak->group_orig->root_id == 0)
			stashRemoveInt(bak->group_orig->def_lib->def_name_table, bak->group_orig->name_str, NULL);
	}

	// Copy data
	StructCopy(parse_GroupDef, src_def, bak->group_orig, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, 0);

	if (bak->group_orig->name_uid != 0)
	{
		if (bak->group_orig->def_lib->editing_lib)
		{
			devassert(strStartsWith(bak->group_orig->filename, "object_library"));
		}

		// re-add group_orig pointer to necessary locations
		assert(stashIntAddPointer(bak->group_orig->def_lib->defs, bak->group_orig->name_uid, bak->group_orig, false));
		if (bak->group_orig->name_str && bak->group_orig->root_id == 0)
			stashAddInt(bak->group_orig->def_lib->def_name_table, bak->group_orig->name_str, bak->group_orig->name_uid, false);
	}

	if (bak->group_orig->filename)
		groupDefFixupMessages(bak->group_orig);

	bak->is_undo = !bak->is_undo;
	groupFixupAfterRead(bak->group_orig, false);

	if (bak->group_orig->name_uid == 0)
		return NULL; // Deleted

	return bak->group_orig;
}

/*
 * This function restores the specified backup GroupDef.
 * PARAMS:
 *   bak - the GroupDefBak to restore
 */
GroupDef *journalReplaceDef(GroupDefBak *bak)
{
	GroupDef *restoredDef;
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	restoredDef = groupDefBakRestore(bak);
	if (restoredDef)
	{
		groupdbDirtyDef(restoredDef, UPDATE_GROUP_PROPERTIES);
	}
	PERFINFO_AUTO_STOP();
	return restoredDef;
}

/*
 * This function restores the last journal entry.
 */
void journalRestoreEntry(JournalEntry * je)
{
	ZoneMap *zmap = worldGetPrimaryMap();
	GroupDefLib **libs_array = NULL;
	int i;

	PERFINFO_AUTO_START("journalRestoreEntry", 1);

	// increment the mod time of the world
	groupDefModify(NULL,UPDATE_GROUP_PROPERTIES,true);

	// process them in the reverse order we received them, like it was a stack;
	// just in case the order ever matters this should handle it correctly
	for (i = eaSize(&je->defs) - 1; i >= 0; i--)
	{
		GroupDef *restoredDef = journalReplaceDef(je->defs[i]);
		if (restoredDef)
			eaPushUnique(&libs_array, restoredDef->def_lib);
	}
	for (i = 0; i < eaSize(&libs_array); i++)
	{
		groupdbUpdateBounds(libs_array[i]);
		groupLibMarkBadChildren(libs_array[i]);
		groupLibConsistencyCheckAll(libs_array[i], true);
	}
	eaDestroy(&libs_array);

	// finally reverse the entries in the new current entry (a redo must be the reverse of an undo)
	eaReverse(&je->defs);
	zmapTrackerUpdate(NULL, false, false);

	PERFINFO_AUTO_STOP();
}

/*
 * This is the public function to call to undo the last journal entry.
 */
void journalUndo(JournalEntry * je)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	assert(je);
	assert(!journalAddingEntry);
	journalRestoreEntry(je);
	PERFINFO_AUTO_STOP();
}

#include "groupjournal_c_ast.c"
