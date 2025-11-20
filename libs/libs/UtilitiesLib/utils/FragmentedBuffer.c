GCC_SYSTEM
#include "FragmentedBuffer.h"
#include "wininclude.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct FragmentedBufferChunk FragmentedBufferChunk;

typedef struct FragmentedBufferChunk {
	FragmentedBufferChunk*		next;
	U8							data[1000];
} FragmentedBufferChunk;

typedef struct FragmentedBuffer {
	U32							readerCount;

	struct {
		FragmentedBufferChunk*	head;
		FragmentedBufferChunk*	tail;
		U32						count;
	} chunks;
	
	union {
		BitBufferWriter			bb;
		U32						bytePos;
	};
	
	struct {
		U32						destroyed		: 1;
		U32						isBufferWriter	: 1;
	} flags;
} FragmentedBuffer;

typedef struct FragmentedBufferReader {
	FragmentedBuffer*			fb;
	FragmentedBufferChunk*		fbc;
	
	U32							bytesRemaining;
	U32							lastChunkBitsTotal;

	union {
		BitBufferReader			bb;
		U32						bytePos;
	};
	
	struct {
		U32						isBufferReader : 1;
	} flags;
} FragmentedBufferReader;

static CRITICAL_SECTION csFragmentedBuffer;

static void fbEnterCS(void){
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&csFragmentedBuffer);
	}
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&csFragmentedBuffer);
}

static void fbLeaveCS(void){
	LeaveCriticalSection(&csFragmentedBuffer);
}

S32 fbCreate(	FragmentedBuffer** fbOut,
				S32 isBufferWriter)
{
	FragmentedBuffer* fb;
	
	if(!fbOut){
		return 0;
	}
	
	fb = callocStruct(FragmentedBuffer);
	
	fb->flags.isBufferWriter = !!isBufferWriter;
	
	*fbOut = fb;
	
	return 1;
}

S32 fbDestroy(FragmentedBuffer** fbInOut){
	FragmentedBuffer* fb = SAFE_DEREF(fbInOut);

	if(!fb){
		return 0;
	}
	
	fbEnterCS();
	
	if(fb->readerCount){
		ASSERT_FALSE_AND_SET(fb->flags.destroyed);
		fbLeaveCS();
	}else{
		fbLeaveCS();
		
		PERFINFO_AUTO_START_FUNC();

		while(fb->chunks.head){
			FragmentedBufferChunk* fbcNext = fb->chunks.head->next;
			SAFE_FREE(fb->chunks.head);
			fb->chunks.head = fbcNext;
		}
		
		SAFE_FREE(fb);
		
		PERFINFO_AUTO_STOP();
	}
	
	*fbInOut = NULL;

	return 1;
}

S32 fbGetSizeAsBytes(	FragmentedBuffer* fb,
						U32* bytesCountOut)
{
	if(	!fb ||
		!bytesCountOut)
	{
		return 0;
	}
	
	if(!fb->chunks.count){
		*bytesCountOut = 0;
	}
	else if(fb->flags.isBufferWriter){
		*bytesCountOut =	(fb->chunks.count - 1) *
							sizeof(fb->chunks.head->data)
							+
							fb->bytePos;
	}else{
		*bytesCountOut =	(fb->chunks.count - 1) *
							sizeof(fb->chunks.head->data)
							+
							fb->bb.pos.byte
							+
							!!fb->bb.pos.bit;
	}

	return 1;
}

S32 fbGetSizeAsBits(FragmentedBuffer* fb,
					U32* bitCountOut)
{
	if(	!fb ||
		!bitCountOut)
	{
		return 0;
	}
	
	if(!fb->chunks.count){
		*bitCountOut = 0;
	}
	else if(fb->flags.isBufferWriter){
		*bitCountOut =	BITS_PER_BYTE *
						(	(fb->chunks.count - 1) *
							sizeof(fb->chunks.head->data)
							+
							fb->bytePos);
	}else{
		*bitCountOut =	BITS_PER_BYTE *
						(	(fb->chunks.count - 1) *
							sizeof(fb->chunks.head->data)
							+
							fb->bb.pos.byte)
						+
						fb->bb.pos.bit;
	}
	
	return 1;
}

static S32 fbChunkCreate(FragmentedBufferChunk** fbcOut){
	if(!fbcOut){
		return 0;
	}
	
	*fbcOut = callocStruct(FragmentedBufferChunk);
	
	return 1;
}

static S32 fbAddNewChunk(FragmentedBuffer* fb){
	FragmentedBufferChunk* fbc;
	
	if(!fbChunkCreate(&fbc)){
		return 0;
	}
	
	if(!fb->chunks.head){
		assert(!fb->chunks.tail);
		fb->chunks.head = fbc;
	}else{
		assert(fb->chunks.tail);
		
		fb->chunks.tail->next = fbc;
	}
	
	fb->chunks.tail = fbc;
	fb->chunks.count++;
	
	if(fb->flags.isBufferWriter){
		fb->bytePos = 0;
	}else{
		fb->bb.buffer = fbc->data;
		fb->bb.bitsTotal = sizeof(fbc->data) * BITS_PER_BYTE;
		fb->bb.pos.byte = 0;
		fb->bb.pos.bit = 0;
	}

	return 1;
}

S32 fbWriteBuffer(	FragmentedBuffer* fb,
					const U8* bytes,
					U32 byteCount)
{
	U32 byteCountRemaining;

	if(	!fb ||
		fb->readerCount ||
		!byteCount ||
		!fb->flags.isBufferWriter)
	{
		return 0;
	}
	
	if(	!fb->chunks.head &&
		!fbAddNewChunk(fb))
	{
		return 0;
	}
	
	byteCountRemaining = byteCount;

	while(byteCountRemaining){
		FragmentedBufferChunk*	fbc = fb->chunks.tail;
		const U32				chunkBytesRemaining = sizeof(fbc->data) - fb->bytePos;
		const U32				curByteCount = MIN(chunkBytesRemaining, byteCountRemaining);
		
		if(!chunkBytesRemaining){
			if(!fbAddNewChunk(fb)){
				return 0;
			}
			continue;
		}
		
		memcpy(	fbc->data + fb->bytePos,
				bytes,
				curByteCount);

		bytes += curByteCount;
		byteCountRemaining -= curByteCount;
		fb->bytePos += curByteCount;
	}
	
	return 1;
}

S32 fbWriteU32(	FragmentedBuffer* fb,
				U32 bitCount,
				U32 value)
{
	U32 bitsWritten = 0;

	if(	!fb ||
		fb->readerCount ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE ||
		fb->flags.isBufferWriter)
	{
		return 0;
	}
	
	if(	!fb->chunks.head &&
		!fbAddNewChunk(fb))
	{
		return 0;
	}
	
	while(1){
		bbWriteU32(	&fb->bb,
					bitCount,
					value,
					&bitsWritten);

		if(bitCount == bitsWritten){
			break;
		}
						
		if(!fbAddNewChunk(fb)){
			return 0;
		}
	}

	return 1;
}

S32 fbWriteU64(	FragmentedBuffer* fb,
				U32 bitCount,
				U64 value)
{
	U32 bitsWritten = 0;

	if(	!fb ||
		fb->readerCount ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE ||
		fb->flags.isBufferWriter)
	{
		return 0;
	}
	
	if(	!fb->chunks.head &&
		!fbAddNewChunk(fb))
	{
		return 0;
	}
	
	while(1){
		bbWriteU64(	&fb->bb,
					bitCount,
					value,
					&bitsWritten);

		if(bitCount == bitsWritten){
			break;
		}
						
		if(!fbAddNewChunk(fb)){
			return 0;
		}
	}

	return 1;
}

S32 fbWriteBit(	FragmentedBuffer* fb,
				U32 value)
{
	return fbWriteU32(fb, 1, value);
}

S32 fbWriteBytes(	FragmentedBuffer* fb,
					const U8* bytes,
					U32 byteCount)
{
	while(byteCount){
		if(!fbWriteU32(fb, BITS_PER_BYTE, bytes[0])){
			return 0;
		}
		bytes++;
		byteCount--;
	}
	return 1;
}

S32 fbWriteString(	FragmentedBuffer* fb,
					const char* str)
{
	if(!str){
		str = "";
	}
	
	if(!fbWriteU32(fb, sizeof(U32) * BITS_PER_BYTE, (U32)strlen(str))){
		return 0;
	}
	
	for(; *str; str++){
		if(!fbWriteU32(fb, BITS_PER_BYTE, (U8)*str)){
			return 0;
		}
	}
	
	return 1;
}

static void fbReaderStartChunk(	FragmentedBufferReader* fbr,
								FragmentedBufferChunk* fbc)
{
	fbr->fbc = fbc;
	
	if(fbc){
		if(fbr->flags.isBufferReader){
			fbr->bytePos = 0;
		}else{
			fbr->bb.buffer = fbc->data;
			fbr->bb.bitsTotal = fbc->next ?
									sizeof(fbc->data) * BITS_PER_BYTE :
									fbr->lastChunkBitsTotal;
			fbr->bb.pos.byte = 0;
			fbr->bb.pos.bit = 0;
		}
	}else{
		ZeroStruct(&fbr->bb);
	}
}

void fbReaderDetach(FragmentedBufferReader* fbr){
	U32 readerCount;
	S32 destroyed;

	if(!SAFE_MEMBER(fbr, fb)){
		return;
	}

	fbEnterCS();
	{
		assert(fbr->fb->readerCount);
		readerCount = --fbr->fb->readerCount;
		destroyed = fbr->fb->flags.destroyed;
	}
	fbLeaveCS();

	if(	!readerCount &&
		destroyed)
	{
		fbDestroy(&fbr->fb);
	}
	
	ZeroStruct(fbr);
}

void fbReaderAttach(FragmentedBufferReader* fbr,
					FragmentedBuffer* fb,
					S32 isBufferReader)
{
	fbReaderDetach(fbr);
	
	fbEnterCS();
	{
		fb->readerCount++;
	}
	fbLeaveCS();
	
	fbr->fb = fb;
	fbr->flags.isBufferReader = !!isBufferReader;
	if(fb->flags.isBufferWriter){
		fbr->lastChunkBitsTotal = fb->bytePos * BITS_PER_BYTE;
	}else{
		fbr->lastChunkBitsTotal =	fb->bb.pos.byte * BITS_PER_BYTE +
									fb->bb.pos.bit;
	}
	fbReaderStartChunk(fbr, fb->chunks.head);
	
	fbGetSizeAsBytes(fb, &fbr->bytesRemaining);
}

S32 fbReaderCreate(FragmentedBufferReader** fbrOut){
	FragmentedBufferReader* fbr;
	
	if(!fbrOut){
		return 0;
	}
	
	fbr = callocStruct(FragmentedBufferReader);
	
	*fbrOut = fbr;
	
	return 1;
}

S32 fbReaderDestroy(FragmentedBufferReader** fbrInOut){
	FragmentedBufferReader* fbr = SAFE_DEREF(fbrInOut);
	
	if(!fbr){
		return 0;
	}
	
	fbReaderDetach(fbr);

	SAFE_FREE(*fbrInOut);
	
	return 1;
}

S32 fbReaderGetSizeAsBytes(	FragmentedBufferReader* fbr,
							U32* bytesOut)
{
	return fbGetSizeAsBytes(SAFE_MEMBER(fbr, fb), bytesOut);
}

S32 fbReaderGetBytesRemaining(	FragmentedBufferReader* fbr,
								U32* bytesOut)
{
	if(	!fbr ||
		!bytesOut ||
		!fbr->flags.isBufferReader)
	{
		return 0;
	}
	
	*bytesOut = fbr->bytesRemaining;
	
	return 1;
}

S32 fbReadBuffer(	FragmentedBufferReader* fbr,
					U8* bytesOut,
					U32 byteCount)
{
	U32 byteCountRemaining;
	
	if(	!fbr ||
		!byteCount ||
		!fbr->flags.isBufferReader ||
		byteCount > fbr->bytesRemaining)
	{
		return 0;
	}
	
	byteCountRemaining = byteCount;
	
	while(byteCountRemaining){
		const U32 chunkBytesRemaining = sizeof(fbr->fbc->data) - fbr->bytePos;
		const U32 readableBytesRemaining = MIN(fbr->bytesRemaining, chunkBytesRemaining);
		const U32 curByteCount = MIN(byteCountRemaining, readableBytesRemaining);
		
		memcpy(	bytesOut,
				fbr->fbc->data + fbr->bytePos,
				curByteCount);
				
		bytesOut += curByteCount;
		byteCountRemaining -= curByteCount;
		fbr->bytePos += curByteCount;
		fbr->bytesRemaining -= curByteCount;
		
		if(fbr->bytePos == sizeof(fbr->fbc->data)){
			fbReaderStartChunk(fbr, fbr->fbc->next);
		}
	}
	
	return 1;
}

S32 bbReadU32(	BitBufferReader* b,
				U32 bitCount,
				U32* valueInOut,
				U32* bitsReadInOut)
{
	U32	value = 0;
	U32	bitsRead = 0;
	U32	bitsAvailable;
	
	if(	!b ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE)
	{
		return 0;
	}
	
	bitsAvailable = b->bitsTotal -
					b->pos.byte * BITS_PER_BYTE -
					b->pos.bit;
	
	if(bitsReadInOut){
		if(*bitsReadInOut){
			if(valueInOut){
				value = *valueInOut;
				assert(value < ((U32)1 << *bitsReadInOut));
			}
			assert(*bitsReadInOut < bitCount);
			bitCount -= *bitsReadInOut;
			bitsRead = *bitsReadInOut;
		}
	}
	else if(bitsAvailable < bitCount){
		return 0;
	}

	while(	bitsAvailable &&
			bitCount)
	{
		U32 curByteBits = BITS_PER_BYTE - b->pos.bit;
		U32 curBitCount = MIN(bitCount, curByteBits);
		U32	curByteMask = (1 << curBitCount) - 1;
		U32	curByte = (b->buffer[b->pos.byte] >> b->pos.bit) & curByteMask;
		
		value |= curByte << bitsRead;
		
		bitCount -= curBitCount;
		bitsRead += curBitCount;
		
		b->pos.bit += curBitCount;
		bitsAvailable -= curBitCount;
		
		if(b->pos.bit == BITS_PER_BYTE){
			b->pos.byte++;
			b->pos.bit = 0;
		}
	}
	
	if(bitsReadInOut){
		*bitsReadInOut = bitsRead;
	}
	
	if(valueInOut){
		*valueInOut = value;
	}
	
	return 1;
}

S32 bbReadU64(	BitBufferReader* b,
				U32 bitCount,
				U64* valueInOut,
				U32* bitsReadInOut)
{
	U64	value = 0;
	U32	bitsRead = 0;
	U32	bitsAvailable;
	
	if(	!b ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE)
	{
		return 0;
	}
	
	bitsAvailable = b->bitsTotal -
					b->pos.byte * BITS_PER_BYTE -
					b->pos.bit;
	
	if(bitsReadInOut){
		if(*bitsReadInOut){
			if(valueInOut){
				value = *valueInOut;
				assert(value < ((U64)1 << *bitsReadInOut));
			}
			assert(*bitsReadInOut < bitCount);
			bitCount -= *bitsReadInOut;
			bitsRead = *bitsReadInOut;
		}
	}
	else if(bitsAvailable < bitCount){
		return 0;
	}

	while(	bitsAvailable &&
			bitCount)
	{
		U32 curByteBits = BITS_PER_BYTE - b->pos.bit;
		U32 curBitCount = MIN(bitCount, curByteBits);
		U32	curByteMask = (1 << curBitCount) - 1;
		U64	curByte = (b->buffer[b->pos.byte] >> b->pos.bit) & curByteMask;
		
		value |= curByte << bitsRead;
		
		bitCount -= curBitCount;
		bitsRead += curBitCount;
		
		b->pos.bit += curBitCount;
		bitsAvailable -= curBitCount;
		
		if(b->pos.bit == BITS_PER_BYTE){
			b->pos.byte++;
			b->pos.bit = 0;
		}
	}
	
	if(bitsReadInOut){
		*bitsReadInOut = bitsRead;
	}
	
	if(valueInOut){
		*valueInOut = value;
	}
	
	return 1;
}

S32 bbReadBit(	BitBufferReader* b,
				U32* valueOut)
{
	return bbReadU32(b, 1, valueOut, NULL);
}

S32 bbWriteU32(	BitBufferWriter* b,
				U32 bitCount,
				U32 value,
				U32* bitsWrittenInOut)
{
	U32 bitsWritten = 0;
	U32 bitsAvailable;

	if(	!b ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE)
	{
		return 0;
	}
	
	bitsAvailable =	b->bitsTotal -
					b->pos.byte * BITS_PER_BYTE -
					b->pos.bit;

	if(bitsWrittenInOut){
		bitsWritten = *bitsWrittenInOut;
		assert(bitsWritten < bitCount);
		bitCount -= bitsWritten;
		value >>= bitsWritten;
	}
	else if(bitsAvailable < bitCount){
		return 0;
	}
	
	while(	bitsAvailable &&
			bitCount)
	{
		U32 curByteBits = BITS_PER_BYTE - b->pos.bit;
		U32 curBitCount = MIN(bitCount, curByteBits);
		U8	curByteMask = (1 << curBitCount) - 1;
		U8	curByte = value & curByteMask;
		
		b->buffer[b->pos.byte] =	(b->buffer[b->pos.byte] & (BIT(b->pos.bit) - 1)) |
									(curByte << b->pos.bit);
		
		bitCount -= curBitCount;
		value >>= curBitCount;
		
		b->pos.bit += curBitCount;
		bitsAvailable -= curBitCount;
		bitsWritten += curBitCount;
		
		if(b->pos.bit == BITS_PER_BYTE){
			b->pos.byte++;
			b->pos.bit = 0;
		}
	}
	
	if(bitsWrittenInOut){
		*bitsWrittenInOut = bitsWritten;
	}
	
	return 1;
}

S32 bbWriteU64(	BitBufferWriter* b,
				U32 bitCount,
				U64 value,
				U32* bitsWrittenInOut)
{
	U32 bitsWritten = 0;
	U32 bitsAvailable;

	if(	!b ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE)
	{
		return 0;
	}
	
	bitsAvailable =	b->bitsTotal -
					b->pos.byte * BITS_PER_BYTE -
					b->pos.bit;

	if(bitsWrittenInOut){
		bitsWritten = *bitsWrittenInOut;
		assert(bitsWritten < bitCount);
		bitCount -= bitsWritten;
		value >>= bitsWritten;
	}
	else if(bitsAvailable < bitCount){
		return 0;
	}
	
	while(	bitsAvailable &&
			bitCount)
	{
		U32 curByteBits = BITS_PER_BYTE - b->pos.bit;
		U32 curBitCount = MIN(bitCount, curByteBits);
		U8	curByteMask = (1 << curBitCount) - 1;
		U8	curByte = value & curByteMask;
		
		b->buffer[b->pos.byte] =	(b->buffer[b->pos.byte] & (BIT(b->pos.bit) - 1)) |
									(curByte << b->pos.bit);
		
		bitCount -= curBitCount;
		value >>= curBitCount;
		
		b->pos.bit += curBitCount;
		bitsAvailable -= curBitCount;
		bitsWritten += curBitCount;
		
		if(b->pos.bit == BITS_PER_BYTE){
			b->pos.byte++;
			b->pos.bit = 0;
		}
	}
	
	if(bitsWrittenInOut){
		*bitsWrittenInOut = bitsWritten;
	}
	
	return 1;
}

S32 bbWriteBit(	BitBufferWriter* b,
				U32 value)
{
	return bbWriteU32(b, 1, value, NULL);
}

S32 fbReadU32(	FragmentedBufferReader* fbr,
				U32 bitCount,
				U32* valueOut)
{
	U32 value = 0;
	U32 bitsRead = 0;
	
	if(	!fbr ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE ||
		fbr->flags.isBufferReader)
	{
		return 0;
	}
	
	while(1){
		if(!fbr->fbc){
			return 0;
		}

		bbReadU32(&fbr->bb, bitCount, &value, &bitsRead);
		
		if(bitsRead == bitCount){
			break;
		}else{
			fbReaderStartChunk(fbr, fbr->fbc->next);
		}
	}
	
	if(valueOut){
		*valueOut = value;
	}
	
	return 1;
}

S32 fbReadU64(	FragmentedBufferReader* fbr,
				U32 bitCount,
				U64* valueOut)
{
	U64 value = 0;
	U32 bitsRead = 0;
	
	if(	!fbr ||
		!bitCount ||
		bitCount > sizeof(value) * BITS_PER_BYTE ||
		fbr->flags.isBufferReader)
	{
		return 0;
	}
	
	while(1){
		if(!fbr->fbc){
			return 0;
		}

		bbReadU64(&fbr->bb, bitCount, &value, &bitsRead);
		
		if(bitsRead == bitCount){
			break;
		}else{
			fbReaderStartChunk(fbr, fbr->fbc->next);
		}
	}
	
	if(valueOut){
		*valueOut = value;
	}
	
	return 1;
}

S32 fbReadBit(	FragmentedBufferReader* fbr,
				U32* valueOut)
{
	return fbReadU32(fbr, 1, valueOut);
}

S32 fbReadBytes(FragmentedBufferReader* fbr,
				U8* bytesOut,
				U32 byteCount)
{
	while(byteCount--){
		U32 byte;
		if(!fbReadU32(fbr, BITS_PER_BYTE, &byte)){
			return 0;
		}
		if(bytesOut){
			*bytesOut++ = byte;
		}
	}
	return 1;
}

S32 fbReadString(	FragmentedBufferReader* fbr,
					char* strOut,
					U32 strOutSize)
{
	U32 strLen;
	
	if(!fbReadU32(fbr, 32, &strLen)){
		return 0;
	}
	
	while(strLen--){
		U32 byte;
		
		if(!fbReadU32(fbr, BITS_PER_BYTE, &byte)){
			return 0;
		}
		
		if(	strOut &&
			strOutSize > 1)
		{
			*strOut++ = (U8)byte;
			strOutSize--;
		}
	}

	if(strOut){
		*strOut = 0;
	}

	return 1;
}

