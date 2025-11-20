#pragma once
GCC_SYSTEM

C_DECLARATIONS_BEGIN

// PC is Little Endian which means the number 0x12345678 is stored as 78 56 34 12 in memory
// Xbox is Big Endian, which means the number 0x12345678 is stored as 12 34 56 78 in memory

typedef union _EndianTest {
	U32 intValue;
	U8 bytes[4];
} EndianTest;

extern EndianTest endian_test;

#define isBigEndian() (endian_test.bytes[0]==0)
#define endianSwapU8(x) (x)
#define endianSwapU16(x) ((((x) & 0xFF) << 8) | (((x) & 0xFF00) >> 8))
#define endianSwapS16 endianSwapU16
#define endianSwapU32(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) & 0xFF0000) >> 8) | (((x) & 0xFF000000) >> 24))
#define endianSwapS32 endianSwapU32
#define endianSwapU64(x) (((endianSwapU32((x) & 0xFFFFFFFF)) << 32LL) | (endianSwapU32(((x) >> 32LL))))
#define endianSwapS64 endianSwapU64
#define endianSwap__time32_t(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) & 0xFF0000) >> 8) | (((x) & 0xFF000000) >> 24))

typedef union _EndianF32 {
    F32 fValue;
	U32 intValue;
} EndianF32;

__forceinline static F32 endianSwapF32(F32 x)
{
    EndianF32 t;
    t.fValue = x;
	t.intValue = endianSwapU32(t.intValue);
	return t.fValue;
}

typedef union _EndianF64 {
    F64 fValue;
	U64 intValue;
} EndianF64;

__forceinline static F64 endianSwapF64(F64 x)
{
    EndianF64 t;
    t.fValue = x;
	t.intValue = endianSwapU64(t.intValue);
	return t.fValue;
}

#define endianSwapVec3(src,dst) (((dst)[0]) = endianSwapF32((src)[0]), ((dst)[1]) = endianSwapF32((src)[1]), ((dst)[2]) = endianSwapF32((src)[2]))
#define endianSwapVec4(src,dst) (((dst)[0]) = endianSwapF32((src)[0]), ((dst)[1]) = endianSwapF32((src)[1]), ((dst)[2]) = endianSwapF32((src)[2]), ((dst)[3]) = endianSwapF32((src)[3]))
#define endianSwapQuat endianSwapVec4

__forceinline static void endianSwapMat4(const Mat4 src, Mat4 dst)
{
	endianSwapVec3(src[0],dst[0]);
	endianSwapVec3(src[1],dst[1]);
	endianSwapVec3(src[2],dst[2]);
	endianSwapVec3(src[3],dst[3]);
}

#define endianSwapIf(type, x, pred)		((pred)?endianSwap##type(x):(x))
#define endianSwapIfBig(type, x)		endianSwapIf(type, x, isBigEndian())
#define endianSwapIfNotBig(type, x)		endianSwapIf(type, x, !isBigEndian())


void endianSwapStruct(ParseTable pti[], void *structptr);
#define endianSwapStructIfBig(pti, structptr) if (isBigEndian()) endianSwapStruct(pti, structptr)


// porting helper defines
#if _XBOX || _PS3
#define xbEndianSwapF32(a) a = endianSwapF32(a)
#define xbEndianSwapU32(a) a = endianSwapU32(a)
#define xbEndianSwapU16(a) a = endianSwapU16(a)
#define xbEndianSwapU64(a) a = endianSwapU64(a)
#define xbEndianSwapVec3(a,b) endianSwapVec3(a, b)
#define xbEndianSwapQuat(a,b) endianSwapQuat(a, b)
#define xbEndianSwapStruct(a, b) endianSwapStruct(a, b)


#else
#define xbEndianSwapF32(a)
#define xbEndianSwapU32(a)
#define xbEndianSwapU16(a)
#define xbEndianSwapU64(a)
#define xbEndianSwapVec3(a,b)
#define xbEndianSwapQuat(a,b)
#define xbEndianSwapStruct(a, b)

#define xbEndianSwapAutoInt(a)

#endif

C_DECLARATIONS_END

