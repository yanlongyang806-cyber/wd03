#include "UnitSpec.h"

const UnitSpec byteSpec[] = 
{
	UNITSPEC("bytes",	1                        , 0                         ),
	UNITSPEC("byte",	1                        , 1                         ),
	UNITSPEC("bytes",	1                        , 2                         ),
	UNITSPEC("KB",		(S64)1024                , (S64)1000                ),
	UNITSPEC("MB",		(S64)1024*1024           , (S64)1000*1024           ),
	UNITSPEC("GB",		(S64)1024*1024*1024      , (S64)1000*1024*1024      ),
//	UNITSPEC("TB",		(S64)1024*1024*1024*1024 , (S64)1000*1024*1024*1024 ),
	{0},
};

// This allows 4 digits if necessary, such as "1022.45K" and "8423.10MB"
const UnitSpec lazyByteSpec[] = 
{
	UNITSPEC("bytes",	1                        , 0                         ),
	UNITSPEC("byte",	1                        , 1                         ),
	UNITSPEC("bytes",	1                        , 2                         ),
	UNITSPEC("KB",		(S64)1024                , (S64)1024                 ),
	UNITSPEC("MB",		(S64)1024*1024           , (S64)1024*1024            ),
	UNITSPEC("GB",		(S64)1024*1024*1024      , (S64)1000*1024*1024*10    ),
//	UNITSPEC("TB",		(S64)1024*1024*1024*1024 , (S64)1000*1024*1024*1024  ),
	{0},
};

const UnitSpec metricSpec[] = 
{
	UNITSPEC("",	1                     , 0                        ),
	UNITSPEC("K",	(S64)1000             , (S64)1000                ),
	UNITSPEC("M",	(S64)1000*1000        , (S64)1000*1000           ),
	UNITSPEC("G",	(S64)1000*1000*1000   , (S64)1000*1000*1000      ),
	{0},
};

const UnitSpec kbyteSpec[] = 
{
	UNITSPEC("KB",	1                     , 1                        ),
	UNITSPEC("MB",	(S64)1024             , (S64)1000                ),
	UNITSPEC("GB",	(S64)1024*1024        , (S64)1000*1024           ),
	UNITSPEC("TB",	(S64)1024*1024*1024   , (S64)1000*1024*1024      ),
	{0},
};

const UnitSpec timeSpec[] = 
{
	UNITSPEC("sec",	1         , 1            ),
	UNITSPEC("min",	60        , 60           ),
	UNITSPEC("hr",	60*60     , 60*60        ),
	UNITSPEC("day",	24*60*60  , 24*60*60     ),
	{0},
};


const UnitSpec* usFindProperUnitSpec(const UnitSpec* specs, S64 size)
{
	int i;
	size = size<0?-size:size;
	for(i = 0; specs[i].unitName; i++)
	{
		if(specs[i].switchBoundary > size)
			return &specs[max(0, i-1)];
	}
	return &specs[i-1];
}

char *friendlyUnitBuf_s(const UnitSpec *spec, S64 num, char* buf, size_t buf_size)
{
	spec = usFindProperUnitSpec(spec, num);

	if(1 != spec->unitBoundary)
		sprintf_s(SAFESTR2(buf), "%1.2f %s", (float)num*spec->ooUnitBoundary, spec->unitName);
	else
		sprintf_s(SAFESTR2(buf), "%1.f %s", (float)num*spec->ooUnitBoundary, spec->unitName);
	return buf;
}

char *friendlyUnitAlignedBuf_s(const UnitSpec *spec, S64 num, char* buf, size_t buf_size) // Aligns the numbers (e.g. "10.3 MB   ", "7 bytes")
{
	spec = usFindProperUnitSpec(spec, num);

	if(1 != spec->unitBoundary)
		sprintf_s(SAFESTR2(buf), "%1.2f %-5s", (float)num*spec->ooUnitBoundary, spec->unitName);
	else
		sprintf_s(SAFESTR2(buf), "%1.f %-5s", (float)num*spec->ooUnitBoundary, spec->unitName);
	return buf;
}


char *friendlyUnit(const UnitSpec *spec, S64 num)
{
	static char buf[128];
	friendlyUnitBuf(spec, num, buf);
	return buf;
}
