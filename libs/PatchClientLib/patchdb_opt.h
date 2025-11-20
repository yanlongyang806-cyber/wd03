// Performance-sensitive internal routines of PatchDB that require optimization.

#ifndef CRYPTIC_PATCHDB_OPT_H
#define CRYPTIC_PATCHDB_OPT_H

typedef void (*ForEachDirEntryCallback)(DirEntry*,void*);
void forEachDirEntry(DirEntry *dir_entry, ForEachDirEntryCallback callback, void *userdata);
void forEachDirEntryReverse(DirEntry *dir_entry, ForEachDirEntryCallback callback, void *userdata);

#endif  // CRYPTIC_PATCHDB_OPT_H
