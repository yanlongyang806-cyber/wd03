#pragma once
GCC_SYSTEM

#include "stdtypes.h"

typedef struct DiffStats
{
	U32 num_blocks, num_bytes;
	U32 miss_first, miss_second, checksums;
	U32 checksum_hist[256];
} DiffStats;

int bindiffCreatePatch(U8 *orig,U32 orig_len,U8 *target,U32 target_len,U8 **output,int minMatchSize);
int bindiffApplyPatch(U8 *src,U8 *patchdata,U8 **target_p);
U32 *bindiffMakeFingerprints(U8 *data,U32 len,int minMatchSize,U32 *size_p);
int bindiffCreatePatchReqFromFingerprints(U32 *crcs,U32 num_crcs,U8 *src,U32 src_len,U8 *dst,U32 dst_len,U32 **block_reqs,int minMatchSize,U32 *scoreboard,
										  U8 *copied,U32 max_contiguous, U32 minimum_row, DiffStats **stats);

