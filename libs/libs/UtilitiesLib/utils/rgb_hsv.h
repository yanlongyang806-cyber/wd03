#ifndef _RGB_HSV_H
#define _RGB_HSV_H

float srgbToLinear(U8 srgb);
U8 linearToSrgb(float lin);
int rgbToHsv(const Vec3 RGB,Vec3 HSV);
int hsvToRgb(const Vec3 HSV,Vec3 RGB);
int hsvToLinearRgb(const Vec3 HSV, Vec3 RGB);
void hsvToHsl(const F32 *hsv, F32 *hsl);
void hslToHsv(const F32 *hsl, F32 *hsv);
void hslToHsvKeepS(const F32 *hsl, F32 *hsv);
void hslToHsvSmartS(const Vec3 hsl, Vec3 hsv, F32 max);
void hsvAdd(const Vec3 hsv_a,const Vec3 hsv_b,Vec3 hsv_out);
void hsvShiftScale(const Vec3 hsv_a,const Vec3 hsv_b,Vec3 hsv_out); // Shifts the hue, scales the Saturation and Value
void hsvMakeLegal(Vec3 hsv, bool allow_negative);
void hsvLerp(const Vec3 hsv_a,const Vec3 hsv_b,F32 weight,Vec3 hsv_out);
void hueShiftRGB(Vec3 rgb_in, Vec3 rgb_out, F32 hue_shift);
void hsvShiftRGB(Vec3 rgb_in, Vec3 rgb_out, F32 hue_shift, F32 sat_shift, F32 val_shift);
#endif
