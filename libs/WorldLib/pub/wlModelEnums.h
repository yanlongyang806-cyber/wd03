#pragma once

//parameters to modelLoad
typedef enum GeoLoadType
{
	GEO_LOAD_BACKGROUND	= 1 << 0,
//	GEO_LOAD_NOW		= 1 << 1,
	GEO_LOAD_HEADER		= 1 << 2, //used by svr sequencer where you don't need animations tracks
	GEO_LOAD_RELOAD		= 1 << 3, //used by on the fly reloading
} GeoLoadType;

//flags on an gld's data state
typedef enum GeoLoadState
{
	GEO_NOT_LOADED			= 1,
	GEO_LOADING,
	GEO_LOAD_FAILED,
	GEO_LOADED_NEED_INIT,
	GEO_LOADED_NULL_DATA,
	GEO_LOADED_LOST_DATA, // Just for model collision - we've got the texids loaded, but we've lost the col data
	GEO_LOADED
} GeoLoadState;

typedef enum ModelFlags
{
	OBJ_DRAWBONED		= 1 << 0, //draw this object using model draw boned node
	OBJ_HIDE			= 1 << 1,
} ModelFlags;

typedef enum PackType
{
	PACK_F32,
	PACK_U32,
	PACK_U16,
	PACK_U8,
} PackType;

#define MAX_OBJBONES 52

typedef enum Geo2VerifyLogMode
{
	G2VLM_None,
	G2VLM_LogAllAndVerify,
	G2VLM_LogDefectiveMSets,
} Geo2VerifyLogMode;
