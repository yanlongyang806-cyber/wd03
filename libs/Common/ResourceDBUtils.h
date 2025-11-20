#pragma once

AUTO_ENUM;
typedef enum eResourceDBResourceType
{
	RESOURCETYPE_UNCALCULATED,
	RESOURCETYPE_UNSUPPORTED,

	RESOURCETYPE_ZONEMAPINFO,
	RESOURCETYPE_MESSAGE,	
	RESOURCETYPE_MISSIONDEF,
	RESOURCETYPE_CONTACTDEF,
	RESOURCETYPE_PLAYERCOSTUME,
	RESOURCETYPE_ITEMDEF,
	RESOURCETYPE_REWARDTABLE,

	RESOURCETYPE_LAST, EIGNORE
} eResourceDBResourceType;

//sent to RequestResourceNames
AUTO_STRUCT;
typedef struct NameSpaceNamesRequest
{
	char *pNameSpaceName;
	int *piRequestedTypes; //ea32 of eResourceDBResourceType
	int iCmdID; NO_AST //used internally for recordkeeping
} NameSpaceNamesRequest;


AUTO_STRUCT;
typedef struct SimpleResourceRef
{
	char *pPrivateName;			AST(NAME(PrivateName))
	char *pPublicName;			AST(NAME(PublicName))
} SimpleResourceRef;
extern ParseTable parse_SimpleResourceRef[];
#define TYPE_parse_SimpleResourceRef SimpleResourceRef

//returns lists of objects in a namespace
AUTO_STRUCT;
typedef struct NameSpaceResourceList
{
	eResourceDBResourceType eType;
	SimpleResourceRef **ppRefs;
} NameSpaceResourceList;

AUTO_STRUCT;
typedef struct NameSpaceAllResourceList
{
	char *pNameSpaceName;
	NameSpaceResourceList **ppLists;
} NameSpaceAllResourceList;



#define RESOURCE_DB_TYPE_VALID(eType) ((eType) > RESOURCETYPE_UNSUPPORTED && (eType) < RESOURCETYPE_LAST)
