#pragma once

char* ReadPNG_FileEx(const char *pFileName, int *width, int *height, bool flipVerticalAxis);
#define ReadPNG_File(pFileName, width, height) ReadPNG_FileEx(pFileName, width, height, false)
