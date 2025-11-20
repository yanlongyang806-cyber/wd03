/***************************************************************************
 *     Copyright (c) 2011-2011, Cryptic Studios
 *     All Rights Reserved
 *
 * Module Description:
 *
 *
 ***************************************************************************/

typedef struct Entity Entity;
typedef struct MapState MapState;


MapState* mapState_FromEnt(Entity *pEnt)
{
	devassertmsg(0,__FUNCTION__" should not be called on an AppsServer");
	return NULL;
}


MapState* mapState_FromPartitionIdx(int iPartitionIdx)
{
	devassertmsg(0,__FUNCTION__" should not be called on an AppsServer");
	return NULL;
}
