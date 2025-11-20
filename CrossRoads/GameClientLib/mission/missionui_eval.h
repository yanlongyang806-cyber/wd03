/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct MissionDefRef MissionDefRef;
typedef struct Message Message;
typedef struct CritterLoreList CritterLoreList;
typedef struct UIGen UIGen;

extern bool g_bDisableAutoHail;

void missionsystem_ClientFormatMessagePtr(const char* sourceName, const Entity *pEnt, const MissionDef *def, U32 iNemesisID, char** ppchResult, Message *pMessage);
const char* missiondef_GetTranslatedCategoryName(const MissionDef *pDef);
void journal_StoreReceivedCritterData(CritterLoreList* pCritterData);

void exprFillMissionListFromRefs(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt, MissionDefRef **eaMissionDefRefs, S32 maxMissions, SA_PARAM_OP_STR const char *pchExcludeTags);

void FillFilterAndSortMissionsWithNumbers(Mission ***peaMissions);
