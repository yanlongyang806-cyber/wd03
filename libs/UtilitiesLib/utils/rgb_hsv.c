#include "rgb_hsv.h"
#include "mathutil.h"

#define RETURN_HWB(h, w, b, neg, success) {HWB[0] = 60*(h); HWB[1] = (w); HWB[2] = neg?-(b):(b); return success;} 
#define RETURN_HSV(h, s, v, neg, success) {HSV[0] = 60*(h); HSV[1] = (s); HSV[2] = neg?-(v):(v); return success;} 
#define RETURN_RGB(r, g, b, neg, success) {RGB[0] = (neg)?-(r):(r); RGB[1] = (neg)?-(g):(g); RGB[2] = (neg)?-(b):(b); return success;} 

#define UNDEFINED (-1.f/60)

__forceinline static float srgbToLinearf(float srgb)
{
	if (srgb <= 0.04045f) {
		return srgb / 12.92f;
	} else {
		return powf((srgb + 0.055f) / 1.055f, 2.4f);
	}
}

float srgbToLinear(U8 srgb)
{
	static const float linear[256] = {
		0.000000f, 0.000304f, 0.000607f, 0.000911f, 0.001214f, 0.001518f, 0.001821f, 0.002125f,
		0.002428f, 0.002732f, 0.003035f, 0.003347f, 0.003677f, 0.004025f, 0.004391f, 0.004777f,
		0.005182f, 0.005605f, 0.006049f, 0.006512f, 0.006995f, 0.007499f, 0.008023f, 0.008568f,
		0.009134f, 0.009721f, 0.010330f, 0.010960f, 0.011612f, 0.012286f, 0.012983f, 0.013702f,
		0.014444f, 0.015209f, 0.015996f, 0.016807f, 0.017642f, 0.018500f, 0.019382f, 0.020289f,
		0.021219f, 0.022174f, 0.023153f, 0.024158f, 0.025187f, 0.026241f, 0.027321f, 0.028426f,
		0.029557f, 0.030713f, 0.031896f, 0.033105f, 0.034340f, 0.035601f, 0.036889f, 0.038204f,
		0.039546f, 0.040915f, 0.042311f, 0.043735f, 0.045186f, 0.046665f, 0.048172f, 0.049707f,
		0.051269f, 0.052861f, 0.054480f, 0.056128f, 0.057805f, 0.059511f, 0.061246f, 0.063010f,
		0.064803f, 0.066626f, 0.068478f, 0.070360f, 0.072272f, 0.074214f, 0.076185f, 0.078187f,
		0.080220f, 0.082283f, 0.084376f, 0.086500f, 0.088656f, 0.090842f, 0.093059f, 0.095307f,
		0.097587f, 0.099899f, 0.102242f, 0.104616f, 0.107023f, 0.109462f, 0.111932f, 0.114435f,
		0.116971f, 0.119538f, 0.122139f, 0.124772f, 0.127438f, 0.130136f, 0.132868f, 0.135633f,
		0.138432f, 0.141263f, 0.144128f, 0.147027f, 0.149960f, 0.152926f, 0.155926f, 0.158961f,
		0.162029f, 0.165132f, 0.168269f, 0.171441f, 0.174647f, 0.177888f, 0.181164f, 0.184475f,
		0.187821f, 0.191202f, 0.194618f, 0.198069f, 0.201556f, 0.205079f, 0.208637f, 0.212231f,
		0.215861f, 0.219526f, 0.223228f, 0.226966f, 0.230740f, 0.234551f, 0.238398f, 0.242281f,
		0.246201f, 0.250158f, 0.254152f, 0.258183f, 0.262251f, 0.266356f, 0.270498f, 0.274677f,
		0.278894f, 0.283149f, 0.287441f, 0.291771f, 0.296138f, 0.300544f, 0.304987f, 0.309469f,
		0.313989f, 0.318547f, 0.323143f, 0.327778f, 0.332452f, 0.337164f, 0.341914f, 0.346704f,
		0.351533f, 0.356400f, 0.361307f, 0.366253f, 0.371238f, 0.376262f, 0.381326f, 0.386430f,
		0.391573f, 0.396755f, 0.401978f, 0.407240f, 0.412543f, 0.417885f, 0.423268f, 0.428691f,
		0.434154f, 0.439657f, 0.445201f, 0.450786f, 0.456411f, 0.462077f, 0.467784f, 0.473532f,
		0.479320f, 0.485150f, 0.491021f, 0.496933f, 0.502886f, 0.508881f, 0.514918f, 0.520996f,
		0.527115f, 0.533276f, 0.539480f, 0.545725f, 0.552011f, 0.558340f, 0.564712f, 0.571125f,
		0.577581f, 0.584078f, 0.590619f, 0.597202f, 0.603827f, 0.610496f, 0.617207f, 0.623960f,
		0.630757f, 0.637597f, 0.644480f, 0.651406f, 0.658375f, 0.665387f, 0.672443f, 0.679543f,
		0.686685f, 0.693872f, 0.701102f, 0.708376f, 0.715694f, 0.723055f, 0.730461f, 0.737911f,
		0.745404f, 0.752942f, 0.760525f, 0.768151f, 0.775822f, 0.783538f, 0.791298f, 0.799103f,
		0.806952f, 0.814847f, 0.822786f, 0.830770f, 0.838799f, 0.846873f, 0.854993f, 0.863157f,
		0.871367f, 0.879622f, 0.887923f, 0.896269f, 0.904661f, 0.913099f, 0.921582f, 0.930111f,
		0.938686f, 0.947307f, 0.955974f, 0.964686f, 0.973445f, 0.982251f, 0.991102f, 1.000000f,
	};

	return linear[srgb];
}

U8 linearToSrgb(float lin)
{
	if (lin <= 0.0031308f) {
		return (unsigned char)(lin * 12.92f);
	} else {
		return (unsigned char)(1.055f * powf(lin, 1.0f/2.4f) - 0.055f);
	}
}

// Theoretically, hue 0 (pure red) is identical to hue 6 in these transforms. Pure 
// red always maps to 6 in this implementation. Therefore UNDEFINED can be 
// defined as 0 in situations where only unsigned numbers are desired.

//when using the functions, Hue is passed in and returned on a scale of [0-360],
//though in the code it is scaled to [0-6]

int rgbToHwb(Vec3 RGB,Vec3 HWB)
{
	// RGB are each on [0, 1]. W and B are returned on [0, 1] and H is  
	// returned on [0, 360]. Exception: H is returned UNDEFINED if W == 1 - B.  
	float	R = ABS(RGB[0]), G = ABS(RGB[1]), B = ABS(RGB[2]), w, v, b, f;  
	bool	neg = (RGB[0] + RGB[1] + RGB[2]) < 0;
	int		i;  

	if (R < G)  { w = R;	v = G; }
	else		{ w = G;	v = R; }
	w = MIN(MIN(R,G),B);
	v = MAX(MAX(R,G),B);
	b = 1 - v;  
	if (v == w) RETURN_HWB(UNDEFINED, w, b, neg, 0);  
	f = (R == w) ? G - B : ((G == w) ? B - R : R - G);  
	i = (R == w) ? 3 : ((G == w) ? 5 : 1);  
	RETURN_HWB(i - f /(v - w), w, b, neg, 1);  
}

int hwbToRgb(Vec3 HWB,Vec3 RGB) 
{
	// H is given on [0, 360] or UNDEFINED. W and B are given on [0, 1].  
	// RGB are each returned on [0, 1].  
	float	h = HWB[0] * 1.f/60.f, w = HWB[1], b = ABS(HWB[2]), v, n, f;  
	bool	neg = HWB[2] < 0;
	int		i;  

	v = 1 - b;  
	if (h == UNDEFINED) RETURN_RGB(v, v, v, neg, 0);  
	i = floor(h);  
	f = h - i;  
	if (i & 1) f = 1 - f; // if i is odd  
	n = w + f * (v - w); // linear interpolation between w and v  
	switch (i)
	{  
		case 6:  
		case 0: RETURN_RGB(v, n, w, neg, 1);  
		case 1: RETURN_RGB(n, v, w, neg, 1);  
		case 2: RETURN_RGB(w, v, n, neg, 1);  
		case 3: RETURN_RGB(w, n, v, neg, 1);  
		case 4: RETURN_RGB(n, w, v, neg, 1);  
		case 5: RETURN_RGB(v, w, n, neg, 1);  
	}
	RETURN_RGB(v, v, v, neg, 0);
}

int rgbToHsv(const Vec3 RGB, Vec3 HSV)
{ 
	// RGB are each on [0, 1]. S and V are returned on [0, 1] and H is  
	// returned on [0, 360].
	float R = ABS(RGB[0]), G = ABS(RGB[1]), B = ABS(RGB[2]), v, x, f;  
	bool neg = (RGB[0] + RGB[1] + RGB[2]) < 0;
	int i;

	x = MIN(MIN(R,G),B);
	v = MAX(MAX(R,G),B);
	if(v == x) RETURN_HSV(0, 0, v, neg, 0);  
	f = (R == x) ? G - B : ((G == x) ? B - R : R - G);  
	i = (R == x) ? 3 : ((G == x) ? 5 : 1);
	RETURN_HSV(i - f /(v - x), (v - x)/v, v, neg, 1);  
}

void hsvToHsl(const Vec3 hsv, Vec3 hsl)
{
	hsl[0] = hsv[0];
	hsl[2] = (2.0 - hsv[1]) * hsv[2];
	hsl[1] = hsv[1] * hsv[2];
	if (hsl[2] == 2)
	{
		hsl[1] = 0;
	}
	else if (hsl[2] == 0)
	{
		hsl[1] = 1;
	}
	else
	{
		hsl[1] /= (hsl[2] <= 1) ? hsl[2] : (2.0 - hsl[2]);
	}
	hsl[2] /= 2.0;
}

void hslToHsv(const Vec3 hsl, Vec3 hsv)
{
	F32 twoLum = 2.0 * hsl[2];
	F32 modifiedSat;
	hsv[0] = hsl[0];
	modifiedSat = hsl[1] * ((twoLum <= 1) ? twoLum : 2.0 - twoLum);
	hsv[2] = (twoLum + modifiedSat) / 2.0;
	if (twoLum + modifiedSat)
	{
		hsv[1] = (2.0 * modifiedSat) / (twoLum + modifiedSat);
		if ((hsv[1] > 1) || (hsv[1] < 0))
		{
			hsv[2] = hsl[2];
			hsv[1] = 0;
		}
	}
	else
	{
		hsv[1] = 0;
	}
}

void hslToHsvKeepS(const Vec3 hsl, Vec3 hsv)
{
	F32 twoLum = 2.0 * hsl[2];
	F32 modifiedSat;
	hsv[0] = hsl[0];
	modifiedSat = hsl[1] * ((twoLum <= 1) ? twoLum : 2.0 - twoLum);
	hsv[2] = (twoLum + modifiedSat) / 2.0;
	if (twoLum + modifiedSat)
	{
		hsv[1] = (2.0 * modifiedSat) / (twoLum + modifiedSat);
		if (hsv[1] != hsl[1])
		{
			F32 change = hsv[1] - hsl[1];
			hsv[2] = ((hsv[1] + change) / change) * hsl[2];
			hsv[1] = 1;
		}
	}
	else
	{
		hsv[1] = hsl[1];
	}
}

void hslToHsvSmartS(const Vec3 hsl, Vec3 hsv, F32 max)
{
	if (max == 1)
	{
		hslToHsv(hsl, hsv);
	}
	else
	{
		hslToHsvKeepS(hsl, hsv);
	}
}

int hsvToLinearRgb(const Vec3 HSV, Vec3 RGB)
{
	int ret = hsvToRgb(HSV, RGB);
	if (ret) {
        RGB[0] = srgbToLinearf(RGB[0]);
        RGB[1] = srgbToLinearf(RGB[1]);
        RGB[2] = srgbToLinearf(RGB[2]);
	}

	return ret;
}

int hsvToRgb(const Vec3 HSV, Vec3 RGB)
{ 

	// H is given on [0, 360] or UNDEFINED. S and V are given on [0, 1].  
	// RGB are each returned on [0, 1].  
	float h = HSV[0] / 60.f, s = HSV[1], v = ABS(HSV[2]), m, n, f;  
	bool neg = HSV[2] < 0;
	int i;  

	if(h == UNDEFINED) RETURN_RGB(v, v, v, neg, 0);  
	i = floor(h);  
	f = h - i;  
	if(!(i & 1)) f = 1 - f; // if i is even  
	m = v * (1 - s);  
	n = v * (1 - s * f);  
	switch (i)
	{  
		case 6:  
		case 0: RETURN_RGB(v, n, m, neg, 1);  
		case 1: RETURN_RGB(n, v, m, neg, 1); 
		case 2: RETURN_RGB(m, v, n, neg, 1); 
		case 3: RETURN_RGB(m, n, v, neg, 1); 
		case 4: RETURN_RGB(n, m, v, neg, 1); 
		case 5: RETURN_RGB(v, m, n, neg, 1); 
	}  
	RETURN_RGB(v, v, v, neg, 0);  
} 


void hsvAdd(const Vec3 hsv_a,const Vec3 hsv_b,Vec3 hsv_out)
{
	hsv_out[0] = fmod(hsv_a[0] + hsv_b[0] + 3600,360);
	hsv_out[1] = MINMAX(hsv_a[1] + hsv_b[1],0,1);
	hsv_out[2] = MINMAX(hsv_a[2] + hsv_b[2],0,1);
}

void hsvLerp(const Vec3 hsv_a,const Vec3 hsv_b,F32 weight,Vec3 hsv_out)
{
	Vec3 rgb_a, rgb_b, rgb_out;
	F32 inv_weight;

	weight = CLAMP(weight, 0, 1);
	inv_weight = 1.f - weight;

	hsvToRgb(hsv_a, rgb_a);
	hsvToRgb(hsv_b, rgb_b);

	scaleVec3(rgb_a, inv_weight, rgb_out);
	scaleAddVec3(rgb_b, weight, rgb_out, rgb_out);

	rgbToHsv(rgb_out, hsv_out);

	hsv_out[1] = CLAMP(hsv_out[1], 0, 1);
	MAX1(hsv_out[2], 0);
}

// Shifts the hue, scales the Saturation and Value
void hsvShiftScale(const Vec3 hsv_a,const Vec3 hsv_b,Vec3 hsv_out)
{
	if (hsv_a[0]==UNDEFINED || hsv_b[0]==UNDEFINED) {
		hsv_out[0] = UNDEFINED;
	} else {
		hsv_out[0] = fmod(hsv_a[0] + hsv_b[0] + 3600,360);
	}
	hsv_out[1] = MINMAX(hsv_a[1] * hsv_b[1],0,1);
	hsv_out[2] = MINMAX(hsv_a[2] * hsv_b[2],0,1);
}

void hsvMakeLegal(Vec3 hsv, bool allow_negative)
{
	while (hsv[0] < 0.f)
		hsv[0] += 360.f;
	while (hsv[0] >= 360.f)
		hsv[0] -= 360.f;

	hsv[1] = CLAMPF32(hsv[1], 0, 1);

	if (!allow_negative)
 		MAX1(hsv[2], 0);
}

void hueShiftRGB(Vec3 rgb_in, Vec3 rgb_out, F32 hue_shift)
{
	Vec3 hsv;
	rgbToHsv(rgb_in, hsv);
	hsv[0] += hue_shift;
	hsvMakeLegal(hsv, false);
	hsvToRgb(hsv, rgb_out);
}

void hsvShiftRGB(Vec3 rgb_in, Vec3 rgb_out, F32 hue_shift, F32 sat_shift, F32 val_shift)
{
	Vec3 hsv;
	rgbToHsv(rgb_in, hsv);
	hsv[0] += hue_shift;
	hsv[1] += sat_shift;
	hsv[2] += val_shift * 255.0;
	hsvMakeLegal(hsv, false);
	hsvToRgb(hsv, rgb_out);
}
