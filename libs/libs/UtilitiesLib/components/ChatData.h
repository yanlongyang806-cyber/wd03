#pragma once
GCC_SYSTEM

AUTO_ENUM;
typedef enum ChatLinkType {
	kChatLinkType_Unknown,
	kChatLinkType_Item,
	kChatLinkType_ItemDef,
	kChatLinkType_PowerDef, 
	kChatLinkType_PlayerHandle,
} ChatLinkType;

AUTO_ENUM;
typedef enum ChatLinkAttribute {
	kChatLinkAttr_Game,
	kChatLinkAttr_Type,
	kChatLinkAttr_Key,
	kChatLinkAttr_Text,
} ChatLinkAttribute;

AUTO_STRUCT;
typedef struct ChatLink {
	// The game/product this link is for
	const char *pchGame; AST(POOL_STRING)
	// The type of data being linked
	ChatLinkType eType; AST(NAME(Type))
	// Type-specific lookup key
	char *pchKey; AST(ESTRING)
	// Sender pre-localized display text
	char *pchText; AST(ESTRING)
	// When available, this contains ChatLogEntry->pMsg->pchText.
	char *pchText2; AST(ESTRING NAME(Text2))
	// If this is a player link, then this is the Ent
	U32 iEntContainerID;

	// If this is a player link, then this indicates if the player is a GM
	bool bIsGM : 1; AST(NAME(IsGM))
	// If this is a player link, then this indicates if the player is a Dev
	bool bIsDev : 1; AST(NAME(IsDev))
} ChatLink;

AUTO_STRUCT;
typedef struct ChatLinkInfo {
	ChatLink *pLink;
	S32 iStart;
	S32 iLength;
} ChatLinkInfo;

AUTO_STRUCT;
typedef struct ChatBubbleData
{
	F32 fDuration;
	const char *pchBubbleStyle;
} ChatBubbleData;

AUTO_STRUCT;
typedef struct ChatData
{
	bool bEmote;
	ChatLinkInfo **eaLinkInfos;
	ChatBubbleData *pBubbleData;
} ChatData;

extern ParseTable parse_ChatLinkInfo[];
#define TYPE_parse_ChatLinkInfo ChatLinkInfo
extern ParseTable parse_ChatLink[];
#define TYPE_parse_ChatLink ChatLink
extern ParseTable parse_ChatData[];
#define TYPE_parse_ChatData ChatData

SA_RET_NN_VALID extern ChatLink *ChatData_CreateLink(ChatLinkType eType, SA_PARAM_NN_STR const char *pchKey, SA_PARAM_NN_STR const char *pchDisplayText);
SA_RET_NN_VALID extern ChatLink *ChatData_CreatePlayerLink(SA_PARAM_NN_STR const char *pchNameAndHandle, SA_PARAM_OP_STR const char *pchDisplayText, bool bIsGM, bool bIsDev, SA_PARAM_OP_STR const char *pchFullMsg, U32 iEntContainerID);

// Search for a reference to pchNameAndHandle within the given message
// and generate a ChatLink that points to it.  If the pchNameAndHandle
// can't be found this returns NULL.
//
// The '@' character must be present in the pchNameAndHandle.
// The name part of pchNameAndHandle is optional, but the handle is not.  
// This is because links to player handles only make sense if the handle
// is present.  The name may provide additional context for link operations
// that require it (such as team & guild commands).
SA_RET_OP_VALID extern ChatLinkInfo *ChatData_CreatePlayerHandleLinkInfoFromMessage(const char *pchMessage, const char *pchNameAndHandle, bool bIsGM, bool bIsDev);

// Search for a reference to pchNameAndHandle within the given message
// and generate a ChatData that contains a ChatLinkInfo that points to it.  
// If the pchNameAndHandle can't be found this returns NULL.
SA_RET_OP_VALID extern ChatData *ChatData_CreatePlayerHandleDataFromMessage(const char *pchMessage, const char *pchNameAndHandle, bool bIsGM, bool bIsDev);

// Encode ChatData/ChatLinks into struct parsable text and back
extern bool ChatData_Encode(SA_PARAM_OP_VALID char **ppchEncodedData, SA_PARAM_OP_VALID ChatData *pData);
extern bool ChatData_Decode(SA_PARAM_OP_VALID const char *pchEncodedData, SA_PARAM_OP_VALID ChatData *pData);
extern bool ChatData_EncodeLink(SA_PARAM_OP_VALID char **ppchEncodedLink, SA_PARAM_OP_VALID ChatLink *pLink);
extern bool ChatData_DecodeLink(SA_PARAM_OP_VALID const char *pchEncodedLink, SA_PARAM_OP_VALID ChatLink *pLink);