#include "EditorManagerUIMotD.h"

#ifndef NO_EDITORS

#include "EditorManagerPrivate.h"
#include "EditorManagerOptions.h"
#include "EditorPrefs.h"
#include "GfxSprite.h"
#include "GfxPrimitive.h"
#include "smf_render.h"
#include "inputLib.h"
#include "inputMouse.h"
#include "gimmeDLLWrapper.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/********************
* REGISTRATION
********************/

void emRegisterMotDForEditorAndGroup(int year, int month, int day, const char *keyname, const char *editor, const char *group, const char *text)
{
	char **eaEditors = NULL;
	char **eaGroups = NULL;
	if (editor != NULL)
	{
		estrTokenize(&eaEditors, ",", editor);
	}
	if (group != NULL)
	{
		estrTokenize(&eaGroups, ",", group);
	}
	emRegisterMotDEx(year, month, day, keyname, eaEditors, eaGroups, text);
}

typedef struct tm TimeStruct;

void emRegisterMotDEx(int year, int month, int day, const char *keyname, const char *const *relevant_editors, const char *const *relevant_groups, const char *text)
{
	EMMessageOfTheDay *motd = calloc(1, sizeof(EMMessageOfTheDay));
	char *keyNameOut = NULL;
	bool bUnique = false;
	TimeStruct _tm;
	memset(&_tm, 0, sizeof(_tm));

	devassertmsg(keyname && keyname[0], "You must provide a non-empty keyname when registering a Message to the MotD system.");
	devassertmsg(text && text[0], "You must provide non-empty text when registering a Message to the MotD system.");

	bUnique = stashAddPointer(em_data.message_of_the_day.registered_messages_by_keyname, estrDup(keyname), motd, false);

	devassertmsg(bUnique, "You must provide a unique keyname when registering a Message to the MotD system.");

	_tm.tm_year = year - 1900;
	_tm.tm_mon = month - 1;
	_tm.tm_mday = day;

	motd->keyname = strdup(keyname);
	motd->text = strdup(text);
	motd->registration_timestamp = _mktime32(&_tm);
	motd->relevant_editors = relevant_editors;
	motd->relevant_groups = relevant_groups;

	motd->never_show_again = EditorPrefGetInt("Editor", "Messages.NeverShowAgain", keyname, false);
	motd->last_shown_timestamp = EditorPrefGetInt("Editor", "Messages.LastShownTimestamp", keyname, 0);

	eaPush(&em_data.message_of_the_day.registered_messages, motd);
}

/********************
* OPTIONS
********************/

static bool s_bShowInViewport = true;

bool emMotDGetShowInViewport(void)
{
	return s_bShowInViewport;
}

void emMotDSetShowInViewport(bool show_in_viewport)
{
	s_bShowInViewport = show_in_viewport;
}

/********************
* REFRESH/DRAW
********************/

void emMotDCycle(void)
{
	EMMessageOfTheDay **messages_we_will_show = NULL;
	int count = eaSize(&em_data.message_of_the_day.registered_messages);
	int i;

	for(i = 0; i < count; i++)
	{
		EMMessageOfTheDay *motd = em_data.message_of_the_day.registered_messages[i];

		if(motd->never_show_again)
			continue;

		if(eaSize(&motd->relevant_editors) > 0)
		{
			bool relevant = false;

			if(em_data.current_editor)
			{
				int j;
				for(j = 0; j < eaSize(&motd->relevant_editors); j++)
					if(stricmp(motd->relevant_editors[j], em_data.current_editor->editor_name) == 0)
					{
						relevant = true;
						break;
					}
			}

			if(!relevant)
				continue;
		}

		if(eaSize(&motd->relevant_groups) > 0)
		{
			bool relevant = true;

			if(gimmeDLLQueryExists())
			{
				const char *const *groups = gimmeDLLQueryGroupList();
				int j, k;
				if(eaSize(&groups) > 0)
				{
					relevant = false;
					for(j = 0; j < eaSize(&groups); j++)
					{
						for(k = 0; k < eaSize(&motd->relevant_groups); k++)
							if(stricmp(motd->relevant_groups[k], groups[j]) == 0)
							{
								relevant = true;
								break;
							}
						if(relevant)
							break;
					}
				}
			}

			if(!relevant)
				continue;
		}

		eaPush(&messages_we_will_show, motd);
	}

	count = eaSize(&messages_we_will_show);
	if(count > 0)
		em_data.message_of_the_day.current_message = messages_we_will_show[randInt(count)];
	else
		em_data.message_of_the_day.current_message = NULL;

	if(em_data.message_of_the_day.current_message)
		em_data.message_of_the_day.current_message->rendered_this_time = false;

	eaDestroy(&messages_we_will_show);
}

void emMotDRefresh(void)
{
	static SMFBlock *s_pSMF = NULL;

	if(!emMotDGetShowInViewport())
		return;

	if(em_data.message_of_the_day.current_message && !em_data.message_of_the_day.current_message->never_show_again)
	{
		// show Message of the Day on main viewport. Currently, always at bottom-right.
		UIStyleFont *font = ui_StyleFontGet("EditorManager_ViewportMessageOfTheDay");
		assert(font);
		ui_StyleFontUse(font, false, 0);

		{
			static char *buffer = NULL;
			CBox box;
			char date_buf[32];
			TimeStruct time_struct;
			const char *text = em_data.message_of_the_day.current_message->text;
			int viewport_width = g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0];
			int x = 0;
			int y = 0;
			int width;
			int height;

			if(!s_pSMF)
				s_pSMF = smfblock_Create();

			if(!buffer)
				estrCreate(&buffer);

			_localtime32_s(&time_struct, &em_data.message_of_the_day.current_message->registration_timestamp);
			strftime(date_buf, sizeof(date_buf), "%x", &time_struct);

			estrPrintf(&buffer, "%s (%s)", text, date_buf);

			smf_ParseAndFormat(s_pSMF, buffer, 0, 0, /*z=*/5000, viewport_width - UI_DSTEP, /*h=*/100000, false, false, false, NULL);
			width = min(viewport_width, smfblock_GetMinWidth(s_pSMF) + UI_DSTEP);
			height = smfblock_GetHeight(s_pSMF) + UI_DSTEP;
			x = g_ui_State.viewportMax[0] - width;
			x = max(0, x);
			y = g_ui_State.viewportMax[1] - height;
			display_sprite(g_ui_Tex.white, x, y, 5001, (F32)width / g_ui_Tex.white->width, (F32)height / g_ui_Tex.white->height, 0xFFFF99FF);
			gfxDrawBox(x + 1, y, x + width, y + height, 5002, colorFromRGBA(0x000000FF));
			smf_ParseAndDisplay(s_pSMF, buffer, x + UI_STEP, y + UI_STEP, 5003, viewport_width - UI_DSTEP, /*h=*/100000, false, false, false, NULL, 0xFF, NULL);

			if(!em_data.message_of_the_day.current_message->rendered_this_time)
			{
				em_data.message_of_the_day.current_message->rendered_this_time = true;
				em_data.message_of_the_day.current_message->last_shown_timestamp = _time32(NULL); // now

				EditorPrefStoreInt("Editor", "Messages.LastShownTimestamp", em_data.message_of_the_day.current_message->keyname, em_data.message_of_the_day.current_message->last_shown_timestamp);
			}

			box.left = x;
			box.right = x + width;
			box.top = y;
			box.bottom = y + height;

			if(!inpCheckHandled() && mouseCollision(&box))
			{
				if(mouseClick(MS_RIGHT))
				{
					emOptionsShowEx("Messages");

					inpHandled();
				}
				else if(mouseClick(MS_LEFT))
				{
					em_data.message_of_the_day.current_message = NULL;

					inpHandled();
				}
			}
		}
	}
}

#endif
