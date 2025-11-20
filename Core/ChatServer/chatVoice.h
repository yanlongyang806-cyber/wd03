#pragma once

typedef struct ChatChannel		ChatChannel;
typedef struct ChatUser			ChatUser;
typedef struct StashTableImp*	StashTable;
typedef struct UrlArgumentList	UrlArgumentList;

typedef void (*voidVoidFunc)(void);
typedef U32 (*u32VoidFunc)(void);
typedef void (*ResponseHandler)(void* userdata, const char* xmldoc, int len);
typedef void (*voidVoidStarFunc)(void *userdata);
typedef U32 (*u32VoidStarFunc)(void* userdata);

AUTO_STRUCT;
typedef struct HttpFormRequest {
	const char* url;					AST(POOL_STRING)
	UrlArgumentList *args;
	UrlArgumentList *backup_args;
	ResponseHandler cb;					NO_AST
	void* userdata;						NO_AST

	u32VoidStarFunc timeoutcb;			NO_AST
	void* timeoutdata;					NO_AST

	const char* file;					AST(POOL_STRING)
	int line;							

	U32 attempt_count;
	U32 isSignin : 1;
} HttpFormRequest;

typedef struct XMLExtractState {
	StashTable table;
	const char* curState;
	int keysFound;
} XMLExtractState;

typedef struct VoiceChatState{
	HttpFormRequest **http_requests;
	HttpFormRequest **active_reqs;

	voidVoidFunc tickCB;
	u32VoidFunc enabledCB;

	S64 timeLastReqSuccess;
	S64 timeLastSignin;
	int failuresSinceLastSuccess;

	const char* auth_token;
	S32 signed_in : 1;
	S32 signing_in : 1;
} VoiceChatState;

extern VoiceChatState g_VoiceChatState;

void cvOncePerFrame(void);
U32 cvIsVoiceEnabled(void);

void simpleFree(void* ptr);
void cvExtractInit(XMLExtractState *state, StashTable table);
void cvExtractKeysValues(const char* xmldoc, int len, XMLExtractState *state);

void chatVoiceTakeRequest(HttpFormRequest *req);
#define chatVoiceProcessPage(list, cb, userdata, owned) chatVoiceProcessPageEx(list, cb, userdata, NULL, NULL, owned, __FILE__, __LINE__)
HttpFormRequest* chatVoiceProcessPageEx(UrlArgumentList *list, ResponseHandler cb, void* userdata, u32VoidStarFunc timeoutcb, void* timeoutdata, int owned, const char* file, int line);

void chatVoiceCreateChannel(ChatChannel *channel);
void chatVoiceChannelDelete(ChatChannel* channel);
void chatVoiceJoin(ChatChannel *channel, ChatUser *user);
void chatVoiceLeave(ChatChannel *channel, ChatUser *user);