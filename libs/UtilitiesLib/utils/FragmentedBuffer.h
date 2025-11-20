#pragma once
GCC_SYSTEM

typedef struct FragmentedBuffer			FragmentedBuffer;
typedef struct FragmentedBufferReader	FragmentedBufferReader;

typedef struct BitBufferPos {
	U32							byte;
	U32 						bit;
} BitBufferPos;

typedef struct BitBufferReader {
	const U8*		buffer;
	U32				bitsTotal;
	BitBufferPos	pos;
} BitBufferReader;

typedef struct BitBufferWriter {
	U8*				buffer;
	U32				bitsTotal;
	BitBufferPos	pos;
} BitBufferWriter;

// BitBufferReader/Writer.

#define BB_WRITER_INIT(b, bufferParam, posBitsParam){	\
	(b).buffer = bufferParam;							\
	(b).bitsTotal = sizeof(bufferParam) * 8;			\
	(b).pos.byte = (posBitsParam) / 8;					\
	(b).pos.bit = (posBitsParam) % 8;					\
}

#define BB_READER_INIT(b, bufferParam, bitsTotalParam, posBitsParam){	\
	(b).buffer = bufferParam;											\
	assert(bitsTotalParam <= sizeof(bufferParam) * 8);					\
	(b).bitsTotal = bitsTotalParam;										\
	(b).pos.byte = (posBitsParam) / 8;									\
	(b).pos.bit = (posBitsParam) % 8;									\
}

S32 bbReadU32(	BitBufferReader* b,
				U32 bitCount,
				U32* valueInOut,
				U32* bitsReadInOut);

S32 bbReadU64(	BitBufferReader* b,
				U32 bitCount,
				U64* valueInOut,
				U32* bitsReadInOut);

S32 bbReadBit(	BitBufferReader* b,
				U32* valueOut);

S32 bbWriteU32(	BitBufferWriter* b,
				U32 bitCount,
				U32 value,
				U32* bitsWrittenInOut);

S32 bbWriteU64(	BitBufferWriter* b,
				U32 bitCount,
				U64 value,
				U32* bitsWrittenInOut);

S32 bbWriteBit(	BitBufferWriter* b,
				U32 value);

// FragmentedBuffer.

S32		fbCreate(	FragmentedBuffer** fbOut,
					S32 isBufferWriter);

S32		fbDestroy(FragmentedBuffer** fbInOut);

S32		fbGetSizeAsBytes(	FragmentedBuffer* fb,
							U32* byteCountOut);

S32		fbGetSizeAsBits(FragmentedBuffer* fb,
						U32* bitCountOut);
						
S32		fbWriteBuffer(	FragmentedBuffer* fb,
						const U8* bytes,
						U32 byteCount);

S32		fbWriteU32(	FragmentedBuffer* fb,
					U32 bitCount,
					U32 value);

S32		fbWriteU64(	FragmentedBuffer* fb,
					U32 bitCount,
					U64 value);
					
S32		fbWriteBit(	FragmentedBuffer* fb,
					U32 value);

S32		fbWriteBytes(	FragmentedBuffer* fb,
						const U8* bytes,
						U32 byteCount);
						
S32		fbWriteString(	FragmentedBuffer* fb,
						const char* str);
						
// FragmentedBufferReader.

S32		fbReaderCreate(FragmentedBufferReader** fbrOut);

S32		fbReaderDestroy(FragmentedBufferReader** fbrInOut);

void	fbReaderAttach(	FragmentedBufferReader* fbr,
						FragmentedBuffer* fb,
						S32 isBufferReader);

void	fbReaderDetach(FragmentedBufferReader* fbr);

S32		fbReaderGetSizeAsBytes(	FragmentedBufferReader* fbr,
								U32* bytesOut);

S32		fbReaderGetBytesRemaining(	FragmentedBufferReader* fbr,
									U32* bytesOut);

S32		fbReadBuffer(	FragmentedBufferReader* fbr,
						U8* bytesOut,
						U32 byteCount);

S32		fbReadU32(	FragmentedBufferReader* fbr,
					U32 bitCount,
					U32* valueOut);
					
S32		fbReadU64(	FragmentedBufferReader* fbr,
					U32 bitCount,
					U64* valueOut);
					
S32		fbReadBit(	FragmentedBufferReader* fbr,
					U32* valueOut);
					
S32		fbReadBytes(FragmentedBufferReader* fbr,
					U8* bytesOut,
					U32 byteCount);
					
S32		fbReadString(	FragmentedBufferReader* fbr,
						char* strOut,
						U32 strOutSize);





						



