#define LowBitMask (0x000003)

// If we bring back flags, change this back
//#define GetIndexedTable(earray) ((void*)((earray)->tableandflags & ~LowBitMask))
#define GetIndexedTable(earray) ((void*)(earray)->tableandflags)

static __forceinline void *eaIndexedGetTable_inline(cEArrayHandle *handle)
{
	EArray* pArray;
	if (!(*handle)) return NULL;
	pArray = EArrayFromHandle(*handle);

	return GetIndexedTable(pArray);
}

