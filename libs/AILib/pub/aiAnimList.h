#ifndef AIANIMLIST_H
#define AIANIMLIST_H

typedef struct	Entity			Entity;
typedef struct	CommandQueue	CommandQueue;
typedef struct	AIAnimList		AIAnimList;

void aiAnimListSetOneTickEx(Entity* be, const AIAnimList* animList, bool isEmote);
#define aiAnimListSetOneTick(be, animList) \
	aiAnimListSetOneTickEx(be, animList, false);

int aiAnimListSet_dbg(Entity* be, const AIAnimList* animList, CommandQueue** queueForDestroyCmd, bool isEmote,
						const char* fileName, U32 fileLine);
#define aiAnimListSetEx(be, animList, queueForDestroyCmd, isEmote) \
	aiAnimListSet_dbg(be, animList, queueForDestroyCmd, isEmote, __FILE__, __LINE__) 
#define aiAnimListSet(be, animList, queueForDestroyCmd) \
	aiAnimListSetEx(be, animList, queueForDestroyCmd, false)

void aiAnimListSetHold(Entity *e, AIAnimList* al);
void aiAnimListClearHold(Entity *e);

#endif