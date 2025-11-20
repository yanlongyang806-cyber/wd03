#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef INPUT_TEXT_H
#define INPUT_TEXT_H

// This contains all of the external definitions needed to access the text
// input buffer. The text input buffer also contains some joystick events,
// since joystick events need to be processed like keyboard events for UI
// purposes (focus, repeat, etc).


// This is the type of key that has been entered, for text input
typedef enum
{
	KIT_None,
	KIT_EditKey,
	KIT_Text,
} KeyInputType;

// A bitfield defining the state of modifier keys
typedef enum
{
	KIA_NONE	= 0,
	KIA_CONTROL = 1 << 0,
	KIA_ALT		= 1 << 1,
	KIA_SHIFT	= 1 << 2,
	KIA_NUMLOCK = 1 << 3,
} KeyInputAttrib;

// Representation of a key press
typedef struct KeyInput
{
	KeyInputType type;

	// The scancode of the key pressed (i.e. INP_x)
	S32 scancode;

	// The UCS code point of the key pressed, if any (e.g. ASCII is 0-128).
	// This is only available if the type is KIT_Text.
	S32 character;

	KeyInputAttrib attrib;

	// The original virtual key
	S32 vkey;
} KeyInput;

KeyInput* inpGetKeyBuf(void);
void inpGetNextKey(KeyInput** input);
int GetVirtualKeyFromScanCode(int scancode);

#endif