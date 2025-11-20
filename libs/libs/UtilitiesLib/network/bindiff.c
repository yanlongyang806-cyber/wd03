#include "bindiff.h"

#include <stdio.h>
#include "utils.h"
#include "stashtable.h"
#include <string.h>
#include "earray.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

#ifdef _FULLDEBUG
#define STATS(diff, op) diff->stats->op;
#else
#define STATS(diff, op) ;
#endif

static int insert_bytes,copy_bytes,insert_ops,copy_ops;

typedef struct
{
	U8	*data;
	U32	size;
	int	max;
} DataBlock;

typedef struct
{
	int	src_pos;
	int	count;
} DiffCommand;

typedef struct
{
	DiffCommand	*data;
	U32	size;
	int	max;
} CommandBlock;

typedef struct
{
	int				MinMatchSize;
	StashTable		FingerPrintTable;
	bool *			FirstLookup;
	DataBlock		src,dst;
	CommandBlock	commands;
	DataBlock		copydata;
	U16				checksum_low,checksum_high;
	U32				*rolling_crcs;
	DiffStats       *stats;
} DiffState;

typedef struct ChecksumState
{
	U16 low;
	U16 high;
	const unsigned char * buf_ptr;
} ChecksumState;


static const unsigned short randomValue[256] =
{
  0xbcd1, 0xbb65, 0x42c2, 0xdffe, 0x9666, 0x431b, 0x8504, 0xeb46,
  0x6379, 0xd460, 0xcf14, 0x53cf, 0xdb51, 0xdb08, 0x12c8, 0xf602,
  0xe766, 0x2394, 0x250d, 0xdcbb, 0xa678, 0x02af, 0xa5c6, 0x7ea6,
  0xb645, 0xcb4d, 0xc44b, 0xe5dc, 0x9fe6, 0x5b5c, 0x35f5, 0x701a,
  0x220f, 0x6c38, 0x1a56, 0x4ca3, 0xffc6, 0xb152, 0x8d61, 0x7a58,
  0x9025, 0x8b3d, 0xbf0f, 0x95a3, 0xe5f4, 0xc127, 0x3bed, 0x320b,
  0xb7f3, 0x6054, 0x333c, 0xd383, 0x8154, 0x5242, 0x4e0d, 0x0a94,
  0x7028, 0x8689, 0x3a22, 0x0980, 0x1847, 0xb0f1, 0x9b5c, 0x4176,
  0xb858, 0xd542, 0x1f6c, 0x2497, 0x6a5a, 0x9fa9, 0x8c5a, 0x7743,
  0xa8a9, 0x9a02, 0x4918, 0x438c, 0xc388, 0x9e2b, 0x4cad, 0x01b6,
  0xab19, 0xf777, 0x365f, 0x1eb2, 0x091e, 0x7bf8, 0x7a8e, 0x5227,
  0xeab1, 0x2074, 0x4523, 0xe781, 0x01a3, 0x163d, 0x3b2e, 0x287d,
  0x5e7f, 0xa063, 0xb134, 0x8fae, 0x5e8e, 0xb7b7, 0x4548, 0x1f5a,
  0xfa56, 0x7a24, 0x900f, 0x42dc, 0xcc69, 0x02a0, 0x0b22, 0xdb31,
  0x71fe, 0x0c7d, 0x1732, 0x1159, 0xcb09, 0xe1d2, 0x1351, 0x52e9,
  0xf536, 0x5a4f, 0xc316, 0x6bf9, 0x8994, 0xb774, 0x5f3e, 0xf6d6,
  0x3a61, 0xf82c, 0xcc22, 0x9d06, 0x299c, 0x09e5, 0x1eec, 0x514f,
  0x8d53, 0xa650, 0x5c6e, 0xc577, 0x7958, 0x71ac, 0x8916, 0x9b4f,
  0x2c09, 0x5211, 0xf6d8, 0xcaaa, 0xf7ef, 0x287f, 0x7a94, 0xab49,
  0xfa2c, 0x7222, 0xe457, 0xd71a, 0x00c3, 0x1a76, 0xe98c, 0xc037,
  0x8208, 0x5c2d, 0xdfda, 0xe5f5, 0x0b45, 0x15ce, 0x8a7e, 0xfcad,
  0xaa2d, 0x4b5c, 0xd42e, 0xb251, 0x907e, 0x9a47, 0xc9a6, 0xd93f,
  0x085e, 0x35ce, 0xa153, 0x7e7b, 0x9f0b, 0x25aa, 0x5d9f, 0xc04d,
  0x8a0e, 0x2875, 0x4a1c, 0x295f, 0x1393, 0xf760, 0x9178, 0x0f5b,
  0xfa7d, 0x83b4, 0x2082, 0x721d, 0x6462, 0x0368, 0x67e2, 0x8624,
  0x194d, 0x22f6, 0x78fb, 0x6791, 0xb238, 0xb332, 0x7276, 0xf272,
  0x47ec, 0x4504, 0xa961, 0x9fc8, 0x3fdc, 0xb413, 0x007a, 0x0806,
  0x7458, 0x95c6, 0xccaa, 0x18d6, 0xe2ae, 0x1b06, 0xf3f6, 0x5050,
  0xc8e8, 0xf4ac, 0xc04c, 0xf41c, 0x992f, 0xae44, 0x5f1b, 0x1113,
  0x1738, 0xd9a8, 0x19ea, 0x2d33, 0x9698, 0x2fe9, 0x323f, 0xcde2,
  0x6d71, 0xe37d, 0xb697, 0x2c4f, 0x4373, 0x9102, 0x075d, 0x8e25,
  0x1672, 0xec28, 0x6acb, 0x86cc, 0x186e, 0x9414, 0xd674, 0xd1a5
};

#define ROLLING_MASK 0xffffffff

#define FIRST_LOOKUP_SIZE (1 << 10)
#define FIRST_LOOKUP_MASK (FIRST_LOOKUP_SIZE - 1)

#define JUMP_AHEAD_ARROW 1
#define TOLERANCE_ARROW 2

static bool checksumLookup(DiffState * diff, U32 checksum, U32 * dst_idx)
{
	STATS(diff,checksums += 1);
	if(diff->FirstLookup && diff->FirstLookup[checksum & FIRST_LOOKUP_MASK])
		if(stashIntFindInt(diff->FingerPrintTable, checksum, dst_idx))
		{
			return true;
		}
		else
		{
			STATS(diff,miss_second += 1);
			return false;
		}
	else
	{
		STATS(diff,miss_first += 1);
		return false;
	}
}

static U32 checksumInit(DiffState *state, const unsigned char* buf, int size)
{
	int		i;
	U16 high=0, low=0;

	for (i=0;i<size;i++)
	{
		low  += buf[i];
		high += (size-i)*buf[i];
	}

	if(state)
	{
		state->checksum_low = low;
		state->checksum_high = high;
	}

	return ((U32)high) << 16 | low;
}

static U32 checksumRoll(DiffState *state, const unsigned char* buf, int k, int size)
{
	//state->checksum_low = state->checksum_low - randomValue[*(buf-1)] + randomValue[buf[size-1]];
	//state->checksum_high = state->checksum_high - randomValue[*(buf-1)] * size + state->checksum_low;
	state->checksum_low = state->checksum_low - buf[k-1] + buf[k+size-1];
	state->checksum_high = state->checksum_high - size*buf[k-1] + state->checksum_low;

	return ((U32)state->checksum_high) << 16 | state->checksum_low;
}

static U32 checksum(const unsigned char* buf, int matchSize)
{
	return checksumInit(NULL, buf, matchSize);
}

static void checksum_split(const unsigned char * buf, int matchSize, U16 * low_ptr, U16 * high_ptr)
{
	U32 sum = checksumInit(NULL, buf, matchSize);

	*low_ptr = sum&0x0FFFF;
	*high_ptr = sum >> 16;
}

static U32 checksum_cached(ChecksumState * state, const unsigned char * buf, int size)
{
	const unsigned char * ptr = state->buf_ptr;
	U16 low, high;

	if(ptr && (buf > ptr) && ((int)(buf - ptr) < (size >> 2)))
	{
		low = state->low;
		high = state->high;
		while(ptr != buf)
		{
			//low = low - randomValue[ptr[0]] + randomValue[ptr[size]];
			//high = high - randomValue[ptr[0]] * size + low;

			ptr++;
			low = low - ptr[-1] + ptr[size-1];
			high = high - size*ptr[-1] + low;
			
		}

		state->high = high;
		state->low = low;
		state->buf_ptr = buf;
	}
	else
	{
		checksum_split(buf, size, &state->low, &state->high);
		state->buf_ptr = buf;
	}

	return(state->high << 16 | state->low) & ROLLING_MASK;
}

U32 *bindiffMakeFingerprints(U8 *data,U32 len,int minMatchSize,U32 *size_p)
{
	U32 j,i,*crcs,num_prints;

	PERFINFO_AUTO_START_FUNC();

	num_prints = len / minMatchSize;
	crcs = malloc(num_prints * sizeof(crcs[0]));
	for(j=i=0;i+minMatchSize <= len; i+=minMatchSize,j++)
		crcs[j] = checksum(&data[i], minMatchSize);
	if (size_p)
		*size_p = num_prints;

	PERFINFO_AUTO_STOP_FUNC();

	return crcs;
}

static void hashFingerPrintTable(DiffState *diff,U32 size)
{
	U32 j,i;
	U32 * collisions = NULL;

	diff->FingerPrintTable = stashTableCreateInt(4 * size / diff->MinMatchSize);
	diff->FirstLookup = calloc(1, FIRST_LOOKUP_SIZE * sizeof(bool));
	for(j=i=0;i+diff->MinMatchSize <= size; i+=diff->MinMatchSize,j++)
	{
		if (diff->rolling_crcs[j])
		{
			diff->FirstLookup[diff->rolling_crcs[j] & FIRST_LOOKUP_MASK] = true;
			if(!stashIntAddInt(diff->FingerPrintTable,diff->rolling_crcs[j], j, 0))
			{
				eaiPush(&collisions, diff->rolling_crcs[j]);
			}
		}
	}

	for(i = 0; i < eaiUSize(&collisions); i++)
	{
		stashIntRemoveInt(diff->FingerPrintTable, collisions[i], NULL);
	}
	eaiDestroy(&collisions);
}

static void addCommand(DiffState *diff,int insert,int count,int src_idx)
{
	DiffCommand	*cmd;

	if (insert)
	{
		insert_bytes+=count;
		insert_ops++;
		src_idx = 0x80000000;
	}
	else
	{
		copy_bytes+=count;
		copy_ops++;
	}
	cmd = dynArrayAdd(diff->commands.data,sizeof(diff->commands.data[0]),diff->commands.size,diff->commands.max,1);
	cmd->src_pos	= src_idx;
	cmd->count		= count;
}

static void insertData(DiffState *diff,int pos,int count)
{
	U8	*mem;

	if (!count)
		return;
	mem = dynArrayAdd(diff->copydata.data,sizeof(diff->copydata.data[0]),diff->copydata.size,diff->copydata.max,count);
	memcpy(mem,diff->dst.data+pos,count);
	addCommand(diff,1,count,0);
}

static void copyData(DiffState *diff,int pos,int src_pos,int count)
{
	addCommand(diff,0,count,src_pos);
}

static int expandMatch(DataBlock *src,DataBlock *dst,int src_idx,int dst_idx,int *backwards_p,int max_match)
{
	int		i,count,backwards = *backwards_p;
	U8		*s,*d;

	s = &src->data[src_idx];
	d = &dst->data[dst_idx];

	backwards = MIN(src_idx,backwards);
	for(i=1;i<=backwards;i++)
	{
		if (s[-i] != d[-i])
			break;
	}
	backwards = i-1;
	count = MIN(src->size - src_idx,dst->size - dst_idx);
	count = MIN(count,max_match - backwards);
	for(i=0;i<count;i++)
	{
		if (s[i] != d[i])
			break;
	}
	*backwards_p = backwards;
	return i + backwards;
}

static void calcDiff(DiffState *diff)
{
	U32					match_size,j,i,insert_pos=0,src_idx,idx;
	DataBlock			*src,*dst;
	int					hash=0,re_init=1,backwards=0;

	insert_bytes = copy_bytes = insert_ops = copy_ops = 0;

	src = &diff->src;
	dst = &diff->dst;

	for(i=0;i+diff->MinMatchSize <= dst->size;i++)
	{
		if (re_init)
			hash = checksumInit(diff,&dst->data[i],diff->MinMatchSize);
		else
			hash = checksumRoll(diff, dst->data, i, diff->MinMatchSize);
		re_init = 0;

		if (!checksumLookup(diff, hash, &src_idx))
			continue;

		for(idx=src_idx,j=i;j+diff->MinMatchSize<=dst->size;j+=diff->MinMatchSize,idx++)
		{
			if (diff->rolling_crcs[idx] != checksum(&dst->data[j],diff->MinMatchSize))
				break;
		}
		match_size = j - i;

		if (src->data)
		{
			backwards = i-insert_pos;
			match_size = expandMatch(src,dst,src_idx*diff->MinMatchSize,i,&backwards,0x7fffffff);
			if (match_size < (U32)diff->MinMatchSize)
				continue;
		}
		else
		{
			if (match_size < (U32)diff->MinMatchSize * 3)
				continue;
		}
		insertData(diff,insert_pos,i-insert_pos-backwards);
		copyData(diff,i-backwards,src_idx * diff->MinMatchSize - backwards,match_size);
		i+=match_size-1-backwards;
		insert_pos = i+1;
		re_init = 1;
	}
	insertData(diff,insert_pos,dst->size-insert_pos);
}

static int packDiff(DiffState *diff,U8 **output)
{
	U8			*mem;
	U32			cmdsize;

	// pack diff bytes into one chunk
	cmdsize = diff->commands.size * sizeof(diff->commands.data[0]);
	mem = malloc(cmdsize + diff->copydata.size + sizeof(U32));
	*((U32 *)mem) = diff->commands.size;
	memcpy(mem+4,diff->commands.data,cmdsize);
	memcpy(mem+4+cmdsize,diff->copydata.data,diff->copydata.size);
	*output = mem;
	free(diff->copydata.data);
	free(diff->commands.data);
	return cmdsize + diff->copydata.size + 4;
}

int bindiffCreatePatchFromFingerprints(U32 *crcs,U32 num_crcs,U8 *target,U32 target_len,U8 **output,int minMatchSize)
{
	DiffState	diff = {0};

	// Init diffstate
	diff.dst.data		= target;
	diff.dst.size		= target_len;
	diff.MinMatchSize	= minMatchSize;
	diff.rolling_crcs	= crcs;
	diff.src.size		= num_crcs * minMatchSize;

	hashFingerPrintTable(&diff,diff.src.size);
	calcDiff(&diff);
	stashTableDestroy(diff.FingerPrintTable);
	free(diff.FirstLookup);

	return packDiff(&diff,output);
}

int bindiffCreatePatch(U8 *orig,U32 orig_len,U8 *target,U32 target_len,U8 **output,int minMatchSize)
{
	DiffState	diff = {0};

	// Init diffstate
	diff.src.data = orig;
	diff.src.size = orig_len;
	diff.dst.data = target;
	diff.dst.size = target_len;
	diff.MinMatchSize = minMatchSize;

	// Create a table to hold all the finger prints for the orig file.
	if (minMatchSize)
	{
		diff.rolling_crcs = bindiffMakeFingerprints(diff.src.data,diff.src.size,diff.MinMatchSize,0);
		hashFingerPrintTable(&diff,diff.src.size);
		calcDiff(&diff);
		free(diff.rolling_crcs);
		stashTableDestroy(diff.FingerPrintTable);
		free(diff.FirstLookup);
	}
	else
		insertData(&diff,0,diff.dst.size);

	return packDiff(&diff,output);
}

int bindiffApplyPatch(U8 *src,U8 *patchdata,U8 **target_p)
{
	int			i,cmd_count,data_pos=0,dst_pos=0;
	DiffCommand	*cmds,*cmd;
	U8			*dst,*data;

	cmd_count = *((U32*)patchdata);
	cmds = (DiffCommand*)(patchdata+4);
	data = patchdata+cmd_count*sizeof(DiffCommand)+4;
	for(i=0;i<cmd_count;i++)
		dst_pos += cmds[i].count;
	dst = malloc(dst_pos);
	for(dst_pos=i=0;i<cmd_count;i++)
	{
		cmd = &cmds[i];
		if (cmd->src_pos == 0x80000000)
		{
			memcpy(dst+dst_pos,data+data_pos,cmd->count);
			data_pos += cmd->count;
		}
		else
		{
			if (!src)
			{ //We've been told to apply a patch to data we don't have
				free(dst);
				return -1;
			}
			memcpy(dst+dst_pos,src+cmd->src_pos,cmd->count);
		}
		dst_pos += cmd->count;
	}
	*target_p = dst;
	return dst_pos;
}









// code for doing reverse rsync


U32 hash_lookups;

static void calcDiffReq(DiffState *diff, U32 *scoreboard, U8 *copied, U32 max_contiguous, U32 minimum_row)
{
	U32					match_size, j, i, insert_pos = 0, dst_idx, idx;
	U32					bookmark, jump_ahead, jump_extra = 1, tolerance, saved, hash = 0;
	U32					fib1 = 0, fib2 = 1, num_blocks, contiguous_start;
	DataBlock			* src, * dst;
	int					re_init = 1;
	ChecksumState		checksum_state = {0};

	if(minimum_row < 2 || minimum_row > 10)
		minimum_row = 2;

	insert_bytes = copy_bytes = insert_ops = copy_ops = 0;

	src = &diff->src;
	dst = &diff->dst;

	bookmark = 0;
	saved = 0;
	jump_ahead = ((((U32)diff->MinMatchSize << 4) > (src->size >> 8)) ? (diff->MinMatchSize << 4) : (src->size >> 8));
	//printf("jump_ahead is %u\n", jump_ahead);
	tolerance = (diff->MinMatchSize >> 1);

	num_blocks = dst->size / diff->MinMatchSize;
	if(num_blocks >= 8)
	{
		for(i=0;i+diff->MinMatchSize <= src->size;i++)
		{
			if(i - bookmark > tolerance && saved < ((i - bookmark) >> 2))
			{
				re_init = 1;
				jump_extra = fib1 + fib2;
				fib1 = fib2;
				fib2 = jump_extra;
				i += jump_ahead + jump_extra - 1;
				if(tolerance > 1)
					tolerance >>= 1;
				bookmark = i;
				saved = 0;
				continue;
			}

			if (copied[i >> 3] == 255)
			{
				re_init = 1;
				i+=7;
				continue;
			}
	
			if (re_init)
				hash = checksumInit(diff,&(src->data[i]),diff->MinMatchSize);
			else
				hash = checksumRoll(diff, src->data, i ,diff->MinMatchSize);
			STATS(diff,checksum_hist[hash >> 24]++);

			re_init = 0;

			hash_lookups++;
			if (!checksumLookup(diff, hash, &dst_idx))
				continue;
			if (scoreboard[dst_idx])
				continue;

			for(idx = dst_idx + 1, j = i + diff->MinMatchSize; ; j += diff->MinMatchSize, idx++)
			{
				if(j + diff->MinMatchSize > src->size)
					break;
				if(idx * diff->MinMatchSize >= dst->size)
					break;
				if(diff->rolling_crcs[idx] != checksum_cached(&checksum_state, &src->data[j], diff->MinMatchSize))
					break;
			}
			match_size = j - i;

			if (match_size < (U32)diff->MinMatchSize * minimum_row)
			{
				int		match_next,match_prev;

				match_next = (dst_idx+1)*diff->MinMatchSize >= dst->size && (scoreboard[dst_idx+1]-1 == i + diff->MinMatchSize);
				match_prev = dst_idx > 0 && (scoreboard[dst_idx-1]-1 == i - diff->MinMatchSize);
				if ( (minimum_row==2 && (!match_next && !match_prev)) || (minimum_row>2 && (!match_next || !match_prev)) )
				{
					i += match_size - 1;
					re_init = 1;
					continue;
				}
			}

			memcpy(dst->data + dst_idx * diff->MinMatchSize,src->data+i,match_size);

			for(j=i;j<i+match_size;j++)
				copied[j>>3] |= 1 << (j & 7);
			for(j=0;j<idx-dst_idx;j++)
				scoreboard[j + dst_idx] = 1 + i + diff->MinMatchSize * j;

			//memset(blocks+dst_idx,1,idx-dst_idx);
			//printf(" miss: %d-%d\n",insert_pos,i);
			//printf("match: %d-%d\n",i,j);
			i+=match_size-1;
			insert_pos = i+1;
			re_init = 1;
			saved += match_size;
		}
		//printf(" miss: %d-%d\n",insert_pos,src->size);
	}
	
	for(i=0; i<num_blocks; i++)
	{
		for(j = i; j < num_blocks; j++)
		{
			if (scoreboard[j])
				break;
		}
		for(contiguous_start = i; contiguous_start < j; contiguous_start += max_contiguous)
		{
			addCommand(diff, 0, ((j - contiguous_start < max_contiguous) ? (j - contiguous_start) : max_contiguous), contiguous_start);
		}
		//printf("need blocks: %d-%d\n",i,j);
		i = j;
	}
}

int bindiffCreatePatchReqFromFingerprints(U32 *crcs,U32 num_crcs,U8 *src,U32 src_len,U8 *dst,U32 dst_len,U32 **block_reqs,int minMatchSize,U32 *scoreboard,
										  U8 *copied,U32 max_contiguous, U32 minimum_row, DiffStats **stats)
{
	DiffState	diff = {0};

	PERFINFO_AUTO_START_FUNC();

	// Init diffstate
	diff.dst.size		= dst_len;
	diff.dst.data		= dst;
	diff.src.data		= src;
	diff.src.size		= src_len;
	diff.MinMatchSize	= minMatchSize;
	diff.rolling_crcs	= crcs;
#ifdef _FULLDEBUG
	if(stats)
		if(*stats)
			diff.stats = *stats;
		else
		{
			diff.stats = calloc(1, sizeof(DiffStats));
			*stats = diff.stats;
		}
	else
	{
		diff.stats = _alloca(sizeof(DiffStats));
		memset(diff.stats, 0, sizeof(DiffStats));
	}
#endif

	hashFingerPrintTable(&diff,diff.dst.size);
	hash_lookups = 0;

	calcDiffReq(&diff,scoreboard,copied,max_contiguous, minimum_row);

	//printf("hashcount = %d\n",hash_lookups);
	stashTableDestroy(diff.FingerPrintTable);
	free(diff.FirstLookup);

	*block_reqs = (U32 *)diff.commands.data;

	PERFINFO_AUTO_STOP_FUNC();

	return diff.commands.size;
}
