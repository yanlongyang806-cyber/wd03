#include "HashFunctions.h"





// A collection of all the possible free hash functions we've tried:



/*
Found these at:
http://burtleburtle.net/bob/c/lookup2.c

Prefixed the hash functions with burtle, just to distinguish them.

--------------------------------------------------------------------
lookup2.c, by Bob Jenkins, December 1996, Public Domain.
hash(), hash2(), hash3, and mix() are externally useful functions.
Routines to test the hash are included if SELF_TEST is defined.
You can use this free for any purpose.  It has no warranty.
--------------------------------------------------------------------
*/

#define hashsize(n) ((U32)1<<(n))
#define hashmask(n) (hashsize(n)-1)

/*
--------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.
For every delta with one or two bit set, and the deltas of all three
high bits or all three low bits, whether the original value of a,b,c
is almost all zero or is uniformly distributed,
* If mix() is run forward or backward, at least 32 bits in a,b,c
have at least 1/4 probability of changing.
* If mix() is run forward, every bit of c will change between 1/3 and
2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)
mix() was built out of 36 single-cycle latency instructions in a 
structure that could supported 2x parallelism, like so:
a -= b; 
a -= c; x = (c>>13);
b -= c; a ^= x;
b -= a; x = (a<<8);
c -= a; b ^= x;
c -= b; x = (b>>13);
...
Unfortunately, superscalar Pentiums and Sparcs can't take advantage 
of that parallelism.  They've also turned some of those single-cycle
latency instructions into multi-cycle latency instructions.  Still,
this is the fastest good hash I could find.  There were about 2^^68
to choose from.  I only looked at a billion or so.
--------------------------------------------------------------------
*/
#define mix(a,b,c) \
{ \
	a -= b; a -= c; a ^= (c>>13); \
	b -= c; b -= a; b ^= (a<<8); \
	c -= a; c -= b; c ^= (b>>13); \
	a -= b; a -= c; a ^= (c>>12);  \
	b -= c; b -= a; b ^= (a<<16); \
	c -= a; c -= b; c ^= (b>>5); \
	a -= b; a -= c; a ^= (c>>3);  \
	b -= c; b -= a; b ^= (a<<10); \
	c -= a; c -= b; c ^= (b>>15); \
}

/* same, but slower, works on systems that might have 8 byte U32's */
#define mix2(a,b,c) \
{ \
	a -= b; a -= c; a ^= (c>>13); \
	b -= c; b -= a; b ^= (a<< 8); \
	c -= a; c -= b; c ^= ((b&0xffffffff)>>13); \
	a -= b; a -= c; a ^= ((c&0xffffffff)>>12); \
	b -= c; b -= a; b = (b ^ (a<<16)) & 0xffffffff; \
	c -= a; c -= b; c = (c ^ (b>> 5)) & 0xffffffff; \
	a -= b; a -= c; a = (a ^ (c>> 3)) & 0xffffffff; \
	b -= c; b -= a; b = (b ^ (a<<10)) & 0xffffffff; \
	c -= a; c -= b; c = (c ^ (b>>15)) & 0xffffffff; \
}

/*
--------------------------------------------------------------------
hash() -- hash a variable-length key into a 32-bit value
k     : the key (the unaligned variable-length array of bytes)
len   : the length of the key, counting by bytes
level : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Every 1-bit and 2-bit delta achieves avalanche.
About 36+6len instructions.

The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.

If you are hashing n strings (U8 **)k, do it like this:
for (i=0, h=0; i<n; ++i) h = hash( k[i], len[i], h);

By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial.  It's free.

See http://burlteburtle.net/bob/hash/evahash.html
Use for hash table lookup, or anything where one collision in 2^32 is
acceptable.  Do NOT use for cryptographic purposes.
--------------------------------------------------------------------
*/

U32 burtlehash( const U8 *k, U32  length, U32  initval)
//register U8 *k;        /* the key */
//register U32  length;   /* the length of the key */
//register U32  initval;    /* the previous hash, or an arbitrary value */
{
	register U32 a,b,c,len;

	/* Set up the internal state */
	len = length;
	a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
	c = initval;           /* the previous hash value */

	/*---------------------------------------- handle most of the key */
	while (len >= 12)
	{
		a += (k[0] +((U32)k[1]<<8) +((U32)k[2]<<16) +((U32)k[3]<<24));
		b += (k[4] +((U32)k[5]<<8) +((U32)k[6]<<16) +((U32)k[7]<<24));
		c += (k[8] +((U32)k[9]<<8) +((U32)k[10]<<16)+((U32)k[11]<<24));
		mix(a,b,c);
		k += 12; len -= 12;
	}

	/*------------------------------------- handle the last 11 bytes */
	c += length;
	switch(len)              /* all the case statements fall through */
	{
	case 11: c+=((U32)k[10]<<24);
	case 10: c+=((U32)k[9]<<16);
	case 9 : c+=((U32)k[8]<<8);
		/* the first byte of c is reserved for the length */
	case 8 : b+=((U32)k[7]<<24);
	case 7 : b+=((U32)k[6]<<16);
	case 6 : b+=((U32)k[5]<<8);
	case 5 : b+=k[4];
	case 4 : a+=((U32)k[3]<<24);
	case 3 : a+=((U32)k[2]<<16);
	case 2 : a+=((U32)k[1]<<8);
	case 1 : a+=k[0];
		/* case 0: nothing left to add */
	}
	mix(a,b,c);
	/*-------------------------------------------- report the result */
	return c;
}


/*
--------------------------------------------------------------------
This works on all machines.  hash2() is identical to hash() on 
little-endian machines, except that the length has to be measured
in U32s instead of bytes.  It is much faster than hash().  It 
requires
-- that the key be an array of U32's, and
-- that all your machines have the same endianness, and
-- that the length be the number of U32's in the key
--------------------------------------------------------------------
*/
U32 burtlehash2( const U32 *k, U32  length, U32  initval)
//register U32 *k;        /* the key */
//register U32  length;   /* the length of the key, in U32s */
//register U32  initval;  /* the previous hash, or an arbitrary value */
{
	register U32 a,b,c,len;

	/* Set up the internal state */
	len = length;
	a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
	c = initval;           /* the previous hash value */

	/*---------------------------------------- handle most of the key */
	while (len >= 3)
	{
		a += k[0];
		b += k[1];
		c += k[2];
		mix(a,b,c);
		k += 3; len -= 3;
	}

	/*-------------------------------------- handle the last 2 U32's */
	c += length;
	switch(len)              /* all the case statements fall through */
	{
		/* c is reserved for the length */
	case 2 : b+=k[1];
	case 1 : a+=k[0];
		/* case 0: nothing left to add */
	}
	mix(a,b,c);
	/*-------------------------------------------- report the result */
	return c;
}

/*
--------------------------------------------------------------------
This is identical to hash() on little-endian machines (like Intel 
x86s or VAXen).  It gives nondeterministic results on big-endian
machines.  It is faster than hash(), but a little slower than 
hash2(), and it requires
-- that all your machines be little-endian
--------------------------------------------------------------------
*/

U32 burtlehash3( const U8 *k, U32  length, U32  initval)
//register U8 *k;        /* the key */
//register U32  length;   /* the length of the key */
//register U32  initval;  /* the previous hash, or an arbitrary value */
{
	register U32 a,b,c,len;

	/* Set up the internal state */
	len = length;
	a = b = 0x9e3779b9;    /* the golden ratio; an arbitrary value */
	c = initval;           /* the previous hash value */

	/*---------------------------------------- handle most of the key */
/*	if (((uintptr_t)k)&3)
	{
		while (len >= 12)    // unaligned
		{
			a += (k[0] +((U32)k[1]<<8) +((U32)k[2]<<16) +((U32)k[3]<<24));
			b += (k[4] +((U32)k[5]<<8) +((U32)k[6]<<16) +((U32)k[7]<<24));
			c += (k[8] +((U32)k[9]<<8) +((U32)k[10]<<16)+((U32)k[11]<<24));
			mix(a,b,c);
			k += 12; len -= 12;
		}
	}
	else*/
	// The aligned code works equally well on non-aligned addresses
	// But there may be some performance issues.
	// The above code does NOT work on bit endian machines -BZ
	{
		while (len >= 12)    /* aligned */
		{
			a += *(U32 *)(k+0);
			b += *(U32 *)(k+4);
			c += *(U32 *)(k+8);
			mix(a,b,c);
			k += 12; len -= 12;
		}
	}

	/*------------------------------------- handle the last 11 bytes */
	c += length;
	switch(len)              /* all the case statements fall through */
	{
	case 11: c+=((U32)k[10]<<24);
	case 10: c+=((U32)k[9]<<16);
	case 9 : c+=((U32)k[8]<<8);
		/* the first byte of c is reserved for the length */
	case 8 : b+=((U32)k[7]<<24);
	case 7 : b+=((U32)k[6]<<16);
	case 6 : b+=((U32)k[5]<<8);
	case 5 : b+=k[4];
	case 4 : a+=((U32)k[3]<<24);
	case 3 : a+=((U32)k[2]<<16);
	case 2 : a+=((U32)k[1]<<8);
	case 1 : a+=k[0];
		/* case 0: nothing left to add */
	}
	mix(a,b,c);
	/*-------------------------------------------- report the result */
	return c;
}


// NOTE: this version of the hash function sets the 5th (lower case) bit on every byte before hashing
// in theory this will not only cause a collision between lower and upper case characters (wanted for case insensitivity)
// but also between certain control characters, punctuation, etc. 
// should be ok in our hash table implementation
U32 burtlehash3CaseInsensitive( const U8 *k, U32  length, U32  initval)
//register U8 *k;        /* the key */
//register U32  length;   /* the length of the key */
//register U32  initval;  /* the previous hash, or an arbitrary value */
{
	register U32 a,b,c,len;

	/* Set up the internal state */
	len = length;
	a = b = 0x9e3779b9;    /* the golden ratio; an arbitrary value */
	c = initval;           /* the previous hash value */

	/*---------------------------------------- handle most of the key */
/*	if (((uintptr_t)k)&3)
	{
		while (len >= 12)    // unaligned
		{
			a += (k[0] +((U32)k[1]<<8) +((U32)k[2]<<16) +((U32)k[3]<<24))|0x20202020;
			b += (k[4] +((U32)k[5]<<8) +((U32)k[6]<<16) +((U32)k[7]<<24))|0x20202020;
			c += (k[8] +((U32)k[9]<<8) +((U32)k[10]<<16)+((U32)k[11]<<24))|0x20202020;
			mix(a,b,c);
			k += 12; len -= 12;
		}
	}
	else*/
	{
		while (len >= 12)    /* aligned */
		{
			a += (*(U32 *)(k+0))|0x20202020;
			b += (*(U32 *)(k+4))|0x20202020;
			c += (*(U32 *)(k+8))|0x20202020;
			mix(a,b,c);
			k += 12; len -= 12;
		}
	}

	/*------------------------------------- handle the last 11 bytes */
	c += length;
	switch(len)              /* all the case statements fall through */
	{
	case 11: c+=((U32)(k[10]|0x20)<<24);
	case 10: c+=((U32)(k[9]|0x20)<<16);
	case 9 : c+=((U32)(k[8]|0x20)<<8);
		/* the first byte of c is reserved for the length */
	case 8 : b+=((U32)(k[7]|0x20)<<24);
	case 7 : b+=((U32)(k[6]|0x20)<<16);
	case 6 : b+=((U32)(k[5]|0x20)<<8);
	case 5 : b+=(k[4]|0x20);
	case 4 : a+=((U32)(k[3]|0x20)<<24);
	case 3 : a+=((U32)(k[2]|0x20)<<16);
	case 2 : a+=((U32)(k[1]|0x20)<<8);
	case 1 : a+=(k[0]|0x20);
		/* case 0: nothing left to add */
	}
	mix(a,b,c);
	/*-------------------------------------------- report the result */
	return c;
}


U32	hashString( const char* pcToHash, bool bCaseSensitive )
{
	if ( bCaseSensitive)
		return burtlehash3(pcToHash, (U32)strlen(pcToHash), DEFAULT_HASH_SEED);
	else // this version of the hash function sets the lowercase bit, so it does not distinguish between the two possibilities
		return burtlehash3CaseInsensitive(pcToHash, (U32)strlen(pcToHash), DEFAULT_HASH_SEED);
}

U32 hashStringInsensitive( const char *pcToHash )
{
	return burtlehash3CaseInsensitive(pcToHash, (U32)strlen(pcToHash), DEFAULT_HASH_SEED);
}

U32 hashCalc( const void *k, U32  length, U32  initval)
{
	return burtlehash3((const U8*)k, length, initval);
}

/*-------------------------------------------------------------------------------
lookup3.c, by Bob Jenkins, May 2006, Public Domain.

These are functions for producing 32-bit hashes for hash table lookup.
hashword(), hashlittle(), hashlittle2(), hashbig(), mix(), and final() 
are externally useful functions.  Routines to test the hash are included 
if SELF_TEST is defined.  You can use this free for any purpose.  It's in
the public domain.  It has no warranty.

You probably want to use hashlittle().  hashlittle() and hashbig()
hash byte arrays.  hashlittle() is is faster than hashbig() on
little-endian machines.  Intel and AMD are little-endian machines.
On second thought, you probably want hashlittle2(), which is identical to
hashlittle() except it returns two 32-bit hashes for the price of one.  
You could implement hashbig2() if you wanted but I haven't bothered here.

If you want to find a hash of, say, exactly 7 integers, do
  a = i1;  b = i2;  c = i3;
  mix(a,b,c);
  a += i4; b += i5; c += i6;
  mix(a,b,c);
  a += i7;
  final(a,b,c);
then use c as the hash value.  If you have a variable length array of
4-byte integers to hash, use hashword().  If you have a byte array (like
a character string), use hashlittle().  If you have several byte arrays, or
a mix of things, see the comments above hashlittle().  

Why is this so big?  I read 12 bytes at a time into 3 4-byte integers, 
then mix those integers.  This is fast (you can do a lot more thorough
mixing with 12*3 instructions on 3 integers than you can with 3 instructions
on 1 byte), but shoehorning those bytes into integers efficiently is messy.
-------------------------------------------------------------------------------
*/

#if _XBOX || _PS3
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 1
#else
# define HASH_LITTLE_ENDIAN 1
# define HASH_BIG_ENDIAN 0
#endif

#define hashsize(n) ((U32)1<<(n))
#define hashmask(n) (hashsize(n)-1)
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

/*
-------------------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.

This is reversible, so any information in (a,b,c) before mix() is
still in (a,b,c) after mix().

If four pairs of (a,b,c) inputs are run through mix(), or through
mix() in reverse, there are at least 32 bits of the output that
are sometimes the same for one pair and different for another pair.
This was tested for:
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or 
  all zero plus a counter that starts at zero.

Some k values for my "a-=c; a^=rot(c,k); c+=b;" arrangement that
satisfy this are
    4  6  8 16 19  4
    9 15  3 18 27 15
   14  9  3  7 17  3
Well, "9 15 3 18 27 15" didn't quite get 32 bits diffing
for "differ" defined as + with a one-bit base and a two-bit delta.  I
used http://burtleburtle.net/bob/hash/avalanche.html to choose 
the operations, constants, and arrangements of the variables.

This does not achieve avalanche.  There are input bits of (a,b,c)
that fail to affect some output bits of (a,b,c), especially of a.  The
most thoroughly mixed value is c, but it doesn't really even achieve
avalanche in c.

This allows some parallelism.  Read-after-writes are good at doubling
the number of bits affected, so the goal of mixing pulls in the opposite
direction as the goal of parallelism.  I did what I could.  Rotates
seem to cost as much as shifts on every machine I could lay my hands
on, and rotates are much kinder to the top and bottom bits, so I used
rotates.
-------------------------------------------------------------------------------
*/
#define burtle3_mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}

/*
-------------------------------------------------------------------------------
burtle3_final -- burtle3_final mixing of 3 32-bit values (a,b,c) into c

Pairs of (a,b,c) values differing in only a few bits will usually
produce values of c that look totally different.  This was tested for
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or 
  all zero plus a counter that starts at zero.

These constants passed:
 14 11 25 16 4 14 24
 12 14 25 16 4 14 24
and these came close:
  4  8 15 26 3 22 24
 10  8 15 26 3 22 24
 11  8 15 26 3 22 24
-------------------------------------------------------------------------------
*/
#define burtle3_final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

/*
--------------------------------------------------------------------
 This works on all machines.  To be useful, it requires
 -- that the key be an array of U32's, and
 -- that the length be the number of U32's in the key

 The function hashword() is identical to hashlittle() on little-endian
 machines, and identical to hashbig() on big-endian machines,
 except that the length has to be measured in uint32_ts rather than in
 bytes.  hashlittle() is more complicated than hashword() only because
 hashlittle() has to dance around fitting the key bytes into registers.
--------------------------------------------------------------------
*/
U32 burtle3_hashword(
const U32 *k,                   /* the key, an array of U32 values */
size_t          length,               /* the length of the key, in uint32_ts */
U32        initval)         /* the previous hash, or an arbitrary value */
{
  U32 a,b,c;

  /* Set up the internal state */
  a = b = c = 0xdeadbeef + (((U32)length)<<2) + initval;

  /*------------------------------------------------- handle most of the key */
  while (length > 3)
  {
    a += k[0];
    b += k[1];
    c += k[2];
    burtle3_mix(a,b,c);
    length -= 3;
    k += 3;
  }

  /*------------------------------------------- handle the last 3 U32's */
  switch(length)                     /* all the case statements fall through */
  { 
  case 3 : c+=k[2];
  case 2 : b+=k[1];
  case 1 : a+=k[0];
    burtle3_final(a,b,c);
  case 0:     /* case 0: nothing left to add */
    break;
  }
  /*------------------------------------------------------ report the result */
  return c;
}


/*
--------------------------------------------------------------------
hashword2() -- same as hashword(), but take two seeds and return two
32-bit values.  pc and pb must both be nonnull, and *pc and *pb must
both be initialized with seeds.  If you pass in (*pb)==0, the output 
(*pc) will be the same as the return value from hashword().
--------------------------------------------------------------------
*/
void burtle3_hashword2 (
const U32 *k,                   /* the key, an array of U32 values */
size_t          length,               /* the length of the key, in uint32_ts */
U32       *pc,                      /* IN: seed OUT: primary hash value */
U32       *pb)               /* IN: more seed OUT: secondary hash value */
{
  U32 a,b,c;

  /* Set up the internal state */
  a = b = c = 0xdeadbeef + ((U32)(length<<2)) + *pc;
  c += *pb;

  /*------------------------------------------------- handle most of the key */
  while (length > 3)
  {
    a += k[0];
    b += k[1];
    c += k[2];
    burtle3_mix(a,b,c);
    length -= 3;
    k += 3;
  }

  /*------------------------------------------- handle the last 3 U32's */
  switch(length)                     /* all the case statements fall through */
  { 
  case 3 : c+=k[2];
  case 2 : b+=k[1];
  case 1 : a+=k[0];
    burtle3_final(a,b,c);
  case 0:     /* case 0: nothing left to add */
    break;
  }
  /*------------------------------------------------------ report the result */
  *pc=c; *pb=b;
}


/*
-------------------------------------------------------------------------------
hashlittle() -- hash a variable-length key into a 32-bit value
  k       : the key (the unaligned variable-length array of bytes)
  length  : the length of the key, counting by bytes
  initval : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Two keys differing by one or two bits will have
totally different hash values.

The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.

If you are hashing n strings (U8 **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = hashlittle( k[i], len[i], h);

By Bob Jenkins, 2006.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial.  It's free.

Use for hash table lookup, or anything where one collision in 2^^32 is
acceptable.  Do NOT use for cryptographic purposes.
-------------------------------------------------------------------------------
*/

U32 burtle3_hashlittle( const void *key, size_t length, U32 initval)
{
  U32 a,b,c;                                          /* internal state */
  union { const void *ptr; size_t i; } u;     /* needed for Mac Powerbook G4 */

  /* Set up the internal state */
  a = b = c = 0xdeadbeef + ((U32)length) + initval;

  u.ptr = key;
  if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
    const U32 *k = (const U32 *)key;         /* read 32-bit chunks */

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      b += k[1];
      c += k[2];
      burtle3_mix(a,b,c);
      length -= 12;
      k += 3;
    }

    /*----------------------------- handle the last (probably partial) block */
    /* 
     * "k[2]&0xffffff" actually reads beyond the end of the string, but
     * then masks off the part it's not allowed to read.  Because the
     * string is aligned, the masked-off tail is in the same word as the
     * rest of the string.  Every machine with memory protection I've seen
     * does it on word boundaries, so is OK with this.  But VALGRIND will
     * still catch it and complain.  The masking trick does make the hash
     * noticably faster for short strings (like English words).
     */
#ifndef VALGRIND

    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=k[2]&0xffffff; b+=k[1]; a+=k[0]; break;
    case 10: c+=k[2]&0xffff; b+=k[1]; a+=k[0]; break;
    case 9 : c+=k[2]&0xff; b+=k[1]; a+=k[0]; break;
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=k[1]&0xffffff; a+=k[0]; break;
    case 6 : b+=k[1]&0xffff; a+=k[0]; break;
    case 5 : b+=k[1]&0xff; a+=k[0]; break;
    case 4 : a+=k[0]; break;
    case 3 : a+=k[0]&0xffffff; break;
    case 2 : a+=k[0]&0xffff; break;
    case 1 : a+=k[0]&0xff; break;
    case 0 : return c;              /* zero length strings require no mixing */
    }

#else /* make valgrind happy */

    k8 = (const U8 *)k;
    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=((U32)k8[10])<<16;  /* fall through */
    case 10: c+=((U32)k8[9])<<8;    /* fall through */
    case 9 : c+=k8[8];                   /* fall through */
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=((U32)k8[6])<<16;   /* fall through */
    case 6 : b+=((U32)k8[5])<<8;    /* fall through */
    case 5 : b+=k8[4];                   /* fall through */
    case 4 : a+=k[0]; break;
    case 3 : a+=((U32)k8[2])<<16;   /* fall through */
    case 2 : a+=((U32)k8[1])<<8;    /* fall through */
    case 1 : a+=k8[0]; break;
    case 0 : return c;
    }

#endif /* !valgrind */

  } else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
    const U16 *k = (const U16 *)key;         /* read 16-bit chunks */
    const U8  *k8;

    /*--------------- all but last block: aligned reads and different mixing */
    while (length > 12)
    {
      a += k[0] + (((U32)k[1])<<16);
      b += k[2] + (((U32)k[3])<<16);
      c += k[4] + (((U32)k[5])<<16);
      burtle3_mix(a,b,c);
      length -= 12;
      k += 6;
    }

    /*----------------------------- handle the last (probably partial) block */
    k8 = (const U8 *)k;
    switch(length)
    {
    case 12: c+=k[4]+(((U32)k[5])<<16);
             b+=k[2]+(((U32)k[3])<<16);
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 11: c+=((U32)k8[10])<<16;     /* fall through */
    case 10: c+=k[4];
             b+=k[2]+(((U32)k[3])<<16);
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 9 : c+=k8[8];                      /* fall through */
    case 8 : b+=k[2]+(((U32)k[3])<<16);
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 7 : b+=((U32)k8[6])<<16;      /* fall through */
    case 6 : b+=k[2];
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 5 : b+=k8[4];                      /* fall through */
    case 4 : a+=k[0]+(((U32)k[1])<<16);
             break;
    case 3 : a+=((U32)k8[2])<<16;      /* fall through */
    case 2 : a+=k[0];
             break;
    case 1 : a+=k8[0];
             break;
    case 0 : return c;                     /* zero length requires no mixing */
    }

  } else {                        /* need to read the key one byte at a time */
    const U8 *k = (const U8 *)key;

    /*--------------- all but the last block: affect some 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      a += ((U32)k[1])<<8;
      a += ((U32)k[2])<<16;
      a += ((U32)k[3])<<24;
      b += k[4];
      b += ((U32)k[5])<<8;
      b += ((U32)k[6])<<16;
      b += ((U32)k[7])<<24;
      c += k[8];
      c += ((U32)k[9])<<8;
      c += ((U32)k[10])<<16;
      c += ((U32)k[11])<<24;
      burtle3_mix(a,b,c);
      length -= 12;
      k += 12;
    }

    /*-------------------------------- last block: affect all 32 bits of (c) */
    switch(length)                   /* all the case statements fall through */
    {
    case 12: c+=((U32)k[11])<<24;
    case 11: c+=((U32)k[10])<<16;
    case 10: c+=((U32)k[9])<<8;
    case 9 : c+=k[8];
    case 8 : b+=((U32)k[7])<<24;
    case 7 : b+=((U32)k[6])<<16;
    case 6 : b+=((U32)k[5])<<8;
    case 5 : b+=k[4];
    case 4 : a+=((U32)k[3])<<24;
    case 3 : a+=((U32)k[2])<<16;
    case 2 : a+=((U32)k[1])<<8;
    case 1 : a+=k[0];
             break;
    case 0 : return c;
    }
  }

  burtle3_final(a,b,c);
  return c;
}


/*
 * hashlittle2: return 2 32-bit hash values
 *
 * This is identical to hashlittle(), except it returns two 32-bit hash
 * values instead of just one.  This is good enough for hash table
 * lookup with 2^^64 buckets, or if you want a second hash if you're not
 * happy with the first, or if you want a probably-unique 64-bit ID for
 * the key.  *pc is better mixed than *pb, so use *pc first.  If you want
 * a 64-bit value do something like "*pc + (((uint64_t)*pb)<<32)".
 */
void burtle3_hashlittle2( 
  const void *key,       /* the key to hash */
  size_t      length,    /* length of the key */
  U32   *pc,        /* IN: primary initval, OUT: primary hash */
  U32   *pb)        /* IN: secondary initval, OUT: secondary hash */
{
  U32 a,b,c;                                          /* internal state */
  union { const void *ptr; size_t i; } u;     /* needed for Mac Powerbook G4 */

  /* Set up the internal state */
  a = b = c = 0xdeadbeef + ((U32)length) + *pc;
  c += *pb;

  u.ptr = key;
  if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
    const U32 *k = (const U32 *)key;         /* read 32-bit chunks */

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      b += k[1];
      c += k[2];
      burtle3_mix(a,b,c);
      length -= 12;
      k += 3;
    }

    /*----------------------------- handle the last (probably partial) block */
    /* 
     * "k[2]&0xffffff" actually reads beyond the end of the string, but
     * then masks off the part it's not allowed to read.  Because the
     * string is aligned, the masked-off tail is in the same word as the
     * rest of the string.  Every machine with memory protection I've seen
     * does it on word boundaries, so is OK with this.  But VALGRIND will
     * still catch it and complain.  The masking trick does make the hash
     * noticably faster for short strings (like English words).
     */
#ifndef VALGRIND

    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=k[2]&0xffffff; b+=k[1]; a+=k[0]; break;
    case 10: c+=k[2]&0xffff; b+=k[1]; a+=k[0]; break;
    case 9 : c+=k[2]&0xff; b+=k[1]; a+=k[0]; break;
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=k[1]&0xffffff; a+=k[0]; break;
    case 6 : b+=k[1]&0xffff; a+=k[0]; break;
    case 5 : b+=k[1]&0xff; a+=k[0]; break;
    case 4 : a+=k[0]; break;
    case 3 : a+=k[0]&0xffffff; break;
    case 2 : a+=k[0]&0xffff; break;
    case 1 : a+=k[0]&0xff; break;
    case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
    }

#else /* make valgrind happy */

    k8 = (const U8 *)k;
    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=((U32)k8[10])<<16;  /* fall through */
    case 10: c+=((U32)k8[9])<<8;    /* fall through */
    case 9 : c+=k8[8];                   /* fall through */
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=((U32)k8[6])<<16;   /* fall through */
    case 6 : b+=((U32)k8[5])<<8;    /* fall through */
    case 5 : b+=k8[4];                   /* fall through */
    case 4 : a+=k[0]; break;
    case 3 : a+=((U32)k8[2])<<16;   /* fall through */
    case 2 : a+=((U32)k8[1])<<8;    /* fall through */
    case 1 : a+=k8[0]; break;
    case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
    }

#endif /* !valgrind */

  } else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
    const U16 *k = (const U16 *)key;         /* read 16-bit chunks */
    const U8  *k8;

    /*--------------- all but last block: aligned reads and different mixing */
    while (length > 12)
    {
      a += k[0] + (((U32)k[1])<<16);
      b += k[2] + (((U32)k[3])<<16);
      c += k[4] + (((U32)k[5])<<16);
      burtle3_mix(a,b,c);
      length -= 12;
      k += 6;
    }

    /*----------------------------- handle the last (probably partial) block */
    k8 = (const U8 *)k;
    switch(length)
    {
    case 12: c+=k[4]+(((U32)k[5])<<16);
             b+=k[2]+(((U32)k[3])<<16);
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 11: c+=((U32)k8[10])<<16;     /* fall through */
    case 10: c+=k[4];
             b+=k[2]+(((U32)k[3])<<16);
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 9 : c+=k8[8];                      /* fall through */
    case 8 : b+=k[2]+(((U32)k[3])<<16);
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 7 : b+=((U32)k8[6])<<16;      /* fall through */
    case 6 : b+=k[2];
             a+=k[0]+(((U32)k[1])<<16);
             break;
    case 5 : b+=k8[4];                      /* fall through */
    case 4 : a+=k[0]+(((U32)k[1])<<16);
             break;
    case 3 : a+=((U32)k8[2])<<16;      /* fall through */
    case 2 : a+=k[0];
             break;
    case 1 : a+=k8[0];
             break;
    case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
    }

  } else {                        /* need to read the key one byte at a time */
    const U8 *k = (const U8 *)key;

    /*--------------- all but the last block: affect some 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      a += ((U32)k[1])<<8;
      a += ((U32)k[2])<<16;
      a += ((U32)k[3])<<24;
      b += k[4];
      b += ((U32)k[5])<<8;
      b += ((U32)k[6])<<16;
      b += ((U32)k[7])<<24;
      c += k[8];
      c += ((U32)k[9])<<8;
      c += ((U32)k[10])<<16;
      c += ((U32)k[11])<<24;
      burtle3_mix(a,b,c);
      length -= 12;
      k += 12;
    }

    /*-------------------------------- last block: affect all 32 bits of (c) */
    switch(length)                   /* all the case statements fall through */
    {
    case 12: c+=((U32)k[11])<<24;
    case 11: c+=((U32)k[10])<<16;
    case 10: c+=((U32)k[9])<<8;
    case 9 : c+=k[8];
    case 8 : b+=((U32)k[7])<<24;
    case 7 : b+=((U32)k[6])<<16;
    case 6 : b+=((U32)k[5])<<8;
    case 5 : b+=k[4];
    case 4 : a+=((U32)k[3])<<24;
    case 3 : a+=((U32)k[2])<<16;
    case 2 : a+=((U32)k[1])<<8;
    case 1 : a+=k[0];
             break;
    case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
    }
  }

  burtle3_final(a,b,c);
  *pc=c; *pb=b;
}



/*
 * hashbig():
 * This is the same as hashword() on big-endian machines.  It is different
 * from hashlittle() on all machines.  hashbig() takes advantage of
 * big-endian byte ordering. 
 */
U32 burtle3_hashbig( const void *key, size_t length, U32 initval)
{
  U32 a,b,c;
  union { const void *ptr; size_t i; } u; /* to cast key to (size_t) happily */

  /* Set up the internal state */
  a = b = c = 0xdeadbeef + ((U32)length) + initval;

  u.ptr = key;
  if (HASH_BIG_ENDIAN && ((u.i & 0x3) == 0)) {
    const U32 *k = (const U32 *)key;         /* read 32-bit chunks */

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      b += k[1];
      c += k[2];
      burtle3_mix(a,b,c);
      length -= 12;
      k += 3;
    }

    /*----------------------------- handle the last (probably partial) block */
    /* 
     * "k[2]<<8" actually reads beyond the end of the string, but
     * then shifts out the part it's not allowed to read.  Because the
     * string is aligned, the illegal read is in the same word as the
     * rest of the string.  Every machine with memory protection I've seen
     * does it on word boundaries, so is OK with this.  But VALGRIND will
     * still catch it and complain.  The masking trick does make the hash
     * noticably faster for short strings (like English words).
     */
#ifndef VALGRIND

    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=k[2]&0xffffff00; b+=k[1]; a+=k[0]; break;
    case 10: c+=k[2]&0xffff0000; b+=k[1]; a+=k[0]; break;
    case 9 : c+=k[2]&0xff000000; b+=k[1]; a+=k[0]; break;
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=k[1]&0xffffff00; a+=k[0]; break;
    case 6 : b+=k[1]&0xffff0000; a+=k[0]; break;
    case 5 : b+=k[1]&0xff000000; a+=k[0]; break;
    case 4 : a+=k[0]; break;
    case 3 : a+=k[0]&0xffffff00; break;
    case 2 : a+=k[0]&0xffff0000; break;
    case 1 : a+=k[0]&0xff000000; break;
    case 0 : return c;              /* zero length strings require no mixing */
    }

#else  /* make valgrind happy */

    k8 = (const U8 *)k;
    switch(length)                   /* all the case statements fall through */
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=((U32)k8[10])<<8;  /* fall through */
    case 10: c+=((U32)k8[9])<<16;  /* fall through */
    case 9 : c+=((U32)k8[8])<<24;  /* fall through */
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=((U32)k8[6])<<8;   /* fall through */
    case 6 : b+=((U32)k8[5])<<16;  /* fall through */
    case 5 : b+=((U32)k8[4])<<24;  /* fall through */
    case 4 : a+=k[0]; break;
    case 3 : a+=((U32)k8[2])<<8;   /* fall through */
    case 2 : a+=((U32)k8[1])<<16;  /* fall through */
    case 1 : a+=((U32)k8[0])<<24; break;
    case 0 : return c;
    }

#endif /* !VALGRIND */

  } else {                        /* need to read the key one byte at a time */
    const U8 *k = (const U8 *)key;

    /*--------------- all but the last block: affect some 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += ((U32)k[0])<<24;
      a += ((U32)k[1])<<16;
      a += ((U32)k[2])<<8;
      a += ((U32)k[3]);
      b += ((U32)k[4])<<24;
      b += ((U32)k[5])<<16;
      b += ((U32)k[6])<<8;
      b += ((U32)k[7]);
      c += ((U32)k[8])<<24;
      c += ((U32)k[9])<<16;
      c += ((U32)k[10])<<8;
      c += ((U32)k[11]);
      burtle3_mix(a,b,c);
      length -= 12;
      k += 12;
    }

    /*-------------------------------- last block: affect all 32 bits of (c) */
    switch(length)                   /* all the case statements fall through */
    {
    case 12: c+=k[11];
    case 11: c+=((U32)k[10])<<8;
    case 10: c+=((U32)k[9])<<16;
    case 9 : c+=((U32)k[8])<<24;
    case 8 : b+=k[7];
    case 7 : b+=((U32)k[6])<<8;
    case 6 : b+=((U32)k[5])<<16;
    case 5 : b+=((U32)k[4])<<24;
    case 4 : a+=k[3];
    case 3 : a+=((U32)k[2])<<8;
    case 2 : a+=((U32)k[1])<<16;
    case 1 : a+=((U32)k[0])<<24;
             break;
    case 0 : return c;
    }
  }

  burtle3_final(a,b,c);
  return c;
}

// http://murmurhash.googlepages.com/
U32 MurmurHash2( const U8* key, U32 len, U32 seed )
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
U32 MurmurHash2CaseInsensitive( const U8* key, U32 len, U32 seed )
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

// http://www.azillionmonkeys.com/qed/hash.html
// Also known as "SuperFastHash"
#define get16bits(d) (*((const U16 *) (d)))
 U32 HsiehHash(const U8 * data, U32 len, U32 seed)
{
	U32 hash = len, tmp;
	int rem;

    if (len <= 0 || data == NULL) return 0;

    rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (;len > 0; len--) {
        hash  += get16bits (data);
        tmp    = (get16bits (data+2) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 2*sizeof (U16);
        hash  += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
        case 3: hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= data[sizeof (U16)] << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += *data;
                hash ^= hash << 10;
                hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return hash;
}

U32 HsiehHashCaseInsensitive(const U8 * data, U32 len, U32 seed)
{
	U32 hash = len, tmp;
	int rem;

    if (len <= 0 || data == NULL) return 0;

    rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (;len > 0; len--) {
        hash  += get16bits (data)|0x2020;
        tmp    = ((get16bits (data+2)|0x2020) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 2*sizeof (U16);
        hash  += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
        case 3: hash += get16bits (data)|0x2020;
                hash ^= hash << 16;
                hash ^= (data[sizeof (U16)]|0x20) << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data)|0x2020;
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += *data|0x20;
                hash ^= hash << 10;
                hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return hash;
}




#include "FolderCache.h"
#include "earray.h"
//#include "StashTable.h"
#include "StashSet.h"
#include "timing.h"
typedef U32 (*HashFunc)( const U8 *k, U32  length, U32  initval);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

void testHashFunc(HashFunc func, char *name)
{
	// Make an array of strings to hash
	const char **eaStrings=NULL;
	int *eaLengths=NULL;
	extern StashSet alloc_add_string_table;
	int count = stashSetGetValidElementCount(alloc_add_string_table);
	int hsize = pow2(count * 1.5);
	U32 hmask = hsize - 1;
	int hbytesize = hsize / 8 + 4;
	U32 *bits = calloc(hbytesize, 1);
	int i, j;
	int collisions;
	int timer = timerAlloc();
	eaSetCapacity(&eaStrings, count);
	eaiSetCapacity(&eaLengths, count);
	{
		StashSetIterator ielemIter;
		char *elem;
		stashSetGetIterator(alloc_add_string_table, &ielemIter);
		while (stashSetGetNextElement(&ielemIter, &elem))
		{
			eaPush(&eaStrings, elem);
			eaiPush(&eaLengths, (U32)strlen(elem));
		}
	}
	assert(count == eaSize(&eaStrings));

	// Do tests
	memset(bits, 0, hbytesize);
	collisions = 0;
	for (i=0; i<count; i++) {
		U32 v = func(eaStrings[i], eaLengths[i], DEFAULT_HASH_SEED) & hmask;
		if (TSTB(bits, v)) {
			collisions++;
		} else {
			SETB(bits, v);
		}
	}

	timerStart(timer);
	for (j=0; j<150; j++) {
		for (i=0; i<count; i++) {
			U32 v = func(eaStrings[i], eaLengths[i], DEFAULT_HASH_SEED) & hmask;
			SETB(bits, v);
		}
	}

	printf("%s : %d collisions (%.1f%%), %1.5fs\n", name, collisions, collisions * 100.f/count, timerElapsed(timer));

	// Clean up
	eaDestroy(&eaStrings);
	eaiDestroy(&eaLengths);
	free(bits);
	timerFree(timer);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void hashFunctionTest(int dummy)
{
#define test(f) testHashFunc(f, #f)
	test(burtlehash3);
	test(burtlehash3CaseInsensitive);
	test(MurmurHash2);
	test(MurmurHash2CaseInsensitive);
	test(HsiehHash);
	test(HsiehHashCaseInsensitive);
}

/*

// Various defines for optimal performance across all builds
// Probably not worth it, as the differences are fairly small compared to the overhead
#ifdef _XBOX
#	ifdef _FULLDEBUG
#		define stashDefaultHash					MurmurHash2
#		define stashDefaultHashCaseInsensitive	MurmurHash2CaseInsensitive
#	else
#		define stashDefaultHash					HsiehHash
#		define stashDefaultHashCaseInsensitive	HsiehHashCaseInsensitive
#	endif
#else
#	ifdef _FULLDEBUG
#		define stashDefaultHash					HsiehHash
#		define stashDefaultHashCaseInsensitive	HsiehHashCaseInsensitive
#	else
#		define stashDefaultHash					MurmurHash2
#		define stashDefaultHashCaseInsensitive	MurmurHash2CaseInsensitive
#	endif
#endif

PC, Full Debug:
burtlehash : 25662 collisions (31.1%), 2.33462s
burtlehash2bytes : 25705 collisions (31.2%), 3.21836s
hashlittle : 25630 collisions (31.1%), 2.19823s

burtlehash3 : 26088 collisions (31.3%), 2.43924s
MurmurHash2 : 25999 collisions (31.2%), 2.14444s
HsiehHash : 26127 collisions (31.3%), 2.04814s
burtlehash3CaseInsensitive : 29557 collisions (35.4%), 2.46978s
MurmurHash2CaseInsensitive : 29606 collisions (35.5%), 2.16574s
HsiehHashCaseInsensitive : 29593 collisions (35.5%), 2.05719s


PC, Debug
burtlehash : 25769 collisions (31.2%), 1.14425s
burtlehash2bytes : 25603 collisions (31.0%), 1.34857s
hashlittle : 25748 collisions (31.2%), 1.01541s

burtlehash3 : 26011 collisions (31.2%), 1.06239s
MurmurHash2 : 26125 collisions (31.3%), 0.86777s
HsiehHash : 25999 collisions (31.2%), 1.06402s
burtlehash3CaseInsensitive : 30127 collisions (36.1%), 1.01814s
MurmurHash2CaseInsensitive : 30083 collisions (36.1%), 0.91338s
HsiehHashCaseInsensitive : 29971 collisions (35.9%), 1.25107s



Xbox Full Debug
burtlehash : 24908 collisions (29.9%), 11.16938s
burtlehash2bytes : 24554 collisions (29.5%), 13.88971s
hashlittle : 24889 collisions (29.9%), 9.43813s

burtlehash3 : 24814 collisions (29.7%), 10.97626s
MurmurHash2 : 24862 collisions (29.8%), 7.78673s
SuperFastHash : 24777 collisions (29.7%), 7.93661s
burtlehash3CaseInsensitive : 28775 collisions (34.5%), 11.02921s
MurmurHash2CaseInsensitive : 28815 collisions (34.5%), 7.81422s
SuperFastHashCaseInsensitive : 28665 collisions (34.4%), 8.04681s


Xbox Debug
burtlehash : 24909 collisions (29.9%), 3.20911s
burtlehash2bytes : 24565 collisions (29.5%), 2.95990s
hashlittle : 24908 collisions (29.9%), 3.20925s

burtlehash3 : 24814 collisions (29.7%), 2.97150s
MurmurHash2 : 24862 collisions (29.8%), 2.81858s
HsiehHash : 24777 collisions (29.7%), 2.77893s
burtlehash3CaseInsensitive : 28775 collisions (34.5%), 3.10802s
MurmurHash2CaseInsensitive : 28815 collisions (34.5%), 2.83915s
HsiehHashCaseInsensitive : 28665 collisions (34.4%), 2.80154s

*/

