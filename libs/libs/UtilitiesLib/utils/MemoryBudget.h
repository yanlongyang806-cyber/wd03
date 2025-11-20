#pragma once
GCC_SYSTEM

#define BUDGET_Textures_Art "Textures:Art"
#define BUDGET_Textures_Misc "Textures:Misc"
#define BUDGET_Materials "Materials"
#define BUDGET_Geometry_Art "Geometry:Art"
#define BUDGET_Geometry_Misc "Geometry:Misc"
#define BUDGET_Terrain_Art "Terrain:Art"
#define BUDGET_Terrain_System "Terrain:System"
#define BUDGET_Framebuffers "Framebuffers"
#define BUDGET_Animation "Animation"
#define BUDGET_Physics "Physics"
#define BUDGET_FXSystem "FXSystem"
#define BUDGET_Audio "Audio"
#define BUDGET_World "World"
#define BUDGET_FileSystem "FileSystem"
#define BUDGET_GameSystems "GameSystems"
#define BUDGET_EXE "EXE"
#define BUDGET_SystemReserved "SystemReserved"
#define BUDGET_Slack "Slack"
#define BUDGET_Unsorted "Unsorted"
#define BUDGET_DemoPlayback "DemoPlayback"
#define BUDGET_Renderer "Renderer"
#define BUDGET_Fonts "Fonts"
#define BUDGET_Networking "Networking"
#define BUDGET_EngineMisc "EngineMisc"
#define BUDGET_UISystem "UISystem"
#define BUDGET_Unknown "Unknown"
#define BUDGET_Editors "Editors" // Should only used if guaranteed not to be loaded on Xbox/regular runtime
#define BUDGET_ReferenceHandles "ReferenceHandles" // Only tracking count, not size.



typedef void (*OutputHandler)(char *appendMe, void *userdata);
void defaultHandler(char *appendMe, void *junk);
void handlerPrintf(OutputHandler handler, void *userdata, FORMAT_STR const char *fmt, ...);
#define handlerPrintf(handler, userdata, fmt, ...) handlerPrintf(handler, userdata, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void estrConcatHandler(char *appendMe, char **estrBuffer);


void memBudgetDisplay(OutputHandler handler, void *userdata, bool verbose);



// Adds a mapping between a struct and a file name (called automatically by TextParser)
// Assumes both strings are persistent/static, if not, call allocAddString on them first!
void memBudgetAddStructMapping(const char *structname, const char *filename);

// Adds a mapping between a struct and a file name.  Does not fail if struct is already mapped.
// Assumes both strings are persistent/static, if not, call allocAddString on them first!
void memBudgetAddStructMappingIfNotMapped(const char *structname, const char *filename);

// Adds a mapping between a filename and a budget module name
// Assumes both strings are persistent/static, if not, call allocAddString on them first!
void memBudgetAddMapping(const char *filename, const char *moduleName);

// Adds a mapping between a simpleMatch expression and a budget module name
// Assumes module string is persistent/static, if not, call allocAddString on it first!
void memBudgetAddDirFilterMapping(const char *expr, const char *moduleName);

SA_RET_NN_STR char *filenameWithStructMappingInFixedSizeBuffer(SA_PARAM_NN_STR const char *filenameOrStructname, int strwidth, char *buf, int buf_size);

// Must be static pointer
void memBudgetSetConfigFile(const char *config_file);

void memBudgetOverrideBudgets(const char *config_file);

void memBudgetStartup(void); // Loads some data files


typedef struct MemoryBudget MemoryBudget;
typedef struct ModuleMemOperationStats ModuleMemOperationStats;

AUTO_STRUCT;
typedef struct MemoryBudgetFileEntry
{
	const char *filename;
	U32 size;
	U32 count;
	U32 traffic;
} MemoryBudgetFileEntry;

AUTO_STRUCT AST_ENDTOK(End);
typedef struct MemoryBudget
{
	const char *module; AST(NAME(Name) POOL_STRING)
	F32 allowed_mb; AST(NAME(Size))
	F32 allowed_mb_secondary; AST(NAME(SizeSecondary))
	size_t allowed;	NO_AST
	size_t allowed_secondary;	NO_AST
	U64 current;	NO_AST
	U64 workingSetSize;		NO_AST
	U32 workingSetCount;	NO_AST
	U32 traffic;	NO_AST
	U32 count;		NO_AST
	U32 lastTraffic; NO_AST
	U32	frame_id; NO_AST
	ModuleMemOperationStats **stats;
	MemoryBudget **subBudgets; AST(NAME(Budget))
} MemoryBudget;

extern ParseTable parse_MemoryBudgetFileEntry[];
#define TYPE_parse_MemoryBudgetFileEntry MemoryBudgetFileEntry

// Queries the memory system and returns the current state of the budgets
SA_RET_NN_VALID MemoryBudget **memBudgetGetBudgets(void);
SA_RET_OP_VALID MemoryBudget *memBudgetGetBudgetByName(SA_PARAM_NN_STR const char *moduleName);
SA_RET_NN_VALID MemoryBudget *memBudgetGetByFilename(SA_PARAM_NN_STR const char *filename);

typedef enum MemBudgetRequirement {
	MemBudget_NotRequired,
	MemBudget_Required_Assert,
	MemBudget_Required_PopupForProgrammers,
	MemBudget_Required_LogToN,
} MemBudgetRequirement;

void memBudgetSetRequirement(MemBudgetRequirement req);
