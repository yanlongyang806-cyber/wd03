
#pragma once
GCC_SYSTEM

/*
	Interface for maintaining a file spec to data mapping.
	Example usage: gimme mapping what hogg files store which files
		Utilities\GimmeDLL\gimme.c
*/

typedef struct FilespecMap FilespecMap;

FilespecMap *filespecMapCreate(void);
FilespecMap *filespecMapCreateNoSorting(void);
void filespecMapDestroy(FilespecMap *handle);

// Create a new FilespecMap from an existing one, flattening it by removing all value information, retaining only information about what is included.
// This allows optimizing filespecMap operations where we only care about inclusion, not the actual value, since overlapping operations can be reduced.
FilespecMap *filespecMapFlatten(const FilespecMap *f, int value);

void filespecMapAdd(FilespecMap *handle, const char *spec, void *data);
bool filespecMapGet(FilespecMap *handle, const char *filename, void **result);
bool filespecMapCheckFlat(FilespecMap *handle, const char *filename);  // Check for a match, without getting the actual value
size_t filespecMapGetCount(FilespecMap *handle, const char *filename);
const char *filespecMapGetMatchFilespec(FilespecMap *handle, const char *filename);
bool filespecMapGetExact(FilespecMap *handle, const char *spec, void **result);

void filespecMapAddInt(FilespecMap *handle, const char *spec, int data);
bool filespecMapGetInt(FilespecMap *handle, const char *filename, int *result);
bool filespecMapGetExactInt(FilespecMap *handle, const char *spec, int *result);

int filespecMapGetNumElements(FilespecMap *handle);

// Simple filespec list interface
typedef struct SimpleFileSpecEntry
{
	bool doInclude;
	const char *filespec;
} SimpleFileSpecEntry;

extern ParseTable parse_filespec_include[];
#define TYPE_parse_filespec_include filespec_include
extern ParseTable parse_filespec_exclude[];
#define TYPE_parse_filespec_exclude filespec_exclude


typedef struct SimpleFileSpec
{
	SimpleFileSpecEntry **entries;
} SimpleFileSpec;

// Strips the prefix from all entries and removes entries which do not start with the prefix or a wildcard
SimpleFileSpec *simpleFileSpecLoad(const char *path, const char *prefixToStrip);
bool simpleFileSpecIncludesFile(const char *path, SimpleFileSpec *filespec);
bool simpleFileSpecExcludesFile(const char *path, SimpleFileSpec *filespec);
void simpleFileSpecDestroy(SimpleFileSpec *filespec);


// FileSpec tree for efficient walking in parallel with walking a tree
typedef struct FileSpecTree FileSpecTree;
struct FileSpecTree
{
	const char *filespec;
	FileSpecTree **folders;
	SimpleFileSpecEntry **entries;
};

FileSpecTree *fileSpecTreeLoad(const char *path);

#define FST_NOT_SPECIFIED (FileSpecTree*)0
#define FST_EXCLUDED (FileSpecTree*)1
#define FST_INCLUDED (FileSpecTree*)2
FileSpecTree *fileSpecTreeGetAction(const FileSpecTree *fstree, const char *path); // path should not have any '/'s in it.
