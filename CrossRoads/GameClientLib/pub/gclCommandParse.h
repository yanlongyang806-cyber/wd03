/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLCOMMANDPARSE_H
#define GCLCOMMANDPARSE_H
GCC_SYSTEM

#include "stdtypes.h"
#include "cmdparse.h"

typedef struct KeyBindProfile KeyBindProfile;
typedef struct Entity Entity;
typedef struct NetLink NetLink;

// Used to temporarily change the access level
void GameClientAccessOverride(int level);

typedef enum GameClientAccessLevelFlags
{
	IGNORE_UGC_MODIFICATIONS = 1 << 0,
} GameClientAccessLevelFlags;


// Returns the current client access level - NULL ent means use active player ptr
int GameClientAccessLevel(Entity *ent, GameClientAccessLevelFlags eFlags);

// Get the access level from a client-side key
int EncryptedKeyedAccessLevel(void);

// Used to mark the time keys were pressed to handle input correctly
void gclCmdSetTimeStamp(int timeStamp);
int gclCmdGetTimeStamp(void);

// Parse a public client command, using the current keyprofile stack
int GameClientParsePublic(const char *str, CmdContextFlag iFlags, Entity *ent, char **ppRetString, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// Parse a private client command
int GameClientParsePrivate(const char *str, CmdContextFlag iFlags, Entity *ent, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// Uses the active entity to parse a command
int GameClientParseActive(const char *str, char **ppRetString, CmdContextFlag iCmdContextFlags, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// given the beginning of the command, find the first match
// returns a search ID to be passed in to later calls.  pass 0 initially
int gclCompleteCommand(char *str, char *out, int searchID, int searchBackwards);
// add a server command to the list of server commands for tab completion
void gclAddServerCommand(char * scmd);
// returns true if the string is an available server command
int gclIsServerCommand(char *str);

// Reads in or writes to config.txt
int gclLoadCmdConfig(void);

// Reads in resume_info
int gclLoadLoginConfig(void);
int gclSaveLoginConfig(void);


void gclSetGameCameraActive(void);
void gclSetFreeCameraActive(void);
void gclSetDemoCameraActive(void);


void gclSetEditorCameraActive(int is_active);

// Once per frame draw skeleton function for the mmLog
void gclMovementDebugDraw3D(const Mat4 matCamera);

//special command parsing function for use by testClient
void HandleCommandRequestFromTestClient(const char *pString, U32 iTestClientCmdID, NetLink *pLinkToTestClient);

char *gclGetDebugPosString(void);

bool getControlButtonState(int index);
#define isPlayerMoving()	isPlayerMovingEx(false)
bool isPlayerMovingEx(bool bIgnoreTurning);

bool isPlayerAutoForward(void);

void gclUpdateAllControls(void);
void gclTurnOffAllControlBits(void);


void gclCommandAliasLoad(void);
const char * gclCommandAliasGetToExecute(const char *pchCmdName);

void CommandEditMode(int enabled);

void GodModeClient(int iSet);

void setMouseForward(int enable);

char *gclGetProxyHost(void);
void gclDisableProxy(void);

#endif
