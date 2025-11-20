/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GSLENTITYDEBUG_H__
#define GSLENTITYDEBUG_H__

typedef struct Entity Entity;

// I apologize for these externs, but they don't get autogened
extern void entCon_mmCmdSetIsStrafing(Entity* e, U32 IsStrafing); 
extern void entCon_mmCmdSetUseThrottle(Entity* e, U32 useThrottle);
extern void entCon_mmCmdSetUseOffsetRotation(Entity* e, U32 useOffsetRotation);


#endif /* GSLENTITYDEBUG_H__ */
