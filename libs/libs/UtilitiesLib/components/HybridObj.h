#pragma once
GCC_SYSTEM

#include "textparser.h"

//tpi_info and tok_Start
#define NUM_HEADER_TPI_COLUMNS 2

//tok_end and null terminator
#define NUM_FOOTER_TPI_COLUMNS 2

typedef struct HybridObjHandle HybridObjHandle;

//Create a handle with a struct named "HybridObject"
HybridObjHandle *HybridObjHandle_Create(void);

//Sets the hyrid object handle to a special mode where unowned structs which are stuck into the hybrid object (a weird thing to do) 
//will be copied with just their keys
//
//This is sometimes useful in servermonitoring, particularly when monitoring HTML_CONVERT_OPT_STRUCT_TO_KEY_STRING fields,
//such as ".machine" or ".list" when monitoring gameServerExes on the MapManager, in case some future code archeologist wants to
//know wtf is up with this code
void HybridObjHandle_SetUnownedStructsCopiedWithKeys(HybridObjHandle *pHandle, bool bSet);

//Create a handle with an arbitrarily named struct
HybridObjHandle *HybridObjHandle_CreateNamed(const char *name);

//returns true on success
bool HybridObjHandle_AddObject(HybridObjHandle *pHandle, ParseTable *pNewObjTPI, char *pObjName);

//returns true on success
bool HybridObjHandle_AddField(HybridObjHandle *pHandle, char *pObjName, char *pXPath);

//Get the internal index of the last field added.
int HybridObjHandle_IndexOfLastFieldAdded(HybridObjHandle *pHandle);

//when the hybrid TPI is being created, each of its columns is copied from one of the source
//TPIs. After each such copying, this callback (if specified) is called
typedef void HybridObjHandle_TPICopyingCB(ParseTable *pSourceTPI, int iSourceColumn, ParseTable *pDestTPI, int iDestColumn,
	bool bSourceTPIIsTopLevel, void *pUserData);

typedef struct NameObjPair
{
	char *pObjName;
	void *pObj;
} NameObjPair;

//normally pInObjects would be an earray, but it turns out to be much more convenient to
//just pass in a static array and a size
void *HybridObjHandle_ConstructObject(HybridObjHandle *pHandle, NameObjPair *pInObjects, int iNumInObjects);

//Use the handle's tpi to destroy a struct
void HybridObjHandle_DestroyObject(HybridObjHandle *pHandle, void *structptr);

//convenience wrappers around the above
void *HybridObjHandle_ConstructObject_1(HybridObjHandle *pHandle, char *pObjName1, void *pObj1);
void *HybridObjHandle_ConstructObject_2(HybridObjHandle *pHandle, char *pObjName1, void *pObj1, char *pObjName2, void *pObj2);
void *HybridObjHandle_ConstructObject_3(HybridObjHandle *pHandle, char *pObjName1, void *pObj1, char *pObjName2, void *pObj2,  char *pObjName3, void *pObj3);


void HybridObjHandle_SetTPICopyingCB(HybridObjHandle *pHandle, HybridObjHandle_TPICopyingCB *pCB, void *pUserData);

ParseTable *HybridObjHandle_GetTPI(HybridObjHandle *pHandle);

void HybridObjHandle_Destroy(HybridObjHandle *pHandle);
void HybridObjHandle_DestroyTPI(ParseTable *pTPI);



/*
int iCurIndex;
void **ppTempObjects = NULL;
ParseTable *pHybridTPI;
void *pHybridObject;
HybridObjHandle *pHandle = HybridObjHandle_Create();

iCurIndex = HybridObjHandle_AddObject(pHandle, parse_Player);
HybridObjHandle_AddField(pHandle, iCurIndex, ".hp");
HybridObjHandle_AddField(pHandle, iCurIndex, ".name");

iCurIndex = HybridObjHandle_AddObject(pHandle, parse_Team);
HybridObjHandle_AddField(pHandle, iCurIndex, ".playerList");


pHybridTPI = HybridObjHandle_GetTPI(

//for all players
{
	Team *pTeam = GetTeamFromPlayer(pPlayer);
	eaPush(&ppTempObjects, pPlayer);
	eaPush(&ppTempObjects, pTeam);

	pHybridObject = HybridObjHandle_ConstructObject(pHandle, ppTempObjects);

	DoSomethingWithHybridObject(pHybridObject, pHybridTPI);

	StructDestroyVoid(pHybridTPI, pHybridObject);
}

HybridObjHandle_Destroy(pHandle);
HybridObjHandle_DestroyTPI(pHybridTPI);
*/






