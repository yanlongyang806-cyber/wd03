#ifndef _SHARDNET_H
#define _SHARDNET_H

#include "chatdb.h"
//#include "netio.h"

//char *getShardName(NetLink *link);

enum 
{
	kChatLink_Public = 0, 
	kChatLink_Shard, 
	kChatLink_Admin,
};

/*
typedef struct
{
	NetLink	*link;
	char	shard_name[64];
	U8		linkType;		// shard, public, or admin (ie chatadmin tool)
	Packet	*aggregate_pak;
	char	last_msg[10000];
	StashTable	user_db_ids;
	int		online;	

	// for ChatAdmin clients only
	int		uploadStatus;
	int		uploadHint;
	int		uploadPending;
	ChatUser	*adminUser;

} ClientLink;
*/

typedef struct {
	int	processed_count;
	int	sendmsg_count;
	int	send_rate;
	int	recv_rate;
	int	cross_shard_msgs;
	int error_msgs_sent;

	F32	crossShardRate;
	F32 invalidRate; 
	int	sendMsgRate; 
	int recvMsgRate;

} ChatStats;

extern ChatStats g_stats;
extern int g_chatLocale;

void updateCrossShardStats(ChatUser * from, ChatUser * to);
void chatServerShutdown(ChatUser * unused, char * reason);
//void adminLoginFinal(ClientLink * client);
void shardDisconnectAll();
bool chatRateLimiter(ChatUser* user);

#if CHATSERVER
void ChatServerLogin(ContainerID accountID, ContainerID characterID, const char *accountName, const char *displayName, U32 access_level);
void ChatServerLogout(ContainerID accountID);
void ChatServerPlayerUpdate(ContainerID accountID, PlayerInfoStruct *pPlayerInfo);
#endif

#endif
