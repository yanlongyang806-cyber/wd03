#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef struct AccountTicket AccountTicket;

// This callback can be called in with different actions.  userErrorMessageKey is only returned for ValidateTicket_Fail.
typedef void (*ValidateLoginTicketCB)(ContainerID accountID, AccountTicket *accountTicket, bool success, const char *userErrorMessageKey, void *userData);
typedef void (*GenerateOneTimeCodeCB)(ContainerID accountID, bool success, const char *userErrorMessageKey, void *userData);
typedef void (*ValidateOneTimeCodeCB)(ContainerID accountID, bool success, const char *userErrorMessageKey, void *userData);
typedef void (*SetMachineNameCB)(ContainerID accountID, bool success, const char *userErrorMessageKey, void *userData);

void aslLogin2_ValidateLoginTicketTick(void);
void aslLogin2_ValidateLoginTicket(ContainerID accountID, U32 ticketID, ValidateLoginTicketCB cbFunc, void *userData);
void aslLogin2_GenerateOneTimeCode(ContainerID accountID, const char *machineID, U32 clientIPAddress, GenerateOneTimeCodeCB cbFunc, void *userData);
void aslLogin2_ValidateOneTimeCode(ContainerID accountID, const char *machineID, const char *oneTimeCode, const char *machineName, U32 clientIPAddress, ValidateOneTimeCodeCB cbFunc, void *userData);
void aslLogin2_SetMachineName(ContainerID accountID, const char *machineID, const char *machineName, U32 clientIPAddress, SetMachineNameCB cbFunc, void *userData);


