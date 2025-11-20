/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslDataQuery.h"
#include "gslEntityNet.h"
#include "gslExtern.h"
#include "gslEntity.h"
#include "net/net.h"
#include "GameServerLib.h"
#include "EntityIterator.h"
#include "BaseEntity.h"
#include "EntityNet.h"
#include "EntityLib.h"
#include "mathutil.h"
#include "structnet.h"
#include "dynnode.h"
#include "EntityMovementManager.h"
#include "timing.h"
#include "testclient_comm.h"
#include "aiStruct.h"
#include "objContainer.h"
#include "EntityGrid.h"
#include "aiExtern.h"
#include "wlCostume.h"
#include "wlSkelInfo.h"
#include "dynSkeleton.h"
#include "dynDraw.h"
#include "dynNode.h"
#include "AutoGen/BaseEntity_h_ast.h"
#include "AutoGen/wlCostume_h_ast.h"
#include "gslTransactions.h"
#include "earray.h"
#include "AttribMod.h"
#include "Powers.h"

// This is the server version of gclDataQuery, and provides useful commands for
// data querying stuff on the server