
#include "dynBitField.h"

typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);
typedef struct PlayerCostumeV0 PlayerCostumeV0;
typedef struct PlayerCostume PlayerCostume;
typedef struct BasicTexture BasicTexture;
typedef struct HeadshotStyleDef HeadshotStyleDef;

typedef struct HeadshotForCostumesOnly
{
	PlayerCostume *pPlayerCostume;
	BasicTexture *pTexture;
	char *pFileName;				// estring
	
} HeadshotForCostumesOnly;

AUTO_STRUCT;
typedef struct CostumesOnlyInfo
{
	union
	{
		NOCONST(PlayerCostume) *pPlayerCostume;		NO_AST
		PlayerCostume *pConstPlayerCostume;			AST(LATEBIND)
	};
	BasicTexture *pTexture;

	const char * pcSpecies;			AST( POOL_STRING )
	char * pCharacterName;		AST( ESTRING )
	DynBitFieldGroup bfg;		NO_AST
	const char *pcAnimKeyword;
	bool bInitialized;
	bool bHeadShotFinal;
	F32 fWidth;
	F32 fHeight;
	bool bSendToServer;

} CostumesOnlyInfo;

AUTO_STRUCT;
typedef struct CostumeOnlyForDisk
{
	PlayerCostumeV0 *pCostumeV0;		AST( NAME("Costume") )	
	PlayerCostume *pCostumeV5;			AST( NAME("CostumeV5") )
}CostumeOnlyForDisk;

AUTO_STRUCT;
typedef struct CostumeOnly_CostumeList
{
	// the actual file name
	char *eFileName;		AST( ESTRING )
	
	// The string to display to the user
	char *eDisplayName;		AST( ESTRING )
}CostumeOnly_CostumeList;
extern ParseTable parse_CostumeOnly_CostumeList[];
#define TYPE_parse_CostumeOnly_CostumeList CostumeOnly_CostumeList

PlayerCostume *CostumeOnly_LoadCostume(const char *pcCostumeName);
bool CostumeOnly_InitFromCostume_External(PlayerCostume *pCostume, const char* strName, F32 fWidth, F32 fHeight);
void CostumeOnly_FinalizeStyle(SA_PARAM_NN_VALID HeadshotStyleDef *pDef, F32 fAnimationFrame, const char *pPose, bool bSendToServer);

void CostumeOnly_OncePerFrame(void);
void CostumeOnly_CreateCostumeListAll(void);
void CostumeOnly_CreateCostumeListAllIncludingNoGender(void);
CostumeOnly_CostumeList*** CostumeOnly_CostumeListModel(void);
PlayerCostume* CostumeOnly_LoadCostume(const char *pcCostumeName);
