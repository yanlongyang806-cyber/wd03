/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GCLCONTROLSCHEME_H__
#define GCLCONTROLSCHEME_H__

#ifndef CONTROLSCHEME_H__
#include "ControlScheme.h"
#endif

extern StaticDefineInt CameraModeEnum[];
extern StaticDefineInt ControlSchemeRegionTypeEnum[];

AUTO_STRUCT;
typedef struct KeybindProfileUI
{
	const char* pchDisplayName;	AST(NAME(DisplayName) UNOWNED)
	const char* pchName; AST(NAME(Name) UNOWNED)
} KeybindProfileUI;

AUTO_STRUCT;
typedef struct ControlSchemeRegionData
{
	const char* pchName;		AST(NAME(Name) UNOWNED)
	S32			eSchemeRegion;	AST(NAME(SchemeRegion) SUBTABLE(ControlSchemeRegionTypeEnum))
} ControlSchemeRegionData;

AUTO_STRUCT;
typedef struct CameraModeRegion
{
	S32						eMode;		AST(NAME(Mode) SUBTABLE(CameraModeEnum) STRUCTPARAM)
	ControlSchemeRegionType eRegions;	AST(NAME(Regions) FLAGS)
} CameraModeRegion;

AUTO_STRUCT;
typedef struct CameraTypeRulesDef
{
	CameraType eType;					AST(NAME(Type))
	CameraModeRegion** eaModeRegions;	AST(NAME(ModeRegion))
	bool bUserSelectable;				AST(NAME(UserSelectable) DEFAULT(true))
} CameraTypeRulesDef;

AUTO_STRUCT;
typedef struct CameraTypeRules
{
	CameraTypeRulesDef** eaDefs; AST(NAME(CameraTypeRules))
} CameraTypeRules;

void schemes_LoadCameraTypeRules(void);
void schemes_HandleUpdate(void);
	// Should be called after entity updates are received so that any changes
	//   to the scheme are handled.

void schemes_initOptions(Entity *pEnt);
void schemes_Cleanup(void);

void schemes_ClearLocal(void);
	// Clear any local data, call on logout

const ControlScheme* schemes_GetActiveStoredScheme(void);
void schemes_UpdateStoredSchemes(bool bUpdateOld, bool bUpdateNew);
void schemes_UpdateForCurrentControlSchemeEx(bool bUpdateCameraMode, bool bNewScheme);
#define schemes_UpdateForCurrentControlScheme(bUpdateCameraMode) schemes_UpdateForCurrentControlSchemeEx(bUpdateCameraMode, false)
ControlScheme* schemes_UpdateCurrentStoredScheme(void);
void schemes_SaveStoredSchemes(void);
void PeriodicStoredSchemesUpdate(void);

S32 gclControlSchemeGetCurrentSchemeRegionType(void);

// Gets the current region selected in the control's option menu
S32 schemes_OptionsGetCurrentSelectedRegion();
void gclControlSchemeGenerateRegionList(void);
bool gclControlSchemeIsChangingCurrent(void);

LATELINK;
void schemes_ControlSchemeOptionInit(const char* pchCategory);

extern bool g_bInvertMousePerScheme;
extern ControlScheme g_CurrentScheme;
extern CameraTypeRules g_CameraTypeRules;
extern ControlSchemeRegionData** g_eaUIRegions;
extern ControlScheme** g_eaStoredSchemes;

#endif /* #ifndef GCLCONTROLSCHEME_H__ */

/* End of File */

