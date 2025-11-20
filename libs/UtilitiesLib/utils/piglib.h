/***************************************************************************



***************************************************************************/

#ifndef _PIGLIB_H
#define _PIGLIB_H
#pragma once
GCC_SYSTEM

typedef enum{
	FSA_NO_EXPLORE_DIRECTORY,
	FSA_EXPLORE_DIRECTORY,
	FSA_STOP
} FileScanAction;

typedef void (*DataFreeCallback)(void *data);

typedef struct NewPigEntry
{
	const char	*fname;
	U32		timestamp;
	U8		*data;
	int		size;
	// These are used when the data is already compressed, (like when patching)
	U32		pack_size;
	U32		checksum[4];
	U32		dont_pack : 1;
	U32		must_pack : 1;
	U32		no_devassert : 1; //Turns off the devassert when fixing 0 checksums
	// Only used by hoggs:
	const U8 *header_data; // Pointer into either data* or a static buffer
	U32		header_data_size;
	DataFreeCallback free_callback;
} NewPigEntry;

void pigChecksumData(const U8* data, U32 size, U32 checksumOut[4]);
#define pigChecksumAndPackEntry(entry) pigChecksumAndPackEntryEx(entry, 0)
void pigChecksumAndPackEntryEx(NewPigEntry *entry, int special_heap);

//#define PIG_SET_DISABLED // disables loading of Pig files and therefore only loads from the filesystem

#include "stdtypes.h"

typedef struct HogFile HogFile;

#define PIG_VERSION 2


void pigDisablePiggs(int disabled); // Disables loading piggs.

typedef struct PigFileDescriptor {
	const char *debug_name;
	HogFile *parent_hog;
	S32 file_index;
	// Cached data:
	U32 size; // TODO: ALaFramboise to fix for larger sizes
	U32 header_data_size:31;
	U32 release_hog_on_close:1;
} PigFileDescriptor;

// Include a particular Windows resource, representing a .hogg file, within the pig set.
// Currently, this must be called before PigSetInit().
void PigSetIncludeResource(int id);

int PigSetInit(void); // Loads all of the Pig files from the gamedatadir
bool PigSetInited(void);
void PigSetDestroy(void);
PigFileDescriptor PigSetGetFileInfo(int pig_index, int file_index, const char *debug_relpath);
PigFileDescriptor PigSetGetFileInfoFake(const char *hogname, const char *file_name);
U32 PigSetExtractBytes(PigFileDescriptor *desc, void *buf, U32 pos, U32 size); // Extracts size bytes into a pre-allocated buffer

void PigSetCompression(unsigned int level);
int PigSetGetNumPigs(void);
int PigSetGetNumPigsNoLoad(void);
HogFile *PigSetGetHogFile(int index);
void PigSetAdd(HogFile *hog_file);

extern int pig_debug;

bool pigShouldCacheHeaderData(const char *ext);
bool pigShouldBeUncompressed(const char *ext);

bool doNotCompressMySize(NewPigEntry * entry);
bool doNotCompressMyExt(NewPigEntry * entry);

#endif

