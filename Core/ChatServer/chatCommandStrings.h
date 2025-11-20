#ifndef CHATCOMMANDSTRINGS_H
#define CHATCOMMANDSTRINGS_H

#define CHATCOMMAND_USER_LOGIN "UserLogin"
#define CHATCOMMAND_USER_LOGOUT "UserLogout"
#define CHATCOMMAND_USER_HANDLE "UserChangeHandle"
#define CHATCOMMAND_USER_ACCOUNTNAME "UserChangeAccountName"

#define CHATCOMMAND_PRIVATE_SEND "PrivateSend"
#define CHATCOMMAND_PRIVATE_RECEIVE "PrivateReceive"

// CSR Commands
#define CHATCOMMAND_GLOBAL_BROADCAST "ChatGlobalBroadcast"

// Channel User Command Names
#define CHATCOMMAND_CHANNEL_CREATE "ChannelCreate"
#define CHATCOMMAND_CHANNEL_JOIN "ChannelJoin"
#define CHATCOMMAND_CHANNEL_LEAVE "ChannelLeave"
#define CHATCOMMAND_CHANNEL_SEND "ChannelSend"
#define CHATCOMMAND_CHANNEL_RECEIVE "ChannelReceive"

#define CHATCOMMAND_CHANNEL_INVITE "ChannelInvite"
#define CHATCOMMAND_CHANNEL_TARGETINVITE "ChannelTargetInvite"

#define CHATCOMMAND_CHANNEL_ONLINE "ChannelUserOnline"
#define CHATCOMMAND_CHANNEL_OFFLINE "ChannelUserOffline"

#define CHATCOMMAND_CHANNEL_DESTROY "ChannelDestroyed"
#define CHATCOMMAND_CHANNEL_OWNERAUTO "ChannelAutoSetOwner"
#define CHATCOMMAND_CHANNEL_TARGETOWNERAUTO "ChannelAutoSetOwnerTarget"

// Channel Administration Command Names
#define CHATCOMMAND_CHANNEL_MOTD "ChannelSetMotd"
#define CHATCOMMAND_CHANNEL_DESCRIPTION "ChannelSetDescription"
#define CHATCOMMAND_CHANNEL_MODIFY "ChannelModify"
#define CHATCOMMAND_CHANNEL_MODIFYLEVEL "ChannelModifyLevel"
#define CHATCOMMAND_CHANNEL_OWNER "ChannelChangeOwner"
#define CHATCOMMAND_CHANNEL_TARGETOWNER "ChannelChangeOwnerTarget"

#define CHATCOMMAND_CHANNEL_MODIFYUSER "ChannelModifyUser"
#define CHATCOMMAND_CHANNEL_MODIFYTARGET "ChannelModifyTarget"

// Friend / Ignore Command Names
#define CHATCOMMAND_FRIEND_ADD "FriendAdd"
#define CHATCOMMAND_FRIEND_TARGETADD "FriendTargetAdd"

#define CHATCOMMAND_FRIEND_COMMENT "FriendComment"
#define CHATCOMMAND_FRIEND_TARGETCOMMENT "FriendTargetComment"

#define CHATCOMMAND_FRIEND_ACCEPT "FriendAccept"
#define CHATCOMMAND_FRIEND_TARGETACCEPT "FriendTargetAccept"

#define CHATCOMMAND_FRIEND_REJECT "FriendReject"
#define CHATCOMMAND_FRIEND_TARGETREJECT "FriendTargetReject"

#define CHATCOMMAND_FRIEND_REMOVE "FriendRemove"
#define CHATCOMMAND_FRIEND_TARGETREMOVE "FriendTargetRemoved"

#define CHATCOMMAND_IGNORE_ADD "IgnoreAdd"
#define CHATCOMMAND_IGNORE_TARGETADD "IgnoreTargetAdd"
#define CHATCOMMAND_IGNORE_REMOVE "IgnoreRemove"
#define CHATCOMMAND_IGNORE_TARGETREMOVE "IgnoreTargetRemove"

// Mail Commands
#define CHATCOMMAND_SENDMAIL "SendMail"
#define CHATCOMMAND_RECEIVEMAIL "ReceiveMail"
#define CHATCOMMAND_DELETEMAIL "DeleteMail"

#endif