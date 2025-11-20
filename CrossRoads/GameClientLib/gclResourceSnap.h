/// An interface for saving out headshots of resources at bin-time.
///
/// (This used to be called "ObjectSnap".)
///
/// Any resource that supports taking a headshot of it can have a
/// headshot saved out at bin time.  This is used by the UGC editor to
/// give quick previews for all the assets in the game.
#pragma once

AUTO_STRUCT;
typedef struct ResourceSnapDesc {
	const char* astrDictName;		AST(NAME(DictName) POOL_STRING)
 	const char* astrResName;		AST(NAME(ResName) POOL_STRING)
	const char* astrHeadshotStyleDef; AST(NAME(HeadshotStyleDef) POOL_STRING)
	bool objectIsTopDownView;		NO_AST
} ResourceSnapDesc;
extern ParseTable parse_ResourceSnapDesc[];
#define TYPE_parse_ResourceSnapDesc ResourceSnapDesc

AUTO_STRUCT;
typedef struct ResourceSnapDescList {
	ResourceSnapDesc** eaResources;	AST(NAME(Resource))
} ResourceSnapDescList;
extern ParseTable parse_ResourceSnapDescList[];
#define TYPE_parse_ResourceSnapDescList ResourceSnapDescList

bool gclResourceSnapTakePhotos( ResourceSnapDesc** eaResources );
const char* gclSnapGetResourceString( const ResourceSnapDesc* desc, bool isForTexName );
