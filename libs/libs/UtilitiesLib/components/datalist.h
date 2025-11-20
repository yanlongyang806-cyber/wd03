#pragma once
GCC_SYSTEM

/*
 *	Sparse list of data elements and sizes, maintains index/ordering of elements.
 *  Used by the Hog system for managing/adding/removing file names/header data chunks
 */

#include "hoglib.h"

typedef enum DataListFlags {
	DLF_DO_NOT_FREEME_STRINGPOOL = 1<<0, // This pointer is pointing into the stringtable
	DLF_DO_NOT_FREEME_DATAPTR = 1<<1, // This pointer is pointing into data_ptr
} DataListFlags;

typedef const U8 *constU8ptr;

typedef struct DataList {
	U32 total_data_size; // Does not include freelist size
	union {
		constU8ptr *data_list; // EArrray, 
		U8 **data_list_not_const; // Same thing, different type
	};
	U32 *size_list; // EArrray, Size == 0 means not in use
	U32 *flags;		// EArrray, flag strings not to be freed
	U32 *free_list; // List of free entries
	void *data_ptr; // Giant chunk containing all data from initial read other than strings
	bool read_only;
	bool data_dumped;
} DataList;

typedef struct DataListJournal {
	U32 size;
	U8 *data;
} DataListJournal;

void DataListCreate(DataList *handle); // Creates an empty data pool
void DataListDestroy(DataList *handle);
S32 DataListRead(DataList *handle, char *filedata, U32 filesize, U32 *stringbits, U32 stringbits_numbits); // reads a data list, and returns the number of bytes read, -1 on fail

U32 DataListGetNumEntries(DataList *handle);
U32 DataListGetNumTotalEntries(DataList *handle);
U32 DataListGetDiskUsage(DataList *handle);
U32 DataListGetMemoryUsage(DataList *handle); // UNCOMMENTED BY VAS 2009-07-24

const char *DataListGetString(DataList *handle, S32 id, bool add_to_string_cache); // if add_to_string_cache, then returns a constant string pointer (allocAddString()'d)
const void *DataListGetData(DataList *handle, S32 id, U32 *size);
S32 DataListGetIDFromString(DataList *handle, const char *s); // Slow/debug

// Only used in creating/writing DataLists:
U8 *DataListWrite(DataList *handle, U32 *filesize); // compacts a data list, and returns the number of bytes and the data to be written
S32 DataListAdd(DataList *handle, const void *data, U32 size, bool is_string, DataListJournal *journal); // returns the index of the newly added data, -1 on failure
S32 DataListUpdate(DataList *handle, S32 oldid, const void *data, U32 size, bool is_string, DataListJournal *journal); // returns the index of the updated data, -1 on failure
void DataListFree(DataList *handle, S32 id, DataListJournal *journal);

int DataListApplyJournal(DataList *handle, DataListJournal *journal);

S32 DataListReadWithJournal(DataList *handle, char *filedata, U32 filesize, U32 *stringbits, U32 stringbits_numbits, DataListJournal *journal, PigErrLevel error_level); // reads a data list, and returns the number of bytes read, -1 on fail

void DataListDumpNonStringData(DataList *handle); // Also makes it read-only, as it cannot recover/write after this