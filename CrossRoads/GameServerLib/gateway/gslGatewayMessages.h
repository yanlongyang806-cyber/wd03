/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GSLGATEWAYMESSAGES_H__
#define GSLGATEWAYMESSAGES_H__

#pragma once


#define PING_CMD          1
#define CONNECTION_CMD 0x10
#define SESSION_CMD    0x20

typedef struct NetLink NetLink;
typedef struct Item Item;
typedef struct Entity Entity;

extern void wgsStartGatewayProxy(void);
extern void wgsBroadcastMessageToAllConnections(const char *pTitle, const char *pString);

extern void wgsMessageHandler(SA_PARAM_NN_VALID Packet *pktIn, int cmd, SA_PARAM_NN_VALID NetLink *link, SA_PARAM_OP_VALID void *userData);
extern int wgsConnectHandler(SA_PARAM_NN_VALID NetLink* link, SA_PARAM_OP_VALID void *userData);
extern int wgsDisconnectHandler(SA_PARAM_NN_VALID NetLink* link, SA_PARAM_OP_VALID void *userData);


LATELINK;
void GetItemInfoComparedSMF(char **pestrResult,
	Language lang,
	SA_PARAM_OP_VALID Item *pItem,
	SA_PARAM_OP_VALID Item *pItemOther, 
	SA_PARAM_OP_VALID Entity *pEnt, 
	const char *pchDescriptionKey,
	const char *pchContextKey,
	bool bGetItemAttribDiffs,
	S32 eActiveGemSlotType);

#define CONNECTION_PKTCREATE(pkt, link, pchMessage)								\
{																				\
	static PacketTracker *__pTracker;											\
	ONCE(__pTracker = PacketTrackerFind("GatewayConnection", 0, pchMessage));	\
	pkt = pktCreateWithTracker(link, CONNECTION_CMD, __pTracker);				\
	pktSendString(pkt, pchMessage);												\
}



#endif /* #ifndef GSLGATEWAYMESSAGES_H__ */

/* End of File */
