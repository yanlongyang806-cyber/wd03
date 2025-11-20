#pragma once

typedef struct ChatMailStruct ChatMailStruct;
typedef struct Entity Entity;
typedef struct EmailV3SenderItem EmailV3SenderItem;
typedef struct EmailV3Message EmailV3Message;
typedef struct Item Item;
typedef struct InventorySlot InventorySlot;
typedef enum EMailType EMailType;

ChatMailStruct *gslMailCreateMail (Entity *sender, const char *pchSubject, const char *pchBody);
void gslMail_InitializeMailEx(SA_PARAM_NN_VALID ChatMailStruct *mail, Entity *sender, const char *pchSubject, const char *pchBody, 
							  const char *fromName, EMailType eType, S32 iNPCEMailID, U32 uFutureSendTime);
void ServerChat_CheckMail(Entity *pEnt);
void gslMailLogTo(char **pchOut, const char *toHandle, const char *subject);