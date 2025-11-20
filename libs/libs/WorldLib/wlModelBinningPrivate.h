#pragma once
GCC_SYSTEM

#include "wlModelEnums.h"
#include "wlModel.h"
#include "wlModelLoad.h"

typedef struct Geo2LoadData Geo2LoadData;
typedef struct GMesh GMesh;
typedef struct AutoLOD AutoLOD;
typedef struct TexID TexID;

typedef struct PackNames
{
	char	**strings;
	int		count;
} PackNames;

typedef struct LodModel2
{
	int index;
	F32 tri_percent, error, upscale;
	ModelSource *model;
	U32 offsetoffset; // The offset of where to write the offset of the data to.
} LodModel2;


typedef struct ModelSource
{
	// frequently used data keep in same cache block
	ModelFlags		flags;
	F32				radius;
	//ModelHeader*	header;

	GMesh*			generic_mesh; // If non-null, this is used to fill the unpack struct.


	//volatile U8		loadstate;	// Whether or not this is loaded or loading (GeoLoadState).  Matches gld->loadstate.
	//void*			dummy; // this is where the boneinfo used to go
	
	int				vert_count;
	int				tri_count;
	Geo2LoadData	*gld;

	int				tex_count;	//number of tex_idxs and blend_modes (sum of all tex_idx->counts == tri_count)
	TexID			*tex_idx;		//array of (textures + number of tris that have it)
	//Material		**materials;

	AutoLOD			**lods; // Copied from a ModelLODInfo
	bool			high_detail_high_lod; // combination of LOD setting and process time flag

	// LOD-related stuff
	const ModelSource		*srcmodel;			// only valid for lod models
	LodModel2		**lod_models;		// only valid for non-lod models that have lods
	F32				autolod_dists[3];	// distances to place 75% tris, 50% tris, and 25% tris lods for automatic mode
	// End LOD-related stuff


	// Less frequently used data
	const char		*name;
	Vec3			min,max,mid;
	float			lightmap_size;
	U32				lightmap_tex_size;
	ModelProcessTimeFlags process_time_flags;
	F32				uv_density;			// log4 of the minimum triangle uv density

	ModelPackData	pack;
	ModelUnpackData unpack;

	bool			collision_only;
	bool			no_collision;
	U32				colloffsetoffset; // The offset of where to write the offset of the collision data to.
} ModelSource;

typedef struct Geo2LoadData
{
	struct Geo2LoadData * next; //for the background loader only now, and maybe the unloader later?
	struct Geo2LoadData * prev;
	int			model_count;
	ModelSource	*model_data;		// pointer to chunk of data models* are stored in (normally models[0], but munged on reload)
									// Used to be unused gld backpointer, but still in .geo files
	ModelSource		**models;

	const char	*filename;
	PackNames	texnames;
	int			headersize;
	int			datasize;
	void		*header_data;
	void		*geo_data;
	int			data_offset;
	int			file_format_version;

	void		**allocList; // List of all allocations to be freed
	void		**eaAllocList; // List of earrays to be freed
} Geo2LoadData;

extern const int byte_count_code[];  // Defined in opt.c; used by wlCompressDeltas

void geo2OptimizeVertexOrder( ModelSource *model );

