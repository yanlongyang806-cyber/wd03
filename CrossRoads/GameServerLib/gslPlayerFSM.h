/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"
#include "referencesystem.h"

typedef struct Entity		Entity;
typedef struct ExprContext	ExprContext;
typedef struct FSM			FSM;
typedef struct FSMContext	FSMContext;
typedef struct NameList		NameList;

AUTO_STRUCT;
typedef struct PlayerFSM {
	REF_TO(FSM)		hFSM;

	S64				nextTick;
	EntityRef		ref;

	FSMContext		*fsmContext;		NO_AST
	ExprContext		*exprContext;		NO_AST
	
	StashTable		messages;			NO_AST
} PlayerFSM;

S32 pfsm_PlayerFSMExists(const char* pfsmName);
PlayerFSM* pfsm_GetByName(Entity* e, const char* pfsmName);
NameList* pfsmGetNameList(void);

void pfsmOncePerFrame(void);
void pfsm_PlayerEnterMap(Entity* e);
void pfsm_PlayerLeaveMap(Entity* e);