#ifndef AUTO_PLACEMENT_COMMON_H
#define AUTO_PLACEMENT_COMMON_H
GCC_SYSTEM

typedef struct TrackerHandle TrackerHandle;
typedef struct AutoPlacementObject AutoPlacementObject;
typedef struct ParseTable ParseTable;
typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct AutoPlacementSet AutoPlacementSet;
typedef struct GameEncounter GameEncounter;
typedef struct MaterialData MaterialData;
typedef struct PotentialLineSegment PotentialLineSegment;
typedef struct WorldAutoPlacementProperties WorldAutoPlacementProperties;
typedef struct OldStaticEncounter OldStaticEncounter;

typedef enum eConnectionType
{
	eConnectionType_NONE = 0,
	eConnectionType_EDGE,
	eConnectionType_EXTERNAL_CORNER,
	eConnectionType_INTERNAL_CORNER,
	eConnectionType_COUNT
} eConnectionType;

typedef struct PotentialLineSegment
{
	Vec3		vPt1;
	Vec3		vPt2;

	U32			pt1Type : 4;
	U32			pt2Type : 3;
	U32			isKosher : 1;
} PotentialLineSegment;


AUTO_STRUCT;
typedef struct AutoPlacementExpressionData
{
	F32		elevation;										AST( NAME("Elevation") )
	F32		slope;											AST( NAME("Slope") )
	const char*	groundMaterialName;							AST( NAME("GroundMaterialName") POOL_STRING )
	const char* materialPhysicalProperty;					AST( NAME("MaterialPhysicalProperty") POOL_STRING)

	PotentialLineSegment**	eaPotentialLineSegmentList;		NO_AST
	const PotentialLineSegment*	pCurrentLineSegment;		NO_AST
	Vec3					currentPotentialPos;			NO_AST
	Vec3					ground_normal;					NO_AST
	GameEncounter**			eaNearbyEncounters;				NO_AST
	OldStaticEncounter**	eaNearbyOldEncounters;			NO_AST
} AutoPlacementExpressionData;

ExprContext* getAutoPlacementExprContext( );


AUTO_STRUCT;
typedef struct AutoPlacementVolume
{
	Vec3 vPos;

	// cube values
	Vec3 vMin;
	Vec3 vMax;
	Quat qInvRot; // the inverse of the rotation

	// sphere
	F32 fRadius; // radius is still valid when as a cube

	const WorldAutoPlacementProperties	*pProperties;		NO_AST

	bool bAsCube; // true if volume is a cube
} AutoPlacementVolume;

extern ParseTable parse_AutoPlacementVolume[];
#define TYPE_parse_AutoPlacementVolume AutoPlacementVolume

bool apvPointInVolume(const AutoPlacementVolume *pVolume, const Vec3 vPt);


AUTO_STRUCT;
typedef struct AutoPlacementParams
{
	AutoPlacementVolume** ppAutoPlacementVolumes;
	
	AutoPlacementSet *pPlacementProperties;
	
} AutoPlacementParams;


AUTO_STRUCT;
typedef struct AutoPlacePosition
{
	Vec3	vPos;
	Vec3	vNormal;
	S16		groupIdx;
	S16		objectIdx;
	
} AutoPlacePosition;

AUTO_STRUCT;
typedef struct AutoPlaceObjectData
{
	AutoPlacePosition **pPositionList; 

} AutoPlaceObjectData;


void performAutoPlacement(const TrackerHandle *handle, const AutoPlacementSet *pAutoPlacementSet);

#endif 