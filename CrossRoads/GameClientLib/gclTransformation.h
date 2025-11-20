#ifndef _GCL_TRANSFORMATION_
#define _GCL_TRANSFORMATION_

typedef struct Entity Entity;
typedef struct PCPart PCPart;
typedef struct StashTableImp *StashTable;

AUTO_ENUM;
typedef enum ETransformColorOrigin
{
	ETransformColorOrigin_NONE = 0,				ENAMES(none)
	ETransformColorOrigin_SOURCE,				ENAMES(source)
	ETransformColorOrigin_DEST,					ENAMES(destination)
	ETransformColorOrigin_COUNT
} ETransformColorOrigin;


AUTO_STRUCT;
typedef struct TransformationPart
{
	const char *pchName;						AST(NAME("Name") POOL_STRING)
	
	ETransformColorOrigin color_0;
	ETransformColorOrigin color_1;				
	ETransformColorOrigin color_2;
	ETransformColorOrigin color_3;

	PCPart		*pPCPart;						AST(NAME("Part"))
	
	AST_STOP
	int bHasColorDest;							
	int bHasColorSrc;

} TransformationPart;

AUTO_STRUCT;
typedef struct TransformationGeoMap
{
	const char *pchCGeoName;					AST(NAME("CGeoName") POOL_STRING)
	const char *pchTransformPartName;			AST(NAME("PartName") POOL_STRING)

	TransformationPart *pPart;					NO_AST
} TransformationGeoMap;

AUTO_STRUCT;
typedef struct TransformationPartMapsDef
{
	// the list of defined parts
	TransformationPart **eaParts;				AST( NAME("TransformPart"))

	TransformationGeoMap **eaCGeoMaps;			AST( NAME("GeoMap"))

	AST_STOP

	StashTable  geoPartMap;

} TransformationPartMapsDef;

void gclTransformation_OncePerFrame();
void gclTransformation_Update(Entity *e, F32 fDTime);
void gclTransformation_BeginTransformation(Entity *pEntity);



#endif