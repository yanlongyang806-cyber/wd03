/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "Message.h"
#include "NotifyEnum.h"

typedef struct ChatData ChatData;
typedef struct ContactHeadshotData ContactHeadshotData;
typedef struct Entity Entity;
typedef struct PlayerCostume PlayerCostume;

extern StaticDefineInt NotifyTypeEnum[];

AUTO_ENUM;
typedef enum NotifySettingFlags
{
	kNotifySettingFlags_None = 0,
	kNotifySettingFlags_DisableChat = (1 << 0),
		// If set, disable sending messages to the chat log
	kNotifySettingFlags_DisableQueue = (1 << 1),
		// If set, disable queuing notify actions
	kNotifySettingFlags_DisableTutorial = (1 << 1),
		// If set, disable tutorial notify actions
} NotifySettingFlags;

AUTO_STRUCT AST_CONTAINER;
typedef struct NotifySetting
{
	const char* pchNotifyGroupName; AST(KEY POOL_STRING PERSIST NO_TRANSACT)

	NotifySettingFlags eFlags; AST(FLAGS SUBTABLE(NotifySettingFlagsEnum) PERSIST NO_TRANSACT)
} NotifySetting;

AUTO_STRUCT;
typedef struct NotifySettingsGroupDef
{
	const char* pchName; AST(STRUCTPARAM KEY POOL_STRING)
		// The internal name of this group

	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
		// The display name of this group of notifications
		
	DisplayMessage msgDescription; AST(NAME(Description) STRUCT(parse_DisplayMessage))
		// The description of this group of notifications
		
	NotifyType* peNotifyTypes; AST(NAME(NotifyType) SUBTABLE(NotifyTypeEnum))
		// The notifications that apply to this group

	NotifySettingFlags eFlags; AST(FLAGS SUBTABLE(NotifySettingFlagsEnum))
		// The default notification settings for this group
} NotifySettingsGroupDef;

AUTO_STRUCT;
typedef struct NotifySettingsCategoryDef
{
	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
		// The display name of this category of notifications
	
	NotifySettingsGroupDef** eaGroupDefs; AST(NAME(Group) NO_INDEX)
		// List of groups in this category
} NotifySettingsCategoryDef;

AUTO_STRUCT;
typedef struct NotifySettingsDef
{
	NotifySettingsCategoryDef** eaCategoryDefs; AST(NAME(Category))
} NotifySettingsDef;


#if defined(GAMECLIENT) || defined(GAMESERVER)
void notify_NotifySend(	Entity *pEnt, 
						NotifyType eType, 
						SA_PARAM_OP_STR const char *pcDisplayString, 
						SA_PARAM_OP_STR const char *pcLogicalString, 
						SA_PARAM_OP_STR const char *pcTexture);

void notify_NotifySendMessageStruct( Entity *pEnt, 
						NotifyType eType, 
						MessageStruct *pFmt);

void notify_NotifySendAudio(Entity *pEnt, 
							NotifyType eType, 
							SA_PARAM_OP_STR const char *pcDisplayString, 
							SA_PARAM_OP_STR const char *pcLogicalString, 
							SA_PARAM_OP_STR const char *pcSound, 
							SA_PARAM_OP_STR const char *pcTexture);

void notify_NotifySendWithData(	Entity *pEnt, 
								NotifyType eType, 
								SA_PARAM_OP_STR const char *pcDisplayString, 
								SA_PARAM_OP_STR const char *pcLogicalString, 
								SA_PARAM_OP_STR const char *pcSound, 
								SA_PARAM_OP_STR const char *pcTexture, 
								SA_PARAM_OP_VALID const ChatData *pData);

void notify_NotifySendWithHeadshot(	Entity *pEnt, 
									NotifyType eType, 
									SA_PARAM_OP_STR const char *pcDisplayString, 
									SA_PARAM_OP_STR const char *pcLogicalString, 
									SA_PARAM_OP_STR const char *pcSound, 
									SA_PARAM_OP_VALID ContactHeadshotData *pHeadshotData);

void notify_NotifySendWithTag(	Entity *pEnt, 
								NotifyType eType, 
								SA_PARAM_OP_STR const char *pcDisplayString, 
								SA_PARAM_OP_STR const char *pcLogicalString, 
								SA_PARAM_OP_STR const char *pcTexture, 
								const char *pcTag);

void notify_NotifySendWithOrigin(	Entity *pEnt, 
									NotifyType eType, 
									SA_PARAM_OP_STR const char *pcDisplayString, 
									SA_PARAM_OP_STR const char *pcLogicalString, 
									SA_PARAM_OP_STR const char *pcTag, 
									S32 iValue,
									const Vec3 vOrigin);

void notify_NotifySendWithItemID(	Entity *pEnt, 
									NotifyType eType, 
									SA_PARAM_OP_STR const char *pcDisplayString, 
									SA_PARAM_OP_STR const char *pcLogicalString, 
									SA_PARAM_OP_STR const char *pcTexture,
									U64 itemID,
									S32 iCount);

#endif