/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#define CCG_PLAYERS_TABLE_SIZE 100

typedef struct CCGPlayer CCGPlayer;

void CCG_PlayersInit(void);

CCGPlayer *CCG_FindPlayer(U32 authToken);

