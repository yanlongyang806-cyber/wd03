/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "estring.h"
#include "GameServerLib.h"
#include "gslMapState.h"
#include "gslMapVariable.h"
#include "gslPartition.h"
#include "gslWorldVariable.h"
#include "MapDescription.h"
#include "mapstate_common.h"
#include "NameList.h"
#include "StringCache.h"
#include "worldgrid.h"

#include "Autogen/GameClientLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Debug Commands
// ----------------------------------------------------------------------------------

static NameList *mapvardebug_CreateNameList(void)
{
	int i;
	static NameList *s_mapvar_namelist = NULL;
	MapVariable **map_vars = NULL;
	int iPartitionIdx = -1;

	if (!s_mapvar_namelist) {
		s_mapvar_namelist = CreateNameList_Bucket();
	}

	NameList_Bucket_Clear(s_mapvar_namelist);

	// Find valid partition
	for(i=partition_GetCurNumPartitionsCeiling()-1; i>=0; --i) {
		if (partition_ExistsByIdx(i)) {
			iPartitionIdx = i;
			break;
		}
	}

	if (i>=0) {
		// slightly questionable clear and re-init here, but
		// this is a debug feature and keeping it in sync is more important
		// than performance IMHO. 
		mapvariable_GetAll(iPartitionIdx, &map_vars);
		for (i=0; i < eaSize(&map_vars); i++)
		{
			MapVariable *var = map_vars[i];
			if(!var)
				continue;
			NameList_Bucket_AddName(s_mapvar_namelist, var->pcName); // bucket copies str
		}
		eaDestroy(&map_vars);
	}
	return s_mapvar_namelist;
}


AUTO_RUN;
void InitMapVarNameList(void)
{
	NameList_RegisterGetListCallback("MapVariables", mapvardebug_CreateNameList);
}


// Gets a map variable
// ACMD_NAMELIST("ZoneMap",REFDICTIONARY)
AUTO_COMMAND ACMD_NAME(GetMapVariable);
char* mapvariable_CmdGetVariable(Entity *pClientEntity, ACMD_NAMELIST("MapVariables",NAMED) const char *pcName)
{
	static char *estrBuf = NULL;
	MapVariable *pMapVar;

	estrClear(&estrBuf);
	pMapVar = mapvariable_GetByNameIncludingCodeOnly(entGetPartitionIdx(pClientEntity), pcName);
	if (pMapVar) {
		worldVariableToEString(pMapVar->pVariable, &estrBuf);
	} else {
		estrPrintf(&estrBuf, "Variable '%s' not found", pcName);
	}
	return estrBuf;
}


// Sets a map variable
AUTO_COMMAND ACMD_NAME(SetMapVariable);
char* mapvariable_CmdSetVariable(Entity *pClientEntity, ACMD_NAMELIST("MapVariables",NAMED) const char *pcName, const char *pcValue)
{
	static char *estrBuf = NULL;
	MapVariable *pMapVar;

	estrClear(&estrBuf);
	pMapVar = mapvariable_GetByNameIncludingCodeOnly(entGetPartitionIdx(pClientEntity), pcName);
	if (pMapVar) {
		worldVariableFromString(pMapVar->pVariable, pcValue, &estrBuf);
		if (!estrBuf) {
			estrPrintf(&estrBuf, "Variable '%s' set to '%s'", pcName, pcValue);
		}
		mapVariable_NotifyModified(entGetPartitionIdx(pClientEntity), pMapVar);
	} else {
		if (!estrBuf) {
			estrPrintf(&estrBuf, "Variable '%s' not found", pcName);
		}
	}
	return estrBuf;
}


// ----------------------------------------------------------------------------------
// Commands to set Map Variables via command line options
// ----------------------------------------------------------------------------------

// Sets a map variable
// TODO_PARTITION: SetMapVariableEarly command
AUTO_COMMAND ACMD_NAME(SetMapVariableEarly);
char *mapvariable_CmdSetVariableEarly(Entity *ent, const char *pcName, ACMD_NAMELIST(WorldVariableTypeEnum, STATICDEFINE) const char *pchVarType, const char *pchValue)
{
	WorldVariableType eType = StaticDefineIntGetInt(WorldVariableTypeEnum, pchVarType);
	static char *estrBuf = NULL;
	MapVariable *pMapVar = NULL;
	WorldVariable *pNewVar = NULL;
	bool bError = false;
	bool bVarExists = false;
	int iPartitionIdx = ent ? entGetPartitionIdx(ent) : PARTITION_UNINITIALIZED;
	estrClear(&estrBuf);

	if (eType < 0) {
		estrPrintf(&estrBuf, "Variable Type '%s' is invalid!", pchVarType);
		bError = true;
	}

	// Set the runtime variable if it exists
	if (!bError && partition_ExistsByIdx(iPartitionIdx)) {
		pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, pcName);
		if (pMapVar) {
			if (pMapVar->pVariable && pMapVar->pVariable->eType == eType) {
				bError &= !worldVariableFromString(pMapVar->pVariable, pchValue, &estrBuf);
				bVarExists = true;
			} else {
				estrPrintf(&estrBuf, "Variable '%s' is incorrect type!", pcName);
				bError = true;
			}
		}
	}
	
	// Also set up something so that this works from the command line
	pNewVar = StructCreate(parse_WorldVariable);
	pNewVar->eType = eType;
	pNewVar->pcName = allocAddString(pcName);
	bError &= !worldVariableFromString(pNewVar, pchValue, &estrBuf);

	if (!bError) {
		eaPush(&g_eaMapVariableOverrides, pNewVar);
		
		//If the map variable exists, then notify everyone that the variable has changed
		if (pMapVar) {
			mapVariable_NotifyModified(iPartitionIdx, pMapVar);
		}
	} else {
		StructDestroy(parse_WorldVariable, pNewVar);
	}

	if (bError) {
		Errorf("%s", estrBuf);
	} else {
		if (bVarExists) {
			estrPrintf(&estrBuf, "Variable '%s' set to '%s'", pcName, pchValue);
		} else {
			estrPrintf(&estrBuf, "Variable '%s' will be initialized to '%s', if it exists", pcName, pchValue);
		}
	}

	return estrBuf;
}

char *mapvariable_CreateMapVariableDbgEx(Entity *pEnt, const char *pcName, const char *pcType, const char *pcValue);
char *mapvariable_CreateMapVariableDbg(Entity *ent, const char *pcName, const char *pchValue);

// testing function only. don't use!
AUTO_COMMAND ACMD_NAME(CreateMapVariableDbg);
char* CreateMapVariableDbg(Entity *ent, const char *pcName, const char *pchValue)
{
	return mapvariable_CreateMapVariableDbg(ent, pcName, pchValue);
}

// testing function only. don't use!
AUTO_COMMAND ACMD_NAME(CreateMapVariableDbgEx);
char* CreateMapVariableDbgEx(Entity *ent, const char *pcName, ACMD_NAMELIST(WorldVariableTypeEnum, STATICDEFINE) const char *pcType, const char *pchValue)
{
	return mapvariable_CreateMapVariableDbgEx(ent, pcName, pcType, pchValue);
}
