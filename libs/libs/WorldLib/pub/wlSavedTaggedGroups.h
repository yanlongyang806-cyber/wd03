#pragma once
GCC_SYSTEM
// This file was initially started in order to get information about certain tagged objects post-binning.
// During the binning process, the layers are traversed and any objects tagged with special strings will
//	become enumerated and saved off into an intermediate file that can be accessed later. 



typedef struct GroupDef GroupDef;
typedef struct GroupInfo GroupInfo;
typedef struct GroupInheritedInfo GroupInheritedInfo;
typedef U32* EArray32Handle;

typedef enum ETaggedObjectType
{
	ETagGleanObjectType_NONE = 0,
	ETagGleanObjectType_TRAFFIC_STOPSIGN,
	ETagGleanObjectType_TRAFFIC_LIGHT,
	ETagGleanObjectType_TRAFFIC_ONEWAY_SIGN,
	ETagGleanObjectType_COUNT
} ETaggedObjectType;

AUTO_STRUCT;
typedef struct TaggedObjectData
{
	ETaggedObjectType	eType;					AST(INT)
	Vec3				vPos;
	Quat				qRot;

	bool				randomedChild;			NO_AST
} TaggedObjectData;


AUTO_STRUCT;
typedef struct SavedTaggedGroups
{
	TaggedObjectData **eaTrafficObjects;
	
	TaggedObjectData **eaParentRandomChild;		NO_AST

} SavedTaggedGroups;


// Use at binning time only
bool wlGenerateAndSaveTaggedGroups(const char *filename);

SavedTaggedGroups* wlLoadSavedTaggedGroupsForCurrentMap();

TaggedObjectData* wlTGDFindNearestObject(SA_PARAM_OP_VALID SavedTaggedGroups *data, SA_PARAM_NN_VALID const Vec3 vPos, F32 fMaxDist, SA_PARAM_OP_VALID const EArray32Handle eaTypes);