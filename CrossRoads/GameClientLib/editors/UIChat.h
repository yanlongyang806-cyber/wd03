/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

//Uses UI2lib to show a chat interface.
//
//NOTE: This is currently extremely tied to the UGC editor.  Do not
//use this in other editors unless you are okay with that!

#pragma once

#include "UILib.h"

typedef struct UIPane UIPane;
typedef struct UILabel UILabel;
typedef struct UISMFView UISMFView;
typedef struct UITextEntry UITextEntry;
typedef struct UIButton UIButton;
typedef struct ChatLogEntry ChatLogEntry;

typedef struct UIChat{
	UI_INHERIT_FROM( UI_WIDGET_TYPE );	//it's a widget!

	const char* strPrivateHandle;

	UILabel *chat_header;				//header says "Chat"
	UIScrollArea *message_log_scroll_area;	//scroll area for the message log.
	UISMFView *message_log;				//smf for the entire chat history
	S32 last_message_id;				//last log's ID. Kept so things can update when it changes.
	S32 last_message_displayed_id;
	UITextEntry *chat_text_entry;		//the place where you type chats
	UIButton *chat_multi_button;		//one button to bring them all and in the darkness bind them
	UIButton *chat_close_button;
	
	UIActivationFunc changedF;
	UserData changedData;
}UIChat;

//all the necessary widget functions:
UIChat* ui_ChatCreate();
UIChat* ui_ChatCreateForPrivateMessages( const char* handle );

void  ui_ChatTick(UIChat* chat, UI_PARENT_ARGS);
void  ui_ChatDraw(UIChat* chat, UI_PARENT_ARGS);
void  ui_ChatFreeInternal(UIChat* chat);

void ui_ChatSetChangedCallback( UIChat* chat, UIActivationFunc changedF, UserData changedData );
