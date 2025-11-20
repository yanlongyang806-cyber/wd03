#include "MemoryMonitor.h"

const static U32 pattern[] = {
	0xaaaaaaaa,
	0xcccccccc,
	0xf0f0f0f0,
	0xff00ff00,
	0xffff0000,
	0x00000000,
	0xffffffff,
	0x0000ffff,
	0x00ff00ff,
	0x0f0f0f0f,
	0x33333333,
	0x55555555,
};
const U32 otherpattern = 0x01234567;

static int testDataLine(U32 *p)
{
	int i;
	int ret = 0;
	for (i=0; i<ARRAY_SIZE(pattern); i++)
	{
		U32 t;
		*p++ = pattern[i];
		*p-- = otherpattern;
		t = *p;
		if (t != pattern[i])
		{
			ret = -1;
			printf("Memory error @%p : wrote %x read %x\n",
				p, pattern[i], t);
		}
	}
	return ret;
}

static int testPattern(U32 *p, size_t size, U32 pat)
{
	int ret=0;
	U32 i;
	U32 *origp = p;
	for (i=0; i<size/sizeof(U32); i++)
	{
		*p++ = pat;
	}
	p = origp;
	for (i=0; i<size/sizeof(U32); i++)
	{
		U32 t = *p;
		if (t != pat)
		{
			ret = -2;
			printf("Memory error @%p : wrote %x read %x\n",
				p, pat, t);
		}
		p++;
	}
	return ret;
}

static int testWalk1(U32 *p, size_t size)
{
	int ret=0;
	U32 i;
	U32 *origp = p;
	for (i=0; i<size/sizeof(U32); i++)
	{
		p[i] = 1 << (i % 32);
	}
	for (i=0; i<size/sizeof(U32); i++)
	{
		U32 t = p[i];
		U32 pat = 1 << (i % 32);
		if (t != pat)
		{
			ret = -3;
			printf("Memory error @%p : wrote %x read %x\n",
				p + i, pat, t);
		}
	}
	return ret;
}

static int testWalk2(U32 *p, size_t size)
{
	int ret=0;
	U32 i;
	U32 *origp = p;
	for (i=0; i<size/sizeof(U32); i++)
	{
		p[i] = i;
	}
	for (i=0; i<size/sizeof(U32); i++)
	{
		U32 t = p[i];
		U32 pat = i;
		if (t != pat)
		{
			ret = -3;
			printf("Memory error @%p : wrote %x read %x\n",
				p + i, pat, t);
		}
	}
	return ret;
}

static int testWalk3(U32 *p, size_t size)
{
	int ret=0;
	U32 i;
	U32 *origp = p;
	for (i=0; i<size/sizeof(U32); i++)
	{
		p[i] = ~i;
	}
	for (i=0; i<size/sizeof(U32); i++)
	{
		U32 t = p[i];
		U32 pat = ~i;
		if (t != pat)
		{
			ret = -3;
			printf("Memory error @%p : wrote %x read %x\n",
				p + i, pat, t);
		}
	}
	return ret;
}


int memTestRange(void *p, size_t size)
{
	int ret = 0;
	if (ret == 0 && size >= sizeof(void*)*2)
		ret = testDataLine(p);
	if (ret == 0)
		ret = testPattern(p, size, 0x00000000);
	if (ret == 0)
		ret = testPattern(p, size, 0xffffffff);
	if (ret == 0)
		ret = testPattern(p, size, 0x55555555);
	if (ret == 0)
		ret = testPattern(p, size, 0xaaaaaaaa);
	if (ret == 0)
		ret = testWalk1(p, size);
	if (ret == 0)
		ret = testWalk2(p, size);
	if (ret == 0)
		ret = testWalk3(p, size);
	return ret;	
}
