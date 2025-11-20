#include "MRUList.h"
#include <string.h>
#include <stdio.h>
#include "utils.h"
#include "Prefs.h"

#define MAX_MRU_STRING 1024

MRUList *createMRUList_dbg(const char *name, int maxEntries, int maxStringSize, const char *caller_fname, int line)
{
	int i;
	MRUList *mru;
	mru = scalloc(sizeof(MRUList),1);
	mru->maxStringSize = MIN(MAX_MRU_STRING, maxStringSize);
	mru->count = 0;
	mru->maxCount = maxEntries;
	mru->name = strdup_dbg(name, caller_fname, line);
	mru->values = scalloc(sizeof(char*)*maxEntries, 1);
	for (i=0; i<mru->maxCount; i++) {
		mru->values[i] = scalloc(mru->maxStringSize,1);
	}
	mruUpdate(mru);
	return mru;
}

void destroyMRUList(MRUList *mru)
{
	int i;
	SAFE_FREE(mru->name);
	for (i=0; i<mru->maxCount; i++) {
		SAFE_FREE(mru->values[i]);
	}
	SAFE_FREE(mru->values);
	SAFE_FREE(mru);
}

void mruUpdate(MRUList *mru)
{
	char key[256];
	char *badvalue="BadValue";
	const char *value;
	int i;
	mru->count = 0;
	for(i=0; i < mru->maxCount; i++) {
		sprintf(key, "%s%d", mru->name, i);
		value = GamePrefGetString(key, badvalue);
		if (strcmp(value, badvalue) != 0)
			strncpyt(mru->values[mru->count++], value, mru->maxStringSize);
	}
}

void mruSaveList(MRUList *mru)
{
	char key[256];
	int i;
	for (i=0; i<mru->count; i++) {
		sprintf(key, "%s%d", mru->name, i);
		GamePrefStoreString(key, mru->values[i]);
	}
}

void mruAddToList(MRUList *mru, const char *_newEntry)
{
	int i;
	char key[256];
	char *newEntry = _alloca(mru->maxStringSize);
	strncpy_s(newEntry, mru->maxStringSize, _newEntry, _TRUNCATE);

	mruUpdate(mru);

	for (i=0; i<mru->count; i++) {
		if (stricmp(mru->values[i], newEntry)==0) {
			// Move to end
			for (; i<mru->count-1; i++) {
				strcpy_s(mru->values[i], mru->maxStringSize, mru->values[i+1]);
			}
			strcpy_s(mru->values[mru->count-1], mru->maxStringSize, newEntry);
			mruSaveList(mru);
			return;
		}
	}

	if(mru->count >= mru->maxCount) {
		// Delete oldest
		for (i=1; i<mru->maxCount; i++) 
			strcpy_s(mru->values[i-1], mru->maxStringSize, mru->values[i]);
		mru->count--;
		mruSaveList(mru);
	}

	sprintf(key, "%s%d", mru->name, mru->count);
	GamePrefStoreString(key, newEntry);

	strcpy_s(mru->values[mru->count++], mru->maxStringSize, newEntry);
}
