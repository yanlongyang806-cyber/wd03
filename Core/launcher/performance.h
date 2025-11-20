#ifndef _PERFORMANCE_H
#define _PERFORMANCE_H

#include "textparser.h"

typedef struct Packet Packet;

void perfSendTrackedInfo(Packet *pak);
void perfGetList(void);
void perfSendReset(void);

void perfLog(void);

//starts doing perfGetList once ever fInterval seconds in a background thread, properly criticalsectioned. If you're doing
//this, do NOT also call perfGetList() from the main thread
void perfBeginBackgroundPerfGetListThread(float fInterval);

extern int siHyperThreadingCPUConversionPoint;
extern int siHyperThreadingCPUConversionEquivalent;

static __forceinline void ConvertCPUUsageForHyperThreading(long *piPercent)
{

	if (*piPercent >= siHyperThreadingCPUConversionPoint)
	{
		*piPercent -= siHyperThreadingCPUConversionPoint;
		*piPercent *= (100 - siHyperThreadingCPUConversionEquivalent);
		*piPercent /= (100 - siHyperThreadingCPUConversionPoint);
		*piPercent += siHyperThreadingCPUConversionEquivalent;
		if (*piPercent > 100)
		{
			*piPercent = 100;
		}
	}
	else
	{
		*piPercent *= siHyperThreadingCPUConversionEquivalent;
		*piPercent /= siHyperThreadingCPUConversionPoint;
	}

}

static __forceinline void ConvertCPUUsageForHyperThreading_float(float *pPercent)
{

	if (*pPercent >= siHyperThreadingCPUConversionPoint)
	{
		*pPercent -= siHyperThreadingCPUConversionPoint;
		*pPercent *= (100 - siHyperThreadingCPUConversionEquivalent);
		*pPercent /= (100 - siHyperThreadingCPUConversionPoint);
		*pPercent += siHyperThreadingCPUConversionEquivalent;
		if (*pPercent > 100)
		{
			*pPercent = 100;
		}
	}
	else
	{
		*pPercent *= siHyperThreadingCPUConversionEquivalent;
		*pPercent /= siHyperThreadingCPUConversionPoint;
	}

}


#endif
