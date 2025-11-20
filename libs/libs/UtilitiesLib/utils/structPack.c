#include "structPack.h"
#include "TokenStore.h"
// #define BITSTREAM_DEBUG
#include "BitStream.h"
#include "EString.h"
#include "structInternals.h"
#include "ScratchStack.h"
#include "zutils.h"
#include "StashTable.h"
#include "AutoGen/structPack_h_ast.c"
#include "structinternals_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping("SerializablePackedStructStream", BUDGET_EngineMisc););


//#define DEBUG_STRUCT_PACK // comment this out

#define STRUCT_PACK_STRING_INDEX_BITS 15 // TODO: Make this dependent on the data

#if 0
// Disabling checking by default, as there is likely a bug in the CRCing code still
static StructPackFlags default_structpack_flags = STRUCT_PACK_WRITE_CRCS; //  | STRUCT_PACK_CHECK_CRCS;
#else
static StructPackFlags default_structpack_flags = STRUCT_PACK_WRITE_CRCS | STRUCT_PACK_CHECK_CRCS;
#endif

AUTO_COMMAND ACMD_COMMANDLINE;
void disableStructPackCRCs(int value)
{
	if (value) {
		default_structpack_flags &= ~STRUCT_PACK_CHECK_CRCS;
	} else {
		default_structpack_flags |= STRUCT_PACK_CHECK_CRCS;
	}
}

static struct {
	int string_index_count;
	int string_index_bit_count;
	int string_table_entry_count;
	int array_indicies_count;
	int array_indicies_bit_count;
	int struct_indicies_count;
	int struct_indicies_bit_count;
} struct_pack_stats;

static int bitsFromStringTableSize(int size);

static int StructPackAllocator(BitStream *bs)
{
	PackedStructStream *pack = bsGetUserData(bs);
	int oldMaxSize = bsGetMaxSize(bs);
	int newMaxSize = oldMaxSize?(oldMaxSize*1.5):1024;
	bsSetNewMaxSizeAndDataPtr(bs,
		newMaxSize,
		strealloc(bsGetDataPtr(bs), newMaxSize, pack));
	return 1;
}

static void StructPackZip(ParseTable pti[], const void *structptr, PackedStructStream *pack MEM_DBG_PARMS)
{
	char *estr=NULL;
	int datasize;
	void *data;
	estrStackCreateSize(&estr, 10000);
	ParserWriteText_dbg(&estr, pti, (void*)structptr, 0, 0, 0 MEM_DBG_PARMS_CALL);
	data = zipDataEx_dbg(estr, estrLength(&estr), &datasize, 9, false, 0 MEM_DBG_PARMS_CALL);
	//bsWriteString(pack->bs, estr);
	bsWriteBitsPack(pack->bs, 4, datasize);
	bsWriteBitsPack(pack->bs, 4, estrLength(&estr));
	bsWriteBitsArray(pack->bs, datasize*8, data);
	free(data);
	estrDestroy(&estr);
}

static void *StructUnpackZip(ParseTable pti[], PackedStructStream *pack, U32 index)
{
	char *str;
	void *data;
	int data_size;
	int unpacked_size;
	void *structptr;
	assert(pack->bs);
	data_size = bsReadBitsPack(pack->bs, 4);
	unpacked_size = bsReadBitsPack(pack->bs, 4);
	data = ScratchAlloc(data_size);
	str = ScratchAlloc(unpacked_size);
	bsReadBitsArray(pack->bs, data_size * 8, data);
	unzipData(str, &unpacked_size, data, data_size);
	structptr = StructAllocVoid(pti);
	ParserReadText(str, pti, structptr, 0);
	ScratchFree(str);
	ScratchFree(data);
	return structptr;
}

// These are the seemingly optimal numbers for at least Materials at the time of
//   writing this, but I'm not sure why ;)
#define NB0 9
#define NB1 17
#define NB2 26
static const int num_bits[] = { NB0, NB1, NB2, 32 };
static const int auto_masks[] = {~((1 << NB0)-1), ~((1 << NB1)-1), ~((1 << NB2)-1)};
STATIC_ASSERT(ARRAY_SIZE(auto_masks) == ARRAY_SIZE(num_bits)-1);

static void sendPtrDiff(BitStream *bs, ptrdiff_t val)
{
	int		i;
	bsWriteBits(bs, 1, (val < 0)?1:0);
	val = ABS(val);

	assert(!(~0xFFFFFFFFLL & (val)));

	for(i=0; i<ARRAY_SIZE(auto_masks); i++)
	{
		if (!(val & auto_masks[i]))
			break;
	}
	bsWriteBits(bs, 2, i);
	bsWriteBits(bs,num_bits[i],val);
}

static ptrdiff_t recvPtrDiff(BitStream *bs)
{
	ptrdiff_t val=1;
	int idx;

	if (bsReadBits(bs, 1)) {
		val = -1;
	}

	idx = bsReadBits(bs, 2);
	assert(idx < ARRAY_SIZE(num_bits));
	val *= bsReadBits(bs,num_bits[idx]);
	return val;
}

#define NBint0 4
#define NBint1 9
#define NBint2 14
static const int num_bits_int[] = { NBint0, NBint1, NBint2, 32 };
static const int auto_masks_int[] = {~((1 << NBint0)-1), ~((1 << NBint1)-1), ~((1 << NBint2)-1)};
STATIC_ASSERT(ARRAY_SIZE(auto_masks_int) == ARRAY_SIZE(num_bits_int)-1);


static void sendIntDiff(BitStream *bs, int val)
{
	int		i;
	if (val == 1) {
		bsWriteBits(bs, 1, 1);
	} else {
		bsWriteBits(bs, 1, 0);
		bsWriteBits(bs, 1, (val < 0)?1:0);
		val = ABS(val);

		assert(!(~0xFFFFFFFFLL & (val)));

		for(i=0; i<ARRAY_SIZE(auto_masks_int); i++)
		{
			if (!(val & auto_masks_int[i]))
				break;
		}
		bsWriteBits(bs, 2, i);
		bsWriteBits(bs,num_bits_int[i],val);
	}
}

static int recvIntDiff(BitStream *bs)
{
	int val=1;
	int idx;

	if (bsReadBits(bs, 1)) {
		return 1;
	} else {
		if (bsReadBits(bs, 1)) {
			val = -1;
		}

		idx = bsReadBits(bs, 2);
		assert(idx < ARRAY_SIZE(num_bits_int));
		val *= bsReadBits(bs,num_bits_int[idx]);
		return val;
	}
}

static void StructPackBitpack(ParseTable pti[], const void *structptr, PackedStructStream *pack, int string_table_bits, const char *caller_fname, int line)
{
	U32 stringTableOffsetIndex;
	U32 stringTableOffset;
	U32 stringTableIndex;
	int i;

	// Summary:
	//  Pack all fields, building a pooled string table as we go
	//  Pack string table
	//  Store offset to string table at the beginning

	// Write placeholder for string table and init
	stringTableOffsetIndex = bsGetCursorBitPosition(pack->bs);
	bsWriteBits(pack->bs, string_table_bits, 0);
	if (pack->structStringTable)
		steaiSetSize(&pack->structStringTable, 0, pack);

	// Pack all fields
	if (!StructBitpackSub(pti, structptr, pack)) {
		// do some rewinding or something?  Nah, leave the single terminator in the stream.
	}

	// Write string table offset
	stringTableIndex = bsGetCursorBitPosition(pack->bs);
	stringTableOffset = stringTableIndex - stringTableOffsetIndex;
	assert(!(stringTableOffset & ~((1 << string_table_bits)-1))); // We only saved string_table_bits bits!
	bsSetCursorBitPosition(pack->bs, stringTableOffsetIndex);
	bsWriteBits(pack->bs, string_table_bits, stringTableOffset);
	bsSetCursorBitPosition(pack->bs, stringTableIndex);
	// Write string table
	bsWriteBitsPack(pack->bs, 1, eaiSize(&pack->structStringTable));
	for (i=0; i<eaiSize(&pack->structStringTable); i++) {
		if (i==0) {
			bsWriteBitsPack(pack->bs, STRUCT_PACK_STRING_INDEX_BITS, pack->structStringTable[i]);
		} else {
			sendIntDiff(pack->bs, pack->structStringTable[i] - pack->structStringTable[i-1]);
		}
		struct_pack_stats.string_table_entry_count++;
	}
	// Release string table
	if (eaiSize(&pack->structStringTable))
		eaiSetSize(&pack->structStringTable, 0);
}

static void *StructUnpackBitpack(ParseTable pti[], PackedStructStream *pack, U32 index, int string_table_bits)
{
	int stringCount, i;
	void *structptr;
	U32 streamStartOffset;
	U32 stringTableOffset;
	U32 stringTableOffsetIndex;
	U32 stringTableIndex;

	stringTableOffsetIndex = bsGetCursorBitPosition(pack->bs);
	stringTableOffset = bsReadBits(pack->bs, string_table_bits);
	stringTableIndex = stringTableOffsetIndex + stringTableOffset;
	streamStartOffset = bsGetCursorBitPosition(pack->bs);
	// Read string table
	bsSetCursorBitPosition(pack->bs, stringTableIndex);
	pack->effStringTableSize = 0;
	stringCount = bsReadBitsPack(pack->bs, 1);
	assert(!eaiSize(&pack->structStringTable));
	for (i=0; i<stringCount; i++) {
		if (i==0) {
			steaiPush(&pack->structStringTable, bsReadBitsPack(pack->bs, STRUCT_PACK_STRING_INDEX_BITS), pack);
		} else {
			steaiPush(&pack->structStringTable, pack->structStringTable[i-1] + recvIntDiff(pack->bs), pack);
		}
	}

	// Read data
	bsSetCursorBitPosition(pack->bs, streamStartOffset);
	structptr = StructAllocVoid(pti);
	StructUnbitpackSub(pti, structptr, pack);

	// Release string table
	eaiDestroy(&pack->structStringTable);

	return structptr;
}

U32 StructPack_dbg(ParseTable pti[], const void *structptr, PackedStructStream *pack MEM_DBG_PARMS)
{
	U32 ret, oldcrc = 0;
	assertmsg(pack->bs,"Stream must be initialized");

	bsChangeMode(pack->bs, Write); // Also forwards to the end of the stream

	bsAlignByte(pack->bs);
	ret = bsGetCursorBitPosition(pack->bs);

	if (pack->flags & STRUCT_PACK_WRITE_CRCS)
	{
		oldcrc = StructCRC(pti, (void*)structptr);
		bsWriteBits(pack->bs, 32, oldcrc);
	}

	switch (pack->method) {
	xcase STRUCT_PACK_ZIP:
		StructPackZip(pti, structptr, pack MEM_DBG_PARMS_CALL);
	xcase STRUCT_PACK_BITPACK:
		StructPackBitpack(pti, structptr, pack, 16 MEM_DBG_PARMS_CALL);
	xcase STRUCT_PACK_BITPACK_LARGE_STRINGTABLE:
		StructPackBitpack(pti, structptr, pack, 28 MEM_DBG_PARMS_CALL);
	xdefault:
		assert(0);
	}

#ifdef DEBUG_STRUCT_PACK
	{
		U32 saved = bsGetCursorBitPosition(pack->bs);
		void *unpacked = StructUnpack(pti, pack, ret);
		U32 newcrc = StructCRC(pti, unpacked);
		int diff = StructCompare(pti, unpacked, structptr, 0, 0, 0);
		if (diff!=0)
			assertmsg(diff==0, "Unpacked struct differs from original");
		if (oldcrc != newcrc)
			assertmsg(oldcrc == newcrc, "Unpacked struct's CRC differs from the original");
		StructDestroyVoid(pti, unpacked);
		bsSetCursorBitPosition(pack->bs, saved);
	}
#endif

	return ret;
}

U32 PackedStructStreamCopyStruct_dbg(U32 uFileOffsetSrc, U32 uSize, PackedStructStream *dst, PackedStructStream const *src MEM_DBG_PARMS)
{
	void * pData = alloca(uSize/8+1);
	U32 ret;
	bsSetCursorBitPosition(src->bs,uFileOffsetSrc);
	bsReadBitsArray(src->bs,uSize,pData);

	bsAlignByte(dst->bs);

	ret = bsGetCursorBitPosition(dst->bs);
	bsWriteBitsArray(dst->bs,uSize,pData);

	return ret;
}

// Do NOT call this function outside of the main thread if the ParseTable contains a REF_TO because you WILL cause an assert.
void *StructUnpack(ParseTable pti[], PackedStructStream *pack, U32 index)
{
	void *ret;
	U32 oldcrc=0;

	// Push values
	int saved_effStringTableSize = pack->effStringTableSize;
	int *saved_structStringTable = pack->structStringTable;
	U32 saved_bitPosition = bsGetCursorBitPosition(pack->bs);
	pack->effStringTableSize = 0;
	pack->structStringTable = NULL;

	assertmsg(pack->bs,"Stream must be initialized");

	bsChangeMode(pack->bs, Read);
	bsSetCursorBitPosition(pack->bs, index);

	if (pack->flags & STRUCT_PACK_WRITE_CRCS)
		oldcrc = bsReadBits(pack->bs, 32);

	switch (pack->method) {
	xcase STRUCT_PACK_ZIP:
		ret = StructUnpackZip(pti, pack, index);
	xcase STRUCT_PACK_BITPACK:
		ret = StructUnpackBitpack(pti, pack, index, 16);
	xcase STRUCT_PACK_BITPACK_LARGE_STRINGTABLE:
		ret = StructUnpackBitpack(pti, pack, index, 28);
	xdefault:
		assert(0);
	}

	if (pack->flags & STRUCT_PACK_WRITE_CRCS &&
		default_structpack_flags & STRUCT_PACK_CHECK_CRCS)
	{
		U32 newcrc = StructCRC(pti, ret);
		if (newcrc != oldcrc) {
			assert(newcrc == oldcrc);
		}
	}

	// Pop values
	pack->effStringTableSize = saved_effStringTableSize;
	pack->structStringTable = saved_structStringTable;
	bsSetCursorBitPosition(pack->bs, saved_bitPosition);

	return ret;
}

#define BITS_FOR_ARRAY_INDEX 1

bool StructBitpackArraySub(ParseTable tpi[], int column, const void *structptr, PackedStructStream *pack, int numelems)
{
	bool ret=false;
	int i;
	int lastSentIndex=-1;
	for (i = 0; i < numelems; i++) {
		U32 previousPosition;

		previousPosition = bsGetCursorBitPosition(pack->bs);
		bsWriteBits(pack->bs, 1, 1);
		bsWriteBitsPack(pack->bs, BITS_FOR_ARRAY_INDEX, i - lastSentIndex - 1);

		if (nonarray_bitpack(tpi, column, structptr, i, pack)) {
			struct_pack_stats.array_indicies_count++;
			struct_pack_stats.array_indicies_bit_count+=1 + bsGetLengthWriteBitsPack(pack->bs, BITS_FOR_ARRAY_INDEX, i - lastSentIndex - 1);
			// Wrote something
			lastSentIndex = i;
			ret = true;
		} else {
			// Save nothing to the stream
			bsSetCursorBitPosition(pack->bs, previousPosition);
		}
	}

	// Say there there are no more elements.
	bsWriteBits(pack->bs, 1, 0);
	if (ret) {
		struct_pack_stats.array_indicies_count++;
		struct_pack_stats.array_indicies_bit_count+=1;
	}
	return ret;
}

void StructUnbitpackArraySub(ParseTable tpi[], int column, void *structptr, PackedStructStream *pack, int numelems)
{
	int lastIndex=-1;
	int i;
	while (bsReadBits(pack->bs, 1)) {
		int indexDelta = bsReadBitsPack(pack->bs, BITS_FOR_ARRAY_INDEX) + 1;
		int newIndex = lastIndex + indexDelta;
		for (i=lastIndex + 1; i<newIndex; i++) {
			// These in here must be the defaults
			if (tpi[column].type & TOK_INDIRECT && TOK_HAS_SUBTABLE(tpi[column].type))
			{
				TokenStoreAlloc(tpi, column, structptr, i, tpi[column].param, NULL, NULL, NULL);
			}
			nonarray_initstruct(tpi, column, structptr, i);
		}
		nonarray_unbitpack(tpi, column, structptr, newIndex, pack);
		lastIndex = newIndex;
	}
	for (i=lastIndex + 1; i<numelems; i++) {
		// These in here must be the defaults
		if (tpi[column].type & TOK_INDIRECT && TOK_HAS_SUBTABLE(tpi[column].type))
		{
			TokenStoreAlloc(tpi, column, structptr, i, tpi[column].param, NULL, NULL, NULL);
		}
		nonarray_initstruct(tpi, column, structptr, i);
	}
}

#define BITS_FOR_FIELD_INDEX 2

bool StructBitpackSub(ParseTable tpi[], const void *structptr, PackedStructStream *pack)
{
	int i;
	int lastSentIndex=-1;
	bool ret=false;
	// Iterate through all fields defined in StructDef
	FORALL_PARSETABLE(tpi, i)
	{
		U32 previousPosition;
		StructTypeField type = TOK_GET_TYPE(tpi[i].type);
		if (type == TOK_IGNORE ||
			type == TOK_START ||
			type == TOK_END)
			continue;
		if (tpi[i].type & TOK_REDUNDANTNAME)
			continue;

		previousPosition = bsGetCursorBitPosition(pack->bs);

		// Send the index of this field.
		// We're assuming that that we'll have the exact same definition
		// during unpack.
		bsWriteBits(pack->bs, 1, 1);
		bsWriteBitsPack(pack->bs, BITS_FOR_FIELD_INDEX, i - lastSentIndex - 1);

		if (bitpack_autogen(tpi, i, (void*)structptr, 0, pack)) {
			// Wrote something
			struct_pack_stats.struct_indicies_count++;
			struct_pack_stats.struct_indicies_bit_count+=1 + bsGetLengthWriteBitsPack(pack->bs, BITS_FOR_FIELD_INDEX, i - lastSentIndex - 1);
			lastSentIndex = i;
			ret = true;
		} else {
			// Save nothing to the stream
			bsSetCursorBitPosition(pack->bs, previousPosition);
		}
	}

	// Say there there are no more fields.
	bsWriteBits(pack->bs, 1, 0);
	if (ret) {
		struct_pack_stats.struct_indicies_count++;
		struct_pack_stats.struct_indicies_bit_count+=1;
	}
	return ret;
}

void StructUnbitpackSub(ParseTable tpi[], void *structptr, PackedStructStream *pack)
{
	int lastIndex = -1;
	StructInitFields(tpi, structptr);
	while (bsReadBits(pack->bs, 1)) {
		int deltaIndex = bsReadBitsPack(pack->bs, BITS_FOR_FIELD_INDEX) + 1;
		lastIndex += deltaIndex;
		unbitpack_autogen(tpi, lastIndex, (void*)structptr, 0, pack);
	}
}

void PackedStructStreamInit_dbg(PackedStructStream *pack, StructPackMethod method MEM_DBG_PARMS)
{
	assertmsg(!pack->bs,"Stream must not already be initialized");

	pack->bs = initBitStream_dbg(NULL, NULL, 0, Write, false, StructPackAllocator MEM_DBG_PARMS_CALL);
	bsSetUserData(pack->bs, pack);

	MEM_DBG_STRUCT_PARMS_INIT(pack);

	pack->method = method;
	pack->flags = default_structpack_flags;
	pack->stGlobalStringTable = stashTableCreateWithStringKeysEx(1024, StashDefault MEM_DBG_PARMS_CALL);
}

void PackedStructStreamDeinit(PackedStructStream *pack)
{
	if (!pack)
		return;

	assertmsg(pack->bs,"Stream must be initialized");
	eaiDestroy(&pack->structStringTable);
	if (pack->stGlobalStringTable) {
		stashTableDestroy(pack->stGlobalStringTable);
		pack->stGlobalStringTable = NULL;
	}
	eaDestroy(&pack->globalStringTable);
	if (bsGetDataPtr(pack->bs)) {
		free(bsGetDataPtr(pack->bs));
		bsSetNewMaxSizeAndDataPtr(pack->bs, 0, NULL);
	}
	SAFE_FREE(pack->bs);	
}

U32 PackedStructStreamGetSize(PackedStructStream *pack)
{
	assertmsg(pack->bs,"Stream must be initialized");
	return ((bsGetBitLength(pack->bs) + 7) >> 3) + eaSize(&pack->globalStringTable)*sizeof(pack->globalStringTable[0]);
}

void PackedStructStreamFinalize(PackedStructStream *pack)
{
	int oldMaxSize, newMaxSize;
	int remainder;
	assertmsg(pack->bs,"Stream must be initialized");

	steaSetCapacity(&pack->globalStringTable, eaSize(&pack->globalStringTable), pack);
	if (pack->stGlobalStringTable)
	{
		stashTableDestroy(pack->stGlobalStringTable);
		pack->stGlobalStringTable = NULL;
	}
	if ((remainder = (bsGetBitLength(pack->bs)%8)) != 0)
	{
		bsChangeMode(pack->bs, Write);
		bsWriteBitsPack(pack->bs, 8-remainder, 0);
	}
	oldMaxSize = bsGetMaxSize(pack->bs);
	newMaxSize = bsGetLength(pack->bs)+1;
	bsSetNewMaxSizeAndDataPtr(pack->bs,
		newMaxSize,
		strealloc(bsGetDataPtr(pack->bs), newMaxSize, pack));
	eaiDestroy(&pack->structStringTable);
}

static int bitsFromStringTableSize(int size)
{
	// 0-1 -> 0
	// 2 -> 1
	// 3-5 -> 2
	// 6-8 -> 3
	// 9-16 -> 4
	int ret=0;
	size--;
	while (size>0) {
		size >>= 1;
		ret++;
	}
	return ret;
}

void StructBitpackPooledStringSub(const char *str, PackedStructStream *pack)
{
	int temp_int_value;
	int global_index = stashFindInt(pack->stGlobalStringTable, str, &temp_int_value)?temp_int_value:-1;
	int oldSize = eaiSize(&pack->structStringTable);
	int bits = bitsFromStringTableSize(oldSize);
	int struct_index;
	if (global_index == -1) {
		// New global string
		global_index = steaPush(&pack->globalStringTable, str, pack);
		verify(stashAddInt(pack->stGlobalStringTable, str, global_index, false));
	}
	struct_index = eaiFind(&pack->structStringTable, global_index);
	if (struct_index==-1) {
		// New string
		if (oldSize)
			bsWriteBits(pack->bs, 1, 1);
		steaiPush(&pack->structStringTable, global_index, pack);
	} else {
		// Old string
		bsWriteBits(pack->bs, 1, 0);
		if (bits)
			bsWriteBits(pack->bs, bits, struct_index);
		struct_pack_stats.string_index_count++;
		struct_pack_stats.string_index_bit_count+=bits;
	}
}

const char *StructUnbitpackPooledStringSub(PackedStructStream *pack)
{
	int oldSize = pack->effStringTableSize;
	int bits = bitsFromStringTableSize(oldSize);
	int index=0;
	int global_index;
	if (!oldSize || bsReadBits(pack->bs, 1)) {
		// New string
		global_index = pack->structStringTable[pack->effStringTableSize++];
	} else {
		if (bits)
			index = bsReadBits(pack->bs, bits);
		assert(index < pack->effStringTableSize);
		global_index = pack->structStringTable[index];
	}
	assert(global_index >= 0 && global_index < eaSize(&pack->globalStringTable));
	ANALYSIS_ASSUME(pack->globalStringTable);
	return pack->globalStringTable[global_index];
}

SerializablePackedStructStream *PackedStructStreamSerialize_dbg(PackedStructStream *pack MEM_DBG_PARMS)
{
	ParseTableInfo info = {parse_SerializablePackedStructStream, sizeof(SerializablePackedStructStream), caller_fname}; // Override the filename of the allocator
	SerializablePackedStructStream *ret = StructAlloc_dbg(parse_SerializablePackedStructStream, &info MEM_DBG_STRUCT_PARMS_CALL(pack));
	ret->method = pack->method;
	ret->flags = pack->flags;
	steaCopy(&ret->globalStringTable, &pack->globalStringTable, pack);
	ret->binary_block = TextParserBinaryBlock_CreateFromMemory_dbg(bsGetDataPtr(pack->bs), (bsGetBitLength(pack->bs) + 7) >> 3, false MEM_DBG_PARMS_CALL);
	return ret;
}

void PackedStructStreamDeserialize_dbg(PackedStructStream *pack, SerializablePackedStructStream *src MEM_DBG_PARMS)
{
	void *data;
	int data_size;
	assertmsg(!pack->bs,"Stream must not already be initialized");

	data = TextParserBinaryBlock_PutIntoMallocedBuffer_dbg(src->binary_block, NULL MEM_DBG_PARMS_CALL);
	data_size = TextParserBinaryBlock_GetSize(src->binary_block);

	pack->bs = initBitStream_dbg(NULL, data, data_size, Read, false, StructPackAllocator MEM_DBG_PARMS_CALL);
	bsSetUserData(pack->bs, pack);
	bsSetBitLength(pack->bs, data_size<<3);

	MEM_DBG_STRUCT_PARMS_INIT(pack);

	pack->method = src->method;
	pack->flags = src->flags;

	steaCopy(&pack->globalStringTable, &src->globalStringTable, pack);
}

void PackedStructStreamAppend_dbg(PackedStructStream *pack, SerializablePackedStructStream *src MEM_DBG_PARMS)
{
	void *data;
	int data_size;
	int i;
	assertmsg(!pack->bs,"Stream must not already be initialized");

	assertmsg(0,"This code is untested.");
	data = TextParserBinaryBlock_PutIntoMallocedBuffer_dbg(src->binary_block, NULL MEM_DBG_PARMS_CALL);
	data_size = TextParserBinaryBlock_GetSize(src->binary_block);

	pack->bs = initBitStream_dbg(NULL, NULL, 0, Write, false, StructPackAllocator MEM_DBG_PARMS_CALL);
	bsSetUserData(pack->bs, pack);

	MEM_DBG_STRUCT_PARMS_INIT(pack);

	bsWriteBitsArray(pack->bs,  data_size*8, data);

	pack->method = src->method;
	pack->flags = src->flags;

	for (i=0;i<eaSize(&src->globalStringTable);i++)
	{
		int global_index = steaPush(&pack->globalStringTable, src->globalStringTable[i], pack);
		devassert(global_index == i);
		verify(stashAddInt(pack->stGlobalStringTable, src->globalStringTable[i], global_index, false));
	}
}
