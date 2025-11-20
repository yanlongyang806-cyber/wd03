/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

//Uses UI2lib to show a chat interface.

#include "UIChat.h"
//chat
#include "gclChat.h"
#include "gclChatLog.h"
#include "chatCommonStructs.h"
#include "gclChatConfig.h"
#include "gclEntity.h"
//UI2
#include "UIList.h"
#include "UIPane.h"
#include "UITextEntry.h"
#include "UIButton.h"
#include "UIMenu.h"
#include "UILabel.h"
#include "inputMouse.h"
#include "GfxClipper.h"
//utils
#include "Estring.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define STANDARD_ROW_HEIGHT 26

//Table of Contents:
//create
//init
static void ui_ChatInitialize(UIChat* chat);
//tick
//draw
//free
//send messages
static void ui_ChatSendMessageCB(UIWidget* textEntry, UIChat* chat);
//smf from ChatLog
static S32 ui_ChatSMFFromChatLog(UIChat* chat, char **estrMessageLog);
//show menu
static void ui_ChatMenuShowCB(UIButton* ignored, UIChat* chat);
//menu selection
static void ui_ChatMenuSelectCB(UIAnyWidget* ui_MenuItem, UserData mystery);
static void ui_ChatMenuSetCurrentCB(UIAnyWidget* ui_MenuItem, UIChat* chat);
static void ui_ChatMenuLeaveCB(UIAnyWidget* ui_MenuItem, UIChat* chat);

//creates a new UIChat.
UIChat* ui_ChatCreate(){
	UIChat* chat = calloc( 1, sizeof(*chat));
	ui_WidgetInitialize( UI_WIDGET( chat ), ui_ChatTick, ui_ChatDraw, ui_ChatFreeInternal, NULL, NULL );
	ui_ChatInitialize(chat);
	return chat;
}

UIChat* ui_ChatCreateForPrivateMessages( const char* handle )
{
	UIChat* chat = calloc( 1, sizeof(*chat));
	chat->strPrivateHandle = strdup( handle );
	ui_WidgetInitialize( UI_WIDGET( chat ), ui_ChatTick, ui_ChatDraw, ui_ChatFreeInternal, NULL, NULL );
	ui_ChatInitialize(chat);

	// Don't pass in @HANDLE or PLAYER@HANDLE.  Just pass in HANDLE.
	assert(!strchr( handle, '@' ));
	return chat;
}

//setup the UIChat's sub-widgets:
static void ui_ChatInitialize(UIChat* chat)
{
	int i = 0;
	UIPane *pane;
	//add header:
	chat->chat_header = ui_LabelCreate(NULL, 0,0);
	ui_WidgetSetTextMessage( UI_WIDGET( chat->chat_header ), "UGC_Chat.EntryLabel" );
	ui_LabelResize( chat->chat_header );
	ui_WidgetSetPositionEx(UI_WIDGET(chat->chat_header), 0, 4, 0, 0, UIBottomLeft);
	ui_WidgetAddChild( UI_WIDGET(chat), UI_WIDGET(chat->chat_header));
	
	//message log:
	pane = ui_PaneCreate(0, 0, 1, 1, UIUnitPercentage, UIUnitPercentage, 0);
		//use the chatLogStyle for this pane.
	ui_PaneSetStyle( pane, UI_GET_SKIN(chat)->astrChatLogStyle, UI_GET_SKIN(chat)->bUseTextureAssemblies, false );
	ui_WidgetAddChild( UI_WIDGET(chat), UI_WIDGET(pane));
	ui_WidgetSetPaddingEx( UI_WIDGET( pane ), 0, 0, 0, STANDARD_ROW_HEIGHT);

	chat->message_log_scroll_area = ui_ScrollAreaCreate(0,0,0,0,0,0,false,true);
	chat->message_log_scroll_area->autosize = 1;
	ui_WidgetSetDimensionsEx( UI_WIDGET( chat->message_log_scroll_area ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_PaneAddChild( pane, UI_WIDGET( chat->message_log_scroll_area ));
	chat->message_log = ui_SMFViewCreate(0,0,0,0);
	COPY_HANDLE( chat->message_log->widget.hOverrideFont, UI_GET_SKIN(chat)->hChatLogFont );
	ui_WidgetSetDimensionsEx( UI_WIDGET( chat->message_log ), 1, 1000, UIUnitPercentage, UIUnitFixed );
	ui_WidgetAddChild( UI_WIDGET(chat->message_log_scroll_area), UI_WIDGET( chat->message_log ));
	
	//force update message_log on next tick:
	chat->last_message_id = -1;
	
	//create multi-button:
	if( !chat->strPrivateHandle ) {
		chat->chat_multi_button = ui_ButtonCreate( NULL, 0, 0, ui_ChatMenuShowCB, chat);
		ui_ButtonSetMessageAndResize( chat->chat_multi_button, "UGC_Chat.Options" );
		ui_WidgetAddChild( UI_WIDGET(chat), UI_WIDGET(chat->chat_multi_button));
		ui_WidgetSetPositionEx(UI_WIDGET(chat->chat_multi_button), 0, 0, 0, 0, UIBottomRight);
	}
	
	//setup text entry:
	chat->chat_text_entry = ui_TextEntryCreate("", 0, 0 );
	if( chat->chat_multi_button ) {
		ui_WidgetSetPaddingEx( UI_WIDGET( chat->chat_text_entry ), ui_WidgetGetNextX( UI_WIDGET( chat->chat_header )) + 4, ui_WidgetGetNextX( UI_WIDGET( chat->chat_multi_button )) + 4, 0, 0);
	} else {
		ui_WidgetSetPaddingEx( UI_WIDGET( chat->chat_text_entry ), ui_WidgetGetNextX( UI_WIDGET( chat->chat_header )) + 4, 0, 0, 0);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(chat->chat_text_entry), 0, 0, 0, 0, UIBottomLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(chat->chat_text_entry), 1, UIUnitPercentage);
	ui_WidgetAddChild( UI_WIDGET(chat), UI_WIDGET( chat->chat_text_entry ));
	ui_TextEntrySetEnterCallback(chat->chat_text_entry, ui_ChatSendMessageCB, chat);

	if( !chat->strPrivateHandle ) {
		//SIP TODO:Move somewhere better encapsulated.
		//add Foundry channel and set to talk to it:
		ClientChat_JoinOrCreateChannel(UGCEDIT_CHANNEL_NAME);
		ClientChat_SetCurrentChannelByName(UGCEDIT_CHANNEL_NAME);
		////get the current channel:
		//uiChatMenuRefreshEntryDefaultText(chat);
	}
}

//update the widget this frame.
void ui_ChatTick(UIChat* chat, UI_PARENT_ARGS)
{		
	//F32 xPos, yPos;
	UI_GET_COORDINATES( chat );
	UI_TICK_EARLY( chat, true, false );
	if (eaSize(&g_ChatLog) && g_ChatLog[eaSize(&g_ChatLog) - 1]->id != chat->last_message_id){
		S32 lastDisplayedID;
		//update the SMF text to include all chats:
		char *new_message_log = NULL;
		estrCreate(&new_message_log);
		lastDisplayedID = ui_ChatSMFFromChatLog(chat, &new_message_log);
		ui_SMFViewSetText(chat->message_log, new_message_log, NULL);
		estrDestroy(&new_message_log);
		//scroll to the bottom:
		ui_ScrollAreaScrollToPosition(chat->message_log_scroll_area,
										0, ui_SMFViewGetHeight(chat->message_log));


		//remember the last message id:
		chat->last_message_id = g_ChatLog[eaSize(&g_ChatLog) - 1]->id;
		if( chat->last_message_displayed_id != lastDisplayedID ) {
			chat->last_message_displayed_id = lastDisplayedID;
			if( chat->changedF ) {
				chat->changedF( chat, chat->changedData );
			}
		}
	}

	//there could have been a command that changes the talk channel, so refresh that:
	if( !chat->strPrivateHandle ) {
		char displayName[ 256 ];
		char systemName[256];
		strcpy(displayName, ClientChat_GetCurrentChannel());
		strcpy(systemName, ClientChat_GetCurrentChannelSystemName());
		
		ui_EditableSetDefaultString(UI_EDITABLE(chat->chat_text_entry), displayName);

		if(   stricmp(systemName, LOCAL_CHANNEL_NAME) == 0
			  || stricmp(systemName, ZONE_CHANNEL_NAME) == 0
			  || strStartsWith( systemName, ZONE_CHANNEL_PREFIX )) {
			if( stricmp( systemName, UGCEDIT_CHANNEL_NAME ) != 0 ) {
				ClientChat_SetCurrentChannelByName( UGCEDIT_CHANNEL_NAME );
			}
		}
	}
	UI_TICK_LATE( chat );
}

//draw children.
void ui_ChatDraw(UIChat* chat, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(chat);
	UI_DRAW_EARLY(chat);
	UI_DRAW_LATE(chat);
}

//delete UIChat
void ui_ChatFreeInternal( UIChat* chat)
{
	ui_WidgetFreeInternal( UI_WIDGET( chat ));
}

//called when ENTER is pressed in the chat text entry to send the message.
static void ui_ChatSendMessageCB(UIWidget* textEntry, UIChat* chat){
	if( chat->strPrivateHandle ) {
		ClientChat_SendPrivateMessage( ui_TextEntryGetText((UITextEntry*)textEntry), chat->strPrivateHandle );
	} else {
		ClientChat_SendMessage(ui_TextEntryGetText((UITextEntry*)textEntry), NULL);
	}
	ui_TextEntrySetText( (UITextEntry*)textEntry, "");

	//this could have been a command that changes the talk channel, so force tick() to refresh:
	chat->last_message_id = 0;
}
//turns chat log into an smf char*.
static S32 ui_ChatSMFFromChatLog(UIChat* chat, char **estrMessageLog){
	S32 lastDisplayedID = 0;
	char *buffer = NULL;
	ChatLogEntry *message;
	int i;
	Entity *pRequester = entActivePlayerPtr();
	estrStackCreate(&buffer);
	//iterate through chat logs
	for (i = 0; i < eaSize(&g_ChatLog); i++){
		message = g_ChatLog[i];

		// If we are a private window, filter out irrevelvant messages
		if( chat->strPrivateHandle ) {
			if( !ChatCommon_IsLogEntryVisibleInPrivateMessageWindow( message->pMsg, chat->strPrivateHandle )) {
				continue;
			}
		}
		
		//parse each
		estrPrintf(&buffer, "<font color=#%08X>", 
					ChatCommon_GetChatColor(ClientChatConfig_GetChatConfig(pRequester),
											message->pMsg->eType, message->pMsg->pchChannel,
											ChatCommon_GetChatConfigSourceForEntity(pRequester)));
		estrAppend(estrMessageLog, &buffer);

		if(message->pMsg->pchChannel && message->pMsg->pFrom && message->pMsg->pFrom->pchHandle && message->pMsg->pTo && message->pMsg->pTo->pchHandle){
			estrPrintf(&buffer, "[%s]  <b>%s to @%s:</b> %s<br>", message->pMsg->pchChannel,
				message->pMsg->pFrom->pchHandle, message->pMsg->pTo->pchHandle, 
				gclChatConfig_FilterProfanity(pRequester, message->pMsg->pchText));
		}
		else if(message->pMsg->pchChannel && message->pMsg->pFrom && message->pMsg->pFrom->pchHandle){
				estrPrintf(&buffer, "[%s] <b>%s:</b> %s<br>", message->pMsg->pchChannel,
					message->pMsg->pFrom->pchHandle, 
					gclChatConfig_FilterProfanity(pRequester, message->pMsg->pchText));
		}
		else if(message->pMsg->pFrom && message->pMsg->pFrom->pchHandle){
			estrPrintf(&buffer, "</b>%s:</b> %s<br>", message->pMsg->pFrom->pchHandle, 
						gclChatConfig_FilterProfanity(pRequester, message->pMsg->pchText));
		}
		else{
			estrPrintf(&buffer, "%s<br>", 
						gclChatConfig_FilterProfanity(pRequester, message->pMsg->pchText));
		}
		estrAppend(estrMessageLog, &buffer);

		estrPrintf(&buffer, "</font>");
		//append to buffer
		estrAppend(estrMessageLog, &buffer);

		lastDisplayedID = message->id;
	}
	estrDestroy(&buffer);

	return lastDisplayedID;
}

//pop-up a menu for the chat. Called when the button is pressed.
static void ui_ChatMenuShowCB(UIButton* ignored, UIChat* chat){
	UIMenu* menu = NULL;
	UIMenuItem *submenu = NULL;
	const char ***channels = ClientChat_GetSubscribedCustomChannels();
	int i;
	menu = ui_MenuCreate("options");

	//set current chat submenu:
	submenu = ui_MenuItemCreateMessage( "UGC_Chat.SetTalkChannel", UIMenuSubmenu, ui_ChatMenuSelectCB, chat, NULL);
	submenu->data.menu = ui_MenuCreate("Talk");
	ui_MenuAppendItem(menu, submenu);
	ui_MenuAppendItem(submenu->data.menu, ui_MenuItemCreate(UGCEDIT_CHANNEL_NAME, UIMenuCallback, ui_ChatMenuSetCurrentCB, chat, NULL));
	for (i=0; i<eaSize(channels); i++){
		ui_MenuAppendItem(submenu->data.menu, ui_MenuItemCreate((*channels)[i], UIMenuCallback, ui_ChatMenuSetCurrentCB, chat, NULL));
	}

	//create/join options:
	ui_MenuAppendItem(menu, ui_MenuItemCreateMessage( "UGC_Chat.JoinChannel", UIMenuCallback, ui_ChatMenuSelectCB, chat, NULL ));

	//the "leave" submenu:
	submenu = ui_MenuItemCreateMessage( "UGC_Chat.LeaveChannel", UIMenuSubmenu, ui_ChatMenuSelectCB, chat, NULL );
	submenu->data.menu = ui_MenuCreate( "Leave" );
	ui_MenuAppendItem(menu, submenu);
	if( eaSize( channels )) {
		for (i=0; i<eaSize(channels); i++){
			ui_MenuAppendItem(submenu->data.menu, ui_MenuItemCreate((*channels)[i], UIMenuCallback, ui_ChatMenuLeaveCB, chat, NULL));
		}
	} else {
		ui_MenuAppendItem( submenu->data.menu, ui_MenuItemCreateMessage( "UGC_Chat.NoChannelsToLeave", UIMenuCallback, NULL, NULL, NULL ));
	}


	ui_WidgetSetWidth(UI_WIDGET(menu), 200);
	ui_MenuPopupAtCursorOrWidgetBox(menu);
}


//called when something is picked from the chat pop-up menu. Just fills in the text command in the
//chat box. 
static void ui_ChatMenuSelectCB(UIAnyWidget* ui_MenuItem, UIChat* chat){
	UIMenuItem* menuItem = (UIMenuItem*) ui_MenuItem;
	UITextEntry* textEntry = chat->chat_text_entry;

	//insert auto-command text so user can add channel name:
	if(!stricmp(ui_MenuItemGetText(menuItem), "Join Channel")) { 
		ui_TextEntrySetText(textEntry, "/Channel_Create ");
	}
	//set focus to text entry:
	ui_SetFocus(textEntry); 
	ui_TextEntrySetCursorPosition(textEntry, -1);

}

//called when a channel is picked from the Set Talk channel sub-menu.  Sets the channel as current.
static void ui_ChatMenuSetCurrentCB(UIAnyWidget* ui_MenuItem, UIChat* chat){
	UIMenuItem* menuItem = (UIMenuItem*) ui_MenuItem;
	ClientChat_SetCurrentChannelByName(ui_MenuItemGetText(menuItem));
}

//called when a channel is picked from the leave channel sub-menu.  Leaves the channel.
static void ui_ChatMenuLeaveCB(UIAnyWidget* ui_MenuItem, UIChat* chat){
	UIMenuItem* menuItem = (UIMenuItem*) ui_MenuItem;
	ClientChat_LeaveChannel(ui_MenuItemGetText(menuItem));
}

void ui_ChatSetChangedCallback( UIChat* chat, UIActivationFunc changedF, UserData changedData )
{
	chat->changedF = changedF;
	chat->changedData = changedData;
}
