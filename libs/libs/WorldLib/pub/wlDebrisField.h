#ifndef __WLDEBRISFIELD_H__
#define __WLDEBRISFIELD_H__
GCC_SYSTEM

typedef struct WorldDebrisFieldProperties WorldDebrisFieldProperties;
typedef struct GroupDef GroupDef;
typedef struct GroupInfo GroupInfo;
typedef struct GroupChild GroupChild;
typedef struct GroupInheritedInfo GroupInheritedInfo;
typedef struct MersenneTable MersenneTable;

typedef struct WorldDebrisFieldGenProps
{
	MersenneTable *random_table;
	Vec3 *position_list;
	Vec3 center;
} WorldDebrisFieldGenProps;

F32 wlDebrisFieldFindColRad(GroupDef *parent, WorldDebrisFieldProperties *properties, GroupDef **fallback_def /*out*/);
U32 wlDebrisFieldFindLocations(WorldDebrisFieldProperties *properties, WorldDebrisFieldGenProps *gen_props, GroupDef *def, F32 *vol_occluders, const Mat4 world_mat, U32 seed, F32 coll_rad);
U32 wlDebrisFieldGetLocationAndSeed(WorldDebrisFieldProperties *properties, WorldDebrisFieldGenProps *gen_props, int item, Mat4 ent_mat);
void wlDebrisFieldCleanUp(WorldDebrisFieldGenProps *gen_props);
bool wlDebrisFieldTraversePre(void *user_data, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry);
bool wlDebrisFieldTraversePost(F32 **debris_excluders, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry);

#endif //__WLDEBRISFIELD_H__