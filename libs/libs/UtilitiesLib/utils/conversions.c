/***************************************************************************



***************************************************************************/

#include "conversions.h"

#include "AutoGen/conversions_h_ast.h"
//The UOM structure (Unit of Measurement)
typedef struct UOM{ 
	F32 fUnitBase;

	F32 fUnitSmall;
	F32 fUnitLarge;
}UOM;

static UOM s_baseImperial = {1.0f,12.0f,0.000189393939f};
static UOM s_Metric = {0.3048f,30.48f,0.0003048f};
static UOM s_Yards = {0.33333333f,-1.f,-1.f};


F32 BaseToMeasurement(F32 fBaseLength, MeasurementType eType, MeasurementSize eSize)
{
	UOM *pMeasurement = NULL;

	switch(eType)
	{
	case kMeasurementType_Base:
		pMeasurement = &s_baseImperial;
		break;
	case kMeasurementType_Metric:
		pMeasurement = &s_Metric;
		break;
	case kMeasurementType_Yards:
		pMeasurement = &s_Yards;
	}

	if(pMeasurement)
	{
		switch(eSize)
		{
		case kMeasurementSize_Base:
			return fBaseLength * pMeasurement->fUnitBase;
		case kMeasurementSize_Large:
			return fBaseLength * (pMeasurement->fUnitLarge != -1.f ? pMeasurement->fUnitLarge : pMeasurement->fUnitBase);
		case kMeasurementSize_Small:
			return fBaseLength * (pMeasurement->fUnitSmall != -1.f ? pMeasurement->fUnitSmall : pMeasurement->fUnitBase);
		}
	}

	devassertmsg(0,"Invalid measurement request!");

	return fBaseLength;
}

F32 MeasurementToBase(F32 fLength, MeasurementType eType, MeasurementSize eSize)
{
	UOM *pMeasurement = NULL;

	switch(eType)
	{
	case kMeasurementType_Base:
		pMeasurement = &s_baseImperial;
		break;
	case kMeasurementType_Metric:
		pMeasurement = &s_Metric;
		break;
	case kMeasurementType_Yards:
		pMeasurement = &s_Yards;
	}

	if(pMeasurement)
	{
		switch(eSize)
		{
		case kMeasurementSize_Base:
			return fLength / pMeasurement->fUnitBase;
		case kMeasurementSize_Large:
			return fLength / (pMeasurement->fUnitLarge != -1.f ? pMeasurement->fUnitLarge : pMeasurement->fUnitBase);
		case kMeasurementSize_Small:
			return fLength / (pMeasurement->fUnitSmall != -1.f ? pMeasurement->fUnitSmall : pMeasurement->fUnitBase);
		}
	}

	devassertmsg(0,"Invalid measurement request!");

	return fLength;
}

#include "AutoGen/conversions_h_ast.c"