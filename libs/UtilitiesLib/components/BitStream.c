/* File BitStream.c
 *
 * FIXME!!!
 *	Stop tracking bitstream size and bitlength independently of the cursor.
 *	This is error-prone.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "BitStream.h"
#include "utils.h"


#include "estring.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/* Structure BitStream
 */
typedef struct BitStream{
	BitStreamMode	mode;
	BitStreamCursor cursor;

	unsigned char*	data;		// current contents of the bitstream
	unsigned int	size;		// current size of the bitstream stored in "data" (in bytes)
	unsigned int	maxSize;	// maximum allowable size of the bitstream (in bytes)
	unsigned int	bitLength;	// How many bits are there in this stream total? (derived from cursor position when appropriate)
	MemoryAllocator memAllocator;
	void*			userData;
	unsigned int	byteAlignedMode;
	BitStreamError	errorFlags; // Set to non-zero if an error occurs in the bitsteam, must be checked by the caller
} BitStream;


#define ROUND_BITS_UP(x) ((x + 7) & ~7)

bool g_bAssertOnBitStreamError = false;
// Assert macro to use in the case where asserts are handly gracefully
#define BSASSERT(exp) assert(!g_bAssertOnBitStreamError || (exp))

void bsAssertOnErrors(int allow) {
	g_bAssertOnBitStreamError = allow?true:false;
}

int bsIsBad(BitStream *bs) {
	return bs->errorFlags;
}

void bsSetUserData(BitStream *bs, void *userData)
{
	bs->userData = userData;
}

void *bsGetUserData(BitStream *bs)
{
	return bs->userData;
}

int bsGetMaxSize(BitStream *bs)
{
	return bs->maxSize;
}

void *bsGetDataPtr(BitStream *bs)
{
	return bs->data;
}

void bsSetNewMaxSizeAndDataPtr(BitStream *bs, int maxSize, void *dataPtr)
{
	bs->maxSize = maxSize;
	bs->data = dataPtr;
}


BitStream *initBitStream_dbg(BitStream* bs, unsigned char* buffer, unsigned int bufferSize, BitStreamMode initMode, unsigned int byteAligned, MemoryAllocator alloc MEM_DBG_PARMS){
	if (!bs) {
		bs = scalloc(sizeof(*bs), 1);
	} else {
		memset(bs, 0, sizeof(BitStream));
	}
	bs->data = buffer;
	bs->maxSize = bufferSize;
	bs->memAllocator = alloc;
	bs->mode = initMode;
	bs->byteAlignedMode=byteAligned;
	return bs;
}

void cloneBitStream(BitStream* orig, BitStream* clone){
	memcpy(clone, orig, sizeof(BitStream));
}

void bsSetByteAlignment(BitStream* bs, int align) {
	bs->byteAlignedMode = align;
	if (align)
		bsAlignByte(bs);
}

int bsGetByteAlignment(BitStream* bs) {
	return bs->byteAlignedMode;
}


void bsWriteBits(BitStream* bs, int numbits, unsigned int val){
	int	outBits, bits;

	if (bs->errorFlags) {
		// Can't write to a stream in an erroneous state!
		return;
	}

	if (bs->byteAlignedMode) {
		if (numbits!=32) { // Mask out bits that we don't want sent
			int mask = ((1 << numbits)-1);
			val = val & mask;
		}
		numbits = ROUND_BITS_UP(numbits);
		assert(bs->cursor.bit==0);
	}
	assert(numbits<=32);

	// Make sure the given bitstream is in write mode.
	if(bs->mode != Write){
		assert(0);
		return;
	}

	if(bs->size >= bs->maxSize){
		if(bs->memAllocator){
			bs->memAllocator(bs);
		} else {
			// Apparently this should never happen
			BSASSERT(!"No memory allocator and we need more");
			bs->errorFlags = BSE_OVERFLOW;
			return;
		}
	}

	// Insert the bits now.
	while(1){
		int mask;
		// Calculate the number of bits to be outputted this iteration.
		//	How many bits until the current byte is filled?
		outBits = 8 - bs->cursor.bit;

		//	Is it possible to send all remaining bits?  Or do we have to settle for filling this
		//	byte only?
		bits = (numbits < outBits) ? numbits : outBits;

		// Mask out data that is not wanted, then put it at the correct bit location
		// as specified by the cursor.
		mask = ((1 << bits)-1) << bs->cursor.bit;
		bs->data[bs->cursor.byte] = ((val << bs->cursor.bit) & mask ) | (bs->data[bs->cursor.byte] & ~mask);

		val >>= bits;
		bs->cursor.bit += bits;

		// Normalize cursor.
		if (bs->cursor.bit >= 8){
			bs->cursor.bit = 0;
			bs->cursor.byte++;
			if (bs->size<bs->cursor.byte)  // Only increase size if cursor matches it, we don't want to increase the size when we're overwriting data in the middle!
				bs->size++;
			assert(bs->size>=bs->cursor.byte);

			if(bs->size >= bs->maxSize){
				if(bs->memAllocator){
					bs->memAllocator(bs);
				} else {
					// Apparently this should never happen
					BSASSERT(!"No memory allocator and we need more");
					bs->errorFlags = BSE_OVERFLOW;
					break;
				}
			}
		}
		numbits -= bits;
		if (numbits <= 0)
			break;
	}

	// Update the recorded bitlength of the stream.
	{
		unsigned int cursorBitPos;
		cursorBitPos = bsGetCursorBitPosition(bs);
		if(bs->bitLength < cursorBitPos)
			bs->bitLength = cursorBitPos;
	}

}



void bsWriteBits64(BitStream* bs, int numbits, U64 val){
	int	outBits, bits;

	if (bs->errorFlags) {
		// Can't write to a stream in an erroneous state!
		return;
	}

	if (bs->byteAlignedMode) {
		if (numbits!=64) { // Mask out bits that we don't want sent
			S64 mask = ((1 << numbits)-1);
			val = val & mask;
		}
		numbits = ROUND_BITS_UP(numbits);
		assert(bs->cursor.bit==0);
	}
	assert(numbits<=64);

	// Make sure the given bitstream is in write mode.
	if(bs->mode != Write){
		assert(0);
		return;
	}

	// Insert the bits now.
	while(1){
		int mask;
		// Calculate the number of bits to be outputted this iteration.
		//	How many bits until the current byte is filled?
		outBits = 8 - bs->cursor.bit;

		//	Is it possible to send all remaining bits?  Or do we have to settle for filling this
		//	byte only?
		bits = (numbits < outBits) ? numbits : outBits;

		// Mask out data that is not wanted, then put it at the correct bit location
		// as specified by the cursor.
		mask = ((1 << bits)-1) << bs->cursor.bit;
		bs->data[bs->cursor.byte] = ((val << bs->cursor.bit) & mask) | (bs->data[bs->cursor.byte] & ~mask);

		val >>= bits;
		bs->cursor.bit += bits;

		// Normalize cursor.
		if (bs->cursor.bit >= 8){
			bs->cursor.bit = 0;
			bs->cursor.byte++;
			if (bs->size<bs->cursor.byte)  // Only increase size if cursor matches it, we don't want to increase the size when we're overwriting data in the middle!
				bs->size++;
			assert(bs->size>=bs->cursor.byte);

			if(bs->size >= bs->maxSize){
				if(bs->memAllocator){
					bs->memAllocator(bs);
				} else {
					// Apparently this should never happen
					BSASSERT(!"No memory allocator and we need more");
					bs->errorFlags = BSE_OVERFLOW;
					break;
				}
			}
		}
		numbits -= bits;
		if (numbits <= 0)
			break;
	}

	// Update the recorded bitlength of the stream.
	{
		unsigned int cursorBitPos;
		cursorBitPos = bsGetCursorBitPosition(bs);
		if(bs->bitLength < cursorBitPos)
			bs->bitLength = cursorBitPos;
	}

}

int bsGetLengthWriteBits(BitStream* bs, int numbits, unsigned int val){
	int	outBits, bits;
	int ret=0;

	if (bs->errorFlags) {
		// Can't write to a stream in an erroneous state!
		return 0;
	}

	if (bs->byteAlignedMode) {
		if (numbits!=32) { // Mask out bits that we don't want sent
			int mask = ((1 << numbits)-1);
			val = val & mask;
		}
		numbits = ROUND_BITS_UP(numbits);
		assert(bs->cursor.bit==0);
	}
	assert(numbits<=32);

	// Make sure the given bitstream is in write mode.
	if(bs->mode != Write){
		assert(0);
		return 0;
	}

	// Insert the bits now.
	while(1){
		// Calculate the number of bits to be outputted this iteration.
		//	How many bits until the current byte is filled?
		outBits = 8 - bs->cursor.bit;

		//	Is it possible to send all remaining bits?  Or do we have to settle for filling this
		//	byte only?
		bits = (numbits < outBits) ? numbits : outBits;

		// Mask out data that is not wanted, then put it at the correct bit location
		// as specified by the cursor.
		//NOT HERE bs->data[bs->cursor.byte] |= (val & ((1 << bits)-1)) << bs->cursor.bit;

		val >>= bits;
		//NOT HERE bs->cursor.bit += bits;
		ret += bits;

		//NOT HERE Normalize cursor.
		numbits -= bits;
		if (numbits <= 0)
			break;
	}

	//NOT HERE Update the recorded bitlength of the stream.
	return ret;
}

unsigned int bsReadBits(BitStream* bs, int numbits){
	unsigned int		val = 0;
	int		in_bits, curr_shift=0, bits;

	if (bs->errorFlags) {
		return -1;
	}

	assert(numbits <= 32);

	if (bs->byteAlignedMode) {
		numbits = ROUND_BITS_UP(numbits);
	}

	if ((bs->cursor.byte<<3) + bs->cursor.bit + numbits > bs->bitLength) {
		BSASSERT(!"Read off of the end of the bitstream!");
		bs->errorFlags = BSE_OVERRUN;
		return -1;
	}

	while(1)
	{
		in_bits = 8 - bs->cursor.bit;
		bits = (numbits < in_bits) ? numbits : in_bits;
		val |= ((bs->data[bs->cursor.byte] >> bs->cursor.bit) & ((1 << bits)-1)) << curr_shift;
		bs->cursor.bit += bits;
		if (bs->cursor.bit >= 8)
		{
			bs->cursor.bit = 0;
			bs->cursor.byte++;
		}
		curr_shift += bits;
		numbits -= bits;
		if (numbits <= 0)
			break;
	}
	return val;
}


U64 bsReadBits64(BitStream* bs, int numbits){
	U64		val = 0;
	int		in_bits, curr_shift=0, bits;

	if (bs->errorFlags) {
		return -1;
	}

	assert(numbits <= 64);

	if (bs->byteAlignedMode) {
		numbits = ROUND_BITS_UP(numbits);
	}

	if ((bs->cursor.byte<<3) + bs->cursor.bit + numbits > bs->bitLength) {
		BSASSERT(!"Read off of the end of the bitstream!");
		bs->errorFlags = BSE_OVERRUN;
		return -1;
	}

	while(1)
	{
		in_bits = 8 - bs->cursor.bit;
		bits = (numbits < in_bits) ? numbits : in_bits;
		val |= (U64)((bs->data[bs->cursor.byte] >> bs->cursor.bit) & ((1 << bits)-1)) << curr_shift;
		bs->cursor.bit += bits;
		if (bs->cursor.bit >= 8)
		{
			bs->cursor.bit = 0;
			bs->cursor.byte++;
		}
		curr_shift += bits;
		numbits -= bits;
		if (numbits <= 0)
			break;
	}
	return val;
}


int bsWriteBitsArray(BitStream* bs, int numbits, const void* data){
	int	fullBytes;
	//int bytesAdded;
	//int bitsAdded;

	// Note: this always aligns to bytes currently, if that changes, we'll need to add a case for byteAligned here

	// Make sure the given bitstream is in write mode.
	if(bs->mode != Write){
		assert(0);
		return 0;
	}

	if (bs->errorFlags) {
		return 0;
	}

	// How many full bytes are being sent?
	fullBytes = (numbits + 7) >> 3;

	// Align the bitstream cursor with the next byte.
	// This prepares the bitstream for a direct memory copy, avoiding
	// any bit alignment issues.
	if(bs->cursor.bit){
		bs->bitLength += 8 - bs->cursor.bit;
		bs->cursor.bit = 0;
		bs->cursor.byte++;
		if (bs->size<bs->cursor.byte)  // Only increase size if cursor matches it, we don't want to increase the size when we're overwriting data in the middle!
			bs->size++;
		assert(bs->size>=bs->cursor.byte);
	}

	// Make sure there is enough room in the bitstream buffer to hold the
	// incoming data.
	while((bs->cursor.byte + fullBytes) >= bs->maxSize){
		if(bs->memAllocator){
			bs->memAllocator(bs);
		}else {
			BSASSERT(!"No memory allocator and we need more");
			bs->errorFlags = BSE_OVERFLOW;
			return 0;
		}
	}

	// Copy the given data into the bitstream.
	memcpy(&bs->data[bs->cursor.byte], data, fullBytes);

	// Update bitstream cursor.
	bs->size += fullBytes;

	// setting the cursor like this here is fine, since it was aligned above anyway, and will be aligned again if this is called again
	bs->cursor.byte += numbits/8;
	bs->cursor.bit = numbits%8;
	bs->bitLength += numbits;

	return 1;
}

int bsReadBitsArray(BitStream* bs, int numbits, void* data){
	int fullBytes;

	// Note: bsWriteBitsArray always aligns to bytes currently, if that changes, we'll need to add a case for byteAligned here

	// How many full bytes are being retrieved?
	fullBytes = (numbits + 7)/8;

	if(bs->cursor.bit){ // align to byte
		bs->cursor.bit = 0;
		bs->cursor.byte++;
	}

	if ((bs->cursor.byte<<3) + bs->cursor.bit + numbits > bs->bitLength) {
		BSASSERT(!"Read off of the end of the bitstream!");
		bs->errorFlags = BSE_OVERRUN;
		return 0;
	}

	memcpy(data, &bs->data[bs->cursor.byte], fullBytes);
	bs->cursor.byte += numbits/8;
	bs->cursor.bit = numbits%8;

	return 1;
}

// measure how the packed bits stuff is doing
unsigned int g_packetsizes_one[33];
unsigned int g_packetsizes_success[33];
unsigned int g_packetsizes_failed[33];

void bsWriteBitsPack(BitStream* bs, int minbits, unsigned int val){
	unsigned int bitmask;
	int success = 1;
	int one = minbits == 1;
	int measured_bits = count_bits(val);


	if (bs->byteAlignedMode) {
		bsWriteBits(bs, 32, val);
		return;
	}
	if (bs->errorFlags) {
		return;
	}

	for(;;)
	{
		// Produce a minbits long mask that contains all 1's
		bitmask = (1 << minbits)-1;

		// If the value to be written can be represented by minbits...
		if (val < bitmask || minbits >= 32)
		{
			// Write the value.
			bsWriteBits(bs,minbits,val);
			if (success)
				g_packetsizes_success[measured_bits]++;
			else
				g_packetsizes_failed[measured_bits]++;
			if (one)
				g_packetsizes_one[measured_bits]++;
			break;
		}
		bsWriteBits(bs,minbits,bitmask);
		minbits <<= 1;
		if (minbits > 32)
			minbits = 32;
		val -= bitmask;
		success = 0;
	}
}

int bsGetLengthWriteBitsPack(BitStream* bs, int minbits, unsigned int val){
	unsigned int bitmask;
	int ret=0;

	if (bs->byteAlignedMode) {
		return bsGetLengthWriteBits(bs, 32, val);
	}
	if (bs->errorFlags) {
		return 0;
	}

	for(;;)
	{
		// Produce a minbits long mask that contains all 1's
		bitmask = (1 << minbits)-1;

		// If the value to be written can be represented by minbits...
		if (val < bitmask || minbits >= 32)
		{
			// Write the value.
			ret += bsGetLengthWriteBits(bs,minbits,val);
			break;
		}
		ret += bsGetLengthWriteBits(bs,minbits,bitmask);
		minbits <<= 1;
		if (minbits > 32)
			minbits = 32;
		val -= bitmask;
	}
	return ret;
}


unsigned int bsReadBitsPack(BitStream* bs,int minbits){
	unsigned int val, base=0;
	unsigned int bitmask;
	int success = 1;
	int one = minbits == 1;

	if (bs->byteAlignedMode) {
		return bsReadBits(bs, 32);
	}

	if (bs->errorFlags) {
		return -1;
	}

	for(;;)
	{
		val = bsReadBits(bs,minbits);
		if (bs->errorFlags)
			return -1;
		bitmask = (1 << minbits)-1;
		if (val < bitmask || minbits == 32)
		{
			int measured_bits = count_bits(val+base);
			if (success)
				g_packetsizes_success[measured_bits]++;
			else
				g_packetsizes_failed[measured_bits]++;
			if (one)
				g_packetsizes_one[measured_bits]++;
			return val + base;
		}
		base += bitmask;
		minbits <<= 1;
		if (minbits > 32)
			minbits = 32;
		success = 0;
	}
	return val + base;
}

void bsWriteString(BitStream* bs, const char* str){
	U8		*data,c;
	U32		shift,len=0,maxsize,orig_size;

	if (bs->byteAlignedMode) {
		assert(bs->cursor.bit==0);
	}

	shift = bs->cursor.bit;
	data = bs->data + bs->cursor.byte;
	orig_size = bs->size;
	maxsize = bs->maxSize - orig_size;
	do
	{
		if(++len >= maxsize)
		{
			if(bs->memAllocator){
				bs->memAllocator(bs);
			}else {
				BSASSERT(!"No memory allocator and we need more");
				bs->errorFlags = BSE_OVERFLOW;
				return;
			}
			maxsize = bs->maxSize - orig_size;
			data = bs->data + bs->cursor.byte;// + len - 1;
		}
		c = str[len-1];
		data[len-1] = (c << shift) | (data[len-1] & ((1 << shift)-1));
		data[len] = c >> (8-shift);
	} while (c);
	bs->size = len + orig_size;
	bs->cursor.byte += len;
	bs->bitLength = bsGetCursorBitPosition(bs);
}

int bsReadString(BitStream* bs, char **ppEString)
{
	static char *null_string = "(null)";
	int			i,shift = bs->cursor.bit;
	U8			*data;


	if ((bs->cursor.byte << 3) + bs->cursor.bit > (bs->bitLength)) {
		BSASSERT(!"Read off of the end of the bitstream!");
		bs->errorFlags = BSE_OVERRUN;
		estrCopy2(ppEString, null_string);
		return 0;
	}

	estrClear(ppEString);


	data = bs->data + bs->cursor.byte;
	for(i=0;;i++)
	{
		char tempChar;
		tempChar = *data++ >> shift;
		tempChar |= *data << (8 - shift);
		
		if (! tempChar)
			break;

		estrConcatChar(ppEString, tempChar);

		if ((bs->cursor.byte << 3) + bs->cursor.bit + (i<<3) > (bs->bitLength)) {
			BSASSERT(!"Read off of the end of the bitstream!");
			bs->errorFlags = BSE_OVERRUN;
			estrCopy2(ppEString, null_string);
			return 0;
		}
	}
	bs->cursor.byte += i+1;
	return estrLength(ppEString);
}

void bsWriteF32(BitStream* bs, float f){
	bsWriteBits(bs,32,* ((int *)&f));
}

float bsReadF32(BitStream* bs){
	unsigned int val;

	val = bsReadBits(bs,32);
	return *((float *) &val);
}

void bsAlignByte(BitStream* bs){
	if(bs->cursor.bit){
		int bitsforward = 8 - bs->cursor.bit;
		bs->cursor.byte++;
		if (bs->mode == Write) {
			// This was broken before.  Not 100% sure it's right now [RMARR - 3/6/12]
			if (bsGetCursorBitPosition(bs)+bitsforward > bs->bitLength) {  // Only increase size if cursor matches it, we don't want to increase the size when we're overwriting data in the middle!
				// clear the rest of the bits, just so we write consistent things to the file
				int mask = ((1 << bitsforward)-1) << bs->cursor.bit;
				bs->data[bs->cursor.byte-1] &= ~mask;

				bs->size = bs->cursor.byte;
				devassert(bs->size <= bs->maxSize);
				bs->bitLength += bitsforward;
			}
		}
		bs->cursor.bit = 0;
	}
}

void bsRewind(BitStream* bs){
	bs->cursor.byte = 0;
	bs->cursor.bit = 0;
	bs->errorFlags = BSE_NOERROR;
}

void bsSeekToEnd(BitStream* bs)
{
	bsSetCursorBitPosition(bs, bsGetBitLength(bs));
}

void bsChangeMode(BitStream* bs, BitStreamMode newMode)
{
	if (newMode == Write)
		bsSeekToEnd(bs);
	else
		bsRewind(bs);
	bs->mode = newMode;
}

/* Function bsGetBitLength()
 *	Grabs the total number of bits currently held in the stream.
 *
 *	BitStream::bitLength is used to explicitly track the number of
 *	bits in the bitstream.  This is required because there would be no
 *	other way to determine where the data really ends in the bitstream.
 *	bsEnded() needs this value to determine if the end of the stream
 *	has been reached.
 */
unsigned int bsGetBitLength(BitStream* bs){
	return bs->bitLength;
}

unsigned int bsGetLength(BitStream* bs){
	return ((bs->cursor.byte << 3) + bs->cursor.bit) >> 3;
}

void bsSetBitLength(BitStream *bs, unsigned int bitlength)
{
	assert(bitlength <= (bs->maxSize << 3));
	bs->bitLength = bitlength;
}


unsigned int bsEnded(BitStream* bs){
	// When a bitstream is being written, testing for end-of-stream does not work.
	//JE: Removed this, pktEnd call in pktWrap needs to know if it's ended, if this breaks
	// something else we could put something special in pktWrap
	//if(Write == bs->mode)
	//	return 0;

	if (bs->errorFlags & BSE_OVERRUN)
		return 1;

	return (bsGetCursorBitPosition(bs) >= bsGetBitLength(bs));
}

unsigned int bsGetCursorBitPosition(BitStream* bs){
	return (bs->cursor.byte << 3) + bs->cursor.bit;
}

void bsSetCursorBitPosition(BitStream* bs, unsigned int position){
	assert (position <= bs->bitLength);
	bs->cursor.byte = position >> 3;
	bs->cursor.bit = position & 7;
}

void bsSetEndOfStreamPosition(BitStream *bs, unsigned int position){
	bsSetCursorBitPosition(bs, position);
	bs->size = bs->cursor.byte;
	bs->bitLength = bsGetCursorBitPosition(bs);
	// Write zeroes to the rest of the byte we're at
	bs->data[bs->cursor.byte] &= (1 << bs->cursor.bit)-1;
}
/*
static char *debugBytesToString(char *buf, int size, int max) {
	int numbytes = min(size, max);
	int i;
	static char buff[256];
	buff[0]=0;
	for (i=0; i<numbytes; i++) {
		strcatf(buff, "%02x", (unsigned char)buf[i]);
		if (i%4==3)
			strcat(buff, " ");
	}
	return buff;
}
*/
void testBitStream(void)
{
	BitStream bs;
	#define bufferSize 4 * 1024
	unsigned char buffer[bufferSize];

	bsAssertOnErrors(1);
	// Simple read/write test.
	{
		int result;
		int opBitCount = 9;
		int opData = 257;

		initBitStream(&bs, buffer, bufferSize, Write, false, NULL);
		bsWriteBits(&bs, opBitCount, opData);
		bsChangeMode(&bs, Read);
		result = bsReadBits(&bs, opBitCount);
		assert(result == opData);
	}

	// Multiple read/write test.
	{
		int resultInt;
		int reserveCount=13;
		int reserveValue = 3210;
		int opBitCount = 9;
		int opPackBitCount = 3;
		int opData = 257;
		int opData2 = -1;
		U32 cursorPosition;
		F32 opFloat = -1.;
		F32 resultFloat;
		char str[] = "Hello world!";
		char str2[] = "Hello again!";
		char resultString[] = "                                    ";

		char *pResultEString = NULL;

		memset(buffer, 0xff, sizeof(buffer));

		initBitStream(&bs, buffer, bufferSize, Write, false, NULL);

//		printf("\n");

		bsWriteBits(&bs, opBitCount, opData);
		cursorPosition = bsGetCursorBitPosition(&bs);
		bsWriteBits(&bs, reserveCount, 0);
		bsWriteBitsPack(&bs, opPackBitCount, opData2);
		bsWriteF32(&bs, opFloat);
		bsWriteString(&bs, str);
		bsWriteBitsArray(&bs, sizeof(str) << 3, str);
		bsWriteBitsArray(&bs, sizeof(str2) << 3, str2);
		bsSetCursorBitPosition(&bs, cursorPosition);
		bsWriteBits(&bs, reserveCount, reserveValue);


		bsChangeMode(&bs, Read);
		resultInt = bsReadBits(&bs, opBitCount);
		assert(resultInt == opData);
		resultInt = bsReadBits(&bs, reserveCount);
		assert(resultInt == reserveValue);
		resultInt = bsReadBitsPack(&bs, opPackBitCount);
		assert(resultInt == opData2);
		resultFloat = bsReadF32(&bs);
		assert(resultFloat == opFloat);

		bsReadString(&bs, &pResultEString);
		assert(stricmp(pResultEString, str)==0);
		estrDestroy(&pResultEString);

		memset(resultString, 0, sizeof(resultString));
		bsReadBitsArray(&bs, sizeof(str) << 3, resultString);
		assert(stricmp(resultString, str)==0);

		memset(resultString, 0, sizeof(resultString));
		bsReadBitsArray(&bs, sizeof(str2) << 3, resultString);
		assert(stricmp(resultString, str2)==0);
	}

}





typedef enum{
	BS_BITS = 3,
	BS_PACKEDBITS,
	BS_BITARRAY,
	BS_STRING,
	BS_F32
} BitType;

// During bsTypedXXX, "DataTypeBitLength" bits are placed before the actual data
// to denote the "type" of the data.
#define DataTypeBitLength 3

// During bsTypedXXX, at least "DataLengthMinBits" bits are used to encode the length
// of data associated with that particular operation.  The "length" means different
// things depending on the type.
#define DataLengthMinBits 5

void bsTypedWriteBits(BitStream* bs, int numbits, unsigned int val){
	bsWriteBits(bs, DataTypeBitLength, BS_BITS);
	bsWriteBitsPack(bs, DataLengthMinBits, numbits);
	bsWriteBits(bs, numbits, val);
}

void bsTypedWriteBits64(BitStream* bs, int numbits, U64 val){
	bsWriteBits(bs, DataTypeBitLength, BS_BITS);
	bsWriteBitsPack(bs, DataLengthMinBits, numbits);
	bsWriteBits64(bs, numbits, val);
}

static const char* bsGetBitTypeName(BitType bitType){
	switch(bitType){
		case BS_BITS:
			return "BS_BITS";
		case BS_PACKEDBITS:
			return "BS_PACKEDBITS";
		case BS_BITARRAY:
			return "BS_BITARRAY";
		case BS_STRING:
			return "BS_STRING";
		case BS_F32:
			return "BS_F32";
		default: {
			static char buf[64];
			sprintf(buf, "UNKNOWN (%d)", bitType);
			return buf;
		}
	}
}

#define ASSERT_INTVALUE(name, rightParam, wrongParam){		\
	int right = rightParam;									\
	int wrong = wrongParam;									\
	if(right != wrong){										\
		if (g_bAssertOnBitStreamError) {					\
			assertNumbits(name, right, wrong);				\
		}													\
		bs->errorFlags = BSE_TYPEMISMATCH;					\
	}														\
}

#define READ_DATALENGTH_NAME(name, expectedLength){			\
	int dataLength = bsReadBitsPack(bs, DataLengthMinBits);	\
	ASSERT_INTVALUE(name, dataLength, expectedLength);		\
}

#define READ_DATALENGTH(expectedLength)						\
	READ_DATALENGTH_NAME(#expectedLength, expectedLength)

static DataTypeErrorCb s_datatypeErrCb = NULL;

DataTypeErrorCb setDataTypeErrorCb(DataTypeErrorCb dtErrCb)
{
	DataTypeErrorCb cur = s_datatypeErrCb;
	s_datatypeErrCb = dtErrCb;
	return cur;
}

static void checkDataTypeError(BitStream* bs, int type, int expected){
	char buffer[1000];
	int dataLength;
	char* pEStr = NULL;

	buffer[0] = 0;

	strcatf(buffer,
			"Bitstream Error:\n"
			"  Found type: %s\n",
			bsGetBitTypeName(type));
			
	strcatf(buffer,
			"  Expected type: %s\n",
			bsGetBitTypeName(expected));

	switch(type){
		xcase BS_BITS:
		case BS_PACKEDBITS:
		case BS_BITARRAY:
		case BS_F32:
			dataLength = bsReadBitsPack(bs, DataLengthMinBits);
			strcatf(buffer, "Expected bits: %d\n", dataLength);

		xcase BS_STRING:
			estrStackCreate(&pEStr);
			dataLength = bsReadBitsPack(bs, DataLengthMinBits);
			bsReadString(bs, &pEStr);
			strcatf(buffer, "String: \"%s\"\n  Length: %d\n  Expected Length: %d\n", pEStr, dataLength, estrLength(&pEStr));
			estrDestroy(&pEStr);
	}

	printf("\n%s", buffer);

	if( s_datatypeErrCb )
		s_datatypeErrCb(buffer);
	else
		assertmsg(0, buffer);
}

static int readDataType(BitStream* bs, int expectedType){
	int gotType = bsReadBits(bs, DataTypeBitLength);

	if (gotType != expectedType) {
		if (g_bAssertOnBitStreamError) {
			checkDataTypeError(bs, gotType, expectedType);
		}
		bs->errorFlags = BSE_TYPEMISMATCH;
		return 0;
	}
	
	return 1;
}

#define READ_DATATYPE(expectedType) if(!readDataType(bs, expectedType)) return 0;

static void assertNumbits(const char* name, int right, int wrong){
	char buffer[100];
	sprintf(buffer, "Wrong %s.  Right: %d.  Wrong: %d\n", name, right, wrong);

	printf("\n%s", buffer);
	assertmsg(0, buffer);
}

unsigned int bsTypedReadBits(BitStream* bs, int numbits){
	// Is the correct type being extracted?

	READ_DATATYPE(BS_BITS);

	// Is the correct number of bits being extracted?

	READ_DATALENGTH(numbits);

	return bsReadBits(bs, numbits);
}

U64 bsTypedReadBits64(BitStream* bs, int numbits){
	// Is the correct type being extracted?

	READ_DATATYPE(BS_BITS);

	// Is the correct number of bits being extracted?

	READ_DATALENGTH(numbits);

	return bsReadBits64(bs, numbits);
}

int bsTypedWriteBitsArray(BitStream* bs, int numbits, const void* data){
	bsWriteBits(bs, DataTypeBitLength, BS_BITARRAY);
	bsWriteBitsPack(bs, DataLengthMinBits, numbits);
	return bsWriteBitsArray(bs, numbits, data);
}

int bsTypedReadBitsArray(BitStream* bs, int numbits, void* data){
	// Is the correct type being extracted?

	READ_DATATYPE(BS_BITARRAY);

	// Is the correct number of bits being extracted?

	READ_DATALENGTH(numbits);

	return bsReadBitsArray(bs, numbits, data);
}

void bsTypedWriteBitsPack(BitStream* bs, int minbits, unsigned int val){
	bsWriteBits(bs, DataTypeBitLength, BS_PACKEDBITS);
	bsWriteBitsPack(bs, DataLengthMinBits, minbits);
	bsWriteBitsPack(bs, minbits, val);
}

int bsGetLengthTypedWriteBitsPack(BitStream* bs, int minbits, unsigned int val){
	int ret=0;
	ret += bsGetLengthWriteBits(bs, DataTypeBitLength, BS_PACKEDBITS);
	ret += bsGetLengthWriteBitsPack(bs, DataLengthMinBits, minbits);
	ret += bsGetLengthWriteBitsPack(bs, minbits, val);
	return ret;
}

unsigned int bsTypedReadBitsPack(BitStream* bs,int minbits){
	// Is the correct type being extracted?
	
	READ_DATATYPE(BS_PACKEDBITS);

	// Is the correct number of bits being extracted?
	
	READ_DATALENGTH(minbits);

	return bsReadBitsPack(bs, minbits);
}

void bsTypedWriteString(BitStream* bs, const char* str){
	if(!str)
		str = "";

	bsWriteBits(bs, DataTypeBitLength, BS_STRING);
	bsWriteBitsPack(bs, DataLengthMinBits, (int)strlen(str));
	bsWriteString(bs, str);
}

int bsTypedReadStringAndLength(BitStream* bs, char **ppEString)
{
	int dataLength;
	int iRetVal;

	// Is the correct type being extracted?
	
	READ_DATATYPE(BS_STRING);

	// How long should the string be?

	dataLength = bsReadBitsPack(bs, DataLengthMinBits);
	iRetVal = bsReadString(bs, ppEString);

	// Did the string match the expected string length?

	ASSERT_INTVALUE("stringlength", dataLength, iRetVal);

	return iRetVal;
}

void bsTypedWriteF32(BitStream* bs, float f){
	bsWriteBits(bs, DataTypeBitLength, BS_F32);
	bsWriteBitsPack(bs, DataLengthMinBits, 32);
	bsWriteF32(bs, f);
}

float bsTypedReadF32(BitStream* bs){
	// Is the correct type being extracted?
	
	READ_DATATYPE(BS_F32);

	// Is the correct number of bits being extracted?

	READ_DATALENGTH_NAME("floatsize", sizeof(float) * 8);

	return bsReadF32(bs);
}

int bsTypedAlignBitsArray(BitStream* bs){
	int dataLength;

	// Read and discard the bit array "type" marker.

	READ_DATATYPE(BS_BITARRAY);

	// Read and discard the bit array length.
	//	WARNING!  Using this function means the bit array length
	//	will not be checked!
	dataLength = bsReadBitsPack(bs, DataLengthMinBits);

	// Align the cursor to where the bit array starts.
	bsAlignByte(bs);
	return 1;
}

// To call this on a good packet, for example, run these:
// bsReadBits(&pak->stream, 3)
// bsReadBitsPack(&pak->stream, 5)
// bsTypedWalkStream(&pak->stream, firstvalue, secondvalue)
void bsTypedWalkStream(BitStream* bs, BitType initialBitType, int initialBitLength)
{
	BitType bt = initialBitType;
	int bl = initialBitLength;
	int val;
	F32 fval;
	int quit=0;
	char *str = "check stack around checkDataTypeError";
	// Assumes we hit a bsAssert (or a dataLength assert), and that the bitType and dataLength have
	//  already  been read
	do {
		switch (bt) {
		case BS_BITS:
			val = bsReadBits(bs, bl);
			printf("%d bit%s = %d\n", bl, (bl>1)?"s":"", val);
		xcase BS_PACKEDBITS:
			val = bsReadBitsPack(bs, bl);
			printf("%d packedBits = %d\n", bl, val);
		xcase BS_BITARRAY:
			printf("BitArrayNotHandled\n");
			quit=1;
		xcase BS_F32:
			fval = bsReadF32(bs);
			printf("F32 = %f\n", fval);
		xcase BS_STRING:
			printf("String = %s\n", str);
		xdefault:
			printf("error/bad stream?");
			quit = 1;
		}
		if (bsEnded(bs)) {
			quit = 1;
		} else {
			bt = bsReadBits(bs, DataTypeBitLength);
			bl = bsReadBitsPack(bs, DataLengthMinBits);
			if (bt==BS_STRING) 
			{
				char *ppEString = NULL;
				estrStackCreate(&ppEString);
				bsReadString(bs, &ppEString);
				estrDestroy(&ppEString);
			}
		}
	} while (!quit && !bsEnded(bs));
	printf("done.\n");
}
