#pragma once

// The different types of mouse buttons
AUTO_ENUM;
typedef enum
{
	MS_LEFT, ENAMES(MouseLeft)
	MS_MID, ENAMES(MouseMiddle)
	MS_RIGHT, ENAMES(MouseRight)
	MS_WHEELUP, ENAMES(MouseWheelUp)
	MS_WHEELDOWN, ENAMES(MouseWheelDown)
	MS_MAXBUTTON, EIGNORE
} MouseButton;
