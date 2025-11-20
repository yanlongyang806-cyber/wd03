#pragma once
GCC_SYSTEM

#include "net/net.h"

typedef enum CommandServingFlags CommandServingFlags;
extern NetLink *gpControllerLink;

//connect yourself to the Controller, if possible, and begin keeping it updated about your state
//
//If you expect to be getting control messages from the controller (currently, only launchers do), then pass
//in an AuxMessageHandler to handle these messages.
//
//Generally, the only thing that can survive if the controller dies is the MCP
void AttemptToConnectToController(bool bCanFail, PacketCallback *pAuxMessageHandler, bool bCanSurviveDisconnect);
void ClearControllerConnection(void);
void UpdateControllerConnection(void);
void RequestTimeDifferenceWithController(void);

//most servers inform the controller of their state automatically through the GSM. Some (ie, logserver) can not,
//either because they do not use the GSM, or because they connect directly to the controller, as opposed to through the
//transaction server. They use this function to manually inform the controller of their state
void DirectlyInformControllerOfState(char *pStateString);

__forceinline static NetLink *GetControllerLink() { return gpControllerLink; }


LATELINK;
char *GetControllerHost(void);

LATELINK;
U32 GetAntiZombificationCookie(void);

LATELINK;
void IncAntiZombificationCoookie(void);

LATELINK;
void ControllerLink_ExtraDisconnectLogging(void);

void DoSlowReturn_NetLink(U32 iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData);

void ControllerLink_SetNeverConnect(bool bSet);

void SendErrorsToController(void);

LATELINK;
void AutoSetting_PacketOfCommandsFromController(Packet *pak);

LATELINK;
void ControllerLink_ProcessTimeDifference(int iDiff);

LATELINK;
void ConnectedToController_ServerSpecificStuff(void);

extern bool gbCanAlwaysSurviveControllerDisconnect;