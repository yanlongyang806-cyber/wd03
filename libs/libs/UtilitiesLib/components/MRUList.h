#ifndef MRULIST_H
#define MRULIST_H
#pragma once
GCC_SYSTEM

typedef struct MRUList {
	char **values;
	int count;
	// Internal:
	int maxStringSize;
	char *name;
	int maxCount;
} MRUList;

MRUList *createMRUList_dbg(const char *name, int maxEntries, int maxStringSize, const char *caller_fname, int line);
#define createMRUList(name, maxEntries, maxStringSize) createMRUList_dbg(name, maxEntries, maxStringSize, __FILE__, __LINE__)
void destroyMRUList(MRUList *mru);

void mruUpdate(MRUList *mru); // Requeries the registry for latest MRU info (in case another process changes the values)  This is done automatically on creation.
void mruAddToList(MRUList *mru, const char *newEntry); // Adds a new element to the list or moves an existing element to the top

#endif