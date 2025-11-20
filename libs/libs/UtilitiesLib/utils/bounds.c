#include "bounds.h"

Vec4H mulBoundMasks[8];

AUTO_RUN;
void initMulBoundsMask(void)
{
	// this goofy order is legacy from the old mulBounds.  Figured it was just best to maintain it
	setIVec4H(mulBoundMasks[0], 0,  0,  0,  0);
	setIVec4H(mulBoundMasks[1], 0xffffffff,  0,  0,  0);
	setIVec4H(mulBoundMasks[2], 0,  0xffffffff,  0,  0);
	setIVec4H(mulBoundMasks[3], 0xffffffff,  0xffffffff,  0,  0);
	setIVec4H(mulBoundMasks[4], 0,  0,  0xffffffff,  0);
	setIVec4H(mulBoundMasks[5], 0xffffffff,  0,  0xffffffff,  0);
	setIVec4H(mulBoundMasks[6], 0,  0xffffffff,  0xffffffff,  0);
	setIVec4H(mulBoundMasks[7], 0xffffffff,  0xffffffff,  0xffffffff,  0);
}