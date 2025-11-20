/***************************************************************************



***************************************************************************/
#if _XBOX

#include "input.h"
#include "inputKeyboard360.h"
#include "inputCommandParse.h"

extern int GetScancodeFromVirtualKey(WPARAM wParam, LPARAM lParam);
extern void inpMouseClick(DIDEVICEOBJECTDATA *didod, S32 button, S32 clickScancode, S32 doubleClickScancode);


void inpKeyboardUpdate360(void)
{
	XINPUT_KEYSTROKE keyStroke;
	const S32 curTime = inpGetTime();
	while (ERROR_SUCCESS == XInputGetKeystroke( XUSER_INDEX_ANY, XINPUT_FLAG_KEYBOARD, &keyStroke ))
	{
		if (keyStroke.Flags & (XINPUT_KEYSTROKE_KEYDOWN | XINPUT_KEYSTROKE_REPEAT))
		{
			KeyInputAttrib attrib = 0;
			if(keyStroke.Flags & XINPUT_KEYSTROKE_CTRL)
				attrib |= KIA_CONTROL;
			if(keyStroke.Flags & XINPUT_KEYSTROKE_ALT)
				attrib |= KIA_ALT;
			if(keyStroke.Flags & XINPUT_KEYSTROKE_SHIFT)
				attrib |= KIA_SHIFT;
			inpKeyAddBuf(KIT_EditKey, GetScancodeFromVirtualKey(keyStroke.VirtualKey, 0), 0, attrib, keyStroke.VirtualKey);
			if (keyStroke.Unicode >= 32)
				inpKeyAddBuf(KIT_Text, GetScancodeFromVirtualKey(keyStroke.VirtualKey, 0), keyStroke.Unicode, attrib, keyStroke.VirtualKey);
		}

		if (keyStroke.Flags & XINPUT_KEYSTROKE_KEYDOWN)
			inpUpdateKey(GetScancodeFromVirtualKey(keyStroke.VirtualKey, 0), 1, curTime);
		else if (keyStroke.Flags & XINPUT_KEYSTROKE_KEYUP)
			inpUpdateKey(GetScancodeFromVirtualKey(keyStroke.VirtualKey, 0), 0, curTime);
	}

	// Process any pending events from the virtual keyboard.
	inpXboxUpdate();
}

void inpMouseUpdate360(void)
{
	static struct {
		UCHAR Buttons;
		SHORT X;
		SHORT Y;
		SHORT Wheel;
	} last, cur;

	if (baIsSetInline(gInput->inp_levels, INP_MOUSEWHEEL_FORWARD))
		baClearBit(gInput->inp_levels, INP_MOUSEWHEEL_FORWARD);
	if (baIsSetInline(gInput->inp_levels, INP_MOUSEWHEEL_BACKWARD))
		baClearBit(gInput->inp_levels, INP_MOUSEWHEEL_BACKWARD);

	//clear old buffer
	memset( &gInput->mouseInpBuf, 0, sizeof(mouse_input) * MOUSE_INPUT_SIZE );
	gInput->mouseBufSize = 0;
	gInput->mouseInpCur.z = 0;

	if (ERROR_SUCCESS == DmGetMouseChanges(&cur.Buttons, &cur.X, &cur.Y, &cur.Wheel))
	{
		int j;
		DIDEVICEOBJECTDATA didod = {0};

		didod.dwTimeStamp = inpGetTime();
		if ((cur.Buttons & DM_MOUSE_LBUTTON) != (last.Buttons & DM_MOUSE_LBUTTON))
		{
			didod.dwData = (cur.Buttons & DM_MOUSE_LBUTTON)?0x80:0x00;
			inpMouseClick(&didod, MS_LEFT, INP_LCLICK, INP_LDBLCLICK);
		}
		if ((cur.Buttons & DM_MOUSE_RBUTTON) != (last.Buttons & DM_MOUSE_RBUTTON))
		{
			didod.dwData = (cur.Buttons & DM_MOUSE_RBUTTON)?0x80:0x00;
			inpMouseClick(&didod, MS_RIGHT, INP_RCLICK, INP_RDBLCLICK);
		}
		if ((cur.Buttons & DM_MOUSE_MBUTTON) != (last.Buttons & DM_MOUSE_MBUTTON))
		{
			didod.dwData = (cur.Buttons & DM_MOUSE_MBUTTON)?0x80:0x00;
			inpMouseClick(&didod, MS_MID, INP_MCLICK, INP_MDBLCLICK);
		}

		// Clamp
		if (gInput->mouseInpCur.x + cur.X < 0)
			cur.X = -gInput->mouseInpCur.x;
		if (gInput->mouseInpCur.x + cur.X > gInput->screenWidth)
			cur.X = gInput->screenWidth - gInput->mouseInpCur.x;
		if (gInput->mouseInpCur.y + cur.Y < 0)
			cur.Y = -gInput->mouseInpCur.y;
		if (gInput->mouseInpCur.y + cur.Y > gInput->screenHeight)
			cur.Y = gInput->screenHeight - gInput->mouseInpCur.y;

		gInput->mouseInpCur.x += cur.X;
		gInput->mouse_dx += cur.X;
		for (j = 0; j < ARRAY_SIZE_CHECKED(gInput->buttons); j++)
			gInput->buttons[j].curx += cur.X;

		gInput->mouseInpCur.y += cur.Y;
		gInput->mouse_dy += cur.Y;
		for (j = 0; j < ARRAY_SIZE_CHECKED(gInput->buttons); j++)
			gInput->buttons[j].cury += cur.Y;

		gInput->mouseInpCur.z += cur.Wheel;
		if(cur.Wheel)
		{
			gInput->mouseInpBuf[gInput->mouseBufSize].states[cur.Wheel > 0 ? MS_WHEELUP : MS_WHEELDOWN] = MS_CLICK;
		}
		gInput->mouseInpBuf[gInput->mouseBufSize].x = gInput->mouseInpCur.x;
		gInput->mouseInpBuf[gInput->mouseBufSize].y = gInput->mouseInpCur.y;
		gInput->mouseBufSize++;

		last = cur;
	}
}


#endif // _XBOX