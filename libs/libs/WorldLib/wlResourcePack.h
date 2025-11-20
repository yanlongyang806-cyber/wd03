#ifndef _WLRESOURCEPACK_H_
#define _WLRESOURCEPACK_H_
GCC_SYSTEM

#include "stdtypes.h"


typedef struct ResourcePack ResourcePack;

typedef enum ResourcePackType ResourcePackType;

void wlLoadPacks(void);
int wlCheckPackExists(const char *pack_name, ResourcePackType pack_type);
ResourcePack *wlGetResourcePack(const char *pack_name, ResourcePackType pack_type);
char ***wlGetResourcePackNames(ResourcePackType pack_type);
void wlReloadResourcePackType(ResourcePackType pack_type);


//////////////////////////////////////////////////////////////////////////
// structs for UI display

typedef struct ResourcePackFolderNode ResourcePackFolderNode;
typedef struct ResourcePackFileNode ResourcePackFileNode;

AUTO_STRUCT;
typedef struct ResourcePackFileNode
{
	char *name;
	const char *filename;				AST(CURRENTFILE)
} ResourcePackFileNode;

AUTO_STRUCT;
typedef struct ResourcePackFolderNode
{
	char *name;
	ResourcePackFolderNode **folders;
	ResourcePackFileNode **files;
} ResourcePackFolderNode;

AUTO_STRUCT;
typedef struct ResourcePack
{
	char *name;
	ResourcePackFolderNode **folders;

	// hash from file name (no extension) to ResourcePackFileNode, for existence checking
	StashTable files_nopath;			NO_AST

	// hash from relative path name (no extension) to ResourcePackFileNode, for existence checking
	StashTable files;					NO_AST

} ResourcePack;

extern ParseTable parse_ResourcePack[];
#define TYPE_parse_ResourcePack ResourcePack
extern ParseTable parse_ResourcePackFileNode[];
#define TYPE_parse_ResourcePackFileNode ResourcePackFileNode
extern ParseTable parse_ResourcePackFolderNode[];
#define TYPE_parse_ResourcePackFolderNode ResourcePackFolderNode


#endif //_WLRESOURCEPACK_H_

