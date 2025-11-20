#include "stdtypes.h"
#include "file.h"

// Internal function (called by hoglib or the patcher)
U8 *pigGetHeaderData(NewPigEntry *entry, U32 *size);
U32 PigExtractBytesInternal(HogFile *hog_file, FILE *file, int file_index, void *buf, U32 pos, U32 size, U64 fileoffset, U32 filesize, U32 pack_size);
