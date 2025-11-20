/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef CLIENTTRADE_H_
#define CLIENTTRADE_H_

#include "GlobalTypeEnum.h"

// Trade client commands
void trade_ReceiveTradeRequest(bool bSender);
void trade_ReceiveTradeCancel(void);

#endif
