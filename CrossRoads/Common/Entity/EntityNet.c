/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityNet.h"

#include "net/net.h"



// These should be sped up, to take advantage of the internal structure of an entref
void entSendRef(Packet *pak, EntityRef ref)
{
	pktSendBitsPack(pak,8,ref);
}


EntityRef entReceiveRef(Packet *pak)
{
	return pktGetBitsPack(pak,8);
}


void entSendType(Packet *pak, GlobalType type)
{
	pktSendBitsPack(pak,8,type);
}


GlobalType entReceiveType(Packet *pak)
{
	return pktGetBitsPack(pak,8);
}

