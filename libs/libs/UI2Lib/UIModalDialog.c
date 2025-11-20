
#include "cmdparse.h"
#include "sysutil.h"
#include "Color.h"

#include "GfxCamera.h"
#include "GfxClipper.h"
#include "GfxDebug.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"
#include "RenderLib.h"
#include "WorldGrid.h"
#include "inputKeyBind.h"
#include "inputMouse.h"
#include "partition_enums.h"
#include "StringUtil.h"

#include "inputLib.h"
#include "UILib.h"
#include "UIInternal.h"
#include "UIModalDialog.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define DLG_WIDTH 600
#define BUTTON_WIDTH 70
#define BUTTON_HEIGHT 30
#define LABEL_HEIGHT 14
#define TEXTENTRY_HEIGHT 20

static UIDialogButtons return_button;
static UIDialogButtons default_value, cancel_value;
static const char *dialog_message;
static const char *button_text[3];
static UISkin* window_skin;
static UITextEntry *text_entry;
static U32 last_modal_frame;

void ui_ModalDialogSetCustomButtons(SA_PARAM_OP_STR const char *button1, SA_PARAM_OP_STR const char *button2, SA_PARAM_OP_STR const char *button3)
{
	button_text[0] = button1;
	button_text[1] = button2;
	button_text[2] = button3;
}

void ui_ModalDialogSetCustomButton(int index, SA_PARAM_OP_STR const char *button)
{
	assert(index >= 0 && index < 3);
	button_text[index] = button;
}

void ui_ModalDialogSetCustomSkin(UISkin* skin)
{
	window_skin = skin;
}

static void pressButton(UIButton *button, void *value)
{
	return_button = (size_t)value;
	switch(return_button)
	{
		xcase UIOk:
		{
			// Do nothing
		}
#if !PLATFORM_CONSOLE
#define FILE_NAME_PREFIX "File: "
		xcase UIEditFile:
		{

			// Extract file name from message, if found, edit it w/ windows assigned editor
			if (dialog_message && strStartsWith(dialog_message, FILE_NAME_PREFIX)) {
				char filename[MAX_PATH];
				char *s;
				strcpy(filename, dialog_message + strlen(FILE_NAME_PREFIX));
				s = strchr(filename, '\n');
				if (s) {
					*s = '\0';
					fileLocateWrite(filename, filename);
					fileOpenWithEditor(filename);
				}
			}
			return_button = -1;
		}
		xcase UIOpenFolder:
		{
			// Extract file name from message, if found, edit it w/ windows assigned editor
			if (dialog_message && strStartsWith(dialog_message, FILE_NAME_PREFIX)) {
				char filename[MAX_PATH];
				char *s;
				strcpy(filename, dialog_message+strlen(FILE_NAME_PREFIX));
				s = strchr(filename, '\n');
				if (s) {
					char cmd[1024];
					*s = '\0';
					fileLocateWrite(filename, filename);
					backSlashes(filename);
					sprintf(cmd, "explorer.exe /select,\"%s\"", filename);
					system_detach(cmd, 0, 0);
				}
			}
			return_button = -1;
		}
#endif
		xcase UICopyToClipboard:
		{
			// Copy dialog message to clipboard
			winCopyToClipboard(dialog_message);
			return_button = -1;
		}
		xdefault:
		{

		}
	}
}

static void defaultButton(void *unused1, void *unused2)
{
	return_button = default_value;
}

static void escapeButton(void *unused1, void *unused2)
{
	return_button = cancel_value;
}

static bool closeButton(void *unused1, void *unused2)
{
	escapeButton(unused1, unused2);
	return true;
}

AUTO_COMMAND ACMD_NAME("Dialog.OK") ACMD_LIST(dialog_cmdlist) ACMD_ACCESSLEVEL(0);
void DialogCmdOK(void)
{
	defaultButton(0, 0);
}

AUTO_COMMAND ACMD_NAME("Dialog.Cancel") ACMD_LIST(dialog_cmdlist) ACMD_ACCESSLEVEL(0);
void DialogCmdCancel(void)
{
	escapeButton(0, 0);
}

AUTO_COMMAND ACMD_NAME("Dialog.Copy") ACMD_LIST(dialog_cmdlist) ACMD_ACCESSLEVEL(0);
void DialogCmdCopy(void)
{
	winCopyToClipboard(dialog_message);
}

CmdList dialog_cmdlist = {0};

static void initButtonWidget(UIWidget *widget)
{
	widget->offsetFrom = UIBottomLeft;
	MAX1(widget->width, BUTTON_WIDTH);
	widget->height = BUTTON_HEIGHT;
	widget->color[0] = CreateColorRGB(225, 225, 225);
	widget->color[1] = CreateColorRGB(225, 225, 225);
}

/// Suite of utility functions to handle word-wrap in UI2Lib
static char* ui_UTF8GetDisplayLineBegin( char* s )
{
	while( *s && UTF8isspace( UTF8ToCodepoint( s ), false )) {
		s = UTF8GetNextCodepoint( s );
	}

	return s;
}

static char* ui_UTF8GetNextPotentialWordWrap( char* s )
{
	while( *s && !UTF8isspace( UTF8ToCodepoint( s ), false )) {
		s = UTF8GetNextCodepoint( s );
	}

	return s;
}

static bool ui_UTF8IsForcedWordWrap( char* s )
{
	return *s == '\0' || *s == '\n' || *s == '\r';
}

static UIWindow *createDialogUI(const char *title, const char *message, Color text_color, UIDialogButtons buttons, char *modifyString)
{
	int i, x, y, height, mcount, tcount;
	int totalWidth;
	UIWindow *window;
	UILabel *label;
	UIButton *button;
	UIButton **button_list = NULL;
	char *message_dup, *s=NULL, *last=NULL;
	char **messages = NULL;
	char delim;

	strdup_alloca(message_dup, message);
	last = message_dup;
	while ( (s = strsep2(&last, "\r\n", &delim)) )
	{
		if (s)
			eaPush(&messages, s);
		else
			eaPush(&messages, "");
	}

	eaPush(&messages, "");
	// Added a button, so this is not necessary
	// eaPush(&messages, "Press CTRL-C to copy to clipboard.");
	// eaPush(&messages, "");

	window = ui_WindowCreate(title, 0, 0, DLG_WIDTH, 100);
	window->resizable = 0;
	ui_WindowSetCloseCallback(window, closeButton, NULL);

	// make labels for the text
	// take care of text wrapping
	mcount = 0;
	for (i = 0; i < eaSize(&messages); ++i)
	{
		int wrapped;
		s = messages[i];

		do
		{
			s = ui_UTF8GetDisplayLineBegin( s );
			
			wrapped = 0;
			label = ui_LabelCreate(s, 0, mcount * LABEL_HEIGHT);
			label->widget.color[0] = text_color;
			ui_WindowAddChild(window, label);
			++mcount;

			if (label->widget.width + label->widget.x > window->widget.width)
			{
				// find wrap point
				bool did_wrap = false;
				wrapped = 1;

				// first try word-wrapping
				if (!did_wrap) {
					char* validWrap = NULL;
					char* nextWrap = s;
					while( true ) {
						char c;
						nextWrap = ui_UTF8GetNextPotentialWordWrap( nextWrap );
						c = *nextWrap;
						*nextWrap = 0;
						if(ui_StyleFontWidth(ui_WidgetGetFont(UI_WIDGET(label)), 1.f, s) + label->widget.x <= window->widget.width)
						{
							*nextWrap = c;
							if( ui_UTF8IsForcedWordWrap( nextWrap ))
							{
								*nextWrap = 0;
								ui_LabelSetText(label, s);
								*nextWrap = c;
								s = nextWrap;
								did_wrap = true;
								break;
							}
							else
							{
								validWrap = nextWrap;
								nextWrap = UTF8GetNextCodepoint( nextWrap );
							}
						}
						else if(validWrap)
						{
							*nextWrap = c;
							c = *validWrap;
							*validWrap = 0;
							ui_LabelSetText(label, s);
							*validWrap = c;
							s = validWrap;
							did_wrap = true;
							break;
						}
						else
						{
							*nextWrap = c;
							break;
						}
					}
				}

				if (!did_wrap) {
					char* validWrap = NULL;
					char* nextWrap = UTF8GetNextCodepoint( s );
					while( true ) {
						char c;
						validWrap = nextWrap;
						nextWrap = UTF8GetNextCodepoint( nextWrap );
						c = *nextWrap;
						*nextWrap = 0;
						if( ui_StyleFontWidth( ui_WidgetGetFont( UI_WIDGET( label )), 1.f, s ) + label->widget.x > window->widget.width )
						{
							*nextWrap = c;
							c = *validWrap;
							*validWrap = 0;
							ui_LabelSetText(label, s);
							*validWrap = c;
							s = validWrap;
							did_wrap = true;
							break;
						}
						*nextWrap = c;
					}
				}
			}
		} while (wrapped);
	}

	tcount = 0;
	if (modifyString)
	{
		text_entry = ui_TextEntryCreate(modifyString, 0, mcount * LABEL_HEIGHT);
		((UIWidget *)text_entry)->offsetFrom = UITop;
		ui_WindowAddChild(window,text_entry);
		tcount = 1;
	}
	else
	{
		text_entry = NULL;
	}

	// set window size and position
	rdrGetDeviceSize(gfxGetActiveOrPrimaryDevice(), NULL, NULL, &x, &y, NULL, NULL, NULL, NULL);
	height = mcount * LABEL_HEIGHT + tcount * TEXTENTRY_HEIGHT + BUTTON_HEIGHT + 10;
	x -= DLG_WIDTH;
	x /= 2;
	y -= height;
	y /= 2;
	window->widget.x = x;
	window->widget.y = y;
	window->widget.height = height;

	default_value = UIOk;
	cancel_value = UIOk;

	totalWidth = 0;

	// make buttons
	{
		struct {
			UIDialogButtons flag;
			const char *text;
		} button_choice_list[] = {
			{UICopyToClipboard, "Copy Msg"},
			{UIEditFile, "Edit File"},
			{UIOpenFolder, "Open Folder"},
			{UICustomButton1, button_text[0]},
			{UICustomButton2, button_text[1]},
			{UICustomButton3, button_text[2]},
			{UIOk, "Ok"},
			{UIYes, "Yes"},
			{UINo, "No"},
			{UICancel, "Cancel"},
		};
		for (i=0; i<ARRAY_SIZE(button_choice_list); i++) {
			if (buttons & button_choice_list[i].flag)
			{
				button = ui_ButtonCreate(button_choice_list[i].text, 5, 5, pressButton, (void *)button_choice_list[i].flag);
				initButtonWidget(&button->widget);
				totalWidth += button->widget.width;
				ui_WindowAddChild(window, button);
				eaPush(&button_list, button);
				cancel_value = button_choice_list[i].flag;
			}
		}
	}

	// center buttons in dialog
	x = DLG_WIDTH - totalWidth - (eaSize(&button_list) - 1) * 10;
	x /= 2;
	for (i = 0; i < eaSize(&button_list); ++i)
	{
		button_list[i]->widget.x = x;
		x += 10;
		x += button_list[i]->widget.width;
	}

	eaDestroy(&button_list);
	eaDestroy(&messages);

	return window;
}

static UIDialogButtons ui_ModalTextEntryDialog(SA_PARAM_NN_STR const char *title, SA_PARAM_NN_STR const char *message,
										Color text_color, UIDialogButtons buttons,
										SA_PARAM_OP_STR char *modifyString, int modifyStringLen)
{
	UIWindow *dialog;
	UIGlobalState old_state;

	dialog_message = message;

	ui_ModalDialogBeforeWidgetAdd( &old_state );

	// add dialog
	dialog = createDialogUI(title?title:"Error", message?message:"NULL", text_color, buttons, modifyString);
	ui_WindowShow(dialog);

	if (window_skin)
		ui_WidgetSkin(UI_WIDGET(dialog), window_skin);

	ui_ModalDialogLoop();

	if (modifyString)
	{
		strncpy_s(modifyString,modifyStringLen,ui_TextEntryGetText(text_entry),modifyStringLen - 1);
	}

	// free dialog
	ui_WindowHide(dialog);
	ui_WindowFreeInternal(dialog);

	ui_ModalDialogAfterWidgetDestroy( &old_state );

	return return_button;
}

UIDialogButtons ui_ModalDialog(const char *title, const char *message, Color text_color, UIDialogButtons buttons)
{
	return ui_ModalTextEntryDialog(title, message, text_color, buttons, NULL, 0);
}

static char file_dir[MAX_PATH];
static char file_name[MAX_PATH];

static void selectFile(const char *dir, const char *fileName, UserData dummy)
{
	strcpy(file_dir, dir);
	strcpy(file_name, fileName);
	return_button = UIOk;
}

static void cancel(UserData dummy)
{
	return_button = UICancel;
}

UIDialogButtons ui_ModalFileBrowser(SA_PARAM_NN_STR const char *title,
									SA_PARAM_NN_STR const char *buttonText,
									UIBrowserMode mode,
									UIBrowserType type, 
									bool excludePrivateDirs,
									SA_PARAM_NN_STR const char *topDir,
									SA_PARAM_OP_STR const char *startDir,
									SA_PARAM_OP_STR const char *defaultExt,
									SA_PRE_VALID SA_POST_OP_STR char *dirOut, int dirOutLen,
									SA_PRE_VALID SA_POST_OP_STR char *fileOut, int fileOutLen, char *defaultText)
{
    static const char** topDirs = NULL;
    static const char** defaultExts = NULL;

    if( !defaultExts ) {
        eaCreate( &defaultExts );
    }
    eaSetSize( &defaultExts, 0 );
    eaPush( &defaultExts, defaultExt );
    
    if( !topDirs ) {
        eaCreate( &topDirs );
    }
    eaSetSize( &topDirs, 0 );
    eaPush( &topDirs, topDir );

    return ui_ModalFileBrowserEx(
            title, buttonText, mode, type, excludePrivateDirs, topDirs, startDir,
            defaultExts, dirOut, dirOutLen, fileOut, fileOutLen,
            NULL, NULL, defaultText );
}

UIDialogButtons ui_ModalFileBrowserEx(
        const char *title, const char *buttonText,
        UIBrowserMode mode, UIBrowserType type, bool excludePrivateDirs,
        const char **topDirs, const char *startDir, const char **defaultExts,
        char *dirOut, int dirOutLen, char *fileOut, int fileOutLen,
        UIFilterFunc filterF, UserData filterD, char *defaultText)
{
	UIGlobalState old_state;
	UIWindow *dialog;

	ui_ModalDialogBeforeWidgetAdd( &old_state );

	// add dialog
	dialog = ui_FileBrowserCreateEx(title, buttonText, mode, type, excludePrivateDirs, topDirs, startDir, defaultText, defaultExts, cancel, NULL, selectFile, NULL, filterF, filterD, NULL, false);
	ui_WindowShow(dialog);

	if (window_skin)
		ui_WidgetSkin(UI_WIDGET(dialog), window_skin);

	ui_ModalDialogLoop();

	// free dialog
	ui_FileBrowserFree();

	ui_ModalDialogAfterWidgetDestroy( &old_state );

	if (return_button == UIOk)
	{
		if (dirOut)
			strcpy_s(dirOut, dirOutLen, file_dir);
		if (fileOut)
			strcpy_s(fileOut, fileOutLen, file_name);
	}
	
	return return_button;
}

void ui_ModalDialogBeforeWidgetAdd(UIGlobalState* state)
{
	// store current ui state
	memcpy(state, &g_ui_State, sizeof(UIGlobalState));

	// clear current windows
	g_ui_State.freeQueue = NULL;
	ui_SetFocus(NULL);
	g_ui_State.focused = NULL;
	g_ui_State.states = stashTableCreateAddress(8);
	g_ui_State.cursorLock = false;
	g_ui_State.modal = 1;
}

void ui_ModalDialogLoop(void)
{
	static KeyBindProfile dialog_keybinds;
	static int keybinds_loadFromNamed = 0;
	F32 elapsed = 0;
	F32 elapsed_total=(gfxGetFrameCount() == last_modal_frame)?1:0;
	U32 frame_timer = timerAlloc();
	WorldRegion *region = NULL;
	Vec3 cam_pos;
	int i;
	GfxDummyFrameInfo frame_loop_info = { 0 };

	gfxDummyFrameSequenceStart(&frame_loop_info);

	mouseClipPushRestrict(NULL);

	// push keybinds
	if (!keybinds_loadFromNamed)
	{
		dialog_keybinds.pchName = "Modal Dialog Commands";
		dialog_keybinds.pCmdList = &dialog_cmdlist;
		dialog_keybinds.bTrickleCommands = 0;
		dialog_keybinds.bTrickleKeys = 0;
		keybinds_loadFromNamed = 1;
	}

	keybind_BindKeyInProfile(&dialog_keybinds, "AB", "Dialog.ok");
	keybind_BindKeyInProfile(&dialog_keybinds, "enter", "Dialog.ok");
	keybind_BindKeyInProfile(&dialog_keybinds, "BB", "Dialog.cancel");
	keybind_BindKeyInProfile(&dialog_keybinds, "escape", "Dialog.cancel");
	keybind_BindKeyInProfile(&dialog_keybinds, "CTRL+C", "Dialog.copy");
	keybind_PushProfileEx(&dialog_keybinds, InputBindPriorityConsole);

	gfxGetActiveCameraPos(cam_pos);

	if (!gbNo3DGraphics)
		region = worldGetWorldRegionByPos(gfxGetActiveCameraController()->camfocus);

	gfxDebugGrabFrame(1);
	if (frame_loop_info.was_ignoringInput)
		inpStopIgnoringInput();
	// render loop
	timerStart(frame_timer);
	return_button = -1;
	while (return_button < 0)
	{
		UISkin* skin = ui_GetActiveSkin();
		
		gfxDummyFrameTop(&frame_loop_info, elapsed);

		ui_OncePerFramePerDevice(elapsed, gfxGetActiveOrPrimaryDevice());
		if( skin->bModalDialogUsesBackgroundColor ) {
			int width, height;
			float modalAlphaCur = CLAMP( elapsed_total * 3, 0, 1 );
			U32 rgba = skin->iWindowModalBackgroundColor;
			U8 alpha = CLAMP( (int)(GET_COLOR_ALPHA(rgba) * modalAlphaCur), 0, 255 );
			rgba = COLOR_ALPHA(rgba, alpha);

			gfxGetActiveSurfaceSize(&width, &height);
			
			gfxDebugShowGrabbedFrame(1, 0xFF, 0);
			display_sprite( white_tex_atlas, 0, 0, 0.001, width/(float)white_tex_atlas->width, height/(float)white_tex_atlas->height, rgba );
		} else {
			gfxDebugShowGrabbedFrame(1, lerpRGBAColors(0xFFFFFFFF, 0xFFEBA5FF, CLAMPF32(elapsed_total / 0.5, 0, 1)), (elapsed_total>0.5)?0.9:(elapsed_total*0.9/0.5));
		}

		// prevent the world from unloading things around the camera while in the modaldialog
		if (!gbNo3DGraphics)
			worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, region, cam_pos, 1, NULL, false, false, gfxGetDrawHighDetailSetting(), gfxGetDrawHighFillDetailSetting(), gfxGetFrameCount(), 1.0f);

		gfxDummyFrameBottom(&frame_loop_info, NULL, false);

		elapsed = timerElapsedAndStart(frame_timer);
		elapsed_total += elapsed;
	}

	// run a couple frames without the window so there is a visual cue that another dialog is coming up
	for (i = 0; i < 5; ++i)
	{
		gfxDummyFrameTop(&frame_loop_info, elapsed);
		// prevent the world from unloading things around the camera while in the modaldialog
		if (!gbNo3DGraphics)
			worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, region, cam_pos, 1, NULL, false, false, gfxGetDrawHighDetailSetting(), gfxGetDrawHighFillDetailSetting(), gfxGetFrameCount(), 1.0f);
		gfxDummyFrameBottom(&frame_loop_info, NULL, false);

		Sleep(5);
	}
	gfxResetFrameCounters();
	gfxOncePerFrame(elapsed, elapsed, false, false);

	// prevent the world from unloading things around the camera while in the modaldialog
	if (!gbNo3DGraphics)
		worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, region, cam_pos, 1, NULL, false, false, gfxGetDrawHighDetailSetting(), gfxGetDrawHighFillDetailSetting(), gfxGetFrameCount(), 1.0f);

	// pop keybinds
	keybind_PopProfileEx(&dialog_keybinds, InputBindPriorityConsole);

	stashTableDestroy(g_ui_State.states);

	if (frame_loop_info.was_ignoringInput)
		inpBeginIgnoringInput();
	mouseClipPop();

	g_ui_State.modal = 0;
	dialog_message = NULL;
	last_modal_frame = gfxGetFrameCount();
	gfxDebugUpdateGrabbedFrameTimestamp(1);
	gfxDebugDoneWithGrabbedFrame(1);

	timerFree(frame_timer);

	gfxResetFrameCounters();

	gfxDummyFrameSequenceEnd(&frame_loop_info);
}

void ui_ModalDialogAfterWidgetDestroy(UIGlobalState* state)
{
	// restore old UI state
	memcpy(&g_ui_State, state, sizeof(UIGlobalState));
}

void ui_ModalDialogLoopExit(void)
{
	return_button = 1;
}
