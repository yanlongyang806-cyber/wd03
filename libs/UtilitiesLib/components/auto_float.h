#pragma once

#define AUTO_FLOAT(floatName, initValue) static float floatName; static bool floatName##AUTOFLOATRegistered = false; int floatName##AUTOFLOATMAGICVARIABLE = floatName##AUTOFLOATRegistered ? 0 : RegisterAutoFloat(&floatName, #floatName, initValue, &floatName##AUTOFLOATRegistered)
int RegisterAutoFloat(float *pFloat, char *pFloatName, float fInitValue, bool *pRegisteredVar);
