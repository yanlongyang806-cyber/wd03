#pragma once

typedef struct ChatUser ChatUser;
typedef struct NetLink NetLink;

void userSilence(ChatUser *user, U32 duration);
void userUnsilence(ChatUser *user, bool bManualCall);

void userIncrementNaughtyValueWithSpam (ChatUser *user, int iAddNaughty, const char *reason, const char *spamMsg);
void userIncrementNaughtyValue (ChatUser *user, int iAddNaughty, const char *reason);
void banSpammer(ChatUser* user, const char *reason);
void userSpamSilence(SA_PARAM_NN_VALID ChatUser *user, U32 curTime);

void saveBlacklistFile(void);
void blacklist_InitShardChatServer(NetLink *link);