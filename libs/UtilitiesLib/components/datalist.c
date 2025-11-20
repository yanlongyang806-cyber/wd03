#include "datalist.h"

#include "StringCache.h"
#include "endian.h"
#include "textparser.h"
#include "MemoryMonitor.h"
#include "StashTable.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem);); // Only used by hogs

typedef enum DataListJournalOp {
	DLJ_INVALID,
	DLJ_ADD_OR_UPDATE, // Must implicitly FREE
	DLJ_FREE,
} DataListJournalOp;

typedef struct DataListHeader {
	U32 version;
	U32 num_entries;
} DataListHeader;
ParseTable parseDataListHeader[] = {
	{"version",		TOK_INT(DataListHeader, version,0)},
	{"num_entries", TOK_INT(DataListHeader, num_entries,0)},
	{0,0}
};

void DataListCreate(DataList *handle)
{
	assert(handle);
	ZeroStructForce(handle);
}

void DataListDestroy(DataList *handle)
{
	int i;
	for (i=0; i<eaSize(&handle->data_list); i++) {
		if (!(handle->flags[i] & (DLF_DO_NOT_FREEME_STRINGPOOL|DLF_DO_NOT_FREEME_DATAPTR))) {
			SAFE_FREE(handle->data_list_not_const[i]);
		}
	}
	eaDestroy(&handle->data_list_not_const);
	eaiDestroy(&handle->size_list);
	eaiDestroy(&handle->free_list);
	eaiDestroy(&handle->flags);
	SAFE_FREE(handle->data_ptr);
	ZeroStructForce(handle);
}

// Adds to a specific place (used internally in journal recovery)
static S32 DataListAddSpecific(DataList *handle, S32 index, const void *data, U32 size, DataListJournal *journal) // returns the index of the newly added data, -1 on failure
{
	assert(size != 0);
	assert(data != 0);
	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	assert(!journal);
	if (index<0)
		return -1;
	while (index >= eaiSize(&handle->size_list)) {
		S32 newindex = eaiSize(&handle->size_list);
		eaiPush(&handle->size_list, 0); // Add new entry
		eaiPush(&handle->flags, 0); // Add new entry
		eaPush(&handle->data_list_not_const, NULL); // Add new entry
		eaiPush(&handle->free_list, newindex); // Add to FreeList
	}

	assert(handle->size_list && handle->flags);

	// Free existing data if there is any
	if (DataListGetData(handle, index, NULL)) {
		DataListFree(handle, index, NULL);
	}
	// Remove index from freelist
	eaiFindAndRemove(&handle->free_list, index);

	handle->size_list[index] = size;
	handle->data_list_not_const[index] = memdup(data, size);
	handle->flags[index] = 0;
	handle->total_data_size += size + sizeof(U32);

	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	return index;
}

S32 DataListAdd(DataList *handle, const void *data, U32 size, bool is_string, DataListJournal *journal) // returns the index of the newly added data, -1 on failure
{
	S32 index;
	const U8 *localdata;
	assert(!handle->read_only);
	assert(size != 0);
	assert(data != 0);
	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	if (is_string) {
		localdata = allocAddFilename(data);
	} else {
		// TODO: This is a run-time, persistent allocation, which is bad for fragmentation, could pre-allocate a chunk (rather costly for one allocation per hogg per process on developer machines), or have a globa persistent allocation heap or something like that
		U8 *temp = malloc(size);
		memcpy(temp, data, size);
		localdata = temp;
	}
	if (eaiSize(&handle->free_list)) {
		index = eaiPop(&handle->free_list);
	} else {
		index = eaiSize(&handle->size_list);
		eaiPush(&handle->size_list, 0); // Add new entry
		eaPush(&handle->data_list_not_const, NULL); // Add new entry
		eaiPush(&handle->flags, 0); // Add new entry
	}

	assert(handle->size_list && handle->flags);

	handle->size_list[index] = size;
	handle->data_list[index] = localdata;
	handle->flags[index] = is_string?DLF_DO_NOT_FREEME_STRINGPOOL:0;
	handle->total_data_size += size + sizeof(U32);
	if (journal) {
		U32 offs=journal->size;
		journal->size += sizeof(U8) + sizeof(S32) + sizeof(U32) + size;
		journal->data = realloc(journal->data, journal->size);
		*(U8*)(journal->data + offs) = DLJ_ADD_OR_UPDATE;
		offs += sizeof(U8);
		*(S32*)(journal->data + offs) = endianSwapIfBig(S32, index);
		offs += sizeof(S32);
		*(U32*)(journal->data + offs) = endianSwapIfBig(U32, size);
		offs += sizeof(U32);
		memcpy(journal->data + offs, data, size);
		offs += size;
		assert(journal->size == offs);
	}

	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	return index;
}

S32 DataListUpdate(DataList *handle, S32 oldid, const void *data, U32 size, bool is_string, DataListJournal *journal) // returns the index of the updated data, -1 on failure
{
	S32 newid;
	assert(!handle->read_only);
	assert(size != 0);
	assert(data != 0);
	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	if (DataListGetData(handle, oldid, NULL)) {
		// Existing data, free it!
		DataListFree(handle, oldid, NULL);
		newid = DataListAdd(handle, data, size, is_string, journal);
		assert(newid == oldid);
	} else {
		// Really just an Add
		newid = DataListAdd(handle, data, size, is_string, journal);
	}
	return newid;
}

void DataListFree(DataList *handle, S32 id, DataListJournal *journal)
{
	assert(!handle->read_only);
	if (id < 0 || id >= eaSize(&handle->data_list))
		return;
	if (handle->data_list[id]==NULL) {
		// Already freed
		return;
	}
	handle->total_data_size -= handle->size_list[id] + sizeof(U32);
	if (handle->flags[id] & (DLF_DO_NOT_FREEME_STRINGPOOL|DLF_DO_NOT_FREEME_DATAPTR)) {
		handle->data_list[id] = NULL;
	} else {
		SAFE_FREE(handle->data_list_not_const[id]);
	}
	handle->size_list[id] = 0;
	handle->flags[id] = 0;
	eaiPush(&handle->free_list, id);
	if (journal) {
		U32 offs=journal->size;
		journal->size += sizeof(U8) + sizeof(S32);
		journal->data = realloc(journal->data, journal->size);
		*(U8*)(journal->data + offs) = DLJ_FREE;
		offs += sizeof(U8);
		*(S32*)(journal->data + offs) = endianSwapIfBig(S32, id);
		offs += sizeof(S32);
		assert(journal->size == offs);
	}
}

// Note: taking stringbits into this function isn't a good idea - half of the journal entries are deletes
//  or things which are no longer valid, so it pollutes the string table with things which would otherwise
//  just get freed
int DataListApplyJournal(DataList *handle, DataListJournal *journal)
{
	U8 *cursor = journal->data;
	U32 size = journal->size;
	assert(!handle->read_only);
	while (size>0) {
#define GET(type) endianSwapIfBig(type, (*(type*)cursor)); cursor += sizeof(type); size -= sizeof(type); if (size<0) break;
		DataListJournalOp op;
		S32 id;
		U32 datasize;
		op = GET(U8);
		switch(op) {
		xcase DLJ_FREE:
			id = GET(S32);
			DataListFree(handle, id, NULL);
		xcase DLJ_ADD_OR_UPDATE:
			id = GET(S32);
			datasize = GET(U32);
			if (datasize > size)
				break;
			size -= datasize;
			DataListAddSpecific(handle, id, cursor, datasize, NULL);
			cursor += datasize;
		}
#undef GET
	}
	if (size != 0) {
		// Corrupt!
		return 1;
	}
	// Set capacities to exactly fit (again, it probably grew), most hoggs are opened read-only, and those that modify will have this
	// grow as needed
	eaSetCapacity(&handle->data_list, eaiSize(&handle->size_list));
	eaiSetCapacity(&handle->flags, eaiSize(&handle->size_list));
	eaiSetCapacity(&handle->size_list, eaiSize(&handle->size_list));
	return 0;
}



const void *DataListGetData(DataList *handle, S32 id, U32 *size)
{
	if (handle->data_dumped)
		return NULL;
	if (id < 0 || id >= eaSize(&handle->data_list))
		return NULL;
	if (size)
		*size = handle->size_list[id];
	return handle->data_list[id];
}

const char *DataListGetString(DataList *handle, S32 id, bool add_to_string_cache)
{
	if (id < 0 || id >= eaSize(&handle->data_list))
		return NULL;
	if (handle->data_dumped)
	{
		assert(handle->data_list[id]);
		return handle->data_list[id];
	}
	if (!(handle->flags[id] & DLF_DO_NOT_FREEME_STRINGPOOL) && add_to_string_cache)
	{
		// Add this string to the string table so that it is valid for the life of the program
		char *olddata = handle->data_list_not_const[id];
		if (!olddata)
			return NULL;
		handle->data_list[id] = allocAddFilename(olddata);
		if (!(handle->flags[id] & DLF_DO_NOT_FREEME_DATAPTR))
			free(olddata);
		handle->flags[id] |= DLF_DO_NOT_FREEME_STRINGPOOL;
		handle->flags[id] &= ~DLF_DO_NOT_FREEME_DATAPTR;
	}
	return handle->data_list[id];
}

S32 DataListGetIDFromString(DataList *handle, const char *s) // Slow!  Don't use!
{
	S32 i;
	const char *s2;
	for (i=eaSize(&handle->data_list)-1; i>=0; i--)
	{
		if (!(s2 = handle->data_list[i]))
			continue;
		if (stricmp(s2, s)==0)
			return i;
	}
	return -1;
}

U32 DataListGetNumEntries(DataList *handle)
{
	return eaSize(&handle->data_list) - eaiSize(&handle->free_list);
}

U32 DataListGetNumTotalEntries(DataList *handle)
{
	return eaSize(&handle->data_list);
}

U32 DataListGetDiskUsage(DataList *handle)
{
	return handle->total_data_size + sizeof(DataListHeader) + sizeof(U32)*eaiSize(&handle->free_list);
}

U32 DataListGetMemoryUsage(DataList *handle)
{
	U32 size = 0;
	int i;

	size += handle->size_list ? (U32)eaiMemUsage(&handle->size_list, true) : 0;
	size += handle->flags ? (U32)eaiMemUsage(&handle->flags, true) : 0;
	size += handle->free_list ? (U32)eaiMemUsage(&handle->free_list, true) : 0;
	size += handle->data_list ? (U32)eaMemUsage(&handle->data_list, true) : 0;

	if(!handle->data_list || !handle->size_list || !handle->flags)
	{
		return size;
	}

	for(i=0; i<eaSize(&handle->data_list); ++i)
	{
		if(!(handle->flags[i] & DLF_DO_NOT_FREEME_STRINGPOOL))
		{
			size += handle->size_list[i];
		}
	}

	return size;
}

U8 *DataListWrite(DataList *handle, U32 *filesize) // compatcs a data list, and returns the number of bytes and the data to be written
{
	U32 size = DataListGetDiskUsage(handle);
	U8 *data = malloc(size);
	U8 *cursor = data;
	DataListHeader header = {0};
	int count;
	int i;
	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));

	assert(!handle->read_only);

	*filesize = size;
	header.version = 0;
	header.num_entries = eaSize(&handle->data_list);
	memcpy(cursor, &header, sizeof(header));
	if (isBigEndian())
		endianSwapStruct(parseDataListHeader, cursor);
	cursor += sizeof(header);
	if (count = eaSize(&handle->data_list)) {
		assert( handle->size_list ); // To make /analyze happy
		for (i=0; i<count; i++) {
			U32 datasize = handle->size_list[i];
			if (isBigEndian())
				*(U32*)cursor = endianSwapU32(datasize);
			else
				*(U32*)cursor = datasize;
			cursor += sizeof(U32);
			if (datasize) {
				memcpy(cursor, handle->data_list[i], datasize);
				cursor += datasize;
				// Must not be in the free list
				//assert(eaiFind(&handle->free_list, i)==-1);
			} else {
				// Must be in the free list!
				//assert(eaiFind(&handle->free_list, i)!=-1);
			}
		}
	}
	assert(cursor == data + size);
	return data;
}

struct {
	int string_size;
	int data_size;
	int total_size;
	int string_count;
	int total_count;
	int data_count;
} datalist_stats;

#if 0
typedef struct
{
	char	ext[100];
	int		size;
} ExtSize;

static ExtSize ext_list[100];
static int 	ext_count;

void logExtSizes(char *fname,int size)
{
	char	*ext = strrchr(fname,'.');
	int		i;

	if (!ext)
		return;

	for(i=0;i<ext_count;i++)
	{
		if (stricmp(ext_list[i].ext,ext)==0)
			break;
	}
	if (i>= ext_count)
	{
		assert(ext_count < ARRAY_SIZE(ext_list));
		ext_count = i+1;
		strcpy(ext_list[i].ext,ext);
	}
	ext_list[i].size += size;
}
#endif

S32 DataListRead(DataList *handle, char *filedata, U32 filesize, U32 *stringbits, U32 stringbits_numbits) // reads a data list, and returns the number of bytes read, -1 on fail
{
	DataListHeader header = {0};
	U8 * cursor = filedata;
	U32 offset=0;
	U32 i;
	char tempStringBuf[1024];
	assert(eaSize(&handle->data_list) == 0);
	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	handle->total_data_size = 0;
#define SAFE_COPY(ptr, size) {					\
		if (offset + (U32)(size) > filesize) {	\
			DataListDestroy(handle);			\
			return -1;							\
		}										\
		memcpy((ptr), cursor, (size));			\
		cursor += size;							\
		offset += size;							\
	}	// End SAFE_COPY

	SAFE_COPY(&header, sizeof(header));
	if (isBigEndian())
		endianSwapStruct(parseDataListHeader, &header);

	if (header.version != 0)
		return -1;

	if (stringbits_numbits) {
		allocAddStringManualLock();
	}

	eaiSetCapacity(&handle->size_list, header.num_entries);
	eaiSetCapacity(&handle->flags, header.num_entries);
	eaSetCapacity(&handle->data_list_not_const, header.num_entries);

	for (i=0; i<header.num_entries; i++) {
		U32 datasize;
		U8 *data = NULL;
		SAFE_COPY(&datasize, sizeof(U32));
		if (isBigEndian())
			datasize = endianSwapU32(datasize);
		eaiPush(&handle->size_list, datasize);
		eaiPush(&handle->flags, 0);
		datalist_stats.total_count++;
		if (datasize) {
			datalist_stats.total_size+=datasize;
			if (i < stringbits_numbits && datasize < ARRAY_SIZE(tempStringBuf) && TSTB(stringbits, i)) {
				datalist_stats.string_count++;
				datalist_stats.string_size+=datasize;
				SAFE_COPY(tempStringBuf, datasize);
				data = (char*)allocAddFilenameWhileLocked(tempStringBuf);
				handle->flags[i] = DLF_DO_NOT_FREEME_STRINGPOOL;
			} else {
				datalist_stats.data_count++;
				datalist_stats.data_size+=datasize;
				data = malloc(datasize);
				SAFE_COPY(data, datasize);
				//logExtSizes(tempStringBuf,datasize);
			}
			handle->total_data_size += sizeof(U32) + datasize;
		} else {
			// Free spot
			eaiPush(&handle->free_list, i);
		}
		eaPush(&handle->data_list_not_const, data);
	}
#undef SAFE_COPY

	if (stringbits_numbits) {
		allocAddStringManualUnlock();
	}

	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	return filesize;
}


#define SAFE_COPY(ptr, size) {					\
		if (offset + (U32)(size) > filesize) {	\
			DataListDestroy(handle);			\
			return -1;							\
		}										\
		memcpy((ptr), cursor, (size));			\
		cursor += size;							\
		offset += size;							\
	}	// End SAFE_COPY

S32 DataListReadWithJournal(DataList *handle, char *filedata, U32 filesize, U32 *stringbits, U32 stringbits_numbits, DataListJournal *journal, PigErrLevel error_level)
{
	DataListHeader header = {0};
	U8 *cursor = filedata;
	U32 offset=0;
	U8 *out_cursor;
	U32 i;
	U32 data_ptr_size;
	U8 *cursor_saved;
	int offset_saved;
	char tempStringBuf[1024] = {0};
	int total_entries;
	StashTable stIDsToChange; // value of 1 means remove from freelist, value of 2 means add to it
	assert(eaSize(&handle->data_list) == 0);
	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));

	SAFE_COPY(&header, sizeof(header));
	if (isBigEndian())
		endianSwapStruct(parseDataListHeader, &header);

	if (header.version != 0)
		return -1;

	if (stringbits_numbits) {
		allocAddStringManualLock();
	}

	total_entries = header.num_entries;

	// Parse journal looking for largest index used
	{
		int num_updates=0;
		U8 *journal_cursor = journal->data;
		U32 journal_size = journal->size;
		while (journal_size>0) {
#define GET(type) endianSwapIfBig(type, (*(type*)journal_cursor)); journal_cursor += sizeof(type); journal_size -= sizeof(type); if (journal_size<0) break;
			DataListJournalOp op;
			S32 id;
			U32 datasize;
			op = GET(U8);
			switch(op)
			{
			xcase DLJ_FREE:
				id = GET(S32);
			xcase DLJ_ADD_OR_UPDATE:
				id = GET(S32);
				datasize = GET(U32);
				if (datasize > journal_size)
					break;
				MAX1(total_entries, id+1);
				journal_size -= datasize;
				journal_cursor += datasize;
			}
			num_updates++;
#undef GET
		}
		stIDsToChange = stashTableCreateInt(MIN(num_updates, total_entries));
	}

	eaiSetCapacity(&handle->size_list, total_entries);
	eaiSetCapacity(&handle->flags, total_entries);
	eaSetCapacity(&handle->data_list_not_const, total_entries);

	// Save data cursor
	cursor_saved = cursor;
	offset_saved = offset;
	// Calculate data size and allocate
	for (i=0; i<header.num_entries; i++) {
		U32 datasize;
		U8 *data = NULL;
		SAFE_COPY(&datasize, sizeof(U32));
		if (isBigEndian())
			datasize = endianSwapU32(datasize);
		eaiPush(&handle->size_list, datasize);
		eaiPush(&handle->flags, 0);
		datalist_stats.total_count++;
		if (datasize) {
			datalist_stats.total_size+=datasize;
			data = cursor;
			offset += datasize;
			cursor += datasize;
		} else {
			// Free spot
			eaiPush(&handle->free_list, i);
		}
		eaPush(&handle->data_list_not_const, data);
	}
	// Parse journal
	{
		U8 *journal_cursor = journal->data;
		U32 journal_size = journal->size;
		while (journal_size>0) {
#define GET(type) endianSwapIfBig(type, (*(type*)journal_cursor)); journal_cursor += sizeof(type); journal_size -= sizeof(type); if (journal_size<0) break;
			DataListJournalOp op;
			S32 id;
			U32 datasize;
			op = GET(U8);
			switch(op)
			{
			xcase DLJ_FREE:
				id = GET(S32);
				if (id < 0 || id >= eaSize(&handle->data_list))
					break;
				if (handle->data_list[id]==NULL) // Already freed
					break;
				handle->data_list[id] = NULL;
				handle->size_list[id] = 0;
				handle->flags[id] = 0;
				stashIntAddInt(stIDsToChange, id+1, 2, true);
			xcase DLJ_ADD_OR_UPDATE:
				id = GET(S32);
				datasize = GET(U32);
				if (datasize > journal_size)
					break;
				journal_size -= datasize;
				
				assert(datasize != 0);
				if (id<0)
					break;
				while (id >= eaiSize(&handle->size_list)) {
					S32 newindex = eaiSize(&handle->size_list);
					eaiPush(&handle->size_list, 0); // Add new entry
					eaiPush(&handle->flags, 0); // Add new entry
					eaPush(&handle->data_list_not_const, NULL); // Add new entry
					eaiPush(&handle->free_list, newindex); // Add to FreeList
				}

				assert(handle->size_list && handle->flags);

				// Remove id from freelist
				stashIntAddInt(stIDsToChange, id+1, 1, true);

				handle->size_list[id] = datasize;
				handle->data_list[id] = journal_cursor;
				handle->flags[id] = 0; // Determine stringpool or data pointer later
				journal_cursor += datasize;
			}
#undef GET
		}

		// Update freelist
		{
			int j;
			for (j=eaiSize(&handle->free_list)-1; j>=0; j--)
			{
				int op;
				if (stashIntRemoveInt(stIDsToChange, handle->free_list[j]+1, &op))
				{
					if (op == 1) { // remove
						eaiRemoveFast(&handle->free_list, j);
					} else if (op == 2) { // add
						// already in the list, just remove it from stashtable
					} else {
						assert(0);
					}
				}
			}
			// Any adds left in the stashtable need to be applied, any removes left in the stashtable can be ignored
			FOR_EACH_IN_STASHTABLE2(stIDsToChange, elem)
			{
				int id = stashElementGetIntKey(elem) - 1;
				int op = stashElementGetInt(elem);
				if (op == 2) {
					eaiPush(&handle->free_list, id);
				}
			}
			FOR_EACH_END;
		}

		stashTableDestroy(stIDsToChange);

		if (journal_size != 0) {
			// Corrupt!
			if (stringbits_numbits) {
				allocAddStringManualUnlock();
			}
			return -1;
		}
	}

	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));

	// Calculate final size
	data_ptr_size = 0;
	for (i=0; i<eaiUSize(&handle->size_list); i++)
	{

		if (handle->size_list[i])
		{
			bool bBadString=false;
			if (i < stringbits_numbits && TSTB(stringbits, i) &&
				(handle->size_list[i] > 1024 || handle->data_list[i][handle->size_list[i]-1] != '\0'))
			{
				// Bad string!
				if (error_level == PIGERR_ASSERT)
					ignorableAssertmsg(0, "Bad string in data list");
				else if (error_level == PIGERR_PRINTF)
					printf("Bad string in data list (%d)\n", i);
				bBadString = true;
			}
			if (i < stringbits_numbits && TSTB(stringbits, i) && !bBadString)
			{
				datalist_stats.string_count++;
				datalist_stats.string_size+=handle->size_list[i];
				handle->data_list[i] = allocAddFilenameWhileLocked(handle->data_list[i]);
				handle->flags[i] = DLF_DO_NOT_FREEME_STRINGPOOL;
			} else {
				datalist_stats.data_count++;
				datalist_stats.data_size+=handle->size_list[i];
				handle->flags[i] = DLF_DO_NOT_FREEME_DATAPTR;
				data_ptr_size += handle->size_list[i];
			}
		}
	}
	// Allocate giant chunk
	handle->data_ptr = malloc(data_ptr_size);
	out_cursor = handle->data_ptr;
	// Restore data cursors
	cursor = cursor_saved;
	offset = offset_saved;
	// Load data
	assert(header.num_entries <= eaUSize(&handle->data_list));
	handle->total_data_size = 0;
	for (i=0; i<eaiUSize(&handle->size_list); i++)
	{
		U32 datasize = handle->size_list[i];
		if (datasize) {
			if (!(handle->flags[i] & DLF_DO_NOT_FREEME_STRINGPOOL))
			{
				assert(handle->flags[i] & DLF_DO_NOT_FREEME_DATAPTR);
				memcpy(out_cursor, handle->data_list[i], datasize);
				handle->data_list[i] = out_cursor;
				out_cursor += datasize;
			}
			handle->total_data_size += sizeof(U32) + datasize;
		}
	}
	assert(out_cursor == (U8*)handle->data_ptr + data_ptr_size);

#undef SAFE_COPY

	if (stringbits_numbits) {
		allocAddStringManualUnlock();
	}

	assert(eaiSize(&handle->size_list) == eaSize(&handle->data_list) &&
		eaiSize(&handle->size_list) == eaiSize(&handle->flags));
	return filesize;
}


void DataListDumpNonStringData(DataList *handle) // Also makes it read-only, as it cannot recover/write after this
{
	int i;
	assert(!handle->data_dumped);
	for (i=0; i<eaiSize(&handle->size_list); i++)
	{
		if (handle->flags[i] & DLF_DO_NOT_FREEME_STRINGPOOL)
		{
			// Leave it alone
		} else if (handle->flags[i] & DLF_DO_NOT_FREEME_DATAPTR) {
			handle->data_list[i] = NULL;
		} else {
			SAFE_FREE(handle->data_list[i]);
		}
	}
	SAFE_FREE(handle->data_ptr);

	eaiDestroy(&handle->flags);
	eaiDestroy(&handle->size_list);
	handle->read_only = true;
	handle->data_dumped = true;
}
