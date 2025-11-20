#ifndef _VOICE_H_
#define _VOICE_H_

#include "XBoxStructs.h"

C_DECLARATIONS_BEGIN

#include "stdtypes.h"
#include "GlobalTypeEnum.h"

void voiceGetMyIdentInfo(ContainerID *cid, U64 *xuid);

int voiceInit(void);
void voiceProcess(void);
void voiceReceive(U64 xuid, char *packetData, U32 packetSize);
void voiceSendThroughServer(U64 xuid, char *pktData, U32 pktSize);

int voiceRegisterLocal(void);
int voiceRegisterRemote(U64 xuid, ContainerID entID, U32 teamID);
void voiceCmdRegisterRemote(U64 xuid, ContainerID entID, U32 teamID);
int voiceUnregisterRemote(U64 xuid);
void voiceCmdLeaveVoiceChat(ContainerID leaderID);
void voiceLeaveVoiceChat(ContainerID leaderID);
void voiceCmdPromoteHost(ContainerID newLeaderID);
void voicePromoteHost(ContainerID newLeaderID);

void voiceClearIsTalkingFlags(void);
void voiceSetTeamMemberIsTalking(ContainerID memberID);
bool voiceIsTeamMemberTalking(ContainerID memberID);

ContainerID voiceGetContainerIDByXUID(U64 xuid);
char *voiceGetRemoteUserName(U64 xuid);
char *voiceGetRemoteUserNameByContainerID(ContainerID id);

void voiceStartSession(bool bHost, U32 teamID);
void voiceSendSessionInfo(void);

#if _XBOX
bool voiceJoinChat(bool bIsSessionHost, U8 *pSessionIdBuffer);
#endif

C_DECLARATIONS_END

#endif