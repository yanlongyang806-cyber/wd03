#ifndef BITSTREAM_H
#define BITSTREAM_H
GCC_SYSTEM

typedef enum{
	Read,
	Write
} BitStreamMode;

typedef enum {
	BSE_NOERROR=0,
	BSE_OVERFLOW, // Buffer filled up
	BSE_OVERRUN, // Read past end of buffer
	BSE_TYPEMISMATCH, // Debug bit type mismatch
	BSE_COMPRESSION, // Misc compression errors (bad data, probably)
} BitStreamError;

/* Structure BitStreamCursor
 *	The bitstream cursor is used to point to locate a single bit in a bitstream.
 *	The cursor points at a position relative to a base position in a bitstream.
 *
 */
typedef struct{
	unsigned int	byte;	// points at the byte offset from bitstream base position.
	unsigned int	bit;	// points at the bit offset relative to the byte offset.
} BitStreamCursor;

typedef struct BitStream BitStream;
typedef int (*MemoryAllocator)(BitStream* bs);
typedef void (*DataTypeErrorCb)(char *buffer);
DataTypeErrorCb setDataTypeErrorCb(DataTypeErrorCb dtErrCb);

typedef struct BitStream BitStream;

void bsAssertOnErrors(int allow);

// Pass NULL to get one allocated
BitStream *initBitStream_dbg(BitStream* bs, unsigned char* buffer, unsigned int bufferSize, BitStreamMode initMode, unsigned int byteAligned, MemoryAllocator alloc MEM_DBG_PARMS);
#define initBitStream(bs, buffer, bufferSize, initMode, byteAligned, alloc) initBitStream_dbg(bs, buffer, bufferSize, initMode, byteAligned, alloc MEM_DBG_PARMS_INIT)

void cloneBitStream(BitStream* orig, BitStream* clone);

void bsSetUserData(BitStream *bs, void *userData);
void *bsGetUserData(BitStream *bs);

// For allocator callbacks
int bsGetMaxSize(BitStream *bs);
void *bsGetDataPtr(BitStream *bs);
void bsSetNewMaxSizeAndDataPtr(BitStream *bs, int maxSize, void *dataPtr);

int bsIsBad(BitStream *bs);

void bsSetByteAlignment(BitStream* bs, int align);
int bsGetByteAlignment(BitStream* bs);

void bsWriteBits(BitStream* bs, int numbits, unsigned int val);
unsigned int bsReadBits(BitStream* bs, int numbits);

void bsWriteBits64(BitStream* bs, int numbits, U64 val);
U64 bsReadBits64(BitStream* bs, int numbits);

int bsGetLengthWriteBits(BitStream* bs, int numbits, unsigned int val);

int bsWriteBitsArray(BitStream* bs, int numbits, const void* data);
int bsReadBitsArray(BitStream* bs, int numbits, void* data);

void bsWriteBitsPack(BitStream* bs, int minbits, unsigned int val);
int bsGetLengthWriteBitsPack(BitStream* bs, int minbits, unsigned int val);
unsigned int bsReadBitsPack(BitStream* bs,int minbits);

void bsWriteString(BitStream* bs, const char* str);

//reads from the BitStream, puts into the passed in EString (clearing it first)
//returns number of bytes read
int bsReadString(BitStream* bs, char **ppEString);

void bsWriteF32(BitStream* bs, float f);
float bsReadF32(BitStream* bs);


void bsAlignByte(BitStream* bs);
void bsRewind(BitStream* bs);
void bsSeekToEnd(BitStream* bs);
void bsChangeMode(BitStream* bs, BitStreamMode newMode);

unsigned int bsGetBitLength(BitStream* bs);
unsigned int bsGetLength(BitStream* bs);
unsigned int bsEnded(BitStream* bs);
void bsSetBitLength(BitStream *bs, unsigned int bitlength);

unsigned int bsGetCursorBitPosition(BitStream* bs);
void bsSetCursorBitPosition(BitStream* bs, unsigned int position);
void bsSetEndOfStreamPosition(BitStream *bs, unsigned int position);

void testBitStream();

void bsTypedWriteBits(BitStream* bs, int numbits, unsigned int val);
unsigned int bsTypedReadBits(BitStream* bs, int numbits);

void bsTypedWriteBits64(BitStream* bs, int numbits, U64 val);
U64 bsTypedReadBits64(BitStream* bs, int numbits);

int bsTypedWriteBitsArray(BitStream* bs, int numbits, const void* data);
int bsTypedReadBitsArray(BitStream* bs, int numbits, void* data);

void bsTypedWriteBitsPack(BitStream* bs, int minbits, unsigned int val);
int bsGetLengthTypedWriteBitsPack(BitStream* bs, int minbits, unsigned int val);
unsigned int bsTypedReadBitsPack(BitStream* bs,int minbits);

void bsTypedWriteString(BitStream* bs, const char* str);

//returns number of bytes read
int bsTypedReadString(BitStream* bs, char **ppEString);

void bsTypedWriteF32(BitStream* bs, float f);
float bsTypedReadF32(BitStream* bs);

int bsTypedAlignBitsArray(BitStream* bs);

#define bsWritePtr(bs, ptr) ((sizeof(void*)==4)?bsWriteBits(bs, 32, (S32)(intptr_t)ptr):bsWriteBits64(bs, 64, (U64)ptr))
#define bsReadPtr(bs)  ((void*)((sizeof(void*)==4)?bsReadBits(bs, 32):bsReadBits64(bs, 64)))


__forceinline static int count_bits(unsigned int val) // returns 1 at a minimum
{
	int bits = 1;
	while (1)
	{
		val >>= 1;
		if (!val) break;
		bits++;
	}
	return bits;
}

#ifdef BITSTREAM_DEBUG

#define bsWriteBits bsTypedWriteBits
#define bsReadBits bsTypedReadBits

#define bsWriteBits64 bsTypedWriteBits64
#define bsReadBits64 bsTypedReadBits64

#define bsWriteBitsArray bsTypedWriteBitsArray
#define bsReadBitsArray bsTypedReadBitsArray

#define bsWriteBitsPack bsTypedWriteBitsPack
#define bsReadBitsPack bsTypedReadBitsPack

#define bsWriteString bsTypedWriteString
#define bsReadStringAndLength bsTypedReadStringAndLength
#define bsReadString bsTypedReadString

#define bsWriteF32 bsTypedWriteF32
#define bsReadF32 bsTypedReadF32

#endif

#endif
