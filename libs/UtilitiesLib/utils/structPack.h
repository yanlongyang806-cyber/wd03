#pragma once
GCC_SYSTEM

typedef struct StashTableImp*			StashTable;
typedef struct BitStream BitStream;
typedef struct TextParserBinaryBlock TextParserBinaryBlock;

AUTO_ENUM;
typedef enum StructPackMethod {
	STRUCT_PACK_BITPACK,
	STRUCT_PACK_ZIP,
	STRUCT_PACK_BITPACK_LARGE_STRINGTABLE,
	STRUCT_PACK_MAX_METHODS,
} StructPackMethod;

AUTO_ENUM;
typedef	enum StructPackFlags
{
	STRUCT_PACK_DEFAULT = 0,
	STRUCT_PACK_WRITE_CRCS = 1 << 0, // Save CRCs on write
	STRUCT_PACK_CHECK_CRCS = 1 << 1, // Check CRCs on load
} StructPackFlags;

typedef struct PackedStructStream {
	BitStream *bs;
	StructPackMethod method;
	StructPackFlags flags;

	const char **globalStringTable; // String table for the whole packed stream, all pooled strings
	StashTable stGlobalStringTable;

	// String table for the struct being packed or unpacked - index into globalStringTable
	// These two are pushed and popped on the stack to allow recursive unpack calls
	int *structStringTable; // Used when packing and unpacking
	int effStringTableSize; // Used when unpacking

	// this declares debugging members
	MEM_DBG_STRUCT_PARMS
} PackedStructStream;

AUTO_STRUCT;
typedef struct SerializablePackedStructStream {
	const char **globalStringTable; AST( POOL_STRING )
	StructPackMethod method;
	StructPackFlags flags;
	TextParserBinaryBlock *binary_block;
} SerializablePackedStructStream;


// Notes:
//  Bitpacking is the generally most efficient in size, and loses very little
//    efficiency when packing lots of small structs vs one large struct.
//  Zipping is slow, but may be more efficient on a single large structure
//  Bitpacking does not do anything smart with MultiVals and default values yet
//  functioncalls and multivals are not doing anything smart with the string table
//  For best string packing results, do not use CURRENTFILE_NON_POOLED

U32 StructPack_dbg(ParseTable pti[], const void *structptr, PackedStructStream *pack MEM_DBG_PARMS);
#define StructPack(pti, structptr, pack) StructPack_dbg(pti, structptr, pack MEM_DBG_PARMS_INIT)
void *StructUnpack(ParseTable pti[], PackedStructStream *pack, U32 index);

bool StructBitpackSub(ParseTable pti[], const void *structptr, PackedStructStream *pack);
void StructUnbitpackSub(ParseTable pti[], void *structptr, PackedStructStream *pack);

bool StructBitpackArraySub(ParseTable tpi[], int column, const void *structptr, PackedStructStream *pack, int numelems);
void StructUnbitpackArraySub(ParseTable tpi[], int column, void *structptr, PackedStructStream *pack, int numelems);

#define PackedStructStreamInit(pack, method) PackedStructStreamInit_dbg(pack, method MEM_DBG_PARMS_INIT)
void PackedStructStreamInit_dbg(SA_PARAM_NN_VALID PackedStructStream *pack, StructPackMethod method MEM_DBG_PARMS);
void PackedStructStreamDeinit(SA_PARAM_OP_VALID PackedStructStream *pack);
U32 PackedStructStreamGetSize(PackedStructStream *pack);
void PackedStructStreamFinalize(PackedStructStream *pack); // Reallocs buffer to hold exactly what is in there currently

void StructBitpackPooledStringSub(const char *str, PackedStructStream *pack);
const char *StructUnbitpackPooledStringSub(PackedStructStream *pack);


// Utilities for converting to a form which can be serialized via TextParser
typedef struct SerializablePackedStructStream SerializablePackedStructStream;
extern ParseTable parse_SerializablePackedStructStream[];
#define TYPE_parse_SerializablePackedStructStream SerializablePackedStructStream

#define PackedStructStreamSerialize(pack) PackedStructStreamSerialize_dbg(pack MEM_DBG_PARMS_INIT)
SerializablePackedStructStream *PackedStructStreamSerialize_dbg(PackedStructStream *pack MEM_DBG_PARMS);

#define PackedStructStreamDeserialize(pack, src) PackedStructStreamDeserialize_dbg(pack, src MEM_DBG_PARMS_INIT)
void PackedStructStreamDeserialize_dbg(PackedStructStream *pack, SerializablePackedStructStream *src MEM_DBG_PARMS);
// takes a serialized stream, and creates a writeable stream from it
#define PackedStructStreamAppend(pack, src) PackedStructStreamAppend_dbg(pack, src MEM_DBG_PARMS_INIT)
void PackedStructStreamAppend_dbg(PackedStructStream *pack, SerializablePackedStructStream *src MEM_DBG_PARMS);

#define PackedStructStreamCopyStruct(uFileOffsetSrc, uSize, dst, src) PackedStructStreamCopyStruct_dbg(uFileOffsetSrc, uSize, dst, src MEM_DBG_PARMS_INIT)
U32 PackedStructStreamCopyStruct_dbg(U32 uFileOffsetSrc, U32 uSize, PackedStructStream *dst, PackedStructStream const *src MEM_DBG_PARMS);

