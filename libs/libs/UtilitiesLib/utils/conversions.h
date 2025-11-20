/***************************************************************************



***************************************************************************/
#ifndef CONVERSIONS_H__
#define CONVERSIONS_H__

AUTO_ENUM;
typedef enum MeasurementType {
	kMeasurementType_Base = 0,
	kMeasurementType_Metric,
	kMeasurementType_Yards
}MeasurementType;

AUTO_ENUM;
typedef enum MeasurementSize {
	kMeasurementSize_Base = 0,
	kMeasurementSize_Small,
	kMeasurementSize_Large
}MeasurementSize;

F32 BaseToMeasurement(F32 fBaseLength, MeasurementType eType, MeasurementSize eSize);
F32 MeasurementToBase(F32 fLength, MeasurementType eType, MeasurementSize eSize);

#endif