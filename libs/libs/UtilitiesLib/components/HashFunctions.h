#ifndef HASHFUNCTIONS_H
#define HASHFUNCTIONS_H

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include "stdtypes.h"

#define DEFAULT_HASH_SEED 0xfaceface

// Specific hash functions
U32 burtlehash( const U8 *k, U32  length, U32  initval);
U32 burtlehash2( const U32 *k, U32  length, U32  initval);
U32 burtlehash3( const U8 *k, U32  length, U32  initval);
U32 burtlehash3CaseInsensitive( const U8 *k, U32  length, U32  initval);

U32 burtle3_hashword( const U32 *k, size_t length, U32  initval);

U32 MurmurHash2( const U8* key, U32 len, U32 seed );
U32 MurmurHash2CaseInsensitive( const U8* key, U32 len, U32 seed );
U32 HsiehHash(const U8 * data, U32 len, U32 seed);
U32 HsiehHashCaseInsensitive(const U8 * data, U32 len, U32 seed);

#define stashDefaultHash				MurmurHash2
#define stashDefaultHashCaseInsensitive	MurmurHash2CaseInsensitive
#define stashDefaultHash_inline				MurmurHash2_inline
#define stashDefaultHashCaseInsensitive_inline	MurmurHash2CaseInsensitive_inline

// General purpose functions
U32	hashString( const char* pcToHash, bool bCaseSensitive );
U32	hashStringInsensitive( const char* pcToHash );
U32 hashCalc( const void *k, U32  length, U32  initval);

__forceinline static U32 MurmurHash2Pointer_inline( const U8* key, U32 seed )
{
	// 'm' and 'r' are mixing constants generated offline.
	// They're not really 'magic', they just happen to work well.

	const unsigned int m = 0x5bd1e995;
	const int r = 24;

	int len = sizeof(void *);

	// Initialize the hash to a 'random' value

	unsigned int h = seed ^ len;

	// Mix 4 bytes at a time into the hash

	const unsigned char * data = (const unsigned char *)key;

	while(len >= 4)
	{
		unsigned int k = *(unsigned int *)data;

		k *= m; 
		k ^= k >> r; 
		k *= m; 
		
		h *= m; 
		h ^= k;

		data += 4;
		len -= 4;
	}

	// Do a few final mixes of the hash to ensure the last few
	// bytes are well-incorporated.

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

__forceinline static U32 MurmurHash2_inline( const U8* key, U32 len, U32 seed )
{
	// 'm' and 'r' are mixing constants generated offline.
	// They're not really 'magic', they just happen to work well.

	const unsigned int m = 0x5bd1e995;
	const int r = 24;

	// Initialize the hash to a 'random' value

	unsigned int h = seed ^ len;

	// Mix 4 bytes at a time into the hash

	const unsigned char * data = (const unsigned char *)key;

	while(len >= 4)
	{
		unsigned int k = *(unsigned int *)data;

		k *= m; 
		k ^= k >> r; 
		k *= m; 
		
		h *= m; 
		h ^= k;

		data += 4;
		len -= 4;
	}
	
	// Handle the last few bytes of the input array

	switch(len)
	{
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
	        h *= m;
	};

	// Do a few final mixes of the hash to ensure the last few
	// bytes are well-incorporated.

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
} 

__forceinline static U32 MurmurHash2CaseInsensitive_inline( const U8* key, U32 len, U32 seed )
{
	// 'm' and 'r' are mixing constants generated offline.
	// They're not really 'magic', they just happen to work well.

	const unsigned int m = 0x5bd1e995;
	const int r = 24;

	// Initialize the hash to a 'random' value

	unsigned int h = seed ^ len;

	// Mix 4 bytes at a time into the hash

	const unsigned char * data = (const unsigned char *)key;

	while(len >= 4)
	{
		unsigned int k = *(unsigned int *)data|0x20202020;

		k *= m; 
		k ^= k >> r; 
		k *= m; 
		
		h *= m; 
		h ^= k;

		data += 4;
		len -= 4;
	}
	
	// Handle the last few bytes of the input array

	switch(len)
	{
	case 3: h ^= (data[2]|0x20) << 16;
	case 2: h ^= (data[1]|0x20) << 8;
	case 1: h ^= (data[0]|0x20);
	        h *= m;
	};

	// Do a few final mixes of the hash to ensure the last few
	// bytes are well-incorporated.

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
} 


__forceinline static U32 MurmurHash2Int(U32 i )
{
	// 'm' and 'r' are mixing constants generated offline.
	// They're not really 'magic', they just happen to work well.

	const unsigned int m = 0x5bd1e995;
	const int r = 24;

	// Initialize the hash to a 'random' value

	unsigned int h = 0xdeadbeef;

	// Mix 4 bytes

	i *= m; 
	i ^= i >> r; 
	i *= m; 

	h *= m; 
	h ^= i;

	// Do a few final mixes of the hash to ensure the last few
	// bytes are well-incorporated.

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
} 

#endif