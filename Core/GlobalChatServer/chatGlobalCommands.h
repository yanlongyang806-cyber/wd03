#ifndef CHATGLOBALCOMMAND_H
#define CHATGLOBALCOMMAND_H
#pragma once
GCC_SYSTEM

typedef struct CmdContext CmdContext;
typedef struct ChatMessage ChatMessage;

int ChatServerPrivateMesssage(CmdContext *context, ChatMessage *pMsg);

#endif