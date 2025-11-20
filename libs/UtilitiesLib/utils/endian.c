#include "endian.h"
#include "structinternals.h"
#include "structinternals_h_ast.h"

EndianTest endian_test = {1};

void testEndian(void)
{
	EndianTest etest;
	EndianTest etest2;
	printf("This system is : %s", isBigEndian()?"BIG endian":"Little Endian");

#define TEST(val) \
	etest.intValue = val;	\
	etest2.intValue = endianSwapU32(etest.intValue);	\
	printf(" int: 0x%08x = bytes: %02x %02x %02x %02x  swapped = bytes: %02x %02x %02x %02x\n",	\
		etest.intValue, (int)etest.bytes[0], (int)etest.bytes[1], (int)etest.bytes[2], (int)etest.bytes[3],	\
		(int)etest2.bytes[0], (int)etest2.bytes[1], (int)etest2.bytes[2], (int)etest2.bytes[3]);	\

	TEST(1);
	TEST(-1);
	TEST(0x12345678);
	TEST(-0x12345678);
	TEST(0xF2345678);
}


void endianSwapStruct(ParseTable pti[], void *structptr)
{
	int i;
	if (structptr == NULL)
		return;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		endianswap_autogen(pti, i, structptr, 0);
	}
}
