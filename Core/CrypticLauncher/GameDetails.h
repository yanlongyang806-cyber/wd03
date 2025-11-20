#pragma once
GCC_SYSTEM

#define DEFAULT_GAME_ID 0

bool gdIDIsValid(U32 gameID);

const char *gdGetName(U32 gameID);
const char *gdGetCode(U32 gameID);
const char *gdGetDisplayName(U32 gameID);
const char *gdGetLauncherURL(U32 gameID);
const char *gdGetURL(U32 gameID);
const char *gdGetQALauncherURL(U32 gameID);
const char *gdGetDevLauncherURL(U32 gameID);
const char *gdGetPWRDLauncherURL(U32 gameID);
const char *gdGetLiveShard(U32 gameID);
const char *gdGetPtsShard1(U32 gameID);
const char *gdGetPtsShard2(U32 gameID);
bool gdIsLocValid(U32 gameID, U32 locID);

U32 gdGetIDByName(const char *name);
U32 gdGetIDByCode(const char *code);
U32 gdGetIDByExecutable(const char *executablename);