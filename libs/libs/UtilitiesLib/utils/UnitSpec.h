#pragma once
GCC_SYSTEM

#include "stdtypes.h"

#define UNITSPEC(name, unitBoundary, switchBoundary) \
    { name, 1.f/(float)(unitBoundary), unitBoundary, switchBoundary }

typedef struct {
	char* unitName;		// Friendly name (i.e. KB)
    F32 ooUnitBoundary;	// What size this unit is (i.e. 1/1024)
	S64 unitBoundary;	// What size this unit is (i.e. 1024)
	S64 switchBoundary; // Where to switch to using this unit in displays (i.e. 1000, in order to get "1023" to display as "0.9 KB")
} UnitSpec;

extern const UnitSpec byteSpec[];
extern const UnitSpec kbyteSpec[];
extern const UnitSpec timeSpec[];
extern const UnitSpec metricSpec[];
extern const UnitSpec lazyByteSpec[];

const UnitSpec* usFindProperUnitSpec(const UnitSpec* specs, S64 size);

char *friendlyUnit(const UnitSpec *spec, S64 num);
char *friendlyUnitBuf_s(const UnitSpec *spec, S64 num, char* buf, size_t buf_size);
char *friendlyUnitAlignedBuf_s(const UnitSpec *spec, S64 num, char* buf, size_t buf_size); // Aligns the numbers (e.g. "10 MB   ", " 7 bytes")

#define friendlyUnitBuf(spec, num, buf) friendlyUnitBuf_s(spec, num, SAFESTR(buf))
#define friendlyUnitAlignedBuf(spec, num, buf) friendlyUnitAlignedBuf_s(spec, num, SAFESTR(buf))

#define friendlyBytes(numbytes) friendlyUnit(byteSpec, numbytes)
#define friendlyBytesBuf(numbytes, buf) friendlyUnitBuf_s(byteSpec, numbytes, SAFESTR(buf))
#define friendlyBytesAlignedBuf(numbytes, buf) friendlyUnitAlignedBuf_s(byteSpec, numbytes, SAFESTR(buf))

#define friendlyLazyBytes(numbytes) friendlyUnit(lazyByteSpec, numbytes)
#define friendlyLazyBytesBuf(numbytes, buf) friendlyUnitBuf_s(lazyByteSpec, numbytes, SAFESTR(buf))
#define friendlyLazyBytesAlignedBuf(numbytes, buf) friendlyUnitAlignedBuf_s(lazyByteSpec, numbytes, SAFESTR(buf))
