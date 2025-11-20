#pragma once


typedef struct IntFIFO IntFIFO;

IntFIFO *IntFIFO_Create(int iInitialCapacity);
void IntFIFO_Push(IntFIFO *pBuf, U32 iInt);
bool IntFIFO_Get(IntFIFO *pBuf, U32 *pOut);
void IntFIFO_Clear(IntFIFO *pBuf);
void IntFIFO_Destroy(IntFIFO **ppBuf);

///use this carefully and sparingly, it's not fast like the other commands
void IntFIFO_FindAndRemove(IntFIFO *pBuf, U32 iInt);

typedef struct PointerFIFO PointerFIFO;

PointerFIFO *PointerFIFO_Create(int iInitialCapacity);
void PointerFIFO_Push(PointerFIFO *pBuf, void *pPtr);
bool PointerFIFO_Get(PointerFIFO *pBuf, void **ppOut);
void PointerFIFO_Clear(PointerFIFO *pBuf);
void PointerFIFO_Destroy(PointerFIFO **ppBuf);
int PointerFIFO_Count(PointerFIFO *pBuf);
bool PointerFIFO_Peek(PointerFIFO *pBuf, void **ppOut);

//Create an internal FloatMaxTracker which tracks the # elements in the FIFO whenever it changes
void PointerFIFO_EnableMaxTracker(PointerFIFO *pBuf);
float PointerFIFO_GetMaxCount(PointerFIFO *pBuf, U32 iStartingTime);
