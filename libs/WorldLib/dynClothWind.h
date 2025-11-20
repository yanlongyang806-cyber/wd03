#ifndef _WIND_H
#define _WIND_H

void dynClothWindSetDir(Vec3 dir, F32 magnitude);
F32 dynClothWindGetRandom(Vec3 wind, F32 *windScale, F32 *windDirScale);

void dynClothWindSetRipplePeriod(F32 period);
void dynClothWindSetRippleSlope(F32 slope);

#endif // _WIND_H


