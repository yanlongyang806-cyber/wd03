/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct MissionTemplateType MissionTemplateType;
typedef struct MissionVarTable MissionVarTable;
typedef struct TemplateVariable TemplateVariable;
typedef struct TemplateVariableGroup TemplateVariableGroup;
typedef struct TemplateVariableSubList TemplateVariableSubList;


bool missiontemplate_MissionIsMadLibs(const MissionDef *pDef);

TemplateVariableSubList *missiontemplate_FindMatchingSubList(MissionVarTable *pValueList, TemplateVariable *pDependencyVal);

TemplateVariable *missiontemplate_GetRandomValueFromSubList(TemplateVariableSubList *pSubList, int iLevel);

bool missiontemplate_RandomizeTemplateValues(MissionTemplateType *pTemplateType, TemplateVariableGroup *pGroup, TemplateVariable ***peaFinishedVariables, int iLevel);

