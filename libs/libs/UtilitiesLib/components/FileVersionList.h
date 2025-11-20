#pragma once
GCC_SYSTEM

// Utilities for reading and writing file version lists, which are identical in format to
// the manifest files used by the patchserver/client

AUTO_STRUCT;
typedef struct SavedFileVersion
{
	char			*filename; AST(KEY)
	U32				modified;
	U32				size;
	U32				checksum;
	U32				header_size;
	U32				header_checksum;
	U32				flags;
} SavedFileVersion;

AUTO_STRUCT;
typedef struct SavedFileVersionList
{
	SavedFileVersion ** versions;
} SavedFileVersionList;

// Create a new list
SavedFileVersionList *CreateFileVersionList(void);

// Destroys a list
void DestroyFileVersionList(SavedFileVersionList *list);

// Returns a file version, if it's in the list
SavedFileVersion *GetFileVersionFromList(SavedFileVersionList *list, const char *filename);

// Append (or replace) a filename to the list
SavedFileVersion *AddFileVersionToList(SavedFileVersionList *list, const char *filename, U32 modified, U32 size, U32 checksum, U32 header_size, U32 header_checksum, U32 flags);

// Removes a file version from the list
void RemoveFileVersionFromList(SavedFileVersionList *list, const char *filename);

// Appends to an existing list from disk
bool ReadFileVersionList(SavedFileVersionList *list, const char *filename);

// Writes a list to disk
bool WriteFileVersionList(SavedFileVersionList *list, const char *filename);


